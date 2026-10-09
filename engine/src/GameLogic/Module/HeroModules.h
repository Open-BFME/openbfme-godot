// OpenBFME. GPL-3.0.
//
// Hero module classes (lane HERO-1): RespawnUpdate (the death and revival of a hero) and BuildableHeroListUpgrade (the ring heroes join the recruit list).
// Neither class exists in Zero Hour; BFME1 has no RespawnUpdate body that matches RotWK's. The BFME2 decompile (Open-BFME-2 Object/Update/RespawnUpdate*.cpp)
// gives the parse rules and the object layout (DONOR, it agrees with the target); the behaviour is read from the RotWK binary.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RespawnUpdateModuleData (table RW 0xC6CE80): DeathAnim (+0x0C, model condition flags RW 0x4B8C21), DeathFX (+0xF0, FXList RW 0x73A302),
//     DeathAnimationTime (+0xFC, parseDurationUnsignedInt RW 0x73A429), InitialSpawnAnim (+0xA4), InitialSpawnFX (+0xF8), InitialSpawnAnimationTime (+0x104),
//     RespawnAnim (+0x58), RespawnFX (+0xF4), RespawnAnimationTime (+0x100), AutoRespawnAtObjectFilter (+0x08, ObjectFilter RW 0x76392F), RespawnRules
//     (+0x10C, RW 0x8B3D65) and RespawnEntry (+0x10C, RW 0x8B3F72) into a map level -> {cost +0x14, time ms +0x18, health +0x1C, autoSpawn +0x20},
//     ButtonImage (+0x118, string RW 0x42EE5E), RespawnAsTemplate (+0x11C, string).
//     RespawnRules: no level-1 rule may exist yet ("Duplicate RespawnRules entry."); then exactly `AutoSpawn:<bool> Cost:<uint> Time:<int> Health:<percent>`
//     in that order, each key matched case-insensitively first (wrong key: "expecting '...' entry") and then case-sensitively ("is case sensitive"); the rule
//     is level 1. RespawnEntry: needs the level-1 rule ("You cannot parse a 'RespawnEntry' before 'RespawnRules'..."), then `Level:<uint>` (same two
//     checks), a level that already has a rule is an error, the rule starts as a copy of level 1's, then any of AutoSpawn (case-insensitive dispatch) /
//     Cost / Time / Health (case-sensitive dispatch, so a wrong-case key other than AutoSpawn is skipped), each at most once; unknown keys are skipped.
//     The messages are retail's, quirks included (Time's duplicate message names 'Cost' with two %d; Health's duplicate repeats Cost's).
//   * the module (constructor RW 0x8B3244, update interface vtable RW 0xC6C354 at +0x10): +0x20 the rule's health fraction (-1.0), +0x24 the button image
//     (UI), +0x28 the RespawnAsTemplate cache, +0x2C the state (0 alive, 1 refunded, 2 permanently dead, 3 auto respawning, 4 playing the (re)spawn
//     animation), +0x30 the object it respawns at, +0x34 / +0x38 / +0x3C (-1, -1 then the rule's time and cost), +0x40 not AutoSpawn, +0x41 the next
//     spawn is the initial one. It sleeps forever from construction.
//   * the rule of a level (RW 0x603AF6 lookups in RW 0x8B3637 / 0x8B36B2 / 0x8B3744 / 0x8B38A2): the tracker's rank (+0x24), else level 1; cost of
//     neither 1000, time of neither (int)(0.005f * 30000.0f); time = rule ms / 1000 (int division);
//   * RespawnBody (RW 0x8C553F) on a lethal hit that is not permanent: a TEMPORARILY_DEFECTED object is restored (RW 0x69ABA7, not ported: S-853),
//     an INHERITED_FROM_ALLY_TEAM object (status 80) is permanently killed (RW 0x8B3349, state 0), else onDeath (RW 0x8B3744); a permanent hit: RW 0x8B3349;
//   * onDeath (RW 0x8B3744), in state 0 or 4: no rule -> sleep forever; else DeathAnim set, DeathFX on the object, DISABLED_PARALYZED (RW 0x692432(4), no
//     end), effectively dead (RW 0x68D950), UNSELECTABLE (RW 0x62684D(3)); +0x40 = !AutoSpawn, +0x38 = time, +0x3C = cost, +0x20 = health; with a
//     controlling player a revive record joins its hero list (RW 0x781792, AutoSpawn: cost 0) and gets the tracker's level cap (+0xD8);
//   * permanently killed (RW 0x8B3349), in a state other than 0 (so never for the alive state RespawnBody calls it in): DeathAnim and RespawnAnim cleared,
//     DISABLED_PARALYZED cleared, sleep forever, +0x30 = 0, state 2;
//   * revived (RW 0x8B38A2, called by ProductionUpdate on the new object of a revive entry): the initial spawn (+0x41) sets InitialSpawnAnim and plays
//     InitialSpawnFX for InitialSpawnAnimationTime, else RespawnAnim / RespawnFX / RespawnAnimationTime; state 4; with a rule: +0x20 = health, the body's
//     health set to that share (body vslot 0x58 RW 0x8C47DE(health * 100, false): internalChangeHealth(fraction * body + 0x2C - health)), +0x34 = +0x38 = -1,
//     wake after the animation time; without a rule sleep forever;
//   * update (RW 0x8B3A35): state 3 (auto respawn, not reachable from retail data: no AutoSpawn:Yes rule exists; its writer is not located, S-851) moves the
//     object to its respawn point, RespawnAnim / RespawnFX, DISABLED_PARALYZED until the animation ends, state 4, health; state 4: RespawnAnim and
//     InitialSpawnAnim cleared, UNSELECTABLE and DISABLED_PARALYZED cleared, state 0, and when the object's producer exists, the object's AI is idle
//     (AI vslot 0x1B8 or RW 0x660AC1 == 16, S-854) and the producer has an exit interface, the producer's exit sends it out (slot 8, exitObjectViaDoor(obj, 0));
//     sleep forever.
//   * UnpauseSpecialPowerUpgrade (create RW 0x64FF28 / 0x64FEF0, data 0x140 bytes: the upgrade base then table RW 0xC6EB5C: SpecialPowerTemplate (+0x138,
//     RW 0x73B22F: the store's template by name, null when unknown) and ObeyRechageOnTrigger (sic, +0x13C, bool); vtables RW 0xC6EA98 / mux RW 0xC6EA50):
//     upgradeImplementation (RW 0x8B9676): every module of the object whose special power interface (module iface vslot 0x20) has that template (slot 0x18)
//     is unpaused (slot 0x24 with false) and, without ObeyRechageOnTrigger, made ready now (slot 0x20, setReadyFrame(frame)); the removal (RW 0x8B9701):
//     when the mux is executed (slot 0), every such module is paused again (slot 0x24 with true). This is how a hero's level (ExperienceLevel Upgrades) or a
//     purchase unlocks an ability that StartsPaused.
//   * AttributeModifierAuraUpdate (constructor RW 0x89ED5C, vtables RW 0xC675A4 / update RW 0xC67598 / mux RW 0xC67550 at +0x20; data: the update part with
//     table RW 0xC67640 and the upgrade base at data + 0x28): BonusName (+0x08, a ModifierList name), RefreshDelay (+0x18, duration), Range (+0x1C),
//     AllowPowerWhenAttacking (+0x20), TargetEnemy (+0x21), ObjectFilter (+0x24), AffectsKindOf (RW 0x89ECD7, a KindOf list written over the object filter's
//     include mask, RW 0x89EEDA: '+' / '-' / plain names, mixing is INIException 2), StartsActive (+0x158), RequiredConditions (+0x15C, names MOUNTED TAINT
//     ELVEN_WOOD), AntiCategory (+0x160), AntiFX (+0x164), AffectGood (+0x168), AffectEvil (+0x169), RunWhileDead (+0x16A), AllowSelf (+0x16B),
//     AffectContainedOnly (+0x16C), MaxActiveRank (+0x170). The constructor wakes it next frame and, with StartsActive, executes the mux at once
//     (RW 0x855388); the mux's implementation (RW 0x8554D6) wakes it next frame.
//     update (RW 0x89F42D): a dead object without RunWhileDead, or a mux not executed, sleeps forever; Object + 0x11C bit 1 (S-856), RequiredConditions MOUNTED
//     without model condition 214 (MOUNTED), or (without AllowPowerWhenAttacking) model condition 37 (ATTACKING) skip the pulse; else the pulse: with an
//     AntiCategory, its expiry is now + the BonusName list's Duration (999999 without one); without AffectContainedOnly every object the partition finds
//     within Range of the object (FROM_CENTER_2D) that is alive, passes the relationship filter (TargetEnemy: enemies; otherwise, unless AffectGood /
//     AffectEvil decide, allies; a WALK_ON_TOP_OF_WALL source uses its geometry radius and no relationship filter: S-856) and the ObjectFilter (for the
//     owner), plus the object itself with AllowSelf, gets the per-object step; with AffectContainedOnly its contain's objects do; the next pulse is after
//     (object id % 5) + RefreshDelay frames.
//     per object (RW 0x89F114): KindOf IGNORED_IN_GUI or ARMY_OF_DEAD, the source itself without AllowSelf, AffectGood with an Evil owner template, AffectEvil
//     with a non-Evil one, a rank above MaxActiveRank (when not 0), Object + 0x11C bit 1, or a TAINT / ELVEN_WOOD condition not met (the terrain areas of
//     RW 0xAD49B0 are not ported: none is met, S-856) skip it; in the contained mode a HORDE recurses into its contain; the AntiCategory goes to its pool
//     (RW 0x804FCC: XP-1's S-633, counted) with AntiFX; then Object::addAttributeModifier(BonusName, -1) (RW 0x68F1A8: the list's own Duration).
//   * AutoHealBehavior (constructor RW 0x85562D, vtables RW 0xC56504 / update RW 0xC564F8 / mux RW 0xC564B0 at +0x20 / damage RW 0xC564A4 at +0x28; data: the
//     upgrade base then table RW 0xC09F70): StartsActive (+0x138), ButtonTriggered (+0x139), SingleBurst (+0x13A), HealingAmount (+0x13C, parseInt), HealingDelay
//     (+0x140), StartHealingDelay (+0x144), Radius (+0x148, parseInt), KindOf (+0x150, a KindOf mask), UnitHealPulseFX (+0x170), AffectsWholePlayer (+0x14C),
//     AffectsContained (+0x14D), HealOnlyIfNotUnderAttack (+0x14E), HealOnlyIfNotInCombat (+0x14F), HealOnlyOthers (+0x16C), NonStackable (+0x174),
//     RespawnNearbyHordeMembers (+0x175), RespawnFXList (+0x178), RespawnMinimumDelay (+0x17C, uint). The module: +0x2C the next heal frame, +0x30 waiting for
//     its button (ButtonTriggered), +0x34 the last horde respawn frame.
//     constructor: +0x30 = ButtonTriggered; StartsActive: the mux executes and the module wakes after GameLogicRandomValue(1, HealingDelay) frames (RW 0x6D328E,
//     a logic random draw at creation), else it sleeps forever; the mux's implementation (RW 0x8554D6) wakes it next frame, its removal (RW 0x85544E) sleeps.
//     update (RW 0x8558C0): waiting for the button, a mux not executed or a dead object: sleep forever; HealOnlyIfNotInCombat and the object in combat (RW
//     0x8554E4: its AI, or its HORDE container's, has a live current victim, AI + 0x40): wake after StartHealingDelay (5 when 0); amount = (int)((float)
//     HealingAmount + the AUTO_HEAL attribute sum) below 1: wake next frame; AffectsWholePlayer (S-858); AffectsContained: the contain's objects (heal with the
//     FX unless the contain's flag); Radius: the allies within Radius (FROM_CENTER_2D) that are KindOf, below their max health and pass RW 0x855533 are healed
//     (RespawnNearbyHordeMembers: S-858); else the object itself when below its max health; then FOREVER after a SingleBurst, else HealingDelay.
//     heal one (RW 0x855761): not while waiting for the button; NonStackable needs the body's last heal frame (body vslot 0x48, S-858); amount as above; Radius 0:
//     Object::attemptHealing(amount, the object) (RW 0x690532), else attemptHealingFromSoleBenefactor(amount, the object, HealingDelay) (RW 0x690584);
//     UnitHealPulseFX on the target; the next heal frame = now + HealingDelay.
//     eligible (RW 0x855533): HealOnlyOthers and the healer itself: no; HealOnlyIfNotInCombat and the target in combat: no; HealOnlyIfNotUnderAttack and a
//     damage within LOGICFRAMES_PER_SECOND frames (body vslot 0x44, the last damage frame): no; a dead (+0x458 bit 0) or bit-3 object: no; else KindOf and
//     below the max health.
//     onDamage (RW 0x855706): not waiting for the button, the mux executed and Radius 0: a hit whose DamageInfo + 0x18 (m_damageSubType) is not 2 with a
//     StartHealingDelay wakes it after StartHealingDelay; otherwise, past the next heal frame, next frame.
//   * BuildableHeroListUpgrade (RW 0x8BC4C6, upgradeImplementation): every BuildableRingHeroesMP name of the controlling player's template joins the hero list
//     as a purchase record (RW 0x781801 with the template found by name); the control bar refresh (TheControlBar + 0x28) and RW 0x8D28F1 (the custom anim)
//     follow.

#pragma once

#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

class ModuleFactory;
struct ObjectFilter;

struct RespawnRule
{
	unsigned level = 1;     ///< node + 0x10
	unsigned cost = 0;      ///< + 0x14
	int timeMs = 0;         ///< + 0x18
	float health = 1.0f;    ///< + 0x1C
	bool autoSpawn = false; ///< + 0x20
};

class RespawnUpdateModuleData : public ModuleData
{
public:
	typedef std::array<std::uint32_t, 19> Flags;
	std::shared_ptr<const ObjectFilter> m_autoRespawnAtObjectFilter; ///< + 0x08 (null: not given)
	Flags m_deathAnim{};          ///< + 0x0C
	Flags m_respawnAnim{};        ///< + 0x58
	Flags m_initialSpawnAnim{};   ///< + 0xA4
	std::string m_deathFX;        ///< + 0xF0
	std::string m_respawnFX;      ///< + 0xF4
	std::string m_initialSpawnFX; ///< + 0xF8
	unsigned m_deathAnimationTime = 0;        ///< + 0xFC (frames)
	unsigned m_respawnAnimationTime = 0;      ///< + 0x100
	unsigned m_initialSpawnAnimationTime = 0; ///< + 0x104
	std::map<unsigned, RespawnRule> m_rules;  ///< + 0x10C
	std::string m_buttonImage;                ///< + 0x118
	std::string m_respawnAsTemplate;          ///< + 0x11C

	const RespawnRule *ruleFor(unsigned level) const; ///< the level's rule, else level 1's, else null (RW 0x603AF6 twice)
	static void buildFieldParse(MultiIniFieldParse &p);
	static void parseDefaultRule(INI *ini, void *instance, void *store, const void *userData);  ///< RW 0x8B3D65
	static void parseRuleForLevel(INI *ini, void *instance, void *store, const void *userData); ///< RW 0x8B3F72
};

class RespawnUpdate : public UpdateModule
{
public:
	enum State
	{
		STATE_ALIVE = 0,
		STATE_REFUNDED = 1,
		STATE_PERMANENTLY_DEAD = 2,
		STATE_AUTO_RESPAWN = 3,
		STATE_SPAWN_ANIMATION = 4
	};

	RespawnUpdate(Thing *thing, const RespawnUpdateModuleData *data); ///< RW 0x8B3244
	const RespawnUpdateModuleData *data() const { return m_data; }
	UpdateSleepTime update() override; ///< RW 0x8B3A35

	void onDeath();             ///< RW 0x8B3744
	void onPermanentDeath();    ///< RW 0x8B3349
	void onRevived(const Object *producer); ///< RW 0x8B38A2
	void setState(int s) { m_state = s; }  ///< RespawnBody's `state = 0` after RW 0x8B3349 (RW 0x8C5657)
	void setInitialSpawn(bool initial) { m_initialSpawn = initial; } ///< RW 0x78142F: + 0x41 = the record is not a revive

	int state() const { return m_state; }
	bool isInitialSpawn() const { return m_initialSpawn; } ///< RW 0x8B330F
	float healthFraction() const { return m_health; }
	unsigned ruleCost() const;                 ///< RW 0x8B3C07 -> 0x8B3637 (the tracker's rank)
	int ruleSeconds() const;                   ///< RW 0x8B3C1C -> 0x8B36B2
	const ThingTemplate *respawnTemplate() const; ///< RW 0x8B316B: RespawnAsTemplate, else the object's template
	void crc(StateHasher &h) const override;

private:
	unsigned rankOfObject() const;

	const RespawnUpdateModuleData *m_data;
	float m_health = -1.0f;              ///< + 0x20
	int m_state = STATE_ALIVE;           ///< + 0x2C
	ObjectID m_respawnAt = INVALID_ID;   ///< + 0x30
	std::int32_t m_34 = -1;              ///< + 0x34
	std::int32_t m_time = -1;            ///< + 0x38
	std::int32_t m_cost = -1;            ///< + 0x3C
	bool m_notAutoSpawn = false;         ///< + 0x40
	bool m_initialSpawn = false;         ///< + 0x41
};

class SpecialPowerTemplate;

class UnpauseSpecialPowerUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_specialPowerTemplateName;                    ///< +0x138 as written
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr; ///< +0x138 (RW 0x73B22F: null for an unknown name)
	bool m_obeyRechargeOnTrigger = false;                      ///< +0x13C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class UnpauseSpecialPowerUpgrade : public UpgradeModule
{
public:
	UnpauseSpecialPowerUpgrade(Thing *thing, const UnpauseSpecialPowerUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override; ///< RW 0x8B9676
	void processUpgradeRemoval() override; ///< RW 0x8B9701

private:
	const UnpauseSpecialPowerUpgradeModuleData *m_data;
};

class AttributeModifierAuraUpdateModuleData : public UpgradeModuleData
{
public:
	std::string m_bonusName;                          ///< +0x08
	unsigned m_refreshDelay = 0;                      ///< +0x18 (frames)
	float m_range = 0.0f;                             ///< +0x1C
	bool m_allowPowerWhenAttacking = false;           ///< +0x20
	bool m_targetEnemy = false;                       ///< +0x21
	std::shared_ptr<const ObjectFilter> m_objectFilter; ///< +0x24 (null: not given)
	bool m_startsActive = false;                      ///< +0x158
	std::uint32_t m_requiredConditions = 0;           ///< +0x15C (bit 0 MOUNTED, 1 TAINT, 2 ELVEN_WOOD)
	std::uint32_t m_antiCategory = 0;                 ///< +0x160 (TheAntiCategoryNames bits)
	std::string m_antiFX;                             ///< +0x164
	bool m_affectGood = false;                        ///< +0x168
	bool m_affectEvil = false;                        ///< +0x169
	bool m_runWhileDead = false;                      ///< +0x16A
	bool m_allowSelf = false;                         ///< +0x16B
	bool m_affectContainedOnly = false;               ///< +0x16C
	int m_maxActiveRank = 0;                          ///< +0x170
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AttributeModifierAuraUpdate : public UpdateModule, public UpgradeMux
{
public:
	AttributeModifierAuraUpdate(Thing *thing, const AttributeModifierAuraUpdateModuleData *data); ///< RW 0x89ED5C
	UpgradeMux *getUpgrade() override { return this; }
	UpdateSleepTime update() override; ///< RW 0x89F42D
	void crc(StateHasher &h) const override;
	unsigned pulses() const { return m_pulses; }
	unsigned long long antiCategoryRequests() const { return m_anti; }

protected:
	void upgradeImplementation() override; ///< RW 0x8554D6: wake next frame

private:
	struct Pulse
	{
		UnsignedInt antiExpire = 0;
		Object *source = nullptr;
		bool containedMode = false;
	};
	void affect(Object &o, const Pulse &pulse); ///< RW 0x89F114
	const AttributeModifierAuraUpdateModuleData *m_data;
	unsigned m_pulses = 0;           ///< pulses run (a test counter, hashed)
	unsigned long long m_anti = 0;   ///< anti-category requests not applied (S-633), not hashed
};

class AutoHealBehaviorModuleData : public UpgradeModuleData
{
public:
	bool m_startsActive = false;            ///< +0x138
	bool m_buttonTriggered = false;         ///< +0x139
	bool m_singleBurst = false;             ///< +0x13A
	int m_healingAmount = 0;                ///< +0x13C
	unsigned m_healingDelay = 0;            ///< +0x140 (frames)
	unsigned m_startHealingDelay = 0;       ///< +0x144
	int m_radius = 0;                       ///< +0x148
	bool m_affectsWholePlayer = false;      ///< +0x14C
	bool m_affectsContained = false;        ///< +0x14D
	bool m_healOnlyIfNotUnderAttack = false;///< +0x14E
	bool m_healOnlyIfNotInCombat = false;   ///< +0x14F
	std::array<std::uint32_t, 7> m_kindOf{}; ///< +0x150
	bool m_healOnlyOthers = false;          ///< +0x16C
	std::string m_unitHealPulseFX;          ///< +0x170
	bool m_nonStackable = false;            ///< +0x174
	bool m_respawnNearbyHordeMembers = false; ///< +0x175
	std::string m_respawnFXList;            ///< +0x178
	unsigned m_respawnMinimumDelay = 0;     ///< +0x17C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AutoHealBehavior : public UpdateModule, public UpgradeMux, public DamageModuleInterface
{
public:
	AutoHealBehavior(Thing *thing, const AutoHealBehaviorModuleData *data); ///< RW 0x85562D
	UpgradeMux *getUpgrade() override { return this; }
	DamageModuleInterface *getDamage() override { return this; }
	UpdateSleepTime update() override;                                    ///< RW 0x8558C0
	void onDamage(const DamageInfo &info) override;                       ///< RW 0x855706
	void onBodyDamageStateChange(BodyDamageType, BodyDamageType) override {}
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)(1u << 3); } ///< RW 0x855830: DISABLED_HELD
	void triggerByButton();                                               ///< RW 0x855431 (SpecialAbilityUpdate's AutoHeal ability)
	void waitForButton();                                                 ///< RW 0x85541B (SpecialAbilityUpdate's finish of the AutoHeal ability)
	void crc(StateHasher &h) const override;
	unsigned heals() const { return m_heals; }

protected:
	void upgradeImplementation() override;  ///< RW 0x8554D6
	void processUpgradeRemoval() override;  ///< RW 0x85544E

private:
	void healOne(Object &target, bool fx);  ///< RW 0x855761
	bool eligible(Object &target) const;    ///< RW 0x855533
	int amount() const;
	const AutoHealBehaviorModuleData *m_data;
	UnsignedInt m_nextHealFrame = 0;        ///< +0x2C
	bool m_waitingForButton = false;        ///< +0x30
	UnsignedInt m_lastRespawnFrame = 0;     ///< +0x34
	unsigned m_heals = 0;                   ///< heals applied (a test counter, hashed)
};

class BuildableHeroListUpgradeModuleData : public UpgradeModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p) { buildBaseFieldParse(p); }
};

class BuildableHeroListUpgrade : public UpgradeModule
{
public:
	BuildableHeroListUpgrade(Thing *thing, const BuildableHeroListUpgradeModuleData *data) : UpgradeModule(thing, data) {}

protected:
	void upgradeImplementation() override; ///< RW 0x8BC4C6
	void processUpgradeRemoval() override {}
};

namespace HeroModules
{
void registerAll(ModuleFactory &modules);
}
