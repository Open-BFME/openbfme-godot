// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GameLODManager (lane PLAY-2): the part the Options screen's advanced page reads: the StaticGameLOD presets of Data\INI\GameLOD.ini and the
// nine advanced options (ZH GameLODManager / StaticGameLODInfo, GameLOD.cpp, as RotWK changed them).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * TheGameLODManager (RW 0xDE3B84) holds six 0x4C-byte StaticGameLOD records at + 0, one per level of the name list RW 0xD9E6A4 (VeryLow, Low,
//     Medium, High, UltraHigh, Custom); + 0x1768 the current static level, + 0x17C4 the ideal one (the benchmark's, not ported: S-2481).
//   * the block StaticGameLOD (RW 0x60252D; BFME2 decomp GameLOD.cpp, tier A): the name is looked up in RW 0xD9E6A4 with a case-insensitive
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
// Lane OPTS-2 ports applying a level (RW 0x6020B4 / 0x601C62) and the Custom record (RW 0x6019D1): StaticLODState below is what the device draws
// with (GodotAptPlayer get_detail_state, game.gd). WHAT IS NOT PORTED (stop S-2481): the hardware detection / benchmark behind the ideal level
// (GameLODPresets.ini), DynamicGameLOD / AudioLOD (the blocks are read past).

#pragma once

#include <array>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;
class OptionPreferences;

// ZH StaticGameLODInfo, RotWK's 0x4C-byte record (field table RW 0xBF9468). The defaults are the record constructor's (RW 0x601745 .. 0x6017A3, lane
// OPTS-2; every record of the manager, Custom included, starts with them): + 0x2C and + 0x38 are not written there (INFERENCE: 0).
struct StaticGameLODInfo
{
	bool useAnisotropic = false, usePixelShaders = true, useLightMap = true, useBuildupScaffolds = true, useTreeSway = true, useHighQualityVideo = true; // BFME1
	int modelLOD = 2;                                  // + 0x00
	int effectsLOD = 3;                                // + 0x04
	int maxParticleCount = 2500;                       // + 0x08
	bool useShadowVolumes = true;                      // + 0x0C
	bool useShadowDecals = true;                       // + 0x0D
	bool useShadowMapping = false;                     // + 0x0E
	int shadowLOD = 3;                                 // + 0x10
	bool useTerrainNormalMap = true;                   // + 0x14
	bool useDistanceDependantTerrainTextures = true;   // + 0x15
	int waterLOD = 2;                                  // + 0x18
	bool showSoftWaterEdge = true;                     // + 0x1C
	int maxTankTrackEdges = 100;                       // + 0x20
	int maxTankTrackOpaqueEdges = 25;                  // + 0x24
	int maxTankTrackFadeDelay = 300000;                // + 0x28
	int textureReductionFactor = 0;                    // + 0x2C
	bool showProps = true;                             // + 0x30
	int animationDetail = 3;                           // + 0x34
	int shaderLOD = 0;                                 // + 0x38
	bool shaderMaterialReplacement = false;            // + 0x3C
	bool useHeatEffects = false;                       // + 0x3D
	int decalLOD = 2;                                  // + 0x40
	int minParticlePriority = 1;                       // + 0x44
	int minParticleSkipPriority = 1;                   // + 0x48
};

// Lane OPTS-2: what applying a static level (RW 0x601C62) leaves in the manager and in TheGlobalData (RW 0xDE4364): the detail the client draws with.
// TARGET FACTS (RotWK game.dat, caveat S-001; RotWK-only setter RW 0x6020B4, apply body RW 0x601C62 = BFME2 0x6022E9 tier B): `info` = record `level`;
// manager + 0x1774 = ModelLOD, + 0x1778 = EffectsLOD, + 0x1784 = WaterLOD, + 0x1788 = AnimationDetail, + 0x177C = ShadowLOD; GlobalData + 0x9A5 =
// DecalLOD > 0, + 0x9A7 = DecalLOD < 2, + 0x9BE (AllHealthBars) = 1 when + 0x9A5 is 0, else OptionPreferences::getAllHealthBars (RW 0x6E6179);
// + 0x25 = UseHeatEffects; + 0xAC8 = MaxParticleCount; + 0x60 / + 0x61 / + 0x62 = UseShadowVolumes / UseShadowDecals / UseShadowMapping, the first two
// cleared when + 0x62 is set; the texture reduction (RW 0x517540) = TextureReductionFactor, level 0's when the manager's memory test failed (+ 0x17AA);
// [RW 0xD9B058] = ShaderLOD (a change reloads the shaders, RW 0x516AB0), [RW 0xDD1E50] = ShaderMaterialReplacement (RW 0x516A30); + 0x49 / + 0x4A =
// UseTerrainNormalMap / UseDistanceDependantTerrainTextures and manager + 0x1780 = 2 / 1 / 0 from them; + 0x5E = WaterLOD >= 2; + 0x84 =
// ShowSoftWaterEdge; + 0x110 / + 0x114 / + 0x118 = the three tank track values; + 0x24 = ModelLOD <= 0; + 0x1C = ShowProps; manager + 0x17A0 /
// + 0x17A4 = MinParticlePriority / MinParticleSkipPriority. Then the terrain's slots 0x204 / 0x208, the display's slot 0x24, GlobalData + 0xAF0 &=
// EffectsLOD > 1 unless + 0xAF1, and the shell's refresh (RW 0x75DE01(1, 1)).
struct StaticLODState
{
	int level = -1;                     // manager + 0x1768
	int modelLOD = 0, effectsLOD = 0, shadowLOD = 0, terrainLOD = 0, waterLOD = 0, animationDetail = 0; // + 0x1774 / 1778 / 177C / 1780 / 1784 / 1788
	int minParticlePriority = 0, minParticleSkipPriority = 0;                                            // + 0x17A0 / 17A4
	bool decals = false;                // GlobalData + 0x9A5
	bool decalsReduced = false;         // + 0x9A7
	bool allHealthBars = false;         // + 0x9BE
	bool useHeatEffects = false;        // + 0x25
	int maxParticleCount = 0;           // + 0xAC8
	bool useShadowVolumes = false, useShadowDecals = false, useShadowMapping = false; // + 0x60 / 61 / 62
	int textureReduction = 0;           // RW 0x517540's argument
	int shaderLOD = 0;                  // [RW 0xD9B058]
	bool shaderMaterialReplacement = false; // [RW 0xDD1E50]
	bool useTerrainNormalMap = false, useDistanceDependantTerrainTextures = false; // + 0x49 / 4A
	bool highWater = false;             // + 0x5E
	bool showSoftWaterEdge = false;     // + 0x84
	int maxTankTrackEdges = 0, maxTankTrackOpaqueEdges = 0, maxTankTrackFadeDelay = 0; // + 0x110 / 114 / 118
	bool lowestModelLOD = false;        // + 0x24
	bool showProps = false;             // + 0x1C
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

	// ---- lane OPTS-2: the static level in effect ----
	// RW 0x6019D1 (BFME2 decomp Rva00202058.cpp, tier A): the Custom record from Options.ini's nine advanced settings (GetEnumValue, default 0):
	// ModelLOD -> + 0x00; AnimationLOD -> + 0x34; EffectsLOD -> + 0x04, UseHeatEffects = EffectsLOD >= 2, and MaxParticleCount / the two particle
	// priorities of the preset EffectsLOD names (0 .. 4, above 4 the UltraHigh one); ShadowLOD s -> + 0x10, UseShadowDecals = s in 1 .. 2,
	// UseShadowVolumes = s == 2, UseShadowMapping = s >= 3; TerrainLOD t -> UseTerrainNormalMap = t >= 2, UseDistanceDependantTerrainTextures =
	// ShowProps = t >= 1, the tank track values of preset Low (t 0), Medium (t 1) or High; WaterLOD w -> + 0x18, ShowSoftWaterEdge = w >= 1;
	// TextureQualityLOD q -> TextureReductionFactor 2 (q 0), 1 (q 1), 0; ShaderLOD -> + 0x38, ShaderMaterialReplacement = ShaderLOD >= 2; DecalLOD
	// -> + 0x40.
	void setCustomFromPreferences(const OptionPreferences &prefs);
	// RW 0x6020B4: -1 is refused; Custom always applies, another level only when it is not the current one; true when applied. `prefs` is what RW
	// 0x601C62 reads AllHealthBars from.
	bool setStaticLODLevel(int level, const OptionPreferences &prefs);
	const StaticLODState &state() const { return m_state; }
	int staticLevel() const { return m_state.level; }
	// RW 0x602165 (GameLODManager::init, BFME2 decomp GameLODManagerInit.cpp, tier A) and RW 0x4465C3 .. 0x4465D8, the parts the port runs: the level of
	// Options.ini's StaticGameLOD (getStaticGameDetail, default the current level -1); Custom fills the Custom record first (RW 0x6021F7); the level
	// is applied (RW 0x60245F); still -1, the game engine applies findStaticLODLevel (RW 0x602C03): IdealStaticGameLOD when Options.ini has it, else
	// the hardware detection and benchmark, NOT PORTED (stop S-2481): INFERENCE kDetectedLevel, and `stop` says so. Returns the level applied.
	int init(const OptionPreferences &prefs, std::string *stop = nullptr);
	// lane OPTS-2 (stop S-3402): what the device does not draw of the level yet, one "[S-3402] ..." line
	static std::string unappliedEffects();
	// lane OPTS-2 (S-3402): whether the device draws anything of advanced option `index` (RW 0xDA2228's order): EffectsLOD (the particle cap and
	// priorities), TextureQualityLOD (the texture reduction, from the next load) and DecalLOD (the health bars Off forces) do; the other six do not yet,
	// and the Options screen greys their rows
	static bool advancedOptionDrawn(int index);
	// the level the port takes for the hardware detection it does not run (S-2481): the top preset (the picture the renderer drew before OPTS-2)
	static constexpr int kDetectedLevel = 4;

private:
	void applyStaticLODLevel(int level, const OptionPreferences &prefs); // RW 0x601C62
	std::array<StaticGameLODInfo, kLevelCount> m_levels{};
	std::array<bool, kLevelCount> m_defined{};
	StaticLODState m_state;
	// + 0x17AA: the memory test of init (RAM / 256 MB >= PROFILE_ERROR_LIMIT); INFERENCE: passed (any machine that runs the port has the memory)
	bool m_memPassed = true;
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
