// OpenBFME. GPL-3.0.
//
// The terrain-relevant keys of the INI "Weather" block: hardware fog and the cloud layer. Defaults come
// from data\ini\weather.ini (CloudTextureSize = X:660 Y:660, CloudOffsetPerSecond = X:-0.012 Y:-0.018);
// maps override them in maps\<name>\map.ini (spec 2.13, 2.15: 50 maps carry Weather fog keys). A map with
// no fog keys has no fog (weather.ini holds none).
//
// This is a name-level scan of one block, not the INI system (another lane owns engine/src/Common/INI*).
// Every other Weather key (snow, rain, lightning, ramps) is ignored here and left to the weather lane.

#pragma once

#include "Common/ArchiveFileSystem.h"

#include <string>

struct MapWeather
{
	bool fogEnabled = false;
	float fogColor[3] = { 0.5f, 0.5f, 0.5f }; // 0..1
	float fogStart = 0.0f;
	float fogEnd = 1.0f;
	float cloudSize[2] = { 660.0f, 660.0f };
	float cloudOffsetPerSecond[2] = { -0.012f, -0.018f };
	bool fogKeysSeen = false;
};

namespace MapWeatherScan
{
// Applies the keys of every "Weather ... End" block in `text` on top of `weather`.
void applyText(const std::string &text, MapWeather &weather);
// weather.ini then maps\<mapDir>\map.ini (mapDir = "map mp evendim"). Missing map.ini is fine; a missing
// weather.ini is an error.
bool load(ArchiveFileSystem &fs, const std::string &mapDir, MapWeather &out, std::string *error);
} // namespace MapWeatherScan
