// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PlayerScience: a player's rank, skill points, science purchase points and sciences (ZH Player::m_rankLevel, m_skillPoints,
// m_sciencePurchasePoints, m_sciences, addSkillPoints, setRankLevel, addScience, attemptToPurchaseScience ... in Source/Common/RTS/Player.cpp).
// Lane SPELL-1. RotWK moved the rank half into a sub-object at Player + 8 with its own vtable; the science half stays in Player.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly). The sub-object ("rank", Player + 8; base vtable RW 0xC13E98, the
// Player's RW 0xC14198):
//   +0x04 a pointer to PlayerTemplate + 0x154 {Side copy, IntrinsicSciencePurchasePoints, MaxLevelMP, MaxLevelSP} (set by Player::init RW 0x6B0509;
//         null -> RW 0xDC62B8, an empty record), +0x08 the PlayerSkillPointsScalarTable (constructor RW 0x782A0C looks it up by name),
//   +0x0C skill points (float, 0), +0x10 skill point modifier (float, 1.0), +0x14 rank level (1), +0x18 the rank the scalar table starts at (1),
//   +0x1C science purchase points, +0x20 the skill points the next rank needs (INT_MAX when there is none), +0x24 the skill points the current
//   rank needed, +0x28 a rank cap (0 = none).
//   vslots: 1 setRankLevel (base RW 0x782865, Player RW 0x6AA9C2 adds the local player's rank-up EVA), 2 addSciencePurchasePoints (base RW 0x782764,
//   Player RW 0x6AA980 first adds a positive delta to the score keeper + 0xD8 via RW 0x79DB86), 3 reset (base RW 0x782774, Player RW 0x6AEE11 then
//   resets the sciences RW 0x6AED4B), 4 rankGranted(PlayerRankInfo) (base: nothing; Player RW 0x6AE332: score keeper + 0xD8 += its
//   SciencePurchasePointsGranted, then addScience for each of its SciencesGranted), 5 a campaign test (Player RW 0x6AA975 -> RW 0x602E64).
//   * reset (RW 0x782774): +0x28 = 0, rank = 1, +0x18 = 1, skill points = 0.0; purchase points = IntrinsicSciencePurchasePoints (0 without a
//     template) + rank 1's SciencePurchasePointsGranted (0 without rank 1); then the needed counts (RW 0x7826F6).
//   * the needed counts (RW 0x7826F6): +0x20 = rank (level + 1) ? its getSkillPointsNeeded(side) : 0x7FFFFFFF; +0x24 = rank (level) ? its count : 0.
//   * addSciencePurchasePoints (RW 0x782764): points += delta, then 0 when negative.
//   * the max rank (RW 0x7827C8): when vslot 5 is true and the Living World manager exists: min(rank count, RW 0x6B34EE); else with a template:
//     min(rank count, MaxLevelMP) in a skirmish (GameLogic + 0x110 == 2) or multiplayer game (RW 0x441B7C), min(rank count, MaxLevelSP) otherwise;
//     without a template the rank count. Vslot 5 (RW 0x602E64) is false in a skirmish: GameLogic + 0x114 is 3 (constructor RW 0x6301D3; no other
//     writer changes it on a normal new game: RW 0x7790EA, 0x779CB4, 0x779D23, 0x779F20, 0x82BC22; stop S-525) and the Living World test RW 0x6253FD needs Living World + 0xB4.
//   * setRankLevel(n) (RW 0x782865): n is clamped to 1 .. rank count, then to the max rank; equal to the current rank: false. Lower: vslot 3 (reset)
//     first. Then for each rank r from current + 1 to n: purchase points += r's SciencePurchasePointsGranted (then 0 when negative); when
//     (int)floor(skill points) < r's needed count, skill points = (float)needed; vslot 4 (rankGranted r). Then rank = n, the needed counts; true.
//   * addSkillPoints(points, applyScalar) (RW 0x782AA4): points = modifier * points (mulss); with a cap (+0x28 > 0) and rank >= cap: false. With
//     applyScalar, GameLogic + 0x114 != 3 and a scalar table: points = table[min(rank - base, size - 1)] * points (index < 0: unscaled; the
//     skirmish never scales, S-525). points == 0.0: false. Skill points = min(skill points + points, (float)needed(max rank)); then while
//     (int)floor(skill points) >= the next rank's count: setRankLevel(rank + 1) (vslot 1), stop when it returns false. Returns whether any rank
//     was gained.
//   * Player::addSkillPointsForKill (RW 0x6AAFC3: the killer's player, the killer, the victim, a scale): neither null; the victim not effectively
//     dead (RW 0x44DDEC(2)); a KindOf gate on the killer's template (RW 0x6AAFF2 .. 0x6AB065: STRUCTURE / PROJECTILE-type bits and its
//     container): see Object.cpp; the victim's controlling player is not this player; points = (float)victim experience value (the victim's
//     ExperienceTracker RW 0x79D1DB: its +0x14, 0 when the killer is an ally of the victim) * scale; points > 0.0: score keeper + 0xF8 += points
//     (RW 0x79DBA1), addSkillPoints(points, true).
//   * the sciences (Player + 0x310, a vector): hasScience RW 0x6AC207 (std::find). addScience (RW 0x6AE186): already owned: false; else appended,
//     then every special power module of every object of the player's teams is told (RW 0x6AE1C9 .. 0x6AE2E6: a module whose template's
//     RequiredSciences contains the science: vslot +0x1C, then its ready frame), the control bar is marked dirty and the script engine notified
//     (RW 0x759A4E): true. grantScience (RW 0x6AE3A7): grantable -> addScience. attemptToPurchaseScience (RW 0x6AE36F): isCapableOfPurchasing
//     (RW 0x6AC8BC: science != -1, not owned, prerequisites (RW 0x6AA8CE -> 0x5FED05), cost != 0 and cost <= purchase points) -> purchase
//     points -= cost through vslot 2, addScience.
//   * resetSciences (RW 0x6AED4B): every owned science is reported to the script engine as removed (RW 0x6AD142) and the list cleared; sciences =
//     the template's IntrinsicSciencesMP in a skirmish / multiplayer game (RW 0x625456), IntrinsicSciences otherwise; then for every rank 1 ..
//     current, its SciencesGranted are added (addScience, so no duplicates).
// DONOR: ZH Player.cpp (the same rules without the RotWK sub-object, the side columns or the MP fields).

#pragma once

#include "Common/Science.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class PlayerTemplate;
class PlayerRankInfo;
class ScoreKeeper;
class StateHasher;

// How the game the player is in reads the spell book rules (RotWK reads these from TheGameLogic; the game start sets them)
struct SpellGameMode
{
	bool skirmishOrMultiplayer = false; ///< GameLogic + 0x110 == 2 or RW 0x441B7C: MP costs, IntrinsicSciencesMP, MaxLevelMP
	bool campaign = false;              ///< RW 0x5FF924 (SkillPointsNeededCampaign)
};

class PlayerScience : public ScienceOwner
{
public:
	PlayerScience() = default;

	// Player::init: the template (null for the neutral player), the game mode, the score keeper and the stores this player reads
	void bind(const PlayerTemplate *pt, const SpellGameMode &mode, ScoreKeeper *score);
	const SpellGameMode &mode() const { return m_mode; }

	// vslot 3 (RW 0x6AEE11): the rank reset and the sciences reset
	void reset();
	bool setRankLevel(int level);                 // vslot 1 (RW 0x782865)
	void addSciencePurchasePoints(int delta);     // vslot 2 (RW 0x6AA980 -> 0x782764)
	bool addSkillPoints(float points, bool applyScalar); // RW 0x782AA4
	int getMaxRankLevel() const;                  // RW 0x7827C8

	int getRankLevel() const { return m_rankLevel; }
	float getSkillPoints() const { return m_skillPoints; }
	int getSciencePurchasePoints() const override { return m_sciencePurchasePoints; }
	int getSkillPointsLevelUp() const { return m_skillPointsNextRank; }
	int getSkillPointsLevelDown() const { return m_skillPointsThisRank; }
	void setSkillPointsModifier(float m) { m_skillPointsModifier = m; }
	void setRankLevelCap(int cap) { m_rankCap = cap; }

	bool hasScience(ScienceType st) const override; // RW 0x6AC207
	const ScienceVec &sciences() const { return m_sciences; }
	bool addScience(ScienceType st);               // RW 0x6AE186
	bool grantScience(ScienceType st);             // RW 0x6AE3A7
	bool isCapableOfPurchasingScience(ScienceType st) const; // RW 0x6AC8BC
	bool attemptToPurchaseScience(ScienceType st); // RW 0x6AE36F

	// RW 0x6AE1C9 .. 0x6AE2E6: the special power modules of the player's objects are told about a new science (installed by the logic that owns
	// the objects; without it the notification is counted, stop S-526)
	void setScienceAddedHook(std::function<void(ScienceType)> hook) { m_onScienceAdded = std::move(hook); }
	unsigned long long droppedScienceNotifications() const { return m_droppedNotifications; }

	// lane AUDIO-4 (QA-1 U20): what the player tells the script engine about its sciences, in call order. TARGET FACTS: addScience (RW 0x6AE186)
	// calls ScriptEngine::notifyOfAcquiredScience (RW 0x759A4E: push_back on the engine's vector for the player index, + 0x1A3A8 + 0xC * index);
	// resetSciences (RW 0x6AED4B) first reports every owned science as removed (RW 0x6AD142 -> RW 0x75950A: the first equal entry of that vector
	// is erased) and, after rebuilding the list (whose addScience calls notify too), notifies every science of the new list once more (RW 0x6AEDBF
	// loop). The log keeps those calls so each reader (the logic ScriptEngine, the client's music scripts) replays them into its own queue
	// (AcquiredScienceQueue): reading never changes the player. Cleared by bind (a new game's player); not hashed: it is the call history whose
	// result m_sciences carries, and the queues built from it are hashed by their owner.
	struct ScriptNotice
	{
		ScienceType science = SCIENCE_INVALID;
		bool acquired = true; ///< false: the removal notice (RW 0x75950A)
	};
	const std::vector<ScriptNotice> &scriptNotices() const { return m_scriptNotices; }
	std::uint32_t scriptNoticeGeneration() const { return m_noticeGeneration; } ///< + 1 at each bind (a reader restarts its replay)

	// ---- part 2: the spell book (RW Player + 0x710 / + 0x718 / + 0x724) --------------------------------------------------------------
	// Player + 0x710: the id of the player's spell book object (0 until made or found, RW 0x6B0455 / 0x6B18F1 / 0x6AD128)
	std::uint32_t spellBookId() const { return m_spellBookId; }
	void setSpellBookId(std::uint32_t id) { m_spellBookId = id; }
	// Player + 0x718: the recharge time discount RESPECT_RECHARGE_TIME_DISCOUNT powers add to 1.0 (RW 0x6AAAD2; set by RW 0x6AAAEE, 0.0 at init)
	float rechargeDiscount() const { return m_rechargeDiscount; }
	void setRechargeDiscount(float d) { m_rechargeDiscount = d; }
	// Player + 0x724: the shared (SharedSyncedTimer) ready frames by special power id, in insertion order (RW 0x6AD180 inserts)
	unsigned getSharedReadyFrame(unsigned powerId, unsigned now);              // RW 0x6AD26F (absent: inserted at `now`)
	unsigned peekSharedReadyFrame(unsigned powerId, unsigned now) const;       // the same answer, nothing inserted (display reads only)
	void setSharedReadyFrame(unsigned powerId, unsigned frame);                // RW 0x6AD22B
	void resetOrStartSharedReadyFrame(unsigned powerId, unsigned reload, unsigned now); // RW 0x6AD1B0

	void crc(StateHasher &h) const;

private:
	void baseReset();                 // RW 0x782774
	void resetSciences();             // RW 0x6AED4B
	void rankGranted(const PlayerRankInfo &ri); // RW 0x6AE332
	void computeNeeded();             // RW 0x7826F6
	const std::string &side() const;

	const PlayerTemplate *m_template = nullptr;  // +0x04 (PlayerTemplate + 0x154)
	SpellGameMode m_mode;
	ScoreKeeper *m_score = nullptr;              // Player + 0x3DC
	float m_skillPoints = 0.0f;                  // +0x0C
	float m_skillPointsModifier = 1.0f;          // +0x10
	int m_rankLevel = 1;                         // +0x14
	int m_scalarBaseRank = 1;                    // +0x18
	int m_sciencePurchasePoints = 0;             // +0x1C
	int m_skillPointsNextRank = 0x7fffffff;      // +0x20
	int m_skillPointsThisRank = 0;               // +0x24
	int m_rankCap = 0;                           // +0x28
	ScienceVec m_sciences;                       // Player + 0x310
	std::function<void(ScienceType)> m_onScienceAdded;
	std::vector<ScriptNotice> m_scriptNotices;  // lane AUDIO-4: RW 0x759A4E / 0x75950A calls in order
	std::uint32_t m_noticeGeneration = 0;
	unsigned long long m_droppedNotifications = 0;
	std::uint32_t m_spellBookId = 0;                                   // Player + 0x710
	float m_rechargeDiscount = 0.0f;                                   // Player + 0x718
	std::vector<std::pair<unsigned, unsigned>> m_sharedReadyFrames;    // Player + 0x724
};

// Lane AUDIO-4 (QA-1 U20): one player's queue of ScriptEngine + 0x1A3A8 + 0xC * index (20 vectors, cleared by ScriptEngine::reset: donor Open-BFME-2
// ScriptEngine::reset 0x00209ABE). It is filled by replaying the player's script notices (PlayerScience::scriptNotices) in order before each read, which
// gives the queue retail's eager pushes and erases would have given at that moment: notices only come from the logic, reads only from conditions.
class AcquiredScienceQueue
{
public:
	// RW 0x759646 (ScriptEngine, the player index, the science, consume): true when the queue holds the science; with `consume` its first entry is
	// erased (RW 0x8FF64D). An index outside 0 .. 19 is false (the caller checks it: RW 0x759646 tests 0 <= index < 0x14)
	bool didAcquire(const PlayerScience &player, ScienceType st, bool consume);
	// ScriptEngine::reset: the queue empties at that moment: the notices the player made before it are not replayed, every later one is (review r1: the
	// position is taken now, not at the next read). `player` null (no player at that index yet): a later player's whole history is replayed
	void clear(const PlayerScience *player);
	void crc(StateHasher &h) const;
	const std::vector<ScienceType> &pending() const { return m_pending; }

private:
	void replay(const PlayerScience &player);
	static constexpr std::uint32_t kNoGeneration = 0xFFFFFFFFu; ///< no history captured: the next read replays the player's from its start

	std::uint32_t m_generation = kNoGeneration;
	size_t m_cursor = 0;
	std::vector<ScienceType> m_pending;
};
