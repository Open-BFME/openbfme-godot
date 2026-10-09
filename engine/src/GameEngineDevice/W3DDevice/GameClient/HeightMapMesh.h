// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Terrain mesh data (no Godot dependency): the vertex/index arrays ZH HeightMapRenderObjClass builds in
// updateVB, one set of arrays per 32x32-cell chunk and draw layer, in SAGE world space (x east, y north,
// z up). The Godot device layer converts them to its frame at the presentation boundary (spec 2.1).
//
// Sources: ZH HeightMap.cpp updateVB :313-500 (four unshared vertices per cell SW,SE,NE,NW; normals by
// central differences with edge clamping; flipped cells rotate the vertex order), the static index
// pattern :1339-1350, extra-blend-tile buffer :2250-2330 (its own flip state, forced by cliffs when
// |p0-p2| > |p1-p3|), BaseHeightMap.cpp doTheLight (BFME1 retail body in Open-BFME-1 BaseHeightMap.cpp
// :611-700); layer structure per spec maps-and-terrain.md 3.2.
//
// Layers (one draw each, same vertex positions): 0 = base (alpha 255 on every cell), 1 = first blend
// (cells with blendTileNdx != 0, per-vertex alpha), 2 = extra "3-way" blend (cells with extraBlendTileNdx != 0).
//
// Named assumptions (INFERRED or UNKNOWN, reported in TerrainMeshStats::assumptions, never silent):
//  * VERTEX_COLOR: RGB = terrain[0].ambient + the accent lights terrain[1], terrain[2] only (the sun,
//    terrain[0], is lit per pixel by the shader, spec 2.12). BFME1's doTheLight adds EVERY global light
//    including the sun; which one BFME2's vertex colour carries is UNKNOWN (S-033).
//  * the light vectors are used as stored (ZH: lightRay = -lightPos, doTheLight dot product, clamped 0..1).
//  * water depth fade of BFME's doTheLight (useDepthFade) is not applied.

#pragma once

#include "GameClient/MapChunks.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <cstdint>
#include <string>
#include <vector>

#define VERTEX_BUFFER_TILE_LENGTH 32 // ZH HeightMap.h:45

enum VertexColorMode
{
	VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS = 0, // default, see the header comment
	VERTEX_COLOR_ALL_GLOBAL_LIGHTS = 1           // BFME1 doTheLight verbatim (sun included)
};

struct HeightMapMeshOptions
{
	TerrainUvOptions uv;
	VertexColorMode colorMode = VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS;
	int chunkCells = VERTEX_BUFFER_TILE_LENGTH;
};

struct TerrainLayerMesh
{
	std::vector<float> position;     // xyz, 3 per vertex
	std::vector<float> normal;       // xyz
	std::vector<std::uint8_t> color; // rgba
	std::vector<float> uv;           // uv, 2 per vertex
	std::vector<float> wrap;         // class block to wrap the UV into, 4 per vertex (x0,y0,w,h; w == 0: none), see ClassWrapRect
	std::vector<std::uint32_t> index;
	std::vector<std::uint32_t> cellId;  // per cell (4 vertices, 6 indices): y * (width-1) + x of the height-grid cell
	std::vector<std::uint8_t> cellFlip; // per cell: 1 when the cell is drawn with the SE-NW diagonal
	size_t vertexCount() const { return position.size() / 3; }
	size_t cellCount() const { return index.size() / 6; }
};

struct TerrainChunk
{
	int cellX0 = 0, cellY0 = 0, cellX1 = 0, cellY1 = 0; // cell range [x0,x1) x [y0,y1) in height-grid indices
	TerrainLayerMesh layer[3];
	float boundsMin[3] = {}, boundsMax[3] = {}; // SAGE space
};

struct TerrainMeshStats
{
	size_t cells = 0;          // (w-1)*(h-1)
	size_t baseCells = 0;
	size_t blendCells = 0;
	size_t extraCells = 0;
	size_t flippedCells = 0;      // base-layer triangles flipped (diagonal SE-NW)
	size_t extraFlippedCells = 0; // extra-layer own flips
	size_t cliffUvCells = 0;      // base tile whose UVs came from the cliff info
	size_t wrapCells = 0;         // base cells whose UVs carry a class wrap rect (hypothesis unit)
	size_t straddlingCells = 0;   // of those, cells with a corner outside the class block (need the per-pixel wrap)
	size_t cliffForcedFlips = 0;
	size_t missingTextureCells = 0; // base tile with no pixels (UV (0,0))
	size_t triangles = 0;
	size_t vertices = 0;
	size_t chunks = 0;
	int timeOfDayIndex = 0;     // 0..3 used for the lights
	std::vector<std::string> assumptions; // named, reported
};

namespace HeightMapMesh
{
// `lighting` may be null (no GlobalLighting chunk): every vertex colour is then white, reported.
bool build(const WorldHeightMap &map, const GlobalLightingData *lighting, const HeightMapMeshOptions &options,
	std::vector<TerrainChunk> &chunks, TerrainMeshStats &stats, std::string *error);

// Vertex colour of ZH doTheLight as configured by options.colorMode: clamped to [0,1].
void vertexLight(const TimeOfDayLights &lights, VertexColorMode mode, const float normal[3], float rgb[3]);

// ZH updateVB vertex normal of grid vertex (gx,gy): central differences with edge clamping.
void gridNormal(const WorldHeightMap &map, int gx, int gy, float normal[3]);

// The static diffuse (vertex colour bytes) of grid vertex (gx,gy), as build() stores it; white when
// `lighting` is null. Used by roads (ZH RoadSegment::updateSegLighting reads getStaticDiffuse).
void staticDiffuseAt(const WorldHeightMap &map, const GlobalLightingData *lighting, VertexColorMode mode, int gx, int gy, std::uint8_t rgb[3]);
} // namespace HeightMapMesh
