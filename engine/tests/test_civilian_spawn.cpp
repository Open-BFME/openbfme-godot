// OpenBFME. GPL-3.0.
// CivilianSpawnUpdate + CivilianSpawnCollide tests (both editions; RW 0x882D5B / 0x8C1823, BFME2 0x47FAFE / 0x4BD6E0).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CivilianSpawnCollide.h"
#include "GameLogic/Module/CivilianSpawnUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kCivilianIni[] =
	"Object TestCivilianHouse\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = CivilianSpawnUpdate ModuleTag_CivilianSpawn\n"
	"    SpawnDelayTime = 100\n"
	"    MaximumDistance = 400.0\n"
	"    RunToFilter = NONE +STRUCTURE ALLIES\n"
	"    Civilian = TestFleeingCivilian\n"
	"  End\n"
	"End\n"
	"Object TestFleeingCivilian\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 15\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_PANIC\n"
	"    Speed = 70\n"
	"  End\n"
	"End\n"
	"Object TestVictimInfantry\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object TestCivilianDestination\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = CivilianSpawnCollide ModuleTag_CivilianCollide\n"
	"    DeleteObjectFilter = NONE +INFANTRY ALLIES\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("LOCAL-36: CivilianSpawnUpdate spawns panicking civilian and CivilianSpawnCollide deletes matching (RW 0x882D5B / 0x8C1823)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld world;
		world.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string err = world.w.load(kCivilianIni);
		REQUIRE_MESSAGE(err.empty(), err);

		Object *house = world.unit("TestCivilianHouse", 'A', 100.0f, 100.0f);
		Object *dest = world.unit("TestCivilianDestination", 'A', 200.0f, 100.0f);
		REQUIRE(house != nullptr);
		REQUIRE(dest != nullptr);

		auto *csu = dynamic_cast<CivilianSpawnUpdate *>(house->findModule("CivilianSpawnUpdate"));
		REQUIRE(csu != nullptr);
		CHECK(csu->spawnFrame() == 1);

		UpdateSleepTime sleep = csu->update();
		CHECK(sleep == (UpdateSleepTime)1);

		Object *spawned = nullptr;
		for (Object *o = world.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "TestFleeingCivilian")
			{
				spawned = o;
				break;
			}
		}
		REQUIRE(spawned != nullptr);
		static const int kPanicking = CombatNames::modelCondition("PANICKING");
		CHECK(spawned->testModelCondition(kPanicking));
		REQUIRE(spawned->getAIUpdateInterface() != nullptr);

		XferSave save1;
		csu->xfer(save1);
		XferLoad load1(save1.data());
		csu->xfer(load1);

		auto *csc = dynamic_cast<CivilianSpawnCollide *>(dest->findModule("CivilianSpawnCollide"));
		REQUIRE(csc != nullptr);
		CHECK(csc->getCollide() != nullptr);
		CHECK(!csc->wouldLikeToCollideWith(*dest));

		csc->onCollide(spawned, nullptr, nullptr);
		CHECK(spawned->isDestroyed());

		Object *enemy = world.unit("TestVictimInfantry", 'B', 200.0f, 100.0f);
		REQUIRE(enemy != nullptr);
		csc->onCollide(enemy, nullptr, nullptr);
		CHECK(!enemy->isDestroyed());

		XferSave save2;
		csc->xfer(save2);
		XferLoad load2(save2.data());
		csc->xfer(load2);
	}
}
