// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PlayerRankInfo / RankInfoStore (ZH Include/GameLogic/RankInfo.h, Source/GameLogic/System/RankInfo.cpp): the `Rank <n>` INI block, the player
// ranks the skill points climb. Lane SPELL-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * TheRankInfoStore is RW 0xDE3B2C; its vector of PlayerRankInfo* is store + 0xC / + 0x10 (rank n is element n - 1).
//   * PlayerRankInfo is 0x48 bytes (constructor RW 0x5FF9F5): +0x04 next override, +0x08 isOverride, +0x0C flag (-1), +0x10 RankName, +0x14
//     SkillPointsNeededDefault (0), +0x18 SkillPointsNeededCampaign, +0x1C ..Men, +0x20 ..Elves, +0x24 ..Dwarves, +0x28 ..Isengard, +0x2C
//     ..Mordor, +0x30 ..Wild, +0x34 ..Angmar (all -1 = "not set"), +0x38 SciencePurchasePointsGranted (0), +0x3C SciencesGranted (vector).
//     Field table RW 0xBF89C8 (order below; the counts are parseInt RW 0x42EC5E, the points parseUnsignedInt RW 0x42ECB2, the sciences
//     parseScienceVector RW 0x73B4A0, RankName the label parser RW 0x73B192).
//   * the block parser RW 0x5FFCAA (nothing when the store is null): rank = scanInt(getNextToken). Load type 2: rank must be 1 .. count and
//     its entry non-null, else INIException(3, "Rank not found in map.ini"); a new PlayerRankInfo copies the found one's final override (RW 0x5FFC41),
//     the final override links it and it is marked an override, then it is parsed. Any other load type: rank must be count + 1, else
//     INIException(3, "Ranks must increase monotonically"); a new PlayerRankInfo is parsed and appended.
//   * getRankInfo (RW 0x5FF99F): rank 1 .. count, the final override; null otherwise.
//   * getSkillPointsNeeded (RW 0x5FFA44, argument: the player's side name, Player + 8 + 4 -> PlayerTemplate + 0x154, a copy of the Side the
//     PlayerTemplate parser makes at RW 0x5FDFC4): in a campaign game (RW 0x5FF924: 0x626355 || 0x6253FD) the Campaign count when it is
//     not -1, else the Default count. Otherwise the side is compared (AsciiString compare, case-sensitive: RW 0x406585) with "Men", "Elves",
//     "Dwarves", "Isengard", "Mordor", "Wild", "Angmar" (RW 0xBF8840 ...) and the matching column is used when it is not -1; every other
//     case uses the Default count. These seven names are the BINARY's (not retail data): a mod Side named otherwise uses the Default count.
// DONOR: BFME1 GameLogic/System/RankInfo.cpp (the parse structure); ZH RankInfo.cpp.
// Named PlayerRankInfo here (ZH: RankInfo): HORDE-1 owns the global RankInfo, the horde formation rank (Module/HordeContain.h); one name for both broke the ODR.

#pragma once

#include "Common/INI.h"
#include "Common/Science.h"

#include <memory>
#include <string>
#include <vector>

class PlayerRankInfo
{
public:
	enum Column
	{
		COLUMN_DEFAULT = 0,
		COLUMN_CAMPAIGN,
		COLUMN_MEN,
		COLUMN_ELVES,
		COLUMN_DWARVES,
		COLUMN_ISENGARD,
		COLUMN_MORDOR,
		COLUMN_WILD,
		COLUMN_ANGMAR,
		COLUMN_COUNT
	};

	PlayerRankInfo *getFinalOverride() { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }
	const PlayerRankInfo *getFinalOverride() const { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }

	// RW 0x5FFA44 (see the file comment)
	int getSkillPointsNeeded(const std::string &side, bool campaign) const;

	PlayerRankInfo *m_nextOverride = nullptr;                                         // +0x04
	bool m_isOverride = false;                                                  // +0x08
	std::string m_rankName;                                                     // +0x10 (label, S-520)
	int m_skillPointsNeeded[COLUMN_COUNT] = { 0, -1, -1, -1, -1, -1, -1, -1, -1 }; // +0x14 .. +0x34
	unsigned m_sciencePurchasePointsGranted = 0;                                // +0x38
	ScienceVec m_sciencesGranted;                                               // +0x3C
};

class RankInfoStore
{
public:
	RankInfoStore() = default;
	~RankInfoStore();
	RankInfoStore(const RankInfoStore &) = delete;
	RankInfoStore &operator=(const RankInfoStore &) = delete;

	void parseRankDefinition(INI *ini);                  // RW 0x5FFCAA
	static void parseRankDefinitionGlobal(INI *ini);    // nothing when TheRankInfoStore is null
	void resetOverrides();                               // ZH RankInfoStore::reset

	int getRankLevelCount() const { return (int)m_rankInfos.size(); } // RW 0x5EA78B
	const PlayerRankInfo *getRankInfo(int level) const;       // RW 0x5FF99F (1-based)

private:
	std::vector<std::unique_ptr<PlayerRankInfo>> m_owned;
	std::vector<PlayerRankInfo *> m_rankInfos;
};

extern thread_local RankInfoStore *TheRankInfoStore;
