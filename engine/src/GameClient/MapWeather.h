// OpenBFME. GPL-3.0.
//
// The terrain-relevant values of the INI "Weather" block (BFME2 WeatherSetting): hardware fog and the cloud layer. data\ini\weather.ini sets the cloud keys
// (CloudTextureSize = X:660 Y:660, CloudOffsetPerSecond = X:-0.012 Y:-0.018) and no fog; maps override them in <mapDir>\map.ini (spec 2.13, 2.15: 50 maps
// carry Weather fog keys). A map with no fog keys has no fog.
//
// The block is read through the INI class (Common/INI) with RotWK's WeatherSetting field table (RW 0xBF8D70), so every key and value is read as retail reads
// it and one retail rejects is an error. The renderer keeps only the values below; the snow, lightning and ramp keys are read and dropped.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/INIDataTypes.h"
#include "GameClient/WeatherSettings.h"

#include <string>

struct MapWeather
{
	bool fogEnabled = false;
	float fogColor[3] = { 0x88 / 255.0f, 0x66 / 255.0f, 0x55 / 255.0f }; // 0..1; the WeatherSetting constructor's values (BFME2 0x201618)
	float fogStart = 300.0f;
	float fogEnd = 1500.0f;
	float cloudSize[2] = { 660.0f, 660.0f };
	float cloudOffsetPerSecond[2] = { -0.012f, -0.018f };
	bool fogKeysSeen = false;
	bool settingSeen = false; // a Weather block was read (a second one is "WeatherSetting defined twice" unless the file loads as overrides)
};

// The DiffuseColor of the "WaterSet MORNING|AFTERNOON|EVENING|NIGHT ... End" blocks (drawRiverWater lights a white river with the time of day's one,
// WaterGeometry::riverVertexColour): data\ini\water.ini, then the map's map.ini, the last block that sets it winning. The block is read through the INI class with
// RotWK's WaterSetting field table (RW 0xC1E4A0) and its name looked up in TimeOfDayNames (RW 0xDA3A68), so every key and value is read as retail reads it and a
// name or key retail rejects is an error.
namespace WaterSetScan
{
// TimeOfDayNames: NONE, MORNING, AFTERNOON, EVENING, NIGHT, INTERPOLATE (the WaterSettings[] elements).
constexpr int kTimeOfDayCount = 6;
struct Settings
{
	WaterSetting waterSettings[kTimeOfDayCount]; // WaterSettings[6] (RW 0xDE4BE8): each element as the WaterSetting constructor leaves it until a block reads it
};
// Reads the WaterSet blocks of one file on top of `settings`. `mapOverride`: the text is a map.ini (INI_LOAD_CREATE_OVERRIDES); WaterSet takes no part in the
// override chain, so only the load type of the nested INI differs. On failure `settings` is unchanged and `error` holds the INIException's text.
bool applyText(const std::string &text, Settings &settings, std::string *error, bool mapOverride = false);
// The RGBA of time of day 0 (NONE) .. 5 (INTERPOLATE) as 0..1; false for any other index.
bool diffuseOf(const Settings &settings, int timeOfDay, float diffuse[4], std::string *error);
// mapDir as MapWeatherScan::load takes it: the map's folder (MapClassification::mapDirectory, "maps\map mp evendim" or a user map's folder). timeOfDay 1 Morning
// .. 4 Night (MapChunks.h). Missing map.ini is fine; a missing water.ini is an error. `diffuse` is written only when
// the files were read and the time of day is a WaterSetting; on failure it is untouched and `error` says why (no colour stands in for one that was not read).
bool load(ArchiveFileSystem &fs, const std::string &mapDir, int timeOfDay, float diffuse[4], std::string *error);
} // namespace WaterSetScan

namespace MapWeatherScan
{
// Reads the "Weather ... End" blocks of `text` on top of `setting` (every member of the WeatherSetting, for the tests); the rules of applyText.
bool applySetting(const std::string &text, WeatherSetting &setting, std::string *error, bool mapOverride = false);
// Applies the "Weather ... End" blocks of `text` on top of `weather`. `mapOverride`: the text is a map.ini (INI_LOAD_CREATE_OVERRIDES), where a second block
// overrides; otherwise a second block is "WeatherSetting defined twice". On failure `weather` is unchanged and `error` holds the INIException's text.
bool applyText(const std::string &text, MapWeather &weather, std::string *error, bool mapOverride = false);
// data\ini\weather.ini then <mapDir>\map.ini (mapDir = MapClassification::mapDirectory, "maps\map mp evendim" or a user map's folder). Missing map.ini
// is fine; a missing weather.ini is an error. On failure `out` is a default MapWeather (nothing of a half-read setting) and `error` says why.
bool load(ArchiveFileSystem &fs, const std::string &mapDir, MapWeather &out, std::string *error);
} // namespace MapWeatherScan
