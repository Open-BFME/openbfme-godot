// OpenBFME. GPL-3.0.
// See GameLogic/Module/FiringTrackerHelper.h.

#include "GameLogic/Module/FiringTrackerHelper.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Object.h"

// RW 0x8E2EB2: the shell's constructor already sleeps it forever (RW 0x8E2F31 -> 0x850C32(object, 0x3FFFFFFF))
FiringTrackerHelper::FiringTrackerHelper(Thing *thing)
	: ObjectHelperShell(thing, "FiringTrackerHelper", "ModuleTag_FiringTrackerHelper")
{
}

// RW 0x8E30DD
UpdateSleepTime FiringTrackerHelper::update()
{
	ObjectWeapons *w = getObject()->getWeapons();
	return w ? (UpdateSleepTime)w->updateFiringTracker() : UPDATE_SLEEP_FOREVER;
}

// the shell's block alone (no version of its own), so the module's layout is the LOGIC-1 shell's and older snapshots load
void FiringTrackerHelper::xfer(Xfer &x)
{
	ObjectHelperShell::xfer(x);
}
