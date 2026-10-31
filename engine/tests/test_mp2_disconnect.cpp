// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: the disconnect path without retail data (GameNetwork/DisconnectManager.h): a peer that goes silent brings up the disconnect screen on the others
// after NetworkDisconnectTime, its timeout bar runs down, the remaining players vote (Kick, or the automatic vote at 0 %) and the next packet router drops it
// at the frame every remaining peer is on: DESTROYPLAYER becomes MSG_SELF_DESTRUCT { TRUE } of its player in the same frame everywhere and the game goes on;
// a peer that comes back in time ends the screen without a drop; a peer that is behind gets the frames it missed from one that ran them; the new net
// commands survive the encoding. The expectations follow the RotWK addresses in DisconnectManager.h.

#include "doctest.h"

#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/LockstepDriver.h"
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

TEST_CASE("mp2 disconnect: the new net commands survive the encoding; a slot outside 0 .. 15 is refused")
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
	// a target of slot 9 in the legacy wire form is refused, as an old client refuses it; the slot takes the wide form (NetPacket kFrameResend16)
	b[6] = 9; // the target slot byte of a DESTROYPLAYER / FRAMERESEND is right after the 6-byte header
	CHECK(!NetPacket::decodeCommand(b, rr, &error));
	CHECK(error.find("about slot 9") != std::string::npos);
	r.targetSlot = 9;
	b = NetPacket::encodeCommand(r, &error);
	REQUIRE(!b.empty());
	CHECK(b[0] == NetPacket::kFrameResend16);
	REQUIRE_MESSAGE(NetPacket::decodeCommand(b, rr, &error), error);
	CHECK(rr.type == NETCOMMANDTYPE_FRAMERESEND);
	CHECK(rr.targetSlot == 9);
	b[6] = MAX_SLOTS;
	CHECK(!NetPacket::decodeCommand(b, rr, &error));
	CHECK(error.find("about slot " + std::to_string(MAX_SLOTS)) != std::string::npos);
	// the wide forms carry the u16 masks the legacy bytes cannot (DISCONNECTSCREENOFF's released frames, FRAMERESEND's rejoin mask)
	NetCommandMsg w;
	w.type = NETCOMMANDTYPE_DISCONNECTSCREENOFF;
	w.slot = 9;
	w.frame = 777;
	w.releasedMask = 0x0305;
	w.releasedFrames = { 11, 0, 13, 0, 0, 0, 0, 0, 17, 19 };
	b = NetPacket::encodeCommand(w, &error);
	REQUIRE(!b.empty());
	CHECK(b[0] == NetPacket::kDisconnectScreenOff16);
	NetCommandMsg wd;
	REQUIRE_MESSAGE(NetPacket::decodeCommand(b, wd, &error), error);
	CHECK(wd.type == NETCOMMANDTYPE_DISCONNECTSCREENOFF);
	CHECK(wd.slot == 9);
	CHECK(wd.releasedMask == 0x0305);
	CHECK(wd.releasedFrames[8] == 17);
	CHECK(wd.releasedFrames[9] == 19);
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
	REQUIRE(lines.size() == 9);
	CHECK(lines[6].rfind("[S-4810]", 0) == 0); // lane MP-DROP-1
	CHECK(lines[7].rfind("[S-4811]", 0) == 0);
	CHECK(lines[8].rfind("[S-4812]", 0) == 0);
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
	g.hub.endpoint(0).rtt = 800; // the router (slot 0): 400 ms one way + 10 % = 440 ms, + the 25 ms margin = 2 frames, + 1 = 3, on both peers once the RUNAHEAD frame ran
	tickWithCommands(10);
	CHECK(g.peers[0].net->currentRunAhead() == 3);
	CHECK(g.peers[1].net->currentRunAhead() == 3);
	CHECK(g.peers[0].net->maxRoundTripMs() == 800);
	g.hub.endpoint(0).rtt = 4000; // 2200 ms + 25 = 11 frames, + 1 = 12
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
	g.hub.endpoint(0).rttVar = 300; // lane NETLAG-2: a test transport plans for its round trip + twice the variation: 700 ms -> 385 ms one way + 25 = 2 frames, + 1
	tickWithCommands(15);
	CHECK(g.peers[0].net->currentRunAhead() == 3);
	CHECK(g.peers[1].net->currentRunAhead() == 3);
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

namespace
{
// lane NETLAG-1: the frame a tagged MSG_DO_MOVETO (x = tag) was relayed on by a peer, -1 when it was not
long long relayFrameOf(const Peer &p, int player)
{
	for (const auto &f : p.relayed)
	{
		for (const std::string &c : f.second)
		{
			if (c == std::to_string((int)MSG_DO_MOVETO) + " " + std::to_string(player))
			{
				return (long long)f.first;
			}
		}
	}
	return -1;
}
} // namespace

TEST_CASE("netlag1 run-ahead: the lobby's run-ahead 1 is RotWK's command delay (frame + 1, RW 0x8D4301): on a LAN round trip it stays 1, a command issued "
		  "on frame P runs on P + 1 on every peer; slower links raise it per 200 ms of one-way latency and it falls back to 1")
{
	Game g(2);
	for (int i = 0; i < 2; ++i)
	{
		NetworkConfig c = configFor(i, 2);
		c.runAhead = 1;
		c.adaptiveRunAhead = true;
		c.settings.runAheadMetricsTime = 1000;
		g.peers[(size_t)i].net = std::make_unique<Network>(c, g.hub.endpoint(i));
		Hub *h = &g.hub;
		g.peers[(size_t)i].net->setClock([h] { return h->clock; });
	}
	for (Peer &p : g.peers)
	{
		p.net->sendLoadComplete();
	}
	// a peer issues one order on its current frame P (before its update stamps it); returns the frame both peers ran it on, -1 when they differ
	auto orderDelay = [&](int slot) -> long long {
		for (Peer &p : g.peers)
		{
			p.relayed.clear();
		}
		Peer &issuer = g.peers[(size_t)slot];
		const UnsignedInt issuedOn = issuer.frame;
		GameMessage m(MSG_DO_MOVETO, 10 + slot);
		m.appendLocationArgument(Coord3D{ 1.0f, 2.0f, 0.0f });
		issuer.pending.append(m);
		for (int t = 0; t < 40; ++t)
		{
			g.tick(100000, 200);
		}
		const long long a = relayFrameOf(g.peers[0], 10 + slot), b = relayFrameOf(g.peers[1], 10 + slot);
		return a >= 0 && a == b ? a - (long long)issuedOn : -1;
	};
	g.hub.endpoint(0).rtt = 2; // a LAN: 1 ms one way
	g.hub.endpoint(1).rtt = 2;
	for (int t = 0; t < 20; ++t)
	{
		g.tick(100000, 200);
	}
	CHECK(g.peers[0].net->currentRunAhead() == 1);
	CHECK(g.peers[1].net->currentRunAhead() == 1);
	CHECK(g.peers[0].net->runAheadChanges() == 0);
	CHECK(orderDelay(0) == 1); // the packet router's own order
	CHECK(orderDelay(1) == 1); // the other player's
	// the round trips where the delay steps up: one-way latency + 10 % (NetworkRunAheadSlack) + 25 ms (lane NETLAG-2; NETLAG-1 kept half a period), in
	// 200 ms frames, + 1
	const int rtts[] = { 160, 318, 319, 400, 680, 681, 2 };
	const int expected[] = { 1, 1, 2, 2, 2, 3, 1 };
	for (size_t k = 0; k < sizeof(rtts) / sizeof(rtts[0]); ++k)
	{
		g.hub.endpoint(0).rtt = rtts[k];
		for (int t = 0; t < 60; ++t) // a fall is one frame per NetworkRunAheadMetricsTime (5 ticks here)
		{
			g.tick(100000, 200);
		}
		INFO("round trip " << rtts[k] << " ms");
		CHECK(g.peers[0].net->currentRunAhead() == expected[k]);
		CHECK(g.peers[1].net->currentRunAhead() == expected[k]);
		CHECK(orderDelay(1) == expected[k]);
	}
	CHECK(g.peers[0].net->errors().empty());
	CHECK(g.peers[1].net->errors().empty());
}

TEST_CASE("netlag2 run-ahead: with RotWK's NetworkRunAheadMetricsTime of 5000 ms a raised delay still falls one frame a second (not one per 5 s), "
		  "only with 50 ms more room than the lower value needs, and the peers run identical frames while it does")
{
	Game g(2);
	for (int i = 0; i < 2; ++i)
	{
		NetworkConfig c = configFor(i, 2);
		c.runAhead = 1;
		c.adaptiveRunAhead = true;
		c.settings.runAheadMetricsTime = 5000; // RotWK's GameData (the relay logs of 2026-10-18 step every 5 s)
		g.peers[(size_t)i].net = std::make_unique<Network>(c, g.hub.endpoint(i));
		Hub *h = &g.hub;
		g.peers[(size_t)i].net->setClock([h] { return h->clock; });
	}
	for (Peer &p : g.peers)
	{
		p.net->sendLoadComplete();
	}
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
	g.hub.endpoint(0).rtt = 2;
	g.hub.endpoint(1).rtt = 2;
	tickWithCommands(10);
	CHECK(g.peers[0].net->currentRunAhead() == 1);
	g.hub.endpoint(0).rtt = 2000; // 1100 ms one way + 25 = 5 frames, + 1 = 6
	tickWithCommands(15);
	CHECK(g.peers[0].net->currentRunAhead() == 6);
	CHECK(g.peers[1].net->currentRunAhead() == 6);
	g.hub.endpoint(0).rtt = 2;
	int ticks = 0;
	while (g.peers[0].net->currentRunAhead() > 1 && ticks < 200)
	{
		tickWithCommands(1);
		++ticks;
	}
	// five steps down, each decided a second (5 ticks) after the last decision once the previous step ran (which takes that step's run-ahead in frames):
	// 29 ticks, 5.8 s, on this driven clock; one step per 5 s took 25 s
	CHECK(ticks == 29);
	MESSAGE("back to 1 frame after " << ticks << " ticks of 200 ms");
	CHECK(g.peers[1].net->currentRunAhead() == 1);
	// at a frame's edge it does not flap: 300 ms round trip = 150 ms + 10 % + 25 = 1 frame, + 1 = 2; from 2 it comes down only with 50 ms more room
	g.hub.endpoint(0).rtt = 400; // 200 + 20 + 25 = 1 frame, + 1 = 2
	tickWithCommands(15);
	CHECK(g.peers[0].net->currentRunAhead() == 2);
	g.hub.endpoint(0).rtt = 300; // the target is 1, but 165 + 25 + 50 = 240 ms does not fit a frame: stays 2
	tickWithCommands(30);
	CHECK(g.peers[0].net->currentRunAhead() == 2);
	CHECK(g.peers[1].net->currentRunAhead() == 2);
	g.hub.endpoint(0).rtt = 220; // 110 + 11 + 25 + 50 = 196 ms: room enough, down to 1
	tickWithCommands(30);
	CHECK(g.peers[0].net->currentRunAhead() == 1);
	CHECK(g.peers[1].net->currentRunAhead() == 1);
	const size_t n = std::min(g.peers[0].relayed.size(), g.peers[1].relayed.size());
	REQUIRE(n > 40); // tolerance: the comparison below needs the frames of the whole change; their exact count is not what this test pins
	for (size_t i = 0; i < n; ++i)
	{
		CHECK(g.peers[0].relayed[i] == g.peers[1].relayed[i]);
	}
	CHECK(g.peers[0].net->errors().empty());
	CHECK(g.peers[1].net->errors().empty());
}

namespace
{
// lane NETLAG-2: both peers wait at frame 9; frame 9 runs; slot 0 is told frame 10 is due in `dueMs` ms (unless < 0) and issues one order (tag 10) before
// its next update, as the HUD's input comes in the pump after the completion
void netlag2WarmUpAndIssue(Game &g, long long dueMs)
{
	for (int t = 0; t < 20; ++t)
	{
		g.tick(9, 50);
	}
	REQUIRE(g.peers[0].frame == 9);
	g.tick(10, 10); // frame 9 runs on both peers; slot 0 has not updated on frame 10 yet
	REQUIRE(g.peers[0].frame == 10);
	if (dueMs >= 0)
	{
		g.peers[0].net->setFrameDue(10, (std::uint32_t)dueMs);
	}
	GameMessage m(MSG_DO_MOVETO, 10);
	m.appendLocationArgument(Coord3D{ 1.0f, 2.0f, 0.0f });
	g.peers[0].pending.append(m);
}

// the networks of a 2-player game remade with run-ahead 1, the option and a measured round trip (the Hub's address is captured: set up in place)
void netlag2Setup(Game &g, int rtt, bool early)
{
	for (int i = 0; i < 2; ++i)
	{
		NetworkConfig c = configFor(i, 2);
		c.runAhead = 1;
		c.earlyOrders = early;
		g.peers[(size_t)i].net = std::make_unique<Network>(c, g.hub.endpoint(i));
		Hub *h = &g.hub;
		g.peers[(size_t)i].net->setClock([h] { return h->clock; });
		g.hub.endpoint(i).rtt = rtt;
	}
	for (Peer &p : g.peers)
	{
		p.net->sendLoadComplete();
	}
}
} // namespace

TEST_CASE("netlag2 early orders: an order issued while the next frame is further away than the lead (the planned one-way latency + 10 % + 40 ms) runs "
		  "on that frame on every peer; closer to it, without the frame's timing, without a measured peer or with the option off it waits for frame + 1")
{
	struct Case
	{
		const char *name;
		int rtt;         ///< the round trip both peers measure (-1: none yet)
		long long dueMs; ///< frame 10 is due in this many ms on slot 0's clock (-1: no timing given)
		bool early;      ///< NetworkConfig::earlyOrders
		long long delay; ///< the frames from the issue frame (10) to the one the order ran on, on both peers
		unsigned long long earlyCount;
	};
	// a 2 ms round trip: lead = 1 ms one way + 0 % (1 ms x 10 % rounds down) + 40 = 41 ms
	const Case cases[] = {
		{ "LAN, 150 ms before the frame", 2, 150, true, 0, 1 },
		{ "LAN, exactly the lead before the frame", 2, 41, true, 0, 1 },
		{ "LAN, 1 ms inside the lead", 2, 40, true, 1, 0 },
		{ "no timing from the clock", 2, -1, true, 1, 0 },
		{ "no round trip measured yet", -1, 150, true, 1, 0 },
		{ "option off", 2, 150, false, 1, 0 },
		// 300 ms round trip: 150 + 15 + 40 = 205 ms, more than a frame: the order keeps the run-ahead's frame (the adaptive run-ahead is off here)
		{ "slow link", 300, 199, true, 1, 0 },
	};
	for (const Case &k : cases)
	{
		INFO(k.name);
		Game g(2);
		netlag2Setup(g, k.rtt, k.early);
		netlag2WarmUpAndIssue(g, k.dueMs);
		for (int t = 0; t < 40; ++t)
		{
			g.tick(100000, 10);
		}
		const long long a = relayFrameOf(g.peers[0], 10), b = relayFrameOf(g.peers[1], 10);
		CHECK(a == b);
		CHECK(a - 10 == k.delay);
		CHECK(g.peers[0].net->earlyOrdersSent() == k.earlyCount);
		const size_t n = std::min(g.peers[0].relayed.size(), g.peers[1].relayed.size());
		for (size_t i = 0; i < n; ++i)
		{
			CHECK(g.peers[0].relayed[i] == g.peers[1].relayed[i]);
		}
		CHECK(g.peers[0].net->errors().empty());
		CHECK(g.peers[1].net->errors().empty());
	}
}

TEST_CASE("netlag2 early orders: the early frame's FRAMEINFO waits until the lead (the other peer cannot run that frame before it); the timing carries "
		  "forward a period per frame; a later order never runs before an earlier one")
{
	{
		Game g(2);
		netlag2Setup(g, 2, true);
		netlag2WarmUpAndIssue(g, 150);
		const std::uint64_t t0 = g.hub.clock;
		std::uint64_t ranAt = 0;
		for (int t = 0; t < 40 && ranAt == 0; ++t)
		{
			g.tick(100000, 10);
			ranAt = g.peers[1].frame > 10 ? g.hub.clock : 0;
		}
		CHECK(relayFrameOf(g.peers[1], 10) == 10);
		// 150 - 41 = 109 ms: slot 0's FRAMEINFO of frame 10 leaves at its first update at or after it (the 110 ms tick), arrives a tick later, and slot 1
		// runs frame 10 at its next step
		CHECK(ranAt - t0 >= 109); // tolerance: the harness's 10 ms ticks and one-tick delivery put the run after the 109 ms the FRAMEINFO waited
		MESSAGE("slot 1 ran frame 10 " << (ranAt - t0) << " ms after the order");
		CHECK(g.peers[0].net->errors().empty());
		CHECK(g.peers[1].net->errors().empty());
	}
	{
		// timing for frame 10 only; on frame 11 (no new timing given) it is carried forward: frame 11 is due 200 ms after frame 10
		Game g(2);
		netlag2Setup(g, 2, true);
		netlag2WarmUpAndIssue(g, 60);
		const std::uint64_t t0 = g.hub.clock;
		while ((g.peers[0].frame < 11 || g.peers[1].frame < 11) && g.hub.clock - t0 < 1000)
		{
			g.tick(11, 10); // frame 10 runs on both peers; both wait before 11
		}
		REQUIRE(g.peers[0].frame == 11);
		REQUIRE(g.peers[1].frame == 11);
		CHECK(relayFrameOf(g.peers[1], 10) == 10); // 60 ms >= 41: early
		const long long until11 = 60 + 200 - (long long)(g.hub.clock - t0);
		INFO("frame 11 is due in " << until11 << " ms by the carried-forward timing");
		REQUIRE(until11 >= 41); // tolerance: the harness ticks decide when frame 10 runs; the case needs only frame 11 still open (at least the 41 ms lead)
		GameMessage m(MSG_DO_STOP, 10);
		g.peers[0].pending.append(m);
		for (int t = 0; t < 60; ++t)
		{
			g.tick(100000, 10);
		}
		bool stopOn11 = false;
		for (const auto &f : g.peers[1].relayed)
		{
			for (const std::string &c : f.second)
			{
				stopOn11 = stopOn11 || (f.first == 11 && c == std::to_string((int)MSG_DO_STOP) + " 10");
			}
		}
		CHECK(stopOn11);
		CHECK(g.peers[0].net->earlyOrdersSent() == 2);
	}
	{
		// the first order 20 ms before frame 10 (inside the 41 ms lead) gets frame 11; a second one, told 150 ms, may not go before it
		Game g(2);
		netlag2Setup(g, 2, true);
		netlag2WarmUpAndIssue(g, 20);
		g.peers[0].step(100000);
		g.peers[0].net->setFrameDue(10, 150);
		GameMessage second(MSG_DO_STOP, 10);
		g.peers[0].pending.append(second);
		g.peers[0].step(100000);
		CHECK(g.peers[0].net->earlyOrdersSent() == 0);
		for (int t = 0; t < 40; ++t)
		{
			g.tick(100000, 10);
		}
		CHECK(relayFrameOf(g.peers[1], 10) == 11);
		CHECK(g.peers[0].net->errors().empty());
	}
}

TEST_CASE("netlag1: the lockstep driver measures each local command's delay in frames and to its effect: a local game runs a command on the next batch "
		  "(0 frames), a network game on the batch of its execution frame (the run-ahead)")
{
	// a local game (no Network): what is pumped on protocol frame P is in batch P
	LockstepDriver local(nullptr, nullptr);
	CommandList pending;
	pending.append(GameMessage(MSG_DO_STOP, 3));
	local.pump(5, pending);
	local.pump(5, pending); // nothing new: counted once
	FrameBatch b;
	REQUIRE(local.acquire(5, b));
	CHECK(b.frame == 5);
	REQUIRE(b.commands.size() == 1);
	FrameCompletion c;
	c.batchFrame = 5;
	c.frame = 6;
	local.completed(c);
	CHECK(local.smoothStats().commandDelayFrames == std::vector<std::uint32_t>{ 0 });
	CHECK(local.smoothStats().commandEffectMs.size() == 1);
	CHECK(local.smoothStats().inputLatencyMs.size() == 1);
	CHECK(local.smoothStats().unmatchedCommands == 0);

	// a network game of two peers with run-ahead 1: the local order of protocol frame 0 runs in batch 1
	Hub hub(2);
	std::vector<std::unique_ptr<Network>> nets;
	for (int i = 0; i < 2; ++i)
	{
		NetworkConfig cfg = configFor(i, 2);
		cfg.runAhead = 1;
		nets.push_back(std::make_unique<Network>(cfg, hub.endpoint(i)));
		Hub *h = &hub;
		nets.back()->setClock([h] { return h->clock; });
		nets.back()->sendLoadComplete();
	}
	LockstepDriver driver(nets[0].get(), nullptr);
	CommandList order;
	order.append(GameMessage(MSG_DO_STOP, 10));
	driver.pump(0, order);
	CommandList none;
	UnsignedInt remoteFrame = 0;
	UnsignedInt next = 0;
	for (int t = 0; t < 40 && next < 3; ++t)
	{
		nets[1]->update(remoteFrame, none);
		while (nets[1]->isFrameReady(remoteFrame) && remoteFrame < 3)
		{
			CommandList out;
			nets[1]->relayCommands(remoteFrame, out);
			++remoteFrame;
			nets[1]->update(remoteFrame, none);
		}
		hub.tick(50);
		FrameBatch batch;
		if (driver.acquire(next, batch))
		{
			FrameCompletion done;
			done.batchFrame = next;
			done.frame = next + 1;
			done.hashed = true;
			driver.completed(done);
			++next;
		}
		else
		{
			driver.pump(next, none);
		}
	}
	CHECK(next == 3);
	CHECK(driver.smoothStats().commandDelayFrames == std::vector<std::uint32_t>{ 1 });
	CHECK(driver.smoothStats().commandEffectMs.size() == 1);
	CHECK(driver.smoothStats().unmatchedCommands == 0);
	CHECK(driver.errors().empty());
	CHECK(nets[0]->errors().empty());
	CHECK(nets[1]->errors().empty());
}
