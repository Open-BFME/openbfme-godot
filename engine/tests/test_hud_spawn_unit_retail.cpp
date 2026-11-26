// OpenBFME retail tests for SpawnUnitBehavior's update (RW 0x8BC9B0) on the Army of the Dead's egg (GondorArmyofTheDeadEgg): 2.01 has no
// Command_ConstructRohanOathbreakerHorde (commented out), so the one press of SpawnOnce builds nothing; a queue of two that starts with UnitName drops that
// entry. They share the HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpawnUnitBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ProductionSettings.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	CommandStore *savedStore;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	explicit Arena(SharedWorld &s)
		: context(s.world->enterContext())
		, savedStore(TheCommandStore)
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		TheCommandStore = &s.world->commands();
		SkirmishSetup setup;
		setup.startingMoney = 10000;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		REQUIRE_MESSAGE(ProductionSettings::load(*s.mount->fs, logic.productionSettings(), &err), err); // GameData's build times
		logic.random().seedRandom(7);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		logic.partition().setRegion(0.0f, 0.0f, 1200.0f, 1200.0f); // the terrain's extent (RW 0x62FCCD)
		logic.combat().setAutoAcquireEnabled(false);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
		TheCommandStore = savedStore;
	}
	Object *place(const char *templateName, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, players.findPlayerWithName("A")->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic.runLogicFrame();
		}
	}
	int count(const char *templateName)
	{
		int n = 0;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate()->getName() == templateName;
		}
		return n;
	}
};
} // namespace

TEST_CASE("spawn unit retail: the Army of the Dead's egg has no Command_ConstructRohanOathbreakerHorde in 2.01 (commented out), so its one press of SpawnOnce builds nothing")
{
	if (!hudtest::haveWorld("spawn unit retail"))
	{
		return;
	}
	Arena a(shared());
	Object *egg = a.place("GondorArmyofTheDeadEgg", 600.0f, 600.0f);
	SpawnUnitBehavior *m = dynamic_cast<SpawnUnitBehavior *>(egg->findModule("SpawnUnitBehavior"));
	REQUIRE(m);
	CHECK(m->spawnData()->m_unitName == "RohanOathbreakerHorde");
	CHECK(m->spawnData()->m_unitCommand == "Command_ConstructRohanOathbreakerHorde");
	CHECK(m->spawnData()->m_spawnOnce);
	CHECK(TheCommandStore->findCommandButton("Command_ConstructRohanOathbreakerHorde") == nullptr); // commandbutton.ini: the block is a comment
	ProductionUpdateInterface *queue = egg->getProductionUpdate();
	REQUIRE(queue);
	a.frames(5); // frames 0 .. 4: before logic frame 5
	CHECK(m->oncePressPending());
	a.frames(2);
	CHECK_FALSE(m->oncePressPending()); // the press is made without a button (RW 0x8BCAF7)
	a.frames(100);
	CHECK(queue->getProductionCount() == 0);
	CHECK(a.count("RohanOathbreakerHorde") == 0);
}

TEST_CASE("spawn unit retail: a queue of two that starts with UnitName drops that first entry; a queue of one stays (RW 0x8BCA3E .. 0x8BCA9D)")
{
	if (!hudtest::haveWorld("spawn unit retail"))
	{
		return;
	}
	Arena a(shared());
	Object *fortress = a.place("WildFortressCitadel", 600.0f, 600.0f);
	SpawnUnitBehavior *m = dynamic_cast<SpawnUnitBehavior *>(fortress->findModule("SpawnUnitBehavior"));
	REQUIRE(m);
	REQUIRE(m->spawnData()->m_unitName == "WildFortressFireDrake");
	CHECK(m->spawnData()->m_unitCommand.empty());
	ProductionUpdateInterface *queue = fortress->getProductionUpdate();
	REQUIRE(queue);
	const ThingTemplate *drake = shared().world->things().findTemplate("WildFortressFireDrake");
	REQUIRE(drake);
	// the update's frame at logic frame 5 or later: the two entries go in just before it
	a.frames(5); // the first update ran in frame 1 (before logic frame 5: nothing), the next is frame 6
	REQUIRE(a.logic.getFrame() == 5u);
	REQUIRE(m->friend_getNextCallFrame() == 6u);
	// the fortress offers the drake (S-207), so its own queue takes it
	REQUIRE(queue->queueCreateUnit(drake, -1, queue->requestUniqueUnitID(), -1, false, std::string(), false));
	REQUIRE(queue->queueCreateUnit(drake, -1, queue->requestUniqueUnitID(), -1, false, std::string(), false));
	const ProductionID first = queue->firstProduction()->productionID;
	REQUIRE(queue->getProductionCount() == 2);
	a.frames(1); // the update: the first entry is cancelled
	CHECK_FALSE(queue->isUnitInQueue(first));
	CHECK(queue->getProductionCount() == 1);
	CHECK(a.count("WildFortressFireDrake") == 0);
	a.frames(10);
	CHECK(queue->getProductionCount() == 1); // a queue of one stays
}
