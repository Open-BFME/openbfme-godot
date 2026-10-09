// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// RepairSpecialPower (lane MOD-4, QA-1 U2): the builders' repair power (the Rohan peasants' SpecialRepairStructure). A BFME class; ZH has none. Open-BFME-2
// names it (RepairSpecialPowerAtObject.cpp, not byte-matched here); ported from the RotWK binary (caveat S-001).
//
// TARGET FACTS (each read from the disassembly):
//   * create RW 0x6524CD (size 0x34), constructor RW 0x8CCADB over SpecialPowerModule's RW 0x8973EE: module vtable RW 0xC75358, behaviour interface RW
//     0xC06050, special power interface RW 0xC752F0; data: the SpecialPowerModuleData table RW 0xC64DB0 and an empty table RW 0xC84858 (no field of its own).
//   * the special power interface is SpecialPowerModule's (RW 0xC64FF0) except its three do* slots: doSpecialPower (0x28) and doSpecialPowerAtLocation (0x30)
//     do nothing (RW 0x9F3A3C / 0x8851E4: `ret`), doSpecialPowerAtObject (0x2C, RW 0x8CCB85): when the module's object is a DOZER (template + 0x109 bit 6 =
//     KindOf 14), the target is a STRUCTURE (template + 0x108 bit 7) and the object has an AI (+ 0x260): the target found again by its id (RW 0x449681) is
//     given to the AI's aiRepair (RW 0x7714C1, AICommandInterface at AI + 0x20) with source 0 (CMD_FROM_PLAYER): AI command 0x13, which DozerAIUpdate's
//     aiDoCommand runs as its repair (RW 0x88BD8D). SpecialPowerModule's base do* (the recharge, the initiate, the trigger) is NOT called.
// INFERENCE: the command passes the AI's command gate (RW 0x667174) first, as every aiDoCommand does here (AIUpdateInterface::acceptCommand).

#pragma once

#include "GameLogic/Module/SpecialPowerModules.h"

class ModuleFactory;

class RepairSpecialPower : public SpecialPowerModule
{
public:
	RepairSpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {} // RW 0x8CCADB
	void doSpecialPower(unsigned) override {}                                     // RW 0x9F3A3C
	void doSpecialPowerAtLocation(const Coord3D &, unsigned) override {}          // RW 0x8851E4
	void doSpecialPowerAtObject(Object *target, unsigned options) override;       // RW 0x8CCB85
	unsigned long long repairs() const { return m_repairs; }                      // the dozer accepted the order (a test counter)
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &modules);

private:
	unsigned long long m_repairs = 0;
};
