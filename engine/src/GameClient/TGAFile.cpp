// OpenBFME. GPL-3.0.
// See GameClient/TGAFile.h.

#include "GameClient/TGAFile.h"

#include <cstring>
#include <vector>

bool TGAFile::decode(const std::uint8_t *data, size_t size, TGAImage &out, std::string *error, int maxDimension, bool rle)
{
	auto fail = [&](const std::string &msg) {
		if (error)
		{
			*error = msg;
		}
		return false;
	};
	out = TGAImage();
	if (size < 18)
	{
		return fail("TGA: shorter than the 18-byte header");
	}
	const std::uint8_t idLength = data[0];
	const std::uint8_t colorMapType = data[1];
	const std::uint8_t imageType = data[2];
	const int width = data[12] | (data[13] << 8);
	const int height = data[14] | (data[15] << 8);
	const int depth = data[16];
	const std::uint8_t flags = data[17];
	if (colorMapType != 0)
	{
		return fail("TGA: colour-mapped images are not supported");
	}
	if (imageType != 2 && !(rle && imageType == 10))
	{
		return fail("TGA: image type " + std::to_string(imageType) + (rle ? " (true-colour types 2 and 10 are read)" : " (only uncompressed true-colour type 2 is read; the terrain reader rejects RLE like retail)"));
	}
	if (depth != 24 && depth != 32)
	{
		return fail("TGA: pixel depth " + std::to_string(depth) + " (24 or 32 expected)");
	}
	if (width <= 0 || height <= 0 || width > maxDimension || height > maxDimension)
	{
		return fail("TGA: dimensions " + std::to_string(width) + "x" + std::to_string(height) + " out of range");
	}
	const size_t bpp = (size_t)depth / 8;
	const size_t pixelsAt = 18 + (size_t)idLength; // colour map length is 0 (colorMapType 0)
	const size_t need = (size_t)width * (size_t)height * bpp;
	std::vector<std::uint8_t> unpacked;
	if (imageType == 10)
	{
		// ZH TARGA.CPP Targa::DecodeImage: packets until every pixel is filled; a packet may not run past the image (an error here)
		unpacked.reserve(need);
		size_t at = pixelsAt;
		while (unpacked.size() < need)
		{
			if (at >= size)
			{
				return fail("TGA: RLE data truncated (" + std::to_string(unpacked.size() / bpp) + " of " + std::to_string(need / bpp) + " pixels)");
			}
			const std::uint8_t packet = data[at++];
			const size_t count = (size_t)(packet & 0x7F) + 1;
			if (unpacked.size() + count * bpp > need)
			{
				return fail("TGA: an RLE packet runs past the image");
			}
			if (packet & 0x80)
			{
				if (at + bpp > size)
				{
					return fail("TGA: RLE data truncated");
				}
				for (size_t i = 0; i < count; ++i)
				{
					unpacked.insert(unpacked.end(), data + at, data + at + bpp);
				}
				at += bpp;
			}
			else
			{
				if (at + count * bpp > size)
				{
					return fail("TGA: RLE data truncated");
				}
				unpacked.insert(unpacked.end(), data + at, data + at + count * bpp);
				at += count * bpp;
			}
		}
	}
	else if (size < pixelsAt + need)
	{
		return fail("TGA: pixel data truncated (" + std::to_string(size - pixelsAt) + " of " + std::to_string(need) + " bytes)");
	}
	out.width = width;
	out.height = height;
	out.pixelDepth = depth;
	out.topDownFlag = (flags & 0x20) != 0;
	out.rgba.resize((size_t)width * (size_t)height * 4);
	const std::uint8_t *src = imageType == 10 ? unpacked.data() : data + pixelsAt;
	std::uint8_t *dst = out.rgba.data();
	for (size_t i = 0; i < (size_t)width * (size_t)height; ++i)
	{
		dst[0] = src[2]; // r
		dst[1] = src[1]; // g
		dst[2] = src[0]; // b
		dst[3] = bpp == 4 ? src[3] : 255;
		src += bpp;
		dst += 4;
	}
	return true;
}
