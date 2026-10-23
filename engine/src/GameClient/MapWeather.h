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

// The DiffuseColor of a time of day's "WaterSet MORNING|AFTERNOON|EVENING|NIGHT ... End" block (RGBA 0..1), which
// drawRiverWater lights a white river with (WaterGeometry::riverVertexColour): data\ini\water.ini, then the map's map.ini,
// the last block that sets it winning. R:175 G:175 B:175 A:255 when none does; A defaults to 255.
namespace WaterSetScan
{
void applyText(const std::string &text, int timeOfDay, float diffuse[4]);
// timeOfDay 1 Morning .. 4 Night (clamped). Missing map.ini is fine; a missing water.ini is an error.
bool load(ArchiveFileSystem &fs, const std::string &mapDir, int timeOfDay, float diffuse[4], std::string *error);
} // namespace WaterSetScan

namespace MapWeatherScan
{
// Applies the keys of every "Weather ... End" block in `text` on top of `weather`.
void applyText(const std::string &text, MapWeather &weather);
// weather.ini then <mapDir>\map.ini (mapDir = MapClassification::mapDirectory, "maps\map mp evendim" or a user map's folder). Missing map.ini
// is fine; a missing weather.ini is an error.
bool load(ArchiveFileSystem &fs, const std::string &mapDir, MapWeather &out, std::string *error);
} // namespace MapWeatherScan
