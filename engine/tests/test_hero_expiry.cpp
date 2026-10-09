// OpenBFME tests (lane HERO-1 review r2): SpecialAbilityUpdate's sleep follows the effect expiry (RW 0x85266D), and the owned special weapon enters the module hash. GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

// RW 0x85266D keeps the update scheduled while the effect expiry (+ 0x2C) is armed: a finished HeroMode cast must wake at its expiry and clear
// WEAPONSET_HERO_MODE (RW 0x8958F8), not sleep forever with the special weapon set
TEST_CASE("HeroModeSpecialAbilityUpdate: a finished cast stays scheduled until its effect expires and then restores the weapon set (RW 0x85266D / 0x8958F8)")
{
	combattest::CombatWorld f;
	SpecialAbilityModules::registerAll(f.w.modules);
	REQUIRE(f.w.load("Object ExpiryHero\n"
					 " KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
					 " WeaponSet\n  Conditions = None\n  Weapon = PRIMARY SwordWeapon\n End\n"
					 " WeaponSet\n  Conditions = WEAPONSET_HERO_MODE\n  Weapon = PRIMARY BowWeapon\n End\n"
					 " Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
					 " Behavior = AIUpdateInterface AiTag\n End\n"
					 " Behavior = HeroModeSpecialAbilityUpdate AbilityTag\n"
					 "  UnpackTime = 200\n  PreparationTime = 200\n  IgnoreFacingCheck = Yes\n  HeroEffectDuration = 4000\n"
					 " End\nEnd\n")
				.empty());
	Object *o = f.unit("ExpiryHero", 'A', 300, 300);
	REQUIRE(o);
	auto *p = dynamic_cast<HeroModeSpecialAbilityUpdate *>(o->findModule("HeroModeSpecialAbilityUpdate"));
	REQUIRE(p);
	p->initiateIntentToDoSpecialPower(nullptr, nullptr, nullptr, 0, 0);
	f.frames(8);
	REQUIRE(p->abilitiesTriggered() == 1);
	REQUIRE_FALSE(p->isActive());
	CHECK(p->friend_getNextCallFrame() != (UnsignedInt)UPDATE_SLEEP_FOREVER); // the armed expiry (20 frames) keeps it scheduled
	CHECK(o->getWeapons()->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "BowWeapon");
	f.frames(25);
	CHECK(o->getWeapons()->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "SwordWeapon");
}

// the owned weapon's ammo and timers are state: they enter the module hash
TEST_CASE("WeaponFireSpecialAbilityUpdate: the owned special weapon's state enters the module hash")
{
	combattest::CombatWorld f;
	SpecialAbilityModules::registerAll(f.w.modules);
	REQUIRE(f.w.load("Object HashHero\n"
					 " KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
					 " WeaponSet\n  Conditions = None\n  Weapon = PRIMARY SwordWeapon\n End\n"
					 " Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
					 " Behavior = AIUpdateInterface AiTag\n End\n"
					 " Behavior = WeaponFireSpecialAbilityUpdate AbilityTag\n  SpecialWeapon = BowWeapon\n  WhichSpecialWeapon = 1\n End\nEnd\n")
				.empty());
	Object *o = f.unit("HashHero", 'A', 300, 300);
	REQUIRE(o);
	auto *p = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(o->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(p);
	Weapon *w = p->ownedWeapon();
	REQUIRE(w != nullptr);
	StateHasher a;
	p->crc(a);
	w->setAmmoInClip(0);
	StateHasher b;
	p->crc(b);
	CHECK(a.value() != b.value());
}
