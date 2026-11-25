// OpenBFME. GPL-3.0.
// See GameClient/DdsDecode.h.

#include "GameClient/DdsDecode.h"

#include <algorithm>
#include <cstring>

namespace
{

constexpr std::uint32_t kFourCC_DXT1 = 0x31545844, kFourCC_DXT2 = 0x32545844, kFourCC_DXT3 = 0x33545844, kFourCC_DXT4 = 0x34545844,
                        kFourCC_DXT5 = 0x35545844, kFourCC_DX10 = 0x30315844;
constexpr std::uint32_t kPfAlphaPixels = 0x1, kPfFourCC = 0x4, kPfRgb = 0x40;
constexpr std::uint32_t kFlagPitch = 0x8, kFlagMipCount = 0x20000, kFlagLinearSize = 0x80000;
constexpr std::uint32_t kCaps2Cube = 0x200, kCaps2Volume = 0x200000;
constexpr size_t kHeaderSize = 128;

std::uint32_t rd32(const std::uint8_t *p)
{
	return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

struct Header
{
	int width = 0;
	int height = 0;
	int mips = 1;
	std::uint32_t flags = 0;
	std::uint32_t pitchOrLinearSize = 0;
	bool block = false;
	size_t blockBytes = 0;
	std::uint32_t fourCC = 0;
	int bitCount = 0;
	std::uint32_t masks[4] = {}; // r g b a
	bool hasAlphaMask = false;
};

bool parse(const std::uint8_t *data, size_t size, Header &h, std::string *error)
{
	auto fail = [&](const std::string &msg) {
		if (error)
		{
			*error = msg;
		}
		return false;
	};
	if (size < kHeaderSize || std::memcmp(data, "DDS ", 4) != 0 || rd32(data + 4) != 124)
	{
		return fail("DDS: not a DDS header");
	}
	const std::uint32_t flags = rd32(data + 8);
	h.flags = flags;
	h.height = (int)rd32(data + 12);
	h.width = (int)rd32(data + 16);
	h.pitchOrLinearSize = rd32(data + 20);
	h.mips = (flags & kFlagMipCount) && rd32(data + 28) > 0 ? (int)rd32(data + 28) : 1;
	const std::uint32_t pfFlags = rd32(data + 80), caps2 = rd32(data + 112);
	if (caps2 & (kCaps2Cube | kCaps2Volume))
	{
		return fail("DDS: cube maps and volume textures are not read");
	}
	if (h.width <= 0 || h.height <= 0 || h.width > 16384 || h.height > 16384)
	{
		return fail("DDS: dimensions " + std::to_string(h.width) + "x" + std::to_string(h.height) + " out of range");
	}
	if (pfFlags & kPfFourCC)
	{
		h.fourCC = rd32(data + 84);
		switch (h.fourCC)
		{
			case kFourCC_DXT1: h.blockBytes = 8; break;
			case kFourCC_DXT2:
			case kFourCC_DXT3:
			case kFourCC_DXT4:
			case kFourCC_DXT5: h.blockBytes = 16; break;
			case kFourCC_DX10: return fail("DDS: DX10 header is not read");
			default:
			{
				const std::string name((const char *)(data + 84), 4);
				return fail("DDS: pixel format '" + name + "' is not read");
			}
		}
		h.block = true;
		return true;
	}
	if (!(pfFlags & kPfRgb))
	{
		return fail("DDS: pixel format flags 0x" + std::to_string(pfFlags) + " (only DXT1 .. DXT5 and RGB(A) are read)");
	}
	h.bitCount = (int)rd32(data + 88);
	if (h.bitCount != 16 && h.bitCount != 24 && h.bitCount != 32)
	{
		return fail("DDS: " + std::to_string(h.bitCount) + "-bit RGB is not read");
	}
	for (int i = 0; i < 4; ++i)
	{
		h.masks[i] = rd32(data + 92 + 4 * i);
	}
	h.hasAlphaMask = (pfFlags & kPfAlphaPixels) != 0 && h.masks[3] != 0;
	return true;
}

size_t levelBytes(const Header &h, int level)
{
	const int w = std::max(1, h.width >> level), hh = std::max(1, h.height >> level);
	if (h.block)
	{
		return (size_t)((w + 3) / 4) * (size_t)((hh + 3) / 4) * h.blockBytes;
	}
	return (size_t)w * (size_t)hh * (size_t)(h.bitCount / 8);
}

void rgb565(std::uint16_t c, std::uint8_t out[3])
{
	const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	out[0] = (std::uint8_t)((r << 3) | (r >> 2));
	out[1] = (std::uint8_t)((g << 2) | (g >> 4));
	out[2] = (std::uint8_t)((b << 3) | (b >> 2));
}

// the 4x4 colour block (DXT1's 8 bytes) into 16 RGBA pixels; `punchThrough` is DXT1's transparent fourth colour
void colourBlock(const std::uint8_t *b, std::uint8_t px[16][4], bool punchThrough)
{
	const std::uint16_t c0 = (std::uint16_t)(b[0] | (b[1] << 8)), c1 = (std::uint16_t)(b[2] | (b[3] << 8));
	std::uint8_t pal[4][4];
	rgb565(c0, pal[0]);
	rgb565(c1, pal[1]);
	pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
	if (c0 > c1 || !punchThrough)
	{
		for (int k = 0; k < 3; ++k)
		{
			pal[2][k] = (std::uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
			pal[3][k] = (std::uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
		}
	}
	else
	{
		for (int k = 0; k < 3; ++k)
		{
			pal[2][k] = (std::uint8_t)((pal[0][k] + pal[1][k]) / 2);
			pal[3][k] = 0;
		}
		pal[3][3] = 0;
	}
	const std::uint32_t idx = rd32(b + 4);
	for (int i = 0; i < 16; ++i)
	{
		std::memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
	}
}

int shiftOf(std::uint32_t mask)
{
	int s = 0;
	while (mask && !(mask & 1))
	{
		mask >>= 1;
		++s;
	}
	return s;
}

int bitsOf(std::uint32_t mask)
{
	mask >>= shiftOf(mask);
	int n = 0;
	while (mask & 1)
	{
		mask >>= 1;
		++n;
	}
	return n;
}

std::uint8_t channel(std::uint32_t value, std::uint32_t mask)
{
	if (!mask)
	{
		return 255;
	}
	const int bits = bitsOf(mask);
	const std::uint32_t v = (value & mask) >> shiftOf(mask);
	return (std::uint8_t)(bits >= 8 ? (v >> (bits - 8)) : (v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1));
}

} // namespace

bool DdsDecode::inspect(const std::uint8_t *data, size_t size, bool &needsFallback, std::string *error)
{
	Header h;
	needsFallback = false;
	if (!parse(data, size, h, error))
	{
		return false;
	}
	if (h.block && ((h.width & 3) || (h.height & 3)))
	{
		needsFallback = true;
		return true;
	}
	// Godot 4.7 (modules/dds/texture_loader_dds.cpp) rejects DDS files whose dwPitchOrLinearSize is non-zero without
	// DDSD_LINEARSIZE (compressed) or DDSD_PITCH (uncompressed), whereas retail's D3D loader accepts them.
	if ((h.block && h.pitchOrLinearSize != 0 && !(h.flags & kFlagLinearSize)) ||
	    (!h.block && h.pitchOrLinearSize != 0 && !(h.flags & kFlagPitch)))
	{
		needsFallback = true;
		return true;
	}
	size_t chain = 0;
	for (int i = 0; i < h.mips; ++i)
	{
		chain += levelBytes(h, i);
	}
	// Godot's loader builds the image as "with mipmaps" and then expects the full chain down to 1 x 1, whatever mip count the header declares: a file that
	// declares and holds fewer levels is refused, whereas retail's D3D loader takes it.
	int fullChain = 1;
	for (int m = std::max(h.width, h.height); m > 1; m >>= 1)
	{
		++fullChain;
	}
	needsFallback = size < kHeaderSize + chain || (h.mips > 1 && h.mips < fullChain);
	return true;
}

bool DdsDecode::decodeLevel0(const std::uint8_t *data, size_t size, DdsImage &out, std::string *error)
{
	out = DdsImage();
	Header h;
	if (!parse(data, size, h, error))
	{
		return false;
	}
	const size_t need = levelBytes(h, 0);
	if (size < kHeaderSize + need)
	{
		if (error)
		{
			*error = "DDS: the top level is cut (" + std::to_string(size - kHeaderSize) + " of " + std::to_string(need) + " bytes)";
		}
		return false;
	}
	const std::uint8_t *src = data + kHeaderSize;
	out.width = h.width;
	out.height = h.height;
	out.rgba.assign((size_t)h.width * (size_t)h.height * 4, 0);
	if (h.block)
	{
		const int bw = (h.width + 3) / 4, bh = (h.height + 3) / 4;
		for (int by = 0; by < bh; ++by)
		{
			for (int bx = 0; bx < bw; ++bx)
			{
				const std::uint8_t *b = src + ((size_t)by * bw + bx) * h.blockBytes;
				std::uint8_t px[16][4];
				std::uint8_t alpha[16];
				if (h.fourCC == kFourCC_DXT1)
				{
					colourBlock(b, px, true);
					for (int i = 0; i < 16; ++i)
					{
						alpha[i] = px[i][3];
					}
				}
				else
				{
					colourBlock(b + 8, px, false);
					if (h.fourCC == kFourCC_DXT2 || h.fourCC == kFourCC_DXT3)
					{
						for (int i = 0; i < 16; ++i)
						{
							const int a = (b[i / 2] >> (4 * (i & 1))) & 15;
							alpha[i] = (std::uint8_t)(a * 17);
						}
					}
					else
					{
						std::uint8_t a[8] = { b[0], b[1] };
						if (a[0] > a[1])
						{
							for (int k = 1; k <= 6; ++k)
							{
								a[1 + k] = (std::uint8_t)(((7 - k) * a[0] + k * a[1]) / 7);
							}
						}
						else
						{
							for (int k = 1; k <= 4; ++k)
							{
								a[1 + k] = (std::uint8_t)(((5 - k) * a[0] + k * a[1]) / 5);
							}
							a[6] = 0;
							a[7] = 255;
						}
						std::uint64_t bits = 0;
						for (int k = 0; k < 6; ++k)
						{
							bits |= (std::uint64_t)b[2 + k] << (8 * k);
						}
						for (int i = 0; i < 16; ++i)
						{
							alpha[i] = a[(bits >> (3 * i)) & 7];
						}
					}
				}
				for (int i = 0; i < 16; ++i)
				{
					const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
					if (x < h.width && y < h.height)
					{
						std::uint8_t *d = out.rgba.data() + ((size_t)y * h.width + x) * 4;
						d[0] = px[i][0];
						d[1] = px[i][1];
						d[2] = px[i][2];
						d[3] = alpha[i];
					}
				}
			}
		}
		return true;
	}
	const size_t bytes = (size_t)h.bitCount / 8;
	for (size_t i = 0; i < (size_t)h.width * (size_t)h.height; ++i)
	{
		std::uint32_t v = 0;
		for (size_t k = 0; k < bytes; ++k)
		{
			v |= (std::uint32_t)src[i * bytes + k] << (8 * k);
		}
		std::uint8_t *d = out.rgba.data() + i * 4;
		d[0] = channel(v, h.masks[0]);
		d[1] = channel(v, h.masks[1]);
		d[2] = channel(v, h.masks[2]);
		d[3] = h.hasAlphaMask ? channel(v, h.masks[3]) : 255;
	}
	return true;
}
