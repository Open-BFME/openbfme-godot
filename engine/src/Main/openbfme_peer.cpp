// OpenBFME. GPL-3.0.
//
// openbfme_peer (lane MP-1): a headless game process: no rendering, no Godot. It hosts or joins a LAN game (GameNetwork/LANLobby.h), loads the skirmish from
// the lobby's START exactly as a rendering peer does (the same retail mount, object world, map and W3D assets: the logic reads the drawables' launch bones,
// so a peer without the W3D archives would diverge; the archive policy makes a missing archive a startup error), and runs it in lockstep
// (GameNetwork/NetGameSession.h). It can also play a replay back (--replay).
//
// The human players of a test game are scripted (--script): a deterministic routine issues selection, move, attack-move and stop commands for the
// local player's units, through the same lockstep command path as the HUD. The AI players are the logic's own skirmish AI.
//
// Usage:
//   openbfme_peer --rotwk DIR --bfme2 DIR (or ROTWK_INSTALL / BFME2_INSTALL)
//     --host PORT [--bind IP] --map KEY --seed N --slot "human|easy|medium|hard|brutal,FactionName,startPos,team" (repeated, in slot order)
//     | --join IP:PORT --name NAME
//     [--frames N] [--run-ahead N] [--crc-interval N] [--script] [--realtime] [--record FILE] [--hashes FILE] [--report FILE]
//     [--inject-divergence FRAME] [--drop PERMILLE] [--delay MS] [--jitter MS] [--feature NAME] [--lobby-timeout S]
//     SMOOTH-1 (review r4, the logic worker in a network game): [--logic-thread] the frames run on LiveGame's logic worker; [--step-ms N] the render step
//     (advance(N ms) per loop, default 200); [--worker-delay-ms N] the worker sleeps N ms after every frame (a slow simulation owner);
//     [--pause-at FRAME:MS] at that protocol frame only advance(0) for MS ms (presentation-only: the transport is pumped, no batch runs);
//     [--replace-driver-at FRAME] remove and reinstall the frame driver there; [--toggle-thread-at FRAME] switch the worker off and on there
//     lane MP-2 (the disconnect path): [--vanish-at FRAME] the process stops at that protocol frame without leaving (a crash: its report says so, nothing
//     more is sent); [--disconnect-ms N] / [--player-timeout-ms N] replace GameData's NetworkDisconnectTime / NetworkPlayerTimeoutTime (tests);
//     [--kick] the local player presses Kick for every player the disconnect screen offers it for; [--desync-dir DIR] where a desync dump goes
//     (GameNetwork/DesyncDump.h, default the current directory); [--fixed-run-ahead] keep the lobby's run-ahead (no adaptation to the round trip)
//     lane MP-2 (the LAN lobby, GameNetwork/LANAPI.h): --lan-join --name NAME [--lan-faction FactionName] [--lan-port-base N] [--lan-targets a.b.c.d,...]
//     instead of --host / --join: joins the first game the LAN lobby lists (a game hosted from LanLobby.apt), asks for the faction, accepts (again after
//     every options change) and plays the game the host starts
//     lane MP-3 (NET-4, the network test harness, GameNetwork/NetImpairment.h): [--link [SLOT:]FAULTS] faults on the datagrams this peer sends (to every
//     peer, or to one slot; repeated), FAULTS "latency=MS,jitter=MS,loss=PERMILLE,dup=PERMILLE,reorder=PERMILLE,reorder_ms=MS" (jitter is +- around the
//     latency); --drop / --delay / --jitter are the older spelling (loss, and a delay uniform in delay .. delay + jitter); [--impair-seed N] (default 1 +
//     the local slot); [--blackout-at FRAME:MS] at that protocol frame the peer is cut off (nothing in, nothing out) for MS ms (repeated);
//     [--census FILE] every logic frame's wall time on the simulation owner and living battalions / troops / objects as CSV (GameClient/FrameCensus.h;
//     tools/net/mp3_perf_table.py reads it); [--no-fast-resend] the transport's MP-2 resend timing (no fast resend, Karn's backoff up to 2 s), for A/B measurements;
//     [--quit-at FRAME] at that protocol frame the local player exits the game as the quit menu's Exit does (MSG_SELF_DESTRUCT { TRUE } of its player,
//     then the leave, GodotGameWorld self_destruct + net_finish); its report says quit_at;
//     [--paced] the game advances by the real time elapsed (a client's render loop at about 200 Hz: the 5 Hz logic clock paces the frames) instead of
//     --step-ms per loop; the report's input_latency / stall lines measure the smoothness (LockstepDriver::SmoothStats)
//     lane HERO-2: [--create-a-hero SLOT:FILE.cah] (host, repeated) the slot's Create-a-Hero, read at the setup and carried by the START to every peer;
//     with --lan-join [--create-a-hero FILE.cah] the own slot's, sent to the lobby's host (LANAPI::requestSlotCreateAHero); [--recruit-create-a-hero FRAME]
//     with --script: from that frame the local player recruits its Create-a-Hero at its fortress until it exists
//   openbfme_peer --rotwk DIR --bfme2 DIR --replay FILE [--report FILE]
// Exit code: 0 ok, 1 usage / load error / another profile's replay, 2 desync (or replay mismatch / an incomplete replay), 3 network failure (stall /
// timeout, refused by the lobby: another profile), 4 network or recording errors (a command that could not be sent or recorded).

#include "Common/JobSystem.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/RetailArchivePolicy.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/GateModules.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetGameSession.h"
#include "GameNetwork/NetworkSettings.h"
#include "GameLogic/SelfDestruct.h"
#include "GameNetwork/ScriptedPlayer.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Common/PlayerScience.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct Args
{
	std::string rotwk, bfme2;
	int hostPort = -1;
	std::string bind = "127.0.0.1";
	std::string join, name = "Player";
	std::string map = "maps/map mp evendim/map mp evendim.map";
	std::uint32_t seed = 1234;
	std::vector<std::string> slots;
	int frames = 600;
	int runAhead = 2;
	int crcInterval = 100;
	bool script = false, realtime = false;
	std::string record, hashes, report, replay;
	long long injectFrame = -1;
	int dropPerMille = 0, delayMs = 0, jitterMs = 0;
	std::vector<std::string> links;          ///< lane MP-3: --link [SLOT:]FAULTS
	long long impairSeed = -1;
	std::vector<std::pair<long long, int>> blackouts; ///< lane MP-3: --blackout-at FRAME:MS
	bool paced = false;
	long long quitAt = -1;                   ///< lane MP-3: --quit-at FRAME
	bool noFastResend = false;               ///< lane MP-3: --no-fast-resend (MP-2's resend timing, for A/B measurements)
	std::string census;                      ///< lane MP-3: --census FILE (the per-frame census CSV of the measurements)
	int startingCash = 1500;
	std::vector<std::string> features; ///< --feature NAME: an enabled engine feature (part of the profile identity)
	int lobbyTimeoutSeconds = 120;
	bool logicThread = false;      ///< SMOOTH-1
	int stepMs = 200, workerDelayMs = 0;
	long long pauseAt = -1, replaceDriverAt = -1, toggleThreadAt = -1;
	int pauseMs = 0;
	long long vanishAt = -1; ///< lane MP-2
	long long disconnectMs = -1, playerTimeoutMs = -1;
	bool kick = false;
	std::string desyncDir;
	bool fixedRunAhead = false;
	bool lanJoin = false;
	std::string lanFaction, lanTargets;
	int lanPortBase = -1;
	int logicThreads = 0;          ///< PERF-2: --logic-threads N, the logic job pool's threads (0: OPENBFME_LOGIC_THREADS or the hardware's)
	int toggleGates = 0;                ///< lane HUD-5: --toggle-gates N (every N frames the local player toggles each settled gate it controls)
	long long recruitCreateAHeroAt = -1; ///< lane HERO-2: --recruit-create-a-hero FRAME (the local player recruits its Create-a-Hero from then on until made)
	std::vector<std::string> createAHeroes; ///< lane HERO-2: --create-a-hero SLOT:FILE.cah (host) / --create-a-hero FILE.cah (--lan-join: the own slot)
};

std::string envOr(const char *name)
{
	const char *v = std::getenv(name);
	return v ? v : "";
}

bool parseArgs(int argc, char **argv, Args &a, std::string &error)
{
	a.rotwk = envOr("ROTWK_INSTALL");
	a.bfme2 = envOr("BFME2_INSTALL");
	for (int i = 1; i < argc; ++i)
	{
		const std::string k = argv[i];
		auto next = [&](std::string &out) {
			if (i + 1 >= argc)
			{
				error = k + " needs a value";
				return false;
			}
			out = argv[++i];
			return true;
		};
		std::string v;
		if (k == "--rotwk" && next(a.rotwk)) {}
		else if (k == "--bfme2" && next(a.bfme2)) {}
		else if (k == "--host" && next(v)) a.hostPort = std::atoi(v.c_str());
		else if (k == "--bind" && next(a.bind)) {}
		else if (k == "--join" && next(a.join)) {}
		else if (k == "--name" && next(a.name)) {}
		else if (k == "--map" && next(a.map)) {}
		else if (k == "--seed" && next(v)) a.seed = (std::uint32_t)std::strtoul(v.c_str(), nullptr, 10);
		else if (k == "--slot" && next(v)) a.slots.push_back(v);
		else if (k == "--frames" && next(v)) a.frames = std::atoi(v.c_str());
		else if (k == "--run-ahead" && next(v)) a.runAhead = std::atoi(v.c_str());
		else if (k == "--crc-interval" && next(v)) a.crcInterval = std::atoi(v.c_str());
		else if (k == "--script") a.script = true;
		else if (k == "--realtime") a.realtime = true;
		else if (k == "--record" && next(a.record)) {}
		else if (k == "--hashes" && next(a.hashes)) {}
		else if (k == "--report" && next(a.report)) {}
		else if (k == "--replay" && next(a.replay)) {}
		else if (k == "--inject-divergence" && next(v)) a.injectFrame = std::atoll(v.c_str());
		else if (k == "--drop" && next(v)) a.dropPerMille = std::atoi(v.c_str());
		else if (k == "--delay" && next(v)) a.delayMs = std::atoi(v.c_str());
		else if (k == "--jitter" && next(v)) a.jitterMs = std::atoi(v.c_str());
		else if (k == "--link" && next(v)) a.links.push_back(v);
		else if (k == "--impair-seed" && next(v)) a.impairSeed = std::atoll(v.c_str());
		else if (k == "--blackout-at" && next(v))
		{
			const size_t colon = v.find(':');
			if (colon == std::string::npos)
			{
				error = "--blackout-at wants FRAME:MS";
				return false;
			}
			a.blackouts.push_back({ std::atoll(v.c_str()), std::atoi(v.c_str() + colon + 1) });
		}
		else if (k == "--paced") a.paced = true;
		else if (k == "--no-fast-resend") a.noFastResend = true;
		else if (k == "--census" && next(a.census)) {}
		else if (k == "--quit-at" && next(v)) a.quitAt = std::atoll(v.c_str());
		else if (k == "--cash" && next(v)) a.startingCash = std::atoi(v.c_str());
		else if (k == "--feature" && next(v)) a.features.push_back(v);
		else if (k == "--lobby-timeout" && next(v)) a.lobbyTimeoutSeconds = std::atoi(v.c_str());
		else if (k == "--logic-thread") a.logicThread = true;
		else if (k == "--step-ms" && next(v)) a.stepMs = std::max(1, std::atoi(v.c_str()));
		else if (k == "--worker-delay-ms" && next(v)) a.workerDelayMs = std::atoi(v.c_str());
		else if (k == "--pause-at" && next(v))
		{
			a.pauseAt = std::atoll(v.c_str());
			a.pauseMs = v.find(':') == std::string::npos ? 1000 : std::atoi(v.c_str() + v.find(':') + 1);
		}
		else if (k == "--replace-driver-at" && next(v)) a.replaceDriverAt = std::atoll(v.c_str());
		else if (k == "--toggle-thread-at" && next(v)) a.toggleThreadAt = std::atoll(v.c_str());
		else if (k == "--vanish-at" && next(v)) a.vanishAt = std::atoll(v.c_str());
		else if (k == "--disconnect-ms" && next(v)) a.disconnectMs = std::atoll(v.c_str());
		else if (k == "--player-timeout-ms" && next(v)) a.playerTimeoutMs = std::atoll(v.c_str());
		else if (k == "--kick") a.kick = true;
		else if (k == "--desync-dir" && next(a.desyncDir)) {}
		else if (k == "--fixed-run-ahead") a.fixedRunAhead = true;
		else if (k == "--lan-join") a.lanJoin = true;
		else if (k == "--create-a-hero" && next(v)) a.createAHeroes.push_back(v);
		else if (k == "--recruit-create-a-hero" && next(v)) a.recruitCreateAHeroAt = std::atoll(v.c_str());
		else if (k == "--toggle-gates" && next(v)) a.toggleGates = std::max(1, std::atoi(v.c_str()));
		else if (k == "--lan-faction" && next(a.lanFaction)) {}
		else if (k == "--lan-targets" && next(a.lanTargets)) {}
		else if (k == "--lan-port-base" && next(v)) a.lanPortBase = std::atoi(v.c_str());
		else if (k == "--logic-threads" && next(v))
		{
			a.logicThreads = std::atoi(v.c_str());
			if (a.logicThreads < 1)
			{
				error = "--logic-threads needs a count of at least 1";
				return false;
			}
		}
		else
		{
			if (error.empty())
			{
				error = "unknown argument " + k;
			}
			return false;
		}
	}
	if (a.rotwk.empty() || a.bfme2.empty())
	{
		error = "the RotWK and BFME2 installs are required (--rotwk / --bfme2 or ROTWK_INSTALL / BFME2_INSTALL)";
		return false;
	}
	return true;
}

// The retail world every peer loads (the same steps as the Godot start path and the START-1 test fixture)
struct World
{
	std::unique_ptr<Win32BIGFileSystem> fs;
	std::unique_ptr<RetailObjectWorld> world;
	MapObjectOptions options;
	GameLogicSettings settings;
	std::vector<MapCacheEntry> maps;
	ProfileIdentity profile; ///< this peer's (GameNetwork/ProfileIdentity.h): the mounted archives in order, the build, the features
	NetworkSettings network; ///< lane MP-2: GameData's network timing
};

bool loadWorld(const Args &a, World &w, std::string &error)
{
	std::vector<RetailInstall> installs;
	for (const auto &want : { std::make_pair(std::string("rotwk"), a.rotwk), std::make_pair(std::string("bfme2"), a.bfme2) })
	{
		RetailInstall inst;
		inst.label = want.first;
		inst.root = want.second;
		if (!RetailArchivePolicy::loadBuiltin(want.first == "rotwk" ? "rotwk-201" : "bfme2-106", inst.policy, &error))
		{
			return false;
		}
		installs.push_back(inst);
	}
	w.fs = std::make_unique<Win32BIGFileSystem>();
	RetailMountOptions mo;
	const RetailMountReport mr = MountRetailArchives(*w.fs, installs, mo);
	if (!mr.ok)
	{
		for (const std::string &e : mr.errors)
		{
			error += e + "\n";
		}
		error = "the retail mount failed (a headless peer needs every archive a rendering peer reads, the W3D archives included):\n" + error;
		return false;
	}
	w.profile = ProfileIdentity::compute(mr.mounted, RandomAlgorithm::ZH_CarryChain, a.features);
	w.world = std::make_unique<RetailObjectWorld>(*w.fs);
	if (!w.world->load(&error) || !MapObjectGameData::load(*w.fs, w.options, &error) || !MapObjectGameData::loadPlayerTemplates(*w.fs, w.options, &error)
		|| !MapCreationHooks::load(*w.fs, w.options.creationScripts, &error) || !GameLogicSettingsLoader::load(*w.fs, w.settings, &error)
		|| !IniSkirmishSetupSource::loadMapCache(*w.fs, w.maps, &error) || !NetworkSettings::load(*w.fs, w.network, &error))
	{
		return false;
	}
	return true;
}

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

// lane HERO-2: a .cah file into a slot at the setup (the only place a peer reads one; the game takes the record from the setup)
bool readCreateAHero(const std::string &path, const World &w, SkirmishGameSlot &slot, std::string &error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		error = "cannot read the Create-a-Hero file " + path;
		return false;
	}
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (!slot.setCreateAHeroBytes(bytes, &error) || !w.world->createAHeroSystem().validateHero(slot.createAHero, w.world->commands(), &error))
	{
		error = path + ": " + error;
		return false;
	}
	return true;
}

bool buildGame(const Args &a, const World &w, NewGameMessage &m, std::string &error)
{
	m.game.mapName = a.map;
	m.game.seed = a.seed;
	m.game.startingCash = a.startingCash;
	if (a.slots.empty() || a.slots.size() > (size_t)MAX_SLOTS)
	{
		error = "a hosted game needs 1 .. 8 --slot";
		return false;
	}
	for (size_t i = 0; i < a.slots.size(); ++i)
	{
		std::vector<std::string> f;
		std::stringstream ss(a.slots[i]);
		std::string part;
		while (std::getline(ss, part, ','))
		{
			f.push_back(part);
		}
		if (f.size() != 4)
		{
			error = "--slot wants kind,faction,startPos,team: " + a.slots[i];
			return false;
		}
		SkirmishGameSlot &s = m.game.slots[i];
		static const std::map<std::string, SlotState> kinds = { { "human", SLOT_PLAYER }, { "easy", SLOT_EASY_AI }, { "medium", SLOT_MED_AI },
			{ "hard", SLOT_HARD_AI }, { "brutal", SLOT_BRUTAL_AI } };
		const auto k = kinds.find(f[0]);
		if (k == kinds.end())
		{
			error = "unknown slot kind " + f[0];
			return false;
		}
		s.state = k->second;
		s.name = s.isHuman() ? u"Human" + std::u16string(1, (char16_t)(u'0' + i)) : u"Computer" + std::u16string(1, (char16_t)(u'0' + i));
		s.playerTemplate = templateIndex(w.world->playerTemplates(), f[1]);
		if (s.playerTemplate < 0)
		{
			error = "unknown faction " + f[1];
			return false;
		}
		s.startPos = std::atoi(f[2].c_str());
		s.color = (int)i;
		s.teamNumber = std::atoi(f[3].c_str());
	}
	for (const std::string &c : a.createAHeroes)
	{
		const size_t colon = c.find(':');
		const int slot = colon == std::string::npos ? -1 : std::atoi(c.substr(0, colon).c_str());
		if (slot < 0 || slot >= MAX_SLOTS || !m.game.slots[slot].isHuman())
		{
			error = "--create-a-hero wants SLOT:FILE.cah with a human slot: " + c;
			return false;
		}
		if (!readCreateAHero(c.substr(colon + 1), w, m.game.slots[slot], error))
		{
			return false;
		}
	}
	return true;
}

void writeReport(const std::string &path, const std::string &text)
{
	if (path.empty())
	{
		std::fputs(text.c_str(), stdout);
		return;
	}
	std::ofstream o(path, std::ios::binary | std::ios::trunc); // LF lines on every OS: reports and hash files compare byte for byte (lane WIN-1)
	o << text;
}

int runReplay(const Args &a, World &w)
{
	std::string error;
	ReplayFile replay;
	if (!ReplayFile::load(a.replay, replay, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	// PLAN: a replay of another profile identity is refused before the game is loaded
	const std::string mismatch = replay.profileMismatch(w.profile);
	if (!mismatch.empty())
	{
		std::fprintf(stderr, "openbfme_peer: the replay was recorded by another profile: %s\n", mismatch.c_str());
		return 1;
	}
	NewGameStart start(replay.header.algorithm);
	if (!NewGame::prepareNewGame(replay.header.game, w.world->playerTemplates(), w.settings, w.maps, replay.header.algorithm, start, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	start.localSlot = replay.header.recordingSlot;
	ArchiveW3DFileSource source(*w.fs);
	WW3DAssetManager assets(source);
	LiveGame game(*w.world, *w.fs, assets, w.options);
	LiveGame::Options o;
	o.start = &start;
	if (!game.load(o, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	ReplayPlayback playback(replay);
	RegisterLogicCRCHandler(game.dispatch());
	game.setFrameDriver(&playback);
	while (!playback.finished())
	{
		game.advance(0.2);
	}
	game.drainFrames();
	std::ostringstream r;
	r << "replay " << a.replay << "\n";
	r << "frames " << game.frame() << " of " << replay.finalFrame << (replay.complete ? "" : " (no end record)") << "\n";
	r << "commands " << replay.commandCount() << "\n";
	r << "hashes_compared " << playback.hashesCompared() << "\n";
	r << "mismatches " << playback.mismatches() << "\n";
	for (const std::string &l : replay.lost)
	{
		r << "lost_command " << l << "\n"; // the recording could not encode it: the replay is incomplete
	}
	const std::uint32_t finalHash = game.logic().computeStateHash();
	char b[64];
	std::snprintf(b, sizeof(b), "final_hash 0x%08X recorded 0x%08X\n", finalHash, replay.finalHash);
	r << b;
	if (playback.hasMismatch())
	{
		std::snprintf(b, sizeof(b), "first_mismatch frame %u recorded 0x%08X played 0x%08X\n", playback.firstMismatch().frame, playback.firstMismatch().recorded,
			playback.firstMismatch().played);
		r << b;
	}
	writeReport(a.report, r.str());
	return playback.hasMismatch() || !replay.lost.empty() || (replay.complete && finalHash != replay.finalHash) ? 2 : 0;
}
} // namespace

int main(int argc, char **argv)
{
	Args a;
	std::string error;
	if (!parseArgs(argc, argv, a, error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	if (a.logicThreads > 0)
	{
		JobSystem::logic().setThreadCount(a.logicThreads); // PERF-2: the peers of one game may use different thread counts (lockstep must not care)
	}
	World w;
	if (!loadWorld(a, w, error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	if (!a.replay.empty())
	{
		return runReplay(a, w);
	}
	NetAddress bindAddress;
	if (!NetAddress::parse(a.bind + ":" + std::to_string(a.hostPort > 0 ? a.hostPort : 0), bindAddress))
	{
		std::fprintf(stderr, "openbfme_peer: bad --bind %s\n", a.bind.c_str());
		return 1;
	}
	UDP socket;
	if (!socket.bind(bindAddress.ip, bindAddress.port, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	// ---- lobby ----
	LobbyStart lobby;
	const std::uint64_t lobbyStart = NetMilliseconds();
	std::unique_ptr<LANAPI> lan;              // lane MP-2: --lan-join
	std::unique_ptr<UDP> lanGameSocket;
	if (a.lanJoin)
	{
		LANAPI::Options lo;
		lo.playerTemplateCount = w.world->playerTemplates().getPlayerTemplateCount();
		lo.colorCount = (int)w.settings.multiplayerColors.size();
		lo.validateCreateAHero = [&w](const CreateAHeroHero &h, std::string *why) { return w.world->createAHeroSystem().validateHero(h, w.world->commands(), why); };
		SkirmishGameSlot ownHero;
		if (a.createAHeroes.size() > 1 || (!a.createAHeroes.empty() && !readCreateAHero(a.createAHeroes.front(), w, ownHero, error)))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", a.createAHeroes.size() > 1 ? "--lan-join takes one --create-a-hero FILE.cah" : error.c_str());
			return 1;
		}
		if (a.lanPortBase > 0)
		{
			lo.lobbyPortBase = (std::uint16_t)a.lanPortBase;
		}
		std::stringstream targets(a.lanTargets);
		for (std::string t; std::getline(targets, t, ',');)
		{
			NetAddress ta;
			if (!NetAddress::parse(t + ":0", ta))
			{
				std::fprintf(stderr, "openbfme_peer: bad --lan-targets entry %s\n", t.c_str());
				return 1;
			}
			lo.broadcast = false;
			lo.extraTargets.push_back(ta.ip);
		}
		const int faction = a.lanFaction.empty() ? LANAPI::kKeep : templateIndex(w.world->playerTemplates(), a.lanFaction);
		if (faction == -1)
		{
			std::fprintf(stderr, "openbfme_peer: unknown --lan-faction %s\n", a.lanFaction.c_str());
			return 1;
		}
		lan = std::make_unique<LANAPI>(w.profile, std::u16string(a.name.begin(), a.name.end()), lo);
		if (!lan->open(&error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		std::uint64_t lastTry = 0;
		bool factionAsked = false;
		while (!lan->started())
		{
			const std::uint64_t now = NetMilliseconds();
			lan->update(now);
			for (const LANAPI::Event &e : lan->takeEvents())
			{
				if (e.kind == LANAPI::Event::Kicked || e.kind == LANAPI::Event::HostLeave)
				{
					std::fprintf(stderr, "openbfme_peer: LAN lobby: %s\n", e.kind == LANAPI::Event::Kicked ? "kicked" : "the host left");
					return 3;
				}
				if (e.kind == LANAPI::Event::GameJoin && e.ret != LANAPI::RET_OK)
				{
					std::fprintf(stderr, "openbfme_peer: LAN lobby: the join was answered %d\n", (int)e.ret);
					if (e.ret == LANAPI::RET_CRC_MISMATCH)
					{
						return 3;
					}
				}
			}
			const LANGame *g = lan->currentGame();
			if (!g && !lan->games().empty() && now - lastTry > 1500)
			{
				lastTry = now;
				lan->requestGameJoin(0);
			}
			else if (g && !factionAsked)
			{
				factionAsked = true;
				lan->requestSlotOptions(faction, LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep);
				if (ownHero.hasCreateAHero && !lan->requestSlotCreateAHero(&ownHero.createAHero, &error))
				{
					std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
					return 1;
				}
				lastTry = now;
			}
			else if (g && !g->accepted[(size_t)lan->localSlot()] && now - lastTry > 500)
			{
				lastTry = now;
				lan->requestAccept(true); // ZH: every options change unaccepts: accept again
			}
			if (!lan->errors().empty() || now - lobbyStart > (std::uint64_t)a.lobbyTimeoutSeconds * 1000u)
			{
				for (const std::string &l : lan->log())
				{
					std::fprintf(stderr, "openbfme_peer: LAN lobby: %s\n", l.c_str());
				}
				std::fprintf(stderr, "openbfme_peer: %s\n", lan->errors().empty() ? "no LAN game start in time" : lan->errors().front().c_str());
				return 3;
			}
			NetSleepMilliseconds(5);
		}
		lobby = lan->start();
		lanGameSocket = lan->takeGameSocket();
	}
	else if (a.hostPort > 0)
	{
		NewGameMessage m;
		if (!buildGame(a, w, m, error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		int hostSlot = -1;
		for (int i = 0; i < MAX_SLOTS && hostSlot < 0; ++i)
		{
			hostSlot = m.game.slots[i].isHuman() ? i : -1;
		}
		if (hostSlot < 0)
		{
			std::fprintf(stderr, "openbfme_peer: the hosted game has no human slot\n");
			return 1;
		}
		LANLobbyHost host(socket, m, hostSlot, a.runAhead, a.crcInterval, w.profile);
		while (!host.poll())
		{
			if (NetMilliseconds() - lobbyStart > (std::uint64_t)a.lobbyTimeoutSeconds * 1000u)
			{
				for (const std::string &l : host.log())
				{
					std::fprintf(stderr, "openbfme_peer: lobby: %s\n", l.c_str());
				}
				std::fprintf(stderr, "openbfme_peer: the game did not fill in %d s\n", a.lobbyTimeoutSeconds);
				return 3;
			}
			NetSleepMilliseconds(2);
		}
		lobby = host.start();
	}
	else
	{
		NetAddress hostAddress;
		if (!NetAddress::parse(a.join, hostAddress))
		{
			std::fprintf(stderr, "openbfme_peer: bad --join %s\n", a.join.c_str());
			return 1;
		}
		std::u16string name(a.name.begin(), a.name.end());
		LANLobbyClient client(socket, hostAddress, name, w.profile);
		while (!client.poll())
		{
			if (!client.error().empty() || NetMilliseconds() - lobbyStart > (std::uint64_t)a.lobbyTimeoutSeconds * 1000u)
			{
				std::fprintf(stderr, "openbfme_peer: %s\n", client.error().empty() ? "no START in 120 s" : client.error().c_str());
				return 3;
			}
			NetSleepMilliseconds(2);
		}
		lobby = client.start();
		// the START_ACK can be lost: two more (late STARTs are answered by NetGameSession::service)
		for (int i = 0; i < 2; ++i)
		{
			const std::vector<std::uint8_t> ack = LANLobby::encodeStartAck(lobby.localSlot);
			socket.sendTo(hostAddress, ack.data(), ack.size());
		}
	}
	// ---- load ----
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	if (!NewGame::prepareNewGame(lobby.game, w.world->playerTemplates(), w.settings, w.maps, RandomAlgorithm::ZH_CarryChain, start, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	start.localSlot = lobby.localSlot; // GameInfo::getLocalSlotNum of this peer
	ArchiveW3DFileSource source(*w.fs);
	WW3DAssetManager assets(source);
	LiveGame game(*w.world, *w.fs, assets, w.options);
	LiveGame::Options o;
	o.start = &start;
	o.logicThread = a.logicThread; // SMOOTH-1: the frames on the logic worker (this thread stays the protocol owner)
	if (!game.load(o, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	NetGameSession::Options so;
	so.replayPath = a.record;
	so.profile = w.profile;
	{
		// lane MP-3: the harness's faults (the older --drop / --delay / --jitter first, then every --link)
		so.transport.fastResend = !a.noFastResend;
		so.census = !a.census.empty();
		NetImpairment::Config &ic = so.transport.impairment;
		ic.seed = a.impairSeed >= 0 ? (std::uint32_t)a.impairSeed : 1u + (std::uint32_t)lobby.localSlot;
		ic.all.lossPerMille = a.dropPerMille;
		ic.all.latencyMs = a.delayMs + a.jitterMs / 2;
		ic.all.jitterMs = a.jitterMs / 2;
		for (const std::string &l : a.links)
		{
			const size_t colon = l.find(':');
			const int slot = colon == std::string::npos ? -1 : std::atoi(l.substr(0, colon).c_str());
			NetLinkFaults f;
			if ((colon != std::string::npos && (slot < 0 || slot >= MAX_SLOTS)) || !NetLinkFaults::parse(l.substr(colon == std::string::npos ? 0 : colon + 1), f, &error))
			{
				std::fprintf(stderr, "openbfme_peer: --link %s: %s\n", l.c_str(), error.empty() ? "the slot is not 0 .. 7" : error.c_str());
				return 1;
			}
			if (slot < 0)
			{
				ic.all = f;
			}
			else
			{
				ic.link[(size_t)slot] = f;
				ic.linkSet[(size_t)slot] = true;
			}
		}
	}
	so.network = w.network;
	so.desyncDirectory = a.desyncDir;
	so.adaptiveRunAhead = !a.fixedRunAhead;
	so.exeName = "openbfme_peer";
	so.playerName = lobby.localSlot >= 0 && lobby.localSlot < MAX_SLOTS ? std::string(lobby.game.game.slots[lobby.localSlot].name.begin(), lobby.game.game.slots[lobby.localSlot].name.end()) : std::string();
	if (a.disconnectMs >= 0)
	{
		so.network.disconnectTime = (std::uint32_t)a.disconnectMs;
	}
	if (a.playerTimeoutMs >= 0)
	{
		so.network.playerTimeoutTime = (std::uint32_t)a.playerTimeoutMs;
	}
	NetGameSession session(lanGameSocket ? *lanGameSocket : socket, lobby, so);
	if (!session.begin(game, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	const std::uint64_t loadWait = NetMilliseconds();
	while (!session.pollLoaded())
	{
		if (NetMilliseconds() - loadWait > 180000)
		{
			std::fprintf(stderr, "openbfme_peer: the other peers did not load in 180 s\n");
			return 3;
		}
		NetSleepMilliseconds(2);
	}
	// the logic frame after each frame ran, the state hash then and a fold of the RNG seeds (written on the simulation owner: the worker with
	// --logic-thread; read after the game drained)
	struct FrameHash
	{
		UnsignedInt frame;
		std::uint32_t hash, rng;
	};
	std::vector<FrameHash> hashes;
	std::vector<std::string> checkpoints; // lane MP-3 (review r1): "self_destruct_checkpoint" lines (written on the simulation owner, read after the drain)
	std::map<int, int> slotOfPlayer;
	{
		const int me = session.config().slotPlayerIndex[(size_t)lobby.localSlot];
		const long long at = a.injectFrame;
		const int delay = a.workerDelayMs;
		// lane MP-3 (review r1): the game slot of every player index (a transfer checkpoint names slots)
		const LiveGame::Report lr0 = game.report();
		for (int sl = 0; sl < MAX_SLOTS && (size_t)sl < lr0.startSlotPlayers.size(); ++sl)
		{
			const Player *p = game.logic().players().findPlayerWithName(lr0.startSlotPlayers[(size_t)sl]);
			if (p && lobby.game.game.slots[sl].isOccupied())
			{
				slotOfPlayer[p->getPlayerIndex()] = sl;
			}
		}
		session.driver().setAfterFrame([me, at, delay, &hashes, &checkpoints, &slotOfPlayer](GameLogic &logic) {
			if (at >= 0 && logic.getFrame() == (UnsignedInt)at)
			{
				// the injected divergence: one more unit of money on this peer only (as a desync bug would)
				logic.players().getNthPlayer(me)->getMoney()->deposit(1, false);
			}
			std::uint32_t rng = 0x811C9DC5u;
			for (std::uint32_t w : logic.random().seedArray())
			{
				rng = (rng ^ w) * 0x01000193u;
			}
			hashes.push_back({ logic.getFrame(), logic.computeStateHash(), rng });
			// lane MP-3 (review r1): right after the frame that executed a MSG_SELF_DESTRUCT { TRUE }: who got the army, and whether every transferred object
			// is the ally's now
			const auto &log = SelfDestruct::stats().transferLog;
			while (checkpoints.size() < log.size())
			{
				const SelfDestruct::Stats::Transfer &t = log[checkpoints.size()];
				size_t owned = 0;
				for (ObjectID id : t.objects)
				{
					const Object *o = logic.findObjectByID(id);
					owned += o && o->getControllingPlayer() && o->getControllingPlayer()->getPlayerIndex() == t.ally ? 1 : 0;
				}
				auto slotOf = [&](int player) {
					const auto it = slotOfPlayer.find(player);
					return it == slotOfPlayer.end() ? -1 : it->second;
				};
				checkpoints.push_back("frame " + std::to_string(logic.getFrame()) + " leaver_slot " + std::to_string(slotOf(t.leaver)) + " ally_slot "
					+ std::to_string(slotOf(t.ally)) + " transferred " + std::to_string(t.objects.size()) + " ally_owned " + std::to_string(owned));
			}
			if (delay > 0)
			{
				NetSleepMilliseconds((std::uint32_t)delay); // SMOOTH-1: a slow simulation owner (the protocol owner keeps pumping)
			}
		});
	}
	game.setFrameDriver(&session.driver()); // the hook is set before the driver is (re)installed
	// ---- run ----
	const int myPlayer = session.config().slotPlayerIndex[(size_t)lobby.localSlot];
	const int myStart = start.message.game.slots[lobby.localSlot].startPos;
	unsigned long long scripted = 0;
	std::map<std::string, unsigned long long> scriptedTypes; // the scripted commands by type
	UnsignedInt lastScripted = 0xFFFFFFFFu;
	UnsignedInt lastGateFrame = 0xFFFFFFFFu; // lane HUD-5: --toggle-gates
	unsigned long long gateMessages = 0;
	const std::uint64_t runStart = NetMilliseconds();
	std::uint64_t lastProgress = runStart;
	int exitCode = 0;
	session.driver().setStopFrame((UnsignedInt)std::max(0, a.frames)); // SMOOTH-1: no batch beyond --frames (the worker may run ahead of presentation)
	// lane MP-2: a frame may wait for the disconnect path (the screen after NetworkDisconnectTime, the drop after NetworkPlayerTimeoutTime)
	const std::uint64_t noProgressMs = 30000u + (std::uint64_t)so.network.disconnectTime + (std::uint64_t)so.network.playerTimeoutTime;
	std::string disconnectQuit;
	std::vector<std::string> blackoutLog; // lane MP-3
	long long quitAtFrame = -1;
	std::uint64_t lastAdvance = NetMilliseconds();
	while ((int)game.protocolFrame() < a.frames)
	{
		session.service();
		if (lan)
		{
			lan->update(NetMilliseconds()); // a repeated GAME_START is answered (a lost GAME_START_ACK keeps the host waiting)
			lan->takeEvents();
		}
		if (a.vanishAt >= 0 && (long long)game.protocolFrame() >= a.vanishAt)
		{
			// lane MP-2: a crash: no leave, nothing more sent; the report says where it stopped
			std::ostringstream v;
			v << "slot " << lobby.localSlot << "\nvanished_at " << game.protocolFrame() << "\n";
			writeReport(a.report, v.str());
			game.drainFrames(); // the frames already handed to the worker: their hashes are this peer's last ones
			if (!a.hashes.empty())
			{
				std::ofstream h(a.hashes, std::ios::binary | std::ios::trunc);
				char hb[96];
				for (const FrameHash &fh : hashes)
				{
					std::snprintf(hb, sizeof(hb), "%u 0x%08X 0x%08X\n", fh.frame, fh.hash, fh.rng);
					h << hb;
				}
			}
			std::fflush(stdout);
			std::_Exit(0);
		}
		if (a.quitAt >= 0 && (long long)game.protocolFrame() >= a.quitAt)
		{
			// lane MP-3: the quit menu's Exit of a network game (RW 0x9218A4: MSG_SELF_DESTRUCT { TRUE } of the local player in the message stream); the
			// leave follows in session.finish, after it
			GameMessage m(MSG_SELF_DESTRUCT, myPlayer);
			m.appendBooleanArgument(true);
			game.commands().append(m);
			quitAtFrame = (long long)game.protocolFrame();
			break;
		}
		if (session.quitRequested())
		{
			disconnectQuit = session.quitReason(); // lane MP-2: dropped by the others, the last one left, or the local connection failed
			exitCode = 3;
			break;
		}
		if (a.kick)
		{
			const DisconnectManager::Screen &sc = session.network().disconnectManager().screen();
			for (const DisconnectManager::Screen::Row &row : sc.rows)
			{
				if (sc.visible && row.used && !row.removed && row.kickShown)
				{
					session.network().voteForPlayerDisconnect(row.slot); // the Kick button (once per slot: RW 0x8D8194)
				}
			}
		}
		if (a.script && game.frame() != lastScripted)
		{
			lastScripted = game.frame();
			const size_t before = game.commands().messages().size();
			scripted += ScriptedPlayer::issue(game, myPlayer, myStart, game.commands());
			// lane HERO-2: the Create-a-Hero is recruited at the fortress (every 25 frames from the given one until the hero exists)
			const CreateAHeroHero *own = a.recruitCreateAHeroAt >= 0 && game.frame() >= (UnsignedInt)a.recruitCreateAHeroAt && game.frame() % 25 == 0
				? game.logic().createAHeroes().heroOf(*game.logic().players().getNthPlayer(myPlayer))
				: nullptr;
			const Object *made = own && own->objectID ? game.logic().findObjectByID(own->objectID) : nullptr; // a file's ID names no object of this game
			if (own && !(made && made->getTemplate() && made->getTemplate()->getName() == "CreateAHero"))
			{
				scripted += ScriptedPlayer::recruitCreateAHero(game, myPlayer, game.commands());
			}
			for (size_t i = before; i < game.commands().messages().size(); ++i)
			{
				++scriptedTypes[GameMessageTypeName(game.commands().messages()[i].getType())];
			}
		}
		if (a.toggleGates > 0 && game.frame() != lastGateFrame && game.frame() % (UnsignedInt)a.toggleGates == 0)
		{
			// lane HUD-5: the gate buttons' messages (TOGGLE_GATE, RW 0x9410D7) through the lockstep command list, for every settled gate the local player controls
			lastGateFrame = game.frame();
			Player *me = game.logic().players().getNthPlayer(myPlayer);
			for (Object *o = game.logic().getFirstObject(); o && me; o = o->getNextObject())
			{
				GateOpenAndCloseBehavior *gate = o->getControllingPlayer() == me ? GateOpenAndCloseBehavior::findGate(*o) : nullptr;
				if (gate && gate->isSettled())
				{
					GameMessage m(gate->isOpen() ? MSG_CLOSE_GATE : MSG_OPEN_GATE, myPlayer);
					m.appendObjectIDArgument(o->getID());
					game.commands().append(m);
					++gateMessages;
				}
			}
		}
		const UnsignedInt pf = game.protocolFrame();
		for (auto &bo : a.blackouts)
		{
			if (bo.first >= 0 && pf >= (UnsignedInt)bo.first)
			{
				session.transport().impairment().blackout(NetMilliseconds(), (std::uint64_t)bo.second); // lane MP-3: the cable is pulled
				blackoutLog.push_back("frame " + std::to_string(pf) + " for " + std::to_string(bo.second) + " ms");
				bo.first = -1;
			}
		}
		if (a.pauseAt >= 0 && pf >= (UnsignedInt)a.pauseAt)
		{
			// SMOOTH-1: a pause: presentation-only advances; the transport keeps being pumped and no batch is acquired
			const std::uint64_t p0 = NetMilliseconds();
			while (NetMilliseconds() - p0 < (std::uint64_t)a.pauseMs)
			{
				session.service();
				game.advance(0.0);
				NetSleepMilliseconds(2);
			}
			a.pauseAt = -1;
			lastProgress = NetMilliseconds();
		}
		if (a.replaceDriverAt >= 0 && pf >= (UnsignedInt)a.replaceDriverAt)
		{
			game.setFrameDriver(nullptr); // drains: the queued batches run, their completions reach the driver
			game.setFrameDriver(&session.driver());
			a.replaceDriverAt = -1;
		}
		if (a.toggleThreadAt >= 0 && pf >= (UnsignedInt)a.toggleThreadAt)
		{
			game.setLogicThread(!game.logicThread());
			game.setLogicThread(!game.logicThread());
			a.toggleThreadAt = -1;
		}
		double step = (double)a.stepMs / 1000.0;
		if (a.paced)
		{
			// lane MP-3: a client's render loop: the real time since the last advance (the logic's 5 Hz clock decides when a frame is due)
			const std::uint64_t t = NetMilliseconds();
			step = (double)(t - lastAdvance) / 1000.0;
			lastAdvance = t;
		}
		const int ran = game.advance(step);
		if (a.paced)
		{
			NetSleepMilliseconds(5);
		}
		if (ran > 0)
		{
			lastProgress = NetMilliseconds();
			if (a.realtime && !a.paced)
			{
				NetSleepMilliseconds((std::uint32_t)a.stepMs);
			}
		}
		else
		{
			NetSleepMilliseconds(1);
			if (NetMilliseconds() - lastProgress > noProgressMs)
			{
				std::fprintf(stderr, "openbfme_peer: no frame for %llu ms (waiting on frame %u)\n", (unsigned long long)noProgressMs, game.frame());
				exitCode = 3;
				break;
			}
		}
	}
	session.finish(game);
	const std::uint64_t drain = NetMilliseconds();
	while (!session.allAcked() && NetMilliseconds() - drain < 5000)
	{
		session.service();
		session.network().update(game.protocolFrame(), game.commands());
		NetSleepMilliseconds(2);
	}
	// a late desync report half: give it a moment
	if (!session.network().desyncs().empty())
	{
		const std::uint64_t wait = NetMilliseconds();
		while (session.network().desyncs().front().halves.size() < 2 && NetMilliseconds() - wait < 3000)
		{
			session.service();
			session.network().update(game.protocolFrame(), game.commands());
			NetSleepMilliseconds(2);
		}
	}
	if (!a.census.empty())
	{
		// lane MP-3: the census CSV (frame = the logic frame after the batch ran)
		std::ofstream c(a.census, std::ios::binary | std::ios::trunc);
		c << "frame,sim_us,battalions,troops,objects\n";
		for (const LockstepDriver::CensusRow &row : session.driver().censusLog())
		{
			c << row.frame << "," << row.simUs << "," << row.battalions << "," << row.troops << "," << row.objects << "\n";
		}
	}
	// ---- report ----
	const Network &net = session.network();
	std::ostringstream r;
	char b[96];
	r << "slot " << lobby.localSlot << "\n";
	r << "player " << myPlayer << "\n";
	r << "frames " << game.frame() << "\n";
	if (quitAtFrame >= 0)
	{
		r << "quit_at " << quitAtFrame << "\n"; // lane MP-3
	}
	r << "logic_threads " << JobSystem::logic().threadCount() << "\n";
	std::snprintf(b, sizeof(b), "final_hash 0x%08X\n", game.logic().computeStateHash());
	r << b;
	r << "crc_checks_passed " << net.crcChecksPassed() << "\n";
	r << "desyncs " << net.desyncs().size() << "\n";
	r << "commands_relayed " << net.commandsRelayed() << "\n";
	r << "scripted_commands " << scripted << "\n";
	r << "gate_messages " << gateMessages << "\n";
	{
		// lane HUD-5: every gate's state at the end (id:state, GateOpenAndCloseBehavior's 0 opening, 1 open, 2 closing, 3 closed)
		r << "gates";
		for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (const GateOpenAndCloseBehavior *gate = GateOpenAndCloseBehavior::findGate(*o))
			{
				r << " " << o->getID() << ":" << (int)gate->state();
			}
		}
		r << "\n";
	}
	r << "stalled_frames " << game.stalledFrames() << "\n";
	r << "seconds " << (double)(NetMilliseconds() - runStart) / 1000.0 << "\n";
	const Transport::Stats &ts = session.transport().stats();
	r << "transport packets_sent " << ts.packetsSent << " received " << ts.packetsReceived << " resent " << ts.commandsResent << " dropped " << ts.dropped
	  << " bad " << ts.badPackets << " duplicates " << ts.duplicates << " fragmented " << ts.fragmentedCommands << " send_errors " << ts.sendErrors << " fast_resends " << ts.fastResends << "\n";
	r << "transport_unacked " << session.transport().unackedRecords() << "\n";
	{
		// lane MP-3: the harness's faults and what they did; the smoothness (wall clock, protocol owner)
		const NetImpairment &im = session.transport().impairment();
		r << "impairment_config faults " << im.config().all.text() << " seed " << im.config().seed << "\n";
		for (int sl = 0; sl < MAX_SLOTS; ++sl)
		{
			if (im.config().linkSet[(size_t)sl])
			{
				r << "impairment_link " << sl << " " << im.config().link[(size_t)sl].text() << "\n";
			}
		}
		r << "impairment " << im.statsText() << "\n";
		for (const std::string &l : blackoutLog)
		{
			r << "blackout " << l << "\n";
		}
		const LockstepDriver::SmoothStats &sm = session.driver().smoothStats();
		r << "input_latency_ms " << LockstepDriver::percentiles(sm.inputLatencyMs) << " unmatched " << sm.unmatchedCommands << "\n";
		unsigned long long over100 = 0, over500 = 0, over1000 = 0, total = 0;
		for (std::uint32_t v : sm.stallMs)
		{
			over100 += v >= 100 ? 1 : 0;
			over500 += v >= 500 ? 1 : 0;
			over1000 += v >= 1000 ? 1 : 0;
			total += v;
		}
		r << "stall_ms " << LockstepDriver::percentiles(sm.stallMs) << " total " << total << " over100 " << over100 << " over500 " << over500 << " over1000 "
		  << over1000 << "\n";
	}
	r << "peers_retired " << ts.peersRetired << "\n";
	r << "profile " << w.profile.digest << "\n";
	r << "engine_id " << ProfileIdentity::buildId() << " (provenance " << ProfileIdentity::gitProvenance() << ")\n";
	for (const std::string &e : session.transport().errors())
	{
		r << "transport_error " << e << "\n";
	}
	for (const std::string &e : session.recordingErrors())
	{
		r << "replay_error " << e << "\n";
	}
	for (const std::string &e : net.errors())
	{
		r << "network_error " << e << "\n";
	}
	for (const std::string &s : session.driver().longStalls())
	{
		r << "long_stall " << s << "\n";
	}
	int defeated = 0;
	for (int i = 0; i < game.logic().players().getPlayerCount(); ++i)
	{
		defeated += game.logic().players().getNthPlayer(i)->isDefeated() ? 1 : 0;
	}
	r << "objects " << game.logic().report().objects << "\n";
	r << "players_defeated " << defeated << "\n";
	for (const auto &kv : scriptedTypes)
	{
		r << "scripted_type " << kv.first << " " << kv.second << "\n";
	}
	{
		// what the archive's features did in this game (shared state: equal on every peer)
		const BuildCommands::Stats &bs = game.buildCommands().stats();
		r << "build foundations " << bs.foundations << " dozer_builds " << bs.dozerBuilds << " wall_spans " << bs.wallSpans << " refused " << bs.refused << "\n";
		std::map<std::string, int> refusals;
		for (const std::string &why : game.buildCommands().refusals())
		{
			++refusals[why.substr(0, 90)];
		}
		for (const auto &kv : refusals)
		{
			r << "build_refused " << kv.second << " x " << kv.first << "\n";
		}
		int heroes = 0, walls = 0, upgrades = 0;
		const int heroBit = ObjectTemplateInfoBuilder::kindOfIndex("HERO"), wallBit = ObjectTemplateInfoBuilder::kindOfIndex("WALL");
		for (const Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			heroes += heroBit >= 0 && o->isKindOf((unsigned)heroBit) ? 1 : 0;
			walls += wallBit >= 0 && o->isKindOf((unsigned)wallBit) ? 1 : 0;
		}
		for (int i = 0; i < game.logic().players().getPlayerCount(); ++i)
		{
			const Player *p = game.logic().players().getNthPlayer(i);
			upgrades += p ? (int)p->science().sciences().size() : 0;
		}
		r << "features heroes " << heroes << " wall_objects " << walls << " sciences " << upgrades << "\n";
		// lane HERO-2: every installed Create-a-Hero (from the setup): its player, name, unique id, the record's checksum, the object made from it
		for (const auto &h : game.logic().createAHeroes().heroes())
		{
			const Object *o = h.second.objectID ? game.logic().findObjectByID(h.second.objectID) : nullptr;
			const Player *p = game.logic().players().getNthPlayer(h.first);
			char hb[64];
			std::snprintf(hb, sizeof(hb), "0x%08X", h.second.checksum);
			r << "create_a_hero " << h.first << " " << std::string(h.second.name.begin(), h.second.name.end()) << " " << h.second.uniqueID << " checksum " << hb
			  << " surcharge " << (p ? p->getCreateAHeroSurcharge() : -1) << " can_build "
			  << (p && TheCreateAHeroSystem && p->hasUpgradeComplete(TheCreateAHeroSystem->canBuildUpgradeName) ? 1 : 0) << " made "
			  << (o && o->getTemplate() && o->getTemplate()->getName() == "CreateAHero" ? 1 : 0) << "\n";
		}
	}
	{
		// lane MP-2: the disconnect path
		const DisconnectManager &dm = net.disconnectManager();
		r << "disconnect_quit " << (disconnectQuit.empty() ? "none" : disconnectQuit) << "\n";
		std::string dropped;
		for (int s : dm.dropped())
		{
			dropped += (dropped.empty() ? "" : ",") + std::to_string(s);
		}
		r << "dropped_by_me " << (dropped.empty() ? "none" : dropped) << "\n";
		std::string disc;
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			if (net.isDisconnected(s))
			{
				disc += (disc.empty() ? "" : ",") + std::to_string(s);
			}
		}
		r << "disconnected_slots " << (disc.empty() ? "none" : disc) << "\n";
		r << "frames_resent " << net.framesResent() << " filled " << net.framesFilledFromResend() << "\n";
		// lane MP-3 (review r1): the packet router of the last frame this peer ran (after a router's leave or drop, its successor), then the one now (the
		// leaves at the end of the game may have passed the role on again)
		r << "packet_router " << net.packetRouterAt(game.frame() > 0 ? game.frame() - 1 : 0) << " at_frame " << (game.frame() > 0 ? game.frame() - 1 : 0) << " now "
		  << net.packetRouterSlot() << "\n";
		r << "run_ahead " << net.currentRunAhead() << " changes " << net.runAheadChanges() << " worst_latency_ms " << net.maxRoundTripMs() << "\n";
		const SelfDestruct::Stats &sd = SelfDestruct::stats();
		for (const std::string &cp : checkpoints)
		{
			r << "self_destruct_checkpoint " << cp << "\n";
		}
		r << "self_destruct executed " << sd.executed << " transfers " << sd.transfers << " kills " << sd.kills << " objects " << sd.objectsTransferred
		  << " upgrades " << sd.upgradesTransferred << "\n";
		for (const std::string &l : dm.log())
		{
			r << "disconnect_log " << l << "\n";
		}
		// every game slot's player: its live objects (a leaver's go to its ally or die) and whether it is defeated
		const LiveGame::Report lr = game.report();
		for (int sl = 0; sl < MAX_SLOTS && (size_t)sl < lr.startSlotPlayers.size(); ++sl)
		{
			const Player *p = game.logic().players().findPlayerWithName(lr.startSlotPlayers[(size_t)sl]);
			if (!p || !lobby.game.game.slots[sl].isOccupied())
			{
				continue;
			}
			int n = 0;
			for (const Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
			{
				n += o->getControllingPlayer() == p && !o->isEffectivelyDead() ? 1 : 0;
			}
			r << "slot_objects " << sl << " " << n << " defeated " << (p->isDefeated() ? 1 : 0) << "\n";
			// lane MP-3: what the score screen shows of the slot (ScoreKeeper, RW 0x79DFFA's score at this frame): equal on every peer
			const ScoreKeeper &sk = p->getScoreKeeper();
			r << "score " << sl << " " << sk.computeScore(w.settings, game.frame()) << " units_built " << sk.unitsBuilt() << " units_lost " << sk.unitsLost()
			  << " units_destroyed " << sk.totalUnitsDestroyed() << " structures_built " << sk.structuresBuilt() << " structures_lost " << sk.structuresLost()
			  << " units_alive " << sk.unitsAlive() << " structures_alive " << sk.structuresAlive() << " money_earned " << sk.moneyEarned() << "\n";
		}
	}
	for (const std::string &st : Network::stopLines())
	{
		r << "stop " << st << "\n";
	}
	for (const std::string &st : DisconnectManager::stopLines())
	{
		r << "stop " << st << "\n";
	}
	for (const std::string &st : NetImpairment::stopLines())
	{
		r << "stop " << st << "\n"; // lane MP-3
	}
	session.service(); // lane MP-2: the desync dumps with every half that arrived
	for (const std::string &dump : session.desyncDumps())
	{
		r << "desync_dump " << dump << "\n";
	}
	for (const std::string &e : session.desyncDumpErrors())
	{
		r << "desync_dump_error " << e << "\n";
	}
	for (const DesyncReport &d : net.desyncs())
	{
		r << d.text();
	}
	writeReport(a.report, r.str());
	if (!a.hashes.empty())
	{
		std::ofstream h(a.hashes, std::ios::binary | std::ios::trunc);
		for (size_t i = 0; i < hashes.size(); ++i)
		{
			std::snprintf(b, sizeof(b), "%u 0x%08X 0x%08X\n", hashes[i].frame, hashes[i].hash, hashes[i].rng);
			h << b;
		}
	}
	if (exitCode == 0 && net.sawCRCMismatch())
	{
		exitCode = 2;
	}
	if (exitCode == 0 && (!net.errors().empty() || !session.recordingErrors().empty()))
	{
		exitCode = 4; // input that could not be sent or recorded: the session failed even without a desync
	}
	return exitCode;
}
