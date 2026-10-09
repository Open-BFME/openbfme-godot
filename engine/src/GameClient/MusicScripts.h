// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// MusicScripts (lane AUDIO-2): RotWK's in-game music. Retail has no music playlist in code: AudioSettings MusicScriptLibraryName names a MAP
// ("Libraries\Music_MusicScripts_Single\Music_MusicScripts_Single.map") whose scripts ARE the music system (strings RW 0xC361A0 "/___MusicScript_Init",
// 0xC37BE4 "The music scripting system is on."). The retail map holds 74 scripts in 7 groups: faction detection (SKIRMISH_PLAYER_FACTION of the local
// player), three "level 0" phases (base building -> explore when 70 of the local player's units are 1000 away from the home base or a random 600..900 s
// turtle timer runs out -> explore2 near the end), a per-faction Multisound for each phase (BaseBuildingMannishMusic, ExploreElvenMusic, ...), 0..40 s
// of silence between level 0 tracks, and "level 1" action / under-attack / triumphal tracks pushed on top of level 0 when ENGAGED units or the Eva
// event UnitUnderAttack near our units say so.
//
// This class runs that script tree with the ZH ScriptEngine rules (ScriptEngine.cpp: update 5503 counts the countdown timers down and runs every
// active non subroutine script of every active non subroutine group; executeScript 6950: difficulty flags, DelayEvaluationSeconds, the OR of AND lists,
// actions or false actions, one-shot; evaluateCounter / evaluateFlag / evaluateTimer / setTimer / callSubroutine 6339 .. 6853) and the condition /
// action ordinals of the BFME2 template tables (Open-BFME-2 ScriptEngine_initActionTemplates.cpp / _initConditionTemplates.cpp: every ordinal the
// music map uses carries exactly the parameter types the map stores).
//
// CLIENT presentation: the scripts read the logic (objects, model conditions, players) and Eva, keep their own flags / counters / timers and drive the
// audio manager's music; they write no logic state, and their random draws come from the audio stream (retail runs them inside the logic script
// engine, where SET_RANDOM_MSEC_TIMER draws GameLogicRandomValue: stop S-711). The music stack semantics of the MUSIC_SCRIPT_* actions (play / push
// atop / pop, play N times then set a flag) are read from the action templates' UI strings, the audio manager's request types AR_PushMusic /
// AR_PopMusic (RW 0x4A813E) are not decoded: stop S-710.

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/PlayerScience.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
class AudioManager;
class Eva;
class GameLogic;
class PlayerList;
struct PlayerScriptsList;
struct LoadedMap;
struct Script;
struct ScriptGroup;
struct ScriptItem;
struct ScriptCondition;
struct ScriptActionRec;

class MusicScripts
{
public:
	// `audio`, `logic`, `players` outlive this object; `eva` may be null (the Eva conditions are then false and counted)
	MusicScripts(AudioManager &audio, GameLogic &logic, PlayerList &players, Eva *eva);
	~MusicScripts();

	// Reads the map AudioSettings MusicScriptLibraryName names (the archive name, with or without the folder). false + *error when it cannot be read.
	bool load(ArchiveFileSystem &fs, const std::string &mapPath, std::string *error);
	// loads an already read script tree (tests)
	void loadScripts(const PlayerScriptsList &scripts);

	// once per logic frame (ZH ScriptEngine::update): `frame` is the logic frame number
	void update(unsigned frame);

	// introspection (tests, the report)
	bool flag(const std::string &name) const;
	int counter(const std::string &name) const;
	const std::string &currentTrack() const { return m_lastTrack; }
	size_t stackDepth() const { return m_stack.size(); }
	struct Stats
	{
		std::uint64_t scriptsRun = 0, actionsRun = 0, tracksStarted = 0, trackFailures = 0, tracksCompleted = 0;
		std::map<std::string, std::uint64_t> unported; ///< condition / action ordinals this port does not answer (the stops)
	};
	const Stats &stats() const { return m_stats; }
	size_t scriptCount() const;

	static std::vector<std::string> acceptanceStops();

private:
	struct Node; // a script or group with its runtime state
	struct Counter
	{
		int value = 0;
		bool isCountdownTimer = false;
	};
	struct Track
	{
		std::string event;
		int remaining = 1;     ///< plays left (-1: forever)
		std::string notifyFlag;
		std::uint32_t handle = 0;
		bool started = false;
		unsigned startedFrame = 0;
	};

	void build(const std::vector<ScriptItem> &items, std::vector<std::unique_ptr<Node>> &out);
	void executeList(std::vector<std::unique_ptr<Node>> &list);
	void executeScript(Node &n);
	bool evaluate(Node &n);
	bool evaluateCondition(const ScriptCondition &c);
	void executeActions(const std::vector<ScriptActionRec> &actions);
	void executeAction(const ScriptActionRec &a);
	Node *find(const std::string &name);
	Node *findIn(std::vector<std::unique_ptr<Node>> &list, const std::string &name);
	void unported(const char *kind, int type, const std::string &note);
	// players named by a script parameter ("<Local Player>", "<All Players>", "<Local Player's Enemies>", a player name)
	std::vector<int> players(const std::string &param) const;
	int countUnitsNear(const std::vector<int> &players, const Coord3D &where, float distance) const;
	static bool compare(int value, int op, int against);
	// the music stack
	void startTrack(Track &t, bool fadeOut);
	void stopCurrent(bool fade);
	void updateMusic();

	AudioManager &m_audio;
	GameLogic &m_logic;
	PlayerList &m_players;
	Eva *m_eva;
	std::unique_ptr<LoadedMap> m_source; ///< the music map (the script tree points into it)
	std::vector<std::unique_ptr<Node>> m_lists;
	std::map<std::string, Counter> m_counters;
	std::map<std::string, bool> m_flags;
	std::map<std::string, ObjectID> m_named;
	std::vector<Track> m_stack;
	// lane AUDIO-4 (U20): the music's own copy of ScriptEngine + 0x1A3A8 (PLAYER_ACQUIRED_SCIENCE consumes from it): replayed from the players'
	// notices, never the logic engine's queues (the music is client side and must not change them)
	std::array<AcquiredScienceQueue, 20> m_acquiredSciences;
	std::string m_lastTrack;
	unsigned m_frame = 0;
	Stats m_stats;
};
