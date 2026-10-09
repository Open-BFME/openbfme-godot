// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane PHYS-1: Pathfinder::getMoveAwayFromPath, the search an allied unit runs when another unit asks it to step out of its path (ZH AIPathfind.cpp
// getMoveAwayFromPath; RotWK body RW 0x6FB231, called by the move-away handler RW 0x66DA5F).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * a computer player's unit is not "human" (controlling player + 0x5C == 1); the start cell is the unit's cell (RW 0x6ECFDE), the parent cell the clipped cell of
//     its position (RW 0x6EA076); a start cell that is not a valid movement cell, or a start footprint checkForMovement refuses (flags 1 for a HERO, else 0x10),
//     makes the search tunnel;
//   * the box half width: radius * 10 - 2.5 (RW 0xBE5AE8), + 5 for a centred footprint, + footprint size of `other` (RW 0x6EAF79) * 10 * 0.5;
//   * each popped cell: its box around the cell centre overlaps when it strictly contains the unit's own position, when a segment of `pathToAvoid` crosses it
//     (RW 0x67C81E), when it contains `other`'s position (lo < p <= hi, p != hi), or a segment of `pathToAvoid2` crosses it; a cell with no overlap that is not the start
//     cell and whose footprint is free (RW 0x6F3082) ends the search with a path built by RW 0x6F1A31;
//   * otherwise the cell is closed (RW 0x6F6286) and expanded (RW 0x6F9850, no goal cell).
// DONOR / INFERENCE (stop S-785): RW 0x67C81E is taken to be ZH's LineInRegion (TerrainLogic.cpp, Cohen-Sutherland clip of a segment against the closed box); the zone
// manager's setAllPassable (RW 0x937C65) has no counterpart here (the expansion ignores zones); RW 0x6F3082 is checkDestination with no other unit's goal in it.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

namespace
{
struct Box
{
	float loX, loY, hiX, hiY;
};

// ZH LineInRegion (donor for RW 0x67C81E)
bool lineInRegion(float x1, float y1, float x2, float y2, const Box &b)
{
	enum
	{
		LEFT = 1,
		RIGHT = 2,
		BOTTOM = 4,
		TOP = 8
	};
	auto code = [&](float x, float y) {
		int c = 0;
		if (x < b.loX)
			c = LEFT;
		else if (x > b.hiX)
			c = RIGHT;
		if (y < b.loY)
			c |= TOP;
		else if (y > b.hiY)
			c |= BOTTOM;
		return c;
	};
	const int c1 = code(x1, y1), c2 = code(x2, y2);
	if ((c1 | c2) == 0)
	{
		return true;
	}
	if (c1 & c2)
	{
		return false;
	}
	float diff;
	if (c1)
	{
		if (c1 & TOP)
		{
			if ((diff = SimMath::subf32(y2, y1)) == 0.0f) return false;
			x1 = SimMath::addf32(x1, SimMath::divf32(SimMath::mulf32(SimMath::subf32(x2, x1), SimMath::subf32(b.loY, y1)), diff));
			y1 = b.loY;
		}
		else if (c1 & BOTTOM)
		{
			if ((diff = SimMath::subf32(y2, y1)) == 0.0f) return false;
			x1 = SimMath::addf32(x1, SimMath::divf32(SimMath::mulf32(SimMath::subf32(x2, x1), SimMath::subf32(b.hiY, y1)), diff));
			y1 = b.hiY;
		}
		if (x1 > b.hiX)
		{
			if ((diff = SimMath::subf32(x2, x1)) == 0.0f) return false;
			y1 = SimMath::addf32(y1, SimMath::divf32(SimMath::mulf32(SimMath::subf32(y2, y1), SimMath::subf32(b.hiX, x1)), diff));
			x1 = b.hiX;
		}
		else if (x1 < b.loX)
		{
			if ((diff = SimMath::subf32(x2, x1)) == 0.0f) return false;
			y1 = SimMath::addf32(y1, SimMath::divf32(SimMath::mulf32(SimMath::subf32(y2, y1), SimMath::subf32(b.loX, x1)), diff));
			x1 = b.loX;
		}
	}
	if (c2)
	{
		if (c2 & TOP)
		{
			if ((diff = SimMath::subf32(y2, y1)) == 0.0f) return false;
			x2 = SimMath::addf32(x2, SimMath::divf32(SimMath::mulf32(SimMath::subf32(x2, x1), SimMath::subf32(b.loY, y2)), diff));
			y2 = b.loY;
		}
		else if (c2 & BOTTOM)
		{
			if ((diff = SimMath::subf32(y2, y1)) == 0.0f) return false;
			x2 = SimMath::addf32(x2, SimMath::divf32(SimMath::mulf32(SimMath::subf32(x2, x1), SimMath::subf32(b.hiY, y2)), diff));
			y2 = b.hiY;
		}
		if (x2 > b.hiX)
		{
			if ((diff = SimMath::subf32(x2, x1)) == 0.0f) return false;
			y2 = SimMath::addf32(y2, SimMath::divf32(SimMath::mulf32(SimMath::subf32(y2, y1), SimMath::subf32(b.hiX, x2)), diff));
			x2 = b.hiX;
		}
		else if (x2 < b.loX)
		{
			if ((diff = SimMath::subf32(x2, x1)) == 0.0f) return false;
			y2 = SimMath::addf32(y2, SimMath::divf32(SimMath::mulf32(SimMath::subf32(y2, y1), SimMath::subf32(b.loX, x2)), diff));
			x2 = b.loX;
		}
	}
	return x1 >= b.loX && x1 <= b.hiX && y1 >= b.loY && y1 <= b.hiY && x2 >= b.loX && x2 <= b.hiX && y2 >= b.loY && y2 <= b.hiY;
}

// a segment of the path crosses the box (the optimized nodes, RW: node + 8 is the next optimized node)
bool pathCrosses(const Path *path, const Box &b)
{
	if (path == nullptr)
	{
		return false;
	}
	for (const PathNode *n = path->getFirstNode(); n && n->getNextOptimized(); n = n->getNextOptimized())
	{
		const Coord3D *p = n->getPosition();
		const Coord3D *q = n->getNextOptimized()->getPosition();
		if (lineInRegion(p->x, p->y, q->x, q->y, b))
		{
			return true;
		}
	}
	return false;
}
} // namespace

Path *Pathfinder::getMoveAwayFromPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const PathfindObject *other, const Path *pathToAvoid,
	const PathfindObject *other2, const Path *pathToAvoid2)
{
	(void)other2;
	if (!m_isMapReady || obj == nullptr || !obj->hasAI())
	{
		return nullptr;
	}
	const bool isHuman = !obj->isComputerControlled();
	const Coord3D pos = obj->getPosition();
	const ICoord2D startNdx = cellOfPosition(*obj, pos);
	Coord3D clipped = pos;
	PathfindCell *parentCell = getClippedCell(obj->getLayer(), &clipped);
	if (parentCell == nullptr)
	{
		return nullptr;
	}
	const PathfindMovement mv = makeMovement(obj, loco);
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(obj, radius, center);
	m_isTunneling = !validMovementPosition(mv, parentCell);
	{
		PathfindCheckMovement info;
		info.cell = startNdx;
		info.layer = obj->getLayer();
		info.radius = radius;
		info.center = center;
		info.flags = obj->isKindOf(PK_HERO) ? 0x01u : 0x10u;
		info.ignoreId = obj->getIgnoredObstacleID();
		info.mv = mv;
		if (!checkForMovement(obj, info, nullptr))
		{
			m_isTunneling = true;
		}
	}
	if (!parentCell->allocateInfo(m_pool, startNdx))
	{
		return nullptr;
	}
	parentCell->startPathfind(nullptr);
	m_open.clear();
	m_closedList = nullptr;
	m_open.push(parentCell);
	m_shortcutCounter = 0;
	float half = SimMath::subf32(SimMath::mulf32(SimMath::sseFromInt32(radius * 10), 1.0f), 2.5f);
	if (center)
	{
		half = SimMath::addf32(half, 5.0f);
	}
	if (other)
	{
		half = SimMath::addf32(SimMath::mulf32(SimMath::sseFromInt32(footprintSize(other) * 10), 0.5f), half);
	}
	const Coord3D otherPos = other ? other->getPosition() : Coord3D{ 0.0f, 0.0f, 0.0f };
	Path *result = nullptr;
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		markClosed(c); // at once (review r3): a successful search gives its records back too
		Coord3D cc;
		adjustCoordToCell(c->getXIndex(), c->getYIndex(), center, cc, c->getLayer());
		const Box b{ SimMath::subf32(cc.x, half), SimMath::subf32(cc.y, half), SimMath::addf32(cc.x, half), SimMath::addf32(cc.y, half) };
		bool overlap = b.loX < pos.x && pos.x < b.hiX && b.loY < pos.y && pos.y < b.hiY;
		if (!overlap && pathCrosses(pathToAvoid, b))
		{
			overlap = true;
		}
		if (other && b.loX < otherPos.x && otherPos.x <= b.hiX && otherPos.x != b.hiX && b.loY < otherPos.y && otherPos.y <= b.hiY && otherPos.y != b.hiY)
		{
			overlap = true;
		}
		if (!overlap && !pathCrosses(pathToAvoid2, b))
		{
			int crowd = 0;
			if ((startNdx.x != (int)c->getXIndex() || startNdx.y != (int)c->getYIndex()) &&
				checkDestination(obj, c->getXIndex(), c->getYIndex(), c->getLayer(), radius, center, &crowd, false) && crowd == 0)
			{
				m_isTunneling = false;
				result = buildActualPath(obj, loco.validSurfaces, &pos, c, center, false);
				break;
			}
		}
		examineNeighboringCells(c, nullptr, loco, isHuman, center, radius, startNdx, obj);
	}
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	parentCell->releaseInfo(m_pool);
	return result;
}
