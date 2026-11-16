// OpenBFME. GPL-3.0.
// A named threat area (PORT 17, issue #73) and TheThreatFinderManager (RW 0xDE8888): what a map object carrying ThreatFinderUpdate registers, and what the
// script action SET_COUNTER_TO_THREAT_FINDER_THREAT asks. The RW facts, the BFME2 cross-check and the inference are in
// GameLogic/Module/ThreatFinderUpdate.h (the module that owns them) and repeated at each member below.
//
// THE FINDER IS THE PORT'S AIThreatFinder, NOT A SECOND CLASS. Retail has ONE finder class: the object RW 0x7EDDFB makes is 0x5BC bytes - + 0x000 twenty records
// of 0x48 bytes (18 floats: the total and 17 categories), + 0x5A0 the name, + 0x5A4 the position, + 0x5B0 the radius, + 0x5B4 the frame of the last scan, +
// 0x5B8 "no structures" - and the map object's / the AITarget's finders differ only in those fields. Lane AI-3 already ported that class as
// `AIThreatFinder` (GameLogic/SkirmishAI/AIThreatFinder.h: the scan RW 0x7EE057, the query RW 0x7EE166, the value RW 0x68F0EC, the categories RW 0x7EDB72),
// and the AITarget's finder there is the same "no structures" finder RW 0x6C6939 makes. So a map object's finder here holds a name, a position, a radius, the
// last scan's frame and the 20 records through AIThreatFinder, and scans with AIThreatFinder::scan: one mechanism, one set of edges.
//
// (The BFME2 decompilation that #73 cites as its cross-check has the SAME class one release older and smaller - 0x56C bytes, 17 floats to a record, the name
// at + 0x550, the radius at + 0x560 (Object/Update/ThreatFinderUpdate*.cpp, Rva003ECD60/003ECFBC/003ED0C4). Its SHAPE is the cross-check the port already used
// for AIThreatFinder (RW 0x7EE057's scan and RW 0x7EE166's sum); its OFFSETS are BFME2's and are not used.)

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"

#include <cstdint>
#include <map>
#include <string>

class GameLogic;
class Player;
class StateHasher;
class Xfer;

// One named threat area: the map object the module belongs to, kept in TheThreatFinderManager by its own name.
class ThreatFinder
{
public:
	ThreatFinder(std::string name, bool noStructures); ///< RW 0x7EDDFB (INFERENCE BFME2 0x003ECD60: the records zeroed, the radius 0, the frame 0)

	// RW 0x7EE1F4 getThreatForPlayer: the records rescanned when 3 s passed (RW 0xBDD42C, as RW 0x7EE166 does), then the element-wise sum over the players whose
	// relationship to `player` is ENEMIES (mode 0) or ALLIES (mode 1); every other mode is the empty record (retail's switch)
	AIThreatFinder::ThreatRecord threatForPlayer(GameLogic &logic, const Player &player, int mode);
	// RW 0x7EE057: the records cleared, then the objects in the radius around the position, as AIThreatFinder::scan reads them (INFERENCE BFME2 0x003ECFBC)
	void scan(GameLogic &logic);
	// RW 0x7EDF70's tail (INFERENCE BFME2 0x003ECBF8): version (1, 1), the twenty records of 18 floats, the name, the position, the radius, the frame, the flag
	void xfer(Xfer &x);
	void xfer(Xfer &x) const;
	void crc(StateHasher &h) const;

	// INFERENCE BFME2 0x003ECDB7: the finder enters / leaves TheThreatFinderManager by its own name (an empty name never does)
	void registerWithManager(GameLogic &logic);
	void removeFromManager(GameLogic &logic);

	const std::string &name() const { return m_name; }              ///< + 0x5A0
	const Coord3D &position() const { return m_position; }          ///< + 0x5A4
	void setPosition(const Coord3D &p) { m_position = p; }
	float radius() const { return m_radius; }                       ///< + 0x5B0
	void setRadius(float r) { m_radius = r; }
	unsigned lastScanFrame() const { return m_lastFrame; }          ///< + 0x5B4
	bool noStructures() const { return m_noStructures; }            ///< + 0x5B8
	const AIThreatFinder::ThreatRecord &record(int playerIndex) const { return m_records[playerIndex]; }

	// observation only (tests; not state, not hashed): the scans this finder ran
	unsigned long long scans() const { return m_scans; }

private:
	AIThreatFinder::ThreatRecord m_records[AIThreatFinder::kRecords]; ///< + 0x000
	std::string m_name;                                               ///< + 0x5A0
	Coord3D m_position{};                                             ///< + 0x5A4
	float m_radius = 0.0f;                                            ///< + 0x5B0
	unsigned m_lastFrame = 0;                                         ///< + 0x5B4
	bool m_noStructures = false;                                      ///< + 0x5B8
	unsigned long long m_scans = 0;
};

// TheThreatFinderManager (RW 0xDE8888): the finders by name key. Retail: a hash map at + 0x0C, add only when the key is absent, remove by key, lookup by key,
// and nothing else (INFERENCE BFME2 Common/GameLogic/Object/ThreatFinderManager.cpp: add 0x003ED265, remove 0x003ED2A3).
class ThreatFinderManager
{
public:
	void reset();
	// INFERENCE BFME2 0x003ED265: registered only when the name's key is not there yet (a second finder of the same name stays unregistered)
	void add(GameLogic &logic, const std::string &name, ThreatFinder &finder);
	// INFERENCE BFME2 0x003ED2A3: the key is erased when it names this finder
	void remove(GameLogic &logic, const std::string &name, const ThreatFinder &finder);
	// RW 0x7EE260: the finder of that name, else null
	ThreatFinder *find(GameLogic &logic, const std::string &name) const;
	size_t size() const { return m_finders.size(); }

private:
	// retail's hash map (RW 0xDE8888 + 0x0C) keyed by the name's key; the port walks it in key order so every report is stable
	std::map<int, ThreatFinder *> m_finders;
};
