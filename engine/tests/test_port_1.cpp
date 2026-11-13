// OpenBFME tests. GPL-3.0.
// PORT-1: AIAttackFireWeaponState reads Weapon/FinishAttackOnceStarted (RW 0x74C6A8 .. RW 0x74C6FB) for an attack on an object whose victim is gone.
// Setup: a turret unit (ControlledWeaponSlots PRIMARY) whose weapon has a ten frame wind-up. The turret's own machine aims, enters its fire state and starts
// the wind-up; the victim is then killed. RW 0x74C6E5 tests the template's byte (+ 0x161, default 1): set, the shot leaves (RW 0x74C6EC -> RW 0x74C530); clear,
// the machine's goal object is dropped (RW 0x74C6F2) and the state fails (RW 0x74C8AC), so the wind-up is cancelled and no shot leaves.
//
// The turret is the reachable machine here: on a unit or horde the outer AIAttackState ends the attack on a gone victim before its fire state sees one
// (stop S-583), while the turret machine keeps its goal object until its aim drops the dead target (TurretAI::getTurretTarget, RW 0x8DC507).
// Synthetic INI only; no retail or Ennorath files.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"

#include <string>

using namespace combattest;

namespace
{
// a bow with a ten frame wind-up (PreAttackDelay 2000 ms at the fixture's five frames per second) and the field under test
std::string bow(const char *name, bool finish)
{
	return std::string("Weapon ") + name + "\n"
		"  AttackRange = 200\n"
		"  WeaponSpeed = 100\n"
		"  DelayBetweenShots = 800\n"
		"  PreAttackDelay = 2000\n"
		"  PreAttackType = PER_SHOT\n"
		"  FinishAttackOnceStarted = " + (finish ? "Yes\n" : "No\n") +
		"  ProjectileNugget\n"
		"    ProjectileTemplateName = Arrow\n"
		"    WarheadTemplateName = BowWarhead\n"
		"  End\n"
		"End\n";
}

// a structure whose only weapon sits on its turret, so TurretAI controls the current weapon (RW 0x8DCA5C)
std::string turret(const char *name, const char *weapon)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 10\n"
		"  GeometryMinorRadius = 10\n"
		"  GeometryHeight = 30\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = PlainArmor\n"
		"  End\n"
		"  WeaponSet\n"
		"    Conditions = None\n"
		"    Weapon = PRIMARY " + weapon + "\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 500\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"    Turret\n"
		"      TurretTurnRate = 180\n"
		"      ControlledWeaponSlots = PRIMARY\n"
		"    End\n"
		"  End\n"
		"End\n";
}

const std::string kPort1Objects =
	bow("Port1FinishShot", true) + bow("Port1CancelShot", false) + turret("Port1TurretFinish", "Port1FinishShot") +
	turret("Port1TurretCancel", "Port1CancelShot");

// the turret, its enemy (the fixture's Dummy destroys itself when it dies) and the walk to the wind-up
struct Scene
{
	CombatWorld *w = nullptr;
	Object *gun = nullptr;
	Object *foe = nullptr;
	TurretAI *turret = nullptr;
};

void arm(Scene &s, const char *turretName)
{
	s.gun = s.w->unit(turretName, 'A', 400.0f, 300.0f);
	s.foe = s.w->unit("Dummy", 'B', 500.0f, 300.0f); // 100 units ahead, inside the bow's 200
	AIUpdateInterface *ai = s.gun->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	s.turret = ai->turret();
	REQUIRE(s.turret != nullptr);
	REQUIRE(s.turret->isOwnersCurWeaponOnTurret());
	s.turret->setTurretTargetObject(s.foe, false); // RW 0x8DCA8B: the goal object and the turret's AIM
}

int runUntilPreAttack(Scene &s, int limit)
{
	return s.w->runUntil(
		[&]() {
			ObjectWeapons *w = s.gun->getWeapons();
			return w && w->currentStatus() == WEAPON_PRE_ATTACK;
		},
		limit);
}

} // namespace

TEST_CASE("port 1: a turret finishes the shot when the victim dies during the wind-up (FinishAttackOnceStarted = Yes, RW 0x74C6E5)")
{
	CombatWorld w(kPort1Objects.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Scene s;
	s.w = &w;
	arm(s, "Port1TurretFinish");

	// the turret aimed and its fire state started the wind-up; runUntil returns the limit when the
	// turret never gets there, which is the only thing this line rejects
	REQUIRE(runUntilPreAttack(s, 20) < 20); // tolerance: how many turns the turret needs depends on its turn rate, not on the fix
	REQUIRE(s.turret->currentStateId() == TURRETAI_FIRE);
	REQUIRE(s.turret->stats().shots == 0);

	s.foe->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // Object + 0x458 bit 0 (RW 0x74C6C7); the Dummy leaves the game at once
	REQUIRE(s.foe->isEffectivelyDead()); // dead is what RW 0x74C6C7 asks; whether the world has removed the body yet is the removal delay's business

	// RW 0x74C6E5 keeps the shot: the state does not fail on the gone victim, the wind-up ends and RW 0x74C530 fires
	w.frames(12);
	CHECK(s.turret->stats().shots == 1);
	CHECK(s.turret->currentStateId() != TURRETAI_FIRE);
}

TEST_CASE("port 1: a turret cancels the shot when the victim dies and FinishAttackOnceStarted = No (RW 0x74C6F2 -> RW 0x74C8AC)")
{
	CombatWorld w(kPort1Objects.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Scene s;
	s.w = &w;
	arm(s, "Port1TurretCancel");

	// the same wait as the first case: the wind-up must start before the deadline (see above)
	REQUIRE(runUntilPreAttack(s, 20) < 20); // tolerance: how many turns the turret needs depends on its turn rate, not on the fix
	REQUIRE(s.turret->currentStateId() == TURRETAI_FIRE);
	REQUIRE(s.turret->stats().shots == 0);

	s.foe->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	REQUIRE(s.foe->isEffectivelyDead()); // the same precondition as the first case: the victim is dead

	// RW 0x74C6F2: the goal object is dropped and the state fails at RW 0x74C8AC, so the wind-up is cancelled (onExit, RW 0x74C8BE) and no shot leaves
	w.frames(12);
	CHECK(s.turret->stats().shots == 0);
	Object *target = nullptr;
	Coord3D pos{ 0.0f, 0.0f, 0.0f };
	CHECK(s.turret->getTurretTarget(target, pos) == 0); // 0 = none (RW TurretAI + 0x2C)
	CHECK(s.turret->currentStateId() != TURRETAI_FIRE);
}
