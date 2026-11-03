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
struct ObjectivesScreenInfo;
struct CreateAHeroScreenContext;
class ArchiveFileSystem;
class GlobalLanguage;
struct OnlineShellContext;
struct OnlineHubContext;
struct ProfileSwitch;
struct CampaignMenuContext;
struct SkirmishHonorsGame;
struct LivingWorldShellInfo; // lane WOTR-1

// lane END-2: what QuitMenu.apt asks of the game it is opened over (TheGameLogic + 0x110 / + 0x114, the recorder, the local player)
struct QuitMenuContext
{
	bool inGame = false;              ///< TheGameLogic exists (a live game)
	int gameMode = 2;                 ///< + 0x110: 1 LAN, 2 skirmish, 5 Internet
	int gameKind = 3;                 ///< + 0x114 (3: a skirmish, RW 0x779F20; 1 LAN / 2 Internet, RW 0x779CB4; 0: a single player War of the Ring game,
	                                  ///< the strategic map and its battles: RW 0x6DCFDF's modes 4 / 3, the quit menu's save type 8 RW 0x92179F)
	bool replay = false;              ///< TheRecorder plays back (RW 0x7B0F25 == 1)
	bool localPlayerDefeated = false; ///< ThePlayerList's local player + 0x754 (RW 0x6AAC4B)
	bool alliedVictory = false;       ///< TheVictoryConditions vslot 0x48 (RW 0x921891)
	bool strategicMap = false;        ///< lane WOTR2 milestone 3: TheLivingWorldLogic's isSelectionLocked (the War of the Ring map is up, no battle runs)
	bool warPlayersGone = false;      ///< WOTR2 milestone 8 (step 4): a human of the network war has left (RW 0x6B5E3E(1) > RW 0x6B5E3E(0)): no Save
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
	// lane OWNERFIX-3: the skirmish game the Skirmish lobby posted last (retail: TheGameInfo), from which the score screen records the profile's battle honors
	// (GUI/Skirmish/RealTimeStats.h); null: no honors are recorded and the score screen says so
	SkirmishHonorsGame *skirmishHonors = nullptr;
	ShellServices *services = nullptr;        // lane END-1: where TimeLine.apt's buttons go (the shell's requests)
	SaveLoadInfo *saveLoad = nullptr;         // lane MP-2: what SaveLoad.apt lists (GUI/SaveLoadInfo.h); null: it lists nothing and says so
	LANAPI *lan = nullptr;                    // lane MP-2: the LAN lobby LanLobby.apt drives (GameNetwork/LANAPI.h); null: it says so
	QuitMenuContext *quitMenu = nullptr;      // lane END-2: the game QuitMenu.apt is opened over; null: no game (TheGameLogic null)
	OptionPreferences *options = nullptr;     // lane UI-2: the player's Options.ini (GameClient/OptionPreferences.h); null: the Options screen saves nothing and says so
	std::string optionsFile;                  // lane UI-2: where `options` is written (the user data folder's Options.ini)
	GameLODManager *gameLOD = nullptr;  // lane PLAY-2: GameLOD.ini's StaticGameLOD presets (the Options screen's advanced page); null: it says so. Lane
	                                    // OPTS-2: the level in effect too (Save applies it)
	TributeSource *tribute = nullptr;         // lane PLAY-1: the live game PlayerTribute.apt shows and sends tribute in (GUI/TributeInfo.h); null: no game, said so
	// lane FB7-1: what the Options screen's InitGadgets reads besides Options.ini (RW 0x9205C4)
	bool haveDefaultVolumes = false;          // AudioSettings DefaultSoundVolume .. DefaultMovieVolume (TheAudio's settings + 0x1C .., RW 0x6E5FB3)
	float defaultVolumes[5] = { 0, 0, 0, 0, 0 }; // SFX, Voice, Music, Ambient, Movie (0..1)
	bool haveScrollDefault = false;           // GameData KeyboardDefaultScrollSpeedFactor (GlobalData + 0xAFC, RW 0x6E59A9)
	float keyboardDefaultScrollSpeedFactor = 0.0f;
	std::vector<std::pair<std::string, std::uint32_t>> localAddresses; // the machine's IPv4 addresses (dotted text, host order value): RW 0x719C53's list
	std::vector<std::pair<int, int>> displayModes; // the resolutions the device offers (RW TheDisplay vslot 0x5C / 0x60)
	std::pair<int, int> currentResolution{ 0, 0 }; // the window's size now
	// lane OPTS-2: false when the device cannot apply the Resolution list (Godot does not change the monitor's display mode; the window size is the
	// OpenBFME screen's): the Options screen then opens with AllowResolutionChange off, the list disabled as retail's lobby / in-game openings leave it
	// (stop S-3401)
	bool resolutionChangeApplies = true;
	// lane OPTS-5: the device offers the OpenBFME options page (AptMenuPlayer.set_openbfme_available): with AptOptionsScreen::kOpenBFMEMenu on, the
	// Options screen opens as the one OpenBFME menu (the original game's settings among its rows), never on the retail page
	bool openbfmeOptionsMenu = false;
	bool shellMapOn = false;                  // lane FB7-1: GameData ShellMapOn (GlobalData + 0xAF0; Shell::showShellMap)
	PlayerStatusInfo *playerStatus = nullptr; // lane HUD-5: the players screen's Status rows (GUI/PlayerStatusInfo.h); null: the page shows no rows and says so
	ObjectivesScreenInfo *objectivesScreen = nullptr; // lane CAMPUI-1: what Objectives.apt lists (GUI/AptScreens/AptObjectivesMenu.h); null: no rows, said so
	CreateAHeroScreenContext *createAHero = nullptr; // lane CAH-1: what CreateAHero.apt edits (GUI/AptScreens/AptCreateAHero.h); null: it says so
	ArchiveFileSystem *fileSystem = nullptr;  // lane UI-4: the mounted archives (the credits roll reads Data\INI\Credits.ini); null: the credits say so
	ProfileSwitch *profileSwitch = nullptr;   // lane UBERJOIN-1: switches the performance mode for one game (GUI/ProfileSwitch.h); null: a game of the other mode says it cannot be joined
	OnlineShellContext *online = nullptr;     // lane ONLINE-4: the lobby client OnlineShell.apt drives (GUI/AptScreens/AptOnlineShell.h); null: it says so
	OnlineHubContext *onlineHub = nullptr;    // lane ONLINEHUB-1: the recorded games and the platform API the statistics, Quick Match and tournament pages
	                                          // read (GUI/AptScreens/AptStats.h); null: they say so
	CampaignMenuContext *campaignMenu = nullptr; // lane CAMPSAVE-1: what CampaignMenu.apt shows (GUI/AptScreens/AptCampaignMenu.h); null: it says so
	// lane CAMPSAVE-1: TheGlobalData + 0xAF5, the main menu's MainMenuUnlockBonusCampaign (RW 0x91BBAD): Options.ini's "BCU" read at start
	// (GlobalData::applyOptionPreferences RW 0x641DED -> RW 0x6E5E16: _strcmpi(value, "true") == 0), set by the campaign's end (RW 0x927F4C)
	bool bonusCampaignUnlocked = false;
	// lane CAMPSAVE-1: the Save folder (RW 0x6DD85E's folder) MainMenuContinueCampaign looks for "00000000.sav" in (RW 0x91BBBB); "" none
	std::string saveFolder;
	const GlobalLanguage *language = nullptr; // lane UI-4: language.ini's Language block (the credits' fonts, GameClient/GlobalLanguage.h); null: likewise
	LivingWorldShellInfo *livingWorld = nullptr; // lane WOTR-1: the War of the Ring scenarios and the strategic flag (GUI/Skirmish/LivingWorldShellInfo.h)
	// lane STABLEQA-1 (owner, 2026-10-30: Stable must not show War of the Ring yet; Nightly does). False in a Stable channel build
	// (BuildVersion::isStableChannel): the main menu's War of the Ring row is hidden and refused, the LAN lobby's War of the Ring page says it is a
	// Nightly feature and creates no game. Not retail: an OpenBFME release switch
	bool warOfTheRingOffered = true;
};
