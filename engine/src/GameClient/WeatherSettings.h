// OpenBFME. GPL-3.0.
//
// The objects the INI "Weather" and "WaterSet" blocks read into: BFME2's WeatherSetting (the 0xB4-byte object) and WaterSetting (0x7C bytes), with the values
// their constructors give (WeatherSettingCtor.cpp, BFME2 0x201618; WaterSettingCtor.cpp, BFME2 0x309CF8). The offsets in the comments are retail's; the structs
// are this port's own layout (the field tables in MapWeather.cpp use offsetof). MapWeather.h reads them from the files; the renderer keeps only what it draws
// (MapWeather, WaterSetScan::diffuseOf) and the tests look at every member.

#pragma once

#include "Common/GameClientRandomVariable.h"
#include "Common/INIDataTypes.h"

#include <string>

struct WeatherSetting
{
	std::string snowTexture = "EXSnowFlake.tga";                                  // +0x18
	float snowFrequencyScaleX = 0.0533f;                                          // +0x1C
	float snowFrequencyScaleY = 0.0275f;                                          // +0x20
	float snowAmplitude = 5.0f;                                                   // +0x24
	float snowPointSize = 1.0f;                                                   // +0x28
	float snowMaxPointSize = 64.0f;                                               // +0x2C
	float snowMinPointSize = 0.0f;                                                // +0x30
	float snowQuadSize = 0.5f;                                                    // +0x34
	float snowBoxHeight = 200.0f;                                                 // +0x38
	float snowSpacing = 50.0f;                                                    // +0x3C
	float snowSpeed = 100.0f;                                                     // +0x40
	bool snowPointSprites = true;                                                 // +0x44
	bool snowEnabled = false;                                                     // +0x45
	bool isSnowing = false;                                                       // +0x46
	int numberTiles = 4;                                                          // +0x48
	Coord2D snowXYSpeed;                                                          // +0x4C
	bool lightningEnabled = false;                                                // +0x54
	GameClientRandomVariable lightningFactor;                                     // +0x58
	int lightningDuration = 30;                                                   // +0x64
	float lightningChance = 0.01f;                                                // +0x68
	bool spellEnabled = true;                                                     // +0x6C
	int spellDuration = 200;                                                      // +0x70
	Coord2D rampControl = { 0.3f, 0.7f };                                         // +0x74
	Coord2D rampSpeed = { 0.0f, 100.0f };                                         // +0x7C
	Coord2D rampSpacing = { 50.0f, 20.0f };                                       // +0x84
	RGBColor hardwareFogColor = { 0x88 / 255.0f, 0x66 / 255.0f, 0x55 / 255.0f };  // +0x8C
	bool hardwareFogEnable = false;                                               // +0x98
	float hardwareFogStart = 300.0f;                                              // +0x9C
	float hardwareFogEnd = 1500.0f;                                               // +0xA0
	Coord2D cloudTextureSize = { 660.0f, 660.0f };                                // +0xA4
	Coord2D cloudOffsetPerSecond = { -0.012f, -0.018f };                          // +0xAC

	// not retail: bookkeeping of the load
	bool blockSeen = false;   // a Weather block was read (a second one is "WeatherSetting defined twice" unless the file loads as overrides)
	bool fogKeysSeen = false; // a HardwareFog* key was read
};

struct WaterSetting
{
	std::string skyTextureFile;                             // +0x04
	std::string waterTextureFile;                           // +0x08
	int waterRepeatCount = 0;                               // +0x0C
	float skyTexelsPerUnit = 0.0f;                          // +0x10
	RGBAColorInt vertex00Diffuse = { 0, 0, 0, 0 };          // +0x14
	RGBAColorInt vertex10Diffuse = { 0, 0, 0, 0 };          // +0x24
	RGBAColorInt vertex11Diffuse = { 0, 0, 0, 0 };          // +0x34
	RGBAColorInt vertex01Diffuse = { 0, 0, 0, 0 };          // +0x44
	RGBAColorInt waterDiffuseColor = { 0, 0, 0, 0 };        // +0x54
	RGBAColorInt transparentWaterDiffuse = { 0, 0, 0, 0 };  // +0x64
	float uScrollPerMs = 0.0f;                              // +0x74
	float vScrollPerMs = 0.0f;                              // +0x78
};
