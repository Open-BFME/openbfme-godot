// OpenBFME. GPL-3.0.
// See GameClient/MapWeather.h.

#include "GameClient/MapWeather.h"

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>

namespace
{

bool equalsNoCase(const char *a, const char *b)
{
	for (; *a && *b; ++a, ++b)
	{
		if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
		{
			return false;
		}
	}
	return *a == *b;
}

// One INI environment per scan: the macro table (the #define values of the first file stay for the map.ini, as retail's one global table does), a block table in
// which `block` is read by `parse` and every other RotWK keyword is skipped by its recording stub (as the camera settings' reader does), and the file system the
// #includes come from. A key or value retail's parse procs reject is an INIException; it reaches the caller as the false return and the text in `error`.
class MapIniLoad
{
public:
	MapIniLoad(ArchiveFileSystem *fs, const char *block, INIBlockParse parse)
	{
		m_env.fileSystem = fs;
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { block }, StubExtent::LenientAfterEnd, false);
		m_env.blocks.registerBlock(block, std::move(parse));
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
};

// data\ini\*.ini load as INI_LOAD_OVERWRITE, a map.ini as INI_LOAD_CREATE_OVERRIDES (INI.h, spec 0.1 fact 9).
INILoadType loadTypeOf(bool mapOverride)
{
	return mapOverride ? INI_LOAD_CREATE_OVERRIDES : INI_LOAD_OVERWRITE;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------------------------------------------------------------------------------------

// A HardwareFog* key is read by retail's proc and noted, so MapWeather can say "this file set the fog".
void weatherFogKey(INI *ini, void *instance, void *store, const void *userData, INIFieldParseProc proc)
{
	proc(ini, instance, store, userData);
	static_cast<WeatherSetting *>(instance)->fogKeysSeen = true;
}
void weatherFogColor(INI *ini, void *instance, void *store, const void *userData) { weatherFogKey(ini, instance, store, userData, INI::parseRGBColor); } // RW 0x42EF99
void weatherFogEnable(INI *ini, void *instance, void *store, const void *userData) { weatherFogKey(ini, instance, store, userData, INI::parseBool); }   // RW 0x42E558
void weatherFogStart(INI *ini, void *instance, void *store, const void *userData) { weatherFogKey(ini, instance, store, userData, INI::parseReal); }    // RW 0x42ED00
void weatherFogEnd(INI *ini, void *instance, void *store, const void *userData) { weatherFogKey(ini, instance, store, userData, INI::parseReal); }

#define WS_OFF(member) ((int)offsetof(WeatherSetting, member))
// The WeatherSetting field table: RW 0xBF8D70 (31 rows; read from the RotWK game.dat, identical in token, parse proc and offset to BFME2's g_00BE2C10 in
// INIParseWeatherDefinition.cpp). Procs: 0x42EE5E parseAsciiString, 0x42ED00 parseReal, 0x42E558 parseBool, 0x42EC5E parseInt, 0x42F298 parseCoord2D, 0x42EF99
// parseRGBColor, 0x73A396 parseRandVar. A key outside the table, or a value its proc rejects, is an error, as in retail.
const FieldParse kWeatherSettingFields[] = {
	{ "SnowTexture", INI::parseAsciiString, nullptr, WS_OFF(snowTexture) },
	{ "SnowFrequencyScaleX", INI::parseReal, nullptr, WS_OFF(snowFrequencyScaleX) },
	{ "SnowFrequencyScaleY", INI::parseReal, nullptr, WS_OFF(snowFrequencyScaleY) },
	{ "SnowAmplitude", INI::parseReal, nullptr, WS_OFF(snowAmplitude) },
	{ "SnowPointSize", INI::parseReal, nullptr, WS_OFF(snowPointSize) },
	{ "SnowMaxPointSize", INI::parseReal, nullptr, WS_OFF(snowMaxPointSize) },
	{ "SnowMinPointSize", INI::parseReal, nullptr, WS_OFF(snowMinPointSize) },
	{ "SnowQuadSize", INI::parseReal, nullptr, WS_OFF(snowQuadSize) },
	{ "SnowBoxHeight", INI::parseReal, nullptr, WS_OFF(snowBoxHeight) },
	{ "SnowSpacing", INI::parseReal, nullptr, WS_OFF(snowSpacing) },
	{ "SnowSpeed", INI::parseReal, nullptr, WS_OFF(snowSpeed) },
	{ "SnowPointSprites", INI::parseBool, nullptr, WS_OFF(snowPointSprites) },
	{ "SnowEnabled", INI::parseBool, nullptr, WS_OFF(snowEnabled) },
	{ "IsSnowing", INI::parseBool, nullptr, WS_OFF(isSnowing) },
	{ "NumberTiles", INI::parseInt, nullptr, WS_OFF(numberTiles) },
	{ "SnowXYSpeed", INI::parseCoord2D, nullptr, WS_OFF(snowXYSpeed) },
	{ "LightningEnabled", INI::parseBool, nullptr, WS_OFF(lightningEnabled) },
	{ "LightningFactor", GameClientRandomVariable::parseRandomVariable, nullptr, WS_OFF(lightningFactor) },
	{ "LightningDuration", INI::parseInt, nullptr, WS_OFF(lightningDuration) },
	{ "LightningChance", INI::parseReal, nullptr, WS_OFF(lightningChance) },
	{ "SpellEnabled", INI::parseBool, nullptr, WS_OFF(spellEnabled) },
	{ "SpellDuration", INI::parseInt, nullptr, WS_OFF(spellDuration) },
	{ "RampControl", INI::parseCoord2D, nullptr, WS_OFF(rampControl) },
	{ "RampSpacing", INI::parseCoord2D, nullptr, WS_OFF(rampSpacing) },
	{ "RampSpeed", INI::parseCoord2D, nullptr, WS_OFF(rampSpeed) },
	{ "HardwareFogColor", weatherFogColor, nullptr, WS_OFF(hardwareFogColor) },
	{ "HardwareFogEnable", weatherFogEnable, nullptr, WS_OFF(hardwareFogEnable) },
	{ "HardwareFogStart", weatherFogStart, nullptr, WS_OFF(hardwareFogStart) },
	{ "HardwareFogEnd", weatherFogEnd, nullptr, WS_OFF(hardwareFogEnd) },
	{ "CloudTextureSize", INI::parseCoord2D, nullptr, WS_OFF(cloudTextureSize) },
	{ "CloudOffsetPerSecond", INI::parseCoord2D, nullptr, WS_OFF(cloudOffsetPerSecond) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef WS_OFF

// INI::parseWeatherDefinition (INIParseWeatherDefinition.cpp, BFME2 0x20187A; the initFromINI call is RW 0x6012BE): the first block creates TheWeatherSetting; a
// later block is an override of the setting in force only in a load of type INI_LOAD_CREATE_OVERRIDES (a map.ini), any other repeat is "WeatherSetting defined
// twice". The override is copy-constructed from the setting in force, so a key it does not name keeps the earlier file's value: the handler reads into the one
// setting of the load.
INIBlockParse weatherBlock(WeatherSetting &setting)
{
	return [&setting](INI *ini) {
		if (setting.blockSeen && ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES)
		{
			throw INIException(3, "WeatherSetting defined twice");
		}
		ini->initFromINI(&setting, kWeatherSettingFields);
		setting.blockSeen = true;
	};
}

// What the renderer keeps of a WeatherSetting (MapWeather.h), and back.
WeatherSetting settingOf(const MapWeather &w)
{
	WeatherSetting s;
	s.hardwareFogEnable = w.fogEnabled;
	s.hardwareFogColor = { w.fogColor[0], w.fogColor[1], w.fogColor[2] };
	s.hardwareFogStart = w.fogStart;
	s.hardwareFogEnd = w.fogEnd;
	s.cloudTextureSize = { w.cloudSize[0], w.cloudSize[1] };
	s.cloudOffsetPerSecond = { w.cloudOffsetPerSecond[0], w.cloudOffsetPerSecond[1] };
	s.blockSeen = w.settingSeen;
	s.fogKeysSeen = w.fogKeysSeen;
	return s;
}

void keep(const WeatherSetting &s, MapWeather &w)
{
	w.fogEnabled = s.hardwareFogEnable;
	w.fogColor[0] = s.hardwareFogColor.red;
	w.fogColor[1] = s.hardwareFogColor.green;
	w.fogColor[2] = s.hardwareFogColor.blue;
	w.fogStart = s.hardwareFogStart;
	w.fogEnd = s.hardwareFogEnd;
	w.cloudSize[0] = s.cloudTextureSize.x;
	w.cloudSize[1] = s.cloudTextureSize.y;
	w.cloudOffsetPerSecond[0] = s.cloudOffsetPerSecond.x;
	w.cloudOffsetPerSecond[1] = s.cloudOffsetPerSecond.y;
	w.settingSeen = s.blockSeen;
	w.fogKeysSeen = s.fogKeysSeen;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// WaterSet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------

// TimeOfDayNames, RW 0xDA3A68 (BFME2 GameType.cpp): the six names INI::parseWaterSettingDefinition (RW 0x600564) looks the block's name up in, case-insensitively;
// the index is the WaterSettings[] element (6 elements of 0x7C bytes at RW 0xDE4BE8).
const char *const kTimeOfDayNames[WaterSetScan::kTimeOfDayCount] = { "NONE", "MORNING", "AFTERNOON", "EVENING", "NIGHT", "INTERPOLATE" };

#define WA_OFF(member) ((int)offsetof(WaterSetting, member))
// The WaterSetting field table: RW 0xC1E4A0 (12 rows; read from the RotWK game.dat, identical in token, proc and order to BFME2's m_waterSettingFieldParseTable
// in WaterSettingCtor.cpp). Procs: 0x42EE5E parseAsciiString, 0x42F03F parseRGBAColorInt (R: G: B: and an optional A:), 0x42ED00 parseReal, 0x42EC5E parseInt.
const FieldParse kWaterSettingFields[] = {
	{ "SkyTexture", INI::parseAsciiString, nullptr, WA_OFF(skyTextureFile) },
	{ "WaterTexture", INI::parseAsciiString, nullptr, WA_OFF(waterTextureFile) },
	{ "Vertex00Color", INI::parseRGBAColorInt, nullptr, WA_OFF(vertex00Diffuse) },
	{ "Vertex10Color", INI::parseRGBAColorInt, nullptr, WA_OFF(vertex10Diffuse) },
	{ "Vertex01Color", INI::parseRGBAColorInt, nullptr, WA_OFF(vertex01Diffuse) },
	{ "Vertex11Color", INI::parseRGBAColorInt, nullptr, WA_OFF(vertex11Diffuse) },
	{ "DiffuseColor", INI::parseRGBAColorInt, nullptr, WA_OFF(waterDiffuseColor) },
	{ "TransparentDiffuseColor", INI::parseRGBAColorInt, nullptr, WA_OFF(transparentWaterDiffuse) },
	{ "UScrollPerMS", INI::parseReal, nullptr, WA_OFF(uScrollPerMs) },
	{ "VScrollPerMS", INI::parseReal, nullptr, WA_OFF(vScrollPerMs) },
	{ "SkyTexelsPerUnit", INI::parseReal, nullptr, WA_OFF(skyTexelsPerUnit) },
	{ "WaterRepeatCount", INI::parseInt, nullptr, WA_OFF(waterRepeatCount) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef WA_OFF

// INI::parseWaterSettingDefinition (INIWater.cpp, RW 0x600564): the block's name picks the WaterSettings[] element ("No water setting found for time of day '%s'"
// when it names none), whose fields are then read in place. "Note that this does not support override situations": a block repeated in any load type, or in a
// map.ini, simply reads over the element, a key it does not name keeping its value.
INIBlockParse waterSetBlock(WaterSetScan::Settings &settings)
{
	return [&settings](INI *ini) {
		const char *name = ini->getNextToken();
		int index = -1;
		for (int i = 0; i < WaterSetScan::kTimeOfDayCount; ++i)
		{
			if (equalsNoCase(kTimeOfDayNames[i], name))
			{
				index = i;
				break;
			}
		}
		if (index < 0)
		{
			throw INIException(3, "No water setting found for time of day '%s'", name);
		}
		ini->initFromINI(&settings.waterSettings[index], kWaterSettingFields);
	};
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

bool WaterSetScan::applyText(const std::string &text, Settings &settings, std::string *error, bool mapOverride)
{
	Settings working = settings;
	MapIniLoad load(nullptr, "WaterSet", waterSetBlock(working));
	if (!load.run(mapOverride ? "map.ini" : "water.ini", text, loadTypeOf(mapOverride), error))
	{
		return false;
	}
	settings = working;
	return true;
}

bool WaterSetScan::diffuseOf(const Settings &settings, int timeOfDay, float diffuse[4], std::string *error)
{
	if (timeOfDay < 0 || timeOfDay >= kTimeOfDayCount)
	{
		if (error)
		{
			*error = "no water setting for time of day " + std::to_string(timeOfDay);
		}
		return false;
	}
	const RGBAColorInt &c = settings.waterSettings[timeOfDay].waterDiffuseColor;
	diffuse[0] = c.red / 255.0f;
	diffuse[1] = c.green / 255.0f;
	diffuse[2] = c.blue / 255.0f;
	diffuse[3] = c.alpha / 255.0f;
	return true;
}

bool WaterSetScan::load(ArchiveFileSystem &fs, const std::string &mapDir, int timeOfDay, float diffuse[4], std::string *error)
{
	Settings settings;
	MapIniLoad load(&fs, "WaterSet", waterSetBlock(settings));
	if (!load.runFile("data\\ini\\water.ini", INI_LOAD_OVERWRITE, error))
	{
		return false;
	}
	const std::string mapIni = mapDir + "\\map.ini";
	if (fs.doesFileExist(mapIni) && !load.runFile(mapIni, INI_LOAD_CREATE_OVERRIDES, error))
	{
		return false;
	}
	return diffuseOf(settings, timeOfDay, diffuse, error);
}

bool MapWeatherScan::applySetting(const std::string &text, WeatherSetting &setting, std::string *error, bool mapOverride)
{
	WeatherSetting working = setting;
	MapIniLoad load(nullptr, "Weather", weatherBlock(working));
	if (!load.run(mapOverride ? "map.ini" : "weather.ini", text, loadTypeOf(mapOverride), error))
	{
		return false;
	}
	setting = working;
	return true;
}

bool MapWeatherScan::applyText(const std::string &text, MapWeather &weather, std::string *error, bool mapOverride)
{
	WeatherSetting setting = settingOf(weather);
	if (!applySetting(text, setting, error, mapOverride))
	{
		return false;
	}
	keep(setting, weather);
	return true;
}

bool MapWeatherScan::load(ArchiveFileSystem &fs, const std::string &mapDir, MapWeather &out, std::string *error)
{
	out = MapWeather();
	WeatherSetting setting;
	MapIniLoad load(&fs, "Weather", weatherBlock(setting));
	if (!load.runFile("data\\ini\\weather.ini", INI_LOAD_OVERWRITE, error))
	{
		return false;
	}
	setting.fogKeysSeen = false; // only map.ini fog counts as "the map set fog"
	const std::string mapIni = mapDir + "\\map.ini";
	if (fs.doesFileExist(mapIni) && !load.runFile(mapIni, INI_LOAD_CREATE_OVERRIDES, error))
	{
		return false;
	}
	keep(setting, out);
	return true;
}
