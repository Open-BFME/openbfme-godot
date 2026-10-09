// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The typed module data of TransportContain, HordeTransportContain, SiegeEngineContain and HordeSiegeEngineContain (lane GARRISON-2), and SiegeEngineContain's own
// rows. The runtime is GameLogic/Object/Contain/TransportContainRuntime.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; module registry engine/data/rotwk-201/module-registry.json):
//   * TransportContain: create RW 0x64B546 (module 0x11C bytes, constructor RW 0x86B67D), data RW 0x64B581; tables: OpenContain RW 0xC59F30, the DieMux table
//     RW 0xC76BD8 (extra 8), TransportContain RW 0xC5ABD8 (GameLogic/Module/TransportContain.h, lane HORDE-1).
//   * HordeTransportContain: create RW 0x64B6B1 (0x128 bytes, constructor RW 0x87A286), data RW 0x64B6EC; the same three tables.
//   * SiegeEngineContain: create RW 0x64B899 (0x138 bytes, constructor RW 0x87F4E0), data RW 0x64B8D4 (0x1B8 bytes, constructor RW 0x87FBE9 after TransportContain's
//     RW 0x86B425); the table RW 0xC5D590 after the three: CrewFilter (+0x18C, ParseObjectFilter RW 0x76392F; the default filter RW 0x76406F), CrewMax (+0x190,
//     parseInt RW 0x42EC5E, 0), InitialCrew (RW 0x87EC0B: <name> [<count>], the count 1 when absent; +0x194 / +0x198, a second line replaces the first),
//     SpeedPercentPerCrew (+0x19C, parsePercentToReal RW 0x42EEFA, 1.0 = [0xBD1908]), CrewAllowedToFire (+0x1A0, parseBool RW 0x42E558, false), ObjectStatusOfCrew
//     (+0x1A4, RW 0x866FF0, the ObjectStatusOfContained proc: the mask and its set byte), TransferSelection (+0x1B4, bool, false).
//   * HordeSiegeEngineContain: create RW 0x64B928 (0x144 bytes, constructor RW 0x8804D7), data RW 0x64B963; the table RW 0xC5D900: the same rows without
//     TransferSelection.

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/MoneyEventModules.h" // DieMuxData
#include "GameLogic/Module/TransportContain.h"

#include <cstddef>
#include <string>

struct SiegeEngineContainModuleData
{
	SiegeEngineContainModuleData(); // RW 0x87FBE9

	TransportContainModuleData m_transport;       // RW base (first member)
	ObjectFilter m_crewFilter;                    // +0x18C CrewFilter
	int m_crewMax = 0;                            // +0x190 CrewMax
	std::string m_initialCrewName;                // +0x194 InitialCrew name
	int m_initialCrewCount = 0;                   // +0x198 InitialCrew count
	float m_speedPercentPerCrew = 1.0f;           // +0x19C SpeedPercentPerCrew (fraction)
	bool m_crewAllowedToFire = false;             // +0x1A0 CrewAllowedToFire
	ObjectStatusOfContainedField m_objectStatusOfCrew; // +0x1A4 ObjectStatusOfCrew
	bool m_transferSelection = false;             // +0x1B4 TransferSelection (SiegeEngineContain only)

	// `horde`: HordeSiegeEngineContain's table RW 0xC5D900 (no TransferSelection)
	static void buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset, bool horde);
	static const FieldParse *getFieldParse(bool horde);
	static void parseInitialCrew(INI *ini, void *instance, void *store, const void *userData); // RW 0x87EC0B
};

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

// `Behavior = TransportContain` / `HordeTransportContain`: OpenContain's table, the DieMux table, TransportContain's
class TransportContainBehaviorData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	TransportContainModuleData transport;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class HordeTransportContainBehaviorData : public TransportContainBehaviorData
{
};

// `Behavior = SiegeEngineContain` / `HordeSiegeEngineContain`: the three tables, then the siege table
class SiegeEngineContainBehaviorData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	SiegeEngineContainModuleData siege;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class HordeSiegeEngineContainBehaviorData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	SiegeEngineContainModuleData siege;
	static void buildFieldParse(MultiIniFieldParse &p);
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// RW 0xC76BD8: the DieMux table every die module starts with (shared with GarrisonContainData.cpp)
const FieldParse *DieMuxContainFieldParse();
