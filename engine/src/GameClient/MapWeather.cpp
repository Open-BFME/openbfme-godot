// OpenBFME. GPL-3.0.
// See GameClient/MapWeather.h.

#include "GameClient/MapWeather.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

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

bool parseXY(const std::string &v, float out[2])
{
	float x, y;
	if (std::sscanf(v.c_str(), "X:%f Y:%f", &x, &y) == 2)
	{
		out[0] = x;
		out[1] = y;
		return true;
	}
	return false;
}

bool parseRGB(const std::string &v, float out[3])
{
	int r, g, b;
	if (std::sscanf(v.c_str(), "R:%d G:%d B:%d", &r, &g, &b) == 3)
	{
		out[0] = r / 255.0f;
		out[1] = g / 255.0f;
		out[2] = b / 255.0f;
		return true;
	}
	return false;
}

} // namespace

void MapWeatherScan::applyText(const std::string &text, MapWeather &w)
{
	size_t pos = 0;
	bool inWeather = false;
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
		if (!inWeather)
		{
			if (lower(line) == "weather")
			{
				inWeather = true;
			}
			continue;
		}
		if (lower(line) == "end")
		{
			inWeather = false;
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		const std::string key = lower(trim(line.substr(0, eq)));
		const std::string val = trim(line.substr(eq + 1));
		if (key == "hardwarefogenable")
		{
			w.fogEnabled = lower(val) == "yes" || val == "1" || lower(val) == "true";
			w.fogKeysSeen = true;
		}
		else if (key == "hardwarefogcolor")
		{
			parseRGB(val, w.fogColor);
			w.fogKeysSeen = true;
		}
		else if (key == "hardwarefogstart")
		{
			w.fogStart = (float)std::atof(val.c_str());
			w.fogKeysSeen = true;
		}
		else if (key == "hardwarefogend")
		{
			w.fogEnd = (float)std::atof(val.c_str());
			w.fogKeysSeen = true;
		}
		else if (key == "cloudtexturesize")
		{
			parseXY(val, w.cloudSize);
		}
		else if (key == "cloudoffsetpersecond")
		{
			parseXY(val, w.cloudOffsetPerSecond);
		}
	}
}

bool MapWeatherScan::load(ArchiveFileSystem &fs, const std::string &mapDir, MapWeather &out, std::string *error)
{
	out = MapWeather();
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\weather.ini", bytes, error))
	{
		return false;
	}
	applyText(std::string(bytes.begin(), bytes.end()), out);
	out.fogKeysSeen = false; // only map.ini fog counts as "the map set fog"
	const std::string mapIni = "maps\\" + mapDir + "\\map.ini";
	if (fs.doesFileExist(mapIni))
	{
		if (!fs.readFile(mapIni, bytes, error))
		{
			return false;
		}
		applyText(std::string(bytes.begin(), bytes.end()), out);
	}
	return true;
}
