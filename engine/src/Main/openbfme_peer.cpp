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
//     --host PORT [--bind IP] --map KEY --seed N --slot "human|easy|medium|hard|brutal,FactionName,startPos,team[,handicap]" (repeated, in slot order;
//       lane OWNERFIX-2: the handicap 0, -5 .. -95, default 0; the report lists each playable player's `player_handicap`)
//     | --join IP:PORT --name NAME
//     [--frames N] [--run-ahead N] [--crc-interval N] [--script] [--realtime] [--record FILE] [--hashes FILE] [--report FILE]
//     [--inject-divergence FRAME] [--drop PERMILLE] [--delay MS] [--jitter MS] [--feature NAME] [--lobby-timeout S]
//     lane UBER-1: [--uber] this peer runs the Uber simulation profile (docs/UBER.md): its identity says "profile uber" (a Faithful peer is refused at the
//     lobby) and a game it hosts runs Uber
//     SMOOTH-1 (review r4, the logic worker in a network game): [--logic-thread] the frames run on LiveGame's logic worker; [--step-ms N] the render step
//     (advance(N ms) per loop, default 200); [--worker-delay-ms N] the worker sleeps N ms after every frame (a slow simulation owner);
//     [--pause-at FRAME:MS] at that protocol frame only advance(0) for MS ms (presentation-only: the transport is pumped, no batch runs);
//     [--replace-driver-at FRAME] remove and reinstall the frame driver there; [--toggle-thread-at FRAME] switch the worker off and on there
//     lane MP-2 (the disconnect path): [--vanish-at FRAME] the process stops at that protocol frame without leaving (a crash: its report says so, nothing
//     more is sent); [--disconnect-ms N] / [--player-timeout-ms N] replace GameData's NetworkDisconnectTime / NetworkPlayerTimeoutTime (tests);
//     [--kick] the local player presses Kick for every player the disconnect screen offers it for; [--desync-dir DIR] where a desync dump goes
//     (GameNetwork/DesyncDump.h, default the current directory); [--fixed-run-ahead] keep the lobby's run-ahead (no adaptation to the round trip)
//     lane MP-2 (the LAN lobby, GameNetwork/LANAPI.h): --lan-host or --lan-join --name NAME [--lan-faction FactionName] [--lan-port-base N]
//     [--lan-targets a.b.c.d,...] [--lan-pump-ms N]
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
//     lane NETLAG-2: [--script-phase MS|random] with --script: the orders go out MS ms into each frame (random: anywhere in it, as a click lands);
//     the report's run_ahead_frames line counts the frames run at each run-ahead; [--hitch-at FRAME:MS] (repeated) at that protocol frame the process
//     stops for MS ms without servicing anything (a client's render hitch: its acknowledgements come late); [--no-early-orders] every order waits for
//     the run-ahead's frame (NetworkConfig::earlyOrders off); the report's early_orders line counts the orders that ran a frame sooner
//     lane HERO-2: [--create-a-hero SLOT:FILE.cah] (host, repeated) the slot's Create-a-Hero, read at the setup and carried by the START to every peer;
//     with --lan-join [--create-a-hero FILE.cah] the own slot's, sent to the lobby's host (LANAPI::requestSlotCreateAHero); [--recruit-create-a-hero FRAME]
//     with --script: from that frame the local player recruits its Create-a-Hero at its fortress until it exists
//     lane ONLINE-2 (online play through the relay, docs/ONLINE.md phase O1): --online-start FILE.json instead of --host / --join / --lan-join: plays the
//     game of the START the lobby service built (GameNetwork/OnlineStart.h), every peer behind the relay (Transport relay mode; the socket binds --bind,
//     0.0.0.0 by default here, any port); the report gets a relay line
//     lane ONLINE-3 (reconnect and pause, docs/ONLINE.md "Phase O3"): a START with a rejoin ticket (the lobby's /v1/games/<id>/rejoin) rejoins the running
//     game: the relay's log is fetched and re-simulated at full speed, then the peer plays in lockstep (the report's rejoin lines: the catch-up time,
//     the frames, the CRCs compared); [--net-pause-at FRAME] at that protocol frame the local player pauses the game (every peer stops before one
//     frame); [--net-resume-ms MS] once the game has been paused MS ms this peer resumes it (any player may); [--rejoin-window-ms N] an online
//     game's rejoin window (default 300000)
//   openbfme_peer --rotwk DIR --bfme2 DIR --replay FILE [--report FILE]
//   lane ANTICHEAT-1: ... --replay FILE --fog-check OUT.json [--game-id ID]   also runs the integrity check D1 (GameNetwork/OrdersIntoFog.h: orders on
//     objects the player could not see) over the re-simulation and writes its JSON report; the check abstains when the replay lost a command, has no
//     recorded hashes, starts from a save or the re-simulation does not reproduce them (crc_parity); without an end record (a relay log's replay) the
//     orders up to the last recorded hash are judged;
//     [--input-rate-check OUT2.json] D5 (GameNetwork/CommandStreamChecks.h: more than 100 commands in 5 s of game time, informational),
//     [--command-check OUT3.json] D6 (commands a clean client never sends), in the same pass and under the same abstain rules;
//     [--fog-check-dump DIR --fog-check-view PLAYER --fog-check-around F1,F2 [--fog-check-before 50] [--fog-check-after 15]] the evidence view: for every
//     state from `before` frames ahead of each listed frame to `after` frames past it, DIR/frame_NNNNNN.txt holds PLAYER's shroud cells (c / f / s) and
//     every player object (own / ally / neutral / enemy, seen or not by PLAYER), DIR/terrain.txt the grid's ground heights (tools/integrity/render_evidence.py)
//   lane SAVE-2 (docs/SAVES.md T1, the fresh process): openbfme_peer ... --load-save SAVE --replay FILE [--report FILE]   loads the save (an .obfmesave of
//     this build: its log is caught up to its frame F, every recorded hash compared, GameClient/CommandLogLoad.h), then plays the rest of FILE (the
//     uninterrupted game's replay, the same game) from F with every frame's hash compared; the report adds save_frame and catch_up_hashes; exit 2 on a
//     refused save, a failed catch-up or any later mismatch (a campaign mission's save starts its mission from the save's campaign start, the replay's
//     map directory and the save's difficulty); lane SAVE-4e: a save with a snapshot loads from it (no frame of its log runs; the report's load_mode
//     snapshot, snapshot_bytes, load_ms), [--verify-log] catches it up from its log instead and compares the snapshot; --profile-text FILE
//     writes this build's profile identity text to FILE (what a test records
//     the game under, so the save is this build's) and exits
//   lane ONLINE-2: openbfme_peer --rotwk DIR --bfme2 DIR --print-profile   prints this build's profile identity MD5 (what the lobby compares),
//     then "faction INDEX NAME" lines (the START's faction numbers); lane STATS-1: [--print-ids] also "thing ID NAME", "upgrade BIT NAME",
//     "science KEY NAME" and "power ID NAME" lines: the names behind the ids a GameMessage carries (they follow this build's INI load order), for
//     the console's stats pipeline (reads the relay logs; never runs the game)
//     lane ONLINE-3: a spectator START ("openbfme-spectate", the lobby's /v1/games/<id>/spectate) watches the game read-only: the relay's log (held back by
//     the spectator delay) is re-simulated as it grows, with no local slot (the observer's ReplayObserver side: no shroud), until --frames or the
//     log's end; the report's spectate lines (frames, CRCs compared with the players', the relay's refusals) and final_hash at that frame;
//     [--spectate-forge] it sends one forged frame command to the relay (which must refuse it); [--spectate-observe N] every N frames its view steps
//     to the next player (retail's observe-next, RW 0x6A8D2B); [--frame-times] frame_ms lines: when each frame (a player) / frames 100, 200, ... (a spectator) ran
//   lane ONLINE-3: openbfme_peer --rotwk DIR --bfme2 DIR --relay-log LOG --online-start FILE.json --catch-up FRAME [--report FILE]   the rejoin's catch-up
//     (GameNetwork/Rejoin.h CommandLogCatchUp: the START + the log -> the state at FRAME, the players' CRCs compared on the way), timed: catch_up frames
//     N ms T crcs_compared C mismatches M; exit 2 on a CRC mismatch
//   lane ONLINE-2: openbfme_peer --rotwk DIR --bfme2 DIR --relay-log LOG --online-start FILE.json --record OUT [--report FILE]   (FILE.json: the lobby's
//     replay START, /v1/replays/<id>/start, or a player's START)
//     the relay's command log of that game (services/online/relay) assembled into a replay (GameNetwork/RelayLog.h); exit 0, or 2 when the players' CRCs
//     disagree in the log, 4 when the log is incomplete or the replay differs from the game (S-3710). `--replay OUT` then checks CRC parity
// Exit code: 0 ok, 1 usage / load error / another profile's replay, 2 desync (or replay mismatch / an incomplete replay), 3 network failure (stall /
// timeout, refused by the lobby: another profile), 4 network or recording errors (a command that could not be sent or recorded).

#include "Common/JobSystem.h"
#include "Common/PlayerTemplate.h"
#include "Common/BuildVersion.h"
#include "Common/Recorder.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/CommandLogLoad.h"
#include "Common/Sha256.h"
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
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetGameSession.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetworkSettings.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/Rejoin.h"
#include "GameNetwork/RelayProtocol.h"
#include "Common/Crc32.h"
#include "GameLogic/SelfDestruct.h"
#include "GameNetwork/ScriptedPlayer.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameNetwork/CommandStreamChecks.h"
#include "GameNetwork/FogCheckDriver.h"
#include "GameNetwork/OrdersIntoFog.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameLogic/SpellStores.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
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
	int runAhead = 1; // lane NETLAG-1 (was 2)
	int crcInterval = 100;
	bool script = false, realtime = false;
	// lane NETLAG-2: --script-phase MS|random: the scripted orders go out MS ms after the frame they are issued on began (random: uniform 0 .. 199 per
	// frame, a human's click lands anywhere in the 200 ms frame), not at its start; the click-to-effect times then sample the whole frame
	int scriptPhaseMs = 0;
	std::vector<std::pair<long long, int>> hitches; ///< lane NETLAG-2: --hitch-at FRAME:MS (repeated)
	std::string record, hashes, report, replay;
	std::string fogCheck, gameId; // lane ANTICHEAT-1
	std::string inputRateCheck, commandCheck; // lane ANTICHEAT-1 (owner 2026-10-21): D5, D6
	std::string fogDump; int fogView = -1; std::vector<UnsignedInt> fogAt; int fogBefore = 50, fogAfter = 15; // the evidence view's frames
	std::string loadSave; ///< lane SAVE-2: --load-save FILE (with --replay)
	bool verifyLog = false; ///< lane SAVE-4e: --verify-log (a snapshot save is caught up from its log and its snapshot compared)
	std::string snapshotOut; ///< lane SAVE-4e: --snapshot-out FILE (with --replay): the snapshot at the replay's end (T3, the bytes across OSes)
	std::string profileText; ///< lane SAVE-2: --profile-text FILE writes this build's profile identity text there and exits (a test records under it)
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
	bool noEarlyOrders = false; ///< lane NETLAG-2: --no-early-orders
	bool lanJoin = false, lanHost = false;
	int lanPumpMs = 5;
	bool lanPumpGiven = false;
	std::string lanFaction, lanTargets;
	int lanPortBase = -1;
	std::string platform; ///< lane PLATFORMICON-1: --platform windows|linux|macos (--lan-join: the PLATFORM_INFO this peer sends; none: it tells nothing, like an older build)
	bool uber = false;             ///< lane UBER-1: --uber
	int logicThreads = 0;          ///< PERF-2: --logic-threads N, the logic job pool's threads (0: OPENBFME_LOGIC_THREADS or the hardware's)
	int toggleGates = 0;                ///< lane HUD-5: --toggle-gates N (every N frames the local player toggles each settled gate it controls)
	long long recruitCreateAHeroAt = -1; ///< lane HERO-2: --recruit-create-a-hero FRAME (the local player recruits its Create-a-Hero from then on until made)
	std::vector<std::string> createAHeroes; ///< lane HERO-2: --create-a-hero SLOT:FILE.cah (host) / --create-a-hero FILE.cah (--lan-join: the own slot)
	std::string onlineStart, relayLog;      ///< lane ONLINE-2: --online-start FILE.json, --relay-log LOG
	bool bindGiven = false;
	bool printProfile = false;              ///< lane ONLINE-2: --print-profile (the lobby compares the profile identity's MD5)
	bool printIds = false;                  ///< lane STATS-1: --print-ids (with --print-profile: the id -> name tables)
	long long netPauseAt = -1;              ///< lane ONLINE-3: --net-pause-at FRAME
	long long netResumeMs = -1;             ///< lane ONLINE-3: --net-resume-ms MS
	long long rejoinWindowMs = -1;          ///< lane ONLINE-3: --rejoin-window-ms N
	long long catchUp = -1;                 ///< lane ONLINE-3: --catch-up FRAME (with --relay-log: time the rejoin's catch-up to that frame)
	bool spectateForge = false;             ///< lane ONLINE-3: --spectate-forge (a spectator sends one forged command: the relay must refuse it)
	int spectateObserveEvery = 0;           ///< lane ONLINE-3: --spectate-observe N (every N frames the spectator's view steps to the next player)
	bool frameTimes = false;                ///< lane ONLINE-3: --frame-times (the report's frame_ms lines: when every 100th frame ran)
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
		else if (k == "--bind" && next(a.bind)) a.bindGiven = true;
		else if (k == "--online-start" && next(a.onlineStart)) {}
		else if (k == "--relay-log" && next(a.relayLog)) {}
		else if (k == "--print-profile") a.printProfile = true;
		else if (k == "--print-ids") a.printProfile = a.printIds = true;
		else if (k == "--net-pause-at" && next(v)) a.netPauseAt = std::atoll(v.c_str());
		else if (k == "--net-resume-ms" && next(v)) a.netResumeMs = std::atoll(v.c_str());
		else if (k == "--rejoin-window-ms" && next(v)) a.rejoinWindowMs = std::atoll(v.c_str());
		else if (k == "--catch-up" && next(v)) a.catchUp = std::atoll(v.c_str());
		else if (k == "--spectate-forge") a.spectateForge = true;
		else if (k == "--spectate-observe" && next(v)) a.spectateObserveEvery = std::max(1, std::atoi(v.c_str()));
		else if (k == "--frame-times") a.frameTimes = true;
		else if (k == "--no-early-orders") a.noEarlyOrders = true;
		else if (k == "--hitch-at" && next(v) && v.find(':') != std::string::npos)
			a.hitches.emplace_back(std::atoll(v.c_str()), std::atoi(v.c_str() + v.find(':') + 1));
		else if (k == "--script-phase" && next(v)) a.scriptPhaseMs = v == "random" ? -1 : std::max(0, std::atoi(v.c_str()));
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
		else if (k == "--fog-check" && next(a.fogCheck)) {} // lane ANTICHEAT-1
		else if (k == "--game-id" && next(a.gameId)) {}
		else if (k == "--input-rate-check" && next(a.inputRateCheck)) {}
		else if (k == "--command-check" && next(a.commandCheck)) {}
		else if (k == "--fog-check-dump" && next(a.fogDump)) {}
		else if (k == "--fog-check-view" && next(v)) a.fogView = std::atoi(v.c_str());
		else if (k == "--fog-check-around" && next(v))
		{
			for (size_t p = 0; p < v.size();)
			{
				a.fogAt.push_back((UnsignedInt)std::strtoul(v.c_str() + p, nullptr, 10));
				const size_t c = v.find(',', p);
				p = c == std::string::npos ? v.size() : c + 1;
			}
		}
		else if (k == "--fog-check-before" && next(v)) a.fogBefore = std::atoi(v.c_str());
		else if (k == "--fog-check-after" && next(v)) a.fogAfter = std::atoi(v.c_str());
		else if (k == "--load-save" && next(a.loadSave)) {}
		else if (k == "--verify-log") { a.verifyLog = true; }
		else if (k == "--snapshot-out" && next(a.snapshotOut)) {}
		else if (k == "--profile-text" && next(a.profileText)) {}
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
		else if (k == "--uber") a.uber = true; // lane UBER-1
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
		else if (k == "--platform" && next(a.platform)) {} // lane PLATFORMICON-1: the system this peer tells the LAN lobby (windows / linux / macos)
		else if (k == "--lan-host") a.lanHost = true;
		else if (k == "--lan-pump-ms" && next(v))
		{
			char *end = nullptr;
			errno = 0;
			const long pumpMs = std::strtol(v.c_str(), &end, 10);
			if (errno == ERANGE || end == v.c_str() || *end != '\0' || pumpMs <= 0 || pumpMs > INT_MAX)
			{
				error = "--lan-pump-ms needs a positive integer";
				return false;
			}
			a.lanPumpMs = (int)pumpMs;
			a.lanPumpGiven = true;
		}
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
	if (a.lanHost && a.lanJoin)
	{
		error = "--lan-host and --lan-join are incompatible";
		return false;
	}
	if ((a.lanHost || a.lanJoin) && (a.hostPort != -1 || !a.join.empty() || !a.onlineStart.empty()))
	{
		error = "a LANAPI host or join cannot be combined with --host, --join, or --online-start";
		return false;
	}
	if (a.lanPumpGiven && !a.lanHost && !a.lanJoin)
	{
		error = "--lan-pump-ms requires --lan-host or --lan-join";
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
	w.profile = ProfileIdentity::compute(mr.mounted, RandomAlgorithm::ZH_CarryChain, a.features, a.uber ? SimulationProfile::Uber : SimulationProfile::Faithful);
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
	m.game.simulationProfile = a.uber ? SimulationProfile::Uber : SimulationProfile::Faithful; // lane UBER-1
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
		if (f.size() != 4 && f.size() != 5)
		{
			error = "--slot wants kind,faction,startPos,team[,handicap]: " + a.slots[i];
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
		s.playerTemplate = templateIndex(w.world->setupPlayerTemplates(), f[1]);
		if (s.playerTemplate < 0)
		{
			error = "unknown faction " + f[1];
			return false;
		}
		s.startPos = std::atoi(f[2].c_str());
		s.color = (int)i;
		s.teamNumber = std::atoi(f[3].c_str());
		s.handicap = f.size() == 5 ? std::atoi(f[4].c_str()) : 0; // lane OWNERFIX-2
		if (!SkirmishGameSlot::handicapValid(s.handicap) || (f.size() == 5 && std::to_string(s.handicap) != f[4]))
		{
			error = "--slot handicap " + (f.size() == 5 ? f[4] : std::string("0")) + " (0, -5 .. -95)";
			return false;
		}
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

bool configureLANOptions(const Args &a, const World &w, LANAPI::Options &lo, std::string &error)
{
	if (!a.platform.empty() && !platformFromName(a.platform, lo.platform))
	{
		error = "--platform must be windows, linux or macos, got " + a.platform;
		return false;
	}
	lo.playerTemplateCount = w.world->setupPlayerTemplates().getPlayerTemplateCount();
	lo.colorCount = (int)w.settings.multiplayerColors.size();
	lo.validateCreateAHero = [&w](const CreateAHeroHero &h, std::string *why) { return w.world->createAHeroSystem().validateHero(h, w.world->commands(), why); };
	if (a.lanPortBase != -1 && (a.lanPortBase < 1 || a.lanPortBase > 65528))
	{
		error = "--lan-port-base must be between 1 and 65528";
		return false;
	}
	if (a.lanPortBase > 0)
	{
		lo.lobbyPortBase = (std::uint16_t)a.lanPortBase;
	}
	if (!a.lanTargets.empty())
	{
		std::stringstream targets(a.lanTargets);
		for (std::string t; std::getline(targets, t, ',');)
		{
			NetAddress ta;
			if (t.empty() || !NetAddress::parse(t + ":0", ta))
			{
				error = "bad --lan-targets entry " + t;
				return false;
			}
			lo.broadcast = false;
			lo.extraTargets.push_back(ta.ip);
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

// lane ANTICHEAT-1: the evidence view's frames (openbfme_peer --fog-check-dump): what the viewed player's shroud showed and where every player object was
void dumpFogState(const Args &a, GameLogic &logic, UnsignedInt next)
{
	const ShroudManager *shroud = logic.shroud();
	const Player *viewer = logic.players().getNthPlayer(a.fogView);
	if (!shroud || !viewer)
	{
		return;
	}
	const int W = shroud->cellCountX(), H = shroud->cellCountY();
	const std::string terrain = a.fogDump + "/terrain.txt";
	if (!std::ifstream(terrain))
	{
		std::ofstream t(terrain, std::ios::binary | std::ios::trunc);
		t << "grid " << W << " " << H << " cell " << shroud->cellSize() << " origin " << shroud->originX() << " " << shroud->originY() << "\n";
		for (int y = 0; y < H; ++y)
		{
			for (int x = 0; x < W; ++x)
			{
				const float h = logic.getGroundHeight(shroud->originX() + ((float)x + 0.5f) * shroud->cellSize(),
					shroud->originY() + ((float)y + 0.5f) * shroud->cellSize());
				t << (x ? " " : "") << (int)std::lround(h);
			}
			t << "\n";
		}
	}
	bool wanted = false;
	for (UnsignedInt f : a.fogAt)
	{
		wanted = wanted || (next + (UnsignedInt)a.fogBefore >= f && next <= f + (UnsignedInt)a.fogAfter);
	}
	if (!wanted)
	{
		return;
	}
	char name[64];
	std::snprintf(name, sizeof(name), "/frame_%06u.txt", next);
	std::ofstream o(a.fogDump + name, std::ios::binary | std::ios::trunc);
	o << "frame " << next << " view " << a.fogView << "\n";
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			const CellShroudStatus c = shroud->getCellStatus(a.fogView, x, y);
			o << (c == CELLSHROUD_CLEAR ? 'c' : c == CELLSHROUD_FOGGED ? 'f' : 's');
		}
		o << "\n";
	}
	for (Object *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		const Player *owner = obj->getControllingPlayer();
		if (!owner || owner == logic.players().getNeutralPlayer() || obj->isEffectivelyDead())
		{
			continue;
		}
		const Relationship r = viewer->getRelationship(owner);
		const char *rel = owner == viewer ? "own" : r == ENEMIES ? "enemy" : r == ALLIES ? "ally" : "neutral";
		const ObjectShroudStatus st = shroud->clientObjectStatus(*obj, a.fogView);
		// what the client draws (Sol r2): not hidden by the shroud, not stealthed and undetected for the viewer; a contained object (no record) is
		// drawn by its container, not here
		const bool seen = shroud->hasRecord(*obj) && st != OBJECTSHROUD_SHROUDED && !InvisibilityManager::isStealthedAndUndetected(*obj, viewer);
		o << "o " << obj->getID() << " " << rel << " " << obj->getPosition()->x << " " << obj->getPosition()->y << " " << (seen ? 1 : 0) << " "
		  << (obj->isKindOfName("STRUCTURE") ? "s" : "u") << "\n";
	}
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
	// lane SAVE-2: a campaign mission's save names its campaign start (the mission's map directory, the difficulty): the mission is started from that,
	// as GameWorld::start_campaign_mission does, not from the recording's NewGameMessage
	SaveGameFile save;
	if (!a.loadSave.empty() && !SaveGameFile::load(a.loadSave, save, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 2;
	}
	const bool mission = !a.loadSave.empty() && !save.header.campaign.empty();
	NewGameStart start(replay.header.algorithm);
	if (!mission && !NewGame::prepareNewGame(replay.header.game, w.world->setupPlayerTemplates(), w.settings, w.maps, replay.header.algorithm, start, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	start.localSlot = replay.header.recordingSlot;
	ArchiveW3DFileSource source(*w.fs);
	WW3DAssetManager assets(source);
	LiveGame game(*w.world, *w.fs, assets, w.options);
	LiveGame::Options o;
	if (mission)
	{
		o.mapName = replay.header.game.game.mapName; // the mission's map directory ("map ang angmar")
		o.campaign = true;
		o.difficulty = save.header.campaignDifficulty;
	}
	else
	{
		o.start = &start;
	}
	if (!game.load(o, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	std::ostringstream r;
	if (!a.loadSave.empty())
	{
		// lane SAVE-2: the game is brought to the save's frame from the save's own log (a fresh process: nothing of the recording run is shared), then the
		// replay of the uninterrupted game supplies the frames after it
		const SaveGameCompatibility c = SaveGameCompatibility::check(save.header, w.profile, ProfileIdentity::buildId(), BuildVersion::kVersion, false);
		CommandLogLoad load;
		load.setVerifyLog(a.verifyLog);
		if (!c.loadable || !load.prepare(save, &error))
		{
			std::fprintf(stderr, "openbfme_peer: the save is refused: %s\n", c.loadable ? error.c_str() : c.refusal.c_str());
			return 2;
		}
		NetByteWriter ours, theirs;
		NetPacket::writeNewGameMessage(ours, load.log().header.game);
		NetPacket::writeNewGameMessage(theirs, replay.header.game);
		if (ours.data() != theirs.data() || load.log().header.profile != replay.header.profile || replay.finalFrame <= save.header.frame)
		{
			std::fprintf(stderr, "openbfme_peer: the save and the replay are not the same game (or the replay ends at the save)\n");
			return 1;
		}
		if (!load.begin(game, &error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 2;
		}
		while (load.step(game, 1000) == CommandLogLoad::State::CatchingUp)
		{
		}
		r << "save " << a.loadSave << "\n";
		r << "save_frame " << save.header.frame << "\n";
		r << "catch_up_hashes " << load.hashesCompared() << "\n";
		r << "load_mode " << (load.mode() == CommandLogLoad::Mode::Snapshot ? "snapshot" : "catch_up") << "\n"; // lane SAVE-4e
		r << "snapshot_bytes " << save.snapshot.size() << "\n";
		char loadMs[48];
		std::snprintf(loadMs, sizeof(loadMs), "load_ms %.1f\n", load.elapsedMs());
		r << loadMs;
		if (load.state() != CommandLogLoad::State::Ready)
		{
			r << "catch_up_failed " << load.failure() << "\n";
			writeReport(a.report, r.str());
			return 2;
		}
	}
	ReplayPlayback playback(replay);
	RegisterLogicCRCHandler(game.dispatch());
	// lane ANTICHEAT-1: the judged players are the recording's network slots (the human players; the AI's orders are not in the stream); one pass runs
	// D1 (--fog-check), D5 (--input-rate-check) and D6 (--command-check): the three are always computed together, each written when asked for
	std::unique_ptr<OrdersIntoFog> fogCheck;
	std::unique_ptr<InputRateCheck> rateCheck;
	std::unique_ptr<CommandIntegrityCheck> commandCheck;
	std::unique_ptr<FogCheckDriver> fogDriver;
	const bool checks = !a.fogCheck.empty() || !a.inputRateCheck.empty() || !a.commandCheck.empty();
	if (checks)
	{
		std::vector<int> humans;
		for (int p : replay.header.network.slotPlayerIndex)
		{
			if (p >= 0)
			{
				humans.push_back(p);
			}
		}
		fogCheck = std::make_unique<OrdersIntoFog>(game.logic(), humans);
		rateCheck = std::make_unique<InputRateCheck>(game.logic(), humans);
		commandCheck = std::make_unique<CommandIntegrityCheck>(game.logic(), humans);
		for (size_t slot = 0; slot < replay.header.network.slotPlayerIndex.size(); ++slot)
		{
			const int p = replay.header.network.slotPlayerIndex[slot];
			if (p >= 0)
			{
				fogCheck->setSlot(p, (int)slot);
				rateCheck->setSlot(p, (int)slot);
				commandCheck->setSlot(p, (int)slot);
			}
		}
		fogDriver = std::make_unique<FogCheckDriver>(playback, replay, *fogCheck);
		fogDriver->inputRate = rateCheck.get();
		fogDriver->commands = commandCheck.get();
		if (!a.fogDump.empty())
		{
			fogDriver->onState = [&a](GameLogic &logic, UnsignedInt next) { dumpFogState(a, logic, next); };
		}
		fogDriver->offer(game.logic());
		game.setFrameDriver(fogDriver.get());
	}
	else
	{
		game.setFrameDriver(&playback);
	}
	while (!playback.finished())
	{
		game.advance(0.2);
	}
	game.drainFrames();
	if (checks)
	{
		std::string parity = "none";
		std::vector<std::string> abstain;
		if (playback.hasMismatch())
		{
			parity = "mismatch";
			abstain.push_back("the re-simulation does not reproduce the recorded state hashes (first at frame " + std::to_string(playback.firstMismatch().frame) + ")");
		}
		else if (playback.hashesCompared() > 0)
		{
			parity = "match";
		}
		else
		{
			abstain.push_back("the replay has no recorded state hashes to compare");
		}
		// a replay without an end record (a relay log's, S-3710, or an interrupted recording) plays up to its last recorded hash: every order judged
		// lies before a state the players' own CRCs confirm, so the missing tail is a coverage limit (frames), not a reason to abstain
		if (!a.loadSave.empty())
		{
			abstain.push_back("the re-simulation started from a save, not from the game's first frame");
		}
		if (!replay.lost.empty())
		{
			abstain.push_back("the replay lost " + std::to_string(replay.lost.size()) + " command(s)");
		}
		if (replay.complete && game.frame() >= replay.finalFrame && game.logic().computeStateHash() != replay.finalHash)
		{
			abstain.push_back("the re-simulation's final state hash differs from the replay's end record"); // Sol r2: the end record is a hash too
		}
		if (game.frame() < replay.finalFrame)
		{
			abstain.push_back("the re-simulation stopped at frame " + std::to_string(game.frame()) + " of " + std::to_string(replay.finalFrame));
		}
		for (const std::string &why : abstain)
		{
			fogCheck->abstain(why);
			rateCheck->abstain(why);
			commandCheck->abstain(why);
		}
		const std::pair<const std::string *, std::string> outputs[3] = { { &a.fogCheck, fogCheck->json(a.gameId, parity, game.frame()) },
			{ &a.inputRateCheck, rateCheck->json(a.gameId, parity, game.frame()) }, { &a.commandCheck, commandCheck->json(a.gameId, parity, game.frame()) } };
		for (const auto &o : outputs)
		{
			if (o.first->empty())
			{
				continue;
			}
			std::ofstream fo(*o.first, std::ios::binary | std::ios::trunc);
			fo << o.second << "\n";
			if (!fo)
			{
				std::fprintf(stderr, "openbfme_peer: cannot write %s\n", o.first->c_str());
				return 1;
			}
		}
		r << "fog_check " << (a.fogCheck.empty() ? "-" : a.fogCheck) << " status " << (fogCheck->abstained() ? "abstained" : "ok") << " samples "
		  << fogCheck->samples() << "\n";
		for (const OrdersIntoFog::PlayerResult &pr : fogCheck->results())
		{
			r << "fog_check_player " << pr.player << " band " << pr.band << " events " << pr.events.size() << " encounters " << pr.encounters << " judged "
			  << pr.judged << "\n";
		}
		for (const InputRateCheck::PlayerResult &pr : rateCheck->results())
		{
			r << "input_rate_player " << pr.player << " commands " << pr.commands << " peak_5s " << pr.peak << " episodes " << pr.episodes.size() << "\n";
		}
		for (const CommandIntegrityCheck::PlayerResult &pr : commandCheck->results())
		{
			r << "command_check_player " << pr.player << " commands " << pr.commands << " events " << pr.events.size() << "\n";
		}
	}
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
	if (!a.snapshotOut.empty())
	{
		// lane SAVE-4e (T3): the snapshot of the replay's last frame, written as is; its size and SHA-256 in the report
		std::vector<std::uint8_t> snap;
		if (!game.logic().saveSnapshot(snap, &error))
		{
			r << "snapshot_refused " << error << "\n";
			writeReport(a.report, r.str());
			return 2;
		}
		std::ofstream so(a.snapshotOut, std::ios::binary | std::ios::trunc);
		so.write(reinterpret_cast<const char *>(snap.data()), (std::streamsize)snap.size());
		if (!so)
		{
			std::fprintf(stderr, "openbfme_peer: cannot write %s\n", a.snapshotOut.c_str());
			return 1;
		}
		r << "snapshot_frame " << game.logic().getFrame() << "\n";
		r << "snapshot_bytes " << snap.size() << "\n";
		const std::array<std::uint8_t, 32> digest = Sha256::hash(snap.data(), snap.size());
		r << "snapshot_sha256 " << HexBytes(digest.data(), digest.size()) << "\n";
	}
	if (playback.hasMismatch())
	{
		std::snprintf(b, sizeof(b), "first_mismatch frame %u recorded 0x%08X played 0x%08X\n", playback.firstMismatch().frame, playback.firstMismatch().recorded,
			playback.firstMismatch().played);
		r << b;
	}
	writeReport(a.report, r.str());
	return playback.hasMismatch() || !replay.lost.empty() || (replay.complete && finalHash != replay.finalHash) ? 2 : 0;
}
// lane ONLINE-2: the START file the lobby service built, as the LobbyStart a LAN lobby would give
bool readOnlineStart(const Args &a, const World &w, LobbyStart &out, std::string &error, OnlineStart::Kind kind = OnlineStart::Kind::Start)
{
	std::ifstream f(a.onlineStart, std::ios::binary);
	if (!f)
	{
		error = "cannot read the online START " + a.onlineStart;
		return false;
	}
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	OnlineStart::Limits limits;
	limits.playerTemplateCount = w.world->setupPlayerTemplates().getPlayerTemplateCount();
	limits.colorCount = (int)w.settings.multiplayerColors.size();
	return OnlineStart::parse(text, w.profile, limits, out, &error, kind);
}

// lane ONLINE-2: the relay's command log -> a replay (GameNetwork/RelayLog.h)
int runRelayLog(const Args &a, World &w)
{
	std::string error;
	LobbyStart lobby;
	RelayLogFile log;
	if (a.onlineStart.empty() || (a.record.empty() && a.catchUp < 0))
	{
		std::fprintf(stderr, "openbfme_peer: --relay-log needs --online-start FILE.json (the game's START) and --record OUT (or --catch-up FRAME)\n");
		return 1;
	}
	// ONLINE-2 r2: the lobby's published replay START (no ticket); a player's own START is accepted too (the soak's fixtures)
	std::string replayError;
	if (!readOnlineStart(a, w, lobby, replayError, OnlineStart::Kind::ReplayStart) && !readOnlineStart(a, w, lobby, error))
	{
		std::fprintf(stderr, "openbfme_peer: %s (as a replay START: %s)\n", error.c_str(), replayError.c_str());
		return 1;
	}
	if (!RelayLogFile::load(a.relayLog, log, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	if (log.gameId != lobby.onlineGameId)
	{
		std::fprintf(stderr, "openbfme_peer: the log is of game %016llx, the START of %016llx\n", (unsigned long long)log.gameId,
			(unsigned long long)lobby.onlineGameId);
		return 1;
	}
	// the slot -> player map of the loaded game, as NetGameSession::begin reads it
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	if (!NewGame::prepareNewGame(lobby.game, w.world->setupPlayerTemplates(), w.settings, w.maps, RandomAlgorithm::ZH_CarryChain, start, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	int recordingSlot = -1;
	for (int s = 0; s < MAX_SLOTS && recordingSlot < 0; ++s)
	{
		recordingSlot = lobby.networkSlot[(size_t)s] ? s : -1;
	}
	start.localSlot = recordingSlot;
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
	NetworkConfig config;
	config.localSlot = recordingSlot;
	config.runAhead = lobby.runAhead;
	config.crcInterval = lobby.crcInterval;
	const LiveGame::Report lr = game.report();
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		const Player *p = lobby.networkSlot[(size_t)s] && (size_t)s < lr.startSlotPlayers.size() ? game.players().findPlayerWithName(lr.startSlotPlayers[(size_t)s]) : nullptr;
		config.slotPlayerIndex[(size_t)s] = p ? p->getPlayerIndex() : -1;
	}
	RelayLog::Assembled assembled;
	if (!RelayLog::assemble(log, config, assembled, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	if (a.catchUp >= 0)
	{
		// lane ONLINE-3: the rejoin's catch-up alone, timed (what a rejoining client spends before it plays: the log's frames at full speed)
		auto shared = std::make_shared<RelayLog::Assembled>(assembled);
		CommandLogCatchUp catchUp(config.crcInterval);
		catchUp.setLog(shared);
		catchUp.setTarget((UnsignedInt)a.catchUp);
		RegisterLogicCRCHandler(game.dispatch());
		const UnsignedInt from = game.frame();
		const std::uint64_t t0 = NetMilliseconds();
		game.setFrameDriver(&catchUp);
		while (game.advance(0.2) > 0)
		{
		}
		game.drainFrames();
		const std::uint64_t ms = NetMilliseconds() - t0;
		game.setFrameDriver(nullptr);
		std::ostringstream r;
		char b[64];
		std::snprintf(b, sizeof b, "0x%08X", game.logic().computeStateHash());
		r << "catch_up frames " << (game.frame() - from) << " to_frame " << game.frame() << " ms " << ms << " frames_per_second "
		  << (ms ? (unsigned long long)(game.frame() - from) * 1000ull / ms : 0ull) << " crcs_compared " << catchUp.crcsCompared() << " mismatches "
		  << catchUp.crcMismatches() << " hash " << b << " log_complete_frame " << assembled.lastCompleteFrame << "\n";
		if (!catchUp.firstMismatch().empty())
		{
			r << "catch_up_first_mismatch " << catchUp.firstMismatch() << "\n";
		}
		writeReport(a.report, r.str());
		return catchUp.crcMismatches() ? 2 : 0;
	}
	ReplayHeader h;
	h.game = lobby.game;
	h.recordingSlot = recordingSlot;
	h.algorithm = RandomAlgorithm::ZH_CarryChain;
	h.profile = w.profile;
	h.network = config;
	if (!RelayLog::writeReplay(a.record, h, assembled, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	std::ostringstream r;
	char b[64];
	std::snprintf(b, sizeof b, "%016llx", (unsigned long long)log.gameId);
	r << "relay_log " << a.relayLog << " game " << b << " complete " << (log.complete ? 1 : 0) << " end_reason " << log.endReason << "\n";
	r << "relay_log_commands " << log.commands.size() << " frames_with_commands " << assembled.frames.size() << " last_complete_frame "
	  << assembled.lastCompleteFrame << "\n";
	r << "relay_log_crc_frames " << assembled.crcFrames << " disagreements " << assembled.crcDisagreements << " hashes " << assembled.hashes.size() << "\n";
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (log.slotMask & (1u << s))
		{
			r << "relay_log_slot " << s << " datagrams_in " << log.datagramsIn[(size_t)s] << " bytes_in " << log.bytesIn[(size_t)s] << " dropped "
			  << log.dropped[(size_t)s] << " leave_frame " << (assembled.leaveFrame[(size_t)s] == 0xFFFFFFFFu ? std::string("none") : std::to_string(assembled.leaveFrame[(size_t)s]))
			  << "\n";
		}
	}
	for (const std::string &e : log.events)
	{
		r << "relay_log_event " << e << "\n";
	}
	for (const std::string &g : log.gaps)
	{
		r << "relay_log_gap " << g << "\n";
	}
	for (const std::string &p : assembled.problems)
	{
		r << "relay_log_problem " << p << "\n";
	}
	r << "replay " << a.record << "\n";
	for (const std::string &st : RelayLog::stopLines())
	{
		r << "stop " << st << "\n";
	}
	writeReport(a.report, r.str());
	return assembled.crcDisagreements ? 2 : !assembled.problems.empty() || !log.gaps.empty() ? 4 : 0;
}
// lane ONLINE-3: a spectator (NetGameSession::beginSpectate): read-only, no fog (no local slot: the ReplayObserver side is the local player)
int runSpectator(const Args &a, World &w, UDP &socket, const LobbyStart &lobby)
{
	std::string error;
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	if (!NewGame::prepareNewGame(lobby.game, w.world->playerTemplates(), w.settings, w.maps, RandomAlgorithm::ZH_CarryChain, start, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	start.localSlot = -1;
	start.observer = true;
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
	const Player *local = game.players().getLocalPlayer();
	NetGameSession::Options so;
	so.profile = w.profile;
	NetGameSession session(socket, lobby, so);
	if (!session.beginSpectate(game, &error))
	{
		std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
		return 1;
	}
	if (a.frames > 0)
	{
		session.setSpectateStopFrame((UnsignedInt)a.frames);
	}
	std::vector<std::string> frameTimes, observed;
	UnsignedInt nextTime = 100, lastObserve = 0;
	bool forged = false;
	NetGameSession::SpectateState st = NetGameSession::SpectateState::Binding;
	const std::uint64_t t0 = NetMilliseconds();
	while (st != NetGameSession::SpectateState::Ended && st != NetGameSession::SpectateState::Failed)
	{
		st = session.spectateStep(game, 50);
		if (a.spectateForge && !forged && session.transport().relayBound())
		{
			// a forged frame command from the spectator's own address: a FRAMEINFO of slot 0 for the next frame, in a well-formed DATA envelope
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_FRAMEINFO;
			c.slot = 0;
			c.executionFrame = game.frame() + 5;
			const std::vector<std::uint8_t> cmd = NetPacket::encodeCommand(c, nullptr);
			std::vector<std::uint8_t> packet(4, 0);
			packet.insert(packet.end(), { 0x42, 0x46, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0 });
			const std::uint32_t len = (std::uint32_t)cmd.size();
			packet.insert(packet.end(), { (std::uint8_t)len, (std::uint8_t)(len >> 8), (std::uint8_t)(len >> 16), (std::uint8_t)(len >> 24) });
			packet.insert(packet.end(), cmd.begin(), cmd.end());
			const std::uint32_t crc = crc32Bytes(packet.data() + 4, packet.size() - 4);
			for (int i = 0; i < 4; ++i)
			{
				packet[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
			}
			const std::vector<std::uint8_t> d = RelayProtocol::encodeData(1, packet);
			socket.sendTo(lobby.relay, d.data(), d.size());
			forged = true;
		}
		if (a.frameTimes && game.frame() >= nextTime)
		{
			frameTimes.push_back(std::to_string(nextTime) + " " + std::to_string(NetMilliseconds()));
			nextTime += 100;
		}
		if (a.spectateObserveEvery > 0 && game.frame() >= lastObserve + (UnsignedInt)a.spectateObserveEvery)
		{
			lastObserve = game.frame();
			if (game.observeNextPlayer(true))
			{
				const Player *p = game.players().getNthPlayer(game.observedPlayer());
				observed.push_back(std::to_string(game.frame()) + " " + (p ? p->getPlayerName() : std::string("?")));
			}
		}
		NetSleepMilliseconds(2);
	}
	game.drainFrames();
	const NetGameSession::SpectateReport &sr = session.spectateReport();
	std::ostringstream r;
	char b[64];
	r << "spectator " << lobby.spectatorIndex << " delay " << lobby.spectatorDelay << "\n";
	r << "local_player " << (local ? local->getPlayerName() : std::string("none")) << " observer " << (local && local->isObserver() ? 1 : 0) << "\n";
	r << "frames " << game.frame() << "\n";
	std::snprintf(b, sizeof b, "final_hash 0x%08X\n", game.logic().computeStateHash());
	r << b;
	r << "spectate state " << (st == NetGameSession::SpectateState::Ended ? "ended" : "failed") << " reached " << sr.reached << " served " << sr.served
	  << " log_bytes " << sr.logBytes << " log_requests " << sr.logRequests << " crcs_compared " << sr.crcsCompared << " crc_mismatches " << sr.crcMismatches
	  << " log_complete " << (sr.logComplete ? 1 : 0) << " caught_up_ms " << (sr.caughtUpMs ? sr.caughtUpMs - sr.startedMs : 0) << " seconds "
	  << (NetMilliseconds() - t0) / 1000 << "\n";
	r << "spectate_relay bound " << (session.transport().relayBound() ? 1 : 0) << " forged " << (forged ? 1 : 0) << " refused " << session.transport().spectatorRefusals()
	  << " transport_packets_sent " << session.transport().stats().packetsSent << " commands_sent " << session.transport().stats().commandsSent << "\n";
	if (!sr.failure.empty())
	{
		r << "spectate_failure " << sr.failure << "\n";
	}
	for (const std::string &l : frameTimes)
	{
		r << "frame_ms " << l << "\n";
	}
	for (const std::string &l : observed)
	{
		r << "observed " << l << "\n";
	}
	for (const std::string &st2 : Network::onlineStopLines())
	{
		r << "stop " << st2 << "\n";
	}
	writeReport(a.report, r.str());
	return st == NetGameSession::SpectateState::Failed ? (sr.crcMismatches ? 2 : 3) : 0;
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
	if (a.printProfile)
	{
		std::printf("%s\n", w.profile.digest.c_str()); // lane ONLINE-2: what a lobby client sends as its profile
		for (int i = 0; i < w.world->setupPlayerTemplates().getPlayerTemplateCount(); ++i)
		{
			std::printf("faction %d %s\n", i, w.world->setupPlayerTemplates().getNthPlayerTemplate(i)->getName().c_str()); // the START's faction indices
		}
		if (a.printIds)
		{
			for (const ThingTemplate *t : w.world->things().templates())
			{
				std::printf("thing %u %s\n", (unsigned)t->getTemplateID(), t->getName().c_str()); // MSG_QUEUE_UNIT_CREATE / *_CONSTRUCT ids
			}
			for (const UpgradeTemplate *u : w.world->upgrades().templates())
			{
				std::printf("upgrade %d %s\n", u->getMaskBit(), u->getUpgradeName().c_str()); // MSG_QUEUE_UPGRADE's mask bit
			}
			const ScienceStore &sciences = w.world->spellStores().sciences();
			for (const ScienceInfo *info : sciences.sciences())
			{
				std::printf("science %d %s\n", (int)info->m_science, sciences.getInternalNameForScience(info->m_science).c_str()); // MSG_PURCHASE_SCIENCE
			}
			const SpecialPowerStore &powers = w.world->spellStores().specialPowers();
			for (size_t i = 0; i < powers.getNumSpecialPowers(); ++i)
			{
				const SpecialPowerTemplate *p = powers.getSpecialPowerByIndex(i);
				std::printf("power %u %s\n", p->getID(), p->getName().c_str()); // MSG_DO_SPECIAL_POWER*
			}
		}
		return 0;
	}
	if (!a.profileText.empty())
	{
		writeReport(a.profileText, w.profile.text); // lane SAVE-2
		return 0;
	}
	if (!a.replay.empty())
	{
		return runReplay(a, w);
	}
	if (!a.relayLog.empty())
	{
		return runRelayLog(a, w); // lane ONLINE-2
	}
	if (!a.onlineStart.empty() && !a.bindGiven)
	{
		a.bind = "0.0.0.0"; // lane ONLINE-2: the relay is on another host
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
	if (!a.onlineStart.empty())
	{
		// lane ONLINE-2: the lobby service already built the START (GameNetwork/OnlineStart.h); lane ONLINE-3: or a spectator's
		std::string spectateError;
		if (!readOnlineStart(a, w, lobby, error))
		{
			if (!readOnlineStart(a, w, lobby, spectateError, OnlineStart::Kind::Spectate))
			{
				std::fprintf(stderr, "openbfme_peer: %s (as a spectator START: %s)\n", error.c_str(), spectateError.c_str());
				return 3;
			}
			return runSpectator(a, w, socket, lobby);
		}
	}
	else if (a.lanHost)
	{
		NewGameMessage m;
		if (!buildGame(a, w, m, error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		if (!m.game.slots[0].isHuman())
		{
			std::fprintf(stderr, "openbfme_peer: --lan-host requires slot 0 to be human\n");
			return 1;
		}
		int expectedHumans = 0;
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			if (m.game.slots[s].isHuman())
			{
				++expectedHumans;
			}
		}
		if (expectedHumans < 2)
		{
			std::fprintf(stderr, "openbfme_peer: --lan-host needs at least two requested human slots\n");
			return 1;
		}
		const SkirmishGameInfo requested = m.game;
		for (int s = 1; s < MAX_SLOTS; ++s)
		{
			if (m.game.slots[s].isHuman())
			{
				m.game.slots[s].state = SLOT_OPEN;
			}
		}
		LANAPI::Options lo;
		lo.runAhead = a.runAhead;
		lo.crcInterval = a.crcInterval;
		if (!configureLANOptions(a, w, lo, error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		lan = std::make_unique<LANAPI>(w.profile, std::u16string(a.name.begin(), a.name.end()), lo);
		if (!lan->open(&error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		std::printf("LAN_READY role=host lobby_port=%u game_port=%u\n", (unsigned)lan->lobbyPort(), (unsigned)lan->gameSocket().localAddress().port);
		std::fflush(stdout);
		lan->requestGameCreate(m.game);
		std::vector<bool> optionsApplied((size_t)MAX_SLOTS, false);
		bool startRequested = false;
		while (!lan->started())
		{
			const std::uint64_t now = NetMilliseconds();
			lan->update(now);
			for (const LANAPI::Event &e : lan->takeEvents())
			{
				if (e.kind == LANAPI::Event::PlayerJoin)
				{
					const LANGame *current = lan->currentGame();
					const std::string endpoint = current && e.slot >= 0 && e.slot < MAX_SLOTS ? current->lobby[(size_t)e.slot].text() : "unknown";
					std::printf("LAN_PLAYER_JOIN slot=%d endpoint=%s\n", e.slot, endpoint.c_str());
					std::fflush(stdout);
					if (e.slot > 0 && e.slot < MAX_SLOTS && !optionsApplied[(size_t)e.slot])
					{
						if (!lan->hostSetOptions(requested))
						{
							std::fprintf(stderr, "openbfme_peer: LAN hostSetOptions failed for slot %d\n", e.slot);
							return 3;
						}
						optionsApplied[(size_t)e.slot] = true;
						lan->requestAccept(true);
					}
				}
				else if (e.kind == LANAPI::Event::Accept)
				{
					std::printf("LAN_ACCEPT slot=%d accepted=%d\n", e.slot, e.flag ? 1 : 0);
					std::fflush(stdout);
				}
				else if (e.kind == LANAPI::Event::PlayerLeave || e.kind == LANAPI::Event::Kicked)
				{
					std::printf("LAN_PLAYER_LEAVE slot=%d\n", e.slot);
					std::fflush(stdout);
				}
				else if (e.kind == LANAPI::Event::GameCreate && e.ret != LANAPI::RET_OK)
				{
					std::fprintf(stderr, "openbfme_peer: LAN game create failed: %d\n", (int)e.ret);
					return 3;
				}
			}
			const LANGame *current = lan->currentGame();
			if (current && !startRequested)
			{
				int humans = 0;
				bool accepted = true;
				for (int s = 0; s < MAX_SLOTS; ++s)
				{
					if (current->info.slots[s].isHuman())
					{
						++humans;
						if (s > 0 && !current->accepted[(size_t)s])
						{
							accepted = false;
						}
					}
				}
				if (humans == expectedHumans && accepted)
				{
					std::string reason;
					if (!lan->hostStartGame(&reason))
					{
						std::fprintf(stderr, "openbfme_peer: LAN host start failed: %s\n", reason.c_str());
						return 3;
					}
					startRequested = true;
				}
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
			NetSleepMilliseconds(a.lanPumpMs);
		}
		lobby = lan->start();
		lanGameSocket = lan->takeGameSocket();
	}
	else if (a.lanJoin)
	{
		LANAPI::Options lo;
		lo.runAhead = a.runAhead;
		lo.crcInterval = a.crcInterval;
		SkirmishGameSlot ownHero;
		if (a.createAHeroes.size() > 1 || (!a.createAHeroes.empty() && !readCreateAHero(a.createAHeroes.front(), w, ownHero, error)))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", a.createAHeroes.size() > 1 ? "--lan-join takes one --create-a-hero FILE.cah" : error.c_str());
			return 1;
		}
		if (!configureLANOptions(a, w, lo, error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		const int faction = a.lanFaction.empty() ? LANAPI::kKeep : templateIndex(w.world->setupPlayerTemplates(), a.lanFaction);
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
		std::printf("LAN_READY role=guest lobby_port=%u game_port=%u\n", (unsigned)lan->lobbyPort(), (unsigned)lan->gameSocket().localAddress().port);
		std::fflush(stdout);
		std::uint64_t lastTry = 0;
		std::uint64_t joinRequestedAt = 0;
		bool factionAsked = false, joinRequested = false;
		while (!lan->started())
		{
			const std::uint64_t now = NetMilliseconds();
			lan->update(now);
			for (const LANAPI::Event &e : lan->takeEvents())
			{
				if (e.kind == LANAPI::Event::GameList)
				{
					std::printf("LAN_DISCOVERY count=%zu hosts=", lan->games().size());
					for (const LANGame &game : lan->games())
					{
						std::printf("%s,", game.host.text().c_str());
					}
					std::printf("\n");
					std::fflush(stdout);
				}
				if (e.kind == LANAPI::Event::Kicked || e.kind == LANAPI::Event::HostLeave)
				{
					std::fprintf(stderr, "openbfme_peer: LAN lobby: %s\n", e.kind == LANAPI::Event::Kicked ? "kicked" : "the host left");
					return 3;
				}
				if (e.kind == LANAPI::Event::GameJoin && e.ret != LANAPI::RET_OK)
				{
					const std::uint64_t elapsed = joinRequestedAt == 0 ? 0 : now - joinRequestedAt;
					std::fprintf(stderr, "openbfme_peer: LAN_GAME_JOIN result=%d elapsed_ms=%llu\n", (int)e.ret, (unsigned long long)elapsed);
					return 3;
				}
				if (e.kind == LANAPI::Event::GameJoin && e.ret == LANAPI::RET_OK)
				{
					const std::uint64_t elapsed = joinRequestedAt == 0 ? 0 : now - joinRequestedAt;
					std::printf("LAN_GAME_JOIN result=0 elapsed_ms=%llu\n", (unsigned long long)elapsed);
					std::fflush(stdout);
				}
			}
			const LANGame *g = lan->currentGame();
			if (!g && !joinRequested && !lan->games().empty())
			{
				lastTry = now;
				joinRequested = true;
				lan->requestGameJoin(0);
				joinRequestedAt = now;
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
			NetSleepMilliseconds(a.lanPumpMs);
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
	if (lan)
	{
		std::printf("LAN_START role=%s local_slot=%d\n", a.lanHost ? "host" : "guest", lobby.localSlot);
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			if (lobby.networkSlot[(size_t)s])
			{
				std::printf("LAN_START_SLOT slot=%d address=%s\n", s, lobby.addresses[(size_t)s].text().c_str());
			}
		}
		std::fflush(stdout);
	}
	// ---- load ----
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	if (!NewGame::prepareNewGame(lobby.game, w.world->setupPlayerTemplates(), w.settings, w.maps, RandomAlgorithm::ZH_CarryChain, start, &error))
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
	so.earlyOrders = !a.noEarlyOrders; // lane NETLAG-2
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
	if (a.rejoinWindowMs >= 0)
	{
		so.rejoinWindowMs = (std::uint32_t)a.rejoinWindowMs; // lane ONLINE-3
	}
	NetGameSession session(lanGameSocket ? *lanGameSocket : socket, lobby, so);
	const bool rejoin = lobby.online && lobby.rejoinGeneration > 0; // lane ONLINE-3
	if (rejoin)
	{
		if (!session.beginRejoin(game, &error))
		{
			std::fprintf(stderr, "openbfme_peer: %s\n", error.c_str());
			return 1;
		}
		// the catch-up: the relay's log at full speed (no rendering), until the packet router's REJOIN frame is reached
		NetGameSession::RejoinState rs = NetGameSession::RejoinState::Binding;
		while (rs != NetGameSession::RejoinState::Live && rs != NetGameSession::RejoinState::Failed)
		{
			rs = session.rejoinStep(game, 100);
			if (rs == NetGameSession::RejoinState::Binding || rs == NetGameSession::RejoinState::Waiting)
			{
				NetSleepMilliseconds(2);
			}
		}
		const NetGameSession::RejoinReport &rr = session.rejoinReport();
		std::fprintf(stderr, "openbfme_peer: rejoin %s: frame %u after %llu ms (%u frames caught up, %llu CRCs compared)\n",
			rs == NetGameSession::RejoinState::Live ? "live" : "failed", rr.liveFrame, (unsigned long long)(rr.liveMs > rr.startedMs ? rr.liveMs - rr.startedMs : 0), rr.framesCaughtUp,
			rr.crcsCompared);
		if (rs == NetGameSession::RejoinState::Failed)
		{
			std::ostringstream f;
			f << "slot " << lobby.localSlot << "\nrejoin_failure " << rr.failure << "\n";
			writeReport(a.report, f.str());
			std::fprintf(stderr, "openbfme_peer: rejoin failed: %s\n", rr.failure.c_str());
			return 3;
		}
	}
	else
	{
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
	}
	// the logic frame after each frame ran, the state hash then and a fold of the RNG seeds (written on the simulation owner: the worker with
	// --logic-thread; read after the game drained)
	struct FrameHash
	{
		UnsignedInt frame;
		std::uint32_t hash, rng;
		std::uint64_t ms; ///< lane ONLINE-3: NetMilliseconds when it ran (--frame-times)
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
			hashes.push_back({ logic.getFrame(), logic.computeStateHash(), rng, NetMilliseconds() });
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
	UnsignedInt scriptFrame = 0xFFFFFFFFu; // lane NETLAG-2: the frame seen last and when its scripted orders are due (--script-phase)
	std::uint64_t scriptDueMs = 0;
	std::uint32_t phaseRng = 0x9E3779B9u ^ (std::uint32_t)(lobby.localSlot * 7919 + 1); // presentation-side only: never the simulation's random
	std::map<int, unsigned long long> runAheadFrames; // lane NETLAG-2: the run-ahead in effect at each frame this peer ran
	UnsignedInt lastGateFrame = 0xFFFFFFFFu; // lane HUD-5: --toggle-gates
	unsigned long long gateMessages = 0;
	const std::uint64_t runStart = NetMilliseconds();
	std::uint64_t lastProgress = runStart;
	int exitCode = 0;
	// SMOOTH-1: no batch beyond --frames (the worker may run ahead of presentation). lane NETLAG-2: --vanish-at stops the process exactly at its frame
	// (a frame that waited and the next can now run in one advance: LiveGame's owed frame). KNOWN-3: --quit-at and a pending --blackout-at hold the
	// driver at their frame alike, so the event happens at exactly that protocol frame on a loaded machine too (a catch-up advance ran over it: the
	// quit at 251); the hold ends once the event is done (the blackout's is lifted in the same loop pass, before the advance)
	auto holdFrame = [&]() {
		long long stop = std::max(0, a.frames);
		for (long long f : { a.vanishAt, a.quitAt })
		{
			stop = f >= 0 ? std::min(stop, f) : stop;
		}
		for (const auto &bo : a.blackouts)
		{
			stop = bo.first >= 0 ? std::min(stop, bo.first) : stop;
		}
		session.driver().setStopFrame((UnsignedInt)stop);
	};
	holdFrame();
	// lane MP-2: a frame may wait for the disconnect path (the screen after NetworkDisconnectTime, the drop after NetworkPlayerTimeoutTime)
	const std::uint64_t noProgressMs = 30000u + (std::uint64_t)so.network.disconnectTime + (std::uint64_t)so.network.playerTimeoutTime;
	std::string disconnectQuit;
	std::vector<std::string> blackoutLog; // lane MP-3
	std::vector<std::string> pauseLog;    // lane ONLINE-3
	std::uint64_t pausedSince = 0, pausedMs = 0;
	long long pauseStopFrame = -1;
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
		// lane ONLINE-3: the multiplayer pause (--net-pause-at / --net-resume-ms)
		if (a.netPauseAt >= 0 && (long long)game.protocolFrame() >= a.netPauseAt)
		{
			session.network().requestPause();
			pauseLog.push_back("requested at protocol frame " + std::to_string(game.protocolFrame()) + " (pause frame " + std::to_string(session.network().pauseFrame()) + ")");
			a.netPauseAt = -1;
		}
		if (session.network().isPaused())
		{
			const std::uint64_t t = NetMilliseconds();
			if (pausedSince == 0)
			{
				pausedSince = t;
				pauseLog.push_back("paused by slot " + std::to_string(session.network().pausedBy()) + " before frame " + std::to_string(session.network().pauseFrame()));
			}
			if (session.driver().paused() && pauseStopFrame < 0)
			{
				pauseStopFrame = (long long)game.frame(); // the frame every peer stops before (the logic's next frame)
				pauseLog.push_back("stopped with logic frame " + std::to_string(game.frame()));
			}
			if (a.netResumeMs >= 0 && t - pausedSince >= (std::uint64_t)a.netResumeMs)
			{
				pauseLog.push_back("resumed by this peer after " + std::to_string(t - pausedSince) + " ms");
				session.network().requestResume();
				a.netResumeMs = -1;
			}
			lastProgress = t; // a pause is not a stall
		}
		else if (pausedSince != 0)
		{
			pausedMs += NetMilliseconds() - pausedSince;
			pauseLog.push_back("running again after " + std::to_string(NetMilliseconds() - pausedSince) + " ms");
			pausedSince = 0;
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
		if (game.frame() != scriptFrame)
		{
			// every frame run since the last look (a catch-up runs several in one loop: Sol r1)
			runAheadFrames[session.network().currentRunAhead()] += scriptFrame == 0xFFFFFFFFu ? 1 : (unsigned long long)(game.frame() - scriptFrame);
			scriptFrame = game.frame();
			std::uint32_t phase = (std::uint32_t)a.scriptPhaseMs;
			if (a.scriptPhaseMs < 0)
			{
				phaseRng ^= phaseRng << 13, phaseRng ^= phaseRng >> 17, phaseRng ^= phaseRng << 5;
				phase = phaseRng % 200;
			}
			scriptDueMs = NetMilliseconds() + phase;
		}
		if (a.script && game.frame() != lastScripted && NetMilliseconds() >= scriptDueMs)
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
				holdFrame(); // KNOWN-3: the next event's frame (or --frames)
			}
		}
		for (auto &h : a.hitches)
		{
			if (h.first >= 0 && pf >= (UnsignedInt)h.first)
			{
				NetSleepMilliseconds((std::uint32_t)h.second); // lane NETLAG-2: the client stops for a moment (a render hitch): nothing is sent or read
				h.first = -1;
				lastProgress = NetMilliseconds();
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
	// lane ONLINE-1: the bandwidth (docs/ONLINE.md): UDP payload bytes; the wire adds 28 bytes of IPv4 / UDP header per datagram
	r << "transport_bytes sent " << ts.bytesSent << " received " << ts.bytesReceived << " ack_only_packets " << ts.ackOnlyPacketsSent << " commands_sent "
	  << ts.commandsSent << "\n";
	r << "transport_unacked " << session.transport().unackedRecords() << "\n";
	if (rejoin)
	{
		// lane ONLINE-3: the rejoin's catch-up
		const NetGameSession::RejoinReport &rr = session.rejoinReport();
		r << "rejoin generation " << lobby.rejoinGeneration << " live_frame " << rr.liveFrame << " catch_up_ms " << (rr.liveMs - rr.startedMs) << " frames_caught_up "
		  << rr.framesCaughtUp << " log_bytes " << rr.logBytes << " log_requests " << rr.logRequests << " crcs_compared " << rr.crcsCompared
		  << " crc_mismatches " << rr.crcMismatches << " crcs_sent_after " << rr.crcsSentAfter << " frames_filled " << rr.framesFilled << "\n";
		for (const std::string &e : rr.tailErrors)
		{
			r << "rejoin_tail_error " << e << "\n";
		}
	}
	if (lobby.online)
	{
		// lane ONLINE-3: the relay's STATUS seen, the held / handed-over / rejoined slots, the pause
		std::string held, handed, rejoined;
		for (int sl = 0; sl < MAX_SLOTS; ++sl)
		{
			if (session.network().isHeld(sl))
			{
				held += (held.empty() ? "" : ",") + std::to_string(sl);
			}
			if (session.network().isHandedOver(sl))
			{
				handed += (handed.empty() ? "" : ",") + std::to_string(sl);
			}
			if (session.network().rejoinFrame(sl) > 0)
			{
				rejoined += (rejoined.empty() ? "" : ",") + std::to_string(sl) + "@" + std::to_string(session.network().rejoinFrame(sl));
			}
		}
		r << "online_slots held " << (held.empty() ? "none" : held) << " handed_over " << (handed.empty() ? "none" : handed) << " rejoined "
		  << (rejoined.empty() ? "none" : rejoined) << " relay_statuses " << session.transport().relayStatuses() << "\n";
		for (const std::string &l : session.network().rejoinLog())
		{
			r << "rejoin_log " << l << "\n";
		}
	}
	if (a.frameTimes)
	{
		for (const FrameHash &fh : hashes)
		{
			r << "frame_ms " << fh.frame << " " << fh.ms << "\n"; // lane ONLINE-3: every frame's, for a spectator's delay against the players' (same host clock)
		}
	}
	r << "pause pauses_seen " << session.network().pausesSeen() << " stop_frame " << pauseStopFrame << " paused_ms " << pausedMs << "\n";
	for (const std::string &l : pauseLog)
	{
		r << "pause_log " << l << "\n";
	}
	if (lobby.online)
	{
		// lane ONLINE-2: the relay link
		r << "relay " << lobby.relay.text() << " bound " << (session.transport().relayBound() ? 1 : 0) << " refusal " << session.transport().relayRefusal()
		  << " hellos " << ts.relayHellos << " welcomes " << ts.relayWelcomes << " not_bound " << ts.relayNotBound << " slot_mismatches "
		  << ts.relaySlotMismatches << " unknown_senders " << ts.unknownSenders << "\n";
	}
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
		// lane NETLAG-1: each local command's delay in frames (batch - issue protocol frame) as "frames:count" ascending, and issue -> effect in ms
		std::map<std::uint32_t, unsigned long long> delays;
		for (std::uint32_t v : sm.commandDelayFrames)
		{
			++delays[v];
		}
		r << "command_delay_frames";
		for (const auto &kv : delays)
		{
			r << " " << kv.first << ":" << kv.second;
		}
		r << (delays.empty() ? " none" : "") << "\n";
		r << "command_effect_ms " << LockstepDriver::percentiles(sm.commandEffectMs) << "\n";
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
	// lane OWNERFIX-2: each playable player's handicap (Player + 0x29C) and how many of its live objects carry a HandicapPercent<n> list (RW 0x6940DB)
	for (int i = 0; i < game.logic().players().getPlayerCount(); ++i)
	{
		const Player *p = game.logic().players().getNthPlayer(i);
		if (!p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		int listed = 0;
		for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			const AttributeModifierPool *pool = o->getControllingPlayer() == p ? dynamic_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate")) : nullptr;
			if (pool && !o->isEffectivelyDead())
			{
				for (const std::string &n : pool->listNames())
				{
					listed += n.rfind("HandicapPercent", 0) == 0 ? 1 : 0;
				}
			}
		}
		r << "player_handicap " << p->getMultiplayerStartIndex() << " " << p->getHandicap() << " " << listed << "\n";
	}
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
		r << "run_ahead_frames"; // lane NETLAG-2: "run-ahead:frames" over the frames this peer ran
		for (const auto &kv : runAheadFrames)
		{
			r << " " << kv.first << ":" << kv.second;
		}
		r << (runAheadFrames.empty() ? " none" : "") << "\n";
		r << "early_orders " << net.earlyOrdersSent() << " lead_ms " << net.earlyLeadMs() << "\n";
		r << "script_phase " << (a.scriptPhaseMs < 0 ? std::string("random") : std::to_string(a.scriptPhaseMs)) << "\n";
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
	for (const std::string &st : lobby.online ? RelayLog::stopLines() : std::vector<std::string>())
	{
		r << "stop " << st << "\n"; // lane ONLINE-2
	}
	for (const std::string &st : Network::onlineStopLines())
	{
		r << "stop " << st << "\n"; // lane ONLINE-3 (the pause is in every network game)
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
