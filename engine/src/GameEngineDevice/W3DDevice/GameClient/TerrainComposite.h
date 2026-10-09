// OpenBFME. GPL-3.0.
//
// The terrain draw layers, re-expressed for a renderer that cannot blend in gamma space.
//
// Retail (D3D9) draws the base layer opaque and the two blend layers with SRCALPHA/INVSRCALPHA straight
// into a gamma-space framebuffer (spec maps-and-terrain.md 3.2, 3.10; the blend state itself is INFERRED,
// stop S-032). Godot blends in a linear framebuffer, so blending the layers as separate draws gives
// different colours (a half-alpha white over black is 0.73 instead of 0.5). The composite therefore draws
// ONE opaque mesh per chunk (the base layer's vertices) and evaluates all three layers per pixel in the
// shader, in gamma space, converting once after the composite (terrain_common.gdshaderinc).
//
// What each layer needs per pixel is its UV and alpha AS THE GPU WOULD HAVE INTERPOLATED THEM over that
// layer's own triangulation (a layer may be flipped differently from the base layer, S-032). They are
// stored per cell: the four corner UVs, the four corner alphas, the class wrap rect and the flip, and the
// shader interpolates them with interpolateCorner() below, which reproduces the barycentric interpolation
// of the two triangles the layer's index buffer draws (the unit test checks it against those triangles).
//
// Data layout (RGBA32F texels, one record of RECORD_TEXELS texels per cell that has a blend layer):
//   0: layer 1 corner UV SW.xy, SE.xy      4: layer 2 corner UV SW.xy, SE.xy
//   1: layer 1 corner UV NE.xy, NW.xy      5: layer 2 corner UV NE.xy, NW.xy
//   2: layer 1 alpha SW, SE, NE, NW (0..1) 6: layer 2 alpha SW, SE, NE, NW
//   3: layer 1 wrap rect x0,y0,w,h         7: layer 2 wrap rect
//   8: flip1, flip2, has1, has2 (0 / 1)
// Record r is texel (RECORD_TEXELS*(r % RECORDS_PER_ROW) + k, r / RECORDS_PER_ROW).

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.h"

#include <cstdint>
#include <vector>

namespace TerrainComposite
{
enum { RECORD_TEXELS = 9, RECORDS_PER_ROW = 512 };

struct Chunk
{
	std::vector<float> position;     // xyz, 3 per vertex (SAGE space)
	std::vector<float> normal;       // xyz
	std::vector<std::uint8_t> color; // rgba (a = 255: the layers' alphas live in the records)
	std::vector<float> uv;           // base layer UV, 2 per vertex
	std::vector<float> local;        // cell-local coordinates of the corner: SW (0,0) SE (1,0) NE (1,1) NW (0,1)
	std::vector<float> wrap;         // base layer class wrap rect, 4 per vertex
	std::vector<float> record;       // 1 per vertex: record index, or -1 when the cell has no blend layer
	std::vector<std::uint32_t> index; // the base layer's triangles
	size_t vertexCount() const { return position.size() / 3; }
};

struct Records
{
	int width = 0;  // texels
	int height = 0; // rows
	size_t count = 0;
	std::vector<float> texels; // RGBA32F, width * height * 4
};

struct Stats
{
	size_t cells = 0;
	size_t recordCells = 0;   // cells with a layer 1 and/or layer 2
	size_t layer1Cells = 0;
	size_t layer2Cells = 0;
	size_t layer2FlipDiffers = 0; // layer 2 uses the other diagonal than the base layer (needs the emulation)
};

// Builds the composite from the three layer meshes of HeightMapMesh::build. Returns false + *error when the
// layers are not consistent subsequences of the base layer's cells.
bool build(const std::vector<TerrainChunk> &chunks, std::vector<Chunk> &out, Records &records, Stats &stats, std::string *error);

// Reference of what the shader does for one corner attribute: the value at cell-local point (lx, ly) of the
// quad whose corner values are c[0..3] = SW, SE, NE, NW, drawn as ZH's two triangles. flip == false: diagonal
// SW-NE ((SW,SE,NE) below it, (SW,NE,NW) above); flip == true: diagonal SE-NW ((SW,SE,NW), (SE,NE,NW)).
float interpolateCorner(const float c[4], bool flip, float lx, float ly);
} // namespace TerrainComposite
