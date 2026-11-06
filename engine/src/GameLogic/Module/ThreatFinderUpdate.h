// OpenBFME. GPL-3.0.
//
// ThreatFinderUpdate (lane RAI-THREAT; module class from RotWK 2.01 game.dat, caveat S-001): the named threat area a map places with the
// "ThreatFinder" / "TacticalMarkerBase" objects. The module makes an AIThreatFinder (GameLogic/SkirmishAI/AIThreatFinder.h) named by the
// object's script name, registers it in TheThreatFinderManager and copies the object's position into it every frame. Map scripts read it
// through SET_COUNTER_TO_THREAT_FINDER_THREAT (RW 0x7C68AA) and SET_COUNTER_TO_UNIT_THREAT's KindOf THREAT_FINDER branch (RW 0x608BCA).
//
// TARGET FACTS (RotWK game.dat; registry golden engine/data/rotwk-201/module-registry.json: site RW 0x65A133, create RW 0x64F96A,
// createData RW 0x64F9A2, module data vtable RW 0xC07A30, data table RW 0xC4C320, interface mask 1):
//   * data (0xC bytes): + 0x08 DefaultRadius, parseReal RW 0x42ED00 (constructor RW 0x7EDD1B sets 0; the table RW 0xC4C320 names
//     "DefaultRadius" at + 0x08). The object data uses DefaultRadius = 100.0f; a map's "objectThreatFinderRadius" property overrides it.
//   * module (constructor RW 0x7EDD3D: UpdateModule RW 0x653114, finder + 0x20 = null; vtables RW 0xC4C40C / 0xC4C350 / 0xC4C340).
//   * onMapPlacement: behaviour interface slot 45 (+ 0xB4) RW 0x7EDECE (called by Object RW 0x694692 with the map object's properties): with
//     an object, a new AIThreatFinder (RW 0x7EDDFB) named by the object's name (+ 0x88) and noStructures 0 (RW 0x7EDEFF), registered by
//     that name in TheThreatFinderManager (RW 0x7EDEA4 -> RW 0x7EE33A); the radius is the property "objectThreatFinderRadius"
//     (RW 0xDA2F24 -> RW 0x714ACA) when the object has it, else the data's DefaultRadius (RW 0x7EDF51 .. 0x7EDF5A).
//   * update RW 0x7EDDB6: the object's position (Object + 0x38) copied to the finder's + 0x5A4; returns 1 (UPDATE_SLEEP_NONE) every frame.
//   * xfer RW 0x7EDF70: UpdateModule::xfer (RW 0x850C60), then (on load) the finder made and registered when it is missing, then the finder's
//     own xfer RW 0x7EDC93: 20 records of 0x48 bytes, the name, the Coord3D position, the radius, the scan frame, the no-structures flag.
//   * the destructor RW 0x7EDFFF -> RW 0x7EDE52 removes the named finder from the manager.
//
// The readers' action tables are already ported (ScriptTemplateTables.inc): SET_COUNTER_TO_THREAT_FINDER_THREAT = 556, SET_COUNTER_TO_UNIT_THREAT
// = 439. No script condition reads TheThreatFinderManager (the stop row's earlier "condition RW 0x608BCA" was the 439 helper, which reads a
// KindOf THREAT_FINDER object's area). UNIT_THREAT_LEVEL / TEAM_THREAT_LEVEL (115 / 116) read an object's / team's own cached threat, S-1180.

#pragma once

#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>

class AIThreatFinder;
class Dict;
class ModuleFactory;
class MultiIniFieldParse;
class StateHasher;
class Xfer;

class ThreatFinderUpdateModuleData : public ModuleData
{
public:
	float m_defaultRadius = 0.0f; ///< + 0x08
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ThreatFinderUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(ThreatFinderUpdate)
	ThreatFinderUpdate(Thing *thing, const ThreatFinderUpdateModuleData *data); ///< RW 0x7EDD3D
	~ThreatFinderUpdate() override;
	UpdateSleepTime update() override;                            ///< RW 0x7EDDB6
	void onMapPlacement(const Dict *properties) override;         ///< behaviour slot 45, RW 0x7EDECE
	void onDelete() override;                                     ///< the destructor's manager removal
	void crc(StateHasher &hasher) const override;
	static void registerModule(ModuleFactory &modules);

	const AIThreatFinder *finder() const { return m_finder.get(); }

private:
	void createFinder(const Dict *properties);
	void destroyFinder();

	const ThreatFinderUpdateModuleData *m_data;
	std::unique_ptr<AIThreatFinder> m_finder;
};
