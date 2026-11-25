// OpenBFME. GPL-3.0.
// See GodotDds.h.

#include "GodotDevice/GodotDds.h"

#include "GameClient/DdsDecode.h"

#include <cstring>
#include <string>

namespace godot
{
Error loadDdsFromBuffer(const Ref<Image> &image, const PackedByteArray &buffer)
{
	bool fallback = false;
	std::string why;
	// a header this decoder does not read (DX10, cube, volume) is Godot's to judge
	if (DdsDecode::inspect(buffer.ptr(), (size_t)buffer.size(), fallback, &why) && fallback)
	{
		DdsImage decoded;
		if (!DdsDecode::decodeLevel0(buffer.ptr(), (size_t)buffer.size(), decoded, &why))
		{
			return ERR_PARSE_ERROR;
		}
		PackedByteArray rgba;
		rgba.resize((int64_t)decoded.rgba.size());
		std::memcpy(rgba.ptrw(), decoded.rgba.data(), decoded.rgba.size());
		image->set_data(decoded.width, decoded.height, false, Image::FORMAT_RGBA8, rgba);
		image->generate_mipmaps();
		return OK;
	}
	const Error err = image->load_dds_from_buffer(buffer);
	if (err != OK)
	{
		DdsImage decoded;
		if (DdsDecode::decodeLevel0(buffer.ptr(), (size_t)buffer.size(), decoded, &why))
		{
			PackedByteArray rgba;
			rgba.resize((int64_t)decoded.rgba.size());
			std::memcpy(rgba.ptrw(), decoded.rgba.data(), decoded.rgba.size());
			image->set_data(decoded.width, decoded.height, false, Image::FORMAT_RGBA8, rgba);
			image->generate_mipmaps();
			return OK;
		}
	}
	return err;
}
}
