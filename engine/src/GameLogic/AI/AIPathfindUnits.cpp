// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Mobile units on the pathfind grid: footprint size, goal and position reservation, the movement validity and destination checks,
// destination adjustment, the request queue. Port of the RotWK pathfinder (target) with ZH AIPathfind.cpp as the structural donor.
//
// TARGET FACTS (RW game.dat; S-001 caveat applies to every address):
//   * footprint size RW 0x6EAF79 (static, returns the diameter in cells), radius / centre RW 0x6ED071, cell of a position RW 0x6E8CE6
//     (floor(pos * 0.1), plus 0.5 when the footprint is not centred);
//   * validMovementPosition RW 0x6E8200, the movement struct builder RW 0x6EA04D, the cell test RW 0x6EA4E7;
//   * checkForMovement RW 0x6EB792 (full) / 0x6EBAA0 (incremental over a previous footprint), its info struct RW 0x6E9119;
//   * checkDestination RW 0x6F1584 with the blocker callback RW 0x6EB151;
//   * reservations: slot records of the object's pathfind module (RW obj+0xA4: position slot, goal slot, kind-1 slot), updateGoal RW
//     0x8E24D3 / removeGoal 0x8E2138 / updatePos 0x8E26B7 / release 0x8E1CC4 / register 0x8E231B / cell add 0x8E1DAD (head insertion into
//     one of five occupant lists per cell info);
//   * adjustDestination RW 0x6FE456 with the ring search 0x6FA2A4 and the per-cell test 0x6F66D9 / 0x6F83EB;
//   * the request queue and processPathfindQueue RW 0x6F2364, 0x6ED0FB.
// DONOR (ZH AIPathfind.cpp:4924-5650, 5648-5965, 9694-10112): the shape of the above where RW was not read. See docs/STOPS.md S-163 and
// S-164 for what is not ported (kind-1 slot, rotated-rectangle fill, the AI target rules of checkForMovement, the partition grid).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/AI/AIPathfindStops.h"

#include <cmath>
#include <cstdlib>

namespace
{
inline int realToIntFloor(float f)
{
	return SimMath::floorToInt(f);
}

// RW 0x8E1A99: an angle quantised to 12 steps (0 when it is not in [0, 11])
int angleToCode(float a)
{
	const float twoPi = 6.28318530718f;
	const float pi = 3.14159265359f;
	while (a > pi) a = SimMath::subf32(a, twoPi);
	while (a <= -pi) a = SimMath::addf32(a, twoPi);
	if (a < 0.0f) a = SimMath::addf32(a, twoPi);
	const float v = SimMath::divf32(SimMath::mulf32(a, 12.0f), twoPi);
	if (v > 11.0f) return 0;
	return SimMath::truncToInt32(SimMath::addf32(v, 0.5f));
}
} // namespace

Pathfinder::UnitRecord &Pathfinder::unit(PathfindObjectID id)
{
	return m_units[id];
}

ICoord2D Pathfinder::pathfindGoalCell(PathfindObjectID id) const
{
	ICoord2D c;
	c.x = c.y = -1;
	auto it = m_units.find(id);
	if (it != m_units.end() && it->second.goal.occupied)
	{
		return it->second.goal.cell;
	}
	return c;
}

ICoord2D Pathfinder::curPathfindCell(PathfindObjectID id) const
{
	ICoord2D c;
	c.x = c.y = -1;
	auto it = m_units.find(id);
	if (it != m_units.end() && it->second.pos.occupied)
	{
		return it->second.pos.cell;
	}
	return c;
}

bool Pathfinder::goalRegistered(PathfindObjectID id) const
{
	auto it = m_units.find(id);
	return it != m_units.end() && it->second.goal.occupied && !it->second.goal.cells.empty();
}

bool Pathfinder::positionRegistered(PathfindObjectID id) const
{
	auto it = m_units.find(id);
	return it != m_units.end() && it->second.pos.occupied && !it->second.pos.cells.empty();
}

// ---------------------------------------------------------------------------------------------------------
// footprint size
// ---------------------------------------------------------------------------------------------------------
int Pathfinder::footprintSize(const PathfindObject *obj) const
{
	// TARGET RW 0x6EAF79: the largest footprint is 4 * 2 + 1 for MONSTER, HORDE and SHIP templates, 2 * 2 + 1 otherwise
	int maxR = 2;
	if (obj->isKindOf(PK_MONSTER) || obj->isKindOf(PK_HORDE) || obj->isKindOf(PK_SHIP))
	{
		maxR = 4;
	}
	float d = SimMath::mulf32(obj->getGeometry().boundingCircleRadius(), 2.0f);
	if (d > 10.0f && d < 20.0f)
	{
		d = 20.0f;
	}
	if (obj->getPathfindDiameter() > 0.0f)
	{
		d = obj->getPathfindDiameter();
	}
	int r = realToIntFloor(SimMath::addf32(SimMath::mulf32(d, 0.1f), 0.3f));
	if (r == 0)
	{
		return 1;
	}
	if (r > 2 * maxR)
	{
		return 2 * maxR + 1;
	}
	return r;
}

void Pathfinder::getRadiusAndCenter(const PathfindObject *obj, int &iRadius, bool &center) const
{
	// TARGET RW 0x6ED071: the footprint size (RW 0x6EAF79) halved, centred when it is odd. Lane MOVE-3: it has no horde / ship branch; only the A* neighbour
	// expansion (RW 0x6F9850's prologue, examineNeighboringCells) and the line test (RW 0x6EE12D, isLinePassable) narrow a HORDE or SHIP to radius 1, centred. The port took the narrow footprint for every caller,
	// so a horde's destination adjustment (RW 0x6EE84E -> 0x6ED071), its goal and position reservation and the blocked repath tested a 3 x 3 footprint where
	// RotWK tests the horde's own (9 x 9 cells for a bounding circle of 54: GondorFighterHorde's 30 x 45 box)
	if (!obj)
	{
		center = true;
		iRadius = 0;
		return;
	}
	const int r = footprintSize(obj);
	iRadius = r / 2;
	center = (r & 1) != 0;
}

bool Pathfinder::worldToCell(const Coord3D *pos, bool center, ICoord2D *cell)
{
	if (center)
	{
		cell->x = realToIntFloor(SimMath::mulf32(pos->x, 0.1f));
		cell->y = realToIntFloor(SimMath::mulf32(pos->y, 0.1f));
	}
	else
	{
		cell->x = realToIntFloor(SimMath::addf32(SimMath::mulf32(pos->x, 0.1f), 0.5f));
		cell->y = realToIntFloor(SimMath::addf32(SimMath::mulf32(pos->y, 0.1f), 0.5f));
	}
	bool overflow = false;
	if (cell->x < m_extent.lo.x) { overflow = true; cell->x = m_extent.lo.x; }
	if (cell->y < m_extent.lo.y) { overflow = true; cell->y = m_extent.lo.y; }
	if (cell->x > m_extent.hi.x) { overflow = true; cell->x = m_extent.hi.x; }
	if (cell->y > m_extent.hi.y) { overflow = true; cell->y = m_extent.hi.y; }
	return overflow;
}

// ---------------------------------------------------------------------------------------------------------
// movement validity
// ---------------------------------------------------------------------------------------------------------
unsigned Pathfinder::validLocomotorSurfacesForCellType(PathfindCell::CellType t)
{
	// TARGET RW table 0xDA2444 indexed by the 3-bit cell type (bits are the LocomotorSurfaceType flags of Locomotor.h: GROUND 1,
	// WATER 2, CLIFF 4, AIR 8, RUBBLE 0x10, OBSTACLE 0x20, IMPASSABLE 0x40, DEEP_WATER 0x80)
	static const unsigned table[8] = { 0x09, 0x0A, 0x0C, 0x18, 0x28, 0x48, 0x48, 0x88 };
	return table[(unsigned)t & 7u];
}

PathfindMovement Pathfinder::makeMovement(const PathfindObject *obj, const PathfindLocomotorInfo &loco)
{
	// TARGET RW 0x6EA04D
	PathfindMovement mv;
	mv.surf = loco.validSurfaces;
	mv.bC = loco.crusher;
	if (obj)
	{
		mv.b4 = !obj->canPathThroughGates();
		mv.b5 = !obj->isComputerControlled();
		mv.i8 = obj->getSlopeLimitIndex() - 1;
	}
	else
	{
		mv.b4 = true; // no template: CanPathThroughGates reads 0
		mv.b5 = true;
	}
	return mv;
}

bool Pathfinder::validMovementPosition(const PathfindMovement &mv, const PathfindCell *cell) const
{
	// TARGET RW 0x6E8200 (S-001 caveat)
	if (!cell)
	{
		return false;
	}
	if (cell->getImpassableToPlayers() && mv.b5)
	{
		return false;
	}
	const PathfindCell::CellType t = cell->getType();
	if (t == PathfindCell::CELL_RUBBLE)
	{
		if (cell->getLayer() != LAYER_GROUND)
		{
			return false; // type 3 only on the ground layer
		}
	}
	else if (t == PathfindCell::CELL_OBSTACLE)
	{
		// with an ignore id of 0 an info-less obstacle cell passes (RW: obstacle id 0 == pathfinder+0x48 == 0)
		const PathfindObjectID obs = cell->getObstacleID();
		if (obs == m_ignoreObstacleID)
		{
			return true;
		}
	}
	if ((mv.surf & validLocomotorSurfacesForCellType(t)) == 0)
	{
		return false;
	}
	if (mv.b4 && cell->getBit17())
	{
		return false;
	}
	if (mv.bC && !cell->getBit22() && t == PathfindCell::CELL_OBSTACLE)
	{
		return false;
	}
	if (mv.i8 >= 0 && cell->getSlopeGrade() > mv.i8)
	{
		return false;
	}
	return true;
}

bool Pathfinder::validMovementPosition(const PathfindObject *obj, const PathfindLocomotorInfo &loco, PathfindLayerEnum layer, const Coord3D *pos)
{
	const int x = SimMath::truncToInt32(SimMath::mulf32(pos->x, 0.1f));
	const int y = SimMath::truncToInt32(SimMath::mulf32(pos->y, 0.1f));
	const PathfindMovement mv = makeMovement(obj, loco);
	return validMovementPosition(mv, getCell(layer, x, y));
}

// ---------------------------------------------------------------------------------------------------------
// checkForMovement (RW 0x6EB792 / 0x6EBAA0)
// ---------------------------------------------------------------------------------------------------------
bool Pathfinder::checkForMovement(const PathfindObject *obj, PathfindCheckMovement &info, const ICoord2D *prev)
{
	info.allyFixed = false;
	info.allyPresent = false;
	info.goalListNonEmpty = false;
	info.terrainPenalty = 0;
	if (!obj)
	{
		return true;
	}
	const bool isHuman = !obj->isComputerControlled();
	const int n = info.radius + (info.center ? 1 : 0);
	PathfindObjectID lastId = PATHFIND_INVALID_ID;
	for (int i = info.cell.x - info.radius; i < info.cell.x + n; i++)
	{
		for (int j = info.cell.y - info.radius; j < info.cell.y + n; j++)
		{
			const PathfindCell *c = getCell(info.layer, i, j);
			if (!c)
			{
				return false; // off the map
			}
			bool covered = false;
			if (prev)
			{
				// the cell lies inside the previous footprint
				const int px = prev->x, py = prev->y;
				covered = i >= px - info.radius && i < px + n && j >= py - info.radius && j < py + n;
			}
			// terrain (always)
			if (c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE)
			{
				info.terrainPenalty++;
			}
			if (c->getImpassableToPlayers() && isHuman)
			{
				info.terrainPenalty++;
			}
			if ((info.flags & 8) && info.layer != c->getLayer())
			{
				bool bad;
				if (info.layer == LAYER_GROUND)
				{
					bad = (int)c->getLayer() != 0x10;
				}
				else
				{
					bad = (int)info.layer >= 0x11 && (int)info.layer <= 0x40 && (int)c->getLayer() != 0x10;
				}
				if (bad) info.terrainPenalty++;
			}
			if ((info.flags & 4) && !validMovementPosition(info.mv, c))
			{
				info.terrainPenalty++;
			}
			if (covered)
			{
				continue;
			}
			if (c->hasOccupants(OCC_GROUND_GOAL))
			{
				info.goalListNonEmpty = true;
			}
			// the units standing on the cell (the position list, RW info+0x20)
			for (const PathfindOccupant *node = c->occupants(OCC_POSITION); node; node = node->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
				if (!o)
				{
					continue;
				}
				if (o == obj || o->getID() == info.ignoreId || o->getID() == lastId)
				{
					continue;
				}
				lastId = o->getID();
				const bool ally = obj->getRelationship(*o) == PATHFIND_ALLIES;
				if (ally)
				{
					info.allyPresent = true;
				}
				const bool check = (info.transient && ally) || o->isParked() || (!ally && (info.flags & 0x10));
				if (!check)
				{
					continue;
				}
				if (ally && obj->pathsThroughEachOther() && o->pathsThroughEachOther())
				{
					continue;
				}
				if (obj->isKindOf(PK_PATH_THROUGH_INFANTRY) && o->isKindOf(PK_INFANTRY))
				{
					continue;
				}
				if (ally && obj->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
				{
					if (!o->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
					{
						info.goalListNonEmpty = false;
						continue;
					}
				}
				if (ally)
				{
					if (!o->hasAI())
					{
						return false;
					}
					if (info.flags & 2)
					{
						return false;
					}
					info.allyFixed = true;
					if (!o->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
					{
						continue;
					}
					if (obj->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
					{
						continue;
					}
					return false;
				}
				// an enemy
				if (obj->canCrushOrSquish(*o))
				{
					continue;
				}
				if (!(info.flags & 0x11))
				{
					continue;
				}
				if (!obj->hasAI())
				{
					return false;
				}
				if (obj->ignoresAsTarget(*o))
				{
					continue;
				}
				return false;
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// checkDestination (RW 0x6F1584, blocker callback 0x6EB151)
// ---------------------------------------------------------------------------------------------------------
bool Pathfinder::blockerCallback(const PathfindObject *obj, const PathfindCell *cell, int *count, bool countAllies, PathfindObjectID ignoreId,
	bool ignoreUnits, const PathfindMovement *rectMovement)
{
	// TARGET RW 0x6EB151: true = the cell blocks the footprint
	if (rectMovement && obj->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
	{
		if (!validMovementPosition(*rectMovement, cell))
		{
			return true;
		}
	}
	const PathfindCell::CellType t = cell->getType();
	const bool isHuman = !obj->isComputerControlled();
	if (t == PathfindCell::CELL_BRIDGE_IMPASSABLE || (cell->getImpassableToPlayers() && isHuman) || t == PathfindCell::CELL_CLIFF)
	{
		return true;
	}
	if (t == PathfindCell::CELL_OBSTACLE)
	{
		// RW 0x6E80B5: an obstacle blocks unless it is the ignored one
		return !(cell->getObstacleID() == ignoreId && ignoreId != PATHFIND_INVALID_ID);
	}
	if (t == PathfindCell::CELL_IMPASSABLE)
	{
		return true;
	}
	if (ignoreUnits)
	{
		return false;
	}
	const PathfindObject *top = obj->getTopContainer();
	for (const PathfindOccupant *node = cell->occupants(OCC_GROUND_GOAL); node; node = node->next)
	{
		const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
		if (!o)
		{
			continue;
		}
		if (o == obj)
		{
			continue;
		}
		if (top && o->getTopContainer() == top)
		{
			continue;
		}
		if (o->getID() == ignoreId)
		{
			continue;
		}
		const PathfindRelationship rel = obj->getRelationship(*o);
		if (rel == PATHFIND_ALLIES)
		{
			if (!obj->isKindOf(PK_HEAVY_MELEE_HITTER))
			{
				if (!countAllies)
				{
					return true;
				}
				if (count) ++*count;
			}
			else if (o->isKindOf(PK_HEAVY_MELEE_HITTER))
			{
				return true; // heavy hitters never share a goal cell
			}
		}
		else if (o->isParked())
		{
			if (obj->canCrushOrSquish(*o))
			{
				continue;
			}
			if (!(obj->isKindOf(PK_PATH_THROUGH_INFANTRY) && o->isKindOf(PK_INFANTRY)))
			{
				return true;
			}
		}
	}
	return false;
}

bool Pathfinder::checkDestination(const PathfindObject *obj, int cellX, int cellY, PathfindLayerEnum layer, int iRadius, bool centerInCell, int *out,
	bool ignoreUnits)
{
	if (out)
	{
		*out = 0;
	}
	if (!obj)
	{
		// ZH: "obj == NULL means checking for any ground units present": the footprint is one cell, the terrain decides
		const PathfindCell *cell = getCell(layer, cellX, cellY);
		if (!cell)
		{
			return false;
		}
		const PathfindCell::CellType t = cell->getType();
		return !(t == PathfindCell::CELL_BRIDGE_IMPASSABLE || t == PathfindCell::CELL_CLIFF || t == PathfindCell::CELL_OBSTACLE || t == PathfindCell::CELL_IMPASSABLE);
	}
	int r = iRadius;
	bool c = centerInCell;
	if (obj->isKindOf(PK_HORDE))
	{
		// RW: (layer == 1 && !flag && !TerrainLogic[+0xC8](cell centre)) keeps the footprint; the terrain query is not decoded
		// (treated as false, S-164)
		if (!(layer == LAYER_GROUND && !ignoreUnits))
		{
			r = 1;
			c = true;
		}
	}
	else if (obj->isKindOf(PK_HERO) && obj->isKindOf(PK_INFANTRY) && layer != LAYER_GROUND)
	{
		r = 1;
		c = false;
	}
	const int n = r + (c ? 1 : 0);
	if (!obj->isComputerControlled())
	{
		if (cellX - r < m_logicalExtent.lo.x || cellX + n > m_logicalExtent.hi.x || cellY - r < m_logicalExtent.lo.y || cellY + n >= m_logicalExtent.hi.y + 1)
		{
			return false;
		}
	}
	const PathfindObjectID ignoreId = obj->hasAI() ? obj->getIgnoredObstacleID() : PATHFIND_INVALID_ID;
	const bool aircraft = obj->hasAI() ? obj->isAircraftThatAdjustsDestination() : false;
	const PathfindObjectID selfId = obj->hasAI() ? obj->getID() : PATHFIND_INVALID_ID;
	if (!aircraft && obj->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
	{
		// the rotated rectangle of the unit (RW 0x6F0195, the polygon fill 0x6ECC06 is not decoded: the footprint is the box of
		// the same size around the cell, S-164)
		const PathfindGeometry &g = obj->getGeometry();
		int w = (int)((g.majorRadius * 2.0f + 4.0f) * 0.1f);
		int h = (int)((g.minorRadius * 2.0f + 4.0f) * 0.1f);
		int count = 0, slots = 0;
		if (obj->hordeFill(count, slots))
		{
			// lane MOVE-3: RW 0x6F18A0 .. 0x6F1902: a horde's box shrinks with its fill, (size - 2) * count / slots + 2 (fild / fidiv / fsub / fmul / fadd under
			// the game's 24-bit precision control: float results for these small integers; _ftol2 truncates)
			const float ratio = SimMath::divf32((float)count, (float)slots);
			w = SimMath::truncToInt32(SimMath::addf32(SimMath::mulf32(SimMath::subf32((float)w, 2.0f), ratio), 2.0f));
			h = SimMath::truncToInt32(SimMath::addf32(SimMath::mulf32(SimMath::subf32((float)h, 2.0f), ratio), 2.0f));
		}
		note(pathstops::kLargeRectFootprint);
		const int x0 = cellX - w / 2, y0 = cellY - h / 2;
		for (int i = x0; i < x0 + (w > 0 ? w : 1); i++)
		{
			for (int j = y0; j < y0 + (h > 0 ? h : 1); j++)
			{
				const PathfindCell *cell = getCell(layer, i, j);
				if (!cell) return false;
				const PathfindMovement mv = makeMovement(obj, PathfindLocomotorInfo{ 0xFFFFFFFFu, false, false });
				if (blockerCallback(obj, cell, out, false, ignoreId, ignoreUnits, &mv)) return false;
			}
		}
		return true;
	}
	for (int i = cellX - r; i < cellX + n; i++)
	{
		for (int j = cellY - r; j < cellY + n; j++)
		{
			const PathfindCell *cell = getCell(layer, i, j);
			if (!cell)
			{
				return false;
			}
			if (aircraft)
			{
				// RW 0x9344B8: an aircraft goal held by another unit
				bool blocked = false;
				for (const PathfindOccupant *o = cell->occupants(OCC_AIR_GOAL); o; o = o->next)
				{
					if (o->owner != selfId) blocked = true;
				}
				if (blocked) return false;
			}
			else if (blockerCallback(obj, cell, out, false, ignoreId, ignoreUnits, nullptr))
			{
				return false;
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// reservations
// ---------------------------------------------------------------------------------------------------------
ICoord2D Pathfinder::cellOfPosition(const PathfindObject &obj, const Coord3D &pos) const
{
	// RW 0x6ECFDE: worldToCell(pos, centre = (footprint size is odd)), clamped
	const int r = footprintSize(&obj);
	ICoord2D cell;
	const bool center = (r & 1) != 0;
	if (center)
	{
		cell.x = realToIntFloor(SimMath::mulf32(pos.x, 0.1f));
		cell.y = realToIntFloor(SimMath::mulf32(pos.y, 0.1f));
	}
	else
	{
		cell.x = realToIntFloor(SimMath::addf32(SimMath::mulf32(pos.x, 0.1f), 0.5f));
		cell.y = realToIntFloor(SimMath::addf32(SimMath::mulf32(pos.y, 0.1f), 0.5f));
	}
	if (cell.x < m_extent.lo.x) cell.x = m_extent.lo.x;
	if (cell.y < m_extent.lo.y) cell.y = m_extent.lo.y;
	if (cell.x > m_extent.hi.x) cell.x = m_extent.hi.x;
	if (cell.y > m_extent.hi.y) cell.y = m_extent.hi.y;
	return cell;
}

void Pathfinder::footprintCells(const PathfindObject &obj, const ICoord2D &cell, int angleCode, std::vector<ICoord2D> &out)
{
	out.clear();
	int w, h;
	if (obj.isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
	{
		const PathfindGeometry &g = obj.getGeometry();
		w = (int)((g.majorRadius * 2.0f + 4.0f) * 0.1f);
		h = (int)((g.minorRadius * 2.0f + 4.0f) * 0.1f);
		if (w < 1) w = 1;
		if (h < 1) h = 1;
		if (angleCode != 0)
		{
			note(pathstops::kLargeRectReservation);
		}
	}
	else
	{
		w = h = footprintSize(&obj);
	}
	const int x0 = cell.x - w / 2;
	const int y0 = cell.y - h / 2;
	for (int i = x0; i < x0 + w; i++)
	{
		for (int j = y0; j < y0 + h; j++)
		{
			if (getCell(LAYER_GROUND, i, j))
			{
				ICoord2D c;
				c.x = i;
				c.y = j;
				out.push_back(c);
			}
		}
	}
}

void Pathfinder::releaseSlot(PathfindObjectID id, Slot &slot)
{
	// RW 0x8E1CC4: unlink the unit's node from each cell of the chain; a cell info with no node left is released
	for (const ICoord2D &c : slot.cells)
	{
		PathfindCell *cell = getCell(LAYER_GROUND, c.x, c.y);
		if (cell)
		{
			cell->removeOccupant(m_pool, slot.kind, id);
		}
	}
	slot.cells.clear();
	slot.occupied = false;
}

void Pathfinder::registerSlot(PathfindObject &obj, Slot &slot)
{
	// RW 0x8E231B + 0x8E216E + 0x8E1DAD: the footprint box around the slot cell gets a node of the slot's kind
	footprintCells(obj, slot.cell, slot.angleCode, slot.cells);
	for (const ICoord2D &c : slot.cells)
	{
		PathfindCell *cell = getCell(LAYER_GROUND, c.x, c.y);
		cell->addOccupant(m_pool, slot.kind, obj.getID(), c);
	}
}

void Pathfinder::updateGoal(PathfindObject &obj, const Coord3D *newGoalPos, PathfindLayerEnum layer)
{
	// the wrappers RW 0x68B3BD / 0x68B3AB (-> 0x8E28DB / 0x8E28AC) pass the goal slot's own angle (RW 0x8E1AEC on the unit record's goal slot + 0x24), so a
	// heading reserved by RW 0x68B3CF survives a plain update; the orientation only replaces it in RW 0x8E24D3's same-cell rule
	updateGoalAngle(obj, newGoalPos, goalAngle(obj.getID()), layer);
}

void Pathfinder::updateGoalAngle(PathfindObject &obj, const Coord3D *newGoalPos, float angle, PathfindLayerEnum layer)
{
	// TARGET RW 0x8E24D3 (lane MOVE-3: the angle argument, RW 0x68B3CF passes one)
	if (!m_isMapReady)
	{
		return;
	}
	if (obj.isEffectivelyDead())
	{
		return; // RW 0x8E24D3: nothing when the object's + 0x458 bit 0 is set (lane MOVE-3)
	}
	UnitRecord &u = unit(obj.getID());
	int angleCode = 0;
	if (obj.isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
	{
		angleCode = angleToCode(angle);
	}
	const ICoord2D c = cellOfPosition(obj, *newGoalPos);
	if (obj.isKindOf(PK_LARGE_RECTANGLE_PATHFIND) && u.pos.occupied && c.x == u.pos.cell.x && c.y == u.pos.cell.y)
	{
		angleCode = angleToCode(obj.getOrientation());
	}
	if (u.goal.occupied && c.x == u.goal.cell.x && c.y == u.goal.cell.y && angleCode == u.goal.angleCode && layer == u.goal.layer)
	{
		return; // unchanged
	}
	if (obj.isKindOf(PK_HORDE))
	{
		// horde goals are never registered: the cell is only remembered
		releaseSlot(obj.getID(), u.goal);
		u.goal.occupied = true;
		u.goal.cell = c;
		u.goal.angleCode = angleCode;
		u.goal.layer = layer;
		// lane MOVE-3: then the horde contain interface's slot 0xC8 (RW 0x86EF13) reserves the members' goals at their slots around this goal: what a second horde's
		// destination adjustment meets (the port skipped it: two hordes sent to one point stood on it together, community feedback FB-0006)
		obj.onHordeGoalChanged();
		return;
	}
	PathfindOccupantKind kind = OCC_GROUND_GOAL;
	if (obj.hasAI())
	{
		if (obj.isDoingGroundMovement())
		{
			kind = OCC_GROUND_GOAL;
		}
		else if (obj.isAircraftThatAdjustsDestination())
		{
			kind = OCC_AIR_GOAL;
		}
		else
		{
			return;
		}
	}
	if (u.goal.occupied)
	{
		releaseSlot(obj.getID(), u.goal);
	}
	u.goal.occupied = true;
	u.goal.cell = c;
	u.goal.angleCode = angleCode;
	u.goal.layer = layer;
	u.goal.kind = kind;
	registerSlot(obj, u.goal);
}

void Pathfinder::removeGoal(PathfindObject &obj)
{
	// RW 0x8E2138
	auto it = m_units.find(obj.getID());
	if (it != m_units.end() && it->second.goal.occupied)
	{
		releaseSlot(obj.getID(), it->second.goal);
	}
}

void Pathfinder::updatePos(PathfindObject &obj)
{
	// TARGET RW 0x8E26B7: only a stationary ground unit holds a position
	if (!m_isMapReady)
	{
		return;
	}
	UnitRecord &u = unit(obj.getID());
	if (!obj.hasAI() || !obj.isDoingGroundMovement() || !obj.isStationary())
	{
		if (u.pos.occupied)
		{
			releaseSlot(obj.getID(), u.pos);
		}
		return;
	}
	const PathfindOccupantKind kind = obj.isKindOf(PK_HORDE) ? OCC_HORDE_POSITION : OCC_POSITION;
	const int angleCode = obj.isKindOf(PK_LARGE_RECTANGLE_PATHFIND) ? angleToCode(obj.getOrientation()) : 0;
	const ICoord2D c = cellOfPosition(obj, obj.getPosition());
	const PathfindLayerEnum lyr = obj.getLayer();
	if (u.pos.occupied && c.x == u.pos.cell.x && c.y == u.pos.cell.y && angleCode == u.pos.angleCode && lyr == u.pos.layer)
	{
		return;
	}
	if (u.pos.occupied)
	{
		releaseSlot(obj.getID(), u.pos);
	}
	if (!getCell(lyr, c.x, c.y))
	{
		return;
	}
	u.pos.occupied = true;
	u.pos.cell = c;
	u.pos.angleCode = angleCode;
	u.pos.layer = lyr;
	u.pos.kind = kind;
	registerSlot(obj, u.pos);
}

void Pathfinder::removePos(PathfindObject &obj)
{
	// RW 0x8E215C
	auto it = m_units.find(obj.getID());
	if (it != m_units.end() && it->second.pos.occupied)
	{
		releaseSlot(obj.getID(), it->second.pos);
	}
}

void Pathfinder::removeUnitFromPathfindMap(PathfindObject &obj)
{
	// RW 0x8E2304 releases all the slots
	removePos(obj);
	removeGoal(obj);
}

// ---------------------------------------------------------------------------------------------------------
// destinations
// ---------------------------------------------------------------------------------------------------------
void Pathfinder::adjustCoordToCell(int cellX, int cellY, bool centerInCell, Coord3D &pos, PathfindLayerEnum layer)
{
	(void)layer;
	if (centerInCell)
	{
		pos.x = SimMath::mulf32((SimMath::addf32((float)cellX, 0.5f)), 10.0f);
		pos.y = SimMath::mulf32((SimMath::addf32((float)cellY, 0.5f)), 10.0f);
	}
	else
	{
		// ZH / RW: (Real)cellX + 0.05 is a double sum, stored as float after the multiply
		pos.x = (float)SimMath::mulD(SimMath::addD((double)cellX, 0.05), (double)10.0f);
		pos.y = (float)SimMath::mulD(SimMath::addD((double)cellY, 0.05), (double)10.0f);
	}
	pos.z = m_terrain ? m_terrain->getGroundHeight(pos.x, pos.y) : 0.0f;
}

namespace
{
// RW 0x6E95B2: a Bresenham walk of cells from `a` to `b` on `layer`; true (blocked) at the first cell other than `skip` whose type is neither CLEAR nor WATER or
// that is pinched (cell dword bits 0-3 / bit 16). A cell off the grid ends the walk unblocked (RW returns 0 there).
bool memberLineBlocked(Pathfinder &pf, ICoord2D a, ICoord2D b, PathfindLayerEnum layer, ICoord2D skip)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	const bool steep = dx < dy;
	int d, diag, count, minor;
	if (steep)
	{
		d = dx * 2 - dy;
		diag = dx - dy;
		count = dy;
		minor = dx;
	}
	else
	{
		diag = dy - dx;
		d = dy * 2 - dx;
		count = dx;
		minor = dy;
	}
	int xs1 = steep ? 0 : 1, ys1 = steep ? 1 : 0; // the straight step (d < 0)
	int xs2 = 1, ys2 = 1;                         // the diagonal step
	int x = a.x, y = a.y;
	if (b.x < x)
	{
		xs1 = -xs1;
		xs2 = -1;
	}
	if (b.y < y)
	{
		ys1 = -ys1;
		ys2 = -1;
	}
	for (int k = 0; k < count + 1; ++k)
	{
		const PathfindCell *c = pf.getCell(layer, x, y);
		if (!c)
		{
			return false;
		}
		if (x != skip.x || y != skip.y)
		{
			const PathfindCell::CellType t = c->getType();
			if ((t != PathfindCell::CELL_CLEAR && t != PathfindCell::CELL_WATER) || c->getPinched())
			{
				return true;
			}
		}
		int sx = xs2, sy = ys2, inc = diag;
		if (d < 0)
		{
			sx = xs1;
			sy = ys1;
			inc = minor;
		}
		d += inc * 2;
		x += sx;
		y += sy;
	}
	return false;
}
} // namespace

// RW 0x6EA5B2 with the scanner RW 0x6E87F9 builds in RW 0x6F0889: the member's footprint at `cell` (radius / centre of RW 0x6ED071)
bool Pathfinder::memberFootprintFits(const PathfindMovement &mv, PathfindLayerEnum memberLayer, PathfindLayerEnum hordeLayer, const ICoord2D &hordeCell,
	int radius, bool center, const ICoord2D &cell)
{
	const int n = radius + (center ? 1 : 0);
	bool first = true;
	float h0 = 0.0f;
	for (int i = cell.x - radius; i < cell.x + n; ++i)
	{
		for (int j = cell.y - radius; j < cell.y + n; ++j)
		{
			const PathfindCell *c = getCell(memberLayer, i, j);
			if (!c || c->getPinched())
			{
				return false;
			}
			const PathfindLayerEnum cl = c->getLayer();
			if (cl != hordeLayer)
			{
				// RW: a cell of another layer than the horde's fails unless it is the wall layer, or the horde is on a raised layer whose tests (RW 0x6E82B3 /
				// 0x5E2F24) pass; only the ground layer is ported (S-161): any other mismatch fails
				if (hordeLayer == LAYER_GROUND && cl != LAYER_WALL)
				{
					return false;
				}
				if (hordeLayer != LAYER_GROUND)
				{
					return false;
				}
			}
			if (!validMovementPosition(mv, c))
			{
				return false;
			}
			// RW: the terrain height at the cell's centre (cell * 10 + 5), on the cell's layer; the footprint is no more than 10 from its first cell
			const float h = m_terrain ? m_terrain->getGroundHeight(SimMath::addf32((float)(i * 10), 5.0f), SimMath::addf32((float)(j * 10), 5.0f)) : 0.0f;
			if (first)
			{
				first = false;
				h0 = h;
			}
			else if (SimMath::absD(SimMath::subD((double)h, (double)h0)) > 10.0)
			{
				return false;
			}
		}
	}
	// RW 0x76550F: the line to the horde's cell is tested on the ground layer and the layers from 0x10 (the wall layer of the port is 15: only the ground runs here)
	if (hordeLayer == LAYER_GROUND && (cell.x != hordeCell.x || cell.y != hordeCell.y))
	{
		if (memberLineBlocked(*this, cell, hordeCell, hordeLayer, hordeCell))
		{
			return false;
		}
	}
	return true;
}

bool Pathfinder::adjustHordeMemberDestination(const PathfindObject &member, const PathfindLocomotorInfo &loco, const PathfindObject &horde, Coord3D *dest)
{
	// RW 0x6F0889 (lane MOVE-2, read with Ghidra). INFERENCE: RW clears pathfinder + 0x48 first (taken as the ignored obstacle id)
	m_ignoreObstacleID = PATHFIND_INVALID_ID;
	// RW 0x6EF346: the horde's cell (RW 0x6ECFDE, the horde's own centre rule); no cell -> false, `dest` unchanged
	const ICoord2D hordeCell = cellOfPosition(horde, horde.getPosition());
	const PathfindLayerEnum hordeLayer = horde.getLayer();
	if (!getCell(hordeLayer, hordeCell.x, hordeCell.y))
	{
		return false;
	}
	const PathfindMovement mv = makeMovement(&member, loco); // RW 0x6F08B2 .. 0x6F0902: the movement struct of RW 0x6EA04D's layout
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(&member, radius, center); // RW 0x6ED071
	const PathfindLayerEnum memberLayer = member.getLayer();
	ICoord2D cell;
	worldToCell(dest, center, &cell); // RW 0x6E8CE6
	if (memberFootprintFits(mv, memberLayer, hordeLayer, hordeCell, radius, center, cell))
	{
		return true;
	}
	ICoord2D last{ -1, -1 };
	const Coord3D &hp = horde.getPosition();
	for (int i = 1; i < 16; ++i)
	{
		// RW 0x6F097E .. 0x6F09CD: t = i * 1/16 (SSE); x = horde.x * t + dest.x * (1 - t), y = dest.y * (1 - t) + horde.y * t
		const float t = SimMath::mulf32((float)i, 0.0625f);
		const float s = SimMath::subf32(1.0f, t);
		const float x = SimMath::addf32(SimMath::mulf32(hp.x, t), SimMath::mulf32(dest->x, s));
		const float y = SimMath::addf32(SimMath::mulf32(s, dest->y), SimMath::mulf32(t, hp.y));
		const Coord3D p{ x, y, 0.0f };
		ICoord2D c;
		worldToCell(&p, center, &c); // RW floors x * 0.1 (+ 0.5 off centre) under the 24 bit FPU precision: the float32 product
		if (c.x == last.x && c.y == last.y)
		{
			continue;
		}
		last = c;
		if (memberFootprintFits(mv, memberLayer, hordeLayer, hordeCell, radius, center, c))
		{
			dest->x = x;
			dest->y = y;
			dest->z = m_terrain ? m_terrain->getGroundHeight(x, y) : dest->z; // RW 0x680A75 (the destination's layer: the ground) then the layer height
			return true;
		}
	}
	adjustCoordToCell(hordeCell.x, hordeCell.y, center, *dest, hordeLayer); // RW 0x6EA0CB -> 0x6E8E19, the member's centre rule on the horde's cell
	return false;
}

void Pathfinder::snapPosition(PathfindObject &obj, Coord3D *pos)
{
	// RW 0x6F3F95 (VERIFIED the same as ZH by the earlier analysis)
	int iRadius;
	bool center;
	getRadiusAndCenter(&obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	worldToCell(&adjustDest, center, &cell);
	adjustCoordToCell(cell.x, cell.y, center, *pos, LAYER_GROUND);
}

void Pathfinder::snapClosestGoalPosition(PathfindObject &obj, Coord3D *pos)
{
	// RW 0x6F3FCD..0x6F43A2 (the CritterDesync strings): a ZH snapClosestGoalPosition
	int iRadius;
	bool center;
	getRadiusAndCenter(&obj, iRadius, center);
	ICoord2D cell;
	PathfindLayerEnum layer = LAYER_GROUND;
	worldToCell(pos, center, &cell);
	adjustCoordToCell(cell.x, cell.y, center, *pos, LAYER_GROUND);
	if (checkDestination(&obj, cell.x, cell.y, layer, iRadius, center))
	{
		return;
	}
	for (int i = cell.x - 1; i < cell.x + 2; i++)
	{
		for (int j = cell.y - 1; j < cell.y + 2; j++)
		{
			if (checkDestination(&obj, i, j, layer, iRadius, center))
			{
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}
	if (iRadius == 0)
	{
		for (int i = cell.x - 1; i < cell.x + 2; i++)
		{
			for (int j = cell.y - 1; j < cell.y + 2; j++)
			{
				PathfindCell *newCell = getCell(layer, i, j);
				if (newCell)
				{
					bool goalFree = true;
					for (const PathfindOccupant *o = newCell->occupants(OCC_GROUND_GOAL); o; o = o->next)
					{
						if (o->owner != obj.getID()) goalFree = false;
					}
					if (goalFree)
					{
						adjustCoordToCell(i, j, center, *pos, layer);
						return;
					}
				}
			}
		}
		for (int i = cell.x - 1; i < cell.x + 2; i++)
		{
			for (int j = cell.y - 1; j < cell.y + 2; j++)
			{
				PathfindCell *newCell = getCell(layer, i, j);
				if (newCell && !newCell->hasOccupants(OCC_POSITION))
				{
					adjustCoordToCell(i, j, center, *pos, layer);
					return;
				}
			}
		}
	}
}

float Pathfinder::goalAngle(PathfindObjectID id) const
{
	// RW 0x68B425 -> 0x8E1C0D -> 0x8E1AEC (lane MOVE-3): the goal slot's angle code times pi / 6 (RW 0xBDE914, fild / fmul), 0 (RW 0xC1B594) for code 0 or no goal
	auto it = m_units.find(id);
	if (it == m_units.end() || it->second.goal.angleCode == 0)
	{
		return 0.0f;
	}
	return SimMath::mulf32((float)it->second.goal.angleCode, 0.5235988f);
}

PathfindLayerEnum Pathfinder::goalLayer(PathfindObjectID id) const
{
	// RW 0x68B43B: the slot record's layer (+ 0x28); ground (1) for an object without a pathfind record
	auto it = m_units.find(id);
	return it == m_units.end() ? LAYER_GROUND : it->second.goal.layer;
}

bool Pathfinder::goalPosition(PathfindObject &obj, Coord3D *pos)
{
	int iRadius;
	bool center;
	getRadiusAndCenter(&obj, iRadius, center);
	const ICoord2D cell = pathfindGoalCell(obj.getID());
	*pos = Coord3D();
	if (cell.x < 0 || cell.y < 0) return false;
	adjustCoordToCell(cell.x, cell.y, center, *pos, LAYER_GROUND);
	return true;
}

bool Pathfinder::checkForAdjust(PathfindObject &obj, const PathfindLocomotorInfo &loco, bool isHuman, int cellX, int cellY, PathfindLayerEnum layer,
	Coord3D *dest, bool &candidate)
{
	// TARGET RW 0x6F66D9 (steps 1-8 of the analysis); the group-height and required-layer steps do not apply on the ground layer only
	candidate = false;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP == nullptr) return false;
	if (cellP->getType() == PathfindCell::CELL_CLIFF) return false; // no final destinations on cliffs
	int iRadius;
	bool center;
	getRadiusAndCenter(&obj, iRadius, center);
	if (isHuman)
	{
		// the footprint must lie inside the logical extent (the computer may move off it)
		const int n = iRadius + (center ? 1 : 0);
		if (cellX - iRadius < m_logicalExtent.lo.x || cellY - iRadius < m_logicalExtent.lo.y || cellX + n > m_logicalExtent.hi.x || cellY + n >= m_logicalExtent.hi.y + 1)
		{
			return false;
		}
	}
	if (!checkDestination(&obj, cellX, cellY, layer, iRadius, center))
	{
		return false;
	}
	Coord3D adjustDest;
	adjustCoordToCell(cellX, cellY, center, adjustDest, cellP->getLayer());
	bool pathExists, adjustedPathExists;
	if (obj.isKindOf(PK_AIRCRAFT))
	{
		pathExists = true;
		adjustedPathExists = true;
	}
	else
	{
		pathExists = clientSafeQuickDoesPathExist(loco, &obj.getPosition(), dest);
		adjustedPathExists = clientSafeQuickDoesPathExist(loco, &obj.getPosition(), &adjustDest);
		if (!pathExists)
		{
			if (clientSafeQuickDoesPathExist(loco, dest, &adjustDest))
			{
				adjustedPathExists = true;
			}
		}
	}
	if (!adjustedPathExists)
	{
		return false;
	}
	*dest = adjustDest;
	return true;
}

bool Pathfinder::adjustDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest, const Coord3D *groupDest)
{
	// TARGET RW 0x6FE456: ring search 0x6FA2A4 over checkForAdjust. The ring visits the centre, then layers of 4, 12, 20 ... cells; a layer
	// is scanned completely once a valid cell was found in it and the one nearest the centre wins (strict <, so the first of equals);
	// `limit` is charged for the whole layer up front. ZH returned the first valid cell.
	if (obj.isKindOf(PK_PROJECTILE))
	{
		return true; // missiles can go wherever they want to
	}
	if (groupDest)
	{
		note(pathstops::kGroupDestination);
	}
	const bool isHuman = !obj.isComputerControlled();
	int iRadius;
	bool center;
	getRadiusAndCenter(&obj, iRadius, center);
	ICoord2D cell;
	worldToCell(dest, center, &cell);
	const PathfindLayerEnum layer = LAYER_GROUND;
	bool candidate = false;
	Coord3D found = *dest;
	if (checkForAdjust(obj, loco, isHuman, cell.x, cell.y, layer, &found, candidate))
	{
		*dest = found;
		return true;
	}
	int limit = m_config.adjustDestinationLimit;
	int d = 1;
	int ring = 4;
	while (limit > 0)
	{
		limit -= ring + 2;
		int best = 0;
		ICoord2D bestCell;
		Coord3D bestDest = *dest;
		int x = 0, y = 0;
		auto visit = [&]() {
			const int d2 = x * x + y * y;
			if (best == 0 || d2 < best)
			{
				Coord3D trial = *dest;
				bool cand = false;
				if (checkForAdjust(obj, loco, isHuman, cell.x + x, cell.y + y, layer, &trial, cand))
				{
					best = d2;
					bestCell.x = cell.x + x;
					bestCell.y = cell.y + y;
					bestDest = trial;
				}
			}
		};
		for (int k = 0; k < d; k++) { x++; visit(); }
		for (int k = 0; k < d; k++) { y++; visit(); }
		for (int k = 0; k < d + 1; k++) { x--; visit(); }
		for (int k = 0; k < d + 1; k++) { y--; visit(); }
		if (best != 0)
		{
			*dest = bestDest;
			return true;
		}
		d += 2;
		ring += 8;
	}
	return false;
}

bool Pathfinder::checkForPossible(const PathfindMovement &mv, int fromZone, bool center, const PathfindLocomotorInfo &loco, int cellX, int cellY, PathfindLayerEnum layer,
	Coord3D *dest, bool startingInObstacle)
{
	(void)loco;
	PathfindCell *goalCell = getCell(layer, cellX, cellY);
	if (!goalCell) return false;
	const PathfindCell::CellType t = goalCell->getType();
	if (t == PathfindCell::CELL_IMPASSABLE || t == PathfindCell::CELL_OBSTACLE || t == PathfindCell::CELL_BRIDGE_IMPASSABLE) return false;
	int zone2 = m_zoneManager.getEffectiveZone(mv, false, goalCell->getZone());
	(void)startingInObstacle;
	if (fromZone == zone2)
	{
		adjustCoordToCell(cellX, cellY, center, *dest, layer);
		return true;
	}
	return false;
}

bool Pathfinder::adjustToPossibleDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest)
{
	// RW 0x6F3C87 (ZH adjustToPossibleDestination with the RW ring order)
	int radius;
	bool center;
	getRadiusAndCenter(&obj, radius, center);
	ICoord2D goalCellNdx;
	if (worldToCell(dest, center, &goalCellNdx))
	{
		return false; // outside of bounds
	}
	const PathfindLayerEnum destinationLayer = LAYER_GROUND;
	PathfindCell *goalCell = getCell(destinationLayer, goalCellNdx.x, goalCellNdx.y);
	Coord3D from = obj.getPosition();
	PathfindCell *parentCell = getClippedCell(obj.getLayer(), &from);
	if (parentCell == nullptr || goalCell == nullptr)
	{
		return false;
	}
	const PathfindMovement mv = makeMovement(&obj, loco);
	int zone1 = m_zoneManager.getEffectiveZone(mv, false, parentCell->getZone());
	int zone2 = m_zoneManager.getEffectiveZone(mv, false, goalCell->getZone());
	if (zone1 == zone2)
	{
		if (checkDestination(&obj, goalCellNdx.x, goalCellNdx.y, destinationLayer, radius, center))
		{
			return true;
		}
	}
	int limit = m_config.adjustToPossibleLimit;
	int d = 1;
	int ring = 4;
	while (limit > 0)
	{
		limit -= ring + 2;
		int x = 0, y = 0;
		bool done = false;
		auto visit = [&]() {
			if (!done && checkForPossible(mv, zone1, center, loco, goalCellNdx.x + x, goalCellNdx.y + y, destinationLayer, dest, false))
			{
				if (checkDestination(&obj, goalCellNdx.x + x, goalCellNdx.y + y, destinationLayer, radius, center))
				{
					done = true;
				}
			}
		};
		for (int k = 0; k < d && !done; k++) { x++; visit(); }
		for (int k = 0; k < d && !done; k++) { y++; visit(); }
		for (int k = 0; k < d + 1 && !done; k++) { x--; visit(); }
		for (int k = 0; k < d + 1 && !done; k++) { y--; visit(); }
		if (done)
		{
			return true;
		}
		d += 2;
		ring += 8;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------
// queue
// ---------------------------------------------------------------------------------------------------------
bool Pathfinder::queueForPath(PathfindObjectID id)
{
	// TARGET RW 0x6ED0FB: a linear duplicate scan (a duplicate returns true), a full ring returns false
	if (!m_queue1.push(id))
	{
		note(pathstops::kQueueFull);
		return false;
	}
	return true;
}

bool Pathfinder::queueBlockedRepath(PathfindObjectID id)
{
	// TARGET RW 0x6ED12F: the same ring routine on the second ring
	if (!m_queue2.push(id))
	{
		note(pathstops::kQueueFull);
		return false;
	}
	return true;
}

void Pathfinder::setLogicalExtentFromTerrain()
{
	if (!m_terrain) return;
	float lx, ly, hx, hy;
	m_terrain->getExtent(lx, ly, hx, hy);
	m_logicalExtent.lo.x = realToIntFloor(SimMath::mulf32(lx, 0.1f));
	m_logicalExtent.hi.x = realToIntFloor(SimMath::mulf32(hx, 0.1f));
	m_logicalExtent.lo.y = realToIntFloor(SimMath::mulf32(ly, 0.1f));
	m_logicalExtent.hi.y = realToIntFloor(SimMath::mulf32(hy, 0.1f));
	m_logicalExtent.hi.x--;
	m_logicalExtent.hi.y--;
}

void Pathfinder::processPathfindQueue(PathfindRequestHandler &handler)
{
	note(pathstops::kQueueTime);
	// TARGET RW 0x6F2364: the zone update (every call), the extent, then ONE counter of cells allocated, reset once. Budget = GameData
	// MaxPathfindCellsPerFrame (x100 while the logic frame is below 5 * LOGICFRAMES = 25). Pass 1 drains the blocked-repath ring (AI vtable
	// +0x234) while the counter is below half the budget; pass 2 drains the request ring (AI vtable +0x230) while it is below the budget.
	// An id is taken off its ring before the call; an id whose object is gone is dropped; what the budget does not reach stays queued.
	if (!m_isMapReady)
	{
		return;
	}
	m_zoneManager.updateDirty(gridView(), PathfindRegion{ m_extent.lo, m_extent.hi });
	setLogicalExtentFromTerrain();
	m_cumulativeCellsAllocated = 0;
	int budget = m_config.cellsPerFrame;
	if (m_world && m_world->getFrame() < 25u)
	{
		budget *= 100;
	}
	const int half = budget / 2;
	while (half > 0 && !m_queue2.empty() && m_cumulativeCellsAllocated < half)
	{
		const PathfindObjectID id = m_queue2.pop();
		if (m_world && m_world->findObjectByID(id))
		{
			handler.doBlockedRepath(id);
		}
	}
	while (m_cumulativeCellsAllocated < budget && !m_queue1.empty())
	{
		const PathfindObjectID id = m_queue1.pop();
		if (m_world && m_world->findObjectByID(id))
		{
			handler.doPathfind(id);
		}
	}
}
