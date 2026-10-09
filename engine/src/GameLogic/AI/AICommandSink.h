// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AICommandSink (lane PROD-1): the narrow hand-off from production to the AI of the produced object. Production issues commands to the new
// object's AIUpdateInterface (RW AICommandParms, `ai + 0x20`): move to the factory's rally point, follow the exit production path, idle. The
// AIUpdateInterface itself belongs to lane MOVE-1 (the AIMover component and the pathfinder are PATH-1's); until it exists the sink only RECORDS
// what production asked for, in issue order, and reports the commands nobody executed (stop S-201). When MOVE-1 installs a handler, the same call
// sites run it unchanged.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the AICommandParms command numbers production uses (RW 0x66C4CA aiMoveToPosition = 0, RW 0x5E821A
// aiIdle = 5, RW 0x87C9B8 aiFollowExitProductionPath = 10, RW 0x770EDE move to object = 0x42), each called with command source CMD_FROM_AI = 2.
//
// Determinism: commands are appended in logic order; nothing depends on pointers.

#pragma once

#include "GameLogic/ObjectTypes.h"

#include "Common/INIDataTypes.h"

#include <functional>
#include <vector>

class Object;

enum AICommandType
{
	AICMD_MOVE_TO_POSITION = 0,       ///< RW 0x66C4CA aiMoveToPosition(pos, source)
	AICMD_IDLE = 5,                   ///< RW 0x5E821A aiIdle(source)
	AICMD_FOLLOW_EXIT_PRODUCTION_PATH = 10, ///< RW 0x87C9B8 aiFollowExitProductionPath(path, source object, source)
	AICMD_MOVE_TO_OBJECT = 0x42,      ///< RW 0x770EDE
	// OpenBFME marker (not a retail command number): the horde regroup call of QueueProductionExitUpdate::releaseLastExit (HordeContain slot 4, RW 0x8759FF)
	AICMD_HORDE_RETURN_TO_FORMATION = 0x1000
};

// ZH CommandSourceType; RotWK's production passes CMD_FROM_AI
enum CommandSourceType
{
	CMD_FROM_PLAYER = 0,
	CMD_FROM_SCRIPT = 1,
	CMD_FROM_AI = 2
};

struct AICommand
{
	AICommandType type = AICMD_IDLE;
	CommandSourceType source = CMD_FROM_AI;
	ObjectID object = INVALID_ID;      ///< the object the command is for
	ObjectID target = INVALID_ID;      ///< AICMD_MOVE_TO_OBJECT; AICMD_FOLLOW_EXIT_PRODUCTION_PATH: the source object (RW 0x87C9B8 arg 2: the producer for the Default and SupplyCenter exits, NULL for the queue exit)
	std::vector<Coord3D> path;         ///< AICMD_MOVE_TO_POSITION: one point; AICMD_FOLLOW_EXIT_PRODUCTION_PATH: the waypoints
	UnsignedInt frame = 0;             ///< the logic frame it was issued in
};

class AICommandSink
{
public:
	// returns true when the AI took the command (MOVE-1); false: it is counted as unexecuted
	typedef std::function<bool(Object &, const AICommand &)> Handler;
	void setHandler(Handler h) { m_handler = std::move(h); }
	bool hasHandler() const { return (bool)m_handler; }
	// records the command and runs the handler when there is one
	void issue(Object &obj, AICommand command);
	const std::vector<AICommand> &issued() const { return m_issued; }
	unsigned long long unexecuted() const { return m_unexecuted; }
	// keeps memory bounded in long games: forgets the recorded commands (the counters stay)
	void clearRecorded() { m_issued.clear(); }

private:
	Handler m_handler;
	std::vector<AICommand> m_issued;
	unsigned long long m_unexecuted = 0;
};
