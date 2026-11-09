// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AIStates.cpp AIFollowWaypointPathState).
//
// The waypoint path states of the AI (lane SCRIPT-3): the scripts' NAMED_ / TEAM_FOLLOW_WAYPOINTS[_EXACT] walk a waypoint path, choosing among a waypoint's
// links with the logic generator as each waypoint is reached.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * NAMED_FOLLOW_WAYPOINTS RW 0x7C9196 / _EXACT RW 0x7C928D: the waypoint of the path label closest to the unit (TheTerrainLogic vslot 0x90), then AI
//     command 6 (RW 0x770FA8) / 0x32 (RW 0x7710D7). TEAM_FOLLOW_WAYPOINTS (team, label, asTeam, formation) RW 0x7CB1DD -> RW 0x7BFE32: the team's AI group
//     (RW 0x7A3D3D), the waypoint closest to the average position of the team's members (RW 0x66362D walk; the sum times 1 / count), then the group's
//     command: `formation` -> RW 0x771E7D (the formation offsets RW 0x94FCBA, then as a team), else asTeam -> RW 0x771E1E (each member's + 0x42C cleared,
//     command 7, RW 0x771072), else RW 0x771D24 (command 6); each with source 1 (CMD_FROM_SCRIPT). TEAM_FOLLOW_WAYPOINTS_EXACT RW 0x7CB295 the same with the
//     exact commands.
//   * the commands (aiDoCommand RW 0x66A7EC): 6 -> AI vslot 0x68 (RW 0x66B907) state 3, 7 -> vslot 0x6C (RW 0x66BA11) state 2, 0x32 -> vslot 0x70 state
//     5, 0x33 -> vslot 0x74 state 4: not mobile (RW 0x690E97): nothing; object + 0x42C cleared, the machine cleared (vslot 0x14), its goal waypoint set
//     (RW 0x6E8498), the source stored (AI + 0x48); a SHIP with a contain whose path ends (first links) on water (RW 0x68F3F2) -> state 0x49, else the
//     state; then (a player or script source) the client's notification RW 0x66B379.
//   * the state (vtable 0xC28F90, ctor RW 0x7445B5(machine, asTeam): + 0x65 asTeam), an internal move (AIInternalMoveToState) whose goal (+ 0x20) is
//     the current waypoint (+ 0x5C); + 0x60 the previous one. onEnter RW 0x75070F: no goal waypoint and not as a team -> STATE_FAILURE; as a team the
//     team's current waypoint (team + 0x6C) is set to it (or, with none, read from it); the move starts (RW 0x74DADA).
//   * update RW 0x755A20: as a team, a team waypoint that differs from the state's is taken at once (the previous = the state's) and the move restarts;
//     otherwise, when the move is done (RW 0x748E46 != 0) (lane AIDATA-1: a SKIRMISH AI player's team, RW 0x6AA5B3, also when the centre of a new AI group
//     of the team (RW 0x70042A / 0x7A3D3D / 0x76EDA7) is within members x AIData SkirmishGroupFudgeDistance of the goal, RW 0x755B77 .. 0x755C08),
//     getNextWaypoint RW 0x74292D: GameLogicRandomValue(0, links - 1) (RW 0x6D328E, AIStates.cpp line 0x2657: drawn for every waypoint with a link,
//     one link included) picks the link (RW 0x484D58), the previous = the current. No next waypoint -> RW 0x66803C(previous) and STATE_SUCCESS. As a
//     team the team's current waypoint becomes the new one. The AI keeps the previous / current waypoint ids (AI + 0x28 / + 0x2C).
//   * the attack-follow forms (NAMED_ATTACK_FOLLOW_WAYPOINTS RW 0x754A3C, the team's RW 0x754AAA) are states 0x22 / 0x23 (AIAttackFollowWaypointPathState
//     below, lane WARG-1); the mood switch of RW 0x755A35 (a computer player's unit whose mood adjustment RW 0x664E18(1) has bit 4 re-issues the path as an
//     attack-follow) gives those commands.
// DONOR: ZH AIStates.cpp AIFollowWaypointPathState.
//
//   * lane BACKLOG-8, the group offset (state + 0x4C / + 0x50; BFME2 decomp AIFollowWaypointPathStateOnEnter.cpp / AIStatesDerivedOnExit.cpp
//     AIFollowWaypointPathState::computeGoal, byte-matched, tier B same-shape for RW 0x75070F / 0x755A20): onEnter clears it; as a team with a goal
//     waypoint a unit with a formation id (Object + 0x42C, RW 0x750790) takes its formation offset (+ 0x430 / + 0x434, AIGroup::createFormation RW
//     0x94FCBA). computeGoal adds it to the current waypoint's location; a unit with a formation id first turns it (AIGroup::rotateOffset RW 0x76E90A)
//     to the direction previous waypoint -> current one, without a previous one current -> its first link, without a link +x. The commands 6 / 0x32
//     clear the unit's formation id (RW 0x66B921 / 0x66B9C6), the as-team group command without the formation clears every member's (RW 0x771E31).
//
// INFERENCE / NOT PORTED (stop S-1189, docs/STOPS.md): a team WITHOUT a formation keeps no offset from its group's centre (the onEnter branch over the AI's
// group, RW 0x6630F5-style accessor, with the group speed: the port has no persistent AI group); the AI group of the skirmish team's arrival test (lane
// AIDATA-1) leaves no group id or membership behind (no AI group list in the port); the SHIP water path (state 0x49); the mood switch
// (lane WARG-1 ported the attack-follow states); the internal move is the port's AIMoveToState (exact: no destination adjustment).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class AIMoveToState;
class Team;
struct Waypoint;

enum
{
	AI_FOLLOW_WAYPOINT_PATH_AS_TEAM = 2,
	AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS = 3,
	AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_TEAM = 4,
	AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_INDIVIDUALS = 5,
	AI_ATTACK_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS = 0x22, // lane WARG-1: RW 0x753755 (RW 0x750AE7(machine, 0))
	AI_ATTACK_FOLLOW_WAYPOINT_PATH_AS_TEAM = 0x23         // RW 0x750AE7(machine, 1)
};

class AIFollowWaypointPathState : public AIState
{
public:
	AIFollowWaypointPathState(AIStateMachine &m, bool asTeam, bool exact);
	~AIFollowWaypointPathState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override; // lane SAVE-4c (AIWaypointPath.cpp)

	// the machine's goal waypoint (RW 0x6E8498): read by onEnter
	void setGoalWaypoint(int id) { m_goalWaypoint = id; }
	int currentWaypoint() const { return m_current; }

	// RW 0x74292D: the next waypoint after `w` (null when it has no link)
	static const Waypoint *nextWaypoint(const Waypoint &w, class GameLogic &logic);

protected:
	StateReturnType startMove();
	// computeGoal (RW 0x747723): the current waypoint plus the group offset; false without a current waypoint
	bool computeGoal(Coord3D &goal) const;
	std::unique_ptr<AIMoveToState> &move() { return m_move; }

private:
	// lane AIDATA-1, RW 0x755B77 .. 0x755C08: a skirmish AI player's team counts the waypoint reached when its centre is within members x SkirmishGroupFudgeDistance
	bool skirmishTeamCloseEnough(Team &team);
	bool m_asTeam, m_exact;
	int m_goalWaypoint = -1; ///< the machine's goal waypoint (-1 none)
	int m_current = -1;     ///< + 0x5C (-1 none)
	int m_previous = -1;    ///< + 0x60
	Coord2D m_groupOffset{}; ///< + 0x4C / + 0x50 (hashed when set)
	std::unique_ptr<AIMoveToState> m_move;
};

// lane WARG-1: the attack-follow states (NAMED_ATTACK_FOLLOW_WAYPOINTS / TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS). DONOR: ZH AIStates.cpp
// AIAttackFollowWaypointPathState.
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the commands: RW 0x754A3C / 0x754AAA make AI command 0x10 / 0x11 (the waypoint, the shot limit, the source); aiDoCommand (RW 0x66A7EC) gives both to
//     AI vslot 0xA8 = RW 0x66C1C9(waypoint, shots, asTeam 0 / 1, source): nothing (-1) when the AI is dead (+ 0x3BD) or the object is not mobile (RW
//     0x690E97); else the machine cleared (vslot 0x14), its goal waypoint set (RW 0x6E8498), the source stored (AI + 0x48), the machine set to 0x22 + asTeam;
//     the current weapon (RW 0x68B58C(0)): + 0x20 (whenFiringEnds) = 0, + 0x34 (maxShotCount) = the limit; a player or script source: the client's
//     notification (RW 0x66B2F5).
//   * the callers pass 0x7FFFFFFF shots and source 1 (CMD_FROM_SCRIPT): NAMED_ATTACK_FOLLOW_WAYPOINTS RW 0x7C920F (the waypoint of the label closest to the
//     unit, TheTerrainLogic vslot 0x90); TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS (team, label, asTeam, formation) RW 0x7CB239 -> RW 0x7BFF62, TEAM_FOLLOW_WAYPOINTS'
//     RW 0x7BFE32 with the group commands' mode 1 (RW 0x771D24 / 0x771E1E / 0x771E7D: mode 0 the follow commands, mode 1 RW 0x754A3C / 0x754AAA).
//   * the state (ctor RW 0x750AE7(machine, asTeam), 0x6C bytes, vtable 0xC29C98): AIFollowWaypointPathState (RW 0x744603(machine, asTeam, 0)) plus + 0x68 its
//     attack machine RW 0x74E083 (vtable 0xC284B8, "AIAttackFollowMachine"): state 0 the idle state that does not look for targets (RW 0x741812(machine, 1)),
//     0x27 the crate pick-up (RW 0x74458E), 10 the attack state (RW 0x74CABC(machine, 0, 1, 0, 0)), each ending in 0; the ctor enters its default state
//     (vslot 0x1C, idle). Vtable slot 0x24 (isActive) is RW 0x8BD372: true.
//   * onEnter RW 0x750B58: the attack machine cleared (vslot 0x14) and set to 0 (vslot 0x20), then the follow state's onEnter (RW 0x75070F).
//     onExit RW 0x74BB7E: the attack machine set to 0, then the follow state's onExit (RW 0x74BAB6).
//   * update RW 0x755D78: an attack machine whose state is not idle (none counts as idle; the state's slot 0x20): the locomotor goal none (AI vslot 0x220),
//     MOVING (Object + 0x113 bit 0x20, bit 61) and BACKING_UP (+ 0x11F bit 0x10, bit 156) cleared, the attack machine updated (vslot 0x10); still busy ->
//     CONTINUE, idle now -> retarget and repath. Idle: the AI's crate (RW 0x66810B) -> its goal and state 0x27, CONTINUE; else the mood target
//     getNextMoodTarget(!retarget, 0) (RW 0x66844A) -> its goal (vslot 0x38), state 10, AI + 0x3C7 = 1, CONTINUE. Then a repath: computeGoal(asTeam) (RW
//     0x747723) and the internal move's computePath (vslot 0x44); then the follow state's update (RW 0x755A20).
//   * BFME2 1.06 (the second edition) has the same state: onEnter / onExit / update Rva 0x34F1C4 / 0x34A19F / 0x35426B, vtable 0xC13638, its attack
//     machine at + 0x68 (BFME2 decomp AIStatesDerivedOnExit_ComputeGoal_ComputeGoal_Update.cpp); the port serves both editions.
// INFERENCE / NOT PORTED (stop S-1189): the crate (no AI crate in the port, as S-1188); AI + 0x3C7 (the idle and attack-move states' flag, unported there
// too); the attack machine's idle state is the hunt machine's (makeHuntIdleState), whose onEnter draws the logic generator as RW 0x7489B6 does but
// skips its mood-check push (RW 0x7489D0 -> RW 0x662DD9), so a unit whose last mood scan is long past may take its first target sooner than retail (review
// round 3; the hunt and attack-area machines share it); the attack machine is made at the first entry, not by the AI machine's constructor: retail's constructor enters its idle state, whose onEnter (RW
// 0x7489B6) draws the logic generator (AIStates.cpp line 0x94A) for both states of every AI machine it builds, which the port does not draw.
class AIAttackFollowWaypointPathState : public AIFollowWaypointPathState
{
public:
	AIAttackFollowWaypointPathState(AIStateMachine &m, bool asTeam);
	~AIAttackFollowWaypointPathState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	bool isActive() const override { return true; } // vtable slot 0x24: RW 0x8BD372
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;

	AIStateMachine *attackMachine() const { return m_attackMachine.get(); }

private:
	std::unique_ptr<AIStateMachine> makeAttackMachine();
	bool attackMachineIdle() const;
	std::unique_ptr<AIStateMachine> m_attackMachine; ///< + 0x68
};

const char *waypointPathStopLine();
