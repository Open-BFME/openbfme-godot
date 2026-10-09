// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/Common/ScoreKeeper.h), as RotWK extends it.
//
// ScoreKeeper: a player's statistics (RotWK Player + 0x3DC). Lane ECON-1 ported the money adders, SPELL-1 the purchase / skill point adders; lane END-1 ports the
// rest of the class the score screen (TimeLine.apt, AptTimeLine) and the victory rules read.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read by END-1 unless marked):
//   * layout (xfer RW 0x79ECCB, version 12; vtable RW 0xC314F0, ctor RW 0x79EFF4, reset RW 0x79EB10):
//       + 4 money earned, + 8 money spent (ECON-1), + 0xC / + 0x10 the money received from / given to allies (RW 0x79DCE9 / 0x79DD01, no caller ported),
//       + 0x14 / + 0x18 / + 0x1C the money spent on units / structures / heroes (RW 0x79DFC2, lane END-2), + 0x20 units destroyed [20] (by the VICTIM's player index), + 0x70 units built,
//       + 0x74 units lost, + 0x78 structures destroyed [20], + 0xC8 structures built, + 0xCC structures lost, + 0xD0 / + 0xD4 heroes / units vetted
//       (RW 0x79DB50 / 0x79DB6B, no caller ported), + 0xD8 science purchase points earned (SPELL-1), + 0xE0 / E4 / E8 Living World region terms,
//       + 0xEC the last calculated score, + 0xF0 the end frame (0 = still running), + 0xF4 the per-frame entry index of the first hero (RW 0x79F1FE),
//       + 0xF8 skill points earned (SPELL-1, float), + 0xFC the elimination bonus (float, RW 0x79DBC6), + 0x100 the owner's player index, + 0x104 the
//       index of the last other playable player that hurt one of ours (-1), + 0x108 / + 0x10C the units / structures alive, + 0x110 "count" (the built /
//       destroyed totals only move while it is set), + 0x114 money earned again, + 0x118 [20] / + 0x168 / + 0x16C / + 0x170 [20] / + 0x1C0 / + 0x1C4
//       second copies of the structure destroyed / built / lost and unit destroyed / built / lost counts, the per-template maps + 0x1C8 / + 0x1D4 /
//       + 0x1E4 / + 0x1F0 / + 0x1FC [20] / + 0x2EC / + 0x2F8, and the per-frame statistics vector + 0x31C (0x14-byte PerFrameStats, vtable RW 0xC314C4:
//       + 4 money, + 8 score (float), + 0xC units alive, + 0xE structures alive, + 0x10 skill points, the three as 16 bits).
//   * reset (RW 0x79EB10): the static KindOf masks STRUCTURE+SCORE (RW 0xDE8294), STRUCTURE+SCORE_DESTROY (RW 0xDE82B0), STRUCTURE+SCORE_CREATE
//     (RW 0xDE82CC); every counter 0, + 0x104 = -1, + 0x100 = the player index, the vectors and maps emptied, + 0x110 = 0.
//   * every adder first tests TheGameLogic + 0x98 (the keep-score switch, `enabled` here); addObjectBuilt / Destroyed / Lost also skip an object whose status
//     DO_NOT_SCORE (bit 0x4C, RW 0x44DDEC) is set.
//   * addObjectBuilt(obj, n) (RW 0x79F0E1): a template of either structure mask: + 0x10C += n, then (when counting) + 0xC8 / + 0x168 += n; else a template the
//     GameData ObjectsThatScore filter accepts (RW 0x7640C1, + 0x1168): + 0x108 += n, then (when counting) + 0x70 / + 0x1C0 += n; a counted object adds n to
//     its template's entries of the maps + 0x1F0 / + 0x1C8 (clamped at 0). Then, for any object while counting: a COMMANDCENTER + SCORE template that is not
//     SUMMONED (mask RW 0xDE82E8, RW 0x79F25B) appends the current per-frame entry count to the vector + 0x328 (the timeline's fortress marks), and the first
//     HERO that is not SUMMONED sets + 0xF4 to that count (RW 0x79F28E). Callers: Object::initObject (RW 0x694140, n = 1, every new object),
//     LifetimeUpdate without ScoreKill (RW 0x7A8017: n = -1 with counting switched off around the call), and eight more (the sell refund RW 0x88D077, a
//     structure that builds itself RW 0x85677A, the object replacements of RW 0x853EDF / 0x8B140D / 0x8B228C / 0x856DF9 / 0x6AF769 and the command
//     dispatcher RW 0x77B2BB) that are not ported (stop S-1061).
//   * addObjectDestroyed(victim) (RW 0x79F303) on the KILLER's keeper: a structure mask -> (counting) + 0x78 / + 0x118 [victim player]++; else the
//     ObjectsThatScore filter -> (counting) + 0x20 / + 0x170 [victim player]++; the counted victim adds 1 to its template in + 0x1FC [victim player].
//   * addObjectLost(victim) (RW 0x79F486) on the victim's keeper, NOT gated by `count`: a structure: + 0x10C-- (clamped at 0), + 0xCC / + 0x16C ++; a scoring unit:
//     + 0x108-- (clamped), + 0x74 / + 0x1C4 ++; the maps + 0x2EC / + 0x1D4; then the victim body's last damage source (body vslot 0x40 -> + 8): its
//     controlling player, when that player's template is a playable side and its index is not ours, becomes + 0x104.
//   * Object::scoreTheKill (RW 0x6956D5 .. 0x695738): addObjectLost on the victim's player first (any scored victim), then, past the enemy and other-owner
//     gates, addObjectDestroyed on the killer's player.
//   * calculateScore (RW 0x79DFFA, all SSE; every partial sum truncated with cvttss2si before the next term):
//       s = trunc(earned * Supplies); s = trunc(s + unitsBuilt * UnitsBuilt); s = trunc(s + structuresBuilt * StructuresBuilt); s = trunc(s + heroesVetted
//       * HeroesVetted); s = trunc(s + unitsVetted * UnitsVetted); s = trunc(s + purchasePoints * PowerPoints); s = trunc(s + objectives * Objectives)
//       (RW 0x79DE90: the completed objectives of TheObjectiveList RW 0xDE8C94); s += timeTaken (RW 0x79DF0E: max(trunc(trunc(TimeTakenMaximumScore) -
//       minutes * TimeTakenMultiplier), trunc(TimeTakenMinimumScore)) with minutes = (endFrame or the frame) / 5 / 60, RW 0x79DC0C / 0x79DF02); then for
//       every player index but ours: s = trunc(s + unitsDestroyed[i] * UnitsDestroyed), s = trunc(s + structuresDestroyed[i] * StructuresDestroyed); then
//       the three region terms; the result is stored at + 0xEC.
//   * recordPerFrameStats(frame) (RW 0x79F704, from Player::update RW 0x6AF3AD with TheGameLogic + 0x40): only for a player that is alive (RW 0x6AAC52: not
//     + 0x35A, not + 0x754) and of a playable side (RW 0x6AAC66): the vector grows to frame + 1 entries; entry[frame] = { money (Player + 0x94; an AI player
//     adds a field of its skirmish AI info record, RW 0x6A950B / 0x6A9999 -> + 0xC -> + 0x14: not identified, stop S-1061), units alive, structures alive,
//     ftol(skill points) (x87 fld + _ftol, RW 0x79F7CB), the running score: skill * SkillPoints, + (earned - [+0xC]) * Supplies, + structuresBuilt *
//     StructuresBuilt, + unitsBuilt * UnitsBuilt, per other index + unitsDestroyed * UnitsDestroyed + structuresDestroyed * StructuresDestroyed,
//     + purchasePoints * PowerPoints, + heroesVetted * HeroesVetted, + unitsVetted * UnitsVetted, + the elimination bonus (all mulss / addss, no truncation) }.
//   * addEliminationBonus (RW 0x79DBC6): + 0xFC += value (addss), behind the switch; VictoryConditions' killPlayer gives the last attacker the loser's last
//     entry score * PlayerEliminatedMultiplier (RW 0x6ABF9E .. 0x6ABFEE).
// DONOR: ZH ScoreKeeper.cpp (the object counts and their kind masks; the ZH score formula is NOT RotWK's).
//   * addMoneySpentByKind(template, amount) (RW 0x79DFC2, NOT behind the switch): a null template or a HERO -> + 0x1C, else a STRUCTURE -> + 0x18, else
//     + 0x14; then + 0x1E0 += amount (all int adds). Callers: the unit queue after its withdraw (RW 0x8A12AD), the queue cancel after its refund with the
//     negated cost (RW 0x8A1471), the plot / dozer builds (RW 0x858B1E / 0x88D34A) and the wall span (RW 0x795647) (lane END-2).
//   * addObjectBuilt also adds n to + 0x1E4[template] (clamped at 0) for a HORDE template, counting or not (RW 0x79F227 .. 0x79F26B).
//   * the score screen's queries (lane END-2): the favourite unit (RW 0x79E3A8 -> 0x79E2A8): over + 0x1F0, the template whose KindOf meets the caller's
//     mask (RW 0x9CD60C: HORDE, HERO, MONSTER, MACHINE, SHIP, MINE) and one of HERO / STRUCTURE / MONSTER / MACHINE / HORDE / SHIP / MINE, and is neither
//     DEPLOYED_MINE nor HORDE_MONSTER, with the strictly highest count; then over + 0x1E4 any template that meets the mask; the result is its display name.
//     Heroes built / lost (RW 0x79E3D6 / 0x79E44C -> 0x79E404): the sum over + 0x1F0 / + 0x2EC of the templates that are HERO and not SUMMONED.
// INFERENCE / NOT PORTED (stop S-1061): the maps are keyed by template name (retail: by ThingTemplate pointer, so retail's iteration order is the address order:
// a tie of the favourite unit may pick another template); the + 0x2F8 map and the hero / unit vetted adders have no ported caller; the position of the
// per-frame recording in the logic frame (Player::update's caller was not found: it runs at the end of the frame here).

#pragma once

#include "Common/NumericState.h"
#include "Common/StateHash.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class ThingTemplate;
struct GameLogicSettings;

class ScoreKeeper
{
public:
	static constexpr int MAX_PLAYERS = 20; // RW 0x79EBBD: the arrays have 0x14 entries

	// RW 0xC314C4: one entry per logic frame of a live player
	struct PerFrameStats
	{
		std::uint32_t money = 0;      ///< + 4
		float score = 0.0f;           ///< + 8
		std::uint16_t unitsAlive = 0; ///< + 0xC
		std::uint16_t structuresAlive = 0; ///< + 0xE
		std::uint16_t skillPoints = 0; ///< + 0x10
		bool recorded = false;        ///< retail's default-constructed entries (a frame the player did not record) are all zero; kept apart for the graph
	};

	// the keep-score switch (GameLogic + 0x98): the economy copies the context's value into every player's keeper (EconomySystem::setScoring)
	void setEnabled(bool on) { m_enabled = on; }
	bool enabled() const { return m_enabled; }
	// RW 0x79EB10 (the switch is the logic's, it stays)
	void reset(int playerIndex = -1);

	void addMoneyEarned(std::int32_t amount)
	{
		if (m_enabled)
		{
			m_moneyEarned += (std::uint32_t)amount; // RW + 4 and + 0x114: both 32-bit adds
			m_moneyEarnedSecond += (std::uint32_t)amount;
		}
	}
	void addMoneySpent(std::int32_t amount)
	{
		if (m_enabled)
		{
			m_moneySpent += (std::uint32_t)amount; // RW + 8
		}
	}
	// lane SPELL-1: RW 0x79DB86 (+ 0xD8 += the science purchase points a rank or a positive grant gave) and RW 0x79DBA1 (+ 0xF8 += the skill points of a
	// kill, addss), both behind the same switch
	void addSciencePurchasePointsEarned(std::int32_t amount)
	{
		if (m_enabled)
		{
			m_purchasePointsEarned += (std::uint32_t)amount;
		}
	}
	void addSkillPointsEarned(float points)
	{
		if (m_enabled)
		{
			m_skillPointsEarned = NumericState::sseAdd(points, m_skillPointsEarned);
		}
	}
	// RW 0x79DBC6
	void addEliminationBonus(float value)
	{
		if (m_enabled)
		{
			m_eliminationBonus = NumericState::sseAdd(value, m_eliminationBonus);
		}
	}
	// RW 0x79DFC2 (lane END-2; see the header): `tmpl` the produced / built template, null for none
	void addMoneySpentByKind(GameLogic &logic, const ThingTemplate *tmpl, int amount);
	// RW 0x79DC9E: + 0x110
	void setCounting(bool on) { m_counting = on; }
	bool counting() const { return m_counting; }

	// RW 0x79F0E1 / 0x79F303 / 0x79F486 (see the header)
	void addObjectBuilt(GameLogic &logic, const Object &obj, int count);
	void addObjectDestroyed(GameLogic &logic, const Object &victim);
	void addObjectLost(GameLogic &logic, const Object &victim);
	// RW 0x79F704: `frame` is TheGameLogic + 0x40; `player` is the keeper's owner
	void recordPerFrameStats(GameLogic &logic, const Player &player, unsigned frame);
	// RW 0x79DFFA without the store at + 0xEC (the client may ask at any time: nothing changes); `frame` is the logic frame
	int computeScore(const GameLogicSettings &settings, unsigned frame) const;
	// RW 0x79DFFA: computeScore and the store at + 0xEC
	int calculateScore(const GameLogicSettings &settings, unsigned frame)
	{
		m_currentScore = computeScore(settings, frame);
		return m_currentScore;
	}
	// RW 0x79DF0E: the time-taken term
	static int timeTakenScore(const GameLogicSettings &settings, unsigned framesTaken);

	std::uint32_t moneyEarned() const { return m_moneyEarned; }
	std::uint32_t moneySpent() const { return m_moneySpent; }
	std::uint32_t moneyEarnedSecond() const { return m_moneyEarnedSecond; }
	std::uint32_t sciencePurchasePointsEarned() const { return m_purchasePointsEarned; }
	float skillPointsEarned() const { return m_skillPointsEarned; }
	float eliminationBonus() const { return m_eliminationBonus; }
	int unitsBuilt() const { return m_unitsBuilt; }
	int unitsLost() const { return m_unitsLost; }
	int structuresBuilt() const { return m_structuresBuilt; }
	int structuresLost() const { return m_structuresLost; }
	int unitsDestroyed(int victimIndex) const { return victimIndex >= 0 && victimIndex < MAX_PLAYERS ? m_unitsDestroyed[victimIndex] : 0; }
	int structuresDestroyed(int victimIndex) const { return victimIndex >= 0 && victimIndex < MAX_PLAYERS ? m_structuresDestroyed[victimIndex] : 0; }
	// RW 0x79DC74 over + 0x20 / RW 0x79DC49 over + 0x78: the sums over every index (the owner's own entry included)
	int totalUnitsDestroyed() const;
	int totalStructuresDestroyed() const;
	int unitsAlive() const { return m_unitsAlive; }
	int structuresAlive() const { return m_structuresAlive; }
	int playerIndex() const { return m_playerIndex; }
	int lastAttackerIndex() const { return m_lastAttacker; }
	int currentScore() const { return m_currentScore; }
	unsigned endFrame() const { return m_endFrame; }
	void setEndFrame(unsigned frame) { m_endFrame = frame; }
	const std::vector<PerFrameStats> &perFrameStats() const { return m_perFrame; }
	// RW + 0x328: the per-frame entry indices at which a fortress (COMMANDCENTER + SCORE, not SUMMONED) was built; RW + 0xF4: the first hero's
	const std::vector<int> &fortressMarks() const { return m_fortressMarks; }
	int firstHeroEntry() const { return m_firstHeroEntry; }
	const std::map<std::string, int> &builtByTemplate() const { return m_builtByTemplate; }
	const std::map<std::string, int> &lostByTemplate() const { return m_lostByTemplate; }
	const std::map<std::string, int> &destroyedByTemplate(int victimIndex) const;
	const std::map<std::string, int> &hordesBuiltByTemplate() const { return m_hordesBuilt; }
	std::uint32_t moneyReceivedFromAllies() const { return m_moneyAdjust; }  // RW + 0xC
	std::uint32_t moneyGivenToAllies() const { return m_moneyGiven; }        // RW + 0x10
	int moneySpentOnUnits() const { return m_spentUnits; }                   // RW + 0x14
	int moneySpentOnStructures() const { return m_spentStructures; }         // RW + 0x18
	int moneySpentOnHeroes() const { return m_spentHeroes; }                 // RW + 0x1C
	int moneySpentByKind() const { return m_spentTotal; }                    // RW + 0x1E0
	// lane END-2, the score screen's statistics: RW 0x79E2A8 (the template name of the favourite unit, "" for none) and RW 0x79E3D6 / 0x79E44C
	std::string favoriteUnit(GameLogic &logic) const;
	int heroesBuilt(GameLogic &logic) const;
	int heroesLost(GameLogic &logic) const;

	// the counters a saved game restores (the ScoreKeeper xfer, RW 0x79ECCB; the save game itself is not ported: tests set them through here)
	struct Totals
	{
		std::uint32_t moneyEarned = 0;
		int unitsBuilt = 0, structuresBuilt = 0;
		std::uint32_t purchasePoints = 0;
		int unitsDestroyed[MAX_PLAYERS] = {}, structuresDestroyed[MAX_PLAYERS] = {};
		unsigned endFrame = 0;
	};
	void restoreTotals(const Totals &t);

	struct Counters
	{
		unsigned long long builtCalls = 0, destroyedCalls = 0, lostCalls = 0, framesRecorded = 0;
		unsigned long long doNotScoreSkipped = 0; ///< DO_NOT_SCORE objects
		unsigned long long aiMoneyTermsUnported = 0; ///< per-frame entries of an AI player whose AI money term (S-1061) was not added
	};
	const Counters &counters() const { return m_counters; }

	void crc(StateHasher &h) const;

private:
	static bool isScoringStructure(const Object &obj, bool forDestroy);
	static bool isScoringStructureForBuild(const Object &obj);
	static bool doNotScore(const Object &obj);
	static void addToMap(std::map<std::string, int> &map, const std::string &key, int count, bool clampAtZero);

	bool m_enabled = true;                 // GameLogic + 0x98 (INFERENCE for the default: S-251)
	std::uint32_t m_moneyEarned = 0;       // RW + 4
	std::uint32_t m_moneySpent = 0;        // RW + 8
	std::uint32_t m_moneyEarnedSecond = 0; // RW + 0x114
	std::uint32_t m_moneyAdjust = 0;       // RW + 0xC (its adder RW 0x79DCE9 has no ported caller: S-1061)
	std::uint32_t m_moneyGiven = 0;        // RW + 0x10 (its adder RW 0x79DD01 has no ported caller: S-1061)
	int m_spentUnits = 0;                  // RW + 0x14 (lane END-2)
	int m_spentStructures = 0;             // RW + 0x18
	int m_spentHeroes = 0;                 // RW + 0x1C
	int m_spentTotal = 0;                  // RW + 0x1E0
	int m_unitsDestroyed[MAX_PLAYERS] = {};      // RW + 0x20
	int m_unitsBuilt = 0;                        // RW + 0x70
	int m_unitsLost = 0;                         // RW + 0x74
	int m_structuresDestroyed[MAX_PLAYERS] = {}; // RW + 0x78
	int m_structuresBuilt = 0;                   // RW + 0xC8
	int m_structuresLost = 0;                    // RW + 0xCC
	std::uint32_t m_purchasePointsEarned = 0;    // RW + 0xD8 (SPELL-1)
	int m_currentScore = 0;                      // RW + 0xEC
	unsigned m_endFrame = 0;                     // RW + 0xF0
	int m_firstHeroEntry = 0;                    // RW + 0xF4
	float m_skillPointsEarned = 0.0f;            // RW + 0xF8 (SPELL-1)
	float m_eliminationBonus = 0.0f;             // RW + 0xFC
	int m_playerIndex = -1;                      // RW + 0x100
	int m_lastAttacker = -1;                     // RW + 0x104
	int m_unitsAlive = 0;                        // RW + 0x108
	int m_structuresAlive = 0;                   // RW + 0x10C
	bool m_counting = false;                     // RW + 0x110
	int m_structuresDestroyed2[MAX_PLAYERS] = {}; // RW + 0x118
	int m_structuresBuilt2 = 0;                  // RW + 0x168
	int m_structuresLost2 = 0;                   // RW + 0x16C
	int m_unitsDestroyed2[MAX_PLAYERS] = {};     // RW + 0x170
	int m_unitsBuilt2 = 0;                       // RW + 0x1C0
	int m_unitsLost2 = 0;                        // RW + 0x1C4
	std::map<std::string, int> m_builtByTemplate2;   // RW + 0x1C8
	std::map<std::string, int> m_lostByTemplate2;    // RW + 0x1D4
	std::map<std::string, int> m_hordesBuilt;        // RW + 0x1E4 (lane END-2)
	std::map<std::string, int> m_builtByTemplate;    // RW + 0x1F0
	std::map<std::string, int> m_destroyedByTemplate[MAX_PLAYERS]; // RW + 0x1FC
	std::map<std::string, int> m_lostByTemplate;     // RW + 0x2EC
	std::vector<PerFrameStats> m_perFrame;           // RW + 0x31C
	std::vector<int> m_fortressMarks;                // RW + 0x328
	Counters m_counters;                             // diagnostics, not state
};
