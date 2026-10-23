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
#include "GameLogic/AI/AIPathfindLine.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/AI/AIPathfindStops.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{
// lane UBER-3P (Uber profile only): a cell search that cannot reach its goal ends once its answer held for this many cells (internalFindPath)
constexpr int kUberStallCells = 256;
} // namespace

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

bool Pathfinder::clientSafeQuickDoesPathExist(const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, const PathfindObject *obj)
{
	// ZH clientSafeQuickDoesPathExist on the ends' layers (review #63), with the RW zone model (the terrain variant when an end is a building). Lane WALL-2: with the unit
	// the movement is the unit's (RW 0x6F5D65 .. 0x6F5D91: its SlopeLimitIndex and CanPathThroughGates, RW 0x6EA04D), without one ZH's anonymous movement
	const PathfindMovement mv = makeMovement(obj, loco);
	// review (#63): the ends' layers, as the search takes them: the goal on a raised walkway is the walkway's cell, the start the unit's layer's
	PathfindCell *goalCell = getClippedCell(searchDestinationLayer(*to), to);
	// review of #63: the unit's layer only where the start is the unit's position; another start (a goal tested onward, an adjusted point) takes the layer
	// there, as the goal does
	const bool atUnit = obj && from->x == obj->getPosition().x && from->y == obj->getPosition().y;
	PathfindCell *parentCell = getClippedCell(atUnit ? obj->getLayer() : searchDestinationLayer(*from), from);
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
	if (z1 == z2)
	{
		return true;
	}
	return obj != nullptr && portalPathExists(obj, loco, z1, z2); // RW 0x6F5EDA .. 0x6F5EE7 (lane WALL-2)
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

// RW 0x6F61BD, the line callback of CanApproachToTarget (record RW 0x6EAF14). The cell dword (+ 0xC): type = bits 0 .. 3, layer = bits 4 .. 9 (what RW 0x6ED023
// is given; IsPointOnWall tests it > 0x10), bit 17 = the gate area (getBit17). Returning true stops the line.
//   1. (bit 17 or layer > 1) and not + 0x1D and not + 0x1E and + 0x1F: stop (RW 0x6F61D3 .. 0x6F61F8)
//   2. type 4 (obstacle): go on, the record untouched (RW 0x6F61FF)
//   3. layer > 1 and not + 0x1D and + 0x1E: go on (RW 0x6F6204 .. 0x6F6215)
//   4. + 0x1E = 0; validMovementPosition (RW 0x6E8200) false: go on
//   5. the cell's point for the unit (RW 0x6ED023: centred when its footprint is odd, on the cell's layer); QuickDoesPathExist from the unit's position (RW 0x6F5BB0,
//      last argument 0) true: + 0x1C = 1, stop
//   6. + 0x1F = 1; + 0x20 set: stop; else stop when + 0x1D (a melee weapon ends the line at its first miss), go on otherwise (RW 0x6F626B .. 0x6F627C)
bool Pathfinder::canApproachToTarget(const PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &target, float weaponRange, bool meleeWeapon,
	bool stopAtFirstMiss)
{
	const float range = 150.0f < weaponRange ? weaponRange : 150.0f; // RW 0x6FB773 .. 0x6FB7B1
	const PathfindMovement mv = makeMovement(&obj, loco);          // the record's + 0xC (RW 0x6EA04D)
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(&obj, radius, center); // RW 0x6ECFC5: odd footprint (RW 0x6EAF79) -> centred
	const Coord3D from = obj.getPosition();
	bool found = false;                        // + 0x1C
	const bool melee = meleeWeapon;            // + 0x1D
	const bool callerStop = stopAtFirstMiss;   // + 0x20
	auto raised = [](const PathfindCell *c) { return (int)c->getLayer() > (int)LAYER_GROUND; };
	for (int i = -1; i < 2; ++i)
	{
		for (int j = -1; j < 2; ++j)
		{
			if (i == 0 && j == 0)
			{
				continue;
			}
			const Coord3D end{ SimMath::addf32(SimMath::mulf32(SimMath::sseFromInt32(i), range), target.x), SimMath::addf32(SimMath::mulf32(SimMath::sseFromInt32(j), range), target.y),
				target.z };
			bool noneYet = true; // + 0x1E (RW 0x6FB82F)
			bool missed = false; // + 0x1F (RW 0x6FB833)
			ICoord2D a, b;
			{
				Coord3D s0 = target, e0 = end;
				worldToCell(&s0, true, &a); // RW 0x6FAA07 / 0x6FAA19: centring 1
				worldToCell(&e0, true, &b);
			}
			// the layer of RW 0x680A75 (the target's; lane WALL-2: a raised walkway's cells when the target stands on one)
			walkLine(*this, getLayerAt(target.x, target.y, target.z), a, b, [&](PathfindCell *, PathfindCell *to, int x, int y) -> bool {
				if ((to->getBit17() || raised(to)) && !melee && !noneYet && missed)
				{
					return true; // 1.
				}
				if (to->getType() == PathfindCell::CELL_OBSTACLE)
				{
					return false; // 2.
				}
				if (raised(to) && !melee && noneYet)
				{
					return false; // 3.
				}
				noneYet = false; // 4.
				if (!validMovementPosition(mv, to))
				{
					return false;
				}
				Coord3D point;
				adjustCoordToCell(x, y, center, point, to->getLayer() == LAYER_INVALID ? LAYER_GROUND : to->getLayer()); // 5.
				if (clientSafeQuickDoesPathExist(loco, &from, &point, &obj)) // RW 0x6F5BB0 with the unit (lane WALL-2)
				{
					found = true;
					return true;
				}
				missed = true; // 6.
				return callerStop || melee;
			});
			if (found)
			{
				return true; // RW 0x6FB84A
			}
		}
	}
	return false;
}

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
	// lane PATHPERF-2: the line memo (LineMemoScope) binds to the scope's first parameters; a call with others walks without it
	bool memo = false;
	if (m_lineMemoActive && m_searchShortcuts)
	{
		if (!m_lineMemoBound)
		{
			m_lineMemoBound = true;
			m_lineMemoObj = obj;
			m_lineMemoSurfaces = acceptableSurfaces;
			m_lineMemoBlockedArg = blocked;
			m_lineMemoPinchedArg = allowPinched;
			m_lineMemoLayer = layer;
		}
		memo = m_lineMemoObj == obj && m_lineMemoSurfaces == acceptableSurfaces && m_lineMemoBlockedArg == blocked && m_lineMemoPinchedArg == allowPinched &&
			m_lineMemoLayer == layer;
	}
	const bool blockedLine = walkLine(*this, layer, start, end, [&](PathfindCell *from, PathfindCell *to, int x, int y) -> bool {
		// RW 0x6EE234, with info+0x5C = 1: goal reservations of other units block the line
		if (to->getExtraPass())
		{
			li.latch5D = true;
		}
		else if (li.latch5D)
		{
			return true;
		}
		// the rest depends only on the cell and the neighbour it is entered from (checkForMovement's footprint overlap): walkLine steps to a neighbour, and
		// li.prev is `from`'s cell whenever `from` is set
		// the cell by its coordinates and layer, not by its address (review #64: a raised walkway's cells are another array, and a difference of pointers
		// into different arrays is undefined)
		const std::uint64_t cellKey = x >= 0 && y >= 0
			? ((std::uint64_t)x * (std::uint64_t)m_stride + (std::uint64_t)y) * 0x41u + (std::uint64_t)to->getLayer()
			: 0xFFFFFFFFu;
		std::uint32_t key = 0;
		if (memo && cellKey < 400000000u)
		{
			const unsigned code = from ? (unsigned)((x - li.prev.x + 1) * 3 + (y - li.prev.y + 1)) : 9u;
			key = (std::uint32_t)(cellKey * 10u + code + 1u);
			bool known = false;
			if (lineMemoFind(key, known))
			{
				if (!known)
				{
					li.prev.x = x;
					li.prev.y = y;
				}
				return known;
			}
		}
		const bool cellBlocks = [&]() -> bool {
			// lane WALL-1 (INFERENCE, S-4840): a line does not step between cells the search could not step between (a wall top and the ground beside it)
			if (from && !PathfindCell::layersCompatible(*from, *to))
			{
				return true;
			}
			for (const PathfindOccupant *n = to->occupants(OCC_GROUND_GOAL); n; n = n->next)
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
		}();
		if (key != 0)
		{
			lineMemoStore(key, cellBlocks);
		}
		if (!cellBlocks)
		{
			li.prev.x = x;
			li.prev.y = y;
		}
		return cellBlocks;
	});
	return !blockedLine;
}

// lane PATHPERF-2: the line memo
Pathfinder::LineMemoScope::LineMemoScope(Pathfinder &pf) : m_pf(pf), m_outer(!pf.m_lineMemoActive)
{
	if (m_outer)
	{
		pf.m_lineMemoActive = true;
		pf.m_lineMemoBound = false;
		pf.m_lineMemoCount = 0;
		if (++pf.m_lineMemoGen == 0)
		{
			for (LineMemoEntry &e : pf.m_lineMemo)
			{
				e.gen = 0;
			}
			pf.m_lineMemoGen = 1;
		}
	}
}

Pathfinder::LineMemoScope::~LineMemoScope()
{
	if (m_outer)
	{
		m_pf.m_lineMemoActive = false;
		m_pf.m_lineMemoBound = false;
	}
}

namespace
{
inline size_t lineMemoSlot(std::uint32_t key, size_t mask)
{
	return (size_t)((key * 2654435761u) >> 7) & mask;
}
} // namespace

bool Pathfinder::lineMemoFind(std::uint32_t key, bool &blocked) const
{
	if (m_lineMemo.empty())
	{
		return false;
	}
	const size_t mask = m_lineMemo.size() - 1;
	for (size_t i = lineMemoSlot(key, mask);; i = (i + 1) & mask)
	{
		const LineMemoEntry &e = m_lineMemo[i];
		if (e.gen != m_lineMemoGen)
		{
			return false;
		}
		if (e.key == key)
		{
			blocked = e.blocked;
			return true;
		}
	}
}

void Pathfinder::lineMemoStore(std::uint32_t key, bool blocked)
{
	if ((m_lineMemoCount + 1) * 2 > m_lineMemo.size())
	{
		// grow (power of two, at most half full); the current generation's entries move over
		std::vector<LineMemoEntry> old;
		old.swap(m_lineMemo);
		m_lineMemo.assign(old.empty() ? 4096 : old.size() * 2, LineMemoEntry());
		const std::uint32_t gen = m_lineMemoGen;
		m_lineMemoCount = 0;
		for (const LineMemoEntry &e : old)
		{
			if (e.gen == gen)
			{
				lineMemoStore(e.key, e.blocked);
			}
		}
	}
	const size_t mask = m_lineMemo.size() - 1;
	for (size_t i = lineMemoSlot(key, mask);; i = (i + 1) & mask)
	{
		LineMemoEntry &e = m_lineMemo[i];
		if (e.gen != m_lineMemoGen)
		{
			e.key = key;
			e.gen = m_lineMemoGen;
			e.blocked = blocked;
			++m_lineMemoCount;
			return;
		}
		if (e.key == key)
		{
			e.blocked = blocked;
			return;
		}
	}
}

bool Pathfinder::isGroundLineClear(const Coord3D &a, const Coord3D &b)
{
	// RW 0x6EE5E3: the Bresenham cells from a to b on the ground layer meet no cell of another layer (lane WALL-1: the wall tops and ramps of the ground grid)
	if (m_wallPieces.empty())
	{
		return true;
	}
	ICoord2D s, e;
	{
		Coord3D p = a, q = b;
		worldToCell(&p, &s);
		worldToCell(&q, &e);
	}
	const bool met = walkLine(*this, LAYER_GROUND, s, e, [](PathfindCell *, PathfindCell *to, int, int) -> bool { return to->getLayer() != LAYER_GROUND; });
	return !met;
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
		unsigned rem = costToGoal(to, d.goal); // RW 0x6F1F0D (lane WALL-2)
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
	const SearcherFacts self = (m_memoActive && obj == m_memoObj) ? m_memoSelf : searcherFacts(obj);
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
				const OccupantFacts *f = occupantFacts(obj, node->owner);
				if (!f || f->o == obj || f->container == obj)
				{
					continue;
				}
				const PathfindObject *o = f->o;
				const bool enemy = f->rel == PATHFIND_ENEMIES;
				if (enemy && skipEnemies)
				{
					continue;
				}
				if (!self.hasAI || !f->hasAI)
				{
					continue;
				}
				if (self.aiPriority > f->aiPriority)
				{
					continue;
				}
				int add;
				if (f->parked)
				{
					add = 8;
				}
				else if (!self.hasLocomotor || !f->hasLocomotor)
				{
					add = 8;
				}
				else
				{
					const float qx = SimMath::mulf32((SimMath::addf32((float)i, 0.5f)), 10.0f), qy = SimMath::mulf32((SimMath::addf32((float)j, 0.5f)), 10.0f);
					if (selfT < 0.0f)
					{
						selfT = (m_memoActive && obj == m_memoObj) ? reachSeconds(*obj, m_memoSelfReach, qx, qy) : obj->secondsToReach(qx, qy);
					}
					const float oT = reachSeconds(*o, f->reach, qx, qy);
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
	int radius, const ICoord2D &startCellNdx, const PathfindObject *obj, bool hierarchical)
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
		// lane PATHPERF-1: RW 0x6F9850 reads the block flags once, for the +1000 below (RW 0x6F9DE7); ZH's rejection of a cell whose block is not passable
		// unless a block 3 cells away diagonally is was not RotWK's and is removed (it never applied while findPath marked every block passable). A cell
		// outside the hierarchical corridor is dear, not forbidden: the corridor's block search does not see walls inside a block
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
		// layer compatibility (RW step 3, BFME2 examineNeighboringCells 0x6F83F5; lane WALL-1): the same layer, one cell connecting to the other's layer, or
		// both connecting to the ramp layer. Every cell of a map without wall pieces is on the ground: always true there
		if (!PathfindCell::layersCompatible(*parentCell, *n))
		{
			continue;
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
			rem = costToGoal(n, goalCell); // RW 0x6F9D8F -> 0x6F1F0D (lane WALL-2)
		}
		if (hierarchical)
		{
			// lane PATHPERF-1, TARGET RW 0x6F9DA7 .. 0x6F9DC4: the corridor's remaining length, (last index - the cell's position (RW 0x93AD93)) * 320
			rem += (unsigned)((m_hierLast - hierarchicalPositionOf(nc.x, nc.y)) * 320);
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
	// TARGET RW 0x6FE7CB (lane PATHPERF-1, read from the disassembly; the old reading 0x6FE7FE is inside it):
	//   * *partial = 0; isHuman: the controlling player is not a computer (RW 0x68B678, player + 0x5C == 1 -> 0);
	//   * the goal copy; no quick path (RW 0x6F5BB0, the port's clientSafeQuickDoesPathExist) -> *partial = 1 and the goal steps back toward `from`:
	//     v = (goal - from) with z = 0, steps = -1 - _ftol2(|v| (RW 0x403111, the root left in the register) * -1/30 (RW 0xC1C4B4)), at most 5; v normalised
	//     (RW 0x405553: 1 / |v| times each component) and times 30 (RW 0xBDAE54); the goal -= v until RW 0x6F5BB0 passes or the steps are used;
	//   * the waypoint vector (+ 0x1C1CC) emptied, the block flags cleared (RW 0x937C23), FindClosestHierarchicalPath (RW 0x6F9829: the corridor and the
	//     adjusted goal); none -> every block passable (RW 0x937C65); an adjusted goal that is not the stepped goal -> *partial = 1;
	//   * internalFindPath (RW 0x6FD06F) to the adjusted goal with the corridor.
	bool localPartial = false;
	bool *p = partial ? partial : &localPartial;
	*p = false;
	const bool isHuman = obj ? !obj->isComputerControlled() : true;
	Coord3D to = *rawTo;
	if (!clientSafeQuickDoesPathExist(loco, from, &to, obj)) // RW 0x6F5BB0 with the unit (lane WALL-2: its portals)
	{
		*p = true;
		float dx = SimMath::subf32(to.x, from->x), dy = SimMath::subf32(to.y, from->y);
		const double lenW = SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f));
		int steps = -1 - (int)SimMath::ftol2Low32(SimMath::pc24MulW(lenW, (double)-0.0333333351f));
		if (steps > 5)
		{
			steps = 5;
		}
		const float inv = SimMath::divf32(1.0f, SimMath::fstpDword(lenW));
		dx = SimMath::mulf32(SimMath::mulf32(dx, inv), 30.0f);
		dy = SimMath::mulf32(SimMath::mulf32(dy, inv), 30.0f);
		for (int i = 0; i < steps; ++i)
		{
			to.x = SimMath::subf32(to.x, dx);
			to.y = SimMath::subf32(to.y, dy);
			if (clientSafeQuickDoesPathExist(loco, from, &to, obj))
			{
				break;
			}
		}
	}
	m_portalHops.clear(); // RW 0x6FE7CB (see above): the waypoint vector (+ 0x1C1CC) emptied before the block search (lane WALL-2)
	m_zoneManager.clearPassableFlags();
	Coord3D adjustedTo = to;
	// the block search runs on the ground layer only (S-162: its bridge links, RW 0x9389CE, are not ported), so a goal on a raised walkway (lane WALL-2)
	// gets no corridor and the full search runs on every block (review #63: the corridor moved such a goal to the ground beside it, a partial path)
	const bool hierarchical = !isRaisedLayer(searchDestinationLayer(to)) && findClosestHierarchicalPath(obj, loco, isHuman, from, &to, &adjustedTo);
	if (!hierarchical)
	{
		m_zoneManager.setAllPassable();
	}
	else if (adjustedTo.x != to.x || adjustedTo.y != to.y)
	{
		*p = true;
	}
	return internalFindPath(obj, loco, from, &adjustedTo, hierarchical, p);
}

// lane PATHPERF-1: the search memo is on for `obj` while the scope lives (a fresh generation each time; an object id's facts are asked once)
struct Pathfinder::SearchMemoScope
{
	Pathfinder &pf;
	SearchMemoScope(Pathfinder &p, const PathfindObject *obj) : pf(p)
	{
		pf.m_memoActive = obj != nullptr;
		pf.m_memoObj = obj;
		if (++pf.m_memoGen == 0)
		{
			for (OccupantFacts &f : pf.m_memo)
			{
				f.gen = 0;
			}
			pf.m_memoGen = 1;
		}
		if (obj)
		{
			pf.m_memoSelf = pf.searcherFacts(obj);
		}
		pf.m_memoSelfReach = ReachMemo();
	}
	~SearchMemoScope()
	{
		pf.m_memoActive = false;
		pf.m_memoObj = nullptr;
	}
};

Path *Pathfinder::findPathWithoutCorridor(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, bool *partial)
{
	// the search findPath was before lane PATHPERF-1 (every block passable, no goal step-back), kept for AIMover::pathReachingGoal: it stands for RW 0x6F74D0,
	// a search of its own (HORDE-2, S-532), not for findPath; and for the callers of pathfinder slot + 4 (FindClosestPath RW 0x6FB869, not ported: S-166)
	if (partial)
	{
		*partial = false;
	}
	m_zoneManager.setAllPassable();
	m_portalHops.clear(); // lane WALL-2: no corridor, no hops (a former search's would bias costToGoal)
	return internalFindPath(obj, loco, from, to, false, partial);
}

Path *Pathfinder::internalFindPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *rawTo, bool hierarchical,
	bool *partial)
{
	// RW 0x6FD06F only ever SETS *partial (a path to the closest cell); findPath clears it
	SearchMemoScope memo(*this, obj);
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
	const PathfindLayerEnum destinationLayer = searchDestinationLayer(*to); // ZH internalFindPath: TheTerrainLogic->getLayerForDestination(to)
	PathfindCell *goalCell = getCell(destinationLayer, to);
	if (goalCell == nullptr)
	{
		return nullptr;
	}
	ICoord2D goalNdx;
	worldToCell(to, true, &goalNdx);
	bool approx = false;
	{
		// TARGET RW 0x6FD306 .. 0x6FD3EC: checkDestination (RW 0x6F1584) with the unit's footprint, then checkForMovement (RW 0x6EB792) with the info's
		// radius 1 (RW 0x6FD341: the literal, not the footprint radius; lane BACKLOG5), flags 0xC for a HERO else 0x1C, transient, the AI's ignored obstacle
		int out = 0;
		const bool destOK = checkDestination(obj, goalNdx.x, goalNdx.y, destinationLayer, radius, centerInCell, &out, true);
		bool moveOK = true;
		if (destOK && obj)
		{
			PathfindCheckMovement info;
			info.cell = goalNdx;
			info.layer = destinationLayer;
			info.radius = 1;
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
		if (zS != zG && !portalPathExists(obj, loco, zS, zG))
		{
			// RW 0x6FD80C .. 0x6FD846: different zones and no portal chain between them (RW 0x6F5547, lane WALL-2): the goal is treated as unreachable
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
		info.flags = 0; // TARGET RW 0x6FD8DF: the start's check has the mode flags 0 (the port had the goal's 0xC / 0x1C here; lane BACKLOG5)
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
	parentCell->setTotalCost(costToGoal(parentCell, goalCell)); // RW 0x6FD6A8 -> 0x6F1F0D (lane WALL-2: through the corridor's portal hops)
	m_open.push(parentCell);
	int cellCount = 0;
	m_shortcutCounter = 0;
	PathfindCell *closest = nullptr;
	int bestClose = std::numeric_limits<int>::max();
	int bestPosition = 0; // RW [ebp + 0x24]: the best corridor position so far (0 to start with: a cell off the corridor, -1, never qualifies)
	int bestD2 = std::numeric_limits<int>::max();
	int slack = std::numeric_limits<int>::max();
	int closestAt = 0, candidateAt = 0; // lane UBER-3P: the cell counts when `closest`, and the candidate key, last changed
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		if (c == goalCell)
		{
			m_isTunneling = false;
			note(pathstops::kPartialRebuild); // RW 0x6FDCDA: the rebuild of the complete path (RW 0x767B0E) is not ported
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
			const int key = d2 + 10000 * (int)m_portalHops.size(); // RW 0x6FDA53 .. 0x6FDA6A: + 10000 per remaining portal hop of the corridor (lane WALL-2)
			// lane PATHPERF-1, TARGET RW 0x6FDA6C .. 0x6FDB7C: with a corridor a cell is a candidate when it is further along the corridor (RW 0x93AD93) than
			// the best so far, or as far and nearer (key); without, when it is nearer. The best is recorded BEFORE the cell's tests (RW 0x6F3082, 0x6E8200;
			// RW's third, 0x6EF3C3, is not ported: S-163), which only decide whether it becomes the closest cell: a nearer cell that fails shuts out the
			// farther ones, and only cells nearer than every one seen run the costly footprint test
			bool candidate;
			if (hierarchical)
			{
				const int position = hierarchicalPositionOf(c->getXIndex(), c->getYIndex());
				candidate = position > bestPosition || (position == bestPosition && key < bestClose);
				if (candidate)
				{
					bestPosition = position;
				}
			}
			else
			{
				candidate = key < bestClose;
			}
			if (candidate)
			{
				candidateAt = cellCount;
				bestClose = key;
				int out = 0;
				if (checkDestination(obj, c->getXIndex(), c->getYIndex(), destinationLayer, radius, centerInCell, &out, false) && out == 0 &&
					validMovementPosition(mv, c))
				{
					closest = c;
					closestAt = cellCount;
				}
			}
		}
		// lane UBER-3P (Uber profile only, docs/UBER.md): a search that cannot reach its goal (an approximate search: the goal is in another zone or
		// not a valid movement position; or the goal cell was closed, its footprint rejected) ends here once its answer has held for kUberStallCells
		// cells: the closest cell (or, with none yet, the candidate key that would test a new one) did not change, and the exhausted search's answer
		// (the path to that closest cell, or none) is given. In a battle the repaths toward a contact point in the press fail this way and kept
		// expanding up to the slack or MaxCellsFindPathLimit. Measured on the 30,000-troop clash (2,094 searches): a failed search's closest cell was
		// final after ~10 cells and never replaced 250 cells on; 1,182 of the 1,644 unreachable searches found no closest cell at all (72% of their
		// cells), 45 of them tested a candidate after such a pause. A tunnelling search tests no candidates: its pause starts when it leaves the obstacle
		if (m_uberLimits && (approx || goalCell->getClosed()))
		{
			if (m_isTunneling)
			{
				candidateAt = cellCount;
			}
			else if (cellCount - (closest != nullptr ? closestAt : candidateAt) >= kUberStallCells)
			{
				break;
			}
		}
		// TARGET RW 0x6FDB7F .. 0x6FDBD9: an approximate search skips a cell beyond the slack unless it is within 10 cells; with a corridor only after 2000 cells
		if (approx && d2 > slack && (!hierarchical || cellCount > 2000) && d2 >= 100)
		{
			continue; // too far from the goal to be worth expanding
		}
		checkChangeLayers(c, obj); // RW 0x6FDBE1 (lane WALL-2): the cell's other layer and its portal's links
		cellCount += examineNeighboringCells(c, goalCell, loco, isHuman, centerInCell, radius, startCellNdx, obj, hierarchical);
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
	// own cell, so the unit's position is used instead. Lane WALL-2 (read from the disassembly):
	//   * a cell at the index of the cell after it (a hop between layers, RW 0x6EF5D5 .. 0x6EF607) adds no node: the node after it takes its layer (the
	//     cell's, or the later cell's when the cell is on the ground) and the walk goes on without making it the previous cell;
	//   * a cell reached through a portal (info + 0xC, RW 0x6EF647 .. 0x6EF71E): its node stands at the portal waypoint's position; before it come, in path
	//     order, the waypoint's intermediates (+ 0x44, until one with links) and, when the cell is itself a portal's start, that portal's position; each of
	//     those nodes and the node after them get the id of the waypoint the step to them starts from (RW 0x6653DC writes the id of the node prepended before
	//     them: the node after, + 0x20) and cannot be optimised (+ 0x1C);
	//   * a node's layer is TheTerrainLogic::getLayerForDestination of its position (RW 0x680A75: a ramp under it, else the ground grid cell's layer: the
	//     port's getLayerAt; lane WALL-1 used the cell's layer, which differs only where a wall cell lies under a ramp's surface).
	path->setWaypointSource(this);
	Coord3D pos;
	PathfindCell *cell, *prevCell = nullptr;
	const bool goalCellNull = (goalCell->getParentCell() == nullptr);
	for (cell = goalCell; cell->getParentCell(); cell = cell->getParentCell())
	{
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		if (prevCell && prevCell->getXIndex() == cell->getXIndex() && prevCell->getYIndex() == cell->getYIndex())
		{
			PathfindLayerEnum layer = cell->getLayer();
			if (layer == LAYER_GROUND)
			{
				layer = prevCell->getLayer();
			}
			if (path->getFirstNode())
			{
				path->getFirstNode()->setLayer(layer);
			}
			continue;
		}
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
		if (const PathfindWaypoint *wp = waypointOfSlot(cell->getViaPortal()))
		{
			pos = wp->pos; // RW 0x6EF66A
			if (wp->id != PathfindWaypoint::NO_ID)
			{
				auto mark = [&](int id) {
					if (path->getFirstNode())
					{
						path->getFirstNode()->setWaypointID(id);
						path->getFirstNode()->setCanOptimize(false);
					}
				};
				if (const PathfindWaypoint *own = waypointOfSlot(cell->getPortal()))
				{
					mark(own->id);
					Coord3D p = own->pos;
					path->prependNode(&p, cell->getLayer());
				}
				// review of #63: a Via chain that loops back on itself (DynamicPortalBehaviour's Link data) ends at its first repeat; a chain without one is walked
				// as before
				std::vector<int> seen;
				for (const PathfindWaypoint *n = findWaypoint(wp->next); n && std::find(seen.begin(), seen.end(), n->id) == seen.end(); n = findWaypoint(n->next))
				{
					seen.push_back(n->id);
					mark(n->id);
					Coord3D p = n->pos;
					path->prependNode(&p, cell->getLayer());
					if (n->numLinks != 0)
					{
						break;
					}
				}
				mark(wp->id);
			}
		}
		path->prependNode(&pos, m_wallPieces.empty() ? cell->getLayer() : getLayerAt(pos.x, pos.y, pos.z)); // RW 0x6EF721 .. 0x6EF741 (see above)
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
