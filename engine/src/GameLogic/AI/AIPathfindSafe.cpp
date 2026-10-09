// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane MODULES-3: Pathfinder::findSafePath, the search a panicking unit runs away along (ZH AIPathfind.cpp findSafePath; RotWK body RW 0x6FCE1A, pathfinder vtable
// 0xC1B8D8 slot +0x14, asked by AIUpdate::doPathfind's safe branch RW 0x668E94 for the run-away-panic state, RW 0x74E3E7).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the map must be ready (pathfinder + 8); the radius and centring of the unit's footprint (RW 0x6ED071); a computer player's unit (player + 0x5C == 1) is not
//     "human"; the zone manager is made all passable (RW 0x937C65); the start is the unit's OWN position (not `from`): its centred cell (RW 0x6E8CE6) and the clipped
//     cell of its layer (RW 0x6EA076); no start cell or no AI (object + 0x260): no path;
//   * the start cell starts the search with no goal cell (RW 0x934400(0)); every popped cell (RW 0x6F4AF2): its centre (RW 0x6EA0A1), the SSE squared 2D distance to
//     each repulsor (dy^2 + dx^2, the nearer of the two), "ok" when it is above radius^2 (mulss), or when the open list is empty (RW 0x6F1B2A) after any cell was
//     examined, or (with the distance a new farthest) when more than 2000 cells were examined; an ok cell whose footprint is free (RW 0x6F3082: checkDestination with
//     no other unit's goal) ends the search with the path RW 0x6F1A31 builds from the unit's position; else the cell is closed (RW 0x6F6286) and expanded (RW
//     0x6F9850, no goal cell), its count added to the examined cells.
// INFERENCE: the zone manager's setAllPassable is the port's PathfindZoneManager::setAllPassable (PATH-2); RW 0x6F3082 is checkDestination with a zero crowd count, as
// PHYS-1 reads it (AIPathfindMoveAway.cpp, S-785).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

Path *Pathfinder::findSafePath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D &repulsor1, const Coord3D &repulsor2, float repulsorRadius)
{
	if (!m_isMapReady || obj == nullptr)
	{
		return nullptr;
	}
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(obj, radius, center);
	const float radiusSqr = SimMath::mulf32(repulsorRadius, repulsorRadius); // RW 0x6FCE4B mulss
	const bool isHuman = !obj->isComputerControlled();
	m_zoneManager.setAllPassable(); // RW 0x937C65
	const Coord3D pos = obj->getPosition();
	ICoord2D startNdx;
	worldToCell(&pos, true, &startNdx); // RW 0x6E8CE6(.., 1, position)
	Coord3D clipped = pos;
	PathfindCell *parentCell = getClippedCell(obj->getLayer(), &clipped);
	if (parentCell == nullptr || !obj->hasAI())
	{
		return nullptr;
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
	Path *result = nullptr;
	int cellCount = 0;
	float farthest = 0.0f;
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		Coord3D cc;
		adjustCoordToCell(c->getXIndex(), c->getYIndex(), center, cc, c->getLayer());
		// RW 0x6FCF31 .. 0x6FCF83 (SSE): (y - r1.y)^2 + (x - r1.x)^2 and the same to r2, the smaller
		const float ay = SimMath::subf32(cc.y, repulsor1.y), ax = SimMath::subf32(cc.x, repulsor1.x);
		const float by = SimMath::subf32(cc.y, repulsor2.y), bx = SimMath::subf32(cc.x, repulsor2.x);
		float distSqr = SimMath::addf32(SimMath::mulf32(ay, ay), SimMath::mulf32(ax, ax));
		const float distSqr2 = SimMath::addf32(SimMath::mulf32(by, by), SimMath::mulf32(bx, bx));
		if (distSqr > distSqr2)
		{
			distSqr = distSqr2;
		}
		bool ok = distSqr > radiusSqr;
		if (m_open.empty() && cellCount > 0)
		{
			ok = true; // RW 0x6FCF92: the search space is exhausted, the last cell is taken
		}
		if (distSqr > farthest)
		{
			farthest = distSqr;
			if (cellCount > 2000)
			{
				ok = true; // RW 0x6FCFA4: a big search takes the farthest cell
			}
		}
		int crowd = 0;
		if (ok && checkDestination(obj, c->getXIndex(), c->getYIndex(), c->getLayer(), radius, center, &crowd, false) && crowd == 0)
		{
			markClosed(c);
			result = buildActualPath(obj, loco.validSurfaces, &pos, c, center, false);
			break;
		}
		markClosed(c);
		cellCount += examineNeighboringCells(c, nullptr, loco, isHuman, center, radius, startNdx, obj);
	}
	cleanOpenAndClosedLists();
	parentCell->releaseInfo(m_pool);
	return result;
}
