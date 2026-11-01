#pragma once

#include "doctest.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioLog.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/Audio/AudioIni.h"
#include "GameClient/GUI/ShellServices.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;

namespace hudaudio
{
struct RetailAudio
{
	INIEnvironment env;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> manager;

	explicit RetailAudio(ArchiveFileSystem &fs)
	{
		env.fileSystem = &fs;
		ini.registerBlocks(env.blocks);
		INI parser(env);
		REQUIRE_NOTHROW(ini.loadAll(parser));
		REQUIRE(parser.loadedFiles() == AudioIniState::loadOrder());
		cache = std::make_unique<AudioAssetCache>(&fs, 16u * 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		manager = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::RotWK_GameDat_LCG, 1u);
		manager->enableEventLog(true);
		AudioApi::install(manager.get());
	}

	~RetailAudio() { AudioApi::install(nullptr); }
};

class RecordingShellServices : public ::RecordingShellServices
{
public:
	void playSound(const std::string &name) override
	{
		::RecordingShellServices::playSound(name);
		AudioLog::Scope sound("HUD movie");
		AudioApi::playUiSound(name);
	}
};

inline std::vector<std::string> takeRequests(AudioManager &manager, const std::string &origin)
{
	std::vector<std::string> names;
	for (const AudioLogEntry &entry : manager.takeEventLog())
	{
		if (!entry.play && entry.origin.find(origin) != std::string::npos)
		{
			names.push_back(entry.event);
		}
	}
	return names;
}

inline void printSequence(const std::string &label, const std::vector<std::string> &down, const std::vector<std::string> &up)
{
	auto print = [](const std::vector<std::string> &names) {
		std::printf("[");
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (i != 0)
			{
				std::printf(",");
			}
			std::printf("%s", names[i].c_str());
		}
		std::printf("]");
	};
	std::printf("click audio: %s down=", label.c_str());
	print(down);
	std::printf(" up=");
	print(up);
	std::printf("\n");
}
} // namespace hudaudio
