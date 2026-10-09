// OpenBFME. GPL-3.0. See AudioEventInfo.h for the sources.

#include "Common/Audio/AudioEventInfo.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>

const char *const theAudioPriorityNames[] = { "LOWEST", "LOW", "NORMAL", "HIGH", "CRITICAL", nullptr };
const char *const theSoundTypeNames[] = { "UI", "WORLD", "SHROUDED", "GLOBAL", "VOICE", "PLAYER", "ALLIES", "ENEMIES", "EVERYONE", "FAKE", "DEFAULT", nullptr };
// RW 0xD9DB70: the seventh name is the C string "\not used by INI files": a newline character followed by "ot used by INI files" (the author meant
// "not used"; the escape ate the n). No INI line can name it.
const char *const theAudioControlNames[] = { "LOOP", "SEQUENTIAL", "RANDOMSTART", "INTERRUPT", "FADE_ON_KILL", "FADE_ON_START", "\not used by INI files", "PLAY_ONE", nullptr };
const char *const theSubmixSliderNames[] = { "SOUNDFX", "VOICE", "MUSIC", "AMBIENT", "MOVIE", "NONE", nullptr };

namespace
{
// RW 0x5DA345 (the Sounds / Attack / Decay / Subsounds token grammar). `eventName` is the event being parsed (error text only).
void parseWeightedTokens(INI *ini, const std::string &eventName, WeightedSoundList &list)
{
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const size_t length = std::strlen(token);
		if (length == 0)
		{
			continue;
		}
		// the weight is the run of digits after the LAST ':' (RW 0x5DA37B scans back over digits only)
		const char *end = token + length - 1;
		while (end > token && std::isdigit((unsigned char)*end))
		{
			--end;
		}
		std::string name;
		int weight;
		if (*end == ':')
		{
			weight = std::atoi(end + 1);
			if (weight < 1)
			{
				throw INIException(3, "Weight of sound files must be >= 1. Sound '%s' for audio event '%s'", token, eventName.c_str());
			}
			name.assign(token, (size_t)(end - token));
		}
		else
		{
			weight = 1000;
			name.assign(token, length);
		}
		if (name.empty())
		{
			throw INIException(3, "Sound file has no file name. Sound '%s' for audio event '%s'", token, eventName.c_str());
		}
		WeightedSound ws;
		ws.name = name;
		ws.weight = weight;
		list.sounds.push_back(ws);
		list.totalWeight += weight;
	}
}

void parseSoundsList(INI *ini, void *instance, void *store, const void *)
{
	parseWeightedTokens(ini, static_cast<AudioEventInfo *>(instance)->audioName, *static_cast<WeightedSoundList *>(store));
}

// RW 0x42F334: two reals, swapped when min > max.
void parsePitchShiftPair(INI *ini, void *, void *store, const void *)
{
	float *v = static_cast<float *>(store);
	v[0] = ini->scanReal(ini->getNextToken());
	v[1] = ini->scanReal(ini->getNextToken());
	if (v[0] > v[1])
	{
		const float t = v[0];
		v[0] = v[1];
		v[1] = t;
	}
}

// RW 0x42F382: two ints, swapped when min > max.
void parseDelayPair(INI *ini, void *, void *store, const void *)
{
	int *v = static_cast<int *>(store);
	v[0] = ini->scanInt(ini->getNextToken());
	v[1] = ini->scanInt(ini->getNextToken());
	if (v[0] > v[1])
	{
		const int t = v[0];
		v[0] = v[1];
		v[1] = t;
	}
}

// RW 0x5D9620: `VolumeSliderMultiplier = Slider:<name> Multiplier:<percent>`; appended to the event's list.
void parseVolumeSliderMultiplier(INI *ini, void *, void *store, const void *)
{
	std::vector<SliderMultiplier> *list = static_cast<std::vector<SliderMultiplier> *>(store);
	SliderMultiplier m;
	const char *token = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!token || AsciiStringUtil::compareNoCase(token, "Slider") != 0)
	{
		throw INIException(3, "Slider:slidername expected after VolumeSliderMultiplier");
	}
	m.slider = ini->scanIndexList(ini->getNextToken(ini->getSepsColon()), theSubmixSliderNames);
	token = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!token || AsciiStringUtil::compareNoCase(token, "Multiplier") != 0)
	{
		throw INIException(3, "Multiplier:number expected after VolumeSliderMultiplier = Slider:slidername");
	}
	m.multiplier = ini->scanPercentToReal(ini->getNextToken(ini->getSepsColon()));
	list->push_back(m);
}
} // namespace

// RW table 0xBEFDF0, in the binary's row order.
const FieldParse *AudioEventInfo::getFieldParse()
{
#define AEI(member) (int)offsetof(AudioEventInfo, member)
	static const FieldParse table[] = {
		{ "Filename", INI::parseAsciiString, nullptr, AEI(filename) },
		{ "Volume", INI::parsePercentToReal, nullptr, AEI(volume) },
		{ "VolumeShift", INI::parsePercentToReal, nullptr, AEI(volumeShift) },
		{ "PerFileVolumeShift", INI::parsePercentToReal, nullptr, AEI(perFileVolumeShift) },
		{ "MinVolume", INI::parsePercentToReal, nullptr, AEI(minVolume) },
		{ "PitchShift", parsePitchShiftPair, nullptr, AEI(pitchShift) },
		{ "PerFilePitchShift", parsePitchShiftPair, nullptr, AEI(perFilePitchShift) },
		{ "PlayPercent", INI::parsePercentToReal, nullptr, AEI(playPercent) },
		{ "Delay", parseDelayPair, nullptr, AEI(delay) },
		{ "Limit", INI::parseInt, nullptr, AEI(limit) },
		{ "Priority", INI::parseIndexList, theAudioPriorityNames, AEI(priority) },
		{ "Type", INI::parseBitString32, theSoundTypeNames, AEI(type) },
		{ "Control", INI::parseBitString32, theAudioControlNames, AEI(control) },
		{ "Sounds", parseSoundsList, nullptr, AEI(sounds) },
		{ "Attack", parseSoundsList, nullptr, AEI(attack) },
		{ "Decay", parseSoundsList, nullptr, AEI(decay) },
		{ "MinRange", INI::parseReal, nullptr, AEI(minRange) },
		{ "MaxRange", INI::parseReal, nullptr, AEI(maxRange) },
		{ "LowPassCutoff", INI::parsePercentToReal, nullptr, AEI(lowPassCutoff) },
		{ "ZoomedInOffscreenVolumePercent", INI::parsePercentToReal, nullptr, AEI(zoomedInOffscreenVolumePercent) },
		{ "ZoomedInOffscreenMinVolumePercent", INI::parsePercentToReal, nullptr, AEI(zoomedInOffscreenMinVolumePercent) },
		{ "ZoomedInOffscreenOcclusionPercent", INI::parsePercentToReal, nullptr, AEI(zoomedInOffscreenOcclusionPercent) },
		{ "ReverbEffectLevel", INI::parsePercentToReal, nullptr, AEI(reverbEffectLevel) },
		{ "DryLevel", INI::parsePercentToReal, nullptr, AEI(dryLevel) },
		{ "SubmixSlider", INI::parseIndexList, theSubmixSliderNames, AEI(submixSlider) },
		{ "VolumeSliderMultiplier", parseVolumeSliderMultiplier, nullptr, AEI(volumeSliderMultipliers) },
		{ nullptr, nullptr, nullptr, 0 }
	};
#undef AEI
	return table;
}

// ---------------------------------------------------------------------------------------------
AudioEventInfoStore::InfoPtr AudioEventInfoStore::find(const std::string &name) const
{
	const auto it = m_byName.find(name);
	return it == m_byName.end() ? InfoPtr() : it->second;
}

AudioEventInfoStore::InfoPtr AudioEventInfoStore::findOrCreate(const std::string &name)
{
	const auto it = m_byName.find(name);
	if (it != m_byName.end())
	{
		return it->second;
	}
	InfoPtr info = std::make_shared<AudioEventInfo>();
	m_byName.emplace(name, info);
	m_ordered.push_back(info);
	return info;
}

void AudioEventInfoStore::clear()
{
	m_byName.clear();
	m_ordered.clear();
}

// RW 0x5DA10F (0 -> "DefaultMusicTrack", string 0xBF0504), 0x5DA159 (2 -> "DefaultSoundEffect" 0xBF0524), 0x5DA1A3 (1 -> "DefaultDialog" 0xBF0544),
// 0x5DA1ED (3 -> "DefaultAmbientStream" 0xBF0564), 0x5DA237 (4 -> "DefaultStreamedSound" 0xBF058C).
const char *AudioDefaultNameFor(int soundType)
{
	switch (soundType)
	{
	case AT_Music:
		return "DefaultMusicTrack";
	case AT_Streaming:
		return "DefaultDialog";
	case AT_SoundEffect:
		return "DefaultSoundEffect";
	case AT_AmbientStream:
		return "DefaultAmbientStream";
	case AT_StreamedSound:
		return "DefaultStreamedSound";
	default:
		return nullptr;
	}
}

// RW 0x5D9EFF (see AudioEventInfo.h).
void ParseAudioEventInfoBlock(INI *ini, AudioEventInfoStore &store, int soundType, const char *blockName)
{
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		throw INIException(3, "You cannot define or override a %s in map.ini", blockName);
	}
	const std::string name = ini->getNextToken();
	AudioEventInfoStore::InfoPtr info = store.findOrCreate(name);

	// the default event of this keyword: copied over the event, then the DEFAULT bit is cleared (RW 0x5D9FA3-0x5D9FCB)
	const char *defaultName = AudioDefaultNameFor(soundType);
	if (defaultName)
	{
		AudioEventInfoStore::InfoPtr def = store.find(defaultName);
		if (def && def != info)
		{
			*info = *def;
		}
		if (def)
		{
			info->type &= ~(unsigned)ST_DEFAULT;
		}
	}
	info->audioName = name;
	info->soundType = soundType;
	ini->initFromINI(info.get(), AudioEventInfo::getFieldParse());

	// RW 0x5D9FF3-0x5DA0DB, after the fields
	if (soundType == AT_SoundEffect && (info->control & AC_RANDOMSTART))
	{
		// retail prints "Control flag 'RANDOMSTART' is not valid for <name>. Streaming sound types only, please." to the debug console
		info->control &= ~(unsigned)AC_RANDOMSTART;
	}
	info->control &= ~(unsigned)AC_PLAY_ONE;
	if (info->delay[0] < 0)
	{
		info->delay[0] = 0;
		if (info->delay[1] < 0)
		{
			info->delay[1] = 0;
		}
	}
	// pitch shifts at or below -100 percent are held at -99 (the constants RW 0xBE5988 / 0xBF046C)
	for (float *pair : { info->pitchShift, info->perFilePitchShift })
	{
		if (pair[0] <= -100.0f)
		{
			pair[0] = -99.0f;
			if (pair[1] <= -100.0f)
			{
				pair[1] = -99.0f;
			}
		}
	}
	// a volume shift is a reduction: outside [-100, 0] it is 0
	for (float *shift : { &info->volumeShift, &info->perFileVolumeShift })
	{
		if (-100.0f > *shift || *shift > 0.0f)
		{
			*shift = 0.0f;
		}
	}
}

// RW 0x5D9C6D + the Subsounds parser 0x5DA4F7.
void ParseMultisoundBlock(INI *ini, AudioEventInfoStore &store)
{
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		throw INIException(3, "You cannot define or override a Multisound in map.ini");
	}
	const std::string name = ini->getNextToken();
	AudioEventInfoStore::InfoPtr info = store.findOrCreate(name);
	info->subsounds.clear();
	info->subsoundsTotalWeight = 0;
	info->audioName = name;
	info->soundType = AT_Multisound;

	struct MultisoundParse
	{
		static void parseControl(INI *ini2, void *, void *s, const void *ud) { INI::parseBitString32(ini2, nullptr, s, ud); }
		static void parseSubsounds(INI *ini2, void *instance, void *, const void *ud)
		{
			AudioEventInfo *self = static_cast<AudioEventInfo *>(instance);
			AudioEventInfoStore *st = static_cast<AudioEventInfoStore *>(const_cast<void *>(ud));
			WeightedSoundList tokens;
			parseWeightedTokens(ini2, self->audioName, tokens);
			for (const WeightedSound &ws : tokens.sounds)
			{
				AudioEventInfoStore::InfoPtr sub = st->find(ws.name);
				if (!sub && AsciiStringUtil::compareNoCase(ws.name, "NoSound") != 0)
				{
					throw INIException(3, "Unknown subsound '%s' in multisound", ws.name.c_str());
				}
				if (sub == st->find(self->audioName))
				{
					throw INIException(3, "Multisound '%s' cannot use itself as a subsound", self->audioName.c_str());
				}
				Subsound s;
				s.name = ws.name;
				s.weight = ws.weight;
				s.info = sub;
				self->subsounds.push_back(s);
				self->subsoundsTotalWeight += ws.weight;
			}
		}
	};
	// RW table 0xBEFFA4: Control (+0x4C) and Subsounds (+0x80)
	const FieldParse table[] = {
		{ "Control", MultisoundParse::parseControl, theAudioControlNames, (int)offsetof(AudioEventInfo, control) },
		{ "Subsounds", MultisoundParse::parseSubsounds, &store, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(info.get(), table);

	if (info->control & ~(unsigned)(AC_LOOP | AC_PLAY_ONE))
	{
		throw INIException(1, "PLAY_ONE and LOOP are the only valid control flags for Multisounds");
	}
	if (info->control & AC_LOOP)
	{
		if (!(info->control & AC_PLAY_ONE))
		{
			throw INIException(1, "Multisound control flag \"LOOP\" is supported only in conjunction with flag PLAY_ONE. See me if you need this changed.");
		}
		for (const Subsound &s : info->subsounds)
		{
			if (s.info && s.info->soundType != AT_Music)
			{
				throw INIException(1, "Multisound control flag \"LOOP\" is supported only when all subsounds are MusicTracks. See me if you need this changed.");
			}
		}
	}
}
