// OpenBFME unit tests. GPL-3.0.
// AimWeaponBehavior (RW 0x85E30B): the AIM_HIGH / STRAIGHT / LOW / NEAR / FAR model conditions of a unit with an aiming weapon. Synthetic data
// (combattest::CombatWorld), no retail files.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AimWeaponBehavior.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
const int kHigh = 0xB2;
const int kStraight = 0xB3;
const int kLow = 0xB4;
const int kNear = 0xB5;
const int kFar = 0xB6;

// a 2 frame pre-attack and a 5 frame firing: the window of RW 0x6CA2C3 is [pre-fire + 2, pre-fire + 7)
std::string weapon(const std::string &name, const std::string &aiming)
{
	return "Weapon " + name + "\n" + aiming + R"ini(  AttackRange = 400
  WeaponSpeed = 100
  DelayBetweenShots = 4000
  PreAttackDelay = 400
  PreAttackType = PER_SHOT
  FiringDuration = 1000
  DamageNugget
    Damage = 1
    Radius = 0.0
    DamageType = PIERCE
    DeathType = NORMAL
  End
End
)ini";
}

std::string aimUnit(const std::string &name, const std::string &weaponName, const std::string &fields, bool ai = true)
{
	return "Object " + name + R"ini(
  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE
  Geometry = CYLINDER
  GeometryMajorRadius = 8
  GeometryMinorRadius = 8
  GeometryHeight = 20
)ini" + (weaponName.empty() ? std::string() : "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY " + weaponName + "\n  End\n") +
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n" + (ai ? "  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n" : "") +
		"  Behavior = AimWeaponBehavior ModuleTag_Aim\n" + fields + R"ini(  End
  LocomotorSet
    Locomotor = WalkerLoco
    Condition = SET_NORMAL
    Speed = 55
  End
End
)ini";
}

struct AimWorld : CombatWorld
{
	AimWorld()
	{
		combat().setAutoAcquireEnabled(false);
		std::string err = w.load(weapon("AimBow", "  IsAimingWeapon = Yes\n") + weapon("PlainBow", ""), INI_LOAD_OVERWRITE, "aimweapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(aimUnit("Aimer", "AimBow", "    AimLowThreshold = -0.15\n    AimHighThreshold = 0.15\n    AimNearDistance = 40\n    AimFarDistance = 80\n") +
				aimUnit("DefaultAimer", "AimBow", "") + aimUnit("PlainShooter", "PlainBow", "") + aimUnit("NoWeapon", "", "") + aimUnit("NoAI", "AimBow", "", false),
			INI_LOAD_OVERWRITE, "aimers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	AimWeaponBehavior *aimOf(Object *o)
	{
		AimWeaponBehavior *m = dynamic_cast<AimWeaponBehavior *>(o->findModule("AimWeaponBehavior"));
		REQUIRE(m);
		return m;
	}
	// the unit's pre-fire at its own position + (dx, dy, dz), then the two frames of the pre-attack: the weapon's window is open for five frames
	void aimAt(Object *o, float dx, float dy, float dz)
	{
		const Coord3D p = *o->getPosition();
		const Coord3D target{ p.x + dx, p.y + dy, p.z + dz };
		o->getWeapons()->preFireCurrentWeapon(nullptr, &target);
		frames(2);
	}
	// another target while the window stays open (a pre-fire writes the target and starts its pre-attack again: that is ended at once)
	void retarget(Object *o, float dx, float dy, float dz)
	{
		const Coord3D p = *o->getPosition();
		const Coord3D target{ p.x + dx, p.y + dy, p.z + dz };
		o->getWeapons()->preFireCurrentWeapon(nullptr, &target);
		o->getWeapons()->currentWeapon()->setWhenPreAttackFinished(logic->getFrame());
	}
	// the conditions as a string, high / straight / low / near / far, e.g. "HS-N-"
	static std::string shown(const Object &o)
	{
		std::string s;
		s += o.testModelCondition(kHigh) ? "H" : "-";
		s += o.testModelCondition(kStraight) ? "S" : "-";
		s += o.testModelCondition(kLow) ? "L" : "-";
		s += o.testModelCondition(kNear) ? "N" : "-";
		s += o.testModelCondition(kFar) ? "F" : "-";
		return s;
	}
	// a fresh unit's conditions after aiming at the offset
	std::string aimed(const std::string &name, float dx, float dy, float dz)
	{
		Object *o = unit(name, 'A', 400.0f, 400.0f);
		frames(2);
		REQUIRE(shown(*o) == "-----");
		aimAt(o, dx, dy, dz);
		aimOf(o)->update();
		const std::string s = shown(*o);
		logic->destroyObject(o);
		frames(1);
		return s;
	}
};
} // namespace

TEST_CASE("AimWeaponBehavior: the model condition numbers are AIM_HIGH .. AIM_FAR (RW word 5 bits 18 .. 22)")
{
	CHECK(std::string(TheModelConditionNames[kHigh]) == "AIM_HIGH");
	CHECK(std::string(TheModelConditionNames[kStraight]) == "AIM_STRAIGHT");
	CHECK(std::string(TheModelConditionNames[kLow]) == "AIM_LOW");
	CHECK(std::string(TheModelConditionNames[kNear]) == "AIM_NEAR");
	CHECK(std::string(TheModelConditionNames[kFar]) == "AIM_FAR");
}

TEST_CASE("AimWeaponBehavior: the data's defaults (RW 0x85E251) and the four fields (RW 0xC58178)")
{
	const AimWeaponBehaviorModuleData d;
	CHECK(d.m_aimLowThreshold == -0.05f);
	CHECK(d.m_aimHighThreshold == 0.05f);
	CHECK(d.m_aimNearDistance == 0.0f);
	CHECK(d.m_aimFarDistance == (std::numeric_limits<float>::max)());
	AimWorld a;
	const AimWeaponBehaviorModuleData *p = a.aimOf(a.unit("Aimer", 'A', 400.0f, 400.0f))->data();
	CHECK(p->m_aimLowThreshold == -0.15f);
	CHECK(p->m_aimHighThreshold == 0.15f);
	CHECK(p->m_aimNearDistance == 40.0f);
	CHECK(p->m_aimFarDistance == 80.0f);
}

TEST_CASE("AimWeaponBehavior: the slope to the recorded target picks AIM_HIGH / STRAIGHT / LOW, strictly past the thresholds (RW 0x85E410 / 0x85E42A)")
{
	AimWorld a;
	CHECK(a.aimed("DefaultAimer", 60.0f, 0.0f, 0.0f) == "-S---");
	CHECK(a.aimed("DefaultAimer", 60.0f, 0.0f, 6.0f) == "H----");  // 0.1 > 0.05
	CHECK(a.aimed("DefaultAimer", 60.0f, 0.0f, -6.0f) == "--L--"); // -0.1 < -0.05
	CHECK(a.aimed("Aimer", 60.0f, 0.0f, 6.0f) == "-S---");         // 0.1 is below 0.15
	CHECK(a.aimed("Aimer", 60.0f, 0.0f, 12.0f) == "H----");        // 0.2
	CHECK(a.aimed("Aimer", 60.0f, 0.0f, -12.0f) == "--L--");
	// the distance is the 2D one: (48, 36) is 60 away, so 12 up is the slope 0.2 too
	CHECK(a.aimed("Aimer", 48.0f, 36.0f, 12.0f) == "H----");
	// at least 10 (RW 0xBD83D8): 1 up at a distance of 1 is the slope 0.1, not 1; 1 up in place is no infinity
	CHECK(a.aimed("Aimer", 1.0f, 0.0f, 1.0f) == "-S-N-");
	CHECK(a.aimed("Aimer", 0.0f, 0.0f, 1.0f) == "-S-N-");
	CHECK(a.aimed("Aimer", 0.0f, 0.0f, 2.0f) == "H--N-");
}

TEST_CASE("AimWeaponBehavior: AIM_NEAR below AimNearDistance, AIM_FAR above AimFarDistance (RW 0x85E45F / 0x85E481); the defaults set neither")
{
	AimWorld a;
	CHECK(a.aimed("Aimer", 39.0f, 0.0f, 0.0f) == "-S-N-");
	CHECK(a.aimed("Aimer", 40.0f, 0.0f, 0.0f) == "-S---");
	CHECK(a.aimed("Aimer", 80.0f, 0.0f, 0.0f) == "-S---");
	CHECK(a.aimed("Aimer", 81.0f, 0.0f, 0.0f) == "-S--F");
	CHECK(a.aimed("DefaultAimer", 1.0f, 0.0f, 0.0f) == "-S---");
	CHECK(a.aimed("DefaultAimer", 300.0f, 0.0f, 0.0f) == "-S---");
}

TEST_CASE("AimWeaponBehavior: a new slope adds its condition and keeps the old one, near and far replace each other, in between both stay (RW 0x85E43D / 0x68D607)")
{
	AimWorld a;
	Object *o = a.unit("Aimer", 'A', 400.0f, 400.0f);
	a.frames(2);
	AimWeaponBehavior *m = a.aimOf(o);
	a.aimAt(o, 30.0f, 0.0f, 0.0f);
	m->update();
	CHECK(a.shown(*o) == "-S-N-");
	a.retarget(o, 60.0f, 0.0f, 30.0f);
	m->update();
	CHECK(a.shown(*o) == "HS-N-"); // 60 is neither near nor far: AIM_NEAR stays
	a.retarget(o, 100.0f, 0.0f, -50.0f);
	m->update();
	CHECK(a.shown(*o) == "HSL-F");
	a.retarget(o, 20.0f, 0.0f, 0.0f);
	m->update();
	CHECK(a.shown(*o) == "HSLN-");
}

TEST_CASE("AimWeaponBehavior: the weapon's window ends and all five conditions go, once (RW 0x85E4B5 .. 0x85E4E7)")
{
	AimWorld a;
	Object *o = a.unit("Aimer", 'A', 400.0f, 400.0f);
	a.frames(2);
	AimWeaponBehavior *m = a.aimOf(o);
	CHECK_FALSE(m->isAiming());
	a.aimAt(o, 30.0f, 0.0f, 30.0f);
	m->update();
	CHECK(m->isAiming());
	CHECK(a.shown(*o) == "H--N-");
	const std::uint32_t end = o->getWeapons()->currentWeapon()->followThruEnd();
	CHECK(end == o->getWeapons()->currentWeapon()->whenPreAttackFinished() + 5u);
	while (a.logic->getFrame() < end)
	{
		CHECK(a.shown(*o) == "H--N-");
		a.frames(1);
	}
	m->update();
	CHECK_FALSE(m->isAiming());
	CHECK(a.shown(*o) == "-----");
	// not aiming: a condition something else sets is left alone
	o->setModelConditionState(kFar, true);
	m->update();
	CHECK(a.shown(*o) == "----F");
}

TEST_CASE("AimWeaponBehavior: a weapon without IsAimingWeapon sets nothing (RW 0x85E373); no AI or no weapon: the module sleeps forever (RW 0x85E505)")
{
	AimWorld a;
	CHECK(a.aimed("PlainShooter", 30.0f, 0.0f, 30.0f) == "-----");
	Object *o = a.unit("Aimer", 'A', 300.0f, 300.0f);
	a.frames(2);
	CHECK(a.aimOf(o)->update() == UPDATE_SLEEP_NONE);
	CHECK(a.aimOf(a.unit("NoWeapon", 'A', 500.0f, 300.0f))->update() == UPDATE_SLEEP_FOREVER);
	CHECK(a.aimOf(a.unit("NoAI", 'A', 500.0f, 500.0f))->update() == UPDATE_SLEEP_FOREVER);
}

TEST_CASE("AimWeaponBehavior: an attack order keeps the unit aiming by the AI's active state (AI vslot 0x1BC), the idle order ends it, without the module's update called by the test")
{
	AimWorld a;
	Object *o = a.unit("Aimer", 'A', 400.0f, 400.0f);
	Object *victim = a.unit("Dummy", 'B', 430.0f, 400.0f);
	a.frames(2);
	CHECK(a.shown(*o) == "-----");
	REQUIRE(o->getAIUpdateInterface()->aiAttackObject(victim, CMD_FROM_PLAYER));
	const int frames = a.runUntil([&] { return a.aimOf(o)->isAiming() && o->getWeapons()->drawTargetID() == victim->getID(); }, 60);
	REQUIRE(frames < 60); // tolerance: the frame the first pre-fire comes
	a.frames(1);
	CHECK(a.shown(*o).substr(1, 1) == "S");
	CHECK(a.shown(*o).substr(3) == "N-");
	o->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	a.runUntil([&] { return !a.aimOf(o)->isAiming(); }, 20);
	CHECK(a.shown(*o) == "-----");
}

TEST_CASE("AimWeaponBehavior: the snapshot keeps the aim flag (RW 0x85E1C7), the hash takes it and the recorded target")
{
	AimWorld a;
	Object *o = a.unit("Aimer", 'A', 400.0f, 400.0f);
	a.frames(2);
	AimWeaponBehavior *m = a.aimOf(o);
	a.aimAt(o, 30.0f, 0.0f, 0.0f);
	m->update();
	REQUIRE(m->isAiming());
	auto hash = [&] {
		StateHasher h;
		m->crc(h);
		return h.value();
	};
	const auto aiming = hash();
	XferSave saved;
	m->xfer(saved);
	while (m->isAiming())
	{
		a.frames(1);
	}
	XferLoad load(saved.data());
	m->xfer(load);
	CHECK(m->isAiming());
	// the same state again but for the target: another hash
	const Coord3D t{ 1.0f, 2.0f, 3.0f };
	const auto before = hash();
	o->getWeapons()->preFireCurrentWeapon(nullptr, &t);
	CHECK(hash() != before);
	CHECK(aiming != 0u);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad shortLoad(truncated);
	CHECK_THROWS_AS(m->xfer(shortLoad), XferException);
}

TEST_CASE("AimWeaponBehavior: a save from before the port (the unported shell's version 1 block) still loads, not aiming and asleep; a version 3 is refused")
{
	AimWorld a;
	Object *o = a.unit("Aimer", 'A', 400.0f, 400.0f);
	a.frames(2);
	AimWeaponBehavior *m = a.aimOf(o);
	a.aimAt(o, 30.0f, 0.0f, 0.0f);
	m->update();
	REQUIRE(m->isAiming());
	// the block UnportedUpdateModule::xfer wrote for this module: version 1, the UpdateModule block (asleep for good), the call count
	UnportedUpdateModule shell(o, m->getModuleData(), MODULEINTERFACE_UPDATE);
	shell.update();
	shell.update();
	REQUIRE(shell.calls() == 2u);
	XferSave old;
	shell.xfer(old);
	REQUIRE(old.data().front() == 1);
	XferLoad load(old.data());
	m->xfer(load);
	CHECK(load.atEnd()); // the call count is read, nothing is left over
	CHECK_FALSE(m->isAiming());
	CHECK(m->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	XferSave now;
	m->xfer(now);
	CHECK(now.data().front() == 2);
	std::vector<std::uint8_t> future = now.data();
	future.front() = 3;
	XferLoad bad(future);
	CHECK_THROWS_AS(m->xfer(bad), XferException);
}
