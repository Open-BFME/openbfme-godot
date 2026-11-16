// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001) with the BFME2 decompilation as the cross-check (decomp-progress/bfme2
// Code/GameEngine/Source/GameLogic/Object/Update/ThreatFinderUpdate*.cpp, Rva003ECA69ElementClear.cpp, Rva003ECA81Add.cpp,
// Object/ThreatFinderManager.cpp). Every statement below either names a RotWK instruction (RW) or is marked INFERENCE with the BFME2 address it
// comes from. ZH has no such class.
//
// ThreatFinderUpdate (PORT 17, issue #73, stop S-1424): a map object that carries it becomes a NAMED threat area. When the map's objects are
// placed (the properties of the map object are applied, then RW 0x694692 reaches every module's slot 45) the module makes a finder named by the
// object's own name (Object + 0x88), with the radius the map property `objectThreatFinderRadius` names or the data's DefaultRadius, and registers
// it in TheThreatFinderManager (RW 0xDE8888) by that name. Every frame it copies the owner's position into the finder. Scripts ask the area's
// threat by name: SET_COUNTER_TO_THREAT_FINDER_THREAT (action 556) sums, for one player and one relationship ("Enemies" / "Allies"), the threat
// of the objects standing in the radius (GameLogic/ScriptEngine/ScriptActionsThreatFinder.cpp).
//
// TARGET FACTS (RotWK game.dat; registry golden engine/data/rotwk-201/module-registry.json: create RW 0x64F96A, createData RW 0x64F9A2, vtable
// RW 0xC07A30, registration site RW 0x65A133, field table RW 0xC4C320; field-tables.json: one row DefaultRadius, parseReal RW 0x42ED00, + 0x08):
//   * data: DefaultRadius + 0x08 (the module data object is 0x0C bytes: vptr + 4 + the real; the factory's ctor RW 0x3ECC91 zeroes the real, a
//     shared fold with RadarUpdateModuleData RW 0x3ECC80 - INFERENCE (BFME2 twin 0x0024FC08 / 0x003ECC80 / 0x003ECC91)).
//   * module: create RW 0x64F96A, module ctor RW 0x7EDD3D (UpdateModule + the heap finder + 0x20 = null, INFERENCE (BFME2 0x003ECCA2)), dtor
//     RW 0x7EDF5B (the heap finder goes: it unregisters itself first).
//   * slot 45 of the behaviour interface, mapLoadPostProcess RW 0x7EDECE (INFERENCE BFME2 0x003ECE33, 162 bytes): with the map object's
//     properties present: a new finder named Object + 0x88, registered in the manager (BFME2 0x003ECDB7: only a name that is not the empty
//     string), its radius = properties->getReal("objectThreatFinderRadius") when the key exists there, else the data's DefaultRadius (+
//     0x08). With a null properties argument (a structure built on a plot: RW 0x858A94) nothing happens: no finder, no name.
//   * update RW 0x7EDDB6 (the update interface's slot 0; INFERENCE BFME2 0x003ECD1B): the owner's position (Object + 0x38) is copied into the
//     finder + 0x554 when the finder exists; the answer is 1 (UPDATE_SLEEP_NONE: every frame).
//   * the finder is retail's ONE finder class (the object RW 0x7EDDFB makes, 0x5BC bytes: + 0x000 twenty records of 0x48 bytes - the total and 17
//     categories -, + 0x5A0 the name, + 0x5A4 the position, + 0x5B0 the radius, + 0x5B4 the frame of the last scan, + 0x5B8 "no structures", 0 for a
//     map area, which counts the structures in it). The port's finder is GameLogic/System/ThreatFinder.h on lane AI-3's already ported mechanism
//     (AIThreatFinder: the same class, its scan RW 0x7EE057 and its sum RW 0x7EE166). The BFME2 decompilation #73 cites has the same class one
//     release older (0x56C bytes, 17 floats to a record, the name at + 0x550): its shape is the cross-check, its offsets are not used.
//   * the answer RW 0x7EE260 / getThreatForPlayer RW 0x7EE1F4 (INFERENCE BFME2 0x003ED0C4): when it is at least 3 seconds since the last scan
//     ((frame - last) * LOGICFRAMES_PER_SECOND >= 3.0, RW 0xBDD42C) the records are rescanned and the frame stored; then, for the searching player,
//     the element-wise sum of the records of every player whose relationship to it is ENEMIES (0) or ALLIES (1) - retail's own relationship test
//     (the port's Player::getRelationship, RW 0x6ACEAF, as the skirmish finder uses it); the sum's first float is the area's threat.
//   * the scan is RW 0x7EE057, lane AI-3's (AIThreatFinder::scan): the records cleared, then ThePartitionManager->iterateObjectsInRange(position,
//     radius, FROM_CENTER_2D, the alive filter RW 0xC10E20, ITER_FASTEST); an object whose template is any of CAN_ATTACK, HERO, SUPPORT and - when
//     + 0x5B8 is 0 - STRUCTURE (never STRUCTURE when + 0x5B8 is 1) adds its threat value (RW 0x68F0EC) to the record of its controlling player's
//     index when that value is above 0: the record's total (+ 0) and its category slot (RW 0x7EDB72; the category is the template's ThreatBreakdown
//     AIKindOf, read with AIThreatFinder::threatCategory, RW 0x530, -1 - the total again - without a block). A map area's + 0x5B8 is 0, so a
//     structure in it counts and a structure that cannot attack counts 0 (RW 0x68F0EC).
//   * xfer RW 0x7EDF70 (docs/SAVES-RETAIL.md; INFERENCE BFME2 0x003ECED5, 143 bytes): version (1, 1), UpdateModule::xfer, then the finder: on a
//     LOAD the finder is made again (named Object + 0x88) and registered, and the finder's own xfer follows (INFERENCE BFME2 0x003ECBF8,
//     version (1, 1), the twenty records, the name, the position (3 floats), the radius, the last-scan frame, the flag).
// NOT PORTED (stop S-7184, reported by stopLines): the condition that reads the same finders (RW 0x608BCA, out of this issue's scope:
// ScriptConditions.cpp belongs to PORT 16). Until it is ported a map script can set a counter from a named area but cannot test it in a condition.
// INFERENCE (OpenBFME): the snapshot is OpenBFME native (S-4190) in RW's field order, and a record is written as its 18 floats in the order the class
// holds them. The finder's own state rides in its module's block (an object's xfer, RW 0x697F28), so the manager is not a state-hash section: it holds
// no state of its own beyond the name -> finder map, which is rebuilt from the objects' names on a load.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/System/ThreatFinder.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class GameLogic;
class ModuleFactory;
class Object;
class StateHasher;
class Xfer;
class Dict;

class ThreatFinderUpdateModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
	float m_radius = 0.0f; ///< + 0x08 DefaultRadius
};

class ThreatFinderUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(ThreatFinderUpdate)
	ThreatFinderUpdate(Thing *thing, const ThreatFinderUpdateModuleData *data); ///< RW 0x7EDD3D
	~ThreatFinderUpdate() override;                                              ///< RW 0x7EDF5B (the finder leaves the manager)
	UpdateSleepTime update() override;                                           ///< RW 0x7EDDB6
	// slot 45 of the behaviour interface: RW 0x7EDECE (Object::friend_onMapPlacement, RW 0x694692)
	void onMapPlacement(const Dict *properties) override;
	void crc(StateHasher &h) const override;

	const ThreatFinderUpdateModuleData *data() const { return m_data; }
	ThreatFinder *finder() const { return m_finder.get(); }

	static void registerModule(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

private:
	// INFERENCE BFME2 0x003ECE33: a finder for the owner's name with the property's radius or the data's
	void makeFinder(const Dict *properties);

	const ThreatFinderUpdateModuleData *m_data;
	std::unique_ptr<ThreatFinder> m_finder; ///< + 0x20
};
