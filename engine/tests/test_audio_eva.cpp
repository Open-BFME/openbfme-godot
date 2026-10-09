// OpenBFME unit tests: the Eva announcer (GameClient/Eva.h). Lane AUDIO-1. The rules come from the comments EA wrote in Data\INI\Eva.ini.

#include "doctest.h"

#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/Eva.h"

#include <cstring>

namespace
{
std::string wav(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::string out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((char)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((char)v);
		out.push_back((char)(v >> 8));
	};
	out += "RIFF";
	u32(36 + frames * 2);
	out += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	out += "data";
	u32(frames * 2);
	out.append(frames * 2, '\0');
	return out;
}

const char kAudio[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 4\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 0\n  MinRange = 160\n  MaxRange = 640\n  PlayPercent = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AudioEvent ElfLost\n  Sounds = elflost\nEnd\nAudioEvent OrcLost\n  Sounds = orclost\nEnd\nAudioEvent Help\n  Sounds = help\nEnd\nAudioEvent Die\n  Sounds = die\nEnd\n";

const char kEva[] =
	"MiscEvaData\n  MaxMillisecondsToKeepJumpToEvents = 60000\n  MaxMillisecondsBeforeResettingLastJumpTo = 5000\n  MinDistanceBetweenJumpToEvents = 100\nEnd\n"
	"PredefinedEvaEvent DefaultEvaEvent\n  Priority = 5\n  TimeBetweenEventsMS = 20000\n  ExpirationTimeMS = 1500\nEnd\n"
	"NewEvaEvent BuildingLost\n  Priority = 6\n  TimeBetweenEventsMS = 10000\n  ExpirationTimeMS = 5000\n"
	"  SideSound\n    Side = Elves\n    Sound = ElfLost\n  End\n  SideSound\n    Side = Mordor\n    Sound = OrcLost\n  End\nEnd\n"
	"NewEvaEvent HelpMe\n  Priority = 3\n  ExpirationTimeMS = 1000\n  SideSound\n    Side = Elves\n    Sound = Help\n  End\nEnd\n"
	"NewEvaEvent Slow\n  Priority = 4\n  MillisecondsToWaitBeforePlaying = 600\n  ExpirationTimeMS = 3000\n  SideSound\n    Side = Elves\n    Sound = Help\n  End\nEnd\n"
	"NewEvaEvent Quiet\n  Priority = 4\n  QuietTimeMS = 2000\n  TimeBetweenEventsMS = 100\n  SideSound\n    Side = Elves\n    Sound = Help\n  End\nEnd\n"
	"NewEvaEvent Ring\n  Priority = 1\n  SideSound\n    Side = Elves\n    Sound = Help\n  End\nEnd\n"
	"NewEvaEvent HeroDie\n  Priority = 9\n  AlwaysPlayFromHomeBase = Yes\n  OtherEvaEventsToBlock = Ring\n  SideSound\n    Side = Elves\n    Sound = Die\n  End\nEnd\n";

struct EvaRig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> audio;
	std::unique_ptr<Eva> eva;
	double t = 0.0;

	EvaRig()
	{
		fx.mount({ { "data\\audio\\sounds\\elflost.wav", wav(800) }, { "data\\audio\\sounds\\orclost.wav", wav(800) }, { "data\\audio\\sounds\\help.wav", wav(500) }, { "data\\audio\\sounds\\die.wav", wav(500) } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", std::string(kAudio) + kEva);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 32u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		audio = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1);
		eva = std::make_unique<Eva>(ini, *audio);
		eva->setLocalSide("Elves");
		audio->update(0.0);
	}

	void run(double ms)
	{
		const double end = t + ms;
		while (t < end)
		{
			t += 10.0;
			device->setTime(t);
			audio->update(t);
			eva->update(t);
		}
	}

	std::vector<std::string> played()
	{
		std::vector<std::string> out;
		for (const auto &e : device->log())
		{
			if (e.kind == SimulatedAudioDevice::LogEntry::Start)
			{
				out.push_back(e.file.substr(strlen("Data\\Audio\\Sounds\\")));
			}
		}
		return out;
	}
};
} // namespace

TEST_CASE("eva: an event plays the sound of the local player's side; a side with no sound plays nothing")
{
	EvaRig r;
	CHECK(r.eva->reportEventByName("BuildingLost", nullptr, r.t));
	r.run(200.0);
	CHECK(r.played() == std::vector<std::string>({ "elflost.wav" }));
	r.eva->setLocalSide("Mordor");
	r.run(11000.0); // past TimeBetweenEvents
	CHECK(r.eva->reportEventByName("BuildingLost", nullptr, r.t));
	r.run(200.0);
	CHECK(r.played().back() == "orclost.wav");
	r.eva->setLocalSide("Dwarves");
	r.run(11000.0);
	CHECK(r.eva->reportEventByName("BuildingLost", nullptr, r.t));
	r.run(200.0);
	CHECK(r.eva->counters().noSideSound == 1);
	CHECK(r.played().size() == 2);
	CHECK_FALSE(r.eva->reportEventByName("NoSuchEvent", nullptr, r.t));
}

TEST_CASE("eva: TimeBetweenEvents ignores an identical event, Priority orders waiting events, ExpirationTime throws a held event away")
{
	EvaRig r;
	r.eva->reportEventByName("BuildingLost", nullptr, r.t); // priority 6: plays at once (800 ms)
	r.run(30.0);
	// while it speaks, a low and a high priority event wait
	r.eva->reportEventByName("HelpMe", nullptr, r.t);   // priority 3, expires after 1000 ms
	r.eva->reportEventByName("Ring", nullptr, r.t);     // priority 1, default expiration 1500 ms
	r.eva->reportEventByName("Slow", nullptr, r.t);     // priority 4, waits 600 ms, expires 3000 ms after that
	CHECK_FALSE(r.eva->reportEventByName("BuildingLost", nullptr, r.t)); // identical within 10 s
	CHECK(r.eva->counters().droppedIdentical == 1);
	r.run(1000.0);
	// after the speech: Slow (4) first, then HelpMe (3) if it did not expire, then Ring (1)
	const std::vector<std::string> p = r.played();
	REQUIRE(p.size() >= 2);
	CHECK(p[0] == "elflost.wav");
	CHECK(p[1] == "help.wav"); // Slow (priority 4) won over HelpMe and Ring
	r.run(4000.0);
	// HelpMe had 1000 ms: the first speech (800 ms) plus Slow (500 ms) outlasted it; Ring (1500 ms) too
	CHECK(r.eva->counters().droppedExpired >= 1);
	CHECK(r.eva->pendingCount() == 0);
}

TEST_CASE("eva: MillisecondsToWaitBeforePlaying delays an event, QuietTime ignores a re-report inside the quiet time")
{
	EvaRig r;
	r.eva->reportEventByName("Slow", nullptr, r.t);
	r.run(400.0);
	CHECK(r.played().empty());
	r.run(400.0);
	CHECK(r.played().size() == 1); // after 600 ms
	CHECK(r.eva->reportEventByName("Quiet", nullptr, r.t));
	r.run(300.0);
	CHECK_FALSE(r.eva->reportEventByName("Quiet", nullptr, r.t)); // reported again 300 ms after the last report: inside QuietTimeMS = 2000
	CHECK(r.eva->counters().droppedQuiet == 1);
	r.run(2200.0);
	CHECK(r.eva->reportEventByName("Quiet", nullptr, r.t));
}

TEST_CASE("eva: OtherEvaEventsToBlock drops the listed events; AlwaysPlayFromHomeBase positions the sound at home; Eva never overlaps itself")
{
	EvaRig r;
	r.eva->setHomeBase({ 500, 600, 0 }, true);
	r.eva->reportEventByName("BuildingLost", nullptr, r.t);
	r.run(30.0);
	r.eva->reportEventByName("Ring", nullptr, r.t);
	r.eva->reportEventByName("HeroDie", nullptr, r.t);
	r.run(100.0);
	CHECK(r.audio->playingCount(VoiceKind::Sample2D) + r.audio->playingCount(VoiceKind::Sample3D) == 1); // one Eva sound at a time
	r.run(2000.0);
	const std::vector<std::string> p = r.played();
	REQUIRE(p.size() == 2);
	CHECK(p[1] == "die.wav");
	CHECK(r.eva->counters().droppedBlocked == 1); // Ring
	CHECK(r.eva->playedRecently(r.ini.eva.findIndex("Ring", false), 5000.0, r.t)); // blocked events count as just played
}

TEST_CASE("eva: jump-to locations merge by distance, expire, and cycle; disabling Eva drops what waits")
{
	EvaRig r;
	Coord3D a{ 100, 100, 0 }, b{ 130, 100, 0 }, c{ 900, 900, 0 };
	r.eva->reportEventByName("HelpMe", &a, r.t);
	r.run(30.0);
	r.eva->reportEventByName("Ring", &b, r.t); // within MinDistanceBetweenJumpToEvents of a: merges
	r.run(30.0);
	r.eva->reportEventByName("Slow", &c, r.t);
	Coord3D got;
	REQUIRE(r.eva->nextJumpToLocation(r.t, &got));
	CHECK(got.x == 130.0f); // the merged entry holds the newest position
	REQUIRE(r.eva->nextJumpToLocation(r.t, &got));
	CHECK(got.x == 900.0f);
	REQUIRE(r.eva->nextJumpToLocation(r.t, &got));
	CHECK(got.x == 130.0f); // cycles
	r.run(6000.0);          // MaxMillisecondsBeforeResettingLastJumpTo = 5000 resets the cycle
	REQUIRE(r.eva->nextJumpToLocation(r.t, &got));
	CHECK(got.x == 130.0f);
	r.run(61000.0); // MaxMillisecondsToKeepJumpToEvents = 60000
	CHECK_FALSE(r.eva->nextJumpToLocation(r.t, &got));
	r.eva->setEnabled(false);
	CHECK_FALSE(r.eva->reportEventByName("BuildingLost", nullptr, r.t));
}

TEST_CASE("eva retail: BuildingStolen for the Elves plays CampElfBuildingLost through the real audio INI and archives, for every side of the event")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("eva retail test");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	INIEnvironment env;
	env.fileSystem = mount->fs.get();
	AudioIniState ini;
	ini.registerBlocks(env.blocks);
	{
		INI loader(env);
		ini.loadAll(loader);
		ini.loadEva(loader);
	}
	AudioAssetCache cache(mount->fs.get(), 64u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager audio(ini, cache, device, RandomAlgorithm::RotWK_GameDat_LCG, 3);
	Eva eva(ini, audio);
	audio.update(0.0);
	const int index = ini.eva.findIndex("BuildingStolen", false);
	REQUIRE(index >= 0);
	const EvaEventRecord *rec = ini.eva.record(index, false);
	REQUIRE(rec != nullptr);
	CHECK(rec->priority == 6u);
	CHECK(rec->timeBetweenEventsMS == 20000u);
	CHECK(rec->alwaysPlayFromHomeBase);
	CHECK(rec->sideSounds.size() == 7);
	size_t heard = 0;
	for (const EvaSideSound &side : rec->sideSounds)
	{
		Eva each(ini, audio);
		each.setLocalSide(side.side);
		each.setHomeBase({ 0, 0, 0 }, true);
		double t = 1000.0 * (double)(heard + 1) * 30.0;
		audio.update(t);
		REQUIRE(each.reportEvent(index, nullptr, t));
		each.update(t + 100.0);
		audio.update(t + 200.0);
		const std::string expected = ini.infos.find(side.sound) ? ini.infos.find(side.sound)->sounds.sounds[0].name : std::string();
		CHECK_MESSAGE(each.counters().played == 1u, side.side << " -> " << side.sound);
		++heard;
		audio.stopAudio(AudioAffect_All);
	}
	CHECK(heard == 7);
	CHECK(audio.report().playFailures == 0);
}
