// OpenBFME. GPL-3.0.
//
// RotWK block keyword table (spec 3.2) and the recording stub handlers. See INIBlockStubs.h.

#include "Common/INI/INIBlockStubs.h"

#include "Common/AsciiString.h"
#include "GameClient/FXList.h"           // FX-1: the FXList block is parsed for real
#include "GameClient/FXParticleSystem.h" // FX-1: the FXParticleSystem block is parsed for real
#include "GameLogic/Locomotor.h" // HORDE-1: the Locomotor block is parsed for real
#include "GameLogic/Armor.h"     // WEAPON-1: the Weapon, Armor and DamageFX blocks are parsed for real
#include "GameLogic/DamageFX.h"
#include "GameLogic/Weapon.h"

#include <algorithm>

// spec ini-and-object-model.md 3.2, verbatim, in the spec's order. 131 entries.
const std::vector<std::string> &RotwkBlockKeywords()
{
	static const std::vector<std::string> keywords = {
		"AIBase", "AIData", "AIDozerAssignment", "AerialPathfindNoFlyZone", "AmbientStream",
		"AnimationSoundClientBehaviorGlobalSetting", "AptButtonTooltipMap", "Armor", "ArmyDefinition",
		"ArmySummaryDescription", "AudioEvent", "AudioLOD", "AudioLowMHz", "AudioSettings", "AutoResolveArmor",
		"AutoResolveBody", "AutoResolveCombatChain", "AutoResolveHandicapLevel", "AutoResolveLeadership",
		"AutoResolveReinforcementSchedule", "AutoResolveWeapon", "AwardSystem", "BannerType", "BenchProfile",
		"Bridge", "ChildObject", "CloudBreakEffect", "CloudEffect", "CommandButton", "CommandMap", "CommandSet",
		"ControlBarResizer", "ControlBarScheme", "CrateData", "CreateAHeroSystem", "CrowdResponse", "DamageFX",
		"DebugCommandMap", "DialogEvent", "DrawGroupInfo", "DynamicGameLOD", "EmotionNugget",
		"EvaEventForwardReference", "ExperienceLevel", "ExperienceScalarTable", "FXList", "FXParticleSystem",
		"FactionVictoryData", "Fire", "FireEffect", "FireLogicSystem", "FontDefaultSettings", "FontSubstitution",
		"FormationAssistant", "GameData", "GlowEffect", "HeaderTemplate", "HouseColor", "InGameNotificationBox",
		"LODPreset", "Language", "LargeGroupAudioMap", "LargeGroupAudioUnusedKnownKeys", "LightPointLevel",
		"LinearCampaign", "LivingWorldAITemplate", "LivingWorldAnimObject", "LivingWorldArmyIcon",
		"LivingWorldAutoResolveResourceBonus", "LivingWorldAutoResolveSciencePurchasePointBonus",
		"LivingWorldBuildPlotIcon", "LivingWorldBuilding", "LivingWorldBuildingIcon", "LivingWorldCampaign",
		"LivingWorldMapInfo", "LivingWorldObject", "LivingWorldPlayerArmy", "LivingWorldPlayerTemplate",
		"LivingWorldRegionCampaign", "LivingWorldRegionEffects", "LivingWorldSound", "LoadSubsystem", "Locomotor",
		"MappedImage", "MeshNameMatches", "MiscAudio", "MiscEvaData", "MissionObjectiveList", "ModifierList", "Mouse",
		"MouseCursor", "MultiplayerColor", "MultiplayerSettings", "Multisound", "MusicTrack", "NewEvaEvent", "Object",
		"ObjectCreationList", "ObjectReskin", "OnlineChatColors", "Pathfinder", "PlayerAIType", "PlayerTemplate",
		"PredefinedEvaEvent", "Rank", "ReallyLowMHz", "RingEffect", "Road", "Science", "ScoredKillEvaAnnouncer",
		"ScriptAction", "ScriptCondition", "ShadowMap", "ShellMenuScheme", "SkirmishAIData", "SkyboxTextureSet",
		"SpecialPower", "StanceTemplate", "StaticGameLOD", "StrategicHUD", "StreamedSound", "Terrain", "Upgrade",
		"VictorySystemData", "WaterSet", "WaterTextureList", "WaterTransparency", "Weapon", "Weather", "WeatherData",
		"WindowTransition",
	};
	return keywords;
}

size_t INIBlockRecorder::endedCount() const
{
	return (size_t)std::count_if(records.begin(), records.end(), [](const INIBlockRecord &r) { return r.endsWithEnd; });
}

namespace
{
bool isColumnZero(const std::string &line)
{
	return !line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '\v' && line[0] != '\f';
}

std::string firstTokenOf(const std::string &line)
{
	const size_t b = line.find_first_not_of(" \n\r\t=");
	if (b == std::string::npos)
	{
		return std::string();
	}
	size_t e = line.find_first_of(" \n\r\t=", b);
	if (e == std::string::npos)
	{
		e = line.size();
	}
	return line.substr(b, e - b);
}
}

namespace
{
size_t indentOf(const std::string &line)
{
	size_t n = 0;
	while (n < line.size() && (line[n] == ' ' || line[n] == '\t' || line[n] == '\v' || line[n] == '\f'))
	{
		++n;
	}
	return n;
}

// Blocks that are one line with no End (corpus census: the keyword's first line is followed by
// another top-level line, never by an End). BenchProfile/ReallyLowMHz/AudioLowMHz/LODPreset are
// one-line directives; EvaEventForwardReference is a header-only forward declaration.
bool isSingleLineKeyword(const std::string &keyword)
{
	return keyword == "BenchProfile" || keyword == "ReallyLowMHz" || keyword == "AudioLowMHz" || keyword == "LODPreset" || keyword == "EvaEventForwardReference";
}
}

void RegisterRecordingBlockStubs(INIBlockRegistry &registry, INIBlockRecorder &recorder, const std::vector<std::string> &skip, StubExtent mode)
{
	for (const std::string &keyword : RotwkBlockKeywords())
	{
		if (std::find(skip.begin(), skip.end(), keyword) != skip.end())
		{
			continue;
		}
		if (keyword == "Locomotor")
		{
			// HORDE-1: a real parser (RW 0x5E8276) into TheLocomotorStore, not a recording stub.
			registry.registerBlock(keyword, [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
			continue;
		}
		if (keyword == "FXList")
		{
			// FX-1: a real parser (RW 0x5e2518) into TheFXListStore; the recorder's store stands in when none is installed.
			registry.registerBlock(keyword, [&recorder](INI *ini) {
				FXListStore *saved = TheFXListStore;
				if (!TheFXListStore)
				{
					if (!recorder.fxLists)
					{
						recorder.fxLists = std::make_shared<FXListStore>();
					}
					TheFXListStore = recorder.fxLists.get();
				}
				try
				{
					ParseFXListDefinitionGlobal(ini);
				}
				catch (...)
				{
					TheFXListStore = saved;
					throw;
				}
				TheFXListStore = saved;
			});
			continue;
		}
		if (keyword == "FXParticleSystem")
		{
			// FX-1: a real parser (RW 0x5fc7db) into the template store of TheFXParticleSystemManager.
			registry.registerBlock(keyword, [&recorder](INI *ini) {
				FXParticleSystem::FXParticleSystemTemplateStore *saved = FXParticleSystem::TheFXParticleSystemTemplateStore;
				if (!FXParticleSystem::TheFXParticleSystemTemplateStore)
				{
					if (!recorder.fxParticleSystems)
					{
						recorder.fxParticleSystems = std::make_shared<FXParticleSystem::FXParticleSystemTemplateStore>();
					}
					FXParticleSystem::TheFXParticleSystemTemplateStore = recorder.fxParticleSystems.get();
				}
				try
				{
					FXParticleSystem::ParseFXParticleSystemDefinitionGlobal(ini);
				}
				catch (...)
				{
					FXParticleSystem::TheFXParticleSystemTemplateStore = saved;
					throw;
				}
				FXParticleSystem::TheFXParticleSystemTemplateStore = saved;
			});
			continue;
		}
		if (keyword == "Weapon")
		{
			// WEAPON-1: a real parser (RW 0x6CED65) into TheWeaponStore, not a recording stub.
			registry.registerBlock(keyword, [](INI *ini) { WeaponStore::parseWeaponTemplateDefinitionGlobal(ini); });
			continue;
		}
		if (keyword == "Armor")
		{
			registry.registerBlock(keyword, [](INI *ini) { ArmorStore::parseArmorDefinitionGlobal(ini); }); // RW 0x5D8D1F
			continue;
		}
		if (keyword == "DamageFX")
		{
			registry.registerBlock(keyword, [](INI *ini) { DamageFXStore::parseDamageFXDefinitionGlobal(ini); }); // RW 0x762799
			continue;
		}
		registry.registerBlock(keyword, [&recorder, keyword, mode](INI *ini) {
			INIBlockRecord record;
			record.keyword = keyword;
			record.file = ini->getFilename();
			record.line = ini->currentSourceLine();
			const size_t headerIndent = indentOf(ini->currentLineText());
			const char *name = ini->getNextTokenOrNull();
			record.name = name ? name : "";

			std::string lastLine;
			if (mode == StubExtent::Strict)
			{
				// the block ends at the first End whose indentation is not deeper than the header's
				if (!isSingleLineKeyword(keyword))
				{
					bool closed = false;
					while (!closed)
					{
						const std::string *next = ini->peekNextLine();
						if (!next)
						{
							throw INIException(4, "Missing 'END' token.\n\nError parsing block '%s' in file '%s', line %i.\n", keyword.c_str(), ini->getFilename().c_str(),
								ini->currentSourceLine());
						}
						ini->readLine();
						lastLine = *next;
						++record.extentLines;
						closed = AsciiStringUtil::compareNoCase(firstTokenOf(lastLine), "End") == 0 && indentOf(lastLine) <= headerIndent;
					}
				}
			}
			else
			{
				for (;;)
				{
					const std::string *next = ini->peekNextLine();
					if (!next)
					{
						break;
					}
					if (isColumnZero(*next) && ini->environment().blocks.contains(firstTokenOf(*next)))
					{
						break;
					}
					ini->readLine();
					lastLine = *next;
					++record.extentLines;
				}
			}
			record.endsWithEnd = !lastLine.empty() && AsciiStringUtil::compareNoCase(firstTokenOf(lastLine), "End") == 0;
			recorder.countByKeyword[keyword]++;
			recorder.records.push_back(std::move(record));
		});
	}
}
