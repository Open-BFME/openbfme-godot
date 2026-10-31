// OpenBFME. GPL-3.0.
// See MissionObjectiveTracker.h (lane CAMPUI-1).

#include "GameLogic/ScriptEngine/MissionObjectiveTracker.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/Xfer.h"

namespace
{
// RW 0x836792: every token of the line is a new record (RW 0x835F4F) with that label (RW 0x836663 pushes it)
void parseTag(INI *ini, void *instance, void *, const void *)
{
	auto &list = *static_cast<std::vector<MissionObjective> *>(instance);
	while (const char *token = ini->getNextTokenOrNull())
	{
		MissionObjective o;
		o.label = token;
		list.push_back(o);
	}
}

// RW 0x8367EB: RW 0x836792, then bonus = 1 on the records the line added
void parseBonusTag(INI *ini, void *instance, void *store, const void *userData)
{
	auto &list = *static_cast<std::vector<MissionObjective> *>(instance);
	const size_t before = list.size();
	parseTag(ini, instance, store, userData);
	for (size_t i = before; i < list.size(); ++i)
	{
		list[i].bonus = true;
	}
}

// field table RW 0xC537E8
const FieldParse kListFields[] = {
	{ "MissionObjectiveTag", parseTag, nullptr, 0 },
	{ "BonusMissionObjectiveTag", parseBonusTag, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

void MissionObjectiveTracker::reset()
{
	m_hasList = false;
	m_objectives.clear();
	m_screenEnabled = true;
}

void MissionObjectiveTracker::installList(const std::vector<MissionObjective> &list)
{
	if (m_hasList)
	{
		return; // RW 0x836497: the tracker keeps its first list
	}
	m_hasList = true;
	m_objectives = list;
}

bool MissionObjectiveTracker::setShown(int index, bool shown)
{
	if (index < 0 || (size_t)index >= m_objectives.size()) // RW 0x835E24
	{
		return false;
	}
	const bool was = m_objectives[(size_t)index].shown;
	m_objectives[(size_t)index].shown = shown;
	return !was && shown;
}

bool MissionObjectiveTracker::setCompleted(int index, bool completed)
{
	if (index < 0 || (size_t)index >= m_objectives.size())
	{
		return false;
	}
	const bool was = m_objectives[(size_t)index].completed;
	m_objectives[(size_t)index].completed = completed;
	return !was && completed;
}

int MissionObjectiveTracker::countShownBonus(int &total) const
{
	int done = 0;
	total = 0;
	for (const MissionObjective &o : m_objectives)
	{
		if (o.shown && o.bonus)
		{
			++total;
			if (o.completed)
			{
				++done;
			}
		}
	}
	return done;
}

const MissionObjective *MissionObjectiveTracker::nthShown(int n) const
{
	for (const MissionObjective &o : m_objectives)
	{
		if (o.shown && n-- == 0)
		{
			return &o;
		}
	}
	return nullptr;
}

void MissionObjectiveTracker::xfer(Xfer &x)
{
	// RW 0x8363F2 (the tracker's snapshot at + 0xC): a CRC xfer transfers nothing
	if (x.getXferMode() == XFER_CRC)
	{
		return;
	}
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	bool has = m_hasList;
	x.xferBool(&has);
	if (x.isLoading())
	{
		m_hasList = has;
		m_objectives.clear(); // a load makes a new list (RW 0x8363F2), whose vector is empty (RW 0x83669A refuses another)
	}
	if (!has)
	{
		return;
	}
	XferVersion lv{ 1, 1 }; // RW 0x836835
	x.xferVersion(lv);
	XferVersion vv{ 1, 1 }; // RW 0x83669A
	x.xferVersion(vv);
	x.xferVectorSize(m_objectives, 4 + 3);
	for (MissionObjective &o : m_objectives)
	{
		XferVersion rv{ 1, 1 }; // RW 0x835E3F
		x.xferVersion(rv);
		x.xferAsciiString(&o.label);
		x.xferBool(&o.bonus);
		x.xferBool(&o.shown);
		x.xferBool(&o.completed);
	}
}

void MissionObjectiveTracker::xferMenu(Xfer &x)
{
	if (x.getXferMode() == XFER_CRC)
	{
		return;
	}
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_screenEnabled);
}

void MissionObjectiveTracker::parseList(INI *ini, std::vector<MissionObjective> &list, bool &parsed)
{
	const INILoadType type = ini->getLoadType();
	if (type != INI_LOAD_CREATE_OVERRIDES && type != INI_LOAD_RELOAD)
	{
		throw INIException(9, "A MissionObjectiveList may only appear in a map.ini file.");
	}
	std::vector<MissionObjective> fresh;
	ini->initFromINI(&fresh, kListFields);
	if (type == INI_LOAD_CREATE_OVERRIDES && !parsed) // the first list wins (RW 0x836497 deletes a list made while one is installed)
	{
		list = std::move(fresh);
		parsed = true;
	}
}
