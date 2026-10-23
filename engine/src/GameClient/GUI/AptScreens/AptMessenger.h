// OpenBFME. GPL-3.0.
// Retail Messenger.apt bindings. Donors: BFME2 AptMessengerConstructor.cpp,
// AptMessengerCallbacks.cpp and AptMessengerOnlineConstructor.cpp. Network state
// is supplied separately; a row's id is a public player id, never an install id.
#pragma once

#include "GameClient/GUI/AptScreen.h"
#include <array>
#include <functional>
#include <set>
#include <string>

struct ShellEnvironment;

class AptMessenger : public AptScreen
{
public:
	struct Row
	{
		std::string id;
		std::u16string name, status;
		bool request = false;
		// lane ONLINEMENU-1 (owner, 2026-10-20: friends live in this Messenger): a PLAYERS row the player ignores (blocks): its second button is
		// RotWK's UN-IGNORE (APT:UnIgnore, the string retail kept for its chat's player list); `friendable` false: ADD FRIEND is greyed (a friend
		// already, or a request either way)
		bool blocked = false;
		bool friendable = true;
	};
	struct Action
	{
		enum Kind { Select, AddFriend, RemoveFriend, AcceptFriend, Invite, Ignore, Send, Unignore };
		Kind kind;
		std::string player;
		std::u16string text;
	};
	AptMessenger(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	void setRows(int tab, const std::vector<Row> &rows);
	void setChat(int tab, const std::vector<std::u16string> &lines);
	void acknowledgeMessage(const std::u16string &text);
	int activeTab() const { return m_tab; }
	bool closing() const { return m_closing; }
	void requestClose() { close(); }
	std::string selectedPlayer();
	// lane ONLINEMENU-1 (Sol): the player chosen elsewhere (the FRIENDS page) is chosen in the active tab's list too, so the list and the chat shown
	// name the same player; false (nothing changes) when the tab does not list them
	bool selectPlayer(const std::string &id);
	bool buttonEnabled(int i) const { return i >= 0 && i < 2 && m_buttonEnabled[(size_t)i]; } ///< lane ONLINEMENU-1 (tests)
	bool inviteOffered() const { return m_inviteEnabled; }                                    ///< the second button is an enabled INVITE
	std::vector<Action> takeActions();
	std::function<void(const Action &)> onAction;
	std::function<void()> onClose;

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void initGadget(const std::string &name, GameWindow *window);
	void update();
	void fillPlayers(int tab);
	void fillChat(int tab);
	void buttons();
	void action(Action::Kind kind);
	void close();
	void prune();
	std::u16string statusText(int tab, const Row &row); ///< lane ONLINEMENU-1: RW 0x9E63E6's Buddy:<status> text
	bool canInvite(const Row &row) const;              ///< lane ONLINEMENU-1: RW 0x9BD9A7's INVITE rule
	ShellEnvironment &m_environment;
	std::array<std::vector<Row>, 2> m_rows;
	std::array<std::vector<std::u16string>, 2> m_chat;
	std::vector<Action> m_actions;
	std::array<GameWindow *, 2> m_players{}, m_chats{};
	std::set<std::string> m_unknownStates;     ///< lane ONLINEMENU-1: presence states with no retail text, noted once each
	std::array<bool, 2> m_buttonEnabled{};     ///< lane ONLINEMENU-1: what the movie's two buttons were last told (tests)
	bool m_inviteEnabled = false;
	bool m_inRoom = false;
	GameWindow *m_entry = nullptr;
	int m_tab = 0;
	bool m_maximized = true, m_dragging = false, m_closing = false, m_buttonsDirty = true, m_initialized = false;
	// Retail uses empty kept coordinates until the movie chooses its initial position.
	std::string m_x, m_y;
};
