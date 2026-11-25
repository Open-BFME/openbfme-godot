// OpenBFME. GPL-3.0.
//
// RunOffMapBehavior (lane FACT-B; editions: both RotWK 2.01 and BFME2 1.06):
// Makes a unit flee off the map to a waypoint or in its facing direction, then die or delete.
//
// TARGET FACTS:
// - Registry: RW 0x64B39A / BFME2 0x64B58E ("RunOffMapBehavior", interface mask 0 = BEHAVIOR).
// - RunOffMapBehaviorModuleData:
//   createData RW 0x654C73 / BFME2 0x654F86,
//   ctor RW 0x86522F / BFME2 0x254ECE,
//   buildFieldParse RW 0x865243 / BFME2 0x46215D:
//   * DieOnMap (INI::parseBool, +0x18, default false)
//   * RequiresSpecificTrigger (INI::parseBool, +0x10, default false)
//   * RunOffMapWaypointName (INI::parseAsciiString, +0x14, default "")
// - RunOffMapBehavior:
//   ctor RW 0x86525F / BFME2 0x46217B,
//   dtor RW 0x865305 / BFME2 0x4622B3,
//   slot 5 RW 0x865321 / BFME2 0x462213: if (!RequiresSpecificTrigger) triggerFlee(),
//   triggerFlee RW 0x86540A / BFME2 0x4622FC:
//   * checks object has AIUpdate
//   * resolves waypoint position or projects facing vector * 500.0f
//   * orders AI moveToAndDie (DieOnMap) or moveToAndDelete
//   * sets status 0x32 and condition 0xFC (RUNNING_OFF_MAP).
// Local 171: RW 0x655C5B (~RunOffMapBehaviorModuleData), RW 0x654C1D (RunOffMapBehaviorModuleData ctor)

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"

#include <string>

class ModuleFactory;
class MultiIniFieldParse;

class RunOffMapBehaviorModuleData : public ModuleData
{
public:
	bool m_requiresSpecificTrigger = false; ///< +0x10 RequiresSpecificTrigger
	std::string m_runToLocation; ///< +0x08 RunToLocation (BFME1 / RotWK field table RW 0x865278)
	std::string m_waypointName;             ///< +0x14 RunOffMapWaypointName
	bool m_dieOnMap = false;                ///< +0x18 DieOnMap

	RunOffMapBehaviorModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x008652BA (getModuleName), RW 0x008652C0 (getModuleNameKey)
class RunOffMapBehavior : public BehaviorModule
{
public:
	OBFME_MODULE_XFER(RunOffMapBehavior)

	RunOffMapBehavior(Thing *thing, const RunOffMapBehaviorModuleData *data);

	void triggerFlee();
	bool hasTriggered() const { return m_triggered; }

	static void registerClass(ModuleFactory &modules);

private:
	const RunOffMapBehaviorModuleData *m_data = nullptr;
	bool m_triggered = false;
};

