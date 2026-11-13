// OpenBFME. GPL-3.0.
// ThreatFinder and TheThreatFinderManager (PORT 17, issue #73): a map object that carries ThreatFinderUpdate registers a named threat area here, and the
// script action SET_COUNTER_TO_THREAT_FINDER_THREAT asks it. The RW facts, the BFME2 cross-check and what is inference are in GameLogic/Module/ThreatFinderUpdate.h
// (the module that owns them) and named again at each member. The scan, the records and the relationship sum are lane AI-3's (GameLogic/SkirmishAI/AIThreatFinder):
// retail has one finder class, and these are its scan RW 0x7EE057 and its sum RW 0x7EE166.

#include "GameLogic/System/ThreatFinder.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <utility>

namespace
{
// RW 0xBDD42C (RW 0x7EE166, INFERENCE BFME2 0x003ED0C4): a scan this many seconds old is redone before the answer
constexpr float kRescanSeconds = 3.0f;
} // namespace

// RW 0x7EDDFB (INFERENCE BFME2 0x003ECD60): the records zeroed (their ctor), the radius 0, the frame 0, the flag as given
ThreatFinder::ThreatFinder(std::string name, bool noStructures)
	: m_name(std::move(name))
	, m_noStructures(noStructures)
{
}

// RW 0x7EE057's scan into this finder's own records (INFERENCE BFME2 0x003ECFBC)
void ThreatFinder::scan(GameLogic &logic)
{
	AIThreatFinder::scan(logic, m_position, m_radius, m_noStructures, m_records);
	++m_scans;
}

// RW 0x7EE1F4: the answer for one player and one relationship, the records rescanned first when the scan is 3 s old
AIThreatFinder::ThreatRecord ThreatFinder::threatForPlayer(GameLogic &logic, const Player &player, int mode)
{
	const unsigned now = logic.getFrame();
	// (frame - last) * LOGICFRAMES_PER_SECOND >= 3.0: RW 0x7EE166's own test (RW 0xBDD42C), the frames' difference as an int times the rate (INFERENCE BFME2
	// 0x003ED0C4 reads the two frames, multiplies by the rate and compares with 3.0)
	const float age = SimMath::mulf32((float)(std::int64_t)(now - m_lastFrame), (float)LOGICFRAMES_PER_SECOND);
	if (age >= kRescanSeconds)
	{
		scan(logic);
		m_lastFrame = now;
	}
	// retail's switch: 0 isEnemy, 1 isAlly, everything else returns the empty record (RW 0x7EE166's own modes, INFERENCE BFME2 0x003ED0C4)
	if (mode != 0 && mode != 1)
	{
		return AIThreatFinder::ThreatRecord{};
	}
	return AIThreatFinder::sumRecords(logic, m_records, player, mode); // RW 0x7EE166
}

// INFERENCE BFME2 0x003ECBF8: version (1, 1), the twenty records of 18 floats, the name, the position, the radius, the frame, the flag
void ThreatFinder::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	for (AIThreatFinder::ThreatRecord &r : m_records)
	{
		for (float &f : r.v)
		{
			x.xferReal(&f);
		}
	}
	x.xferAsciiString(&m_name);
	SnapshotXfer::coord3D(x, m_position);
	x.xferReal(&m_radius);
	x.xferUnsignedInt(&m_lastFrame);
	x.xferBool(&m_noStructures);
}

void ThreatFinder::xfer(Xfer &x) const
{
	ThreatFinder copy = *this;
	copy.xfer(x);
}

void ThreatFinder::crc(StateHasher &h) const
{
	h.addString(m_name);
	h.addFloat(m_position.x);
	h.addFloat(m_position.y);
	h.addFloat(m_position.z);
	h.addFloat(m_radius);
	h.addU32(m_lastFrame);
	h.addBool(m_noStructures);
	for (const AIThreatFinder::ThreatRecord &r : m_records)
	{
		for (float f : r.v)
		{
			h.addFloat(f);
		}
	}
}

// INFERENCE BFME2 0x003ECDB7 registerName: into TheThreatFinderManager when the name is not the empty string
void ThreatFinder::registerWithManager(GameLogic &logic)
{
	logic.threatFinders().add(logic, m_name, *this);
}

// INFERENCE BFME2 0x003ECDB7's dtor half
void ThreatFinder::removeFromManager(GameLogic &logic)
{
	logic.threatFinders().remove(logic, m_name, *this);
}

// ---- TheThreatFinderManager (RW 0xDE8888) --------------------------------------------------------------------------------------------------
void ThreatFinderManager::reset()
{
	m_finders.clear();
}

// INFERENCE BFME2 0x003ED265: the name's key registered only when it holds no finder yet (a second finder of that name stays unregistered)
void ThreatFinderManager::add(GameLogic &logic, const std::string &name, ThreatFinder &finder)
{
	if (name.empty()) // INFERENCE BFME2 0x003ECDB7: a finder only registers a name that is not the empty string
	{
		return;
	}
	const int key = logic.things().nameKeys().nameToKey(name);
	if (m_finders.find(key) == m_finders.end())
	{
		m_finders.emplace(key, &finder);
	}
}

// INFERENCE BFME2 0x003ED2A3: the key erased when it names this finder
void ThreatFinderManager::remove(GameLogic &logic, const std::string &name, const ThreatFinder &finder)
{
	if (name.empty())
	{
		return;
	}
	const int key = logic.things().nameKeys().findKey(name);
	if (key == NAMEKEY_INVALID)
	{
		return;
	}
	const auto it = m_finders.find(key);
	if (it != m_finders.end() && it->second == &finder)
	{
		m_finders.erase(it);
	}
}

// RW 0x7EE260: the finder whose name's key is `name` (a key the table never saw is a miss)
ThreatFinder *ThreatFinderManager::find(GameLogic &logic, const std::string &name) const
{
	if (name.empty())
	{
		return nullptr;
	}
	const int key = logic.things().nameKeys().findKey(name);
	if (key == NAMEKEY_INVALID)
	{
		return nullptr;
	}
	const auto it = m_finders.find(key);
	return it == m_finders.end() ? nullptr : it->second;
}
