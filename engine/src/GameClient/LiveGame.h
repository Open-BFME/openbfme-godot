// OpenBFME. GPL-3.0.
//
// LiveGame: one map running as live objects (lane LOGIC-1), everything between the retail files and the device layer, with no Godot:
//   RetailObjectWorld (templates, map.ini overrides)  ->  map + terrain  ->  MapObjectCreation classification (MAPOBJ-1)
//   ->  PlayerList::newGame (+ lobby slots)  ->  GameLogic (explicit RNG, settings read from GameData / AIData, terrain)
//   ->  MapObjectLoop (live objects; hordes create their members)  ->  DrawableManager (the clients' drawables)
// and a clock that ticks the logic at retail's rate (LogicFrameClock: 5 logic frames per second, render at any rate) and drives the render side.
//
// Pacing (stop S-151): a logic frame runs as one step of its six phases (GameLogic::runLogicFrame) when the clock says it is due; retail
// spreads the phases over six client ticks. At most `maxFramesPerAdvance` frames run per advance(): a stall drops the rest of the time (counted),
// it never makes the logic run ahead of the clock.
//
// The static client-only layer (trees, shrubs, props: not objects, MapObjectCreation fates CLIENT_*) is exposed as `clientOnly()` for the device
// layer to draw once.

#pragma once

#include "Common/GameCommon.h"
#include "Common/LogicFrameClock.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGameFrameDriver.h"
#include "GameClient/LiveScripting.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/BuildCommands.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/VisionSettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/NewGame/StartingBase.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/MapObjectLoop.h"
#include "GameLogic/PlayerCommands.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <set>
#include <string>
#include <vector>

class ArchiveFileSystem;
class ArchiveW3DFileSource;
class WW3DAssetManager;


class LiveGame
{
public:
	struct Options
	{
		std::string mapName;               ///< "map mp evendim": the map's directory / file stem
		SkirmishSetup slots;               ///< lobby slots taken over by the map's players (empty: the map's own players)
		std::uint32_t seed = 1;            ///< the logic RNG seed
		std::uint32_t defaultStartingCash = 0;    ///< a faction without StartMoney starts with this; 0 = the game's (GameData DefaultStartingCash)
		int maxFramesPerAdvance = 10;
		// false (default): a logic frame runs as one step of its six phases when the 5 Hz clock says it is due. true: retail's pacing, one phase per
		// engine tick of a 30 Hz tick clock (GameLogic::update(phase), phases 1..6 in turn): the logic state is identical at every frame boundary, the
		// phases of a frame just happen on six different render times (stop S-151).
		bool sixTickPacing = false;
		// lane START-1: start a skirmish from a resolved new-game message (GameLogic/NewGame/NewGame.h). When set, `mapName`, `slots` and `seed` are not used: the
		// map is start->mapName, the sides are built from start->message (retail's prepareForMP_or_Skirmish + addSidesForSlots), the logic RNG is start->random (the
		// stream the resolution already advanced), and every occupied slot's starting base is placed after the map's objects.  `start` must outlive load().
		const NewGameStart *start = nullptr;
		// load progress (RotWK's updateLoadProgress): called with a percentage 0..100 that never decreases, at the milestones listed in LiveGame::progressMilestones()
		std::function<void(int percent)> progress;
		// diagnostic: record every logic RNG draw (GameLogicRandom::callLog) from the first object on; tests count the creation draws with it
		bool logRandomCalls = false;
		// lane SMOOTH-1 (S-810): run the logic frames on a worker thread, the one owner of the simulation while it runs a frame. The render side draws
		// from published snapshots and applies the ordered client events; every other access to the logic waits for the worker (waitIdle). false:
		// the frames run on the caller's thread inside advance() (the single-thread fallback; the results are identical, pinned by test).
		bool logicThread = false;
		// SMOOTH-1: the render side presents the logic's time this much behind the clock (threaded: room for the worker to finish a frame before it
		// is shown; when a frame is later than this, the presented time stops at the end of the last completed frame, never a partial frame, and SMOOTH-2
		// lengthens the delay a little for each such render frame, S-1140)
		double presentationDelaySeconds = 0.0;
		// SMOOTH-2 diagnostic (tests): the worker sleeps this long before each frame it runs (a slow simulation; publication comes that much later)
		int workerFrameDelayMs = 0;
		// SMOOTH-1: hash the state after every frame and keep it in the frame's snapshot (frameHashes(); false: no per-frame hash)
		bool hashEveryFrame = false;
		// lane SCRIPT-1: the map script engine. -1 (default): the scripts run in a started game (`start`) and a campaign mission (`campaign`), not in a bare
		// map load (viewers, object tests); 0 never; 1 always. A campaign mission is a single player game (GameLogic + 0x110 = 0) with the map's own sides.
		int mapScripts = -1;
		bool campaign = false;
	};

	// `world`, `fs`, `assets` and `mapOptions` must outlive the game; `mapOptions` is MapObjectGameData / playerTemplates / creationScripts as the
	// object loop wants it (MapObjectGameData::load etc. already ran).
	LiveGame(RetailObjectWorld &world, ArchiveFileSystem &fs, WW3DAssetManager &assets, const MapObjectOptions &mapOptions);
	~LiveGame();
	LiveGame(const LiveGame &) = delete;
	LiveGame &operator=(const LiveGame &) = delete;

	// Loads the map and creates every live object. false + *error when the map cannot be read; problems inside the map are in report().
	bool load(const Options &options, std::string *error);

	// SMOOTH-1: every accessor of simulation state waits until the logic worker is idle (waitIdle): the caller sees a completed frame, never one
	// being computed. The drawables belong to the render side and need no wait.
	GameLogic &logic() { waitIdle(); return *m_logic; }
	DrawableManager &drawables() { return *m_drawables; }
	// RENDER-2: the launch bone provider the logic's projectile launches ask (CombatState::setLaunchOffsets)
	DrawableLaunchBones &launchBones() { waitIdle(); return *m_launchBones; }
	const LoadedMap &map() const { return m_map; }
	const MapObjectDrawables &classified() const { return m_classified; }
	// the trees, shrubs and props of the map (not objects)
	const MapObjectDrawables &clientOnly() const { return m_clientOnly; }
	PlayerList &players() { waitIdle(); return *m_players; }
	// lane PROD-1: the lockstep command path. Messages appended to commands() are executed by the logic's `commandList` phase row in the next frame
	CommandList &commands() { waitIdle(); return m_commands; }
	GameLogicDispatch &dispatch() { waitIdle(); return *m_dispatch; }
	// makes an object of `templateName` for player `playerIndex` at `pos` (a map-less start: the starting buildings and units a skirmish creates); null +
	// *error when the template or the player does not exist
	Object *createObject(const std::string &templateName, int playerIndex, const Coord3D &pos, float angle, std::string *error);
	// MOVE-1: TheAI of this game (the pathfinder of the map, the AI movers)
	AIWorld &ai() { waitIdle(); return *m_ai; }
	// VIS-1: TheShroudManager (attached to the logic) and the GameData / MultiplayerSettings values it was made from
	ShroudManager &shroud() { waitIdle(); return *m_shroud; }
	const VisionSettings &visionSettings() const { return m_vision; }
	// the local player's cell edges since the last call (the client's fog texture rebuilds when this is true)
	bool takeShroudChanged()
	{
		return m_shroudChanged.exchange(false);
	}
	// MOVE-1: the movement lane's executors of the move / stop messages, registered on dispatch() (the one dispatch path)
	AICommands &aiCommands() { waitIdle(); return m_aiCommands; }
	BuildCommands &buildCommands() { waitIdle(); return m_buildCommands; }
	SpellCommands &spellCommands() { waitIdle(); return m_spellCommands; } // SPELL-1

	// Feeds wall-clock seconds in: runs the logic frames that are due (each as six phases), steps the drawables' animations by the render time and
	// refreshes their interpolated poses. Returns the logic frames run.
	// the milestones of Options::progress with what runs before each, for reports and tests
	static std::vector<std::pair<int, std::string>> progressMilestones();
	int advance(double seconds);
	// lane MP-1: installs (or with null removes) the frame driver; the driver must outlive its installation. SMOOTH-1 (review r4): called by the protocol
	// owner (the thread that calls advance); it first drains the previous driver (every batch already queued runs, its completions are consumed by that
	// driver: complete batches only, so this never waits for the network), then takes the new driver's capture spec and starts its cursors at the logic's
	// current frame (the loaded game's first frame has already run). The worker never calls the driver's protocol half.
	void setFrameDriver(LiveGameFrameDriver *driver);
	// SMOOTH-1 (protocol owner): runs every queued batch and hands its completion to the driver (shutdown, leave, a replay's end: the protocol and the
	// logic agree on the frame afterwards)
	void drainFrames();
	// SMOOTH-1 (protocol owner): the logic frame of the last completion the driver consumed (what local input is stamped against), and the next batch frame
	UnsignedInt protocolFrame() const { return m_protocolFrame; }
	UnsignedInt nextBatchFrame() const { return m_nextBatchFrame; }
	// frames that were due but could not run because the driver held them (the lockstep waited); their time is not caught up (ZH Network::timeForNewFrame
	// resets its frame time when it falls more than two frames behind). Only the clock's time is dropped: an authoritative batch is never skipped
	unsigned long long stalledFrames() const { return m_stalledFrames; }
	// SMOOTH-1: batches the driver handed out, and completions it consumed (each exactly once, in frame order)
	unsigned long long batchesAcquired() const { return m_batchesAcquired; }
	unsigned long long completionsConsumed() const { return m_completionsConsumed; }
	// at most this many complete batches wait for the worker (backpressure: the next one stays with the driver)
	static constexpr size_t kMaxQueuedBatches = 16;
	// the fraction of the logic frame elapsed, in [0, 1): the 5 Hz clock's, or in six tick pacing the phases done plus the tick's fraction over six
	double alpha() const;
	// lane SMOOTH-1: the drawables are posed between two logic frames by alpha (default), or, off, at the current logic transform: the stepped 5 Hz
	// look of a client without interpolation (the before / after comparison of the benchmark and the video). Presentation only, never the logic.
	void setRenderInterpolation(bool enabled) { m_renderInterpolation = enabled; }
	bool renderInterpolation() const { return m_renderInterpolation; }
	// the latest completed (published) logic frame
	UnsignedInt frame() const;

	// ---- lane SMOOTH-1 (S-810): the simulation owner ----
	// Switches the logic worker on or off (Options::logicThread); waits for the frames already requested. Only between frames (never on the worker).
	void setLogicThread(bool enabled);
	bool logicThread() const { return m_threaded; }
	// the logic frame the clock has made due (the last one requested of the worker, or run): inputs keyed to logic frames compare against it
	UnsignedInt dueFrame() const { return m_dueFrame; }
	// Blocks until the worker has run every requested frame (no-op single-threaded or on the worker itself); rethrows a worker failure.
	void waitIdle() const;
	// true when no frame is requested or running: logic state may be read now without waiting
	bool logicIdle() const;
	// the snapshot the render side draws (the presented frame) and the latest published one
	std::shared_ptr<const LogicSnapshot> presentedSnapshot() const { return m_presented; }
	std::shared_ptr<const LogicSnapshot> latestSnapshot() const;
	// SMOOTH-1: where an object is in the presented snapshot, for the render side's per-frame readers (the camera's lock / follow target): 1 found,
	// 0 gone (ids only grow: an id below the snapshot's nextObjectId allocation watermark that it does not hold is gone), -1 newer than the snapshot (not published yet)
	int presentedObjectPosition(ObjectID id, Coord3D &position) const;
	// the presentation alpha of the last advance() (after the presentation delay and the late-frame clamp)
	double presentedAlpha() const { return m_presentedAlpha; }
	// lane SMOOTH-2: the delay late worker frames added to Options::presentationDelaySeconds (seconds; decays while the worker keeps up)
	double presentationExtraDelaySeconds() const;
	double presentationExtraDelayMs() const;
	// SMOOTH-1 diagnostics: { frame, hash } of every completed frame since load when Options::hashEveryFrame; frames the presentation held because
	// the worker was late; the worker's busy time of the last frame (ms)
	std::vector<std::pair<UnsignedInt, std::uint32_t>> frameHashes() const;
	unsigned long long heldPresentations() const { return m_heldPresentations; }
	// client events published or taken but not applied yet (their frame is not presented yet)
	size_t pendingClientEvents() const;
	// For drivers that run logic frames themselves (tests: logic().runLogicFrame()): publishes the current state and its events, applies every event
	// and poses the drawables from that snapshot at `alpha`, after stepping their animations by `elapsedMs`.
	void refreshClient(double elapsedMs, double alpha);
	double lastWorkerFrameMs() const
	{
		std::lock_guard<std::mutex> lock(m_pubMutex); // the worker writes it under the same mutex (review r2)
		return m_lastWorkerFrameMs;
	}
	unsigned long long droppedFrames() const { return m_droppedFrames; }

	// lane SCRIPT-1: the script engine's client requests (camera, UI, audio ...) published with the frames, in execution order (render side; taken once)
	std::vector<ScriptClientRequest> takeScriptRequests();
	bool mapScriptsRunning() const { return m_mapScripts; }

	struct Report
	{
		std::string map;
		size_t mapObjects = 0;                  ///< ObjectsList entries
		size_t classifiedFull = 0, clientOnlyObjects = 0;
		MapObjectLoopResult loop;
		std::vector<std::string> mapIniErrors, playerNotes, classificationErrors, errors, stops;
		std::vector<std::string> playerSummary;
		// lane START-1 (empty unless Options::start was set)
		std::vector<std::string> startSlotPlayers;           ///< per slot: the player name ("" for an unoccupied slot)
		std::vector<StartingBase::Placed> startingObjects;   ///< the starting structures and units, in placement order
		int createAHeroes = 0;                               ///< lane HERO-2: the slots whose Create-a-Hero the game start installed
		size_t spellBooks = 0;                               ///< SPELL-1: the players' spell book objects made at the game start (RW 0x6B183D)
		std::vector<std::string> startErrors;                ///< problems of the start (a slot without waypoint, a template that does not exist ...)
		std::vector<int> progressCalls;                      ///< the percentages reported
		GameLogic::Report logic;
		DrawableManager::Report drawables;
		LiveScripting::Stats scripting;        ///< the script engine: creations (one creation draw each), OnCreated handlers that ran, its reports
		std::vector<std::string> mapScripts;   ///< lane SCRIPT-1: the sides' script lists and libraries, the setup's notes
		double secondsMap = 0.0, secondsLoop = 0.0, secondsPathfinder = 0.0;
		std::vector<std::string> movement;     ///< MOVE-1: the AI world's movement-data errors
		std::vector<std::string> movementStops; ///< MOVE-1: the stops the movers raised
		std::vector<std::string> launchBoneProblems; ///< RENDER-2: the launch-bone provider's data problems (missing models / bones / pose animations); counted in its S-460 stop
	};
	// the load-time part is fixed; logic and drawables are read when this is called
	Report report() const;

private:
	RetailObjectWorld &m_world;
	ArchiveFileSystem &m_fs;
	WW3DAssetManager &m_assets;
	const MapObjectOptions &m_mapOptions;
	LoadedMap m_map;
	TerrainLogic m_terrain;
	MapObjectDrawables m_classified, m_clientOnly;
	TeamFactory m_teams;
	std::unique_ptr<PlayerList> m_players;
	std::unique_ptr<WW3DDrawAssets> m_launchAssets;      // RENDER-2: the launch bone provider's view of the assets
	std::unique_ptr<DrawableLaunchBones> m_launchBones; // RENDER-2: the drawables' launch bones (RW 0x6756A1) for the logic's projectile launches; before the logic
	std::unique_ptr<DrawableManager> m_drawables; // before the logic: the logic deletes its objects (and calls the manager) in its destructor first
	std::unique_ptr<LiveScripting> m_scripting;   // likewise: the objects' destructors call the world hooks
	std::unique_ptr<class MapScriptSetup> m_scriptSetup; // lane SCRIPT-1: the libraries the script engine's lists point into (outlive the logic)
	std::unique_ptr<ScriptEngineHost> m_scriptHost;      // lane SCRIPT-1
	bool m_mapScripts = false;
	BuildCommands m_buildCommands;                // BUILD-1: the construction messages
	AICommands m_aiCommands;                      // MOVE-1: before the logic; its handlers are registered on m_dispatch
	SpellCommands m_spellCommands;                // SPELL-1: the spell book messages
	PlayerCommands m_playerCommands;              // HUD-1: likewise (stateless)
	std::unique_ptr<TerrainPathfindSource> m_pathTerrain; // the pathfinder's view of the terrain (MOVE-1); outlives the AI world and the logic
	std::unique_ptr<AIWorld> m_ai;                // likewise (MOVE-1): the objects' destructors leave the pathfinder through its hooks
	std::unique_ptr<GameLogic> m_logic;
	std::unique_ptr<ShroudManager> m_shroud;       ///< VIS-1: after the logic (destroyed first: it detaches its hooks)
	VisionSettings m_vision;
	std::atomic<bool> m_shroudChanged{ true }; ///< set by the shroud callback on the simulation owner (the worker); the client reads the snapshot's ShroudView
	CommandList m_commands;                          ///< lane PROD-1
	std::unique_ptr<GameLogicDispatch> m_dispatch;   ///< after the logic: it is destroyed first
	LogicFrameClock m_clock{ LOGICFRAMES_PER_SECOND };
	LogicFrameClock m_tickClock{ LOGICFRAMES_PER_SECOND * 6 }; ///< the engine tick clock of six tick pacing
	int m_nextPhase = 1;                                       ///< six tick pacing: the phase the next tick runs
	Options m_options;
	Report m_report;
	std::vector<std::string> m_economyStops; ///< the terrain source's stop lines of the economy's blocked-cell map (lane ECON-1)
	bool m_renderInterpolation = true;
	unsigned long long m_droppedFrames = 0;
	LiveGameFrameDriver *m_frameDriver = nullptr; ///< lane MP-1 (protocol owner; the worker sees it only through a queued WorkItem)
	unsigned long long m_stalledFrames = 0;
	bool m_loaded = false;
	// ---- SMOOTH-1 + MP-1 (review r4): numbered batches and completion records ----
	struct WorkItem
	{
		std::shared_ptr<const FrameBatch> batch;  ///< null: a frame of the clock without a driver (commands() goes to it)
		LiveGameFrameDriver *driver = nullptr;    ///< for simulationAfterFrame (setFrameDriver drains before the driver changes)
		LiveGameFrameDriver::Capture capture;
	};
	LiveGameFrameDriver::Capture m_capture;       ///< protocol owner: the installed driver's
	UnsignedInt m_nextBatchFrame = 0;             ///< protocol owner: the next frame acquire() is asked for
	UnsignedInt m_protocolFrame = 0;              ///< protocol owner: the logic frame of the last completion consumed
	unsigned long long m_batchesAcquired = 0, m_completionsConsumed = 0;
	int advanceWithDriver(int due);               ///< protocol owner: completions, pump, acquire up to `due` batches (queued or run inline)
	void consumeCompletions();                    ///< protocol owner: the published completions to the driver, in order
	void installBatch(const FrameBatch &batch);   ///< simulation owner: the batch's commands into the command list before phase 1
	std::shared_ptr<const FrameCompletion> captureCompletion(const WorkItem &item); ///< simulation owner, after the frame

	// lane SCRIPT-1: the sides' lists and libraries into the logic's ScriptEngine (before the map's objects exist)
	bool setupMapScripts(const Options &options, const SidesList &sidesUsed, const std::set<std::string> &slotSides, std::string *error);
	// ---- lane SMOOTH-1: the simulation owner, the publication and the presentation ----
	void runFrameOwned(const WorkItem &item); ///< one logic frame, its hash, its snapshot and events published (on the owner's thread)
	void publish(std::shared_ptr<const FrameCompletion> completion = nullptr); ///< the completed state as a snapshot plus the events since the last one
	void present(double seconds);         ///< the render side: events, animations, poses from the presented snapshot
	void workerLoop();
	void stopWorker();
	std::unique_ptr<ArchiveW3DFileSource> m_simAssetSource; ///< SMOOTH-1: the simulation's own asset manager (launch bones), apart from the drawables'
	std::unique_ptr<WW3DAssetManager> m_simAssets;
	std::unique_ptr<ClientEventRecorder> m_recorder;       ///< the client hooks of the logic (owner thread)
	std::shared_ptr<const LogicSnapshot> m_lastBuilt;      ///< owner thread: the previous snapshot (lastMovedFrame history)
	mutable std::mutex m_pubMutex;
	mutable std::condition_variable m_pubCv;
	std::shared_ptr<const LogicSnapshot> m_published, m_publishedPrev; ///< guarded by m_pubMutex
	std::vector<ClientEvent> m_publishedEvents;                         ///< guarded: published, not yet taken by the render side
	std::vector<ScriptClientRequest> m_publishedScriptRequests;         ///< guarded: lane SCRIPT-1, published, not yet taken
	std::vector<std::pair<UnsignedInt, std::uint32_t>> m_frameHashes;   ///< guarded
	std::deque<WorkItem> m_work;          ///< guarded: frames requested from the worker (numbered batches with a driver), not started
	std::deque<std::shared_ptr<const FrameCompletion>> m_completions; ///< guarded: published by the simulation owner, not consumed yet
	bool m_running = false;               ///< guarded: the worker is inside a frame
	bool m_stop = false;                  ///< guarded
	std::exception_ptr m_workerError;     ///< guarded
	double m_lastWorkerFrameMs = 0.0;     ///< guarded
	std::thread m_worker;
	std::atomic<std::thread::id> m_workerId{};
	bool m_threaded = false;
	UnsignedInt m_dueFrame = 0;           ///< main thread: the logic frame the clock has made due (the last requested)
	std::shared_ptr<const LogicSnapshot> m_presented, m_presentedPrev; ///< main thread
	std::vector<ClientEvent> m_heldEvents; ///< main thread: taken events of frames not presented yet
	double m_presentedAlpha = 0.0;
	unsigned long long m_heldPresentations = 0;
	// lane SMOOTH-2 (LiveGameRunner.cpp present): the continuous presented time (logic frames: the presented snapshot's frame + its alpha) and the delay a late
	// worker added to Options::presentationDelaySeconds (frames). Main thread.
	double m_presentTime = 0.0;
	bool m_presentTimeValid = false;
	double m_extraDelayFrames = 0.0;
	static constexpr double kDelayGrowFrames = 0.02;      ///< 4 ms more delay per late render frame
	static constexpr double kMaxExtraDelayFrames = 0.35;  ///< at most 70 ms more (with the 30 ms default: 100 ms)
	static constexpr double kDelayDecayPerFrame = 0.01;   ///< 10 ms less per second the worker keeps up
};
