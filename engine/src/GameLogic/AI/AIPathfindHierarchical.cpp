// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane PATHPERF-1: the hierarchical search of findPath (closes most of S-162). Before its cell A*, RotWK's findPath (RW 0x6FE7CB) runs an A* over the
// 16 x 16 zone blocks, from the start cell to the goal's block, through the cells on the blocks' edges, and marks the blocks of the chain it finds passable
// (the corridor). The cell A* then rejects cells outside the corridor (examineNeighboringCells' block test, already ported) and adds the remaining corridor
// length to its estimate. Without the corridor a long or obstructed order searched up to MaxCellsFindPathLimit (15000) cells per unit and came back partial,
// so a 150 horde order cost over a second of logic time a frame.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0x6F9829 FindClosestHierarchicalPath (BFME2 decomp pathfinder.cpp, byte-matched, tier A): RW 0x6F8601(isHuman, the locomotor set's surfaces, obj,
//     from, to, crusher, closestOK = 1, adjustedTo); RW 0x6F8601 itself (4605 bytes, tier B same-shape, no decomp source) read from the disassembly:
//       - (to.x, to.y) == (0, 0) or the map not ready -> none; from / to clipped to the grid (RW 0x6E8A14); + 0x38 (tunnelling) = 0;
//       - the mineshaft branch: when the unit's player has a usable network (RW 0x77559F) the type 8 waypoints nearest the unit and the goal become an
//         intermediate goal (NOT PORTED, reported: kHierarchical);
//       - the goal cell (RW 0x6E8CE6 centred, RW 0x5E2E9C) and the start cell (RW 0x6EA076) get records (RW 0x6E9FF8); none -> none; start == goal -> none;
//         a start in an obstacle that is not the ignored one -> none;
//       - the goal record is reset (RW 0x934400); the start's total cost is RW 0x6F1F0D (start, goal); the goal's zone is the effective zone (RW 0x9389A6, the
//         non-terrain variant) on the ground and wall layers (RW 0x76550F), with the goal's block;
//       - the loop pops the open heap (RW 0x6F4AF2), closes the cell (RW 0x934594) and is done when the cell's effective zone and block are the goal's; else
//         the bridge links (RW 0x9389CE) and the block portals (RW 0x937F2D / 0x937F7F) of the block (the bridge links: not ported, S-161 / S-4856; the portals: lane WALL-2), the closest cell by
//         dy*dy + dx*dx (float, below 999999995904.0f, RW 0xC1C4AC), then the four edges of the block: x = bx*16 (left, step -1), bx*16 + 15 (right, +1), y = by*16
//         (bottom), by*16 + 15 (top), each edge's 16 cells from its middle outward (8, 9, 7, 10, 6, ... , 15, 1, 16 from the block's corner), a cell
//         that exists, is neither open nor closed and (a human's search) lies in the logical extent goes to RW 0x6F6454 with a zone list cleared per edge;
//       - found: the goal's parent is the found cell (RW 0x93443D), the corridor RW 0x6F49BC(from, goal, template + 0x11C bit 31 = KindOf SHIP), RW 0x6F5519
//         (the open and closed lists released, their counts added to the queue's cell counter + 0x3C), the goal's record released (RW 0x9347C6), adjustedTo = the
//         clipped goal; not found: closestOK and a closest cell that is not the start -> adjustedTo = its centre (RW 0x6EA0CB) and the corridor of its chain,
//         else none (the waypoint vector + 0x1C1CC cleared); RW 0x6F5519 and the goal's release either way;
//   * RW 0x6F6454 (tier A, no decomp source): the edge cell in the extent, its effective zone (RW 0x9389A6) the parent's, not open or closed; the cell across
//     the edge in the extent, not open or closed, its effective zone `z`; `z` already in this edge's list -> that cell is closed (RW 0x934594) and nothing more;
//     the two raw zones must join (RW 0x938240); the edge cell gets a record and is closed, the cell across gets a record; c1 = its RW 0x9345AA distance to
//     the parent, c2 to the goal; +20 to c1 when either cell is pinched (bit 16) or has bit 23, else `z` joins the list; its cost so far = the parent's + c1,
//     its total = cost so far + c2 (16 bit), its parent the parent (RW 0x93443D), pushed (RW 0x6F57A1);
//   * RW 0x6F49BC (decomp Pathfinder::MakeHierarchicalPathPassable, BFME2 0x6F35AF, byte-matched): along the parent chain from the cell, a portal hop is
//     recorded (none here), the cell's block (expand: the 3 x 3 blocks around it) marked passable (RW 0x937E2B), the block put at the FRONT of the path's list
//     (RW 0x93AE7D); RW 0x93B3D8 (changed from BFME2 0x934AAE) numbers the list from its head (the start) and leaves the last number at + 0x14;
//     RW 0x93AD93 (decomp HierarchicalPath::lookUpPositionOnPath) maps a cell's block to its number, -1 off the path.
// DONOR: the BFME2 decomp named above (structure and the byte-matched helpers); ZH has no hierarchical search.
// INFERENCE: a start cell whose record already existed (a reservation keeps it) gets its parent link cleared (retail leaves a stale one, which the chain walk
// would follow); a record the pool cannot give ends the search with no corridor (retail dereferences null).

#include "GameLogic/AI/AIPathfind.h"

#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"

#include <cstdlib>

namespace
{
const int kBlock = 16; // PathfindZoneManager block (RW 0x939226)
} // namespace

float Pathfinder::hierarchicalClosestKey(int dx, int dy)
{
	// TARGET RW 0x6F937E .. 0x6F93B9 (SSE): abs (MSVCR71 abs RW 0xA3D6B0) of goal - cell in x, cvtsi2ss; the same in y; mulss each by itself, then
	// addss (y*y + x*x). Above 2^24 the squares and the sum round to float: keys that differ as integers can tie (the first cell keeps the place)
	const float fx = SimMath::sseFromInt32(dx < 0 ? -dx : dx);
	const float fy = SimMath::sseFromInt32(dy < 0 ? -dy : dy);
	return SimMath::sseAdd(SimMath::sseMul(fy, fy), SimMath::sseMul(fx, fx));
}

int Pathfinder::hierarchicalPositionOf(int cellX, int cellY) const
{
	// TARGET RW 0x93AD93: the block (RW 0x93AD6E: x / 16, y / 16) looked up in the path's map, -1 when it is not there
	if (m_hierLast < 0 || cellX < 0 || cellY < 0)
	{
		return -1;
	}
	ICoord2D blocks;
	m_zoneManager.getExtent(blocks);
	const int bx = cellX / kBlock, by = cellY / kBlock;
	if (bx >= blocks.x || by >= blocks.y)
	{
		return -1;
	}
	return m_hierIndex[(size_t)bx * (size_t)blocks.y + (size_t)by];
}

void Pathfinder::makeHierarchicalPathPassable(PathfindCell *last, bool expand)
{
	// TARGET RW 0x6F49BC, RW 0x93AE7D, RW 0x93B3D8 (see the file comment)
	ICoord2D blocks;
	m_zoneManager.getExtent(blocks);
	const size_t n = (size_t)blocks.x * (size_t)blocks.y;
	if (m_hierIndex.size() != n)
	{
		m_hierIndex.assign(n, -1);
		m_hierBlocks.clear();
	}
	for (int b : m_hierBlocks)
	{
		m_hierIndex[(size_t)b] = -1;
	}
	m_hierBlocks.clear();
	// lane WALL-2, RW 0x6F49D5 .. 0x6F4A29: first every portal hop of the chain (a record with a waypoint, info + 0xC, and a parent) goes to the back of the waypoint
	// vector (+ 0x1C1CC) as {the waypoint's id, the parent's cell}: goal first
	for (PathfindCell *node = last; node && node->getParentCell(); node = node->getParentCell())
	{
		if (const PathfindWaypoint *w = waypointOfSlot(node->getViaPortal()))
		{
			PathfindPortalHop hop;
			hop.waypointId = w->id;
			hop.cell.x = node->getParentCell()->getXIndex();
			hop.cell.y = node->getParentCell()->getYIndex();
			m_portalHops.push_back(hop);
		}
	}
	// the chain from `last` back to the start (RW 0x6E810B: the record's parent); the walk visits it goal first, the list holds it start first
	int count = 0;
	for (PathfindCell *node = last; node; node = node->getParentCell())
	{
		const int x = node->getXIndex(), y = node->getYIndex();
		if (expand)
		{
			for (int dx = -kBlock; dx <= kBlock; dx += kBlock)
			{
				for (int dy = -kBlock; dy <= kBlock; dy += kBlock)
				{
					m_zoneManager.setPassable(x + dx, y + dy, true);
				}
			}
		}
		else
		{
			m_zoneManager.setPassable(x, y, true);
		}
		++count;
		if (count > m_pool.capacity())
		{
			break; // a chain longer than every record there is would be a cycle (cannot happen: parents are set once, when a cell is pushed)
		}
	}
	// the numbers from the head of the list (the start): the walk's i-th node is number count - 1 - i; a block met twice keeps the higher number
	int i = 0;
	for (PathfindCell *node = last; node && i < count; node = node->getParentCell(), ++i)
	{
		const int number = count - 1 - i;
		const int x = node->getXIndex(), y = node->getYIndex();
		if (x < 0 || y < 0)
		{
			continue;
		}
		const int bx = x / kBlock, by = y / kBlock;
		if (bx >= blocks.x || by >= blocks.y)
		{
			continue;
		}
		const size_t b = (size_t)bx * (size_t)blocks.y + (size_t)by;
		if (m_hierIndex[b] < 0)
		{
			m_hierBlocks.push_back((int)b);
			m_hierIndex[b] = number;
		}
		else if (number > m_hierIndex[b])
		{
			m_hierIndex[b] = number;
		}
	}
	m_hierLast = count - 1;
}

void Pathfinder::examineHierarchicalEdgeCell(const ICoord2D &cell, const ICoord2D &delta, PathfindCell *parent, PathfindCell *goal, zoneStorageType parentZone,
	zoneStorageType *zones, int &zoneCount, const PathfindMovement &mv)
{
	// TARGET RW 0x6F6454 (see the file comment)
	if (cell.x < m_extent.lo.x || cell.x > m_extent.hi.x || cell.y < m_extent.lo.y || cell.y > m_extent.hi.y)
	{
		return;
	}
	PathfindCell *edge = getCell(LAYER_GROUND, cell.x, cell.y);
	if (!edge || m_zoneManager.getEffectiveZone(mv, false, edge->getZone()) != parentZone)
	{
		return;
	}
	if (edge->hasInfo() && (edge->getOpen() || edge->getClosed()))
	{
		return;
	}
	const ICoord2D next{ cell.x + delta.x, cell.y + delta.y };
	if (next.x < m_extent.lo.x || next.x > m_extent.hi.x || next.y < m_extent.lo.y || next.y > m_extent.hi.y)
	{
		return;
	}
	PathfindCell *across = getCell(LAYER_GROUND, next.x, next.y);
	if (!across || (across->hasInfo() && (across->getOpen() || across->getClosed())))
	{
		return;
	}
	const zoneStorageType z = m_zoneManager.getEffectiveZone(mv, false, across->getZone());
	for (int i = 0; i < zoneCount; ++i)
	{
		if (zones[i] == z)
		{
			// this edge already leads into that zone: the cell across is closed, not pushed
			if (across->allocateInfo(m_pool, next))
			{
				markClosed(across);
			}
			return;
		}
	}
	if (!m_zoneManager.zonesJoin(mv, edge->getZone(), across->getZone()))
	{
		return;
	}
	if (!edge->allocateInfo(m_pool, cell))
	{
		return;
	}
	if (!edge->getClosed() && !edge->getOpen())
	{
		markClosed(edge);
	}
	if (!across->allocateInfo(m_pool, next))
	{
		return;
	}
	unsigned toParent = across->costToHierGoal(parent);
	const unsigned toGoal = across->costToHierGoal(goal);
	if (across->getPinched() || edge->getPinched() || across->getBit23() || edge->getBit23())
	{
		toParent += 20;
	}
	else
	{
		zones[zoneCount++] = z;
	}
	across->setCostSoFar((std::uint16_t)(parent->getCostSoFar() + toParent));
	across->setTotalCost((std::uint16_t)(across->getCostSoFar() + toGoal));
	across->setParentCell(parent);
	m_open.push(across);
}

bool Pathfinder::findClosestHierarchicalPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, bool isHuman, const Coord3D *from, const Coord3D *to,
	Coord3D *adjustedTo)
{
	// TARGET RW 0x6F9829 -> RW 0x6F8601 (see the file comment)
	m_hierLast = -1;
	if ((to->x == 0.0f && to->y == 0.0f) || !m_isMapReady)
	{
		return false;
	}
	Coord3D goalPos = *to;
	Coord3D fromPos = *from;
	clip(&fromPos, &goalPos);
	m_isTunneling = false;
	note(pathstops::kHierarchical); // the mineshaft branch (RW 0x77559F) and the bridge branch below are not ported (S-161, S-162); the portal branch is (WALL-2)
	ICoord2D goalNdx;
	if (worldToCell(&goalPos, true, &goalNdx))
	{
		return false; // RW 0x5E2E9C: no cell off the grid (the port's worldToCell clamps and reports it)
	}
	PathfindCell *goal = getCell(LAYER_GROUND, goalNdx.x, goalNdx.y);
	if (goal && goal->getType() == PathfindCell::CELL_OBSTACLE && m_ignoreObstacleID != PATHFIND_INVALID_ID && goal->getObstacleID() == m_ignoreObstacleID)
	{
		// INFERENCE (S-162, kHierarchical): a goal inside the obstacle the unit ignores (a builder walking onto the site it builds) is in an obstacle zone no
		// ground block joins, so the block search could only end at the closest block cell. The port's builders aim inside their site's footprint (the
		// cell search then reaches it through the ignored obstacle); no corridor here, every block passable, as before the corridor was ported
		return false;
	}
	if (!goal || !goal->allocateInfo(m_pool, goalNdx))
	{
		return false;
	}
	PathfindCell *start = getClippedCell(LAYER_GROUND, &fromPos); // the start's ground grid cell (the corridor's raised branch RW 0x9389CE is not ported: S-4856)
	if (!start)
	{
		return false;
	}
	if (start == goal)
	{
		goal->releaseInfo(m_pool);
		return false;
	}
	ICoord2D startNdx;
	worldToCell(&fromPos, true, &startNdx);
	if (!start->allocateInfo(m_pool, startNdx))
	{
		goal->releaseInfo(m_pool);
		return false;
	}
	if (start->getType() == PathfindCell::CELL_OBSTACLE && start->getObstacleID() != m_ignoreObstacleID)
	{
		start->releaseInfo(m_pool);
		goal->releaseInfo(m_pool);
		return false;
	}
	const PathfindMovement mv = makeMovement(obj, loco);
	const bool expand = obj && obj->isKindOf(PK_SHIP);
	goal->startPathfind(nullptr);
	start->clearParentCell();
	start->setTotalCost(start->costToGoal(goal));
	const zoneStorageType goalZone = m_zoneManager.getEffectiveZone(mv, false, goal->getZone());
	const int goalBx = goal->getXIndex() / kBlock, goalBy = goal->getYIndex() / kBlock;
	ICoord2D blocks;
	m_zoneManager.getExtent(blocks);
	m_open.clear();
	m_closedList = nullptr;
	m_open.push(start);
	PathfindCell *closest = nullptr;
	float bestKey = 999999995904.0f; // RW 0xC1C4AC
	zoneStorageType zones[kBlock];
	int zoneCount = 0;
	auto edgeCell = [&](int x, int y, const ICoord2D &delta, PathfindCell *parent, zoneStorageType parentZone) {
		PathfindCell *c = getCell(LAYER_GROUND, x, y);
		if (!c || (c->hasInfo() && (c->getClosed() || c->getOpen())))
		{
			return;
		}
		if (isHuman && (x < m_logicalExtent.lo.x || x > m_logicalExtent.hi.x || y < m_logicalExtent.lo.y || y > m_logicalExtent.hi.y))
		{
			return;
		}
		examineHierarchicalEdgeCell(ICoord2D{ x, y }, delta, parent, goal, parentZone, zones, zoneCount, mv);
	};
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		markClosed(c);
		const zoneStorageType cz = m_zoneManager.getEffectiveZone(mv, false, c->getZone());
		const int bx = c->getXIndex() / kBlock, by = c->getYIndex() / kBlock;
		if (cz == goalZone && bx == goalBx && by == goalBy)
		{
			if (c != goal)
			{
				goal->setParentCell(c);
			}
			m_isTunneling = false;
			makeHierarchicalPathPassable(goal, expand);
			cleanOpenAndClosedLists();
			goal->releaseInfo(m_pool);
			*adjustedTo = goalPos;
			return true;
		}
		// lane WALL-2: the portals of the cell's block (RW 0x6F9089 .. 0x6F9371; the bridge branch before it, RW 0x6F8CC7 .. 0x6F8F7B, is S-161 / S-4856)
		int portalCells = 0;
		hierarchicalPortals(c, goal, cz, goalZone, mv, obj, portalCells);
		const float key = hierarchicalClosestKey((int)goal->getXIndex() - (int)c->getXIndex(), (int)goal->getYIndex() - (int)c->getYIndex());
		if (bestKey > key) // RW 0x6F93C2: comiss best, key; jbe keeps the first of equal keys
		{
			closest = c;
			bestKey = key;
		}
		// the four edges, each one's cells from its middle outward: offsets 0, +1, -1, +2, ..., -7, +8 from the block's cell 8
		if (bx > 0)
		{
			zoneCount = 0;
			for (int i = 1; i < 17; ++i)
			{
				const int off = (i & 1) ? -(i >> 1) : (i >> 1);
				edgeCell(bx * kBlock, by * kBlock + 8 + off, ICoord2D{ -1, 0 }, c, cz);
			}
		}
		if (bx < blocks.x - 1)
		{
			zoneCount = 0;
			for (int i = 1; i < 17; ++i)
			{
				const int off = (i & 1) ? -(i >> 1) : (i >> 1);
				edgeCell(bx * kBlock + 15, by * kBlock + 8 + off, ICoord2D{ 1, 0 }, c, cz);
			}
		}
		if (by > 0)
		{
			zoneCount = 0;
			for (int i = 1; i < 17; ++i)
			{
				const int off = (i & 1) ? -(i >> 1) : (i >> 1);
				edgeCell(bx * kBlock + 8 + off, by * kBlock, ICoord2D{ 0, -1 }, c, cz);
			}
		}
		if (by < blocks.y - 1)
		{
			zoneCount = 0;
			for (int i = 1; i < 17; ++i)
			{
				const int off = (i & 1) ? -(i >> 1) : (i >> 1);
				edgeCell(bx * kBlock + 8 + off, by * kBlock + 15, ICoord2D{ 0, 1 }, c, cz);
			}
		}
	}
	m_isTunneling = false;
	if (closest == nullptr || closest == start)
	{
		m_portalHops.clear(); // RW: none -> the waypoint vector (+ 0x1C1CC) cleared (lane WALL-2)
		cleanOpenAndClosedLists();
		goal->releaseInfo(m_pool);
		return false;
	}
	Coord3D centre = *to;
	adjustCoordToCell(closest->getXIndex(), closest->getYIndex(), true, centre, LAYER_GROUND); // RW 0x6EA0CB(out, 1, record, 1)
	*adjustedTo = centre;
	makeHierarchicalPathPassable(closest, expand);
	cleanOpenAndClosedLists();
	goal->releaseInfo(m_pool);
	return true;
}
