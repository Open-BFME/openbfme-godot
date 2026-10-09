// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Source/GameLogic/Object/ExperienceTracker.cpp).
// See GameLogic/Object/ExperienceTracker.h.

#include "GameLogic/Object/ExperienceTracker.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <variant>

namespace
{
const unsigned kStatusTemporarilyDefected = 62; // RW 0x79D328 (TheObjectStatusNames[62])
const unsigned kStatusHordeMember = 38;         // RW 0x79DB2C (TheObjectStatusNames[38])
} // namespace

ExperienceTracker::ExperienceTracker(Object *parent)
	: m_parent(parent)
{
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(parent->getTemplate());
	m_templateName = tt->getName(); // RW 0x79D43C: the name key of the template's own name (+0x64), not its final override's
	// RW 0x79D37F: the template's ExperienceScalarTable (+0x9C; data\ini\default\object.ini sets DefaultExperienceScalarTable for every object)
	std::string tableName;
	if (const FieldValue *v = tt->getFinalOverride()->findField("ExperienceScalarTable"))
	{
		if (const std::string *s = std::get_if<std::string>(v))
		{
			tableName = *s;
		}
	}
	if (TheExperienceLevelSystem)
	{
		m_table = TheExperienceLevelSystem->findScalarTable(tableName);
	}
}

bool ExperienceTracker::multiplayerGame() const
{
	return m_parent->logic().economy().isMultiplayerGame();
}

bool ExperienceTracker::isTrainable() const
{
	// template + 0x5F7, default TRUE (RW 0x740063)
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(m_parent->getTemplate())->getFinalOverride();
	if (const FieldValue *v = tt->findField("IsTrainable"))
	{
		if (const bool *b = std::get_if<bool>(v))
		{
			return *b;
		}
	}
	return true;
}

int ExperienceTracker::getExperienceValue(const Object &killer, bool evenForAllies) const
{
	if (killer.getRelationship(*m_parent) == ALLIES && !evenForAllies)
	{
		return 0;
	}
	return m_experienceValue;
}

int ExperienceTracker::getOwnGuysDieValue(const Object &killer) const
{
	if (killer.getRelationship(*m_parent) == ALLIES)
	{
		return 0;
	}
	return m_ownGuysDieValue == -1 ? 0 : m_ownGuysDieValue;
}

bool ExperienceTracker::isAcceptingExperiencePoints() const
{
	if (m_parent->testStatus(kStatusTemporarilyDefected))
	{
		return false;
	}
	return isTrainable() || m_sink != INVALID_ID;
}

float ExperienceTracker::factorFor(int rank) const
{
	if (!m_table || m_table->m_scalars.empty())
	{
		return 1.0f;
	}
	int idx = rank - m_baseRank; // RW 0x79D3B0
	const int size = (int)m_table->m_scalars.size();
	if (idx <= 0)
	{
		idx = 0;
	}
	else if (idx >= size)
	{
		idx = size - 1;
	}
	return m_table->m_scalars[(size_t)idx];
}

float ExperienceTracker::scaleForRank(float value) const
{
	if (m_parent->logic().economy().context().gameKind == 3 || m_parent->isKindOfName("STRUCTURE"))
	{
		return value;
	}
	return NumericState::pc24Mul(value, m_rankFactor); // fld value; fmul factor (PC24)
}

const ExperienceLevelTemplate *ExperienceTracker::nextLevel() const
{
	if (!TheExperienceLevelSystem)
	{
		return nullptr;
	}
	return TheExperienceLevelSystem->nextLevel(m_templateName, m_levelName, multiplayerGame());
}

int ExperienceTracker::checkLevels(bool feedback)
{
	if (!TheExperienceLevelSystem || !TheExperienceLevelSystem->levelsFor(m_templateName))
	{
		return 0;
	}
	const int t = SimMath::truncToInt32(m_experience); // cvttss2si
	const ExperienceLevelTemplate *reached = nullptr;
	for (const ExperienceLevelTemplate *next = nextLevel(); next && t >= next->m_requiredExperience; next = nextLevel())
	{
		onLevelReached(*next, feedback);
		reached = next;
	}
	return reached ? reached->m_rank : m_rank;
}

void ExperienceTracker::onLevelReached(const ExperienceLevelTemplate &level, bool feedback)
{
	GameLogic &logic = m_parent->logic();
	logic.experience().queueLevelGrant(level, *m_parent, feedback); // RW 0x79DAB3
	m_levelName = level.m_name;                                     // RW 0x79DAD8
	setRank(level.m_rank);                                          // RW 0x79DAE7
	if (level.m_rank > 1 && !m_leveled && !m_parent->isKindOfName("STRUCTURE")) // RW 0x79DAEC .. 0x79DB04
	{
		m_leveled = true;
		if (logic.economy().context().scoring && m_parent->getControllingPlayer()) // RW 0x79DB50 / 0x79DB6B: only while scores are kept
		{
			if (m_parent->isKindOfName("HERO"))
			{
				++logic.experience().counters().heroLevelUps;
			}
			else if (!m_parent->testStatus(kStatusHordeMember))
			{
				++logic.experience().counters().unitLevelUps;
			}
		}
	}
}

void ExperienceTracker::setRank(int rank)
{
	if (rank != 0 && m_rank != rank)
	{
		m_rank = rank;
		m_rankFactor = factorFor(m_rank); // RW 0x79D717
	}
}

void ExperienceTracker::setHelperBase(int rank)
{
	m_baseRank = rank; // RW 0x79D700: the base, then the factor at that rank (index 0)
	m_rankFactor = factorFor(rank);
}

void ExperienceTracker::setBaseRank(int rank)
{
	setHelperBase(rank); // RW 0x79D745
	if (m_rank == rank)
	{
		m_leveled = false;
	}
}

void ExperienceTracker::resetBaseToRank()
{
	setBaseRank(m_rank);
}

void ExperienceTracker::addInternal(float value, bool scaleByRank, bool scaleByScalar, bool feedback, float multiplier)
{
	if (!isTrainable())
	{
		return;
	}
	if (m_levelCap > 0 && m_rank >= m_levelCap)
	{
		return;
	}
	float x = value;
	if (scaleByScalar)
	{
		x = SimMath::mulf32(SimMath::mulf32(m_scalar, value), multiplier); // RW 0x79D6B9: scalar * value * multiplier (SSE)
	}
	if (scaleByRank)
	{
		x = scaleForRank(x);
	}
	m_experience = SimMath::addf32(m_experience, x);
	checkLevels(feedback);
}

void ExperienceTracker::addExperiencePoints(float value, bool scaleByRank, bool scaleByScalar, bool feedback, bool unused)
{
	ExperienceTracker *t = this;
	GameLogic &logic = m_parent->logic();
	while (t->m_sink != INVALID_ID) // RW 0x79D843
	{
		Object *sink = logic.findObjectByID(t->m_sink);
		if (!sink || !sink->getExperienceTracker())
		{
			break;
		}
		value = SimMath::mulf32(t->m_scalar, value);
		t = sink->getExperienceTracker();
		unused = false;
		feedback = true; // RW 0x79D863: +0x14 (feedback) = true; the scaling flags stay as given
	}
	float multiplier = 1.0f;
	const Economy &economy = logic.economy();
	if (economy.isMultiplayerGame()) // RW 0x79D882
	{
		const int n = economy.livePlayableCount(false) - 1; // RW 0x6A8630(0), then index n - 1 (RW 0x64201C / 0x642037)
		const EconomySettings &s = economy.settings();
		const float *table = t->m_parent->isKindOfName("STRUCTURE") ? s.multiPlayBuildingXPMult : s.multiPlayUnitXPMult;
		multiplier = (n >= 0 && n < 20) ? table[n] : 1.0f;
	}
	(void)unused;
	t->addInternal(value, scaleByRank, scaleByScalar, feedback, multiplier);
}

void ExperienceTracker::setExperienceAndLevel(float value, bool setBase)
{
	ExperienceTracker *t = this;
	GameLogic &logic = m_parent->logic();
	while (t->m_sink != INVALID_ID) // RW 0x79D8F2
	{
		Object *sink = logic.findObjectByID(t->m_sink);
		if (!sink || !sink->getExperienceTracker())
		{
			break;
		}
		t = sink->getExperienceTracker();
	}
	if (!t->isTrainable())
	{
		return;
	}
	t->m_experience = value;
	const int rank = t->checkLevels(t->m_defaultFeedback);
	if (setBase && rank != 0 && rank != t->m_baseRank)
	{
		t->setHelperBase(rank); // RW 0x79D947 -> 0x79D700 (the base and its factor; +0x20 is not touched here)
	}
}

void ExperienceTracker::applyLevel(const ExperienceLevelTemplate &level, bool feedback, bool setBase)
{
	onLevelReached(level, feedback);
	m_experience = (float)level.m_requiredExperience; // RW 0x79D77F cvtsi2ss
	setRank(level.m_rank);
	if (setBase)
	{
		setBaseRank(level.m_rank);
	}
}

void ExperienceTracker::gainLevel(bool feedback)
{
	if (const ExperienceLevelTemplate *next = nextLevel())
	{
		applyLevel(*next, feedback, false);
	}
}

void ExperienceTracker::gainLevels(int levels, bool feedback)
{
	for (int i = 0; i < levels; ++i)
	{
		gainLevel(feedback);
	}
}

int ExperienceTracker::experienceForNextLevel(int *nextRank) const
{
	const ExperienceLevelTemplate *next = nextLevel();
	if (!next)
	{
		return 0;
	}
	if (nextRank)
	{
		*nextRank = next->m_rank;
	}
	return next->m_requiredExperience - SimMath::truncToInt32(m_experience);
}

bool ExperienceTracker::gainExpForLevel(int levels, bool feedback, bool unused)
{
	bool gained = false;
	for (int i = 0; i < levels; ++i)
	{
		const int x = experienceForNextLevel(nullptr);
		if (x <= 0)
		{
			break;
		}
		addExperiencePoints((float)x, false, false, feedback, unused);
		gained = true;
	}
	return gained;
}

void ExperienceTracker::setLevelTemplate(const ExperienceLevelTemplate &level)
{
	m_levelName = level.m_name;                         // RW 0x79D810
	m_experienceValue = level.m_experienceAward;        // RW 0x79D815
	m_ownGuysDieValue = level.m_experienceAwardOwnGuysDie;
	setRank(level.m_rank);                              // RW 0x79D829
}

void ExperienceTracker::reset()
{
	m_sink = INVALID_ID;
	m_experience = 0.0f;
	m_scalar = 1.0f;
	m_levelName.clear();
	m_leveled = false;
	gainLevel(true);
}

void ExperienceTracker::crc(StateHasher &hasher) const
{
	hasher.addString(m_levelName);
	hasher.addFloat(m_experience);
	hasher.addI32(m_experienceValue);
	hasher.addI32(m_ownGuysDieValue);
	hasher.addFloat(m_scalar);
	hasher.addBool(m_leveled);
	hasher.addI32(m_rank);
	hasher.addI32(m_levelCap);
	hasher.addFloat(m_rankFactor);
	hasher.addI32(m_baseRank);
	hasher.addU32(m_sink);
}
