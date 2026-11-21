// OpenBFME. PORT-28 (issue fork issue 100): Weapon/RotatingTurret, the turret aim vector. RotWK's Weapon::calcProjectileLaunchPosition
// (RW 0x6CAB85) turns the launch transform of a projectile by the turret's current angle and pitch when the slot's weapon template
// carries the flag (WeaponTemplate + 0x16C, RW 0x6CAD58) and the launcher's AI names a turret for that slot (RW 0x66246B, -1 skips at
// RW 0x6CAD65); the block is RW 0x6CAD6E .. 0x6CB43B. The launch offset answers both turret points as zero (RW 0x4C36F8 /
// RW 0x4C3711), so the composition is Rz(angle) * Ry(-pitch) about the model origin. Synthetic weapon, AI and turret: no retail data.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Weapon.h"

#include <cmath>
#include <cstring>
#include <string>

namespace
{
using namespace combattest;

// BowWeapon with the flag under test: the same arrow, the same warhead, RotatingTurret Yes or No
std::string turretBow(const char *name, bool rotatingTurret)
{
	return std::string("Weapon ") + name + "\n"
		"  AttackRange = 200\n"
		"  WeaponSpeed = 100\n"
		"  DelayBetweenShots = 800\n"
		"  PreAttackDelay = 400\n"
		"  PreAttackType = PER_POSITION\n"
		"  RotatingTurret = " + (rotatingTurret ? "Yes" : "No") + "\n"
		"  ProjectileNugget\n"
		"    ProjectileTemplateName = Arrow\n"
		"    WarheadTemplateName = BowWarhead\n"
		"  End\n"
		"End\n";
}

// An archer whose AI carries the port's turret (TurretAI, RW AI + 0x20C): `slots` is the turret's ControlledWeaponSlots (RW + 0x4C)
// and the natural angle is 90 degrees, where the constructor leaves the turret (RW 0x8DC8B0, TurretAI + 0x18)
std::string turretShooter(const char *weapon, const char *slots)
{
	return std::string("Object TurretShooter\n"
		"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
		"  VisionRange = 250\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n    Conditions = None\n    Armor = PlainArmor\n  End\n"
		"  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY ") + weapon + "\n  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 60\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"    Turret\n"
		"      TurretTurnRate = 360\n"
		"      ControlledWeaponSlots = " + slots + "\n"
		"      NaturalTurretAngle = 90\n"
		"    End\n"
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 55\n"
		"  End\n"
		"End\n";
}

// one bone: a quarter turn about Z (x -> y) and the translation (1, 2, 3), as test_render2_launch's FixedBone
class FixedBone : public ProjectileLaunchOffsets
{
public:
	bool launchOffset(const Object &, int, int, float launch[12]) override
	{
		const float m[12] = { 0, -1, 0, 1, 1, 0, 0, 2, 0, 0, 1, 3 };
		std::memcpy(launch, m, sizeof(m));
		return true;
	}
};

struct TurretRange
{
	CombatWorld w;
	Object *a = nullptr, *b = nullptr;
	std::unique_ptr<FixedBone> bone = std::make_unique<FixedBone>();

	TurretRange(bool rotatingTurret, const char *slots)
		: w((turretBow("TurretBowOn", true) + turretBow("TurretBowOff", false)
			+ turretShooter(rotatingTurret ? "TurretBowOn" : "TurretBowOff", slots)).c_str())
	{
		w.combat().setAutoAcquireEnabled(false);
		w.combat().setLaunchOffsets(bone.get());
		a = w.unit("TurretShooter", 'A', 300, 300);
		b = w.unit("Dummy", 'B', 300, 400);
		w.frames(2);
	}
	// the arrow the shot creates (the stand-in projectile launcher of the fixture), and its flight module, which keeps the launch
	// point in m_flightPathStart (ProjectileModules.cpp:483): the projectile's own orientation and position move on the same tick
	Object *fire(BezierProjectileBehavior **flight = nullptr)
	{
		ObjectWeapons *ow = a->getWeapons();
		REQUIRE(ow->chooseBestWeaponForTarget(b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
		while (ow->currentStatus() != WEAPON_READY_TO_FIRE && w.logic->getFrame() < 60)
		{
			w.frames(1);
		}
		ow->preFireCurrentWeapon(b, nullptr);
		int waited = 0;
		while (ow->currentStatus() == WEAPON_PRE_ATTACK && waited++ < 10)
		{
			w.frames(1);
		}
		ow->fireCurrentWeapon(b, nullptr);
		for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			for (const std::unique_ptr<BehaviorModule> &m : o->modules())
			{
				if (BezierProjectileBehavior *bz = dynamic_cast<BezierProjectileBehavior *>(m.get()))
				{
					if (flight)
					{
						*flight = bz;
					}
					return o;
				}
			}
		}
		return nullptr;
	}
};

// The launch point RW 0x6CAB85 leaves at RW 0x6CB440 (Thing::convertBonePosToWorldPos): the launcher's basis times the bone's
// translation, plus the launcher's position. `local` is that translation after the turret's turn.
Coord3D launchPoint(const Object &launcher, const float local[3])
{
	const float *t = launcher.getBasis();
	const Coord3D &p = *launcher.getPosition();
	Coord3D out;
	out.x = p.x + t[0] * local[0] + t[1] * local[1] + t[2] * local[2];
	out.y = p.y + t[3] * local[0] + t[4] * local[1] + t[5] * local[2];
	out.z = p.z + t[6] * local[0] + t[7] * local[1] + t[8] * local[2];
	return out;
}

// tolerance: the turret's quarter turn is the port's sinDet / cosDet of the float 90 degrees (the RW 0xA3CF90 / RW 0xA3CF84 sin and
// cos are approximated in the port, S-167), and the same functions answer the basis products, so the point is not exact
void checkPoint(const Coord3D &at, const Coord3D &want)
{
	CHECK(std::fabs(at.x - want.x) <= 1e-3); // tolerance: sinDet / cosDet of 90 degrees are approximations of RW 0xA3CF90 / RW 0xA3CF84
	CHECK(std::fabs(at.y - want.y) <= 1e-3); // tolerance: sinDet / cosDet of 90 degrees are approximations of RW 0xA3CF90 / RW 0xA3CF84
	CHECK(std::fabs(at.z - want.z) <= 1e-3); // tolerance: sinDet / cosDet of 90 degrees are approximations of RW 0xA3CF90 / RW 0xA3CF84
}
} // namespace

// RW 0x6CAD3F .. 0x6CB43B with the flag set: the turret's angle turns the launch bone about the model origin. The turret stands still
// at its natural quarter turn, so the bone's translation (1, 2, 3) becomes (c*1 - s*2, s*1 + c*2, 3) = (-2, 1, 3) with cos = 0,
// sin = 1. The launch transform itself is set on the projectile at RW 0x6CB490 (RW 0x70BA76 setTransformMatrix) and the projectile's
// flight orientation replaces it on the same tick, so the test reads the launch point, which the same rotation answers (RW 0x70C201).
TEST_CASE("PORT-28 rotating turret: the launch transform of a projectile is turned by the turret's angle (RW 0x6CAD3F .. 0x6CB43B)")
{
	TurretRange r(true, "PRIMARY");
	BezierProjectileBehavior *flight = nullptr;
	Object *arrow = r.fire(&flight);
	REQUIRE(arrow != nullptr);
	REQUIRE(flight != nullptr);
	CHECK(r.w.combat().counters().turretedLaunches == 1); // RW 0x6CAD58 and RW 0x6CAD65 let the block run
	const float turned[3] = { -2.0f, 1.0f, 3.0f };
	checkPoint(flight->flightStart(), launchPoint(*r.a, turned));
	// without the turn the point would be the bone's own translation: the two differ, which is what the flag changes
	const float unturned[3] = { 1.0f, 2.0f, 3.0f };
	const Coord3D wrong = launchPoint(*r.a, unturned);
	CHECK(std::fabs(flight->flightStart().x - wrong.x) > 1.0f); // tolerance: the turn moves the point by 3 in x and 1 in y, far above rounding
	CHECK(std::fabs(flight->flightStart().y - wrong.y) > 0.5f); // tolerance: the turn moves the point by 1 in y, far above rounding
}

// RW 0x6CAD58: the flag clear skips the block: the launch transform stays the launch bone's (the neighbouring behaviour).
TEST_CASE("PORT-28 without the flag the launch transform stays the launch bone's (RW 0x6CAD58 -> RW 0x6CB440)")
{
	TurretRange r(false, "PRIMARY");
	BezierProjectileBehavior *flight = nullptr;
	Object *arrow = r.fire(&flight);
	REQUIRE(arrow != nullptr);
	REQUIRE(flight != nullptr);
	CHECK(r.w.combat().counters().turretedLaunches == 0);
	const float bone[3] = { 1.0f, 2.0f, 3.0f };
	checkPoint(flight->flightStart(), launchPoint(*r.a, bone));
}

// RW 0x6CAD65: the flag set, but the turret of the AI controls another weapon slot (RW 0x66246B answers -1 for PRIMARY): skipped.
TEST_CASE("PORT-28 with the flag but no turret for the slot the launch transform stays the launch bone's (RW 0x6CAD65)")
{
	TurretRange r(true, "SECONDARY");
	BezierProjectileBehavior *flight = nullptr;
	Object *arrow = r.fire(&flight);
	REQUIRE(arrow != nullptr);
	REQUIRE(flight != nullptr);
	CHECK(r.w.combat().counters().turretedLaunches == 0);
	const float bone[3] = { 1.0f, 2.0f, 3.0f };
	checkPoint(flight->flightStart(), launchPoint(*r.a, bone));
}

// The two inputs of the block: the parsed template flag (RW 0x42E558 + 0x16C, the getter RW 0x6C9C50) and the AI's answer
// (RW 0x66246B: the index of the turret whose ControlledWeaponSlots hold the slot, with its angle (+ 0x18) and pitch (+ 0x1C)).
TEST_CASE("PORT-28 the template flag parses (RW 0x42E558 + 0x16C) and the AI names the turret of the slot (RW 0x66246B)")
{
	TurretRange r(true, "PRIMARY");
	const Weapon *on = r.a->getWeapons()->weaponInSlot(0);
	REQUIRE(on != nullptr);
	CHECK(on->getTemplate()->rotatingTurret());
	AIUpdateInterface *ai = r.a->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->turret() != nullptr);
	float angle = 0.0f, pitch = 0.0f;
	CHECK(ai->whichTurretForWeaponSlot(0, &angle, &pitch) == ai->turret()->whichTurret());
	CHECK(std::fabs((angle) - (1.5707964f)) <= 1e-5); // tolerance: NaturalTurretAngle 90 degrees parsed by the float RADS_PER_DEGREE (RW 0x42EE15) is 1.5707964f, not pi / 2
	CHECK(pitch == 0.0f);
	CHECK(ai->whichTurretForWeaponSlot(1, &angle, &pitch) == TURRET_INVALID); // SECONDARY is not on the turret
	CHECK(ai->whichTurretForWeaponSlot(-1, nullptr, nullptr) == TURRET_INVALID);
}
