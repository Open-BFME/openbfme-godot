// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// WeaponStatusHelper: the helper module that keeps an object's weapon model conditions (PREATTACK_x / FIRING_x / BETWEEN_FIRING_SHOTS_x / RELOADING_x and the
// combined bits) in step with its weapons (ZH Source/GameLogic/Object/Helper/ObjectWeaponStatusHelper.cpp), lane PROJ-2. It replaces the LOGIC-1 shell of the
// same name (GameLogic/Module/ObjectHelper.h, stop S-141) and the COMBAT-1 stand-in that ran the same update at the start of every AI update.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * made by the Object constructor after the GuardingHelper when the template can possibly have a weapon (RW 0x68D19A, 0x20 bytes, vtables 0xC11F24 /
//     0xC67300 / 0xC11F18); the constructor wakes it at once: RW 0x68D1D5 -> 0x850C32 setWakeFrame(object, now + 1);
//   * update (the update interface's slot 0, RW 0x690053: `mov ecx, [ecx - 8]; call 0x68E197`) is Object::adjustModelConditionForWeaponStatus (RW 0x68E197,
//     ported as ObjectWeapons::updateWeaponStatusConditions) and returns 1 (UPDATE_SLEEP_NONE): it runs every frame;
//   * getUpdatePhase (main vtable slot 0x30, RW 0x8311B1) returns 3, PHASE_FINAL: the scheduler files it in the last update vector, run after the normal
//     modules of every object (GameLogic::runModules, logic phase 6). So in the frame of a shot the AI fires FIRST, with the flags the helper set at the end of
//     the frame before (PREATTACK_x and FIRING_OR_PREATTACK_x): the launch bone of the shot is the attack state's pose (FrameForPristineBonePositions,
//     RW 0x6756A1 -> 0x4C34A2 reads the drawable's flags, which follow the object's at once, RW 0x68D607 -> 0x68B53C). Then the helper sees the shot of this
//     frame (RW 0x68E1E7: lastFireFrame == now gives FIRING_x) and the attack animation state is kept, never interrupted by a BETWEEN_FIRING_SHOTS frame;
//   * getDisabledTypesToProcess (the update interface's slot 1, RW 0x8E3C58 -> 0x850F76(mask, 3, 8)): DISABLED_HELD (3) and DISABLED type 8
//     (TEMPORARILY_BUSY): it keeps running while the object is held or busy.
// DONOR: ZH ObjectWeaponStatusHelper (update = adjustModelConditionForWeaponStatus; ZH sleeps it, RotWK runs it every frame).
// INFERENCE / not ported: RW 0x68E197 changes the conditions only when the object has a drawable (+0x84) and also tells the drawable the clip
// (RW 0x671913, a client-side barrel / recoil hint); this port changes them for every object (every retail object that can have a weapon has a Draw
// module, so it has a drawable) and the drawable hint is not ported. Not ported: xfer / save state.

#pragma once

#include "GameLogic/Module/ObjectHelper.h"

// derives from the LOGIC-1 shell so GameLogic::report() still lists it by name (helperShells); unlike the shell it acts (isUnported() is false)
class WeaponStatusHelper : public ObjectHelperShell
{
public:
	explicit WeaponStatusHelper(Thing *thing);

	bool isUnported() const override { return false; }
	UpdateSleepTime update() override;
	SleepyUpdatePhase getUpdatePhase() const override { return PHASE_FINAL; } // RW 0x8311B1
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)((1u << 3) | (1u << 8)); } // RW 0x8E3C58
};
