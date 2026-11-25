// OpenBFME. GPL-3.0.
//
// Level 0 of a DDS file as RGBA8, for the files Godot's DDS loader refuses. Retail's D3D loader takes them: a mip chain shorter than the header's count,
// a partial mip chain that stops above 1 x 1, non-zero dwPitchOrLinearSize without DDSD_LINEARSIZE / DDSD_PITCH, and block-compressed sizes that are not
// a multiple of 4. Only the top level is read, so the chain's tail does not matter.
//   formats: DXT1, DXT2 / DXT3, DXT4 / DXT5 and uncompressed 16 / 24 / 32-bit with channel masks; anything else (a DX10 header, a volume or cube map,
//   a palette) is an error, never a silent default.
//   donor: the S3TC block layout (two RGB565 endpoints and 2-bit indices; DXT3 4-bit alpha; DXT5 two alpha endpoints and 3-bit indices).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DdsImage
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba; // width * height * 4
};

namespace DdsDecode
{
// true when the header is a 2D texture this decoder reads; `needsFallback` is then true when Godot's loader cannot take the file as it is:
// width or height not a multiple of 4 for a block format, non-zero pitch/linearSize without the matching header flag, a partial mip chain,
// or fewer bytes than the header's mip chain needs.
bool inspect(const std::uint8_t *data, size_t size, bool &needsFallback, std::string *error);
bool decodeLevel0(const std::uint8_t *data, size_t size, DdsImage &out, std::string *error);
}
