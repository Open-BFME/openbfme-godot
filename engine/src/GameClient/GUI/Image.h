// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Mapped images (ZH GameClient/Image.h, System/Image.cpp ImageCollection): `MappedImage <name>` blocks of data\ini\mappedimages\**.ini
// give a rectangle of a texture; the gadgets read the image size for their layout and the renderer draws the rectangle.  The Coords
// line is `Left:n Top:n Right:n Bottom:n` (pixels of the texture; the UV is the pixel box over the texture size).
//
// Target facts: RotWK 2.01 ships 51 MappedImage files; the APT gadgets' skins live in aptimages\aptcomponents.ini (e.g.
// AptListBoxHiliteSelectedItem, AptCheckboxCheckedEnabled).  Donor: the field table and parseImageCoords / parseImageStatus of ZH.

#pragma once

#include "Common/INI.h"

#include <map>
#include <string>
#include <vector>

enum ImageStatus
{
	IMAGE_STATUS_NONE = 0x00000000,
	IMAGE_STATUS_ROTATED_90_CLOCKWISE = 0x00000001,
	IMAGE_STATUS_RAW_TEXTURE = 0x00000002
};

struct Image
{
	std::string name;
	std::string filename;  // the texture file
	ICoord2D textureSize;
	float uvLo[2] = { 0, 0 };
	float uvHi[2] = { 1, 1 };
	ICoord2D imageSize;
	unsigned status = IMAGE_STATUS_NONE;

	int getImageWidth() const { return imageSize.x; }
	int getImageHeight() const { return imageSize.y; }
	// ZH Image::m_imageFieldParseTable
	static const FieldParse *fieldParseTable();
};

// The collection: images by lower-cased name (ZH keys by TheNameKeyGenerator->nameToLowercaseKey).
class MappedImageCollection
{
public:
	// Registers the `MappedImage` block in an INI environment (INI::parseMappedImageDefinition).
	void registerBlocks(INIBlockRegistry &registry);
	const Image *findImageByName(const std::string &name) const;
	std::size_t size() const { return m_images.size(); }
	// Adds an image directly (tests, mods without INI).
	void addImage(const Image &image);

private:
	void parseDefinition(INI *ini);
	std::map<std::string, Image> m_images;
};
