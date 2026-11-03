// OpenBFME. GPL-3.0.
// Lane MENUWORK-1: GameSorter, the game lists' sort order (RW 0x9888B7 / 0x9881DE, BFME2 0x5803C0; GameNetwork/GameSorter.h).
#include "GameClient/GUI/Skirmish/LivingWorldShellInfo.h"
#include "doctest.h"

#include "GameNetwork/GameSorter.h"

#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameNetwork/LANAPI.h"
#include "StartShellFx.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
GameSorter::Row row(const char16_t *name, const char *map, int openOrOccupied, int players, bool userMap = false, bool rules = false, bool inProgress = false,
	int mode = 0)
{
	GameSorter::Row r;
	r.name = name;
	r.map = map;
	r.openOrOccupied = openOrOccupied;
	r.players = players;
	r.icon[0] = userMap;
	r.icon[1] = rules;
	r.icon[2] = inProgress;
	r.mode = mode;
	return r;
}

std::vector<std::u16string> names(const GameSorter &sorter, const std::vector<GameSorter::Row> &rows)
{
	std::vector<std::u16string> out;
	for (size_t i : sorter.order(rows))
	{
		out.push_back(rows[i].name);
	}
	return out;
}
} // namespace

TEST_CASE("menuwork1 GameSorter: RotWK's key selection (RW 0x9881DE)")
{
	GameSorter s;
	// the constructor's keys (RW 0x9888B7): players, then ping; the status cycle 0
	CHECK(s.primary() == GameSorter::KEY_PLAYERS);
	CHECK(s.secondary() == GameSorter::KEY_PING);
	CHECK(s.statusCycle() == 0);
	CHECK_FALSE(s.takeChanged());
	// another key: it becomes the primary, the old primary the secondary
	s.select(GameSorter::KEY_NAME);
	CHECK(s.primary() == GameSorter::KEY_NAME);
	CHECK(s.secondary() == GameSorter::KEY_PLAYERS);
	CHECK(s.takeChanged());
	CHECK_FALSE(s.takeChanged()); // test-and-clear
	// the same key again: its direction flips, the secondary stays
	s.select(GameSorter::KEY_NAME);
	CHECK(s.primary() == (GameSorter::KEY_NAME | GameSorter::KEY_DESCENDING));
	CHECK(s.secondary() == GameSorter::KEY_PLAYERS);
	s.select(GameSorter::KEY_NAME);
	CHECK(s.primary() == GameSorter::KEY_NAME);
	// a descending primary is still "the same key" for the next press of another
	s.select(GameSorter::KEY_NAME);
	s.select(GameSorter::KEY_MAP);
	CHECK(s.primary() == GameSorter::KEY_MAP);
	CHECK(s.secondary() == (GameSorter::KEY_NAME | GameSorter::KEY_DESCENDING));
	// Status presses advance the cycle (+ 0x18)
	s.select(GameSorter::KEY_STATUS);
	s.select(GameSorter::KEY_STATUS);
	CHECK(s.statusCycle() == 2);
	CHECK(s.primary() == (GameSorter::KEY_STATUS | GameSorter::KEY_DESCENDING));
}

TEST_CASE("menuwork1 GameSorter: RotWK's comparator (RW 0x988418, BFME2 0x9803C0)")
{
	// gimli and boromir have the same open-or-occupied slots (vslot 9) and differ in the occupied ones (vslot 8); their names sort the other way
	const std::vector<GameSorter::Row> rows = {
		row(u"gimli", "maps/map mp fords of isen/map mp fords of isen.map", 4, 2),
		row(u"Aragorn", "maps/map mp evendim/map mp evendim.map", 2, 1, true),
		row(u"boromir", "maps/map mp evendim/map mp evendim.map", 4, 3, false, false, true),
		row(u"Legolas", "Maps/Map MP Anduin/map mp anduin.map", 3, 2, false, true),
	};
	GameSorter s;
	// the start (players, then ping): vslot 9 ascending; gimli before boromir by vslot 8 (ascending), not by name
	CHECK(names(s, rows) == std::vector<std::u16string>{ u"Aragorn", u"Legolas", u"gimli", u"boromir" });
	// players descending: vslot 9 descending, but vslot 8 stays ascending (RotWK's case 4 returns diff08 < 0 whatever the direction)
	s.select(GameSorter::KEY_PLAYERS);
	CHECK(names(s, rows) == std::vector<std::u16string>{ u"gimli", u"boromir", u"Legolas", u"Aragorn" });
	// name ascending, without case
	s.select(GameSorter::KEY_NAME);
	CHECK(names(s, rows) == std::vector<std::u16string>{ u"Aragorn", u"boromir", u"gimli", u"Legolas" });
	// map ascending (without case); the two Evendim games by the secondary key (name ascending)
	s.select(GameSorter::KEY_MAP);
	CHECK(names(s, rows) == std::vector<std::u16string>{ u"Legolas", u"Aragorn", u"boromir", u"gimli" });
	// Status (cycle 1: the icons from index 1: non-default rules, in progress, user map): a game with the icon first, no direction
	GameSorter st;
	st.select(GameSorter::KEY_STATUS);
	CHECK(names(st, rows) == std::vector<std::u16string>{ u"Legolas", u"boromir", u"Aragorn", u"gimli" });
	// the second press (cycle 2: in progress, user map, rules): a different order, not a reversed one
	st.select(GameSorter::KEY_STATUS);
	CHECK(names(st, rows) == std::vector<std::u16string>{ u"boromir", u"Aragorn", u"Legolas", u"gimli" });
}

TEST_CASE("menuwork1 GameSorter: maps compare only within a mode (+ 0x5C), the last tie is the list's order")
{
	GameSorter s;
	s.select(GameSorter::KEY_MAP); // primary map, secondary players
	// same counts: in one mode the map decides (aaa before zzz); across modes the maps are "equal" and the name decides (abe before zed)
	const std::vector<GameSorter::Row> sameMode = { row(u"zed", "aaa", 4, 2, false, false, false, 0), row(u"abe", "zzz", 4, 2, false, false, false, 0) };
	CHECK(names(s, sameMode) == std::vector<std::u16string>{ u"zed", u"abe" });
	const std::vector<GameSorter::Row> mixed = { row(u"zed", "aaa", 4, 2, false, false, false, 0), row(u"abe", "zzz", 4, 2, false, false, false, 1) };
	CHECK(names(s, mixed) == std::vector<std::u16string>{ u"abe", u"zed" });
	// everything equal: the list's order (RotWK: the pointers' order, S-4751)
	const std::vector<GameSorter::Row> twins = { row(u"Same", "m", 4, 2), row(u"same", "M", 4, 2) };
	CHECK(names(s, twins) == std::vector<std::u16string>{ u"Same", u"same" });
	CHECK(std::string(GameSorter::stopLine()).rfind("[S-4751]", 0) == 0);
}

// Lane MENUWORK-1: the LAN lobby's buttons the menu inventory found doing nothing: the game list's headers (GameSorter, RotWK's AptLanLobby member)
// and Refresh now reach a handler; the War of the Ring page and Load Game tell the player they are not available, and War of the Ring's Create Game is
// refused while Open Play's still hosts (the control: the refusal is the page's). SKIP loudly without the retail installs.
TEST_CASE("menuwork1 retail: LanLobby.apt: the list headers sort, Refresh refills, War of the Ring and Load Game say they are not available")
{
	OPENBFME_REQUIRE_START(s);
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity profile = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
	LANAPI::Options o;
	o.playerTemplateCount = s->world->playerTemplates().getPlayerTemplateCount();
	o.colorCount = (int)s->settings.multiplayerColors.size();
#ifdef _WIN32
	const int pid = 4322;
#else
	const int pid = (int)getpid();
#endif
	o.lobbyPortBase = (std::uint16_t)(45000 + (pid % 1000) * 16);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	auto lan = std::make_unique<LANAPI>(profile, u"Host", o);
	std::string error;
	REQUIRE_MESSAGE(lan->open(&error), error);
	starttest::ShellFx fx(*s, 13);
	fx.environment.lan = lan.get();
	fx.environment.services = &fx.services;
	fx.shell->push("LanLobby.apt");
	REQUIRE(fx.shell->errors().empty());
	AptLanLobby *lobby = dynamic_cast<AptLanLobby *>(fx.shell->top());
	REQUIRE(lobby);
	auto run = [&](const std::function<bool()> &done) {
		for (int i = 0; i < 300; ++i)
		{
			fx.tick(1);
			if (done())
			{
				return true;
			}
		}
		return false;
	};
	auto unhandled = [&] {
		return std::any_of(fx.wm->notes().begin(), fx.wm->notes().end(), [](const WindowManagerNote &n) { return n.kind == "command-unhandled"; });
	};
	REQUIRE(run([&] { return lobby->state() == 1; }));
	// the headers: RotWK's key selection (RW 0x9881DE) on the screen's own sorter
	CHECK(fx.wm->invokeCallback("GameSorter::OnSortName", ""));
	CHECK(lobby->sorter().primary() == GameSorter::KEY_NAME);
	CHECK(fx.wm->invokeCallback("GameSorter::OnSortName", ""));
	CHECK(lobby->sorter().primary() == (GameSorter::KEY_NAME | GameSorter::KEY_DESCENDING));
	for (const char *cmd : { "GameSorter::OnSortMap", "GameSorter::OnSortPlayers", "GameSorter::OnSortPing", "GameSorter::OnSortStatus",
			 "AptLanLobby::CustomMatch::Refresh" })
	{
		CHECK_MESSAGE(fx.wm->invokeCallback(cmd, ""), cmd);
	}
	CHECK(lobby->sorter().primary() == GameSorter::KEY_STATUS);
	CHECK_FALSE(unhandled());
	// the sort's inference is reported when the screen is made (S-4751)
	CHECK(std::any_of(fx.wm->notes().begin(), fx.wm->notes().end(),
		[](const WindowManagerNote &n) { return n.kind == "lan-stops" && n.detail == GameSorter::stopLine(); }));
	// Load Game: a box says it is not available
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnLoadGameBttn", ""));
	REQUIRE(lobby->messageBox());
	CHECK(lobby->messageBox()->text() == u"Loading a saved network game is not available yet: start a new game instead.");
	// the War of the Ring page: the box on opening, its Create Game refused (no game hosted)
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnLoadScreen", "LanStrategic"));
	CHECK(lobby->messageBox()->text() == AptLanLobby::kStrategicUnavailable);
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	CHECK_FALSE(run([&] { return lobby->state() != 1 || lan->currentGame() != nullptr; }));
	// back on Open Play the same press hosts a game
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnLoadScreen", "LanOpenPlay"));
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	CHECK(run([&] { return lobby->state() == 4 && lan->amIHost(); }));
	// lane STABLEQA-1: a Stable build (warOfTheRingOffered false), even with the War of the Ring scenarios: the page names the Nightly version, its
	// Create Game hosts nothing and its Load Game asks for no LAN war save
	LivingWorldShellInfo lw;
	lw.scenarios.push_back(LivingWorldScenarioEntry{});
	fx.environment.livingWorld = &lw;
	fx.environment.warOfTheRingOffered = false;
	const std::size_t requests = fx.services.requests.size();
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnLoadScreen", "LanStrategic"));
	CHECK(lobby->messageBox()->text() == AptLanLobby::kStrategicStable);
	CHECK(fx.wm->invokeCallback("AptLanLobby::OnLoadGameBttn", ""));
	CHECK(lobby->messageBox()->text() == AptLanLobby::kStrategicStable);
	CHECK(fx.services.requests.size() == requests);
	fx.environment.livingWorld = nullptr;
}
