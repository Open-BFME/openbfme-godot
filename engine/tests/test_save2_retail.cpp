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

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/SimulationProfile.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/CommandLogLoad.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
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

void load(starttest::Shared &s, const NewGameMessage &m, Loaded &out)
{
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
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
Recorded recordGame(starttest::Shared &s, const NewGameMessage &message, const ProfileIdentity &profile, int beforeSave, int after, const std::string &tag)
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
		humanCommands(*g.game, g.game->commands());
		REQUIRE(g.game->advance(0.2) == 1);
	}
	// the save, as GameWorld::save_game takes it: the queued batches run, the log so far + (F, hash)
	g.game->drainFrames();
	r.savedFrame = g.game->logic().getFrame();
	const std::uint32_t hash = g.game->logic().computeStateHash();
	REQUIRE_MESSAGE(writer.logUpTo(r.savedFrame, hash, r.save.log, &error), error);
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
	while (cl.step(*g.game, 20) == CommandLogLoad::State::CatchingUp)
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
	CHECK(cl.localInputIgnored() > 0); // tolerance: the player's input during the catch-up was taken and dropped (how much is the loop's timing)
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
		CHECK(cl.hashesCompared() == 150u);
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
		CHECK(continued.hashes.size() == 250u);
		// the second save is this player's own profile now; it replays under the saved profile to the switch, then under the player's, every hash equal
		CHECK_FALSE(SaveGameCompatibility::check(second.header, running, ProfileIdentity::buildId(), "test", false).crossProfile);
		g.game.reset(); // lane MAPRESET-1 (merge q7): one live game per world (S-3808); the first game is over
		Loaded g2;
		CommandLogLoad cl2;
		REQUIRE_MESSAGE(catchUp(*s, second, g2, cl2, error) == CommandLogLoad::State::Ready, error);
		CHECK(cl2.hashesCompared() == 250u);
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
		CHECK(error.rfind("the saved game does not replay the same in this build: frame " + std::to_string(switchFrame + 1) + " has state hash ", 0) == 0);
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
		  "past F + 3000 with the uninterrupted game's commands: every frame's hash is the unsaved game's")
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
	const NewGameMessage message = fourAiMessage(*s, 8642u);
	struct Taken
	{
		std::string scenario, path, found;
		UnsignedInt frame = 0;
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
		// from two minutes on, each scenario is saved at the first frame it is in flight (at most 20 minutes of game time)
		bool seen[4] = { false, false, false, false };
		for (int i = 0; i < 6000 && saves.size() < 4; ++i)
		{
			humanCommands(*g.game, g.game->commands());
			REQUIRE(g.game->advance(0.2) == 1);
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
				SaveGameFile save;
				const std::uint32_t hash = g.game->logic().computeStateHash();
				REQUIRE_MESSAGE(writer.logUpTo(t.frame, hash, save.log, &error), error);
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
				MESSAGE(t.scenario << " at frame " << t.frame << ":" << t.found);
				saves.push_back(t);
			}
		}
		for (int k = 0; k < 4; ++k)
		{
			CHECK_MESSAGE(seen[k], names[k] << " never happened in 20 minutes of the game");
		}
		REQUIRE(!saves.empty());
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
	// each save in a fresh process: caught up from its own log, then the uninterrupted game's commands to its end, every hash compared
	for (const Taken &t : saves)
	{
		const std::string reportPath = t.path + ".report";
		const int rc = runPeer("--load-save \"" + t.path + "\" --replay \"" + fullPath + "\" --report \"" + reportPath + "\"");
		const std::string report = readText(reportPath);
		INFO(t.scenario << " (frame " << t.frame << "), fresh process report:\n" << report);
		CHECK(rc == 0);
		CHECK(reportField(report, "save_frame") == std::to_string(t.frame));
		CHECK(reportField(report, "catch_up_hashes") == std::to_string(t.frame - startFrame));
		CHECK(reportField(report, "hashes_compared") == std::to_string(lastFrame - t.frame));
		CHECK(reportField(report, "mismatches") == "0");
		char want[64];
		std::snprintf(want, sizeof(want), "0x%08X recorded 0x%08X", finalHash, finalHash);
		CHECK(reportField(report, "final_hash") == want);
		std::remove(t.path.c_str());
		std::remove(reportPath.c_str());
	}
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

TEST_CASE("save2 retail T1 (SAVES.md), a script waiting on a timer: a campaign mission saved with a script countdown timer running, loaded in a fresh "
		  "process from its campaign start and played on to F + 3000 with the uninterrupted game's commands: every frame's hash equal")
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
	const int rc = runPeer("--load-save \"" + savePath + "\" --replay \"" + fullPath + "\" --report \"" + reportPath + "\"");
	const std::string report = readText(reportPath);
	MESSAGE("fresh process report:\n" << report);
	CHECK(rc == 0);
	CHECK(reportField(report, "save_frame") == std::to_string(savedFrame));
	CHECK(reportField(report, "catch_up_hashes") == std::to_string(savedFrame - startFrame));
	CHECK(reportField(report, "hashes_compared") == "3000");
	CHECK(reportField(report, "mismatches") == "0");
	char want[64];
	std::snprintf(want, sizeof(want), "0x%08X recorded 0x%08X", finalHash, finalHash);
	CHECK(reportField(report, "final_hash") == want);
	for (const std::string &p : { savePath, fullPath, reportPath })
	{
		std::remove(p.c_str());
	}
	(void)lastFrame;
#endif
}
