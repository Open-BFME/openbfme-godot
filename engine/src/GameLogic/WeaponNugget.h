// OpenBFME. GPL-3.0.
//
// Weapon nuggets: the sub-blocks of a Weapon block (DamageNugget, ProjectileNugget, MetaImpactNugget, ...), their data, field
// tables and the registry of every nugget the binary knows. Lane WEAPON-1. Source of every fact below: workspace research
// notes weapons-and-damage.md (spec) and the RotWK game.dat disassembly (S-001 caveat applies to every address).
//
// TARGET FACTS
//   * The Weapon table (RW 0xC16DD8) has 19 nugget rows (rows 104, 106..123) plus ClearNuggets (105). A row's parse function
//     `(INI*, WeaponTemplate*)` (e.g. RW 0x6CD301) does exactly: operator new(size) (not cleared), the nugget constructor, owner
//     = the weapon (+0x124), the static field parse (initFromINIMulti over the class's table chain), appendNugget (RW 0x6CC779:
//     nugget +0x144 = weapon +0x160, push_back on the weapon's nugget list), then an optional flag byte on the weapon
//     (+0x114 = 1 for DamageNugget, DamageFieldNugget, DOTNugget; +0x157 = 1 for GrabNugget). Evaluation order is parse order;
//     there is no de-duplication.
//   * Base class (RW vtable 0xC7AE40, ctor 0x90D901, 0x148 bytes) and base table RW 0xC7AE00: RequiredUpgradeNames,
//     ForbiddenUpgradeNames (string vectors) and SpecialObjectFilter (ObjectFilter, default NONE via RW 0x763D11). Every nugget
//     chains the base table EXCEPT LuaEventNugget (only its own table). DamageNugget's table is RW 0xC7AFB0; FireLogicNugget and
//     DOTNugget derive from DamageNugget and chain base + damage + own (RW 0xC7BF18 / 0xC7BAB8).
//   * The virtual interface the weapon drives (slots, RW): 1 isApplicable(source, victim), 2 isApplicableAt(source, position),
//     3/4 preFire at object / position, 5 apply to victim, 6 apply at position, 7 direct-victim flag, 8 postProcessLoad
//     (name resolution), 9 preload, 10 asDamageNugget, 11 warhead template, 12 forces position delivery, 13 isSlaveAttack.
//     Their effects need live objects, the partition manager, FX, OCL and script systems: they are NOT in this lane (the data
//     side is complete: constructor defaults, field tables, name lists). nuggetHostNeeds() documents what each needs.
//   * Constructor defaults are listed per nugget in the structs below (RW constructor addresses in each header comment).
//     GrabNugget::RemoveTargetFromOtherContain (+0x14A) is never written by the retail constructor (operator new does not zero it):
//     the port reports it (S-18x) and defaults to false.

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectFilter.h"

#include <memory>
#include <string>
#include <vector>

class WeaponTemplate;

// RW 0xD9F5E4
extern const char *const TheVeterancyNames[];
// RW 0xDB5D9C (DamageNugget DamageSubType)
extern const char *const TheDamageSubTypeNames[];
// RW 0xD9FA08 (EmotionWeaponNugget EmotionType)
extern const char *const TheEmotionTypeNames[];
// RW 0xD9FA40 (AttributeModifierNugget AntiCategories)
extern const char *const TheAntiCategoryNames[];
// RW 0xDB6694 (FireLogicNugget LogicType)
extern const char *const TheFireLogicTypeNames[];
// RW 0xC16928 (HordeAttackNugget LockWeaponSlot)
extern const LookupListRec TheWeaponSlotLookup[];

enum WeaponNuggetKind
{
	NUGGET_DAMAGE,
	NUGGET_DAMAGE_FIELD,
	NUGGET_WEAPON_OCL,
	NUGGET_PROJECTILE,
	NUGGET_META_IMPACT,
	NUGGET_HORDE_ATTACK,
	NUGGET_SPAWN_AND_FADE,
	NUGGET_GRAB,
	NUGGET_ATTRIBUTE_MODIFIER,
	NUGGET_SPECIAL_MODEL_CONDITION,
	NUGGET_PARALYZE,
	NUGGET_LUA_EVENT,
	NUGGET_FIRE_LOGIC,
	NUGGET_SLAVE_ATTACK,
	NUGGET_DAMAGE_CONTAINED,
	NUGGET_DOT,
	NUGGET_OPEN_GATE,
	NUGGET_EMOTION_WEAPON,
	NUGGET_STEAL_MONEY,
	NUGGET_KIND_COUNT
};

// A name the nugget parsers could not check against the store that owns it (FXList, ParticleSystem, AudioEvent, ...): the store
// belongs to another lane, so the check is deferred and reported (stop S-181), never skipped silently.
struct WeaponReference
{
	std::string kind;  ///< "FXList", "AudioEvent", "ParticleSystem"
	std::string name;
};

// ---- the base nugget (RW 0x90D901) ---------------------------------------------------------------------------------------
class WeaponNugget
{
public:
	WeaponNugget();
	virtual ~WeaponNugget() {}
	virtual WeaponNuggetKind kind() const = 0;
	const char *keyword() const;

	std::vector<std::string> m_requiredUpgradeNames;  // +0x128
	std::vector<std::string> m_forbiddenUpgradeNames; // +0x134
	ObjectFilter m_specialObjectFilter;               // +0x140 (default NONE, RW 0x763D11)
	const WeaponTemplate *m_owner = nullptr;          // +0x124
	bool m_ownedByOverride = false;                   // +0x144: weapon +0x160 at append time
	// references this nugget's parse could not verify (see WeaponReference)
	std::vector<WeaponReference> m_references;

	static const FieldParse *getBaseFieldParse(); // RW 0xC7AE00
};

// An entry of a DamageScalar list (RW 0x90E9E9): `DamageScalar = <percent> <object filter>`.
struct DamageScalarEntry
{
	ObjectFilter filter;
	float scalar = 1.0f; ///< percent * 0.01f
};

// ---- DamageNugget (RW ctor 0x90DD99, size 0x1C4, table RW 0xC7AFB0) -------------------------------------------------
class DamageNugget : public WeaponNugget
{
public:
	DamageNugget();
	WeaponNuggetKind kind() const override { return NUGGET_DAMAGE; }

	float m_damage = 0.0f;                  // +0x148 Damage
	float m_damageTaperOff = -1.0f;         // +0x14C DamageTaperOff
	bool m_acceptDamageAdd = true;          // +0x168 AcceptDamageAdd
	float m_radius = 0.0f;                  // +0x150 Radius
	float m_minRadius = 0.0f;               // +0x154 MinRadius
	float m_damageArc = 3.14159274f;        // +0x158 DamageArc (angle, radians; default is the float pi)
	bool m_damageArcInverted = false;       // +0x15C DamageArcInverted
	float m_damageMaxHeight = -1.0f;        // +0x160 DamageMaxHeight
	float m_damageMaxHeightAboveTerrain = -1.0f; // +0x164 DamageMaxHeightAboveTerrain
	unsigned m_delayTime = 0;               // +0x174 DelayTime (frames)
	int m_damageType = 22;                  // +0x178 DamageType (RW 0xDB5D28; 22 = UNDEFINED)
	int m_deathType = 0;                    // +0x17C DeathType (RW 0xDB5DB0)
	int m_damageFXType = 29;                // +0x180 DamageFXType (RW 0xDB5C90; 29 = UNDEFINED)
	int m_damageSubType = 0;                // +0x184 DamageSubType (RW 0xDB5D9C)
	std::vector<DamageScalarEntry> m_damageScalar; // +0x188 DamageScalar
	float m_damageSpeed = 0.0f;             // +0x194 DamageSpeed (velocity, per frame)
	KindOfMaskType m_lostLeadershipUselessAgainst{}; // +0x198
	float m_flankingBonus = 0.0f;           // +0x16C FlankingBonus (percent as fraction)
	float m_flankedScalar = 1.0f;           // +0x170 FlankedScalar
	bool m_drainLife = false;               // +0x1B4 DrainLife
	float m_drainLifeMultiplier = 1.0f;     // +0x1B8 DrainLifeMultiplier
	bool m_cylinderAOE = false;             // +0x1C0 CylinderAOE
	ObjectFilter m_forceKillObjectFilter;   // +0x1BC ForceKillObjectFilter (default NONE)

	static const FieldParse *getFieldParse(); // RW 0xC7AFB0
};

// ---- DamageFieldNugget (RW ctor 0x90F74F, size 0x154, table RW 0xC7B378) ---------------------------------------------
class DamageFieldNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_DAMAGE_FIELD; }
	std::string m_weaponTemplateName;       // +0x150 WeaponTemplateName (resolved at postProcessLoad, RW 0x90F4DF)
	unsigned m_duration = 0;                // +0x14C Duration (frames)
	static const FieldParse *getFieldParse();
};

// ---- WeaponOCLNugget (RW ctor 0x90F8B8, size 0x150, table RW 0xC7B3F0) -----------------------------------------------
class WeaponOCLNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_WEAPON_OCL; }
	std::string m_weaponOCLName;            // +0x14C WeaponOCLName
	static const FieldParse *getFieldParse();
};

// ---- ProjectileNugget (RW ctor 0x90FEF7, size 0x174, table RW 0xC7B4C8) ----------------------------------------------
class ProjectileNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_PROJECTILE; }
	std::string m_warheadTemplateName;      // +0x150 WarheadTemplateName
	std::string m_projectileTemplateName;   // +0x154 ProjectileTemplateName
	std::string m_projectileStreamName;     // +0x158 ProjectileStreamName
	int m_weaponLaunchBoneSlotOverride = 6; // +0x15C (list RW 0xDA12E4; 6 = no override)
	Coord3D m_alwaysAttackHereOffset{};     // +0x160 AlwaysAttackHereOffset
	bool m_useAlwaysAttackOffset = false;   // +0x16C UseAlwaysAttackOffset
	static const FieldParse *getFieldParse();
};

// ---- MetaImpactNugget (RW ctor 0x9107E1, size 0x184, table RW 0xC7B6A8) ----------------------------------------------
class MetaImpactNugget : public WeaponNugget
{
public:
	MetaImpactNugget();
	WeaponNuggetKind kind() const override { return NUGGET_META_IMPACT; }
	float m_shockWaveAmount = 0.0f;         // +0x148 (velocity, per frame)
	float m_shockWaveRadius = 0.0f;         // +0x14C
	float m_shockWaveArc = 3.14159274f;     // +0x150 (angle)
	bool m_shockWaveArcInverted = false;    // +0x154
	float m_shockWaveTaperOff = 0.0f;       // +0x158
	float m_shockWaveSpeed = 0.0f;          // +0x15C (velocity)
	float m_shockWaveZMult = 1.0f;          // +0x160
	unsigned m_delayTime = 0;               // +0x168 (frames)
	bool m_invertShockWave = false;         // +0x16C
	bool m_flipDirection = false;           // +0x16D
	float m_heroResist = 0.0f;              // +0x170
	bool m_onlyWhenJustDied = false;        // +0x174
	float m_cyclonicFactor = 0.0f;          // +0x164
	bool m_shockWaveClearRadius = false;    // +0x176
	float m_shockWaveClearMult = 2.0f;      // +0x178
	float m_shockWaveClearFlingHeight = 100.0f; // +0x17C
	ObjectFilter m_killObjectFilter;        // +0x180 (default NONE)
	bool m_affectHordes = false;            // +0x175
	static const FieldParse *getFieldParse();
};

// ---- HordeAttackNugget (RW ctor 0x911B39, size 0x150, table RW 0xC7BC7C) ---------------------------------------------
class HordeAttackNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_HORDE_ATTACK; }
	bool m_closestMemberOnly = false;       // +0x148
	int m_lockWeaponSlot = 5;               // +0x14C (lookup RW 0xC16928; 5 = unlocked)
	static const FieldParse *getFieldParse();
};

// ---- SpawnAndFadeNugget (RW ctor 0x911DC5, size 0x15C, table RW 0xC7BD20) --------------------------------------------
class SpawnAndFadeNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_SPAWN_AND_FADE; }
	ObjectFilter m_objectTargetFilter;      // +0x148 (default: the parser's default filter, handle -1)
	std::string m_spawnedObjectName;        // +0x14C
	Coord3D m_spawnOffset{};                // +0x150
	static const FieldParse *getFieldParse();
};

// ---- GrabNugget (RW ctor 0x910D2F, size 0x160, table RW 0xC7B8E0) ---------------------------------------------------
class GrabNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_GRAB; }
	bool m_containTargetOnEffect = true;    // +0x148
	bool m_impactTargetOnEffect = false;    // +0x149
	bool m_removeTargetFromOtherContain = false; // +0x14A: UNINITIALISED in retail (see header)
	bool m_removeTargetFromOtherContainSet = false; // report: the INI line was present
	float m_shockWaveAmount = 0.0f;         // +0x14C
	float m_shockWaveRadius = 0.0f;         // +0x150
	float m_shockWaveTaperOff = 0.0f;       // +0x154
	float m_shockWaveSpeed = 0.0f;          // +0x158
	float m_shockWaveZMult = 1.0f;          // +0x15C
	static const FieldParse *getFieldParse();
};

// ---- AttributeModifierNugget (RW ctor 0x90EDA8, size 0x164, table RW 0xC7B148) ---------------------------------------
class AttributeModifierNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_ATTRIBUTE_MODIFIER; }
	std::string m_attributeModifier;        // +0x148
	int m_damageFXType = 29;                // +0x14C (RW 0xDB5F40)
	float m_radius = 0.0f;                  // +0x150
	float m_damageArc = 3.14159274f;        // +0x154 (angle)
	unsigned m_antiCategories = 0;          // +0x158 (bits over RW 0xD9FA40)
	std::string m_antiFX;                   // +0x15C AntiFX (FXList name, "" = none)
	bool m_affectHordeMembers = false;      // +0x160
	static const FieldParse *getFieldParse();
};

// ---- SpecialModelConditionNugget (RW ctor 0x90EFDB, size 0x158, table RW 0xC7B22C) -----------------------------------
class SpecialModelConditionNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_SPECIAL_MODEL_CONDITION; }
	std::vector<std::string> m_modelConditionNames; // +0x148
	unsigned m_modelConditionDuration = 0;  // +0x154 (frames)
	static const FieldParse *getFieldParse();
};

// ---- ParalyzeNugget (RW ctor 0x90F130, size 0x15C, table RW 0xC7B2B8) ------------------------------------------------
class ParalyzeNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_PARALYZE; }
	float m_radius = 0.0f;                  // +0x148
	unsigned m_duration = 0;                // +0x14C (frames)
	float m_damageArc = 3.14159274f;        // +0x150 (angle)
	std::string m_paralyzeFX;               // +0x154 (FXList name)
	bool m_freezeAnimation = false;         // +0x158
	bool m_affectHordeMembers = false;      // +0x159
	static const FieldParse *getFieldParse();
};

// ---- LuaEventNugget (RW ctor 0x911FFC, size 0x154, table RW 0xC7BDC8; does NOT chain the base table) ----------------
class LuaEventNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_LUA_EVENT; }
	std::string m_luaEvent;                 // +0x148
	float m_radius = 0.0f;                  // +0x14C
	bool m_sendToEnemies = false;           // +0x150
	bool m_sendToAllies = false;            // +0x151
	bool m_sendToNeutral = false;           // +0x152
	static const FieldParse *getFieldParse();
};

// ---- FireLogicNugget (RW ctor 0x912241, size 0x1D4, derives DamageNugget; tables RW 0xC7AE00 + 0xC7AFB0 + 0xC7BF18) ----
class FireLogicNugget : public DamageNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_FIRE_LOGIC; }
	int m_logicType = 0;                    // +0x1C4 (RW 0xDB6694)
	int m_minMaxBurnRate = 0;               // +0x1C8
	int m_minDecay = 0;                     // +0x1CC
	int m_maxResistance = 10000;            // +0x1D0
	static const FieldParse *getFieldParse();
};

// ---- SlaveAttackNugget (RW ctor 0x910E8F, size 0x148, own table RW 0xC84858 is empty) -----------------------------------
class SlaveAttackNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_SLAVE_ATTACK; }
};

// ---- DamageContainedNugget (RW ctor 0x911143, size 0x188, table RW 0xC7BA10) -------------------------------------------
class DamageContainedNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_DAMAGE_CONTAINED; }
	int m_killCount = 0;                    // +0x148
	KindOfMaskType m_killKindof{};          // +0x14C
	KindOfMaskType m_killKindofNot{};       // +0x168
	int m_deathType = 1;                    // +0x184 (RW 0xDB63D8; 1 = NONE)
	static const FieldParse *getFieldParse();
};

// ---- DOTNugget (RW ctor 0x911494, size 0x1CC, derives DamageNugget; tables RW 0xC7AE00 + 0xC7AFB0 + 0xC7BAB8) ----------
class DOTNugget : public DamageNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_DOT; }
	unsigned m_damageInterval = 0;          // +0x1C4 (frames)
	unsigned m_damageDuration = 0;          // +0x1C8 (frames)
	static const FieldParse *getFieldParse();
};

// ---- OpenGateNugget (RW ctor 0x911602, size 0x14C, table RW 0xC7BB24) ---------------------------------------------------
class OpenGateNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_OPEN_GATE; }
	float m_radius = 0.0f;                  // +0x148
	static const FieldParse *getFieldParse();
};

// ---- EmotionWeaponNugget (RW ctor 0x911829, size 0x154, table RW 0xC7BB80) ---------------------------------------------
class EmotionWeaponNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_EMOTION_WEAPON; }
	int m_emotionType = -1;                 // +0x148 (index in RW 0xD9FA08; unknown name -> -1)
	float m_radius = 0.0f;                  // +0x14C
	unsigned m_duration = 0;                // +0x150 (a RAW unsigned int, not converted to frames)
	static const FieldParse *getFieldParse();
};

// ---- StealMoneyNugget (RW ctor 0x91193A, size 0x14C, table RW 0xC7BC10) --------------------------------------------------
class StealMoneyNugget : public WeaponNugget
{
public:
	WeaponNuggetKind kind() const override { return NUGGET_STEAL_MONEY; }
	float m_amountStolenPerAttack = 0.0f;   // +0x148
	static const FieldParse *getFieldParse();
};

// ---- field parse procs shared by the nugget tables (declared for the golden tests) --------------------------------------------
void ParseDamageScalar(INI *ini, void *instance, void *store, const void *userData);       // RW 0x90E9E9: store = vector<DamageScalarEntry>
void ParseWeaponLaunchBoneSlot(INI *ini, void *instance, void *store, const void *userData); // RW 0x90F933: int, list RW 0xDA12E4
void ParseEmotionType(INI *ini, void *instance, void *store, const void *userData);         // RW 0x8E09CE: int, -1 for an unknown name
void ParseNuggetUnsignedInt(INI *ini, void *instance, void *store, const void *userData);   // RW 0x42ECB2 (userData 0: no cap)
void ParseNuggetFXList(INI *ini, void *instance, void *store, const void *userData);        // RW 0x73A302: store = std::string name
void ParseRemoveTargetFromOtherContain(INI *ini, void *instance, void *store, const void *userData); // RW 0x42E558 into GrabNugget +0x14A

// ---- the registry ----------------------------------------------------------------------------------------------------------
// One entry per nugget the binary registers, in Weapon-table row order (RW rows 104, 106..123).
struct WeaponNuggetInfo
{
	WeaponNuggetKind kind;
	const char *keyword;       ///< the Weapon field name, e.g. "DamageNugget"
	std::unique_ptr<WeaponNugget> (*create)();
	// the tables the static parse chains, in order (RW "adds"); NULL terminated
	const FieldParse *tables[4];
	// the flag byte the row's parse function sets on the weapon: 0 none, 0x114 or 0x157
	int weaponFlagOffset;
	unsigned rwParseFunction;  ///< the row's parse function VA (golden tests)
};
const WeaponNuggetInfo &WeaponNuggetInfoFor(WeaponNuggetKind kind);
const WeaponNuggetInfo *FindWeaponNuggetInfo(const char *keyword); ///< case sensitive; nullptr when unknown

// Parses one `<Nugget> ... End` block of `info` into a fresh nugget (RW 0x6CD301 and its siblings, minus appendNugget): constructor
// defaults, then initFromINIMulti over the class's table chain. The caller appends it to the weapon.
std::unique_ptr<WeaponNugget> ParseWeaponNugget(INI *ini, const WeaponNuggetInfo &info, const WeaponTemplate *owner);
