// OpenBFME unit tests. GPL-3.0.
// Lane ANTICHEAT-1 on the retail game: detector D1 (GameNetwork/OrdersIntoFog.h) over recorded skirmishes on Evendim (a human, Men, against a Medium
// AI, Mordor). Two recordings of the same game: normal play (the human's units move around and attack only enemies it sees) and a cheater-like
// stream (the same, plus attacks on the AI's buildings and units the human never saw, in three fights minutes apart). Each replay is played back
// through FogCheckDriver as openbfme_peer --fog-check does: the playback must reproduce every recorded hash, the normal game must have no event, the
// cheater-like one must reach the alert band with never_seen events naming the AI's objects. SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "GameNetwork/ProfileIdentity.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameNetwork/FogCheckDriver.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/OrdersIntoFog.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
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

const Player *humanOf(GameLogic &logic)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p->getPlayerType() == PLAYER_HUMAN && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide)
		{
			return p;
		}
	}
	return nullptr;
}

// the human's own mobile units as a new selection (the order acts on the selection)
bool selectArmy(GameLogic &logic, const Player *human, CommandList &out, Coord3D *at)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, human->getPlayerIndex());
	sel.appendBooleanArgument(true);
	int n = 0;
	for (Object *o = logic.getFirstObject(); o && n < 10; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == human && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE"))
		{
			sel.appendObjectIDArgument(o->getID());
			if (at)
			{
				*at = *o->getPosition();
			}
			++n;
		}
	}
	if (n > 0)
	{
		out.append(sel);
	}
	return n > 0;
}

struct Plan
{
	bool cheat = false;
	std::vector<UnsignedInt> cheatFrames; // filled while recording: the frames the never-seen orders were issued on
	std::set<ObjectID> cheatTargets;
	int visibleAttacks = 0;
	int burstCommands = 0; ///< the macro burst's commands (D5)
	int views = 0;         ///< the views recorded (D2)
	std::string hoverTemplate; ///< the cheater's hover target (D2)
	float hoverX = 0.0f, hoverY = 0.0f;
	UnsignedInt hoverFirst = 0, hoverLast = 0; ///< the frames of the first and the last hover view
	UnsignedInt afterHover = 0;                ///< the frame of the first view after the hover (0: none; the plan records a view only while the human has a mobile unit)
};

// lane ANTICHEAT-1 (D2): the view a client would report every other frame: the camera and pointer over the human's own first unit; for the cheater,
// from 1000 frames in for 30 frames, the pointer over a building of the AI the human never saw (a map hacker looking at the enemy base)
void recordView(LiveGame &game, ReplayWriter &writer, Plan &plan, UnsignedInt startFrame)
{
	const UnsignedInt f = game.frame();
	if (f % 2 != 0)
	{
		return;
	}
	GameLogic &logic = game.logic();
	const Player *human = humanOf(logic);
	const ShroudManager *shroud = logic.shroud();
	ReplayFile::View v;
	v.frame = f;
	v.slot = 0;
	v.flags = 1;
	bool any = false;
	for (const Object *o = logic.getFirstObject(); o && !any; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == human && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE"))
		{
			v.cameraX = v.cursorX = o->getPosition()->x;
			v.cameraY = v.cursorY = o->getPosition()->y;
			any = true;
		}
	}
	const UnsignedInt rel = f - startFrame;
	if (plan.cheat && rel >= 1000 && rel < 1030)
	{
		for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			const Player *owner = o->getControllingPlayer();
			if (owner && owner != human && human->getRelationship(owner) == ENEMIES && owner->getPlayerTemplate() && owner->getPlayerTemplate()->m_playableSide &&
				o->isKindOfName("STRUCTURE") && !shroud->hasSeen(*o, human->getPlayerIndex()))
			{
				v.cursorX = o->getPosition()->x;
				v.cursorY = o->getPosition()->y;
				plan.hoverTemplate = o->getTemplate()->getName();
				plan.hoverX = v.cursorX;
				plan.hoverY = v.cursorY;
				plan.hoverFirst = plan.hoverFirst ? plan.hoverFirst : f;
				plan.hoverLast = f;
				any = true;
				break;
			}
		}
	}
	if (any && plan.hoverLast && rel >= 1030 && !plan.afterHover)
	{
		plan.afterHover = f;
	}
	if (any)
	{
		writer.recordView(v);
		++plan.views;
	}
}

// every 20 frames: the human's units move around their start; every 40 frames they attack an enemy object the human sees now (normal play); with
// `cheat`, at three frames minutes apart, they also attack the AI's objects the human has never seen (the cheater-like stream)
void humanCommands(LiveGame &game, CommandList &out, Plan &plan, UnsignedInt startFrame)
{
	const UnsignedInt f = game.frame();
	const UnsignedInt rel = f - startFrame;
	// the macro burst (D5): 30 moves a frame for 5 frames, 150 commands within one second of game time
	if (plan.cheat && rel >= 1200 && rel < 1205)
	{
		GameLogic &logic = game.logic();
		const Player *human = humanOf(logic);
		REQUIRE(human != nullptr);
		for (int i = 0; i < 30; ++i)
		{
			GameMessage mv(MSG_DO_MOVETO, human->getPlayerIndex());
			Coord3D at{ 1000.0f + 10.0f * (float)i, 1000.0f, 0.0f };
			at.z = logic.getGroundHeight(at.x, at.y);
			mv.appendLocationArgument(at);
			out.append(mv);
		}
		plan.burstCommands += 30;
		return;
	}
	const bool cheatNow = plan.cheat && (rel == 100 || rel == 800 || rel == 1500);
	if (!cheatNow && (f == 0 || f % 20 != 0))
	{
		return;
	}
	GameLogic &logic = game.logic();
	const Player *human = humanOf(logic);
	REQUIRE(human != nullptr);
	Coord3D at{ 0, 0, 0 };
	// the cheater's orders are judged as messages: they are issued whether or not an army is left to select
	if (!selectArmy(logic, human, out, &at) && !cheatNow)
	{
		return;
	}
	const ShroudManager *shroud = logic.shroud();
	REQUIRE(shroud != nullptr);
	const int hp = human->getPlayerIndex();
	if (!cheatNow && f % 40 == 0)
	{
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			const Player *owner = o->getControllingPlayer();
			const ObjectShroudStatus st = shroud->clientObjectStatus(*o, hp);
			if (owner && owner != human && human->getRelationship(owner) == ENEMIES && !o->isEffectivelyDead() &&
				(st == OBJECTSHROUD_CLEAR || st == OBJECTSHROUD_PARTIAL_CLEAR))
			{
				GameMessage atk(MSG_DO_ATTACK_OBJECT, hp);
				atk.appendObjectIDArgument(o->getID());
				out.append(atk);
				++plan.visibleAttacks;
				break;
			}
		}
	}
	if (cheatNow)
	{
		// the AI player's own buildings (its fortress, its farms ...), not the map's creep lairs: the building the player could not have known existed
		int n = 0;
		for (Object *o = logic.getFirstObject(); o && n < 2; o = o->getNextObject())
		{
			const Player *owner = o->getControllingPlayer();
			if (owner && owner != human && human->getRelationship(owner) == ENEMIES && owner->getPlayerTemplate() &&
				owner->getPlayerTemplate()->m_playableSide && o->isKindOfName("STRUCTURE") && !o->isEffectivelyDead() && !shroud->hasSeen(*o, hp) &&
				shroud->clientObjectStatus(*o, hp) == OBJECTSHROUD_SHROUDED)
			{
				GameMessage atk(MSG_DO_ATTACK_OBJECT, hp);
				atk.appendObjectIDArgument(o->getID());
				out.append(atk);
				plan.cheatTargets.insert(o->getID());
				++n;
			}
		}
		REQUIRE(n == 2); // two of the AI's objects the human never saw, in every fight
		plan.cheatFrames.push_back(f);
		return;
	}
	GameMessage mv(MSG_DO_MOVETO, hp);
	at.x += (f / 20) % 2 ? 120.0f : -120.0f;
	at.y += 60.0f;
	at.z = logic.getGroundHeight(at.x, at.y);
	mv.appendLocationArgument(at);
	out.append(mv);
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

std::string readText(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Checked
{
	std::vector<OrdersIntoFog::PlayerResult> results;
	std::string json;
	unsigned long long hashesCompared = 0;
	bool mismatch = false;
	bool abstained = false;
	std::string peerJson; ///< openbfme_peer --replay --fog-check's report of the same recording ("" when the executable is not built)
	std::string peerRate, peerCommands; ///< its --input-rate-check (D5) and --command-check (D6) reports of the same pass
	std::string peerViews;              ///< its --view-check (D2) report
	int peerRc = -1;
};

// records the game (kFrames), then plays the replay back with the check, as openbfme_peer --replay --fog-check does
Checked recordAndCheck(starttest::Shared &s, Plan &plan, const std::string &path, int frames)
{
	const NewGameMessage message = gameMessage(s, 2468u);
	{
		Loaded g;
		load(s, message, g);
		ReplayWriter writer;
		ReplayHeader h;
		h.game = message;
		h.recordingSlot = 0;
		h.network.slotPlayerIndex[0] = humanOf(g.game->logic())->getPlayerIndex(); // the judged human
#ifdef OPENBFME_PEER_EXE
		// the peer's identity (the archives it mounts, hashed): the game is recorded under it so that openbfme_peer plays it back
		const std::string profilePath = path + ".profile.txt";
		REQUIRE(runPeer("--profile-text \"" + profilePath + "\"") == 0);
		h.profile = ProfileIdentity::fromText(readText(profilePath));
		std::remove(profilePath.c_str());
#endif
		std::string error;
		REQUIRE_MESSAGE(writer.open(path, h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		const UnsignedInt startFrame = g.game->frame();
		g.game->setFrameDriver(&driver);
		for (int i = 0; i < frames; ++i)
		{
			humanCommands(*g.game, g.game->commands(), plan, startFrame);
			recordView(*g.game, writer, plan, startFrame);
			REQUIRE(g.game->advance(0.2) == 1);
		}
		writer.close(g.game->frame(), g.game->logic().computeStateHash());
		CHECK(writer.errors().empty());
		g.game->setFrameDriver(nullptr);
	}
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
	REQUIRE(replay.complete);
	Checked out;
	{
		Loaded g;
		load(s, replay.header.game, g);
		std::vector<int> humans;
		for (int p : replay.header.network.slotPlayerIndex)
		{
			if (p >= 0)
			{
				humans.push_back(p);
			}
		}
		REQUIRE(humans.size() == 1);
		OrdersIntoFog check(g.game->logic(), humans);
		ReplayPlayback playback(replay);
		RegisterLogicCRCHandler(g.game->dispatch());
		FogCheckDriver driver(playback, replay, check);
		driver.offer(g.game->logic());
		g.game->setFrameDriver(&driver);
		while (!playback.finished(g.game->logic()))
		{
			g.game->advance(0.2);
		}
		g.game->drainFrames();
		out.hashesCompared = playback.hashesCompared();
		out.mismatch = playback.hasMismatch();
		if (out.mismatch)
		{
			check.abstain("crc mismatch");
		}
		out.abstained = check.abstained();
		out.results = check.results();
		out.json = check.json("", out.mismatch ? "mismatch" : "match", g.game->frame());
		g.game->setFrameDriver(nullptr);
	}
#ifdef OPENBFME_PEER_EXE
	{
		// the real tool on the same recording: openbfme_peer --replay --fog-check (what the integrity worker runs)
		const std::string jsonPath = path + ".fog.json";
		const std::string ratePath = path + ".d5.json", commandPath = path + ".d6.json", viewPath = path + ".d2.json";
		out.peerRc = runPeer("--replay \"" + path + "\" --fog-check \"" + jsonPath + "\" --input-rate-check \"" + ratePath + "\" --command-check \"" +
			commandPath + "\" --view-check \"" + viewPath + "\" --game-id 00000000000ac001 --report \"" + path + ".report.txt\"");
		out.peerViews = readText(viewPath);
		std::remove(viewPath.c_str());
		out.peerJson = readText(jsonPath);
		out.peerRate = readText(ratePath);
		out.peerCommands = readText(commandPath);
		std::remove(jsonPath.c_str());
		std::remove(ratePath.c_str());
		std::remove(commandPath.c_str());
		std::remove((path + ".report.txt").c_str());
	}
#endif
	// OPENBFME_ANTICHEAT_OUT=DIR keeps the recording (DIR/<path>) for openbfme_peer --replay --fog-check and the evidence view
	if (const char *keep = std::getenv("OPENBFME_ANTICHEAT_OUT"))
	{
		std::ifstream in(path, std::ios::binary);
		std::ofstream o(std::string(keep) + "/" + path, std::ios::binary | std::ios::trunc);
		o << in.rdbuf();
	}
	std::remove(path.c_str());
	return out;
}
} // namespace

TEST_CASE("orders into fog retail: normal play on Evendim (moves, attacks on enemies the human sees) has no event; the playback reproduces every hash")
{
	OPENBFME_REQUIRE_START(s);
	Plan plan;
	const Checked c = recordAndCheck(*s, plan, "anticheat1_clean.replay", 1700);
	CHECK(c.hashesCompared == 1700u);
	CHECK_FALSE(c.mismatch);
	CHECK_FALSE(c.abstained);
	REQUIRE(c.results.size() == 1);
	const auto &r = c.results[0];
	MESSAGE("clean: judged " << r.judged << " (visible attacks issued " << plan.visibleAttacks << "), remembered " << r.remembered << ", stale " << r.stale
							 << ", abstained " << r.abstained);
	CHECK(r.events.empty());
	CHECK(r.band == "none");
	CHECK(r.judged == (unsigned long long)plan.visibleAttacks - r.stale);
	CHECK(plan.visibleAttacks > 0); // tolerance: how many enemies the human meets follows the AI's moves (pins move with the logic); the claim is that some were judged
#ifdef OPENBFME_PEER_EXE
	CHECK(c.peerRc == 0);
	CHECK(c.peerJson.find("\"status\":\"ok\"") != std::string::npos);
	CHECK(c.peerJson.find("\"crc_parity\":\"match\"") != std::string::npos);
	CHECK(c.peerJson.find("\"band\":\"none\"") != std::string::npos);
	CHECK(c.peerJson.find("\"events\":[]") != std::string::npos);
	// D5 / D6 in the same pass: an ordinary stream has no input-rate episode and no command a clean client does not send
	CHECK(c.peerRate.find("\"detector\":\"D5-input-rate\"") != std::string::npos);
	CHECK(c.peerRate.find("\"band\":\"none\"") != std::string::npos);
	CHECK(c.peerCommands.find("\"detector\":\"D6-command-integrity\"") != std::string::npos);
	CHECK(c.peerCommands.find("\"band\":\"none\"") != std::string::npos);
	// D2: the pointer followed the human's own army: no hover over a hidden enemy, and every recorded view was read
	CHECK(c.peerViews.find("\"detector\":\"D2-view-focus\"") != std::string::npos);
	CHECK(c.peerViews.find("\"band\":\"none\"") != std::string::npos);
	CHECK(c.peerViews.find("\"views\":" + std::to_string(plan.views) + ",") != std::string::npos);
#endif
}

TEST_CASE("orders into fog retail: a cheater-like stream (attacks on the AI's never-seen objects in three fights minutes apart) reaches the alert band")
{
	OPENBFME_REQUIRE_START(s);
	Plan plan;
	plan.cheat = true;
	const Checked c = recordAndCheck(*s, plan, "anticheat1_cheat.replay", 1700);
	CHECK(c.hashesCompared == 1700u);
	CHECK_FALSE(c.mismatch);
	REQUIRE(plan.cheatFrames.size() == 3);
	REQUIRE(c.results.size() == 1);
	const auto &r = c.results[0];
	MESSAGE("cheat: " << r.events.size() << " events, " << r.encounters << " encounters, score " << r.score << ", band " << r.band);
	MESSAGE(c.json);
	CHECK(r.band == "alert");
	CHECK(r.encounters == 3);
	std::set<UnsignedInt> frames;
	for (const auto &e : r.events)
	{
		CHECK(e.reason == OrdersIntoFog::Reason::NeverSeen);
		CHECK(plan.cheatTargets.count(e.target) == 1);
		frames.insert(e.frame);
	}
	// the events are at the frames the orders ran on: a local recording has no run-ahead (the command list filled on frame f is batch f's)
	for (UnsignedInt f : plan.cheatFrames)
	{
		CHECK(frames.count(f) == 1);
	}
#ifdef OPENBFME_PEER_EXE
	// openbfme_peer --replay --fog-check finds the same: the alert, the same events (the report's player entry equals the in-process one)
	MESSAGE(c.peerJson);
	CHECK(c.peerRc == 0);
	CHECK(c.peerJson.find("\"game_id\":\"00000000000ac001\"") != std::string::npos);
	CHECK(c.peerJson.find("\"crc_parity\":\"match\"") != std::string::npos);
	const size_t a = c.json.find("\"players\":"), b = c.peerJson.find("\"players\":");
	REQUIRE(a != std::string::npos);
	REQUIRE(b != std::string::npos);
	std::string inProcess = c.json.substr(a), peer = c.peerJson.substr(b);
	while (!peer.empty() && (peer.back() == '\n' || peer.back() == '\r'))
	{
		peer.pop_back();
	}
	// the peer knows the slot (from the replay header); the in-process check was not told
	const std::string slotIn = "\"slot\":-1,", slotPeer = "\"slot\":0,";
	REQUIRE(inProcess.find(slotIn) != std::string::npos);
	inProcess.replace(inProcess.find(slotIn), slotIn.size(), slotPeer);
	CHECK(inProcess == peer);
	// the macro burst: one D5 episode (review, informational) peaking at the burst plus whatever else ran in that window; no D6 event
	CHECK(plan.burstCommands == 150);
	CHECK(c.peerRate.find("\"band\":\"review\"") != std::string::npos);
	CHECK(c.peerRate.find("\"encounters\":1,") != std::string::npos);
	CHECK(c.peerRate.find("\"command\":\"MSG_DO_MOVETO\"") != std::string::npos);
	CHECK(c.peerCommands.find("\"band\":\"none\"") != std::string::npos);
	MESSAGE(c.peerRate);
	// D2: the 30 frames the pointer rested on the AI's never-seen building: one hover episode on it (review: client-reported)
	MESSAGE(c.peerViews);
	CHECK(!plan.hoverTemplate.empty());
	CHECK(c.peerViews.find("\"band\":\"review\"") != std::string::npos);
	CHECK(c.peerViews.find("\"reason\":\"hover_hidden\"") != std::string::npos);
	// the episode starts where the pointer rested (the hidden object nearest to it may be another part of that building, e.g. its citadel)
	char where[96];
	std::snprintf(where, sizeof where, "\"x\":%.6f,\"y\":%.6f", plan.hoverX, plan.hoverY);
	CHECK(c.peerViews.find(where) != std::string::npos);
	// the episode lasts until the next view, or, with none within 10 frames (the human had no mobile unit then), until the last hover view is stale
	// (ViewCheck::Settings::staleFrames, 10 frames after it): 30 frames, or 39 (since MERGEQ-23's AI changes, archive dac88c9f9, the human has no
	// mobile unit from the hover's end until frame 1116 of this game, so no view follows the hover in time)
	const UnsignedInt hoverEnd = plan.afterHover && plan.afterHover <= plan.hoverLast + 10 ? plan.afterHover : plan.hoverLast + 11;
	MESSAGE("D2 hover views " << plan.hoverFirst << ".." << plan.hoverLast << ", next view " << plan.afterHover << ", episode " << hoverEnd - plan.hoverFirst);
	CHECK(c.peerViews.find("\"frame\":" + std::to_string(plan.hoverFirst) + ",") != std::string::npos);
	CHECK(c.peerViews.find("\"frames\":" + std::to_string(hoverEnd - plan.hoverFirst) + ",") != std::string::npos);
#endif
}
