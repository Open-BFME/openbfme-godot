// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The terrain texture atlas: every texture class of a map packed into one 2048-px-wide RGBA image, the
// faithful (ZH/BFME1) scheme the tile and blend UVs are computed against.
//
// Sources:
//  * target fact (BFME2 decompile): Open-BFME-2 .../WorldHeightMapReadTexClass.cpp (a terrain type has a
//    normal map named "X_nrm.tga"), WorldHeightMapTiles.cpp (countTiles: up to 16x16 tiles),
//    TileDataGetRGBDataForWidth.cpp (tiles are kept in 16 bit per pixel in BFME2).
//  * target fact (BFME1 decompile): Open-BFME-1 WorldHeightMap.cpp:1535-1590 (readTiles: type-8 RLE and
//    widths over 1024 are rejected).
//  * donor (ZH): WorldHeightMap.cpp countTiles/readTiles :1331-1465, updateTileTexturePositions
//    :1469-1610, readTexClass :1020-1062; TerrainTex.cpp TerrainTextureClass::update (copy rows inverted,
//    4-px wrap gutters around each class block).
//
// Named presentation choices (reported in TerrainAtlasReport, never silent):
//  * the atlas is RGBA8; retail quantises tiles to 16 bit (A1R5G5B5 in ZH, 16 bpp in BFME2).
//  * mip levels are built by the renderer (ZH: 3 levels, box filter); this class only produces level 0.
//  * UNKNOWN whether BFME2 still uses one 2048-wide atlas (spec 3.3, open question 2): the layout and the
//    UV maths assume it. Classes without a "_nrm" texture (534 of 990 in the corpus) get FLAT_NORMAL_TEXEL (S-034).

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "GameClient/TerrainTypes.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <cstdint>
#include <string>
#include <vector>

// Named assumption (UNKNOWN in retail, stop S-034): the texel of a class that has no "_nrm" texture. The
// shader reads N.xy = 2*texel.rg-1 and the specular gloss from the blue channel (spec 3.4). Every retail
// "_nrm" texture inspected is (128,128,255) over flat ground (blue ~ 1.0), so a missing map is filled with
// the same flat texel: such a class looks like its neighbours on flat ground instead of forming a seam.
#define FLAT_NORMAL_TEXEL_R 128
#define FLAT_NORMAL_TEXEL_G 128
#define FLAT_NORMAL_TEXEL_B 255
#define FLAT_NORMAL_TEXEL_A 255

struct TerrainAtlasReport
{
	int classes = 0;
	int classesPlaced = 0;
	int tilesPlaced = 0;
	int atlasWidth = TERRAIN_TEXTURE_WIDTH;
	int atlasHeight = 0;
	int classWidthMismatches = 0; // class width field != the square the loaded tile count implies
	bool grownBeyondZhGrid = false; // the map needs more than ZH's 28x28 = 784 tile grid, so the atlas grew downward (S-030)
	int sourceTiles = 0;            // map.m_numBitmapTiles
	std::vector<std::string> unknownTerrainType;   // class names not in terrain.ini (class unplaced, UV (0,0))
	std::vector<std::string> missingTextureFile;   // terrain.ini names a texture that is not in the archives
	std::vector<std::string> badTexture;           // "<class>: <decode error>"
	std::vector<std::string> undersizedTexture;    // image holds fewer tiles than the class needs
	std::vector<std::string> unplacedClass;        // no room in the atlas
	std::vector<std::string> missingNormalMap;     // class uses FLAT_NORMAL_TEXEL
	std::vector<std::string> badNormalMap;         // "<class>: <reason>" (wrong size / undecodable): FLAT_NORMAL_TEXEL
	bool tgaOriginFlagIgnored = true;              // retail reads rows in storage order
	std::vector<std::string> topDownTextures;      // textures whose descriptor says top-down (flipped, as in retail)
};

struct TerrainAtlasImages
{
	int width = TERRAIN_TEXTURE_WIDTH;
	int height = 0;
	std::vector<std::uint8_t> base;   // RGBA8, width*height*4
	std::vector<std::uint8_t> normal; // RGBA8, same layout
};

namespace TerrainAtlas
{
// Loads the class textures from the archives, packs them (ZH updateTileTexturePositions), fills
// map.m_textureClasses[].positionInTexture, map.m_tileLocations and map.m_terrainTexHeight, and renders
// both atlas images. Never throws; every problem lands in the report.
bool build(WorldHeightMap &map, ArchiveFileSystem &fs, const TerrainTypeIndex &types, TerrainAtlasImages &images, TerrainAtlasReport &report, std::string *error);

// The packing step alone (no files): tilesPresent[i] says whether source tile i has pixels.
// Returns the used height (ZH return value of updateTileTexturePositions).
int packTiles(WorldHeightMap &map, const std::vector<bool> &tilePresent);

// ZH countTiles for an image of this size: the largest w (<= 16) with w*64 <= width and height, as w*w; else 0.
int countTiles(int imageWidth, int imageHeight);
} // namespace TerrainAtlas
