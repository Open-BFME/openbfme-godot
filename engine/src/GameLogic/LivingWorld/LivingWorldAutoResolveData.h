// OpenBFME. GPL-3.0.
//
// LivingWorldAutoResolveData: the War of the Ring autoresolve data of RotWK 2.01 (the AutoResolveArmor / Weapon / Body / CombatChain / Leadership /
// HandicapLevel / ReinforcementSchedule blocks and the LivingWorldAutoResolveResourceBonus / SciencePurchasePointBonus schedules of
// data\ini\livingworldautoresolve*.ini).  Lane WOTR-2.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the names are WorldBuilder's trace strings for the same classes, see LivingWorldAutoResolveBattle.h):
//   * the unit types are the index list RW 0xDADBE4: Soldier Archer Pikemen Cavalry Monster Hero Fortress Siege Support INVALID (10, the
//     ThingTemplate AutoResolveUnitType row RW 0x42E956 at +0x5D4, constructor default 9 = INVALID, RW 0x73FF94);
//   * block parsers (the INI block list RW 0xDAD8B4..): AutoResolveWeapon RW 0x82A2A2, AutoResolveArmor RW 0x836B35, AutoResolveBody RW 0x8293C6,
//     AutoResolveCombatChain RW 0x829CE8, AutoResolveLeadership RW 0x828C70 (a name defined twice throws code 8 "Duplicate AutoResolveX entries"),
//     AutoResolveHandicapLevel RW 0x823E54, AutoResolveReinforcementSchedule RW 0x824653 ("Attacker" -> store +0x1C, "Defender" -> +0x0C, any other
//     name throws "Unknown AutoResolveReinforcementSchedule"), LivingWorldAutoResolveResourceBonus RW 0x824B8A, ...SciencePurchasePointBonus RW 0x824448;
//   * a new Weapon / Body / CombatChain starts as a COPY of AutoResolve_DefaultWeapon / _DefaultBody / _DefaultCombatChain when that one exists
//     (RW 0x829FFB / 0x829125 / 0x829A57 then the copy constructors RW 0x829F6C / 0x829071 / 0x8299DD); without it the constructor defaults:
//     Weapon MissPercentChance 50, ReduceAttackWhenHurt Yes, every DamagePerRound 0; Body CanBeAttacked Yes, LeaveInArmySummary No; CombatChain every
//     default-target bit set (~0);
//   * a new Armor copies AutoResolve_DefaultArmor (RW 0x8369F0) or is all 1.0 (RW 0x8368EC); an Armor defined twice is parsed into a temporary that is
//     thrown away (RW 0x836B35's else branch: the first definition stays);
//   * Weapon (RW 0xC51FE8): DamagePerRound "Damage:<real> Against:<type>" (RW 0x829E20), LevelBonus "Level:<int> Bonus:<percent>" (RW 0x829EEF,
//     duplicate level throws), ReduceAttackWhenHurt bool +0x3C, MissPercentChance int +0x10.  canDamage (RW 0x829DD9, cached in +0x40): MissPercentChance
//     below 100 and some DamagePerRound > 0.  getDamageForRound (RW 0x829E64, level, hp, maxHp, type): 0 for a weapon that cannot damage or a type out
//     of range, else d = DamagePerRound[type] (SSE), with ReduceAttackWhenHurt d = (hp / maxHp) * d (SSE), then the LevelBonus of the highest level <=
//     the unit's level: (bonus + 1.0) * d (x87, PC24);
//   * Armor "Armor = <type> <percent>" (RW 0x8368A9); getDamageScalar(type) = scalar[type] (RW 0x83687D);
//   * Body (RW 0xC51DD0): HitpointsAtLevel "Hitpoints:<real> Level:<int>" (RW 0x828EBE), RegenerateAtLevel "Regenerate:<real> Level:<int>" (RW 0x828F39),
//     CanBeAttacked +0x1C, LeaveInArmySummary +0x1D; hitpoints for a level (RW 0x828E4D) = the entry of the highest level <= it, else 1.0; regenerate
//     (RW 0x828E84) the same, else 0.0;
//   * CombatChain "Target = Target:<type> Priority:<int>" (RW 0x829880): a type whose default bit is already clear throws "Cannot redefine attack
//     priority"; a non-zero priority clears the default bit and inserts {type, priority} into a set ordered by (priority, type) (RW 0x8294B7);
//     priority 0 leaves the type a default target.  getHighestPriorityTarget (RW 0x8295CB): the LAST (largest) entry, unless its priority is negative
//     and some default bit is set (RW 0x9325B4): then the first default type with priority 0; an empty set gives the first default type, none: false.
//     The next target after `cur` (RW 0x8297EE) walks the set downward and the default types upward (see AutoResolveCombatChain::nextTarget);
//   * Leadership (RW 0xC51C50): Affects (type bits) +0x04, AffectsHigherLevelFirst +0x14, BonusForLevel (RW 0x8288DA, table RW 0xC51B78: MinLevel,
//     MaximumUnitsAffected, Priority, Weapon / Experience / ArmorMultiplier percentages; defaults RW 0x828389: 0, 1000000, 10, 1.0, 1.0, 1.0; two blocks
//     with one MinLevel throw); getBonusForLevel (RW 0x8288A4) = the block of the highest MinLevel <= the level, else none;
//   * HandicapLevel (RW 0xC50EB0): GUIDisplayedLevel, Weapon / Armor / ExperienceMultiplier reals (defaults 0, 1.0, 1.0, 1.0); the lookup (RW 0x823DDA):
//     no levels -> {0, 1, 1, 1}; else lower_bound(level), the last level when past the end;
//   * ReinforcementSchedule rows "<n> = <round>" (RW 0x8244FD: n >= 1 stored as key n - 1, a '#' math value throws, a duplicate throws) and
//     "EachRemaining = <int>" (+0x0C); getRoundForArmyNumber(k) (RW 0x1292630 in WorldBuilder): the round of key k, else 0 for an empty schedule, else
//     last round + EachRemaining * (k - last key);
//   * the bonus schedules (tables RW 0xC51400 / 0xC51040): Sides (LivingWorldPlayerTemplate names, RW 0x6E398A) and Bonus sub-blocks (RW 0xC513A0
//     MinResourceBonus / RW 0xC50FC0 MinSciencePurchasePointsForBonus, Weapon / ArmorMultiplier reals; duplicate minimum throws); the schedule of a
//     side (RW 0x824722) is the first whose Sides lists the player's template; the bonus for an amount (RW 0x8248BC) is the block of the highest
//     minimum <= amount, else none.
// INFERENCE: the Affects row parser (RW 0x8287E3) was not decompiled: its tokens are read as type names setting bits (the data's use).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;

namespace LivingWorld
{

enum AutoResolveUnitType
{
	AR_SOLDIER = 0,
	AR_ARCHER,
	AR_PIKEMEN,
	AR_CAVALRY,
	AR_MONSTER,
	AR_HERO,
	AR_FORTRESS,
	AR_SIEGE,
	AR_SUPPORT,
	AR_INVALID,
	AR_UNIT_TYPE_COUNT
};
const char *const *autoResolveUnitTypeNames(); // RW 0xDADBE4, null terminated

struct AutoResolveWeapon
{
	std::string name;
	int missPercentChance = 50;        // +0x10
	float damage[AR_UNIT_TYPE_COUNT] = {}; // +0x14
	std::map<int, float> levelBonus;   // +0x04
	bool reduceAttackWhenHurt = true;  // +0x3C

	bool canDamage() const;                                                    // RW 0x829DD9
	float damageForRound(int level, float hp, float maxHp, int targetType) const; // RW 0x829E64
};

struct AutoResolveArmor
{
	std::string name;
	float scalar[AR_UNIT_TYPE_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

	float damageScalar(int attackerType) const { return scalar[attackerType]; } // RW 0x83687D
};

struct AutoResolveBody
{
	std::string name;
	std::map<int, float> hitpoints;  // +0x00
	std::map<int, float> regenerate; // +0x0C
	bool canBeAttacked = true;       // +0x1C
	bool leaveInArmySummary = false; // +0x1D

	float hitpointsForLevel(int level) const; // RW 0x828E4D
	float regenForLevel(int level) const;     // RW 0x828E84
};

struct AutoResolveCombatChain
{
	struct Target
	{
		int type = AR_INVALID;
		int priority = 0;
	};
	std::string name;
	std::vector<Target> targets;            // ascending (priority, type) (RW 0x8294B7)
	std::uint32_t defaultTargets = ~0u;     // +0x10 (only the low 10 bits are ever tested)
	int definitionIndex = 0;                // WOTR2 milestone 4: the order of definition (the AI's building types order chains by their objects, allocated
	                                        // at parse: INFERENCE S-1330)

	bool highestPriorityTarget(Target *out) const;               // RW 0x8295CB
	bool nextTarget(const Target &current, Target *out) const;   // RW 0x8297EE
};

struct AutoResolveLeadership
{
	struct Bonus
	{
		int minLevel = 0;
		int maximumUnitsAffected = 1000000;
		int priority = 10;
		float weapon = 1.0f, experience = 1.0f, armor = 1.0f;
	};
	std::string name;
	std::uint32_t affects = 0;            // +0x04 type bits
	bool affectsHigherLevelFirst = false; // +0x14
	std::map<int, Bonus> bonuses;         // +0x08 by MinLevel

	const Bonus *bonusForLevel(int level) const; // RW 0x8288A4
};

struct AutoResolveHandicap
{
	int guiDisplayedLevel = 0;
	float weapon = 1.0f, armor = 1.0f, experience = 1.0f;
};

struct AutoResolveReinforcementSchedule
{
	std::map<int, int> rounds; // army index (0-based) -> round
	int eachRemaining = 0;

	int roundForArmyNumber(int index) const;
};

struct AutoResolveBonusSchedule
{
	struct Bonus
	{
		int minimum = -1;
		float weapon = 1.0f, armor = 1.0f;
	};
	std::vector<std::string> sides;  // LivingWorldPlayerTemplate names
	std::map<int, Bonus> bonuses;

	const Bonus *bonusFor(int amount) const; // RW 0x8248BC
};

class AutoResolveStores
{
public:
	static const std::vector<std::string> &blockKeywords();
	void registerBlocks(INIBlockRegistry &registry);
	void clear();

	std::map<std::string, AutoResolveWeapon> weapons;
	std::map<std::string, AutoResolveArmor> armors;
	std::map<std::string, AutoResolveBody> bodies;
	std::map<std::string, AutoResolveCombatChain> combatChains;
	std::map<std::string, AutoResolveLeadership> leaderships;
	std::map<int, AutoResolveHandicap> handicaps;
	AutoResolveReinforcementSchedule defenderSchedule; // store +0x0C
	AutoResolveReinforcementSchedule attackerSchedule; // store +0x1C
	std::vector<AutoResolveBonusSchedule> resourceBonuses;
	std::vector<AutoResolveBonusSchedule> sciencePurchasePointBonuses;

	// a named template; nullptr when missing.  The default ones throw (std::runtime_error) when the data has none: every lookup that falls back to
	// them in retail would read a null template.
	const AutoResolveWeapon *findWeapon(const std::string &name) const;
	const AutoResolveArmor *findArmor(const std::string &name) const;
	const AutoResolveBody *findBody(const std::string &name) const;
	const AutoResolveCombatChain *findCombatChain(const std::string &name) const;
	const AutoResolveLeadership *findLeadership(const std::string &name) const;
	const AutoResolveWeapon &defaultWeapon() const;
	const AutoResolveArmor &defaultArmor() const;
	const AutoResolveBody &defaultBody() const;
	const AutoResolveCombatChain &defaultCombatChain() const;
	AutoResolveHandicap handicapFor(int displayedLevel) const;                                    // RW 0x823DDA
	const AutoResolveBonusSchedule *resourceScheduleFor(const std::string &lwTemplate) const;      // RW 0x824722
	const AutoResolveBonusSchedule *scienceScheduleFor(const std::string &lwTemplate) const;
	const AutoResolveReinforcementSchedule &scheduleForSide(int side) const { return side == 0 ? defenderSchedule : attackerSchedule; }

	void parseWeapon(INI *ini);
	void parseArmor(INI *ini);
	void parseBody(INI *ini);
	void parseCombatChain(INI *ini);
	void parseLeadership(INI *ini);
	void parseHandicap(INI *ini);
	void parseReinforcementSchedule(INI *ini);
	void parseBonusSchedule(INI *ini, bool science);
};

} // namespace LivingWorld
