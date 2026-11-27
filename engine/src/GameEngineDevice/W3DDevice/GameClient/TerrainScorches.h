// OpenBFME. GPL-3.0.
//
// TerrainScorchBuffer: the scorch marks of the terrain render object (ZH / RotWK BaseHeightMapRenderObjClass: addScorch, updateScorches,
// drawScorches). Client / draw only: never read by the logic, never saved (RotWK has no xfer of the list: the map's Scorch objects are added
// again when the map loads). One store for every source: the FXList TerrainScorch nugget (W3DGameClient::addScorch RW 0x44C07E), the map's
// Scorch objects (W3DTerrainVisual::load RW 0x491EC3) and the burning terrain (the fire manager's update RW 0x49843D).
//
// TARGET (RotWK game.dat, read with tools/mod/rw.py; caveat S-001):
//   * addScorch RW 0x46765B (thiscall, by-value Vector3, radius, type; RET 0x14). The list is 500 entries of 0x1C bytes at + 0xE0 (location
//     + 0xE4, radius + 0xF0, type + 0xF4, the "hidden" byte + 0xF8), the count at + 0x3790, the "scorches in buffer" at + 0x3794, the oldest
//     slot at + 0x3798:
//       - a scorch that matches one of the list (|x - x'|, |y - y'| and |radius - radius'| each below radius * 0.25 (RW 0xBD1904), the same
//         type) is not added: that entry's hidden byte is cleared (RW 0x467778) and nothing else changes (+ 0x3794 is NOT cleared, so the
//         drawn buffer is not rebuilt for it);
//       - else, with 500 (RW 0x4676F4) in the list, the slot is the oldest one, the oldest moves on ((slot + 1) % 500) and the count drops by
//         one (RW 0x4676FC .. 0x46771C); the slot gets the location, radius, type and a cleared hidden byte, the count rises and + 0x3794 is
//         cleared (RW 0x46771E .. 0x46776A).
//   * RW 0x46947C (TheTerrainVisual slot 0x5C, forwarder RW 0x4918B8): every scorch whose radius + the given radius exceeds the 3D distance
//     (x87 sqrt) of its location from the given one is hidden; + 0x3794 is cleared. Its only caller is the radius clear RW 0x683393 (not
//     ported, S-5260).
//   * the list is emptied (count, + 0x3794 and the oldest slot zeroed) by the constructor (RW 0x46C554) and the map reset (RW 0x46CE22);
//     the reset leaves the drawn counts (+ 0xD8 / + 0xDC) alone, the terrain's next build zeroes them (RW 0x46D0A7 .. 0x46D0B3).
//   * updateScorches RW 0x467073 (called by drawScorches RW 0x468BCB first): nothing when + 0x3794 > 1, the list is empty or the buffers are
//     missing; else + 0x3794, the vertex count (+ 0xD8) and the index count (+ 0xDC) are zeroed and the list is walked from the newest entry
//     to the oldest index (count - 1 down to 0), skipping the hidden ones; every drawn one adds 1 to + 0x3794 (so with 0 or 1 drawn scorch the
//     buffer is rebuilt every frame, with more only after a change). Per scorch: the type outside 0 .. 8 is 0; the atlas cell is
//     (type % 3, type / 3) in thirds (RW 0xBDBC78); the cells minX = floor((x - r) * 0.1) - 1, minY likewise, maxX = ceil((x + r) * 0.1) + 1,
//     maxY likewise (0.1 = RW 0xBD83D4), clamped to [-border, extent - border]; a vertex per grid point of [minX, maxX) x [minY, maxY) at
//     (i * 10, j * 10, height(i + border, j + border) * 0.0390625 + 1) (RW 0xBDB8EC; the height read clamped to the map, RW 0x465CEA), u =
//     (X - left) / width / 3 + uOffset, v likewise, with left = minX * 10, width = (maxX - minX - 1) * 10; past 0x2002 vertices (RW
//     0x46741B) the scorch's vertices are dropped and the update ends; two triangles per cell, the cell's flip bit (RW 0x4AD985, the
//     WorldHeightMap flip plane at + 0x28 / stride + 0x34, set by RW 0x5138AC from RW 0x4AF24A) choosing the diagonal; past 0xC00C indices
//     (RW 0x467530) the update ends.
//   * the vertex colour (RW 0x4670E1, S-033): ambient(+ 0x8D8) + scale(+ 0x944) * diffuse(+ 0x8FC) * 0.5 (RW 0xBD869C), times 2.0 (RW
//     0xBD889C) when the overbright byte RW 0xD9B03C is set, each clamped to 1, times 255 (RW 0xBD88A8) truncated, alpha 0xFF.
//   * drawScorches RW 0x468BCB: no draw without indices or when hidden; the shader at RW 0xD9B2F0 (BFME2 decomp: ShaderClass::
//     _PresetAlphaShader) with the hardware fog when fog is on, the texture at + 0xD4 (RW 0x46D1D5: "exscorch01.tga", RW 0xBDBFC4), the
//     projection depth bias 1.0 (RW 0x51EB60), the sampler's U and V clamped (SetSamplerState 1 / 2 = CLAMP 3), Draw_Triangles of the index
//     count / 3 triangles.
// DONOR: BFME2 decomp BaseHeightMapAddScorch.cpp, BaseHeightMapRenderObjClass_updateScorches.cpp, BaseHeightMapScorchOverlap.cpp,
//   BaseHeightMapDrawScorches.cpp, BaseHeightMapScorchResources.cpp (the same bodies at BFME2 0x67EA3 / 0x678BB / 0x69C5F / 0x693AE).
// INFERENCE: the cell diagonal is the one the port's terrain mesh draws (HeightMapMesh's base layer); retail's flip plane is the one its terrain
//   draw reads, so the scorch sits on the drawn triangles in both.

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class MapObject;
class WorldHeightMap;
struct TerrainChunk;
struct TimeOfDayLights;

class TerrainScorchBuffer
{
public:
	static constexpr int MAX_SCORCHES = 500;            // RW 0x4676F4 (cmp 0x1F4), the list at + 0xE0
	static constexpr int MAX_SCORCH_VERTEX = 0x2002;    // RW 0x46741B
	static constexpr int MAX_SCORCH_INDEX = 0xC00C;     // RW 0x467530
	static constexpr int SCORCH_MARKS_IN_TEXTURE = 9;   // RW 0x467212 (cmp 9)
	static constexpr int SCORCH_PER_ROW = 3;            // RW 0x467219 (idiv 3)
	static constexpr float MATCH_FRACTION = 0.25f;      // RW 0xBD1904
	static constexpr const char *TEXTURE = "exscorch01.tga"; // RW 0xBDBFC4 (RW 0x46D1D5)

	struct Scorch
	{
		Coord3D location;     ///< + 0x04
		float radius = 0.0f;  ///< + 0x10
		int type = 0;         ///< + 0x14 (SCORCH_1 .. SCORCH_9 = 0 .. 8)
		bool hidden = false;  ///< + 0x18
	};

	// RW 0x46765B
	void addScorch(const Coord3D &location, float radius, int type);
	// RW 0x46947C (TheTerrainVisual slot 0x5C)
	void hideScorchesInRadius(const Coord3D &location, float radius);
	// RW 0x46CE22 (the map reset) / RW 0x46C554 (the constructor)
	void clearAllScorches();
	// W3DTerrainVisual::load RW 0x491E36 .. 0x491EC3: every map object with the scorch flag (+ 0x44 bit 3, set by RW 0x70F5AE for a scorchType
	// INT), in map order: addScorch(location, the REAL "objectRadius" (NameKey RW 0xDA2E84 -> RW 0xC1F840, Dict::getReal RW 0x714ACA), the INT
	// "scorchType" (RW 0xDA2E9C -> RW 0xC1F874, Dict::getInt RW 0x714A98)). A Scorch object without a REAL objectRadius is an error in `errors`
	// and is not added (retail's getReal would give 0). Returns how many were added.
	static int addMapScorches(TerrainScorchBuffer &buffer, const std::vector<MapObject> &objects, std::vector<std::string> &errors);

	int numScorches() const { return m_numScorches; }
	int nextScorch() const { return m_nextScorch; }
	int scorchesInBuffer() const { return m_scorchesInBuffer; }
	const Scorch &scorch(int i) const { return m_scorches[(size_t)i]; }

	// ---- the drawn buffer (updateScorches RW 0x467073) ----
	struct Vertex
	{
		float x = 0, y = 0, z = 0;  ///< SAGE world space
		std::uint32_t diffuse = 0;  ///< ARGB
		float u = 0, v = 0;
	};
	// What updateScorches reads of the terrain: the height map (RW 0x37C0: extents + 8 / + 0xC, border + 0x10, heights + 0x24) and its
	// flip plane (RW 0x4AD985). `flip` may be empty (every cell unflipped).
	struct Terrain
	{
		int xExtent = 0, yExtent = 0, border = 0;
		std::vector<std::uint16_t> heights;  ///< row-major, y * xExtent + x
		std::vector<std::uint8_t> flip;      ///< per height-grid cell (y * xExtent + x): 1 = the SE-NW diagonal
		bool valid() const { return xExtent > 0 && yExtent > 0 && heights.size() == (size_t)xExtent * (size_t)yExtent; }
	};
	// The terrain lights updateScorches reads (GlobalData + 0x8D8 / + 0x8FC: terrain light 0 of the time of day, + 0x944: the map's
	// light scale) and the overbright byte RW 0xD9B03C.
	struct Shade
	{
		float ambient[3] = { 0, 0, 0 };
		float diffuse[3] = { 0, 0, 0 };
		float scale[3] = { 1, 1, 1 };
		bool overbright = false;
	};
	// RW 0x4670E1 .. 0x4671A4
	static std::uint32_t scorchDiffuse(const Shade &shade);

	// ---- the depth bias of the draw (drawScorches RW 0x468C43: Set_DX8_ZBias(1.0) RW 0x51EB60, back to 0.0 at RW 0x468CAE) ----
	static constexpr float DEPTH_BIAS = 1.0f;       // RW 0x468C43 (fld1)
	static constexpr float ZBIAS_SCALE = 0.000625f; // RW 0xBE6CF8
	// RW 0x51EB60: a changed bias copies the projection (RW 0xDD3670) and, when ZNear (RW 0xDD1F30) differs from ZFar (RW 0xDD3438), takes
	// zbias * ZBIAS_SCALE * (zfar * znear / (zfar - znear)) off its _43 (RW 0xDD3668), the projection set again (SetTransform 3). The depth a
	// point at eye distance d gets under the D3D projection (_33 = zfar / (zfar - znear), _43 = -znear * zfar / (zfar - znear)): _33 + _43' / d.
	static float zBiasedDepth(float zbias, float znear, float zfar, float d);
	// The same depth as a point drawn without bias: _43 grows by the factor (1 + zbias * ZBIAS_SCALE), which is the depth of the point at
	// d / (1 + zbias * ZBIAS_SCALE). The draw scales the eye-space vertex by this (the screen position stays, the camera's near and far drop out).
	static float depthBiasViewScale(float zbias);
	// The height map as updateScorches reads it, the cell diagonals taken from the base layer of the terrain mesh as drawn (HeightMapMesh, cellId /
	// cellFlip; the INFERENCE above)
	static Terrain terrainFrom(const WorldHeightMap &map, const std::vector<TerrainChunk> &chunks);
	// GlobalData + 0x8D8 / + 0x8FC: the time of day's terrain light 0 (null: no GlobalLighting chunk, ambient and diffuse 0); `scale` the map's light
	// scale as RW 0x4670E1 reads it (TerrainLightScale::Scales::scorch: always the raw scale); `overbright` RW 0xD9B03C
	static Shade shadeFrom(const TimeOfDayLights *lights, const float scale[3], bool overbright);

	// RW 0x467086 .. 0x4670B6: whether a draw rebuilds the buffer
	bool needsUpdate() const { return m_scorchesInBuffer <= 1 && m_numScorches != 0; }
	// RW 0x467073: rebuilds the buffer when needsUpdate(); returns true when it did (the revision then moves on)
	bool updateScorches(const Terrain &terrain, const Shade &shade);
	// RW 0x46ACBA / 0x46D0A7 / 0x467C02: the buffers were made again (the terrain was built): the next draw rebuilds
	void invalidate() { m_scorchesInBuffer = 0; m_vertices.clear(); m_indices.clear(); }

	const std::vector<Vertex> &vertices() const { return m_vertices; }
	const std::vector<std::uint16_t> &indices() const { return m_indices; }
	std::uint64_t revision() const { return m_revision; }

private:
	std::vector<Scorch> m_scorches = std::vector<Scorch>(MAX_SCORCHES);
	int m_numScorches = 0;       ///< + 0x3790
	int m_scorchesInBuffer = 0;  ///< + 0x3794
	int m_nextScorch = 0;        ///< + 0x3798
	std::vector<Vertex> m_vertices;        ///< + 0xCC, its count + 0xD8
	std::vector<std::uint16_t> m_indices;  ///< + 0xD0, its count + 0xDC
	std::uint64_t m_revision = 0;
};
