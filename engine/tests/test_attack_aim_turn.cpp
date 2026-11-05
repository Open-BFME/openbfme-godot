// OpenBFME unit tests. GPL-3.0.
// The attack aim's success test (RW 0x75232F, BFME2 0x3508C5): a residual angle inside the weapon's aim delta OR inside half the current locomotor's turn
// rate (RW 0x5E372D) ends the aim. Synthetic data only.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Weapon.h"

#include <cmath>

using namespace combattest;

TEST_CASE("attack aim: a residual angle inside half the locomotor's turn rate ends the aim and the weapon fires (RW 0x75232F, RW 0x5E372D)")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *archer = w.unit("Archer", 'A', 300.0f, 300.0f);
	Object *victim = w.unit("Swordsman", 'B', 400.0f, 300.0f);
	w.frames(2);
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	Locomotor *loco = ai->curLocomotor();
	REQUIRE(loco != nullptr);
	const float turnRate = loco->getMaxTurnRate(ai->locomotorHost());
	REQUIRE(turnRate > 0.0f); // tolerance: the fixture's locomotor sets it and the residual is derived from it, so the test does not pin the locomotor's rate
	// a target that circles the archer at 0.3 of a turn per frame: inside half a turn, so retail's second test aims at it, while the first test (the
	// weapon's aim delta) never does
	const float residual = SimMath::mulf32(turnRate, 0.3f);
	ObjectWeapons *weapons = archer->getWeapons();
	REQUIRE(weapons != nullptr);
	const Weapon *weapon = weapons->currentWeapon();
	REQUIRE(weapon != nullptr);
	REQUIRE(weapons->aimDelta() < residual); // tolerance: the bow has no AcceptableAimDelta, so the aim-delta test alone can never succeed here
	REQUIRE(ai->aiAttackObject(victim, CMD_FROM_AI));
	const unsigned firstShot = weapon->lastFireFrame();
	int firedAt = -1;
	for (int f = 0; f < 60 && firedAt < 0; ++f)
	{
		const Coord3D here = *archer->getPosition();
		const float angle = SimMath::addf32(archer->getOrientation(), residual);
		Coord3D there{ SimMath::addf32(here.x, SimMath::mulf32(100.0f, std::cos(angle))), SimMath::addf32(here.y, SimMath::mulf32(100.0f, std::sin(angle))), here.z };
		victim->setPosition(&there);
		w.frames(1);
		if (weapon->lastFireFrame() > firstShot)
		{
			firedAt = f;
		}
	}
	MESSAGE("turn rate " << turnRate << " rad/frame, residual " << residual << ", aim delta " << weapons->aimDelta() << ", first shot after " << firedAt << " frames");
	CHECK(firedAt == 9); // the aim ends at once, then the PreAttackDelay and the arrow's flight
}
