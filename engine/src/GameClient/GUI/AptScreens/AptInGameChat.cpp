// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptInGameChat.h (lane CHAT-1).

#include "GameClient/GUI/AptScreens/AptInGameChat.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"

#include <set>
#include <vector>

AptInGameChat::AptInGameChat(WindowManager &windows, Shell &shell, InGameChat &model, InGameChatMode mode, const std::string &receivers)
	: AptScreen(windows, shell, "InGameChat.apt", "AptInGameChat"), m_model(model), m_mode(mode)
{
	// RW 0x8EC97D: the receivers label (setAptString RW 0x624FFD of the record the movie's $InGameChatReceivers reads)
	windows.setAptText("APT:InGameChatReceivers", receivers);
	registerScreenRef("AptInGameChat::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x8ECC1D: only the entry
		if (name != "InGameChatEntry" || !w)
		{
			this->windows().note("unknown-gadget", "AptInGameChat::InitGadgets(" + name + ")");
			return;
		}
		m_entry = w;
		GadgetTextEntrySetText(w, m_model.draft());
		if (EntryData *e = static_cast<EntryData *>(w->winGetUserData()))
		{
			e->maxTextLen = 0x6E;
		}
		w->manager().winSetFocus(w);
	});
	registerCommand("AptInGameChat::OnInitialized", [this](const std::string &) {
		if (m_state == STATE_LOADING)
		{
			m_state = STATE_OPEN; // RW 0x8EC93D
		}
	});
	registerCommand("AptInGameChat::OnClosed", [this](const std::string &) { m_state = STATE_CLOSED; }); // RW 0x8EC951
	registerCommand("AptInGameChat::Close", [this](const std::string &) {
		if (m_state == STATE_OPEN)
		{
			m_state = STATE_CLOSE_REQUESTED; // RW 0x8EC95E
		}
	});
	registerCommand("AptInGameChat::Send", [this](const std::string &) { send(); });
}

bool AptInGameChat::entryAlive()
{
	if (!m_entry)
	{
		return false;
	}
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer)
	{
		m_entry = nullptr;
		return false;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	const std::set<const GameWindow *> alive(live.begin(), live.end());
	if (!alive.count(m_entry))
	{
		m_entry = nullptr;
	}
	return m_entry != nullptr;
}

std::u16string AptInGameChat::entryText() const
{
	return m_entry ? GadgetTextEntryGetText(m_entry) : std::u16string();
}

void AptInGameChat::typeText(const std::u16string &text)
{
	if (entryAlive())
	{
		GadgetTextEntrySetText(m_entry, text);
	}
}

void AptInGameChat::send()
{
	// RW 0x8ECF3E
	if (m_state != STATE_OPEN)
	{
		return;
	}
	std::u16string text;
	if (entryAlive())
	{
		text = GadgetTextEntryGetText(m_entry);
		GadgetTextEntrySetText(m_entry, std::u16string());
	}
	text = InGameChat::trim(text);
	if (!text.empty())
	{
		++m_sends;
		if (onSend)
		{
			onSend(text, m_mode);
		}
	}
	m_state = STATE_CLOSE_REQUESTED;
}

void AptInGameChat::close()
{
	// RW 0x8ECC7A: the draft, the movie's CloseChat, state 3, the entry emptied (state 0: closed at once)
	m_model.setDraft(entryAlive() ? GadgetTextEntryGetText(m_entry) : std::u16string());
	if (m_state == STATE_LOADING)
	{
		m_state = STATE_CLOSED;
		return;
	}
	if (m_state == STATE_CLOSING || m_state == STATE_CLOSED)
	{
		return;
	}
	std::string error;
	if (level() < 0 || !windows().invokeAS(level(), "CloseChat", {}, nullptr, &error))
	{
		windows().note("invoke-failed", "InGameChat CloseChat: " + error);
		m_state = STATE_CLOSED; // no movie to run the close animation: the window goes now
	}
	else
	{
		m_state = STATE_CLOSING;
	}
	if (entryAlive())
	{
		GadgetTextEntrySetText(m_entry, std::u16string());
	}
}

void AptInGameChat::frameUpdate()
{
	// RW 0x8ECD3E: state 2 closes; state 4 is removed by the host (closed())
	if (m_state == STATE_CLOSE_REQUESTED)
	{
		close();
	}
}

bool AptInGameChat::inputKey(int key, int keyState)
{
	// RW 0x8ECB8F: Esc's release closes; Enter / keypad Enter with Ctrl (CHAT_BUDDIES) closes on its release. The other keys go to the focused entry
	// (the window layer offers them to it first in retail; the port's host offers the screen first, so only the screen's own keys are taken here)
	const bool up = (keyState & KEY_STATE_UP) != 0;
	if (key == KEY_ESC)
	{
		if (up && m_state == STATE_OPEN)
		{
			m_state = STATE_CLOSE_REQUESTED;
		}
		return true;
	}
	if ((key == KEY_ENTER || key == KEY_KPENTER) && (keyState & KEY_STATE_CONTROL))
	{
		if (up && m_state == STATE_OPEN)
		{
			m_state = STATE_CLOSE_REQUESTED;
		}
		return true;
	}
	return false;
}

WindowMsgHandledType AptInGameChat::gadgetMessage(GameWindow *, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	// RW 0x8ED45C: GEM_EDIT_DONE from the entry, data 0
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	if (msg == GEM_EDIT_DONE && gadget && gadget == m_entry && data2 == 0)
	{
		send();
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}
