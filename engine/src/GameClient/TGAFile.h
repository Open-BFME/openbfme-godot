// OpenBFME. GPL-3.0.
//
// Uncompressed true-colour TGA reader for terrain tile textures.
//   target fact: Open-BFME-2 .../WorldHeightMapTiles.cpp (countTiles) and Open-BFME-1
//     WorldHeightMap.cpp:1545-1600 (readTiles): colour-mapped images are refused, only image type
//     2 (uncompressed) is read (RLE type 10 is rejected, widths over 1024 are rejected), pixel depth
//     24 or 32, BGR(A) byte order, and the origin flag (bit 5 of the descriptor) is IGNORED: rows are
//     taken in storage order, so a TGA saved top-down is flipped by retail (spec 2.3).
//   donor: ZH WorldHeightMap.cpp:1331-1465.
//
// `rgba` is in STORAGE row order (row 0 is the first row in the file) as RGBA, alpha 255 for 24-bit.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct TGAImage
{
	int width = 0;
	int height = 0;
	int pixelDepth = 0;
	bool topDownFlag = false; // descriptor bit 5; informational (terrain ignores it, like retail)
	std::vector<std::uint8_t> rgba;
};

namespace TGAFile
{
// maxDimension: images larger than this are refused (terrain: 1024).
// rle (lane MODUI-1): also read run-length encoded true-colour images (type 10). The terrain reader refuses them (above); the textures retail
// loads through its texture loader (the Apt menu images and map previews) are read with ZH WWLib TARGA.CPP Targa::Load, which decodes type 10
// (TGA_TRUECOLOR_ENCODED -> DecodeImage: a packet byte, bit 7 = a run of count + 1 copies of one pixel, else count + 1 literal pixels).
bool decode(const std::uint8_t *data, size_t size, TGAImage &out, std::string *error, int maxDimension = 1024, bool rle = false);
}
