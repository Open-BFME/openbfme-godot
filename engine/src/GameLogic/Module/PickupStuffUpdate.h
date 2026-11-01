// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001); BFME2 decomp cross-check (decomp-progress/bfme2 Code/GameEngine/Source/GameLogic/Object/Update/
// PickupStuffUpdate*.cpp, Common/PickupStuffUpdate.cpp (xfer and update helpers): the same fields and steps, the AI at Object + 0x258 there).
// ZH has no such class.
//
// PickupStuffUpdate (lane MODCOMPAT-MOD1, census rank 3: every horde of the base game carries it): a skirmish AI's unit looks for crates around it every
// ScanIntervalSeconds and walks to the nearest one in a temporary AI state machine; once its AI is idle it goes back to what it did before.
//
// TARGET FACTS (RotWK game.dat; registry golden engine/data/rotwk-201/module-registry.json: registration site RW 0x659981, table RW 0xC64600):
//   * data: createData RW 0x64D634 (0x18 bytes), constructor RW 0x8954C3 (vtable RW 0xC64550): SkirmishAIOnly + 0x08 = true, ScanRange + 0x0C = 200.0
//     (RW 0xBE4170), StuffToPickUp + 0x10 (an ObjectFilter, RW 0x76406F), ScanIntervalSeconds + 0x14 = 0.5 (RW 0xBD869C). Table RW 0xC64600: SkirmishAIOnly
//     (parseBool RW 0x42E558), ScanRange (parseReal RW 0x42ED00), StuffToPickUp (ParseObjectFilter RW 0x76392F), ScanIntervalSeconds (parseReal: seconds
//     as a real, not a duration). The retail data: 113 templates, all `SkirmishAIOnly = Yes, StuffToPickUp = NONE +CRATE, ScanRange = 200,
//     ScanIntervalSeconds = 0.5`.
//   * module: create RW 0x64D5FC (0x28 bytes), constructor RW 0x895524 (UpdateModule RW 0x653114; vtables RW 0xC64518 / 0xC67300 / 0xC6450C): the
//     picking-up flag + 0x20 = false, the last scan frame + 0x24 = 0; no wake call (the update runs from the next frame).
//   * update RW 0x8957B7 (the update interface's slot 0): no AI (Object + 0x260) -> sleep forever; SkirmishAIOnly and the controlling player (RW 0x68B678)
//     without a skirmish AI (TheSkirmishAIManager RW 0xDE4938, RW 0x6A950B: a null player has none) -> sleep forever; else the picking-up step (flag set)
//     or the scan step (flag clear); 1 (every frame).
//   * the scan step RW 0x895741: when the frame (TheGameLogic + 0x40) > the last scan frame + LOGICFRAMES_PER_SECOND (RW 0xD9F608) * ScanIntervalSeconds
//     (x87: FILD of the two unsigned frames (with the 2^32 fix-up RW 0xBD8698 for a negative int), FILD / FMUL of the rate, FADDP, FCOMIP: compared in
//     extended precision), then: only while the AI's temporary machine slot (AI + 0x34) is empty, the scan RW 0x895632; a hit is given to the AI by
//     RW 0x895715 (beginTemporaryStateMachine RW 0x6630A9, then aiMoveToObject(hit, CMD_FROM_AI) RW 0x7549D7: AICommandParms (1, 2) RW 0x7536DD through
//     aiDoCommand) and the flag set; either way the last scan frame = the frame.
//   * the scan RW 0x895632: ThePartitionManager (RW 0xDE4354) iterateObjectsInRange RW 0xA39340(the object's position + 0x38, ScanRange, distance type 0
//     FROM_CENTER_2D, the filters, order 1 near to far), the first hit (RW 0x444F19). The filters: RW 0xC10E20 (alive: Object + 0x458 bit 0 clear), RW
//     0xBE4CC8 (StuffToPickUp allows the hit for the object's controlling player, match flag 1: RW 0x66122D -> RW 0x7640C1) and RW 0xC2E0B0 (built by
//     RW 0x76A28D: the hit's object status (Object + 0x94) has none of the "must be clear" mask, here status 0x62 ATTACHED (dword 3 bit 2, set at RW
//     0x895661), and every bit of the empty "must be set" mask: RW 0x66146A -> RW 0x661317). (lane MOD-4's stop text said THROWN_OBJECT: that is KindOf
//     0x62, the mask is the object status one.)
//   * the picking-up step RW 0x895572: when the AI is idle (AI vslot 0x1B8): a kept machine (AI + 0x34) is given back (endTemporaryStateMachine RW
//     0x6630D7), the flag cleared.
//   * xfer RW 0x8955A4: UpdateModule (RW 0x850C60), version (1, 1), the flag (+ 0x20, bool), the last scan frame (+ 0x24, unsigned).
// The AI's temporary machine (RW 0x6630A9 / 0x6630D7, the slot AI + 0x34, the in-update byte AI + 0x3E0) is AIUpdateInterface's (AIUpdate.h); any order
// that is not the AI's own ends it (aiDoCommand RW 0x66A91A), as do a defection (RW 0x6993A4 / 0x69AC55) and a fling (RW 0x792E0E).
// WHAT IS INFERENCE (OpenBFME):
//   * the scan's frame compare is made as (frame - last) > LOGICFRAMES_PER_SECOND * rate exactly, in integers from the float's bits (no float arithmetic in
//     the simulation): the difference of two 32-bit frames and the product of an int below 2^24 and a float are both exact, so this is the exact compare, which RW's 80-bit sum and compare also give for
//     every rate whose product needs no more than the extended mantissa (any rate above 2^-24);
//   * the snapshot is OpenBFME native (S-4190): RW's fields in RW's order.
// NOT PORTED (stop S-1422): SpecialAbilityUpdate's use of the same temporary machine (its initiate RW 0x852FB1 begins one, its finish RW 0x853105 gives it
// back: S-859), so a skirmish AI's hero ability does not end a pick-up's machine.
// Both SalvageCrateCollide and VeterancyCrateCollide are recognized by the existing
// collide-interface guard and shared AIWorld crate contacts. Genuinely unported
// crate classes remain refused; pilot veterancy collection remains S-5901.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <string>
#include <vector>

class ModuleFactory;
class Object;
class StateHasher;

class PickupStuffUpdateModuleData : public ModuleData
{
public:
	bool m_skirmishAIOnly = true;        ///< + 0x08
	float m_scanRange = 200.0f;          ///< + 0x0C
	ObjectFilter m_stuffToPickUp;        ///< + 0x10
	float m_scanIntervalSeconds = 0.5f;  ///< + 0x14
	static void buildFieldParse(MultiIniFieldParse &p);
};

class PickupStuffUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(PickupStuffUpdate)
	PickupStuffUpdate(Thing *thing, const PickupStuffUpdateModuleData *data); ///< RW 0x895524
	UpdateSleepTime update() override;                                        ///< RW 0x8957B7
	void crc(StateHasher &h) const override;

	const PickupStuffUpdateModuleData *data() const { return m_data; }
	bool pickingUp() const { return m_pickingUp; }               ///< + 0x20
	unsigned lastScanFrame() const { return m_lastScanFrame; }   ///< + 0x24
	// observation only (tests; not state, not hashed): the scans run and the pick-ups begun
	unsigned long long scans() const { return m_scans; }
	unsigned long long pickups() const { return m_pickups; }
	unsigned long long blocked() const { return m_blocked; } ///< the scans whose stuff was a crate without a crate collide runtime
	// lane MODCOMPAT-MOD1: false when `stuff` lists a crate collide this port does not run (a genuinely unported crate class)
	static bool crateCollideRuns(const Object &stuff);

	static void registerModule(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

private:
	void scanStep();        ///< RW 0x895741
	Object *findStuff();    ///< RW 0x895632
	void pickingUpStep();   ///< RW 0x895572

	const PickupStuffUpdateModuleData *m_data;
	bool m_pickingUp = false;      ///< + 0x20
	unsigned m_lastScanFrame = 0;  ///< + 0x24
	unsigned long long m_scans = 0;
	unsigned long long m_pickups = 0;
	unsigned long long m_blocked = 0;
};
