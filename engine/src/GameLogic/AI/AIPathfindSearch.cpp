// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The ground search: A* over the cell grid with the RotWK rules, the straight-line shortcut, path building and the line-passability
// query. Port of RW Pathfinder::internalFindPath 0x6FD06F, examineNeighboringCells 0x6F9850 (its straight-line callback 0x6F6D57),
// findPath 0x6FE7FE, buildActualPath 0x6F1A31, prependCells 0x6EF591, isLinePassable 0x6F2FC3 (callback 0x6EE234), with ZH
// AIPathfind.cpp as the structural donor. See docs/STOPS.md S-162 and S-163 for what is not ported.
//
// TARGET FACTS (RW game.dat, S-001 caveat). Differences from ZH, all read from the binary:
//   * the open list is a binary min-heap (PathfindOpenHeap); a cell that is open or closed is skipped, so a cell is never reopened
//     or improved (no decrease-key); a neighbour that fails validMovementPosition (and is not tunnelled) is marked closed at once;
//   * the heuristic is 10 * max + 4 * min (PathfindCell::costToGoal);
//   * the neighbour cost is  extras + costSoFar(n, parent) + occupantCost  with extras = +98 on a flat cliff cell, +10 on a pinched or
//     bit-23 cell more than 3 from the goal, +14 for a fixed ally (and blockedByAlly unless the unit can path through units), +1000 in
//     a block outside the hierarchical corridor (unless bit 21), +1000 for a foreign obstacle, +10 per terrain failure of
//     checkForMovement, +100 when tunnelling onto an invalid cell, halved when the cell has bit 21;
//   * after the pop of a cell: a goal that cannot be reached by the zones sets `approx`, and the search then prunes cells farther
//     from the goal than a slack, remembers the closest valid cell, and returns a path to it when the open list empties or
//     the examined cell count exceeds MaxCellsFindPathLimit (ZH returns null);
//   * the zone screening uses the terrain variant of the effective zone (buildings do not split it).
// INFERRED / not ported (S-162, S-163): the hierarchical block search that fills the passable-block flags (findPath marks every block
// passable), the portal / waypoint routing (RW 0x6F5547, 0x6F6286, the waypoint vector), the attack-path scorer, the partition grid
// word, LARGE_RECTANGLE_PATHFIND's offset goal and the patch mode.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/AI/AIPathfindStops.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{
inline int realToIntFloor(float f)
{
	return SimMath::floorToInt(f);
}

// ZH Line2D.cpp ClipLine2D (Cohen-Sutherland on integer cells)
enum { CLIP_LEFT = 0x01, CLIP_RIGHT = 0x02, CLIP_BOTTOM = 0x04, CLIP_TOP = 0x08 };
bool clipLine2D(const ICoord2D *p1, const ICoord2D *p2, ICoord2D *c1, ICoord2D *c2, const PathfindRegion &clipRegion)
{
	int x1, y1, x2, y2;
	const int clipLeft = clipRegion.lo.x;
	const int clipRight = clipRegion.hi.x;
	const int clipTop = clipRegion.lo.y;
	const int clipBottom = clipRegion.hi.y;
	int diff;
	x1 = p1->x;
	y1 = p1->y;
	x2 = p2->x;
	y2 = p2->y;
	int clipCode1 = 0;
	if (x1 < clipLeft) clipCode1 = CLIP_LEFT;
	else if (x1 > clipRight) clipCode1 = CLIP_RIGHT;
	if (y1 < clipTop) clipCode1 |= CLIP_TOP;
	else if (y1 > clipBottom) clipCode1 |= CLIP_BOTTOM;
	int clipCode2 = 0;
	if (x2 < clipLeft) clipCode2 = CLIP_LEFT;
	else if (x2 > clipRight) clipCode2 = CLIP_RIGHT;
	if (y2 < clipTop) clipCode2 |= CLIP_TOP;
	else if (y2 > clipBottom) clipCode2 |= CLIP_BOTTOM;
	if ((clipCode1 | clipCode2) == 0)
	{
		*c1 = *p1;
		*c2 = *p2;
		return true;
	}
	if (clipCode1 & clipCode2)
	{
		return false;
	}
	if (clipCode1)
	{
		if (clipCode1 & CLIP_TOP)
		{
			if ((diff = (y2 - y1)) == 0) return false;
			x1 += (x2 - x1) * (clipTop - y1) / diff;
			y1 = clipTop;
		}
		else if (clipCode1 & CLIP_BOTTOM)
		{
			if ((diff = (y2 - y1)) == 0) return false;
			x1 += (x2 - x1) * (clipBottom - y1) / diff;
			y1 = clipBottom;
		}
		if (x1 > clipRight)
		{
			if ((diff = (x2 - x1)) == 0) return false;
			y1 += (y2 - y1) * (clipRight - x1) / diff;
			x1 = clipRight;
		}
		else if (x1 < clipLeft)
		{
			if ((diff = (x2 - x1)) == 0) return false;
			y1 += (y2 - y1) * (clipLeft - x1) / diff;
			x1 = clipLeft;
		}
	}
	if (clipCode2)
	{
		if (clipCode2 & CLIP_TOP)
		{
			if ((diff = (y2 - y1)) == 0) return false;
			x2 += (x2 - x1) * (clipTop - y2) / diff;
			y2 = clipTop;
		}
		else if (clipCode2 & CLIP_BOTTOM)
		{
			if ((diff = (y2 - y1)) == 0) return false;
			x2 += (x2 - x1) * (clipBottom - y2) / diff;
			y2 = clipBottom;
		}
		if (x2 > clipRight)
		{
			if ((diff = (x2 - x1)) == 0) return false;
			y2 += (y2 - y1) * (clipRight - x2) / diff;
			x2 = clipRight;
		}
		else if (x2 < clipLeft)
		{
			if ((diff = (x2 - x1)) == 0) return false;
			y2 += (y2 - y1) * (clipLeft - x2) / diff;
			x2 = clipLeft;
		}
	}
	if (x1 < clipLeft || x1 > clipRight || y1 < clipTop || y1 > clipBottom || x2 < clipLeft || x2 > clipRight || y2 < clipTop || y2 > clipBottom)
	{
		return false;
	}
	c1->x = x1;
	c1->y = y1;
	c2->x = x2;
	c2->y = y2;
	return true;
}

// RW Bresenham (0x6F7F7D for the shortcut, 0x6F03B1 for the passability walk): N = max(|dx|, |dy|) + 1 cells, the decision variable
// d = 2 * minor - major, incE = 2 * minor, incNE = 2 * (minor - major); d >= 0 steps both axes. `visit(from, to, x, y)` returns true to
// stop; the walk ends (returning false) at a cell outside the grid. `from` is the previous cell, null at the first.
template <class Grid, class Visit>
bool walkLine(Grid &grid, PathfindLayerEnum layer, const ICoord2D &start, const ICoord2D &end, Visit visit)
{
	int dx = std::abs(end.x - start.x);
	int dy = std::abs(end.y - start.y);
	const int sx = end.x >= start.x ? 1 : -1;
	const int sy = end.y >= start.y ? 1 : -1;
	int x = start.x, y = start.y;
	PathfindCell *from = nullptr;
	if (dx >= dy)
	{
		int d = 2 * dy - dx;
		const int incE = 2 * dy, incNE = 2 * (dy - dx);
		for (int k = 0; k <= dx; k++)
		{
			PathfindCell *to = grid.getCell(layer, x, y);
			if (to == nullptr) return false;
			if (visit(from, to, x, y)) return true;
			from = to;
			if (d >= 0)
			{
				x += sx;
				y += sy;
				d += incNE;
			}
			else
			{
				x += sx;
				d += incE;
			}
		}
	}
	else
	{
		int d = 2 * dx - dy;
		const int incE = 2 * dx, incNE = 2 * (dx - dy);
		for (int k = 0; k <= dy; k++)
		{
			PathfindCell *to = grid.getCell(layer, x, y);
			if (to == nullptr) return false;
			if (visit(from, to, x, y)) return true;
			from = to;
			if (d >= 0)
			{
				x += sx;
				y += sy;
				d += incNE;
			}
			else
			{
				y += sy;
				d += incE;
			}
		}
	}
	return false;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------
// housekeeping
// ---------------------------------------------------------------------------------------------------------
void Pathfinder::markClosed(PathfindCell *cell)
{
	m_closedList = cell->putOnClosedList(m_closedList);
}

void Pathfinder::cleanOpenAndClosedLists()
{
	int count = 0;
	// every cell still in the heap: clear the open flag and give the info back
	for (PathfindCell *c : m_open.items())
	{
		c->info()->m_open = 0;
	}
	for (PathfindCell *c : m_open.items())
	{
		++count;
		c->releaseInfo(m_pool);
	}
	m_open.clear();
	if (m_closedList)
	{
		count += PathfindCell::releaseClosedList(m_pool, m_closedList);
		m_closedList = nullptr;
	}
	m_cumulativeCellsAllocated += count;
}

void Pathfinder::clip(Coord3D *from, Coord3D *to)
{
	ICoord2D fromCell, toCell;
	ICoord2D clipFromCell, clipToCell;
	fromCell.x = realToIntFloor(SimMath::mulf32(from->x, 0.1f));
	fromCell.y = realToIntFloor(SimMath::mulf32(from->y, 0.1f));
	toCell.x = realToIntFloor(SimMath::mulf32(to->x, 0.1f));
	toCell.y = realToIntFloor(SimMath::mulf32(to->y, 0.1f));
	PathfindRegion ext;
	ext.lo = m_extent.lo;
	ext.hi = m_extent.hi;
	if (clipLine2D(&fromCell, &toCell, &clipFromCell, &clipToCell, ext))
	{
		if (fromCell.x != clipFromCell.x || fromCell.y != clipFromCell.y)
		{
			from->x = SimMath::addf32(SimMath::mulf32((float)clipFromCell.x, 10.0f), 0.05f);
			from->y = SimMath::addf32(SimMath::mulf32((float)clipFromCell.y, 10.0f), 0.05f);
		}
		if (toCell.x != clipToCell.x || toCell.y != clipToCell.y)
		{
			to->x = SimMath::addf32(SimMath::mulf32((float)clipToCell.x, 10.0f), 0.05f);
			to->y = SimMath::addf32(SimMath::mulf32((float)clipToCell.y, 10.0f), 0.05f);
		}
	}
}

bool Pathfinder::clientSafeQuickDoesPathExist(const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to)
{
	// ZH clientSafeQuickDoesPathExist on the ground layer, with the RW zone model (the terrain variant when an end is a building)
	const PathfindMovement mv = makeMovement(nullptr, loco);
	PathfindCell *goalCell = getClippedCell(LAYER_GROUND, to);
	PathfindCell *parentCell = getClippedCell(LAYER_GROUND, from);
	if (parentCell == nullptr || goalCell == nullptr)
	{
		return false;
	}
	if (!validMovementPosition(mv, goalCell))
	{
		return false;
	}
	if (goalCell->getType() == PathfindCell::CELL_CLIFF)
	{
		return false; // no goals on cliffs
	}
	bool terrainVariant = false;
	zoneStorageType zone1 = m_zoneManager.getEffectiveZone(mv, false, parentCell->getZone());
	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE)
	{
		terrainVariant = true;
		if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE)
		{
			return true; // a building just placed and the zones are not updated yet: a false positive is better than a false negative
		}
	}
	if (goalCell->getType() == PathfindCell::CELL_OBSTACLE)
	{
		terrainVariant = true;
	}
	const zoneStorageType z1 = m_zoneManager.getEffectiveZone(mv, terrainVariant, parentCell->getZone());
	const zoneStorageType z2 = m_zoneManager.getEffectiveZone(mv, terrainVariant, goalCell->getZone());
	return z1 == z2;
}

// ---------------------------------------------------------------------------------------------------------
// line passability (RW 0x6F2FC3 / callback 0x6EE234)
// ---------------------------------------------------------------------------------------------------------
namespace
{
struct LineInfo
{
	const PathfindObject *obj = nullptr;
	PathfindCheckMovement move;
	bool allowPinched = false; // RW info+8
	bool latch5D = false;      // RW info+0x5D: a cell with bit 21 was met on the line
	ICoord2D prev;
};
} // namespace

bool Pathfinder::isLinePassable(const PathfindObject *obj, unsigned acceptableSurfaces, PathfindLayerEnum layer, const Coord3D &startWorld, const Coord3D &endWorld,
	bool blocked, bool allowPinched)
{
	LineInfo li;
	li.obj = obj;
	li.allowPinched = allowPinched;
	// RW 0x6EE12D (the line info's constructor): a HORDE or SHIP tests radius 1, centred, like the A* expansion; other units the footprint of RW 0x6ED071
	if (obj && (obj->isKindOf(PK_HORDE) || obj->isKindOf(PK_SHIP)))
	{
		li.move.radius = 1;
		li.move.center = true;
	}
	else
	{
		getRadiusAndCenter(obj, li.move.radius, li.move.center);
	}
	li.move.transient = blocked;
	// RW mask: (HERO ? 0x0D : 0x1C), plus 2 unless the template is a MACHINE
	li.move.flags = (obj && obj->isKindOf(PK_HERO)) ? 0x0Du : 0x1Cu;
	if (!(obj && obj->isKindOf(PK_MACHINE)))
	{
		li.move.flags |= 2;
	}
	li.move.ignoreId = (obj && obj->hasAI()) ? obj->getIgnoredObstacleID() : PATHFIND_INVALID_ID;
	li.move.mv = makeMovement(obj, PathfindLocomotorInfo{ acceptableSurfaces, false, false });
	li.move.mv.bC = obj ? obj->getCrusherLevel() > 0 : false;
	ICoord2D start, end;
	{
		Coord3D s = startWorld, e = endWorld;
		worldToCell(&s, &start);
		worldToCell(&e, &end);
	}
	const bool blockedLine = walkLine(*this, layer, start, end, [&](PathfindCell *from, PathfindCell *to, int x, int y) -> bool {
		// RW 0x6EE234, with info+0x5C = 1: goal reservations of other units block the line
		const PathfindOccupant *list = to->occupants(OCC_GROUND_GOAL);
		if (to->getExtraPass())
		{
			li.latch5D = true;
		}
		else if (li.latch5D)
		{
			return true;
		}
		for (const PathfindOccupant *n = list; n; n = n->next)
		{
			const PathfindObject *o = m_world ? m_world->findObjectByID(n->owner) : nullptr;
			if (!o)
			{
				// an occupant the world no longer knows blocks like an unknown object (retail dereferences it)
				return true;
			}
			if (o != obj && o->getContainer() != obj)
			{
				return true;
			}
		}
		li.move.cell.x = x;
		li.move.cell.y = y;
		li.move.layer = to->getLayer();
		const bool ok = checkForMovement(obj, li.move, from ? &li.prev : nullptr);
		if (!ok)
		{
			return true;
		}
		if (li.move.terrainPenalty != 0)
		{
			return true;
		}
		li.prev.x = x;
		li.prev.y = y;
		if (!li.allowPinched)
		{
			if (to->getPinched())
			{
				return true;
			}
			if (to->getBit23())
			{
				return true;
			}
		}
		return !validMovementPosition(li.move.mv, to);
	});
	return !blockedLine;
}

bool Pathfinder::isGroundLineClear(const Coord3D &a, const Coord3D &b)
{
	// no non-ground layer exists in this port (S-161): every cell is layer 1, the line is clear
	(void)a;
	(void)b;
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// A*: the neighbours of a popped cell
// ---------------------------------------------------------------------------------------------------------
struct ShortcutState
{
	const PathfindObject *obj = nullptr;
	const PathfindLocomotorInfo *loco = nullptr;
	PathfindMovement mv;
	PathfindCell *goal = nullptr;
	bool isHuman = true;
	int radius = 0;
	bool center = true;
	unsigned mode = 0x12;
	PathfindObjectID ignoreId = PATHFIND_INVALID_ID;
	ICoord2D last;
	bool haveLast = false;
	int *counter = nullptr;
};

bool Pathfinder::examineShortcutCell(PathfindCell *from, PathfindCell *to, int x, int y, ShortcutState &d)
{
	// RW 0x6F6D57: returns true to abort the walk
	if (*d.counter > m_config.examineTowardsGoalLimit)
	{
		return true;
	}
	++*d.counter;
	if (m_isTunneling)
	{
		return true;
	}
	if (from)
	{
		if (to->hasInfo() && (to->getOpen() || to->getClosed()))
		{
			return true;
		}
		if (!validMovementPosition(d.mv, to))
		{
			return true;
		}
		if (to->getLayer() == LAYER_GROUND && !m_zoneManager.isPassable(x, y))
		{
			return true;
		}
		if (from->getLayer() != to->getLayer())
		{
			return true;
		}
		if (to->getPinched())
		{
			return true;
		}
		if (to->getBit23())
		{
			// RW: abort unless the goal is ground and the unit is a ship on water (ships are not modelled: always abort)
			return true;
		}
		if (to->getType() == PathfindCell::CELL_CLIFF)
		{
			return true;
		}
		if (d.isHuman)
		{
			if (x < m_logicalExtent.lo.x || y < m_logicalExtent.lo.y || x > m_logicalExtent.hi.x || y > m_logicalExtent.hi.y)
			{
				return true;
			}
		}
		PathfindCheckMovement info;
		info.cell.x = x;
		info.cell.y = y;
		info.layer = from->getLayer();
		info.radius = d.radius;
		info.center = d.center;
		info.flags = d.mode;
		info.ignoreId = d.ignoreId;
		info.mv = d.mv;
		if (!checkForMovement(d.obj, info, d.haveLast ? &d.last : nullptr))
		{
			return true;
		}
		if (info.terrainPenalty != 0)
		{
			return true;
		}
		ICoord2D pos;
		pos.x = x;
		pos.y = y;
		unsigned c = (unsigned)SimMath::truncToInt32(SimMath::addf32((float)from->getCostSoFar(), (to->getExtraPass() ? 2.5f : 5.0f)));
		c += (unsigned)occupantCost(d.obj, from, x, y, d.radius, d.radius + (d.center ? 1 : 0), true);
		if (!to->allocateInfo(m_pool, pos))
		{
			return true;
		}
		to->setBlockedByAlly(false);
		unsigned rem = to->costToGoal(d.goal);
		to->setCostSoFar(c);
		to->setParentCell(from);
		to->setTotalCost((unsigned)(std::uint16_t)to->getCostSoFar() + (unsigned)(std::uint16_t)rem);
		m_open.push(to);
	}
	d.last.x = x;
	d.last.y = y;
	d.haveLast = true;
	return false;
}

int Pathfinder::occupantCost(const PathfindObject *obj, const PathfindCell *parent, int cx, int cy, int r, int n, bool skipEnemies)
{
	// TARGET RW 0x6ED21E: units whose goals are in the footprint and that have the same or higher priority add cost
	if (!parent || !obj)
	{
		return 0;
	}
	const int shift = r >= 4 ? 2 : (r > 1 ? 1 : 0);
	int cost = 0;
	float selfT = -1.0f;
	for (int i = cx - r; i < cx + n; i++)
	{
		for (int j = cy - r; j < cy + n; j++)
		{
			const PathfindCell *c = getCell(parent->getLayer(), i, j);
			if (!c || !c->hasInfo())
			{
				continue;
			}
			for (const PathfindOccupant *node = c->occupants(OCC_GROUND_GOAL); node; node = node->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
				if (!o || o == obj || o->getContainer() == obj)
				{
					continue;
				}
				const bool enemy = obj->getRelationship(*o) == PATHFIND_ENEMIES;
				if (enemy && skipEnemies)
				{
					continue;
				}
				if (!obj->hasAI() || !o->hasAI())
				{
					continue;
				}
				if (obj->aiPriority() > o->aiPriority())
				{
					continue;
				}
				int add;
				if (o->isParked())
				{
					add = 8;
				}
				else if (!obj->hasLocomotor() || !o->hasLocomotor())
				{
					add = 8;
				}
				else
				{
					const float qx = SimMath::mulf32((SimMath::addf32((float)i, 0.5f)), 10.0f), qy = SimMath::mulf32((SimMath::addf32((float)j, 0.5f)), 10.0f);
					if (selfT < 0.0f)
					{
						selfT = obj->secondsToReach(qx, qy);
					}
					const float oT = o->secondsToReach(qx, qy);
					if (selfT > oT)
					{
						add = 4;
					}
					else
					{
						continue;
					}
				}
				cost += add >> shift;
			}
		}
	}
	return cost;
}

int Pathfinder::examineNeighboringCells(PathfindCell *parentCell, PathfindCell *goalCell, const PathfindLocomotorInfo &loco, bool isHuman, bool centerInCell,
	int radius, const ICoord2D &startCellNdx, const PathfindObject *obj)
{
	(void)startCellNdx;
	// TARGET RW 0x6F9850
	if (obj && (obj->isKindOf(PK_HORDE) || obj->isKindOf(PK_SHIP)))
	{
		radius = 1;
		centerInCell = true;
	}
	const bool canPathThroughUnits = obj ? obj->canPathThroughUnits() : false;
	const unsigned mode = (obj && obj->isKindOf(PK_HERO)) ? 0x0Cu : 0x1Cu;
	const PathfindMovement mv = makeMovement(obj, loco);
	const PathfindObjectID ignoreId = (obj && obj->hasAI()) ? obj->getIgnoredObstacleID() : PATHFIND_INVALID_ID;
	int cnt = 0;
	// the straight line shortcut towards the goal
	if (!m_isTunneling && !loco.downhillOnly && goalCell && (parentCell->getXIndex() & 3) == 0 && (parentCell->getYIndex() & 3) == 0)
	{
		ShortcutState d;
		d.obj = obj;
		d.loco = &loco;
		d.mv = mv;
		d.goal = goalCell;
		d.isHuman = isHuman;
		d.radius = radius;
		d.center = centerInCell;
		d.mode = (obj && obj->isKindOf(PK_HERO)) ? 3u : 0x12u;
		d.ignoreId = ignoreId;
		d.counter = &m_shortcutCounter;
		ICoord2D start, end;
		start.x = parentCell->getXIndex();
		start.y = parentCell->getYIndex();
		end.x = goalCell->getXIndex();
		end.y = goalCell->getYIndex();
		walkLine(*this, parentCell->getLayer(), start, end, [&](PathfindCell *from, PathfindCell *to, int x, int y) { return examineShortcutCell(from, to, x, y, d); });
	}
	// the eight neighbours: orthogonal first, then diagonal
	static const ICoord2D delta[] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
	bool flag[8] = { false, false, false, false, false, false, false, false };
	const int px = parentCell->getXIndex(), py = parentCell->getYIndex();
	ICoord2D parentNdx;
	parentNdx.x = px;
	parentNdx.y = py;
	for (int i = 0; i < 8; i++)
	{
		ICoord2D nc;
		nc.x = px + delta[i].x;
		nc.y = py + delta[i].y;
		PathfindCell *n = getCell(parentCell->getLayer(), nc.x, nc.y);
		if (n == nullptr)
		{
			continue;
		}
		// the hierarchical corridor: a cell in a block that is not passable is rejected unless one of the four diagonal points 3 away is
		if (n->getLayer() == LAYER_GROUND && !m_zoneManager.isPassable(nc.x, nc.y))
		{
			const bool nearPassable = m_zoneManager.isPassable(nc.x + 3, nc.y + 3) || m_zoneManager.isPassable(nc.x - 3, nc.y + 3) ||
				m_zoneManager.isPassable(nc.x + 3, nc.y - 3) || m_zoneManager.isPassable(nc.x - 3, nc.y - 3);
			if (!nearPassable)
			{
				continue;
			}
		}
		if (n->hasInfo() && (n->getOpen() || n->getClosed()))
		{
			continue;
		}
		if (n != parentCell)
		{
			if (i >= 4 && !flag[i & 3] && !flag[(i + 1) & 3])
			{
				continue; // a diagonal needs one of its two orthogonal neighbours
			}
		}
		if (n->getType() != PathfindCell::CELL_CLEAR || true)
		{
			// layer compatibility (RW step 3): the ground layer only
		}
		if (isHuman)
		{
			if (nc.x < m_logicalExtent.lo.x || nc.y < m_logicalExtent.lo.y || nc.x > m_logicalExtent.hi.x || nc.y > m_logicalExtent.hi.y)
			{
				continue;
			}
		}
		if (loco.downhillOnly && m_terrain)
		{
			const float fromZ = m_terrain->getGroundHeight(SimMath::mulf32((float)px, 10.0f), SimMath::mulf32((float)py, 10.0f));
			const float toZ = m_terrain->getGroundHeight(SimMath::mulf32((float)nc.x, 10.0f), SimMath::mulf32((float)nc.y, 10.0f));
			if (fromZ < toZ)
			{
				continue; // the compare direction was not verified (S-163)
			}
		}
		const bool mvOK = validMovementPosition(mv, n);
		const bool tl = m_isTunneling; // same layer: the ground only
		if (!mvOK && !tl)
		{
			// the cell is marked closed at once and not counted
			if (!n->hasInfo())
			{
				if (!n->allocateInfo(m_pool, nc))
				{
					return cnt;
				}
			}
			markClosed(n);
			continue;
		}
		flag[i] = true;
		PathfindCheckMovement info;
		info.cell = nc;
		info.layer = parentCell->getLayer();
		info.radius = radius;
		info.center = centerInCell;
		info.flags = mode;
		info.ignoreId = ignoreId;
		info.mv = mv;
		const bool ok = checkForMovement(obj, info, &parentNdx);
		if (ok)
		{
			if (mvOK)
			{
				m_isTunneling = false;
			}
		}
		else
		{
			if (!tl)
			{
				if (!n->hasInfo())
				{
					if (!n->allocateInfo(m_pool, nc))
					{
						return cnt;
					}
				}
				markClosed(n);
				continue;
			}
			info.terrainPenalty++;
		}
		if (!n->hasInfo())
		{
			if (!n->allocateInfo(m_pool, nc))
			{
				return cnt; // out of cells for pathing
			}
			cnt++;
		}
		const int gdx = goalCell ? std::abs(nc.x - (int)goalCell->getXIndex()) : 3;
		const int gdy = goalCell ? std::abs(nc.y - (int)goalCell->getYIndex()) : 3;
		unsigned X = 0;
		if (n->getType() == PathfindCell::CELL_CLIFF && !n->getPinched())
		{
			const float fromZ = m_terrain ? m_terrain->getGroundHeight(SimMath::mulf32((float)px, 10.0f), SimMath::mulf32((float)py, 10.0f)) : 0.0f;
			const float toZ = m_terrain ? m_terrain->getGroundHeight(SimMath::mulf32((float)nc.x, 10.0f), SimMath::mulf32((float)nc.y, 10.0f)) : 0.0f;
			if (SimMath::absD(SimMath::subf32(fromZ, toZ)) < 10.0f)
			{
				X = 98;
			}
		}
		else if (n->getPinched() || n->getBit23())
		{
			if (gdx + gdy > 3)
			{
				X = 10;
			}
		}
		n->setBlockedByAlly(false);
		if (info.allyFixed)
		{
			X += 14;
			if (!canPathThroughUnits)
			{
				n->setBlockedByAlly(true);
			}
		}
		unsigned rem = 0;
		if (goalCell)
		{
			rem = n->costToGoal(goalCell);
		}
		if (n->getLayer() == LAYER_GROUND && !m_zoneManager.isPassable(nc.x, nc.y) && !n->getExtraPass())
		{
			X += 1000;
		}
		if (n->getType() == PathfindCell::CELL_OBSTACLE && n->getObstacleID() != m_ignoreObstacleID)
		{
			X += 1000;
		}
		X += 10u * (unsigned)info.terrainPenalty;
		if (m_isTunneling && !validMovementPosition(mv, n))
		{
			X += 100;
		}
		if (n->getExtraPass())
		{
			X >>= 1;
		}
		X += n->costSoFar(parentCell);
		X += (unsigned)occupantCost(obj, parentCell, nc.x, nc.y, radius, radius + (centerInCell ? 1 : 0), true);
		n->setCostSoFar(X);
		n->setParentCell(parentCell);
		if (m_isTunneling)
		{
			rem = 0; // find the closest valid cell
		}
		n->setTotalCost((unsigned)(std::uint16_t)n->getCostSoFar() + (unsigned)(std::uint16_t)rem);
		m_open.push(n);
	}
	return cnt;
}

// ---------------------------------------------------------------------------------------------------------
// findPath / internalFindPath
// ---------------------------------------------------------------------------------------------------------
Path *Pathfinder::findPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *rawTo, bool *partial)
{
	// TARGET RW 0x6FE7FE: clear the passable flags, find the hierarchical path (it marks the blocks the A* may use cheaply), mark every
	// block passable when there is none, then internalFindPath. The hierarchical search (RW 0x6F85A6-0x6F9829) was not read: every
	// block counts as passable, so the A* is not confined to a corridor (S-162).
	note(pathstops::kHierarchical);
	m_zoneManager.setAllPassable();
	return internalFindPath(obj, loco, from, rawTo, partial);
}

Path *Pathfinder::internalFindPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *rawTo, bool *partial)
{
	if (partial)
	{
		*partial = false;
	}
	int radius = 0;
	bool centerInCell = true;
	if (obj)
	{
		getRadiusAndCenter(obj, radius, centerInCell);
	}
	const bool isHuman = obj ? !obj->isComputerControlled() : true;
	if (rawTo->x == 0.0f && rawTo->y == 0.0f)
	{
		return nullptr; // attempting a pathfind to (0, 0), generally a bug
	}
	if (m_isMapReady == false)
	{
		return nullptr;
	}
	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);
	if (!centerInCell)
	{
		adjustTo.x = SimMath::addf32(adjustTo.x, 5.0f);
		adjustTo.y = SimMath::addf32(adjustTo.y, 5.0f);
	}
	if (obj && obj->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
	{
		note(pathstops::kLargeRectGoal);
	}
	m_isTunneling = false;
	const PathfindLayerEnum destinationLayer = LAYER_GROUND;
	PathfindCell *goalCell = getCell(destinationLayer, to);
	if (goalCell == nullptr)
	{
		return nullptr;
	}
	ICoord2D goalNdx;
	worldToCell(to, true, &goalNdx);
	bool approx = false;
	{
		int out = 0;
		const bool destOK = checkDestination(obj, goalNdx.x, goalNdx.y, destinationLayer, radius, centerInCell, &out, true);
		bool moveOK = true;
		if (destOK && obj)
		{
			PathfindCheckMovement info;
			info.cell = goalNdx;
			info.layer = destinationLayer;
			info.radius = radius;
			info.center = centerInCell;
			info.flags = obj->isKindOf(PK_HERO) ? 0x0Cu : 0x1Cu;
			info.transient = true;
			info.ignoreId = obj->hasAI() ? obj->getIgnoredObstacleID() : PATHFIND_INVALID_ID;
			info.mv = makeMovement(obj, loco);
			moveOK = checkForMovement(obj, info, nullptr);
		}
		if (!destOK || !moveOK)
		{
			approx = true;
		}
	}
	ICoord2D startCellNdx;
	worldToCell(&clipFrom, &startCellNdx);
	const PathfindLayerEnum layer = obj ? obj->getLayer() : LAYER_GROUND;
	PathfindCell *parentCell = getClippedCell(layer, &clipFrom);
	if (parentCell == nullptr)
	{
		return nullptr;
	}
	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(m_pool, pos2d))
	{
		return nullptr;
	}
	if (parentCell != goalCell)
	{
		worldToCell(&clipFrom, &pos2d);
		if (!parentCell->allocateInfo(m_pool, pos2d))
		{
			goalCell->releaseInfo(m_pool);
			return nullptr;
		}
	}
	// a tunnelling search starts inside an obstacle and ignores obstacle cells until it leaves
	m_isTunneling = parentCell->getType() == PathfindCell::CELL_OBSTACLE && parentCell->getObstacleID() != m_ignoreObstacleID;
	const PathfindMovement mv = makeMovement(obj, loco);
	{
		// the zone screening: the terrain variant of the effective zone for both ends
		const zoneStorageType zS = m_zoneManager.getEffectiveZone(mv, true, parentCell->getZone());
		const zoneStorageType zG = m_zoneManager.getEffectiveZone(mv, true, goalCell->getZone());
		if (zS != zG)
		{
			// RW 0x6F5547 (a breadth-first search over portal objects) is not ported: the goal is treated as unreachable
			approx = true;
		}
	}
	{
		Coord3D fromPos = *from;
		if (!validMovementPosition(obj, loco, destinationLayer, to))
		{
			approx = true;
		}
		if (!validMovementPosition(obj, loco, layer, &fromPos))
		{
			m_isTunneling = true;
		}
	}
	if (!m_isTunneling && obj)
	{
		PathfindCheckMovement info;
		info.cell = startCellNdx;
		info.layer = layer;
		info.radius = radius;
		info.center = centerInCell;
		info.flags = obj->isKindOf(PK_HERO) ? 0x0Cu : 0x1Cu;
		info.transient = true;
		info.ignoreId = obj->hasAI() ? obj->getIgnoredObstacleID() : PATHFIND_INVALID_ID;
		info.mv = mv;
		if (!checkForMovement(obj, info, nullptr))
		{
			m_isTunneling = true;
		}
	}
	parentCell->startPathfind(goalCell);
	m_open.clear();
	m_closedList = nullptr;
	parentCell->setTotalCost(parentCell->costToGoal(goalCell));
	m_open.push(parentCell);
	int cellCount = 0;
	m_shortcutCounter = 0;
	PathfindCell *closest = nullptr;
	int bestClose = std::numeric_limits<int>::max();
	int bestD2 = std::numeric_limits<int>::max();
	int slack = std::numeric_limits<int>::max();
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		if (c == goalCell)
		{
			m_isTunneling = false;
			Path *path = buildActualPath(obj, loco.validSurfaces, from, goalCell, centerInCell, false);
			parentCell->releaseInfo(m_pool);
			cleanOpenAndClosedLists();
			c->releaseInfo(m_pool);
			return path;
		}
		markClosed(c);
		if (cellCount > m_config.findPathLimit)
		{
			break;
		}
		const int dx = (int)c->getXIndex() - (int)goalCell->getXIndex();
		const int dy = (int)c->getYIndex() - (int)goalCell->getYIndex();
		const int d2 = dx * dx + dy * dy;
		if (!m_isTunneling)
		{
			if (d2 < bestD2)
			{
				if (std::abs(dx) + std::abs(dy) > 25)
				{
					const double s = SimMath::addD(SimMath::sqrtd((double)d2), 25.0);
					slack = (int)SimMath::ftol2Low32(SimMath::mulD(s, s)); // _ftol2, low word
				}
				else
				{
					slack = 4 * d2;
				}
				bestD2 = d2;
			}
			const int key = d2; // + 10000 * the waypoint count (none)
			if (key < bestClose)
			{
				int out = 0;
				if (checkDestination(obj, c->getXIndex(), c->getYIndex(), destinationLayer, radius, centerInCell, &out, false) && out == 0 &&
					validMovementPosition(mv, c))
				{
					closest = c;
					bestClose = key;
				}
			}
		}
		if (approx && d2 > slack && d2 >= 100)
		{
			continue; // too far from the goal to be worth expanding
		}
		cellCount += examineNeighboringCells(c, goalCell, loco, isHuman, centerInCell, radius, startCellNdx, obj);
	}
	// failure or budget: a path to the closest valid cell, else nothing
	if (closest)
	{
		if (partial)
		{
			*partial = true;
		}
		m_isTunneling = false;
		note(pathstops::kPartialRebuild);
		Path *path = buildActualPath(obj, loco.validSurfaces, from, closest, centerInCell, false);
		cleanOpenAndClosedLists();
		goalCell->releaseInfo(m_pool);
		return path;
	}
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	goalCell->releaseInfo(m_pool);
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------
// path building
// ---------------------------------------------------------------------------------------------------------
Path *Pathfinder::buildActualPath(const PathfindObject *obj, unsigned acceptableSurfaces, const Coord3D *fromPos, PathfindCell *goalCell, bool center, bool blocked)
{
	note(pathstops::kPath);
	Path *path = new Path();
	if (goalCell->getPinched() && goalCell->getParentCell() && !goalCell->getParentCell()->getPinched())
	{
		goalCell = goalCell->getParentCell(); // ZH: a path does not end in a pinched cell
	}
	prependCells(path, fromPos, goalCell, center);
	path->optimize(*this, obj, acceptableSurfaces, blocked, nullptr);
	return path;
}

void Pathfinder::prependCells(Path *path, const Coord3D *fromPos, PathfindCell *goalCell, bool center)
{
	// TARGET RW 0x6EF591: the cells are walked in REVERSE order, creating the path in the desired order; the LAST node is in the unit's
	// own cell, so the unit's position is used instead. (Waypoint cells and the layer lookup are not modelled: S-161.)
	Coord3D pos;
	PathfindCell *cell, *prevCell = nullptr;
	const bool goalCellNull = (goalCell->getParentCell() == nullptr);
	for (cell = goalCell; cell->getParentCell(); cell = cell->getParentCell())
	{
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		bool canOptimize = true;
		if (cell->getType() == PathfindCell::CELL_CLIFF)
		{
			if (prevCell && prevCell->getType() != PathfindCell::CELL_CLIFF)
			{
				if (path->getFirstNode())
				{
					path->getFirstNode()->setCanOptimize(false);
				}
			}
		}
		else
		{
			if (prevCell && prevCell->getType() == PathfindCell::CELL_CLIFF)
			{
				canOptimize = false;
			}
		}
		path->prependNode(&pos, LAYER_GROUND);
		path->getFirstNode()->setCanOptimize(canOptimize);
		if (cell->isBlockedByAlly())
		{
			path->setBlockedByAlly(true);
		}
		if (prevCell)
		{
			prevCell->clearParentCell();
		}
		prevCell = cell;
	}
	if (goalCellNull)
	{
		// a very short path
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		path->prependNode(&pos, cell->getLayer());
	}
	// the actual start position is the first node, so the path begins at the unit's feet
	if (fromPos->x != path->getFirstNode()->getPosition()->x || fromPos->y != path->getFirstNode()->getPosition()->y)
	{
		path->prependNode(fromPos, cell->getLayer());
	}
}
