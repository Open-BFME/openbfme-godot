// OpenBFME. GPL-3.0.
//
// The engine facilities the APT screens consume.  Retail's screens reach them through globals (TheGameText, TheMapCache,
// TheMessageStream, the PlayerTemplateStore ...); the port passes one environment struct so a screen can be built in a test with
// exactly the data it is given.  Every pointer may be null; a screen that needs a facility it was not given reports it
// (no silent default).

#pragma once

#include <string>

class GameTextSource;
class SkirmishSetupSource;
class NewGameSink;
class SkirmishProfileStore;
struct LoadScreenInfo;
struct ScoreScreenData;
class ShellServices;
struct SaveLoadInfo;
class LANAPI;
class OptionPreferences;

// lane END-2: what QuitMenu.apt asks of the game it is opened over (TheGameLogic + 0x110 / + 0x114, the recorder, the local player)
struct QuitMenuContext
{
	bool inGame = false;              ///< TheGameLogic exists (a live game)
	int gameMode = 2;                 ///< + 0x110: 1 LAN, 2 skirmish, 5 Internet
	int gameKind = 3;                 ///< + 0x114 (3: a skirmish, RW 0x779F20; 1 LAN / 2 Internet, RW 0x779CB4)
	bool replay = false;              ///< TheRecorder plays back (RW 0x7B0F25 == 1)
	bool localPlayerDefeated = false; ///< ThePlayerList's local player + 0x754 (RW 0x6AAC4B)
	bool alliedVictory = false;       ///< TheVictoryConditions vslot 0x48 (RW 0x921891)
	bool isMultiplayer() const { return gameMode == 1 || gameMode == 5; } // RW 0x441B7C
};

struct ShellEnvironment
{
	GameTextSource *gameText = nullptr;       // labels -> text (GameText.h)
	SkirmishSetupSource *skirmish = nullptr;  // factions, maps, colours for the skirmish lobby (GUI/Skirmish/SkirmishSetup.h)
	NewGameSink *newGame = nullptr;           // where StartGame posts the new-game message (GUI/Skirmish/SkirmishSetup.h)
	LoadScreenInfo *loadScreen = nullptr;     // what LoadScreen.apt shows (GUI/LoadScreenInfo.h); null: its providers answer nothing and say so
	SkirmishProfileStore *profiles = nullptr; // the Skirmish profiles (GUI/Skirmish/SkirmishSetup.h); null: the screen keeps its own in memory [S-179]
	ScoreScreenData *scoreScreen = nullptr;   // lane END-1: what TimeLine.apt shows (GameClient/EndGame.h); null: its providers answer nothing and say so
	ShellServices *services = nullptr;        // lane END-1: where TimeLine.apt's buttons go (the shell's requests)
	SaveLoadInfo *saveLoad = nullptr;         // lane MP-2: what SaveLoad.apt lists (GUI/SaveLoadInfo.h); null: it lists nothing and says so
	LANAPI *lan = nullptr;                    // lane MP-2: the LAN lobby LanLobby.apt drives (GameNetwork/LANAPI.h); null: it says so
	QuitMenuContext *quitMenu = nullptr;      // lane END-2: the game QuitMenu.apt is opened over; null: no game (TheGameLogic null)
	OptionPreferences *options = nullptr;     // lane UI-2: the player's Options.ini (GameClient/OptionPreferences.h); null: the Options screen saves nothing and says so
	std::string optionsFile;                  // lane UI-2: where `options` is written (the user data folder's Options.ini)
};
