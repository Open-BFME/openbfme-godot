// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The pathfinder's part of the obfme:AIWorld block (lane SAVE-4c, docs/SAVES.md section 8). OpenBFME native layout (stop S-4220). Retail: ZH saves no
// pathfinder (ZH AI::xfer saves the groups; the pathfinder map is rebuilt from the terrain and the objects at the load, ZH Pathfinder::newMap); RotWK's
// PathfindZoneManager::DoXfer RW 0x93AC0F, its Block / CellType DoXfers RW 0x93A43D / 0x9382DD and PathfindLayer RW 0x7683B8 exist (docs/SAVES-RETAIL.md
// section 6, tier A, no version byte) but no CHUNK token is tied to them. A rebuild cannot give this port's state back: the occupant chains of a cell are
// in the order the units reserved them (head insertion, RW 0x8E1DAD), and that order decides the blocker tests. So the grid's changing part is saved:
//   * the configuration and the extents are the map's and the data's: a snapshot whose hash of them differs is refused (another map or AIData);
//   * every cell's class word (type, layer, connected layer, the bits 17, 18, 21, 22, 23, the pinch) and its zone, each in runs of equal cells; then
//     every cell with a cell info by index: the obstacle id, the flags and its non-empty occupant chains in order;
//   * the zone manager (blocks, zone classes, adjacency), both request rings (head, tail, ids), every unit's goal and position slots, the cell counter.
// Not saved, rebuilt: the cell info pool's free list and the occupant node pool (only their counts are state: a load allocates the pool again and takes an
// info for each cell that has one), the zone manager's equivalence tables (a cache), the search scratch (between frames the open heap and the closed list
// are empty: a save during a search is refused; an info's cost and parent fields are written by a search before it reads them, INFERENCE S-4221;
// lane PATHPERF-1's hierarchical corridor index and search memo are a search's scratch too, and its unit index is made again from the records).

#include "GameLogic/AI/AIPathfind.h"

#include "Common/StateHash.h"
#include "GameLogic/AI/AIXfer.h"

#include <map>
#include <set>

namespace
{
// version 3 (lanes WALL-1 / WALL-2): a cell's layer and connected layer take 7 bits each (0 .. 0x40: the wall tops 0x11 .. 0x40, the ramp 0x10, the raised
// walkways 2 .. 15); versions 1 and 2 (upstream's 4-bit fields, a map without walls) are still read
std::uint32_t classWord(const PathfindCell &c)
{
	return (std::uint32_t)c.getType() | ((std::uint32_t)c.getLayer() << 4) | ((std::uint32_t)c.getConnectLayer() << 11) | (c.getBit17() ? 1u << 18 : 0u) |
		(c.getImpassableToPlayers() ? 1u << 19 : 0u) | (c.getExtraPass() ? 1u << 20 : 0u) | (c.getBit22() ? 1u << 21 : 0u) | (c.getBit23() ? 1u << 22 : 0u) |
		(c.getPinched() ? 1u << 23 : 0u);
}

// a version 1 or 2 word in version 3's layout
std::uint32_t widenClassWord(std::uint32_t w)
{
	return (w & 0xFu) | (((w >> 4) & 0xFu) << 4) | (((w >> 8) & 0xFu) << 11) | ((w >> 12) << 18);
}

// the closed set of a class word: the 24 bits classWord writes (18 before version 3), a cell type 0 .. 7 (RW 0xDA2444's table), layers up to LAYER_LAST
void checkClassWord(std::uint32_t w, int version)
{
	const int bits = version >= 3 ? 24 : 18;
	if (w >> bits)
	{
		throw XferException("pathfinder: a cell class word " + std::to_string(w) + " with bits beyond the " + std::to_string(bits) + " a cell keeps");
	}
	AIXfer::inRange(w & 0xF, 0, 7, "pathfinder: a cell type");
	if (version >= 3)
	{
		AIXfer::inRange((w >> 4) & 0x7F, LAYER_INVALID, LAYER_LAST, "pathfinder: a cell's layer");
		AIXfer::inRange((w >> 11) & 0x7F, LAYER_INVALID, LAYER_LAST, "pathfinder: a cell's connected layer");
	}
}

void setClassWord(PathfindCell &c, std::uint32_t w)
{
	c.setType((PathfindCell::CellType)(w & 0xF));
	c.setLayer((PathfindLayerEnum)((w >> 4) & 0x7F));
	c.setConnectLayer((PathfindLayerEnum)((w >> 11) & 0x7F));
	c.setBit17((w >> 18) & 1);
	c.setImpassableToPlayers((w >> 19) & 1);
	c.setExtraPass((w >> 20) & 1);
	c.setBit22((w >> 21) & 1);
	c.setBit23((w >> 22) & 1);
	c.setPinched((w >> 23) & 1);
}

} // namespace

std::uint32_t Pathfinder::classWord(const PathfindCell &c)
{
	return ::classWord(c);
}

void Pathfinder::setClassWordWide(PathfindCell &c, std::uint32_t w)
{
	::setClassWord(c, w);
}

void PathfindRequestRing::xfer(Xfer &x)
{
	std::int32_t head = m_head, tail = m_tail;
	x.xferInt(&head);
	x.xferInt(&tail);
	if (x.isLoading())
	{
		if (head < 0 || head >= m_len || tail < 0 || tail >= m_len) // lane PATHPERF-4: this ring's length (the game's profile)
		{
			throw XferException("pathfinder: a request ring's head " + std::to_string(head) + " / tail " + std::to_string(tail));
		}
		reset();
		m_head = head;
		m_tail = tail;
	}
	for (int s = m_head; s != m_tail; s = (s + 1) % m_len)
	{
		x.xferUnsignedInt(&m_slots[(size_t)s]);
		if (x.isLoading()) // lane SAVE-4e (closed sets): an id the game issued (a later object must not inherit a queued request)
		{
			AIXfer::issued(m_slots[(size_t)s], "pathfinder: a queued request's object");
		}
	}
}

void PathfindZoneManager::xfer(Xfer &x)
{
	x.xferInt(&m_blockSize);
	AIXfer::icoord2(x, m_zoneBlockExtent);
	x.xferBool(&m_dirty);
	x.xferVectorSize(m_blocks, 2);
	for (Block &b : m_blocks)
	{
		x.xferBool(&b.passable);
		x.xferBool(&b.dirty);
	}
	x.xferVectorSize(m_zones, 6);
	for (ZoneInfo &z : m_zones)
	{
		x.xferUnsignedByte(&z.type);
		x.xferUnsignedByte(&z.layer);
		x.xferUnsignedByte(&z.conn);
		x.xferBool(&z.bit17);
		x.xferBool(&z.bit18);
		x.xferBool(&z.bit22);
	}
	x.xferVectorSize(m_adjacent, 4);
	for (auto &a : m_adjacent)
	{
		x.xferUnsignedShort(&a.first);
		x.xferUnsignedShort(&a.second);
	}
	if (!x.isLoading())
	{
		return;
	}
	m_tables.clear();
	// the closed sets: a block per block of the extent, a zone's class (a cell type 0 .. 7, layers 0 .. LAYER_LAST, lanes WALL-1 / WALL-2), the adjacency sorted, unique, a < b, of zones
	if (m_blockSize <= 0 || m_zoneBlockExtent.x < 0 || m_zoneBlockExtent.y < 0 ||
		(std::uint64_t)m_zoneBlockExtent.x * (std::uint64_t)m_zoneBlockExtent.y != m_blocks.size())
	{
		throw XferException("pathfinder zones: " + std::to_string(m_blocks.size()) + " blocks for an extent of " + std::to_string(m_zoneBlockExtent.x) + " x " +
			std::to_string(m_zoneBlockExtent.y) + " (block size " + std::to_string(m_blockSize) + ")");
	}
	for (const ZoneInfo &z : m_zones)
	{
		AIXfer::inRange(z.type, 0, 7, "pathfinder zones: a zone's cell type");
		AIXfer::inRange(z.layer, LAYER_INVALID, LAYER_LAST, "pathfinder zones: a zone's layer");
		AIXfer::inRange(z.conn, LAYER_INVALID, LAYER_LAST, "pathfinder zones: a zone's connected layer");
	}
	for (size_t i = 0; i < m_adjacent.size(); ++i)
	{
		const auto &a = m_adjacent[i];
		if (!(a.first < a.second) || a.second >= m_zones.size() || (i != 0 && !(m_adjacent[i - 1] < a)))
		{
			throw XferException("pathfinder zones: the adjacency " + std::to_string(a.first) + " - " + std::to_string(a.second) +
				" is out of order or names a zone beyond the " + std::to_string(m_zones.size()));
		}
	}
}

std::uint32_t Pathfinder::configHash() const
{
	StateHasher h;
	h.addFloat(m_config.wadeWaterDepth);
	h.addFloat(m_config.deepWaterDepth);
	h.addFloat(m_config.slopeLimits[0]);
	h.addFloat(m_config.slopeLimits[1]);
	for (int v : { m_config.cellsPerFrame, m_config.adjustDestinationLimit, m_config.adjustHordeMeleeLimit, m_config.patchPathLimit, m_config.findPathLimit,
			 m_config.adjustToPossibleLimit, m_config.examineTowardsGoalLimit, m_config.cellInfoPoolSize, m_config.zoneBlockSize, m_config.findAttackPathLimit,
			 m_config.adjustToMeleeLimit, m_config.findMeleeEngagementLimit })
	{
		h.addI32(v);
	}
	h.addBool(m_config.hordesWaitForHordes);
	h.addBool(m_config.planningModeEnabled);
	h.addFloat(m_config.meleeApproachDist);
	h.addFloat(m_config.meleeApproachTolerance);
	h.addFloat(m_config.castleSiegeStandBackDistance);
	for (int v : { m_extent.lo.x, m_extent.lo.y, m_extent.hi.x, m_extent.hi.y, m_logicalExtent.lo.x, m_logicalExtent.lo.y, m_logicalExtent.hi.x,
			 m_logicalExtent.hi.y, m_stride })
	{
		h.addI32(v);
	}
	h.addU32((std::uint32_t)m_blockOfMapCells.size());
	// upstream hashed the pool's capacity here, which was always the configured size; an Uber pool grows (lane PATHPERF-4), so the configured size is
	// hashed instead: the same value as before, so a snapshot of an earlier build keeps its configuration hash (review #63)
	h.addI32(m_config.cellInfoPoolSize);
	return h.value();
}

void Pathfinder::xfer(Xfer &x)
{
	XferVersion v{ 1, 3 }; // 2: the Uber profile's limits, the request rings' lengths and the burst latch (lane PATHPERF-4); 3: the walls, the raised
	                       // walkways and the portals (lanes WALL-1 / WALL-2), the wide class words
	x.xferVersion(v);
	const int version = v.current;
	const bool loading = x.isLoading();
	if (!loading && (!m_open.empty() || m_closedList))
	{
		throw XferException("pathfinder: a search is running (a snapshot is taken between frames)");
	}
	bool ready = loading ? false : m_isMapReady;
	x.xferBool(&ready);
	std::uint32_t config = loading ? 0 : configHash();
	x.xferUnsignedInt(&config);
	if (loading && (ready != m_isMapReady || config != configHash()))
	{
		throw XferException(std::string("pathfinder: the snapshot's map grid or pathfinding data differs from this game's (") +
			(ready != m_isMapReady ? "one grid is not made" : "the configuration or the extents") + ")");
	}
	// everything is read into these first; a load changes the pathfinder only once all of it is read and checked (rule 4)
	bool tunneling = m_isTunneling;
	PathfindObjectID ignore = m_ignoreObstacleID;
	std::int32_t cumulative = m_cumulativeCellsAllocated;
	bool exhausted = m_pool.exhaustedOnce();
	x.xferBool(&tunneling);
	x.xferUnsignedInt(&ignore);
	if (loading)
	{
		AIXfer::issued(ignore, "pathfinder: the obstacle it ignores"); // lane SAVE-4e (closed sets)
	}
	x.xferInt(&cumulative);
	x.xferBool(&exhausted);
	const std::uint32_t cells = (std::uint32_t)m_blockOfMapCells.size(); // the map's grid (the configuration hash covers its size)
	std::vector<std::uint32_t> words;
	std::vector<std::uint16_t> zones;
	if (loading)
	{
		words.resize(cells);
		zones.resize(cells);
	}
	// the class words and the zones, each in runs of equal cells (u32 run length, the value)
	auto runs = [&](auto get, auto &loaded, auto xferValue, const char *what) {
		std::uint32_t at = 0;
		while (at < cells)
		{
			std::uint32_t run = 1;
			auto value = loading ? decltype(get(m_blockOfMapCells[0])){} : get(m_blockOfMapCells[at]);
			if (!loading)
			{
				while (at + run < cells && get(m_blockOfMapCells[at + run]) == value)
				{
					++run;
				}
			}
			x.xferUnsignedInt(&run);
			xferValue(value);
			if (run == 0 || run > cells - at)
			{
				throw XferException(std::string("pathfinder: a run of ") + std::to_string(run) + " " + what + " at cell " + std::to_string(at) + " of " +
					std::to_string(cells));
			}
			for (std::uint32_t i = 0; loading && i < run; ++i)
			{
				loaded[at + i] = value;
			}
			at += run;
		}
	};
	runs([](const PathfindCell &c) { return classWord(c); }, words,
		[&](std::uint32_t &w) {
			x.xferUnsignedInt(&w);
			checkClassWord(w, version);
			if (version < 3)
			{
				w = widenClassWord(w);
			}
		},
		"class words");
	runs([](const PathfindCell &c) { return (std::uint16_t)c.getZone(); }, zones, [&](std::uint16_t &z) { x.xferUnsignedShort(&z); }, "zones");
	// the cells with a cell info, in index order (the cell (x, y) is m_blockOfMapCells[x * stride + y], Pathfinder::getCell): the obstacle id, the flags, a
	// mask of the occupant kinds that have a chain, then those chains in order
	struct LoadedInfo
	{
		std::uint32_t index;
		PathfindObjectID obstacle;
		std::uint8_t flags;
		std::vector<std::uint32_t> owners[OCC_KIND_COUNT];
	};
	std::vector<LoadedInfo> infos;
	std::uint32_t infoCount = 0;
	for (const PathfindCell &c : m_blockOfMapCells)
	{
		infoCount += c.hasInfo() ? 1u : 0u;
	}
	x.xferCount(&infoCount, 10, sizeof(LoadedInfo)); // index, obstacle, flags, kinds
	// lane PATHPERF-4: the pool grows up to its limit, which is the SNAPSHOT's profile (its Uber flag comes after the infos, checked there); here only
	// the most any profile holds, so a count no pool can take is refused before it is read
	if (loading && infoCount > (std::uint32_t)std::max(m_pool.capacity(), (int)kCellInfoPoolMax))
	{
		throw XferException("pathfinder: the snapshot holds " + std::to_string(infoCount) + " cell infos, more than any pool of " +
			std::to_string(std::max(m_pool.capacity(), (int)kCellInfoPoolMax)));
	}
	std::uint32_t index = 0, last = 0;
	for (std::uint32_t n = 0; n < infoCount; ++n)
	{
		LoadedInfo li{};
		std::uint8_t kinds = 0;
		if (!loading)
		{
			while (!m_blockOfMapCells[index].hasInfo())
			{
				++index;
			}
			const PathfindCell &c = m_blockOfMapCells[index];
			const PathfindCellInfo *info = c.info();
			li.obstacle = info->m_obstacleID;
			li.flags = (std::uint8_t)((info->m_blockedByAlly ? 1 : 0) | (info->m_obstacleIsFence ? 2 : 0) | (info->m_obstacleIsTransparent ? 4 : 0));
			for (int k = 0; k < OCC_KIND_COUNT; ++k)
			{
				for (const PathfindOccupant *o = c.occupants((PathfindOccupantKind)k); o; o = o->next)
				{
					li.owners[k].push_back(o->owner);
				}
				kinds |= li.owners[k].empty() ? 0 : (std::uint8_t)(1u << k);
			}
		}
		li.index = index;
		x.xferUnsignedInt(&li.index);
		if (li.index >= cells || (n != 0 && li.index <= last))
		{
			throw XferException("pathfinder: a cell info at cell " + std::to_string(li.index) + " out of order or beyond the " + std::to_string(cells) + " cells");
		}
		last = li.index;
		x.xferUnsignedInt(&li.obstacle);
		x.xferUnsignedByte(&li.flags);
		x.xferUnsignedByte(&kinds);
		if (li.flags >> 3 || kinds >> OCC_KIND_COUNT)
		{
			throw XferException("pathfinder: the cell info at cell " + std::to_string(li.index) + " has unknown flags or occupant kinds");
		}
		for (int k = 0; k < OCC_KIND_COUNT; ++k)
		{
			if (kinds & (1u << k))
			{
				AIXfer::ids(x, li.owners[k]);
				if (loading && li.owners[k].empty())
				{
					throw XferException("pathfinder: an empty occupant chain at cell " + std::to_string(li.index));
				}
			}
		}
		if (loading)
		{
			infos.push_back(std::move(li));
		}
		index = li.index + 1;
	}
	PathfindZoneManager zoneManager;
	PathfindRequestRing queue1, queue2;
	// version 2 (lane PATHPERF-4): the rings' lengths, retail's PATHFIND_QUEUE_LEN or the Uber profile's kUberQueueLength, so a game that switched its
	// profile loads into a fresh game of either one (review #64); a version 1 snapshot's rings are retail's
	std::int32_t len1 = loading ? (std::int32_t)PATHFIND_QUEUE_LEN : (std::int32_t)m_queue1.length();
	std::int32_t len2 = loading ? (std::int32_t)PATHFIND_QUEUE_LEN : (std::int32_t)m_queue2.length();
	bool uberLimits = m_uberLimits; // a version 1 snapshot keeps the loading game's
	if (version >= 2)
	{
		x.xferBool(&uberLimits); // the Uber profile's limits on (Pathfinder::setUberLimits; the state hash folds the burst latch only then)
		x.xferInt(&len1);
		x.xferInt(&len2);
		if ((len1 != PATHFIND_QUEUE_LEN && len1 != kUberQueueLength) || (len2 != PATHFIND_QUEUE_LEN && len2 != kUberQueueLength))
		{
			throw XferException("pathfinder: request ring lengths len1 " + std::to_string(len1) + ", len2 " + std::to_string(len2));
		}
	}
	queue1.setLength(len1);
	queue2.setLength(len2);
	PathfindZoneManager &zm = loading ? zoneManager : m_zoneManager;
	PathfindRequestRing &q1 = loading ? queue1 : m_queue1, &q2 = loading ? queue2 : m_queue2;
	zm.xfer(x);
	q1.xfer(x);
	q2.xfer(x);
	// every unit's slots (a slot: occupied, its cell, angle code, layer, kind, the cells it registered)
	std::map<PathfindObjectID, UnitRecord> units;
	std::uint32_t unitCount = (std::uint32_t)m_units.size();
	x.xferCount(&unitCount, 4 + 2 * 25, sizeof(UnitRecord) + Xfer::kNodeBytes);
	auto xferSlot = [&](Slot &s) {
		x.xferBool(&s.occupied);
		AIXfer::icoord2(x, s.cell);
		x.xferInt(&s.angleCode);
		AIXfer::enumIn(x, s.layer, LAYER_INVALID, LAYER_LAST, "pathfinder: a unit slot's layer");
		AIXfer::enumIn(x, s.kind, OCC_GROUND_GOAL, OCC_KIND_COUNT - 1, "pathfinder: a unit slot's occupant kind");
		std::uint32_t n = (std::uint32_t)s.cells.size();
		x.xferCount(&n, 8, sizeof(ICoord2D));
		if (loading)
		{
			AIXfer::inRange(s.layer, LAYER_INVALID, LAYER_LAST, "pathfinder: a unit slot's layer");
			AIXfer::inRange(s.kind, OCC_GROUND_GOAL, OCC_KIND_COUNT - 1, "pathfinder: a unit slot's occupant kind");
			s.cells.assign(n, ICoord2D());
		}
		for (ICoord2D &c : s.cells)
		{
			AIXfer::icoord2(x, c);
		}
	};
	if (loading)
	{
		for (std::uint32_t i = 0; i < unitCount; ++i)
		{
			PathfindObjectID id = 0;
			x.xferUnsignedInt(&id);
			UnitRecord r;
			xferSlot(r.goal);
			xferSlot(r.pos);
			if (!units.emplace(id, std::move(r)).second)
			{
				throw XferException("pathfinder: the reservations of object " + std::to_string(id) + " are saved twice");
			}
		}
	}
	else
	{
		for (auto &kv : m_units)
		{
			PathfindObjectID id = kv.first;
			x.xferUnsignedInt(&id);
			xferSlot(kv.second.goal);
			xferSlot(kv.second.pos);
		}
	}
	std::vector<std::string> stops = m_stops;
	AIXfer::reportStrings(x, stops);
	// version 2 (lane PATHPERF-4): the request queue's burst latch (AIPathfindUnits.cpp; hashed in an Uber game, always false in a Faithful one)
	bool uberBurst = loading ? false : m_uberBurst;
	if (version >= 2)
	{
		x.xferBool(&uberBurst);
	}
	WallState walls;
	if (version >= 3)
	{
		xferWalls(x, walls);
	}
	if (!loading)
	{
		return;
	}
	// the snapshot's profile decides the pool's limit (setGrowthLimit below), not the loading game's (review of #62: a game whose profile is set after
	// its map, AIWorld's setUberLimits when it differs, would check against the wrong one)
	const std::size_t most = (std::size_t)std::max(m_pool.capacity(), uberLimits ? (int)kCellInfoPoolMax : 0);
	if (infos.size() > most)
	{
		throw XferException("pathfinder: the snapshot holds " + std::to_string(infos.size()) + " cell infos, more than the pool of " + std::to_string(most));
	}
	// review of #63: the raised layers' cell infos come from the same pool as the ground's (applyWalls allocates them after the ground's); each count was
	// checked alone, so a snapshot whose two together pass the pool is refused here, before anything changes
	{
		std::size_t raisedInfos = 0;
		for (const WallState::Raised &r : walls.raised)
		{
			raisedInfos += r.used ? r.infos.size() : 0;
		}
		if (infos.size() + raisedInfos > most)
		{
			throw XferException("pathfinder: the snapshot holds " + std::to_string(infos.size()) + " ground and " + std::to_string(raisedInfos) +
				" raised cell infos, more than the pool of " + std::to_string(most));
		}
	}
	// the references: the zones name zones of the loaded table; the obstacles, the occupants and the reserving units are objects of the game (the
	// pathfinder's world); request rings hold any id (a unit that died while queued stays queued; the handler skips it)
	auto object = [&](PathfindObjectID id, const char *what) {
		if (id != PATHFIND_INVALID_ID && (!m_world || !m_world->findObjectByID(id)))
		{
			throw XferException(std::string("pathfinder: ") + what + " " + std::to_string(id) + " is not an object of the loaded game");
		}
	};
	for (std::uint32_t i = 0; i < cells; ++i)
	{
		if (zones[i] != 0 && zones[i] >= zm.zoneCount())
		{
			throw XferException("pathfinder: cell " + std::to_string(i) + " is in the zone " + std::to_string(zones[i]) + " of " + std::to_string(zm.zoneCount()));
		}
	}
	// lanes WALL-1 / WALL-2: a raised cell's zone too (the raised layers' zones are numbered after the ground's in the same table, AIPathfindZones.cpp)
	for (const WallState::Raised &r : walls.raised)
	{
		for (std::uint16_t z : r.zones)
		{
			if (z != 0 && z >= zm.zoneCount())
			{
				throw XferException("pathfinder: a raised cell is in the zone " + std::to_string(z) + " of " + std::to_string(zm.zoneCount()));
			}
		}
	}
	for (const LoadedInfo &li : infos)
	{
		object(li.obstacle, "the obstacle of a cell");
		for (const std::vector<std::uint32_t> &chain : li.owners)
		{
			std::set<std::uint32_t> once(chain.begin(), chain.end());
			if (once.size() != chain.size())
			{
				throw XferException("pathfinder: an occupant chain of cell " + std::to_string(li.index) + " names a unit twice");
			}
			for (std::uint32_t owner : chain)
			{
				object(owner, "the occupant of a cell");
			}
		}
		// every occupant is a reservation its unit holds (review r3: a reference belongs where it claims): the unit's goal or position slot is occupied,
		// of this kind, and registered this cell (Pathfinder::registerSlot adds a node per cell of the slot; releaseSlot takes them off by the slot's cells,
		// so a node no slot names could never leave)
		ICoord2D at;
		at.x = (int)(li.index / (std::uint32_t)m_stride);
		at.y = (int)(li.index % (std::uint32_t)m_stride);
		for (int k = 0; k < OCC_KIND_COUNT; ++k)
		{
			for (std::uint32_t owner : li.owners[k])
			{
				auto u = units.find(owner);
				bool held = false;
				for (const Slot *slot : { u == units.end() ? nullptr : &u->second.goal, u == units.end() ? nullptr : &u->second.pos })
				{
					if (!slot || !slot->occupied || (int)slot->kind != k)
					{
						continue;
					}
					for (const ICoord2D &c : slot->cells)
					{
						held = held || (c.x == at.x && c.y == at.y);
					}
				}
				if (!held)
				{
					throw XferException("pathfinder: unit " + std::to_string(owner) + " occupies cell " + std::to_string(at.x) + ", " + std::to_string(at.y) +
						" (kind " + std::to_string(k) + ") without a reservation of it");
				}
			}
		}
	}
	object(ignore, "the ignored obstacle");
	for (size_t i = 0; i < q1.size(); ++i)
	{
		AIXfer::issued(q1.at(i), "pathfinder: a queued path request's unit");
	}
	for (size_t i = 0; i < q2.size(); ++i)
	{
		AIXfer::issued(q2.at(i), "pathfinder: a queued blocked repath's unit");
	}
	for (const auto &kv : units)
	{
		AIXfer::issued(kv.first, "pathfinder: the unit of a reservation record");
		// a unit that left keeps its record with both slots released (Pathfinder::unit makes a record on first use and never erases it): only a record
		// that holds a reservation must name an object of the game
		if (kv.second.goal.occupied || kv.second.pos.occupied)
		{
			object(kv.first, "the unit of a reservation");
		}
		if (kv.first == PATHFIND_INVALID_ID)
		{
			throw XferException("pathfinder: a reservation of no unit");
		}
	}
	// valid: the grid is replaced (every cell reset and its info taken again from a fresh pool)
	for (PathfindCell &c : m_blockOfMapCells)
	{
		c.reset(nullptr); // the infos go with the pool below (nothing is released into the old free list)
	}
	// as setUberLimits: only an Uber pool grows. Set before the infos are taken again: an Uber snapshot's may pass the configured pool
	m_pool.setGrowthLimit(uberLimits ? (int)kCellInfoPoolMax : 0);
	m_pool.allocate(m_pool.capacity());
	for (std::uint32_t i = 0; i < cells; ++i)
	{
		setClassWord(m_blockOfMapCells[i], words[i]);
		m_blockOfMapCells[i].setZone(zones[i]);
	}
	for (const LoadedInfo &li : infos)
	{
		PathfindCell &c = m_blockOfMapCells[li.index];
		ICoord2D pos;
		pos.x = (int)(li.index / (std::uint32_t)m_stride);
		pos.y = (int)(li.index % (std::uint32_t)m_stride);
		c.allocateInfo(m_pool, pos); // the count is within the pool (checked above, against the snapshot's limit)
		PathfindCellInfo *info = c.info();
		info->m_obstacleID = li.obstacle;
		info->m_blockedByAlly = li.flags & 1;
		info->m_obstacleIsFence = (li.flags >> 1) & 1;
		info->m_obstacleIsTransparent = (li.flags >> 2) & 1;
		for (int k = 0; k < OCC_KIND_COUNT; ++k)
		{
			for (size_t i = li.owners[k].size(); i-- > 0;)
			{
				c.addOccupant(m_pool, (PathfindOccupantKind)k, li.owners[k][i], pos); // head insertion (RW 0x8E1DAD): the last first gives the saved order
			}
		}
	}
	m_pool.setExhausted(exhausted);
	m_isTunneling = tunneling;
	m_ignoreObstacleID = ignore;
	m_cumulativeCellsAllocated = cumulative;
	m_zoneManager = std::move(zoneManager);
	m_queue1 = queue1;
	m_queue2 = queue2;
	m_units = std::move(units);
	// lane PATHPERF-1's unit index points into the map: made again for the loaded records (Pathfinder::unit's rule: ids below kUnitIndexMax)
	m_unitIndex.clear();
	for (auto &kv : m_units)
	{
		if (kv.first < (PathfindObjectID)kUnitIndexMax)
		{
			if (kv.first >= m_unitIndex.size())
			{
				m_unitIndex.resize((size_t)kv.first + 1, nullptr);
			}
			m_unitIndex[kv.first] = &kv.second;
		}
	}
	m_hierLast = -1; // the last search's corridor is that search's scratch (rebuilt by findClosestHierarchicalPath before it is read)
	m_stops = std::move(stops);
	m_uberBurst = uberBurst;
	m_uberLimits = uberLimits;
	applyWalls(walls, version >= 3); // its raised infos come from the pool after the ground's (the limit set above)
}

void Path::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	if (x.isLoading())
	{
		xferLoad(x);
		return;
	}
	x.xferBool(&m_isOptimized);
	x.xferBool(&m_blockedByAlly);
	x.xferReal(&m_t);
	AIXfer::coord(x, m_lastAhead);
	std::vector<PathNode *> nodes;
	for (PathNode *n = m_path; n; n = n->m_next)
	{
		nodes.push_back(n);
	}
	auto indexOf = [&](const PathNode *p) -> std::int32_t {
		if (!p)
		{
			return -1;
		}
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			if (nodes[i] == p)
			{
				return (std::int32_t)i;
			}
		}
		throw XferException("path: a link to a node outside the path");
	};
	std::uint32_t n = (std::uint32_t)nodes.size();
	x.xferCount(&n, 37, sizeof(PathNode)); // a node: position, layer, flag, waypoint, distance, direction, its optimised link
	for (PathNode *node : nodes)
	{
		AIXfer::coord(x, node->m_pos);
		AIXfer::enumIn(x, node->m_layer, LAYER_INVALID, LAYER_LAST, "path: a node's layer");
		x.xferBool(&node->m_canOptimize);
		x.xferInt(&node->m_waypointID);
		x.xferReal(&node->m_nextOptiDist2D);
		AIXfer::coord2(x, node->m_nextOptiDirNorm2D);
		std::int32_t opti = indexOf(node->m_nextOpti);
		x.xferInt(&opti);
	}
	std::int32_t closest = indexOf(m_closest), ahead = indexOf(m_lastAheadNode);
	x.xferInt(&closest);
	x.xferInt(&ahead);
}

// the load: every node made and checked on its own first (its layer, its optimised link forward along the nodes, the follower's two links within the
// nodes); only then are the nodes linked into this path, which must be empty
void Path::xferLoad(Xfer &x)
{
	if (m_path)
	{
		throw XferException("path: loading into a path that has nodes");
	}
	bool optimized = false, blocked = false;
	float t = 0.0f;
	Coord3D lastAhead{ 0.0f, 0.0f, 0.0f };
	x.xferBool(&optimized);
	x.xferBool(&blocked);
	x.xferReal(&t);
	AIXfer::coord(x, lastAhead);
	std::uint32_t n = 0;
	x.xferCount(&n, 37, sizeof(PathNode));
	std::vector<std::unique_ptr<PathNode>> nodes;
	std::vector<std::int32_t> links;
	for (std::uint32_t at = 0; at < n; ++at)
	{
		std::unique_ptr<PathNode> node = std::make_unique<PathNode>();
		AIXfer::coord(x, node->m_pos);
		AIXfer::enumIn(x, node->m_layer, LAYER_INVALID, LAYER_LAST, "path: a node's layer");
		AIXfer::inRange(node->m_layer, LAYER_INVALID, LAYER_LAST, "path: a node's layer");
		x.xferBool(&node->m_canOptimize);
		x.xferInt(&node->m_waypointID);
		x.xferReal(&node->m_nextOptiDist2D);
		AIXfer::coord2(x, node->m_nextOptiDirNorm2D);
		std::int32_t opti = -1;
		x.xferInt(&opti);
		// the optimised chain runs forward along the nodes (ZH Path::optimize links a node to a later one): a link to itself or back would make the chain a
		// cycle that a walk never leaves (review r2)
		if (opti != -1 && (opti <= (std::int32_t)at || opti >= (std::int32_t)n))
		{
			throw XferException("path: node " + std::to_string(at) + "'s optimised link to node " + std::to_string(opti) + " of " + std::to_string(n) +
				" does not run forward");
		}
		links.push_back(opti);
		nodes.push_back(std::move(node));
	}
	std::int32_t follow[2] = { -1, -1 };
	for (std::int32_t &i : follow)
	{
		x.xferInt(&i);
		if (i < -1 || i >= (std::int32_t)n)
		{
			throw XferException("path: a follower link to node " + std::to_string(i) + " of " + std::to_string(n));
		}
	}
	// valid: the nodes are linked in order
	for (std::uint32_t at = 0; at < n; ++at)
	{
		PathNode *node = nodes[at].get();
		node->m_nextOpti = links[at] < 0 ? nullptr : nodes[(size_t)links[at]].get();
		node->m_prev = at ? nodes[at - 1].get() : nullptr;
		node->m_next = at + 1 < n ? nodes[at + 1].get() : nullptr;
	}
	m_isOptimized = optimized;
	m_blockedByAlly = blocked;
	m_t = t;
	m_lastAhead = lastAhead;
	m_closest = follow[0] < 0 ? nullptr : nodes[(size_t)follow[0]].get();
	m_lastAheadNode = follow[1] < 0 ? nullptr : nodes[(size_t)follow[1]].get();
	m_path = n ? nodes.front().get() : nullptr;
	m_pathTail = n ? nodes.back().get() : nullptr;
	for (std::unique_ptr<PathNode> &node : nodes)
	{
		node.release(); // the path owns its nodes (Path::~Path deletes the chain)
	}
}
