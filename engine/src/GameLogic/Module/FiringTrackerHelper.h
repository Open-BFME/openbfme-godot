// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// FiringTrackerHelper: the helper module that runs an object's FiringTracker (ZH Source/GameLogic/Object/FiringTracker.cpp), lane FIRINGTRACKER-1. It replaces
// the LOGIC-1 shell of the same name (GameLogic/Module/ObjectHelper.h, stop S-141) and the COMBAT-1 stand-in that ran the tracker's update at the start of
// every AI update.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; both editions, DONOR BFME2 decomp Code/GameEngine/Source/GameLogic/Object/FiringTrackerCtor.cpp /
// FiringTrackerXfer.cpp, tier B same-shape):
//   * made by the Object constructor after the WeaponStatusHelper when the template can possibly have a weapon (RW 0x8E2EB2, 0x5C bytes, vtables 0xC781F0 /
//     0xC67300 / 0xC781E4); the constructor sleeps it: RW 0x8E2F31 -> 0x850C32 setWakeFrame(object, 0x3FFFFFFF);
//   * update (the update interface's slot 0, RW 0x8E30DD, ported as FiringTracker::update) returns its own sleep; FiringTracker::shotFired (RW 0x8E326D) wakes
//     it after every shot that is not a forced reset (RW 0x8E34E4 -> 0x850C32(object, calcTimeToSleep RW 0x8E2E52));
//   * getUpdatePhase (main vtable slot 0x30, RW 0x8311B1) returns 3, PHASE_FINAL (as the WeaponStatusHelper; tools/rw_object_model/update_phases.py);
//   * getDisabledTypesToProcess (the update interface's slot 1, RW 0x8B3313) copies the all-types mask RW 0xDE8B90: it runs whatever disables the object.
// The tracker's state is ObjectWeapons' member (ObjectWeapons::firingTracker, saved by ObjectWeapons::xfer); this module keeps the scheduler state only.

#pragma once

#include "GameLogic/Module/ObjectHelper.h"

// derives from the LOGIC-1 shell so GameLogic::report() still lists it by name (helperShells); unlike the shell it acts (isUnported() is false)
class FiringTrackerHelper : public ObjectHelperShell
{
public:
	explicit FiringTrackerHelper(Thing *thing);

	bool isUnported() const override { return false; }
	UpdateSleepTime update() override;
	SleepyUpdatePhase getUpdatePhase() const override { return PHASE_FINAL; } // RW 0x8311B1
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)0xFFFFFFFFu; } // RW 0x8B3313: RW 0xDE8B90, all types
	bool isFiringTrackerHelper() const override { return true; }
	// RW 0x8E34E4 -> 0x850C32: shotFired's wake (the module's own setWakeFrame, protected in UpdateModule)
	void wakeAfterShot(UpdateSleepTime sleep) { setWakeFrame(getObject(), sleep); }
	OBFME_MODULE_XFER(FiringTrackerHelper) // the shell's (the tracker's state is ObjectWeapons')
};
