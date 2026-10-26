// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/Module/FireWeaponWhenDamagedBehavior.h,
// Source/GameLogic/Object/Behavior/FireWeaponWhenDamagedBehavior.cpp: the shape; every field and step below is RotWK's).
//
// FireWeaponWhenDamagedBehavior (lane MODS-3): an update / damage / upgrade module that fires a reaction weapon when its object takes damage of the right
// type and amount, and a continuous weapon every frame, each chosen by the body's damage state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json: registration site RW 0x6585EA, tables RW 0xC06698
// and RW 0xC76AD8 (extra 8)):
//   * data: create RW 0x653466 (0x164 bytes), constructor RW 0x6533E5 (the upgrade data RW 0x653173 at +8; DamageTypes -1 = every type, DamageAmount 0,
//     StartsActive false, every weapon null), buildFieldParse RW 0x653444: table RW 0xC06698 (offset 0) then the upgrade table RW 0xC76AD8 (RW 0x8D26E0,
//     offset 8). RW 0xC06698: StartsActive (+0x138, parseBool), ReactionWeaponPristine / Damaged / ReallyDamaged / Rubble (+0x144 .. +0x150) and
//     ContinuousWeaponPristine / Damaged / ReallyDamaged / Rubble (+0x154 .. +0x160) (RW 0x73AE79: the weapon template by name, NULL when unknown or
//     "None"), DamageTypes (+0x13C, RW 0x73A5C9: the damage type flags, bit 1 << (type - 1)), DamageAmount (+0x140, parseReal RW 0x42ED00).
//   * module: constructor RW 0x885A6F (UpdateModule RW 0x653114; the mux at + 0x20 (RW 0x8D26E6, vtable RW 0xC5FE20), the damage interface at + 0x28
//     (vtable RW 0xC5FE10), the eight Weapon pointers at + 0x2C .. + 0x48 in the data's order). Each named weapon: TheWeaponStore->allocateNewWeapon(
//     template, slot 0) (RW 0x68B150), Weapon::reloadAmmo(object) (RW 0x6CEE4C), the weapon's owner (+ 8) = the object's id (+ 0x74). Then StartsActive
//     gives the module its own upgrade (RW 0x855388); then the update wakes next frame (RW 0x850C32(obj, 1)) when the mux executed (slot 0) and any
//     ContinuousWeapon is set, else it sleeps forever (0x3FFFFFFF).
//   * mux (vtable RW 0xC5FE20): slot 4 / 7 (isPermanent) RW 0x9188EB false (the data's Permanent is not consulted), slot 6 postUpgradeCheck RW 0x63F3BF
//     no-op, slot 8 the removal RW 0x8B88A8 (its own slot 9 setUpgradeExecuted(false)), slot 10 the implementation RW 0x8554D6: the update wakes next
//     frame (RW 0x850C32(obj, 1)); slot 11 RW 0x885A39 (data + 8 / + 0x98), slots 12 / 15 RW 0x885A60 (RW 0x9F3A3C, no-op), slots 13 / 14 RW 0x887444 /
//     0x88744E (data + 0x134 / + 0x135).
//   * onDamage RW 0x885CD5 (damage vtable slot 0; slot 1 onHealing RW 0x9F3A3C and slot 2 onBodyDamageStateChange RW 0x918BA0 do nothing): nothing unless
//     the mux executed (slot 0); nothing unless DamageTypes holds bit (DamageInfo + 0x10 - 1) (x86 `shl` by cl: the count is taken mod 32); nothing when
//     DamageAmount > the amount dealt (DamageInfo + 0x70; comiss / ja: a NaN fires); then the body's damage state (Object + 0x25C, vslot 0x24): RUBBLE (3),
//     REALLYDAMAGED (2), DAMAGED (1), else PRISTINE picks the reaction weapon; a set weapon whose status (RW 0x6CDCE7) is READY_TO_FIRE (0) fires at the
//     object's own position (Weapon::forceFireWeapon(object, &object + 0x38), RW 0x6CF3D2).
//   * update RW 0x885D90: the mux not executed sleeps forever; else the same choice and fire with the continuous weapons; returns 1 (every frame).
//   * xfer RW 0x88588B: UpdateModule (RW 0x850C60), the mux (RW 0x8D27E8), then (not a light CRC) version (1, 1) and per weapon in the data's order a
//     present flag and the weapon's snapshot.
//   * BFME2 1.06 cross-check (decomp FireWeaponWhenDamagedBehaviorModuleDataCtor.cpp, RVA 0x253682): the same eleven fields 0x20 lower (+0x118 .. +0x140).
//   * ZH (donor): the same fields and steps (the ZH data has no upgrade table offset; ZH tests the actual damage dealt with `<` and fires through
//     forceFireWeapon at the object's position).
// Edain Unchained's engine change, feature ModEngineFeature::EdainUnchainedFireAtAttacker (lane MODS-3, logs/mods-engine-edits-catalogue.md "5. fire-at-attacker",
// its game.dat section .faa, decoded against the retail binary): the data grows to 0x168 with FireAtAttacker (+0x164, parseBool, default No: the constructor
// wrapper 0xEE60E0 clears it after RW 0x6533E5); the table 0xEE6010 is RW 0xC06698 plus that row. The hook 0xEE60F0 replaces onDamage's fire (RW 0x885D81,
// reached by all four damage states once the weapon is READY_TO_FIRE): with FireAtAttacker, the damage's source (DamageInfo + 8) found by
// TheGameLogic->findObjectByID (RW 0x449681) is fired at through RW 0x6CF3AE (privateFireWeapon RW 0x6CEF6D with the source as victim, its argument 6
// (ignore the ranges) = 1 and argument 7 (a projectile detonation: the weapon's nuggets are delivered at the victim at once, RW 0x6CB7BD) = 1; the
// catalogue's "fireWeapon at an object" is this detonation variant); no source object: retail's fire at the own position. The update's continuous
// weapons are unchanged. Without the feature, FireAtAttacker is an unknown field (retail's INI error).
// WHAT IS INFERENCE (OpenBFME):
//   * the weapon names are resolved by the module's constructor (RW resolves them at the parse: the same templates unless a later INI replaces one);
//   * an object without a WeaponSet fires through an ObjectWeapons host made for it (as FireWeaponUpdate, S-924: the shot's bonus conditions and attribute
//     modifiers are the object's);
//   * the snapshot is OpenBFME native (S-4190): the module's fields in RW's order, plus the shot counters.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <memory>
#include <string>

class ModuleFactory;
class ObjectWeapons;
class Weapon;

class FireWeaponWhenDamagedBehaviorModuleData : public UpgradeModuleData
{
public:
	enum
	{
		PRISTINE = 0,
		DAMAGED,
		REALLY_DAMAGED,
		RUBBLE,
		STATE_COUNT
	};
	bool m_startsActive = false;              ///< +0x138
	unsigned m_damageTypes = 0xFFFFFFFFu;     ///< +0x13C (every type)
	float m_damageAmount = 0.0f;              ///< +0x140
	std::string m_reactionWeapon[STATE_COUNT];   ///< +0x144 .. +0x150 (empty: "None")
	std::string m_continuousWeapon[STATE_COUNT]; ///< +0x154 .. +0x160
	bool m_fireAtAttacker = false;            ///< +0x164 (Edain Unchained's FireAtAttacker; parsed only with the feature)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FireWeaponWhenDamagedBehavior : public UpdateModule, public UpgradeMux, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(FireWeaponWhenDamagedBehavior)
	FireWeaponWhenDamagedBehavior(Thing *thing, const FireWeaponWhenDamagedBehaviorModuleData *data);
	~FireWeaponWhenDamagedBehavior() override;
	UpgradeMux *getUpgrade() override { return this; }
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;                                 // RW 0x885CD5
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {} // RW 0x918BA0
	UpdateSleepTime update() override;                                               // RW 0x885D90
	bool isPermanent() const override { return false; }                             // mux slot 7: RW 0x9188EB
	void crc(StateHasher &hasher) const override;

	// the module's weapons (null when the data names none): tests
	Weapon *reactionWeapon(int state) const { return m_reaction[state].get(); }
	Weapon *continuousWeapon(int state) const { return m_continuous[state].get(); }
	// shots fired (OpenBFME counters: tests and the hash): reaction shots at the own position, at the attacker (FireAtAttacker), continuous shots
	unsigned long long reactionShots() const { return m_reactionShots; }
	unsigned long long attackerShots() const { return m_attackerShots; }
	unsigned long long continuousShots() const { return m_continuousShots; }

	static void registerModule(ModuleFactory &modules);

protected:
	void upgradeImplementation() override;                         // mux slot 10: RW 0x8554D6
	void processUpgradeRemoval() override { setUpgradeExecuted(false); } // mux slot 8: RW 0x8B88A8 (slot 9 with false)

private:
	ObjectWeapons &weaponHost();
	int stateIndex() const; // the body's damage state as the weapon index (RW 0x885D12 .. 0x885D6E)
	const FireWeaponWhenDamagedBehaviorModuleData *m_data;
	std::unique_ptr<Weapon> m_reaction[FireWeaponWhenDamagedBehaviorModuleData::STATE_COUNT];   ///< + 0x2C .. + 0x38
	std::unique_ptr<Weapon> m_continuous[FireWeaponWhenDamagedBehaviorModuleData::STATE_COUNT]; ///< + 0x3C .. + 0x48
	std::unique_ptr<ObjectWeapons> m_ownHost; ///< S-924: the weapon host of an object without a WeaponSet
	unsigned long long m_reactionShots = 0;
	unsigned long long m_attackerShots = 0;
	unsigned long long m_continuousShots = 0;
};
