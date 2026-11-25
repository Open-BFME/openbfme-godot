// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001). ZH has no such class.
//
// SpecialEnemySenseUpdate: an object shows with the model condition SPECIAL_ENEMY_NEAR that an enemy its filter names is near (Frodo's Sting near orcs).
//
// TARGET FACTS (RotWK game.dat; registration site RW 0x658E8B, table RW 0xC07E98):
//   * data: createData RW 0x653E58 (0x14 bytes), constructor RW 0x653E06 (vtable RW 0xC07DE8): SpecialEnemyFilter + 0x08 = the default filter (RW 0x76406F),
//     ScanRange + 0x0C = 0.0, ScanInterval + 0x10 = 1 frame. Table: ParseObjectFilter RW 0x76392F, parseReal RW 0x42ED00, parseDurationUnsignedInt RW 0x73A429.
//   * module: create RW 0x64E165 (0x20 bytes: no field of its own), constructor RW 0x89D758 (UpdateModule RW 0x653114; vtables RW 0xC66C10 / 0xC67300 /
//     0xC66C04): the first update on the next frame (RW 0x850C32(1)).
//   * update RW 0x89D7E6: the closest object (RW 0xA39090, distance type 0, the object's own position) within ScanRange that is not effectively dead (RW
//     0xC10E20), that SpecialEnemyFilter allows for the object's controlling player (RW 0xBE4CC8, flag 1) and that is an enemy (RW 0xC11DC0(object, mask 1,
//     0): the object's relationship to it, RW 0x68D7AB). With one, SPECIAL_ENEMY_NEAR (model condition 0x102: Object + 0x12C bit 2) is set on the object, or,
//     when its contain has the horde interface (slot 0x7C), on each object of the horde's member list (slot 0x108) and not on the horde; without one it is
//     cleared in the same way. Only a change is told the drawable (RW 0x68B53C). It returns ScanInterval.
//   * xfer RW 0x89D7C5: UpdateModule (RW 0x850C60), then version 1.
//   * BFME2 1.06: the same update, instruction for instruction (0x89A037; only the contain's Object offset, 0x250, differs).
// WHAT IS INFERENCE (OpenBFME): the snapshot is OpenBFME native (S-4190).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

class ModuleFactory;

class SpecialEnemySenseUpdateModuleData : public ModuleData
{
public:
	ObjectFilter m_specialEnemyFilter; ///< + 0x08 (RW 0x76406F)
	float m_scanRange = 0.0f;          ///< + 0x0C
	unsigned m_scanInterval = 1;       ///< + 0x10 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpecialEnemySenseUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(SpecialEnemySenseUpdate)
	SpecialEnemySenseUpdate(Thing *thing, const SpecialEnemySenseUpdateModuleData *data); ///< RW 0x89D758
	UpdateSleepTime update() override;                                                   ///< RW 0x89D7E6

	const SpecialEnemySenseUpdateModuleData *data() const { return m_data; }

	static void registerModule(ModuleFactory &modules);

private:
	const SpecialEnemySenseUpdateModuleData *m_data;
};
