// OpenBFME unit tests. GPL-3.0.
// Lane MP-DROP-1 (stop S-4556): a player who crashes or drops must never freeze an online game. The disconnect path (GameNetwork/DisconnectManager.h)
// without retail data, on an in-memory network with the relay's view of each slot (the harness of test_online3_rejoin.cpp, without its relay tap): the
// silent player is dropped at one frame on every survivor and the game goes on in step, whatever happens on the way (a moment of traffic from the silent
// slot on one survivor, a restarted game binding late, a pause, another player's rejoin, two to eight players, LAN or online); a forged vote or drop is
// refused by name.

#include "doctest.h"

#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
// an in-memory network with a shared test clock: every datagram arrives a tick later (reliable, ordered, like Transport); a cut link holds its datagrams
// until it is restored (a dead or hung peer: the transport resends what was not acknowledged). The relay's view of each slot (its generation, silent at
// the relay) is set by the test
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
		int peerGeneration(int s) const override { return hub->generation[(size_t)s]; }
		bool silentAtRelay(int s) const override { return hub->silentAtRelay[(size_t)s]; }
		void resetPeer(int s) override
		{
			retired.erase(s);
			hub->blocked.erase({ slot, s });
			hub->blocked.erase({ s, slot });
		}
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
	void tick(std::uint64_t ms)
	{
		++m_now;
		clock += ms;
		std::deque<Flight> keep;
		std::set<std::pair<int, int>> held;
		for (Flight &f : m_inFlight)
		{
			if (f.at <= m_now && blocked.count({ f.from, f.to }))
			{
				continue; // the streams of a restarted slot's old and new incarnation do not match
			}
			if (f.at <= m_now && !m_cut[{ f.from, f.to }] && !held.count({ f.from, f.to }))
			{
				m_endpoints[(size_t)f.to]->inbox.push_back({ f.from, f.bytes });
			}
			else
			{
				if (f.at <= m_now)
				{
					held.insert({ f.from, f.to }); // keeps the link's order
				}
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
	std::array<int, MAX_SLOTS> generation{};
	std::array<bool, MAX_SLOTS> silentAtRelay{};
	std::set<std::pair<int, int>> blocked;

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

constexpr std::uint32_t kDisconnectMs = 1000; // NetworkDisconnectTime of these games (retail: 15000)
constexpr std::uint32_t kTimeoutMs = 6000;    // NetworkPlayerTimeoutTime (retail: 60000)
constexpr std::uint64_t kTickMs = 200;        // one logic frame at 5 frames a second

NetworkConfig configFor(int slot, int peers, bool online)
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
	c.settings.disconnectTime = kDisconnectMs;
	c.settings.playerTimeoutTime = kTimeoutMs;
	c.holdForRejoin = online;
	c.rejoinWindowMs = 20000;
	return c;
}

// a stand-in for the state hash: every peer that ran the same frames with the same commands has the same value
std::uint32_t fakeHash(std::uint32_t previous, UnsignedInt frame, const std::vector<std::string> &commands)
{
	std::uint32_t h = previous * 16777619u ^ frame;
	for (const std::string &c : commands)
	{
		for (char ch : c)
		{
			h = (h ^ (std::uint8_t)ch) * 16777619u;
		}
	}
	return h;
}

struct Peer
{
	std::unique_ptr<Network> net;
	UnsignedInt frame = 0;
	bool running = true;
	CommandList pending;
	std::map<UnsignedInt, std::vector<std::string>> relayed; ///< frame -> its commands ("type player")
	std::vector<UnsignedInt> selfDestructFrames;
	std::uint32_t hash = 0x811C9DC5u;
	std::map<UnsignedInt, std::uint32_t> hashes; ///< logic frame after the frame -> fakeHash
	// lane MPDROP-2: the frames go through the skirmish frame driver (LockstepDriver: pump, acquire, completed, as LiveGame::advance calls it), up to
	// `catchUp` a tick (LiveGame acquires every due frame of an advance: a game that hung is many frames behind its clock)
	std::unique_ptr<LockstepDriver> driver;
	int catchUp = 1;
	int framesAfterQuit = 0; ///< frames handed out while the network asked this player to leave
	void record(UnsignedInt f, const std::vector<GameMessage> &messages)
	{
		std::vector<std::string> cmds;
		for (const GameMessage &m : messages)
		{
			cmds.push_back(std::to_string(m.getType()) + " " + std::to_string(m.getPlayerIndex()));
			if (m.getType() == MSG_SELF_DESTRUCT)
			{
				selfDestructFrames.push_back(f);
			}
		}
		relayed[f] = cmds;
		hash = fakeHash(hash, f, cmds);
		hashes[f + 1] = hash;
	}
	void order()
	{
		if (frame % 7 == 3)
		{
			// the player gives an order now and then (its commands are part of every frame's list)
			GameMessage m(MSG_DO_STOP, net->config().slotPlayerIndex[(size_t)net->config().localSlot]);
			pending.append(m);
		}
	}
	void stepDriver(UnsignedInt stopAt)
	{
		for (int i = 0; i < catchUp && frame < stopAt; ++i)
		{
			driver->pump(frame, pending);
			FrameBatch b;
			if (!driver->acquire(frame, b))
			{
				return;
			}
			framesAfterQuit += net->quitRequested() ? 1 : 0;
			record(frame, b.commands);
			FrameCompletion c;
			c.batchFrame = frame;
			c.frame = frame + 1;
			driver->completed(c);
			++frame;
			order();
		}
	}
	void step(UnsignedInt stopAt)
	{
		if (!running)
		{
			return;
		}
		if (driver)
		{
			stepDriver(stopAt);
			return;
		}
		net->update(frame, pending);
		if (frame < stopAt && net->isFrameReady(frame) && net->allowedToContinue(frame))
		{
			CommandList out;
			net->relayCommands(frame, out);
			record(frame, out.messages());
			++frame;
			order();
		}
	}
};

struct Game
{
	Hub hub;
	std::vector<Peer> peers;
	// `loading`: a slot whose game never finishes loading (it sends no LOADCOMPLETE, and nothing else: it crashed on the load screen)
	// `catchUp` > 0 (lane MPDROP-2): every peer runs its frames through a LockstepDriver, up to that many a tick
	Game(int n, bool online, int loading = -1, int catchUp = 0)
		: hub(n)
		, peers((size_t)n)
	{
		for (int i = 0; i < n; ++i)
		{
			peers[(size_t)i].net = std::make_unique<Network>(configFor(i, n, online), hub.endpoint(i));
			Hub *h = &hub;
			peers[(size_t)i].net->setClock([h] { return h->clock; });
			if (catchUp > 0)
			{
				peers[(size_t)i].driver = std::make_unique<LockstepDriver>(peers[(size_t)i].net.get(), nullptr);
				peers[(size_t)i].catchUp = catchUp;
			}
		}
		for (int i = 0; i < n; ++i)
		{
			if (i == loading)
			{
				kill(i);
				continue;
			}
			peers[(size_t)i].net->sendLoadComplete();
		}
	}
	void tick(UnsignedInt stopAt = 100000)
	{
		for (Peer &p : peers)
		{
			p.step(stopAt);
		}
		hub.tick(kTickMs);
	}
	// the process of `slot` dies (SIGKILL): nothing in or out from now on (the relay's STATUS reports it silent only after its own silence time: tests
	// that need that view set Hub::silentAtRelay)
	void kill(int slot)
	{
		hub.silence(slot, true);
		peers[(size_t)slot].running = false;
	}
	// the process of `slot` hangs (lane MPDROP-2): it runs nothing and nothing is heard from it (what it sent waits on the way, the transport resends
	// it); datagrams to it still arrive and wait in its inbox. `on` false: it runs again
	void hang(int slot, bool on)
	{
		for (int i = 0; i < (int)peers.size(); ++i)
		{
			hub.cut(slot, i, on);
		}
		peers[(size_t)slot].running = !on;
	}
	bool screensUp(const std::vector<int> &survivors) const
	{
		for (int s : survivors)
		{
			if (!peers[(size_t)s].net->disconnectManager().screen().visible)
			{
				return false;
			}
		}
		return true;
	}
	std::string logs(const std::vector<int> &survivors) const
	{
		std::string out;
		for (int s : survivors)
		{
			for (const std::string &l : peers[(size_t)s].net->disconnectManager().log())
			{
				out += "peer " + std::to_string(s) + ": " + l + "\n";
			}
			for (const std::string &l : peers[(size_t)s].net->rejoinLog())
			{
				out += "peer " + std::to_string(s) + " rejoin: " + l + "\n";
			}
			for (const std::string &l : peers[(size_t)s].net->errors())
			{
				out += "peer " + std::to_string(s) + " error: " + l + "\n";
			}
			out += "peer " + std::to_string(s) + " at frame " + std::to_string(peers[(size_t)s].frame) + "\n";
		}
		return out;
	}
};

// every survivor ran the same frames with the same commands up to `frame` (equal hashes)
void checkInStep(const Game &g, const std::vector<int> &survivors, UnsignedInt frame)
{
	const Peer &a = g.peers[(size_t)survivors[0]];
	REQUIRE(a.hashes.count(frame));
	for (int s : survivors)
	{
		INFO("peer " << s);
		const Peer &p = g.peers[(size_t)s];
		REQUIRE(p.hashes.count(frame));
		CHECK(p.hashes.at(frame) == a.hashes.at(frame));
		CHECK(p.selfDestructFrames == a.selfDestructFrames);
		CHECK(p.net->errors().empty());
	}
}

// runs until every survivor dropped `victim` (and at most `budgetMs` of test time); returns the time it took, 0 = never
std::uint64_t runUntilDropped(Game &g, const std::vector<int> &survivors, int victim, std::uint64_t budgetMs)
{
	const std::uint64_t start = g.hub.clock;
	while (g.hub.clock - start < budgetMs)
	{
		g.tick();
		bool all = true;
		for (int s : survivors)
		{
			all = all && g.peers[(size_t)s].net->isDisconnected(victim);
		}
		if (all)
		{
			return g.hub.clock - start;
		}
	}
	return 0;
}
} // namespace

TEST_CASE("mpdrop1: a crashed player whose silence is broken for a moment on one survivor (a restarted game's first datagram, a hung game that "
		  "flickers back) is still dropped on time, at one frame, and the game goes on in step (S-4556)")
{
	// ONLINE-5's three-player crash: the survivors' screens came on, the bar ran out and both voted, but the drop never came: one survivor heard the
	// crashed slot for a moment, its screen went off and on at the same frame, and RW 0x8D836B's rule ignored its repeated announcement of that frame,
	// so "every player on one frame" (RW 0x8D861A) never held again
	for (const bool online : { true, false })
	{
		for (const int flicker : { 1, 0 }) // the survivor that hears the crashed slot for a moment: the non-router (slot 1), then the router (slot 0)
		{
			CAPTURE(online);
			CAPTURE(flicker);
			Game g(3, online);
			for (int t = 0; t < 60; ++t)
			{
				g.tick();
			}
			g.kill(2);
			const std::vector<int> survivors{ 0, 1 };
			for (int t = 0; t < 100 && !g.screensUp(survivors); ++t)
			{
				g.tick();
			}
			REQUIRE(g.screensUp(survivors));
			// a quarter of the bar later, one datagram of slot 2 reaches the flickering survivor's transport (and nothing else of it)
			for (int t = 0; t < (int)(kTimeoutMs / kTickMs / 4); ++t)
			{
				g.tick();
			}
			g.hub.silentAtRelay[2] = true; // by now the relay's STATUS reports the slot silent
			g.tick();
			g.hub.endpoint(flicker).heard[2] = g.hub.clock;
			g.hub.silentAtRelay[2] = false; // the relay's STATUS: the slot sent something
			g.tick();
			g.hub.silentAtRelay[2] = true;
			const std::uint64_t took = runUntilDropped(g, survivors, 2, 4 * (kDisconnectMs + kTimeoutMs));
			INFO(g.logs(survivors));
			// the timeout's drop (nobody pressed Kick): the router's bar ran out 1 s of silence + 6 s after the crash; when the router itself heard the
			// slot, its screen came back after another second of silence with every bar full. Exact in this deterministic network (0 = never dropped)
			CHECK(took == (flicker == 1 ? 4800u : 6600u));
			// both survivors were stalled before frame 61; the router (slot 0) decided the drop for frame 61 and ran
			// it in the same step; slot 1 runs it once the DISCONNECTPLAYER arrives, a tick later
			CHECK(g.peers[0].frame == 62);
			CHECK(g.peers[1].frame == 61);
			CHECK(g.peers[0].net->isHeld(2) == online);
			CHECK(g.peers[1].net->isHeld(2) == online);
			for (int t = 0; t < 60; ++t)
			{
				g.tick();
			}
			// the game went on in step: 59 frames in the 60 ticks after the drop (the first tick runs the frame the drop released)
			CHECK(g.peers[0].frame == 121);
			CHECK(g.peers[1].frame == 121);
			checkInStep(g, survivors, 121);
			// the army: online, still its player's (held for a rejoin, the window is 20 s); LAN, handed over at once (one MSG_SELF_DESTRUCT on both)
			CHECK(g.peers[0].selfDestructFrames.size() == (online ? 0u : 1u));
			CHECK_FALSE(g.peers[0].net->disconnectManager().screen().visible);
			CHECK_FALSE(g.peers[1].net->disconnectManager().screen().visible);
		}
	}
}

TEST_CASE("mpdrop1: a player whose game dies on the load screen: once it is silent (four times NetworkDisconnectTime below frame 6) the others leave "
		  "the load screen (RW 0x62B385), the disconnect screen opens, it is dropped at frame 0 and the game starts in step")
{
	for (const bool online : { true, false })
	{
		CAPTURE(online);
		Game g(3, online, 2);
		const std::vector<int> survivors{ 0, 1 };
		bool screenSeen = false;
		std::uint64_t screenAt = 0;
		const std::uint64_t start = g.hub.clock;
		while (!screenSeen && g.hub.clock - start < 20000)
		{
			g.tick();
			screenSeen = g.screensUp(survivors);
			screenAt = g.hub.clock - start;
		}
		INFO(g.logs(survivors));
		REQUIRE(screenSeen);
		CHECK(g.peers[0].frame == 0); // nobody ran a frame: the load barrier
		CHECK(g.peers[1].frame == 0);
		// RW 0x8D335D: below frame 6 the silence limit is four times NetworkDisconnectTime (the clock starts at the first query); the load screen goes on
		// the update the slot is silent (RW 0x62B385 reads + 0x270), the disconnect screen opens on the next one (RW 0x8D8B82)
		CHECK(screenAt == 4 * kDisconnectMs + 3 * kTickMs);
		CHECK_FALSE(g.peers[0].net->disconnectManager().loadScreenUp());
		const std::uint64_t took = runUntilDropped(g, survivors, 2, 4 * kTimeoutMs);
		// the bar runs out (nobody pressed Kick): the router decides on the tick after it reads below 0, the other survivor gets the drop a tick later
		CHECK(took == kTimeoutMs + 2 * kTickMs);
		CHECK(g.peers[0].net->isHeld(2) == online);
		for (int t = 0; t < 60; ++t)
		{
			g.tick();
		}
		CHECK(g.peers[0].frame == 61); // the game started: 61 frames in the 60 ticks after the drop (the run-ahead's first frames were ready)
		CHECK(g.peers[1].frame == 61);
		checkInStep(g, survivors, 61);
		CHECK(g.peers[0].selfDestructFrames.size() == (online ? 0u : 1u));
	}
}

TEST_CASE("mpdrop1: two to eight players: a crashed player is dropped when its bar runs out and every survivor runs on in step; the packet router "
		  "itself crashing hands the decision to the next slot")
{
	for (const bool online : { true, false })
	{
		for (int n = 2; n <= 8; ++n)
		{
			for (const int victim : { n - 1, 0 })
			{
				if (n == 2 && victim == 0)
				{
					continue; // the same game as victim 1 seen from the other side
				}
				CAPTURE(online);
				CAPTURE(n);
				CAPTURE(victim);
				Game g(n, online);
				for (int t = 0; t < 60; ++t)
				{
					g.tick();
				}
				g.kill(victim);
				std::vector<int> survivors;
				for (int i = 0; i < n; ++i)
				{
					if (i != victim)
					{
						survivors.push_back(i);
					}
				}
				const std::uint64_t took = runUntilDropped(g, survivors, victim, 4 * (kDisconnectMs + kTimeoutMs));
				INFO(g.logs(survivors));
				// the silence limit (noticed on the tick after it is exceeded), the full bar (read below 0 a tick after it ran out), the router's decision
				// on the next tick and the drop reaching the others a tick later
				CHECK(took == kDisconnectMs + kTimeoutMs + (n == 2 ? 3 : 4) * kTickMs); // two players: the router is the only survivor
				const int decider = victim == 0 ? 1 : 0; // the next packet router of the fallback order (slot order)
				CHECK(g.peers[(size_t)decider].net->disconnectManager().dropped() == std::vector<int>{ victim });
				for (int s : survivors)
				{
					CHECK(g.peers[(size_t)s].net->isHeld(victim) == online);
					CHECK_FALSE(g.peers[(size_t)s].net->quitRequested());
				}
				for (int t = 0; t < 30; ++t)
				{
					g.tick();
				}
				CHECK(g.peers[(size_t)survivors[0]].frame == g.peers[(size_t)survivors.back()].frame);
				checkInStep(g, survivors, g.peers[(size_t)survivors[0]].frame);
				CHECK(g.peers[(size_t)survivors[0]].selfDestructFrames.size() == (online ? 0u : 1u));
				for (int s : survivors)
				{
					CHECK_FALSE(g.peers[(size_t)s].net->disconnectManager().screen().visible);
				}
			}
		}
	}
}

TEST_CASE("mpdrop1: a player crashes while the game is paused (another player's pause, or its own): the screen, the drop during the pause, and the "
		  "game resumes in step when a survivor resumes it")
{
	for (const int pauser : { 1, 2 }) // slot 1 pauses and slot 2 crashes; then slot 2 pauses and crashes itself
	{
		CAPTURE(pauser);
		Game g(3, true);
		for (int t = 0; t < 40; ++t)
		{
			g.tick();
		}
		g.peers[(size_t)pauser].net->requestPause();
		for (int t = 0; t < 10; ++t)
		{
			g.tick();
		}
		const std::vector<int> survivors{ 0, 1 };
		REQUIRE(g.peers[0].net->isPaused());
		REQUIRE(g.peers[1].net->isPaused());
		const UnsignedInt pausedAt = g.peers[0].frame;
		CHECK(g.peers[1].frame == pausedAt);
		g.kill(2);
		const std::uint64_t took = runUntilDropped(g, survivors, 2, 4 * (kDisconnectMs + kTimeoutMs));
		INFO(g.logs(survivors));
		CHECK(took == kDisconnectMs + kTimeoutMs + 4 * kTickMs);
		// dropped during the pause: nobody ran a frame
		CHECK(g.peers[0].frame == pausedAt);
		CHECK(g.peers[1].frame == pausedAt);
		CHECK(g.peers[0].net->isPaused());
		CHECK(g.peers[1].net->isPaused());
		g.peers[0].net->requestResume(); // any player may resume (ONLINE-3), also a pause of the player who crashed
		for (int t = 0; t < 40; ++t)
		{
			g.tick();
		}
		CHECK_FALSE(g.peers[0].net->isPaused());
		CHECK_FALSE(g.peers[1].net->isPaused());
		CHECK(g.peers[0].frame == g.peers[1].frame);
		CHECK(g.peers[0].frame > pausedAt + 30); // tolerance: the claim is that the game went on after the resume (the frames per tick follow the run-ahead)
		checkInStep(g, survivors, g.peers[0].frame);
	}
}

TEST_CASE("mpdrop1 hostile: a forged vote or drop is refused by name and changes nothing: a vote claiming another voter, a vote for oneself, for a "
		  "slot outside the game or out of range, a drop claiming the decider")
{
	auto encode = [](const NetCommandMsg &c) {
		std::string error;
		std::vector<std::uint8_t> b = NetPacket::encodeCommand(c, &error);
		REQUIRE_MESSAGE(!b.empty(), error);
		return b;
	};
	struct Case
	{
		const char *name;
		int sender;    ///< the slot the transport delivers it from
		int claimed;   ///< the slot the command names as its sender
		int target;
		NetCommandType type;
		const char *refusal; ///< the error text that names the refusal
		std::uint8_t gone = 0; ///< a drop's claimed departures
	};
	const Case cases[] = {
		{ "a vote claiming another voter", 1, 0, 2, NETCOMMANDTYPE_DISCONNECTVOTE, "sent a command claiming slot 0" },
		{ "a vote for oneself", 1, 1, 1, NETCOMMANDTYPE_DISCONNECTVOTE, "DISCONNECTVOTE of slot 1 for itself: refused" },
		{ "a vote for a slot outside the game", 1, 1, 5, NETCOMMANDTYPE_DISCONNECTVOTE, "DISCONNECTVOTE of slot 1 for slot 5, which is not a player of the game: refused" },
		{ "a drop claiming the decider", 1, 0, 2, NETCOMMANDTYPE_DISCONNECTPLAYER, "sent a command claiming slot 0" },
	};
	for (const Case &k : cases)
	{
		CAPTURE(k.name);
		Game g(3, true);
		for (int t = 0; t < 40; ++t)
		{
			g.tick();
		}
		NetCommandMsg c;
		c.type = k.type;
		c.slot = (std::uint8_t)k.claimed;
		c.targetSlot = (std::uint8_t)k.target;
		c.frame = g.peers[0].frame;
		const std::uint32_t before = g.peers[0].hash;
		g.hub.endpoint(0).inbox.push_back({ k.sender, encode(c) });
		g.tick();
		bool named = false;
		for (const std::string &e : g.peers[0].net->errors())
		{
			named = named || e.find(k.refusal) != std::string::npos;
		}
		INFO(g.logs({ 0 }));
		CHECK(named);
		CHECK(g.peers[0].net->errors().size() == 1);
		CHECK(g.peers[0].net->disconnectManager().countVotesForPlayer(*g.peers[0].net, k.target, g.hub.clock) == 0);
		CHECK_FALSE(g.peers[0].net->isDisconnected(2));
		// the game goes on unchanged: slot 0 runs the same frames as the others
		for (int t = 0; t < 20; ++t)
		{
			g.tick();
		}
		CHECK(g.peers[0].hash != before);
		const UnsignedInt common = std::min({ g.peers[0].frame, g.peers[1].frame, g.peers[2].frame });
		CHECK(g.peers[0].hashes.at(common) == g.peers[1].hashes.at(common));
		CHECK(g.peers[0].hashes.at(common) == g.peers[2].hashes.at(common));
	}
	// a vote out of range never decodes (NetPacket: "about slot 16")
	NetCommandMsg v;
	v.type = NETCOMMANDTYPE_DISCONNECTVOTE;
	v.slot = 1;
	v.targetSlot = 2;
	std::vector<std::uint8_t> b = encode(v);
	b[6] = MAX_SLOTS;
	NetCommandMsg out;
	std::string error;
	CHECK_FALSE(NetPacket::decodeCommand(b, out, &error));
	CHECK(error.find("about slot " + std::to_string(MAX_SLOTS)) != std::string::npos);
}


namespace
{
std::vector<std::uint8_t> encodeOrFail(const NetCommandMsg &c)
{
	std::string error;
	std::vector<std::uint8_t> b = NetPacket::encodeCommand(c, &error);
	REQUIRE_MESSAGE(!b.empty(), error);
	return b;
}
bool hasError(const Network &n, const std::string &text)
{
	for (const std::string &e : n.errors())
	{
		if (e.find(text) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}
NetCommandMsg dropMsg(int sender, int target, std::uint32_t frame, std::uint8_t gone)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
	c.slot = (std::uint8_t)sender;
	c.targetSlot = (std::uint8_t)target;
	c.frame = frame;
	c.releasedMask = gone;
	return c;
}
} // namespace

TEST_CASE("mpdrop1 hostile (Sol r1): a drop the whole decision does not support is refused by name before anything changes: of the healthy receiver, "
		  "of another healthy player, with an out-of-game slot or the receiver in its departed list, at an impossible frame")
{
	struct Case
	{
		const char *name;
		int target;
		std::uint8_t gone;
		bool maxFrame;
		bool killTarget; ///< the target really crashed (the receiver has not announced yet)
		const char *refusal;
	};
	const Case cases[] = {
		{ "the packet router drops the healthy receiver", 1, 0x00, false, false, "its sender never announced a disconnect frame at or after it to this player: refused" },
		{ "the packet router drops another healthy player", 2, 0x00, false, false, "its sender never announced a disconnect frame at or after it to this player: refused" },
		{ "an out-of-game slot in the departed list", 2, 0x80, false, false, "names slot 7, which is not a player of the game: refused" },
		{ "the receiver in the departed list", 2, 0x02, false, false, "it claims this player is gone: refused" },
		{ "the target in the departed list", 2, 0x04, false, false, "names slot 2, the player it drops: refused" },
		{ "frame 0xFFFFFFFF for a crashed player", 2, 0x00, true, true, "its sender never announced a disconnect frame at or after it to this player: refused" },
	};
	for (const Case &k : cases)
	{
		CAPTURE(k.name);
		Game g(3, true);
		for (int t = 0; t < 40; ++t)
		{
			g.tick();
		}
		if (k.killTarget)
		{
			g.kill(k.target);
		}
		Network &rx = *g.peers[1].net; // slot 1 receives it from slot 0, the packet router
		g.hub.endpoint(1).inbox.push_back({ 0, encodeOrFail(dropMsg(0, k.target, k.maxFrame ? 0xFFFFFFFFu : g.peers[1].frame, k.gone)) });
		g.tick();
		INFO(g.logs({ 0, 1 }));
		CHECK(hasError(rx, k.refusal));
		CHECK(rx.errors().size() == 1);
		for (int s = 0; s < 3; ++s)
		{
			CHECK_FALSE(rx.isDisconnected(s)); // nothing changed: no drop, no departure, no quit
		}
		CHECK_FALSE(rx.quitRequested());
		CHECK(rx.allowedToContinue(g.peers[1].frame)); // nothing held
		const int ticks = k.killTarget ? 60 : 20;
		for (int t = 0; t < ticks; ++t)
		{
			g.tick();
		}
		if (k.killTarget)
		{
			// the real drop comes from the packet router when the bar runs out, at one frame on both survivors
			CHECK(g.peers[0].net->isDisconnected(2));
			CHECK(rx.isDisconnected(2));
			CHECK(g.peers[0].frame == g.peers[1].frame);
			checkInStep(g, { 0 }, g.peers[0].frame);
			CHECK(g.peers[0].hashes.at(g.peers[1].frame) == g.peers[1].hashes.at(g.peers[1].frame));
		}
		else
		{
			const UnsignedInt common = std::min({ g.peers[0].frame, g.peers[1].frame, g.peers[2].frame });
			CHECK(common > 50); // tolerance: the claim is that every player ran on after the refusal (the frames per tick follow the run-ahead)
			CHECK(g.peers[1].hashes.at(common) == g.peers[0].hashes.at(common));
			CHECK(g.peers[1].hashes.at(common) == g.peers[2].hashes.at(common));
		}
	}
}

TEST_CASE("mpdrop1 hostile: during a real crash, a drop from a player who is not the decider is refused by name and the decider's own drop follows")
{
	Game g(3, true);
	for (int t = 0; t < 60; ++t)
	{
		g.tick();
	}
	g.kill(2);
	// both survivors announce the frame they wait on once the bar is below a third (RW 0x8D8785)
	for (int t = 0; t < 200 && !(g.peers[0].net->disconnectManager().disconnectFrameReceived(1) && g.peers[1].net->disconnectManager().disconnectFrameReceived(0)); ++t)
	{
		g.tick();
	}
	REQUIRE(g.peers[0].net->disconnectManager().disconnectFrameReceived(1));
	REQUIRE_FALSE(g.peers[0].net->isDisconnected(2));
	// slot 1 claims the decision although slot 0 (first in the fallback order) is in the game and heard
	g.hub.endpoint(0).inbox.push_back({ 1, encodeOrFail(dropMsg(1, 2, g.peers[0].frame, 0x00)) });
	g.tick();
	INFO(g.logs({ 0, 1 }));
	CHECK(hasError(*g.peers[0].net, "which may not decide it"));
	CHECK(g.peers[0].net->errors().size() == 1);
	const std::uint64_t took = runUntilDropped(g, { 0, 1 }, 2, 2 * kTimeoutMs);
	CHECK(took != 0); // tolerance: 0 is the sentinel for never dropped; the timing of this drop is the plain timeout's (pinned above)
	CHECK(g.peers[0].net->disconnectManager().dropped() == std::vector<int>{ 2 });
	for (int t = 0; t < 30; ++t)
	{
		g.tick();
	}
	CHECK(g.peers[0].frame == g.peers[1].frame);
	checkInStep(g, { 1 }, g.peers[1].frame);
	CHECK(g.peers[0].hashes.at(g.peers[1].frame) == g.peers[1].hashes.at(g.peers[1].frame));
}

TEST_CASE("mpdrop1: a survivor that had not announced the drop frame refuses the drop, announces it later and gets it again from the decider: one "
		  "drop frame on every survivor")
{
	Game g(4, true);
	for (int t = 0; t < 60; ++t)
	{
		g.tick();
	}
	g.kill(3);
	// slot 2 keeps hearing slot 3 for a while (a stale link at its end), so its screen stays off and it announces nothing; around the decision the
	// decider (slot 0) does not hear slot 2 (silent for it, not timed out), so the decision cannot count slot 2
	bool refused = false;
	for (int t = 0; t < 200 && !refused; ++t)
	{
		g.hub.endpoint(2).heard[3] = g.hub.clock;
		if (t == 15)
		{
			g.hub.cut(2, 0, true);
		}
		g.tick();
		if (g.peers[0].net->isDisconnected(3))
		{
			g.hub.cut(2, 0, false);
		}
		refused = hasError(*g.peers[2].net, "this player did not announce that frame");
	}
	INFO(g.logs({ 0, 1, 2 }));
	REQUIRE(refused);
	CHECK_FALSE(g.peers[2].net->isDisconnected(3));
	CHECK_FALSE(g.peers[0].net->isDisconnected(2)); // slot 2 itself was never timed out
	// slot 2 hears the silence now: its screen, its announcement, the decider sends the drop again
	const std::uint64_t took = runUntilDropped(g, { 2 }, 3, 4 * (kDisconnectMs + kTimeoutMs));
	CHECK(took != 0); // tolerance: 0 is the sentinel for never; the claim is that the refused drop arrives again and is applied
	bool resent = false;
	for (const std::string &l : g.peers[0].net->disconnectManager().log())
	{
		resent = resent || l.rfind("sent the drop of slot 3 at frame ", 0) == 0;
	}
	CHECK(resent);
	for (int t = 0; t < 40; ++t)
	{
		g.tick();
	}
	const UnsignedInt common = std::min({ g.peers[0].frame, g.peers[1].frame, g.peers[2].frame });
	CHECK(common > 70); // tolerance: the claim is that the game went on after the drop on every survivor
	CHECK(g.peers[2].hashes.at(common) == g.peers[0].hashes.at(common));
	CHECK(g.peers[1].hashes.at(common) == g.peers[0].hashes.at(common));
	CHECK(g.peers[2].selfDestructFrames == g.peers[0].selfDestructFrames);
}

TEST_CASE("mpdrop1 (Sol r1): a slow loader that keeps sending is never dropped: every player stays on the load screen however long it takes, then "
		  "the game starts in step")
{
	Game g(3, true, 2); // slot 2 is still loading: it sends no LOADCOMPLETE
	g.hub.silence(2, false);
	for (int t = 0; t < (int)(4 * (4 * kDisconnectMs + kTimeoutMs) / kTickMs); ++t)
	{
		if (t % 2 == 0)
		{
			NetCommandMsg c; // what a loading client sends while it loads (here a keep-alive; S-4811: OpenBFME's clients send nothing yet)
			c.type = NETCOMMANDTYPE_DISCONNECTKEEPALIVE;
			c.slot = 2;
			g.peers[2].net->sendDirect(c);
		}
		g.tick();
		for (int s : { 0, 1 })
		{
			REQUIRE_FALSE(g.peers[(size_t)s].net->isDisconnected(2));
			REQUIRE_FALSE(g.peers[(size_t)s].net->disconnectManager().screen().visible);
			REQUIRE(g.peers[(size_t)s].net->disconnectManager().loadScreenUp());
		}
	}
	CHECK(g.peers[0].frame == 0);
	CHECK(g.peers[1].frame == 0);
	g.peers[2].net->sendLoadComplete(); // it finished loading
	g.peers[2].running = true;
	for (int t = 0; t < 60; ++t)
	{
		g.tick();
	}
	INFO(g.logs({ 0, 1, 2 }));
	const UnsignedInt common = std::min({ g.peers[0].frame, g.peers[1].frame, g.peers[2].frame });
	CHECK(common > 50); // tolerance: the claim is that the game started for all three
	CHECK(g.peers[0].hashes.at(common) == g.peers[2].hashes.at(common));
	CHECK(g.peers[1].hashes.at(common) == g.peers[2].hashes.at(common));
	CHECK_FALSE(g.peers[0].net->disconnectManager().loadScreenUp()); // gone at frame 6
	CHECK(g.peers[0].net->errors().empty());
}

TEST_CASE("mpdrop1 (Sol r1, S-4810 races): a stale DISCONNECTSCREENOFF does not reopen a player's announcement; a repeat after the packet router's own "
		  "release is counted and the drop is decided")
{
	SUBCASE("a screen-off of an earlier frame leaves the open announcement as it is (ZH processDisconnectScreenOff: only a frame at or after it)")
	{
		Game g(3, true);
		for (int t = 0; t < 60; ++t)
		{
			g.tick();
		}
		g.kill(2);
		const DisconnectManager &dm = g.peers[0].net->disconnectManager();
		for (int t = 0; t < 200 && !dm.disconnectFrameReceived(1); ++t)
		{
			g.tick();
		}
		REQUIRE(dm.disconnectFrameReceived(1));
		const std::uint32_t f = dm.disconnectFrame(1);
		NetCommandMsg off;
		off.type = NETCOMMANDTYPE_DISCONNECTSCREENOFF;
		off.slot = 1;
		off.frame = f - 1;
		off.releasedFrames.assign(MAX_SLOTS, 0u);
		g.hub.endpoint(0).inbox.push_back({ 1, encodeOrFail(off) });
		g.tick();
		CHECK(dm.disconnectFrameReceived(1));
		CHECK(dm.disconnectFrame(1) == f);
		INFO(g.logs({ 0, 1 }));
		CHECK(runUntilDropped(g, { 0, 1 }, 2, 2 * kTimeoutMs) != 0); // tolerance: 0 is the sentinel for never; the drop's timing is pinned above
		for (int t = 0; t < 20; ++t)
		{
			g.tick();
		}
		CHECK(g.peers[0].frame == g.peers[1].frame);
		CHECK(g.peers[0].hashes.at(g.peers[1].frame) == g.peers[1].hashes.at(g.peers[1].frame));
	}
	SUBCASE("the router released slot 1's announcement (its own screen was off), later its own screen comes on: slot 1's repeat of the same frame counts")
	{
		Game g(3, true);
		for (int t = 0; t < 60; ++t)
		{
			g.tick();
		}
		g.kill(2);
		const DisconnectManager &dm = g.peers[0].net->disconnectManager();
		bool released = false;
		for (int t = 0; t < 120; ++t)
		{
			g.hub.endpoint(0).heard[2] = g.hub.clock; // the router still hears slot 2 (a stale link at its end): its screen stays off
			g.tick();
			released = released || (dm.disconnectFrame(1) > 0 && !dm.disconnectFrameReceived(1) && !dm.screen().visible);
		}
		INFO(g.logs({ 0, 1 }));
		REQUIRE(released); // slot 1 announced; the router, screen off, released it at once
		CHECK_FALSE(g.peers[0].net->isDisconnected(2));
		const std::uint64_t took = runUntilDropped(g, { 0, 1 }, 2, 4 * (kDisconnectMs + kTimeoutMs));
		CHECK(took != 0); // tolerance: 0 is the sentinel for never (RW 0x8D836B's equal-frame rule alone froze here)
		for (int t = 0; t < 20; ++t)
		{
			g.tick();
		}
		CHECK(g.peers[0].frame == g.peers[1].frame);
		CHECK(g.peers[0].hashes.at(g.peers[1].frame) == g.peers[1].hashes.at(g.peers[1].frame));
	}
}

TEST_CASE("mpdrop1 hostile (Sol r2): at the load barrier a drop of the healthy receiver from a sender that never announced anything is refused by name; "
		  "an announcement of frame 0 does count")
{
	Game g(3, true, 2); // slot 2 never finishes loading: everybody is at the load barrier, frame 0
	for (int t = 0; t < 5; ++t)
	{
		g.tick();
	}
	Network &rx = *g.peers[1].net;
	REQUIRE_FALSE(g.peers[1].net->disconnectManager().disconnectFrameReceived(0));
	g.hub.endpoint(1).inbox.push_back({ 0, encodeOrFail(dropMsg(0, 1, 0, 0x00)) }); // sender 0, target 1 (the receiver), frame 0, empty list
	g.tick();
	INFO(g.logs({ 0, 1 }));
	CHECK(hasError(rx, "its sender never announced a disconnect frame at or after it to this player: refused"));
	CHECK(rx.errors().size() == 1);
	CHECK_FALSE(rx.quitRequested());
	CHECK_FALSE(rx.disconnectManager().quitRequested());
	for (int s = 0; s < 3; ++s)
	{
		CHECK_FALSE(rx.isDisconnected(s));
	}
	// the same drop after the sender really announced frame 0 (a DISCONNECTFRAME) is not refused for that reason
	NetCommandMsg ann;
	ann.type = NETCOMMANDTYPE_DISCONNECTFRAME;
	ann.slot = 0;
	ann.frame = 0;
	g.hub.endpoint(1).inbox.push_back({ 0, encodeOrFail(ann) });
	g.hub.endpoint(1).inbox.push_back({ 0, encodeOrFail(dropMsg(0, 2, 0, 0x00)) }); // a real drop of the silent loader, frame 0
	g.tick();
	CHECK(rx.disconnectManager().disconnectFrameReceived(0));
	// the sender's frame-0 announcement counts: this drop of another player is now refused only because the receiver did not announce frame 0 itself
	REQUIRE(rx.errors().size() == 2);
	CHECK(rx.errors()[1].find("this player did not announce that frame") != std::string::npos);
	CHECK(rx.errors()[1].find("never announced") == std::string::npos);
}

// lane CHEAT-1 (Sol review r2 / r3): a resent frame holding MSG_OPENBFME_CHEAT. (1) The host's frame with a Cheat Mode command reached one survivor only,
// then the host died: the survivors still reach the same frame and drop it (the silent owner's resend is taken), exactly as with an ordinary command.
// (2) While the host is heard, a resend of a host frame holding a Cheat Mode command is set aside even when another player's silence put the disconnect
// screen up: the frame waits for the host's own packet, and every peer runs the same frame
TEST_CASE("cheat1 disconnect: a host frame with a Cheat Mode command that one survivor missed still lets the survivors drop the host")
{
	for (const bool cheat : { false, true })
	{
		Game g(3, false);
		for (int t = 0; t < 60; ++t)
		{
			g.tick();
		}
		g.hub.cut(0, 2, true);
		GameMessage command(cheat ? MSG_OPENBFME_CHEAT : MSG_DO_STOP, 10);
		if (cheat)
		{
			for (int v : { 1, 10, 2000 })
			{
				command.appendIntegerArgument(v);
			}
		}
		g.peers[0].pending.append(command);
		for (int t = 0; t < 5; ++t)
		{
			g.tick();
		}
		g.kill(0);
		const auto took = runUntilDropped(g, { 1, 2 }, 0, 60000);
		for (int t = 0; t < 50; ++t)
		{
			g.tick();
		}
		INFO(g.logs({ 1, 2 }));
		CHECK(took > 0);                    // tolerance: the drop's time follows the disconnect timers; it must happen at all
		CHECK(g.peers[1].frame > 100);      // tolerance: both survivors play on past the drop
		CHECK(g.peers[2].frame > 100);      // tolerance: both survivors play on past the drop
		CHECK(g.peers[1].hashes[100] == g.peers[2].hashes[100]); // Sol r4 follow-up: and run the same game
	}
}

TEST_CASE("cheat1 disconnect: while the host is heard, a resent host frame holding a Cheat Mode command is set aside even with the disconnect screen up")
{
	Game g(3, false);
	for (int t = 0; t < 60; ++t)
	{
		g.tick();
	}
	g.hub.cut(1, 2, true); // slot 1 goes silent for slot 2 (its screen comes up); the host's link stays healthy
	for (int t = 0; t < 8; ++t)
	{
		g.tick();
	}
	auto &victim = g.peers[2];
	REQUIRE(victim.net->disconnectManager().state() != DisconnectManager::SCREENOFF);
	const auto targetFrame = std::max(g.peers[0].frame, victim.frame) + 10;
	NetCommandMsg resend;
	resend.slot = 1;
	resend.type = NETCOMMANDTYPE_FRAMERESEND;
	resend.targetSlot = 0;
	resend.frame = targetFrame;
	auto msg = std::make_shared<GameMessage>(MSG_OPENBFME_CHEAT, 11);
	for (int v : { 8, -1, 0 })
	{
		msg->appendIntegerArgument(v);
	}
	resend.messages.push_back(msg);
	const auto errors = victim.net->errors().size();
	g.hub.endpoint(2).inbox.push_back({ 1, NetPacket::encodeCommand(resend, nullptr) });
	victim.net->update(victim.frame, victim.pending);
	CHECK(victim.net->errors().size() == errors + 1);
	g.hub.cut(1, 2, false);
	for (int t = 0; t < 70; ++t)
	{
		g.tick();
	}
	REQUIRE(g.peers[0].frame > targetFrame); // tolerance: both played past the frame
	REQUIRE(victim.frame > targetFrame);     // tolerance: both played past the frame
	bool hostCheat = false, victimCheat = false;
	for (const auto &c : g.peers[0].relayed[targetFrame])
	{
		hostCheat = hostCheat || c == "32001 10";
	}
	for (const auto &c : victim.relayed[targetFrame])
	{
		victimCheat = victimCheat || c == "32001 10";
	}
	CHECK_FALSE(hostCheat);
	CHECK_FALSE(victimCheat);
	CHECK(g.peers[0].hashes[targetFrame + 1] == victim.hashes[targetFrame + 1]);
}

// lane MPDROP-2 (found by WOTR2's stalled-client test on the strategic map): a skirmish player whose game hangs past the disconnect time is dropped by the
// others; when its game runs again, its network learns it was dropped ("dropped by the other players", DisconnectManager) in the same update that brings
// in the others' commands for the frames they ran, the drop's frame included. LiveGame acquires every due frame of an advance (a hung game is far behind
// its clock) before GDScript reads quit_requested, so the frame driver itself must hand out no frame once its network asks the player to leave: the drop's
// frame runs on the others without this player (its army handed over, LAN), so running it here is a game of its own
TEST_CASE("mpdrop2: a skirmish player whose game hangs is dropped by the others; when it runs again its frame driver runs no frame once its network asks "
		  "it to leave, and every frame it ran is one the others ran with the same state")
{
	for (const bool online : { false, true })
	{
		CAPTURE(online);
		Game g(3, online, -1, 8);
		for (int t = 0; t < 60; ++t)
		{
			g.tick();
		}
		const std::vector<int> survivors{ 0, 1 };
		g.hang(2, true);
		const UnsignedInt hungAt = g.peers[2].frame;
		REQUIRE(runUntilDropped(g, survivors, 2, 4 * (kDisconnectMs + kTimeoutMs)) != 0); // tolerance: the drop's timing is the two-to-eight case's; here only that it came
		for (int t = 0; t < 10; ++t)
		{
			g.tick(); // the survivors run on without it
		}
		INFO(g.logs({ 0, 1, 2 }));
		REQUIRE(g.peers[0].frame > hungAt + 5); // tolerance: the survivors played on past the drop
		CHECK(g.peers[2].frame == hungAt);
		// its game runs again and its player gives an order at once (its first frames are ones the others ran without it: a split game if they run)
		g.hang(2, false);
		g.peers[2].pending.append(GameMessage(MSG_DO_STOP, g.peers[2].net->config().slotPlayerIndex[2]));
		bool asked = false;
		for (int t = 0; t < 50 && !asked; ++t)
		{
			g.tick();
			asked = g.peers[2].net->quitRequested();
		}
		for (int t = 0; t < 20; ++t)
		{
			g.tick(); // and its game runs on a while
		}
		REQUIRE(asked);
		CHECK(g.peers[2].net->quitReason().find("dropped by the other players") == 0);
		CHECK(g.peers[2].framesAfterQuit == 0);
		CHECK(g.peers[2].frame == hungAt); // the drop came in with the first update after the hang: it ran nothing more
		CHECK(g.peers[2].selfDestructFrames.empty());
		// every frame it ran is one the survivors ran, with the same state (no split game)
		const Peer &a = g.peers[0];
		for (const auto &h : g.peers[2].hashes)
		{
			CAPTURE(h.first);
			REQUIRE(a.hashes.count(h.first));
			CHECK(a.hashes.at(h.first) == h.second);
		}
		checkInStep(g, survivors, g.peers[1].frame);
		CHECK(g.peers[0].selfDestructFrames.size() == (online ? 0u : 1u));
	}
}
