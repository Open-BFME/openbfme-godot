// OpenBFME. GPL-3.0. See TerrainScorches.h.

#include "GameEngineDevice/W3DDevice/GameClient/TerrainScorches.h"

#include "Common/MapObject.h"
#include "GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.h"

#include <algorithm>
#include <cmath>

// RW 0x46765B
void TerrainScorchBuffer::addScorch(const Coord3D &location, float radius, int type)
{
	// RW 0x46765F .. 0x467664: the match distance, radius * 0.25 (RW 0xBD1904)
	const float limit = radius * MATCH_FRACTION;
	// RW 0x467686 .. 0x4676EC: fld / fsub / fabs on doubles against the float limit, then the type (RW 0x4676D9)
	for (int i = 0; i < m_numScorches; ++i)
	{
		Scorch &s = m_scorches[(size_t)i];
		if (!(std::fabs((double)location.x - (double)s.location.x) < (double)limit))
		{
			continue;
		}
		if (!(std::fabs((double)location.y - (double)s.location.y) < (double)limit))
		{
			continue;
		}
		if (!(std::fabs((double)radius - (double)s.radius) < (double)limit))
		{
			continue;
		}
		if (s.type != type)
		{
			continue;
		}
		s.hidden = false; // RW 0x467778: the hidden byte cleared; + 0x3794 left as it is
		return;
	}
	// RW 0x4676EE .. 0x46771C: a full list takes the oldest slot
	int slot = m_numScorches;
	if (slot >= MAX_SCORCHES)
	{
		slot = m_nextScorch;
		m_nextScorch = (m_nextScorch + 1) % MAX_SCORCHES;
		--m_numScorches;
	}
	// RW 0x46771E .. 0x46776A
	Scorch &s = m_scorches[(size_t)slot];
	s.location = location;
	s.radius = radius;
	s.type = type;
	s.hidden = false;
	++m_numScorches;
	m_scorchesInBuffer = 0;
}

// RW 0x46947C
void TerrainScorchBuffer::hideScorchesInRadius(const Coord3D &location, float radius)
{
	for (int i = 0; i < m_numScorches; ++i)
	{
		Scorch &s = m_scorches[(size_t)i];
		// RW 0x469491 .. 0x4694C7: the given location minus the scorch's, z * z + y * y + x * x in SSE floats
		const float dx = location.x - s.location.x;
		const float dy = location.y - s.location.y;
		const float dz = location.z - s.location.z;
		float sum = dz * dz;
		sum = sum + dy * dy;
		sum = sum + dx * dx;
		// RW 0x4694D0 .. 0x4694D5: x87 fsqrt, stored as a float
		const float distance = (float)std::sqrt((double)sum);
		// RW 0x4694D8 .. 0x4694E8
		if (s.radius + radius > distance)
		{
			s.hidden = true;
		}
	}
	m_scorchesInBuffer = 0; // RW 0x4694F8
}

// RW 0x46CE22 .. 0x46CE2E (and RW 0x467646)
void TerrainScorchBuffer::clearAllScorches()
{
	m_numScorches = 0;
	m_scorchesInBuffer = 0;
	m_nextScorch = 0;
}

// RW 0x491E36 .. 0x491EC3
int TerrainScorchBuffer::addMapScorches(TerrainScorchBuffer &buffer, const std::vector<MapObject> &objects, std::vector<std::string> &errors)
{
	int added = 0;
	for (const MapObject &o : objects)
	{
		if (!o.isScorch())
		{
			continue;
		}
		bool hasRadius = false;
		const float radius = o.m_properties.getReal("objectRadius", &hasRadius);
		const int type = o.m_properties.getInt("scorchType");
		if (!hasRadius)
		{
			errors.push_back("Scorch map object at (" + std::to_string(o.m_location.x) + ", " + std::to_string(o.m_location.y) +
				") has no REAL objectRadius: not added");
			continue;
		}
		buffer.addScorch(o.m_location, radius, type);
		++added;
	}
	return added;
}

TerrainScorchBuffer::Terrain TerrainScorchBuffer::terrainFrom(const WorldHeightMap &map, const std::vector<TerrainChunk> &chunks)
{
	Terrain t;
	t.xExtent = map.getXExtent();
	t.yExtent = map.getYExtent();
	t.border = map.getBorderSize();
	t.heights = map.m_data;
	t.flip.assign((size_t)t.xExtent * (size_t)t.yExtent, 0);
	const int cellsX = t.xExtent - 1; // HeightMapMesh's cellId = y * (width - 1) + x
	if (cellsX <= 0)
	{
		return t;
	}
	for (const TerrainChunk &ch : chunks)
	{
		const TerrainLayerMesh &base = ch.layer[0];
		for (size_t c = 0; c < base.cellId.size() && c < base.cellFlip.size(); ++c)
		{
			const int x = (int)(base.cellId[c] % (std::uint32_t)cellsX);
			const int y = (int)(base.cellId[c] / (std::uint32_t)cellsX);
			t.flip[(size_t)y * (size_t)t.xExtent + (size_t)x] = base.cellFlip[c];
		}
	}
	return t;
}

TerrainScorchBuffer::Shade TerrainScorchBuffer::shadeFrom(const TimeOfDayLights *lights, const float scale[3], bool overbright)
{
	Shade s;
	for (int k = 0; k < 3; ++k)
	{
		s.ambient[k] = lights ? lights->terrain[0].ambient[k] : 0.0f;
		s.diffuse[k] = lights ? lights->terrain[0].diffuse[k] : 0.0f;
		s.scale[k] = scale[k];
	}
	s.overbright = overbright;
	return s;
}

// RW 0x51EB60 on the D3D projection of the camera's clip planes
float TerrainScorchBuffer::zBiasedDepth(float zbias, float znear, float zfar, float d)
{
	const float m33 = zfar / (zfar - znear);
	float m43 = -(znear * zfar) / (zfar - znear);
	if (zfar != znear) // RW 0x51EC77 .. 0x51EC96
	{
		// RW 0x51EC98 .. 0x51ECB7: xmm1 = zbias * 0.000625; xmm4 = zfar * znear / (zfar - znear) * xmm1; _43 -= xmm4
		const float b = zbias * ZBIAS_SCALE;
		float t = zfar * znear;
		t = t / (zfar - znear);
		t = t * b;
		m43 = m43 - t;
	}
	return (m33 * d + m43) / d;
}

float TerrainScorchBuffer::depthBiasViewScale(float zbias)
{
	return 1.0f / (1.0f + zbias * ZBIAS_SCALE);
}

// RW 0x4670BC .. 0x4671A4
std::uint32_t TerrainScorchBuffer::scorchDiffuse(const Shade &shade)
{
	float c[3];
	for (int k = 0; k < 3; ++k)
	{
		// RW 0x4670E1 .. 0x46713D: scale * diffuse, times 0.5 (RW 0xBD869C), plus ambient
		c[k] = shade.scale[k] * shade.diffuse[k];
		c[k] = c[k] * 0.5f;
		c[k] = c[k] + shade.ambient[k];
	}
	// RW 0x467140 .. 0x467174: times 2.0 (RW 0xBD889C) under the overbright byte RW 0xD9B03C, else 1.0; clamped to 1.0 (RW 0xBD1908)
	const float mul = shade.overbright ? 2.0f : 1.0f;
	int v[3];
	for (int k = 0; k < 3; ++k)
	{
		float x = mul * c[k];
		if (x > 1.0f)
		{
			x = 1.0f;
		}
		// RW 0x467177 .. 0x4671A0: times 255 (RW 0xBD88A8), cvttss2si
		v[k] = (int)(x * 255.0f);
	}
	// RW 0x467187 .. 0x4671A4: ((r | 0xFFFFFF00) << 8 | g) << 8 | b, in 32-bit integers
	std::uint32_t d = (std::uint32_t)v[0] | 0xFFFFFF00u;
	d <<= 8;
	d |= (std::uint32_t)v[1];
	d <<= 8;
	d |= (std::uint32_t)v[2];
	return d;
}

namespace
{
// RW 0x465CEA: the height read, x and y clamped to the map
std::uint16_t clampedHeight(const TerrainScorchBuffer::Terrain &t, int x, int y)
{
	x = x < 0 ? 0 : (x >= t.xExtent ? t.xExtent - 1 : x);
	y = y < 0 ? 0 : (y >= t.yExtent ? t.yExtent - 1 : y);
	return t.heights[(size_t)y * (size_t)t.xExtent + (size_t)x];
}

// RW 0x4AD985: the flip bit; false outside the map
bool flipState(const TerrainScorchBuffer::Terrain &t, int x, int y)
{
	if (x < 0 || y < 0 || x >= t.xExtent || y >= t.yExtent)
	{
		return false;
	}
	const size_t i = (size_t)y * (size_t)t.xExtent + (size_t)x;
	return i < t.flip.size() && t.flip[i] != 0;
}
} // namespace

// RW 0x467073
bool TerrainScorchBuffer::updateScorches(const Terrain &terrain, const Shade &shade)
{
	// RW 0x467086 .. 0x4670B6 (the buffers exist in the port once a terrain is given)
	if (!needsUpdate() || !terrain.valid())
	{
		return false;
	}
	// RW 0x4670C4 .. 0x4670D0
	m_scorchesInBuffer = 0;
	m_vertices.clear();
	m_indices.clear();
	++m_revision;
	const std::uint32_t diffuse = scorchDiffuse(shade);
	const int border = terrain.border; // RW 0x4671AA .. 0x4671B0 (+ 0x10 of the height map)
	const float third = 1.0f / 3.0f;   // RW 0xBDBC78 (0x3EAAAAAB)
	const float xy = 10.0f;            // RW 0xBD83D8
	const float invXY = 0.1f;          // RW 0xBD83D4

	// RW 0x4671D5 .. 0x46760E: from the last index to the first
	for (int cur = m_numScorches - 1; cur >= 0; --cur)
	{
		const Scorch &s = m_scorches[(size_t)cur];
		if (s.hidden)
		{
			continue;
		}
		++m_scorchesInBuffer; // RW 0x467205
		int type = s.type;
		if (type < 0 || type >= SCORCH_MARKS_IN_TEXTURE)
		{
			type = 0; // RW 0x46720E .. 0x467217
		}
		const float uOffset = (float)(type % SCORCH_PER_ROW) * third;
		const float vOffset = (float)(type / SCORCH_PER_ROW) * third;
		const float radius = s.radius;
		const Coord3D loc = s.location;
		// RW 0x467250 .. 0x46735A: floor / ceil (CRT, on doubles) of the float (x -+ r) * 0.1. INFERENCE: the max side's x87 add and multiply
		// run at the game's 24-bit x87 precision, the same rounding as the min side's SSE floats.
		int minX = (int)(float)std::floor((double)((loc.x - radius) * invXY)) - 1;
		int minY = (int)(float)std::floor((double)((loc.y - radius) * invXY)) - 1;
		if (minX < -border)
		{
			minX = -border;
		}
		if (minY < -border)
		{
			minY = -border;
		}
		int maxX = (int)(float)std::ceil((double)((loc.x + radius) * invXY)) + 1;
		int maxY = (int)(float)std::ceil((double)((loc.y + radius) * invXY)) + 1;
		if (maxX > terrain.xExtent - border)
		{
			maxX = terrain.xExtent - border;
		}
		if (maxY > terrain.yExtent - border)
		{
			maxY = terrain.yExtent - border;
		}
		// RW 0x467386 .. 0x4673E0
		const int startVertex = (int)m_vertices.size();
		const float left = (float)minX * xy;
		const float top = (float)minY * xy;
		const float width = (float)(maxX - minX - 1) * xy;
		const float height = (float)(maxY - minY - 1) * xy;
		const int yOffset = maxX - minX;
		// RW 0x4673EE .. 0x4674F5: the vertices, rows of x
		for (int j = minY; j < maxY; ++j)
		{
			const float Y = (float)j * xy;
			for (int i = minX; i < maxX; ++i)
			{
				if ((int)m_vertices.size() >= MAX_SCORCH_VERTEX)
				{
					// RW 0x46741B / 0x467616: this scorch's vertices are taken back and the update ends
					m_vertices.resize((size_t)startVertex);
					return true;
				}
				const float X = (float)i * xy;
				Vertex v;
				v.diffuse = diffuse;
				v.u = (X - left) / width * third + uOffset;
				v.v = (Y - top) / height * third + vOffset;
				v.x = X;
				v.y = Y;
				// RW 0x4674B2 .. 0x4674CC: height * 0.0390625 (RW 0xBDB8EC) + 1.0
				v.z = (float)clampedHeight(terrain, i + border, j + border) * 0.0390625f + 1.0f;
				m_vertices.push_back(v);
			}
		}
		// RW 0x4674FB .. 0x4675F5: two triangles per cell
		for (int j = 0; j < maxY - minY - 1; ++j)
		{
			const int yNdx = j + minY + border;
			for (int i = 0; i < yOffset - 1; ++i)
			{
				if ((int)m_indices.size() + 6 > MAX_SCORCH_INDEX)
				{
					return true; // RW 0x467530 .. 0x467535
				}
				const int xNdx = i + minX + border;
				const int base = startVertex + j * yOffset + i;
				if (flipState(terrain, xNdx, yNdx))
				{
					// RW 0x46755B .. 0x46759C
					const int idx[6] = { base + 1, base + yOffset, base, base + 1, base + 1 + yOffset, base + yOffset };
					for (int k : idx)
					{
						m_indices.push_back((std::uint16_t)k);
					}
				}
				else
				{
					// RW 0x4675A0 .. 0x4675D6
					const int idx[6] = { base, base + 1 + yOffset, base + yOffset, base, base + 1, base + 1 + yOffset };
					for (int k : idx)
					{
						m_indices.push_back((std::uint16_t)k);
					}
				}
			}
		}
	}
	return true;
}
