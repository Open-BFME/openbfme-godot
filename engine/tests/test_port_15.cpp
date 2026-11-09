// OpenBFME. GPL-3.0.
// PORT-15 (issue #71): LockWeaponCreate locks through Object::setWeaponLock (RW 0x69121A), not on its own path. Synthetic INI only (no retail or
// Ennorath files). Every expectation is read from the binary with tools/mod/rw.py on RotWK 2.01 (game.dat md5 daed3668, caveat S-001):
//
//   * LockWeaponCreate::onBuildComplete RW 0x8BCE0A: clear the flag byte, then push 2 (LOCKED_PERMANENTLY), push [data+8] (SlotToLock), call
//     Object::setWeaponLock RW 0x69121A — nothing else, the return value is dropped;
//   * RW 0x69121A: the contain module (Object + 0x258) first gets vslot 0x168 (slot, type) (RW 0x691220 .. 0x691232 — HordeContain's pass-through,
//     RW 0x8656A7 per contained object), then SWITCHED_WEAPONS (status 0x51) = (type == 2 and slot != 0) (RW 0x691238 .. 0x69124E via RW 0x62684D),
//     then the weapon set's lock RW 0x6C97F9 (RW 0x691253 .. 0x69125F).
//
// The port's one path for that is ObjectWeapons::setObjectWeaponLock (ObjectWeapons.cpp, RW 0x69121A); LockWeaponCreate now calls it with
// (SlotToLock, LOCKED_PERMANENTLY). Module order matters as in RW 0x68D252: the horde's template lists HordeContain before LockWeaponCreate, so the
// payload exists when the lock runs.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
const char kPort15Objects[] =
	// a horde member with a weapon in slot 1 (SECONDARY), so the lock on SECONDARY lands (RW 0x6C97F9: no weapon in the slot -> false)
	"Object Port15Member\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"    Weapon = SECONDARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// the horde: HordeContain BEFORE LockWeaponCreate (RW 0x68D252 runs the create slot 1 of the modules in template order), SlotToLock SECONDARY
	"Object Port15LockHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 150\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"    Weapon = SECONDARY BowWeapon\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Port15Member Position:X:50 Y:0 Position:X:50 Y:20\n"
	"    Slots = 2\n"
	"    InitialPayload = Port15Member 2\n"
	"  End\n"
	"  Behavior = LockWeaponCreate ModuleTag_Lock\n"
	"    SlotToLock = SECONDARY\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	// a plain object, SECONDARY: the lock lands and SWITCHED_WEAPONS is set (RW 0x691238: type == 2 and slot != 0)
	"Object Port15PlainSecondary\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"    Weapon = SECONDARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = LockWeaponCreate ModuleTag_Lock\n"
	"    SlotToLock = SECONDARY\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// a plain object, PRIMARY: the lock lands but SWITCHED_WEAPONS stays clear (RW 0x69123E: slot == 0 -> push 0)
	"Object Port15PlainPrimary\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = LockWeaponCreate ModuleTag_Lock\n"
	"    SlotToLock = PRIMARY\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

bool switchedWeapons(Object &o)
{
	static const int kSwitchedWeapons = CombatNames::status("SWITCHED_WEAPONS"); // RW status 0x51, resolved by name like test_archer_spread.cpp
	return o.testStatus((unsigned)kSwitchedWeapons);
}

bool stopNoted(CombatWorld &w, const char *prefix)
{
	for (const std::string &line : w.logic->report().stops)
	{
		if (line.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

// RW 0x8BCE0A -> RW 0x69121A -> contain vslot 0x168 (RW 0x8656A7 on every member): the horde's members get the permanent lock on SECONDARY and
// SWITCHED_WEAPONS, the horde itself too; no S-981 stop is noted. Without the port's fix the members stay unlocked (the old path locked the horde
// only), so this test fails there.
TEST_CASE("PORT-15 LockWeaponCreate locks through Object::setWeaponLock: a horde's members get the permanent lock and SWITCHED_WEAPONS")
{
	CombatWorld w(kPort15Objects);
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("Port15LockHorde", 'A', 300, 300); // spawn completes the horde (RW 0x68D252)
	REQUIRE(h != nullptr);
	HordeContain *hc = dynamic_cast<HordeContain *>(h->getContain());
	REQUIRE(hc != nullptr);
	std::vector<Object *> members = w.membersOf(h);
	REQUIRE(members.size() == 2);
	for (Object *m : members)
	{
		REQUIRE(m->getWeapons() != nullptr);
		CHECK(m->getWeapons()->isCurWeaponLocked());           // RW 0x6C97F9 via the contain's pass-through
		CHECK(m->getWeapons()->curSlot() == SECONDARY_WEAPON); // the locked slot is the module's SlotToLock
		CHECK(switchedWeapons(*m));                            // RW 0x69124E: status 0x51 = (type == 2 and slot != 0)
	}
	REQUIRE(h->getWeapons() != nullptr);
	CHECK(h->getWeapons()->isCurWeaponLocked());
	CHECK(h->getWeapons()->curSlot() == SECONDARY_WEAPON);
	CHECK(switchedWeapons(*h));
	CHECK_FALSE(stopNoted(w, "[S-981] LockWeaponCreate"));
}

// The plain-object half of RW 0x8BCE0A / 0x69121A: SECONDARY sets the lock and SWITCHED_WEAPONS, PRIMARY sets the lock but leaves the status clear
// (RW 0x69123E). This pins the neighbouring behavior the new call must keep.
TEST_CASE("PORT-15 LockWeaponCreate on a plain object: SECONDARY sets lock + SWITCHED_WEAPONS, PRIMARY sets the lock only")
{
	CombatWorld w(kPort15Objects);
	w.combat().setAutoAcquireEnabled(false);
	Object *sec = w.unit("Port15PlainSecondary", 'A', 300, 300);
	REQUIRE(sec != nullptr);
	sec->friend_onBuildComplete(); // RW 0x68D252 / 0x68D252's slot 1 on a non-horde creator
	REQUIRE(sec->getWeapons() != nullptr);
	CHECK(sec->getWeapons()->isCurWeaponLocked());
	CHECK(sec->getWeapons()->curSlot() == SECONDARY_WEAPON);
	CHECK(switchedWeapons(*sec));

	Object *pri = w.unit("Port15PlainPrimary", 'A', 350, 300);
	REQUIRE(pri != nullptr);
	pri->friend_onBuildComplete();
	REQUIRE(pri->getWeapons() != nullptr);
	CHECK(pri->getWeapons()->isCurWeaponLocked());
	CHECK(pri->getWeapons()->curSlot() == PRIMARY_WEAPON);
	CHECK_FALSE(switchedWeapons(*pri)); // RW 0x69123E: slot == 0 -> status 0x51 = 0
	CHECK_FALSE(stopNoted(w, "[S-981] LockWeaponCreate"));
}
