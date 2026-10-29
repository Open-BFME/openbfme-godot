// OpenBFME. GPL-3.0. Lane TERRAIN-BAKES: see TerrainBake.h.
#include "GameEngineDevice/W3DDevice/GameClient/TerrainBake.h"

#include "Common/MapObject.h"

#include <algorithm>

namespace TerrainBake
{
float nearestPointDistanceSquared(const Box &box, const float camera[3])
{
	float best = -1.0f;
	for (int x = -1; x < 2; ++x)
	{
		for (int y = -1; y < 2; ++y)
		{
			for (int z = -1; z < 2; ++z)
			{
				const float px = box.center[0] + box.extent[0] * (float)x;
				const float py = box.center[1] + box.extent[1] * (float)y;
				const float pz = box.center[2] + box.extent[2] * (float)z;
				const float dx = camera[0] - px, dy = camera[1] - py, dz = camera[2] - pz;
				const float d = dx * dx + dy * dy + dz * dz;
				if (d < best || best < 0.0f)
				{
					best = d;
				}
			}
		}
	}
	return best;
}

float mip2xDistance(bool hasDisplay, unsigned displayWidth)
{
	float distance = 260.0f; // RW 0xBE5AF0
	float slop = 170.0f;     // RW 0xBE5AEC
	if (hasDisplay)
	{
		float scale = (float)displayWidth * 0.0009765625f; // RW 0xBE5798 = 1 / 1024
		if (0.5f > scale)
		{
			scale = 0.5f; // RW 0xBD869C
		}
		else if (scale > 2.5f)
		{
			scale = 2.5f; // RW 0xBE5AE8
		}
		distance = scale * 260.0f;
		slop = scale * 170.0f;
	}
	return distance + (slop + 25.0f); // RW 0xC69574
}

int textureMultiplier(const Box &box, const float camera[3], bool hasDisplay, unsigned displayWidth, bool allowTexture2X, bool stretchTerrain)
{
	const float distance = mip2xDistance(hasDisplay, displayWidth);
	const float minDistSqr = nearestPointDistanceSquared(box, camera);
	return (allowTexture2X && (minDistSqr < distance * distance || stretchTerrain)) ? 2 : 1;
}

TileGrid buildTileGrid(const WorldHeightMap &hm)
{
	TileGrid g;
	const int limitX = hm.getXExtent() - 1, limitY = hm.getYExtent() - 1;
	g.tilesX = (limitX + TILE_CELLS - 1) / TILE_CELLS;
	g.tilesY = (limitY + TILE_CELLS - 1) / TILE_CELLS;
	g.originX = -(float)hm.getBorderSize() * MAP_XY_FACTOR;
	g.originY = g.originX;
	g.boxes.resize((size_t)g.tilesX * (size_t)g.tilesY);
	for (int ty = 0; ty < g.tilesY; ++ty)
	{
		for (int tx = 0; tx < g.tilesX; ++tx)
		{
			const int minX = tx * TILE_CELLS, minY = ty * TILE_CELLS;
			const int maxX = std::min(minX + (int)TILE_CELLS, limitX), maxY = std::min(minY + (int)TILE_CELLS, limitY);
			unsigned lo = 0xFFFFu, hi = 0;
			for (int j = minY; j <= maxY; ++j)
			{
				for (int i = minX; i <= maxX; ++i)
				{
					lo = std::min<unsigned>(lo, hm.getHeight(i, j));
					hi = std::max<unsigned>(hi, hm.getHeight(i, j));
				}
			}
			const float x0 = g.originX + (float)minX * MAP_XY_FACTOR, x1 = g.originX + (float)maxX * MAP_XY_FACTOR;
			const float y0 = g.originY + (float)minY * MAP_XY_FACTOR, y1 = g.originY + (float)maxY * MAP_XY_FACTOR;
			const float z0 = (float)lo * MAP_HEIGHT_SCALE, z1 = (float)hi * MAP_HEIGHT_SCALE;
			Box &b = g.boxes[(size_t)ty * (size_t)g.tilesX + (size_t)tx];
			b.center[0] = (x1 + x0) * 0.5f;
			b.center[1] = (y1 + y0) * 0.5f;
			b.center[2] = (z1 + z0) * 0.5f;
			b.extent[0] = (x1 - x0) * 0.5f;
			b.extent[1] = (y1 - y0) * 0.5f;
			b.extent[2] = (z1 - z0) * 0.5f;
		}
	}
	return g;
}

std::vector<std::uint8_t> farTiles(const TileGrid &grid, const float camera[3], bool hasDisplay, unsigned displayWidth, bool allowTexture2X, bool stretchTerrain)
{
	std::vector<std::uint8_t> out(grid.boxes.size());
	for (size_t i = 0; i < grid.boxes.size(); ++i)
	{
		out[i] = textureMultiplier(grid.boxes[i], camera, hasDisplay, displayWidth, allowTexture2X, stretchTerrain) == 2 ? 0 : 1;
	}
	return out;
}
} // namespace TerrainBake
