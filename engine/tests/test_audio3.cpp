// OpenBFME unit tests. GPL-3.0.
// Lane AUDIO-3: sounds that play when they should, and only then. The request log of the audio manager (AudioManager::enableEventLog,
// LiveGameAudio::takeEventLogLines) and a diagnostic skirmish that writes it (tools/audio/audio_log_sweep.py reads the file).
#include "doctest.h"

#include "BuildTestUtil.h"
#include "ProdTestUtil.h"
#include "HudTestUtil.h"
#include "IniTestUtil.h"
#include "PerfScenarios.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioLog.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/Eva.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/LiveFX.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LiveGameAudio.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/WallSpan.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{
// a manager with two events: "Click" (2D) and "Thud" (positional, Limit 1)
struct MiniAudio
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	MiniAudio()
	{
		fx.mount({ { "data\\audio\\sounds\\click.wav", "x" } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini",
			"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  SoundsExtension = wav\n  SampleCount2D = 2\n  SampleCount3D = 3\n"
			"  StreamCount = 3\n  MinSampleVolume = 10\nEnd\nAudioEvent Click\n  Sounds = click\n  Type = ui everyone\nEnd\n"
			"AudioEvent Thud\n  Sounds = click\n  Limit = 1\n  Type = world everyone\nEnd\n"
			"AudioEvent LoopA\n  Sounds = click\n  Control = loop\n  Type = world everyone\n  MaxRange = 100\nEnd\n"
			"AudioEvent LoopB\n  Sounds = click\n  Control = loop\n  Type = world everyone\n  MaxRange = 100\n  Delay = 2000 5000\nEnd\n"
			"AudioEvent Late\n  Sounds = click\n  Type = world everyone\n  MaxRange = 100\n  Delay = 40 40\nEnd\n"
			"Multisound Pair\n  Subsounds = LoopA LoopB\nEnd\n"
			"AudioEvent LoopC\n  Sounds = click\n  Control = loop\n  Type = world everyone\n  MaxRange = 100\n  Delay = 2000 5000\nEnd\n"
			"AudioEvent Silent\n  Sounds = click\n  Control = loop\n  Type = world everyone\n  Volume = 0\nEnd\n"
			"Multisound Inner\n  Subsounds = LoopB LoopC\nEnd\n"
			"Multisound InnerFirst\n  Subsounds = Inner LoopA\nEnd\n"
			"Multisound InnerLast\n  Subsounds = LoopA Inner\nEnd\n"
			"Multisound MutedFirst\n  Subsounds = Silent LoopB\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1u);
	}
};
} // namespace

TEST_CASE("audio3: the request log records each request with its caller's tag and what became of it (off by default)")
{
	MiniAudio a;
	a.mgr->playSound("Click");
	CHECK(a.mgr->takeEventLog().empty()); // off: nothing is kept
	a.mgr->enableEventLog(true);
	a.mgr->setLogFrame(12);
	{
		AudioLog::Scope outer("fx FXListDie");
		AudioLog::Scope inner("obj 7");
		CHECK(AudioLog::origin() == "fx FXListDie/obj 7");
		a.mgr->playSound("Click");
	}
	CHECK(AudioLog::origin().empty());
	a.mgr->playSound("NoSuchEvent");
	a.mgr->playSoundAt("Thud", Coord3D{ 0.0f, 0.0f, 0.0f });
	a.mgr->playSoundAt("Thud", Coord3D{ 0.0f, 0.0f, 0.0f }); // Limit 1: the second is refused while the first is queued
	std::vector<AudioLogEntry> log = a.mgr->takeEventLog();
	REQUIRE(log.size() == 4);
	CHECK(log[0].event == "Click");
	CHECK(log[0].origin == "fx FXListDie/obj 7");
	CHECK(log[0].frame == 12u);
	CHECK(log[0].outcome == "queued");
	CHECK_FALSE(log[0].play);
	CHECK(log[1].outcome == "refused: unknown event");
	CHECK(log[2].outcome == "queued");
	CHECK(log[2].positional);
	CHECK(log[3].outcome == "refused: Limit");
	a.mgr->update(0.0);
	a.mgr->update(100.0);
	log = a.mgr->takeEventLog();
	// both Clicks (the one requested before the log was on was queued all the same) and the first Thud reached the device, whose file here is not a WAV
	REQUIRE(log.size() == 3);
	for (const AudioLogEntry &e : log)
	{
		CHECK(e.play);
		CHECK(e.outcome.rfind("failed: ", 0) == 0);
	}
	CHECK(log[0].event == "Click");
	CHECK(log[1].event == "Click");
	CHECK(log[2].event == "Thud");
}

namespace
{
// the retail audio tables and a manager on a simulated device, installed for this thread with the request log on (the logic of a buildtest game runs on
// this thread, so its audio requests run at once)
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
		manager->enableEventLog(true);
		AudioApi::install(manager.get());
	}
	~RetailAudio() { AudioApi::install(nullptr); }
	// the request lines of `event` for `objectId` (0: any object) whose caller tag contains `origin`
	int requests(const std::vector<AudioLogEntry> &log, const std::string &event, std::uint32_t objectId, const std::string &origin) const
	{
		int n = 0;
		for (const AudioLogEntry &e : log)
		{
			n += !e.play && e.event == event && (objectId == 0 || e.objectId == objectId) && e.origin.find(origin) != std::string::npos ? 1 : 0;
		}
		return n;
	}
};

void appendAll(std::vector<AudioLogEntry> &into, std::vector<AudioLogEntry> &&from)
{
	into.insert(into.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
}
} // namespace

// The owner's MP-2 game: "the build sound for the wall didn't seem to play". Retail's wall sounds are logic calls the port lacked: the wall hub's worker starts the
// hub's UnitSpecificSounds UnderConstruction (BuildingConstructionLoop) when it reaches the site (RW 0x88DD74 -> startBuildingSound RW 0x88C4A3) and stops it at
// the completion (RW 0x88DEFA); every wall segment builds itself (no WorkerName) and its GettingBuiltBehavior starts SelfBuildingLoop (WallConstructionLoop,
// RW 0x8566DF) and removes it when done (RW 0x856B11).
TEST_CASE("audio3 walls: the wall hub's worker plays the hub's UnderConstruction loop, each wall segment its SelfBuildingLoop, each stopped at completion")
{
	OPENBFME_REQUIRE_START(s);
	RetailAudio audio(*s->mount->fs);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player != nullptr);
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	Object *centre = nullptr, *plot = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		centre = (o && o->getControllingPlayer() == player) ? o : centre;
	}
	REQUIRE(centre != nullptr);
	float best = -1.0f;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("BASE_FOUNDATION"))
		{
			const float dx = o->getPosition()->x - centre->getPosition()->x, dy = o->getPosition()->y - centre->getPosition()->y;
			if (dx * dx + dy * dy > best)
			{
				best = dx * dx + dy * dy;
				plot = o; // the outermost pad
			}
		}
	}
	REQUIRE(plot != nullptr);
	const ThingTemplate *hubTmpl = logic.things().findTemplate("MenWallHubSmallExpansion");
	const ThingTemplate *capTmpl = logic.things().findTemplate("MenWallHubSmall");
	REQUIRE(hubTmpl != nullptr);
	REQUIRE(capTmpl != nullptr);
	audio.manager->takeEventLog();
	// the hub: built by its spawned worker (GondorWorkerNoSelect, a dozer)
	Object *hub = Construction::constructOnPlot(*plot, *hubTmpl->getFinalOverride(), *plot->getPosition(), 0.0f, *player, false);
	REQUIRE(hub != nullptr);
	std::vector<AudioLogEntry> log;
	ObjectID worker = INVALID_ID;
	bool heldWhileBuilding = false;
	for (int i = 0; i < 2000 && hub->getConstructionPercent() != -1.0f; ++i)
	{
		logic.runLogicFrame();
		worker = hub->getBuilderID() != hub->getID() && hub->getBuilderID() != INVALID_ID ? hub->getBuilderID() : worker;
		heldWhileBuilding |= worker != INVALID_ID && AudioApi::heldSound(AudioApi::HELD_BUILDING_LOOP, worker) != 0;
	}
	REQUIRE(hub->getConstructionPercent() == -1.0f);
	REQUIRE(worker != INVALID_ID);
	appendAll(log, audio.manager->takeEventLog());
	CHECK(audio.requests(log, "BuildingConstructionLoop", hub->getID(), "building loop") == 1); // started once, when the worker reached the site
	CHECK(heldWhileBuilding);                                                                    // kept by the worker (RW: the dozer's handle)
	CHECK(AudioApi::heldSound(AudioApi::HELD_BUILDING_LOOP, worker) == 0);                     // stopped at the completion
	CHECK(audio.requests(log, "GondorBarracksBeginConstruction", 0, "") == 0); // the hub's draw script sound is the client's (no drawables here)

	// the span: every segment and the cap build themselves; each asks for its SelfBuildingLoop once and stops it when done
	Coord3D start = *hub->getPosition(), end = start;
	const float dx = start.x - centre->getPosition()->x, dy = start.y - centre->getPosition()->y;
	const float len = std::sqrt(dx * dx + dy * dy);
	end.x += dx / len * 400.0f;
	end.y += dy / len * 400.0f;
	const unsigned optionOne = 1u << 13;
	GameMessage m(MSG_WALL_HUB_CONSTRUCT_SPAN, player->getPlayerIndex());
	m.appendIntegerArgument((int)capTmpl->getFinalOverride()->getTemplateID());
	m.appendLocationArgument(start);
	m.appendLocationArgument(end);
	m.appendIntegerArgument((int)optionOne);
	m.appendObjectIDArgument(hub->getID());
	live.commands().append(m);
	logic.runLogicFrame();
	std::vector<Object *> tiles;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == hub->getID() && o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE"))
		{
			tiles.push_back(o);
		}
	}
	REQUIRE(tiles.size() >= 3);
	int held = 0;
	bool done = false;
	for (int i = 0; i < 3000 && !done; ++i)
	{
		logic.runLogicFrame();
		int now = 0;
		for (Object *t : tiles)
		{
			now += AudioApi::heldSound(AudioApi::HELD_BUILDING_LOOP, t->getID()) != 0 ? 1 : 0;
		}
		held = std::max(held, now);
		done = true;
		for (Object *t : tiles)
		{
			done = done && t->getConstructionPercent() == -1.0f;
		}
	}
	REQUIRE(done);
	appendAll(log, audio.manager->takeEventLog());
	int segments = 0;
	for (Object *t : tiles)
	{
		const GettingBuiltBehavior *gb = dynamic_cast<const GettingBuiltBehavior *>(t->findModule("GettingBuiltBehavior"));
		REQUIRE(gb != nullptr);
		const std::string &loop = gb->data()->m_selfBuildingLoop;
		INFO(t->getTemplate()->getName() << " " << loop);
		CHECK(audio.requests(log, loop, t->getID(), "building loop") == (loop.empty() ? 0 : 1)); // the cap (MenWallHubSmall) has no SelfBuildingLoop
		CHECK(AudioApi::heldSound(AudioApi::HELD_BUILDING_LOOP, t->getID()) == 0);
		segments += t->getTemplate()->getName() == "MenWallSegmentSmall" ? 1 : 0;
	}
	CHECK(segments >= 3);
	CHECK(audio.requests(log, "WallConstructionLoop", 0, "building loop") == segments); // MenWallSegmentSmall: SelfBuildingLoop = WallConstructionLoop
	CHECK(held >= 2); // the staggered segments overlap
}

// The owner's "a door sound or something that kept playing": the port played a unit's SoundMoveStart whenever its MOVING model condition came back (a client
// guess, S-712), so a unit whose MOVING flickered while walking (blocked a frame, the locomotor at speed 0) replayed it every few frames. Retail plays it once per
// entry of AIInternalMoveToState (RW 0x74E06F -> startMoveSound RW 0x748C0B): SoundMoveStart once, SoundMoveLoop held until the state is left.
TEST_CASE("audio3 move sounds: one SoundMoveStart per move order and the SoundMoveLoop held until the move ends (RW 0x748C0B), whatever MOVING does")
{
	OPENBFME_REQUIRE_START(s);
	RetailAudio audio(*s->mount->fs);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player != nullptr);
	Object *porter = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->getTemplate()->getName() == "MenPorter")
		{
			porter = o;
		}
	}
	REQUIRE(porter != nullptr);
	logic.runLogicFrame();
	audio.manager->takeEventLog();
	AIUpdateInterface *ai = porter->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	int movingChanges = 0;
	for (int order = 0; order < 2; ++order)
	{
		Coord3D goal = *porter->getPosition();
		goal.x += order == 0 ? 150.0f : -150.0f;
		ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
		bool was = false;
		for (int i = 0; i < 150; ++i)
		{
			logic.runLogicFrame();
			const bool now = porter->testModelCondition(AIUpdateInterface::modelConditionBit("MOVING"));
			movingChanges += now != was ? 1 : 0;
			was = now;
			if (i > 5 && ai->isIdle())
			{
				break;
			}
		}
		CHECK(ai->isIdle());
		CHECK(AudioApi::heldSound(AudioApi::HELD_MOVE_LOOP, porter->getID()) == 0); // the loop ended with the move
	}
	const std::vector<AudioLogEntry> log = audio.manager->takeEventLog();
	INFO("MOVING changed " << movingChanges << " times");
	CHECK(audio.requests(log, "MenBuilderMoveStart", porter->getID(), "") == 2);
	CHECK(audio.requests(log, "MenBuilderMoveLoopMS", porter->getID(), "move loop") == 2);
}

// RW 0x45CC91 / 0x461A83: a sound canPlayNow refuses is queued when it loops or its Delay is at least one audio step (33.33 ms) and checked again when due;
// a non-looping one that is due now is refused. RW 0x459B55: a multisound that plays all its subsounds is one handle: removing it removes all of them.
TEST_CASE("audio3: culled loops and delayed sounds wait for a later check; a multisound's handle removes every subsound")
{
	MiniAudio a;
	a.mgr->enableEventLog(true);
	a.mgr->setListenerPosition(Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 0.0f, 1.0f, 0.0f });
	const Coord3D far{ 5000.0f, 0.0f, 0.0f };
	CHECK(a.mgr->playSoundAt("Thud", far) == AHSV_NoSound);       // due now, no loop: refused (beyond MaxRange: the Thud's default range)
	const AudioHandle late = a.mgr->playSoundAt("Late", far);       // Delay 40: queued, refused when due
	CHECK(late >= AHSV_FirstHandle);
	const AudioHandle loop = a.mgr->playSoundAt("LoopA", far);      // a loop: queued and checked again until it can play
	CHECK(loop >= AHSV_FirstHandle);
	a.mgr->update(0.0);
	for (int i = 1; i <= 10; ++i)
	{
		a.mgr->update(i * 100.0);
	}
	CHECK_FALSE(a.mgr->isCurrentlyPlaying(late));
	CHECK(a.mgr->isCurrentlyPlaying(loop)); // still waiting in the queue
	CHECK(a.mgr->playingCount(VoiceKind::Sample3D) == 0);
	bool lateRefused = false;
	for (const AudioLogEntry &e : a.mgr->takeEventLog())
	{
		lateRefused |= e.event == "Late" && e.outcome.rfind("refused after its delay: beyond MaxRange", 0) == 0;
	}
	CHECK(lateRefused);
	a.mgr->removeAudioEvent(loop);
	a.mgr->update(1200.0);
	CHECK(a.mgr->pendingRequests() == 0);
	// the multisound: both subsounds are queued (LoopB has a delay), one handle stops both
	const AudioHandle pair = a.mgr->playSoundAt("Pair", Coord3D{ 10.0f, 0.0f, 0.0f });
	REQUIRE(pair >= AHSV_FirstHandle);
	a.mgr->update(1300.0);
	CHECK(a.mgr->pendingRequests() >= 1); // LoopB waits for its delay
	a.mgr->removeAudioEvent(pair);
	for (int i = 0; i < 80; ++i)
	{
		a.mgr->update(1400.0 + i * 100.0);
	}
	CHECK(a.mgr->pendingRequests() == 0);
	CHECK_FALSE(a.mgr->isCurrentlyPlaying(pair));
}

// review r1 (Sol): a group's handle is a live leaf and nested all-multisounds are flattened, so one handle stops every leaf in either nesting order and when the
// first child is refused (muted); setEventPosition moves every leaf, queued ones included
TEST_CASE("audio3 r2: a multisound handle reaches every leaf (nested either way, a refused first child) and moves them all")
{
	for (const char *name : { "InnerFirst", "InnerLast", "MutedFirst" })
	{
		INFO(name);
		MiniAudio a;
		a.mgr->setListenerPosition(Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 0.0f, 1.0f, 0.0f });
		a.mgr->update(0.0);
		const AudioHandle h = a.mgr->playSoundAt(name, Coord3D{ 10.0f, 0.0f, 0.0f });
		REQUIRE(h >= AHSV_FirstHandle); // a live leaf, never the muted child's sentinel
		a.mgr->update(100.0);
		CHECK(a.mgr->pendingRequests() >= 1); // the delayed loops wait
		a.mgr->removeAudioEvent(h);
		for (int i = 0; i < 80; ++i)
		{
			a.mgr->update(200.0 + i * 100.0);
		}
		CHECK(a.mgr->pendingRequests() == 0);
		CHECK(a.mgr->playingCount(VoiceKind::Sample3D) == 0);
		CHECK_FALSE(a.mgr->isCurrentlyPlaying(h));
	}
	// both leaves of a group placed out of range follow setEventPosition (LoopA re-checked, LoopB still in its delay) and start once near
	MiniAudio a;
	a.mgr->setListenerPosition(Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 0.0f, 1.0f, 0.0f });
	a.mgr->update(0.0);
	const AudioHandle pair = a.mgr->playSoundAt("Pair", Coord3D{ 5000.0f, 0.0f, 0.0f });
	REQUIRE(pair >= AHSV_FirstHandle);
	a.mgr->update(100.0);
	CHECK(a.mgr->playingCount(VoiceKind::Sample3D) == 0);
	CHECK(a.mgr->setEventPosition(pair, Coord3D{ 10.0f, 0.0f, 0.0f }));
	a.mgr->enableEventLog(true);
	for (int i = 0; i < 80; ++i)
	{
		a.mgr->update(200.0 + i * 100.0);
	}
	int startedA = 0, startedB = 0;
	for (const AudioLogEntry &e : a.mgr->takeEventLog())
	{
		const bool reached = e.play && e.outcome.rfind("refused", 0) != 0; // the device was asked (the fixture's file is not a WAV)
		startedA += reached && e.event == "LoopA" ? 1 : 0;
		startedB += reached && e.event == "LoopB" ? 1 : 0;
	}
	CHECK(startedA >= 1);
	CHECK(startedB >= 1);
}

// review r1 (Sol): with the log off nothing builds a tag (AudioLog::enabled is the gate the call sites test first)
TEST_CASE("audio3 r2: the log gate: off by default, on while a manager logs, off again when it stops or is destroyed")
{
	CHECK_FALSE(AudioLog::enabled());
	{
		AudioLog::Scope s([] { FAIL("a tag was built with the log off"); return std::string("x"); });
		CHECK(AudioLog::origin().empty());
	}
	{
		MiniAudio a;
		a.mgr->enableEventLog(true);
		CHECK(AudioLog::enabled());
		{
			AudioLog::Scope s([] { return std::string("built"); });
			CHECK(AudioLog::origin() == "built");
		}
		a.mgr->enableEventLog(false);
		CHECK_FALSE(AudioLog::enabled());
		a.mgr->enableEventLog(true);
	}
	CHECK_FALSE(AudioLog::enabled()); // the destroyed manager gave its count back
}

// RW 0x856992 (GettingBuiltBehavior::finishConstruction): not constructing and with a WorkerName the function returns at RW 0x856A2C, before the loop removal
// and m_rebuilding = false (RW 0x856B30); without a WorkerName it reaches them. The port cleared m_rebuilding on both paths (review r1)
TEST_CASE("audio3 r2: finishConstruction keeps m_rebuilding on the WorkerName path, as RW 0x856A2C returns early")
{
	for (int withWorker = 0; withWorker < 2; ++withWorker)
	{
		INFO("WorkerName " << withWorker);
		prodtest::ProdWorld f;
		f.load(std::string("Object Hut\n  KindOf = STRUCTURE SELECTABLE IMMOBILE\n  Behavior = GettingBuiltBehavior ModuleTag_GB\n") +
			(withWorker ? "    WorkerName = Hut\n" : "") +
			"    RebuildWhenDead = Yes\n  End\n  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 50\n    MaxHealthReallyDamaged = 10\n  End\nEnd\n");
		Object *hut = f.make("Hut", f.teamOf("Alice"));
		REQUIRE(hut != nullptr);
		GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(hut->findModule("GettingBuiltBehavior"));
		REQUIRE(gb != nullptr);
		hut->friend_setEffectivelyDead(true);
		gb->startConstruction(true); // a dead structure rebuilds (+0x35 set, RW 0x856701)
		REQUIRE(gb->rebuilding());
		gb->stopConstruction(); // not constructing any more
		REQUIRE_FALSE(gb->isConstructing());
		gb->finishConstruction(nullptr);
		CHECK(gb->rebuilding() == (withWorker == 1));
	}
}

// RW 0x88E44F (DozerAIUpdate::aiDoCommand): a player's command cancels the dozer's task before it runs (ZH / Open-BFME-2 the same), so a Porter ordered away
// from its site stays away instead of walking back the next frame (and replaying its move start, the owner's repeated creak); the AI's own orders do not cancel
TEST_CASE("audio3 r2: a player's move order cancels a Porter's build task (RW 0x88E44F); an AI move does not")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player != nullptr);
	Object *porter = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->getTemplate()->getName() == "MenPorter")
		{
			porter = o;
		}
	}
	REQUIRE(porter != nullptr);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);
	const ThingTemplate *farm = logic.things().findTemplate("GondorFarm");
	REQUIRE(farm != nullptr);
	Coord3D site = *porter->getPosition();
	Object *structure = nullptr;
	for (int i = 0; i < 16 && !structure; ++i)
	{
		Coord3D at = site;
		at.x += 150.0f + 40.0f * (float)i;
		at.y += 60.0f;
		at.z = logic.getGroundHeight(at.x, at.y);
		structure = dozer->construct(*farm->getFinalOverride(), at, 0.0f, *player);
	}
	REQUIRE(structure != nullptr);
	const ObjectID structureId = structure->getID();
	const std::uint32_t moneyAfterOrder = player->getMoney()->countMoney();
	const std::uint32_t paid = (std::uint32_t)structure->getBuildCostPaid();
	for (int i = 0; i < 5; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(dozer->taskTarget() == structure->getID());
	// the AI's own move keeps the task
	Coord3D away = *porter->getPosition();
	away.x -= 300.0f;
	dozer->aiMoveToPosition(away, CMD_FROM_AI);
	CHECK(dozer->taskTarget() == structure->getID());
	// the player's move cancels it: the Porter walks away and stays there
	dozer->aiMoveToPosition(away, CMD_FROM_PLAYER);
	CHECK(dozer->taskTarget() == INVALID_ID);
	for (int i = 0; i < 150 && !dozer->isIdle(); ++i)
	{
		logic.runLogicFrame();
	}
	for (int i = 0; i < 20; ++i)
	{
		logic.runLogicFrame();
	}
	const float dx = porter->getPosition()->x - away.x, dy = porter->getPosition()->y - away.y;
	CHECK(dx * dx + dy * dy < 60.0f * 60.0f);
	CHECK(dozer->taskTarget() == INVALID_ID);
	// lane BUILD-3: the Porter had not reached the site, so the structure was not placed yet: the cancel removes it and pays its price back (RW 0x88E3CE -> 0x88CFFE)
	CHECK(logic.findObjectByID(structureId) == nullptr);
	CHECK(paid > 0);
	CHECK(player->getMoney()->countMoney() >= moneyAfterOrder + paid);
}

// OPENBFME_AUDIO3_DIAG=<frames>,<log path>[,<seed>]: a 4-player skirmish of medium computer players (Men, Mordor, Elves, Isengard on map mp fall back 4p)
// with the in-game audio and the effect player (LiveFX) attached to a simulated device and every sound request logged; the listener follows the first command centre's owner's first structure. The client is
// presented at 30 frames a second (6 presents per logic frame) like the game's render loop.
TEST_CASE("audio3 diag: a skirmish's sound request log (OPENBFME_AUDIO3_DIAG=<frames>,<path>[,<seed>])" * doctest::skip())
{
	const char *spec = std::getenv("OPENBFME_AUDIO3_DIAG");
	REQUIRE_MESSAGE(spec != nullptr, "set OPENBFME_AUDIO3_DIAG");
	const std::string s(spec);
	const size_t c1 = s.find(',');
	REQUIRE(c1 != std::string::npos);
	const size_t c2 = s.find(',', c1 + 1);
	const int frames = std::atoi(s.substr(0, c1).c_str());
	const std::string path = s.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
	const std::uint32_t seed = c2 == std::string::npos ? 7u : (std::uint32_t)std::atoi(s.c_str() + c2 + 1);

	hudtest::SharedWorld w;
	GameLogicSettings settings;
	std::string error;
	REQUIRE_MESSAGE(perf1::loadFreshWorld(w, settings, error), error);
	auto scope = w.world->enterContext();

	INIEnvironment env;
	env.fileSystem = w.mount->fs.get();
	AudioIniState ini;
	ini.registerBlocks(env.blocks);
	{
		INI parser(env);
		ini.loadAll(parser);
		ini.loadEva(parser);
		ini.loadLargeGroupAudio(parser);
	}
	AudioAssetCache cache(w.mount->fs.get(), 64u * 1024u * 1024u);
	SimulatedAudioDevice device(&cache);
	AudioManager manager(ini, cache, device, RandomAlgorithm::RotWK_GameDat_LCG, 1u);
	AudioApi::install(&manager);
	Eva eva(ini, manager);
	AudioApi::installEva(&eva);

	std::vector<MapCacheEntry> mapCache;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*w.mount->fs, mapCache, &error), error);
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 4p/map mp fall back 4p.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const std::vector<std::string> &factions = perf1::fourFactions();
	for (size_t i = 0; i < factions.size(); ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_MED_AI;
		slot.name = std::u16string(u"Player") + (char16_t)(u'0' + (int)i);
		slot.playerTemplate = -1;
		for (int t = 0; t < w.world->playerTemplates().getPlayerTemplateCount(); ++t)
		{
			if (w.world->playerTemplates().getNthPlayerTemplate(t)->getName() == factions[i])
			{
				slot.playerTemplate = t;
			}
		}
		REQUIRE(slot.playerTemplate >= 0);
		slot.startPos = (int)i;
		slot.color = (int)i;
		slot.teamNumber = (int)i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, w.world->playerTemplates(), settings, mapCache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*w.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*w.world, *w.mount->fs, assets, w.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);

	// the effect player (lane FX-2): the FX list sound nuggets (fire, impact, death) are its requests
	FXPlayback playback(*w.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(playback.loadRetailData().empty());
	LiveFX fx(game, playback);
	std::unique_ptr<LiveGameAudio> audio = LiveGameAudio::attachToInstalled(game, nullptr, &error, nullptr);
	REQUIRE_MESSAGE(audio != nullptr, error);
	FILE *out = std::fopen(path.c_str(), "w");
	REQUIRE_MESSAGE(out != nullptr, path);
	std::fprintf(out, "%s\n", LiveGameAudio::eventLogHeader());
	audio->enableEventLog(true);

	double clockMs = 0.0;
	const Player *watched = nullptr;
	for (int f = 0; f < frames; ++f)
	{
		for (int p = 0; p < 6; ++p)
		{
			game.advance(0.2 / 6.0);
			LiveGameAudio::drainDeferredAudio();
			fx.flushPending(); // as GameWorld's frame: the logic's effect calls, then the attached systems
			fx.updateAttachedSystems();
			playback.step();
			// the listener over the first command centre's owner's first structure (the base it watches; every player is a computer player)
			if (!watched)
			{
				for (Object *obj = game.logic().getFirstObject(); obj; obj = obj->getNextObject())
				{
					if (obj->isKindOfName("COMMANDCENTER") && obj->getControllingPlayer())
					{
						watched = obj->getControllingPlayer();
						break;
					}
				}
			}
			for (Object *obj = game.logic().getFirstObject(); obj && watched; obj = obj->getNextObject())
			{
				if (obj->getControllingPlayer() == watched && obj->isKindOfName("STRUCTURE"))
				{
					manager.setListenerPosition(*obj->getPosition(), Coord3D{ 0.0f, 1.0f, 0.0f });
					break;
				}
			}
			audio->update();
			clockMs += 200.0 / 6.0;
			manager.update(clockMs);
			for (const std::string &l : audio->takeEventLogLines())
			{
				std::fprintf(out, "%s\n", l.c_str());
			}
		}
	}
	std::fclose(out);
	audio.reset();
	AudioApi::installEva(nullptr);
	AudioApi::install(nullptr);
	MESSAGE("audio log written: " << path << " (" << frames << " frames)");
}
