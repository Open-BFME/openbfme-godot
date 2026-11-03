// OpenBFME unit tests. GPL-3.0.
// WOTR2 milestone 8 (step 2): a War of the Ring's strategic map in a network game (GameNetwork/StrategicSession.h; spec
// wotr2-work/specs/V-lan-war-of-the-ring.md): the war every peer builds from the lobby's GameInfo, the wire (the GameInfo's game mode and scenario under
// LANAPI 5 / LANLobby 8 and 9, the message families), and peers on an in-memory network (the harness of test_mpdrop1_disconnect.cpp) playing the map in
// lockstep with the phase timer, a graceful leave and a dropped peer whose whole team leaves the war. No retail data: the stores are parsed from text.

#include "doctest.h"

#include "Common/INI.h"
#include "GameClient/EndGame.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/LivingWorld/LivingWorldData.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/StrategicSession.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace LivingWorld;

namespace
{
const char *kNetWar = R"(
LivingWorldPlayerTemplate PlayerGood
	Faction = FactionGood
	StartingWorldCP = 100
	MaxWorldCP = 300
	ScenarioStartResources = 1000
	ScenarioMaxResources = 5000
End
LivingWorldPlayerTemplate PlayerEvil
	Faction = FactionEvil
	StartingWorldCP = 100
	MaxWorldCP = 300
	ScenarioStartResources = 1000
End
AutoResolveWeapon AutoResolve_DefaultWeapon
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Soldier
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Hero
	MissPercentChance = 50
End
AutoResolveArmor AutoResolve_DefaultArmor
	Armor = AutoResolveUnit_Soldier 100%
End
AutoResolveBody AutoResolve_DefaultBody
	HitpointsAtLevel = Hitpoints:300 Level:0
End
AutoResolveBody AutoResolve_HeroBody
	HitpointsAtLevel = Hitpoints:2000 Level:1
	LeaveInArmySummary = Yes
End
AutoResolveCombatChain AutoResolve_DefaultCombatChain
End
AutoResolveReinforcementSchedule Defender
	1 = 0
	EachRemaining = +1
End
AutoResolveReinforcementSchedule Attacker
	1 = 0
	EachRemaining = +1
End
LivingWorldAITemplate DefaultAITemplate
	BuildingScoreFarm = 50
End
LivingWorldPlayerArmy
	Name = GoodHeroArmy
	ArmyEntry
		ThingTemplate = GoodHero
		Quantity = 1
	End
	ArmyEntry
		ThingTemplate = GoodSoldiers
		Quantity = 2
	End
End
LivingWorldPlayerArmy
	Name = EvilHeroArmy
	ArmyEntry
		ThingTemplate = EvilHero
		Quantity = 1
	End
End
LivingWorldBuilding LWB_Farm
	AvailableTo = PlayerGood PlayerEvil
	TurnsToBuild = 1
	StrategicResourceCost = 100
	Type = Resource
	BuildingNugget IncreaseTreasury NuggetTag_GiveLoot
		TreasureAmount = 300
	END
End
LivingWorldRegionCampaign NetCampaign
	SmallArmyCommandPoints = 120
	Region A
		MapName = "MAP A"
		BuildingSpot = X:1 Y:1
		ConnectsTo = B E
	End
	Region B
		MapName = "MAP B"
		BuildingSpot = X:1 Y:1
		ConnectsTo = A C
	End
	Region C
		MapName = "MAP C"
		BuildingSpot = X:1 Y:1
		ConnectsTo = B D
	End
	Region D
		MapName = "MAP D"
		BuildingSpot = X:1 Y:1
		ConnectsTo = C E
	End
	Region E
		MapName = "MAP E"
		BuildingSpot = X:1 Y:1
		ConnectsTo = D A
	End
End
LivingWorldCampaign NetScenario
	Scenario
		DisplayName = LWScenario:Net
		RegionCampaign = NetCampaign
		MaxPlayers = 4
		DefaultStartSpots = A B C D
		PlayerDefeatCondition
			Teams = 0 1 2
			LoseIfCapitalLost = Yes
		End
		TeamDefeatCondition
			Teams = 0 1 2
			NumControlledRegionsLessOrEqualTo = 0
		End
	End
	Act One
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerGood
			HeroTemplateName = GoodHero
			PlayerArmy = GoodHeroArmy
		End
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerEvil
			HeroTemplateName = EvilHero
			PlayerArmy = EvilHeroArmy
		End
	End
End
)";

UnitInfo netUnits(const std::string &name)
{
	UnitInfo u;
	u.known = true;
	if (name == "GoodHero" || name == "EvilHero")
	{
		u.hero = true;
		u.buildCost = 1000;
		u.strength = 1000;
		u.autoResolveUnitType = AR_HERO;
		u.autoResolveBody = "AutoResolve_HeroBody";
		u.levels = { { 1, 1, 50 } };
	}
	else if (name == "GoodSoldiers")
	{
		u.buildCost = 300;
		u.commandPoints = 20;
		u.strength = 300;
		u.autoResolveUnitType = AR_SOLDIER;
	}
	else
	{
		u.known = false;
	}
	return u;
}

LivingWorldStores &netStores()
{
	static LivingWorldStores stores;
	static bool loaded = false;
	if (!loaded)
	{
		INIEnvironment env;
		stores.registerBlocks(env.blocks);
		INI ini(env);
		const std::string text = kNetWar;
		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		ini.loadMemory("wotr8-net.ini", bytes, INI_LOAD_OVERWRITE);
		loaded = true;
	}
	return stores;
}

const std::vector<std::string> kTemplates{ "FactionGood", "FactionEvil" };

// the host's War of the Ring: Ann (slot 0) and Bob (slot 1) are allies (team 0), Cid (slot 2) is alone (team 1), an AI (slot 3) alone (team 2); a 5 s
// phase timer (25 frames)
SkirmishGameInfo netGame(int timerSeconds = 5)
{
	SkirmishGameInfo g;
	g.gameMode = 1;
	g.scenario = "NetScenario";
	g.seed = 42;
	g.mapName = "maps\\map wor mirkwood\\map wor mirkwood.map"; // RW 0xC54AE0: the placeholder
	g.rules.fill(0);
	g.rules[RULE_STRATEGIC_PHASE_TIMER] = timerSeconds;
	const char16_t *names[] = { u"Ann", u"Bob", u"Cid", u"Orc" };
	const int templates[] = { 0, 0, 1, 1 };
	const int teams[] = { 0, 0, 1, 2 };
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &s = g.slots[i];
		s.state = i < 3 ? SLOT_PLAYER : SLOT_MED_AI;
		s.name = names[i];
		s.playerTemplate = templates[i];
		s.teamNumber = teams[i];
		s.color = i;
		s.startPos = i; // region index: A, B, C, D
	}
	for (int i = 4; i < LEGACY_MAX_SLOTS; ++i)
	{
		g.slots[i].state = SLOT_CLOSED;
	}
	return g;
}

// the in-memory network of test_mpdrop1_disconnect.cpp: every datagram arrives a tick later; a killed slot sends and receives nothing
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
	void silence(int slot)
	{
		for (int i = 0; i < (int)m_endpoints.size(); ++i)
		{
			m_cut.insert({ slot, i });
			m_cut.insert({ i, slot });
		}
	}
	// a hung process: nothing heard from `slot` (it sends nothing) while datagrams to it still arrive and wait in its inbox
	void mute(int slot, bool on)
	{
		for (int i = 0; i < (int)m_endpoints.size(); ++i)
		{
			if (on)
			{
				m_cut.insert({ slot, i });
			}
			else
			{
				m_cut.erase({ slot, i });
			}
		}
	}
	void queue(int from, int to, const std::vector<std::uint8_t> &b) { m_inFlight.push_back(Flight{ m_now + 1, from, to, b }); }
	void tick(std::uint64_t ms)
	{
		++m_now;
		clock += ms;
		std::deque<Flight> keep;
		for (Flight &f : m_inFlight)
		{
			if (f.at <= m_now && !m_cut.count({ f.from, f.to }))
			{
				m_endpoints[(size_t)f.to]->inbox.push_back({ f.from, f.bytes });
			}
			else if (!m_cut.count({ f.from, f.to }))
			{
				keep.push_back(f);
			}
		}
		m_inFlight.swap(keep);
		for (int a = 0; a < (int)m_endpoints.size(); ++a)
		{
			for (int b = 0; b < (int)m_endpoints.size(); ++b)
			{
				if (a != b && !m_cut.count({ b, a }))
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
	std::set<std::pair<int, int>> m_cut;
	std::deque<Flight> m_inFlight;
	int m_now = 0;
};

constexpr std::uint32_t kDisconnectMs = 1000;
constexpr std::uint32_t kTimeoutMs = 6000;

struct Peer
{
	std::unique_ptr<LivingWorldLogic> logic;
	std::unique_ptr<StrategicSession> session;
	bool running = true;
	std::set<std::pair<int, int>> voted; ///< (turn, battle) this peer's player voted on
};

struct NetWar
{
	Hub hub;
	WarStart war;
	std::vector<Peer> peers;
	// `online` (WOTR2 milestone 8 step 4): the relay mode's disconnect rule (a dropped player is held for a rejoin; its surrender comes when the window ends)
	explicit NetWar(const SkirmishGameInfo &g, int humans = 3, bool online = false)
		: hub(humans)
		, peers((size_t)humans)
	{
		std::string error;
		REQUIRE_MESSAGE(BuildWarStart(g, netStores(), kTemplates, war, &error), error);
		for (int i = 0; i < humans; ++i)
		{
			Peer &p = peers[(size_t)i];
			p.logic = std::make_unique<LivingWorldLogic>(netStores(), netUnits);
			REQUIRE_MESSAGE(p.logic->start(war.setup, &error), error);
			NetworkConfig c;
			c.localSlot = i;
			for (int s = 0; s < humans; ++s)
			{
				c.slotPlayerIndex[(size_t)s] = war.slotPlayer[(size_t)s]; // the human slots' players; the AI slot is no network slot
				c.slotNames[(size_t)s] = g.slots[s].name;
			}
			c.runAhead = 2;
			c.crcInterval = 10;
			c.settings.disconnectTime = kDisconnectMs;
			c.settings.playerTimeoutTime = kTimeoutMs;
			c.holdForRejoin = online;
			c.rejoinWindowMs = 3000;
			p.session = std::make_unique<StrategicSession>(hub.endpoint(i), c, *p.logic, war.phaseTimerSeconds);
			Hub *h = &hub;
			p.session->network().setClock([h] { return h->clock; });
		}
	}
	// one lockstep tick of every live peer (200 ms: one logic frame at 5 a second); each peer votes auto-resolve on a pending battle of its player
	void tick()
	{
		for (Peer &p : peers)
		{
			if (!p.running)
			{
				continue;
			}
			const int battle = p.logic->pendingHumanBattle();
			const int me = p.session->localPlayer();
			if (battle >= 0 && !p.voted.count({ p.logic->turn(), battle }))
			{
				const Battle &b = p.logic->battles()[(size_t)battle];
				bool mine = b.defender == me || b.attacker == me;
				for (int id : b.armies)
				{
					const Army *a = p.logic->findArmy(id);
					mine = mine || (a && a->owner == me);
				}
				if (mine)
				{
					GameMessage vote(MSG_LW_SET_CONFLICT_RESOLUTION_METHOD, me);
					vote.appendIntegerArgument(1);
					vote.appendIntegerArgument(battle);
					REQUIRE(p.session->post(vote));
					p.voted.insert({ p.logic->turn(), battle });
				}
			}
			p.session->step();
		}
		hub.tick(200);
	}
	void kill(int slot)
	{
		hub.silence(slot);
		peers[(size_t)slot].running = false;
	}
	GameMessage done(int peer) const
	{
		const LivingWorldLogic &lw = *peers[(size_t)peer].logic;
		const int me = peers[(size_t)peer].session->localPlayer();
		GameMessage m(MSG_LW_DONE_PHASE, me);
		m.appendIntegerArgument(lw.turn());
		m.appendIntegerArgument(lw.phase());
		m.appendIntegerArgument(me);
		return m;
	}
	std::string errors() const
	{
		std::string out;
		for (size_t i = 0; i < peers.size(); ++i)
		{
			for (const std::string &e : peers[i].session->errors())
			{
				out += "peer " + std::to_string(i) + ": " + e + "\n";
			}
		}
		return out;
	}
};

// every peer in `who` hashed the same state after every frame both ran, over at least `minShared` frames (no tolerance: a tick steps the peers in turn, so
// one may have run a frame more; the frames both ran must agree exactly)
void checkInStep(const NetWar &w, const std::vector<int> &who, size_t minShared)
{
	const std::vector<std::uint32_t> &a = w.peers[(size_t)who[0]].session->hashes();
	for (int i : who)
	{
		INFO("peer " << i);
		const std::vector<std::uint32_t> &b = w.peers[(size_t)i].session->hashes();
		const size_t n = std::min(a.size(), b.size());
		REQUIRE(n >= minShared); // tolerance: a coverage floor (how many frames both ran); every shared frame is compared exactly below
		for (size_t f = 0; f < n; ++f)
		{
			if (a[f] != b[f])
			{
				FAIL("frame " << f + 1 << " differs");
			}
		}
		CHECK(w.peers[(size_t)i].session->network().desyncs().empty());
	}
}

const StrategicSession::Executed *findExecuted(const StrategicSession &s, int type, int sender)
{
	for (const StrategicSession::Executed &e : s.executed())
	{
		if (e.type == type && e.sender == sender)
		{
			return &e;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("wotr8 net: the war every peer builds from the START's GameInfo (CreatePlayers RW 0x932EB8): slot order, region starts, the AI level, the "
		  "handicap, the timer; refusals by name")
{
	SkirmishGameInfo g = netGame();
	g.slots[1].handicap = -25;
	WarStart w;
	std::string error;
	REQUIRE_MESSAGE(BuildWarStart(g, netStores(), kTemplates, w, &error), error);
	REQUIRE(w.setup.slots.size() == 4);
	CHECK(w.setup.campaign == "NetScenario");
	CHECK(w.setup.seed == 42);
	CHECK(w.phaseTimerSeconds == 5);
	CHECK(w.slotPlayer[0] == 0);
	CHECK(w.slotPlayer[3] == 3);
	CHECK(w.slotPlayer[4] == -1);
	CHECK(w.setup.slots[1].name == "Bob");
	CHECK(w.setup.slots[1].handicap == 25); // -(slot + 0x20)
	CHECK(w.setup.slots[2].faction == "FactionEvil");
	CHECK(w.setup.slots[2].startRegion == "C");
	CHECK(w.setup.slots[3].human == false);
	CHECK(w.setup.slots[3].aiDifficulty == SLOT_MED_AI);
	CHECK(w.setup.slots[0].team == 0);
	{
		SkirmishGameInfo open = g;
		open.gameMode = 0;
		CHECK_FALSE(BuildWarStart(open, netStores(), kTemplates, w, &error));
		CHECK(error == "the game is not a War of the Ring (game mode 0)");
	}
	{
		SkirmishGameInfo bad = g;
		bad.scenario = "Nowhere";
		CHECK_FALSE(BuildWarStart(bad, netStores(), kTemplates, w, &error));
		CHECK(error == "no LivingWorldCampaign with a Scenario named 'Nowhere'");
	}
	{
		SkirmishGameInfo bad = g;
		bad.slots[2].startPos = -1; // GUI:NeedStartSpots
		CHECK_FALSE(BuildWarStart(bad, netStores(), kTemplates, w, &error));
		CHECK(error == "slot 2 has no start region (-1)");
		bad.slots[2].startPos = 5;
		CHECK_FALSE(BuildWarStart(bad, netStores(), kTemplates, w, &error));
		CHECK(error == "slot 2 has no start region (5)");
	}
	{
		SkirmishGameInfo bad = g;
		bad.slots[0].playerTemplate = -1; // a random faction: GUI:DisabledFaction
		CHECK_FALSE(BuildWarStart(bad, netStores(), kTemplates, w, &error));
		CHECK(error == "slot 0 has no faction (-1)");
	}
}

TEST_CASE("wotr8 net: the wire: the GameInfo's game mode and scenario travel only in LANAPI 5 / LANLobby 8 and 9 messages; replay and save bytes are "
		  "unchanged; a version 6 or 7 START is refused by name; each lockstep game refuses the other family's messages")
{
	const SkirmishGameInfo war = netGame();
	// without the war fields (a replay, a save: Recorder / GameState write it so) the bytes are the ones before the fields existed: an open-play GameInfo
	// written with the default is the same as written with warFields false, and the LAN form only appends u8 mode + str scenario
	SkirmishGameInfo open = war;
	open.gameMode = 0;
	open.scenario.clear();
	NetByteWriter plain, lan;
	NetPacket::writeGameInfo(plain, open);
	NetPacket::writeGameInfo(lan, open, true);
	REQUIRE(lan.data().size() == plain.data().size() + 1 + 4 + 4 + 4); // u8 mode, str scenario (""), step 4: str resume save (""), u32 resume hash
	CHECK(std::equal(plain.data().begin(), plain.data().end(), lan.data().begin()));
	{
		NetByteReader r(lan.data());
		SkirmishGameInfo back;
		std::string error;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error, false, true, true, true), error);
		CHECK(back == open);
		CHECK(r.atEnd());
	}
	// the war's START round trip (version 8)
	LobbyStart start;
	start.game.mode = NewGameMode::Skirmish;
	start.game.game = war;
	start.networkSlot[0] = start.networkSlot[1] = start.networkSlot[2] = true;
	std::vector<std::uint8_t> bytes = LANLobby::encodeStart(start);
	CHECK(bytes[3] == 10); // step 4: lobby 10
	LobbyStart out;
	std::string error;
	REQUIRE_MESSAGE(LANLobby::decodeStart(bytes, out, &error), error);
	CHECK(out.game.game.warOfTheRing());
	CHECK(out.game.game.scenario == "NetScenario");
	CHECK(out.game.game == war);
	// a peer of an earlier lobby (6, 8) or of an old build's extended START (7, 9): refused by its version byte, never parsed as another layout
	for (std::uint8_t v : { 6, 7, 8, 9 })
	{
		std::vector<std::uint8_t> old = bytes;
		old[3] = v;
		CHECK_FALSE(LANLobby::decodeStart(old, out, &error));
		CHECK(error == "lobby START version " + std::to_string(v) + " (this build speaks 10, 11 for a 16-slot map)");
	}
	// a war without a scenario, open play with one, an unknown mode: refused
	for (int which = 0; which < 3; ++which)
	{
		SkirmishGameInfo bad = war;
		if (which == 0)
		{
			bad.scenario.clear();
		}
		else if (which == 1)
		{
			bad.gameMode = 0;
		}
		else
		{
			bad.gameMode = 2;
		}
		NetByteWriter w;
		NetPacket::writeGameInfo(w, bad, true);
		NetByteReader r(w.data());
		SkirmishGameInfo back;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &error, false, true, true, true));
		CHECK(error == (which == 0 ? "GameInfo War of the Ring without a scenario (or a name over 128 bytes)"
		                : which == 1 ? "GameInfo open play names the scenario \"NetScenario\""
		                             : "GameInfo game mode 2 (0 open play, 1 War of the Ring)"));
	}
	// the message families: a skirmish refuses MSG_LW_* as before; the strategic map refuses the RTS logic's messages
	GameMessage order(MSG_LW_ARMY_MOVE_REQUEST, 0);
	order.appendIntegerArgument(1);
	order.appendIntegerArgument(2);
	GameMessage stop(MSG_DO_STOP, 0);
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_GAMECOMMAND;
	c.message = std::make_shared<GameMessage>(order);
	CHECK(NetPacket::encodeCommand(c, &error).empty());
	CHECK(error == "GameMessage type 1704 is not a logic message (1001 .. 1147, or 32001 lane CHEAT-1)");
	const std::vector<std::uint8_t> strategic = NetPacket::encodeCommand(c, &error, NetPacket::MessageFamily::Strategic);
	REQUIRE_FALSE(strategic.empty());
	NetCommandMsg d;
	CHECK_FALSE(NetPacket::decodeCommand(strategic, d, &error));
	CHECK(error == "GameMessage type 1704 is not a logic message (1001 .. 1147, or 32001 lane CHEAT-1)");
	REQUIRE_MESSAGE(NetPacket::decodeCommand(strategic, d, &error, NetPacket::MessageFamily::Strategic), error);
	CHECK(d.message->getType() == MSG_LW_ARMY_MOVE_REQUEST);
	c.message = std::make_shared<GameMessage>(stop);
	const std::vector<std::uint8_t> logic = NetPacket::encodeCommand(c, &error);
	REQUIRE_FALSE(logic.empty());
	CHECK_FALSE(NetPacket::decodeCommand(logic, d, &error, NetPacket::MessageFamily::Strategic));
	CHECK(error == "GameMessage type " + std::to_string((int)MSG_DO_STOP) + " is not a strategic message (1701 .. 1728, the LAN war save 32002, the logic CRC or a self-destruct)");
	CHECK(NetPacket::messageInFamily(MSG_LOGIC_CRC, NetPacket::MessageFamily::Strategic));
	CHECK(NetPacket::messageInFamily(MSG_SELF_DESTRUCT, NetPacket::MessageFamily::Strategic));
	CHECK_FALSE(NetPacket::messageInFamily(MSG_LW_DONE_PHASE, NetPacket::MessageFamily::Logic));
}

TEST_CASE("wotr8 net: the phase timer (RW 0x6B359B / 0x6B4779 / 0x6B47AA / 0x6B5113) exists only in a network war's orders phase; the clock is hashed only "
		  "in a network war")
{
	WarStart w;
	std::string error;
	REQUIRE(BuildWarStart(netGame(), netStores(), kTemplates, w, &error));
	LivingWorldLogic single(netStores(), netUnits), net(netStores(), netUnits);
	REQUIRE(single.start(w.setup, &error));
	REQUIRE(net.start(w.setup, &error));
	const std::uint32_t before = single.stateHash();
	single.advanceFrame();
	CHECK(single.stateHash() == before); // a single player war: no timer, the clock is not state
	CHECK_FALSE(single.phaseTimerEnabled());
	CHECK(single.phaseFramesRemaining() == 0x7FFFFFFF);
	net.setNetworked(5);
	CHECK(net.stateHash() != before);
	CHECK(net.phaseTimerEnabled());
	CHECK(net.phaseFramesRemaining() == 25);
	CHECK(net.phaseSecondsRemaining() == 5);
	for (int f = 0; f < 24; ++f)
	{
		net.advanceFrame();
	}
	CHECK(net.phaseFramesRemaining() == 1);
	CHECK(net.phaseSecondsRemaining() == 1); // (1 + 4) / 5
	CHECK_FALSE(net.phaseTimerExpired());
	net.advanceFrame();
	CHECK(net.phaseTimerExpired());
	CHECK(net.mayEndPhase(0));
	REQUIRE(net.endTurn(0, &error));
	CHECK_FALSE(net.mayEndPhase(0)); // L + 0x344 set: nothing more to send
	LivingWorldLogic none(netStores(), netUnits);
	REQUIRE(none.start(w.setup, &error));
	none.setNetworked(0);
	CHECK_FALSE(none.phaseTimerEnabled()); // the rule's "none"
}

TEST_CASE("wotr8 net: three peers play the strategic map in lockstep; the timer ends a phase for a peer that never presses END PHASE; a dropped peer's "
		  "whole team leaves the war at one frame on every survivor, who play on in step")
{
	NetWar w(netGame());
	// turn 1: Ann and Bob end their phase at once; Cid never does: its timer (25 frames) sends Cid's DONE_PHASE
	for (int t = 0; t < 3; ++t)
	{
		w.tick();
	}
	REQUIRE(w.peers[0].session->post(w.done(0)));
	REQUIRE(w.peers[1].session->post(w.done(1)));
	std::uint32_t turn2At[3] = { 0, 0, 0 };
	for (int t = 0; t < 60 && w.peers[2].logic->turn() < 2; ++t)
	{
		w.tick();
		for (int i = 0; i < 3; ++i)
		{
			if (turn2At[i] == 0 && w.peers[(size_t)i].logic->turn() == 2)
			{
				turn2At[i] = w.peers[(size_t)i].session->frame();
			}
		}
	}
	INFO(w.errors());
	REQUIRE(w.peers[2].logic->turn() == 2);
	const StrategicSession::Executed *cid = findExecuted(*w.peers[0].session, MSG_LW_DONE_PHASE, 2);
	REQUIRE(cid);
	CHECK(cid->ok);
	CHECK(cid->frame >= 25);     // tolerance: the timer runs out on frame 25; the message's execution frame adds the run-ahead (2, or 1 with lane
	CHECK(cid->frame <= 25 + 3); // tolerance: NETLAG-2's early orders when every peer can learn of it in time), which the network decides
	// the turn changed on the same frame everywhere (each peer saw it after the same number of frames)
	for (int i = 0; i < 3; ++i)
	{
		CHECK(w.peers[(size_t)i].logic->turn() == 2);
	}
	CHECK(turn2At[0] == turn2At[1]);
	CHECK(turn2At[1] == turn2At[2]);
	// turn 2: Ann moves its army (an order on the wire), everyone runs on the timer; then two more turns
	const Army *ann = nullptr;
	for (const Army &a : w.peers[0].logic->armies())
	{
		ann = a.owner == 0 && !a.garrison ? &a : ann;
	}
	REQUIRE(ann);
	GameMessage move(MSG_LW_ARMY_MOVE_REQUEST, 0);
	move.appendIntegerArgument(ann->id);
	move.appendIntegerArgument(4); // A -> E (neutral)
	move.appendLocationArgument(Coord3D{});
	REQUIRE(w.peers[0].session->post(move));
	for (int t = 0; t < 200 && w.peers[0].logic->turn() < 4; ++t)
	{
		w.tick();
	}
	REQUIRE(w.peers[0].logic->turn() == 4);
	const StrategicSession::Executed *moved = findExecuted(*w.peers[2].session, MSG_LW_ARMY_MOVE_REQUEST, 0);
	REQUIRE(moved);
	CHECK_MESSAGE(moved->ok, moved->error);
	CHECK(w.peers[2].logic->regions()[4].owner == w.peers[0].logic->regions()[4].owner);
	checkInStep(w, { 0, 1, 2 }, 60);
	CHECK(w.errors().empty());
	// Bob's game dies: the disconnect path drops slot 1 on every survivor, and the frame's MSG_SELF_DESTRUCT becomes Bob's 0x6B8: Bob and Ann (team 0) leave
	w.kill(1);
	for (int t = 0; t < 100 && !(w.peers[0].logic->players()[1].defeated && w.peers[2].logic->players()[1].defeated); ++t)
	{
		w.tick();
	}
	INFO(w.errors());
	for (int i : { 0, 2 })
	{
		const LivingWorldLogic &lw = *w.peers[(size_t)i].logic;
		CHECK(lw.players()[0].defeated); // the whole team (RW 0x6BECB5)
		CHECK(lw.players()[1].defeated);
		CHECK_FALSE(lw.players()[2].defeated);
		CHECK_FALSE(lw.players()[3].defeated);
		CHECK_FALSE(lw.finished()); // two teams are left
		for (const Army &a : lw.armies())
		{
			CHECK((a.owner == 2 || a.owner == 3)); // RemovePlayer destroyed team 0's armies
		}
	}
	const StrategicSession::Executed *s0 = findExecuted(*w.peers[0].session, MSG_LW_PLAYER_SURRENDERED, 1);
	const StrategicSession::Executed *s2 = findExecuted(*w.peers[2].session, MSG_LW_PLAYER_SURRENDERED, 1);
	REQUIRE(s0);
	REQUIRE(s2);
	CHECK(s0->frame == s2->frame);
	CHECK(s0->ok);
	// the survivors play on in step (Cid alone ends its phases on the timer; Ann's peer only watches)
	const int turnAtDrop = w.peers[2].logic->turn();
	for (int t = 0; t < 400 && w.peers[2].logic->turn() < turnAtDrop + 3; ++t)
	{
		w.tick();
	}
	CHECK(w.peers[2].logic->turn() == turnAtDrop + 3); // three more turns, each ended by Cid's timer
	checkInStep(w, { 0, 2 }, 150); // through the drop and the three turns after it (170 frames on the reference run)
	// the compared frames lie on both sides of the surrender's frame: the turns before the drop, and the turns after it (three turn starts after the
	// drop's turn: at least two whole turns of the 25-frame timer), so the floor cannot be met by the frames before the drop alone
	const size_t shared = std::min(w.peers[0].session->hashes().size(), w.peers[2].session->hashes().size());
	CHECK(s0->frame >= 75);                    // tolerance: turns 1 .. 3 ran before the drop, each at least the 25-frame timer
	CHECK(shared >= (size_t)s0->frame + 50); // tolerance: at least two whole timer-ended turns after the surrender's frame, compared exactly in checkInStep
	CHECK(w.peers[0].session->network().isDisconnected(1));
}

TEST_CASE("wotr8 net: a player who leaves from the quit menu on the map surrenders first: its team leaves on every peer; two peers that never leave "
		  "stay in step")
{
	SkirmishGameInfo g = netGame(0); // no timer: every phase ends by END PHASE
	g.slots[1].teamNumber = 3;       // Bob alone
	NetWar w(g);
	for (int t = 0; t < 5; ++t)
	{
		w.tick();
	}
	w.peers[1].session->leave();
	w.peers[1].session->step(); // the leave goes out
	w.peers[1].running = false;
	for (int t = 0; t < 40; ++t)
	{
		w.tick();
	}
	INFO(w.errors());
	for (int i : { 0, 2 })
	{
		CHECK(w.peers[(size_t)i].logic->players()[1].defeated);
		CHECK_FALSE(w.peers[(size_t)i].logic->players()[0].defeated); // another team
	}
	// the turn now waits for Ann and Cid only (a gone player is not waited for)
	REQUIRE(w.peers[0].session->post(w.done(0)));
	REQUIRE(w.peers[2].session->post(w.done(2)));
	for (int t = 0; t < 20; ++t)
	{
		w.tick();
	}
	CHECK(w.peers[0].logic->turn() == 2);
	CHECK(w.peers[2].logic->turn() == 2);
	checkInStep(w, { 0, 2 }, 40);
	// a forged surrender of another player is refused on every peer (the sender is the slot's own player)
	GameMessage forged(MSG_LW_PLAYER_SURRENDERED, 0);
	forged.appendIntegerArgument(2);
	REQUIRE(w.peers[0].session->post(forged));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	CHECK_FALSE(w.peers[2].logic->players()[2].defeated);
	const StrategicSession::Executed *refused = findExecuted(*w.peers[2].session, MSG_LW_PLAYER_SURRENDERED, 0);
	REQUIRE(refused);
	CHECK_FALSE(refused->ok);
	CHECK(refused->error == "MSG_LW_PLAYER_SURRENDERED: a player can surrender only itself");
}

// Sol review wotr2m8s2a: a FRAMERESEND's owner is the sender's claim; a surrender in it is refused while the claimed owner is heard (as Cheat Mode's
// command), so a hostile peer cannot make another peer remove a team that is still playing
TEST_CASE("wotr8 net: a forged FRAMERESEND carrying another player's surrender is refused; the peers stay in step and nobody leaves")
{
	NetWar w(netGame(0));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	const std::uint32_t target = w.peers[2].session->frame() + 6; // a frame of slot 0 peer 2 does not have yet
	NetCommandMsg forged;
	forged.slot = 1;
	forged.type = NETCOMMANDTYPE_FRAMERESEND;
	forged.targetSlot = 0;
	forged.frame = target;
	forged.messages.push_back(std::make_shared<GameMessage>(MSG_LW_PLAYER_SURRENDERED, 0));
	forged.messages.back()->appendIntegerArgument(0);
	std::string error;
	const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(forged, &error, NetPacket::MessageFamily::Strategic);
	REQUIRE_MESSAGE(!bytes.empty(), error);
	w.hub.queue(1, 2, bytes);
	for (int t = 0; t < 20; ++t)
	{
		w.tick();
	}
	CHECK_FALSE(w.peers[2].logic->players()[0].defeated);
	CHECK_FALSE(w.peers[2].logic->players()[1].defeated);
	const std::vector<std::string> &errors = w.peers[2].session->network().errors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0] == "slot 1 resent a War of the Ring surrender in slot 0's frame " + std::to_string(target) + ": refused (a surrender is never relayed)");
	checkInStep(w, { 0, 1, 2 }, 25);
}

namespace
{
// a network war's battle in region `target` (B: Bob's; D: the AI Orc's): Cid's army attacks from C (every player ends the orders phase); the battle's
// index, -1 none
int cidAttacks(LivingWorldLogic &lw, int target)
{
	std::string error;
	int cid = 0;
	for (const Army &a : lw.armies())
	{
		cid = a.owner == 2 && !a.garrison ? a.id : cid;
	}
	REQUIRE(cid != 0); // tolerance: Cid has a hero army (an id, any value)
	REQUIRE_MESSAGE(lw.orderMove(2, cid, target, &error), error);
	for (int p = 0; p < (int)lw.players().size(); ++p)
	{
		if (lw.players()[(size_t)p].human)
		{
			REQUIRE(lw.endTurn(p, &error));
		}
	}
	lw.update();
	REQUIRE(lw.phase() == PHASE_BATTLES);
	for (size_t b = 0; b < lw.battles().size(); ++b)
	{
		if (lw.battles()[b].region == target)
		{
			return (int)b;
		}
	}
	return -1;
}

int cidAttacksBob(LivingWorldLogic &lw)
{
	return cidAttacks(lw, 1);
}

GameMessage voteMessage(int player, int method, int battle)
{
	GameMessage m(MSG_LW_SET_CONFLICT_RESOLUTION_METHOD, player);
	m.appendIntegerArgument(method);
	m.appendIntegerArgument(battle);
	return m;
}
} // namespace

// WOTR2 milestone 8 (step 3): 0x6A7 in a network war (RW 0x6BE9A5 .. 0x6BEC30): every participant votes before anything is decided; real time needs every
// live human of the WAR (RW 0x6B5E3E(false): the whole player list, not the battle's) to have voted auto-resolve or real time, so a battle that leaves a
// human of the war out is auto-resolved, and a battle every human fights goes to real time
TEST_CASE("wotr8 net: the network battle vote: nothing is decided before every participant voted; real time only when every live human of the war is in "
		  "the battle (RW 0x6B5E3E vs the 0x6A7 tally)")
{
	std::string error;
	{
		// three humans: Bob and Cid fight, Ann does not: two real-time votes are not enough
		WarStart w;
		REQUIRE(BuildWarStart(netGame(0), netStores(), kTemplates, w, &error));
		LivingWorldLogic lw(netStores(), netUnits);
		w.setup.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } }; // the autoresolve's power point trackers read them
		REQUIRE(lw.start(w.setup, &error));
		lw.setNetworked(0, 1); // BattleChoice 1: real time first
		const int b = cidAttacksBob(lw);
		REQUIRE(b >= 0); // tolerance: the battle in B exists (its index depends on the AI's battles)
		REQUIRE_MESSAGE(lw.dispatch(voteMessage(2, 3, b), 2, &error), error);
		CHECK(lw.battles()[(size_t)b].resolution == Battle::PENDING); // Bob has not voted
		CHECK(lw.players()[2].battleVote == 3);
		REQUIRE_MESSAGE(lw.dispatch(voteMessage(1, 3, b), 1, &error), error);
		CHECK(lw.battles()[(size_t)b].resolution == Battle::RESOLVED); // auto-resolved: Ann, a live human, is not in the battle
		BattleSetup setup;
		CHECK_FALSE(lw.takeBattleSetup(setup));
		CHECK(lw.players()[1].battleVote == 0); // StartBattle resets every human's vote
		CHECK(lw.players()[2].battleVote == 0);
	}
	{
		// two humans (Ann is an AI): the battle every human fights goes to real time
		SkirmishGameInfo g = netGame(0);
		g.slots[0].state = SLOT_MED_AI;
		WarStart w;
		REQUIRE(BuildWarStart(g, netStores(), kTemplates, w, &error));
		LivingWorldLogic lw(netStores(), netUnits);
		w.setup.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } }; // the autoresolve's power point trackers read them
		REQUIRE(lw.start(w.setup, &error));
		lw.setNetworked(0, 1);
		const int b = cidAttacksBob(lw);
		REQUIRE(b >= 0); // tolerance: the battle in B exists (its index depends on the AI's battles)
		REQUIRE_MESSAGE(lw.dispatch(voteMessage(1, 3, b), 1, &error), error);
		CHECK(lw.battles()[(size_t)b].resolution == Battle::PENDING);
		REQUIRE_MESSAGE(lw.dispatch(voteMessage(2, 3, b), 2, &error), error);
		CHECK(lw.battles()[(size_t)b].resolution == Battle::RTS_REQUESTED);
		BattleSetup setup;
		REQUIRE(lw.takeBattleSetup(setup));
		CHECK(setup.region == 1);
		CHECK(setup.battleIndex == b);
	}
	for (const int starts : { 1, -2 })
	{
		// [S-1342] a battle voted real time with more sides than its map's start positions (1 here) is auto-resolved on every peer with the RTS_DEMOTED
		// notice; a battle map this install does not have (-2) is an error by name (RTS_MAP_MISSING, not the side limit), auto-resolved alike
		CAPTURE(starts);
		SkirmishGameInfo g = netGame(0);
		g.slots[0].state = SLOT_MED_AI;
		WarStart w;
		REQUIRE(BuildWarStart(g, netStores(), kTemplates, w, &error));
		LivingWorldLogic lw(netStores(), netUnits);
		w.setup.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } };
		REQUIRE(lw.start(w.setup, &error));
		lw.setNetworked(0, 1);
		std::vector<std::string> asked;
		lw.setMapStartCount([&](const std::string &map) {
			asked.push_back(map);
			return starts;
		});
		const int b = cidAttacksBob(lw);
		REQUIRE(b >= 0); // tolerance: the battle in B exists (its index depends on the AI's battles)
		REQUIRE(lw.battles()[(size_t)b].sides.size() == 2);
		lw.clearNotices();
		REQUIRE(lw.dispatch(voteMessage(1, 3, b), 1, &error));
		REQUIRE(lw.dispatch(voteMessage(2, 3, b), 2, &error));
		CHECK(lw.battles()[(size_t)b].resolution == Battle::RESOLVED);
		BattleSetup none;
		CHECK_FALSE(lw.takeBattleSetup(none));
		// the battle region's own map, as the region data holds it (the test data's "MAP B" through INI::parseAsciiString)
		const std::string mapB = lw.regionCampaign().regions[(size_t)lw.battles()[(size_t)b].region].mapName;
		CHECK_FALSE(mapB.empty());
		CHECK(asked == std::vector<std::string>{ mapB });
		const int want = starts == -2 ? StrategicNotice::RTS_MAP_MISSING : StrategicNotice::RTS_DEMOTED;
		const auto &notices = lw.notices();
		const bool noticed = std::any_of(notices.begin(), notices.end(), [&](const StrategicNotice &n) { return n.kind == want && n.region == 1; });
		CHECK(noticed);
		const std::string line = starts == -2 ? "error: battle map '" + mapB + "' not found in this install: battle auto-resolved"
		                                      : "[S-1342] the battle in B has 2 sides for 1 start positions on its map: auto-resolved instead of fought in real time";
		const auto &events = lw.events();
		const bool logged = std::any_of(events.begin(), events.end(), [&](const StrategicEvent &e) { return e.text == line; });
		CHECK_MESSAGE(logged, line);
	}
	{
		// Sol review wotr2m8s3: an AI participant abstains (RW 0x9001FB's 4: no flag, no active vote): Cid's real-time vote against the AI Orc is the only
		// active one of two live humans, so the battle is auto-resolved even with BattleChoice 1
		SkirmishGameInfo g = netGame(0);
		g.slots[0].state = SLOT_MED_AI;
		WarStart w;
		REQUIRE(BuildWarStart(g, netStores(), kTemplates, w, &error));
		LivingWorldLogic lw(netStores(), netUnits);
		w.setup.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } };
		REQUIRE(lw.start(w.setup, &error));
		lw.setNetworked(0, 1);
		const int b = cidAttacks(lw, 3);
		REQUIRE(b >= 0); // tolerance: the battle in D exists (its index depends on the AI's battles)
		REQUIRE_MESSAGE(lw.dispatch(voteMessage(2, 3, b), 2, &error), error);
		CHECK(lw.battles()[(size_t)b].resolution == Battle::RESOLVED);
		BattleSetup none;
		CHECK_FALSE(lw.takeBattleSetup(none));
	}
	{
		// one auto-resolve vote and one real-time vote between the two humans: both bits stand (every live human voted auto or real time) and the
		// BattleChoice rule (TheGameInfo + 0x80) picks: 0 auto-resolve first, 1 real time first
		for (const int choice : { 0, 1 })
		{
			CAPTURE(choice);
			SkirmishGameInfo g = netGame(0);
			g.slots[0].state = SLOT_MED_AI;
			WarStart w;
			REQUIRE(BuildWarStart(g, netStores(), kTemplates, w, &error));
			LivingWorldLogic lw(netStores(), netUnits);
			w.setup.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } };
			REQUIRE(lw.start(w.setup, &error));
			lw.setNetworked(0, choice);
			const int b = cidAttacksBob(lw);
			REQUIRE(b >= 0); // tolerance: the battle in B exists (its index depends on the AI's battles)
			REQUIRE(lw.dispatch(voteMessage(1, 1, b), 1, &error));
			REQUIRE(lw.dispatch(voteMessage(2, 3, b), 2, &error));
			CHECK(lw.battles()[(size_t)b].resolution == (choice == 0 ? Battle::RESOLVED : Battle::RTS_REQUESTED));
		}
	}
}

// Sol review wotr2m8s2b2 (b): a client whose own lockstep stalls (its process hangs) is dropped by the others; when it runs again it runs no strategic frame
// (the others went on without it), and its network asks it to leave (Network::quitRequested), which the client checks before the frame count (wotr.gd)
TEST_CASE("wotr8 net: a stalled client is dropped by the others and, once it runs again, its network asks it to leave; it never runs a frame the others did not")
{
	NetWar w(netGame(0));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	// Ann and Bob end the orders phase; Cid's process hangs before it does (no step, nothing heard from it; datagrams to it wait in its inbox)
	REQUIRE(w.peers[0].session->post(w.done(0)));
	REQUIRE(w.peers[1].session->post(w.done(1)));
	w.peers[2].running = false;
	w.hub.mute(2, true);
	for (int t = 0; t < 100 && !(w.peers[0].logic->players()[2].defeated && w.peers[1].logic->players()[2].defeated); ++t)
	{
		w.tick();
	}
	INFO(w.errors());
	REQUIRE(w.peers[0].logic->players()[2].defeated);
	REQUIRE(w.peers[1].logic->players()[2].defeated);
	CHECK(w.peers[0].session->network().isDisconnected(2));
	// Cid's game runs again and ends its phase at once (as the scripted client does after its stall)
	const size_t ranBeforeStall = w.peers[2].session->hashes().size();
	w.hub.mute(2, false);
	w.peers[2].running = true;
	REQUIRE(w.peers[2].session->post(w.done(2)));
	bool asked = false;
	for (int t = 0; t < 50 && !asked; ++t)
	{
		w.tick();
		asked = w.peers[2].session->network().quitRequested();
	}
	for (int t = 0; t < 20; ++t)
	{
		w.tick(); // and runs on a while: it must not run a frame of its own
	}
	CHECK(asked);
	CHECK_FALSE(w.peers[2].session->network().quitReason().empty());
	// the dropped client runs no frame after its stall (not even the drop's: the others run it without this player), so its END PHASE never runs alone;
	// every frame it ran is one the others ran, with the same state (no split game)
	CHECK(w.peers[2].session->hashes().size() == ranBeforeStall);
	checkInStep(w, { 0, 2 }, ranBeforeStall);
	checkInStep(w, { 0, 1 }, ranBeforeStall + 5);
}

TEST_CASE("wotr8 net: two clients that see the battle's victory in different snapshots get the same T_end (the victory event's frame + the end-game "
	"timer); their own timers run out at or after it")
{
	// the logic recorded the victory on frame 100 (the loser's defeat, then the alliance's victory); client A presents frame 103 first, client B (a slower
	// peer) skips to frame 106. Before the victory there is no T_end
	const std::vector<VictoryConditions::Event> before = { VictoryConditions::Event{ 90, VictoryConditions::Event::PLAYER_DEFEATED, 3 } };
	CHECK(EndGameController::victoryTimerEndFrame({}) == -1);
	CHECK(EndGameController::victoryTimerEndFrame(before) == -1);
	std::vector<VictoryConditions::Event> events = before;
	events.push_back(VictoryConditions::Event{ 100, VictoryConditions::Event::PLAYER_DEFEATED, 2 });
	events.push_back(VictoryConditions::Event{ 100, VictoryConditions::Event::ALLIANCE_VICTORY, 1 });
	auto viewAt = [&](unsigned frame) {
		EndGameView v;
		v.frame = frame;
		v.singleAllianceRemaining = true;
		v.endFrame = 100;
		v.localPlayerIndex = 1;
		v.events = events;
		v.logicScripts = true;
		v.scriptEnds = { true }; // the side's VICTORY: the client starts its timer on the frame it presents (RW 0x602FFE)
		return v;
	};
	const EndGameView a = viewAt(103), b = viewAt(106);
	const long long endA = EndGameController::victoryTimerEndFrame(a.events);
	const long long endB = EndGameController::victoryTimerEndFrame(b.events);
	CHECK(endA == 100 + EndGameController::kEndGameTimerFrames);
	CHECK(endA == endB);

	// each client's own timer counts from the frame it noticed the victory in, so the two leave on different frames, neither before T_end: their
	// leaves are never a departure (lan_war_battle_departed: departure frame < T_end)
	auto runOutFrame = [](const EndGameView &v) {
		EndGameController c;
		c.start(1, 3);
		c.update(&v, v.frame, 0.0);
		for (unsigned f = v.frame + 1; f < v.frame + 100; ++f)
		{
			c.update(nullptr, f, 0.0);
			EndGameView later = v;
			later.frame = f;
			c.update(&later, f, 0.0);
			for (const EndGameRequest &r : c.takeRequests())
			{
				if (r.kind == EndGameRequest::CLEAR_GAME_DATA)
				{
					return (long long)f;
				}
			}
		}
		return -1LL;
	};
	const long long outA = runOutFrame(a), outB = runOutFrame(b);
	CHECK(outA == 103 + EndGameController::kEndGameTimerFrames);
	CHECK(outB == 106 + EndGameController::kEndGameTimerFrames);
	CHECK(outA - endA == 3); // A noticed the victory 3 frames after its event frame
	CHECK(outB - endA == 6); // B 6 frames after it
}

namespace
{
GameMessage saveAsk(int player, const std::u16string &name)
{
	GameMessage m(MSG_LW_OPENBFME_SAVE, player);
	m.appendIntegerArgument(LivingWorldLogic::SAVE_ASK);
	m.appendIntegerArgument((int)name.size());
	for (char16_t c : name)
	{
		m.appendIntegerArgument((int)c);
	}
	return m;
}

// an answer to the save request `request` (its serial: Sol wotr2m8s4 follow-up 1)
GameMessage saveAnswer(int player, int kind, std::uint32_t request)
{
	GameMessage m(MSG_LW_OPENBFME_SAVE, player);
	m.appendIntegerArgument(kind);
	m.appendIntegerArgument((int)request);
	return m;
}

// ticks until every live peer's logic satisfies `pred` (at most `limit` ticks); true when it did
bool tickUntil(NetWar &w, const std::function<bool(const LivingWorldLogic &)> &pred, int limit)
{
	for (int t = 0; t < limit; ++t)
	{
		bool all = true;
		for (const Peer &p : w.peers)
		{
			all = all && (!p.running || pred(*p.logic));
		}
		if (all)
		{
			return true;
		}
		w.tick();
	}
	return false;
}
} // namespace

// WOTR2 milestone 8 (step 4): the LAN war's save request (RotWK's NETCOMMANDTYPE_SAVE_GAME, ProcessSaveGameCommand RW 0x629750; the port's strategic order
// MSG_LW_OPENBFME_SAVE, S-1344): the asker's request reaches every peer at one frame and holds the phase timer; the first Yes saves on every peer at one
// frame under the asker's name; the leave-for-now vote (MSG 0x6B9) ends the war only when every live human says yes; a No refuses
TEST_CASE("wotr8 save: Ann asks to save the LAN war: every peer holds the request and the timer; Cid's Yes saves on every peer at one frame; the "
	"unanimous leave vote ends the war; a No refuses")
{
	for (const bool refuse : { false, true })
	{
		CAPTURE(refuse);
		NetWar w(netGame(5));
		for (int t = 0; t < 10; ++t)
		{
			w.tick();
		}
		const int ann = w.peers[0].session->localPlayer(), cid = w.peers[2].session->localPlayer();
		REQUIRE(w.peers[0].session->post(saveAsk(ann, u"War one")));
		REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == ann; }, 50));
		for (const Peer &p : w.peers)
		{
			CHECK(p.logic->saveName() == u"War one");
			CHECK_FALSE(p.logic->phaseTimerExpired()); // GameLogic + 0x11D holds the timer while the request is pending
		}
		// Bob and Cid are asked (the notices of their players), Ann is not
		auto asked = [&](int peer) {
			const LivingWorldLogic &lw = *w.peers[(size_t)peer].logic;
			const int me = w.peers[(size_t)peer].session->localPlayer();
			return std::any_of(lw.notices().begin(), lw.notices().end(), [&](const StrategicNotice &n) { return n.kind == StrategicNotice::SAVE_REQUESTED && n.player == me && n.defeated == ann; });
		};
		CHECK_FALSE(asked(0));
		CHECK(asked(1));
		CHECK(asked(2));
		// the timer runs out meanwhile (5 s): no phase ends while the request is pending
		const int phaseBefore = w.peers[0].logic->phase(), turnBefore = w.peers[0].logic->turn();
		for (int t = 0; t < 40; ++t)
		{
			w.tick();
		}
		CHECK(w.peers[0].logic->phase() == phaseBefore);
		CHECK(w.peers[0].logic->turn() == turnBefore);
		REQUIRE(w.peers[2].session->post(saveAnswer(cid, refuse ? LivingWorldLogic::SAVE_REFUSE : LivingWorldLogic::SAVE_YES, w.peers[2].logic->saveRequestSerial())));
		REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == -1; }, 50));
		if (refuse)
		{
			for (const Peer &p : w.peers)
			{
				CHECK(p.logic->saveOrders().empty());
				CHECK_FALSE(p.logic->leaveVoteOpen());
			}
			const LivingWorldLogic &a = *w.peers[0].logic;
			CHECK(std::any_of(a.notices().begin(), a.notices().end(), [&](const StrategicNotice &n) { return n.kind == StrategicNotice::SAVE_REFUSED && n.player == ann && n.defeated == cid; }));
		}
		else
		{
			const std::vector<NetSaveOrder> &first = w.peers[0].logic->saveOrders();
			REQUIRE(first.size() == 1);
			CHECK(first[0].name == u"War one");
			CHECK(first[0].asker == ann);
			for (const Peer &p : w.peers)
			{
				REQUIRE(p.logic->saveOrders().size() == 1);
				CHECK(p.logic->saveOrders()[0].frame == first[0].frame); // one frame on every peer
				CHECK(p.logic->leaveVoteOpen());
			}
			// every live human votes to leave for now: the war ends on every peer
			for (size_t i = 0; i < w.peers.size(); ++i)
			{
				GameMessage vote(MSG_LW_REQUEST_ENDGAME, w.peers[i].session->localPlayer());
				vote.appendIntegerArgument(1);
				vote.appendIntegerArgument((int)w.peers[i].logic->saveRequestSerial());
				REQUIRE(w.peers[i].session->post(vote));
			}
			REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.warEndAgreed(); }, 50));
		}
		INFO(w.errors());
		checkInStep(w, { 0, 1, 2 }, 30);
		// the timer works again: the phase ends by itself
		if (refuse)
		{
			CHECK(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.phase() != phaseBefore || lw.turn() != turnBefore; }, 60));
		}
	}
}

// a player who leaves takes its part of a pending save with it, at one frame on every peer, in LAN (drop: the surrender at once) and relay mode (the
// dropped player is held for a rejoin, its surrender comes when the window ends): Cid (the asker) crashes; Ann's team (the answerers) crashes
TEST_CASE("wotr8 save: the asker crashes while the Yes / No box is open, or every answerer does: the request ends at one frame on every peer and the "
	"phase timer ends the phase again (LAN and relay mode)")
{
	for (const bool online : { false, true })
	{
		for (const bool askerDrops : { true, false })
		{
			CAPTURE(online);
			CAPTURE(askerDrops);
			NetWar w(netGame(5), 3, online);
			for (int t = 0; t < 10; ++t)
			{
				w.tick();
			}
			const int cid = w.peers[2].session->localPlayer();
			REQUIRE(w.peers[2].session->post(saveAsk(cid, u"Mid war")));
			REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == cid; }, 50));
			// askerDrops: Cid's process dies; else Ann's (her team, Ann and Bob, leaves: nobody is left to answer Cid)
			const int dead = askerDrops ? 2 : 0;
			w.kill(dead);
			const std::vector<int> live = askerDrops ? std::vector<int>{ 0, 1 } : std::vector<int>{ 1, 2 };
			REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == -1; }, 2000));
			INFO(w.errors());
			// the frame the request ended at: the same on the live peers (their hashes, which hold the request, agree frame by frame)
			checkInStep(w, live, 10); // the frames since the war began (the request ends some 20 frames after it; every shared one must agree)
			for (int i : live)
			{
				CHECK(w.peers[(size_t)i].logic->saveName().empty());
				CHECK(w.peers[(size_t)i].logic->saveOrders().empty());
			}
			// the phase timer is not left blocked: the phase ends by itself
			const LivingWorldLogic &lw = *w.peers[(size_t)live[0]].logic;
			const int phase = lw.phase(), turn = lw.turn();
			CHECK(tickUntil(w, [&](const LivingWorldLogic &l) { return l.finished() || l.phase() != phase || l.turn() != turn; }, 200));
		}
	}
}

// the request and the vote are part of a network war's save (strategic block version 6) and come back with its clock; a version-6 load hashes alike
TEST_CASE("wotr8 save: the pending request, the leave vote and the network clock survive the strategic block's save and load")
{
	NetWar w(netGame(5));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	const int ann = w.peers[0].session->localPlayer();
	REQUIRE(w.peers[0].session->post(saveAsk(ann, u"Kept")));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == ann; }, 50));
	const LivingWorldLogic &lw = *w.peers[1].logic;
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(lw.saveState(bytes, &error), error);
	LivingWorldLogic loaded(netStores(), netUnits);
	REQUIRE_MESSAGE(loaded.loadState(w.war.setup, bytes, &error), error);
	loaded.setNetworked(w.war.phaseTimerSeconds);
	CHECK(loaded.saveAsker() == ann);
	CHECK(loaded.saveName() == u"Kept");
	CHECK(loaded.frame() == lw.frame());
	CHECK(loaded.stateHash() == lw.stateHash());
}

TEST_CASE("wotr8 save: a save name is a plain file name: a peer's request with a path or a reserved character is refused on every peer, and a loaded "
	"block with one fails")
{
	// LivingWorldLogic::saveNameValid: every peer writes its copy under the request's name (wotr.gd's _write_lan_save joins it to the Save folder)
	for (const std::u16string &ok : { std::u16string(u"War one"), std::u16string(u".x"), std::u16string(u"Gondor's war 2"), std::u16string(64, u'a') })
	{
		CAPTURE(std::string(ok.begin(), ok.end()));
		CHECK(LivingWorldLogic::saveNameValid(ok, nullptr));
	}
	for (const std::u16string &bad : { std::u16string(), std::u16string(65, u'a'), std::u16string(u"../x"), std::u16string(u"a/b"), std::u16string(u"a\\b"),
		     std::u16string(u"C:x"), std::u16string(u"."), std::u16string(u".."), std::u16string(u"a*"), std::u16string(u"a?"), std::u16string(u"a\"b"),
		     std::u16string(u"<a>"), std::u16string(u"a|b"), std::u16string(u"50%"), std::u16string(u"a\tb"), std::u16string(u" a"), std::u16string(u"a "),
		     std::u16string(u"a.") })
	{
		CAPTURE(std::string(bad.begin(), bad.end()));
		std::string why;
		CHECK_FALSE(LivingWorldLogic::saveNameValid(bad, &why));
		CHECK_FALSE(why.empty());
	}
	NetWar w(netGame(5));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	const int ann = w.peers[0].session->localPlayer();
	REQUIRE(w.peers[0].session->post(saveAsk(ann, u"../../escape")));
	for (int t = 0; t < 30; ++t)
	{
		w.tick();
	}
	for (const Peer &p : w.peers)
	{
		CHECK(p.logic->saveAsker() == -1);
		CHECK(p.logic->saveOrders().empty());
	}
	// the strategic block with a pending request whose name is then made a path: the load refuses it
	REQUIRE(w.peers[0].session->post(saveAsk(ann, u"Kept")));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == ann; }, 50));
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(w.peers[1].logic->saveState(bytes, &error), error);
	const std::uint8_t kept[] = { 'K', 0, 'e', 0, 'p', 0, 't', 0 };
	auto at = std::search(bytes.begin(), bytes.end(), std::begin(kept), std::end(kept));
	REQUIRE(at != bytes.end());
	at[2] = '/'; // "K/pt"
	LivingWorldLogic loaded(netStores(), netUnits);
	CHECK_FALSE(loaded.loadState(w.war.setup, bytes, &error));
	CHECK(error.find("not allowed in a file name") != std::string::npos);
}

// Sol review wotr2m8s4 #3: a save made while some humans of a battle have voted holds their votes (hashed in a network war): it loads with the same hash
TEST_CASE("wotr8 save: a partly answered battle vote survives the strategic block's save and load, with the network war's state hash")
{
	std::string error;
	WarStart w;
	REQUIRE(BuildWarStart(netGame(0), netStores(), kTemplates, w, &error));
	LivingWorldLogic lw(netStores(), netUnits);
	w.setup.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } };
	REQUIRE(lw.start(w.setup, &error));
	lw.setNetworked(0, 1);
	const int b = cidAttacksBob(lw);
	REQUIRE(b >= 0); // tolerance: the battle in B exists (its index depends on the AI's battles)
	REQUIRE_MESSAGE(lw.dispatch(voteMessage(2, 3, b), 2, &error), error);
	REQUIRE(lw.battles()[(size_t)b].resolution == Battle::PENDING); // Bob has not voted
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(lw.saveState(bytes, &error), error);
	LivingWorldLogic loaded(netStores(), netUnits);
	REQUIRE_MESSAGE(loaded.loadState(w.setup, bytes, &error), error);
	loaded.setNetworked(0, 1);
	CHECK(loaded.players()[2].battleVote == 3);
	CHECK(loaded.players()[2].battleVoteFor == b);
	CHECK(loaded.players()[1].battleVote == 0);
	CHECK(loaded.stateHash() == lw.stateHash());
}

// Sol review wotr2m8s4 #2: every LAN save is written on the Yes's frame, which opens the leave-for-now vote; the resume closes it on every peer (the
// vote's notices are not saved), so the resumed war can be saved again
TEST_CASE("wotr8 save: a resumed LAN war's saved leave vote is over: resumeAfterLanLoad closes it after the hash check, and a new save request is taken")
{
	NetWar w(netGame(5));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	const int ann = w.peers[0].session->localPlayer(), cid = w.peers[2].session->localPlayer();
	REQUIRE(w.peers[0].session->post(saveAsk(ann, u"Again")));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == ann; }, 50));
	REQUIRE(w.peers[2].session->post(saveAnswer(cid, LivingWorldLogic::SAVE_YES, w.peers[2].logic->saveRequestSerial())));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return !lw.saveOrders().empty(); }, 50));
	const LivingWorldLogic &saved = *w.peers[1].logic;
	REQUIRE(saved.leaveVoteOpen()); // the frame the peers write their copies
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(saved.saveState(bytes, &error), error);
	LivingWorldLogic loaded(netStores(), netUnits);
	REQUIRE_MESSAGE(loaded.loadState(w.war.setup, bytes, &error), error);
	loaded.setNetworked(w.war.phaseTimerSeconds);
	CHECK(loaded.stateHash() == saved.stateHash()); // the joiners' check compares this hash
	CHECK(loaded.leaveVoteOpen());
	loaded.resumeAfterLanLoad();
	CHECK_FALSE(loaded.leaveVoteOpen());
	CHECK_FALSE(loaded.warEndAgreed());
	CHECK(loaded.saveAsker() == -1);
	// a new request (Ann asks again on the resumed war): taken, not refused as "the leave-for-now vote of the last save is open"
	REQUIRE_MESSAGE(loaded.dispatch(saveAsk(ann, u"Again 2"), ann, &error), error);
	CHECK(loaded.saveAsker() == ann);
}

// Sol review wotr2m8s4 #6: a save answer in a forged FRAMERESEND (a hostile asker's Yes in another player's frame) is refused while the claimed owner is
// heard, as the surrender is
TEST_CASE("wotr8 net: a forged FRAMERESEND carrying another player's save answer or leave vote is refused; the peers stay in step")
{
	for (const int type : { (int)MSG_LW_OPENBFME_SAVE, (int)MSG_LW_REQUEST_ENDGAME })
	{
		CAPTURE(type);
		NetWar w(netGame(0));
		for (int t = 0; t < 10; ++t)
		{
			w.tick();
		}
		const std::uint32_t target = w.peers[2].session->frame() + 6; // a frame of slot 0 peer 2 does not have yet
		NetCommandMsg forged;
		forged.slot = 1;
		forged.type = NETCOMMANDTYPE_FRAMERESEND;
		forged.targetSlot = 0;
		forged.frame = target;
		forged.messages.push_back(std::make_shared<GameMessage>(type, 0));
		forged.messages.back()->appendIntegerArgument(type == MSG_LW_OPENBFME_SAVE ? (int)LivingWorldLogic::SAVE_YES : 1);
		std::string error;
		const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(forged, &error, NetPacket::MessageFamily::Strategic);
		REQUIRE_MESSAGE(!bytes.empty(), error);
		w.hub.queue(1, 2, bytes);
		for (int t = 0; t < 20; ++t)
		{
			w.tick();
		}
		const std::vector<std::string> &errors = w.peers[2].session->network().errors();
		REQUIRE(errors.size() == 1);
		CHECK(errors[0] == "slot 1 resent " + std::string(type == MSG_LW_OPENBFME_SAVE ? "a War of the Ring save request or answer" : "a War of the Ring leave-for-now vote") +
			" in slot 0's frame " + std::to_string(target) + ": refused (" + (type == MSG_LW_OPENBFME_SAVE ? "a save request or answer" : "a leave-for-now vote") +
			" is never relayed)");
		checkInStep(w, { 0, 1, 2 }, 25);
	}
}

// coordinator 10-30 (the stricter owner / phase checks): an order a peer sends for another player is refused the same way on every peer inside the frame,
// and the refusal is counted in the network war's state hash (and its save)
TEST_CASE("wotr8 net: an order for another player is refused on every peer at the same frame; every peer counts the refusal and the hashes stay equal")
{
	NetWar w(netGame(0));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	const LivingWorldLogic &first = *w.peers[0].logic;
	const int bob = w.peers[1].session->localPlayer(), ann = w.peers[0].session->localPlayer();
	REQUIRE(first.refusedOrders() == 0);
	const std::uint32_t hashBefore = first.stateHash();
	// Bob's peer ends Ann's orders phase (DONE_PHASE names its player: RW 0x6BEC57; retail would take it)
	GameMessage forged(MSG_LW_DONE_PHASE, bob);
	forged.appendIntegerArgument(first.turn());
	forged.appendIntegerArgument(first.phase());
	forged.appendIntegerArgument(ann);
	REQUIRE(w.peers[1].session->post(forged));
	REQUIRE(tickUntil(w, [](const LivingWorldLogic &lw) { return lw.refusedOrders() == 1; }, 50));
	for (const Peer &p : w.peers)
	{
		CHECK(p.logic->refusedOrders() == 1);
		CHECK_FALSE(p.logic->players()[(size_t)ann].endedTurn);
		const StrategicSession::Executed *e = findExecuted(*p.session, MSG_LW_DONE_PHASE, bob);
		REQUIRE(e);
		CHECK_FALSE(e->ok);
		CHECK(e->error == "MSG_LW_DONE_PHASE: a player can end only its own phase");
	}
	CHECK(first.stateHash() != hashBefore); // the count is in the hash (the clock moved too)
	for (int t = 0; t < 20; ++t)
	{
		w.tick();
	}
	checkInStep(w, { 0, 1, 2 }, 25);
	// the count survives the block's save and load with the network war's hash
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(first.saveState(bytes, &error), error);
	LivingWorldLogic loaded(netStores(), netUnits);
	REQUIRE_MESSAGE(loaded.loadState(w.war.setup, bytes, &error), error);
	loaded.setNetworked(w.war.phaseTimerSeconds);
	CHECK(loaded.refusedOrders() == first.refusedOrders());
	CHECK(loaded.stateHash() == first.stateHash());
}

// Sol wotr2m8s4 follow-up 1 (coordinator 10-30): a save answer names its request; a late answer for request A is refused by request B on every peer at the
// same frame (counted), and B is answered only by its own answers
TEST_CASE("wotr8 save: a late Yes for an ended save request is refused by the next request on every peer; the next request takes only its own answers")
{
	NetWar w(netGame(5));
	for (int t = 0; t < 10; ++t)
	{
		w.tick();
	}
	const int ann = w.peers[0].session->localPlayer(), bob = w.peers[1].session->localPlayer(), cid = w.peers[2].session->localPlayer();
	// request A: Cid refuses it
	REQUIRE(w.peers[0].session->post(saveAsk(ann, u"War A")));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == ann; }, 50));
	const std::uint32_t a = w.peers[1].logic->saveRequestSerial();
	REQUIRE(w.peers[2].session->post(saveAnswer(cid, LivingWorldLogic::SAVE_REFUSE, a)));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == -1; }, 50));
	// request B
	REQUIRE(w.peers[0].session->post(saveAsk(ann, u"War B")));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.saveAsker() == ann; }, 50));
	const std::uint32_t b = w.peers[1].logic->saveRequestSerial();
	REQUIRE(b == a + 1);
	const std::uint32_t refusedBefore = w.peers[0].logic->refusedOrders();
	// Bob's Yes for A arrives late: refused on every peer, B still pending, nothing saved
	REQUIRE(w.peers[1].session->post(saveAnswer(bob, LivingWorldLogic::SAVE_YES, a)));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return lw.refusedOrders() == refusedBefore + 1; }, 50));
	for (const Peer &p : w.peers)
	{
		CHECK(p.logic->saveAsker() == ann);
		CHECK(p.logic->saveName() == u"War B");
		CHECK(p.logic->saveOrders().empty());
		const StrategicSession::Executed *e = findExecuted(*p.session, MSG_LW_OPENBFME_SAVE, bob);
		REQUIRE(e);
		CHECK_FALSE(e->ok);
		CHECK(e->error == "MSG_LW_OPENBFME_SAVE: the answer is for request " + std::to_string(a) + ", the pending one is " + std::to_string(b));
	}
	// Bob's Yes for B saves B on every peer
	REQUIRE(w.peers[1].session->post(saveAnswer(bob, LivingWorldLogic::SAVE_YES, b)));
	REQUIRE(tickUntil(w, [&](const LivingWorldLogic &lw) { return !lw.saveOrders().empty(); }, 50));
	for (const Peer &p : w.peers)
	{
		REQUIRE(p.logic->saveOrders().size() == 1);
		CHECK(p.logic->saveOrders()[0].name == u"War B");
	}
	for (int t = 0; t < 20; ++t)
	{
		w.tick();
	}
	checkInStep(w, { 0, 1, 2 }, 25);
}

// coordinator 10-30: Saturday's nightly writes the strategic block version 6. Two version-6 blocks written by that code (lane/wotr2 66ef6b753, the
// synthetic war of netGame(5): tests/data/wotr/lanwar_v6_*.bin, with the networked state hash each had): "saved" on the Yes's frame (what a LAN
// war's save holds: the leave vote open, no request pending), "pending" while a request was pending
TEST_CASE("wotr8 save: a version-6 strategic block (Saturday's nightly) loads: serial 0, a pending request dropped, the leave vote kept; the resume's hash "
	"check passes and the resumed war takes a new request")
{
	auto read = [](const char *name) {
		std::ifstream f(std::string(OPENBFME_TEST_DATA_DIR) + "/wotr/" + name, std::ios::binary);
		REQUIRE(f.good());
		return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	NetWar w(netGame(5)); // the war setup the fixtures were written with
	std::string error;
	{
		const std::vector<std::uint8_t> bytes = read("lanwar_v6_saved.bin");
		REQUIRE(bytes.size() == 1674u);
		LivingWorldLogic loaded(netStores(), netUnits);
		REQUIRE_MESSAGE(loaded.loadState(w.war.setup, bytes, &error), error);
		loaded.setNetworked(w.war.phaseTimerSeconds);
		CHECK(loaded.saveRequestSerial() == 0u);
		CHECK(loaded.saveAsker() == -1);
		CHECK(loaded.leaveVoteOpen()); // kept as saved: the resume compares the hash first
		CHECK(loaded.stateHash() == 4248229272u); // the resume's hash check (the hash the version-6 save holds)
		loaded.resumeAfterLanLoad();
		CHECK_FALSE(loaded.leaveVoteOpen());
		const int ann = 0;
		REQUIRE(loaded.players()[(size_t)ann].human);
		REQUIRE_MESSAGE(loaded.dispatch(saveAsk(ann, u"After v6"), ann, &error), error);
		CHECK(loaded.saveAsker() == ann);
		CHECK(loaded.saveRequestSerial() == 1u);
	}
	{
		const std::vector<std::uint8_t> bytes = read("lanwar_v6_pending.bin");
		REQUIRE(bytes.size() == 1635u);
		LivingWorldLogic loaded(netStores(), netUnits);
		REQUIRE_MESSAGE(loaded.loadState(w.war.setup, bytes, &error), error);
		loaded.setNetworked(w.war.phaseTimerSeconds);
		CHECK(loaded.saveRequestSerial() == 0u);
		CHECK(loaded.saveAsker() == -1); // dropped: a version-6 request has no serial to answer
		CHECK(loaded.saveName().empty());
		CHECK_FALSE(loaded.leaveVoteOpen());
		CHECK(loaded.stateHash() != 4007082944u); // tolerance: only "not the hash with the request" is the claim (the dropped request is out of the state; a LAN war's own save never holds one)
	}
}
