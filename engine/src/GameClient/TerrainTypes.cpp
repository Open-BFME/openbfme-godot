// OpenBFME. GPL-3.0.
// See GameClient/TerrainTypes.h.

#include "GameClient/TerrainTypes.h"

#include <cctype>

namespace
{

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

} // namespace

const std::string *TerrainTypeIndex::findTexture(const std::string &terrainName) const
{
	auto it = textureByName.find(lower(terrainName));
	return it == textureByName.end() ? nullptr : &it->second;
}

std::string TerrainTypeIndex::textureArchivePath(const std::string &texture)
{
	return "art\\terrain\\" + lower(texture);
}

std::string TerrainTypeIndex::normalMapName(const std::string &texture)
{
	size_t dot = texture.find_last_of('.');
	if (dot == std::string::npos)
	{
		return texture + "_nrm";
	}
	return texture.substr(0, dot) + "_nrm" + texture.substr(dot);
}

void TerrainTypes::scanText(const std::string &text, TerrainTypeIndex &out)
{
	size_t pos = 0;
	bool inBlock = false;
	std::string cur, tex;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.erase(semi);
		}
		line = trim(line);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			if (line.size() > 8 && lower(line.substr(0, 7)) == "terrain" && std::isspace((unsigned char)line[7]))
			{
				cur = trim(line.substr(7));
				size_t sp = cur.find_first_of(" \t");
				if (sp != std::string::npos)
				{
					cur.erase(sp);
				}
				tex.clear();
				inBlock = true;
			}
			continue;
		}
		if (lower(line) == "end")
		{
			if (!tex.empty())
			{
				std::string key = lower(cur);
				if (out.textureByName.count(key))
				{
					++out.duplicates;
				}
				out.textureByName[key] = tex;
			}
			++out.blocksScanned;
			inBlock = false;
			continue;
		}
		size_t eq = line.find('=');
		if (eq != std::string::npos && lower(trim(line.substr(0, eq))) == "texture")
		{
			tex = trim(line.substr(eq + 1));
		}
	}
}

bool TerrainTypes::load(ArchiveFileSystem &fs, TerrainTypeIndex &out, std::string *error)
{
	out = TerrainTypeIndex();
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\terrain.ini", bytes, error))
	{
		return false;
	}
	scanText(std::string(bytes.begin(), bytes.end()), out);
	if (out.textureByName.empty())
	{
		if (error)
		{
			*error = "data\\ini\\terrain.ini holds no Terrain blocks";
		}
		return false;
	}
	return true;
}
