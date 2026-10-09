// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GarrisonContainModuleData / HordeGarrisonContainModuleData: the INI data of the structure garrisons (lane GARRISON-1). Port of ZH
// GameEngine/Include/GameLogic/Module/GarrisonContain.h as changed by RotWK, with RotWK's HordeGarrisonContain (BFME2 / RotWK only).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; module registry engine/data/rotwk-201/module-registry.json):
//   * GarrisonContain: create RW 0x64B740 (module 0x9E0 bytes, constructor RW 0x87B1A7), data RW 0x654CDF (0xAC bytes, constructor RW 0x87CCCB); the build proc
//     RW 0x654CC4 adds OpenContain's chain (RW 0x867F34: the OpenContain table RW 0xC59F30 and the DieMux table RW 0xC76BD8 at +8) then the table RW 0xC095F0:
//       MobileGarrison (+0xA0, parseBool RW 0x42E558), HealObjects (+0x98, bool), TimeForFullHeal (+0x9C, parseDurationReal RW 0x73A403),
//       InitialRoster (RW 0x653381: <name> [<count>], the count 1 when absent; the name at +0xA4, the count at +0xA8; a second line replaces the first),
//       ImmuneToClearBuildingAttacks (+0xA1, bool).
//     The constructor (after OpenContain's RW 0x867E1B) replaces PassengerFilter with RW 0x763E1C(include INFANTRY, exclude none): rule ANY, flag 1 (an empty
//     include mask would have made it RW 0x763D11's NONE); HealObjects / MobileGarrison / ImmuneToClearBuildingAttacks false, TimeForFullHeal 1.0 (RW 0xBD1908), the
//     roster count 0.
//   * HordeGarrisonContain: create RW 0x64B77B (module 0x9E4 bytes, constructor RW 0x87D2BE), data RW 0x64B7B6; the build proc RW 0x87D2A3 adds GarrisonContain's
//     chain (RW 0x654CC4) then the table RW 0xC5C9A0: ExitDelay (+0xAC, parseDurationUnsignedInt RW 0x73A429), EntryOffset (+0xB0), EntryPosition (+0xBC),
//     ExitOffset (+0xC8) (parseCoord3D RW 0x42F247); all zero by default (BFME2 HordeGarrisonContainModuleData constructor, retail 0x0047A251).
// The data classes embed their base FIRST (the parse procs use `store`, see GameLogic/Module/OpenContain.h). The runtime is GarrisonContain.cpp
// (GameLogic/Object/Contain/GarrisonContainRuntime.h).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/MoneyEventModules.h" // DieMuxData
#include "GameLogic/Module/OpenContain.h"

#include <cstddef>
#include <string>

struct GarrisonContainModuleData
{
	GarrisonContainModuleData(); // RW 0x87CCCB

	OpenContainModuleData m_open;  // RW base (first member)
	bool m_doHealing;              // +0x98 HealObjects
	float m_framesForFullHeal;     // +0x9C TimeForFullHeal (frames, not rounded)
	bool m_mobileGarrison;         // +0xA0 MobileGarrison
	bool m_immuneToClearBuildingAttacks; // +0xA1
	std::string m_initialRosterName; // +0xA4 InitialRoster name
	int m_initialRosterCount;      // +0xA8 InitialRoster count

	static void buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset = 0);
	static const FieldParse *getFieldParse();
	static void parseInitialRoster(INI *ini, void *instance, void *store, const void *userData); // RW 0x653381
};

struct HordeGarrisonContainModuleData
{
	HordeGarrisonContainModuleData(); // BFME2 0x0047A251 (RW data proc 0x64B7B6)

	GarrisonContainModuleData m_garrison; // RW base (first member)
	unsigned m_exitDelay;                 // +0xAC ExitDelay (frames)
	Coord3D m_entryOffset;                // +0xB0 EntryOffset
	Coord3D m_entryPosition;              // +0xBC EntryPosition
	Coord3D m_exitOffset;                 // +0xC8 ExitOffset

	static void buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset = 0);
	static const FieldParse *getFieldParse();
};

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

// The typed ModuleData the ModuleFactory makes for `Behavior = GarrisonContain` / `HordeGarrisonContain`. The DieMux table (RW 0xC76BD8, extra 8: OpenContain's
// die interface asks it, RW 0x867141) sits beside the contain data.
class GarrisonContainBehaviorData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	GarrisonContainModuleData garrison;
	static void buildFieldParse(MultiIniFieldParse &p);
	const OpenContainModuleData &open() const { return garrison.m_open; }
};

class HordeGarrisonContainBehaviorData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	HordeGarrisonContainModuleData horde;
	static void buildFieldParse(MultiIniFieldParse &p);
	const OpenContainModuleData &open() const { return horde.m_garrison.m_open; }
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
