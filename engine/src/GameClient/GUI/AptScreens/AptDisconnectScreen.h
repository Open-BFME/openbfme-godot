// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// DisconnectScreen.apt (CodePrefix AptDisconnectScreen, lane MP-2): the "waiting for players" screen of a network game. The disconnect path
// (GameNetwork/DisconnectManager.h) decides what it shows; this class puts it into the movie and hands the buttons back.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the menu object TheDisconnectMenu = RW 0xDEA328, constructor / registration RW 0x91988F):
//   * commands: DisconnectScreen::InitGadgets (RW 0x919529, the chat gadgets), AptDisconnectScreen::OnInitialized (RW 0x918FEE: + 0x284 = 1, so the next
//     update RW 0x9190C0 sends every row's Show / HideKickButton again), AptDisconnectScreen::Quit (RW 0x9195E1: the disconnect chat "Network:PlayerLeftGame",
//     a vote for every slot TheNetwork vslot 0x104 does not exclude, then TheNetwork vslot 0x94 = quit), AptDisconnectScreen::Kick (RW 0x919064: atoi of the
//     argument is a row; RW 0x8D7D90 turns it into the slot with the local slot (TheNetwork vslot 0xB8); TheNetwork vslot 0x98 = voteForPlayerDisconnect),
//     AptDisconnectScreen::Chat::OnBttnEnterText (RW 0x9196B9);
//   * the providers DisconnectScreen:PlayerColor:0 .. 7 (RW 0x9190A6: "0xFFFFFFFF" whatever the row);
//   * show RW 0x9190F0 (the window manager loads DisconnectScreen.apt, vslot 0x80) / hide RW 0x918F97;
//   * setPlayerName RW 0x9191EC: the text "DisconnectScreen::PlayerName%d" of the row (an empty name is L" ", RW 0xBD16E4), the row's kick button
//     hidden, "DisconnectScreen::VotesReceived%d" = L" "; showPlayerVotes RW 0x91949D: VotesReceived%d = L"%d" of the count (RW 0xBDF1B0), L" " for 0; setPlayerTimeoutTime RW 0x91934F: the movie's SetBarPercent(row,
//     percent) (both arguments "%d"); RW 0x918FF8: ShowKickButton(row) / HideKickButton(row), remembered at + 0x286[row].
// NOT PORTED (stop S-1123): the chat (InitGadgets, Chat::OnBttnEnterText, the disconnect chat of Quit), the in-game UI calls of show (RW 0x9190F0 closes
// the other menus and calls TheInGameUI vslot 0x178), the movie's level (the first free `_level`, as GuiFX's).

#pragma once

#include "GameClient/GUI/AptScreen.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

class AptDisconnectScreen : public AptScreen
{
public:
	// one row of the screen (the other slots in slot order, the local one skipped: DisconnectManager::translatedSlotPosition)
	struct Row
	{
		bool used = false;
		std::string nameUtf8;
		int votes = 0;
		int barPercent = 100;
		bool kickShown = false;
	};
	AptDisconnectScreen(WindowManager &windows, Shell &shell);

	// the host's answers to the buttons: Kick of a row, Quit
	std::function<void(int row)> onKick;
	std::function<void()> onQuit;

	// puts the rows into the movie: names and votes as texts, the bars and kick buttons through the movie's functions; only what changed is sent (the
	// first call after the movie loaded sends everything). False while the movie is not loaded yet (call again on the next frame)
	bool apply(const std::array<Row, 7> &rows);

	// what the movie was told (tests): per row the last SetBarPercent and kick state, and every call in order
	const std::vector<std::string> &calls() const { return m_calls; }
	int kicks() const { return m_kicks; }
	int quits() const { return m_quits; }

private:
	void setPlayerName(int row, const std::string &name);
	void showVotes(int row, int votes);
	void setBar(int row, int percent);
	void showKick(int row, bool shown);

	std::array<Row, 7> m_sent{};
	std::array<bool, 8> m_kickShown{}; // + 0x286
	bool m_initialized = false;        // + 0x284
	bool m_first = true;
	std::vector<std::string> m_calls;
	int m_kicks = 0, m_quits = 0;
};
