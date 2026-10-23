// OpenBFME. GPL-3.0.
//
// The raised layers 2 .. 15 of the pathfinder (lane WALL-2): the RaisedWallMesh walkways of RotWK's gatehouses (Helm's Deep's HelmsDeepGatehouseLeft /
// Right / Center, Minas Tirith's and the faction castles' gate walkways) on a layer with cells of its own, above a ground grid whose cells stay the ground
// (the open gate under the walkway is passed on the ground).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 WorldBuilder decompile, pathfinder_layer.cpp / pathfinder_classify.cpp, names the functions):
//   * the layer (PathfindLayer, 0x40 bytes at pathfinder + 0x60 + layer * 0x40; constructor RW 0x768381): + 0 the cells, + 4 the column pointers, + 8 / + 0xC
//     width / height in cells, + 0x10 / + 0x14 the origin cell, + 0x18 / + 0x20 two wall connect cells (AddWallConnectCell RW 0x76879E), + 0x28 the layer id,
//     + 0x2C its zone, + 0x30 a flag of the bridge kind, + 0x34 the bridge, + 0x38 the area list (the raised wall kind), + 0x3C the height (an int);
//   * ClassifyWall (RW 0x935FAA) when a wall piece is added: the drawable's raised wall polygon and its height (Drawable RW 0x67290D, the draw interface's
//     vslot 0x9C; W3DModelDraw data + 0x110 RaisedWallMesh); for the layers 2 .. 15 in use (RW 0x768326) with an area whose bounds come within 10 of the
//     polygon's (RW 0x768286) and |(float)height - h| < 3.5 (RW 0xC7FADC, x87 then CRT fabs): the polygon joins the layer (AddArea RW 0x768276; with the map
//     loaded the layer's cells are freed, RW 0x76848F, and made again, RW 0x7684F8); else the first layer not in use takes it (RW 0x768246: height = the
//     polygon's + 0x30) and makes its cells;
//   * AllocateCells (RW 0x7684F8): the union of the areas' bounds; origin = floor((lo - 0.1) * 0.1) - 1 and end = ceil((hi + 0.1) * 0.1) + 1 (x87 PC24, the
//     CRT floor / ceil, fistp), clamped to the map's extent; the cells' zone is the layer's;
//   * ClassifyWallCells (RW 0x768832): every cell: connect layer none, layer = the id; its four corners (x * 10, y * 10) .. (x * 10 + 10, y * 10 + 10) are
//     tested against the areas (RW 0x7687EB: pointInTrigger of any area); the cell is reset (RW 0x93483E), takes the layer and the layer's zone, and is
//     type 5 (BRIDGE_IMPASSABLE) unless a corner is inside: then CLEAR (RW 0x934387);
//   * PathfindLayer::GetCell (RW 0x7682E1): null outside the cells or for a type 5 cell; Pathfinder::getCell (BFME2 WB 0xd35570) answers the ground grid's
//     cell then;
//   * the zones (RW 0x93A584 .. 0x93A5BE): every raised layer gets a zone of its own whose class is the layer (setZone RW 0x768336); a CLEAR ground cell whose
//     connect layer is a raised layer joins its zone with the layer's (RW 0x93A0D1 .. 0x93A14B);
//   * the ramps (BFME2 WB gd_0052ED74 / gd_0052E914, pathfinder_classify.cpp): a ramp end on a raised layer (the layer at the end, RW 0x6EE600) adds a wall
//     connect cell; a cell the ramp covers in part whose raised layer cell exists and whose ramp height is within 15 of the layer's height becomes a ramp cell
//     (layer 0x10) connecting to the raised layer, and the raised cell connects to the ramp;
//   * the layer at a point (RW 0x6EE600): an object with Object + 0x4AC is on the ground; an even footprint moves the point by 5; the first raised layer in
//     use whose cell there is passable and whose (float)height - z < 25 (RW 0xC69574); else the ground cell's layer. TheTerrainLogic::getLayerForDestination
//     (RW 0x680A75) asks the ramps first (RW 0x6E89EE), then RW 0x6EE600.
// INFERENCE (stop S-4856, docs/STOPS.md, pathstops::kRaisedLayers):
//   * the raised wall polygon is the RaisedWallMesh's triangles (GameClient/DrawableWallMeshes, as lane WALL-1 does for the wall tops) and its height (RW:
//     the polygon's int + 0x30) is the mesh's highest z truncated (cvttss2si);
//   * the ramp joint rule is BFME2's (the RotWK counterpart of gd_0052E914 was not compared), applied to every raised layer with a cell under the ramp (BFME2:
//     the layers of the ramp's two ends);
//   * the raised zones are numbered after the ground zones (RW numbers them first): the zone ids differ, the partition does not;
//   * a unit's layer on a walkway (UpdateLayer / AdjustLayer): it takes the raised layer at a node on it once off the ramp's surface, and leaves it where
//     the layer has no cell (Pathfinder::settleLayer, AIPathfindWalls.cpp);
//   * the hierarchical search's raised branch (RW 0x9389CE) is not ported: a corridor that needs the walkway is not found and the search runs without one.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "Common/StateHash.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{
const float kRaisedSameHeight = 3.5f; // RW 0xC7FADC
const float kRaisedJoin = 10.0f;      // RW 0x768286: the bounds overlap within 10
const float kRampJoinTolerance = 15.0f;

bool boundsNear(const PathfindWallSurface &a, const PathfindWallSurface &b)
{
	// RW 0x768286: b.hi + 10 > a.lo, b.lo - 10 < a.hi on both axes (the area's bounds `a`, the new polygon's `b`)
	return SimMath::addf32(b.hiX, kRaisedJoin) > a.loX && SimMath::addf32(b.hiY, kRaisedJoin) > a.loY && SimMath::subf32(b.loX, kRaisedJoin) < a.hiX &&
		SimMath::subf32(b.loY, kRaisedJoin) < a.hiY;
}

// RW 0x768608 .. 0x7686A5: floor((lo - 0.1) * 0.1) / ceil((hi + 0.1) * 0.1) under x87 PC24, the CRT floor / ceil, fstp dword, fistp
int cellFloor(float lo)
{
	return SimMath::fistp32(SimMath::floorD(SimMath::pc24MulW(SimMath::pc24SubW((double)lo, (double)0.1f), (double)0.1f)));
}
int cellCeil(float hi)
{
	return SimMath::fistp32(SimMath::ceilD(SimMath::pc24MulW(SimMath::pc24AddW((double)hi, (double)0.1f), (double)0.1f)));
}
} // namespace

PathfindGridView Pathfinder::gridView()
{
	PathfindGridView v;
	v.cells = m_blockOfMapCells.data();
	v.stride = m_stride;
	if (m_anyRaised)
	{
		for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
		{
			RaisedLayer &r = m_raised[(size_t)l];
			if (r.used() && !r.cells.empty())
			{
				v.raised.push_back(PathfindRaisedView{ l, r.cells.data(), r.cells.size() });
			}
		}
	}
	return v;
}

const PathfindCell *Pathfinder::raisedCellAt(PathfindLayerEnum layer, int x, int y) const
{
	if (!isRaisedLayer(layer))
	{
		return nullptr;
	}
	const RaisedLayer &l = m_raised[(size_t)layer];
	const int dx = x - l.origin.x, dy = y - l.origin.y;
	if (l.cells.empty() || dx < 0 || dy < 0 || dx >= l.width || dy >= l.depth)
	{
		return nullptr;
	}
	return &l.cells[(size_t)dx * (size_t)l.depth + (size_t)dy];
}

bool Pathfinder::raisedLayerInUse(PathfindLayerEnum layer) const
{
	return isRaisedLayer(layer) && m_raised[(size_t)layer].used();
}

float Pathfinder::raisedLayerHeight(PathfindLayerEnum layer) const
{
	return raisedLayerInUse(layer) ? SimMath::sseFromInt32(m_raised[(size_t)layer].height) : 0.0f;
}

size_t Pathfinder::raisedLayerCount() const
{
	size_t n = 0;
	for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
	{
		n += m_raised[(size_t)l].used() ? 1 : 0;
	}
	return n;
}

PathfindLayerEnum Pathfinder::raisedLayerFor(const PathfindWallSurface &s, PathfindObjectID owner)
{
	// RW 0x935FEA .. 0x9360E4
	for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
	{
		RaisedLayer &r = m_raised[(size_t)l];
		if (!r.used())
		{
			continue;
		}
		bool near = false;
		for (const auto &a : r.areas)
		{
			near = near || boundsNear(a.second, s);
		}
		// x87: fild the height, fsub the polygon's height (dword), the CRT fabs, compared with 3.5
		if (near && SimMath::absD(SimMath::subD((double)r.height, (double)s.height)) < (double)kRaisedSameHeight)
		{
			r.areas.emplace_back(owner, s); // AddArea RW 0x768276
			return (PathfindLayerEnum)l;
		}
	}
	for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
	{
		RaisedLayer &r = m_raised[(size_t)l];
		if (!r.used())
		{
			r.areas.emplace_back(owner, s); // RW 0x768246
			r.height = SimMath::cvttss2si(s.height); // INFERENCE (S-4856): the polygon's int + 0x30
			m_anyRaised = true;
			return (PathfindLayerEnum)l;
		}
	}
	note(pathstops::kRaisedLayerCount); // RW: no layer is left (RW 0x93606A .. 0x93607E: the polygon is dropped)
	return LAYER_INVALID;
}

void Pathfinder::rebuildRaisedLayer(PathfindLayerEnum layer)
{
	// review (#63, #64): the units' reservations on this layer are released while the old cells are there (a footprint over the walkway's edge holds
	// ground cells too, which the rebuild keeps: registering it again alone left a second, stale node on them), then registered again on the new
	// cells, in unit id order (m_units is a map), so the occupant lists are the same on every machine
	std::vector<std::pair<PathfindObjectID, Slot *>> again;
	for (auto &kv : m_units)
	{
		for (Slot *s : { &kv.second.pos, &kv.second.goal })
		{
			if (s->occupied && s->layer == layer && !s->cells.empty()) // a horde goal keeps no cells (updateGoalAngle)
			{
				releaseSlot(kv.first, *s);
				again.push_back({ kv.first, s });
			}
		}
	}
	rebuildRaisedCells(layer);
	for (const auto &e : again)
	{
		if (PathfindObject *o = m_world ? m_world->findObjectByID(e.first) : nullptr)
		{
			e.second->occupied = true;
			registerSlot(*o, *e.second);
		}
	}
}

void Pathfinder::rebuildRaisedCells(PathfindLayerEnum layer)
{
	RaisedLayer &r = m_raised[(size_t)layer];
	// RW 0x76848F: the cells go (their records released)
	for (PathfindCell &c : r.cells)
	{
		c.reset(&m_pool);
	}
	r.cells.clear();
	r.width = r.depth = 0;
	r.origin = ICoord2D{ 0, 0 };
	if (!r.used())
	{
		m_anyRaised = false;
		for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
		{
			m_anyRaised = m_anyRaised || m_raised[(size_t)l].used();
		}
		return;
	}
	// RW 0x7684F8: the union of the areas' bounds
	float lox = r.areas.front().second.loX, loy = r.areas.front().second.loY, hix = r.areas.front().second.hiX, hiy = r.areas.front().second.hiY;
	for (const auto &a : r.areas)
	{
		lox = std::min(lox, a.second.loX);
		loy = std::min(loy, a.second.loY);
		hix = std::max(hix, a.second.hiX);
		hiy = std::max(hiy, a.second.hiY);
	}
	int x0 = cellFloor(lox) - 1, y0 = cellFloor(loy) - 1;
	int x1 = cellCeil(hix) + 1, y1 = cellCeil(hiy) + 1;
	x0 = std::max(x0, m_extent.lo.x);
	y0 = std::max(y0, m_extent.lo.y);
	x1 = std::min(x1, m_extent.hi.x);
	y1 = std::min(y1, m_extent.hi.y);
	if (x1 <= x0 || y1 <= y0)
	{
		return;
	}
	r.origin = ICoord2D{ x0, y0 };
	r.width = x1 - x0;
	r.depth = y1 - y0;
	r.cells.assign((size_t)r.width * (size_t)r.depth, PathfindCell());
	// ClassifyWallCells RW 0x768832
	for (int i = 0; i < r.width; ++i)
	{
		for (int j = 0; j < r.depth; ++j)
		{
			PathfindCell &c = r.cells[(size_t)i * (size_t)r.depth + (size_t)j];
			const float cx0 = SimMath::mulf32(SimMath::sseFromInt32(r.origin.x + i), 10.0f), cy0 = SimMath::mulf32(SimMath::sseFromInt32(r.origin.y + j), 10.0f);
			const float cx1 = SimMath::addf32(cx0, 10.0f), cy1 = SimMath::addf32(cy0, 10.0f);
			int inside = 0;
			const float xs[4] = { cx0, cx0, cx1, cx1 }, ys[4] = { cy0, cy1, cy1, cy0 };
			for (int k = 0; k < 4; ++k)
			{
				float z = 0.0f;
				for (const auto &a : r.areas)
				{
					if (a.second.heightAt(xs[k], ys[k], z))
					{
						++inside;
						break;
					}
				}
			}
			c.reset(nullptr);
			c.setLayer(layer);
			c.setConnectLayer(LAYER_INVALID);
			c.setType(inside > 0 ? PathfindCell::CELL_CLEAR : PathfindCell::CELL_BRIDGE_IMPASSABLE);
		}
	}
	m_zoneManager.markDirty(PathfindRegion{ ICoord2D{ x0, y0 }, ICoord2D{ x1, y1 } });
}

void Pathfinder::connectRampToRaised(int i, int j, PathfindCell *c, float rampHeight, int corners)
{
	// BFME2 WB gd_0052E914, the partly covered cell with a raised layer cell: the cell is a ramp cell connecting to the raised layer, the raised cell connects
	// to the ramp (INFERENCE S-4856: every raised layer with a passable cell there, the first in layer order)
	if (!m_anyRaised || corners >= 4)
	{
		return;
	}
	for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
	{
		if (!m_raised[(size_t)l].used())
		{
			continue;
		}
		PathfindCell *rc = raisedCell((PathfindLayerEnum)l, i, j);
		if (!rc || rc->getType() != PathfindCell::CELL_CLEAR)
		{
			continue;
		}
		const float h = raisedLayerHeight((PathfindLayerEnum)l);
		if (SimMath::absD(SimMath::subD((double)rampHeight, (double)h)) >= (double)kRampJoinTolerance)
		{
			continue;
		}
		c->setLayer(LAYER_RAMP);
		c->setConnectLayer((PathfindLayerEnum)l);
		rc->setConnectLayer(LAYER_RAMP);
		note(pathstops::kRaisedLayers);
		return;
	}
}

void Pathfinder::crcRaised(StateHasher &h) const
{
	if (!m_anyRaised)
	{
		return; // a map without raised layers hashes as before
	}
	h.addU32(0x52414953u); // "RAIS"
	for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
	{
		const RaisedLayer &r = m_raised[(size_t)l];
		if (!r.used())
		{
			continue;
		}
		h.addI32(l);
		h.addI32(r.height);
		h.addU32((std::uint32_t)r.areas.size());
		h.addI32(r.origin.x);
		h.addI32(r.origin.y);
		h.addI32(r.width);
		h.addI32(r.depth);
		for (const PathfindCell &c : r.cells)
		{
			h.addU32(c.getType());
			h.addU32(c.getConnectLayer());
			h.addU32(c.getZone());
			// the kept info as the ground cells' (Pathfinder::crc): the walkway's goal and position reservations decide which raised cells a search finds free
			const bool info = c.hasInfo();
			h.addBool(info);
			if (!info)
			{
				continue;
			}
			h.addU32((std::uint32_t)c.getObstacleID());
			h.addU32((c.isObstacleFence() ? 1u : 0u) | (c.isObstacleTransparent() ? 2u : 0u) | (c.isBlockedByAlly() ? 4u : 0u));
			for (int k = 0; k < OCC_KIND_COUNT; ++k)
			{
				std::uint32_t n = 0;
				for (const PathfindOccupant *o = c.occupants((PathfindOccupantKind)k); o; o = o->next)
				{
					h.addU32((std::uint32_t)o->owner); // the chain in list order: the order is history
					++n;
				}
				h.addU32(n);
			}
		}
	}
}
