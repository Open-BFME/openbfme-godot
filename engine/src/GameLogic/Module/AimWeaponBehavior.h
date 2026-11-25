// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001). ZH has no such class; the BFME2 decomp has its constructor and module data only.
//
// AimWeaponBehavior: a unit with an aiming weapon (Weapon IsAimingWeapon) shows where it aims with the model conditions AIM_HIGH / AIM_STRAIGHT / AIM_LOW
// (the slope to the target) and AIM_NEAR / AIM_FAR (the distance to it): archers, the giants.
//
// TARGET FACTS (RotWK game.dat; registration site RW 0x657DCA, table RW 0xC58178):
//   * data: createData RW 0x64ADE4 (0x18 bytes), constructor RW 0x85E251: AimLowThreshold + 0x08 = -0.05 (RW 0xC29938), AimHighThreshold + 0x0C = 0.05
//     (RW 0xBDD760), AimNearDistance + 0x10 = 0.0, AimFarDistance + 0x14 = FLT_MAX (RW 0xBD1910); all four parseReal (RW 0x42ED00).
//   * module: create RW 0x64ADAC (0x24 bytes), constructor RW 0x85E29A (UpdateModule RW 0x653114; vtables RW 0xC580FC / 0xC67300 / 0xC580F0): the aim
//     flag + 0x20 = false; wake next frame (RW 0x85E2D9).
//   * update RW 0x85E30B: without an AI (Object + 0x260), a drawable (RW 0x70E013) or a current weapon (RW 0x68B58C(0)) -> forever. Else, every frame:
//       1. "attacking" = the AI's state is active (AI vslot 0x1BC) or the frame is in the weapon's [pre-attack end, follow-through end) (RW 0x6CA2C3).
//       2. attacking with an aiming weapon (template + 0x156): from the object's position to the target its last pre-fire recorded (Object + 0x3A8, RW
//          0x692174) the 2D distance (x87, at least 10.0, RW 0xBD83D8) and the slope dz / distance. slope > AimHighThreshold: AIM_HIGH (word 5 bit 18);
//          AimLowThreshold > slope: AIM_LOW (bit 20); else AIM_STRAIGHT (bit 19). The bit is added when it is not set: the other two STAY (RW 0x85E43D ..
//          0x85E44D). AimNearDistance > distance: AIM_FAR cleared and AIM_NEAR set (RW 0x68D607); else distance > AimFarDistance: the other way round; else
//          neither changes. The aim flag is set.
//       3. the aim flag set and not attacking: the flag goes and all five conditions are cleared (RW 0x85E4C1 .. 0x85E4E7).
//       4. after 2 or 3 the drawable's changed flags are flushed (RW 0x67449C(0)).
//   * xfer RW 0x85E1C7: version 1, UpdateModule (RW 0x850C60), then (Xfer slot 0x10 false) the aim flag.
//   * BFME2 1.06: the same update, instruction for instruction (0x85B297; only the Object offsets differ: the AI + 0x258, the recorded target + 0x38C).
// WHAT IS INFERENCE (OpenBFME): the drawable exists for every object (RW 0x628882 makes one for each), so the "no drawable" exit never applies; the snapshot
// is OpenBFME native (S-4190): RW's field in RW's order; the state hash also takes the recorded target, which no other logic reads.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;
class StateHasher;

class AimWeaponBehaviorModuleData : public ModuleData
{
public:
	float m_aimLowThreshold = -0.05f; ///< + 0x08
	float m_aimHighThreshold = 0.05f; ///< + 0x0C
	float m_aimNearDistance = 0.0f;   ///< + 0x10
	float m_aimFarDistance;           ///< + 0x14 (FLT_MAX)
	AimWeaponBehaviorModuleData();    ///< RW 0x85E251
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AimWeaponBehavior : public UpdateModule
{
public:
	OBFME_MODULE_XFER(AimWeaponBehavior)
	AimWeaponBehavior(Thing *thing, const AimWeaponBehaviorModuleData *data); ///< RW 0x85E29A
	UpdateSleepTime update() override;                                        ///< RW 0x85E30B
	void crc(StateHasher &h) const override;

	const AimWeaponBehaviorModuleData *data() const { return m_data; }
	bool isAiming() const { return m_aiming; } ///< + 0x20

	static void registerModule(ModuleFactory &modules);

private:
	const AimWeaponBehaviorModuleData *m_data;
	bool m_aiming = false; ///< + 0x20
};
