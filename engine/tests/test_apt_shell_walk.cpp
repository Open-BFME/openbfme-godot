// OpenBFME unit tests. GPL-3.0.
//
// Lane APT-5: the shell walk of the retail movies, headless, screen by screen: MainMenu (revealed, the Solo Play nav), the Skirmish lobby
// (profile, map, faction, colour), Options, the LoadScreen and the in-game Palantir.  Each screen must log no script error; the calls without
// a function the retail movies make (stop S-380: the retail interpreter skips them silently) are pinned per screen with their root cause, so a
// new one is a test failure, not a silent change.

#include "doctest.h"
#include "StartShellFx.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace starttest;

namespace
{
struct ScreenLog
{
	std::vector<std::string> errors;
	std::vector<std::string> calls; // call-without-function notes
};

struct Walk
{
	ShellFx &fx;
	std::size_t errorsSeen = 0;
	std::size_t notesSeen = 0;

	ScreenLog take()
	{
		ScreenLog log;
		for (; errorsSeen < fx.wm->errors().size(); ++errorsSeen)
		{
			log.errors.push_back(fx.wm->errors()[errorsSeen]);
		}
		const std::vector<AptNote> &notes = fx.wm->apt().notes();
		for (; notesSeen < notes.size(); ++notesSeen)
		{
			if (notes[notesSeen].kind == "call-without-function")
			{
				log.calls.push_back(notes[notesSeen].detail);
			}
		}
		std::sort(log.calls.begin(), log.calls.end());
		return log;
	}
};

std::string joined(const std::vector<std::string> &v)
{
	std::string s;
	for (const std::string &e : v)
	{
		s += "\n  " + e;
	}
	return s;
}

// `remainingErrors`: script errors that are still the port's gaps (an extern provider whose retail answer was not read), pinned so they can
// only shrink; every other screen must log none.
void expectScreen(const char *screen, const ScreenLog &log, std::vector<std::string> expectedCalls, const std::vector<std::string> &remainingErrors = {})
{
	INFO(screen);
	// distinct errors: how often a provider is polled depends on how long the screen is ticked
	std::vector<std::string> distinct = log.errors;
	std::sort(distinct.begin(), distinct.end());
	distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
	std::vector<std::string> expected = remainingErrors;
	std::sort(expected.begin(), expected.end());
	CHECK_MESSAGE(distinct == expected, "script errors:" << joined(distinct));
	std::sort(expectedCalls.begin(), expectedCalls.end());
	CHECK_MESSAGE(log.calls == expectedCalls, "calls without a function:" << joined(log.calls));
}
} // namespace

TEST_CASE("retail shell walk: MainMenu, Skirmish lobby, Options, LoadScreen and Palantir log no script error; the retail calls without a function are pinned (S-380)")
{
	OPENBFME_REQUIRE_START(s);
	ShellFx fx(*s, 4242);
	Walk walk{ fx };
	fx.tick(2);
	expectScreen("AptLevel0", walk.take(), {});

	// ---- MainMenu ----
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	const int menuLevel = fx.shell->top()->level();
	std::string asError;
	REQUIRE_MESSAGE(fx.wm->invokeAS(menuLevel, "ShowMainMenu", {}, nullptr, &asError), asError);
	fx.tick(60);
	// InitialSetup stops the open buttons of every nav, TutorialsNav included: RotWK's MainMenu.apt names it in the constant pool but never places it
	expectScreen("MainMenu", walk.take(), { "method 'stop' is not a function on a value of type undefined [in _level1]" });

	// ---- Skirmish lobby ----
	clickNavItem(fx, menuLevel, "SoloPlayNav", "Skirmish");
	REQUIRE(fx.shell->errors().empty());
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	fx.tick(60);
	REQUIRE(screen->setup());
	REQUIRE(screen->profileEntryWindow());
	typeText(fx, screen->profileEntryWindow(), u"Gimli");
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(40);
	GameWindow *list = screen->mapListWindow();
	REQUIRE(list);
	wheelAndClickRow(fx, list, 3);
	fx.tick(10);
	GameWindow *faction = screen->slotGadget(0, "PlayerTemplate");
	REQUIRE(faction);
	chooseComboRow(fx, faction, 2);
	fx.tick(10);
	GameWindow *colour = screen->slotGadget(0, "Color");
	REQUIRE(colour);
	chooseImageComboRow(fx, colour, 3);
	fx.tick(30);
	const ScreenLog lobby = walk.take();
	// SkirmishOpenPlay.apt's frame action calls SetProgressBar(-1), a function no retail movie defines (the name occurs only there)
	expectScreen("Skirmish", lobby, { "call of undefined function 'SetProgressBar' (EA_CallFuncPop at file offset 4996) [in _level2.OpenPlay]" });

	// ---- Start ----
	const int lobbyLevel = fx.shell->top()->level();
	AptButtonInst *start = popupButton(fx, "lobby.StartGame");
	REQUIRE(start);
	REQUIRE(fx.sink.messages.empty());
	clickButton(fx, *start);
	fx.tick(30);
	REQUIRE_MESSAGE(fx.sink.messages.size() == 1, "refusals " << fx.sink.refusals.size());
	// The receivers the movie's Start route names but Skirmish.apt does not have (retail skips the four calls too):
	//  - DoButtonNav (run by the StartGame button, `this` = lobby.StartGame) disables `StartGame` and `Back` by bare names: they resolve on
	//    Skirmish's _root, where neither exists (both buttons live under `lobby`, and `lobby` has no `Back` at all);
	//  - the _close frame plays `_root[vScreenName]` and `lobby.Back`: vScreenName is undefined on the root timeline, and lobby.Back does not exist.
	CHECK(fx.at(lobbyLevel, "StartGame") == nullptr);
	CHECK(fx.at(lobbyLevel, "Back") == nullptr);
	CHECK(fx.at(lobbyLevel, "lobby.Back") == nullptr);
	CHECK(fx.at(lobbyLevel, "lobby.StartGame") != nullptr);
	{
		// the _close frame (root frame 14) plays _root[vScreenName]: vScreenName is a local of OpenMainMovie (EA_CallNamedMethod defines it in
		// the function's scope), so on the root timeline it is undefined and so is the receiver
		AptValue screenName;
		CHECK_FALSE(fx.wm->apt().level(lobbyLevel)->getMember("vScreenName", screenName));
	}
	expectScreen("Skirmish Start", walk.take(),
		{ "method 'gotoAndStop' is not a function on a value of type undefined [in _level2.lobby.StartGame]",
			"method 'gotoAndStop' is not a function on a value of type undefined [in _level2.lobby.StartGame]",
			"method 'gotoAndPlay' is not a function on a value of type undefined [in _level2]",
			"method 'gotoAndPlay' is not a function on a value of type undefined [in _level2]" });

	// ---- back to the menu, Options ----
	fx.shell->pop();
	fx.tick(30);
	expectScreen("Skirmish -> MainMenu", walk.take(), {});
	fx.shell->push("Options.apt");
	fx.tick(90);
	// frame 0 calls _root.assignOpen, which plays Main: Options.apt places Main on frame 1
	// lane FB7-1: the Options externs (AdvancedOnly, NetworkEnabled, AllowAdvancedOptions, AllowResolutionChange) have RotWK's provider (RW 0x91F7F5): no
	// script error is left
	expectScreen("Options", walk.take(), { "method 'gotoAndPlay' is not a function on a value of type undefined [in _level2]" });
	fx.shell->pop();
	fx.tick(30);
	expectScreen("Options -> MainMenu", walk.take(), {});

	// ---- the load screen, then the game's HUD ----
	while (fx.shell->screenCount() > 0)
	{
		fx.shell->pop();
		fx.tick(1);
	}
	fx.shell->push("LoadScreen.apt");
	fx.tick(60);
	// AnimsOpen plays `screen` and `pageTitle`, which LoadScreen.apt never places
	expectScreen("LoadScreen", walk.take(), { "method 'gotoAndPlay' is not a function on a value of type undefined [in _level1]",
		"method 'gotoAndPlay' is not a function on a value of type undefined [in _level1]" });
	fx.shell->pop();
	fx.tick(1);
	fx.shell->push("Palantir.apt");
	fx.tick(120);
	// InitialSetup asks for ShowHeroSelectInterface right after it requested InGameHeroSelect.swf, which defines it: a requested load completes at the
	// end of the step (load tracker 0x00AD17F0); InitGlobeUIs -> PlayElvenEffect plays EmptyGlobe.ElvenEffect, placed only on EmptyGlobe's frame 10
	// (_show) while EmptyGlobe waits on frame 0 (_hide); the side command bar's Button<i> (i = 0..10) calls UpdateFrameState of Button<i+1> from its
	// frame 0 before Button<i+1>'s frame 0 has defined it (the retail order: test "player: siblings placed on one frame ...")
	std::vector<std::string> palantirCalls = { "method 'ShowHeroSelectInterface' is not a function on a value of type movieclip [in _level1]",
		"method 'gotoAndPlay' is not a function on a value of type undefined [in _level1]" };
	for (int i = 0; i <= 10; ++i)
	{
		palantirCalls.push_back("method 'UpdateFrameState' is not a function on a value of type movieclip [in _level1.SideCommandBar.ButtonSet.Button" + std::to_string(i) + "]");
	}
	// extern.PalantirMinLOD and extern.DoTrace answer the string "0", which is true in an Apt 7 movie (AptValue::toBoolean; the retail extern value is an Apt
	// string too, RW 0x4A8C36), so InitialSetup writes and traces extern.MinLOD: RotWK has no handler for it and answers as its host does for an unknown name
	// (RW 0x623B47 / 0x623AD3: "" / dropped, lane HUD-4, QA-1 U18), noted, not an error
	expectScreen("Palantir", walk.take(), palantirCalls, {});
}
