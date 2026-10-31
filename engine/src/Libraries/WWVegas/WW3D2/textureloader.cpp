// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.

#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include "Common/AsciiString.h"

namespace
{
void addCandidates(const std::string &lowerName, const std::function<bool(const std::string &)> &exists, TextureResolution &out, bool flatLayout)
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
	if (flatLayout)
	{
		// BFME1 (docs/BFME1.md) has no compiled texture folders: its Textures.big holds art\textures\<stem>.dds for the .tga a model names,
		// ZH's lookup (the extension swapped for dds, then the name as written)
		candidates = { "art\\textures\\" + stem + ".dds", "art\\textures\\" + stem + "." + ext,
			"art\\textures\\" + stem + ".jpg", "art\\textures\\" + stem + ".png" };
	}
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

TextureResolution Resolve_W3D_Texture(const std::string &name, const std::function<bool(const std::string &)> &exists, bool flatLayout)
{
	TextureResolution out;
	std::string lower = AsciiStringUtil::lowered(name);
	addCandidates(lower, exists, out, flatLayout);
	return out;
}

TextureResolution Resolve_Packed_W3D_Texture(const std::string &name, const std::function<bool(const std::string &)> &exists, bool flatLayout)
{
	auto result = Resolve_W3D_Texture(name, exists, flatLayout);
	if (result.Found || flatLayout) return result;
	std::string stem = AsciiStringUtil::lowered(name);
	const auto dot = stem.find_last_of('.');
	if (dot != std::string::npos) stem.resize(dot);
	if (stem.empty()) return result;
	const auto path = "art\\compiledtextures\\" + stem.substr(0, 2) + "\\" + stem + ".jpg";
	result.Tried.push_back(path);
	if (exists(path))
	{
		result.Found = true;
		result.Path = path;
		result.IsDDS = false;
		result.TerrainFolderPath.clear();
	}
	return result;
}
