// OpenBFME. GPL-3.0.
//
// Lane TERRAIN-BAKES: what retail's terrain does to its textures before the effect sees them, and which tiles get which resolution.
//
// Retail never draws ordinary terrain from an atlas. The one terrain render object is FlatHeightMapRenderObjClass (BFME2 W3DTerrainVisualInit.cpp:233, RW's
// W3DTerrainVisual::init), whose W3DTerrainBackground tiles (16 x 16 cells, FlatHeightMap.cpp rva000E1C1A's `i * 16`) bake their ground on the CPU:
//   - a tile nearer than the 2x distance (below) gets a 32 px/cell A1R5G5B5 texture and a 32 px/cell normal texture (UpdateTexture.cpp:124, getFlatTexture(.., 0x20, 0x19, kind));
//   - every other tile gets 16 px/cell (DoTesselatedUpdate.cpp:429-432: DXT1 for the colour when TheGameClient is set, A1R5G5B5 for the normal map).
// A baked cell without a blend tile copies the source tile's 16-bit texels; a cell with one loads the raw 32-bit tile, mixes the blend tile(s) in (blendTile,
// RW 0x4AC035's MMX loop: (src * alpha + (255 - alpha) * dst) >> 8 per byte) and updateFlat drops the low 3 bits of every channel into an A1R5G5B5 texel with the alpha bit
// set. The effect then lights that composite texel ONCE.
//
// TARGET FACTS (RotWK 2.01, game.dat; pinned in tools/render/test_terrain_bake_binary_facts.py): RW 0x4AC035 is the blend loop; RW 0x511DCB .. 0x511F3B is
// W3DTerrainBackground::updateCenter's distance rule (constants 260.0f at 0xBE5AF0, 170.0f at 0xBE5AEC, 25.0f at 0xC69574, 1 / 1024 at 0xBE5798, the clamp 0.5 /
// 2.5 at 0xBD869C / 0xBE5AE8), GlobalData + 0x4A the allow-2x flag and + 0xEA0 the stretch flag. DONOR (BFME2 1.06, same code): the tile width, the bake
// formats and the conversions. INFERENCE: the tile bounds are the box of every vertex of the tile (the tesselated mesh's vertex set was not read).
#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <cstdint>
#include <vector>

namespace TerrainBake
{
enum { TILE_CELLS = 16, NEAR_PIXELS_PER_CELL = 32, FAR_PIXELS_PER_CELL = 16 };

// RW 0x4AC035 (BFME2 Rva000AC63FBlendPixels): one byte of the blend loop, dst = (src * alpha + (255 - alpha) * dst) >> 8. The 16-bit lanes hold the products
// (at most 255 * 255 + 0), so nothing wraps; a full alpha is not an identity (255 * 255 >> 8 = 254).
inline std::uint8_t blendByte(std::uint8_t src, std::uint8_t dst, std::uint8_t alpha)
{
	return (std::uint8_t)(((unsigned)src * alpha + (255u - alpha) * dst) >> 8);
}

// updateFlat's conversion (BFME2 TerrainTex.cpp EF39F..EF43E): B8G8R8 -> A1R5G5B5 by dropping the low 3 bits of each channel, alpha bit set
inline std::uint16_t toA1R5G5B5(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
	return (std::uint16_t)(0x8000u | ((unsigned)(r >> 3) << 10) | ((unsigned)(g >> 3) << 5) | (unsigned)(b >> 3));
}

// Direct3D's read of a 5-bit channel as an 8-bit value (n * 255 / 31, rounded)
inline std::uint8_t expand5(unsigned n)
{
	return (std::uint8_t)((n * 255u + 15u) / 31u);
}

inline std::uint8_t channelR(std::uint16_t t) { return expand5((t >> 10) & 31u); }
inline std::uint8_t channelG(std::uint16_t t) { return expand5((t >> 5) & 31u); }
inline std::uint8_t channelB(std::uint16_t t) { return expand5(t & 31u); }

// The texel a blended cell bakes to: the raw 8-bit base, the blend tile mixed in by its alpha, then updateFlat's conversion
inline std::uint16_t bakeBlended(const std::uint8_t base[3], const std::uint8_t blend[3], std::uint8_t alpha)
{
	return toA1R5G5B5(blendByte(blend[0], base[0], alpha), blendByte(blend[1], base[1], alpha), blendByte(blend[2], base[2], alpha));
}

// ---- the distance rule ----------------------------------------------------------------------------------------------------------------------------
struct Box
{
	float center[3] = {};
	float extent[3] = {}; // half sizes
};

// W3DTerrainBackground::updateCenter (RW 0x511DCB .. 0x511F3B): the squared distance from the camera to the nearest of the box's 27 points (centre + extent * {-1, 0, 1}
// per axis), the three nested loops' float operations in order
float nearestPointDistanceSquared(const Box &box, const float camera[3]);

// the 2x distance: scale = displayWidth / 1024 clamped to 0.5 .. 2.5 (no display: the constants' own 260 and 170), 260 * scale + (170 * scale + 25)
float mip2xDistance(bool hasDisplay, unsigned displayWidth);

// GlobalData + 0x4A (allow 2x) and + 0xEA0 (stretch the terrain) as retail reads them: 2 when the tile is nearer than the 2x distance (or stretched), else 1
int textureMultiplier(const Box &box, const float camera[3], bool hasDisplay, unsigned displayWidth, bool allowTexture2X, bool stretchTerrain);

// ---- the tiles of a map ----------------------------------------------------------------------------------------------------------------------------
struct TileGrid
{
	int tilesX = 0, tilesY = 0;
	float originX = 0.0f, originY = 0.0f; // SAGE position of height-map sample (0, 0): -border * MAP_XY_FACTOR
	std::vector<Box> boxes;               // tilesX * tilesY, row major (y * tilesX + x)
};

// the 16 x 16 cell tiles of the map and the box of the vertices of each (samples origin .. min(origin + 16, extent - 1))
TileGrid buildTileGrid(const WorldHeightMap &hm);

// one flag per tile, 1 = a 16 px/cell tile (the multiplier is 1), 0 = a 32 px/cell tile
std::vector<std::uint8_t> farTiles(const TileGrid &grid, const float camera[3], bool hasDisplay, unsigned displayWidth, bool allowTexture2X, bool stretchTerrain);
} // namespace TerrainBake
