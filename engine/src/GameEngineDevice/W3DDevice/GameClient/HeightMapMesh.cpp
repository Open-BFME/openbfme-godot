// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See HeightMapMesh.h for sources and named assumptions.

#include "GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.h"

#include "Common/MapObject.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{

struct LayerBuilder
{
	static void addCell(TerrainLayerMesh &L, const float pos[4][3], const float nrm[4][3], const std::uint8_t rgb[4][3],
		const std::uint8_t alpha[4], const float U[4], const float V[4], bool flip, const ClassWrapRect &wrap, std::uint32_t cellId)
	{
		const std::uint32_t base = (std::uint32_t)L.vertexCount();
		L.cellId.push_back(cellId);
		L.cellFlip.push_back(flip ? 1 : 0);
		for (int k = 0; k < 4; ++k)
		{
			L.wrap.push_back(wrap.x0);
			L.wrap.push_back(wrap.y0);
			L.wrap.push_back(wrap.w);
			L.wrap.push_back(wrap.h);
			L.position.insert(L.position.end(), pos[k], pos[k] + 3);
			L.normal.insert(L.normal.end(), nrm[k], nrm[k] + 3);
			L.color.push_back(rgb[k][0]);
			L.color.push_back(rgb[k][1]);
			L.color.push_back(rgb[k][2]);
			L.color.push_back(alpha[k]);
			L.uv.push_back(U[k]);
			L.uv.push_back(V[k]);
		}
		// Vertices are SW=0, SE=1, NE=2, NW=3. ZH index pattern (0,2,3),(0,1,2) with the vertex order rotated
		// by one for a flipped cell; emitted here with the winding reversed for Godot's clockwise front face
		// under the (x, z, -y) frame (spec 2.1, 3.2): unflipped (SW,NW,NE),(SW,NE,SE); flipped (SE,SW,NW),(SE,NW,NE).
		static const std::uint32_t kUnflipped[6] = { 0, 3, 2, 0, 2, 1 };
		static const std::uint32_t kFlipped[6] = { 1, 0, 3, 1, 3, 2 };
		const std::uint32_t *p = flip ? kFlipped : kUnflipped;
		for (int i = 0; i < 6; ++i)
		{
			L.index.push_back(base + p[i]);
		}
	}
};

float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

// ZH/BFME1 doTheLight (BaseHeightMap.cpp): ambient of the FIRST terrain light only, plus for each light
// clamp(dot(lightRay, normal), 0, 1) * diffuse; lightRay = -lightPos. See HeightMapMesh.h (S-033).
void HeightMapMesh::vertexLight(const TimeOfDayLights &lights, VertexColorMode mode, const float normal[3], float rgb[3])
{
	rgb[0] = lights.terrain[0].ambient[0];
	rgb[1] = lights.terrain[0].ambient[1];
	rgb[2] = lights.terrain[0].ambient[2];
	const int firstLight = mode == VERTEX_COLOR_ALL_GLOBAL_LIGHTS ? 0 : 1;
	for (int i = firstLight; i < 3; ++i)
	{
		const GlobalLight &L = lights.terrain[i];
		float rx = -L.lightPos[0], ry = -L.lightPos[1], rz = -L.lightPos[2];
		float shade = rx * normal[0] + rz * normal[2] + ry * normal[1];
		shade = clamp01(shade);
		rgb[0] += shade * L.diffuse[0];
		rgb[1] += shade * L.diffuse[1];
		rgb[2] += shade * L.diffuse[2];
	}
	rgb[0] = clamp01(rgb[0]);
	rgb[1] = clamp01(rgb[1]);
	rgb[2] = clamp01(rgb[2]);
}

void HeightMapMesh::gridNormal(const WorldHeightMap &map, int x, int y, float normal[3])
{
	const int w = map.m_width, h = map.m_height;
	const int un0 = std::max(x - 1, 0), up1 = std::min(x + 1, w - 1);
	const int vn0 = std::max(y - 1, 0), vp1 = std::min(y + 1, h - 1);
	const float dzx = MAP_HEIGHT_SCALE * (float)((int)map.getHeight(up1, y) - (int)map.getHeight(un0, y));
	const float dzy = MAP_HEIGHT_SCALE * (float)((int)map.getHeight(x, vp1) - (int)map.getHeight(x, vn0));
	// cross((2*XY, 0, dzx), (0, 2*XY, dzy))
	const float k = 2 * MAP_XY_FACTOR;
	const float cx = 0 * dzy - dzx * k;
	const float cy = dzx * 0 - k * dzy;
	const float cz = k * k;
	const float len = std::sqrt(cx * cx + cy * cy + cz * cz);
	normal[0] = cx / len;
	normal[1] = cy / len;
	normal[2] = cz / len;
}

void HeightMapMesh::staticDiffuseAt(const WorldHeightMap &map, const GlobalLightingData *lighting, VertexColorMode mode, int gx, int gy, std::uint8_t rgb[3])
{
	rgb[0] = rgb[1] = rgb[2] = 255;
	if (!lighting || lighting->timeOfDay < 1 || lighting->timeOfDay > 4)
	{
		return;
	}
	gx = std::max(0, std::min(gx, map.m_width - 1));
	gy = std::max(0, std::min(gy, map.m_height - 1));
	float n[3], c[3];
	gridNormal(map, gx, gy, n);
	vertexLight(lighting->tod[lighting->timeOfDay - 1], mode, n, c);
	for (int i = 0; i < 3; ++i)
	{
		rgb[i] = (std::uint8_t)(int)(c[i] * 255.0f);
	}
}

bool HeightMapMesh::build(const WorldHeightMap &map, const GlobalLightingData *lighting, const HeightMapMeshOptions &options,
	std::vector<TerrainChunk> &chunks, TerrainMeshStats &stats, std::string *error)
{
	chunks.clear();
	stats = TerrainMeshStats();
	if (!map.m_hasBlendTileData || map.m_tileLocations.empty())
	{
		if (error)
		{
			*error = "terrain mesh needs BlendTileData and a packed atlas (TerrainAtlas::build)";
		}
		return false;
	}
	const int w = map.m_width, h = map.m_height, border = map.m_borderSize;

	// ---- vertex normals: central differences with edge clamping (ZH updateVB), SAGE space
	std::vector<float> normals((size_t)w * (size_t)h * 3);
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			gridNormal(map, x, y, &normals[((size_t)y * w + x) * 3]);
		}
	}

	// ---- vertex colours (static diffuse), one per grid vertex
	std::vector<std::uint8_t> colors((size_t)w * (size_t)h * 3, 255);
	if (lighting)
	{
		int tod = lighting->timeOfDay - 1;
		if (tod < 0 || tod > 3)
		{
			if (error)
			{
				*error = "GlobalLighting.timeOfDay " + std::to_string(lighting->timeOfDay) + " is not 1..4";
			}
			return false;
		}
		stats.timeOfDayIndex = tod;
		for (size_t i = 0; i < (size_t)w * (size_t)h; ++i)
		{
			float rgb[3];
			vertexLight(lighting->tod[tod], options.colorMode, &normals[i * 3], rgb);
			for (int c = 0; c < 3; ++c)
			{
				colors[i * 3 + (size_t)c] = (std::uint8_t)(int)(rgb[c] * 255.0f); // ZH: (Int)shade*255
			}
		}
	}
	else
	{
		stats.assumptions.push_back("no GlobalLighting chunk: vertex colour is white");
	}
	stats.assumptions.push_back(std::string("S-033 vertex colour = ")
		+ (options.colorMode == VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS ? "terrain[0] ambient + accent lights terrain[1], terrain[2] (INFERRED; the sun is per pixel)"
			: "terrain[0] ambient + all three terrain lights (BFME1 doTheLight verbatim)")
		+ "; water depth fade not applied");
	stats.assumptions.push_back(std::string("S-030 cliff UV unit = ")
		+ (options.uv.cliffUnit == CLIFF_UV_ATLAS_2048 ? "2048-px atlas fractions (BFME1/ZH formula, no wrap: a cell past its class reads the neighbouring atlas texels as the donor does)"
			: "256-px units wrapped per pixel inside the texture class block (INFERRED from the corpus, unverified for BFME2)")
		+ "; the ZH 'old UV adjustment' stretch heuristic is absent in BFME1 retail and not ported");
	stats.assumptions.push_back("S-032 layer draw: S0 opaque base, S1/S2 alpha-blended (SRCALPHA/INVSRCALPHA, INFERRED), identical vertex positions; "
		"S1 shares the base triangulation, S2 uses its own flip as in ZH");

	auto vpos = [&](int x, int y, float out[3]) {
		out[0] = (float)(x - border) * MAP_XY_FACTOR;
		out[1] = (float)(y - border) * MAP_XY_FACTOR;
		out[2] = (float)map.getHeight(x, y) * MAP_HEIGHT_SCALE;
	};

	const int cellsX = w - 1, cellsY = h - 1; // the last column/row of the grids is never drawn (spec 1.7.2)
	stats.cells = (size_t)cellsX * (size_t)cellsY;
	const int cc = std::max(1, options.chunkCells);
	for (int cy0 = 0; cy0 < cellsY; cy0 += cc)
	{
		for (int cx0 = 0; cx0 < cellsX; cx0 += cc)
		{
			chunks.emplace_back();
			TerrainChunk &ch = chunks.back();
			ch.cellX0 = cx0;
			ch.cellY0 = cy0;
			ch.cellX1 = std::min(cx0 + cc, cellsX);
			ch.cellY1 = std::min(cy0 + cc, cellsY);
			float bmin[3] = { 1e30f, 1e30f, 1e30f }, bmax[3] = { -1e30f, -1e30f, -1e30f };
			for (int y = ch.cellY0; y < ch.cellY1; ++y)
			{
				for (int x = ch.cellX0; x < ch.cellX1; ++x)
				{
					// vertex order SW, SE, NE, NW = (x,y), (x+1,y), (x+1,y+1), (x,y+1)
					const int gx[4] = { x, x + 1, x + 1, x };
					const int gy[4] = { y, y, y + 1, y + 1 };
					float pos[4][3], nrm[4][3];
					std::uint8_t rgb[4][3];
					for (int k = 0; k < 4; ++k)
					{
						vpos(gx[k], gy[k], pos[k]);
						const float *n = &normals[((size_t)gy[k] * w + gx[k]) * 3];
						nrm[k][0] = n[0];
						nrm[k][1] = n[1];
						nrm[k][2] = n[2];
						const std::uint8_t *c = &colors[((size_t)gy[k] * w + gx[k]) * 3];
						rgb[k][0] = c[0];
						rgb[k][1] = c[1];
						rgb[k][2] = c[2];
						for (int a = 0; a < 3; ++a)
						{
							bmin[a] = std::min(bmin[a], pos[k][a]);
							bmax[a] = std::max(bmax[a], pos[k][a]);
						}
					}

					float U[4], V[4];
					const int ndx = y * w + x;
					ClassWrapRect wrapBase, wrapBlend, wrapExtra;
					map.getUVData(x, y, U, V, options.uv, &wrapBase);
					float UA[4], VA[4];
					std::uint8_t alpha[4];
					bool flipBase = false;
					map.getAlphaUVData(x, y, UA, VA, alpha, &flipBase, options.uv, &wrapBlend);

					const std::uint8_t opaque[4] = { 255, 255, 255, 255 };
					LayerBuilder::addCell(ch.layer[0], pos, nrm, rgb, opaque, U, V, flipBase, wrapBase, (std::uint32_t)(y * cellsX + x));
					++stats.baseCells;
					stats.flippedCells += flipBase ? 1 : 0;
					if (map.cliffUvApplies(ndx, map.m_tileNdxes[(size_t)ndx]))
					{
						++stats.cliffUvCells;
					}
					if (wrapBase.valid())
					{
						++stats.wrapCells;
						// does any corner of this cell lie outside the class block? (it then needs the per-pixel wrap)
						bool outside = false;
						for (int k = 0; k < 4; ++k)
						{
							outside = outside || U[k] < wrapBase.x0 || U[k] > wrapBase.x0 + wrapBase.w || V[k] < wrapBase.y0
								|| V[k] > wrapBase.y0 + wrapBase.h;
						}
						stats.straddlingCells += outside ? 1 : 0;
					}
					if (U[0] == 0.0f && U[1] == 0.0f && U[2] == 0.0f && U[3] == 0.0f)
					{
						++stats.missingTextureCells;
					}

					if (map.m_blendTileNdxes[(size_t)ndx] != 0)
					{
						LayerBuilder::addCell(ch.layer[1], pos, nrm, rgb, alpha, UA, VA, flipBase, wrapBlend, (std::uint32_t)(y * cellsX + x));
						++stats.blendCells;
					}

					float UE[4], VE[4];
					std::uint8_t alphaE[4];
					bool flipE = false, cliffE = false;
					if (map.getExtraAlphaUVData(x, y, UE, VE, alphaE, &flipE, &cliffE, options.uv, &wrapExtra))
					{
						// ZH HeightMap.cpp:2316: cliffs sometimes force a flip
						const float p0 = (float)map.getHeight(x, y) * MAP_HEIGHT_SCALE;
						const float p1 = (float)map.getHeight(x + 1, y) * MAP_HEIGHT_SCALE;
						const float p2 = (float)map.getHeight(x + 1, y + 1) * MAP_HEIGHT_SCALE;
						const float p3 = (float)map.getHeight(x, y + 1) * MAP_HEIGHT_SCALE;
						if (cliffE && std::fabs(p0 - p2) > std::fabs(p1 - p3))
						{
							flipE = true;
							++stats.cliffForcedFlips;
						}
						LayerBuilder::addCell(ch.layer[2], pos, nrm, rgb, alphaE, UE, VE, flipE, wrapExtra, (std::uint32_t)(y * cellsX + x));
						++stats.extraCells;
						stats.extraFlippedCells += flipE ? 1 : 0;
					}
				}
			}
			for (int a = 0; a < 3; ++a)
			{
				ch.boundsMin[a] = bmin[a];
				ch.boundsMax[a] = bmax[a];
			}
		}
	}
	stats.chunks = chunks.size();
	for (const TerrainChunk &c : chunks)
	{
		for (int l = 0; l < 3; ++l)
		{
			stats.vertices += c.layer[l].vertexCount();
			stats.triangles += c.layer[l].index.size() / 3;
		}
	}
	return true;
}
