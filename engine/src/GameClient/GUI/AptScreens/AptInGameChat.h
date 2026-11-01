// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// InGameChat.apt (CodePrefix AptInGameChat, lane CHAT-1): the in-game chat window. GameClient/InGameChat.h holds the rules (modes, recipients, formats);
// this class is the movie's side: the entry, the receivers label, Send / Cancel / Enter / Esc and the open / close states.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the single instance RW 0xDE9ED0: + 0x274 movie, + 0x27C state, + 0x280 mode, + 0x284 entry):
//   * the constructor (RW 0x8ED256) registers AptInGameChat::InitGadgets (RW 0x8ECC1D), ::OnInitialized (RW 0x8EC93D: state 0 -> 1), ::OnClosed
//     (RW 0x8EC951: state 4), ::Close (RW 0x8EC95E: state 1 -> 2), ::Send (RW 0x8ECF3E).
//   * InitGadgets for "InGameChatEntry": the entry gets the draft (RW 0xDE9ED4), at most 110 characters (EntryData + 0x10 = 0x6E, RW 0x81606D), the focus.
//   * the movie (InGameChat.apt, frames 9 .. 18 the opening, label _close at 19, frame 29 GameCode("OnClosed")): Send = GameCode("Send"); Cancel =
//     GameCode("Close") then _root.CloseChat(); the label text field $InGameChatReceivers (the record APT:InGameChatReceivers).
//   * the update (vtable + 0x18, RW 0x8ECD3E): state 2 -> close (RW 0x8ECC7A: the entry's text becomes the draft, the movie's CloseChat, state 3, the
//     entry emptied; in state 0 it is state 4 at once); state 4 -> the screen goes.
//   * the input procedure (vtable + 0x08, RW 0x8ECB8F): every GWM_CHAR is used; on a key's release Esc closes, and Enter / keypad Enter with Ctrl closes
//     (Ctrl+Enter is CHAT_BUDDIES' key). The system procedure (vtable + 0x0C, RW 0x8ED45C): GEM_EDIT_DONE (Enter) from the entry with data 0 is Send.
//   * Send: only in state 1; the entry's text, the entry emptied, trimmed; whatever happens next the state is 2 (the window closes).
// OpenBFME: the host does what Send's remaining steps need the live game for (onSend: the recipients, the format, Network::sendChat).

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/InGameChat.h"

#include <functional>
#include <string>

class AptInGameChat : public AptScreen
{
public:
	enum State
	{
		STATE_LOADING = 0, ///< the movie is opening
		STATE_OPEN = 1,
		STATE_CLOSE_REQUESTED = 2,
		STATE_CLOSING = 3, ///< the movie's close animation runs
		STATE_CLOSED = 4   ///< the host removes the screen
	};
	// `model` keeps the draft between opens (it outlives the screen); `receivers` is the game text of InGameChat::receiversLabel (UTF-8)
	AptInGameChat(WindowManager &windows, Shell &shell, InGameChat &model, InGameChatMode mode, const std::string &receivers);

	// the host's part of Send: a trimmed, non-empty line of this window's mode
	std::function<void(const std::u16string &text, InGameChatMode mode)> onSend;

	InGameChatMode mode() const { return m_mode; }
	int state() const { return m_state; }
	bool closed() const { return m_state == STATE_CLOSED; }
	GameWindow *entry() const { return m_entry; }
	int sends() const { return m_sends; }

	void frameUpdate() override;
	bool inputKey(int key, int keyState) override;
	// a close from outside (another screen opening over the game, the game ending: RW 0x8ECC7A's other callers)
	void close();
	// the text the entry holds now (tests)
	std::u16string entryText() const;
	// the entry's text replaced as typing would leave it (tests: the gadget's own path)
	void typeText(const std::u16string &text);

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void send();
	bool entryAlive();

	InGameChat &m_model;
	InGameChatMode m_mode;
	int m_state = STATE_LOADING;
	GameWindow *m_entry = nullptr;
	int m_sends = 0;
};
