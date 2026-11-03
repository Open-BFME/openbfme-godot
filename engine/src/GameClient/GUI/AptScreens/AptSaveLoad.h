// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SaveLoad.apt (CodePrefix AptSaveLoad, lane MP-2; lane SAVE-2: the saved-game pages): the screen of saved games and replays. AptSaveLoad::OpenScreen
// (RW 0x816655, BFME2 decomp AptSaveLoad_OpenScreen.cpp, tier A) pushes SaveLoad.apt and sets + 0x294 the mode (2 load, 3 save), + 0x298 the game types, + 0x2A0
// the first type of the mask (1, 2 or 4) and + 0x29C the third argument (opened over a game). Callers: the main menu's LoadReplay (RW 0x91C36C: 2, 4, 0)
// and, INFERENCE, its LoadGame (RW 0x91C360: 2, 0xB, 0, the pending-action switch RW 0x91C359), the quit menu's SaveMenu / LoadMenu (RW 0x92179F /
// 0x9217F0: 3 / 2, the game's type, 1), the campaign menu (RW 0x927A93 / 0x927AA4: 3 / 2, 1, 0), the LAN lobby (RW 0x84929F: 2, 0x10, 0).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the menu object RW 0xDE8A80, constructor / registration RW 0x81874E; the BFME2 decomp's
// AptSaveLoadCallbacks.cpp is tier A for these callbacks):
//   * commands AptSaveLoad::OnInitialized / ConfirmationCancel (state 1: the lists enabled again), OnClosed, Cancel (RW 0x816146), Load (RW 0x816DBF: in
//     state 0 the selection (RW 0x8162E5) is taken; over a game (+ 0x29C) state 14 asks first (the update RW 0x819548 calls the movie's
//     showMessageBox("Load"), state 18); the Replay type loads at once (RW 0x816981); else state 3: the screen closes and the file loads), Save
//     (RW 0x817B5F: the trimmed FileNameTextEntry text + the mode's extension (RW 0x6DCDE9); an existing file -> state 15, showMessageBox("Save"), state
//     19; else RW 0x817719: the disk check (APT:SaveFileDiskFull), state 4: the box APT:SaveGameProgress / APT:SavingWait, then RW 0x817D0E saves and
//     shows APT:SaveGameProgress / GUI:GameSaved), Delete (RW 0x81680D: a selection -> state 16, showMessageBox("Delete"), state 20), ConfirmationOk
//     (RW 0x817CC4: state 0x12 -> load, 0x13 -> save, 0x14 -> delete RW 0x81707F, 0x15); the screen reference AptSaveLoad::InitGadgets (RW 0x817667:
//     "GameList" and "AutoSaveList" are the list boxes (+ 0x288 / + 0x28C), "FileNameTextEntry" the name entry (+ 0x290): emptied, at most 40 characters,
//     validation flags 8, then the default name, RW 0x816FA3 (the decomp's SetDefaultFileName, tier A): the selected entry's file name, else GameState RW 0x6DE38D: GUI:DefaultSaveFileName
//     with the first number from 1 whose file does not exist);
//   * the providers of RW 0xDA726C (RW 0x816164): SaveLoadMode = "Save" for mode 3, "Load" for mode 2 (RW 0xC4FF8C / 0xC4FF84); GameTypes = the names of
//     the mask's types concatenated ("Campaign", "Skirmish", "Replay", "WOTRSP", "WOTRMP"); CurrentGameType (setting): the type the movie's tabs chose
//     (state 2: the lists are filled again);
//   * the replay list (RW 0x818D41): both lists get 4 columns of 28 / 36 / 19 / 17 percent of their width; for every replay file (TheLocalFileSystem's list
//     of Replays\*.BfME2Replay) a row: column 0 the map's display name (the map file name when the map cache does not know it), column 1 the file name
//     without its extension, columns 2 and 3 the recording's time and date (RW 0x6DD1B2 / 0x6DD0DE; the movie heads them Time and Date), all in 0xFFFFFFFF, or 0xFF808080 when the replay's version is not
//     this one; the file named GUI:LastReplay goes to the AutoSaveList, the others to the GameList;
//   * the saved-game list (GameState::populateSaveGameListbox RW 0x6E04AD, BFME2 decomp GameStateAutoSave.cpp tier B same-shape): the same four columns;
//     on the save page first a "GUI:NewSaveGame" row in (200, 200, 200); per file of the type (Campaign: types 0, 1, 8; Skirmish: 2) a row (column 0 the
//     map's display name, 1 the file name without its extension, 2 the time, 3 the date) in (239, 204, 82) for a campaign save, else white; auto-saves
//     go to the AutoSaveList; then the GameList's first row is selected, else the AutoSaveList's.
// OpenBFME DIFFERENCES / NOT PORTED (stops S-1126, S-4003): the host gives the lists (GUI/SaveLoadInfo.h: OpenBFME replays and .obfmesave files; retail
// reads the folders itself) and receives the chosen file, the typed name and the deleted file as shell requests (LoadReplayFile, LoadGameFile,
// SaveGameFile, DeleteGameFile); the host saves and answers saveFinished(), which shows the result in the message box (APT:SaveGameProgress with
// GUI:GameSaved or the error) instead of retail's two boxes; a save the running build cannot load is drawn grey (0xFF808080) and Load shows why
// (GUI:ErrorLoadingGame's title, the refusal as text); lane CAMPSAVE-1: the campaign's auto-saves are listed in the AutoSaveList; retail saves are listed grey as import entries that do not load yet (SAVE-7); the disk space
// check (RW 0x6DD795) is the host's write failure; the War of the Ring page (type 8, WOTR2 milestone 3) lists the War of the Ring map's saves with
// RW 0x6E04AD's filter-8 columns (file name, blank, time, date; a map save gold); the multiplayer type is not offered. The gadget
// handler is retail's (RW 0x8180A8): a selection clears the other list's, refreshes the buttons (RW 0x816927 -> 0x8165A1: the movie's
// LoadButtonEnable / DeleteButtonEnable) and on the save page takes the file's name into the entry (RW 0x816FA3) and SaveButtonEnable (RW 0x816F24); a
// double click saves or loads, Enter in the entry saves, a change of the entry's text refreshes SaveButtonEnable; nothing while a confirmation or
// a result box is up.

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/SaveLoadInfo.h"

#include <memory>
#include <string>

struct ShellEnvironment;
class AptMessageBox;

class AptSaveLoad : public AptScreen
{
public:
	AptSaveLoad(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptSaveLoad() override;

	void runInit() override;
	void frameUpdate() override;
	// lane SAVE-UI1: RW 0x816627, the screen's input procedure (the host gives it Escape): true when the key is used (KEY_ESC); its release in state 0
	// closes the screen
	bool inputMessage(std::uint32_t msg, WindowMsgData data1, WindowMsgData data2);
	bool inputKey(int key, int keyState) override { return inputMessage(GWM_CHAR, (WindowMsgData)key, (WindowMsgData)keyState); }
	// lane SAVE-UI1: + 0x2A4, the constructor showed the front-end background (none was up), the dtor hides it (tests)
	bool showedBackground() const { return m_showedBackground; }

	// the rows put into the lists (tests): every row as "list|col0|col1|col2|col3|colour"
	const std::vector<std::string> &rows() const { return m_rows; }
	// the replay / saved game the Load command chose last ("" none)
	const std::string &loaded() const { return m_loaded; }
	// the list boxes (tests)
	GameWindow *gameList() { prune(); return m_gameList; }
	GameWindow *autoSaveList() { prune(); return m_autoSaveList; }
	GameWindow *fileNameEntry() { prune(); return m_fileEntry; }
	// lane SAVE-2: + 0x27C (tests: 0 idle, 18 / 19 / 20 waiting for the confirmation of a load / save / delete)
	int state() const { return m_state; }
	int gameType() const { return m_gameType; }
	// lane SAVE-2: the host's answer to a SaveGameFile request: the message box shows APT:SaveGameProgress with GUI:GameSaved (ok) or the error; its Ok
	// closes the screen after a save (retail state 9)
	void saveFinished(bool ok, const std::string &errorUtf8);
	// lane SAVE-2: the host refused the LoadGameFile it was given (the full check of the file found what the list's header check could not: a damaged
	// log): GUI:ErrorLoadingGame's title with the save's name, the reason as text; the screen stays
	void loadRefused(const std::string &nameUtf8, const std::string &errorUtf8);
	// the message box the screen shows (tests): its title and text, "" when none was shown
	std::u16string boxTitle() const;
	std::u16string boxText() const;
	// the movie's showMessageBox calls made (tests): "Load", "Save", "Delete"
	const std::vector<std::string> &confirmations() const { return m_confirmations; }
	// the movie's button refreshes made (tests): "SaveButtonEnable 1", "LoadButtonEnable 0" ...
	const std::vector<std::string> &buttonCalls() const { return m_buttonCalls; }

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void prune();
	void populate();
	void populateReplays();
	void populateSaves();
	void load();
	void save();
	void remove();
	void confirmationOk();
	void doLoad();
	void doSave();
	void doDelete();
	void askConfirmation(const char *kind, int waitState);
	void setDefaultFileName();
	void refreshButtons();   // RW 0x816927 -> RW 0x8165A1
	void saveButtonEnable(); // RW 0x816F24
	void enableLists(bool enable);
	// OpenBFME's result / refusal box; `pressedButton` (a MainButtons clip: "Load", "Save") is revealed again after its Ok
	void showBox(const std::u16string &title, const std::u16string &text, bool closeAfter, const std::string &pressedButton);
	void revealButton(const std::string &button);
	std::u16string text(const std::string &label) const;
	// column 0 of a save (populateSaveGameListbox -> getMapDisplayName)
	std::string mapName(const SaveLoadInfo::Save &save) const;
	// the selected row's index into SaveLoadInfo::saves (-1: none or the new-save row)
	int selectedSave();

	ShellEnvironment &m_env;
	GameWindow *m_gameList = nullptr;     // + 0x288
	GameWindow *m_autoSaveList = nullptr; // + 0x28C
	GameWindow *m_fileEntry = nullptr;    // + 0x290
	bool m_populated = false;
	int m_gameType = 4;                   // + 0x2A0
	int m_state = 0;                      // + 0x27C
	GameWindow *m_selectedList = nullptr;
	int m_selectedRow = -1;
	std::vector<std::string> m_rows;
	std::vector<std::string> m_gameListPaths, m_autoSavePaths;
	std::vector<int> m_gameListSaves;     // lane SAVE-2: GameList row -> SaveLoadInfo::saves index (-1 the new-save row)
	std::vector<int> m_autoSaveSaves;     // lane CAMPSAVE-1: AutoSaveList row -> SaveLoadInfo::saves index
	std::string m_loaded;
	std::string m_pendingPath, m_pendingName; // the file / typed name a confirmation waits for
	unsigned m_generation = 0;
	std::unique_ptr<AptMessageBox> m_box;
	bool m_closeAfterBox = false, m_boxOpen = false;
	bool m_showedBackground = false;      // + 0x2A4 (lane SAVE-UI1)
	std::string m_boxButton;
	std::vector<std::string> m_confirmations;
	std::vector<std::string> m_buttonCalls;
};
