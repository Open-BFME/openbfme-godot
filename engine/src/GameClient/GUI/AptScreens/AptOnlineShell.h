// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// OnlineShell.apt (CodePrefix AptOnline, lane ONLINE-4, docs/ONLINE.md section 5 / phase O2): RotWK's online screens on the OpenBFME lobby service
// (GameNetwork/OnlineLobbyClient.h) in the place of GameSpy. The shell movie loads its pages (OnlineLogin.swf, OnlineHome.swf, OnlineOpenPlay.swf ...) and
// tells the engine through AptOnline::ShellLoadScreen / ShellUnloadScreen; the engine makes a page object for each that registers the page's own callbacks
// (retail's sub-screens). OnlineOpenPlay hosts MpGameSetup.swf for the staging room, so this screen reuses AptSkirmish's MpGameSetup handling with the
// online game as its model, as AptLanLobby does for the LAN.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the opener RW 0x91D7A3 (BFME2 0x516FA9, tier A): unless a shell exists ([0xDEA36C]) it calls RW 0x9CCC06 (GameSpy's setup), pushes OnlineShell.apt
//     on TheShell (RW 0x75E3B7) and RW 0x6230B6(1) on [0xDE3F0C];
//   * the shell's registration (RW 0x91E133, BFME2 0x51795C tier A; decomp Rva005173F8Dtor.cpp): commands AptOnline::OnInitialized (RW 0x9F3A3C, empty),
//     ShellLoadScreen (RW 0x91DEFB), ShellUnloadScreen (RW 0x91D995), Options (RW 0x91D6BA: the options screen (1, 0, 1, 0)), ShellExit (RW 0x91D6CD);
//     the extern providers OnlineShellStartScreen (0: the current page's name, read only; it starts as "OnlineLogin") and OnlineAdvMode (1: GameSpy's
//     misc preference InAdvMode), both RW 0x91D9F9; the text APT:OnlineOrNetwork = APT:Online;
//   * ShellLoadScreen (BFME2 0x517724, decomp AptOnlineShellScreens.cpp): the current page is deactivated, the page of that name is made from the table
//     (OnlineLogin, OnlineHome, OnlineOpenPlay, OnlineStrategic, OnlineQuickMatch, Stats; a page already up is not made twice), kept and made current;
//     ShellUnloadScreen deletes the page of that name;
//   * the update (BFME2 0x51805B, decomp AptOnlineShellUpdate.cpp): every page updates; a page asked for (AptOnlineShell::LoadChildScreen, BFME2 0x517027)
//     goes to the movie's LoadScreen(name);
//   * OnlineLogin (RW 0x97C819, BFME2 0x572885 tier A): AptOnline::Login::Login (RW 0x97C5C6), DeleteNickname, Register, ServiceTerms, AcceptLocale,
//     CancelLogin (RW 0x977B0F), OfficialSite, GameSpy and the screen reference AptOnlineLogin::InitGadgets (RW 0x97A413: OnlineLogin::Password (16
//     characters), ::Nickname (a combo, 15 characters), ::Email (a combo), ::RememberInfo, ::CountryList, ::TOSText); the login disables the movie's
//     buttons through the shell's CallChild (DisableButtonDeleteNickname / Create / Login / ServiceTerms); a missing field is the box GUI:GSErrorTitle /
//     GUI:GSNoLoginInfo*; a login that succeeded loads OnlineHome (BFME2 0x5700A0);
//   * OnlineHome (RW 0x9C8F23, BFME2 0x5B9BBC): AptOnline::OnlineHome::OfficialSite, OnOpened (RW 0x9C8ED2), the screen reference AptOnlineHome::InitGadgets
//     (RW 0x9C8EDA: OnlineHome::MessageOfTheDay, a list box, filled with the message of the day) and the provider OnlineHome::NumTickerFields (RW
//     0x9C8350: 12); its constructor asks GameSpy for the message of the day (peer request 0x17);
//   * OnlineOpenPlay = AptOnlineCustomMatch (RW 0x9AF431, BFME2 0x5A5BC7; decomp AptOnlineCustomMatchCtor.cpp): AptOnline::CustomMatch::CancelPopUpJoin
//     (RW 0x9A8483: state 10 -> ClosePassword, 1), CreateGame (RW 0x9A8423: state 2 or 3 -> the GameName / GamePassword entries disabled, 4), JoinGame
//     (RW 0x9A845E: state 2 -> 9), LoadGame, AcceptJoinGame (RW 0x9AEE9C: state 10: an empty password -> GUI:JoinFailedDefault / GUI:JoinFailedBadPassword),
//     PlayGame (RW 0x9A8416: state 7), Cancel, Refresh (RW 0x9AA76A), AptOnline::OnOpenCreateDialog (RW 0x9AA774: state 3), CancelPopUpCreate (RW 0x9A84C9:
//     state 1), CancelPopUpHost (RW 0x9A84DD: ClosePassword, 1), OnOpenConnectionsScreen / OnClosingConnectionsScreen, the screen reference
//     AptOnlineCustomMatch::InitGadgets (RW 0x9AA4C3: GameList (+0x48C), Lobbies (+0x490), GameInfo (+0x494), GameName (+0x498, 20 characters),
//     GamePassword (+0x49C, 20 characters)); the game list's seven columns {4, 5, 5, 36, 33, 10, 7}: user map, settings, lock, name, map, players "%d/%d",
//     ping (BFME2 0x59F496); GameSorter::OnSortName / OnSortMap / OnSortPlayers / OnSortPing / OnSortStatus (RW 0x98821C ..); a join (BFME2 0x5A57BA)
//     with nothing selected is GUI:Error / GUI:NoGameSelected, a full game GUI:JoinFailedDefault / GUI:JoinFailedRoomFull, a password game EnterPassword
//     and state 10; the staging room is entered with the movie's GotoAndPlay("_host") (state 5 -> 6) or ClosePassword + GotoAndPlay("_join") (11 -> 12)
//     (BFME2 0x5A20A7); the connecting pop-up is PopUpConnectingOpen / PopUpConnectingClose (BFME2 0x59F37C).
// OpenBFME DIFFERENCES (docs/ONLINE.md section 5; stops S-4070 .. S-4079):
//   * sign-in is the lobby's guest sign-in: the nickname combo is the guest name (remembered by the host program), the e-mail and password fields are
//     disabled, Register / Create / Delete nickname / ServiceTerms are disabled through the movie's own DisableButton* functions, and the locale pop-up,
//     GameSpy and OfficialSite buttons report "not available" (their EA / GameSpy sites are gone) [S-4071];
//   * the message of the day is the lobby's /v1/motd; a lobby without one shows "The lobby has no message of the day." [S-4072];
//   * the game list is the lobby's: no ping (column 7 empty), no settings / user-map icons; the lobby rooms (Lobbies) are not used [S-4073];
//   * the staging room's state machine beyond the facts above (which state the update moves from, the GameSpy NAT negotiation, the countdown) is
//     OpenBFME's: the host's Play Game sends the lobby's START; a lobby without a relay refuses it (no_relay), which the player sees as a message box
//     [S-4074];
//   * Quick Match, Strategic (War of the Ring online), Profile / Stats and the Messenger are not ported: their pages report "not available" [S-4075].

#pragma once

#include "GameClient/GUI/AptScreens/AptSkirmish.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class AptMessageBox;
class OnlineLobbyClient;

// What the online screens need from the host program (ShellEnvironment::online)
struct OnlineShellContext
{
	OnlineLobbyClient *client = nullptr;              ///< the lobby client (the host sends its HTTP requests)
	std::u16string rememberedName;                    ///< the guest name the player signed in with last
	std::function<void(const std::u16string &)> rememberName; ///< keeps the name for the next session (null: not kept, reported)
	// lane ONLINE-4: the lobby session kept from an earlier run (ONLINE-3's user://online_session.txt) and where a new one is kept: a game that restarted
	// signs in as the same player and can rejoin its running game
	std::string sessionToken;
	std::function<void(const std::string &)> rememberSession;
	std::string lobbyUrl;                             ///< shown in the reports only
};

class AptOnlineShell : public AptSkirmish
{
public:
	AptOnlineShell(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptOnlineShell() override;

	// the lane's acceptance stops, noted ("online-stops") when the screen is made: one line per stop, "[S-40xx] ..."
	static const std::vector<std::string> &stopLines();

	// ---- state (tests, the host) ----
	const std::string &currentPage() const { return m_currentName; } ///< OnlineShellStartScreen
	std::vector<std::string> loadedPages() const;
	int openPlayState() const { return m_state; }  ///< the custom match screen's state (+0x488)
	AptMessageBox *messageBox() const { return m_box.get(); }
	// the rows of the game list (tests): "name|map|players|lock"
	const std::vector<std::string> &gameRows() const { return m_gameRows; }
	const std::vector<std::string> &gameIds() const { return m_rowIds; }
	const std::vector<std::string> &chatLines() const { return m_chat; }
	const std::vector<std::string> &motdLines() const { return m_motdLines; }
	GameWindow *gameList();
	GameWindow *nicknameWindow();
	GameWindow *gameNameWindow();
	GameWindow *gamePasswordWindow();
	bool connectingShown() const { return m_connecting; }
	// the boxes shown (title|text, as the game text gave them), for the tests
	const std::vector<std::string> &boxesShown() const { return m_boxes; }
	// Sol r1: the host program could not play the START the screens handed on (GodotAptPlayer.online_start_failed: the relay socket would not open):
	// the player is told in a box and is back at the game list (a recoverable lobby error, never the game's end)
	void startFailed(const std::string &why);

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;
	void lobbyChanged() override;
	bool lobbyEditable(int slot) const override;
	void onReadyPress(const std::string &argument) override;
	bool applySlotHero(int slot, const CreateAHeroHero *hero) override;
	void onKickPlayer(const std::string &argument) override;
	void onChatSend(const std::u16string &text) override;
	bool showChat() const override;
	void ruleNotice(const std::u16string &line) override;
	void showRuleWarning(const std::u16string &title, const std::u16string &text) override;

private:
	enum class Page
	{
		None,
		Login,
		Home,
		OpenPlay
	};
	struct PageNames
	{
		std::vector<std::string> commands, providers, screenRefs;
	};
	// the page objects of retail are the registrations of their names (removed on ShellUnloadScreen)
	void loadPage(const std::string &name);
	void unloadPage(const std::string &name);
	void pageCommand(const std::string &page, const std::string &name, std::function<void(const std::string &)> fn);
	void pageProvider(const std::string &page, const std::string &name, WindowManager::ProviderFn fn);
	void pageScreenRef(const std::string &page, const std::string &name, WindowManager::ScreenRefFn fn);
	void registerLogin();
	void registerHome();
	void registerOpenPlay();
	void notAvailable(const std::string &what, const std::string &stop);

	void update();
	void pumpClient();
	void callChild(const std::string &function, const std::vector<std::string> &args = {}); ///< the shell's CallChild (the login page's calls)
	void callPage(const std::string &function, const std::vector<std::string> &args = {});  ///< a function of the OnlineOpenPlay page
	void loadChildScreen(const std::string &name) { m_pendingScreen = name; } ///< AptOnlineShell::LoadChildScreen (BFME2 0x517027)
	AptMessageBox &box();
	std::u16string text(const std::string &label) const;
	void showBox(const std::u16string &title, const std::u16string &body, const std::string &record);
	void showError(const std::string &titleLabel, const std::string &textLabel);
	void prune();
	void candidate(GameWindow *&role, GameWindow *w);

	// login
	void login();
	// home
	void fillMotd();
	// open play
	void fillGamesList();
	void fillCreateEntries();
	void fillJoinEntries();
	void sortBy(int column);
	void joinSelected();
	void requestJoin(const std::string &id, const std::string &password);
	void startRunning(const std::string &id, bool rejoin); ///< lane ONLINE-4: a rejoin (own game) or spectator START for a running game
	void offerRejoin();
	void enterSetup(bool host);
	void backToList();
	void startPressed();
	void systemChat(const std::u16string &line);
	void syncModel();
	bool startUsable(const std::string &kind, std::string *why) const; ///< Sol r1: the START OnlineStart would read (checked before it is handed on)
	std::u16string mapTitle(const std::string &key);                  ///< BFME2 0x59F496: the map's display name in the game list

	OnlineShellContext *m_ctx = nullptr;
	OnlineLobbyClient *m_client = nullptr;
	std::string m_currentName = "OnlineLogin"; // + 0x294
	std::string m_pendingScreen;               // + 0x28C
	std::map<std::string, PageNames> m_pages;  // + 0x280 (the loaded pages)
	std::string m_advMode = "0";
	std::unique_ptr<AptMessageBox> m_box;
	std::vector<std::string> m_boxes;
	// login
	GameWindow *m_nickname = nullptr, *m_email = nullptr, *m_password = nullptr, *m_remember = nullptr;
	bool m_loginSent = false;
	int m_loginDisablesDue = 0; ///< updates until the account buttons are disabled (0: done or not due)
	// home
	GameWindow *m_motd = nullptr;
	std::vector<std::string> m_motdLines;
	std::u16string m_motdError; ///< the lobby's message of the day was refused (why)
	// open play (AptOnlineCustomMatch)
	int m_state = 0;                 // + 0x488
	GameWindow *m_gameList = nullptr; // + 0x48C
	GameWindow *m_lobbies = nullptr;  // + 0x490
	GameWindow *m_gameInfo = nullptr; // + 0x494
	GameWindow *m_gameName = nullptr; // + 0x498
	GameWindow *m_gamePassword = nullptr; // + 0x49C
	std::map<GameWindow **, std::vector<GameWindow *>> m_candidates; ///< every window InitGadgets gave a role (prune picks the shown, live one)
	bool m_popUp = false;             // + 0x4A0
	std::string m_joinId;             // + 0x4B8: the game to join
	std::vector<std::string> m_gameRows, m_rowIds;
	std::vector<std::string> m_rowKinds; ///< lane ONLINE-4: "join", "rejoin" (the player's running game) or "watch" (another running game)
	bool m_rejoinOffered = false, m_rejoinPending = false;
	std::u16string m_loginName;
	int m_sortColumn = -1;            ///< GameSorter: -1 the lobby's order (newest first), else 0 name 1 map 2 players 3 ping 4 status
	bool m_sortDescending = false;
	bool m_listDirty = true;
	std::vector<std::string> m_chat;
	bool m_ready = false;
	std::uint32_t m_syncedSeq = 0xFFFFFFFFu; ///< the lobby view the staging room's gadgets show
	bool m_connecting = false;
	bool m_startSent = false;
	bool m_left = false;
	bool m_pollTrouble = false;             ///< Sol r1: the staging room's poll failed (said once in the chat, and again when it is back)
	std::set<std::string> m_unknownMaps;    ///< listed maps the player does not have (noted once each)
};
