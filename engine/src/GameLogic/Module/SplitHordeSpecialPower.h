// OpenBFME. GPL-3.0. MODGAP-2 (finished by lane MODGAP-1): a mixed horde splits into the hordes its SplitHorde rows name.
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 0x8C861E, HordeContainRva00475F61.cpp names the shape): create RW 0x652444, 0x34 bytes, constructor RW 0x8CC99B over SpecialPowerModule RW 0x8973EE;
// only SpecialPowerModule's table (RW 0xC64DB0).
//   * doSpecialPower RW 0x8CCA45: a disabled object (RW 0x9325B4) does nothing; its contain's slot 0x1F4 (RW 0x86CA3D, HordeContain::canSplit) allows, then slot 0x1F8 (RW 0x8791C5, splitHorde).
// The location and object forms are SpecialPowerModule's: a targetless power (stop S-6004 lists what the split leaves out).
// Local 171: RW 0x65247C (SplitHordeSpecialPower newData)
#pragma once

#include "GameLogic/Module/SpecialPowerModules.h"

class ModuleFactory;

// RW 0x008CC9CB (~SplitHordeSpecialPower)
class SplitHordeSpecialPower : public SpecialPowerModule
{
public:
	OBFME_MODULE_XFER(SplitHordeSpecialPower)
	SplitHordeSpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {}
	void doSpecialPower(unsigned options) override; // RW 0x8CCA45
	static void registerClass(ModuleFactory &modules);
};
