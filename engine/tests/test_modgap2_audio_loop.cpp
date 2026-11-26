// ModelConditionAudioLoopClientBehavior (MODGAP-2, RW 0x8D07CF / 0x8D02D9 / 0x8D0425 / 0x8D03F8, S-6024). GPL-3.0.
// The expected values come from the addresses named in each case and from retail's data\ini\object\goodfaction\units\men\gondorfighter.ini.
#include "doctest.h"

#include "HudTestUtil.h"
#include "LogicTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/INI.h"
#include "Common/ModelState.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/ClientBehaviorModules.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/ModelConditionAudioLoopClientBehavior.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <string>

namespace
{
const ModelConditionAudioLoopClientBehaviorModuleData *loopData(const ThingTemplate &tt)
{
	for (const auto &n : tt.clientBehaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const ModelConditionAudioLoopClientBehaviorModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

std::string loadLoop(logictest::LogicWorld &f, const std::string &name, const std::string &line)
{
	return f.w.load("Object " + name + "\n  ClientBehavior = ModelConditionAudioLoopClientBehavior ModuleTag_Loop\n    " + line + "\n  End\nEnd\n", INI_LOAD_OVERWRITE, name + ".ini");
}

ModelConditionFlags flagsOf(std::initializer_list<const char *> names)
{
	ModelConditionFlags out;
	for (const char *name : names)
	{
		const int bit = ModelCondition::indexOf(name);
		REQUIRE_MESSAGE(bit != -1, name); // ModelCondition::indexOf: -1 for an unknown name
		out.set(bit);
	}
	return out;
}

ModelConditionAudioLoopClientBehavior *loopOf(hudtest::Rig &game, const Object *o)
{
	Drawable *d = game.game->drawables().findByObject(o->getID());
	if (d)
	{
		for (const auto &m : d->clientModules())
		{
			if (auto *loop = dynamic_cast<ModelConditionAudioLoopClientBehavior *>(m.get()))
			{
				return loop;
			}
		}
	}
	return nullptr;
}

// the retail audio of the shared world, as test_audio4.cpp's footsteps have it
struct RetailAudio
{
	INIEnvironment env;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	double t = 0.0;
	explicit RetailAudio(retailtest::Mount &mount)
	{
		env.fileSystem = mount.fs.get();
		ini.registerBlocks(env.blocks);
		INI reader(env);
		ini.loadAll(reader);
		cache = std::make_unique<AudioAssetCache>(mount.fs.get(), 32u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 3);
		mgr->enableEventLog(true);
	}
	void frames(hudtest::Rig &game, int n)
	{
		for (int i = 0; i < n; ++i)
		{
			game.logic().runLogicFrame();
			for (int c = 0; c < 6; ++c)
			{
				game.game->refreshClient(1000.0 / 30.0, (double)c / 6.0);
				t += 1000.0 / 30.0;
				device->setTime(t);
				mgr->update(t);
			}
		}
	}
	bool logged(const std::string &event)
	{
		bool found = false;
		for (const AudioLogEntry &e : mgr->takeEventLog())
		{
			found = found || e.event == event;
		}
		return found;
	}
};

const char *const kFighterSound = "EmotionMenGoodCompilationBFME1and2VoxTauntSinglesLoop"; // gondorfighter.ini: Required:RAISING_FLAG Excluded:DYING ENGAGED
} // namespace

TEST_CASE("modgap2 audio loop: the ModelCondition line (RW 0x8D07CF): keywords in any order and case, a name that is no flag ends its list, the retail errors")
{
	logictest::LogicWorld f;
	ClientBehaviorModules::registerAll(f.w.modules);
	{
		const std::function<bool(const std::string &)> saved = TheContainParseHooks().audioEventExists;
		TheContainParseHooks().audioEventExists = nullptr;
		CHECK(loadLoop(f, "NoTable", "ModelCondition = Sound:LoopA").find("event table is not installed") != std::string::npos);
		TheContainParseHooks().audioEventExists = saved;
	}
	AnimationSoundClientBehaviorModuleData::soundExists() = [](const std::string &n) { return n == "LoopA" || n == "LoopB"; };
	REQUIRE(f.w.load("Object Parsed\n  ClientBehavior = ModelConditionAudioLoopClientBehavior ModuleTag_Loop\n"
					 "    ModelCondition = Required:RAISING_FLAG\t\tExcluded:DYING ENGAGED\tSound:LoopA\n"
					 "    ModelCondition = sound: LoopB EXCLUDED : MOVING REQUIRED:RUBBLE MOVING\n"
					 "    ModelCondition = Required:DYING\n"
					 "    ModelCondition =\n"
					 "  End\nEnd\n",
				INI_LOAD_OVERWRITE, "Parsed.ini")
				.empty());
	const ModelConditionAudioLoopClientBehaviorModuleData *d = loopData(*f.w.get("Parsed"));
	REQUIRE(d);
	REQUIRE(d->m_entries.size() == 4);
	CHECK(d->m_entries[0].sound == "LoopA");
	CHECK(d->m_entries[0].required == flagsOf({ "RAISING_FLAG" }));
	CHECK(d->m_entries[0].excluded == flagsOf({ "DYING", "ENGAGED" }));
	CHECK(d->m_entries[1].sound == "LoopB");
	CHECK(d->m_entries[1].required == flagsOf({ "RUBBLE", "MOVING" }));
	CHECK(d->m_entries[1].excluded == flagsOf({ "MOVING" }));
	CHECK(d->m_entries[2].sound.empty()); // no Sound keyword: an entry without a sound
	CHECK(d->m_entries[3].sound.empty()); // an empty line: an entry that holds for any flags (RW 0x8D0825)

	auto err = [&](const char *name, const std::string &line) { return loadLoop(f, name, line); };
	CHECK(err("E1", "ModelCondition = Sound:Nope").find("Unknown sound Nope") != std::string::npos); // RW 0x8D099D
	CHECK(err("E2", "ModelCondition = Required:NOT_A_FLAG").find("Expected one of Required, Excluded, or Sound next") != std::string::npos); // RW 0x8D09B3
	CHECK(err("E3", "ModelCondition = Sound:NoSound").find("Expected one of Required, Excluded, or Sound next") != std::string::npos);      // RW 0x8D08FB -> 0x8D0964
	CHECK_FALSE(err("E4", "ModelCondition = Sound").empty());                                                                               // RW 0x42DC9F: the value must exist
	AnimationSoundClientBehaviorModuleData::soundExists() = nullptr;
}

TEST_CASE("modgap2 audio loop: the selection (RW 0x8D02D9): the first entry with every required flag and no excluded flag gives its sound, even none")
{
	ModelConditionAudioLoopClientBehaviorModuleData data;
	ModelConditionAudioLoopEntry a, b, none;
	a.sound = "A";
	a.required = flagsOf({ "RAISING_FLAG" });
	a.excluded = flagsOf({ "DYING", "ENGAGED" });
	none.required = flagsOf({ "RUBBLE" }); // holds before b: it answers with no sound
	b.sound = "B";
	b.required = flagsOf({ "RUBBLE", "MOVING" });
	data.m_entries = { a, none, b };
	CHECK(data.selectSound(ModelConditionFlags()).empty());
	CHECK(data.selectSound(flagsOf({ "RAISING_FLAG" })) == "A");
	CHECK(data.selectSound(flagsOf({ "RAISING_FLAG", "MOVING" })) == "A");
	CHECK(data.selectSound(flagsOf({ "RAISING_FLAG", "DYING" })).empty());
	CHECK(data.selectSound(flagsOf({ "RAISING_FLAG", "ENGAGED" })).empty());
	CHECK(data.selectSound(flagsOf({ "RUBBLE", "MOVING" })).empty());
}

TEST_CASE("modgap2 audio loop retail: a Gondor soldier's flag raising starts its taunt loop for the drawable, the same selection does not restart it, ENGAGED stops it (RW 0x8D0425 / 0x8D03F8)")
{
	if (!hudtest::haveWorld("modgap2 audio loop retail"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	RetailAudio audio(*hudtest::shared().mount);
	LiveGameAudio att(*game.game, *audio.mgr);
	std::string error;
	Object *soldier = game.game->createObject("GondorFighter", game.index(), Coord3D{ 1200.0f, 1200.0f, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(soldier != nullptr, error);
	audio.mgr->setListenerPosition(*soldier->getPosition(), Coord3D{ 0.0f, 1.0f, 0.0f });
	audio.frames(game, 2);
	ModelConditionAudioLoopClientBehavior *loop = loopOf(game, soldier);
	REQUIRE(loop != nullptr);
	CHECK(loop->handle() == AHSV_NoSound); // RW 0x8D036F: the ctor does not select
	CHECK(loop->heldSound().empty());
	CHECK_FALSE(audio.logged(kFighterSound));

	const int raising = ModelCondition::indexOf("RAISING_FLAG"), engaged = ModelCondition::indexOf("ENGAGED");
	soldier->setModelConditionState(raising, true);
	audio.frames(game, 2);
	CHECK(loop->heldSound() == kFighterSound);
	REQUIRE(loop->handle() >= AHSV_FirstHandle); // tolerance: the audio manager numbers its handles; any real one is at least the first
	const AudioHandle first = loop->handle();
	CHECK(audio.mgr->isCurrentlyPlaying(first));
	CHECK(audio.logged(kFighterSound));

	soldier->setModelConditionState(ModelCondition::indexOf("MOVING"), true); // RW 0x8D0454: the same sound: not restarted
	audio.frames(game, 2);
	CHECK(loop->handle() == first);
	CHECK_FALSE(audio.logged(kFighterSound));

	soldier->setModelConditionState(engaged, true);
	audio.frames(game, 2);
	CHECK(loop->handle() == AHSV_NoSound); // RW 0x8D0413
	CHECK(loop->heldSound().empty());
	// the loop ends: a removed event stops looping and plays its Decay (AudioManager::stopAudioEvent); one still looping would never end
	for (int i = 0; i < 100 && audio.mgr->isCurrentlyPlaying(first); ++i)
	{
		audio.frames(game, 1);
	}
	CHECK_FALSE(audio.mgr->isCurrentlyPlaying(first));
	CHECK(loop->handle() == AHSV_NoSound); // nothing new started meanwhile

	soldier->setModelConditionState(engaged, false); // selected again: a new request
	audio.frames(game, 2);
	REQUIRE(loop->handle() >= AHSV_FirstHandle); // tolerance: the audio manager numbers its handles; any real one is at least the first
	const AudioHandle second = loop->handle();
	CHECK(second != first);
	CHECK(audio.logged(kFighterSound));

	// inaudible: the clear; audible: selected again (RW 0x8D04EB / 0x8D04D9)
	loop->becameInaudible();
	CHECK(loop->handle() == AHSV_NoSound);
	CHECK_FALSE(audio.mgr->isCurrentlyPlaying(second));
	loop->becameAudible();
	CHECK(loop->handle() >= AHSV_FirstHandle); // tolerance: the audio manager numbers its handles; any real one is at least the first

	// the object's destruction removes the sound with the module (RW 0x8D055C)
	const AudioHandle last = loop->handle();
	const size_t before = game.game->drawables().animationSounds().audioLoopCount();
	game.logic().destroyObject(soldier);
	audio.frames(game, 2);
	CHECK(game.game->drawables().animationSounds().audioLoopCount() == before - 1);
	CHECK_FALSE(audio.mgr->isCurrentlyPlaying(last));
}

TEST_CASE("modgap2 audio loop retail (S-6024): flags set before the audio attached start the loop when it arrives; a detach forgets the handle and the next attachment starts it again")
{
	if (!hudtest::haveWorld("modgap2 audio loop S-6024"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	std::string error;
	Object *soldier = game.game->createObject("GondorFighter", game.index(), Coord3D{ 1200.0f, 1200.0f, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(soldier != nullptr, error);
	soldier->setModelConditionState(ModelCondition::indexOf("RAISING_FLAG"), true);
	game.logic().runLogicFrame();
	game.game->refreshClient(1000.0 / 30.0, 0.0);
	ModelConditionAudioLoopClientBehavior *loop = loopOf(game, soldier);
	REQUIRE(loop != nullptr);
	CHECK(loop->heldSound() == kFighterSound); // no TheAudio: selected, nothing plays
	CHECK(loop->handle() == AHSV_NoSound);

	RetailAudio audio(*hudtest::shared().mount);
	audio.mgr->setListenerPosition(*soldier->getPosition(), Coord3D{ 0.0f, 1.0f, 0.0f });
	{
		LiveGameAudio att(*game.game, *audio.mgr);
		audio.frames(game, 2);
		CHECK(loop->heldSound() == kFighterSound);
		CHECK(loop->handle() >= AHSV_FirstHandle); // tolerance: the audio manager numbers its handles; any real one is at least the first
		CHECK(audio.logged(kFighterSound));
	}
	CHECK(loop->handle() == AHSV_NoSound);
	CHECK(loop->heldSound().empty());
	{
		LiveGameAudio att(*game.game, *audio.mgr); // the same manager again
		audio.frames(game, 2);
		CHECK(loop->handle() >= AHSV_FirstHandle); // tolerance: the audio manager numbers its handles; any real one is at least the first
		CHECK(audio.logged(kFighterSound));
	}
}
