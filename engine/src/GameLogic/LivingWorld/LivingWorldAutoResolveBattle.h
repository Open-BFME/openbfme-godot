// OpenBFME. GPL-3.0.
//
// LivingWorldAutoResolveBattle: the War of the Ring autoresolve battle of RotWK 2.01 (WorldBuilder's trace names: LivingWorldAutoResolveBattle,
// LivingWorldAutoResolveUnit, LivingWorldLogic::SetUpAutoResolveBattle / StartAutoResolveBattle).  Lane WOTR-2.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the matching WorldBuilder functions, RotWK's WorldBuilder.exe, carry the trace names):
//   * the battle (0x80 bytes, ctor RW 0x8FF0AC = WB 0x1312EB0): +0x00 the player records (0x34 bytes each: +0x00 a multimap round -> army,
//     +0x0C the extra units (the region's CreateUnitDuringAutoResolve buildings, an auto fort, free inn units), +0x18 / +0x1C / +0x20 the weapon /
//     armor / experience multipliers, +0x24 the science bonus schedule, +0x28 the player's SciencePurchasePointTracker (the resolver's), +0x2C the
//     side, +0x30 the player id); +0x0C + side * 0x0C the units fighting on each side; +0x24 + side * 0x0C the reinforcement maps (round -> units);
//     +0x48 / +0x54 per record the battle's own tracker / the science bonus; +0x78 the winning side (-1 while fighting); +0x7C the round;
//   * the ctor: round 0; for each record (in order): a copy of its tracker when it has one (RW 0x8FF19B: RW 0x8FAD90, the base vtable) goes to
//     +0x48, its science bonus (RW 0x8FB695: tracker and schedule set: the schedule's bonus at the tracker's purchase points + 0x1C, RW 0x8248BC;
//     else none), its extra units go straight onto its side (RW 0x8FE33B); then buildReinforcementMaps (RW 0x8FEED6:
//     each record's armies in multimap order, each army's units into map[side][round]), addUnitsForCurrentRound (RW 0x8FDEE9), the winner check
//     (RW 0x8FAFB8);
//   * adding units (RW 0x8FE33B): every summary entry becomes a unit (RW 0x9A38A1: record +0x30, army +0x28, ThingTemplate +0x2C (null for an unknown
//     name: the unit is dropped again), its experience tracker +0x24, hp = hp at round start = max hp = the body's hitpoints for its level), appended,
//     then the whole vector is sorted (STLport sort, RW 0x8FE222) by level, the highest first (RW 0x8FC5C1);
//   * a round (RW 0x8FEDF2), only while there is no winner: round + 1; the units of map[side][round] join their side (appended, then std::inplace_merge
//     by the same order); every unit's round start (RW 0x9A35B1: hp at round start = hp, max hp = the body's hitpoints for its level, not attacked);
//     the leadership (RW 0x8FEC1C); then target choice (RW 0x8FE401) and attacks (RW 0x8FE6AF) repeat until no attacker found its target dead;
//     completeRound (RW 0x8FEA4D); both sides re-sorted (RW 0x8FEB3C); the winner check;
//   * leadership (RW 0x8FEC1C), per side: every unit with an AutoResolveLeadership whose getBonusForLevel(level) exists gives {bonus, template, leader};
//     the list is STLport-sorted by the bonus Priority, highest first (RW 0x8FCE3F); each entry then walks the side's units (from the front when
//     AffectsHigherLevelFirst, else from the back) giving its bonus to units without one whose type the template Affects, that are not the leader and
//     have hp > 0, up to MaximumUnitsAffected;
//   * target choice (RW 0x8FE401), per side: every unit loses its target; a unit that can attack (RW 0x9A3573: not attacked this round, hp at round
//     start > 0, its weapon canDamage) counts and, when its combat chain has a highest-priority target, goes onto the side's attacker heap
//     {unit, type, priority} (push_heap, comparator "priority greater": the LOWEST priority is on top, RW 0x8FB983); a unit that can be attacked
//     (RW 0x9A34B5: hp > 0 and the body CanBeAttacked) counts and is appended to the side's list of its type.  Then per side, when the enemy has
//     targets: quota = attackers / enemy targets, remainder = attackers % enemy targets; pop the heap: walk the enemy list of the wanted type from
//     the front: a target with fewer attackers than the quota (or exactly the quota while remainder > 0, which uses one) takes the attacker (its
//     count + 1), else it leaves the list; a list that runs empty sends the attacker back onto the heap with the chain's next target, if any;
//   * attacks (RW 0x8FE6AF), side 0 then side 1, units in order: a unit that can attack and has a target: when the target's hp <= 0 the round is not
//     resolved (targets are chosen again); else, unless the logic's no-miss flag (RW 0x6B3025: +0xB4 and the tutorial) is set, roll =
//     GetGameLogicRandomValue(1, 100) (RW 0x6D328E, line 0x331) and the attack misses when roll <= the weapon's MissPercentChance; a hit takes
//     target.rec.armor * attacker.rec.weapon * target science armor * attacker science weapon * target leader ArmorMultiplier * target armor
//     getDamageScalar(attacker type) * attacker leader WeaponMultiplier * getDamageForRound(level, hp at round start, max hp for level, target type)
//     (SSE, that order) off the target's hp; a target brought to hp <= 0 gives the attacker (int)(rec.experience * the target's experience value
//     (RW 0x9A35DE) [* leader ExperienceMultiplier]) experience points (RW 0x9A34F0 -> the tracker's addExperiencePoints) and a kill; before the
//     leader's multiplier, rec.experience * the value goes to the attacker record's tracker (RW 0x8FE8C6: addSkillPoints(points, true)).  Hit or
//     miss, the attacker has attacked;
//   * completeRound (RW 0x8FEA4D): per side from the back, a dead unit (RW 0x9A34CD: no template, or hp <= 0 without LeaveInArmySummary) leaves
//     (RW 0x8FE971: a building's unit marks the building destroyed), any other unit rescales hp to a changed max hp, regenerates (round < 500) up
//     to the max and loses its leadership bonus (RW 0x9A360F); every record's science bonus is read again (RW 0x8FEAB5 -> 0x8FB695); from round
//     1000 the side with fewer units loses outright (its units' hp 0, the dead ones removed);
//   * the winner check (RW 0x8FAFB8): a side without a unit that can be attacked loses (side 1 checked first: side 0 wins when side 1 has none,
//     else side 1 wins when side 0 has none);
//   * applyResults (RW 0x8FC8A9): destroyed buildings, the kill notices; per record with both trackers, a temporary copy of the resolver's tracker
//     (RW 0x8FC954: RW 0x8FAD90 into [ebp - 0x48]) gets addSkillPoints((float)(int)(battle tracker's - its skill points), false) (RW 0x8FC959 ..
//     0x8FC97A: cvttss2si, ecx = the copy) and is destroyed, so the resolver's tracker keeps its value; every army whose round was reached is
//     marked as having fought (+0x2C = 4) and rebuilt from its surviving units (summary entries with the units' new experience), armies still
//     waiting are untouched.
// INFERENCE (stop S-1328): the experience value of a killed unit (RW 0x9A35DE's visitor RW 0xC88350 was not decompiled) is the ExperienceAward of the
// level its experience reached, a horde's the sum over its InitialPayload members times their count; experience gain skips the template's rank scalar
// table (RW 0x79D4B9); the no-miss flag is false (no tutorial); the trackers skip their scalar table (SciencePurchasePointTracker.h).

#pragma once

#include "GameLogic/LivingWorld/LivingWorldAutoResolveData.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/LivingWorld/SciencePurchasePointTracker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameLogicRandom;

namespace LivingWorld
{

// one army summary entry
struct AutoResolveUnitSpec
{
	std::string thingTemplate;
	int count = 1;           // summary entry +0xA0 (free inn units: the bonus count; one per unit otherwise)
	float experience = 0.0f;
	std::vector<std::string> upgrades;
	int armyId = 0;          // 0: not an army unit (a building's or a bonus unit)
	int memberIndex = -1;    // the army member the unit came from
	int buildingRegion = -1; // a CreateUnitDuringAutoResolve building's unit: its region and slot
	int buildingSlot = -1;
};

struct AutoResolveArmySpec
{
	int armyId = 0;
	bool garrison = false; // no hero name: always round 0 (RW 0x6BD3F3), not counted by the reinforcement schedule
	std::vector<AutoResolveUnitSpec> units;
};

struct AutoResolvePlayerSpec
{
	int player = -1;
	int side = 0;                   // 0: the region owner's team (the defenders), 1: everyone else
	float weapon = 1.0f, armor = 1.0f, experience = 1.0f; // record +0x18 / +0x1C / +0x20
	std::vector<AutoResolveArmySpec> armies; // the battle's armies of this player, battle order
	std::vector<AutoResolveUnitSpec> extraUnits; // record +0x0C
	// lane WOTR-2: record + 0x24 / + 0x28, set together by SetUpAutoResolveBattle (RW 0x6BD73A .. 0x6BD7DF) when the resolver passes its trackers,
	// as every War of the Ring autoresolve does: the science purchase point bonus schedule whose Sides list the player's template (RW 0x824722,
	// null: none) and the resolver's tracker of the player (LivingWorldLogic::autoResolve)
	const AutoResolveBonusSchedule *scienceSchedule = nullptr;
	std::optional<SciencePurchasePointTracker> rankTracker;
};

class AutoResolveBattle
{
public:
	AutoResolveBattle(const AutoResolveStores &stores, const UnitInfoSource &units, const std::vector<AutoResolvePlayerSpec> &players, GameLogicRandom &rng);
	~AutoResolveBattle();
	AutoResolveBattle(const AutoResolveBattle &) = delete;
	AutoResolveBattle &operator=(const AutoResolveBattle &) = delete;

	int winner() const { return m_winner; } // the winning side, -1 while fighting
	int round() const { return m_round; }
	void update();       // one round (RW 0x8FEDF2)
	void runToEnd();     // rounds until there is a winner (the round-1000 rule ends every battle)

	struct Survivor
	{
		AutoResolveUnitSpec spec; // experience updated
		int record = 0;
		float hp = 0.0f;
		int level = 0;         // WOTR2 milestone 5: the level the summary entry takes (+0x0C, RW 0x9EE009)
		bool rankedUp = false; // reached a rank above 1 in this battle (the unit's flag RW 0x9EE028 clears after its creation; StrategicConflictResults' veterancy)
	};
	// the units left on both sides, side 0 first, each side in its order (RW 0x8FC8A9's last loop)
	std::vector<Survivor> survivors() const;
	// the armies whose round was reached (they fought and are rebuilt from their survivors), record then multimap order
	std::vector<int> enteredArmies() const;
	struct DestroyedBuilding
	{
		int region = -1, slot = -1;
	};
	const std::vector<DestroyedBuilding> &destroyedBuildings() const { return m_destroyedBuildings; }
	// lane WOTR-2: the dead heroes of the battle's armies, in removal order (battle + 0x6C, filled by RW 0x8FE971 for a KindOf HERO unit with an army)
	const std::vector<AutoResolveUnitSpec> &deadHeroes() const { return m_deadHeroes; }
	int killCount(int side) const { return m_kills[side]; }
	// WOTR2 milestone 5: every lethal hit as {killer record, victim record} (RW 0x8FE8F4: the battle's kill list, read by RW 0x7F6443)
	struct KillEvent
	{
		int killerRecord = 0, victimRecord = 0;
		AutoResolveUnitSpec victim; // the victim unit's entry (its template, building, army)
	};
	const std::vector<KillEvent> &killEvents() const { return m_killEvents; }
	int randomDraws() const { return m_draws; }
	// lane WOTR-2: a record's own tracker (battle + 0x48, null without one) and its science bonus (+ 0x54, null: none), as the last read left them
	const SciencePurchasePointTracker *recordTracker(size_t record) const;
	const AutoResolveBonusSchedule::Bonus *scienceBonus(size_t record) const;
	// summary entries whose template is unknown (dropped like retail drops a unit without a ThingTemplate)
	const std::vector<std::string> &droppedUnits() const { return m_dropped; }

private:
	struct Unit;
	struct Record;
	Unit *makeUnit(const AutoResolveUnitSpec &spec, int record);
	void addUnits(std::vector<Unit *> &dest, const std::vector<AutoResolveUnitSpec> &specs, int record);
	void addUnitsForCurrentRound();
	void assignLeadership();
	void chooseTargets();
	bool attackRound();
	void completeRound();
	void checkWinner();
	void updateScienceBonus(size_t record); // RW 0x8FB695
	void removeUnit(std::vector<Unit *> &side, size_t index);
	float maxHitpoints(const Unit &u) const;
	int experienceValue(const Unit &u) const;
	void addExperience(Unit &u, int points);

	const AutoResolveStores &m_stores;
	const UnitInfoSource &m_unitInfo;
	GameLogicRandom &m_rng;
	std::vector<std::unique_ptr<Unit>> m_storage;
	std::vector<std::unique_ptr<UnitInfo>> m_infos;
	std::vector<Record> m_records;
	std::vector<Unit *> m_sides[2];
	std::vector<std::pair<int, std::vector<Unit *>>> m_reinforcements[2]; // ordered by round
	int m_winner = -1;
	int m_round = 0;
	int m_kills[2] = { 0, 0 };
	std::vector<KillEvent> m_killEvents;
	int m_draws = 0;
	std::vector<DestroyedBuilding> m_destroyedBuildings;
	std::vector<AutoResolveUnitSpec> m_deadHeroes;
	std::vector<std::string> m_dropped;
};

} // namespace LivingWorld
