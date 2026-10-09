// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Pathfind zones: connected areas of equal cell class, and which of them a unit can treat as one area. The searches use them to
// refuse an impossible request before they flood the map.
//
// TARGET FACTS (RW game.dat, S-001 caveat; docs/STOPS.md S-162):
//   * the grid is zoned per 16 x 16 block (RW 0x939226); inside a block two 4-adjacent cells share a zone when RW 0x937FD2 says so:
//     they agree on type, layer, connectsToLayer and the cell bits 17, 18, 19-20 and 22 (the pinched bit 16, bit 21 and bit 23 do
//     not count). Raster order x outer, y inner, the left neighbour first then the lower one.
//   * the zone descriptor (RW 0x937AFB): type, layer, connectsToLayer, bit 17, bit 18, bits 19-20, bit 22 of the zone's first cell.
//   * the effective zone of a unit (RW 0x93894B): an AIR surface is zone 1; the surface mask gives a KIND (0x937B9F); the profile
//     p = 2 * e + flag (e = (SlopeLimitIndex-1 >= 0 ? idx : 0) + b4 + 2 * b5, flag 1 = the terrain variant) selects a table of zone
//     equivalences, built from the adjacent zone pairs (0x938E42): kind 0 joins adjacent zones of equal type, kind 1 {0,2},
//     kind 2 {0,1,7}, kind 3 {0,3}, kind 4 {1,7}, kind 5 {2}, kind 6 {0,3} and type 4 when the zone has bit 22 (0x9380BC); a zone
//     blocked by the profile (bit 17 for b4, bit 18 for b5; RW 0x938085) joins nothing.
// INFERRED: the terrain variant (flag 1, "the building does not split the terrain") treats obstacle zones as ground when joining;
// the layer compatibility of RW 0x9380BC is applied to every kind; zone ids are not capped (retail's allocator holds 24000, INFERRED).
// The zone tables are rebuilt lazily from the adjacency list instead of being kept in retail's incremental structures; the
// partition they describe is the same.

#include "GameLogic/AI/AIPathfind.h"

#include <algorithm>
#include <numeric>

namespace
{
// RW 0x9380BC layer compatibility
bool layerCompatible(std::uint8_t aLayer, std::uint8_t aConn, std::uint8_t bLayer, std::uint8_t bConn)
{
	return aLayer == bLayer || aLayer == bConn || aConn == bLayer || (aConn == 0x10 && bConn == 0x10);
}

// root with path compression over a parent array
zoneStorageType findRoot(std::vector<zoneStorageType> &parent, zoneStorageType z)
{
	while (parent[z] != z)
	{
		parent[z] = parent[parent[z]];
		z = parent[z];
	}
	return z;
}
} // namespace

void PathfindZoneManager::reset()
{
	m_blocks.clear();
	m_zoneBlockExtent = ICoord2D();
	m_dirty = false;
	m_zones.clear();
	m_adjacent.clear();
	m_tables.clear();
}

void PathfindZoneManager::allocateBlocks(const PathfindRegion &globalBounds)
{
	// RW 0x939226: (hi - lo + blockSize) / blockSize on each axis
	m_zoneBlockExtent.x = (globalBounds.hi.x - globalBounds.lo.x + m_blockSize) / m_blockSize;
	m_zoneBlockExtent.y = (globalBounds.hi.y - globalBounds.lo.y + m_blockSize) / m_blockSize;
	m_blocks.assign((size_t)m_zoneBlockExtent.x * (size_t)m_zoneBlockExtent.y, Block());
}

void PathfindZoneManager::calculateZones(const PathfindGridView &map, const PathfindRegion &globalBounds)
{
	m_zones.clear();
	m_zones.push_back(ZoneInfo()); // zone 0: "unassigned"
	m_adjacent.clear();
	m_tables.clear();
	const int bs = m_blockSize;
	const int xCount = (globalBounds.hi.x - globalBounds.lo.x + 1 + bs - 1) / bs;
	const int yCount = (globalBounds.hi.y - globalBounds.lo.y + 1 + bs - 1) / bs;
	std::vector<zoneStorageType> parent;
	for (int xBlock = 0; xBlock < xCount; xBlock++)
	{
		for (int yBlock = 0; yBlock < yCount; yBlock++)
		{
			PathfindRegion b;
			b.lo.x = globalBounds.lo.x + xBlock * bs;
			b.lo.y = globalBounds.lo.y + yBlock * bs;
			b.hi.x = std::min(b.lo.x + bs - 1, globalBounds.hi.x);
			b.hi.y = std::min(b.lo.y + bs - 1, globalBounds.hi.y);
			// pass 1: raster union-find with temporary labels
			const int w = b.hi.x - b.lo.x + 1, h = b.hi.y - b.lo.y + 1;
			parent.assign((size_t)w * (size_t)h + 1, 0);
			std::vector<zoneStorageType> label((size_t)w * (size_t)h, 0);
			zoneStorageType n = 1;
			for (int i = b.lo.x; i <= b.hi.x; i++)
			{
				for (int j = b.lo.y; j <= b.hi.y; j++)
				{
					PathfindCell &cell = map.at(i, j);
					zoneStorageType &lab = label[(size_t)(i - b.lo.x) * (size_t)h + (size_t)(j - b.lo.y)];
					lab = 0;
					if (i > b.lo.x && cell.zoneClassEquals(map.at(i - 1, j)))
					{
						lab = label[(size_t)(i - 1 - b.lo.x) * (size_t)h + (size_t)(j - b.lo.y)];
					}
					if (j > b.lo.y && cell.zoneClassEquals(map.at(i, j - 1)))
					{
						const zoneStorageType down = label[(size_t)(i - b.lo.x) * (size_t)h + (size_t)(j - 1 - b.lo.y)];
						if (lab == 0)
						{
							lab = down;
						}
						else
						{
							const zoneStorageType ra = findRoot(parent, lab), rb = findRoot(parent, down);
							if (ra != rb)
							{
								// the lower label stays the root (union by rank is an optimisation only)
								if (ra < rb) parent[rb] = ra; else parent[ra] = rb;
							}
						}
					}
					if (lab == 0)
					{
						lab = n;
						parent[n] = n;
						++n;
					}
				}
			}
			// pass 2: one real zone id per root label (in label order)
			std::vector<zoneStorageType> idOfRoot((size_t)n, 0);
			for (zoneStorageType l = 1; l < n; ++l)
			{
				if (findRoot(parent, l) == l)
				{
					idOfRoot[l] = (zoneStorageType)m_zones.size();
					m_zones.push_back(ZoneInfo());
				}
			}
			// pass 3: assign the zone of each cell and fill the descriptors from the first cell of each zone
			std::vector<bool> described(m_zones.size(), false);
			for (int i = b.lo.x; i <= b.hi.x; i++)
			{
				for (int j = b.lo.y; j <= b.hi.y; j++)
				{
					PathfindCell &cell = map.at(i, j);
					const zoneStorageType lab = label[(size_t)(i - b.lo.x) * (size_t)h + (size_t)(j - b.lo.y)];
					const zoneStorageType id = idOfRoot[findRoot(parent, lab)];
					cell.setZone(id);
					if (!described[id])
					{
						described[id] = true;
						ZoneInfo &zi = m_zones[id];
						zi.type = (std::uint8_t)cell.getType();
						zi.layer = (std::uint8_t)cell.getLayer();
						zi.conn = (std::uint8_t)cell.getConnectLayer();
						zi.bit17 = cell.getBit17();
						zi.bit18 = cell.getImpassableToPlayers();
						zi.bit22 = cell.getBit22();
					}
				}
			}
		}
	}
	// adjacency: every pair of 4-adjacent cells of different zones
	for (int i = globalBounds.lo.x; i <= globalBounds.hi.x; i++)
	{
		for (int j = globalBounds.lo.y; j <= globalBounds.hi.y; j++)
		{
			const zoneStorageType z = map.at(i, j).getZone();
			if (i > globalBounds.lo.x)
			{
				const zoneStorageType o = map.at(i - 1, j).getZone();
				if (o != z) m_adjacent.emplace_back(std::min(z, o), std::max(z, o));
			}
			if (j > globalBounds.lo.y)
			{
				const zoneStorageType o = map.at(i, j - 1).getZone();
				if (o != z) m_adjacent.emplace_back(std::min(z, o), std::max(z, o));
			}
		}
	}
	std::sort(m_adjacent.begin(), m_adjacent.end());
	m_adjacent.erase(std::unique(m_adjacent.begin(), m_adjacent.end()), m_adjacent.end());
	for (Block &bl : m_blocks)
	{
		bl.dirty = false;
	}
	m_dirty = false;
}

void PathfindZoneManager::markDirty(const PathfindRegion &cells)
{
	// RW 0x937CA7: the blocks x / blockSize .. clamped to the grid
	for (int cx = cells.lo.x / m_blockSize; cx <= cells.hi.x / m_blockSize; ++cx)
	{
		for (int cy = cells.lo.y / m_blockSize; cy <= cells.hi.y / m_blockSize; ++cy)
		{
			if (cx >= 0 && cx < m_zoneBlockExtent.x && cy >= 0 && cy < m_zoneBlockExtent.y)
			{
				blockAt(cx, cy).dirty = true;
				m_dirty = true;
			}
		}
	}
}

void PathfindZoneManager::markCellDirty(int cellX, int cellY)
{
	PathfindRegion r;
	r.lo.x = r.hi.x = cellX;
	r.lo.y = r.hi.y = cellY;
	markDirty(r);
}

void PathfindZoneManager::updateDirty(const PathfindGridView &map, const PathfindRegion &globalBounds)
{
	if (m_dirty)
	{
		// the passable flags of the hierarchical search survive the recomputation
		std::vector<bool> keep;
		keep.reserve(m_blocks.size());
		for (const Block &b : m_blocks) keep.push_back(b.passable);
		calculateZones(map, globalBounds);
		for (size_t i = 0; i < m_blocks.size(); ++i) m_blocks[i].passable = keep[i];
	}
}

int PathfindZoneManager::kindOf(const PathfindMovement &mv)
{
	// TARGET RW 0x937B9F (the table of the analysis)
	const unsigned mask = mv.surf;
	const unsigned all = LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_RUBBLE | LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_DEEP_WATER | LOCOMOTORSURFACE_OBSTACLE | LOCOMOTORSURFACE_IMPASSABLE;
	if ((mask & all) == all)
	{
		return 5;
	}
	const unsigned c = mask & (LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_DEEP_WATER);
	switch (c)
	{
	case LOCOMOTORSURFACE_GROUND:
		if (mv.bC) return 6;
		if (mask & LOCOMOTORSURFACE_RUBBLE) return 3;
		return 0;
	case LOCOMOTORSURFACE_WATER:
	case LOCOMOTORSURFACE_CLIFF:
	case LOCOMOTORSURFACE_DEEP_WATER:
		return 0;
	case LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_CLIFF:
		return 1;
	case LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_DEEP_WATER:
		return 4;
	case LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_DEEP_WATER:
		return 2;
	case LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_DEEP_WATER:
		return -1;
	default:
		return -666;
	}
}

bool PathfindZoneManager::joins(const ZoneInfo &a, const ZoneInfo &b, int profile, int kind, bool terrainVariant) const
{
	// the profile filter (RW 0x938085): a zone with bit 17 (profile bit 1) or bit 18 (profile bit 2) is blocked and joins nothing
	if (((profile & 2) && (a.bit17 || b.bit17)) || ((profile & 4) && (a.bit18 || b.bit18)))
	{
		return false;
	}
	if (!layerCompatible(a.layer, a.conn, b.layer, b.conn))
	{
		return false;
	}
	int ta = a.type, tb = b.type;
	if (terrainVariant)
	{
		if (ta == PathfindCell::CELL_OBSTACLE) ta = PathfindCell::CELL_CLEAR;
		if (tb == PathfindCell::CELL_OBSTACLE) tb = PathfindCell::CELL_CLEAR;
	}
	if (kind == 0)
	{
		return ta == tb;
	}
	auto inSet = [&](const ZoneInfo &z, int t) {
		switch (kind)
		{
		case 1: return t == 0 || t == 2;
		case 2: return t == 0 || t == 1 || t == 7;
		case 3: return t == 0 || t == 3;
		case 4: return t == 1 || t == 7;
		case 5: return t == 2;
		case 6: return t == 0 || t == 3 || (t == 4 && z.bit22);
		default: return false;
		}
	};
	return inSet(a, ta) && inSet(b, tb);
}

const std::vector<zoneStorageType> &PathfindZoneManager::table(int profile, int kind, bool terrainVariant) const
{
	const int key = (profile * 8 + kind) * 2 + (terrainVariant ? 1 : 0);
	auto it = m_tables.find(key);
	if (it != m_tables.end())
	{
		return it->second;
	}
	std::vector<zoneStorageType> parent(m_zones.size());
	std::iota(parent.begin(), parent.end(), (zoneStorageType)0);
	for (const auto &pair : m_adjacent)
	{
		if (joins(m_zones[pair.first], m_zones[pair.second], profile, kind, terrainVariant))
		{
			const zoneStorageType ra = findRoot(parent, pair.first), rb = findRoot(parent, pair.second);
			if (ra != rb)
			{
				if (ra < rb) parent[rb] = ra; else parent[ra] = rb;
			}
		}
	}
	for (size_t z = 0; z < parent.size(); ++z)
	{
		parent[z] = findRoot(parent, (zoneStorageType)z);
	}
	return m_tables.emplace(key, std::move(parent)).first->second;
}

zoneStorageType PathfindZoneManager::getEffectiveZone(const PathfindMovement &mv, bool terrainVariant, zoneStorageType zone) const
{
	if (zone >= m_zones.size())
	{
		return 0;
	}
	if (mv.surf & LOCOMOTORSURFACE_AIR)
	{
		return 1; // air is all zone 1
	}
	const int kind = kindOf(mv);
	if (kind < 0)
	{
		return kind == -1 ? 1 : 0;
	}
	if (zone == UNINITIALIZED_ZONE)
	{
		return 0;
	}
	const int e = (mv.i8 >= 0 ? mv.i8 + 1 : 0) + (mv.b4 ? 1 : 0) + (mv.b5 ? 2 : 0);
	const int profile = 2 * e + (terrainVariant ? 1 : 0);
	return table(profile, kind, terrainVariant)[zone];
}

void PathfindZoneManager::clearPassableFlags()
{
	for (Block &b : m_blocks) b.passable = false;
}

void PathfindZoneManager::setAllPassable()
{
	for (Block &b : m_blocks) b.passable = true;
}

void PathfindZoneManager::setPassable(int cellX, int cellY, bool passable)
{
	const int bx = cellX / m_blockSize, by = cellY / m_blockSize;
	if (bx < 0 || bx >= m_zoneBlockExtent.x || by < 0 || by >= m_zoneBlockExtent.y)
	{
		return;
	}
	blockAt(bx, by).passable = passable;
}

bool PathfindZoneManager::isPassable(int cellX, int cellY) const
{
	// RW 0x937E77: false outside the grid
	if (cellX < 0 || cellY < 0)
	{
		return false;
	}
	const int bx = cellX / m_blockSize, by = cellY / m_blockSize;
	if (bx >= m_zoneBlockExtent.x || by >= m_zoneBlockExtent.y)
	{
		return false;
	}
	return m_blocks[(size_t)bx * (size_t)m_zoneBlockExtent.y + (size_t)by].passable;
}
