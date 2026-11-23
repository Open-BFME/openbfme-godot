// OpenBFME unit tests: LOCAL-87 -- WeaponChangeSpecialPowerModule::doSpecialPowerAtLocation / doSpecialPowerAtObject.
// Retail 2.01 (RotWK game.dat, caveat S-001):
//   * RW 0x008C82E2 (WeaponChangeSpecialPowerModule::doSpecialPowerAtObject): forwards to slot 0x28 doSpecialPower(options) (RW 0x008C8420).
//   * RW 0x008C82EE (WeaponChangeSpecialPowerModule::doSpecialPowerAtLocation): forwards to slot 0x28 doSpecialPower(options) (RW 0x008C8420).
// GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameClient/FXList.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/WeaponChangeSpecialPower.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "Common/INI.h"
#include "GameLogic/Weapon.h"

#include <string>

namespace
{
constexpr size_t kToggle1Flag = 24; // WEAPONSET_TOGGLE_1

const char *kLocal87Ini =
	"Weapon Local87Sword\n"
	"  AttackRange = 20.0\n"
	"  DelayBetweenShots = 1000\n"
	"  PreAttackDelay = 100\n"
	"  WeaponSpeed = 999999\n"
	"  DamageNugget\n"
	"    Damage = 10.0\n"
	"    DamageType = SLASH\n"
	"  End\n"
	"End\n"
	"Weapon Local87Torch\n"
	"  AttackRange = 20.0\n"
	"  DelayBetweenShots = 1000\n"
	"  PreAttackDelay = 100\n"
	"  WeaponSpeed = 999999\n"
	"  DamageNugget\n"
	"    Damage = 25.0\n"
	"    DamageType = FLAME\n"
	"  End\n"
	"End\n"
	"SpecialPower SpecialPower_Local87Toggle\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
	"  ReloadTime = 0\n"
	"End\n"
	"Object Local87Toggler\n"
	"  Side = Isengard\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY Local87Sword\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY Local87Torch\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = WeaponChangeSpecialPowerModule ModuleTag_Change\n"
	"    SpecialPowerTemplate = SpecialPower_Local87Toggle\n"
	"    FlagsUsedForToggle = WEAPONSET_TOGGLE_1\n"
	"    ToggleOnSleepFrames = 5\n"
	"    ToggleOffSleepFrames = 3\n"
	"  End\n"
	"End\n";

struct Local87World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;

	explicit Local87World(const char *ini) : combattest::CombatWorld("")
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *i) { ParseFXListDefinitionGlobal(i); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *i) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(i); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string loadErr = w.load(ini, INI_LOAD_OVERWRITE, "local87_powers.ini");
		REQUIRE_MESSAGE(loadErr.empty(), loadErr);
	}
	~Local87World()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
};

WeaponChangeSpecialPowerModule *changeOf(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (auto *wc = dynamic_cast<WeaponChangeSpecialPowerModule *>(m.get()))
		{
			return wc;
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

// RW 0x008C82EE (doSpecialPowerAtLocation) and RW 0x008C82E2 (doSpecialPowerAtObject) both forward to slot 0x28 doSpecialPower(options) (RW 0x008C8420).
TEST_CASE("LOCAL-87 WeaponChangeSpecialPowerModule::doSpecialPowerAtLocation and doSpecialPowerAtObject forward to doSpecialPower (RW 0x008C82EE, RW 0x008C82E2)")
{
	Local87World w(kLocal87Ini);
	Object *toggler = w.unit("Local87Toggler", 'A', 200.0f, 200.0f);
	Object *other = w.unit("Local87Toggler", 'B', 300.0f, 200.0f);
	REQUIRE(toggler != nullptr);
	REQUIRE(other != nullptr);
	WeaponChangeSpecialPowerModule *wc = changeOf(*toggler);
	REQUIRE(wc != nullptr);
	REQUIRE(weaponOf(*toggler) == "Local87Sword");
	CHECK_FALSE(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));

	// Location form (RW 0x008C82EE) toggles WEAPONSET_TOGGLE_1 on and switches to Local87Torch.
	const Coord3D loc{ 250.0f, 250.0f, 0.0f };
	wc->doSpecialPowerAtLocation(loc, 0);
	CHECK(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));
	CHECK(weaponOf(*toggler) == "Local87Torch");

	// Advance past ToggleOnSleepFrames (5) so the unit is no longer disabled (type 4).
	for (int i = 0; i < 6; ++i)
	{
		for (int phase = 1; phase <= 6; ++phase) { toggler->logic().update(phase); }
	}

	// Object form (RW 0x008C82E2) toggles WEAPONSET_TOGGLE_1 back off and switches to Local87Sword.
	wc->doSpecialPowerAtObject(other, 0);
	CHECK_FALSE(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));
	CHECK(weaponOf(*toggler) == "Local87Sword");
}

// Neighbouring behaviour: ToggleOnSleepFrames / ToggleOffSleepFrames disable (RW 0x008C8420) when triggered via doSpecialPowerAtLocation (RW 0x008C82EE).
TEST_CASE("LOCAL-87 WeaponChangeSpecialPowerModule::doSpecialPowerAtLocation applies ToggleOn/ToggleOff sleep frames (RW 0x008C82EE, RW 0x008C8420)")
{
	Local87World w(kLocal87Ini);
	Object *toggler = w.unit("Local87Toggler", 'A', 200.0f, 200.0f);
	REQUIRE(toggler != nullptr);
	WeaponChangeSpecialPowerModule *wc = changeOf(*toggler);
	REQUIRE(wc != nullptr);

	const Coord3D loc{ 210.0f, 210.0f, 0.0f };
	wc->doSpecialPowerAtLocation(loc, 0);
	CHECK(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));
	CHECK(toggler->getDisabledMask() == (1ULL << 4));

	for (int i = 0; i < 6; ++i)
	{
		for (int phase = 1; phase <= 6; ++phase) { toggler->logic().update(phase); }
	}
	CHECK(toggler->getDisabledMask() == DISABLEDMASK_NONE);

	wc->doSpecialPowerAtLocation(loc, 0);
	CHECK_FALSE(BitFlagsTest(toggler->getWeaponSetFlags(), kToggle1Flag));
	CHECK(toggler->getDisabledMask() == (1ULL << 4));
}
