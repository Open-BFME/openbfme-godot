// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GUI/AptScreens/AptOnlineHub.h (lane ONLINEHUB-1).

#include "GameClient/GUI/AptScreens/AptOnlineHub.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/OptionPreferences.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace
{
std::u16string u16(const std::string &utf8)
{
	std::u16string out;
	if (!OnlineStart::utf8ToUtf16(utf8, out))
	{
		out.clear();
		for (char c : utf8)
		{
			out.push_back((unsigned char)c < 0x80 ? (char16_t)c : u'?');
		}
	}
	return out;
}

std::string narrow(const std::u16string &s)
{
	return OnlineLobbyClient::utf16ToUtf8(s);
}

void *itemData(int value)
{
	return reinterpret_cast<void *>(static_cast<std::intptr_t>(value));
}

int itemValue(void *data)
{
	return static_cast<int>(reinterpret_cast<std::intptr_t>(data));
}

// FNV-1a over the match id: every player of a match computes the same map (no pointer, no hash seed)
std::uint32_t fnv(const std::string &s)
{
	std::uint32_t h = 2166136261u;
	for (unsigned char c : s)
	{
		h = (h ^ c) * 16777619u;
	}
	return h;
}

const char *const kListSkin = "window/apt/listbox.wnd";
const char16_t *const kTitle = u"Quick Match";
const char16_t *const kUnavailable = u"Online services unavailable";
constexpr Color kFrameText = 0xFFBFEDFFu; // the options page's label colour (OPTS-3)
constexpr Color kHelpText = 0xFFFFFFFFu;
const char *const kSizes[2] = { "1v1", "2v2" };
const char16_t *const kSizeNames[2] = { u"1 vs 1", u"2 vs 2" };

std::u16string stateName(const std::string &state)
{
	if (state == "upcoming")
	{
		return u"Upcoming";
	}
	if (state == "running")
	{
		return u"Running";
	}
	if (state == "cancelled")
	{
		return u"Cancelled";
	}
	return u"Finished";
}
} // namespace

AptOnlineHub::AptOnlineHub(AptOnlineShell &shell) : m_shell(shell) {}

AptOnlineHub::~AptOnlineHub()
{
	unregisterQuickMatch();
	m_stats.reset();
	// lane ONLINEUX-1: the panel's lettering function (set below, every update while the page is up) captures this hub: the online shell is destroyed
	// when an online game starts (the shell's stack is emptied for the game) without its pages being unloaded first, and the gadget layer, which
	// outlives every screen, called the dead hub's lambda in the game (the in-game HUD draws through the same layer: signal 11 in a Quick Match game,
	// ONLINEUX-1 audit). Taken away here, and only when it is this hub's
	if (AptGadgetLayer *layer = m_shell.windows().gadgetLayer())
	{
		layer->clearDecorator(this);
		// lane MENUFIX-2: the tournaments panel's two list boxes (engine gadgets, not Apt components: nothing else frees them) went only with
		// showPanel(false); leaving the online menu otherwise left them alive and drawn over every later menu (the menu inventory saw
		// "No tournaments are planned." and two scroll bars over Create-a-Hero), owned by the shell's freed owner window
		for (GameWindow *w : { m_list, m_details })
		{
			if (w)
			{
				layer->destroyEngineGadget(w);
			}
		}
		m_list = m_details = nullptr;
	}
}

std::uint64_t AptOnlineHub::now() const
{
	return m_clock ? m_clock() : NetMilliseconds();
}

std::int64_t AptOnlineHub::unixNow() const
{
	return m_unixClock ? m_unixClock() : (std::int64_t)std::time(nullptr);
}

std::string AptOnlineHub::roomPhase() const
{
	switch (m_room.phase)
	{
	case Room::Phase::None: return "none";
	case Room::Phase::Creating: return "creating";
	case Room::Phase::Joining: return "joining";
	case Room::Phase::InRoom: return "in_room";
	case Room::Phase::Starting: return "starting";
	case Room::Phase::Failed: return "failed";
	}
	return "?";
}

// ---- the pages ------------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineHub::loadPage(const std::string &name)
{
	if (name == "Stats")
	{
		if (!m_stats)
		{
			m_stats = std::make_unique<AptStats>(m_shell.windows(), m_shell.environment(), AptStats::Host::Online, m_shell.ownerWindow());
			m_stats->setLevel(m_shell.level());
			m_stats->setMapNames([this](const std::string &key) { return m_shell.mapTitle(key); }); // BFME2 0x59F496's lookup
		}
		return;
	}
	if (name == "OnlineQuickMatch" && !m_qmUp)
	{
		// lane ONLINEUX-1 (S-5143): the page's line said RotWK's "Tournament Play is a stat tracked mode ... raise your rank in the Overall Online
		// Ladder" (APT:TournamentModeDesc): OpenBFME has no ranked ladder, and nothing on the page said where Quick Match is (its left half). The line
		// names the two halves in the player's words; the movie, its labels and its layout are retail's
		m_shell.windows().setAptText("APT:TournamentModeDesc",
			"QUICK MATCH (left): choose a size and army, then JOIN GAME. TOURNAMENTS (right): double-click one.");
		m_qmUp = true;
		registerQuickMatch();
		showPanel(true);
		if (PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr)
		{
			p->listTournaments();
			m_listAsked = true;
		}
	}
}

void AptOnlineHub::unloadPage(const std::string &name)
{
	if (name == "Stats")
	{
		m_stats.reset();
		return;
	}
	if (name == "OnlineQuickMatch" && m_qmUp)
	{
		// the page goes: a search still running is cancelled (BFME2: the page's teardown). A room being made keeps going: the START takes the player
		// into the game wherever the shell is
		if (m_qmState == 1 && m_room.phase == Room::Phase::None)
		{
			cancelSearch(false);
		}
		unregisterQuickMatch();
		showPanel(false);
		m_qmUp = false;
	}
}

void AptOnlineHub::registerQuickMatch()
{
	WindowManager &w = m_shell.windows();
	auto command = [&](const std::string &name, WindowManager::CommandFn fn) {
		if (w.registerCommand(name, std::move(fn)))
		{
			m_commands.push_back(name);
		}
		else
		{
			w.note("register-duplicate", name);
		}
	};
	// RW 0x9C9F30's registrations
	command("AptOnline::OnlineQuickMatch::PlayGame", [this](const std::string &) {
		// BFME2 0x5BA34B: the simple flag, state 1 (searching)
		m_simple = true;
		startSearch();
	});
	command("AptOnline::OnlineQuickMatch::Cancel", [this](const std::string &) { cancelSearch(true); }); // RW 0x9C9E55
	command("AptOnline::OnlineQuickMatch::WidenSearch", [this](const std::string &) {
		// RW 0x9C9DDD: GameSpy's request 0x11. The platform matches the same mode and size, oldest first, and widens nothing else [S-4870]
		m_shell.windows().note("online-qm", "AptOnline::OnlineQuickMatch::WidenSearch: the search already takes every player of the same mode and size [S-4870]");
		m_status = "The search already looks for every player of your mode and size.";
	});
	command("AptOnline::OnlineQuickMatch::StartSimple", [this](const std::string &) { m_simple = true; }); // BFME2 0x5BA344
	command("AptOnline::OnlineQuickMatch::OnFoundMovieDone", [this](const std::string &) {
		// BFME2 0x5BA359: state 2
		m_qmState = 2;
		m_foundShown = false;
	});
	if (w.registerScreenRef("AptOnlineQuickMatch::InitGadgets", [this](const std::string &name, GameWindow *win) { initGadget(name, win); }))
	{
		m_screenRefs.push_back("AptOnlineQuickMatch::InitGadgets");
	}
}

void AptOnlineHub::unregisterQuickMatch()
{
	for (const std::string &n : m_commands)
	{
		m_shell.windows().unregisterCommand(n);
	}
	for (const std::string &n : m_screenRefs)
	{
		m_shell.windows().closeAptScreen(n);
	}
	m_commands.clear();
	m_screenRefs.clear();
	m_color = m_numPlayers = m_side = m_speed = m_ladder = nullptr;
}

void AptOnlineHub::initGadget(const std::string &name, GameWindow *w)
{
	// BFME2 0x5BA94A: each combo emptied and kept; the colour list made (0x5BA803)
	if (!w)
	{
		return;
	}
	ShellEnvironment &env = m_shell.environment();
	if (name == "OnlineQuickMatch::Color")
	{
		m_color = w;
		GadgetImageComboBoxReset(w);
		// BFME2 0x5BA803: AptWhiteBox tinted by each multiplayer colour; OpenBFME puts the lobby's random colour first (AptRandomColor), as the
		// staging room's colour list
		GadgetImageComboBoxSetItemData(w, GadgetImageComboBoxAddEntry(w, "AptRandomColor", 0xFFFFFFFFu), itemData(-1));
		if (env.skirmish)
		{
			const auto &colors = env.skirmish->colors();
			for (int c = 0; c < (int)colors.size(); ++c)
			{
				GadgetImageComboBoxSetItemData(w, GadgetImageComboBoxAddEntry(w, "AptWhiteBox", 0xFF000000u | colors[(std::size_t)c].rgb), itemData(c));
			}
		}
		GadgetImageComboBoxSetSelectedPos(w, 0);
	}
	else if (name == "OnlineQuickMatch::Side")
	{
		m_side = w;
		GadgetComboBoxReset(w);
		const std::u16string random = fetchOrMissing(env.gameText, "GUI:Random");
		GadgetComboBoxSetItemData(w, GadgetComboBoxAddEntry(w, random, 0xFFFFFFFFu), itemData(-1));
		if (env.skirmish)
		{
			const auto &factions = env.skirmish->factions();
			for (int c = 0; c < (int)factions.size(); ++c)
			{
				const SkirmishFaction &f = factions[(std::size_t)c];
				if (f.playableSide && !f.isObserver)
				{
					GadgetComboBoxSetItemData(w, GadgetComboBoxAddEntry(w, fetchOrMissing(env.gameText, f.displayLabel), 0xFFFFFFFFu), itemData(c));
				}
			}
		}
		GadgetComboBoxSetSelectedPos(w, 0, false);
	}
	else if (name == "OnlineQuickMatch::Ladder" || name == "OnlineQuickMatch::NumOfPlayers")
	{
		// RotWK's ladders were 1 vs 1 and 2 vs 2 (LadderRank_1v1 / LadderRank_2v2): the Quick Match sizes [S-4870]
		(name == "OnlineQuickMatch::Ladder" ? m_ladder : m_numPlayers) = w;
		GadgetComboBoxReset(w);
		for (int i = 0; i < 2; ++i)
		{
			GadgetComboBoxSetItemData(w, GadgetComboBoxAddEntry(w, kSizeNames[i], 0xFFFFFFFFu), itemData(i));
		}
		GadgetComboBoxSetSelectedPos(w, 0, false);
	}
	else if (name == "OnlineQuickMatch::ConnectionSpeed")
	{
		// GameSpy's ping limit: the platform matches no connection speed [S-4870]
		m_speed = w;
		GadgetComboBoxReset(w);
		GadgetComboBoxAddEntry(w, u"Any", 0xFFFFFFFFu);
		GadgetComboBoxSetSelectedPos(w, 0, false);
		w->winEnable(false);
	}
}

std::string AptOnlineHub::choices() const
{
	int sizeIndex = 0, side = -1, color = -1, pos = -1;
	if (GameWindow *s = sizeCombo())
	{
		GadgetComboBoxGetSelectedPos(s, &pos);
		if (pos >= 0)
		{
			sizeIndex = itemValue(GadgetComboBoxGetItemData(s, pos)) == 1 ? 1 : 0;
		}
	}
	if (m_side)
	{
		GadgetComboBoxGetSelectedPos(m_side, &pos);
		side = pos >= 0 ? itemValue(GadgetComboBoxGetItemData(m_side, pos)) : -1;
	}
	if (m_color)
	{
		GadgetImageComboBoxGetSelectedPos(m_color, &pos);
		color = pos >= 0 ? itemValue(GadgetImageComboBoxGetItemData(m_color, pos)) : -1;
	}
	std::string faction = "random";
	ShellEnvironment &env = const_cast<AptOnlineShell &>(m_shell).environment();
	if (side >= 0 && env.skirmish && side < (int)env.skirmish->factions().size())
	{
		faction = env.skirmish->factions()[(std::size_t)side].templateName;
	}
	return std::string(kSizes[sizeIndex]) + "|" + faction + "|" + std::to_string(color);
}

// ---- Quick Match -----------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineHub::callChild(const std::string &function)
{
	// the page's own entry point HandleGameCall (BFME2 AptOnlineQuickMatchOptions.cpp: CallChild("DoOpenFound") / "CloseFoundAndReset")
	std::string err;
	if (m_shell.level() < 0 || !m_shell.windows().invokeASAt(m_shell.level(), "OnlineQuickMatch", "HandleGameCall", { function }, nullptr, &err))
	{
		m_shell.windows().note("movie-function-missing", "OnlineQuickMatch.HandleGameCall(" + function + "): " + err);
	}
}

void AptOnlineHub::startSearch()
{
	ShellEnvironment &env = m_shell.environment();
	PlatformApiClient *p = env.onlineHub ? env.onlineHub->platform : nullptr;
	auto refuse = [&](const std::u16string &why) {
		m_qmState = 0;
		m_status = narrow(why);
		callChild("CloseFoundAndReset");
		m_shell.showBox(kTitle, why, "quickmatch|" + narrow(why));
	};
	if (!p)
	{
		refuse(std::u16string(kUnavailable) + u": no platform service is configured.");
		return;
	}
	if (m_room.phase != Room::Phase::None && m_room.phase != Room::Phase::Failed)
	{
		refuse(u"A match is already being set up.");
		return;
	}
	if (m_shell.m_client && m_shell.m_client->signedIn())
	{
		p->setNick(narrow(m_shell.m_client->accountName()));
	}
	const std::string c = choices();
	const std::size_t a = c.find('|'), b = c.find('|', a + 1);
	const std::string size = c.substr(0, a), faction = c.substr(a + 1, b - a - 1);
	// the mode is the player's simulation profile (Options' Uber performance mode, OptionPreferences::simulationProfile: UBERJOIN-1's question too)
	const std::string mode = env.options && env.options->simulationProfile() == SimulationProfile::Uber ? "uber" : "faithful";
	std::string err;
	if (!p->qmSearch(mode, size, { faction }, &err))
	{
		refuse(u"Quick Match could not start: " + u16(err) + u".");
		return;
	}
	m_qmState = 1;
	m_status = "Searching for " + std::string(size == "2v2" ? "a 2 vs 2" : "a 1 vs 1") + " game (" + (mode == "uber" ? "Uber" : "Normal") + " mode) ...";
	m_shell.windows().note("online-qm", "search: " + c + " mode " + mode);
}

void AptOnlineHub::cancelSearch(bool byPlayer)
{
	// RW 0x9C9E55: the request, state 0, the owner told (the movie closes its Searching panel itself: DoSearchFunctions)
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (p)
	{
		p->qmCancel();
		p->qmReset();
	}
	if (m_room.phase != Room::Phase::None && m_room.phase != Room::Phase::Failed && m_shell.m_client && m_shell.m_client->inGame())
	{
		m_shell.m_client->leaveGame();
	}
	m_room = Room();
	m_qmState = 0;
	m_status = byPlayer ? "The search was cancelled." : std::string();
}

void AptOnlineHub::searchEnded(const std::string &why)
{
	m_qmState = 0;
	m_status = why;
	callChild("CloseFoundAndReset");
	m_shell.showBox(kTitle, u16(why), "quickmatch|" + why);
	if (PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr)
	{
		p->qmReset();
	}
}

void AptOnlineHub::matched()
{
	PlatformApiClient *p = m_shell.environment().onlineHub->platform;
	const PlatformApiClient::Match &m = p->match();
	int team = -1;
	for (const auto &pl : m.players)
	{
		team = pl.you ? pl.team : team;
	}
	m_shell.windows().note("online-qm", "matched: match " + m.id + ", " + std::to_string(m.players.size()) + " players, " + (m.host ? "hosting" : "joining"));
	callChild("DoOpenFound"); // BFME2's matched handler: the found movie
	m_foundShown = true;
	startRoom(m.roomCode, m.id, "", m.host, team, (int)m.players.size());
	p->qmReset();
}

bool AptOnlineHub::pickMap(const std::string &matchId, int players, std::string &map) const
{
	// the official multiplayer maps for exactly that many players, in the skirmish list's order (SkirmishGameSetup::listedMaps), one picked by the
	// match id: every player of the match picks the same [S-4871]
	const SkirmishGameSetup *setup = const_cast<AptOnlineShell &>(m_shell).setupModel();
	if (!setup)
	{
		return false;
	}
	std::vector<std::string> maps;
	for (int index : setup->listedMaps())
	{
		const MapCacheEntry &m = setup->source().maps()[(std::size_t)index];
		if (m.isOfficial && m.isMultiplayer && m.numPlayers == players)
		{
			maps.push_back(m.name);
		}
	}
	if (maps.empty())
	{
		return false;
	}
	map = maps[fnv(matchId) % maps.size()];
	return true;
}

void AptOnlineHub::startRoom(const std::string &code, const std::string &matchId, const std::string &tournamentId, bool host, int team, int expected)
{
	OnlineLobbyClient *client = m_shell.m_client;
	m_room = Room();
	m_room.code = code;
	m_room.matchId = matchId;
	m_room.tournamentId = tournamentId;
	m_room.host = host;
	m_room.team = team;
	m_room.expected = expected;
	m_room.deadline = now() + kRoomTimeoutMs;
	const std::string c = choices();
	const std::size_t a = c.find('|'), b = c.find('|', a + 1);
	const std::string faction = c.substr(a + 1, b - a - 1);
	m_room.color = std::atoi(c.substr(b + 1).c_str());
	m_room.faction = -1;
	if (faction != "random" && m_shell.environment().skirmish)
	{
		const auto &fs = m_shell.environment().skirmish->factions();
		for (int i = 0; i < (int)fs.size(); ++i)
		{
			m_room.faction = fs[(std::size_t)i].templateName == faction ? i : m_room.faction;
		}
	}
	if (!client || !client->signedIn())
	{
		roomFailed("not signed in to the lobby");
		return;
	}
	if (client->inGame())
	{
		client->leaveGame();
	}
	if (host)
	{
		SkirmishGameSetup *model = m_shell.setupModel();
		std::string map;
		if (!model || !pickMap(matchId, expected, map))
		{
			roomFailed("no official map for " + std::to_string(expected) + " players is in this game's map list");
			return;
		}
		SkirmishGameInfo info = model->info();
		info.mapName = map;
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			info.slots[s] = SkirmishGameSlot();
			info.slots[s].state = s == 0 ? SLOT_PLAYER : (s < expected ? SLOT_OPEN : SLOT_CLOSED);
		}
		info.slots[0].playerTemplate = m_room.faction;
		info.slots[0].color = m_room.color;
		info.slots[0].teamNumber = team;
		info.cheatMode = -1; // lane CHEAT-1: a Quick Match / tournament room never has Cheat Mode (the lobby refuses it with a room code)
		m_room.phase = Room::Phase::Creating;
		m_status = "Opponent found: opening the game ...";
		client->createGame(tournamentId.empty() ? u"Quick Match" : u"Tournament match", "", info, 0, code);
		m_room.slotSent = true; // the host's choices are in the create
	}
	else
	{
		m_room.phase = Room::Phase::Joining;
		m_status = "Opponent found: joining the game ...";
		client->joinByCode(code);
	}
}

void AptOnlineHub::roomFailed(const std::string &why)
{
	m_shell.windows().note("online-qm", "the match could not start: " + why);
	if (m_shell.m_client && m_shell.m_client->inGame())
	{
		m_shell.m_client->leaveGame();
	}
	m_room.phase = Room::Phase::Failed;
	m_qmState = 0;
	m_status = "The match could not start: " + why + (!why.empty() && why.back() == '.' ? "" : ".");
	if (m_qmUp)
	{
		callChild("CloseFoundAndReset");
	}
	m_shell.showBox(kTitle, u16(m_status), "quickmatch|" + m_status);
}

bool AptOnlineHub::lobbyEvent(const OnlineLobbyClient::Event &e)
{
	if (m_room.phase == Room::Phase::None || m_room.phase == Room::Phase::Failed)
	{
		return false;
	}
	OnlineLobbyClient *client = m_shell.m_client;
	switch (e.kind)
	{
	case OnlineLobbyClient::Event::GameCreated:
	case OnlineLobbyClient::Event::GameJoined:
		m_room.phase = Room::Phase::InRoom;
		m_status = "In the match's game: waiting for every player ...";
		sendCheckIn(); // the room screen: a tournament match's check-in
		if (!m_room.slotSent)
		{
			// the joiner's own army, colour and the match's team (the start position stays random, as the matchmaker's slots in BFME2 0x5BAF35)
			client->setOwnSlot(m_room.faction, m_room.color, OnlineLobbyClient::kKeep, m_room.team);
			m_room.slotSent = true;
		}
		return true;
	case OnlineLobbyClient::Event::GameUpdated:
		roomUpdate();
		return true;
	case OnlineLobbyClient::Event::Chat:
		return true;
	case OnlineLobbyClient::Event::Removed:
		roomFailed(e.code == "closed" ? "the host left the match" : e.code == "kicked" ? "the host removed you" : "you left the match");
		return true;
	case OnlineLobbyClient::Event::GameStart:
		m_room.phase = Room::Phase::Starting;
		m_status = "The match starts ...";
		return false; // the shell plays the START as any online game's
	case OnlineLobbyClient::Event::Refused:
		if (e.request != "create" && e.request != "join" && e.request != "slot" && e.request != "ready" && e.request != "start" && e.request != "poll")
		{
			return false; // not the room's (the server browser's list, the message of the day ...): the shell's
		}
		if (e.request == "join" && e.code == "no_room" && m_room.phase == Room::Phase::Joining)
		{
			m_room.nextJoin = now() + kJoinRetryMs; // the host has not made the room yet
			return true;
		}
		if (e.request == "poll" && e.code == "transport")
		{
			return true; // the next poll goes out (the shell's own rule); the room's deadline still holds
		}
		roomFailed(narrow(e.text).empty() ? e.code : narrow(e.text));
		return true;
	default:
		return false;
	}
}

std::string AptOnlineHub::myNick() const
{
	const OnlineLobbyClient *c = m_shell.m_client;
	return c && c->signedIn() ? narrow(c->accountName()) : std::string();
}

void AptOnlineHub::sendCheckIn()
{
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (m_room.tournamentId.empty())
	{
		m_room.checkedIn = true; // a Quick Match has no check-in
		return;
	}
	if (!p)
	{
		roomFailed("the online services are not available to check you in");
		return;
	}
	++m_room.checkInTries;
	m_room.checkInRetryAt = 0;
	m_status = "Checking in for the match ...";
	p->checkIn(m_room.tournamentId, m_room.matchId);
}

void AptOnlineHub::roomUpdate()
{
	OnlineLobbyClient *client = m_shell.m_client;
	if (!client || !client->inGame() || m_room.phase != Room::Phase::InRoom)
	{
		return;
	}
	const auto &views = client->slotViews();
	const int me = client->localSlot();
	// the lobby unreadies everyone at every change of the room (S-4553): each player says ready again once the room changed since its last ready;
	// in a tournament match nobody readies before the check-in is taken
	if (m_room.checkedIn && me >= 0 && me < MAX_SLOTS && !views[(std::size_t)me].ready && (!m_room.readySent || client->viewSeq() != m_room.readySeq))
	{
		client->setReady(true);
		m_room.readySent = true;
		m_room.readySeq = client->viewSeq();
	}
	if (!m_room.host || m_room.startSent || !m_room.checkedIn)
	{
		return;
	}
	int humans = 0, ready = 0;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (views[(std::size_t)s].state == "player")
		{
			++humans;
			ready += views[(std::size_t)s].ready ? 1 : 0;
		}
	}
	if (humans == m_room.expected && ready == humans)
	{
		client->start();
		m_room.startSent = true;
		m_status = "Every player is ready: starting ...";
	}
}

// ---- per frame ------------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineHub::pumpPlatform()
{
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (!p)
	{
		return;
	}
	for (const PlatformApiClient::Event &e : p->takeUiEvents())
	{
		switch (e.kind)
		{
		case PlatformApiClient::Event::QmChanged:
			if (m_qmState != 1)
			{
				break;
			}
			if (p->qmState() == PlatformApiClient::QmState::Matched)
			{
				matched();
			}
			else if (p->qmState() == PlatformApiClient::QmState::Searching)
			{
				// still searching (the ticket came)
			}
			else if (p->qmState() == PlatformApiClient::QmState::TimedOut)
			{
				searchEnded("No opponent was found. Try again later, or play an Open Play game.");
			}
			else if (p->qmState() == PlatformApiClient::QmState::Expired)
			{
				searchEnded("The search expired. Try again.");
			}
			else if (p->qmState() == PlatformApiClient::QmState::Failed)
			{
				searchEnded(std::string("Online services unavailable: ") + p->qmProblem());
			}
			else if (p->qmState() == PlatformApiClient::QmState::Cancelled && e.text != "the search was cancelled")
			{
				searchEnded("The search was cancelled by the online service.");
			}
			break;
		case PlatformApiClient::Event::Tournaments:
			m_panelProblem.clear();
			fillTournaments();
			break;
		case PlatformApiClient::Event::TournamentInfo:
			fillDetails();
			break;
		case PlatformApiClient::Event::CheckedIn:
			if (m_room.phase == Room::Phase::InRoom && e.id == m_room.matchId)
			{
				m_room.checkedIn = true;
				m_status = "Checked in: waiting for your opponent ...";
				roomUpdate();
			}
			break;
		case PlatformApiClient::Event::Registered:
			m_status = e.flag ? "You are registered." : "You withdrew.";
			p->listTournaments();
			break;
		case PlatformApiClient::Event::Refused:
			if (e.request == "tournaments" || e.request == "tournament")
			{
				m_panelProblem = std::string("Online services unavailable: ") + e.text;
				fillTournaments();
			}
			else if (e.request == "checkin")
			{
				if (m_room.phase == Room::Phase::InRoom && e.id == m_room.matchId)
				{
					if (e.code == "transport" && m_room.checkInTries < 2)
					{
						m_room.checkInRetryAt = now() + 3000; // once more, shortly
					}
					else
					{
						roomFailed("the check-in was not taken: " + narrow(u16(e.text)));
					}
				}
			}
			else if (e.request == "register" || e.request == "withdraw")
			{
				const std::string why = (e.request == "register" ? "The registration was refused: " : "The withdrawal was refused: ") + e.text;
				m_status = why;
				m_shell.showBox(u"Tournaments", u16(why), "tournament|" + why);
			}
			else if (e.request == "global" && m_stats)
			{
				m_stats->communityFailed(e.text);
			}
			break;
		default:
			break;
		}
	}
}

void AptOnlineHub::update()
{
	pumpPlatform();
	if (m_stats)
	{
		m_stats->update();
	}
	if (m_room.phase == Room::Phase::InRoom && m_room.checkInRetryAt && now() >= m_room.checkInRetryAt)
	{
		sendCheckIn();
	}
	if (m_room.phase == Room::Phase::Joining && m_room.nextJoin && now() >= m_room.nextJoin && m_shell.m_client)
	{
		m_room.nextJoin = 0;
		m_shell.m_client->joinByCode(m_room.code);
	}
	if ((m_room.phase == Room::Phase::Creating || m_room.phase == Room::Phase::Joining || m_room.phase == Room::Phase::InRoom) && now() >= m_room.deadline)
	{
		roomFailed(m_room.phase == Room::Phase::Joining ? "the host did not open the match's game in time" : "not every player joined the match in time");
	}
	if (m_qmUp)
	{
		// the panel's lettering while the online shell is the screen on top (the Options screen opened over it draws its own page); taken back
		// every frame, as the Options screen hands the layer's decorator back to nobody when it closes
		AptGadgetLayer *layer = m_shell.windows().gadgetLayer();
		if (layer && m_shell.shell().top() == &m_shell)
		{
			m_shell.takeDecorator(); // lane ONLINEMENU-1: the shell's decorator draws this panel, then the friends button and page
		}
		// the movie's Searching / found pop-up covers the page's middle while a search or a match's room is under way: the panel steps aside
		// (its status line stays, below the pop-up)
		// lane ONLINEUX-1: and while the shell is covered (Options over the page: the panel's lists, engine gadgets of the shell's window, showed
		// through the Options screen)
		const bool hide = panelHidden() || m_shell.isHidden() || m_shell.shell().top() != &m_shell;
		for (GameWindow *w : { m_list, m_details })
		{
			if (w && w->winIsHidden() != hide)
			{
				w->winHide(hide);
			}
		}
		if (m_qmState == 1)
		{
			PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
			if (p && p->qmState() == PlatformApiClient::QmState::Searching)
			{
				const std::uint64_t seconds = (now() - p->qmSearchStartedMs()) / 1000;
				char buf[96];
				std::snprintf(buf, sizeof(buf), "Searching for a %s game (%s mode) ... %llu:%02llu", p->qmSize() == "2v2" ? "2 vs 2" : "1 vs 1",
					p->qmMode() == "uber" ? "Uber" : "Normal", (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60));
				m_status = buf;
			}
		}
	}
}

void AptOnlineHub::recordsChanged()
{
	if (m_stats)
	{
		m_stats->refresh();
	}
}

bool AptOnlineHub::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	if (m_stats && m_stats->gadgetMessage(from, msg, data1, data2))
	{
		return true;
	}
	if (!from || (from != m_list && from != m_details))
	{
		return false;
	}
	if (from == m_list && (msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED))
	{
		const int row = (int)(std::intptr_t)data2;
		if (msg == GLM_SELECTED)
		{
			chooseTournament(row);
		}
		else
		{
			tournamentAction(row);
		}
	}
	return true;
}

// ---- the tournaments panel ------------------------------------------------------------------------------------------------------------------------------

void AptOnlineHub::showPanel(bool show)
{
	AptGadgetLayer *layer = m_shell.windows().gadgetLayer();
	if (!layer)
	{
		return;
	}
	if (!show)
	{
		if (m_list)
		{
			layer->destroyEngineGadget(m_list);
		}
		if (m_details)
		{
			layer->destroyEngineGadget(m_details);
		}
		m_list = m_details = nullptr;
		layer->clearDecorator(this); // lane ONLINEUX-1: only this hub's (the Options screen's page sets its own)
		return;
	}
	if (m_list || !m_shell.ownerWindow())
	{
		return;
	}
	// two list boxes of the game's own skin (window\apt\listbox.wnd): the tournaments, then the chosen one's rules and bracket
	std::string err;
	const int x0 = kPanelLeft + 14, x1 = kPanelRight - 14;
	m_list = layer->createEngineGadget(kListSkin, x0, kPanelTop + 40, x1 - x0, 112, m_shell.ownerWindow(), &err);
	m_details = m_list ? layer->createEngineGadget(kListSkin, x0, kPanelTop + 160, x1 - x0, 140, m_shell.ownerWindow(), &err) : nullptr;
	if (!m_list || !m_details)
	{
		m_shell.windows().note("online-tournaments", "the tournaments panel could not be made: " + err);
		if (m_list)
		{
			layer->destroyEngineGadget(m_list);
		}
		m_list = m_details = nullptr;
		return;
	}
	const int listWidths[3] = { 52, 30, 18 };
	GadgetListBoxSetColumnWidths(m_list, 3, listWidths);
	fillTournaments();
}

std::vector<std::string> AptOnlineHub::tournamentRows() const
{
	std::vector<std::string> out;
	PlatformApiClient *p = const_cast<AptOnlineShell &>(m_shell).environment().onlineHub
		? const_cast<AptOnlineShell &>(m_shell).environment().onlineHub->platform : nullptr;
	if (!p)
	{
		return out;
	}
	for (const auto &t : p->tournaments())
	{
		out.push_back(t.name + "|" + narrow(stateName(t.state)) + "|" + std::to_string(t.registered) + "/" + std::to_string(t.maxPlayers));
	}
	return out;
}

void AptOnlineHub::fillTournaments()
{
	if (!m_list)
	{
		return;
	}
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	GadgetListBoxReset(m_list);
	if (!p || !m_panelProblem.empty())
	{
		GadgetListBoxAddEntryText(m_list, p ? u16(m_panelProblem) : std::u16string(kUnavailable), 0xFFFFFFFFu, 0, 0);
		return;
	}
	if (!p->tournamentsKnown())
	{
		GadgetListBoxAddEntryText(m_list, u"Asking the online services ...", 0xFFFFFFFFu, 0, 0);
		return;
	}
	// A committed refresh may have removed the selected tournament. Do not leave
	// its old row index and bracket displayed beside a different tournament.
	if (!m_selectedId.empty())
	{
		const auto &listed = p->tournaments();
		auto it = std::find_if(listed.begin(), listed.end(), [this](const PlatformApiClient::Tournament &t) { return t.id == m_selectedId; });
		if (it == listed.end())
		{
			m_selected = -1;
			m_selectedId.clear();
			m_detailRows.clear();
			if (m_details)
			{
				GadgetListBoxReset(m_details);
			}
		}
	}
	if (p->tournaments().empty())
	{
		GadgetListBoxAddEntryText(m_list, u"No tournaments are planned.", 0xFFFFFFFFu, 0, 0);
		return;
	}
	int row = 0;
	for (const auto &t : p->tournaments())
	{
		const std::u16string mode = t.mode == "uber" ? u"Uber" : u"Normal";
		GadgetListBoxAddEntryText(m_list, u16(t.name) + u" (" + u16(t.size) + u", " + mode + u")", 0xFFFFFFFFu, row, 0);
		GadgetListBoxAddEntryText(m_list, stateName(t.state), 0xFFFFFFFFu, row, 1);
		GadgetListBoxAddEntryText(m_list, u16(std::to_string(t.registered) + "/" + std::to_string(t.maxPlayers)), 0xFFFFFFFFu, row, 2);
		++row;
	}
	if (p->tournamentsTruncated())
	{
		GadgetListBoxAddEntryText(m_list, u"More tournaments exist than are listed.", 0xFFFFFFFFu, row, 0);
		GadgetListBoxSetItemData(m_list, itemData(-1), row, 0); // not a tournament: no action
	}
	if (!m_selectedId.empty())
	{
		for (int i = 0; i < (int)p->tournaments().size(); ++i)
		{
			if (p->tournaments()[(std::size_t)i].id == m_selectedId)
			{
				m_selected = i;
				GadgetListBoxSetSelected(m_list, i);
			}
		}
	}
}

void AptOnlineHub::chooseTournament(int row)
{
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (!p || row < 0 || row >= (int)p->tournaments().size())
	{
		return;
	}
	m_selected = row;
	m_selectedId = p->tournaments()[(std::size_t)row].id;
	m_detailRows = { "Asking the online services ..." };
	if (m_details)
	{
		GadgetListBoxReset(m_details);
		GadgetListBoxAddEntryText(m_details, u16(m_detailRows[0]), 0xFFFFFFFFu, 0, 0);
	}
	p->fetchTournament(m_selectedId);
}

void AptOnlineHub::fillDetails()
{
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (!p || !p->tournamentKnown() || p->tournament().info.id != m_selectedId)
	{
		return;
	}
	const PlatformApiClient::TournamentDetail &d = p->tournament();
	const std::string me = myNick();
	m_detailRows.clear();
	const std::time_t start = (std::time_t)d.info.start;
	char when[64] = "";
	if (const std::tm *tm = std::gmtime(&start))
	{
		std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M UTC", tm);
	}
	const bool yours = p->registeredIn(d.info.id) || d.nextMatch() != nullptr;
	m_detailRows.push_back(narrow(stateName(d.info.state)) + ", starts " + when + "; " + std::to_string(d.info.registered) + " of " +
		std::to_string(d.info.maxPlayers) + " places taken" + (yours ? "; you are registered" : "") +
		(d.info.state == "cancelled" && !d.reason.empty() ? " (" + d.reason + ")" : ""));
	// the rules, line by line
	std::size_t pos = 0;
	while (pos < d.rules.size())
	{
		std::size_t eol = d.rules.find('\n', pos);
		eol = eol == std::string::npos ? d.rules.size() : eol;
		if (eol > pos)
		{
			m_detailRows.push_back(d.rules.substr(pos, eol - pos));
		}
		pos = eol + 1;
	}
	if (const PlatformApiClient::BracketMatch *next = d.nextMatch())
	{
		const std::string &opponent = next->players[0] == me ? next->players[1] : next->players[0];
		m_detailRows.push_back("YOUR NEXT MATCH: round " + std::to_string(next->round + 1) + " against " + opponent + " (" + next->state +
			"): double-click the tournament to join");
	}
	for (std::size_t r = 0; r < d.rounds.size(); ++r)
	{
		m_detailRows.push_back("ROUND " + std::to_string(r + 1));
		for (const std::string &id : d.rounds[r])
		{
			for (const PlatformApiClient::BracketMatch &m : d.matches)
			{
				if (m.id != id)
				{
					continue;
				}
				std::string line = (m.players[0].empty() ? "(waiting)" : m.players[0]) + " vs " + (m.players[1].empty() ? "(waiting)" : m.players[1]);
				line += m.winner.empty() ? " (" + m.state + ")" : ": " + m.winner + " won";
				m_detailRows.push_back(line);
			}
		}
	}
	if (m_details)
	{
		GadgetListBoxReset(m_details);
		for (std::size_t i = 0; i < m_detailRows.size(); ++i)
		{
			GadgetListBoxAddEntryText(m_details, u16(m_detailRows[i]), 0xFFFFFFFFu, (int)i, 0);
		}
	}
}

void AptOnlineHub::tournamentAction(int row)
{
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (!p || row < 0 || row >= (int)p->tournaments().size())
	{
		return;
	}
	const PlatformApiClient::Tournament listed = p->tournaments()[(std::size_t)row];
	if (m_shell.m_client && m_shell.m_client->signedIn())
	{
		p->setNick(myNick());
	}
	const bool detailKnown = p->tournamentKnown() && p->tournament().info.id == listed.id;
	if (!detailKnown)
	{
		chooseTournament(row);
		m_status = "Reading " + listed.name + " ... double-click it again.";
		return;
	}
	const PlatformApiClient::TournamentDetail &d = p->tournament();
	// the detail was read after the list: its state, mode and size are the tournament's now (an upcoming one may have started since the list was read)
	const PlatformApiClient::Tournament t = d.info;
	const std::u16string title = u"Tournaments";
	if (t.state == "running")
	{
		const PlatformApiClient::BracketMatch *next = d.nextMatch();
		if (!next)
		{
			m_status = "You have no match to play in " + t.name + " now.";
			return;
		}
		// the tournament's mode is the game's: a player in the other mode would be refused by the room (its profile identity differs), after the
		// opponent waited
		const std::string mine = m_shell.environment().options && m_shell.environment().options->simulationProfile() == SimulationProfile::Uber ? "uber" : "faithful";
		if (t.mode != mine)
		{
			const std::u16string text = t.mode == "uber" ? u"This tournament is played in Uber performance mode. Turn it on in Options > Performance, then join your match."
														 : u"This tournament is played in Normal mode. Turn Uber performance mode off in Options > Performance, then join your match.";
			m_status.clear();
			m_shell.showBox(title, text, "tournament|mode " + t.mode);
			return;
		}
		if (t.size != "1v1" || next->players[0].empty() || next->players[1].empty() || next->players[0] == next->players[1])
		{
			// platform v1 has 1v1 tournaments only (the parser refuses others); two players of one nickname cannot be told apart to name the host
			const std::u16string text = next->players[0] == next->players[1] ? u"Both players of this match have the same nickname: the room's host cannot be chosen. One of you must change nickname."
																		   : u"This match is not a 1 vs 1 match with two players.";
			m_status.clear();
			m_shell.showBox(title, text, "tournament|not joinable " + next->id);
			return;
		}
		const PlatformApiClient::BracketMatch match = *next;
		m_status.clear();
		m_shell.box().show(AptMessageBox::TYPE_YES_NO, title, u16("Join your round " + std::to_string(match.round + 1) + " match of " + t.name + " now?"),
			[this, match, t, me = myNick()](int button) {
				if (button != AptMessageBox::BUTTON_YES)
				{
					return;
				}
				// the first player listed hosts the match's room; a 1v1: the host is team 0, the other team 1 [S-4873]
				const bool host = match.players[0] == me;
				startRoom(match.roomCode, match.id, t.id, host, host ? 0 : 1, 2);
			});
		m_shell.m_boxes.push_back("tournament|join " + match.id);
		return;
	}
	if (t.state != "upcoming")
	{
		m_status = t.state == "cancelled" ? t.name + " was cancelled." : t.name + " has finished.";
		return;
	}
	// the public detail names no registrants: this session's own registrations (and a match room code, which only a registered player is given) say so;
	// otherwise Yes keeps or takes the place (the service answers the same to a second registration)
	const bool registered = p->registeredIn(t.id) || d.nextMatch() != nullptr;
	m_status.clear();
	m_shell.box().show(AptMessageBox::TYPE_YES_NO, title,
		u16(registered ? "You are registered for " + t.name + ". Withdraw?"
					   : "Register for " + t.name + "? (If you registered before, Yes keeps your place; No offers to withdraw.)"),
		[this, p, t, registered](int button) {
			if (button == AptMessageBox::BUTTON_YES)
			{
				if (registered)
				{
					p->withdrawTournament(t.id);
				}
				else
				{
					p->registerTournament(t.id);
				}
				return;
			}
			if (!registered)
			{
				// a restarted game does not know its registrations (the public detail names none): Withdraw is offered on its own, and the service
				// answers a withdrawal of a place the player never had with "not registered", changing nothing [S-4873]
				askWithdraw(t);
			}
		});
	m_shell.m_boxes.push_back(std::string("tournament|") + (registered ? "withdraw " : "register ") + t.id);
}

void AptOnlineHub::askWithdraw(const PlatformApiClient::Tournament &t)
{
	PlatformApiClient *p = m_shell.environment().onlineHub ? m_shell.environment().onlineHub->platform : nullptr;
	if (!p)
	{
		return;
	}
	m_shell.box().show(AptMessageBox::TYPE_YES_NO, u"Tournaments", u16("Withdraw from " + t.name + "? (Nothing changes if you are not registered.)"),
		[p, id = t.id](int button) {
			if (button == AptMessageBox::BUTTON_YES)
			{
				p->withdrawTournament(id);
			}
		});
	m_shell.m_boxes.push_back("tournament|withdraw? " + t.id);
}

bool AptOnlineHub::panelHidden() const
{
	// GuiFX's message box (its own level) is drawn under the engine's gadgets: the panel steps aside while it is up too
	const bool box = m_shell.m_box && m_shell.m_box->type() != AptMessageBox::TYPE_NONE;
	return box || m_qmState != 0 || (m_room.phase != Room::Phase::None && m_room.phase != Room::Phase::Failed);
}

void AptOnlineHub::decorate(GadgetDrawList &out) const
{
	// lane ONLINEUX-1: nothing of the page while the online shell is not the screen shown on top (the Quick Match status line was drawn over the game's
	// loading screen: the shell's pop for the game hides it before it is destroyed)
	if (m_shell.isHidden() || m_shell.shell().top() != &m_shell)
	{
		return;
	}
	if (panelHidden())
	{
		if (!m_status.empty())
		{
			OpenBFMEOptionsPage::text(out, u16(m_status), kPanelLeft + 16, kPanelBottom - 22, 14, kFrameText, kPanelRight - kPanelLeft - 32, false);
		}
		return;
	}
	OpenBFMEOptionsPage::frame(out, kPanelLeft, kPanelTop, kPanelRight, kPanelBottom, u"TOURNAMENTS");
	OpenBFMEOptionsPage::text(out, u"Double-click a tournament to register, withdraw or join your match.", kPanelLeft + 16, kPanelTop + 306, 14, kHelpText,
		kPanelRight - kPanelLeft - 32, false);
	if (!m_status.empty())
	{
		OpenBFMEOptionsPage::text(out, u16(m_status), kPanelLeft + 16, kPanelBottom - 22, 14, kFrameText, kPanelRight - kPanelLeft - 32, false);
	}
}
