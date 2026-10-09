// OpenBFME. GPL-3.0.
//
// Weapon data: the Weapon INI block (WeaponTemplate), WeaponStore, WeaponBonusSet. Port of ZH GameEngine/Include/GameLogic/
// Weapon.h and Source/GameLogic/Object/Weapon.cpp (the template half) as changed by RotWK. Lane WEAPON-1. The live Weapon
// instance (timing state machine) is GameLogic/WeaponState.h; the nuggets are GameLogic/WeaponNugget.h.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address; details in the spec weapons-and-damage.md)
//   * The block parse function is RW 0x6CED65, registered under "Weapon". Steps: name = getNextToken; look it up in TheWeaponStore
//     (RW global 0xDE4A1C; findWeaponTemplate RW 0x6CBADE: a linear scan of a pointer vector comparing the name KEY, so names are
//     case sensitive); not found: newTemplate (RW 0x6CE31C: new 0x180 bytes, ctor RW 0x6CDE89, name, key, push_back, NO duplicate
//     check). Found: by load type (INI+8): 2 (CREATE_OVERRIDES) -> newOverride (RW 0x6CECC6); 5 (RELOAD) -> the old template is
//     retired (RW 0x6CE82E) and a fresh one made; ANY OTHER load type -> the function returns WITHOUT reading the block (its body
//     lines then reach the top-level dispatcher). Then initFromINI(template, table RW 0xC16DD8). No post-processing exists.
//   * newOverride: NULL (-> initFromINI throws "INI::initFromINI - Invalid parameters supplied!") when the parent is already an
//     override (+0x160 != 0); otherwise a copy of the parent (every field; the nuggets and the WeaponBonusSet are SHARED with the
//     parent, the scatter / linear target vectors are copied), +4 = parent, +0x160 = 1, and the copy REPLACES the parent in the
//     store vector. Nuggets parsed into an override are owned by it (nugget +0x144).
//   * Field table RW 0xC16DD8: 124 rows (golden tests/data/weapon1/table_weapon_template.tsv). Rows 99..105 are custom
//     (DelayBetweenShots, ClipReloadTime, ScatterTarget, LinearTarget, WeaponBonus, DamageNugget, ClearNuggets), rows 106..123 the
//     other nuggets. Constructor defaults: see WeaponTemplate::WeaponTemplate (RW 0x6CDE89).
//   * parseFXList (RW 0x73A302) THROWS when the name is neither "None" (stricmp) nor a known FXList ("iniParseFXList -- FXList %s not
//     found! ..."); parseAudioEventRTS (RW 0x73AA94) throws "Invalid Sound '%s'" for an unknown name ("NoSound" clears);
//     parseParticleSystemTemplate (RW 0x73AECB) stores NULL silently. FXList, audio and particle stores belong to other lanes: the
//     checks go through WeaponReferenceHost; without a host the names are recorded (WeaponStore::unverifiedReferences, stop S-181).
//   * MaxAttackPassengers is a parseInt (4 bytes) into the BYTE at +0x141: it overwrites +0x142, +0x143 and the low byte of
//     FiringDuration (+0x144). Reproduced (parseMaxAttackPassengers).

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Damage.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/WeaponNugget.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class WeaponStore;

// ---- name lists (generated: WeaponNames.cpp) ------------------------------------------------------------------------------
extern const char *const TheFXTriggerNames[];          // RW 0xDA14FC
extern const char *const TheWeaponReloadNames[];       // RW 0xDA16BC (AutoReloadsClip)
extern const char *const TheWeaponPrefireNames[];      // RW 0xDA16CC (PreAttackType)
extern const char *const TheWeaponAffectsMaskNames[];  // RW 0xDA16E0 (RadiusDamageAffects)
extern const char *const TheWeaponCollideMaskNames[];  // RW 0xDA170C (ProjectileCollidesWith)
extern const char *const TheWeaponBonusConditionNames[]; // RW 0xDA1740
extern const char *const TheWeaponBonusFieldNames[];   // RW 0xDA179C

enum WeaponReloadType
{
	AUTO_RELOAD = 0,
	NO_RELOAD,
	RETURN_TO_BASE_TO_RELOAD
};

enum WeaponPrefireType
{
	PREFIRE_PER_SHOT = 0,
	PREFIRE_PER_ATTACK,
	PREFIRE_PER_CLIP,
	PREFIRE_PER_POSITION
};

// RW 0xDA16E0: bit i = name i
enum WeaponAffectsMask
{
	WEAPON_AFFECTS_SELF = 1 << 0,
	WEAPON_AFFECTS_ALLIES = 1 << 1,
	WEAPON_AFFECTS_ENEMIES = 1 << 2,
	WEAPON_AFFECTS_NEUTRALS = 1 << 3,
	WEAPON_KILLS_SELF = 1 << 4,
	WEAPON_DOESNT_AFFECT_SIMILAR = 1 << 5,
	WEAPON_DOESNT_AFFECT_AIRBORNE = 1 << 6,
	WEAPON_AFFECTS_PROJECTILES = 1 << 7,
	WEAPON_AFFECTS_SAME_HEIGHT_ONLY = 1 << 8,
	WEAPON_AFFECTS_MINES = 1 << 9
};

// RW 0xDA170C: bit i = name i
enum WeaponCollideMask
{
	WEAPON_COLLIDE_ALLIES = 1 << 0,
	WEAPON_COLLIDE_ENEMIES = 1 << 1,
	WEAPON_COLLIDE_NEUTRALS = 1 << 2,
	WEAPON_COLLIDE_STRUCTURES = 1 << 3,
	WEAPON_COLLIDE_SHRUBBERY = 1 << 4,
	WEAPON_COLLIDE_PROJECTILES = 1 << 5,
	WEAPON_COLLIDE_WALLS = 1 << 6,
	WEAPON_COLLIDE_SMALL_MISSILES = 1 << 7,
	WEAPON_COLLIDE_BALLISTIC_MISSILES = 1 << 8,
	WEAPON_COLLIDE_CONTROLLED_STRUCTURES = 1 << 9,
	WEAPON_COLLIDE_MONSTERS = 1 << 10
};

// The Anti* bits of WeaponTemplate::m_antiMask (RW +0x10C; the userData of the table rows 88..97).
enum WeaponAntiMaskType
{
	WEAPON_ANTI_AIRBORNE_VEHICLE = 0x001,
	WEAPON_ANTI_GROUND = 0x002,
	WEAPON_ANTI_PROJECTILE = 0x004,
	WEAPON_ANTI_SMALL_MISSILE = 0x008,
	WEAPON_ANTI_MINE = 0x010,
	WEAPON_ANTI_AIRBORNE_INFANTRY = 0x020,
	WEAPON_ANTI_BALLISTIC_MISSILE = 0x040,
	WEAPON_ANTI_PARACHUTE = 0x080,
	WEAPON_ANTI_STRUCTURE = 0x100,
	WEAPON_ANTI_AIRBORNE_MONSTER = 0x200
};

// ---- WeaponBonus (RW 0x64209F / 0x6420B2) ------------------------------------------------------------------------------
enum WeaponBonusField
{
	WEAPONBONUS_DAMAGE = 0,
	WEAPONBONUS_RADIUS,
	WEAPONBONUS_RANGE,
	WEAPONBONUS_RATE_OF_FIRE,
	WEAPONBONUS_PRE_ATTACK,
	WEAPONBONUS_FIRING,
	WEAPONBONUS_FIELD_COUNT
};

enum { WEAPONBONUS_CONDITION_COUNT = 22 };

// The six multipliers one weapon fires with. clear() = all 1.0f.
struct WeaponBonus
{
	WeaponBonus() { clear(); }
	void clear()
	{
		for (int i = 0; i < WEAPONBONUS_FIELD_COUNT; ++i)
		{
			m_field[i] = 1.0f;
		}
	}
	float getField(WeaponBonusField f) const { return m_field[f]; }
	float m_field[WEAPONBONUS_FIELD_COUNT];
};

// RW 0x210 bytes: float v[22][6], every element 1.0f (RW 0x6420B2). The `WeaponBonus = <CONDITION> <FIELD> <percent>` lines (RW
// 0x6CA45B) set v[cond][field] = percent * 0.01f. Applying a set to a bonus: appendBonuses (see Weapon.cpp for the exact rule).
class WeaponBonusSet
{
public:
	WeaponBonusSet();
	void parseWeaponBonusSet(INI *ini); // one line: after the keyword
	// `flags` is the object's weapon bonus condition mask (bit i = condition i, i < 22).
	void appendBonuses(std::uint32_t flags, WeaponBonus &bonus) const;
	float get(int condition, int field) const { return m_bonus[condition][field]; }

private:
	float m_bonus[WEAPONBONUS_CONDITION_COUNT][WEAPONBONUS_FIELD_COUNT];
};

// RW 0x6CA7AC (Weapon::computeBonus): `objectBonusMask` is the object's weapon bonus condition mask (RW Object+0x39C), `globalSet` the
// GameData WeaponBonus set (RW GlobalData+0xAD0, may be null), `extraMask` the caller's extra conditions.
class WeaponTemplate;
void ComputeWeaponBonus(std::uint32_t objectBonusMask, std::uint32_t extraMask, const WeaponBonusSet *globalSet, const WeaponTemplate &weapon, WeaponBonus &out);

// ---- parse hosts (FXList / AudioEvent / ParticleSystem stores belong to other lanes) -----------------------------------------
class WeaponReferenceHost
{
public:
	virtual ~WeaponReferenceHost() {}
	// RW 0x5E20A2 (TheFXListStore.findFXList): false -> the parser throws like retail
	virtual bool fxListExists(const std::string &name) const = 0;
	// RW 0x73AA94 (TheAudio->getAudioEventInfo): false -> "Invalid Sound '%s'"
	virtual bool audioEventExists(const std::string &name) const = 0;
	// RW 0x5F889B: false -> the pointer stays NULL, no error (reported only)
	virtual bool particleSystemExists(const std::string &name) const = 0;
	// RW 0x5DDEA2 (TheEva): false -> "Unknown EVA event in EVA:%s"
	virtual bool evaEventExists(const std::string &name) const = 0;
};

// ---- WeaponTemplate ---------------------------------------------------------------------------------------------------------
class WeaponTemplate
{
public:
	WeaponTemplate(); // RW 0x6CDE89

	const std::string &getName() const { return m_name; }
	// ZH Overridable
	const WeaponTemplate *getNextOverride() const { return m_nextOverride.get(); }
	bool isOverride() const { return m_isOverride; }

	// the field table, RW 0xC16DD8 (124 rows, terminated by a NULL row)
	static const FieldParse *getFieldParse();

	// ---- bookkeeping ----
	std::string m_name = "NoNameWeapon";               // +8 (RW default literal 0xC18418)
	std::shared_ptr<const WeaponTemplate> m_nextOverride; // +4: the template this one overrides
	bool m_isOverride = false;                         // +0x160
	int m_retiredFlag = -1;                            // +0x174 (ctor -1; the type-5 reload path writes 1 / 0)

	// ---- fields in table order (RW offset) ----
	int m_fxTrigger = 0;                // 0x10 FXTrigger
	float m_attackRange = 0;            // 0x14 AttackRange
	float m_minimumAttackRange = 0;     // 0x18 MinimumAttackRange
	float m_rangeBonusMinHeight = 0;    // 0x1C RangeBonusMinHeight
	float m_rangeBonus = 0;             // 0x20 RangeBonus
	float m_rangeBonusPerFoot = 0;      // 0x24 RangeBonusPerFoot
	float m_requestAssistRange = 0;     // 0x28 RequestAssistRange
	float m_aimDelta = 0;               // 0x2C AcceptableAimDelta (angle, radians)
	float m_aimDirection = 0;           // 0x30 AimDirection (angle)
	float m_scatterRadius = 0;          // 0x34 ScatterRadius
	float m_scatterTargetScalar = 0;    // 0x38 ScatterTargetScalar
	bool m_scatterIndependently = false; // 0x3C
	bool m_disableScatterForTargetsOnWall = false; // 0x3D
	std::vector<Coord2D> m_scatterTargets; // 0x40 ScatterTarget (lines accumulate)
	struct LinearTarget
	{
		float x, y;
		unsigned t;
	};
	std::vector<LinearTarget> m_linearTargets; // 0x4C LinearTarget (lines accumulate)
	int m_damageType = DAMAGE_UNDEFINED;   // 0x58 DamageType (default 22)
	int m_deathType = DEATH_NORMAL;        // 0x5C DeathType
	int m_damageFXType = 29;               // 0x60 DamageFXType (default 29 = UNDEFINED)
	int m_damageSubType = 0;               // 0x64 DamageSubType
	float m_weaponSpeed = 999999.0f;       // 0x68 WeaponSpeed (velocity: per-frame)
	float m_minWeaponSpeed = 999999.0f;    // 0x6C
	float m_maxWeaponSpeed = 999999.0f;    // 0x70
	bool m_isScaleWeaponSpeed = false;     // 0x74
	bool m_canBeDodged = false;            // 0x75
	unsigned m_idleAfterFiringDelay = 0xFFFFFFFFu; // 0x78 (frames)
	unsigned m_holdAfterFiringDelay = 0;   // 0x7C (frames)
	bool m_holdDuringReload = false;       // 0x80
	bool m_canFireWhileMoving = false;     // 0x81
	bool m_canFireWhileCharging = false;   // 0x82
	bool m_canSwoop = false;               // 0x83
	float m_weaponRecoil = 0;              // 0x84 (angle)
	float m_minTargetPitch = -3.14159274f; // 0x88 (angle)
	float m_maxTargetPitch = 3.14159274f;  // 0x8C (angle)
	std::string m_preferredTargetBone;     // 0x90
	std::string m_projectileExhaust[4];    // 0x94 per veterancy level (REGULAR VETERAN ELITE HEROIC); particle system names
	std::string m_fireFXs[4];              // 0xA4 FXList names per veterancy level
	std::string m_fireFlankFX;             // 0xB4
	std::string m_preAttackFXs[4];         // 0xB8
	std::string m_fireSound;               // 0xC8 audio event name ("" = none)
	unsigned m_fireSoundLoopTime = 0;      // 0xCC (frames)
	struct VoiceOverride
	{
		std::string name;                  ///< "" = none; the event name without a prefix
		bool isEva = false;                ///< the token was `EVA:<event>` (an EVA event id in retail)
		bool plusSound = false;            ///< the name had the "+SOUND:" prefix
	};
	VoiceOverride m_overrideVoiceAttackSound;           // 0xD0
	VoiceOverride m_overrideVoiceEnterStateAttackSound; // 0xD8
	std::shared_ptr<WeaponBonusSet> m_extraBonus;       // 0xE0 (null until the first WeaponBonus line; SHARED with overrides)
	int m_clipSize = 0;                    // 0xE4
	unsigned m_clipReloadMin = 0;          // 0xE8 ClipReloadTime min (frames)
	unsigned m_clipReloadMax = 0;          // 0xEC
	unsigned m_delayBetweenShotsMin = 0;   // 0xF0 DelayBetweenShots min (frames)
	unsigned m_delayBetweenShotsMax = 0;   // 0xF4
	int m_continuousFireOneShotsNeeded = 0x7FFFFFFF; // 0xF8 ContinuousFireOne
	int m_continuousFireTwoShotsNeeded = 0x7FFFFFFF; // 0xFC ContinuousFireTwo
	unsigned m_continuousFireCoastFrames = 0; // 0x100 (frames)
	unsigned m_autoReloadWhenIdle = 0;     // 0x104 (frames)
	int m_shotsPerBarrel = 1;              // 0x108
	unsigned m_antiMask = WEAPON_ANTI_GROUND; // 0x10C
	unsigned m_affectsMask = WEAPON_AFFECTS_ALLIES | WEAPON_AFFECTS_ENEMIES | WEAPON_AFFECTS_NEUTRALS; // 0x110 RadiusDamageAffects (default 0xE)
	bool m_hasDamageNugget = false;        // 0x114 (a damage-type nugget exists; ClearNuggets clears it)
	unsigned m_collideMask = WEAPON_COLLIDE_STRUCTURES; // 0x118 ProjectileCollidesWith (default 8)
	bool m_damageDealtAtSelfPosition = false; // 0x11C
	ObjectFilter m_projectileFilterInContainer; // 0x120 (default: rule NONE, RW 0x763D11)
	bool m_projectileSelf = false;         // 0x124
	bool m_meleeWeapon = false;            // 0x125
	bool m_chaseWeapon = false;            // 0x126
	int m_autoReloadsClip = AUTO_RELOAD;   // 0x128 AutoReloadsClip
	int m_preAttackType = PREFIRE_PER_SHOT; // 0x12C PreAttackType
	bool m_leechRangeWeapon = false;       // 0x130
	bool m_hitStoredTarget = false;        // 0x131
	bool m_capableOfFollowingWaypoints = false; // 0x132
	bool m_showsAmmoPips = false;          // 0x133
	bool m_allowAttackGarrisonedBldgs = false; // 0x134
	bool m_playFXWhenStealthed = false;    // 0x135
	unsigned m_preAttackDelay = 0;         // 0x138 (frames)
	unsigned m_preAttackRandomAmount = 0;  // 0x13C (frames)
	bool m_passengerProportionalAttack = false; // 0x140
	unsigned char m_maxAttackPassengers = 0; // 0x141 (a byte; see the header: parseInt overruns it)
	unsigned m_firingDuration = 0;         // 0x144 (frames)
	float m_continueAttackRange = 0;       // 0x148
	float m_infantryInaccuracyDist = 0;    // 0x14C ScatterRadiusVsInfantry
	unsigned m_suspendFXDelay = 0;         // 0x150 (frames)
	bool m_ignoreLinearFirstTarget = false; // 0x154
	bool m_forceDisplayPercentReady = false; // 0x155
	bool m_isAimingWeapon = false;         // 0x156
	bool m_hasGrabNugget = false;          // 0x157 (a GrabNugget was parsed; ClearNuggets does NOT clear it)
	float m_hitPercentage = 1.0f;          // 0x158 (percent as fraction)
	float m_hitPassengerPercentage = 0.0f; // 0x15C
	bool m_finishAttackOnceStarted = true; // 0x161
	float m_restrictedHeightRange = 0;     // 0x164
	bool m_cannotTargetCastleVictims = false; // 0x168
	bool m_requireFollowThru = false;      // 0x169
	bool m_shareTimers = false;            // 0x16A
	bool m_noVictimNeeded = false;         // 0x16B
	bool m_rotatingTurret = false;         // 0x16C
	bool m_shouldPlayUnderAttackEvaEvent = true; // 0x16D
	bool m_instantLoadClipOnActivate = false; // 0x16E
	bool m_lockWhenUsing = false;          // 0x16F
	bool m_bombardType = false;            // 0x170
	bool m_useInnateAttributes = false;    // 0x171
	std::string m_projectileStreamName;    // 0x178
	// 0x17C: the nugget list, in parse order (evaluation order). Nuggets of a parent are shared with its overrides.
	std::vector<std::shared_ptr<WeaponNugget>> m_nuggets;

	// ---- custom parse procs ----
	static void parseDelayBetweenShots(INI *ini, void *instance, void *store, const void *userData);   // RW 0x6C9D99
	static void parseClipReloadTime(INI *ini, void *instance, void *store, const void *userData);      // RW 0x6C9E7A
	static void parseScatterTarget(INI *ini, void *instance, void *store, const void *userData);       // RW 0x6CF43F
	static void parseLinearTarget(INI *ini, void *instance, void *store, const void *userData);        // RW 0x6CF475
	static void parseWeaponBonus(INI *ini, void *instance, void *store, const void *userData);         // RW 0x6CB5AB
	static void parseClearNuggets(INI *ini, void *instance, void *store, const void *userData);        // RW 0x6CB608
	static void parseNugget(INI *ini, void *instance, void *store, const void *userData);              // RW 0x6CD301 ... (userData = WeaponNuggetInfo)
	static void parseMaxAttackPassengers(INI *ini, void *instance, void *store, const void *userData); // RW 0x42EC5E into +0x141
	static void parseFireFX(INI *ini, void *instance, void *store, const void *userData);              // RW 0x6C9CDC (userData: 0 = all levels, 1 = PreAttackFX)
	static void parseVeterancyFireFX(INI *ini, void *instance, void *store, const void *userData);    // RW 0x6C9C9A
	static void parseFireFlankFX(INI *ini, void *instance, void *store, const void *userData);         // RW 0x73A302
	static void parseProjectileExhaust(INI *ini, void *instance, void *store, const void *userData);  // RW 0x6C9D46
	static void parseVeterancyProjectileExhaust(INI *ini, void *instance, void *store, const void *userData); // RW 0x6C9D04
	static void parseFireSound(INI *ini, void *instance, void *store, const void *userData);          // RW 0x73B217
	static void parseOverrideVoice(INI *ini, void *instance, void *store, const void *userData);      // RW 0x73ACEF -> 0x73AB45
	static void parseProjectileFilter(INI *ini, void *instance, void *store, const void *userData);   // RW 0x76392F

	// append a nugget (RW 0x6CC779)
	void appendNugget(std::shared_ptr<WeaponNugget> nugget);
	// every nugget's references that could not be verified, plus the template's own (report)
	std::vector<WeaponReference> m_references; // template-level: FireFX, PreAttackFX, FireFlankFX, ProjectileExhaust, FireSound, voices
};

// ---- WeaponStore (ZH WeaponStore; global TheWeaponStore, RW 0xDE4A1C) -----------------------------------------------------------
class WeaponStore
{
public:
	// RW 0x6CBADE: nullptr when absent (case sensitive). Returns the store's CURRENT entry for the name (an override once one exists).
	const WeaponTemplate *findWeaponTemplate(const std::string &name) const;
	WeaponTemplate *findWeaponTemplate(const std::string &name);

	// RW 0x6CED65. Reads the rest of one `Weapon <name> ... End` block (header already read).
	void parseWeaponTemplateDefinition(INI *ini);
	static void parseWeaponTemplateDefinitionGlobal(INI *ini); // throws code 3 when TheWeaponStore is null

	// Drops every override (map.ini's CREATE_OVERRIDES blocks): each entry returns to its root template. ZH WeaponStore::reset does this
	// between maps; the RotWK reset path was NOT located (the store's SubsystemInterface slot 1, RW 0x63F3BF, is a bare `ret`: stop
	// S-182), so the owner of the load (RetailObjectWorld) calls this where ZH calls reset(). A parent's shared WeaponBonusSet keeps
	// whatever an override's WeaponBonus lines wrote into it (retail shares the set).
	void resetOverrides();

	// templates in store order (the retail vector order; an override takes its parent's place)
	const std::vector<std::shared_ptr<WeaponTemplate>> &templates() const { return m_templates; }
	size_t size() const { return m_templates.size(); }
	size_t retiredCount() const { return m_retired.size(); }

	// Optional resolver for FXList / AudioEvent / ParticleSystem names (nullptr: names are only recorded).
	void setReferenceHost(const WeaponReferenceHost *host) { m_host = host; }
	const WeaponReferenceHost *referenceHost() const { return m_host; }

	// The acceptance stops this store carries (PLAN "Acceptance stops"): non-empty text lines.
	std::vector<std::string> acceptanceStops() const;
	// every reference that could not be verified, across all templates (name, kind, weapon)
	struct Unverified
	{
		std::string weapon, kind, name;
	};
	std::vector<Unverified> unverifiedReferences() const;

private:
	WeaponTemplate *newTemplate(const std::string &name);
	WeaponTemplate *newOverride(WeaponTemplate *parent);
	void retire(WeaponTemplate *t);
	void reindex();

	std::vector<std::shared_ptr<WeaponTemplate>> m_templates;
	std::vector<std::shared_ptr<WeaponTemplate>> m_retired;
	std::unordered_map<std::string, size_t> m_index; ///< name -> index in m_templates (first match, like the linear scan)
	const WeaponReferenceHost *m_host = nullptr;
};

extern thread_local WeaponStore *TheWeaponStore;

// The acceptance stops of the weapons / armour / damage lane that are not tied to a store's contents (docs/STOPS.md S-180 .. S-189, S-181 is
// WeaponStore::acceptanceStops). One line per stop, "S-nnn: text". Every consumer of this lane's results inherits these.
std::vector<std::string> WeaponLaneStops();

// Reference checks shared by the template and the nugget parsers (see WeaponReferenceHost). They throw like retail when a host
// is set and the name is unknown; without a host the name is appended to `refs` (stop S-181).
void WeaponCheckFXList(std::vector<WeaponReference> &refs, const std::string &name);        // RW 0x73A302: "None" (stricmp) is fine
void WeaponCheckAudioEvent(std::vector<WeaponReference> &refs, const std::string &name);    // RW 0x73AA94: "NoSound" is fine
void WeaponCheckParticleSystem(std::vector<WeaponReference> &refs, const std::string &name); // RW 0x73AECB: never throws
