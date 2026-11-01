// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// InGameChat (ZH GameClient/GUI/GUICallbacks/InGameChat.cpp; RotWK's AptInGameChat, lane CHAT-1): what the in-game chat window decides without the movie:
// who a line goes to, the text it carries, how a received line reads, whether the window may open, and the scroll-back.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; logs/chat1 research, Ghidra + rw-text.asm):
//   * the keys (lang\English.big commandmap.ini): CHAT_EVERYONE = Enter (down), CHAT_ALLIES = Backspace (down); CHAT_PLAYERS is commented out. The meta
//     handler (RW 0x81F8D8, cases 0x6D / 0x6E at RW 0x81FEE0 / 0x81FF34) opens the window only in a LAN or online game (RW 0x441B7C: GameLogic + 0x110 is
//     1 or 5) that is not a replay, and when the local player is not active only if GlobalData + 0xA44 is 0 (INFERENCE: ZH's m_netMinPlayers gate);
//     ALLIES opens mode 0; EVERYONE opens mode 1, or 4 (Observers) when the local player is not active (isPlayerActive RW 0x6AAC52: not an observer and
//     not dead). The meta is destroyed either way.
//   * open (RW 0x8EC97D): nothing in a replay, while the in-game UI is busy (TheInGameUI vslot 0x17C), the disconnect screen is up, or a chat is open;
//     the receivers label "APT:InGameChatReceivers" gets the game text of Chat:Allies (0), Chat:Everyone (1; Chat:Observers when the local player is not
//     active), Chat:Players (2), Chat:Console (3), Chat:Observers (4).
//   * Send (RW 0x8ECF3E): the entry's text, the entry cleared, trimmed (UnicodeString::trim); an empty line (or a handled slash command, RW 0x8ECD8E)
//     sends nothing. Per slot 0 .. 7 with a player of that name: mode 0 takes the slot when the player and the local player are ALLIES both ways or it is
//     the local player; mode 1 every slot that is not muted (GameSlot + 0xA); mode 2 only the local player; mode 4 every player that is not active. The
//     line is the game text APT:TeamChat / APT:GlobalChat / APT:PrivateChat / APT:ObserverChat ("<Team> %s" ...) formatted with the text, run through
//     the language filter (RW 0x78A594), and sent with the mask (Network::sendChat).
//   * a received line (processChat RW 0x8D54D1) reads L"[%ls] %ls" (RW 0xBF6908) of the sender's name and the line, in the sender's player colour
//     (Player + 0x2A0), as an InGameUI message line (messageColor), with the IncomingChatNotification sound (RW 0x8D354A).
// OpenBFME ADDITIONS (named, docs/STOPS.md S-5150): the scroll-back (the last kHistoryLines lines, which the HUD shows while the window is open: retail's
// window has no history widget) and the slots above 8 (P16).
// NOT PORTED (S-5150): the console mode's commands (3, unreachable in retail), the /tribute slash command (RW 0x8ECD8E: such a line is sent as text here).

#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

enum class InGameChatMode
{
	Allies = 0,
	Everyone = 1,
	Players = 2,
	Console = 3,
	Observers = 4
};

class InGameChat
{
public:
	// what Send needs to know of a slot (retail reads TheGameInfo's slot and ThePlayerList's player of its name)
	struct Slot
	{
		bool player = false;         ///< a player of the slot's name exists
		bool local = false;          ///< it is the local player
		bool active = false;         ///< Player::isPlayerActive: not an observer and not dead
		bool alliedBothWays = false; ///< the player and the local player are ALLIES each to the other
		bool muted = false;          ///< GameSlot + 0xA (the players screen's mute)
	};

	// the meta handler's choice (RW 0x81FEE0 / 0x81FF34): EVERYONE opens Observers for a local player that is not active
	static InGameChatMode modeForMeta(bool everyone, bool localActive) { return !everyone ? InGameChatMode::Allies : localActive ? InGameChatMode::Everyone : InGameChatMode::Observers; }
	// whether the meta opens the window (RW 0x441B7C + the replay and the not-active gates)
	static bool metaOpens(bool networkGame, bool replay, bool localActive, bool netMinPlayersGate) { return networkGame && !replay && (localActive || !netMinPlayersGate); }
	// the game text label of the receivers line (RW 0x8EC97D)
	static const char *receiversLabel(InGameChatMode mode, bool localActive);
	// the game text label of the sent line's format (RW 0x8ECF3E)
	static const char *formatLabel(InGameChatMode mode);
	// RW 0x8ECF3E's mask (bit = slot index in `slots`)
	static std::uint32_t recipientMask(InGameChatMode mode, const std::vector<Slot> &slots);
	// UnicodeString::trim: the leading and trailing white space removed (space, tab, CR, LF, VT, FF)
	static std::u16string trim(const std::u16string &text);
	// the fetched format with its first %s / %ls replaced by `text` (UnicodeString::format of a one-argument label); a format without one is
	// the format followed by nothing (as printf ignores the extra argument)
	static std::u16string formatLine(const std::u16string &format, const std::u16string &text);
	// RW 0x8D54D1: L"[%ls] %ls"
	static std::u16string receivedLine(const std::u16string &senderName, const std::u16string &line) { return u"[" + senderName + u"] " + line; }

	// the acceptance stop of the chat (docs/STOPS.md S-5150), one line
	static std::vector<std::string> stopLines();

	// ---- the scroll-back (OpenBFME addition) ----
	struct HistoryLine
	{
		std::u16string text;     ///< as shown ("[Name] <Team> hello")
		std::uint32_t argb = 0;  ///< the sender's colour (0xFF alpha)
		std::uint32_t frame = 0; ///< the logic frame it arrived on
	};
	static constexpr std::size_t kHistoryLines = 100;
	void addHistory(const std::u16string &text, std::uint32_t argb, std::uint32_t frame);
	const std::deque<HistoryLine> &history() const { return m_history; }

	// ---- the draft kept between opens (RW 0xDE9ED4: Close saves the entry's text, Send clears it) ----
	const std::u16string &draft() const { return m_draft; }
	void setDraft(const std::u16string &d) { m_draft = d; }

private:
	std::deque<HistoryLine> m_history;
	std::u16string m_draft;
};
