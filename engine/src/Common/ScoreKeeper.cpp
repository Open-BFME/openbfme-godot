// OpenBFME. GPL-3.0.
// See Common/ScoreKeeper.h for the target facts, the donor and what is inference (lane END-1).

#include "Common/ScoreKeeper.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

namespace
{
// the binary's bit numbers (RW 0x79EB10 sets them in the static masks; RW 0x79F321 pushes 0x4C)
constexpr unsigned KINDOF_STRUCTURE = 7;
constexpr unsigned KINDOF_COMMANDCENTER = 17;
constexpr unsigned KINDOF_SCORE = 39;
constexpr unsigned KINDOF_SCORE_CREATE = 40;
constexpr unsigned KINDOF_SCORE_DESTROY = 41;
constexpr unsigned KINDOF_HERO = 90;      // template + 0x113 & 4 (RW 0x79F29C)
constexpr unsigned KINDOF_SUMMONED = 179; // template + 0x11E & 8 (RW 0x79F2A5) and the must-be-clear bit of RW 0x79F25B
constexpr unsigned STATUS_DO_NOT_SCORE = 76;
// lane END-2: the bits the score screen's queries test (TheKindOfNames, RW 0xDA0E68)
constexpr unsigned KINDOF_MONSTER = 10;
constexpr unsigned KINDOF_MACHINE = 11;
constexpr unsigned KINDOF_MINE = 55;
constexpr unsigned KINDOF_HORDE = 109;        // template + 0x115 & 0x20 (RW 0x79F22D)
constexpr unsigned KINDOF_DEPLOYED_MINE = 172; // template + 0x11D & 0x10 (RW 0x79E31D)
constexpr unsigned KINDOF_SHIP = 191;
constexpr unsigned KINDOF_HORDE_MONSTER = 221; // template + 0x123 & 0x20 (RW 0x79E326)

const std::map<std::string, int> kEmptyMap;

const std::string &templateKey(const Object &obj)
{
	return obj.getTemplate()->getName();
}

// RW 0x7640C1 with the GameData filter (+ 0x1168); an unset filter (RW 0x762977 "not valid") lets nothing through
bool objectScores(GameLogic &logic, const Object &obj)
{
	const ObjectFilter *f = logic.settings().objectsThatScore.get();
	return f && ObjectFilterMatch::isValid(f) && ObjectFilterMatch::allows(logic, *f, obj, nullptr);
}

// RW 0x6AAC66: the player's template is a playable side
bool playableSide(const Player *p)
{
	return p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide;
}
} // namespace

void ScoreKeeper::reset(int playerIndex)
{
	m_moneyEarned = m_moneySpent = m_moneyEarnedSecond = m_moneyAdjust = m_moneyGiven = 0;
	m_spentUnits = m_spentStructures = m_spentHeroes = m_spentTotal = 0;
	m_unitsBuilt = m_unitsLost = m_structuresBuilt = m_structuresLost = 0;
	m_purchasePointsEarned = 0;
	m_currentScore = 0;
	m_endFrame = 0;
	m_firstHeroEntry = 0;
	m_skillPointsEarned = 0.0f;
	m_eliminationBonus = 0.0f;
	m_structuresBuilt2 = m_structuresLost2 = m_unitsBuilt2 = m_unitsLost2 = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		m_unitsDestroyed[i] = m_structuresDestroyed[i] = m_unitsDestroyed2[i] = m_structuresDestroyed2[i] = 0;
		m_destroyedByTemplate[i].clear();
	}
	m_lastAttacker = -1;          // RW 0x79EBF9
	m_playerIndex = playerIndex;  // RW 0x79EC78
	m_perFrame.clear();
	m_fortressMarks.clear();
	m_builtByTemplate.clear();
	m_builtByTemplate2.clear();
	m_lostByTemplate.clear();
	m_lostByTemplate2.clear();
	m_hordesBuilt.clear();
	m_unitsAlive = m_structuresAlive = 0;
	m_counting = false;           // RW 0x79ECA9
	m_counters = Counters();
}

bool ScoreKeeper::isScoringStructureForBuild(const Object &obj)
{
	// RW 0x79F116 / 0x79F12A: STRUCTURE + SCORE, or STRUCTURE + SCORE_CREATE (RW 0x70B8C7 with an empty must-be-clear mask)
	return obj.isKindOf(KINDOF_STRUCTURE) && (obj.isKindOf(KINDOF_SCORE) || obj.isKindOf(KINDOF_SCORE_CREATE));
}

bool ScoreKeeper::isScoringStructure(const Object &obj, bool)
{
	// RW 0x79F354 / 0x79F384 (destroyed) and RW 0x79F4AD / 0x79F4C1 (lost): STRUCTURE + SCORE, or STRUCTURE + SCORE_DESTROY
	return obj.isKindOf(KINDOF_STRUCTURE) && (obj.isKindOf(KINDOF_SCORE) || obj.isKindOf(KINDOF_SCORE_DESTROY));
}

bool ScoreKeeper::doNotScore(const Object &obj)
{
	return obj.testStatus(STATUS_DO_NOT_SCORE); // RW 0x44DDEC(0x4C)
}

void ScoreKeeper::addToMap(std::map<std::string, int> &map, const std::string &key, int count, bool clampAtZero)
{
	auto it = map.find(key);
	int v = (it == map.end() ? 0 : it->second) + count;
	if (clampAtZero && v < 0)
	{
		v = 0;
	}
	map[key] = v;
}

// RW 0x79F0E1
void ScoreKeeper::addObjectBuilt(GameLogic &logic, const Object &obj, int count)
{
	if (!m_enabled)
	{
		return;
	}
	if (doNotScore(obj))
	{
		++m_counters.doNotScoreSkipped;
		return;
	}
	++m_counters.builtCalls;
	bool counted = false;
	if (isScoringStructureForBuild(obj))
	{
		m_structuresAlive += count;
		if (m_counting)
		{
			m_structuresBuilt += count;
			m_structuresBuilt2 += count;
			counted = true;
		}
	}
	else if (objectScores(logic, obj))
	{
		m_unitsAlive += count;
		if (m_counting)
		{
			m_unitsBuilt += count;
			m_unitsBuilt2 += count;
			counted = true;
		}
	}
	if (counted)
	{
		addToMap(m_builtByTemplate, templateKey(obj), count, true);   // RW + 0x1F0
		addToMap(m_builtByTemplate2, templateKey(obj), count, true);  // RW + 0x1C8
	}
	// RW 0x79F227 .. 0x79F26B (lane END-2): a HORDE template counts in + 0x1E4 whether or not the keeper is counting
	if (obj.isKindOf(KINDOF_HORDE))
	{
		addToMap(m_hordesBuilt, templateKey(obj), count, true);
	}
	if (m_counting)
	{
		// RW 0x79F247 .. 0x79F269: the fortress marks (+ 0x328)
		if (obj.isKindOf(KINDOF_COMMANDCENTER) && obj.isKindOf(KINDOF_SCORE) && !obj.isKindOf(KINDOF_SUMMONED))
		{
			m_fortressMarks.push_back((int)m_perFrame.size());
		}
		// RW 0x79F26E .. 0x79F2B1: the first hero's entry (+ 0xF4)
		if (m_firstHeroEntry == 0 && obj.isKindOf(KINDOF_HERO) && !obj.isKindOf(KINDOF_SUMMONED))
		{
			m_firstHeroEntry = (int)m_perFrame.size();
		}
	}
}

// RW 0x79F303 (the killer's keeper)
void ScoreKeeper::addObjectDestroyed(GameLogic &logic, const Object &victim)
{
	if (!m_enabled || doNotScore(victim))
	{
		return;
	}
	const Player *victimPlayer = victim.getControllingPlayer();
	const int idx = victimPlayer ? victimPlayer->getPlayerIndex() : -1; // RW 0x79F333: Player + 0x54 of the victim's controller
	if (idx < 0 || idx >= MAX_PLAYERS)
	{
		return; // retail indexes the arrays with it unchecked; a victim without a controller does not reach here (scoreTheKill tests the player first)
	}
	++m_counters.destroyedCalls;
	if (isScoringStructure(victim, true))
	{
		if (!m_counting)
		{
			return;
		}
		++m_structuresDestroyed[idx];  // RW 0x79F36A
		++m_structuresDestroyed2[idx]; // RW 0x79F36E
	}
	else
	{
		if (!objectScores(logic, victim) || !m_counting)
		{
			return;
		}
		++m_unitsDestroyed[idx];  // + 0x20
		++m_unitsDestroyed2[idx]; // + 0x170
	}
	addToMap(m_destroyedByTemplate[idx], templateKey(victim), 1, false); // RW 0x79F3BD .. : + 0x1FC [idx]
}

// RW 0x79F486 (the victim's keeper)
void ScoreKeeper::addObjectLost(GameLogic &logic, const Object &victim)
{
	if (!m_enabled || doNotScore(victim))
	{
		return;
	}
	++m_counters.lostCalls;
	if (isScoringStructure(victim, false))
	{
		if (--m_structuresAlive < 0)
		{
			m_structuresAlive = 0;
		}
		++m_structuresLost;  // + 0xCC
		++m_structuresLost2; // + 0x16C
	}
	else
	{
		if (!objectScores(logic, victim))
		{
			return;
		}
		if (--m_unitsAlive < 0)
		{
			m_unitsAlive = 0;
		}
		++m_unitsLost;  // + 0x74
		++m_unitsLost2; // + 0x1C4
	}
	addToMap(m_lostByTemplate, templateKey(victim), 1, false);  // + 0x2EC
	addToMap(m_lostByTemplate2, templateKey(victim), 1, false); // + 0x1D4
	// RW 0x79F5A8 ..: the body's last damage source (vslot 0x40 -> + 8), its controlling player, a playable side other than ours
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(victim.getBodyModule());
	if (!body)
	{
		return;
	}
	const Object *source = logic.findObjectByID(body->lastDamager());
	if (!source)
	{
		return;
	}
	const Player *attacker = source->getControllingPlayer();
	if (playableSide(attacker))
	{
		const int idx = attacker ? attacker->getPlayerIndex() : -1;
		if (idx >= 0 && idx != m_playerIndex)
		{
			m_lastAttacker = idx;
		}
	}
}

// RW 0x79F704
void ScoreKeeper::recordPerFrameStats(GameLogic &logic, const Player &player, unsigned frame)
{
	// RW 0x79F716 .. 0x79F735: the keeper's player by its index (the owner); RW 0x6AAC52 alive, RW 0x6AAC66 playable side
	if (player.isDefeated() || !playableSide(&player))
	{
		return;
	}
	if (frame >= m_perFrame.size())
	{
		m_perFrame.resize((size_t)frame + 1); // RW 0x79F6E3: default entries
	}
	PerFrameStats &e = m_perFrame[frame];
	e.recorded = true;
	e.money = player.getMoney()->countMoney(); // RW 0x79F781: Player + 0x94
	if (player.isSkirmishAI())
	{
		++m_counters.aiMoneyTermsUnported; // RW 0x79F78A .. 0x79F7AC (S-1061)
	}
	e.unitsAlive = (std::uint16_t)m_unitsAlive;
	e.structuresAlive = (std::uint16_t)m_structuresAlive;
	e.skillPoints = (std::uint16_t)SimMath::truncToInt32(m_skillPointsEarned); // fld + _ftol (truncation)
	const GameLogicSettings::ScoreMultipliers &m = logic.settings().score;
	using SimMath::sseFromInt32;
	float s = SimMath::mulf32(m.skillPoints, m_skillPointsEarned);
	s = SimMath::addf32(SimMath::mulf32(sseFromInt32((std::int32_t)(m_moneyEarned - m_moneyAdjust)), m.suppliesCollected), s);
	s = SimMath::addf32(SimMath::mulf32(sseFromInt32(m_structuresBuilt), m.structuresBuilt), s);
	s = SimMath::addf32(SimMath::mulf32(sseFromInt32(m_unitsBuilt), m.unitsBuilt), s);
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (i == m_playerIndex)
		{
			continue;
		}
		s = SimMath::addf32(SimMath::mulf32(sseFromInt32(m_unitsDestroyed[i]), m.unitsDestroyed), s);
		s = SimMath::addf32(SimMath::mulf32(sseFromInt32(m_structuresDestroyed[i]), m.structuresDestroyed), s);
	}
	s = SimMath::addf32(SimMath::mulf32(sseFromInt32((std::int32_t)m_purchasePointsEarned), m.powerPoints), s);
	s = SimMath::addf32(SimMath::mulf32(sseFromInt32(0), m.heroesVetted), s); // + 0xD0: no ported adder (S-1061)
	s = SimMath::addf32(SimMath::mulf32(sseFromInt32(0), m.unitsVetted), s);  // + 0xD4
	s = SimMath::addf32(m_eliminationBonus, s);
	e.score = s;
	++m_counters.framesRecorded;
}

// RW 0x79DF0E
int ScoreKeeper::timeTakenScore(const GameLogicSettings &settings, unsigned framesTaken)
{
	const GameLogicSettings::ScoreMultipliers &m = settings.score;
	const int minutes = (int)(framesTaken / 5u) / 60; // RW 0x79DC0C: unsigned / LOGICFRAMES_PER_SECOND; RW 0x79DF07: signed idiv 0x3C
	const float top = SimMath::sseFromInt32(SimMath::truncToInt32(m.timeTakenMaximumScore));
	const int v = SimMath::truncToInt32(SimMath::subf32(top, SimMath::mulf32(SimMath::sseFromInt32(minutes), m.timeTakenMultiplier)));
	if (m.timeTakenMinimumScore > SimMath::sseFromInt32(v)) // comiss xmm1 (min), xmm0: above -> trunc(min)
	{
		return SimMath::truncToInt32(m.timeTakenMinimumScore);
	}
	return v;
}

// RW 0x79DFFA
int ScoreKeeper::computeScore(const GameLogicSettings &settings, unsigned frame) const
{
	const GameLogicSettings::ScoreMultipliers &m = settings.score;
	using SimMath::sseFromInt32;
	using SimMath::truncToInt32;
	auto term = [](int acc, std::int32_t count, float mult) {
		return truncToInt32(SimMath::addf32(sseFromInt32(acc), SimMath::mulf32(sseFromInt32(count), mult)));
	};
	int s = truncToInt32(SimMath::mulf32(sseFromInt32((std::int32_t)m_moneyEarned), m.suppliesCollected));
	s = term(s, m_unitsBuilt, m.unitsBuilt);
	s = term(s, m_structuresBuilt, m.structuresBuilt);
	s = term(s, 0, m.heroesVetted); // + 0xD0 (S-1061)
	s = term(s, 0, m.unitsVetted);  // + 0xD4
	s = term(s, (std::int32_t)m_purchasePointsEarned, m.powerPoints);
	s = term(s, 0, m.objectivesCompleted); // RW 0x79DE90: no objective list in a skirmish (S-1061)
	s += timeTakenScore(settings, m_endFrame != 0 ? m_endFrame : frame);
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (i == m_playerIndex)
		{
			continue;
		}
		s = term(s, m_unitsDestroyed[i], m.unitsDestroyed);
		s = term(s, m_structuresDestroyed[i], m.structuresDestroyed);
	}
	s = term(s, 0, m.regionCommandPoints); // + 0xE0 / E4 / E8: Living World only
	s = term(s, 0, m.regionResources);
	s = term(s, 0, m.regionPowerPoints);
	return s;
}

void ScoreKeeper::restoreTotals(const Totals &t)
{
	m_moneyEarned = t.moneyEarned;
	m_unitsBuilt = t.unitsBuilt;
	m_structuresBuilt = t.structuresBuilt;
	m_purchasePointsEarned = t.purchasePoints;
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		m_unitsDestroyed[i] = t.unitsDestroyed[i];
		m_structuresDestroyed[i] = t.structuresDestroyed[i];
	}
	m_endFrame = t.endFrame;
}

int ScoreKeeper::totalUnitsDestroyed() const
{
	int t = 0;
	for (int v : m_unitsDestroyed)
	{
		t += v;
	}
	return t;
}

int ScoreKeeper::totalStructuresDestroyed() const
{
	int t = 0;
	for (int v : m_structuresDestroyed)
	{
		t += v;
	}
	return t;
}

const std::map<std::string, int> &ScoreKeeper::destroyedByTemplate(int victimIndex) const
{
	return victimIndex >= 0 && victimIndex < MAX_PLAYERS ? m_destroyedByTemplate[victimIndex] : kEmptyMap;
}

void ScoreKeeper::crc(StateHasher &h) const
{
	h.addBool(m_enabled);
	h.addU32(m_moneyEarned);
	h.addU32(m_moneySpent);
	h.addU32(m_moneyEarnedSecond);
	h.addU32(m_purchasePointsEarned);
	h.addFloat(m_skillPointsEarned);
	// lane END-1
	h.addFloat(m_eliminationBonus);
	h.addU32(m_moneyAdjust);
	// lane END-2
	h.addU32(m_moneyGiven);
	h.addI32(m_spentUnits);
	h.addI32(m_spentStructures);
	h.addI32(m_spentHeroes);
	h.addI32(m_spentTotal);
	h.addI32(m_unitsBuilt);
	h.addI32(m_unitsLost);
	h.addI32(m_structuresBuilt);
	h.addI32(m_structuresLost);
	h.addI32(m_structuresBuilt2);
	h.addI32(m_structuresLost2);
	h.addI32(m_unitsBuilt2);
	h.addI32(m_unitsLost2);
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		h.addI32(m_unitsDestroyed[i]);
		h.addI32(m_structuresDestroyed[i]);
		h.addI32(m_unitsDestroyed2[i]);
		h.addI32(m_structuresDestroyed2[i]);
		h.addU32((std::uint32_t)m_destroyedByTemplate[i].size());
		for (const auto &kv : m_destroyedByTemplate[i])
		{
			h.addString(kv.first);
			h.addI32(kv.second);
		}
	}
	for (const std::map<std::string, int> *m : { &m_builtByTemplate, &m_builtByTemplate2, &m_lostByTemplate, &m_lostByTemplate2, &m_hordesBuilt })
	{
		h.addU32((std::uint32_t)m->size());
		for (const auto &kv : *m)
		{
			h.addString(kv.first);
			h.addI32(kv.second);
		}
	}
	h.addI32(m_currentScore);
	h.addU32(m_endFrame);
	h.addI32(m_firstHeroEntry);
	h.addI32(m_playerIndex);
	h.addI32(m_lastAttacker);
	h.addI32(m_unitsAlive);
	h.addI32(m_structuresAlive);
	h.addBool(m_counting);
	h.addU32((std::uint32_t)m_fortressMarks.size());
	for (int v : m_fortressMarks)
	{
		h.addI32(v);
	}
	// the per-frame vector only grows and an entry is written in its own frame: its size and the newest entry carry every change (an O(1) hash per frame)
	h.addU32((std::uint32_t)m_perFrame.size());
	if (!m_perFrame.empty())
	{
		const PerFrameStats &e = m_perFrame.back();
		h.addU32(e.money);
		h.addFloat(e.score);
		h.addU32(e.unitsAlive);
		h.addU32(e.structuresAlive);
		h.addU32(e.skillPoints);
		h.addBool(e.recorded);
	}
}

// ---- lane END-2 --------------------------------------------------------------------------------------------------------------------------------------

namespace
{
const KindOfMaskType *kindsOf(GameLogic &logic, const ThingTemplate *tt)
{
	return tt ? &logic.templateInfo(tt->getFinalOverride()).kindOf : nullptr;
}
const KindOfMaskType *kindsOf(GameLogic &logic, const std::string &name)
{
	return kindsOf(logic, logic.things().findTemplate(name));
}
} // namespace

// RW 0x79DFC2
void ScoreKeeper::addMoneySpentByKind(GameLogic &logic, const ThingTemplate *tmpl, int amount)
{
	const KindOfMaskType *k = kindsOf(logic, tmpl);
	if (!k || MaskTest(*k, KINDOF_HERO))
	{
		m_spentHeroes += amount; // + 0x1C
	}
	else if (MaskTest(*k, KINDOF_STRUCTURE))
	{
		m_spentStructures += amount; // + 0x18
	}
	else
	{
		m_spentUnits += amount; // + 0x14
	}
	m_spentTotal += amount; // + 0x1E0
}

// RW 0x79E3A8 -> 0x79E2A8 with the mask of RW 0x9CDF8B (RW 0x9CD60C: HORDE, HERO, MONSTER, MACHINE, SHIP, MINE)
std::string ScoreKeeper::favoriteUnit(GameLogic &logic) const
{
	auto inMask = [](const KindOfMaskType &k) {
		for (unsigned bit : { KINDOF_HORDE, KINDOF_HERO, KINDOF_MONSTER, KINDOF_MACHINE, KINDOF_SHIP, KINDOF_MINE })
		{
			if (MaskTest(k, bit))
			{
				return true; // RW 0x661359: any common bit
			}
		}
		return false;
	};
	std::string best;
	int bestCount = 0; // RW 0x79E2B2: [ebp - 4] = 0, then a strictly higher count wins
	for (const auto &kv : m_builtByTemplate) // + 0x1F0
	{
		const KindOfMaskType *k = kindsOf(logic, kv.first);
		if (!k || !inMask(*k))
		{
			continue;
		}
		// RW 0x79E2E3 .. 0x79E313: HERO, STRUCTURE, MONSTER, MACHINE, HORDE, SHIP or MINE; RW 0x79E315 .. 0x79E32D: not DEPLOYED_MINE, not HORDE_MONSTER
		const bool kind = MaskTest(*k, KINDOF_HERO) || MaskTest(*k, KINDOF_STRUCTURE) || MaskTest(*k, KINDOF_MONSTER) || MaskTest(*k, KINDOF_MACHINE) ||
			MaskTest(*k, KINDOF_HORDE) || MaskTest(*k, KINDOF_SHIP) || MaskTest(*k, KINDOF_MINE);
		if (kind && !MaskTest(*k, KINDOF_DEPLOYED_MINE) && !MaskTest(*k, KINDOF_HORDE_MONSTER) && bestCount < kv.second)
		{
			best = kv.first;
			bestCount = kv.second;
		}
	}
	for (const auto &kv : m_hordesBuilt) // + 0x1E4: the mask only
	{
		const KindOfMaskType *k = kindsOf(logic, kv.first);
		if (k && inMask(*k) && bestCount < kv.second)
		{
			best = kv.first;
			bestCount = kv.second;
		}
	}
	return best;
}

namespace
{
// RW 0x79E404: the counts of the HERO templates that are not SUMMONED (RW 0x70B8C7 with the masks of RW 0x444D39(0x5A) / (0xB3))
int heroCount(GameLogic &logic, const std::map<std::string, int> &map)
{
	int n = 0;
	for (const auto &kv : map)
	{
		const KindOfMaskType *k = kindsOf(logic, kv.first);
		if (k && MaskTest(*k, KINDOF_HERO) && !MaskTest(*k, KINDOF_SUMMONED))
		{
			n += kv.second;
		}
	}
	return n;
}
} // namespace

int ScoreKeeper::heroesBuilt(GameLogic &logic) const
{
	return heroCount(logic, m_builtByTemplate); // RW 0x79E3D6: + 0x1F0
}

int ScoreKeeper::heroesLost(GameLogic &logic) const
{
	return heroCount(logic, m_lostByTemplate); // RW 0x79E44C: + 0x2EC
}
