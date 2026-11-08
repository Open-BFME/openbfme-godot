// OpenBFME unit tests. GPL-3.0.
// Lane AI-1, step 1: TheSkirmishAIManager (GameLogic/SkirmishAI/SkirmishAIManager.h): which players get an AI, the allied groups, the disabled rule,
// the update cadence and the hash. The expectations follow the RW functions named in each test (RW 0x6AA030, 0x6A9EAD, 0x8EDBB0, 0x6A96A0, 0x8EDDF6,
// 0x6C7946), never this engine's own output. The retail test starts real skirmishes (START-1's LiveGame) with every faction in a computer slot.

#include "doctest.h"

#include "LogicTestUtil.h"
#include "MoveTestUtil.h"
#include "PathfindTestUtil.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Economy.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <cmath>
#include <string>
#include <vector>

using namespace logictest;

namespace
{
const char kAIData[] = "SkirmishAIData TheSkirmishAIData\n DisableWallBuilding = No\nEnd\n"
					   "ArmyDefinition AlphaArmy\n Side = Alpha\n ArmyMemberDefinition M\n  Unit = Soldier\n  PercentageOfArmyPhase1 = 100\n End\nEnd\n"
					   "AIDozerAssignment AlphaDozer\n Side = Alpha\n Unit = AlphaPorter\nEnd\n";

struct AIFixture : LogicWorld
{
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;

	explicit AIFixture(const std::vector<SkirmishPlayer> &slots, const char *data = kAIData)
	{
		store.registerBlocks(env.blocks);
		if (data)
		{
			INI ini(env);
			const std::string text = data;
			ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		}
		SkirmishSetup setup;
		setup.players = slots;
		setup.defaultStartingCash = 2000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic->skirmishAI().setStore(&store);
	}
	int indexOf(const std::string &name) { return players.findPlayerWithName(name)->getPlayerIndex(); }
};
} // namespace

TEST_CASE("skirmish ai manager: newGame gives each computer player of a playable side an AI; allies share a group (RW 0x6AA030, 0x6A9EAD)")
{
	// slots: name, faction, human, team
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 }, { "C2", "FactionB", false, 1, 0, 2 },
		{ "C3", "FactionA", false, 2, 0, 3 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	CHECK(m.newGame(true) == 3);
	REQUIRE(m.groups().size() == 2);
	// C1 and C2 are allies (team 1): one group, C1 first; C3 alone
	CHECK((m.groups()[0]->members == std::vector<int>{ fx.indexOf("C1"), fx.indexOf("C2") }));
	CHECK((m.groups()[1]->members == std::vector<int>{ fx.indexOf("C3") }));
	const AISkirmishPlayer *c1 = m.findAI(fx.indexOf("C1"));
	const AISkirmishPlayer *c2 = m.findAI(fx.indexOf("C2"));
	const AISkirmishPlayer *c3 = m.findAI(fx.indexOf("C3"));
	REQUIRE(c1);
	REQUIRE(c2);
	REQUIRE(c3);
	CHECK(m.findAI(fx.indexOf("H")) == nullptr);
	CHECK(c1->firstOfGroup);
	CHECK(!c2->firstOfGroup);
	CHECK(c3->firstOfGroup);
	// Alpha has an army and a dozer; Beta has no army: its AI is disabled (RW 0x6A9F96) and gets no dozer
	CHECK(c1->army != nullptr);
	CHECK(c1->dozerTemplate == "AlphaPorter");
	CHECK(!c1->disabled);
	CHECK(c2->army == nullptr);
	CHECK(c2->disabled);
	CHECK(c2->dozerTemplate.empty());
	CHECK(c1->field178 == -1);
	CHECK(c1->retryTimer == 150u);
	// every playable player has a player info record: the human first (index order), the AIs as they were added
	CHECK(m.infoPlayers().size() == 4);
	const std::vector<std::string> rep = m.report();
	bool disabledReported = false;
	for (const std::string &line : rep)
	{
		disabledReported = disabledReported || line.find("has no ArmyDefinition") != std::string::npos;
	}
	CHECK(disabledReported);
}

TEST_CASE("skirmish ai manager: no AI outside a skirmish, in a loaded save, or without SkirmishAIData (reported)")
{
	{
		AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
		CHECK(fx.logic->skirmishAI().newGame(false) == 0);
		CHECK(fx.logic->skirmishAI().newGame(true, true) == 0);
		CHECK(!fx.logic->skirmishAI().started());
	}
	{
		AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } }, nullptr);
		CHECK(fx.logic->skirmishAI().newGame(true) == 0);
		const std::vector<std::string> rep = fx.logic->skirmishAI().report();
		REQUIRE(!rep.empty());
		CHECK(rep[0].find("[S-410]") != std::string::npos);
	}
}

TEST_CASE("skirmish ai manager: MakeAllSkirmishSidesAIControlled gives the human slots an AI too (RW 0x6AA0AE)")
{
	const std::string data = std::string(kAIData) + "SkirmishAIData X\n MakeAllSkirmishSidesAIControlled = Yes\nEnd\n";
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } }, data.c_str());
	CHECK(fx.logic->skirmishAI().newGame(true) == 2);
	CHECK(fx.logic->skirmishAI().findAI(fx.indexOf("H")) != nullptr);
}

TEST_CASE("skirmish ai manager: nothing runs before logic frame 10; then groups and AIs update once per frame in phase 5 (RW 0x6A96A0)")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 }, { "C2", "FactionB", false, 1, 0, 2 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 2);
	while (fx.logic->getFrame() < 9)
	{
		fx.logic->runLogicFrame();
	}
	CHECK(m.counters().managerUpdates == 0);
	CHECK(m.counters().gatedUpdates == 9);
	fx.logic->runLogicFrame(); // frame 10
	CHECK(fx.logic->getFrame() == 10u);
	CHECK(m.counters().managerUpdates == 1);
	CHECK(m.counters().groupUpdates == 1);
	CHECK(m.counters().aiUpdates == 2);
	// C1 enabled: its AIPlayer part and brain; C2 disabled, but the group has 2 live AIs, so its brain still runs (RW 0x6C79B9)
	CHECK(m.counters().aiPlayerUpdates == 1);
	CHECK(m.counters().brainUpdates == 2);
	CHECK(m.groups()[0]->lastLiveCount == 2);
	CHECK(m.counters().playerInfoUpdates == 3);
	fx.logic->runLogicFrame();
	CHECK(m.counters().managerUpdates == 2);
	CHECK(m.counters().aiUpdates == 4);
}

TEST_CASE("skirmish ai manager: a defeated AI player is shut down and disabled; a group without a live AI keeps running its disabled members by the rule")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 }, { "C2", "FactionB", false, 1, 0, 2 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 2);
	while (fx.logic->getFrame() < 10)
	{
		fx.logic->runLogicFrame();
	}
	fx.players.findPlayerWithName("C1")->setDefeated(true);
	fx.logic->runLogicFrame();
	const AISkirmishPlayer *c1 = m.findAI(fx.indexOf("C1"));
	CHECK(c1->disabled);
	CHECK(c1->shutdownFrame == 11u);
	CHECK(m.counters().shutdowns == 1);
	// C2 (disabled, no army): the group has one live AI (C2 itself), and C2 has no army -> no brain update (RW 0x6C79C0)
	const unsigned long long brains = m.counters().brainUpdates;
	// lane AI-3 (QA-1 U22): the defeated C1's brain really runs (RW 0x6C79C9 -> RW 0x90CD00): an ended tactic leaves its list (RW 0x90BA94)
	AITactic ended;
	ended.kind = "SimpleAttack";
	ended.ended = true;
	const_cast<AISkirmishPlayer *>(c1)->brain.tactics.push_back(ended);
	fx.logic->runLogicFrame();
	CHECK(m.counters().brainUpdates == brains + 1); // C1: disabled with an army, 1 live AI -> brain
	CHECK(m.counters().disabledSkips >= 1);
	bool endedLeft = false;
	for (const AITactic &t : c1->brain.tactics)
	{
		endedLeft = endedLeft || t.ended;
	}
	CHECK_FALSE(endedLeft);
}

TEST_CASE("skirmish ai manager: the game phase follows PhaseDuration_Rush / _MidGame in logic seconds (RW 0x6C7677)")
{
	const std::string data = std::string("SkirmishAIData T\nEnd\n") +
		"ArmyDefinition A\n Side = Alpha\n PhaseDuration_Rush = 2.0\n PhaseDuration_MidGame = 2.0\nEnd\nAIDozerAssignment D\n Side = Alpha\n Unit = AlphaPorter\nEnd\n";
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } }, data.c_str());
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 1);
	const AISkirmishPlayer *ai = m.findAI(fx.indexOf("C"));
	REQUIRE(ai);
	CHECK(ai->creationFrame == 0u);
	std::vector<int> phase;
	std::vector<float> fraction;
	while (fx.logic->getFrame() < 22)
	{
		fx.logic->runLogicFrame();
		phase.push_back(ai->build.phase);
		fraction.push_back(ai->build.phaseFraction);
	}
	// frames 1 .. 9: no AI update (gate); frame 10: 10 * 0.2 = 2.0 s >= Rush -> MidGame, fraction 0; frame 11: 2.2 / (2 + 2) = 0.55; frame 20: 4.0 -> EndGame; 21: 1.0
	CHECK(phase[8] == 0);
	CHECK(phase[9] == 1);
	CHECK(fraction[9] == 0.0f);
	CHECK(fraction[10] == doctest::Approx(0.55f));
	CHECK(phase[18] == 1);
	CHECK(phase[19] == 2);
	CHECK(fraction[19] == 0.0f);
	CHECK(fraction[20] == 1.0f);
}

TEST_CASE("skirmish ai manager: the AI state is in the logic hash (each field changes it) and two identical runs hash alike every frame")
{
	auto run = [](std::vector<std::uint32_t> &hashes) {
		AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 }, { "C2", "FactionB", false, 2, 0, 2 } });
		fx.logic->skirmishAI().newGame(true);
		for (int i = 0; i < 20; ++i)
		{
			fx.logic->runLogicFrame();
			hashes.push_back(fx.logic->computeStateHash());
		}
	};
	std::vector<std::uint32_t> a, b;
	run(a);
	run(b);
	CHECK(a == b);

	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 } });
	const std::uint32_t before = fx.logic->computeStateHash();
	fx.logic->skirmishAI().newGame(true);
	const std::uint32_t started = fx.logic->computeStateHash();
	CHECK(started != before);
	// mutate each hashed field of the AI through the const_cast a test may do, and see the hash move
	AISkirmishPlayer *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C1")));
	REQUIRE(ai);
	auto moved = [&](auto mutate) {
		const std::uint32_t h0 = fx.logic->computeStateHash();
		mutate();
		return fx.logic->computeStateHash() != h0;
	};
	CHECK(moved([&] { ai->disabled = !ai->disabled; }));
	CHECK(moved([&] { ai->field170 = 1.5f; }));
	CHECK(moved([&] { ai->field178 = 3; }));
	CHECK(moved([&] { ai->creationFrame += 1; }));
	CHECK(moved([&] { ai->dozerTemplate += "x"; }));
	CHECK(moved([&] { ai->shutdownFrame = 7; }));
	CHECK(moved([&] { ai->retryTimer = 1; }));
	CHECK(moved([&] { ai->firstOfGroup = !ai->firstOfGroup; }));
	AISkirmishGroup *g = const_cast<AISkirmishGroup *>(fx.logic->skirmishAI().findGroup(fx.indexOf("C1")));
	CHECK(moved([&] { g->active = !g->active; }));
	CHECK(moved([&] { g->lastLiveCount = 9; }));
	// the AI data the decisions read is folded into the manager's hash (DisableBaseBuilding, PhaseDuration_Rush)
	CHECK(moved([&] { fx.store.data().disableBaseBuilding = !fx.store.data().disableBaseBuilding; }));
	CHECK(moved([&] { const_cast<ArmyDefinition *>(fx.store.findArmy("Alpha"))->phaseDurationRush = 1.0f; }));
	// the economy builder's counters and a request's order are hashed (step 5); the manager's farm site pool is hashed too
	CHECK(moved([&] { ai->build.economy.built += 1; }));
	CHECK(moved([&] { ai->build.economy.inFlight += 1; }));
	CHECK(moved([&] { ai->build.economy.lastFrame = 77; }));
	CHECK(moved([&] { ai->build.economy.made += 1; }));
	CHECK(moved([&] { ai->build.economy.refusedByDifficulty += 1; }));
	CHECK(moved([&] { ai->build.economy.noSite += 1; }));
	CHECK(moved([&] { ai->build.economy.disabled = !ai->build.economy.disabled; }));
	auto request = [&](std::uint32_t serial) {
		auto r = std::make_shared<AIBuildRequest>();
		r->serial = serial;
		r->templateName = "Same";
		return r;
	};
	auto reqA = request(1), reqB = request(2);
	CHECK(moved([&] { ai->build.pending.push_back(reqA); ai->build.pending.push_back(reqB); }));
	CHECK(moved([&] { std::swap(ai->build.pending[0], ai->build.pending[1]); })); // queue reorder, same contents
	CHECK(moved([&] { ai->build.inProgress.push_back(reqA); }));
	CHECK(moved([&] { ai->build.economy.requests.push_back(reqA); }));
	// the farm site pool: filled, mutated in place, reordered, and cleared by reset
	std::vector<std::shared_ptr<AIBuildRequest>> &pool = const_cast<std::vector<std::shared_ptr<AIBuildRequest>> &>(fx.logic->skirmishAI().farmSites());
	auto site = [&](std::uint32_t serial, float x) {
		auto r = request(serial);
		r->farm = true;
		r->offset = Coord3D{ x, 0.0f, 0.0f };
		r->siteIndex = (int)serial;
		return r;
	};
	CHECK(moved([&] { pool.push_back(site(0x80000000u, 10.0f)); pool.push_back(site(0x80000001u, 20.0f)); }));
	CHECK(moved([&] { pool[0]->inUse = true; }));
	CHECK(moved([&] { pool[1]->failures = 1; }));
	CHECK(moved([&] { pool[0]->owner = 3; }));
	CHECK(moved([&] { pool[1]->offset.x = 21.0f; }));
	CHECK(moved([&] { std::swap(pool[0], pool[1]); })); // the pool's order
	CHECK(moved([&] { pool.pop_back(); }));
	const std::uint32_t withPool = fx.logic->computeStateHash();
	pool.push_back(site(0x80000009u, 5.0f));
	CHECK(fx.logic->computeStateHash() != withPool);
	// reset empties it again
	fx.logic->skirmishAI().reset();
	CHECK(fx.logic->skirmishAI().farmSites().empty());
	CHECK(fx.logic->skirmishAI().groups().empty());
}

TEST_CASE("skirmish ai manager: a retail skirmish gives each of the 7 factions in a computer slot a running AI with its army and dozer")
{
	OPENBFME_REQUIRE_START(s);
	const SkirmishAIStore &store = s->world->skirmishAI();
	// the playable factions of PlayerTemplate.ini and the Side each one names (the AI looks the army up by Side)
	std::vector<std::string> unknown;
	const char *factions[] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
	for (const char *faction : factions)
	{
		INFO(std::string(faction));
		const PlayerTemplate *pt = s->world->playerTemplates().findPlayerTemplate(faction);
		REQUIRE(pt != nullptr);
		CHECK(pt->m_playableSide);
		const ArmyDefinition *army = store.findArmy(pt->m_side);
		REQUIRE(army != nullptr);
		CHECK(!army->members.empty());
		REQUIRE(store.findDozer(pt->m_side) != nullptr);
		CHECK(s->world->things().findTemplate(*store.findDozer(pt->m_side)) != nullptr);
		for (const ArmyMemberDefinition &m : army->members)
		{
			if (s->world->things().findTemplate(m.unit) == nullptr)
			{
				unknown.push_back(std::string(faction) + ":" + m.unit);
			}
		}
	}
	// retail data error, kept as data: the Elves army's ElvenEnt_Member2 names RohanFirBirch, which no Object block defines (the parse takes the string;
	// what the unit builder does with it is step 3's)
	CHECK((unknown == std::vector<std::string>{ "FactionElves:RohanFirBirch" }));
}

// ---- a real skirmish (START-1's LiveGame on Evendim) ----------------------------------------------------------------------------------------------------
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

namespace
{
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// slot 0 the human (FactionMen), slot 1 the computer at `state` playing `aiFaction`; start positions, colours and teams fixed (no random draws)
NewGameMessage aiMessage(const starttest::Shared &s, const std::string &aiFaction, SlotState state)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 1234;
	m.game.startingCash = 1500;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = SLOT_PLAYER;
	h.name = u"Human";
	h.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMen");
	h.startPos = 0;
	h.color = 0;
	h.teamNumber = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = state;
	c.name = u"Computer";
	c.playerTemplate = templateIndex(s.world->playerTemplates(), aiFaction);
	c.startPos = 1;
	c.color = 1;
	c.teamNumber = 1;
	return m;
}

struct AIGameRun
{
	std::vector<std::uint32_t> hashes;
	int aiPlayers = 0;
	bool aiEnabled = false;
	std::string dozer;
	std::string side;
	unsigned long long managerUpdates = 0;
	std::vector<std::string> stops;
};

void runAIGame(starttest::Shared &s, const NewGameMessage &message, int frames, AIGameRun &out)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	out.hashes.push_back(game.logic().computeStateHash());
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		out.hashes.push_back(game.logic().computeStateHash());
	}
	const SkirmishAIManager &m = game.logic().skirmishAI();
	for (const auto &g : m.groups())
	{
		for (const auto &kv : g->players)
		{
			++out.aiPlayers;
			out.aiEnabled = !kv.second->disabled;
			out.dozer = kv.second->dozerTemplate;
			out.side = kv.second->armySide;
		}
	}
	out.managerUpdates = m.counters().managerUpdates;
	out.stops = game.logic().report().stops;
}
} // namespace

TEST_CASE("skirmish ai manager: Evendim with each of the 7 factions as the Medium computer: the slot's player gets an enabled AI with its side's dozer")
{
	OPENBFME_REQUIRE_START(s);
	const char *factions[] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
	for (const char *faction : factions)
	{
		INFO(std::string(faction));
		AIGameRun run;
		runAIGame(*s, aiMessage(*s, faction, SLOT_MED_AI), 12, run);
		CHECK(run.aiPlayers == 1);
		CHECK(run.aiEnabled);
		const PlayerTemplate *pt = s->world->playerTemplates().findPlayerTemplate(faction);
		REQUIRE(pt);
		CHECK(run.side == pt->m_side);
		REQUIRE(s->world->skirmishAI().findDozer(pt->m_side) != nullptr);
		CHECK(run.dozer == *s->world->skirmishAI().findDozer(pt->m_side));
		// frames 1 .. 13 ran (the load's frame 1, then 12): the gate opens at frame 10
		CHECK(run.managerUpdates == 4);
		bool stop412 = false;
		for (const std::string &line : run.stops)
		{
			stop412 = stop412 || line.find("[S-412] skirmish ai: 1 AI player(s)") != std::string::npos;
		}
		CHECK(stop412);
	}
}

TEST_CASE("skirmish ai manager: two runs of the same AI skirmish agree on every frame hash; another difficulty gives the same AI state (step 1 reads none)")
{
	OPENBFME_REQUIRE_START(s);
	AIGameRun a, b;
	runAIGame(*s, aiMessage(*s, "FactionMordor", SLOT_MED_AI), 15, a);
	runAIGame(*s, aiMessage(*s, "FactionMordor", SLOT_MED_AI), 15, b);
	CHECK(a.hashes == b.hashes);
	CHECK(a.hashes.size() == 16);
}

TEST_CASE("skirmish ai capture registry: neutral map flags register once, "
		  "survive ownership changes and destruction, and round-trip")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	REQUIRE(fx.w.load("Object TestFlag\n KindOf = CAPTUREFLAG\nEnd\n").empty());
	Object *first = fx.make("TestFlag", fx.players.getNeutralPlayer()->getDefaultTeam());
	Object *second = fx.make("TestFlag", fx.players.getNeutralPlayer()->getDefaultTeam());
	const ObjectID firstId = first->getID(), secondId = second->getID();
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 1);
	CHECK(m.captureFlags() == std::vector<ObjectID>{ firstId, secondId });
	m.objectAdded(*first);
	first->setCapturedTeam(fx.teamOf("C"));
	m.objectAdded(*first);
	CHECK(m.captureFlags() == std::vector<ObjectID>{ firstId, secondId });
	fx.logic->destroyObject(second);
	fx.logic->processDestroyList();
	CHECK(m.captureFlags() == std::vector<ObjectID>{ firstId, secondId });
	XferSave save;
	save.beginBlock();
	m.xfer(save);
	save.endBlock();
	StateHasher before;
	m.crc(before);
	// The v3 registry is the last field: count, then two IDs. Refused loads must be atomic.
	auto rejected = [&](std::vector<std::uint8_t> bytes, const char *message) {
		XferLoad bad(bytes);
		bad.beginBlock();
		CHECK_THROWS_WITH(m.xfer(bad), message);
		StateHasher unchanged;
		m.crc(unchanged);
		CHECK(unchanged.value() == before.value());
	};
	auto duplicate = save.data();
	std::copy(duplicate.end() - 8, duplicate.end() - 4, duplicate.end() - 4);
	rejected(duplicate, "skirmish ai: repeated registered capture flag");
	auto invalid = save.data();
	std::fill(invalid.end() - 4, invalid.end(), 0);
	rejected(invalid, "skirmish ai: an invalid registered capture flag");
	Object *third = fx.make("TestFlag", fx.players.getNeutralPlayer()->getDefaultTeam());
	REQUIRE(m.captureFlags().size() == 3);
	XferLoad load(save.data());
	load.beginBlock();
	m.xfer(load);
	load.endBlock();
	CHECK(m.captureFlags() == std::vector<ObjectID>{ firstId, secondId });
	StateHasher after;
	m.crc(after);
	CHECK(after.value() == before.value());
	m.objectAdded(*third);
	CHECK(m.captureFlags().size() == 3);
	m.reset();
	CHECK(m.captureFlags().empty());
}

TEST_CASE("skirmish ai pending attack: recruitment survives a brain update and later launches")
{
	// RW 0x8F28DD prunes active teams, not the pending prototypes at +4.
	// Exercise the observable consequence: an attack created before troops exist must still recruit and launch later.
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	AIUpdateInterface::registerClasses(fx.w.modules);
	pathtest::SyntheticTerrain terrain(100, 100);
	AIWorldConfig config;
	config.pathfind = pathtest::testConfig();
	AIWorld world(*fx.logic, config, fx.w.fx.env.macros);
	world.attach();
	world.newMap(terrain);
	LocomotorStore locomotors;
	struct LocomotorScope
	{
		GameLogic &logic;
		LocomotorStore *saved = TheLocomotorStore;
		~LocomotorScope()
		{
			logic.reset();
			TheLocomotorStore = saved;
		}
	} locomotorScope{ *fx.logic };
	TheLocomotorStore = &locomotors;
	fx.w.fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
	REQUIRE(fx.w.load(movetest::kLocomotors).empty());
	REQUIRE(fx.w.load("Object Recruit\n KindOf = CAN_ATTACK INFANTRY\n"
		" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
		" Behavior = AIUpdateInterface ModuleTag_AI\n End\n"
		" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n").empty());
	auto &manager = fx.logic->skirmishAI();
	REQUIRE(manager.newGame(true) == 1);
	auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(fx.indexOf("C")));
	REQUIRE(ai);
	ai->brain.chooser = true;
	ai->brain.chooserInitialised = true;
	fx.players.findPlayerWithName("C")->setSkirmishDifficulty(3);
	fx.store.data().difficultyTuning[3].offensiveTactic.numerator = 1;
	fx.store.data().difficultyTuning[3].offensiveTactic.denominator = 1;
	ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1;
	AITarget target;
	target.inactive = false;
	target.position = { 100.0f, 100.0f, 0.0f };
	ai->brain.targets.push_back(target);
	// Let the actual chooser, generator and startTactic reserve the target and create the pending team.
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	REQUIRE(ai->brain.tactics.size() == 1);
	REQUIRE(ai->brain.tactics[0].kind == "SimpleAttack");
	CHECK(ai->brain.tacticsStarted == 1);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	REQUIRE(ai->brain.tactics.size() == 1);
	REQUIRE(ai->brain.tactics[0].teams.size() == 1);
	CHECK_FALSE(ai->brain.tactics[0].started);
	CHECK_FALSE(ai->brain.tactics[0].ready); // RW 0x8F2348: an unmet recruiting team is not the zero-team case
	CHECK(ai->brain.targets[0].teams == 1);
	CHECK(ai->brain.bestTarget == -1); // The occupied target is unavailable to another attack.
	std::vector<ObjectID> recruits;
	for (int i = 0; i < 5; ++i)
	{
		Object *unit = fx.make("Recruit", fx.teamOf("C"));
		REQUIRE(unit);
		recruits.push_back(unit->getID());
	}
	// The real builder excludes units younger than ten seconds (50 logic frames).
	ai->disabled = true;
	for (int i = 0; i < 50; ++i)
	{
		fx.logic->runLogicFrame();
	}
	ai->disabled = false;
	REQUIRE(fx.logic->getFrame() == 50);
	REQUIRE(AITacticalAI::recordOf(*fx.logic, ai->playerIndex).army() == recruits);
	AITacticalAI::updateTeamBuilder(*fx.logic, *ai);
	CHECK(ai->brain.tactics[0].teams[0].members == recruits);
	CHECK(ai->brain.tactics[0].teams[0].handedOver);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.tactics[0].started);
	CHECK(ai->brain.attacksLaunched == 1);
	for (ObjectID id : recruits)
	{
		const Object *unit = fx.logic->findObjectByID(id);
		REQUIRE(unit);
		REQUIRE(unit->getAIUpdateInterface());
		CHECK(unit->getAIUpdateInterface()->attackMoveActive());
	}
}

TEST_CASE("skirmish ai stranded attack: target expiry releases its occupied slot")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	REQUIRE(fx.logic->skirmishAI().newGame(true) == 1);
	auto *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
	REQUIRE(ai);
	ai->brain.chooser = false;
	ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1;
	AITarget target;
	target.inactive = false;
	target.teams = 1;
	ai->brain.targets.push_back(target);
	AITactic attack; // Reproduce the old corrupted state: no pending teams, not yet started.
	attack.kind = "SimpleAttack";
	attack.targetIndex = 0;
	attack.targetOwner = ai->playerIndex;
	ai->brain.tactics.push_back(attack);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK_FALSE(ai->brain.tactics[0].ended);
	CHECK_FALSE(ai->brain.tactics[0].started);
	CHECK(ai->brain.targets[0].teams == 1);
	// Force the normal expiry branch deterministically, without depending on random draws.
	auto *army = const_cast<ArmyDefinition *>(ai->army);
	army->secondsTillTargetsCanExpire = 0.0f;
	army->chanceForTargetToExpire = 0.0f;
	ai->brain.chooser = true;
	ai->brain.chooserInitialised = true;
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.targets[0].dropped);
	CHECK(ai->brain.targets[0].teams == 0);
	CHECK(ai->brain.tactics[0].ended);
}

TEST_CASE("skirmish ai capture squad: allied flags do not start a squad; a neutral flag starts one that survives waiting for recruits")
{
	for (bool allied : { true, false })
	{
		INFO(allied);
		AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
		REQUIRE(fx.w.load("Object TestFlag\n KindOf = CAPTUREFLAG\nEnd\n").empty());
		fx.make("TestFlag", allied ? fx.teamOf("C") : fx.players.getNeutralPlayer()->getDefaultTeam());
		SkirmishAIManager &m = fx.logic->skirmishAI();
		REQUIRE(m.newGame(true) == 1);
		AISkirmishPlayer *ai = const_cast<AISkirmishPlayer *>(m.findAI(fx.indexOf("C")));
		REQUIRE(ai);
		ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1; // Isolate capture eligibility from the other implemented targetless tactic.
		fx.logic->random().enableCallLog(true);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(ai->brain.tacticsByKind.count("FlagCaptureSquad") == (allied ? 0 : 1));
		if (allied) { continue; }
		REQUIRE(ai->brain.tactics.size() == 1);
		REQUIRE(ai->brain.tactics[0].teams.size() == 1);
		const AITacticTeam &team = ai->brain.tactics[0].teams[0];
		CHECK(team.members.empty());
		CHECK_FALSE(team.handedOver);
		CHECK(team.minimum == 1);
		CHECK(team.priority == 30);
		const GameLogicRandom::Call &setup = fx.logic->random().callLog().back();
		CHECK(setup.file == "AIFlagCaptureSquad.cpp");
		CHECK(setup.line == 0xEF);
		CHECK(setup.lo == 1);
		CHECK(setup.hi == 3);
		CHECK(team.maximum == setup.result);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		REQUIRE(ai->brain.tactics.size() == 1);
		CHECK(ai->brain.tactics[0].teams.size() == 1);
		CHECK(ai->brain.namedCounters.at("AIFlagCaptureSquad_IsRunning") == 1);
		// A handed-over team losing its final member ends the squad and schedules its next attempt.
		ai->brain.tactics[0].teams[0].handedOver = true;
		ai->brain.tactics[0].started = true;
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(ai->brain.tactics[0].ended);
		CHECK(ai->brain.namedCounters.at("AIFlagCaptureSquad_IsRunning") == 0);
		const GameLogicRandom::Call &cleanup = fx.logic->random().callLog().back();
		CHECK(cleanup.file == "AIFlagCaptureSquad.cpp");
		CHECK(cleanup.line == 0xD5);
		CHECK(cleanup.lo == 100);
		CHECK(cleanup.hi == 450);
		CHECK(ai->brain.namedCounters.at("AIFlagCaptureSquad_FrameNextRun") == (int)fx.logic->getFrame() + cleanup.result);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(ai->brain.tactics.empty());
	}
}

TEST_CASE("skirmish ai return the ring: a HOLDING_THE_RING unit's one-member team, the nearest non-summoned fortress and the garrison end (RW 0x9B89CF / 0x9B877C / 0x9B8A7B / 0x9B8A06 / 0x9B88D8 / 0x9B8994 / 0x9B8A47)")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	pathtest::SyntheticTerrain terrain(100, 100);
	AIWorldConfig config;
	config.pathfind = pathtest::testConfig();
	AIWorld world(*fx.logic, config, fx.w.fx.env.macros);
	world.attach();
	world.newMap(terrain);
	LocomotorStore locomotors;
	struct LocomotorScope
	{
		GameLogic &logic;
		LocomotorStore *saved = TheLocomotorStore;
		~LocomotorScope()
		{
			logic.reset();
			TheLocomotorStore = saved;
		}
	} locomotorScope{ *fx.logic };
	TheLocomotorStore = &locomotors;
	fx.w.fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
	REQUIRE(fx.w.load(movetest::kLocomotors).empty());
	GarrisonContain::registerClasses(fx.w.modules);
	const auto ringErrors = fx.w.load("Object RingBearer\n KindOf = CAN_ATTACK INFANTRY\n TransportSlotCount = 1\n"
		" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
		" Behavior = AIUpdateInterface ModuleTag_AI\n End\n"
		" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n"
		"Object Fortress\n KindOf = STRUCTURE DOZER_FACTORY IMMOBILE\n"
		" Geometry = CYLINDER\n GeometryMajorRadius = 20\n GeometryHeight = 40\n"
		" Behavior = CitadelSlaughterHordeContain ModuleTag_Contain\n ContainMax = 1\n EnterSound = NoSound\n"
		" StatusForRingEntry = HOLDING_THE_RING\n ObjectToDestroyForRingEntry = NONE\n End\nEnd\n"
		"Object SummonedFortress\n KindOf = STRUCTURE DOZER_FACTORY SUMMONED\nEnd\n");
	REQUIRE_MESSAGE(ringErrors.empty(), ringErrors);
	AIUpdateInterface::registerClasses(fx.w.modules);
	REQUIRE(fx.logic->skirmishAI().newGame(true) == 1);
	auto *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
	REQUIRE(ai);
	ai->build.startPosition = { 100.0f, 100.0f, 0.0f };
	ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1;
	fx.players.findPlayerWithName("C")->setSkirmishDifficulty(1); // not EASY: AIRoamingDefenseTactic stays out
	Object *holder = fx.make("RingBearer", fx.teamOf("C"));
	REQUIRE(holder);
	const Coord3D holderPos{ 200.0f, 200.0f, 0.0f };
	holder->setPosition(&holderPos);
	Object *fortress = fx.make("Fortress", fx.teamOf("C"));
	REQUIRE(fortress);
	const Coord3D fortressPos{ 500.0f, 500.0f, 0.0f };
	fortress->setPosition(&fortressPos);
	// RW 0x9B8930 skips a corpse still present in the structure record.
	Object *dead = fx.make("Fortress", fx.teamOf("C"));
	const Coord3D deadPos{ 210, 210, 0 };
	dead->setPosition(&deadPos);
	dead->friend_onDie(DieModuleInterface::Event());
	// A closer SUMMONED fortress is skipped (template KindOf 179, RW 0x9B8945).
	Object *summoned = fx.make("SummonedFortress", fx.teamOf("C"));
	REQUIRE(summoned);
	const Coord3D summonedPos{ 300.0f, 300.0f, 0.0f };
	summoned->setPosition(&summonedPos);
	// The manager starts at frame 10 (RW 0x6A96B6); the ready frame stores the game frame, where 0 means "unset".
	for (int i = 0; i < 10; ++i)
	{
		fx.logic->runLogicFrame();
	}
	REQUIRE(fx.logic->getFrame() == 10);
	// Without a ring holder the prototype does not apply (RW 0x9B877C).
	const int unportedBefore = ai->brain.targetlessUnported;
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.tacticsByKind.count("ReturnTheRing") == 0);
	CHECK(ai->brain.targetlessUnported - unportedBefore == 6); // six remaining applicability checks, once per pick
	holder->setStatus((unsigned)CombatNames::status("HOLDING_THE_RING"), true);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	REQUIRE(ai->brain.tacticsByKind.count("ReturnTheRing") == 1);
	REQUIRE(ai->brain.tactics.size() == 1);
	CHECK(ai->brain.tactics[0].squadTarget == holder->getID()); // the prototype's + 0x5C: the found bearer
	CHECK(ai->brain.tactics[0].teams.empty());
	// RW 0x8F11DA starts a zero-team prototype after its ready frame; RW 0x9B8A7B builds the one-member team. The pick can
	// add a second clone before the key is set; its run then ends it (RW 0x9B8A7B's + 0x65 == 0), as retail does.
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK_FALSE(ai->brain.tactics[0].started);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	auto live = [&]() -> AITactic * {
		for (AITactic &t : ai->brain.tactics)
		{
			if (t.kind == "ReturnTheRing" && !t.ended)
			{
				return &t;
			}
		}
		return nullptr;
	};
	AITactic *tac = live();
	REQUIRE(tac);
	CHECK(tac->started);
	CHECK(tac->step == 1); // + 0x65: the run created the team
	REQUIRE(tac->teams.size() == 1);
	CHECK(tac->teams[0].members == std::vector<ObjectID>{ holder->getID() });
	CHECK(ai->brain.namedCounters.at("AIReturnTheRingTactic_IsRunning") == 1);
	CHECK(tac->step2 == (int)fortress->getID()); // the nearest NON-SUMMONED DOZER_FACTORY
	// Whole game save/load/continue, restoring the checkpoint after comparing the next update.
	const ObjectID holderID = holder->getID(), fortressID = fortress->getID();
	auto roundTrip = [&]() {
		std::vector<std::uint8_t> saved, again;
		std::string error;
		const auto hash = fx.logic->computeStateHash();
		REQUIRE_MESSAGE(fx.logic->saveSnapshot(saved, &error), error);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		const auto continued = fx.logic->computeStateHash();
		REQUIRE_MESSAGE(fx.logic->loadSnapshot(saved, &error), error);
		ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
		CHECK(fx.logic->computeStateHash() == hash);
		REQUIRE_MESSAGE(fx.logic->saveSnapshot(again, &error), error);
		CHECK(again == saved);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(fx.logic->computeStateHash() == continued);
		REQUIRE_MESSAGE(fx.logic->loadSnapshot(saved, &error), error);
		ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
		holder = fx.logic->findObjectByID(holderID);
		fortress = fx.logic->findObjectByID(fortressID);
		tac = live();
	};
	roundTrip(); // journey
	bool deliver = false;
	SUBCASE("entry and delivery") { deliver = true; }
	SUBCASE("stationary entry times out") {}
	SUBCASE("destroyed destination during travel")
	{
		fx.logic->destroyObject(fortress);
		roundTrip(); // issued but now absent fortress ID
		REQUIRE(tac);
		tac->teams[0].lastPosition = *holder->getPosition();
		tac->teams[0].idleFrames = 15;
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(live()->step == 1); // only the dead and summoned structures remain
		return;
	}
	SUBCASE("no fortress sentinel")
	{
		tac->step2 = -1;
		roundTrip();
		CHECK(live()->step2 == -1);
		return;
	}
	// RW 0x9B8A47: every team done (RW 0x8F175A) -> RW 0x9B8994 garrisons the fortress and marks + 0x64; the next done update ends.
	AIUpdateInterface *holderAI = holder->getAIUpdateInterface();
	REQUIRE(holderAI);
	holderAI->aiIdle(CMD_FROM_PLAYER);
	// The team's last position starts at the origin; place it where the holder stands so the idle counter is not reset.
	tac->teams[0].lastPosition = *holder->getPosition();
	holderAI->aiIdle(CMD_FROM_PLAYER);
	tac->teams[0].idleFrames = 15; // TeamTimeUntilConsideredIdle 3 s * FPS 5
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	tac = live();
	REQUIRE(tac);
	CHECK(tac->step == 2);
	CHECK(holderAI->currentStateId() == AI_MOVE_TO_POSITION_AND_ENTER);
	CHECK(holderAI->stateMachine().goalObjectID() == fortressID);
	CHECK(holderAI->lastCommandSource() == CMD_FROM_PLAYER);
	CHECK(tac->teams[0].idleFrames == 0); // RW 0x8F192A
	roundTrip(); // entry issued
	if (deliver)
	{
		// Let the actual command walk into the ring-capable contain; never supply the entry ourselves.
		for (int i = 0; i < 300 && holder->testStatus((unsigned)CombatNames::status("HOLDING_THE_RING")); ++i)
			fx.logic->runLogicFrame();
		INFO(holder->getPosition()->x << "," << holder->getPosition()->y << " state " << holder->getAIUpdateInterface()->currentStateId() << " contained " << (holder->getContainedBy() != nullptr));
		CHECK_FALSE(holder->testStatus((unsigned)CombatNames::status("HOLDING_THE_RING")));
		for (int i = 0; i < 100 && ai->brain.namedCounters.at("AIReturnTheRingTactic_IsRunning") != 0; ++i)
			fx.logic->runLogicFrame();
		CHECK(ai->brain.namedCounters.at("AIReturnTheRingTactic_IsRunning") == 0); // RW 0x9B8635 after actual delivery
		CHECK(live() == nullptr);
		roundTrip();
		return;
	}
	holderAI = holder->getAIUpdateInterface();
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	REQUIRE(live());
	CHECK(live()->teams[0].idleFrames == 1);
	// Stationary simulation: complete only after the fresh 15-update idle period.
	for (int i = 1; i < 15; ++i) AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	// The end itself clears the key (RW 0x9B8635), so the same update's targetless pick can start a fresh clone for the
	// holder that still carries the ring; the ended one is what this step checks.
	bool ended = false;
	for (const AITactic &t : ai->brain.tactics)
	{
		ended = ended || (t.kind == "ReturnTheRing" && t.ended);
	}
	CHECK(ended);
	CHECK(ai->brain.namedCounters.at("AIReturnTheRingTactic_IsRunning") == 0); // RW 0x9B8635
	roundTrip(); // ended but not yet removed

}



TEST_CASE("skirmish ai roaming defense: EASY only, the 5 s scheduling, the zero-team run and the first idle unit (RW 0x9B7F56 / 0x9B8047 / 0x9B80FA / 0x9B7EBD / 0x9B83A6)")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	pathtest::SyntheticTerrain terrain(100, 100);
	AIWorldConfig config;
	config.pathfind = pathtest::testConfig();
	AIWorld world(*fx.logic, config, fx.w.fx.env.macros);
	world.attach();
	world.newMap(terrain);
	LocomotorStore locomotors;
	struct Scope {
		GameLogic &logic;
		LocomotorStore *saved = TheLocomotorStore;
		~Scope() { logic.reset(); TheLocomotorStore = saved; }
	} scope{ *fx.logic };
	TheLocomotorStore = &locomotors;
	fx.w.fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
	REQUIRE(fx.w.load(movetest::kLocomotors).empty());
	AIUpdateInterface::registerClasses(fx.w.modules);
	InvisibilityModules::registerAll(fx.w.modules);
	REQUIRE(fx.w.load("Object Roamer\n KindOf = CAN_ATTACK INFANTRY\n"
		" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
		" Behavior = AIUpdateInterface ModuleTag_AI\n AutoAcquireEnemiesWhenIdle = Yes\n End\n"
		" Behavior = StealthUpdate ModuleTag_Stealth\n StealthDelay = 10000\n End\n"
		" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n"
		"Object Farm\n KindOf = STRUCTURE ECONOMY_STRUCTURE IMMOBILE\nEnd\n").empty());
	REQUIRE(fx.logic->skirmishAI().newGame(true) == 1);
	auto *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
	REQUIRE(ai);
	Player *computer = fx.players.findPlayerWithName("C");
	REQUIRE(computer);
	ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1; // isolate this prototype from FarmKillSquad
	computer->setSkirmishDifficulty(0); // RW 0x6AA61B == 0: EASY only
	fx.logic->random().enableCallLog(true);
	// RW 0x9B7F56: the first check only schedules the next attempt 5 s (FPS * 5 = 25 frames) on and answers false.
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.tactics.empty());
	CHECK(ai->brain.namedCounters.at("AIRoamingDefenseTactic_NextLogicFrameRun") == (int)fx.logic->getFrame() + 25);
	CHECK(ai->brain.namedCounters.at("AIRoamingDefenseTactic_IsRunning") == 0);
	CHECK(ai->brain.targetlessUnported == 6); // Ring, WoTR and roaming are all ported (S-893)
	// Any difficulty but EASY answers false without scheduling (RW 0x6AA61B).
	computer->setSkirmishDifficulty(1);
	ai->brain.namedCounters["AIRoamingDefenseTactic_NextLogicFrameRun"] = 0;
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.namedCounters.at("AIRoamingDefenseTactic_NextLogicFrameRun") == 0);
	CHECK(ai->brain.tactics.empty());
	// EASY and due: the pool draw starts it; RW 0x9B80FA sets IsRunning in the zero-team setup. The manager starts at frame
	// 10 (RW 0x6A96B6), so advance there first with the scheduled 25 still in the future.
	for (int i = 0; i < 10; ++i)
	{
		fx.logic->runLogicFrame();
	}
	REQUIRE(fx.logic->getFrame() == 10);
	CHECK(ai->brain.tactics.empty());
	computer->setSkirmishDifficulty(0);
	ai->brain.namedCounters["AIRoamingDefenseTactic_NextLogicFrameRun"] = 10;
	fx.logic->random().clearCallLog();
	fx.logic->runLogicFrame(); // frame 11: the manager's updateBrain sees the due frame
	REQUIRE(fx.logic->getFrame() == 11);
	REQUIRE(ai->brain.tactics.size() == 1);
	CHECK(ai->brain.tactics[0].kind == "AIRoamingDefenseTactic");
	CHECK(ai->brain.tactics[0].teams.empty()); // RW 0x851E97: no team
	CHECK(ai->brain.namedCounters.at("AIRoamingDefenseTactic_IsRunning") == 1);
	CHECK(ai->brain.targetlessPicks == 1);
	CHECK(ai->brain.targetlessStarted == 1);
	const GameLogicRandom::Call &pick = fx.logic->random().callLog().back();
	CHECK(pick.file == "AITacticsGenerator.cpp");
	CHECK(pick.line == 0x1BC);
	auto roundTrip = [&]() {
		std::vector<std::uint8_t> saved, again;
		std::string error;
		const auto hash = fx.logic->computeStateHash();
		REQUIRE_MESSAGE(fx.logic->saveSnapshot(saved, &error), error);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		const auto continued = fx.logic->computeStateHash();
		REQUIRE_MESSAGE(fx.logic->loadSnapshot(saved, &error), error);
		ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
		CHECK(fx.logic->computeStateHash() == hash);
		REQUIRE_MESSAGE(fx.logic->saveSnapshot(again, &error), error);
		CHECK(again == saved);
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(fx.logic->computeStateHash() == continued);
		REQUIRE_MESSAGE(fx.logic->loadSnapshot(saved, &error), error);
		ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
	};
	roundTrip(); // scheduled
	// RW 0x8F232F gives a zero-team prototype its ready flag; RW 0x8F11DA then starts it and its run RW 0x9B7EBD sets the roaming
	// state (+ 0x58) after the ready frame.
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK_FALSE(ai->brain.tactics[0].started);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.tactics[0].started);
	CHECK(ai->brain.tactics[0].step == 1);
	roundTrip(); // started
	const Coord3D position = ai->brain.tactics[0].position, origin = ai->brain.tactics[0].origin;
	// RW 0x9B8412: the first idle army unit belongs to another logical team; only the next free one roams.
	Object *first = fx.make("Roamer", fx.teamOf("C"));
	Object *free = fx.make("Roamer", fx.teamOf("C"));
	Object *later = fx.make("Roamer", fx.teamOf("C"));
	const Coord3D from{ 200, 200, 0 }, destination{ 600, 200, 37 };
	for (Object *unit : {first, free, later}) unit->setPosition(&from);
	free->setStatus((unsigned)CombatNames::status("CAN_STEALTH"), true);
	Object *farm = fx.make("Farm", fx.teamOf("C"));
	farm->setPosition(&destination);
	AITactic assigned;
	assigned.kind = "ReturnTheRing";
	assigned.kindIndex = 105;
	assigned.targetless = true;
	assigned.started = true;
	assigned.step = 1;
	assigned.step2 = -1;
	assigned.id = ai->brain.nextTacticId++;
	assigned.teams.emplace_back();
	assigned.teams[0].handedOver = true;
	assigned.teams[0].members = {first->getID()};
	bool allAssigned = false, refused = false;
	SUBCASE("command refusal still follows selection")
	{
		refused = true;
		free->getAIUpdateInterface()->setPreventPlayerCommands(true);
	}
	SUBCASE("logical assignment") {}
	SUBCASE("actual non-default Team")
	{
		first->setTeam(fx.teams.createTeam(fx.teams.initTeam("Other", fx.players.findPlayerWithName("C"), false)));
		assigned.teams[0].members.clear();
	}
	SUBCASE("all logically assigned: no destination draw")
	{
		assigned.teams[0].members = {first->getID(), free->getID(), later->getID()};
		allAssigned = true;
	}
	ai->brain.tactics.push_back(assigned);
	fx.logic->random().clearCallLog();
	// RW 0x9B83A6: dispatch once, then end; RW 0x9B8047 clears
	// both keys; the same update's targetless pick then sees the cleared next frame and schedules it 5 s on (RW 0x9B7F56).
	const int before = (int)fx.logic->getFrame();
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	CHECK(ai->brain.tactics[0].ended);
	CHECK(ai->brain.tactics[0].position.x == position.x);
	CHECK(ai->brain.tactics[0].position.y == position.y);
	CHECK(ai->brain.tactics[0].position.z == position.z);
	CHECK(ai->brain.tactics[0].origin.x == origin.x);
	CHECK(ai->brain.tactics[0].origin.y == origin.y);
	CHECK(ai->brain.tactics[0].origin.z == origin.z);
	CHECK_FALSE(first->getAIUpdateInterface()->attackMoveActive());
	CHECK_FALSE(later->getAIUpdateInterface()->attackMoveActive());
	CHECK(free->getAIUpdateInterface()->attackMoveActive() == (!allAssigned && !refused));
	REQUIRE(fx.logic->random().callLog().size() == (allAssigned ? 0 : 1));
	if (!allAssigned)
	{
		auto *u = free->getAIUpdateInterface();
		if (!refused)
		{
			CHECK(u->lastCommandSource() == CMD_FROM_PLAYER);
			CHECK(u->stateMachine().goalPosition().x == destination.x);
			CHECK(u->stateMachine().goalPosition().y == destination.y);
			CHECK(u->stateMachine().goalPosition().z == destination.z);
		}
		else CHECK(u->isIdle());
		const auto &draw = fx.logic->random().callLog()[0];
		CHECK(draw.file == "AIRoamingDefenseTactic.cpp");
		CHECK(draw.line == 0xA4);
		CHECK(draw.lo == 0);
		CHECK(draw.hi == 0);
		CHECK(draw.result == 0);
		CHECK_FALSE(draw.real);
		CHECK(draw.drewNumber);
	}
	// The ring tactic's dynamic team count must still average its one team (RW 0x5F2DC6 / 0x9B8878).
	if (!assigned.teams[0].members.empty())
	{
		CHECK(ai->brain.tactics[1].position.x == from.x);
		CHECK(ai->brain.tactics[1].position.y == from.y);
	}
	// RW 0x696266 replaces the old state without routing a type-0 group move.
	if (!allAssigned && !refused)
	{
		free->getAIUpdateInterface()->aiBusy(CMD_FROM_PLAYER);
		REQUIRE(free->getAIUpdateInterface()->isBusy());
		fx.logic->random().clearCallLog();
		const Coord3D next{ 400, 300, 53 };
		REQUIRE(free->getAIUpdateInterface()->aiAttackMoveToPosition(next, CMD_FROM_PLAYER));
		CHECK_FALSE(free->getAIUpdateInterface()->isBusy());
		CHECK(free->getAIUpdateInterface()->stateMachine().goalPosition().x == next.x);
		CHECK(free->getAIUpdateInterface()->stateMachine().goalPosition().y == next.y);
		CHECK(free->getAIUpdateInterface()->stateMachine().goalPosition().z == next.z);
		CHECK(fx.logic->random().callLog().size() == 0);
	}
	ai->brain.tactics.pop_back(); // the assignment fixture is separate from the roaming snapshot
	roundTrip(); // ended, before removal
	CHECK(ai->brain.namedCounters.at("AIRoamingDefenseTactic_IsRunning") == 0);
	CHECK(ai->brain.namedCounters.at("AIRoamingDefenseTactic_NextLogicFrameRun") == before + 25);
}

TEST_CASE("skirmish ai roaming destination: nearby economy, or an empty economy record and a fortress-ring draw (RW 0x9B8160)")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	REQUIRE(fx.w.load("Object Fortress\n KindOf = STRUCTURE DOZER_FACTORY\nEnd\n"
		"Object Farm\n KindOf = STRUCTURE ECONOMY_STRUCTURE\nEnd\n").empty());
	REQUIRE(fx.logic->skirmishAI().newGame(true) == 1);
	auto *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
	REQUIRE(ai);
	ai->build.startPosition = { 100.0f, 100.0f, 0.0f };
	Object *fortress = fx.make("Fortress", fx.teamOf("C"));
	REQUIRE(fortress);
	const Coord3D fortressPos{ 500.0f, 500.0f, 0.0f };
	fortress->setPosition(&fortressPos);
	Object *farm = fx.make("Farm", fx.teamOf("C"));
	REQUIRE(farm);
	const Coord3D farmPos{ 700.0f, 100.0f, 0.0f };
	farm->setPosition(&farmPos);
	fx.logic->random().enableCallLog(true);
	// The economy structure is inside the 900 radius (RW 0xC89CF0): its position, drawn 0 .. 0 at line 0xA4.
	Coord3D out{};
	REQUIRE(AITacticalAI::roamDestinationForTest(*fx.logic, *ai, *farm, out));
	CHECK(out.x == 700.0f);
	CHECK(out.y == 100.0f);
	REQUIRE(fx.logic->random().callLog().size() == 1);
	const GameLogicRandom::Call &economy = fx.logic->random().callLog()[0];
	CHECK(economy.file == "AIRoamingDefenseTactic.cpp");
	CHECK(economy.line == 0xA4);
	CHECK(economy.lo == 0);
	CHECK(economy.hi == 0);
	CHECK(economy.result == 0);
	CHECK(economy.drewNumber); // even 0..0 advances the retail stream
	CHECK_FALSE(economy.real);
	// RW 0x9B826A indexes the candidate vector in collector order.
	Object *second = fx.make("Farm", fx.teamOf("C"));
	const Coord3D secondPos{ 400, 200, 11 };
	second->setPosition(&secondPos);
	fx.logic->random().clearCallLog();
	auto expectedRandom = fx.logic->random();
	const int selected = expectedRandom.getValue(0, 1, "AIRoamingDefenseTactic.cpp", 0xA4);
	REQUIRE(AITacticalAI::roamDestinationForTest(*fx.logic, *ai, *farm, out));
	const Coord3D expected = selected == 0 ? farmPos : secondPos;
	CHECK(out.x == expected.x);
	CHECK(out.y == expected.y);
	CHECK(out.z == expected.z);
	REQUIRE(fx.logic->random().callLog().size() == 1);
	const auto &multiple = fx.logic->random().callLog()[0];
	CHECK(multiple.file == "AIRoamingDefenseTactic.cpp");
	CHECK(multiple.line == 0xA4);
	CHECK(multiple.lo == 0);
	CHECK(multiple.hi == 1);
	CHECK(multiple.result == selected);
	CHECK(multiple.drewNumber);
	CHECK_FALSE(multiple.real);
	CHECK(fx.logic->random().seedArray() == expectedRandom.seedArray());
	fx.logic->destroyObject(second);
	fx.logic->processDestroyList();
	// RW 0x9B8251: a nonempty economy list with no nearby member consumes no draw.
	const Coord3D farFarmPos{ 1200.0f, 100.0f, 0.0f };
	farm->setPosition(&farFarmPos);
	fx.logic->random().clearCallLog();
	out = { 1, 2, 3 };
	CHECK_FALSE(AITacticalAI::roamDestinationForTest(*fx.logic, *ai, *farm, out));
	CHECK(fx.logic->random().callLog().size() == 0);
	CHECK(out.x == 1);
	CHECK(out.y == 2);
	CHECK(out.z == 3);
	// Boundary witness: RW (x²+y²)+z² = 809999.9375f; reversed order = 810000.0f.
	ai->build.startPosition = { 0, 0, 0 };
	const Coord3D witness{ 514.9111328125f, 475.62969970703125f, 564.484619140625f };
	farm->setPosition(&witness);
	REQUIRE(AITacticalAI::roamDestinationForTest(*fx.logic, *ai, *farm, out));
	CHECK(out.x == witness.x);
	CHECK(out.y == witness.y);
	CHECK(out.z == witness.z);
	REQUIRE(fx.logic->random().callLog().size() == 1);
	const auto &boundary = fx.logic->random().callLog()[0];
	CHECK(boundary.file == "AIRoamingDefenseTactic.cpp");
	CHECK(boundary.line == 0xA4);
	CHECK(boundary.lo == 0);
	CHECK(boundary.hi == 0);
	CHECK(boundary.result == 0);
	CHECK(boundary.drewNumber);
	CHECK_FALSE(boundary.real);
	// Only an empty economy list reaches the fortress branch (RW 0x9B818E).
	fx.logic->destroyObject(farm);
	fx.logic->processDestroyList();
	fx.logic->random().clearCallLog();
	REQUIRE(AITacticalAI::roamDestinationForTest(*fx.logic, *ai, *fortress, out));
	REQUIRE(fx.logic->random().callLog().size() == 1);
	const GameLogicRandom::Call &angle = fx.logic->random().callLog()[0];
	CHECK(angle.file == "AIRoamingDefenseTactic.cpp");
	CHECK(angle.line == 0xBA);
	CHECK(angle.lo == 0);
	CHECK(angle.hi == 6);
	CHECK(angle.drewNumber);
	CHECK_FALSE(angle.real);
	const double radians = (double)angle.result;
	// tolerance: the binary's MSVCR71 sin/cos (double, RW 0xA3CF90 / 0xA3CF84) rounded to float vs this host's libm in double.
	CHECK(out.x == doctest::Approx(500.0f + 400.0 * std::cos(radians)).epsilon(1e-5));
	// tolerance: as above.
	CHECK(out.y == doctest::Approx(500.0f + 400.0 * std::sin(radians)).epsilon(1e-5));
	CHECK(out.z == 0.0f);
}

TEST_CASE("skirmish ai roaming destination: no economy structure and no fortress gives no destination (RW 0x9B8160)")
{
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	REQUIRE(fx.w.load("Object SoldierUnit\n KindOf = CAN_ATTACK\nEnd\n").empty());
	REQUIRE(fx.logic->skirmishAI().newGame(true) == 1);
	auto *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
	REQUIRE(ai);
	Object *unit = fx.make("SoldierUnit", fx.teamOf("C"));
	REQUIRE(unit);
	fx.logic->random().enableCallLog(true);
	Coord3D out{ 1.0f, 2.0f, 3.0f };
	CHECK_FALSE(AITacticalAI::roamDestinationForTest(*fx.logic, *ai, *unit, out));
	CHECK(out.x == 1.0f);
	CHECK(fx.logic->random().callLog().size() == 0);
}


TEST_CASE("skirmish ai farm choice: the farm and production selectors' draws (RW 0x9BBA51 / 0x9BB5E4 / 0x9BBF4C)")
{
	// The binary facts: RW 0x9BBA51 selects from the enemy record's economy list (record + 0xC -> + 4), RW 0x9BB5E4 from its structure list
	// (record + 8 -> + 4); both take the enemy fortress as the first DOZER_FACTORY in that structure list (RW 0x9B58BF, no status test), score each
	// candidate (base 5000 when the base is nearer than 5000, RW 0xC8A1D8; reject within 275 of the base, RW 0xC8A1D0; the production attribute
	// RW 0x68C82D 13 halves above 2; x 0.01, RW 0xBE5600; a floor of 5), and a first draw of 0 among 0 .. 3 (line 0x203 / 0x186) takes a weighted
	// draw 0 .. total (line 0x20A / 0x18D); anything else takes the highest weight. The caller draws 0 .. 4 at line 0x8B and lets the production
	// choice replace a farm only on 0. Without a fortress the selectors draw up to three ids (line 0x21D / 0x1A0) and return the last object.
	AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
	GateModules::registerAll(fx.w.modules); // GateOpenAndCloseBehavior, for the production choice's open-gate filter
	AIUpdateInterface::registerClasses(fx.w.modules);
	pathtest::SyntheticTerrain terrain(100, 100);
	AIWorldConfig config;
	config.pathfind = pathtest::testConfig();
	AIWorld world(*fx.logic, config, fx.w.fx.env.macros);
	world.attach();
	world.newMap(terrain);
	LocomotorStore locomotors;
	struct LocomotorScope
	{
		GameLogic &logic;
		LocomotorStore *saved = TheLocomotorStore;
		~LocomotorScope()
		{
			logic.reset();
			TheLocomotorStore = saved;
		}
	} locomotorScope{ *fx.logic };
	TheLocomotorStore = &locomotors;
	fx.w.fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
	REQUIRE(fx.w.load(movetest::kLocomotors).empty());
	const std::string recruitError = fx.w.load(
		"Object Recruit\n KindOf = CAN_ATTACK INFANTRY\n"
		" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
		" Behavior = AIUpdateInterface ModuleTag_AI\n End\n"
		" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n");
	REQUIRE_MESSAGE(recruitError.empty(), recruitError);
	const std::string fortressError = fx.w.load(
		"Object TestFortress\n KindOf = STRUCTURE DOZER_FACTORY IMMOBILE\n"
		" Geometry = CYLINDER\n GeometryMajorRadius = 30\n GeometryMinorRadius = 30\n GeometryHeight = 60\nEnd\n");
	REQUIRE_MESSAGE(fortressError.empty(), fortressError);
	const std::string farmError = fx.w.load(
		"Object TestFarm\n KindOf = STRUCTURE ECONOMY_STRUCTURE IMMOBILE\n"
		" Geometry = CYLINDER\n GeometryMajorRadius = 20\n GeometryMinorRadius = 20\n GeometryHeight = 40\nEnd\n");
	REQUIRE_MESSAGE(farmError.empty(), farmError);
	const std::string gateError = fx.w.load(
		"Object TestGate\n KindOf = STRUCTURE BLOCKING_GATE IMMOBILE\n"
		" Geometry = CYLINDER\n GeometryMajorRadius = 20\n GeometryMinorRadius = 20\n GeometryHeight = 40\n"
		" Behavior = GateOpenAndCloseBehavior ModuleTag_Gate\n  OpenByDefault = Yes\n End\nEnd\n");
	REQUIRE_MESSAGE(gateError.empty(), gateError);
	const std::string wallError = fx.w.load(
		"Object TestWallSegment\n KindOf = STRUCTURE WALL_SEGMENT IMMOBILE\n"
		" Geometry = CYLINDER\n GeometryMajorRadius = 20\n GeometryMinorRadius = 20\n GeometryHeight = 40\nEnd\n");
	REQUIRE_MESSAGE(wallError.empty(), wallError);
	auto &manager = fx.logic->skirmishAI();
	REQUIRE(manager.newGame(true) == 1);
	const int me = fx.indexOf("C"), enemy = fx.indexOf("H");
	auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(me));
	REQUIRE(ai);
	Object *squad = fx.make("Recruit", fx.teamOf("C"));
	REQUIRE(squad);
	Coord3D squadPos{ 100.0f, 100.0f, 0.0f };
	squad->teleportTo(&squadPos, true);
	Object *fortress = fx.make("TestFortress", fx.teamOf("H"));
	REQUIRE(fortress);
	Coord3D fortressPos{ 700.0f, 700.0f, 0.0f };
	fortress->teleportTo(&fortressPos, true);
	Object *farmA = fx.make("TestFarm", fx.teamOf("H"));
	Object *farmB = fx.make("TestFarm", fx.teamOf("H"));
	REQUIRE(farmA);
	REQUIRE(farmB);
	Coord3D farmAPos{ 1000.0f, 400.0f, 0.0f }, farmBPos{ 300.0f, 900.0f, 0.0f };
	farmA->teleportTo(&farmAPos, true);
	farmB->teleportTo(&farmBPos, true);
	// the records are the manager's; both farms are in the economy list, the fortress is in the structure list
	CHECK(AITacticalAI::recordOf(*fx.logic, enemy).economy() == std::vector<ObjectID>{ farmA->getID(), farmB->getID() });
	CHECK(AITacticalAI::recordOf(*fx.logic, enemy).structures() == std::vector<ObjectID>{ fortress->getID() });
	// base (700,700), team centre (100,100), direction (600,600), distance 848.53, baseline 5000; farm A: toBase 424.26, toTeam 948.68,
	// projection 1.0 (the line test: 424.26 > 275, kept) -> trunc((5000 + 424.26 - 948.68) * 0.01) = trunc(44.76) = 44; farm B: toBase 447.21,
	// toTeam 824.62, projection 0.83 -> trunc((5000 + 447.21 - 824.62) * 0.01) = trunc(46.23) = 46; total 90
	AITactic tac;
	tac.targetless = true;
	tac.kind = "FarmKillSquad";
	tac.kindIndex = 101; // kTargetlessBase (100) + TL_FARM_KILL (1)
	tac.id = 1;
	AITacticTeam team;
	team.name = "TARGETLESS_FarmKillSquad_1_0";
	team.type = 4;
	team.index = 0;
	team.members = { squad->getID() };
	team.handedOver = true;
	tac.teams.push_back(team);
	ai->brain.currentEnemy = enemy;
	ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1; // the per-frame picker adds no second squad
	ai->brain.tactics.push_back(tac);
	fx.logic->runLogicFrame(); // frame 1: the ready frame cannot be 0 (RW 0x8F11DA stores it and tests it against 0)
	REQUIRE(fx.logic->getFrame() == 1);
	fx.logic->random().enableCallLog(true);
	AITacticalAI::updateBrain(*fx.logic, *ai, ai); // the ready frame
	AITacticalAI::updateBrain(*fx.logic, *ai, ai); // started
	REQUIRE(ai->brain.tactics.size() == 1);
	REQUIRE(ai->brain.tactics[0].started);
	fx.logic->random().clearCallLog();
	AITacticalAI::updateBrain(*fx.logic, *ai, ai); // the started FarmKillSquad body
	REQUIRE(ai->brain.tactics.size() == 1);
	AITactic &active = ai->brain.tactics[0];
	const auto &calls = fx.logic->random().callLog();
	REQUIRE(!calls.empty());
	CHECK(calls[0].file == "AIFarmKillSquad.cpp");
	CHECK(calls[0].line == 0x203);
	CHECK(calls[0].lo == 0);
	CHECK(calls[0].hi == 3);
	Object *expected = farmB; // the highest weight, 46
	if (calls[0].result == 0)
	{
		REQUIRE(calls.size() == 3); // 0x203, the weighted 0x20A, 0x8B
		CHECK(calls[1].line == 0x20A);
		CHECK(calls[1].lo == 0);
		CHECK(calls[1].hi == 90);
		expected = calls[1].result < 44 ? farmA : farmB; // the first cumulative weight past the draw, else the last
	}
	CHECK(calls.size() == (calls[0].result == 0 ? 3u : 2u)); // 0x203, the 0x20A only on 0, then 0x8B
	CHECK(active.squadTarget == expected->getID());
	// the caller's one-in-five production replacement draw comes last (the production list holds the fortress alone and it is within 275 of the base)
	CHECK(calls.back().file == "AIFarmKillSquad.cpp");
	CHECK(calls.back().line == 0x8B);
	CHECK(calls.back().lo == 0);
	CHECK(calls.back().hi == 4);

	// RW 0x9BB5E4's two extra filters: an open BLOCKING_GATE (its GateOpenAndCloseBehavior slot 0x18, isOpen RW 0x8DC9AB) and a WALL_SEGMENT without
	// CAN_ATTACK are skipped; the closed gate is the one production candidate left (the fortress is within 275 of the base)
	Object *openGate = fx.make("TestGate", fx.teamOf("H"));
	Object *closedGate = fx.make("TestGate", fx.teamOf("H"));
	Object *wall = fx.make("TestWallSegment", fx.teamOf("H"));
	REQUIRE(openGate);
	REQUIRE(closedGate);
	REQUIRE(wall);
	Coord3D openPos{ 1000.0f, 400.0f, 0.0f }, closedPos{ 300.0f, 900.0f, 0.0f }, wallPos{ 400.0f, 1000.0f, 0.0f };
	openGate->teleportTo(&openPos, true);
	closedGate->teleportTo(&closedPos, true);
	wall->teleportTo(&wallPos, true);
	GateOpenAndCloseBehavior::findGate(*closedGate)->close();
	REQUIRE(openGate->isKindOfName("BLOCKING_GATE"));
	GateOpenAndCloseBehavior *openBehavior = GateOpenAndCloseBehavior::findGate(*openGate);
	GateOpenAndCloseBehavior *closedBehavior = GateOpenAndCloseBehavior::findGate(*closedGate);
	REQUIRE(openBehavior != nullptr);
	REQUIRE(closedBehavior != nullptr);
	CHECK(openBehavior->isOpen());
	CHECK_FALSE(closedBehavior->isOpen());
	fx.logic->random().clearCallLog();
	CHECK(AITacticalAI::productionTargetForTest(*fx.logic, tac, enemy) == closedGate);
	{
		const auto &prod = fx.logic->random().callLog();
		REQUIRE(!prod.empty());
		CHECK(prod[0].file == "AIFarmKillSquad.cpp");
		CHECK(prod[0].line == 0x186);
		CHECK(prod[0].lo == 0);
		CHECK(prod[0].hi == 3);
	}

	// without a fortress RW 0x9BBA51 draws an id from the economy list (line 0x21D, 0 .. size - 1) and returns it
	for (Object *o : { fortress, openGate, closedGate, wall })
	{
		o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		manager.objectRemoved(*o);
	}
	REQUIRE(AITacticalAI::recordOf(*fx.logic, enemy).structures().empty());
	active.squadTarget = INVALID_ID;
	fx.logic->random().clearCallLog();
	AITacticalAI::updateBrain(*fx.logic, *ai, ai);
	const auto &after = fx.logic->random().callLog();
	REQUIRE(after.size() == 2); // the 0x21D id draw and the caller's 0x8B
	CHECK(after[0].file == "AIFarmKillSquad.cpp");
	CHECK(after[0].line == 0x21D);
	CHECK(after[0].lo == 0);
	CHECK(after[0].hi == 1);
	CHECK(active.squadTarget == (after[0].result == 0 ? farmA : farmB)->getID());
	CHECK(after.back().line == 0x8B);
}

TEST_CASE("skirmish ai WotR battle start: fixed base orders, exact RNG and save-load continuation (RW 0x9B8451 / 0x9B85AD / 0x9B8608)")
{
	for (bool skirmish : { true, false })
	for (bool hasFortress : { true, false })
	{
		CAPTURE(skirmish);
		CAPTURE(hasFortress);
		AIFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C", "FactionA", false, 1, 0, 1 } });
		if (!skirmish)
		{
			fx.logic->economy().context().gameKind = 0; // not a skirmish (GameLogic + 0x114 != 3)
		}
		pathtest::SyntheticTerrain terrain(100, 100);
		AIWorldConfig config;
		config.pathfind = pathtest::testConfig();
		AIWorld world(*fx.logic, config, fx.w.fx.env.macros);
		world.attach();
		world.newMap(terrain);
		LocomotorStore locomotors;
		struct LocomotorScope
		{
			GameLogic &logic;
			LocomotorStore *saved = TheLocomotorStore;
			~LocomotorScope()
			{
				logic.reset();
				TheLocomotorStore = saved;
			}
		} locomotorScope{ *fx.logic };
		TheLocomotorStore = &locomotors;
		fx.w.fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
		REQUIRE(fx.w.load(movetest::kLocomotors).empty());
		AIUpdateInterface::registerClasses(fx.w.modules);
		InvisibilityModules::registerAll(fx.w.modules);
		REQUIRE(fx.w.load("Object BattleUnit\n KindOf = CAN_ATTACK INFANTRY\n"
			" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
			" Behavior = AIUpdateInterface ModuleTag_AI\n AutoAcquireEnemiesWhenIdle = Yes\n End\n"
			" Behavior = StealthUpdate ModuleTag_Stealth\n StealthDelay = 10000\n End\n"
			" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n"
			"Object BattleHero\n KindOf = HERO\n"
			" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
			" Behavior = AIUpdateInterface ModuleTag_AI\n End\n"
			" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n"
			"Object BattleWorker\n KindOf = DOZER\n"
			" Body = ActiveBody ModuleTag_Body\n MaxHealth = 100\n InitialHealth = 100\n End\n"
			" Behavior = AIUpdateInterface ModuleTag_AI\n End\n"
			" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\nEnd\n"
			"Object BattleFortress\n KindOf = STRUCTURE DOZER_FACTORY\nEnd\n").empty());
		REQUIRE(fx.logic->skirmishAI().newGame(true) == 1);
		auto *ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
		REQUIRE(ai);
		const Coord3D base{ 100.0f, 100.0f, 37.0f };
		ai->build.startPosition = base;
		ai->brain.namedCounters["FarmKillSquad::IsRunning"] = 1;
		fx.players.findPlayerWithName("C")->setSkirmishDifficulty(1); // isolate WoTR from Easy-only roaming
		if (hasFortress)
		{
			Object *fortress = fx.make("BattleFortress", fx.teamOf("C"));
			REQUIRE(fortress);
			const Coord3D fortressPos{ 500.0f, 500.0f, 0.0f };
			fortress->setPosition(&fortressPos);
		}
		Object *unit = fx.make("BattleUnit", fx.teamOf("C"));
		Object *hero = fx.make("BattleHero", fx.teamOf("C"));
		Object *worker = fx.make("BattleWorker", fx.teamOf("C"));
		REQUIRE(unit);
		REQUIRE(hero);
		REQUIRE(worker);
		const ObjectID unitID = unit->getID(), heroID = hero->getID(), workerID = worker->getID();
		const Coord3D from{ 200.0f, 200.0f, 0.0f };
		for (Object *o : { unit, hero, worker }) o->setPosition(&from);
		unit->setStatus((unsigned)CombatNames::status("CAN_STEALTH"), true);
		for (int i = 0; i < 5; ++i) fx.logic->runLogicFrame();
		REQUIRE(fx.logic->getFrame() == 5);
		fx.logic->random().enableCallLog(true);
		fx.logic->random().clearCallLog();
		const auto frame5Seed = fx.logic->random().seedArray();
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(ai->brain.tactics.empty()); // RW 0x9B8451: frame > 5, even outside skirmish
		CHECK(ai->brain.namedCounters.at("StartWoTRBattleTacticHasRun") == 0);
		CHECK(fx.logic->random().callLog().empty());
		CHECK(fx.logic->random().seedArray() == frame5Seed);
		fx.logic->runLogicFrame();
		REQUIRE(fx.logic->getFrame() == 6);
		// A visible stealth-capable unit starts busy; command 0xF must replace the state without the AIGroup.cpp:0x874 draw.
		unit->getAIUpdateInterface()->aiBusy(CMD_FROM_PLAYER);
		REQUIRE(unit->getAIUpdateInterface()->isBusy());
		fx.logic->random().clearCallLog();
		auto expected = fx.logic->random();
		if (!skirmish) expected.getValue(0, 0, "AITacticsGenerator.cpp", 0x1BC);
		auto checkLog = [&](const GameLogicRandom &want) {
			const auto &got = fx.logic->random().callLog();
			const auto &calls = want.callLog();
			REQUIRE(got.size() == calls.size());
			CHECK(fx.logic->random().seedArray() == want.seedArray());
			for (size_t i = 0; i < calls.size(); ++i)
			{
				CHECK(got[i].file == calls[i].file);
				CHECK(got[i].line == calls[i].line);
				CHECK(got[i].real == calls[i].real);
				CHECK(got[i].lo == calls[i].lo);
				CHECK(got[i].hi == calls[i].hi);
				CHECK(got[i].rlo == calls[i].rlo);
				CHECK(got[i].rhi == calls[i].rhi);
				CHECK(got[i].result == calls[i].result);
				CHECK(got[i].rresult == calls[i].rresult);
				CHECK(got[i].drewNumber == calls[i].drewNumber);
			}
		};
		const int unportedBefore = ai->brain.targetlessUnported;
		AITacticalAI::updateBrain(*fx.logic, *ai, ai);
		CHECK(ai->brain.targetlessUnported - unportedBefore == 6);
		checkLog(expected); // exactly the ordinary pool draw; no A4, BA or group draw
		if (skirmish)
		{
			CHECK(ai->brain.tactics.empty());
			CHECK(ai->brain.namedCounters.at("StartWoTRBattleTacticHasRun") == 0);
			CHECK_FALSE(unit->getAIUpdateInterface()->attackMoveActive());
			CHECK_FALSE(hero->getAIUpdateInterface()->attackMoveActive());
			continue;
		}
		REQUIRE(ai->brain.tactics.size() == 1);
		CHECK(ai->brain.tactics[0].kind == "AIStartWoTRBattleTactic");
		CHECK_FALSE(ai->brain.tactics[0].started);
		auto reacquire = [&]() {
			ai = const_cast<AISkirmishPlayer *>(fx.logic->skirmishAI().findAI(fx.indexOf("C")));
			unit = fx.logic->findObjectByID(unitID);
			hero = fx.logic->findObjectByID(heroID);
			worker = fx.logic->findObjectByID(workerID);
			REQUIRE(ai);
			REQUIRE(unit);
			REQUIRE(hero);
			REQUIRE(worker);
		};
		// Save before/after the one-shot run; compare every following brain state, complete bytes and the entire draw log.
		auto roundTrip = [&](bool beforeRun) {
			std::string error;
			std::vector<std::uint8_t> saved, finalBytes;
			const auto savedHash = fx.logic->computeStateHash();
			REQUIRE_MESSAGE(fx.logic->saveSnapshot(saved, &error), error);
			fx.logic->random().clearCallLog();
			auto want = fx.logic->random();
			if (beforeRun) want.getValue(0, 0, "AITacticsGenerator.cpp", 0x1BC); // a second clone before the key is set
			std::vector<std::uint32_t> hashes;
			for (int pass = 0; pass < 2; ++pass)
			{
				if (pass)
				{
					REQUIRE_MESSAGE(fx.logic->loadSnapshot(saved, &error), error);
					reacquire();
					CHECK(fx.logic->computeStateHash() == savedHash);
					std::vector<std::uint8_t> again;
					REQUIRE_MESSAGE(fx.logic->saveSnapshot(again, &error), error);
					CHECK(again == saved);
				}
				fx.logic->random().clearCallLog();
				for (int step = 0; step < 2; ++step)
				{
					AITacticalAI::updateBrain(*fx.logic, *ai, ai);
					if (beforeRun && step == 0)
					{
						CHECK(ai->brain.tactics[0].ready);
						CHECK(ai->brain.tactics[0].readyFrame == 6);
						CHECK_FALSE(ai->brain.tactics[0].started); // RW 0x8F11DA records ready before running
					}
					const auto hash = fx.logic->computeStateHash();
					if (!pass) hashes.push_back(hash);
					CHECK(hash == hashes[(size_t)step]);
				}
				checkLog(want);
				std::vector<std::uint8_t> after;
				REQUIRE_MESSAGE(fx.logic->saveSnapshot(after, &error), error);
				if (!pass) finalBytes = after;
				CHECK(after == finalBytes);
			}
		};
		roundTrip(true);
		CHECK(ai->brain.namedCounters.at("StartWoTRBattleTacticHasRun") == 1);
		REQUIRE(!ai->brain.tactics.empty());
		CHECK(ai->brain.tactics[0].ended);
		for (Object *o : { unit, hero })
		{
			auto *u = o->getAIUpdateInterface();
			REQUIRE(u);
			CHECK(u->attackMoveActive());
			CHECK_FALSE(u->isBusy());
			CHECK(u->stateMachine().goalPosition().x == base.x);
			CHECK(u->stateMachine().goalPosition().y == base.y);
			CHECK(u->stateMachine().goalPosition().z == base.z);
			CHECK(u->lastCommandSource() == CMD_FROM_PLAYER);
			u->aiIdle(CMD_FROM_PLAYER);
		}
		CHECK_FALSE(worker->getAIUpdateInterface()->attackMoveActive());
		roundTrip(false); // the pending duplicate observes HasRun and ends without reissuing either order
		CHECK_FALSE(unit->getAIUpdateInterface()->attackMoveActive());
		CHECK_FALSE(hero->getAIUpdateInterface()->attackMoveActive());
		CHECK(ai->brain.tactics.empty());
		CHECK(ai->brain.namedCounters.at("StartWoTRBattleTacticHasRun") == 1);
	}
}
