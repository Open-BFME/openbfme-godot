// OpenBFME. GPL-3.0.
// See GameLogic/AI/AICommands.h.

#include "GameLogic/AI/AICommands.h"

#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>

namespace
{
const char *const kLane = "MOVE-1";

const GameMessageArgument *arg(const GameMessage &m, size_t i, GameMessageArgumentDataType type)
{
	const GameMessageArgument *a = m.getArgument(i);
	return a && a->type == type ? a : nullptr;
}
}

void AICommands::registerHandlers(GameLogicDispatch &d)
{
	for (int t : { (int)MSG_DO_MOVETO, (int)MSG_DO_FORCEMOVETO, (int)MSG_DO_ATTACKMOVETO, (int)MSG_ADD_WAYPOINT })
	{
		d.registerHandler(t, kLane, [this](GameLogic &l, const GameMessage &m) { return move(l, m); });
	}
	for (int t : { (int)MSG_DO_ATTACK_OBJECT, (int)MSG_DO_FORCE_ATTACK_OBJECT })
	{
		d.registerHandler(t, "COMBAT-1", [this](GameLogic &l, const GameMessage &m) { return attackObject(l, m); });
	}
	d.registerHandler(MSG_DO_STOP, kLane, [this](GameLogic &l, const GameMessage &m) { return stop(l, m); });
	d.registerHandler(MSG_DO_MOVETO_FORMATION, kLane, [this](GameLogic &l, const GameMessage &m) { return formationMove(l, m); });
	d.registerHandler(MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO, kLane, [this](GameLogic &l, const GameMessage &m) { return moveAndOrientate(l, m); });
}

// RW 0x6AAB85 / 0x76ED16: the player's selection as a group, the objects the player does not control removed (and the ones that are gone)
std::vector<ObjectID> AICommands::selection(GameLogic &logic, int playerIndex)
{
	std::vector<ObjectID> out;
	Player *player = logic.players().getNthPlayer(playerIndex);
	if (!player)
	{
		return out;
	}
	for (ObjectID id : player->selection())
	{
		const Object *o = logic.findObjectByID(id);
		if (o && !o->isDestroyed() && o->getControllingPlayer() == player)
		{
			out.push_back(id);
		}
	}
	return out;
}

bool AICommands::validPlayer(GameLogic &logic, const GameMessage &m)
{
	return logic.players().getNthPlayer(m.getPlayerIndex()) != nullptr;
}

bool AICommands::move(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *dest = arg(m, 0, ARGUMENTDATATYPE_LOCATION);
	if (!dest || !validPlayer(logic, m))
	{
		++m_stats.rejected;
		return false;
	}
	AIGroup group(logic, selection(logic, m.getPlayerIndex()));
	const int type = m.getType();
	if (type == MSG_ADD_WAYPOINT)
	{
		++m_stats.waypoints;
		group.groupMoveToPosition(dest->location, true, CMD_FROM_PLAYER);
	}
	else
	{
		(type == MSG_DO_FORCEMOVETO ? m_stats.forceMoves : type == MSG_DO_ATTACKMOVETO ? m_stats.attackMoves : m_stats.moves) += 1;
		if (type != MSG_DO_FORCEMOVETO && logic.aiWorld() && logic.aiWorld()->config().pathfind.planningModeEnabled)
		{
			// lane MOVE-3: RW 0x77BAAA (MSG_DO_MOVETO) and RW 0x77AA7A (MSG_DO_ATTACKMOVETO) hand the group to the group manager's move order (RW 0x75748C) while
			// GameData + 0x11CA (PlanningModeEnabled) is set, which retail always is; MSG_DO_FORCEMOVETO (RW 0x77AAF8) takes the generic group command (RW 0x774897)
			group.planningMoveToPosition(dest->location, type == MSG_DO_ATTACKMOVETO, CMD_FROM_PLAYER);
			return true;
		}
		group.groupMoveToPosition(dest->location, false, CMD_FROM_PLAYER);
		if (type == MSG_DO_ATTACKMOVETO)
		{
			// ZH groupAttackMoveToPosition (lane COMBAT-1, stop S-328): every unit marches to its own goal and attacks what it meets on the way
			for (Object *o : group.members())
			{
				if (AIUpdateInterface *ai = o->getAIUpdateInterface())
				{
					ai->armAttackMove(ai->stateMachine().goalPosition());
				}
			}
		}
	}
	return true;
}

// ZH AIGroup::groupAttackObject (MSG_DO_ATTACK_OBJECT RW 0x77B6B3 area): every unit of the selection that can attack the victim is ordered; a horde attacks the victim's horde
bool AICommands::attackObject(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *who = arg(m, 0, ARGUMENTDATATYPE_OBJECTID);
	if (!who || !validPlayer(logic, m))
	{
		++m_stats.rejected;
		return false;
	}
	const bool force = m.getType() == MSG_DO_FORCE_ATTACK_OBJECT;
	(force ? m_stats.forceAttacks : m_stats.attacks) += 1;
	Object *victim = logic.findObjectByID(who->objectID);
	if (!victim || victim->isDestroyed())
	{
		return true; // the target is already gone: nothing happens
	}
	AIGroup group(logic, selection(logic, m.getPlayerIndex()));
	for (Object *o : group.members())
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		Object *target = victim;
		if (o->isKindOf((unsigned)CombatNames::kinds().horde) && victim->getContainedBy() && victim->getContainedBy()->getContain() &&
			victim->getContainedBy()->getContain()->getHordeContainInterface())
		{
			target = victim->getContainedBy(); // a horde fights the horde of the member that was clicked
		}
		const bool ok = force ? ai->aiForceAttackObject(target, CMD_FROM_PLAYER) : ai->aiAttackObject(target, CMD_FROM_PLAYER);
		m_stats.attackOrdersAccepted += ok ? 1u : 0u;
	}
	return true;
}

bool AICommands::stop(GameLogic &logic, const GameMessage &m)
{
	if (!validPlayer(logic, m))
	{
		++m_stats.rejected;
		return false;
	}
	AIGroup group(logic, selection(logic, m.getPlayerIndex()));
	group.groupIdle(CMD_FROM_PLAYER);
	++m_stats.stops;
	return true;
}

bool AICommands::formationMove(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *dest = arg(m, 0, ARGUMENTDATATYPE_LOCATION);
	const GameMessageArgument *angle = arg(m, 1, ARGUMENTDATATYPE_REAL);
	if (!dest || !angle || !validPlayer(logic, m))
	{
		++m_stats.rejected;
		return false;
	}
	AIGroup group(logic, selection(logic, m.getPlayerIndex()));
	group.groupMoveToPosition(dest->location, false, CMD_FROM_PLAYER, true, angle->real);
	++m_stats.formationMoves;
	return true;
}

bool AICommands::moveAndOrientate(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *who = arg(m, 0, ARGUMENTDATATYPE_OBJECTID);
	const GameMessageArgument *dest = arg(m, 1, ARGUMENTDATATYPE_LOCATION);
	const GameMessageArgument *angle = arg(m, 2, ARGUMENTDATATYPE_REAL);
	if (!who || !dest || !angle)
	{
		++m_stats.rejected;
		return false;
	}
	Object *o = logic.findObjectByID(who->objectID);
	if (!o || !o->getAIUpdateInterface())
	{
		return true; // RW 0x77BDB3: no such object or no AI: nothing happens
	}
	o->getAIUpdateInterface()->aiMoveToPositionAndOrientate(dest->location, angle->real, CMD_FROM_PLAYER);
	++m_stats.orientMoves;
	return true;
}
