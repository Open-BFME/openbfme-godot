// OpenBFME unit tests (lane MODCOMPAT-MOD1): AudioLoopUpgrade (data RW 0x8BBD1E / table RW 0xC6F9F8 over RW 0xC76AD8 and RW 0xC76BD8, implementation
// RW 0x8BBE71, removal RW 0x8BBEFE, update RW 0x8BBCB6, onDie RW 0x8BBF5A, xfer RW 0x8BBF93). The synthetic cases need no retail files; the retail case
// plays the loop through a retail audio manager and SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"

#include "LogicTestUtil.h"
#include "RetailTestMount.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AudioLoopUpgrade.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
// the INI's audio lookup for the synthetic worlds: two known events (restored at the end)
struct SoundTable
{
	std::function<bool(const std::string &)> saved = TheContainParseHooks().audioEventExists;
	SoundTable()
	{
		TheContainParseHooks().audioEventExists = [](const std::string &n) { return n == "TestBuildLoop" || n == "TestWallLoop"; };
	}
	~SoundTable() { TheContainParseHooks().audioEventExists = saved; }
};

std::string probe(const std::string &name, const std::string &fields)
{
	return "Object " + name + "\n  KindOf = STRUCTURE\n  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
		"  Behavior = AudioLoopUpgrade ModuleTag_Loop\n" + fields + "  End\nEnd\n";
}

struct LoopWorld
{
	SoundTable sounds;
	logictest::LogicWorld f;
	explicit LoopWorld(const std::string &objects)
	{
		REQUIRE_MESSAGE(f.loadError.empty(), f.loadError);
		LogicModules::registerAll(f.w.modules);
		f.logic->settings().unitDamagedThreshold = 0.5f; // ActiveBody needs GameData's thresholds
		f.logic->settings().unitReallyDamagedThreshold = 0.25f;
		f.logic->settings().bodyThresholdsLoaded = true;
		const std::string err = f.w.load(objects, INI_LOAD_OVERWRITE, "loops.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			f.logic->runLogicFrame();
		}
	}
};

AudioLoopUpgrade *loopOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (AudioLoopUpgrade *x = dynamic_cast<AudioLoopUpgrade *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

const AudioLoopUpgradeModuleData *loopData(const ThingTemplate *t)
{
	for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const AudioLoopUpgradeModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}
} // namespace

// RW 0x8BBD1E / RW 0xC6F9F8: SoundToPlay (RW 0x73B217: NoSound none, an unknown event an INI error), KillAfterMS (ms -> frames), KillOnDeath (default Yes); the
// upgrade table (TriggeredBy) and the DieMux table (DeathTypes) parse too
TEST_CASE("audio loop upgrade: the data is RW 0xC6F9F8 over the upgrade and DieMux tables; an unknown sound is an INI error")
{
	LoopWorld w(probe("LoopDefaults", "") +
		probe("LoopParsed", "    TriggeredBy = Upgrade_Barracks2\n    SoundToPlay = TestBuildLoop\n    KillAfterMS = 6166\n    KillOnDeath = No\n"
							"    DeathTypes = NONE +NORMAL\n"));
	const AudioLoopUpgradeModuleData *d0 = loopData(w.f.w.get("LoopDefaults"));
	REQUIRE(d0);
	CHECK(d0->m_soundToPlay.name.empty());
	CHECK(d0->m_killAfter == 0u);
	CHECK(d0->m_killOnDeath);
	const AudioLoopUpgradeModuleData *d1 = loopData(w.f.w.get("LoopParsed"));
	REQUIRE(d1);
	CHECK(d1->m_soundToPlay.name == "TestBuildLoop");
	CHECK(d1->m_killAfter == 31u); // ceil(6166 ms at 5 logic frames a second)
	CHECK_FALSE(d1->m_killOnDeath);
	CHECK(d1->m_triggeredBy == std::vector<std::string>{ "Upgrade_Barracks2" });
	CHECK_FALSE(d1->m_dieMux.m_deathTypes == 0xFFFFFFFFu); // NONE +NORMAL: not every type
	CHECK_FALSE(w.f.w.load(probe("LoopBad", "    SoundToPlay = NoSuchLoop\n"), INI_LOAD_OVERWRITE, "bad.ini").empty());
	CHECK(w.f.w.load(probe("LoopNone", "    SoundToPlay = NoSound\n"), INI_LOAD_OVERWRITE, "none.ini").empty());
}

// RW 0x8BBDDA / 0x8BBE71 / 0x8BBCB6: the update sleeps forever from creation; the upgrade starts the loop and wakes the update after KillAfterMS, which stops it
TEST_CASE("audio loop upgrade: the upgrade starts the loop; KillAfterMS stops it; the mux and the wake frames are the logic's, the sound is not hashed")
{
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		// the same game with and without a sound: the logic hash must not see the sound
		LoopWorld w(probe("Loop", std::string("    TriggeredBy = Upgrade_Barracks2\n    SoundToPlay = ") + (run == 0 ? "TestBuildLoop" : "NoSound") +
								  "\n    KillAfterMS = 1000\n"));
		Object *o = w.f.make("Loop", w.f.teamOf("Alice"));
		AudioLoopUpgrade *l = loopOf(o);
		REQUIRE(l);
		CHECK_FALSE(l->soundHeld());
		CHECK(l->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
		w.frames(2);
		o->giveUpgrade("Upgrade_Barracks2");
		CHECK(l->isAlreadyUpgraded());
		CHECK(l->soundHeld());
		CHECK(l->soundStarts() == 1);
		const UnsignedInt wake = l->friend_getNextCallFrame();
		CHECK(wake == w.f.logic->getFrame() + 5u); // 1000 ms
		w.frames(4);
		CHECK(l->soundHeld());
		w.frames(2);
		CHECK_FALSE(l->soundHeld()); // RW 0x8BBCB6
		CHECK(l->soundStops() == 1);
		CHECK(l->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
		CHECK(l->isAlreadyUpgraded()); // the upgrade stays executed
		hashes[run] = w.f.logic->computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

// RW 0x8BBEFE: the removal stops the loop and sleeps; RW 0x8BBF5A: KillOnDeath stops it at the death (no DieMux test), KillOnDeath No keeps it
TEST_CASE("audio loop upgrade: the upgrade's removal and the death (KillOnDeath) stop the loop; KillOnDeath No keeps it")
{
	LoopWorld w(probe("LoopKill", "    TriggeredBy = Upgrade_Barracks2\n    SoundToPlay = TestBuildLoop\n    DeathTypes = NONE +BURNED\n") +
		probe("LoopKeep", "    TriggeredBy = Upgrade_Barracks2\n    SoundToPlay = TestWallLoop\n    KillOnDeath = No\n"));
	Object *a = w.f.make("LoopKill", w.f.teamOf("Alice"));
	Object *b = w.f.make("LoopKeep", w.f.teamOf("Alice"));
	AudioLoopUpgrade *la = loopOf(a), *lb = loopOf(b);
	REQUIRE(la);
	REQUIRE(lb);
	CHECK(la->soundKey() != lb->soundKey());
	w.frames(1);
	a->giveUpgrade("Upgrade_Barracks2");
	b->giveUpgrade("Upgrade_Barracks2");
	CHECK(la->soundHeld());
	CHECK(lb->soundHeld());
	CHECK(la->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER); // KillAfterMS 0: no wake
	DieModuleInterface::Event normal; // a NORMAL death: the DieMux (DeathTypes BURNED only) is not consulted
	la->onDie(normal);
	lb->onDie(normal);
	CHECK_FALSE(la->soundHeld());
	CHECK(lb->soundHeld());
	// the removal (RW 0x8D2688 -> slot 8 RW 0x8BBEFE, then not executed: isPermanent is RW 0x9188EB false)
	b->removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_Barracks2"));
	CHECK_FALSE(lb->isAlreadyUpgraded());
	CHECK_FALSE(lb->soundHeld());
	CHECK(lb->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}

// RW 0x8BBF93: version, the handle (here: held or not), UpdateModule, the mux; a truncated snapshot is an error
TEST_CASE("audio loop upgrade: a save / load round trip keeps the mux, the wake frame and the held loop")
{
	LoopWorld w(probe("Loop", "    TriggeredBy = Upgrade_Barracks2\n    SoundToPlay = TestBuildLoop\n    KillAfterMS = 1000\n"));
	Object *o = w.f.make("Loop", w.f.teamOf("Alice"));
	AudioLoopUpgrade *l = loopOf(o);
	REQUIRE(l);
	w.frames(1);
	o->giveUpgrade("Upgrade_Barracks2");
	w.frames(3);
	const UnsignedInt wakeBefore = l->friend_getNextCallFrame();
	XferSave saved;
	l->xfer(saved);
	w.frames(40); // the loop stops
	CHECK_FALSE(l->soundHeld());
	{
		XferLoad load(saved.data());
		l->xfer(load);
	}
	CHECK(l->friend_getNextCallFrame() == wakeBefore); // the saved wake frame (the scheduler's slot index is rebuilt by a real load, not saved)
	CHECK(l->soundHeld()); // the loaded game hears the loop again
	CHECK(l->isAlreadyUpgraded());
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(l->xfer(lt), XferException);
}

// ---- retail data ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct RetailShared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

RetailShared &retailShared()
{
	static RetailShared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

// the retail audio tables and a manager on a simulated device, installed for this thread (test_audio3.cpp's fixture)
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
		ini.loadAll(parser);
		cache = std::make_unique<AudioAssetCache>(&fs, 16u * 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		manager = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::RotWK_GameDat_LCG, 1u);
		AudioApi::install(manager.get());
	}
	~RetailAudio() { AudioApi::install(nullptr); }
};
} // namespace

// RotWK 2.01: the castles' and walls' build loops (e.g. gondorbuildings.ini ModuleTag_GarrisonBuildLoop: TriggeredBy Upgrade_OpenGarrison, SoundToPlay
// BuildingConstructionLoop, KillAfterMS 6166 = 31 frames). A retail template's loop plays through a retail audio manager while the upgrade builds up
TEST_CASE("audio loop upgrade retail: a retail wall's build loop is held by the audio side for KillAfterMS, then removed")
{
	RetailShared &sh = retailShared();
	if (!sh.mount || !sh.mount->fs)
	{
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail AudioLoopUpgrade test)");
		return;
	}
	REQUIRE_MESSAGE(sh.world, sh.error);
	auto contextScope = sh.world->enterContext();
	const ThingTemplate *carrier = nullptr;
	const AudioLoopUpgradeModuleData *data = nullptr;
	size_t users = 0;
	for (const ThingTemplate *t : sh.world->things().templates())
	{
		const AudioLoopUpgradeModuleData *d = loopData(t);
		if (d)
		{
			++users;
			if (!carrier && d->m_soundToPlay.name == "BuildingConstructionLoop" && d->m_killAfter == 31u && d->m_killOnDeath && !d->m_triggeredBy.empty())
			{
				carrier = t;
				data = d;
			}
		}
	}
	CHECK(users == 13); // the module census (rotwk201_template_modules.tsv): 13 templates
	REQUIRE(carrier);
	MESSAGE("carrier: " << carrier->getName() << " (" << data->m_triggeredBy[0] << ")");
	RetailAudio audio(*sh.mount->fs);
	TeamFactory teams;
	PlayerList players(sh.world->nameKeys(), sh.world->playerTemplates(), teams);
	SkirmishSetup setup;
	setup.players.push_back({ "Tester", "FactionMen", true, 0, 0, 0 });
	setup.players.push_back({ "Enemy", "FactionMordor", false, 1, 0, 1 });
	REQUIRE(players.setupSkirmish(setup).empty());
	GameLogic logic(sh.world->things(), sh.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
	logic.random().seedRandom(1);
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic.settings(), &err), err);
	logic.setUpgradeTypes(&sh.world->upgradeTypes());
	Player *tester = players.findPlayerWithName("Tester");
	REQUIRE(tester);
	Object *o = logic.newObject(carrier, tester->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(o);
	AudioLoopUpgrade *l = loopOf(o);
	REQUIRE(l);
	logic.runLogicFrame();
	l->giveSelfUpgrade(); // RW 0x855388: the mux's own grant
	CHECK(l->soundHeld());
	CHECK(AudioApi::heldSound(AudioApi::HELD_UPGRADE_LOOP, l->soundKey()) != 0); // tolerance: any handle (its value is the audio manager's)
	for (int i = 0; i < 32; ++i)
	{
		logic.runLogicFrame();
	}
	CHECK_FALSE(l->soundHeld());
	CHECK(AudioApi::heldSound(AudioApi::HELD_UPGRADE_LOOP, l->soundKey()) == 0);
	logic.reset();
}

