// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// HordeDispatchSpecialPower (lane FACT-C): a horde's special power that passes the order on to its members (the goblin warriors' Poisoned Blades: the horde
// carries the button and this module, each member a SpecialPowerModule and the WeaponModeSpecialPowerUpdate that does the work). A BFME class; ZH has none.
// Open-BFME-2 matches only its constructor (SupplyTruckAIUpdateCtor.cpp, BFME2 0x4C89E6: SpecialPowerModule base, no member of its own); the do* bodies are
// read from the RotWK binary (caveat S-001).
//
// TARGET FACTS:
//   * create RW 0x6525E2 (size 0x34), constructor RW 0x8CCE0D over SpecialPowerModule's RW 0x8973EE: module vtable RW 0xC754C0, behaviour interface RW
//     0xC06050, special power interface RW 0xC75458; data: the SpecialPowerModuleData table RW 0xC64DB0 and the empty RW 0xC84858 (no field of its own).
//   * the special power interface is SpecialPowerModule's except its three do* slots. Each first requires the object to be a HORDE (template + 0x115 bit 5,
//     KindOf 109) with a contain (RW 0x68C866), copies the contain's list (contain vslot 0x10C) and looks every member's module of the same template up
//     (Object::getSpecialPowerModule RW 0x68C26D):
//     - doSpecialPower (0x28, RW 0x8CD08C): SpecialPowerModule::doSpecialPower (RW 0x8980A3) on the horde, then each member module's doSpecialPower(options);
//     - doSpecialPowerAtLocation (0x30, RW 0x8CCEC9): SpecialPowerModule::doSpecialPowerAtLocation (RW 0x89816C), then each member's at the location;
//     - doSpecialPowerAtObject (0x2C, RW 0x8CCF76): each member's doSpecialPowerAtObject(target, options) first; the target the member module kept (interface
//       slot 0x50, RW 0x7B0F25: module + 0x2C, the target id initiateIntentToDoSpecialPower stores) found again (RW 0x449681) replaces the horde's target
//       when it is farther from the horde (squared distance RW 0x5E3BE4, x87 at PC24) than the best so far (start: the order's target); then
//       SpecialPowerModule::doSpecialPowerAtObject (RW 0x8980EF) on the horde with that target.
// INFERENCE: the port's SpecialPowerModule keeps no + 0x2C target id; a member's kept target is the order's target unless the member's initiate swaps it
// (adjustVictim, BFME2 decomp SpecialPowerModuleDo.cpp:157, not ported by the base either), so the horde's base call takes the order's target.

#pragma once

#include "GameLogic/Module/SpecialPowerModules.h"

class ModuleFactory;

class HordeDispatchSpecialPower : public SpecialPowerModule
{
public:
	HordeDispatchSpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {} // RW 0x8CCE0D
	void doSpecialPower(unsigned options) override;                               // RW 0x8CD08C
	void doSpecialPowerAtObject(Object *target, unsigned options) override;       // RW 0x8CCF76
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8CCEC9
	unsigned long long dispatched() const { return m_dispatched; }               // member module calls (a test counter, hashed)
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> acceptanceStops(); // [S-2001]

private:
	// the members' modules of this power (empty when the object is no HORDE or has no contain)
	std::vector<SpecialPowerModuleInterface *> memberModules() const;
	bool isHordeWithContain() const;
	unsigned long long m_dispatched = 0;
};
