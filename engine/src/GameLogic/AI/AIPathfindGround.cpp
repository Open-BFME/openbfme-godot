// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (AIPathfind.cpp findGroundPath, buildGroundPath,
// clearCellForDiameter, groundCellsCallback, Path::optimizeGroundPath, isGroundPathPassable).
//
// Lane BACKLOG-8 r2: the ground path a group's formation march follows (AIGroup::computeGroundPath RW 0x76F115 asks for it) and the line tests of the
// march (AIGroup::moveFormationToPos RW 0x77348C).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * clearCellForDiameter RW 0x6E8BBA (BFME2 decomp PathfinderClearCellForDiameter.cpp, tier A): radius = diameter / 2 (one cell for 0 / 1); above 1 the
//     square's corner cells are skipped; a cell fails off the map, when it is not CLEAR (an OBSTACLE cell passes only a crusher on a fence) or when a unit
//     both stands and has its goal there (a position occupant that is also a ground goal occupant, object id != 0) whose crushable level exceeds the
//     crusher level; a failing cell returns 0 (`exact`) or the answer for diameter - 2; else the diameter (1 for radius 0).
//   * findGroundPath RW 0x6FAA6D (BFME2 0x6F95F6; the decomp's attempt is not byte-matched: read from the RotWK disassembly): the zone manager's passable
//     flags cleared (RW 0x937C23), the hierarchical corridor of a human GROUND search (RW 0x6F8601(1, 1, ...)) or every block passable (RW 0x937C65); no
//     path to (0, 0) or before the map; the goal cell (centred, RW 0x6E8CE6) must clear the diameter, else the first of up to seven square rings
//     (+x, +y, -x, -x, -y, -y, +x, +x by the ring's offset) whose cell clears it; the start cell is the clipped cell of `from`; different effective
//     zones of a GROUND movement (the object's gate / human / slope / crusher fields) end the search. The A* expands the 8 neighbours of the popped cell
//     (dx RW 0xDA2594, dy RW 0xDA2574): a ground cell the zone manager does not mark passable needs a passable cell 3 away diagonally; a cell already
//     open or closed is skipped (no re-relaxation); a cell other than the goal must be CLEAR and not pinched, and a diagonal needs one of its two sides
//     taken; the cost is costSoFar (RW 0x934602) + the occupant cost (RW 0x6ED21E with the diameter) + 0.6 * 10 * (diameter - clearance) when the
//     cell does not clear the diameter (x87: fild, fmul 0.6f RW 0xBDAD70, fild, fadd, _ftol); the estimate is costToGoal (RW 0x6F1F0D). Before the
//     neighbours, the cells on the straight line to the goal (RW 0x6F7E3E with RW 0x6F58C3) that are neither open nor closed and clear the diameter
//     (exact) join the open list chained to the previous line cell at + 2 (cell bit 21) or + 5; the first cell that does not stops the line.
//   * buildGroundPath RW 0x6F0DA3 (decomp tier A): the cells (RW 0x6EF591), optimizeGroundPath (RW 0x765BF6, tier A: farthest node with a clear ground line,
//     RW 0x6F0AFE, or a straight / diagonal run; then a node closer than sqrt(390) to the next is skipped), then every inner optimised node moves halfway
//     toward the point 2 * FormationColumnWidth (AIData + 0xA0) off the corner on its outer side, cut back where the ground line from the node stops
//     clearing diameter 4 (RW 0x6EF387).
//   * isLineBlocked RW 0x6F2F8F (decomp tier B): along the cell line, checkForMovement with radius 0, centred, flags 0x0C (terrain failures and layer
//     mismatches counted, units ignored); a refusal, a counted failure or a cell the movement may not enter (RW 0x6E8200) blocks.
// INFERENCE / NOT PORTED (stop S-4462): the terrain fire system's burning cells (RotWK adds 10 to a burning neighbour and stops the line at one: grid
// RW 0xDE46A8, S-191) are never marked; the mineshaft fields (+ 0x1C1D8 ..) and the layer changes (RW 0x6F6286) are not modelled (S-161).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindLine.h"
#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

namespace
{
const int kGroundDx[8] = { 1, 0, -1, 0, 1, -1, -1, 1 }; // RW 0xDA2594
const int kGroundDy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 }; // RW 0xDA2574
const int kAdjacent[5] = { 0, 1, 2, 3, 0 };

// an existing info record is reused with its parent link cleared (RW + 0xC := 0), a new one is taken from the pool
bool claimInfo(PathfindCell *cell, PathfindCellInfoPool &pool, const ICoord2D &pos)
{
	if (cell->hasInfo())
	{
		cell->clearParentCell();
		return true;
	}
	return cell->allocateInfo(pool, pos);
}
} // namespace

bool Pathfinder::groundCellClear(const PathfindCell *cell, unsigned crusher) const
{
	// the cell test of RW 0x6E8BBA (BFME2 decomp PathfinderClearCellForDiameter.cpp rva002E76D3, with its occupant test rva0052DB11(0))
	if (cell->hasInfo())
	{
		bool parked = false;
		for (const PathfindOccupant *p = cell->occupants(OCC_POSITION); p && !parked; p = p->next)
		{
			if (p->owner == PATHFIND_INVALID_ID)
			{
				continue;
			}
			for (const PathfindOccupant *g = cell->occupants(OCC_GROUND_GOAL); g; g = g->next)
			{
				if (g->owner == p->owner)
				{
					parked = true;
					break;
				}
			}
		}
		if (parked)
		{
			for (const PathfindOccupant *p = cell->occupants(OCC_POSITION); p; p = p->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(p->owner) : nullptr;
				if (o && o->getCrushableLevel() > crusher)
				{
					return false;
				}
			}
		}
	}
	switch (cell->getType())
	{
	case PathfindCell::CELL_CLEAR:
		return true;
	case PathfindCell::CELL_OBSTACLE:
		return crusher != 0 && cell->isObstacleFence();
	default:
		return false;
	}
}

int Pathfinder::clearCellForDiameter(unsigned crusher, int x, int y, PathfindLayerEnum layer, int diameter, bool exact)
{
	// RW 0x6E8BBA
	const int radius = diameter / 2;
	int above = radius;
	if (radius == 0)
	{
		++above;
	}
	if (radius > 1)
	{
		for (int i = x - radius; i < x + above; ++i)
		{
			const bool side = i == x - radius || i == x + above - 1;
			const int end = side ? y + above - 1 : y + above;
			for (int j = side ? y - radius + 1 : y - radius; j < end; ++j)
			{
				const PathfindCell *cell = getCell(layer, i, j);
				if (!cell || !groundCellClear(cell, crusher))
				{
					return exact ? 0 : clearCellForDiameter(crusher, x, y, layer, diameter - 2, false);
				}
			}
		}
	}
	else
	{
		for (int i = x - radius; i < x + above; ++i)
		{
			for (int j = y - radius; j < y + above; ++j)
			{
				const PathfindCell *cell = getCell(layer, i, j);
				if (!cell || !groundCellClear(cell, crusher))
				{
					return 0;
				}
			}
		}
	}
	return radius == 0 ? 1 : radius * 2;
}

bool Pathfinder::isGroundPathPassable(const Coord3D &start, PathfindLayerEnum layer, const Coord3D &end, int diameter)
{
	// RW 0x6F0AFE: the cell line (both ends centred, RW 0x6E8CE6) with the callback BFME2 0x6E93A7: a cell that does not clear the diameter (exact) stops it
	ICoord2D a, b;
	Coord3D s = start, e = end;
	worldToCell(&s, true, &a);
	worldToCell(&e, true, &b);
	const bool stopped = walkLine(*this, layer, a, b, [&](PathfindCell *fromCell, PathfindCell *to, int x, int y) {
		if (fromCell && !PathfindCell::layersCompatible(*fromCell, *to))
		{
			return true; // lane WALL-1: no step between a wall top and the ground beside it
		}
		return clearCellForDiameter(0, x, y, to->getLayer(), diameter, true) != diameter;
	});
	return !stopped;
}

bool Pathfinder::isLineBlocked(const PathfindObject *obj, unsigned surfaces, PathfindLayerEnum layer, const Coord3D &from, const Coord3D &to)
{
	// RW 0x6F2F8F with the line callback of BFME2 0x6ED01E (decomp PathfinderRva002ED15ACellCallback.cpp): radius 0, centred, flags 0x0C
	PathfindCheckMovement info;
	info.radius = 0;
	info.center = true;
	info.flags = 0x0C;
	info.transient = false;
	info.ignoreId = PATHFIND_INVALID_ID;
	info.mv = makeMovement(obj, PathfindLocomotorInfo{ surfaces, false, false });
	info.mv.bC = obj ? obj->getCrusherLevel() > 0 : false;
	const PathfindMovement valid = info.mv;
	ICoord2D a, b, prev{ 0, 0 };
	Coord3D s = from, e = to;
	worldToCell(&s, true, &a);
	worldToCell(&e, true, &b);
	return walkLine(*this, layer, a, b, [&](PathfindCell *fromCell, PathfindCell *toCell, int x, int y) {
		info.cell.x = x;
		info.cell.y = y;
		info.layer = toCell->getLayer();
		if (fromCell && !PathfindCell::layersCompatible(*fromCell, *toCell))
		{
			return true; // lane WALL-1: no step between a wall top and the ground beside it
		}
		if (!checkForMovement(obj, info, fromCell ? &prev : nullptr) || info.terrainPenalty != 0)
		{
			return true;
		}
		prev.x = x;
		prev.y = y;
		return !validMovementPosition(valid, toCell);
	});
}

bool Pathfinder::adjustGroundPathPosition(const Coord3D &from, Coord3D *pos)
{
	// RW 0x6EF387: the cell line from `from` to `pos` with the callback BFME2 0x6E7B29: each cell that clears diameter 4 (exact) is remembered (its centred
	// coordinate, RW 0x6E79A8 with 1); the first that does not stops the walk, and `pos` becomes the last remembered point (`from` when none)
	Coord3D result = from;
	ICoord2D a, b;
	Coord3D s = from, e = *pos;
	worldToCell(&s, true, &a);
	worldToCell(&e, true, &b);
	const bool stopped = walkLine(*this, getLayerAt(s.x, s.y), a, b, [&](PathfindCell *fromCell, PathfindCell *to, int x, int y) {
		if (fromCell && !PathfindCell::layersCompatible(*fromCell, *to))
		{
			return true; // lane WALL-1: from the layer of `from` (a wall top's), no step onto another
		}
		if (clearCellForDiameter(0, x, y, to->getLayer(), 4, true) == 4)
		{
			adjustCoordToCell(x, y, true, result, to->getLayer());
			return false;
		}
		return true;
	});
	if (stopped)
	{
		*pos = result;
	}
	return stopped;
}

void Pathfinder::optimizeGroundPath(Path &path, int diameter)
{
	// RW 0x765BF6 (BFME2 decomp PathRva00363FA4.cpp, tier A). Only the ground layer exists (S-161): the layer-change limit of the first loop never applies
	PathNode *anchor = path.getFirstNode();
	while (anchor && anchor != path.getLastNode())
	{
		bool optimized = false;
		PathNode *node = anchor->getNext();
		while (node->getNext())
		{
			node = node->getNext();
		}
		for (; node != anchor; node = node->getPrevious())
		{
			bool passable = isGroundPathPassable(*anchor->getPosition(), anchor->getLayer(), *node->getPosition(), diameter);
			if (!passable)
			{
				// straight and diagonal runs pass (the differences are truncated to int, as RW's cvttss2si)
				int dx = SimMath::truncToInt32(SimMath::subf32(node->getPosition()->x, anchor->getPosition()->x));
				int dy = SimMath::truncToInt32(SimMath::subf32(node->getPosition()->y, anchor->getPosition()->y));
				bool might = false;
				auto stepX = [](PathNode *t) { return SimMath::truncToInt32(SimMath::subf32(t->getNext()->getPosition()->x, t->getPosition()->x)); };
				auto stepY = [](PathNode *t) { return SimMath::truncToInt32(SimMath::subf32(t->getNext()->getPosition()->y, t->getPosition()->y)); };
				if (dx == 0)
				{
					might = true;
					for (PathNode *t = node->getPrevious(); t && t != anchor; t = t->getPrevious())
					{
						dx = stepX(t);
						if (dx != 0)
						{
							might = false;
						}
					}
				}
				if (dy == 0)
				{
					might = true;
					for (PathNode *t = node->getPrevious(); t && t != anchor; t = t->getPrevious())
					{
						dy = stepY(t);
						if (dy != 0)
						{
							might = false;
						}
					}
				}
				if (dx == dy)
				{
					might = true;
					for (PathNode *t = node->getPrevious(); t && t != anchor; t = t->getPrevious())
					{
						dx = stepX(t);
						dy = stepY(t);
						if (dy != dx)
						{
							might = false;
						}
					}
				}
				if (dx == -dy)
				{
					might = true;
					for (PathNode *t = node->getPrevious(); t && t != anchor; t = t->getPrevious())
					{
						dx = stepX(t);
						dy = stepY(t);
						if (dy != -dx)
						{
							might = false;
						}
					}
				}
				passable = might;
			}
			if (passable)
			{
				anchor->setNextOptimized(node);
				anchor = node;
				optimized = true;
				break;
			}
		}
		if (!optimized)
		{
			anchor->setNextOptimized(anchor->getNext());
			anchor = anchor->getNext();
		}
	}
	for (anchor = path.getFirstNode(); anchor; anchor = anchor->getNextOptimized())
	{
		PathNode *node = anchor->getNextOptimized();
		if (node && node->getNextOptimized())
		{
			const float dx = SimMath::subf32(node->getPosition()->x, anchor->getPosition()->x);
			const float dy = SimMath::subf32(node->getPosition()->y, anchor->getPosition()->y);
			if (SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) < 390.0f)
			{
				anchor->setNextOptimized(node->getNextOptimized());
			}
		}
	}
	path.markOptimized();
}

Path *Pathfinder::buildGroundPath(const Coord3D *from, PathfindCell *goalCell, bool center, int diameter, float formationColumnWidth)
{
	// RW 0x6F0DA3 (BFME2 decomp PathfinderBuildGroundPath.cpp, tier A)
	Path *path = new Path();
	prependCells(path, from, goalCell, center);
	optimizeGroundPath(*path, diameter);
	const float spread = SimMath::mulf32(2.0f, formationColumnWidth); // AIData + 0xA0
	bool havePrev = false;
	Coord3D prevPos{ 0.0f, 0.0f, 0.0f };
	for (PathNode *node = path->getFirstNode(); node; node = node->getNextOptimized())
	{
		PathNode *next = node->getNextOptimized();
		if (!next)
		{
			break;
		}
		const Coord3D curPos = *node->getPosition();
		if (havePrev)
		{
			const Coord3D p = *node->getPosition();
			const float v1x = SimMath::subf32(p.x, prevPos.x), v1y = SimMath::subf32(p.y, prevPos.y);
			const float v2x = SimMath::subf32(next->getPosition()->x, p.x), v2y = SimMath::subf32(next->getPosition()->y, p.y);
			const float cross = SimMath::subf32(SimMath::mulf32(v1x, v2y), SimMath::mulf32(v1y, v2x));
			float dx = SimMath::addf32(v1x, v2x), dy = SimMath::addf32(v1y, v2y);
			const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f))); // Coord3D::normalize RW 0x405553
			if (len != 0.0f)
			{
				const float inv = SimMath::divf32(1.0f, len);
				dx = SimMath::mulf32(dx, inv);
				dy = SimMath::mulf32(dy, inv);
			}
			dx = SimMath::mulf32(dx, spread);
			dy = SimMath::mulf32(dy, spread);
			Coord3D outer = p;
			if (cross < 0.0f)
			{
				outer.x = SimMath::addf32(outer.x, SimMath::subf32(0.0f, dy));
				outer.y = SimMath::addf32(outer.y, dx);
			}
			else
			{
				outer.x = SimMath::addf32(outer.x, dy);
				outer.y = SimMath::addf32(outer.y, SimMath::subf32(0.0f, dx));
			}
			adjustGroundPathPosition(p, &outer);
			Coord3D moved = p;
			moved.x = SimMath::addf32(moved.x, SimMath::mulf32(SimMath::subf32(outer.x, p.x), 0.5f));
			moved.y = SimMath::addf32(moved.y, SimMath::mulf32(SimMath::subf32(outer.y, p.y), 0.5f));
			moved.z = SimMath::addf32(moved.z, SimMath::mulf32(SimMath::subf32(outer.z, p.z), 0.5f));
			node->setPosition(&moved);
		}
		havePrev = true;
		prevPos = curPos;
	}
	return path;
}

Path *Pathfinder::findGroundPath(PathfindObject *obj, const Coord3D *from, const Coord3D *rawTo, int diameter, float formationColumnWidth)
{
	// RW 0x6FAA6D (see the file comment)
	m_zoneManager.clearPassableFlags(); // RW 0x937C23
	const PathfindLocomotorInfo ground{ LOCOMOTORSURFACE_GROUND, false, false };
	Coord3D hierarchicalTarget;
	m_portalHops.clear(); // lane WALL-2: the hops of this search only (INFERENCE: as findPath empties them, RW 0x6FE7CB)
	// a goal on a raised walkway gets no corridor (S-162: the block search runs on the ground only), as findPath (review #64)
	const bool raisedGoal = isRaisedLayer(getLayerAt(rawTo->x, rawTo->y, rawTo->z));
	if (raisedGoal || !findClosestHierarchicalPath(obj, ground, true, from, rawTo, &hierarchicalTarget))
	{
		m_zoneManager.setAllPassable(); // RW 0x937C65
	}
	if ((rawTo->x == 0.0f && rawTo->y == 0.0f) || !m_isMapReady)
	{
		return nullptr;
	}
	Coord3D adjusted = *rawTo;
	Coord3D clipped = *from;
	clip(&clipped, &adjusted);
	m_isTunneling = false;
	// TheTerrainLogic::getLayerForDestination at the destination's height (lane WALL-1: a wall top's, as computePath; review #64: with the height, a raised
	// walkway's, which the terrain's height under it never names)
	const PathfindLayerEnum layer = getLayerAt(adjusted.x, adjusted.y, adjusted.z);
	ICoord2D cell;
	worldToCell(&adjusted, true, &cell);
	if (diameter != clearCellForDiameter(0, cell.x, cell.y, layer, diameter, true))
	{
		int offset = 1;
		while (offset < 8)
		{
			const ICoord2D ring = cell;
			const int steps[8][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { -1, 0 }, { 0, -1 }, { 0, -1 }, { 1, 0 }, { 1, 0 } };
			bool found = false;
			for (const auto &st : steps)
			{
				cell.x += st[0] * offset;
				cell.y += st[1] * offset;
				if (clearCellForDiameter(0, cell.x, cell.y, layer, diameter, true) == diameter)
				{
					found = true;
					break;
				}
			}
			if (found)
			{
				break;
			}
			++offset;
			cell = ring;
		}
		if (offset >= 8)
		{
			return nullptr;
		}
	}
	PathfindCell *goal = getCell(layer, cell.x, cell.y);
	if (!goal || !claimInfo(goal, m_pool, cell))
	{
		return nullptr;
	}
	PathfindCell *parent = getClippedCell(obj->getLayer(), &clipped);
	if (!parent)
	{
		return nullptr; // RW 0x6FAD34: the goal keeps its record
	}
	if (parent != goal)
	{
		ICoord2D start;
		worldToCell(&clipped, true, &start);
		if (!claimInfo(parent, m_pool, start))
		{
			return nullptr;
		}
	}
	PathfindMovement mv = makeMovement(obj, ground);
	mv.bC = obj->getCrusherLevel() > 0;
	if (m_zoneManager.getEffectiveZone(mv, true, parent->getZone()) != m_zoneManager.getEffectiveZone(mv, true, goal->getZone()))
	{
		goal->releaseInfo(m_pool);
		parent->releaseInfo(m_pool);
		return nullptr;
	}
	parent->startPathfind(goal); // with the estimate costToGoal (RW 0x6FAE1C .. 0x6FAE2F)
	m_open.clear();
	m_closedList = nullptr;
	m_open.push(parent);
	while (!m_open.empty())
	{
		parent = m_open.pop();
		if (parent == goal)
		{
			m_isTunneling = false;
			Path *path = buildGroundPath(from, goal, false, diameter, formationColumnWidth);
			cleanOpenAndClosedLists();
			parent->releaseInfo(m_pool);
			return path;
		}
		markClosed(parent);
		// the straight line to the goal (RW 0x6F7E3E with the callback RW 0x6F58C3)
		{
			const ICoord2D a{ parent->getXIndex(), parent->getYIndex() }, b{ goal->getXIndex(), goal->getYIndex() };
			walkLine(*this, parent->getLayer(), a, b, [&](PathfindCell *lineFrom, PathfindCell *to, int x, int y) {
				if (!lineFrom)
				{
					return false;
				}
				if (!PathfindCell::layersCompatible(*lineFrom, *to))
				{
					return true; // lane WALL-1: the line keeps to joined layers
				}
				if (to->hasInfo() && (to->getOpen() || to->getClosed()))
				{
					return true;
				}
				if (clearCellForDiameter(0, x, y, to->getLayer(), diameter, true) != diameter)
				{
					return true;
				}
				claimInfo(to, m_pool, ICoord2D{ x, y });
				to->setBlockedByAlly(false);
				const unsigned c = (unsigned)lineFrom->getCostSoFar() + (to->getExtraPass() ? 2u : 5u);
				const unsigned rem = to->costToGoal(goal);
				to->setCostSoFar(c);
				to->setParentCell(lineFrom);
				to->setTotalCost((unsigned)(std::uint16_t)((unsigned)(std::uint16_t)to->getCostSoFar() + rem));
				m_open.push(to);
				return false;
			});
		}
		bool taken[8] = { false, false, false, false, false, false, false, false };
		for (int i = 0; i < 8; ++i)
		{
			const ICoord2D next{ parent->getXIndex() + kGroundDx[i], parent->getYIndex() + kGroundDy[i] };
			PathfindCell *n = getCell(parent->getLayer(), next.x, next.y);
			if (!n || !PathfindCell::layersCompatible(*parent, *n))
			{
				continue; // lane WALL-1: a wall top and the ground beside it are not neighbours (as examineNeighboringCells)
			}
			if (n->getLayer() == LAYER_GROUND && !m_zoneManager.isPassable(next.x, next.y))
			{
				bool passable = false;
				passable = m_zoneManager.isPassable(next.x + 3, next.y + 3) || passable;
				passable = m_zoneManager.isPassable(next.x - 3, next.y + 3) || passable;
				passable = m_zoneManager.isPassable(next.x + 3, next.y - 3) || passable;
				passable = m_zoneManager.isPassable(next.x - 3, next.y - 3) || passable;
				if (!passable)
				{
					continue;
				}
			}
			if (n->hasInfo() && (n->getOpen() || n->getClosed()))
			{
				continue;
			}
			if (n != goal)
			{
				if (i >= 4 && !taken[kAdjacent[i - 4]] && !taken[kAdjacent[i - 3]])
				{
					continue;
				}
				if (n->getType() != PathfindCell::CELL_CLEAR || n->getPinched())
				{
					continue;
				}
				taken[i] = true;
				if (!claimInfo(n, m_pool, next))
				{
					continue;
				}
			}
			const int clear = clearCellForDiameter(0, next.x, next.y, n->getLayer(), diameter, false);
			unsigned cost = n->costSoFar(parent);
			cost += (unsigned)occupantCost(obj, parent, next.x, next.y, diameter, diameter, true);
			if (clear < diameter)
			{
				// RW 0x6FB0C0 .. 0x6FB0E6 (x87, PC24): fild((diameter - clear) * 10) * 0.6f + the cost as unsigned, truncated
				const float add = NumericState::pc24MulD((double)((diameter - clear) * 10), 0.6f);
				cost = (unsigned)SimMath::truncToInt32(NumericState::pc24AddD((double)cost, add));
			}
			n->setBlockedByAlly(false);
			const unsigned rem = n->costToGoal(goal);
			n->setCostSoFar(cost);
			n->setParentCell(parent);
			n->setTotalCost((unsigned)(std::uint16_t)((unsigned)(std::uint16_t)n->getCostSoFar() + rem));
			m_open.push(n);
		}
	}
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	goal->releaseInfo(m_pool);
	return nullptr;
}
