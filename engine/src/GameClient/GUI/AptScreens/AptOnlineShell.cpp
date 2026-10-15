// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/AptScreens/AptOnlineShell.h (lane ONLINE-4).

#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace
{
std::string narrow(const std::u16string &s)
{
	return OnlineLobbyClient::utf16ToUtf8(s);
}
UnicodeString wide(const std::string &s)
{
	return UnicodeString(s.begin(), s.end());
}
std::u16string trim(std::u16string s)
{
	while (!s.empty() && (s.back() == u' ' || s.back() == u'\t'))
	{
		s.pop_back();
	}
	size_t i = 0;
	while (i < s.size() && (s[i] == u' ' || s[i] == u'\t'))
	{
		++i;
	}
	return s.substr(i);
}
// the map cache key's display form (as AptLanLobby's list): the file name without its folder and ".map"
std::string mapLeaf(const std::string &key)
{
	std::string map = key;
	const size_t slash = map.find_last_of("/\\");
	map = slash == std::string::npos ? map : map.substr(slash + 1);
	if (map.size() > 4 && map.compare(map.size() - 4, 4, ".map") == 0)
	{
		map.resize(map.size() - 4);
	}
	return map;
}
// OpenBFME's own texts (no retail label exists for them; the online service is OpenBFME's)
const char16_t *const kNoMotd = u"The lobby has no message of the day.";
const char16_t *const kNoRelayTitle = u"Online";
const char16_t *const kNoRelay = u"Online games can't start yet: no relay server is available.";
const char16_t *const kRejoinTitle = u"Rejoin";
} // namespace

const std::vector<std::string> &AptOnlineShell::stopLines()
{
	static const std::vector<std::string> lines = {
		"[S-4070] online shell: the opener's RW 0x6230B6(1) is taken as the front-end background; GameSpy's setup (RW 0x9CCC06) and its misc preference "
		"InAdvMode (OnlineAdvMode) are not ported (the mode is kept for the session)",
		"[S-4071] online sign-in: the lobby's guest sign-in only: the nickname is the guest name; e-mail, password, remember-me, register, delete nickname, "
		"terms of service, the locale pop-up and the GameSpy / official site buttons are disabled or report not available",
		"[S-4072] online home: the message of the day is the lobby's /v1/motd (none: said plainly); the ticker and the ladder site are not ported",
		"[S-4073] online game list: the lobby's games (name, the map's display name (a map not in the cache: its file name), players, lock); no ping, "
		"settings or user-map icons, no lobby rooms; the sort order "
		"(a second press reverses) is OpenBFME's (RotWK's sorter RW 0x988BB5 not read)",
		"[S-4074] online staging room: the states between the retail facts (the update's moves, the GameSpy NAT negotiation, the countdown) are OpenBFME's: "
		"the host's Play Game asks the lobby for the START (no relay: refused, told to the player); loading a saved online game is not available",
		"[S-4075] online pages not ported: Quick Match, Strategic (War of the Ring online), Profile / Stats, the Messenger",
		"[S-4076] online Create-a-Hero: the lobby service does not carry a slot's Create-a-Hero record: online games are played without them",
		"[S-4550] online Connections screen: not shown (retail's lists the GameSpy connection between every two players; OpenBFME's players reach each "
		"other only through the relay): the button says so and the staging room comes back",
		"[S-4551] online staging room chat: the Players list names the room's players (retail's fill, the chat's recipients, was not read) and every "
		"line goes to everyone (no private lines)",
		"[S-4552] online staging room rows: Hero (no Create-a-Hero through the lobby, S-4076) and Handicap (no entries in this port, S-178) are shown "
		"locked; the lobby carries no handicap",
		"[S-4553] online ready rules: the lobby unreadies every player on any change of the room (a join, a leave, a kick, a slot or setup change); "
		"RotWK's own reset (the game's vslot 0x38 after a rule change, RW 0x8421E0) was read only for the host's rule changes; the clan game's checks "
		"before a ready (CLAN:ErrorMissingClanAffiliation / CLAN:ErrorOnlyTwoClans) are not ported",
		"[S-4554] online staging room transport: polls held by the lobby (long polling) and the sender's own chat line shown at its sending are "
		"OpenBFME's (retail: GameSpy's peer chat)",
		"[S-4077] online rejoin / watch: RotWK's movies have no Rejoin or Watch button: the player's running game is offered in GuiFX's Yes / No box at "
		"sign-in and is a REJOIN row of the Open Play list, other running games are WATCH rows; Join Game on such a row asks the lobby for the rejoin "
		"or spectator START (state 13, OpenBFME's)",
	};
	return lines;
}

AptOnlineShell::~AptOnlineShell()
{
	for (auto &p : m_pages)
	{
		for (const std::string &n : p.second.commands)
		{
			windows().unregisterCommand(n);
		}
		for (const std::string &n : p.second.providers)
		{
			windows().unregisterProvider(n);
		}
		for (const std::string &n : p.second.screenRefs)
		{
			windows().closeAptScreen(n); // _bfme_closeAptScreen
		}
	}
}

AptOnlineShell::AptOnlineShell(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptSkirmish(windows, shell, environment, "OnlineShell.apt", "AptOnline", true), m_ctx(environment.online),
	  m_client(environment.online ? environment.online->client : nullptr)
{
	// RW 0x91D7A3: RW 0x6230B6(1) after the push. INFERENCE (S-4070): the front-end background as the LAN opener's (RW 0x847180)
	windows.setBackground(1);
	if (!m_client)
	{
		windows.note("provider-unwired", "OnlineShell.apt: the host gave no online lobby client [S-4070]");
	}
	for (const std::string &line : stopLines())
	{
		windows.note("online-stops", line);
	}
	// RW 0x91E133: the page title text APT:OnlineOrNetwork = APT:Online
	windows.setAptText("APT:OnlineOrNetwork", narrow(text("APT:Online")));
	registerCommand("AptOnline::OnInitialized", [](const std::string &) {}); // RW 0x9F3A3C (an empty callback)
	registerCommand("AptOnline::ShellLoadScreen", [this](const std::string &name) { loadPage(name); });
	registerCommand("AptOnline::ShellUnloadScreen", [this](const std::string &name) { unloadPage(name); });
	registerCommand("AptOnline::Options", [this](const std::string &) {
		AptOptionsScreen::open(this->shell(), true, false, true, false); // RW 0x91D6BA (BFME2 0x516EC0: OpenScreen(1, 0, 1, 0))
	});
	registerCommand("AptOnline::ShellExit", [this](const std::string &) {
		// RW 0x91D6CD (BFME2 0x516ED3): the online shell is left (BFME2 0x516E92). The lobby game, if any, is left first
		if (m_client && m_client->inGame())
		{
			m_client->leaveGame();
		}
		this->windows().requestShellPop();
	});
	// lane ONLINE-5: the MESSENGER button of the shell's frame (MpGameSetup.apt's MessengerButton: its release sends AptMessenger::OnMessengerBttn and
	// its look follows the extern AptMessenger::IsOpen). The Messenger (GameSpy's buddy chat, AptMessenger) is not ported (S-4075): the press used to
	// reach no handler at all; now the player is told, and the button shows the Messenger closed
	registerCommand("AptMessenger::OnMessengerBttn", [this](const std::string &) { notAvailable("the Messenger", "[S-4075]"); });
	registerProvider("AptMessenger::IsOpen", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0";
		}
		return true;
	});
	registerProvider("OnlineShellStartScreen", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = m_currentName; // RW 0x91D9F9 query 0 (read only)
		}
		return true;
	});
	registerProvider("OnlineAdvMode", [this](const std::string &, std::string &value, bool setting) {
		// query 1: GameSpy's misc preference InAdvMode; kept for the session only (GameSpy's preference file is not written: S-4070)
		if (setting)
		{
			m_advMode = value == "1" || value == "t" || value == "true" ? "1" : "0";
		}
		else
		{
			value = m_advMode;
		}
		return true;
	});
	windows.addUpdateListener(this, [this]() { update(); });
}

std::vector<std::string> AptOnlineShell::loadedPages() const
{
	std::vector<std::string> out;
	for (const auto &p : m_pages)
	{
		out.push_back(p.first);
	}
	return out;
}

AptMessageBox &AptOnlineShell::box()
{
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	return *m_box;
}

std::u16string AptOnlineShell::text(const std::string &label) const
{
	return fetchOrMissing(const_cast<AptOnlineShell *>(this)->environment().gameText, label);
}

void AptOnlineShell::showBox(const std::u16string &title, const std::u16string &body, const std::string &record)
{
	m_boxes.push_back(record);
	windows().note("online-box", record);
	box().show(AptMessageBox::TYPE_OK, title, body);
}

void AptOnlineShell::showError(const std::string &titleLabel, const std::string &textLabel)
{
	// GSMessageBoxOk(TheGameText->fetch(title), TheGameText->fetch(text)) (BFME2 0x5A57BA, 0x570D36 ...)
	showBox(text(titleLabel), text(textLabel), titleLabel + "|" + textLabel);
}

void AptOnlineShell::notAvailable(const std::string &what, const std::string &stop)
{
	windows().note("unported", what + " is not available in OpenBFME's online play " + stop);
	showBox(text("GUI:GSErrorTitle"), u"Not available in OpenBFME's online play yet: " + std::u16string(what.begin(), what.end()) + u".",
		"GUI:GSErrorTitle|not available: " + what);
}

void AptOnlineShell::callChild(const std::string &function, const std::vector<std::string> &args)
{
	// the login page's calls (BFME2 0x572632: TheWindowManager's invoke of "CallChild" with the function's name on the shell's movie + 0x274): the
	// page's own entry point HandleGameCall(name) runs them (OnlineLogin.apt traces "HandleGameCall:<name>"). INFERENCE (S-4071): the shell's CallChild
	// forwards to it; here the page's HandleGameCall is called at its path
	std::vector<std::string> all{ function };
	all.insert(all.end(), args.begin(), args.end());
	std::string err;
	if (level() < 0 || !windows().invokeASAt(level(), m_currentName, "HandleGameCall", all, nullptr, &err))
	{
		windows().note("movie-function-missing", m_currentName + ".HandleGameCall(" + function + "): " + err);
	}
}

void AptOnlineShell::callPage(const std::string &function, const std::vector<std::string> &args)
{
	// the custom match screen calls its page's functions at its own Apt path (vslot 0x28 composes it: RW 0x9AFAA4 .. 0x9AFAB6 for OnStartLobby)
	std::string err;
	if (level() < 0 || !windows().invokeASAt(level(), "OnlineOpenPlay", function, args, nullptr, &err))
	{
		windows().note("movie-function-missing", "OnlineOpenPlay." + function + ": " + err);
	}
}

// ---- the pages ------------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::pageCommand(const std::string &page, const std::string &name, std::function<void(const std::string &)> fn)
{
	if (windows().registerCommand(name, std::move(fn)))
	{
		m_pages[page].commands.push_back(name);
	}
	else
	{
		windows().note("register-duplicate", name);
	}
}

void AptOnlineShell::pageProvider(const std::string &page, const std::string &name, WindowManager::ProviderFn fn)
{
	if (windows().registerProvider(name, std::move(fn)))
	{
		m_pages[page].providers.push_back(name);
	}
}

void AptOnlineShell::pageScreenRef(const std::string &page, const std::string &name, WindowManager::ScreenRefFn fn)
{
	if (windows().registerScreenRef(name, std::move(fn)))
	{
		m_pages[page].screenRefs.push_back(name);
	}
}

void AptOnlineShell::loadPage(const std::string &name)
{
	// BFME2 0x517724: the page table 0xDD1568 by name; a factory makes nothing while its page is up
	if (m_pages.count(name))
	{
		m_currentName = name;
		return;
	}
	if (name == "OnlineLogin")
	{
		m_pages[name];
		registerLogin();
	}
	else if (name == "OnlineHome")
	{
		m_pages[name];
		registerHome();
	}
	else if (name == "OnlineOpenPlay")
	{
		m_pages[name];
		registerOpenPlay();
	}
	else
	{
		// OnlineStrategic, OnlineQuickMatch, Stats: not ported [S-4075]
		m_pages[name];
		notAvailable(name, "[S-4075]");
	}
	m_currentName = name;
}

void AptOnlineShell::unloadPage(const std::string &name)
{
	// BFME2 0x5171A3: the page of that name is deleted
	auto it = m_pages.find(name);
	if (it == m_pages.end())
	{
		return;
	}
	for (const std::string &n : it->second.commands)
	{
		windows().unregisterCommand(n);
	}
	for (const std::string &n : it->second.providers)
	{
		windows().unregisterProvider(n);
	}
	for (const std::string &n : it->second.screenRefs)
	{
		windows().closeAptScreen(n); // _bfme_closeAptScreen
	}
	m_pages.erase(it);
	if (name == "OnlineLogin")
	{
		m_nickname = m_email = m_password = m_remember = nullptr;
		m_loginDisablesDue = 0;
	}
	else if (name == "OnlineHome")
	{
		m_motd = nullptr;
	}
	else if (name == "OnlineOpenPlay")
	{
		m_gameList = m_lobbies = m_gameInfo = m_gameName = m_gamePassword = nullptr;
		if (m_client && m_client->inGame())
		{
			m_client->leaveGame(); // the custom match screen's teardown leaves the staging room (BFME2 0x59FC45's path)
		}
		m_state = 0;
	}
}

// ---- OnlineLogin -----------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::registerLogin()
{
	const std::string page = "OnlineLogin";
	pageCommand(page, "AptOnline::Login::Login", [this](const std::string &) { login(); });
	pageCommand(page, "AptOnline::Login::DeleteNickname", [this](const std::string &) { notAvailable("Delete nickname", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::Register", [this](const std::string &) { notAvailable("Register", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::ServiceTerms", [this](const std::string &) { notAvailable("GameSpy's terms of service", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::AcceptLocale", [this](const std::string &) { notAvailable("The locale", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::OfficialSite", [this](const std::string &) { notAvailable("The official site (URL:LotrHome)", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::GameSpy", [this](const std::string &) { notAvailable("GameSpy (URL:GameSpyHome)", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::CancelLogin", [this](const std::string &) {
		// RW 0x977B0F: the online shell is left (as ShellExit)
		this->windows().requestShellPop();
	});
	pageScreenRef(page, "AptOnlineLogin::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x97A413
		if (!w)
		{
			return;
		}
		if (name == "OnlineLogin::Password")
		{
			// 16 characters; disabled: the guest sign-in has no password [S-4071]
			candidate(m_password, w);
			GadgetTextEntrySetText(w, UnicodeString());
			w->winEnable(false);
		}
		else if (name == "OnlineLogin::Nickname")
		{
			// a combo of the cached nicknames, 15 characters (RW 0x97A494): here the guest name the player used last
			candidate(m_nickname, w);
			GadgetComboBoxReset(w);
			GadgetComboBoxSetMaxChars(w, 15);
			GadgetComboBoxSetIsEditable(w, true);
			const std::u16string last = m_ctx ? m_ctx->rememberedName : std::u16string();
			if (!last.empty())
			{
				GadgetComboBoxAddEntry(w, last, 0xFFFFFFFFu);
				GadgetComboBoxSetSelectedPos(w, 0, false);
				GadgetComboBoxSetText(w, last);
			}
		}
		else if (name == "OnlineLogin::Email")
		{
			candidate(m_email, w);
			GadgetComboBoxReset(w);
			w->winEnable(false); // no e-mail sign-in [S-4071]
		}
		else if (name == "OnlineLogin::RememberInfo")
		{
			candidate(m_remember, w); // "remember me" kept the password; a guest has none: the name is always remembered [S-4071]
			w->winEnable(false);
		}
		else if (name == "OnlineLogin::CountryList")
		{
			// the locale pop-up's list (WOL:Locale01 .. 37); the pop-up is never opened [S-4071]
		}
		// the account features' buttons are disabled through the movie's own DisableButton* (BFME2 0x572632 calls them at the login) once the page
		// shows its fields (update): the shell's CallChild reaches the page only after it became the shell's current movie
		m_loginDisablesDue = 15;
	});
}

void AptOnlineShell::login()
{
	// RW 0x97C5C6 (BFME2 0x572632 -> 0x57179D): the fields are read, a missing one is a box, else the request goes out and the buttons are disabled
	if (m_loginSent)
	{
		return;
	}
	prune();
	const std::u16string name = m_nickname ? trim(GadgetComboBoxGetText(m_nickname)) : std::u16string();
	if (name.empty())
	{
		showError("GUI:GSErrorTitle", "GUI:GSNoLoginInfoNickname");
		return;
	}
	if (!m_client)
	{
		showBox(text("GUI:GSErrorTitle"), u"No online lobby is configured.", "GUI:GSErrorTitle|no lobby");
		return;
	}
	callChild("DisableButtonLogin");
	callChild("DisableButtonDeleteNickname");
	m_loginSent = true;
	m_loginName = name;
	// lane ONLINE-4: the session a restarted game kept (ONLINE-3's user://online_session.txt) signs in as the same player when the name is its own: the
	// lobby then knows the running game to rejoin. Another name is another guest
	if (m_ctx && !m_ctx->sessionToken.empty() && OnlineLobbyClient::sessionName(m_ctx->sessionToken) == name)
	{
		windows().note("online-session", "the kept lobby session of " + narrow(name) + " is used");
		m_client->useSession(m_ctx->sessionToken);
		return;
	}
	m_client->signInGuest(name);
}

// ---- OnlineHome ------------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::registerHome()
{
	const std::string page = "OnlineHome";
	pageCommand(page, "AptOnline::OnlineHome::OnOpened", [this](const std::string &) { fillMotd(); }); // RW 0x9C8ED2
	pageCommand(page, "AptOnline::OnlineHome::OfficialSite", [this](const std::string &) { notAvailable("The ladder site (URL:LotrLadder)", "[S-4072]"); });
	pageScreenRef(page, "AptOnlineHome::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x9C8EDA (BFME2 0x5B9B73): the list box is reset; OnlineHome::MessageOfTheDay is kept and filled
		if (!w)
		{
			return;
		}
		GadgetListBoxReset(w);
		if (name == "OnlineHome::MessageOfTheDay")
		{
			candidate(m_motd, w);
			fillMotd();
		}
	});
	pageProvider(page, "OnlineHome::NumTickerFields", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "12"; // RW 0x9C8350
		}
		return true;
	});
	// the constructor's message-of-the-day request (peer request 0x17)
	if (m_client)
	{
		m_client->fetchMotd();
	}
}

void AptOnlineShell::fillMotd()
{
	prune();
	m_motdLines.clear();
	if (!m_client)
	{
		m_motdLines.push_back("No online lobby is configured.");
	}
	else if (!m_client->motdKnown() && !m_motdError.empty())
	{
		m_motdLines.push_back(narrow(u"The lobby's message of the day could not be read: " + m_motdError)); // Sol r1: kept for a later fill
	}
	else if (!m_client->motdKnown())
	{
		m_motdLines.push_back("..."); // the lobby has not answered yet (the answer refills the list)
	}
	else if (!m_client->hasMotd())
	{
		m_motdLines.push_back(narrow(kNoMotd));
	}
	else
	{
		std::u16string motd = m_client->motd();
		size_t at = 0;
		while (at <= motd.size())
		{
			const size_t nl = motd.find(u'\n', at);
			m_motdLines.push_back(narrow(motd.substr(at, nl == std::u16string::npos ? std::u16string::npos : nl - at)));
			if (nl == std::u16string::npos)
			{
				break;
			}
			at = nl + 1;
		}
	}
	if (!m_motd)
	{
		return;
	}
	GadgetListBoxReset(m_motd);
	for (const std::string &line : m_motdLines)
	{
		std::u16string w;
		OnlineStart::utf8ToUtf16(line, w);
		GadgetListBoxAddEntryText(m_motd, w, 0xFFFFFFFFu, -1, 0);
	}
}

// ---- OnlineOpenPlay (AptOnlineCustomMatch) ----------------------------------------------------------------------------------------------------------------

void AptOnlineShell::registerOpenPlay()
{
	const std::string page = "OnlineOpenPlay";
	m_state = 0;
	m_listDirty = true;
	pageCommand(page, "AptOnline::CustomMatch::CancelPopUpJoin", [this](const std::string &) {
		if (m_state == 10) // RW 0x9A8483
		{
			callPage("ClosePassword");
			m_state = 1;
			m_popUp = false;
		}
	});
	pageCommand(page, "AptOnline::CustomMatch::CreateGame", [this](const std::string &) {
		if (m_state == 2 || m_state == 3) // RW 0x9A8423
		{
			prune();
			if (m_gameName)
			{
				m_gameName->winEnable(false);
			}
			if (m_gamePassword)
			{
				m_gamePassword->winEnable(false);
			}
			m_state = 4;
		}
	});
	pageCommand(page, "AptOnline::CustomMatch::JoinGame", [this](const std::string &) {
		if (m_state == 2) // RW 0x9A845E
		{
			m_state = 9;
		}
	});
	pageCommand(page, "AptOnline::CustomMatch::LoadGame", [this](const std::string &) { notAvailable("Loading a saved online game", "[S-4074]"); });
	pageCommand(page, "AptOnline::CustomMatch::AcceptJoinGame", [this](const std::string &) {
		// RW 0x9AEE9C (BFME2 AptOnlineCustomMatchAcceptJoinGame.cpp)
		if (m_state != 10)
		{
			return;
		}
		prune();
		const std::string password = m_gamePassword ? narrow(GadgetTextEntryGetText(m_gamePassword)) : std::string();
		m_popUp = false;
		if (password.empty())
		{
			callPage("ClosePassword");
			m_state = 1;
			showError("GUI:JoinFailedDefault", "GUI:JoinFailedBadPassword");
			return;
		}
		const auto it = std::find(m_rowIds.begin(), m_rowIds.end(), m_joinId);
		if (it == m_rowIds.end())
		{
			callPage("ClosePassword");
			m_state = 1;
			showError("GUI:JoinFailedDefault", "GUI:HostLeft");
			return;
		}
		requestJoin(m_joinId, password);
	});
	pageCommand(page, "AptOnline::CustomMatch::PlayGame", [this](const std::string &) { m_state = 7; }); // RW 0x9A8416
	pageCommand(page, "AptOnline::CustomMatch::Cancel", [this](const std::string &) {
		// BFME2 0x59FC45: state 8 only clears the start flag; else the room is left and the state 0
		if (m_state == 8)
		{
			return;
		}
		if (m_client && m_client->inGame())
		{
			m_client->leaveGame();
		}
		backToList();
	});
	pageCommand(page, "AptOnline::CustomMatch::Refresh", [this](const std::string &) {
		if (m_client) // RW 0x9AA76A: the list asked for again
		{
			m_client->refreshList(true);
			m_client->fetchMine();
		}
	});
	pageCommand(page, "AptOnline::OnOpenCreateDialog", [this](const std::string &) {
		// RW 0x9AA774 (BFME2 0x5A0F1F): FillGameNameTextEntry(true), the focus to the entry, state 3
		fillCreateEntries();
		m_state = 3;
	});
	pageCommand(page, "AptOnline::CustomMatch::CancelPopUpCreate", [this](const std::string &) {
		m_state = 1; // RW 0x9A84C9
		m_popUp = false;
	});
	pageCommand(page, "AptOnline::CustomMatch::CancelPopUpHost", [this](const std::string &) {
		callPage("ClosePassword"); // RW 0x9A84DD
		m_state = 1;
		m_popUp = false;
	});
	// lane ONLINE-5: the CONNECTIONS button: the page's OpenConnectionScreen closes the room's panels, loads Connections.swf and sends
	// OnOpenConnectionsScreen. Retail's screen shows the GameSpy connection (NAT negotiation) between every two players; OpenBFME's players do not
	// connect to each other (every one talks to the relay), so there is nothing for it to show [S-4550]: the player is told, and the room's panels
	// come back through the page's own close (StartCloseConnectionsScreen reopens them, OnConnectionsScreenClosed unloads the movie). The handler
	// used to do nothing: the room's panels went and an empty screen stayed
	pageCommand(page, "AptOnline::CustomMatch::OnOpenConnectionsScreen", [this](const std::string &) {
		m_closeConnections = 2; // the page's own functions run from the next updates (not inside the button's script)
		notAvailable("the Connections screen (online games go through the relay server, players do not connect to each other)", "[S-4550]");
	});
	pageCommand(page, "AptOnline::CustomMatch::OnClosingConnectionsScreen", [](const std::string &) {});
	// GameSorter (RW 0x98821C ..): the columns of the list
	pageCommand(page, "GameSorter::OnSortName", [this](const std::string &) { sortBy(0); });
	pageCommand(page, "GameSorter::OnSortMap", [this](const std::string &) { sortBy(1); });
	pageCommand(page, "GameSorter::OnSortPlayers", [this](const std::string &) { sortBy(2); });
	pageCommand(page, "GameSorter::OnSortPing", [this](const std::string &) { sortBy(3); });
	pageCommand(page, "GameSorter::OnSortStatus", [this](const std::string &) { sortBy(4); });
	pageScreenRef(page, "AptOnlineCustomMatch::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x9AA4C3
		if (!w)
		{
			return;
		}
		if (name == "GameList")
		{
			candidate(m_gameList, w);
			m_listDirty = true;
		}
		else if (name == "Lobbies")
		{
			candidate(m_lobbies, w); // GameSpy's group rooms: one lobby here [S-4073]
			w->winEnable(false);
		}
		else if (name == "GameInfo")
		{
			candidate(m_gameInfo, w);
		}
		else if (name == "GameName" || name == "GamePassword")
		{
			GameWindow *&slot = name == "GameName" ? m_gameName : m_gamePassword;
			candidate(slot, w);
			if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
			{
				entry->maxTextLen = 20; // RW 0x81606D(entry, 0x14)
			}
			// the pop-ups' own entries come after OnOpenCreateDialog / EnterPassword (the page places them as it shows the pop-up): RW 0x9AA5AB calls
			// RW 0x9AA0A7 at every GameName / GamePassword placement. INFERENCE (S-4074): it fills them as FillGameNameTextEntry (BFME2 0x5A0DC2) does
			if (m_state == 3)
			{
				fillCreateEntries();
			}
			else if (m_state == 10)
			{
				fillJoinEntries();
			}
		}
	});
	if (m_client)
	{
		m_client->refreshList(true);
		if (m_client->signedIn())
		{
			m_client->fetchMine(); // lane ONLINE-4: the player's running games are rows too (REJOIN)
		}
	}
}

void AptOnlineShell::fillCreateEntries()
{
	// BFME2 0x5A0DC2 FillGameNameTextEntry(true): the game name is the player's (GameModePreferences' last name for it, rva0044DAA8: here the account's
	// name), the password the preferences' (here empty), both entries enabled
	prune();
	if (m_gameName)
	{
		GadgetTextEntrySetText(m_gameName, m_client ? m_client->accountName() : std::u16string());
		m_gameName->winEnable(true);
	}
	if (m_gamePassword)
	{
		GadgetTextEntrySetText(m_gamePassword, UnicodeString());
		m_gamePassword->winEnable(true);
	}
}

void AptOnlineShell::fillJoinEntries()
{
	// BFME2 0x5A0DC2 FillGameNameTextEntry(false): the game to join's name, the name entry disabled; the password entry empty and enabled
	prune();
	std::u16string name;
	if (m_client)
	{
		for (const OnlineLobbyClient::Listing &g : m_client->games())
		{
			name = g.id == m_joinId ? g.name : name;
		}
	}
	if (m_gameName)
	{
		GadgetTextEntrySetText(m_gameName, name);
		m_gameName->winEnable(false);
	}
	if (m_gamePassword)
	{
		GadgetTextEntrySetText(m_gamePassword, UnicodeString());
		m_gamePassword->winEnable(true);
	}
}

GameWindow *AptOnlineShell::gameList()
{
	prune();
	return m_gameList;
}
GameWindow *AptOnlineShell::nicknameWindow()
{
	prune();
	return m_nickname;
}
GameWindow *AptOnlineShell::gameNameWindow()
{
	prune();
	return m_gameName;
}
GameWindow *AptOnlineShell::gamePasswordWindow()
{
	prune();
	return m_gamePassword;
}

void AptOnlineShell::candidate(GameWindow *&role, GameWindow *w)
{
	role = w;
	m_candidates[&role].push_back(w);
}

void AptOnlineShell::prune()
{
	// the pages place their gadgets twice (a CreateComponents frame, then the page's own; the login page in two clips): the windows of a removed clip
	// stay, hidden (WindowManager::componentInstanceDestroyed). A role takes the last live window that is shown, else the last live one (as
	// AptLanLobby::pruneLan follows a gadget's live twin)
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer)
	{
		return;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	const std::set<const GameWindow *> alive(live.begin(), live.end());
	for (GameWindow **w : { &m_nickname, &m_email, &m_password, &m_remember, &m_motd, &m_gameList, &m_lobbies, &m_gameInfo, &m_gameName, &m_gamePassword })
	{
		std::vector<GameWindow *> &cands = m_candidates[w];
		cands.erase(std::remove_if(cands.begin(), cands.end(), [&](GameWindow *c) { return !alive.count(c); }), cands.end());
		GameWindow *best = cands.empty() ? nullptr : cands.back();
		for (GameWindow *c : cands)
		{
			if (!c->winIsHidden())
			{
				best = c; // the last one shown
			}
		}
		if (*w != best)
		{
			if (w == &m_gameList)
			{
				m_listDirty = true;
			}
			*w = best;
		}
	}
	pruneDeadGadgets();
}

void AptOnlineShell::sortBy(int column)
{
	// GameSorter: a second press of the same column reverses it (INFERENCE: RotWK's sorter RW 0x988BB5 not read, S-4073)
	m_sortDescending = m_sortColumn == column ? !m_sortDescending : false;
	m_sortColumn = column;
	m_listDirty = true;
}

void AptOnlineShell::fillGamesList()
{
	prune();
	if (!m_gameList || !m_client)
	{
		return;
	}
	m_listDirty = false;
	int selected = -1;
	GadgetListBoxGetSelected(m_gameList, &selected);
	const std::string keep = selected >= 0 && selected < (int)m_rowIds.size() ? m_rowIds[(size_t)selected] : std::string();
	// lane ONLINE-4 (S-4077): the player's own running games first (REJOIN), then the lobby's games: open ones (Join Game) and running ones of others
	// (WATCH: a spectator START)
	std::vector<OnlineLobbyClient::Listing> games = m_client->mine();
	for (OnlineLobbyClient::Listing &g : games)
	{
		g.state = "rejoin";
	}
	for (const OnlineLobbyClient::Listing &g : m_client->games())
	{
		if (std::none_of(games.begin(), games.end(), [&](const OnlineLobbyClient::Listing &m) { return m.id == g.id; }))
		{
			games.push_back(g);
			games.back().state = g.state == "started" ? "watch" : "join";
		}
	}
	std::map<std::string, std::u16string> titles;
	for (const OnlineLobbyClient::Listing &g : games)
	{
		if (!titles.count(g.map))
		{
			titles[g.map] = mapTitle(g.map);
		}
	}
	if (m_sortColumn >= 0)
	{
		const int col = m_sortColumn;
		const bool desc = m_sortDescending;
		std::stable_sort(games.begin(), games.end(), [col, desc, &titles](const OnlineLobbyClient::Listing &a, const OnlineLobbyClient::Listing &b) {
			int c = 0;
			switch (col)
			{
			case 0: c = a.name < b.name ? -1 : (b.name < a.name ? 1 : 0); break;
			case 1: c = titles.at(a.map) < titles.at(b.map) ? -1 : (titles.at(b.map) < titles.at(a.map) ? 1 : 0); break;
			case 2: c = a.players - b.players; break;
			case 4: c = (int)a.password - (int)b.password; break;
			default: c = 0; break; // the lobby gives no ping [S-4073]
			}
			return desc ? c > 0 : c < 0;
		});
	}
	GadgetListBoxReset(m_gameList);
	if (GadgetListBoxGetNumColumns(m_gameList) < 7)
	{
		static const int widths[7] = { 4, 5, 5, 36, 33, 10, 7 }; // BFME2 0x59F496
		GadgetListBoxSetColumnWidths(m_gameList, 7, widths);
	}
	m_gameRows.clear();
	m_rowIds.clear();
	m_rowKinds.clear();
	for (const OnlineLobbyClient::Listing &g : games)
	{
		const Color c = g.state == "join" ? 0xFFFFFFFFu : 0xFFFFD27Fu; // a running game's row in the gold of the movie's highlighted text
		const std::u16string label = g.state == "rejoin" ? g.name + u"  - REJOIN" : g.state == "watch" ? g.name + u"  - WATCH" : g.name;
		const int row = GadgetListBoxAddEntryText(m_gameList, label, c, -1, 3);
		if (g.password)
		{
			GadgetListBoxAddEntryImage(m_gameList, "AptLock", row, 2, 20, 20, true); // BFME2 0x59F496: the lock of a locked game
		}
		const std::u16string &map = titles.at(g.map);
		const std::string players = std::to_string(g.players) + "/" + std::to_string(g.players + g.openSlots);
		GadgetListBoxAddEntryText(m_gameList, map, c, row, 4);
		GadgetListBoxAddEntryText(m_gameList, wide(players), c, row, 5);
		GadgetListBoxAddEntryText(m_gameList, UnicodeString(), c, row, 6);
		m_gameRows.push_back(narrow(g.name) + "|" + narrow(map) + "|" + players + "|" + (g.password ? "lock" : "") + (g.state == "join" ? "" : "|" + g.state));
		m_rowIds.push_back(g.id);
		m_rowKinds.push_back(g.state);
		if (!keep.empty() && g.id == keep)
		{
			GadgetListBoxSetSelected(m_gameList, row);
		}
	}
}

std::u16string AptOnlineShell::mapTitle(const std::string &key)
{
	// BFME2 0x59F496 (the list's mode 0): the game's map found in the map cache (AptMapPreview 0x57D922) and its
	// MapMetaData::bfme_getBaseDisplayName (RotWK 0x00300AEA: the map.str / game text label), as the skirmish map list shows it
	if (const SkirmishGameSetup *s = setup())
	{
		if (const MapCacheEntry *m = s->findMap(key))
		{
			return s->mapBaseDisplayName(*m);
		}
	}
	// a map the player does not have (retail's lookup has no answer for it): its file name, noted once
	if (m_unknownMaps.insert(key).second)
	{
		windows().note("online-map-unknown", "the listed map " + key + " is not in this player's map cache: shown by its file name");
	}
	return wide(mapLeaf(key));
}

bool AptOnlineShell::startUsable(const std::string &kind, std::string *why) const
{
	// Sol r1: the START is read as the host program will read it (GodotGameWorld::net_online: OnlineStart::parse, a spectator's as one) before it is
	// handed on: a malformed one is a lobby error the player is told about, never the game's end
	if (!m_client)
	{
		*why = "no lobby client";
		return false;
	}
	ProfileIdentity own;
	own.digest = m_client->options().profileDigest;
	OnlineStart::Limits limits;
	limits.playerTemplateCount = m_client->options().playerTemplateCount;
	limits.colorCount = m_client->options().colorCount;
	LobbyStart start;
	return OnlineStart::parse(m_client->startJson(), own, limits, start, why, kind == "spectate" ? OnlineStart::Kind::Spectate : OnlineStart::Kind::Start);
}

void AptOnlineShell::startFailed(const std::string &why)
{
	const std::string safe = OnlineLobbyClient::redact(why);
	windows().note("online-start-failed", safe);
	m_rejoinPending = false;
	if (m_client)
	{
		m_client->dropStart(); // no leave: the player keeps the slot (a running game is offered for a rejoin)
	}
	const std::u16string body = u"The online game could not start: " + std::u16string(safe.begin(), safe.end());
	if (m_pages.count("OnlineOpenPlay"))
	{
		backToList();
	}
	m_connecting = false;
	showBox(text("GUI:GSErrorTitle"), body, "GUI:GSErrorTitle|" + narrow(body));
}

void AptOnlineShell::joinSelected()
{
	// BFME2 0x5A57BA (fromInvite false)
	prune();
	m_state = 1;
	int selected = -1;
	if (m_gameList)
	{
		GadgetListBoxGetSelected(m_gameList, &selected);
	}
	if (selected < 0 || selected >= (int)m_rowIds.size())
	{
		showError("GUI:Error", "GUI:NoGameSelected");
		return;
	}
	const std::string id = m_rowIds[(size_t)selected];
	const std::string kind = selected < (int)m_rowKinds.size() ? m_rowKinds[(size_t)selected] : std::string("join");
	if (kind == "rejoin" || kind == "watch")
	{
		// lane ONLINE-4 (S-4077): Join Game on the player's running game asks for a rejoin START, on another running game for a spectator START
		startRunning(id, kind == "rejoin");
		return;
	}
	const auto &games = m_client->games();
	const auto it = std::find_if(games.begin(), games.end(), [&](const OnlineLobbyClient::Listing &g) { return g.id == id; });
	if (it == games.end())
	{
		showError("GUI:Error", "GUI:NoGameInfo");
		return;
	}
	// the exe / INI CRC check is the lobby's profile comparison at the join (a refusal is its "profile" error)
	if (it->openSlots == 0)
	{
		showError("GUI:JoinFailedDefault", "GUI:JoinFailedRoomFull");
		return;
	}
	m_joinId = id;
	if (it->password)
	{
		m_popUp = true;
		callPage("EnterPassword");
		m_state = 10;
		fillJoinEntries();
		return;
	}
	requestJoin(id, std::string());
}

void AptOnlineShell::startRunning(const std::string &id, bool rejoin)
{
	if (!m_client)
	{
		return;
	}
	windows().note("online-rejoin", std::string(rejoin ? "rejoin" : "watch") + " game " + id + " [S-4077]");
	if (rejoin)
	{
		m_client->rejoinGame(id);
	}
	else
	{
		m_client->spectateGame(id);
	}
	m_rejoinPending = true;
	if (m_pages.count("OnlineOpenPlay"))
	{
		m_state = 13; // OpenBFME's (S-4077): waiting for the running game's START
		m_connecting = true;
		callPage("PopUpConnectingOpen");
	}
}

void AptOnlineShell::offerRejoin()
{
	// lane ONLINE-4 (S-4077): RotWK's movies have no Rejoin button; on sign-in the player's running game is offered in GuiFX's own Yes / No box
	if (!m_client || m_client->mine().empty() || m_rejoinOffered)
	{
		return;
	}
	m_rejoinOffered = true;
	const OnlineLobbyClient::Listing g = m_client->mine().front();
	const std::u16string body = u"Your game \"" + g.name + u"\" is still running. Rejoin it now?";
	m_boxes.push_back("rejoin-offer|" + narrow(body));
	windows().note("online-box", "rejoin-offer|" + narrow(body));
	box().show(AptMessageBox::TYPE_YES_NO, kRejoinTitle, body, [this, id = g.id](int button) {
		if (button == 2) // Yes
		{
			startRunning(id, true);
		}
	});
}

void AptOnlineShell::requestJoin(const std::string &id, const std::string &password)
{
	// BFME2 0x5A2C77 RequestJoinGame: the request, state 11
	m_client->joinGame(id, password);
	m_state = 11;
	m_popUp = true;
}

void AptOnlineShell::syncModel()
{
	SkirmishGameSetup *model = setupModel();
	if (!model || !m_client || !m_client->inGame())
	{
		return;
	}
	model->setNetwork(m_client->localSlot());
	model->replaceInfo(m_client->gameInfo());
	m_syncedSeq = m_client->viewSeq();
	refreshLobby();
}

void AptOnlineShell::enterSetup(bool host)
{
	// BFME2 0x5A20A7: the staging room: state 5 -> GotoAndPlay("_host"), 6; state 11 -> ClosePassword, GotoAndPlay("_join"), 12
	setHostModeText(host ? "1" : "0");
	m_ready = false;
	m_startSent = false;
	m_chat.clear();
	syncModel();
	if (host)
	{
		callPage("GotoAndPlay", { "_host" });
		m_state = 6;
	}
	else
	{
		callPage("ClosePassword");
		callPage("GotoAndPlay", { "_join" });
		m_state = 12;
	}
	m_popUp = false;
}

void AptOnlineShell::backToList()
{
	// RW 0x9AB748 (BFME2 0x5A1EEE, byte-matched): the room left, ClosePassword, GotoAndPlay("_lobby"), EnableButtonCreateGame, DisableButtonJoinGame,
	// state 1
	callPage("ClosePassword");
	callPage("GotoAndPlay", { "_lobby" });
	callPage("EnableButtonCreateGame");
	callPage("DisableButtonJoinGame");
	m_state = 1;
	m_ready = false;
	m_startSent = false;
	m_popUp = false;
	if (m_connecting)
	{
		callPage("PopUpConnectingClose");
		m_connecting = false;
	}
	setHostModeText("1");
	m_listDirty = true;
	if (m_client)
	{
		m_client->refreshList(true);
	}
}

void AptOnlineShell::systemChat(const std::u16string &line)
{
	addChatLine(line, 0xFFFFFFFFu);
	m_chat.push_back(narrow(line));
}

void AptOnlineShell::startPressed()
{
	// state 7 (PlayGame): the host checks the setup as the LAN lobby does (RW 0x8431B8) and asks the lobby for the START; a joiner's Play Game is its ready
	if (!m_client || !m_client->inGame())
	{
		return;
	}
	if (!m_client->amIHost())
	{
		onReadyPress("PlayGame");
		return;
	}
	if (SkirmishGameSetup *model = setupModel())
	{
		// lane ONLINE-5: with the screen's own flags (0x20: a player who is not ready refuses the start with GUI:NotifiedStartIntent, RW 0x8431B8's
		// last test). The owner's vslot 0x44 (BFME2 0x59F043 MpOwnerPrintMessage): kind 2 is a line every player of the room gets, the others are local
		const SkirmishGameSetup::StartCheck check = model->checkStart(setupFlags());
		for (const SkirmishGameSetup::StartCheck::Line &line : check.lines)
		{
			if (line.kind == 2)
			{
				m_client->chat(line.text);
			}
			else
			{
				systemChat(line.text);
			}
		}
		if (!check.ok)
		{
			if (!check.refusal.empty())
			{
				m_startRefusals.push_back(check.refusalLabel);
			}
			windows().note("online-start-refused", check.refusalLabel + ": " + narrow(check.refusal));
			callPage("EnableButtonPlayGame");
			return;
		}
	}
	m_startSent = true;
	m_connecting = true;
	callPage("PopUpConnectingOpen"); // BFME2 0x59F37C: the connecting pop-up while the game is set up
	m_client->start();
}

// ---- the lobby client -----------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::pumpClient()
{
	if (!m_client)
	{
		return;
	}
	m_client->update(NetMilliseconds());
	for (const OnlineLobbyClient::Event &e : m_client->takeEvents())
	{
		switch (e.kind)
		{
		case OnlineLobbyClient::Event::SignedIn:
			m_loginSent = false;
			if (m_ctx)
			{
				m_ctx->sessionToken = m_client->sessionToken();
				if (m_ctx->rememberSession)
				{
					m_ctx->rememberSession(m_ctx->sessionToken);
				}
			}
			if (!e.flag)
			{
				m_client->fetchMine(); // a kept session fetched them already
			}
			if (m_ctx && m_ctx->rememberName)
			{
				m_ctx->rememberName(e.text);
				m_ctx->rememberedName = e.text;
			}
			else
			{
				windows().note("online-name-not-kept", "the host keeps no guest name [S-4071]");
			}
			loadChildScreen("OnlineHome"); // BFME2 0x5700A0: a login that succeeded loads the home page
			break;
		case OnlineLobbyClient::Event::Mine:
			m_listDirty = true;
			offerRejoin();
			break;
		case OnlineLobbyClient::Event::Motd:
			fillMotd();
			break;
		case OnlineLobbyClient::Event::GameList:
			m_listDirty = true;
			break;
		case OnlineLobbyClient::Event::GameCreated:
			if (m_state == 4 || m_state == 5)
			{
				m_state = 5;
				enterSetup(true);
			}
			break;
		case OnlineLobbyClient::Event::GameJoined:
			if (m_state == 11)
			{
				enterSetup(false);
			}
			break;
		case OnlineLobbyClient::Event::GameUpdated:
			if (m_pollTrouble && m_client->inGame())
			{
				m_pollTrouble = false; // Sol r1: the lobby answers again
				systemChat(u"The lobby answers again.");
			}
			// a poll brings the view every second: the gadgets follow it only when the lobby changed it (its seq moved)
			if ((m_state == 6 || m_state == 12 || m_state == 7) && m_client->viewSeq() != m_syncedSeq)
			{
				syncModel();
			}
			break;
		case OnlineLobbyClient::Event::Chat:
		{
			// a player's line "[name] text", the lobby's own lines as system lines (as LANAPI::OnChat, RW 0x649E5C)
			const std::u16string line = e.flag ? e.text : u"[" + e.from + u"] " + e.text;
			addChatLine(line, 0xFFFFFFFFu);
			m_chat.push_back(narrow(line));
			break;
		}
		case OnlineLobbyClient::Event::Removed:
			if (e.code == "kicked")
			{
				showError("GUI:GSErrorTitle", "GUI:GSKicked"); // as the LAN lobby's state 9
				backToList();
			}
			else if (e.code == "closed")
			{
				showError("GUI:GSErrorTitle", "GUI:HostLeft");
				backToList();
			}
			break;
		case OnlineLobbyClient::Event::GameStart:
		{
			std::string why;
			if (!startUsable(e.code, &why))
			{
				startFailed(why);
				break;
			}
			if (environment().services)
			{
				environment().services->request(ShellRequest{ ShellAction::OnlineGameStart, m_client->startJson() });
			}
			m_box.reset(); // the box goes with the shell (its GuiFX.apt level is the game's, as the LAN lobby's start)
			break;
		}
		case OnlineLobbyClient::Event::Refused:
		{
			const std::string record = e.request + ": " + e.code + ": " + narrow(e.text);
			windows().note("online-refused", record);
			if (e.request == "session")
			{
				// the kept session is not this lobby's any more (expired, another lobby): a new guest of the same name (it has no running game)
				windows().note("online-session", "the kept lobby session was refused (" + e.code + "): signing in as a new guest");
				if (m_ctx)
				{
					m_ctx->sessionToken.clear();
				}
				m_client->signInGuest(m_loginName);
			}
			else if (e.request == "rejoin" || e.request == "spectate")
			{
				m_rejoinPending = false;
				if (m_connecting)
				{
					callPage("PopUpConnectingClose");
					m_connecting = false;
				}
				if (m_state == 13)
				{
					m_state = 1;
				}
				showBox(kRejoinTitle, e.text, "rejoin-refused|" + narrow(e.text));
			}
			else if (e.request == "signin")
			{
				m_loginSent = false;
				callChild("EnableButtonLogin");
				showBox(text("GUI:GSErrorTitle"), e.text, "GUI:GSErrorTitle|" + narrow(e.text));
			}
			else if (e.request == "motd")
			{
				m_motdError = e.text;
				fillMotd();
			}
			else if (e.request == "create")
			{
				prune();
				if (m_gameName)
				{
					m_gameName->winEnable(true);
				}
				if (m_gamePassword)
				{
					m_gamePassword->winEnable(true);
				}
				m_state = 3;
				showBox(text("GUI:GSErrorTitle"), e.text, "GUI:GSErrorTitle|" + narrow(e.text));
			}
			else if (e.request == "join")
			{
				callPage("ClosePassword");
				m_state = 1;
				m_popUp = false;
				showBox(text("GUI:JoinFailedDefault"), e.text, "GUI:JoinFailedDefault|" + narrow(e.text));
			}
			else if (e.request == "start")
			{
				m_startSent = false;
				if (m_connecting)
				{
					callPage("PopUpConnectingClose");
					m_connecting = false;
				}
				callPage("EnableButtonPlayGame");
				if (e.code == "no_relay")
				{
					// docs/ONLINE.md section 5 step 5: a lobby without a relay refuses the START; the player is told plainly [S-4074]
					showBox(kNoRelayTitle, kNoRelay, "no_relay|" + narrow(kNoRelay));
				}
				else
				{
					showBox(text("GUI:GSErrorTitle"), e.text, "GUI:GSErrorTitle|" + narrow(e.text));
				}
			}
			else if (e.request == "list")
			{
				showBox(text("GUI:GSErrorTitle"), e.text, "GUI:GSErrorTitle|" + narrow(e.text));
			}
			else if (e.request == "poll")
			{
				// Sol r1: the staging room lost the lobby (it keeps asking every second): the player is told once, in the room's chat
				if (!m_pollTrouble && m_client->inGame())
				{
					m_pollTrouble = true;
					systemChat(u"The lobby does not answer: " + e.text + u" (still trying)");
				}
			}
			else if (e.request != "leave")
			{
				// a slot / setup / ready / chat / kick the lobby refused: a system line in the staging room
				systemChat(e.text);
			}
			break;
		}
		}
	}
}

void AptOnlineShell::update()
{
	prune();
	pumpClient();
	serviceReadyBoxes(); // lane ONLINE-5: RW 0x846BA4 (the setup panel's update: + 0x2B9)
	if (m_box)
	{
		m_box->update();
	}
	// BFME2 0x51805B: a page asked for goes to the movie's LoadScreen
	if (!m_pendingScreen.empty())
	{
		std::string err;
		if (level() < 0 || !windows().invokeAS(level(), "LoadScreen", { m_pendingScreen }, nullptr, &err))
		{
			windows().note("movie-function-missing", "_root.LoadScreen(" + m_pendingScreen + "): " + err);
		}
		m_pendingScreen.clear();
	}
	if (m_loginDisablesDue > 0 && m_currentName == "OnlineLogin" && m_nickname && !m_nickname->winIsHidden() && --m_loginDisablesDue == 0)
	{
		// OpenBFME (S-4071): the guest sign-in has no account: Create, Delete nickname, Register and Service terms stay disabled
		windows().note("online-login", "the account buttons are disabled (S-4071)");
		callChild("DisableButtonCreate");
		callChild("DisableButtonDeleteNickname");
		callChild("DisableButtonRegister");
		callChild("DisableButtonServiceTerms");
	}
	if (!m_pages.count("OnlineOpenPlay"))
	{
		return;
	}
	if (m_closeConnections > 0 && --m_closeConnections == 0)
	{
		invokeRoot("StartCloseConnectionsScreen"); // lane ONLINE-5 (S-4550): the room's panels back (the page defines both on _root)
		invokeRoot("OnConnectionsScreenClosed");
	}
	switch (m_state)
	{
	case 0:
		// RW 0x9AFAD5: once GameSpy is connected ([0xDEBE6C] == 1, here: the lobby client) the room is left (RW 0x9AB748), which does its work only
		// once the GameList and Lobbies gadgets exist (+ 0x48C, + 0x490): ClosePassword, GotoAndPlay("_lobby"), ..., state 1
		prune();
		if (m_client && m_gameList && m_lobbies)
		{
			backToList();
		}
		break;
	case 1:
		// RW 0x9AFA99: the page's OnStartLobby, state 2 (the list)
		callPage("OnStartLobby");
		m_state = 2;
		m_listDirty = true;
		break;
	case 2:
	case 3:
		if (m_listDirty)
		{
			fillGamesList();
		}
		break;
	case 4:
	{
		// the create pop-up's accept (RW 0x9A8423 -> state 4): the lobby game with the player's name and the password
		prune();
		std::u16string name = m_gameName ? trim(GadgetTextEntryGetText(m_gameName)) : std::u16string();
		if (name.empty() && m_client)
		{
			name = m_client->accountName();
		}
		const std::string password = m_gamePassword ? narrow(GadgetTextEntryGetText(m_gamePassword)) : std::string();
		SkirmishGameSetup *model = setupModel();
		if (!m_client || !model)
		{
			windows().note("online-create-failed", "no lobby client or no setup model");
			m_state = 2;
			break;
		}
		// the host's slot 0, every other slot open (as the LAN lobby's host start)
		SkirmishGameInfo info = model->info();
		info.slots[0].state = SLOT_PLAYER;
		for (int s = 1; s < MAX_SLOTS; ++s)
		{
			info.slots[s] = SkirmishGameSlot();
			info.slots[s].state = SLOT_OPEN;
		}
		m_client->createGame(name, password, info, 0);
		m_state = 5;
		break;
	}
	case 7:
		startPressed();
		m_state = m_client && m_client->amIHost() ? 6 : 12;
		break;
	case 9:
		joinSelected();
		break;
	default:
		break;
	}
}

// ---- the staging room (MpGameSetup on the lobby game) -----------------------------------------------------------------------------------------------------

WindowMsgHandledType AptOnlineShell::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	prune();
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	if (gadget && gadget == m_gameList)
	{
		if (msg == GLM_DOUBLE_CLICKED && m_state == 2)
		{
			if ((int)(std::intptr_t)data2 >= 0)
			{
				GadgetListBoxSetSelected(m_gameList, (int)(std::intptr_t)data2);
			}
			m_state = 9; // as JoinGame
		}
		return MSG_HANDLED;
	}
	if (gadget && (gadget == m_nickname || gadget == m_motd || gadget == m_lobbies || gadget == m_gameInfo || gadget == m_gameName || gadget == m_gamePassword))
	{
		return MSG_HANDLED;
	}
	return AptSkirmish::gadgetMessage(from, msg, data1, data2);
}

void AptOnlineShell::lobbyChanged()
{
	// the gadgets changed the model: the differences go to the lobby (the host's setup and the slots that are not players', a player's own slot)
	SkirmishGameSetup *model = setupModel();
	if (!m_client || !model || !m_client->inGame())
	{
		return;
	}
	const SkirmishGameInfo &now = model->info();
	const SkirmishGameInfo &was = m_client->gameInfo();
	if (m_client->amIHost() && (now.mapName != was.mapName || now.startingCash != was.startingCash || now.superweaponRestriction != was.superweaponRestriction ||
								   now.legendaryHeroes != was.legendaryHeroes || now.rules != was.rules))
	{
		m_client->setSetup(now);
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &a = now.slots[i];
		const SkirmishGameSlot &b = was.slots[i];
		const bool changed = a.state != b.state || a.playerTemplate != b.playerTemplate || a.color != b.color || a.startPos != b.startPos ||
			a.teamNumber != b.teamNumber;
		if (!changed)
		{
			continue;
		}
		if (i == m_client->localSlot())
		{
			m_client->setOwnSlot(a.playerTemplate, a.color, a.startPos, a.teamNumber);
		}
		else if (m_client->amIHost() && !b.isHuman())
		{
			if (a.state == SLOT_PLAYER)
			{
				continue; // a player's place is taken by a join, never set
			}
			m_client->setOtherSlot(i, a.state, a.playerTemplate, a.color, a.startPos, a.teamNumber);
		}
	}
}

bool AptOnlineShell::lobbyEditable(int slot) const
{
	if (!m_client || !m_client->inGame())
	{
		return false;
	}
	if (slot < 0)
	{
		return m_client->amIHost();
	}
	if (slot == m_client->localSlot())
	{
		return true;
	}
	return m_client->amIHost() && !m_client->gameInfo().slots[slot].isHuman();
}

void AptOnlineShell::onReadyPress(const std::string &argument)
{
	// MpGameSetup::OnReadyPress (RW 0x841590 region; BFME2 0x43EFD3, decomp MpGameSetupSlots.cpp AptMpGameSetup::OnReadyPress): without an argument the
	// movie's Ready boxes were just made and get their states; with one, the slot it names (1 .. 7: the row of the box pressed) asks to toggle its ready
	// state and the answer goes to the owner (BFME2 0x5A1DCE MpOwnerOnReadyChecked: a joined player's READY / UNREADY; the host has none). Only the local
	// player's own box takes a press in the movie (RW 0x84211D: the others are "_disabled..."); a press naming another slot is refused by name.
	// NOT PORTED: the clan game's checks before a ready (rules mode 1: CLAN:ErrorMissingClanAffiliation / CLAN:ErrorOnlyTwoClans): no clans here
	if (argument.empty() || argument == "undefined")
	{
		readyBoxesMade();
		return;
	}
	if (!m_client || !m_client->inGame())
	{
		return;
	}
	const int local = m_client->localSlot();
	int index = local;
	if (argument != "PlayGame") // startPressed: a joiner's Play Game command is its own Ready
	{
		char *end = nullptr;
		const long v = std::strtol(argument.c_str(), &end, 10);
		if (end == argument.c_str() || *end != '\0' || v <= 0 || v >= MAX_SLOTS)
		{
			windows().note("online-ready-refused", "MpGameSetup::OnReadyPress(" + argument + "): not a player slot (1 .. 7)");
			return;
		}
		index = (int)v;
	}
	if (index != local || m_client->amIHost())
	{
		windows().note("online-ready-refused", "MpGameSetup::OnReadyPress(" + argument + "): only the local player's own Ready box (slot " + std::to_string(local) +
													 ") takes a press, and the host has none");
		updateReadyIcon(index, true); // the box pressed goes back to what the lobby says
		return;
	}
	m_ready = !m_client->slotViews()[(size_t)local].ready;
	m_client->setReady(m_ready);
}

bool AptOnlineShell::applySlotHero(int slot, const CreateAHeroHero *hero)
{
	(void)slot;
	if (hero)
	{
		windows().note("create-a-hero-lobby", "a Create-a-Hero in an online game: the lobby service does not carry the record yet [S-4076]");
	}
	return false;
}

void AptOnlineShell::onKickPlayer(const std::string &argument)
{
	if (!m_client || !m_client->amIHost() || argument.empty())
	{
		return;
	}
	m_client->kick(std::atoi(argument.c_str()));
}

void AptOnlineShell::onChatSend(const std::u16string &line)
{
	if (!m_client || !m_client->inGame())
	{
		windows().note("command-ignored", "AptMpChat::Send outside an online game");
		return;
	}
	m_client->chat(line);
}

bool AptOnlineShell::showChat() const
{
	return m_client && m_client->inGame();
}

void AptOnlineShell::ruleNotice(const std::u16string &line)
{
	if (m_client && m_client->inGame())
	{
		m_client->chat(line);
	}
	else
	{
		addChatLine(line, 0xFFFFFFFFu);
	}
}

void AptOnlineShell::showRuleWarning(const std::u16string &title, const std::u16string &body)
{
	showBox(title, body, narrow(title) + "|" + narrow(body));
}
