// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// RankInfoStore. See GameLogic/RankInfo.h for the target facts. Lane SPELL-1.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/RankInfo.h"

#include "Common/INIException.h"

#include <cstddef>

thread_local RankInfoStore *TheRankInfoStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
void parseLabel(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73B192 (GameText not loaded: S-520)
}

#define RI_OFF(member) (int)offsetof(PlayerRankInfo, member)
#define RI_COL(c) (int)(offsetof(PlayerRankInfo, m_skillPointsNeeded) + sizeof(int) * (c))
// RW 0xBF89C8, in the binary's order
const FieldParse kRankFieldParse[] = {
	{ "RankName", parseLabel, nullptr, RI_OFF(m_rankName) },
	{ "SkillPointsNeededDefault", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_DEFAULT) },
	{ "SkillPointsNeededCampaign", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_CAMPAIGN) },
	{ "SkillPointsNeededMen", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_MEN) },
	{ "SkillPointsNeededElves", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_ELVES) },
	{ "SkillPointsNeededDwarves", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_DWARVES) },
	{ "SkillPointsNeededIsengard", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_ISENGARD) },
	{ "SkillPointsNeededMordor", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_MORDOR) },
	{ "SkillPointsNeededWild", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_WILD) },
	{ "SkillPointsNeededAngmar", INI::parseInt, nullptr, RI_COL(PlayerRankInfo::COLUMN_ANGMAR) },
	{ "SciencesGranted", ScienceParse::parseScienceVector, nullptr, RI_OFF(m_sciencesGranted) },
	{ "SciencePurchasePointsGranted", INI::parseUnsignedInt, nullptr, RI_OFF(m_sciencePurchasePointsGranted) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RI_COL
#undef RI_OFF

// RW 0xBF8840 .. 0xBF881C: the side names RW 0x5FFA44 tests, in its order, with their columns
const struct
{
	const char *side;
	int column;
} kSideColumns[] = {
	{ "Men", PlayerRankInfo::COLUMN_MEN },
	{ "Elves", PlayerRankInfo::COLUMN_ELVES },
	{ "Dwarves", PlayerRankInfo::COLUMN_DWARVES },
	{ "Isengard", PlayerRankInfo::COLUMN_ISENGARD },
	{ "Mordor", PlayerRankInfo::COLUMN_MORDOR },
	{ "Wild", PlayerRankInfo::COLUMN_WILD },
	{ "Angmar", PlayerRankInfo::COLUMN_ANGMAR },
};
} // namespace

// RW 0x5FFA44
int PlayerRankInfo::getSkillPointsNeeded(const std::string &side, bool campaign) const
{
	if (campaign)
	{
		const int v = m_skillPointsNeeded[COLUMN_CAMPAIGN];
		return v != -1 ? v : m_skillPointsNeeded[COLUMN_DEFAULT];
	}
	for (const auto &sc : kSideColumns)
	{
		if (side == sc.side && m_skillPointsNeeded[sc.column] != -1)
		{
			return m_skillPointsNeeded[sc.column];
		}
	}
	return m_skillPointsNeeded[COLUMN_DEFAULT];
}

RankInfoStore::~RankInfoStore()
{
	if (TheRankInfoStore == this)
	{
		TheRankInfoStore = nullptr;
	}
}

// RW 0x5FFCAA
void RankInfoStore::parseRankDefinition(INI *ini)
{
	const int rank = ini->scanInt(ini->getNextToken()); // RW 0x42E9D7
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		if (rank < 1 || rank > getRankLevelCount() || !m_rankInfos[(size_t)rank - 1])
		{
			throw INIException(3, "Rank not found in map.ini");
		}
		m_owned.push_back(std::unique_ptr<PlayerRankInfo>(new PlayerRankInfo));
		PlayerRankInfo *info = m_owned.back().get();
		PlayerRankInfo *final = m_rankInfos[(size_t)rank - 1]->getFinalOverride();
		*info = *final; // RW 0x5FFC41
		info->m_nextOverride = nullptr;
		final->m_nextOverride = info;
		info->m_isOverride = true;
		ini->initFromINI(info, kRankFieldParse);
		return;
	}
	if (rank != getRankLevelCount() + 1)
	{
		throw INIException(3, "Ranks must increase monotonically");
	}
	m_owned.push_back(std::unique_ptr<PlayerRankInfo>(new PlayerRankInfo));
	PlayerRankInfo *info = m_owned.back().get();
	ini->initFromINI(info, kRankFieldParse);
	m_rankInfos.push_back(info);
}

void RankInfoStore::parseRankDefinitionGlobal(INI *ini)
{
	if (TheRankInfoStore) // RW 0x5FFCB2
	{
		TheRankInfoStore->parseRankDefinition(ini);
	}
}

void RankInfoStore::resetOverrides()
{
	for (PlayerRankInfo *ri : m_rankInfos)
	{
		ri->m_nextOverride = nullptr;
	}
	std::vector<std::unique_ptr<PlayerRankInfo>> owned;
	for (auto &p : m_owned)
	{
		if (!p->m_isOverride)
		{
			owned.push_back(std::move(p));
		}
	}
	m_owned.swap(owned);
}

// RW 0x5FF99F
const PlayerRankInfo *RankInfoStore::getRankInfo(int level) const
{
	if (level < 1 || level > getRankLevelCount())
	{
		return nullptr;
	}
	const PlayerRankInfo *ri = m_rankInfos[(size_t)level - 1];
	return ri ? ri->getFinalOverride() : nullptr;
}
