// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PathfindCellInfo pool and PathfindCell: the per-cell record, the occupant lists (reservations), the closed list, the A* open
// heap and the cost functions. Port of ZH AIPathfind.cpp lines 1100-1760 as changed by RotWK.
//
// SOURCES
//   * target fact (RW game.dat, S-001 caveat): cost functions costSoFar RW 0x934602 (10 orthogonal, 14 diagonal, +14 for a
//     pinched cell (bit 16), +4 / +8 / +16 for a turn of 45 / 90 / 135 degrees against the parent's parent; the 16 bit cost
//     fields wrap past 65535) and costToGoal RW 0x6F1F0D (10 * max + 4 * min; ZH and BFME1 use 10 * max + 5 * min);
//     the open list is a binary min-heap (PathfindOpenHeap below, RW 0x6ECF1D / 0x6ECF65), not ZH's insertion-sorted list;
//     the cell info holds five occupant lists (RW info+0x14..0x24), not ZH's goal / position unit ids.
//   * donor (ZH AIPathfind.cpp:1110-1760): the info pool free list, the obstacle bookkeeping, the closed list.
//   * donor (Open-BFME-1 PathfindCell_costSoFar.cpp, retail 0x003F6D20): the same costSoFar body.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------------------------------------------------
// PathfindCellInfoPool
// ---------------------------------------------------------------------------------------------------------
// lane PATHPERF-4: the records live in blocks that never move (cells and searches keep pointers to them), so the pool can grow: allocate makes the
// first block, and with a growth limit an empty free list gets another block instead of failing (setGrowthLimit: the Uber profile only; BFME1 keeps 0x200 fixed
// slots plus an overflow list, Open-BFME-1 PathfindCellInfoPoolReset.cpp). Until the first block runs out nothing differs from a fixed pool
void PathfindCellInfoPool::addBlock(int count)
{
	PathfindCellInfo *block = new PathfindCellInfo[(size_t)count]();
	m_blocks.emplace_back(block);
	for (int i = 0; i < count; ++i)
	{
		PathfindCellInfo &info = block[i];
		info.m_nextOpen = nullptr;
		info.m_prevOpen = nullptr;
		info.m_pathParent = (i + 1 < count) ? &block[i + 1] : m_firstFree;
		info.m_cell = nullptr;
		info.m_totalCost = 0;
		info.m_costSoFar = 0;
		info.m_viaPortal = 0;
		info.m_pos = ICoord2D();
		info.m_obstacleID = PATHFIND_INVALID_ID;
		for (int k = 0; k < OCC_KIND_COUNT; ++k)
		{
			info.m_occupants[k] = nullptr;
		}
		info.m_isFree = 1;
		info.m_blockedByAlly = 0;
		info.m_obstacleIsFence = 0;
		info.m_obstacleIsTransparent = 0;
		info.m_open = 0;
		info.m_closed = 0;
	}
	if (count > 0)
	{
		m_firstFree = &block[0];
	}
	m_capacity += count;
}

void PathfindCellInfoPool::allocate(int count)
{
	m_blocks.clear();
	m_capacity = 0;
	m_firstFree = nullptr;
	m_exhausted = false;
	addBlock(count);
	m_occChunks.clear();
	m_occFree = nullptr;
	m_occUsedInChunk = 0;
	m_occupantsInUse = 0;
}

PathfindCellInfo *PathfindCellInfoPool::get(PathfindCell *cell, const ICoord2D &pos)
{
	if (!m_firstFree && m_growthLimit > m_capacity)
	{
		const int grow = std::min(std::max(m_capacity / 2, 4096), m_growthLimit - m_capacity);
		addBlock(grow);
		++m_growths;
	}
	PathfindCellInfo *info = m_firstFree;
	if (info)
	{
		m_firstFree = info->m_pathParent;
		info->m_isFree = 0;
		info->m_cell = cell;
		info->m_pos = pos;
		info->m_nextOpen = nullptr;
		info->m_prevOpen = nullptr;
		info->m_pathParent = nullptr;
		info->m_costSoFar = 0;
		info->m_totalCost = 0;
		info->m_viaPortal = 0;
		info->m_open = 0;
		info->m_closed = 0;
		info->m_obstacleID = PATHFIND_INVALID_ID;
		for (int k = 0; k < OCC_KIND_COUNT; ++k)
		{
			info->m_occupants[k] = nullptr;
		}
		info->m_obstacleIsFence = 0;
		info->m_obstacleIsTransparent = 0;
		info->m_blockedByAlly = 0;
	}
	else
	{
		m_exhausted = true;
	}
	return info;
}

void PathfindCellInfoPool::release(PathfindCellInfo *info)
{
	info->m_pathParent = m_firstFree;
	m_firstFree = info;
	info->m_isFree = 1;
}

int PathfindCellInfoPool::freeCount() const
{
	int n = 0;
	for (const PathfindCellInfo *p = m_firstFree; p; p = p->m_pathParent)
	{
		++n;
	}
	return n;
}

PathfindOccupant *PathfindCellInfoPool::newOccupant()
{
	const size_t chunk = 4096;
	PathfindOccupant *o;
	if (m_occFree)
	{
		o = m_occFree;
		m_occFree = o->next;
	}
	else
	{
		if (m_occChunks.empty() || m_occUsedInChunk == chunk)
		{
			m_occChunks.emplace_back(new PathfindOccupant[chunk]);
			m_occUsedInChunk = 0;
		}
		o = &m_occChunks.back()[m_occUsedInChunk++];
	}
	o->next = nullptr;
	o->owner = PATHFIND_INVALID_ID;
	++m_occupantsInUse;
	return o;
}

void PathfindCellInfoPool::freeOccupant(PathfindOccupant *o)
{
	o->next = m_occFree;
	m_occFree = o;
	--m_occupantsInUse;
}

// ---------------------------------------------------------------------------------------------------------
// PathfindCell
// ---------------------------------------------------------------------------------------------------------
void PathfindCell::reset(PathfindCellInfoPool *pool)
{
	m_type = CELL_CLEAR;
	m_zone = 0;
	m_pinched = 0;
	m_bit17 = 0;
	m_bit18 = 0;
	m_bit21 = 0;
	m_bit22 = 0;
	m_bit23 = 0;
	if (pool == nullptr)
	{
		m_info = nullptr;
	}
	else if (m_info)
	{
		m_info->m_obstacleID = PATHFIND_INVALID_ID;
		pool->release(m_info);
		m_info = nullptr;
	}
	m_connectsToLayer = LAYER_INVALID;
	m_layer = LAYER_GROUND;
}

bool PathfindCell::startPathfind(PathfindCell *goalCell)
{
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;
	m_info->m_pathParent = nullptr;
	m_info->m_viaPortal = 0;
	m_info->m_costSoFar = 0;
	m_info->m_totalCost = 0;
	if (goalCell)
	{
		m_info->m_totalCost = (std::uint16_t)costToGoal(goalCell);
	}
	m_info->m_open = 0; // the caller pushes the cell on the heap, which sets the flag
	m_info->m_closed = 0;
	return true;
}

void PathfindCell::setParentCell(PathfindCell *parent)
{
	m_info->m_pathParent = parent->m_info;
	m_info->m_viaPortal = 0; // lane WALL-2 (see the header)
}

void PathfindCell::setParentCellViaPortal(PathfindCell *parent, std::uint16_t portalSlot)
{
	// RW 0x93443D: info + 8 = the parent's record, info + 0xC = the waypoint
	m_info->m_pathParent = parent ? parent->m_info : nullptr;
	m_info->m_viaPortal = portalSlot;
}

bool PathfindCell::allocateInfo(PathfindCellInfoPool &pool, const ICoord2D &pos)
{
	if (!m_info)
	{
		m_info = pool.get(this, pos);
		return m_info != nullptr;
	}
	return true;
}

bool PathfindCell::hasAnyOccupant() const
{
	if (!m_info) return false;
	for (int k = 0; k < OCC_KIND_COUNT; ++k)
	{
		if (m_info->m_occupants[k]) return true;
	}
	return false;
}

void PathfindCell::releaseInfo(PathfindCellInfoPool &pool)
{
	if (m_type == CELL_OBSTACLE)
	{
		return;
	}
	if (hasAnyOccupant())
	{
		return;
	}
	if (m_info)
	{
		// ZH: a record still linked into a list is never released ("better leak than crash")
		if (m_info->m_prevOpen || m_info->m_nextOpen || m_info->m_open || m_info->m_closed)
		{
			return;
		}
		pool.release(m_info);
		m_info = nullptr;
	}
}

void PathfindCell::addOccupant(PathfindCellInfoPool &pool, PathfindOccupantKind kind, PathfindObjectID owner, const ICoord2D &pos)
{
	if (!m_info)
	{
		allocateInfo(pool, pos);
	}
	if (!m_info)
	{
		return; // out of cell infos: the pool reports it (exhaustedOnce)
	}
	// RW 0x8E1DAD: a new node at the head of the kind's list
	PathfindOccupant *o = pool.newOccupant();
	o->owner = owner;
	o->next = m_info->m_occupants[(int)kind];
	m_info->m_occupants[(int)kind] = o;
}

bool PathfindCell::removeOccupant(PathfindCellInfoPool &pool, PathfindOccupantKind kind, PathfindObjectID owner)
{
	if (!m_info)
	{
		return false;
	}
	PathfindOccupant **link = &m_info->m_occupants[(int)kind];
	while (*link)
	{
		if ((*link)->owner == owner)
		{
			PathfindOccupant *dead = *link;
			*link = dead->next;
			pool.freeOccupant(dead);
			releaseInfo(pool);
			return true;
		}
		link = &(*link)->next;
	}
	return false;
}

bool PathfindCell::isOccupiedBy(PathfindOccupantKind kind, PathfindObjectID owner) const
{
	for (const PathfindOccupant *o = occupants(kind); o; o = o->next)
	{
		if (o->owner == owner) return true;
	}
	return false;
}

bool PathfindCell::setTypeAsObstacle(PathfindCellInfoPool &pool, const PathfindObject &obstacle, bool isFence, const ICoord2D &pos)
{
	// RW 0x93485A: only a CLEAR cell or a type-5 cell (the footprint pinch conversion) takes an obstacle
	if (m_type != CELL_CLEAR && m_type != CELL_BRIDGE_IMPASSABLE)
	{
		return false;
	}
	if (obstacle.isRubble())
	{
		m_type = CELL_RUBBLE;
		if (m_info)
		{
			m_info->m_obstacleID = PATHFIND_INVALID_ID;
			releaseInfo(pool);
		}
		return true;
	}
	m_type = CELL_OBSTACLE;
	if (!m_info)
	{
		m_info = pool.get(this, pos);
		if (!m_info)
		{
			return false;
		}
	}
	m_info->m_obstacleID = obstacle.getID();
	m_info->m_obstacleIsFence = isFence;
	m_info->m_obstacleIsTransparent = obstacle.isKindOf(PK_CAN_SEE_THROUGH_STRUCTURE);
	return true;
}

void PathfindCell::setType(CellType type)
{
	// RW 0x934387: an existing obstacle id forces OBSTACLE
	if (m_info && (m_info->m_obstacleID != PATHFIND_INVALID_ID))
	{
		m_type = CELL_OBSTACLE;
		return;
	}
	m_type = type;
}

bool PathfindCell::removeObstacle(PathfindCellInfoPool &pool, const PathfindObject &obstacle)
{
	if (m_type == CELL_RUBBLE)
	{
		m_type = CELL_CLEAR;
	}
	if (!m_info)
	{
		return false;
	}
	if (m_info->m_obstacleID != obstacle.getID())
	{
		return false;
	}
	m_type = CELL_CLEAR;
	m_info->m_obstacleID = PATHFIND_INVALID_ID;
	releaseInfo(pool);
	return true;
}

bool PathfindCell::removeObstacleId(PathfindCellInfoPool &pool, PathfindObjectID id)
{
	if (!m_info || id == PATHFIND_INVALID_ID || m_info->m_obstacleID != id)
	{
		return false;
	}
	m_type = CELL_CLEAR;
	m_info->m_obstacleID = PATHFIND_INVALID_ID;
	releaseInfo(pool);
	return true;
}

// ---- the closed list (links through the info; the open list is the heap) ----
int PathfindCell::releaseClosedList(PathfindCellInfoPool &pool, PathfindCell *list)
{
	int count = 0;
	while (list)
	{
		++count;
		PathfindCell *cur = list;
		PathfindCellInfo *curInfo = list->m_info;
		list = curInfo->m_nextOpen ? curInfo->m_nextOpen->m_cell : nullptr;
		curInfo->m_nextOpen = nullptr;
		curInfo->m_prevOpen = nullptr;
		curInfo->m_closed = 0;
		cur->releaseInfo(pool);
	}
	return count;
}

PathfindCell *PathfindCell::putOnClosedList(PathfindCell *list)
{
	if (m_info->m_closed == 0)
	{
		m_info->m_closed = 1;
		m_info->m_prevOpen = nullptr;
		m_info->m_nextOpen = list ? list->m_info : nullptr;
		if (list)
		{
			list->m_info->m_prevOpen = this->m_info;
		}
		list = this;
	}
	return list;
}

PathfindCell *PathfindCell::removeFromClosedList(PathfindCell *list)
{
	if (m_info->m_nextOpen)
	{
		m_info->m_nextOpen->m_prevOpen = m_info->m_prevOpen;
	}
	if (m_info->m_prevOpen)
	{
		m_info->m_prevOpen->m_nextOpen = m_info->m_nextOpen;
	}
	else
	{
		list = getNextOpen();
	}
	m_info->m_closed = 0;
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;
	return list;
}

// ---- costs ----
unsigned PathfindCell::costToGoal(const PathfindCell *goal) const
{
	// TARGET RW 0x6F1F0D with an empty waypoint vector (S-001 caveat): 10 * max + 4 * min of the cell index differences
	// (dx > dy: 10*dx + 4*dy; else 14*dx + 10*(dy - dx)). ZH (and BFME1) use 10 * max + 5 * min. RW adds `leg + 2*acc + 1400` per
	// entry of the pathfinder's waypoint vector (+0x1C1CC): not modelled (S-163).
	int dx = m_info->m_pos.x - goal->getXIndex();
	int dy = m_info->m_pos.y - goal->getYIndex();
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	int cost;
	if (dx > dy)
	{
		cost = 10 * dx + 4 * dy;
	}
	else
	{
		cost = 14 * dx + 10 * (dy - dx);
	}
	return (unsigned)cost;
}

unsigned PathfindCell::costToHierGoal(const PathfindCell *goal) const
{
	// lane PATHPERF-1. TARGET RW 0x9345AA (BFME2 0x52DC3F, decomp PathfindCell::CalcCostToHierGoal, tier A): d2 = dy*dy + dx*dx of the info positions (integers),
	// fild, the CRT sqrt (RW 0xA3CF96) under the game's 24-bit precision, fmul 10.0f (RW 0xBD83D8), fadd 0.5f (RW 0xBD869C), the CRT floor, fstp dword, fistp
	const int dx = m_info->m_pos.x - goal->getXIndex();
	const int dy = m_info->m_pos.y - goal->getYIndex();
	const int d2 = dy * dy + dx * dx;
	const double r = SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sqrtPC24((double)d2), (double)10.0f), (double)0.5f);
	return (unsigned)SimMath::fistp32((double)SimMath::fstpDword(SimMath::floorD(r)));
}

unsigned PathfindCell::costSoFar(const PathfindCell *parent) const
{
	if (parent == nullptr)
	{
		return 0;
	}
	ICoord2D prevDir;
	int cost;
	prevDir.x = parent->getXIndex() - m_info->m_pos.x;
	prevDir.y = parent->getYIndex() - m_info->m_pos.y;
	if (prevDir.x == 0 || prevDir.y == 0)
	{
		cost = (int)parent->getCostSoFar() + COST_ORTHOGONAL;
	}
	else
	{
		cost = (int)parent->getCostSoFar() + COST_DIAGONAL;
	}
	if (getPinched())
	{
		cost += 1 * COST_DIAGONAL;
	}
	int numTurns = 0;
	const PathfindCell *prevCell = parent->getParentCell();
	if (prevCell)
	{
		ICoord2D dir;
		dir.x = prevCell->getXIndex() - parent->getXIndex();
		dir.y = prevCell->getYIndex() - parent->getYIndex();
		if (dir.x != prevDir.x || dir.y != prevDir.y)
		{
			int dot = dir.x * prevDir.x + dir.y * prevDir.y;
			if (dot > 0)
			{
				numTurns = 4; // 45 degree turn
			}
			else if (dot == 0)
			{
				numTurns = 8; // 90 degree turn
			}
			else
			{
				numTurns = 16; // 135 degree turn
			}
		}
	}
	return (unsigned)(cost + numTurns);
}

// ---------------------------------------------------------------------------------------------------------
// PathfindOpenHeap (RW 0x6ECF1D push, 0x6ECF65 pop; see AIPathfind.h)
// ---------------------------------------------------------------------------------------------------------
// lane PATHPERF-2: the totals live beside the cells (m_k, parallel to m_v), so a comparison reads one dense array instead of cell -> info -> total.
// A cell's total is set before its push and never changed while it is open (every push site skips open and closed cells), so the copy is its total.
void PathfindOpenHeap::push(PathfindCell *c)
{
	c->info()->m_open = 1;
	const std::uint16_t key = (std::uint16_t)c->getTotalCost();
	m_v.push_back(c);
	m_k.push_back(key);
	size_t i = m_v.size() - 1;
	while (i > 0)
	{
		const size_t p = (i - 1) / 2;
		if (!(m_k[p] > key))
		{
			break;
		}
		m_v[i] = m_v[p];
		m_k[i] = m_k[p];
		i = p;
	}
	m_v[i] = c;
	m_k[i] = key;
}

PathfindCell *PathfindOpenHeap::pop()
{
	PathfindCell *top = m_v[0];
	top->info()->m_open = 0;
	const size_t n = m_v.size();
	PathfindCell *val = m_v[n - 1];
	const std::uint16_t valKey = m_k[n - 1];
	m_v[n - 1] = m_v[0];
	m_k[n - 1] = m_k[0];
	const size_t len = n - 1; // the heap without the moved root
	size_t h = 0;
	size_t ch = 2 * h + 2;
	while (ch < len)
	{
		if (m_k[ch] > m_k[ch - 1])
		{
			ch--;
		}
		m_v[h] = m_v[ch];
		m_k[h] = m_k[ch];
		h = ch;
		ch = 2 * h + 2;
	}
	if (ch == len)
	{
		m_v[h] = m_v[len - 1];
		m_k[h] = m_k[len - 1];
		h = len - 1;
	}
	// sift the saved element up from the hole with the push rule
	while (h > 0)
	{
		const size_t p = (h - 1) / 2;
		if (!(m_k[p] > valKey))
		{
			break;
		}
		m_v[h] = m_v[p];
		m_k[h] = m_k[p];
		h = p;
	}
	if (len > 0)
	{
		m_v[h] = val;
		m_k[h] = valKey;
	}
	m_v.pop_back();
	m_k.pop_back();
	return top;
}
