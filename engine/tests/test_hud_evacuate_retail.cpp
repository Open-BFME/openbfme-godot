// OpenBFME retail tests for EvacuateDamage (RW 0x8BF12E): the Mordor catapult's severed heads (MordorCatapultHumanHeads, its SECONDARY weapon) clear a battle
// tower of its garrison, its rocks do not. They share the HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when
// ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EvacuateDamage.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

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
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;
	GameLogicDispatch dispatch;

	explicit Arena(SharedWorld &s)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
		, dispatch(logic)
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
		GarrisonCommands::registerHandlers(dispatch);
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
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		testutil::completeHorde(o);
		return o;
	}
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic.runLogicFrame();
		}
	}
};

struct Bombardment
{
	std::string weapon;      // the catapult's current weapon when the tower lost health
	int framesToDamage = -1; // the first frame the tower lost health
	int framesToExit = -1;   // the first frame the horde was out (the tower standing, the horde alive)
	float lost = 0.0f;
};

// a Men battle tower with an archer horde inside under fire of a Mordor catapult with its weapon `slot`
Bombardment bombard(int slot, int frames)
{
	Arena a(shared());
	Object *tower = a.place("GondorBattleTower", 0, 600.0f, 600.0f);
	REQUIRE(dynamic_cast<EvacuateDamage *>(tower->findModule("EvacuateDamage")));
	CHECK(dynamic_cast<EvacuateDamage *>(tower->findModule("EvacuateDamage"))->data()->m_weaponThatCausesEvacuation == "MordorCatapultHumanHeads");
	Object *archers = a.place("GondorArcherHorde", 0, 660.0f, 600.0f);
	a.run(5);
	GameMessage enter(MSG_ENTER, a.players.findPlayerWithName("A")->getPlayerIndex());
	a.players.findPlayerWithName("A")->selection() = { archers->getID() };
	enter.appendObjectIDArgument(archers->getID());
	enter.appendObjectIDArgument(tower->getID());
	a.dispatch.dispatch(enter);
	for (int f = 0; f < 600 && !(archers->getContainedBy() == tower && archers->getContain()->getHordeContainInterface()->allMembersEntered()); ++f)
	{
		a.run(1);
	}
	REQUIRE(archers->getContainedBy() == tower);
	Object *catapult = a.place("MordorCatapult", 1, 600.0f, 1000.0f);
	a.run(2);
	REQUIRE(ObjectWeapons::setObjectWeaponLock(*catapult, slot, LOCKED_PERMANENTLY)); // the weapon toggle's lock
	REQUIRE(catapult->getAIUpdateInterface()->aiAttackObject(tower, CMD_FROM_PLAYER));
	Bombardment b;
	const ObjectID catapultId = catapult->getID();
	const ObjectID towerId = tower->getID(), hordeId = archers->getID();
	const float health = tower->getBodyModule()->getHealth();
	for (int f = 0; f < frames && b.framesToExit < 0; ++f)
	{
		a.run(1);
		Object *c = a.logic.findObjectByID(catapultId);
		REQUIRE(c);
		if (!a.logic.findObjectByID(towerId) || tower->isEffectivelyDead() || !a.logic.findObjectByID(hordeId) || archers->isEffectivelyDead())
		{
			break; // a destroyed tower ejects its garrison (EjectPassengersOnDeath, RW 0x867120); the rocks can kill the archers inside
		}
		b.lost = health - tower->getBodyModule()->getHealth();
		if (b.framesToDamage < 0 && b.lost > 0.0f)
		{
			b.framesToDamage = f;
			b.weapon = c->getWeapons()->currentWeapon()->getTemplate()->getName();
		}
		if (!archers->getContainedBy())
		{
			b.framesToExit = f;
		}
	}
	return b;
}
} // namespace

TEST_CASE("evacuate retail: the Mordor catapult's heads clear a battle tower, its rocks do not (EvacuateDamage, RW 0x8BF12E)")
{
	if (!hudtest::haveWorld("evacuate retail"))
	{
		return;
	}
	const Bombardment heads = bombard(SECONDARY_WEAPON, 600);
	MESSAGE("heads: " << heads.weapon << " hit after " << heads.framesToDamage << " frames (" << heads.lost << " damage), the horde out after " << heads.framesToExit);
	CHECK(heads.weapon == "MordorCatapultHumanHeads"); // empty: the tower was never hit
	CHECK(heads.framesToExit == heads.framesToDamage + 1);
	const Bombardment rocks = bombard(PRIMARY_WEAPON, 200);
	MESSAGE("rocks: " << rocks.weapon << " hit after " << rocks.framesToDamage << " frames (" << rocks.lost << " damage)");
	CHECK(rocks.weapon == "MordorCatapultRock");
	CHECK(rocks.framesToExit == -1);
}
