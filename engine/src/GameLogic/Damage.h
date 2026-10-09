// OpenBFME. GPL-3.0.
//
// Damage vocabulary: the damage types, death types, damage FX types and the Weapon-side enumerations the damage
// pipeline and the INI blocks share. Port of ZH GameEngine/Include/GameLogic/Damage.h with RotWK's lists. Lane WEAPON-1.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address):
//   * DamageType: 28 names, RW 0xDA15A8 (FORCE .. POISON, FROST). BFME2 1.06 has 27 (no FROST; Open-BFME-2 Armor decompile),
//     ZH has 38. The Armor block resolves `Armor = <name> <percent>` against this list with scanIndexList.
//   * DeathType: 24 names, RW 0xDA1630 (the Weapon block's DeathType index list, value = index) and an identical list at
//     RW 0xDA39E8 (the DeathTypeFlags mask list, bit = index - 1; GameLogic/BitFlags.h).
//   * DamageFXType: 36 names, RW 0xDA1510 (Weapon DamageFXType / DamageSubType; DamageFX block rows).
// The names are generated into WeaponNames.cpp from tests/data/weapon1/list_*.tsv (tools/weapon/gen_names.py).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectFilter.h"

#include <cstdint>

// RW 0xDA15A8. Value = index in the list.
enum DamageType
{
	DAMAGE_FORCE = 0,
	DAMAGE_CRUSH,
	DAMAGE_SLASH,
	DAMAGE_PIERCE,
	DAMAGE_SIEGE,
	DAMAGE_STRUCTURAL,
	DAMAGE_FLAME,
	DAMAGE_HEALING,
	DAMAGE_UNRESISTABLE,
	DAMAGE_WATER,
	DAMAGE_PENALTY,
	DAMAGE_FALLING,
	DAMAGE_TOPPLING,
	DAMAGE_REFLECTED,
	DAMAGE_PASSENGER,
	DAMAGE_MAGIC,
	DAMAGE_CHOP,
	DAMAGE_HERO,
	DAMAGE_SPECIALIST,
	DAMAGE_URUK,
	DAMAGE_HERO_RANGED,
	DAMAGE_FLY_INTO,
	DAMAGE_UNDEFINED,
	DAMAGE_LOGICAL_FIRE,
	DAMAGE_CAVALRY,
	DAMAGE_CAVALRY_RANGED,
	DAMAGE_POISON,
	DAMAGE_FROST,

	DAMAGE_NUM_TYPES
};

// RW 0xDA1630. Value = index in the list.
enum DeathType
{
	DEATH_NORMAL = 0,
	DEATH_NONE,
	DEATH_CRUSHED,
	DEATH_BURNED,
	DEATH_EXPLODED,
	DEATH_POISONED,
	DEATH_TOPPLED,
	DEATH_FLOODED,
	DEATH_SUICIDED,
	DEATH_LASERED,
	DEATH_DETONATED,
	DEATH_SPLATTED,
	DEATH_POISONED_BETA,
	DEATH_EXTRA_2,
	DEATH_EXTRA_3,
	DEATH_EXTRA_4,
	DEATH_EXTRA_5,
	DEATH_EXTRA_6,
	DEATH_EXTRA_7,
	DEATH_EXTRA_8,
	DEATH_KNOCKBACK,
	DEATH_SUPERNATURAL,
	DEATH_FADED,
	DEATH_SLAUGHTERED,

	DEATH_NUM_TYPES
};

// The binary's name lists (generated: WeaponNames.cpp). NULL terminated.
extern const char *const TheDamageNames[];      // RW 0xDA15A8 (28 names)
extern const char *const TheWeaponDeathNames[]; // RW 0xDA1630 (24 names)
extern const char *const TheDamageFXTypeNames[]; // RW 0xDA1510 (36 names)

// =============================================================================================================================
// The deterministic damage core (lane WEAPON-1). Every function mirrors one retail routine and its operation order; "SSE" means
// plain float32 arithmetic in the stated order (the build must not contract it: CMake -ffp-contract=off), "x87" means one rounding to
// 24 bits per operation (NumericState::pc24*). Anything that needs a live object goes through a host interface.
// =============================================================================================================================
class ArmorTemplate;
class DamageNugget;
class WeaponTemplate;

// RW DamageInfo (0x7C bytes, ctor RW 0x66365E): the input half at D+4 (ctor RW 0x66341C), the output half at D+0x6C.
struct DamageInfoInput
{
	unsigned m_sourceID = 0;                  // D+0x08
	std::uint32_t m_sourcePlayerMask = 0;     // D+0x0C
	int m_damageType = DAMAGE_UNDEFINED;      // D+0x10
	int m_damageFXOverride = 29;              // D+0x14 (the nugget's DamageFXType; 29 = UNDEFINED)
	int m_damageSubType = 0;                  // D+0x18
	int m_deathType = DEATH_NORMAL;           // D+0x1C
	float m_amount = 0.0f;                    // D+0x20
	bool m_kill = false;                      // D+0x24
	bool m_shouldPlayUnderAttackEva = true;   // D+0x25
	float m_delay = 0.0f;                     // D+0x28 (frames)
	int m_fxTrigger = 0;                      // D+0x2C
	// lane COMBAT-4: the shockwave half (ctor RW 0x66341C: the vector, amount, radius, taper and center 0, ZMult and ClearMult 1.0, ClearFlingHeight 100.0 RW 0xBD88D8),
	// written by MetaImpactNugget (RW 0x910380) and RamPower's crush hit (RW 0x8BFF08), read by Object's shockwave handler RW 0x6968BC
	unsigned m_shockWaveSourceID = 0;         // D+0x30 (the nugget's source object id: the cyclone's centre)
	Coord3D m_shockWaveVector{};              // D+0x34
	float m_shockWaveAmount = 0.0f;           // D+0x40
	float m_shockWaveRadius = 0.0f;           // D+0x44
	float m_shockWaveTaperOff = 0.0f;         // D+0x48
	float m_shockWaveZMult = 1.0f;            // D+0x4C
	bool m_shockWaveClearRadius = false;      // D+0x50
	float m_shockWaveClearMult = 1.0f;        // D+0x54
	float m_shockWaveClearFlingHeight = 100.0f; // D+0x58
	Coord3D m_shockWaveClearCenter{};         // D+0x5C (the source's position when ClearRadius)
	float m_cyclonicFactor = 0.0f;            // D+0x68
};

struct DamageInfoOutput
{
	float m_actualDamageDealt = 0.0f;   // D+0x70: the amount applied, NOT clipped to the remaining health
	float m_actualDamageClipped = 0.0f; // D+0x74: previous health - new health (negative for healing)
	bool m_noEffect = false;            // D+0x78
};

struct DamageInfo
{
	DamageInfoInput m_input;
	DamageInfoOutput m_output;
};

// RW AttributeModifier types (list RW 0xD8AF48) the damage core reads.
enum
{
	ATTRIBUTE_MODIFIER_ARMOR = 1,
	ATTRIBUTE_MODIFIER_DAMAGE_ADD = 2,
	ATTRIBUTE_MODIFIER_DAMAGE_MULT = 3,
	ATTRIBUTE_MODIFIER_SPELL_DAMAGE = 11,
	ATTRIBUTE_MODIFIER_RATE_OF_FIRE = 21,
	ATTRIBUTE_MODIFIER_INVULNERABLE = 27
};

// ---- AdjustDamage (RW 0x5D893C, thiscall ret 0xC) ---------------------------------------------------------------------------
class AdjustDamageHost
{
public:
	virtual ~AdjustDamageHost() {}
	// RW 0x68FB63: the victim is flanked by the attacker (asked only when the armour exists, flanking is on and the source is known)
	virtual bool victimFlankedByAttacker() = 0;
	// AttributeModifier INVULNERABLE whose damage type name list admits TheDamageNames[type]
	virtual bool invulnerableTo(int damageType) = 0;
	// the sum of the active ARMOR modifiers for that damage type name (RW 0x68C818 -> 0x804F39: 0 + v1 + v2 ... in list order)
	virtual float armorModifierSum(int damageType) = 0;
	// GlobalData AttributeModifierArmorMaxBonus (RW +0xAE8; retail 75%)
	virtual float armorMaxBonus() = 0;
};

// `armor`: the template of the body's cached armor NAME (nullptr when the name is unknown: coefficient 1.0, no flank penalty).
// `setArmor`: the template of the victim's CURRENT ArmorSet entry (nullptr: scalar 1.0). `noFlank`: the estimate path (RW 0x8C1C51).
// The result has NO clamp at 0 and no rounding; HEALING returns the amount unchanged.
float AdjustDamage(const ArmorTemplate *armor, const ArmorTemplate *setArmor, const DamageInfoInput &in, bool noFlank, AdjustDamageHost &host);

// ---- DamageNugget::fillDamageInfo (RW 0x90E28C) ---------------------------------------------------------------------------------
class NuggetDamageHost
{
public:
	virtual ~NuggetDamageHost() {}
	virtual bool hasVictim() = 0;
	// RW 0x66137C: squared distance in x,y between the victim and `center`
	virtual float victimDistanceSqr2D(const Coord3D &center) = 0;
	virtual Coord3D victimPosition() = 0;
	virtual Coord3D sourcePosition() = 0;
	virtual bool sourceHasContain() = 0;
	virtual int sourcePassengerCount() = 0;
	// `filter.allows(victim, source player)` (RW 0x7640C1); an empty/NONE filter allows nothing
	virtual bool filterAllowsVictim(const ObjectFilter &filter) = 0;
	// getAdditive(source, type, &out, name = null): found?
	virtual bool sourceAdditive(int modifierType, float &out) = 0;
	// getMultiplicative(source, type, &out, name = null, innate): found? (`out` is initialised to 1.0 by the caller)
	virtual bool sourceMultiplicative(int modifierType, bool innate, float &out) = 0;
	virtual bool victimFlankedBySource() = 0;
	virtual bool sourceFlankedByVictim() = 0;
	virtual bool weaponSourceIsVictim() = 0;
	virtual std::uint32_t sourcePlayerMask() = 0;
	virtual unsigned weaponSourceID() = 0;
};

// Returns false when the hit is dropped (FlankedScalar * amount < 1.0). `center` is the radius-damage centre or nullptr for a direct hit.
bool FillDamageInfo(const DamageNugget &nugget, const WeaponTemplate &weapon, NuggetDamageHost &host, bool noFlank, const Coord3D *center, DamageInfo &out);

// ---- the health arithmetic of ActiveBody::attemptDamage (RW 0x8C3FA3) and internalChangeHealth (RW 0x8C31A5) ----------------------
struct BodyHealth
{
	float health = 0.0f;
	float maxHealth = 0.0f;
	float previousHealth = 0.0f; ///< RW +0x0C
	bool healHistoryCleared = false; ///< the new health was capped at the maximum (RW zeroes the 4-float heal history)
};

// RW 0x8C31A5: h = delta + health; h > max -> max; 0 > h -> 0; previous = old health.
void InternalChangeHealth(BodyHealth &body, float delta);
// RW 0x8C4AB4 (ImmortalBody): delta = max(delta, 1.0f - health), then the base rule
void ImmortalInternalChangeHealth(BodyHealth &body, float delta);

// Steps 7, 9, 11, 13 and 15 of ActiveBody::attemptDamage on an already armour-adjusted amount: the body scalar (not for UNRESISTABLE),
// the `amount > 0 || kill` gate, kill = current health, the health change and the output fields. Returns false when the gate skips the hit.
// `fireCapped`: the burning-death cap of step 10 (amount = min(amount, health - 1.0f)) applies.
bool ActiveBodyApplyDamage(BodyHealth &body, DamageInfo &info, float adjustedAmount, float bodyDamageScalar, bool fireCapped);
// RW 0x8C49C7 (HighlanderBody): the raw input amount is clamped to health - 1.0f for every type but UNRESISTABLE, before armour
float HighlanderClampAmount(float inputAmount, float health, int damageType);

// RW 0x697EB6 (Object::updatePendingDamage): one logic frame for one pending hit: delay = delay - 1.0f; due when 0 > delay.
bool PendingDamageDue(float &delay);
// RW 0x698E7D: a hit on an object that is currently updating its AI is delayed by one frame (delay = 1.0f)
inline float PendingDamageDelayFor(float delay, bool objectUpdatingAI)
{
	return objectUpdatingAI ? 1.0f : delay;
}
