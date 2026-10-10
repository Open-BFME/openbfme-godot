// OpenBFME. GPL-3.0.
//
// Lane MOVE-3 (community feedback FB-0012: hostile hordes walking through each other). The blocked unit's path patch as RotWK computes it: the blocked-repath
// handler RW 0x6631BF (AIMove.cpp) walks its path ahead in steps of 10 to the first point whose footprint is free (RW 0x6EDFD7, here patchPointIsFree), asks
// the pathfinder's vtable 0xC1B8D8 slot 0x18 for a patch from its own cell to that point or to a cell of one of the next path nodes (RW 0x6F7938, patchPath),
// and splices the patch into its path (RW 0x767A66, Path::splicePatch). The port replaced all of this with a full findPath to the end of the path (S-166),
// whose line-of-sight optimisation (RW Path::optimize with `blocked` false) drops every detour around units: a blocked horde got a straight path through
// the enemy horde it had collided with, remembered it as a collider and never stopped for it again.
//
// TARGET FACTS (RW game.dat, S-001 caveat), read with Ghidra and capstone:
//   * RW 0x6F7938 (patchPath): pathfinder + 0x48 (the ignored obstacle) is cleared; the footprint radius / centring of RW 0x6ED071 (the unit's own footprint:
//     no horde narrowing); the cells of the unit (RW 0x6E8CE6 with that centring) and of the point; equal cells: no patch. The movement record RW 0x6EA04D;
//     the point's cell on the point's layer (RW 0x680A75 / 0x5E2E9C) must exist and pass validMovementPosition (RW 0x6E8200); the unit's clipped cell
//     (RW 0x6EA076). Both get a cell record (RW 0x6E9FF8, an existing record has + 0xC cleared), the start's pathfind starts towards the point's cell
//     (RW 0x934400), its total is costToGoal (RW 0x6F1F0D) and it is pushed (RW 0x6F57A1). The targets are the point's cell and the cells (RW 0x5E2EF2) of
//     the raw nodes (node + 0) after the given node, at most 20 in all. A popped cell (RW 0x6F4AF2) that is a target ends the search: a new Path
//     (RW 0x765A1A) gets the cells from the unit's position (RW 0x6EF591) and is marked optimised (+ 0xC = 1) as it is; else the cell is closed
//     (RW 0x934594), the search ends once more than 3000 cell records were taken, the layer links of the cell are expanded (RW 0x6F6286: bridge / wall
//     layers and portals, S-161: none here), then the eight neighbours (RW 0xDA2514 / 0xDA24F4: the orthogonal four first, a diagonal only beside an
//     accepted orthogonal one) that exist, are neither open nor closed and share a layer: a neighbour whose footprint cost (RW 0x6ED46C, the cells the
//     parent's footprint does not cover) is negative or that fails validMovementPosition is closed; else its cost so far is the footprint cost + the step
//     (RW 0x934602) + 10 for a pinched cell (cell bit 16) more than 3 (manhattan) from the point's cell + 1000 for an obstacle cell (type 4), its
//     blocked-by-ally bit (info + 0x2C bit 0) is cleared, its parent set (RW 0x93442F) and its total is that plus costToGoal to the point's cell;
//   * RW 0x6ED46C (the footprint cost): over the footprint cells around the cell that the parent's footprint does not cover, for each cell with a record:
//     every ground goal (record + 0x14) of another unit that is not one of this unit's horde members (+ 0x27C), both with an AI, whose path priority
//     (RW 0x663F48) is at least this unit's: a unit that stands on its goal (RW 0x68B47F) makes the cell blocked (-1); without a locomotor (AI + 0x1F0) on
//     either side + 8; else + 4 when this unit (its time computed once) needs longer than the other to the cell's centre (RW 0x6ED049; the 2D distance
//     by the CRT sqrt over the locomotor's speed, RW 0x5E3F49); every position (+ 0x20) of such a unit of a priority at least this unit's blocks; with the
//     last argument false also every horde position (+ 0x24) of another unit of such a priority;
//   * RW 0x6EDFD7 (a free point): every cell of the footprint around the point's cell, from -r to r - 1 on each axis, exists and passes
//     validMovementPosition; checkForMovement (RW 0x6EB792) with the flags 1 for a HERO, else 0x10, and the AI's ignored obstacle passes; the footprint
//     cost with no parent and the horde positions counted is 0;
//   * RW 0x767A66 (the splice): RW 0x765598 with `opt` 0 finds the segment of the raw chain closest to the patch's last node; without one the patch is
//     dropped; else the nodes before that segment's start are deleted, the patch's last node is linked (next and next optimised) to the start's raw next,
//     the start is deleted, the path begins with the patch, and the follower restarts at its head (+ 0x10 = head, + 0x14 = 0).
// PORT NOTES: the occupant lists are the port's per-cell lists (OCC_GROUND_GOAL = + 0x14, OCC_POSITION = + 0x20, OCC_HORDE_POSITION = + 0x24); only the
// ground layer exists (S-161), so the layer tests of the expansion always pass and RW 0x6F6286 adds nothing.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"

#include <cstdlib>

namespace
{
// RW 0xDA2514 / 0xDA24F4: the neighbour deltas, orthogonal first
const int kDx[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
const int kDy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
// RW 0x6F7938's table at EBP - 0x5C: diagonal i (4 .. 7) needs the orthogonal neighbour kSide[i - 4] or kSide[i - 3] accepted
const int kSide[5] = { 0, 1, 2, 3, 0 };
const int kMaxPatchCells = 3000; // RW 0x6F7938: the literal compared with the count of new cell records
const int kMaxTargets = 20;      // RW 0x6F7938: the point's cell and up to 19 node cells
} // namespace

int Pathfinder::patchCellCost(const PathfindObject &obj, const ICoord2D *parent, const ICoord2D &cell, PathfindLayerEnum layer, int r, int n, bool skipHordePositions)
{
	// TARGET RW 0x6ED46C
	int px0 = 0, px1 = 0, py0 = 0, py1 = 0;
	if (parent)
	{
		px0 = parent->x - r;
		px1 = parent->x + n;
		py0 = parent->y - r;
		py1 = parent->y + n;
	}
	int cost = 0;
	float selfT = -1.0f; // RW 0xBD19DC: computed on the first goal that needs it
	const int selfPri = obj.hasAI() ? obj.aiPriority() : 0;
	for (int i = cell.x - r; i < cell.x + n; i++)
	{
		for (int j = cell.y - r; j < cell.y + n; j++)
		{
			if (parent && i >= px0 && i < px1 && j >= py0 && j < py1)
			{
				continue; // the parent's footprint already covered this cell
			}
			const PathfindCell *c = getCell(layer, i, j);
			if (!c || !c->hasInfo())
			{
				continue;
			}
			for (const PathfindOccupant *node = c->occupants(OCC_GROUND_GOAL); node; node = node->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
				if (!o || o == &obj || o->getContainer() == &obj)
				{
					continue;
				}
				if (!obj.hasAI() || !o->hasAI())
				{
					continue;
				}
				if (selfPri > o->aiPriority())
				{
					continue;
				}
				if (o->isParked())
				{
					return -1;
				}
				if (!obj.hasLocomotor() || !o->hasLocomotor())
				{
					cost += 8;
					continue;
				}
				Coord3D centre;
				adjustCoordToCell(cell.x, cell.y, cellCentred(obj), centre, LAYER_GROUND); // RW 0x6ED049(this unit, the candidate cell param_4, 1), not the scanned cell
				if (selfT < 0.0f)
				{
					selfT = obj.secondsToReach(centre.x, centre.y);
				}
				if (selfT > o->secondsToReach(centre.x, centre.y))
				{
					cost += 4;
				}
			}
			for (const PathfindOccupant *node = c->occupants(OCC_POSITION); node; node = node->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
				if (!o || o == &obj || o->getContainer() == &obj)
				{
					continue;
				}
				if (obj.hasAI() && o->hasAI() && selfPri <= o->aiPriority())
				{
					return -1;
				}
			}
			if (!skipHordePositions)
			{
				for (const PathfindOccupant *node = c->occupants(OCC_HORDE_POSITION); node; node = node->next)
				{
					const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
					if (!o || o == &obj)
					{
						continue;
					}
					if (obj.hasAI() && o->hasAI() && selfPri <= o->aiPriority())
					{
						return -1;
					}
				}
			}
		}
	}
	return cost;
}

bool Pathfinder::cellCentred(const PathfindObject &obj) const
{
	int r;
	bool centre;
	getRadiusAndCenter(&obj, r, centre); // RW 0x6ECFC5: the centring of RW 0x6ED071
	return centre;
}

bool Pathfinder::patchPointIsFree(PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &point)
{
	// TARGET RW 0x6EDFD7
	int r;
	bool centre;
	getRadiusAndCenter(&obj, r, centre);
	ICoord2D cell;
	worldToCell(&point, centre, &cell);
	const PathfindLayerEnum layer = obj.getLayer();
	const PathfindMovement mv = makeMovement(&obj, loco);
	for (int i = -r; i < r; i++)
	{
		for (int j = -r; j < r; j++)
		{
			const PathfindCell *c = getCell(layer, cell.x + i, cell.y + j);
			if (!c || !validMovementPosition(mv, c))
			{
				return false;
			}
		}
	}
	PathfindCheckMovement info;
	info.cell = cell;
	info.layer = layer;
	info.radius = r;
	info.center = centre;
	info.flags = obj.isKindOf(PK_HERO) ? 1u : 0x10u; // RW: template + 0x110 bit 26 (HERO) -> 1, else 0x10
	info.transient = false;
	info.ignoreId = obj.hasAI() ? obj.getIgnoredObstacleID() : PATHFIND_INVALID_ID;
	info.mv = mv;
	if (!checkForMovement(&obj, info, nullptr))
	{
		return false;
	}
	return patchCellCost(obj, nullptr, cell, layer, r, r + (centre ? 1 : 0), false) == 0;
}

Path *Pathfinder::patchPath(PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &point, const PathNode *node)
{
	// TARGET RW 0x6F7938
	note(pathstops::kPatchLayers);
	m_ignoreObstacleID = PATHFIND_INVALID_ID; // pathfinder + 0x48 = 0
	if (!m_isMapReady)
	{
		return nullptr;
	}
	int radius;
	bool centre;
	getRadiusAndCenter(&obj, radius, centre);
	const Coord3D from = obj.getPosition();
	ICoord2D startNdx, goalNdx;
	worldToCell(&from, centre, &startNdx);
	worldToCell(&point, centre, &goalNdx);
	if (startNdx.x == goalNdx.x && startNdx.y == goalNdx.y)
	{
		return nullptr;
	}
	const PathfindMovement mv = makeMovement(&obj, loco);
	PathfindCell *targets[kMaxTargets];
	int targetCount = 1;
	PathfindCell *goalCell = getCell(LAYER_GROUND, goalNdx.x, goalNdx.y);
	targets[0] = goalCell;
	if (goalCell == nullptr || !validMovementPosition(mv, goalCell))
	{
		return nullptr;
	}
	PathfindCell *startCell = getClippedCell(obj.getLayer(), &from);
	if (startCell == nullptr)
	{
		return nullptr;
	}
	if (!goalCell->hasInfo())
	{
		if (!goalCell->allocateInfo(m_pool, goalNdx))
		{
			return nullptr;
		}
	}
	else
	{
		goalCell->clearParentCell();
	}
	if (!startCell->hasInfo())
	{
		ICoord2D s;
		worldToCell(&from, &s);
		if (!startCell->allocateInfo(m_pool, s))
		{
			goalCell->releaseInfo(m_pool);
			return nullptr;
		}
	}
	else
	{
		startCell->clearParentCell();
	}
	startCell->startPathfind(goalCell);
	startCell->setTotalCost(startCell->costToGoal(goalCell));
	m_open.clear();
	m_closedList = nullptr;
	m_open.push(startCell);
	// the cells of the raw nodes after `node` (RW: node + 0, at most 19 more targets)
	for (const PathNode *nd = node ? node->getNext() : nullptr; nd && targetCount < kMaxTargets; nd = nd->getNext())
	{
		PathfindCell *c = getCell(nd->getLayer(), nd->getPosition());
		if (c)
		{
			targets[targetCount++] = c;
		}
	}
	int newRecords = 0;
	Path *result = nullptr;
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		bool isTarget = false;
		for (int t = 0; t < targetCount; t++)
		{
			isTarget = isTarget || targets[t] == c;
		}
		if (isTarget)
		{
			result = new Path();
			prependCells(result, &from, c, centre);
			result->markOptimized(); // RW + 0xC = 1: the patch keeps every cell (no line-of-sight optimisation)
			break;
		}
		markClosed(c);
		if (newRecords > kMaxPatchCells)
		{
			break;
		}
		bool accepted[4] = { false, false, false, false };
		for (int i = 0; i < 8; i++)
		{
			ICoord2D nc;
			nc.x = (int)c->getXIndex() + kDx[i];
			nc.y = (int)c->getYIndex() + kDy[i];
			PathfindCell *n = getCell(c->getLayer(), nc.x, nc.y);
			if (n == nullptr || (n->hasInfo() && (n->getOpen() || n->getClosed())))
			{
				continue;
			}
			if (i >= 4 && !accepted[kSide[i - 4]] && !accepted[kSide[i - 3]])
			{
				continue; // a diagonal step only beside an accepted orthogonal one
			}
			const ICoord2D parentNdx = { (int)c->getXIndex(), (int)c->getYIndex() };
			const int occ = patchCellCost(obj, &parentNdx, nc, c->getLayer(), radius, radius + (centre ? 1 : 0), true);
			if (occ < 0 || !validMovementPosition(mv, n))
			{
				if (!n->hasInfo())
				{
					if (!n->allocateInfo(m_pool, nc))
					{
						continue;
					}
				}
				else
				{
					n->clearParentCell();
				}
				markClosed(n);
				continue;
			}
			if (i < 4)
			{
				accepted[i] = true;
			}
			if (!n->hasInfo())
			{
				++newRecords;
				if (!n->allocateInfo(m_pool, nc))
				{
					continue;
				}
			}
			else
			{
				n->clearParentCell();
			}
			unsigned step = n->costSoFar(c);
			const int gdx = std::abs(nc.x - (int)goalCell->getXIndex()), gdy = std::abs(nc.y - (int)goalCell->getYIndex());
			if (n->getPinched() && gdx + gdy > 3)
			{
				step += 10;
			}
			n->setBlockedByAlly(false);
			const unsigned rem = n->costToGoal(goalCell);
			if (n->getType() == PathfindCell::CELL_OBSTACLE)
			{
				step += 1000;
			}
			n->setCostSoFar((unsigned)(std::uint16_t)((std::uint16_t)occ + (std::uint16_t)step));
			n->setParentCell(c);
			n->setTotalCost((unsigned)(std::uint16_t)((std::uint16_t)n->getCostSoFar() + (std::uint16_t)rem));
			m_open.push(n);
		}
	}
	cleanOpenAndClosedLists();
	startCell->releaseInfo(m_pool); // a record still holding occupants or on a list is kept (PathfindCell::releaseInfo)
	for (int t = 0; t < targetCount; t++)
	{
		targets[t]->releaseInfo(m_pool); // the target the search ended on was neither open nor closed
	}
	return result;
}
