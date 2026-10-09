// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/TerrainAtlas.h for sources and the named presentation choices.

#include "GameClient/TerrainAtlas.h"

#include "GameClient/TGAFile.h"

#include <algorithm>
#include <cstring>

namespace
{

struct LoadedClass
{
	bool loaded = false;      // pixels decoded and big enough
	int widthTiles = 0;       // numRows of readTiles
	TGAImage image;           // base texture, storage row order
	bool hasNormal = false;
	TGAImage normal;
};

// ZH readTexClass: numTiles = countTiles(...); if (numTiles >= class.numTiles) { ...largest width with
// numTiles >= width*width... readTiles(width) }
int derivedWidth(int classNumTiles)
{
	for (int width = 16; width >= 1; --width)
	{
		if (classNumTiles >= width * width)
		{
			return width;
		}
	}
	return 0;
}

bool readTga(ArchiveFileSystem &fs, const std::string &path, TGAImage &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile(path, bytes, error))
	{
		return false;
	}
	return TGAFile::decode(bytes.data(), bytes.size(), out, error, 1024);
}

// Copies one class block (width tiles square, storage rows) into the atlas, rows inverted per tile like
// TerrainTextureClass::update, for tiles that are present.
void blitClass(std::vector<std::uint8_t> &atlas, int atlasW, const WorldHeightMap &map, const TXTextureClass &c, int width, const TGAImage &src)
{
	for (int j = 0; j < width; ++j)
	{
		for (int i = 0; i < width; ++i)
		{
			int baseNdx = c.firstTile + i + j * width;
			if (baseNdx < 0 || (size_t)baseNdx >= map.m_tileLocations.size() || !map.m_tileLocations[(size_t)baseNdx].present)
			{
				continue;
			}
			const TileLocation &loc = map.m_tileLocations[(size_t)baseNdx];
			for (int py = 0; py < TILE_PIXEL_EXTENT; ++py)
			{
				const int srcRow = j * TILE_PIXEL_EXTENT + py; // storage row
				const std::uint8_t *s = &src.rgba[((size_t)srcRow * src.width + (size_t)i * TILE_PIXEL_EXTENT) * 4];
				std::uint8_t *d = &atlas[((size_t)(loc.y + (TILE_PIXEL_EXTENT - 1 - py)) * atlasW + (size_t)loc.x) * 4];
				std::memcpy(d, s, (size_t)TILE_PIXEL_EXTENT * 4);
			}
		}
	}
}

// ZH TerrainTextureClass::update, "draw the 4 pixel border around each tile class".
void drawGutters(std::vector<std::uint8_t> &atlas, int atlasW, int ox, int oy, int width)
{
	const size_t bpp = 4;
	auto px = [&](int x, int y) { return &atlas[((size_t)y * atlasW + (size_t)x) * bpp]; };
	for (int j = 0; j < width; ++j)
	{
		std::uint8_t *row = px(ox, oy + j);
		std::memcpy(row - 4 * bpp, row + (size_t)(width - 4) * bpp, 4 * bpp); // before: from the right edge
		std::memcpy(row + (size_t)width * bpp, row, 4 * bpp);                 // after: from the left edge
	}
	for (int j = 0; j < 4; ++j)
	{
		// rows before the class repeat the bottom rows; rows after repeat the top rows (width + 8 pixels wide)
		std::uint8_t *before = px(ox - 4, oy - j - 1);
		std::memcpy(before, before + (size_t)width * atlasW * bpp, (size_t)(width + 8) * bpp);
		std::uint8_t *after = px(ox - 4, oy + j);
		std::memcpy(after + (size_t)width * atlasW * bpp, after, (size_t)(width + 8) * bpp);
	}
}

void fillRect(std::vector<std::uint8_t> &atlas, int atlasW, int x0, int y0, int w, int h, const std::uint8_t texel[4])
{
	for (int y = y0; y < y0 + h; ++y)
	{
		for (int x = x0; x < x0 + w; ++x)
		{
			std::memcpy(&atlas[((size_t)y * atlasW + (size_t)x) * 4], texel, 4);
		}
	}
}

} // namespace

int TerrainAtlas::countTiles(int imageWidth, int imageHeight)
{
	int tileWidth = imageWidth / TILE_PIXEL_EXTENT;
	int tileHeight = imageHeight / TILE_PIXEL_EXTENT;
	if (tileWidth > 16 || tileHeight > 16)
	{
		return 0; // don't do huge images, or bad files.
	}
	for (int width = 16; width > 0; --width)
	{
		if (tileWidth >= width && tileHeight >= width)
		{
			return width * width;
		}
	}
	return 0;
}

// ZH WorldHeightMap::updateTileTexturePositions (WorldHeightMap.cpp:1469-1560), including its quirky
// search that only tests the first free cell of each row.
int TerrainAtlas::packTiles(WorldHeightMap &map, const std::vector<bool> &tilePresent)
{
	const int tilesPerRow = TERRAIN_TEXTURE_WIDTH / (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET); // 28 columns
	// ZH's grid is square (tilesPerRow x tilesPerRow = 784 tiles), which cannot hold a BFME2/RotWK map: the
	// median pure-2.01 map has 832 source tiles and the largest 1804 (spec 1.7.2). The rows therefore grow
	// downward, up to the 8192-px texture limit (113 rows). The retail BFME2 layout is UNKNOWN (stop S-030).
	const int maxRows = TERRAIN_ATLAS_MAX_HEIGHT / (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET);
	static bool availableGrid[TERRAIN_ATLAS_MAX_HEIGHT / (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET) + 1][64];
	for (int row = 0; row < maxRows; row++)
	{
		for (int column = 0; column < tilesPerRow; column++)
		{
			availableGrid[row][column] = true;
		}
	}
	int maxHeight = 0;
	map.m_tileLocations.assign((size_t)map.m_numBitmapTiles, TileLocation());

	for (int tileWidth = tilesPerRow; tileWidth > 0; tileWidth--)
	{
		for (size_t texClass = 0; texClass < map.m_textureClasses.size(); texClass++)
		{
			TXTextureClass &c = map.m_textureClasses[texClass];
			const int width = c.width;
			if (width != tileWidth)
			{
				continue;
			}
			// Find an available block of space.
			bool found = false;
			int row, column = 0;
			for (row = 0; row < maxRows - width + 1 && !found; row++)
			{
				for (column = 0; column < tilesPerRow - width + 1 && !found; column++)
				{
					if (availableGrid[row][column])
					{
						bool open = true;
						for (int i = 0; i < width && open; i++)
						{
							for (int j = 0; j < width && open; j++)
							{
								if (!availableGrid[row + j][column + i])
								{
									open = false;
								}
							}
						}
						if (open)
						{
							found = true;
						}
						break;
					}
				}
				if (found)
				{
					break;
				}
			}
			if (!found)
			{
				c.positionInTexture.x = 0;
				c.positionInTexture.y = 0;
				continue;
			}
			const int xOrigin = TERRAIN_TILE_OFFSET / 2 + column * (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET);
			const int yOrigin = TERRAIN_TILE_OFFSET / 2 + row * (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET);
			c.positionInTexture.x = xOrigin;
			c.positionInTexture.y = yOrigin;
			const int classHeight = yOrigin + width * TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET / 2;
			if (maxHeight < classHeight)
			{
				maxHeight = classHeight;
			}
			for (int i = 0; i < width; i++)
			{
				for (int j = 0; j < width; j++)
				{
					availableGrid[row + j][column + i] = false;
					const int baseNdx = c.firstTile + i + j * width;
					if (baseNdx < 0 || (size_t)baseNdx >= tilePresent.size() || !tilePresent[(size_t)baseNdx])
					{
						continue; // In case we are just checking for room...
					}
					TileLocation &loc = map.m_tileLocations[(size_t)baseNdx];
					loc.x = xOrigin + i * TILE_PIXEL_EXTENT;
					loc.y = yOrigin + (width - j - 1) * TILE_PIXEL_EXTENT;
					loc.present = true;
				}
			}
		}
	}
	return maxHeight;
}

bool TerrainAtlas::build(WorldHeightMap &map, ArchiveFileSystem &fs, const TerrainTypeIndex &types, TerrainAtlasImages &images, TerrainAtlasReport &report, std::string *error)
{
	report = TerrainAtlasReport();
	images = TerrainAtlasImages();
	if (!map.m_hasBlendTileData)
	{
		if (error)
		{
			*error = "map has no BlendTileData";
		}
		return false;
	}
	report.classes = (int)map.m_textureClasses.size();

	std::vector<LoadedClass> loaded(map.m_textureClasses.size());
	std::vector<bool> tilePresent((size_t)map.m_numBitmapTiles, false);
	for (size_t ci = 0; ci < map.m_textureClasses.size(); ++ci)
	{
		const TXTextureClass &c = map.m_textureClasses[ci];
		LoadedClass &lc = loaded[ci];
		const std::string *tex = types.findTexture(c.name);
		if (!tex)
		{
			report.unknownTerrainType.push_back(c.name);
			continue;
		}
		const std::string path = TerrainTypeIndex::textureArchivePath(*tex);
		if (!fs.doesFileExist(path))
		{
			report.missingTextureFile.push_back(c.name + " -> " + path);
			continue;
		}
		std::string e;
		if (!readTga(fs, path, lc.image, &e))
		{
			report.badTexture.push_back(c.name + ": " + e);
			continue;
		}
		if (lc.image.topDownFlag)
		{
			report.topDownTextures.push_back(*tex);
		}
		const int numTiles = countTiles(lc.image.width, lc.image.height);
		if (numTiles < c.numTiles)
		{
			report.undersizedTexture.push_back(c.name + " (" + std::to_string(lc.image.width) + "x" + std::to_string(lc.image.height)
				+ " holds " + std::to_string(numTiles) + " tiles, class needs " + std::to_string(c.numTiles) + ")");
			continue;
		}
		lc.widthTiles = derivedWidth(c.numTiles);
		if (lc.widthTiles != c.width)
		{
			++report.classWidthMismatches;
		}
		lc.loaded = true;
		for (int t = 0; t < lc.widthTiles * lc.widthTiles; ++t)
		{
			const int idx = c.firstTile + t;
			if (idx >= 0 && (size_t)idx < tilePresent.size())
			{
				tilePresent[(size_t)idx] = true;
			}
		}
		// the BFME2 normal map
		const std::string nrmPath = TerrainTypeIndex::textureArchivePath(TerrainTypeIndex::normalMapName(*tex));
		if (!fs.doesFileExist(nrmPath))
		{
			report.missingNormalMap.push_back(c.name);
		}
		else if (!readTga(fs, nrmPath, lc.normal, &e))
		{
			report.badNormalMap.push_back(c.name + ": " + e);
		}
		else if (lc.normal.width < lc.widthTiles * TILE_PIXEL_EXTENT || lc.normal.height < lc.widthTiles * TILE_PIXEL_EXTENT)
		{
			report.badNormalMap.push_back(c.name + ": " + std::to_string(lc.normal.width) + "x" + std::to_string(lc.normal.height)
				+ " is smaller than the base texture's tile block");
		}
		else
		{
			lc.hasNormal = true;
		}
	}

	const int usedHeight = packTiles(map, tilePresent);
	int pow2 = 1;
	while (pow2 < usedHeight)
	{
		pow2 *= 2;
	}
	map.m_terrainTexHeight = pow2;
	images.width = TERRAIN_TEXTURE_WIDTH;
	images.height = pow2;
	images.base.assign((size_t)images.width * (size_t)images.height * 4, 0);
	images.normal.assign((size_t)images.width * (size_t)images.height * 4, 0);
	report.atlasHeight = pow2;
	report.sourceTiles = map.m_numBitmapTiles;
	report.grownBeyondZhGrid = usedHeight > (TERRAIN_TEXTURE_WIDTH / (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET)) * (TILE_PIXEL_EXTENT + TERRAIN_TILE_OFFSET) + TERRAIN_TILE_OFFSET / 2;

	const std::uint8_t flat[4] = { FLAT_NORMAL_TEXEL_R, FLAT_NORMAL_TEXEL_G, FLAT_NORMAL_TEXEL_B, FLAT_NORMAL_TEXEL_A };
	for (size_t ci = 0; ci < map.m_textureClasses.size(); ++ci)
	{
		TXTextureClass &c = map.m_textureClasses[ci];
		LoadedClass &lc = loaded[ci];
		if (!lc.loaded)
		{
			continue;
		}
		if (c.positionInTexture.x <= 0)
		{
			report.unplacedClass.push_back(c.name);
			continue;
		}
		++report.classesPlaced;
		const int widthPx = lc.widthTiles * TILE_PIXEL_EXTENT;
		blitClass(images.base, images.width, map, c, lc.widthTiles, lc.image);
		drawGutters(images.base, images.width, c.positionInTexture.x, c.positionInTexture.y, widthPx);
		if (lc.hasNormal)
		{
			blitClass(images.normal, images.width, map, c, lc.widthTiles, lc.normal);
			drawGutters(images.normal, images.width, c.positionInTexture.x, c.positionInTexture.y, widthPx);
		}
		else
		{
			fillRect(images.normal, images.width, c.positionInTexture.x - 4, c.positionInTexture.y - 4, widthPx + 8, widthPx + 8, flat);
		}
	}
	for (size_t i = 0; i < map.m_tileLocations.size(); ++i)
	{
		report.tilesPlaced += map.m_tileLocations[i].present ? 1 : 0;
	}
	return true;
}
