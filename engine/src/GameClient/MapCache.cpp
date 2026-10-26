// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/MapCache.h.

#include "GameClient/MapCache.h"
#include "GameNetwork/GameInfo.h"
#include "Common/AsciiString.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace
{

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
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

// lane MODS-2 (Edain's mapcache.ini writes "End" and mixed-case keys): retail reads MapCache.ini with the generic INI reader (its block,
// field and value tables, the format strings "  PlayerPosition %d" / "  techPosition = ..." / "  supplyPosition = ..." / "  END" next to
// them), so the block name, END, every key and yes / no compare without case (INI::parseBool and the field lookup are stricmp)
bool eqNoCase(const std::string &a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

bool parseYesNo(const std::string &v, bool &out)
{
	if (eqNoCase(v, "yes"))
	{
		out = true;
		return true;
	}
	if (eqNoCase(v, "no"))
	{
		out = false;
		return true;
	}
	return false;
}

bool parseInt(const std::string &v, long long &out)
{
	if (v.empty())
	{
		return false;
	}
	char *end = nullptr;
	out = std::strtoll(v.c_str(), &end, 10);
	return end && *end == 0;
}

// "X:541.58 Y:297.66 Z:0.00"
bool parseCoord(const std::string &v, Coord3D &out)
{
	float x, y, z;
	char tail = 0;
	if (std::sscanf(v.c_str(), "X:%f Y:%f Z:%f%c", &x, &y, &z, &tail) != 3)
	{
		return false;
	}
	out.x = x;
	out.y = y;
	out.z = z;
	return true;
}

std::u16string toUtf16(const std::string &bytes)
{
	std::u16string s;
	for (size_t i = 0; i + 1 < bytes.size(); i += 2)
	{
		s.push_back((char16_t)((unsigned char)bytes[i] | ((unsigned char)bytes[i + 1] << 8)));
	}
	return s;
}

} // namespace

namespace MapCache
{

std::string unescape(const std::string &escaped)
{
	std::string out;
	for (size_t i = 0; i < escaped.size(); ++i)
	{
		if (escaped[i] == '_' && i + 2 < escaped.size() + 0 && std::isxdigit((unsigned char)escaped[i + 1])
			&& std::isxdigit((unsigned char)escaped[i + 2]))
		{
			char hex[3] = { escaped[i + 1], escaped[i + 2], 0 };
			out.push_back((char)std::strtol(hex, nullptr, 16));
			i += 2;
		}
		else
		{
			out.push_back(escaped[i]);
		}
	}
	return out;
}

bool parse(const std::string &text, std::vector<MapCacheEntry> &out, std::string *error, bool bfme1)
{
	out.clear();
	std::istringstream in(text);
	std::string raw;
	int lineNo = 0;
	MapCacheEntry *cur = nullptr;
	int playerPosition = 0;
	auto fail = [&](const std::string &msg) {
		if (error)
		{
			*error = "MapCache.ini line " + std::to_string(lineNo) + ": " + msg;
		}
		return false;
	};
	while (std::getline(in, raw))
	{
		++lineNo;
		std::string line = raw;
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
		if (!cur)
		{
			if (line.size() <= 9 || !eqNoCase(line.substr(0, 8), "MapCache") || !std::isspace((unsigned char)line[8]))
			{
				return fail("expected 'MapCache <name>', got '" + line + "'");
			}
			out.emplace_back();
			cur = &out.back();
			cur->rawName = trim(line.substr(8));
			std::string n = unescape(cur->rawName);
			for (char &c : n)
			{
				c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
			}
			cur->name = n;
			continue;
		}
		// community PR #36: BFME1's scenario slot blocks; lane MODS-2: RotWK's reader has them too (its "  PlayerPosition %d" writer format)
		if (line.size() > 15 && eqNoCase(line.substr(0, 15), "PlayerPosition "))
		{
			long long slot = 0;
			if (playerPosition || !parseInt(trim(line.substr(15)), slot) || slot < 1 || slot > 16) return fail("bad PlayerPosition");
			playerPosition = (int)slot;
			continue;
		}
		if (playerPosition)
		{
			if (eqNoCase(line, "END")) { playerPosition = 0; continue; }
			const auto separator = line.find('=');
			if (separator == std::string::npos) return fail("expected scenario slot setting");
			cur->playerPositions[playerPosition][trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
			continue;
		}
		if (eqNoCase(line, "END"))
		{
			cur = nullptr;
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			return fail("expected 'key = value', got '" + line + "'");
		}
		std::string key = trim(line.substr(0, eq));
		std::string val = trim(line.substr(eq + 1));
		long long n = 0;
		bool ok = true;
		Coord3D at;
		if (eqNoCase(key, "fileSize")) { ok = parseInt(val, n); cur->fileSize = (std::uint32_t)n; }
		else if (eqNoCase(key, "fileCRC")) { ok = parseInt(val, n); cur->fileCRC = (std::uint32_t)n; }
		else if (eqNoCase(key, "timestampLo")) { ok = parseInt(val, n); cur->timestampLo = (std::int32_t)n; }
		else if (eqNoCase(key, "timestampHi")) { ok = parseInt(val, n); cur->timestampHi = (std::int32_t)n; }
		else if (eqNoCase(key, "numPlayers")) { ok = parseInt(val, n); cur->numPlayers = (std::int32_t)n; }
		else if (eqNoCase(key, "isOfficial")) ok = parseYesNo(val, cur->isOfficial);
		else if (eqNoCase(key, "isMultiplayer")) ok = parseYesNo(val, cur->isMultiplayer);
		else if (eqNoCase(key, "isScenarioMP")) ok = parseYesNo(val, cur->isScenarioMP);
		else if (eqNoCase(key, "extentMin")) ok = parseCoord(val, cur->extentMin);
		else if (eqNoCase(key, "extentMax")) ok = parseCoord(val, cur->extentMax);
		else if (eqNoCase(key, "InitialCameraPosition"))
		{
			ok = parseCoord(val, cur->initialCamera);
			cur->hasInitialCamera = ok;
		}
		else if (eqNoCase(key, "displayName")) cur->displayName = toUtf16(unescape(val));
		else if (eqNoCase(key, "description")) cur->description = toUtf16(unescape(val));
		else if (eqNoCase(key, "techPosition")) // lane MODS-2: the writer's "  techPosition = X:%2.2f Y:%2.2f Z:%2.2f"
		{
			ok = parseCoord(val, at);
			if (ok) cur->techPositions.push_back(at);
		}
		else if (eqNoCase(key, "supplyPosition"))
		{
			ok = parseCoord(val, at);
			if (ok) cur->supplyPositions.push_back(at);
		}
		else if (key.size() > 13 && eqNoCase(key.substr(0, 7), "Player_") && eqNoCase(key.substr(key.size() - 6), "_Start"))
		{
			long long idx = 0;
			ok = parseInt(key.substr(7, key.size() - 13), idx) && idx >= 1 && idx <= MAX_SLOTS && key == "Player_" + std::to_string(idx) + "_Start";
			Coord3D c;
			ok = ok && parseCoord(val, c);
			if (ok)
			{
				ok = cur->startPositions.emplace((int)idx, c).second;
			}
		}
		else
		{
			return fail("unknown key '" + key + "'");
		}
		if (!ok)
		{
			return fail("bad value for '" + key + "': '" + val + "'");
		}
	}
	if (cur)
	{
		return fail("missing END for block '" + cur->rawName + "'");
	}
	return true;
}

bool validateStartPositions(const MapCacheEntry &map, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error) *error = "map '" + map.name + "': " + why;
		return false;
	};
	if (map.numPlayers < 1 || map.numPlayers > MAX_SLOTS)
	{
		return fail("start count must be in 1..16");
	}
	for (const auto &[number, pos] : map.startPositions)
	{
		if (number < 1 || number > map.numPlayers) return fail("unknown start Player_" + std::to_string(number) + "_Start");
		if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return fail("non-finite start " + std::to_string(number));
	}
	for (int i = 1; i <= map.numPlayers; ++i)
	{
		if (!map.startPositions.count(i)) return fail("missing Player_" + std::to_string(i) + "_Start");
	}
	return true;
}

} // namespace MapCache
