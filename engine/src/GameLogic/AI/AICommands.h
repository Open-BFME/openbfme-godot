// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The player commands of movement (lane MOVE-1): the selection messages and the move / stop messages, as a pure deterministic step from a GameMessage to the
// units' AI (the path lockstep multiplayer takes; ZH GameLogicDispatch.cpp:260-330, 740-900, 1533-1600).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the dispatcher is RW 0x779A3D, its handlers were located through the decision tree and the two jump tables
// RW 0x77D0B7 / 0x77D127):
//   (the selection messages, 1001 .. 1005, are GameLogicDispatch's: the selection lives in Player::selection(), hashed with the player)
//   MSG_DO_MOVETO (1071, RW 0x77BAAA): arg0 location; MSG_DO_FORCEMOVETO (1073, RW 0x77AAF8) and MSG_DO_ATTACKMOVETO (1072, RW 0x77AA7A): arg0 location;
//   MSG_ADD_WAYPOINT (1074, RW 0x77BDD1): arg0 location, appended to the units' path;
//   MSG_DO_STOP (1077, RW 0x77ABF7): the group manager's stop, then groupIdle;
//   MSG_DO_MOVETO_FORMATION (1124, RW 0x77BB58): arg0 location, arg1 float, arg2 int, arg3 bool;
//   MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO (1125, RW 0x77BD65): arg0 object id, arg1 location, arg2 float (the facing): the one object moves to the location and turns.
//   lane PLAY-2: MSG_DO_FORCE_ATTACK_OBJECT (1062, RW 0x77AED3): arg0 object id, arg1 location (read, used only by the reserved-id branch, S-2480); a victim
//   that is gone: nothing; else the group's temporary weapon locks are released (RW 0x76FB64(1)), the group force-attacks (RW 0x77208A(force 1, victim,
//   0x7FFFFFFF, CMD_FROM_PLAYER)) and the members' temporary speed caps go (RW 0x76FD04). MSG_DO_FORCE_ATTACK_GROUND (1063, RW 0x77AF93): arg0 location; a
//   group that is not idle (RW 0x76FCBF) locks PRIMARY temporarily (RW 0x76FAB7(0, 1)), attacks the position (RW 0x77229A(pos, 0x7FFFFFFF, CMD_FROM_PLAYER))
//   and releases the temporary locks; an idle group releases them first and then attacks; both end with RW 0x76FD04. ZH GameLogicDispatch.cpp:1229-1290 is
//   the same shape.
//
// WHAT IS INFERENCE / NOT PORTED (stop S-223): the meaning of the formation move's float (taken as the facing the units end with), int and bool; the BFME group
// manager behind 1071 / 1124 (see AIGroup.h); the attack part of MSG_DO_ATTACKMOVETO (the units move, they do not engage on the way: the combat milestone), the
// weapon-lock release, the sounds and the selection checks of the shell (any message selects any live object, as in the logic of ZH).
//
// Determinism: the group a move message moves is the issuing player's selection in selection order, minus the objects that are gone or that the player does
// not control (RW 0x76ED16); handlers read only the logic and the message.

#pragma once

#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/ObjectTypes.h"

#include <vector>

class GameLogic;

class AICommands
{
public:
	// registers one handler per message type this class executes (a type another lane registered first is a logic error)
	void registerHandlers(GameLogicDispatch &dispatcher);

	// the ids a move message of `player` moves: Player::selection() without the objects that are gone or that the player does not control (RW 0x76ED16)
	static std::vector<ObjectID> selection(GameLogic &logic, int player);

	struct Stats
	{
		unsigned long long attacks = 0, forceAttacks = 0, attackMoves = 0, attackOrdersAccepted = 0; // lane COMBAT-1
		unsigned long long forceAttackGrounds = 0;                                                  // lane PLAY-2
		unsigned long long moves = 0, forceMoves = 0, attackMovesAsMoves = 0, waypoints = 0, stops = 0, formationMoves = 0, orientMoves = 0;
		unsigned long long rejected = 0; // malformed messages (missing arguments, unknown player)
	};
	const Stats &stats() const { return m_stats; }

private:
	bool move(GameLogic &logic, const GameMessage &m);
	bool attackObject(GameLogic &logic, const GameMessage &m); // lane COMBAT-1: MSG_DO_ATTACK_OBJECT / MSG_DO_FORCE_ATTACK_OBJECT (ZH AIGroup::groupAttackObject)
	bool forceAttackGround(GameLogic &logic, const GameMessage &m); // lane PLAY-2: MSG_DO_FORCE_ATTACK_GROUND (RW 0x77AF93)
	bool stop(GameLogic &logic, const GameMessage &m);
	bool formationMove(GameLogic &logic, const GameMessage &m);
	bool moveAndOrientate(GameLogic &logic, const GameMessage &m);
	bool validPlayer(GameLogic &logic, const GameMessage &m);

	Stats m_stats;
};
