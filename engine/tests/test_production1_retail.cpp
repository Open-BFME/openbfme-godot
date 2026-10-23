// OpenBFME unit tests. GPL-3.0.
// Lane PRODUCTION1: what ProductionUpdate::update (RW 0x8A1B9F) does besides making the unit, on retail data in a started skirmish (LiveGame: the AIWorld,
// the experience world and the logic's bone provider are the game's own):
//   * the producer's experience: the BuildCost of every object it makes (RW 0x8A27F9 .. 0x8A283B) and the cost of every upgrade it researches (RW 0x8A2015 ..
//     0x8A2057), unless GiveNoXP; a building levels up from it (its ExperienceLevel chain grants its upgrades and modifiers). Most RotWK unit buildings have
//     GiveNoXP and level through a purchased LevelUpUpgrade; the Forbidden Pool, the fortresses, keeps, towers and mines earn;
//   * the progress rate: the producer's PRODUCTION modifiers (RW 0x8A1EBC: GondorBarracksBuildSpeedModLvl2 is GONDOR_BARRACKS_LVL2_BUILD_SPEED = 1.10);
//   * VeteranUnitsFromVeteranFactory (RW 0x8A27D4: one free level from a producer at rank 3 or more; the Mumakil pen);
//   * SpawnPointProductionExitUpdate (RW 0x8A7363 / 0x8A74B7): the Mordor barricade's archers stand on its ARCHER_ bones, held.
// Without the port the producer's experience stays 0, the counter grows by 1.0 a frame, the mumak has the pen's default level and the barricade stays empty.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Module/ProductionExitModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
Object *make(GameLogic &logic, const char *name, Player &owner, const Coord3D &at)
{
	const ThingTemplate *tt = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(tt, name);
	Object *o = logic.newObject(tt, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o, name);
	o->setPosition(&at);
	return o;
}

// the position of the player's first structure (its start), moved by (dx, dy)
Coord3D nearStart(GameLogic &logic, const Player &p, float dx, float dy)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &p && o->isKindOf((unsigned)CombatNames::kindOf("STRUCTURE")))
		{
			return Coord3D{ o->getPosition()->x + dx, o->getPosition()->y + dy, o->getPosition()->z };
		}
	}
	FAIL("the player has no structure");
	return Coord3D{};
}

int buildCost(const ThingTemplate &tt)
{
	const FieldValue *v = tt.getFinalOverride()->findField("BuildCost");
	return v ? (int)std::get<long long>(*v) : 0;
}

void run(GameLogic &logic, int frames)
{
	for (int i = 0; i < frames; ++i)
	{
		logic.runLogicFrame();
	}
}

// the objects `producer` has made, in creation order
std::vector<Object *> madeBy(GameLogic &logic, const Object &producer)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == producer.getID())
		{
			out.push_back(o);
		}
	}
	return out;
}

Player &human(buildtest::Game &g)
{
	Player *p = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	REQUIRE(p);
	return *p;
}
} // namespace

TEST_CASE("production1 retail: the Forbidden Pool earns the BuildCost of what it trains and levels to rank 3 (RW 0x8A27F9 .. 0x8A283B)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player &men = human(g);
	// GondorForbiddenPool: a ProductionUpdate without GiveNoXP (forbiddenpool.ini) and the levels GondorForbiddenPool / 2 / 3 at RequiredExperience 1 / 2 / 3
	// (experiencelevels.ini). Most RotWK unit buildings (GondorBarracks, ...) have GiveNoXP = Yes and level through their purchased LevelUpUpgrade instead
	Object *pool = make(logic, "GondorForbiddenPool", men, nearStart(logic, men, 400.0f, 0.0f));
	const ObjectID poolId = pool->getID();
	ExperienceTracker *xp = pool->getExperienceTracker();
	REQUIRE(xp);
	CHECK(xp->getRank() == 1);
	const float before = xp->getExperience();
	ProductionUpdateInterface *pu = pool->getProductionUpdate();
	REQUIRE(pu);
	const ThingTemplate *horde = logic.things().findTemplate("GondorRangerHorde");
	REQUIRE(horde);
	men.getMoney()->deposit(10000u, false);
	pool->giveUpgrade("Upgrade_GondorArcheryRangeLevel2"); // Command_ConstructGondorRangerHorde's NeededUpgrade (commandbutton.ini)
	REQUIRE(pu->queueCreateUnit(horde, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	for (int i = 0; i < 1000 && pu->getProductionCount() != 0; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(pu->getProductionCount() == 0);
	run(logic, 2); // the delayed level grant runs in the frame's phase 5
	pool = logic.findObjectByID(poolId);
	REQUIRE(pool);
	const std::vector<Object *> made = madeBy(logic, *pool);
	REQUIRE_FALSE(made.empty());
	int expected = 0;
	for (const Object *o : made)
	{
		expected += buildCost(*static_cast<const ThingTemplate *>(o->getTemplate()));
	}
	std::printf("production1: GondorForbiddenPool made %d objects, BuildCost sum %d, experience %.1f -> %.1f, rank %d\n", (int)made.size(), expected, (double)before,
		(double)xp->getExperience(), xp->getRank());
	// MultiPlayBuildingXPMult is 1.0 for every player count in 2.01's GameData; a STRUCTURE's points are not scaled by its rank (RW 0x79D4B9)
	CHECK(xp->getExperience() == before + (float)expected);
	CHECK(expected >= 3); // tolerance: GondorForbiddenPool3's RequiredExperience 3, the threshold the next checks rely on
	CHECK(xp->getRank() == 3);
	CHECK(pool->hasUpgrade("Upgrade_StructureLevel3")); // GondorForbiddenPool3's Upgrades
}

TEST_CASE("production1 retail: a Gondor barracks levelled by Upgrade_GondorBarracksLevel2 trains at GONDOR_BARRACKS_LVL2_BUILD_SPEED; its GiveNoXP keeps its experience (RW 0x8A1EBC)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 15, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player &men = human(g);
	Object *barracks = make(logic, "GondorBarracks", men, nearStart(logic, men, 400.0f, 0.0f));
	ExperienceTracker *xp = barracks->getExperienceTracker();
	REQUIRE(xp);
	barracks->giveUpgrade("Upgrade_GondorBarracksLevel2"); // what Command_PurchaseUpgradeGondorBarracksLevel2 researches: its LevelUpUpgrade reaches GondorBarracksLevel2
	run(logic, 2);
	REQUIRE(xp->getRank() == 2);
	const float before = xp->getExperience();
	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu);
	const ThingTemplate *horde = logic.things().findTemplate("GondorFighterHorde");
	REQUIRE(horde);
	men.getMoney()->deposit(10000u, false);
	REQUIRE(pu->queueCreateUnit(horde, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	// GondorBarracksBuildSpeedModLvl2: Modifier = PRODUCTION GONDOR_BARRACKS_LVL2_BUILD_SPEED (1.10, gamedata.ini): the counter adds 1.1f a frame (ADDSS)
	const int frames = 10;
	run(logic, frames);
	REQUIRE(pu->firstProduction());
	volatile float counter = 0.0f;
	for (int i = 0; i < frames; ++i)
	{
		counter = counter + 1.10f;
	}
	CHECK(pu->firstProduction()->framesUnderConstruction == (float)counter);
	for (int i = 0; i < 600 && pu->getProductionCount() != 0; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(pu->getProductionCount() == 0);
	CHECK(xp->getExperience() == before); // GiveNoXP = Yes (barracks.ini)
}

TEST_CASE("production1 retail: researching Upgrade_MenFortressBanners gives the Men fortress the upgrade's 500 as experience (RW 0x8A2015 .. 0x8A2057)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 12, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player &men = human(g);
	Object *fortress = make(logic, "MenFortressCitadel", men, nearStart(logic, men, 700.0f, 0.0f));
	ExperienceTracker *xp = fortress->getExperienceTracker();
	REQUIRE(xp);
	const float before = xp->getExperience();
	REQUIRE(TheUpgradeCenter);
	const UpgradeTemplate *banners = TheUpgradeCenter->findUpgrade("Upgrade_MenFortressBanners");
	REQUIRE(banners);
	men.getMoney()->deposit(10000u, false);
	ProductionUpdateInterface *pu = fortress->getProductionUpdate();
	REQUIRE(pu);
	REQUIRE(pu->queueUpgrade(banners));
	for (int i = 0; i < 2000 && pu->getProductionCount() != 0; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(pu->getProductionCount() == 0);
	std::printf("production1: Upgrade_MenFortressBanners BuildCost %d, fortress experience %.1f -> %.1f\n", banners->getBuildCost(), (double)before, (double)xp->getExperience());
	CHECK(banners->getBuildCost() == 500); // upgrade.ini
	CHECK(xp->getExperience() == before + 500.0f);
	CHECK(fortress->hasUpgrade(banners));
}

TEST_CASE("production1 retail: a Mumakil pen at rank 3 (VeteranUnitsFromVeteranFactory, GiveNoXP) makes its mumak one level higher and earns nothing (RW 0x8A27D4)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMordor", "FactionMen", 13, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player &mordor = human(g);
	const ThingTemplate *mumak = logic.things().findTemplate("MordorMumakil");
	REQUIRE(mumak);
	// a mumak made directly: the template's own starting rank
	Object *plain = make(logic, "MordorMumakil", mordor, nearStart(logic, mordor, -600.0f, 0.0f));
	REQUIRE(plain->getExperienceTracker());
	const int plainRank = plain->getExperienceTracker()->getRank();
	Object *pen = make(logic, "MordorMumakilPen", mordor, nearStart(logic, mordor, 500.0f, 0.0f));
	const ObjectID penId = pen->getID();
	ExperienceTracker *xp = pen->getExperienceTracker();
	REQUIRE(xp);
	REQUIRE(xp->getRank() == 1);
	xp->gainLevels(2, false); // MordorMumakilPenLevel2, Level3
	REQUIRE(xp->getRank() == 3);
	run(logic, 2);
	const float penXp = xp->getExperience();
	mordor.getMoney()->deposit(20000u, false);
	ProductionUpdateInterface *pu = pen->getProductionUpdate();
	REQUIRE(pu);
	REQUIRE(pu->queueCreateUnit(mumak, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	for (int i = 0; i < 2000 && pu->getProductionCount() != 0; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(pu->getProductionCount() == 0);
	pen = logic.findObjectByID(penId);
	REQUIRE(pen);
	const std::vector<Object *> made = madeBy(logic, *pen);
	REQUIRE(made.size() == 1);
	REQUIRE(made[0]->getExperienceTracker());
	std::printf("production1: a mumak from a rank 3 pen has rank %d (a plain one %d); the pen's experience %.1f -> %.1f\n", made[0]->getExperienceTracker()->getRank(), plainRank,
		(double)penXp, (double)xp->getExperience());
	CHECK(made[0]->getExperienceTracker()->getRank() == plainRank + 1);
	CHECK(xp->getExperience() == penXp); // GiveNoXP = Yes (mumakilpen.ini)
}

TEST_CASE("production1 retail: the Mordor barricade's four archers stand on its ARCHER_ bones, held there (SpawnPointProductionExitUpdate RW 0x8A7363 / 0x8A74B7)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMordor", "FactionMen", 14, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player &mordor = human(g);
	REQUIRE(logic.combat().launchOffsets()); // LiveGame installs the bone provider
	Coord3D at = nearStart(logic, mordor, 350.0f, 150.0f);
	Object *barricade = make(logic, "MordorBarricade", mordor, at);
	barricade->setOrientation(0.5f);
	const ObjectID id = barricade->getID();
	const SpawnBehavior *spawn = dynamic_cast<const SpawnBehavior *>(SpawnBehaviorInterface::of(*barricade));
	REQUIRE(spawn);
	run(logic, 20);
	barricade = logic.findObjectByID(id);
	REQUIRE(barricade);
	const SpawnPointProductionExitUpdate *exit = dynamic_cast<const SpawnPointProductionExitUpdate *>(barricade->findModule("SpawnPointProductionExitUpdate"));
	REQUIRE(exit);
	CHECK(exit->bonesInitialized());
	// SpawnNumber = InitialBurst = 4 (barricade.ini), one archer a bone
	REQUIRE(spawn->spawns().size() == 4);
	CHECK(exit->spawnPointCount() == 4);
	// the bones independently: the provider's ARCHER_01.. in model space through the barricade's transform (double here, float SSE in the port)
	float bones[10][12];
	const int count = logic.combat().launchOffsets()->multiLogicalBones(*barricade, "ARCHER_", barricade->getModelConditionBits(), 10, bones);
	CHECK(count == 4);
	const float *t = barricade->getBasis();
	const Coord3D &p = *barricade->getPosition();
	std::vector<bool> used(4, false);
	for (ObjectID sid : spawn->spawns())
	{
		const Object *archer = logic.findObjectByID(sid);
		REQUIRE(archer);
		INFO(archer->getTemplate()->getName());
		CHECK(archer->getTemplate()->getName() == "MordorArcherBarricade_Slaved");
		CHECK(archer->isDisabledByType(DISABLED_HELD));
		int match = -1;
		for (int i = 0; i < count; ++i)
		{
			const double wx = (double)t[0] * bones[i][3] + (double)t[1] * bones[i][7] + (double)t[2] * bones[i][11] + p.x;
			const double wy = (double)t[3] * bones[i][3] + (double)t[4] * bones[i][7] + (double)t[5] * bones[i][11] + p.y;
			const double angle = std::atan2((double)t[3] * bones[i][0] + (double)t[4] * bones[i][4] + (double)t[5] * bones[i][8],
				(double)t[0] * bones[i][0] + (double)t[1] * bones[i][4] + (double)t[2] * bones[i][8]);
			const bool here = std::fabs(wx - archer->getPosition()->x) < 1e-3 && std::fabs(wy - archer->getPosition()->y) < 1e-3; // tolerance: the port's float SSE order vs this double model
			if (here && !used[(size_t)i])
			{
				used[(size_t)i] = true;
				match = i;
				CHECK(std::fabs(angle - (double)archer->getOrientation()) < 1e-5); // tolerance: as above
			}
		}
		CHECK(match != -1); // tolerance: some bone holds this archer (which one is the spawn order)
	}
	std::printf("production1: MordorBarricade spawn points %d, archers %d\n", exit->spawnPointCount(), (int)spawn->spawns().size());
}
