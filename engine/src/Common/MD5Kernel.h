// OpenBFME. GPL-3.0.
// RFC 1321 MD5 compression. transformReference is the scalar loop.
// transformFast is the same operations and the same order, with each of the
// 64 steps written out so the four round functions are not data-dependent
// branches. Integer arithmetic only: the digest bits match the scalar loop.

#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#define MD5K_ALWAYS_INLINE __forceinline
#else
#define MD5K_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

namespace md5k
{
inline std::uint32_t rotl(std::uint32_t x, unsigned c)
{
	return (x << c) | (x >> (32u - c));
}

inline void loadBlock(std::uint32_t M[16], const std::uint8_t block[64])
{
	for (int i = 0; i < 16; ++i)
	{
		const std::uint8_t *p = block + i * 4;
		M[i] = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) |
			((std::uint32_t)p[3] << 24);
	}
}

MD5K_ALWAYS_INLINE void transformReference(std::uint32_t state[4], const std::uint8_t block[64])
{
	static const std::uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
	};
	static const unsigned S[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
	};
	std::uint32_t M[16];
	loadBlock(M, block);
	std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	for (unsigned i = 0; i < 64; ++i)
	{
		std::uint32_t f;
		unsigned g;
		if (i < 16) { f = (b & c) | (~b & d); g = i; }
		else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
		else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
		else { f = c ^ (b | ~d); g = (7 * i) % 16; }
		const std::uint32_t tmp = d;
		d = c;
		c = b;
		b = b + rotl(a + f + K[i] + M[g], S[i]);
		a = tmp;
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5STEP(f, w, x, y, z, data, s) \
	do \
	{ \
		(w) += f((x), (y), (z)) + (data); \
		(w) = rotl((w), (s)); \
		(w) += (x); \
	} while (0)

MD5K_ALWAYS_INLINE void transformFast(std::uint32_t state[4], const std::uint8_t block[64])
{
	std::uint32_t M[16];
	loadBlock(M, block);
	std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	MD5STEP(F, a, b, c, d, M[0] + 0xd76aa478u, 7);
	MD5STEP(F, d, a, b, c, M[1] + 0xe8c7b756u, 12);
	MD5STEP(F, c, d, a, b, M[2] + 0x242070dbu, 17);
	MD5STEP(F, b, c, d, a, M[3] + 0xc1bdceeeu, 22);
	MD5STEP(F, a, b, c, d, M[4] + 0xf57c0fafu, 7);
	MD5STEP(F, d, a, b, c, M[5] + 0x4787c62au, 12);
	MD5STEP(F, c, d, a, b, M[6] + 0xa8304613u, 17);
	MD5STEP(F, b, c, d, a, M[7] + 0xfd469501u, 22);
	MD5STEP(F, a, b, c, d, M[8] + 0x698098d8u, 7);
	MD5STEP(F, d, a, b, c, M[9] + 0x8b44f7afu, 12);
	MD5STEP(F, c, d, a, b, M[10] + 0xffff5bb1u, 17);
	MD5STEP(F, b, c, d, a, M[11] + 0x895cd7beu, 22);
	MD5STEP(F, a, b, c, d, M[12] + 0x6b901122u, 7);
	MD5STEP(F, d, a, b, c, M[13] + 0xfd987193u, 12);
	MD5STEP(F, c, d, a, b, M[14] + 0xa679438eu, 17);
	MD5STEP(F, b, c, d, a, M[15] + 0x49b40821u, 22);
	MD5STEP(G, a, b, c, d, M[1] + 0xf61e2562u, 5);
	MD5STEP(G, d, a, b, c, M[6] + 0xc040b340u, 9);
	MD5STEP(G, c, d, a, b, M[11] + 0x265e5a51u, 14);
	MD5STEP(G, b, c, d, a, M[0] + 0xe9b6c7aau, 20);
	MD5STEP(G, a, b, c, d, M[5] + 0xd62f105du, 5);
	MD5STEP(G, d, a, b, c, M[10] + 0x02441453u, 9);
	MD5STEP(G, c, d, a, b, M[15] + 0xd8a1e681u, 14);
	MD5STEP(G, b, c, d, a, M[4] + 0xe7d3fbc8u, 20);
	MD5STEP(G, a, b, c, d, M[9] + 0x21e1cde6u, 5);
	MD5STEP(G, d, a, b, c, M[14] + 0xc33707d6u, 9);
	MD5STEP(G, c, d, a, b, M[3] + 0xf4d50d87u, 14);
	MD5STEP(G, b, c, d, a, M[8] + 0x455a14edu, 20);
	MD5STEP(G, a, b, c, d, M[13] + 0xa9e3e905u, 5);
	MD5STEP(G, d, a, b, c, M[2] + 0xfcefa3f8u, 9);
	MD5STEP(G, c, d, a, b, M[7] + 0x676f02d9u, 14);
	MD5STEP(G, b, c, d, a, M[12] + 0x8d2a4c8au, 20);
	MD5STEP(H, a, b, c, d, M[5] + 0xfffa3942u, 4);
	MD5STEP(H, d, a, b, c, M[8] + 0x8771f681u, 11);
	MD5STEP(H, c, d, a, b, M[11] + 0x6d9d6122u, 16);
	MD5STEP(H, b, c, d, a, M[14] + 0xfde5380cu, 23);
	MD5STEP(H, a, b, c, d, M[1] + 0xa4beea44u, 4);
	MD5STEP(H, d, a, b, c, M[4] + 0x4bdecfa9u, 11);
	MD5STEP(H, c, d, a, b, M[7] + 0xf6bb4b60u, 16);
	MD5STEP(H, b, c, d, a, M[10] + 0xbebfbc70u, 23);
	MD5STEP(H, a, b, c, d, M[13] + 0x289b7ec6u, 4);
	MD5STEP(H, d, a, b, c, M[0] + 0xeaa127fau, 11);
	MD5STEP(H, c, d, a, b, M[3] + 0xd4ef3085u, 16);
	MD5STEP(H, b, c, d, a, M[6] + 0x04881d05u, 23);
	MD5STEP(H, a, b, c, d, M[9] + 0xd9d4d039u, 4);
	MD5STEP(H, d, a, b, c, M[12] + 0xe6db99e5u, 11);
	MD5STEP(H, c, d, a, b, M[15] + 0x1fa27cf8u, 16);
	MD5STEP(H, b, c, d, a, M[2] + 0xc4ac5665u, 23);
	MD5STEP(I, a, b, c, d, M[0] + 0xf4292244u, 6);
	MD5STEP(I, d, a, b, c, M[7] + 0x432aff97u, 10);
	MD5STEP(I, c, d, a, b, M[14] + 0xab9423a7u, 15);
	MD5STEP(I, b, c, d, a, M[5] + 0xfc93a039u, 21);
	MD5STEP(I, a, b, c, d, M[12] + 0x655b59c3u, 6);
	MD5STEP(I, d, a, b, c, M[3] + 0x8f0ccc92u, 10);
	MD5STEP(I, c, d, a, b, M[10] + 0xffeff47du, 15);
	MD5STEP(I, b, c, d, a, M[1] + 0x85845dd1u, 21);
	MD5STEP(I, a, b, c, d, M[8] + 0x6fa87e4fu, 6);
	MD5STEP(I, d, a, b, c, M[15] + 0xfe2ce6e0u, 10);
	MD5STEP(I, c, d, a, b, M[6] + 0xa3014314u, 15);
	MD5STEP(I, b, c, d, a, M[13] + 0x4e0811a1u, 21);
	MD5STEP(I, a, b, c, d, M[4] + 0xf7537e82u, 6);
	MD5STEP(I, d, a, b, c, M[11] + 0xbd3af235u, 10);
	MD5STEP(I, c, d, a, b, M[2] + 0x2ad7d2bbu, 15);
	MD5STEP(I, b, c, d, a, M[9] + 0xeb86d391u, 21);
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

#undef MD5STEP
#undef I
#undef H
#undef G
#undef F
#undef MD5K_ALWAYS_INLINE
}
