// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// WorldHeightMap: heights, tile/blend/cliff index arrays and the per-cell bit planes of a map.
//
// Sources (priority order):
//  * target facts (BFME1 retail decompile): Open-BFME-1 GameEngineDevice/Source/W3DDevice/GameClient/
//      WorldHeightMapParseHeightMapData.cpp, WorldHeightMapRva0074ACB0Load.cpp (BlendTileData load:
//      the blend, cliff and extra index arrays are Int in BFME, not Short as in ZH),
//      WorldHeightMapGetUVForTileIndex.cpp, WorldHeightMapGetExtraAlphaUVData.cpp (UV and alpha).
//  * target facts (BFME2 decompile, TU-local helpers): Open-BFME-2 .../WorldHeightMapTiles.cpp
//      (countTiles, up to 16x16 tiles), TileDataGetRGBDataForWidth.cpp (tiles stored 16 bit).
//      NOTE: Open-BFME-2's WorldHeightMap.cpp is ZH text pasted in ("present-unmatched") and is
//      NOT evidence.
//  * donor: ZH GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp (ParseHeightMapData
//      :890-944, ParseBlendTileData :1071-1230, patch-bad-maps :533-545, bit planes :733-740).
//  * layout of the v10..v17 planes: spec maps-and-terrain.md 1.7.2 (verified: all 181 retail maps
//      parse with zero leftover bytes). The plane NAMES (impassable-to-players, passage widths,
//      taintability, extra passability, visibility) come from OpenSAGE and their semantics beyond
//      plane 0 (cell cliff state) are UNKNOWN (spec 2.4); they are parsed and exposed unnamed
//      by role.
//
// Coordinates and scale are stated once in spec 2.1: one cell is MAP_XY_FACTOR (10) world units,
// one raw height unit is MAP_HEIGHT_SCALE (10/256), sample (i,j) is at x=(i-border)*10,
// y=(j-border)*10, z=h[j*w+i]*MAP_HEIGHT_SCALE.

#pragma once

#include "Common/MapObject.h"
#include "Common/INIDataTypes.h" // ICoord2D
#include "Common/MapReaderWriterInfo.h"

#include <cstdint>
#include <string>
#include <vector>

class DataChunkInput;
struct DataChunkInfo;


// ZH WorldHeightMap.h TBlendTileInfo, with the BFME layout (spec 1.7.2).
struct TBlendTileInfo
{
	std::int32_t blendNdx = 0;
	std::uint8_t horiz = 0, vert = 0, rightDiagonal = 0, leftDiagonal = 0;
	std::uint8_t inverted = 0; // bit0 INVERTED_MASK, bit1 FLIPPED_MASK (ZH TileData.h:51-52)
	std::uint8_t longDiagonal = 0;
	std::int32_t customBlendEdgeClass = -1;
};

// ZH TCliffInfo
struct TCliffInfo
{
	std::int32_t tileIndex = 0;
	float u0 = 0, v0 = 0, u1 = 0, v1 = 0, u2 = 0, v2 = 0, u3 = 0, v3 = 0;
	bool flip = false;
	bool mutant = false;
};

// ZH TXTextureClass
struct TXTextureClass
{
	std::int32_t firstTile = 0;
	std::int32_t numTiles = 0;
	std::int32_t width = 0; // class edge in 64-px tiles
	std::int32_t legacy = 0; // ZH "isGDF"; 0 in all 6945 corpus classes
	std::string name;
	ICoord2D positionInTexture; // top-left of the class block in the terrain atlas (set by the atlas packer); x == 0 = not placed
};

// ZH TILE_PIXEL_EXTENT / TEXTURE_WIDTH / TILE_OFFSET (TileData.h:53, 76; TerrainTex.h:41)
#define TILE_PIXEL_EXTENT 64
#define TERRAIN_TEXTURE_WIDTH 2048
#define TERRAIN_TILE_OFFSET 8
#define TERRAIN_ATLAS_MAX_HEIGHT 8192 // largest texture the packer may use (GPU limit); ZH stopped at 28 rows

// Where a source tile sits in the atlas. present == false: the class texture could not be loaded
// (ZH m_sourceTiles[i] == NULL), so every UV that names the tile is (0,0).
struct TileLocation
{
	int x = 0, y = 0;
	bool present = false;
};

// UNKNOWN (spec maps-and-terrain.md 2.3, 3.5, 6.1): what unit the cliff-info UVs are in for BFME2/RotWK.
//  * CLIFF_UV_ATLAS_2048: the donor formula, kept verbatim. TARGET FACT for BFME1 (Open-BFME-1
//    WorldHeightMapGetUVForTileIndex.cpp); ZH has the same. u/v are fractions of the 2048-px atlas.
//  * CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP: INFERRED from the BFME2/RotWK corpus only (one-cell edges
//    cluster at 0.125 for width-4/6/8 classes, offsets run past the class by several class spans):
//    u/v are in units of 256 px measured from the class block's left edge / bottom edge, and a cell's
//    four corners wrap together inside the class block. No decompiled BFME2 body confirms this.
enum CliffUvUnit
{
	CLIFF_UV_ATLAS_2048 = 0,
	CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP = 1
};

// Where a cliff cell's UVs must be wrapped: the texture class block it samples, as fractions of the atlas
// (x0,y0 = top-left corner, w,h = size; w == 0: no wrap). Only the CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP
// unit produces one. The UVs of such a cell are NOT clamped into the class block: the corners are the
// unwrapped positions (a cell whose four corners straddle the class edge has corners outside it), and the
// sampler wraps every PIXEL into the block, so no texel of another class is ever read
// (TerrainUv::wrapIntoClass is the reference of that per-pixel wrap; the terrain shader mirrors it).
struct ClassWrapRect
{
	float x0 = 0.0f, y0 = 0.0f, w = 0.0f, h = 0.0f;
	bool valid() const { return w > 0.0f && h > 0.0f; }
};

namespace TerrainUv
{
// Wraps the atlas UV (u,v) into `rect`: p' = origin + mod(p - origin, size) per axis, with mod() taking the
// sign of the divisor (GLSL mod semantics), so negative offsets wrap too. A rect with w == 0 returns the UV
// unchanged.
void wrapIntoClass(const ClassWrapRect &rect, float u, float v, float &outU, float &outV);
} // namespace TerrainUv

struct TerrainUvOptions
{
	// GameData.ini "AdjustCliffTextures = Yes" in RotWK 2.01 (data/ini/gamedata.ini)
	bool adjustCliffTextures = true;
	CliffUvUnit cliffUnit = CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP;
};

class WorldHeightMap
{
public:
	// ---- HeightMapData ----
	int m_width = 0;
	int m_height = 0;
	int m_borderSize = 0;
	int m_dataSize = 0;
	int m_heightMapVersion = 0;
	std::vector<ICoord2D> m_boundaries; // playable extents in cells, origin = (border, border)
	std::vector<std::uint16_t> m_data;  // row-major, index = y*width + x

	// ---- BlendTileData ----
	bool m_hasBlendTileData = false;
	int m_blendTileVersion = 0;
	std::vector<std::int16_t> m_tileNdxes;
	std::vector<std::int32_t> m_blendTileNdxes;      // 0 = no blend
	std::vector<std::int32_t> m_extraBlendTileNdxes; // 0 = none ("3-way" second blend layer)
	std::vector<std::int32_t> m_cliffInfoNdxes;      // 0 = default UVs

	int m_planeStride = 0; // (width+7)/8 bytes per row, all planes
	std::vector<std::uint8_t> m_cellCliffState;   // v>=7: ZH m_cellCliffState (impassable / cliff)
	std::vector<std::uint8_t> m_planeA;           // v>=10  (OpenSAGE: ImpassabilityToPlayers) semantics UNKNOWN
	std::vector<std::uint8_t> m_planeB;           // v>=11  (OpenSAGE: PassageWidths)           semantics UNKNOWN
	std::vector<std::uint8_t> m_planeTaint;       // 14<=v<25 (OpenSAGE: Taintability)          semantics UNKNOWN
	std::vector<std::uint8_t> m_planeExtraPass;   // v>=15  (OpenSAGE: ExtraPassability)        semantics UNKNOWN
	std::vector<std::uint8_t> m_flammability;     // 16<=v<25, one byte per cell
	std::vector<std::uint8_t> m_planeVisibility;  // v>=17  (all ones in every retail map)

	int m_numBitmapTiles = 0;
	int m_numBlendedTiles = 1; // including the dummy entry 0
	int m_numCliffInfo = 1;    // including the dummy entry 0
	std::vector<TXTextureClass> m_textureClasses;
	int m_numEdgeTiles = 0;
	std::vector<TXTextureClass> m_edgeTextureClasses;
	std::vector<TBlendTileInfo> m_blendedTiles; // index 0 is a zeroed dummy
	std::vector<TCliffInfo> m_cliffInfo;        // index 0 is a zeroed dummy

	// CRC-32 of the arrays as stored in the file (index arrays at their stored width, before the
	// patch pass below). The corpus test compares them with an independent oracle.
	struct RawCrc
	{
		std::uint32_t tile = 0, blend = 0, extraBlend = 0, cliffInfo = 0, cliffState = 0;
	};
	RawCrc m_rawCrc;

	// Result of the load-time "patch bad maps" pass (ZH :533-545).
	struct PatchReport
	{
		// entries whose stored VALUE was changed (non-zero and out of range)
		int cliffIndicesPatched = 0;
		int blendIndicesPatched = 0;
		int extraBlendIndicesPatched = 0;
		// entries that failed the range test, including stored zeros of a table with zero entries
		int cliffFormallyOutOfRange = 0;
		int blendFormallyOutOfRange = 0;
		int extraBlendFormallyOutOfRange = 0;
	};
	PatchReport m_patchReport;

	// ---- chunk parsers (ZH static parser callbacks); userData = WorldHeightMap* ----
	static bool ParseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static bool ParseBlendTileDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);

	// ZH WorldHeightMap constructor tail: any cliff/blend/extra index outside [0,count) -> 0.
	void patchBadIndices();

	// ---- accessors ----
	int getXExtent() const { return m_width; }
	int getYExtent() const { return m_height; }
	int getBorderSize() const { return m_borderSize; }
	std::uint16_t getHeight(int x, int y) const { return m_data[(size_t)y * m_width + x]; }
	// ZH getCliffState(): bit (x&7) of byte [y*stride + (x>>3)]; false outside the map.
	bool getCliffState(int x, int y) const { return planeBit(m_cellCliffState, x, y); }
	bool planeBit(const std::vector<std::uint8_t> &plane, int x, int y) const;

	// Whole-array checksums used by the corpus oracle comparison (CRC-32, IEEE).
	std::uint32_t crcHeights() const;
	std::uint32_t crcTileNdxes() const;

	// ---- terrain atlas placement (filled by the atlas packer, see GameClient/TerrainAtlas.h) ----
	int m_terrainTexHeight = 1;                 // power-of-two height of the base atlas (ZH m_terrainTexHeight)
	std::vector<TileLocation> m_tileLocations;  // one per source tile (m_numBitmapTiles)

	// ---- tile / blend UVs (ZH WorldHeightMap.cpp:1613-2158; BFME1 retail bodies for
	//      getUVForTileIndex and getExtraAlphaUVData, see .cpp). Vertex order SW=0, SE=1, NE=2, NW=3.
	//      Valid only after the atlas was packed. xIndex/yIndex index the height grid; x == width-1 or
	//      y == height-1 cells are never drawn and must not be asked for.
	void getUVForNdx(int tileNdx, float *minU, float *minV, float *maxU, float *maxV, bool fullTile = false) const;
	// Returns the cliff entry's flip flag when a matching cliff info overrode the UVs, else false.
	// `wrap` (optional) receives the class block the cell's UVs must be wrapped into (invalid when none).
	bool getUVForTileIndex(int ndx, int tileNdx, float U[4], float V[4], const TerrainUvOptions &options, bool fullTile = false, ClassWrapRect *wrap = nullptr) const;
	// True when the cell's cliff info overrides the UVs of `tileNdx` (a cliff index is set and its tile lies
	// in the same texture class as `tileNdx`): the "tilesMatch" test of getUVForTileIndex.
	bool cliffUvApplies(int ndx, int tileNdx) const;
	// Base layer UVs of cell (x,y) (ZH getUVData).
	bool getUVData(int xIndex, int yIndex, float U[4], float V[4], const TerrainUvOptions &options, ClassWrapRect *wrap = nullptr) const;
	// First blend layer (ZH getAlphaUVData): UVs, per-vertex alpha, and whether the cell's triangles are
	// flipped (diagonal SE-NW) for the layer's blend direction.
	void getAlphaUVData(int xIndex, int yIndex, float U[4], float V[4], std::uint8_t alpha[4], bool *flip, const TerrainUvOptions &options, ClassWrapRect *wrap = nullptr) const;
	// Second ("3-way") blend layer (ZH getExtraAlphaUVData). Returns false when the cell has none.
	bool getExtraAlphaUVData(int xIndex, int yIndex, float U[4], float V[4], std::uint8_t alpha[4], bool *needFlip, bool *cliff, const TerrainUvOptions &options, ClassWrapRect *wrap = nullptr) const;
};
