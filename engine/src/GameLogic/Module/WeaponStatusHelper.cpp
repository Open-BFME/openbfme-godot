// OpenBFME. GPL-3.0.
// See GameLogic/Module/WeaponStatusHelper.h.

#include "GameLogic/Module/WeaponStatusHelper.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

// RW 0x68D19A: the shell's constructor leaves the module asleep; RW 0x68D1D5 -> 0x850C32 then wakes it at now + 1. The object is not in the list yet, so
// friend_awakenUpdateModule only stores the frame (the module index is -1): the same state, set directly
WeaponStatusHelper::WeaponStatusHelper(Thing *thing)
	: ObjectHelperShell(thing, "WeaponStatusHelper", "ModuleTag_WeaponStatusHelper")
{
	friend_setNextCallFrame(getObject()->logic().getFrame() + 1u);
}

// RW 0x690053 -> 0x68E197; returns 1 (UPDATE_SLEEP_NONE)
UpdateSleepTime WeaponStatusHelper::update()
{
	if (ObjectWeapons *w = getObject()->getWeapons())
	{
		w->updateWeaponStatusConditions();
	}
	return UPDATE_SLEEP_NONE;
}
