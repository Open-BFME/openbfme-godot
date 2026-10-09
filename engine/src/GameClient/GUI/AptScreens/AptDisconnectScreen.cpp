// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptDisconnectScreen.h (lane MP-2).

#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"

#include <cstdlib>
#include <string>

AptDisconnectScreen::AptDisconnectScreen(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "DisconnectScreen.apt", "AptDisconnectScreen")
{
	registerScreenRef("DisconnectScreen::InitGadgets", [this](const std::string &name, GameWindow *) {
		this->windows().note("unported-command", "DisconnectScreen::InitGadgets (" + name + "): the chat gadgets are not ported [S-1123]");
	});
	registerCommand("AptDisconnectScreen::OnInitialized", [this](const std::string &) {
		m_initialized = true; // RW 0x918FEE
	});
	registerCommand("AptDisconnectScreen::Quit", [this](const std::string &) {
		++m_quits;
		if (onQuit)
		{
			onQuit(); // RW 0x9195E1 (the disconnect chat is not ported: S-1123)
		}
	});
	registerCommand("AptDisconnectScreen::Kick", [this](const std::string &argument) {
		// RW 0x919064: an empty argument does nothing; atoi of it is the row
		if (argument.empty())
		{
			return;
		}
		++m_kicks;
		if (onKick)
		{
			onKick(std::atoi(argument.c_str()));
		}
	});
	registerCommand("AptDisconnectScreen::Chat::OnBttnEnterText", unportedCommand("AptDisconnectScreen::Chat::OnBttnEnterText"));
	for (int i = 0; i < 8; ++i)
	{
		// RW 0x9190A6: every colour answers "0xFFFFFFFF"
		registerProvider("DisconnectScreen:PlayerColor:" + std::to_string(i), [](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value = "0xFFFFFFFF";
			}
			return true;
		});
	}
	// RW 0x91988F: every row's name starts empty (setPlayerName with the empty string: the texts become RW 0xBD16E4's L" ")
	for (int i = 0; i < 8; ++i)
	{
		windows.setAptText("DisconnectScreen::PlayerName" + std::to_string(i), " ");
		windows.setAptText("DisconnectScreen::VotesReceived" + std::to_string(i), " ");
	}
}

void AptDisconnectScreen::setPlayerName(int row, const std::string &name)
{
	// RW 0x9191EC: an empty name is RW 0xBD16E4's L" " (a single space: the field shows nothing instead of its label's game text)
	windows().setAptText("DisconnectScreen::PlayerName" + std::to_string(row), name.empty() ? std::string(" ") : name);
	showKick(row, false);
	windows().setAptText("DisconnectScreen::VotesReceived" + std::to_string(row), " ");
	m_calls.push_back("PlayerName" + std::to_string(row) + " " + name);
}

void AptDisconnectScreen::showVotes(int row, int votes)
{
	// RW 0x91949D: L" " (RW 0xBD16E4) for no vote, else L"%d" (RW 0xBDF1B0) of the count
	windows().setAptText("DisconnectScreen::VotesReceived" + std::to_string(row), votes ? std::to_string(votes) : std::string(" "));
	m_calls.push_back("VotesReceived" + std::to_string(row) + " " + std::to_string(votes));
}

void AptDisconnectScreen::setBar(int row, int percent)
{
	// RW 0x91934F: SetBarPercent("%d" row, "%d" percent)
	std::string error;
	if (!windows().invokeAS(level(), "SetBarPercent", { std::to_string(row), std::to_string(percent) }, nullptr, &error))
	{
		windows().note("invoke-failed", "DisconnectScreen SetBarPercent: " + error);
	}
	m_calls.push_back("SetBarPercent " + std::to_string(row) + " " + std::to_string(percent));
}

void AptDisconnectScreen::showKick(int row, bool shown)
{
	// RW 0x918FF8
	std::string error;
	if (!windows().invokeAS(level(), shown ? "ShowKickButton" : "HideKickButton", { std::to_string(row) }, nullptr, &error))
	{
		windows().note("invoke-failed", std::string("DisconnectScreen ") + (shown ? "ShowKickButton: " : "HideKickButton: ") + error);
	}
	m_kickShown[(size_t)row] = shown;
	m_calls.push_back(std::string(shown ? "ShowKickButton " : "HideKickButton ") + std::to_string(row));
}

bool AptDisconnectScreen::apply(const std::array<Row, 7> &rows)
{
	if (level() < 0 || !windows().isAptWindowLoaded(level()))
	{
		return false;
	}
	for (int i = 0; i < 7; ++i)
	{
		const Row &r = rows[(size_t)i];
		Row &s = m_sent[(size_t)i];
		if (m_first || r.nameUtf8 != s.nameUtf8)
		{
			setPlayerName(i, r.nameUtf8);
			s.kickShown = false;
			s.votes = -1;
			s.barPercent = -1;
		}
		if (!r.used)
		{
			s = r;
			continue;
		}
		if (r.votes != s.votes)
		{
			showVotes(i, r.votes);
		}
		if (r.barPercent != s.barPercent)
		{
			setBar(i, r.barPercent);
		}
		if (r.kickShown != s.kickShown)
		{
			showKick(i, r.kickShown);
		}
		s = r;
	}
	if (m_initialized)
	{
		// RW 0x9190C0: after OnInitialized every row's kick state again
		m_initialized = false;
		for (int i = 0; i < 7; ++i)
		{
			showKick(i, m_kickShown[(size_t)i]);
		}
	}
	m_first = false;
	return true;
}
