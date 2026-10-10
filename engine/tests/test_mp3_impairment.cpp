// OpenBFME unit tests. GPL-3.0.
// Lane MP-3 (NET-4): the network test harness without retail data. The fault injector (GameNetwork/NetImpairment.h) is reproducible per seed and link and
// meets its configured rates; the UDP Transport delivers every command once and in order through latency, jitter, loss, duplication and reordering; and
// lockstep peers (Network on real Transports over localhost, the protocol without a game: a peer's "state" is the fold of the command lists it ran, its
// CRC exchanged every 20 frames) finish with identical states under 150 ms +- 50 ms and 5 % loss, 2 and 4 of them, and through a 3 s blackout of one peer:
// the disconnect screen comes up and goes again, nobody is dropped.

#include "doctest.h"

#include "GameNetwork/Network.h"
#include "GameNetwork/NetImpairment.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("mp3 impairment: link faults parse exactly; an unknown key, a negative value or a per-mille above 1000 is an error")
{
	NetLinkFaults f;
	std::string error;
	REQUIRE_MESSAGE(NetLinkFaults::parse("latency=150,jitter=50,loss=50,dup=10,reorder=20,reorder_ms=80", f, &error), error);
	CHECK(f.latencyMs == 150);
	CHECK(f.jitterMs == 50);
	CHECK(f.lossPerMille == 50);
	CHECK(f.duplicatePerMille == 10);
	CHECK(f.reorderPerMille == 20);
	CHECK(f.reorderMs == 80);
	CHECK(f.text() == "latency=150,jitter=50,loss=50,dup=10,reorder=20,reorder_ms=80");
	REQUIRE(NetLinkFaults::parse("", f, &error));
	CHECK(!f.any());
	for (const char *bad : { "latency", "latency=-1", "loss=1001", "lag=5", "jitter=5x", "dup=" })
	{
		INFO(bad);
		NetLinkFaults g;
		g.latencyMs = 7;
		error.clear();
		CHECK(!NetLinkFaults::parse(bad, g, &error));
		CHECK(!error.empty());
		CHECK(g.latencyMs == 7); // untouched
	}
}

TEST_CASE("mp3 impairment: the fate of the n-th datagram on a link depends on the seed and the link only; the rates are met; a blackout swallows "
		  "both directions")
{
	NetImpairment::Config c;
	c.seed = 42;
	c.all.latencyMs = 150;
	c.all.jitterMs = 50;
	c.all.lossPerMille = 50;
	c.all.duplicatePerMille = 20;
	c.all.reorderPerMille = 30;
	c.linkSet[3] = true; // slot 3 has a clean link
	NetImpairment a(c), b(c);
	c.seed = 43;
	NetImpairment other(c);
	const int kDatagrams = 20000;
	std::vector<std::uint64_t> ta, tb, to;
	int differ = 0, copies = 0, lost = 0, dups = 0, late = 0;
	std::uint64_t minDelay = ~0ull, maxDelay = 0;
	for (int i = 0; i < kDatagrams; ++i)
	{
		const std::uint64_t now = 1000 + (std::uint64_t)i;
		// interleaved with traffic on another link: it must not change link 1's sequence on `a`
		a.schedule(2, now, to);
		a.schedule(1, now, ta);
		b.schedule(1, now, tb);
		REQUIRE(ta == tb);
		other.schedule(1, now, to);
		differ += to != ta ? 1 : 0;
		copies += (int)ta.size();
		lost += ta.empty() ? 1 : 0;
		dups += ta.size() == 2 ? 1 : 0;
		for (std::uint64_t t : ta)
		{
			const std::uint64_t d = t - now;
			if (d > 200)
			{
				++late; // reordered: held back 2 * 50 + 20 more
				CHECK(d >= 220);
				CHECK(d <= 320);
			}
			else
			{
				minDelay = std::min(minDelay, d);
				maxDelay = std::max(maxDelay, d);
			}
		}
	}
	CHECK(differ > kDatagrams / 2);
	// 5 % +- 1 %, 2 % +- 0.6 %, 3 % +- 0.8 % of 20000 (binomial: several standard deviations)
	CHECK(lost > 800);
	CHECK(lost < 1200);
	CHECK(dups > 280);
	CHECK(dups < 520);
	CHECK(late > 400);
	CHECK(late < 800);
	CHECK(minDelay == 100);
	CHECK(maxDelay == 200);
	CHECK(a.stats().lost > (unsigned long long)lost); // link 2's losses are counted too
	// the clean link and a non-peer datagram go out at once
	a.schedule(3, 5, ta);
	CHECK(ta == std::vector<std::uint64_t>{ 5 });
	a.schedule(-1, 6, ta);
	CHECK(ta == std::vector<std::uint64_t>{ 6 });
	// a blackout from 100 for 3000 ms: nothing out, nothing in, nothing due released; afterwards the faults apply again
	NetImpairment clean;
	CHECK(!clean.active());
	clean.blackout(100, 3000);
	CHECK(clean.active());
	clean.schedule(1, 100, ta);
	CHECK(ta.empty());
	clean.schedule(1, 3099, ta);
	CHECK(ta.empty());
	CHECK(!clean.acceptIncoming(2000));
	CHECK(!clean.releaseDue(3099));
	CHECK(clean.acceptIncoming(3100));
	CHECK(clean.releaseDue(3100));
	clean.schedule(1, 3100, ta);
	CHECK(ta == std::vector<std::uint64_t>{ 3100 });
	CHECK(clean.stats().blackoutOut == 3);
	CHECK(clean.stats().blackoutIn == 1);
	CHECK(clean.stats().blackouts == 1);
}

TEST_CASE("mp3 impairment: the UDP transport delivers every command exactly once and in order through 150 ms +- 50 ms, 5 % loss, duplicates and "
		  "reordering, fragmented commands included")
{
	UDP sa, sb;
	std::string error;
	REQUIRE_MESSAGE(sa.bind(0x7F000001u, 0, &error), error);
	REQUIRE_MESSAGE(sb.bind(0x7F000001u, 0, &error), error);
	Transport::Options o;
	o.packetBytes = 300; // commands above ~270 bytes travel in fragments
	REQUIRE(NetLinkFaults::parse("latency=150,jitter=50,loss=50,dup=30,reorder=30", o.impairment.all, &error));
	o.impairment.seed = 9;
	Transport ta(sa, 0, o);
	o.impairment.seed = 10;
	Transport tb(sb, 1, o);
	ta.setPeer(1, sb.localAddress());
	tb.setPeer(0, sa.localAddress());
	const int kCommands = 400;
	std::vector<std::vector<std::uint8_t>> sent;
	for (int i = 0; i < kCommands; ++i)
	{
		std::vector<std::uint8_t> bytes(1 + (size_t)((i * 37) % 900), (std::uint8_t)i);
		bytes[0] = (std::uint8_t)(i >> 8);
		sent.push_back(bytes);
	}
	std::vector<std::vector<std::uint8_t>> got;
	size_t next = 0;
	const std::uint64_t t0 = NetMilliseconds();
	while ((got.size() < sent.size() || !ta.allAcked(1)) && NetMilliseconds() - t0 < 60000)
	{
		// a burst of 8 commands every 20 ms
		for (int k = 0; k < 8 && next < sent.size(); ++k)
		{
			REQUIRE(ta.sendTo(1, sent[next++], &error));
		}
		ta.service();
		tb.service();
		int from = -1;
		std::vector<std::uint8_t> b;
		while (tb.receive(from, b))
		{
			CHECK(from == 0);
			got.push_back(b);
		}
		NetSleepMilliseconds(next < sent.size() ? 20 : 2);
	}
	REQUIRE(got.size() == sent.size());
	CHECK(got == sent);
	CHECK(ta.errors().empty());
	CHECK(tb.errors().empty());
	CHECK(ta.stats().fragmentedCommands > 0);
	CHECK(tb.stats().duplicates > 0); // the duplicated datagrams were seen and ignored
	CHECK(ta.impairment().stats().lost > 0);
	CHECK(ta.impairment().stats().duplicated > 0);
	CHECK(ta.impairment().stats().reordered > 0);
	CHECK(ta.stats().commandsResent > 0);
	CHECK(ta.roundTripMs(1) >= 150); // two directions of 150 +- 50 ms (200 at least; the transport reads its clock once per service call, so a loaded
	                                  // machine measures up to a call's duration less)
	MESSAGE("a -> b: " << ta.impairment().statsText() << "; resent " << ta.stats().commandsResent << ", round trip " << ta.roundTripMs(1) << " ms");
}

namespace
{
// one lockstep peer without a game: a Network on a real Transport; its "state" is a fold of every command it ran (what a real game's state is a function
// of), sent as its CRC every kCrc frames
constexpr int kCrc = 20;

struct LivePeer
{
	UDP socket;
	std::unique_ptr<Transport> transport;
	std::unique_ptr<Network> net;
	UnsignedInt frame = 0;
	std::uint32_t state = 0x811C9DC5u;
	CommandList pending;
	std::uint64_t nextDue = 0;
	std::vector<std::uint32_t> states; ///< the state after every frame
	bool sawScreen = false;
	bool left = false; ///< the player left (Network::leave): it runs no more frames, its transport still answers
	unsigned long long maxStall = 0;
	std::uint64_t waitStart = 0;
	void fold(std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
		{
			state = (state ^ ((v >> (8 * i)) & 0xFFu)) * 0x01000193u;
		}
	}
};

struct LiveGameNet
{
	std::vector<std::unique_ptr<LivePeer>> peers;
	int frameMs;
	LiveGameNet(int n, const NetLinkFaults &faults, int frameMs_, std::uint32_t disconnectMs)
		: frameMs(frameMs_)
	{
		std::string error;
		for (int i = 0; i < n; ++i)
		{
			peers.push_back(std::make_unique<LivePeer>());
			REQUIRE_MESSAGE(peers.back()->socket.bind(0x7F000001u, 0, &error), error);
		}
		for (int i = 0; i < n; ++i)
		{
			LivePeer &p = *peers[(size_t)i];
			Transport::Options o;
			o.impairment.all = faults;
			o.impairment.seed = 100u + (std::uint32_t)i;
			p.transport = std::make_unique<Transport>(p.socket, i, o);
			NetworkConfig c;
			c.localSlot = i;
			for (int j = 0; j < n; ++j)
			{
				c.slotPlayerIndex[(size_t)j] = 3 + j;
				c.slotNames[(size_t)j] = u"Player" + std::u16string(1, (char16_t)(u'0' + j));
				if (j != i)
				{
					p.transport->setPeer(j, peers[(size_t)j]->socket.localAddress());
				}
			}
			c.runAhead = 1; // review r1: from 1, so a measured round trip of 300+ ms must raise it
			c.adaptiveRunAhead = true;
			c.crcInterval = kCrc;
			c.settings.disconnectTime = disconnectMs;
			c.settings.playerTimeoutTime = 60000;
			p.net = std::make_unique<Network>(c, *p.transport);
		}
		for (auto &p : peers)
		{
			p->net->sendLoadComplete();
		}
	}
	// every peer runs to `frames` at one frame per frameMs of its own clock (a frame that is not ready waits: a stall); `hook(t)` runs every loop
	void run(UnsignedInt frames, std::uint64_t timeoutMs, const std::function<void(LivePeer &, int)> &hook = {})
	{
		const std::uint64_t t0 = NetMilliseconds();
		bool all = false;
		while (!all && NetMilliseconds() - t0 < timeoutMs)
		{
			all = true;
			for (size_t i = 0; i < peers.size(); ++i)
			{
				LivePeer &p = *peers[i];
				if (hook)
				{
					hook(p, (int)i);
				}
				if (p.left)
				{
					p.transport->service(); // the last acks
					continue;
				}
				const std::uint64_t now = NetMilliseconds();
				p.net->update(p.frame, p.pending);
				p.sawScreen = p.sawScreen || p.net->disconnectManager().screen().visible;
				if (p.frame >= frames)
				{
					continue;
				}
				all = false;
				if (now < p.nextDue)
				{
					continue;
				}
				if (!p.net->allLoaded() || !p.net->isFrameReady(p.frame) || !p.net->allowedToContinue(p.frame))
				{
					p.waitStart = p.waitStart ? p.waitStart : now;
					continue;
				}
				if (p.waitStart && p.frame > 0)
				{
					if (std::getenv("MP3_STALL_TRACE") && now - p.waitStart > 300)
					{
						std::printf("peer %zu frame %u stalled %llu ms run-ahead %d rtt %d\n", i, p.frame, (unsigned long long)(now - p.waitStart), p.net->currentRunAhead(), p.transport->roundTripMs(i == 0 ? 1 : 0));
					}
					p.maxStall = std::max<unsigned long long>(p.maxStall, now - p.waitStart);
				}
				p.waitStart = 0;
				CommandList out;
				p.net->relayCommands(p.frame, out);
				for (const GameMessage &m : out.messages())
				{
					p.fold((std::uint32_t)m.getType());
					p.fold((std::uint32_t)m.getPlayerIndex());
					for (size_t a = 0; a < m.getArgumentCount(); ++a)
					{
						if (m.getArgument(a)->type == ARGUMENTDATATYPE_INTEGER && m.getType() != MSG_LOGIC_CRC)
						{
							p.fold((std::uint32_t)m.getArgument(a)->integer);
						}
					}
				}
				p.states.push_back(p.state);
				++p.frame;
				p.nextDue = (p.nextDue ? p.nextDue : now) + (std::uint64_t)frameMs;
				// the CRC step after the frame (Network::frameCompleted), then the player's own commands of this frame
				FrameCompletion c;
				c.batchFrame = p.frame - 1;
				c.frame = p.frame;
				c.hashed = c.breakdown = true;
				c.hash = p.state;
				p.net->frameCompleted(c, p.pending);
				if ((p.frame + i) % 3 == 0)
				{
					GameMessage m(MSG_DO_STOP, 0);
					p.pending.append(m);
					GameMessage n(MSG_DO_MOVETO, 0);
					n.appendIntegerArgument((int)(p.frame * 7 + i));
					p.pending.append(n);
				}
				p.net->update(p.frame, p.pending); // as LockstepDriver::completed: the CRC and the commands go out at once
			}
			NetSleepMilliseconds(1);
		}
	}
};

void checkSameGame(LiveGameNet &g, UnsignedInt frames)
{
	for (size_t i = 0; i < g.peers.size(); ++i)
	{
		LivePeer &p = *g.peers[i];
		INFO("peer " << i);
		CHECK(p.frame == frames);
		CHECK(p.net->desyncs().empty());
		CHECK(p.net->errors().empty());
		CHECK(p.transport->errors().empty());
		CHECK(p.net->crcChecksPassed() == (unsigned long long)(frames / kCrc) - 1); // the CRC of the last interval runs after the end
		CHECK(!p.net->quitRequested());
		for (int s = 0; s < (int)g.peers.size(); ++s)
		{
			CHECK(!p.net->isDisconnected(s));
		}
		CHECK(p.states == g.peers[0]->states);
		MESSAGE("peer " << i << ": run-ahead " << p.net->currentRunAhead() << " (" << p.net->runAheadChanges() << " changes), worst stall " << p.maxStall
						<< " ms, " << p.transport->impairment().statsText() << ", resent " << p.transport->stats().commandsResent);
	}
}
} // namespace

TEST_CASE("mp3 lockstep under faults: 2 peers and 4 peers on localhost, 150 ms +- 50 ms one way and 5 % loss, finish with identical states and every "
		  "CRC passed")
{
	NetLinkFaults f;
	std::string error;
	REQUIRE(NetLinkFaults::parse("latency=150,jitter=50,loss=50", f, &error));
	for (int n : { 2, 4 })
	{
		INFO(n << " peers");
		const UnsignedInt kFrames = 200;
		LiveGameNet g(n, f, 50, 5000);
		g.run(kFrames, 90000);
		checkSameGame(g, kFrames);
		for (auto &p : g.peers)
		{
			CHECK(!p->sawScreen);
			CHECK(p->transport->impairment().stats().lost > 0);
			CHECK(p->net->runAheadChanges() >= 1); // the router measured the round trip (300 +- 100 ms) and changed the command delay
			CHECK(p->transport->stats().fastResends > 0);
		}
	}
}

TEST_CASE("mp3 lockstep under faults: a 3 s blackout of one of 4 peers brings up the disconnect screen on every peer and it goes again; nobody is "
		  "dropped and the states stay identical")
{
	NetLinkFaults f;
	std::string error;
	REQUIRE(NetLinkFaults::parse("latency=150,jitter=50,loss=50", f, &error));
	const UnsignedInt kFrames = 260;
	LiveGameNet g(4, f, 50, 1000); // NetworkDisconnectTime 1 s (retail 15 s: a 3 s blackout would only stall)
	bool cut = false;
	std::vector<bool> screenDuring(4, false);
	std::uint64_t cutAt = 0;
	g.run(kFrames, 120000, [&](LivePeer &p, int i) {
		if (!cut && i == 2 && p.frame >= 80)
		{
			cut = true;
			cutAt = NetMilliseconds();
			p.transport->impairment().blackout(cutAt, 3000);
		}
		if (cut && NetMilliseconds() - cutAt < 3500 && p.net->disconnectManager().screen().visible)
		{
			screenDuring[(size_t)i] = true;
		}
	});
	REQUIRE(cut);
	checkSameGame(g, kFrames);
	for (size_t i = 0; i < g.peers.size(); ++i)
	{
		INFO("peer " << i);
		LivePeer &p = *g.peers[i];
		CHECK(screenDuring[i]);
		CHECK(!p.net->disconnectManager().screen().visible);
		CHECK(p.net->disconnectManager().dropped().empty());
		CHECK(p.maxStall >= 2000); // the game held while slot 2 was cut off
		bool on = false, off = false;
		for (const std::string &l : p.net->disconnectManager().log())
		{
			on = on || l.find("disconnect screen on") != std::string::npos;
			off = off || l.find("disconnect screen off") != std::string::npos;
		}
		CHECK(on);
		CHECK(off);
	}
	CHECK(g.peers[2]->transport->impairment().stats().blackoutIn > 0);
	CHECK(g.peers[2]->transport->impairment().stats().blackoutOut > 0);
}

TEST_CASE("mp3 impairment: the lane's acceptance stops are reported (S-1890 .. S-1892)")
{
	const std::vector<std::string> stops = NetImpairment::stopLines();
	REQUIRE(stops.size() == 3);
	CHECK(stops[0].rfind("[S-1890] network faults:", 0) == 0);
	CHECK(stops[1].rfind("[S-1891] resend policy:", 0) == 0);
	CHECK(stops[2].rfind("[S-1892] smoothness:", 0) == 0);
	CHECK(stops[2].find("RW 0x4E03C7") != std::string::npos);
}

TEST_CASE("mp3 lockstep under faults (review r1): the packet router leaves gracefully; the next slot of the fallback order is the router from the leave "
		  "frame on on every survivor, its run-ahead changes are taken, and the survivors stay identical")
{
	NetLinkFaults f;
	std::string error;
	REQUIRE(NetLinkFaults::parse("latency=150,jitter=50,loss=50", f, &error));
	const UnsignedInt kFrames = 260;
	LiveGameNet g(3, f, 50, 5000);
	UnsignedInt leaveFrame = 0;
	std::vector<int> changesAtLeave(3, -1);
	g.run(kFrames, 120000, [&](LivePeer &p, int i) {
		if (i == 0 && !p.left && p.frame >= 80)
		{
			p.net->leave(p.frame); // ZH handleLocalPlayerLeaving: PLAYERLEAVE at the execution frame + 1
			p.left = true;
			leaveFrame = p.frame;
			for (int k = 1; k < 3; ++k)
			{
				changesAtLeave[(size_t)k] = g.peers[(size_t)k]->net->runAheadChanges();
			}
		}
	});
	REQUIRE(g.peers[0]->left);
	for (int i = 1; i < 3; ++i)
	{
		INFO("survivor " << i);
		LivePeer &p = *g.peers[(size_t)i];
		CHECK(p.frame == kFrames);
		for (const DesyncReport &d : p.net->desyncs())
		{
			MESSAGE(d.text().substr(0, 600));
		}
		CHECK(p.net->desyncs().empty());
		CHECK(p.net->errors().empty()); // a RUNAHEAD of a slot that is not the frame's router would be an error
		CHECK(p.net->hasLeft(0));
		CHECK(p.net->packetRouterSlot() == 1);
		CHECK(p.net->nextPacketRouterSlot(1) == 2); // the fallback order without slot 0
		CHECK(!p.net->quitRequested());
		CHECK(p.states == g.peers[1]->states);
		MESSAGE("survivor " << i << ": run-ahead " << p.net->currentRunAhead() << ", changes " << changesAtLeave[(size_t)i] << " at the leave (frame "
							<< leaveFrame << "), " << p.net->runAheadChanges() << " at the end");
	}
	// the new router adapted the command delay after the leave (the run-ahead starts at 1 and the old router's last value is not the measured one for
	// two peers at the end of the game: at least the successor's own measurement is taken)
	CHECK(g.peers[1]->net->runAheadChanges() > changesAtLeave[1]);
	CHECK(g.peers[1]->net->runAheadChanges() == g.peers[2]->net->runAheadChanges());
	// up to its leave the leaver ran the survivors' frames
	REQUIRE(g.peers[0]->states.size() >= 80);
	CHECK(std::equal(g.peers[0]->states.begin(), g.peers[0]->states.end(), g.peers[1]->states.begin()));
}

namespace
{
// an in-memory transport whose received commands the test queues in the order it wants
struct InboxTransport : NetTransport
{
	std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
	bool sendTo(int, const std::vector<std::uint8_t> &, std::string *) override { return true; }
	bool receive(int &from, std::vector<std::uint8_t> &bytes) override
	{
		if (inbox.empty())
		{
			return false;
		}
		from = inbox.front().first;
		bytes = inbox.front().second;
		inbox.pop_front();
		return true;
	}
};

std::vector<std::uint8_t> leaveOf(int slot, UnsignedInt frame)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_PLAYERLEAVE;
	c.slot = (std::uint8_t)slot;
	c.executionFrame = frame;
	std::string error;
	std::vector<std::uint8_t> b = NetPacket::encodeCommand(c, &error);
	REQUIRE_MESSAGE(!b.empty(), error);
	return b;
}
} // namespace

TEST_CASE("mp3 network (review r3): the packet router of every frame follows from the departure frames alone: slot 0 leaving at frame 100 and slot 1 "
		  "at 102 give the same routers whichever leave arrives first")
{
	std::vector<std::vector<int>> routers;
	for (int order = 0; order < 2; ++order)
	{
		for (int local : { 2, 3 })
		{
			NetworkConfig c;
			c.localSlot = local;
			for (int s = 0; s < 4; ++s)
			{
				c.slotPlayerIndex[(size_t)s] = 3 + s;
			}
			InboxTransport t;
			Network n(c, t);
			if (order == 0)
			{
				t.inbox.push_back({ 0, leaveOf(0, 100) });
				t.inbox.push_back({ 1, leaveOf(1, 102) });
			}
			else
			{
				t.inbox.push_back({ 1, leaveOf(1, 102) });
				t.inbox.push_back({ 0, leaveOf(0, 100) });
			}
			CommandList pending;
			n.update(0, pending);
			INFO("arrival order " << order << ", local slot " << local);
			CHECK(n.errors().empty());
			CHECK(n.hasLeft(0));
			CHECK(n.hasLeft(1));
			std::vector<int> r;
			for (UnsignedInt f = 98; f <= 104; ++f)
			{
				r.push_back(n.packetRouterAt(f));
			}
			// the agreed order 0, 1, 2, 3: slot 0 until frame 99, slot 1 at 100 and 101 (it leaves from 102), then slot 2
			CHECK(r == std::vector<int>{ 0, 0, 1, 1, 2, 2, 2 });
			CHECK(n.packetRouterSlot() == 2);
			CHECK(n.nextPacketRouterSlot(2) == 3);
			routers.push_back(r);
		}
	}
	for (const std::vector<int> &r : routers)
	{
		CHECK(r == routers.front());
	}
}
