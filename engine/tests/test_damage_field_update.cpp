// OpenBFME. GPL-3.0.
// DamageFieldUpdate tests (both editions; RW 0x8949A2 / BFME2 0x491316).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DamageFieldUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kDamageFieldIni[] =
	"Upgrade UpgradeTestDamageField\n"
	"  Type = OBJECT\n"
	"End\n"
	"Weapon TestDamageFieldWeapon\n"
	"  AttackRange = 100.0\n"
	"  DamageNugget\n"
	"    Damage = 25\n"
	"    DamageType = MAGIC\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Object TestDamageFieldStructure\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = DamageFieldUpdate ModuleTag_DamageField\n"
	"    Radius = 150.0\n"
	"    RequiredUpgrade = UpgradeTestDamageField\n"
	"    ObjectFilter = NONE +INFANTRY -STRUCTURE ENEMIES\n"
	"    FireWeaponNugget\n"
	"      WeaponName = TestDamageFieldWeapon\n"
	"      FireDelay = 0\n"
	"      OneShot = No\n"
	"    End\n"
	"  End\n"
	"End\n"
	"Object TestVictimInfantry\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("LOCAL-36: DamageFieldUpdate requires upgrade, hits entering enemies, clears exited (RW 0x8949A2)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld world;
		world.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		const std::string err = world.w.load(kDamageFieldIni);
		REQUIRE_MESSAGE(err.empty(), err);

		Object *fortress = world.unit("TestDamageFieldStructure", 'A', 200.0f, 200.0f);
		REQUIRE(fortress != nullptr);
		auto *df = dynamic_cast<DamageFieldUpdate *>(fortress->findModule("DamageFieldUpdate"));
		REQUIRE(df != nullptr);

		Object *enemy = world.unit("TestVictimInfantry", 'B', 250.0f, 200.0f);
		REQUIRE(enemy != nullptr);
		auto *enemyBody = dynamic_cast<ActiveBody *>(enemy->getBodyModule());
		REQUIRE(enemyBody != nullptr);
		CHECK(enemyBody->getHealth() == 100.0f);

		UpdateSleepTime sleep = df->update();
		CHECK(sleep == UPDATE_SLEEP_NONE);
		CHECK(enemyBody->getHealth() == 100.0f);
		CHECK(df->victims().empty());

		const UpgradeTemplate *upg = TheUpgradeCenter->findUpgrade("UpgradeTestDamageField");
		REQUIRE(upg != nullptr);
		fortress->giveUpgradeSelf(upg);
		CHECK(fortress->hasUpgrade(upg));

		world.frames(1);
		CHECK(enemyBody->getHealth() == 75.0f);
		CHECK(df->victims().size() == 1);
		CHECK(df->victims()[0] == enemy->getID());

		world.frames(1);
		CHECK(enemyBody->getHealth() == 75.0f);

		Coord3D farPos{ 450.0f, 200.0f, 0.0f };
		enemy->setPosition(&farPos);
		world.frames(1);
		CHECK(df->victims().empty());

		Coord3D nearPos{ 250.0f, 200.0f, 0.0f };
		enemy->setPosition(&nearPos);
		world.frames(1);
		CHECK(enemyBody->getHealth() == 50.0f);
		CHECK(df->victims().size() == 1);

		Object *ally = world.unit("TestVictimInfantry", 'A', 240.0f, 200.0f);
		REQUIRE(ally != nullptr);
		auto *allyBody = dynamic_cast<ActiveBody *>(ally->getBodyModule());
		REQUIRE(allyBody != nullptr);
		world.frames(1);
		CHECK(allyBody->getHealth() == 100.0f);

		StateHasher h1;
		df->crc(h1);
		XferSave save;
		df->xfer(save);
		XferLoad load(save.data());
		df->xfer(load);
		StateHasher h2;
		df->crc(h2);
		CHECK(h1.value() == h2.value());

	}
}
