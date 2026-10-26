// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-2 on the retail game (docs/SAVES.md T1, the command-log variant, and T5's log failures): a recorded skirmish (a scripted human that selects,
// moves and attack-moves its army against a Medium AI on Evendim) is saved at frame F; the save file is loaded into a new game, caught up to F from its log
// (GameClient/CommandLogLoad.h) and played on with the same human: every later frame's state hash equals the unsaved game's, and its recording continues
// the saved log so the whole game is one replay. A save of the other execution profile loads (single player) without the hash comparison; a log whose
// recorded hash or header was changed is refused naming the frame. The game files are read in this process (the "fresh process" of T1 is a fresh LiveGame
// here). SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Economy.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Weapon.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/SimulationProfile.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/CommandLogLoad.h"
#include "GameClient/ReplaySeek.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LinearCampaign.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "Common/BuildVersion.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameNetwork/LockstepDriver.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace
{
// the game and the scripted human of test_net_retail.cpp (lane MP-1; its helpers are file-local): a human (slot 0, Men) against a Medium AI (slot 1, Mordor)
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

NewGameMessage gameMessage(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = SLOT_PLAYER;
	h.name = u"Human";
	h.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMen");
	h.startPos = 0;
	h.color = 0;
	h.teamNumber = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = SLOT_MED_AI;
	c.name = u"Computer";
	c.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMordor");
	c.startPos = 1;
	c.color = 1;
	c.teamNumber = 1;
	return m;
}

const std::vector<MapCacheEntry> &mapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// every 20 frames the human's first ten mobile objects are selected and moved around its start (every third time an attack-move toward the AI): a function
// of the game's state, so the unsaved and the loaded game issue the same commands when their states are the same
void humanCommands(LiveGame &game, CommandList &out, bool requireHuman = true)
{
	const UnsignedInt f = game.frame();
	if (f == 0 || f % 20 != 0)
	{
		return;
	}
	GameLogic &logic = game.logic();
	const Player *human = nullptr;
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p->getPlayerType() == PLAYER_HUMAN && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide)
		{
			human = p;
		}
	}
	if (!requireHuman && !human)
	{
		return;
	}
	REQUIRE(human != nullptr);
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, human->getPlayerIndex());
	sel.appendBooleanArgument(true);
	Coord3D at{ 0, 0, 0 };
	int n = 0;
	for (Object *o = logic.getFirstObject(); o && n < 10; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == human && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE"))
		{
			sel.appendObjectIDArgument(o->getID());
			at = *o->getPosition();
			++n;
		}
	}
	if (n == 0)
	{
		return;
	}
	out.append(sel);
	GameMessage mv((f / 20) % 3 == 2 ? MSG_DO_ATTACKMOVETO : MSG_DO_MOVETO, human->getPlayerIndex());
	at.x += (f / 20) % 2 ? 120.0f : -120.0f;
	at.y += 60.0f;
	at.z = logic.getGroundHeight(at.x, at.y);
	mv.appendLocationArgument(at);
	out.append(mv);
}

struct Loaded
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

void load(starttest::Shared &s, const NewGameMessage &m, Loaded &out, bool paced = false)
{
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	o.sixTickPacing = paced;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

// a campaign mission as GameWorld::start_campaign_mission loads it (the map's own sides and scripts, the difficulty)
void loadMission(starttest::Shared &s, const std::string &map, int difficulty, Loaded &out)
{
	std::string error;
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.mapName = map;
	o.campaign = true;
	o.difficulty = difficulty;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

ProfileIdentity withProfile(const ProfileIdentity &p, const std::string &value)
{
	const std::string t = p.text;
	const size_t at = t.find("\nprofile "); // the identity's second line ("openbfme-profile 1" is the first)
	REQUIRE(at != std::string::npos);
	const size_t end = t.find('\n', at + 1);
	return ProfileIdentity::fromText(t.substr(0, at) + "\nprofile " + value + t.substr(end));
}

struct Recorded
{
	SaveGameFile save;          ///< the save taken at F
	ReplayFile whole;           ///< the unsaved game's recording, start .. F + after
	UnsignedInt startFrame = 0, savedFrame = 0;
};

// the unsaved game: recorded from its start; the save is taken after `beforeSave` frames, then it runs `after` frames more
// lane SAVE-4e: `snapshot` also takes the snapshot at F into the save (as GameWorld::save_game does)
Recorded recordGame(starttest::Shared &s, const NewGameMessage &message, const ProfileIdentity &profile, int beforeSave, int after, const std::string &tag,
	bool snapshot = false, const std::function<void(LiveGame &)> &beforeLastFrame = nullptr)
{
	Recorded r;
	const std::string path = "save2_" + tag + ".replay";
	Loaded g;
	load(s, message, g);
	ReplayWriter writer;
	writer.setKeepCopy(true);
	ReplayHeader h;
	h.game = message;
	h.recordingSlot = 0;
	h.profile = profile;
	std::string error;
	REQUIRE_MESSAGE(writer.open(path, h, &error), error);
	LockstepDriver driver(nullptr, &writer);
	RegisterLogicCRCHandler(g.game->dispatch());
	r.startFrame = g.game->frame();
	g.game->setFrameDriver(&driver);
	for (int i = 0; i < beforeSave; ++i)
	{
		if (beforeLastFrame && i == beforeSave - 1)
		{
			g.game->drainFrames();
			beforeLastFrame(*g.game); // lane SAVE-4e review r1: a change outside the commands, before the last frame (a toppled tree)
		}
		humanCommands(*g.game, g.game->commands());
		REQUIRE(g.game->advance(0.2) == 1);
	}
	// the save, as GameWorld::save_game takes it: the queued batches run, the log so far + (F, hash)
	g.game->drainFrames();
	r.savedFrame = g.game->logic().getFrame();
	const std::uint32_t hash = g.game->logic().computeStateHash();
	if (!writer.hasHashOf(r.savedFrame))
	{
		writer.recordHash(r.savedFrame, hash); // lane UBER-2C: as save_game does for an Uber recording (every 5th frame's hash)
	}
	REQUIRE_MESSAGE(writer.logUpTo(r.savedFrame, hash, r.save.log, &error), error);
	if (snapshot)
	{
		REQUIRE_MESSAGE(g.game->logic().saveSnapshot(r.save.snapshot, &error), error);
	}
	SaveGameHeader &sh = r.save.header;
	sh.buildVersion = "test";
	sh.engineId = ProfileIdentity::buildId();
	sh.profile = profile;
	sh.executionProfile = g.game->logic().simulationProfile() == SimulationProfile::Uber ? "uber" : "enhanced";
	sh.gameKind = 2;
	sh.frame = r.savedFrame;
	sh.stateHash = hash;
	sh.description = tag;
	sh.mapPath = message.game.mapName;
	for (int i = 0; i < after; ++i)
	{
		humanCommands(*g.game, g.game->commands());
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	writer.close(g.game->logic().getFrame(), g.game->logic().computeStateHash());
	CHECK(writer.errors().empty());
	g.game->setFrameDriver(nullptr);
	REQUIRE_MESSAGE(ReplayFile::load(path, r.whole, &error), error);
	std::remove(path.c_str());
	return r;
}

// a loaded save caught up to its frame (the state Ready), or the failure
CommandLogLoad::State catchUp(starttest::Shared &s, const SaveGameFile &save, Loaded &g, CommandLogLoad &cl, std::string &error)
{
	if (!cl.prepare(save, &error))
	{
		return cl.state();
	}
	load(s, cl.log().header.game, g);
	if (!cl.begin(*g.game, &error))
	{
		return cl.state();
	}
	// lane MACGATE-1: budget 0 = one frame per step, so the scripted human's input (every 20th frame) is offered at every frame boundary. A
	// 20 ms budget made it timing-dependent: on a steady host each step ran the same number of frames and the step ends could miss every
	// 20th frame, so localInputIgnored() stayed 0 (failed alone on macOS, passed under load)
	while (cl.step(*g.game, 0) == CommandLogLoad::State::CatchingUp)
	{
		humanCommands(*g.game, g.game->commands()); // the local input during the catch-up is ignored (it would be the player's, ZH playback)
	}
	error = cl.failure();
	return cl.state();
}

// changes the recorded hash of `frame` in a log: its hash record (u8 2, u32 frame, u32 hash) found by all nine bytes; false when there is none
bool changeRecordedHash(std::vector<std::uint8_t> &log, UnsignedInt frame, std::uint32_t hash)
{
	std::uint8_t record[9] = { 2 };
	std::memcpy(record + 1, &frame, 4);
	std::memcpy(record + 5, &hash, 4);
	for (size_t i = 8; i + 9 <= log.size(); ++i)
	{
		if (std::memcmp(log.data() + i, record, 9) == 0)
		{
			log[i + 5] ^= 0x55;
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("CASTER-2 seek: restart and budgeted seek match every recorded hash, including threaded and partially paced steps")
{
	OPENBFME_REQUIRE_START(s);
	const auto profile = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 2468u), profile, 40, 40, "caster_seek");
	for (int mode = 0; mode < 3; ++mode)
	{
		Loaded g;
		load(*s, rec.whole.header.game, g, mode == 2);
		ReplayPlayback playback(rec.whole);
		RegisterLogicCRCHandler(g.game->dispatch());
		g.game->setFrameDriver(&playback);
		g.game->enableReplayObserver();
		g.game->setLogicThread(mode == 1);
		if (mode == 2)
		{
			CHECK(g.game->advance(2.0 / 30.0) == 0);
		}
		const UnsignedInt target = rec.startFrame + 25;
		ReplaySeek::Result result;
		do
		{
			result = ReplaySeek::step(*g.game, playback, rec.whole, target, 1);
			REQUIRE_MESSAGE(result.error.empty(), result.error);
		} while (!result.ready);
		CHECK(g.game->protocolFrame() == target);
		CHECK(g.game->logic().computeStateHash() == rec.whole.hashes.at(target));
		CHECK(playback.hashesCompared() == 25);
		CHECK(g.game->logicThread() == (mode == 1));
		// S-4931: no world keyframes; a backward seek is refused by name (the caller restarts the replay and fast-forwards)
		CHECK(ReplaySeek::step(*g.game, playback, rec.whole, target - 1, 1).error ==
			"Seek target outside this playback range; restart the replay before seeking backwards");
		while (g.game->protocolFrame() < rec.whole.finalFrame)
		{
			REQUIRE(g.game->stepFrame() == 1);
			CHECK(g.game->logic().computeStateHash() == rec.whole.hashes.at(g.game->protocolFrame()));
		}
		CHECK(playback.hashesCompared() == 80);
		CHECK_FALSE(playback.hasMismatch());
		CHECK(g.game->stepFrame() == 0);
		g.game->setFrameDriver(nullptr);
	}
	for (bool missing : {false, true})
	{
		ReplayFile broken = rec.whole;
		const UnsignedInt badFrame = rec.startFrame + 3;
		if (missing)
		{
			broken.hashes.erase(badFrame);
		}
		else
		{
			broken.hashes[badFrame] ^= 1;
		}
		Loaded g;
		load(*s, broken.header.game, g);
		ReplayPlayback playback(broken);
		g.game->setFrameDriver(&playback);
		ReplaySeek::Result result;
		do
		{
			result = ReplaySeek::step(*g.game, playback, broken, broken.finalFrame, 1);
		} while (result.error.empty() && !result.ready);
		CHECK(result.error.find(std::to_string(badFrame)) != std::string::npos);
		CHECK(result.reached == (missing ? badFrame - 1 : badFrame));
		g.game->setFrameDriver(nullptr);
	}
}

TEST_CASE("CASTER-4 metrics: retail battalions count once, support units count, bounded income is explicit and sampling cannot change simulation")
{
	OPENBFME_REQUIRE_START(s);
	Loaded g;
	NewGameMessage message = gameMessage(*s, 2468u);
	message.game.slots[1].state = SLOT_PLAYER;
	load(*s, message, g);
	GameLogic &logic = g.game->logic();
	Player *player = logic.players().getLocalPlayer();
	REQUIRE(player != nullptr);
	std::int64_t expectedValue = 0;
	Object *horde = nullptr;
	for (const char *name : {"GondorFighterHorde", "RohanEomer", "MordorDrummerTroll"})
	{
		const ThingTemplate *tt = logic.things().findTemplate(name);
		REQUIRE(tt != nullptr);
		REQUIRE(tt->findField("BuildCost") != nullptr);
		CHECK(BuildAssistant::buildCost(*tt) > 0); // tolerance: a positive retail cost is required to expose accidental member double-counting.
		expectedValue += BuildAssistant::buildCost(*tt);
		Object *object = logic.newObject(tt, player->getDefaultTeam(), {});
		REQUIRE(object != nullptr);
		if (object->isKindOfName("HORDE"))
		{
			horde = object;
		}
	}
	REQUIRE(horde != nullptr);
	g.game->enableReplayObserver();
	REQUIRE(g.game->stepFrame() == 1);
	auto row = [&](const ObserverMetrics &metrics) -> const ObserverPlayerMetrics & {
		for (const auto &p : metrics.players())
		{
			if (p.player == player->getPlayerIndex())
			{
				return p;
			}
		}
		FAIL("the playable player is absent from metrics");
		return metrics.players().front();
	};
	ObserverMetrics direct;
	const auto hash = logic.computeStateHash();
	direct.capture(logic);
	CHECK(logic.computeStateHash() == hash);
	const auto &initial = row(direct);
	CHECK(initial.armyComplete);
	CHECK(initial.armyValue == expectedValue);
	REQUIRE(initial.army.count("GondorFighterHorde") == 1);
	CHECK(initial.army.at("GondorFighterHorde").count == 1);
	CHECK(initial.army.count("GondorFighter") == 0);
	CHECK(initial.army.count("MordorDrummerTroll") == 1);
	CHECK(initial.heroes.size() == 1);
	CHECK(initial.money == player->getMoney()->countMoney());
	CHECK(initial.cpUsed == player->commandPoints().getUsage());
	CHECK(initial.cpLimit == player->commandPointLimit());
	CHECK(initial.powerPoints == player->science().getSciencePurchasePoints());
	CHECK(initial.unitsLost == player->getScoreKeeper().unitsLost());
	logic.economy().noteIncome(*horde, *player, 7);
	ObserverMetrics current;
	current.capture(logic);
	CHECK(row(current).resourceIncome == initial.resourceIncome + 7);
	const auto count = direct.history().size();
	direct.capture(logic);
	CHECK(direct.history().size() == count);
	int members = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		members += o->getContainedBy() == horde ? 1 : 0;
	}
	CHECK(members > 0); // tolerance: the retail horde must have spawned members for the exclusion assertion to exercise anything.
	for (int i = 0; i < 8193; ++i)
	{
		logic.economy().noteIncome(*horde, *player, 7);
	}
	REQUIRE(g.game->stepFrame() == 1);
	CHECK_FALSE(row(g.game->observerMetrics()).incomeComplete);
	const UnsignedInt before = g.game->frame();
	const size_t samples = g.game->observerMetrics().history().size();
	for (int i = 0; i < 300; ++i)
	{
		REQUIRE(g.game->stepFrame() == 1);
		if (g.game->frame() == before + 299)
		{
			CHECK(row(g.game->observerMetrics()).incomeComplete);
		}
	}
	const auto &after = g.game->observerMetrics();
	CHECK(after.frame() == before + 300);
	CHECK(after.history().size() == samples + 60);
	CHECK(row(after).incomeComplete);
	std::int64_t income = 0;
	for (const auto &e : logic.economy().incomeLog())
	{
		if (e.playerIndex == player->getPlayerIndex() && e.frame > after.frame() - 300 && e.frame <= after.frame())
		{
			income += e.amount;
		}
	}
	CHECK(row(after).resourceIncome == income);
}

TEST_CASE("CASTER-2 timeline: damage groups by time, location and player without swallowing hero, power or capture events")
{
	ObserverTimeline timeline;
	ObserverEvent damage;
	damage.frame = 2;
	damage.player = 1;
	damage.weight = 10;
	timeline.add(damage);
	damage.frame = 3;
	timeline.add(damage);
	REQUIRE(timeline.events().size() == 1);
	CHECK(timeline.events()[0].weight == 20);
	damage.player = 2;
	timeline.add(damage);
	damage.position.x = 350;
	timeline.add(damage);
	damage.frame = 30;
	timeline.add(damage);
	CHECK(timeline.events().size() == 4);
	for (auto kind : {ObserverEvent::Death, ObserverEvent::Power, ObserverEvent::Capture})
	{
		damage.kind = kind;
		timeline.add(damage);
	}
	CHECK(timeline.events().size() == 7);
}

TEST_CASE("CASTER-2 timeline: signed cell boundaries retain floor semantics")
{
	ObserverTimeline timeline;
	ObserverEvent damage;
	damage.weight = 0.25f;
	damage.position.x = -300.0f;
	timeline.add(damage);
	damage.position.x = -0.001f;
	timeline.add(damage);
	REQUIRE(timeline.events().size() == 1);
	CHECK(timeline.events()[0].weight == 0.5f);
	damage.position.x = 0.0f;
	timeline.add(damage);
	damage.position.x = 299.999f;
	timeline.add(damage);
	REQUIRE(timeline.events().size() == 2);
	CHECK(timeline.events()[1].weight == 0.5f);
	damage.position.x = 300.0f;
	timeline.add(damage);
	damage.position.x = -300.001f;
	timeline.add(damage);
	damage.position.x = 0.0f;
	damage.position.y = -0.001f;
	timeline.add(damage);
	CHECK(timeline.events().size() == 5);
}

TEST_CASE("save2 retail T1: a skirmish saved at frame F, loaded and played on with the same commands has every later frame's hash of the unsaved game")
{
	OPENBFME_REQUIRE_START(s);
	const int kBefore = 600, kAfter = 600; // two minutes each at 5 frames per second: the armies move and the AI builds and attacks
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 2468u), running, kBefore, kAfter, "t1");
	REQUIRE(rec.savedFrame == rec.startFrame + (UnsignedInt)kBefore);
	CHECK(rec.whole.hashes.size() == (size_t)(kBefore + kAfter));
	ReplayFile savedLog;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::parse(rec.save.log, savedLog, &error), error);
	const unsigned long long commandsBefore = savedLog.commandCount();
	// tolerance: a precondition, not the result: the scripted human issued its selections and moves before F (the count is the game's)
	REQUIRE(commandsBefore >= 40);
	// through the file: written, read back whole, checked as GameWorld::prepare_save_load checks it
	const std::string path = "save2_t1.obfmesave";
	REQUIRE_MESSAGE(rec.save.write(path, &error), error);
	SaveGameFile save;
	REQUIRE_MESSAGE(SaveGameFile::load(path, save, &error), error);
	std::remove(path.c_str());
	const SaveGameCompatibility c = SaveGameCompatibility::check(save.header, running, ProfileIdentity::buildId(), "test", false);
	REQUIRE(c.loadable);
	CHECK_FALSE(c.crossProfile);
	Loaded g;
	CommandLogLoad cl;
	REQUIRE_MESSAGE(catchUp(*s, save, g, cl, error) == CommandLogLoad::State::Ready, error);
	CHECK(g.game->frame() == rec.savedFrame);
	CHECK(cl.startFrame() == rec.startFrame);
	CHECK(cl.hashesCompared() == (unsigned long long)kBefore); // every frame of the log was compared on the way
	CHECK(cl.localInputIgnored() > 0); // tolerance: the player's input during the catch-up was taken and dropped (offered every 20th frame)
	CHECK(g.game->logic().computeStateHash() == save.header.stateHash);
	MESSAGE("catch-up of " << kBefore << " frames: " << cl.elapsedMs() << " ms");
	// handed to the player: a recording that continues the saved log, the same human from here on
	const std::string continued = "save2_t1_continued.replay";
	ReplayWriter writer;
	writer.setKeepCopy(true);
	REQUIRE_MESSAGE(writer.openContinuing(continued, save.log, &error), error);
	LockstepDriver driver(nullptr, &writer);
	RegisterLogicCRCHandler(g.game->dispatch());
	g.game->setFrameDriver(&driver);
	for (int i = 0; i < kAfter; ++i)
	{
		humanCommands(*g.game, g.game->commands());
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	writer.close(g.game->logic().getFrame(), g.game->logic().computeStateHash());
	g.game->setFrameDriver(nullptr);
	ReplayFile after;
	REQUIRE_MESSAGE(ReplayFile::load(continued, after, &error), error);
	std::remove(continued.c_str());
	// every frame after F: the loaded game's hash is the unsaved game's (no threshold: each one, by frame)
	int compared = 0, different = 0;
	UnsignedInt firstDifferent = 0;
	for (UnsignedInt f = rec.savedFrame + 1; f <= rec.savedFrame + (UnsignedInt)kAfter; ++f)
	{
		REQUIRE(after.hashes.count(f) == 1);
		REQUIRE(rec.whole.hashes.count(f) == 1);
		++compared;
		if (after.hashes.at(f) != rec.whole.hashes.at(f))
		{
			firstDifferent = different++ == 0 ? f : firstDifferent;
		}
	}
	CHECK(compared == kAfter);
	CHECK_MESSAGE(different == 0, "first differing frame " << firstDifferent);
	// the loaded game's recording is the whole game: the same commands and hashes as the unsaved game's from frame 0
	CHECK(after.hashes == rec.whole.hashes);
	CHECK(after.commandCount() == rec.whole.commandCount());
	CHECK(after.finalHash == rec.whole.finalHash);
}

TEST_CASE("save2 retail T5: a log that does not replay the same is refused naming its first differing frame; a header that does not match its log too")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 97531u), running, 200, 0, "t5");
	std::string error;
	SUBCASE("a recorded hash changed")
	{
		SaveGameFile bad = rec.save;
		const UnsignedInt frame = rec.startFrame + 120;
		REQUIRE(changeRecordedHash(bad.log, frame, rec.whole.hashes.at(frame)));
		SaveGameFile reread;
		REQUIRE_MESSAGE(SaveGameFile::parse(bad.encode(), reread, &error), error); // re-signed: the container is intact, the game is not
		Loaded g;
		CommandLogLoad cl;
		CHECK(catchUp(*s, reread, g, cl, error) == CommandLogLoad::State::Failed);
		CHECK(error.rfind("the saved game does not replay the same in this build: frame " + std::to_string(frame) + " has state hash ", 0) == 0);
		CHECK(g.game->frame() == frame); // stopped at that frame, nothing after it ran
	}
	SUBCASE("the header's frame or hash is not the log's end")
	{
		SaveGameFile bad = rec.save;
		bad.header.stateHash ^= 1u;
		CommandLogLoad cl;
		CHECK_FALSE(cl.prepare(bad, &error));
		CHECK(error == "the save's command log ends at frame " + std::to_string(rec.savedFrame) + " (hash " + std::to_string(rec.save.header.stateHash)
			+ "), its header says frame " + std::to_string(rec.savedFrame) + " (hash " + std::to_string(bad.header.stateHash) + ")");
		CHECK(cl.state() == CommandLogLoad::State::Failed);
	}
	SUBCASE("a log recorded by another identity than its header's")
	{
		SaveGameFile bad = rec.save;
		bad.header.profile = withProfile(running, "uber");
		CommandLogLoad cl;
		CHECK_FALSE(cl.prepare(bad, &error));
		CHECK(error.rfind("the save's command log was recorded by another profile than its header names: ", 0) == 0);
	}
	SUBCASE("a log cut short")
	{
		SaveGameFile bad = rec.save;
		bad.log.resize(bad.log.size() - 9); // no end record
		CommandLogLoad cl;
		CHECK_FALSE(cl.prepare(bad, &error));
		CHECK(error == "the save's command log has no end record");
		bad.log.resize(bad.log.size() - 3); // inside a record
		CommandLogLoad cl2;
		CHECK_FALSE(cl2.prepare(bad, &error));
		CHECK(error.rfind("the save's command log is damaged: ", 0) == 0);
	}
}

TEST_CASE("save2 retail: a save of the other execution profile loads in single player (Faithful <-> Uber): replayed under its profile, every hash "
		  "compared, then the game goes on under the player's; the switch is kept, so the next save replays the same")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity faithful = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful);
	const ProfileIdentity uber = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Uber);
	REQUIRE(SaveGameFile::executionProfileOf(faithful.text) == "enhanced");
	REQUIRE(SaveGameFile::executionProfileOf(uber.text) == "uber");
	std::string error;
	// lane UBER-2C: the hashes a recording holds of the frames from .. to: every one run under Faithful, every LiveGameFrameDriver::kUberRecordedHashEvery-th
	// under Uber, and a save's frame (save_game records it)
	const auto recorded = [](UnsignedInt from, UnsignedInt to, SimulationProfile p) {
		size_t n = 0;
		for (UnsignedInt f = from; f <= to; ++f)
		{
			n += (p != SimulationProfile::Uber || f % (UnsignedInt)LiveGameFrameDriver::kUberRecordedHashEvery == 0 || f == to) ? 1 : 0;
		}
		return n;
	};
	for (int reverse = 0; reverse < 2; ++reverse)
	{
		// saved under one profile (its game setup and identity), loaded by a player of the other
		const SimulationProfile savedProfile = reverse ? SimulationProfile::Faithful : SimulationProfile::Uber;
		const SimulationProfile ownProfile = reverse ? SimulationProfile::Uber : SimulationProfile::Faithful;
		const ProfileIdentity &saved = reverse ? faithful : uber;
		const ProfileIdentity &running = reverse ? uber : faithful;
		NewGameMessage message = gameMessage(*s, 1357u + (std::uint32_t)reverse);
		message.game.simulationProfile = savedProfile;
		Recorded rec = recordGame(*s, message, saved, 150, 0, reverse ? "faithful" : "uber");
		CHECK(rec.save.header.executionProfile == (reverse ? "enhanced" : "uber"));
		const SaveGameCompatibility c = SaveGameCompatibility::check(rec.save.header, running, ProfileIdentity::buildId(), "test", false);
		REQUIRE(c.loadable);
		REQUIRE(c.crossProfile);
		CHECK_FALSE(SaveGameCompatibility::check(rec.save.header, running, ProfileIdentity::buildId(), "test", true).loadable); // never in multiplayer
		// the catch-up runs under the saved profile, every recorded hash compared
		Loaded g;
		CommandLogLoad cl;
		REQUIRE_MESSAGE(catchUp(*s, rec.save, g, cl, error) == CommandLogLoad::State::Ready, error);
		const size_t firstHashes = recorded(rec.startFrame + 1, rec.savedFrame, savedProfile);
		CHECK(firstHashes == (reverse ? 150u : 31u));
		CHECK(cl.hashesCompared() == firstHashes);
		CHECK(g.game->logic().simulationProfile() == savedProfile);
		CHECK(g.game->logic().computeStateHash() == rec.save.header.stateHash);
		// handed over as GameWorld::save_load_step does (CommandLogLoad::handOver): this player's profile from the saved frame on, the switch recorded in the
		// continued replay itself
		const UnsignedInt switchFrame = g.game->logic().getFrame();
		const std::string continuedPath = std::string("save2_switch_") + (reverse ? "faithful" : "uber") + ".replay";
		ReplayWriter writer;
		REQUIRE_MESSAGE(cl.handOver(*g.game, writer, continuedPath, ownProfile, &error), error);
		CHECK(g.game->logic().simulationProfile() == ownProfile);
		LockstepDriver driver(nullptr, &writer);
		RegisterLogicCRCHandler(g.game->dispatch());
		g.game->setFrameDriver(&driver);
		for (int i = 0; i < 100; ++i)
		{
			humanCommands(*g.game, g.game->commands());
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->drainFrames();
		SaveGameFile second = rec.save;
		second.header.frame = g.game->logic().getFrame();
		second.header.stateHash = g.game->logic().computeStateHash();
		second.header.executionProfile = ownProfile == SimulationProfile::Uber ? "uber" : "enhanced";
		if (!writer.hasHashOf(second.header.frame))
		{
			writer.recordHash(second.header.frame, second.header.stateHash); // lane UBER-2C: as save_game does
		}
		REQUIRE_MESSAGE(writer.logUpTo(second.header.frame, second.header.stateHash, second.log, &error), error);
		writer.close(second.header.frame, second.header.stateHash);
		g.game->setFrameDriver(nullptr);
		// the continued replay file carries the switch (review: not only a later save's header)
		ReplayFile continued;
		REQUIRE_MESSAGE(ReplayFile::load(continuedPath, continued, &error), error);
		std::remove(continuedPath.c_str());
		REQUIRE(continued.profileSwitches.size() == 1u);
		CHECK(continued.profileSwitches[0].first == switchFrame);
		CHECK(continued.profileSwitches[0].second == ownProfile);
		const size_t allHashes = firstHashes + recorded(switchFrame + 1, second.header.frame, ownProfile);
		CHECK(allHashes == (reverse ? 171u : 131u));
		CHECK(continued.hashes.size() == allHashes);
		// the second save is this player's own profile now; it replays under the saved profile to the switch, then under the player's, every hash equal
		CHECK_FALSE(SaveGameCompatibility::check(second.header, running, ProfileIdentity::buildId(), "test", false).crossProfile);
		g.game.reset(); // lane MAPRESET-1 (merge q7): one live game per world (S-3808); the first game is over
		Loaded g2;
		CommandLogLoad cl2;
		REQUIRE_MESSAGE(catchUp(*s, second, g2, cl2, error) == CommandLogLoad::State::Ready, error);
		CHECK(cl2.hashesCompared() == allHashes);
		CHECK(g2.game->logic().simulationProfile() == ownProfile);
		CHECK(g2.game->logic().computeStateHash() == second.header.stateHash);
		// without the switch record the same log does not replay: named at its first differing frame
		SaveGameFile noSwitch = second;
		std::uint8_t rec5[9] = { 5 };
		const std::uint32_t sf = switchFrame, sp = (std::uint32_t)ownProfile;
		std::memcpy(rec5 + 1, &sf, 4);
		std::memcpy(rec5 + 5, &sp, 4);
		size_t at = std::string::npos;
		for (size_t i = 8; i + 9 <= noSwitch.log.size() && at == std::string::npos; ++i)
		{
			at = std::memcmp(noSwitch.log.data() + i, rec5, 9) == 0 ? i : at;
		}
		REQUIRE(at != std::string::npos);
		noSwitch.log.erase(noSwitch.log.begin() + (std::ptrdiff_t)at, noSwitch.log.begin() + (std::ptrdiff_t)(at + 9));
		noSwitch.header.executionProfile = rec.save.header.executionProfile;
		g2.game.reset(); // lane MAPRESET-1 (merge q7): one live game per world (S-3808)
		Loaded g3;
		CommandLogLoad cl3;
		CHECK(catchUp(*s, noSwitch, g3, cl3, error) == CommandLogLoad::State::Failed);
		// lane UBER-2C: an Uber continuation holds every 5th frame's hash, so without its switch the log reads as Faithful frames with hashes missing and is
		// refused before it replays; a Faithful continuation replays and is named at its first differing frame
		const std::string expected = ownProfile == SimulationProfile::Uber
			? "the save's command log has no state hash of frame " + std::to_string(switchFrame + 1)
			: "the saved game does not replay the same in this build: frame " + std::to_string(switchFrame + 1) + " has state hash ";
		CHECK_MESSAGE(error.rfind(expected, 0) == 0, error);
	}
}

namespace
{
// SAVES.md T1's named scenarios, each found in the saved state by name: a unit in an attack state (mid-combat), a structure being built
// (mid-construction), a special ability running (a special power in flight: SpecialPowerUpdateInterface::isActive), a horde moving (a horde mid-march),
// a countdown counter of the script engine still running (a script waiting on a timer)
struct Scenarios
{
	std::set<std::string> combat, construction, power, marching, timers;
	std::string text() const
	{
		std::ostringstream o;
		auto list = [&](const char *n, const std::set<std::string> &v) {
			o << n << " (" << v.size() << "):";
			for (const std::string &x : v)
			{
				o << " " << x;
			}
			o << "; ";
		};
		list("mid-combat", combat);
		list("mid-construction", construction);
		list("special power in flight", power);
		list("horde mid-march", marching);
		list("script timer", timers);
		return o.str();
	}
};

Scenarios probe(LiveGame &game)
{
	Scenarios sc;
	GameLogic &logic = game.logic();
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isEffectivelyDead() || !o->getTemplate())
		{
			continue;
		}
		const std::string name = o->getTemplate()->getName();
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (ai && ai->isAttacking())
		{
			sc.combat.insert(name);
		}
		if (o->isUnderConstruction() && o->getConstructionPercent() > 0.0f && o->getConstructionPercent() < 100.0f)
		{
			sc.construction.insert(name);
		}
		for (const auto &m : o->modules())
		{
			const SpecialPowerUpdateInterface *u = dynamic_cast<const SpecialPowerUpdateInterface *>(m.get());
			if (u && u->isActive())
			{
				sc.power.insert(name);
			}
		}
		if (o->isKindOfName("HORDE") && ai && ai->isMoving())
		{
			sc.marching.insert(name);
		}
	}
	for (const auto &kv : logic.scriptEngine().allCounters())
	{
		if (kv.second.countdown && kv.second.value > 0)
		{
			sc.timers.insert(kv.first.first + "/" + kv.first.second);
		}
	}
	return sc;
}

int templateIndexOf(const starttest::Shared &s, const char *name)
{
	return templateIndex(s.world->playerTemplates(), name);
}

// a human (slot 0, Men) and four Medium AIs (Mordor, Elves, Isengard, Dwarves) on the 8-player Fall Back, every player on a team of its own
NewGameMessage fourAiMessage(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const char *factions[5] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard", "FactionDwarves" };
	for (int i = 0; i < 5; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[(size_t)i];
		sl.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		sl.name = i == 0 ? u"Human" : u"Computer";
		sl.playerTemplate = templateIndexOf(s, factions[i]);
		REQUIRE(sl.playerTemplate != -1);
		sl.startPos = i;
		sl.color = i;
		sl.teamNumber = i;
	}
	return m;
}

std::string readText(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string reportField(const std::string &report, const std::string &key)
{
	std::istringstream in(report);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.rfind(key + " ", 0) == 0)
		{
			return line.substr(key.size() + 1);
		}
	}
	return "";
}

#ifdef OPENBFME_PEER_EXE
int runPeer(const std::string &args)
{
	const std::string cmd = std::string("\"") + OPENBFME_PEER_EXE + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL")
		+ "\" " + args;
#ifdef _WIN32
	return std::system(("\"" + cmd + "\"").c_str());
#else
	const int rc = std::system(cmd.c_str());
	return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}
#endif
} // namespace

TEST_CASE("save2 retail T1 (SAVES.md): a 4-AI skirmish saved with each named scenario in flight, every save loaded in a fresh process and played on "
		  "past F + 3000 with the uninterrupted game's commands: every frame's hash is the unsaved game's (lane SAVE-4e: from the save's snapshot, no frame "
		  "of its log run; the 11-minute save also caught up from its log, its snapshot compared, both load times measured; T3 with a Windows peer)")
{
	OPENBFME_REQUIRE_START(s);
#ifndef OPENBFME_PEER_EXE
	MESSAGE("SKIP save2 retail T1 (fresh process): the openbfme_peer executable is not built");
#else
	std::string error;
	const std::string profilePath = "save2_t1_profile.txt", fullPath = "save2_t1_full.replay";
	// the peer's identity (the archives it mounts, hashed): the game is recorded under it, so the saves are this build's in the fresh process too
	REQUIRE(runPeer("--profile-text " + profilePath) == 0);
	const ProfileIdentity profile = ProfileIdentity::fromText(readText(profilePath));
	std::remove(profilePath.c_str());
	REQUIRE(profile.text.rfind("openbfme-profile 1\n", 0) == 0);
	// merge queue q24: seed 8644 (8642 before). Since OFFMAP-1 (RotWK's idle state RW 0x745A64: no ZH frame <= 1 snap) seed 8642's game had no special power
	// in flight in its 20 minutes (the Dwarves alive with 100+ objects; measured); seeds 8644 and 8645 hold all four scenarios (a DwarvenGuardianHorde's power
	// at frame 1632 / 1887) and pass. The scenarios are the test's subject, the seed is not
	const NewGameMessage message = fourAiMessage(*s, 8644u);
	struct Taken
	{
		std::string scenario, path, found;
		UnsignedInt frame = 0;
		bool eleven = false; ///< lane SAVE-4e: the 11-minute save (the load time measurement, the verify mode, T3)
	};
	std::vector<Taken> saves;
	const char *const names[4] = { "mid-combat", "mid-construction", "special power in flight", "horde mid-march" };
	UnsignedInt startFrame = 0, lastFrame = 0;
	std::uint32_t finalHash = 0;
	{
		Loaded g;
		load(*s, message, g);
		ReplayWriter writer;
		writer.setKeepCopy(true);
		ReplayHeader h;
		h.game = message;
		h.recordingSlot = 0;
		h.profile = profile;
		REQUIRE_MESSAGE(writer.open(fullPath, h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		RegisterLogicCRCHandler(g.game->dispatch());
		startFrame = g.game->frame();
		g.game->setFrameDriver(&driver);
		// from two minutes on, each scenario is saved at the first frame it is in flight (at most 30 minutes of game time; review of #74: since the destination
		// adjustment spirals around the cell, RW 0x6FA2A4, the first special power in flight comes at frame 7405, a DwarvenGuardianHorde's, past 20 minutes)
		bool seen[4] = { false, false, false, false };
		bool elevenTaken = false;
		// lane SAVE-4e: the save as GameWorld::save_game writes it: the log to F and the snapshot at F
		auto takeSave = [&](Taken &t) {
			SaveGameFile save;
			const std::uint32_t hash = g.game->logic().computeStateHash();
			REQUIRE_MESSAGE(writer.logUpTo(t.frame, hash, save.log, &error), error);
			REQUIRE_MESSAGE(g.game->logic().saveSnapshot(save.snapshot, &error), error);
			SaveGameHeader &sh = save.header;
			sh.buildVersion = BuildVersion::kVersion;
			sh.engineId = ProfileIdentity::buildId();
			sh.profile = profile;
			sh.executionProfile = "enhanced";
			sh.gameKind = 2;
			sh.frame = t.frame;
			sh.stateHash = hash;
			sh.description = t.scenario;
			sh.mapPath = message.game.mapName;
			REQUIRE_MESSAGE(save.write(t.path, &error), error);
		};
		for (int i = 0; i < 9000 && (saves.size() < 5 || !elevenTaken); ++i)
		{
			humanCommands(*g.game, g.game->commands());
			REQUIRE(g.game->advance(0.2) == 1);
			if (i + 1 == 3300)
			{
				// eleven minutes of game time (the owner's catch-up was "2:51 of 11:00")
				g.game->drainFrames();
				Taken t;
				t.scenario = "eleven minutes";
				t.frame = g.game->logic().getFrame();
				t.path = "save2_t1_eleven.obfmesave";
				t.eleven = true;
				takeSave(t);
				saves.push_back(t);
				elevenTaken = true;
			}
			if (i < 600)
			{
				continue;
			}
			g.game->drainFrames();
			const Scenarios sc = probe(*g.game);
			const std::set<std::string> *sets[4] = { &sc.combat, &sc.construction, &sc.power, &sc.marching };
			for (int k = 0; k < 4; ++k)
			{
				if (seen[k] || sets[k]->empty())
				{
					continue;
				}
				seen[k] = true;
				Taken t;
				t.scenario = names[k];
				t.frame = g.game->logic().getFrame();
				for (const std::string &n : *sets[k])
				{
					t.found += " " + n;
				}
				t.path = "save2_t1_" + std::to_string(k) + ".obfmesave";
				takeSave(t);
				MESSAGE(t.scenario << " at frame " << t.frame << ":" << t.found);
				saves.push_back(t);
			}
		}
		for (int k = 0; k < 4; ++k)
		{
			CHECK_MESSAGE(seen[k], names[k] << " never happened in 30 minutes of the game");
		}
		REQUIRE(elevenTaken);
		for (int i = 0; i < 3000; ++i) // F + 3000 for the last save, more for the earlier ones
		{
			humanCommands(*g.game, g.game->commands());
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->drainFrames();
		lastFrame = g.game->logic().getFrame();
		finalHash = g.game->logic().computeStateHash();
		writer.close(lastFrame, finalHash);
		CHECK(writer.errors().empty());
		g.game->setFrameDriver(nullptr);
	}
	// each save in a fresh process: loaded from its snapshot (lane SAVE-4e: no frame of its log runs), then the uninterrupted game's commands to its end,
	// every hash compared; the 11-minute save also caught up from its log (--verify-log: an old save's path, the snapshot compared at F)
	char want[64];
	std::snprintf(want, sizeof(want), "0x%08X recorded 0x%08X", finalHash, finalHash);
	auto check = [&](const Taken &t, const std::string &report, int rc, bool snapshot) {
		INFO(t.scenario << " (frame " << t.frame << "), " << (snapshot ? "from its snapshot" : "caught up from its log") << ", fresh process report:\n" << report);
		CHECK(rc == 0);
		CHECK(reportField(report, "save_frame") == std::to_string(t.frame));
		CHECK(reportField(report, "load_mode") == (snapshot ? "snapshot" : "catch_up"));
		CHECK(reportField(report, "catch_up_hashes") == (snapshot ? std::string("0") : std::to_string(t.frame - startFrame)));
		CHECK(reportField(report, "hashes_compared") == std::to_string(lastFrame - t.frame));
		CHECK(reportField(report, "mismatches") == "0");
		CHECK(reportField(report, "final_hash") == want);
	};
	std::string elevenPath;
	for (const Taken &t : saves)
	{
		const std::string reportPath = t.path + ".report";
		int rc = runPeer("--load-save \"" + t.path + "\" --replay \"" + fullPath + "\" --report \"" + reportPath + "\"");
		const std::string report = readText(reportPath);
		check(t, report, rc, true);
		std::remove(reportPath.c_str());
		if (!t.eleven)
		{
			std::remove(t.path.c_str());
			continue;
		}
		elevenPath = t.path;
		rc = runPeer("--load-save \"" + t.path + "\" --replay \"" + fullPath + "\" --report \"" + reportPath + "\" --verify-log");
		const std::string verified = readText(reportPath);
		check(t, verified, rc, false);
		std::remove(reportPath.c_str());
		// the measurement (SAVES.md section 8): the same 11-minute save, from its snapshot and caught up from its log, in a fresh process each
		MESSAGE("SAVE-4e load time of the 11-minute save (frame " << t.frame << ", " << reportField(report, "snapshot_bytes")
										<< " snapshot bytes): from the snapshot " << reportField(report, "load_ms") << " ms, caught up from the log "
										<< reportField(verified, "load_ms") << " ms");
	}
	// T3 (SAVES.md section 6): a Windows peer (OPENBFME_WINDOWS_PEER: the command that runs openbfme_peer.exe, e.g. under Wine; its install paths in
	// OPENBFME_WINDOWS_ROTWK / OPENBFME_WINDOWS_BFME2) catches the Linux save up from its log and saves the same snapshot bytes at F (--verify-log),
	// then loads the Linux snapshot and plays on with every hash equal
	const char *windowsPeer = std::getenv("OPENBFME_WINDOWS_PEER");
	if (!windowsPeer || !*windowsPeer)
	{
		MESSAGE("SKIP save2 retail T3 (the snapshot bytes on Windows): OPENBFME_WINDOWS_PEER is not set");
	}
	else
	{
		const char *wr = std::getenv("OPENBFME_WINDOWS_ROTWK"), *wb = std::getenv("OPENBFME_WINDOWS_BFME2");
		const std::string install = std::string(" --rotwk \"") + (wr ? wr : std::getenv("ROTWK_INSTALL")) + "\" --bfme2 \"" + (wb ? wb : std::getenv("BFME2_INSTALL")) + "\" ";
		const Taken *eleven = nullptr;
		for (const Taken &t : saves)
		{
			eleven = t.eleven ? &t : eleven;
		}
		REQUIRE(eleven);
		for (bool snapshot : { false, true })
		{
			const std::string reportPath = "save2_t1_windows.report";
			const std::string cmd = std::string(windowsPeer) + install + "--load-save " + elevenPath + " --replay " + fullPath + " --report " + reportPath
				+ (snapshot ? "" : " --verify-log");
			const int raw = std::system(cmd.c_str());
#ifdef _WIN32
			const int rc = raw;
#else
			const int rc = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
			const std::string report = readText(reportPath);
			MESSAGE("T3, the Windows peer " << (snapshot ? "from the Linux snapshot" : "caught up, its snapshot compared with Linux's") << ":\n" << report);
			check(*eleven, report, rc, snapshot);
			std::remove(reportPath.c_str());
		}
	}
	std::remove(elevenPath.c_str());
	std::remove(fullPath.c_str());
#endif
}

TEST_CASE("save2 retail: the loaded game's recording must open, or the load fails named (review: it went on unrecorded); a log keeping only its final "
		  "hash is refused")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 24680u), running, 20, 0, "handover");
	std::string error;
	{
		Loaded g;
		CommandLogLoad cl;
		REQUIRE_MESSAGE(catchUp(*s, rec.save, g, cl, error) == CommandLogLoad::State::Ready, error);
		ReplayWriter writer;
		CHECK_FALSE(cl.handOver(*g.game, writer, "save2-no-such-dir/sub/x.replay", SimulationProfile::Faithful, &error));
		CHECK(error == "the loaded game's recording could not be opened: cannot write the replay save2-no-such-dir/sub/x.replay");
		CHECK(cl.state() == CommandLogLoad::State::Failed);
		CHECK_FALSE(writer.isOpen());
	}
	// a full disk (review r2: /dev/full took the 14 KB continued log without an open error and the load went on "ready"): the flush shows it, named
	if (std::ifstream("/dev/full").good())
	{
		Loaded g;
		CommandLogLoad cl;
		REQUIRE_MESSAGE(catchUp(*s, rec.save, g, cl, error) == CommandLogLoad::State::Ready, error);
		ReplayWriter writer;
		CHECK_FALSE(cl.handOver(*g.game, writer, "/dev/full", SimulationProfile::Faithful, &error));
		CHECK(error == "the loaded game's recording could not be written: writing the replay failed (disk full or the file went away): the recording is "
					   "incomplete");
		CHECK(cl.state() == CommandLogLoad::State::Failed);
		// the same with a profile switch to write after the log
		g.game.reset(); // lane MAPRESET-1 (merge q7): one live game per world (S-3808)
		Loaded g2;
		CommandLogLoad cl2;
		REQUIRE_MESSAGE(catchUp(*s, rec.save, g2, cl2, error) == CommandLogLoad::State::Ready, error);
		ReplayWriter writer2;
		CHECK_FALSE(cl2.handOver(*g2.game, writer2, "/dev/full", SimulationProfile::Uber, &error));
		CHECK(cl2.state() == CommandLogLoad::State::Failed);
		CHECK(g2.game->logic().simulationProfile() == SimulationProfile::Faithful); // not switched
	}
	// the same 20-frame game's log with its final hash only (every other hash record left out): refused naming the first missing frame
	ReplayFile full;
	REQUIRE_MESSAGE(ReplayFile::parse(rec.save.log, full, &error), error);
	ReplayWriter w;
	w.setKeepCopy(true);
	REQUIRE_MESSAGE(w.open(std::string(), full.header, &error), error);
	for (const auto &kv : full.frames)
	{
		CommandList list;
		for (const GameMessage &m : kv.second)
		{
			list.append(m);
		}
		w.recordFrame(kv.first, list);
	}
	w.recordHash(rec.savedFrame, rec.save.header.stateHash);
	SaveGameFile onlyFinal = rec.save;
	REQUIRE_MESSAGE(w.logUpTo(rec.savedFrame, rec.save.header.stateHash, onlyFinal.log, &error), error);
	Loaded g;
	CommandLogLoad cl;
	CHECK(catchUp(*s, onlyFinal, g, cl, error) == CommandLogLoad::State::Failed);
	CHECK(error == "the save's command log has no state hash of frame " + std::to_string(rec.startFrame + 1));
	CHECK(cl.hashesCompared() == 0u);
}

TEST_CASE("save2 retail T1, a campaign mission: recorded in memory, saved at frame F, started again from its campaign and caught up, every later hash equal")
{
	OPENBFME_REQUIRE_START(s);
	// the mission's script countdowns after PLAY-4's movement and guard changes: the intro cine's last ones run until about frame 200 and no
	// countdown runs after frame 300 under these commands, so the save is searched from frame 150 on (the timer found is printed)
	const int kBefore = 150, kAfter = 300;
	const std::string map = "map ang angmar";
	const ProfileIdentity profile = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	std::string error;
	// the unsaved mission, recorded in memory as GameWorld::start_campaign_mission's record_memory does (no replay file)
	SaveGameFile save;
	ReplayFile whole;
	UnsignedInt startFrame = 0, savedFrame = 0;
	{
		Loaded g;
		loadMission(*s, map, 2, g);
		ReplayWriter writer;
		writer.setKeepCopy(true);
		ReplayHeader h;
		h.game.game.mapName = map;
		h.profile = profile;
		REQUIRE_MESSAGE(writer.open(std::string(), h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		RegisterLogicCRCHandler(g.game->dispatch());
		startFrame = g.game->frame();
		g.game->setFrameDriver(&driver);
		// SAVES.md T1's "a script waiting on a timer": the mission is saved at the first frame from start + kBefore on where one of its script countdown
		// timers is running (named in the output)
		bool timer = false;
		for (int i = 0; i < kBefore + 3000 && !timer; ++i)
		{
			humanCommands(*g.game, g.game->commands(), false);
			REQUIRE(g.game->advance(0.2) == 1);
			if (i + 1 >= kBefore)
			{
				g.game->drainFrames();
				timer = !probe(*g.game).timers.empty();
			}
		}
		REQUIRE_MESSAGE(timer, "no script countdown timer ran in the mission");
		MESSAGE("the mission at its save: " << probe(*g.game).text());
		g.game->drainFrames();
		savedFrame = g.game->logic().getFrame();
		const std::uint32_t hash = g.game->logic().computeStateHash();
		REQUIRE_MESSAGE(writer.logUpTo(savedFrame, hash, save.log, &error), error);
		SaveGameHeader &sh = save.header;
		sh.engineId = ProfileIdentity::buildId();
		sh.profile = profile;
		sh.gameKind = 1;
		sh.executionProfile = "enhanced";
		sh.campaign = "ANGMAR_CAMPAIGN";
		sh.campaignMission = 0;
		sh.campaignDifficulty = 2;
		sh.frame = savedFrame;
		sh.stateHash = hash;
		sh.mapPath = map;
		for (int i = 0; i < kAfter; ++i)
		{
			humanCommands(*g.game, g.game->commands(), false);
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->drainFrames();
		std::vector<std::uint8_t> all;
		REQUIRE_MESSAGE(writer.logUpTo(g.game->logic().getFrame(), g.game->logic().computeStateHash(), all, &error), error);
		REQUIRE_MESSAGE(ReplayFile::parse(all, whole, &error), error);
		writer.close(g.game->logic().getFrame(), g.game->logic().computeStateHash());
		g.game->setFrameDriver(nullptr);
	}
	REQUIRE(savedFrame >= startFrame + (UnsignedInt)kBefore); // tolerance: the timer is searched from start + kBefore on (the frame is printed)
	SaveGameFile back;
	REQUIRE_MESSAGE(SaveGameFile::parse(save.encode(), back, &error), error);
	CHECK(back.header.campaign == "ANGMAR_CAMPAIGN");
	// loaded: the mission started again from its campaign start (not from the log's NewGameMessage), caught up to F, then played on
	Loaded g;
	loadMission(*s, back.header.mapPath, back.header.campaignDifficulty, g);
	CommandLogLoad cl;
	REQUIRE_MESSAGE(cl.prepare(back, &error), error);
	REQUIRE_MESSAGE(cl.begin(*g.game, &error), error);
	while (cl.step(*g.game, 20) == CommandLogLoad::State::CatchingUp)
	{
	}
	REQUIRE_MESSAGE(cl.state() == CommandLogLoad::State::Ready, cl.failure());
	CHECK(cl.hashesCompared() == (unsigned long long)(savedFrame - startFrame));
	ReplayWriter writer;
	writer.setKeepCopy(true);
	REQUIRE_MESSAGE(writer.openContinuing(std::string(), back.log, &error), error);
	LockstepDriver driver(nullptr, &writer);
	RegisterLogicCRCHandler(g.game->dispatch());
	g.game->setFrameDriver(&driver);
	for (int i = 0; i < kAfter; ++i)
	{
		humanCommands(*g.game, g.game->commands(), false);
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	std::vector<std::uint8_t> continued;
	REQUIRE_MESSAGE(writer.logUpTo(g.game->logic().getFrame(), g.game->logic().computeStateHash(), continued, &error), error);
	ReplayFile after;
	REQUIRE_MESSAGE(ReplayFile::parse(continued, after, &error), error);
	g.game->setFrameDriver(nullptr);
	int different = 0;
	UnsignedInt firstDifferent = 0;
	for (UnsignedInt f = savedFrame + 1; f <= savedFrame + (UnsignedInt)kAfter; ++f)
	{
		REQUIRE(after.hashes.count(f) == 1);
		if (after.hashes.at(f) != whole.hashes.at(f))
		{
			firstDifferent = different++ == 0 ? f : firstDifferent;
		}
	}
	CHECK_MESSAGE(different == 0, "first differing frame " << firstDifferent);
	CHECK(after.hashes == whole.hashes);
	CHECK(after.commandCount() == whole.commandCount());
}

namespace
{
// lane CAMPSAVE-1: a campaign mission (by its index in ANGMAR_CAMPAIGN) recorded in memory as GameWorld::start_campaign_mission records it, saved at frame
// start + before (a version 2 header: the save point 0, no campaign flags), the file read back, the mission started again from its campaign and caught
// up, then played on `after` frames with the same commands: every later frame's hash and the command count equal the uninterrupted mission's
void campaignMissionRoundTrip(starttest::Shared &s, int missionIndex, int difficulty, int before, int after)
{
	// copies: a game's load may rebuild the world's stores the campaign list lives in (a pointer into it does not outlive a load)
	std::string map, campaignName;
	{
		const LinearCampaign *campaign = s.world->linearCampaigns().find("ANGMAR_CAMPAIGN");
		REQUIRE(campaign != nullptr);
		REQUIRE(missionIndex < (int)campaign->missions.size()); // tolerance: a guard before the index is used (the caller picks it from the list)
		map = campaign->missions[(size_t)missionIndex].map;
		campaignName = campaign->name;
	}
	INFO("mission " << missionIndex << " " << map);
	const ProfileIdentity profile = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	std::string error;
	SaveGameFile save;
	ReplayFile whole;
	UnsignedInt startFrame = 0, savedFrame = 0;
	{
		Loaded g;
		loadMission(s, map, difficulty, g);
		ReplayWriter writer;
		writer.setKeepCopy(true);
		ReplayHeader h;
		h.game.game.mapName = map;
		h.profile = profile;
		REQUIRE_MESSAGE(writer.open(std::string(), h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		RegisterLogicCRCHandler(g.game->dispatch());
		startFrame = g.game->frame();
		g.game->setFrameDriver(&driver);
		for (int i = 0; i < before; ++i)
		{
			humanCommands(*g.game, g.game->commands(), false);
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->drainFrames();
		MESSAGE("mission " << map << " at its save: " << probe(*g.game).text());
		savedFrame = g.game->logic().getFrame();
		const std::uint32_t hash = g.game->logic().computeStateHash();
		REQUIRE_MESSAGE(writer.logUpTo(savedFrame, hash, save.log, &error), error);
		SaveGameHeader &sh = save.header;
		sh.engineId = ProfileIdentity::buildId();
		sh.profile = profile;
		sh.gameKind = 1;
		sh.executionProfile = "enhanced";
		sh.campaign = campaignName;
		sh.campaignMission = missionIndex;
		sh.campaignDifficulty = difficulty;
		sh.frame = savedFrame;
		sh.stateHash = hash;
		sh.mapPath = map;
		for (int i = 0; i < after; ++i)
		{
			humanCommands(*g.game, g.game->commands(), false);
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->drainFrames();
		std::vector<std::uint8_t> all;
		REQUIRE_MESSAGE(writer.logUpTo(g.game->logic().getFrame(), g.game->logic().computeStateHash(), all, &error), error);
		REQUIRE_MESSAGE(ReplayFile::parse(all, whole, &error), error);
		writer.close(g.game->logic().getFrame(), g.game->logic().computeStateHash());
		g.game->setFrameDriver(nullptr);
	}
	REQUIRE(savedFrame == startFrame + (UnsignedInt)before);
	SaveGameFile back;
	REQUIRE_MESSAGE(SaveGameFile::parse(save.encode(), back, &error), error);
	CHECK(back.header.savePoint == 0);
	CHECK(back.header.campaignMission == missionIndex);
	CHECK(back.header.campaignDifficulty == difficulty);
	// the mission started again from its campaign (start_campaign_mission: its map, the difficulty), caught up to F, then played on
	const LinearCampaign *campaign = s.world->linearCampaigns().find(back.header.campaign);
	REQUIRE(campaign != nullptr);
	const std::string againMap = campaign->missions[(size_t)back.header.campaignMission].map;
	CHECK(againMap == map);
	Loaded g;
	loadMission(s, againMap, back.header.campaignDifficulty, g);
	CommandLogLoad cl;
	REQUIRE_MESSAGE(cl.prepare(back, &error), error);
	REQUIRE_MESSAGE(cl.begin(*g.game, &error), error);
	while (cl.step(*g.game, 20) == CommandLogLoad::State::CatchingUp)
	{
	}
	REQUIRE_MESSAGE(cl.state() == CommandLogLoad::State::Ready, cl.failure());
	CHECK(cl.hashesCompared() == (unsigned long long)(savedFrame - startFrame));
	CHECK(g.game->logic().computeStateHash() == back.header.stateHash);
	ReplayWriter writer;
	writer.setKeepCopy(true);
	REQUIRE_MESSAGE(writer.openContinuing(std::string(), back.log, &error), error);
	LockstepDriver driver(nullptr, &writer);
	RegisterLogicCRCHandler(g.game->dispatch());
	g.game->setFrameDriver(&driver);
	for (int i = 0; i < after; ++i)
	{
		humanCommands(*g.game, g.game->commands(), false);
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	std::vector<std::uint8_t> continued;
	REQUIRE_MESSAGE(writer.logUpTo(g.game->logic().getFrame(), g.game->logic().computeStateHash(), continued, &error), error);
	ReplayFile resumed;
	REQUIRE_MESSAGE(ReplayFile::parse(continued, resumed, &error), error);
	g.game->setFrameDriver(nullptr);
	int different = 0;
	UnsignedInt firstDifferent = 0;
	for (UnsignedInt f = savedFrame + 1; f <= savedFrame + (UnsignedInt)after; ++f)
	{
		REQUIRE(resumed.hashes.count(f) == 1);
		if (resumed.hashes.at(f) != whole.hashes.at(f))
		{
			firstDifferent = different++ == 0 ? f : firstDifferent;
		}
	}
	CHECK_MESSAGE(different == 0, "first differing frame " << firstDifferent);
	CHECK(resumed.hashes == whole.hashes);
	CHECK(resumed.commandCount() == whole.commandCount());
}
} // namespace

TEST_CASE("campsave1 retail: three more ANGMAR_CAMPAIGN missions (Conquest of Rhudaur, Amon Sul, Carn Dum) saved mid-mission, started again from "
		  "their campaign and caught up: every later frame's hash equals the uninterrupted mission's")
{
	OPENBFME_REQUIRE_START(s);
	std::vector<int> missions;
	{
		const LinearCampaign *campaign = s->world->linearCampaigns().find("ANGMAR_CAMPAIGN");
		REQUIRE(campaign != nullptr);
		for (int i = 0; i < (int)campaign->missions.size(); ++i)
		{
			const std::string &name = campaign->missions[(size_t)i].name;
			if (name == "Rhudaur" || name == "Amonsul" || name == "Carndum")
			{
				missions.push_back(i);
			}
		}
	}
	REQUIRE(missions.size() == 3);
	for (int i : missions)
	{
		campaignMissionRoundTrip(*s, i, i % 3, 200, 300);
	}
}

TEST_CASE("save2 retail T1 (SAVES.md), a script waiting on a timer: a campaign mission saved with a script countdown timer running, loaded in a fresh "
		  "process from its campaign start and played on to F + 3000 with the uninterrupted game's commands: every frame's hash equal (lane SAVE-4e: from "
		  "its snapshot, and caught up from its log with the snapshot compared)")
{
	OPENBFME_REQUIRE_START(s);
#ifndef OPENBFME_PEER_EXE
	MESSAGE("SKIP save2 retail T1 (a mission, fresh process): the openbfme_peer executable is not built");
#else
	std::string error;
	const std::string profilePath = "save2_t1m_profile.txt", fullPath = "save2_t1m_full.replay", savePath = "save2_t1m.obfmesave",
					  reportPath = "save2_t1m.report";
	REQUIRE(runPeer("--profile-text " + profilePath) == 0);
	const ProfileIdentity profile = ProfileIdentity::fromText(readText(profilePath));
	std::remove(profilePath.c_str());
	const std::string map = "map ang angmar";
	UnsignedInt startFrame = 0, savedFrame = 0, lastFrame = 0;
	std::uint32_t finalHash = 0;
	{
		Loaded g;
		loadMission(*s, map, 2, g);
		ReplayWriter writer;
		writer.setKeepCopy(true);
		ReplayHeader h;
		h.game.game.mapName = map; // as GameWorld::start_campaign_mission records a mission (its map directory)
		h.profile = profile;
		REQUIRE_MESSAGE(writer.open(fullPath, h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		RegisterLogicCRCHandler(g.game->dispatch());
		startFrame = g.game->frame();
		g.game->setFrameDriver(&driver);
		Scenarios sc;
		for (int i = 0; i < 3300 && sc.timers.empty(); ++i)
		{
			humanCommands(*g.game, g.game->commands(), false);
			REQUIRE(g.game->advance(0.2) == 1);
			if (i + 1 >= 150)
			{
				g.game->drainFrames();
				sc = probe(*g.game);
			}
		}
		REQUIRE_MESSAGE(!sc.timers.empty(), "no script countdown timer ran in the mission");
		MESSAGE("the mission at its save: " << sc.text());
		savedFrame = g.game->logic().getFrame();
		const std::uint32_t hash = g.game->logic().computeStateHash();
		SaveGameFile save;
		REQUIRE_MESSAGE(writer.logUpTo(savedFrame, hash, save.log, &error), error);
		REQUIRE_MESSAGE(g.game->logic().saveSnapshot(save.snapshot, &error), error); // lane SAVE-4e
		SaveGameHeader &sh = save.header;
		sh.buildVersion = BuildVersion::kVersion;
		sh.engineId = ProfileIdentity::buildId();
		sh.profile = profile;
		sh.executionProfile = "enhanced";
		sh.gameKind = 1;
		sh.campaign = "ANGMAR_CAMPAIGN";
		sh.campaignMission = 0;
		sh.campaignDifficulty = 2;
		sh.frame = savedFrame;
		sh.stateHash = hash;
		sh.mapPath = "maps/" + map + "/" + map + ".map";
		REQUIRE_MESSAGE(save.write(savePath, &error), error);
		for (int i = 0; i < 3000; ++i)
		{
			humanCommands(*g.game, g.game->commands(), false);
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->drainFrames();
		lastFrame = g.game->logic().getFrame();
		finalHash = g.game->logic().computeStateHash();
		writer.close(lastFrame, finalHash);
		g.game->setFrameDriver(nullptr);
	}
	char want[64];
	std::snprintf(want, sizeof(want), "0x%08X recorded 0x%08X", finalHash, finalHash);
	for (bool snapshot : { true, false })
	{
		const int rc = runPeer("--load-save \"" + savePath + "\" --replay \"" + fullPath + "\" --report \"" + reportPath + "\"" + (snapshot ? "" : " --verify-log"));
		const std::string report = readText(reportPath);
		MESSAGE("fresh process report (" << (snapshot ? "from the snapshot" : "caught up, the snapshot compared") << "):\n" << report);
		CHECK(rc == 0);
		CHECK(reportField(report, "save_frame") == std::to_string(savedFrame));
		CHECK(reportField(report, "load_mode") == (snapshot ? "snapshot" : "catch_up"));
		CHECK(reportField(report, "catch_up_hashes") == (snapshot ? std::string("0") : std::to_string(savedFrame - startFrame)));
		CHECK(reportField(report, "hashes_compared") == "3000");
		CHECK(reportField(report, "mismatches") == "0");
		CHECK(reportField(report, "final_hash") == want);
	}
	for (const std::string &p : { savePath, fullPath, reportPath })
	{
		std::remove(p.c_str());
	}
	(void)lastFrame;
#endif
}

// ---- lane SAVE-4e: saves with a snapshot (docs/SAVES.md section 2, case 1) ------------------------------------------------------------------------------

namespace
{
// the loaded game played on `frames` frames with the scripted human, recorded as handOver records it: its hashes after F
std::map<UnsignedInt, std::uint32_t> playOn(Loaded &g, const std::vector<std::uint8_t> &log, int frames)
{
	std::string error;
	ReplayWriter writer;
	writer.setKeepCopy(true);
	REQUIRE_MESSAGE(writer.openContinuing(std::string(), log, &error), error);
	LockstepDriver driver(nullptr, &writer);
	RegisterLogicCRCHandler(g.game->dispatch());
	g.game->setFrameDriver(&driver);
	for (int i = 0; i < frames; ++i)
	{
		humanCommands(*g.game, g.game->commands());
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	std::vector<std::uint8_t> all;
	REQUIRE_MESSAGE(writer.logUpTo(g.game->logic().getFrame(), g.game->logic().computeStateHash(), all, &error), error);
	g.game->setFrameDriver(nullptr);
	ReplayFile after;
	REQUIRE_MESSAGE(ReplayFile::parse(all, after, &error), error);
	return after.hashes;
}

// the frames after `from` whose hash differs between the two (each one compared, by frame)
std::string differences(const std::map<UnsignedInt, std::uint32_t> &a, const std::map<UnsignedInt, std::uint32_t> &b, UnsignedInt from, UnsignedInt to)
{
	std::ostringstream o;
	for (UnsignedInt f = from + 1; f <= to; ++f)
	{
		if (!a.count(f) || !b.count(f))
		{
			o << " frame " << f << " missing;";
		}
		else if (a.at(f) != b.at(f))
		{
			o << " frame " << f << ";";
		}
	}
	return o.str();
}

// the state hash sections and objects that differ between two games (the diagnosis of a load that does not give the saved state back)
std::string stateDifference(const GameLogic &a, const GameLogic &b)
{
	std::vector<GameLogic::StateHashSection> sa, sb;
	std::vector<GameLogic::ObjectStateHash> oa, ob;
	a.computeStateHashBreakdown(sa, &oa);
	b.computeStateHashBreakdown(sb, &ob);
	std::ostringstream o;
	for (size_t i = 0; i < sa.size() || i < sb.size(); ++i)
	{
		if (i >= sa.size() || i >= sb.size() || sa[i].name != sb[i].name || sa[i].value != sb[i].value)
		{
			o << " section '" << (i < sa.size() ? sa[i].name : std::string("-")) << "';";
		}
	}
	int shown = 0;
	for (size_t i = 0; i < oa.size() && i < ob.size() && shown < 8; ++i)
	{
		if (oa[i].id != ob[i].id || oa[i].value != ob[i].value)
		{
			o << " object " << oa[i].id << " " << oa[i].templateName << " / " << ob[i].id << " " << ob[i].templateName << ";";
			++shown;
		}
	}
	if (oa.size() != ob.size())
	{
		o << " " << oa.size() << " objects / " << ob.size() << ";";
	}
	return o.str();
}

// every drawable shows its object's model conditions (the loaded state reached the client)
std::string drawablesOutOfStep(LiveGame &game)
{
	std::ostringstream o;
	size_t objects = 0;
	for (Object *obj = game.logic().getFirstObject(); obj; obj = obj->getNextObject())
	{
		++objects;
		const Drawable *d = game.drawables().findByObject(obj->getID());
		if (!d)
		{
			o << " no drawable for " << obj->getID() << ";";
			continue;
		}
		for (int bit = 0; bit < 19 * 32; ++bit)
		{
			if (d->getModelConditionFlags().test(bit) != obj->testModelCondition(bit))
			{
				o << " " << obj->getTemplate()->getName() << " " << obj->getID() << " bit " << bit << ";";
				break;
			}
		}
	}
	if (game.drawables().liveCount() != objects)
	{
		o << " " << game.drawables().liveCount() << " drawables for " << objects << " objects;";
	}
	return o.str();
}
} // namespace

TEST_CASE("save4e retail: a save with a snapshot loads in one step into a new game (no frame of its log runs, the frame and state hash are the header's), "
		  "every drawable shows its loaded object's model conditions, and the game played on has every later frame's hash of the unsaved game")
{
	OPENBFME_REQUIRE_START(s);
	const int kBefore = 600, kAfter = 600;
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 4501u), running, kBefore, kAfter, "s4e", true);
	REQUIRE(!rec.save.snapshot.empty());
	std::string error;
	SaveGameFile save;
	REQUIRE_MESSAGE(SaveGameFile::parse(rec.save.encode(), save, &error), error);
	CHECK(save.snapshot == rec.save.snapshot);
	{
		Loaded g; // one live game per world (S-3808): this one goes before the verify mode's
		CommandLogLoad cl;
		REQUIRE_MESSAGE(cl.prepare(save, &error), error);
		CHECK(cl.mode() == CommandLogLoad::Mode::Snapshot);
		load(*s, cl.log().header.game, g);
		const UnsignedInt started = g.game->frame();
		CHECK(started == rec.startFrame);
		REQUIRE_MESSAGE(cl.begin(*g.game, &error), error);
		CHECK(cl.state() == CommandLogLoad::State::Ready); // at once: nothing to step
		CHECK(cl.hashesCompared() == 0);
		CHECK(g.game->frame() == rec.savedFrame);
		CHECK(g.game->logic().getFrame() == rec.savedFrame);
		CHECK(g.game->logic().computeStateHash() == save.header.stateHash);
		MESSAGE("snapshot of " << save.snapshot.size() << " bytes loaded in " << cl.snapshotLoadMs() << " ms");
		const std::string outOfStep = drawablesOutOfStep(*g.game);
		if (!outOfStep.empty())
		{
			MESSAGE("drawables out of step:" << outOfStep);
		}
		CHECK(outOfStep.empty());
		// the player's game: the recording continues the saved log; every later frame equals the unsaved game's
		const std::map<UnsignedInt, std::uint32_t> after = playOn(g, save.log, kAfter);
		const std::string diff = differences(after, rec.whole.hashes, rec.savedFrame, rec.savedFrame + (UnsignedInt)kAfter);
		CHECK_MESSAGE(diff.empty(), "differing frames:" << diff);
		CHECK(after == rec.whole.hashes); // the continued recording is the whole game, the frames before F included
	}
	// T2 through the load path: the loaded game's snapshot at F was the save's (checked by the verify mode below on a fresh game)
	Loaded v;
	CommandLogLoad verify;
	verify.setVerifyLog(true);
	REQUIRE_MESSAGE(verify.prepare(save, &error), error);
	CHECK(verify.mode() == CommandLogLoad::Mode::CatchUp);
	load(*s, verify.log().header.game, v);
	REQUIRE_MESSAGE(verify.begin(*v.game, &error), error);
	while (verify.step(*v.game, 50) == CommandLogLoad::State::CatchingUp)
	{
	}
	CHECK_MESSAGE(verify.state() == CommandLogLoad::State::Ready, verify.failure());
	CHECK(verify.hashesCompared() == (unsigned long long)kBefore);
}

TEST_CASE("save4e retail: a refused snapshot is a named error and the started game is as it was (its state hash and frame), and it still runs: a changed "
		  "byte, a truncated snapshot, another game's snapshot, a header's other hash; the verify mode names the first byte a snapshot differs from its log")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 4502u), running, 300, 0, "s4e_bad", true);
	Recorded other = recordGame(*s, gameMessage(*s, 4503u), running, 300, 0, "s4e_other", true); // another seed: another game at the same frame
	REQUIRE(rec.savedFrame == other.savedFrame);
	auto refused = [&](SaveGameFile bad, const std::string &expect, const char *what) {
		INFO(what);
		std::string error;
		SaveGameFile reread;
		REQUIRE_MESSAGE(SaveGameFile::parse(bad.encode(), reread, &error), error); // the container is intact (re-signed): the snapshot is not
		CommandLogLoad cl;
		REQUIRE_MESSAGE(cl.prepare(reread, &error), error);
		Loaded g;
		load(*s, cl.log().header.game, g);
		const UnsignedInt frame = g.game->frame();
		const std::uint32_t hash = g.game->logic().computeStateHash();
		const size_t objects = g.game->drawables().liveCount();
		const std::string stepBefore = drawablesOutOfStep(*g.game);
		CHECK_FALSE(cl.begin(*g.game, &error));
		CHECK(cl.state() == CommandLogLoad::State::Failed);
		CHECK_MESSAGE(error.rfind("the save's snapshot is refused: " + expect, 0) == 0, error);
		CHECK_FALSE(g.game->closed());
		CHECK(g.game->frame() == frame);
		CHECK(g.game->logic().getFrame() == frame);
		CHECK(g.game->logic().computeStateHash() == hash);
		CHECK(g.game->drawables().liveCount() == objects);
		// the client: untouched by a refusal of the framing pass, or told every object's conditions again after a put-back
		const std::string outOfStep = drawablesOutOfStep(*g.game);
		CHECK_MESSAGE((outOfStep.empty() || outOfStep == stepBefore), "drawables out of step:" << outOfStep);
		CHECK(g.game->advance(0.2) == 1); // the game still runs (the caller discards it; it is not half loaded)
	};
	{
		SaveGameFile bad = rec.save;
		bad.snapshot[bad.snapshot.size() / 2] ^= 0x5A; // inside a block's data
		refused(bad, "", "a changed byte");
	}
	{
		SaveGameFile bad = rec.save;
		bad.snapshot.resize(bad.snapshot.size() * 2 / 3);
		refused(bad, "", "a truncated snapshot");
	}
	{
		SaveGameFile bad = rec.save;
		bad.snapshot = other.save.snapshot; // loads, but is not the header's state
		refused(bad, "it loads with state hash " + std::to_string(other.save.header.stateHash), "another game's snapshot");
	}
	{
		SaveGameFile bad = rec.save;
		bad.header.stateHash ^= 1u;
		bad.log = rec.save.log; // the log's end record keeps the true hash: prepare refuses the header first
		std::string error;
		SaveGameFile reread;
		REQUIRE_MESSAGE(SaveGameFile::parse(bad.encode(), reread, &error), error);
		CommandLogLoad cl;
		CHECK_FALSE(cl.prepare(reread, &error));
		CHECK_MESSAGE(error.find("its header says frame") != std::string::npos, error);
	}
	{
		// the verify mode: the log replays to the save's state, the snapshot is another game's: named by its first differing byte
		SaveGameFile bad = rec.save;
		bad.snapshot = other.save.snapshot;
		std::string error;
		CommandLogLoad cl;
		cl.setVerifyLog(true);
		REQUIRE_MESSAGE(cl.prepare(bad, &error), error);
		Loaded g;
		load(*s, cl.log().header.game, g);
		REQUIRE_MESSAGE(cl.begin(*g.game, &error), error);
		while (cl.step(*g.game, 50) == CommandLogLoad::State::CatchingUp)
		{
		}
		CHECK(cl.state() == CommandLogLoad::State::Failed);
		CHECK_MESSAGE(cl.failure().rfind("the save's snapshot is not the state its log replays to: the caught-up game's snapshot (", 0) == 0, cl.failure());
		CHECK(cl.failure().find(") from byte ") != std::string::npos);
	}
}

TEST_CASE("save4e retail: legal but unusual events right before a save (a structure captured, a unit temporarily defected, a unit killed, a player's "
		  "every object killed: defeated) load from the snapshot into a new game that plays on with every frame's hash of the unsaved game")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage message = fourAiMessage(*s, 4504u);
	std::string error;
	Loaded a;
	load(*s, message, a);
	for (int i = 0; i < 900; ++i)
	{
		humanCommands(*a.game, a.game->commands());
		REQUIRE(a.game->advance(0.2) == 1);
	}
	a.game->drainFrames();
	GameLogic &logic = a.game->logic();
	PlayerList &players = logic.players();
	Player *human = nullptr;
	std::vector<Player *> ais;
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		Player *p = players.getNthPlayer(i);
		if (!p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		if (p->getPlayerType() == PLAYER_HUMAN)
		{
			human = p;
		}
		else
		{
			ais.push_back(p);
		}
	}
	REQUIRE(human);
	REQUIRE(ais.size() == 4u);
	std::vector<std::string> events;
	// a structure of the first AI captured by the human (RW 0x6996DC's team change)
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == ais[0] && o->isKindOfName("STRUCTURE") && !o->isEffectivelyDead() && !o->isUnderConstruction())
		{
			o->setCapturedTeam(human->getDefaultTeam());
			events.push_back("captured " + o->getTemplate()->getName());
			break;
		}
	}
	// a unit of the second AI temporarily defected to the human (RW 0x699513)
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == ais[1] && !o->isKindOfName("STRUCTURE") && o->getAIUpdateInterface() && !o->isEffectivelyDead())
		{
			o->setTemporaryTeam(human->getDefaultTeam());
			events.push_back("defected " + o->getTemplate()->getName());
			break;
		}
	}
	// a unit of the third AI killed
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == ais[2] && !o->isKindOfName("STRUCTURE") && o->getAIUpdateInterface() && !o->isEffectivelyDead())
		{
			o->kill(0);
			events.push_back("killed " + o->getTemplate()->getName());
			break;
		}
	}
	// every object of the fourth AI killed: it is defeated within the next frames (the victory conditions)
	int killed = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == ais[3] && !o->isEffectivelyDead())
		{
			o->kill(0);
			++killed;
		}
	}
	events.push_back("killed every object of the fourth AI (" + std::to_string(killed) + ")");
	REQUIRE(events.size() == 4u);
	std::string eventText;
	for (const std::string &e : events)
	{
		eventText += " " + e + ";";
	}
	MESSAGE("events:" << eventText);
	// saved right after the events (between frames), and again 30 frames later (the defeat ran)
	for (int round = 0; round < 2; ++round)
	{
		if (round == 1)
		{
			for (int i = 0; i < 30; ++i)
			{
				humanCommands(*a.game, a.game->commands());
				REQUIRE(a.game->advance(0.2) == 1);
			}
			a.game->drainFrames();
		}
		const UnsignedInt saved = a.game->logic().getFrame();
		std::vector<std::uint8_t> snapshot;
		REQUIRE_MESSAGE(a.game->logic().saveSnapshot(snapshot, &error), error);
		const std::uint32_t hash = a.game->logic().computeStateHash();
		Loaded b;
		{
			auto peerContext = starttest::peerWorld()->world->enterContext();
			load(*starttest::peerWorld(), message, b);
		}
		REQUIRE_MESSAGE(b.game->loadSnapshot(snapshot, &error), error);
		CHECK(b.game->logic().getFrame() == saved);
		CHECK_MESSAGE(b.game->logic().computeStateHash() == hash, "differs:" << stateDifference(a.game->logic(), b.game->logic()));
		std::vector<std::uint8_t> again;
		REQUIRE_MESSAGE(b.game->logic().saveSnapshot(again, &error), error);
		CHECK(again == snapshot); // T2
		// both play on 300 frames with the same human: every frame's hash equal
		std::string diff;
		for (int i = 0; i < 300 && diff.empty(); ++i)
		{
			humanCommands(*a.game, a.game->commands());
			humanCommands(*b.game, b.game->commands());
			REQUIRE(a.game->advance(0.2) == 1);
			REQUIRE(b.game->advance(0.2) == 1);
			a.game->drainFrames();
			b.game->drainFrames();
			if (a.game->logic().computeStateHash() != b.game->logic().computeStateHash())
			{
				diff = "frame " + std::to_string(a.game->logic().getFrame());
			}
		}
		CHECK_MESSAGE(diff.empty(), (round == 0 ? "saved right after the events" : "saved 30 frames after them") << ": first differing " << diff);
	}
}

// ---- lane SAVE-4e review r1 (Sol): the load is transactional for every block, the closed sets ----------------------------------------------------------

namespace
{
// a standing tree of the map toppled through the logic (TerrainTrees::toppleTree, RW 0x6849F8): the game's trees are no longer the START's
void toppleATree(LiveGame &game, size_t skip = 0)
{
	TerrainTrees &trees = game.logic().trees();
	TerrainTrees::Record *tree = nullptr;
	for (const TerrainTrees::Record &r : trees.records())
	{
		if (r.clientId && !r.toppled && !r.changed && skip-- == 0)
		{
			tree = trees.findClosestTree(r.position, 1.0f, true, 0);
			break;
		}
	}
	REQUIRE(tree);
	trees.toppleTree(*tree, Coord3D{ tree->position.x - 10.0f, tree->position.y, tree->position.z }, nullptr);
	REQUIRE(trees.anyChanged());
}

// the stream's blocks: (token, the offset of the block's first byte, its end), read from the framing (GameState: "ALAE" "2STR" version tagged, then per
// block its token (u8 length or 0xFF + u32), "BLOK", the absolute end offset)
struct BlockAt
{
	std::string token;
	size_t begin = 0, end = 0;
};
std::vector<BlockAt> blocksOf(const std::vector<std::uint8_t> &b)
{
	std::vector<BlockAt> out;
	auto u32 = [&](size_t at) { return (std::uint32_t)b[at] | ((std::uint32_t)b[at + 1] << 8) | ((std::uint32_t)b[at + 2] << 16) | ((std::uint32_t)b[at + 3] << 24); };
	size_t at = 16;
	for (;;)
	{
		size_t n = b[at++];
		if (n == 0xFF)
		{
			n = u32(at);
			at += 4;
		}
		const std::string token(b.begin() + (std::ptrdiff_t)at, b.begin() + (std::ptrdiff_t)(at + n));
		at += n;
		if (token == "SG_EOF")
		{
			break;
		}
		REQUIRE(std::string(b.begin() + (std::ptrdiff_t)at, b.begin() + (std::ptrdiff_t)at + 4) == "KOLB"); // "BLOK" as a little-endian u32
		BlockAt blk;
		blk.token = token;
		blk.end = u32(at + 4);
		blk.begin = at + 8;
		out.push_back(blk);
		at = blk.end;
	}
	return out;
}
} // namespace

TEST_CASE("save4e retail review r1: a refused snapshot (a hostile end token, a late truncation, a late block's version refused) with a toppled tree "
		  "saved leaves the started game exactly as it was: not closed, its state hash and its snapshot bytes the same")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 4502u), running, 300, 0, "review_trees", true, [](LiveGame &g) { toppleATree(g); });
	for (int c = 0; c < 3; ++c)
	{
		SaveGameFile bad = rec.save;
		const char *what = c == 0 ? "a hostile end token" : c == 1 ? "a late truncation" : "a late block's version refused";
		INFO(what);
		if (c == 0)
		{
			bad.snapshot.back() ^= 1;
		}
		else if (c == 1)
		{
			bad.snapshot.resize(bad.snapshot.size() - 3);
		}
		else
		{
			const std::vector<BlockAt> blocks = blocksOf(bad.snapshot);
			REQUIRE(blocks.back().token == "CHUNK_MissionObjectives"); // lane CAMPUI-1: the tracker's block, its version byte first (RW 0x8363F2)
			REQUIRE(bad.snapshot[blocks.back().begin] == 1);
			bad.snapshot[blocks.back().begin] = 0xFF;
		}
		std::string error;
		SaveGameFile reread;
		REQUIRE_MESSAGE(SaveGameFile::parse(bad.encode(), reread, &error), error);
		CommandLogLoad cl;
		REQUIRE_MESSAGE(cl.prepare(reread, &error), error);
		Loaded g;
		load(*s, cl.log().header.game, g);
		std::vector<std::uint8_t> before, after;
		REQUIRE(g.game->logic().saveSnapshot(before, &error));
		const std::uint32_t hash = g.game->logic().computeStateHash();
		CHECK_FALSE(cl.begin(*g.game, &error));
		MESSAGE(what << ": " << error);
		CHECK(error.rfind("the save's snapshot is refused: ", 0) == 0);
		CHECK(error.find("could not be put back") == std::string::npos);
		CHECK_FALSE(g.game->closed());
		CHECK(g.game->logic().computeStateHash() == hash);
		REQUIRE(g.game->logic().saveSnapshot(after, &error));
		CHECK(after == before);
	}
}

TEST_CASE("save4e retail review r1: a refusal inside each block of a real snapshot (its version byte out of the window), into a game whose own trees "
		  "changed since its START, leaves that game byte-identical (state hash and snapshot bytes) and running; the exact list of blocks")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage message = gameMessage(*s, 4505u);
	std::string error;
	std::vector<std::uint8_t> snapshot;
	{
		Loaded a;
		load(*s, message, a);
		for (int i = 0; i < 300; ++i)
		{
			humanCommands(*a.game, a.game->commands());
			REQUIRE(a.game->advance(0.2) == 1);
		}
		a.game->drainFrames();
		toppleATree(*a.game);
		REQUIRE(a.game->advance(0.2) == 1);
		a.game->drainFrames();
		REQUIRE_MESSAGE(a.game->logic().saveSnapshot(snapshot, &error), error);
	}
	const std::vector<BlockAt> blocks = blocksOf(snapshot);
	std::vector<std::string> tokens;
	for (const BlockAt &b : blocks)
	{
		tokens.push_back(b.token);
	}
	CHECK(tokens == std::vector<std::string>{ "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random", "obfme:Objects",
						"obfme:UpdateScheduler", "obfme:TunnelTracker", "obfme:Combat", "obfme:SellList", "obfme:Experience", "obfme:Invisibility",
						"obfme:DamageOverTime", "obfme:Partition", "obfme:Emotions", "obfme:AIWorld", "obfme:Shroud", "obfme:SkirmishAI",
						"obfme:ScriptEngine", "obfme:Victory", "obfme:Weather", "obfme:Economy", "obfme:CreateAHero", "obfme:TerrainTrees",
						"obfme:LargeGroupAudio", "CHUNK_ObjectivesMenu", "CHUNK_MissionObjectives" });
	Loaded g;
	load(*s, message, g);
	for (int i = 0; i < 50; ++i)
	{
		humanCommands(*g.game, g.game->commands());
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	toppleATree(*g.game, 3); // another tree than the snapshot's: the game is not at its START
	for (const BlockAt &b : blocks)
	{
		INFO("a refusal in " << b.token);
		std::vector<std::uint8_t> bad = snapshot;
		bad[b.begin] = 0xFF; // every block opens with its class's version byte: 255 is above every window
		std::vector<std::uint8_t> before, after;
		REQUIRE(g.game->logic().saveSnapshot(before, &error));
		const std::uint32_t hash = g.game->logic().computeStateHash();
		const UnsignedInt frame = g.game->frame();
		std::string why;
		CHECK_FALSE(g.game->loadSnapshot(bad, &why));
		CHECK_MESSAGE(why.find("block '" + b.token + "'") != std::string::npos, why);
		CHECK_MESSAGE(why.find("put back") == std::string::npos, why);
		CHECK_FALSE(g.game->closed());
		CHECK(g.game->frame() == frame);
		CHECK(g.game->logic().computeStateHash() == hash);
		REQUIRE(g.game->logic().saveSnapshot(after, &error));
		CHECK(after == before);
	}
	// the good snapshot then loads into the same game, and the game runs on
	REQUIRE_MESSAGE(g.game->loadSnapshot(snapshot, &error), error);
	CHECK(g.game->advance(0.2) == 1);
}

TEST_CASE("save4e retail review r1: blocks in another order, a truncation and a hostile end token are refused by the framing pass before any block loads")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	Recorded rec = recordGame(*s, gameMessage(*s, 4506u), running, 200, 0, "review_order", true);
	std::string error;
	Loaded g;
	load(*s, rec.whole.header.game, g);
	GameState &gs = g.game->logic().gameState();
	// a validly framed permutation: obfme:GameLogic and obfme:Objects swapped (Sol's probe)
	std::vector<std::string> tokens;
	for (const BlockAt &b : blocksOf(rec.save.snapshot))
	{
		tokens.push_back(b.token);
	}
	std::vector<std::uint8_t> reordered;
	{
		REQUIRE_MESSAGE(g.game->loadSnapshot(rec.save.snapshot, &error), error);
		std::vector<std::string> swapped = tokens;
		std::iter_swap(std::find(swapped.begin(), swapped.end(), "obfme:GameLogic"), std::find(swapped.begin(), swapped.end(), "obfme:Objects"));
		gs.saveBlocks(swapped, reordered);
	}
	CHECK_FALSE(gs.checkFraming(reordered, &error));
	CHECK(error == "the snapshot is damaged: the snapshot's block 'obfme:Objects' stands where this build loads 'obfme:GameLogic' (the blocks load in the "
				   "list's order)");
	std::vector<std::uint8_t> truncated = rec.save.snapshot;
	truncated.resize(truncated.size() - 3);
	CHECK_FALSE(gs.checkFraming(truncated, &error));
	CHECK(error.rfind("the snapshot is damaged: ", 0) == 0);
	std::vector<std::uint8_t> endToken = rec.save.snapshot;
	endToken.back() ^= 1;
	CHECK_FALSE(gs.checkFraming(endToken, &error));
	CHECK(error == "the snapshot is damaged: the snapshot carries an unknown block 'SG_EOG'");
	CHECK(gs.checkFraming(rec.save.snapshot, &error));
	// through the load: refused by the framing, the game unchanged
	std::vector<std::uint8_t> before, after;
	REQUIRE(g.game->logic().saveSnapshot(before, &error));
	CHECK_FALSE(g.game->loadSnapshot(reordered, &error));
	CHECK(error.rfind("the snapshot is damaged: ", 0) == 0);
	REQUIRE(g.game->logic().saveSnapshot(after, &error));
	CHECK(after == before);
}

TEST_CASE("save4e retail review r1: an object's weapon bonus mask with a bit outside the 22 conditions is refused by name, the object unchanged")
{
	OPENBFME_REQUIRE_START(s);
	Loaded g;
	load(*s, gameMessage(*s, 4502u), g);
	Object *o = g.game->logic().getFirstObject();
	REQUIRE(o);
	for (int i = 0; i < WEAPONBONUS_CONDITION_COUNT; ++i)
	{
		o->setWeaponBonusCondition(i, i % 2 == 1);
	}
	REQUIRE(o->weaponBonusConditionMask() == 0x002AAAAAu);
	XferSave w;
	o->xfer(w);
	std::vector<std::uint8_t> data = w.data();
	const std::vector<std::uint8_t> marker{ 0xAA, 0xAA, 0x2A, 0 };
	auto at = std::search(data.begin(), data.end(), marker.begin(), marker.end());
	REQUIRE(at != data.end());
	REQUIRE(std::search(at + 4, data.end(), marker.begin(), marker.end()) == data.end());
	at[0] = 0;
	at[1] = 0;
	at[2] = 0;
	at[3] = 0x80;
	XferLoad r(data);
	std::string refusal;
	try
	{
		o->xfer(r);
	}
	catch (const XferException &e)
	{
		refusal = e.what();
	}
	CHECK(refusal == "an object's weapon bonus conditions 0x80000000 hold bits outside the 22 conditions (0x003FFFFF)");
	CHECK(o->weaponBonusConditionMask() == 0x002AAAAAu);
}

namespace
{
// the trees' records as the logic holds them (position, id, toppled and its direction, lumber value, changed), compared field by field: the hash and
// the snapshot cover only the changed records, so a record wrongly marked unchanged hides from both (review r2)
std::string treesDiffer(const std::vector<TerrainTrees::Record> &now, const std::vector<TerrainTrees::Record> &want)
{
	if (now.size() != want.size())
	{
		return std::to_string(now.size()) + " trees, expected " + std::to_string(want.size());
	}
	for (size_t i = 0; i < now.size(); ++i)
	{
		const TerrainTrees::Record &a = now[i];
		const TerrainTrees::Record &b = want[i];
		if (a.clientId != b.clientId || a.toppled != b.toppled || a.value != b.value || a.changed != b.changed || a.index != b.index ||
			a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z || a.toppleDirection.x != b.toppleDirection.x ||
			a.toppleDirection.y != b.toppleDirection.y)
		{
			return "tree " + std::to_string(i) + ": toppled " + std::to_string(a.toppled) + " value " + std::to_string(a.value) + " changed " +
				std::to_string(a.changed) + " id " + std::to_string(a.clientId) + ", expected toppled " + std::to_string(b.toppled) + " value " +
				std::to_string(b.value) + " changed " + std::to_string(b.changed) + " id " + std::to_string(b.clientId);
		}
	}
	return "";
}
} // namespace

TEST_CASE("save4e retail review r2: a snapshot refused late (LargeGroupAudio's version) or after every post-process, loaded into a game that has since "
		  "made three teams of a new prototype and toppled a tree, leaves that game exactly as it was: not closed, its teams, trees, state hash and bytes")
{
	OPENBFME_REQUIRE_START(s);
	Loaded g;
	load(*s, gameMessage(*s, 4502u), g);
	TeamFactory &teams = g.game->logic().players().teams();
	TeamPrototype *prototype = teams.initTeam("ReviewExtra", g.game->logic().players().getNthPlayer(1), false);
	REQUIRE(prototype);
	for (int i = 0; i < 20; ++i)
	{
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	std::string error;
	std::vector<std::uint8_t> older;
	REQUIRE_MESSAGE(g.game->logic().saveSnapshot(older, &error), error);
	for (int i = 0; i < 40; ++i)
	{
		REQUIRE(g.game->advance(0.2) == 1);
	}
	g.game->drainFrames();
	const size_t olderTeams = teams.teamCount();
	for (int i = 0; i < 3; ++i)
	{
		REQUIRE(teams.createTeam(prototype));
	}
	REQUIRE(teams.teamCount() == olderTeams + 3);
	toppleATree(*g.game);
	std::vector<std::uint8_t> before, after;
	REQUIRE_MESSAGE(g.game->logic().saveSnapshot(before, &error), error);
	const std::uint32_t hash = g.game->logic().computeStateHash();
	const UnsignedInt frame = g.game->frame();
	const size_t count = teams.teamCount();
	const std::vector<TerrainTrees::Record> records = g.game->logic().trees().records();
	auto same = [&](const char *what) {
		INFO(what);
		REQUIRE_FALSE(g.game->closed());
		CHECK(g.game->frame() == frame);
		CHECK(teams.teamCount() == count);
		CHECK(g.game->logic().computeStateHash() == hash);
		REQUIRE_MESSAGE(g.game->logic().saveSnapshot(after, &error), error);
		CHECK(after == before);
		const std::string trees = treesDiffer(g.game->logic().trees().records(), records);
		CHECK_MESSAGE(trees.empty(), trees);
	};
	std::vector<std::uint8_t> bad = older;
	const std::vector<BlockAt> blocks = blocksOf(bad);
	REQUIRE(blocks.back().token == "CHUNK_MissionObjectives"); // lane CAMPUI-1: the tracker's block, its version byte first (RW 0x8363F2)
	bad[blocks.back().begin] = 0xFF;
	CHECK_FALSE(g.game->loadSnapshot(bad, &error));
	MESSAGE("late refusal with three extra teams: " << error);
	CHECK(error.find("could not be put back") == std::string::npos);
	same("MissionObjectives' version refused");
	CHECK_FALSE(g.game->loadSnapshot(older, &error, [](GameLogic &) { return std::string("review refusal after every post-process"); }));
	MESSAGE("refused after every post-process: " << error);
	same("refused after every post-process");
	// the older snapshot itself loads (its teams trimmed back), and the game runs on
	REQUIRE_MESSAGE(g.game->loadSnapshot(older, &error), error);
	CHECK(teams.teamCount() == olderTeams);
	REQUIRE(g.game->advance(0.2) == 1);
	g.game->drainFrames();
	// the sibling (review r2, pattern 7): this game now has three teams fewer than the newer snapshot `before`; that snapshot refused late leaves the
	// game with its fewer teams (the put-back trims the candidate's three), and the newer snapshot then loads with them
	std::vector<std::uint8_t> fewer;
	REQUIRE_MESSAGE(g.game->logic().saveSnapshot(fewer, &error), error);
	const std::uint32_t fewerHash = g.game->logic().computeStateHash();
	std::vector<std::uint8_t> newer = before;
	const std::vector<BlockAt> newerBlocks = blocksOf(newer);
	newer[newerBlocks.back().begin] = 0xFF;
	CHECK_FALSE(g.game->loadSnapshot(newer, &error));
	MESSAGE("a newer snapshot with three more teams refused late: " << error);
	CHECK(error.find("could not be put back") == std::string::npos);
	REQUIRE_FALSE(g.game->closed());
	CHECK(teams.teamCount() == olderTeams);
	CHECK(g.game->logic().computeStateHash() == fewerHash);
	REQUIRE_MESSAGE(g.game->logic().saveSnapshot(after, &error), error);
	CHECK(after == fewer);
	REQUIRE_MESSAGE(g.game->loadSnapshot(before, &error), error);
	CHECK(teams.teamCount() == count);
	CHECK(g.game->logic().computeStateHash() == hash);
	REQUIRE(g.game->advance(0.2) == 1);
}

TEST_CASE("save4e retail review r2: a snapshot taken before the game's first tree change (a topple, a harvest, a tree harvested away), loaded after it, restores the trees' "
		  "actual records (toppled, lumber value), not only the hash and the snapshot bytes")
{
	OPENBFME_REQUIRE_START(s);
	for (int c = 0; c < 3; ++c)
	{
		const char *what = c == 0 ? "a tree toppled after the save" : c == 1 ? "a tree harvested after the save" : "a tree harvested away after the save";
		INFO(what);
		Loaded g;
		load(*s, gameMessage(*s, 4502u), g);
		std::string error;
		std::vector<std::uint8_t> before, after;
		REQUIRE_MESSAGE(g.game->logic().saveSnapshot(before, &error), error);
		const std::vector<TerrainTrees::Record> original = g.game->logic().trees().records();
		const std::uint32_t hash = g.game->logic().computeStateHash();
		TerrainTrees &trees = g.game->logic().trees();
		REQUIRE_FALSE(trees.anyChanged());
		if (c == 0)
		{
			toppleATree(*g.game);
		}
		else
		{
			TerrainTrees::Record *tree = nullptr;
			for (const TerrainTrees::Record &r : trees.records())
			{
				if (r.clientId && r.value > 1)
				{
					tree = trees.findClosestTree(r.position, 1.0f, true, 0);
					if (tree && tree->value > 1)
					{
						break;
					}
					tree = nullptr;
				}
			}
			REQUIRE(tree);
			const int all = tree->value;
			REQUIRE(trees.harvestTree(*tree, c == 1 ? 1 : all) == (c == 1 ? 1 : all)); // c == 2: the record cleared (RW 0x67D3C2)
		}
		REQUIRE(!treesDiffer(trees.records(), original).empty());
		REQUIRE_MESSAGE(g.game->loadSnapshot(before, &error), error);
		const std::string differ = treesDiffer(trees.records(), original);
		CHECK_MESSAGE(differ.empty(), differ);
		CHECK(g.game->logic().computeStateHash() == hash);
		REQUIRE_MESSAGE(g.game->logic().saveSnapshot(after, &error), error);
		CHECK(after == before);
	}
}

// lane OWNERFIX-2 (Sol's review of 7fe9e8e1): a handicapped player's game saved, loaded (snapshot) and played on, and its recording replayed: the handicap
// (Player + 0x29C, RW 0x6B09DE) is the START's on every path (it is not in the snapshot: the players are made from the recorded START first), the objects
// carrying HandicapPercent50 (RW 0x6940DB) are the same ones, every later frame's hash is the unsaved game's, and a unit made after the load takes the list
TEST_CASE("ownerfix2 retail: a skirmish with a -50% handicap saved, loaded and played on keeps the handicap and every hash; its recording replays with every hash equal")
{
	OPENBFME_REQUIRE_START(s);
	const int kBefore = 150, kAfter = 150;
	const ProfileIdentity running = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	NewGameMessage m = gameMessage(*s, 5031u);
	m.game.slots[0].handicap = -50;
	Recorded rec = recordGame(*s, m, running, kBefore, kAfter, "of2", true);
	REQUIRE(!rec.save.snapshot.empty());
	std::string error;
	SaveGameFile save;
	REQUIRE_MESSAGE(SaveGameFile::parse(rec.save.encode(), save, &error), error);
	auto playerAt = [](GameLogic &logic, int start) -> Player * {
		for (int i = 0; i < logic.players().getPlayerCount(); ++i)
		{
			Player *p = logic.players().getNthPlayer(i);
			if (p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
			{
				return p;
			}
		}
		return nullptr;
	};
	// the objects carrying a HandicapPercent list, by id and list name
	auto listed = [](GameLogic &logic) {
		std::set<std::pair<UnsignedInt, std::string>> out;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate")))
			{
				for (const std::string &n : pool->listNames())
				{
					if (n.rfind("HandicapPercent", 0) == 0)
					{
						out.insert({ (UnsignedInt)o->getID(), n });
					}
				}
			}
		}
		return out;
	};
	std::set<std::pair<UnsignedInt, std::string>> atSave;
	{
		Loaded g;
		CommandLogLoad cl;
		REQUIRE_MESSAGE(cl.prepare(save, &error), error);
		REQUIRE(cl.mode() == CommandLogLoad::Mode::Snapshot);
		CHECK(cl.log().header.game.game.slots[0].handicap == -50); // the save's recorded START carries it
		load(*s, cl.log().header.game, g);
		REQUIRE_MESSAGE(cl.begin(*g.game, &error), error);
		REQUIRE(cl.state() == CommandLogLoad::State::Ready);
		CHECK(g.game->logic().computeStateHash() == save.header.stateHash);
		Player *human = playerAt(g.game->logic(), 0);
		Player *ai = playerAt(g.game->logic(), 1);
		REQUIRE(human);
		REQUIRE(ai);
		CHECK(human->getHandicap() == -50);
		CHECK(ai->getHandicap() == 0);
		atSave = listed(g.game->logic());
		REQUIRE(!atSave.empty());
		for (const auto &e : atSave)
		{
			INFO(e.first << " " << e.second);
			CHECK(e.second == "HandicapPercent50");
			Object *o = g.game->logic().findObjectByID((ObjectID)e.first);
			REQUIRE(o);
			CHECK(o->getControllingPlayer() == human);
		}
		const std::map<UnsignedInt, std::uint32_t> after = playOn(g, save.log, kAfter);
		const std::string diff = differences(after, rec.whole.hashes, rec.savedFrame, rec.savedFrame + (UnsignedInt)kAfter);
		CHECK_MESSAGE(diff.empty(), "differing frames:" << diff);
		CHECK(after == rec.whole.hashes);
		// a unit made after the load: the handicap list too (the loaded player's + 0x29C at initObject)
		const ThingTemplate *tt = g.game->logic().things().findTemplate("GondorFighter");
		REQUIRE(tt);
		Object *fresh = g.game->logic().newObject(tt, human->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(fresh);
		const ObjectID freshId = fresh->getID();
		g.game->logic().runLogicFrame();
		CHECK(listed(g.game->logic()).count({ (UnsignedInt)freshId, "HandicapPercent50" }) == 1);
	}
	// the recording replayed from frame 0 (the verify mode: every recorded hash compared on the way): the same player handicap, the same objects listed
	Loaded v;
	CommandLogLoad verify;
	verify.setVerifyLog(true);
	REQUIRE_MESSAGE(verify.prepare(save, &error), error);
	REQUIRE(verify.mode() == CommandLogLoad::Mode::CatchUp);
	load(*s, verify.log().header.game, v);
	REQUIRE_MESSAGE(verify.begin(*v.game, &error), error);
	while (verify.step(*v.game, 50) == CommandLogLoad::State::CatchingUp)
	{
	}
	CHECK_MESSAGE(verify.state() == CommandLogLoad::State::Ready, verify.failure());
	CHECK(verify.hashesCompared() == (unsigned long long)kBefore);
	REQUIRE(playerAt(v.game->logic(), 0));
	CHECK(playerAt(v.game->logic(), 0)->getHandicap() == -50);
	CHECK(listed(v.game->logic()) == atSave);
}
