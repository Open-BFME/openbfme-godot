// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/Map/TerrainLogic.h for sources and the numeric-facade note.

#include "GameLogic/Map/TerrainLogic.h"

#include <cmath>

namespace
{
const float MAP_XY_FACTOR_INV = 1.0f / MAP_XY_FACTOR;
}

std::uint16_t TerrainLogic::getClipHeight(int x, int y) const
{
	int xextent = m_map->m_width;
	int yextent = m_map->m_height;
	if (x < 0)
		x = 0;
	else if (x >= xextent)
		x = xextent - 1;
	if (y < 0)
		y = 0;
	else if (y >= yextent)
		y = yextent - 1;
	return m_map->m_data[(size_t)(x + y * xextent)];
}

float TerrainLogic::getGroundHeight(float x, float y, Coord3D *normal) const
{
	if (!m_map || m_map->m_data.empty())
	{
		if (normal)
		{
			normal->x = 0.0f;
			normal->y = 0.0f;
			normal->z = 1.0f;
		}
		return 0;
	}

	float height;

	//	3-----2
	//  |    /|
	//  |  /  |
	//	|/    |
	//  0-----1
	float xdiv = x * MAP_XY_FACTOR_INV;
	float ydiv = y * MAP_XY_FACTOR_INV;
	float ixf = std::floor(xdiv);
	float iyf = std::floor(ydiv);
	float fx = xdiv - ixf;
	float fy = ydiv - iyf;
	int ix = (int)ixf + m_map->getBorderSize();
	int iy = (int)iyf + m_map->getBorderSize();
	int xExtent = m_map->getXExtent();

	// Check for extent-3, not extent-1: smoothed triangle points go into the next row/column of data.
	if (ix > (xExtent - 3) || iy > (m_map->getYExtent() - 3) || iy < 1 || ix < 1)
	{
		if (normal)
		{
			normal->x = 0.0f;
			normal->y = 0.0f;
			normal->z = 1.0f;
		}
		return getClipHeight(ix, iy) * MAP_HEIGHT_SCALE;
	}

	const std::uint16_t *data = m_map->m_data.data();
	int idx = ix + iy * xExtent;
	float p0 = data[idx];
	float p2 = data[idx + xExtent + 1];
	if (fy > fx) // test if we are in the upper triangle
	{
		float p3 = data[idx + xExtent];
		height = (p3 + (1.0f - fy) * (p0 - p3) + fx * (p2 - p3)) * MAP_HEIGHT_SCALE;
	}
	else
	{
		// we are in the lower triangle
		float p1 = data[idx + 1];
		height = (p1 + fy * (p2 - p1) + (1.0f - fx) * (p0 - p1)) * MAP_HEIGHT_SCALE;
	}

	if (normal)
	{
		//		9		  8
		//10	3-----2		7
		//	  |    /|
		//		|/    |
		//11	0-----1		6
		//		4			5
		int idx4 = ix + (iy - 1) * xExtent;
		int idx0 = ix + iy * xExtent;
		int idx3 = ix + iy * xExtent + xExtent;
		int idx9 = ix + (iy + 2) * xExtent;
		float d0 = data[idx0];
		float d1 = data[idx0 + 1];
		float d2 = data[idx3 + 1];
		float d3 = data[idx3];
		float d4 = data[idx4];
		float d5 = data[idx4 + 1];
		float d6 = data[idx0 + 2];
		float d7 = data[idx3 + 2];
		float d8 = data[idx9 + 1];
		float d9 = data[idx9];
		float d10 = data[idx3 - 1];
		float d11 = data[idx0 - 1];

		float deltaZ_X0 = d1 - d11;
		float deltaZ_X1 = d6 - d0;
		float deltaZ_X2 = d7 - d3;
		float deltaZ_X3 = d6 - d0;

		float deltaZ_Y0 = d3 - d4;
		float deltaZ_Y1 = d2 - d5;
		float deltaZ_Y2 = d8 - d1;
		float deltaZ_Y3 = d9 - d0;
		(void)d10;

		// Interpolate to get the smoothed valued.
		float deltaZ_X_Left = deltaZ_X0 * (1.0f - fx) + fx * deltaZ_X3;
		float deltaZ_X_Right = deltaZ_X1 * (1.0f - fx) + fx * deltaZ_X2;
		float deltaZ_X = (float)(deltaZ_X_Left * (1.0 - fy) + fy * deltaZ_X_Right);

		float deltaZ_Y_Left = deltaZ_Y0 * (1.0f - fx) + fx * deltaZ_Y3;
		float deltaZ_Y_Right = deltaZ_Y1 * (1.0f - fx) + fx * deltaZ_Y2;
		float deltaZ_Y = (float)(deltaZ_Y_Left * (1.0 - fy) + fy * deltaZ_Y_Right);

		// l2r = (2*XY/SCALE, 0, dX), n2f = (0, 2*XY/SCALE, dY); normal = normalize(cross(l2r, n2f))
		const float k = 2 * MAP_XY_FACTOR / MAP_HEIGHT_SCALE;
		// cross((k,0,dX),(0,k,dY)) = (0*dY - dX*k, dX*0 - k*dY, k*k - 0*0)
		float cx = 0 * deltaZ_Y - deltaZ_X * k;
		float cy = deltaZ_X * 0 - k * deltaZ_Y;
		float cz = k * k;
		float len = std::sqrt(cx * cx + cy * cy + cz * cz);
		normal->x = cx / len;
		normal->y = cy / len;
		normal->z = cz / len;
	}
	return height;
}

bool TerrainLogic::getExtent(size_t boundary, float &maxX, float &maxY) const
{
	if (!m_map || boundary >= m_map->m_boundaries.size())
	{
		return false;
	}
	maxX = (float)m_map->m_boundaries[boundary].x * MAP_XY_FACTOR;
	maxY = (float)m_map->m_boundaries[boundary].y * MAP_XY_FACTOR;
	return true;
}

void TerrainLogic::getMapExtentIncludingBorder(float &minX, float &minY, float &maxX, float &maxY) const
{
	minX = -(float)m_map->m_borderSize * MAP_XY_FACTOR;
	minY = -(float)m_map->m_borderSize * MAP_XY_FACTOR;
	maxX = (float)(m_map->m_width - m_map->m_borderSize) * MAP_XY_FACTOR;
	maxY = (float)(m_map->m_height - m_map->m_borderSize) * MAP_XY_FACTOR;
}

void TerrainLogic::init(const WorldHeightMap &heightMap, const MapChunks &chunks, std::vector<std::string> *problems)
{
	m_map = &heightMap;
	m_chunks = &chunks;
	m_waypoints.clear();
	m_waypointById.clear();
	for (const MapObject &o : chunks.objects)
	{
		if (!o.isWaypoint())
		{
			continue;
		}
		Waypoint w;
		w.id = o.getWaypointID();
		w.name = o.getWaypointName();
		w.location = o.m_location;
		w.location.z = getGroundHeight(w.location.x, w.location.y); // snap down to the terrain (ZH addWaypoint)
		w.label1 = o.m_properties.getAsciiString("waypointPathLabel1");
		w.label2 = o.m_properties.getAsciiString("waypointPathLabel2");
		w.label3 = o.m_properties.getAsciiString("waypointPathLabel3");
		w.biDirectional = o.m_properties.getBool("waypointPathBiDirectional");
		if (m_waypointById.count(w.id) && problems)
		{
			problems->push_back("duplicate waypoint id " + std::to_string(w.id));
		}
		m_waypointById[w.id] = m_waypoints.size();
		m_waypoints.push_back(std::move(w));
	}
	for (const WaypointLink &l : chunks.waypointLinks)
	{
		auto a = m_waypointById.find(l.from);
		auto b = m_waypointById.find(l.to);
		if (a == m_waypointById.end() || b == m_waypointById.end())
		{
			if (problems)
			{
				problems->push_back("waypoint link " + std::to_string(l.from) + " -> " + std::to_string(l.to) + " names an unknown waypoint id");
			}
			continue;
		}
		m_waypoints[a->second].linksTo.push_back(l.to);
	}
}

const Waypoint *TerrainLogic::findWaypointById(int id) const
{
	auto it = m_waypointById.find(id);
	return it == m_waypointById.end() ? nullptr : &m_waypoints[it->second];
}

const Waypoint *TerrainLogic::findWaypointByName(const std::string &name) const
{
	for (const Waypoint &w : m_waypoints)
	{
		if (w.name == name)
		{
			return &w;
		}
	}
	return nullptr;
}

namespace
{
// Even-odd rule point in polygon (non-convex allowed).
bool pointInPolygon(const std::vector<Point2F> &poly, float x, float y)
{
	bool inside = false;
	const size_t n = poly.size();
	for (size_t i = 0, j = n - 1; i < n; j = i++)
	{
		const Point2F &a = poly[i];
		const Point2F &b = poly[j];
		if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x))
		{
			inside = !inside;
		}
	}
	return inside;
}
} // namespace

bool TerrainLogic::getStandingWaterHeight(float x, float y, float &waterZ) const
{
	if (!m_chunks)
	{
		return false;
	}
	for (const StandingWaterArea &a : m_chunks->standingWaterAreas)
	{
		if (a.points.size() >= 3 && pointInPolygon(a.points, x, y))
		{
			waterZ = (float)a.waterHeight;
			return true;
		}
	}
	return false;
}

bool TerrainLogic::isUnderwater(float x, float y, float z) const
{
	float wz;
	return getStandingWaterHeight(x, y, wz) && z < wz;
}
