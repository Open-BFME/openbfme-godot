// OpenBFME unit tests. GPL-3.0.
// Lane OPTS-2: Options.ini (OptionPreferences: a retail file round-trips byte for byte, unknown keys are kept, the OpenBFME keys carry the "OpenBFME"
// prefix), the getters GlobalData::applyOptionPreferences reads (RW 0x641DED: ScrollFactor RW 0x6E59A9, Brightness -> gamma RW 0x641E8B, Resolution
// RW 0x6E5F27) and the detail levels taking effect (GameLODManager: the Custom record RW 0x6019D1, setStaticLODLevel RW 0x6020B4 / apply RW 0x601C62,
// init RW 0x602165 / RW 0x4465D8). The expected values are the binary's arithmetic on retail GameLOD.ini (the retail tests SKIP loudly without the
// installs) or on records the test fills.

#include "doctest.h"
#include "AptRetail.h"

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "GameClient/GameLODManager.h"
#include "GameClient/OptionPreferences.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
// GodotAptPlayer's boot reads GameLOD.ini the same way
void loadGameLOD(AptRetail &mount, GameLODManager &lod)
{
	INIEnvironment env;
	env.fileSystem = &mount.fs;
	INIBlockRecorder recorder;
	RegisterRecordingBlockStubs(env.blocks, recorder, { "StaticGameLOD" }, StubExtent::Lenient);
	lod.registerBlocks(env.blocks);
	INI ini(env);
	for (const std::string &f : GameLODManager::loadOrder())
	{
		ini.load(f, INI_LOAD_OVERWRITE);
	}
}

std::string readFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}
} // namespace

TEST_CASE("opts2 Options.ini: a file retail wrote round-trips byte for byte; unknown keys are kept; OpenBFME keys carry the prefix")
{
	// what RotWK's UserPreferences::write leaves (keys sorted by strcmp, "%s = %s\n"), retail's entries of every page plus keys another program wrote
	const std::string retail = "AllHealthBars = no\nAlternateMouseSetup = yes\nAmbientVolume = 60.000000\nAnimationLOD = High\nAudioLOD = High\n"
							   "Brightness = 50\nDecalLOD = High\nEffectsLOD = UltraHigh\nFirewallPortOverride = 0\nGameSpyIPAddress = 192.168.1.5\n"
							   "IdealStaticGameLOD = High\nModelLOD = UltraHigh\nMovieVolume = 80.000000\nMusicVolume = 37.500000\n"
							   "Resolution = 1920 1080\nSFXVolume = 80.000000\nScrollFactor = 50\nSendDelay = no\nShaderLOD = UltraHigh\n"
							   "ShadowLOD = UltraHigh\nStaticGameLOD = Custom\nTerrainLOD = High\nTextureQualityLOD = High\nUseEAX3 = no\n"
							   "VoiceVolume = 75.000000\nWaterLOD = UltraHigh\nZZSomeModTool = keep me\n";
	OptionPreferences p;
	p.parse(retail);
	CHECK(p.text() == retail);
	CHECK(p.get("ZZSomeModTool") == "keep me");
	// the file on disk, through load / save
	const std::string file = (std::filesystem::temp_directory_path() / "openbfme-opts2-roundtrip.ini").string();
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << retail;
	}
	OptionPreferences q;
	REQUIRE(q.load(file));
	// an OpenBFME extra written beside them: the retail lines stay byte-identical, the extra sorts in with its prefix
	q.set("OpenBFMEGameClock", "yes");
	REQUIRE(q.save(file));
	std::string written = readFile(file);
	const std::string extra = "OpenBFMEGameClock = yes\n";
	const std::size_t at = written.find(extra);
	REQUIRE(at != std::string::npos);
	CHECK(written.substr(0, at) + written.substr(at + extra.size()) == retail);
	q.erase("OpenBFMEGameClock");
	REQUIRE(q.save(file));
	CHECK(readFile(file) == retail);
	std::remove(file.c_str());
	// the prefix: every OpenBFME key the C++ side names; UI-2's unprefixed SoftParticles is renamed once (its value kept)
	CHECK(std::string(OptionPreferences::kSoftParticles).rfind(OptionPreferences::kOpenBFMEPrefix, 0) == 0);
	OptionPreferences legacy;
	legacy.parse("Brightness = 60\nSoftParticles = no\n");
	const std::vector<std::string> renamed = legacy.migrateLegacyKeys();
	REQUIRE(renamed.size() == 1);
	CHECK(renamed[0] == "SoftParticles");
	CHECK(legacy.text() == "Brightness = 60\nOpenBFMESoftParticles = no\n");
	CHECK(legacy.migrateLegacyKeys().empty());
	OptionPreferences both;
	both.parse("OpenBFMESoftParticles = yes\nSoftParticles = no\n");
	both.migrateLegacyKeys();
	CHECK(both.text() == "OpenBFMESoftParticles = yes\n"); // the prefixed key wins
}

TEST_CASE("opts2 OptionPreferences: ScrollFactor (RW 0x6E59A9), Brightness and the display gamma (RW 0x6E5ECD, RW 0x641E8B), Resolution (RW 0x6E5F27)")
{
	OptionPreferences p;
	CHECK(p.getScrollFactor(1.0f) == 1.0f); // missing: GlobalData + 0xAFC
	p.set("ScrollFactor", "50");
	CHECK(p.getScrollFactor(1.0f) == 1.0f);
	p.set("ScrollFactor", "25");
	CHECK(p.getScrollFactor(1.0f) == 25.0f * 0.02f);
	p.set("ScrollFactor", "-3");
	CHECK(p.getScrollFactor(1.0f) == 0.02f); // below 0 -> 1
	p.set("ScrollFactor", "400");
	CHECK(p.getScrollFactor(1.0f) == 2.0f); // above 100 -> 100
	CHECK(p.getBrightness() == 50.0f);
	p.set("Brightness", "75");
	CHECK(p.getBrightness() == 75.0f);
	CHECK(OptionPreferences::displayGamma(50, 1.0f) == 1.0f);
	CHECK(OptionPreferences::displayGamma(50, 1.3f) == 1.3f); // 50 stores nothing
	CHECK(OptionPreferences::displayGamma(0, 1.0f) == 0.6f);
	CHECK(OptionPreferences::displayGamma(-5, 1.0f) == 0.6f);
	CHECK(OptionPreferences::displayGamma(25, 1.0f) == 1.0f - 25.0f * 0.008f);
	CHECK(OptionPreferences::displayGamma(100, 1.0f) == 2.0f);
	CHECK(OptionPreferences::displayGamma(75, 1.0f) == 25.0f * 0.02f + 1.0f);
	int x = 7, y = 9;
	CHECK_FALSE(p.getResolution(&x, &y));
	CHECK(x == 7);
	p.set("Resolution", "1280 720");
	CHECK(p.getResolution(&x, &y));
	CHECK(x == 1280);
	CHECK(y == 720);
	p.set("Resolution", "1280x720");
	x = 1;
	CHECK_FALSE(p.getResolution(&x, &y)); // "%d%d" reads one number
	CHECK(x == 1);
}

TEST_CASE("opts2 GameLODManager: setStaticLODLevel (RW 0x6020B4) and the apply (RW 0x601C62) on records the test fills")
{
	GameLODManager lod;
	OptionPreferences prefs;
	// the records start with the constructor's values (RW 0x601745): applying Custom gives them
	REQUIRE(lod.setStaticLODLevel(GameLODManager::kCustomLevel, prefs));
	const StaticLODState &s = lod.state();
	CHECK(s.level == GameLODManager::kCustomLevel);
	CHECK(s.maxParticleCount == 2500);
	CHECK(s.useShadowVolumes);
	CHECK(s.useShadowDecals);
	CHECK_FALSE(s.useShadowMapping);
	CHECK(s.terrainLOD == 2);
	CHECK(s.decals);
	CHECK_FALSE(s.decalsReduced);
	CHECK_FALSE(s.allHealthBars); // DecalLOD 2: Options.ini's AllHealthBars, missing false
	CHECK(lod.setStaticLODLevel(GameLODManager::kCustomLevel, prefs)); // Custom applies again
	CHECK_FALSE(lod.setStaticLODLevel(-1, prefs));
	CHECK_FALSE(lod.setStaticLODLevel(6, prefs));
	CHECK(lod.setStaticLODLevel(0, prefs));
	CHECK_FALSE(lod.setStaticLODLevel(0, prefs)); // the same level: nothing
	// AllHealthBars: strcmp with "yes"
	prefs.set("AllHealthBars", "YES");
	CHECK(lod.setStaticLODLevel(1, prefs));
	CHECK_FALSE(lod.state().allHealthBars);
	prefs.set("AllHealthBars", "yes");
	CHECK(lod.setStaticLODLevel(2, prefs));
	CHECK(lod.state().allHealthBars);
}

TEST_CASE("opts2 GameLODManager retail: each level of GameLOD.ini applied (RW 0x601C62), the Custom record from Options.ini (RW 0x6019D1), init (RW 0x602165)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	GameLODManager lod;
	loadGameLOD(mount, lod);
	OptionPreferences prefs;
	// VeryLow: DecalLOD Off -> no decals, every health bar; TextureReductionFactor 2; 500 particles; no shadows
	REQUIRE(lod.setStaticLODLevel(0, prefs));
	{
		const StaticLODState &s = lod.state();
		CHECK(s.maxParticleCount == 500);
		CHECK(s.textureReduction == 2);
		CHECK_FALSE(s.decals);
		CHECK(s.decalsReduced);
		CHECK(s.allHealthBars);
		CHECK_FALSE(s.useShadowDecals);
		CHECK_FALSE(s.useShadowMapping);
		CHECK_FALSE(s.showProps);
		CHECK(s.lowestModelLOD); // ModelLOD = Low is index 0 of the ModelLOD list (Low Medium High UltraHigh ...): + 0x24 = ModelLOD <= 0
		CHECK(s.minParticlePriority == 5); // VERY_LOW_OR_ABOVE
	}
	CHECK(lod.state().modelLOD == 0);
	// Medium: volumes and decals, distance textures (terrain 1), heat effects, 1500 particles
	REQUIRE(lod.setStaticLODLevel(2, prefs));
	{
		const StaticLODState &s = lod.state();
		CHECK(s.maxParticleCount == 1500);
		CHECK(s.useShadowVolumes);
		CHECK(s.useShadowDecals);
		CHECK_FALSE(s.useShadowMapping);
		CHECK(s.terrainLOD == 1);
		CHECK(s.useHeatEffects);
		CHECK(s.showProps);
		CHECK(s.textureReduction == 0);
		CHECK(s.maxTankTrackFadeDelay == 30000);
	}
	// UltraHigh: shadow mapping clears the volume and decal shadows it also names
	REQUIRE(lod.setStaticLODLevel(4, prefs));
	{
		const StaticLODState &s = lod.state();
		CHECK(s.useShadowMapping);
		CHECK_FALSE(s.useShadowVolumes);
		CHECK_FALSE(s.useShadowDecals);
		CHECK(s.maxParticleCount == 3000);
		CHECK(s.minParticlePriority == 1); // ULTRA_HIGH_ONLY
		CHECK(s.highWater);
		CHECK(s.shaderMaterialReplacement);
	}
	// Custom from the nine settings: EffectsLOD Low (1) takes Low's particles; ShadowLOD Medium (2) volumes + decals; TerrainLOD Low (0): no props,
	// Low's tank tracks; TextureQualityLOD Medium (1) -> factor 1; DecalLOD Off
	prefs.parse("ModelLOD = High\nAnimationLOD = Low\nEffectsLOD = Low\nShadowLOD = Medium\nTerrainLOD = Low\nWaterLOD = Medium\nTextureQualityLOD = Medium\n"
				"ShaderLOD = High\nDecalLOD = Off\nAllHealthBars = no\n");
	lod.setCustomFromPreferences(prefs);
	REQUIRE(lod.setStaticLODLevel(GameLODManager::kCustomLevel, prefs));
	{
		const StaticLODState &s = lod.state();
		CHECK(s.modelLOD == 2);
		CHECK(s.animationDetail == 1);
		CHECK(s.effectsLOD == 1);
		CHECK_FALSE(s.useHeatEffects);
		CHECK(s.maxParticleCount == 500);
		CHECK(s.minParticlePriority == 4); // Low's LOW_OR_ABOVE
		CHECK(s.useShadowVolumes);
		CHECK(s.useShadowDecals);
		CHECK_FALSE(s.useShadowMapping);
		CHECK(s.terrainLOD == 0);
		CHECK_FALSE(s.showProps);
		CHECK(s.maxTankTrackEdges == 30);
		CHECK(s.waterLOD == 1);
		CHECK_FALSE(s.highWater);
		CHECK(s.showSoftWaterEdge);
		CHECK(s.textureReduction == 1);
		CHECK(s.shaderLOD == 2);
		CHECK(s.shaderMaterialReplacement);
		CHECK_FALSE(s.decals);
		CHECK(s.allHealthBars); // DecalLOD Off forces them
	}
	// a preset's nine settings written as Custom give the preset back (RW 0x601BBD then RW 0x6019D1), the fields the nine settings carry
	for (int level = 0; level < 5; ++level)
	{
		OptionPreferences p;
		const auto settings = lod.presetSettings(level);
		for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
		{
			AdvancedOptionPrefs::setSetting(p, i, settings[(size_t)i]);
		}
		GameLODManager a, b;
		loadGameLOD(mount, a);
		loadGameLOD(mount, b);
		a.setCustomFromPreferences(p);
		REQUIRE(a.setStaticLODLevel(GameLODManager::kCustomLevel, p));
		REQUIRE(b.setStaticLODLevel(level, p));
		CAPTURE(level);
		CHECK(a.state().modelLOD == b.state().modelLOD);
		CHECK(a.state().effectsLOD == b.state().effectsLOD);
		CHECK(a.state().maxParticleCount == b.state().maxParticleCount);
		CHECK(a.state().useShadowMapping == b.state().useShadowMapping);
		CHECK(a.state().terrainLOD == b.state().terrainLOD);
		CHECK(a.state().textureReduction == b.state().textureReduction);
		CHECK(a.state().decals == b.state().decals);
	}
	// init: StaticGameLOD names the level; Custom fills the record first
	{
		GameLODManager g;
		loadGameLOD(mount, g);
		OptionPreferences p;
		p.parse("StaticGameLOD = medium\n"); // GetEnumValue: case-insensitive
		std::string stop;
		CHECK(g.init(p, &stop) == 2);
		CHECK(stop.empty());
	}
	{
		GameLODManager g;
		loadGameLOD(mount, g);
		OptionPreferences p;
		p.parse("StaticGameLOD = Custom\nEffectsLOD = VeryLow\n");
		CHECK(g.init(p) == GameLODManager::kCustomLevel);
		CHECK(g.state().maxParticleCount == 500);
	}
	{
		// no StaticGameLOD: IdealStaticGameLOD (findStaticLODLevel RW 0x602C03's saved answer)
		GameLODManager g;
		loadGameLOD(mount, g);
		OptionPreferences p;
		p.parse("IdealStaticGameLOD = Low\n");
		std::string stop;
		CHECK(g.init(p, &stop) == 1);
		CHECK(stop.empty());
	}
	{
		// neither: the hardware detection is not ported (S-2481): the port takes its kDetectedLevel and says so
		GameLODManager g;
		loadGameLOD(mount, g);
		OptionPreferences p;
		std::string stop;
		CHECK(g.init(p, &stop) == GameLODManager::kDetectedLevel);
		CHECK(stop.rfind("[S-2481]", 0) == 0);
	}
}

TEST_CASE("opts2 stop S-3402: the detail effects the device does not draw are reported")
{
	const std::string r = GameLODManager::unappliedEffects();
	CHECK(r.rfind("[S-3402]", 0) == 0);
	CHECK(r.find("shadows") != std::string::npos);
}

TEST_CASE("opts2 Options.ini line endings: a CR LF file (RotWK's _wfopen L\"w\", RW 0x7B274C) round-trips byte for byte; an LF file stays LF; a new file is CR LF")
{
	// the retail format: keys in strcmp order, "%s = %s" and the text mode stream's CR LF
	const std::string retail = "AllHealthBars = no\r\nAlternateMouseSetup = no\r\nAmbientVolume = 60.000000\r\nAudioLOD = High\r\nBrightness = 50\r\n"
							   "FirewallPortOverride = 0\r\nIdealStaticGameLOD = UltraHigh\r\nMovieVolume = 80.000000\r\nMusicVolume = 70.000000\r\n"
							   "Resolution = 1024 768\r\nSFXVolume = 80.000000\r\nScrollFactor = 0\r\nSendDelay = no\r\nStaticGameLOD = High\r\n"
							   "UseEAX3 = no\r\nVoiceVolume = 75.000000\r\n";
	const std::string file = (std::filesystem::temp_directory_path() / "openbfme-opts2-crlf.ini").string();
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << retail;
	}
	OptionPreferences p;
	REQUIRE(p.load(file));
	CHECK(p.lineEnding() == "\r\n");
	CHECK(p.get("ScrollFactor") == "0");
	CHECK(p.getScrollFactor(1.0f) == 0.0f); // 0 is a value retail takes (atoi, not below 0)
	REQUIRE(p.save(file));
	CHECK(readFile(file) == retail);
	p.set("OpenBFMEGameClock", "yes");
	REQUIRE(p.save(file));
	const std::string written = readFile(file);
	CHECK(written.find("OpenBFMEGameClock = yes\r\n") != std::string::npos);
	CHECK(written.find("\n") == written.find("\r\n") + 1); // no bare LF
	std::remove(file.c_str());
	OptionPreferences lf;
	lf.parse("Brightness = 50\nScrollFactor = 20\n");
	CHECK(lf.text() == "Brightness = 50\nScrollFactor = 20\n");
	OptionPreferences fresh;
	fresh.set("Brightness", "50");
	CHECK(fresh.text() == "Brightness = 50\r\n");
	OptionPreferences missing;
	CHECK_FALSE(missing.load(file)); // a missing file: no entries, the new file's CR LF
	CHECK(missing.lineEnding() == "\r\n");
}
