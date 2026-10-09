// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The pathfinder's view of a loaded map (lane PATH-1). Every rule is a TARGET fact from the RotWK game.dat unless marked
// (S-001 caveat; docs/STOPS.md S-160):
//   * extent: W3DTerrainLogic::getExtent (RW 0x46273E) returns lo = (0, 0) and hi = the largest boundary * 10 on each axis;
//     Pathfinder::newMap (0x6EA1F2) takes the pathfind extent from it.
//   * cliff / plane tests: W3DTerrainLogic vtable +0x50 isCliffCell (0x4624A4), +0x58 (0x4624E2), +0x5C (0x462501) each call the
//     height-map render object and the WorldHeightMap bit test (0x4ADB82, 0x4ADBD1, 0x4ADC20) on the file's cliff plane, the FIRST
//     plane after it ("ImpassabilityToPlayers": what MAP-1 calls planeA) and the extra-passability plane. The index is
//     ix = border + trunc(x * 0.1f) (float multiply), negative values clamped to 0, ix >= width - 1 becomes width - 2 (same for
//     y); the bit read is false outside the plane.
//   * isUnderwater (0x67DAE6): terrainZ = the ground height; the water handle (0x681F0A) is the HIGHEST water polygon whose z
//     is above terrainZ and that contains (x, y); waterZ is its z; true when there is one. The polygons come from the map's
//     StandingWaterAreas (INFERRED: the container at TerrainLogic+0x50 is filled by the water-area parser; whether river areas
//     also enter it is not determined, S-160); the point test is RW 0x70E911 (lane PATH-2, see pointInPolygon below).

#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{
struct PlaneCell
{
	int x, y;
};

PlaneCell planeCell(const WorldHeightMap &map, float x, float y)
{
	// ix = border - trunc(x * -0.1f) == border + trunc(x * 0.1f)
	PlaneCell c;
	c.x = map.getBorderSize() + (int)(x * 0.1f);
	c.y = map.getBorderSize() + (int)(y * 0.1f);
	if (c.x < 0) c.x = 0;
	if (c.y < 0) c.y = 0;
	if (c.x >= map.getXExtent() - 1) c.x = map.getXExtent() - 2;
	if (c.y >= map.getYExtent() - 1) c.y = map.getYExtent() - 2;
	return c;
}

// TARGET RW 0x70E911 (lane PATH-2): the polygon's box (the virtual at RW 0x47D9EB; that it is the min / max of the points is INFERRED) rejects first; then each edge from point i to point i - 1 (point n - 1 for i = 0):
// a horizontal edge is skipped, an edge with both ends left of the point is skipped, the ends are ordered by y (RW 0x70CAD5 swaps them), the edge
// counts when ay < py <= by and (py - ay) * (bx - ax) >= (px - ax) * (by - ay) (SSE float32, each product rounded); every counted edge toggles.
bool pointInPolygon(const std::vector<Point2F> &poly, float x, float y)
{
	if (poly.empty())
	{
		return false;
	}
	float loX = poly[0].x, loY = poly[0].y, hiX = poly[0].x, hiY = poly[0].y;
	for (const Point2F &p : poly)
	{
		if (p.x < loX) loX = p.x;
		if (p.y < loY) loY = p.y;
		if (p.x > hiX) hiX = p.x;
		if (p.y > hiY) hiY = p.y;
	}
	if (loX > x || loY > y || x > hiX || y > hiY)
	{
		return false;
	}
	bool inside = false;
	const size_t n = poly.size();
	for (size_t i = 0; i < n; ++i)
	{
		Point2F a = poly[i];
		Point2F b = poly[i > 0 ? i - 1 : n - 1];
		if (a.y == b.y)
		{
			continue;
		}
		if (x > a.x && x > b.x)
		{
			continue;
		}
		if (a.y > b.y)
		{
			const Point2F t = a;
			a = b;
			b = t;
		}
		if (y > b.y || a.y >= y)
		{
			continue;
		}
		const float lhs = SimMath::mulf32(SimMath::subf32(y, a.y), SimMath::subf32(b.x, a.x));
		const float rhs = SimMath::mulf32(SimMath::subf32(x, a.x), SimMath::subf32(b.y, a.y));
		if (lhs < rhs)
		{
			continue;
		}
		inside = !inside;
	}
	return inside;
}
} // namespace

TerrainPathfindSource::TerrainPathfindSource(const TerrainLogic &terrain, const WorldHeightMap &heightMap, const MapChunks &chunks)
	: m_terrain(terrain), m_map(heightMap), m_chunks(chunks)
{
}

void TerrainPathfindSource::getMaximumPathfindExtent(float &loX, float &loY, float &hiX, float &hiY) const
{
	loX = 0.0f;
	loY = 0.0f;
	hiX = 0.0f;
	hiY = 0.0f;
	for (const ICoord2D &b : m_map.m_boundaries)
	{
		hiX = std::max(hiX, (float)b.x * MAP_XY_FACTOR);
		hiY = std::max(hiY, (float)b.y * MAP_XY_FACTOR);
	}
}

void TerrainPathfindSource::getExtent(float &loX, float &loY, float &hiX, float &hiY) const
{
	getMaximumPathfindExtent(loX, loY, hiX, hiY);
}

bool TerrainPathfindSource::isCliffCell(float x, float y) const
{
	const PlaneCell c = planeCell(m_map, x, y);
	return m_map.getCliffState(c.x, c.y);
}

bool TerrainPathfindSource::impassableToPlayers(float x, float y) const
{
	const PlaneCell c = planeCell(m_map, x, y);
	return m_map.planeBit(m_map.m_planeA, c.x, c.y);
}

bool TerrainPathfindSource::extraPass(float x, float y) const
{
	const PlaneCell c = planeCell(m_map, x, y);
	return m_map.planeBit(m_map.m_planeExtraPass, c.x, c.y);
}

bool TerrainPathfindSource::isUnderwater(float x, float y, float *waterZ, float *terrainZ) const
{
	const float tz = m_terrain.getGroundHeight(x, y);
	float best = tz;
	bool found = false;
	for (const StandingWaterArea &a : m_chunks.standingWaterAreas)
	{
		const float z = (float)a.waterHeight;
		if (z > best && pointInPolygon(a.points, x, y)) // RW 0x681F0A: the area's height must be above the best so far (strict: the first of equal heights stays)
		{
			best = z;
			found = true;
		}
	}
	if (terrainZ) *terrainZ = tz;
	if (waterZ) *waterZ = best;
	return found;
}

std::vector<std::string> TerrainPathfindSource::stops() const
{
	return { pathstops::kRivers };
}
