// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Tile, blend and cliff UVs of a terrain cell. Sources, in priority order:
//  * TARGET (BFME1 retail decompile, byte-matched bodies):
//      Open-BFME-1 .../W3DDevice/GameClient/WorldHeightMapGetUVForTileIndex.cpp  (getUVForTileIndex)
//      Open-BFME-1 .../WorldHeightMapGetExtraAlphaUVData.cpp                       (getExtraAlphaUVData)
//    The BFME1 getUVForTileIndex has NO "old UV adjustment" stretch heuristic (ZH WorldHeightMap.cpp:
//    1778-1975): after the cliff-info branch it returns false. The heuristic is therefore not ported.
//  * DONOR (ZH WorldHeightMap.cpp): getUVForNdx :1613-1660, getUVData :1662-1700, getAlphaUVData
//    :2066-2158 (BFME1's getAlphaUVData body is ZH text pasted in, not retail evidence; the extra-layer
//    twin in BFME1 retail is identical to ZH's, which is the inference used here).
//  * Open-BFME-2's WorldHeightMap.cpp is ZH text pasted in ("present-unmatched"): not evidence.
//
// Everything here is presentation maths in float32 (ZH Real); it never feeds the simulation.

#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
const std::uint8_t INVERTED_MASK = 0x1; // ZH TileData.h:51
const std::uint8_t FLIPPED_MASK = 0x2;  // ZH TileData.h:52
} // namespace

// ZH getUVForNdx (:1613-1660)
void WorldHeightMap::getUVForNdx(int tileNdx, float *minU, float *minV, float *maxU, float *maxV, bool fullTile) const
{
	int baseNdx = tileNdx >> 2;
	if (baseNdx < 0 || (size_t)baseNdx >= m_tileLocations.size() || !m_tileLocations[(size_t)baseNdx].present)
	{
		// Missing texture.
		*minU = *minV = *maxU = *maxV = 0.0f;
		return;
	}
	const TileLocation &pos = m_tileLocations[(size_t)baseNdx];
	*minU = (float)pos.x;
	*minV = (float)pos.y;
	*maxU = *minU + TILE_PIXEL_EXTENT;
	*maxV = *minV + TILE_PIXEL_EXTENT;
	*minU /= TERRAIN_TEXTURE_WIDTH;
	*minV /= (float)m_terrainTexHeight;
	*maxU /= TERRAIN_TEXTURE_WIDTH;
	*maxV /= (float)m_terrainTexHeight;
	if (!fullTile)
	{
		// Tiles are 64x64 pixels, height grids map to 32x32. So get the proper quadrant of the tile.
		float midX = (*minU + *maxU) / 2;
		float midY = (*minV + *maxV) / 2;
		if (tileNdx & 2)
		{ // y's are flipped.
			*maxV = midY;
		}
		else
		{
			*minV = midY;
		}
		if (tileNdx & 1)
		{
			*minU = midX;
		}
		else
		{
			*maxU = midX;
		}
	}
}

// BFME1 retail getUVForTileIndex (0x0074BEB0), with the cliff UV unit made explicit.
bool WorldHeightMap::getUVForTileIndex(int ndx, int tileNdx, float U[4], float V[4], const TerrainUvOptions &options, bool fullTile, ClassWrapRect *wrap) const
{
	if (wrap)
	{
		*wrap = ClassWrapRect();
	}
	float nU = 0, nV = 0, xU = 0, xV = 0;
	if (ndx < m_dataSize && !m_tileNdxes.empty())
	{
		getUVForNdx(tileNdx, &nU, &nV, &xU, &xV, fullTile);
		U[0] = nU; U[1] = xU; U[2] = xU; U[3] = nU;
		V[0] = xV; V[1] = xV; V[2] = nV; V[3] = nV;
		if (!options.adjustCliffTextures)
		{
			return false;
		}
		if (nU == 0.0)
		{
			return false; // missing texture.
		}
		if (fullTile)
		{
			return false;
		}
		if (ndx >= 0 && m_cliffInfoNdxes[(size_t)ndx])
		{
			const TCliffInfo &info = m_cliffInfo[(size_t)m_cliffInfoNdxes[(size_t)ndx]];
			bool tilesMatch = false;
			int ndx1 = tileNdx >> 2;
			int ndx2 = info.tileIndex >> 2;
			size_t i;
			for (i = 0; i < m_textureClasses.size(); i++)
			{
				const TXTextureClass &c = m_textureClasses[i];
				if (ndx1 >= c.firstTile && ndx1 < c.firstTile + c.numTiles)
				{
					tilesMatch = ndx2 >= c.firstTile && ndx2 < c.firstTile + c.numTiles;
					break;
				}
			}
			if (tilesMatch)
			{
				const TXTextureClass &c = m_textureClasses[i];
				const float infoU[4] = { info.u0, info.u1, info.u2, info.u3 };
				const float infoV[4] = { info.v0, info.v1, info.v2, info.v3 };
				if (options.cliffUnit == CLIFF_UV_ATLAS_2048)
				{
					float minU = (float)c.positionInTexture.x;
					float maxV = (float)(c.positionInTexture.y + c.width * TILE_PIXEL_EXTENT);
					minU *= 1.0f / TERRAIN_TEXTURE_WIDTH; // retail: minU *= Gen01121AE4
					maxV /= (float)m_terrainTexHeight;
					// retail: Real vFactor = TEXTURE_WIDTH/m_terrainTexHeight; both Int, so INTEGER division
					float vFactor = (float)(TERRAIN_TEXTURE_WIDTH / m_terrainTexHeight);
					for (int k = 0; k < 4; ++k)
					{
						U[k] = infoU[k] + minU;
						V[k] = infoV[k] * vFactor + maxV;
					}
				}
				else
				{
					// Hypothesis (see CliffUvUnit): units of 256 px from the class block's left edge and
					// bottom edge (v grows downward in the texture, so in-class v is negative), wrapped
					// inside the class block. The four corners are positioned in the UNWRAPPED frame (shifted by
					// a whole number of class spans so they stay near the block, which changes no wrapped
					// sample); a cell that straddles the class edge keeps corners beyond it, and the sampler wraps
					// each pixel into `wrap` (reviewer finding MAP-1 P1: shifting the four corners together left
					// 1458 of 1460 Minas Morgul cliff cells reading other classes through the atlas).
					const float classPx = (float)(c.width * TILE_PIXEL_EXTENT);
					const float top = (float)c.positionInTexture.y;
					float uPx[4], vTop[4]; // v measured from the class block's top edge
					float minU = 1e30f, minV = 1e30f;
					for (int k = 0; k < 4; ++k)
					{
						uPx[k] = infoU[k] * 256.0f;
						vTop[k] = classPx + infoV[k] * 256.0f;
						minU = std::min(minU, uPx[k]);
						minV = std::min(minV, vTop[k]);
					}
					const float offU = std::floor(minU / classPx) * classPx;
					const float offV = std::floor(minV / classPx) * classPx;
					for (int k = 0; k < 4; ++k)
					{
						U[k] = ((float)c.positionInTexture.x + uPx[k] - offU) / TERRAIN_TEXTURE_WIDTH;
						V[k] = (top + vTop[k] - offV) / (float)m_terrainTexHeight;
					}
					if (wrap)
					{
						wrap->x0 = (float)c.positionInTexture.x / TERRAIN_TEXTURE_WIDTH;
						wrap->y0 = top / (float)m_terrainTexHeight;
						wrap->w = classPx / TERRAIN_TEXTURE_WIDTH;
						wrap->h = classPx / (float)m_terrainTexHeight;
					}
				}
				return info.flip;
			}
		}
	}
	return false;
}

bool WorldHeightMap::cliffUvApplies(int ndx, int tileNdx) const
{
	if (ndx < 0 || ndx >= m_dataSize || m_cliffInfoNdxes.empty() || !m_cliffInfoNdxes[(size_t)ndx])
	{
		return false;
	}
	const TCliffInfo &info = m_cliffInfo[(size_t)m_cliffInfoNdxes[(size_t)ndx]];
	int ndx1 = tileNdx >> 2;
	int ndx2 = info.tileIndex >> 2;
	for (const TXTextureClass &c : m_textureClasses)
	{
		if (ndx1 >= c.firstTile && ndx1 < c.firstTile + c.numTiles)
		{
			return ndx2 >= c.firstTile && ndx2 < c.firstTile + c.numTiles;
		}
	}
	return false;
}

// ZH getUVData (:1662-1700)
bool WorldHeightMap::getUVData(int xIndex, int yIndex, float U[4], float V[4], const TerrainUvOptions &options, ClassWrapRect *wrap) const
{
	int ndx = (yIndex * m_width) + xIndex;
	if (wrap)
	{
		*wrap = ClassWrapRect();
	}
	if (ndx < m_dataSize && !m_tileNdxes.empty())
	{
		return getUVForTileIndex(ndx, m_tileNdxes[(size_t)ndx], U, V, options, false, wrap);
	}
	return false;
}

// Reference of the per-pixel wrap the terrain shader performs (godot/shaders/terrain_common.gdshaderinc,
// wrap_into_class). GLSL mod(): x - y * floor(x / y).
void TerrainUv::wrapIntoClass(const ClassWrapRect &rect, float u, float v, float &outU, float &outV)
{
	if (!rect.valid())
	{
		outU = u;
		outV = v;
		return;
	}
	const float du = u - rect.x0;
	const float dv = v - rect.y0;
	outU = rect.x0 + (du - rect.w * std::floor(du / rect.w));
	outV = rect.y0 + (dv - rect.h * std::floor(dv / rect.h));
}

// ZH getAlphaUVData (:2066-2158)
void WorldHeightMap::getAlphaUVData(int xIndex, int yIndex, float U[4], float V[4], std::uint8_t alpha[4], bool *flip, const TerrainUvOptions &options, ClassWrapRect *wrap) const
{
	if (wrap)
	{
		*wrap = ClassWrapRect();
	}
	int ndx = (yIndex * m_width) + xIndex;
	bool stretchedForCliff = false;
	bool needFlip = false;

	if (ndx < m_dataSize && !m_tileNdxes.empty())
	{
		int blendNdx = m_blendTileNdxes[(size_t)ndx];
		if (blendNdx == 0)
		{
			stretchedForCliff = getUVForTileIndex(ndx, m_tileNdxes[(size_t)ndx], U, V, options, false, wrap);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			// No alpha blend, so never need to flip.
			needFlip = false;
		}
		else
		{
			const TBlendTileInfo &b = m_blendedTiles[(size_t)blendNdx];
			stretchedForCliff = getUVForTileIndex(ndx, b.blendNdx, U, V, options, false, wrap);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			if (b.horiz)
			{
				// Horizontals don't need flipping unless forced because of 3way blend.
				needFlip = (b.inverted & FLIPPED_MASK) != 0;
				if (b.inverted & INVERTED_MASK)
				{
					alpha[0] = alpha[3] = 255;
				}
				else
				{
					alpha[1] = alpha[2] = 255;
				}
			}
			if (b.vert)
			{
				needFlip = (b.inverted & FLIPPED_MASK) != 0;
				if (b.inverted & INVERTED_MASK)
				{
					alpha[0] = alpha[1] = 255;
				}
				else
				{
					alpha[2] = alpha[3] = 255;
				}
			}
			if (b.rightDiagonal)
			{
				if (b.inverted & INVERTED_MASK)
				{
					alpha[1] = 255;
					if (b.longDiagonal)
					{
						alpha[0] = 255;
						alpha[2] = 255;
					}
				}
				else
				{
					// Uninverted right diagonals need flipping.
					needFlip = true;
					alpha[2] = 255;
					if (b.longDiagonal)
					{
						alpha[1] = 255;
						alpha[3] = 255;
					}
				}
			}
			if (b.leftDiagonal)
			{
				if (b.inverted & INVERTED_MASK)
				{
					// Inverted left diagonals need flipping.
					needFlip = true;
					alpha[0] = 255;
					if (b.longDiagonal)
					{
						alpha[1] = 255;
						alpha[3] = 255;
					}
				}
				else
				{
					alpha[3] = 255;
					if (b.longDiagonal)
					{
						alpha[0] = 255;
						alpha[2] = 255;
					}
				}
			}
			if (b.customBlendEdgeClass >= 0)
			{
				alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
				// No alpha blend, so never need to flip.
				needFlip = false;
			}
		}
	}
	if (stretchedForCliff)
	{
		// If we had to stretch for cliff, check heights.
		int p0 = getHeight(xIndex, yIndex);
		int p1 = getHeight(xIndex + 1, yIndex);
		int p2 = getHeight(xIndex + 1, yIndex + 1);
		int p3 = getHeight(xIndex, yIndex + 1);
		int dz1 = std::abs(p0 - p2);
		int dz2 = std::abs(p1 - p3);
		needFlip = dz1 > dz2;
	}
	*flip = needFlip;
}

// BFME1 retail getExtraAlphaUVData (0x0074C140)
bool WorldHeightMap::getExtraAlphaUVData(int xIndex, int yIndex, float U[4], float V[4], std::uint8_t alpha[4], bool *needFlip, bool *cliff, const TerrainUvOptions &options, ClassWrapRect *wrap) const
{
	int ndx = (yIndex * m_width) + xIndex;
	*needFlip = false;
	*cliff = false;
	if (wrap)
	{
		*wrap = ClassWrapRect();
	}

	if (ndx >= 0 && ndx < m_dataSize && !m_tileNdxes.empty())
	{
		int blendNdx = m_extraBlendTileNdxes[(size_t)ndx];
		if (blendNdx == 0)
		{
			return false;
		}
		const TBlendTileInfo &b = m_blendedTiles[(size_t)blendNdx];
		*cliff = getUVForTileIndex(ndx, b.blendNdx, U, V, options, false, wrap);
		alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
		if (b.horiz)
		{
			// Horizontals don't need flipping unless forced because of 3way blend and a diagonal in base blend layer.
			*needFlip = (b.inverted & FLIPPED_MASK) != 0;
			if (b.inverted & INVERTED_MASK)
			{
				alpha[0] = alpha[3] = 255;
			}
			else
			{
				alpha[1] = alpha[2] = 255;
			}
		}
		if (b.vert)
		{
			*needFlip = (b.inverted & FLIPPED_MASK) != 0;
			if (b.inverted & INVERTED_MASK)
			{
				alpha[0] = alpha[1] = 255;
			}
			else
			{
				alpha[2] = alpha[3] = 255;
			}
		}
		if (b.rightDiagonal)
		{
			if (b.inverted & INVERTED_MASK)
			{
				alpha[1] = 255;
				if (b.longDiagonal)
				{
					alpha[0] = 255;
					alpha[2] = 255;
				}
			}
			else
			{
				*needFlip = true;
				alpha[2] = 255;
				if (b.longDiagonal)
				{
					alpha[1] = 255;
					alpha[3] = 255;
				}
			}
		}
		if (b.leftDiagonal)
		{
			if (b.inverted & INVERTED_MASK)
			{
				*needFlip = true;
				alpha[0] = 255;
				if (b.longDiagonal)
				{
					alpha[1] = 255;
					alpha[3] = 255;
				}
			}
			else
			{
				alpha[3] = 255;
				if (b.longDiagonal)
				{
					alpha[0] = 255;
					alpha[2] = 255;
				}
			}
		}
		if (b.customBlendEdgeClass >= 0)
		{
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			*needFlip = false;
		}
	}
	return true;
}
