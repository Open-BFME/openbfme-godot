// OpenBFME. GPL-3.0.
// See GameClient/GUI/Skirmish/IniSkirmishSetupSource.h.

#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace
{
std::string trim(const std::string &s)
{
	std::size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
	{
		++a;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}
// INI comments: `;` and `//` start a comment (the retail files use both)
std::string stripComment(const std::string &line)
{
	std::size_t cut = line.size();
	const std::size_t semi = line.find(';');
	if (semi != std::string::npos)
	{
		cut = std::min(cut, semi);
	}
	const std::size_t slashes = line.find("//");
	if (slashes != std::string::npos)
	{
		cut = std::min(cut, slashes);
	}
	return trim(line.substr(0, cut));
}
std::string lowerAscii(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}
bool parseYesNo(const std::string &v, bool &out)
{
	const std::string l = lowerAscii(v);
	if (l == "yes" || l == "1" || l == "true")
	{
		out = true;
		return true;
	}
	if (l == "no" || l == "0" || l == "false")
	{
		out = false;
		return true;
	}
	return false;
}
// "R:43 G:150 B:179" (INI::parseRGBColor; an optional "A:" is accepted by the retail parse and ignored here)
bool parseRGB(const std::string &v, std::uint32_t &out)
{
	int r = -1, g = -1, b = -1;
	if (std::sscanf(v.c_str(), " R:%d G:%d B:%d", &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
	{
		return false;
	}
	out = ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
	return true;
}
bool parseIntValue(const std::string &v, int &out)
{
	char *end = nullptr;
	const long n = std::strtol(v.c_str(), &end, 10);
	if (end == v.c_str() || *end != '\0')
	{
		return false;
	}
	out = (int)n;
	return true;
}
// Calls `line(lineNumber, text)` for every non-empty line without comments.
template <typename Fn>
void eachLine(const std::string &text, Fn line)
{
	std::size_t pos = 0;
	int number = 0;
	while (pos <= text.size())
	{
		std::size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		++number;
		const std::string s = stripComment(text.substr(pos, nl - pos));
		if (!s.empty())
		{
			line(number, s);
		}
		pos = nl + 1;
	}
}
bool splitKeyValue(const std::string &line, std::string &key, std::string &value)
{
	const std::size_t eq = line.find('=');
	if (eq == std::string::npos)
	{
		return false;
	}
	key = trim(line.substr(0, eq));
	value = trim(line.substr(eq + 1));
	return !key.empty();
}
std::string firstToken(const std::string &s)
{
	std::size_t i = 0;
	while (i < s.size() && !std::isspace((unsigned char)s[i]))
	{
		++i;
	}
	return s.substr(0, i);
}
std::string restAfterToken(const std::string &s)
{
	return trim(s.substr(firstToken(s).size()));
}
bool readFile(ArchiveFileSystem &fs, const std::string &path, std::string &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	if (!fs.readFile(path, bytes, &err))
	{
		if (error)
		{
			*error = path + ": " + err;
		}
		return false;
	}
	out.assign(bytes.begin(), bytes.end());
	return true;
}
std::string backslashes(std::string s)
{
	std::replace(s.begin(), s.end(), '/', '\\');
	return s;
}
} // namespace

bool IniSkirmishSetupSource::scanPlayerTemplates(const std::string &text, std::vector<SkirmishFaction> &out, std::string *error)
{
	out.clear();
	bool inBlock = false;
	bool ok = true;
	SkirmishFaction cur;
	eachLine(text, [&](int number, const std::string &line) {
		if (!ok)
		{
			return;
		}
		auto fail = [&](const std::string &why) {
			ok = false;
			if (error)
			{
				*error = "playertemplate.ini line " + std::to_string(number) + ": " + why;
			}
		};
		if (!inBlock)
		{
			if (lowerAscii(firstToken(line)) == "playertemplate")
			{
				cur = SkirmishFaction();
				cur.templateName = restAfterToken(line);
				if (cur.templateName.empty())
				{
					fail("a PlayerTemplate block without a name");
					return;
				}
				inBlock = true;
			}
			return; // other blocks of the file are not read
		}
		if (lowerAscii(line) == "end")
		{
			// a template that is defined again replaces the earlier one in place (RW 0x5FE852: an existing name is parsed into)
			bool replaced = false;
			for (SkirmishFaction &f : out)
			{
				if (f.templateName == cur.templateName)
				{
					f = cur;
					replaced = true;
					break;
				}
			}
			if (!replaced)
			{
				out.push_back(cur);
			}
			inBlock = false;
			return;
		}
		std::string key, value;
		if (!splitKeyValue(line, key, value))
		{
			fail("'" + line + "' is not a key = value line");
			return;
		}
		const std::string k = lowerAscii(key);
		if (k == "side")
		{
			cur.side = value;
		}
		else if (k == "playableside")
		{
			if (!parseYesNo(value, cur.playableSide))
			{
				fail("PlayableSide '" + value + "' is not a boolean");
			}
		}
		else if (k == "isobserver")
		{
			if (!parseYesNo(value, cur.isObserver))
			{
				fail("IsObserver '" + value + "' is not a boolean");
			}
		}
		else if (k == "startingbuilding")
		{
			cur.startingBuilding = (lowerAscii(value) == "none") ? std::string() : value;
		}
		else if (k == "displayname")
		{
			cur.displayLabel = value;
		}
		else if (k == "preferredcolor")
		{
			if (!parseRGB(value, cur.preferredColor))
			{
				fail("PreferredColor '" + value + "' is not R:n G:n B:n");
			}
		}
		else if (k == "buildableheroesmp")
		{
			cur.buildableHeroesMP.clear();
			std::string rest = value;
			while (!rest.empty())
			{
				const std::string token = firstToken(rest);
				cur.buildableHeroesMP.push_back(token);
				rest = restAfterToken(rest);
			}
		}
	});
	if (ok && inBlock)
	{
		ok = false;
		if (error)
		{
			*error = "playertemplate.ini: the block '" + cur.templateName + "' has no End";
		}
	}
	return ok;
}

bool IniSkirmishSetupSource::scanMultiplayer(const std::string &text, std::vector<SkirmishColor> &colors, std::vector<int> &cashChoices, std::string *error)
{
	colors.clear();
	cashChoices.clear();
	enum class Block
	{
		None,
		Settings,
		Color,
		Other
	} block = Block::None;
	bool ok = true;
	SkirmishColor cur;
	std::vector<std::pair<std::string, int>> credits;
	eachLine(text, [&](int number, const std::string &line) {
		if (!ok)
		{
			return;
		}
		auto fail = [&](const std::string &why) {
			ok = false;
			if (error)
			{
				*error = "multiplayer.ini line " + std::to_string(number) + ": " + why;
			}
		};
		if (block == Block::None)
		{
			const std::string word = lowerAscii(firstToken(line));
			if (word == "multiplayersettings")
			{
				block = Block::Settings;
			}
			else if (word == "multiplayercolor")
			{
				cur = SkirmishColor();
				cur.name = restAfterToken(line);
				if (cur.name.empty())
				{
					fail("a MultiplayerColor block without a name");
					return;
				}
				block = Block::Color;
			}
			else
			{
				block = Block::Other;
			}
			return;
		}
		if (lowerAscii(line) == "end")
		{
			if (block == Block::Color)
			{
				colors.push_back(cur);
			}
			block = Block::None;
			return;
		}
		std::string key, value;
		if (!splitKeyValue(line, key, value))
		{
			if (block != Block::Other)
			{
				fail("'" + line + "' is not a key = value line");
			}
			return;
		}
		const std::string k = lowerAscii(key);
		if (block == Block::Settings && k.compare(0, 14, "initialcredits") == 0)
		{
			int amount = 0;
			if (!parseIntValue(value, amount))
			{
				fail(key + " '" + value + "' is not an integer");
				return;
			}
			credits.emplace_back(key, amount);
		}
		else if (block == Block::Color)
		{
			if (k == "rgbcolor")
			{
				if (!parseRGB(value, cur.rgb))
				{
					fail("RGBColor '" + value + "' is not R:n G:n B:n");
				}
			}
			else if (k == "tooltipname")
			{
				cur.tooltipName = value;
			}
			else if (k == "availableinwotr")
			{
				if (!parseYesNo(value, cur.availableInWotR))
				{
					fail("AvailableInWotR '" + value + "' is not a boolean");
				}
			}
		}
	});
	if (ok && block != Block::None)
	{
		ok = false;
		if (error)
		{
			*error = "multiplayer.ini: a block has no End";
		}
	}
	if (ok)
	{
		for (const auto &c : credits)
		{
			cashChoices.push_back(c.second);
		}
		std::sort(cashChoices.begin(), cashChoices.end());
	}
	return ok;
}

bool IniSkirmishSetupSource::scanDefaultStartingCash(const std::string &text, int &out, std::string *error)
{
	bool found = false;
	bool ok = true;
	eachLine(text, [&](int number, const std::string &line) {
		std::string key, value;
		if (!ok || !splitKeyValue(line, key, value) || lowerAscii(key) != "defaultstartingcash")
		{
			return;
		}
		if (!parseIntValue(value, out))
		{
			ok = false;
			if (error)
			{
				*error = "gamedata.ini line " + std::to_string(number) + ": DefaultStartingCash '" + value + "' is not an integer";
			}
			return;
		}
		found = true; // the last assignment wins
	});
	if (ok && !found)
	{
		if (error)
		{
			*error = "gamedata.ini: no DefaultStartingCash";
		}
		return false;
	}
	return ok;
}

bool IniSkirmishSetupSource::load(ArchiveFileSystem &fs, std::string *error)
{
	m_fs = &fs;
	std::string text;
	if (!readFile(fs, "data\\ini\\playertemplate.ini", text, error) || !scanPlayerTemplates(text, m_factions, error))
	{
		return false;
	}
	if (!readFile(fs, "data\\ini\\multiplayer.ini", text, error) || !scanMultiplayer(text, m_colors, m_cashChoices, error))
	{
		return false;
	}
	if (!readFile(fs, "data\\ini\\gamedata.ini", text, error) || !scanDefaultStartingCash(text, m_defaultStartingCash, error))
	{
		return false;
	}
	return loadMapCache(fs, m_maps, error);
}

bool IniSkirmishSetupSource::loadMapCache(ArchiveFileSystem &fs, std::vector<MapCacheEntry> &out, std::string *error)
{
	std::string text;
	if (!readFile(fs, "maps\\mapcache.ini", text, error))
	{
		return false;
	}
	std::string mapError;
	if (!MapCache::parse(text, out, &mapError))
	{
		if (error)
		{
			*error = "maps\\mapcache.ini: " + mapError;
		}
		return false;
	}
	return true;
}

// ZH GameInfo::setMap (GameInfo.cpp:513-604) with GameNetwork/FileTransfer.cpp GetStrFileFromMap / GetSoloINIFromMap / GetAssetUsageFromMap /
// GetReadmeFromMap: the files next to the map.
int IniSkirmishSetupSource::mapContentsMask(const MapCacheEntry &map) const
{
	return mapContentsMaskIn(m_fs, map);
}

int IniSkirmishSetupSource::mapContentsMaskIn(ArchiveFileSystem *fsPtr, const MapCacheEntry &map)
{
	int mask = 1;
	if (!fsPtr)
	{
		return mask;
	}
	ArchiveFileSystem *m_fs = fsPtr;
	const std::string path = backslashes(map.name);
	const std::size_t slash = path.rfind('\\');
	const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash);
	std::string preview = path;
	if (preview.size() >= 3)
	{
		preview.replace(preview.size() - 3, 3, "tga");
	}
	const struct
	{
		int bit;
		std::string file;
	} probes[] = { { 2, preview }, { 4, dir + "\\map.ini" }, { 8, dir + "\\map.str" }, { 16, dir + "\\solo.ini" }, { 32, dir + "\\assetusage.txt" }, { 64, dir + "\\readme.txt" } };
	for (const auto &p : probes)
	{
		if (m_fs->doesFileExist(p.file))
		{
			mask |= p.bit;
		}
	}
	return mask;
}

bool IniSkirmishSetupSource::fileExists(const std::string &path) const
{
	return m_fs && m_fs->doesFileExist(path);
}
