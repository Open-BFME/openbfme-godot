// OpenBFME. GPL-3.0.
//
// SciencePurchasePointTracker: RotWK's player rank object (WorldBuilder's trace name SciencePurchasePointTracker::setRankLevel; base vtable
// RW 0xC13E98) as the War of the Ring autoresolve uses it.  Lane WOTR-2.  Common/PlayerScience.h ports the Player's subclass (Player + 8, vtable
// RW 0xC14198: the score keeper, the sciences, the rank-up EVA); the autoresolve builds the base class (RW 0x6B2FD7 sets the vtable RW 0xC14264,
// whose slots 1 .. 5 are the base's) and copies it (RW 0x8FAD90: the base vtable), so none of the Player's additions run here.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the field layout and the base slots are read in PlayerScience.h, the same addresses):
//   * the constructor (RW 0x782A0C): template (+0x04) null, skill points 0.0, modifier 1.0, then reset (RW 0x782774) while the template is still
//     null: purchase points = rank 1's SciencePurchasePointsGranted (no IntrinsicSciencePurchasePoints), rank 1, scalar base rank 1, no cap.
//   * the base slots: 1 setRankLevel RW 0x782865, 2 addSciencePurchasePoints RW 0x782764 (points += delta, 0 when negative; no score keeper),
//     3 reset RW 0x782774, 4 rankGranted RW 0x9F3A3C (`ret 4`: nothing), 5 RW 0x9188EB (`xor al, al`: false).
//   * the max rank (RW 0x7827C8 with slot 5 false): no template: the rank count; else min(rank count, MaxLevelMP) when GameLogic + 0x110 is 2 or
//     the game is multiplayer (RW 0x441B7C: + 0x110 is 1 or 5), min(rank count, MaxLevelSP) otherwise.
//   * the needed counts (RW 0x7826F6) read getSkillPointsNeeded (RW 0x5FFA44), whose campaign test (RW 0x5FF924) is true in a War of the Ring game:
//     RW 0x6253FD needs GameLogic + 0x114 == 3 and the Living World logic's + 0xB4, which its start (RW 0x6B8FBF, store at RW 0x6B9033) sets and
//     its reset (RW 0x6BA50E) clears. The campaign path takes SkillPointsNeededCampaign when it is not -1, else SkillPointsNeededDefault, so the
//     side is never compared: the caller passes each rank's campaign count (RankRow).
//   * addSkillPoints (RW 0x782AA4) skips the PlayerSkillPointsScalarTable while GameLogic + 0x114 == 3, the value RW 0x6253FD requires above.
// INFERENCE (stop S-1328): GameLogic + 0x114 is 3 during the strategic game (the constructor RW 0x6301D3 and the reset RW 0x779F20 store 3; the
// new-game message stores its argument 1 at RW 0x779CB4 / 0x779D23 only for the game info RW 0xDE892C, read in S-525) and + 0x110 is not 1, 2 or
// 5 (a single-player game: MaxLevelSP).

#pragma once

#include <vector>

namespace LivingWorld
{

class SciencePurchasePointTracker
{
public:
	// one Rank block of TheRankInfoStore (rank n is element n - 1)
	struct RankRow
	{
		bool present = false;                 // RW 0x5FF99F: the rank's entry is not null
		unsigned sciencePurchasePointsGranted = 0; // + 0x38 (parseUnsignedInt)
		int skillPointsNeeded = 0;            // RW 0x5FFA44's campaign path: SkillPointsNeededCampaign, else SkillPointsNeededDefault
	};

	// the constructor (RW 0x782A0C): `ranks` must outlive the tracker
	explicit SciencePurchasePointTracker(const std::vector<RankRow> &ranks);

	// RW 0x6BD777 .. 0x6BD784: the template's record (PlayerTemplate + 0x154) found by the Living World player template's Faction; the template's
	// MaxLevelSP is the only part these calls read (the side is not compared in the campaign path, IntrinsicSciencePurchasePoints only by reset)
	void setTemplate(int maxLevelSP, int intrinsicSciencePurchasePoints);

	void reset();                                        // slot 3 (RW 0x782774)
	bool setRankLevel(int level);                        // slot 1 (RW 0x782865)
	void addSciencePurchasePoints(int delta);            // slot 2 (RW 0x782764)
	bool addSkillPoints(float points, bool applyScalar); // RW 0x782AA4
	int getMaxRankLevel() const;                         // RW 0x7827C8
	void setScalarBaseRank(int rank) { m_scalarBaseRank = rank; } // RW 0x71C311 (+ 0x18)

	int getRankLevel() const { return m_rankLevel; }
	float getSkillPoints() const { return m_skillPoints; }
	int getSciencePurchasePoints() const { return m_sciencePurchasePoints; }
	int getScalarBaseRank() const { return m_scalarBaseRank; }


private:
	const RankRow *rank(int level) const; // RW 0x5FF99F: 1 .. count, null for an absent entry
	void computeNeeded();                 // RW 0x7826F6

	const std::vector<RankRow> *m_ranks;
	bool m_haveTemplate = false;           // + 0x04
	int m_maxLevelSP = 0;
	int m_intrinsicPurchasePoints = 0;
	float m_skillPoints = 0.0f;            // + 0x0C
	float m_skillPointsModifier = 1.0f;    // + 0x10
	int m_rankLevel = 1;                   // + 0x14
	int m_scalarBaseRank = 1;              // + 0x18
	int m_sciencePurchasePoints = 0;       // + 0x1C
	int m_skillPointsNextRank = 0x7fffffff; // + 0x20
	int m_skillPointsThisRank = 0;         // + 0x24
	int m_rankCap = 0;                     // + 0x28
};

} // namespace LivingWorld
