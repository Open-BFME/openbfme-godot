// OpenBFME unit tests (lane GARRISONWEAPON-1): GarrisonContain::onContaining (RW 0x87BEC8, at RW 0x87BF44 .. 0x87BF9D) gives a rider whose template has a
// CONTAINED weapon set (RW 0x691014(20)) the weapon set flag CONTAINED (RW 0x691059(20): Object + 0x38C bit 20 and the weapon set update), then the members
// of its horde that have one; onRemoving (RW 0x87BFA6, at RW 0x87C20E .. 0x87C267) clears it on the rider (RW 0x691106(20)) and sets it on the members.
// Synthetic templates: a wildman with a melee set (SlowSword) and a CONTAINED set (BowWeapon), a horde of two of them, a plain melee unit, a tower.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/WeaponState.h"

#include <string>

using namespace combattest;

namespace
{
const char kGarrisonWeaponObjects[] =
	"Object Wildman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SlowSword\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = CONTAINED\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object WildmanHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY HordeRangefinder\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = CONTAINED\n"
	"    Weapon = PRIMARY HordeRangefinder\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Wildman Position:X:50 Y:0 Position:X:50 Y:20\n"
	"    InitialPayload = Wildman 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	"ChildObject WatchTower Dummy\n"
	"  KindOf = STRUCTURE\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 10\n"
	"  End\n"
	"End\n";

constexpr int kContained = 20; // WeaponSetType CONTAINED (RW 0x87BF44 push 0x14)

bool containedFlag(const Object *o) { return MaskTest(o->getWeapons()->weaponSetFlags(), (unsigned)kContained); }

std::string primary(const Object *o)
{
	const Weapon *w = o->getWeapons()->weaponInSlot(0);
	return w && w->getTemplate() ? w->getTemplate()->getName() : std::string();
}
} // namespace

TEST_CASE("garrisonweapon1: a rider with a CONTAINED weapon set fights with it inside and with its default set again outside (RW 0x87BF44, 0x87C20E)")
{
	CombatWorld w(kGarrisonWeaponObjects);
	Object *tower = w.unit("WatchTower", 'A', 300, 300);
	Object *wild = w.unit("Wildman", 'A', 330, 300);
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c);
	REQUIRE(primary(wild) == "SlowSword");
	REQUIRE(c->addToContain(wild));
	CHECK(containedFlag(wild));
	CHECK(primary(wild) == "BowWeapon");
	c->removeFromContain(wild);
	CHECK(wild->getContainedBy() == nullptr);
	CHECK_FALSE(containedFlag(wild));
	CHECK(primary(wild) == "SlowSword");
}

TEST_CASE("garrisonweapon1: a unit without a CONTAINED weapon set keeps its weapons (RW 0x691014(20) false)")
{
	CombatWorld w(kGarrisonWeaponObjects);
	Object *tower = w.unit("WatchTower", 'A', 300, 300);
	Object *slasher = w.unit("Slasher", 'A', 330, 300);
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c->addToContain(slasher));
	CHECK_FALSE(containedFlag(slasher));
	CHECK(primary(slasher) == "SlowSword");
	c->removeFromContain(slasher);
	CHECK_FALSE(containedFlag(slasher));
	CHECK(primary(slasher) == "SlowSword");
}

TEST_CASE("garrisonweapon1: a horde rider passes CONTAINED to its members on the entry; its exit clears its own and sets the members' (RW 0x87BF5D, 0x87C225)")
{
	CombatWorld w(kGarrisonWeaponObjects);
	Object *tower = w.unit("WatchTower", 'A', 300, 300);
	Object *horde = w.unit("WildmanHorde", 'A', 400, 300);
	const ContainModuleInterface::ContainedItemsList *members = horde->getContain()->getContainedItemsList();
	REQUIRE(members);
	REQUIRE(members->size() == 2u);
	for (const Object *m : *members)
	{
		CHECK(primary(m) == "SlowSword");
	}
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c->addToContain(horde));
	CHECK(containedFlag(horde));
	for (const Object *m : *members)
	{
		CHECK(containedFlag(m));
		CHECK(primary(m) == "BowWeapon");
	}
	c->removeFromContain(horde);
	CHECK_FALSE(containedFlag(horde));
	for (const Object *m : *members)
	{
		CHECK(containedFlag(m)); // retail's own bytes: RW 0x87C25B sets the flag (RW 0x691059) on the members on the rider's exit
	}
}
