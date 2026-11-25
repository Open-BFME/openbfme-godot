// OpenBFME. GPL-3.0.
//
// Image::load_dds_from_buffer wrapper for DDS files that Godot's loader refuses but retail's D3D takes (a mip chain shorter than the header says,
// partial mip chains, non-zero dwPitchOrLinearSize without the matching flag, or block-compressed sizes not a multiple of 4). Those are decoded
// from their top level (GameClient/DdsDecode.h) and get a generated mip chain; every other file goes to Godot's loader as before, and a file
// neither can read is the caller's error.

#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace godot
{
Error loadDdsFromBuffer(const Ref<Image> &image, const PackedByteArray &buffer);
}
