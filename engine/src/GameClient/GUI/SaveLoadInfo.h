// OpenBFME. GPL-3.0.
//
// SaveLoadInfo (lane MP-2; lane SAVE-2: the saved-game pages): what SaveLoad.apt lists (GameClient/GUI/AptScreens/AptSaveLoad.h). The host fills it before it
// pushes the screen (retail's AptSaveLoad reads the files itself: the Replays\ folder through TheLocalFileSystem, RW 0x818D41; the Save\ folder through
// GameState::populateSaveGameListbox, RW 0x6E04AD) and receives the chosen file through the shell's requests.

#pragma once

#include <string>
#include <vector>

struct SaveLoadInfo
{
	// RotWK + 0x294: 2 = load ("SaveLoadMode" answers "Load"), 3 = save ("Save")
	int mode = 2;
	// RotWK + 0x298: the game types the screen offers (1 Campaign, 2 Skirmish, 4 Replay, 8 War of the Ring SP, 0x10 MP); the main menu's LoadReplay opens
	// it with 4 (RW 0x91C36C -> RW 0x816655)
	unsigned flags = 4;
	// lane SAVE-2: RotWK + 0x29C, AptSaveLoad::OpenScreen's third argument (RW 0x816655): true when the screen is opened over a live game (the quit menu's
	// SaveMenu / LoadMenu, RW 0x92179F / 0x9217F0); a load then asks first (RW 0x816DBF: state 14, the movie's showMessageBox "Load")
	bool inGame = false;
	struct Replay
	{
		std::string path;          ///< the file the host plays when it is loaded
		std::string fileNameUtf8;  ///< the name without the extension (column 1)
		std::string mapUtf8;       ///< the map's display name (column 0; the map file name when the map is unknown)
		std::string dateUtf8, timeUtf8; ///< column 3 (date) and column 2 (time)
		bool compatible = true;    ///< recorded by this profile (otherwise drawn grey, 0xFF808080)
	};
	std::vector<Replay> replays;
	std::string lastReplayNameUtf8; ///< GUI:LastReplay: the file that goes to the AutoSaveList
	// lane SAVE-2: the saved games of the Save\ folder (OpenBFME's .obfmesave files and retail's own saves, listed as import entries)
	struct Save
	{
		std::string path;          ///< the file
		std::string fileNameUtf8;  ///< the name without the extension (column 1; what Save compares the typed name with)
		std::string mapPathUtf8;   ///< the saved game's map ("maps/map mp evendim/map mp evendim.map"): column 0 is its display name in the map cache
		std::string mapUtf8;       ///< the map's display label as saved (column 0 when the map cache does not know the map)
		std::string dateUtf8, timeUtf8; ///< column 3 (date) and column 2 (time)
		int gameKind = 2;          ///< 1 Campaign, 2 Skirmish (the page it is listed on)
		bool loadable = true;      ///< false: refused (another build, other data, a damaged file); drawn grey, Load shows `refusalUtf8`
		std::string refusalUtf8;
		bool retailImport = false; ///< a retail save (.BfME2Skirmish / .BfME2Campaign): listed, never loaded yet (import is SAVE-7)
	};
	std::vector<Save> saves;
	// lane SAVE-2: raised by the host whenever it changes the lists while the screen is open (a delete); the screen fills its lists again
	unsigned generation = 0;
};
