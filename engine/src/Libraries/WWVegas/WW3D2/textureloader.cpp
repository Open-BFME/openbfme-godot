// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.

#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include "Common/AsciiString.h"

namespace
{
void addCandidates(const std::string &lowerName, const std::function<bool(const std::string &)> &exists, TextureResolution &out)
{
	std::string stem = lowerName;
	std::string ext = "tga";
	size_t dot = stem.find_last_of('.');
	if (dot != std::string::npos)
	{
		ext = stem.substr(dot + 1);
		stem = stem.substr(0, dot);
	}
	if (stem.empty())
	{
		return;
	}
	const std::string folder = "art\\compiledtextures\\" + stem.substr(0, 2) + "\\";
	// ZH replaces the last three characters with "dds"; for the usual ".xxx" suffix that is the same as swapping the extension.
	std::vector<std::string> candidates = { folder + stem + ".dds", folder + stem + "." + ext, "art\\textures\\" + stem + "." + ext };
	for (const std::string &c : candidates)
	{
		out.Tried.push_back(c);
		if (!out.Found && exists(c))
		{
			out.Found = true;
			out.Path = c;
			out.IsDDS = c.size() > 4 && c.compare(c.size() - 4, 4, ".dds") == 0;
		}
	}
	if (!out.Found)
	{
		const std::string terrain = "art\\terrain\\" + stem + "." + ext;
		if (exists(terrain))
		{
			out.TerrainFolderPath = terrain;
		}
	}
}
} // namespace

TextureResolution Resolve_W3D_Texture(const std::string &name, const std::function<bool(const std::string &)> &exists)
{
	TextureResolution out;
	std::string lower = AsciiStringUtil::lowered(name);
	addCandidates(lower, exists, out);
	return out;
}
