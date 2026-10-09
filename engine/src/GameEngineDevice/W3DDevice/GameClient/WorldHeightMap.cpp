// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See WorldHeightMap.h for sources.

#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include "Common/Crc32.h"
#include "Common/DataChunk.h"

#include <cmath>

namespace
{

std::int32_t readCount(DataChunkInput &file, const char *what, std::int32_t limit)
{
	std::int32_t n = file.readInt();
	if (n < 0 || n > limit)
	{
		throw MapParseError(std::string(what) + " " + std::to_string(n) + " out of range");
	}
	return n;
}

// Every count read from a map sizes an allocation. The bytes that count implies must be in the chunk
// BEFORE anything is allocated (a tiny payload declaring INT_MAX records would otherwise try to reserve
// tens of gigabytes before the first read rejected it). Reviewer finding MAP-1 P1.
void requireBytes(DataChunkInput &file, unsigned long long bytes, const char *what)
{
	const std::int32_t left = file.getChunkDataSizeLeft();
	if (left < 0 || bytes > (unsigned long long)left)
	{
		throw MapParseError(std::string(what) + " needs " + std::to_string(bytes) + " bytes but the chunk has " + std::to_string(left)
			+ " left (count larger than the payload)");
	}
}

void readBytes(DataChunkInput &file, std::vector<std::uint8_t> &v, size_t n)
{
	requireBytes(file, n, "byte plane");
	v.resize(n);
	if (n)
	{
		file.readArrayOfBytes(v.data(), (std::int32_t)n);
	}
}

// Index array: 16-bit before v14, 32-bit from v14 (BFME `Int *`; spec 1.7.2), always widened to i32.
// Returns the CRC-32 of the array exactly as stored in the file (before widening and before the
// patch-bad-indices pass), for the corpus comparison against the independent oracle.
std::uint32_t readIndexArray(DataChunkInput &file, std::vector<std::int32_t> &out, size_t n, bool wide)
{
	requireBytes(file, (unsigned long long)n * (wide ? 4ull : 2ull), "index array");
	out.resize(n);
	if (wide)
	{
		if (n)
		{
			file.readArrayOfBytes(out.data(), (std::int32_t)(n * 4));
		}
		return crc32Bytes(out.data(), n * 4);
	}
	std::vector<std::int16_t> raw(n);
	if (n)
	{
		file.readArrayOfBytes(raw.data(), (std::int32_t)(n * 2));
	}
	for (size_t i = 0; i < n; ++i)
	{
		out[i] = raw[i];
	}
	return crc32Bytes(raw.data(), n * 2);
}

} // namespace

// ZH WorldHeightMap::ParseHeightMapData (WorldHeightMap.cpp:890-944), BFME2 heights (u16, v5).
bool WorldHeightMap::ParseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *m = (WorldHeightMap *)userData;
	if (info->version < K_HEIGHT_MAP_VERSION_3 || info->version > K_HEIGHT_MAP_VERSION_5)
	{
		// v1 (decimated by 2) and v2 are not in the corpus (every map is v5); not ported.
		throw MapParseError("HeightMapData version " + std::to_string(info->version) + " not supported (corpus: v5)");
	}
	m->m_heightMapVersion = info->version;
	m->m_width = file.readInt();
	m->m_height = file.readInt();
	m->m_borderSize = file.readInt(); // v>=3
	if (m->m_width <= 0 || m->m_height <= 0 || m->m_borderSize < 0 || m->m_width > 16384 || m->m_height > 16384)
	{
		throw MapParseError("HeightMapData size/border out of range");
	}
	if (info->version >= K_HEIGHT_MAP_VERSION_4)
	{
		std::int32_t nb = readCount(file, "boundary count", 4096);
		m->m_boundaries.resize((size_t)nb);
		for (std::int32_t i = 0; i < nb; ++i)
		{
			m->m_boundaries[(size_t)i].x = file.readInt();
			m->m_boundaries[(size_t)i].y = file.readInt();
		}
	}
	else
	{
		m->m_boundaries.resize(1);
		m->m_boundaries[0].x = m->m_width - 2 * m->m_borderSize;
		m->m_boundaries[0].y = m->m_height - 2 * m->m_borderSize;
	}
	m->m_dataSize = file.readInt();
	if (m->m_dataSize <= 0 || m->m_dataSize != m->m_width * m->m_height)
	{
		throw MapParseError("HeightMapData dataSize " + std::to_string(m->m_dataSize) + " != width*height (ERROR_CORRUPT_FILE_FORMAT)");
	}
	requireBytes(file, (unsigned long long)m->m_dataSize * (info->version >= K_HEIGHT_MAP_VERSION_5 ? 2ull : 1ull), "height array");
	m->m_data.resize((size_t)m->m_dataSize);
	if (info->version >= K_HEIGHT_MAP_VERSION_5)
	{
		file.readArrayOfBytes(m->m_data.data(), m->m_dataSize * 2);
	}
	else
	{
		// v4: 8-bit heights; BFME promotes each to u16 as (u16)(b*16.0f+0.5f) (spec 1.7.1).
		std::vector<std::uint8_t> raw((size_t)m->m_dataSize);
		file.readArrayOfBytes(raw.data(), m->m_dataSize);
		for (size_t i = 0; i < raw.size(); ++i)
		{
			m->m_data[i] = (std::uint16_t)(raw[i] * 16.0f + 0.5f);
		}
	}
	return true;
}

// ZH WorldHeightMap::ParseBlendTileData (WorldHeightMap.cpp:1071-1230) with the BFME layout.
bool WorldHeightMap::ParseBlendTileDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *m = (WorldHeightMap *)userData;
	const int v = info->version;
	if (m->m_data.empty())
	{
		throw MapParseError("BlendTileData before HeightMapData");
	}
	if (v < K_BLEND_TILE_VERSION_7 || v >= 24)
	{
		// v1-v6 need initCliffFlagsFromHeights()/decimation (not in the corpus); v>=24 changes the
		// index width and v>=25 drops planes (layout unverified).
		throw MapParseError("BlendTileData version " + std::to_string(v) + " not supported (corpus: 8, 11, 14, 15, 16, 18)");
	}
	m->m_blendTileVersion = v;
	m->m_hasBlendTileData = true;
	const int w = m->m_width, h = m->m_height;
	const size_t n = (size_t)w * (size_t)h;
	std::int32_t len = file.readInt();
	if ((size_t)len != n || len != m->m_dataSize)
	{
		throw MapParseError("BlendTileData dataSize != HeightMapData dataSize (ERROR_CORRUPT_FILE_FORMAT)");
	}
	const bool wide = v >= 14;
	m->m_planeStride = (w + 7) / 8;
	const size_t planeBytes = (size_t)m->m_planeStride * (size_t)h;

	requireBytes(file, (unsigned long long)n * 2ull, "tile index array");
	m->m_tileNdxes.resize(n);
	file.readArrayOfBytes(m->m_tileNdxes.data(), (std::int32_t)(n * 2));
	m->m_rawCrc.tile = crc32Bytes(m->m_tileNdxes.data(), n * 2);
	m->m_rawCrc.blend = readIndexArray(file, m->m_blendTileNdxes, n, wide);
	m->m_rawCrc.extraBlend = readIndexArray(file, m->m_extraBlendTileNdxes, n, wide); // v>=6
	m->m_rawCrc.cliffInfo = readIndexArray(file, m->m_cliffInfoNdxes, n, wide);      // v>=5

	// v>=7: cliff state plane. v7 saved rows with the previous, incorrect stride (width+1)/8 and
	// ZH copies it row by row into the (width+7)/8 stride array (WorldHeightMap.cpp:1156-1169).
	m->m_cellCliffState.assign(planeBytes, 0);
	if (v == K_BLEND_TILE_VERSION_7)
	{
		int byteWidth = (w + 1) / 8;
		requireBytes(file, (unsigned long long)byteWidth * (unsigned long long)h, "v7 cliff plane");
		std::vector<std::uint8_t> data((size_t)byteWidth * (size_t)h);
		if (!data.empty())
		{
			file.readArrayOfBytes(data.data(), (std::int32_t)data.size());
		}
		for (int j = 0; j < h; ++j)
		{
			for (int i = 0; i < byteWidth; ++i)
			{
				m->m_cellCliffState[(size_t)j * m->m_planeStride + i] = data[(size_t)j * byteWidth + i];
			}
		}
	}
	else
	{
		readBytes(file, m->m_cellCliffState, planeBytes);
	}
	// cliff plane CRC as stored (v7 stores the shorter rows, see above)
	m->m_rawCrc.cliffState = crc32Bytes(m->m_cellCliffState.data(), m->m_cellCliffState.size());
	if (v >= 10) readBytes(file, m->m_planeA, planeBytes);
	if (v >= 11) readBytes(file, m->m_planeB, planeBytes);
	if (v >= 14 && v < 25) readBytes(file, m->m_planeTaint, planeBytes);
	if (v >= 15) readBytes(file, m->m_planeExtraPass, planeBytes);
	if (v >= 16 && v < 25) readBytes(file, m->m_flammability, n);
	if (v >= 17) readBytes(file, m->m_planeVisibility, planeBytes);

	m->m_numBitmapTiles = file.readInt();
	m->m_numBlendedTiles = file.readInt();
	m->m_numCliffInfo = file.readInt(); // v>=5
	if (m->m_numBitmapTiles < 0 || m->m_numBlendedTiles < 1 || m->m_numCliffInfo < 0)
	{
		throw MapParseError("BlendTileData tile counts out of range");
	}
	std::int32_t ntc = readCount(file, "texture class count", 4096);
	m->m_textureClasses.clear();
	for (std::int32_t i = 0; i < ntc; ++i)
	{
		TXTextureClass c;
		c.firstTile = file.readInt();
		c.numTiles = file.readInt();
		c.width = file.readInt();
		c.legacy = file.readInt(); // ZH: "legacy GDF data"
		c.name = file.readAsciiString();
		m->m_textureClasses.push_back(std::move(c));
	}
	m->m_edgeTextureClasses.clear();
	m->m_numEdgeTiles = file.readInt(); // v>=4
	std::int32_t nec = readCount(file, "edge texture class count", 4096);
	for (std::int32_t i = 0; i < nec; ++i)
	{
		TXTextureClass c;
		c.firstTile = file.readInt();
		c.numTiles = file.readInt();
		c.width = file.readInt();
		c.name = file.readAsciiString();
		m->m_edgeTextureClasses.push_back(std::move(c));
	}
	// per record (index >= 1): i32 blendNdx, 6 flag bytes, i32 customBlendEdgeClass, u32 sentinel = 18 bytes
	requireBytes(file, (unsigned long long)(m->m_numBlendedTiles - 1) * 18ull, "blended tile records");
	m->m_blendedTiles.assign((size_t)m->m_numBlendedTiles, TBlendTileInfo());
	for (int i = 1; i < m->m_numBlendedTiles; ++i)
	{
		TBlendTileInfo &b = m->m_blendedTiles[(size_t)i];
		b.blendNdx = file.readInt();
		b.horiz = file.readByte();
		b.vert = file.readByte();
		b.rightDiagonal = file.readByte();
		b.leftDiagonal = file.readByte();
		b.inverted = file.readByte();
		b.longDiagonal = file.readByte(); // v>=3
		b.customBlendEdgeClass = file.readInt(); // v>=4
		std::uint32_t flag = file.readUnsignedInt();
		if (flag != BLEND_TILE_FLAG_VAL)
		{
			throw MapParseError("blended tile " + std::to_string(i) + " lacks the 0x7ADA0000 sentinel (Invalid format)");
		}
	}
	// per record (index >= 1): i32 tileIndex, 8 reals, 2 flag bytes = 38 bytes
	requireBytes(file, (unsigned long long)(m->m_numCliffInfo > 0 ? m->m_numCliffInfo - 1 : 0) * 38ull, "cliff info records");
	m->m_cliffInfo.assign((size_t)(m->m_numCliffInfo > 0 ? m->m_numCliffInfo : 1), TCliffInfo());
	for (int i = 1; i < m->m_numCliffInfo; ++i)
	{
		TCliffInfo &c = m->m_cliffInfo[(size_t)i];
		c.tileIndex = file.readInt();
		c.u0 = file.readReal();
		c.v0 = file.readReal();
		c.u1 = file.readReal();
		c.v1 = file.readReal();
		c.u2 = file.readReal();
		c.v2 = file.readReal();
		c.u3 = file.readReal();
		c.v3 = file.readReal();
		c.flip = file.readByte() != 0;
		c.mutant = file.readByte() != 0;
	}
	return true;
}

// ZH WorldHeightMap constructor, "patch bad maps" (WorldHeightMap.cpp:533-545); also in
// Open-BFME-1 WorldHeightMapRva0074ACB0Load.cpp.
void WorldHeightMap::patchBadIndices()
{
	// Only entries whose VALUE changes are counted: a map with numCliffInfo == 0 (osgiliath) makes
	// every index, including the 0 already stored, "out of range", and writing 0 over 0 patches nothing.
	m_patchReport = PatchReport();
	for (size_t i = 0; i < m_cliffInfoNdxes.size(); ++i)
	{
		if (m_cliffInfoNdxes[i] < 0 || m_cliffInfoNdxes[i] >= m_numCliffInfo)
		{
			m_patchReport.cliffIndicesPatched += m_cliffInfoNdxes[i] != 0;
			++m_patchReport.cliffFormallyOutOfRange;
			m_cliffInfoNdxes[i] = 0;
		}
		if (m_blendTileNdxes[i] < 0 || m_blendTileNdxes[i] >= m_numBlendedTiles)
		{
			m_patchReport.blendIndicesPatched += m_blendTileNdxes[i] != 0;
			++m_patchReport.blendFormallyOutOfRange;
			m_blendTileNdxes[i] = 0;
		}
		if (m_extraBlendTileNdxes[i] < 0 || m_extraBlendTileNdxes[i] >= m_numBlendedTiles)
		{
			m_patchReport.extraBlendIndicesPatched += m_extraBlendTileNdxes[i] != 0;
			++m_patchReport.extraBlendFormallyOutOfRange;
			m_extraBlendTileNdxes[i] = 0;
		}
	}
}

// ZH getCliffState / getFlipState bit access (WorldHeightMap.cpp:733-740).
bool WorldHeightMap::planeBit(const std::vector<std::uint8_t> &plane, int x, int y) const
{
	if (x < 0 || y < 0 || y >= m_height || x >= m_width || plane.empty())
	{
		return false;
	}
	return (plane[(size_t)y * m_planeStride + (x >> 3)] & (1 << (x & 7))) != 0;
}

std::uint32_t WorldHeightMap::crcHeights() const
{
	return crc32Bytes(m_data.data(), m_data.size() * sizeof(std::uint16_t));
}

std::uint32_t WorldHeightMap::crcTileNdxes() const
{
	return crc32Bytes(m_tileNdxes.data(), m_tileNdxes.size() * sizeof(std::int16_t));
}
