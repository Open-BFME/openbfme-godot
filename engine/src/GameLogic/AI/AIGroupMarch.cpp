// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (AIGroup.cpp friend_computeGroundPath,
// friend_moveFormationToPos, getCenter, getMinMaxAndCenter).
//
// The formation march of a group (lane BACKLOG-8 r2): RotWK's generic group move (RW 0x774897) hands a player's group of more than one unit to the
// gather-and-move (RW 0x7747DE) when AIData UseFormations is set; that lays the group out (AIGroup::createFormation RW 0x94FCBA, AIGroupFormations.cpp)
// along the group's ground path (RW 0x76F115) and sends every unit along the path at its slot (RW 0x77348C), each through the march state 54
// (AIFollowPathAsTeam.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp's byte-matched sources are the donors, tier B same-shape unless noted):
//   * RW 0x774897's head (RW 0x7748C1 .. 0x774986): the tighten test: with no target, a player source and GameData GroupMoveClickToGatherAreaFactor (+ 0xB60)
//     above 0, the group's min / max rectangle (RW 0x76EEC1) scaled about its middle by the factor (RW 0x65F4DC: each side moves by (factor - 1) / 2 of
//     the size) containing the point (RW 0x65F4A2, inclusive) means a tighten; then a group of more than one unit, no tighten, no waypoint, no target,
//     a player source, no formation template (-1) and AIData UseFormations (+ 0xBD) go to RW 0x7747DE(pos, player, mode 0, the ignored obstacle, ground 1);
//     its true ends the move.
//   * RW 0x7747DE (decomp BfmeConv986.cpp, tier A): RW 0x76FFF0 drops the members that are held (Object + 0x1C8 bit 3), have no AI, no current locomotor or
//     are KindOf IMMOBILE (template + 0x108 bit 2), one at a time; a group whose last member would go answers true (nothing moves). Without a member whose
//     locomotor has a FormationPriority (+ 0x78 != 0), false. Then RW 0x76F426 (every member gives up its pathfinder goal, RW 0x68B401, a horde its members'
//     goals as well, HordeContain slot 52 RW 0x86F0F1), the ground path (RW 0x76F115; false ends with false), the formation (RW 0x94FCBA), the march
//     (RW 0x77348C with keepFormation false), true.
//   * RW 0x76F115 computeGroundPath (decomp AIGroupGetCenter.cpp): no march into a destination off the ground layer or on the passage plane (W3DTerrainLogic
//     slot 0xC8, RW 0x4627C1); the min / max / centre of the members not held with an AI; the path start the centre, the end the point; every member gives
//     up its pathfinder goal (RW 0x68B401); a member not held, with an AI and not AIRCRAFT counts: INFANTRY, MONSTER, CAVALRY or MACHINE (template + 0x108
//     bits 8, 10, 9 / 11) remember it by kind; the smallest squared distance of a counted member to the point (from (4 * MinDistanceForGroup)^2) and the
//     member closest to the centre (from 10 times that); no such member: false; the centre moves onto that member when the line between them is blocked
//     (RW 0x6F2F8F); a group spread wider than 4 * MinDistanceForGroup takes its spread as the distance; nearer than MinDistanceForGroup: false; the
//     leader is the MONSTER, else the CAVALRY / MACHINE, else the INFANTRY one; none: false; an enemy within FormationEnemyDistance (+ 0x70) of the leader
//     (RW 0x701443 with 0xE4): false; the old path goes (RW 0x76EBA1: the end back to (10, 0, 0)), the ground path of diameter 4 from the centre (RW
//     0x6FAA6D); the start / end are the centre and the point, or the path's first two nodes; a node on the passage plane drops the path: false.
//   * RW 0x77348C moveFormationToPos (decomp AIGroupMoveFormation.cpp): see the comments in the body.
// INFERENCE / NOT PORTED (stop S-4462): the closest enemy of RW 0x701443 is the port's partition scan (alive ENEMIES within the range in 2D; the
// qualifiers 0xE4 are not read: S-1227 / S-326); the group's dirty recompute (RW 0x76F037: the speed, no reader here) is not ported; a horde's damage
// record (H + 0x298, HordeContain slot 36 RW 0x86EE52) is read from its members' bodies; the terrain fire system's burning cells are never marked
// (S-191); the ground layer only (S-161); the tighten move itself (groupTightenToPosition) is not ported: a tighten makes the port's ZH group move.

#include "GameLogic/AI/AIGroup.h"

#include "Common/GameCommon.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIGroupFormations.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

namespace
{
struct MarchKinds
{
	int infantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY"); // 8
	int cavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");   // 9
	int monster = ObjectTemplateInfoBuilder::kindOfIndex("MONSTER");   // 10
	int machine = ObjectTemplateInfoBuilder::kindOfIndex("MACHINE");   // 11
	int aircraft = ObjectTemplateInfoBuilder::kindOfIndex("AIRCRAFT"); // 12
	int immobile = ObjectTemplateInfoBuilder::kindOfIndex("IMMOBILE"); // 2
};
const MarchKinds &kinds()
{
	static const MarchKinds k;
	return k;
}

bool held(const Object &o)
{
	return (o.getDisabledMask() & (1u << DISABLED_HELD)) != 0; // Object + 0x1C8 bit 3
}

float sq(float v)
{
	return SimMath::mulf32(v, v);
}

// Object RW 0x68C933 (decomp ObjectRva0028C264.cpp, `n` seconds): the body took damage within the last n seconds; a horde asks its members (HordeContain
// slot 36 RW 0x86EE52: INFERENCE, its own record H + 0x298 is not ported)
bool damagedWithin(const Object &o, int seconds)
{
	const unsigned frames = (unsigned)(LOGICFRAMES_PER_SECOND * seconds);
	const unsigned now = o.logic().getFrame();
	if (const HordeContain *hc = dynamic_cast<const HordeContain *>(o.getContain()))
	{
		if (frames >= now)
		{
			return false;
		}
		for (const Object *m : *hc->getContainedItemsList())
		{
			if (m && damagedWithin(*m, seconds))
			{
				return true;
			}
		}
		return false;
	}
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(o.getBodyModule());
	if (!body || body->lastDamageFrame() == 0xFFFFFFFFu)
	{
		return false;
	}
	return body->lastDamageFrame() + frames >= now;
}
} // namespace

AIGroup::~AIGroup() = default;

const char *AIGroup::marchStopLine()
{
	return "[S-4462] the formation march (lane BACKLOG-8: RW 0x774897's head, 0x7747DE, 0x76F115, 0x6FAA6D, 0x77348C) runs for a player's group of more than "
		   "one unit with AIData UseFormations; INFERENCE: the closest enemy (RW 0x701443, qualifiers 0xE4) is the alive ENEMIES partition scan, a horde's "
		   "damage record is its members' bodies; NOT ported: the group's speed recompute (RW 0x76F037), the tighten move (a tighten makes the ZH group "
		   "move), the burning cells of the ground search (S-191), the bridge / wall layers (S-161), the attack march (mode 1, state 61)";
}

bool AIGroup::getCenter(Coord3D &center) const
{
	// the inline getCenter of RW 0x77348C (decomp AIGroupMoveFormation.cpp): the mobile members with an AI that are not held; with none of them, every member
	// that is not held
	int count = 0;
	center = Coord3D{ 0.0f, 0.0f, 0.0f };
	for (const Object *o : m_members)
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (held(*o) || !ai || ai->isImmobile())
		{
			continue;
		}
		center.x = SimMath::addf32(center.x, o->getPosition()->x);
		center.y = SimMath::addf32(center.y, o->getPosition()->y);
		center.z = SimMath::addf32(center.z, o->getPosition()->z);
		++count;
	}
	if (count == 0 && !m_members.empty())
	{
		for (const Object *o : m_members)
		{
			if (held(*o))
			{
				continue;
			}
			center.x = SimMath::addf32(o->getPosition()->x, center.x);
			center.y = SimMath::addf32(center.y, o->getPosition()->y);
			center.z = SimMath::addf32(center.z, o->getPosition()->z);
			++count;
		}
	}
	if (count == 0)
	{
		return false; // RW divides by 0 here (a NaN centre) and answers false
	}
	center.x = SimMath::divf32(center.x, (float)count);
	center.y = SimMath::divf32(center.y, (float)count);
	center.z = SimMath::divf32(center.z, (float)count);
	return true;
}

bool AIGroup::getMinMaxAndCenter(Coord2D &min, Coord2D &max, Coord3D &center) const
{
	// RW 0x76EEC1 (decomp AIGroupRva00372571.cpp)
	int count = 0;
	min.x = 1e10f;
	max.x = -1e10f;
	min.y = 1e10f;
	max.y = -1e10f;
	center = Coord3D{ 0.0f, 0.0f, 0.0f };
	int id = 0;
	for (const Object *o : m_members)
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (held(*o) || !ai)
		{
			continue;
		}
		const Coord3D &p = *o->getPosition();
		center.x = SimMath::addf32(center.x, p.x);
		center.y = SimMath::addf32(center.y, p.y);
		center.z = SimMath::addf32(center.z, p.z);
		min.x = min.x > p.x ? p.x : min.x;
		max.x = max.x < p.x ? p.x : max.x;
		min.y = min.y > p.y ? p.y : min.y;
		max.y = max.y < p.y ? p.y : max.y;
		if (count == 0)
		{
			id = ai->formationId();
		}
		++count;
	}
	if (count > 0)
	{
		center.x = SimMath::divf32(center.x, (float)count);
		center.y = SimMath::divf32(center.y, (float)count);
		center.z = SimMath::divf32(center.z, (float)count);
	}
	return id != 0 && count >= 2;
}

void AIGroup::releaseGroundPath()
{
	// RW 0x76EBA1: the path goes and the path end returns to (10, 0, 0)
	if (m_groundPath)
	{
		m_groundPath.reset();
		m_pathEnd = Coord3D{ 10.0f, 0.0f, 0.0f };
	}
}

bool AIGroup::removeInvalidMembers()
{
	// RW 0x76FFF0
	for (size_t i = 0; i < m_members.size();)
	{
		Object *o = m_members[i];
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		const bool reject = held(*o) || !ai || !ai->curLocomotor() || o->isKindOf((unsigned)kinds().immobile);
		if (!reject)
		{
			++i;
			continue;
		}
		if (m_members.size() == 1)
		{
			return false;
		}
		m_members.erase(m_members.begin() + (std::ptrdiff_t)i);
		i = 0;
	}
	return true;
}

bool AIGroup::computeGroundPath(const Coord3D &pos)
{
	// RW 0x76F115
	AIWorld *world = m_logic.aiWorld();
	if (!world || !world->mapReady() || m_members.empty())
	{
		return false;
	}
	Pathfinder &pf = world->pathfinder();
	const PathfindTerrain *terrain = pf.terrainView();
	if (terrain && terrain->passagePlane(pos)) // the ground layer only (S-161), then W3DTerrainLogic slot 0xC8
	{
		return false;
	}
	const GameLogicSettings &gs = m_logic.settings();
	Coord2D min, max;
	Coord3D center;
	getMinMaxAndCenter(min, max, center);
	float distSqr = sq(SimMath::mulf32(gs.minDistanceForGroup, 4.0f));
	m_pathStart = center;
	m_pathEnd = pos;
	float distSqrCenter = SimMath::mulf32(distSqr, 10.0f);
	Object *infantry = nullptr, *other = nullptr, *monster = nullptr, *centerUnit = nullptr;
	const MarchKinds &k = kinds();
	for (Object *o : m_members)
	{
		pf.removeGoal(world->adapterFor(*o)); // RW 0x68B401
		if (held(*o) || !o->getAIUpdateInterface() || o->isKindOf((unsigned)k.aircraft))
		{
			continue;
		}
		if (o->isKindOf((unsigned)k.infantry))
		{
			infantry = o;
		}
		else if (o->isKindOf((unsigned)k.monster))
		{
			monster = o;
		}
		else if (o->isKindOf((unsigned)k.cavalry) || o->isKindOf((unsigned)k.machine))
		{
			other = o;
		}
		const Coord3D &p = *o->getPosition();
		const float d = SimMath::addf32(sq(SimMath::subf32(p.y, pos.y)), sq(SimMath::subf32(p.x, pos.x)));
		if (distSqr > d)
		{
			distSqr = d;
		}
		const float dc = SimMath::addf32(sq(SimMath::subf32(p.y, center.y)), sq(SimMath::subf32(p.x, center.x)));
		if (!centerUnit || distSqrCenter > dc)
		{
			centerUnit = o;
			distSqrCenter = dc;
		}
	}
	if (!centerUnit)
	{
		return false;
	}
	AIUpdateInterface *cai = centerUnit->getAIUpdateInterface();
	if (pf.isLineBlocked(&world->adapterFor(*centerUnit), cai->locomotorInfo().validSurfaces, world->adapterFor(*centerUnit).getLayer(), *centerUnit->getPosition(), center)) // lane WALL-1: the unit's layer
	{
		center = *centerUnit->getPosition();
	}
	const float spread = SimMath::addf32(sq(SimMath::subf32(max.y, min.y)), sq(SimMath::subf32(max.x, min.x)));
	if (spread > sq(SimMath::mulf32(gs.minDistanceForGroup, 4.0f)))
	{
		distSqr = spread;
	}
	if (sq(gs.minDistanceForGroup) > distSqr)
	{
		return false;
	}
	Object *leader = infantry;
	leader = other ? other : leader;
	leader = monster ? monster : leader;
	if (!leader)
	{
		return false;
	}
	// RW 0x701443(leader, FormationEnemyDistance, 0xE4, 0, 0, 0): INFERENCE (S-4462) the alive ENEMIES within the distance (2D, from the leader's centre)
	{
		PartitionFilterFn enemy([&](Object &o) { return !o.isEffectivelyDead() && leader->getRelationship(o) == ENEMIES; });
		for (const PartitionHit &h : m_logic.partition().iterateObjectsInRange(*leader->getPosition(), gs.formationEnemyDistance, FROM_CENTER_2D, { &enemy }, ITER_SORTED_NEAR_TO_FAR))
		{
			(void)h;
			return false;
		}
	}
	releaseGroundPath();
	m_groundPath.reset(pf.findGroundPath(&world->adapterFor(*leader), &center, &pos, 4, gs.formationColumnWidth));
	m_pathStart = center;
	m_pathEnd = pos;
	if (m_groundPath)
	{
		PathNode *node = m_groundPath->getFirstNode();
		if (node)
		{
			if (node->getNextOptimized())
			{
				m_pathStart = *node->getPosition();
				m_pathEnd = *node->getNextOptimized()->getPosition();
			}
			for (; node; node = node->getNextOptimized())
			{
				if (terrain && terrain->passagePlane(*node->getPosition()))
				{
					releaseGroundPath();
					return false;
				}
			}
		}
	}
	return m_groundPath != nullptr;
}

bool AIGroup::gatherAndMoveInFormation(const Coord3D &pos, CommandSourceType source, int mode, bool ground)
{
	// RW 0x7747DE
	AIWorld *world = m_logic.aiWorld();
	if (!world)
	{
		return false;
	}
	if (!removeInvalidMembers())
	{
		return true; // RW 0x7747ED -> 0x77485E: the order is taken and nothing moves
	}
	bool any = false;
	for (Object *o : m_members)
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		any = any || (ai && ai->curLocomotor() && ai->curLocomotor()->getTemplate().m_formationPriority != 0);
	}
	if (!any)
	{
		return false;
	}
	m_logic.noteStop(marchStopLine());
	// RW 0x76F426: every member gives up its pathfinder goal (RW 0x68B401), a horde its members' as well (HordeContain slot 52, RW 0x86F0F1)
	if (world->mapReady())
	{
		for (Object *o : m_members)
		{
			world->pathfinder().removeGoal(world->adapterFor(*o));
			if (const HordeContain *hc = dynamic_cast<const HordeContain *>(o->getContain()))
			{
				for (Object *m : *hc->getContainedItemsList())
				{
					if (m)
					{
						world->pathfinder().removeGoal(world->adapterFor(*m));
					}
				}
			}
		}
	}
	if (!computeGroundPath(pos))
	{
		return false;
	}
	AIGroupFormations::createFormation(m_logic, m_members, m_pathStart, m_pathEnd);
	moveFormationToPos(pos, source, mode, ground, false);
	return true;
}

void AIGroup::moveFormationToPos(const Coord3D &pos, CommandSourceType source, int mode, bool ground, bool keepFormation)
{
	// RW 0x77348C
	AIWorld *world = m_logic.aiWorld();
	Coord3D center;
	if (!world || !getCenter(center))
	{
		return;
	}
	Pathfinder &pf = world->pathfinder();
	const PathfindTerrain *terrain = pf.terrainView();
	const GameLogicSettings &gs = m_logic.settings();
	const float rowDepth = gs.formationRowDepth; // AIData + 0xA4
	PathNode *startNode = nullptr, *endNode = nullptr;
	Coord3D endPoint = pos, startPoint = center, moveStart = center;
	float startDistance = SimMath::mulf32(rowDepth, 4.0f);
	const float endDistance = SimMath::mulf32(rowDepth, 2.0f);
	bool useMoveStart = false;
	if (m_members.empty())
	{
		return;
	}
	// the distance from the start before the march may turn: the deepest slot plus the unit's depth, half a row more; twice it for a group under fire
	float spacing = 0.0f;
	bool wider = false;
	for (Object *o : m_members)
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		const Coord2D off = ai ? ai->formationOffset() : Coord2D{};
		const float required = SimMath::addf32(SimMath::subf32(0.0f, off.x),
			SimMath::mulf32((float)world->movementInfo(*o->getTemplate()).formationDepth, rowDepth)); // template + 0x574
		spacing = spacing > required ? spacing : required;
		wider = damagedWithin(*o, 4) || wider; // RW 0x68C933(&info, 4)
	}
	spacing = SimMath::addf32(spacing, SimMath::mulf32(rowDepth, 0.5f));
	startDistance = startDistance > spacing ? startDistance : spacing;
	if (wider)
	{
		startDistance = SimMath::mulf32(startDistance, 2.0f);
	}
	if (m_groundPath && m_groundPath->getFirstNode())
	{
		startPoint = *m_groundPath->getFirstNode()->getPosition();
		float remaining = startDistance;
		Coord3D previous = startPoint;
		for (PathNode *node = m_groundPath->getFirstNode(); node; node = node->getNextOptimized())
		{
			float dx = SimMath::subf32(node->getPosition()->x, previous.x), dy = SimMath::subf32(node->getPosition()->y, previous.y);
			if (SimMath::addf32(sq(dx), sq(dy)) > sq(remaining))
			{
				useMoveStart = true;
				startNode = node;
				const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f)));
				if (len != 0.0f)
				{
					const float inv = SimMath::divf32(1.0f, len);
					dx = SimMath::mulf32(dx, inv);
					dy = SimMath::mulf32(dy, inv);
				}
				moveStart = previous;
				moveStart.x = SimMath::addf32(moveStart.x, SimMath::mulf32(dx, remaining));
				moveStart.y = SimMath::addf32(moveStart.y, SimMath::mulf32(dy, remaining));
				break;
			}
			previous = *node->getPosition();
			remaining = SimMath::subf32(remaining, SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f))));
		}
		endPoint = *m_groundPath->getLastNode()->getPosition();
		{
			const float dx = SimMath::subf32(endPoint.x, moveStart.x), dy = SimMath::subf32(endPoint.y, moveStart.y), dz = SimMath::subf32(endPoint.z, moveStart.z);
			if (SimMath::addf32(SimMath::addf32(sq(dx), sq(dy)), sq(dz)) < sq(endDistance))
			{
				useMoveStart = false;
			}
		}
		for (PathNode *node = m_groundPath->getFirstNode(); node; node = node->getNextOptimized())
		{
			const float dx = SimMath::subf32(node->getPosition()->x, endPoint.x), dy = SimMath::subf32(node->getPosition()->y, endPoint.y);
			if (SimMath::addf32(sq(dx), sq(dy)) > sq(endDistance))
			{
				endNode = node;
			}
		}
		for (PathNode *t = endNode; t; t = t->getNextOptimized())
		{
			if (t == startNode)
			{
				endNode = nullptr;
			}
		}
		if (!startNode || !endNode)
		{
			m_groundPath.reset(); // RW deletes the path without the end reset of RW 0x76EBA1
			startNode = endNode = nullptr;
		}
	}
	// the final facing: from the last node before the end (or the start) to the end, ACos of its x (negated for y < 0)
	Coord3D facingStart = startPoint;
	if (endNode)
	{
		facingStart = *endNode->getPosition();
	}
	float fx = SimMath::subf32(endPoint.x, facingStart.x), fy = SimMath::subf32(endPoint.y, facingStart.y), fz = SimMath::subf32(endPoint.z, facingStart.z);
	{
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(fx, fy, fz)));
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			fx = SimMath::mulf32(fx, inv);
			fy = SimMath::mulf32(fy, inv);
		}
	}
	float angle = (float)SimMath::acosDet((double)(fx < -1.0f ? -1.0f : (fx > 1.0f ? 1.0f : fx)));
	if (fy < 0.0f)
	{
		angle = SimMath::subf32(0.0f, angle);
	}
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
	if (terrain)
	{
		terrain->getExtent(loX, loY, hiX, hiY); // TheTerrainLogic slot 8
	}
	for (Object *unit : m_members)
	{
		if (held(*unit))
		{
			continue;
		}
		AIUpdateInterface *ai = unit->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		const Coord2D offset = ai->formationOffset();
		ObjectPathfindAdapter &adapter = world->adapterFor(*unit);
		const unsigned surfaces = ai->locomotorInfo().validSurfaces; // AI + 0x1DC
		bool pathClear = true;
		if (pf.isLineBlocked(&adapter, surfaces, adapter.getLayer(), *unit->getPosition(), startPoint))
		{
			pathClear = false;
			if (useMoveStart && !pf.isLineBlocked(&adapter, surfaces, adapter.getLayer(), *unit->getPosition(), moveStart))
			{
				pathClear = true;
			}
		}
		std::vector<Coord3D> path;
		Coord3D previous = startPoint;
		if (useMoveStart && pathClear)
		{
			Coord2D rotated = offset;
			AIGroupFormations::rotateOffset(previous, moveStart, rotated);
			previous = moveStart;
			Coord3D dest = moveStart;
			dest.x = SimMath::addf32(dest.x, rotated.x);
			dest.y = SimMath::addf32(dest.y, rotated.y);
			if (ground)
			{
				pf.adjustGroundPathPosition(moveStart, &dest);
			}
			path.push_back(dest);
		}
		if (startNode && pathClear)
		{
			for (PathNode *node = startNode; node;)
			{
				Coord3D dest = *node->getPosition();
				Coord2D rotated = offset;
				AIGroupFormations::rotateOffset(previous, dest, rotated);
				if (terrain && terrain->passagePlane(dest))
				{
					rotated.x = SimMath::mulf32(rotated.x, gs.narrowPassageScale); // AIData NarrowPassageScale (+ 0xAC)
					rotated.y = SimMath::mulf32(rotated.y, gs.narrowPassageScale);
				}
				dest.x = SimMath::addf32(dest.x, rotated.x);
				dest.y = SimMath::addf32(dest.y, rotated.y);
				if (ground)
				{
					pf.adjustGroundPathPosition(*node->getPosition(), &dest);
				}
				path.push_back(dest);
				if (node == endNode)
				{
					break;
				}
				node = node->getNextOptimized();
				previous = dest;
			}
		}
		Coord2D rotated = offset;
		AIGroupFormations::rotateOffset(previous, endPoint, rotated);
		if (terrain && terrain->passagePlane(endPoint))
		{
			rotated.x = SimMath::mulf32(rotated.x, gs.narrowPassageScale);
			rotated.y = SimMath::mulf32(rotated.y, gs.narrowPassageScale);
		}
		if (ai->curLocomotor() && ai->curLocomotor()->getTemplate().m_formationPriority == 0 && !keepFormation)
		{
			rotated.x = SimMath::mulf32(rotated.x, 0.0f); // NO_FORMATION: every such unit ends on the point itself
			rotated.y = SimMath::mulf32(rotated.y, 0.0f);
		}
		Coord3D dest = endPoint;
		dest.x = SimMath::addf32(dest.x, rotated.x);
		dest.y = SimMath::addf32(dest.y, rotated.y);
		if (ground)
		{
			pf.adjustGroundPathPosition(endPoint, &dest);
		}
		const PathfindLocomotorInfo info = ai->locomotorInfo();
		if (!pf.adjustDestination(adapter, info, &dest, nullptr))
		{
			if (!(dest.x > loX && dest.x < hiX && dest.y > loY && dest.y < hiY))
			{
				dest = endPoint;
				pf.adjustDestination(adapter, info, &dest, nullptr);
			}
		}
		pf.updateGoal(adapter, &dest, pf.getLayerAt(dest.x, dest.y)); // RW 0x68B3BD (lane WALL-1: the destination's layer, as AIMoveToState::onEnter)
		path.push_back(dest);
		// mode 1 (the attack march, command 0x25 / RW 0x695C42 0xF) is not ported (S-4463): the port's callers ask for mode 0 only
		(void)mode;
		if (!ai->canFollowTeamPath())
		{
			ai->aiMoveToPosition(dest, source);
		}
		else
		{
			ai->aiFollowPathAsTeam(path, nullptr, angle, source);
		}
	}
}
