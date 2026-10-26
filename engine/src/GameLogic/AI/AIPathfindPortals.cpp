// OpenBFME. GPL-3.0.
//
// Lane WALL-2: the pathfinder's portals. A portal is a waypoint (RW Waypoint, TerrainLogic.cpp) whose cell on the ground grid starts a hop the searches may take to
// the cells of the waypoints it links to, past an obstacle no cell step crosses: a postern gate through a wall (DynamicPortalBehaviour, Type:Walk), a siege
// ladder or a siege tower onto a wall top (DynamicPortalBehaviour, Type:Climb / PreClimb), a castle gate an ally may path through (FakePathfindPortalBehaviour),
// the mineshafts (type 8, MineshaftPortalBehaviour: not ported).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 WorldBuilder decompile with symbols, reverse/wb/wb_decomp.jsonl.xz, names the functions):
//   * the waypoint (ctor RW 0x6829B0): id 0x7FFFFFFE takes the counter RW 0xDA0748 (0x40000000 when the waypoint list RW 0xDE4694 is empty, then + 1); + 0x48
//     the portal flag (the ctor sets it), + 0xA9 AllowNonSkirmishAI (set), the links + 0x20 (8) with the count + 0x4C, + 0x40 the waypoint linking to it, + 0x44
//     the next intermediate, + 0x60 the type, + 0xA8 AllowEnemies, + 0xAC the portal cell, + 0xB0 the owner, + 0xB4 the activation frame, + 0xB8 / + 0xBC the
//     ObjectFilter; Waypoint::addLink (RW 0x67262C, WB 0xc461c0): appended while fewer than 8, the target's + 0x40 set either way;
//   * Pathfinder::AddPortal (RW 0x6EA42D, WB 0xd38e90): the cell RW 0x6E8CE6(pos, centred) on the ground grid (RW 0x5E2E9C); when it has no portal (cell + 4)
//     it takes the waypoint (PathfindCell::SetPortal RW 0x9343CE, which sets the waypoint's portal cell) and its zone block lists it (RW 0x937EC3 ->
//     Block::SetInteractsWithPortal RW 0x937A97: appended when not listed and fewer than 12; removal moves the last into the gap); + 0x48 = 1 always.
//     RemovePortal (RW 0x6EA48A): the cell's portal when it is this waypoint is cleared and the block drops it; + 0x48 = 0;
//   * Waypoint::isValidUnitType (RW 0x68229C, WB 0xc3e210): with an owner: no AllowEnemies -> the owner must exist and regard the unit as an ally (RW 0x68D7AB
//     == 2); no AllowNonSkirmishAI -> the unit's player is a skirmish AI player (RW 0x6AAC66) TheSkirmishAIManager runs (RW 0x6A950B); type 7 -> the unit has an
//     AI whose byte + 0x3CF is clear. Then the ObjectFilter (+ 0xB8: RW 0x7640C1(unit, 0)), the include / exclude KindOf masks (+ 0x68 / + 0x88, map waypoints;
//     none here), and an activation frame (+ 0xB4) the logic frame has reached;
//   * RW 0x6E82FD: a portal waypoint is + 0x48 set and type 1, 2, 3, 4, 7 or 8;
//   * RW 0x6F5547 (the zone search over portals; called by internalFindPath RW 0x6FD83D when the start and goal zones differ, by QuickDoesPathExist RW 0x6F5EE7
//     / 0x6F5F50, by the hierarchical search RW 0x6F8A1C): false for RW 0x68C053 (Object + 0x4AC) and for a unit with no AI and no locomotor set given; the
//     movement of RW 0x6EA04D; every portal waypoint of the terrain's list (RW vslot 0x84, next + 0x1C) the unit may take is filed under the effective zone (RW
//     0x938D89, the terrain variant) of its cell; a breadth-first walk from the start zone through each filed waypoint's links that are themselves filed: true
//     when the goal zone is reached;
//   * Pathfinder::CheckChangeLayers (RW 0x6F6286, WB 0xd4dd50; after a cell is popped and closed in every cell search): (1) a cell with a connect layer: the
//     cell of that layer at its index (the ground grid for 1 and 0x10 .. 0x40, RW 0x76550F) joins the open list when it is neither open nor closed, its parent
//     the cell, its costs the cell's (RW 0x93442F, + 0x12 / + 0x10 copied, RW 0x6F57A1 push); (2) a cell with a portal the unit may take: the corridor's last
//     portal hop (+ 0x1C1CC) is dropped when it is this waypoint; every link's cell (RW 0x5E2EF2 on the ground grid) that is neither open nor closed joins the
//     open list at the cell's costs, its parent the cell and its info + 0xC the waypoint (RW 0x93443D);
//   * RW 0x6F1F0D (the estimate of every cell search): from the cell through the corridor's portal hops, last to first, then to the goal; a leg is 10 * max +
//     4 * min of the cell index differences; a hop adds its leg, twice the sum so far and 1400 (RW 0x6F1F64 .. 0x6F1F89);
//   * internalFindPath's closest-cell key adds 10000 per remaining portal hop (RW 0x6FDA53 .. 0x6FDA6A);
//   * prependCells (RW 0x6EF591, see AIPathfindSearch.cpp) and the hierarchical search's portal branch (RW 0x6F909A .. 0x6F936B, see hierarchicalPortals).
// DONOR: the BFME2 WorldBuilder decompile (the names, the asserts: "dest!=(Waypoint *)1", "!(m_numPortals>=MAX_PORTALS)"), byte-matched where Open-BFME-2 has
// the body (DynamicPortalBehaviour*.cpp).
// INFERENCE (stop S-4857, pathstops::kPortals): the waypoints live in the pathfinder (RW: in TheTerrainLogic's list with the map's waypoints; a map waypoint of
// a portal type is not registered: none of the 2.01 maps' waypoints was checked for one), so the id counter restarts at each map; the ObjectFilter is the
// module's callback; RW 0x68C053's object flag is never set (S-4851); the BFS's movement is the caller's locomotor set (RW: the AI's).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "Common/StateHash.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace
{
// RW 0x76550F: the layer's cells are the ground grid's (the ground, the ramps and the wall tops)
bool groundGridLayer(int layer)
{
	return layer == (int)LAYER_GROUND || layer >= (int)LAYER_RAMP;
}

// RW 0x6F1F0D's leg: 10 * max + 4 * min
int legCost(int dx, int dy)
{
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	return dx > dy ? 10 * dx + 4 * dy : 14 * dx + 10 * (dy - dx);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------
// the waypoint table
// ---------------------------------------------------------------------------------------------------------
int Pathfinder::slotOfWaypoint(int id) const
{
	for (size_t i = 0; i < m_waypointSlots.size(); ++i)
	{
		if (m_waypointSlots[i].used && m_waypointSlots[i].w.id == id)
		{
			return (int)i;
		}
	}
	return -1;
}

const PathfindWaypoint *Pathfinder::findWaypoint(int id) const
{
	const int s = slotOfWaypoint(id);
	return s >= 0 ? &m_waypointSlots[(size_t)s].w : nullptr;
}

PathfindWaypoint *Pathfinder::findWaypoint(int id)
{
	const int s = slotOfWaypoint(id);
	return s >= 0 ? &m_waypointSlots[(size_t)s].w : nullptr;
}

const PathfindWaypoint *Pathfinder::waypointOfSlot(std::uint16_t slot) const
{
	if (slot == 0 || (size_t)slot > m_waypointSlots.size() || !m_waypointSlots[(size_t)slot - 1].used)
	{
		return nullptr;
	}
	return &m_waypointSlots[(size_t)slot - 1].w;
}

int Pathfinder::createWaypoint(const PathfindWaypoint &src)
{
	// RW 0x6829B0 with id 0x7FFFFFFE: the counter's id; the portal flag and AllowNonSkirmishAI set by the constructor, no links, no cell
	size_t slot = m_waypointSlots.size();
	for (size_t i = 0; i < m_waypointSlots.size(); ++i)
	{
		if (!m_waypointSlots[i].used)
		{
			slot = i;
			break;
		}
	}
	if (slot == m_waypointSlots.size())
	{
		if (m_waypointSlots.size() >= 0xFFFEu)
		{
			note(pathstops::kPortalTable);
			return PathfindWaypoint::NO_ID;
		}
		m_waypointSlots.emplace_back();
	}
	WaypointSlot &s = m_waypointSlots[slot];
	s.used = true;
	s.w = src;
	s.w.id = m_waypointCounter++;
	s.w.portal = true;
	s.w.numLinks = 0;
	s.w.linkedFrom = PathfindWaypoint::NO_ID;
	s.w.next = PathfindWaypoint::NO_ID;
	s.w.hasCell = false;
	m_waypointIds.push_back(s.w.id);
	note(pathstops::kPortals);
	return s.w.id;
}

void Pathfinder::destroyWaypoint(int id)
{
	const int s = slotOfWaypoint(id);
	if (s < 0)
	{
		return;
	}
	if (m_waypointSlots[(size_t)s].w.hasCell)
	{
		removePortal(id); // its cell must not keep a slot that a later waypoint reuses (RW's caller removes the portal first: DynamicPortalBehaviour RW 0x8639EF)
	}
	m_waypointSlots[(size_t)s].used = false;
	m_waypointSlots[(size_t)s].w = PathfindWaypoint();
	m_waypointIds.erase(std::find(m_waypointIds.begin(), m_waypointIds.end(), id));
}

bool Pathfinder::addWaypointLink(int from, int to)
{
	// RW 0x67262C (WB 0xc461c0): appended while fewer than 8 links; the target remembers the source either way
	PathfindWaypoint *a = findWaypoint(from);
	PathfindWaypoint *b = findWaypoint(to);
	if (!a || !b)
	{
		return false;
	}
	if (a->numLinks < PathfindWaypoint::MAX_LINKS)
	{
		a->links[a->numLinks++] = to;
	}
	b->linkedFrom = from;
	return true;
}

void Pathfinder::setWaypointNext(int a, int b)
{
	// RW 0x8642A5 .. 0x8642C2: b's + 0x48 = 0, a's + 0x44 = b
	PathfindWaypoint *wa = findWaypoint(a);
	PathfindWaypoint *wb = findWaypoint(b);
	if (!wa || !wb)
	{
		return;
	}
	wb->portal = false;
	wa->next = b;
}

void Pathfinder::addPortal(int id)
{
	// RW 0x6EA42D
	const int s = slotOfWaypoint(id);
	if (s < 0)
	{
		return;
	}
	PathfindWaypoint &w = m_waypointSlots[(size_t)s].w;
	Coord3D p = w.pos;
	ICoord2D c;
	const bool off = worldToCell(&p, true, &c); // RW 0x6E8CE6 (no clamp) then RW 0x5E2E9C: null off the grid
	PathfindCell *cell = off ? nullptr : getCell(LAYER_GROUND, c.x, c.y);
	if (cell && cell->getPortal() == 0)
	{
		cell->setPortal((std::uint16_t)(s + 1));
		w.hasCell = true;
		w.cell = c;
		m_zoneManager.setInteractsWithPortal(c.x, c.y, true, (std::uint16_t)(s + 1));
	}
	w.portal = true;
}

void Pathfinder::removePortal(int id)
{
	// RW 0x6EA48A
	const int s = slotOfWaypoint(id);
	if (s < 0)
	{
		return;
	}
	PathfindWaypoint &w = m_waypointSlots[(size_t)s].w;
	Coord3D p = w.pos;
	ICoord2D c;
	const bool off = worldToCell(&p, true, &c);
	PathfindCell *cell = off ? nullptr : getCell(LAYER_GROUND, c.x, c.y);
	if (cell && cell->getPortal() == (std::uint16_t)(s + 1))
	{
		cell->setPortal(0);
		w.hasCell = false;
		m_zoneManager.setInteractsWithPortal(c.x, c.y, false, (std::uint16_t)(s + 1));
	}
	w.portal = false;
}

void Pathfinder::pathsLostPortals()
{
	// RW 0x6E8454 (DynamicPortalBehaviour's portal removal RW 0x8639EF and FakePathfindPortalBehaviour's RW 0x864980 call it)
	if (m_world)
	{
		m_world->onPathsLostPortals();
	}
}

bool Pathfinder::isPortalWaypoint(const PathfindWaypoint &w)
{
	// RW 0x6E82FD
	if (!w.portal)
	{
		return false;
	}
	switch (w.type)
	{
	case 1: case 2: case 3: case 4: case 7: case 8:
		return true;
	default:
		return false;
	}
}

bool Pathfinder::waypointAllows(const PathfindWaypoint &w, const PathfindObject &obj) const
{
	// RW 0x68229C (see the file comment)
	if (w.owner != PATHFIND_INVALID_ID)
	{
		const PathfindObject *owner = m_world ? m_world->findObjectByID(w.owner) : nullptr;
		if (!w.allowEnemies)
		{
			if (!owner || owner->getRelationship(obj) != PATHFIND_ALLIES)
			{
				return false;
			}
		}
		if (!w.allowNonSkirmishAI && !obj.isSkirmishAIControlled())
		{
			return false;
		}
		if (w.type == PWAYPOINT_TYPE7)
		{
			// RW 0x68230B: refused without an AI or with the AI's byte + 0x3CF (set around the move-away searches)
			if (!obj.hasAI() || obj.inMoveAwaySearch())
			{
				return false;
			}
		}
	}
	if (w.filter && !w.filter(obj))
	{
		return false;
	}
	if (w.activationFrame != 0 && m_world && m_world->getFrame() < w.activationFrame)
	{
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// RW 0x6F5547: is there a portal chain from one zone to another
// ---------------------------------------------------------------------------------------------------------
bool Pathfinder::portalPathExists(const PathfindObject *obj, const PathfindLocomotorInfo &loco, zoneStorageType from, zoneStorageType to) const
{
	if (m_waypointIds.empty() || obj == nullptr)
	{
		return false; // nothing to cross (RW walks the terrain's list and files nothing)
	}
	if (obj->refusesPortals() || !obj->hasAI())
	{
		return false; // RW 0x6F555F (Object + 0x4AC) and 0x6F5568 (no AI and no locomotor set given)
	}
	const PathfindMovement mv = makeMovement(obj, loco);
	// zone -> the portals filed under it (RW 0x6F31D4, a multimap) and waypoint -> its zone (RW 0x6F4B66)
	std::multimap<zoneStorageType, int> byZone;
	std::map<int, zoneStorageType> zoneOf;
	Pathfinder &self = const_cast<Pathfinder &>(*this);
	for (auto it = m_waypointIds.rbegin(); it != m_waypointIds.rend(); ++it) // the terrain's list is newest first
	{
		const PathfindWaypoint *w = findWaypoint(*it);
		if (!w || !isPortalWaypoint(*w) || !waypointAllows(*w, *obj))
		{
			continue;
		}
		Coord3D p = w->pos;
		PathfindCell *cell = self.getCell(LAYER_GROUND, &p); // RW 0x680A75's layer: the ground grid (S-4857 for the raised layers)
		if (!cell)
		{
			continue;
		}
		const zoneStorageType z = m_zoneManager.getEffectiveZone(mv, true, cell->getZone());
		byZone.emplace(z, w->id);
		zoneOf[w->id] = z;
	}
	std::deque<zoneStorageType> queue;
	std::set<zoneStorageType> seen;
	queue.push_back(from);
	seen.insert(from);
	while (!queue.empty())
	{
		const zoneStorageType z = queue.front();
		queue.pop_front();
		if (z == to)
		{
			return true;
		}
		auto range = byZone.equal_range(z);
		for (auto it = range.first; it != range.second; ++it)
		{
			const PathfindWaypoint *w = findWaypoint(it->second);
			for (int i = 0; w && i < w->numLinks; ++i)
			{
				auto lz = zoneOf.find(w->links[i]);
				if (lz != zoneOf.end() && seen.insert(lz->second).second)
				{
					queue.push_back(lz->second);
				}
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------
// RW 0x6F6286 CheckChangeLayers
// ---------------------------------------------------------------------------------------------------------
void Pathfinder::checkChangeLayers(PathfindCell *cell, const PathfindObject *obj)
{
	if (!cell || !cell->hasInfo())
	{
		return;
	}
	const int x = cell->getXIndex(), y = cell->getYIndex();
	// (1) the connect layer
	const int conn = (int)cell->getConnectLayer();
	if (conn != (int)LAYER_INVALID)
	{
		PathfindCell *other = getCell(groundGridLayer(conn) ? LAYER_GROUND : (PathfindLayerEnum)conn, x, y);
		if (other && !(other->hasInfo() && (other->getOpen() || other->getClosed())))
		{
			if (other->allocateInfo(m_pool, ICoord2D{ x, y }))
			{
				other->setParentCell(cell);
				other->setCostSoFar(cell->getCostSoFar());
				other->setTotalCost(cell->getTotalCost());
				m_open.push(other);
			}
		}
	}
	// (2) the portal
	const std::uint16_t slot = cell->getPortal();
	if (slot == 0 || obj == nullptr)
	{
		return;
	}
	const PathfindWaypoint *w = waypointOfSlot(slot);
	if (!w || !waypointAllows(*w, *obj))
	{
		return;
	}
	if (!m_portalHops.empty() && m_portalHops.back().waypointId == w->id)
	{
		m_portalHops.pop_back(); // RW 0x6F636A .. 0x6F6388: the corridor's next hop is taken
	}
	for (int i = 0; i < w->numLinks; ++i)
	{
		const PathfindWaypoint *l = findWaypoint(w->links[i]);
		if (!l)
		{
			continue; // RW: "no link?"
		}
		Coord3D lp = l->pos;
		PathfindCell *lc = getCell(LAYER_GROUND, &lp); // RW 0x5E2EF2(layer 1, pos)
		if (!lc || (lc->hasInfo() && (lc->getOpen() || lc->getClosed())))
		{
			continue;
		}
		ICoord2D idx;
		lp = l->pos;
		worldToCell(&lp, true, &idx); // RW 0x6E8CE6(pos, centred): the record's position
		if (!lc->allocateInfo(m_pool, idx))
		{
			continue;
		}
		lc->setParentCellViaPortal(cell, slot);
		lc->setCostSoFar(cell->getCostSoFar());
		lc->setTotalCost(cell->getTotalCost());
		m_open.push(lc);
	}
}

unsigned Pathfinder::costToGoal(const PathfindCell *cell, const PathfindCell *goal) const
{
	// RW 0x6F1F0D (see the file comment)
	if (m_portalHops.empty())
	{
		return cell->costToGoal(goal);
	}
	// review of #63: the sum in unsigned 32-bit arithmetic, which wraps as RW's 32-bit adds do (a signed overflow on a long portal chain is undefined in C++);
	// below 2^31 the values are the signed ones
	int x = cell->info()->m_pos.x, y = cell->info()->m_pos.y;
	std::uint32_t acc = 0;
	for (size_t k = m_portalHops.size(); k-- > 0;)
	{
		const ICoord2D &h = m_portalHops[k].cell;
		acc = (std::uint32_t)legCost(x - h.x, y - h.y) + 2u * acc + 1400u;
		x = h.x;
		y = h.y;
	}
	return (unsigned)((std::uint32_t)legCost(x - (int)goal->getXIndex(), y - (int)goal->getYIndex()) + acc);
}

// ---------------------------------------------------------------------------------------------------------
// the hierarchical search's portal branch (RW 0x6F909A .. 0x6F936B; WB internal_findHierarchicalPath lines 936-1086)
// ---------------------------------------------------------------------------------------------------------
// Every portal of the popped cell's block the unit may take (a type 8 one only while the mineshaft goal is set: never here) is tried from its start cell to
// each link's cell: the start cell must lie in the popped cell's block and its effective zone (non-terrain variant, RW 0x9389A6) be the popped cell's. RW
// then reads the RAISED LAYER whose index is the portal's index in the block: when the start cell is not in the block it takes that layer's two end cells
// instead (the start cell from the end, the link cell from the start), and a portal whose layer's zone (layer + 0x2C) is the goal's ends the search as found
// (the BFME2 decompile has the same reads: a retail quirk, ported as read; without that raised layer neither applies). Otherwise the link cell, neither open
// nor closed, joins the open list: the start cell (when it is not the popped one) gets a record, the popped cell as parent, and is closed; the link cell's
// parent is the start cell with the waypoint (RW 0x93443D), its cost so far the start cell's plus 500 for a type 8 waypoint, else the 1/10-cell distance
// between the two (RW 0x9345AA), its total that plus its distance to the goal.
bool Pathfinder::hierarchicalPortals(PathfindCell *c, PathfindCell *goal, zoneStorageType cz, zoneStorageType goalZone, const PathfindMovement &mv,
	const PathfindObject *obj, int &cellCount)
{
	(void)goalZone;
	const int bs = m_zoneManager.blockSize();
	const int cx = c->getXIndex(), cy = c->getYIndex();
	if (!m_zoneManager.interactsWithPortal(cx, cy))
	{
		return false;
	}
	const int bx = cx / bs, by = cy / bs;
	for (int i = 0;; ++i)
	{
		const std::uint16_t slot = m_zoneManager.getPortal(cx, cy, i);
		if (slot == 0)
		{
			break;
		}
		const PathfindWaypoint *w = waypointOfSlot(slot);
		if (!w || w->type == PWAYPOINT_MINESHAFT || !obj || !waypointAllows(*w, *obj))
		{
			continue; // the mineshaft network's goal switch is not ported (S-162): its type 8 waypoints are never taken
		}
		ICoord2D from;
		{
			Coord3D p = w->pos;
			worldToCell(&p, true, &from); // RW 0x6E8CE6 (centred, no clamp: the < 0 tests below and RW 0x5E2E9C reject off-grid cells)
		}
		for (int k = 0; k < w->numLinks; ++k)
		{
			const PathfindWaypoint *l = findWaypoint(w->links[k]);
			if (!l)
			{
				continue;
			}
			ICoord2D to;
			{
				Coord3D p = l->pos;
				worldToCell(&p, true, &to);
			}
			ICoord2D f = from;
			if (f.x / bs != bx || f.y / bs != by)
			{
				// the raised layer `i`'s end cells (see above); the wall connect cells (RW + 0x18 / + 0x20) are not kept by the port (S-4856): (-1, -1)
				note(pathstops::kPortalLayerQuirk);
				continue;
			}
			if (f.x < 0 || f.y < 0 || to.x < 0 || to.y < 0)
			{
				continue;
			}
			if (to.x == f.x && to.y == f.y)
			{
				// OpenBFME guard, not RotWK: RW tests the link cell's open / closed state (RW 0x6F91FE .. 0x6F9216) before it closes the source cell
				// (RW 0x6F9235 ..), as the port does, so a link whose two ends share a cell would make that cell its own parent and the corridor's parent
				// walk (RW 0x6F49BC, makeHierarchicalPathPassable) would never end. Such a link moves nobody: it is skipped before either record changes
				continue;
			}
			PathfindCell *fromCell = getCell(LAYER_GROUND, f.x, f.y);
			if (!fromCell || m_zoneManager.getEffectiveZone(mv, false, fromCell->getZone()) != cz)
			{
				continue;
			}
			PathfindCell *linkCell = getCell(LAYER_GROUND, to.x, to.y);
			if (!linkCell || (linkCell->hasInfo() && (linkCell->getOpen() || linkCell->getClosed())))
			{
				continue;
			}
			if (fromCell != c)
			{
				if (!fromCell->allocateInfo(m_pool, f))
				{
					continue;
				}
				fromCell->setParentCellViaPortal(c, 0);
				if (!fromCell->getOpen() && !fromCell->getClosed())
				{
					markClosed(fromCell);
				}
			}
			if (!linkCell->allocateInfo(m_pool, to))
			{
				continue;
			}
			linkCell->setParentCellViaPortal(fromCell, slot);
			++cellCount;
			const unsigned c1 = w->type == PWAYPOINT_MINESHAFT ? 500u : linkCell->costToHierGoal(fromCell);
			const unsigned c2 = linkCell->costToHierGoal(goal);
			linkCell->setCostSoFar((std::uint16_t)(fromCell->getCostSoFar() + c1));
			linkCell->setTotalCost((std::uint16_t)(linkCell->getCostSoFar() + c2));
			linkCell->setParentCellViaPortal(fromCell, slot);
			m_open.push(linkCell);
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------
// the zone blocks' portal lists
// ---------------------------------------------------------------------------------------------------------
void PathfindZoneManager::setInteractsWithPortal(int cellX, int cellY, bool on, std::uint16_t portal)
{
	// RW 0x937EC3 -> RW 0x937A97 (the block size 16 is hard-coded there: RW divides by 16)
	if (cellX < 0 || cellY < 0 || portal == 0)
	{
		return;
	}
	const int bx = cellX / m_blockSize, by = cellY / m_blockSize;
	if (bx >= m_zoneBlockExtent.x || by >= m_zoneBlockExtent.y)
	{
		return;
	}
	Block &b = blockAt(bx, by);
	auto it = std::find(b.portals.begin(), b.portals.end(), portal);
	if (on)
	{
		if (it == b.portals.end() && b.portals.size() < 12)
		{
			b.portals.push_back(portal);
			m_anyPortal = true;
		}
	}
	else if (it != b.portals.end())
	{
		*it = b.portals.back();
		b.portals.pop_back();
	}
}

bool PathfindZoneManager::interactsWithPortal(int cellX, int cellY) const
{
	// RW 0x937F2D
	if (cellX < 0 || cellY < 0)
	{
		return false;
	}
	const int bx = cellX / m_blockSize, by = cellY / m_blockSize;
	if (bx >= m_zoneBlockExtent.x || by >= m_zoneBlockExtent.y)
	{
		return false;
	}
	return !m_blocks[(size_t)bx * (size_t)m_zoneBlockExtent.y + (size_t)by].portals.empty();
}

std::uint16_t PathfindZoneManager::getPortal(int cellX, int cellY, int i) const
{
	// RW 0x937F7F -> RW 0x937AE4
	if (cellX < 0 || cellY < 0 || i < 0)
	{
		return 0;
	}
	const int bx = cellX / m_blockSize, by = cellY / m_blockSize;
	if (bx >= m_zoneBlockExtent.x || by >= m_zoneBlockExtent.y)
	{
		return 0;
	}
	const Block &b = m_blocks[(size_t)bx * (size_t)m_zoneBlockExtent.y + (size_t)by];
	return (size_t)i < b.portals.size() ? b.portals[(size_t)i] : 0;
}

// ---------------------------------------------------------------------------------------------------------
// the state hash (only when portals exist: a map without them hashes as before)
// ---------------------------------------------------------------------------------------------------------
void Pathfinder::crcPortals(StateHasher &h) const
{
	if (m_waypointIds.empty() && m_portalHops.empty())
	{
		return;
	}
	h.addI32(m_waypointCounter);
	h.addU32((std::uint32_t)m_waypointIds.size());
	for (int id : m_waypointIds)
	{
		const PathfindWaypoint *w = findWaypoint(id);
		h.addI32(id);
		h.addI32(slotOfWaypoint(id));
		h.addFloat(w->pos.x);
		h.addFloat(w->pos.y);
		h.addFloat(w->pos.z);
		h.addI32(w->type);
		h.addI32(w->numLinks);
		for (int i = 0; i < w->numLinks; ++i)
		{
			h.addI32(w->links[i]);
		}
		h.addI32(w->linkedFrom);
		h.addI32(w->next);
		h.addU32((w->portal ? 1u : 0u) | (w->allowEnemies ? 2u : 0u) | (w->allowNonSkirmishAI ? 4u : 0u) | (w->hasCell ? 8u : 0u) | (w->filter ? 16u : 0u));
		h.addU32((std::uint32_t)w->owner);
		h.addU32(w->activationFrame);
		h.addI32(w->cell.x);
		h.addI32(w->cell.y);
	}
	h.addU32((std::uint32_t)m_portalHops.size());
	for (const PathfindPortalHop &p : m_portalHops)
	{
		h.addI32(p.waypointId);
		h.addI32(p.cell.x);
		h.addI32(p.cell.y);
	}
}
