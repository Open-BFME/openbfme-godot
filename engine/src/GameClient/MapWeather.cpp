// OpenBFME. GPL-3.0.
// See GameClient/MapWeather.h.

#include "GameClient/MapWeather.h"

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"

#include <algorithm>
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

void WaterSetScan::applyText(const std::string &text, int timeOfDay, float diffuse[4])
{
	static const char *const NAMES[4] = { "morning", "afternoon", "evening", "night" };
	const std::string wanted = NAMES[std::max(0, std::min(3, timeOfDay - 1))];
	size_t pos = 0;
	bool inSet = false;
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
		line = lower(trim(line));
		if (line.empty())
		{
			continue;
		}
		if (!inSet)
		{
			if (line.rfind("waterset", 0) == 0 && trim(line.substr(8)) == wanted)
			{
				inSet = true;
			}
			continue;
		}
		if (line == "end")
		{
			inSet = false;
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos || trim(line.substr(0, eq)) != "diffusecolor")
		{
			continue;
		}
		int r, g, b, a = 255;
		if (std::sscanf(trim(line.substr(eq + 1)).c_str(), "r:%d g:%d b:%d a:%d", &r, &g, &b, &a) >= 3)
		{
			diffuse[0] = r / 255.0f;
			diffuse[1] = g / 255.0f;
			diffuse[2] = b / 255.0f;
			diffuse[3] = a / 255.0f;
		}
	}
}

bool WaterSetScan::load(ArchiveFileSystem &fs, const std::string &mapDir, int timeOfDay, float diffuse[4], std::string *error)
{
	diffuse[0] = diffuse[1] = diffuse[2] = 175.0f / 255.0f;
	diffuse[3] = 1.0f;
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\water.ini", bytes, error))
	{
		return false;
	}
	applyText(std::string(bytes.begin(), bytes.end()), timeOfDay, diffuse);
	const std::string mapIni = "maps\\" + mapDir + "\\map.ini";   // as MapWeatherScan::load
	if (fs.doesFileExist(mapIni))
	{
		if (!fs.readFile(mapIni, bytes, error))
		{
			return false;
		}
		applyText(std::string(bytes.begin(), bytes.end()), timeOfDay, diffuse);
	}
	return true;
}

// WaterTransparency: the block is read through the INI pipeline (Common/INI), so a value is scanned as retail scans it: INI::parseReal (RW 0x42ED00:
// getNextToken, then scanReal 0x42EAAD, which resolves macros and #ADD / #SUBTRACT / #MULTIPLY / #DIVIDE expressions and reads the prefix MSVCR71's sscanf "%f"
// takes) and INI::parseBool (RW 0x42E558, scanBool 0x42CE52: Yes or No only, anything else is an error). The field table is RW 0xC1E570..0xC1E610
// (BFME2 INIWaterTransparency.cpp): TransparentWaterDepth +0x18, TransparentWaterMinOpacity +0x1C, StandingWaterColor +0x20, RadarWaterColor +0x2C,
// AdditiveBlending +0x38, StandingWaterTexture +0x3C, RiverTransparencyMultiplier +0x40, ReflectionPlaneZ +0x4C, ReflectionOn +0x50, ReflectionGuard +0x54; a key
// outside it is an error. Only the reflection's two fields are kept; the others are scanned (and fail) exactly as retail's do. A second block is an error
// unless the file loads as overrides ("WaterTransparency found twice": water.ini loads as INI_LOAD_OVERWRITE, a map.ini as INI_LOAD_CREATE_OVERRIDES).
namespace
{
using WaterTransparencyResult = WaterTransparencyScan::Result;

void wtReal(INI *ini, void *, void *, const void *)
{
	float discarded;
	INI::parseReal(ini, nullptr, &discarded, nullptr); // RW 0x42ED00
}
void wtBool(INI *ini, void *, void *, const void *)
{
	bool discarded;
	INI::parseBool(ini, nullptr, &discarded, nullptr); // RW 0x42E558
}
void wtColor(INI *ini, void *, void *, const void *)
{
	RGBColor discarded;
	INI::parseRGBColor(ini, nullptr, &discarded, nullptr); // RW 0x42EF99
}
void wtString(INI *ini, void *, void *, const void *)
{
	std::string discarded;
	INI::parseAsciiString(ini, nullptr, &discarded, nullptr); // RW 0x42EE5E
}
void wtCoord2D(INI *ini, void *, void *, const void *)
{
	Coord2D discarded;
	INI::parseCoord2D(ini, nullptr, &discarded, nullptr); // RW 0x42F298
}
void wtReflectionPlaneZ(INI *ini, void *instance, void *, const void *)
{
	WaterTransparencyResult *r = static_cast<WaterTransparencyResult *>(instance);
	INI::parseReal(ini, nullptr, &r->reflectionPlaneZ, nullptr); // RW 0x42ED00, + 0x4C
	r->reflectionPlaneZSet = true;
}
void wtReflectionOn(INI *ini, void *instance, void *, const void *)
{
	INI::parseBool(ini, nullptr, &static_cast<WaterTransparencyResult *>(instance)->reflectionOn, nullptr); // RW 0x42E558, + 0x50
}

const FieldParse kWaterTransparencyFields[] = {
	{ "TransparentWaterDepth", wtReal, nullptr, 0x18 },
	{ "TransparentWaterMinOpacity", wtReal, nullptr, 0x1C },
	{ "StandingWaterColor", wtColor, nullptr, 0x20 },
	{ "StandingWaterTexture", wtString, nullptr, 0x3C },
	{ "AdditiveBlending", wtBool, nullptr, 0x38 },
	{ "RadarWaterColor", wtColor, nullptr, 0x2C },
	{ "RiverTransparencyMultiplier", wtReal, nullptr, 0x40 },
	{ "ReflectionPlaneZ", wtReflectionPlaneZ, nullptr, 0x4C },
	{ "ReflectionOn", wtReflectionOn, nullptr, 0x50 },
	{ "ReflectionGuard", wtCoord2D, nullptr, 0x54 },
	{ nullptr, nullptr, nullptr, 0 }
};

// One environment per scan: the macro table (the #define values of water.ini stay for the map.ini, as retail's one global table does), the blocks (every other
// RotWK keyword is skipped by its stub, as the camera settings' reader does) and the file system the #includes come from.
class WaterTransparencyLoad
{
public:
	WaterTransparencyLoad(ArchiveFileSystem *fs, WaterTransparencyResult &result) : m_result(result)
	{
		m_env.fileSystem = fs;
		// LenientAfterEnd: an unrelated block of a map.ini ends at its own End, and the keyword after it is found at any indentation (a Weather block followed by an
		// indented WaterTransparency still overrides the plane), as retail's per-block parsers and whitespace-skipping tokenizer do
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { "WaterTransparency" }, StubExtent::LenientAfterEnd, false);
		m_env.blocks.registerBlock("WaterTransparency", [this](INI *ini) {
			if (m_result.blockSeen && ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES)
			{
				throw INIException(3, "WaterTransparency found twice");
			}
			m_result.blockSeen = true;
			ini->initFromINI(&m_result, kWaterTransparencyFields);
		});
	}
	bool run(const std::string &name, const std::string &text, INILoadType type, std::string *error)
	{
		return guarded(name, error, [&](INI &ini) { ini.loadMemory(name, std::vector<std::uint8_t>(text.begin(), text.end()), type); });
	}
	bool runFile(const std::string &path, INILoadType type, std::string *error)
	{
		return guarded(path, error, [&](INI &ini) { ini.load(path, type); });
	}

private:
	template <class F> bool guarded(const std::string &name, std::string *error, F &&load)
	{
		INI ini(m_env);
		try
		{
			load(ini);
		}
		catch (const std::exception &e)
		{
			if (error)
			{
				*error = name + ": " + e.what();
			}
			return false;
		}
		return true;
	}

	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
	WaterTransparencyResult &m_result;
};
} // namespace

bool WaterTransparencyScan::applyText(const std::string &text, Result &result, std::string *error, bool mapOverride)
{
	WaterTransparencyLoad load(nullptr, result);
	return load.run(mapOverride ? "map.ini" : "water.ini", text, mapOverride ? INI_LOAD_CREATE_OVERRIDES : INI_LOAD_OVERWRITE, error);
}

bool WaterTransparencyScan::load(ArchiveFileSystem &fs, const std::string &mapDir, Result &result, std::string *error)
{
	result = Result(); // the setting's constructor
	WaterTransparencyLoad load(&fs, result);
	if (!load.runFile("data\\ini\\water.ini", INI_LOAD_OVERWRITE, error))
	{
		return false;
	}
	const std::string mapIni = mapDir + "\\map.ini";
	if (fs.doesFileExist(mapIni) && !load.runFile(mapIni, INI_LOAD_CREATE_OVERRIDES, error))
	{
		return false;
	}
	return true;
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
	const std::string mapIni = mapDir + "\\map.ini";
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
