// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AptOnlineHub (lane ONLINEHUB-1): the online shell's TOURNAMENT and STATS pages (AptOnlineShell owns it; the shell's ShellLoadScreen hands it the two
// pages). RotWK's TOURNAMENT button loads OnlineQuickMatch.swf (its title GUI:QuickMatchTitle reads "Tournament": retail's ranked automatch); STATS
// loads Stats.swf (GUI/AptScreens/AptStats.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp AptOnlineQuickMatch*.cpp, tier A):
//   * the page's constructor RW 0x9C9F30 (BFME2 0x5BAB7E): the commands AptOnline::OnlineQuickMatch::PlayGame (BFME2 0x5BA34B: the simple flag set,
//     state 1 = searching), ::Cancel (RW 0x9C9E55, BFME2 0x5BAAA3: GameSpy request 0x12, state 0, the owner told), ::WidenSearch (RW 0x9C9DDD, BFME2
//     0x5BAA2B: request 0x11), ::StartSimple (BFME2 0x5BA344: the simple flag), ::OnFoundMovieDone (BFME2 0x5BA359: state 2) and the screen reference
//     AptOnlineQuickMatch::InitGadgets (BFME2 0x5BA94A): OnlineQuickMatch::Color (the colour list, BFME2 0x5BA803: AptWhiteBox entries tinted by
//     the multiplayer colours), ::NumOfPlayers, ::Side, ::ConnectionSpeed, ::Ladder (each combo emptied and kept);
//   * the keyboard (BFME2 0x5BAECA): Escape while searching calls the movie's CallChild("EscapeKeyPressed") and Cancel;
//   * the peer messages (BFME2 AptOnlineQuickMatchOptions.cpp): a match found calls CallChild("DoOpenFound") then the match handler (BFME2
//     0x5BAF35: every player's slot from the matchmaker's answer: colour, faction, team i / players-per-team); a failure CallChild("CloseFoundAndReset");
//     status lines QM:WORKING / QM:POOLSIZE;
//   * the movie (OnlineQuickMatch.apt): JOIN GAME opens the Searching panel (DoOpenSearch: components disabled), its Cancel / WidenSearch buttons call
//     GameCode with their names; HandleGameCall takes DoOpenFound, CloseFoundAndReset, movieQuickMatchFound, EscapeKeyPressed.
// OpenBFME DIFFERENCES (stops S-4870 .. S-4873, S-4877):
//   * the matchmaker is the OpenBFME platform's (GameNetwork/PlatformApiClient.h: a ticket, polled; same mode and size, oldest first): the Ladder combo
//     chooses the size (1 vs 1, 2 vs 2), Army the faction (or random), Color the colour; the mode is the player's simulation profile (Options' Uber
//     performance mode); ConnectionSpeed is shown disabled (the platform matches no connection speed) and Widen Search says the search is as wide as it
//     gets [S-4870];
//   * a match found: the match's host creates a private lobby room named by the match's room code (OnlineLobbyClient::createGame with a room code, on
//     one of the official maps for that many players, picked by the match id), the others join it by the code, everyone takes the match's team and
//     their own army / colour, says ready, and the host starts it: the lobby's START, as any online game [S-4871];
//   * the tournaments (no RotWK screen: retail's Tournament was the ranked automatch above) are a panel of the game's own list box skin
//     (window\apt\listbox.wnd) in the page's empty right half, framed and lettered as the OpenBFME options page (OPTS-3): the tournaments (upcoming,
//     running, finished), the chosen one's rules and bracket; a double click on a tournament asks in GuiFX's message box to register, withdraw or join
//     "your next match" (the match's room, as a Quick Match's: the first player listed hosts) [S-4873];
//   * the platform unreachable, a search that times out or is cancelled, a match whose room does not come together in 90 seconds: the player is told in
//     a box or the panel's status line and the page comes back; nothing waits forever [S-4877].

#pragma once

#include "GameClient/GUI/AptScreens/AptStats.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/PlatformApiClient.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AptOnlineShell;

class AptOnlineHub
{
public:
	// lane ONLINEMENU-1: the shell's decorator draws the TOURNAMENT panel through these (the shell owns the layer's decorator)
	void drawPanel(GadgetDrawList &out) const { decorate(out); }
	bool decorating() const { return m_qmUp; } ///< the TOURNAMENT page's panel is up
	static constexpr std::uint64_t kRoomTimeoutMs = 90 * 1000; ///< a matched room must start within this long
	static constexpr std::uint64_t kJoinRetryMs = 2000;        ///< a joiner asks for the room again while the host has not made it
	// the tournaments panel on the page's 1024 x 768 stage (the empty right half of OnlineQuickMatch.swf: its left frame ends at x 528)
	static constexpr int kPanelLeft = 548, kPanelRight = 988, kPanelTop = 296, kPanelBottom = 640;

	explicit AptOnlineHub(AptOnlineShell &shell);
	~AptOnlineHub();
	AptOnlineHub(const AptOnlineHub &) = delete;
	AptOnlineHub &operator=(const AptOnlineHub &) = delete;

	static bool isHubPage(const std::string &name) { return name == "OnlineQuickMatch" || name == "Stats"; }
	void loadPage(const std::string &name);
	void unloadPage(const std::string &name);
	void update();
	// a lobby event while a Quick Match / tournament room is being made or played: true when it was the hub's (the shell must not act on it)
	bool lobbyEvent(const OnlineLobbyClient::Event &e);
	bool gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2);
	// a game was recorded (the host): the Stats page shows it
	void recordsChanged();

	// ---- state (tests, the host's online_status) ----
	AptStats *stats() const { return m_stats.get(); }
	bool quickMatchUp() const { return m_qmUp; }
	const std::string &status() const { return m_status; } ///< the panel's status line (the search, the room)
	std::string roomPhase() const;                         ///< "none", "creating", "joining", "in_room", "starting", "failed"
	const std::string &matchId() const { return m_room.matchId; }
	const std::string &tournamentId() const { return m_room.tournamentId; }
	bool roomHost() const { return m_room.host; }
	bool roomCheckedIn() const { return m_room.checkedIn; }
	std::vector<std::string> tournamentRows() const;       ///< "name|when|players"
	const std::vector<std::string> &detailRows() const { return m_detailRows; }
	int selectedTournament() const { return m_selected; }
	GameWindow *tournamentList() const { return m_list; }
	GameWindow *detailList() const { return m_details; }
	GameWindow *sideCombo() const { return m_side; }
	GameWindow *sizeCombo() const { return m_ladder ? m_ladder : m_numPlayers; }
	GameWindow *colorCombo() const { return m_color; }
	// the choices the page would search with now (tests): "size|faction template or random|colour index"
	std::string choices() const;
	// automation (tests, the proof walk): a tournament row chosen as a click chooses it; its action as a double click asks for it
	void chooseTournament(int row);
	void tournamentAction(int row);
	// the clock (tests: a fake one; else NetMilliseconds)
	void setClock(std::function<std::uint64_t()> clock) { m_clock = std::move(clock); }
	std::int64_t unixNow() const;
	void setUnixClock(std::function<std::int64_t()> clock) { m_unixClock = std::move(clock); }

private:
	struct Room
	{
		enum class Phase
		{
			None,
			Creating,
			Joining,
			InRoom,
			Starting,
			Failed
		} phase = Phase::None;
		std::string code, matchId, tournamentId;
		bool host = false;
		int team = -1;
		int expected = 0;  ///< the human players the room needs
		int faction = -1;  ///< the player's own choice (a PlayerTemplate index, -1 random)
		int color = -1;
		std::uint64_t deadline = 0, nextJoin = 0;
		std::uint32_t readySeq = 0;
		bool readySent = false, slotSent = false, startSent = false;
		// a tournament match: the player is checked in at the platform once in the room (the room screen); nobody readies before that
		bool checkedIn = false;
		int checkInTries = 0;
		std::uint64_t checkInRetryAt = 0;
	};
	void registerQuickMatch();
	void unregisterQuickMatch();
	void initGadget(const std::string &name, GameWindow *w);
	void startSearch();
	void cancelSearch(bool byPlayer);
	void pumpPlatform();
	void searchEnded(const std::string &why);
	void matched();
	void startRoom(const std::string &code, const std::string &matchId, const std::string &tournamentId, bool host, int team, int expected);
	void roomUpdate();
	void roomFailed(const std::string &why);
	void sendCheckIn();
	void askWithdraw(const PlatformApiClient::Tournament &t);
	std::string myNick() const;
	bool pickMap(const std::string &matchId, int players, std::string &map) const;
	void showPanel(bool show);
	void fillTournaments();
	void fillDetails();
	void decorate(GadgetDrawList &out) const;
	bool panelHidden() const; ///< a search or a match's room is under way: the movie's pop-up has the page's middle
	void callChild(const std::string &function);
	std::u16string mapDisplay(const std::string &key) const;
	std::uint64_t now() const;

	AptOnlineShell &m_shell;
	std::vector<std::string> m_commands, m_screenRefs;
	bool m_qmUp = false;
	std::unique_ptr<AptStats> m_stats;
	// the Quick Match page's gadgets (InitGadgets)
	GameWindow *m_color = nullptr, *m_numPlayers = nullptr, *m_side = nullptr, *m_speed = nullptr, *m_ladder = nullptr;
	int m_qmState = 0;        ///< retail + 0x60: 0 idle, 1 searching, 2 the found movie done
	bool m_simple = false;    ///< retail + 0x79
	bool m_foundShown = false;
	std::string m_status;
	Room m_room;
	// the tournaments panel
	GameWindow *m_list = nullptr, *m_details = nullptr;
	int m_selected = -1;
	std::string m_selectedId;
	std::vector<std::string> m_detailRows;
	std::string m_panelProblem;
	bool m_listAsked = false;
	std::function<std::uint64_t()> m_clock;
	std::function<std::int64_t()> m_unixClock;
};
