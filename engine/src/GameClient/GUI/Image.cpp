// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/Image.h.

#include "GameClient/GUI/Image.h"

#include <cctype>
#include <cstddef>

namespace
{

std::string lowerKey(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return out;
}

// ZH Image::parseImageCoords
void parseImageCoords(INI *ini, void *instance, void *, const void *)
{
	const int left = ini->scanInt(ini->getNextSubToken("Left"));
	const int top = ini->scanInt(ini->getNextSubToken("Top"));
	const int right = ini->scanInt(ini->getNextSubToken("Right"));
	const int bottom = ini->scanInt(ini->getNextSubToken("Bottom"));
	Image *image = static_cast<Image *>(instance);
	float lx = (float)left, ly = (float)top, hx = (float)right, hy = (float)bottom;
	if (image->textureSize.x)
	{
		lx /= (float)image->textureSize.x;
		hx /= (float)image->textureSize.x;
	}
	if (image->textureSize.y)
	{
		ly /= (float)image->textureSize.y;
		hy /= (float)image->textureSize.y;
	}
	image->uvLo[0] = lx;
	image->uvLo[1] = ly;
	image->uvHi[0] = hx;
	image->uvHi[1] = hy;
	image->imageSize.x = right - left;
	image->imageSize.y = bottom - top;
}

// ZH Image::parseImageStatus: bit string over imageStatusNames; a 90-degree rotated image swaps its size
void parseImageStatus(INI *ini, void *instance, void *, const void *)
{
	Image *image = static_cast<Image *>(instance);
	unsigned bits = 0;
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		const std::string t = lowerKey(token);
		if (t == "none")
		{
			continue;
		}
		if (t == "rotated_90_clockwise")
		{
			bits |= IMAGE_STATUS_ROTATED_90_CLOCKWISE;
		}
		else if (t == "raw_texture")
		{
			bits |= IMAGE_STATUS_RAW_TEXTURE;
		}
		else
		{
			throw INIException(3, "Image status '%s' is not a known status", token);
		}
	}
	image->status = bits;
	if (bits & IMAGE_STATUS_ROTATED_90_CLOCKWISE)
	{
		std::swap(image->imageSize.x, image->imageSize.y);
	}
}

} // namespace

const FieldParse *Image::fieldParseTable()
{
	static const FieldParse table[] = {
		{ "Texture", INI::parseAsciiString, nullptr, (int)offsetof(Image, filename) },
		{ "TextureWidth", INI::parseInt, nullptr, (int)(offsetof(Image, textureSize) + offsetof(ICoord2D, x)) },
		{ "TextureHeight", INI::parseInt, nullptr, (int)(offsetof(Image, textureSize) + offsetof(ICoord2D, y)) },
		{ "Coords", parseImageCoords, nullptr, 0 },
		{ "Status", parseImageStatus, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

void MappedImageCollection::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("MappedImage", [this](INI *ini) { parseDefinition(ini); });
}

void MappedImageCollection::parseDefinition(INI *ini)
{
	// INI::parseMappedImageDefinition: the image is found by name or created, then its fields are read in place
	const std::string name = ini->getNextToken();
	Image &image = m_images[lowerKey(name)];
	if (image.name.empty())
	{
		image.name = name;
	}
	ini->initFromINI(&image, Image::fieldParseTable());
}

const Image *MappedImageCollection::findImageByName(const std::string &name) const
{
	auto it = m_images.find(lowerKey(name));
	return it == m_images.end() ? nullptr : &it->second;
}

void MappedImageCollection::addImage(const Image &image)
{
	m_images[lowerKey(image.name)] = image;
}
