// OpenBFME unit tests: retail audio resolution. Every file a retail audio event can play resolves through the archive file system exactly as
// retail names it and decodes; the files that do not exist are retail data defects pinned EXACTLY (tests/data/audio/retail_files.json,
// written by the independent scan tools/audio/audio_files_census.py). Lane AUDIO-1.

#include "doctest.h"

#include "RetailTestMount.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEventRTS.h"
#include "Common/Audio/AudioIni.h"
#include "Common/AsciiString.h"
#include "Common/MiniJson.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>

namespace
{
struct RetailFiles
{
	bool available = false;
	std::string mountError;
	INIEnvironment env;
	AudioIniState state;
	std::unique_ptr<AudioAssetCache> cache;
	std::map<std::string, std::string> files;     // lower-case path -> keyword of the first event referencing it
	std::vector<std::string> missing;             // lower-case paths that do not resolve
	std::vector<std::string> decodeFailures;
	std::map<std::string, int> keywordCount;
	double decodeSeconds = 0.0, loadSeconds = 0.0;
	std::uint64_t decodedBytes = 0;
	std::map<std::string, std::pair<int, double>> perExtension; // files, seconds

	RetailFiles()
	{
		retailtest::Mount *mount = retailtest::pureMount();
		if (!mount)
		{
			mountError = "ROTWK_INSTALL / BFME2_INSTALL not set";
			return;
		}
		available = true;
		if (!mount->error.empty() || !mount->fs)
		{
			mountError = mount->error.empty() ? "mount failed" : mount->error;
			return;
		}
		env.fileSystem = mount->fs.get();
		state.registerBlocks(env.blocks);
		INI ini(env);
		state.loadAll(ini);
		cache = std::make_unique<AudioAssetCache>(mount->fs.get(), 96u * 1024u * 1024u);
		static const char *kKeyword[] = { "MusicTrack", "DialogEvent", "AudioEvent", "AmbientStream", "StreamedSound", "Multisound" };
		for (const auto &info : state.infos.all())
		{
			for (const std::string &f : AudioEventFiles(state.settings, *info))
			{
				files.emplace(AsciiStringUtil::lowered(f), kKeyword[info->soundType]);
			}
		}
		const auto t0 = std::chrono::steady_clock::now();
		for (const auto &kv : files)
		{
			if (!cache->exists(kv.first))
			{
				missing.push_back(kv.first);
				continue;
			}
			const auto d0 = std::chrono::steady_clock::now();
			std::string err;
			// WAV files decode in full; an MP3 track (125 ms to decode on average, the whole set takes ~90 s) is probed: its exact frame walk proves the
			// container, and the full decode of every MP3 is the decoder corpus test (test_audio_decode.cpp). AUDIO_DECODE_ALL=1 decodes them here too.
			const bool isMp3 = kv.first.size() > 4 && kv.first.compare(kv.first.size() - 4, 4, ".mp3") == 0;
			std::shared_ptr<const AudioDecode::DecodedAudio> audio;
			if (isMp3 && !std::getenv("AUDIO_DECODE_ALL"))
			{
				AudioDecode::AudioInfo probed;
				if (cache->probe(kv.first, &probed, &err))
				{
					audio = std::make_shared<AudioDecode::DecodedAudio>();
				}
			}
			else
			{
				audio = cache->decoded(kv.first, &err);
			}
			const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - d0).count();
			const std::string ext = kv.first.substr(kv.first.rfind('.'));
			perExtension[ext].first++;
			perExtension[ext].second += s;
			if (!audio)
			{
				decodeFailures.push_back(kv.first + ": " + err);
			}
			else
			{
				decodedBytes += audio->pcm.size() * 2;
			}
		}
		decodeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}
};

RetailFiles &retail()
{
	static RetailFiles r;
	return r;
}

bool haveRetail()
{
	RetailFiles &r = retail();
	if (!r.available)
	{
		retailtest::printSkip("audio retail file tests");
		return false;
	}
	REQUIRE_MESSAGE(r.mountError.empty(), "retail mount failed:\n" << r.mountError);
	return true;
}

JsonValue loadGolden()
{
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/audio/retail_files.json", bytes, &err), err);
	JsonValue root;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), root, &err), err);
	return root;
}
} // namespace

TEST_CASE("audio retail: the files every event can play equal the independent scan, the missing ones are exactly the pinned data defects")
{
	if (!haveRetail())
	{
		return;
	}
	RetailFiles &r = retail();
	const JsonValue golden = loadGolden();
	CHECK((int)r.files.size() == (int)golden.get("distinctFiles")->number);
	std::map<std::string, int> byKeyword;
	for (const auto &kv : r.files)
	{
		byKeyword[kv.second]++;
	}
	for (const auto &kv : golden.get("distinctFilesByKeyword")->object)
	{
		CHECK_MESSAGE(byKeyword[kv.first] == (int)kv.second.number, kv.first);
	}
	std::vector<std::string> pinned;
	for (const JsonValue &v : golden.get("missing")->array)
	{
		pinned.push_back(v.string);
	}
	std::sort(pinned.begin(), pinned.end());
	std::vector<std::string> got = r.missing;
	std::sort(got.begin(), got.end());
	CHECK(got == pinned);
	for (const std::string &m : got)
	{
		if (!std::binary_search(pinned.begin(), pinned.end(), m))
		{
			MESSAGE("new missing file: " << m);
		}
	}
	std::printf("audio retail: %zu event infos, %zu distinct files, %zu missing (pinned), decode+resolve %.2f s, %.1f MB decoded in total\n", r.state.infos.size(), r.files.size(), r.missing.size(),
		r.decodeSeconds, (double)r.decodedBytes / (1024.0 * 1024.0));
	for (const auto &kv : r.perExtension)
	{
		std::printf("audio retail: %s %d files, %.2f s\n", kv.first.c_str(), kv.second.first, kv.second.second);
	}
	const AudioAssetCache::Stats &st = r.cache->stats();
	std::printf("audio retail: cache peak %.1f MB, %llu evictions\n", (double)st.peakBytesCached / (1024.0 * 1024.0), (unsigned long long)st.evictions);
}

TEST_CASE("audio retail: every file that exists decodes (MP3: probes)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailFiles &r = retail();
	for (const std::string &f : r.decodeFailures)
	{
		MESSAGE(f);
	}
	CHECK(r.decodeFailures.empty());
}

TEST_CASE("audio retail: MiscAudio, the shell music and every Multisound subsound resolve to events with files")
{
	if (!haveRetail())
	{
		return;
	}
	const AudioIniState &s = retail().state;
	for (const char *slot : { "LowLODShellMusic", "HighLODShellMusic", "ScoreScreenMusic", "FullScreenSubMenuMusic", "CreditsMusic", "VolumeSampleMusic" })
	{
		const std::string &name = s.miscAudio.get(slot);
		CHECK_MESSAGE(!name.empty(), slot);
		const auto info = s.infos.find(name);
		const bool found = info != nullptr;
		REQUIRE_MESSAGE(found, slot << " -> " << name);
	}
	// the main menu music of RotWK: MiscAudio LowLODShellMusic = ShellLowLOD (a Multisound of MusicTracks)
	const auto shell = s.infos.find(s.miscAudio.get("LowLODShellMusic"));
	const bool shellFound = shell != nullptr;
	REQUIRE(shellFound);
	CHECK(shell->soundType == AT_Multisound);
	CHECK((shell->control & AC_LOOP) != 0);
	for (const auto &info : s.infos.all())
	{
		if (info->soundType != AT_Multisound)
		{
			continue;
		}
		for (const Subsound &sub : info->subsounds)
		{
			if (sub.info)
			{
				const bool selfReference = sub.info.get() == info.get();
				CHECK_MESSAGE(!selfReference, info->audioName);
			}
		}
	}
}

TEST_CASE("audio retail: the retail object world parses the audio blocks for real and installs the audioEventExists hook")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("audio retail object world");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	const auto savedHook = TheContainParseHooks().audioEventExists;
	{
		RetailObjectWorld world(*mount->fs);
		std::string error;
		REQUIRE_MESSAGE(world.load(&error), error);
		CHECK(world.audio().infos.size() == 7988); // the one subsystem load fills the world's table: nothing is recorded as a stub any more
		REQUIRE(static_cast<bool>(TheContainParseHooks().audioEventExists));
		CHECK(TheContainParseHooks().audioEventExists("Gui_ShellMapMouseOver"));
		CHECK_FALSE(TheContainParseHooks().audioEventExists("NoSuchAudioEvent"));
		CHECK_FALSE(TheContainParseHooks().audioEventExists(""));
	}
	CHECK(static_cast<bool>(TheContainParseHooks().audioEventExists) == static_cast<bool>(savedHook)); // restored when the world dies
}
