// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// LanLobby.apt (CodePrefix AptLanLobby, lane MP-2): the LAN lobby screen. The movie hosts LanOpenPlay.swf (the list of the games on the network, the
// player's name) and, once the player hosts or joins, MpGameSetup.swf: the same setup movie the Skirmish screen hosts, so this screen reuses AptSkirmish's
// MpGameSetup handling with the network game as its model (GameNetwork/LANAPI.h owns the options: the host's changes go out to every player, a
// player's own slot changes are requests to the host).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the screen's constructor / registration RW 0x848C3D, vtable RW 0xC5470C):
//   * commands AptLanLobby::OnInitialized (RW 0x9F3A3C), OnCancelBttn (RW 0x847A91: state 6 clears the start flag + 0x54C, else the game is left
//     (RW 0x84700F) and the state is 0), OnExitBttn (RW 0x846C41: the LAN lobby is left), OnStartGameBttn (RW 0x846F72: state 5), OnCreateGameBttn (RW
//     0x847B85), OnJoinGameBttn (RW 0x846F8A: state 1 -> 7), OnLoadGameBttn (RW 0x846F9E: state 1 -> 0xB), OnOptionsBttn (RW 0x846F53), OnLoadScreen
//     (RW 0x846FB2); the screen reference AptLanLobby::InitGadgets (RW 0x848A7F: LanLobby::CustomGamesList the game list (+ 0x6A8), LanLobby::NameEntry
//     the name (12 characters, RW 0x81606D), LanLobby::ScenarioDescriptionLobby / HostJoin); the page title APT:Network;
//   * the gadget messages (RW 0x847449): a game selected in the list shows it (RW 0x841C53), a double click joins it (as OnJoinGameBttn);
//   * the update (RW 0x8490E0, TheLAN->update first): state 0, once the movie is ready (RW 0x847ED0): without a game (+ 0x538) the movie's StartLobby
//     and state 1, else state 2; state 1: the list follows TheLAN's games; state 2: TheLAN->RequestGameCreate, state 3; state 5: the setup's start check
//     (RW 0x8431B8, as the skirmish lobby's) -> 6, else the movie's EnablePlayGame and state 4; state 6: the start waits (RW 0x84392C), refused -> 4 and
//     EnablePlayGame; state 7: the selected game (column 3's item data, RW 0x846D4F) -> TheLAN->RequestGameJoin, state 8; state 9: our slot is gone ->
//     the message box GUI:GSKicked / GUI:GSErrorTitle, the game left, state 0; state 0xB: the load screen (RW 0x816655 with the type 0x10); a socket
//     error (+ 0x6BA) -> GUI:SocketError / GUI:NetworkError and the lobby is left.
//   * the movie's functions (LanLobby.apt's root): StartLobby, DoCreateGame / DoJoinGame (RW 0x849677 / 0x8496C2, after TheLAN's OnGameCreate / OnGameJoin),
//     CancelGame (RW 0x847AB2), Enable / DisableCreateGame, Enable / DisableJoinGame, Enable / DisablePlayGame, Enable / DisableHostCancel ...
// OpenBFME DIFFERENCES / NOT PORTED (stop S-724, narrowed): the list keeps its selected game across refills (by the host's address); the list's columns (GameName, Players, Map, Ping, Status: the movie's
// headers; RotWK's sorter RW 0x988BB5 / 0x8483E4 is not read) are filled here as name, "players / slots", the map, "" and the progress; the scenario
// description, OnLoadGameBttn, OnOptionsBttn, the start countdown (GAME_START_TIMER) and the message boxes (reported as notes) are not ported.

#pragma once

#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameNetwork/Transport.h"

#include <memory>

class AptMessageBox;

class LANAPI;

class AptLanLobby : public AptSkirmish
{
public:
	AptLanLobby(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptLanLobby() override;
	// lane UI-1: the message box (GuiFX.apt's MessageBox clip, AptMessageBox.h), null until the lobby needs one
	AptMessageBox *messageBox() const { return m_box.get(); }
	// lane UI-1: the start countdown (MpGameSetup + 0x2C4 / + 0x2C5 / + 0x2C8 / + 0x2CC)
	bool countingDown() const { return m_counting; }
	bool startingBoxShown() const { return m_startShown; }

	int state() const { return m_state; }
	GameWindow *gamesList()
	{
		pruneLan();
		return m_gamesList;
	}
	// the rows of the games list (tests): "name|players|map|status"
	const std::vector<std::string> &gameRows() const { return m_gameRows; }
	const std::vector<std::string> &chatLines() const { return m_chat; }

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;
	void lobbyChanged() override;
	bool lobbyEditable(int slot) const override;
	void onReadyPress(const std::string &argument) override;
	void onKickPlayer(const std::string &argument) override;
	void onChatSend(const std::u16string &text) override;
	bool showChat() const override;

private:
	void update();
	void pumpLan();
	void fillGamesList();
	void enterSetup(bool host);
	void backToLobby();
	void pruneLan();
	void requestJoinSelected();
	// lane UI-1
	AptMessageBox &box();
	std::u16string text(const std::string &label) const;
	void showError(const std::string &title, const std::string &text);
	void systemChat(const std::u16string &line);
	bool armCountdown();
	int countdownTick();

	LANAPI *m_lan = nullptr;
	int m_state = 0;            // + 0x6A4
	bool m_initialized = false; // AptLanLobby::OnInitialized came
	GameWindow *m_gamesList = nullptr;   // + 0x6A8
	GameWindow *m_nameEntry = nullptr;
	std::vector<GameWindow *> m_listCandidates, m_nameCandidates; ///< every window InitGadgets gave for the two names (pruneLan)
	bool m_listDirty = true;
	std::vector<std::string> m_gameRows;
	std::vector<NetAddress> m_listedHosts; ///< the host of each row of the list
	std::vector<std::string> m_chat;
	bool m_ready = false;
	// lane UI-1
	std::unique_ptr<AptMessageBox> m_box;
	bool m_counting = false;      // + 0x2C4
	bool m_startShown = false;    // + 0x2C5: the QM:STARTINGGAME box is up
	std::uint64_t m_deadline = 0; // + 0x2C8 (timeGetTime)
	int m_lastSeconds = 0;        // + 0x2CC
	int m_minPlayers = 0;         // + 0x3A8
};
