// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (OCLUpdate).
//
// OCLUpdate (lane MODGAP-1, group 4): creates an ObjectCreationList at its object every MinDelay .. MaxDelay, Amount times (the wall trebuchet and catapult makers).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp OCLUpdate.cpp names the layout):
//   * data (ctor RW 0x89EB1C, vtable RW 0xC07A30): OCL (+8), MinDelay (+0xC) and MaxDelay (+0x10) in frames, CreateAtEdge byte (+0x18), Amount (+0x14, default -1:
//     no limit); the field names are BFME2's table (RW 0xC50BA8 there: OCL, MinDelay, MaxDelay, CreateAtEdge, Amount);
//   * module (ctor RW 0x89EB48): UpdateModule, + 0x20 the next creation frame, + 0x24 the frame it was scheduled at, + 0x28 the count made (all 0);
//   * update RW 0x89EC43: not yet due (the logic frame below + 0x20, RW 0x89EB99): stay awake. Due: with + 0x20 still 0 (the first call) only the next frame is
//     scheduled; else it is scheduled, the OCL (when it has one) is created at the object (primary the object, position its position, secondary its position,
//     no lifetime: RW 0x5F00CA), and with Amount > 0 the count grows and reaching Amount puts the module to sleep for ever (RW 0x89ECC5 .. 0x89ECD0);
//   * the schedule RW 0x89EBBA: + 0x24 = the frame, + 0x20 = the frame + GameLogicRandomValue(MinDelay, MaxDelay) ("OCLUpdate.cpp" line 0x7B: a draw even when
//     both are equal);
//   * xfer RW 0x89EC00: version 2, UpdateModule, + 0x20, + 0x24, and from version 2 + 0x28.
// NOT PORTED: none (PORT-237 closed stop S-5501: CreateAtEdge uses TerrainLogic::findClosestEdgePoint RW 0x67D949 / 0x89EC81, and getCountdownPercent RW 0x89EA88 is ported).
// Local 171: RW 0x64E283 (OCLUpdate newInstance), RW 0x64E2BB (OCLUpdate newData)

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;
class StateHasher;

class OCLUpdateModuleData : public ModuleData
{
public:
	std::string m_ocl;          // +8
	unsigned m_minDelay = 0;    // +0xC (frames)
	unsigned m_maxDelay = 0;    // +0x10 (frames)
	int m_amount = -1;          // +0x14
	bool m_createAtEdge = false; // +0x18
	static void buildFieldParse(MultiIniFieldParse &p);
};

class OCLUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(OCLUpdate)
	OCLUpdate(Thing *thing, const OCLUpdateModuleData *data); // RW 0x89EB48
	~OCLUpdate() override = default; // RW 0x0089EA69 / RW 0x0089EB7D
	UpdateSleepTime update() override;                        // RW 0x89EC43
	void crc(StateHasher &h) const override;

	unsigned nextFrame() const { return m_nextFrame; }
	unsigned scheduledAt() const { return m_scheduledAt; }
	int created() const { return m_created; }
	float getCountdownPercent() const; // RW 0x89EA88
	static void registerClass(ModuleFactory &modules); // getModuleName RW 0x0089EA82, getModuleNameKey RW 0x0089EAD7
	static const char *stopLine();

private:
	void schedule(); // RW 0x89EBBA

	const OCLUpdateModuleData *m_data;
	unsigned m_nextFrame = 0;    // + 0x20
	unsigned m_scheduledAt = 0;  // + 0x24
	int m_created = 0;           // + 0x28
};
