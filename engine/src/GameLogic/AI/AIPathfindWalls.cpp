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
//       - RampMesh1 / 2: a Bridge (W3DScriptedModelDraw::getRamp WB 0x938e80) on layer 0x10; a cell at the ramp's start or end edge joins a wall (or the
//         ground) when the ramp's height there is within 15 (RW 0xBDC6CC, 15.0) of that layer's (the cell keeps its layer, its connect layer becomes 0x10),
//         else it is layer 0x10 (RotWK RW 0x93527F, read in full for this file's stampRampSurface).
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
//   * a ramp's cells follow RotWK's ramp cell RW 0x93527F on the Bridge getRamp builds (makeRampBridge, stampRampSurface): a cell all four of whose
//     corners are on the ramp is the ramp's; a partly covered cell at the ramp's start or end edge joins a raised layer at the ramp's height, else keeps
//     its own layer (ground or wall) connected to the ramp when that layer is within 15 of the ramp at the cell's corner, else is the ramp's; a partly
//     covered cell along a side is type 5 on the ramp's layer. The Bridge's vertices are the mesh triangles' corners (retail walks the vertex array);
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

// ---- RotWK's ramp: a Bridge quad built from the RampMesh -----------------------------------------------------------------------------------------------
// W3DScriptedModelDraw::getRamp (BFME2 RVA 0xB9EC2, decomp W3DScriptedModelDrawGetRamp.cpp; WB 0x938E80): over the mesh's vertices the z range gives a band of
// 1/50 of it at the bottom and at the top; in each band the two mutually farthest vertices are found (two passes); the top pair's midpoint is `from`, the
// bottom pair's `to`, the mean of the two spans the width. A mesh of more than two polygons gives the four corners as those vertices rounded toward zero
// (fromLeft / fromRight the top pair, toLeft / toRight the bottom pair); else `from` / `to` offset by half the width along the XY perpendicular. The Bridge
// (RotWK ctor RW 0x67FFA6) keeps the corners at + 0x28 / + 0x34 / + 0x40 / + 0x4C and their 2D bounds at + 0xB4 .. + 0xC0.
// INFERENCE (S-4840): the port has the mesh as world-space triangles; the vertices are their corners in first-seen order (retail walks the mesh's vertex array
// in model space and transforms the chosen ones: the same points; a tie may pick another vertex)
struct RampBridge
{
	bool valid = false;
	Coord3D from, to; // the middles of the high and the low band (getRamp's info->from / info->to)
	Coord3D fromLeft, fromRight, toLeft, toRight;
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
};

float dist2(const Coord3D &a, const Coord3D &b)
{
	return SimMath::sumSquares3(SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y), SimMath::subf32(a.z, b.z));
}

Coord3D truncated(const Coord3D &v)
{
	return Coord3D{ SimMath::sseFromInt32(SimMath::truncToInt32(v.x)), SimMath::sseFromInt32(SimMath::truncToInt32(v.y)), SimMath::sseFromInt32(SimMath::truncToInt32(v.z)) };
}

Coord3D midpoint(const Coord3D &a, const Coord3D &b)
{
	return Coord3D{ SimMath::mulf32(SimMath::addf32(a.x, b.x), 0.5f), SimMath::mulf32(SimMath::addf32(a.y, b.y), 0.5f), SimMath::mulf32(SimMath::addf32(a.z, b.z), 0.5f) };
}

float span(const Coord3D &a, const Coord3D &b)
{
	return (float)SimMath::length3d(SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y), SimMath::subf32(a.z, b.z));
}

RampBridge makeRampBridge(const PathfindWallSurface &s)
{
	RampBridge b;
	std::vector<Coord3D> v;
	for (const PathfindWallTriangle &t : s.triangles)
	{
		for (const Coord3D &p : t.v)
		{
			bool seen = false;
			for (const Coord3D &q : v)
			{
				seen = seen || (q.x == p.x && q.y == p.y && q.z == p.z);
			}
			if (!seen)
			{
				v.push_back(p);
			}
		}
	}
	if (v.empty())
	{
		return b;
	}
	float minZ = v[0].z, maxZ = v[0].z;
	for (const Coord3D &p : v)
	{
		minZ = std::min(minZ, p.z);
		maxZ = std::max(maxZ, p.z);
	}
	const float tolerance = SimMath::divf32(SimMath::subf32(maxZ, minZ), 50.0f);
	const float lowTop = SimMath::addf32(minZ, tolerance), highBottom = SimMath::subf32(maxZ, tolerance);
	Coord3D lowA{}, lowB{}, highA{}, highB{};
	bool foundLow = false, foundHigh = false;
	for (const Coord3D &p : v)
	{
		if (lowTop > p.z)
		{
			if (foundLow)
			{
				if (dist2(lowA, lowB) < dist2(lowA, p))
				{
					lowB = p;
				}
			}
			else
			{
				lowA = lowB = p;
				foundLow = true;
			}
		}
		else if (highBottom < p.z)
		{
			if (foundHigh)
			{
				if (dist2(highA, highB) < dist2(highA, p))
				{
					highB = p;
				}
			}
			else
			{
				highA = highB = p;
				foundHigh = true;
			}
		}
	}
	if (!foundLow || !foundHigh)
	{
		return b; // getRamp answers false: no ramp
	}
	for (const Coord3D &p : v)
	{
		if (lowTop > p.z)
		{
			if (dist2(lowA, lowB) < dist2(lowB, p))
			{
				lowA = p;
			}
		}
		else if (highBottom < p.z)
		{
			if (dist2(highA, highB) < dist2(highA, p))
			{
				highB = p;
			}
		}
	}
	b.from = midpoint(highA, highB);
	b.to = midpoint(lowA, lowB);
	if (s.triangles.size() > 2)
	{
		b.toLeft = truncated(lowA);
		b.toRight = truncated(lowB);
		b.fromLeft = truncated(highA);
		b.fromRight = truncated(highB);
	}
	else
	{
		const Coord3D lowMid = midpoint(lowA, lowB), highMid = midpoint(highA, highB);
		const float width = SimMath::mulf32(SimMath::addf32(span(highA, highB), span(lowA, lowB)), 0.5f);
		float nx = SimMath::subf32(0.0f, SimMath::subf32(lowMid.y, highMid.y)), ny = SimMath::subf32(lowMid.x, highMid.x);
		const float len = SimMath::length2d(nx, ny);
		if (len != 0.0f)
		{
			nx = SimMath::divf32(nx, len);
			ny = SimMath::divf32(ny, len);
		}
		const float hw = SimMath::mulf32(width, 0.5f);
		b.fromLeft = Coord3D{ SimMath::addf32(highMid.x, SimMath::mulf32(nx, hw)), SimMath::addf32(highMid.y, SimMath::mulf32(ny, hw)), highMid.z };
		b.fromRight = Coord3D{ SimMath::subf32(highMid.x, SimMath::mulf32(nx, hw)), SimMath::subf32(highMid.y, SimMath::mulf32(ny, hw)), highMid.z };
		b.toLeft = Coord3D{ SimMath::addf32(lowMid.x, SimMath::mulf32(nx, hw)), SimMath::addf32(lowMid.y, SimMath::mulf32(ny, hw)), lowMid.z };
		b.toRight = Coord3D{ SimMath::subf32(lowMid.x, SimMath::mulf32(nx, hw)), SimMath::subf32(lowMid.y, SimMath::mulf32(ny, hw)), lowMid.z };
	}
	b.loX = std::min(std::min(b.fromLeft.x, b.fromRight.x), std::min(b.toLeft.x, b.toRight.x));
	b.loY = std::min(std::min(b.fromLeft.y, b.fromRight.y), std::min(b.toLeft.y, b.toRight.y));
	b.hiX = std::max(std::max(b.fromLeft.x, b.fromRight.x), std::max(b.toLeft.x, b.toRight.x));
	b.hiY = std::max(std::max(b.fromLeft.y, b.fromRight.y), std::max(b.toLeft.y, b.toRight.y));
	b.valid = true;
	return b;
}

// RW 0x67F2D0 at the four corners of cell (i, j): (x, y), (x, y + 10), (x + 10, y + 10), (x + 10, y); the ramp cell rule counts them
int bridgeCorners(const RampBridge &b, int i, int j);

// RW 0x67EF1F (x, y): the point inside the triangle or on its edges
bool inTriangle2d(const Coord3D &a, const Coord3D &b, const Coord3D &c, float x, float y)
{
	using SimMath::mulf32;
	using SimMath::subf32;
	const float d1 = subf32(mulf32(subf32(b.x, a.x), subf32(y, a.y)), mulf32(subf32(b.y, a.y), subf32(x, a.x)));
	const float d2 = subf32(mulf32(subf32(c.x, b.x), subf32(y, b.y)), mulf32(subf32(c.y, b.y), subf32(x, b.x)));
	const float d3 = subf32(mulf32(subf32(a.x, c.x), subf32(y, c.y)), mulf32(subf32(a.y, c.y), subf32(x, c.x)));
	const bool neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
	const bool pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
	return !(neg && pos);
}

// RW 0x67F2D0: beyond the bounds -> no; else in the triangle (fromLeft, fromRight, toLeft) or (fromRight, toLeft, toRight)
bool bridgeContains(const RampBridge &b, float x, float y)
{
	if (x < b.loX || x > b.hiX || y < b.loY || y > b.hiY)
	{
		return false;
	}
	return inTriangle2d(b.fromLeft, b.fromRight, b.toLeft, x, y) || inTriangle2d(b.fromRight, b.toLeft, b.toRight, x, y);
}

// RW 0x68044E: the height at (x, y) of the plane through fromLeft, fromRight and toLeft (a vertical line met with that plane, RW 0x47B887 / 0x468985)
int bridgeCorners(const RampBridge &b, int i, int j)
{
	const float x0 = SimMath::sseFromInt32(i * 10), y0 = SimMath::sseFromInt32(j * 10);
	const float x1 = SimMath::addf32(x0, 10.0f), y1 = SimMath::addf32(y0, 10.0f);
	return (bridgeContains(b, x0, y0) ? 1 : 0) + (bridgeContains(b, x0, y1) ? 1 : 0) + (bridgeContains(b, x1, y1) ? 1 : 0) + (bridgeContains(b, x1, y0) ? 1 : 0);
}

// the normal of the Bridge's plane through fromLeft, fromRight and toLeft (RW 0x68044E's plane)
Coord3D bridgeNormal(const RampBridge &b)
{
	using SimMath::mulf32;
	using SimMath::subf32;
	const Coord3D &p = b.fromLeft, &q = b.fromRight, &r = b.toLeft;
	const float ux = subf32(q.x, p.x), uy = subf32(q.y, p.y), uz = subf32(q.z, p.z);
	const float vx = subf32(r.x, p.x), vy = subf32(r.y, p.y), vz = subf32(r.z, p.z);
	return Coord3D{ subf32(mulf32(uy, vz), mulf32(uz, vy)), subf32(mulf32(uz, vx), mulf32(ux, vz)), subf32(mulf32(ux, vy), mulf32(uy, vx)) };
}

// RW 0x68044E: the plane's height at (x, y). Only for a Bridge whose plane is not vertical (stampRampSurface leaves the others out, reported)
float bridgeHeight(const RampBridge &b, float x, float y)
{
	using SimMath::addf32;
	using SimMath::mulf32;
	using SimMath::subf32;
	const Coord3D &p = b.fromLeft;
	const Coord3D n = bridgeNormal(b);
	return subf32(p.z, SimMath::divf32(addf32(mulf32(n.x, subf32(x, p.x)), mulf32(n.y, subf32(y, p.y))), n.z));
}

// RW 0x67C81E: the segment (a, b) meets the rectangle [x0, x1] x [y0, y1] (edges included; Cohen-Sutherland)
bool segmentMeetsRect(float ax, float ay, float bx, float by, float x0, float y0, float x1, float y1)
{
	auto code = [&](float x, float y) {
		int c = 0;
		c |= x < x0 ? 1 : (x > x1 ? 2 : 0);
		c |= y > y1 ? 4 : (y < y0 ? 8 : 0);
		return c;
	};
	int ca = code(ax, ay), cb = code(bx, by);
	for (int guard = 0; guard < 8; ++guard)
	{
		if ((ca | cb) == 0)
		{
			return true;
		}
		if ((ca & cb) != 0)
		{
			return false;
		}
		const int out = ca != 0 ? ca : cb;
		float x = 0.0f, y = 0.0f;
		const float dx = SimMath::subf32(bx, ax), dy = SimMath::subf32(by, ay);
		if (out & 4)
		{
			if (dy == 0.0f) return false;
			x = SimMath::addf32(ax, SimMath::mulf32(dx, SimMath::divf32(SimMath::subf32(y1, ay), dy)));
			y = y1;
		}
		else if (out & 8)
		{
			if (dy == 0.0f) return false;
			x = SimMath::addf32(ax, SimMath::mulf32(dx, SimMath::divf32(SimMath::subf32(y0, ay), dy)));
			y = y0;
		}
		else if (out & 2)
		{
			if (dx == 0.0f) return false;
			y = SimMath::addf32(ay, SimMath::mulf32(dy, SimMath::divf32(SimMath::subf32(x1, ax), dx)));
			x = x1;
		}
		else
		{
			if (dx == 0.0f) return false;
			y = SimMath::addf32(ay, SimMath::mulf32(dy, SimMath::divf32(SimMath::subf32(x0, ax), dx)));
			x = x0;
		}
		if (out == ca)
		{
			ax = x;
			ay = y;
			ca = code(ax, ay);
		}
		else
		{
			bx = x;
			by = y;
			cb = code(bx, by);
		}
	}
	return false;
}

// RW 0x67CAC6: the cell's square meets the ramp's start edge (fromLeft .. fromRight) or its end edge (toLeft .. toRight), each shortened by 10 at both ends
// along the start edge's direction (RotWK takes the end edge's own direction only for a Bridge with a raised layer, + 200: never a RampMesh's)
bool bridgeEndMeetsCell(const RampBridge &b, float x0, float y0, float x1, float y1)
{
	float dx = SimMath::subf32(b.fromRight.x, b.fromLeft.x), dy = SimMath::subf32(b.fromRight.y, b.fromLeft.y);
	const float len = span(b.fromRight, b.fromLeft);
	if (len != 0.0f)
	{
		dx = SimMath::divf32(dx, len);
		dy = SimMath::divf32(dy, len);
	}
	const float ex = SimMath::mulf32(dx, 10.0f), ey = SimMath::mulf32(dy, 10.0f);
	if (segmentMeetsRect(SimMath::addf32(b.fromLeft.x, ex), SimMath::addf32(b.fromLeft.y, ey), SimMath::subf32(b.fromRight.x, ex), SimMath::subf32(b.fromRight.y, ey),
			x0, y0, x1, y1))
	{
		return true;
	}
	return segmentMeetsRect(SimMath::addf32(b.toLeft.x, ex), SimMath::addf32(b.toLeft.y, ey), SimMath::subf32(b.toRight.x, ex), SimMath::subf32(b.toRight.y, ey), x0,
		y0, x1, y1);
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
	// RotWK's ramp cell RW 0x93527F (its add branch; RW 0x9356DF walks the Bridge's cells for the footprint classifier RW 0x935FAA / 0x936B7D) on the Bridge
	// getRamp builds from the RampMesh (makeRampBridge). A ramp meets the ground, a wall top or a raised walkway only at its start and end edges; its sides are
	// type 5. (The port connected every ramp cell within 15 of the terrain to the ground, and every ramp cell beside a wall within 15 to that wall.)
	const RampBridge b = makeRampBridge(s);
	if (!b.valid)
	{
		return; // getRamp answers false (a flat RampMesh): RW records no ramp, so RW 0x9356DF never runs for it
	}
	if (bridgeNormal(b).z == 0.0f)
	{
		note(pathstops::kRampNoHeight); // a vertical plane (a RampMesh of no width): RW 0x68044E has no height for it, the ramp is left out
		return;
	}
	const PathfindRegion r = surfaceCells(s);
	addRampConnectCells(b.from, b.to);
	for (int i = r.lo.x; i <= r.hi.x; ++i)
	{
		for (int j = r.lo.y; j <= r.hi.y; ++j)
		{
			const float x0 = SimMath::sseFromInt32(i * 10), y0 = SimMath::sseFromInt32(j * 10);
			const float x1 = SimMath::addf32(x0, 10.0f), y1 = SimMath::addf32(y0, 10.0f);
			const int corners = bridgeCorners(b, i, j);
			if (corners == 0)
			{
				continue;
			}
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			if (!c)
			{
				continue;
			}
			// RW 0x9352E6 .. 0x935308: an OBSTACLE cell loses its object's footprint (the wall piece's own, or another structure's under the ramp)
			if (c->getType() == PathfindCell::CELL_OBSTACLE)
			{
				const PathfindObjectID obs = c->getObstacleID();
				if (obs != PATHFIND_INVALID_ID)
				{
					c->removeObstacleId(m_pool, obs);
				}
			}
			c->setBit17(true); // RW 0x93531A: bit 17 set (the units without CanPathThroughGates stay off the ramp, lane WALL-2)
			bool fullRamp = corners == 4;
			if (!fullRamp)
			{
				// RW 0x935330 ..: a partly covered cell. The heights at its corner (x, y): its own layer's (RW 0x6F0B27) and the Bridge's (RW 0x68044E)
				const float own = getLayerHeight(x0, y0, c->getLayer());
				const float ramp = bridgeHeight(b, x0, y0);
				if (bridgeEndMeetsCell(b, x0, y0, x1, y1))
				{
					// at the ramp's start or end edge: a raised layer at the ramp's height takes it (RW 0x93537A .. 0x9353C9); else a layer within 15 (RW 0xBDC6CC)
					// keeps the cell and connects it to the ramp (RW 0x9353D4 .. 0x935412); else the cell is the ramp's
					if (connectRampToRaised(i, j, c, ramp, corners))
					{
						c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
						if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
						continue;
					}
					if (SimMath::absD(SimMath::subD((double)own, (double)ramp)) < (double)kRampJoinTolerance)
					{
						c->setImpassableToPlayers(false);
						c->setConnectLayer(LAYER_RAMP);
						c->setType(PathfindCell::CELL_CLEAR);
					}
					else
					{
						fullRamp = true;
					}
				}
				else
				{
					// along a side (RW 0x935430 .. 0x93545E): the ramp's layer, type 5 (no unit stands there), no connect layer
					c->setLayer(LAYER_RAMP);
					c->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
					c->setConnectLayer(LAYER_INVALID);
				}
			}
			if (fullRamp)
			{
				// RW 0x9353CB .. 0x935409: the ramp's own cell: bit 18 off, layer 0x10, no connect layer, CLEAR, not pinched
				c->setImpassableToPlayers(false);
				c->setLayer(LAYER_RAMP);
				c->setConnectLayer(LAYER_INVALID);
				c->setType(PathfindCell::CELL_CLEAR);
				c->setPinched(false);
			}
			c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
			if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
		}
	}
}

void Pathfinder::addRampConnectCells(const Coord3D &from, const Coord3D &to)
{
	// RW 0x9356DF (the add branch) before its cells: an end of the ramp on a raised layer (RW 0x6EE600 at the Bridge's from, then its to; RW 0x5E2F24)
	// makes the end's cell (RW 0x6E8CE6, centred) a wall connect cell of that layer (AddWallConnectCell RW 0x76879E): the hierarchical search's raised
	// branch walks between a layer's two connect cells
	for (const Coord3D &end : { from, to })
	{
		const PathfindLayerEnum l = raisedLayerAtPoint(end.x, end.y, end.z);
		if (l != LAYER_INVALID)
		{
			Coord3D p = end;
			ICoord2D cell;
			worldToCell(&p, true, &cell);
			m_raised[(size_t)l].addWallConnectCell(cell);
		}
	}
}

void Pathfinder::rebuildWallConnectCells()
{
	for (RaisedLayer &r : m_raised)
	{
		r.connect[0] = r.connect[1] = ICoord2D{ -1, -1 };
	}
	for (const WallPiece &p : m_wallPieces)
	{
		for (const PathfindWallSurface &s : p.ramps)
		{
			const RampBridge b = makeRampBridge(s);
			if (b.valid && bridgeNormal(b).z != 0.0f) // the ramps stampRampSurface stamps
			{
				addRampConnectCells(b.from, b.to);
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
		// a ramp's cells are the ones its Bridge covers (RW 0x9356DF removes through the same cell rule it adds with), a wall top's its mesh's
		auto unstamp = [&](const PathfindWallSurface &s, const RampBridge *bridge, auto &&isMine) {
			const PathfindRegion r = surfaceCells(s);
			for (int i = r.lo.x; i <= r.hi.x; ++i)
			{
				for (int j = r.lo.y; j <= r.hi.y; ++j)
				{
					float h = 0.0f;
					PathfindCell *c = getCell(LAYER_GROUND, i, j);
					if (!c || (bridge ? bridgeCorners(*bridge, i, j) : cellCoverage(s, i, j, h)) == 0)
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
					else if (bridge)
					{
						// RW 0x93527F's remove branch: every cell the ramp covers loses bit 17 (RW 0x93496C(0)) and its connect layer (RW 0x7681E8(0)): a joint
						// of this ramp on the ground, a wall top or a walkway
						c->setBit17(false);
						c->setConnectLayer(LAYER_INVALID);
					}
					else if (c->getConnectLayer() == LAYER_RAMP)
					{
						c->setConnectLayer(LAYER_INVALID); // a joint of a ramp on this wall top
					}
					c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
				}
			}
		};
		for (size_t k = 0; k < piece.walls.size(); ++k)
		{
			const PathfindLayerEnum layer = piece.wallLayers[k];
			unstamp(piece.walls[k], nullptr, [&](const PathfindCell &c) { return c.getLayer() == layer; });
		}
		for (const PathfindWallSurface &s : piece.ramps)
		{
			const RampBridge b = makeRampBridge(s);
			if (b.valid && bridgeNormal(b).z != 0.0f) // the ramps stampRampSurface stamped
			{
				unstamp(s, &b, [](const PathfindCell &c) { return c.getLayer() == LAYER_RAMP; });
			}
		}
		// lane WALL-2: the piece's raised areas leave their layers (RW: the layer is freed and its cells made again; an empty layer is no longer in use)
		for (PathfindLayerEnum l : piece.raisedLayers)
		{
			RaisedLayer &r = m_raised[(size_t)l];
			// INFERENCE (S-4856): a ramp's joint on this walkway (a ground-grid cell the ramp cell rule RW 0x93527F put on layer 0x10 connecting to l) goes
			// back to the ground like the piece's own cells, so the ramps classify it again from the ground (RotWK's walkway removal is not decoded; the
			// ramp rule reads the cell's own layer, and a cell left on 0x10 would connect to 0x10 itself)
			for (const std::pair<PathfindObjectID, PathfindWallSurface> &a : r.areas)
			{
				if (a.first != id)
				{
					continue;
				}
				const PathfindRegion ar = surfaceCells(a.second);
				for (int i = ar.lo.x; i <= ar.hi.x; ++i)
				{
					for (int j = ar.lo.y; j <= ar.hi.y; ++j)
					{
						PathfindCell *c = getCell(LAYER_GROUND, i, j);
						if (c && c->getLayer() == LAYER_RAMP && c->getConnectLayer() == l)
						{
							c->setLayer(LAYER_GROUND);
							c->setConnectLayer(LAYER_INVALID);
							c->setBit17(false);
							classifyMapCell(i, j, c);
							c->setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
							if (!m_buildingMap) m_zoneManager.markCellDirty(i, j);
						}
					}
				}
			}
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
	// lane WALL-2, RW 0x6EE600: the first raised layer in use whose cell there is passable and whose height is less than 25 (RW 0xC69574) above z
	const PathfindLayerEnum raised = raisedLayerAtPoint(x, y, z);
	if (raised != LAYER_INVALID)
	{
		return raised;
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
