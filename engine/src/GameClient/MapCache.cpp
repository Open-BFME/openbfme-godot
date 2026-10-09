// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/MapCache.h.

#include "GameClient/MapCache.h"

#include <cctype>
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

bool parseYesNo(const std::string &v, bool &out)
{
	if (v == "yes" || v == "Yes" || v == "YES")
	{
		out = true;
		return true;
	}
	if (v == "no" || v == "No" || v == "NO")
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

bool parse(const std::string &text, std::vector<MapCacheEntry> &out, std::string *error)
{
	out.clear();
	std::istringstream in(text);
	std::string raw;
	int lineNo = 0;
	MapCacheEntry *cur = nullptr;
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
			if (line.rfind("MapCache", 0) != 0 || line.size() <= 9 || !std::isspace((unsigned char)line[8]))
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
		if (line == "END")
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
		if (key == "fileSize") { ok = parseInt(val, n); cur->fileSize = (std::uint32_t)n; }
		else if (key == "fileCRC") { ok = parseInt(val, n); cur->fileCRC = (std::uint32_t)n; }
		else if (key == "timestampLo") { ok = parseInt(val, n); cur->timestampLo = (std::int32_t)n; }
		else if (key == "timestampHi") { ok = parseInt(val, n); cur->timestampHi = (std::int32_t)n; }
		else if (key == "numPlayers") { ok = parseInt(val, n); cur->numPlayers = (std::int32_t)n; }
		else if (key == "isOfficial") ok = parseYesNo(val, cur->isOfficial);
		else if (key == "isMultiplayer") ok = parseYesNo(val, cur->isMultiplayer);
		else if (key == "isScenarioMP") ok = parseYesNo(val, cur->isScenarioMP);
		else if (key == "extentMin") ok = parseCoord(val, cur->extentMin);
		else if (key == "extentMax") ok = parseCoord(val, cur->extentMax);
		else if (key == "InitialCameraPosition")
		{
			ok = parseCoord(val, cur->initialCamera);
			cur->hasInitialCamera = ok;
		}
		else if (key == "displayName") cur->displayName = toUtf16(unescape(val));
		else if (key == "description") cur->description = toUtf16(unescape(val));
		else if (key.rfind("Player_", 0) == 0 && key.size() > 13 && key.compare(key.size() - 6, 6, "_Start") == 0)
		{
			long long idx = 0;
			ok = parseInt(key.substr(7, key.size() - 13), idx) && idx >= 1;
			Coord3D c;
			ok = ok && parseCoord(val, c);
			if (ok)
			{
				cur->startPositions[(int)idx] = c;
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

} // namespace MapCache
