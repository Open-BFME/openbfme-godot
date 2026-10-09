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
//     otherwise, when the move is done (RW 0x748E46 != 0) (a human player's team: also when the group's centre is within count * a radius of the goal),
//     getNextWaypoint RW 0x74292D: GameLogicRandomValue(0, links - 1) (RW 0x6D328E, AIStates.cpp line 0x2657: drawn for every waypoint with a link,
//     one link included) picks the link (RW 0x484D58), the previous = the current. No next waypoint -> RW 0x66803C(previous) and STATE_SUCCESS. As a
//     team the team's current waypoint becomes the new one. The AI keeps the previous / current waypoint ids (AI + 0x28 / + 0x2C).
//   * the attack-follow forms (NAMED_ATTACK_FOLLOW_WAYPOINTS RW 0x754A3C, the team's RW 0x754AAA) and the mood switch of RW 0x755A35 (a computer
//     player's unit whose mood adjustment RW 0x664E18(1) has bit 4 re-issues the path as an attack-follow) are other states.
// DONOR: ZH AIStates.cpp AIFollowWaypointPathState.
//
// INFERENCE / NOT PORTED (stop S-1189, docs/STOPS.md): the team's formation offsets (+ 0x4C / + 0x50, RW 0x94FCBA) and the human team's group-radius
// arrival; the SHIP water path (state 0x49); the attack-follow states and the mood switch (they stay S-1186's stand-in); the internal move is the
// port's AIMoveToState (exact: no destination adjustment).

#pragma once

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
	AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_INDIVIDUALS = 5
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

	// the machine's goal waypoint (RW 0x6E8498): read by onEnter
	void setGoalWaypoint(int id) { m_goalWaypoint = id; }
	int currentWaypoint() const { return m_current; }

	// RW 0x74292D: the next waypoint after `w` (null when it has no link)
	static const Waypoint *nextWaypoint(const Waypoint &w, class GameLogic &logic);

private:
	StateReturnType startMove();
	bool m_asTeam, m_exact;
	int m_goalWaypoint = -1; ///< the machine's goal waypoint (-1 none)
	int m_current = -1;     ///< + 0x5C (-1 none)
	int m_previous = -1;    ///< + 0x60
	std::unique_ptr<AIMoveToState> m_move;
};

const char *waypointPathStopLine();
