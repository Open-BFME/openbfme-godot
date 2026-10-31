// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptDisconnectScreen.h (lane MP-2).

#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameNetwork/GameInfo.h"

#include "GameClient/GUI/Gadget.h"
#include "GameClient/InGameChat.h"

#include <set>
#include <vector>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>

AptDisconnectScreen::AptDisconnectScreen(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "DisconnectScreen.apt", "AptDisconnectScreen")
{
	registerScreenRef("DisconnectScreen::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// lane CHAT-1: RW 0x919529
		if (!w)
		{
			return;
		}
		if (name == "ChatBox")
		{
			m_chatBox = w;
		}
		else if (name == "ChatEntry")
		{
			m_chatEntry = w;
			GadgetTextEntrySetText(w, std::u16string());
		}
		else if (name == "RulesBox")
		{
			GadgetListBoxReset(w);
			GadgetListBoxAddEntryText(w, m_rules, 0xFFFFFFFFu, -1, 0);
		}
		else
		{
			this->windows().note("unknown-gadget", "DisconnectScreen::InitGadgets(" + name + ")");
		}
	});
	registerCommand("AptDisconnectScreen::OnInitialized", [this](const std::string &) {
		m_initialized = true; // RW 0x918FEE
		m_ready = true;
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
		int row = -1;
		const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), row);
		if (parsed.ec != std::errc() || parsed.ptr != argument.data() + argument.size() || row < 0 || row >= 7 ||
			(m_rowRange && (!m_rows[(size_t)(m_firstRow + row)].used || !m_kickShown[(size_t)row])))
		{
			m_error = "invalid disconnect kick row: " + argument;
			this->windows().note("disconnect-error", m_error);
			return;
		}
		++m_kicks;
		if (onKick)
		{
			onKick(m_firstRow + row);
		}
	});
	registerCommand("AptDisconnectScreen::Chat::OnBttnEnterText", [this](const std::string &) { sendChat(); }); // lane CHAT-1: RW 0x9196B9
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
	return apply(std::vector<Row>(rows.begin(), rows.end()), LEGACY_MAX_SLOTS);
}

AptDisconnectScreen::~AptDisconnectScreen()
{
	if (AptGadgetLayer *layer = windows().gadgetLayer()) layer->destroyEngineGadget(m_rowRange);
}

WindowMsgHandledType AptDisconnectScreen::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	// lane CHAT-1: Enter in the chat entry sends the line
	if (msg == GEM_EDIT_DONE && data1 && reinterpret_cast<GameWindow *>(data1) == m_chatEntry && data2 == 0)
	{
		sendChat(); // INFERENCE: Enter is the movie's enter button
		return MSG_HANDLED;
	}
	if (m_rowRange && reinterpret_cast<GameWindow *>(data1) == m_rowRange && msg == GCM_SELECTED)
	{
		int selected = -1;
		GadgetComboBoxGetSelectedPos(m_rowRange, &selected);
		if (selected >= 0 && selected <= (int)m_rows.size() - 7)
		{
			m_firstRow = selected;
			m_first = true;
			renderRows();
		}
		return MSG_HANDLED;
	}
	return AptScreen::gadgetMessage(from, msg, data1, data2);
}

bool AptDisconnectScreen::apply(const std::vector<Row> &rows, int mapStartCount)
{
	m_error.clear();
	if (mapStartCount < LEGACY_MAX_SLOTS || mapStartCount > MAX_SLOTS || rows.size() > (size_t)(mapStartCount - 1))
	{
		m_error = "disconnect rows exceed map capacity or capacity is outside 8 .. 16";
		windows().note("disconnect-error", m_error);
		return false;
	}
	const int count = mapStartCount - 1;
	if ((int)m_rows.size() != count)
	{
		if (AptGadgetLayer *layer = windows().gadgetLayer()) layer->destroyEngineGadget(m_rowRange);
		m_rowRange = nullptr;
		m_firstRow = 0;
		m_first = true;
	}
	m_rows = rows;
	m_rows.resize((size_t)count);
	if (count > 7 && !m_rowRange)
	{
		if (!m_ready) return false; // the movie's components must precede the range control in input order
		AptGadgetLayer *layer = windows().gadgetLayer();
		if (!layer)
		{
			m_error = "disconnect range requires a gadget layer";
			return false;
		}
		m_rowRange = layer->createEngineGadget("window/apt/combobox.wnd", 392, 490, 240, 24, ownerWindow(), &m_error);
		if (!m_rowRange) return false;
		GadgetComboBoxSetMaxDisplay(m_rowRange, 4);
		// OpenBFME extension of RW 0x9190C0: reuse the seven authored rows without compacting translated remote-slot indices.
		for (int i = 0; i <= count - 7; ++i)
		{
			const std::string label = "Remote rows " + std::to_string(i + 1) + "-" + std::to_string(i + 7) + " / " + std::to_string(count);
			GadgetComboBoxAddEntry(m_rowRange, std::u16string(label.begin(), label.end()), 0xFFFFFFFFu);
		}
		GadgetComboBoxSetSelectedPos(m_rowRange, 0, true);
	}
	return renderRows();
}

bool AptDisconnectScreen::renderRows()
{
	if (level() < 0 || !windows().isAptWindowLoaded(level()))
	{
		return false;
	}
	for (int i = 0; i < 7; ++i)
	{
		const Row &r = m_rows[(size_t)(m_firstRow + i)];
		Row &s = m_sent[(size_t)i];
		if (m_first || r.nameUtf8 != s.nameUtf8 || (m_rowRange && r.used != s.used))
		{
			setPlayerName(i, m_rowRange && !r.used ? std::string() : r.nameUtf8);
			s.kickShown = false;
			s.votes = -1;
			s.barPercent = -1;
		}
		if (!r.used)
		{
			if (m_rowRange && (m_first || s.used))
			{
				setBar(i, 0);
				showKick(i, false);
			}
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

namespace
{
bool gadgetAlive(WindowManager &windows, GameWindow *&w)
{
	AptGadgetLayer *layer = windows.gadgetLayer();
	if (!w || !layer)
	{
		w = nullptr;
		return false;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	if (std::find(live.begin(), live.end(), w) == live.end())
	{
		w = nullptr;
	}
	return w != nullptr;
}
} // namespace

void AptDisconnectScreen::sendChat()
{
	// RW 0x9196B9: the entry's text, the entry emptied, trimmed; a non-empty line goes out (RW 0x9193F1)
	if (!gadgetAlive(windows(), m_chatEntry))
	{
		windows().note("command-ignored", "AptDisconnectScreen::Chat::OnBttnEnterText without a ChatEntry gadget");
		return;
	}
	const std::u16string text = InGameChat::trim(GadgetTextEntryGetText(m_chatEntry));
	GadgetTextEntrySetText(m_chatEntry, std::u16string());
	if (text.empty())
	{
		return;
	}
	++m_chatsSent;
	if (onChat)
	{
		onChat(text);
	}
}

bool AptDisconnectScreen::showChat(const std::u16string &line)
{
	// RW 0x919447: GadgetListBoxAddEntryText(ChatBox, line, the colour at RW 0xDB68C4 = 0xFFFF0000, -1, 0)
	if (!gadgetAlive(windows(), m_chatBox))
	{
		return false;
	}
	GadgetListBoxAddEntryText(m_chatBox, line, 0xFFFF0000u, -1, 0);
	return true;
}
