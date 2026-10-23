// OpenBFME. GPL-3.0.
//
// Wall tops and ramps in the pathfinder's ground grid (lane WALL-1: units walk up the ramps of Helm's Deep, the castle walls and the fortress walls onto
// their tops). RotWK does not use ZH's one flat wall layer (ZH AIPathfind.cpp:3410-3825, AIData WallHeight): every wall top height has a layer id of its own
// (0x11 .. 0x40) and every ramp is layer 0x10, both written into the GROUND grid's cells (the layer bits 4 .. 9 of the cell dword).
//
// DONOR (BFME2 WorldBuilder decomp, reverse/wb/wb_decomp.jsonl.xz with symbols and asserts; BFME2 1.06 addresses through reverse/wb/matches_full; the RotWK
// addresses are matched by distance only, S-001 caveat):
//   * Pathfinder::ClassifyWall (WB 0x12db220, BFME2 0x92F63F, pathfinder_classify.cpp:722), called by the structure footprint routine (BFME2 0x930212, RotWK
//     RW 0x936B7D = Pathfinder::classifyObjectFootprint) for an object whose drawable has a WallBoundsMesh / RampMesh1 / RampMesh2:
//       - WallBoundsMesh: the height h is the mesh's world z; the layer is the wall id whose height H has |h - H| < 3.5, else a new id 0x11 + count (at
//         most 0x30); H becomes max(H, h). The cells floor(lo / 10) - 2 .. ceil(hi / 10) + 2: a cell one of whose four corners is inside the outline
//         (pointInTrigger) takes the wall's layer unless a taller wall has it; its connect layer is cleared, the pinched bit cleared, its type updated (the
//         wall's own obstacle removed: BFME2 0x92DFB1, INFERENCE).
//       - RampMesh1 / 2: a Bridge (W3DScriptedModelDraw::getRamp WB 0x938e80) on layer 0x10; a cell under the ramp joins a wall when the ramp's height
//         there is within 15 (RW 0xBDC6CC, 15.0) of the wall's (the cell keeps the wall's layer, its connect layer becomes 0x10), else it is layer 0x10.
//       - removal: the cells go back to layer 1, then the other wall pieces whose bounds overlap are classified again.
//   * the neighbour test of every search (examineNeighboringCells BFME2 0x6F83F5, the zone joins RW 0x9380BC): PathfindCell::layersCompatible.
//   * Pathfinder::GetWallHeight (WB 0xd3ed70, BFME2 0x6EF68C): getLayerHeight. Pathfinder::UpdateLayer (WB 0xd388b0, BFME2 0x6F11A1): updateLayer.
//
// INFERENCE (stop S-4840, docs/STOPS.md, raised as pathstops::kWallLayer):
//   * the outline polygon of a mesh (W3DScriptedModelDraw::getPolygon WB 0x93ead0: the boundary edges chained) is tested as the mesh's triangles (the same
//     point set for a mesh without holes), and a ramp's height is the ramp mesh's own surface (retail builds a Bridge from the mesh, from / to ends and a
//     width, and interpolates along it);
//   * the meshes are the pristine model of the draw module's default model state (retail asks the drawable's current render object: a damaged model's
//     sub meshes are not followed);
//   * how a ramp's foot meets the ground: retail's cell rules for it were not decoded; here a ramp cell whose ramp height is within 15 of the terrain
//     connects to the ground layer, and a ramp cell beside a wall cell within 15 of that wall's height connects to that wall;
//   * a wall-top or ramp cell drops bit 18; lane WALL-2 read RotWK's wall cell (RW 0x935051) and ramp cell (RW 0x93527F) rules: both SET bit 17 (the bit
//     that keeps a unit with CanPathThroughGates = No, RW template + 0x644 whose constructor default is 1, RW 0x74004B, off: the mumakil, Grond, the siege
//     towers), as the wall piece's footprint does (WALK_ON_TOP_OF_WALL);
//   * the pinch pass of ClassifyWall (a CLEAR cell with 2 of its 8 neighbours on another layer gets bit 16) is not applied;
//   * RaisedWallMesh (a walkway on a raised layer 2 .. 15 over a passable gate) is lane WALL-2's (AIPathfindRaised.cpp, S-4856), as are the portals
//     (AIPathfindPortals.cpp, S-4857).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <array>

namespace
{
const float kSameWallTolerance = 3.5f;  // ClassifyWall: two wall tops closer than this are one layer
const float kRampJoinTolerance = 15.0f; // RW 0xBDC6CC: a ramp meets a wall (or, INFERENCE, the ground) within this height
const float kLayerSwitch = 10.0f;       // UpdateLayer: a unit takes a wall's layer only within this height of the wall top
const int kMaxWallLayers = LAYER_WALL_LAST - LAYER_WALL_FIRST + 1;

float cornerX(int i, int k) { return SimMath::sseFromInt32((i + ((k == 1 || k == 2) ? 1 : 0)) * 10); }
float cornerY(int j, int k) { return SimMath::sseFromInt32((j + ((k >= 2) ? 1 : 0)) * 10); }
float centreOf(int i) { return SimMath::addf32(SimMath::sseFromInt32(i * 10), 5.0f); }

bool within(float a, float b, float tolerance)
{
	return SimMath::absD(SimMath::subD((double)a, (double)b)) <= (double)tolerance;
}

// the corners of cell (i, j) inside `s` and the mean surface height over them (the cell's centre height when the centre is inside)
int cellCoverage(const PathfindWallSurface &s, int i, int j, float &height)
{
	int n = 0;
	float sum = 0.0f;
	for (int k = 0; k < 4; ++k)
	{
		float z = 0.0f;
		if (s.heightAt(cornerX(i, k), cornerY(j, k), z))
		{
			++n;
			sum = SimMath::addf32(sum, z);
		}
	}
	if (n == 0)
	{
		return 0;
	}
	float zc = 0.0f;
	height = s.heightAt(centreOf(i), centreOf(j), zc) ? zc : SimMath::divf32(sum, SimMath::sseFromInt32(n));
	return n;
}

bool overlaps(const PathfindRegion &a, const PathfindRegion &b)
{
	return a.lo.x <= b.hi.x && b.lo.x <= a.hi.x && a.lo.y <= b.hi.y && b.lo.y <= a.hi.y;
}
} // namespace

bool PathfindWallSurface::heightAt(float x, float y, float &z) const
{
	if (x < loX || x > hiX || y < loY || y > hiY)
	{
		return false;
	}
	using SimMath::addf32;
	using SimMath::divf32;
	using SimMath::mulf32;
	using SimMath::subf32;
	for (const PathfindWallTriangle &t : triangles)
	{
		const Coord3D &a = t.v[0], &b = t.v[1], &c = t.v[2];
		// the edge functions: the signed doubled areas of (edge, point); the point is inside when none has the other sign
		const float dab = subf32(mulf32(subf32(b.x, a.x), subf32(y, a.y)), mulf32(subf32(b.y, a.y), subf32(x, a.x)));
		const float dbc = subf32(mulf32(subf32(c.x, b.x), subf32(y, b.y)), mulf32(subf32(c.y, b.y), subf32(x, b.x)));
		const float dca = subf32(mulf32(subf32(a.x, c.x), subf32(y, c.y)), mulf32(subf32(a.y, c.y), subf32(x, c.x)));
		const bool neg = dab < 0.0f || dbc < 0.0f || dca < 0.0f;
		const bool pos = dab > 0.0f || dbc > 0.0f || dca > 0.0f;
		if (neg && pos)
		{
			continue;
		}
		const float area = addf32(addf32(dab, dbc), dca);
		if (area == 0.0f)
		{
			continue; // a triangle seen edge on
		}
		// barycentric weights: the area opposite a vertex over the whole
		z = addf32(addf32(mulf32(a.z, divf32(dbc, area)), mulf32(b.z, divf32(dca, area))), mulf32(c.z, divf32(dab, area)));
		return true;
	}
	return false;
}

PathfindRegion Pathfinder::surfaceCells(const PathfindWallSurface &s) const
{
	PathfindRegion r;
	r.lo.x = std::max(SimMath::floorToInt(SimMath::mulf32(s.loX, 0.1f)) - 2, m_extent.lo.x);
	r.lo.y = std::max(SimMath::floorToInt(SimMath::mulf32(s.loY, 0.1f)) - 2, m_extent.lo.y);
	r.hi.x = std::min(SimMath::ceilToInt(SimMath::mulf32(s.hiX, 0.1f)) + 2, m_extent.hi.x);
	r.hi.y = std::min(SimMath::ceilToInt(SimMath::mulf32(s.hiY, 0.1f)) + 2, m_extent.hi.y);
	return r;
}

float Pathfinder::wallHeightOf(PathfindLayerEnum layer) const
{
	const int k = (int)layer - (int)LAYER_WALL_FIRST;
	return isWallLayer(layer) && k < (int)m_wallHeights.size() ? m_wallHeights[(size_t)k] : 0.0f;
}

PathfindLayerEnum Pathfinder::wallLayerFor(float height)
{
	for (size_t k = 0; k < m_wallHeights.size(); ++k)
	{
		if (SimMath::absD(SimMath::subD((double)height, (double)m_wallHeights[k])) < (double)kSameWallTolerance)
		{
			if (height > m_wallHeights[k])
			{
				m_wallHeights[k] = height;
			}
			return (PathfindLayerEnum)((int)LAYER_WALL_FIRST + (int)k);
		}
	}
	if ((int)m_wallHeights.size() >= kMaxWallLayers)
	{
		return LAYER_INVALID; // RW: at most 0x40 - 0x11 + 1 heights; the surface is left out (reported by the caller)
	}
	m_wallHeights.push_back(height);
	return (PathfindLayerEnum)((int)LAYER_WALL_FIRST + (int)m_wallHeights.size() - 1);
}

PathfindObjectID Pathfinder::wallPieceAt(const Coord3D &pos)
{
	const PathfindCell *c = getCell(LAYER_GROUND, &pos);
	if (!c || !isWallLayer(c->getLayer()))
	{
		return PATHFIND_INVALID_ID;
	}
	ICoord2D at;
	worldToCell(&pos, &at); // the cell of getCell(LAYER_GROUND, &pos) (a cell outside a search keeps no index of its own)
	const int x = at.x, y = at.y;
	for (const WallPiece &p : m_wallPieces)
	{
		if (x < p.cells.lo.x || x > p.cells.hi.x || y < p.cells.lo.y || y > p.cells.hi.y)
		{
			continue;
		}
		for (PathfindLayerEnum l : p.wallLayers)
		{
			if (l == c->getLayer())
			{
				return p.id;
			}
		}
	}
	return PATHFIND_INVALID_ID;
}

bool Pathfinder::isWallPiece(PathfindObjectID id) const
{
	if (id == PATHFIND_INVALID_ID)
	{
		return false;
	}
	for (const WallPiece &p : m_wallPieces)
	{
		if (p.id == id)
		{
			return true;
		}
	}
	return false;
}

size_t Pathfinder::rampCount() const
{
	size_t n = 0;
	for (const WallPiece &p : m_wallPieces)
	{
		n += p.ramps.size();
	}
	return n;
}

void Pathfinder::stampWallSurface(const PathfindWallSurface &s, PathfindLayerEnum layer)
{
	const PathfindRegion r = surfaceCells(s);
	const float h = wallHeightOf(layer);
	for (int i = r.lo.x; i <= r.hi.x; ++i)
	{
		for (int j = r.lo.y; j <= r.hi.y; ++j)
		{
			float z = 0.0f;
			if (cellCoverage(s, i, j, z) == 0)
			{
				continue;
			}
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			if (!c)
			{
				continue;
			}
			const PathfindLayerEnum cl = c->getLayer();
			if (isWallLayer(cl) && cl != layer && wallHeightOf(cl) > h)
			{
				continue; // a taller wall keeps the cell
			}
			const PathfindObjectID obs = c->getObstacleID();
			if (isWallPiece(obs))
			{
				c->removeObstacleId(m_pool, obs); // a wall piece's footprint does not cover a wall top (its own or a neighbour's)
			}
			c->setLayer(layer);
			c->setConnectLayer(LAYER_INVALID);
			c->setPinched(false);
			c->setBit17(true); // lane WALL-2: RW 0x935191 (the wall cell RW 0x935051): bit 17 set (the units without CanPathThroughGates stay off the wall)
			c->setImpassableToPlayers(false);
			c->setType(PathfindCell::CELL_CLEAR); // keeps another structure's obstacle (setType)
			c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
			if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
		}
	}
}

void Pathfinder::stampRampSurface(const PathfindWallSurface &s)
{
	const PathfindRegion r = surfaceCells(s);
	// pass 1: the joints (a wall cell the ramp reaches at the wall's height) and the ramp's own cells
	for (int i = r.lo.x; i <= r.hi.x; ++i)
	{
		for (int j = r.lo.y; j <= r.hi.y; ++j)
		{
			float h = 0.0f;
			const int corners = cellCoverage(s, i, j, h);
			if (corners == 0)
			{
				continue;
			}
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			if (!c)
			{
				continue;
			}
			const PathfindLayerEnum cl = c->getLayer();
			c->setBit17(true); // lane WALL-2: RW 0x9353A4 (the ramp cell RW 0x93527F): every cell a ramp covers takes bit 17
			if (isWallLayer(cl) && within(h, wallHeightOf(cl), kRampJoinTolerance))
			{
				c->setConnectLayer(LAYER_RAMP);
			}
			else
			{
				const PathfindObjectID obs = c->getObstacleID();
				if (isWallPiece(obs))
				{
					c->removeObstacleId(m_pool, obs);
				}
				const float ground = m_terrain ? m_terrain->getGroundHeight(centreOf(i), centreOf(j)) : 0.0f;
				c->setLayer(LAYER_RAMP);
				c->setConnectLayer(within(h, ground, kRampJoinTolerance) ? LAYER_GROUND : LAYER_INVALID);
				c->setPinched(false);
				c->setImpassableToPlayers(false);
				c->setType(PathfindCell::CELL_CLEAR);
			}
			connectRampToRaised(i, j, c, h, corners); // lane WALL-2: a ramp's edge under a raised walkway at its height joins the walkway
			c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
			if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
		}
	}
	// pass 2: a ramp cell beside a wall cell at the ramp's height there connects to that wall (the ramp's top edge meets a wall it does not overlap)
	for (int i = r.lo.x; i <= r.hi.x; ++i)
	{
		for (int j = r.lo.y; j <= r.hi.y; ++j)
		{
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			float h = 0.0f;
			if (!c || c->getLayer() != LAYER_RAMP || c->getConnectLayer() != LAYER_INVALID || cellCoverage(s, i, j, h) == 0)
			{
				continue;
			}
			for (int k = 0; k < 8; ++k)
			{
				static const int dx[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
				static const int dy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
				const PathfindCell *n = getCell(LAYER_GROUND, i + dx[k], j + dy[k]);
				if (n && isWallLayer(n->getLayer()) && within(h, wallHeightOf(n->getLayer()), kRampJoinTolerance))
				{
					c->setConnectLayer(n->getLayer());
					break;
				}
			}
		}
	}
}

void Pathfinder::restampWalls(const PathfindRegion &region)
{
	// lane WALL-2: the raised layers whose pieces meet the region are made again (their cells, RW 0x7684F8 / 0x768832), before the ramps join them
	bool rebuilt = false;
	if (m_anyRaised)
	{
		std::array<bool, 16> touched{};
		for (const WallPiece &p : m_wallPieces)
		{
			if (overlaps(p.cells, region))
			{
				for (PathfindLayerEnum l : p.raisedLayers)
				{
					touched[(size_t)l] = true;
				}
			}
		}
		for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
		{
			if (touched[(size_t)l])
			{
				rebuildRaisedLayer((PathfindLayerEnum)l);
				rebuilt = true;
			}
		}
	}
	// every piece whose cells meet the region, in insertion order: the wall tops first (a taller one keeps its cells), then the ramps (their joints read
	// the wall tops)
	for (const WallPiece &p : m_wallPieces)
	{
		if (overlaps(p.cells, region))
		{
			for (size_t k = 0; k < p.walls.size(); ++k)
			{
				stampWallSurface(p.walls[k], p.wallLayers[k]);
			}
		}
	}
	// a rebuilt raised layer lost every ramp's joint on it, the far ones too (review #63): then every ramp, in insertion order as a full classification
	for (const WallPiece &p : m_wallPieces)
	{
		if (rebuilt || overlaps(p.cells, region))
		{
			for (const PathfindWallSurface &s : p.ramps)
			{
				stampRampSurface(s);
			}
		}
	}
}

void Pathfinder::classifyWall(PathfindObject &obj, bool insert)
{
	const PathfindObjectID id = obj.getID();
	if (!insert)
	{
		auto it = std::find_if(m_wallPieces.begin(), m_wallPieces.end(), [&](const WallPiece &p) { return p.id == id; });
		if (it == m_wallPieces.end())
		{
			return;
		}
		const WallPiece piece = *it;
		m_wallPieces.erase(it);
		// the piece's cells go back to the ground (their terrain class again: RW 0x934A85 keeps an obstacle), then the overlapping pieces are classified again
		auto unstamp = [&](const PathfindWallSurface &s, auto &&isMine) {
			const PathfindRegion r = surfaceCells(s);
			for (int i = r.lo.x; i <= r.hi.x; ++i)
			{
				for (int j = r.lo.y; j <= r.hi.y; ++j)
				{
					float h = 0.0f;
					PathfindCell *c = getCell(LAYER_GROUND, i, j);
					if (!c || cellCoverage(s, i, j, h) == 0)
					{
						continue;
					}
					if (isMine(*c))
					{
						c->setLayer(LAYER_GROUND);
						c->setConnectLayer(LAYER_INVALID);
						c->setBit17(false);
						classifyMapCell(i, j, c);
					}
					else if (c->getConnectLayer() == LAYER_RAMP)
					{
						c->setConnectLayer(LAYER_INVALID); // a joint of this piece's ramp
					}
					c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
				}
			}
		};
		for (size_t k = 0; k < piece.walls.size(); ++k)
		{
			const PathfindLayerEnum layer = piece.wallLayers[k];
			unstamp(piece.walls[k], [&](const PathfindCell &c) { return c.getLayer() == layer; });
		}
		for (const PathfindWallSurface &s : piece.ramps)
		{
			unstamp(s, [](const PathfindCell &c) { return c.getLayer() == LAYER_RAMP; });
		}
		// lane WALL-2: the piece's raised areas leave their layers (RW: the layer is freed and its cells made again; an empty layer is no longer in use)
		for (PathfindLayerEnum l : piece.raisedLayers)
		{
			RaisedLayer &r = m_raised[(size_t)l];
			r.areas.erase(std::remove_if(r.areas.begin(), r.areas.end(), [&](const std::pair<PathfindObjectID, PathfindWallSurface> &a) { return a.first == id; }),
				r.areas.end());
			rebuildRaisedLayer(l);
		}
		restampWalls(piece.cells);
		return;
	}
	const PathfindWallGeometry *geom = obj.getWallGeometry();
	if (!geom || geom->empty())
	{
		return;
	}
	if (isWallPiece(id))
	{
		classifyWall(obj, false); // classified again (a moved or re-added piece)
	}
	note(pathstops::kWallLayer);
	WallPiece piece;
	piece.id = id;
	bool first = true;
	auto grow = [&](const PathfindWallSurface &s) {
		const PathfindRegion r = surfaceCells(s);
		if (first)
		{
			piece.cells = r;
			first = false;
			return;
		}
		piece.cells.lo.x = std::min(piece.cells.lo.x, r.lo.x);
		piece.cells.lo.y = std::min(piece.cells.lo.y, r.lo.y);
		piece.cells.hi.x = std::max(piece.cells.hi.x, r.hi.x);
		piece.cells.hi.y = std::max(piece.cells.hi.y, r.hi.y);
	};
	for (const PathfindWallSurface &s : geom->walls)
	{
		const PathfindLayerEnum layer = wallLayerFor(s.height);
		if (layer == LAYER_INVALID)
		{
			note(pathstops::kWallLayerCount);
			continue;
		}
		piece.walls.push_back(s);
		piece.wallLayers.push_back(layer);
		grow(s);
	}
	for (const PathfindWallSurface &s : geom->ramps)
	{
		piece.ramps.push_back(s);
		grow(s);
	}
	// lane WALL-2: RaisedWallMesh (RW 0x935FEA .. 0x9360E4): the layer of the same height whose areas come near, else a new layer
	for (const PathfindWallSurface &s : geom->raised)
	{
		const PathfindLayerEnum layer = raisedLayerFor(s, id);
		if (layer == LAYER_INVALID)
		{
			continue;
		}
		note(pathstops::kRaisedLayers);
		piece.raised.push_back(s);
		piece.raisedLayers.push_back(layer);
		grow(s);
	}
	if (first)
	{
		return;
	}
	m_wallPieces.push_back(piece);
	restampWalls(piece.cells);
}

bool Pathfinder::isPointOnRamp(float x, float y, float *z) const
{
	for (const WallPiece &p : m_wallPieces)
	{
		for (const PathfindWallSurface &s : p.ramps)
		{
			float h = 0.0f;
			if (s.heightAt(x, y, h))
			{
				if (z) *z = h;
				return true;
			}
		}
	}
	return false;
}

bool Pathfinder::isPointOnWall(const Coord3D &pos, bool requireClear) const
{
	if (m_wallPieces.empty() || m_blockOfMapCells.empty())
	{
		return false;
	}
	const int x = SimMath::floorToInt(SimMath::mulf32(pos.x, 0.1f));
	const int y = SimMath::floorToInt(SimMath::mulf32(pos.y, 0.1f));
	const PathfindCell *c = cellAt(x, y);
	return c && isWallLayer(c->getLayer()) && (!requireClear || c->getType() == PathfindCell::CELL_CLEAR);
}

PathfindLayerEnum Pathfinder::getLayerAt(float x, float y) const
{
	return getLayerAt(x, y, m_terrain ? m_terrain->getGroundHeight(x, y) : 0.0f);
}

PathfindLayerEnum Pathfinder::getLayerAt(float x, float y, float z) const
{
	if (m_wallPieces.empty() || m_blockOfMapCells.empty())
	{
		return LAYER_GROUND;
	}
	const int cx = SimMath::floorToInt(SimMath::mulf32(x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(y, 0.1f));
	const PathfindCell *c = cellAt(cx, cy);
	if (isPointOnRamp(x, y))
	{
		return LAYER_RAMP; // TerrainLogic::getLayerForDestination RW 0x680A75 (BFME2 0x6802FE): a ramp first (RW 0x6E89EE)
	}
	if (m_anyRaised)
	{
		// lane WALL-2, RW 0x6EE600: the first raised layer in use whose cell there is passable and whose height is less than 25 (RW 0xC69574) above z
		for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
		{
			const PathfindCell *rc = raisedCellAt((PathfindLayerEnum)l, cx, cy);
			if (rc && rc->getType() != PathfindCell::CELL_BRIDGE_IMPASSABLE && SimMath::subf32(raisedLayerHeight((PathfindLayerEnum)l), z) < 25.0f)
			{
				return (PathfindLayerEnum)l;
			}
		}
	}
	return c && (isWallLayer(c->getLayer()) || c->getLayer() == LAYER_RAMP) ? c->getLayer() : LAYER_GROUND;
}

float Pathfinder::getLayerHeight(float x, float y, PathfindLayerEnum layer) const
{
	const float terrain = m_terrain ? m_terrain->getGroundHeight(x, y) : 0.0f;
	if (m_wallPieces.empty())
	{
		return terrain;
	}
	if (isWallLayer(layer))
	{
		const int k = (int)layer - (int)LAYER_WALL_FIRST;
		return k < (int)m_wallHeights.size() ? m_wallHeights[(size_t)k] : terrain;
	}
	if (isRaisedLayer(layer))
	{
		return raisedLayerInUse(layer) ? raisedLayerHeight(layer) : terrain; // lane WALL-2: GetWallLayerHeight (BFME2 WB 0xd3ec10)
	}
	if (layer == LAYER_RAMP)
	{
		// the ramp under the point (raised to the wall the cell belongs to), else the ramp under one of the four points 10 away, else the cell's wall
		const PathfindCell *c = cellAt(SimMath::floorToInt(SimMath::mulf32(x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(y, 0.1f)));
		const float wall = c && isWallLayer(c->getLayer()) ? wallHeightOf(c->getLayer()) : terrain;
		float z = 0.0f;
		if (isPointOnRamp(x, y, &z))
		{
			return c && isWallLayer(c->getLayer()) && wall > z ? wall : z;
		}
		static const float dx[4] = { -10.0f, 10.0f, -10.0f, 10.0f };
		static const float dy[4] = { -10.0f, -10.0f, 10.0f, 10.0f };
		for (int k = 0; k < 4; ++k)
		{
			if (isPointOnRamp(SimMath::addf32(x, dx[k]), SimMath::addf32(y, dy[k]), &z))
			{
				return z;
			}
		}
		return wall;
	}
	return terrain;
}

float Pathfinder::getHighestLayerHeight(float x, float y) const
{
	const float terrain = m_terrain ? m_terrain->getGroundHeight(x, y) : 0.0f;
	if (m_wallPieces.empty())
	{
		return terrain;
	}
	// the highest surface at the point (ZH TerrainLogic::getHighestLayerForDestination; review #63: getLayerAt names a ramp first and the first raised
	// layer in index order, not the highest): the terrain, a ramp's surface, the cell's wall top, every raised walkway passable there
	float best = terrain;
	const int cx = SimMath::floorToInt(SimMath::mulf32(x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(y, 0.1f));
	auto take = [&](float h) {
		if (h > best)
		{
			best = h;
		}
	};
	if (isPointOnRamp(x, y))
	{
		take(getLayerHeight(x, y, LAYER_RAMP));
	}
	if (const PathfindCell *c = m_blockOfMapCells.empty() ? nullptr : cellAt(cx, cy))
	{
		if (isWallLayer(c->getLayer()))
		{
			take(getLayerHeight(x, y, c->getLayer()));
		}
	}
	if (m_anyRaised)
	{
		for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
		{
			const PathfindCell *rc = raisedCellAt((PathfindLayerEnum)l, cx, cy);
			if (rc && rc->getType() != PathfindCell::CELL_BRIDGE_IMPASSABLE)
			{
				take(raisedLayerHeight((PathfindLayerEnum)l));
			}
		}
	}
	return best;
}

PathfindLayerEnum Pathfinder::searchDestinationLayer(const Coord3D &to) const
{
	const PathfindLayerEnum l = getLayerAt(to.x, to.y, to.z);
	return isRaisedLayer(l) ? l : LAYER_GROUND;
}

PathfindLayerEnum Pathfinder::updateLayer(const PathfindObject &obj, PathfindLayerEnum nodeLayer) const
{
	PathfindLayerEnum cur = obj.getLayer();
	if (m_wallPieces.empty())
	{
		return cur;
	}
	// BFME2 0x6F11A1: the mode from the node's layer (later rules win), then the unit's layer
	const Coord3D &pos = obj.getPosition();
	const bool onRamp = isPointOnRamp(pos.x, pos.y);
	if (isRaisedLayer(nodeLayer) || isRaisedLayer(cur))
	{
		// lane WALL-2 (INFERENCE S-4856): a node on a raised walkway: the unit takes the walkway's layer once off the ramp's surface where the walkway has a cell;
		// a unit on a walkway keeps it while the walkway has a cell under it, else it is on the ramp it walks down or on the ground
		const int cx = SimMath::floorToInt(SimMath::mulf32(pos.x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(pos.y, 0.1f));
		if (isRaisedLayer(nodeLayer))
		{
			const PathfindCell *rc = raisedCellAt(nodeLayer, cx, cy);
			if (onRamp && !isRaisedLayer(cur))
			{
				return LAYER_RAMP;
			}
			if (rc && rc->getType() != PathfindCell::CELL_BRIDGE_IMPASSABLE)
			{
				return nodeLayer;
			}
			return cur;
		}
		const PathfindCell *rc = raisedCellAt(cur, cx, cy);
		if (rc && rc->getType() != PathfindCell::CELL_BRIDGE_IMPASSABLE && !(nodeLayer == LAYER_RAMP && onRamp))
		{
			return cur;
		}
		return onRamp ? LAYER_RAMP : LAYER_GROUND;
	}
	int mode = 3;
	if (nodeLayer == LAYER_RAMP || (nodeLayer == LAYER_GROUND && isWallLayer(cur)))
	{
		mode = 1;
	}
	if (isWallLayer(nodeLayer))
	{
		mode = 2;
		if (!within(pos.z, getLayerHeight(pos.x, pos.y, nodeLayer), kLayerSwitch) || cur == LAYER_GROUND)
		{
			mode = 1;
		}
	}
	switch (mode)
	{
	case 1:
		if (onRamp)
		{
			cur = LAYER_RAMP;
		}
		else if (cur == LAYER_RAMP)
		{
			cur = LAYER_GROUND;
		}
		break;
	case 2:
		cur = onRamp ? LAYER_RAMP : nodeLayer;
		break;
	default:
		cur = cur == LAYER_RAMP && onRamp ? LAYER_RAMP : LAYER_GROUND;
		break;
	}
	return cur;
}

PathfindLayerEnum Pathfinder::settleLayer(const PathfindObject &obj) const
{
	const PathfindLayerEnum cur = obj.getLayer();
	if (m_wallPieces.empty() || m_blockOfMapCells.empty())
	{
		return cur == LAYER_GROUND ? cur : LAYER_GROUND; // no wall left: every unit is on the ground
	}
	const Coord3D &pos = obj.getPosition();
	const PathfindCell *c = cellAt(SimMath::floorToInt(SimMath::mulf32(pos.x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(pos.y, 0.1f)));
	if (!c)
	{
		return cur;
	}
	const PathfindLayerEnum cl = c->getLayer();
	const bool onRamp = isPointOnRamp(pos.x, pos.y);
	if (isRaisedLayer(cur))
	{
		// lane WALL-2 (INFERENCE S-4856): on a walkway while it has a passable cell under the unit; else the ramp under it, else the ground grid's layer
		const PathfindCell *rc = raisedCellAt(cur, SimMath::floorToInt(SimMath::mulf32(pos.x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(pos.y, 0.1f)));
		if (rc && rc->getType() != PathfindCell::CELL_BRIDGE_IMPASSABLE)
		{
			return cur;
		}
		return onRamp ? LAYER_RAMP : (isWallLayer(cl) ? cl : LAYER_GROUND);
	}
	if (cur == LAYER_GROUND)
	{
		if (isWallLayer(cl))
		{
			return cl; // put on a wall top
		}
		// a unit walking straight (a horde member to its slot) onto a ramp: the ramp (UpdateLayer's mode 1 for a unit without a path node). A ramp cell that
		// does not connect to the ground is up the ramp: a ground unit on it is on the ramp even where the cell's edge is off the ramp's surface
		return cl == LAYER_RAMP && (onRamp || c->getConnectLayer() != LAYER_GROUND) ? LAYER_RAMP : cur;
	}
	if (cur == LAYER_RAMP)
	{
		if (onRamp)
		{
			return cur;
		}
		if (m_anyRaised)
		{
			// lane WALL-2: off the ramp's top onto a walkway at the unit's height
			const int cx = SimMath::floorToInt(SimMath::mulf32(pos.x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(pos.y, 0.1f));
			for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
			{
				const PathfindCell *rc = raisedCellAt((PathfindLayerEnum)l, cx, cy);
				if (rc && rc->getType() != PathfindCell::CELL_BRIDGE_IMPASSABLE && within(pos.z, raisedLayerHeight((PathfindLayerEnum)l), kLayerSwitch))
				{
					return (PathfindLayerEnum)l;
				}
			}
		}
		if (isWallLayer(cl) && within(pos.z, wallHeightOf(cl), kLayerSwitch))
		{
			return cl; // off the ramp's top onto the wall (UpdateLayer's mode 2)
		}
		return cl == LAYER_GROUND ? LAYER_GROUND : cur;
	}
	if (isWallLayer(cur) && !c->allowsLayer(cur))
	{
		if (cl == LAYER_RAMP)
		{
			return onRamp ? LAYER_RAMP : cur; // from the wall down onto its ramp
		}
		if (!isWallLayer(cl))
		{
			return LAYER_GROUND; // the wall under the unit is gone
		}
	}
	return cur;
}
