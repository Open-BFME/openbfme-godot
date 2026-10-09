// OpenBFME unit tests. GPL-3.0.
// The input helpers of the START-1 end-to-end tests: a real shell (WindowManager, Shell, gadget layer, the retail screens) over the mounted archives, with the
// lobby fed from the logic's PlayerTemplate store (WorldSkirmishSetupSource), driven by mouse / key events like the APT-4 acceptance tests (copied from
// test_apt_skirmish.cpp, whose helpers are file-local).
#pragma once

#include "doctest.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include "StartTestUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <map>
#include <memory>
#include <string>

namespace starttest
{
class MapGameText : public GameTextSource
{
public:
	std::map<std::string, std::u16string> labels;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = labels.find(label);
		if (it == labels.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
	void set(const std::string &label, const std::string &text) { labels[label] = asciiToU16(text); }
};

// the labels the lobby reads (test texts; the real strings are in the language archive)
inline void lobbyLabels(MapGameText &t)
{
	t.set("GUI:Open", "Open");
	t.set("GUI:Closed", "Closed");
	t.set("GUI:EasyAI", "Easy AI");
	t.set("GUI:MediumAI", "Medium AI");
	t.set("GUI:HardAI", "Hard AI");
	t.set("GUI:BrutalAI", "Brutal AI");
	t.set("GUI:Random", "Random");
	t.set("GUI:???", "???");
	t.set("GUI:Player", "Player");
	t.set("GUI:StartingMoneyFormat", "$%d");
	t.set("GUI:ErrorStartingGame", "Error starting game");
	t.set("GUI:CantFindMap", "Cannot find the map");
	t.set("GUI:TooManyPlayers", "Too many players: the map has %d");
	for (int i = 0; i <= 4; ++i)
	{
		t.set("Team:" + std::to_string(i), "Team " + std::to_string(i));
	}
}

struct ShellFx
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	MapGameText text;
	WorldSkirmishSetupSource setup;
	RecordingNewGameSink sink;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	LoadScreenInfo loadScreen;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;

	explicit ShellFx(Shared &s, std::uint32_t seed) : source(*s.mount->fs)
	{
		std::string error;
		REQUIRE_MESSAGE(loadGadgetSkinData(*s.mount->fs, skins, &error), error);
		REQUIRE_MESSAGE(setup.load(*s.world, s.settings, *s.mount->fs, &error), error);
		lobbyLabels(text);
		sink.seed = seed;
		environment.gameText = &text;
		environment.skirmish = &setup;
		environment.newGame = &sink;
		environment.loadScreen = &loadScreen;
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->registerComponents();
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->setShell(shell.get());
		wm->init();
	}
	~ShellFx()
	{
		shell.reset();
		layer.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
	AptCharacterInst *at(int level, const std::string &path) { return wm->apt().resolvePath(wm->apt().level(level), path); }
};

inline void clickAt(ShellFx &fx, float x, float y)
{
	fx.wm->postMouseMove(x, y);
	fx.tick(1);
	fx.wm->postMouseButton(true);
	fx.tick(1);
	fx.wm->postMouseButton(false);
	fx.tick(1);
}

// Clicks the centre of the first row of a list box that GadgetListBoxGetEntryBasedOnXY maps to `row`.

inline AptButtonInst *firstButtonIn(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptButtonInst *b = firstButtonIn(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

inline void clickButton(ShellFx &fx, AptButtonInst &b)
{
	float x0, y0, x1, y1;
	REQUIRE(b.contentBounds(x0, y0, x1, y1));
	float x, y;
	b.globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	// the button the pointer really hits at the centre: the target itself or a button of the same movie clip (a button clip may stack its own hit areas)
	AptButtonInst *hit = fx.wm->apt().input().hitTestButtons(x, y);
	const std::string target = b.targetPath();
	const std::string clip = target.substr(0, target.rfind('.'));
	REQUIRE_MESSAGE((hit && (hit == &b || hit->targetPath().compare(0, clip.size(), clip) == 0)), "the centre of " << target << " is covered by " << (hit ? hit->targetPath() : std::string("nothing")));
	clickAt(fx, x, y);
}

// The profile popup's pages are frames of ProfilePopup.Main (labels _delete, _change, _new): the list box of the change / delete pages exists only

inline void clickWindowCentre(ShellFx &fx, GameWindow *w)
{
	int x = 0, y = 0, a = 0, b = 0;
	w->winGetScreenPosition(&x, &y);
	w->winGetSize(&a, &b);
	clickAt(fx, x + a / 2.0f, y + b / 2.0f);
}

// Finds the screen y of a row's first pixel row by asking the list where its rows are (reading geometry is not an action); -1 when the row is not in view.
inline int rowScreenY(GameWindow *list, int row, int x)
{
	int sx = 0, sy = 0, w = 0, h = 0;
	list->winGetScreenPosition(&sx, &sy);
	list->winGetSize(&w, &h);
	(void)w;
	for (int y = sy + 1; y < sy + h - 1; ++y)
	{
		int r = -1, c = -1;
		GadgetListBoxGetEntryBasedOnXY(list, x, y, r, c);
		if (r == row)
		{
			return y + 1;
		}
	}
	return -1;
}

// Brings `row` into view with the mouse wheel over the list and clicks it.
inline void wheelAndClickRow(ShellFx &fx, GameWindow *list, int row)
{
	int sx = 0, sy = 0, w = 0, h = 0;
	list->winGetScreenPosition(&sx, &sy);
	list->winGetSize(&w, &h);
	const int x = sx + w / 3;
	for (int guard = 0; guard < 400; ++guard)
	{
		const int y = rowScreenY(list, row, x);
		if (y >= 0)
		{
			clickAt(fx, (float)x, (float)y);
			return;
		}
		fx.wm->postMouseMove((float)x, (float)(sy + h / 2));
		fx.tick(1);
		fx.wm->postMouseWheel(GadgetListBoxGetTopVisibleEntry(list) > row ? 1 : -1);
		fx.tick(1);
	}
	FAIL("the wheel did not bring row " << row << " into view");
}

// Opens a ZH combo box with its drop-down button and clicks a row.
inline void chooseComboRow(ShellFx &fx, GameWindow *combo, int row)
{
	GameWindow *drop = GadgetComboBoxGetDropDownButton(combo);
	REQUIRE(drop);
	clickWindowCentre(fx, drop);
	GameWindow *list = GadgetComboBoxGetListBox(combo);
	REQUIRE(list);
	REQUIRE_FALSE(list->winIsHidden());
	wheelAndClickRow(fx, list, row);
	CHECK(list->winIsHidden());
}

// The ImageComboBox opens on a click in its box.
inline void chooseImageComboRow(ShellFx &fx, GameWindow *box, int row)
{
	int x = 0, y = 0, w = 0, h = 0;
	box->winGetScreenPosition(&x, &y);
	box->winGetSize(&w, &h);
	clickAt(fx, x + w / 4.0f, y + h / 2.0f);
	GameWindow *list = GadgetImageComboBoxGetListBox(box);
	REQUIRE(list);
	REQUIRE_FALSE(list->winIsHidden());
	wheelAndClickRow(fx, list, row);
	CHECK(list->winIsHidden());
}

inline void typeText(ShellFx &fx, GameWindow *entry, const std::u16string &text)
{
	clickWindowCentre(fx, entry);
	for (char16_t c : text)
	{
		fx.wm->postTextInput(c);
		fx.tick(1);
	}
	fx.tick(3);
}

inline AptButtonInst *popupButton(ShellFx &fx, const char *path)
{
	AptButtonInst *b = firstButtonIn(fx.wm->apt().resolvePath(fx.wm->apt().level(fx.shell->top()->level()), path));
	return b && b->globallyVisible() ? b : nullptr;
}

// Opens `nav` of the main menu and clicks its `item` (the click-through of the A3 menu test, test_apt_shell.cpp)
inline void clickNavItem(ShellFx &fx, int level, const std::string &nav, const std::string &item)
{
	AptCharacterInst *navClip = fx.at(level, nav);
	REQUIRE_MESSAGE(navClip, nav);
	AptButtonInst *open = nullptr;
	for (AptCharacterInst *k : navClip->asSprite()->children())
	{
		if (k->asButton())
		{
			open = k->asButton();
			break;
		}
	}
	REQUIRE_MESSAGE(open, nav);
	clickButton(fx, *open);
	AptButtonInst *itemButton = nullptr;
	for (int waited = 0; waited < 120 && !itemButton; ++waited)
	{
		itemButton = firstButtonIn(fx.at(level, nav + "." + item));
		if (!itemButton)
		{
			fx.tick(1);
		}
	}
	REQUIRE_MESSAGE(itemButton, nav << "." << item);
	fx.tick(20); // the reveal animation of the entry
	clickButton(fx, *itemButton);
	fx.tick(2);
}

} // namespace starttest
