// OpenBFME unit tests: Port 256 -- the weapon-switching powers WeaponModeSpecialPowerUpdate and WeaponChangeSpecialPowerModule.
//
// What RotWK does, read with rw.py from game.dat 2.01 (the class's data tables and vtables are cited in GameLogic/Module/HeroAbilityModules.h and
// WeaponChangeSpecialPower.h):
//   * WeaponModeSpecialPowerUpdate::initiate (RW 0x89841E): with LockWeaponSlot 5 ("none", the data default RW 0x8984DC) the data's WeaponSetFlags
//     (data + 0x24) are not empty (RW 0x68FDF7 is a popcount over the four words, RW 0x89843E) and every set bit of 0 .. 0x67 goes ON through
//     RW 0x691059 -- a method of the OBJECT, which writes Object + 0x38C, runs the weapon set update (RW 0x6C99E2) and mirrors the model condition the
//     table RW 0xC16958 names (RW 0x68B53C), including the SWAPPING_TO_WEAPONSET_1 special state for WEAPONSET_TOGGLE_1 .. 3. RW passes the object,
//     never a weapons object, and asks the object's own words: an object whose template has no WeaponSet at all (a horde container is one) keeps the
//     bits too. The end of the mode (RW 0x8983B5) clears the same bits with RW 0x691106, the same object method, the same way.
//   * WeaponChangeSpecialPowerModule::doSpecialPower (RW 0x8C8420): per bit of FlagsUsedForToggle (data + 0x7C) the object's flag test RW 0x68DEEC,
//     then RW 0x691106 when it is on and RW 0x691059 when it is off -- the neighbour behaviour this lane holds in place.
//
// The expectations come from the RotWK rules above and the synthetic INI below, never from this engine's output.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/INI.h"
#include "Common/SpecialPower.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/WeaponChangeSpecialPower.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponSetToggle.h"

#include <string>

using namespace combattest;

namespace
{
// WEAPONSET_TOGGLE_1 is weapon set flag 24 of TheWeaponConditionNames (the toggle the Rohirrim's bow / spear and the charge modes use); the model
// condition the table RW 0xC16958 maps it to is 0x12D (301), which is why the flag is visible on the model at all.
constexpr size_t kToggle1Flag = 24;
constexpr int kToggle1ModelCondition = 0x12D;

const char kPort256Ini[] =
	"SpecialPower SpecialPower_Port256Mode\n"
	"  Enum = SPECIAL_HERO_MODE\n"
	"  ReloadTime = 1000\n"
	"End\n"
	"Weapon Port256Sword\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon Port256Bow\n"
	"  AttackRange = 120\n"
	"  DelayBetweenShots = 1000\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Locomotor Port256Loco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  MinSpeed = 0\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  StickToGround = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n"
	// the mode module on a template with no WeaponSet at all: RW 0x691059 still writes the object's own words
	"Object Port256Bare\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPower_Port256Mode\n"
	"  End\n"
	"  Behavior = WeaponModeSpecialPowerUpdate ModuleTag_Mode\n"
	"    SpecialPowerTemplate = SpecialPower_Port256Mode\n"
	"    Duration = 1000\n"
	"    WeaponSetFlags = WEAPONSET_TOGGLE_1\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = Port256Loco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// the same module on a template with two weapon sets: the flag has to pick the second set and the sword has to come back at the end
	"Object Port256Fighter\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY Port256Sword\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY Port256Bow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPower_Port256Mode\n"
	"  End\n"
	"  Behavior = WeaponModeSpecialPowerUpdate ModuleTag_Mode\n"
	"    SpecialPowerTemplate = SpecialPower_Port256Mode\n"
	"    Duration = 1000\n"
	"    WeaponSetFlags = WEAPONSET_TOGGLE_1\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = Port256Loco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// the neighbour class: the same flag toggled by a power, no duration, no sleep frames
	"Object Port256Toggler\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY Port256Sword\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY Port256Bow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = WeaponChangeSpecialPowerModule ModuleTag_Change\n"
	"    SpecialPowerTemplate = SpecialPower_Port256Mode\n"
	"    FlagsUsedForToggle = WEAPONSET_TOGGLE_1\n"
	"    ToggleOnSleepFrames = 0\n"
	"    ToggleOffSleepFrames = 0\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = Port256Loco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

// The world of engine/tests/test_local_73.cpp's PowerWorld: the special power, FX list and attribute modifier stores are installed over the combat
// fixture's, then the object INI is loaded through them.
struct Port256World : CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	explicit Port256World(const char *ini) : CombatWorld("")
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *i) { ParseFXListDefinitionGlobal(i); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *i) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(i); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string loadErr = w.load(ini, INI_LOAD_OVERWRITE, "port256_powers.ini");
		REQUIRE_MESSAGE(loadErr.empty(), loadErr);
	}
	~Port256World()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
};

WeaponModeSpecialPowerUpdate *modeOf(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (auto *w = dynamic_cast<WeaponModeSpecialPowerUpdate *>(m.get()))
		{
			return w;
		}
	}
	return nullptr;
}

WeaponChangeSpecialPowerModule *changeOf(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (auto *w = dynamic_cast<WeaponChangeSpecialPowerModule *>(m.get()))
		{
			return w;
		}
	}
	return nullptr;
}

std::string weaponOf(const Object &o)
{
	const ObjectWeapons *w = o.getWeapons();
	if (!w || !w->currentWeapon() || !w->currentWeapon()->getTemplate())
	{
		return std::string();
	}
	return w->currentWeapon()->getTemplate()->getName();
}
} // namespace

// RW 0x89841E -> RW 0x691059: the mode's flag goes on the OBJECT, so a template without any WeaponSet gets the bit in its own words and the model
// condition (RW 0xC16958 -> 0x12D) too; RW 0x8983B5 -> RW 0x691106 takes both away when the Duration is over.
TEST_CASE("PORT-256 WeaponModeSpecialPowerUpdate sets and clears the weapon set flag of an object with no weapon set (RW 0x691059 / 0x691106 on the object)")
{
	Port256World w(kPort256Ini);
	Object *bare = w.unit("Port256Bare", 'A', 200, 200);
	REQUIRE(bare != nullptr);
	REQUIRE(bare->getWeapons() == nullptr); // no WeaponSet block at all: the object keeps the bits itself
	WeaponModeSpecialPowerUpdate *wm = modeOf(*bare);
	REQUIRE(wm != nullptr);
	CHECK_FALSE(BitFlagsTest(bare->getWeaponSetFlags(), kToggle1Flag));
	REQUIRE(WeaponSetToggle::modelConditionForWeaponSetBit((int)kToggle1Flag) == kToggle1ModelCondition);
	CHECK_FALSE(bare->testModelCondition(kToggle1ModelCondition));

	wm->initiateIntentToDoSpecialPower(wm->getSpecialPowerTemplate(), bare, bare->getPosition(), 0u, 0);
	CHECK(wm->active());
	CHECK(BitFlagsTest(bare->getWeaponSetFlags(), kToggle1Flag));
	CHECK(bare->testModelCondition(kToggle1ModelCondition));

	w.runUntil([&]() { return !wm->active(); }, 30);
	CHECK_FALSE(wm->active());
	CHECK_FALSE(BitFlagsTest(bare->getWeaponSetFlags(), kToggle1Flag));
	CHECK_FALSE(bare->testModelCondition(kToggle1ModelCondition));
}

// the neighbour: the same module on a template with two weapon sets still picks the WEAPONSET_TOGGLE_1 set (RW 0x6C99E2 through RW 0x691059) and
// gives the sword back when the mode ends
TEST_CASE("PORT-256 WeaponModeSpecialPowerUpdate switches the weapon set of an armed object and switches back at the end")
{
	Port256World w(kPort256Ini);
	Object *fighter = w.unit("Port256Fighter", 'A', 300, 300);
	REQUIRE(fighter != nullptr);
	REQUIRE(fighter->getWeapons() != nullptr);
	REQUIRE(weaponOf(*fighter) == "Port256Sword");
	WeaponModeSpecialPowerUpdate *wm = modeOf(*fighter);
	REQUIRE(wm != nullptr);

	wm->initiateIntentToDoSpecialPower(wm->getSpecialPowerTemplate(), fighter, fighter->getPosition(), 0u, 0);
	CHECK(wm->active());
	CHECK(BitFlagsTest(fighter->getWeaponSetFlags(), kToggle1Flag));
	CHECK(weaponOf(*fighter) == "Port256Bow");
	CHECK(fighter->testModelCondition(kToggle1ModelCondition));

	w.runUntil([&]() { return !wm->active(); }, 30);
	CHECK_FALSE(BitFlagsTest(fighter->getWeaponSetFlags(), kToggle1Flag));
	CHECK(weaponOf(*fighter) == "Port256Sword");
}

// RW 0x8C8420 -> RW 0x68DEEC then RW 0x691106 / RW 0x691059: the other class toggles the same bit on every use
TEST_CASE("PORT-256 WeaponChangeSpecialPowerModule toggles the weapon set flag on each use (RW 0x8C8420)")
{
	Port256World w(kPort256Ini);
	Object *toggler = w.unit("Port256Toggler", 'A', 400, 400);
	REQUIRE(toggler != nullptr);
	REQUIRE(toggler->getWeapons() != nullptr);
	REQUIRE(weaponOf(*toggler) == "Port256Sword");
	WeaponChangeSpecialPowerModule *wc = changeOf(*toggler);
	REQUIRE(wc != nullptr);
	CHECK_FALSE(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));

	wc->doSpecialPower(0);
	CHECK(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));
	CHECK(weaponOf(*toggler) == "Port256Bow");

	wc->doSpecialPower(0);
	CHECK_FALSE(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));
	CHECK(weaponOf(*toggler) == "Port256Sword");
}
