// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Structure footprints on the pathfind grid: which objects become obstacles and which cells they cover. Port of RW
// Pathfinder::classifyObjectFootprint (internal_classifyObjectFootprint) 0x936B7D and its fence routine 0x934E34, with ZH
// AIPathfind.cpp:4007-4505 as the structural donor. See docs/STOPS.md S-164.
//
// TARGET FACTS (RW game.dat, S-001 caveat):
//   * skipped objects, in order: KindOf BRIDGE_TOWER, PROJECTILE, DO_NOT_CLASSIFY; a fence (template FenceWidth > 0, not
//     WALK_ON_TOP_OF_WALL and not DEFENSIVE_WALL) goes to the fence routine; then everything that is not a STRUCTURE, a mobile object
//     (KindOf IMMOBILE clear), an object whose geometry IsSmall, an object in construction (status index 88), an object higher than
//     10 above the ground unless it is a wall piece (the height test is INFERRED).
//   * `wall` = BLOCKING_GATE or the walk-on-top-of-wall flag; `s22` = SCALEABLE_WALL on insertion.
//   * BOX (ZH's rasteriser): step 5 along the rotated axes, ceil(0.4 * radius) steps, cell = floor((v + 0.5) * 0.1), the cell must be
//     inside 0 <= c < extent.hi; only a CLEAR (0) or RUBBLE (3) cell takes an obstacle on insertion ({3, 4} on removal); a wall piece
//     first turns a CLIFF or RUBBLE cell into CLEAR; a wall also writes bit 17 (gate area) and every footprint writes bit 22 (s22).
//   * SPHERE / CYLINDER: ZH's rasteriser (size = r * 0.1 + 0.4, top-left floor(0.5 + (p - r) * 0.1) - 1).
//   * afterwards, in the box of the object grown by one cell: removal turns type 5 back to CLEAR; pass A clears bit 16 and marks a
//     CLEAR cell pinched when it has fewer than 2 clear cross neighbours or fewer than 4 clear neighbours; pass B turns pinched CLEAR
//     cells into type 5 (RW's IMPASSABLE role, the cell the pathfinder never enters); pass C marks CLEAR cells with an OBSTACLE cross
//     neighbour pinched.
//   * (lane PATH-2) every shape of the object's geometry list is rasterised (RW 0x936E25: inactive shapes and shapes with a z offset above 10
//     are skipped, a shape sits at the object's position moved by its turned offset, RW 0xAD14F0); the post region is the box of all active
//     shapes (RW 0xAD1D60) grown to whole cells and by one (RW 0x937469).
// Not modelled (S-164): the object's pathfind module calls (RW obj+0x84,
// obj+0xA4), the wall layer, the BASE_SITE bit-17 window, status 77 and the WALL_UPGRADE query.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cmath>

namespace
{
inline int realToIntFloor(float f)
{
	return SimMath::floorToInt(f);
}
inline int realToIntCeil(float f)
{
	return SimMath::ceilToInt(f);
}
} // namespace

void Pathfinder::classifyFence(PathfindObject &obj, bool insert)
{
	// TARGET RW 0x934E34 (the cell-bounds initialisation was not read: the object's own cell)
	const Coord3D *pos = &obj.getPosition();
	float angle = obj.getOrientation();
	float halfsizeX = obj.getFenceWidth() / 2;
	float halfsizeY = 10.0f / 10.0f;
	float fenceOffset = obj.getFenceXOffset();
	float c = SimMath::cosDet(angle);
	float s = SimMath::sinDet(angle);
	const float STEP_SIZE = 10.0f * 0.5f;
	float ydx = s * STEP_SIZE;
	float ydy = -c * STEP_SIZE;
	float xdx = c * STEP_SIZE;
	float xdy = s * STEP_SIZE;
	int numStepsX = realToIntCeil(2.0f * halfsizeX / STEP_SIZE);
	int numStepsY = realToIntCeil(2.0f * halfsizeY / STEP_SIZE);
	float tl_x = pos->x - fenceOffset * c - halfsizeY * s;
	float tl_y = pos->y + halfsizeY * c - fenceOffset * s;
	PathfindRegion cellBounds;
	cellBounds.lo.x = realToIntFloor((pos->x + 0.5f) * 0.1f);
	cellBounds.lo.y = realToIntFloor((pos->y + 0.5f) * 0.1f);
	cellBounds.hi = cellBounds.lo;
	for (int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
	{
		float x = tl_x;
		float y = tl_y;
		for (int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
		{
			int cx = realToIntFloor((x + 0.5f) * 0.1f);
			int cy = realToIntFloor((y + 0.5f) * 0.1f);
			if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
			{
				PathfindCell &cellRef = *getCell(LAYER_GROUND, cx, cy);
				bool changed = false;
				if (insert)
				{
					ICoord2D p;
					p.x = cx;
					p.y = cy;
					changed = cellRef.setTypeAsObstacle(m_pool, obj, true, p);
				}
				else
				{
					changed = cellRef.removeObstacle(m_pool, obj);
				}
				if (changed)
				{
					cellRef.setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					if (!m_buildingMap) m_zoneManager.markCellDirty(cx, cy);
				}
				if (cellBounds.lo.x > cx) cellBounds.lo.x = cx;
				if (cellBounds.lo.y > cy) cellBounds.lo.y = cy;
				if (cellBounds.hi.x < cx) cellBounds.hi.x = cx;
				if (cellBounds.hi.y < cy) cellBounds.hi.y = cy;
			}
		}
	}
}

void Pathfinder::classifyObjectFootprint(PathfindObject &obj, bool insert)
{
	if (obj.isKindOf(PK_BRIDGE_TOWER) || obj.isKindOf(PK_PROJECTILE) || obj.isKindOf(PK_DO_NOT_CLASSIFY))
	{
		return;
	}
	const bool walkOnWall = obj.isKindOf(PK_WALK_ON_TOP_OF_WALL);
	if (!walkOnWall && obj.getFenceWidth() > 0.0f && !obj.isKindOf(PK_DEFENSIVE_WALL))
	{
		classifyFence(obj, insert);
		return;
	}
	if (!insert)
	{
		if (obj.isKindOf(PK_BLAST_CRATER))
		{
			return; // these footprints are permanent (ZH)
		}
		removeUnitFromPathfindMap(obj);
	}
	if (!obj.isKindOf(PK_STRUCTURE))
	{
		return; // only path around structures
	}
	if (obj.isMobile())
	{
		return; // RW 0x690E97: mobile units aren't obstacles
	}
	if (obj.getGeometry().isSmall)
	{
		return; // small objects are not obstacles
	}
	if (obj.footprintSkippedByStatus())
	{
		return; // under construction
	}
	if (obj.getHeightAboveTerrain() > 10.0f && !walkOnWall)
	{
		return; // don't add bounds that are up in the air
	}
	internal_classifyObjectFootprint(obj, insert);
}

void Pathfinder::internal_classifyObjectFootprint(PathfindObject &obj, bool insert)
{
	const bool wall = obj.isKindOf(PK_BLOCKING_GATE) || obj.isKindOf(PK_WALK_ON_TOP_OF_WALL);
	const bool s22 = insert ? obj.isKindOf(PK_SCALEABLE_WALL) : false;
	if (wall || s22)
	{
		note(pathstops::kWallLayer);
	}
	const PathfindGeometry &geom = obj.getGeometry();
	const float angle = obj.getOrientation();
	auto applyCell = [&](int cx, int cy) {
		PathfindCell &cellRef = *getCell(LAYER_GROUND, cx, cy);
		const PathfindCell::CellType t = cellRef.getType();
		const bool proceed = insert ? (t == PathfindCell::CELL_CLEAR || t == PathfindCell::CELL_RUBBLE) : (t == PathfindCell::CELL_RUBBLE || t == PathfindCell::CELL_OBSTACLE);
		if (!proceed && !(insert && wall && (t == PathfindCell::CELL_CLIFF)))
		{
			return;
		}
		if (insert && wall && (t == PathfindCell::CELL_CLIFF || t == PathfindCell::CELL_RUBBLE))
		{
			cellRef.setType(PathfindCell::CELL_CLEAR); // a wall piece overwrites a cliff or rubble cell
		}
		bool changed;
		if (insert)
		{
			ICoord2D p;
			p.x = cx;
			p.y = cy;
			changed = cellRef.setTypeAsObstacle(m_pool, obj, false, p);
		}
		else
		{
			changed = cellRef.removeObstacle(m_pool, obj);
		}
		if (changed)
		{
			cellRef.setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
		}
		if (wall && cellRef.getBit17() != insert)
		{
			cellRef.setBit17(insert);
		}
		cellRef.setBit22(s22);
		if (!m_buildingMap) m_zoneManager.markCellDirty(cx, cy);
	};
	// RW 0x936E25 .. 0x937463: every shape of the object's geometry in turn (lane PATH-2, S-341): an inactive shape (+0x20) and a shape whose z offset is
	// above 10 (+0x18, RW 0xBD83D8) are skipped; the others are rasterised at the object's position moved by the shape's turned offset (RW 0xAD14F0)
	for (size_t shapeIndex = 0; shapeIndex < geom.shapeCount(); ++shapeIndex)
	{
		const PathfindShape shape = geom.shape(shapeIndex);
		if (!shape.active || shape.offsetZ > 10.0f)
		{
			continue;
		}
		Coord3D shapePos = obj.getPosition();
		PathfindGeometry::applyShapeOffset(shape, angle, shapePos);
		const Coord3D *pos = &shapePos;
		switch (shape.type)
		{
		case PATHFIND_GEOMETRY_BOX:
		{
			// TARGET RW 0x936EE8: ceil(radius * 0.4f) steps of 5 along each turned axis (the minor axis outside)
			float halfsizeX = shape.majorRadius;
			float halfsizeY = shape.minorRadius;
			float c = SimMath::cosDet(angle);
			float s = SimMath::sinDet(angle);
			const float STEP_SIZE = 5.0f;
			float ydx = SimMath::mulf32(s, STEP_SIZE);
			float ydy = SimMath::subf32(0.0f, SimMath::mulf32(c, STEP_SIZE));
			float xdx = SimMath::mulf32(c, STEP_SIZE);
			float xdy = SimMath::mulf32(s, STEP_SIZE);
			int numStepsX = realToIntCeil(SimMath::pc24Mul(halfsizeX, 0.4f));
			int numStepsY = realToIntCeil(SimMath::pc24Mul(halfsizeY, 0.4f));
			float tl_x = SimMath::subf32(SimMath::subf32(pos->x, SimMath::mulf32(halfsizeX, c)), SimMath::mulf32(halfsizeY, s));
			float tl_y = SimMath::subf32(SimMath::addf32(SimMath::mulf32(halfsizeY, c), pos->y), SimMath::mulf32(halfsizeX, s));
			for (int iy = 0; iy < numStepsY; ++iy, tl_x = SimMath::addf32(tl_x, ydx), tl_y = SimMath::addf32(tl_y, ydy))
			{
				float x = tl_x;
				float y = tl_y;
				for (int ix = 0; ix < numStepsX; ++ix, x = SimMath::addf32(x, xdx), y = SimMath::addf32(y, xdy))
				{
					int cx = realToIntFloor(SimMath::pc24Mul(SimMath::pc24Add(x, 0.5f), 0.1f));
					int cy = realToIntFloor(SimMath::pc24Mul(SimMath::pc24Add(y, 0.5f), 0.1f));
					if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
					{
						applyCell(cx, cy);
					}
				}
			}
		}
		break;
		case PATHFIND_GEOMETRY_SPHERE:
		case PATHFIND_GEOMETRY_CYLINDER:
		{
			// TARGET RW 0x9371ED
			float radius = shape.majorRadius;
			ICoord2D topLeft, bottomRight;
			topLeft.x = realToIntFloor(SimMath::addf32(SimMath::mulf32(SimMath::subf32(pos->x, radius), 0.1f), 0.5f)) - 1;
			topLeft.y = realToIntFloor(SimMath::addf32(SimMath::mulf32(SimMath::subf32(pos->y, radius), 0.1f), 0.5f)) - 1;
			const float size = SimMath::addf32(SimMath::mulf32(radius, 0.1f), 0.4f);
			const float centerX = SimMath::mulf32(pos->x, 0.1f);
			const float centerY = SimMath::mulf32(pos->y, 0.1f);
			const float r2 = SimMath::mulf32(size, size);
			const float size2 = SimMath::mulf32(size, 2.0f);
			bottomRight.x = SimMath::cvttss2si(SimMath::addf32(SimMath::addf32(SimMath::sseFromInt32(topLeft.x), size2), 2.0f));
			bottomRight.y = SimMath::cvttss2si(SimMath::addf32(SimMath::addf32(SimMath::sseFromInt32(topLeft.y), size2), 2.0f));
			for (int j = topLeft.y; j < bottomRight.y; j++)
			{
				const float deltaY = SimMath::subf32(SimMath::addf32(SimMath::sseFromInt32(j), 0.5f), centerY);
				const float dy2 = SimMath::mulf32(deltaY, deltaY);
				for (int i = topLeft.x; i < bottomRight.x; i++)
				{
					const float deltaX = SimMath::subf32(SimMath::addf32(SimMath::sseFromInt32(i), 0.5f), centerX);
					if (!(r2 < SimMath::addf32(SimMath::mulf32(deltaX, deltaX), dy2)))
					{
						if (i >= 0 && j >= 0 && i < m_extent.hi.x && j < m_extent.hi.y)
						{
							applyCell(i, j);
						}
					}
				}
			}
		}
		break;
		}
	}
	// the post region (RW 0x937469): the box of every active shape (RW 0xAD1D60), floor(lo * 0.1) - 1 .. ceil(hi * 0.1) + 1, clamped to the extent
	float box[4];
	geom.boundingBox2D(obj.getPosition(), angle, box);
	PathfindRegion region;
	region.lo.x = std::max(realToIntFloor(SimMath::pc24Mul(box[0], 0.1f)) - 1, m_extent.lo.x);
	region.lo.y = std::max(realToIntFloor(SimMath::pc24Mul(box[1], 0.1f)) - 1, m_extent.lo.y);
	region.hi.x = std::min(realToIntCeil(SimMath::pc24Mul(box[2], 0.1f)) + 1, m_extent.hi.x);
	region.hi.y = std::min(realToIntCeil(SimMath::pc24Mul(box[3], 0.1f)) + 1, m_extent.hi.y);
	if (!insert)
	{
		// removal: type 5 cells go back to CLEAR
		for (int j = region.lo.y; j <= region.hi.y; j++)
		{
			for (int i = region.lo.x; i <= region.hi.x; i++)
			{
				PathfindCell *c = getCell(LAYER_GROUND, i, j);
				if (c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE)
				{
					c->setType(PathfindCell::CELL_CLEAR);
					if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
				}
			}
		}
	}
	// pass A: pinched cells (fewer than 2 clear cross neighbours or fewer than 4 clear neighbours)
	for (int j = region.lo.y; j <= region.hi.y; j++)
	{
		for (int i = region.lo.x; i <= region.hi.x; i++)
		{
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			c->setPinched(false);
			if (c->getType() == PathfindCell::CELL_CLEAR)
			{
				int totalCount = 0;
				int orthogonalCount = 0;
				for (int k = i - 1; k < i + 2; k++)
				{
					if (k < m_extent.lo.x || k > m_extent.hi.x) continue;
					for (int l = j - 1; l < j + 2; l++)
					{
						if (l < m_extent.lo.y || l > m_extent.hi.y) continue;
						if ((k == i) && (j == l)) continue;
						if (getCell(LAYER_GROUND, k, l)->getType() == PathfindCell::CELL_CLEAR)
						{
							totalCount++;
							if ((k == i) || (l == j))
							{
								orthogonalCount++;
							}
						}
					}
				}
				if (orthogonalCount < 2 || totalCount < 4)
				{
					c->setPinched(true);
				}
			}
		}
	}
	// pass B: pinched CLEAR cells become type 5
	for (int j = region.lo.y; j <= region.hi.y; j++)
	{
		for (int i = region.lo.x; i <= region.hi.x; i++)
		{
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			if (c->getPinched() && c->getType() == PathfindCell::CELL_CLEAR)
			{
				c->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
				c->setPinched(false);
				if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
			}
		}
	}
	// pass C: CLEAR cells with an OBSTACLE cross neighbour are pinched
	for (int j = region.lo.y; j <= region.hi.y; j++)
	{
		for (int i = region.lo.x; i <= region.hi.x; i++)
		{
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			if (c->getType() == PathfindCell::CELL_CLEAR)
			{
				bool objectAdjacent = false;
				for (int k = i - 1; k < i + 2 && !objectAdjacent; k++)
				{
					if (k < m_extent.lo.x || k > m_extent.hi.x) continue;
					for (int l = j - 1; l < j + 2; l++)
					{
						if (l < m_extent.lo.y || l > m_extent.hi.y) continue;
						if ((k == i) && (l == j)) continue;
						if ((k != i) && (l != j)) continue;
						if (getCell(LAYER_GROUND, k, l)->getType() == PathfindCell::CELL_OBSTACLE)
						{
							objectAdjacent = true;
							break;
						}
					}
				}
				if (objectAdjacent)
				{
					c->setPinched(true);
				}
			}
		}
	}
}
