// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane PHYS-1: Pathfinder::findAttackPath, the search for a cell from which the attacker's weapon reaches its victim (ZH AIPathfind.cpp findAttackPath; RotWK body
// RW 0x6FC18E, pathfinder vtable 0xC1B8D8 slot +8, the ship variant RW 0x6FDE38 at +0xC is not ported).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; review r2 of lane PHYS-1):
//   1. without the path-through flag: a short forward probe. dir = normalize2(target - position) * 10; for i = 1 .. 9 the point position + i * dir * 0.5: its cell
//      (RW 0x6E8CE6, centred) must be inside the logical extent for a human player, a valid movement cell (RW 0x6E8200) and a free footprint (RW 0x6F3082:
//      checkDestination with no other unit's goal in it); the first point from which the weapon reaches with an extra 10 (RW 0x6CC07C(obj, point, victim, pos, 10, 1))
//      and the view is not blocked (RW 0x6F4557) ends a two-node path (position -> point);
//   2. the A* search: the goal for the heuristic is the victim's position, moved toward the attacker by the victim's bounding circle + 10 (unless RW 0x441B59);
//      the start cell is the position (+5, +5 for a centred footprint); the search pops cells in heap order: a cell from which the weapon reaches (RW 0x6CC07C, extra
//      0) and whose footprint checkDestination accepts (RW 0x6F1584, its occupancy count) is an endpoint unless it lies within 5 (25 squared) of the start; an endpoint
//      with a count below 2 ends the search at once, else the lowest count is kept; every popped cell is expanded (RW 0x6F9850) while fewer than
//      MaxCellsFindAttackPath (GameData +0x1214) cells were examined; with no endpoint the cell nearest the goal is used and `fallback` is set;
//   3. the path is built by RW 0x6F1A31 (buildActualPath).
// INFERENCE (stop S-784): the view test RW 0x6F4557 is not ported (S-325: never blocked); RW 0x441B59's condition on the goal shift and RW 0x68C9AC's KindOf 150 goal
// are not read (the shift is always made); the closest-cell fallback ranks by the distance to the goal cell only (RW also ranks by RW 0x93AD93 of the cell, a zone
// value, when the zone seed RW 0x6F9829 succeeded); RW 0x6F9850's range limit (attack distance + 30) and seed arguments are not passed (the port's expansion has
// none); the occupancy count is the port's checkDestination count (allied goals block, S-164); a popped cell is always closed (RW 0x6F6286 is skipped with
// the path-through flag; the port's records would not return to the pool).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

#include <limits>

Path *Pathfinder::findAttackPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const PathfindObject *victim, const Coord3D *victimPos,
	float victimRadius, const PathfindAttackRange &range, bool pathThrough, bool *fallback)
{
	if (fallback)
	{
		*fallback = false;
	}
	if (!m_isMapReady || obj == nullptr)
	{
		return nullptr;
	}
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(obj, radius, center);
	const PathfindMovement mv = makeMovement(obj, loco);
	const bool isHuman = !obj->isComputerControlled();
	const Coord3D pos = *from;
	const Coord3D target = victim ? victim->getPosition() : *victimPos;
	// 1. the forward probe
	if (!pathThrough)
	{
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
			if (range.inRangeFrom(p, 10.0f))
			{
				Path *path = new Path();
				path->prependNode(&p, LAYER_GROUND);
				path->prependNode(&pos, obj->getLayer());
				path->optimize(*this, obj, loco.validSurfaces, false, nullptr);
				return path;
			}
		}
	}
	// 2. the search
	Coord3D goal = target;
	if (victim)
	{
		float ux = SimMath::subf32(pos.x, target.x), uy = SimMath::subf32(pos.y, target.y);
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
	Coord3D start = pos;
	if (center)
	{
		start.x = SimMath::addf32(start.x, 5.0f);
		start.y = SimMath::addf32(start.y, 5.0f);
	}
	ICoord2D startNdx;
	worldToCell(&start, true, &startNdx);
	PathfindCell *parentCell = getClippedCell(obj->getLayer(), &start);
	if (parentCell == nullptr || !obj->hasAI())
	{
		return nullptr;
	}
	// review r3: the shifted point only seeds the zone search (RW 0x6F9829 at 0x6FC4E4, not ported: S-784); the goal cell is the target's own position (RW 0x6FC661 ..
	// 0x6FC68B: the target object's + 0x38, or the supplied position)
	(void)goal;
	ICoord2D goalNdx;
	Coord3D goalPos = target;
	worldToCell(&goalPos, true, &goalNdx);
	PathfindCell *goalCell = getCell(LAYER_GROUND, goalNdx.x, goalNdx.y);
	if (goalCell == nullptr)
	{
		return nullptr;
	}
	if (!goalCell->allocateInfo(m_pool, goalNdx))
	{
		return nullptr;
	}
	if (parentCell != goalCell && !parentCell->allocateInfo(m_pool, startNdx))
	{
		goalCell->releaseInfo(m_pool);
		return nullptr;
	}
	m_isTunneling = false;
	parentCell->startPathfind(goalCell);
	m_open.clear();
	m_closedList = nullptr;
	parentCell->setTotalCost(parentCell->costToGoal(goalCell));
	m_open.push(parentCell);
	m_shortcutCounter = 0;
	PathfindCell *best = nullptr, *closest = nullptr;
	int bestCount = 9999999;
	int closestD2 = std::numeric_limits<int>::max();
	int examined = 0;
	Path *result = nullptr;
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		// every popped cell is closed at once (review r3): its record returns to the pool on success, failure and an exhausted budget alike. RW skips the close
		// (RW 0x6F6286) with the path-through flag (S-784)
		markClosed(c);
		const int gx = (int)goalCell->getXIndex() - (int)c->getXIndex(), gy = (int)goalCell->getYIndex() - (int)c->getYIndex();
		const int d2 = gx * gx + gy * gy;
		if (d2 < closestD2)
		{
			closestD2 = d2;
			closest = c;
		}
		Coord3D centre;
		adjustCoordToCell(c->getXIndex(), c->getYIndex(), center, centre, c->getLayer());
		int crowd = 0;
		if (range.inRangeFrom(centre, 0.0f) && checkDestination(obj, c->getXIndex(), c->getYIndex(), c->getLayer(), radius, center, &crowd, false))
		{
			bool nearStart = false;
			if (c != parentCell)
			{
				const float sx = SimMath::subf32(centre.x, start.x), sy = SimMath::subf32(centre.y, start.y);
				nearStart = SimMath::addf32(SimMath::mulf32(sy, sy), SimMath::mulf32(sx, sx)) < 25.0f;
				if (!nearStart)
				{
					if (crowd < 2)
					{
						best = c;
						bestCount = -1;
						break;
					}
					if (crowd < bestCount)
					{
						best = c;
						bestCount = crowd;
					}
				}
			}
		}
		if (examined < m_config.findAttackPathLimit)
		{
			examined += examineNeighboringCells(c, goalCell, loco, isHuman, center, radius, startNdx, obj);
		}
	}
	PathfindCell *end = best;
	if (end == nullptr && closest != nullptr)
	{
		end = closest;
		if (fallback)
		{
			*fallback = true;
		}
	}
	if (end)
	{
		result = buildActualPath(obj, loco.validSurfaces, from, end, center, false);
	}
	cleanOpenAndClosedLists();
	parentCell->releaseInfo(m_pool);
	if (goalCell != parentCell)
	{
		goalCell->releaseInfo(m_pool);
	}
	return result;
}
