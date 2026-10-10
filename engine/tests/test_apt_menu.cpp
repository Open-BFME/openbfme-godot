// OpenBFME retail tests: the real RotWK 2.01 main menu run by the Apt player (spec menus-apt.md steps A2 and A3), headless.
//
// The movies are read from the mounted retail archives; the click-through tests drive the player through instance paths and
// observed fscommands, never pixels.  Expected command names and arguments are what the retail bytecode sends
// (disassembly of MainMenu.apt: `GameCode('Skirmish')` -> `getURL2("FSCommand:AptMainMenu::Skirmish")` ...); the expected
// ORDER and the clip tree come from the file data, not from this engine's output.  The tests print SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include "GameClient/AptCanvas.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>

using namespace apttest;

namespace
{

struct MenuFx
{
	AptArchiveFileSource source;
	AptStubHost host;
	std::unique_ptr<Apt> apt;

	explicit MenuFx(AptRetail &mount, const std::string &movie = "MainMenu") : source(mount.fs)
	{
		// The engine side of the main menu: an extern provider the first frames read (menus-apt.md 1.2, `MainMenuUnlockBonusCampaign`)
		host.setExternValue("MainMenuUnlockBonusCampaign", "0");
		apt = std::make_unique<Apt>(source, host);
		std::string error;
		REQUIRE_MESSAGE(apt->loadMovie(0, "AptLevel0", &error), error); // the base movie the engine loads at init (spec 1.1)
		REQUIRE_MESSAGE(apt->loadMovie(1, movie, &error), error);
	}

	void tick(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			apt->update(33);
		}
	}
	AptCharacterInst *at(const std::string &path) { return apt->resolvePath(apt->level(1), path); }

	std::vector<AptRecordingHost::Command> take()
	{
		std::vector<AptRecordingHost::Command> out;
		out.swap(host.fscommands);
		return out;
	}

	static AptButtonInst *firstButton(AptCharacterInst *c)
	{
		if (!c)
		{
			return nullptr;
		}
		if (AptButtonInst *b = c->asButton())
		{
			return b;
		}
		if (AptSpriteInst *s = c->asSprite())
		{
			for (AptCharacterInst *k : s->children())
			{
				if (AptButtonInst *b = firstButton(k))
				{
					return b;
				}
			}
		}
		return nullptr;
	}

	// The centre of a button's hit bounds in stage coordinates.
	static void centreOf(AptButtonInst &b, float &x, float &y)
	{
		float x0, y0, x1, y1;
		REQUIRE(b.contentBounds(x0, y0, x1, y1));
		b.globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	}

	// Move onto the button, press, release; one frame each.
	void click(AptButtonInst &b)
	{
		float x, y;
		centreOf(b, x, y);
		REQUIRE_MESSAGE(apt->input().hitTestButtons(x, y) == &b, "the centre of the hit bounds is not inside the hit mesh of " << b.targetPath());
		apt->input().postMouseMove(x, y);
		tick(1);
		apt->input().postMouseButton(true);
		tick(1);
		apt->input().postMouseButton(false);
		tick(1);
	}

	std::size_t count(const std::string &command) const
	{
		std::size_t n = 0;
		for (const AptRecordingHost::Command &c : host.fscommands)
		{
			if (c.command == command)
			{
				++n;
			}
		}
		return n;
	}
	const AptRecordingHost::Command *find(const std::string &command) const
	{
		for (const AptRecordingHost::Command &c : host.fscommands)
		{
			if (c.command == command)
			{
				return &c;
			}
		}
		return nullptr;
	}
};

std::string errorsOf(const AptRecordingHost &host)
{
	std::string s;
	for (const std::string &e : host.errors)
	{
		s += e + "; ";
	}
	return s;
}

} // namespace

TEST_CASE("retail MainMenu: with AptLevel0 loaded the first update sends AptMainMenu::OnInitialized and the nav clips exist")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuFx fx(mount);
	// The menu's InitFunc/Startup ran at load (frame 0 actions) and the movie asked the engine for OnInitialized in its first
	// frames; the host saw it once, before the second update
	fx.tick(2);
	CHECK(fx.count("AptMainMenu::OnInitialized") == 1);
	const AptRecordingHost::Command *init = fx.find("AptMainMenu::OnInitialized");
	REQUIRE(init);
	CHECK(init->argument.empty());
	// the nav clips of the spec (menus-apt.md 1.2) are named instances of the root, at the depths the file places them
	struct Expect
	{
		const char *path;
		int depth;
	};
	const Expect expected[] = { { "SoloPlayNav", 199 }, { "MultiPlayNav", 37 }, { "OptionsNav", 118 }, { "MyHeroes", 25 }, { "QuitMainMenu", 13 } };
	for (const Expect &e : expected)
	{
		AptCharacterInst *c = fx.at(e.path);
		REQUIRE_MESSAGE(c, e.path);
		CHECK(c->depth() == e.depth);
		CHECK(c->type() == AptCharacterInst::Type::Sprite);
	}
	// the root of the level is the movie instance, one 17-frame movie at 33 ms
	AptSpriteInst *root = fx.apt->level(1);
	REQUIRE(root);
	CHECK(root->type() == AptCharacterInst::Type::Movie);
	CHECK(root->totalFrames() == 17);
	// the level-0 base movie answered with its own trace and its background command
	CHECK(fx.apt->level(0)->totalFrames() == 107);
	// retail script data: the movie's InitialSetup calls `stop` on a nav clip it names but never places (`TutorialsNav`: it is in the
	// constant pool and has no place object).  Retail skips that call silently (S-380); the player records it as a note, not an error.
	CHECK(fx.host.errors.empty());
	REQUIRE(fx.apt->noteCount("call-without-function") == 1);
	for (const AptNote &n : fx.apt->notes())
	{
		if (n.kind == "call-without-function")
		{
			CHECK(n.detail == "method 'stop' is not a function on a value of type undefined [in _level1]");
		}
	}
}

TEST_CASE("retail MainMenu: the nav clips start on their _hide frame; the nav items do not exist until the nav opens")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuFx fx(mount);
	fx.tick(10);
	AptSpriteInst *solo = fx.at("SoloPlayNav")->asSprite();
	REQUIRE(solo);
	CHECK(solo->frame == 0);        // `_hide`: the frame action is stop()
	CHECK_FALSE(solo->playing);
	CHECK(solo->totalFrames() == 51);
	CHECK(fx.at("SoloPlayNav.OpenButton") != nullptr);
	CHECK(fx.at("SoloPlayNav.Skirmish") == nullptr);
	CHECK(fx.at("SoloPlayNav.WarOfTheRing") == nullptr);
	CHECK(fx.at("OptionsNav.AdvancedSettings") == nullptr);
	CHECK(fx.at("MultiPlayNav.Replay") == nullptr);
	CHECK(solo->labelFrame("_hide") == 0);
	CHECK(solo->labelFrame("_show") == 9); // the nav items are placed on frame 9 (spec 1.2)
}

namespace
{
struct MenuClick
{
	const char *nav;       // the nav clip whose OpenButton opens the list ("" for a top-level button)
	const char *item;      // the instance name of the entry
	const char *command;   // the fscommand the entry sends
	const char *argument;  // its argument string
};
} // namespace

TEST_CASE("retail MainMenu: click through the nav lists - each entry sends its command exactly once")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const MenuClick table[] = {
		{ "SoloPlayNav", "Skirmish", "AptMainMenu::Skirmish", "" },
		{ "SoloPlayNav", "WarOfTheRing", "AptMainMenu::WarOfTheRing", "" },
		{ "SoloPlayNav", "LoadGame", "AptMainMenu::LoadGame", "" },
		{ "OptionsNav", "Settings", "AptMainMenu::Options", "false" },
		{ "OptionsNav", "AdvancedSettings", "AptMainMenu::Options", "true" },
		{ "OptionsNav", "Credits", "AptMainMenu::Credits", "" },
		{ "MultiPlayNav", "Online", "AptMainMenu::OnlineButtonPressed", "" },
		{ "MultiPlayNav", "LocalNetwork", "AptMainMenu::LAN", "" },
		{ "MultiPlayNav", "Replay", "AptMainMenu::LoadReplay", "" },
	};
	for (const MenuClick &t : table)
	{
		MenuFx fx(mount);
		fx.tick(10);
		fx.take();
		// 1. open the nav: the centre of the button that sits over `<nav>.OpenButton`
		AptCharacterInst *nav = fx.at(t.nav);
		REQUIRE_MESSAGE(nav, t.nav);
		AptButtonInst *open = nullptr;
		for (AptCharacterInst *k : nav->asSprite()->children())
		{
			if (k->asButton())
			{
				open = k->asButton();
				break;
			}
		}
		REQUIRE_MESSAGE(open, t.nav);
		fx.click(*open);
		// 2. wait for the entry (placed on frame 9 of the nav) and for its own button (placed on its frame 9)
		AptCharacterInst *item = nullptr;
		AptButtonInst *itemButton = nullptr;
		int waited = 0;
		for (; waited < 120; ++waited)
		{
			item = fx.at(std::string(t.nav) + "." + t.item);
			itemButton = MenuFx::firstButton(item);
			if (itemButton)
			{
				break;
			}
			fx.tick(1);
		}
		INFO(t.nav << "." << t.item << " waited " << waited << " frames");
		REQUIRE(item);
		REQUIRE(itemButton);
		CHECK(waited < 40);
		fx.tick(20); // let the reveal animation of the entry finish
		fx.take();
		// 3. click it
		fx.click(*itemButton);
		fx.tick(2);
		CHECK_MESSAGE(fx.count(t.command) == 1, t.command << " fired " << fx.count(t.command) << " times");
		if (const AptRecordingHost::Command *c = fx.find(t.command))
		{
			CHECK(c->argument == t.argument);
		}
		// nothing else but sounds went to the engine
		for (const AptRecordingHost::Command &c : fx.host.fscommands)
		{
			if (c.command != t.command)
			{
				CHECK_MESSAGE(c.command == "PlaySound", "unexpected fscommand " << c.command << " | " << c.argument);
			}
		}
		// and it does not repeat
		fx.take();
		fx.tick(60);
		CHECK(fx.count(t.command) == 0);
	}
}

TEST_CASE("retail MainMenu: Create-a-Hero and Exit send their commands after the reveal and release animations")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	struct Case
	{
		const char *clip;
		const char *command;
	};
	const Case cases[] = { { "MyHeroes", "AptMainMenu::CreateAHero" }, { "QuitMainMenu", "AptMainMenu::ExitGame" } };
	for (const Case &c : cases)
	{
		MenuFx fx(mount);
		fx.tick(10);
		// The two buttons sit on their `_reveal` frames until the movie's ShowMainMenu() runs; the engine calls it (the engine side
		// that does so is unverified, spec 1.2), by invoking the function on the level
		std::string error;
		REQUIRE_MESSAGE(fx.apt->invoke(fx.apt->level(1), "ShowMainMenu", {}, nullptr, &error), error);
		fx.tick(60);
		AptButtonInst *b = MenuFx::firstButton(fx.at(c.clip));
		REQUIRE_MESSAGE(b, c.clip);
		fx.take();
		fx.click(*b);
		fx.tick(60); // the release animation ends with the command (retail trace `$MyHeroes_release end`)
		CHECK_MESSAGE(fx.count(c.command) == 1, c.command << " fired " << fx.count(c.command) << " times");
		fx.take();
		fx.tick(60);
		CHECK(fx.count(c.command) == 0);
	}
}

TEST_CASE("retail MainMenu: the render list of the first frames is the shapes and texts of the placed clips")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuFx fx(mount);
	fx.tick(10);
	// the engine reveals the main menu (ShowMainMenu, spec 1.2); the Quit and My Heroes buttons run their reveal animation
	std::string error;
	REQUIRE_MESSAGE(fx.apt->invoke(fx.apt->level(1), "ShowMainMenu", {}, nullptr, &error), error);
	fx.tick(60);
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	CHECK(rl.errors.empty());
	// every command is a shape or a text at this state (no masks, no host-flagged components), except the one clip the main menu's own
	// script tagged `_type = "RenderImage"` (its `Image`): a Placeholder, with its authored magenta placeholder art not emitted (S-136)
	CHECK(rl.count(AptRenderCommand::Kind::Shape) + rl.count(AptRenderCommand::Kind::Text) + rl.count(AptRenderCommand::Kind::Placeholder) == rl.commands.size());
	REQUIRE(rl.count(AptRenderCommand::Kind::Placeholder) == 1);
	CHECK(rl.forPath("_level1.Image.instance1").empty());
	// the Quit button's first shape sits where the file places the clip: translation (917.3, 711.85), identity matrix
	std::vector<const AptRenderCommand *> quit = rl.forPath("_level1.QuitMainMenu.instance1.instance1");
	REQUIRE(quit.size() == 1);
	CHECK(quit[0]->kind == AptRenderCommand::Kind::Shape);
	CHECK(quit[0]->matrix.tx == doctest::Approx(917.3f));
	CHECK(quit[0]->matrix.ty == doctest::Approx(711.85f));
	CHECK(quit[0]->matrix.a == doctest::Approx(1.0f));
	CHECK(quit[0]->matrix.d == doctest::Approx(1.0f));
	CHECK(quit[0]->shapeMovie == "MenuExport");
	REQUIRE_FALSE(quit[0]->fills.empty());
	// the button label is text with its font: "$Quit" set by the clip's Load event, Albertus MT, 14 high (spec 2.6)
	std::vector<const AptRenderCommand *> label = rl.forPath("_level1.QuitMainMenu.instance3.Text");
	REQUIRE(label.size() == 1);
	CHECK(label[0]->kind == AptRenderCommand::Kind::Text);
	CHECK(label[0]->text == "$Quit");
	CHECK(label[0]->fontName == "Albertus MT");
	CHECK(label[0]->fontHeight == doctest::Approx(14.0f));
	// every textured fill names a texture that exists in the mounted archives, as spec 2.1 says (art/Textures/apt_X_<n>.tga)
	std::set<std::string> textures;
	for (const AptRenderCommand &c : rl.commands)
	{
		for (const AptRenderFill &f : c.fills)
		{
			if (f.kind == APT_STYLE_TEXTURED)
			{
				REQUIRE(f.imageResolved);
				textures.insert(f.textureName);
			}
		}
	}
	CHECK_FALSE(textures.empty());
	for (const std::string &t : textures)
	{
		CHECK_MESSAGE(fx.source.fileExists("art/textures/" + t), "missing texture " << t);
	}
	// the render order is depth order: the Quit shapes (depth 13) are before the My Heroes shapes (depth 25)
	std::size_t quitFirst = rl.commands.size(), heroesFirst = rl.commands.size();
	for (std::size_t i = 0; i < rl.commands.size(); ++i)
	{
		if (quitFirst == rl.commands.size() && rl.commands[i].path.find("QuitMainMenu") != std::string::npos)
		{
			quitFirst = i;
		}
		if (heroesFirst == rl.commands.size() && rl.commands[i].path.find("MyHeroes") != std::string::npos)
		{
			heroesFirst = i;
		}
	}
	CHECK(quitFirst < heroesFirst);
}

TEST_CASE("retail MainMenu: opening a nav adds its entries to the render list; the colour transform and matrices compose down the tree")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuFx fx(mount);
	fx.tick(10);
	AptRenderList before;
	fx.apt->buildRenderList(before);
	AptCharacterInst *nav = fx.at("SoloPlayNav");
	AptButtonInst *open = nullptr;
	for (AptCharacterInst *k : nav->asSprite()->children())
	{
		if (k->asButton())
		{
			open = k->asButton();
			break;
		}
	}
	REQUIRE(open);
	fx.click(*open);
	fx.tick(30);
	AptRenderList after;
	fx.apt->buildRenderList(after);
	CHECK(after.commands.size() > before.commands.size());
	// the entry texts are in the list, at the nav's position plus the entry offset
	std::size_t skirmishTexts = 0;
	for (const AptRenderCommand &c : after.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Text && c.path.find("SoloPlayNav.Skirmish") != std::string::npos)
		{
			++skirmishTexts;
		}
	}
	CHECK(skirmishTexts >= 1);
	// matrices compose: a shape under SoloPlayNav.OpenButton is translated by at least the nav's own (109.5, 711.2) offset
	for (const AptRenderCommand &c : after.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Shape && c.path.rfind("_level1.SoloPlayNav.", 0) == 0)
		{
			CHECK(c.matrix.tx > 109.5f - 300.0f);
			CHECK(c.matrix.tx < 1100.0f);
			CHECK(c.matrix.ty > 0.0f);
		}
	}
}

// ---- the whole corpus ------------------------------------------------------------------------------------------------

namespace
{
std::string playerBaselinePath()
{
	return std::string(OPENBFME_APT_TEST_DATA_DIR) + "/player_corpus_baseline.tsv";
}

bool loadPlayerBaseline(std::map<std::string, std::size_t> &out, std::string &error)
{
	std::ifstream in(playerBaselinePath(), std::ios::binary);
	if (!in)
	{
		error = "cannot open " + playerBaselinePath();
		return false;
	}
	std::string line;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		std::size_t last = line.rfind('\t');
		if (last == std::string::npos)
		{
			error = "malformed baseline line: " + line;
			return false;
		}
		out[line.substr(0, last)] = (std::size_t)std::stoull(line.substr(last + 1));
	}
	return true;
}
} // namespace

// What this gate establishes: every one of the 86 retail movies loads as a level, its imports resolve, and 100 frames of the
// player run without a VM fault.  Survivable script errors (a movie asking its host for something the headless stub does not
// provide, a script calling a function the surrounding screen would define) and the player's own notes (behaviours the binary
// reading did not settle, registered in docs/STOPS.md) are compared occurrence by occurrence with a reviewed baseline; a new
// identity or a changed count fails.  Regenerate with OPENBFME_APT_PLAYER_BASELINE_WRITE=<file> and review the diff.
//
// What it is NOT: an oracle.  The TSV is a snapshot of what this implementation reports (it is written by this test), so it is a
// deterministic regression and survivability gate, not independent evidence that the player matches retail behaviour or that a
// listed error is a host-only gap.  The expected-behaviour evidence is the binary-derived synthetic tests (test_apt_player.cpp,
// test_apt_input.cpp, test_apt_player_review.cpp) and the click-through tests above.
TEST_CASE("retail corpus: every movie runs 100 frames in the player without a VM fault; errors and notes match the reviewed baseline")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	std::map<std::string, std::size_t> occurrences;
	std::size_t movies = 0;
	std::size_t faults = 0;
	std::string firstFault;
	for (const std::string &name : source.listMovies())
	{
		AptStubHost host;
		Apt apt(source, host);
		std::string error;
		REQUIRE_MESSAGE(apt.loadMovie(1, name, &error), name << ": " << error);
		for (int f = 0; f < 100; ++f)
		{
			apt.update(33);
		}
		++movies;
		if (apt.vm().faultCount() != 0)
		{
			++faults;
			if (firstFault.empty())
			{
				firstFault = name;
			}
		}
		for (const std::string &e : host.errors)
		{
			++occurrences["error\t" + AptPropertyMap::foldKey(name) + ": " + e];
		}
		for (const AptNote &n : apt.notes())
		{
			++occurrences["note\t" + AptPropertyMap::foldKey(name) + ": " + n.kind + ": " + n.detail];
		}
	}
	CHECK(movies == 86);
	CHECK_MESSAGE(faults == 0, faults << " movies faulted, first " << firstFault);
	if (const char *writeTo = std::getenv("OPENBFME_APT_PLAYER_BASELINE_WRITE"))
	{
		std::ofstream out(writeTo, std::ios::binary);
		out << "# Reviewed baseline of the survivable script errors and the player notes while each of the 86 retail movies runs 100 frames\n"
			   "# alone as _level1 on the headless stub host.  kind<TAB>movie: identity<TAB>count.  Notes are registered stops (docs/STOPS.md).\n";
		for (const auto &kv : occurrences)
		{
			out << kv.first << "\t" << kv.second << "\n";
		}
		MESSAGE("baseline written to " << std::string(writeTo));
		return;
	}
	std::map<std::string, std::size_t> baseline;
	std::string baselineError;
	REQUIRE_MESSAGE(loadPlayerBaseline(baseline, baselineError), baselineError);
	std::string differences;
	std::size_t differenceCount = 0;
	for (const auto &o : occurrences)
	{
		auto b = baseline.find(o.first);
		if (b == baseline.end() || b->second != o.second)
		{
			if (++differenceCount <= 20)
			{
				differences += "\n  " + o.first + ": " + std::to_string(o.second) + (b == baseline.end() ? " (new identity)" : " (baseline " + std::to_string(b->second) + ")");
			}
		}
	}
	for (const auto &b : baseline)
	{
		if (!occurrences.count(b.first))
		{
			if (++differenceCount <= 20)
			{
				differences += "\n  " + b.first + ": gone (baseline " + std::to_string(b.second) + ")";
			}
		}
	}
	CHECK_MESSAGE(differenceCount == 0, differenceCount << " occurrence differences from the reviewed baseline:" << differences);
}

// Lane FB7-1 (community FB-0007, stop S-1910): the main menu's button labels are multiline edit texts WITHOUT word wrap. RotWK's Apt display
// string (ctor RW 0x4AA369: wrap only when the word-wrap flag +0x24 is set; vertical flag +0x2E = !multiline || !wordWrap; draw RW 0x4A8F95) centres
// such a field vertically in its box. The port's device treated "multiline" as "wraps" and put the text at the top of the box.
TEST_CASE("fb7 main menu: a button label (multiline, no word wrap, alignment 2) is centred vertically in its box as RW 0x4A8F95 places it")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MenuFx fx(mount);
	fx.tick(10);
	std::string error;
	REQUIRE_MESSAGE(fx.apt->invoke(fx.apt->level(1), "ShowMainMenu", {}, nullptr, &error), error);
	fx.tick(60);
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	std::vector<const AptRenderCommand *> quit = rl.forPath("_level1.QuitMainMenu.instance3.Text");
	REQUIRE(quit.size() == 1);
	const AptRenderCommand &t = *quit[0];
	REQUIRE(t.kind == AptRenderCommand::Kind::Text);
	// the field as MainMenu.apt defines it: bounds (-2 -2 158 39), alignment 2, multiline, no word wrap
	CHECK(t.bounds[0] == -2.0f);
	CHECK(t.bounds[1] == -2.0f);
	CHECK(t.bounds[2] == 158.0f);
	CHECK(t.bounds[3] == doctest::Approx(39.2f));
	CHECK(t.alignment == 2);
	CHECK(t.multiline);
	CHECK_FALSE(t.wordWrap);
	// retail's placement in a 1:1 window (box 160 x 41.2 at the instance, a 17 pixel high string 60 wide): centred both ways, whole pixels
	const float x0 = t.matrix.a * t.bounds[0] + t.matrix.c * t.bounds[1] + t.matrix.tx, y0 = t.matrix.b * t.bounds[0] + t.matrix.d * t.bounds[1] + t.matrix.ty;
	const float x1 = t.matrix.a * t.bounds[2] + t.matrix.c * t.bounds[3] + t.matrix.tx, y1 = t.matrix.b * t.bounds[2] + t.matrix.d * t.bounds[3] + t.matrix.ty;
	const AptTextPlacement p = PlaceAptText(x0, y0, x1, y1, 60.0f, 17.0f, t.alignment, t.multiline, t.wordWrap);
	CHECK(p.centredVertically);
	CHECK(p.y == (float)(int)((y1 - y0 - 17.0f) * 0.5f + y0));
	CHECK(p.y - y0 >= 11.0f); // about 12 below the top (0.5 * (41.2 - 17) = 12.1, truncated to a pixel), not on it
	CHECK(p.y - y0 <= 13.0f);
	CHECK(p.x == (float)(int)((x1 - x0 - 60.0f) * 0.5f + x0));
}
