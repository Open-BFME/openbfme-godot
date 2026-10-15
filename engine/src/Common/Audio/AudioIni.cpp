// OpenBFME. GPL-3.0. See AudioIni.h.

#include "Common/Audio/AudioIni.h"
#include "Common/Audio/AudioAssetCache.h"
#include <functional>
#include <stdexcept>

std::map<std::string, std::string> AudioIniState::installMenuMusic(const AudioIniState &theme)
{
	std::map<std::string, AudioEventInfoStore::InfoPtr> copied;
	std::function<AudioEventInfoStore::InfoPtr(const std::string &)> copy = [&](const std::string &name) {
		auto prior = copied.find(name);
		if (prior != copied.end()) return prior->second;
		auto source = theme.infos.find(name);
		if (!source) throw std::runtime_error("Unknown theme music event: " + name);
		const std::string privateName = "OpenBFMEMenu::" + name;
		auto target = infos.findOrCreate(privateName);
		*target = *source;
		target->audioName = privateName;
		copied[name] = target;
		if (source->soundType == AT_Music && !source->filename.empty())
			target->filename = std::string(AudioAssetCache::menuPathMarker) + theme.settings.musicFolder + source->filename;
		for (auto &child : target->subsounds)
		{
			child.info = copy(child.name);
			child.name = child.info->audioName;
		}
		return target;
	};
	std::map<std::string, std::string> slots;
	for (const char *field : {"LowLODShellMusic", "HighLODShellMusic", "FullScreenSubMenuMusic", "CreditsMusic",
		"ScoreScreenMusic", "ShellMapLoadMusic", "SaveFileLoadMusic", "VolumeSampleMusic"})
	{
		const auto &name = theme.miscAudio.get(field);
		slots[field] = name.empty() ? "" : copy(name)->audioName;
	}
	return slots;
}

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <cstddef>

const std::vector<std::string> &AudioIniState::blockKeywords()
{
	static const std::vector<std::string> keywords = { "AudioEvent", "MusicTrack", "DialogEvent", "AmbientStream", "StreamedSound", "Multisound", "AudioSettings", "MiscAudio", "AudioLOD",
		"AudioLowMHz", "AnimationSoundClientBehaviorGlobalSetting", "PredefinedEvaEvent", "NewEvaEvent", "EvaEventForwardReference", "MiscEvaData", "ScoredKillEvaAnnouncer",
		"LargeGroupAudioMap", "LargeGroupAudioUnusedKnownKeys" };
	return keywords;
}

// RW 0x4538DA-0x453A34 (spec 9.5): AudioSettings; Default/Music, Default/Speech, Default/SoundEffects, Default/AmbientStream; Music, SoundEffects, Speech, Voice, AmbientStream;
// MiscAudio. The Default files come first, each type's file after its default.
const std::vector<std::string> &AudioIniState::loadOrder()
{
	static const std::vector<std::string> files = {
		"Data\\INI\\AudioSettings.ini",
		"Data\\INI\\Default\\Music.ini",
		"Data\\INI\\Default\\Speech.ini",
		"Data\\INI\\Default\\SoundEffects.ini",
		"Data\\INI\\Default\\AmbientStream.ini",
		"Data\\INI\\Music.ini",
		"Data\\INI\\SoundEffects.ini",
		"Data\\INI\\Speech.ini",
		"Data\\INI\\Voice.ini",
		"Data\\INI\\AmbientStream.ini",
		"Data\\INI\\MiscAudio.ini",
	};
	return files;
}

void AudioIniState::loadAll(INI &ini)
{
	for (const std::string &file : loadOrder())
	{
		ini.load(file, INI_LOAD_OVERWRITE);
	}
}

void AudioIniState::loadEva(INI &ini)
{
	for (const std::string &file : EvaEventStore::loadOrder())
	{
		ini.load(file, INI_LOAD_OVERWRITE);
	}
}

void AudioIniState::loadLargeGroupAudio(INI &ini)
{
	for (const std::string &file : LargeGroupAudioStore::loadOrder())
	{
		ini.load(file, INI_LOAD_OVERWRITE);
	}
}

void AudioIniState::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("PredefinedEvaEvent", [this](INI *ini) { eva.parsePredefined(ini, infos); });
	registry.registerBlock("NewEvaEvent", [this](INI *ini) { eva.parseNew(ini, infos, false); });
	registry.registerBlock("EvaEventForwardReference", [this](INI *ini) { eva.parseNew(ini, infos, true); });
	registry.registerBlock("MiscEvaData", [this](INI *ini) { eva.parseMiscEvaData(ini); });
	registry.registerBlock("ScoredKillEvaAnnouncer", [this](INI *ini) { eva.parseScoredKillAnnouncer(ini); });
	registry.registerBlock("LargeGroupAudioMap", [this](INI *ini) { largeGroupAudio.parseMap(ini, infos); });
	registry.registerBlock("LargeGroupAudioUnusedKnownKeys", [this](INI *ini) { largeGroupAudio.parseUnusedKnownKeys(ini); });
	registry.registerBlock("MusicTrack", [this](INI *ini) { ParseAudioEventInfoBlock(ini, infos, AT_Music, "MusicTrack"); });
	registry.registerBlock("DialogEvent", [this](INI *ini) { ParseAudioEventInfoBlock(ini, infos, AT_Streaming, "DialogEvent"); });
	registry.registerBlock("AudioEvent", [this](INI *ini) { ParseAudioEventInfoBlock(ini, infos, AT_SoundEffect, "AudioEvent"); });
	registry.registerBlock("AmbientStream", [this](INI *ini) { ParseAudioEventInfoBlock(ini, infos, AT_AmbientStream, "AmbientStream"); });
	registry.registerBlock("StreamedSound", [this](INI *ini) { ParseAudioEventInfoBlock(ini, infos, AT_StreamedSound, "StreamedSound"); });
	registry.registerBlock("Multisound", [this](INI *ini) { ParseMultisoundBlock(ini, infos); });
	registry.registerBlock("AudioSettings", [this](INI *ini) { AudioSettings::parse(ini, settings); });
	registry.registerBlock("MiscAudio", [this](INI *ini) { miscAudio.parse(ini, infos); });
	registry.registerBlock("AudioLowMHz", [this](INI *ini) {
		int value = 0;
		INI::parseInt(ini, nullptr, &value, nullptr);
		audioLowMHz = value; // retail stores it only when TheGameLODManager exists (RW 0x601818); the value is kept here either way
	});
	registry.registerBlock("AudioLOD", [this](INI *ini) {
		// RW 0x602905: the level name, "Low" or "High" (RW 0xD9E760); an unknown name is INIException(8, "Unknown Audio LOD level '%s'")
		const std::string name = ini->getNextToken();
		int level = -1;
		if (AsciiStringUtil::compareNoCase(name, "Low") == 0)
		{
			level = 0;
		}
		else if (AsciiStringUtil::compareNoCase(name, "High") == 0)
		{
			level = 1;
		}
		if (level < 0)
		{
			throw INIException(8, "Unknown Audio LOD level '%s'", name.c_str());
		}
		AudioLODSettings &lod = audioLOD[level];
		const FieldParse table[] = {
			{ "MaximumAmbientStreams", INI::parseInt, nullptr, (int)offsetof(AudioLODSettings, maximumAmbientStreams) },
			{ "AllowDolby", INI::parseBool, nullptr, (int)offsetof(AudioLODSettings, allowDolby) },
			{ "AllowReverb", INI::parseBool, nullptr, (int)offsetof(AudioLODSettings, allowReverb) },
			{ nullptr, nullptr, nullptr, 0 }
		};
		ini->initFromINI(&lod, table);
		lod.defined = true;
	});
	registry.registerBlock("AnimationSoundClientBehaviorGlobalSetting", [this](INI *ini) {
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
		{
			throw INIException(3, "Cannot override AnimationSoundClientBehaviorGlobalSetting in map.ini");
		}
		const FieldParse table[] = { { "MinMicrophoneDistanceToDirty", INI::parseReal, nullptr, (int)offsetof(AudioIniState, minMicrophoneDistanceToDirty) }, { nullptr, nullptr, nullptr, 0 } };
		ini->initFromINI(this, table);
	});
}
