// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AIGroup (lane MOVE-1): a player's selected objects as an order target (ZH AIGroup.h / AIGroup.cpp; RW creates one per message from the player's selection,
// RW 0x779A9E). The group move is ZH's AIGroup::groupMoveToPosition (AIGroup.cpp:1552-1760) in its plain, non-formation form: every unit keeps its offset from the
// nearest unit and the destinations are adjusted by the pathfinder.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): MSG_DO_MOVETO (RW 0x77BAAA) takes the group, releases its temporary weapon locks (RW 0x76FB64(group, 1)) and then
// either hands the group to the BFME group manager (RW 0x75748C, when GameData byte +0x11CA is set) or to the generic group command (RW 0x774897, the ZH
// AIGroup path); MSG_DO_STOP (RW 0x77ABF7) is the group manager's stop (RW 0x756E5C(mgr, 3, group)), the weapon-lock release and groupIdle (RW 0x771F49(group, 0)).
//
// WHAT IS INFERENCE / NOT PORTED (stop S-223): the BFME group manager and its formation layout (RW 0x94FCBA: FormationWidth x FormationDepth slots bucketed by
// locomotor FormationPriority, AIData FormationColumns / FormationSquadSpacing / FormationRowDepth / FormationColumnWidth, the swap optimisation) and the ZH
// column / tighten moves (friend_computeGroundPath, groupTightenToPosition) are not ported: the group moves with ZH's individual destinations; the weapon locks do
// not exist yet. The order of units with equal distance to the goal is the order of the group (ZH's sort is unspecified).
//
// Determinism: members are kept in message order; the sort is a stable sort on (distance squared, id); floats are float32 through SimMath.

#pragma once

#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/ObjectTypes.h"

#include "Common/INIDataTypes.h"

#include <vector>

class AIWorld;
class GameLogic;
class Object;

class AIGroup
{
public:
	// the members that still exist and have an AI; others are left out
	AIGroup(GameLogic &logic, const std::vector<ObjectID> &ids);

	size_t size() const { return m_members.size(); }
	const std::vector<Object *> &members() const { return m_members; }

	// ZH AIGroup::groupMoveToPosition. `addWaypoint` appends the destination to the path the units follow. `finalAngle` (when `haveFinalAngle`) is the facing every unit
	// takes on arrival (the BFME formation move's drag direction, S-223).
	void groupMoveToPosition(const Coord3D &pos, bool addWaypoint, CommandSourceType source, bool haveFinalAngle = false, float finalAngle = 0.0f);
	// lane MOVE-3: MSG_DO_MOVETO / MSG_DO_ATTACKMOVETO with GameData + 0x11CA (PlanningModeEnabled, set by the GameData constructor RW 0x643982 and cleared only
	// by a command-line switch RW 0x7B9F93; retail INI never names it): the group manager's move order (RW 0x75748C -> order RW 0x94E232, vtable 0xC81430)
	// executed at once for every object of the group in group order (RW 0x7572FA -> 0x94ED4C -> slot 0x10 RW 0x94E184): an object that is not a horde member
	// (RW 0x6939DF) and has an AI gets its own destination (RW 0x94DF97: a ground mover's is the point adjusted by adjustDestination RW 0x6FE456 with the
	// point as the group destination, its goal reserved there, a LARGE_RECTANGLE_PATHFIND unit's with its heading to it) and the move (RW 0x94DF1B ->
	// aiMoveToPosition RW 0x66C4CA) or attack-move (RW 0x696266) to it. Every unit is sent to the one point: the destinations spread only by the reservations
	void planningMoveToPosition(const Coord3D &pos, bool attackMove, CommandSourceType source);
	// the stop line S-1831 (raised by every planning move)
	static const char *planningStopLine();
	// ZH AIGroup::groupIdle
	void groupIdle(CommandSourceType source);

	// ---- lane PLAY-2: the force-attack commands (RW 0x77AED3 / 0x77AF93 in GameLogicDispatch RW 0x779A3D) ----
	// RW 0x76FCBF (BFME2 decomp AIGroupIsIdle.cpp:AIGroup::isIdle, tier B same-shape): every member's AI is idle or the member is effectively dead
	bool isIdle() const;
	// RW 0x76FAB7 (BFME2 decomp AIGroupAttackTeam.cpp:AIGroup::setWeaponLockForGroup, tier A): Object::setWeaponLock (RW 0x69121A) on every member; a
	// member whose weapon before the lock had ShareTimers (template + 0x16A) and was not ready to fire (RW 0x6CD142 != 0) passes that weapon's next fire frame
	// (+ 0x18) to its new current weapon and marks it OUT_OF_AMMO (RW 0x6CA232(1)). True when any member locked
	bool setWeaponLockForGroup(int slot, WeaponLockType type);
	// RW 0x76FB64 (decomp AIGroup::releaseWeaponLockForGroup, tier A): Object::releaseWeaponLock (RW 0x68DF11) on every member
	void releaseWeaponLockForGroup(WeaponLockType type);
	// RW 0x77229A (decomp AIGroupAttackTeam.cpp:AIGroup::groupAttackPosition, tier B same-shape; ZH AIGroup.cpp:2223): every member attacks the position
	// (aiAttackPosition RW 0x6961F1)
	void groupAttackPosition(const Coord3D &pos, int maxShotsToFire, CommandSourceType source);
	// RW 0x76FD04 (RotWK only): every member's current locomotor (AI + 0x1F0) loses its temporary speed cap (+ 0x2C = -1.0, RW 0xBD19DC). The attack
	// commands end with it (RW 0x77B00A -> 0x77A342)
	void clearTemporarySpeedCaps();
	// the stop line of the force-attack commands (S-2480)
	static const char *forceAttackStopLine();
	// the stop line S-223 (raised by every group move)
	static const char *stopLine();

private:
	// ZH AIGroup::computeIndividualDestination (non-formation): the offset of the unit from `center` (clamped to six bounding radii) added to the group's goal
	Coord3D computeIndividualDestination(const Coord3D &groupDest, Object &obj, const Coord3D &center) const;
	// ZH clampWaypointPosition: the goal stays inside the map minus four cells
	Coord3D clampWaypointPosition(const Coord3D &pos) const;

	GameLogic &m_logic;
	std::vector<Object *> m_members;
};
