// OpenBFME. GPL-3.0.
// Lane MODULES-1: create modules the base game uses that no lane ported yet. RotWK classes (no ZH donor). The create module base (RW 0x8BCE38, on the
// BehaviorModule constructor RW 0x6530B4) sets the "need to run onBuildComplete" flag (+0x14) to true; its create interface (+0x10) has slot 0 onCreate,
// slot 1 onBuildComplete, slot 3 the flag (RW 0x4986C4).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * ExperienceLevelCreate (create RW 0x65092C, constructor RW 0x8BD2BC, create vtable RW 0xC70214; table RW 0xC7026C: LevelToGrant (+8, parseInt), MPOnly
//     (+0xC, parseBool)). onCreate is a no-op; onBuildComplete RW 0x8BD375 (the flag is not consulted): with MPOnly, nothing unless the game is a multiplayer game
//     (RW 0x625456); then RW 0x8BD342: the experience tracker (Object + 0x26C) gainExpForLevel(LevelToGrant - its rank (+0x24), false, false) (RW 0x79DA0A).
//   * LockWeaponCreate (create RW 0x650730, constructor RW 0x8BCD4F, create vtable RW 0xC6FFA4; table RW 0xC6FFF0: SlotToLock (+8, parseLookupList RW 0x42E9B7
//     over RW 0xC16928: PRIMARY 0 .. QUINARY 4)). onBuildComplete RW 0x8BCE0A: the flag is cleared (not consulted), then Object::setWeaponLock(slot,
//     LOCKED_PERMANENTLY) (RW 0x69121A: the contain module (+0x258) slot 0x168 first, then the object status SWITCHED_WEAPONS (0x51) = (permanent and slot != 0),
//     then the weapon set's lock RW 0x6C97F9).
//   * InheritUpgradeCreate (create RW 0x650A25, constructor RW 0x8BD587, create vtable RW 0xC7039C; table RW 0xC703E0: Radius (+8, parseReal), Upgrade (+0xC,
//     the upgrade mask RW 0x66F603), ObjectFilter (+0x9C, RW 0x76392F)). onCreate is a no-op; onBuildComplete RW 0x8BD80E: when the flag (slot 3) is set, it
//     is cleared and RW 0x8BD701 runs: ThePartitionManager iterates the objects within Radius of the object's position (RW 0xA39340, distance type 1 = from
//     centre 3D) through a filter of the ObjectFilter with the object's controlling player; for every object found whose controlling player is the object's own
//     and whose upgrade mask holds ALL the Upgrade bits (RW 0x6AACB3), every bit of the Upgrade mask (0 .. 0x47F) is given to the object (Object::giveUpgrade
//     RW 0x69388B of TheUpgradeCenter->findUpgradeByMaskBit RW 0x66F218).
// WHAT IS INFERENCE / NOT PORTED (stop S-981, reported per use):
//   * InheritUpgradeCreate's partition query is a scan of the logic's object list (list order) with the 3D centre distance <= Radius and
//     ObjectFilterMatch::allows; the partition manager's cell order and its exact range test are not read (the effect does not depend on the order: every
//     match gives the same bits);
//   * LockWeaponCreate on an object with a contain module: the contain's slot 0x168 (its weapon lock pass-through) is not ported.

#pragma once

#include "Common/INI.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/ObjectFilter.h"

class ExperienceLevelCreateModuleData : public ModuleData
{
public:
	int m_levelToGrant = -1; ///< +8 (the data constructor sets -1: RW 0x8BD363 `or [eax+8], -1`; a negative count of levels grants nothing)
	bool m_mpOnly = false;  ///< +0xC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ExperienceLevelCreate : public BehaviorModule, public CreateModuleInterface
{
public:
	ExperienceLevelCreate(Thing *thing, const ExperienceLevelCreateModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override;

private:
	const ExperienceLevelCreateModuleData *m_data;
};

class LockWeaponCreateModuleData : public ModuleData
{
public:
	int m_slotToLock = 0; ///< +8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class LockWeaponCreate : public BehaviorModule, public CreateModuleInterface
{
public:
	LockWeaponCreate(Thing *thing, const LockWeaponCreateModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override;
	void crc(StateHasher &hasher) const override;

private:
	const LockWeaponCreateModuleData *m_data;
	bool m_needToRunOnBuildComplete = true; ///< +0x14
};

class InheritUpgradeCreateModuleData : public ModuleData
{
public:
	float m_radius = 0.0f;           ///< +8
	UpgradeMaskType m_upgrade;       ///< +0xC
	ObjectFilter m_objectFilter;     ///< +0x9C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class InheritUpgradeCreate : public BehaviorModule, public CreateModuleInterface
{
public:
	InheritUpgradeCreate(Thing *thing, const InheritUpgradeCreateModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override;
	void crc(StateHasher &hasher) const override;
	unsigned inherited() const { return m_inherited; }

private:
	const InheritUpgradeCreateModuleData *m_data;
	bool m_needToRunOnBuildComplete = true; ///< +0x14
	unsigned m_inherited = 0;               ///< how many upgrades the module gave (tests)
};
