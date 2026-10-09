// OpenBFME unit tests. GPL-3.0.
// Lane MP-1 on the retail game: a recorded skirmish plays back with every frame's hash matching (and a changed state is found at its frame); two headless
// peers (tools: the openbfme_peer executable, separate processes, UDP on localhost) play a skirmish with scripted humans on both sides and Medium AIs in
// lockstep with identical hashes on every frame; an injected divergence on one peer gives a desync report on both naming the frame and the subsystem; the
// recording of a network game plays back in a third process with matching hashes. SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/PlayerScience.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/NetPacket.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
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

// a human (slot 0, Men) against a Medium AI (slot 1, Mordor) on Evendim
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

// the human's commands: every 20 frames its first mobile objects are selected and moved around its start (some attack-move toward the AI)
void humanCommands(LiveGame &game, CommandList &out)
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

// lane SPELL-2: the human's spell book through the command path: every 10 frames it buys the first science of its book it can buy now and casts one
// of its ready powers next to its first structure (MSG_DO_SPECIAL_POWER_AT_LOCATION from the book, the HUD's format); the powers cast are collected
Player *humanOf(GameLogic &logic)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p->getPlayerType() == PLAYER_HUMAN && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide)
		{
			return p;
		}
	}
	return nullptr;
}

void spellCommands(LiveGame &game, CommandList &out, std::map<std::string, int> *cast)
{
	const UnsignedInt f = game.frame();
	if (f == 0 || f % 10 != 5)
	{
		return;
	}
	GameLogic &logic = game.logic();
	Player *human = humanOf(logic);
	Object *book = human ? SpecialPowerModules::findSpellBookObject(logic, *human) : nullptr;
	if (!book)
	{
		return;
	}
	std::vector<const SpecialPowerTemplate *> ready;
	bool bought = false;
	for (const auto &m : book->modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
		if (!t)
		{
			continue;
		}
		bool owned = t->getRequiredSciences().empty();
		for (ScienceType st : t->getRequiredSciences())
		{
			owned = owned || human->science().hasScience(st);
			if (!bought && human->science().isCapableOfPurchasingScience(st))
			{
				GameMessage m2(MSG_PURCHASE_SCIENCE, human->getPlayerIndex());
				m2.appendIntegerArgument(human->getPlayerIndex());
				m2.appendIntegerArgument(st);
				out.append(m2);
				bought = true;
			}
		}
		if (owned && sp->isReadyForDisplay() && SpecialPowerModules::canUseSpecialPower(*book, t))
		{
			ready.push_back(t);
		}
	}
	if (ready.empty())
	{
		return;
	}
	Coord3D at{ 0, 0, 0 };
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == human && o->isKindOfName("STRUCTURE"))
		{
			at = *o->getPosition();
			break;
		}
	}
	const SpecialPowerTemplate *t = ready[(f / 10) % ready.size()];
	at.x += 150.0f;
	at.y += 150.0f;
	at.z = logic.getGroundHeight(at.x, at.y);
	GameMessage c(MSG_DO_SPECIAL_POWER_AT_LOCATION, human->getPlayerIndex());
	c.appendIntegerArgument((int)t->getID());
	c.appendLocationArgument(at);
	c.appendObjectIDArgument(INVALID_ID);
	c.appendIntegerArgument(0);
	c.appendObjectIDArgument(book->getID());
	out.append(c);
	if (cast)
	{
		++(*cast)[t->getName()];
	}
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

std::string readFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// "key value" lines of a peer report
std::string field(const std::string &report, const std::string &key)
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

int runCommand(const std::string &cmdIn)
{
	std::string cmd = cmdIn;
#ifdef _WIN32
	cmd = "\"" + cmd + "\"";
	return std::system(cmd.c_str());
#else
	const int rc = std::system(cmd.c_str());
	return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

#ifdef OPENBFME_PEER_EXE
std::string peerBase()
{
	return std::string("\"") + OPENBFME_PEER_EXE + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
}

int testPort()
{
#ifdef _WIN32
	const int pid = (int)std::chrono::steady_clock::now().time_since_epoch().count();
#else
	const int pid = (int)getpid();
#endif
	return 20000 + (pid % 20000 + 20000) % 20000;
}

struct PeerRun
{
	int hostRc = -1, joinRc = -1;
	std::string hostReport, joinReport, hostHashes, joinHashes;
	std::vector<int> rcs;                  ///< every peer, the host first
	std::vector<std::string> reports, hashes;
};

// a host and `joinExtras.size()` joiners, each its own process; joinExtras[i] goes to joiner i's command line only
PeerRun runPeers(const std::string &tag, int frames, const std::string &common, const std::string &hostExtra, const std::vector<std::string> &joinExtras)
{
	const int port = testPort();
	PeerRun r;
	const size_t n = 1 + joinExtras.size();
	r.rcs.assign(n, -1);
	std::vector<std::string> reportFiles, hashFiles, commands;
	for (size_t i = 0; i < n; ++i)
	{
		reportFiles.push_back("mp1_" + tag + "_" + std::to_string(i) + "_report.txt");
		hashFiles.push_back("mp1_" + tag + "_" + std::to_string(i) + "_hashes.txt");
		const std::string role = i == 0 ? " --host " + std::to_string(port) + " " + hostExtra
										: " --join 127.0.0.1:" + std::to_string(port) + " --name Joiner" + std::to_string(i) + " " + joinExtras[i - 1];
		// the role and its extras last: a peer's own --frames overrides the common one
		commands.push_back(peerBase() + " --frames " + std::to_string(frames) + " " + common + role + " --report " + reportFiles[i] + " --hashes " + hashFiles[i]);
	}
	std::vector<std::thread> threads;
	for (size_t i = 0; i < n; ++i)
	{
		threads.emplace_back([&, i] { r.rcs[i] = runCommand(commands[i]); });
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}
	for (std::thread &t : threads)
	{
		t.join();
	}
	for (size_t i = 0; i < n; ++i)
	{
		r.reports.push_back(readFile(reportFiles[i]));
		r.hashes.push_back(readFile(hashFiles[i]));
		std::remove(reportFiles[i].c_str());
		std::remove(hashFiles[i].c_str());
	}
	r.hostRc = r.rcs[0];
	r.hostReport = r.reports[0];
	r.hostHashes = r.hashes[0];
	if (n > 1)
	{
		r.joinRc = r.rcs[1];
		r.joinReport = r.reports[1];
		r.joinHashes = r.hashes[1];
	}
	return r;
}

// the archive's features were in play (lane MP-1 merge): the scripted humans cast spells, research upgrades, queue units, build with their builders and
// on fortress pads (wall hubs when offered) and the logic built some of it
void checkFeatures(const std::vector<std::string> &reports)
{
	auto count = [&](const std::string &type) {
		long n = 0;
		for (const std::string &rep : reports)
		{
			n += std::atol(field(rep, "scripted_type " + type).c_str());
		}
		return n;
	};
	CHECK(count("MSG_DO_SPECIAL_POWER_AT_LOCATION") >= 1);
	CHECK(count("MSG_PURCHASE_SCIENCE") >= 1);
	CHECK(count("MSG_QUEUE_UPGRADE") >= 1);
	CHECK(count("MSG_QUEUE_UNIT_CREATE") >= 1);
	CHECK(count("MSG_DOZER_CONSTRUCT") >= 1);
	CHECK(count("MSG_FOUNDATION_CONSTRUCT") >= 1);
	const std::string b = field(reports[0], "build"); // "foundations F dozer_builds D wall_spans W refused R"
	MESSAGE("build: " << b << "; features: " << field(reports[0], "features"));
	CHECK(std::atoi(b.substr(b.find("foundations ") + 12).c_str()) >= 1);
	CHECK(std::atoi(b.substr(b.find("dozer_builds ") + 13).c_str()) >= 1);
}

PeerRun runPair(const std::string &tag, int frames, const std::string &common, const std::string &hostExtra, const std::string &extraJoin)
{
	return runPeers(tag, frames, common, hostExtra, { extraJoin });
}
#endif
} // namespace

TEST_CASE("net retail: a recorded skirmish (human commands + Medium AI) plays back with every frame's hash equal; a changed state is found at its frame")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 900;
	const std::string path = "mp1_inprocess.replay";
	const NewGameMessage message = gameMessage(*s, 1234u);
	std::uint32_t recordedFinal = 0;
	UnsignedInt startFrame = 0; // the logic frame of a loaded game (the load runs the frame that creates the objects)
	unsigned long long recordedCommands = 0;
	{
		Loaded g;
		load(*s, message, g);
		ReplayWriter writer;
		ReplayHeader h;
		h.game = message;
		h.recordingSlot = 0;
		std::string error;
		REQUIRE_MESSAGE(writer.open(path, h, &error), error);
		LockstepDriver driver(nullptr, &writer); // a local game being recorded
		startFrame = g.game->frame();
		g.game->setFrameDriver(&driver);
		for (int i = 0; i < kFrames; ++i)
		{
			humanCommands(*g.game, g.game->commands());
			REQUIRE(g.game->advance(0.2) == 1);
		}
		recordedFinal = g.game->logic().computeStateHash();
		writer.close(g.game->frame(), recordedFinal);
		CHECK(writer.errors().empty());
		g.game->setFrameDriver(nullptr);
	}
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
	recordedCommands = replay.commandCount();
	CHECK(replay.complete);
	CHECK(replay.finalFrame == startFrame + (UnsignedInt)kFrames);
	CHECK(replay.hashes.size() == (size_t)kFrames);
	CHECK(recordedCommands >= 40); // the human's selections and moves (the AI acts inside the logic, not through commands)
	{
		Loaded g;
		load(*s, replay.header.game, g);
		ReplayPlayback playback(replay);
		RegisterLogicCRCHandler(g.game->dispatch());
		g.game->setFrameDriver(&playback);
		while (!playback.finished(g.game->logic()))
		{
			humanCommands(*g.game, g.game->commands()); // local input during playback is ignored
			g.game->advance(0.2);
		}
		MESSAGE("replay: " << recordedCommands << " commands, " << playback.hashesCompared() << " hashes compared, " << playback.localInputIgnored()
						   << " local messages ignored");
		CHECK(playback.hashesCompared() == (unsigned long long)kFrames);
		CHECK_FALSE(playback.hasMismatch());
		CHECK(g.game->logic().computeStateHash() == recordedFinal);
		CHECK(playback.localInputIgnored() > 0);
		g.game->setFrameDriver(nullptr);
	}
	{
		// the regression tool finds a divergence at its frame: one unit of money more before frame 300 runs
		Loaded g;
		load(*s, replay.header.game, g);
		ReplayPlayback playback(replay);
		RegisterLogicCRCHandler(g.game->dispatch());
		g.game->setFrameDriver(&playback);
		while (!playback.finished(g.game->logic()))
		{
			if (g.game->frame() == 300)
			{
				Player *p = g.game->logic().players().getNthPlayer(1);
				p->getMoney()->deposit(1, false);
			}
			g.game->advance(0.2);
		}
		REQUIRE(playback.hasMismatch());
		CHECK(playback.firstMismatch().frame == 301u); // the hash after frame 300 ran
		g.game->setFrameDriver(nullptr);
	}
	std::remove(path.c_str());
}

TEST_CASE("net retail (SPELL-2): a recorded skirmish whose human buys and casts its spell book powers plays back with every frame's hash equal")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 600;
	const std::string path = "spell2_inprocess.replay";
	const NewGameMessage message = gameMessage(*s, 4321u);
	std::map<std::string, int> cast;
	std::uint32_t recordedFinal = 0;
	// the human's purchase points (a game earns them by rank): the same harness step in the recording and in the playback, before frame 1
	auto givePoints = [](LiveGame &game) {
		Player *h = humanOf(game.logic());
		REQUIRE(h);
		h->science().addSciencePurchasePoints(200);
	};
	{
		Loaded g;
		load(*s, message, g);
		givePoints(*g.game);
		ReplayWriter writer;
		ReplayHeader h;
		h.game = message;
		h.recordingSlot = 0;
		std::string error;
		REQUIRE_MESSAGE(writer.open(path, h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		g.game->setFrameDriver(&driver);
		for (int i = 0; i < kFrames; ++i)
		{
			spellCommands(*g.game, g.game->commands(), &cast);
			REQUIRE(g.game->advance(0.2) == 1);
		}
		recordedFinal = g.game->logic().computeStateHash();
		writer.close(g.game->frame(), recordedFinal);
		CHECK(writer.errors().empty());
		g.game->setFrameDriver(nullptr);
	}
	std::string names;
	for (const auto &c : cast)
	{
		names += c.first + " x" + std::to_string(c.second) + " ";
	}
	MESSAGE("SPELL-2 lockstep: powers cast " << names);
	CHECK(cast.size() >= 6u);
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
	{
		Loaded g;
		load(*s, replay.header.game, g);
		givePoints(*g.game);
		ReplayPlayback playback(replay);
		RegisterLogicCRCHandler(g.game->dispatch());
		g.game->setFrameDriver(&playback);
		while (!playback.finished(g.game->logic()))
		{
			g.game->advance(0.2);
		}
		CHECK(playback.hashesCompared() == (unsigned long long)kFrames);
		CHECK_FALSE(playback.hasMismatch());
		CHECK(g.game->logic().computeStateHash() == recordedFinal);
		g.game->setFrameDriver(nullptr);
	}
	std::remove(path.c_str());
}

#ifdef OPENBFME_PEER_EXE
TEST_CASE("net retail: two peer processes on localhost play 5 minutes of a 4-player skirmish (2 scripted humans, 2 Medium AIs) in lockstep, and the recording "
		  "plays back in a third")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 1500; // 5 minutes at 5 logic frames per second
	const std::string replay = "mp1_lockstep.replay";
	const std::string common = "--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 77 --script --crc-interval 100 --run-ahead 2 --cash 10000";
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot medium,FactionElves,2,0 --slot medium,FactionWild,3,1";
	const auto t0 = std::chrono::steady_clock::now();
	PeerRun r = runPair("lockstep", kFrames, common, slots + " --record " + replay, "");
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	MESSAGE("host:\n" << r.hostReport);
	MESSAGE("joiner:\n" << r.joinReport);
	MESSAGE("two peers: " << seconds << " s wall clock");
	CHECK(r.hostRc == 0);
	CHECK(r.joinRc == 0);
	CHECK(field(r.hostReport, "frames") == std::to_string(kFrames));
	CHECK(field(r.joinReport, "frames") == std::to_string(kFrames));
	CHECK(field(r.hostReport, "desyncs") == "0");
	CHECK(field(r.joinReport, "desyncs") == "0");
	CHECK(field(r.hostReport, "crc_checks_passed") == "14");
	CHECK(field(r.joinReport, "crc_checks_passed") == "14");
	CHECK(field(r.hostReport, "final_hash") == field(r.joinReport, "final_hash"));
	CHECK(!r.hostHashes.empty());
	CHECK(r.hostHashes == r.joinHashes); // every frame
	CHECK(std::atoi(field(r.hostReport, "scripted_commands").c_str()) > 50);
	CHECK(field(r.hostReport, "build") == field(r.joinReport, "build")); // shared state
	checkFeatures({ r.hostReport, r.joinReport });
	CHECK(std::atoi(field(r.joinReport, "scripted_commands").c_str()) > 50);
	CHECK(field(r.hostReport, "commands_relayed") == field(r.joinReport, "commands_relayed"));
	CHECK(r.hostReport.find("network_error") == std::string::npos);
	CHECK(r.joinReport.find("network_error") == std::string::npos);

	// the host's recording, played back by a third process
	const std::string rr = "mp1_replay_report.txt";
	const int rc = runCommand(peerBase() + " --replay " + replay + " --report " + rr);
	const std::string rep = readFile(rr);
	MESSAGE("replay:\n" << rep);
	CHECK(rc == 0);
	CHECK(field(rep, "mismatches") == "0");
	CHECK(field(rep, "hashes_compared") == std::to_string(kFrames - 1)); // the game starts on frame 1
	CHECK(field(rep, "final_hash").substr(0, 10) == field(r.hostReport, "final_hash"));
	std::remove(rr.c_str());
	std::remove(replay.c_str());
}

// lane PERF-2: the logic job pool's thread count is a local choice, never part of the game: a host on 1 logic thread and a joiner on every hardware
// thread (the parallel collision pass in a battle of four armies) stay in lockstep in every frame
TEST_CASE("net retail perf2: two peer processes with different logic thread counts (1 and N) play a 4-player skirmish in lockstep, every frame's hash equal")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 1000;
	const int n = std::max(2, (int)std::thread::hardware_concurrency());
	const std::string common = "--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 77 --script --crc-interval 100 --run-ahead 2 --cash 10000";
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot medium,FactionElves,2,0 --slot medium,FactionWild,3,1";
	PeerRun r = runPair("perf2threads", kFrames, common, slots + " --logic-threads 1", "--logic-threads " + std::to_string(n));
	MESSAGE("host:\n" << r.hostReport);
	MESSAGE("joiner:\n" << r.joinReport);
	CHECK(r.hostRc == 0);
	CHECK(r.joinRc == 0);
	CHECK(field(r.hostReport, "logic_threads") == "1");
	CHECK(field(r.joinReport, "logic_threads") == std::to_string(n));
	CHECK(field(r.hostReport, "frames") == std::to_string(kFrames));
	CHECK(field(r.joinReport, "frames") == std::to_string(kFrames));
	CHECK(field(r.hostReport, "desyncs") == "0");
	CHECK(field(r.joinReport, "desyncs") == "0");
	CHECK(field(r.hostReport, "crc_checks_passed") == "9");
	CHECK(field(r.joinReport, "crc_checks_passed") == "9");
	CHECK(field(r.hostReport, "final_hash") == field(r.joinReport, "final_hash"));
	CHECK(!r.hostHashes.empty());
	CHECK(r.hostHashes == r.joinHashes); // every frame
}

TEST_CASE("net retail: a divergence injected on one peer is caught at the next CRC frame; both peers report the frame, both hashes and the diverging subsystem")
{
	OPENBFME_REQUIRE_START(s);
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 5 --script --crc-interval 100 --run-ahead 2";
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1";
	PeerRun r = runPair("desync", 400, common, slots, "--inject-divergence 150");
	// lane MP-2: both peers wrote RW 0x6290C7's desync dumps into the current directory (the peer's --desync-dir default)
	for (const char *dump : { "DESYNC-Frame202-openbfme_peer-Human0.txt", "DESYNC-Frame202-openbfme_peer-Joiner1.txt",
			 "DESYNC-Frame302-openbfme_peer-Human0.txt", "DESYNC-Frame302-openbfme_peer-Joiner1.txt" })
	{
		std::remove(dump);
	}
	MESSAGE("host:\n" << r.hostReport);
	MESSAGE("joiner:\n" << r.joinReport);
	CHECK(r.hostRc == 2);
	CHECK(r.joinRc == 2);
	for (const std::string *rep : { &r.hostReport, &r.joinReport })
	{
		CHECK(field(*rep, "crc_checks_passed") == "1"); // frame 100 agreed
		CHECK(field(*rep, "desyncs") != "0");
		CHECK(rep->find("DESYNC: the state hash of logic frame 200 differs (CRCs compared on frame 202)") != std::string::npos);
		CHECK(rep->find("players and teams") != std::string::npos);
		CHECK(rep->find("<== DIFFERS") != std::string::npos);
	}
	// only the players section differs (the injected money); the objects and the RNG agree
	const size_t at = r.hostReport.find("players and teams");
	REQUIRE(at != std::string::npos);
	const std::string line = r.hostReport.substr(at, r.hostReport.find('\n', at) - at);
	CHECK(line.find("<== DIFFERS") != std::string::npos);
	CHECK(r.hostReport.find("objects that differ: 0") != std::string::npos);
	// the hashes agree up to the injection and differ after it
	std::istringstream hh(r.hostHashes), jh(r.joinHashes);
	std::string a, b;
	int firstDiff = -1;
	while (std::getline(hh, a) && std::getline(jh, b))
	{
		if (a != b && firstDiff < 0)
		{
			firstDiff = std::atoi(a.c_str()); // "<frame> <hash>"
		}
	}
	CHECK(firstDiff == 150); // the joiner added the money when the logic reached frame 150
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("net retail: a headless peer without the full retail data (the W3D archives the launch bones need) is a startup error, not a diverging peer")
{
	OPENBFME_REQUIRE_START(s);
	// an install directory without any archive: the md5-verified archive policy refuses the mount, so the peer never runs a frame
	const std::string empty = "mp1_empty_install";
#ifdef _WIN32
	runCommand("mkdir " + empty);
#else
	runCommand("mkdir -p " + empty);
#endif
	const std::string out = "mp1_empty_install.txt";
	const int rc = runCommand(std::string("\"") + OPENBFME_PEER_EXE + "\" --rotwk " + empty + " --bfme2 \"" + std::getenv("BFME2_INSTALL")
		+ "\" --host 1 --slot human,FactionMen,0,0 2> " + out);
	const std::string text = readFile(out);
	MESSAGE(text);
	CHECK(rc == 1);
	CHECK(text.find("the retail mount failed (a headless peer needs every archive a rendering peer reads, the W3D archives included)") != std::string::npos);
	std::remove(out.c_str());
#ifdef _WIN32
	runCommand("rmdir " + empty);
#else
	runCommand("rmdir " + empty);
#endif
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("net retail: three peer processes (3 scripted humans and a Medium AI) stay in lockstep for 1500 frames with the archive's features in play")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 1500;
	const std::string common = "--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 9 --script --crc-interval 100 --run-ahead 3 --cash 10000";
	const std::string slots = "--slot human,FactionDwarves,0,0 --slot human,FactionIsengard,1,1 --slot human,FactionAngmar,2,0 --slot medium,FactionMen,3,1";
	PeerRun r = runPeers("three", kFrames, common, slots, { "", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << ":\n" << r.reports[i]);
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "frames") == std::to_string(kFrames));
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == "14");
		CHECK(field(r.reports[i], "final_hash") == field(r.reports[0], "final_hash"));
		CHECK(r.hashes[i] == r.hashes[0]);
		CHECK(std::atoi(field(r.reports[i], "scripted_commands").c_str()) > 20);
		CHECK(field(r.reports[i], "build") == field(r.reports[0], "build")); // shared state
	}
	checkFeatures(r.reports);
}
#endif

#ifdef OPENBFME_PEER_EXE
// lane HERO-2 (review r1, fix 1): a Create-a-Hero on one slot of a LAN game travels with the game setup: the host reads the .cah at the setup, the START
// carries the whole record, the joiner (which never sees the file) installs the same record; every frame's hash is equal. The record is a system hero
// changed here (another name, unique id and power), so no archive of either peer holds it
TEST_CASE("net retail (HERO-2): a LAN game with a Create-a-Hero on the joiner's slot: the record comes from the host's setup, the joiner without the .cah "
		  "file gets the hero, every frame's hash is equal")
{
	OPENBFME_REQUIRE_START(s);
	std::vector<std::string> loadErrors;
	const std::vector<CreateAHeroHero> heroes = CreateAHeroLibrary::systemHeroes(*s->mount->fs, &loadErrors);
	CreateAHeroHero hero;
	for (const CreateAHeroHero &h : heroes)
	{
		if (h.name == u"Berethor")
		{
			hero = h;
		}
	}
	REQUIRE(hero.name == u"Berethor");
	hero.name = u"Lanhero";
	hero.uniqueID = "HERO2LANTEST0001";
	hero.isSystemHero = false;
	hero.powers[2].commandButton = "Command_CreateAHeroAssassin_Level1";
	const std::string cah = "mp1_hero2_lanhero.cah";
	{
		const std::vector<std::uint8_t> bytes = hero.save();
		std::ofstream f(cah, std::ios::binary | std::ios::trunc);
		f.write((const char *)bytes.data(), (std::streamsize)bytes.size());
	}
	const int kFrames = 1200;
	const std::string common = "--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 21 --script --crc-interval 100 --run-ahead 2 --cash 10000";
	const std::string slots = "--slot human,FactionMordor,0,0 --slot human,FactionMen,1,1 --slot medium,FactionElves,2,0";
	PeerRun r = runPair("hero2cah", kFrames, common, slots + " --create-a-hero 1:" + cah, "--recruit-create-a-hero 100");
	std::remove(cah.c_str());
	MESSAGE("host:\n" << r.hostReport);
	MESSAGE("joiner:\n" << r.joinReport);
	CHECK(r.hostRc == 0);
	CHECK(r.joinRc == 0);
	CHECK(field(r.hostReport, "frames") == std::to_string(kFrames));
	CHECK(field(r.joinReport, "frames") == std::to_string(kFrames));
	CHECK(field(r.hostReport, "desyncs") == "0");
	CHECK(field(r.joinReport, "desyncs") == "0");
	CHECK(field(r.hostReport, "final_hash") == field(r.joinReport, "final_hash"));
	CHECK(!r.hostHashes.empty());
	CHECK(r.hostHashes == r.joinHashes); // every frame
	const std::string host = field(r.hostReport, "create_a_hero"), join = field(r.joinReport, "create_a_hero");
	CHECK(join == host);
	char checksum[16];
	std::snprintf(checksum, sizeof(checksum), "0x%08X", hero.computeChecksum());
	CHECK(join.find(" Lanhero HERO2LANTEST0001 checksum " + std::string(checksum)) != std::string::npos);
	CHECK(join.find(" surcharge 0") == std::string::npos); // the record's power cost (RW 0x809CA6) on the joiner too
	// the joiner recruited it at its fortress (it could: RW 0x61B103's CanBuildCreateAHeroUpgradeName from the carried record); a multiplayer game takes the
	// upgrade away once the hero exists (RW 0x6AE60C): the hero of the carried record plays on both peers
	CHECK(join.find(" can_build 0 made 1") != std::string::npos);
	MESSAGE("the joiner's Create-a-Hero: " << join);
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("net retail smooth1: two peer processes with the logic worker (different render rates and worker delays, lossy reordering links, a pause, driver "
		  "replacement, a worker switch) stay in lockstep with equal hashes, RNG and command records at every frame; the recording plays back")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 400;
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 31 --script --crc-interval 50 --run-ahead 2 --logic-thread --drop 100 --jitter 20";
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1";
	const std::string hostReplay = "smooth1_host.replay", joinReplay = "smooth1_join.replay";
	PeerRun r = runPair("smooth1", kFrames, common,
		slots + " --record " + hostReplay + " --step-ms 16 --worker-delay-ms 15 --pause-at 120:600 --replace-driver-at 200",
		"--record " + joinReplay + " --step-ms 45 --toggle-thread-at 150");
	MESSAGE("host:\n" << r.hostReport.substr(0, r.hostReport.find("stop [S-720]")));
	MESSAGE("joiner:\n" << r.joinReport.substr(0, r.joinReport.find("stop [S-720]")));
	CHECK(r.hostRc == 0);
	CHECK(r.joinRc == 0);
	for (const std::string *rep : { &r.hostReport, &r.joinReport })
	{
		CHECK(field(*rep, "frames") == std::to_string(kFrames));
		CHECK(field(*rep, "desyncs") == "0");
		CHECK(field(*rep, "crc_checks_passed") == "7");
		CHECK(rep->find("network_error") == std::string::npos);
	}
	CHECK(field(r.hostReport, "final_hash") == field(r.joinReport, "final_hash"));
	REQUIRE(!r.hostHashes.empty());
	CHECK(r.hostHashes == r.joinHashes); // "<frame> <hash> <rng>" after every frame
	CHECK(field(r.hostReport, "commands_relayed") == field(r.joinReport, "commands_relayed"));
	// both peers recorded every batch: the same ordered command record and hashes
	ReplayFile hr, jr;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(hostReplay, hr, &error), error);
	REQUIRE_MESSAGE(ReplayFile::load(joinReplay, jr, &error), error);
	CHECK(hr.hashes == jr.hashes);
	CHECK(hr.hashes.size() == (size_t)(kFrames - 1));
	REQUIRE(hr.frames.size() == jr.frames.size());
	for (auto a = hr.frames.begin(), b = jr.frames.begin(); a != hr.frames.end(); ++a, ++b)
	{
		CHECK(a->first == b->first);
		REQUIRE(a->second.size() == b->second.size());
		for (size_t i = 0; i < a->second.size(); ++i)
		{
			NetByteWriter wa, wb;
			NetPacket::writeGameMessage(wa, a->second[i], nullptr);
			NetPacket::writeGameMessage(wb, b->second[i], nullptr);
			CHECK(wa.take() == wb.take());
		}
	}
	CHECK(hr.commandCount() > 20);
	// the single-thread baseline: the host's recording played back by a third process
	const std::string rr = "smooth1_replay_report.txt";
	const int rc = runCommand(peerBase() + " --replay " + hostReplay + " --report " + rr);
	const std::string rep = readFile(rr);
	CHECK(rc == 0);
	CHECK(field(rep, "mismatches") == "0");
	CHECK(field(rep, "hashes_compared") == std::to_string(kFrames - 1));
	CHECK(field(rep, "final_hash").substr(0, 10) == field(r.hostReport, "final_hash"));
	std::remove(rr.c_str());
	std::remove(hostReplay.c_str());
	std::remove(joinReplay.c_str());
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("net retail r1 (fix 5): the host leaves at frame 101 of three peers; the survivors play on to 1500 with equal hashes, the departed peer retired and "
		  "their queues and resends bounded")
{
	OPENBFME_REQUIRE_START(s);
	const std::string common = "--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 21 --script --crc-interval 100 --run-ahead 2 --drop 100 --delay 5";
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot human,FactionElves,2,0 --slot medium,FactionWild,3,1";
	PeerRun r = runPeers("leave", 1500, common, slots + " --frames 101", { "", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << ":\n" << r.reports[i].substr(0, r.reports[i].find("stop [S-720]")));
		CHECK(r.rcs[i] == 0);
	}
	CHECK(field(r.reports[0], "frames") == "101");
	for (size_t i = 1; i < r.reports.size(); ++i)
	{
		CHECK(field(r.reports[i], "frames") == "1500");
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == "14");
		// the host, and the survivor that finished first (it leaves at frame 1500 as well) for the one that finished last
		CHECK(std::atoi(field(r.reports[i], "peers_retired").c_str()) >= 1);
		CHECK(std::atoi(field(r.reports[i], "transport_unacked").c_str()) < 100);
		CHECK(field(r.reports[i], "final_hash") == field(r.reports[1], "final_hash"));
		// resends stay in proportion to the traffic (10 % loss), not to the time since the host left
		const std::string t = field(r.reports[i], "transport");
		const long resent = std::atol(t.substr(t.find("resent ") + 7).c_str());
		CHECK(resent < 3000);
	}
	CHECK(r.hashes[1] == r.hashes[2]);
}

TEST_CASE("net retail r1 (fix 3): peers of different profile identities refuse to play; a replay of another identity is refused before the game loads")
{
	OPENBFME_REQUIRE_START(s);
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 3 --lobby-timeout 25";
	PeerRun r = runPeers("profile", 50, common, "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --record mp1_profile.replay", { "--feature test-feature" });
	CHECK(r.rcs[0] == 3); // the host's game never filled
	CHECK(r.rcs[1] == 3); // the joiner was refused
	CHECK(r.reports[0].empty()); // nothing was loaded
	// a replay recorded by this build, then played by a build with another feature
	PeerRun ok = runPeers("profile2", 50, common, "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --record mp1_profile.replay", { "" });
	REQUIRE(ok.rcs[0] == 0);
	const std::string out = "mp1_profile_replay.txt";
	const int same = runCommand(peerBase() + " --replay mp1_profile.replay --report " + out);
	CHECK(same == 0);
	const int other = runCommand(peerBase() + " --feature test-feature --replay mp1_profile.replay --report " + out + " 2> mp1_profile_err.txt");
	const std::string err = readFile("mp1_profile_err.txt");
	MESSAGE(err);
	CHECK(other == 1);
	CHECK(err.find("the replay was recorded by another profile") != std::string::npos);
	CHECK(err.find("test-feature") != std::string::npos);
	for (const char *f : { "mp1_profile.replay", "mp1_profile_replay.txt", "mp1_profile_err.txt" })
	{
		std::remove(f);
	}
}
#endif

TEST_CASE("net retail (merge): the per-subsystem breakdown covers every word of the state hash, the archive's subsystems included (shroud, experience)")
{
	OPENBFME_REQUIRE_START(s);
	Loaded g;
	load(*s, gameMessage(*s, 1234u), g);
	for (int i = 0; i < 120; ++i)
	{
		humanCommands(*g.game, g.game->commands());
		g.game->advance(0.2);
	}
	std::vector<GameLogic::StateHashSection> sections;
	std::vector<GameLogic::ObjectStateHash> objects;
	const std::uint32_t total = g.game->logic().computeStateHashBreakdown(sections, &objects);
	CHECK(total == g.game->logic().computeStateHash());
	REQUIRE(!sections.empty());
	// the sections chain: the first starts at 0, each starts where the previous ended, the last ends at the total: no word of the total is outside a section
	CHECK(sections.front().chainBefore == 0u);
	for (size_t i = 1; i < sections.size(); ++i)
	{
		CHECK_MESSAGE(sections[i].chainBefore == sections[i - 1].chainAfter, "between " << sections[i - 1].name << " and " << sections[i].name);
	}
	CHECK(sections.back().chainAfter == total);
	std::vector<std::string> names;
	for (const auto &sec : sections)
	{
		names.push_back(sec.name);
	}
	MESSAGE("sections: " << [&] { std::string t; for (const auto &n : names) t += n + "; "; return t; }());
	// the archive's order: frame/ids/RNG, scheduler, objects, the contributors (pathfinder, shroud), players, settings, economy, combat, victory, skirmish AI,
	// experience, weather (lane SPELL-2: TheGlobalWeatherSystem), invisibility (lane STEALTH-1), the map script engine (lane SCRIPT-1: a game whose scripts run)
	const std::vector<std::string> fixed = { "frame, object ids, RNG", "scheduler", "objects" };
	CHECK(std::vector<std::string>(names.begin(), names.begin() + 3) == fixed);
	// lane AUDIO-4: TheLargeGroupAudio's gate after the invisibility
	const std::vector<std::string> tail = { "players and teams", "settings", "economy", "combat", "victory", "skirmish AI", "experience", "weather", "invisibility",
		"large group audio", "script engine" };
	REQUIRE(names.size() >= tail.size() + 3);
	CHECK(std::vector<std::string>(names.end() - (long)tail.size(), names.end()) == tail);
	CHECK(std::find(names.begin(), names.end(), "shroud") != names.end());
	CHECK(std::find(names.begin(), names.end(), "pathfinder and AI movers") != names.end());
	for (const auto &n : names)
	{
		CHECK_MESSAGE(n.rfind("contributor ", 0) != 0, "an unnamed contributor: " << n);
	}
	CHECK(objects.size() == g.game->logic().report().objects);
}
