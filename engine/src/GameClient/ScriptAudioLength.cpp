// OpenBFME. GPL-3.0.
// See GameClient/ScriptAudioLength.h.

#include "GameClient/ScriptAudioLength.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEventRTS.h"
#include "Common/Audio/AudioIni.h"

bool ScriptAudioLength::lengthMs(const AudioIniState &audio, AudioAssetCache &files, const std::string &name, std::int32_t &ms, bool &picked)
{
	ms = 0;
	picked = false;
	const AudioEventInfoStore::InfoPtr info = audio.infos.find(name);
	if (!info)
	{
		return false;
	}
	AudioEventInfo first = *info;
	for (WeightedSoundList *list : { &first.sounds, &first.attack, &first.decay })
	{
		if (list->sounds.size() > 1)
		{
			picked = true;
			list->sounds.resize(1);
		}
	}
	std::int64_t total = 0;
	for (const std::string &file : AudioEventFiles(audio.settings, first))
	{
		AudioDecode::AudioInfo probe;
		std::string error;
		if (files.probe(file, &probe, &error) && probe.sampleRate != 0)
		{
			total += (std::int64_t)(probe.frames * 1000u / probe.sampleRate);
		}
	}
	ms = (std::int32_t)total;
	return true;
}

std::shared_ptr<AudioAssetCache> ScriptAudioLength::makeProbeCache(ArchiveFileSystem &fs)
{
	return std::make_shared<AudioAssetCache>(&fs, 0);
}
