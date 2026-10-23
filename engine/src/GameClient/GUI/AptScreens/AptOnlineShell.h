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
//     OfficialSite reports "not available" [S-4071]; the former GameSpy button opens OpenBFME's website;
//   * the message of the day is the lobby's /v1/motd; a lobby without one shows "The lobby has no message of the day." [S-4072];
//   * the game list is the lobby's: no ping (column 7 empty), no settings / user-map icons; the lobby rooms (Lobbies) are not used [S-4073];
//   * the staging room's state machine beyond the facts above (which state the update moves from, the GameSpy NAT negotiation, the countdown) is
//     OpenBFME's: the host's Play Game sends the lobby's START; a lobby without a relay refuses it (no_relay), which the player sees as a message box
//     [S-4074];
//   * Strategic (War of the Ring online) is not ported: its page reports "not available" [S-4075]; lane ONLINEHUB-1 ported the TOURNAMENT
//     (OnlineQuickMatch: Quick Match and the tournaments) and STATS pages on the OpenBFME platform (GUI/AptScreens/AptOnlineHub.h); SOCIAL-1 (Devin,
//     landed by lane ONLINEMENU-1) the Messenger (GUI/AptScreens/AptMessenger.h)

#pragma once

#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h"
#include "Common/SimulationProfile.h"
#include "GameNetwork/RelayProbe.h" // lane ONLINEMENU-1: RelayProbe::Result kept (m_lastLink)

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class AptMessageBox;
class AptOnlineHub;
class AptMessenger;
class RelayProbe;
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
	// lane ONLINEUX-1: what the screens keep across the online shell's remaking (the shell is popped for a game and pushed again after it):
	std::string lastPage;   ///< the page shown last (OnlineShellStartScreen after a game: retail's shell, kept through the game, shows the page it was on)
	std::shared_ptr<std::set<std::string>> leftGames; ///< the started games this player left (finished, forfeited, quit): never offered for a rejoin
	std::u16string pendingNotice; ///< a message for the player the screens show once they are up (an online game that could not go on)
	int playersOnline = -1, gamesOpen = -1, gamesRunning = -1; ///< the lobby's counts (GET /v1/stats, the host asks): the home page's ticker; -1 unknown
	// lane ONLINEUX-1 r2 (Claude F1, Sol): the online game the screens started could not go on on this side (a rejoin or a watch that failed before it was
	// live; game.gd _online_game_failed through AptMenuPlayer.online_game_failed): `why` is shown once the screens are up, and the game is NOT left (the
	// client forgets it without a leave), so the lobby keeps offering it for a rejoin: the player never left it in the game
	void gameFailed(const std::u16string &why);
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
	int sortColumn() const { return m_sortColumn; } ///< lane MENUWORK-1: the game list's sort (the menu inventory's evidence)
	bool sortDescending() const { return m_sortDescending; }
	AptMessageBox *messageBox() const { return m_box.get(); }
	AptMessenger *messenger() const { return m_messenger.get(); }
	// lane ONLINEMENU-1 (owner, 2026-10-20: friends in the game's own Messenger, no OpenBFME page): a Yes / No question the host program asks in
	// GuiFX's own box (a friend's game invite: RotWK's APT:BuddyInviteTitle / APT:BuddyInviteTextOpenPlay). False while a box is up (ask again
	// later); the answer comes back once as "<key>|yes" or "<key>|no" (takeAnswers)
	bool askYesNo(const std::string &key, const std::u16string &title, const std::u16string &text);
	std::vector<std::string> takeAnswers();
	// Sol (delta review): whether the box still shows that question (false once answered, or when another box took its place: the room closed
	// under the player, a refusal); the host asks again or drops it, never acting on it
	bool asking(const std::string &key);
	bool createChallenge(const std::string &pid, const std::string &mode, const std::string &password);
	// the rows of the game list (tests): "name|map|players|lock"
	const std::vector<std::string> &gameRows() const { return m_gameRows; }
	const std::vector<std::string> &gameIds() const { return m_rowIds; }
	const std::vector<std::string> &chatLines() const { return m_chat; }
	const std::vector<std::string> &motdLines() const { return m_motdLines; }
	GameWindow *gameList();
	GameWindow *nicknameWindow();
	GameWindow *gameNameWindow();
	GameWindow *gamePasswordWindow();
	GameWindow *lobbiesWindow(); ///< lane ONLINEUX-1 (tests): the Lobby Room drop-down
	bool connectingShown() const { return m_connecting; }
	// lane MENUWORK-1 (S-4750): the login page's account parts the engine hides (Apt paths in OnlineShell.apt) and the line shown in the e-mail row's place
	static const std::vector<std::string> &loginAccountParts();
	static const char *const kNicknameLine;
	// lane MENUWORK-1: the box text of an online page OpenBFME does not have (empty: none of those)
	static std::u16string unavailablePageText(const std::string &page);
	static const float kNicknameFrameX, kNicknameFrameY, kNicknameFrameXScale;
	// the boxes shown (title|text, as the game text gave them), for the tests
	const std::vector<std::string> &boxesShown() const { return m_boxes; }
	// lane ONLINE-5: the labels of the host's refused starts (the start check's refusal: "GUI:NotifiedStartIntent" while a player is not ready)
	const std::vector<std::string> &startRefusals() const { return m_startRefusals; }
	// Sol r1: the host program could not play the START the screens handed on (GodotAptPlayer.online_start_failed: the relay socket would not open):
	// the player is told in a box and is back at the game list (a recoverable lobby error, never the game's end)
	void startFailed(const std::string &why);
	// lane ONLINEHUB-1: the TOURNAMENT (Quick Match, tournaments) and STATS pages (GUI/AptScreens/AptOnlineHub.h)
	AptOnlineHub *hub() const { return m_hub.get(); }
	// lane ONLINE-4: a rejoin (own game) or spectator START for a running game (the list's Join on a REJOIN / WATCH row, the sign-in's rejoin box; lane
	// UBERJOIN-1: public for the tests of a refused one)
	void startRunning(const std::string &id, bool rejoin);
	// lane ONLINEUX-1 (tests): whether the movie was last told to enable its Login button (-1: not told yet)
	int loginButtonState() const { return m_loginButton; }
	// lane ONLINEMENU-1 (tests): the Connections screen's cells as last drawn ("" not drawn), and whether it is open
	const std::string &connectionCell(int row, int column) const { return m_gridFrames[(size_t)(row * MAX_SLOTS + column)]; }
	bool connectionsOpen() const { return m_connectionsOpen; }
	const std::string &gridCaller() const { return m_gridCaller; }
	// lane ONLINEMENU-1 (owner, 2026-10-20: the grid "makes like no sense just have it show like ping to server, ping to other players etc"): the
	// Connections screen is covered by an OpenBFME page (OpenBFMEOptionsPage, as the FRIENDS page) that says it in words: the lobby server's
	// response time, the player's ping to the game relay and its loss, and each other player's ping to the player through the relay. NOT RETAIL. The
	// grid underneath still follows the links (drawConnectionGrid); the screen's own DONE closes both
	bool networkPageOpen() const { return m_networkPage && m_networkPage->shown(); }
	OpenBFMEOptionsPage *networkPage() const { return m_networkPage.get(); }
	// a room player's ping as the room shows it (the Players list's second column, the page): the player's own ping to the relay, another player's
	// ping to the local player (their two pings to the relay: what a message between them crosses); "" when the room has no relay or `slot` no player
	UnicodeString pingText(int slot, Color *color) const;
	// ---- lane ONLINEMENU-1: the FRIENDS page (owner, 2026-10-20: SOCIAL-1's friend features in the game's style). NOT RETAIL: RotWK's Messenger has no
	// place for a friend code, requests, challenges, reports, blocks or the stored chat; they are an OpenBFME page (OpenBFMEOptionsPage: RotWK's own
	// gadget skins, Options.apt's frames and lettering) over the online screens, opened from a FRIENDS button drawn beside the frame's MESSENGER.
	// Since the owner's call of 2026-10-20 ("looks out of place and bad") the friends features are RotWK's Messenger's and the host shows neither
	// the button nor the page (scripts/online_social.gd); the shell keeps them as a page any host program can show ----
	static constexpr int kFriendsButtonLeft = 596, kFriendsButtonTop = 650, kFriendsButtonRight = 716, kFriendsButtonBottom = 684;
	// the host shows the button (signed in, the social service there) with its label ("FRIENDS", "FRIENDS (2)"); empty label: no button
	void setFriendsButton(const UnicodeString &label) { m_friendsLabel = label; }
	bool openFriendsPage(const OpenBFMEPageSpec &spec, std::string *error); // shows (or replaces) the page
	void closeFriendsPage();
	bool friendsPageOpen() const { return m_friendsPage && m_friendsPage->shown(); }
	OpenBFMEOptionsPage *friendsPage() const { return m_friendsPage.get(); }
	// the page's events, then "friends_button" presses (kind Entry, key "friends_button") and Esc while the page is up (kind Cancel)
	std::vector<OpenBFMEPageEvent> takeFriendsEvents();
	bool friendsMouse(int kind, float x, float y); ///< the window manager's mouse filter while the shell is on top
	void takeDecorator();                           ///< the gadget layer's decorator: the TOURNAMENT panel, the FRIENDS button, the page
	void nicknameEdited() { applyLoginButton(false); } ///< the nickname set by the host program (a script): as a typed change
	// the home page's ticker rows the screens filled ("label|value"), in the movie's order
	const std::vector<std::string> &tickerRows() const { return m_tickerRows; }
	// the player's words for a lobby refusal or transport error (OpenBFME's: the lobby's own text is for the logs), broken into lines the box can show
	std::u16string playerMessage(const std::string &request, const std::string &code, const std::u16string &lobbyText) const;
	static std::u16string wrapLines(const std::u16string &text, size_t width);

protected:
	void runInit() override; // lane ONLINEUX-1: the shell shown again after a screen covered it (Options)
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;
	void lobbyChanged() override;
	bool lobbyEditable(int slot) const override;
	void onReadyPress(const std::string &argument) override;
	// lane ONLINEMENU-1: the lobby carries each player's Create-a-Hero record (was locked: ONLINE-5, S-4076), so the Hero combo works as the LAN one does
	// lane ONLINEMENU-1: the lobby carries each slot's handicap into the START (was locked: OWNERFIX-2, S-4552), so the combo works as the LAN one does
	int setupFlags() const override { return 0x200001F0; } // lane ONLINE-5: RW 0x9AF464 (BFME2 0x5A5BC7's 0x200001F0)
	bool applySlotHero(int slot, const CreateAHeroHero *hero) override;
	void onKickPlayer(const std::string &argument) override;
	void onChatSend(const std::u16string &text) override;
	UnicodeString chatPlayerNote(int slot, Color *color) const override { return pingText(slot, color); } // lane ONLINEMENU-1
	bool showChat() const override;
	void ruleNotice(const std::u16string &line) override;
	void showRuleWarning(const std::u16string &title, const std::u16string &text) override;

private:
	friend class AptOnlineHub; // lane ONLINEHUB-1: the hub's pages use the shell's box, lobby client and setup model
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
	void showNicknameOnly();
	void registerHome();
	void registerOpenPlay();
	void notAvailable(const std::string &what, const std::string &stop, const std::u16string &playerText = std::u16string());
	// lane ONLINEUX-1
	void applyLoginButton(bool force);         ///< RW 0x97A668 (BFME2 0x5706D4): Login enabled while the nickname has a printable character
	void navTo(const std::string &page);        ///< the shell's ButtonNav clip to the page's frame label (its highlighted button)
	void retryNav();                            ///< DEVINPR-1: navTo's label once the clip is there (at most kNavRetryFrames updates)
	void fillTicker();                          ///< RW 0x9C881D / 0x9C8694: the home page's ticker rows
	bool gameLeft(const std::string &id) const; ///< a started game this player left (ctx leftGames)
	void showSelectedGameMap();                 ///< the browser's MAP panel shows the selected game's map (the panel is read only there)
	// lane ONLINEMENU-1
	std::string connectionFrame(int row, int column) const; ///< BFME2 0x005DB512: a cell's frame
	void drawConnectionGrid();                              ///< BFME2 0x005DB647 RedrawGrid
	void updateRelayLink();                                 ///< probe the room's relay, report the link
	void updateNetworkPage();                               ///< shows / refreshes / closes the page over the Connections screen
	OpenBFMEPageSpec networkSpec() const;

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
	void offerRejoin();
	// lane UBERJOIN-1 (GUI/ProfileSwitch.h): true when `gameMode` is the player's own; else the Yes / No box asks and Yes switches, then `proceed` runs
	bool modeAllows(SimulationProfile gameMode, std::function<void()> proceed);
	bool switchMode(SimulationProfile mode, std::string *why);
	void syncMode();
	void restoreMode();
	void enterSetup(bool host);
	void backToList();
	void startPressed();
	void systemChat(const std::u16string &line);
	void syncModel();
	bool startUsable(const std::string &kind, std::string *why) const; ///< Sol r1: the START OnlineStart would read (checked before it is handed on)
	std::u16string mapTitle(const std::string &key);                  ///< BFME2 0x59F496: the map's display name in the game list

	OnlineShellContext *m_ctx = nullptr;
	OnlineLobbyClient *m_client = nullptr;
	std::unique_ptr<AptOnlineHub> m_hub; // lane ONLINEHUB-1
	std::string m_currentName = "OnlineLogin"; // + 0x294
	std::string m_pendingScreen;               // + 0x28C
	std::map<std::string, PageNames> m_pages;  // + 0x280 (the loaded pages)
	std::string m_advMode = "0";
	std::unique_ptr<AptMessageBox> m_box;
	std::unique_ptr<AptMessenger> m_messenger;
	std::string m_challengeTarget, m_challengeMode, m_challengePassword;
	bool m_socialJoined = false;
	std::vector<std::string> m_boxes;
	std::vector<std::string> m_startRefusals;
	std::vector<std::string> m_answers; ///< lane ONLINEMENU-1: askYesNo's answers not yet taken
	std::string m_askKey;               ///< the question the box shows ("" none)
	std::u16string m_askTitle, m_askText;
	// lane ONLINEMENU-1: the Connections screen on the relay (RelayProbe.h)
	bool m_connectionsOpen = false;
	int m_gridDue = 0;                      ///< updates until the grid is drawn after the screen opened
	std::string m_gridDrawn;                ///< what the grid shows now (redrawn when it changes)
	std::string m_gridPath = "?";           ///< where the movie's grid functions were found
	std::string m_gridCaller;               ///< the clip that called Connection::RedrawGrid (its target path)
	// lane ONLINEMENU-1: the FRIENDS page
	std::unique_ptr<OpenBFMEOptionsPage> m_friendsPage;
	std::vector<OpenBFMEPageEvent> m_friendsEvents;
	UnicodeString m_friendsLabel;
	bool m_friendsHot = false, m_friendsPressed = false;
	void decorateAll(GadgetDrawList &out);
	std::array<std::string, MAX_SLOTS * MAX_SLOTS> m_gridFrames; ///< each cell's frame as last drawn (tests)
	std::unique_ptr<RelayProbe> m_probe;
	std::uint64_t m_probeDueMs = 0;
	std::uint64_t m_probeDoneMs = 0;        ///< when the last measurement ended (0 none)
	std::string m_probeTarget;              ///< "room <relay>" / "lobby <relay>" being measured ("" none)
	RelayProbe::Result m_lastLink;          ///< Sol: the last completed measurement (shown while the next one runs)
	bool m_hasLastLink = false;
	std::string m_relayErrorNoted;          ///< the lobby's relay lookup failure last noted
	std::unique_ptr<OpenBFMEOptionsPage> m_networkPage;
	std::string m_networkRows;              ///< the page's row keys as shown (a change rebuilds it; values change in place)
	std::string m_linkReported;             ///< the band last reported to the lobby ("" none)
	// login
	GameWindow *m_nickname = nullptr, *m_email = nullptr, *m_password = nullptr, *m_remember = nullptr;
	bool m_loginSent = false;
	int m_loginHideUpdates = 0;                // lane MENUWORK-1: updates since the login page's clips were placed
	std::set<std::string> m_loginPartsMissing; // lane MENUWORK-1: the account parts already noted as missing
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
	std::vector<SimulationProfile> m_rowModes; ///< lane UBERJOIN-1: the performance mode of each row
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
	// lane ONLINEUX-1
	int m_loginButton = -1;                 ///< what the movie's Login button was last told (1 enabled, 0 disabled, -1 not yet)
	bool m_shownOnce = false;               ///< the shell's movie has been up (a later runInit is a return from a covering screen)
	std::string m_navDue;                   ///< DEVINPR-1: the ButtonNav frame label still to be played ("" none)
	int m_navTries = 0;
	static constexpr int kNavRetryFrames = 60; ///< DEVINPR-1: about a second of shell updates
	bool m_barDue = false;                  ///< the button bar is to be shown again on the next update (the movie's ShowButtonBar)
	std::string m_runningId;                ///< the running game a rejoin / watch was asked for
	std::vector<std::string> m_tickerRows;
	int m_tickerClipsDone = 0;              ///< the ticker rows' clips given their texts (fillTicker)
	std::string m_selectedMap;              ///< the map the browser's MAP panel shows for the selected game
	std::string m_ownMap;                   ///< the player's own map while the panel shows a selected game's
};
