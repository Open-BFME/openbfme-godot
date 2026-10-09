// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (ScriptEngine.cpp, ScriptActions.cpp, VictoryConditions.cpp), as RotWK
// extends it.
//
// EndGame (lane END-1): the CLIENT side of the end of a skirmish / LAN game: what the local player is told, the end screen, the human player's win / loss scripts
// and the data of the score screen. Nothing here writes logic state; the logic's side is VictoryConditions and the ScoreKeeper.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the end of a multiplayer game is SCRIPTED: the skirmish sides list gives every human side the script library "Multiplayer_Human" (RW 0x731C69, string
//     RW 0xC24130; SidesList preparation RW 0x73193D, next to "SkirmishHuman" and "FactionCivilian"). Its ScriptList holds, in the group "Multiplayer Win/Loss",
//     "Multiplayer Win" (MULTIPLAYER_ALLIED_VICTORY -> VICTORY, one-shot) and "Multiplayer Lose" (MULTIPLAYER_ALLIED_DEFEAT or MULTIPLAYER_PLAYER_DEFEAT ->
//     DEFEAT, one-shot); the data, never this code, says so. The conditions are VictoryConditions vslots 0x48 / 0x4C and RW 0x7E526C (the evaluator's table
//     RW 0x7ED414: ordinals 44 / 45 / 46 -> RW 0x7EC1D3 / 0x7EC1E3 / 0x7EC1F3).
//   * doVictory (ScriptActions RW 0x7BF05C): closes the menus (RW 0x779173) and the in-game input (RW 0x7BC586), then, when the local player exists,
//     TheVictoryConditions->showEndGame("APT:EndVictorious" (or "APT:EndGameOver" when RW 0x7BB35D: the local player is dead or the game is a replay),
//     the local side is evil, "Gui_VictoryScreen", "Gui_VictoryCheerEvil" / "Gui_VictoryCheerGood"); doDefeat (RW 0x7BF15E): "APT:EndDefeat" (or
//     EndGameOver), "Gui_DefeatScreen", no cheer.
//   * showEndGame (vslot 0x5C, RW 0x808E1F): only when the window manager exists and the end screen is not up (+ 0x10): the Apt text ":VictoryDefeat" =
//     TheGameText(label), then `_level13.ShowEndGame("0" when evil else "1", the screen sound, the cheer)` (RW 0x808E5B: the data strings 0xBD5D8C "0" / 0xBD5D90 "1") (RW 0x62279C formats "_level%d"); + 0x10 = 1,
//     + 0x14 = timeGetTime(), the display's + 0x140 = 0.
//   * updateEndGame (vslot 0x7C, RW 0x808A20, first in update): 7000 ms of real time after showEndGame -> hideEndGame (vslot 0x60, RW 0x808CCD):
//     `_level13.HideEndGame()`, + 0x10 = 0, the display's + 0x140 = 1, and in a multiplayer game with a single alliance left the window transition group
//     "MPorSkirmishFadeToScoreScreen" (WindowTransitions.ini: a SOUNDFADE of the TACTICAL view over 30 frames, LeaveSilent).
//   * VictoryConditions::update, per newly defeated player at frame > 1 (RW 0x8090A4 .. 0x80925E): "GUI:YouHaveBeenDefeated" to the in-game UI (vslot 0x3C) when
//     the player is the local one (and the game is a replay / a saved game or of kind 0), else "GUI:PlayerHasBeenDefeated" (vslot 0x48, with the name) when
//     the game is not single player and its kind is not 0; then in a LAN / Internet (RW 0x441B7C) or skirmish (mode 2) game: the local player's
//     relationship to the defeated player's default team (RW 0x6ADBEB) ALLIES: the local player itself -> showEndGame("APT:EndDefeat", evil,
//     "Gui_DefeatScreen") (after four RW 0x61632F calls when RW 0xDE3D6C is set); another ally -> Eva event 6 (AllyDefeated); else Eva event 7
//     (EnemyDefeated) (TheEva RW 0x5DD9EE).
//   * the score screen is TimeLine.apt (GameLogic::clearGameData RW 0x7792BC -> RW 0x927898 with the game type: 2 skirmish, 3 LAN, 4 Internet, 1 single
//     player, 6 / 7 / 8 the War of the Ring kinds); RW 0x9275EC fills one 0x50-byte entry per occupied game slot whose player is not an observer
//     (RW 0x9270AD: the local player first): + 4 the display name, + 8 the colour (Player + 0x2A0), + 0xC the side (Player + 0x58), + 0x10 the result
//     (0 victorious: VictoryConditions vslot 0x38; 1 defeated: vslot 0x40; 2 disconnected: RW 0x90313E; 3 none of them), the ScoreKeeper's per-frame
//     vector (+ 0x6F8) and its fortress marks (+ 0x704). See GUI/AptScreens/AptTimeLine.h for the screen.
// INFERENCE / NOT PORTED (stop S-1060): the library is run once, for the local player (retail runs it for every human side; the end screen's + 0x10 test makes
// a second VICTORY / DEFEAT a no-op); only the conditions CONDITION_TRUE and 44 / 45 / 46 and the actions VICTORY, DEFEAT, ENABLE_SCRIPT, DISABLE_SCRIPT and
// SET_TIMER are answered here, every other condition is false and every other action is counted (their LOGIC effects, the base unpacking and the money of
// the library's other scripts, belong to a logic script engine this port does not have); the library path pattern "Libraries\<name>\<name>.map" is the
// music library's; doVictory's menu / input calls (RW 0x779173, RW 0x7BC586) and RW 0x61632F are not ported; the movie level is GuiFX.apt's.

#pragma once

#include "Common/GameCommon.h"
#include "Common/ScoreKeeper.h"
#include "GameLogic/VictoryConditions.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
class GameLogic;
struct LoadedMap;
struct ScriptCondition;
struct ScriptActionRec;
struct ScriptItem;
struct PlayerScriptsList;
struct Script;

// The victory state of one completed logic frame as the client reads it (LogicSnapshot::endGame): the shared events and the LOCAL player's answers.
struct EndGameView
{
	unsigned frame = 0;             ///< the frame the view was built in (a later frame with the same state shares it: the caller passes the frame it presents)
	bool singleAllianceRemaining = false;
	unsigned endFrame = 0;
	bool observer = false;
	int localPlayerIndex = -1;
	bool localEvil = false;       ///< the local player's template is evil (PlayerTemplate + 0x1BC)
	bool localAlive = true;       ///< RW 0x6AAC52 for the local player (doVictory's "game over" text when false)
	bool alliedVictory = false;   ///< condition 44 (VictoryConditions vslot 0x48)
	bool alliedDefeat = false;    ///< condition 45 (vslot 0x4C)
	bool playerOnlyDefeat = false; ///< condition 46 (RW 0x7E526C)
	std::vector<VictoryConditions::Event> events;
	// the names and relationships the messages need: per player index (the PlayerList's), its display name and the local player's relationship to its default team
	std::vector<std::string> displayNames; ///< UTF-8 (Player::getPlayerDisplayName)
	std::vector<int> localRelationship;
	// lane SCRIPT-1: the logic ScriptEngine runs the side libraries (Multiplayer_Human's VICTORY / DEFEAT); its end requests for the local player's side
	bool logicScripts = false;
	std::vector<bool> scriptEnds; ///< in execution order: true VICTORY / QUICKVICTORY, false DEFEAT

	bool sameAs(const EndGameView &o) const;
	// Must run on the thread that owns the simulation, between frames; `previous` (may be null) is returned when nothing changed
	static std::shared_ptr<const EndGameView> build(GameLogic &logic, const std::shared_ptr<const EndGameView> &previous);
};

// One request of the end sequence for the presentation (the device layer carries them out on the movies, the in-game UI and the audio).
struct EndGameRequest
{
	enum Kind
	{
		SHOW_END_GAME = 0, ///< text = the game text label for ":VictoryDefeat", flag = evil, sound / cheer
		HIDE_END_GAME = 1,
		MESSAGE = 2,       ///< text = the game text label, name = the player's display name (PlayerHasBeenDefeated)
		EVA = 3,           ///< eva = the Eva event index (6 AllyDefeated, 7 EnemyDefeated)
		TRANSITION = 4     ///< text = the window transition group ("MPorSkirmishFadeToScoreScreen")
	};
	int kind = SHOW_END_GAME;
	std::string text;
	std::string name; ///< UTF-8
	bool evil = false;
	std::string sound, cheer;
	int eva = -1;
};

// The human player's script library run with ZH's script engine rules (ScriptEngine::update / executeScript, as GameClient/MusicScripts) over EndGameView.
class EndGameScripts
{
public:
	EndGameScripts();
	~EndGameScripts();
	// the side's library map ("Libraries\Multiplayer_Human\Multiplayer_Human.map" for the library "Multiplayer_Human"); false + *error when it cannot be read
	bool load(ArchiveFileSystem &fs, const std::string &libraryName, std::string *error);
	void loadScripts(const PlayerScriptsList &scripts);
	// once per logic frame; the actions VICTORY / DEFEAT come back as `victory` / `defeat` calls of the controller
	struct Actions
	{
		bool victory = false, defeat = false;
	};
	Actions update(const EndGameView &view, unsigned frame);
	size_t scriptCount() const;
	const std::map<std::string, std::uint64_t> &unported() const { return m_unported; }
	std::uint64_t scriptsRun() const { return m_scriptsRun; }

	static std::string libraryPath(const std::string &libraryName);

private:
	struct Node;
	void build(const std::vector<ScriptItem> &items, std::vector<std::unique_ptr<Node>> &out);
	void executeList(std::vector<std::unique_ptr<Node>> &list, const EndGameView &view, Actions &a);
	void executeScript(Node &n, const EndGameView &view, Actions &a);
	bool evaluateCondition(const ScriptCondition &c, const EndGameView &view);
	void executeActions(const std::vector<ScriptActionRec> &actions, Actions &a);
	Node *find(std::vector<std::unique_ptr<Node>> &list, const std::string &name);

	std::unique_ptr<LoadedMap> m_source;
	std::vector<std::unique_ptr<Node>> m_lists;
	std::map<std::string, int> m_timers; ///< SET_TIMER countdowns (frames)
	std::map<std::string, std::uint64_t> m_unported;
	std::uint64_t m_scriptsRun = 0;
	unsigned m_frame = 0;
};

// The end sequence of the local player's client (VictoryConditions' client half: the messages, Eva, showEndGame / updateEndGame / hideEndGame).
class EndGameController
{
public:
	// `mode`: the economy context's game mode (1 LAN, 2 skirmish, 5 Internet, ...); `kind`: GameLogic + 0x114
	void start(int mode, int kind);
	// once per presented logic frame (the frame's view) and at least once per render frame with view == null (the real time clock of updateEndGame)
	void update(const EndGameView *view, unsigned frame, double nowMs);
	std::vector<EndGameRequest> takeRequests();
	EndGameScripts &scripts() { return m_scripts; }
	bool scriptsLoaded() const { return m_scriptsLoaded; }
	void setScriptsLoaded(bool on) { m_scriptsLoaded = on; }

	bool endGameShowing() const { return m_showing; }
	bool endGameShown() const { return m_shownOnce; }      ///< showEndGame ran at least once
	bool victoryScreen() const { return m_lastWasVictory; }
	bool hidden() const { return m_hiddenOnce; }           ///< hideEndGame ran after a show
	bool fadeToScore() const { return m_fadeToScore; }     ///< the transition group of hideEndGame ran (a single alliance remained)

	static constexpr double kEndGameMs = 7000.0; // RW 0x808A31: cmp 7000 (timeGetTime milliseconds)
	// the stops of the client end of a game (S-1060 end sequence, S-1063 score screen, S-1064 leaving the game)
	static std::vector<std::string> stopLines();
	// the predefined Eva event `index` (EvaEventStore::predefinedNames, RW 0xBF2168) by name ("" outside the table), and its report to TheEva (AudioApi::reportEva)
	static std::string evaName(int index);
	static bool playEva(int index);

private:
	void showEndGame(const std::string &label, bool evil, const std::string &sound, const std::string &cheer, double nowMs, bool victory);
	void hideEndGame();

	EndGameScripts m_scripts;
	bool m_scriptsLoaded = false;
	int m_mode = 2, m_kind = 3;
	size_t m_eventsSeen = 0;
	size_t m_scriptEndsSeen = 0; ///< lane SCRIPT-1
	bool m_showing = false, m_shownOnce = false, m_hiddenOnce = false, m_fadeToScore = false, m_lastWasVictory = false;
	bool m_singleAlliance = false;
	double m_shownAt = 0.0;
	std::vector<EndGameRequest> m_requests;
};

// What TimeLine.apt shows (RW 0x9275EC / 0x9270AD), taken from the logic when the game is left (GameLogic::clearGameData).
struct ScoreScreenData
{
	struct Entry
	{
		int playerIndex = -1;
		std::string name;             ///< + 4 (UTF-8)
		std::uint32_t color = 0;      ///< + 8 (0xRRGGBB)
		std::string side;             ///< + 0xC
		int result = 3;               ///< + 0x10: 0 victorious, 1 defeated, 2 disconnected, 3 none
		bool local = false;
		int score = 0;                ///< ScoreKeeper::computeScore at the end (RW 0x79DFFA)
		std::vector<ScoreKeeper::PerFrameStats> perFrame;
		std::vector<int> fortressMarks;
		// the totals of the stats list
		int unitsBuilt = 0, unitsLost = 0, unitsDestroyed = 0, structuresBuilt = 0, structuresLost = 0, structuresDestroyed = 0;
		std::uint32_t moneyEarned = 0, moneySpent = 0;
		std::uint32_t purchasePoints = 0;
		float skillPoints = 0.0f;
		// lane END-2: the rest of the statistics page's inputs (RW 0x9CDEC1)
		std::uint32_t sessionSeconds = 0;    ///< RW 0x79DC0C: (end frame, else the frame) / 5
		std::uint32_t firstHeroSeconds = 0;  ///< RW 0x79DC27: + 0xF4 / 5
		int fortressesBuilt = 0;             ///< + 0x328 entries
		bool favoriteUnitFound = false;      ///< RW 0x79E3A8 found a template (else the cell keeps GUI:None)
		std::string favoriteUnitLabel;       ///< its DisplayName label (template + 0x30; may be empty)
		std::uint32_t moneyReceived = 0, moneyGiven = 0; ///< + 0xC / + 0x10
		int spentStructures = 0, spentUnits = 0, spentHeroes = 0; ///< + 0x18 / + 0x14 / + 0x1C
		int heroesBuilt = 0, heroesLost = 0;
	};
	int type = 2;                    ///< the screen's game type (+ 0x284): 2 skirmish, 3 LAN, 4 Internet
	bool localIsObserver = false;    ///< + 0x294
	bool valid = false;
	unsigned frames = 0;             ///< the logic frames the game ran
	std::vector<Entry> entries;

	// RW 0x9275EC for a game started from game slots: the players in slot order (the local player first, RW 0x9270AD); `slotPlayerIndices` lists the player
	// index of every occupied slot in slot order (-1: no player), `disconnected` the indices whose slot says so (RW 0x90313E)
	// RW 0x927898's screen type: 3 for a LAN game (the economy's mode 1, or a network session whatever the logic's mode says), 4 for the Internet (mode 5), else 2
	static int typeFor(int gameMode, bool lanSession) { return (gameMode == 1 || lanSession) ? 3 : (gameMode == 5 ? 4 : 2); }
	static ScoreScreenData build(GameLogic &logic, int type, const std::vector<int> &slotPlayerIndices, const std::vector<int> &disconnected);

	// RW 0x9257F2: the graph's modes ("Units" 0, "Structures" 1, "Resources" 2, "Territories" 3, "FinalScore" 4 and anything else) and the value of one entry
	// (RW 0x92525D: + 0xC, + 0xE, + 4 as integers, + 8 the score; the War of the Ring source RW 0x9252C4 is not ported)
	static int graphMode(const std::string &name);
	static float graphValue(const ScoreKeeper::PerFrameStats &e, int mode);
	// the y axis: the highest value over every entry and frame (start -FLT_MAX), then the step: p = 10^max(1, digits(int(max)) - 2), top = ((int(max) / p + 1) * p)
	// / 10 (at least 1) and the axis runs 0 .. 10 * top
	struct Axis
	{
		int samples = 0;          ///< the most per-frame entries of any player
		float maxValue = 0.0f;
		int step = 1;             ///< the y label step (Timeline:YAxis:i = i * step, i 0 .. 10)
		float top = 10.0f;        ///< 10 * step
		std::vector<std::string> yLabels, xLabels; ///< UTF-8
		std::string totalTime; ///< Timeline:TotalTime (= the last x label)
		int timeFormat = 0;    ///< 0 APT:TimeMinuteSecond (under an hour), 1 APT:TimeHoursMinute (under a day), 2 APT:TimeDaysHoursMinute
	};
	// `formats` are the game texts of APT:TimeMinuteSecond / APT:TimeHoursMinute / APT:TimeDaysHoursMinute (UTF-8): their letters s, m, h, d are replaced
	static const char *const kTimeFormatLabels[3];
	static const char *const kTimeDescriptionLabels[3];
	Axis axis(int mode, const std::string formats[3]) const;
	static std::string formatTime(const std::string &format, int seconds);

	// lane END-2: the statistics page (TimeLine.apt's ListBox gadget AptTimeLine::StatsList). RW 0x9F19DC makes 24 rows (labels below, fetched from the
	// game text, RW 0x9F18CD) of one value per entry (RW 0x9CDEC1, see EndGame.cpp); a row that got no value (17 and 21) is not listed. Each value keeps its
	// number (RW 0x9F0B0B: the row's best is the highest, strictly); the list shows the label in white, then the value of each shown entry in white when it
	// is the row's best, else 0xFF7FAABB (RW 0x9F082F), then an empty row (RW 0x9F0B50).
	static constexpr int kStatRows = 24;
	static const char *const kStatLabels[kStatRows];
	struct StatCell
	{
		bool present = false;
		std::string text; ///< UTF-8
		float value = 0.0f;
	};
	struct StatRow
	{
		std::string label;            ///< the fetched label (UTF-8)
		std::vector<StatCell> values; ///< per entry
		float best = 0.0f;            ///< the values are never negative: the start value does not decide anything
		bool shown = false;           ///< + 0x14: a value was stored
	};
	// `fetch` answers a game text label (UTF-8; the missing-label text when absent); `decimal` is the locale's decimal separator (RW 0x9F0885:
	// GetLocaleInfoA LOCALE_SDECIMAL; the host's locale is not read: ".")
	std::vector<StatRow> statRows(const std::function<std::string(const std::string &)> &fetch, const std::string &decimal = ".") const;
	// RW 0x9F07C7 (an integer), RW 0x9F0885 (a ratio), RW 0x9F0E7A (a duration in seconds)
	static StatCell intCell(std::int32_t v);
	static StatCell ratioCell(float a, float b, const std::string &decimal);
	static StatCell timeCell(std::uint32_t seconds, const std::function<std::string(const std::string &)> &fetch);
	// RW 0x9CD723 (AptTimeLine::SetPlayerFocus): the three shown entries of the statistics page for the movie's argument
	static std::array<int, 3> focusColumns(int argument, int entries, bool localIsObserver, std::array<int, 3> current);
};
