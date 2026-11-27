// OpenBFME retail tests for SpecialEnemySenseUpdate (RW 0x89D7E6): Frodo's Sting (SPECIAL_ENEMY_NEAR) near an orc of an enemy. They share the HUD tests'
// retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialEnemySenseUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

const int kNear = 0x102;

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	explicit Arena(SharedWorld &s)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
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
	}
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, players.findPlayerWithName(side == 0 ? "A" : "B")->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
	// Frodo's condition after one scan interval
	bool seen(Object *frodo, unsigned interval)
	{
		for (unsigned i = 0; i < interval; ++i)
		{
			logic.runLogicFrame();
		}
		return frodo->testModelCondition(kNear);
	}
};
} // namespace

TEST_CASE("special enemy retail: Sting (SPECIAL_ENEMY_NEAR) shows an enemy orc within 200 of Frodo, not a troll, an orc of the own side or one further away")
{
	if (!hudtest::haveWorld("special enemy retail"))
	{
		return;
	}
	Arena a(shared());
	Object *frodo = a.place("RohanFrodo", 0, 600.0f, 600.0f);
	const SpecialEnemySenseUpdate *sense = dynamic_cast<SpecialEnemySenseUpdate *>(frodo->findModule("SpecialEnemySenseUpdate"));
	REQUIRE(sense);
	CHECK(sense->data()->m_scanRange == 200.0f);
	const unsigned interval = sense->data()->m_scanInterval;
	REQUIRE(interval == 10u); // 2000 ms
	a.logic.runLogicFrame();
	CHECK_FALSE(a.seen(frodo, interval));
	Object *own = a.place("MordorFighter", 0, 650.0f, 600.0f);
	Object *troll = a.place("MordorMountainTroll", 1, 600.0f, 650.0f);
	CHECK_FALSE(a.seen(frodo, interval));
	a.logic.destroyObject(own);
	a.logic.destroyObject(troll);
	Object *orc = a.place("MordorFighter", 1, 750.0f, 600.0f);
	CHECK(a.seen(frodo, interval));
	const Coord3D away{ 1000.0f, 600.0f, 0.0f };
	orc->setPosition(&away);
	CHECK_FALSE(a.seen(frodo, interval));
}
