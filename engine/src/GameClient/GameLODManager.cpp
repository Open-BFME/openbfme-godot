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
	{ "MinParticlePriority", INI::parseIndexList, FXParticleSystem::ParticlePriorityNames, LOD_OFF(minParticlePriority) },       // RW 0xC31AE8
	{ "MinParticleSkipPriority", INI::parseIndexList, FXParticleSystem::ParticlePriorityNames, LOD_OFF(minParticleSkipPriority) }, // RW 0xC31AE8
	{ nullptr, nullptr, nullptr, 0 },
};
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
