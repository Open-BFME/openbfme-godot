// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GameLODManager (lane PLAY-2): the part the Options screen's advanced page reads: the StaticGameLOD presets of Data\INI\GameLOD.ini and the
// nine advanced options (ZH GameLODManager / StaticGameLODInfo, GameLOD.cpp, as RotWK changed them).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * TheGameLODManager (RW 0xDE3B84) holds six 0x4C-byte StaticGameLOD records at + 0, one per level of the name list RW 0xD9E6A4 (VeryLow, Low,
//     Medium, High, UltraHigh, Custom); + 0x1768 the current static level, + 0x17C4 the ideal one (the benchmark's, not ported: S-2481).
//   * the block StaticGameLOD (RW 0x60252D; BFME2 decomp Rva00202BB2Parse.cpp, tier A): the name is looked up in RW 0xD9E6A4 with a case-insensitive
//     compare (RW 0x6024E7 -> RW 0x4372C0); a name that is not there (or no manager) leaves the block's lines unread; else the record is filled by the
//     field table RW 0xBF9468 (23 rows: ModelLOD + 0x00 .. MinParticleSkipPriority + 0x48; parseIndexList RW 0x42E956, parseInt RW 0x42EC5E, parseBool
//     RW 0x42E558). Its index lists are NOT NULL terminated: each runs on through the neighbouring lists up to RW 0xD9E778 (kLODNameRun below), so
//     ModelLOD = VeryLow is index 4.
//   * the preset -> the nine Options.ini settings (RW 0x601BBD; decomp GameLODManagerRva00202244.cpp, tier A): 0 ModelLOD (+ 0x00), 1 AnimationLOD
//     (+ 0x34), 2 EffectsLOD (+ 0x04), 3 ShadowLOD (+ 0x10), 4 TerrainLOD (2 when UseTerrainNormalMap, else UseDistanceDependantTerrainTextures),
//     5 WaterLOD (+ 0x18), 6 TextureQualityLOD (TextureReductionFactor 2 -> 0, 1 -> 1, else 2), 7 ShaderLOD (+ 0x38), 8 DecalLOD (+ 0x40).
//   * the nine advanced options (RW 0xDA2228, 12-byte rows: key, choice names, count): ModelLOD 4, AnimationLOD 5, EffectsLOD 5, ShadowLOD 5,
//     TerrainLOD 3, WaterLOD 4, TextureQualityLOD 3, ShaderLOD 4, DecalLOD 3; the choice names are slices of the same run.
//   * OptionPreferences: setSetting (RW 0x6E69D9) writes key = choice name for a value in [0, count); GetEnumValue (vslot 0x1C, RW 0x7B2847) answers the
//     index of the stored text among the choices (RW 0x4372C0: case-insensitive), the default when the key is missing, empty or unknown.
//
// WHAT IS NOT PORTED (stop S-2481): applying a preset or the custom settings to the renderer (RW 0x6019D1 / RW 0x6020B4), the ideal level (benchmark,
// GameLODPresets.ini), DynamicGameLOD / AudioLOD (the blocks are read past).

#pragma once

#include <array>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;
class OptionPreferences;

// ZH StaticGameLODInfo, RotWK's 0x4C-byte record (field table RW 0xBF9468)
struct StaticGameLODInfo
{
	int modelLOD = 0;                                  // + 0x00
	int effectsLOD = 0;                                // + 0x04
	int maxParticleCount = 0;                          // + 0x08
	bool useShadowVolumes = false;                     // + 0x0C
	bool useShadowDecals = false;                      // + 0x0D
	bool useShadowMapping = false;                     // + 0x0E
	int shadowLOD = 0;                                 // + 0x10
	bool useTerrainNormalMap = false;                  // + 0x14
	bool useDistanceDependantTerrainTextures = false;  // + 0x15
	int waterLOD = 0;                                  // + 0x18
	bool showSoftWaterEdge = false;                    // + 0x1C
	int maxTankTrackEdges = 0;                         // + 0x20
	int maxTankTrackOpaqueEdges = 0;                   // + 0x24
	int maxTankTrackFadeDelay = 0;                     // + 0x28
	int textureReductionFactor = 0;                    // + 0x2C
	bool showProps = false;                            // + 0x30
	int animationDetail = 0;                           // + 0x34
	int shaderLOD = 0;                                 // + 0x38
	bool shaderMaterialReplacement = false;            // + 0x3C
	bool useHeatEffects = false;                       // + 0x3D
	int decalLOD = 0;                                  // + 0x40
	int minParticlePriority = 0;                       // + 0x44
	int minParticleSkipPriority = 0;                   // + 0x48
};

// RW 0xDA2228
struct AdvancedOptionInfo
{
	const char *key;
	const char *const *choices;
	int count;
};

class GameLODManager
{
public:
	static constexpr int kLevelCount = 6;   // RW 0xD9E6A4
	static constexpr int kCustomLevel = 5;
	static constexpr int kAdvancedOptionCount = 9;

	// RW 0xD9E6A4: VeryLow, Low, Medium, High, UltraHigh, Custom
	static const char *levelName(int level);
	// RW 0x6024E7: the level of a name (case-insensitive), -1 when it is not one
	static int findLevel(const std::string &name);
	static const AdvancedOptionInfo &advancedOption(int index);

	// Data\INI\GameLOD.ini (RW string 0x7F995C)
	static const std::vector<std::string> &loadOrder();
	// StaticGameLOD (RW 0x60252D) here; the file's other blocks through the recording stubs
	void registerBlocks(INIBlockRegistry &registry);
	void parseStaticGameLODDefinition(INI *ini);

	bool hasLevel(int level) const { return level >= 0 && level < kLevelCount && m_defined[(size_t)level]; }
	const StaticGameLODInfo &level(int level) const { return m_levels[(size_t)level]; }
	// RW 0x601BBD: the nine settings of a preset, in the advanced options' order
	std::array<int, kAdvancedOptionCount> presetSettings(int level) const;

private:
	std::array<StaticGameLODInfo, kLevelCount> m_levels{};
	std::array<bool, kLevelCount> m_defined{};
};

// OptionPreferences' advanced settings (lane PLAY-2)
namespace AdvancedOptionPrefs
{
// RW 0x6E69D9: key `index` = its choice `value` when 0 <= value < count
void setSetting(OptionPreferences &prefs, int index, int value);
// RW 0x6E566C -> RW 0x7B2847: the choice index of key `index`, 0 when missing / unknown
int getSetting(const OptionPreferences &prefs, int index);
// RW 0x6E6986: the nine keys are removed
void clearSettings(OptionPreferences &prefs);
// RW 0x91EE9A: the nine values as "%d" joined by ","
std::string format(const std::array<int, GameLODManager::kAdvancedOptionCount> &values);
std::string format(const OptionPreferences &prefs);
// RW 0x91EE11: the text split at "," (RW 0x4366D0), atoi of each token into setSetting(0 ..), stopping at the ninth or when the tokens run out
void parseInto(const std::string &text, OptionPreferences &prefs);
} // namespace AdvancedOptionPrefs
