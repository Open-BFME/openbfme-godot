// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: DisconnectScreen.apt through the window manager (GameClient/GUI/AptScreens/AptDisconnectScreen.h): the retail movie loads outside the shell
// stack, the rows' names and votes become its text records (an empty one a single space, RW 0xBD16E4), the bars and kick buttons are the movie's
// functions, and the movie's Kick / Quit commands reach the host with the row (RW 0x919064 / 0x9195E1). SKIP loudly without the retail install.

#include "doctest.h"

#include "AptRetail.h"
#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("mp2 retail: DisconnectScreen.apt shows the rows (names, votes, bars, kick buttons) and hands Kick of a row and Quit to the host")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	WindowManager wm(source, services);
	Shell shell(wm, factories, services, environment);
	wm.init();
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			wm.update(33);
		}
	};
	tick(2);
	AptDisconnectScreen screen(wm, shell);
	REQUIRE(screen.level() >= 1); // the first free level (AptLevel0 holds 0)
	std::vector<int> kicks;
	int quits = 0;
	screen.onKick = [&](int row) { kicks.push_back(row); };
	screen.onQuit = [&]() { ++quits; };
	std::array<AptDisconnectScreen::Row, 7> rows{};
	rows[0].used = true;
	rows[0].nameUtf8 = "Alice";
	rows[0].barPercent = 100;
	rows[1].used = true;
	rows[1].nameUtf8 = "Bob";
	rows[1].barPercent = 42;
	rows[1].votes = 1;
	rows[1].kickShown = true;
	bool applied = false;
	for (int i = 0; i < 60 && !applied; ++i)
	{
		applied = screen.apply(rows);
		tick(1);
	}
	REQUIRE(applied);
	tick(5);
	REQUIRE(wm.aptText("DisconnectScreen::PlayerName0"));
	CHECK(*wm.aptText("DisconnectScreen::PlayerName0") == "Alice");
	CHECK(*wm.aptText("DisconnectScreen::PlayerName1") == "Bob");
	CHECK(*wm.aptText("DisconnectScreen::PlayerName2") == " "); // RW 0xBD16E4
	CHECK(*wm.aptText("DisconnectScreen::VotesReceived1") == "1");
	CHECK(*wm.aptText("DisconnectScreen::VotesReceived0") == " ");
	const std::vector<std::string> &calls = screen.calls();
	CHECK(std::find(calls.begin(), calls.end(), "SetBarPercent 1 42") != calls.end());
	CHECK(std::find(calls.begin(), calls.end(), "ShowKickButton 1") != calls.end());
	// the movie's functions exist (a missing one would be an invoke-failed note)
	CHECK(wm.noteCount("invoke-failed") == 0);
	// only what changed goes out again
	const size_t before = calls.size();
	rows[1].barPercent = 41;
	REQUIRE(screen.apply(rows));
	CHECK(screen.calls().size() == before + 1);
	CHECK(screen.calls().back() == "SetBarPercent 1 41");
	// the movie's commands: Kick with the row as its argument, an empty argument ignored, Quit
	wm.fscommand("AptDisconnectScreen::Kick", "1");
	wm.fscommand("AptDisconnectScreen::Kick", "");
	wm.fscommand("AptDisconnectScreen::Quit", "");
	CHECK(kicks == std::vector<int>{ 1 });
	CHECK(quits == 1);
	// the colour providers answer RW 0x9190A6's constant
	std::string colour;
	CHECK(wm.getExtern("DisconnectScreen:PlayerColor:3", colour) == AptExternResult::Value);
	CHECK(colour == "0xFFFFFFFF");
}
