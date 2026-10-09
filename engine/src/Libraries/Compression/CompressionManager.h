// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the read side of CompressionManager.
//   target fact (BFME1 decompile): Open-BFME-1 game/Libraries/Source/Compression/
//     CompressionManager_decompressData.cpp (getCompressionType, isDataCompressed,
//     getUncompressedSize, decompressData; retail 0x0081E560 / 0x0081E890).
//   donor fact: ZH Libraries/Source/Compression/CompressionManager.cpp:103-134, 166-190, 286-318.
//
// Envelope: 4-byte tag ("EAR\0", "ZL1".."ZL9", "NOX\0", "EAB\0", "EAH\0") + i32 uncompressed
// size at offset 4 + the codec stream.
//
// Scope: only RefPack ("EAR") is implemented, which is every envelope in the retail map corpus
// (181/181). The zlib, NOX, EAB and EAH codecs are not ported; asking for them is an error (a
// gap, never a silent pass-through of the raw bytes).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum CompressionType
{
	COMPRESSION_MIN = 0,
	COMPRESSION_NONE = COMPRESSION_MIN,
	COMPRESSION_REFPACK,
	COMPRESSION_NOXLZH,
	COMPRESSION_ZLIB1,
	COMPRESSION_ZLIB2,
	COMPRESSION_ZLIB3,
	COMPRESSION_ZLIB4,
	COMPRESSION_ZLIB5,
	COMPRESSION_ZLIB6,
	COMPRESSION_ZLIB7,
	COMPRESSION_ZLIB8,
	COMPRESSION_ZLIB9,
	COMPRESSION_BTREE,
	COMPRESSION_HUFF
};

class CompressionManager
{
public:
	static CompressionType getCompressionType(const std::uint8_t *mem, size_t len);
	static bool isDataCompressed(const std::uint8_t *mem, size_t len);

	// Declared uncompressed size of an enveloped buffer; `len` when it has no envelope.
	static std::int64_t getUncompressedSize(const std::uint8_t *mem, size_t len);

	// Decodes an enveloped buffer. The decoded length must equal the declared size. On success
	// `out` holds the data. Unenveloped input is NOT accepted here (callers check
	// isDataCompressed first, like CachedFileInputStream::open).
	static bool decompressData(const std::uint8_t *mem, size_t len, std::vector<std::uint8_t> &out, std::string *error);
};
