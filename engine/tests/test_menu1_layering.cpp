// OpenBFME unit tests. GPL-3.0.
//
// Lane MENU-1: the owner's front-end layering bugs of preview.4 (2026-10-10), walked headless over the retail movies with real input: after every step
// at most one screen (and one sub-screen of it) may draw, no two titles may overprint (GameClient/GUI/Shell/ShellLayering.h), and a modal box keeps
// the input from the widgets under it.

#include "doctest.h"
#include "StartShellFx.h"

#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/ShellLayering.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <string>

using namespace starttest;

namespace
{
ShellLayeringReport layering(ShellFx &fx)
{
	AptRenderList rl;
	fx.wm->apt().buildRenderList(rl);
	return checkShellLayering(*fx.wm, *fx.shell, rl);
}

std::string describe(const ShellLayeringReport &r)
{
	std::string s = "top " + r.topScreen + "; screens";
	for (const std::string &x : r.visibleScreens)
	{
		s += " " + x;
	}
	s += "; movies";
	for (const std::string &x : r.visibleMovies)
	{
		s += " " + x;
	}
	s += "; gadgets " + std::to_string(r.shownGadgets) + " shown, " + std::to_string(r.enabledGadgets) + " enabled; input blocked " + (r.componentInputBlocked ? "yes" : "no");
	for (const std::string &p : r.problems)
	{
		s += "\n  PROBLEM " + p;
	}
	return s;
}

void expectClean(ShellFx &fx, const char *step)
{
	const ShellLayeringReport r = layering(fx);
	INFO(step << ": " << describe(r));
	CHECK(r.problems.empty());
}

// the hittable button under `path` of the top screen
AptButtonInst *topButton(ShellFx &fx, const std::string &path)
{
	return firstButtonIn(fx.at(fx.shell->top()->level(), path));
}

bool waitFor(ShellFx &fx, const std::string &path, int frames)
{
	for (int i = 0; i < frames; ++i)
	{
		if (AptButtonInst *b = topButton(fx, path))
		{
			if (b->globallyVisible())
			{
				return true;
			}
		}
		fx.tick(1);
	}
	return false;
}
} // namespace

TEST_CASE("menu1 layering retail: Skirmish's first visit asks for a profile in a modal box over the lobby, the profile page replaces the game setup, "
	"and every step draws one screen without overprinting titles")
{
	OPENBFME_REQUIRE_START(s);
	ShellFx fx(*s, 4242);
	fx.tick(2);
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	const int menuLevel = fx.shell->top()->level();
	std::string asError;
	REQUIRE_MESSAGE(fx.wm->invokeAS(menuLevel, "ShowMainMenu", {}, nullptr, &asError), asError);
	fx.tick(60);
	expectClean(fx, "main menu");

	// ---- Skirmish: no profile yet, the add-profile box opens over the lobby ----
	clickNavItem(fx, menuLevel, "SoloPlayNav", "Skirmish");
	fx.tick(90);
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	expectClean(fx, "the profile prompt");
	{
		// RotWK's Skirmish.apt: OpenMainMovie sets _root.vScreenName (EA's CallMethodSetVar, RW 0xB1D440), OpenPop disables the components under
		// _root[vScreenName] (DisableComponents, RW 0x81456F): the game setup's gadgets are disabled, the box's name entry is not
		AptValue screenName;
		REQUIRE(fx.wm->apt().level(screen->level())->getMember("vScreenName", screenName));
		CHECK(screenName.toString() == "OpenPlay");
		const ShellLayeringReport r = layering(fx);
		CHECK_FALSE(r.componentInputBlocked);
		REQUIRE(screen->profileEntryWindow());
		CHECK(BitTest(screen->profileEntryWindow()->winGetStatus(), WIN_STATUS_ENABLED));
		GameWindow *army = screen->slotGadget(0, "PlayerTemplate");
		REQUIRE(army);
		CHECK_FALSE(BitTest(army->winGetStatus(), WIN_STATUS_ENABLED));
		// a click on the covered Army combo does not open it
		clickWindowCentre(fx, army);
		fx.tick(5);
		GameWindow *list = GadgetComboBoxGetListBox(army);
		CHECK((list == nullptr || list->winIsHidden()));
	}
	typeText(fx, screen->profileEntryWindow(), u"asdasd");
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(60);
	expectClean(fx, "the lobby");
	{
		const ShellLayeringReport r = layering(fx);
		CHECK_FALSE(r.componentInputBlocked);
		GameWindow *army = screen->slotGadget(0, "PlayerTemplate");
		REQUIRE(army);
		CHECK(BitTest(army->winGetStatus(), WIN_STATUS_ENABLED)); // ClosePop's EnableComponents
	}

	// ---- PROFILE -> the profile page (Stats.swf in SkirmishStats) replaces the game setup ----
	REQUIRE(waitFor(fx, "ProfileNav", 60));
	clickButton(fx, *topButton(fx, "ProfileNav"));
	REQUIRE(waitFor(fx, "ProfileNav.ProfileStats", 120));
	fx.tick(20);
	clickButton(fx, *topButton(fx, "ProfileNav.ProfileStats"));
	fx.tick(150);
	expectClean(fx, "the profile page");
	{
		const ShellLayeringReport r = layering(fx);
		INFO(describe(r));
		REQUIRE(r.visibleMovies.size() == 1);
		CHECK(r.visibleMovies[0].find("SkirmishStats") != std::string::npos);
	}
	REQUIRE(waitFor(fx, "stats.BackProfile", 60));
	clickButton(fx, *topButton(fx, "stats.BackProfile"));
	fx.tick(150);
	for (const std::string &n : fx.layer->gadgets().notes())
	{
		CHECK_MESSAGE(n.find("pool is full") == std::string::npos, n);
	}
	expectClean(fx, "the lobby again");
	{
		const ShellLayeringReport r = layering(fx);
		INFO(describe(r));
		REQUIRE(r.visibleMovies.size() == 1);
		CHECK(r.visibleMovies[0].find("OpenPlay") != std::string::npos);
	}
}
