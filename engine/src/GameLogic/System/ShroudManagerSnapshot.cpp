// OpenBFME. GPL-3.0.
//
// The obfme:Shroud block (lane SAVE-4c, docs/SAVES.md section 8): TheShroudManager's state. OpenBFME native layout (stop S-4220): retail registers
// CHUNK_Shroud in its lists 0 and 1 (GameState::init RW 0x6DF73E); the implementation's xfers are ShroudManagerImpl RW 0xB51A50 (window (1, 1): u32,
// region3d, two reals, two ints, u32, int, bool: the grid), which then saves every CELL through RW 0xB52D50 ((1, 2): u16 and two calls; review r2:
// the element is the cell, docs/SAVES-RETAIL.md section 6). This port's records carry more than retail's (the channel looks, the coverage, the
// cached statuses, the ground reference), so the block is OpenBFME's own, in this order: the settings and the grid, every cell's per-player looker count
// and three channels (per player and channel a plane over the cells, run-length coded), the unlook queue in order (RW + 0x3C deque), the records by slot (a free
// slot as not in use), the free slot stack, the registration list (RW + 0x30) and the dirty list (RW markDirty's list), all as slots. Derived and
// rebuilt: each cell's covering-record list (the invalidation of RW + 0 walks it, order free: every covering record's status is cleared) from the records'
// coverage. Client state is not saved: the local player and its callback (set by the game's client at a load), the display levels and switch.

#include "GameLogic/System/ShroudManager.h"

#include "Common/Xfer.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <set>

namespace
{
void xferRadii(Xfer &x, ShroudManager::Radii &r)
{
	x.xferInt(&r.forward);
	x.xferInt(&r.side);
	x.xferInt(&r.rear);
	if (x.isLoading())
	{
		// a radius in cells, -1 for none (RW 0xB50C90); a mask of the 20 players
		AIXfer::inRange(r.forward, -1, 0x7FFFFFFF, "shroud: a look's forward radius");
		AIXfer::inRange(r.side, -1, 0x7FFFFFFF, "shroud: a look's side radius");
		AIXfer::inRange(r.rear, -1, 0x7FFFFFFF, "shroud: a look's rear radius");
	}
}

void checkMask(std::uint32_t mask)
{
	if (mask >> ShroudManager::kMaxPlayers)
	{
		throw XferException("shroud: a look's player mask 0x" + std::to_string(mask) + " names players beyond the " +
			std::to_string(ShroudManager::kMaxPlayers));
	}
}

void xferU64(Xfer &x, unsigned long long &v)
{
	std::uint64_t w = v;
	x.xferUnsignedInt64(&w);
	v = w;
}
} // namespace

void ShroudManager::xferState(Xfer &x)
{
	// lane VISIONDEF-1: version 2 drops the records' vision ground reference (now the object's)
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	x.xferUnsignedInt(&m_counter);
	x.xferUnsignedInt(&m_unlookPersist);
	x.xferReal(&m_loX);
	x.xferReal(&m_loY);
	x.xferReal(&m_hiX);
	x.xferReal(&m_hiY);
	x.xferReal(&m_cellSize);
	x.xferReal(&m_cellSizeInv);
	const int wantX = m_countX, wantY = m_countY; // a load: the grid of the game being loaded (ShroudManager::xfer sets it on the fresh manager)
	x.xferInt(&m_countX);
	x.xferInt(&m_countY);
	if (loading && (m_countX != wantX || m_countY != wantY))
	{
		// the grid comes from the map (RW 0xB51D90): a snapshot of another grid is another game's, and its cell count is never allocated
		throw XferException("shroud: the snapshot's grid is " + std::to_string(m_countX) + " x " + std::to_string(m_countY) + " cells, this game's " +
			std::to_string(wantX) + " x " + std::to_string(wantY));
	}
	// the cells: per player, its looker counts, then each of its three channels, every plane over the cells in runs of equal values (u32 run length, then
	// the value): the players without a slot are one run each
	std::uint32_t cells = (std::uint32_t)m_cells.size();
	x.xferUnsignedInt(&cells);
	if (cells != (std::uint32_t)m_countX * (std::uint32_t)m_countY)
	{
		throw XferException("shroud: " + std::to_string(cells) + " cells for a grid of " + std::to_string(m_countX) + " x " + std::to_string(m_countY));
	}
	if (loading)
	{
		m_cells.assign(cells, Cell());
	}
	auto plane = [&](auto get) {
		std::uint32_t at = 0;
		while (at < cells)
		{
			std::uint32_t run = 1;
			if (!loading)
			{
				while (at + run < cells && *get(m_cells[at + run]) == *get(m_cells[at]))
				{
					++run;
				}
			}
			x.xferUnsignedInt(&run);
			if (run == 0 || run > cells - at)
			{
				throw XferException("shroud: a run of " + std::to_string(run) + " cells at cell " + std::to_string(at) + " of " + std::to_string(cells));
			}
			auto value = *get(m_cells[at]);
			x.xferUser(&value, sizeof(value));
			for (std::uint32_t i = 0; loading && i < run; ++i)
			{
				*get(m_cells[at + i]) = value;
			}
			at += run;
		}
	};
	for (size_t p = 0; p < (size_t)kMaxPlayers; ++p)
	{
		plane([p](Cell &c) { return &c.lookers[p]; });
		for (size_t k = 0; k < (size_t)kChannels; ++k)
		{
			plane([p, k](Cell &c) { return &c.channels[p][k]; });
		}
	}
	// the unlook queue
	std::uint32_t pending = (std::uint32_t)m_pending.size();
	x.xferCount(&pending, 32, sizeof(Pending));
	if (loading)
	{
		m_pending.assign(pending, Pending{});
	}
	for (Pending &p : m_pending)
	{
		x.xferUnsignedInt(&p.due);
		x.xferInt(&p.x);
		x.xferInt(&p.y);
		xferRadii(x, p.radii);
		x.xferReal(&p.facing);
		x.xferUnsignedInt(&p.mask);
		checkMask(p.mask);
	}
	// the records by slot
	std::uint32_t slots = (std::uint32_t)m_records.size();
	x.xferCount(&slots, 1, sizeof(Record)); // a free slot is its in-use byte
	if (loading)
	{
		m_records.assign(slots, Record());
	}
	for (Record &r : m_records)
	{
		x.xferBool(&r.inUse);
		if (!r.inUse)
		{
			continue;
		}
		x.xferObjectID(&r.id);
		x.xferBool(&r.dirty);
		x.xferBool(&r.force);
		x.xferInt(&r.lookX);
		x.xferInt(&r.lookY);
		xferRadii(x, r.radii);
		x.xferUnsignedInt(&r.mask);
		checkMask(r.mask);
		x.xferReal(&r.facing);
		for (int k = 0; k < kChannels; ++k)
		{
			x.xferInt(&r.channelRadius[(size_t)k]);
			x.xferUnsignedInt(&r.channelMask[(size_t)k]);
			checkMask(r.channelMask[(size_t)k]); // lane SAVE-4e (closed sets): a channel's player mask, as a look's
			x.xferInt(&r.channelAmount[(size_t)k]);
		}
		x.xferVectorSize(r.covered, 4);
		std::set<int> coveredOnce;
		for (int &index : r.covered)
		{
			x.xferInt(&index);
			if (loading && (index < 0 || (std::uint32_t)index >= cells))
			{
				throw XferException("shroud: object " + std::to_string(r.id) + " covers the cell " + std::to_string(index) + " of " + std::to_string(cells));
			}
			if (loading && !coveredOnce.insert(index).second)
			{
				throw XferException("shroud: object " + std::to_string(r.id) + " covers the cell " + std::to_string(index) + " twice"); // RW 0xB4EB70 adds a cell once
			}
		}
		x.xferUser(r.status.data(), r.status.size());
		x.xferUser(r.previous.data(), r.previous.size());
		x.xferUser(r.seen.data(), r.seen.size());
		for (int p = 0; loading && p < kMaxPlayers; ++p)
		{
			AIXfer::inRange(r.status[(size_t)p], 0, OBJECTSHROUD_SHROUDED, "shroud: a record's status");
			AIXfer::inRange(r.previous[(size_t)p], 0, OBJECTSHROUD_SHROUDED, "shroud: a record's previous status");
			AIXfer::inRange(r.seen[(size_t)p], 0, 1, "shroud: a record's seen flag");
		}
		if (v.current < 2)
		{
			// version 1 kept the vision ground reference here (valid flag, integer position, height); it is the object's now (lane VISIONDEF-1:
			// Object + 0x1C4, Object::xfer version 4): read and dropped
			bool groundValid = false;
			std::int32_t groundX = 0, groundY = 0;
			float groundRef = 0.0f;
			x.xferBool(&groundValid);
			x.xferInt(&groundX);
			x.xferInt(&groundY);
			x.xferReal(&groundRef);
		}
	}
	// the slot lists
	auto xferSlots = [&](std::vector<std::uint32_t> &list, const char *what) {
		x.xferVectorSize(list, 4);
		for (std::uint32_t &slot : list)
		{
			x.xferUnsignedInt(&slot);
			if (slot >= slots)
			{
				throw XferException(std::string("shroud: the ") + what + " names the slot " + std::to_string(slot) + " of " + std::to_string(slots));
			}
		}
	};
	xferSlots(m_freeSlots, "free slot stack");
	xferSlots(m_registration, "registration list");
	xferSlots(m_dirty, "dirty list");
	xferU64(x, m_stats.looks);
	xferU64(x, m_stats.unlooks);
	xferU64(x, m_stats.polygonLooks);
	xferU64(x, m_stats.edges);
	if (!loading)
	{
		return;
	}
	// one record per object (the id -> slot index is built by the post-process, once the ids are known to be live objects), and the cells' covering lists
	// follow from the records
	std::set<ObjectID> ids;
	for (std::uint32_t slot = 0; slot < slots; ++slot)
	{
		Record &r = m_records[slot];
		if (!r.inUse)
		{
			continue;
		}
		if (!ids.insert(r.id).second)
		{
			throw XferException("shroud: object " + std::to_string(r.id) + " has two records");
		}
		for (int index : r.covered)
		{
			m_cells[(size_t)index].coveredBy.push_back(slot);
		}
	}
	// the slot lists: the free slots are the records not in use, the registration list the ones in use, each once; the dirty list the dirty records, once
	std::vector<char> freeSeen(slots, 0), regSeen(slots, 0), dirtySeen(slots, 0);
	for (std::uint32_t slot : m_freeSlots)
	{
		if (m_records[slot].inUse || freeSeen[slot]++)
		{
			throw XferException("shroud: the free slot stack names the slot " + std::to_string(slot) + (m_records[slot].inUse ? ", which is in use" : " twice"));
		}
	}
	for (std::uint32_t slot : m_registration)
	{
		if (!m_records[slot].inUse || regSeen[slot]++)
		{
			throw XferException("shroud: the registration list names the slot " + std::to_string(slot) + (m_records[slot].inUse ? " twice" : ", which is free"));
		}
	}
	for (std::uint32_t slot : m_dirty)
	{
		if (!m_records[slot].inUse || !m_records[slot].dirty || dirtySeen[slot]++)
		{
			throw XferException("shroud: the dirty list names the slot " + std::to_string(slot) + ", which is not a dirty record or is listed twice");
		}
	}
	for (std::uint32_t slot = 0; slot < slots; ++slot)
	{
		const Record &r = m_records[slot];
		if (r.inUse ? !regSeen[slot] || (r.dirty && !dirtySeen[slot]) : !freeSeen[slot])
		{
			throw XferException("shroud: the slot " + std::to_string(slot) + " is in none of the lists its record needs");
		}
	}
}

void ShroudManager::xfer(Xfer &x)
{
	if (!x.isLoading())
	{
		xferState(x);
		return;
	}
	std::unique_ptr<ShroudManager> loaded = std::make_unique<ShroudManager>(m_logic);
	loaded->m_countX = m_countX; // the grid the snapshot must have
	loaded->m_countY = m_countY;
	loaded->xferState(x);
	m_pendingLoad = std::move(loaded);
}

// rule 4: the records' objects are checked before anything changes; then the loaded state replaces this manager's (the client side, the object source and
// the attachment stay this manager's)
void ShroudManager::loadPostProcess()
{
	if (!m_pendingLoad)
	{
		return;
	}
	std::unique_ptr<ShroudManager> p = std::move(m_pendingLoad);
	for (const Record &r : p->m_records)
	{
		if (r.inUse && !m_logic.findObjectByID(r.id))
		{
			throw XferException("shroud: the record of object " + std::to_string(r.id) + " has no object in the loaded game");
		}
	}
	m_counter = p->m_counter;
	m_unlookPersist = p->m_unlookPersist;
	m_loX = p->m_loX;
	m_loY = p->m_loY;
	m_hiX = p->m_hiX;
	m_hiY = p->m_hiY;
	m_cellSize = p->m_cellSize;
	m_cellSizeInv = p->m_cellSizeInv;
	m_countX = p->m_countX;
	m_countY = p->m_countY;
	m_cells = std::move(p->m_cells);
	m_pending = std::move(p->m_pending);
	m_records = std::move(p->m_records);
	m_slotById.clear();
	for (std::uint32_t slot = 0; slot < (std::uint32_t)m_records.size(); ++slot)
	{
		const Record &r = m_records[slot];
		if (r.inUse)
		{
			if (r.id >= m_slotById.size())
			{
				m_slotById.resize((size_t)r.id + 1, 0);
			}
			m_slotById[r.id] = slot + 1;
		}
	}
	m_dirty = std::move(p->m_dirty);
	m_registration = std::move(p->m_registration);
	m_freeSlots = std::move(p->m_freeSlots);
	m_stats = p->m_stats;
}
