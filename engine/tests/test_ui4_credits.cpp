// OpenBFME unit tests. GPL-3.0.
// Lane UI-4 (the owner's findings on v0.3.0-preview.1, 2026-10-10): the main menu's Options entries. Credits rolls RotWK's credits (AptMainMenu::Credits
// RW 0x91B5E9, CreditsManager RW 0x9C6A7F .. 0x9C7113, the RenderCredits callback RW 0x91B1B8 -> RW 0x9C6765), its fonts come from language.ini
// (GlobalLanguage RW 0x5E9D3C, table RW 0xBF4C20), and Custom Settings opens the Options screen on its advanced page (AdvancedOnly). Expected values are
// the retail files' own data and the binary's arithmetic (GameClient/Credits.h); the retail tests SKIP loudly without the installs.

#include "doctest.h"
#include "AptRetail.h"

#include "Common/INI.h"
#include "GameClient/Credits.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "AptPlayerTestUtil.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptFile.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>
#include <cmath>

#include <memory>
#include <string>

namespace
{
GlobalLanguage loadLanguage(Win32BIGFileSystem &fs)
{
	GlobalLanguage language;
	INIEnvironment env;
	env.fileSystem = &fs;
	language.registerBlocks(env.blocks);
	INI ini(env);
	ini.load(GlobalLanguage::fileName(), INI_LOAD_OVERWRITE);
	return language;
}

// widths and heights of a fixed font geometry, so the layout arithmetic is checkable
class FixedMetrics : public FontMetricsSource
{
public:
	int fontHeight(const GameFont &font) override { return font.pointSize + 4; }
	int textWidth(const GameFont &font, const UnicodeString &text) override { return (int)text.size() * font.pointSize / 2; }
	int wrappedHeight(const GameFont &font, const UnicodeString &, int) override { return fontHeight(font); }
};

std::u16string widen(const std::string &s)
{
	std::u16string out;
	for (char c : s)
	{
		out.push_back((char16_t)(unsigned char)c);
	}
	return out;
}

struct MenuShell
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	GameTextTableSource text;
	GlobalLanguage language;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	FixedMetrics metrics;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;

	explicit MenuShell(AptRetail &mount) : source(mount.fs)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
		language = loadLanguage(mount.fs);
		environment.gameText = &text;
		environment.fileSystem = &mount.fs;
		environment.language = &language;
		environment.displayModes = { { 1024, 768 } };
		environment.currentResolution = { 1024, 768 };
		registerAptScreenFactories(factories);
		REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->gadgets().setFontMetrics(&metrics);
		layer->registerComponents();
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->setShell(shell.get());
		wm->init();
		tick(2);
		shell->push("MainMenu.apt");
		tick(30);
		// the host reveals the menu once its first frames ran (game.gd: ShowMainMenu, S-139)
		std::string reveal;
		AptMainMenu *m = menu();
		REQUIRE(m);
		REQUIRE_MESSAGE(wm->invokeAS(m->level(), "ShowMainMenu", {}, nullptr, &reveal), reveal);
		tick(90);
	}
	~MenuShell()
	{
		shell.reset();
		layer.reset();
	}
	void tick(int n, int ms = 33)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(ms);
		}
	}
	AptMainMenu *menu() { return dynamic_cast<AptMainMenu *>(shell->findScreenByFilename("MainMenu.apt")); }
	// the RenderCredits clip's rectangle in stage units (its placeholder in the render list), false when it is not on screen
	bool creditsRect(float &x, float &y, float &w, float &h)
	{
		AptRenderList list;
		wm->apt().buildRenderList(list);
		for (const AptRenderCommand &c : list.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Placeholder && c.nativeTag && c.symbolName == "AptMainMenu::RenderCredits")
			{
				float ax, ay, bx, by;
				c.matrix.apply(c.bounds[0], c.bounds[1], ax, ay);
				c.matrix.apply(c.bounds[2], c.bounds[3], bx, by);
				x = std::min(ax, bx);
				y = std::min(ay, by);
				w = std::abs(bx - ax);
				h = std::abs(by - ay);
				return true;
			}
		}
		return false;
	}
};
} // namespace

TEST_CASE("ui4 retail: language.ini's Language block gives the credits fonts; adjustFontSize is floor(size * xRes / 1024)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const GlobalLanguage language = loadLanguage(mount.fs);
	CHECK(language.loaded);
	// the archive's language.ini (RotWK lang\English.big): CreditsTitleFont = "Albertus MT" 24 No, CreditsMinorTitleFont = ... 16 Yes, CreditsNormalFont = ... 14 No
	CHECK(language.creditsTitleFont.name == "Albertus MT");
	CHECK(language.creditsTitleFont.size == 24);
	CHECK_FALSE(language.creditsTitleFont.bold);
	CHECK(language.creditsMinorTitleFont.size == 16);
	CHECK(language.creditsMinorTitleFont.bold);
	CHECK(language.creditsNormalFont.size == 14);
	CHECK_FALSE(language.creditsNormalFont.bold);
	CHECK(language.thousandSeparator == ",");
	CHECK(language.unicodeFontName == "Arial Unicode MS");
	CHECK(language.resolutionFontAdjustment == 1.0f);
	// LocalFontFile: pushed to the front, so the file's last entry is first
	REQUIRE(language.localFonts.size() == 2);
	CHECK(language.localFonts.front() == "OmniaLTStd.ttf");
	CHECK(language.localFonts.back() == "AlbertusMT.otf");
	// RW 0x5E9BEF
	CHECK(GlobalLanguage::adjustFontSize(24, 1024) == 24);
	CHECK(GlobalLanguage::adjustFontSize(24, 1280) == 30);
	CHECK(GlobalLanguage::adjustFontSize(14, 800) == 10); // 10.9375 floored
	CHECK(GlobalLanguage::adjustFontSize(16, 1920) == 30);
}

TEST_CASE("ui4 retail: Credits.ini (a BOM line instead of a block keyword) loads through RW 0x42D753's every-line parser")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const GlobalLanguage language = loadLanguage(mount.fs);
	GameTextTableSource text;
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", bytes, &error), error);
	REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
	FixedMetrics metrics;
	CreditsManager credits;
	REQUIRE_NOTHROW(credits.load(mount.fs, &text, language, metrics, 1024));
	// the file's settings: ScrollRate 1, ScrollRateEveryFrames 1, ScrollDown No, TitleColor / MinorTitleColor R:192 G:238 B:255, NormalColor R:139 G:185 B:204
	CHECK(credits.scrollRate() == 1);
	CHECK(credits.scrollRatePerFrames() == 1);
	CHECK_FALSE(credits.scrollDown());
	CHECK(credits.titleColor() == 0xFFC0EEFFu);
	CHECK(credits.positionColor() == 0xFFC0EEFFu);
	CHECK(credits.normalColor() == 0xFF8BB9CCu);
	CHECK(credits.normalFontHeight() == 14 + 4); // CreditsNormalFont 14 at 1024 wide, FixedMetrics' height
	// the first lines: Blank, then TITLE "CREDITS:DevelopmentTitle1" (a label: lotr.str's text)
	const std::list<CreditsLine *> &lines = credits.lines();
	REQUIRE(lines.size() > 400);
	auto it = lines.begin();
	CHECK((*it)->style == CREDIT_STYLE_BLANK);
	++it;
	CHECK((*it)->style == CREDIT_STYLE_TITLE);
	std::u16string expected;
	REQUIRE(text.fetch("CREDITS:DevelopmentTitle1", expected));
	CHECK((*it)->text == expected);
	// a quoted name without ':' is widened byte by byte (UnicodeString::translate): the file's UTF-8 "Börjel" shows its two bytes, as in retail
	bool sawBorjel = false;
	for (const CreditsLine *line : lines)
	{
		sawBorjel = sawBorjel || line->text == widen("Joel B\xC3\xB6rjel") || line->secondText == widen("Joel B\xC3\xB6rjel");
	}
	CHECK(sawBorjel);
	for (const CreditsLine *line : lines)
	{
		CHECK(line->text.find(u"MISSING:") == std::u16string::npos);
	}
}

TEST_CASE("ui4 retail: the roll scrolls up 1 unit a frame from the bottom, a line enters when the last one moved its height + 2, alpha fades at the thirds")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const GlobalLanguage language = loadLanguage(mount.fs);
	FixedMetrics metrics;
	CreditsManager credits;
	REQUIRE_NOTHROW(credits.load(mount.fs, nullptr, language, metrics, 1024));
	credits.init();
	GadgetDrawList out;
	// nothing moves before the first draw gave the display size (RW 0x9C6BB5's guard)
	credits.update();
	CHECK(credits.framesSinceStarted() == 0);
	credits.draw(100, 50, 600, 300, out);
	CHECK(out.commands.empty());
	// frame 1: the first line (a Blank, the normal font's height) enters at the bottom (ScrollDown No: start = the height)
	credits.update();
	REQUIRE(credits.shownLines().size() == 1);
	CHECK(credits.shownLines().front()->posY == 300);
	CHECK(credits.shownLines().front()->height == 18);
	// it moves up 1 a frame; the next line (TITLE, 24 + 4 high) enters when the last line's y + height + 2 <= 300: 20 frames later
	for (int i = 0; i < 19; ++i)
	{
		credits.update();
	}
	CHECK(credits.shownLines().size() == 1);
	CHECK(credits.shownLines().front()->posY == 281);
	credits.update();
	REQUIRE(credits.shownLines().size() == 2);
	CHECK(credits.shownLines().front()->posY == 280);
	const CreditsLine *title = credits.shownLines().back();
	CHECK(title->style == CREDIT_STYLE_TITLE);
	CHECK(title->posY == 300);
	CHECK(title->height == 28);
	CHECK(title->posX == 300 - title->width / 2);
	// draw: below the lower third's end the alpha is 0 (y 300 is not < 0 and not > 300: 1 - (300 - 200) / 100 = 0)
	out.commands.clear();
	credits.draw(100, 50, 600, 300, out);
	REQUIRE(out.commands.size() == 1); // the Blank draws nothing
	CHECK(out.commands[0].x0 == 100 + title->posX);
	CHECK(out.commands[0].y0 == 50 + 300);
	CHECK((out.commands[0].color >> 24) == 0);
	CHECK((out.commands[0].color & 0xFFFFFF) == 0xC0EEFF);
	CHECK(out.commands[0].dropColor == 0);
	CHECK(out.commands[0].font.name == "Albertus MT");
	CHECK(out.commands[0].font.pointSize == 24);
	// 50 frames on: y 250, a quarter into the lower third: alpha 255 * (1 - 50 / 100) = 127
	for (int i = 0; i < 50; ++i)
	{
		credits.update();
	}
	CHECK(title->posY == 250);
	out.commands.clear();
	credits.draw(100, 50, 600, 300, out);
	bool found = false;
	for (const GadgetDrawCommand &c : out.commands)
	{
		if (c.text == title->text)
		{
			found = true;
			CHECK((c.color >> 24) == 127);
			CHECK(c.dropColor == 0x7F000000u);
		}
	}
	CHECK(found);
	// the roll ends: every line leaves the top, then the manager is finished
	int frames = 0;
	while (!credits.isFinished() && frames < 400000)
	{
		credits.update();
		++frames;
	}
	CHECK(credits.isFinished());
	CHECK(credits.shownLines().empty());
	CHECK(credits.linesLeft() == 0);
	// reset (vslot 9): back to the first line
	credits.reset();
	CHECK_FALSE(credits.isFinished());
	CHECK(credits.linesLeft() == credits.lines().size());
}

TEST_CASE("ui4 retail: the main menu's Credits rolls the credits on its page, RenderCredits draws them, Exit stops the roll")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuShell fx(mount);
	AptMainMenu *menu = fx.menu();
	REQUIRE(menu);
	CHECK_FALSE(menu->credits());
	// MainMenu.apt's CreditsButton (the Options nav's Credits entry): hides the menu, plays _CreditsMovie, GameCode('Credits')
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(menu->level(), "CreditsButton", {}, nullptr, &error), error);
	fx.tick(2);
	REQUIRE(menu->credits());
	CHECK(menu->menuState() == 4);
	CHECK(fx.services.countRequests(ShellAction::Credits) == 1);
	bool transitionStop = false;
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		CHECK_MESSAGE(n.kind != "credits", n.detail);
		transitionStop = transitionStop || (n.kind == "unported-transition" && n.detail.find("[S-2520] MainMenuToCreditsScreen") == 0);
	}
	CHECK(transitionStop); // stop S-2520: the page appears without its window transition
	fx.tick(30);
	float x = 0, y = 0, w = 0, h = 0;
	REQUIRE(fx.creditsRect(x, y, w, h));
	CHECK(w > 100);
	CHECK(h > 100);
	const WindowManager::RenderCallback *callback = fx.wm->renderCallback("AptMainMenu::RenderCredits");
	REQUIRE(callback);
	// the device draws every frame: the first draw gives the roll its size, the menu's update scrolls one unit per 10 ms of the shell clock
	GadgetDrawList out;
	(*callback)(x, y, w, h, out);
	const int before = menu->credits()->framesSinceStarted();
	fx.tick(10, 50); // 500 ms
	CHECK(menu->credits()->framesSinceStarted() - before == 50);
	for (int i = 0; i < 300; ++i)
	{
		out.commands.clear();
		(*callback)(x, y, w, h, out);
		fx.tick(1, 100);
	}
	CHECK_FALSE(out.commands.empty());
	CHECK(menu->credits()->shownLines().size() > 3);
	for (const GadgetDrawCommand &c : out.commands)
	{
		CHECK(c.kind == GadgetDrawCommand::Kind::Text);
		CHECK(c.y0 >= (int)y - 40);
	}
	// the page's Exit: ExitCreditsButton -> GameCode('CreditsExit')
	REQUIRE_MESSAGE(fx.wm->invokeAS(menu->level(), "ExitCreditsButton", {}, nullptr, &error), error);
	fx.tick(2);
	CHECK_FALSE(menu->credits());
	CHECK(menu->menuState() == 0);
	CHECK(fx.services.countRequests(ShellAction::CreditsExit) == 1);
	out.commands.clear();
	(*callback)(x, y, w, h, out);
	CHECK(out.commands.empty()); // RW 0x91B1B8: no roll, nothing drawn
}

TEST_CASE("ui4 retail: Custom Settings (AdvancedSettingsButton) opens Options.apt with AdvancedOnly, whose movie opens on its advanced page")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuShell fx(mount);
	AptMainMenu *menu = fx.menu();
	REQUIRE(menu);
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(menu->level(), "AdvancedSettingsButton", {}, nullptr, &error), error);
	fx.tick(60);
	REQUIRE(fx.shell->top());
	REQUIRE(fx.shell->top()->filename() == "Options.apt");
	AptOptionsScreen *options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
	REQUIRE(options);
	// the extern the movie reads (RW 0x91F7F5's seventh) and what the movie made of it: `_root.AdvancedOnly = true` and assignOpen('_advanced')
	std::string value;
	REQUIRE(fx.wm->invokeAS(options->level(), "GetExtern", { "AdvancedOnly" }, &value, &error));
	CHECK(value == "1");
	AptValue member;
	AptSpriteInst *root = fx.wm->apt().level(options->level());
	REQUIRE(root);
	REQUIRE(root->getOwn("AdvancedOnly", member));
	CHECK(member.toBoolean(7));
	// lane PLAY-2's advanced page: the movie's EnterAdvancedSettings (RW 0x91F043) put the screen on its page 2
	CHECK(options->pageState() == 2);
	// Settings (SettingsButton) gives the basic page
	MenuShell basic(mount);
	REQUIRE_MESSAGE(basic.wm->invokeAS(basic.menu()->level(), "SettingsButton", {}, nullptr, &error), error);
	basic.tick(60);
	REQUIRE(basic.shell->top()->filename() == "Options.apt");
	REQUIRE(basic.wm->invokeAS(basic.shell->top()->level(), "GetExtern", { "AdvancedOnly" }, &value, &error));
	CHECK(value == "0");
}

TEST_CASE("ui4 retail: the nav entries' dim look is authored: MenuExport's menu1_sub01 shows its text at alpha 127 on _up and 255 on _over")
{
	// the owner read the Options nav's SETTINGS / CUSTOM SETTINGS / CREDITS as greyed out. They are the movie's _up state: the entry clip
	// (MenuExport.menu1_sub01, imported by MainMenu.apt) places its text sprite (depth 11) with a colour transform of alpha 127 on frame 0 (`_up`)
	// and modifies it to 255 on frame 9 (`_over`); the text colour itself is _root.colorSubTextBright (0xC4D8DF)
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::string error;
	std::shared_ptr<const AptFile> file = loader.loadMovie("MenuExport", &error);
	REQUIRE_MESSAGE(file, error);
	std::uint32_t id = 0;
	bool ambiguous = false;
	REQUIRE(file->findExport("menu1_sub01", id, &ambiguous));
	REQUIRE(id < file->characters.size());
	const AptCharacter &entry = file->characters[id];
	REQUIRE(entry.frames.size() > 9);
	auto textPlace = [&](std::size_t frame) -> const AptPlaceObject * {
		for (const AptFrameItem &it : entry.frames[frame].items)
		{
			if (it.place && it.place->depth == 11)
			{
				return it.place.get();
			}
		}
		return nullptr;
	};
	const AptPlaceObject *up = textPlace(0);
	const AptPlaceObject *over = textPlace(9);
	REQUIRE(up);
	REQUIRE(over);
	CHECK((up->flags & APT_PLACE_HASCOLORTRANSFORM) != 0);
	CHECK(up->tint[3] == 127);
	CHECK((over->flags & APT_PLACE_HASCOLORTRANSFORM) != 0);
	CHECK(over->tint[3] == 255);
	bool upLabel = false, overLabel = false;
	for (const AptFrameItem &it : entry.frames[0].items)
	{
		upLabel = upLabel || it.label == "_up";
	}
	for (const AptFrameItem &it : entry.frames[9].items)
	{
		overLabel = overLabel || it.label == "_over";
	}
	CHECK(upLabel);
	CHECK(overLabel);
}
