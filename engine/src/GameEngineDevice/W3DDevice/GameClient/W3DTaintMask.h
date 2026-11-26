// OpenBFME. GPL-3.0.
//
// W3DTaintMask (stop S-5261; TOP20 "Fire": BurntTerrainColor / FuelIndicatorColor; Local 67 / fork issue 512): the terrain's taint mask, the texture
// terrain.fxo's Taint struct reads. RotWK keeps it in a second instance of W3DShroud's class at BaseHeightMap + 0x387C (the shroud's is + 0x3878; both
// made over the height map at GameData PartitionCellSize, RW 0x46CF61 / 0x46CF8D -> RW 0x4732A9). Client only.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; DONOR BFME2 0x00073CC0 W3DTaint.cpp, 0x000E1A39 HeightMapInitHeightDataRva000E1A39.cpp, 0x001149EB
// W3DTerrainBackground.cpp):
//   * the cell write RW 0x4735AB(x, y, value, flag), from TheTaintManager's callback (RW 0x6D8B6E -> TheDisplay slot 0x13C = RW 0x4449C4) and from the
//     shroud's cell write (RW 0x44499D, with TheTaintManager's value at the cell): nothing without texels (+ 0x18), outside the cells or without GameData
//     TaintOn (+ 0xC6A); the texel is value << 24 | R << 16 | G << 8 | B with each channel the colour times a factor through _ftol (RW 0xA3CFA4).
//     - When the terrain draws with its ps_2_0 effect (render object RW 0xDD169C slot 0x24C = RW 0x4E0F5D: the effect's + 0x1C above 0, RW 0x4738F3 ..
//       0x4739A4), the colour is TheFireManager's (RW 0xDE4EFC) slot 0x3C at the cell's centre (trunc((x + 0.5) * cellW), trunc((y + 0.5) * cellH), SSE)
//       and the factor 255.
//     - Else (mode <= 0, RW 0x473659 .. 0x4738F1, terrain.fxo technique Default_L ps_1_1 blobs 0 .. 9 multiplying albedo * TaintMask.rgb): for value < 0x80
//       (RW 0x47365F .. 0x473743), t = (0x80 - value) * (1 / 128) (RW 0xBDAD58) and the base colour is GameData TaintColor (+ 0xBEC); for value > 0x80
//       (RW 0x473748 .. 0x473831), t = (value - 0x80) * (1 / 127) (RW 0xBDC534) and the base colour is ElvenWoodColor (+ 0xBF8); for value == 0x80, white;
//       the per-cell pattern offsets are dr = (x % 5 == 0 && y % 2 == 0) ? 0.03 : -0.02 (RW 0xBDC540 / 0xBDC53C), dg = (x % 3 == 0 && y % 5 == 0) ? -0.02 :
//       0.03, db = (x % 2 == 0 && y % 3 == 0) ? -0.03 : 0.02 (RW 0xBDC538 / 0xBDC320); each channel is ((c + d) * t + (1 - t)) * fireChannel (RW 0x473836 ..
//       0x4738CC), and the factor is the shroud level at (x, y) (RW 0x4738D1 .. 0x4738F1, 255 without shroud).
//   * TheFireManager slot 0x3C RW 0x497F60(out, x, y): white without TheFireLogicSystem; else the cell type (RW 0x686459): 1 (burning) -> the manager's
//     + 0x20 (FuelIndicatorColor, copied from the Fire block + 0x14 at RW 0x70C5ED), 2 (burnt out) -> + 0x14 (BurntTerrainColor, from + 0x08 at RW
//     0x70C5E2), else white.
//   * terrain.fxo (_Default ps_2_0, blobs 35, 36, 40, 41, 45, 46, 50, 51): a = mask.a; d = 2|a - 0.5|; the taint texture is TaintLow (TSTaintMed.tga,
//     BaseHeightMap + 0x3840, RW 0x46C5DB) for a <= 0.5, else TaintHigh (TSElvenMed.tga, + 0x384C, RW 0x46C5FC), both sampled at oT0 = v3 (vs blob 60
//     @ 0x1c780, WRAP, LINEAR); albedo = Taint.IsEnabled ? mask.rgb * lerp(albedo, taint, d) : albedo.
//     - On flat terrain, FlatHeightMapRenderObjClass::initHeightData (RW 0x4E133F at RW 0x4E148F .. 0x4E14C2, DONOR BFME2 0x000E1A39) partitions the map
//       into 16x16-cell W3DTerrainBackground tiles (m_xOrigin = tileX * 16, m_yOrigin = tileY * 16, m_width = 16), and W3DTerrainBackground::doTesselatedUpdate
//       (RW 0x514398 at RW 0x514628 .. 0x51465F, DONOR BFME2 0x001149EB) writes v3 = ((i - minX) / 16, 1 - (j - minY) / 16) at vertex (i, j) where
//       world.xy = (i - borderSize, j - borderSize) * cellSize; because minX, minY are multiples of 16 and TaintLowSampler / TaintHighSampler wrap, one
//       512x512 taint texture spans 16x16 map cells (160x160 world units, 32x32 texels per cell): ((world.x + borderWidth) / (16 * cellSize),
//       1 - (world.y + borderWidth) / (16 * cellSize)).
//     - On cliff cells, W3DTerrainBackground::doPartialUpdate (RW 0x5149F1, DONOR BFME2 0x00115044) writes TCliffInfo's normalized class UV (RW 0x4ADE8F).
//     - The mask's UV (vs): (world.xy + Offset) * Scale with Offset = cell size - W3DTaint + 0x2C / + 0x30 (RW 0x4E2033) and Scale = 1 / (texture size *
//       cell size) (RW 0x4E2096); IsEnabled = TaintOn (RW 0x4E2002); the mask sampler CLAMP, LINEAR.
//   * the texture (RW 0x473E83 .. 0x473EE0, the shroud's layout): numCells + 2 texels a side; a dirty border (+ 0x35) fills it all through RW 0x47321C with
//     the border level (+ 0x36 = GameData TaintAlpha, RW 0x46CEC7) in all four bytes, then the cells are copied at texel (1, 1).
// INFERENCE (S-5261): the mask is rebuilt from the presented TaintView whenever TheTaintManager or the fire grid moved (retail writes the changed cells).

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <vector>

struct FireSetting;

// the logic's ground the mask is made from, as the presented snapshot carries it (LogicSnapshot::taint)
struct TaintView
{
	std::uint64_t taintVersion = 0, fireVersion = 0;
	int countX = 0, countY = 0;   ///< TheTaintManager's cells
	float originX = 0.0f, originY = 0.0f, cellSize = 1.0f;
	std::vector<std::uint8_t> values;    ///< the cell values, row-major
	std::vector<std::uint8_t> fireTypes; ///< TheFireLogicSystem's type at each cell's centre (RW 0x686459), row-major
};

namespace W3DTaintMask
{
// RW 0x4E14A1: FlatHeightMapRenderObjClass::initHeightData partitions the terrain into 16x16-cell W3DTerrainBackground tiles
inline constexpr int kBackgroundTileCells = 16;

// RW 0x497F60
RGBColor fireColour(const FireSetting &fire, int type);
// RW 0x4738F3 .. 0x4739A4 (the shader path, mode > 0): value << 24 | (R * 255) << 16 | (G * 255) << 8 | B * 255, each product truncated (_ftol)
std::uint32_t texel(int value, const RGBColor &colour);
// RW 0x47365F .. 0x4738CC (the Default_L / fixed-function path, mode <= 0): TaintColor / ElvenWoodColor with per-cell pattern, times the fire colour
RGBColor lowColour(int x, int y, int value, const RGBColor &taintColor, const RGBColor &elvenWoodColor,
	const RGBColor &fire = RGBColor{ 1.0f, 1.0f, 1.0f });
// RW 0x473659 .. 0x4739A4 (mode <= 0): value << 24 | (R * shroudLevel) << 16 | (G * shroudLevel) << 8 | B * shroudLevel, truncated (_ftol)
std::uint32_t lowTexel(int x, int y, int value, const RGBColor &taintColor, const RGBColor &elvenWoodColor,
	const RGBColor &fire = RGBColor{ 1.0f, 1.0f, 1.0f }, std::uint8_t shroudLevel = 0xFF);
// RW 0x473905 .. 0x473928: trunc((n + 0.5) * cellSize) in SSE
int cellCentre(int n, float cellSize);
// RW 0x514628 .. 0x51465F: W3DTerrainBackground::doTesselatedUpdate vertex UV ((cellX - minX) / width, 1 - (cellY - minY) / width)
void tileVertexUv(int cellX, int cellY, int tileOriginX, int tileOriginY, float &outU, float &outV, int tileWidth = kBackgroundTileCells);
// RW 0x514628 .. 0x51465F with minX = tileX * 16, minY = tileY * 16 under TaintLowSampler / TaintHighSampler WRAP:
// ((worldX + borderWidth) / (16 * cellSize), 1 - (worldY + borderWidth) / (16 * cellSize))
void terrainUv(float worldX, float worldY, float borderWidth, float cellSize, float &outU, float &outV);
// the texture: (countX + 2) x (countY + 2) ARGB texels, the border `borderLevel` (TaintAlpha) in every byte
void build(const TaintView &view, const FireSetting &fire, std::uint8_t borderLevel, std::vector<std::uint32_t> &texels, int &width, int &height);
} // namespace W3DTaintMask
