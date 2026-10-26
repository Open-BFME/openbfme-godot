// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/AptScreens/AptLanLobby.h (lane MP-2).

#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "Common/MpGameRules.h"
#include "GameClient/MapCache.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/ProfileSwitch.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameNetwork/LANAPI.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace
{
std::string narrow(const std::u16string &s)
{
	std::string out;
	for (char16_t c : s)
	{
		out.push_back(c < 0x80 ? (char)c : '?');
	}
	return out;
}
UnicodeString wide(const std::string &s)
{
	return UnicodeString(s.begin(), s.end());
}
// LANAPI's error text of a return code (RW 0x648BA7: the jump table RW 0x648C2A)
const char *errorLabel(LANAPI::ReturnType ret)
{
	switch (ret)
	{
		case LANAPI::RET_OK: return "LAN:OK";
		case LANAPI::RET_TIMEOUT: return "LAN:ErrorTimeout";
		case LANAPI::RET_GAME_FULL: return "LAN:ErrorGameFull";
		case LANAPI::RET_DUPLICATE_NAME: return "LAN:ErrorDuplicateName";
		case LANAPI::RET_CRC_MISMATCH: return "LAN:ErrorCRCMismatch";
		case LANAPI::RET_SERIAL_DUPE: return "WOL:ChatErrorSerialDup";
		case LANAPI::RET_GAME_STARTED: return "LAN:ErrorGameStarted";
		case LANAPI::RET_GAME_EXISTS: return "LAN:ErrorGameExists";
		case LANAPI::RET_GAME_GONE: return "LAN:ErrorGameGone";
		case LANAPI::RET_BUSY: return "LAN:ErrorBusy";
		default: return "LAN:ErrorUnknown";
	}
}
// the game text's "%d" filled (TheGameText slot 0x44's format, RW 0x84392C)
std::u16string withNumber(std::u16string format, int n)
{
	const std::size_t at = format.find(u"%d");
	if (at != std::u16string::npos)
	{
		const std::string digits = std::to_string(n);
		format.replace(at, 2, std::u16string(digits.begin(), digits.end()));
	}
	return format;
}
} // namespace

AptLanLobby::~AptLanLobby() = default;

AptMessageBox &AptLanLobby::box()
{
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	return *m_box;
}

std::u16string AptLanLobby::text(const std::string &label) const
{
	return fetchOrMissing(const_cast<AptLanLobby *>(this)->environment().gameText, label);
}

const char16_t *const AptLanLobby::kStrategicUnavailable =
	u"War of the Ring network games are not available yet: its strategy map game is still being rebuilt. Use Open Play.";

void AptLanLobby::showError(const std::string &title, const std::string &body)
{
	// RW 0x84ED5D -> RW 0x81A452: an Ok box with the two game texts
	box().show(AptMessageBox::TYPE_OK, text(title), text(body));
}

void AptLanLobby::ruleNotice(const std::u16string &line)
{
	systemChat(line);
}

void AptLanLobby::showRuleWarning(const std::u16string &title, const std::u16string &text)
{
	box().show(AptMessageBox::TYPE_OK, title, text);
}

void AptLanLobby::systemChat(const std::u16string &line)
{
	// RW 0x847508 kind 2: TheLAN's RequestChat with the SYSTEM type (slot 0x54 with 3): every player of the game gets the line, this one included
	if (m_lan && m_lan->currentGame())
	{
		m_lan->requestChat(line, true);
	}
	else
	{
		addChatLine(line, 0xFFFFFFFFu);
	}
}

bool AptLanLobby::showChat() const
{
	// RW 0x846D9A: TheLAN's slot 0xD8 (INFERENCE: "in a game": the setup page of a LAN game shows its chat to every player)
	return m_lan && m_lan->currentGame();
}

bool AptLanLobby::armCountdown()
{
	// RW 0x8431B8 with its argument set, after the start check passed (RW 0x843840): + 0x2CC = 6, the deadline now + 6 * 1000 - 1, counting, + 0x3A8 the
	// players at the check (INFERENCE: the human players that are not observers, the count RW 0x84392C compares with), then the first tick
	m_minPlayers = 0;
	if (const LANGame *g = m_lan ? m_lan->currentGame() : nullptr)
	{
		for (const SkirmishGameSlot &s : g->info.slots)
		{
			m_minPlayers += s.isHuman() && s.playerTemplate != PLAYERTEMPLATE_OBSERVER ? 1 : 0;
		}
	}
	m_lastSeconds = 6;
	m_deadline = NetMilliseconds() + 6 * 1000 - 1;
	m_counting = true;
	m_startShown = false;
	return countdownTick() != 0;
}

int AptLanLobby::countdownTick()
{
	// RW 0x84392C: 0 = the start is off (state 4, EnablePlayGame), 1 = waiting, 2 = the game was started
	const LANGame *g = m_lan ? m_lan->currentGame() : nullptr;
	if (!g)
	{
		m_counting = false;
		return 0;
	}
	int present = 0, accepted = 0;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = g->info.slots[(std::size_t)i];
		if (s.isHuman() && s.playerTemplate != PLAYERTEMPLATE_OBSERVER)
		{
			++present;
			// RW 0x8402E2: the slot's accept flag (+ 8); the host's own slot counts as accepted (LANAPI keeps no flag for it)
			accepted += (i == m_lan->localSlot() || g->accepted[(std::size_t)i]) ? 1 : 0;
		}
	}
	if (present < m_minPlayers)
	{
		systemChat(text("LAN:HostCanceledGameBecausePlayerLeave"));
		m_counting = false;
		m_deadline = 0;
	}
	else if (accepted < m_minPlayers)
	{
		systemChat(text("LAN:CountdownStoppedGeneric"));
		m_counting = false;
		m_deadline = 0;
	}
	if (!m_counting)
	{
		if (m_deadline != 0)
		{
			systemChat(text("LAN:HostCanceledGame"));
		}
		if (m_startShown)
		{
			m_startShown = false;
			box().hide(true); // RW 0x8400DD -> the box's hide (RW 0x81A38D with 1)
		}
		m_lastSeconds = 0;
		m_deadline = 0;
		return 0;
	}
	const std::int64_t remaining = (std::int64_t)m_deadline - (std::int64_t)NetMilliseconds();
	if (remaining < 1)
	{
		if (m_startShown)
		{
			// RW 0x8431B8 with 0: the start itself
			m_counting = false;
			std::string reason;
			if (!m_lan->hostStartGame(&reason))
			{
				windows().note("lan-start-refused", reason);
				m_startShown = false;
				box().hide(true);
				return 0;
			}
			return 2;
		}
		// RW 0x84392C: the non-interactive QM:STARTINGGAME box (the lobby's slot 0x40 with 1, RW 0x8479EB: title APT:None), five more seconds
		box().show(AptMessageBox::TYPE_NON_INTERACTIVE, text("APT:None"), text("QM:STARTINGGAME"));
		m_startShown = true;
		m_deadline = NetMilliseconds() + 5000;
	}
	else if (!m_startShown && (int)(remaining / 1000) < m_lastSeconds)
	{
		const int seconds = (int)(remaining / 1000);
		if (seconds > 0)
		{
			systemChat(withNumber(text(seconds == 1 ? "LAN:GameStartTimerSingular" : "LAN:GameStartTimerPlural"), seconds));
		}
		m_lastSeconds = seconds;
	}
	return 1;
}

AptLanLobby::AptLanLobby(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptSkirmish(windows, shell, environment, "LanLobby.apt", "AptLanLobby", true), m_lan(environment.lan)
{
	windows.setBackground(1); // lane FB7-1: RotWK's opener shows the front-end background after its push (RW 0x847180 .. 0x847182)
	if (!m_lan)
	{
		windows.note("provider-unwired", "LanLobby.apt: the host gave no LAN lobby (LANAPI) [S-724]");
	}
	registerCommand("AptLanLobby::OnInitialized", [this](const std::string &) { m_initialized = true; });
	registerCommand("AptLanLobby::OnCancelBttn", [this](const std::string &) {
		// RW 0x847A91
		if (m_state == 6)
		{
			// state 6 clears the start flag (lane UI-1: the countdown stops; its next tick says LAN:HostCanceledGame and gives Play Game back)
			m_counting = false;
			return;
		}
		const bool host = m_lan && m_lan->amIHost();
		if (m_lan)
		{
			m_lan->requestGameLeave(); // RW 0x84700F
		}
		backToLobby();
	});
	registerCommand("AptLanLobby::OnExitBttn", [this](const std::string &) {
		// RW 0x846C41: the LAN lobby is left
		if (m_lan)
		{
			if (m_lan->currentGame())
			{
				m_lan->requestGameLeave();
			}
			m_lan->requestLobbyLeave();
		}
		restoreMode(); // lane UBERJOIN-1: the lobby is left: the player's own mode again (the room is gone)
		this->windows().requestShellPop();
	});
	registerCommand("AptLanLobby::OnStartGameBttn", [this](const std::string &) { m_state = 5; });   // RW 0x846F72
	registerCommand("AptLanLobby::OnCreateGameBttn", [this](const std::string &) {
		if (m_strategic)
		{
			box().show(AptMessageBox::TYPE_OK, u"OpenBFME", kStrategicUnavailable); // lane MENUWORK-1 (S-4753)
			return;
		}
		if (m_state == 1)
		{
			m_state = 2;
		}
	});
	registerCommand("AptLanLobby::OnJoinGameBttn", [this](const std::string &) {
		if (m_state == 1) // RW 0x846F8A
		{
			m_state = 7;
		}
	});
	// lane MENUWORK-1 (S-4753): a LAN game from a save is not ported: the press used to note it and show nothing; the player is told now
	registerCommand("AptLanLobby::OnLoadGameBttn", [this](const std::string &) {
		this->windows().note("unported-command", "AptLanLobby::OnLoadGameBttn: loading a saved LAN game is not ported [S-4753]");
		box().show(AptMessageBox::TYPE_OK, u"OpenBFME", u"Loading a saved network game is not available yet: start a new game instead.");
	});
	registerCommand("AptLanLobby::OnOptionsBttn", [this](const std::string &) {
		// RW 0x846F53: RW 0x846F25 (the list gadget forgotten, the state 0; + 0x6C0 = 1, + 0x6BB = 0 and the interface + 0x6AC's slot 4(0) are not
		// identified), then Options.apt opened (lane FB7-1: RW 0x846F58 .. 0x846F5F pass RW 0x91ED91(0, 0, 1, 0): advanced options only allowed)
		m_gamesList = nullptr;
		m_listCandidates.clear();
		m_state = 0;
		AptOptionsScreen::open(this->shell(), false, false, true, false);
	});
	registerCommand("AptLanLobby::OnLoadScreen", [this](const std::string &argument) {
		// RotWK switches the lobby between Open Play and War of the Ring games (the list's mode + 0x304: 0 open play, 1 strategic). Lane MENUWORK-1
		// (S-4753): War of the Ring is not available yet, so its page says so when it opens and its Create Game is refused with the same words
		m_strategic = argument == "LanStrategic";
		if (m_strategic)
		{
			box().show(AptMessageBox::TYPE_OK, u"OpenBFME", kStrategicUnavailable);
		}
	});
	// lane MENUWORK-1: the screen's GameSorter (RotWK's AptLanLobby constructs one, decomp AptLanLobbyDestructor.cpp; its constructor RW 0x9888B7
	// registers the five header commands): a header pressed re-sorts the game list (the commands used to reach no handler)
	windows.note("lan-stops", GameSorter::stopLine()); // lane MENUWORK-1: the sort's inference, reported when the screen is made
	registerCommand("GameSorter::OnSortStatus", [this](const std::string &) { m_sorter.select(GameSorter::KEY_STATUS); });
	registerCommand("GameSorter::OnSortName", [this](const std::string &) { m_sorter.select(GameSorter::KEY_NAME); });
	registerCommand("GameSorter::OnSortMap", [this](const std::string &) { m_sorter.select(GameSorter::KEY_MAP); });
	registerCommand("GameSorter::OnSortPlayers", [this](const std::string &) { m_sorter.select(GameSorter::KEY_PLAYERS); });
	registerCommand("GameSorter::OnSortPing", [this](const std::string &) { m_sorter.select(GameSorter::KEY_PING); });
	// lane MENUWORK-1 (S-4752): the list's Refresh button (the movie sends AptLanLobby::CustomMatch::Refresh; RotWK's game.dat has no such name, so
	// retail's press does nothing: its list refills on every announcement). OpenBFME refills the list at once, so the button does what it says
	registerCommand("AptLanLobby::CustomMatch::Refresh", [this](const std::string &) { m_listDirty = true; });
	registerScreenRef("AptLanLobby::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x848A7F
		if (!w)
		{
			return;
		}
		if (name == "LanLobby::CustomGamesList")
		{
			m_gamesList = w;
			m_listCandidates.push_back(w);
			m_listDirty = true;
		}
		else if (name == "LanLobby::NameEntry")
		{
			m_nameEntry = w;
			m_nameCandidates.push_back(w);
			if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
			{
				entry->maxTextLen = 12; // RW 0x81606D(entry, 0xC): g_lanPlayerNameLength
			}
			GadgetTextEntrySetText(w, m_lan ? m_lan->name() : std::u16string());
		}
	});
	windows.addUpdateListener(this, [this]() { update(); });
}

void AptLanLobby::pruneLan()
{
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer)
	{
		return;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	const std::set<const GameWindow *> alive(live.begin(), live.end());
	// LanOpenPlay places the gadgets twice: CreateComponents (frame 0, removed at frame 10) and the page's own (LobbyMain); the last one initialised
	// can be the one about to go (a seek back to "_lobby" places them in depth order), so a dead gadget falls back to a live one of the same name
	auto follow = [&](GameWindow *&w, std::vector<GameWindow *> &candidates) {
		candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](GameWindow *c) { return !alive.count(c); }), candidates.end());
		if (w && !alive.count(w))
		{
			w = candidates.empty() ? nullptr : candidates.back();
			return true;
		}
		return false;
	};
	if (follow(m_gamesList, m_listCandidates) && m_gamesList)
	{
		m_listDirty = true;
	}
	follow(m_nameEntry, m_nameCandidates);
}

void AptLanLobby::backToLobby()
{
	// RW 0x847AB2: state 0 and the movie's CancelGame (its "_lobby" frame); retail's state 0 then waits for the movie (RW 0x847ED0) before StartLobby,
	// which plays the same frame: here the list state follows at once, so "_lobby" is not started twice (a second start rebuilds the list's gadgets)
	restoreMode(); // lane UBERJOIN-1
	invokeRoot("CancelGame");
	m_state = 1;
	m_ready = false;
	m_listDirty = true;
	m_gameRows.clear(); // until the lobby's list is filled again
	m_listedHosts.clear();
	setHostModeText("1");
}

void AptLanLobby::enterSetup(bool host)
{
	// the setup movie as host or joiner: TheLAN's OnGameCreate / OnGameJoin (RW 0x649129 / 0x649A2A) -> RW 0x849677 / 0x8496C2: in state 3 / 8 the movie's
	// DoCreateGame / DoJoinGame (gotoAndPlay "_lobby_close", vGoingTo "_host" / "_join"), state 4
	SkirmishGameSetup *model = setupModel();
	const auto *game = m_lan ? m_lan->currentGame() : nullptr;
	if (model && game)
	{
		model->setNetwork(m_lan->localSlot());
		model->replaceInfo(game->info);
	}
	setHostModeText(host ? "1" : "0");
	m_ready = false;
	invokeRoot(host ? "DoCreateGame" : "DoJoinGame");
	m_state = 4;
	refreshLobby();
	if (game && game->info.simulationProfile == SimulationProfile::Uber)
	{
		// lane UBERJOIN-1: the room's mode, a line of this player's own chat (a Normal room says nothing)
		const std::u16string line = u"This room uses Uber performance mode.";
		addChatLine(line, 0xFFFFFFFFu);
		m_chat.push_back(narrow(line));
	}
}

void AptLanLobby::pumpLan()
{
	if (!m_lan)
	{
		return;
	}
	m_lan->update(NetMilliseconds()); // RW 0x8490FB: TheLAN->update() first
	for (const LANAPI::Event &e : m_lan->takeEvents())
	{
		switch (e.kind)
		{
		case LANAPI::Event::GameList:
			m_listDirty = true;
			break;
		case LANAPI::Event::GameCreate:
			if (e.ret == LANAPI::RET_OK)
			{
				enterSetup(true);
			}
			else
			{
				windows().note("lan-create-failed", "the game could not be created (" + std::to_string((int)e.ret) + ")");
				m_state = 1;
			}
			break;
		case LANAPI::Event::GameJoin:
			if (e.ret == LANAPI::RET_OK)
			{
				enterSetup(false);
			}
			else
			{
				// RW 0x649CE0: the Ok box LAN:JoinFailed with the return code's text (RW 0x648BA7), the lobby back to its list (RW 0x846F7F)
				windows().note("lan-join-failed", "the join was refused or timed out (" + std::to_string((int)e.ret) + ")");
				showError("LAN:JoinFailed", errorLabel(e.ret));
				restoreMode(); // lane UBERJOIN-1: a join that failed after a mode switch
				m_state = 1;
			}
			break;
		case LANAPI::Event::GameOptions:
		case LANAPI::Event::PlayerJoin:
		case LANAPI::Event::PlayerLeave:
		case LANAPI::Event::Accept:
			if (SkirmishGameSetup *model = setupModel())
			{
				if (const LANGame *game = m_lan->currentGame())
				{
					model->replaceInfo(game->info);
					refreshLobby();
				}
			}
			break;
		case LANAPI::Event::Kicked:
			m_state = 9;
			break;
		case LANAPI::Event::HostLeave:
			windows().note("lan-host-left", "the host left the game");
			backToLobby();
			break;
		case LANAPI::Event::Chat:
		{
			// RW 0x649E5C (LANAPI::OnChat): a SYSTEM line is the text alone (colour RW 0xD9F7A4 = 0xFFFFFFFF), a player's "[" + name + "] " + text (RW 0xC04BE0 /
			// 0xC04BD8, colour -1), both in channel 1 (RW 0x78419D). Not ported: RW 0x78A594's pass over the text; the emote type (none in OpenBFME's chat)
			const std::u16string line = e.flag ? e.text : u"[" + e.name + u"] " + e.text;
			addChatLine(line, 0xFFFFFFFFu);
			m_chat.push_back(narrow(line));
			break;
		}
		case LANAPI::Event::GameStart:
			// every player has the start: the host's shell starts the network game (GameWorld takes TheLAN's start and game socket)
			if (environment().services)
			{
				environment().services->request(ShellRequest{ ShellAction::LanGameStart, std::string() });
			}
			// lane UI-1: the starting box goes with the lobby; its GuiFX.apt (level 11) is released for the game's own (EndGame's GuiFX, S-1060)
			m_startShown = false;
			m_box.reset();
			break;
		default:
			break;
		}
	}
}

void AptLanLobby::fillGamesList()
{
	pruneLan();
	if (!m_gamesList || !m_lan)
	{
		return;
	}
	m_listDirty = false;
	++m_listFills; // lane MENUWORK-1: the menu inventory sees a refill
	// the selected game stays selected when the list is refilled (every announcement refills it): by its host's address
	int selected = -1;
	GadgetListBoxGetSelected(m_gamesList, &selected);
	const bool keep = selected >= 0 && selected < (int)m_listedHosts.size();
	const NetAddress selectedHost = keep ? m_listedHosts[(size_t)selected] : NetAddress();
	m_listedHosts.clear();
	GadgetListBoxReset(m_gamesList);
	if (GadgetListBoxGetNumColumns(m_gamesList) != 5)
	{
		// the movie's headers (S-724: RotWK's widths not read): Status, Game Name, Map, Players are the movie's header cells (read from its APT: the left
		// edges are 0.9 / 86.8 / 311.3 / 575.8 px right of the list's, which is 623 px wide inside its scroll bar), so the cells take 14 / 37 / 42 / 6 percent,
		// Ping the last one percent (the movie has no Ping header); an equal split put each cell under the middle of the wrong header (UBERJOIN-1 r2)
		static const int widths[5] = { 14, 37, 42, 6, 1 };
		GadgetListBoxSetColumnWidths(m_gamesList, 5, widths);
	}
	m_gameRows.clear();
	m_listedGames.clear();
	const std::vector<LANGame> &games = m_lan->games();
	// lane MENUWORK-1: the rows in the GameSorter's order (RotWK's list walk goes through the sorted games, BFME2 0x98113D): each game's sort inputs
	// as RotWK's GameInfo gives them (GameNetwork/GameSorter.h)
	const GameRules skirmishDefaults = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	std::vector<GameSorter::Row> sortRows(games.size());
	for (size_t i = 0; i < games.size(); ++i)
	{
		GameSorter::Row &r = sortRows[i];
		for (const SkirmishGameSlot &s : games[i].info.slots)
		{
			r.players += s.isOccupied() ? 1 : 0;                               // vslot 8 (RW 0x800ACE)
			r.openOrOccupied += (s.state == SLOT_OPEN || s.isOccupied()) ? 1 : 0; // vslot 9 (RW 0x801360)
		}
		r.name = games[i].name;
		r.map = games[i].info.mapName;
		r.mode = 0; // open play: OpenBFME's LAN has no War of the Ring games (S-4751)
		// vslot 6 (RW 0x802B04 / 0x84A8AD): 0 a user map (not in the map cache or not official), 1 non-default rules, 2 in progress
		const MapCacheEntry *cached = setupModel() ? setupModel()->findMap(games[i].info.mapName) : nullptr;
		r.icon[0] = !cached || !cached->isOfficial;
		r.icon[1] = MpGameRules::countDifferences(skirmishDefaults, games[i].info.rules) != 0;
		r.icon[2] = games[i].inProgress;
	}
	for (const size_t i : m_sorter.order(sortRows))
	{
		const LANGame &g = games[i];
		const int occupied = sortRows[i].players;
		std::string map = g.info.mapName;
		const size_t slash = map.find_last_of("/\\");
		map = slash == std::string::npos ? map : map.substr(slash + 1);
		if (map.size() > 4 && map.compare(map.size() - 4, 4, ".map") == 0)
		{
			map.resize(map.size() - 4);
		}
		// lane P16: the count is the game's own slots (the map's start count, capped at the record capacity)
		const std::string players = std::to_string(occupied) + "/" + std::to_string(std::min(g.info.mapStartCount, MAX_SLOTS));
		const std::string status = g.inProgress ? "in progress" : "";
		const Color c = 0xFFFFFFFFu;
		// lane UBERJOIN-1: the game's performance mode (carried by the announced GameInfo's simulation profile), tagged as the online list does
		const bool uber = g.info.simulationProfile == SimulationProfile::Uber;
		// the columns as the movie's headers stand on screen: Status, Game Name, Map, Players, Ping (UBERJOIN-1 r2: the name and the count were one column
		// off, under Status and Game Name; the first column the list takes is the Status one)
		const int row = GadgetListBoxAddEntryText(m_gamesList, wide(status), c, -1, 0);
		// lane CHEAT-1: a Cheat Mode game is tagged the same way, before anyone joins
		GadgetListBoxAddEntryText(m_gamesList, (uber ? g.name + u"  - UBER" : g.name) + (g.info.cheatModeOn() ? u"  - CHEAT MODE" : u""), c, row, 1);
		GadgetListBoxAddEntryText(m_gamesList, wide(map), c, row, 2);
		GadgetListBoxAddEntryText(m_gamesList, wide(players), c, row, 3);
		GadgetListBoxAddEntryText(m_gamesList, UnicodeString(), c, row, 4);
		m_gameRows.push_back(narrow(g.name) + "|" + players + "|" + map + "|" + status + (uber ? "|uber" : "") + (g.info.cheatModeOn() ? "|cheat" : ""));
		m_listedHosts.push_back(g.host);
		m_listedGames.push_back(i);
		if (keep && g.host == selectedHost)
		{
			GadgetListBoxSetSelected(m_gamesList, row);
		}
	}
}

void AptLanLobby::requestJoinSelected()
{
	// RW 0x84931F .. 0x849370: the list's selection (column 3's item data is the game: here its row is the index of TheLAN's list)
	pruneLan();
	int row = -1;
	if (m_gamesList)
	{
		GadgetListBoxGetSelected(m_gamesList, &row);
	}
	if (!m_lan || row < 0 || row >= (int)m_listedGames.size() || m_listedGames[(size_t)row] >= m_lan->games().size())
	{
		windows().note("command-ignored", "AptLanLobby join: no game is selected");
		m_state = 1;
		return;
	}
	// lane UBERJOIN-1 (S-4860): a game in the other performance mode asks first in the game's own Yes / No box: Yes switches to its mode for the game and
	// joins it, No stays in the list (the announcement already carries the mode; the host refuses a join of another profile identity in any case)
	const LANGame &chosen = m_lan->games()[(size_t)row];
	const SimulationProfile gameMode = chosen.info.simulationProfile;
	if (gameMode != m_lan->profile().simulationProfile())
	{
		const std::u16string body = performanceModeSwitchPrompt(gameMode);
		const NetAddress host = chosen.host;
		m_boxes.push_back("mode-switch|" + narrow(body));
		windows().note("lan-box", "mode-switch|" + narrow(body));
		m_state = 1;
		box().show(AptMessageBox::TYPE_YES_NO, u"Performance mode", body, [this, gameMode, host](int button) {
			if (button != 2) // No: stay in the list
			{
				return;
			}
			ProfileSwitch *ps = environment().profileSwitch;
			std::string why = "this build cannot switch the performance mode";
			if (!ps || !ps->complete() || !ps->request(gameMode, &why))
			{
				const std::u16string text = u"The performance mode could not be switched: " + std::u16string(why.begin(), why.end());
				m_boxes.push_back("mode-switch-failed|" + narrow(text));
				box().show(AptMessageBox::TYPE_OK, u"Performance mode", text);
				return;
			}
			pruneLan();
			const std::vector<LANGame> &games = m_lan->games();
			const auto it = std::find_if(games.begin(), games.end(), [&](const LANGame &g) { return g.host == host; });
			if (it == games.end())
			{
				windows().note("lan-join-failed", "the game went away while the mode box was up");
				ps->restore();
				showError("LAN:JoinFailed", errorLabel(LANAPI::RET_GAME_GONE));
				return;
			}
			m_state = 8;
			m_lan->requestGameJoin((int)(it - games.begin()));
		});
		return;
	}
	m_state = 8;
	m_lan->requestGameJoin((int)m_listedGames[(size_t)row]); // lane MENUWORK-1: the row's game in the sorted list
}

void AptLanLobby::restoreMode()
{
	// lane UBERJOIN-1: the lobby was left without a game: the player's own performance mode again (a no-op when it was not switched)
	if (ProfileSwitch *ps = environment().profileSwitch; ps && ps->complete())
	{
		ps->restore();
	}
}

void AptLanLobby::refreshJoinButton()
{
	int row = -1;
	if (m_gamesList)
	{
		GadgetListBoxGetSelected(m_gamesList, &row);
	}
	const bool enabled = m_lan && row >= 0 && row < (int)m_listedHosts.size();
	AptSpriteInst *root = windows().apt().level(level());
	AptValue current;
	if (root && root->getMember("vJoinGameEnabled", current) && current.toBoolean(7) != enabled)
	{
		invokeRoot(enabled ? "EnableJoinGame" : "DisableJoinGame");
	}
}

void AptLanLobby::refreshReadyControls()
{
	const LANGame *game = m_lan ? m_lan->currentGame() : nullptr;
	Apt &apt = windows().apt();
	AptSpriteInst *root = apt.level(level());
	AptValue value;
	if (!game || !root || !root->getMember("SetReadyState", value))
	{
		return; // MpGameSetup installs the function as its PlayerList appears.
	}
	AptFunction *setter = value.isObject() ? dynamic_cast<AptFunction *>(value.asObject()) : nullptr;
	AptCharacterInst *list = setter ? dynamic_cast<AptCharacterInst *>(setter->scopeTarget) : nullptr;
	if (!list || !list->defined() || !list->globallyVisible())
	{
		return;
	}
	for (int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		AptCharacterInst *clip = apt.resolvePath(list, "~" + std::to_string(slot) + ".Ready");
		AptSpriteInst *ready = clip ? clip->asSprite() : nullptr;
		if (!ready)
		{
			continue;
		}
		const bool local = slot == m_lan->localSlot() && !m_lan->amIHost();
		const bool checked = slot == 0 || game->accepted[(size_t)slot];
		const char *label = !game->info.slots[(size_t)slot].isHuman() ? "_none"
			: local ? (checked ? "_enabledChecked" : "_enabledUnchecked")
			: (checked ? "_disabledChecked" : "_disabledUnchecked");
		// MpGameSetup.apt's SetReadyState(index, to) plays these labels on PlayerList.~index.Ready.
		const int first = ready->labelFrame(label);
		int end = ready->totalFrames();
		for (const auto &entry : apt.labelsOf(ready->frames))
		{
			if (entry.second > first)
			{
				end = std::min(end, entry.second);
			}
		}
		if (first >= 0 && ready->frame >= first && ready->frame < end)
		{
			continue; // Let the state's animation finish instead of restarting it each update.
		}
		std::string error;
		if (first < 0 || !windows().invokeAS(level(), "SetReadyState", { std::to_string(slot), label }, nullptr, &error))
		{
			windows().note("movie-function-missing", std::string("SetReadyState: ") + label + ": " + error);
		}
	}
}

void AptLanLobby::update()
{
	// RW 0x8490E0
	pruneLan(); // a gadget that went falls back to its live twin (and the list refills)
	pumpLan();
	updateExtendedRows();
	serviceReadyBoxes(); // lane ONLINE-5
	if (m_box)
	{
		m_box->update();
	}
	switch (m_state)
	{
	case 0:
		if (m_initialized)
		{
			if (m_lan && m_lan->currentGame())
			{
				m_state = 2;
				break;
			}
			invokeRoot("StartLobby");
			m_state = 1;
			m_listDirty = true;
		}
		break;
	case 1:
		if (m_sorter.takeChanged() || m_listDirty) // lane MENUWORK-1: a header pressed (RW 0x9881CE's test-and-clear) refills the list
		{
			fillGamesList();
		}
		refreshJoinButton();
		break;
	case 2:
		if (m_lan && !m_lan->currentGame())
		{
			// RW 0x849160 .. 0x84919B: TheLAN->RequestGameCreate; the host's slot 0, every other slot open (ZH LanGameOptionsMenu's host start).
			// lane P16: the map's slots only (a slot past the map's starts does not exist)
			SkirmishGameInfo info = setupModel() ? setupModel()->info() : SkirmishGameInfo();
			for (int s = 1; s < info.mapStartCount; ++s)
			{
				info.slots[s] = SkirmishGameSlot();
				info.slots[s].state = s < info.mapStartCount ? SLOT_OPEN : SLOT_CLOSED;
			}
			m_lan->requestGameCreate(info);
		}
		m_state = 3;
		break;
	case 5:
	{
		// RW 0x849142: the setup's start check (RW 0x8431B8), then the start
		SkirmishGameSetup *model = setupModel();
		if (!m_lan || !m_lan->amIHost())
		{
			// a joiner's Play Game is its accept (ZH LanGameOptionsMenu: the accept button)
			onReadyPress("PlayGame");
			m_state = 4;
			break;
		}
		if (model)
		{
			// lane GAMEMODES-1: RotWK's start check (RW 0x8431B8); its lines go to every player as system chat (the owner's vslot 0x44, kinds 0 and 2)
			const SkirmishGameSetup::StartCheck check = model->checkStart();
			for (const std::string &stop : check.stops)
			{
				windows().note("unported", stop);
			}
			// the LAN owner's vslot 0x44 (RW 0x847508): kind 0 a local chat line (TheLAN vslot 0xA0), kind 1 MessageBoxOk(L"", text) (unless RW 0xDEA110 is set:
			// not identified, taken as clear; the box is not shown by this port: a note) and the local line, kind 2 a system line to every player (vslot 0x54)
			for (const SkirmishGameSetup::StartCheck::Line &line : check.lines)
			{
				if (line.kind == 2)
				{
					systemChat(line.text);
				}
				else
				{
					addChatLine(line.text, 0xFFFFFFFFu);
					if (line.kind == 1)
					{
						windows().note("message-box", "MessageBoxOk(\"\", " + narrow(line.text) + ") (RW 0x84758C)");
					}
				}
			}
			if (!check.ok)
			{
				windows().note("lan-start-refused", check.refusalLabel + ": " + narrow(check.refusal));
				invokeRoot("EnablePlayGame");
				m_state = 4;
				break;
			}
		}
		// every player accepted (MP-2's check, LANAPI::hostStartGame; retail's start check RW 0x8431B8 was not read that far: INFERENCE)
		bool allAccepted = true;
		if (const LANGame *g = m_lan->currentGame())
		{
			for (int s = 0; s < MAX_SLOTS; ++s)
			{
				allAccepted = allAccepted && (s == m_lan->localSlot() || !g->info.slots[(std::size_t)s].isHuman() || g->accepted[(std::size_t)s]);
			}
		}
		if (!allAccepted)
		{
			windows().note("lan-start-refused", "a player has not accepted");
			invokeRoot("EnablePlayGame");
			m_state = 4;
			break;
		}
		// lane UI-1: the check passed: the countdown (RW 0x843840), the start comes from its end (RW 0x84392C)
		if (!armCountdown())
		{
			invokeRoot("EnablePlayGame");
			m_state = 4;
			break;
		}
		m_state = 6;
		break;
	}
	case 6:
		// RW 0x8490E0 state 6: the countdown; refused -> 4 and EnablePlayGame
		if (m_counting)
		{
			const int r = countdownTick();
			if (r == 0)
			{
				invokeRoot("EnablePlayGame");
				m_state = 4;
			}
		}
		else if (!m_lan || !m_lan->started())
		{
			// a cancel (OnCancelBttn) stopped it: the tick reports it
			countdownTick();
			invokeRoot("EnablePlayGame");
			m_state = 4;
		}
		break;
	case 7:
		requestJoinSelected();
		break;
	case 9:
		// RW 0x8490E0 state 9: the Ok box GUI:GSErrorTitle / GUI:GSKicked, the game left
		windows().note("lan-kicked", "GUI:GSKicked");
		showError("GUI:GSErrorTitle", "GUI:GSKicked");
		backToLobby();
		break;
	default:
		break;
	}
	if (m_state == 4 || m_state == 6)
	{
		refreshReadyControls();
	}
}

WindowMsgHandledType AptLanLobby::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	pruneLan();
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	if (gadget && gadget == m_gamesList)
	{
		if (msg == GLM_DOUBLE_CLICKED && m_state == 1)
		{
			if ((int)(std::intptr_t)data2 >= 0)
			{
				GadgetListBoxSetSelected(m_gamesList, (int)(std::intptr_t)data2);
			}
			m_state = 7; // RW 0x8474C3 (0x4015): as OnJoinGameBttn
		}
		return MSG_HANDLED;
	}
	if (gadget && gadget == m_nameEntry && msg == GEM_EDIT_DONE)
	{
		if (m_lan)
		{
			m_lan->requestSetName(GadgetTextEntryGetText(m_nameEntry));
		}
		return MSG_HANDLED;
	}
	return AptSkirmish::gadgetMessage(from, msg, data1, data2);
}

void AptLanLobby::lobbyChanged()
{
	SkirmishGameSetup *model = setupModel();
	if (!m_lan || !model || !m_lan->currentGame())
	{
		return;
	}
	if (m_lan->amIHost())
	{
		m_lan->hostSetOptions(model->info());
		return;
	}
	const SkirmishGameSlot &s = model->info().slots[m_lan->localSlot()];
	m_lan->requestSlotOptions(s.playerTemplate, s.color, s.teamNumber, s.startPos, s.handicap); // lane OWNERFIX-2: the handicap through the host too
}

bool AptLanLobby::lobbyEditable(int slot) const
{
	if (!m_lan || !m_lan->currentGame())
	{
		return false;
	}
	if (slot < 0)
	{
		return m_lan->amIHost(); // the map: the host's
	}
	if (slot == m_lan->localSlot())
	{
		return true;
	}
	return m_lan->amIHost() && !m_lan->currentGame()->info.slots[slot].isHuman(); // the host sets the other slots that are not players
}

bool AptLanLobby::slotAccepted(int slot) const
{
	// lane ONLINE-5: LANAPI keeps the accept flags beside the slots (SET_ACCEPT); the host's own slot has none
	const LANGame *game = m_lan ? m_lan->currentGame() : nullptr;
	return game && slot >= 0 && slot < MAX_SLOTS && game->accepted[(std::size_t)slot];
}

bool AptLanLobby::applySlotHero(int slot, const CreateAHeroHero *hero)
{
	// lane CAH-1: the owner's applySlotHero in a LAN game (BFME2 owner vslot 6): only the local slot's hero, and through the host
	// (LANAPI::requestSlotCreateAHero: the host checks it, Options::validateCreateAHero, and sends the options to every player)
	pruneLan();
	if (!m_lan || !setupModel() || slot != setupModel()->localSlot())
	{
		return false;
	}
	std::string error;
	if (!m_lan->requestSlotCreateAHero(hero, &error))
	{
		windows().note("create-a-hero-lobby", "the hero request was refused: " + error);
		return false;
	}
	return true;
}

void AptLanLobby::onReadyPress(const std::string &argument)
{
	// MpGameSetup::OnReadyPress: a joiner's accept (ZH SET_ACCEPT). The ready box's first frame calls it without an argument (MpGameSetup.apt: once,
	// vReadyInitialized); a press passes the box's slot (the name's suffix): only a press accepts
	if (argument.empty() || argument == "undefined")
	{
		readyBoxesMade(); // lane ONLINE-5: the boxes get their states (RW 0x84211D)
		return;
	}
	if (!m_lan || !m_lan->currentGame() || m_lan->amIHost())
	{
		return;
	}
	// the flag follows the game's: a change of the options unaccepts everybody (ZH), so the next press accepts again
	m_ready = !m_lan->currentGame()->accepted[(size_t)m_lan->localSlot()];
	m_lan->requestAccept(m_ready);
}

void AptLanLobby::onKickPlayer(const std::string &argument)
{
	// MpGameSetup::OnKickPlayer(slot): the host removes the player
	if (!m_lan || !m_lan->amIHost() || argument.empty())
	{
		return;
	}
	m_lan->hostKick(std::atoi(argument.c_str()));
}

void AptLanLobby::onChatSend(const std::u16string &text)
{
	// the chat helper's slot 0x10 (the LAN's: TheLAN->RequestChat, normal type)
	if (!m_lan || !m_lan->currentGame())
	{
		windows().note("command-ignored", "AptMpChat::Send outside a LAN game");
		return;
	}
	m_lan->requestChat(text);
}
