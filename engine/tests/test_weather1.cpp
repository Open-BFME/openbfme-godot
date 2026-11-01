// OpenBFME unit tests, lane WEATHER-1 (the weather: snow and rain, the Freezing Rain ramp, the Darkness darkening, lightning, the weather sound).
// GPL-3.0.
//
// The Weather / WeatherData / CloudEffect blocks through the INI system (RW 0x6011F3, 0x719D60, 0x614FC9), TheSnowManager (RW 0x4943E1, 0x49405A,
// 0x493563, 0x492E5F, 0x492C35, 0x4936CB), TheCloudEffect (RW 0x497D00, 0x494DBA), the weather sound (RW 0x719DEE), the logic snapshot's weather and
// the FXList weather filter. The expected values are derived by hand from the rules named in the headers (GameClient/WeatherSetting.h, SnowManager.h,
// CloudEffect.h); the retail cases read the shipped weather.ini / environment.ini / map.ini and SKIP loudly without the installs.

#include "doctest.h"

#include "LogicTestUtil.h"
#include "RetailTestMount.h"

#include "Common/RandomValue.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/CloudEffect.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/SnowManager.h"
#include "GameClient/WeatherClient.h"
#include "GameClient/WeatherSetting.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GlobalWeatherSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace
{
WeatherSettingStore::SoundValidator anySound(const std::vector<std::string> &known)
{
	return [known](const std::string &n) {
		for (const std::string &k : known)
		{
			if (k == n)
			{
				return true;
			}
		}
		return false;
	};
}

bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

// retail weather.ini's Weather block, verbatim values (data\ini\weather.ini)
const char *kRetailLikeWeather = "Weather\n"
								 "  SnowEnabled = yes\n"
								 "  IsSnowing = no\n"
								 "  SnowTexture = EXRainDrop.tga\n"
								 "  SnowBoxHeight = 300\n"
								 "  SnowSpacing = 30\n"
								 "  NumberTiles = 4\n"
								 "  SnowAmplitude = 0\n"
								 "  SnowFrequencyScaleX = 0\n"
								 "  SnowFrequencyScaleY = 0\n"
								 "  SnowSpeed = 50.0\n"
								 "  SnowPointSize = 5\n"
								 "  SnowMinPointSize = 40\n"
								 "  SnowMaxPointSize = 80\n"
								 "  SnowPointSprites = no\n"
								 "  SnowQuadSize = 10\n"
								 "  LightningEnabled = no\n"
								 "  LightningFactor = 0.5 1.0\n"
								 "  LightningDuration = 50\n"
								 "  LightningChance = 0.05\n"
								 "  SpellEnabled = yes\n"
								 "  SpellDuration = 500\n"
								 "  RampControl = X:0.2 Y:0.8\n"
								 "  RampSpeed = X:150.0 Y:250.0\n"
								 "  RampSpacing = X:30.0 Y:20.0\n"
								 "  CloudTextureSize = X:660.0 Y:660.0\n"
								 "  CloudOffsetPerSecond = X:-0.012 Y:-0.018\n"
								 "End\n";
} // namespace

TEST_CASE("weather1 setting: the WeatherSetting ctor defaults (RW 0x600F91) and every row of the field table RW 0xBF8D70")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini", "Weather\nEnd\n", 1, &error));
	const WeatherSetting &d = s.finalSetting();
	CHECK(d.snowTexture == "EXSnowFlake.tga");
	CHECK(d.snowFrequencyScaleX == 0.0533f);
	CHECK(d.snowFrequencyScaleY == 0.0275f);
	CHECK(d.snowAmplitude == 5.0f);
	CHECK(d.snowPointSize == 1.0f);
	CHECK(d.snowMaxPointSize == 64.0f);
	CHECK(d.snowMinPointSize == 0.0f);
	CHECK(d.snowQuadSize == 0.5f);
	CHECK(d.snowBoxHeight == 200.0f);
	CHECK(d.snowSpacing == 50.0f);
	CHECK(d.snowSpeed == 100.0f);
	CHECK(d.snowPointSprites);
	CHECK_FALSE(d.snowEnabled);
	CHECK_FALSE(d.isSnowing);
	CHECK(d.numberTiles == 4);
	CHECK(d.lightningDuration == 30);
	CHECK(d.lightningChance == 0.01f);
	CHECK(d.spellEnabled);
	CHECK(d.spellDuration == 200);
	CHECK(d.rampControl.x == 0.3f);
	CHECK(d.rampControl.y == 0.7f);
	CHECK(d.rampSpeed.x == 0.0f);
	CHECK(d.rampSpeed.y == 100.0f);
	CHECK(d.rampSpacing.x == 50.0f);
	CHECK(d.rampSpacing.y == 20.0f);
	CHECK_FALSE(d.hardwareFogEnable);
	CHECK(d.hardwareFogStart == 300.0f);
	CHECK(d.hardwareFogEnd == 1500.0f);
	CHECK(d.cloudTextureSize.x == 660.0f);
	CHECK(d.cloudOffsetPerSecond.y == -0.018f);

	WeatherSettingStore r;
	REQUIRE(r.loadText(nullptr, "weather.ini",
		std::string(kRetailLikeWeather).replace(std::string(kRetailLikeWeather).find("End\n"), 4,
			"  SnowXYSpeed = X:40 Y:25\n  HardwareFogColor = R:120 G:166 B:191\n  HardwareFogEnable = Yes\n  HardwareFogStart = 500\n"
			"  HardwareFogEnd = 2000\nEnd\n"),
		1, &error));
	const WeatherSetting &w = r.finalSetting();
	CHECK(w.snowTexture == "EXRainDrop.tga");
	CHECK(w.snowEnabled);
	CHECK_FALSE(w.isSnowing);
	CHECK(w.snowBoxHeight == 300.0f);
	CHECK(w.snowSpacing == 30.0f);
	CHECK(w.snowSpeed == 50.0f);
	CHECK(w.snowQuadSize == 10.0f);
	CHECK_FALSE(w.snowPointSprites);
	CHECK(w.snowXYSpeed.x == 40.0f);
	CHECK(w.snowXYSpeed.y == 25.0f);
	CHECK(w.lightningFactor.m_type == GameClientRandomVariable::UNIFORM);
	CHECK(w.lightningFactor.m_low == 0.5f);
	CHECK(w.lightningFactor.m_high == 1.0f);
	CHECK(w.lightningDuration == 50);
	CHECK(w.lightningChance == 0.05f);
	CHECK(w.spellDuration == 500);
	CHECK(w.rampControl.x == 0.2f);
	CHECK(w.rampControl.y == 0.8f);
	CHECK(w.rampSpeed.x == 150.0f); // RampSpeed is stored at +0x7C
	CHECK(w.rampSpeed.y == 250.0f);
	CHECK(w.rampSpacing.x == 30.0f); // RampSpacing at +0x84
	CHECK(w.rampSpacing.y == 20.0f);
	CHECK(w.hardwareFogEnable);
	CHECK(w.hardwareFogColor.red == 120.0f * (1.0f / 255.0f));
	CHECK(w.hardwareFogStart == 500.0f);
	CHECK(w.hardwareFogEnd == 2000.0f);
}

TEST_CASE("weather1 setting: a map.ini Weather block is an override that starts from the BASE setting (RW 0x6011F3); load type 1 throws on a repeat")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini", "Weather\n  SnowSpacing = 30\n  SnowSpeed = 50\nEnd\n", 1, &error));
	// a second block in a startup file: "WeatherSetting defined twice"
	CHECK_FALSE(s.loadText(nullptr, "weather2.ini", "Weather\n  SnowSpeed = 70\nEnd\n", 1, &error));
	CHECK(contains(error, "WeatherSetting defined twice"));
	CHECK(s.overrideCount() == 0);
	// map.ini (load type 2): an override; a second override starts from the base again, not from the first override
	// the world's blocks of a map.ini (Object, and Weapon, which the shared stub table would parse into TheWeaponStore) are skipped
	REQUIRE_MESSAGE(s.loadText(nullptr, "map.ini",
						"Object Foo\n  Draw = W3DModelDraw x\n  End\nEnd\nWeapon MapWeapon\n  AttackRange = 10\nEnd\nWeather\n  SnowSpeed = 60\n  IsSnowing = Yes\nEnd\n",
						2, &error),
		error);
	CHECK(s.overrideCount() == 1);
	CHECK(s.finalSetting().snowSpeed == 60.0f);
	CHECK(s.finalSetting().isSnowing);
	CHECK(s.finalSetting().snowSpacing == 30.0f); // copied from the base
	REQUIRE(s.loadText(nullptr, "map2.ini", "Weather\n  SnowSpacing = 25\nEnd\n", 2, &error));
	CHECK(s.overrideCount() == 2);
	CHECK(s.finalSetting().snowSpacing == 25.0f);
	CHECK(s.finalSetting().snowSpeed == 50.0f); // the base's, not the first override's 60
	CHECK_FALSE(s.finalSetting().isSnowing);
	CHECK(s.baseSetting().snowSpeed == 50.0f);
	// SnowManager::reset (RW 0x600AAD): the overrides go
	s.deleteOverrides();
	CHECK(s.overrideCount() == 0);
	CHECK(s.finalSetting().snowSpacing == 30.0f);
}

TEST_CASE("weather1 setting: WeatherData (RW 0x719D60): the five names, the errors, WeatherSound through the audio validator, NoSound")
{
	WeatherSettingStore s;
	std::string error;
	// no validator: a logic error, never a silent acceptance
	CHECK_FALSE(s.loadText(nullptr, "weather.ini", "WeatherData RAINY\n  WeatherSound = RainStereoLoop\nEnd\n", 1, &error));
	CHECK(contains(error, "audio event validator"));
	s.setSoundValidator(anySound({ "RainStereoLoop" }));
	REQUIRE(s.loadText(nullptr, "weather.ini",
		"WeatherData rainy\n  WeatherSound = RainStereoLoop\n  HasLightning = Yes\nEnd\nWeatherData SUNNY\n  HasLightning = No\nEnd\n"
		"WeatherData CLOUDYRAINY\n  WeatherSound = NoSound\n  HasLightning = Yes\nEnd\n",
		1, &error));
	CHECK(s.weatherData(WEATHER_TYPE_RAINY).weatherSound == "RainStereoLoop"); // the name compares case-insensitively
	CHECK(s.weatherData(WEATHER_TYPE_RAINY).hasLightning);
	CHECK_FALSE(s.weatherData(WEATHER_TYPE_SUNNY).hasLightning);
	CHECK(s.weatherData(WEATHER_TYPE_CLOUDYRAINY).weatherSound.empty());
	CHECK(s.weatherData(WEATHER_TYPE_CLOUDYRAINY).hasLightning);
	CHECK_FALSE(s.weatherData(WEATHER_TYPE_NONE).hasLightning);
	CHECK_FALSE(s.loadText(nullptr, "weather.ini", "WeatherData STORMY\nEnd\n", 1, &error));
	CHECK(contains(error, "Unknown weather type STORMY"));
	CHECK_FALSE(s.loadText(nullptr, "weather.ini", "WeatherData NONE\n  WeatherSound = ThunderStrikes\nEnd\n", 1, &error));
	CHECK(contains(error, "Invalid Sound 'ThunderStrikes'"));
	CHECK_FALSE(s.loadText(nullptr, "map.ini", "WeatherData NONE\nEnd\n", 2, &error));
	CHECK(contains(error, "Cannot define WeatherData in map.ini"));
}

TEST_CASE("weather1 setting: CloudEffect (RW 0x614FC9): a map.ini block starts from the saved defaults and does not become them")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "environment.ini",
		"CloudEffect\n  DarkeningFactor = R:100 G:100 B:30\n  LightningChance = 0.005\n  LightningDuration = 10 50\n  LightningIntensity = 0.05 0.4\n"
		"  LightningLightPosition2 = X:27 Y:113\n  LightningFX = FX_LightningThunderStrike\n  Angle = -30\nEnd\n",
		1, &error));
	CHECK(s.cloudEffectSeen());
	CHECK(s.cloudEffect().darkeningFactor.blue == 30.0f * (1.0f / 255.0f));
	CHECK(s.cloudEffect().darkeningFactorRain.red == 0.0f); // the static ctor's 0 (RW 0x614E2B leaves the numbers 0)
	CHECK(s.cloudEffect().lightningDuration.m_low == 10.0f);
	CHECK(s.cloudEffect().lightningDuration.m_high == 50.0f);
	CHECK(s.cloudEffect().lightningLightPosition[1].y == 113.0f);
	CHECK(s.cloudEffect().angle == -30);
	REQUIRE(s.loadText(nullptr, "map.ini", "CloudEffect\n  LightningChance = 0.5\nEnd\n", 2, &error));
	CHECK(s.cloudEffect().lightningChance == 0.5f);
	CHECK(s.cloudEffect().darkeningFactor.red == 100.0f * (1.0f / 255.0f)); // from the defaults
	CHECK(s.cloudEffectDefaults().lightningChance == 0.005f);       // load type 2 does not save
	REQUIRE(s.loadText(nullptr, "map2.ini", "CloudEffect\n  DarkeningRate = 7\nEnd\n", 2, &error));
	CHECK(s.cloudEffect().lightningChance == 0.005f); // the first map block is gone: each starts from the defaults
	CHECK(s.cloudEffect().darkeningRate == 7);
}

TEST_CASE("weather1 snow: updateIniSettings (RW 0x600ADE) draws the starting heights from MSVCR71 rand() in cell order; the time update RW 0x4943E1")
{
	CrtRand r;
	CHECK(r.rand() == 41); // MSVCR71 srand(1) sequence
	CHECK(r.rand() == 18467);
	CHECK(r.rand() == 6334);
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini", kRetailLikeWeather, 1, &error));
	SnowManager m;
	m.init(s.finalSetting());
	CHECK(m.startingHeights()[0] == 41.0f);           // 41 % 300
	CHECK(m.startingHeights()[1] == 167.0f);          // 18467 % 300
	CHECK(m.startingHeights()[2] == 34.0f);           // 6334 % 300
	CHECK(m.velocity() == 50.0f);
	CHECK(m.fullTimePeriod() == 6.0f); // 300 / 50
	CHECK(m.spacing() == 30.0f);
	CHECK_FALSE(m.snowing()); // IsSnowing = no
	// 500 ms: time = (float)(500 * 0.001f + 0) = 0.5f, travelled 0.5 * 50
	m.update(s.finalSetting(), 500, 0);
	CHECK(m.time() == 0.5f);
	CHECK(m.heightTraveled() == 25.0f);
	// 1000 ms more: (float)(1000 * 0.001f + 0.5f) = 1.5f (the product's 4.7e-8 is below half an ulp), travel 25 + 1.0 * 50
	m.update(s.finalSetting(), 1000, 0);
	CHECK(m.time() == 1.5f);
	CHECK(m.heightTraveled() == 75.0f);
	// the fmod wrap of the period: 4.5 s more make 6.0 -> 0, the travel goes back with the time (75 + (0 - 1.5) * 50)
	m.update(s.finalSetting(), 4500, 0);
	CHECK(m.time() == 0.0f);
	CHECK(m.heightTraveled() == 0.0f);
	// 5.5 s from 0.5: 5500 * 0.001f + 0.5f = 6.00000026 on the x87, stored as the float above 6.0 (6.00000048f): fmod leaves 4.76837158e-07f
	SnowManager m2;
	m2.init(s.finalSetting());
	m2.update(s.finalSetting(), 500, 0);
	m2.update(s.finalSetting(), 5500, 0);
	CHECK(m2.time() == (float)std::fmod((double)6.00000048f, 6.0));
	CHECK(m.phase() == 0);
}

TEST_CASE("weather1 snow: the Freezing Rain spell ramp (RW 0x49405A / 0x493563 / 0x492E5F) with retail weather.ini's values")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini", kRetailLikeWeather, 1, &error));
	const WeatherSetting &w = s.finalSetting();
	SnowManager m;
	m.init(w);
	// the weather turns RAINY (Freezing Rain's ChangeWeather): phase 1 and snowing at once, frames = SpellDuration
	m.clientFrame(w, 2);
	CHECK(m.phase() == 1);
	CHECK(m.snowing());
	CHECK(m.spellFrames() == 500);
	CHECK(m.followedWeather() == 2);
	CHECK(m.spacing() == 30.0f); // the blend runs from the next frame on
	// frame 2: frames 499, f = 1 / 500, b = f / 0.2 = 0.01: spacing (20 - 30) * b + 30, velocity (250 - 150) * b + 150
	m.clientFrame(w, 2);
	CHECK(m.spellFrames() == 499);
	const float f1 = (float)(500 - 499) / 500.0f;
	const float b1 = f1 / 0.2f;
	CHECK(m.spacing() == (20.0f - 30.0f) * b1 + 30.0f);
	CHECK(m.velocity() == (250.0f - 150.0f) * b1 + 150.0f);
	for (int i = 0; i < 99; ++i)
	{
		m.clientFrame(w, 2);
	}
	// f = 100 / 500 = 0.2f is not above RampControl.x: still phase 1 at b = 1
	CHECK(m.spellFrames() == 400);
	CHECK(m.phase() == 1);
	CHECK(m.spacing() == 20.0f);
	m.clientFrame(w, 2); // f = 101 / 500 > 0.2: the peak
	CHECK(m.phase() == 2);
	CHECK(m.spacing() == 20.0f);
	CHECK(m.velocity() == 250.0f);
	CHECK(m.spellFrames() == 399);
	m.clientFrame(w, 2); // phase 2 does not count
	CHECK(m.spellFrames() == 399);
	// the weather ends: phase 3 with frames = (int)((1 - 0.8f) * 500) = 99, the new weather followed
	m.clientFrame(w, 0);
	CHECK(m.phase() == 3);
	CHECK(m.spellFrames() == (int)((1.0f - 0.8f) * 500.0f));
	CHECK(m.followedWeather() == 0);
	for (int i = 0; i < 200 && m.phase() != 0; ++i)
	{
		m.clientFrame(w, 0);
	}
	// finish without IsSnowing: snowing off, spacing = the initial ramp spacing, velocity = the PEAK RAMP SPACING (+0x68, retail's own choice)
	CHECK(m.phase() == 0);
	CHECK_FALSE(m.snowing());
	CHECK(m.spacing() == 30.0f);
	CHECK(m.velocity() == 20.0f);
	CHECK(m.spellFrames() == 0);
}

TEST_CASE("weather1 snow: the drops' lightning (RW 0x492C35) and its gray (RW 0x492CE9) only while the ramp runs")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini",
		"Weather\n  SnowEnabled = yes\n  IsSnowing = yes\n  LightningEnabled = yes\n  LightningChance = 1.0\n  LightningDuration = 10\n"
		"  LightningFactor = 0.5 0.5\n  SnowSpacing = 30\n  SnowBoxHeight = 300\n  SnowSpeed = 50\nEnd\n",
		1, &error));
	const WeatherSetting &w = s.finalSetting();
	SnowManager m;
	m.init(w); // 4096 rand() calls
	CrtRand mirror;
	for (int i = 0; i < 4096; ++i)
	{
		mirror.rand();
	}
	m.clientFrame(w, 0);
	CHECK(m.lightningOn());
	mirror.randomFloat(); // the chance roll
	const float r = mirror.randomFloat();
	CHECK(m.lightningFrames() == (int)(((double)r * (double)0.50000006f + (double)0.7f) * 10.0 + 0.5));
	W3DClientRandom rnd(RandomAlgorithm::RotWK_GameDat_LCG);
	// phase 0: the gray stays 255 although the lightning is on
	CHECK(m.drawParams(w, 100, 100, 10, rnd).color == 0xFFFFFFFFu);
	// the ramp (RAINY) running: the gray is LightningFactor * 255 (0.5 0.5: no draw) = 127
	m.clientFrame(w, 2);
	CHECK(m.phase() == 1);
	CHECK(m.drawParams(w, 100, 100, 10, rnd).color == 127u * 0x10101u + 0xFF000000u);
}

// review (#59): a map's SnowSpacing is any float its Weather block writes; drawParams bounds it (at least 1 as retail's max, at most
// SnowManager::kMaxSnowSpacing) and multiplies in 64 bits, so 65536 (squared: 2^32, an int's wrap to a zero divisor) and larger draw no particle
TEST_CASE("weather1 snow: a map's SnowSpacing of any size draws without an overflow or a zero divisor")
{
	struct Case
	{
		const char *spacing;
		int expected; // the drawn spacing
	};
	for (const Case &c : { Case{ "65536", 65536 }, Case{ "1000000", 1000000 }, Case{ "1e30", (int)SnowManager::kMaxSnowSpacing }, Case{ "0.5", 1 }, Case{ "-5", 1 } })
	{
		CAPTURE(c.spacing);
		WeatherSettingStore s;
		std::string error;
		REQUIRE_MESSAGE(s.loadText(nullptr, "weather.ini", std::string("Weather\n  SnowEnabled = yes\n  IsSnowing = yes\n  SnowSpacing = ") + c.spacing + "\nEnd\n", 1, &error),
			error);
		SnowManager m;
		m.init(s.finalSetting());
		W3DClientRandom rnd(RandomAlgorithm::RotWK_GameDat_LCG);
		const SnowDrawParams p = m.drawParams(s.finalSetting(), 100, 100, 10, rnd);
		CHECK(p.spacing == c.expected);
		if (c.expected == 1)
		{
			CHECK(p.count == p.stepX * p.stepY);
		}
		else
		{
			CHECK(p.count == 0); // wider than the map's 800 world units: no particle
			CHECK(p.columns == 1);
		}
	}
}

// review (#59): NumberTiles = 1 and SnowSpacing = 1 on a 600 x 600 map asked for 33.6 million particles, which crashed the client; drawParams keeps a
// tile at SnowManager::kMaxSnowParticles by widening the spacing to the least one whose grid fits
TEST_CASE("weather1 snow: a tile's particles stay at kMaxSnowParticles, the spacing widened evenly; the stock field is unchanged")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE_MESSAGE(s.loadText(nullptr, "weather.ini", "Weather\n  SnowEnabled = yes\n  IsSnowing = yes\n  SnowSpacing = 1\n  NumberTiles = 1\nEnd\n", 1, &error),
		error);
	SnowManager m;
	m.init(s.finalSetting());
	W3DClientRandom rnd(RandomAlgorithm::RotWK_GameDat_LCG);
	// a 600 x 600 height map with a border of 10: one tile of (600 - 20) * 10 = 5800 world units a side, 33,640,000 points at spacing 1
	const SnowDrawParams p = m.drawParams(s.finalSetting(), 600, 600, 10, rnd);
	CHECK(p.tiles == 1);
	CHECK(p.stepX == 5800);
	CHECK(p.spacing == 23);  // 33,640,000 / 22^2 = 69,504 is above 65,536; / 23^2 = 63,591 is not
	CHECK(p.count == 63591);
	CHECK(p.columns == 253); // ceil(5800 / 23)
	// the stock Weather (SnowSpacing 30, NumberTiles 4) on the same map keeps its spacing: 1450^2 / 30^2 = 2336
	WeatherSettingStore stock;
	REQUIRE_MESSAGE(stock.loadText(nullptr, "weather.ini", "Weather\n  SnowEnabled = yes\n  IsSnowing = yes\n  SnowSpacing = 30\n  NumberTiles = 4\nEnd\n", 1, &error),
		error);
	SnowManager m2;
	m2.init(stock.finalSetting());
	const SnowDrawParams q = m2.drawParams(stock.finalSetting(), 600, 600, 10, rnd);
	CHECK(q.spacing == 30);
	CHECK(q.count == 2336);
}

TEST_CASE("weather1 snow: the quad path's grid (RW 0x4936CB) and one particle's position")
{
	WeatherSettingStore s;
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini", "Weather\n  SnowEnabled = yes\n  IsSnowing = yes\n  SnowSpacing = 30\n  SnowQuadSize = 10\nEnd\n", 1, &error));
	SnowManager m;
	m.init(s.finalSetting());
	W3DClientRandom rnd(RandomAlgorithm::RotWK_GameDat_LCG);
	// a 100 x 100 height map with a border of 10: dims (int)((float)(100 - 20) * 10.0f) = 800, 4 tiles of 200, spacing 30
	const SnowDrawParams p = m.drawParams(s.finalSetting(), 100, 100, 10, rnd);
	CHECK(p.draw);
	CHECK(p.tiles == 4);
	CHECK(p.stepX == 200);
	CHECK(p.stepY == 200);
	CHECK(p.spacing == 30);
	CHECK(p.count == 44);  // (200 * 200) / (30 * 30)
	CHECK(p.columns == 7); // 0, 30, ... 180
	CHECK(p.quadHalf == 10.0f * 0.707f);
	// one point by hand: grid (1, 2) = world (30, 60), noise ((60 - 32) & 63) * 64 + ((30 - 32) & 63) = 28 * 64 + 62 = 1854
	std::vector<float> heights(4096, 0.0f);
	heights[1854] = 120.0f;
	SnowDrawParams q = p;
	q.heights = heights.data();
	q.heightTraveled = 500.0f;
	q.box = 300.0f;
	q.ceiling = 300.0f;
	q.amplitude = 0.0f;
	q.driftX = 5.0f;
	q.driftY = 7.0f;
	q.windStepX = 2.0f;
	q.windStepY = 3.0f;
	float c[3];
	SnowManager::particleCenter(q, 1, 2, c);
	// h = 620, cycles = floor(620 / 300) = 2, z = 300 - (620 - 600) = 280; x = (5 - 2 * 2) + 30, y = (7 - 2 * 3) + 60
	CHECK(c[0] == 31.0f);
	CHECK(c[1] == 61.0f);
	CHECK(c[2] == 280.0f);
	// the table sine: exact at the table's points
	CHECK(SnowManager::fastSin(256.0f) == 1.0f);
	CHECK(SnowManager::fastSin(0.0f) == 0.0f);
}

TEST_CASE("weather1 cloud: Darkness (CLOUDY) darkens over 255 client frames, relights every 10th, lightning after four hits, stops at its end (RW 0x497D00 / 0x494DBA)")
{
	WeatherSettingStore s;
	s.setSoundValidator(anySound({}));
	std::string error;
	REQUIRE(s.loadText(nullptr, "weather.ini", "Weather\nEnd\nWeatherData CLOUDY\n  HasLightning = Yes\nEnd\n", 1, &error));
	REQUIRE(s.loadText(nullptr, "environment.ini",
		"CloudEffect\n  DarkeningFactor = R:100 G:100 B:30\n  DarkeningFactorRain = R:51 G:51 B:51\n  LightningChance = 1.0\n  LightningDuration = 3 3\n"
		"  LightningIntensity = 0.95 0.95\n  LightningFX = FX_Strike\nEnd\n",
		1, &error));
	CloudEffect e;
	W3DClientRandom rnd(RandomAlgorithm::RotWK_GameDat_LCG);
	auto step = [&](std::uint32_t frame, int w) { return e.update(frame, w, s, 0.0f, 0.0f, 4000.0f, 3000.0f, rnd, true); };
	CloudEffectFrame f = step(1, 1);
	CHECK(e.phase() == 1);
	CHECK(e.counter() == 254);
	CHECK(f.active);
	CHECK_FALSE(f.relightTerrain);
	const float r = 100.0f * (1.0f / 255.0f);
	CHECK(f.darkening[0] == r * (float)(1.0 - 254.0 * 0.00392156862745098));
	// the same client frame again: nothing runs
	step(1, 1);
	CHECK(e.counter() == 254);
	int relights = 0;
	for (std::uint32_t k = 2; k <= 254; ++k)
	{
		f = step(k, 1);
		relights += f.relightTerrain ? 1 : 0;
		if (k == 10)
		{
			CHECK(f.relightTerrain); // a multiple of 10
		}
	}
	CHECK(relights == 25); // frames 10, 20, ... 250
	CHECK(e.counter() == 1);
	CHECK(e.phase() == 1);
	f = step(255, 1); // the counter reaches 0: phase 2, the first lightning roll hits
	CHECK(e.phase() == 2);
	CHECK(e.counter() == 0);
	CHECK(e.lightningHits() == 1);
	CHECK_FALSE(e.lightningOn());
	CHECK(f.darkening[0] == r);
	CHECK(f.darkening[2] == 30.0f * (1.0f / 255.0f));
	step(256, 1);
	step(257, 1);
	CHECK(e.lightningHits() == 3);
	f = step(258, 1); // the fourth hit (more than 3): the lightning
	CHECK(e.lightningOn());
	CHECK(e.lightningFrames() == 3);
	CHECK(f.lightningIntensity == 0.89999998f); // LightningIntensity 0.95 clamped (RW 0xBDE198)
	CHECK(f.lightColor[1] == 0.89999998f);
	CHECK(f.playFX);
	CHECK(f.fxName == "FX_Strike");
	CHECK(f.fxPosition.x == 2000.0f); // the extent's centre
	CHECK(f.fxPosition.y == 1500.0f);
	step(259, 1);
	f = step(260, 1);
	CHECK(e.lightningFrames() == 1);
	f = step(261, 1); // frames reach 0: the lightning stops, with the darkening colour 0 for this frame
	CHECK_FALSE(e.lightningOn());
	CHECK(e.lightningHits() == 0);
	CHECK(f.darkening[0] == 0.0f);
	CHECK(f.lightColor[0] == 0.0f);
	f = step(262, 1);
	CHECK(f.darkening[0] == r);
	// the weather turns RAINY: phase 3 following RAINY, the rain darkening, no lightning (RAINY has none here)
	f = step(263, 2);
	CHECK(e.phase() == 3);
	CHECK(e.followedWeather() == 2);
	CHECK(e.counter() == 1);
	CHECK(f.darkening[0] == (51.0f * (1.0f / 255.0f)) * (float)(1.0 - 1.0 * 0.00392156862745098));
	for (std::uint32_t k = 264; k < 264 + 253; ++k)
	{
		step(k, 2);
	}
	CHECK(e.counter() == 254);
	f = step(600, 2);
	CHECK(e.phase() == 0);
	CHECK(e.counter() == 255);
	CHECK_FALSE(f.active);
}

TEST_CASE("weather1 cloud: the lightning shadow colour (RW 0x614AB6) and light position (RW 0x61501A)")
{
	CloudEffectSettings s;
	s.shadowColor = { 0.0f, 0.0f, 0.0f };
	s.shadowIntensity = 0.6f;
	// a = ftol((1 - 0.6) * 255) = 101 (0.39999998 * 255 = 101.99999); rgb = (int)(0 * 0.6 * 255 + 101) = 101
	CHECK(CloudEffect::lightningShadowColor(s) == 0xFF656565u);
	float d[3];
	CloudEffect::lightDirection(0.0f, 0.0f, d); // x 0, y 0: (-sin(pi/2) cos 0, -sin(pi/2) sin 0, -cos(pi/2)) * 10000
	CHECK(d[0] == -10000.0f);
	CHECK(d[1] == 0.0f);
}

TEST_CASE("weather1 client: the weather sound follows TheGlobalWeatherSystem's weather (RW 0x719DEE), the reset removes it")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m || !m->fs)
	{
		retailtest::printSkip("weather1 client: the weather sound");
		return;
	}
	WeatherClient c;
	std::vector<std::string> log;
	WeatherClient::Audio audio;
	audio.start = [&](const std::string &n) {
		log.push_back("start " + n);
		return 77u;
	};
	audio.stop = [&](std::uint32_t h) { log.push_back("stop " + std::to_string(h)); };
	c.setAudio(audio);
	std::string error;
	REQUIRE_MESSAGE(c.load(*m->fs, "map mp withered heath", anySound({ "RainStereoLoop" }), 200, 200, 30, 1400.0f, 1400.0f, &error), error);
	W3DClientRandom rnd(RandomAlgorithm::RotWK_GameDat_LCG);
	c.advance(1.0 / 30.0, 0, rnd);
	CHECK(log.empty()); // NONE has no sound and was the weather already
	c.advance(1.0 / 30.0, 2, rnd);
	REQUIRE(log.size() == 2);
	CHECK(log[0] == "stop 1"); // RW 0x719DEE: removeAudioEvent(+0x14) first (1 at construction)
	CHECK(log[1] == "start RainStereoLoop");
	CHECK(c.soundHandle() == 77u);
	c.advance(1.0 / 30.0, 4, rnd); // SUNNY: no sound
	REQUIRE(log.size() == 3);
	CHECK(log[2] == "stop 77");
	CHECK(c.soundHandle() == 1u);
	c.shutdown();
	CHECK(log.back() == "stop 1");
}

TEST_CASE("weather1 retail: weather.ini, environment.ini and a snowy map's map.ini as RotWK 2.01 ships them")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m || !m->fs)
	{
		retailtest::printSkip("weather1 retail: weather.ini");
		return;
	}
	WeatherSettingStore s;
	s.setSoundValidator(anySound({ "RainStereoLoop" }));
	std::string error;
	REQUIRE_MESSAGE(s.loadForMap(*m->fs, "map mp withered heath", &error), error);
	REQUIRE(s.files().size() == 5);
	CHECK(s.files()[4] == "maps\\map mp withered heath\\map.ini");
	const WeatherSetting &base = s.baseSetting();
	CHECK(base.snowTexture == "EXRainDrop.tga");
	CHECK(base.snowEnabled);
	CHECK_FALSE(base.isSnowing);
	CHECK(base.spellDuration == 500);
	CHECK(base.rampSpeed.y == 250.0f);
	CHECK_FALSE(base.lightningEnabled);
	CHECK(s.overrideCount() == 1);
	const WeatherSetting &w = s.finalSetting();
	CHECK(w.isSnowing);
	CHECK(w.snowTexture == "EXSnowFlake2.tga");
	CHECK(w.snowBoxHeight == 500.0f);
	CHECK(w.snowAmplitude == 20.0f);
	CHECK(w.snowXYSpeed.x == 40.0f);
	CHECK(w.snowSpeed == 60.0f);
	CHECK(w.hardwareFogEnable);
	CHECK(w.hardwareFogEnd == 2000.0f);
	CHECK(w.spellDuration == 500); // the override's copy of the base
	CHECK(s.weatherData(WEATHER_TYPE_RAINY).weatherSound == "RainStereoLoop");
	CHECK(s.weatherData(WEATHER_TYPE_RAINY).hasLightning);
	CHECK(s.weatherData(WEATHER_TYPE_CLOUDYRAINY).hasLightning);
	CHECK_FALSE(s.weatherData(WEATHER_TYPE_CLOUDY).hasLightning); // "Lightning for rain only. Bug #9644"
	CHECK(s.weatherData(WEATHER_TYPE_NONE).weatherSound.empty());
	const CloudEffectData &ce = s.cloudEffect();
	CHECK(ce.darkCloudTexture == "exdarkcloudc.tga");
	CHECK(ce.darkeningFactor.red == 100.0f * (1.0f / 255.0f));
	CHECK(ce.darkeningFactor.blue == 30.0f * (1.0f / 255.0f));
	CHECK(ce.lightningChance == 0.005f);
	CHECK(ce.lightningFX == "FX_LightningThunderStrike");
	CHECK(ce.lightningLightPosition[2].y == 126.0f);
	// a map without a map.ini Weather block keeps the base (evendim has none)
	WeatherSettingStore e;
	e.setSoundValidator(anySound({ "RainStereoLoop" }));
	REQUIRE_MESSAGE(e.loadForMap(*m->fs, "map mp evendim", &error), error);
	CHECK(e.finalSetting().snowTexture == "EXRainDrop.tga");
}

TEST_CASE("weather1 retail: every map with a map.ini / solo.ini loads its weather; the snowing maps and their textures")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m || !m->fs)
	{
		retailtest::printSkip("weather1 retail: every map");
		return;
	}
	FilenameList files;
	m->fs->getFileListInDirectory("maps\\", "maps\\", "*.ini", files, true);
	std::set<std::string> dirs;
	for (const std::string &f : files)
	{
		std::string lower = f;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)(c == '/' ? '\\' : std::tolower(c)); });
		const size_t cut = lower.rfind('\\');
		const std::string leaf = cut == std::string::npos ? lower : lower.substr(cut + 1);
		if (leaf == "map.ini" || leaf == "solo.ini")
		{
			dirs.insert(lower.substr(0, cut));
		}
	}
	std::vector<std::string> failed, snowing;
	size_t overrides = 0;
	for (const std::string &d : dirs)
	{
		WeatherSettingStore s;
		s.setSoundValidator(anySound({ "RainStereoLoop" }));
		std::string error;
		if (!s.loadForMap(*m->fs, d, &error))
		{
			failed.push_back(d + ": " + error);
			continue;
		}
		overrides += s.overrideCount();
		if (s.finalSetting().isSnowing)
		{
			snowing.push_back(d.substr(5) + "=" + s.finalSetting().snowTexture);
		}
	}
	MESSAGE(dirs.size() << " map folders, " << overrides << " Weather overrides, snowing: " << snowing.size());
	CHECK(failed.empty());
	for (const std::string &f : failed)
	{
		MESSAGE(f);
	}
	CHECK(std::find(snowing.begin(), snowing.end(), "map mp withered heath=EXSnowFlake2.tga") != snowing.end());
	CHECK(std::find(snowing.begin(), snowing.end(), "map good ettenmoors=EXSnowFlake1.tga") != snowing.end());
	CHECK(std::find(snowing.begin(), snowing.end(), "map ang barrow wights=EXSnowFlake2.tga") == snowing.end()); // its snow block is commented out
}

TEST_CASE("weather1 logic: the presented snapshot carries TheGlobalWeatherSystem's weather; the stop lines")
{
	logictest::LogicWorld f;
	GameLogic &logic = *f.logic;
	logic.weather().setWeather(2, 0, 10);
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(logic, nullptr, false, 0);
	CHECK(snap->globalWeather == 2);
	const std::vector<std::string> lines = WeatherClient::stopLines();
	REQUIRE(lines.size() == 4);
	CHECK(lines[0].rfind("[S-4890] ", 0) == 0);
	CHECK(lines[1].rfind("[S-4891] ", 0) == 0);
	CHECK(lines[2].rfind("[S-4892] ", 0) == 0);
	CHECK(lines[3].rfind("[S-4893] ", 0) == 0);
}

TEST_CASE("weather1 time of day: at NIGHT a new drawable takes its player's night colour (Drawable::friend_bindToObject RW 0x674566 -> RW 0x68B71F)")
{
	logictest::LogicWorld f;
	REQUIRE(f.w.load("Object WeatherNightThing\nEnd\n", INI_LOAD_OVERWRITE, "night.ini").empty());
	Player *alice = f.players.findPlayerWithName("Alice");
	REQUIRE(alice);
	alice->setPlayerColor(0xFF112233u);
	alice->setPlayerNightColor(0xFF445566u);
	REQUIRE(alice->hasTeamColor());
	ClientEventRecorder rec(*f.logic);
	Object *day = f.make("WeatherNightThing", f.teamOf("Alice"));
	rec.objectCreated(*day);
	f.logic->settings().night = true;
	Object *night = f.make("WeatherNightThing", f.teamOf("Alice"));
	rec.objectCreated(*night);
	const std::vector<ClientEvent> ev = rec.take(false);
	std::vector<std::uint32_t> colours;
	for (const ClientEvent &e : ev)
	{
		if (e.kind == ClientEvent::CREATED)
		{
			REQUIRE(e.hasHouseColor);
			colours.push_back(e.houseColor);
		}
	}
	REQUIRE(colours.size() == 2);
	CHECK(colours[0] == 0xFF112233u); // getIndicatorColor: + 0x2A0
	CHECK(colours[1] == 0xFF445566u); // the time of day is NIGHT: + 0x2A4
}

namespace
{
// the FX-2 world interface with only the weather answered (lane WEATHER-1: the FXList nuggets' Weather filter reads it)
struct SnowyWorld : FXPlaybackWorld
{
	int weather = 1;
	std::uint32_t logicFrame() const override { return 1; }
	float groundHeight(float, float) override { return 0.0f; }
	bool isWater(float, float) override { return false; }
	int shroudStatusAt(const Coord3D &) override { return 0; }
	bool objectShroudedForLocalPlayer(const FXObject &) override { return false; }
	FXParticleSystem::ParticleAttachInfo attachedDrawable(std::uint32_t, const std::string &) override { return FXParticleSystem::ParticleAttachInfo(); }
	FXParticleSystem::ParticleAttachInfo attachedObject(std::uint32_t, const std::string &, int) override { return FXParticleSystem::ParticleAttachInfo(); }
	bool boneWorldMatrix(const FXObject &, const std::string &, Matrix3D &) override { return false; }
	std::vector<FXBoneTransform> boneWorldTransforms(const FXObject &, const std::string &, int, int) override { return {}; }
	bool playSound(const std::string &, const Coord3D *, int) override { return false; }
	int mapWeather() const override { return weather; }
};
} // namespace

TEST_CASE("weather1 fx: an FXList nugget with Weather = SNOWY plays on a snowy map only (TheWritableGlobalData + 0x138, the map's WorldInfo weather)")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m || !m->fs)
	{
		retailtest::printSkip("weather1 fx: the nugget weather");
		return;
	}
	FXPlayback fx(*m->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	const std::vector<std::string> errors =
		fx.loadIniText("weather1.ini", "FXList FX_Weather1Snowy\n  ViewShake\n    Type = SUBTLE\n    Weather = SNOWY\n  End\nEnd\n");
	REQUIRE(errors.empty());
	SnowyWorld world;
	fx.setWorld(&world);
	Coord3D at;
	REQUIRE(fx.playFXList("FX_Weather1Snowy", at, false));
	CHECK(fx.events().size() == 1);
	fx.clearEvents();
	world.weather = 0;
	REQUIRE(fx.playFXList("FX_Weather1Snowy", at, false));
	CHECK(fx.events().empty());
	fx.setWorld(nullptr);
}
