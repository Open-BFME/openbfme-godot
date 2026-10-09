// OpenBFME unit tests: the audio INI blocks (AudioEvent / MusicTrack / DialogEvent / AmbientStream / StreamedSound / Multisound /
// AudioSettings / MiscAudio / AudioLOD / AudioLowMHz / AnimationSoundClientBehaviorGlobalSetting). Lane AUDIO-1.
//
// Expectations come from the RotWK binary (RW addresses in Common/Audio/AudioEventInfo.h) and from the retail INI through an
// independent Python scan (tools/audio/audio_ini_census.py -> tests/data/audio/ini_census.json).

#include "doctest.h"

#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioIni.h"
#include "Common/MiniJson.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{
struct AudioFixture
{
	initest::Fixture fx;
	AudioIniState state;
	AudioFixture() { state.registerBlocks(fx.env.blocks); }

	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return initest::loadError(fx.env, "synthetic.ini", text, type, code);
	}
};

const char kDefaults[] =
	"AudioEvent DefaultSoundEffect\n"
	"  Volume = 100\n"
	"  Priority = high\n"
	"  Limit = 4\n"
	"  MinRange = 160\n"
	"  MaxRange = 640\n"
	"  PitchShift = -0 0\n"
	"  PlayPercent = 100\n"
	"  Type = ui player\n"
	"  Type = +DEFAULT\n"
	"End\n"
	"MusicTrack DefaultMusicTrack\n"
	"  Filename = NoFilename\n"
	"  Volume = 50.0\n"
	"  Type = DEFAULT\n"
	"  SubmixSlider = music\n"
	"End\n";
} // namespace

TEST_CASE("audio ini: an AudioEvent starts from DefaultSoundEffect, its DEFAULT bit is cleared and the default itself keeps it")
{
	AudioFixture f;
	REQUIRE_MESSAGE(f.load(std::string(kDefaults) + "AudioEvent Boom\n  Sounds = boom1 boom2\n  Type = world\nEnd\n").empty(), "load");
	auto def = f.state.infos.find("DefaultSoundEffect");
	auto boom = f.state.infos.find("Boom");
	REQUIRE(def);
	REQUIRE(boom);
	CHECK((def->type & ST_DEFAULT) != 0);
	CHECK((boom->type & ST_DEFAULT) == 0);
	CHECK(boom->type == ST_WORLD); // a plain name list replaces the bits copied from the default
	CHECK(boom->volume == doctest::Approx(1.0f)); // Volume = 100 from the default, percent scaled
	CHECK(boom->priority == AP_HIGH);
	CHECK(boom->limit == 4);
	CHECK(boom->minRange == doctest::Approx(160.0f));
	CHECK(boom->maxRange == doctest::Approx(640.0f));
	CHECK(boom->playPercent == doctest::Approx(1.0f));
	CHECK(boom->soundType == AT_SoundEffect);
	CHECK(boom->audioName == "Boom");
	REQUIRE(boom->sounds.sounds.size() == 2);
	CHECK(boom->sounds.totalWeight == 2000);
}

TEST_CASE("audio ini: a redefinition resets the event to the default before reading its own fields")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "AudioEvent Boom\n  Limit = 9\n  Sounds = a\nEnd\nAudioEvent Boom\n  Sounds = b\nEnd\n").empty());
	auto boom = f.state.infos.find("Boom");
	REQUIRE(boom);
	CHECK(boom->limit == 4);          // back to the default's limit
	REQUIRE(boom->sounds.sounds.size() == 1); // not appended to the old list
	CHECK(boom->sounds.sounds[0].name == "b");
	CHECK(f.state.infos.size() == 3); // one info per name
}

TEST_CASE("audio ini: Sounds grammar name[:weight], default 1000, weight below 1 and an empty name are errors")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "AudioEvent A\n  Sounds = x:1 y z:250 w:a:7\nEnd\n").empty());
	auto a = f.state.infos.find("A");
	REQUIRE(a);
	REQUIRE(a->sounds.sounds.size() == 4);
	CHECK(a->sounds.sounds[0].weight == 1);
	CHECK(a->sounds.sounds[1].weight == 1000);
	CHECK(a->sounds.sounds[2].weight == 250);
	CHECK(a->sounds.sounds[3].name == "w:a");
	CHECK(a->sounds.sounds[3].weight == 7);
	CHECK(a->sounds.totalWeight == 1 + 1000 + 250 + 7);

	AudioFixture g;
	int code = 0;
	std::string e = g.load(std::string(kDefaults) + "AudioEvent B\n  Sounds = x:0\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(e.find("Weight of sound files must be >= 1. Sound 'x:0' for audio event 'B'") != std::string::npos);
	CHECK(code == 3);
	AudioFixture h;
	e = h.load(std::string(kDefaults) + "AudioEvent C\n  Sounds = :5\nEnd\n");
	CHECK(e.find("Sound file has no file name. Sound ':5' for audio event 'C'") != std::string::npos);
}

TEST_CASE("audio ini: PitchShift / Delay pairs are swapped when min > max, delays clamp at 0, pitch at -99, volume shift into [-1, 0]")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) +
		"AudioEvent P\n  PitchShift = 20 -10\n  PerFilePitchShift = -150 -120\n  Delay = 300 100\n  VolumeShift = 25\n  PerFileVolumeShift = -30\nEnd\n"
		"AudioEvent Q\n  Delay = -5 -7\nEnd\n"
		"AudioEvent R\n  Delay = -5 7\nEnd\n").empty());
	auto p = f.state.infos.find("P");
	CHECK(p->pitchShift[0] == -10.0f);
	CHECK(p->pitchShift[1] == 20.0f);
	CHECK(p->perFilePitchShift[0] == -99.0f);
	CHECK(p->perFilePitchShift[1] == -99.0f);
	CHECK(p->delay[0] == 100);
	CHECK(p->delay[1] == 300);
	CHECK(p->volumeShift == 0.0f);                              // +25% is not a reduction: 0
	CHECK(p->perFileVolumeShift == doctest::Approx(-0.3f));
	auto q = f.state.infos.find("Q");
	CHECK(q->delay[0] == 0);
	CHECK(q->delay[1] == 0);
	auto r = f.state.infos.find("R"); // min -5 sorts first; max 7 is untouched
	CHECK(r->delay[0] == 0);
	CHECK(r->delay[1] == 7);
}

TEST_CASE("audio ini: RANDOMSTART is dropped from an AudioEvent but kept on a MusicTrack; PLAY_ONE never survives a block")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "AudioEvent A\n  Control = LOOP RANDOMSTART INTERRUPT PLAY_ONE\nEnd\nMusicTrack M\n  Control = RANDOMSTART fade_on_kill\nEnd\n").empty());
	CHECK(f.state.infos.find("A")->control == (AC_LOOP | AC_INTERRUPT));
	CHECK(f.state.infos.find("M")->control == (AC_RANDOMSTART | AC_FADE_ON_KILL));
	CHECK(f.state.infos.find("M")->soundType == AT_Music);
}

TEST_CASE("audio ini: Type accepts +/- edits and every retail flag name; an unknown name is an error")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "AudioEvent T\n  Type = world shrouded voice player allies enemies everyone global ui fake\nEnd\n").empty());
	CHECK(f.state.infos.find("T")->type == (ST_UI | ST_WORLD | ST_SHROUDED | ST_GLOBAL | ST_VOICE | ST_PLAYER | ST_ALLIES | ST_ENEMIES | ST_EVERYONE | ST_FAKE));
	AudioFixture g;
	CHECK(g.load(std::string(kDefaults) + "AudioEvent U\n  Type = loud\nEnd\n").find("loud") != std::string::npos);
}

TEST_CASE("audio ini: VolumeSliderMultiplier reads Slider:<name> Multiplier:<percent> and appends")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "AudioEvent V\n  SubmixSlider = voice\n  VolumeSliderMultiplier = Slider:Voice Multiplier:70\n  VolumeSliderMultiplier = Slider:Ambient Multiplier:60\nEnd\n").empty());
	auto v = f.state.infos.find("V");
	CHECK(v->submixSlider == SLIDER_VOICE);
	REQUIRE(v->volumeSliderMultipliers.size() == 2);
	CHECK(v->volumeSliderMultipliers[0].slider == SLIDER_VOICE);
	CHECK(v->volumeSliderMultipliers[0].multiplier == doctest::Approx(0.7f));
	CHECK(v->volumeSliderMultipliers[1].slider == SLIDER_AMBIENT);
	AudioFixture g;
	CHECK(g.load(std::string(kDefaults) + "AudioEvent W\n  VolumeSliderMultiplier = Voice 70\nEnd\n").find("Slider:slidername expected after VolumeSliderMultiplier") != std::string::npos);
	AudioFixture h;
	CHECK(h.load(std::string(kDefaults) + "AudioEvent W\n  VolumeSliderMultiplier = Slider:Voice 70\nEnd\n").find("Multiplier:number expected after VolumeSliderMultiplier = Slider:slidername") !=
		std::string::npos);
}

TEST_CASE("audio ini: a map.ini (load type 2) may not define any audio event block")
{
	for (const char *kw : { "AudioEvent", "MusicTrack", "DialogEvent", "AmbientStream", "StreamedSound" })
	{
		AudioFixture f;
		int code = 0;
		const std::string e = f.load(std::string(kw) + " X\nEnd\n", INI_LOAD_CREATE_OVERRIDES, &code);
		CHECK_MESSAGE(e.find(std::string("You cannot define or override a ") + kw + " in map.ini") != std::string::npos, kw << ": " << e);
		CHECK(code == 3);
	}
	AudioFixture m;
	CHECK(m.load("Multisound X\nEnd\n", INI_LOAD_CREATE_OVERRIDES).find("You cannot define or override a Multisound in map.ini") != std::string::npos);
	AudioFixture s;
	CHECK(s.load("AudioSettings\nEnd\n", INI_LOAD_CREATE_OVERRIDES).find("You cannot define or override the AudioSettings in map.ini") != std::string::npos);
	AudioFixture a;
	CHECK(a.load("AnimationSoundClientBehaviorGlobalSetting\nEnd\n", INI_LOAD_CREATE_OVERRIDES).find("Cannot override AnimationSoundClientBehaviorGlobalSetting in map.ini") != std::string::npos);
}

TEST_CASE("audio ini: Multisound subsounds resolve, control flags are validated")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "MusicTrack T1\n  Filename = a.mp3\nEnd\nMusicTrack T2\n  Filename = b.mp3\nEnd\nAudioEvent S1\n  Sounds = s\nEnd\n"
		"Multisound Good\n  Control = PLAY_ONE LOOP\n  Subsounds = T1 T2:2000 NoSound\nEnd\n").empty());
	auto good = f.state.infos.find("Good");
	REQUIRE(good);
	CHECK(good->soundType == AT_Multisound);
	REQUIRE(good->subsounds.size() == 3);
	CHECK(good->subsounds[0].info == f.state.infos.find("T1"));
	CHECK(good->subsounds[1].weight == 2000);
	CHECK(good->subsounds[2].info == nullptr);
	CHECK(good->subsoundsTotalWeight == 1000 + 2000 + 1000);
	CHECK(good->control == (AC_LOOP | AC_PLAY_ONE));

	auto expect = [&](const char *body, const char *message) {
		AudioFixture g;
		const std::string e = g.load(std::string(kDefaults) + "MusicTrack T1\n  Filename = a.mp3\nEnd\nAudioEvent S1\n  Sounds = s\nEnd\n" + body);
		CHECK_MESSAGE(e.find(message) != std::string::npos, body << " -> " << e);
	};
	expect("Multisound X\n  Subsounds = Missing\nEnd\n", "Unknown subsound 'Missing' in multisound");
	expect("Multisound X\n  Subsounds = X\nEnd\n", "Multisound 'X' cannot use itself as a subsound");
	expect("Multisound X\n  Control = LOOP\n  Subsounds = T1\nEnd\n", "supported only in conjunction with flag PLAY_ONE");
	expect("Multisound X\n  Control = LOOP PLAY_ONE\n  Subsounds = S1\nEnd\n", "supported only when all subsounds are MusicTracks");
	expect("Multisound X\n  Control = INTERRUPT\n  Subsounds = T1\nEnd\n", "PLAY_ONE and LOOP are the only valid control flags for Multisounds");
}

TEST_CASE("audio ini: AudioSettings joins the folders to the root, copies unset Living World values from the tactical group and stores squares")
{
	AudioFixture f;
	REQUIRE_MESSAGE(f.load("AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n"
		"  SoundsExtension = wav\n  MicrophoneMinDistanceToCamera = 100\n  MicrophoneMaxDistanceToCamera = 300\n  LivingWorldMicrophoneMaxDistanceToCamera = 4500\n"
		"  ZoomMinDistance = 130.0\n  DefaultSoundVolume = 70%\n  MinDelayBetweenEnterStateVoiceMS = 5000\n  AutomaticSubtitleTextColor = R:255 G:204 B:0 A:255\n  SampleCount3D = 25\nEnd\n").empty(), "load");
	const AudioSettings &s = f.state.settings;
	CHECK(s.soundsFolder == "Data\\Audio\\Sounds\\");
	CHECK(s.musicFolder == "Data\\Audio\\Tracks\\");
	CHECK(s.streamingFolder == "Data\\Audio\\Speech\\");
	CHECK(s.ambientStreamFolder == "Data\\Audio\\AmbientStreams\\");
	CHECK(s.soundsExtension == "wav");
	CHECK(s.microphone[0].minDistanceToCamera == 100.0f);
	CHECK(s.microphone[0].minDistanceToCameraSq == 10000.0f);
	CHECK(s.microphone[1].minDistanceToCamera == 100.0f);   // not given: the tactical value
	CHECK(s.microphone[1].maxDistanceToCamera == 4500.0f);  // given
	CHECK(s.microphone[1].maxDistanceToCameraSq == 4500.0f * 4500.0f);
	CHECK(s.microphone[2].maxDistanceToCamera == 300.0f);   // the third group has no INI names
	CHECK(s.microphone[0].zoomMinDistanceSq == 16900.0f);
	CHECK(s.defaultSoundVolume == doctest::Approx(0.7f));
	CHECK(s.minDelayBetweenEnterStateVoiceFrames == 25u);   // 5000 ms * 0.005, ceil
	CHECK(s.sampleCount3D == 25);
	CHECK(s.automaticSubtitleTextColor == 0xFFFFCC00u);
}

TEST_CASE("audio ini: MiscAudio resolves names, NoSound clears a slot, an unknown event is Invalid Sound")
{
	AudioFixture f;
	REQUIRE(f.load(std::string(kDefaults) + "AudioEvent Click\n  Sounds = c\nEnd\nMusicTrack Shell\n  Filename = s.mp3\nEnd\n"
		"MiscAudio\n  NoCanDoSound = Click\n  LowLODShellMusic = Shell\n  AllCheerSound = NoSound\n  RallyPointSet = nosound\nEnd\n").empty());
	CHECK(f.state.miscAudio.get("NoCanDoSound") == "Click");
	CHECK(f.state.miscAudio.get("LowLODShellMusic") == "Shell");
	CHECK(f.state.miscAudio.get("AllCheerSound").empty());
	AudioFixture g;
	int code = 0;
	CHECK(g.load(std::string(kDefaults) + "MiscAudio\n  NoCanDoSound = Missing\nEnd\n", INI_LOAD_OVERWRITE, &code).find("Invalid Sound 'Missing'") != std::string::npos);
	CHECK(code == 3);
	AudioFixture h;
	CHECK(h.load(std::string(kDefaults) + "MiscAudio\n  NotAField = Click\nEnd\n").find("NotAField") != std::string::npos);
}

TEST_CASE("audio ini: AudioLOD levels, AudioLowMHz and the animation sound global setting")
{
	AudioFixture f;
	REQUIRE(f.load("AudioLOD = Low\n  AllowDolby = No\n  MaximumAmbientStreams = 0\n  AllowReverb = No\nEnd\nAudioLOD = High\n  AllowDolby = Yes\n  MaximumAmbientStreams = 2\n  AllowReverb = Yes\nEnd\n"
		"AudioLowMHz = 2200\nAnimationSoundClientBehaviorGlobalSetting\n  MinMicrophoneDistanceToDirty = 61\nEnd\n").empty());
	CHECK(f.state.audioLOD[0].defined);
	CHECK_FALSE(f.state.audioLOD[0].allowDolby);
	CHECK(f.state.audioLOD[1].maximumAmbientStreams == 2);
	CHECK(f.state.audioLOD[1].allowReverb);
	CHECK(f.state.audioLowMHz == 2200);
	CHECK(f.state.minMicrophoneDistanceToDirty == 61.0f);
	AudioFixture g;
	CHECK(g.load("AudioLOD = Ultra\nEnd\n").find("Unknown Audio LOD level 'Ultra'") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// Retail: every audio block of the pure 2.01 mount parses with zero errors; the counts match an independent Python scan.
// ---------------------------------------------------------------------------------------------
namespace
{
struct RetailAudio
{
	bool available = false;
	std::string mountError;
	INIEnvironment env;
	AudioIniState state;
	std::vector<std::string> errors;
	double seconds = 0.0;

	RetailAudio()
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
		const auto t0 = std::chrono::steady_clock::now();
		INI ini(env);
		// retail defines its macros in a pre-pass over every INI file; ScoredKillEvaAnnouncer.ini uses `NOT_CREEP`, defined in GameData.ini
		try
		{
			ini.preprocessFile("Data\\INI\\GameData.ini", INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			errors.push_back(std::string("GameData.ini macros: ") + e.message());
		}
		std::vector<std::string> files = AudioIniState::loadOrder();
		for (const std::string &f : EvaEventStore::loadOrder())
		{
			files.push_back(f); // TheEva: after the audio files (the SideSound names must exist)
		}
		for (const std::string &f : LargeGroupAudioStore::loadOrder())
		{
			files.push_back(f);
		}
		files.push_back("Data\\INI\\ScoredKillEvaAnnouncer.ini");
		for (const std::string &file : files)
		{
			try
			{
				ini.load(file, INI_LOAD_OVERWRITE);
			}
			catch (const INIException &e)
			{
				errors.push_back(file + ": " + e.message());
			}
		}
		seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}
};

RetailAudio &retail()
{
	static RetailAudio r;
	return r;
}

bool haveRetail()
{
	RetailAudio &r = retail();
	if (!r.available)
	{
		retailtest::printSkip("audio ini retail tests");
		return false;
	}
	REQUIRE_MESSAGE(r.mountError.empty(), "retail mount failed:\n" << r.mountError);
	return true;
}

int goldenNumber(const JsonValue &root, const char *section, const std::string &key)
{
	const JsonValue *s = root.get(section);
	REQUIRE(s);
	const JsonValue *v = s->get(key);
	REQUIRE_MESSAGE(v, section << "." << key);
	return (int)v->number;
}

JsonValue loadGolden()
{
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/audio/ini_census.json", bytes, &err), err);
	JsonValue root;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), root, &err), err);
	return root;
}
} // namespace

TEST_CASE("audio ini retail: every audio block of the 2.01 mount parses without an error")
{
	if (!haveRetail())
	{
		return;
	}
	RetailAudio &r = retail();
	for (const std::string &e : r.errors)
	{
		MESSAGE(e);
	}
	CHECK(r.errors.empty());
	std::printf("audio ini retail: %zu event infos, %.2f s\n", r.state.infos.size(), r.seconds);
}

TEST_CASE("audio ini retail: block and name counts equal the independent scan (tools/audio/audio_ini_census.py)")
{
	if (!haveRetail())
	{
		return;
	}
	const JsonValue golden = loadGolden();
	RetailAudio &r = retail();
	int perType[6] = { 0, 0, 0, 0, 0, 0 };
	for (const auto &info : r.state.infos.all())
	{
		REQUIRE(info->soundType >= 0);
		REQUIRE(info->soundType <= 5);
		++perType[info->soundType];
	}
	// AudioEvent keyword = type 2 events; Default* / names defined twice count once (the census counts distinct names)
	CHECK(perType[AT_Music] == goldenNumber(golden, "distinctNames", "MusicTrack"));
	CHECK(perType[AT_Streaming] == goldenNumber(golden, "distinctNames", "DialogEvent"));
	CHECK(perType[AT_SoundEffect] == goldenNumber(golden, "distinctNames", "AudioEvent"));
	CHECK(perType[AT_AmbientStream] == goldenNumber(golden, "distinctNames", "AmbientStream"));
	CHECK(perType[AT_StreamedSound] == goldenNumber(golden, "distinctNames", "StreamedSound"));
	CHECK(perType[AT_Multisound] == goldenNumber(golden, "distinctNames", "Multisound"));
	// weighted tokens of every list field
	size_t sounds = 0, attack = 0, decay = 0, subsounds = 0;
	for (const auto &info : r.state.infos.all())
	{
		sounds += info->sounds.sounds.size();
		attack += info->attack.sounds.size();
		decay += info->decay.sounds.size();
		subsounds += info->subsounds.size();
	}
	CHECK((int)sounds == goldenNumber(golden, "listTokens", "AudioEvent.Sounds"));
	CHECK((int)attack == goldenNumber(golden, "listTokens", "AudioEvent.Attack"));
	CHECK((int)decay == goldenNumber(golden, "listTokens", "AudioEvent.Decay"));
	CHECK((int)subsounds == goldenNumber(golden, "listTokens", "Multisound.Subsounds"));
}

TEST_CASE("audio ini retail: pinned values of the settings, the defaults and MiscAudio")
{
	if (!haveRetail())
	{
		return;
	}
	const AudioIniState &s = retail().state;
	CHECK(s.settings.audioRoot == "Data\\Audio");
	CHECK(s.settings.soundsFolder == "Data\\Audio\\Sounds\\");
	CHECK(s.settings.soundsExtension == "wav");
	CHECK(s.settings.sampleCount2D == 4);
	CHECK(s.settings.sampleCount3D == 25);
	CHECK(s.settings.streamCount == 3);
	CHECK(s.settings.globalMaxRange == 5000000);
	CHECK(s.settings.microphone[0].maxDistanceToCamera == 300.0f);
	CHECK(s.settings.microphone[1].maxDistanceToCamera == 4500.0f);
	CHECK(s.settings.microphone[1].minDistanceToCamera == 100.0f); // LivingWorldMicrophoneMinDistanceToCamera is absent from the INI
	CHECK(s.settings.defaultMusicVolume == doctest::Approx(0.7f));
	CHECK(s.settings.minDelayBetweenEnterStateVoiceFrames == 25u);
	const auto def = s.infos.find("DefaultSoundEffect");
	REQUIRE(def);
	CHECK((def->type & ST_DEFAULT) != 0);
	CHECK(def->limit == 4);
	CHECK(s.miscAudio.get("LowLODShellMusic") == "ShellLowLOD");
	CHECK(s.miscAudio.get("ScoreScreenMusic") == "ScoreScreenMusic");
	CHECK(s.miscAudio.get("RadarNotifyOnlineSound").empty());
	CHECK(s.minMicrophoneDistanceToDirty == 61.0f);
}

// ---------------------------------------------------------------------------------------------
// The registered fields equal the binary's tables (tests/data/audio/rw_audio_tables.json, tools/audio/extract_tables.py reads RW game.dat).
// ---------------------------------------------------------------------------------------------
namespace
{
JsonValue loadTables()
{
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/audio/rw_audio_tables.json", bytes, &err), err);
	JsonValue root;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), root, &err), err);
	return root;
}

std::vector<std::string> rowNames(const JsonValue &rows)
{
	std::vector<std::string> names;
	for (const JsonValue &r : rows.array)
	{
		names.push_back(r.get("name")->string);
	}
	return names;
}

std::vector<std::string> arrayOf(const JsonValue &root, const char *key)
{
	std::vector<std::string> out;
	for (const JsonValue &v : root.get("nameArrays")->get(key)->array)
	{
		out.push_back(v.string);
	}
	return out;
}
} // namespace

TEST_CASE("audio ini: the AudioEvent field table has exactly the rows and the row order of RW 0xBEFDF0, and the name arrays are the binary's")
{
	const JsonValue t = loadTables();
	std::vector<std::string> mine;
	for (const FieldParse *f = AudioEventInfo::getFieldParse(); f->token; ++f)
	{
		mine.push_back(f->token);
	}
	CHECK(mine == rowNames(*t.get("eventTable")));
	CHECK(rowNames(*t.get("blocks")->get("AudioEvent")->get("tables")->array.data()) == mine);
	auto names = [](const char *const *a) {
		std::vector<std::string> out;
		for (; *a; ++a)
		{
			out.push_back(*a);
		}
		return out;
	};
	CHECK(names(theAudioPriorityNames) == arrayOf(t, "priority"));
	CHECK(names(theSoundTypeNames) == arrayOf(t, "soundType"));
	CHECK(names(theSubmixSliderNames) == arrayOf(t, "submixSlider"));
	std::vector<std::string> control = arrayOf(t, "control");
	std::vector<std::string> myControl = names(theAudioControlNames);
	CHECK(myControl.size() == control.size());
	for (size_t i = 0; i < control.size() && i < myControl.size(); ++i)
	{
		if (i != 6)
		{
			CHECK(myControl[i] == control[i]);
		}
	}
	CHECK(control[6] == "\not used by INI files"); // the binary placeholder: a newline character followed by "ot used by INI files"
}

TEST_CASE("audio ini: AudioSettings, MiscAudio, AudioLOD and the animation setting register exactly the binary's field names")
{
	const JsonValue t = loadTables();
	std::vector<std::string> binary;
	for (const JsonValue &table : t.get("blocks")->get("AudioSettings")->get("tables")->array)
	{
		for (const std::string &n : rowNames(table))
		{
			binary.push_back(n);
		}
	}
	std::vector<std::string> mine = AudioSettings::fieldNames();
	std::sort(binary.begin(), binary.end());
	std::sort(mine.begin(), mine.end());
	CHECK(mine == binary);
	CHECK(MiscAudio::fieldNames() == rowNames(t.get("blocks")->get("MiscAudio")->get("tables")->array[0]));
	CHECK(rowNames(t.get("blocks")->get("AudioLOD")->get("tables")->array[0]) == std::vector<std::string>({ "MaximumAmbientStreams", "AllowDolby", "AllowReverb" }));
	CHECK(rowNames(t.get("blocks")->get("Multisound")->get("tables")->array[0]) == std::vector<std::string>({ "Control", "Subsounds" }));
}

// ---------------------------------------------------------------------------------------------
// Eva.ini, LargeGroupAudio.ini, ScoredKillEvaAnnouncer.ini
// ---------------------------------------------------------------------------------------------
TEST_CASE("audio ini: PredefinedEvaEvent / NewEvaEvent rules (names, defaults, forward references, map.ini)")
{
	AudioFixture f;
	const std::string base = std::string(kDefaults) + "AudioEvent Snd\n  Sounds = a\nEnd\n";
	REQUIRE_MESSAGE(f.load(base +
		"PredefinedEvaEvent DefaultEvaEvent\n  Priority = 5\n  TimeBetweenEventsMS = 20000\n  ExpirationTimeMS = 1500\n  CountAsJumpToLocation = Yes\nEnd\n"
		"PredefinedEvaEvent BeaconDetected\n  Priority = 9\n  MillisecondsToWaitBeforePlaying = 1000\n  SideSound\n    Side = Elves\n    Sound = Snd\n  End\n  SideSound\n    Side = Men\n    Sound = NoSound\n  End\nEnd\n"
		"EvaEventForwardReference Later\nNewEvaEvent First\n  Priority = 7\n  OtherEvaEventsToBlock = Later None BeaconDetected\n  AlwaysPlayFromHomeBase = Yes\nEnd\n"
		"NewEvaEvent Later\n  QuietTimeMS = 400\nEnd\n").empty(), "load");
	const EvaEventStore &e = f.state.eva;
	const int beacon = e.findIndex("BeaconDetected", false);
	REQUIRE(beacon == 1);
	const EvaEventRecord *b = e.record(beacon, false);
	CHECK(b->priority == 9u);
	CHECK(b->timeBetweenEventsMS == 20000u);      // copied from the default event
	CHECK(b->millisecondsToWaitBeforePlayingFrames == 5u); // 1000 ms at 5 frames per second
	REQUIRE(b->sideSounds.size() == 2);
	CHECK(b->sideSounds[0].side == "Elves");
	CHECK(b->sideSounds[0].sound == "Snd");
	CHECK(b->sideSounds[1].sound.empty());
	const int first = e.findIndex("First", false);
	const int later = e.findIndex("Later", false);
	CHECK(later == 22); // the forward reference created the name first
	CHECK(first == 23);
	const EvaEventRecord *fr = e.record(first, false);
	REQUIRE(fr->otherEventsToBlock.size() == 3);
	CHECK(fr->otherEventsToBlock[0] == later);
	CHECK(fr->otherEventsToBlock[1] == -1);
	CHECK(fr->otherEventsToBlock[2] == beacon);
	CHECK(fr->alwaysPlayFromHomeBase);
	CHECK(e.record(later, false)->quietTimeMS == 400u);
	CHECK_FALSE(e.record(later, false)->forwardReference);
	CHECK(e.unresolvedForwardReferences().empty());

	auto expect = [&](const std::string &body, const char *message, INILoadType type = INI_LOAD_OVERWRITE) {
		AudioFixture g;
		std::string err;
		if (type == INI_LOAD_OVERWRITE)
		{
			err = g.load(base + "PredefinedEvaEvent DefaultEvaEvent\nEnd\n" + body, type);
		}
		else
		{
			REQUIRE(g.load(base + "PredefinedEvaEvent DefaultEvaEvent\nEnd\n").empty());
			err = g.load(body, type);
		}
		CHECK_MESSAGE(err.find(message) != std::string::npos, body << " -> " << err);
	};
	expect("PredefinedEvaEvent NotPredefined\nEnd\n", "'NotPredefined' is not a predefined Eva event name");
	expect("NewEvaEvent None\nEnd\n", "Cannot use 'None' as a new Eva event's name");
	expect("NewEvaEvent BeaconDetected\nEnd\n", "'BeaconDetected' is a predefined Eva event name, and cannot be used as a new event name");
	expect("NewEvaEvent X\nEnd\nNewEvaEvent X\nEnd\n", "Cannot redefine existing Eva event 'X' in Eva.ini");
	expect("NewEvaEvent X\n  OtherEvaEventsToBlock = Missing\nEnd\n", "Expected a recognized Eva event name or 'None'; got 'Missing'");
	expect("NewEvaEvent X\n  SideSound\n    Side = A\n    Sound = NoSuchSound\n  End\nEnd\n", "Invalid Sound 'NoSuchSound'");
	expect("PredefinedEvaEvent DefaultEvaEvent\nEnd\n", "You cannot redefine the default Eva event in a map.ini", INI_LOAD_CREATE_OVERRIDES);
	expect("MiscEvaData\nEnd\n", "Cannot override MiscEvaData entries", INI_LOAD_CREATE_OVERRIDES);
	AudioFixture u;
	REQUIRE(u.load(base + "PredefinedEvaEvent DefaultEvaEvent\nEnd\nEvaEventForwardReference Pending\n").empty());
	CHECK(u.state.eva.unresolvedForwardReferences() == std::vector<std::string>({ "Pending" }));
}

TEST_CASE("audio ini: LargeGroupAudioMap parse, merge, map.ini override and restore")
{
	AudioFixture f;
	const std::string base = std::string(kDefaults) + "AudioEvent Loop\n  Sounds = a\nEnd\n";
	REQUIRE_MESSAGE(f.load(base +
		"LargeGroupAudioMap Wade\n  Sound\n    Sound = Loop\n    Key = Hero Orc\n    Key = Elf\n  End\n  Sound Big\n    Sound = Loop\n    Key = Troll\n"
		"    Duck = AudioMap:Other Sound:Quiet Multiplier:50%\n  End\n  RequiredModelConditionFlags = WADING MOVING\n  ExcludedObjectStatusBits = INSIDE_GARRISON\n  Size = 175\n"
		"  StartThreshold = 3\n  StopThreshold = 2\n  HandOffModeDuration = 3000\n  MaximumAudioSpeed = 45\n  IgnoreStealthedUnits = No\nEnd\n"
		"LargeGroupAudioUnusedKnownKeys\n  Key = A B\n  Key = C\nEnd\n").empty(), "load");
	const LargeGroupAudioMap *m = f.state.largeGroupAudio.find("Wade");
	REQUIRE(m != nullptr);
	REQUIRE(m->sounds.size() == 2);
	CHECK(m->sounds[0].keys == std::vector<std::string>({ "Hero", "Orc", "Elf" }));
	CHECK(m->sounds[1].name == "Big");
	REQUIRE(m->sounds[1].ducks.size() == 1);
	CHECK(m->sounds[1].ducks[0].audioMap == "Other");
	CHECK(m->sounds[1].ducks[0].multiplier == doctest::Approx(0.5f));
	CHECK(m->size == 175.0f);
	CHECK(m->startThreshold == 3);
	CHECK(m->stopThreshold == 2);
	CHECK(m->handOffModeDurationFrames == 15u);
	CHECK(m->maximumAudioSpeed == doctest::Approx(9.0f)); // 45 units per second at 5 frames per second
	CHECK_FALSE(m->ignoreStealthedUnits);
	CHECK(BitFlagsTest(m->requiredModelConditionFlags, 0) == false);
	CHECK(f.state.largeGroupAudio.unusedKnownKeys() == std::vector<std::string>({ "A", "B", "C" }));

	// a second block of the same name merges; a duplicate Sound name in one map is an error outside a map.ini
	AudioFixture g;
	CHECK(g.load(base + "LargeGroupAudioMap M\n  Sound S\n    Sound = Loop\n  End\n  Sound S\n    Sound = Loop\n  End\nEnd\n")
			.find("LargeGroupAudio: You cannot use the same name(S) for two Sound blocks within the same LargeGroupAudioMap(M)") != std::string::npos);
	AudioFixture h;
	REQUIRE(h.load(base + "LargeGroupAudioMap M\n  Size = 10\n  StartThreshold = 4\nEnd\nLargeGroupAudioMap M\n  Size = 20\nEnd\n").empty());
	CHECK(h.state.largeGroupAudio.find("M")->size == 20.0f);
	CHECK(h.state.largeGroupAudio.find("M")->startThreshold == 4); // not named by the later block: kept

	// a map.ini edits a copy and restores the original; a map.ini name that did not exist is removed again
	AudioFixture k;
	REQUIRE(k.load(base + "LargeGroupAudioMap M\n  Size = 10\nEnd\n").empty());
	REQUIRE(k.load("LargeGroupAudioMap M\n  Size = 99\nEnd\nLargeGroupAudioMap Extra\n  Size = 5\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(k.state.largeGroupAudio.find("M")->size == 99.0f);
	CHECK(k.state.largeGroupAudio.find("Extra") != nullptr);
	k.state.largeGroupAudio.resetMapOverrides();
	CHECK(k.state.largeGroupAudio.find("M")->size == 10.0f);
	CHECK(k.state.largeGroupAudio.find("Extra") == nullptr);
	AudioFixture d;
	CHECK(d.load(base + "LargeGroupAudioMap M\n  Sound S\n    Duck = Map:Other\n  End\nEnd\n").find("Expected AudioMap:<name> next") != std::string::npos);
}

TEST_CASE("audio ini retail: Eva.ini, LargeGroupAudio.ini and ScoredKillEvaAnnouncer.ini equal the independent scan")
{
	if (!haveRetail())
	{
		return;
	}
	RetailAudio &r = retail();
	for (const std::string &e : r.errors)
	{
		MESSAGE(e);
	}
	REQUIRE(r.errors.empty());
	const JsonValue golden = loadGolden();
	const JsonValue *g = golden.get("evaAndLargeGroupAudio");
	REQUIRE(g != nullptr);
	const EvaEventStore &eva = r.state.eva;
	CHECK(eva.recordCount(false) == (size_t)(EvaEventStore::kPredefinedCount + (int)g->get("distinctNames")->get("NewEvaEvent")->number));
	CHECK(eva.unresolvedForwardReferences().empty());
	size_t sideSounds = 0;
	for (size_t i = 0; i < eva.recordCount(false); ++i)
	{
		sideSounds += eva.record((int)i, false)->sideSounds.size();
	}
	CHECK((int)sideSounds == (int)g->get("nested")->get("NewEvaEvent.SideSound")->number + (int)g->get("nested")->get("PredefinedEvaEvent.SideSound")->number);
	CHECK((int)eva.announcers().size() == (int)g->get("blocks")->get("ScoredKillEvaAnnouncer")->number);
	const LargeGroupAudioStore &lga = r.state.largeGroupAudio;
	CHECK((int)lga.maps().size() == (int)g->get("blocks")->get("LargeGroupAudioMap")->number);
	size_t sounds = 0, keys = 0, ducks = 0;
	for (const LargeGroupAudioMap &m : lga.maps())
	{
		sounds += m.sounds.size();
		for (const LargeGroupAudioSound &s : m.sounds)
		{
			keys += s.keys.size();
			ducks += s.ducks.size();
		}
	}
	CHECK((int)sounds == (int)g->get("nested")->get("LargeGroupAudioMap.Sound")->number);
	CHECK((int)keys == (int)g->get("tokens")->get("LargeGroupAudioMap.Key")->number);
	CHECK((int)ducks == (int)g->get("nested")->get("Duck")->number);
	CHECK((int)lga.unusedKnownKeys().size() == (int)g->get("tokens")->get("LargeGroupAudioUnusedKnownKeys.Key")->number);
	const LargeGroupAudioMap *fire = lga.find("SmallFireLoop");
	REQUIRE(fire != nullptr);
	CHECK(fire->size == 100.0f);
	CHECK(fire->startThreshold == 1);
	CHECK(fire->handOffModeDurationFrames == 15u);
	CHECK(fire->ignoreStealthedUnits);
}
