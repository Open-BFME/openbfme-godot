// OpenBFME. GPL-3.0.
// See GameClient/GameLODManager.h for the target facts.

#include "GameClient/GameLODManager.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "GameClient/FXParticleSystem.h"
#include "GameClient/OptionPreferences.h"

#include <cstddef>
#include <cstdlib>

namespace
{
// RW 0xD9E6A4 .. 0xD9E774: the level names and every LOD index list after them, as one run that ends at the NULL of RW 0xD9E778. Each list below
// starts inside it and parseIndexList reads on to that NULL, as retail's does.
const char *const kLODNameRun[] = {
	"VeryLow", "Low", "Medium", "High", "UltraHigh", "Custom", // RW 0xD9E6A4 the levels
	"Low", "Medium", "High", "UltraHigh",                      // RW 0xD9E6BC ModelLOD
	"VeryLow", "Low", "Medium", "High", "UltraHigh",           // RW 0xD9E6CC AnimationDetail
	"VeryLow", "Low", "Medium", "High", "UltraHigh",           // RW 0xD9E6E0 EffectsLOD
	"Off", "Low", "High",                                      // RW 0xD9E6F4 DecalLOD
	"Low", "Medium", "High", "UltraHigh",                      // RW 0xD9E700 WaterLOD
	"Off", "Low", "Medium", "High", "UltraHigh",               // RW 0xD9E710 ShadowLOD
	"Low", "Medium", "High",                                   // RW 0xD9E724 TerrainLOD (the advanced option's choices)
	"Low", "Medium", "High",                                   // RW 0xD9E730 TextureQualityLOD
	"Low", "Medium", "High", "UltraHigh",                      // RW 0xD9E73C ShaderLOD
	"VeryLow", "Low", "Medium", "High", "VeryHigh",            // RW 0xD9E74C the dynamic levels
	"Low", "High",                                             // RW 0xD9E760 the audio levels
	"XX", "P3", "P4", "K7",                                    // RW 0xD9E768 the CPU types
	nullptr };

constexpr size_t kLevels = 0, kModel = 6, kAnimation = 10, kEffects = 15, kDecal = 20, kWater = 23, kShadow = 27, kTerrain = 32, kTexture = 35,
				 kShader = 38;

const char *const *at(size_t index) { return kLODNameRun + index; }

#define LOD_OFF(f) (int)offsetof(StaticGameLODInfo, f)
// RW 0xBF9468, in its order
const FieldParse kStaticGameLODFields[] = {
	{ "ModelLOD", INI::parseIndexList, at(kModel), LOD_OFF(modelLOD) },
	{ "EffectsLOD", INI::parseIndexList, at(kEffects), LOD_OFF(effectsLOD) },
	{ "MaxParticleCount", INI::parseInt, nullptr, LOD_OFF(maxParticleCount) },
	{ "UseShadowVolumes", INI::parseBool, nullptr, LOD_OFF(useShadowVolumes) },
	{ "UseShadowDecals", INI::parseBool, nullptr, LOD_OFF(useShadowDecals) },
	{ "UseShadowMapping", INI::parseBool, nullptr, LOD_OFF(useShadowMapping) },
	{ "ShadowLOD", INI::parseIndexList, at(kShadow), LOD_OFF(shadowLOD) },
	{ "UseTerrainNormalMap", INI::parseBool, nullptr, LOD_OFF(useTerrainNormalMap) },
	{ "UseDistanceDependantTerrainTextures", INI::parseBool, nullptr, LOD_OFF(useDistanceDependantTerrainTextures) },
	{ "WaterLOD", INI::parseIndexList, at(kWater), LOD_OFF(waterLOD) },
	{ "ShowSoftWaterEdge", INI::parseBool, nullptr, LOD_OFF(showSoftWaterEdge) },
	{ "MaxTankTrackEdges", INI::parseInt, nullptr, LOD_OFF(maxTankTrackEdges) },
	{ "MaxTankTrackOpaqueEdges", INI::parseInt, nullptr, LOD_OFF(maxTankTrackOpaqueEdges) },
	{ "MaxTankTrackFadeDelay", INI::parseInt, nullptr, LOD_OFF(maxTankTrackFadeDelay) },
	{ "ShowProps", INI::parseBool, nullptr, LOD_OFF(showProps) },
	{ "TextureReductionFactor", INI::parseInt, nullptr, LOD_OFF(textureReductionFactor) },
	{ "AnimationDetail", INI::parseIndexList, at(kAnimation), LOD_OFF(animationDetail) },
	{ "ShaderLOD", INI::parseIndexList, at(kShader), LOD_OFF(shaderLOD) },
	{ "ShaderMaterialReplacement", INI::parseBool, nullptr, LOD_OFF(shaderMaterialReplacement) },
	{ "UseHeatEffects", INI::parseBool, nullptr, LOD_OFF(useHeatEffects) },
	{ "DecalLOD", INI::parseIndexList, at(kDecal), LOD_OFF(decalLOD) },
	{ "MinParticlePriority", FXParticleSystem::ParsePriority, FXParticleSystem::ParticlePriorityNames, LOD_OFF(minParticlePriority) },       // RW 0xC31AE8
	{ "MinParticleSkipPriority", FXParticleSystem::ParsePriority, FXParticleSystem::ParticlePriorityNames, LOD_OFF(minParticleSkipPriority) }, // RW 0xC31AE8
	{ nullptr, nullptr, nullptr, 0 },
};
// community PR #36: BFME1's StaticGameLOD rows (stored, not used) (BFME1 data only, Common/BfmeGame.h); RotWK's table stays the binary's
const FieldParse kBfme1StaticGameLODFields[] = {
	{ "UseAnisotropic", INI::parseBool, nullptr, LOD_OFF(useAnisotropic) },
	{ "UsePixelShaders", INI::parseBool, nullptr, LOD_OFF(usePixelShaders) },
	{ "UseLightMap", INI::parseBool, nullptr, LOD_OFF(useLightMap) },
	{ "UseBuildupScaffolds", INI::parseBool, nullptr, LOD_OFF(useBuildupScaffolds) },
	{ "UseTreeSway", INI::parseBool, nullptr, LOD_OFF(useTreeSway) },
	{ "UseHighQualityVideo", INI::parseBool, nullptr, LOD_OFF(useHighQualityVideo) },
	{ nullptr, nullptr, nullptr, 0 },
};
const Bfme1FieldRowsRegistration kBfme1StaticGameLODFieldsRegistration(kStaticGameLODFields, kBfme1StaticGameLODFields);

#undef LOD_OFF

// RW 0xDA2228
const AdvancedOptionInfo kAdvancedOptions[GameLODManager::kAdvancedOptionCount] = {
	{ "ModelLOD", at(kModel), 4 },          { "AnimationLOD", at(kAnimation), 5 }, { "EffectsLOD", at(kEffects), 5 },
	{ "ShadowLOD", at(kShadow), 5 },        { "TerrainLOD", at(kTerrain), 3 },     { "WaterLOD", at(kWater), 4 },
	{ "TextureQualityLOD", at(kTexture), 3 }, { "ShaderLOD", at(kShader), 4 },     { "DecalLOD", at(kDecal), 3 },
};
} // namespace

const char *GameLODManager::levelName(int level)
{
	return level >= 0 && level < kLevelCount ? kLODNameRun[kLevels + (size_t)level] : nullptr;
}

int GameLODManager::findLevel(const std::string &name)
{
	for (int i = 0; i < kLevelCount; ++i)
	{
		if (AsciiStringUtil::compareNoCase(name, levelName(i)) == 0)
		{
			return i;
		}
	}
	return -1;
}

const AdvancedOptionInfo &GameLODManager::advancedOption(int index)
{
	return kAdvancedOptions[index];
}

const std::vector<std::string> &GameLODManager::loadOrder()
{
	static const std::vector<std::string> files = { "Data\\INI\\GameLOD.ini" };
	return files;
}

void GameLODManager::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("StaticGameLOD", [this](INI *ini) { parseStaticGameLODDefinition(ini); });
}

void GameLODManager::parseStaticGameLODDefinition(INI *ini)
{
	// RW 0x60252D: the name (getNextToken(0)), its level; an unknown name leaves the fields unread
	const int level = findLevel(ini->getNextToken());
	if (level == -1)
	{
		return;
	}
	ini->initFromINI(&m_levels[(size_t)level], kStaticGameLODFields);
	m_defined[(size_t)level] = true;
}

std::array<int, GameLODManager::kAdvancedOptionCount> GameLODManager::presetSettings(int level) const
{
	const StaticGameLODInfo &p = m_levels[(size_t)level];
	const int terrain = p.useTerrainNormalMap ? 2 : (p.useDistanceDependantTerrainTextures ? 1 : 0);
	const int texture = p.textureReductionFactor == 2 ? 0 : (p.textureReductionFactor == 1 ? 1 : 2);
	return { p.modelLOD, p.animationDetail, p.effectsLOD, p.shadowLOD, terrain, p.waterLOD, texture, p.shaderLOD, p.decalLOD };
}

void GameLODManager::setCustomFromPreferences(const OptionPreferences &prefs)
{
	// RW 0x6019D1 (BFME2 0x602058, tier A): the nine forwarders RW 0x6E56CB .. (GetEnumValue of the RW 0xDA2228 rows, default 0)
	auto setting = [&prefs](int index) { return AdvancedOptionPrefs::getSetting(prefs, index); };
	StaticGameLODInfo &c = m_levels[(size_t)kCustomLevel];
	c.modelLOD = setting(0);
	c.animationDetail = setting(1);
	const int effects = setting(2);
	c.effectsLOD = effects;
	c.useHeatEffects = effects >= 2;
	const int particles = effects <= 0 ? 0 : (effects == 1 ? 1 : (effects == 2 ? 2 : (effects == 3 ? 3 : 4)));
	c.maxParticleCount = m_levels[(size_t)particles].maxParticleCount;
	c.minParticlePriority = m_levels[(size_t)particles].minParticlePriority;
	c.minParticleSkipPriority = m_levels[(size_t)particles].minParticleSkipPriority;
	const int shadow = setting(3);
	c.useShadowDecals = shadow >= 1 && shadow <= 2;
	c.useShadowVolumes = shadow == 2;
	c.useShadowMapping = shadow >= 3;
	c.shadowLOD = shadow;
	const int terrain = setting(4);
	c.useTerrainNormalMap = terrain >= 2;
	c.useDistanceDependantTerrainTextures = terrain >= 1;
	const int water = setting(5);
	c.waterLOD = water;
	c.showSoftWaterEdge = water >= 1;
	const StaticGameLODInfo &tracks = m_levels[(size_t)(terrain <= 0 ? 1 : (terrain > 1 ? 3 : 2))];
	c.maxTankTrackEdges = tracks.maxTankTrackEdges;
	c.maxTankTrackOpaqueEdges = tracks.maxTankTrackOpaqueEdges;
	c.maxTankTrackFadeDelay = tracks.maxTankTrackFadeDelay;
	const int texture = setting(6);
	c.textureReductionFactor = texture <= 0 ? 2 : (texture <= 1 ? 1 : 0);
	c.showProps = terrain >= 1;
	const int shader = setting(7);
	c.shaderLOD = shader;
	c.shaderMaterialReplacement = shader >= 2;
	c.decalLOD = setting(8);
}

bool GameLODManager::setStaticLODLevel(int level, const OptionPreferences &prefs)
{
	// RW 0x6020B4: the apply (RW 0x601C62) runs before the level is stored at + 0x1768
	if (level < 0 || level >= kLevelCount) // -1 (RW 0x6020B9); the port also refuses what no record has
	{
		return false;
	}
	if (level != kCustomLevel && m_state.level == level)
	{
		return false;
	}
	applyStaticLODLevel(level, prefs);
	m_state.level = level;
	return true;
}

void GameLODManager::applyStaticLODLevel(int level, const OptionPreferences &prefs)
{
	// RW 0x601C62 (see StaticLODState for the targets)
	const StaticGameLODInfo &info = m_levels[(size_t)level];
	StaticLODState &s = m_state;
	s.modelLOD = info.modelLOD;
	s.effectsLOD = info.effectsLOD;
	s.waterLOD = info.waterLOD;
	s.animationDetail = info.animationDetail;
	s.decals = info.decalLOD > 0;
	s.decalsReduced = info.decalLOD < 2;
	s.allHealthBars = !s.decals || prefs.get("AllHealthBars") == "yes"; // RW 0x6E6179: strcmp with "yes", missing false
	s.useHeatEffects = info.useHeatEffects;
	s.maxParticleCount = info.maxParticleCount;
	s.shadowLOD = info.shadowLOD;
	s.useShadowVolumes = info.useShadowVolumes;
	s.useShadowDecals = info.useShadowDecals;
	s.useShadowMapping = info.useShadowMapping;
	if (s.useShadowMapping)
	{
		s.useShadowVolumes = false;
		s.useShadowDecals = false;
	}
	s.textureReduction = m_memPassed ? info.textureReductionFactor : m_levels[0].textureReductionFactor; // RW 0x601D64 .. 0x601D73
	s.shaderLOD = info.shaderLOD;
	s.shaderMaterialReplacement = info.shaderMaterialReplacement;
	s.useTerrainNormalMap = info.useTerrainNormalMap;
	s.useDistanceDependantTerrainTextures = info.useDistanceDependantTerrainTextures;
	s.terrainLOD = s.useTerrainNormalMap ? 2 : (s.useDistanceDependantTerrainTextures ? 1 : 0);
	s.highWater = info.waterLOD >= 2;
	s.showSoftWaterEdge = info.showSoftWaterEdge;
	s.maxTankTrackEdges = info.maxTankTrackEdges;
	s.maxTankTrackOpaqueEdges = info.maxTankTrackOpaqueEdges;
	s.maxTankTrackFadeDelay = info.maxTankTrackFadeDelay;
	s.lowestModelLOD = info.modelLOD <= 0;
	s.showProps = info.showProps;
	s.minParticlePriority = info.minParticlePriority;
	s.minParticleSkipPriority = info.minParticleSkipPriority;
}

std::string GameLODManager::unappliedEffects()
{
	return "[S-3402] the detail level drives the particle cap and priorities, texture reduction (models loaded afterwards), health bars and Godot object shadow maps; not "
		   "drawn: retail stencil and decal shadows, heat effects, model / animation / terrain / water / shader detail, props, tank tracks, the decal style";
}

bool GameLODManager::advancedOptionDrawn(int index)
{
	return index == 2 || index == 3 || index == 6 || index == 8;
}

int GameLODManager::init(const OptionPreferences &prefs, std::string *stop)
{
	// RW 0x602165: getStaticGameDetail (GetEnumValue over the six level names, default the current level)
	auto enumValue = [&prefs](const char *key, int defaultValue) {
		const std::string v = prefs.get(key);
		const int level = v.empty() ? -1 : findLevel(v);
		return level == -1 ? defaultValue : level;
	};
	const int userSet = enumValue("StaticGameLOD", m_state.level);
	if (userSet == kCustomLevel)
	{
		setCustomFromPreferences(prefs); // RW 0x6021F7
	}
	setStaticLODLevel(userSet, prefs); // RW 0x60245F (init's staticLevel argument is -1)
	if (m_state.level == -1)
	{
		// RW 0x4465C3 .. 0x4465D8: findStaticLODLevel (RW 0x602C03): the ideal level of IdealStaticGameLOD (RW 0x6E5663, default -1)
		int ideal = enumValue("IdealStaticGameLOD", -1);
		if (ideal == -1)
		{
			ideal = kDetectedLevel;
			if (stop)
			{
				*stop = "[S-2481] Options.ini has neither StaticGameLOD nor IdealStaticGameLOD: retail detects the hardware and benchmarks it (RW 0x602C03, "
						"GameLODPresets.ini), which is not ported; the port takes " + std::string(levelName(ideal));
			}
		}
		setStaticLODLevel(ideal, prefs);
	}
	return m_state.level;
}

namespace AdvancedOptionPrefs
{
void setSetting(OptionPreferences &prefs, int index, int value)
{
	const AdvancedOptionInfo &o = GameLODManager::advancedOption(index);
	if (value >= 0 && value < o.count)
	{
		prefs.set(o.key, o.choices[value]);
	}
}

int getSetting(const OptionPreferences &prefs, int index)
{
	const AdvancedOptionInfo &o = GameLODManager::advancedOption(index);
	const std::string value = prefs.get(o.key);
	if (!value.empty())
	{
		for (int i = 0; i < o.count; ++i)
		{
			if (AsciiStringUtil::compareNoCase(value, o.choices[i]) == 0)
			{
				return i;
			}
		}
	}
	return 0;
}

void clearSettings(OptionPreferences &prefs)
{
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		prefs.erase(GameLODManager::advancedOption(i).key);
	}
}

std::string format(const std::array<int, GameLODManager::kAdvancedOptionCount> &values)
{
	std::string out;
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		if (i != 0)
		{
			out += ",";
		}
		out += std::to_string(values[(size_t)i]);
	}
	return out;
}

std::string format(const OptionPreferences &prefs)
{
	std::array<int, GameLODManager::kAdvancedOptionCount> values{};
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		values[(size_t)i] = getSetting(prefs, i);
	}
	return format(values);
}

void parseInto(const std::string &text, OptionPreferences &prefs)
{
	std::string rest = text;
	std::string token;
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount && AsciiStringUtil::nextToken(rest, &token, ","); ++i)
	{
		setSetting(prefs, i, std::atoi(token.c_str())); // RW 0xBD0628 atoi
	}
}
} // namespace AdvancedOptionPrefs
