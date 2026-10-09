// OpenBFME. GPL-3.0.
//
// SaveLoadInfo (lane MP-2): what SaveLoad.apt lists (GameClient/GUI/AptScreens/AptSaveLoad.h). The host fills it before it pushes the screen (retail's
// AptSaveLoad reads the files itself: the Replays\ folder through TheLocalFileSystem, RW 0x818D41) and receives the chosen file through the shell's
// requests.

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
};
