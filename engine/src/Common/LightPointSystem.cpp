// OpenBFME. GPL-3.0.
//
// LightPointTracker and TheLightPointSystem (PORT-113): see Common/LightPointTracker.h and Common/LightPointSystem.h. Every body below
// follows the RotWK 2.01 function named next to it.
#include "Common/LightPointSystem.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

thread_local LightPointSystem *TheLightPointSystem = nullptr; // per thread, like the stores it sits with (SMOOTH-1: the world context of each thread)

// ---- LightPointTracker ----

// RW 0x8309E3: add [ecx + 4], amount; jns; and [ecx + 4], 0. The sum wraps like the retail add and its sign decides.
void LightPointTracker::addPoints(int amount)
{
	m_points = (int)((std::uint32_t)m_points + (std::uint32_t)amount);
	if (m_points < 0)
	{
		m_points = 0;
	}
}

// RW 0x6AA91D: sub [ecx + 0x33C], cost; jns; and [..], 0
void LightPointTracker::subtractPoints(int cost)
{
	m_points = (int)((std::uint32_t)m_points - (std::uint32_t)cost);
	if (m_points < 0)
	{
		m_points = 0;
	}
}

// RW 0x8309F3
bool LightPointTracker::hasLevel(int index) const
{
	return std::find(m_levels.begin(), m_levels.end(), index) != m_levels.end();
}

// RW 0x830D1E
void LightPointTracker::addLevel(int index)
{
	if (!hasLevel(index))
	{
		m_levels.push_back(index);
	}
}

// RW 0x830AB9
void LightPointTracker::removeLevel(int index)
{
	const auto it = std::find(m_levels.begin(), m_levels.end(), index);
	if (it != m_levels.end())
	{
		m_levels.erase(it);
	}
}

void LightPointTracker::reset()
{
	m_levels.clear(); // RW 0x6B0355
	m_points = 0;     // RW 0x6B0362
}

void LightPointTracker::crc(StateHasher &h) const
{
	h.addI32(m_points);
	h.addU32((std::uint32_t)m_levels.size());
	for (int index : m_levels)
	{
		h.addI32(index);
	}
}

void LightPointTracker::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	std::int32_t points = x.isLoading() ? 0 : m_points;
	x.xferInt(&points);
	if (x.isLoading() && points < 0)
	{
		throw XferException("light points: a total of " + std::to_string(points) + " (the tracker never holds a negative one)");
	}
	std::vector<int> levels;
	if (!x.isLoading())
	{
		levels = m_levels;
	}
	x.xferVectorSize(levels, 4);
	// a level index names one of this game's levels (the indices are positions in TheLightPointSystem's list), once
	const size_t known = TheLightPointSystem ? TheLightPointSystem->getNumLevels() : 0;
	for (size_t i = 0; i < levels.size(); ++i)
	{
		std::int32_t index = x.isLoading() ? 0 : levels[i];
		x.xferInt(&index);
		if (x.isLoading())
		{
			if (index < 0 || (size_t)index >= known)
			{
				throw XferException("light points: level index " + std::to_string(index) + " is not one of this game's " + std::to_string(known) + " levels");
			}
			if (std::find(levels.begin(), levels.begin() + (std::ptrdiff_t)i, index) != levels.begin() + (std::ptrdiff_t)i)
			{
				throw XferException("light points: level index " + std::to_string(index) + " is listed twice");
			}
			levels[i] = index;
		}
	}
	if (x.isLoading())
	{
		m_points = points;
		m_levels = std::move(levels);
	}
}

// ---- LightPointSystem ----

namespace
{
#define LPL_OFF(member) (int)offsetof(LightPointLevel, member)

void parseLabel(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73B192 translates the label and throws for an unknown one (GameText not loaded: S-520)
}

// RW 0x73B4FF: every token of the line; a name TheSpecialPowerStore does not know is skipped (RW 0x73B51E); the id of the final override (+ 0x14) is
// APPENDED (RW 0x73B534), so the row of a map.ini override adds to the list it copied
void parseSpecialAbilities(INI *ini, void *, void *store, const void *)
{
	std::vector<unsigned> *ids = static_cast<std::vector<unsigned> *>(store);
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(token) : nullptr; // RW 0x69C146
		if (t)
		{
			ids->push_back(t->getFinalOverride()->getID());
		}
	}
}

// RW 0xC532C0, in the binary's order
const FieldParse kLightPointLevelFieldParse[] = {
	{ "Name", parseLabel, nullptr, LPL_OFF(m_displayName) },
	{ "SpecialAbilities", parseSpecialAbilities, nullptr, LPL_OFF(m_specialAbilities) },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

LightPointSystem::~LightPointSystem()
{
	if (TheLightPointSystem == this)
	{
		TheLightPointSystem = nullptr;
	}
}

void LightPointSystem::init()
{
	m_levels.clear();
	m_owned.clear();
}

void LightPointSystem::reset()
{
	// a stored level is never an override itself (the parser makes a new level only outside load type 2), so RW 0x5E2E2E deletes its chain and keeps it
	for (LightPointLevel *level : m_levels)
	{
		level->m_nextOverride = nullptr;
	}
	m_owned.erase(std::remove_if(m_owned.begin(), m_owned.end(), [](const std::unique_ptr<LightPointLevel> &level) { return level->m_isOverride; }),
		m_owned.end());
}

LightPointLevel *LightPointSystem::newLevel()
{
	m_owned.push_back(std::unique_ptr<LightPointLevel>(new LightPointLevel));
	return m_owned.back().get();
}

LightPointLevel *LightPointSystem::findLevel(const std::string &name) const
{
	for (LightPointLevel *level : m_levels)
	{
		if (level->m_name == name)
		{
			return level;
		}
	}
	return nullptr;
}

void LightPointSystem::addLevel(LightPointLevel *level)
{
	if (const LightPointLevel *existing = findLevel(level->m_name)) // RW 0x830E17
	{
		throw INIException(3, "A light point level %s already exists.", existing->m_name.c_str()); // RW 0xC53284
	}
	m_levels.push_back(level); // RW 0x830E59
}

void LightPointSystem::parseLightPointLevelDefinition(INI *ini)
{
	const std::string name = ini->getNextToken(); // RW 0x830ED8
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) // RW 0x830EEA
	{
		LightPointLevel *existing = findLevel(name); // RW 0x830F02
		if (!existing)
		{
			throw INIException(3, "Light point level %s not found in map.ini", name.c_str()); // RW 0xC532F0
		}
		LightPointLevel *last = existing->getFinalOverride(); // RW 0x830F59 .. 0x830F65
		LightPointLevel *level = newLevel();
		level->m_name = last->m_name; // RW 0x830CE5: the name, the label and the list are copied
		level->m_displayName = last->m_displayName;
		level->m_specialAbilities = last->m_specialAbilities;
		last->m_nextOverride = level; // RW 0x830F77
		level->m_isOverride = true;   // RW 0x830F7B
		ini->initFromINI(level, kLightPointLevelFieldParse); // RW 0x830F7F
		return;
	}
	LightPointLevel *level = newLevel();
	level->m_name = name; // RW 0x830FA6
	try
	{
		ini->initFromINI(level, kLightPointLevelFieldParse); // RW 0x830FB3
		addLevel(level);                                     // RW 0x830FBF: after the block was read
	}
	catch (...)
	{
		m_owned.pop_back(); // not listed: the level goes (retail leaks it)
		throw;
	}
}

void LightPointSystem::parseLightPointLevelDefinitionGlobal(INI *ini)
{
	if (!TheLightPointSystem) // RW 0x830EC2: the function returns, the block's rows are left to the caller
	{
		return;
	}
	TheLightPointSystem->parseLightPointLevelDefinition(ini);
}

int LightPointSystem::findLevelIndexOfSpecialPower(const SpecialPowerTemplate *t) const
{
	for (size_t i = 0; i < m_levels.size(); ++i)
	{
		for (unsigned id : m_levels[i]->m_specialAbilities) // RW 0x830A5E .. 0x830A6F: the stored level's own list, its override is not followed
		{
			if (id == t->getFinalOverride()->getID()) // RW 0x830A75 .. 0x830A7C
			{
				return (int)i;
			}
		}
	}
	return -1;
}

bool LightPointSystem::canUseSpecialPower(const LightPointTracker &tracker, const SpecialPowerTemplate *t) const
{
	if (findLevelIndexOfSpecialPower(t) < 0) // RW 0x830AEE
	{
		return true;
	}
	return tracker.getPoints() >= t->getFinalOverride()->m_lightPointCost; // RW 0x830AF8 .. 0x830B07 (setge)
}

int LightPointSystem::findLevelIndexByFinalName(const std::string &name) const
{
	for (size_t i = 0; i < m_levels.size(); ++i)
	{
		if (m_levels[i]->getFinalOverride()->m_name == name) // RW 0x830E7A .. 0x830E94 / 0x830BEC .. 0x830BFF
		{
			return (int)i;
		}
	}
	return -1;
}

void LightPointSystem::giveLevel(LightPointTracker &tracker, const std::string &name) const
{
	const int index = findLevelIndexByFinalName(name);
	if (index >= 0)
	{
		tracker.addLevel(index); // RW 0x830EAB
	}
}

void LightPointSystem::removeLevel(LightPointTracker &tracker, const std::string &name) const
{
	const int index = findLevelIndexByFinalName(name);
	if (index >= 0)
	{
		tracker.removeLevel(index); // RW 0x830C1D
	}
}
