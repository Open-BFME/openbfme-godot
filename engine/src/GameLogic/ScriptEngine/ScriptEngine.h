// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/ScriptEngine/ScriptEngine.cpp,
// ScriptConditions.cpp, ScriptActions.cpp), as RotWK changes it.
//
// ScriptEngine (lane SCRIPT-1): the LOGIC map script engine. It runs the script lists of every side (the map's own scripts plus the script
// libraries of the side's PlayerAIType and LibraryMaps) once per logic frame, keeps the counters, flags and timers, and executes the
// conditions and actions this port has (ScriptConditions.cpp / ScriptActions.cpp). Its state is logic state: hashed, deterministic, logic RNG
// only where retail draws.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly / Ghidra):
//   * update = ScriptEngine vslot 0x28, RW 0x60CC67, the FIRST of GameLogic's phase-1 subsystem row (RW 0x62E6AF: [0xDE3BAC] vslot 0x28): runs when
//     the game is not a War of the Ring battle (RW 0x5DC46C) or the battle's per-player bit is set; the first update runs RW 0x60A3CB (the named
//     object cache); the close-window (+ 0x1A208) and end-game (+ 0x1A204, MSG_CLEAR_GAME_DATA at 0) timers count down; the fades (+ 0x1A238);
//     then, only while the end-game timer is < 0: TheScriptActions and TheScriptConditions vslot 0x28, every countdown counter (a std::map at
//     + 0x191A0, node + 0x18 value, + 0x1C countdown flag) with value >= 0 decrements, then for i < TheSidesList->getNumSides() (RW 0xDE77A0 + 0x3C):
//     current player = ThePlayerList->getNthPlayer(i) (+ 0x1A230), current side name (+ 0x1A20C) = its name, the side's ScriptList (RW 0x602F20:
//     sides + 0x40 + i * 0x60, + 8): its scripts (RW 0x60A377, list + 0x10) then its groups (RW 0x60BCE5, list + 0xC; a group runs when active
//     (+ 0xC) and not a subroutine (+ 0xD): its scripts (+ 8), then its child groups (+ 4), recursively). A script runs when it is not a subroutine
//     (+ 0x2A). Then ThePlayerList->updateTeamStates (RW 0x6A8541), RW 0x862535, the sequential scripts (RW 0x60C441), the update counter.
//   * executeScript RW 0x60A15C: RW 0x603878 (+ 0x4C = 0; inactive (+ 0x40) -> no; the difficulty of the current player (RW 0x6AA61B; the game's
//     + 0x1A5C4 without one): 0 -> easy (+ 0x2B), 1 -> normal (+ 0x2C), 2 / 3 -> hard (+ 0x2D) must be set; then frame >= + 0x3C (the War of the
//     Ring clock otherwise)); a delay (+ 0x20) > 0 sets + 0x3C = 5 * delay + frame (RW 0xD9F608 = 5, LOGICFRAMES_PER_SECOND); then the
//     sequential (+ 0x10, RW 0x609C3A) or the normal path (RW 0x6099DC): conditions true -> the actions (+ 0x34), one-shot (+ 0x29) -> inactive;
//     false -> the false actions (+ 0x38). A script with a condition team (RW 0x4DD1F4) runs once per member object of that team (not ported, S-1183).
//   * evaluateConditions RW 0x60930F: the OR of the OrConditions (+ 0x30); an OrCondition is true when its AND list is NOT EMPTY and every ENABLED
//     condition (+ 0x4C) of it is true (a disabled one is skipped); no OrCondition, or only empty ones: false (ZH treats an empty AND list as true).
//   * evaluateCondition RW 0x6092A9: the template's mode mask (+ 0x00) against the game mode (RW 0x602FDD: 2 War of the Ring, else 1), else false;
//     0 false, 1 COUNTER (RW 0x608B28), 2 FLAG (RW 0x608F49), 3 true, 4 TIMER_EXPIRED (RW 0x60904D), anything else TheScriptConditions vslot 0x38
//     (RW 0x7EB7CD).
//   * executeActions RW 0x60C1C9: per action with a matching mode mask: RW 0x7B48A5 (not ported), then the engine's own actions (SET_FLAG 1,
//     SET_COUNTER 2, NO_OP 5, SET_TIMER 6, ENABLE_SCRIPT 8, DISABLE_SCRIPT 9, CALL_SUBROUTINE 10, INCREMENT / DECREMENT_COUNTER 15 / 16,
//     SET_MILLISECOND_TIMER 20, SET_RANDOM_TIMER 150, SET_RANDOM_MSEC_TIMER 151, STOP_TIMER 152, RESTART_TIMER 153, ADD_TO / SUB_FROM_MSEC_TIMER
//     154 / 155, SET_RANDOM_COUNTER 373, SET_COUNTER_TO_COUNTER 374, SET_FLAG_TO_FLAG 375, SET_COUNTER_IN_SECONDS 415, SET_RANDOM_COUNTER_IN_SECONDS
//     416, SET_COUNTER_TO_CLIENT_RANDOM_VALUE 508; the tree sway, fades, attack priorities and threat counters 103, 124 .. 127, 132 .. 134, 439, 440
//     are engine-owned and not ported here), everything else TheScriptActions vslot 0x38 (RW 0x7CAFA5, which skips an action whose + 0x41 is 0).
//   * names are side-qualified (RW 0x72C43C): "Side/name" names the side's counter / flag / script, a plain name the current side's. Counters and
//     flags live per (side, name) (RW 0x60817A get-or-create, RW 0x6080F4 find; flags RW 0x6082CF / 0x608249); scripts and groups are found in
//     the named side's list (RW 0x604986, 0x604A5D, 0x6049DD).
//   * COUNTER / COUNTER_COUNTER / COUNTER_SECONDS compare with ops 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 != (anything else false); FLAG is true when the
//     flag equals the value or the flag's name is a UI interaction of this frame (+ 0x1A364); TIMER_EXPIRED: countdown and value < 1.
//   * seconds -> frames (RW 0x60911C, 0x608E7B, 0x6089A7): ceil((double)(0.005f * seconds * 1000.0f)) under the PC24 x87 state, stored as float,
//     FISTP. The random variants draw GetGameLogicRandomValue((int)lo, (int)hi) (RW 0x6D328E; seconds: GetGameLogicRandomValueReal RW 0x6D332C);
//     SET_COUNTER_TO_CLIENT_RANDOM_VALUE draws the CLIENT generator (RW 0x6D32E4: not logic state, stop S-1184).
//   * ENABLE_SCRIPT / DISABLE_SCRIPT (RW 0x604ADD / 0x604BA1) set the group (+ 0xC) AND the script (+ 0x40) of that name; CALL_SUBROUTINE
//     (RW 0x60BFBD): a group of that name that is a subroutine runs its scripts when active; else a script that is a subroutine runs
//     (executeScript); anything else is reported ("Attempting to call script that is not a subroutine", "Script not defined").
//
// INFERENCE / NOT PORTED (stops S-1180 .. S-1189, docs/STOPS.md): which side runs which library in a skirmish (S-1181), the condition-team
// iteration (S-1183), the sequential scripts (S-1185), the conditions / actions without a port here (counted by name: S-1180), the client
// (camera, UI, audio) actions as client requests (S-1182).

#pragma once

#include "Common/GameCommon.h"
#include "Common/INIDataTypes.h"
#include "Common/PlayerScience.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/ScriptEngine/Scripts.h"

#include <array>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class GameLogic;
class Object;
class Player;
class StateHasher;
class Team;
class ThingTemplate;
struct TriggerArea;
struct NamedCamera;

// What the script engine needs from the game beyond GameLogic (the live game implements it; a test may install its own)
class ScriptEngineHost
{
public:
	virtual ~ScriptEngineHost() = default;
	// ZH ScriptActions::doCreateObject: a full object of `tt` on `team` at `pos` (ground height applied) facing `angle`, registered with the
	// pathfinder like every creation site; null when it cannot be made
	virtual Object *createObject(const ThingTemplate &tt, Team &team, const Coord3D &pos, float angle) = 0;
	// lane SCRIPT-3: TheAudio's getAudioLengthMS (RW 0x4541FF) of the event `name`, as a logic-side model: false when the event has no info (TheAudio
	// vslot 0x12C null), else `ms` = the attack, main and decay files' lengths summed (RW 0x454186: a whole ms each, 0 for a file that does not open).
	// Retail picks a multi-file list's file with the audio generator (RW 0x6DB821 / 0x6DA80E); the port's model takes the first so every peer
	// agrees (`picked` true then; stop S-1187). The default host knows no audio
	virtual bool audioLengthMs(const std::string &name, std::int32_t &ms, bool &picked)
	{
		(void)name;
		ms = 0;
		picked = false;
		return false;
	}
};

// One client-facing script action (camera, UI, audio, input ...): executed in the logic frame, applied by the client (stop S-1182). Never read
// back by the logic, not hashed.
struct ScriptClientRequest
{
	UnsignedInt frame = 0;
	std::string action;      ///< the template's internal name ("MOVE_CAMERA_TO", "SHOW_MILITARY_CAPTION", ...)
	int playerIndex = -1;    ///< the current side's player (whose script ran it)
	std::vector<ScriptParameter> params;
	bool hasPosition = false; ///< `position` was resolved from a waypoint / named camera parameter
	Coord3D position;
	std::string script;      ///< the script that ran it
	std::uint32_t objectId = 0; ///< lane SCRIPT-2: the object of the first named-unit parameter (resolved when the logic ran the action; 0 none)
};

class ScriptEngine
{
public:
	explicit ScriptEngine(GameLogic &logic);
	~ScriptEngine();
	ScriptEngine(const ScriptEngine &) = delete;
	ScriptEngine &operator=(const ScriptEngine &) = delete;

	// ---- loading a game ---------------------------------------------------------------------------------------------------------------------
	// One side of TheSidesList: its name (the player's) and its script list (copied: the map's own list merged with its libraries' lists).
	struct SideScripts
	{
		std::string sideName;
		std::vector<const ScriptList *> lists; ///< the side's own list first, then the libraries' (S-1181); must outlive the engine
		std::vector<std::string> libraries;    ///< the library maps merged in, in load order (reports)
	};
	// Builds the runtime tree. `sides` in TheSidesList order (index i is ThePlayerList's player i). `triggers` and `cameras` are the map's
	// (must outlive the engine). `warOfTheRing`: the game mode for the template masks.
	void newGame(const std::vector<SideScripts> &sides, const std::vector<TriggerArea> *triggers, const std::vector<NamedCamera> *cameras,
		bool warOfTheRing);
	void setHost(ScriptEngineHost *host) { m_host = host; }
	// ScriptEngine + 0x1A5C4, the scripts' difficulty (0 easy, 1 normal, 2 hard; the script difficulty filter RW 0x603878, an AIPlayer's difficulty RW 0x8F7FEB).
	// lane CAMP-1H: reset (RW 0x609685) and prepareNewGame (RW 0x77948E -> RW 0x603517(1)) set it to 1 for every game, the campaign's included: the chosen
	// difficulty goes to GameLogic::gameDifficulty (TheGameLogic + 0xA4) instead
	void setGameDifficulty(int d) { m_gameDifficulty = d; }
	int gameDifficulty() const { return m_gameDifficulty; }
	// lane CAMP-1H: ScriptEngine + 0x1A5D5, whether a new object receives the difficulty bonus (Object::initObject RW 0x693D63); reset (RW 0x609685) sets
	// it, OBJECT_ALLOW_BONUSES (RW 0x7BD718) sets every object's flag and this one
	void setObjectsReceiveDifficultyBonus(bool b) { m_objectsReceiveDifficultyBonus = b; }
	bool objectsReceiveDifficultyBonus() const { return m_objectsReceiveDifficultyBonus; }
	bool loaded() const { return m_loaded; }
	void reset();

	// ---- the frame ---------------------------------------------------------------------------------------------------------------------------
	// RW 0x60CC67 (see the header). GameLogic calls it first in phase 1's subsystem row.
	void update();

	// ---- the named object cache (ZH ScriptEngine::addObjectToCache; RotWK + 0x1A220 vector of { name, object }) ---------------------------------
	// Object::setName calls it: a name already in the cache gets the new object, a new name is appended. The cache keeps ids: an object that is
	// gone (its id no longer resolves) is the retail entry whose object pointer was cleared
	void objectNamed(Object &obj);
	Object *getUnitNamed(const std::string &name) const; // RW 0x75A243 (the cache)
	// RW 0x60A1D5: a cache entry for `name` (a new name appended, a known one re-pointed) without renaming the object (SET_REF_TO_... actions)
	void nameInCache(const std::string &name, Object &obj);
	// lane CAMP-1: RW 0x759601, the object has an entry in the named cache
	bool isInNamedCache(const Object &obj) const;
	bool didUnitExist(const std::string &name) const;    // RW 0x758F46: an entry whose object is gone

	// ---- counters / flags / timers (side-qualified names, RW 0x72C43C) ------------------------------------------------------------------------
	struct Counter
	{
		std::int32_t value = 0;
		bool countdown = false;   ///< + 4
		bool milliseconds = false; ///< + 5
	};
	const Counter *findCounter(const std::string &qualifiedName) const; // with the current side as the default
	Counter &counter(const std::string &name);                           // get or create (RW 0x60817A)
	bool *findFlag(const std::string &name);
	bool &flag(const std::string &name);                                  // get or create (RW 0x6082CF)
	// lane CAMP-1: every flag / counter by (side, name), for reports and the mission tests' progress logs
	const std::map<std::pair<std::string, std::string>, bool> &allFlags() const { return m_flags; }
	const std::map<std::pair<std::string, std::string>, Counter> &allCounters() const { return m_counters; }
	// the (side, name) key RW 0x72C43C gives a name with the current side as the default
	std::pair<std::string, std::string> qualify(const std::string &name) const;

	// ---- object type lists (ZH ObjectTypes; RotWK: the engine's list of named lists, RW 0x759158 find, RW 0x759D77 add / remove) ----------------
	// OBJECTLIST_ADDOBJECTTYPE / OBJECTLIST_REMOVEOBJECTTYPE: a missing list is made (and appended) first; a type is added once; a list left empty
	// is removed (RW 0x604067)
	void objectListAdd(const std::string &list, const std::string &type, bool add);
	const std::vector<std::string> *findObjectList(const std::string &list) const;

	// ---- scripts by name -------------------------------------------------------------------------------------------------------------------
	struct RScript;
	struct RGroup;
	RScript *findScript(const std::string &name);
	RGroup *findGroup(const std::string &name);
	bool scriptActive(const std::string &name) const;

	// ---- the current context (valid inside update) -----------------------------------------------------------------------------------------
	Player *currentPlayer() const { return m_currentPlayer; }
	const std::string &currentSideName() const { return m_currentSide; }
	GameLogic &logic() const { return m_logic; }
	ScriptEngineHost *host() const { return m_host; }
	bool warOfTheRing() const { return m_warOfTheRing; }
	const TriggerArea *findTrigger(const std::string &name) const;
	// the map's trigger list (null before newGame): Object::updateTriggerAreaFlags walks it in order (RW 0x69264D, the list at RW 0xDA21FC)
	const std::vector<TriggerArea> *triggerAreas() const { return m_triggers; }
	int triggerIndex(const TriggerArea *t) const;
	// lane HUD-5: PolygonTrigger::addPolygonTrigger at run time (AIGateUpdate::loadTrigger RW 0x8B4BB7 adds "AIGateUpdateTrigger_%d"): the trigger joins the global
	// list after the map's ones, with its callback (the polygon's + 0x54 / + 0x58 pair, called with entered 1 / 0 by RW 0x6E4B21 / 0x6E4B3E from the object's
	// trigger update RW 0x69264D). The index of a run-time trigger in the combined list is the map's count + its own index.
	typedef std::function<void(Object &obj, bool entered)> TriggerCallback;
	int addPolygonTrigger(const TriggerArea &area, TriggerCallback callback);
	size_t triggerAreaCount() const;
	const TriggerArea &triggerAreaAt(size_t i) const;
	void triggerCallback(size_t i, Object &obj, bool entered) const;
	const NamedCamera *findNamedCamera(const std::string &name) const;

	// ---- the acquired sciences (lane AUDIO-4, QA-1 U20) -------------------------------------------------------------------------------------------
	// RW 0x759646 (this, the player index, the science, consume = 1 from PLAYER_ACQUIRED_SCIENCE RW 0x7E4FE0): the player's queue (ScriptEngine +
	// 0x1A3A8 + 0xC * index, filled by RW 0x759A4E, see PlayerScience::ScriptNotice) holds the science: true, the entry erased. Index outside 0 .. 19: false
	bool didPlayerAcquireScience(const Player &p, ScienceType st, bool consume);

	// ---- seconds -> frames (RW 0x60911C) -----------------------------------------------------------------------------------------------------
	static std::int32_t secondsToFrames(float seconds);

	// ---- output and reports ----------------------------------------------------------------------------------------------------------------
	// the client requests of the frames since the last take (in execution order)
	std::vector<ScriptClientRequest> takeClientRequests();
	const std::vector<ScriptClientRequest> &pendingClientRequests() const { return m_clientRequests; }
	void addClientRequest(ScriptClientRequest r);
	// VICTORY / DEFEAT / QUICKVICTORY executed for a side (the client shows them for its local player)
	struct EndRequest
	{
		UnsignedInt frame = 0;
		int playerIndex = -1;
		bool victory = false;
		std::string action;
	};
	const std::vector<EndRequest> &endRequests() const { return m_endRequests; }
	void addEndRequest(bool victory, const std::string &action);

	struct Stats
	{
		unsigned long long updates = 0, scriptsEvaluated = 0, scriptsFired = 0, falseFired = 0, actionsRun = 0, conditionsEvaluated = 0;
		std::map<std::string, unsigned long long> unportedConditions; ///< by name: evaluated false here (S-1180)
		std::map<std::string, unsigned long long> unportedActions;    ///< by name: did nothing here (S-1180)
		std::map<std::string, unsigned long long> clientRequests;     ///< by name (S-1182)
		std::map<std::string, unsigned long long> notes;              ///< retail's own reports ("Script not defined" ...) and the port's stops met
		size_t sides = 0, scripts = 0, groups = 0;
		// lane CAMP-1H: the team scripts run (runScript, by the team field that asked: "OnCreate", "EnemySighted", ...) and the generic hooks that fired
		std::map<std::string, unsigned long long> teamScripts;
		unsigned long long genericScriptsFired = 0;
	};
	const Stats &stats() const { return m_stats; }
	Stats &mutableStats() { return m_stats; }
	void noteUnportedCondition(const std::string &name) { ++m_stats.unportedConditions[name]; }
	void noteUnportedAction(const std::string &name) { ++m_stats.unportedActions[name]; }
	void note(const std::string &text) { ++m_stats.notes[text]; }
	std::vector<std::string> report() const;
	static std::vector<std::string> stopLines();

	void crc(StateHasher &hasher) const;

	// the runtime tree (public for the tests and reports)
	struct RScript
	{
		const Script *def = nullptr;
		std::string sideName;
		bool active = false;                 ///< + 0x40
		UnsignedInt frameToEvaluateAt = 0;   ///< + 0x3C
	};
	struct RGroup
	{
		std::string name;
		bool active = false;     ///< + 0xC
		bool subroutine = false; ///< + 0xD
		std::vector<std::unique_ptr<RScript>> scripts;
		std::vector<std::unique_ptr<RGroup>> groups;
	};
	// lane SCRIPT-2: a sequential script of a team or a unit (RW record 0x2C bytes, ctor RW 0x604121): + 4 the team, + 8 the object id, + 0xC the side
	// whose names its actions use, + 0x10 the script's name, + 0x14 the script, + 0x18 the action index (-1 before the first), + 0x1C the loops left
	// (-1 forever), + 0x20 frames to wait (-1), + 0x24 the action asked to be run again, + 0x28 the next record of the same team / unit
	struct SequentialScript
	{
		std::uint32_t team = 0; ///< TeamID, 0 none
		ObjectID object = INVALID_ID;
		std::string side;
		std::string scriptName;
		const Script *script = nullptr;
		std::int32_t index = -1;
		std::int32_t loopCount = 0;
		std::int32_t framesToWait = -1;
		bool again = false;
	};
	// the engine's list (+ 0x10 .. + 0x14): one chain per team / unit, in the order they were first given a script
	const std::vector<std::vector<SequentialScript>> &sequentialScripts() const { return m_sequential; }
	// RW 0x606FDA: appended to the chain of the same unit (object id) or team, else a new chain; the copy starts at index -1
	void appendSequentialScript(const SequentialScript &r);
	// RW 0x604C9F / 0x604CD9 (UNIT_ / TEAM_STOP_SEQUENTIAL_SCRIPT)
	void removeSequentialScriptsOf(ObjectID object, std::uint32_t team);

	// lane SCRIPT-3: HAS_FINISHED_AUDIO's list (RW ScriptEngine + 0x1A35C, a list of { name key, end frame }, appended at the end, RW 0x759903)
	struct AudioTimer
	{
		std::string name;
		UnsignedInt endFrame = 0;
	};
	const std::vector<AudioTimer> &audioTimers() const { return m_audioTimers; }
	// RW 0x759B84(name, consume): the first query starts the timer (the event's length in logic frames from now); true once the frame reached it,
	// and the entry removed when `consume`. An event without info is finished at once (no entry)
	bool hasFinishedAudio(const std::string &name, bool consume);
	// lane SCRIPT-3: RW + 0x1A218, the team "<This Team>" names (RW 0x759FDA) while a sequential script's action runs (RW 0x60C674); 0 none
	std::uint32_t conditionTeam() const { return m_conditionTeam; }
	// lane CAMP-1H: RW + 0x1A210, the team of a team script ("<This Team>" asks it first, RW 0x759FDA): set by runScript (RW 0x60BD42) around the script,
	// by the generic scripts around their conditions (RW 0x60930F) and actions (RW 0x60D053); a script's own conditions see none (RW 0x60930F with no team)
	Team *thisTeam() const { return m_thisTeam; }
	// lane CAMP-1H r2: runs inside a scope with + 0x1A210 = team (restored after); runScript and the generic scripts do the same inline (tests use it)
	struct ThisTeamScope
	{
		ScriptEngine &engine;
		Team *saved;
		ThisTeamScope(ScriptEngine &e, Team *t) : engine(e), saved(e.m_thisTeam) { e.m_thisTeam = t; }
		~ThisTeamScope() { engine.m_thisTeam = saved; }
		ThisTeamScope(const ThisTeamScope &) = delete;
		ThisTeamScope &operator=(const ThisTeamScope &) = delete;
	};
	// lane CAMP-1H: RW 0x60BD42 ScriptEngine::runScript(side, name, team): "" / "<none>" nothing; `side` current (+ 0x1A20C); the team context (+ 0x1A210 = team, + 0x1A218 = 0, the current
	// player (+ 0x1A230) = the team's controlling player, RW 0x79FD6F) around the call of a subroutine group (its scripts, RW 0x60A377) or subroutine script
	// (RW 0x60A15C) of that name, the names' side current (RW 0x604243); all restored after. "***Script not defined: ***" / "***Attempting to call script
	// that is not a subroutine***" are noted
	void runScript(const std::string &side, const std::string &name, Team *team);
	// lane CAMP-1H: ThePlayerList->updateTeamStates (RW 0x6A8541, after the side scripts of ScriptEngine::update): the 20 player slots in index order, each
	// player's team prototypes (Player + 0x34C) and their instances from the list head (the newest), Team::updateState (RW 0x7A208C)
	void updateTeamStates();
	void updateTeamState(Team &t);
	// lane CAMP-1H: Player::update's team pass (RW 0x6AF269 -> RW 0x7A267D Team::updateGenericScripts) for one player
	void updateGenericScripts(Player &p);
	void updateGenericScripts(Team &t);

	struct RSide
	{
		std::string name;
		RGroup root; ///< the list: root.scripts (+ 0x10) and root.groups (+ 0xC); root.active / subroutine unused
	};
	const std::vector<RSide> &sides() const { return m_sides; }

	// RW 0x6092A9 (the mode mask, then the engine's own or TheScriptConditions' case) and one action of RW 0x60C1C9 (the sequential runner, the coverage
	// test): public for the tests
	bool evaluateCondition(const ScriptCondition &c);
	void executeAction(const ScriptActionRec &a);

private:
	void buildGroup(const std::vector<ScriptItem> &items, const std::string &side, RGroup &out);
	void executeScripts(std::vector<std::unique_ptr<RScript>> &scripts);
	void executeGroups(std::vector<std::unique_ptr<RGroup>> &groups);
	void executeScript(RScript &s);
	bool shouldEvaluate(RScript &s) const;
	bool evaluateConditions(const Script &s);
	bool evaluateConditions(const Script &s, Team *team); // lane CAMP-1H: RW 0x60930F with a team (+ 0x1A210, the team's player current)
	bool evaluateConditionsNoContext(const Script &s);    // the OR of ANDs itself
	bool teamReady(const Team &t) const;                  // lane CAMP-1H: RW 0x7A09E4
	void handTeamToDefault(Team &t);                      // lane CAMP-1H: RW 0x7A12F4
	void executeActions(const std::vector<ScriptActionRec> &actions, RScript &s);
	void updateSequentialScripts(); // RW 0x60C441
	bool startSequentialScript(RScript &s); // RW 0x609C3A's record
	bool executeEngineAction(const ScriptActionRec &a);
	RSide *findSide(const std::string &name);
	RScript *findScriptIn(RGroup &g, const std::string &name);
	RGroup *findGroupIn(RGroup &g, const std::string &name);
	int gameMode() const { return m_warOfTheRing ? 2 : 1; }

	// the engine's counter / flag / timer conditions and actions (RW 0x608B28 ...)
	bool evaluateCounter(const ScriptCondition &c);
	bool evaluateFlag(const ScriptCondition &c);
	bool evaluateTimer(const ScriptCondition &c);
	void setTimer(const ScriptActionRec &a, bool milliseconds, bool random);
	void setCounter(const ScriptActionRec &a, int randomMode, bool fromCounter, bool seconds);
	void adjustMsecTimer(const ScriptActionRec &a, bool milliseconds, bool add);
	void enableScript(const ScriptActionRec &a, bool enable);
	void callSubroutine(const ScriptActionRec &a);

	GameLogic &m_logic;
	ScriptEngineHost *m_host = nullptr;
	bool m_loaded = false;
	bool m_firstUpdate = true;
	bool m_warOfTheRing = false;
	int m_gameDifficulty = 1;
	bool m_objectsReceiveDifficultyBonus = true; ///< lane CAMP-1H: + 0x1A5D5 (hashed)
	std::vector<RSide> m_sides;
	std::map<std::pair<std::string, std::string>, Counter> m_counters;
	std::map<std::pair<std::string, std::string>, bool> m_flags;
	std::vector<std::pair<std::string, ObjectID>> m_namedObjects; ///< insertion order
	std::vector<std::pair<std::string, std::vector<std::string>>> m_objectLists; ///< creation order
	const std::vector<TriggerArea> *m_triggers = nullptr;
	struct RuntimeTrigger
	{
		std::shared_ptr<TriggerArea> area; ///< shared: the type is incomplete here (the deleter is made in ScriptEngine.cpp)
		TriggerCallback callback;
	};
	std::vector<RuntimeTrigger> m_runtimeTriggers; ///< lane HUD-5 (see addPolygonTrigger)
	const std::vector<NamedCamera> *m_cameras = nullptr;
	Player *m_currentPlayer = nullptr;
	std::string m_currentSide;
	const RScript *m_currentScript = nullptr;
	int m_callDepth = 0;
	std::vector<ScriptClientRequest> m_clientRequests;
	std::vector<EndRequest> m_endRequests;
	bool m_updatingSequential = false; ///< updateSequentialScripts is running: removals empty slots instead of erasing them (not state)
	std::vector<std::vector<SequentialScript>> m_sequential; ///< lane SCRIPT-2: RW + 0x10 (a chain per slot; an empty chain is retail's null slot)
	std::array<AcquiredScienceQueue, 20> m_acquiredSciences; ///< lane AUDIO-4: + 0x1A3A8, one per player index (RW 0x759646 bound 0x14)
	std::vector<AudioTimer> m_audioTimers;                  ///< lane SCRIPT-3: RW + 0x1A35C
	std::uint32_t m_conditionTeam = 0; ///< lane SCRIPT-3: RW + 0x1A218 while a sequential action runs (the record's team): "<This Team>" (not state)
	Team *m_thisTeam = nullptr;        ///< lane CAMP-1H: RW + 0x1A210 while a team script runs (not state: set and restored inside one call)
	// lane CAMP-1H: the generic scripts of a prototype (RW 0x7A1759: looked up once by name in the prototype owner's side and DUPLICATED, RW 0x7B7F2B: the copy
	// keeps its own active flag + 0x40); keyed by the prototype id, [i] null when the hook names no script
	struct GenericCopy
	{
		const Script *def = nullptr;
		std::string side;
		bool active = false;
	};
	std::map<int, std::vector<GenericCopy>> m_genericScripts;
	Stats m_stats;
	friend class ScriptConditions;
	friend class ScriptActions;
};
