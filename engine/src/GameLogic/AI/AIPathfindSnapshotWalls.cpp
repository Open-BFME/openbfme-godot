// OpenBFME. GPL-3.0.
//
// The pathfinder snapshot's walls (version 2 of obfme:AIWorld's pathfinder, lanes WALL-1 / WALL-2; see AIPathfindSnapshot.cpp): what the ground grid's
// class words do not hold. The wall heights and pieces (Pathfinder::classifyWall's records: a piece's surfaces, their layers, its cells), the raised
// walkways (each layer's areas, extent and cells, those cells' class words, zones, portal markers and cell infos with their occupants: the infos come
// from the same pool as the ground grid's, which the load makes again), the ground cells' portal markers, the pathfind waypoints (the registry, its
// counter and ids; a waypoint's unit filter is the owner's module data, made again through PathfindWorld::portalFilterOf), the corridor's pending
// portal hops, the zone blocks' portal lists (in their own order: the state hash folds them) and Uber's burst flag (PATHPERF-4). A load reads all of it
// first, checks it, then replaces the pathfinder's (with the rest of the block, SAVES.md rule 4).

#include "GameLogic/AI/AIPathfind.h"

#include "Common/Xfer.h"
#include "GameLogic/AI/AIXfer.h"

#include <set>
#include <string>

namespace
{
void xferSurfaceW(Xfer &x, PathfindWallSurface &s)
{
	std::uint32_t n = (std::uint32_t)s.triangles.size();
	x.xferCount(&n, 36, sizeof(PathfindWallTriangle));
	if (x.isLoading())
	{
		s.triangles.assign(n, PathfindWallTriangle());
	}
	for (PathfindWallTriangle &t : s.triangles)
	{
		for (Coord3D &v : t.v)
		{
			AIXfer::coord(x, v);
		}
	}
	x.xferReal(&s.loX);
	x.xferReal(&s.loY);
	x.xferReal(&s.hiX);
	x.xferReal(&s.hiY);
	x.xferReal(&s.height);
}

void xferRegion(Xfer &x, PathfindRegion &r)
{
	AIXfer::icoord2(x, r.lo);
	AIXfer::icoord2(x, r.hi);
}

void xferLayers(Xfer &x, std::vector<PathfindLayerEnum> &v, PathfindLayerEnum lo, PathfindLayerEnum hi, const char *what)
{
	std::uint32_t n = (std::uint32_t)v.size();
	x.xferCount(&n, 1, sizeof(PathfindLayerEnum));
	if (x.isLoading())
	{
		v.assign(n, LAYER_INVALID);
	}
	for (PathfindLayerEnum &l : v)
	{
		AIXfer::enumIn(x, l, lo, hi, what);
		if (x.isLoading())
		{
			AIXfer::inRange(l, lo, hi, what);
		}
	}
}

// a cell's info: the obstacle, the flags, the occupant chains (the ground grid's layout, AIPathfindSnapshot.cpp)
void xferInfo(Xfer &x, Pathfinder::WallState::Info &in)
{
	x.xferUnsignedInt(&in.obstacle);
	x.xferUnsignedByte(&in.flags);
	std::uint8_t kinds = 0;
	for (int k = 0; k < OCC_KIND_COUNT; ++k)
	{
		kinds |= in.owners[k].empty() ? 0 : (std::uint8_t)(1u << k);
	}
	x.xferUnsignedByte(&kinds);
	if (in.flags >> 3 || kinds >> OCC_KIND_COUNT)
	{
		throw XferException("pathfinder: a raised cell's info has unknown flags or occupant kinds");
	}
	for (int k = 0; k < OCC_KIND_COUNT; ++k)
	{
		if (kinds & (1u << k))
		{
			AIXfer::ids(x, in.owners[k]);
			if (x.isLoading() && in.owners[k].empty())
			{
				throw XferException("pathfinder: an empty occupant chain on a raised cell");
			}
		}
	}
}
} // namespace

void Pathfinder::xferWalls(Xfer &x, WallState &w)
{
	const bool loading = x.isLoading();
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	// the wall heights and pieces (WALL-1, WALL-2's raised layers of a piece)
	if (!loading)
	{
		w.wallHeights = m_wallHeights;
	}
	std::uint32_t n = (std::uint32_t)w.wallHeights.size();
	x.xferCount(&n, 4, sizeof(float));
	if (loading)
	{
		if (n > (std::uint32_t)((int)LAYER_WALL_LAST - (int)LAYER_WALL_FIRST + 1))
		{
			throw XferException("pathfinder: " + std::to_string(n) + " wall heights, more than the wall layers");
		}
		w.wallHeights.assign(n, 0.0f);
	}
	for (float &h : w.wallHeights)
	{
		x.xferReal(&h);
	}
	n = (std::uint32_t)m_wallPieces.size();
	x.xferCount(&n, 28, sizeof(WallPiece));
	if (loading)
	{
		w.wallPieces.assign(n, WallPiece());
	}
	for (std::uint32_t i = 0; i < n; ++i)
	{
		WallPiece &p = loading ? w.wallPieces[i] : m_wallPieces[i];
		x.xferUnsignedInt(&p.id);
		std::uint32_t c = (std::uint32_t)p.walls.size();
		x.xferCount(&c, 24, sizeof(PathfindWallSurface));
		if (loading)
		{
			p.walls.assign(c, PathfindWallSurface());
		}
		for (PathfindWallSurface &s : p.walls)
		{
			xferSurfaceW(x, s);
		}
		xferLayers(x, p.wallLayers, LAYER_INVALID, LAYER_WALL_LAST, "pathfinder: a wall piece's layer");
		c = (std::uint32_t)p.ramps.size();
		x.xferCount(&c, 24, sizeof(PathfindWallSurface));
		if (loading)
		{
			p.ramps.assign(c, PathfindWallSurface());
		}
		for (PathfindWallSurface &s : p.ramps)
		{
			xferSurfaceW(x, s);
		}
		xferLayers(x, p.raisedLayers, LAYER_INVALID, LAYER_RAISED_LAST, "pathfinder: a wall piece's raised layer");
		xferRegion(x, p.cells);
		if (loading && p.wallLayers.size() != p.walls.size())
		{
			throw XferException("pathfinder: wall piece " + std::to_string(p.id) + " has " + std::to_string(p.walls.size()) + " surfaces and " +
				std::to_string(p.wallLayers.size()) + " layers");
		}
	}
	// the raised walkways (WALL-2)
	bool anyRaised = m_anyRaised;
	x.xferBool(&anyRaised);
	w.anyRaised = anyRaised;
	for (int l = 0; l < (int)w.raised.size(); ++l)
	{
		WallState::Raised &r = w.raised[(size_t)l];
		const RaisedLayer *mine = loading ? nullptr : &m_raised[(size_t)l];
		bool used = loading ? false : mine->used() || !mine->cells.empty();
		x.xferBool(&used);
		r.used = used;
		if (!used)
		{
			continue;
		}
		if (!loading)
		{
			r.areas = mine->areas;
			r.height = mine->height;
			r.origin = mine->origin;
			r.width = mine->width;
			r.depth = mine->depth;
		}
		std::uint32_t a = (std::uint32_t)r.areas.size();
		x.xferCount(&a, 28, sizeof(r.areas[0]) + 64);
		if (loading)
		{
			r.areas.assign(a, {});
		}
		for (auto &area : r.areas)
		{
			x.xferUnsignedInt(&area.first);
			xferSurfaceW(x, area.second);
		}
		x.xferInt(&r.height);
		AIXfer::icoord2(x, r.origin);
		x.xferInt(&r.width);
		x.xferInt(&r.depth);
		if (loading && (r.width < 0 || r.depth < 0 || (std::uint64_t)r.width * (std::uint64_t)r.depth > (std::uint64_t)m_blockOfMapCells.size()))
		{
			throw XferException("pathfinder: raised layer " + std::to_string(l) + " of " + std::to_string(r.width) + " x " + std::to_string(r.depth) + " cells");
		}
		const size_t cells = (size_t)r.width * (size_t)r.depth;
		if (loading)
		{
			r.words.assign(cells, 0);
			r.zones.assign(cells, 0);
			r.portals.assign(cells, 0);
		}
		for (size_t i = 0; i < cells; ++i)
		{
			std::uint32_t word = loading ? 0 : classWord(mine->cells[i]);
			std::uint16_t zone = loading ? 0 : (std::uint16_t)mine->cells[i].getZone(), portal = loading ? 0 : mine->cells[i].getPortal();
			x.xferUnsignedInt(&word);
			if (loading && (word >> 24 || (word & 0xF) > 7 || ((word >> 4) & 0x7F) > (std::uint32_t)LAYER_LAST || ((word >> 11) & 0x7F) > (std::uint32_t)LAYER_LAST))
			{
				throw XferException("pathfinder: a raised cell's class word " + std::to_string(word));
			}
			x.xferUnsignedShort(&zone);
			x.xferUnsignedShort(&portal);
			if (loading)
			{
				r.words[i] = word;
				r.zones[i] = zone;
				r.portals[i] = portal;
			}
		}
		std::uint32_t infos = 0;
		for (size_t i = 0; !loading && i < cells; ++i)
		{
			infos += mine->cells[i].hasInfo() ? 1u : 0u;
		}
		x.xferCount(&infos, 10, sizeof(WallState::Info));
		std::uint32_t index = 0;
		for (std::uint32_t k = 0; k < infos; ++k)
		{
			WallState::Info in;
			if (!loading)
			{
				while (!mine->cells[index].hasInfo())
				{
					++index;
				}
				const PathfindCell &c = mine->cells[index];
				const PathfindCellInfo *info = c.info();
				in.obstacle = info->m_obstacleID;
				in.flags = (std::uint8_t)((info->m_blockedByAlly ? 1 : 0) | (info->m_obstacleIsFence ? 2 : 0) | (info->m_obstacleIsTransparent ? 4 : 0));
				for (int kind = 0; kind < OCC_KIND_COUNT; ++kind)
				{
					for (const PathfindOccupant *o = c.occupants((PathfindOccupantKind)kind); o; o = o->next)
					{
						in.owners[kind].push_back(o->owner);
					}
				}
			}
			in.index = index;
			x.xferUnsignedInt(&in.index);
			if (in.index >= cells || (k != 0 && loading && in.index <= r.infos.back().index))
			{
				throw XferException("pathfinder: a raised cell info at cell " + std::to_string(in.index) + " out of order or beyond the layer's " + std::to_string(cells));
			}
			xferInfo(x, in);
			if (loading)
			{
				r.infos.push_back(std::move(in));
			}
			index = in.index + 1;
		}
	}
	// the ground cells' portal markers (PathfindCell::m_portal: the waypoint slot + 1)
	if (!loading)
	{
		for (std::uint32_t i = 0; i < (std::uint32_t)m_blockOfMapCells.size(); ++i)
		{
			if (m_blockOfMapCells[i].getPortal() != 0)
			{
				w.portalCells.emplace_back(i, m_blockOfMapCells[i].getPortal());
			}
		}
	}
	n = (std::uint32_t)w.portalCells.size();
	x.xferCount(&n, 6, sizeof(w.portalCells[0]));
	if (loading)
	{
		w.portalCells.assign(n, {});
	}
	for (auto &pc : w.portalCells)
	{
		x.xferUnsignedInt(&pc.first);
		x.xferUnsignedShort(&pc.second);
		if (loading && (pc.first >= m_blockOfMapCells.size() || pc.second == 0))
		{
			throw XferException("pathfinder: a portal marker at cell " + std::to_string(pc.first));
		}
	}
	// the waypoints: the registry (a slot: used, then the waypoint without its filter, whether it has one), the counter, the ids in creation order, the hops
	x.xferInt(&(loading ? w.waypointCounter : m_waypointCounter));
	if (!loading)
	{
		w.waypointCounter = m_waypointCounter;
	}
	n = (std::uint32_t)m_waypointSlots.size();
	x.xferCount(&n, 1, sizeof(WaypointSlot));
	if (loading)
	{
		if (n >= 0xFFFFu)
		{
			throw XferException("pathfinder: " + std::to_string(n) + " waypoint slots (a cell's portal marker holds 16 bits)");
		}
		w.slots.assign(n, WallState::Slot());
	}
	for (std::uint32_t i = 0; i < n; ++i)
	{
		WallState::Slot *ls = loading ? &w.slots[i] : nullptr;
		const WaypointSlot *ms = loading ? nullptr : &m_waypointSlots[i];
		bool used = loading ? false : ms->used;
		x.xferBool(&used);
		if (loading)
		{
			ls->used = used;
		}
		if (!used)
		{
			continue;
		}
		PathfindWaypoint wp = loading ? PathfindWaypoint() : ms->w;
		x.xferInt(&wp.id);
		AIXfer::coord(x, wp.pos);
		x.xferInt(&wp.type);
		if (loading && !(wp.type >= PWAYPOINT_NORMAL && wp.type <= PWAYPOINT_PRECLIMB) && wp.type != PWAYPOINT_TYPE7 && wp.type != PWAYPOINT_MINESHAFT)
		{
			throw XferException("pathfinder: waypoint " + std::to_string(wp.id) + " has the type " + std::to_string(wp.type));
		}
		x.xferInt(&wp.numLinks);
		if (loading && (wp.numLinks < 0 || wp.numLinks > PathfindWaypoint::MAX_LINKS))
		{
			throw XferException("pathfinder: waypoint " + std::to_string(wp.id) + " with " + std::to_string(wp.numLinks) + " links");
		}
		for (int k = 0; k < wp.numLinks; ++k)
		{
			x.xferInt(&wp.links[k]);
		}
		x.xferInt(&wp.linkedFrom);
		x.xferInt(&wp.next);
		x.xferBool(&wp.portal);
		x.xferUnsignedInt(&wp.owner);
		x.xferBool(&wp.allowEnemies);
		x.xferBool(&wp.allowNonSkirmishAI);
		x.xferUnsignedInt(&wp.activationFrame);
		bool filtered = !loading && (bool)wp.filter;
		x.xferBool(&filtered);
		x.xferBool(&wp.hasCell);
		AIXfer::icoord2(x, wp.cell);
		if (loading)
		{
			ls->w = wp;
			ls->filtered = filtered;
		}
	}
	if (!loading)
	{
		w.ids = m_waypointIds;
		w.hops = m_portalHops;
	}
	n = (std::uint32_t)w.ids.size();
	x.xferCount(&n, 4, sizeof(int));
	if (loading)
	{
		w.ids.assign(n, 0);
	}
	for (int &id : w.ids)
	{
		x.xferInt(&id);
	}
	n = (std::uint32_t)w.hops.size();
	x.xferCount(&n, 12, sizeof(PathfindPortalHop));
	if (loading)
	{
		w.hops.assign(n, PathfindPortalHop());
	}
	for (PathfindPortalHop &h : w.hops)
	{
		x.xferInt(&h.waypointId);
		AIXfer::icoord2(x, h.cell);
	}
	// the zone blocks' portal lists (their own order) and Uber's burst flag
	if (!loading)
	{
		w.blockPortals = m_zoneManager.blockPortals();
		w.anyPortal = m_zoneManager.anyPortal();
	}
	n = (std::uint32_t)w.blockPortals.size();
	x.xferCount(&n, 4, sizeof(w.blockPortals[0]));
	if (loading)
	{
		w.blockPortals.assign(n, {});
	}
	for (std::vector<std::uint16_t> &b : w.blockPortals)
	{
		std::uint32_t c = (std::uint32_t)b.size();
		x.xferCount(&c, 2, sizeof(std::uint16_t));
		if (loading)
		{
			if (c > 12)
			{
				throw XferException("pathfinder: a zone block lists " + std::to_string(c) + " portals (RW keeps 12)");
			}
			b.assign(c, 0);
		}
		for (std::uint16_t &p : b)
		{
			x.xferUnsignedShort(&p);
		}
	}
	x.xferBool(&w.anyPortal);
	if (!loading)
	{
		return;
	}
	// the references: the pieces and the raised areas' owners, the occupants, the waypoints' owners are objects of the game; the ids name used slots
	auto object = [&](PathfindObjectID id, const char *what) {
		if (id != PATHFIND_INVALID_ID && (!m_world || !m_world->findObjectByID(id)))
		{
			throw XferException(std::string("pathfinder: ") + what + " " + std::to_string(id) + " is not an object of the loaded game");
		}
	};
	for (const WallPiece &p : w.wallPieces)
	{
		object(p.id, "a wall piece");
	}
	std::set<int> used;
	for (const WallState::Slot &s : w.slots)
	{
		if (s.used)
		{
			object(s.w.owner, "the owner of a waypoint");
			if (!used.insert(s.w.id).second)
			{
				throw XferException("pathfinder: waypoint " + std::to_string(s.w.id) + " is in two slots");
			}
		}
	}
	for (int id : w.ids)
	{
		if (!used.count(id))
		{
			throw XferException("pathfinder: waypoint id " + std::to_string(id) + " names no used slot");
		}
	}
	for (const auto &pc : w.portalCells)
	{
		if (pc.second > w.slots.size() || !w.slots[pc.second - 1u].used)
		{
			throw XferException("pathfinder: a portal marker names slot " + std::to_string(pc.second - 1u) + ", not a used one");
		}
	}
	// a portal marker (raised cells) and a zone block's portal (RW block + 4) name a used waypoint slot by its number + 1 (Pathfinder::waypointOfSlot)
	auto usedSlot = [&](std::uint32_t slot) { return slot != 0 && slot <= w.slots.size() && w.slots[slot - 1u].used; };
	for (const WallState::Raised &r : w.raised)
	{
		for (std::uint16_t portal : r.portals)
		{
			if (portal != 0 && !usedSlot(portal))
			{
				throw XferException("pathfinder: a raised cell's portal marker names slot " + std::to_string(portal - 1) + ", not a used one");
			}
		}
	}
	for (const std::vector<std::uint16_t> &b : w.blockPortals)
	{
		for (std::uint16_t p : b)
		{
			if (!usedSlot(p))
			{
				throw XferException("pathfinder: a zone block's portal names slot " + std::to_string((int)p - 1) + ", not a used one");
			}
		}
	}
	for (const WallState::Raised &r : w.raised)
	{
		for (const auto &area : r.areas)
		{
			object(area.first, "the owner of a raised area");
		}
		for (const WallState::Info &in : r.infos)
		{
			object(in.obstacle, "the obstacle of a raised cell");
			for (const std::vector<std::uint32_t> &chain : in.owners)
			{
				for (std::uint32_t owner : chain)
				{
					object(owner, "the occupant of a raised cell");
				}
			}
		}
	}
	w.loaded = true;
}

void Pathfinder::applyWalls(WallState &w, bool loaded)
{
	if (!loaded || !w.loaded)
	{
		return; // a version 1 snapshot: a map without walls (the fresh game's are kept)
	}
	m_wallHeights = std::move(w.wallHeights);
	m_wallPieces = std::move(w.wallPieces);
	m_anyRaised = w.anyRaised;
	for (size_t l = 0; l < w.raised.size(); ++l)
	{
		WallState::Raised &r = w.raised[l];
		RaisedLayer &mine = m_raised[l];
		mine = RaisedLayer(); // the old cells' infos went with the pool (Pathfinder::xfer allocated it again)
		if (!r.used)
		{
			continue;
		}
		mine.areas = std::move(r.areas);
		mine.height = r.height;
		mine.origin = r.origin;
		mine.width = r.width;
		mine.depth = r.depth;
		mine.cells.assign(r.words.size(), PathfindCell());
		for (size_t i = 0; i < r.words.size(); ++i)
		{
			setClassWordWide(mine.cells[i], r.words[i]);
			mine.cells[i].setZone(r.zones[i]);
			mine.cells[i].setPortal(r.portals[i]);
		}
		for (const WallState::Info &in : r.infos)
		{
			PathfindCell &c = mine.cells[in.index];
			const ICoord2D pos{ mine.origin.x + (int)(in.index / (std::uint32_t)mine.depth), mine.origin.y + (int)(in.index % (std::uint32_t)mine.depth) };
			c.allocateInfo(m_pool, pos);
			PathfindCellInfo *info = c.info();
			info->m_obstacleID = in.obstacle;
			info->m_blockedByAlly = in.flags & 1;
			info->m_obstacleIsFence = (in.flags >> 1) & 1;
			info->m_obstacleIsTransparent = (in.flags >> 2) & 1;
			for (int k = 0; k < OCC_KIND_COUNT; ++k)
			{
				for (size_t i = in.owners[k].size(); i-- > 0;)
				{
					c.addOccupant(m_pool, (PathfindOccupantKind)k, in.owners[k][i], pos); // head insertion: the saved order
				}
			}
		}
	}
	for (PathfindCell &c : m_blockOfMapCells)
	{
		c.setPortal(0);
	}
	for (const auto &pc : w.portalCells)
	{
		m_blockOfMapCells[pc.first].setPortal(pc.second);
	}
	m_waypointSlots.assign(w.slots.size(), WaypointSlot());
	for (size_t i = 0; i < w.slots.size(); ++i)
	{
		m_waypointSlots[i].used = w.slots[i].used;
		m_waypointSlots[i].w = w.slots[i].w;
		if (w.slots[i].used && w.slots[i].filtered && m_world)
		{
			m_waypointSlots[i].w.filter = m_world->portalFilterOf(w.slots[i].w.owner); // the owner module's ObjectFilter (it is not data of the snapshot)
		}
	}
	m_waypointIds = std::move(w.ids);
	m_waypointCounter = w.waypointCounter;
	m_portalHops = std::move(w.hops);
	m_zoneManager.setBlockPortals(std::move(w.blockPortals), w.anyPortal);
}
