// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIGroup.h.

#include "GameLogic/AI/AIGroup.h"

#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{
const float kCellSize = 10.0f;                       // PATHFIND_CELL_SIZE_F
const float kWaypointClampMargin = 40.0f;             // ZH STD_WAYPOINT_CLAMP_MARGIN = PATHFIND_CELL_SIZE_F * 4 (exactly representable)
}

AIGroup::AIGroup(GameLogic &logic, const std::vector<ObjectID> &ids)
	: m_logic(logic)
{
	for (ObjectID id : ids)
	{
		Object *o = logic.findObjectByID(id);
		if (o && !o->isDestroyed() && o->getAIUpdateInterface())
		{
			m_members.push_back(o);
		}
	}
}

Coord3D AIGroup::clampWaypointPosition(const Coord3D &pos) const
{
	Coord3D p = pos;
	AIWorld *ai = m_logic.aiWorld();
	if (!ai || !ai->mapReady())
	{
		return p;
	}
	ICoord2D lo, hi;
	ai->pathfinder().getLogicalExtent(lo, hi);
	const float x0 = SimMath::addf32(SimMath::mulf32((float)lo.x, kCellSize), kWaypointClampMargin);
	const float y0 = SimMath::addf32(SimMath::mulf32((float)lo.y, kCellSize), kWaypointClampMargin);
	const float x1 = SimMath::subf32(SimMath::mulf32((float)(hi.x + 1), kCellSize), kWaypointClampMargin);
	const float y1 = SimMath::subf32(SimMath::mulf32((float)(hi.y + 1), kCellSize), kWaypointClampMargin);
	if (!(p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1))
	{
		if (p.x > x1) p.x = x1;
		else if (p.x < x0) p.x = x0;
		if (p.y > y1) p.y = y1;
		else if (p.y < y0) p.y = y0;
		p.z = m_logic.getGroundHeight(p.x, p.y);
	}
	return p;
}

const char *AIGroup::stopLine()
{
	return "S-223 AIGroup's force moves and waypoints (lane MOVE-3: not MSG_DO_MOVETO, S-1831) move with ZH's individual destinations (offset from the nearest unit clamped to "
		"six bounding radii): the BFME group manager's formation layout (RW 0x94FCBA), "
		"the ZH column and tighten moves and the weapon locks are not ported; a formation move's drag direction becomes the units' final facing (inference)";
}

Coord3D AIGroup::computeIndividualDestination(const Coord3D &groupDest, Object &obj, const Coord3D &center) const
{
	const Coord3D &pos = *obj.getPosition();
	float vx = SimMath::subf32(pos.x, center.x), vy = SimMath::subf32(pos.y, center.y);
	float length = SimMath::length2d(vx, vy);
	AIWorld *world = m_logic.aiWorld();
	const float radius = world ? world->adapterFor(obj).getGeometry().boundingCircleRadius() : 0.0f;
	const float maxLength = SimMath::mulf32(6.0f, radius);
	if (length > maxLength)
	{
		length = maxLength;
	}
	// Coord2D::normalize: x /= len, y /= len when len != 0 (the length is the one before the clamp)
	const float len0 = SimMath::length2d(vx, vy);
	if (len0 != 0.0f)
	{
		vx = SimMath::divf32(vx, len0);
		vy = SimMath::divf32(vy, len0);
	}
	vx = SimMath::mulf32(vx, length);
	vy = SimMath::mulf32(vy, length);
	Coord3D dest;
	dest.x = SimMath::addf32(groupDest.x, vx);
	dest.y = SimMath::addf32(groupDest.y, vy);
	dest.z = m_logic.getGroundHeight(dest.x, dest.y);
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	if (world && world->mapReady() && ai && ai->isDoingGroundMovement())
	{
		Pathfinder &pf = world->pathfinder();
		pf.adjustDestination(world->adapterFor(obj), ai->locomotorInfo(), &dest, &groupDest);
		pf.updateGoal(world->adapterFor(obj), &dest, LAYER_GROUND);
	}
	return dest;
}

void AIGroup::groupMoveToPosition(const Coord3D &posIn, bool addWaypoint, CommandSourceType source, bool haveFinalAngle, float finalAngle)
{
	AIWorld *world = m_logic.aiWorld();
	if (!world || m_members.empty())
	{
		return;
	}
	world->noteStop(stopLine());
	const Coord3D pos = clampWaypointPosition(posIn);
	// the units to move, nearest to the goal first (ZH sorts them near to far so the near units get the first paths)
	struct Entry
	{
		Object *obj;
		float distSq;
		size_t order;
	};
	std::vector<Entry> units;
	for (size_t i = 0; i < m_members.size(); ++i)
	{
		Object *o = m_members[i];
		if (o->isDestroyed())
		{
			continue;
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || ai->isImmobile())
		{
			continue;
		}
		// lane MODULES-3 r2: each unit's order passes aiDoCommand's gate (RW 0x667174) before anything of it changes: a refused unit keeps its reservation and
		// takes no part in the group's layout
		if (!ai->acceptCommand(source, addWaypoint || haveFinalAngle ? AIUpdateInterface::kCommandUnidentifiedMove : 0))
		{
			continue;
		}
		if (world->mapReady())
		{
			world->pathfinder().removeGoal(world->adapterFor(*o)); // ZH: every unit gives up its goal reservation first
		}
		const Coord3D &p = *o->getPosition();
		const float dx = SimMath::subf32(p.x, pos.x), dy = SimMath::subf32(p.y, pos.y);
		units.push_back(Entry{ o, SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), i });
	}
	std::stable_sort(units.begin(), units.end(), [](const Entry &a, const Entry &b) {
		if (a.distSq != b.distSq)
		{
			return a.distSq < b.distSq;
		}
		return a.obj->getID() < b.obj->getID();
	});
	Coord3D center = pos;
	bool first = true;
	for (const Entry &e : units)
	{
		Object *o = e.obj;
		if (first)
		{
			center = *o->getPosition(); // ZH: the group's reference point is the nearest unit
			first = false;
		}
		const Coord3D dest = computeIndividualDestination(pos, *o, center);
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!addWaypoint)
		{
			if (haveFinalAngle)
			{
				ai->aiMoveToPositionAndOrientate(dest, finalAngle, source);
			}
			else
			{
				ai->aiMoveToPosition(dest, source);
			}
		}
		else
		{
			ai->aiFollowPathAppend(dest, source);
		}
	}
}

const char *AIGroup::planningStopLine()
{
	return "S-1831 the group manager's move order (RW 0x75748C / 0x94E232) is executed at once and alone: the order queue's modes (player + 0x770, RW 0x7572FA), the "
		"chained orders (RW 0x7571A7 / 0x756F76), the order's completion test (slot 0x14 RW 0x94DC37), its save data, the SHIP / AMPHIBIOUS transport branch "
		"(RW 0x94DA57 -> 0x68F3F2 / 0x770D27) and the object + 0x27C branch (RW 0x754B18) are not ported";
}

void AIGroup::planningMoveToPosition(const Coord3D &pos, bool attackMove, CommandSourceType source)
{
	AIWorld *world = m_logic.aiWorld();
	if (!world || m_members.empty())
	{
		return;
	}
	world->noteStop(planningStopLine());
	for (Object *o : m_members)
	{
		if (o->isDestroyed())
		{
			continue;
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || ai->isContained()) // RW 0x94E184: RW 0x6939DF (a horde member) or no AI (+ 0x260): nothing
		{
			continue;
		}
		// lane MODULES-3 r2: the order passes aiDoCommand's gate (RW 0x667174) before anything of the unit changes
		if (!ai->acceptCommand(source, 0))
		{
			continue;
		}
		// RW 0x94DF97: the destination of a unit without an AI doing ground movement is the point itself
		Coord3D dest = pos;
		if (world->mapReady() && ai->isDoingGroundMovement())
		{
			Pathfinder &pf = world->pathfinder();
			PathfindObject &adapter = world->adapterFor(*o);
			Coord3D p{ pos.x, pos.y, m_logic.getGroundHeight(pos.x, pos.y) }; // RW TerrainLogic vslot 0x1C at the point's layer (only the ground exists, S-161)
			if (!pf.adjustDestination(adapter, ai->locomotorInfo(), &p, &pos))
			{
				p = pos;
			}
			if (adapter.isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
			{
				// RW 0x94E04E .. 0x94E083: Object + 0x44 plus RW 0x4B3D8D to the destination (x87 fadd, stored as a float), RW 0x68B3CF
				const float heading = SimMath::pc24Add(ObjectKnockback::relativeAngle2D(*o, p), o->getOrientation());
				pf.updateGoalAngle(adapter, &p, heading, LAYER_GROUND);
			}
			else
			{
				pf.updateGoal(adapter, &p, LAYER_GROUND); // RW 0x68B3BD
			}
			dest = p;
		}
		// RW 0x94DF1B: the order's attack flag (+ 0x24) picks the attack-move (RW 0x696266(dest, 0x7FFFFFFF, 0)), else aiMoveToPosition(dest, 0) (RW 0x66C4CA)
		ai->aiMoveToPosition(dest, source);
		if (attackMove)
		{
			ai->armAttackMove(ai->stateMachine().goalPosition()); // the port's attack-move (lane COMBAT-1, S-328)
		}
	}
}

void AIGroup::groupIdle(CommandSourceType source)
{
	for (Object *o : m_members)
	{
		if (o->isDestroyed())
		{
			continue;
		}
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			ai->aiIdle(source);
		}
	}
}
