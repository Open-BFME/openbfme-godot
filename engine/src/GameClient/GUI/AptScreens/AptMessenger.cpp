// OpenBFME. GPL-3.0. See AptMessenger.h for donor bindings.
#include "GameClient/GUI/AptScreens/AptMessenger.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "Libraries/Source/Apt/AptValue.h"
#include <algorithm>
#include <set>

AptMessenger::AptMessenger(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "Messenger.apt", "AptMessenger"), m_environment(environment)
{
	registerScreenRef("AptMessenger::InitGadgets", [this](const std::string &name, GameWindow *window) { initGadget(name, window); });
	registerCommand("AptMessenger::OnInitialized", [this](const std::string &) { m_initialized = true; m_buttonsDirty = true; });
	registerCommand("AptMessenger::OnClosed", [this](const std::string &) { close(); });
	registerCommand("AptMessenger::CloseScreen", [this](const std::string &) { close(); });
	registerCommand("AptMessenger::OnButtonSend", [this](const std::string &) { action(Action::Send); });
	registerCommand("AptMessenger::OnBttn_0", [this](const std::string &) { action(m_tab == 0 ? Action::RemoveFriend : Action::AddFriend); });
	registerCommand("AptMessenger::OnBttn_1", [this](const std::string &) {
		const auto id = selectedPlayer();
		const auto &rows = m_rows[(size_t)m_tab];
		const auto selected = std::find_if(rows.begin(), rows.end(), [&](const Row &r) { return r.id == id; });
		if (m_tab == 0 && selected != rows.end() && !selected->request && !canInvite(*selected))
		{
			this->windows().note("messenger-invite", "INVITE is not available: the friend is offline or playing, or you are not in a game room (RW 0x9BD9A7)");
			return;
		}
		action(m_tab == 1 ? (selected != rows.end() && selected->blocked ? Action::Unignore : Action::Ignore) :
			selected != rows.end() && selected->request ? Action::AcceptFriend : Action::Invite);
	});
	registerProvider("MessengerActiveTab", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			if (value != "0" && value != "1") return false;
			m_tab = value == "1" ? 1 : 0;
			m_buttonsDirty = true;
		}
		else value = std::to_string(m_tab);
		return true;
	});
	for (const auto &name : { "MessengerX", "MessengerY" })
	{
		registerProvider(name, [this, name](const std::string &, std::string &value, bool setting) {
			auto &position = std::string(name) == "MessengerX" ? m_x : m_y;
			if (setting) position = value;
			else value = position;
			return true;
		});
	}
	for (const auto &name : { "IsMaximized", "MessengerSetDragging" })
	{
		registerProvider(name, [this, name](const std::string &, std::string &value, bool setting) {
			auto &flag = std::string(name) == "IsMaximized" ? m_maximized : m_dragging;
			if (setting)
			{
				if (value != "0" && value != "1" && value != "true" && value != "false") return false;
				flag = value == "1" || value == "true";
				m_buttonsDirty = true;
			}
			else value = flag ? "1" : "0";
			return true;
		});
	}
	registerProvider("ShowPlayerButtons", [](const std::string &, std::string &value, bool setting) {
		if (!setting) value = "1";
		return !setting;
	});
	registerComponent("AptMessenger::GameWindowSize", [this](AptComponentRequest &r) {
		if (ownerWindow())
		{
			ownerWindow()->winSetPosition((int)r.x0, (int)r.y0);
			ownerWindow()->winSetSize((int)(r.x1 - r.x0), (int)(r.y1 - r.y0));
		}
		return std::shared_ptr<GameWindow>();
	});
	windows.setAptText("APT:MessengerTab0", OnlineLobbyClient::utf16ToUtf8(fetchOrMissing(environment.gameText, "APT:FriendsTab")));
	windows.setAptText("APT:MessengerTab1", OnlineLobbyClient::utf16ToUtf8(fetchOrMissing(environment.gameText, "APT:PlayersTab")));
	windows.setAptText("APT:CurrentLobbyName", "Friend chat");
	windows.addUpdateListener(this, [this]() { update(); });
	for (const auto &name : { "MessengerActiveTab", "MessengerX", "MessengerY", "IsMaximized", "MessengerSetDragging", "ShowPlayerButtons" })
		windows.markNumericProvider(name);
}

void AptMessenger::close()
{
	if (m_closing) return;
	m_closing = true;
	if (onClose) onClose();
	else windows().requestShellPop();
}

void AptMessenger::prune()
{
	if (auto *layer = windows().gadgetLayer())
	{
		const auto live = layer->gadgets().allWindows();
		const std::set<GameWindow *> alive(live.begin(), live.end());
		for (auto &w : m_players) if (w && !alive.count(w)) w = nullptr;
		for (auto &w : m_chats) if (w && !alive.count(w)) w = nullptr;
		if (m_entry && !alive.count(m_entry)) m_entry = nullptr;
	}
}

void AptMessenger::initGadget(const std::string &name, GameWindow *window)
{
	if (name == "Messenger::ChatEntry")
	{
		m_entry = window;
		GadgetTextEntrySetText(m_entry, std::u16string());
	}
	for (int tab = 0; tab < 2; ++tab)
	{
		if (name == "Messenger::PlayersTab_" + std::to_string(tab))
		{
			m_players[(size_t)tab] = window;
			GadgetListBoxRemoveMultiSelect(window);
			fillPlayers(tab);
		}
		if (name == "Messenger::ChatTab_" + std::to_string(tab))
		{
			m_chats[(size_t)tab] = window;
			fillChat(tab);
		}
	}
}

std::string AptMessenger::selectedPlayer()
{
	prune();
	int selected = -1;
	if (m_players[(size_t)m_tab]) GadgetListBoxGetSelected(m_players[(size_t)m_tab], &selected);
	const auto &rows = m_rows[(size_t)m_tab];
	return selected >= 0 && selected < (int)rows.size() ? rows[(size_t)selected].id : std::string();
}

bool AptMessenger::selectPlayer(const std::string &id)
{
	prune();
	const auto &rows = m_rows[(size_t)m_tab];
	const auto match = std::find_if(rows.begin(), rows.end(), [&](const Row &r) { return r.id == id; });
	if (id.empty() || match == rows.end() || !m_players[(size_t)m_tab])
	{
		return false;
	}
	GadgetListBoxSetSelected(m_players[(size_t)m_tab], (int)(match - rows.begin()));
	m_buttonsDirty = true;
	return true;
}

void AptMessenger::setRows(int tab, const std::vector<Row> &rows)
{
	if (tab < 0 || tab > 1) return;
	prune();
	int selected = -1;
	if (m_players[(size_t)tab]) GadgetListBoxGetSelected(m_players[(size_t)tab], &selected);
	const auto &previous = m_rows[(size_t)tab];
	const auto id = selected >= 0 && selected < (int)previous.size() ? previous[(size_t)selected].id : std::string();
	m_rows[(size_t)tab] = rows;
	fillPlayers(tab);
	if (m_players[(size_t)tab])
	{
		const auto match = std::find_if(rows.begin(), rows.end(), [&](const Row &r) { return r.id == id; });
		GadgetListBoxSetSelected(m_players[(size_t)tab], match == rows.end() ? -1 : (int)(match - rows.begin()));
	}
	m_buttonsDirty = true;
}

// lane ONLINEMENU-1: a friend's status as retail shows it. RW 0x9E63E6 formats "Buddy:%ls" with the GameSpy status name and shows that game text
// (lotr.str: Buddy:Online, Buddy:Offline, Buddy:Staging, Buddy:Loading, Buddy:Playing, Buddy:Matching; a pending request Buddy:BuddyRequest). The lobby's
// presence states map onto them (OpenBFME: "in_room" is retail's Staging; "away" has no retail status and shows Online). The PLAYERS tab's rows keep
// their own words ("In this room", "Recent player": OpenBFME's, retail had the lobby room's players). An unknown state is shown as it came, noted once
std::u16string AptMessenger::statusText(int tab, const Row &row)
{
	if (tab != 0)
	{
		return row.status;
	}
	static const struct
	{
		const char16_t *state;
		const char *label;
	} kStates[] = { { u"online", "Buddy:Online" }, { u"away", "Buddy:Online" }, { u"offline", "Buddy:Offline" }, { u"in_room", "Buddy:Staging" },
		{ u"loading", "Buddy:Loading" }, { u"playing", "Buddy:Playing" }, { u"matching", "Buddy:Matching" } };
	if (row.request)
	{
		return fetchOrMissing(m_environment.gameText, "Buddy:BuddyRequest");
	}
	for (const auto &k : kStates)
	{
		if (row.status == k.state)
		{
			return fetchOrMissing(m_environment.gameText, k.label);
		}
	}
	if (!row.status.empty() && m_unknownStates.insert(OnlineLobbyClient::utf16ToUtf8(row.status)).second)
	{
		windows().note("messenger-status", "a friend's presence state \"" + OnlineLobbyClient::utf16ToUtf8(row.status) + "\" has no retail status text");
	}
	return row.status;
}

// lane ONLINEMENU-1: INVITE is offered as retail's friends list offers it (RW 0x9BD9A7, the menu refresh: with one friend selected REMOVE FRIEND is
// enabled; a pending request turns the second button into ACCEPT FRIEND; otherwise INVITE is enabled only when the friend's status is neither offline
// nor playing (GameSpy status 0 / 2) and the player is in a custom match staging room, g_currentAptOnlineCustomMatch [0xDEBE68] + RW 0x9A86E4)
bool AptMessenger::canInvite(const Row &row) const
{
	if (row.request || row.status == u"offline" || row.status == u"playing" || row.status == u"loading")
	{
		return false;
	}
	const OnlineShellContext *online = m_environment.online;
	return online && online->client && online->client->inGame();
}

void AptMessenger::fillPlayers(int tab)
{
	auto *w = m_players[(size_t)tab];
	if (!w) return;
	GadgetListBoxReset(w);
	GadgetListBoxSetColumnWidths(w, 1, nullptr);
	for (const auto &row : m_rows[(size_t)tab])
	{
		const std::u16string status = statusText(tab, row);
		GadgetListBoxAddEntryText(w, row.name + (status.empty() ? u"" : u" - " + status), 0xFFFFFFFFu, -1, 0);
	}
}

void AptMessenger::setChat(int tab, const std::vector<std::u16string> &lines)
{
	if (tab < 0 || tab > 1) return;
	prune();
	m_chat[(size_t)tab] = lines;
	fillChat(tab);
}

void AptMessenger::acknowledgeMessage(const std::u16string &text)
{
	prune();
	if (m_entry && GadgetTextEntryGetText(m_entry) == text) GadgetTextEntrySetText(m_entry, std::u16string());
}

void AptMessenger::fillChat(int tab)
{
	auto *w = m_chats[(size_t)tab];
	if (!w) return;
	GadgetListBoxReset(w);
	for (const auto &line : m_chat[(size_t)tab]) GadgetListBoxAddEntryText(w, line, 0xFFFFFFFFu, -1, 0);
}

void AptMessenger::action(Action::Kind kind)
{
	const auto id = selectedPlayer();
	// lane ONLINEMENU-1: ENTER with nobody selected still hands the text on (a friend code typed into the entry adds that friend)
	if (id.empty() && kind != Action::Send) return;
	const auto message = kind == Action::Send && m_entry ? GadgetTextEntryGetText(m_entry) : std::u16string();
	if (kind == Action::Send && message.empty()) return;
	Action a{kind, id, message};
	if (onAction) onAction(a);
	else if (m_actions.size() < 64) m_actions.push_back(a);
}

std::vector<AptMessenger::Action> AptMessenger::takeActions()
{
	std::vector<Action> out;
	out.swap(m_actions);
	return out;
}

WindowMsgHandledType AptMessenger::gadgetMessage(GameWindow *, std::uint32_t msg, WindowMsgData data1, WindowMsgData)
{
	prune();
	if (msg == GLM_SELECTED && (GameWindow *)data1 == m_players[(size_t)m_tab])
	{
		m_buttonsDirty = true;
		action(Action::Select);
		return MSG_HANDLED;
	}
	if (msg == GEM_EDIT_DONE && (GameWindow *)data1 == m_entry)
	{
		action(Action::Send);
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

void AptMessenger::buttons()
{
	const auto id = selectedPlayer();
	const auto &rows = m_rows[(size_t)m_tab];
	const auto selected = std::find_if(rows.begin(), rows.end(), [&](const Row &r) { return r.id == id; });
	const char *labels[2] = { m_tab == 0 ? "APT:RemoveFriend" : "APT:AddFriend", m_tab == 0 ? "APT:InviteToPlay" : "APT:Ignore" };
	windows().setAptText("APT:CurrentLobbyName", selected == rows.end() ? "Friend chat" : OnlineLobbyClient::utf16ToUtf8(selected->name));
	if (m_tab == 0 && selected != rows.end() && selected->request) labels[1] = "APT:AcceptFriend";
	if (m_tab == 1 && selected != rows.end() && selected->blocked) labels[1] = "APT:UnIgnore"; // lane ONLINEMENU-1
	// lane ONLINEMENU-1: the second button of the FRIENDS tab follows RW 0x9BD9A7 (canInvite); the others need only a selected player
	const bool second = !id.empty() && (m_tab != 0 || (selected != rows.end() && (selected->request || canInvite(*selected))));
	m_inviteEnabled = m_tab == 0 && second && selected != rows.end() && !selected->request;
	for (int i = 0; i < 2; ++i)
	{
		windows().setAptText("APT:MessengerBttn_" + std::to_string(i), OnlineLobbyClient::utf16ToUtf8(fetchOrMissing(m_environment.gameText, labels[i])));
		std::string error;
		// lane ONLINEMENU-1: ADD FRIEND is greyed for a friend, a pending request or an ignored player
		const bool enabled = i == 0 ? !id.empty() && (m_tab == 0 || (selected != rows.end() && selected->friendable && !selected->blocked)) : second;
		m_buttonEnabled[(size_t)i] = enabled;
		if (!windows().invokeASAtValues(level(), "", "EnableButton", {AptValue::number(i), AptValue::boolean(enabled)}, &error))
			windows().note("movie-function-missing", "Messenger EnableButton: " + error);
	}
	m_buttonsDirty = false;
}

void AptMessenger::update()
{
	prune();
	// lane ONLINEMENU-1: INVITE depends on being in a staging room (RW 0x9BD9A7): the buttons follow a join or a leave
	const OnlineShellContext *online = m_environment.online;
	const bool inRoom = online && online->client && online->client->inGame();
	if (inRoom != m_inRoom)
	{
		m_inRoom = inRoom;
		m_buttonsDirty = true;
	}
	if (m_initialized && m_maximized && m_buttonsDirty && !m_closing) buttons();
}
