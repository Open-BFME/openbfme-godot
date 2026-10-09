// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: the disconnect path without retail data (GameNetwork/DisconnectManager.h): a peer that goes silent brings up the disconnect screen on the others
// after NetworkDisconnectTime, its timeout bar runs down, the remaining players vote (Kick, or the automatic vote at 0 %) and the next packet router drops it
// at the frame every remaining peer is on: DESTROYPLAYER becomes MSG_SELF_DESTRUCT { TRUE } of its player in the same frame everywhere and the game goes on;
// a peer that comes back in time ends the screen without a drop; a peer that is behind gets the frames it missed from one that ran them; the new net
// commands survive the encoding. The expectations follow the RotWK addresses in DisconnectManager.h.

#include "doctest.h"

#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/NetworkSettings.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
// an in-memory network with a shared test clock: every datagram arrives a tick later (reliable, ordered, like Transport); a cut link holds its datagrams
// until it is restored (a stalled or dead peer: the transport resends what was not acknowledged)
class Hub
{
public:
	struct Endpoint : NetTransport
	{
		Hub *hub = nullptr;
		int slot = 0;
		std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
		std::set<int> retired;
		std::map<int, std::uint64_t> heard;
		bool sendTo(int to, const std::vector<std::uint8_t> &bytes, std::string *) override
		{
			if (retired.count(to))
			{
				return false;
			}
			hub->queue(slot, to, bytes);
			return true;
		}
		void retire(int to) override { retired.insert(to); }
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
		std::uint64_t lastHeardFrom(int s) const override
		{
			const auto it = heard.find(s);
			return it == heard.end() ? 0 : it->second;
		}
		int rtt = -1, rttVar = 0; // the measured round trip the endpoint reports (the adaptive run-ahead test)
		int roundTripMs(int) const override { return rtt; }
		int roundTripVarMs(int) const override { return rttVar; }
	};
	explicit Hub(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			auto e = std::make_unique<Endpoint>();
			e->hub = this;
			e->slot = i;
			m_endpoints.push_back(std::move(e));
		}
	}
	Endpoint &endpoint(int slot) { return *m_endpoints[(size_t)slot]; }
	void cut(int from, int to, bool c) { m_cut[{ from, to }] = c; }
	void silence(int slot, bool s)
	{
		for (int i = 0; i < (int)m_endpoints.size(); ++i)
		{
			cut(slot, i, s);
			cut(i, slot, s);
		}
	}
	void queue(int from, int to, const std::vector<std::uint8_t> &b) { m_inFlight.push_back(Flight{ m_now + 1, from, to, b }); }
	// one tick: the clock moves `ms`; what is due arrives; a peer that is not silenced is "heard" (the transport's keep-alives) by every other
	void tick(std::uint64_t ms)
	{
		++m_now;
		clock += ms;
		std::deque<Flight> keep;
		std::set<std::pair<int, int>> held;
		for (Flight &f : m_inFlight)
		{
			if (f.at <= m_now && !m_cut[{ f.from, f.to }] && !held.count({ f.from, f.to }))
			{
				m_endpoints[(size_t)f.to]->inbox.push_back({ f.from, f.bytes });
			}
			else if (f.at <= m_now)
			{
				held.insert({ f.from, f.to }); // keeps the link's order: nothing after a held datagram overtakes it
				keep.push_back(f);
			}
			else
			{
				keep.push_back(f);
			}
		}
		m_inFlight.swap(keep);
		for (int a = 0; a < (int)m_endpoints.size(); ++a)
		{
			for (int b = 0; b < (int)m_endpoints.size(); ++b)
			{
				if (a != b && !m_cut[{ b, a }])
				{
					m_endpoints[(size_t)a]->heard[b] = clock;
				}
			}
		}
	}
	std::uint64_t clock = 1000;

private:
	struct Flight
	{
		int at, from, to;
		std::vector<std::uint8_t> bytes;
	};
	std::vector<std::unique_ptr<Endpoint>> m_endpoints;
	std::map<std::pair<int, int>, bool> m_cut;
	std::deque<Flight> m_inFlight;
	int m_now = 0;
};

NetworkConfig configFor(int slot, int peers)
{
	NetworkConfig c;
	c.localSlot = slot;
	for (int i = 0; i < peers; ++i)
	{
		c.slotPlayerIndex[(size_t)i] = 10 + i;
		c.slotNames[(size_t)i] = u"Player" + std::u16string(1, (char16_t)(u'0' + i));
	}
	c.runAhead = 2;
	c.crcInterval = 0;
	c.settings.disconnectTime = 1000;
	c.settings.playerTimeoutTime = 6000;
	return c;
}

// one peer's game loop: the network pumped every tick, a frame relayed when it is ready and allowed
struct Peer
{
	std::unique_ptr<Network> net;
	UnsignedInt frame = 0;
	bool running = true;
	CommandList pending;
	std::vector<std::pair<UnsignedInt, std::vector<std::string>>> relayed; ///< frame -> its commands ("type player")
	std::vector<UnsignedInt> selfDestructFrames;
	void step(UnsignedInt stopAt)
	{
		if (!running)
		{
			return;
		}
		net->update(frame, pending);
		if (frame < stopAt && net->isFrameReady(frame) && net->allowedToContinue(frame))
		{
			CommandList out;
			net->relayCommands(frame, out);
			std::vector<std::string> cmds;
			for (const GameMessage &m : out.messages())
			{
				cmds.push_back(std::to_string(m.getType()) + " " + std::to_string(m.getPlayerIndex()));
				if (m.getType() == MSG_SELF_DESTRUCT)
				{
					selfDestructFrames.push_back(frame);
				}
			}
			relayed.push_back({ frame, cmds });
			++frame;
		}
	}
};

struct Game
{
	Hub hub;
	std::vector<Peer> peers;
	explicit Game(int n)
		: hub(n)
		, peers((size_t)n)
	{
		for (int i = 0; i < n; ++i)
		{
			peers[(size_t)i].net = std::make_unique<Network>(configFor(i, n), hub.endpoint(i));
			Hub *h = &hub;
			peers[(size_t)i].net->setClock([h] { return h->clock; });
		}
		for (Peer &p : peers)
		{
			p.net->sendLoadComplete();
		}
	}
	void tick(UnsignedInt stopAt, std::uint64_t ms = 50)
	{
		for (Peer &p : peers)
		{
			p.step(stopAt);
		}
		hub.tick(ms);
	}
};
} // namespace

TEST_CASE("mp2 disconnect: the new net commands survive the encoding; a slot outside 0 .. 7 is refused")
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DISCONNECTSCREENOFF;
	c.slot = 3;
	c.frame = 777;
	c.releasedMask = 0x05;
	c.releasedFrames = { 11, 0, 13, 0, 0, 0, 0, 0 };
	std::string error;
	std::vector<std::uint8_t> b = NetPacket::encodeCommand(c, &error);
	REQUIRE_MESSAGE(!b.empty(), error);
	NetCommandMsg d;
	REQUIRE_MESSAGE(NetPacket::decodeCommand(b, d, &error), error);
	CHECK(d.type == NETCOMMANDTYPE_DISCONNECTSCREENOFF);
	CHECK(d.frame == 777);
	CHECK(d.releasedMask == 0x05);
	CHECK(d.releasedFrames[0] == 11);
	CHECK(d.releasedFrames[2] == 13);
	for (NetCommandType t : { NETCOMMANDTYPE_DESTROYPLAYER, NETCOMMANDTYPE_DISCONNECTPLAYER, NETCOMMANDTYPE_DISCONNECTVOTE, NETCOMMANDTYPE_DISCONNECTFRAME,
			 NETCOMMANDTYPE_DISCONNECTKEEPALIVE })
	{
		NetCommandMsg e;
		e.type = t;
		e.slot = 1;
		e.executionFrame = 42;
		e.targetSlot = 6;
		e.frame = 99;
		b = NetPacket::encodeCommand(e, &error);
		REQUIRE(!b.empty());
		NetCommandMsg f;
		REQUIRE_MESSAGE(NetPacket::decodeCommand(b, f, &error), error);
		CHECK(f.type == t);
		CHECK(f.slot == 1);
		CHECK(f.executionFrame == 42);
		if (t == NETCOMMANDTYPE_DESTROYPLAYER || t == NETCOMMANDTYPE_DISCONNECTPLAYER || t == NETCOMMANDTYPE_DISCONNECTVOTE)
		{
			CHECK(f.targetSlot == 6);
		}
		if (t == NETCOMMANDTYPE_DISCONNECTPLAYER || t == NETCOMMANDTYPE_DISCONNECTVOTE || t == NETCOMMANDTYPE_DISCONNECTFRAME)
		{
			CHECK(f.frame == 99);
		}
	}
	NetCommandMsg r;
	r.type = NETCOMMANDTYPE_FRAMERESEND;
	r.slot = 0;
	r.targetSlot = 2;
	r.frame = 50;
	auto m = std::make_shared<GameMessage>(MSG_DO_MOVETO, 12);
	m->appendLocationArgument(Coord3D{ 1.0f, 2.0f, 3.0f });
	r.messages = { m, m };
	b = NetPacket::encodeCommand(r, &error);
	REQUIRE(!b.empty());
	NetCommandMsg rr;
	REQUIRE_MESSAGE(NetPacket::decodeCommand(b, rr, &error), error);
	CHECK(rr.messages.size() == 2);
	CHECK(rr.messages[1]->getPlayerIndex() == 12);
	b[6] = 9; // the target slot byte of a DESTROYPLAYER / FRAMERESEND is right after the 6-byte header
	CHECK(!NetPacket::decodeCommand(b, rr, &error));
	CHECK(error.find("about slot 9") != std::string::npos);
}

TEST_CASE("mp2 disconnect: a peer that goes silent is dropped after its timeout: the screen, the bars, the automatic vote, the drop at one frame, "
		  "MSG_SELF_DESTRUCT of its player in the same frame on every survivor")
{
	Game g(3);
	for (int t = 0; t < 60; ++t)
	{
		g.tick(1000);
	}
	CHECK(g.peers[0].frame > 20);
	// slot 2 vanishes (its process stopped: nothing in or out)
	g.hub.silence(2, true);
	g.peers[2].running = false;
	bool sawScreen = false, sawLowBar = false, sawKick = false;
	for (int t = 0; t < 400; ++t)
	{
		g.tick(1000);
		const DisconnectManager::Screen &s = g.peers[1].net->disconnectManager().screen();
		if (s.visible)
		{
			sawScreen = true;
			// slot 2 is the second row (translated position 1) of slot 1's screen; slot 0 the first
			CHECK(s.rows[0].slot == 0);
			CHECK(s.rows[1].slot == 2);
			CHECK(s.rows[1].name == u"Player2");
			sawLowBar = sawLowBar || s.rows[1].barPercent < 50;
			sawKick = sawKick || s.rows[1].kickShown;
		}
	}
	for (int t = 0; t < 2000 && (g.peers[0].frame < 1000 || g.peers[1].frame < 1000); ++t)
	{
		g.tick(1000);
	}
	CHECK(sawScreen);
	CHECK(sawLowBar);
	CHECK(sawKick);
	for (int i = 0; i < 2; ++i)
	{
		Network &n = *g.peers[(size_t)i].net;
		INFO("peer " << i);
		CHECK(n.isDisconnected(2));
		CHECK(n.errors().empty());
		CHECK(!n.quitRequested());
		CHECK(!n.disconnectManager().screen().visible);
		REQUIRE(g.peers[(size_t)i].selfDestructFrames.size() == 1);
		CHECK(n.leftGameNotices().size() == 1);
	}
	for (const std::string &l : g.peers[0].net->disconnectManager().log())
	{
		MESSAGE("peer 0: " << l);
	}
	CHECK(g.peers[0].selfDestructFrames == g.peers[1].selfDestructFrames);
	// the next packet router (slot 0, the first of the fallback order) dropped it
	CHECK(g.peers[0].net->disconnectManager().dropped() == std::vector<int>{ 2 });
	CHECK(g.peers[1].net->disconnectManager().dropped().empty());
	// every frame both survivors ran has the same command list, and the game went on after the drop
	CHECK(g.peers[0].frame == 1000);
	CHECK(g.peers[1].frame == 1000);
	REQUIRE(g.peers[0].relayed.size() == g.peers[1].relayed.size());
	CHECK(g.peers[0].relayed == g.peers[1].relayed);
	// the self destruct is the victim's player (10 + 2) and executes after the drop frame
	bool found = false;
	for (const auto &f : g.peers[0].relayed)
	{
		for (const std::string &c : f.second)
		{
			found = found || c == std::to_string(MSG_SELF_DESTRUCT) + " 12";
		}
	}
	CHECK(found);
}

TEST_CASE("mp2 disconnect: the remaining players' Kick votes drop a silent peer before its timeout")
{
	Game g(3);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	g.hub.silence(1, true);
	g.peers[1].running = false;
	int ticksToScreen = -1;
	for (int t = 0; t < 200 && !g.peers[0].net->isDisconnected(1); ++t)
	{
		g.tick(1000);
		if (ticksToScreen < 0 && g.peers[0].net->disconnectManager().screen().visible && g.peers[2].net->disconnectManager().screen().visible)
		{
			ticksToScreen = t;
			g.peers[0].net->voteForPlayerDisconnect(1); // both press Kick at once
			g.peers[2].net->voteForPlayerDisconnect(1);
		}
		if (ticksToScreen >= 0)
		{
			CHECK(t - ticksToScreen < 40); // far below the 6 s timeout (120 ticks)
		}
	}
	for (int t = 0; t < 20; ++t)
	{
		g.tick(400);
	}
	CHECK(ticksToScreen >= 0);
	for (int i : { 0, 2 })
	{
		for (const std::string &l : g.peers[(size_t)i].net->disconnectManager().log())
		{
			MESSAGE("peer " << i << ": " << l);
		}
	}
	CHECK(g.peers[0].net->isDisconnected(1));
	CHECK(g.peers[2].net->isDisconnected(1));
	for (int t = 0; t < 1000 && (g.peers[0].frame < 400 || g.peers[2].frame < 400); ++t)
	{
		g.tick(400);
	}
	CHECK(g.peers[0].relayed == g.peers[2].relayed);
	CHECK(g.peers[0].frame == 400);
	CHECK(g.peers[0].selfDestructFrames.size() == 1);
	CHECK(g.peers[0].net->errors().empty());
	CHECK(g.peers[2].net->errors().empty());
}

TEST_CASE("mp2 disconnect: a peer that comes back before its timeout ends the screen without a drop and every peer runs the same frames")
{
	Game g(3);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	g.hub.silence(2, true);
	bool screen = false;
	for (int t = 0; t < 60; ++t) // 3 s of silence: the screen (after 1 s) but no timeout (6 s)
	{
		g.tick(1000);
		screen = screen || g.peers[0].net->disconnectManager().screen().visible;
	}
	CHECK(screen);
	g.hub.silence(2, false);
	for (int t = 0; t < 3000 && (g.peers[0].frame < 1000 || g.peers[1].frame < 1000 || g.peers[2].frame < 1000); ++t)
	{
		g.tick(1000);
	}
	for (Peer &p : g.peers)
	{
		CHECK(p.frame == 1000);
		CHECK(!p.net->disconnectManager().screen().visible);
		CHECK(p.selfDestructFrames.empty());
		CHECK(p.net->errors().empty());
	}
	CHECK(!g.peers[0].net->isDisconnected(2));
	CHECK(g.peers[0].relayed == g.peers[1].relayed);
	CHECK(g.peers[0].relayed == g.peers[2].relayed);
}

TEST_CASE("mp2 disconnect: a survivor that missed the leaver's last frame gets it from one that ran it (sendFrameDataToPlayer) before the drop")
{
	Game g(3);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	// slot 2's traffic to slot 1 stops first: slot 0 still gets two more frames of it
	g.hub.cut(2, 1, true);
	for (int t = 0; t < 2; ++t)
	{
		g.tick(1000);
	}
	g.hub.silence(2, true);
	g.peers[2].running = false;
	for (int t = 0; t < 3000 && (g.peers[0].frame < 1000 || g.peers[1].frame < 1000); ++t)
	{
		g.tick(1000);
	}
	for (int i : { 0, 1 })
	{
		for (const std::string &l : g.peers[(size_t)i].net->disconnectManager().log())
		{
			MESSAGE("peer " << i << ": " << l);
		}
		for (const std::string &l : g.peers[(size_t)i].net->errors())
		{
			MESSAGE("peer " << i << " error: " << l);
		}
		Network &n = *g.peers[(size_t)i].net;
		const UnsignedInt f = g.peers[(size_t)i].frame;
		std::string w;
		for (int x : n.waitingFor(f))
		{
			w += std::to_string(x) + " ";
		}
		MESSAGE("peer " << i << " frame " << f << " ready " << n.isFrameReady(f) << " allowed " << n.allowedToContinue(f) << " waiting " << w << " resent "
						<< n.framesResent() << " filled " << n.framesFilledFromResend() << " screen " << n.disconnectManager().screen().visible);
	}
	CHECK(g.peers[0].net->isDisconnected(2));
	CHECK(g.peers[1].net->isDisconnected(2));
	CHECK(g.peers[0].net->framesResent() > 0);
	CHECK(g.peers[1].net->framesFilledFromResend() > 0);
	CHECK(g.peers[0].relayed == g.peers[1].relayed);
	CHECK(g.peers[0].frame == 1000);
	CHECK(g.peers[0].net->errors().empty());
	CHECK(g.peers[1].net->errors().empty());
	CHECK(g.peers[0].selfDestructFrames == g.peers[1].selfDestructFrames);
}

TEST_CASE("mp2 disconnect: the last connected player quits; GameData's network timing is read with the constructor defaults for missing fields")
{
	Game g(2);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	g.hub.silence(1, true);
	for (int t = 0; t < 60; ++t)
	{
		g.tick(1000);
	}
	// two players: the silent one is the only other connected slot, the screen still comes (numConnected counts the silent connection)
	CHECK(g.peers[0].net->disconnectManager().screen().visible);

	NetworkSettings s;
	std::string error;
	REQUIRE_MESSAGE(NetworkSettings::scan("GameData\n  NetworkDisconnectTime = 15000\n  NetworkPlayerTimeoutTime = 60000\n  MaxCameraHeight = 100\nEnd\n", s, &error), error);
	CHECK(s.loaded);
	CHECK(s.disconnectTime == 15000);
	CHECK(s.playerTimeoutTime == 60000);
	CHECK(s.runAheadSlack == 10);              // RW 0x64332E
	CHECK(s.disconnectScreenNotifyTime == 15000); // RW 0x643352
	NetworkSettings d;
	CHECK(d.disconnectTime == 5000);            // RW 0x64333E (GameData.ini of 2.01 sets 15000)
	CHECK(d.playerTimeoutTime == 60000);
}

TEST_CASE("mp2 disconnect: the lane's acceptance stops are reported (S-1121 .. S-1126)")
{
	const std::vector<std::string> lines = DisconnectManager::stopLines();
	REQUIRE(lines.size() == 6);
	CHECK(lines[4].rfind("[S-1125]", 0) == 0);
	CHECK(lines[5].rfind("[S-1126]", 0) == 0);
	CHECK(Network::stopLines()[0].find("the packet router adapts the common command delay") != std::string::npos);
	CHECK(lines[3].rfind("[S-1124]", 0) == 0);
	CHECK(lines[0].rfind("[S-1121]", 0) == 0);
	CHECK(lines[1].rfind("[S-1122]", 0) == 0);
	CHECK(lines[2].rfind("[S-1123]", 0) == 0);
	CHECK(Network::stopLines()[3].find("S-1121") != std::string::npos);
}

TEST_CASE("mp2 run-ahead: the packet router's measured round trip sets every peer's command delay (ZH updateRunAhead's formula, NetworkRunAheadSlack, "
		  "RUNAHEAD as a frame command): it rises at once, falls one frame per period, and the peers stay in lockstep while it changes")
{
	Game g(2);
	for (Peer &p : g.peers)
	{
		NetworkConfig c = configFor((int)(&p - &g.peers[0]), 2);
		c.adaptiveRunAhead = true;
		c.settings.runAheadMetricsTime = 1000;
		p.net = std::make_unique<Network>(c, g.hub.endpoint((int)(&p - &g.peers[0])));
		Hub *h = &g.hub;
		p.net->setClock([h] { return h->clock; });
	}
	for (Peer &p : g.peers)
	{
		p.net->sendLoadComplete();
	}
	// one command a frame from each peer, so a changing delay moves real commands
	auto tickWithCommands = [&](int ticks) {
		for (int t = 0; t < ticks; ++t)
		{
			for (int i = 0; i < 2; ++i)
			{
				GameMessage m(MSG_DO_MOVETO, 10 + i);
				m.appendLocationArgument(Coord3D{ (float)t, (float)i, 0.0f });
				g.peers[(size_t)i].pending.append(m);
			}
			g.tick(100000, 200);
		}
	};
	tickWithCommands(10);
	CHECK(g.peers[0].net->currentRunAhead() == 2); // no round trip measured yet: the lobby's
	g.hub.endpoint(1).rtt = 4000; // slot 1 is not the packet router: its measurement decides nothing
	tickWithCommands(10);
	CHECK(g.peers[0].net->currentRunAhead() == 2);
	CHECK(g.peers[1].net->currentRunAhead() == 2);
	g.hub.endpoint(0).rtt = 800; // the router (slot 0): (800 / 2) ms one way = 2 frames, + 10 % = 2, + 1 = 3, on both peers once the RUNAHEAD frame ran
	tickWithCommands(10);
	CHECK(g.peers[0].net->currentRunAhead() == 3);
	CHECK(g.peers[1].net->currentRunAhead() == 3);
	CHECK(g.peers[0].net->maxRoundTripMs() == 800);
	g.hub.endpoint(0).rtt = 4000; // 10 frames + 1 + 1 = 12
	tickWithCommands(10);
	CHECK(g.peers[0].net->currentRunAhead() == 12);
	CHECK(g.peers[1].net->currentRunAhead() == 12);
	g.hub.endpoint(0).rtt = 100; // the target drops to 2: one frame per period (1 s = 5 ticks), each change after the last one ran
	tickWithCommands(20);
	CHECK(g.peers[0].net->currentRunAhead() < 12);
	CHECK(g.peers[0].net->currentRunAhead() > 2);
	CHECK(g.peers[0].net->currentRunAhead() == g.peers[1].net->currentRunAhead());
	tickWithCommands(150);
	CHECK(g.peers[0].net->currentRunAhead() == 2);
	CHECK(g.peers[1].net->currentRunAhead() == 2);
	g.hub.endpoint(0).rttVar = 300; // the variation counts four times: 1300 ms -> 4 frames + 1
	tickWithCommands(15);
	CHECK(g.peers[0].net->currentRunAhead() == 5);
	CHECK(g.peers[1].net->currentRunAhead() == 5);
	// every frame both peers ran has the same commands
	REQUIRE(g.peers[0].relayed.size() > 50);
	const size_t n = std::min(g.peers[0].relayed.size(), g.peers[1].relayed.size());
	for (size_t i = 0; i < n; ++i)
	{
		CHECK(g.peers[0].relayed[i] == g.peers[1].relayed[i]);
	}
	CHECK(g.peers[0].net->errors().empty());
	CHECK(g.peers[1].net->errors().empty());
}

TEST_CASE("mp2 disconnect (review): a command the dropped player sent for its drop frame reached one survivor only; both survivors discard it and run identical "
		  "command sets every frame")
{
	Game g(3);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	// slot 2's last command reaches slot 1 but not slot 0, then slot 2 goes silent and is dropped
	g.hub.cut(2, 0, true);
	GameMessage m(MSG_CREATE_SELECTED_GROUP, 12);
	m.appendBooleanArgument(true);
	g.peers[2].pending.append(m);
	g.tick(1000);
	g.hub.silence(2, true);
	g.peers[2].running = false;
	for (int t = 0; t < 400; ++t)
	{
		g.tick(150);
	}
	REQUIRE(g.peers[0].net->isDisconnected(2));
	REQUIRE(g.peers[1].net->isDisconnected(2));
	CHECK(g.peers[0].frame > 60);
	const size_t n = std::min(g.peers[0].relayed.size(), g.peers[1].relayed.size());
	REQUIRE(n > 60);
	size_t differing = 0;
	for (size_t i = 0; i < n; ++i)
	{
		differing += g.peers[0].relayed[i] != g.peers[1].relayed[i] ? 1 : 0;
	}
	CHECK(differing == 0);
	CHECK(g.peers[0].net->errors().empty());
	CHECK(g.peers[1].net->errors().empty());
}

TEST_CASE("mp2 disconnect (review): the packet router itself goes silent; its successor decides the drop and every survivor applies it at one frame")
{
	Game g(3);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	g.hub.silence(0, true); // slot 0 is the router (the fallback order is the slot order)
	g.peers[0].running = false;
	for (int t = 0; t < 400; ++t)
	{
		g.tick(150);
	}
	REQUIRE(g.peers[1].net->isDisconnected(0));
	REQUIRE(g.peers[2].net->isDisconnected(0));
	CHECK(g.peers[1].net->packetRouterSlot() == 1);
	CHECK(g.peers[2].net->packetRouterSlot() == 1);
	CHECK(g.peers[1].frame > 60);
	const size_t n = std::min(g.peers[1].relayed.size(), g.peers[2].relayed.size());
	size_t differing = 0;
	for (size_t i = 0; i < n; ++i)
	{
		differing += g.peers[1].relayed[i] != g.peers[2].relayed[i] ? 1 : 0;
	}
	CHECK(differing == 0);
	CHECK(g.peers[1].net->errors().empty());
	CHECK(g.peers[2].net->errors().empty());
}

TEST_CASE("mp2 disconnect (review): the router leaves while another player is silent and its leave is delayed to one survivor; the automatic drop of the "
		  "silent player gives every survivor one membership and identical command records")
{
	Game g(4);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(1000);
	}
	g.hub.cut(0, 2, true);
	g.peers[0].net->leave(g.peers[0].frame);
	g.peers[0].running = false;
	g.hub.silence(3, true);
	g.peers[3].running = false;
	for (int t = 0; t < 400; ++t)
	{
		g.tick(1000, 150);
	}
	Peer &a = g.peers[1];
	Peer &b = g.peers[2];
	// while the drop is held on slot 2 (the claimed leave has not reached it) its frames from the drop frame on wait: the frames both ran are identical
	{
		const size_t m = std::min(a.relayed.size(), b.relayed.size());
		size_t differ = 0;
		for (size_t i = 0; i < m; ++i)
		{
			differ += a.relayed[i] != b.relayed[i] ? 1 : 0;
		}
		CHECK(differ == 0);
	}
	g.hub.cut(0, 2, false); // the leave arrives
	for (int t = 0; t < 400; ++t)
	{
		g.tick(1000, 150);
	}
	CHECK(a.net->isDisconnected(3));
	CHECK(b.net->isDisconnected(3));
	CHECK(a.frame > 60);
	const size_t n = std::min(a.relayed.size(), b.relayed.size());
	REQUIRE(n > 60);
	size_t differing = 0;
	for (size_t i = 0; i < n; ++i)
	{
		differing += a.relayed[i] != b.relayed[i] ? 1 : 0;
	}
	CHECK(differing == 0);
	CHECK(a.net->errors().empty());
	CHECK(b.net->errors().empty());
}
