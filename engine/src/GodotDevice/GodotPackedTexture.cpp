// OpenBFME. GPL-3.0.
// See GodotPackedTexture.h.

#include "GodotDevice/GodotPackedTexture.h"

#include "Libraries/Source/Apt/AptLoad.h"

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstring>
#include <vector>

namespace godot
{
// lane UI-2: a texture the install packs under another extension, in RotWK's order (RW 0x530D29: the requested name's stem with .dds, else .tga,
// else .jpg, and with a .jpg also <stem>.png as a second stream). The .tga is the caller's (AptTextureStore). INFERENCE [S-1482]: the .png is the
// .jpg's alpha (its colour is white in the corpus: ScrollShroud.png is white RGBA with the corner alpha) - its alpha replaces the picture's, its
// colour multiplies it; how the loader merges the two streams was not read.
Ref<godot::Image> loadPackedTexture(AptFileSource &source, const std::string &stem, bool *pairedPng)
{
	*pairedPng = false;
	auto read = [&](const std::string &path, PackedByteArray &out) {
		std::vector<std::uint8_t> bytes;
		std::string err;
		if (!source.readFile(path, bytes, &err))
		{
			return false;
		}
		out.resize((int64_t)bytes.size());
		memcpy(out.ptrw(), bytes.data(), bytes.size());
		return true;
	};
	PackedByteArray data;
	Ref<godot::Image> image;
	image.instantiate();
	if (read(stem + ".dds", data))
	{
		// BFME1 also ships TGA bytes under DDS filenames (PalantirA).
		static const unsigned char pngSignature[] = {137,80,78,71,13,10,26,10};
		Error error;
		if (data.size() >= 8 && std::memcmp(data.ptr(), pngSignature, 8) == 0)
			error = image->load_png_from_buffer(data);
		else if (data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
			error = image->load_jpg_from_buffer(data);
		else if (data.size() >= 4 && std::memcmp(data.ptr(), "DDS ", 4) == 0)
			error = image->load_dds_from_buffer(data);
		else
			error = image->load_tga_from_buffer(data);
		return error == OK ? image : Ref<godot::Image>();
	}
	if (!read(stem + ".jpg", data) || image->load_jpg_from_buffer(data) != OK)
	{
		return Ref<godot::Image>();
	}
	image->convert(godot::Image::FORMAT_RGBA8);
	PackedByteArray pngData;
	Ref<godot::Image> png;
	png.instantiate();
	if (read(stem + ".png", pngData) && png->load_png_from_buffer(pngData) == OK)
	{
		png->convert(godot::Image::FORMAT_RGBA8);
		*pairedPng = true;
		if (png->get_width() != image->get_width() || png->get_height() != image->get_height())
		{
			png->resize(image->get_width(), image->get_height(), godot::Image::INTERPOLATE_NEAREST);
		}
		for (int y = 0; y < image->get_height(); ++y)
		{
			for (int x = 0; x < image->get_width(); ++x)
			{
				const Color c = image->get_pixel(x, y), a = png->get_pixel(x, y);
				image->set_pixel(x, y, Color(c.r * a.r, c.g * a.g, c.b * a.b, a.a));
			}
		}
	}
	return image;
}
} // namespace godot
