// OpenBFME tests: the WindowManager, the Shell and the APT screens (spec menus-apt.md build step A4).
//
// Synthetic tests assemble tiny movies in memory (AptTestUtil.h) and check the registry, slot and shell rules against the decompile
// facts quoted in GameClient/GUI/WindowManager.h.  The retail tests drive the real RotWK 2.01 MainMenu.apt and Skirmish.apt through
// the WindowManager and the Shell (SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset); the binary-fact tests read the registered
// names out of game.dat (RW_GAME_DAT, or ROTWK_INSTALL + /game.dat) and print SKIP when neither is set.  GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

using namespace apttest;

namespace
{

// ---- binary facts ---------------------------------------------------------------------------------------------------------

bool readGameDat(std::vector<unsigned char> &out)
{
	std::string path;
	if (const char *p = std::getenv("RW_GAME_DAT"))
	{
		path = p;
	}
	else if (const char *install = std::getenv("ROTWK_INSTALL"))
	{
		path = std::string(install) + "/game.dat";
	}
	if (path.empty())
	{
		return false;
	}
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		FAIL("cannot read game.dat at " << path);
	}
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

// Every NUL-terminated string in the image that starts with `prefix` (the strings the engine registers by name).
std::set<std::string> binaryStrings(const std::vector<unsigned char> &image, const std::string &prefix)
{
	std::set<std::string> out;
	const std::string blob(image.begin(), image.end());
	std::size_t pos = 0;
	while ((pos = blob.find(prefix, pos)) != std::string::npos)
	{
		const bool atStart = pos == 0 || blob[pos - 1] == '\0';
		const std::size_t end = blob.find('\0', pos);
		if (atStart && end != std::string::npos)
		{
			out.insert(blob.substr(pos, end - pos));
		}
		pos += prefix.size();
	}
	return out;
}

// ---- synthetic fixtures ---------------------------------------------------------------------------------------------------

// A movie with one root frame that sends `commands` as FSCommands, plus an OnFocus function that sends "OnFocusCalled".
std::uint32_t buildScreenMovie(TestMovie &m, const std::vector<std::string> &rootCommands, bool withOnFocus)
{
	std::uint32_t frame = program(m, [&](Asm &a) {
		for (const std::string &c : rootCommands)
		{
			fscmd(a, c);
		}
		if (withOnFocus)
		{
			a.defineFunction("OnFocus", {}, [](Asm &f) { fscmd(f, "OnFocusCalled"); });
		}
	});
	m.setRootFrames({ { m.addActionItem(frame) } });
	return frame;
}

struct ShellFx
{
	MemorySource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<Shell> shell;

	ShellFx()
	{
		TestMovie level0;
		buildScreenMovie(level0, {}, false);
		source.add("AptLevel0", level0);
		wm = std::make_unique<WindowManager>(source, services);
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
	}
	void addMovie(const std::string &name, const std::vector<std::string> &commands, bool withOnFocus = false)
	{
		TestMovie m;
		buildScreenMovie(m, commands, withOnFocus);
		source.add(name, m);
	}
	void tick(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
};

// A screen with its own commands, for the shell tests.
class TestScreen : public AptScreen
{
public:
	TestScreen(WindowManager &windows, Shell &shell, const std::string &file, std::vector<std::string> *log)
		: AptScreen(windows, shell, file, "T"), m_log(log)
	{
		registerCommand("T::" + file, [this](const std::string &) { m_log->push_back("cmd " + this->filename()); });
	}
	~TestScreen() override { m_log->push_back("destroyed " + filename()); }
	void runInit() override
	{
		m_log->push_back("init " + filename());
		AptScreen::runInit();
	}
	void runShutdown(bool *immediate) override
	{
		m_log->push_back("shutdown " + filename());
		AptScreen::runShutdown(immediate);
	}

private:
	std::vector<std::string> *m_log;
};

void registerTestScreen(AptScreenFactoryTable &table, const std::string &file, std::vector<std::string> *log)
{
	table.registerFactory(file, [file, log](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<TestScreen>(c.windows, c.shell, file, log);
	});
}

} // namespace

// ============================================================================================================================
// WindowManager: slots
// ============================================================================================================================

TEST_CASE("WindowManager: loadAptWindow answers like retail (duplicate -1, occupied slot -1, slot >= 12 gives 0, first free slot for -1)")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	CHECK(wm.loadAptWindow("Apt", "A.apt", true, 7, 3) == 3);            // a requested free slot
	CHECK(wm.aptWindow(3).directory == "Apt\\");                         // the directory gets its separator
	CHECK(wm.aptWindow(3).file == "A.apt");
	CHECK(wm.aptWindow(3).parameter == 7);
	CHECK(wm.aptWindow(3).index == 3);
	CHECK(wm.aptWindow(3).flags == WindowManager::APTWIN_LOAD_PENDING);
	CHECK(wm.loadAptWindow("Apt\\", "A.apt", true, 0, -1) == -1);        // the file is mapped already
	CHECK(wm.loadAptWindow("Apt\\", "B.apt", true, 0, 3) == -1);         // the requested slot is occupied
	CHECK(wm.loadAptWindow("Apt\\", "B.apt", true, 0, 12) == 0);         // out of range: retail returns 0, not -1
	CHECK(wm.loadAptWindow("Apt/", "B.apt", false, 0, -1) == 0);         // the first free slot; '/' is accepted as a separator
	CHECK(wm.aptWindow(0).directory == "Apt/");
	CHECK(wm.aptWindow(0).flags == 0);                                   // no load requested
	CHECK(wm.findAptMovieIndex("B.apt") == 0);
	CHECK(wm.findAptMovieIndex("nope.apt") == -1);
	// twelve slots: fill the rest, the thirteenth is refused
	int next = 1;
	for (int i = 0; i < 10; ++i)
	{
		while (next == 3)
		{
			++next;
		}
		const std::string f = "F" + std::to_string(i) + ".apt";
		CHECK(wm.loadAptWindow("Apt\\", f, false, 0, -1) == next);
		++next;
	}
	CHECK(wm.loadAptWindow("Apt\\", "Extra.apt", false, 0, -1) == -1);
}

TEST_CASE("WindowManager: init loads AptLevel0 into slot 0 on the first update; pending windows load on update and focus is announced")
{
	ShellFx fx;
	fx.addMovie("Screen", { "ScreenStarted" }, true);
	WindowManager &wm = *fx.wm;
	std::vector<std::string> seen;
	wm.registerCommand("ScreenStarted", [&](const std::string &) { seen.push_back("ScreenStarted"); });
	wm.registerCommand("OnFocusCalled", [&](const std::string &) { seen.push_back("OnFocusCalled"); });
	wm.init();
	CHECK(wm.aptWindow(0).file == "AptLevel0.apt");
	CHECK(wm.findAptMovieIndex("AptLevel0.apt") == 0);
	CHECK_FALSE(wm.isAptWindowLoaded(0)); // the movie loads on the next update
	CHECK((wm.aptWindow(0).flags & WindowManager::APTWIN_FLAG4) == 0);
	int slot = wm.loadAptWindow("Apt\\", "Screen.apt", true, 0, -1);
	CHECK(slot == 1);
	fx.tick(1);
	CHECK(wm.isAptWindowLoaded(0));
	CHECK(wm.isAptWindowLoaded(1));
	CHECK(wm.apt().level(1) != nullptr);
	std::string errs;
	for (const std::string &e : wm.errors())
	{
		errs += e + "; ";
	}
	CHECK_MESSAGE(wm.errors().empty(), errs);
	REQUIRE_FALSE(seen.empty());
	CHECK(seen[0] == "ScreenStarted");
	// the focus refresh ran once the windows were loaded: the movie with an OnFocus function got "1" (focus = AptLevel0 default
	// vector entry 12 = every loaded window)
	fx.tick(2);
	CHECK(std::count(seen.begin(), seen.end(), "OnFocusCalled") == 1);
	CHECK((wm.aptWindow(1).flags & WindowManager::APTWIN_FOCUS) != 0);
}

TEST_CASE("WindowManager: a movie that is not there is an error that reaches errors(), not a silent empty level")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	wm.init();
	wm.loadAptWindow("Apt\\", "Missing.apt", true, 0, -1);
	fx.tick(1);
	REQUIRE(wm.errors().size() >= 1);
	CHECK(wm.errors()[0].find("Missing.apt") != std::string::npos);
	CHECK(wm.noteCount("movie-load-failed") == 1);
	CHECK_FALSE(wm.isAptWindowLoaded(1));
}

TEST_CASE("WindowManager: unloadAptWindow frees the slot and the file name; hide/show toggle the level's visibility")
{
	ShellFx fx;
	fx.addMovie("Screen", {});
	WindowManager &wm = *fx.wm;
	wm.init();
	int slot = wm.loadAptWindow("Apt\\", "Screen.apt", true, 0, -1);
	fx.tick(1);
	REQUIRE(wm.isAptWindowLoaded(slot));
	CHECK(wm.hideAptWindow(slot));
	CHECK_FALSE(wm.apt().level(slot)->visible);
	CHECK(wm.showAptWindow(slot));
	CHECK(wm.apt().level(slot)->visible);
	CHECK_FALSE(wm.hideAptWindow(9)); // not loaded
	CHECK_FALSE(wm.hideAptWindow(40));
	CHECK(wm.unloadAptWindow(slot));
	CHECK(wm.apt().level(slot) == nullptr);
	CHECK(wm.findAptMovieIndex("Screen.apt") == -1);
	CHECK(wm.loadAptWindow("Apt\\", "Screen.apt", true, 0, -1) == slot); // the slot is free again
}

// ============================================================================================================================
// WindowManager: the registries
// ============================================================================================================================

TEST_CASE("WindowManager registries: the first registration of a name is kept; a null functor is ignored; names are exact")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	int first = 0, second = 0;
	CHECK(wm.registerCommand("Cmd", [&](const std::string &) { ++first; }));
	CHECK_FALSE(wm.registerCommand("Cmd", [&](const std::string &) { ++second; }));
	CHECK_FALSE(wm.registerCommand("Null", nullptr));
	CHECK_FALSE(wm.hasCommand("Null"));
	CHECK(wm.invokeCallback("Cmd", "x"));
	CHECK(first == 1);
	CHECK(second == 0);
	CHECK_FALSE(wm.invokeCallback("cmd", "x")); // case-sensitive, like the AsciiString hash map
	CHECK(wm.registerProvider("P", [](const std::string &, std::string &, bool) { return true; }));
	CHECK_FALSE(wm.registerProvider("P", [](const std::string &, std::string &, bool) { return false; }));
	CHECK(wm.registerComponent("C", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; }));
	CHECK_FALSE(wm.registerComponent("C", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; }));
	CHECK(wm.registerScreenRef("S::InitGadgets", [](const std::string &, GameWindow *) {}));
	CHECK_FALSE(wm.registerScreenRef("S::InitGadgets", [](const std::string &, GameWindow *) {}));
	CHECK(wm.registerTooltip("T", [](const std::string &) { return std::string("L"); }));
	CHECK_FALSE(wm.registerTooltip("T", [](const std::string &) { return std::string("M"); }));
	// unregistering makes the name free again
	CHECK(wm.unregisterCommand("Cmd"));
	CHECK(wm.registerCommand("Cmd", [&](const std::string &) { ++second; }));
	wm.invokeCallback("Cmd", "");
	CHECK(second == 1);
	// closeAptScreen erases the screen reference
	wm.closeAptScreen("S::InitGadgets");
	CHECK_FALSE(wm.hasScreenRef("S::InitGadgets"));
}

TEST_CASE("WindowManager: an unknown command is logged and does not throw; the handler may unregister itself")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	CHECK_FALSE(wm.invokeCallback("AptPlayerTribute::OnInitialized", ""));
	CHECK_FALSE(wm.invokeCallback("", "")); // a null name returns without a note
	CHECK(wm.noteCount("command-unhandled") == 1);
	CHECK(wm.notes().back().detail == "AptPlayerTribute::OnInitialized");
	wm.registerCommand("Once", [&](const std::string &) { wm.unregisterCommand("Once"); });
	CHECK(wm.invokeCallback("Once", ""));
	CHECK_FALSE(wm.hasCommand("Once"));
}

TEST_CASE("WindowManager providers: a value, 'no value' and 'no provider' are three different answers; the level prefix is retried")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	wm.registerProvider("Has", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "42";
		}
		return true;
	});
	wm.registerProvider("Empty", [](const std::string &, std::string &, bool) { return false; });
	std::string v;
	CHECK(wm.getExtern("Has", v) == AptExternResult::Value);
	CHECK(v == "42");
	CHECK(wm.getExtern("Empty", v) == AptExternResult::Undefined);
	CHECK(wm.getExtern("Nobody", v) == AptExternResult::NoProvider);
	CHECK(wm.noteCount("extern-read-unhandled") == 1);
	// `_level3/Has` is retried without the prefix (BfmeSkipLevelPrefix.cpp)
	CHECK(wm.getExtern("_level3/Has", v) == AptExternResult::Value);
	CHECK(wm.getExtern("_level12.Has", v) == AptExternResult::Value);
	// a write reaches the provider with setting = true and the written string
	std::string written;
	wm.registerProvider("Sink", [&](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			written = value;
		}
		return true;
	});
	CHECK(wm.setExtern("Sink", "hello"));
	CHECK(written == "hello");
	CHECK_FALSE(wm.setExtern("Nobody", "x"));
	CHECK(wm.noteCount("extern-write-unhandled") == 1);
}

TEST_CASE("WindowManager: the built-in providers answer InGame 1 and the demo / trace flags 0 (the movies read InGame to leave authoring mode)")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	wm.init();
	std::string v;
	CHECK(wm.getExtern("InGame", v) == AptExternResult::Value);
	CHECK(v == "1");
	for (const char *flag : { "InBetaDemo", "InDreamMachineDemo", "DoTrace" })
	{
		CHECK(wm.getExtern(flag, v) == AptExternResult::Value);
		CHECK(v == "0");
	}
	CHECK(wm.setExtern("InGame", "0")); // a write is ignored
	CHECK(wm.getExtern("InGame", v) == AptExternResult::Value);
	CHECK(v == "1");
	// the global commands and the gadget component names the binary registers
	for (const char *c : { "PlaySound", "SetBackground", "MouseSetVisibility", "CloseWindow", "OnClickThroughPress", "OnClickThroughRelease", "EnableComponents", "DisableComponents" })
	{
		CHECK_MESSAGE(wm.hasCommand(c), c);
	}
	wm.invokeCallback("PlaySound", "click");
	wm.invokeCallback("SetBackground", "fadein");
	wm.invokeCallback("MouseSetVisibility", "1");
	CHECK(fx.services.sounds == std::vector<std::string>{ "click" });
	CHECK(fx.services.backgrounds == std::vector<std::string>{ "fadein" });
	CHECK(fx.services.mouseVisibility == std::vector<bool>{ true });
}

TEST_CASE("WindowManager: setAptText creates the record, tells the bound listener, and a rebind starts from the stored text")
{
	ShellFx fx;
	WindowManager &wm = *fx.wm;
	CHECK(wm.aptText("APT:Title") == nullptr);
	wm.setAptText("APT:Title", "first");
	REQUIRE(wm.aptText("APT:Title"));
	CHECK(*wm.aptText("APT:Title") == "first");
	std::vector<std::string> heard;
	wm.bindAptText("APT:Title", "default", [&](const std::string &t) { heard.push_back(t); });
	CHECK(heard == std::vector<std::string>{ "first" }); // the stored text wins over the default
	wm.setAptText("APT:Title", "second");
	CHECK(heard == std::vector<std::string>{ "first", "second" });
	wm.bindAptText("APT:Title", "", nullptr); // unbind
	wm.setAptText("APT:Title", "third");
	CHECK(heard.size() == 2);
	// a record with no text takes the default at bind time
	std::string got;
	wm.bindAptText("APT:Other", "dflt", [&](const std::string &t) { got = t; });
	CHECK(got == "dflt");
}

TEST_CASE("WindowManager: invokeAS calls a function of the level with string arguments and returns its result; a missing function is an error")
{
	ShellFx fx;
	TestMovie m;
	std::uint32_t frame = program(m, [&](Asm &a) {
		a.defineFunction("Echo", { "x" }, [](Asm &f) {
			f.pushString("FSCommand:Echoed");
			f.getStringVar("x");
			f.op(APT_OP_GETURL2);
		});
	});
	m.setRootFrames({ { m.addActionItem(frame) } });
	fx.source.add("Fn", m);
	WindowManager &wm = *fx.wm;
	wm.init();
	std::string got;
	wm.registerCommand("Echoed", [&](const std::string &arg) { got = arg; });
	wm.loadAptWindow("Apt\\", "Fn.apt", true, 0, -1);
	fx.tick(1);
	std::string error;
	CHECK_MESSAGE(wm.invokeAS(1, "Echo", { "hello" }, nullptr, &error), error);
	CHECK_FALSE(wm.invokeAS(1, "Nope", {}, nullptr, &error));
	CHECK(error.find("Nope") != std::string::npos);
	CHECK_FALSE(wm.invokeAS(5, "Echo", {}, nullptr, &error)); // no movie in the level
	CHECK(error.find("_level5") != std::string::npos);
}

// ============================================================================================================================
// Shell
// ============================================================================================================================

TEST_CASE("Shell: push loads the screen's movie into a level, a push over a screen hides it, pop destroys the top and re-inits the one below")
{
	std::vector<std::string> log; // outlives the screens the fixture destroys
	ShellFx fx;
	registerTestScreen(fx.factories, "One.apt", &log);
	registerTestScreen(fx.factories, "Two.apt", &log);
	fx.addMovie("One", {});
	fx.addMovie("Two", {});
	fx.wm->init();
	fx.shell->push("One.apt");
	REQUIRE(fx.shell->screenCount() == 1);
	CHECK(fx.shell->top()->filename() == "One.apt");
	CHECK(fx.shell->top()->level() == 1); // AptLevel0 holds slot 0
	CHECK(log == std::vector<std::string>{ "init One.apt" });
	fx.tick(1);
	CHECK(fx.wm->isAptWindowLoaded(1));
	log.clear();
	fx.shell->push("Two.apt");
	REQUIRE(fx.shell->screenCount() == 2);
	CHECK(log == std::vector<std::string>{ "shutdown One.apt", "init Two.apt" });
	CHECK(fx.shell->screenAt(0)->isHidden());
	CHECK_FALSE(fx.shell->top()->isHidden());
	CHECK(fx.shell->top()->level() == 2);
	fx.tick(1);
	CHECK_FALSE(fx.wm->apt().level(1)->visible);
	CHECK(fx.wm->apt().level(2)->visible);
	// registered names belong to the screen: a screen of the same kind can be created again after it closed
	CHECK(fx.wm->hasCommand("T::Two.apt"));
	log.clear();
	fx.shell->pop();
	REQUIRE(fx.shell->screenCount() == 1);
	CHECK(log == std::vector<std::string>{ "shutdown Two.apt", "destroyed Two.apt", "init One.apt" });
	CHECK_FALSE(fx.wm->hasCommand("T::Two.apt"));
	CHECK(fx.wm->apt().level(2) == nullptr);
	CHECK(fx.wm->findAptMovieIndex("Two.apt") == -1);
	CHECK(fx.wm->apt().level(1)->visible); // shown again
	CHECK_FALSE(fx.shell->top()->isHidden());
	// popImmediate
	fx.shell->popImmediate();
	CHECK(fx.shell->screenCount() == 0);
	CHECK(fx.wm->apt().level(1) == nullptr);
	CHECK(fx.shell->errors().empty());
}

TEST_CASE("Shell: a file with no factory is an error, not an empty screen; a full stack refuses the push; findScreenByFilename ignores case")
{
	std::vector<std::string> log; // outlives the screens the fixture destroys
	ShellFx fx;
	fx.wm->init();
	fx.shell->push("Nothing.apt");
	CHECK(fx.shell->screenCount() == 0);
	REQUIRE(fx.shell->errors().size() == 1);
	CHECK(fx.shell->errors()[0].find("Nothing.apt") != std::string::npos);
	// the stack is 16 deep (MAX_SHELL_STACK)
	for (int i = 0; i < 11; ++i) // 12 slots minus AptLevel0 = 11 movies fit in levels
	{
		const std::string f = "S" + std::to_string(i) + ".apt";
		registerTestScreen(fx.factories, f, &log);
		fx.addMovie("S" + std::to_string(i), {});
		fx.shell->push(f);
	}
	CHECK(fx.shell->screenCount() == 11);
	CHECK(fx.shell->findScreenByFilename("s3.APT") != nullptr);
	CHECK(fx.shell->findScreenByFilename("zzz.apt") == nullptr);
	// a 12th screen has no level left: the window manager refuses and the shell reports it
	registerTestScreen(fx.factories, "Late.apt", &log);
	fx.shell->push("Late.apt");
	CHECK(fx.shell->screenCount() == 11);
	CHECK(fx.shell->errors().size() == 2);
	fx.shell->clear();
	CHECK(fx.shell->screenCount() == 0);
}

TEST_CASE("Shell: a screen asking for a pop through the window manager is popped at the start of the next update")
{
	std::vector<std::string> log; // outlives the screens the fixture destroys
	ShellFx fx;
	registerTestScreen(fx.factories, "One.apt", &log);
	fx.addMovie("One", {});
	fx.wm->init();
	fx.shell->push("One.apt");
	fx.tick(1);
	fx.wm->requestShellPop();
	CHECK(fx.shell->screenCount() == 1); // not before the update
	fx.tick(1);
	CHECK(fx.shell->screenCount() == 0);
}

TEST_CASE("Shell: the A4 factory table holds MainMenu, Skirmish, LoadScreen, Options, GuiFX, Background and AptLevel0; AptLevel0 adopts slot 0")
{
	ShellFx fx;
	registerAptScreenFactories(fx.factories);
	std::set<std::string> have;
	for (const std::string &f : fx.factories.filenames())
	{
		have.insert(f);
	}
	CHECK(have == std::set<std::string>{ "MainMenu.apt", "Skirmish.apt", "LoadScreen.apt", "Options.apt", "GuiFX.apt", "Background.apt", "AptLevel0.apt", "Palantir.apt",
		"TimeLine.apt", "QuitMenu.apt", // lane END-1: the score screen; lane END-2: the quit menu
		"SaveLoad.apt", "LanLobby.apt", // lane MP-2: the replay page, the LAN lobby
		"PlayerTribute.apt", // lane PLAY-1: the Palantir flag's screen
		"CreateAHero.apt" }); // lane CAH-1: the Create-a-Hero builder
	CHECK(fx.factories.has("mainmenu.APT"));
	fx.wm->init();
	fx.shell->push("AptLevel0.apt");
	REQUIRE(fx.shell->screenCount() == 1);
	CHECK(fx.shell->top()->level() == 0);
	fx.tick(1);
	fx.shell->pop(); // the base movie stays
	CHECK(fx.shell->screenCount() == 0);
	CHECK(fx.wm->apt().level(0) != nullptr);
	// GuiFX asks for slot 11 (AptGuiFXRegisterCallbacks.cpp)
	fx.addMovie("GuiFX", {});
	fx.shell->push("GuiFX.apt");
	REQUIRE(fx.shell->screenCount() == 1);
	CHECK(fx.shell->top()->level() == 11);
	CHECK(fx.wm->hasCommand("AptGuiFX::OnInitialized"));
	CHECK(fx.wm->hasComponent("ToolTipText"));
}

// ============================================================================================================================
// Components (native placeholders)
// ============================================================================================================================

TEST_CASE("components: an instance of a registered symbol reaches its factory with its stage bounds and the InitGadgets name; destroying it is reported")
{
	TestMovie lib;
	std::uint32_t shape = lib.addShape(0, 0, 40, 20, 1);
	lib.addCharacter(0);
	std::uint32_t libShape = lib.addCharacter(shape);
	std::uint32_t sprite = lib.addSprite({ { lib.addPlaceItem(placeChar(libShape, 1)) } });
	std::uint32_t libSprite = lib.addCharacter(sprite);
	lib.addExport("Gadget", libSprite);
	TestMovie user;
	user.addCharacter(0);
	user.addCharacter(0);
	user.addImport("Lib", "Gadget", 1);
	// a plain gadget at depth 1 and one inside a `~7` row clip at depth 2
	TestMovie::Place p = placeChar(1, 1, "plain");
	p.flags |= APT_PLACE_HASMATRIX;
	p.translation[0] = 7;
	p.translation[1] = 8;
	std::uint32_t rowSprite = user.addSprite({ { user.addPlaceItem(placeChar(1, 1, "Player")) } });
	std::uint32_t rowId = user.addCharacter(rowSprite);
	user.setRootFrames({ { user.addPlaceItem(p), user.addPlaceItem(placeChar(rowId, 2, "~7")) } });
	ShellFx fx;
	fx.source.add("Lib", lib);
	fx.source.add("User", user);
	WindowManager &wm = *fx.wm;
	wm.init();
	std::vector<AptComponentRequest> seen;
	std::vector<std::string> paths;
	wm.registerComponent("Gadget", [&](AptComponentRequest &r) -> std::shared_ptr<GameWindow> {
		seen.push_back(AptComponentRequest{ r.manager, r.instance });
		AptComponentRequest &c = seen.back();
		c.level = r.level;
		c.movie = r.movie;
		c.symbol = r.symbol;
		c.instancePath = r.instancePath;
		c.instanceName = r.instanceName;
		c.x0 = r.x0;
		c.y0 = r.y0;
		c.x1 = r.x1;
		c.y1 = r.y1;
		return nullptr;
	});
	wm.loadAptWindow("Apt\\", "User.apt", true, 0, -1);
	fx.tick(1);
	REQUIRE(wm.errors().empty());
	REQUIRE(seen.size() == 2);
	CHECK(seen[0].movie == "Lib");
	CHECK(seen[0].symbol == "Gadget");
	CHECK(seen[0].level == 1);
	CHECK(seen[0].instancePath == "_level1.plain");
	CHECK(seen[0].instanceName == "plain");
	CHECK(seen[0].x0 == doctest::Approx(7.0f));
	CHECK(seen[0].y0 == doctest::Approx(8.0f));
	CHECK(seen[0].x1 == doctest::Approx(47.0f));
	CHECK(seen[0].y1 == doctest::Approx(28.0f));
	CHECK(seen[1].instancePath == "_level1.~7.Player");
	CHECK(seen[1].instanceName == "7/Player"); // [S-170]: the slot number first, the leaf after the last '/'
	CHECK(wm.components().size() == 2);
	// one record per instance, not one per update
	fx.tick(3);
	CHECK(seen.size() == 2);
	// unloading the window destroys the instances: their records go
	wm.unloadAptWindow(1);
	CHECK(wm.components().empty());
}

// ============================================================================================================================
// Binary facts: the registered names are the binary's strings
// ============================================================================================================================

TEST_CASE("binary: AptMainMenu registers exactly the 22 AptMainMenu:: names of RotWK game.dat (21 commands plus the RenderCredits component)")
{
	std::vector<unsigned char> image;
	if (!readGameDat(image))
	{
		retailtest::printSkip("binary names of AptMainMenu (RW_GAME_DAT / ROTWK_INSTALL unset)");
		return;
	}
	const std::set<std::string> binary = binaryStrings(image, "AptMainMenu::");
	const std::set<std::string> ours(AptMainMenu::retailNames().begin(), AptMainMenu::retailNames().end());
	CHECK(binary.size() == 22);
	CHECK(ours == binary);
	CHECK(AptMainMenu::retailNames().size() == 22);
}

TEST_CASE("binary: AptSkirmish registers exactly the 16 AptSkirmish:: names of RotWK game.dat")
{
	std::vector<unsigned char> image;
	if (!readGameDat(image))
	{
		retailtest::printSkip("binary names of AptSkirmish (RW_GAME_DAT / ROTWK_INSTALL unset)");
		return;
	}
	const std::set<std::string> binary = binaryStrings(image, "AptSkirmish::");
	const std::set<std::string> ours(AptSkirmish::retailNames().begin(), AptSkirmish::retailNames().end());
	CHECK(binary.size() == 16);
	CHECK(ours == binary);
}

// ============================================================================================================================
// Retail: the real main menu through the window manager and the shell
// ============================================================================================================================

namespace
{

struct RetailShell
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<Shell> shell;

	explicit RetailShell(AptRetail &mount) : source(mount.fs)
	{
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->init();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
	AptCharacterInst *at(int level, const std::string &path) { return wm->apt().resolvePath(wm->apt().level(level), path); }

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
	void click(AptButtonInst &b)
	{
		float x0, y0, x1, y1;
		REQUIRE(b.contentBounds(x0, y0, x1, y1));
		float x, y;
		b.globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		REQUIRE_MESSAGE(wm->apt().input().hitTestButtons(x, y) == &b, "the centre of the hit bounds is not inside the hit mesh of " << b.targetPath());
		wm->apt().input().postMouseMove(x, y);
		tick(1);
		wm->apt().input().postMouseButton(true);
		tick(1);
		wm->apt().input().postMouseButton(false);
		tick(1);
	}
	// Opens `nav` and clicks its `item` (the click-through of the A3 menu test)
	void clickNavItem(int level, const std::string &nav, const std::string &item)
	{
		AptCharacterInst *navClip = at(level, nav);
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
		click(*open);
		AptButtonInst *itemButton = nullptr;
		for (int waited = 0; waited < 120 && !itemButton; ++waited)
		{
			itemButton = firstButton(at(level, nav + "." + item));
			if (!itemButton)
			{
				tick(1);
			}
		}
		REQUIRE_MESSAGE(itemButton, nav << "." << item);
		tick(20); // the reveal animation of the entry
		click(*itemButton);
		tick(2);
	}
};

} // namespace

TEST_CASE("retail shell: pushing MainMenu.apt registers the 22 AptMainMenu names and the movie runs: OnInitialized is handled, the nav clips exist")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	RetailShell fx(mount);
	fx.shell->push("MainMenu.apt");
	REQUIRE(fx.shell->errors().empty());
	REQUIRE(fx.shell->screenCount() == 1);
	AptScreen *menu = fx.shell->top();
	CHECK(menu->filename() == "MainMenu.apt");
	CHECK(menu->codePrefix() == "AptMainMenu");
	const int level = menu->level();
	CHECK(level == 1); // AptLevel0 holds slot 0
	// the registered names: every AptMainMenu:: name except RenderCredits is a command, RenderCredits is a component
	for (const std::string &name : AptMainMenu::retailNames())
	{
		if (name == "AptMainMenu::RenderCredits")
		{
			CHECK(fx.wm->hasComponent(name));
		}
		else
		{
			CHECK_MESSAGE(fx.wm->hasCommand(name), name);
		}
	}
	for (const char *provider : { "MainMenuUnlockBonusCampaign", "MainMenuLevel", "MainMenuContinueCampaign", "BlinkBattleSchoolOff" })
	{
		CHECK_MESSAGE(fx.wm->hasProvider(provider), provider);
	}
	fx.tick(10);
	CHECK(fx.wm->isAptWindowLoaded(0));
	CHECK(fx.wm->isAptWindowLoaded(level));
	// OnInitialized came from the movie and found its handler (the note is the handler's own report)
	CHECK(fx.wm->noteCount("command-unhandled") == 0);
	CHECK(fx.wm->noteCount("unported-command") >= 1);
	for (const char *nav : { "SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeroes", "QuitMainMenu" })
	{
		CHECK_MESSAGE(fx.at(level, nav), nav);
	}
	// the providers the shell does not wire are reported, not defaulted: a read is "no value" plus a note
	std::string value;
	CHECK(fx.wm->getExtern("MainMenuLevel", value) == AptExternResult::Undefined);
	CHECK(fx.wm->noteCount("provider-unwired") == 1);
	CHECK(fx.wm->noteCount("extern-read-unhandled") == 0);
	// destroying the screen frees its names and its level
	fx.shell->pop();
	CHECK_FALSE(fx.wm->hasCommand("AptMainMenu::Skirmish"));
	CHECK_FALSE(fx.wm->hasProvider("MainMenuLevel"));
	CHECK(fx.wm->apt().level(level) == nullptr);
	CHECK(fx.wm->apt().level(0) != nullptr);
}

TEST_CASE("retail shell: the main-menu click on Skirmish pushes Skirmish.apt into a level and AptSkirmish::OnInitialized fires exactly once")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	RetailShell fx(mount);
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	const int menuLevel = fx.shell->top()->level();
	fx.clickNavItem(menuLevel, "SoloPlayNav", "Skirmish");
	// the command was handled by AptMainMenu::Skirmish: Skirmish.apt is on the stack, MainMenu below it, hidden
	REQUIRE(fx.shell->errors().empty());
	REQUIRE(fx.shell->screenCount() == 2);
	AptScreen *skirmish = fx.shell->top();
	CHECK(skirmish->filename() == "Skirmish.apt");
	CHECK(skirmish->codePrefix() == "AptSkirmish");
	CHECK(skirmish->level() == 2);
	CHECK(fx.shell->screenAt(0)->isHidden());
	CHECK_FALSE(fx.wm->apt().level(menuLevel)->visible);
	const std::size_t initBefore = fx.wm->noteCount("unported-command");
	(void)initBefore;
	fx.tick(30);
	// the movie's own script errors are A6's business (the AptMapPreview / MpGameSetup providers); a movie that failed to load is not
	CHECK(fx.wm->noteCount("movie-load-failed") == 0);
	CHECK(fx.wm->isAptWindowLoaded(skirmish->level()));
	CHECK(fx.wm->apt().level(skirmish->level())->totalFrames() == 45);
	// OnInitialized: the screen counts what the movie sent
	auto *aptSkirmish = dynamic_cast<AptSkirmish *>(skirmish);
	REQUIRE(aptSkirmish);
	CHECK(aptSkirmish->initializedCount() == 1);
	// the movie also sends MpGameSetup::OnReadyPress: the lobby screen registers it (A6) and reports it as an unported command
	bool sawReady = false;
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		if (n.kind == "unported-command" && n.detail == "MpGameSetup::OnReadyPress")
		{
			sawReady = true;
		}
	}
	CHECK(sawReady);
	// clicking Skirmish again does not push a second one (ShowSkirmish.cpp: only when the singleton does not exist)
	fx.wm->invokeCallback("AptMainMenu::Skirmish", "");
	CHECK(fx.shell->screenCount() == 2);
}

// ============================================================================================================================
// Stops S-170..S-175 (pinned: the exact behaviour the port reports instead of guessing)
// ============================================================================================================================

TEST_CASE("stops S-173: the tooltip hover path is '<movie>/<clip path>' of the clip under the pointer; no registered callback -> TOOLTIP:<name>")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	RetailShell fx(mount);
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	AptCharacterInst *nav = fx.at(fx.shell->top()->level(), "SoloPlayNav");
	REQUIRE(nav);
	AptButtonInst *open = RetailShell::firstButton(nav);
	REQUIRE(open);
	float x0, y0, x1, y1;
	REQUIRE(open->contentBounds(x0, y0, x1, y1));
	float x, y;
	open->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	fx.wm->apt().input().postMouseMove(x, y);
	fx.tick(2);
	CHECK(fx.wm->hoverPath() == "MainMenu/SoloPlayNav");
	CHECK(fx.wm->currentTooltipLabel() == "TOOLTIP:MainMenu/SoloPlayNav");
	REQUIRE_FALSE(fx.services.tooltips.empty());
	CHECK(fx.services.tooltips.back() == "TOOLTIP:MainMenu/SoloPlayNav");
	// a registered tooltip callback of the hovered name wins
	fx.wm->apt().input().postMouseMove(1.0f, 1.0f);
	fx.tick(2);
	CHECK(fx.wm->hoverPath() != "MainMenu/SoloPlayNav");
	CHECK(fx.wm->currentTooltipLabel() != "TOOLTIP:MainMenu/SoloPlayNav"); // the tooltip follows the pointer
	fx.wm->registerTooltip("MainMenu/SoloPlayNav", [](const std::string &name) { return "CB:" + name; });
	fx.wm->apt().input().postMouseMove(x, y);
	fx.tick(2);
	CHECK(fx.wm->currentTooltipLabel() == "CB:MainMenu/SoloPlayNav");
}

TEST_CASE("stops S-174: ActionRandom has no client RNG behind it in the shell; the first use is reported")
{
	ShellFx fx;
	CHECK(fx.wm->noteCount("apt-random-unwired") == 0);
	const std::uint32_t a = fx.wm->random();
	const std::uint32_t b = fx.wm->random();
	CHECK(a != b);
	CHECK(fx.wm->noteCount("apt-random-unwired") == 1); // once, not per call
	// deterministic: the same seed gives the same stream
	ShellFx other;
	other.wm->seedRandom(1u);
	CHECK(other.wm->random() == a);
}

TEST_CASE("stops S-175: handlers with no decompiled body report 'unported-command'; every main-menu action is still a request the engine can see")
{
	ShellFx fx;
	fx.addMovie("MainMenu", {});
	registerAptScreenFactories(fx.factories);
	fx.wm->init();
	fx.shell->push("MainMenu.apt");
	fx.tick(1);
	const char *requests[] = { "LoadGame", "LoadReplay", "LAN", "OnlineButtonPressed", "BattleSchool", "LevelSelect", "LoadCampaign",
		"ContinueCampaign", "Expansion1Campaign", "BonusCampaign", "WarOfTheRing", "OnTutorial", "Credits",
		"CreditsExit", "StopGameMovie", "ResetResolution" };
	for (const char *r : requests)
	{
		CHECK_MESSAGE(fx.wm->invokeCallback(std::string("AptMainMenu::") + r, "arg"), r);
	}
	CHECK(fx.services.requests.size() == 16);
	CHECK(fx.wm->noteCount("unported-command") == 16);
	CHECK(fx.services.requests[0].action == ShellAction::LoadGame);
	CHECK(fx.services.requests[0].argument == "arg");
	// ExitGame is complete: a request, and the shell pops on the next update
	fx.wm->invokeCallback("AptMainMenu::ExitGame", "");
	CHECK(fx.services.countRequests(ShellAction::ExitGame) == 1);
	CHECK(fx.shell->screenCount() == 1);
	fx.tick(1);
	CHECK(fx.shell->screenCount() == 0);
	// global commands the binary registers whose bodies were not read
	for (const char *c : { "CloseWindow", "OnClickThroughPress", "OnClickThroughRelease", "EnableComponents", "DisableComponents" })
	{
		const std::size_t before = fx.wm->noteCount("unported-command");
		fx.wm->invokeCallback(c, "");
		CHECK_MESSAGE(fx.wm->noteCount("unported-command") == before + 1, c);
	}
	// the Options command pushes Options.apt; the advanced flag is the argument
	fx.addMovie("Options", {});
	fx.shell->push("MainMenu.apt");
	fx.tick(1);
	fx.wm->invokeCallback("AptMainMenu::Options", "true");
	REQUIRE(fx.shell->screenCount() == 2);
	CHECK(fx.shell->top()->filename() == "Options.apt");
}

TEST_CASE("stops S-171: an APT screen shuts down at once (hidden, loses focus); the shell reports a movie the manager refused")
{
	std::vector<std::string> log; // outlives the screens the fixture destroys
	ShellFx fx;
	registerTestScreen(fx.factories, "One.apt", &log);
	registerTestScreen(fx.factories, "Two.apt", &log);
	fx.addMovie("One", {});
	fx.addMovie("Two", {});
	fx.wm->init();
	fx.shell->push("One.apt");
	fx.tick(1);
	CHECK(fx.wm->focusedLevel() == 1);
	fx.shell->push("Two.apt");
	fx.tick(1);
	CHECK(fx.wm->focusedLevel() == 2); // One lost it when it was hidden, Two took it when it was initialised
	CHECK(fx.shell->screenAt(0)->isHidden());
	CHECK((fx.wm->aptWindow(1).flags & WindowManager::APTWIN_HIDDEN) != 0);
	CHECK((fx.wm->aptWindow(2).flags & WindowManager::APTWIN_HIDDEN) == 0);
}
