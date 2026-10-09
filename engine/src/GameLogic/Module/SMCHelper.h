// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SMCHelper: the "special model condition" timer helper of an Object (ZH Source/GameLogic/Object/Helper/ObjectSMCHelper.cpp), lane PROD-1.
// It replaces the LOGIC-1 shell of the same name (GameLogic/Module/ObjectHelper.h) now that production needs it: a freshly produced unit is
// JUST_BUILT, COMING_OUT_OF_FACTORY and INVULNERABLE for a number of frames.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the helper is made by the Object constructor first (RW 0x69300C, 0x24 bytes, vtables 0xC12160 /
// 0xC67300 / 0xC12154). The list is a std::list of {model condition bit, expiry frame} nodes at +0x20.
//   set(bit, frames) RW 0x8E2C0F: a bit outside 0 .. 0x24E is ignored. When the bit is in the list its expiry becomes max(old, now + frames);
//     otherwise a node {bit, now + frames} is appended and the object's model condition bit is set (the drawable is told, RW 0x68B53C). Then
//     the helper is woken at calc() frames from now (RW 0x850C32 = UpdateModule::setWakeFrame).
//   update() RW 0x8E2B72: every node whose expiry is <= now is removed after its bit is cleared on the object (the drawable is told); then the
//     drawable gets a refresh (RW 0x67449C) and the return value is calc().
//   calc() RW 0x8E2ACA: UPDATE_SLEEP_FOREVER for an empty list, else max(1, min(expiry) - now).
//   getDisabledTypesToProcess RW 0x8B3313 (update interface vtable RW 0xC12154 slot 1): every disabled type (lane COMBAT-3).
// DONOR: ZH ObjectSMCHelper.cpp (same structure: a list of timed model conditions).
// Not ported: xfer / save state.

#pragma once

#include "GameLogic/Module/ObjectHelper.h"

#include <list>

// derives from the LOGIC-1 shell so GameLogic::report() still lists it by name (helperShells); unlike the shell it acts (isUnported() is false)
class SMCHelper : public ObjectHelperShell
{
public:
	explicit SMCHelper(Thing *thing);

	bool isUnported() const override { return false; }
	UpdateSleepTime update() override;
	// lane COMBAT-3: the update interface's slot 1 (vtable RW 0xC12154 + 4) is RW 0x8B3313, the global all-types mask RW 0xDE8B90: the timers run on a disabled
	// object too. A held passenger or siege crew member (DISABLED_HELD) loses JUST_BUILT on time: Grond's trolls (JUST_BUILT: Model None) were never drawn.
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)0xFFFFFFFFu; }
	void setSpecialModelConditionState(int bit, UnsignedInt frames);
	size_t activeCount() const { return m_list.size(); }
	void crc(StateHasher &hasher) const override;

private:
	struct Entry
	{
		int bit;
		UnsignedInt expiry;
	};
	UpdateSleepTime calcSleep() const;

	std::list<Entry> m_list;
};
