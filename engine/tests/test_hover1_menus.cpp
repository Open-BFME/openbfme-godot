// OpenBFME tests, lane HOVER-1: the menus' tool tips (CursorTooltip.h): the hover path of the hovered button (RW 0x624C68), AptButtonTooltipMap.ini's names
// and the string file test (RW 0x6235E2), Mouse::drawTooltip's delay and hide (RW 0x5EE7A8) and GuiFX.apt's ShowToolTip / MoveToolTip / HideToolTip /
// ToolTipText (RW 0x782DD9 / 0x782CDC / 0x782CA0 / 0x782D52). GPL-3.0. The retail cases hover every visible button of the main menu (each navigation
// opened) and of Options with the Apt input's pointer; they SKIP loudly without the installs.

#include "doctest.h"

#include "AptRetail.h"

#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptHoverInventory.h"
#include "GameClient/GUI/CursorTooltip.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
struct MapText : GameTextSource
{
	std::map<std::string, std::u16string> t;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = t.find(label);
		if (it == t.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
};
} // namespace

TEST_CASE("hover1 cursor tooltip: the delay, ShowToolTip's size and MoveToolTip's position, the 4-update hide and the centred ToolTipText (RW 0x5EE7A8 / 0x782DD9)")
{
	CursorTooltipSettings s;
	std::string error;
	REQUIRE_MESSAGE(CursorTooltipSettings::scan("Mouse\n  TooltipFontName = Albertus MT\n  TooltipFontSize = 10\n  TooltipFontIsBold = No\n"
	                                            "  TooltipDelayTime = 800\n  TooltipTextColor = R:220 G:220 B:220 A:255\nEnd\n",
	                    "", s, &error),
		error);
	CHECK(s.font.name == "Albertus"); // parseAsciiString: the first token
	CHECK(s.font.pointSize == 10);
	CHECK(s.delayMs == 800);
	CHECK(s.textColor == 0xFFDCDCDCu);
	DefaultFontMetrics metrics; // height = size + 4, width = chars * (size * 6 / 10 + 1)
	CursorTooltip tip;
	std::vector<CursorTooltip::Call> calls;
	tip.setTooltip(u"Hello");
	tip.update(1000, 100, 200, s, 1.0f, 1.0f, metrics, calls);
	CHECK(calls.empty()); // the delay starts
	tip.update(1799, 100, 200, s, 1.0f, 1.0f, metrics, calls);
	CHECK(calls.empty());
	tip.update(1800, 100, 200, s, 1.0f, 1.0f, metrics, calls);
	REQUIRE(calls.size() == 2);
	// 5 chars * 7 = 35 wide, 14 high, pad (14 + 1) / 3 = 5: ShowToolTip((35 + 5) * 0.5, 14 + 5), MoveToolTip at the pointer
	CHECK(calls[0].function == "ShowToolTip");
	CHECK(calls[0].args == std::vector<std::string>{ "20", "19" });
	CHECK(calls[1].function == "MoveToolTip");
	CHECK(calls[1].args == std::vector<std::string>{ "100", "200" });
	CHECK(tip.shown() == u"Hello");
	GadgetDrawList list;
	tip.render(0.0f, 0.0f, 100.0f, 40.0f, 1.0f, 1.0f, list);
	REQUIRE(list.commands.size() == 1);
	CHECK(list.commands[0].text == u"Hello");
	CHECK(list.commands[0].x0 == (int)(50.0f - 17.5f + 0.5f));
	CHECK(list.commands[0].y0 == (int)(20.0f - 7.0f + 0.5f));
	// the request goes: three updates keep it, the fourth hides it
	calls.clear();
	tip.setTooltip(u"");
	for (int i = 0; i < 3; ++i)
	{
		tip.update(1900 + (std::uint32_t)i, 100, 200, s, 1.0f, 1.0f, metrics, calls);
	}
	CHECK(calls.empty());
	tip.update(1910, 100, 200, s, 1.0f, 1.0f, metrics, calls);
	REQUIRE(calls.size() == 1);
	CHECK(calls[0].function == "HideToolTip");
	CHECK(tip.shown().empty());
	// a 2560 x 1440 window (scale 2.5 x 1.875): the string measured at the stage's size times min(scale) = 1.875: 35 -> 65 px wide, one line of
	// 14 * 1.875 = 26 px, pad (26 + 1) / 3 = 9; ShowToolTip((65 + 9) / 2.5 * 0.5, (26 + 9) / 1.875), MoveToolTip at the stage pointer
	{
		CursorTooltip wide;
		std::vector<CursorTooltip::Call> c2;
		wide.setTooltip(u"Hello");
		wide.update(0, 300.0f, 400.0f, s, 2.5f, 1.875f, metrics, c2);
		wide.update(800, 300.0f, 400.0f, s, 2.5f, 1.875f, metrics, c2);
		REQUIRE(c2.size() == 2);
		CHECK(c2[0].args == std::vector<std::string>{ "14.8", "18.6667" });
		CHECK(c2[1].args == std::vector<std::string>{ "300", "400" });
		// GuiFX's ToolTip (the box's layout program, its side clips eased to +- targetSizeX, the background box cornerTop._x + 4 wide): the box is
		// 2 * 14.8 + 4 = 33.6 stage units, the text the device draws 65 px / 2.5 = 26 stage units: the box exceeds it by the pad (9 / 2.5) and 4
		GadgetDrawList drawn;
		wide.render(0.0f, 0.0f, 33.6f, 20.0f, 2.5f, 1.875f, drawn);
		REQUIRE(drawn.commands.size() == 1);
		CHECK(drawn.commands[0].x1 - drawn.commands[0].x0 == 26);
		CHECK(drawn.commands[0].font.pointSize == 10); // drawn at the stage's size; the device scales it by min(scale), the size it was measured at
	}
	// the button map: the main menu's buttons are named through AptButtonTooltipMap.ini
	AptButtonTooltipMap map;
	REQUIRE(AptButtonTooltipMap::scan("AptButtonTooltipMap\n\tButtonMap = MainMenu/SoloPlayNav/Skirmish/bttn OpeningMenu/SkirmishButton\n"
	                                  "\t;ButtonMap = MainMenu/A B\nEND\n",
		map, &error));
	CHECK(map.buttonMap.size() == 1);
	CHECK(map.buttonMap["MainMenu/SoloPlayNav/Skirmish/bttn"] == "OpeningMenu/SkirmishButton");
	CHECK_FALSE(AptButtonTooltipMap::scan("AptButtonTooltipMap\n\tButtonMap = OnlyOne\nEND\n", map, &error));
}

namespace
{
struct MenuRig
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GameTextTableSource text;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<Shell> shell;
	std::unique_ptr<AptGuiFXScreen> guiFX;
	CursorTooltipSettings settings;
	AptButtonTooltipMap names;
	std::unique_ptr<ShellCursorTooltip> tip;
	DefaultFontMetrics metrics;
	std::uint32_t nowMs = 1000;
	explicit MenuRig(AptRetail &mount) : source(mount.fs)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(text.table.loadRetail(mount.fs, &error), error); // data/lotr.str, or a non-English install's lotr.csf
		REQUIRE_MESSAGE(CursorTooltipSettings::load(mount.fs, settings, &error), error);
		REQUIRE_MESSAGE(AptButtonTooltipMap::load(mount.fs, names, &error), error);
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		wm->setTooltipNameMap(names.buttonMap);
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->init();
		guiFX = std::make_unique<AptGuiFXScreen>(*wm, *shell); // the window manager's GuiFX.apt (BFME1 AptGuiFXRegisterCallbacks)
		tip = std::make_unique<ShellCursorTooltip>(*wm, &text, settings);
		tick(10);
		REQUIRE(wm->findAptMovieIndex("GuiFX.apt") == 11); // the window manager's GuiFX level
	}
	~MenuRig()
	{
		tip.reset();
		guiFX.reset();
		shell.reset();
		wm.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
			nowMs += 33;
			tip->update(nowMs, 1024.0f, 768.0f, metrics);
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
	// the hover path as retail keys it: "<movie without its extension>/<names below the level>", the button's own name last
	static std::string pathOf(const AptButtonInst &b, const std::string &movie)
	{
		std::vector<const AptCharacterInst *> chain;
		for (const AptCharacterInst *c = &b; c && c->parent(); c = c->parent())
		{
			chain.push_back(c);
		}
		std::string out = movie;
		for (size_t i = chain.size(); i-- > 0;)
		{
			out += "/" + chain[i]->instName();
		}
		return out;
	}
	bool centreOf(AptButtonInst &b, float &x, float &y)
	{
		float x0, y0, x1, y1;
		if (!b.contentBounds(x0, y0, x1, y1))
		{
			return false;
		}
		// the centre, else the first point of a 9 x 9 grid over the bounds that hits the button's mesh
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
};

struct MenuTally
{
	std::vector<std::string> shown, withoutLabel, notShown;
	std::set<std::string> shownPaths; ///< lower-cased hover paths whose tool tip showed
};

// the front end's labelled hover areas the runtime test shows (lower-cased static paths, AptHoverInventory's), and the labelled ones it cannot reach, each
// with the evidence why (the placement frames are the inventory's "placed at frames"); together they are the inventory's 30 (see the inventory test)
const std::set<std::string> kShownStatic = { "mainmenu/multiplaynav/localnetwork/bttn", "mainmenu/multiplaynav/online/bttn", "mainmenu/multiplaynav/replay/bttn",
	"mainmenu/optionsnav/advancedsettings/bttn", "mainmenu/optionsnav/credits/bttn", "mainmenu/optionsnav/settings/bttn",
	"mainmenu/soloplaynav/bonuscampaign/bttndisabled", "mainmenu/soloplaynav/expansion1campaign/bttn", "mainmenu/soloplaynav/loadgame/bttn",
	"mainmenu/soloplaynav/skirmish/bttn", "mainmenu/soloplaynav/warofthering/bttn", "options/tooltipseax3", "options/tooltipshighaudioquality",
	"quitmenu/exitmission/thebutton", "quitmenu/load/thebutton", "quitmenu/options/thebutton", "quitmenu/restart/thebutton", "quitmenu/resume/thebutton",
	"quitmenu/save/thebutton" };
const std::map<std::string, std::string> kNotReached = {
	{ "mainmenu/myheroes/bttn", "MenuExport's GenericButton_Med places bttn at its frame 9 (_up); the running main menu leaves the MyHeroes clip on frame 0 (_reveal) of 96 "
	                            "(the menu's reveal of the clip is not driven: S-4610)" },
	{ "mainmenu/quitmainmenu/bttn", "the same GenericButton_Med clip: bttn at frame 9 (_up), the QuitMainMenu clip stays on frame 0 of 96 (S-4610)" },
	{ "mainmenu/soloplaynav/bonuscampaign/bttn", "a fresh profile has not unlocked the bonus campaign: the entry shows bttnDisabled (its frame 29), whose tool tip shows" },
	{ "onlineshell/toggles/buttonnav/" "home/bttn", "ButtonNav is placed at Toggles' frame 16, after signing in; the login page stops Toggles at frame 8 of 49" },
	{ "onlineshell/toggles/buttonnav/openplay/bttn", "ButtonNav: after signing in (Toggles frame 16; the login page holds frame 8)" },
	{ "onlineshell/toggles/buttonnav/profile/bttn", "ButtonNav: after signing in (Toggles frame 16; the login page holds frame 8)" },
	{ "onlineshell/toggles/buttonnav/quickmatch/bttn", "ButtonNav: after signing in (Toggles frame 16; the login page holds frame 8)" },
	{ "onlineshell/toggles/buttonnav/strategic/bttn", "ButtonNav: after signing in (Toggles frame 16; the login page holds frame 8)" },
	{ "options/main/frameleft/advanced/bttn", "placed at Options.Main's frame 0 (FrameLeft); the screen opens on Main's frame 18 of 30, which has no FrameLeft" },
	{ "options/main/frameright/refreshnat/bttn", "placed at Options.Main's frame 0 (FrameRight); Main opens on frame 18 of 30, without FrameRight" },
	{ "skirmish/profilenav/bttn", "the ProfileNav clip stays on frame 0 of 51 and no point of its bttn's hit mesh is the topmost button (the lobby's own buttons cover it)" } };

// hovers every visible button of `level` and checks the label and the text GuiFX shows
void hoverAll(MenuRig &r, int level, const std::string &movie, MenuTally &tally, std::set<std::string> &done)
{
	std::vector<AptButtonInst *> list;
	MenuRig::buttons(r.wm->apt().level(level), list);
	for (AptButtonInst *b : list)
	{
		float x, y;
		if (!r.centreOf(*b, x, y))
		{
			continue; // hidden or covered
		}
		const std::string path = MenuRig::pathOf(*b, movie);
		if (!done.insert(path).second)
		{
			continue;
		}
		INFO(path);
		// away, so the last tip hides
		r.wm->apt().input().postMouseMove(1.0f, 1.0f);
		r.tick(6);
		r.wm->apt().input().postMouseMove(x, y);
		r.tick(1);
		if (path.back() == '/')
		{
			CHECK(r.wm->currentTooltipLabel().empty()); // an unnamed button has no tool tip (RW 0x624E64)
			continue;
		}
		auto alias = r.names.buttonMap.find(path);
		const std::string label = "TOOLTIP:" + (alias != r.names.buttonMap.end() ? alias->second : path);
		const std::string callback = r.wm->hasTooltip(path) ? path : std::string();
		if (callback.empty())
		{
			CHECK(r.wm->currentTooltipLabel() == label);
		}
		std::u16string text;
		const bool exists = r.text.fetch(callback.empty() ? label : r.wm->currentTooltipLabel(), text);
		// the delay (TooltipDelayTime) then ShowToolTip
		for (int i = 0; i < r.settings.delayMs / 33 + 4 && r.tip->tooltip().shown().empty(); ++i)
		{
			r.tick(1);
		}
		if (!exists)
		{
			CHECK(r.tip->tooltip().shown().empty()); // RW 0x6235E2: only a label the string file has
			tally.withoutLabel.push_back(path);
			continue;
		}
		if (r.tip->tooltip().shown() != text)
		{
			tally.notShown.push_back(path + " (" + label + ")");
			continue;
		}
		GadgetDrawList drawn;
		const WindowManager::RenderCallback *render = r.wm->renderCallback("ToolTipText");
		REQUIRE(render);
		(*render)(0.0f, 0.0f, 200.0f, 50.0f, drawn);
		std::u16string joined;
		for (const GadgetDrawCommand &c : drawn.commands)
		{
			joined += (joined.empty() ? u"" : u"\n") + c.text;
		}
		CHECK(loadScreenU16ToUtf8(joined) == loadScreenU16ToUtf8(text));
		tally.shown.push_back(label);
		std::string lowered = path;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		tally.shownPaths.insert(lowered);
	}
}
} // namespace

TEST_CASE("hover1 retail: the shell's window manager reports stop S-4610 exactly once the tool tips run")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuRig r(mount);
	const std::string expected =
		"[S-4610] menu tool tips (HOVER-1): ported RW 0x624C68's path (the hovered button's own path), RW 0x6235E2 (the AptButtonTooltipMap name, a label only "
		"when the string file has it), Mouse::setCursorTooltip / drawTooltip (RW 0x5EE1A6 / 0x5EE7A8: TooltipDelayTime, the 4-update hide) and GuiFX's "
		"ShowToolTip / MoveToolTip / HideToolTip / ToolTipText (RW 0x782DD9 / 0x782CDC / 0x782CA0 / 0x782D52), in the front end and in the quit menu; not "
		"shown: My Heroes' and Quit's buttons (the main menu leaves their clips on frame 0), ScriptEngine + 0x1A238's gate is not read; not fetched, named by "
		"the inventory of every tool tip source (tools/hover/hover_inventory.py, sources.csv): 18 lobby, staging and map list tips (TOOLTIP:AdvSetting, "
		"BuddyPlayer, ChatPlayerInfo, GameStarted, GenericPlayer, LadderRankUnavailable, LocalPlayer, MapEasySuccess, MapHardMaxSuccess, MapHardSuccess, "
		"MapMediumSuccess, MapNoSuccess, MpSaveGame, PasswordIcon, ProfiledPlayer, StagingPlayerInfo, UserMapIcon, Skirmish/tooltipPlayerLevelIcon), 4 "
		"Create-a-Hero builder tips (TOOLTIP:CAH_CURRENT_POWER, CAH_IN_PALANTIR, CAH_NO_CURRENT_POWER, CAH_PALANTIR_EMPTY_SLOT), the quit menu's "
		"TOOLTIP:QuitMenu/Surrender/WOTRSurrender, the TOOLTIPTEXT of the native gadgets (the experience screen's two, the load screen's progress bar) and 10 "
		"Mouse.ini presentation fields; INFERENCE: the string's pixel size is the font metrics' stage measure times min(scale), ToolTipText places it in stage "
		"units";
	size_t n = 0;
	for (const WindowManagerNote &note : r.wm->notes())
	{
		n += note.kind == "stop" && note.detail == expected ? 1 : 0;
	}
	CHECK(n == 1);
}

TEST_CASE("hover1 retail: every visible button of the main menu (each navigation open) and of Options shows its TOOLTIP text through GuiFX.apt")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuTally tally;
	std::set<std::string> done;
	// the menu as it opens, then each navigation opened in a fresh menu (an open navigation covers its neighbours' buttons)
	for (const char *nav : { "", "SoloPlayNav", "MultiPlayNav", "OptionsNav" })
	{
		INFO(std::string(nav));
		MenuRig r(mount);
		r.shell->push("MainMenu.apt");
		r.tick(150); // the menu's opening animation
		const int menu = r.shell->top()->level();
		if (*nav)
		{
			AptCharacterInst *navClip = r.wm->apt().resolvePath(r.wm->apt().level(menu), nav);
			REQUIRE(navClip);
			AptButtonInst *open = nullptr;
			for (AptCharacterInst *k : navClip->asSprite()->children())
			{
				open = open ? open : k->asButton();
			}
			REQUIRE(open);
			float x = 0, y = 0;
			REQUIRE(r.centreOf(*open, x, y));
			// the navigation's button opens it; its entries reveal over a few updates (a press the menu does not take yet is repeated: the first press
			// after the opening animation only focuses the movie)
			auto entries = [&]() {
				std::vector<AptButtonInst *> all;
				MenuRig::buttons(navClip, all);
				int visible = 0;
				for (AptButtonInst *b : all)
				{
					float bx, by;
					visible += b != open && r.centreOf(*b, bx, by) ? 1 : 0;
				}
				return visible;
			};
			for (int attempt = 0; attempt < 3 && entries() == 0; ++attempt)
			{
				r.wm->apt().input().postMouseMove(x, y);
				r.tick(1);
				r.wm->apt().input().postMouseButton(true);
				r.tick(1);
				r.wm->apt().input().postMouseButton(false);
				r.tick(120); // the entries' reveal animation
			}
			CHECK(entries() > 0); // tolerance: the navigation opened (its entries are hovered by name below)
		}
		hoverAll(r, menu, "MainMenu", tally, done);
		CHECK(r.tip->errors().empty());
	}
	const size_t mainShown = tally.shown.size();
	std::string list;
	for (const std::string &l : tally.shown)
	{
		list += l + ", ";
	}
	MESSAGE("main menu: " << done.size() << " buttons hovered, " << mainShown << " tool tips shown: " << list << tally.withoutLabel.size() << " without a label");
	// Options, then the other front-end movies with TOOLTIP labels (the inventory's: Skirmish, OnlineShell, QuitMenu)
	for (const char *movie : { "Options", "Skirmish", "OnlineShell", "QuitMenu" })
	{
		INFO(std::string(movie));
		MenuRig r(mount);
		r.shell->push(std::string(movie) + ".apt");
		r.tick(150);
		REQUIRE(r.shell->top());
		const size_t before = tally.shown.size();
		hoverAll(r, r.shell->top()->level(), movie, tally, done);
		std::string got;
		for (size_t i = before; i < tally.shown.size(); ++i)
		{
			got += tally.shown[i] + ", ";
		}
		MESSAGE(std::string(movie) << ": " << got);
		CHECK(r.tip->errors().empty());
	}
	list.clear();
	for (size_t i = mainShown; i < tally.shown.size(); ++i)
	{
		list += tally.shown[i] + ", ";
	}
	MESSAGE("options: " << (tally.shown.size() - mainShown) << " tool tips shown: " << list);
	for (const std::string &n : tally.notShown)
	{
		FAIL_CHECK("the string file has the tool tip but it never showed: " << n);
	}
	// the exact set: the 19 labelled static areas (kShownStatic), the added Good Campaign row's (lane BFME2CAMP1: RotWK's tool tip map still names
	// MainMenu/SoloPlayNav/GoodCampaign/bttn; its EvilCampaign line is commented out) and the 7 of OnlineShell's login page, which ActionScript attaches
	const std::set<std::string> expected = { "TOOLTIP:OpeningMenu/GoodCampaignButton", "TOOLTIP:OpeningMenu/LoadGameButton", "TOOLTIP:OpeningMenu/BonusButtonDisabled",
		"TOOLTIP:OpeningMenu/Expansion1Button", "TOOLTIP:OpeningMenu/WaroftheRingButton", "TOOLTIP:OpeningMenu/SkirmishButton",
		"TOOLTIP:OpeningMenu/OnlineButton", "TOOLTIP:OpeningMenu/NetworkButton", "TOOLTIP:OpeningMenu/ReplaysButton", "TOOLTIP:OpeningMenu/SettingsButton",
		"TOOLTIP:OpeningMenu/CustomSettingsButton", "TOOLTIP:OpeningMenu/CreditsButton", "TOOLTIP:Settings/EAX", "TOOLTIP:Settings/HighQualityAudio",
		"TOOLTIP:QuitMenu/Options/TheButton", "TOOLTIP:QuitMenu/Save/TheButton", "TOOLTIP:QuitMenu/Load/TheButton", "TOOLTIP:QuitMenu/Restart/TheButton",
		"TOOLTIP:QuitMenu/ExitMission/TheButton", "TOOLTIP:QuitMenu/Resume/TheButton", "TOOLTIP:OnlineShell/OnlineLogin/Login/OfficialSite/buttonClip",
		"TOOLTIP:OnlineShell/OnlineLogin/Login/ServiceTerms/buttonClip", "TOOLTIP:OnlineShell/OnlineLogin/Login/DeleteNickname/buttonClip",
		"TOOLTIP:OnlineShell/OnlineLogin/Login/Register/buttonClip", "TOOLTIP:OnlineShell/OnlineLogin/tooltipOnlineEAAccountName",
		"TOOLTIP:OnlineShell/OnlineLogin/tooltipRememberMe", "TOOLTIP:OnlineShell/OnlineLogin/GamespyShortcut" };
	CHECK((std::set<std::string>(tally.shown.begin(), tally.shown.end()) == expected));
	std::set<std::string> shownStatic;
	for (const std::string &p : tally.shownPaths)
	{
		// lane BFME2CAMP1: the added Good Campaign row is made at run time (duplicateMovieClip), not a static area of the movie
		if (p.rfind("onlineshell/onlinelogin", 0) != 0 && p != "mainmenu/soloplaynav/goodcampaign/bttn")
		{
			shownStatic.insert(p);
		}
	}
	CHECK((shownStatic == kShownStatic));
	// none of the unreached areas is on the screen of these states (their reasons hold)
	for (const auto &e : kNotReached)
	{
		CHECK_MESSAGE(tally.shownPaths.count(e.first) == 0, e.first);
	}
	CHECK(tally.shown.size() == expected.size());
}

namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}
// the TOOLTIP labels of the string file whose name is a hover path ("TOOLTIP:<movie>/<path>"), as written
std::vector<std::string> pathLabels(const std::vector<std::uint8_t> &str)
{
	std::vector<std::string> out;
	std::string line;
	for (size_t i = 0; i <= str.size(); ++i)
	{
		const char c = i < str.size() ? (char)str[i] : '\n';
		if (c != '\n')
		{
			line += c;
			continue;
		}
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
		{
			line.pop_back();
		}
		if (line.size() > 8 && lower(line.substr(0, 8)) == "tooltip:" && line.find('/') != std::string::npos && line.find(' ') == std::string::npos)
		{
			out.push_back(line);
		}
		line.clear();
	}
	return out;
}
} // namespace

TEST_CASE("hover1 inventory: every Apt movie's hover areas (buttons of every timeline, through imports and exports) against the string file's TOOLTIP labels")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	if (retailtest::skipUnlessEnglish("hover1 inventory (the counts of English lotr.str's TOOLTIP labels)"))
	{
		return;
	}
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::vector<std::uint8_t> str;
	std::string error;
	REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", str, &error), error);
	GameTextTableSource text;
	REQUIRE(text.table.parse(str, &error));
	AptButtonTooltipMap names;
	REQUIRE_MESSAGE(AptButtonTooltipMap::load(mount.fs, names, &error), error);
	std::vector<AptHoverArea> all;
	std::vector<std::string> movies = source.listMovies();
	std::sort(movies.begin(), movies.end());
	std::vector<std::string> broken;
	std::vector<std::pair<std::string, size_t>> movieAreas; // every movie walked (also one with no area): the tool refuses an areas file without them
	for (const std::string &m : movies)
	{
		std::vector<AptHoverArea> areas;
		if (!AptHoverInventory::walk(loader, m, areas, &error))
		{
			broken.push_back(m + ": " + error);
			continue;
		}
		all.insert(all.end(), areas.begin(), areas.end());
		movieAreas.push_back({ m, areas.size() });
	}
	// every area's label (an unnamed button has none) and whether the string file has it. The paths compare without case: the window manager names a
	// movie by the file name the screen loads ("MainMenu.apt"), the archive lists it lower-cased; the string file's labels compare without case
	std::map<std::string, std::string> aliases;
	for (const auto &e : names.buttonMap)
	{
		aliases[lower(e.first)] = e.second;
	}
	size_t named = 0, labelled = 0;
	std::set<std::string> staticPaths, leaves, exportLeaves;
	const char *out = std::getenv("OPENBFME_HOVER_INVENTORY_OUT");
	std::ofstream csv;
	if (out)
	{
		csv.open(std::string(out) + "/apt_hover_areas.csv");
		csv << "movie,path,from export,placed at frames,label,in string file\n";
	}
	if (out)
	{
		std::ofstream movieCsv(std::string(out) + "/apt_movies.csv");
		movieCsv << "movie,areas\n";
		for (const auto &e : movieAreas)
		{
			movieCsv << e.first << "," << e.second << "\n";
		}
	}
	for (const AptHoverArea &a : all)
	{
		(a.fromExport ? exportLeaves : leaves).insert(lower(AptHoverInventory::leafName(a.path)));
		if (!a.fromExport)
		{
			staticPaths.insert(lower(a.path));
		}
		std::string label;
		bool has = false;
		if (!a.fromExport && a.path.back() != '/')
		{
			++named;
			auto alias = aliases.find(lower(a.path));
			label = "TOOLTIP:" + (alias != aliases.end() ? alias->second : a.path);
			std::u16string t;
			has = text.fetch(label, t);
			labelled += has ? 1 : 0;
		}
		if (csv)
		{
			csv << a.movie << "," << a.path << "," << (a.fromExport ? "yes" : "no") << "," << a.frames << "," << label << "," << (label.empty() ? "" : has ? "yes" : "no") << "\n";
		}
	}
	// every TOOLTIP:<movie>/<path> label: a static hover area (directly or through a ButtonMap name), an export's button (attached at run time), or no
	// button of any movie has its last name
	std::map<std::string, std::string> aliasTargets; // tooltip name -> the path that uses it
	for (const auto &e : names.buttonMap)
	{
		aliasTargets[lower(e.second)] = lower(e.first);
	}
	std::vector<std::string> reachable, exported, none;
	for (const std::string &l : pathLabels(str))
	{
		const std::string path = lower(l.substr(8));
		auto viaAlias = aliasTargets.find(path);
		if (staticPaths.count(path) || (viaAlias != aliasTargets.end() && staticPaths.count(viaAlias->second)))
		{
			reachable.push_back(l);
		}
		else if (exportLeaves.count(AptHoverInventory::leafName(path)) || leaves.count(AptHoverInventory::leafName(path)))
		{
			exported.push_back(l);
		}
		else
		{
			none.push_back(l);
		}
	}
	// the labelled static areas: exactly the ones the runtime test shows plus the ones it cannot reach, each with its reason
	std::set<std::string> labelledStatic;
	for (const AptHoverArea &a : all)
	{
		if (!a.fromExport && a.path.back() != '/')
		{
			auto alias = aliases.find(lower(a.path));
			std::u16string t;
			if (text.fetch("TOOLTIP:" + (alias != aliases.end() ? alias->second : a.path), t))
			{
				labelledStatic.insert(lower(a.path));
			}
		}
	}
	std::set<std::string> partition = kShownStatic;
	for (const auto &e : kNotReached)
	{
		CHECK_MESSAGE(partition.insert(e.first).second, e.first); // disjoint
	}
	CHECK((labelledStatic == partition));
	MESSAGE("movies " << movies.size() << ", broken " << broken.size() << ", areas " << all.size() << ", named static " << named << ", labelled " << labelled
	                  << "; TOOLTIP path labels: reachable " << reachable.size() << ", by a name only " << exported.size() << ", no button " << none.size());
	// pinned (RotWK 2.01's 86 movies and lotr.str, measured 2026-10-14)
	CHECK(movies.size() == 86);
	CHECK(broken.empty());
	CHECK(all.size() == 1648);
	CHECK(named == 560);
	CHECK(labelled == 30);
	CHECK(reachable.size() == 29);
	CHECK(exported.size() == 34); // a button of that last name exists somewhere (an export ActionScript attaches: OnlineShell's login page shows 7 of them)
	CHECK(none.size() == 133);
	// Options' labels with no hover area: no button of any movie or export has the name (Options.apt's own areas are tooltipsEAX3,
	// tooltipsHighAudioQuality and tooltipsSendDelay, and the last has no label): retail can never show these texts
	const std::set<std::string> optionsNone = { "TOOLTIP:Options/tooltipShowUnitDecals", "TOOLTIP:Options/tooltipEnableHealthBars", "TOOLTIP:Options/tooltipResolution", "TOOLTIP:Options/tooltipDetail", "TOOLTIP:Options/tooltipMusic", "TOOLTIP:Options/tooltipBrightness", "TOOLTIP:Options/tooltipSoundFX", "TOOLTIP:Options/tooltipVoice", "TOOLTIP:Options/tooltipAmbient", "TOOLTIP:Options/tooltipMovie", "TOOLTIP:Options/tooltipScrollSpeed", "TOOLTIP:Options/tooltipDoubleClickGuard", "TOOLTIP:Options/tooltipAlternateMouse", "TOOLTIP:Options/tooltipAlternativeMouseSetUp", "TOOLTIP:options/tooltipEAX", "TOOLTIP:Options/tooltipHighQualityAudio", "TOOLTIP:Options/tooltipHighAudioQuality", "TOOLTIP:Options/tooltipOnlineIP", "TOOLTIP:Options/tooltipFireWall", "TOOLTIP:Options/tooltipSendDelay", "TOOLTIP:Options/clipOnlineOptions/RefreshNAT/buttonRefreshNAT", "TOOLTIP:Options/tooltipDisplayForeignLanguage", "TOOLTIP:Options/tooltipTurnOffMessengerInGame", "TOOLTIP:Options/tooltipFilterLanguage", "TOOLTIP:Options/tooltipAnisotropicTextureFiltering", "TOOLTIP:Options/tooltipTerrainLighting", "TOOLTIP:Options/tooltip3DShadows", "TOOLTIP:Options/tooltip2DShadows", "TOOLTIP:Options/tooltipSmoothWaterBorder", "TOOLTIP:Options/tooltipShowProps", "TOOLTIP:Options/tooltipShowAnimations", "TOOLTIP:Options/tooltipHeatEffects", "TOOLTIP:Options/tooltipDisableDynamicLOD", "TOOLTIP:Options/tooltipTextureDetail", "TOOLTIP:Options/tooltipParticleCap", "TOOLTIP:Options/tooltipRetaliation" };
	std::set<std::string> gotOptionsNone;
	for (const std::string &l : none)
	{
		if (lower(l).rfind("tooltip:options/", 0) == 0)
		{
			gotOptionsNone.insert(l);
		}
	}
	CHECK((gotOptionsNone == optionsNone));
	CHECK(optionsNone.size() == 36);
	for (const std::string &b : broken)
	{
		MESSAGE("BROKEN " << b);
	}
}
