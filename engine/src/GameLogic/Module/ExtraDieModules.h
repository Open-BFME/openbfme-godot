// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (CreateObjectDie.h, FireWeaponWhenDeadBehavior.h are the
// donors of the shape; every field and step below is RotWK's).
//
// Lane MODULES-1: die modules the base game uses that the combat lanes left unported.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * CreateObjectDie (create RW 0x64C572, constructor RW 0x888D14, die vtable RW 0xC60FD4; data: the DieMux table RW 0xC76BD8 (extra 8) and RW 0xC61120:
//     CreationList (+0x38, RW 0x73A368: the ObjectCreationList by name through RW 0x5EFFE7, "None" is NULL, an unknown name NULL without an error),
//     DebrisPortionOfSelf (+0x3C, parseAsciiString), UpgradeRequired (+0x40, parseAsciiStringVector RW 0x42EED6)).
//     onDie RW 0x888F04: nothing unless the DieMux applies (RW 0x8D29A9); every UpgradeRequired name is looked up in TheUpgradeCenter (RW 0x66F5E5; an
//     unknown name is skipped): a PLAYER upgrade (+4 == 0) must be complete for the controlling player (RW 0x68B678 -> 0x6AC2AF), an OBJECT upgrade must be on
//     the object (RW 0x691421), else nothing happens; then the list (when set) is created with the object as primary and the killer
//     (TheGameLogic->findObjectByID(info source), RW 0x449681) as secondary (RW 0x5F0126: a retired list follows its replacement, then every nugget's slot 2).
//     DebrisPortionOfSelf is not read by onDie.
//   * FireWeaponWhenDeadBehavior (create RW 0x64C020, constructor RW 0x885EB6, 0xAC bytes; vtables: update RW 0xC5FFC0, mux RW 0xC5FF78, die RW 0xC5FF70;
//     data: RW 0xC067A0 StartsActive (+0x138), ActiveDuringConstruction (+0x139), DelayTime (+0x13C, parseDurationUnsignedInt), DeathWeapon (+0x17C,
//     RW 0x73AE79: the weapon template by name, NULL when unknown or "None"), WeaponOffset (+0x140, parseCoord3D); the upgrade table RW 0xC76AD8 (extra 8);
//     the DieMux table RW 0xC76BD8 (extra 0x14C)).
//     Constructor: the mux (RW 0x8D26E6), StartsActive gives the module its own upgrade at once (RW 0x855388); the delay counter (+0xA8) is DelayTime when
//     DelayTime > 0 (else 0); the update sleeps forever (RW 0x850C32).
//     Mux: the implementation (slot 10) and the removal (slot 8) are no-ops (RW 0x63F3BF, RW 0x8B88A8 returns false), isPermanent (slot 7) is false; the
//     module is "active" when its mux executed. resetUpgrade (slot 3) is RW 0x8D278A, the reset WITHOUT clearing the custom anim.
//     onDie RW 0x885FF3: with a delay counter above 0: the damage info is copied into the module (+0x2C, RW 0x7441E0) and the update wakes next frame.
//     Otherwise: nothing unless the mux executed or StartsActive, the DieMux applies (data + 0x14C), the object is not BUILD_BEING_CANCELED (status 0x56) and,
//     without ActiveDuringConstruction, not UNDER_CONSTRUCTION (status 2); the module's activation masks (slot 11): nothing when the object's upgrade mask
//     (+0x28C) or the controlling player's completed mask (+0x14C) holds any CONFLICTING bit (RW 0x8097D6); then, with a DeathWeapon, WeaponOffset goes through
//     the object's transform (RW 0x70BFD1: x' = (m02 z + m01 y) + m00 x + m03, the same order per row) and TheWeaponStore->createAndFireTempWeapon(weapon,
//     object, that position) (RW 0x6CF530).
//     Update RW 0x885E25: a delay counter above 0 counts down; when it reaches 0 the stored damage info goes to onDie again (which now fires); the update
//     returns 1 (every frame).
//   * UpgradeDie (create RW 0x64C87A, constructor RW 0x889E6B, die vtable RW 0xC61770; data: the DieMux table (extra 8) and RW 0xC06BF0: UpgradeToRemove (+0x38,
//     parseAsciiString)). onDie RW 0x889EB1: when the DieMux applies, the upgrade by name (RW 0x66F5E5; unknown: nothing): a PLAYER upgrade leaves the
//     controlling player (Player::removeUpgrade(u, false) RW 0x6AE60C); an OBJECT upgrade leaves the object's PRODUCER (Object + 0x78, found by id) when the
//     producer has it (RW 0x691421 / 0x691438). (The donor ZH UpgradeDie removes the producer's upgrade only.)
//   * HeroDie (create RW 0x6515F9, constructor RW 0x8C64B8, die vtable RW 0xC730A4; data: the DieMux table (extra 8, parsed but NOT consulted) and RW 0xC06BC0:
//     SpecialPowerTemplate (+0x38, RW 0x73B22F: the template by name from TheSpecialPowerStore)). onDie RW 0x8C64FE: the controlling player iterates its objects
//     (RW 0x6ABABD) with RW 0x8C642D: an object whose special power module for the template exists (RW 0x68C26D) gets setReadyFrame(now) (slot 0x20): the
//     power is ready at once.
// WHAT IS INFERENCE / NOT PORTED (stop S-980, reported per use):
//   * HeroDie walks the logic's object list for the player's objects (RW walks the player's own object list; every match gets the same frame);
//   * the OCL is created through ObjectCreationList::create(logic, object, object position), the position variant (RW slot 3) of SPELL-1's port, not RW's
//     object-pair variant (slot 2: the primary object and the killer); the nugget kinds and fields that port does not run are its own stop S-530;
//   * names (CreationList, DeathWeapon) are resolved at the death, not at parse time like RW (the same lists, unless a later INI replaces one);
//   * the death weapon fires through DeliverNuggets (the temporary weapon's damage nuggets at the position, as GettingBuiltBehavior's HealWeapon, S-651): the
//     temporary Weapon's own steps (fire FX / sound, projectile nuggets) are not delivered;
//   * the DieMux killer angle window is not ported (reported when consulted, as RefundDie).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <string>
#include <vector>

class CreateObjectDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                         ///< RW 0xC76BD8
	std::string m_creationList;                  ///< +0x38 (empty: "None")
	std::string m_debrisPortionOfSelf;           ///< +0x3C
	std::vector<std::string> m_upgradeRequired;  ///< +0x40
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CreateObjectDie : public BehaviorModule, public DieModuleInterface
{
public:
	CreateObjectDie(Thing *thing, const CreateObjectDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;
	// how many objects this module's deaths made (tests)
	unsigned createdCount() const { return m_created; }
	void crc(StateHasher &hasher) const override;

private:
	const CreateObjectDieModuleData *m_data;
	unsigned m_created = 0;
};

class FireWeaponWhenDeadBehaviorModuleData : public UpgradeModuleData
{
public:
	bool m_startsActive = false;              ///< +0x138
	bool m_activeDuringConstruction = false;  ///< +0x139
	unsigned m_delayTime = 0;                 ///< +0x13C (frames)
	Coord3D m_weaponOffset{ 0.0f, 0.0f, 0.0f }; ///< +0x140
	std::string m_deathWeapon;                ///< +0x17C (empty: "None")
	DieMuxData m_dieMux;                      ///< +0x14C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FireWeaponWhenDeadBehavior : public UpdateModule, public UpgradeMux, public DieModuleInterface
{
public:
	FireWeaponWhenDeadBehavior(Thing *thing, const FireWeaponWhenDeadBehaviorModuleData *data);
	UpgradeMux *getUpgrade() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;
	unsigned shots() const { return m_shots; }
	unsigned delayCounter() const { return m_delay; }

protected:
	void upgradeImplementation() override {} // RW slot 10: RW 0x63F3BF
	void processUpgradeRemoval() override {}  // RW slot 8: RW 0x8B88A8

private:
	void fire();
	const FireWeaponWhenDeadBehaviorModuleData *m_data;
	unsigned m_delay = 0;               ///< +0xA8
	DieModuleInterface::Event m_stored; ///< +0x2C (the damage info of a delayed death)
	unsigned m_shots = 0;
};

class UpgradeDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;           ///< RW 0xC76BD8
	std::string m_upgradeToRemove; ///< +0x38
	static void buildFieldParse(MultiIniFieldParse &p);
};

class UpgradeDie : public BehaviorModule, public DieModuleInterface
{
public:
	UpgradeDie(Thing *thing, const UpgradeDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;

private:
	const UpgradeDieModuleData *m_data;
};

class SpecialPowerTemplate;

class HeroDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                                   ///< RW 0xC76BD8 (parsed, not consulted by onDie)
	std::string m_specialPowerTemplateName;                ///< +0x38
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class HeroDie : public BehaviorModule, public DieModuleInterface
{
public:
	HeroDie(Thing *thing, const HeroDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;

private:
	const HeroDieModuleData *m_data;
};
