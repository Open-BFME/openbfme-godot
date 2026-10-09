// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PlayerScience. See Common/PlayerScience.h for the target facts. Lane SPELL-1.

#include "Common/PlayerScience.h"

#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/StateHash.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
// RW adds and negates 32-bit registers (add / neg): two's complement wrap-around. The sum is computed on uint32_t and its bits moved to int32_t
// (memcpy), so an overflow is the retail result and never C++ signed overflow (Sol review r1).
std::int32_t wrapAdd(std::int32_t a, std::int32_t b)
{
	const std::uint32_t u = (std::uint32_t)a + (std::uint32_t)b;
	std::int32_t r;
	std::memcpy(&r, &u, sizeof r);
	return r;
}
std::int32_t wrapAdd(std::int32_t a, std::uint32_t b) // an unsigned operand (SciencePurchasePointsGranted, parseUnsignedInt) is added as its bits
{
	std::int32_t bs;
	std::memcpy(&bs, &b, sizeof bs);
	return wrapAdd(a, bs);
}
std::int32_t wrapNeg(std::int32_t a)
{
	const std::uint32_t u = 0u - (std::uint32_t)a;
	std::int32_t r;
	std::memcpy(&r, &u, sizeof r);
	return r;
}

const std::string kNoSide; // RW 0xDC62B8: the empty record used without a template
}

void PlayerScience::bind(const PlayerTemplate *pt, const SpellGameMode &mode, ScoreKeeper *score)
{
	m_template = pt;
	m_mode = mode;
	m_score = score;
	m_skillPointsModifier = 1.0f; // RW 0x782A28 (constructor)
	m_sciences.clear();
	m_scriptNotices.clear(); // lane AUDIO-4: a new game's player starts a new notice history
	++m_noticeGeneration;
	m_spellBookId = 0;          // RW 0x6B0455
	m_rechargeDiscount = 0.0f;  // RW 0x6B045B
	m_sharedReadyFrames.clear(); // (INFERENCE: a new game's player has no shared timer)
}

const std::string &PlayerScience::side() const
{
	return m_template ? m_template->m_side : kNoSide; // PlayerTemplate + 0x154 is the copy of Side made after the parse (RW 0x5FDFC4)
}

// RW 0x7826F6
void PlayerScience::computeNeeded()
{
	const PlayerRankInfo *next = TheRankInfoStore ? TheRankInfoStore->getRankInfo(m_rankLevel + 1) : nullptr;
	m_skillPointsNextRank = next ? next->getSkillPointsNeeded(side(), m_mode.campaign) : 0x7fffffff;
	const PlayerRankInfo *cur = TheRankInfoStore ? TheRankInfoStore->getRankInfo(m_rankLevel) : nullptr;
	m_skillPointsThisRank = cur ? cur->getSkillPointsNeeded(side(), m_mode.campaign) : 0;
}

// RW 0x782774
void PlayerScience::baseReset()
{
	m_rankCap = 0;
	m_rankLevel = 1;
	m_scalarBaseRank = 1;
	m_skillPoints = 0.0f;
	m_sciencePurchasePoints = m_template ? m_template->m_intrinsicSciencePurchasePoints : 0;
	const PlayerRankInfo *r1 = TheRankInfoStore ? TheRankInfoStore->getRankInfo(1) : nullptr;
	m_sciencePurchasePoints = wrapAdd(m_sciencePurchasePoints, r1 ? r1->m_sciencePurchasePointsGranted : 0u); // RW 0x7827BD: a plain add, no clamp
	computeNeeded();
}

// Player vslot 3 (RW 0x6AEE11)
void PlayerScience::reset()
{
	baseReset();
	resetSciences();
}

// RW 0x6AED4B
void PlayerScience::resetSciences()
{
	for (ScienceType st : m_sciences) // RW 0x6AD142: each owned science is reported removed (RW 0x75950A), then the list cleared
	{
		m_scriptNotices.push_back(ScriptNotice{ st, false });
	}
	m_sciences.clear();
	if (m_template && TheScienceStore)
	{
		// the template keeps the science names (PlayerTemplate.cpp, S-145): they resolve against the store here, an unknown name is an error
		const std::vector<std::string> &names = m_mode.skirmishOrMultiplayer ? m_template->m_intrinsicSciencesMP : m_template->m_intrinsicSciences;
		for (const std::string &n : names)
		{
			const ScienceType st = TheScienceStore->getScienceFromInternalName(n);
			if (st == SCIENCE_INVALID)
			{
				throw std::runtime_error("PlayerScience: intrinsic science " + n + " of " + m_template->getName() + " is not a Science");
			}
			m_sciences.push_back(st); // RW 0x6AED81: a vector copy (duplicates kept as listed)
		}
	}
	for (int r = 1; r <= m_rankLevel; ++r) // RW 0x6AED86 .. 0x6AEDBA
	{
		const PlayerRankInfo *ri = TheRankInfoStore ? TheRankInfoStore->getRankInfo(r) : nullptr;
		if (ri)
		{
			for (ScienceType st : ri->m_sciencesGranted)
			{
				addScience(st);
			}
		}
	}
	for (ScienceType st : m_sciences) // RW 0x6AEDBF .. 0x6AEDD9: every science of the new list is notified (RW 0x759A4E) once more
	{
		m_scriptNotices.push_back(ScriptNotice{ st, true });
	}
}

// RW 0x6AE332
void PlayerScience::rankGranted(const PlayerRankInfo &ri)
{
	if (m_score)
	{
		m_score->addSciencePurchasePointsEarned((std::int32_t)ri.m_sciencePurchasePointsGranted);
	}
	for (ScienceType st : ri.m_sciencesGranted)
	{
		addScience(st);
	}
}

// RW 0x6AA980 -> 0x782764
void PlayerScience::addSciencePurchasePoints(int delta)
{
	if (delta > 0 && m_score)
	{
		m_score->addSciencePurchasePointsEarned(delta);
	}
	m_sciencePurchasePoints = wrapAdd(m_sciencePurchasePoints, delta);
	if (m_sciencePurchasePoints < 0)
	{
		m_sciencePurchasePoints = 0;
	}
}

// RW 0x7827C8 (vslot 5 is false in a skirmish: S-525)
int PlayerScience::getMaxRankLevel() const
{
	const int count = TheRankInfoStore ? TheRankInfoStore->getRankLevelCount() : 0;
	if (!m_template)
	{
		return count;
	}
	const int cap = m_mode.skirmishOrMultiplayer ? m_template->m_maxLevelMP : m_template->m_maxLevelSP;
	return cap < count ? cap : count;
}

// RW 0x782865
bool PlayerScience::setRankLevel(int level)
{
	const int count = TheRankInfoStore ? TheRankInfoStore->getRankLevelCount() : 0;
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
		reset(); // vslot 3
	}
	for (int r = m_rankLevel + 1; r <= level; ++r)
	{
		const PlayerRankInfo *ri = TheRankInfoStore ? TheRankInfoStore->getRankInfo(r) : nullptr;
		if (!ri)
		{
			continue;
		}
		const int needed = ri->getSkillPointsNeeded(side(), m_mode.campaign);
		m_sciencePurchasePoints = wrapAdd(m_sciencePurchasePoints, ri->m_sciencePurchasePointsGranted);
		if (m_sciencePurchasePoints < 0)
		{
			m_sciencePurchasePoints = 0;
		}
		if (SimMath::floorToInt(m_skillPoints) < needed) // RW 0x7828F6 .. 0x782915: floor (CRT), fistp
		{
			m_skillPoints = SimMath::sseFromInt32(needed); // cvtsi2ss
		}
		rankGranted(*ri); // vslot 4
	}
	m_rankLevel = level;
	computeNeeded();
	return true;
}

// RW 0x782AA4
bool PlayerScience::addSkillPoints(float points, bool applyScalar)
{
	points = SimMath::sseMul(m_skillPointsModifier, points);
	if (m_rankCap > 0 && m_rankLevel >= m_rankCap)
	{
		return false;
	}
	(void)applyScalar; // the PlayerSkillPointsScalarTable is skipped while GameLogic + 0x114 == 3, as in every skirmish (S-525)
	if (points == 0.0f)
	{
		return false;
	}
	const int maxLevel = getMaxRankLevel();
	const PlayerRankInfo *top = TheRankInfoStore ? TheRankInfoStore->getRankInfo(maxLevel) : nullptr;
	const int capPoints = top ? top->getSkillPointsNeeded(side(), m_mode.campaign) : 0; // RW 0x782B47 (a null rank info would crash in RW)
	const float sum = SimMath::sseAdd(m_skillPoints, points);
	const float cap = SimMath::sseFromInt32(capPoints);
	m_skillPoints = sum > cap ? cap : sum; // RW 0x782B5C comiss / ja
	const int whole = SimMath::floorToInt(m_skillPoints);
	bool gained = false;
	while (whole >= m_skillPointsNextRank)
	{
		const bool ok = setRankLevel(m_rankLevel + 1); // vslot 1
		gained = gained || ok;
		if (!ok)
		{
			break;
		}
	}
	return gained;
}

// RW 0x6AC207
bool PlayerScience::hasScience(ScienceType st) const
{
	return std::find(m_sciences.begin(), m_sciences.end(), st) != m_sciences.end();
}

// RW 0x6AE186
bool PlayerScience::addScience(ScienceType st)
{
	if (hasScience(st))
	{
		return false;
	}
	m_sciences.push_back(st);
	m_scriptNotices.push_back(ScriptNotice{ st, true }); // the script engine's notice (RW 0x759A4E; lane AUDIO-4)
	if (m_onScienceAdded)
	{
		m_onScienceAdded(st);
	}
	else
	{
		++m_droppedNotifications;
	}
	return true;
}

// RW 0x6AE3A7
bool PlayerScience::grantScience(ScienceType st)
{
	if (!TheScienceStore || !TheScienceStore->isScienceGrantable(st))
	{
		return false;
	}
	return addScience(st);
}

// RW 0x6AC8BC
bool PlayerScience::isCapableOfPurchasingScience(ScienceType st) const
{
	if (st == SCIENCE_INVALID || hasScience(st) || !TheScienceStore)
	{
		return false;
	}
	if (!TheScienceStore->playerHasPrereqsForScience(*this, st))
	{
		return false;
	}
	const int cost = TheScienceStore->getSciencePurchaseCost(st, m_mode.skirmishOrMultiplayer);
	return cost != 0 && cost <= m_sciencePurchasePoints;
}

// RW 0x6AE36F
bool PlayerScience::attemptToPurchaseScience(ScienceType st)
{
	if (!isCapableOfPurchasingScience(st))
	{
		return false;
	}
	addSciencePurchasePoints(wrapNeg(TheScienceStore->getSciencePurchaseCost(st, m_mode.skirmishOrMultiplayer))); // vslot 2 (RW 0x6AE392 neg)
	addScience(st);
	return true;
}

// RW 0x6AD26F
unsigned PlayerScience::getSharedReadyFrame(unsigned powerId, unsigned now)
{
	for (const auto &e : m_sharedReadyFrames)
	{
		if (e.first == powerId)
		{
			return e.second;
		}
	}
	m_sharedReadyFrames.push_back({ powerId, now }); // RW 0x6AD2A4 -> 0x6AD180
	return now;
}

unsigned PlayerScience::peekSharedReadyFrame(unsigned powerId, unsigned now) const
{
	for (const auto &e : m_sharedReadyFrames)
	{
		if (e.first == powerId)
		{
			return e.second;
		}
	}
	return now;
}

// RW 0x6AD22B
void PlayerScience::setSharedReadyFrame(unsigned powerId, unsigned frame)
{
	for (auto &e : m_sharedReadyFrames)
	{
		if (e.first == powerId)
		{
			e.second = frame;
			return;
		}
	}
	m_sharedReadyFrames.push_back({ powerId, frame });
}

// RW 0x6AD1B0: absent: inserted at now; present: now + (scaled < reload ? scaled + reload : 0) with scaled = _ftol2((x87) reload * discount)
void PlayerScience::resetOrStartSharedReadyFrame(unsigned powerId, unsigned reload, unsigned now)
{
	for (auto &e : m_sharedReadyFrames)
	{
		if (e.first == powerId)
		{
			const unsigned scaled = SimMath::ftol2Low32((double)SimMath::pc24MulD((double)reload, m_rechargeDiscount)); // fild ; fmul dword ; _ftol2
			e.second = (scaled < reload ? scaled + reload : 0u) + now; // RW 0x6AD219 .. 0x6AD222 (unsigned compare)
			return;
		}
	}
	m_sharedReadyFrames.push_back({ powerId, now });
}

void PlayerScience::crc(StateHasher &h) const
{
	h.addU32(m_spellBookId);
	h.addFloat(m_rechargeDiscount);
	h.addU32((std::uint32_t)m_sharedReadyFrames.size());
	for (const auto &e : m_sharedReadyFrames)
	{
		h.addU32(e.first);
		h.addU32(e.second);
	}
	h.addBool(m_mode.skirmishOrMultiplayer);
	h.addBool(m_mode.campaign);
	h.addFloat(m_skillPoints);
	h.addFloat(m_skillPointsModifier);
	h.addI32(m_rankLevel);
	h.addI32(m_scalarBaseRank);
	h.addI32(m_sciencePurchasePoints);
	h.addI32(m_skillPointsNextRank);
	h.addI32(m_skillPointsThisRank);
	h.addI32(m_rankCap);
	h.addU32((std::uint32_t)m_sciences.size());
	for (ScienceType st : m_sciences) // a vector in acquisition order; the keys are the load's name keys (identical for an identical install)
	{
		h.addI32(st);
	}
	// lane AUDIO-4 (review r1): the script notices the engine's queues replay, in order, with their generation
	h.addU32(m_noticeGeneration);
	h.addU32((std::uint32_t)m_scriptNotices.size());
	for (const ScriptNotice &n : m_scriptNotices)
	{
		h.addI32(n.science);
		h.addBool(n.acquired);
	}
}

// ---- lane AUDIO-4: the script engine's acquired-science queue --------------------------------------------------------------------------------------

void AcquiredScienceQueue::replay(const PlayerScience &player)
{
	const std::vector<PlayerScience::ScriptNotice> &log = player.scriptNotices();
	if (player.scriptNoticeGeneration() != m_generation)
	{
		m_generation = player.scriptNoticeGeneration(); // another game's player (or none captured): its whole history is new
		m_cursor = 0;
		m_pending.clear();
	}
	for (; m_cursor < log.size(); ++m_cursor)
	{
		const PlayerScience::ScriptNotice &n = log[m_cursor];
		if (n.acquired)
		{
			m_pending.push_back(n.science); // RW 0x759A4E -> 0x5487BB (push_back)
			continue;
		}
		auto it = std::find(m_pending.begin(), m_pending.end(), n.science); // RW 0x75950A: the first equal entry, erased (RW 0x8FF64D)
		if (it != m_pending.end())
		{
			m_pending.erase(it);
		}
	}
}

bool AcquiredScienceQueue::didAcquire(const PlayerScience &player, ScienceType st, bool consume)
{
	replay(player);
	auto it = std::find(m_pending.begin(), m_pending.end(), st); // RW 0x759646: the first equal entry
	if (it == m_pending.end())
	{
		return false;
	}
	if (consume)
	{
		m_pending.erase(it);
	}
	return true;
}

void AcquiredScienceQueue::clear(const PlayerScience *player)
{
	m_pending.clear();
	if (player)
	{
		m_generation = player->scriptNoticeGeneration(); // the history so far is gone with the vector (retail's clear), what follows is kept
		m_cursor = player->scriptNotices().size();
	}
	else
	{
		m_generation = kNoGeneration;
		m_cursor = 0;
	}
}

void AcquiredScienceQueue::crc(StateHasher &h) const
{
	h.addU32(m_generation);
	h.addU32((std::uint32_t)m_cursor);
	h.addU32((std::uint32_t)m_pending.size());
	for (ScienceType st : m_pending)
	{
		h.addI32(st);
	}
}
