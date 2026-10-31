// OpenBFME tests, lane MENUSND-1: the menus' hover and click sounds. GPL-3.0. They SKIP loudly without the installs.
//
// RotWK plays every menu sound from the movies' ActionScript: a button calls _root.PlaySound(<event>), the movie's PlaySound function sends
// getURL("FSCommand:PlaySound", <event>) while _root.Initialized is set (its InitialSetup sets it), and the game adds the event (FSCommand PlaySound,
// RW 0x815772, registered by RW 0x815C7D; WindowManager::registerBuiltinCallbacks -> ShellServices::playSound). The game's own gadgets add none: of
// the GUI* events only GUIControlButtonClick is named in the binary (string 0xC22F48), and RotWK's INI gives it no sound file.
//
// The inventory below is the retail data, read from the movies (not from this port): per control, the events its pointer-on, press and release ask for.
//   * MenuExport.apt's GenericButton (Short / Med / Small): the button's actions go to the sprite's `_over` frame on the pointer (transition 1), whose action
//     plays Gui_ShellMapMouseOver, and to `_down` on the press (transition 4), whose action plays Gui_ShellMapSelect; its click (OnButtonClick) is the
//     `_release end` frame. A release asks for no Select (the `_release end` frame may go back to `_over`, which plays its hover again).
//   * QuitMenu.apt's TheButton (button 12), LanLobby.apt's buttons (button 45), OnlineLogin's buttonClip (button 98): Gui_ShellMapMouseOver on
//     transition 1, Gui_ShellMapSelect on the release (transition 8), nothing on the press.
//   * OnlineLogin's Exit: a GenericButton whose clip's DoButtonCallback (its Load clip event) plays Gui_ShellMapSelect once more on the click.
// Each control is pressed in a fresh shell (a click may leave the screen) with the Apt input's pointer, the way the hover tests do (test_hover1_menus.cpp).
// The main menu's navigations (their OpenButton sounds; their entries, MenuExport.apt's menu1_sub01, have no PlaySound in RotWK and stay silent) and
// the Options bar of the OpenBFME menu are checked in the running game (godot/scripts/menusnd_check.gd) and in test_fb7_menus.cpp; the Palantir's in
// test_hud_palantir.cpp.

#include "doctest.h"

#include "AptRetail.h"

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
const char *const kOver = "Gui_ShellMapMouseOver";
const char *const kSelect = "Gui_ShellMapSelect";

struct SoundRig
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<Shell> shell;
	std::unique_ptr<AptGuiFXScreen> guiFX;
	explicit SoundRig(AptRetail &mount) : source(mount.fs)
	{
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->init();
		guiFX = std::make_unique<AptGuiFXScreen>(*wm, *shell); // the window manager's GuiFX.apt
		tick(10);
	}
	~SoundRig()
	{
		guiFX.reset();
		shell.reset();
		wm.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
	static void buttons(AptCharacterInst *c, std::vector<AptButtonInst *> &out)
	{
		if (!c)
		{
			return;
		}
		if (AptButtonInst *b = c->asButton())
		{
			out.push_back(b);
		}
		if (AptSpriteInst *s = c->asSprite())
		{
			for (AptCharacterInst *k : s->children())
			{
				buttons(k, out);
			}
		}
	}
	// "<instance names below the level>/<button name>" (an unnamed instance is an empty part)
	static std::string pathOf(const AptButtonInst &b)
	{
		std::vector<const AptCharacterInst *> chain;
		for (const AptCharacterInst *c = &b; c && c->parent(); c = c->parent())
		{
			chain.push_back(c);
		}
		std::string out;
		for (size_t i = chain.size(); i-- > 0;)
		{
			out += "/" + chain[i]->instName();
		}
		return out.empty() ? out : out.substr(1);
	}
	// a point of the button's mesh where the input's hit test finds this button (the centre, else a 9 x 9 grid over its bounds)
	bool hitPoint(AptButtonInst &b, float &x, float &y)
	{
		float x0, y0, x1, y1;
		if (!b.contentBounds(x0, y0, x1, y1))
		{
			return false;
		}
		for (int i = -1; i < 81; ++i)
		{
			const float fx = i < 0 ? 0.5f : (float)(i % 9 + 1) / 10.0f, fy = i < 0 ? 0.5f : (float)(i / 9 + 1) / 10.0f;
			b.globalMatrix().apply(x0 + (x1 - x0) * fx, y0 + (y1 - y0) * fy, x, y);
			if (wm->apt().input().hitTestButtons(x, y) == &b)
			{
				return true;
			}
		}
		return false;
	}
	std::vector<std::string> take()
	{
		std::vector<std::string> out;
		out.swap(services.sounds);
		return out;
	}
};

enum class Click
{
	OnPress,       // GenericButton: Select on the press, none on the release
	OnRelease,     // TheButton / LAN buttons / buttonClip: Select on the release only
	PressAndClick, // OnlineLogin's Exit: Select on the press and once more on the click
};

struct Control
{
	const char *movie, *path;
	Click click;
};

std::string listed(const std::vector<std::string> &v)
{
	std::string s;
	for (const std::string &x : v)
	{
		s += (s.empty() ? "" : ", ") + x;
	}
	return "[" + s + "]";
}

int countOf(const std::vector<std::string> &v, const char *name)
{
	int n = 0;
	for (const std::string &x : v)
	{
		n += x == name ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("menusnd1 retail: every inventoried menu control asks for RotWK's hover and click sounds, at the moment the movie plays them")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	static const Control kControls[] = {
		// Options.apt (its retail page; the OpenBFME menu's bar: test_fb7_menus.cpp "menusnd1 options")
		{ "Options", "Buttons/Cancel/bttn", Click::OnPress },
		{ "Options", "Buttons/Accept/bttn", Click::OnPress },
		{ "Options", "Buttons/Default/bttn", Click::OnPress },
		// QuitMenu.apt
		{ "QuitMenu", "Options/TheButton", Click::OnRelease },
		{ "QuitMenu", "Save/TheButton", Click::OnRelease },
		{ "QuitMenu", "Load/TheButton", Click::OnRelease },
		{ "QuitMenu", "Restart/TheButton", Click::OnRelease },
		{ "QuitMenu", "ExitMission/TheButton", Click::OnRelease },
		{ "QuitMenu", "Resume/TheButton", Click::OnRelease },
		// SaveLoad.apt
		{ "SaveLoad", "MainButtons/Cancel/bttn", Click::OnPress },
		{ "SaveLoad", "MainButtons/Delete/bttn", Click::OnPress },
		{ "SaveLoad", "MainButtons/Load/bttn", Click::OnPress },
		// CampaignMenu.apt
		{ "CampaignMenu", "Buttons/MainMenu/bttn", Click::OnPress },
		{ "CampaignMenu", "Buttons/LastMission/bttn", Click::OnPress },
		{ "CampaignMenu", "Buttons/LoadGame/bttn", Click::OnPress },
		// LanLobby.apt
		{ "LanLobby", "MainButtons/MainMenu/bttn", Click::OnPress },
		{ "LanLobby", "MainButtons/Options/bttn", Click::OnPress },
		{ "LanLobby", "LanOpenPlay/LobbyMain/TopRightFrame/CreateGame/", Click::OnRelease },
		{ "LanLobby", "LanOpenPlay/LobbyMain//OnSortName/OnSortName", Click::OnRelease },
		{ "LanLobby", "LanOpenPlay/LobbyMain//OnSortPlayers/OnSortPlayers", Click::OnRelease },
		// OnlineShell.apt's login page
		{ "OnlineShell", "OnlineLogin/Login/LogIn/bttn", Click::OnPress },
		{ "OnlineShell", "OnlineLogin/Login/OfficialSite/buttonClip", Click::OnRelease },
		{ "OnlineShell", "OnlineLogin/Login/ServiceTerms/buttonClip", Click::OnRelease },
		{ "OnlineShell", "OnlineLogin/Login/Register/buttonClip", Click::OnRelease },
		{ "OnlineShell", "OnlineLogin/Exit/bttn", Click::PressAndClick },
		// Skirmish.apt, CreateAHero.apt, TimeLine.apt
		{ "Skirmish", "ProfilePopup/Main/Cancel/bttn", Click::OnPress },
		{ "CreateAHero", "Main/mcNewFeaturesScreen/mcClassButtons/MainMenu/bttn", Click::OnPress },
		{ "CreateAHero", "Main/mcNewFeaturesScreen/mcClassButtons/Continue/bttn", Click::OnPress },
		{ "TimeLine", "Main/MainButtons/Skip/bttn", Click::OnPress },
	};
	int checked = 0;
	for (const Control &c : kControls)
	{
		INFO(c.movie << ": " << c.path);
		SoundRig r(mount);
		r.shell->push(std::string(c.movie) + ".apt");
		r.tick(150); // the screen's opening animation
		REQUIRE(r.shell->top());
		AptSpriteInst *level = r.wm->apt().level(r.shell->top()->level());
		std::vector<AptButtonInst *> all;
		SoundRig::buttons(level, all);
		AptButtonInst *button = nullptr;
		for (AptButtonInst *b : all)
		{
			button = !button && SoundRig::pathOf(*b) == c.path ? b : button;
		}
		REQUIRE_MESSAGE(button, "the screen shows no button " << c.path);
		float x, y;
		REQUIRE_MESSAGE(r.hitPoint(*button, x, y), "the pointer cannot reach " << c.path);
		r.wm->apt().input().postMouseMove(5.0f, 5.0f);
		r.tick(5);
		r.take();
		r.wm->apt().input().postMouseMove(x, y);
		r.tick(20);
		const std::vector<std::string> hover = r.take();
		CHECK_MESSAGE(hover == std::vector<std::string>{ kOver }, "pointer on it: " << listed(hover));
		r.wm->apt().input().postMouseButton(true);
		r.tick(2);
		const std::vector<std::string> press = r.take();
		r.wm->apt().input().postMouseButton(false);
		r.tick(30);
		const std::vector<std::string> release = r.take();
		switch (c.click)
		{
			case Click::OnPress:
				CHECK_MESSAGE(press == std::vector<std::string>{ kSelect }, "press: " << listed(press));
				CHECK_MESSAGE(countOf(release, kSelect) == 0, "release: " << listed(release));
				break;
			case Click::OnRelease:
				CHECK_MESSAGE(press.empty(), "press: " << listed(press));
				CHECK_MESSAGE(release == std::vector<std::string>{ kSelect }, "release: " << listed(release));
				break;
			case Click::PressAndClick:
				CHECK_MESSAGE(press == std::vector<std::string>{ kSelect }, "press: " << listed(press));
				CHECK_MESSAGE(countOf(release, kSelect) == 1, "release: " << listed(release));
				break;
		}
		++checked;
	}
	CHECK(checked == (int)(sizeof(kControls) / sizeof(kControls[0])));
}

// the shell's PlaySound gate: a movie whose _root.Initialized is not set yet (its InitialSetup has not run) asks for nothing; the press of a control is
// silent while the flag is clear and sounds again once it is set (the movie's PlaySound function, e.g. Options.apt root frame 0: if (!_root.Initialized)
// return). Guards the inventory above against passing through some other path than the movies' own PlaySound
TEST_CASE("menusnd1 retail: the movies' PlaySound asks for nothing while _root.Initialized is clear")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	SoundRig r(mount);
	r.shell->push("SaveLoad.apt");
	r.tick(150);
	REQUIRE(r.shell->top());
	AptSpriteInst *level = r.wm->apt().level(r.shell->top()->level());
	AptValue initialized;
	REQUIRE(level->getMember("Initialized", initialized));
	CHECK((initialized.isBoolean() && initialized.asBool()));
	std::vector<AptButtonInst *> all;
	SoundRig::buttons(level, all);
	AptButtonInst *del = nullptr;
	for (AptButtonInst *b : all)
	{
		del = !del && SoundRig::pathOf(*b) == "MainButtons/Delete/bttn" ? b : del;
	}
	REQUIRE(del);
	float x, y;
	REQUIRE(r.hitPoint(*del, x, y));
	// a press released away from the button (no click), so the screen stays as it is between the two presses
	auto pressOnly = [&]() {
		r.wm->apt().input().postMouseMove(5.0f, 5.0f);
		r.tick(20);
		r.take();
		r.wm->apt().input().postMouseMove(x, y);
		r.tick(20);
		r.take(); // the pointer on it (its own gate: the same PlaySound)
		r.wm->apt().input().postMouseButton(true);
		r.tick(2);
		const std::vector<std::string> sounds = r.take();
		r.wm->apt().input().postMouseMove(5.0f, 5.0f);
		r.tick(2);
		r.wm->apt().input().postMouseButton(false);
		r.tick(30);
		return sounds;
	};
	level->setMember("Initialized", AptValue::boolean(false));
	const std::vector<std::string> gated = pressOnly();
	CHECK_MESSAGE(gated.empty(), listed(gated));
	level->setMember("Initialized", AptValue::boolean(true));
	const std::vector<std::string> open = pressOnly();
	CHECK_MESSAGE(open == std::vector<std::string>{ kSelect }, listed(open));
}
