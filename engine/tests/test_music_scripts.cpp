// OpenBFME unit tests (lane AUDIO-2): the weapon fire sounds, the building-complete voice hook and the in-game music (GameClient/MusicScripts: the music map's scripts with ZH's script engine rules) on a
// synthetic script tree, and the retail Music_MusicScripts_Single.map.

#include "doctest.h"

#include "HudTestUtil.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/INI.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/MusicScripts.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/Scripts.h"

namespace
{
std::vector<std::uint8_t> wavOf(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
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

const char kAudio[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 4\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n"
	"  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n"
	"AudioEvent TrackA\n  Sounds = tracka\n  Limit = 0\nEnd\n"
	"AudioEvent TrackB\n  Sounds = trackb\n  Limit = 0\nEnd\n"
	"MusicTrack MusicA\n  Filename = musica.wav\nEnd\n"
	"Multisound MusicPool\n  Control = PLAY_ONE\n  Subsounds = MusicA\nEnd\n";

ScriptParameter sp(const std::string &s, int i = 0, float r = 0.0f)
{
	ScriptParameter p;
	p.stringValue = s;
	p.intValue = i;
	p.realValue = r;
	return p;
}
ScriptCondition cond(int type, std::vector<ScriptParameter> params)
{
	ScriptCondition c;
	c.type = type;
	c.params = std::move(params);
	return c;
}
ScriptActionRec act(int type, std::vector<ScriptParameter> params)
{
	ScriptActionRec a;
	a.type = type;
	a.params = std::move(params);
	return a;
}
ScriptItem script(const std::string &name, bool oneShot, bool subroutine, std::vector<ScriptCondition> conds, std::vector<ScriptActionRec> actions)
{
	ScriptItem it;
	it.script = std::make_unique<Script>();
	it.script->name = name;
	it.script->isActive = true;
	it.script->isOneShot = oneShot;
	it.script->isSubroutine = subroutine;
	it.script->easy = it.script->normal = it.script->hard = true;
	OrCondition orc;
	orc.conditions = std::move(conds);
	it.script->orConditions.push_back(std::move(orc));
	it.script->actions = std::move(actions);
	return it;
}

struct MusicRig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	logictest::LogicWorld f;
	double t = 0.0;
	MusicRig()
	{
		fx.mount({ { "data\\audio\\sounds\\tracka.wav", std::string((const char *)wavOf(1000).data(), wavOf(1000).size()) },
			{ "data\\audio\\sounds\\trackb.wav", std::string((const char *)wavOf(600).data(), wavOf(600).size()) },
			{ "data\\audio\\tracks\\musica.wav", std::string((const char *)wavOf(1000).data(), wavOf(1000).size()) } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", kAudio);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 16u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 7);
		mgr->update(0.0);
	}
	// one logic frame = 200 ms of audio time
	void frame(MusicScripts &m)
	{
		f.logic->runLogicFrame();
		for (int i = 0; i < 20; ++i)
		{
			t += 10.0;
			device->setTime(t);
			mgr->update(t);
		}
		m.update(f.logic->getFrame());
	}
};
} // namespace

TEST_CASE("music scripts: flags, counters, timers, subroutines, one-shot scripts and the play / push / pop music stack (ZH ScriptEngine rules)")
{
	MusicRig r;
	PlayerScriptsList psl;
	psl.lists.resize(1);
	auto &items = psl.lists[0].items;
	// ordinals: the BFME2 template tables (C 1 COUNTER, 2 FLAG, 3 TRUE, 4 TIMER_EXPIRED; A 1 SET_FLAG, 10 CALL_SUBROUTINE, 15 INCREMENT_COUNTER,
	// 20 SET_MILLISECOND_TIMER, 458 POP, 476 PLAY_FINITE_AND_NOTIFY, 478 PUSH_FINITE_AND_NOTIFY)
	items.push_back(script("Init", true, false, { cond(3, {}) },
		{ act(1, { sp("Started"), sp("", 1) }), act(476, { sp("TrackA"), sp("", 1), sp("", 0), sp("", 0), sp("ADone") }), act(20, { sp("T"), sp("", 0, 2.0f) }) }));
	items.push_back(script("PushB", true, false, { cond(2, { sp("ADone"), sp("", 1) }) }, { act(478, { sp("TrackB"), sp("", 1), sp("", 0), sp("", 0), sp("BDone") }) }));
	items.push_back(script("PopB", true, false, { cond(2, { sp("BDone"), sp("", 1) }) }, { act(458, { sp("", 0), sp("", 0) }) }));
	items.push_back(script("Count", false, true, { cond(3, {}) }, { act(15, { sp("", 1), sp("C") }) }));
	items.push_back(script("Caller", true, false, { cond(4, { sp("T") }) }, { act(10, { sp("Count") }) }));
	items.push_back(script("Never", false, false, { cond(1, { sp("C"), sp("", 4), sp("", 5) }) }, { act(1, { sp("Impossible"), sp("", 1) }) }));
	MusicScripts m(*r.mgr, *r.f.logic, r.f.players, nullptr);
	m.loadScripts(psl);
	CHECK(m.scriptCount() == 6);
	r.frame(m);
	CHECK(m.flag("Started"));
	CHECK(m.currentTrack() == "TrackA");
	CHECK(m.stackDepth() == 1);
	CHECK(m.counter("C") == 0);
	// the 2 s timer is 10 frames: the subroutine has run once by frame 12, never twice (the caller is one-shot)
	int pushedAt = -1, poppedAt = -1;
	for (int i = 0; i < 30; ++i)
	{
		r.frame(m);
		if (pushedAt < 0 && m.stackDepth() == 2)
		{
			pushedAt = (int)r.f.logic->getFrame();
			CHECK(m.currentTrack() == "TrackB");
		}
		if (pushedAt >= 0 && poppedAt < 0 && m.stackDepth() == 1)
		{
			poppedAt = (int)r.f.logic->getFrame();
		}
	}
	CHECK(m.counter("C") == 1);
	CHECK(m.flag("ADone"));
	CHECK(m.flag("BDone"));
	CHECK(pushedAt > 0);
	CHECK(poppedAt > pushedAt);
	CHECK(m.currentTrack() == "TrackA"); // the pop restarted the track below
	CHECK_FALSE(m.flag("Impossible"));
	CHECK(m.stats().tracksStarted >= 3);
	CHECK(m.stats().trackFailures == 0);
	CHECK(MusicScripts::acceptanceStops().size() == 2);
	CHECK(MusicScripts::acceptanceStops()[0].rfind("[S-710]", 0) == 0);
	CHECK(MusicScripts::acceptanceStops()[1].rfind("[S-711]", 0) == 0);
}

TEST_CASE("music scripts retail: the music map loads (74 scripts) and a side with no music faction gets the observers' LivingWorldMusic")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("music scripts retail");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	INIEnvironment env;
	env.fileSystem = mount->fs.get();
	AudioIniState ini;
	ini.registerBlocks(env.blocks);
	INI reader(env);
	ini.loadAll(reader);
	CHECK(ini.settings.musicScriptLibraryName.find("Music_MusicScripts_Single.map") != std::string::npos);
	AudioAssetCache cache(mount->fs.get(), 32u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager mgr(ini, cache, device, RandomAlgorithm::ZH_CarryChain, 3);
	logictest::LogicWorld f; // the local player's side is "Alpha": no SKIRMISH_PLAYER_FACTION test matches, the faction counter stays 0 (neutral)
	MusicScripts m(mgr, *f.logic, f.players, nullptr);
	std::string path = ini.settings.musicScriptLibraryName;
	if (path.size() >= 2 && path.front() == '"')
	{
		path = path.substr(1, path.size() - 2);
	}
	std::string error;
	REQUIRE_MESSAGE(m.load(*mount->fs, path, &error), error);
	CHECK(m.scriptCount() == 74);
	double t = 0.0;
	for (int i = 0; i < 20; ++i)
	{
		f.logic->runLogicFrame();
		for (int k = 0; k < 20; ++k)
		{
			t += 10.0;
			device.setTime(t);
			mgr.update(t);
		}
		m.update(f.logic->getFrame());
	}
	CHECK(m.counter("___MusicScript_PlayerFaction") == 0);
	CHECK(m.flag("___MusicScript_InPhaseBaseBuilding"));
	CHECK(m.currentTrack() == "LivingWorldMusic");
	CHECK(m.stats().trackFailures == 0);
	for (const auto &kv : m.stats().unported)
	{
		MESSAGE("music scripts retail: unported " << kv.first << " x" << kv.second);
	}
}

TEST_CASE("weapon fire sounds: one shot or a loop held by FireSoundLoopTime and stopped when it runs out (RW FiringTracker 0x8E3411 / 0x8E30DD)")
{
	MusicRig r;
	AudioManager *saved = AudioApi::current();
	AudioApi::install(r.mgr.get());
	const std::uint64_t posted = AudioApi::weaponFireSoundsPosted();
	AudioApi::postWeaponFireSound("TrackA", 77, 0, 1);
	CHECK(AudioApi::loopingWeaponFireSounds() == 0);
	AudioApi::postWeaponFireSound("TrackB", 78, 10, 1);
	CHECK(AudioApi::loopingWeaponFireSounds() == 1);
	AudioApi::postWeaponFireSound("TrackB", 78, 10, 5); // another shot moves the stop frame to 15
	AudioApi::updateWeaponFireSounds(14);
	CHECK(AudioApi::loopingWeaponFireSounds() == 1);
	AudioApi::updateWeaponFireSounds(15);
	CHECK(AudioApi::loopingWeaponFireSounds() == 0);
	CHECK(AudioApi::weaponFireSoundsPosted() == posted + 3);
	AudioApi::install(saved);
}

TEST_CASE("audio review r1: attachments are owner-specific and follow the manager's lifetime, not the installed manager (LiveGameAudio)")
{
	if (!hudtest::haveWorld("audio attachment lifetime"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	MusicRig a, b;
	AudioManager *saved = AudioApi::current();
	{
		auto first = std::make_unique<LiveGameAudio>(*game.game, *a.mgr);
		CHECK(a.mgr->ownerResolver() != nullptr);
		CHECK(a.mgr->hasWorldQueriesOf(first.get()));
		AudioApi::install(b.mgr.get()); // another manager is installed now; the attachment's manager A still lives
		first.reset();
		CHECK(a.mgr->ownerResolver() == nullptr); // detached from A all the same
		CHECK_FALSE(a.mgr->hasWorldQueriesOf(nullptr));
	}
	{
		// a later attachment replaces the bindings; the earlier one's destruction must not erase them
		auto one = std::make_unique<LiveGameAudio>(*game.game, *a.mgr);
		auto two = std::make_unique<LiveGameAudio>(*game.game, *a.mgr);
		const AudioOwnerResolver *twoResolver = a.mgr->ownerResolver();
		one.reset();
		CHECK(a.mgr->ownerResolver() == twoResolver);
		CHECK(a.mgr->hasWorldQueriesOf(two.get()));
		two.reset();
		CHECK(a.mgr->ownerResolver() == nullptr);
	}
	{
		// the manager dies first: the attachment knows and does not touch it
		auto c = std::make_unique<MusicRig>();
		auto att = std::make_unique<LiveGameAudio>(*game.game, *c->mgr);
		CHECK(att->attachedManagerAlive());
		c.reset();
		CHECK_FALSE(att->attachedManagerAlive());
		att->update();
		att.reset();
	}
	AudioApi::install(saved);
	// the HUD's voice handler registration is owner-specific too
	int got = 0;
	int ownerA = 0, ownerB = 0;
	AudioApi::installUnitVoiceHandler([&](int, std::uint32_t, std::uint32_t) { got = 1; }, &ownerA);
	AudioApi::installUnitVoiceHandler([&](int, std::uint32_t, std::uint32_t) { got = 2; }, &ownerB);
	AudioApi::uninstallUnitVoiceHandler(&ownerA); // not A's any more: stays
	AudioApi::postUnitVoice(0x7DA, 1);
	CHECK(got == 2);
	AudioApi::uninstallUnitVoiceHandler(&ownerB);
	const std::uint64_t without = AudioApi::unitVoicesWithoutHandler();
	AudioApi::postUnitVoice(0x7DA, 1);
	CHECK(AudioApi::unitVoicesWithoutHandler() == without + 1);
}

TEST_CASE("music scripts review r2: a finite action plays a real MusicTrack (through a PLAY_ONE Multisound) once per request, repeats it and notifies")
{
	MusicRig r;
	PlayerScriptsList psl;
	psl.lists.resize(1);
	psl.lists[0].items.push_back(script("Play", true, false, { cond(3, {}) }, { act(476, { sp("MusicPool"), sp("", 2), sp("", 0), sp("", 0), sp("Done") }) }));
	MusicScripts m(*r.mgr, *r.f.logic, r.f.players, nullptr);
	m.loadScripts(psl);
	r.frame(m);
	CHECK(r.mgr->isMusicPlaying());
	int doneAt = -1;
	for (int i = 0; i < 40 && doneAt < 0; ++i) // a 1 s track twice = about 10 frames of 200 ms
	{
		r.frame(m);
		if (m.flag("Done"))
		{
			doneAt = (int)r.f.logic->getFrame();
		}
	}
	CHECK(doneAt > 0);
	CHECK(m.stats().tracksCompleted == 2);
	CHECK(m.stats().tracksStarted == 2);
	CHECK(r.mgr->musicTrackCompletions("MusicA") == 2);
	for (int i = 0; i < 10; ++i)
	{
		r.frame(m);
	}
	CHECK_FALSE(r.mgr->isMusicPlaying()); // no backend loop, no playlist chain after the two plays
	// SET_TRACK keeps the continuous (looping) mode
	MusicRig r2;
	PlayerScriptsList loopList;
	loopList.lists.resize(1);
	loopList.lists[0].items.push_back(script("Loop", true, false, { cond(3, {}) }, { act(456, { sp("MusicPool"), sp("", 0), sp("", 0) }) }));
	MusicScripts m2(*r2.mgr, *r2.f.logic, r2.f.players, nullptr);
	m2.loadScripts(loopList);
	for (int i = 0; i < 20; ++i)
	{
		r2.frame(m2);
	}
	CHECK(r2.mgr->isMusicPlaying());
	CHECK(m2.stats().tracksCompleted == 0);
}

namespace
{
const char kGroupAudio[] =
	"AudioEvent GroupLoop\n  Sounds = tracka\n  Control = LOOP\n  Type = world everyone\n  MinRange = 10\n  MaxRange = 1000\n  Limit = 0\nEnd\n"
	"LargeGroupAudioMap TestSoldiers\n  Sound TestLoop\n    Sound = GroupLoop\n    Key = Gondor_Soldier\n  End\n  Size = 4000\n  StartThreshold = 1\n  StopThreshold = 1\n"
	"  HandOffModeDuration = 3000\n  MaximumAudioSpeed = 20\nEnd\n"
	"LargeGroupAudioMap SeesStealth\n  Sound SeeLoop\n    Sound = GroupLoop\n    Key = Gondor_Soldier\n  End\n  Size = 4000\n  StartThreshold = 1\n  StopThreshold = 1\n"
	"  MaximumAudioSpeed = 20\n  IgnoreStealthedUnits = No\nEnd\n";

struct GroupRig : MusicRig
{
	GroupRig()
	{
		const std::string err = initest::loadError(fx.env, "groups.ini", kGroupAudio);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	float voiceVolume(const std::string &event) const
	{
		float v = -1.0f;
		for (const auto &kv : device->voices())
		{
			if (!kv.second.stopped && kv.second.start.eventName == event)
			{
				v = kv.second.params.volume;
			}
		}
		return v;
	}
	void tick(double ms)
	{
		for (double end = t + ms; t < end;)
		{
			t += 10.0;
			device->setTime(t);
			mgr->update(t);
		}
	}
};
} // namespace

TEST_CASE("large group audio review r2 / AUDIO-4: the members come from the LargeGroupAudioUpdate modules' calls; a culled loop starts once the listener comes near, the sound follows the group at MaximumAudioSpeed, undetected stealth does not count")
{
	if (!hudtest::haveWorld("large group audio runtime"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	GroupRig a;
	LiveGameAudio att(*game.game, *a.mgr);
	// lane AUDIO-4: a module calls TheLargeGroupAudio at its join (onObjectCreated) and at its wakes (every 4 .. 5 frames: 3 + GameLogicRandomValue(0, 1) + 1);
	// the client applies the calls at its next pass
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			game.logic().runLogicFrame();
		}
	};
	const Coord3D p{ 1200.0f, 1200.0f, game.game->logic().terrain() ? 0.0f : 0.0f };
	std::string error;
	Object *soldier = game.game->createObject("GondorFighter", game.index(), p, 0.0f, &error);
	REQUIRE_MESSAGE(soldier != nullptr, error);
	// the listener far away: the loop is culled by distance, the cell stays qualified without a sound. The join (onObjectCreated) is made before createObject
	// places the object (retail: ThingFactory::newObject's modules are told before the caller positions it), so the maps learn the position at its first wake
	a.mgr->setListenerPosition(Coord3D{ 90000.0f, 90000.0f, 0.0f }, Coord3D{ 0.0f, 1.0f, 0.0f });
	frames(6);
	att.runLargeGroupAudio();
	a.tick(100);
	CHECK(att.stats().largeGroupEvents >= 1);
	CHECK(att.stats().largeGroupMembers >= 1);
	att.runLargeGroupAudio();
	a.tick(100);
	// lane AUDIO-3: RotWK's SoundManager keeps a culled loop queued and checks it again every step (RW 0x45CC91 / 0x461A83): no voice sounds yet
	CHECK(a.voiceVolume("GroupLoop") < 0.0f);
	CHECK(att.stats().largeGroupQualified >= 1);
	// the listener arrives: the qualifying loop is retried and plays
	a.mgr->setListenerPosition(p, Coord3D{ 0.0f, 1.0f, 0.0f });
	att.runLargeGroupAudio();
	a.tick(200);
	CHECK(att.stats().largeGroupPlaying >= 1);
	const float v0 = a.voiceVolume("GroupLoop");
	CHECK(v0 > 0.0f);
	// the only member walks 400 away inside the same cell; its module tells the maps at its next wake, then the sound moves toward it at 20 * 0.2 = 4 units
	// per pass and gets quieter
	Coord3D far = p;
	far.x += 400.0f;
	soldier->setPosition(&far);
	frames(6);
	Coord3D before{};
	for (const auto &kv : att.largeGroups())
	{
		if (kv.first.rfind("TestSoldiers/", 0) == 0)
		{
			before = kv.second.first;
		}
	}
	for (int i = 0; i < 25; ++i)
	{
		att.runLargeGroupAudio();
		a.tick(50);
	}
	Coord3D after{};
	for (const auto &kv : att.largeGroups())
	{
		if (kv.first.rfind("TestSoldiers/", 0) == 0)
		{
			after = kv.second.first;
		}
	}
	CHECK(after.x - before.x == doctest::Approx(25.0f * 4.0f).epsilon(0.01));
	const float v1 = a.voiceVolume("GroupLoop");
	CHECK(v1 > 0.0f);
	CHECK(v1 < v0);
	// IgnoreStealthedUnits: a stealthed and undetected soldier (RW 0x694C0D, no viewer) leaves the default map's cell at its next wake, the map with
	// IgnoreStealthedUnits = No keeps counting it
	const int stealthed = ObjectTemplateInfoBuilder::objectStatusIndex("STEALTHED");
	const int detected = ObjectTemplateInfoBuilder::objectStatusIndex("DETECTED");
	REQUIRE(stealthed >= 0);
	REQUIRE(detected >= 0);
	soldier->setStatus((unsigned)stealthed, true);
	frames(6);
	att.runLargeGroupAudio();
	bool defaultMap = false, seesStealth = false;
	for (const auto &kv : att.largeGroups())
	{
		defaultMap |= kv.first.rfind("TestSoldiers/", 0) == 0;
		seesStealth |= kv.first.rfind("SeesStealth/", 0) == 0;
	}
	CHECK_FALSE(defaultMap);
	CHECK(seesStealth);
	soldier->setStatus((unsigned)detected, true); // detected: counted again
	frames(6);
	att.runLargeGroupAudio();
	defaultMap = false;
	for (const auto &kv : att.largeGroups())
	{
		defaultMap |= kv.first.rfind("TestSoldiers/", 0) == 0;
	}
	CHECK(defaultMap);
}
