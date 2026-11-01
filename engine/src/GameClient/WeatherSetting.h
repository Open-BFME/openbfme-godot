// OpenBFME. GPL-3.0.
//
// Lane WEATHER-1: the weather data of RotWK as the INI system loads it. Three blocks:
//   * Weather      TheWeatherSetting (OVERRIDE<WeatherSetting>, RW global 0xDE3B58): the snow / rain particles, the lightning flash of the drops, the
//                  spell ramp of Freezing Rain, the hardware fog and the cloud layer. Data\INI\Default\Weather.ini and Data\INI\Weather.ini (the
//                  TheGlobalWeatherSystem legend entry, load type 1), then maps\<map>\map.ini and solo.ini (load type 2).
//   * WeatherData  the five WeatherData records of TheGlobalWeatherSystem (+0x30, 8 bytes each): the weather's sound loop and whether it may flash.
//   * CloudEffect  the dark cloud / lightning data of Environment.ini (TheWritableGlobalData's hard-coded loads), overridable in map.ini.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * parseWeatherDefinition RW 0x6011F3: the first block creates the base setting (new 0xB4 bytes, ctor RW 0x600F91); a later block in load type 2
//     makes an override whose start values are a copy of the BASE setting (RW 0x600D8B called with the base, global copy flag RW 0xDE78F0), marks it
//     an override (+8 = 1) and chains it behind the final override (+4); any other repeat throws "WeatherSetting defined twice". The final
//     override is then filled from the field table RW 0xBF8D70 (31 rows) and TheSnowManager's updateIniSettings (vtable slot 0x38) runs.
//   * the ctor defaults RW 0x600F91 (read below in WeatherSetting), the field offsets of RW 0xBF8D70, the parsers: SnowTexture parseAsciiString
//     RW 0x42EE5E, the reals parseReal RW 0x42ED00, the bools parseBool RW 0x42E558, NumberTiles / LightningDuration / SpellDuration parseInt
//     RW 0x42EC5E, SnowXYSpeed / RampControl / RampSpacing / RampSpeed / CloudTextureSize / CloudOffsetPerSecond parseCoord2D RW 0x42F298,
//     LightningFactor GameClientRandomVariable RW 0x73A396, HardwareFogColor parseRGBColor RW 0x42EF99. RampSpacing is stored at +0x84 and
//     RampSpeed at +0x7C (the table's order is Control, Spacing, Speed).
//   * SnowManager::reset (RW 0x493547 -> base RW 0x600AAD) deletes the overrides; the base stays.
//   * WeatherData RW 0x719D60: only load type 1 ("Cannot define WeatherData in map.ini" otherwise), the name is one of RW 0xC22CF8 NONE, CLOUDY,
//     RAINY, CLOUDYRAINY, SUNNY (case-insensitive; "Unknown weather type %s"), fields RW 0xC22D0C: WeatherSound (RW 0x73B217 -> 0x73AA94: an
//     audio event name, "NoSound" clears, an unknown one is "Invalid Sound '%s'") and HasLightning (parseBool). Ctor (RW 0x719C53): no sound, false.
//   * CloudEffect RW 0x614FC9: the parse first copies the saved defaults RW 0xDE3C28 over the current data RW 0xDE3CC0 (RW 0x614B5F), parses the
//     block into the current data (table RW 0xBFB6A8, 27 rows), and unless the load type is 2 or 4 saves the current data as the new defaults;
//     then TheCloudEffect's settings refresh (vtable slot 0x38, RW 0x614C71) runs. The data's static ctor RW 0x614E2B leaves every number 0.
//
// INFERENCE: TheWeatherSetting and TheCloudEffect are client objects; the WeatherData table lives in TheGlobalWeatherSystem but only client code
// reads it (RW 0x719DEE the sound, RW 0x719BE4 the lightning test), so it is kept here, with the client.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/GameClientRandomVariable.h"
#include "Common/INIDataTypes.h"

#include <functional>
#include <string>
#include <vector>

// RW 0xB4-byte WeatherSetting (the Overridable header +0x00 .. +0x17 is the store's chain here)
// the default HardwareFogColor, RGBColor::setFromInt(0x886655): 0x88 / 255.0f, 0x66 / 255.0f, 0x55 / 255.0f (WeatherSetting.cpp: the weather headers reach
// an audited simulation-input file, tools/sim/sim_audit.py, so their float maths lives in the .cpp files)
RGBColor defaultHardwareFogColor();

struct WeatherSetting
{
	std::string snowTexture = "EXSnowFlake.tga"; ///< +0x18 SnowTexture
	float snowFrequencyScaleX = 0.0533f;         ///< +0x1C (RW 0xBF91B0)
	float snowFrequencyScaleY = 0.0275f;         ///< +0x20 (RW 0xBF91AC)
	float snowAmplitude = 5.0f;                  ///< +0x24
	float snowPointSize = 1.0f;                  ///< +0x28
	float snowMaxPointSize = 64.0f;              ///< +0x2C
	float snowMinPointSize = 0.0f;               ///< +0x30
	float snowQuadSize = 0.5f;                   ///< +0x34
	float snowBoxHeight = 200.0f;                ///< +0x38 SnowBoxHeight
	float snowSpacing = 50.0f;                   ///< +0x3C SnowSpacing
	float snowSpeed = 100.0f;                    ///< +0x40 SnowSpeed
	bool snowPointSprites = true;                ///< +0x44
	bool snowEnabled = false;                    ///< +0x45
	bool isSnowing = false;                      ///< +0x46
	int numberTiles = 4;                         ///< +0x48
	Coord2D snowXYSpeed;                         ///< +0x4C (0, 0)
	bool lightningEnabled = false;               ///< +0x54
	GameClientRandomVariable lightningFactor;    ///< +0x58 (type 0, 0, 0)
	int lightningDuration = 30;                  ///< +0x64
	float lightningChance = 0.01f;               ///< +0x68 (RW 0xBE5600)
	bool spellEnabled = true;                    ///< +0x6C
	int spellDuration = 200;                     ///< +0x70
	Coord2D rampControl{ 0.3f, 0.7f };           ///< +0x74
	Coord2D rampSpeed{ 0.0f, 100.0f };           ///< +0x7C
	Coord2D rampSpacing{ 50.0f, 20.0f };         ///< +0x84
	RGBColor hardwareFogColor = defaultHardwareFogColor(); ///< +0x8C RGBColor::setFromInt(0x886655)
	bool hardwareFogEnable = false;              ///< +0x98
	float hardwareFogStart = 300.0f;             ///< +0x9C
	float hardwareFogEnd = 1500.0f;              ///< +0xA0
	Coord2D cloudTextureSize{ 660.0f, 660.0f };  ///< +0xA4
	Coord2D cloudOffsetPerSecond{ -0.012f, -0.018f }; ///< +0xAC
};

// RW 0xC22CF8 order; RW 0xDB0074 is the same list for ChangeWeather
enum WeatherType
{
	WEATHER_TYPE_NONE = 0,
	WEATHER_TYPE_CLOUDY = 1,
	WEATHER_TYPE_RAINY = 2,
	WEATHER_TYPE_CLOUDYRAINY = 3,
	WEATHER_TYPE_SUNNY = 4,
	WEATHER_TYPE_COUNT = 5
};

struct WeatherDataRecord
{
	std::string weatherSound; ///< +0 (empty: none)
	bool hasLightning = false; ///< +4
};

// RW 0xDE3CC0 (the current data) / 0xDE3C28 (the saved defaults): the INI layout of CloudEffect
struct CloudEffectData
{
	std::string cloudTexture;         ///< +0x00
	std::string darkCloudTexture;     ///< +0x04
	std::string alphaTexture;         ///< +0x08
	float propagateSpeed = 0.0f;      ///< +0x0C
	int angle = 0;                    ///< +0x10 (parseInt: degrees)
	std::string dissipateTexture;     ///< +0x14
	float dissipateStartLevel = 0.0f; ///< +0x18
	float dissipateSpeed = 0.0f;      ///< +0x1C
	RGBColor darkeningFactor{ 0, 0, 0 };     ///< +0x20
	RGBColor darkeningFactorRain{ 0, 0, 0 }; ///< +0x2C
	int darkeningRate = 0;            ///< +0x38
	int lighteningRate = 0;           ///< +0x3C
	float cloudScrollSpeed = 0.0f;    ///< +0x40
	float dissipateRateScale = 0.0f;  ///< +0x44
	bool lightningShadows = false;    ///< +0x48
	bool jitterLightningLightPosition = false;  ///< +0x49
	bool jitterLightningLightIntensity = false; ///< +0x4A
	float lightningChance = 0.0f;     ///< +0x4C
	RGBColor lightningShadowColor{ 0, 0, 0 }; ///< +0x50
	float lightningShadowIntensity = 0.0f;    ///< +0x5C
	GameClientRandomVariable lightningDuration;  ///< +0x60
	float lightningFrequency = 0.0f;  ///< +0x6C
	GameClientRandomVariable lightningIntensity; ///< +0x70
	Coord2D lightningLightPosition[3];           ///< +0x7C / +0x84 / +0x8C
	std::string lightningFX;          ///< +0x94
};

// The weather data of one game: the base WeatherSetting and its map overrides, the WeatherData table and the CloudEffect data with its defaults.
class WeatherSettingStore
{
public:
	// The audio event validator WeatherSound needs (RW 0x73AA94: an unknown name is "Invalid Sound"); it is required (no silent acceptance).
	typedef std::function<bool(const std::string &eventName)> SoundValidator;

	// One INI file through the three real blocks; every other block keyword is a recording stub. false + *error on an INI error (the
	// retail exception text). `loadType` is the INILoadType (1 the startup files, 2 map.ini / solo.ini).
	bool loadFile(ArchiveFileSystem &fs, const std::string &path, int loadType, std::string *error);
	// (`fs` may be null: the text then has no #include)
	bool loadText(ArchiveFileSystem *fs, const std::string &name, const std::string &text, int loadType, std::string *error);

	// The retail sequence of a game on `mapDir` (as "maps\\map mp evendim" or "map mp evendim"): Data\INI\Default\Weather.ini, Data\INI\Weather.ini,
	// Data\INI\Default\Environment.ini, Data\INI\Environment.ini (load type 1; all four are required, as the game's own startup requires them in
	// RotWK), then the map's map.ini and solo.ini when present (load type 2). false + *error when one of the four is missing or a block fails.
	bool loadForMap(ArchiveFileSystem &fs, const std::string &mapDir, std::string *error);

	// SnowManager::reset (RW 0x493547 -> RW 0x600AAD): the overrides go, the base stays
	void deleteOverrides();

	bool hasWeatherSetting() const { return !m_chain.empty(); }
	// RW TheWeatherSetting.getFinalOverride(): the last of the chain (the base when there is no override). Throws std::logic_error before any block.
	const WeatherSetting &finalSetting() const;
	const WeatherSetting &baseSetting() const;
	size_t overrideCount() const { return m_chain.empty() ? 0 : m_chain.size() - 1; }

	const WeatherDataRecord &weatherData(int weather) const { return m_weatherData[(weather >= 0 && weather < WEATHER_TYPE_COUNT) ? weather : 0]; }
	bool cloudEffectSeen() const { return m_cloudEffectSeen; }
	const CloudEffectData &cloudEffect() const { return m_cloudCurrent; }
	const CloudEffectData &cloudEffectDefaults() const { return m_cloudDefaults; }

	// the files loadForMap read, in order
	const std::vector<std::string> &files() const { return m_files; }

	void setSoundValidator(SoundValidator v) { m_validSound = std::move(v); }

	// RW 0xC22CF8
	static const char *const WeatherTypeNames[WEATHER_TYPE_COUNT];

private:
	friend struct WeatherBlockParsers;
	std::vector<WeatherSetting> m_chain;                 ///< [0] the base, then the overrides in chain order
	WeatherDataRecord m_weatherData[WEATHER_TYPE_COUNT];
	CloudEffectData m_cloudCurrent;                      ///< RW 0xDE3CC0
	CloudEffectData m_cloudDefaults;                     ///< RW 0xDE3C28
	bool m_cloudEffectSeen = false;
	SoundValidator m_validSound;
	std::vector<std::string> m_files;
};
