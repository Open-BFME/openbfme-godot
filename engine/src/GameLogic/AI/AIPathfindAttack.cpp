// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane PHYS-1, completed by lane ATTACKPATH-1: Pathfinder::findAttackPath, the search for a cell from which the attacker's weapon reaches its victim (ZH AIPathfind.cpp
// findAttackPath; RotWK body RW 0x6FC18E, pathfinder vtable 0xC1B8D8 slot +8, tier B to BFME2 0x6FAD13 whose decomp source is the near-matching draft
// reverse/attempts/0x002fad13.cpp (2117 / 2117 bytes, not yet byte-matched): every step below was read from the RotWK disassembly, the draft supplied the names.
// The ship variant RW 0x6FDE38 (slot +0xC, decomp PathfinderSidewaysAttack.cpp) is not ported: ships are not modelled by the pathfinder.)
//
// TARGET FACTS (RotWK game.dat, caveat S-001; lane PHYS-1 review r2 / r3, lane ATTACKPATH-1):
//   0. the map not ready -> null (before *fallback is cleared); *fallback = 0; the mineshaft fields + 0x1C1D8 / + 0x1C1DC = 0; crusher = crusher level > 0
//      (RW 0x695070, passed to the corridor); the footprint (RW 0x6ED071); the movement (RW 0x6EA04D); human = not a computer player (player + 0x5C == 1);
//   1. without the path-through flag: the forward probe. dir = normalize(target - unit) (RW 0x403175, z = 0) * 10; for i = 1 .. 9 the point unit + i * dir * 0.5
//      (SSE): its cell (RW 0x6E8CE6, centred) must be inside the logical extent for a human player, exist on the unit's layer and be a valid movement cell
//      (RW 0x6E8200), with a free footprint (RW 0x6F3082); the first point from which the weapon reaches with an extra 10 (RW 0x6CC07C(obj, point, victim, pos,
//      10, 1)) and the view is not blocked (RW 0x6F4557) ends a two-node path (unit -> point; the first node's optimised link set by hand);
//   2. the goal of the corridor: the given position; for a victim object a WALL_UPGRADE victim's point (RW 0x68C9AC) and, unless the weapon is a MeleeWeapon
//      (RW 0x441B59, template + 0x125), the goal moves toward the unit by the victim's bounding radius (+ 0xB8) + 10 along Normalize(unit - victim) (RW 0x405553);
//   3. the waypoint vector cleared (RW 0x8E6731), the block flags cleared (RW 0x937C23), the corridor FindClosestHierarchicalPath (RW 0x6F9829(human, locomotors,
//      unit, from, goal, crusher, goal)); none -> every block passable (RW 0x937C65);
//   4. the attack distance (int) Weapon::getAttackDistance(unit, victim, pos) (RW 0x6CA974, _ftol RW 0xA3CFA4) + 30 limits the expansion; the start = the unit's
//      position (+5, +5 for a centred footprint), its cell (RW 0x6EA076) on the unit's layer; none, or a unit without an AI (+ 0x260) -> null; the start's record
//      (RW 0x6E9FF8; an existing one has + 0xC cleared) starts with no goal (RW 0x934400(0)); the goal cell = the victim object's own position (+ 0x38) or the
//      given position (RW 0x6E8CE6, centred, RW 0x5E2E9C on the ground layer: none off the grid -> null), its record; the start is pushed (RW 0x6F57A1);
//   5. the fallback key starts at (corridor index 0, 0.9f (RW 0xBDE198) * (float)(dx^2 + dy^2) of the start cell to the goal cell, SSE); the view is tested
//      only when there is no victim object or it is not significantly above the terrain (RW 0x70C55D); the towards-goal counter is reset (RW 0xDE4B14);
//   6. every popped cell (RW 0x6F4AF2): with a corridor, its index (RW 0x93AD93) and (float)(dx^2 + dy^2) to the goal cell replace the fallback when the index is
//      higher, or equal with a smaller distance; a cell reached through no waypoint (record + 0xC, always 0 here: S-161) from whose centre (RW 0x6EA0A1) the
//      weapon reaches (extra 0) and whose footprint checkDestination (RW 0x6F1584) accepts, with its occupancy count: blocked = the view test (when tested);
//      the start cell itself is never an endpoint, a centre within 5 (squared 25, RW 0xC69574, SSE y*y + x*x) of the start is blocked; an endpoint with a
//      count <= 1 is closed and ends the search at once (RW 0x6FC91D); a lower count than the best so far becomes the acceptable cell; the cell is closed
//      (RW 0x934594); while fewer than MaxCellsFindAttackPath (GameData + 0x1214, unsigned) were examined: the layer links (RW 0x6F6286, without the
//      path-through flag) and the neighbours (RW 0x6F9850 with the attack distance and the corridor) are expanded;
//   7. the acceptable cell, else the fallback cell (*fallback = 1), else null; the path RW 0x6F1A31(unit, surfaces, unit position, cell, centred, 0, 1).
// INFERENCE / NOT PORTED (stop S-784): RW 0x68C9AC's transformed point of a WALL_UPGRADE victim is its position (the goal only seeds the corridor); the
// occupancy count is the port's checkDestination count (allied goals block, S-164); a retail path keeps the goal cell's record when it was never opened or
// closed (RW releases it only on the null branch), the port gives it back (no observable difference: an occupant-free record carries nothing); the
// path is built and optimised by the port's buildActualPath (RW's last argument 1 is not identified).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"

#include <limits>

namespace
{
// RW 0x6E8CE6 with the centring flag 1 (no clamp)
inline int cellIndexCentred(float v)
{
	return SimMath::floorToInt(SimMath::mulf32(v, 0.1f));
}
} // namespace

namespace
{
// lane UBER-3P (Uber profile only): a search with no endpoint ends once its closest cell held for this many examined cells (internalFindPath's
// kUberStallCells)
constexpr int kUberAttackStallCells = 256;
} // namespace

Path *Pathfinder::findAttackPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const PathfindObject *victim, const Coord3D *victimPos,
	float victimRadius, const PathfindAttackRange &range, bool pathThrough, bool *fallback)
{
	if (!m_isMapReady)
	{
		return nullptr;
	}
	if (fallback)
	{
		*fallback = false;
	}
	if (obj == nullptr)
	{
		return nullptr;
	}
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(obj, radius, center);
	const PathfindMovement mv = makeMovement(obj, loco);
	const bool isHuman = !obj->isComputerControlled();
	const Coord3D pos = obj->getPosition();
	// 1. the forward probe
	if (!pathThrough)
	{
		const Coord3D target = victim ? victim->getPosition() : *victimPos;
		float dx = SimMath::subf32(target.x, pos.x), dy = SimMath::subf32(target.y, pos.y);
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(dx, dy)));
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(dx, inv);
			dy = SimMath::mulf32(dy, inv);
		}
		dx = SimMath::mulf32(dx, 10.0f);
		dy = SimMath::mulf32(dy, 10.0f);
		for (int i = 1; i <= 9; ++i)
		{
			const float fi = SimMath::sseFromInt32(i);
			const Coord3D p{ SimMath::addf32(SimMath::mulf32(SimMath::mulf32(fi, dx), 0.5f), pos.x), SimMath::addf32(SimMath::mulf32(SimMath::mulf32(fi, dy), 0.5f), pos.y),
				pos.z };
			ICoord2D c;
			worldToCell(&p, true, &c);
			if (isHuman && (c.x < m_logicalExtent.lo.x || c.y < m_logicalExtent.lo.y || c.x > m_logicalExtent.hi.x || c.y > m_logicalExtent.hi.y))
			{
				break;
			}
			const PathfindCell *cell = getCell(LAYER_GROUND, c.x, c.y);
			if (cell == nullptr || !validMovementPosition(mv, cell))
			{
				break;
			}
			int crowd = 0;
			if (!checkDestination(obj, c.x, c.y, LAYER_GROUND, radius, center, &crowd, false) || crowd != 0)
			{
				break;
			}
			// lane ATTACKPATH-1: the view test RW 0x6F4557 after the reach (RW 0x6FC3BE)
			if (range.inRangeFrom(p, 10.0f) && !isAttackViewBlockedByObstacle(*obj, p, victim, *victimPos))
			{
				Path *path = new Path();
				path->prependNode(&p, LAYER_GROUND);
				path->prependNode(&pos, obj->getLayer());
				path->optimize(*this, obj, loco.validSurfaces, false, nullptr);
				return path;
			}
		}
	}
	// 2. the corridor's goal
	Coord3D goal = *victimPos;
	if (victim && !range.meleeWeapon())
	{
		// RW 0x6FC436 .. 0x6FC4AC (SSE; RW 0x405553 normalises with z = 0)
		float ux = SimMath::subf32(pos.x, victim->getPosition().x), uy = SimMath::subf32(pos.y, victim->getPosition().y);
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(ux, uy)));
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			ux = SimMath::mulf32(ux, inv);
			uy = SimMath::mulf32(uy, inv);
		}
		const float k = SimMath::addf32(victimRadius, 10.0f);
		goal.y = SimMath::addf32(SimMath::mulf32(uy, k), goal.y); // the binary's order: y, x (the z term is 0)
		goal.x = SimMath::addf32(SimMath::mulf32(k, ux), goal.x);
	}
	// 3. the corridor (lane ATTACKPATH-1: RW 0x6FC4B1 .. 0x6FC4F9; closes the feature map's perf trap "findAttackPath's own corridor", S-162)
	m_portalHops.clear(); // lane WALL-2: only this search's corridor leaves hops (review #64: a previous search's hops steered costToGoal; INFERENCE as findPath, RW 0x6FE7CB)
	m_zoneManager.clearPassableFlags();
	Coord3D adjustedGoal = goal;
	const bool hierarchical = findClosestHierarchicalPath(const_cast<PathfindObject *>(obj), loco, isHuman, from, &goal, &adjustedGoal);
	if (!hierarchical)
	{
		m_zoneManager.setAllPassable();
	}
	// 4. the start and the goal cell
	const int attackDistance = SimMath::truncToInt32(range.attackDistance()) + 30;
	Coord3D start = pos;
	if (center)
	{
		start.x = SimMath::addf32(start.x, 5.0f);
		start.y = SimMath::addf32(start.y, 5.0f);
	}
	ICoord2D startNdx{ cellIndexCentred(start.x), cellIndexCentred(start.y) };
	PathfindCell *parentCell = getClippedCell(obj->getLayer(), &start);
	if (parentCell == nullptr || !obj->hasAI())
	{
		return nullptr;
	}
	// RW 0x6E9FF8 or + 0xC := 0 on an existing record: the waypoint link, which the port's records do not have (S-161); the parent link is cleared by
	// RW 0x934400 below
	if (!parentCell->allocateInfo(m_pool, startNdx))
	{
		return nullptr;
	}
	m_isTunneling = false;
	parentCell->startPathfind(nullptr);
	const Coord3D target = victim ? victim->getPosition() : *victimPos;
	const ICoord2D goalNdx{ cellIndexCentred(target.x), cellIndexCentred(target.y) };
	PathfindCell *goalCell = getCell(LAYER_GROUND, goalNdx.x, goalNdx.y);
	if (goalCell == nullptr)
	{
		parentCell->releaseInfo(m_pool);
		return nullptr;
	}
	if (!goalCell->allocateInfo(m_pool, goalNdx))
	{
		parentCell->releaseInfo(m_pool);
		return nullptr;
	}
	m_open.clear();
	m_closedList = nullptr;
	m_open.push(parentCell);
	// 5. the fallback key and the view rule
	PathfindCell *acceptable = nullptr, *closest = nullptr;
	int acceptableCount = 9999999;
	int closestOrder = 0;
	float closestDistance;
	{
		const int dx = (int)goalCell->getXIndex() - (int)parentCell->getXIndex(), dy = (int)goalCell->getYIndex() - (int)parentCell->getYIndex();
		closestDistance = SimMath::mulf32(SimMath::sseFromInt32(dy * dy + dx * dx), 0.9f);
	}
	const bool checkView = victim == nullptr || !victim->isSignificantlyAboveTerrain();
	// lane WALL-2 with ATTACKPATH-1 (S-4858, OpenBFME's own): RotWK keeps the closest-cell fallback only with a corridor (the RW 0x93AD93 ranking below runs
	// only when RW 0x6F9829 found one). The port's corridor runs on the ground grid only (S-162; its raised branch RW 0x9389CE is S-4856), so a unit on a wall
	// top, a raised walkway or a ramp often gets none, and the search would then end with no path at all: the unit would wait in its approach for good (Grond
	// put on a Minas Tirith wall top, every wall cell blocking it: CanPathThroughGates = No). Such a search keeps the cell closest to the goal cell by its
	// squared cell distance instead, as the attack search did before the corridor was ported
	const bool rankByDistance = !hierarchical && obj->getLayer() != LAYER_GROUND;
	if (rankByDistance)
	{
		note(pathstops::kAttackRaisedFallback);
	}
	int closestD2 = std::numeric_limits<int>::max();
	m_shortcutCounter = 0;
	unsigned examined = 0;
	// lane UBER-3P (Uber profile only): the cell nearest the goal (squared cell distance) and the examined count when it last changed
	int stallD2 = std::numeric_limits<int>::max();
	unsigned stallAt = 0;
	Path *result = nullptr;
	bool done = false;
	// 6. the search
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		if (m_uberLimits)
		{
			const int sx = (int)goalCell->getXIndex() - (int)c->getXIndex(), sy = (int)goalCell->getYIndex() - (int)c->getYIndex();
			const int d2 = sx * sx + sy * sy;
			if (d2 < stallD2)
			{
				stallD2 = d2;
				stallAt = examined;
			}
		}
		Coord3D centre;
		adjustCoordToCell(c->getXIndex(), c->getYIndex(), center, centre, c->getLayer());
		if (rankByDistance)
		{
			const int gx = (int)goalCell->getXIndex() - (int)c->getXIndex(), gy = (int)goalCell->getYIndex() - (int)c->getYIndex();
			const int d2 = gx * gx + gy * gy;
			if (d2 < closestD2)
			{
				closestD2 = d2;
				closest = c;
			}
		}
		else if (hierarchical)
		{
			const int order = hierarchicalPositionOf(c->getXIndex(), c->getYIndex());
			const int gx = (int)goalCell->getXIndex() - (int)c->getXIndex(), gy = (int)goalCell->getYIndex() - (int)c->getYIndex();
			const float d = SimMath::sseFromInt32(gy * gy + gx * gx);
			if (order > closestOrder || (order == closestOrder && closestDistance > d))
			{
				closest = c;
				closestOrder = order;
				closestDistance = d;
			}
		}
		int crowd = 0;
		if (range.inRangeFrom(centre, 0.0f) && checkDestination(obj, c->getXIndex(), c->getYIndex(), c->getLayer(), radius, center, &crowd, false))
		{
			bool blocked = checkView ? isAttackViewBlockedByObstacle(*obj, centre, victim, *victimPos) : false;
			if (c != parentCell)
			{
				const float sx = SimMath::subf32(centre.x, start.x), sy = SimMath::subf32(centre.y, start.y);
				if (SimMath::addf32(SimMath::mulf32(sy, sy), SimMath::mulf32(sx, sx)) < 25.0f)
				{
					blocked = true;
				}
				if (!blocked)
				{
					if (crowd <= 1)
					{
						markClosed(c);
						result = buildActualPath(obj, loco.validSurfaces, &pos, c, center, false);
						done = true;
						break;
					}
					if (crowd < acceptableCount)
					{
						acceptable = c;
						acceptableCount = crowd;
					}
				}
			}
		}
		markClosed(c);
		// lane UBER-3P (Uber profile only, docs/UBER.md): with no endpoint yet and the cell nearest the goal unchanged for kUberAttackStallCells examined cells,
		// the search ends with the fallback the exhausted search gives (the path to the fallback cell). In the 30,000-troop clash 126 of 200 attack
		// searches found no endpoint and spent 75% of their cells after such a pause; no endpoint of the other 74 came after one
		if (m_uberLimits && acceptable == nullptr && examined - stallAt >= (unsigned)kUberAttackStallCells)
		{
			break;
		}
		if (examined < (unsigned)m_config.findAttackPathLimit)
		{
			if (!pathThrough)
			{
				checkChangeLayers(c, obj); // RW 0x6F6286
			}
			examined += (unsigned)examineNeighboringCells(c, goalCell, loco, isHuman, center, radius, startNdx, obj, hierarchical, attackDistance);
		}
	}
	// 7. the result
	if (!done)
	{
		if (acceptable)
		{
			result = buildActualPath(obj, loco.validSurfaces, &pos, acceptable, center, false);
		}
		else if (closest)
		{
			if (fallback)
			{
				*fallback = true;
			}
			result = buildActualPath(obj, loco.validSurfaces, &pos, closest, center, false);
		}
	}
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	parentCell->releaseInfo(m_pool);
	if (goalCell != parentCell)
	{
		goalCell->releaseInfo(m_pool);
	}
	return result;
}
