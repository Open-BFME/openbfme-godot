// OpenBFME. GPL-3.0.
// See GameLogic/System/TerrainResourceManager.h for the target facts and the addresses.

#include "GameLogic/System/TerrainResourceManager.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Object/Object.h"

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
void TerrainResourceManager::forEachInDisc(const Disc &d, Visitor &&visit)
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

float TerrainResourceManager::previewShare(float x, float y, float radius, bool complete, ObjectID objectId, int playerIndex)
{
	// RW 0x75B561 with the counting visitor RW 0xC2BA38 (0x75C523): the same disc and the same usability test, nothing written
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
		if (usable(cx, cy, 0u, objectId, complete))
		{
			++counts.claimed;
		}
	});
	(void)playerIndex;
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
