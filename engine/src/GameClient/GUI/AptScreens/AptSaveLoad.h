// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SaveLoad.apt (CodePrefix AptSaveLoad, lane MP-2): the load screen of saved games and replays, here its replay page: the main menu's LoadReplay opens it
// with the game type Replay (RW 0x91C36C -> RW 0x816655(mode, 4, 0): TheShell pushes SaveLoad.apt, + 0x294 the mode, + 0x298 the game types, + 0x2A0 the
// first type of the mask, + 0x29C the third argument).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the menu object RW 0xDE8A80, constructor / registration RW 0x81874E):
//   * commands AptSaveLoad::OnInitialized / OnClosed / Load (RW 0x816DBF: with no pending state, the selection (RW 0x8162E5) is taken; the Replay type
//     (+ 0x2A0 == 4) loads it at once, RW 0x816981) / Save (RW 0x817B5F) / Delete (RW 0x81680D) / Cancel (RW 0x816146) / ConfirmationOk (RW 0x817CC4) /
//     ConfirmationCancel; the screen reference AptSaveLoad::InitGadgets (RW 0x817667: "GameList" and "AutoSaveList" are the list boxes (+ 0x288 / + 0x28C),
//     "FileNameTextEntry" the name entry (+ 0x290));
//   * the providers of RW 0xDA726C (RW 0x816164): SaveLoadMode = "Save" for mode 3, "Load" for mode 2 (RW 0xC4FF8C / 0xC4FF84); GameTypes = the names of
//     the mask's types concatenated ("Campaign", "Skirmish", "Replay", "WOTRSP", "WOTRMP"); CurrentGameType (setting): the type the movie's tabs chose;
//   * the replay list (RW 0x818D41): both lists get 4 columns of 28 / 36 / 19 / 17 percent of their width; for every replay file (TheLocalFileSystem's list
//     of Replays\*.BfME2Replay) a row: column 0 the map's display name (the map file name when the map cache does not know it), column 1 the file name
//     without its extension, columns 2 and 3 the recording's time and date (RW 0x6DD1B2 / 0x6DD0DE; the movie heads them Time and Date), all in 0xFFFFFFFF, or 0xFF808080 when the replay's version is not
//     this one; the file named GUI:LastReplay goes to the AutoSaveList, the others to the GameList.
// OpenBFME DIFFERENCES / NOT PORTED (stop S-1126): the host gives the list (GUI/SaveLoadInfo.h: OpenBFME replays of the enhanced profile, their file's date
// and time in OpenBFME's own format, "compatible" = the same profile identity) and receives the chosen file (ShellAction::LoadReplayFile); the saved-game
// pages (Campaign, Skirmish, War of the Ring), Save, Delete and the confirmation box are not ported.

#pragma once

#include "GameClient/GUI/AptScreen.h"

#include <string>

struct ShellEnvironment;

class AptSaveLoad : public AptScreen
{
public:
	AptSaveLoad(WindowManager &windows, Shell &shell, ShellEnvironment &environment);

	void runInit() override;

	// the rows put into the lists (tests): every row as "list|col0|col1|col2|col3|colour"
	const std::vector<std::string> &rows() const { return m_rows; }
	// the replay the Load command chose last ("" none)
	const std::string &loaded() const { return m_loaded; }
	// the list boxes (tests)
	GameWindow *gameList() { prune(); return m_gameList; }
	GameWindow *autoSaveList() { prune(); return m_autoSaveList; }

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void prune();
	void populate();
	void load();

	ShellEnvironment &m_env;
	GameWindow *m_gameList = nullptr;     // + 0x288
	GameWindow *m_autoSaveList = nullptr; // + 0x28C
	GameWindow *m_fileEntry = nullptr;    // + 0x290
	bool m_populated = false;
	int m_gameType = 4;                   // + 0x2A0
	GameWindow *m_selectedList = nullptr;
	int m_selectedRow = -1;
	std::vector<std::string> m_rows;
	std::vector<std::string> m_gameListPaths, m_autoSavePaths;
	std::string m_loaded;
};
