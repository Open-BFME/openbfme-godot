// OpenBFME. GPL-3.0.
//
// The engine facilities the APT screens consume.  Retail's screens reach them through globals (TheGameText, TheMapCache,
// TheMessageStream, the PlayerTemplateStore ...); the port passes one environment struct so a screen can be built in a test with
// exactly the data it is given.  Every pointer may be null; a screen that needs a facility it was not given reports it
// (no silent default).

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class GameTextSource;
class SkirmishSetupSource;
class NewGameSink;
class SkirmishProfileStore;
struct LoadScreenInfo;
class TributeSource;
struct ScoreScreenData;
class ShellServices;
struct SaveLoadInfo;
class LANAPI;
class OptionPreferences;
class GameLODManager;
struct PlayerStatusInfo;
struct CreateAHeroScreenContext;
class ArchiveFileSystem;
class GlobalLanguage;

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
	const GameLODManager *gameLOD = nullptr;  // lane PLAY-2: GameLOD.ini's StaticGameLOD presets (the Options screen's advanced page); null: it says so
	TributeSource *tribute = nullptr;         // lane PLAY-1: the live game PlayerTribute.apt shows and sends tribute in (GUI/TributeInfo.h); null: no game, said so
	// lane FB7-1: what the Options screen's InitGadgets reads besides Options.ini (RW 0x9205C4)
	bool haveDefaultVolumes = false;          // AudioSettings DefaultSoundVolume .. DefaultMovieVolume (TheAudio's settings + 0x1C .., RW 0x6E5FB3)
	float defaultVolumes[5] = { 0, 0, 0, 0, 0 }; // SFX, Voice, Music, Ambient, Movie (0..1)
	bool haveScrollDefault = false;           // GameData KeyboardDefaultScrollSpeedFactor (GlobalData + 0xAFC, RW 0x6E59A9)
	float keyboardDefaultScrollSpeedFactor = 0.0f;
	std::vector<std::pair<std::string, std::uint32_t>> localAddresses; // the machine's IPv4 addresses (dotted text, host order value): RW 0x719C53's list
	std::vector<std::pair<int, int>> displayModes; // the resolutions the device offers (RW TheDisplay vslot 0x5C / 0x60)
	std::pair<int, int> currentResolution{ 0, 0 }; // the window's size now
	bool shellMapOn = false;                  // lane FB7-1: GameData ShellMapOn (GlobalData + 0xAF0; Shell::showShellMap)
	PlayerStatusInfo *playerStatus = nullptr; // lane HUD-5: the players screen's Status rows (GUI/PlayerStatusInfo.h); null: the page shows no rows and says so
	CreateAHeroScreenContext *createAHero = nullptr; // lane CAH-1: what CreateAHero.apt edits (GUI/AptScreens/AptCreateAHero.h); null: it says so
	ArchiveFileSystem *fileSystem = nullptr;  // lane UI-4: the mounted archives (the credits roll reads Data\INI\Credits.ini); null: the credits say so
	const GlobalLanguage *language = nullptr; // lane UI-4: language.ini's Language block (the credits' fonts, GameClient/GlobalLanguage.h); null: likewise
};
