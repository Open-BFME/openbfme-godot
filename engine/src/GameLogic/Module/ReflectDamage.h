// OpenBFME. GPL-3.0.
//
// ReflectDamage (lane MODGAP-1; editions: both RotWK 2.01 and BFME2 1.06):
// Deals a share of incoming damage back to the attacker as DAMAGE_REFLECTED.
// Ported from the RotWK binary (caveat S-001); the BFME2 decompilation (Open-BFME-2
// Code/GameEngine/Source/GameLogic/Object/Damage/ReflectDamageOnDamage.cpp) matches.
//
// TARGET FACTS (RotWK game.dat):
//   * data (ctor RW 0x8BECE3, parse RW 0x8BEC6E over table RW 0xC70928):
//     DamageTypesToReflect + 0x08 (RW 0x73A5C9, damage type flags 1 << (type - 1); starts 0),
//     ReflectDamagePercentage + 0x0C (RW 0x42EEFA, percent as real; starts 0),
//     MinimumDamageToReflect + 0x10 (RW 0x42ED00; starts 0).
//   * module (ctor RW 0x8BECFD over DamageModule RW 0x8BD869; vtables RW 0xC70974, +0xC RW 0xC704B0, +0x10 RW 0xC70968): no state;
//   * onDamage (RW 0x8BED43, damage interface slot 0): damage type 13 (REFLECTED) is never reflected; a type
//     whose bit (type - 1) is not in DamageTypesToReflect is skipped; source object (DamageInfo + 0x08, RW 0x449681)
//     must still exist. Amount is INPUT amount (DamageInfo + 0x20) * ReflectDamagePercentage (mulss), clamped up to
//     MinimumDamageToReflect (comiss; ja). Source takes attemptDamage (RW 0x698E7D) with type 13 (REFLECTED),
//     damageFXOverride = 2, deathType = DEATH_CRUSHED (2), sourceID = self->getID().
//   * onHealing and onBodyDamageStateChange do nothing; xfer: DamageModule's (version 1 + BehaviorModule's).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DamageModule.h"

class ModuleFactory;

class ReflectDamageModuleData : public ModuleData
{
public:
	unsigned m_damageTypes = 0;        // + 0x08
	float m_reflectPercentage = 0.0f;  // + 0x0C
	float m_reflectMinimum = 0.0f;     // + 0x10
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8BEC6E
};

// RW 0x008BEC98 (getModuleName), RW 0x008BEC9E (getModuleNameKey), RW 0x008BED27 (scalar deleting dtor -> RW 0x008BEC7F)
// RW 0x00650B28 (friend_newModuleInstance), RW 0x00650B60 (ReflectDamageModuleData::friend_newModuleData)
class ReflectDamage : public BehaviorModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(ReflectDamage)
	ReflectDamage(Thing *thing, const ReflectDamageModuleData *data); // RW 0x8BECFD
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;                                                        // RW 0x8BED43
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {}
	const ReflectDamageModuleData *data() const { return m_data; }
	static void registerClass(ModuleFactory &modules);

private:
	const ReflectDamageModuleData *m_data;
};
