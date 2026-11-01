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

#include "Common/Team.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameClient/MapChunks.h"

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

// lane BFME2CAMP1: RW 0x664C11 privateGuardAreaFromPosition(area, mode, source, pos) (AI vslot 0x10C; commands 0x21 RW 0x7718E1 with no position and 0x44
// RW 0x77194F with one; BFME2 decomp AIUpdateInterfacePrivateCommands.cpp, tier B): nothing for a HORDE_MEMBER (status 0x26), an immobile object (RW 0x690E97)
// or a PROJECTILE (template + 0x10B bit 1); the guard target type AREA goes to + 0x54 (the one the guard state reads) when it is NONE (4), else to + 0x50;
// + 0x6C = the area, + 0x4C = the mode, the guard location (+ 0x58) = the position, else the area's centre (RW 0x6E4CA7), + 0x64 = 0, the machine cleared
// (vslot 0x14), + 0x48 = the source, setState(0x10, AI_GUARD).
// INFERENCE: the port keeps no NONE type and no + 0x50 / + 0x64 fields (above). Every retail writer of + 0x54 writes the guard location too (guardHere,
// privateGuardPosition, this), so "+ 0x54 is NONE" is taken as "no guard was ever set": the mode, the type and the location all still 0 (the state the
// hash's guard block skips). With + 0x54 not NONE the type stays as it is (+ 0x50 takes AREA in retail, read by nothing the port has): the unit then guards
// the area's centre as a location, as retail's guard state does.
bool AIUpdateInterface::aiGuardArea(const TriggerArea &area, int mode, CommandSourceType source, const Coord3D *pos)
{
	if (!acceptCommand(source, pos ? 0x44 : 0x21))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) || !GarrisonRules::isMobile(obj) ||
		obj.isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return false; // RW 0x664C2B .. 0x664C46
	}
	const int index = obj.logic().scriptEngine().triggerIndex(&area);
	if (index < 0)
	{
		return false; // not a trigger of the game (every area a script names is one of the map's)
	}
	const bool none = m_guardMode == 0 && m_guardTargetType == 0 && m_guardLocation.x == 0.0f && m_guardLocation.y == 0.0f && m_guardLocation.z == 0.0f;
	if (none)
	{
		m_guardTargetType = GUARDTARGET_AREA; // RW 0x664C4E: + 0x54
	}
	m_areaToGuard = index; // + 0x6C
	m_guardMode = mode;    // + 0x4C
	m_guardLocation = pos ? *pos : ScriptConditions::triggerCenter(obj.logic(), area); // RW 0x664C70 / RW 0x6E4CA7
	m_machine->clear();          // RW 0x664C9E: vslot 0x14
	setLastCommandSource(source); // + 0x48
	m_machine->setState(AI_GUARD); // RW 0x664CA9
	wakeUpNow();
	return true;
}

// lane BFME2CAMP1: RW 0x664B93 privateGuardTeam(team, mode, source) (AI vslot 0x104; command 0x20 RW 0x771873): nothing for a HORDE_MEMBER (status 0x26), an
// immobile object (RW 0x690E97) or a PROJECTILE (template + 0x10B bit 1); the guard target type TEAM goes to + 0x54 when it is NONE (4), else to + 0x50;
// + 0x64 (the guarded object) = 0, + 0x4C = the mode, + 0x68 = the team's id (team + 0x34), the machine cleared (vslot 0x14), + 0x48 = the source,
// setState(0x10, AI_GUARD). The guard location is not written. INFERENCE: "+ 0x54 is NONE" as in aiGuardArea (above).
bool AIUpdateInterface::aiGuardTeam(const Team &team, int mode, CommandSourceType source)
{
	if (!acceptCommand(source, 0x20))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) || !GarrisonRules::isMobile(obj) ||
		obj.isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return false; // RW 0x664B9E .. 0x664BC1
	}
	const bool none = m_guardMode == 0 && m_guardTargetType == 0 && m_guardLocation.x == 0.0f && m_guardLocation.y == 0.0f && m_guardLocation.z == 0.0f;
	if (none)
	{
		m_guardTargetType = GUARDTARGET_TEAM; // RW 0x664BCC: + 0x54
	}
	m_guardMode = mode;          // RW 0x664BE7: + 0x4C
	m_guardTeam = team.getID();  // RW 0x664BF1: + 0x68
	m_machine->clear();          // RW 0x664BF6: vslot 0x14
	setLastCommandSource(source); // RW 0x664C00: + 0x48
	m_machine->setState(AI_GUARD); // RW 0x664C07: setState(0x10)
	wakeUpNow();
	return true;
}
