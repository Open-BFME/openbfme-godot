// OpenBFME. GPL-3.0.
//
// Layer III constant tables that are standardised numeric data (generated, see Mp3Tables.cpp and
// tools/audio/gen_mp3_tables.py): Huffman code tables (ISO 11172-3 Table B.7) and the synthesis window.

#pragma once

#include <cstdint>

namespace AudioDecode
{
namespace mp3tab
{

constexpr int kHuffmanTableCount = 15; // ISO table numbers 1,2,3,5,6,7,8,9,10,11,12,13,15,16,24

struct HuffmanSource
{
	int tableNumber; // ISO table number
	int n;           // xlen == ylen
	const uint8_t *len;    // [x * n + y] code length in bits
	const uint32_t *code;  // [x * n + y] code, right-aligned
};

struct QuadSource
{
	const uint8_t *len;  // [v * 8 + w * 4 + x * 2 + y]
	const uint8_t *code;
};

extern const HuffmanSource kHuffmanSources[kHuffmanTableCount];
extern const QuadSource kQuadSources[2]; // count1 table A (select 0) and B (select 1)
extern const int32_t kSynthWindow257[257]; // D[i] * 65536, i = 0..256

} // namespace mp3tab
} // namespace AudioDecode
