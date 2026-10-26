// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AIUpdate.cpp privateGuardPosition).
//
// Lane SCRIPT-4: the guard-position command of the AI (AICommandInterface::aiGuardPosition), the script actions' guard (TEAM_GUARD, NAMED_GUARD,
// UNIT_GUARD_FOR_FRAMECOUNT / _SECONDS, UNIT_ / TEAM_GUARD_POSITION). The guard state and its machine are AIGuard.cpp's (lane PLAY-4).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor, tier A for both addresses):
//   * RW 0x771790 aiGuardPosition(pos, mode, source): AICommandParms(0x1E, source) (RW 0x7536DD) with the position and the mode, then aiDoCommand
//     (vslot 0, RW 0x66A7EC: the gate RW 0x667174, then vslot 0xFC).
//   * RW 0x6649C6 privateGuardPosition(pos, mode, source): nothing for a HORDE_MEMBER (status 0x26), an immobile object (RW 0x690E97) or a PROJECTILE
//     (template + 0x10B bit 1); the guard target type: + 0x54 (the one the guard state reads, vslot 0x1EC) becomes LOCATION when it is NONE (4), else
//     + 0x50 does; for a player's command (source 0) a position outside TheTerrainLogic's extent (vslot 0x20) becomes the closest point inside (vslot
//     0x34); then + 0x58 = the position, + 0x4C = the mode, the machine cleared (vslot 0x14), + 0x48 = the source, setState(0x10, AI_GUARD). The angle
//     (+ 0x1A0) is not written.
//
// INFERENCE / NOT PORTED: the port keeps one guard target type (+ 0x54) starting at LOCATION (0), not retail's two starting at NONE (4); this command
// leaves + 0x54 as it is (the port has no NONE; its only other writer is guardHere, LOCATION unless AREA; no area guard is ported, S-1186). The
// player's extent clamp has no caller (the players' guard message is not ported): such a call is counted as an unported command and refused. The
// wake-up after the state change is the port's convention for the AI commands (aiHunt).

#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

bool AIUpdateInterface::aiGuardPosition(const Coord3D &pos, int mode, CommandSourceType source)
{
	if (source == CMD_FROM_PLAYER)
	{
		aiUnported("aiGuardPosition from a player (the extent clamp RW 0x664A2C)", source);
		return false;
	}
	if (!acceptCommand(source, 0x1E))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) || !GarrisonRules::isMobile(obj) ||
		obj.isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return false; // RW 0x6649D7 .. 0x6649FD
	}
	// RW 0x6649FF: + 0x54 becomes LOCATION only when it is NONE, else + 0x50 is written; the port's + 0x54 starts at LOCATION (see the header), so it is
	// left as it is
	m_guardLocation = pos;
	m_guardMode = mode;
	m_machine->clear();
	setLastCommandSource(source);
	m_machine->setState(AI_GUARD);
	wakeUpNow();
	return true;
}
