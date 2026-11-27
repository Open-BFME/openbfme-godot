// OpenBFME. GPL-3.0.
// See GameLogic/System/TerrainResourceManager.h for the target facts and the addresses.

#include "GameLogic/System/TerrainResourceManager.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <stdexcept>

namespace
{
const float kHalf = 0.5f;  // RW 0xBD869C
const float kOne = 1.0f;   // RW 0xBD1908

// x87 floor then `fstp dword`, `fistp dword` (RW 0x75BA9D .. 0x75BAB7): the cell index of a world coordinate
int floorToCell(float value)
{
	const double floored = NumericState::floorD((double)value);
	return NumericState::fistp32((double)NumericState::fstpDword(floored));
}
} // namespace

TerrainResourceManager::TerrainResourceManager(GameLogic &logic)
	: m_logic(logic)
{
}

void TerrainResourceManager::reset()
{
	// RW 0x75BD62: the largest radius and the extents are zeroed and the grid freed
	m_cells.clear();
	m_cells.shrink_to_fit();
	m_width = m_height = 0;
	m_cellSize = 0.0f;
	m_originX = m_originY = m_originZ = 0.0f;
	m_extentX = m_extentY = m_extentZ = 0.0f;
	m_maxRadius = 0.0f;
	m_blockedCount = 0;
	m_blockedHash = 0;
}

void TerrainResourceManager::clearAll()
{
	reset();
	m_claimants.clear();
}

void TerrainResourceManager::init(const TerrainResourceTerrain &terrain, float cellSize)
{
	if (!(cellSize > 0.0f))
	{
		throw std::logic_error("TerrainResourceManager::init: the cell size (GameData TerrainResourceCellSize) must be positive");
	}
	float loX = 0, loY = 0, hiX = 0, hiY = 0;
	terrain.getExtent(loX, loY, hiX, hiY);
	// RW 0x75BE82 .. 0x75BEC6: a region narrower than 1.0 is widened to 1.0 (SSE compares and adds)
	if (NumericState::sseSub(hiX, loX) < kOne)
	{
		hiX = NumericState::sseAdd(loX, kOne);
	}
	if (NumericState::sseSub(hiY, loY) < kOne)
	{
		hiY = NumericState::sseAdd(loY, kOne);
	}
	// RW 0x75BEC6 .. 0x75BF33: inverse = 1.0 / cellSize (SSE divss); width = ceil((hiX - loX) * inverse) as SSE sub / mul, then x87 ceil, store, fistp;
	// height = the same with x87 fsub / fmul at 24 bits
	const float inverse = NumericState::sseDiv(kOne, cellSize);
	const float widthF = NumericState::sseMul(NumericState::sseSub(hiX, loX), inverse);
	int width = NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD((double)widthF)));
	const double heightW = NumericState::pc24MulW(NumericState::pc24SubW((double)hiY, (double)loY), (double)inverse);
	int height = NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD(heightW)));
	if (width < 1)
	{
		width = 1;
	}
	if (height < 1)
	{
		height = 1;
	}
	// the arithmetic is retail's; the allocation must be sane (a mod's absurd extent is an error, not a bad_alloc)
	if ((long long)width * (long long)height > 64ll * 1024 * 1024)
	{
		throw std::logic_error("TerrainResourceManager::init: the grid would have " + std::to_string((long long)width * (long long)height) + " cells");
	}
	m_cells.assign((size_t)width * (size_t)height, Cell());
	m_width = width;
	m_height = height;
	m_cellSize = cellSize;
	m_originX = loX;
	m_originY = loY;
	m_originZ = 0.0f;
	m_extentX = hiX;
	m_extentY = hiY;
	m_extentZ = 0.0f;
	classifyBlockedCells(terrain);
	// RW 0x75BFE5 .. 0x75C01D: every claimant whose object still exists is applied again, in list order, with its own flag
	const std::vector<Claimant> snapshot = m_claimants;
	for (const Claimant &c : snapshot)
	{
		if (Object *o = m_logic.findObjectByID(c.id))
		{
			applyClaim(*o, c.radius, false, c.complete);
		}
	}
}

void TerrainResourceManager::classifyBlockedCells(const TerrainResourceTerrain &terrain)
{
	// RW 0x75B7BB: the cell types 1, 7, 2 and 5 of the pathfinder's ground layer block a cell for good
	m_blockedCount = 0;
	StateHasher blocked;
	for (int j = 0; j < m_height; ++j)
	{
		const float rowCenter = NumericState::sseAdd(NumericState::sseFromInt32(j), kHalf);
		for (int i = 0; i < m_width; ++i)
		{
			const float px = NumericState::sseMul(NumericState::sseAdd(NumericState::sseFromInt32(i), kHalf), m_cellSize);
			const float py = NumericState::sseMul(m_cellSize, rowCenter);
			int type = 2;
			if (terrain.cellTypeAt(px, py, type) && (type == 1 || type == 7 || type == 2 || type == 5))
			{
				Cell &c = at(i, j);
				if (c.state != CELL_BLOCKED)
				{
					c.state = CELL_BLOCKED;
					++m_blockedCount;
					blocked.addU32((std::uint32_t)((size_t)j * (size_t)m_width + (size_t)i));
				}
			}
		}
	}
	m_blockedHash = blocked.value();
}

TerrainResourceManager::Disc TerrainResourceManager::makeDisc(float x, float y, float radius) const
{
	// RW 0x75BA76 .. 0x75BB03
	Disc d;
	d.cx = floorToCell(NumericState::sseAdd(NumericState::sseDiv(NumericState::sseSub(x, m_originX), m_cellSize), kHalf));
	d.cy = floorToCell(NumericState::sseAdd(NumericState::sseDiv(NumericState::sseSub(y, m_originY), m_cellSize), kHalf));
	const double ratio = NumericState::pc24DivW((double)radius, (double)m_cellSize); // fld radius; fdiv cell
	d.r = NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD(ratio)));
	return d;
}

template <class Visitor>
void TerrainResourceManager::forEachInDisc(const Disc &d, Visitor &&visit) const
{
	// RW 0x75B4BA: the state of the retail loop in the registers (x counter [ebp-4], x1 ebx, x0 [ebp+8], y edi, error esi); the rows are visited from
	// the middle outward by RW 0x75B3AE (x0 .. x1 at one y)
	auto row = [&](int a, int b, int yy) {
		for (int x = a; x <= b; ++x)
		{
			visit(x, yy);
		}
	};
	int xi = 0;
	int x1 = d.cx;
	int x0 = d.cx;
	int y = d.r;
	int e = 2 - 2 * d.r;
	for (;;)
	{
		if (e + y > 0) // RW 0x75B4DA: jle 0x75B52B skips the rows
		{
			if (y == 0 && d.r == 1) // RW 0x75B4E1 .. 0x75B4F2
			{
				++xi;
				++x1;
				--x0;
			}
			row(x0, x1, d.cy + y);
			if (y == 0)
			{
				return;
			}
			row(x0, x1, d.cy - y);
			--y;
			e += 1 - 2 * y;
		}
		if (xi <= e) // RW 0x75B52B: jle 0x75B4DA
		{
			continue;
		}
		++xi; // RW 0x75B530: widen
		++x1;
		--x0;
		e = e + xi * 2 + 1;
	}
}

bool TerrainResourceManager::usable(int x, int y, std::uint32_t mask, ObjectID objectId, bool complete)
{
	// RW 0x75C02D with the filter -1 (the logic form): the grid exists, the coordinates are in it, then the state decides
	if (!inGrid(x, y))
	{
		return false;
	}
	Cell &c = at(x, y);
	switch (c.state)
	{
	case CELL_FREE:
		return true;
	case CELL_BLOCKED:
		return false;
	case CELL_SHARED:
		return true;
	case CELL_OWNED:
	{
		if (c.claims.empty())
		{
			c.state = CELL_FREE; // RW 0x75C13B: an owned cell without an entry is free again
			return true;
		}
		if (!complete)
		{
			return false; // RW 0x75C155
		}
		const ClaimEntry &first = c.claims.front();
		if (first.playerIndex == -1 || mask == kHighPriorityMask || mask == first.objectId || objectId == first.objectId)
		{
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

void TerrainResourceManager::visitClaim(int x, int y, std::uint32_t mask, ObjectID objectId, bool complete, DiscCounts &counts)
{
	// RW 0x75C5D2
	if (m_cells.empty())
	{
		return;
	}
	++counts.total;
	if (!usable(x, y, mask, objectId, complete))
	{
		return;
	}
	Object *obj = m_logic.findObjectByID(objectId);
	if (!obj)
	{
		return;
	}
	Player *player = obj->getControllingPlayer();
	if (!player)
	{
		return;
	}
	const std::int32_t index = player->getPlayerIndex();
	Cell &c = at(x, y);
	switch (c.state)
	{
	case CELL_FREE:
		c.state = complete ? CELL_OWNED : CELL_SHARED;
		c.claims.clear();
		c.claims.push_back({ index, objectId });
		++counts.claimed;
		return;
	case CELL_SHARED:
		if (complete)
		{
			c.claims.clear();
			c.claims.push_back({ index, objectId });
			c.state = CELL_OWNED;
			++counts.claimed;
			return;
		}
		for (const ClaimEntry &e : c.claims)
		{
			if (e.playerIndex == index)
			{
				return; // this player already shares the cell: nothing changes and nothing is counted
			}
		}
		c.claims.push_back({ index, objectId });
		++counts.claimed;
		return;
	case CELL_OWNED:
		if (!complete)
		{
			return;
		}
		if (!c.claims.empty())
		{
			c.claims.front() = { index, objectId };
		}
		else
		{
			c.claims.push_back({ index, objectId });
		}
		++counts.claimed;
		return;
	default:
		return;
	}
}

void TerrainResourceManager::visitRelease(int x, int y, ObjectID objectId)
{
	// RW 0x75C461
	if (!inGrid(x, y))
	{
		return;
	}
	Cell &c = at(x, y);
	switch (c.state)
	{
	case CELL_SHARED:
	{
		bool allRemoved = true;
		for (ClaimEntry &e : c.claims)
		{
			if (e.objectId == objectId)
			{
				e.playerIndex = -1; // the retail scan stops here: the entries after this one are not looked at
				e.objectId = 0;
				break;
			}
			if (e.playerIndex != -1)
			{
				allRemoved = false;
			}
		}
		if (allRemoved)
		{
			c.state = CELL_FREE;
			c.claims.clear();
		}
		return;
	}
	case CELL_OWNED:
		if (c.claims.empty() || c.claims.front().objectId == objectId)
		{
			c.state = CELL_FREE; // the entries are left behind: a free cell's list is cleared by the next claim (RW 0x75C5D2 state 0)
		}
		return;
	default:
		return;
	}
}

void TerrainResourceManager::applyClaim(Object &obj, float radius, bool highPriority, bool complete)
{
	// RW 0x75BA1D
	TerrainResourceBehavior *module = dynamic_cast<TerrainResourceBehavior *>(obj.findModule("TerrainResourceBehavior"));
	if (!module)
	{
		return;
	}
	if (m_cells.empty())
	{
		// no grid yet (retail always has one, built with a placeholder extent at the game's start; the real one re-applies every claimant, init()): the claim is recorded
		// by the caller's list and the largest radius, the share is computed when the grid exists
		if (radius > m_maxRadius)
		{
			m_maxRadius = radius;
		}
		return;
	}
	const Coord3D &pos = *obj.getPosition();
	const Disc disc = makeDisc(pos.x, pos.y, radius);
	if (disc.r < 0)
	{
		// the retail loop never ends for a negative radius; a mod's data error is reported and the claim ignored
		m_logic.reportError("TerrainResourceBehavior of " + obj.getTemplate()->getName() + ": a negative Radius would hang the claim loop of the retail game");
		return;
	}
	const std::uint32_t mask = highPriority ? kHighPriorityMask : 0u;
	DiscCounts counts;
	const ObjectID id = obj.getID();
	forEachInDisc(disc, [&](int x, int y) { visitClaim(x, y, mask, id, complete, counts); });
	// RW 0x75BB3E .. 0x75BB4D: the share is claimed / total (x87 fild, fidiv; 0.0 for an empty disc), stored as float
	float share = 0.0f;
	if (counts.total != 0)
	{
		share = NumericState::fstpDword(NumericState::pc24DivW((double)counts.claimed, (double)counts.total));
	}
	module->setClaimShare(share);
	if (radius > m_maxRadius) // RW 0x75BB52: comiss radius, largest; jbe
	{
		m_maxRadius = radius;
	}
	if (highPriority)
	{
		reapplyNeighbours(obj, NumericState::sseAdd(m_maxRadius, radius));
	}
}

void TerrainResourceManager::release(Object &obj, float radius)
{
	// RW 0x75BB8F
	TerrainResourceBehavior *module = dynamic_cast<TerrainResourceBehavior *>(obj.findModule("TerrainResourceBehavior"));
	if (!module)
	{
		return;
	}
	if (m_cells.empty())
	{
		module->setClaimShare(0.0f);
		return;
	}
	const Coord3D &pos = *obj.getPosition();
	const Disc disc = makeDisc(pos.x, pos.y, radius);
	if (disc.r < 0)
	{
		return;
	}
	const ObjectID id = obj.getID();
	forEachInDisc(disc, [&](int x, int y) { visitRelease(x, y, id); });
	module->setClaimShare(0.0f); // fldz
	reapplyNeighbours(obj, NumericState::sseAdd(m_maxRadius, radius));
}

void TerrainResourceManager::reapplyNeighbours(const Object &center, float reach)
{
	// RW 0x75B923: every claimant (the object itself included while it is in the list) within `reach` of the object is applied again with its own flag
	const float reach2 = NumericState::sseMul(reach, reach);
	const std::vector<Claimant> snapshot = m_claimants; // applyClaim does not edit the list, the copy keeps the walk defined anyway
	const Coord3D &cp = *center.getPosition();
	for (const Claimant &c : snapshot)
	{
		Object *o = m_logic.findObjectByID(c.id);
		if (!o)
		{
			continue;
		}
		const Coord3D &op = *o->getPosition();
		// RW 0x66137C: fld a.x; fsub b.x; fld a.y; fsub b.y; squares; sum: every step at 24 bits
		const double dx = NumericState::pc24SubW((double)cp.x, (double)op.x);
		const double dy = NumericState::pc24SubW((double)cp.y, (double)op.y);
		const double d2 = NumericState::pc24AddW(NumericState::pc24MulW(dy, dy), NumericState::pc24MulW(dx, dx));
		if ((double)reach2 < d2) // fcompi; jb: skipped when reach^2 < distance^2
		{
			continue;
		}
		applyClaim(*o, c.radius, false, c.complete);
	}
}

TerrainResourceManager::Claimant *TerrainResourceManager::findClaimant(ObjectID id)
{
	for (Claimant &c : m_claimants)
	{
		if (c.id == id)
		{
			return &c;
		}
	}
	return nullptr;
}

void TerrainResourceManager::claim(Object &obj, float radius, bool visible, bool highPriority)
{
	// RW 0x75BDC2
	Claimant rec;
	rec.id = obj.getID();
	rec.radius = radius;
	rec.visible = visible;
	rec.complete = false;
	if (highPriority)
	{
		m_claimants.insert(m_claimants.begin(), rec); // RW 0x75B756 push_front
	}
	else
	{
		m_claimants.push_back(rec); // RW 0x8214A7 push_back
	}
	applyClaim(obj, radius, highPriority, false);
}

void TerrainResourceManager::unclaim(Object &obj, float radius)
{
	// RW 0x75BE1C
	for (size_t i = 0; i < m_claimants.size(); ++i)
	{
		if (m_claimants[i].id == obj.getID())
		{
			m_claimants.erase(m_claimants.begin() + (std::ptrdiff_t)i);
			release(obj, radius);
			return;
		}
	}
}

void TerrainResourceManager::onBuildComplete(Object &obj)
{
	// RW 0x75BCD9: for each record of this object (the list may hold it once): apply with flag 1, then set the record's flag
	for (size_t i = 0; i < m_claimants.size(); ++i)
	{
		if (m_claimants[i].id == obj.getID())
		{
			const float radius = m_claimants[i].radius;
			applyClaim(obj, radius, false, true);
			m_claimants[i].complete = true;
		}
	}
}

bool TerrainResourceManager::previewUsable(int x, int y, int filter) const
{
	// RW 0x75C02D as the preview's visitor RW 0x75C523 calls it: mask = the HighPriority mask, object 0, complete 0, filter = the player. The mask and the
	// object are read only for a complete claimant, so with complete 0 they do not change the answer.
	if (!inGrid(x, y))
	{
		return false;
	}
	const ShroudManager *shroud = m_logic.shroud();
	CellShroudStatus cellShroud = CELLSHROUD_CLEAR;
	if (filter >= 0 && shroud)
	{
		// RW 0x75C02D: the cell's centre ((x + 0.5) * cell + origin) asked of the shroud for the filter player (RW 0xB4D9A0 -> 0xB4FB20);
		// a shrouded cell (2) is not usable. A logic without a shroud manager (unit rigs) answers every cell clear
		const float wx = NumericState::sseAdd(NumericState::sseMul(NumericState::sseAdd((float)x, kHalf), m_cellSize), m_originX);
		const float wy = NumericState::sseAdd(NumericState::sseMul(NumericState::sseAdd((float)y, kHalf), m_cellSize), m_originY);
		cellShroud = shroud->getStatusAt(filter, wx, wy);
		if (cellShroud == CELLSHROUD_SHROUDED)
		{
			return false;
		}
	}
	const Cell &c = m_cells[(size_t)y * (size_t)m_width + (size_t)x];
	switch (c.state)
	{
	case CELL_FREE:
		return true;
	case CELL_SHARED:
		// RW 0x75C02D, state 2: a clear cell the filter player already shares is not claimable for it again
		if (filter != -1 && cellShroud == CELLSHROUD_CLEAR)
		{
			for (const ClaimEntry &e : c.claims)
			{
				if (e.playerIndex == filter)
				{
					return false;
				}
			}
		}
		return true;
	case CELL_OWNED:
		if (cellShroud != CELLSHROUD_CLEAR)
		{
			return true; // RW 0x75C02D, state 3: a fogged owned cell looks claimable
		}
		if (c.claims.empty())
		{
			return true; // RW 0x75C13B also sets the state to free: not written here (see the file comment)
		}
		if (filter >= 0)
		{
			const Object *owner = m_logic.findObjectByID(c.claims.front().objectId);
			if (!owner)
			{
				return true; // RW 0x75C02D also erases the entries and frees the cell: not written here
			}
			// RW 0x75C02D: the owner's shroud status for the filter player (Object RW 0x68D8F7 -> RW 0xB4E890) fogged (3) or shrouded (4): it looks free.
			// clientObjectStatus answers it without the cached status write (SMOOTH-1)
			const ObjectShroudStatus s = shroud ? shroud->clientObjectStatus(*owner, filter) : OBJECTSHROUD_INVALID;
			if (s == OBJECTSHROUD_FOGGED || s == OBJECTSHROUD_SHROUDED)
			{
				return true;
			}
		}
		return false; // complete 0: an owned cell is not claimable
	default:
		return false;
	}
}

float TerrainResourceManager::previewShare(float x, float y, float radius, bool highPriority, int playerIndex) const
{
	// RW 0x75B561 with the counting visitor RW 0xC2BA38 (0x75C523, made by RW 0x75B357: mask = highPriority ? 0x7FFFF43 : 0, the player, complete 0): the
	// same disc as a claim; every disc cell counts toward the total, the usable ones toward the claimed; nothing written
	(void)highPriority; // the mask matters only for a complete claimant (previewUsable)
	if (m_cells.empty())
	{
		return 0.0f;
	}
	const Disc disc = makeDisc(x, y, radius);
	if (disc.r < 0)
	{
		return 0.0f;
	}
	DiscCounts counts;
	forEachInDisc(disc, [&](int cx, int cy) {
		++counts.total;
		if (previewUsable(cx, cy, playerIndex))
		{
			++counts.claimed;
		}
	});
	return counts.total == 0 ? 0.0f : NumericState::fstpDword(NumericState::pc24DivW((double)counts.claimed, (double)counts.total));
}

void TerrainResourceManager::crc(StateHasher &h) const
{
	h.addI32(m_width);
	h.addI32(m_height);
	h.addFloat(m_cellSize);
	h.addFloat(m_originX);
	h.addFloat(m_originY);
	h.addFloat(m_originZ);
	h.addFloat(m_extentX);
	h.addFloat(m_extentY);
	h.addFloat(m_extentZ);
	h.addFloat(m_maxRadius);
	h.addU32((std::uint32_t)m_blockedCount);
	h.addU32(m_blockedHash);
	h.addU32((std::uint32_t)m_claimants.size());
	for (const Claimant &c : m_claimants)
	{
		h.addU32(c.id);
		h.addFloat(c.radius);
		h.addBool(c.visible);
		h.addBool(c.complete);
	}
	// the cells that are not free and not blocked, in index order (blocked cells never change: m_blockedHash covers them)
	std::uint32_t used = 0;
	for (const Cell &c : m_cells)
	{
		if (c.state == CELL_SHARED || c.state == CELL_OWNED || !c.claims.empty())
		{
			++used;
		}
	}
	h.addU32(used);
	for (size_t i = 0; i < m_cells.size(); ++i)
	{
		const Cell &c = m_cells[i];
		if (c.state == CELL_SHARED || c.state == CELL_OWNED || !c.claims.empty())
		{
			h.addU32((std::uint32_t)i);
			h.addI32(c.state);
			h.addU32((std::uint32_t)c.claims.size());
			for (const ClaimEntry &e : c.claims)
			{
				h.addI32(e.playerIndex);
				h.addU32(e.objectId);
			}
		}
	}
}

// lane SAVE-4d. TARGET FACTS (RW 0x75C7AE, tier B edited against BFME2's TerrainResourceManager.cpp (DoXfer); read from the RotWK disassembly): version (1, 3);
// the claimant list (+ 0x14): an int count, then per node its object id (XferObjectID, node + 8), its radius (real, + 0xC) and, from version 2, its
// visible flag (bool, + 0x10); the region (+ 0x1C, Region3D: the low corner then the high corner, six reals); the width (+ 0x34) and height (+ 0x38)
// ints; the cell size (+ 0x3C) and the largest radius (+ 0x18) reals; an int, width * height; then every cell (16 bytes, + 0x40): from version 3 its state
// (int, cell + 0xC) and its entry count (int), each entry its player index (int) and object id (XferObjectID); before version 3 one object id that loads
// as an OWNED cell of player -1. A load allocates the cells anew from the saved size.
// RETAIL QUIRK, NOT KEPT (S-4253): the node's complete flag (+ 0x11) is not in the layout, so a retail load leaves it uninitialised (the local record at
// RW 0x75C816 is pushed with that byte unset). The port keeps it: after the retail layout an OpenBFME trailer (its own version) holds every claimant's
// complete flag in list order.
// The load (SAVE-4a review r1 rules, SAVES.md section 8) reads into temporaries and checks before anything changes: the grid (region, size, cell size)
// and its blocked cells are the ones this game's START made (RW 0x75BE66 / 0x75B7BB); a claimant's id is not 0 and listed once (that the object exists
// is checked by finishLoad, after every block loaded, before the manager takes the staged state); a cell's state is one of the four, its entries' player indices -1 .. 19.
void TerrainResourceManager::xfer(Xfer &x)
{
	const bool loading = x.isLoading();
	if (loading)
	{
		m_pending.reset(); // an earlier load that never finished is dropped
	}
	XferVersion v{ 1, 3 };
	x.xferVersion(v);
	std::vector<Claimant> loadedClaimants;
	std::vector<Claimant> &claimants = loading ? loadedClaimants : m_claimants; // saving reads the members, loading fills temporaries
	x.xferVectorSize(claimants, v.current >= 2 ? 9 : 8);
	for (Claimant &c : claimants)
	{
		x.xferObjectID(&c.id);
		x.xferReal(&c.radius);
		if (v.current >= 2)
		{
			x.xferBool(&c.visible);
		}
	}
	float region[6] = {};
	std::int32_t width = 0, height = 0;
	float cellSize = 0.0f, maxRadius = 0.0f;
	if (!loading)
	{
		const float r[6] = { m_originX, m_originY, m_originZ, m_extentX, m_extentY, m_extentZ };
		std::copy(r, r + 6, region);
		width = m_width;
		height = m_height;
		cellSize = m_cellSize;
		maxRadius = m_maxRadius;
	}
	for (float &r : region)
	{
		x.xferReal(&r);
	}
	x.xferInt(&width);
	x.xferInt(&height);
	x.xferReal(&cellSize);
	x.xferReal(&maxRadius);
	if (loading)
	{
		const float mine[6] = { m_originX, m_originY, m_originZ, m_extentX, m_extentY, m_extentZ };
		if (width != m_width || height != m_height || !std::equal(region, region + 6, mine) || cellSize != m_cellSize)
		{
			throw XferException("terrain resource manager: the snapshot's grid is " + std::to_string(width) + " x " + std::to_string(height) +
				", this game's " + std::to_string(m_width) + " x " + std::to_string(m_height) + " (another map or data)");
		}
	}
	std::vector<Cell> loadedCells;
	std::vector<Cell> &cells = loading ? loadedCells : m_cells;
	x.xferVectorSize(cells, v.current >= 3 ? 8 : 4);
	if (loading && cells.size() != m_cells.size())
	{
		throw XferException("terrain resource manager: " + std::to_string(cells.size()) + " cells for a grid of " + std::to_string(m_cells.size()));
	}
	for (size_t i = 0; i < cells.size(); ++i)
	{
		Cell &c = cells[i];
		if (loading && v.current < 3)
		{
			ClaimEntry e;
			x.xferObjectID(&e.objectId);
			c.state = CELL_OWNED; // RW 0x75C9C6
			c.claims.assign(1, e);
		}
		else
		{
			std::int32_t state = loading ? 0 : c.state;
			x.xferInt(&state);
			x.xferVectorSize(c.claims, 8);
			for (ClaimEntry &e : c.claims)
			{
				x.xferInt(&e.playerIndex);
				x.xferObjectID(&e.objectId);
			}
			if (loading)
			{
				c.state = state;
			}
		}
		if (!loading)
		{
			continue;
		}
		const std::string cell = "terrain resource manager: cell " + std::to_string(i);
		if (c.state < CELL_FREE || c.state > CELL_OWNED)
		{
			throw XferException(cell + " has the state " + std::to_string(c.state));
		}
		if ((c.state == CELL_BLOCKED) != (m_cells[i].state == CELL_BLOCKED))
		{
			throw XferException(cell + (c.state == CELL_BLOCKED ? " is blocked in the snapshot, not in this game's map" : " is blocked in this game's map, not in the snapshot"));
		}
		for (const ClaimEntry &e : c.claims)
		{
			if (e.playerIndex < -1 || e.playerIndex >= 20)
			{
				throw XferException(cell + " has an entry of player index " + std::to_string(e.playerIndex));
			}
		}
		if (c.state == CELL_BLOCKED && !c.claims.empty())
		{
			throw XferException(cell + " is blocked and holds " + std::to_string(c.claims.size()) + " entries"); // RW 0x75C02D: nothing claims one
		}
	}
	// the OpenBFME trailer (S-4253)
	XferVersion own{ 1, 1 };
	x.xferVersion(own);
	for (Claimant &c : claimants)
	{
		x.xferBool(&c.complete);
	}
	if (!loading)
	{
		return;
	}
	for (size_t i = 0; i < claimants.size(); ++i)
	{
		if (claimants[i].id == INVALID_ID)
		{
			throw XferException("terrain resource manager: claimant " + std::to_string(i) + " has no object id");
		}
		for (size_t j = 0; j < i; ++j)
		{
			if (claimants[j].id == claimants[i].id)
			{
				throw XferException("terrain resource manager: object " + std::to_string(claimants[i].id) + " claims twice");
			}
		}
	}
	// the active entries (a SHARED cell's, an OWNED cell's owner): a tombstone {-1, 0} (RW 0x75C461 clears a released entry in place) or a claimant's
	// object with a player of this game; an OWNED cell's live owner is a complete claimant (only a complete claim takes a cell, RW 0x75C5D2). A FREE cell's
	// entries are the stale ones a release leaves behind (cleared by the next claim) and are not references
	const PlayerList &players = m_logic.players();
	auto isPlayer = [&](std::int32_t index) {
		for (int n = 0; n < players.getPlayerCount(); ++n)
		{
			const Player *p = players.getNthPlayer(n);
			if (p && p->getPlayerIndex() == index)
			{
				return true;
			}
		}
		return false;
	};
	for (size_t i = 0; i < loadedCells.size(); ++i)
	{
		const Cell &c = loadedCells[i];
		if (c.state != CELL_SHARED && c.state != CELL_OWNED)
		{
			continue;
		}
		const std::string cell = "terrain resource manager: cell " + std::to_string(i);
		for (size_t k = 0; k < c.claims.size(); ++k)
		{
			const ClaimEntry &e = c.claims[k];
			if (e.playerIndex == -1 && e.objectId == INVALID_ID)
			{
				continue;
			}
			auto claimant = std::find_if(claimants.begin(), claimants.end(), [&](const Claimant &cl) { return cl.id == e.objectId; });
			if (e.objectId == INVALID_ID || claimant == claimants.end())
			{
				throw XferException(cell + " names the object " + std::to_string(e.objectId) + ", which claims no ground");
			}
			if (!isPlayer(e.playerIndex))
			{
				throw XferException(cell + " names player index " + std::to_string(e.playerIndex) + ", which this game lacks");
			}
			if (c.state == CELL_OWNED && k == 0 && !claimant->complete)
			{
				throw XferException(cell + " is owned by the object " + std::to_string(e.objectId) + ", whose claim is not complete");
			}
		}
	}
	// staged until finishLoad: the claimants' objects are checked after every block loaded, then the manager takes the state
	m_pending = std::make_unique<Pending>();
	m_pending->claimants = std::move(loadedClaimants);
	m_pending->cells = std::move(loadedCells);
	m_pending->maxRadius = maxRadius;
}

void TerrainResourceManager::finishLoad()
{
	std::unique_ptr<Pending> pending = std::move(m_pending);
	if (!pending)
	{
		throw XferException("terrain resource manager: nothing loaded to finish");
	}
	for (const Claimant &c : pending->claimants)
	{
		if (!m_logic.findObjectByID(c.id))
		{
			throw XferException("terrain resource manager: the claimant object " + std::to_string(c.id) + " does not exist");
		}
	}
	m_claimants = std::move(pending->claimants);
	m_cells = std::move(pending->cells);
	m_maxRadius = pending->maxRadius;
}
