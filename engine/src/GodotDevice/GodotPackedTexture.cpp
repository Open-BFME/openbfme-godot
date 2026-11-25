// OpenBFME. GPL-3.0.
// See GodotPackedTexture.h.

#include "GodotDevice/GodotPackedTexture.h"
#include "GodotDevice/GodotDds.h"

#include "Libraries/Source/Apt/AptLoad.h"

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstring>
#include <vector>

namespace godot
{
namespace
{
// Both editions' presentation: the decoded streams are RGBA8. Exhaustive checks
// against Godot 4.7.2's C++ Image get_pixel/set_pixel operation establish that
// its float colour product stores floor(a*b/255), with the companion alpha
// replacing the colour alpha. Work on bytes to avoid three ABI calls per texel.
bool mergePackedRgba8(const Ref<Image> &image, const Ref<Image> &mask)
{
	PackedByteArray pixels = image->get_data();
	const PackedByteArray maskPixels = mask->get_data();
	const int64_t bytes = pixels.size();
	if (maskPixels.size() != bytes)
	{
		UtilityFunctions::push_error("Packed texture RGBA8 image and mask byte counts differ");
		return false;
	}
	uint8_t *out = pixels.ptrw();
	const uint8_t *in = maskPixels.ptr();
	for (int64_t i = 0; i < bytes; i += 4)
	{
		out[i] = (uint8_t)((unsigned(out[i]) * in[i]) / 255u);
		out[i + 1] = (uint8_t)((unsigned(out[i + 1]) * in[i + 1]) / 255u);
		out[i + 2] = (uint8_t)((unsigned(out[i + 2]) * in[i + 2]) / 255u);
		out[i + 3] = in[i + 3];
	}
	image->set_data(image->get_width(), image->get_height(), false, Image::FORMAT_RGBA8, pixels);
	return true;
}
} // namespace

// lane UI-2: a texture the install packs under another extension, in RotWK's order (RW 0x530D29: the requested name's stem with .dds, else .tga,
// else .jpg, and with a .jpg also <stem>.png as a second stream). The .tga is the caller's (AptTextureStore). INFERENCE [S-1482]: the .png is the
// .jpg's alpha (its colour is white in the corpus: ScrollShroud.png is white RGBA with the corner alpha) - its alpha replaces the picture's, its
// colour multiplies it; how the loader merges the two streams was not read.
Ref<godot::Image> loadPackedTexture(AptFileSource &source, const std::string &stem, bool *pairedPng, const PackedByteArray *preloadedJpg)
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
		return loadDdsFromBuffer(image, data) == OK ? image : Ref<godot::Image>();
	}
	if ((!preloadedJpg && !read(stem + ".jpg", data)) || image->load_jpg_from_buffer(preloadedJpg ? *preloadedJpg : data) != OK)
	{
		return Ref<godot::Image>();
	}
	image->convert(godot::Image::FORMAT_RGBA8);
	PackedByteArray pngData;
	Ref<godot::Image> png;
	png.instantiate();
	if (source.fileExists(stem + ".png"))
	{
		// A missing companion is valid; an unreadable or corrupt companion is
		// a failed texture, not permission to silently discard its alpha.
		if (!read(stem + ".png", pngData) || png->load_png_from_buffer(pngData) != OK)
			return Ref<godot::Image>();
		png->convert(godot::Image::FORMAT_RGBA8);
		if (png->get_width() != image->get_width() || png->get_height() != image->get_height())
		{
			png->resize(image->get_width(), image->get_height(), godot::Image::INTERPOLATE_NEAREST);
		}
		if (!mergePackedRgba8(image, png))
			return Ref<godot::Image>();
		*pairedPng = true;
	}
	return image;
}
} // namespace godot
