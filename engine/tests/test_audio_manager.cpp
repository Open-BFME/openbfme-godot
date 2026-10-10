// OpenBFME unit tests: the audio manager core (event selection, limits, priorities, interrupts, delays, loops, attack / decay, volumes,
// distance attenuation, shroud / player filtering, multisounds, music, ambient streams, determinism). Lane AUDIO-1.
//
// Expectations are derived from the sources named in GameAudio.h / AudioEventRTS.h (RW disassembly facts, ZH MilesAudioManager.cpp) and from
// the documentation EA wrote at the top of SoundEffects.ini.

#include "doctest.h"

#include "IniTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <cstring>
#include <map>

namespace
{
// A PCM16 mono 22.05 kHz WAV of `ms` milliseconds (silence).
std::vector<std::uint8_t> makeWav(double ms)
{
	const std::uint32_t rate = 22050;
	const std::uint32_t frames = (std::uint32_t)(ms * rate / 1000.0);
	std::vector<std::uint8_t> out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((std::uint8_t)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((std::uint8_t)v);
		out.push_back((std::uint8_t)(v >> 8));
	};
	auto tag = [&](const char *t) { out.insert(out.end(), t, t + 4); };
	tag("RIFF");
	u32(36 + frames * 2);
	tag("WAVE");
	tag("fmt ");
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	tag("data");
	u32(frames * 2);
	out.insert(out.end(), frames * 2, 0);
	return out;
}

std::string bytesToString(const std::vector<std::uint8_t> &b)
{
	return std::string(b.begin(), b.end());
}

const char kBase[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 2\n  SampleCount3D = 3\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n"
	"  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n";

struct Rig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	double t = 0.0;

	Rig(const std::string &events, const std::vector<std::pair<std::string, double>> &files, std::uint32_t seed = 1234)
	{
		initest::FileList list;
		for (const auto &f : files)
		{
			list.push_back({ f.first, bytesToString(makeWav(f.second)) });
		}
		if (list.empty())
		{
			list.push_back({ "data\\ini\\none.txt", "x" });
		}
		fx.mount(list);
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", std::string(kBase) + events);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 64u * 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, seed);
		mgr->update(0.0);
	}

	// advances `ms` of time in steps the manager's tick divides evenly
	void run(double ms)
	{
		const double end = t + ms;
		while (t < end)
		{
			t += 10.0;
			device->setTime(t);
			mgr->update(t);
		}
	}
};

std::string snd(const char *name)
{
	return std::string("data\\audio\\sounds\\") + name + ".wav";
}
} // namespace

TEST_CASE("audio manager: a UI sound plays the file Data\\Audio\\Sounds\\<name>.wav and ends after its length")
{
	Rig r("AudioEvent Click\n  Sounds = click\n  Type = ui everyone\nEnd\n", { { snd("click"), 300.0 } });
	const AudioHandle h = r.mgr->playSound("Click");
	CHECK(h >= AHSV_FirstHandle);
	r.run(100.0);
	REQUIRE(r.device->log().size() >= 1);
	CHECK(r.device->log()[0].file == "Data\\Audio\\Sounds\\click.wav");
	CHECK(r.mgr->isCurrentlyPlaying(h));
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 1);
	// lane AUDIO-5: a 2D voice plays at Miles' default pan: volume ^ (5/3) x 2^-0.3 in both channels
	REQUIRE(r.device->voices().size() == 1);
	CHECK(r.device->voices().begin()->second.params.gainLeft == doctest::Approx(0.8122522f));
	CHECK(r.device->voices().begin()->second.params.gainRight == doctest::Approx(0.8122522f));
	r.run(400.0);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(h));
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 0);
	CHECK(r.mgr->report().played == 1);
}

TEST_CASE("audio manager: an unknown event is AHSV_NoSound (RW 0x45CEA7) and reported by name, NoSound and an empty name are AHSV_NoSound")
{
	Rig r("", {});
	CHECK(r.mgr->playSound("Nope") == AHSV_NoSound);
	CHECK(r.mgr->report().unknownEvents == 1);
	CHECK(r.mgr->report().errors.empty()); // the play failures only (lane AUDIO-4)
	REQUIRE(r.mgr->report().unknownEventNames.size() == 1);
	CHECK(r.mgr->report().unknownEventNames.begin()->first == "Nope");
	CHECK(r.mgr->playSound("NoSound") == AHSV_NoSound);
	CHECK(r.mgr->playSound("") == AHSV_NoSound);
}

TEST_CASE("audio manager: a missing sound file is a reported play failure, never a silent success")
{
	Rig r("AudioEvent Ghost\n  Sounds = ghost\nEnd\n", {});
	r.mgr->playSound("Ghost");
	r.run(100.0);
	CHECK(r.mgr->report().playFailures == 1);
	REQUIRE(r.mgr->report().errors.size() == 1);
	CHECK(r.mgr->report().errors[0].find("Data\\Audio\\Sounds\\ghost.wav") != std::string::npos);
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 0);
}

TEST_CASE("audio manager: random selection follows the weights and never repeats the previous pick; SEQUENTIAL cycles in order")
{
	// weights 1000 / 3000: over many events the heavy file wins about three times in four (the no-repeat rule holds across
	// separate events: the last index lives in the event info)
	Rig r("AudioEvent Pick\n  Sounds = a:1000 b:3000 c:1000\n  Limit = 0\n  Type = ui everyone\nEnd\n"
		  "AudioEvent Seq\n  Sounds = a b c\n  Control = SEQUENTIAL LOOP\n  Limit = 0\n  Type = ui everyone\nEnd\n",
		{ { snd("a"), 50.0 }, { snd("b"), 50.0 }, { snd("c"), 50.0 } });
	std::map<std::string, int> counts;
	std::string last;
	int repeats = 0;
	for (int i = 0; i < 400; ++i)
	{
		AudioEventRTS e("Pick");
		e.setAudioEventInfo(r.ini.infos.find("Pick"));
		const std::string file = e.getFilename(r.mgr->env());
		counts[file]++;
		repeats += (file == last);
		last = file;
	}
	CHECK(repeats == 0); // the first generation of each event starts from the info's last index
	const int b = counts["Data\\Audio\\Sounds\\b.wav"];
	CHECK(b > 150);
	CHECK(b < 330);
	CHECK(counts["Data\\Audio\\Sounds\\a.wav"] + counts["Data\\Audio\\Sounds\\c.wav"] + b == 400);

	// one looping sequential event: a, b, c, a, ... in file order (the first pick of an event instance is index 0 + 1 mod 3 after the
	// constructor index -1: the sequence starts at the first entry)
	AudioEventRTS s("Seq");
	s.setAudioEventInfo(r.ini.infos.find("Seq"));
	std::string order;
	for (int i = 0; i < 6; ++i)
	{
		s.markDirty();
		order += s.getFilename(r.mgr->env()).substr(strlen("Data\\Audio\\Sounds\\"), 1);
	}
	CHECK(order == "abcabc");
}

TEST_CASE("audio manager: PlayPercent 0 never plays, 100 always plays")
{
	Rig r("AudioEvent Never\n  Sounds = a\n  PlayPercent = 0\n  Limit = 0\nEnd\nAudioEvent Always\n  Sounds = a\n  PlayPercent = 100\n  Limit = 0\nEnd\n", { { snd("a"), 30.0 } });
	for (int i = 0; i < 20; ++i)
	{
		r.mgr->playSound("Never");
		r.run(40.0);
	}
	CHECK(r.mgr->report().played == 0);
	CHECK(r.mgr->report().culledPlayPercent == 20);
	for (int i = 0; i < 20; ++i)
	{
		r.mgr->playSound("Always");
		r.run(40.0);
	}
	CHECK(r.mgr->report().played == 20);
}

TEST_CASE("audio manager: Limit culls the extra instances; INTERRUPT replaces the oldest instance of the event")
{
	Rig r("AudioEvent Cap\n  Sounds = long\n  Limit = 2\nEnd\nAudioEvent Intr\n  Sounds = long\n  Limit = 2\n  Control = INTERRUPT\nEnd\n", { { snd("long"), 2000.0 } });
	const AudioHandle h1 = r.mgr->playSound("Cap");
	const AudioHandle h2 = r.mgr->playSound("Cap");
	const AudioHandle h3 = r.mgr->playSound("Cap");
	CHECK(h1 >= AHSV_FirstHandle);
	CHECK(h2 >= AHSV_FirstHandle);
	CHECK(h3 == AHSV_NoSound);
	CHECK(r.mgr->report().culledLimit == 1);
	r.run(100.0);
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 2);

	// interrupting: two playing, a third starts and the oldest is replaced
	AudioHandle i1 = r.mgr->playSound("Intr");
	r.run(70.0);
	// the channel pool (2) is full of the two Cap instances: free it first
	r.mgr->killAudioEventImmediately(h1);
	r.mgr->killAudioEventImmediately(h2);
	r.run(70.0);
	i1 = r.mgr->playSound("Intr");
	const AudioHandle i2 = r.mgr->playSound("Intr");
	r.run(70.0);
	const AudioHandle i3 = r.mgr->playSound("Intr");
	r.run(70.0);
	CHECK(i3 >= AHSV_FirstHandle);
	CHECK(r.mgr->report().interrupted >= 1);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(i1));
	CHECK(r.mgr->isCurrentlyPlaying(i2));
	CHECK(r.mgr->isCurrentlyPlaying(i3));
}

TEST_CASE("audio manager: with every 2D channel busy a higher priority event steals the lowest, an equal priority one is culled")
{
	Rig r("AudioEvent Low\n  Sounds = long\n  Priority = low\n  Limit = 0\nEnd\nAudioEvent High\n  Sounds = long\n  Priority = high\n  Limit = 0\nEnd\nAudioEvent Low2\n  Sounds = long\n  Priority = low\n  Limit = 0\nEnd\n",
		{ { snd("long"), 3000.0 } });
	const AudioHandle l1 = r.mgr->playSound("Low");
	const AudioHandle l2 = r.mgr->playSound("Low");
	r.run(70.0);
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 2); // SampleCount2D = 2
	CHECK(r.mgr->playSound("Low2") == AHSV_NoSound);        // equal priority: nothing lower to kill
	CHECK(r.mgr->report().culledNoChannel == 1);
	const AudioHandle h = r.mgr->playSound("High");
	CHECK(h >= AHSV_FirstHandle);
	r.run(70.0);
	CHECK(r.mgr->report().stolenChannels == 1);
	CHECK(r.mgr->isCurrentlyPlaying(h));
	CHECK((r.mgr->isCurrentlyPlaying(l1) != r.mgr->isCurrentlyPlaying(l2)));
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 2);
}

TEST_CASE("audio manager: Delay holds the request; Attack, Sound and Decay play in that order; stopping a loop plays the Decay")
{
	Rig r("AudioEvent Chain\n  Attack = at\n  Sounds = mid\n  Decay = de\n  Delay = 330 330\n  Limit = 0\nEnd\n"
		  "AudioEvent Loopy\n  Attack = at\n  Sounds = mid\n  Decay = de\n  Control = LOOP\n  Limit = 0\nEnd\n",
		{ { snd("at"), 100.0 }, { snd("mid"), 200.0 }, { snd("de"), 150.0 } });
	r.mgr->playSound("Chain");
	r.run(200.0);
	CHECK(r.device->log().empty()); // the 330 ms delay is not over
	r.run(300.0);
	REQUIRE(r.device->log().size() >= 1);
	CHECK(r.device->log()[0].file == "Data\\Audio\\Sounds\\at.wav");
	r.run(1000.0);
	std::vector<std::string> starts;
	for (const auto &e : r.device->log())
	{
		if (e.kind == SimulatedAudioDevice::LogEntry::Start)
		{
			starts.push_back(e.file.substr(strlen("Data\\Audio\\Sounds\\")));
		}
	}
	REQUIRE(starts.size() == 3);
	CHECK(starts[0] == "at.wav");
	CHECK(starts[1] == "mid.wav");
	CHECK(starts[2] == "de.wav");
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 0);

	// a looping event repeats its Sound until it is removed, then plays the Decay
	const size_t before = r.device->log().size();
	const AudioHandle h = r.mgr->playSound("Loopy");
	r.run(1200.0);
	size_t mids = 0;
	for (size_t i = before; i < r.device->log().size(); ++i)
	{
		mids += r.device->log()[i].file.find("mid.wav") != std::string::npos && r.device->log()[i].kind == SimulatedAudioDevice::LogEntry::Start;
	}
	CHECK(mids >= 4);
	CHECK(r.mgr->isCurrentlyPlaying(h));
	r.mgr->removeAudioEvent(h);
	r.run(40.0);
	bool decayStarted = false;
	for (size_t i = before; i < r.device->log().size(); ++i)
	{
		decayStarted |= r.device->log()[i].file.find("de.wav") != std::string::npos && r.device->log()[i].kind == SimulatedAudioDevice::LogEntry::Start;
	}
	CHECK(decayStarted);
	r.run(400.0);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(h));
}

TEST_CASE("audio manager: positional distance falloff is linear between MinRange and MaxRange; beyond MaxRange the sound is culled unless MinVolume > 0")
{
	Rig r("AudioEvent Boom\n  Sounds = a\n  Volume = 100\n  MinRange = 100\n  MaxRange = 500\n  Type = world everyone\n  SubmixSlider = None\n  Limit = 0\nEnd\n"
		  "AudioEvent Faint\n  Sounds = a\n  Volume = 100\n  MinRange = 100\n  MaxRange = 500\n  MinVolume = 20\n  Type = world everyone\n  SubmixSlider = None\n  Limit = 0\nEnd\n",
		{ { snd("a"), 5000.0 } });
	r.mgr->setListenerPosition({ 0, 0, 0 }, { 0, 1, 0 });
	auto volumeAt = [&](const char *name, float x) {
		AudioEventRTS e(name, Coord3D{ x, 0, 0 });
		e.setAudioEventInfo(r.ini.infos.find(name));
		return r.mgr->getEffectiveVolume(e);
	};
	CHECK(volumeAt("Boom", 50) == doctest::Approx(1.0f));
	CHECK(volumeAt("Boom", 100) == doctest::Approx(1.0f));
	CHECK(volumeAt("Boom", 300) == doctest::Approx(0.5f));
	CHECK(volumeAt("Boom", 500) == doctest::Approx(0.0f));
	CHECK(volumeAt("Faint", 500) == doctest::Approx(0.2f)); // the MinVolume floor
	CHECK(r.mgr->playSoundAt("Boom", { 600, 0, 0 }) == AHSV_NoSound);
	CHECK(r.mgr->report().culledDistance == 1);
	CHECK(r.mgr->playSoundAt("Faint", { 600, 0, 0 }) >= AHSV_FirstHandle);
	const AudioHandle near = r.mgr->playSoundAt("Boom", { 300, 0, 0 });
	CHECK(near >= AHSV_FirstHandle);
	r.run(100.0);
	CHECK(r.mgr->playingCount(VoiceKind::Sample3D) == 2);
	// lane AUDIO-5: both sounds are due right of the listener at its height: Miles Fast 2D puts them entirely in the right channel at
	// volume ^ (5/3) (Boom 0.5 by distance, Faint its MinVolume 0.2: above MinSampleVolume, so Miles' maximum distance is twice
	// GlobalMaxRange and it is not muted at 600)
	int checked = 0;
	for (const auto &kv : r.device->voices())
	{
		const VoiceParams &p = kv.second.params;
		CHECK(p.gainLeft == doctest::Approx(0.0f));
		CHECK(p.gainRight == doctest::Approx(std::pow(p.volume, 5.0f / 3.0f)).epsilon(1e-4));
		CHECK((p.volume == doctest::Approx(0.5f) || p.volume == doctest::Approx(0.2f)));
		++checked;
	}
	CHECK(checked == 2);
}

TEST_CASE("audio manager: SHROUDED events are culled by a shroud query; a missing query is counted, never assumed")
{
	Rig r("AudioEvent Sh\n  Sounds = a\n  Type = world everyone shrouded\n  MaxRange = 5000\n  Limit = 0\nEnd\n", { { snd("a"), 500.0 } });
	r.mgr->setListenerPosition({ 0, 0, 0 }, { 0, 1, 0 });
	CHECK(r.mgr->playSoundAt("Sh", { 10, 0, 0 }) >= AHSV_FirstHandle);
	CHECK(r.mgr->report().missingHooks.size() == 1);
	AudioWorldQueries q;
	q.localPlayerIndex = [] { return 0; };
	q.isShroudClear = [](int, const Coord3D &p) { return p.x < 100.0f; };
	r.mgr->setWorldQueries(q);
	CHECK(r.mgr->playSoundAt("Sh", { 10, 0, 0 }) >= AHSV_FirstHandle);
	CHECK(r.mgr->playSoundAt("Sh", { 200, 0, 0 }) == AHSV_NoSound);
	CHECK(r.mgr->report().culledShroud == 1);
}

TEST_CASE("audio manager: PLAYER / ALLIES / ENEMIES filter by the owning player's relationship to the local player")
{
	Rig r("AudioEvent Mine\n  Sounds = a\n  Type = ui player\n  Limit = 0\nEnd\nAudioEvent Ally\n  Sounds = a\n  Type = ui allies\n  Limit = 0\nEnd\nAudioEvent Foe\n  Sounds = a\n  Type = ui enemies\n  Limit = 0\nEnd\n",
		{ { snd("a"), 500.0 } });
	AudioWorldQueries q;
	q.localPlayerIndex = [] { return 1; };
	q.playerExists = [](int i) { return i >= 0 && i < 4; };
	q.relationship = [](int owner, int) { return owner == 2 ? AR_ALLIES : (owner == 3 ? AR_ENEMIES : AR_NEUTRAL); };
	r.mgr->setWorldQueries(q);
	CHECK(r.mgr->playSoundForPlayer("Mine", 1) >= AHSV_FirstHandle);
	CHECK(r.mgr->playSoundForPlayer("Mine", 2) == AHSV_NotForLocal);
	CHECK(r.mgr->playSoundForPlayer("Ally", 2) >= AHSV_FirstHandle);
	CHECK(r.mgr->playSoundForPlayer("Ally", 1) == AHSV_NotForLocal); // the local player is not their own ally here
	CHECK(r.mgr->playSoundForPlayer("Foe", 3) >= AHSV_FirstHandle);
	CHECK(r.mgr->playSoundForPlayer("Foe", 2) == AHSV_NotForLocal);
	CHECK(r.mgr->playSoundForPlayer("Mine", -1) >= AHSV_FirstHandle); // PLAYER + UI without an owner plays for the local player
}

TEST_CASE("audio manager: a Multisound plays all its subsounds, PLAY_ONE plays one weighted pick")
{
	Rig r("AudioEvent S1\n  Sounds = a\n  Limit = 0\nEnd\nAudioEvent S2\n  Sounds = a\n  Limit = 0\nEnd\n"
		  "Multisound All\n  Subsounds = S1 S2\nEnd\nMultisound One\n  Control = PLAY_ONE\n  Subsounds = S1:1 S2:1000\nEnd\n",
		{ { snd("a"), 2000.0 } });
	r.mgr->playSound("All");
	r.run(70.0);
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) == 2);
	r.mgr->stopAudio(AudioAffect_Sound);
	std::map<std::string, int> picks;
	for (int i = 0; i < 100; ++i)
	{
		r.mgr->playSound("One");
		r.run(40.0);
		for (const std::string &n : r.mgr->playingEventNames())
		{
			picks[n]++;
		}
		r.mgr->stopAudio(AudioAffect_Sound);
	}
	CHECK(picks["S2"] > 90); // weight 1000 against 1
}

TEST_CASE("audio manager: music replaces music, FADE_ON_KILL fades the old track, a looping PLAY_ONE multisound chains tracks")
{
	Rig r("MusicTrack T1\n  Filename = t1.mp3\n  Control = fade_on_kill\nEnd\nMusicTrack T2\n  Filename = t2.mp3\nEnd\n"
		  "Multisound List\n  Control = PLAY_ONE LOOP\n  Subsounds = T1 T2\nEnd\n",
		{});
	// the music files are MP3 in retail; the device length override stands in for them
	r.fx.mount({ { "data\\audio\\tracks\\t1.mp3", "x" }, { "data\\audio\\tracks\\t2.mp3", "x" } });
	r.device->setFileLength("Data\\Audio\\Tracks\\t1.mp3", 1000.0);
	r.device->setFileLength("Data\\Audio\\Tracks\\t2.mp3", 1000.0);
	r.mgr->playMusic("T1");
	r.run(100.0);
	CHECK(r.mgr->isMusicPlaying());
	CHECK(r.mgr->getMusicTrackName() == "T1");
	CHECK(r.mgr->playingCount(VoiceKind::Stream) == 1);
	r.mgr->playMusic("T2");
	r.run(100.0);
	CHECK(r.mgr->getMusicTrackName() == "T2");
	CHECK(r.mgr->playingCount(VoiceKind::Stream) == 1);
	// T1 asked to fade on kill: its voice is still alive for a moment, then gone
	size_t alive = 0;
	for (const auto &kv : r.device->voices())
	{
		alive += (!kv.second.stopped && kv.second.start.file.find("t1.mp3") != std::string::npos);
	}
	CHECK(alive <= 1);
	r.run(600.0);
	size_t t1Alive = 0;
	for (const auto &kv : r.device->voices())
	{
		t1Alive += (!kv.second.stopped && kv.second.start.file.find("t1.mp3") != std::string::npos);
	}
	CHECK(t1Alive == 0);

	// the looping multisound: each track plays once (the device does not loop), then the next one is picked
	r.mgr->stopMusic(false);
	r.mgr->playMusic("List");
	r.run(100.0);
	CHECK(r.mgr->isMusicPlaying());
	const size_t starts0 = r.device->log().size();
	r.run(3500.0);
	size_t startsNow = 0;
	for (size_t i = starts0; i < r.device->log().size(); ++i)
	{
		startsNow += r.device->log()[i].kind == SimulatedAudioDevice::LogEntry::Start;
	}
	CHECK(startsNow >= 2);
	CHECK(r.mgr->isMusicPlaying());
	r.mgr->stopMusic(false);
	CHECK_FALSE(r.mgr->isMusicPlaying());
	r.run(3000.0);
	CHECK_FALSE(r.mgr->isMusicPlaying()); // the chain ended with the stop
}

TEST_CASE("audio manager: VolumeSliderMultiplier ducks the other events of the named slider while the event plays; NONE is never adjusted")
{
	Rig r("AudioEvent Speaker\n  Sounds = long\n  SubmixSlider = Voice\n  VolumeSliderMultiplier = Slider:SoundFX Multiplier:50\n  Limit = 0\nEnd\n"
		  "AudioEvent Effect\n  Sounds = a\n  SubmixSlider = SoundFX\n  Limit = 0\nEnd\nAudioEvent Fixed\n  Sounds = a\n  SubmixSlider = None\n  Limit = 0\nEnd\n",
		{ { snd("long"), 5000.0 }, { snd("a"), 300.0 } });
	auto vol = [&](const char *n) {
		AudioEventRTS e(n);
		e.setAudioEventInfo(r.ini.infos.find(n));
		return r.mgr->getEffectiveVolume(e);
	};
	CHECK(vol("Effect") == doctest::Approx(1.0f));
	r.mgr->playSound("Speaker");
	r.run(100.0);
	CHECK(vol("Effect") == doctest::Approx(0.5f));
	CHECK(vol("Fixed") == doctest::Approx(1.0f));
	CHECK(vol("Speaker") == doctest::Approx(1.0f)); // immune
	r.mgr->stopAudio(AudioAffect_Sound);
	r.run(100.0);
	CHECK(vol("Effect") == doctest::Approx(1.0f));
}

TEST_CASE("audio manager: the submix slider defaults by type (RW 0x5D90C3) and the system / script volumes scale the result")
{
	Rig r("AudioEvent Fx\n  Sounds = a\nEnd\nAudioEvent Vo\n  Sounds = a\n  Type = ui everyone voice\nEnd\nAudioEvent Am\n  Sounds = a\n  SubmixSlider = Ambient\nEnd\n", { { snd("a"), 300.0 } });
	r.mgr->setVolume(0.5f, AudioAffect_Sound | AudioAffect_SystemSetting);
	r.mgr->setVolume(0.25f, AudioAffect_Speech | AudioAffect_SystemSetting);
	r.mgr->setVolume(0.8f, AudioAffect_Speech); // script volume
	auto vol = [&](const char *n) {
		AudioEventRTS e(n);
		e.setAudioEventInfo(r.ini.infos.find(n));
		return r.mgr->getEffectiveVolume(e);
	};
	CHECK(vol("Fx") == doctest::Approx(0.5f));
	CHECK(vol("Vo") == doctest::Approx(0.25f * 0.8f)); // the VOICE type bit selects the Voice slider
	CHECK(vol("Am") == doctest::Approx(1.0f));
}

TEST_CASE("audio manager: ambient streams are markers: the loudest play, a quieter one takes over only by the hysteresis")
{
	Rig r("AmbientStream Wind\n  Filename = wind.mp3\n  Type = everyone\nEnd\nAmbientStream Birds\n  Filename = birds.mp3\n  Type = everyone\nEnd\nAmbientStream Surf\n  Filename = surf.mp3\n  Type = everyone\nEnd\n",
		{});
	r.fx.mount({ { "data\\audio\\ambientstreams\\wind.mp3", "x" }, { "data\\audio\\ambientstreams\\birds.mp3", "x" }, { "data\\audio\\ambientstreams\\surf.mp3", "x" } });
	for (const char *f : { "wind", "birds", "surf" })
	{
		r.device->setFileLength(std::string("Data\\Audio\\AmbientStreams\\") + f + ".mp3", 60000.0);
	}
	r.ini.audioLOD[1].defined = true;
	r.ini.audioLOD[1].maximumAmbientStreams = 2;
	r.mgr->setListenerPosition({ 0, 0, 0 }, { 0, 1, 0 });
	// the ambient streams are positional through their owner position; the effective volume falls with distance
	const AudioHandle w = r.mgr->playSoundAt("Wind", { 120, 0, 0 });
	const AudioHandle b = r.mgr->playSoundAt("Birds", { 400, 0, 0 });
	const AudioHandle s = r.mgr->playSoundAt("Surf", { 800, 0, 0 });
	CHECK(w >= AHSV_FirstHandle);
	r.run(200.0);
	CHECK(r.mgr->playingCount(VoiceKind::Stream) == 2); // MaximumAmbientStreams of the High LOD
	CHECK(r.mgr->isCurrentlyPlaying(w));
	CHECK(r.mgr->isCurrentlyPlaying(b));
	r.mgr->removeAudioEvent(w);
	r.run(100.0);
	CHECK(r.mgr->playingCount(VoiceKind::Stream) == 2); // Surf takes the free place
	(void)s;
}

TEST_CASE("audio manager: never touches the logic random generator and the same seed gives the same decisions")
{
	// the process-wide logic generator is deliberately NOT initialised: it throws std::logic_error on any draw (PLAN rule 10), so every manager call
	// below proves the audio path never reaches for it. A local instance stands in for "the logic stream" to check its state stays put.
	GameLogicRandom logicProbe(RandomAlgorithm::ZH_CarryChain);
	logicProbe.seedRandom(77);
	const auto logicBefore = logicProbe.seedArray();
	auto drive = [&](std::uint32_t seed) {
		Rig r("AudioEvent Pick\n  Sounds = a b c d\n  PitchShift = -20 20\n  VolumeShift = -30\n  Delay = 0 100\n  PlayPercent = 70\n  Limit = 0\n  Type = ui everyone\nEnd\n",
			{ { snd("a"), 60.0 }, { snd("b"), 60.0 }, { snd("c"), 60.0 }, { snd("d"), 60.0 } }, seed);
		for (int i = 0; i < 60; ++i)
		{
			r.mgr->playSound("Pick");
			r.run(50.0);
		}
		std::string trace;
		for (const auto &e : r.device->log())
		{
			if (e.kind == SimulatedAudioDevice::LogEntry::Start)
			{
				trace += e.file.back() == 'v' ? e.file.substr(e.file.size() - 5, 1) : "?";
				trace += std::to_string((int)std::lround(r.device->find(e.voice)->params.pitch * 1000)) + ",";
			}
		}
		return trace;
	};
	const std::string first = drive(5);
	CHECK(first == drive(5));
	CHECK(first != drive(6));
	CHECK(logicProbe.seedArray() == logicBefore);
}

TEST_CASE("audio manager: a stall longer than the catch-up cap drops steps and counts them")
{
	Rig r("AudioEvent A\n  Sounds = a\nEnd\n", { { snd("a"), 100.0 } });
	r.mgr->update(100000.0);
	CHECK(r.mgr->report().droppedTicks > 0);
	CHECK(r.mgr->report().ticks == 8);
}

TEST_CASE("audio entry points: a call with no manager installed returns AHSV_Error and is counted; installed, the named calls reach the manager")
{
	AudioApi::install(nullptr);
	const std::uint64_t before = AudioApi::callsWithoutAudio();
	CHECK(AudioApi::playUiSound("Click") == AHSV_Error);
	CHECK(AudioApi::playSoundAtPosition("Click", { 1, 2, 3 }) == AHSV_Error);
	CHECK(AudioApi::playSoundForObject("Click", 5) == AHSV_Error);
	CHECK(AudioApi::playSoundForDrawable("Click", 5) == AHSV_Error);
	CHECK(AudioApi::callsWithoutAudio() == before + 4);
	CHECK_FALSE(AudioApi::isValidEvent("Click"));

	Rig r("AudioEvent Click\n  Sounds = click\n  Type = ui world everyone\n  Limit = 0\nEnd\n", { { snd("click"), 300.0 } });
	AudioApi::install(r.mgr.get());
	CHECK(AudioApi::isValidEvent("Click"));
	CHECK(AudioApi::playUiSound("Click") >= AHSV_FirstHandle);
	CHECK(AudioApi::playSoundAtPosition("Click", { 10, 0, 0 }) >= AHSV_FirstHandle);
	r.run(100.0);
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) + r.mgr->playingCount(VoiceKind::Sample3D) == 2);
	AudioApi::install(nullptr);
}

namespace
{
// An owner table whose objects can disappear (the world removes a unit while its sound plays).
struct VanishingOwners : AudioOwnerResolver
{
	std::set<std::uint32_t> alive;
	bool objectPosition(std::uint32_t id, Coord3D *pos) const override
	{
		if (!alive.count(id))
		{
			return false;
		}
		*pos = Coord3D{ 50.0f, 0.0f, 0.0f };
		return true;
	}
	bool drawablePosition(std::uint32_t, Coord3D *) const override { return false; }
};

size_t startsOfLog(const SimulatedAudioDevice &d, const char *needle)
{
	size_t n = 0;
	for (const auto &e : d.log())
	{
		n += e.kind == SimulatedAudioDevice::LogEntry::Start && e.file.find(needle) != std::string::npos;
	}
	return n;
}
} // namespace

TEST_CASE("audio manager: a sound whose owner disappears is stopped (no use of the erased list node); a Decay plays out")
{
	Rig r("AudioEvent Plain\n  Sounds = long\n  Type = world everyone\n  Limit = 0\nEnd\n"
		  "AudioEvent WithDecay\n  Sounds = long\n  Decay = de\n  Type = world everyone\n  Limit = 0\nEnd\n",
		{ { snd("long"), 5000.0 }, { snd("de"), 200.0 } });
	VanishingOwners owners;
	owners.alive = { 7, 8 };
	r.mgr->setOwnerResolver(&owners);
	r.mgr->setListenerPosition({ 0, 0, 0 }, { 0, 1, 0 });
	const AudioHandle plain = r.mgr->playSoundForObject("Plain", 7);
	const AudioHandle decay = r.mgr->playSoundForObject("WithDecay", 8);
	r.run(200.0);
	REQUIRE(plain >= AHSV_FirstHandle);
	CHECK(r.mgr->isCurrentlyPlaying(plain));
	CHECK(r.mgr->isCurrentlyPlaying(decay));
	CHECK(r.mgr->playingCount(VoiceKind::Sample3D) == 2);
	owners.alive.clear(); // both objects are gone; the sounds are 5 s long
	r.run(100.0);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(plain));
	CHECK(startsOfLog(*r.device, "de.wav") == 1); // the Decay of the second plays; the first has none
	r.run(600.0);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(decay));
	CHECK(r.mgr->playingCount(VoiceKind::Sample3D) == 0);
	r.mgr->setOwnerResolver(nullptr);
}

TEST_CASE("audio manager: finished voices are released to the device, so a small pool serves any number of sounds")
{
	Rig r("AudioEvent Blip\n  Sounds = blip\n  Type = ui everyone\n  Limit = 0\nEnd\n", { { snd("blip"), 50.0 } });
	r.device->setPoolSize(3);
	const int count = 60; // far beyond the pool: only recycled voices can play them
	for (int i = 0; i < count; ++i)
	{
		CHECK(r.mgr->playSound("Blip") >= AHSV_FirstHandle);
		r.run(120.0);
		CHECK(r.device->heldVoices() <= 1);
	}
	CHECK(r.mgr->report().played == (std::uint64_t)count);
	CHECK(r.mgr->report().playFailures == 0);
	CHECK(r.device->heldVoices() == 0);
}

TEST_CASE("audio manager: looping events that never pass PlayPercent are tried once per tick, never in an endless loop")
{
	Rig r("AudioEvent ZeroLoop\n  Sounds = a\n  PlayPercent = 0\n  Control = LOOP\n  Limit = 0\nEnd\n"
		  "AudioEvent NegativeLoop\n  Sounds = a\n  PlayPercent = -10\n  Control = LOOP\n  Limit = 0\nEnd\n",
		{ { snd("a"), 30.0 } });
	r.mgr->playSound("ZeroLoop");
	r.mgr->playSound("NegativeLoop");
	const std::uint64_t ticks0 = r.mgr->report().ticks;
	const std::uint64_t requeued0 = r.mgr->report().requeuedLoops;
	r.run(500.0); // would never return before the fix
	const std::uint64_t ticks = r.mgr->report().ticks - ticks0;
	CHECK(ticks >= 10);
	CHECK(r.mgr->report().requeuedLoops - requeued0 == 2 * ticks); // each of the two requests is processed exactly once per tick
	CHECK(r.mgr->report().played == 0);
	CHECK(r.mgr->pendingRequests() == 2);
}

TEST_CASE("audio manager: a requeued request waits for the next tick and keeps its place behind the delayed ones")
{
	Rig r("AudioEvent Late\n  Sounds = a\n  Delay = 200 200\n  Limit = 0\nEnd\nAudioEvent Now\n  Sounds = b\n  Limit = 0\nEnd\n", { { snd("a"), 30.0 }, { snd("b"), 30.0 } });
	r.mgr->playSound("Late");
	r.mgr->playSound("Now");
	r.run(100.0);
	CHECK(startsOfLog(*r.device, "b.wav") == 1);
	CHECK(startsOfLog(*r.device, "a.wav") == 0); // the delay counts down in tick steps
	r.run(300.0);
	CHECK(startsOfLog(*r.device, "a.wav") == 1);
}

TEST_CASE("audio manager: stopAudio cancels delayed requests as well as the playing voices")
{
	Rig r("AudioEvent Later\n  Sounds = a\n  Delay = 500 500\n  Limit = 0\nEnd\nAudioEvent Click\n  Sounds = a\n  Limit = 0\nEnd\n"
		  "AudioEvent Spatial\n  Sounds = a\n  Delay = 500 500\n  Type = world everyone\n  Limit = 0\nEnd\n",
		{ { snd("a"), 400.0 } });
	r.mgr->setListenerPosition({ 0, 0, 0 }, { 0, 1, 0 });
	const AudioHandle later = r.mgr->playSound("Later");
	r.mgr->playSoundAt("Spatial", { 20, 0, 0 });
	r.run(100.0);
	CHECK(r.mgr->pendingRequests() == 2);
	r.mgr->stopAudio(AudioAffect_Sound3D); // only the positional one goes
	CHECK(r.mgr->pendingRequests() == 1);
	CHECK(r.mgr->isCurrentlyPlaying(later));
	r.mgr->stopAudio(AudioAffect_All);
	CHECK(r.mgr->pendingRequests() == 0);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(later));
	r.run(1500.0);
	CHECK(r.device->log().empty());
	CHECK(r.mgr->report().played == 0);
	CHECK(r.mgr->playingCount(VoiceKind::Sample2D) + r.mgr->playingCount(VoiceKind::Sample3D) == 0);
}

TEST_CASE("audio manager: stopAudio(AmbientStream) removes the ambient markers so nothing restarts; fades and the music chain stop with it")
{
	Rig r("AmbientStream Wind\n  Filename = wind.mp3\n  Type = everyone\nEnd\n"
		  "MusicTrack T1\n  Filename = t1.mp3\n  Control = fade_on_kill\nEnd\nMusicTrack T2\n  Filename = t2.mp3\nEnd\n"
		  "Multisound List\n  Control = PLAY_ONE LOOP\n  Subsounds = T1 T2\nEnd\n",
		{});
	r.fx.mount({ { "data\\audio\\ambientstreams\\wind.mp3", "x" }, { "data\\audio\\tracks\\t1.mp3", "x" }, { "data\\audio\\tracks\\t2.mp3", "x" } });
	r.device->setFileLength("Data\\Audio\\AmbientStreams\\wind.mp3", 60000.0);
	r.device->setFileLength("Data\\Audio\\Tracks\\t1.mp3", 500.0);
	r.device->setFileLength("Data\\Audio\\Tracks\\t2.mp3", 500.0);
	r.mgr->setListenerPosition({ 0, 0, 0 }, { 0, 1, 0 });
	const AudioHandle wind = r.mgr->playSoundAt("Wind", { 50, 0, 0 });
	r.mgr->playMusic("List");
	r.run(200.0);
	CHECK(r.mgr->isCurrentlyPlaying(wind));
	CHECK(r.mgr->isMusicPlaying());
	r.mgr->stopAudio(AudioAffect_AmbientStream);
	CHECK_FALSE(r.mgr->isCurrentlyPlaying(wind)); // the marker is gone
	r.run(500.0);
	CHECK(startsOfLog(*r.device, "wind.mp3") == 1); // and nothing started it again
	CHECK(r.mgr->isMusicPlaying());                 // music was not part of the stop
	r.mgr->stopMusic(true);                         // a fading track
	r.mgr->playMusic("List");
	r.run(100.0);
	r.mgr->stopAudio(AudioAffect_All);              // takes the playing track, any fade and the chain
	CHECK_FALSE(r.mgr->isMusicPlaying());
	r.run(3000.0);
	CHECK_FALSE(r.mgr->isMusicPlaying());
	CHECK(r.device->heldVoices() == 0);
}

TEST_CASE("audio manager: unverified stops S-240 .. S-249, S-1930 and S-1931 are all reported by the manager and registered in docs/STOPS.md")
{
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE_MESSAGE(static_cast<bool>(in), "cannot read docs/STOPS.md");
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string doc = ss.str();
	std::set<std::string> ids;
	for (const std::string &line : AudioManager::unverified())
	{
		REQUIRE(line.size() > 6);
		REQUIRE(line.compare(0, 2, "S-") == 0);
		const std::string id = line.substr(0, line.find(':'));
		ids.insert(id);
		CHECK_MESSAGE(doc.find("| " + id + " |") != std::string::npos, "docs/STOPS.md has no row for " << id);
	}
	for (int n = 240; n <= 249; ++n)
	{
		CHECK_MESSAGE(ids.count("S-" + std::to_string(n)) == 1, "S-" << n << " is not reported by AudioManager::unverified()");
	}
	CHECK(ids.count("S-1930") == 1); // lane AUDIO-5: the Miles provider / stereo image
	CHECK(ids.count("S-1931") == 1);
}
