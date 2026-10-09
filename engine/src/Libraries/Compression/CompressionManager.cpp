// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// See CompressionManager.h for sources.

#include "Libraries/Compression/CompressionManager.h"

#include "Libraries/Compression/EAC/refdecode.h"

#include <cstring>

CompressionType CompressionManager::getCompressionType(const std::uint8_t *mem, size_t len)
{
	if (len < 8)
	{
		return COMPRESSION_NONE;
	}
	// BFME1 getCompressionType compares 4 bytes against the tag including its NUL.
	if (std::memcmp(mem, "NOX\0", 4) == 0)
	{
		return COMPRESSION_NOXLZH;
	}
	for (int level = 1; level <= 9; ++level)
	{
		const char tag[4] = { 'Z', 'L', (char)('0' + level), '\0' };
		if (std::memcmp(mem, tag, 4) == 0)
		{
			return (CompressionType)(COMPRESSION_ZLIB1 + level - 1);
		}
	}
	if (std::memcmp(mem, "EAB\0", 4) == 0)
	{
		return COMPRESSION_BTREE;
	}
	if (std::memcmp(mem, "EAH\0", 4) == 0)
	{
		return COMPRESSION_HUFF;
	}
	return std::memcmp(mem, "EAR\0", 4) == 0 ? COMPRESSION_REFPACK : COMPRESSION_NONE;
}

bool CompressionManager::isDataCompressed(const std::uint8_t *mem, size_t len)
{
	return getCompressionType(mem, len) != COMPRESSION_NONE;
}

std::int64_t CompressionManager::getUncompressedSize(const std::uint8_t *mem, size_t len)
{
	if (len < 8)
	{
		return (std::int64_t)len;
	}
	if (getCompressionType(mem, len) == COMPRESSION_NONE)
	{
		return (std::int64_t)len;
	}
	std::int32_t v;
	std::memcpy(&v, mem + 4, 4); // little-endian host, as the original reads *(Int *)(mem + 4)
	return v;
}

bool CompressionManager::decompressData(const std::uint8_t *mem, size_t len, std::vector<std::uint8_t> &out, std::string *error)
{
	CompressionType type = getCompressionType(mem, len);
	if (type != COMPRESSION_REFPACK)
	{
		if (error)
		{
			*error = type == COMPRESSION_NONE ? "not a compressed envelope" : "compression codec not ported (only RefPack 'EAR')";
		}
		return false;
	}
	std::int64_t declared = getUncompressedSize(mem, len);
	if (declared < 0)
	{
		if (error)
		{
			*error = "negative uncompressed size in envelope";
		}
		return false;
	}
	size_t consumed = 0;
	if (!REF_decode(mem + 8, len - 8, out, &consumed, error))
	{
		return false;
	}
	// Target/donor fact: the retail decoder stops at the terminator command and never looks at what
	// follows. Inference (reviewer finding MAP-1 P2): a stream with bytes after the terminator is not a
	// stream any retail tool wrote (all 181 corpus maps end exactly at the terminator), so it is refused
	// rather than silently truncated.
	if (consumed != len - 8)
	{
		if (error)
		{
			*error = "RefPack stream ends at byte " + std::to_string(consumed) + " of " + std::to_string(len - 8) + ": "
				+ std::to_string(len - 8 - consumed) + " trailing byte(s) after the terminator";
		}
		out.clear();
		return false;
	}
	// ZH CachedFileInputStream::open keeps the raw bytes when the lengths disagree; here that is
	// an error (no silent fallback).
	if ((std::int64_t)out.size() != declared)
	{
		if (error)
		{
			*error = "envelope declares " + std::to_string(declared) + " bytes, RefPack stream decodes to " + std::to_string(out.size());
		}
		out.clear();
		return false;
	}
	return true;
}
