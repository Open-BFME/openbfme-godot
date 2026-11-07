// OpenBFME unit tests, lane AIMAIM-1 (decompfind-20261104 F-7): AIAttackAimAtTargetState::updateInternal (RW 0x75232F). A melee weapon aims at the "Ram"
// contact point (RW 0x690BD2, not preferred: the point must touch the victim's geometry, RW 0xAD3415 -> 0xAD2CE0) of a BLOCKING_GATE / WALK_ON_TOP_OF_WALL
// victim, not at its centre; an owner with FiringArc reports S-7175. Synthetic data (combattest::CombatWorld), no retail files. GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"

#include <cmath>
#include <string>

using namespace combattest;

namespace
{
std::string gate(const std::string &name, const std::string &kinds, const std::string &contact)
{
	return "Object " + name + "\n  KindOf = " + kinds + R"ini(
  Geometry = BOX
  GeometryMajorRadius = 40
  GeometryMinorRadius = 10
  GeometryHeight = 30
)ini" + contact + R"ini(  ArmorSet
    Conditions = None
    Armor = PlainArmor
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100000
  End
End
)ini";
}

// the goal angle the swordsman takes once it aims at `victim` (it stands at (330, 335) facing +x, the gate's centre is at (300, 350))
float aimAngleAt(CombatWorld &w, Object *victim)
{
	Object *a = w.unit("Swordsman", 'A', 330.0f, 335.0f);
	w.frames(2);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	REQUIRE(ai->aiAttackObject(victim, CMD_FROM_PLAYER));
	const int aiming = CombatNames::statuses().isAimingWeapon;
	const int frames = w.runUntil([&] { return a->testStatus((unsigned)aiming); }, 60);
	REQUIRE(frames < 60); // tolerance: the frame the aim begins (the walk to the gate's edge is short)
	REQUIRE(ai->mover().goalType() == AIGOAL_ANGLE);
	return ai->mover().goalAngle();
}
} // namespace

TEST_CASE("AIMAIM-1: RW 0x7524EF .. 0x752561, a melee attacker aims at a BLOCKING_GATE's \"Ram\" contact point, not at the gate's centre")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	std::string err = w.w.load(gate("AimRamGate", "BLOCKING_GATE IMMOBILE", "  GeometryContactPoint = X:30 Y:-10 Z:0 Ram\n") +
			gate("AimPlainGate", "IMMOBILE", "  GeometryContactPoint = X:30 Y:-10 Z:0 Ram\n") +
			gate("AimFarRamGate", "BLOCKING_GATE IMMOBILE", "  GeometryContactPoint = X:30 Y:-30 Z:0 Ram\n"),
		INI_LOAD_OVERWRITE, "aimgates.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	const float toRam = std::atan2(340.0f - 335.0f, 330.0f - 330.0f);    // the Ram point (330, 340): straight up, pi / 2
	const float toCentre = std::atan2(350.0f - 335.0f, 300.0f - 330.0f); // the gate's centre: about 2.68
	SUBCASE("a BLOCKING_GATE with a Ram point on its edge: the Ram point")
	{
		Object *g = w.unit("AimRamGate", 'B', 300.0f, 350.0f);
		CHECK(aimAngleAt(w, g) == doctest::Approx(toRam).epsilon(1e-5)); // tolerance: relAngle RW 0x4B3D8D's acos and the orientation sum
	}
	SUBCASE("the same point on a gate that is no BLOCKING_GATE: the centre")
	{
		Object *g = w.unit("AimPlainGate", 'B', 300.0f, 350.0f);
		CHECK(aimAngleAt(w, g) == doctest::Approx(toCentre).epsilon(1e-5)); // tolerance: as above
	}
	SUBCASE("a Ram point 20 off the gate's edge does not touch its geometry (the radius 2 probe, RW 0xAD2CE0): the centre")
	{
		Object *g = w.unit("AimFarRamGate", 'B', 300.0f, 350.0f);
		CHECK(aimAngleAt(w, g) == doctest::Approx(toCentre).epsilon(1e-5)); // tolerance: as above
	}
}

TEST_CASE("AIMAIM-1: an owner whose template sets FiringArc reports S-7175 when it aims (RW 0x7523B7 / 0x752572 are not ported); one without does not")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	const std::string arc = R"ini(Object ArcSwordsman
  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE
  FiringArc = 90
  Geometry = CYLINDER
  GeometryMajorRadius = 8
  GeometryMinorRadius = 8
  GeometryHeight = 20
  WeaponSet
    Conditions = None
    Weapon = PRIMARY SwordWeapon
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100
  End
  Behavior = AIUpdateInterface ModuleTag_AI
  End
  Behavior = PhysicsBehavior ModuleTag_Physics
  End
  LocomotorSet
    Locomotor = WalkerLoco
    Condition = SET_NORMAL
    Speed = 55
  End
End
)ini";
	std::string err = w.w.load(arc, INI_LOAD_OVERWRITE, "aimarc.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	auto reported = [&] {
		for (const std::string &l : w.logic->report().stops)
		{
			if (l.rfind("[S-7175]", 0) == 0)
			{
				return true;
			}
		}
		return false;
	};
	Object *plain = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	Object *v1 = w.unit("Dummy", 'B', 315.0f, 300.0f);
	w.frames(2);
	REQUIRE(plain->getAIUpdateInterface()->aiAttackObject(v1, CMD_FROM_PLAYER));
	w.frames(10);
	CHECK_FALSE(reported());
	Object *a = w.unit("ArcSwordsman", 'A', 300.0f, 500.0f);
	Object *v2 = w.unit("Dummy", 'B', 315.0f, 500.0f);
	w.frames(2);
	REQUIRE(a->getAIUpdateInterface()->aiAttackObject(v2, CMD_FROM_PLAYER));
	w.frames(10);
	CHECK(reported());
}

TEST_CASE("AIMAIM-1 follow-up: relAngle (RW 0x4B3D8D) measures against the orientation's cosine and sine (RW 0x70B9E0), not a tilted transform's X axis")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	// facing +x, pitched 30 degrees (a unit on a slope): the X axis is (cos 30, 0, -sin 30); the angle stays 0 (atan2 of the X axis)
	const float c30 = 0.866025388f, s30 = 0.5f;
	const float basis[9] = { c30, 0.0f, s30, 0.0f, 1.0f, 0.0f, -s30, 0.0f, c30 }; // row major, columns X, Y, Z
	a->setTransform(a->getPosition(), basis);
	REQUIRE(a->getOrientation() == 0.0f);
	const Coord3D p{ 400.0f, 400.0f, a->getPosition()->z }; // 45 degrees to the left
	// tolerance: acos of the float dot (pi / 4 = 0.785398); the transform's X axis would answer acos(0.866 * 0.707) = 0.9117
	CHECK(std::fabs(emotionRelAngle(*a, p) - 0.785398f) < 1e-4f);
	const Coord3D q{ 400.0f, 200.0f, a->getPosition()->z }; // 45 degrees to the right
	CHECK(std::fabs(emotionRelAngle(*a, q) + 0.785398f) < 1e-4f); // tolerance: as above, mirrored
}
