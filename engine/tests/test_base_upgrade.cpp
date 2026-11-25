// OpenBFME unit tests: BaseUpgrade (fork issue 286; editions: both RotWK 2.01 and BFME2 1.06).
//
// Target facts and RotWK 2.01 / BFME2 1.06 addresses:
//   * Module registration: RW 0x65A28A (create RW 0x64FB99, createData RW 0x64FBD1) / DONOR BFME2 0x659074.
//   * Data ctor RW 0x8B7885 / DONOR BFME2 0x4B3698, buildFieldParse RW 0x8B7863 / DONOR BFME2 0x4B3670,
//     table RW 0xC6DEB8 / DONOR BFME2 0xB48288 (BuildingTemplateName +0x138, PlacementPrefix +0x13C, PlacementIndex +0x140).
//   * Runtime ctor RW 0x8B7885 / DONOR BFME2 0x4B371C, upgradeImplementation RW 0x8B7933 / DONOR BFME2 0x4B37A0 (BaseUpgrade.cpp:332),
//     xfer RW 0x8B7880 / DONOR BFME2 0x4B3640 (version 1, 1).

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BaseUpgrade.h"
#include "GameLogic/Object/Object.h"

namespace {

const char kBaseUpgradeIni[] =
	"Upgrade UpgradeTestMordorBase\n"
	"  Type = OBJECT\n"
	"End\n"
	"Object TestSpawningBase\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 30\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = BaseUpgrade ModuleTag_BaseUpgrade\n"
	"    TriggeredBy = UpgradeTestMordorBase\n"
	"    BuildingTemplateName = TestSpawnedBuilding\n"
	"    PlacementPrefix = BONE_\n"
	"    PlacementIndex = 0\n"
	"  End\n"
	"End\n"
	"Object TestSpawnedBuilding\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n";

struct BaseUpgradeRig
{
	combattest::CombatWorld world;

	BaseUpgradeRig()
		: world()
	{
		std::string err = world.w.load(kBaseUpgradeIni, INI_LOAD_OVERWRITE, "base_upgrade_test.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
};

} // namespace

TEST_CASE("BaseUpgrade: spawns building template upon upgrade grant and xfer round-trips (RW 0x8B7933)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		BaseUpgradeRig rig;
		rig.world.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		const auto *tmpl = rig.world.w.modules.findModuleTemplate("BaseUpgrade", MODULETYPE_BEHAVIOR);
		REQUIRE(tmpl != nullptr);
		CHECK(tmpl->portedModule);
		CHECK(tmpl->typed);

		Object *spawner = rig.world.unit("TestSpawningBase", 'A', 100.0f, 100.0f);
		REQUIRE(spawner != nullptr);

		auto *bu = dynamic_cast<BaseUpgrade *>(spawner->findModule("BaseUpgrade"));
		REQUIRE(bu != nullptr);
		CHECK(!bu->isAlreadyUpgraded());

		const UpgradeTemplate *upg = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("UpgradeTestMordorBase") : nullptr;
		REQUIRE(upg != nullptr);
		CHECK(!spawner->hasUpgrade(upg));

		spawner->giveUpgradeSelf(upg);
		CHECK(spawner->hasUpgrade(upg));
		CHECK(bu->isAlreadyUpgraded());

		bool foundSpawned = false;
		for (Object *o = rig.world.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "TestSpawnedBuilding")
			{
				foundSpawned = true;
				CHECK(o->getProducerID() == spawner->getID());
				CHECK(o->getPosition()->x == 100.0f);
				CHECK(o->getPosition()->y == 100.0f);
				CHECK(o->getControllingPlayer() == spawner->getControllingPlayer());
				break;
			}
		}
		CHECK(foundSpawned);

		XferSave save;
		bu->xfer(save);
		spawner->removeUpgrade(upg);
		CHECK_FALSE(bu->isAlreadyUpgraded());

		XferLoad load(save.data());
		bu->xfer(load);
		CHECK(bu->isAlreadyUpgraded());
	}
}
