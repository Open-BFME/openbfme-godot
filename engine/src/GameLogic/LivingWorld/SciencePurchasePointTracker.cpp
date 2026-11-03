// OpenBFME. GPL-3.0.
//
// SciencePurchasePointTracker. See SciencePurchasePointTracker.h for the target facts. Lane WOTR-2.

#include "GameLogic/LivingWorld/SciencePurchasePointTracker.h"

#include "GameLogic/SimMath.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace LivingWorld
{

namespace
{
// RW adds 32-bit registers (add): two's complement wrap-around, as Common/PlayerScience.cpp does
std::int32_t wrapAdd(std::int32_t a, std::uint32_t b)
{
	const std::uint32_t u = (std::uint32_t)a + b;
	std::int32_t r;
	std::memcpy(&r, &u, sizeof r);
	return r;
}
}

SciencePurchasePointTracker::SciencePurchasePointTracker(const std::vector<RankRow> &ranks)
	: m_ranks(&ranks)
{
	reset(); // RW 0x782A0C: the reset runs before the caller sets the template
}

void SciencePurchasePointTracker::setTemplate(int maxLevelSP, int intrinsicSciencePurchasePoints)
{
	m_haveTemplate = true;
	m_maxLevelSP = maxLevelSP;
	m_intrinsicPurchasePoints = intrinsicSciencePurchasePoints;
}

const SciencePurchasePointTracker::RankRow *SciencePurchasePointTracker::rank(int level) const
{
	if (level < 1 || level > (int)m_ranks->size() || !(*m_ranks)[(size_t)level - 1].present)
	{
		return nullptr;
	}
	return &(*m_ranks)[(size_t)level - 1];
}

// RW 0x7826F6
void SciencePurchasePointTracker::computeNeeded()
{
	const RankRow *next = rank(m_rankLevel + 1);
	m_skillPointsNextRank = next ? next->skillPointsNeeded : 0x7fffffff;
	const RankRow *cur = rank(m_rankLevel);
	m_skillPointsThisRank = cur ? cur->skillPointsNeeded : 0;
}

// RW 0x782774
void SciencePurchasePointTracker::reset()
{
	m_rankCap = 0;
	m_rankLevel = 1;
	m_scalarBaseRank = 1;
	m_skillPoints = 0.0f;
	m_sciencePurchasePoints = m_haveTemplate ? m_intrinsicPurchasePoints : 0;
	const RankRow *r1 = rank(1);
	m_sciencePurchasePoints = wrapAdd(m_sciencePurchasePoints, r1 ? r1->sciencePurchasePointsGranted : 0u); // a plain add, no clamp
	computeNeeded();
}

// RW 0x782764
void SciencePurchasePointTracker::addSciencePurchasePoints(int delta)
{
	std::uint32_t bits;
	std::memcpy(&bits, &delta, sizeof bits);
	m_sciencePurchasePoints = wrapAdd(m_sciencePurchasePoints, bits);
	if (m_sciencePurchasePoints < 0)
	{
		m_sciencePurchasePoints = 0;
	}
}

// RW 0x7827C8 (slot 5 false)
int SciencePurchasePointTracker::getMaxRankLevel() const
{
	const int count = (int)m_ranks->size(); // RW 0x5EA78B
	if (!m_haveTemplate)
	{
		return count;
	}
	return m_maxLevelSP < count ? m_maxLevelSP : count; // single player (header INFERENCE)
}

// RW 0x782865
bool SciencePurchasePointTracker::setRankLevel(int level)
{
	const int count = (int)m_ranks->size();
	if (level < 1)
	{
		level = 1;
	}
	else if (level > count)
	{
		level = count;
	}
	const int maxLevel = getMaxRankLevel();
	if (level > maxLevel)
	{
		level = maxLevel;
	}
	if (level == m_rankLevel)
	{
		return false;
	}
	if (level < m_rankLevel)
	{
		reset(); // slot 3
	}
	for (int r = m_rankLevel + 1; r <= level; ++r)
	{
		const RankRow *ri = rank(r);
		if (!ri)
		{
			continue;
		}
		m_sciencePurchasePoints = wrapAdd(m_sciencePurchasePoints, ri->sciencePurchasePointsGranted);
		if (m_sciencePurchasePoints < 0)
		{
			m_sciencePurchasePoints = 0;
		}
		if (SimMath::floorToInt(m_skillPoints) < ri->skillPointsNeeded) // RW 0x7828F6 .. 0x782915: floor (CRT), fistp
		{
			m_skillPoints = SimMath::sseFromInt32(ri->skillPointsNeeded); // cvtsi2ss
		}
		// slot 4 (RW 0x9F3A3C): nothing
	}
	m_rankLevel = level;
	computeNeeded();
	return true;
}

// RW 0x782AA4
bool SciencePurchasePointTracker::addSkillPoints(float points, bool applyScalar)
{
	points = SimMath::sseMul(m_skillPointsModifier, points);
	if (m_rankCap > 0 && m_rankLevel >= m_rankCap)
	{
		return false;
	}
	(void)applyScalar; // the PlayerSkillPointsScalarTable is skipped while GameLogic + 0x114 == 3 (header INFERENCE)
	if (points == 0.0f)
	{
		return false;
	}
	const RankRow *top = rank(getMaxRankLevel());
	if (!top)
	{
		throw std::runtime_error("SciencePurchasePointTracker: the max rank " + std::to_string(getMaxRankLevel()) +
			" has no Rank block (RW 0x782B47 reads it)");
	}
	const float sum = SimMath::sseAdd(m_skillPoints, points);
	const float cap = SimMath::sseFromInt32(top->skillPointsNeeded);
	m_skillPoints = sum > cap ? cap : sum; // RW 0x782B5C comiss / ja
	const int whole = SimMath::floorToInt(m_skillPoints);
	bool gained = false;
	while (whole >= m_skillPointsNextRank)
	{
		const bool ok = setRankLevel(m_rankLevel + 1); // slot 1
		gained = gained || ok;
		if (!ok)
		{
			break;
		}
	}
	return gained;
}

} // namespace LivingWorld
