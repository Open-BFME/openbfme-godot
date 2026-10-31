// OpenBFME. GPL-3.0.
// See GameLogic/AI/AICommands.h.

#include "GameLogic/AI/AICommands.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/SimMath.h"

#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>

namespace
{
const char *const kLane = "MOVE-1";

const GameMessageArgument *arg(const GameMessage &m, size_t i, GameMessageArgumentDataType type)
{
	const GameMessageArgument *a = m.getArgument(i);
	return a && a->type == type ? a : nullptr;
}

bool finiteLocation(const Coord3D &p)
{
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
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
	d.registerHandler(MSG_DO_FORCE_ATTACK_GROUND, "PLAY-2", [this](GameLogic &l, const GameMessage &m) { return forceAttackGround(l, m); });
	d.registerHandler(MSG_DO_STOP, kLane, [this](GameLogic &l, const GameMessage &m) { return stop(l, m); });
	d.registerHandler(MSG_DO_ROTATE_FIRINGARC, kLane, [this](GameLogic &logic, const GameMessage &m) {
		const auto *target=arg(m,0,ARGUMENTDATATYPE_LOCATION);
		// PR #88 review fix 5: a non-finite target from a malformed peer message is rejected (a NaN facing breaks the locomotor goal)
		if(!target || !validPlayer(logic,m) || !finiteLocation(target->location)) {++m_stats.rejected;return false;}
		for(auto id:selection(logic,m.getPlayerIndex()))if(auto *object=logic.findObjectByID(id)) {
			const float dx=SimMath::subf32(target->location.x,object->getPosition()->x),dy=SimMath::subf32(target->location.y,object->getPosition()->y);
			if(SimMath::addf32(SimMath::mulf32(dx,dx),SimMath::mulf32(dy,dy))<1)continue;
			if(auto *ai=object->getAIUpdateInterface())ai->setLocomotorGoalOrientation(SimMath::fstpDword(SimMath::atan2d(dy,dx)));
		}
		return true;
	});
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
		group.groupMoveToPosition(dest->location, false, CMD_FROM_PLAYER, false, 0.0f, type == MSG_DO_ATTACKMOVETO);
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
	if (force)
	{
		group.releaseWeaponLockForGroup(LOCKED_TEMPORARILY); // RW 0x77AF7C (lane PLAY-2)
	}
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
	if (force)
	{
		group.clearTemporarySpeedCaps(); // RW 0x77AF8E -> 0x77A3BF -> 0x77B00A -> RW 0x76FD04 (lane PLAY-2)
	}
	return true;
}

// lane PLAY-2: MSG_DO_FORCE_ATTACK_GROUND, RW 0x77AF93 (see AICommands.h)
bool AICommands::forceAttackGround(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *where = arg(m, 0, ARGUMENTDATATYPE_LOCATION);
	if (!where || !validPlayer(logic, m))
	{
		++m_stats.rejected;
		return false;
	}
	++m_stats.forceAttackGrounds;
	const int kNoMaxShotsLimit = 0x7FFFFFFF; // RW 0x77AFD9 / 0x77AFFA
	AIGroup group(logic, selection(logic, m.getPlayerIndex()));
	if (!group.isIdle())
	{
		// RW 0x77AFD2 .. 0x77AFF2 (ZH: "forceAttackRequiresPrimaryWeapon"): a busy group fires its PRIMARY weapon
		group.setWeaponLockForGroup(PRIMARY_WEAPON, LOCKED_TEMPORARILY);
		group.groupAttackPosition(where->location, kNoMaxShotsLimit, CMD_FROM_PLAYER);
		group.releaseWeaponLockForGroup(LOCKED_TEMPORARILY);
	}
	else
	{
		// RW 0x77AFF4 .. 0x77B005
		group.releaseWeaponLockForGroup(LOCKED_TEMPORARILY);
		group.groupAttackPosition(where->location, kNoMaxShotsLimit, CMD_FROM_PLAYER);
	}
	group.clearTemporarySpeedCaps(); // RW 0x77B00A -> 0x77A342 -> RW 0x76FD04
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
	const auto *type=arg(m,2,ARGUMENTDATATYPE_INTEGER);
	const auto *attack=arg(m,3,ARGUMENTDATATYPE_BOOLEAN);
	const auto *world=RetailObjectWorld::currentContext();
	// PR #88 review fix 5: a NaN / infinite angle or location from a malformed peer message is rejected; it reached the per-slot moves and
	// crashed (or, on the old whole-group path, hung) the simulation. The angle must also lie in [-2 pi, 2 pi] (the client sends an atan2
	// result): a huge finite facing (1e20) never leaves the final-turn wrap loop (AIMove, RW 0x748E46), whose 2 pi steps vanish at that magnitude
	const float twoPi = 6.2831855f;
	if (!type || !attack || (!world && !m_formations) || !finiteLocation(dest->location) || !(angle->real >= -twoPi && angle->real <= twoPi)) { ++m_stats.rejected; return false; }
	const auto ids=selection(logic,m.getPlayerIndex());
	// An assistant set explicitly (setFormationAssistant: the unit tests' synthetic worlds) wins over the registered world's, so a retail
	// world left registered by another test cannot replace it; the game sets none and uses the world's FormationAssistant.ini
	const auto &assistant=m_formations?*m_formations:world->formations();
	const auto slots=assistant.layout(logic,ids,type->integer,dest->location);
	if(slots.empty()){++m_stats.rejected;return false;}
	const float c=SimMath::cosDet(SimMath::addf32(angle->real,1.5707964f)),s=SimMath::sinDet(SimMath::addf32(angle->real,1.5707964f));
	for(const auto &slot:slots) {
		Coord3D position{SimMath::subf32(SimMath::addf32(dest->location.x,SimMath::mulf32(slot.offset.x,c)),SimMath::mulf32(slot.offset.y,s)),SimMath::addf32(SimMath::addf32(dest->location.y,SimMath::mulf32(slot.offset.x,s)),SimMath::mulf32(slot.offset.y,c)),0};
		// The preview centres occupied ranks; the horde object origin need not.
		if(const auto *object=logic.findObjectByID(slot.object)) {
		 const auto center=FormationAssistant::rankCenter(*object);
		 const float facingCos=SimMath::cosDet(angle->real),facingSin=SimMath::sinDet(angle->real);
		 position.x=SimMath::subf32(position.x,SimMath::subf32(SimMath::mulf32(center.x,facingCos),SimMath::mulf32(center.y,facingSin)));
		 position.y=SimMath::subf32(position.y,SimMath::addf32(SimMath::mulf32(center.x,facingSin),SimMath::mulf32(center.y,facingCos)));
		}
		position.z=logic.getGroundHeight(position.x,position.y);
		// PR #88 review fix 4, issue #100: with the attack flag set, groupMoveToPosition arms each member's attack-move toward the slot goal its oriented
		// move installed (RW 0x94E72B -> RW 0x770CA5), and only when that move was accepted; a refused member keeps its earlier intent
		AIGroup group(logic,{slot.object});group.groupMoveToPosition(position,false,CMD_FROM_PLAYER,true,angle->real,attack->boolean);
	}
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
