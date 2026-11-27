// OpenBFME. GPL-3.0.
// DetachableRiderUpdate + DetachableRiderBody tests (both editions; RW 0x8B2D59 / 0x8C5EF3, BFME2 0x4AEC9B / 0x4C1D49).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/DetachableRiderUpdate.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kRiderIni[] =
	"Object TestMountedKnight\n"
	"  KindOf = CAVALRY SELECTABLE\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = DetachableRiderBody BodyTag\n"
	"    MaxHealth = 200\n"
	"    StartsActive = Yes\n"
	"    HealthPercentageWhenRiderDies = 50%\n"
	"    RiderlessDeathChance = 0%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"  Behavior = DetachableRiderUpdate RiderTag\n"
	"    DeathEntry = AnimState:DYING AnimTime:400 RiderOCL:None\n"
	"    RiderSubObjects = RIDER_MESH\n"
	"    RiderlessWeaponSlot = SECONDARY\n"
	"    RiderlessHordeFlees = No\n"
	"    RemoveRiderlessFromHorde = No\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("LOCAL-24 DetachableRider: lethal hit clamps health, kills rider, enters riderless state, restores, and round-trips xfer")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w(kRiderIni);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *knight = w.unit("TestMountedKnight", 'A', 200.0f, 200.0f);
		REQUIRE(knight != nullptr);
		auto *body = dynamic_cast<DetachableRiderBody *>(knight->getBodyModule());
		auto *rider = dynamic_cast<DetachableRiderUpdate *>(knight->findModule("DetachableRiderUpdate"));
		REQUIRE(body != nullptr);
		REQUIRE(rider != nullptr);

		CHECK(body->getHealth() == 200.0f);
		CHECK_FALSE(rider->isRiderDead());

		// Lethal hit clamps health to 50% (100.0f) and kills the rider
		DamageInfo dmg;
		dmg.m_input.m_amount = 500.0f;
		dmg.m_input.m_damageType = DAMAGE_SLASH;
		dmg.m_input.m_deathType = DEATH_NORMAL;
		knight->attemptDamage(dmg);

		CHECK_FALSE(knight->isEffectivelyDead());
		CHECK(body->getHealth() == 100.0f);
		CHECK(rider->isRiderDead());

		// After AnimTime (400ms = 2 frames), update runs and sets OBJECT_STATUS_RIDERLESS (39) and MODELCONDITION_RIDERLESS (297)
		w.frames(4);
		CHECK(knight->testStatus(39));
		CHECK(knight->testModelCondition(297));

		// xfer round-trip for both body and update
		StateHasher h1;
		body->crc(h1);
		rider->crc(h1);
		XferSave save;
		body->xfer(save);
		rider->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		body->xfer(load);
		rider->xfer(load);
		StateHasher h2;
		body->crc(h2);
		rider->crc(h2);
		CHECK(h1.value() == h2.value());

		// Restore rider clears riderless state
		rider->restoreRider();
		w.frames(2);
		CHECK_FALSE(rider->isRiderDead());
		CHECK_FALSE(knight->testStatus(39));
		CHECK_FALSE(knight->testModelCondition(297));
	}
}

TEST_CASE("LOCAL-24 DetachableRider xfer: v2 round-trip, v1 UnportedUpdateModule/UnportedBehaviorModule compatibility, and v3 rejection")
{
	combattest::CombatWorld w(kRiderIni);
	Object *knight = w.unit("TestMountedKnight", 'A', 200.0f, 200.0f);
	REQUIRE(knight != nullptr);
	auto *body = dynamic_cast<DetachableRiderBody *>(knight->getBodyModule());
	auto *rider = dynamic_cast<DetachableRiderUpdate *>(knight->findModule("DetachableRiderUpdate"));
	REQUIRE(body != nullptr);
	REQUIRE(rider != nullptr);

	// Trigger lethal hit so both body and rider are non-default
	DamageInfo dmg;
	dmg.m_input.m_amount = 500.0f;
	dmg.m_input.m_damageType = DAMAGE_SLASH;
	dmg.m_input.m_deathType = DEATH_NORMAL;
	knight->attemptDamage(dmg);
	REQUIRE(body->getHealth() == 100.0f);
	REQUIRE(body->isAlreadyUpgraded());
	REQUIRE(rider->isRiderDead());

	// DetachableRiderUpdate: v2 round-trip, v3 rejection, and real v1 UnportedUpdateModule load
	{
		XferSave v2Save;
		rider->xfer(v2Save);
		REQUIRE(!v2Save.data().empty());
		CHECK(v2Save.data().front() == 2);
		XferLoad v2Load(v2Save.data());
		rider->xfer(v2Load);
		CHECK(v2Load.atEnd());
		CHECK(rider->isRiderDead());

		std::vector<std::uint8_t> badVer = v2Save.data();
		badVer.front() = 3;
		XferLoad badLoad(badVer);
		CHECK_THROWS_AS(rider->xfer(badLoad), XferException);

		UnportedUpdateModule legacy(knight, nullptr, 1);
		legacy.update();
		XferSave v1Save;
		legacy.xfer(v1Save);
		REQUIRE(!v1Save.data().empty());
		CHECK(v1Save.data().front() == 1);

		XferLoad v1Load(v1Save.data());
		rider->xfer(v1Load);
		CHECK(v1Load.atEnd());
		CHECK_FALSE(rider->isRiderDead());
		CHECK_FALSE(rider->whichDeathEntry());
	}

	// DetachableRiderBody: v2 round-trip, v3 rejection, and real v1 UnportedBehaviorModule load
	{
		XferSave v2Save;
		body->xfer(v2Save);
		REQUIRE(!v2Save.data().empty());
		CHECK(v2Save.data().front() == 2);
		XferLoad v2Load(v2Save.data());
		body->xfer(v2Load);
		CHECK(v2Load.atEnd());
		CHECK(body->getHealth() == 100.0f);
		CHECK(body->isAlreadyUpgraded());

		std::vector<std::uint8_t> badVer = v2Save.data();
		badVer.front() = 3;
		XferLoad badLoad(badVer);
		CHECK_THROWS_AS(body->xfer(badLoad), XferException);

		UnportedBehaviorModule legacy(knight, nullptr, 32);
		XferSave v1Save;
		legacy.xfer(v1Save);
		REQUIRE(!v1Save.data().empty());
		CHECK(v1Save.data().front() == 1);

		XferLoad v1Load(v1Save.data());
		body->xfer(v1Load);
		CHECK(v1Load.atEnd());
		CHECK(body->getHealth() == 200.0f);
		CHECK_FALSE(body->isAlreadyUpgraded());
	}
}
