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

#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------------------------------------------------
// PathfindCellInfoPool
// ---------------------------------------------------------------------------------------------------------
void PathfindCellInfoPool::allocate(int count)
{
	m_infos.assign((size_t)count, PathfindCellInfo());
	m_exhausted = false;
	for (int i = 0; i < count; ++i)
	{
		PathfindCellInfo &info = m_infos[(size_t)i];
		info.m_nextOpen = nullptr;
		info.m_prevOpen = nullptr;
		info.m_pathParent = (i + 1 < count) ? &m_infos[(size_t)i + 1] : nullptr;
		info.m_cell = nullptr;
		info.m_totalCost = 0;
		info.m_costSoFar = 0;
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
	m_firstFree = count > 0 ? &m_infos[0] : nullptr;
	m_occChunks.clear();
	m_occFree = nullptr;
	m_occUsedInChunk = 0;
	m_occupantsInUse = 0;
}

PathfindCellInfo *PathfindCellInfoPool::get(PathfindCell *cell, const ICoord2D &pos)
{
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
void PathfindOpenHeap::push(PathfindCell *c)
{
	c->info()->m_open = 1;
	m_v.push_back(c);
	size_t i = m_v.size() - 1;
	while (i > 0)
	{
		const size_t p = (i - 1) / 2;
		if (!(m_v[p]->getTotalCost() > c->getTotalCost()))
		{
			break;
		}
		m_v[i] = m_v[p];
		i = p;
	}
	m_v[i] = c;
}

PathfindCell *PathfindOpenHeap::pop()
{
	PathfindCell *top = m_v[0];
	top->info()->m_open = 0;
	const size_t n = m_v.size();
	PathfindCell *val = m_v[n - 1];
	m_v[n - 1] = m_v[0];
	const size_t len = n - 1; // the heap without the moved root
	size_t h = 0;
	size_t ch = 2 * h + 2;
	while (ch < len)
	{
		if (m_v[ch]->getTotalCost() > m_v[ch - 1]->getTotalCost())
		{
			ch--;
		}
		m_v[h] = m_v[ch];
		h = ch;
		ch = 2 * h + 2;
	}
	if (ch == len)
	{
		m_v[h] = m_v[len - 1];
		h = len - 1;
	}
	// sift the saved element up from the hole with the push rule
	while (h > 0)
	{
		const size_t p = (h - 1) / 2;
		if (!(m_v[p]->getTotalCost() > val->getTotalCost()))
		{
			break;
		}
		m_v[h] = m_v[p];
		h = p;
	}
	if (len > 0)
	{
		m_v[h] = val;
	}
	m_v.pop_back();
	return top;
}
