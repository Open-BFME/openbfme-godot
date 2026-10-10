// OpenBFME. GPL-3.0.
//
// This file is derived from FFmpeg (https://ffmpeg.org), libavcodec, which carries these notices:
//
//   libavcodec/vp6.c, vp56.c, vp56data.c, vp6data.h, vp6dsp.c: Copyright (C) 2006 Aurelien Jacobs <aurel@gnuage.org>
//   libavcodec/vp3dsp.c: Copyright (C) 2004 The FFmpeg project
//   libavcodec/vpx_rac.c: Copyright (c) 2010 Fiona Glaser <fiona@x264.com>
//   libavcodec/h264chroma_template.c: Copyright (c) 2000, 2001 Fabrice Bellard; Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
//   libavcodec/videodsp_template.c: Copyright (c) 2002-2012 Michael Niedermayer; Copyright (C) 2012 Ronald S. Bultje
//   libavcodec/huffman.c: Copyright (c) 2006 Konstantin Shishkov; Copyright (c) 2007 Loren Merritt
//   libavcodec/mathtables.c: Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
//   FFmpeg is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as
//   published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.
//   FFmpeg is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
//
// Licence conversion: LGPL-2.1 section 3 lets the licensee apply the terms of the ordinary GNU General Public License (version 2 or any
// later version) to a copy of the Library instead of the LGPL. This derived copy is distributed under the GNU General Public License
// version 3 (as all of OpenBFME, see LICENSE and NOTICE); OpenBFME's changes (a C++ port: bitstream orientation, the EA container index,
// the output conversion) are Copyright (C) 2026 the OpenBFME contributors, GPL-3.0. This program is distributed WITHOUT ANY WARRANTY.
//
// See GameClient/VP6Decoder.h. The decoding process is FFmpeg's VP6 decoder (LGPL-2.1-or-later, see NOTICE) ported to plain C++: the function
// names in the comments are FFmpeg's (libavcodec/vp56.c, vp6.c, vp3dsp.c ...). It works in the bitstream's orientation (FFmpeg's flip = +1 layout:
// block 0 on top) and the output is flipped (the EA container's VP6 is stored upside down).

#include "GameClient/VP6Decoder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace
{
struct VP6Tree
{
	std::int8_t val;
	std::int8_t probIdx;
};

#include "GameClient/VP6Tables.inc"

// libavcodec/mathtables.c ff_zigzag_direct
const std::uint8_t kZigzag[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

// libavcodec/vpx_rac.c ff_vpx_norm_shift
const std::uint8_t kNormShift[256] = { 8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// the macroblock types (FFmpeg VP56mb)
enum MbType
{
	MB_INTER_NOVEC_PF = 0,
	MB_INTRA = 1,
	MB_INTER_DELTA_PF = 2,
	MB_INTER_V1_PF = 3,
	MB_INTER_V2_PF = 4,
	MB_INTER_NOVEC_GF = 5,
	MB_INTER_DELTA_GF = 6,
	MB_INTER_4V = 7,
	MB_INTER_V1_GF = 8,
	MB_INTER_V2_GF = 9,
};

// the reference frames (FFmpeg VP56Frame)
enum RefFrame
{
	FRAME_NONE = -1,
	FRAME_CURRENT = 0,
	FRAME_PREVIOUS = 1,
	FRAME_GOLDEN = 2,
};

inline std::uint8_t clipU8(int v) { return (std::uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
inline int clipInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// the boolean range decoder (vpx_rac.h); the input is padded with zero bytes (FFmpeg's packet padding)
struct RangeCoder
{
	int high = 0;
	int bits = 0;
	const std::uint8_t *buffer = nullptr;
	const std::uint8_t *end = nullptr;
	unsigned codeWord = 0;
	int endReached = 0;

	bool init(const std::uint8_t *buf, int size)
	{
		// ff_vpx_init_range_decoder
		high = 255;
		bits = -16;
		buffer = buf;
		end = buf + size;
		endReached = 0;
		if (size < 1)
		{
			return false;
		}
		codeWord = ((unsigned)buffer[0] << 16) | ((unsigned)buffer[1] << 8) | buffer[2];
		buffer += 3;
		return true;
	}
	bool isEnd()
	{
		// vpx_rac_is_end
		if (end <= buffer && bits >= 0)
		{
			endReached += 1;
		}
		return endReached > 10;
	}
	unsigned renorm()
	{
		const int shift = kNormShift[high];
		int b = bits;
		unsigned cw = codeWord;
		high <<= shift;
		cw <<= shift;
		b += shift;
		if (b >= 0 && buffer < end)
		{
			cw |= (((unsigned)buffer[0] << 8) | buffer[1]) << b;
			buffer += 2;
			b -= 16;
		}
		bits = b;
		return cw;
	}
	int getProb(int prob)
	{
		const unsigned cw = renorm();
		const unsigned low = 1 + (((unsigned)(high - 1) * (unsigned)prob) >> 8);
		const unsigned lowShift = low << 16;
		const int bit = cw >= lowShift;
		high = bit ? high - (int)low : (int)low;
		codeWord = bit ? cw - lowShift : cw;
		return bit;
	}
	int get()
	{
		// equiprobable
		unsigned cw = renorm();
		const int low = (high + 1) >> 1;
		const unsigned lowShift = (unsigned)low << 16;
		const int bit = cw >= lowShift;
		if (bit)
		{
			high -= low;
			cw -= lowShift;
		}
		else
		{
			high = low;
		}
		codeWord = cw;
		return bit;
	}
	int gets(int n)
	{
		// vp56_rac_gets
		int value = 0;
		while (n--)
		{
			value = (value << 1) | get();
		}
		return value;
	}
	int getsNn(int n)
	{
		// vp56_rac_gets_nn (always 7 bits)
		const int v = gets(n) << 1;
		return v + !v;
	}
	int getTree(const VP6Tree *tree, const std::uint8_t *probs)
	{
		// vp56_rac_get_tree
		while (tree->val > 0)
		{
			if (getProb(probs[tree->probIdx]))
			{
				tree += tree->val;
			}
			else
			{
				tree++;
			}
		}
		return -tree->val;
	}
};

// MSB-first bit reader of the Huffman coefficient partition (get_bits.h); reads past the end return zero bits
struct BitReader
{
	const std::uint8_t *data = nullptr;
	int sizeBits = 0;
	int index = 0;

	void init(const std::uint8_t *d, int sizeBytes)
	{
		data = d;
		sizeBits = sizeBytes * 8;
		index = 0;
	}
	int left() const { return sizeBits - index; }
	unsigned bit()
	{
		unsigned b = 0;
		if (index < sizeBits)
		{
			b = (data[index >> 3] >> (7 - (index & 7))) & 1;
		}
		index += 1;
		return b;
	}
	unsigned bits(int n)
	{
		unsigned v = 0;
		while (n-- > 0)
		{
			v = (v << 1) | bit();
		}
		return v;
	}
};

// a Huffman tree as ff_huff_build_tree builds it (FF_HUFFMAN_FLAG_HNODE_FIRST); decoding walks it bit by bit (0: n0, 1: n0 + 1)
struct HuffTree
{
	struct Node
	{
		std::int16_t sym = 0;
		std::int16_t n0 = 0;
		std::uint32_t count = 0;
	};
	Node nodes[24];
	int head = 0;

	// vp6_build_huff_tree
	void build(const std::uint8_t *coeffModel, const std::uint8_t *map, int size)
	{
		Node *tmp = &nodes[size];
		for (Node &n : nodes)
		{
			n = Node();
		}
		tmp[0].count = 256;
		for (int i = 0; i < size - 1; ++i)
		{
			const std::uint32_t a = tmp[i].count * coeffModel[i] >> 8;
			const std::uint32_t b = tmp[i].count * (255u - coeffModel[i]) >> 8;
			nodes[map[2 * i]].count = a + !a;
			nodes[map[2 * i + 1]].count = b + !b;
		}
		// ff_huff_build_tree: the leaves ascend by count, the symbols descend among equal counts (vp6_huff_cmp: a strict order)
		for (int i = 0; i < size; ++i)
		{
			nodes[i].sym = (std::int16_t)i;
			nodes[i].n0 = -2;
		}
		std::sort(nodes, nodes + size, [](const Node &a, const Node &b) {
			const long long d = ((long long)a.count - (long long)b.count) * 16 + (b.sym - a.sym);
			return d < 0;
		});
		int curNode = size;
		nodes[size * 2 - 1].count = 0;
		for (int i = 0; i < size * 2 - 1; i += 2)
		{
			const std::uint32_t curCount = nodes[i].count + nodes[i + 1].count;
			int j;
			for (j = curNode; j > i + 2; --j)
			{
				if (curCount > nodes[j - 1].count)
				{
					break;
				}
				nodes[j] = nodes[j - 1];
			}
			nodes[j].sym = -1; // HNODE
			nodes[j].count = curCount;
			nodes[j].n0 = (std::int16_t)i;
			curNode++;
		}
		head = size * 2 - 2;
	}
	int decode(BitReader &gb) const
	{
		int n = head;
		int depth = 0;
		while (nodes[n].sym == -1 && nodes[n].count != 0)
		{
			n = nodes[n].n0 + (int)gb.bit();
			if (++depth > 30)
			{
				return -1; // longer than get_vlc2's 3 x 10 bits
			}
		}
		return nodes[n].sym;
	}
};

struct Mv
{
	std::int16_t x = 0, y = 0;
};

struct RefDc
{
	std::uint8_t notNullDc = 0;
	int refFrame = FRAME_NONE;
	std::int16_t dcCoeff = 0;
};

struct Macroblock
{
	std::uint8_t type = 0;
	Mv mv;
};

struct Model
{
	std::uint8_t coeffReorder[64];
	std::uint8_t coeffIndexToPos[64];
	std::uint8_t coeffIndexToIdctSelector[64];
	std::uint8_t vectorSig[2];
	std::uint8_t vectorDct[2];
	std::uint8_t vectorPdv[2][7];
	std::uint8_t vectorFdv[2][8];
	std::uint8_t coeffDccv[2][11];
	std::uint8_t coeffRact[2][3][6][11];
	std::uint8_t coeffDcct[2][36][5];
	std::uint8_t coeffRunv[2][14];
	std::uint8_t mbType[3][10][10];
	std::uint8_t mbTypesStats[3][10][2];
};

struct Frame
{
	std::vector<std::uint8_t> plane[3];
	bool valid = false;
};

// ---- the VP3 IDCT and loop filter (vp3dsp.c) ----------------------------------------------------------------------------------------------------------

const int xC1S7 = 64277, xC2S6 = 60547, xC3S5 = 54491, xC4S4 = 46341, xC5S3 = 36410, xC6S2 = 25080, xC7S1 = 12785;
inline int M(int a, int b) { return (int)((unsigned)a * (unsigned)b) >> 16; }

// type 1: put, 2: add
void idct(std::uint8_t *dst, std::ptrdiff_t stride, std::int16_t *input, int type)
{
	std::int16_t *ip = input;
	for (int i = 0; i < 8; i++)
	{
		if (ip[0 * 8] | ip[1 * 8] | ip[2 * 8] | ip[3 * 8] | ip[4 * 8] | ip[5 * 8] | ip[6 * 8] | ip[7 * 8])
		{
			const int A = M(xC1S7, ip[1 * 8]) + M(xC7S1, ip[7 * 8]);
			const int B = M(xC7S1, ip[1 * 8]) - M(xC1S7, ip[7 * 8]);
			const int C = M(xC3S5, ip[3 * 8]) + M(xC5S3, ip[5 * 8]);
			const int D = M(xC3S5, ip[5 * 8]) - M(xC5S3, ip[3 * 8]);
			const int Ad = M(xC4S4, (A - C));
			const int Bd = M(xC4S4, (B - D));
			const int Cd = A + C;
			const int Dd = B + D;
			const int E = M(xC4S4, (ip[0 * 8] + ip[4 * 8]));
			const int F = M(xC4S4, (ip[0 * 8] - ip[4 * 8]));
			const int G = M(xC2S6, ip[2 * 8]) + M(xC6S2, ip[6 * 8]);
			const int H = M(xC6S2, ip[2 * 8]) - M(xC2S6, ip[6 * 8]);
			const int Ed = E - G, Gd = E + G, Add = F + Ad, Bdd = Bd - H, Fd = F - Ad, Hd = Bd + H;
			ip[0 * 8] = (std::int16_t)(Gd + Cd);
			ip[7 * 8] = (std::int16_t)(Gd - Cd);
			ip[1 * 8] = (std::int16_t)(Add + Hd);
			ip[2 * 8] = (std::int16_t)(Add - Hd);
			ip[3 * 8] = (std::int16_t)(Ed + Dd);
			ip[4 * 8] = (std::int16_t)(Ed - Dd);
			ip[5 * 8] = (std::int16_t)(Fd + Bdd);
			ip[6 * 8] = (std::int16_t)(Fd - Bdd);
		}
		ip += 1;
	}
	ip = input;
	for (int i = 0; i < 8; i++)
	{
		if (ip[1] | ip[2] | ip[3] | ip[4] | ip[5] | ip[6] | ip[7])
		{
			const int A = M(xC1S7, ip[1]) + M(xC7S1, ip[7]);
			const int B = M(xC7S1, ip[1]) - M(xC1S7, ip[7]);
			const int C = M(xC3S5, ip[3]) + M(xC5S3, ip[5]);
			const int D = M(xC3S5, ip[5]) - M(xC5S3, ip[3]);
			const int Ad = M(xC4S4, (A - C));
			const int Bd = M(xC4S4, (B - D));
			const int Cd = A + C;
			const int Dd = B + D;
			int E = M(xC4S4, (ip[0] + ip[4])) + 8;
			int F = M(xC4S4, (ip[0] - ip[4])) + 8;
			if (type == 1)
			{
				E += 16 * 128;
				F += 16 * 128;
			}
			const int G = M(xC2S6, ip[2]) + M(xC6S2, ip[6]);
			const int H = M(xC6S2, ip[2]) - M(xC2S6, ip[6]);
			const int Ed = E - G, Gd = E + G, Add = F + Ad, Bdd = Bd - H, Fd = F - Ad, Hd = Bd + H;
			const int out[8] = { Gd + Cd, Add + Hd, Add - Hd, Ed + Dd, Ed - Dd, Fd + Bdd, Fd - Bdd, Gd - Cd };
			for (int k = 0; k < 8; ++k)
			{
				dst[k * stride] = type == 1 ? clipU8(out[k] >> 4) : clipU8(dst[k * stride] + (out[k] >> 4));
			}
		}
		else
		{
			if (type == 1)
			{
				const std::uint8_t v = clipU8(128 + ((xC4S4 * ip[0] + (8 << 16)) >> 20));
				for (int k = 0; k < 8; ++k)
				{
					dst[k * stride] = v;
				}
			}
			else if (ip[0])
			{
				const int v = (xC4S4 * ip[0] + (8 << 16)) >> 20;
				for (int k = 0; k < 8; ++k)
				{
					dst[k * stride] = clipU8(dst[k * stride] + v);
				}
			}
		}
		ip += 8;
		dst++;
	}
	std::memset(input, 0, sizeof(std::int16_t) * 64);
}

void idct10(std::uint8_t *dst, std::ptrdiff_t stride, std::int16_t *input, int type)
{
	std::int16_t *ip = input;
	for (int i = 0; i < 4; i++)
	{
		if (ip[0 * 8] | ip[1 * 8] | ip[2 * 8] | ip[3 * 8])
		{
			const int A = M(xC1S7, ip[1 * 8]);
			const int B = M(xC7S1, ip[1 * 8]);
			const int C = M(xC3S5, ip[3 * 8]);
			const int D = -M(xC5S3, ip[3 * 8]);
			const int Ad = M(xC4S4, (A - C));
			const int Bd = M(xC4S4, (B - D));
			const int Cd = A + C;
			const int Dd = B + D;
			const int E = M(xC4S4, ip[0 * 8]);
			const int F = E;
			const int G = M(xC2S6, ip[2 * 8]);
			const int H = M(xC6S2, ip[2 * 8]);
			const int Ed = E - G, Gd = E + G, Add = F + Ad, Bdd = Bd - H, Fd = F - Ad, Hd = Bd + H;
			ip[0 * 8] = (std::int16_t)(Gd + Cd);
			ip[7 * 8] = (std::int16_t)(Gd - Cd);
			ip[1 * 8] = (std::int16_t)(Add + Hd);
			ip[2 * 8] = (std::int16_t)(Add - Hd);
			ip[3 * 8] = (std::int16_t)(Ed + Dd);
			ip[4 * 8] = (std::int16_t)(Ed - Dd);
			ip[5 * 8] = (std::int16_t)(Fd + Bdd);
			ip[6 * 8] = (std::int16_t)(Fd - Bdd);
		}
		ip += 1;
	}
	ip = input;
	for (int i = 0; i < 8; i++)
	{
		if (ip[0] | ip[1] | ip[2] | ip[3])
		{
			const int A = M(xC1S7, ip[1]);
			const int B = M(xC7S1, ip[1]);
			const int C = M(xC3S5, ip[3]);
			const int D = -M(xC5S3, ip[3]);
			const int Ad = M(xC4S4, (A - C));
			const int Bd = M(xC4S4, (B - D));
			const int Cd = A + C;
			const int Dd = B + D;
			int E = M(xC4S4, ip[0]);
			if (type == 1)
			{
				E += 16 * 128;
			}
			const int F = E;
			const int G = M(xC2S6, ip[2]);
			const int H = M(xC6S2, ip[2]);
			int Ed = E - G, Gd = E + G, Add = F + Ad;
			const int Bdd = Bd - H;
			int Fd = F - Ad;
			const int Hd = Bd + H;
			Gd += 8;
			Add += 8;
			Ed += 8;
			Fd += 8;
			const int out[8] = { Gd + Cd, Add + Hd, Add - Hd, Ed + Dd, Ed - Dd, Fd + Bdd, Fd - Bdd, Gd - Cd };
			for (int k = 0; k < 8; ++k)
			{
				dst[k * stride] = type == 1 ? clipU8(out[k] >> 4) : clipU8(dst[k * stride] + (out[k] >> 4));
			}
		}
		else if (type == 1)
		{
			for (int k = 0; k < 8; ++k)
			{
				dst[k * stride] = 128;
			}
		}
		ip += 8;
		dst++;
	}
	std::memset(input, 0, sizeof(std::int16_t) * 64);
}

void idctDcAdd(std::uint8_t *dest, std::ptrdiff_t stride, std::int16_t *block)
{
	// vp3_idct_dc_add_c: only the DC is cleared (FFmpeg's behaviour, kept)
	const int dc = (block[0] + 15) >> 5;
	for (int i = 0; i < 8; i++)
	{
		for (int k = 0; k < 8; ++k)
		{
			dest[k] = clipU8(dest[k] + dc);
		}
		dest += stride;
	}
	block[0] = 0;
}

void setBoundingValues(int *array, int filterLimit)
{
	// ff_vp3dsp_set_bounding_values
	int *bv = array + 127;
	std::memset(array, 0, 256 * sizeof(int));
	for (int x = 0; x < filterLimit; x++)
	{
		bv[-x] = -x;
		bv[x] = x;
	}
	int x, value;
	for (x = value = filterLimit; x < 128 && value; x++, value--)
	{
		bv[x] = value;
		bv[-x] = -value;
	}
	if (value)
	{
		bv[128] = value;
	}
	bv[129] = bv[130] = (int)((unsigned)filterLimit * 0x02020202U);
}

void vLoopFilter12(std::uint8_t *first, std::ptrdiff_t stride, const int *bv)
{
	const std::ptrdiff_t nstride = -stride;
	for (std::uint8_t *end = first + 12; first < end; first++)
	{
		int f = (first[2 * nstride] - first[stride]) + (first[0] - first[nstride]) * 3;
		f = bv[(f + 4) >> 3];
		first[nstride] = clipU8(first[nstride] + f);
		first[0] = clipU8(first[0] - f);
	}
}

void hLoopFilter12(std::uint8_t *first, std::ptrdiff_t stride, const int *bv)
{
	for (int i = 0; i < 12; ++i, first += stride)
	{
		int f = (first[-2] - first[1]) + (first[0] - first[-1]) * 3;
		f = bv[(f + 4) >> 3];
		first[-1] = clipU8(first[-1] + f);
		first[0] = clipU8(first[0] - f);
	}
}

// h264chroma put_h264_chroma_mc8 (8 wide, h rows): the bilinear block copy, ((A s0 + B s1 + C s[stride] + D s[stride + 1]) + 32) >> 6
void chromaMc8(std::uint8_t *dst, const std::uint8_t *src, std::ptrdiff_t stride, int h, int x, int y)
{
	const int A = (8 - x) * (8 - y), B = x * (8 - y), C = (8 - x) * y, D = x * y;
	for (int i = 0; i < h; i++)
	{
		for (int k = 0; k < 8; ++k)
		{
			int v;
			if (D)
			{
				v = A * src[k] + B * src[k + 1] + C * src[stride + k] + D * src[stride + k + 1];
			}
			else if (B + C)
			{
				const std::ptrdiff_t step = C ? stride : 1;
				v = A * src[k] + (B + C) * src[step + k];
			}
			else
			{
				v = A * src[k];
			}
			dst[k] = (std::uint8_t)((v + 32) >> 6);
		}
		dst += stride;
		src += stride;
	}
}

void filterHv4(std::uint8_t *dst, const std::uint8_t *src, std::ptrdiff_t stride, std::ptrdiff_t delta, const std::int16_t *w)
{
	// vp6_filter_hv4
	for (int y = 0; y < 8; y++)
	{
		for (int x = 0; x < 8; x++)
		{
			dst[x] = clipU8((src[x - delta] * w[0] + src[x] * w[1] + src[x + delta] * w[2] + src[x + 2 * delta] * w[3] + 64) >> 7);
		}
		src += stride;
		dst += stride;
	}
}

void filterDiag4(std::uint8_t *dst, const std::uint8_t *src, std::ptrdiff_t stride, const std::int16_t *hw, const std::int16_t *vw)
{
	// ff_vp6_filter_diag4_c
	int tmp[8 * 11];
	int *t = tmp;
	src -= stride;
	for (int y = 0; y < 11; y++)
	{
		for (int x = 0; x < 8; x++)
		{
			t[x] = clipU8((src[x - 1] * hw[0] + src[x] * hw[1] + src[x + 1] * hw[2] + src[x + 2] * hw[3] + 64) >> 7);
		}
		src += stride;
		t += 8;
	}
	t = tmp + 8;
	for (int y = 0; y < 8; y++)
	{
		for (int x = 0; x < 8; x++)
		{
			dst[x] = clipU8((t[x - 8] * vw[0] + t[x] * vw[1] + t[x + 8] * vw[2] + t[x + 16] * vw[3] + 64) >> 7);
		}
		dst += stride;
		t += 8;
	}
}

int blockVariance(const std::uint8_t *src, std::ptrdiff_t stride)
{
	// vp6_block_variance
	int sum = 0, squareSum = 0;
	for (int y = 0; y < 8; y += 2)
	{
		for (int x = 0; x < 8; x += 2)
		{
			sum += src[x];
			squareSum += src[x] * src[x];
		}
		src += 2 * stride;
	}
	return (16 * squareSum - sum * sum) >> 8;
}

inline int rshift2(int a)
{
	// RSHIFT(a, 2)
	return a > 0 ? (a + 2) >> 2 : (a + 1) >> 2;
}

const int kSizeChange = 1;
} // namespace

struct VP6Decoder::Impl
{
	Frame frames[3]; // current, previous, golden
	int planeWidth[3] = { 0, 0, 0 }, planeHeight[3] = { 0, 0, 0 };
	int mbWidth = 0, mbHeight = 0;
	int blockOffset[6] = { 0, 0, 0, 0, 0, 0 };
	int quantizer = -1;
	int dequantDc = 0, dequantAc = 0;
	int boundingValues[258]; // ff_vp3dsp_set_bounding_values writes [127 + 129] and [127 + 130] past its 256 (FFmpeg: into the next field)
	std::vector<RefDc> aboveBlocks;
	RefDc leftBlock[4];
	int aboveBlockIdx[6] = { 0, 0, 0, 0, 0, 0 };
	std::int16_t prevDc[3][3];
	int mbType = 0;
	std::vector<Macroblock> macroblocks;
	std::int16_t blockCoeff[6][64];
	int idctSelector[6] = { 0, 0, 0, 0, 0, 0 };
	Mv mv[6];
	Mv vectorCandidate[2];
	int vectorCandidatePos = 0;
	int filterHeader = 0, deblockFiltering = 0, filterSelection = 0, filterMode = 0, maxVectorLength = 0, sampleVarianceThreshold = 0;
	int subVersion = 0;
	int goldenFrame = 0;
	bool key = false;
	int useHuffman = 0;
	RangeCoder c, cc;
	RangeCoder *ccp = &c;
	BitReader gb;
	bool huffmanCoeffs = false;
	HuffTree dccvVlc[2], runvVlc[2], ractVlc[2][3][6];
	unsigned nbNull[2][2] = { { 0, 0 }, { 0, 0 } };
	Model model;
	std::uint8_t idctScantable[64];
	std::vector<std::uint8_t> packet; // the frame, padded with zero bytes

	Impl()
	{
		std::memset(blockCoeff, 0, sizeof blockCoeff);
		std::memset(prevDc, 0, sizeof prevDc);
		std::memset(&model, 0, sizeof model);
		std::memset(boundingValues, 0, sizeof boundingValues);
		for (int i = 0; i < 64; i++)
		{
			idctScantable[i] = (std::uint8_t)((kZigzag[i] >> 3) | ((kZigzag[i] & 7) << 3));
		}
	}

	std::ptrdiff_t stride(int plane) const { return planeWidth[plane]; }

	void initDequant(int q)
	{
		// ff_vp56_init_dequant
		if (quantizer != q)
		{
			setBoundingValues(boundingValues, vp56_filter_threshold[q]);
		}
		quantizer = q;
		dequantDc = vp56_dc_dequant[q] << 2;
		dequantAc = vp56_ac_dequant[q] << 2;
	}

	// ---- vp6.c --------------------------------------------------------------------------------------------------------------------------------------

	int parseHeader(const std::uint8_t *buf, int bufSize, std::string *error, int &cols, int &rows)
	{
		// vp6_parse_header; the frame's dimensions come back in cols / rows (a key frame)
		int parseFilterInfo = 0;
		int coeffOffset = 0;
		int vrtShift = 0;
		int res = 0;
		const int separatedCoeff = buf[0] & 1;
		key = !(buf[0] & 0x80);
		initDequant((buf[0] >> 1) & 0x3F);
		if (key)
		{
			const int sub = buf[1] >> 3;
			if (sub > 8)
			{
				*error = "VP6 sub-version " + std::to_string(sub) + " is not VP6";
				return -1;
			}
			filterHeader = buf[1] & 0x06;
			if (buf[1] & 1)
			{
				*error = "VP6 interlaced frames are not supported (no retail movie uses them)";
				return -1;
			}
			if (separatedCoeff || !filterHeader)
			{
				coeffOffset = ((buf[2] << 8) | buf[3]) - 2;
				buf += 2;
				bufSize -= 2;
			}
			rows = buf[2];
			cols = buf[3];
			if (!rows || !cols)
			{
				*error = "VP6 key frame of size 0";
				return -1;
			}
			if (macroblocks.empty() || 16 * cols != planeWidth[0] || 16 * rows != planeHeight[0])
			{
				res = kSizeChange;
			}
			if (!c.init(buf + 6, bufSize - 6))
			{
				*error = "VP6 key frame header truncated";
				return -1;
			}
			c.gets(2);
			parseFilterInfo = filterHeader;
			if (sub < 8)
			{
				vrtShift = 5;
			}
			subVersion = sub;
			goldenFrame = 0;
		}
		else
		{
			if (!subVersion || !planeWidth[0] || !planeHeight[0])
			{
				*error = "VP6 inter frame before any key frame";
				return -1;
			}
			if (separatedCoeff || !filterHeader)
			{
				coeffOffset = ((buf[1] << 8) | buf[2]) - 2;
				buf += 2;
				bufSize -= 2;
			}
			if (!c.init(buf + 1, bufSize - 1))
			{
				*error = "VP6 inter frame truncated";
				return -1;
			}
			goldenFrame = c.get();
			if (filterHeader)
			{
				deblockFiltering = c.get();
				if (deblockFiltering)
				{
					c.get();
				}
				if (subVersion > 7)
				{
					parseFilterInfo = c.get();
				}
			}
		}
		if (parseFilterInfo)
		{
			if (c.get())
			{
				filterMode = 2;
				sampleVarianceThreshold = c.gets(5) << vrtShift;
				maxVectorLength = 2 << c.gets(3);
			}
			else if (c.get())
			{
				filterMode = 1;
			}
			else
			{
				filterMode = 0;
			}
			filterSelection = subVersion > 7 ? c.gets(4) : 16;
		}
		useHuffman = c.get();
		huffmanCoeffs = false;
		if (coeffOffset)
		{
			buf += coeffOffset;
			bufSize -= coeffOffset;
			if (bufSize < 0)
			{
				*error = "VP6 coefficient partition outside the frame";
				return -1;
			}
			if (useHuffman)
			{
				huffmanCoeffs = true;
				gb.init(buf, bufSize);
			}
			else
			{
				if (!cc.init(buf, bufSize))
				{
					*error = "VP6 coefficient partition empty";
					return -1;
				}
				ccp = &cc;
			}
		}
		else
		{
			ccp = &c;
		}
		return res;
	}

	void coeffOrderTableInit()
	{
		// vp6_coeff_order_table_init
		int idx = 1;
		model.coeffIndexToPos[0] = 0;
		for (int i = 0; i < 16; i++)
		{
			for (int pos = 1; pos < 64; pos++)
			{
				if (model.coeffReorder[pos] == i)
				{
					model.coeffIndexToPos[idx++] = (std::uint8_t)pos;
				}
			}
		}
		for (idx = 0; idx < 64; idx++)
		{
			int max = 0;
			for (int i = 0; i <= idx; i++)
			{
				const int v = model.coeffIndexToPos[i];
				if (v > max)
				{
					max = v;
				}
			}
			if (subVersion > 6)
			{
				max++;
			}
			model.coeffIndexToIdctSelector[idx] = (std::uint8_t)max;
		}
	}

	void defaultModelsInit()
	{
		// vp6_default_models_init
		model.vectorDct[0] = 0xA2;
		model.vectorDct[1] = 0xA4;
		model.vectorSig[0] = 0x80;
		model.vectorSig[1] = 0x80;
		std::memcpy(model.mbTypesStats, vp56_def_mb_types_stats, sizeof model.mbTypesStats);
		std::memcpy(model.vectorFdv, vp6_def_fdv_vector_model, sizeof model.vectorFdv);
		std::memcpy(model.vectorPdv, vp6_def_pdv_vector_model, sizeof model.vectorPdv);
		std::memcpy(model.coeffRunv, vp6_def_runv_coeff_model, sizeof model.coeffRunv);
		std::memcpy(model.coeffReorder, vp6_def_coeff_reorder, sizeof model.coeffReorder);
		coeffOrderTableInit();
	}

	void parseVectorModels()
	{
		// vp6_parse_vector_models
		for (int comp = 0; comp < 2; comp++)
		{
			if (c.getProb(vp6_sig_dct_pct[comp][0]))
			{
				model.vectorDct[comp] = (std::uint8_t)c.getsNn(7);
			}
			if (c.getProb(vp6_sig_dct_pct[comp][1]))
			{
				model.vectorSig[comp] = (std::uint8_t)c.getsNn(7);
			}
		}
		for (int comp = 0; comp < 2; comp++)
		{
			for (int node = 0; node < 7; node++)
			{
				if (c.getProb(vp6_pdv_pct[comp][node]))
				{
					model.vectorPdv[comp][node] = (std::uint8_t)c.getsNn(7);
				}
			}
		}
		for (int comp = 0; comp < 2; comp++)
		{
			for (int node = 0; node < 8; node++)
			{
				if (c.getProb(vp6_fdv_pct[comp][node]))
				{
					model.vectorFdv[comp][node] = (std::uint8_t)c.getsNn(7);
				}
			}
		}
	}

	bool parseCoeffModels()
	{
		// vp6_parse_coeff_models
		int defProb[11];
		for (int &p : defProb)
		{
			p = 0x80808080; // memset(def_prob, 0x80, ...): only stored as uint8 (0x80)
		}
		for (int pt = 0; pt < 2; pt++)
		{
			for (int node = 0; node < 11; node++)
			{
				if (c.getProb(vp6_dccv_pct[pt][node]))
				{
					defProb[node] = c.getsNn(7);
					model.coeffDccv[pt][node] = (std::uint8_t)defProb[node];
				}
				else if (key)
				{
					model.coeffDccv[pt][node] = (std::uint8_t)defProb[node];
				}
			}
		}
		if (c.get())
		{
			for (int pos = 1; pos < 64; pos++)
			{
				if (c.getProb(vp6_coeff_reorder_pct[pos]))
				{
					model.coeffReorder[pos] = (std::uint8_t)c.gets(4);
				}
			}
			coeffOrderTableInit();
		}
		for (int cg = 0; cg < 2; cg++)
		{
			for (int node = 0; node < 14; node++)
			{
				if (c.getProb(vp6_runv_pct[cg][node]))
				{
					model.coeffRunv[cg][node] = (std::uint8_t)c.getsNn(7);
				}
			}
		}
		for (int ct = 0; ct < 3; ct++)
		{
			for (int pt = 0; pt < 2; pt++)
			{
				for (int cg = 0; cg < 6; cg++)
				{
					for (int node = 0; node < 11; node++)
					{
						if (c.getProb(vp6_ract_pct[ct][pt][cg][node]))
						{
							defProb[node] = c.getsNn(7);
							model.coeffRact[pt][ct][cg][node] = (std::uint8_t)defProb[node];
						}
						else if (key)
						{
							model.coeffRact[pt][ct][cg][node] = (std::uint8_t)defProb[node];
						}
					}
				}
			}
		}
		if (useHuffman)
		{
			for (int pt = 0; pt < 2; pt++)
			{
				dccvVlc[pt].build(model.coeffDccv[pt], vp6_huff_coeff_map, 12);
				runvVlc[pt].build(model.coeffRunv[pt], vp6_huff_run_map, 9);
				for (int ct = 0; ct < 3; ct++)
				{
					for (int cg = 0; cg < 6; cg++)
					{
						ractVlc[pt][ct][cg].build(model.coeffRact[pt][ct][cg], vp6_huff_coeff_map, 12);
					}
				}
			}
			std::memset(nbNull, 0, sizeof nbNull);
		}
		else
		{
			// coeff_dcct is a linear combination of coeff_dccv
			for (int pt = 0; pt < 2; pt++)
			{
				for (int ctx = 0; ctx < 3; ctx++)
				{
					for (int node = 0; node < 5; node++)
					{
						model.coeffDcct[pt][ctx][node] = (std::uint8_t)clipInt(
							((model.coeffDccv[pt][node] * vp6_dccv_lc[ctx][node][0] + 128) >> 8) + vp6_dccv_lc[ctx][node][1], 1, 255);
					}
				}
			}
		}
		return true;
	}

	void parseVectorAdjustment(Mv &vect)
	{
		// vp6_parse_vector_adjustment
		vect = Mv();
		if (vectorCandidatePos < 2)
		{
			vect = vectorCandidate[0];
		}
		for (int comp = 0; comp < 2; comp++)
		{
			int delta = 0;
			if (c.getProb(model.vectorDct[comp]))
			{
				static const std::uint8_t probOrder[] = { 0, 1, 2, 7, 6, 5, 4 };
				for (std::uint8_t j : probOrder)
				{
					delta |= c.getProb(model.vectorFdv[comp][j]) << j;
				}
				if (delta & 0xF0)
				{
					delta |= c.getProb(model.vectorFdv[comp][3]) << 3;
				}
				else
				{
					delta |= 8;
				}
			}
			else
			{
				delta = c.getTree(vp56_pva_tree, model.vectorPdv[comp]);
			}
			if (delta && c.getProb(model.vectorSig[comp]))
			{
				delta = -delta;
			}
			if (!comp)
			{
				vect.x = (std::int16_t)(vect.x + delta);
			}
			else
			{
				vect.y = (std::int16_t)(vect.y + delta);
			}
		}
	}

	unsigned getNbNull()
	{
		// vp6_get_nb_null
		unsigned val = gb.bits(2);
		if (val == 2)
		{
			val += gb.bits(2);
		}
		else if (val == 3)
		{
			val = gb.bit() << 2;
			val = 6 + val + gb.bits(2 + (int)val);
		}
		return val;
	}

	bool parseCoeffHuffman()
	{
		// vp6_parse_coeff_huffman
		const std::uint8_t *permute = idctScantable;
		int pt = 0;
		for (int b = 0; b < 6; b++)
		{
			int ct = 0;
			if (b > 3)
			{
				pt = 1;
			}
			const HuffTree *vlcCoeff = &dccvVlc[pt];
			int coeffIdx;
			for (coeffIdx = 0;;)
			{
				int run = 1;
				if (coeffIdx < 2 && nbNull[coeffIdx][pt])
				{
					nbNull[coeffIdx][pt]--;
					if (coeffIdx)
					{
						break;
					}
				}
				else
				{
					if (gb.left() <= 0)
					{
						return false;
					}
					const int coeff = vlcCoeff->decode(gb);
					if (coeff < 0)
					{
						return false;
					}
					if (coeff == 0)
					{
						if (coeffIdx)
						{
							const int rpt = coeffIdx >= 6;
							const int r = runvVlc[rpt].decode(gb);
							if (r < 0)
							{
								return false;
							}
							run += r;
							if (run >= 9)
							{
								run += (int)gb.bits(6);
							}
						}
						else
						{
							nbNull[0][pt] = getNbNull();
						}
						ct = 0;
					}
					else if (coeff == 11)
					{
						// end of block
						if (coeffIdx == 1)
						{
							nbNull[1][pt] = getNbNull();
						}
						break;
					}
					else
					{
						int coeff2 = vp56_coeff_bias[coeff];
						if (coeff > 4)
						{
							coeff2 += (int)gb.bits(coeff <= 9 ? coeff - 4 : 11);
						}
						ct = 1 + (coeff2 > 1);
						const int sign = (int)gb.bit();
						coeff2 = (coeff2 ^ -sign) + sign;
						if (coeffIdx)
						{
							coeff2 *= dequantAc;
						}
						const int idx = model.coeffIndexToPos[coeffIdx];
						blockCoeff[b][permute[idx]] = (std::int16_t)coeff2;
					}
				}
				coeffIdx += run;
				if (coeffIdx >= 64)
				{
					break;
				}
				const int cg = std::min<int>(vp6_coeff_groups[coeffIdx], 3);
				vlcCoeff = &ractVlc[pt][ct][cg];
			}
			idctSelector[b] = model.coeffIndexToIdctSelector[std::min(coeffIdx, 63)];
		}
		return true;
	}

	bool parseCoeff()
	{
		// vp6_parse_coeff
		if (huffmanCoeffs)
		{
			return parseCoeffHuffman();
		}
		RangeCoder *rc = ccp;
		const std::uint8_t *permute = idctScantable;
		int pt = 0;
		if (rc->isEnd())
		{
			return false;
		}
		for (int b = 0; b < 6; b++)
		{
			int ct = 1;
			int run = 1;
			if (b > 3)
			{
				pt = 1;
			}
			const int ctx = leftBlock[vp56_b6to4[b]].notNullDc + aboveBlocks[aboveBlockIdx[b]].notNullDc;
			const std::uint8_t *model1 = model.coeffDccv[pt];
			const std::uint8_t *model2 = model.coeffDcct[pt][ctx];
			int coeffIdx = 0;
			for (;;)
			{
				if ((coeffIdx > 1 && ct == 0) || rc->getProb(model2[0]))
				{
					// a coefficient
					int coeff;
					if (rc->getProb(model2[2]))
					{
						if (rc->getProb(model2[3]))
						{
							const int idx = rc->getTree(vp56_pc_tree, model1);
							coeff = vp56_coeff_bias[idx + 5];
							for (int i = vp56_coeff_bit_length[idx]; i >= 0; i--)
							{
								coeff += rc->getProb(vp56_coeff_parse_table[idx][i]) << i;
							}
						}
						else
						{
							coeff = rc->getProb(model2[4]) ? 3 + rc->getProb(model1[5]) : 2;
						}
						ct = 2;
					}
					else
					{
						ct = 1;
						coeff = 1;
					}
					const int sign = rc->get();
					coeff = (coeff ^ -sign) + sign;
					if (coeffIdx)
					{
						coeff *= dequantAc;
					}
					const int idx = model.coeffIndexToPos[coeffIdx];
					blockCoeff[b][permute[idx]] = (std::int16_t)coeff;
					run = 1;
				}
				else
				{
					// a run
					ct = 0;
					if (coeffIdx > 0)
					{
						if (!rc->getProb(model2[1]))
						{
							break;
						}
						const std::uint8_t *model3 = model.coeffRunv[coeffIdx >= 6];
						run = rc->getTree(vp6_pcr_tree, model3);
						if (!run)
						{
							run = 9;
							for (int i = 0; i < 6; i++)
							{
								run += rc->getProb(model3[i + 8]) << i;
							}
						}
					}
				}
				coeffIdx += run;
				if (coeffIdx >= 64)
				{
					break;
				}
				const int cg = vp6_coeff_groups[coeffIdx];
				model1 = model2 = model.coeffRact[pt][ct][cg];
			}
			leftBlock[vp56_b6to4[b]].notNullDc = aboveBlocks[aboveBlockIdx[b]].notNullDc = blockCoeff[b][0] != 0;
			idctSelector[b] = model.coeffIndexToIdctSelector[std::min(coeffIdx, 63)];
		}
		return true;
	}

	// ---- vp56.c -------------------------------------------------------------------------------------------------------------------------------------

	int getVectorsPredictors(int row, int col, int refFrame)
	{
		// vp56_get_vectors_predictors
		int nbPred = 0;
		Mv vect[2];
		for (int pos = 0; pos < 12; pos++)
		{
			const int x = col + vp56_candidate_predictor_pos[pos][0];
			const int y = row + vp56_candidate_predictor_pos[pos][1];
			if (x < 0 || x >= mbWidth || y < 0 || y >= mbHeight)
			{
				continue;
			}
			const Macroblock &mb = macroblocks[(size_t)(x + mbWidth * y)];
			if (vp56_reference_frame[mb.type] != refFrame)
			{
				continue;
			}
			if ((mb.mv.x == vect[0].x && mb.mv.y == vect[0].y) || (mb.mv.x == 0 && mb.mv.y == 0))
			{
				continue;
			}
			vect[nbPred++] = mb.mv;
			if (nbPred > 1)
			{
				nbPred = -1;
				break;
			}
			vectorCandidatePos = pos;
		}
		vectorCandidate[0] = vect[0];
		vectorCandidate[1] = vect[1];
		return nbPred + 1;
	}

	void parseMbTypeModels()
	{
		// vp56_parse_mb_type_models
		for (int ctx = 0; ctx < 3; ctx++)
		{
			if (c.getProb(174))
			{
				const int idx = c.gets(4);
				std::memcpy(model.mbTypesStats[ctx], vp56_pre_def_mb_type_stats[idx][ctx], sizeof model.mbTypesStats[ctx]);
			}
			if (c.getProb(254))
			{
				for (int type = 0; type < 10; type++)
				{
					for (int i = 0; i < 2; i++)
					{
						if (c.getProb(205))
						{
							const int sign = c.get();
							int delta = c.getTree(vp56_pmbtm_tree, vp56_mb_type_model_model);
							if (!delta)
							{
								delta = 4 * c.gets(7);
							}
							model.mbTypesStats[ctx][type][i] = (std::uint8_t)(model.mbTypesStats[ctx][type][i] + ((delta ^ -sign) + sign));
						}
					}
				}
			}
		}
		// the MB type probabilities from the previous MB type
		for (int ctx = 0; ctx < 3; ctx++)
		{
			int p[10];
			for (int type = 0; type < 10; type++)
			{
				p[type] = 100 * model.mbTypesStats[ctx][type][1];
			}
			for (int type = 0; type < 10; type++)
			{
				const int s0 = model.mbTypesStats[ctx][type][0], s1 = model.mbTypesStats[ctx][type][1];
				model.mbType[ctx][type][0] = (std::uint8_t)(255 - (255 * s0) / (1 + s0 + s1));
				p[type] = 0; // same MB type => weight is null
				const int p02 = p[0] + p[2], p34 = p[3] + p[4], p0234 = p02 + p34, p17 = p[1] + p[7], p56 = p[5] + p[6], p89 = p[8] + p[9];
				const int p5689 = p56 + p89, p156789 = p17 + p5689;
				model.mbType[ctx][type][1] = (std::uint8_t)(1 + 255 * p0234 / (1 + p0234 + p156789));
				model.mbType[ctx][type][2] = (std::uint8_t)(1 + 255 * p02 / (1 + p0234));
				model.mbType[ctx][type][3] = (std::uint8_t)(1 + 255 * p17 / (1 + p156789));
				model.mbType[ctx][type][4] = (std::uint8_t)(1 + 255 * p[0] / (1 + p02));
				model.mbType[ctx][type][5] = (std::uint8_t)(1 + 255 * p[3] / (1 + p34));
				model.mbType[ctx][type][6] = (std::uint8_t)(1 + 255 * p[1] / (1 + p17));
				model.mbType[ctx][type][7] = (std::uint8_t)(1 + 255 * p56 / (1 + p5689));
				model.mbType[ctx][type][8] = (std::uint8_t)(1 + 255 * p[5] / (1 + p56));
				model.mbType[ctx][type][9] = (std::uint8_t)(1 + 255 * p[8] / (1 + p89));
				p[type] = 100 * s1; // restore initial value
			}
		}
	}

	int parseMbType(int prevType, int ctx)
	{
		// vp56_parse_mb_type
		const std::uint8_t *m = model.mbType[ctx][prevType];
		if (c.getProb(m[0]))
		{
			return prevType;
		}
		return c.getTree(vp56_pmbt_tree, m);
	}

	void decode4mv(int row, int col)
	{
		// vp56_decode_4mv
		Mv sum;
		int type[4];
		for (int b = 0; b < 4; b++)
		{
			type[b] = c.gets(2);
			if (type[b])
			{
				type[b]++; // only 0, 2, 3 or 4 (all INTER_PF)
			}
		}
		for (int b = 0; b < 4; b++)
		{
			switch (type[b])
			{
			case MB_INTER_NOVEC_PF:
				mv[b] = Mv();
				break;
			case MB_INTER_DELTA_PF:
				parseVectorAdjustment(mv[b]);
				break;
			case MB_INTER_V1_PF:
				mv[b] = vectorCandidate[0];
				break;
			case MB_INTER_V2_PF:
				mv[b] = vectorCandidate[1];
				break;
			}
			sum.x = (std::int16_t)(sum.x + mv[b].x);
			sum.y = (std::int16_t)(sum.y + mv[b].y);
		}
		macroblocks[(size_t)(row * mbWidth + col)].mv = mv[3];
		// chroma vectors are average luma vectors
		mv[4].x = mv[5].x = (std::int16_t)rshift2(sum.x);
		mv[4].y = mv[5].y = (std::int16_t)rshift2(sum.y);
	}

	int decodeMv(int row, int col)
	{
		// vp56_decode_mv
		Mv vect;
		const Mv *v = &vect;
		const int ctx = getVectorsPredictors(row, col, FRAME_PREVIOUS);
		mbType = parseMbType(mbType, ctx);
		macroblocks[(size_t)(row * mbWidth + col)].type = (std::uint8_t)mbType;
		switch (mbType)
		{
		case MB_INTER_V1_PF:
			v = &vectorCandidate[0];
			break;
		case MB_INTER_V2_PF:
			v = &vectorCandidate[1];
			break;
		case MB_INTER_V1_GF:
			getVectorsPredictors(row, col, FRAME_GOLDEN);
			v = &vectorCandidate[0];
			break;
		case MB_INTER_V2_GF:
			getVectorsPredictors(row, col, FRAME_GOLDEN);
			v = &vectorCandidate[1];
			break;
		case MB_INTER_DELTA_PF:
			parseVectorAdjustment(vect);
			break;
		case MB_INTER_DELTA_GF:
			getVectorsPredictors(row, col, FRAME_GOLDEN);
			parseVectorAdjustment(vect);
			break;
		case MB_INTER_4V:
			decode4mv(row, col);
			return mbType;
		default:
			break;
		}
		const Mv chosen = *v;
		macroblocks[(size_t)(row * mbWidth + col)].mv = chosen;
		for (int b = 0; b < 6; b++)
		{
			mv[b] = chosen; // same vector for all blocks
		}
		return mbType;
	}

	void addPredictorsDc(int refFrame)
	{
		// vp56_add_predictors_dc (VP6: the left and above blocks only)
		const int idx = idctScantable[0];
		for (int b = 0; b < 6; b++)
		{
			RefDc &ab = aboveBlocks[(size_t)aboveBlockIdx[b]];
			RefDc &lb = leftBlock[vp56_b6to4[b]];
			int count = 0;
			int dc = 0;
			if (refFrame == lb.refFrame)
			{
				dc += lb.dcCoeff;
				count++;
			}
			if (refFrame == ab.refFrame)
			{
				dc += ab.dcCoeff;
				count++;
			}
			if (count == 0)
			{
				dc = prevDc[vp56_b2p[b]][refFrame];
			}
			else if (count == 2)
			{
				dc /= 2;
			}
			blockCoeff[b][idx] = (std::int16_t)(blockCoeff[b][idx] + dc);
			prevDc[vp56_b2p[b]][refFrame] = blockCoeff[b][idx];
			ab.dcCoeff = blockCoeff[b][idx];
			ab.refFrame = refFrame;
			lb.dcCoeff = blockCoeff[b][idx];
			lb.refFrame = refFrame;
			blockCoeff[b][idx] = (std::int16_t)(blockCoeff[b][idx] * dequantDc);
		}
	}

	void filter(std::uint8_t *dst, std::uint8_t *src, int offset1, int offset2, std::ptrdiff_t strideBytes, Mv v, int mask, int select, bool luma)
	{
		// vp6_filter (flip = +1)
		int filter4 = 0;
		int x8 = v.x & mask;
		int y8 = v.y & mask;
		if (luma)
		{
			x8 *= 2;
			y8 *= 2;
			filter4 = filterMode;
			if (filter4 == 2)
			{
				if (maxVectorLength && (std::abs((int)v.x) > maxVectorLength || std::abs((int)v.y) > maxVectorLength))
				{
					filter4 = 0;
				}
				else if (sampleVarianceThreshold && blockVariance(src + offset1, strideBytes) < sampleVarianceThreshold)
				{
					filter4 = 0;
				}
			}
		}
		if ((y8 && (offset2 - offset1) < 0) || (!y8 && offset1 > offset2))
		{
			offset1 = offset2;
		}
		const int diagShift = ((int)v.x ^ (int)v.y) >> 31;
		if (filter4)
		{
			if (!y8)
			{
				filterHv4(dst, src + offset1, strideBytes, 1, vp6_block_copy_filter[select][x8]);
			}
			else if (!x8)
			{
				filterHv4(dst, src + offset1, strideBytes, strideBytes, vp6_block_copy_filter[select][y8]);
			}
			else
			{
				filterDiag4(dst, src + offset1 + diagShift, strideBytes, vp6_block_copy_filter[select][x8], vp6_block_copy_filter[select][y8]);
			}
		}
		else
		{
			if (!x8 || !y8)
			{
				chromaMc8(dst, src + offset1, strideBytes, 8, x8, y8);
			}
			else
			{
				filterDiag2(dst, src + offset1 + diagShift, strideBytes, x8, y8);
			}
		}
	}

	void filterDiag2(std::uint8_t *dst, const std::uint8_t *src, std::ptrdiff_t stride, int hWeight, int vWeight)
	{
		// vp6_filter_diag2: put_h264_chroma_pixels_tab[0](tmp, src, stride, 9, h_weight, 0), then (dst, tmp, stride, 8, 0, v_weight)
		const size_t need = (size_t)(stride * 10 + 16);
		if (diagTmp.size() < need)
		{
			diagTmp.resize(need);
		}
		chromaMc8(diagTmp.data(), src, stride, 9, hWeight, 0);
		chromaMc8(dst, diagTmp.data(), stride, 8, 0, vWeight);
	}
	std::vector<std::uint8_t> diagTmp;

	void mc(int b, int plane, const std::uint8_t *ref, std::ptrdiff_t strideBytes, int x, int y)
	{
		// vp56_mc: the 12 x 12 source window around the block (edge pixels repeated outside the plane, as emulated_edge_mc; inside it the same
		// pixels FFmpeg reads in place), deblocked when the frame says so, then the block copy filter
		std::uint8_t *dst = frames[FRAME_CURRENT].plane[plane].data() + blockOffset[b];
		const int coordDiv = vp6_coord_div[b];
		const int mask = coordDiv - 1;
		const int dx = mv[b].x / coordDiv;
		const int dy = mv[b].y / coordDiv;
		if (b >= 4)
		{
			x /= 2;
			y /= 2;
		}
		x += dx - 2;
		y += dy - 2;
		// the window in a buffer of the plane's stride (the filters address rows by the stride)
		const size_t need = (size_t)(strideBytes * 13 + 16);
		if (window.size() < need)
		{
			window.assign(need, 0);
		}
		const int pw = planeWidth[plane], ph = planeHeight[plane];
		for (int j = 0; j < 12; ++j)
		{
			const int sy = clipInt(y + j, 0, ph - 1);
			for (int i = 0; i < 12; ++i)
			{
				const int sx = clipInt(x + i, 0, pw - 1);
				window[(size_t)(j * strideBytes + i)] = ref[(size_t)sy * (size_t)strideBytes + (size_t)sx];
			}
		}
		std::uint8_t *srcBlock = window.data();
		const int srcOffset = 2 + 2 * (int)strideBytes;
		if (deblockFiltering)
		{
			// vp56_deblock_filter (VP6: the VP3 loop filter at the block edge inside the window)
			const int *bv = boundingValues + 127;
			const int ex = dx & 7, ey = dy & 7;
			if (ex)
			{
				hLoopFilter12(srcBlock + 10 - ex, strideBytes, bv);
			}
			if (ey)
			{
				vLoopFilter12(srcBlock + strideBytes * (10 - ey), strideBytes, bv);
			}
		}
		int overlapOffset = 0;
		if (mv[b].x & mask)
		{
			overlapOffset += mv[b].x > 0 ? 1 : -1;
		}
		if (mv[b].y & mask)
		{
			overlapOffset += mv[b].y > 0 ? (int)strideBytes : -(int)strideBytes;
		}
		if (overlapOffset)
		{
			filter(dst, srcBlock, srcOffset, srcOffset + overlapOffset, strideBytes, mv[b], mask, filterSelection, b < 4);
		}
		else
		{
			for (int j = 0; j < 8; ++j)
			{
				std::memcpy(dst + j * strideBytes, srcBlock + srcOffset + j * strideBytes, 8);
			}
		}
	}
	std::vector<std::uint8_t> window;

	void idctPut(std::uint8_t *dest, std::ptrdiff_t s, std::int16_t *block, int selector)
	{
		if (selector > 10 || selector == 1)
		{
			idct(dest, s, block, 1);
		}
		else
		{
			idct10(dest, s, block, 1);
		}
	}

	void idctAdd(std::uint8_t *dest, std::ptrdiff_t s, std::int16_t *block, int selector)
	{
		if (selector > 10)
		{
			idct(dest, s, block, 2);
		}
		else if (selector > 1)
		{
			idct10(dest, s, block, 2);
		}
		else
		{
			idctDcAdd(dest, s, block);
		}
	}

	void renderMb(int row, int col, int type)
	{
		// vp56_render_mb (no alpha plane: the alpha of the "_with_alpha" movies is its own stream)
		const int refFrame = vp56_reference_frame[type];
		addPredictorsDc(refFrame);
		Frame &cur = frames[FRAME_CURRENT];
		Frame &ref = frames[refFrame];
		if (type != MB_INTRA && !ref.valid)
		{
			return;
		}
		switch (type)
		{
		case MB_INTRA:
			for (int b = 0; b < 6; b++)
			{
				const int plane = vp56_b2p[b];
				idctPut(cur.plane[plane].data() + blockOffset[b], stride(plane), blockCoeff[b], idctSelector[b]);
			}
			break;
		case MB_INTER_NOVEC_PF:
		case MB_INTER_NOVEC_GF:
			for (int b = 0; b < 6; b++)
			{
				const int plane = vp56_b2p[b];
				const int off = blockOffset[b];
				const std::ptrdiff_t s = stride(plane);
				for (int j = 0; j < 8; ++j)
				{
					std::memcpy(cur.plane[plane].data() + off + j * s, ref.plane[plane].data() + off + j * s, 8);
				}
				idctAdd(cur.plane[plane].data() + off, s, blockCoeff[b], idctSelector[b]);
			}
			break;
		default:
			for (int b = 0; b < 6; b++)
			{
				const int xOff = b == 1 || b == 3 ? 8 : 0;
				const int yOff = b == 2 || b == 3 ? 8 : 0;
				const int plane = vp56_b2p[b];
				mc(b, plane, ref.plane[plane].data(), stride(plane), 16 * col + xOff, 16 * row + yOff);
				idctAdd(cur.plane[plane].data() + blockOffset[b], stride(plane), blockCoeff[b], idctSelector[b]);
			}
			break;
		}
	}

	void sizeChanged(int cols, int rows)
	{
		// vp56_size_changed (the references are dropped: the frame is a key frame)
		planeWidth[0] = 16 * cols;
		planeHeight[0] = 16 * rows;
		planeWidth[1] = planeWidth[2] = planeWidth[0] / 2;
		planeHeight[1] = planeHeight[2] = planeHeight[0] / 2;
		mbWidth = cols;
		mbHeight = rows;
		aboveBlocks.assign((size_t)(4 * mbWidth + 6), RefDc());
		macroblocks.assign((size_t)(mbWidth * mbHeight), Macroblock());
		for (Frame &f : frames)
		{
			f.valid = false;
			for (int p = 0; p < 3; ++p)
			{
				f.plane[p].assign((size_t)planeWidth[p] * (size_t)planeHeight[p], 0);
			}
		}
	}

	bool decodeMbs(std::string *error)
	{
		// ff_vp56_decode_mbs (flip = +1: block 0 on top)
		if (key)
		{
			defaultModelsInit();
			for (Macroblock &mb : macroblocks)
			{
				mb.type = MB_INTRA;
			}
		}
		else
		{
			parseMbTypeModels();
			parseVectorModels();
			mbType = MB_INTER_NOVEC_PF;
		}
		parseCoeffModels();
		std::memset(prevDc, 0, sizeof prevDc);
		prevDc[1][FRAME_CURRENT] = 128;
		prevDc[2][FRAME_CURRENT] = 128;
		for (RefDc &r : aboveBlocks)
		{
			r = RefDc();
		}
		aboveBlocks[(size_t)(2 * mbWidth + 2)].refFrame = FRAME_CURRENT;
		aboveBlocks[(size_t)(3 * mbWidth + 4)].refFrame = FRAME_CURRENT;
		const int strideY = (int)stride(0), strideUv = (int)stride(1);
		for (int mbRow = 0; mbRow < mbHeight; mbRow++)
		{
			for (RefDc &l : leftBlock)
			{
				l = RefDc();
			}
			aboveBlockIdx[0] = 1;
			aboveBlockIdx[1] = 2;
			aboveBlockIdx[2] = 1;
			aboveBlockIdx[3] = 2;
			aboveBlockIdx[4] = 2 * mbWidth + 2 + 1;
			aboveBlockIdx[5] = 3 * mbWidth + 4 + 1;
			blockOffset[0] = mbRow * 16 * strideY;
			blockOffset[2] = blockOffset[0] + 8 * strideY;
			blockOffset[1] = blockOffset[0] + 8;
			blockOffset[3] = blockOffset[2] + 8;
			blockOffset[4] = mbRow * 8 * strideUv;
			blockOffset[5] = blockOffset[4];
			for (int mbCol = 0; mbCol < mbWidth; mbCol++)
			{
				// vp56_decode_mb
				const int type = key ? (int)MB_INTRA : decodeMv(mbRow, mbCol);
				if (!parseCoeff())
				{
					// FFmpeg discards a damaged frame unless it can conceal it (error concealment needs an earlier undamaged frame); no retail movie
					// has one: an error
					*error = "VP6 coefficients damaged at macroblock " + std::to_string(mbCol) + ", " + std::to_string(mbRow);
					return false;
				}
				renderMb(mbRow, mbCol, type);
				for (int yb = 0; yb < 4; yb++)
				{
					aboveBlockIdx[yb] += 2;
					blockOffset[yb] += 16;
				}
				for (int uv = 4; uv < 6; uv++)
				{
					aboveBlockIdx[uv] += 1;
					blockOffset[uv] += 8;
				}
			}
		}
		return true;
	}
};

VP6Decoder::VP6Decoder() : m(new Impl()) {}

VP6Decoder::~VP6Decoder() { delete m; }

bool VP6Decoder::decodeFrame(const std::uint8_t *data, size_t size, std::string *error)
{
	std::string scratch;
	if (!error)
	{
		error = &scratch;
	}
	if (size < 3)
	{
		*error = "VP6 frame of " + std::to_string(size) + " bytes";
		return false;
	}
	// FFmpeg's packets carry 64 zero bytes of padding (AV_INPUT_BUFFER_PADDING_SIZE): the range decoder may read up to two bytes past the end
	m->packet.assign(data, data + size);
	m->packet.resize(size + 64, 0);
	int cols = 0, rows = 0;
	const int res = m->parseHeader(m->packet.data(), (int)size, error, cols, rows);
	if (res < 0)
	{
		return false;
	}
	if (res == kSizeChange)
	{
		m->sizeChanged(cols, rows);
		m_width = m->planeWidth[0];
		m_height = m->planeHeight[0];
	}
	if (!m->decodeMbs(error))
	{
		return false;
	}
	// the golden frame (a key frame or a frame flagged golden), then the current frame becomes the previous one
	Frame &cur = m->frames[FRAME_CURRENT];
	cur.valid = true;
	if (m->key || m->goldenFrame)
	{
		m->frames[FRAME_GOLDEN] = cur;
	}
	std::swap(m->frames[FRAME_CURRENT], m->frames[FRAME_PREVIOUS]);
	m->frames[FRAME_CURRENT].valid = false;
	m_hasFrame = true;
	m_key = m->key;
	return true;
}

void VP6Decoder::copyPlanes(std::vector<std::uint8_t> &y, std::vector<std::uint8_t> &u, std::vector<std::uint8_t> &v) const
{
	std::vector<std::uint8_t> *out[3] = { &y, &u, &v };
	const Frame &f = m->frames[FRAME_PREVIOUS]; // the frame just decoded
	for (int p = 0; p < 3; ++p)
	{
		const int w = m->planeWidth[p], h = m->planeHeight[p];
		out[p]->resize((size_t)w * (size_t)h);
		for (int row = 0; row < h; ++row)
		{
			std::memcpy(out[p]->data() + (size_t)row * (size_t)w, f.plane[p].data() + (size_t)(h - 1 - row) * (size_t)w, (size_t)w);
		}
	}
}

std::uint8_t VP6Decoder::lumaAt(int x, int y) const
{
	const Frame &f = m->frames[FRAME_PREVIOUS];
	const int w = m->planeWidth[0], h = m->planeHeight[0];
	if (x < 0 || y < 0 || x >= w || y >= h || f.plane[0].empty())
	{
		return 0;
	}
	return f.plane[0][(size_t)(h - 1 - y) * (size_t)w + (size_t)x];
}

void VP6Decoder::toRGBA(int width, int height, std::uint8_t *rgba, const VP6Decoder *alpha) const
{
	// display orientation (the bitstream's rows bottom up); BT.601 limited range in 16.16 fixed point
	const Frame &f = m->frames[FRAME_PREVIOUS];
	const int w = m->planeWidth[0], h = m->planeHeight[0];
	const int cw = m->planeWidth[1];
	const Frame *af = alpha ? &alpha->m->frames[FRAME_PREVIOUS] : nullptr;
	const int aw = alpha ? alpha->m->planeWidth[0] : 0, ah = alpha ? alpha->m->planeHeight[0] : 0;
	for (int y = 0; y < height; ++y)
	{
		const int sy = h - 1 - std::min(y, h - 1);
		const std::uint8_t *Y = f.plane[0].data() + (size_t)sy * (size_t)w;
		const std::uint8_t *U = f.plane[1].data() + (size_t)(sy / 2) * (size_t)cw;
		const std::uint8_t *V = f.plane[2].data() + (size_t)(sy / 2) * (size_t)cw;
		const std::uint8_t *A = af && !af->plane[0].empty() ? af->plane[0].data() + (size_t)(ah - 1 - std::min(y, ah - 1)) * (size_t)aw : nullptr;
		std::uint8_t *o = rgba + (size_t)y * (size_t)width * 4;
		for (int x = 0; x < width; ++x)
		{
			const int sx = std::min(x, w - 1);
			const int yy = (Y[sx] - 16) * 76309;
			const int u = U[sx / 2] - 128, v = V[sx / 2] - 128;
			o[0] = clipU8((yy + 104597 * v + 32768) >> 16);
			o[1] = clipU8((yy - 25675 * u - 53279 * v + 32768) >> 16);
			o[2] = clipU8((yy + 132201 * u + 32768) >> 16);
			o[3] = A ? A[std::min(x, aw - 1)] : 255;
			o += 4;
		}
	}
}

// ---- EA's container --------------------------------------------------------------------------------------------------------------------------------

namespace
{
std::uint32_t le32(const std::uint8_t *p) { return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); }
}

namespace
{
// the chunk walk of both parsers: `read(offset, n, buffer)` reads n bytes at offset (false past the end)
template <class Read>
bool parseChunks(size_t size, Read read, EAVP6Movie &out, std::string *error)
{
	out = EAVP6Movie();
	size_t off = 0;
	bool header = false;
	std::uint8_t head[8 + 24];
	while (off + 8 <= size)
	{
		if (!read(off, 8, head))
		{
			*error = "read error at " + std::to_string(off);
			return false;
		}
		const std::string tag((const char *)head, 4);
		const std::uint32_t chunk = le32(head + 4);
		if (chunk < 8 || off + chunk > size)
		{
			*error = "chunk " + tag + " at " + std::to_string(off) + " has the size " + std::to_string(chunk) + " (file " + std::to_string(size) + " bytes)";
			return false;
		}
		const std::uint8_t *p = head + 8;
		if ((tag == "MVhd" || tag == "AVhd") && chunk >= 8 + 24 && !read(off + 8, 24, head + 8))
		{
			*error = "read error at " + std::to_string(off);
			return false;
		}
		if (tag == "MVhd" || tag == "AVhd")
		{
			// the codec tag, u16 width, u16 height, u32 frames, u32 largest chunk, u32 rate numerator, u32 rate denominator
			if (chunk < 8 + 24)
			{
				*error = tag + " is too short";
				return false;
			}
			if (tag == "MVhd")
			{
				out.codec.assign((const char *)p, 4);
				out.width = (std::uint32_t)p[4] | ((std::uint32_t)p[5] << 8);
				out.height = (std::uint32_t)p[6] | ((std::uint32_t)p[7] << 8);
				out.frames = le32(p + 8);
				out.largestChunk = le32(p + 12);
				out.rateNumerator = le32(p + 16);
				out.rateDenominator = le32(p + 20);
				header = true;
			}
			else
			{
				out.hasAlpha = true;
			}
			if (p[0] != 'v' && p[0] != 'V')
			{
				*error = tag + " codec '" + std::string((const char *)p, 4) + "' is not VP6";
				return false;
			}
		}
		else if (tag == "AVP6")
		{
			out.hasAlpha = true; // the alpha movies' leading chunk (its payload was not read: S-2340)
		}
		else if (tag == "MV0K" || tag == "MV0F")
		{
			out.video.push_back({ (std::uint32_t)(off + 8), chunk - 8, tag == "MV0K" });
		}
		else if (tag == "AV0K" || tag == "AV0F")
		{
			out.alpha.push_back({ (std::uint32_t)(off + 8), chunk - 8, tag == "AV0K" });
		}
		else
		{
			*error = "unknown chunk '" + tag + "' at " + std::to_string(off);
			return false;
		}
		off += chunk;
	}
	if (!header)
	{
		*error = "no MVhd chunk";
		return false;
	}
	if (out.width == 0 || out.height == 0 || out.width > EAVP6Movie::kMaxDimension || out.height > EAVP6Movie::kMaxDimension)
	{
		// lane CAMP-2 (review): a size the codec cannot have is an error before anything is allocated for the picture
		*error = "the MVhd picture size " + std::to_string(out.width) + " x " + std::to_string(out.height) + " is outside 1 .. " +
			std::to_string(EAVP6Movie::kMaxDimension);
		return false;
	}
	if (out.rateNumerator == 0 || out.rateDenominator == 0)
	{
		*error = "the MVhd frame rate is zero";
		return false;
	}
	if (out.video.empty())
	{
		*error = "no video frames";
		return false;
	}
	return true;
}

} // namespace

bool EAVP6Movie::parse(const std::uint8_t *data, size_t size, EAVP6Movie &out, std::string *error)
{
	return parseChunks(
		size,
		[&](size_t off, size_t n, std::uint8_t *buf) {
			if (off + n > size)
			{
				return false;
			}
			std::memcpy(buf, data + off, n);
			return true;
		},
		out, error);
}

bool EAVP6Movie::parseFile(const std::string &path, EAVP6Movie &out, std::string *error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		*error = "cannot open " + path;
		return false;
	}
	f.seekg(0, std::ios::end);
	const size_t size = (size_t)f.tellg();
	return parseChunks(
		size,
		[&](size_t off, size_t n, std::uint8_t *buf) {
			f.seekg((std::streamoff)off);
			f.read((char *)buf, (std::streamsize)n);
			return (size_t)f.gcount() == n;
		},
		out, error);
}

double EAVP6Movie::durationMs() const { return (double)frames * 1000.0 * (double)rateDenominator / (double)rateNumerator; }
