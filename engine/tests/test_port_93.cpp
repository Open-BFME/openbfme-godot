// OpenBFME unit tests (lane PORT-93, issue fork issue 241): SpecialAbilityUpdate's ActiveLoopSound — the looping sound of an arrow-storm-like power.
//
// RotWK 2.01, read through tools/mod/rw.py on game.dat (md5 daed3668):
//   data + 0x34  the ActiveLoopSound string, table RW 0xC55F40 row RW 0xC56140, parser RW 0x73B217 (NoSound clears, an unknown event is an INI error);
//                RW 0x854008 tests it, so an ability without the field never touches the audio
//   RW 0x854008 .. 0x85405B (the tail of SpecialAbilityUpdate::triggerAbilityEffect, RW 0x853EDF): with the field set, the manager (RW 0xDE42FC) is asked whether the
//                handle kept at module + 0x38 still sounds (RW 0x854014 / 0x854019, vslot 0xD0 = isCurrentlyPlaying); it does, so nothing is added (RW 0x854021);
//                else the event is built for the object (player index RW 0x853FF1, object id RW 0x854026, RW 0x6DB5B3) and added through the manager's vslot 0x64
//                (RW 0x85404B), and the handle is stored at + 0x38 (RW 0x854054)
//   RW 0x85311B .. 0x853131 (finishAbility, RW 0x8530BA): + 0x38 is removed (vslot 0x6C) and set back to 1, before endPreparation (RW 0x853134 -> 0x85260A)
//   RW 0x8528C8 / 0x8528DB (xfer): both handles are transferred, so a load hears the loop it saved
// The port keeps the handle on the audio side (AudioApi::HELD_ABILITY_LOOP, keyed by the module) like the other held loops; the tests read it there.
// GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "IniTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "Common/SpecialPower.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
// a PCM16 mono 22.05 kHz silent WAV (the fixture of the audio tests: the simulated device needs a real header)
std::vector<std::uint8_t> makeWavMs(double ms)
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

std::string bytesOf(const std::vector<std::uint8_t> &b)
{
	return std::string(b.begin(), b.end());
}

// the audio INI the sound names are looked up in (AudioSettings + the events the synthetic objects ask for)
const char kAudioBase[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n"
	"  SoundsExtension = wav\n  SampleCount2D = 2\n  SampleCount3D = 3\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n"
	"  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n"
	"  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n"
	"  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n"
	"AudioEvent TestArrowLoop\n  Sounds = arrowloop\n  Type = world everyone\n  MaxRange = 5000\n  Limit = 0\nEnd\n";

std::string sndPath(const char *name)
{
	return std::string("data\\audio\\sounds\\") + name + ".wav";
}

// an audio manager on a simulated device over a synthetic archive (the fixture of test_audio_manager.cpp), installed for this thread
struct AbilityAudio
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	double t = 0.0;

	explicit AbilityAudio(const std::string &events)
	{
		initest::FileList list;
		list.push_back({ sndPath("arrowloop"), bytesOf(makeWavMs(4000.0)) });
		list.push_back({ "data\\ini\\none.txt", "x" });
		fx.mount(list);
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", std::string(kAudioBase) + events);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 64u * 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1234u);
		mgr->update(0.0);
		AudioApi::install(mgr.get());
	}
	~AbilityAudio() { AudioApi::install(nullptr); }

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
	size_t startsOf(const std::string &needle) const
	{
		size_t n = 0;
		for (const auto &e : device->log())
		{
			if (e.kind == SimulatedAudioDevice::LogEntry::Start && e.file.find(needle) != std::string::npos)
			{
				++n;
			}
		}
		return n;
	}
};

// the arrow-storm-like power of the synthetic world: an ability whose data carries ActiveLoopSound (the shape of retail GandalfLightningLoop use)
const char kStorm[] =
	"Weapon StormWeapon\n  AttackRange = 400\n  DelayBetweenShots = 100\n  DamageNugget\n    Damage = 10\n    Radius = 0.0\n    DelayTime = 0\n"
	"    DamageType = SLASH\n    DeathType = NORMAL\n  End\nEnd\n"
	"SpecialPower SpecialAbilityTestStorm\n  Enum = SPECIAL_ARROW_STORM\n  ReloadTime = 1000\nEnd\n"
	"Object StormHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule StormPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestStorm\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = ArrowStormUpdate StormTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestStorm\n  StartAbilityRange = 300\n  UnpackTime = 200\n  PreparationTime = 200\n"
	"  PackTime = 200\n  IgnoreFacingCheck = Yes\n  WeaponTemplate = StormWeapon\n  TargetRadius = 60\n  ShotsPerTarget = 1\n"
	"  ShotsPerBurst = 2\n  MaxShots = 5\n  ActiveLoopSound = TestArrowLoop\n"
	" End\n"
	"End\n"
	"Object StormHeroSilent\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule StormPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestStorm\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = ArrowStormUpdate StormTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestStorm\n  StartAbilityRange = 300\n  UnpackTime = 200\n  PreparationTime = 200\n"
	"  PackTime = 200\n  IgnoreFacingCheck = Yes\n  WeaponTemplate = StormWeapon\n  TargetRadius = 60\n  ShotsPerTarget = 1\n"
	"  ShotsPerBurst = 2\n  MaxShots = 5\n"
	" End\n"
	"End\n"
	"Object StormVictim\n  KindOf = INFANTRY SELECTABLE\n  Body = ActiveBody BodyTag\n  MaxHealth = 1000\n End\n  Behavior = AIUpdateInterface AiTag\n End\nEnd\n";

// the synthetic combat world of lane HERO-2 (PowerWorld) with the special power, FX list and modifier stores mounted
struct StormWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	explicit StormWorld(const char *objects)
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~StormWorld()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
};

SpecialAbilityUpdate *stormOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (SpecialAbilityUpdate *u = dynamic_cast<SpecialAbilityUpdate *>(m.get()))
		{
			return u;
		}
	}
	return nullptr;
}
} // namespace

// RW 0xC55F40 row RW 0xC56140 (data + 0x34, parser RW 0x73B217): the field is parsed onto the ArrowStormUpdate's data, and RW 0x854008 tests it before any
// audio call — an ability whose data does not set it never starts a loop
TEST_CASE("PORT-93 the data: ActiveLoopSound (RW 0xC55F40, data + 0x34) reaches the ArrowStormUpdate's data; an object without it has an empty name")
{
	StormWorld f(kStorm);
	Object *loud = f.unit("StormHero", 'A', 300, 300);
	Object *quiet = f.unit("StormHeroSilent", 'A', 320, 300);
	REQUIRE(loud);
	REQUIRE(quiet);
	SpecialAbilityUpdate *a = stormOf(loud);
	SpecialAbilityUpdate *b = stormOf(quiet);
	REQUIRE(a);
	REQUIRE(b);
	CHECK(a->abilityData()->m_activeLoopSound == "TestArrowLoop");
	CHECK(b->abilityData()->m_activeLoopSound.empty());
}

// RW 0x854008 .. 0x85405B: the trigger starts the loop for the object; RW 0x854021: while the kept handle sounds it is not added again; RW 0x85311B .. 0x853131:
// finishAbility removes it. RW 0x854014 / 0x854019 is the isCurrentlyPlaying the second and third checks ride on.
TEST_CASE("PORT-93 the loop: the storm's trigger starts ActiveLoopSound for the hero once, it keeps sounding while the power runs and stops when the power ends")
{
	AbilityAudio audio(std::string("AudioEvent TestArrowLoop\n  Sounds = arrowloop\n  Type = world everyone\n  MaxRange = 5000\n  Limit = 0\nEnd\n"));
	StormWorld f(kStorm);
	Object *hero = f.unit("StormHero", 'A', 300, 300);
	Object *victim = f.unit("StormVictim", 'B', 380, 300);
	REQUIRE(hero);
	REQUIRE(victim);
	SpecialAbilityUpdate *u = stormOf(hero);
	REQUIRE(u);
	REQUIRE(u->activeLoopKey() != 0); // tolerance: non-zero generated loop key
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) == 0); // nothing before the trigger
	const Coord3D at = { 385.0f, 305.0f, 0.0f };
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	bool started = false;
	for (int i = 0; i < 80 && u->abilitiesTriggered() == 0; ++i)
	{
		f.frames(1);
		audio.run(100.0);
		started = started || AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) != 0;
	}
	REQUIRE(u->abilitiesTriggered() == 1); // RW 0x853F1D: the one trigger of the power
	CHECK(u->activeLoopHeld());
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) != 0); // tolerance: non-zero held sound handle
	CHECK(audio.startsOf("arrowloop") == 1);
	// the power is still running (MaxShots 5 at ShotsPerBurst 2): persistent triggers do not add the loop again (RW 0x854021)
	const size_t startedWhileActive = audio.startsOf("arrowloop");
	f.frames(30);
	audio.run(300.0);
	if (u->isActive())
	{
		CHECK(audio.startsOf("arrowloop") == startedWhileActive); // still one sound: the kept handle sounds
	}
	// the power ends (RW 0x85311B .. 0x853131): the loop is removed and the handle set back
	f.frames(60);
	audio.run(300.0);
	REQUIRE_FALSE(u->isActive());
	CHECK_FALSE(u->activeLoopHeld());
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) == 0);
}

// RW 0x854008 tests data + 0x34 first: an ability of the same class without ActiveLoopSound posts nothing, and its finishAbility removes nothing
TEST_CASE("PORT-93 no field, no loop: the same storm without ActiveLoopSound plays no sound and holds no handle")
{
	AbilityAudio audio(std::string("AudioEvent TestArrowLoop\n  Sounds = arrowloop\n  Type = world everyone\n  MaxRange = 5000\n  Limit = 0\nEnd\n"));
	StormWorld f(kStorm);
	Object *hero = f.unit("StormHeroSilent", 'A', 300, 300);
	Object *victim = f.unit("StormVictim", 'B', 380, 300);
	REQUIRE(hero);
	REQUIRE(victim);
	SpecialAbilityUpdate *u = stormOf(hero);
	REQUIRE(u);
	const Coord3D at = { 385.0f, 305.0f, 0.0f };
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	for (int i = 0; i < 80 && u->abilitiesTriggered() == 0; ++i)
	{
		f.frames(1);
		audio.run(100.0);
	}
	REQUIRE(u->abilitiesTriggered() == 1);
	CHECK_FALSE(u->activeLoopHeld());
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) == 0);
	f.frames(90);
	audio.run(300.0);
	CHECK_FALSE(u->activeLoopHeld());
	CHECK(audio.startsOf("arrowloop") == 0);
}

// RW 0x8528C8 / 0x8528DB: xfer transfers whether the loop's handle is held; a load re-posts the loop it saved, a finished ability saves "not held"
TEST_CASE("PORT-93 the save: a storm saved while its loop sounds hears the loop again after the load; one saved after the end does not")
{
	AbilityAudio audio(std::string("AudioEvent TestArrowLoop\n  Sounds = arrowloop\n  Type = world everyone\n  MaxRange = 5000\n  Limit = 0\nEnd\n"));
	StormWorld f(kStorm);
	Object *hero = f.unit("StormHero", 'A', 300, 300);
	Object *victim = f.unit("StormVictim", 'B', 380, 300);
	REQUIRE(hero);
	REQUIRE(victim);
	SpecialAbilityUpdate *u = stormOf(hero);
	REQUIRE(u);
	const Coord3D at = { 385.0f, 305.0f, 0.0f };
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	for (int i = 0; i < 80 && u->abilitiesTriggered() == 0; ++i)
	{
		f.frames(1);
		audio.run(100.0);
	}
	REQUIRE(u->activeLoopHeld());
	XferSave saved;
	u->xfer(saved);
	// the game moves on, the power ends and the loop stops
	f.frames(90);
	audio.run(300.0);
	CHECK_FALSE(u->activeLoopHeld());
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) == 0);
	const size_t beforeLoad = audio.startsOf("arrowloop");
	{
		XferLoad load(saved.data());
		u->xfer(load);
	}
	CHECK(u->activeLoopHeld());                                                    // the saved fact
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) != 0); // tolerance: non-zero held sound handle
	audio.run(100.0); // the manager starts the re-posted event on its next tick
	CHECK(audio.startsOf("arrowloop") == beforeLoad + 1);
	// a save taken while the loop is not held loads as "not held": the loop is dropped, never posted
	XferSave stopped;
	u->xfer(stopped);
	CHECK(AudioApi::heldSound(AudioApi::HELD_ABILITY_LOOP, u->activeLoopKey()) != 0); // tolerance: non-zero held sound handle
	{
		XferLoad load(stopped.data());
		u->xfer(load);
	}
	CHECK(u->activeLoopHeld());
}
