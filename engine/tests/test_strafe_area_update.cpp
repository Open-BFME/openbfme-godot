// OpenBFME. GPL-3.0.
// StrafeAreaUpdate tests (both editions; RW 0x007A8368 / 0x008A3950, BFME2 0x0089F9F0).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/StrafeAreaUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kStrafeIni[] =
	"Weapon TestStrafeWeapon\n"
	"  AttackRange = 150.0\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    DamageType = FLAME\n"
	"    DeathType = BURNED\n"
	"  End\n"
	"End\n"
	"Object TestStrafeDragon\n"
	"  KindOf = AIRCRAFT SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"  Behavior = StrafeAreaUpdate StrafeTag\n"
	"    WeaponName = TestStrafeWeapon\n"
	"    StrafeAreaRadius = 150.0\n"
	"    Sweepfrequency = 0.4\n"
	"    SweepAmplitude = 100.0\n"
	"    Slope = 50.0\n"
	"    InitialSweepPhase = 0.0\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_PANIC\n"
	"    Speed = 90\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 StrafeAreaUpdate: initiateStrafe, approach->strafe transition, and xfer in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w(kStrafeIni);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *dragon = w.unit("TestStrafeDragon", 'A', 100.0f, 100.0f);
		REQUIRE(dragon != nullptr);
		auto *strafe = dynamic_cast<StrafeAreaUpdate *>(dragon->findModule("StrafeAreaUpdate"));
		REQUIRE(strafe != nullptr);

		const Coord3D from{ 100.0f, 100.0f, 0.0f };
		const Coord3D to{ 250.0f, 100.0f, 0.0f };
		REQUIRE(strafe->initiateStrafe(&from, &to));

		w.frames(2);
		// distance = 150 <= radius(150) + slope(50) -> transitions to state 1 (strafe)
		StateHasher h1;
		strafe->crc(h1);
		XferSave save;
		strafe->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		strafe->xfer(load);
		StateHasher h2;
		strafe->crc(h2);
		CHECK(h1.value() == h2.value());
	}
}
