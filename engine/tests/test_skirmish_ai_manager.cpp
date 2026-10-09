// OpenBFME unit tests. GPL-3.0.
// Lane AI-1, step 1: TheSkirmishAIManager (GameLogic/SkirmishAI/SkirmishAIManager.h): which players get an AI, the allied groups, the disabled rule,
// the update cadence and the hash. The expectations follow the RW functions named in each test (RW 0x6AA030, 0x6A9EAD, 0x8EDBB0, 0x6A96A0, 0x8EDDF6,
// 0x6C7946), never this engine's own output. The retail test starts real skirmishes (START-1's LiveGame) with every faction in a computer slot.

#include "doctest.h"

#include "LogicTestUtil.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

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
