// OpenBFME unit tests. GPL-3.0.
// Lane FB7-1 round 2 (FB-0007 r2, the owner's menu reports): RotWK's Options screen controls (AptOptions InitGadgets / Save, RW 0x9205C4 / 0x91FC9C), the
// horizontal slider (RW 0x7237F8 / 0x4A1130), the main menu after a return from Options (the seek's same-placement test, BFME2 0x00AF9564), the
// front-end background movie (RW 0x6224C5 / 0x6230B6 / 0x622C88) and the shell's backdrop (Shell::showShellMap RW 0x75DE01, Shell::update RW 0x75E1D3).
// The expected values are the binary's arithmetic on the retail data and on the preferences the test writes. The retail tests SKIP loudly without the
// installs.

#include "doctest.h"
#include "AptRetail.h"

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "GameClient/GameLODManager.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/OptionPreferences.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace
{
struct ShellFx
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	OptionPreferences prefs;
	GameLODManager gameLOD; // lane PLAY-2: GameLOD.ini's presets (GodotAptPlayer's boot reads them the same way)
	GameTextTableSource text;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	std::string file;

	explicit ShellFx(AptRetail &mount, const std::string &prefsText) : source(mount.fs)
	{
		file = (std::filesystem::temp_directory_path() / "openbfme-fb7-options-test.ini").string();
		{
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out << prefsText;
		}
		REQUIRE(prefs.load(file));
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
		environment.options = &prefs;
		environment.optionsFile = file;
		environment.gameText = &text;
		{
			INIEnvironment env;
			env.fileSystem = &mount.fs;
			INIBlockRecorder recorder;
			RegisterRecordingBlockStubs(env.blocks, recorder, { "StaticGameLOD" }, StubExtent::Lenient);
			gameLOD.registerBlocks(env.blocks);
			INI ini(env);
			for (const std::string &f : GameLODManager::loadOrder())
			{
				ini.load(f, INI_LOAD_OVERWRITE);
			}
			environment.gameLOD = &gameLOD;
		}
		// the inputs the device gives (GodotAptPlayer boot_shell): AudioSettings-like defaults, two addresses, one mode
		const float defaults[5] = { 0.70f, 0.75f, 0.55f, 0.60f, 0.80f };
		for (int i = 0; i < 5; ++i)
		{
			environment.defaultVolumes[i] = defaults[i];
		}
		environment.haveDefaultVolumes = true;
		environment.keyboardDefaultScrollSpeedFactor = 0.5f;
		environment.haveScrollDefault = true;
		environment.localAddresses = { { "192.168.1.5", 0xC0A80105u }, { "10.0.0.7", 0x0A000007u } };
		environment.displayModes = { { 800, 600 }, { 1024, 768 } };
		environment.currentResolution = { 1024, 768 };
		registerAptScreenFactories(factories);
		REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
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
		std::remove(file.c_str());
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
	std::string fileText() const
	{
		std::ifstream in(file, std::ios::binary);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}
	AptOptionsScreen *openOptionsFromMainMenu()
	{
		tick(2);
		shell->push("MainMenu.apt");
		tick(5);
		// the main menu's GameCode('Options', false) (MainMenu.apt SettingsButton)
		REQUIRE(wm->invokeCallback("AptMainMenu::Options", "false"));
		tick(60);
		REQUIRE(shell->top());
		REQUIRE(shell->top()->filename() == "Options.apt");
		return dynamic_cast<AptOptionsScreen *>(shell->top());
	}
};

// the slider's thumb as RotWK places it: y 0, 13 wide and as tall as the track, x = (int)((position - min) * numTicks), numTicks = (width - 13) / range
void checkThumb(GameWindow *slider, int position)
{
	REQUIRE(slider);
	CHECK(GadgetSliderGetPosition(slider) == position);
	int w = 0, h = 0, mn = 0, mx = 0;
	slider->winGetSize(&w, &h);
	GadgetSliderGetMinMax(slider, &mn, &mx);
	GameWindow *thumb = GadgetSliderGetThumb(slider);
	REQUIRE(thumb);
	int tx = 0, ty = 0, tw = 0, th = 0;
	thumb->winGetPosition(&tx, &ty);
	thumb->winGetSize(&tw, &th);
	const float numTicks = (float)(w - 13) / (float)(mx - mn);
	CHECK(ty == 0);
	CHECK(tw == 13);
	CHECK(th == h);
	CHECK(tx == (int)((float)(position - mn) * numTicks));
}
} // namespace

TEST_CASE("fb7 options retail: InitGadgets shows the saved settings (sliders on their track row, boxes, address, port), the network panel off from the main menu, APT:VersionNum")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "AllHealthBars = yes\nAudioLOD = High\nBrightness = 60\nFirewallPortOverride = 9000\nGameSpyIPAddress = 10.0.0.7\nMusicVolume = 37.500000\n"
					  "SFXVolume = 80.000000\nScrollFactor = 25\nSendDelay = yes\nUseEAX3 = yes\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	// RW 0x6E5FB3 + ftol: Music 37.5 -> 37, SFX 80; Voice, Ambient, Movie take the AudioSettings defaults * 100
	checkThumb(options->gadget("MusicVolume"), 37);
	checkThumb(options->gadget("SoundFxVolume"), 80);
	checkThumb(options->gadget("VoiceVolume"), 75);
	checkThumb(options->gadget("AmbientVolume"), 60);
	checkThumb(options->gadget("MovieVolume"), 80);
	checkThumb(options->gadget("Brightness"), 60);
	checkThumb(options->gadget("ScrollSpeed"), 25); // ScrollFactor 25 * 0.02 * 50
	REQUIRE(options->gadget("HealthBars"));
	CHECK(GadgetCheckBoxIsChecked(options->gadget("HealthBars")));
	REQUIRE(options->gadget("EAX3"));
	CHECK(GadgetCheckBoxIsChecked(options->gadget("EAX3")));
	REQUIRE(options->gadget("HighAudioQuality"));
	CHECK(GadgetCheckBoxIsChecked(options->gadget("HighAudioQuality")));
	REQUIRE(options->gadget("SendDelay"));
	CHECK(GadgetCheckBoxIsChecked(options->gadget("SendDelay")));
	// the address list (item data the address) with GameSpyIPAddress selected; the port; both disabled (the main menu passes NetworkEnabled 0)
	GameWindow *ip = options->gadget("OnlineIp");
	REQUIRE(ip);
	CHECK(GadgetComboBoxGetLength(ip) == 2);
	int selected = -1;
	GadgetComboBoxGetSelectedPos(ip, &selected);
	CHECK(selected == 1);
	CHECK(GadgetComboBoxGetText(ip) == u"10.0.0.7");
	CHECK_FALSE(BitTest(ip->winGetStatus(), WIN_STATUS_ENABLED));
	GameWindow *port = options->gadget("OnlinePortNum");
	REQUIRE(port);
	CHECK(GadgetTextEntryGetText(port) == u"9000");
	CHECK_FALSE(BitTest(port->winGetStatus(), WIN_STATUS_ENABLED));
	// the resolution list: 1024 x 768 selected, enabled (AllowResolutionChange 1 from the main menu)
	GameWindow *res = options->gadget("Resolution");
	REQUIRE(res);
	CHECK(GadgetComboBoxGetText(res) == u"1024x768");
	CHECK(BitTest(res->winGetStatus(), WIN_STATUS_ENABLED));
	// the externs (RW 0x91F7F5) and the version text (Version:Format2 with 2 and 1)
	for (const char *name : { "MasterOption0Num", "MasterOption0Current", "MasterOption0ResetDefault", "AllowResolutionChange", "AllowAdvancedOptions", "NetworkEnabled",
			 "AdvancedOnly" })
	{
		CHECK(fx.wm->hasProvider(name));
	}
	const std::string *version = fx.wm->aptText("APT:VersionNum");
	REQUIRE(version);
	CHECK(*version == "Version 2.1");
}

TEST_CASE("fb7 options retail: Save writes RotWK's entries for the controls and closes; Cancel re-applies the saved volumes")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "Resolution = 1024 768\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	GadgetSliderSetPosition(options->gadget("MusicVolume"), 42);
	GadgetSliderSetPosition(options->gadget("ScrollSpeed"), 0); // saved as 1
	GadgetCheckBoxSetChecked(options->gadget("EAX3"), true);
	REQUIRE(fx.wm->invokeCallback("AptOptions::Save", ""));
	fx.tick(30);
	const std::string text = fx.fileText();
	CHECK(text.find("MusicVolume = 42.000000\n") != std::string::npos);
	CHECK(text.find("SFXVolume = 70.000000\n") != std::string::npos); // the default 0.70 * 100, unchanged
	CHECK(text.find("ScrollFactor = 1\n") != std::string::npos);
	CHECK(text.find("Brightness = 50\n") != std::string::npos); // RW 0x6E5ECD's default
	CHECK(text.find("UseEAX3 = yes\n") != std::string::npos);
	CHECK(text.find("AudioLOD = Low\n") != std::string::npos);
	CHECK(text.find("FirewallPortOverride = 0\n") != std::string::npos);
	CHECK(text.find("Resolution = 1024 768\n") != std::string::npos);
	bool musicApplied = false;
	for (const auto &o : fx.services.appliedOptions)
	{
		musicApplied = musicApplied || (o.first == "MusicVolume" && o.second == "42.000000");
	}
	CHECK(musicApplied);
	REQUIRE(fx.shell->top());
	CHECK(fx.shell->top()->filename() == "MainMenu.apt");
}

TEST_CASE("fb7 main menu retail: after Options and Cancel the nav buttons are undimmed and hittable (a seek back recreates a clip placed on another frame)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	fx.tick(2);
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	const int level = fx.shell->top()->level();
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(level, "ShowMainMenu", {}, nullptr, &error), error);
	fx.tick(90);
	auto navTextAlpha = [&]() {
		AptRenderList rl;
		fx.wm->apt().buildRenderList(rl);
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Text && c.path.find("SoloPlayNav.OpenButton") != std::string::npos)
			{
				return c.color.mul[3];
			}
		}
		return -1.0f;
	};
	const float before = navTextAlpha();
	REQUIRE(before > 0.99f);
	// MainMenu.apt SettingsButton: DisableAllButtons (the navs play _disable: OpenButton re-places its clips at 50% on frame 59), then Options
	REQUIRE_MESSAGE(fx.wm->invokeAS(level, "DisableAllButtons", {}, nullptr, &error), error);
	fx.tick(60);
	CHECK(navTextAlpha() < 0.6f);
	REQUIRE(fx.wm->invokeCallback("AptMainMenu::Options", "false"));
	fx.tick(60);
	REQUIRE(fx.shell->top()->filename() == "Options.apt");
	REQUIRE(fx.wm->invokeCallback("AptOptions::Cancel", ""));
	fx.tick(90);
	REQUIRE(fx.shell->top()->filename() == "MainMenu.apt");
	// OnFocus("1") -> ShowMainMenu -> RevealAllButtons: the navs seek back to _hide; OpenButton's depth-3 clip (frame 9, ratio 9 / 65536) replaces the frame-59
	// one (ratio 59 / 65536) instead of keeping its 50% colour transform
	CHECK(navTextAlpha() > 0.99f);
	for (const char *path : { "SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeroes", "QuitMainMenu" })
	{
		AptCharacterInst *c = fx.wm->apt().resolvePath(fx.wm->apt().level(level), path);
		REQUIRE(c);
		CHECK(c->visible);
	}
}

TEST_CASE("fb7 background retail: Background.apt in the first free slot, SetBackground drives it; the shell's backdrop is ShellMapLowLOD while GameData's ShellMapOn is No")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	REQUIRE(fx.wm->loadBackground());
	CHECK(fx.wm->backgroundLevel() == 1); // AptLevel0 holds slot 0
	fx.tick(3);
	AptSpriteInst *bg = fx.wm->apt().level(fx.wm->backgroundLevel());
	REQUIRE(bg);
	REQUIRE(fx.wm->invokeCallback("SetBackground", "fadein"));
	fx.tick(1);
	CHECK(fx.wm->backgroundMode() == 1);
	REQUIRE(fx.wm->invokeCallback("SetBackground", "off"));
	CHECK(fx.wm->backgroundMode() == 0);
	// GameData: RotWK 2.01 ships ShellMapOn = No
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(mount.fs.readFile("data/ini/gamedata.ini", bytes, &error), error);
	bool on = true;
	REQUIRE_MESSAGE(Shell::readShellMapOn(std::string(bytes.begin(), bytes.end()), on, &error), error);
	CHECK_FALSE(on);
	fx.environment.shellMapOn = on;
	fx.shell->showShellMap(true);
	CHECK(fx.shell->backdropImage().empty()); // Shell::update puts it up
	fx.tick(1);
	CHECK(fx.shell->backdropImage() == "ShellMapLowLOD");
	CHECK(fx.skins.images.findImageByName("ShellMapLowLOD") != nullptr);
	fx.shell->showShellMap(false);
	CHECK(fx.shell->backdropImage().empty());
}

TEST_CASE("fb7 shell: readShellMapOn reads the GameData block only and rejects a value that is not Yes / No")
{
	bool on = false;
	std::string error;
	CHECK(Shell::readShellMapOn("Weapon X\n ShellMapOn = No\nEnd\nGameData\n  ShellMapOn = Yes ; comment\nEnd\n", on, &error));
	CHECK(on);
	CHECK_FALSE(Shell::readShellMapOn("GameData\n  ShellMapOn = Maybe\nEnd\n", on, &error));
	CHECK_FALSE(Shell::readShellMapOn("GameData\nEnd\n", on, &error));
}

TEST_CASE("fb7 main menu retail: the RenderImage clip `Image` shows the mapped image LogoWithShadow the screen's ctor binds to it (RW 0x91CA5C, RW 0x6236F6)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	fx.tick(2);
	fx.shell->push("MainMenu.apt");
	fx.tick(5);
	const std::string *image = fx.wm->aptImage("Image");
	REQUIRE(image != nullptr);
	CHECK(*image == "LogoWithShadow");
	CHECK(fx.skins.images.findImageByName("LogoWithShadow") != nullptr);
	// the movie's clip: tagged RenderImage, no `_imageMap` (the device keys the record by the instance name)
	AptRenderList rl;
	fx.wm->apt().buildRenderList(rl);
	int found = 0;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Placeholder && c.path.size() > 6 && c.path.compare(c.path.size() - 6, 6, ".Image") == 0)
		{
			++found;
			CHECK(c.symbolName == "RenderImage");
			for (const auto &kv : c.nativeVars)
			{
				CHECK(kv.first != "_imageMap");
			}
		}
	}
	CHECK(found == 1);
}

TEST_CASE("cah2 main menu retail: a request with no screen (War of the Ring) shows the Ok box and its Ok undims the nav buttons (no soft-lock, S-1914)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	fx.tick(2);
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	const int level = fx.shell->top()->level();
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(level, "ShowMainMenu", {}, nullptr, &error), error);
	fx.tick(90);
	auto navTextAlpha = [&]() {
		AptRenderList rl;
		fx.wm->apt().buildRenderList(rl);
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Text && c.path.find("SoloPlayNav.OpenButton") != std::string::npos)
			{
				return c.color.mul[3];
			}
		}
		return -1.0f;
	};
	REQUIRE(navTextAlpha() > 0.99f);
	// MainMenu.apt WarOfTheRingButton: DisableAllButtons, then GameCode('WarOfTheRing') (a request the host opens no screen for)
	REQUIRE_MESSAGE(fx.wm->invokeAS(level, "DisableAllButtons", {}, nullptr, &error), error);
	REQUIRE(fx.wm->invokeCallback("AptMainMenu::WarOfTheRing", ""));
	fx.tick(60);
	CHECK(navTextAlpha() < 0.6f);
	auto *menu = dynamic_cast<AptMainMenu *>(fx.shell->top());
	REQUIRE(menu);
	CHECK(AptMainMenu::unavailableScreenName("StopGameMovie").empty());
	CHECK(menu->screenUnavailable("StopGameMovie").empty());
	const std::string line = menu->screenUnavailable("WarOfTheRing");
	CHECK(line.rfind("[S-1914] War of the Ring", 0) == 0);
	REQUIRE(menu->unavailableBox());
	const int boxLevel = menu->unavailableBox()->level();
	REQUIRE(boxLevel >= 0);
	fx.tick(10);
	const std::vector<std::string> calls = menu->unavailableBox()->calls();
	REQUIRE_FALSE(calls.empty());
	CHECK(calls.front() == "Show Ok false");
	CHECK(menu->unavailableBox()->text() == u"War of the Ring is not available in this build yet.");
	// the box's Ok (GuiFX.apt's button sends "_level<n>.MessageBox_OnButtonOk"): the box goes, the menu gets OnFocus("1") -> ShowMainMenu
	REQUIRE(fx.wm->invokeCallback("_level" + std::to_string(boxLevel) + ".MessageBox_OnButtonOk", ""));
	fx.tick(90);
	CHECK(menu->unavailableBox() == nullptr);
	CHECK(fx.shell->top()->filename() == "MainMenu.apt");
	CHECK(navTextAlpha() > 0.99f);
	// a second press works the same (GuiFX.apt was released with the box and loads again)
	CHECK_FALSE(menu->screenUnavailable("LoadGame").empty());
	REQUIRE(menu->unavailableBox());
	CHECK(menu->unavailableBox()->level() >= 0);
}

// Lane WINCRASH-1: the owner's Windows crash (2026-10-09, e204772c): Options open, then AptOptionsScreen::save -> GadgetSliderGetPosition on a slider
// whose window the port had destroyed with its placeholder clip (the Advanced page removes the basic page's clips; RotWK keeps the windows,
// RW 0x8142D2 / 0x814BA9). Accept / Cancel run CloseOptions: Main plays _close, then its callWhenDone sends GameCode('Save') / ('Cancel'); every
// gadget window must live until then (clickOptionsButton checks it on every frame). The test takes the player's whole visit with real mouse clicks:
// open from the main menu, wait, move every slider, toggle every box, Accept, back on the main menu, open again (the values read back), Cancel,
// open once more and Accept unchanged.
namespace
{
AptButtonInst *firstButtonUnder(AptCharacterInst *c)
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
			if (AptButtonInst *b = firstButtonUnder(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

// the visible clip whose target path ends with ".<name>" (Options.apt: Main.Buttons.Accept ...)
AptCharacterInst *findClip(AptCharacterInst *c, const std::string &name)
{
	if (!c)
	{
		return nullptr;
	}
	const std::string path = c->targetPath();
	if (path.size() > name.size() && path.compare(path.size() - name.size() - 1, std::string::npos, "." + name) == 0 && c->globallyVisible())
	{
		return c;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptCharacterInst *f = findClip(k, name))
			{
				return f;
			}
		}
	}
	return nullptr;
}

// clicks the Options.apt button `name` (Accept, Cancel, Done) as the pointer does, then waits until the screen is gone; while the screen is up
// (its close animation plays, then AptOptions::Save / Cancel runs) every gadget window InitGadgets gave it must stay alive (the e204772c crash:
// Save read a slider whose window was gone)
void clickOptionsButton(ShellFx &fx, const std::string &name)
{
	auto *screen = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
	REQUIRE(screen);
	std::vector<std::pair<std::string, GameWindow *>> given;
	for (const char *g : { "SoundFxVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume", "Brightness", "ScrollSpeed", "HealthBars", "EAX3",
			 "HighAudioQuality", "SendDelay", "Resolution", "Detail", "OnlineIp", "OnlinePortNum" })
	{
		REQUIRE_MESSAGE(screen->gadget(g), g);
		given.emplace_back(g, screen->gadget(g));
	}
	auto allAlive = [&]() {
		for (const auto &g : given)
		{
			if (!fx.layer->gadgets().winIsAlive(g.second))
			{
				FAIL_CHECK("the gadget window of Options::" << g.first << " was destroyed while Options.apt is up (" << name << ")");
				return false;
			}
		}
		return true;
	};
	REQUIRE(fx.shell->top());
	REQUIRE(fx.shell->top()->filename() == "Options.apt");
	AptButtonInst *b = firstButtonUnder(findClip(fx.wm->apt().level(fx.shell->top()->level()), name));
	REQUIRE_MESSAGE(b, "no visible button " << name << " on Options.apt");
	float x0, y0, x1, y1;
	REQUIRE(b->contentBounds(x0, y0, x1, y1));
	float x, y;
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	fx.wm->postMouseMove(x, y);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	for (int i = 0; i < 300 && fx.shell->top() && fx.shell->top()->filename() == "Options.apt" && allAlive(); ++i)
	{
		fx.tick(1);
	}
	fx.tick(30);
	REQUIRE(fx.shell->top());
	CHECK_MESSAGE(fx.shell->top()->filename() == "MainMenu.apt", name << " did not close Options");
}
} // namespace

TEST_CASE("wincrash1 options retail: a player's visit with real clicks (sliders moved, boxes toggled, Accept, back, reopen, Cancel, reopen, Accept) keeps every value")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "Brightness = 60\nScrollFactor = 25\nMusicVolume = 37.500000\n");
	const char *sliders[7] = { "SoundFxVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume", "Brightness", "ScrollSpeed" };
	const int moved[7] = { 11, 22, 33, 44, 55, 66, 77 };
	const char *boxes[3] = { "HealthBars", "EAX3", "HighAudioQuality" };
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	fx.tick(300); // ten seconds on the screen
	bool was[3] = {};
	for (int i = 0; i < 7; ++i)
	{
		REQUIRE_MESSAGE(options->gadget(sliders[i]), sliders[i]);
		GadgetSliderSetPosition(options->gadget(sliders[i]), moved[i]);
		fx.tick(3);
	}
	for (int i = 0; i < 3; ++i)
	{
		REQUIRE_MESSAGE(options->gadget(boxes[i]), boxes[i]);
		was[i] = GadgetCheckBoxIsChecked(options->gadget(boxes[i]));
		GadgetCheckBoxToggle(options->gadget(boxes[i]));
		fx.tick(3);
	}
	clickOptionsButton(fx, "Accept");
	const std::string text = fx.fileText();
	CHECK(text.find("SFXVolume = 11.000000\n") != std::string::npos);
	CHECK(text.find("VoiceVolume = 22.000000\n") != std::string::npos);
	CHECK(text.find("MusicVolume = 33.000000\n") != std::string::npos);
	CHECK(text.find("AmbientVolume = 44.000000\n") != std::string::npos);
	CHECK(text.find("MovieVolume = 55.000000\n") != std::string::npos);
	CHECK(text.find("Brightness = 66\n") != std::string::npos);
	CHECK(text.find("ScrollFactor = 77\n") != std::string::npos);
	CHECK(text.find(std::string("AllHealthBars = ") + (was[0] ? "no" : "yes") + "\n") != std::string::npos);
	CHECK(text.find(std::string("UseEAX3 = ") + (was[1] ? "no" : "yes") + "\n") != std::string::npos);
	CHECK(text.find(std::string("AudioLOD = ") + (was[2] ? "Low" : "High") + "\n") != std::string::npos);

	// open again: the saved values are on the controls; Cancel closes without writing
	REQUIRE(fx.wm->invokeCallback("AptMainMenu::Options", "false"));
	fx.tick(60);
	REQUIRE(fx.shell->top()->filename() == "Options.apt");
	options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
	REQUIRE(options);
	for (int i = 0; i < 7; ++i)
	{
		REQUIRE_MESSAGE(options->gadget(sliders[i]), sliders[i]);
		CHECK_MESSAGE(GadgetSliderGetPosition(options->gadget(sliders[i])) == moved[i], sliders[i]);
	}
	for (int i = 0; i < 3; ++i)
	{
		REQUIRE_MESSAGE(options->gadget(boxes[i]), boxes[i]);
		CHECK_MESSAGE(GadgetCheckBoxIsChecked(options->gadget(boxes[i])) == !was[i], boxes[i]);
	}
	GadgetSliderSetPosition(options->gadget("MusicVolume"), 90);
	clickOptionsButton(fx, "Cancel");
	CHECK(fx.fileText() == text);

	// and once more: Accept with nothing changed writes the same file
	REQUIRE(fx.wm->invokeCallback("AptMainMenu::Options", "false"));
	fx.tick(120);
	clickOptionsButton(fx, "Accept");
	CHECK(fx.fileText() == text);
}

TEST_CASE("wincrash1 options retail: mouse on every control (slider drags, box clicks, drop-downs, every button hovered) keeps the gadget windows alive")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "Brightness = 60\nScrollFactor = 25\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	const char *names[15] = { "SoundFxVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume", "Brightness", "ScrollSpeed", "HealthBars", "EAX3",
		"HighAudioQuality", "SendDelay", "Resolution", "Detail", "OnlineIp", "OnlinePortNum" };
	std::map<std::string, GameWindow *> given;
	for (const char *n : names)
	{
		REQUIRE_MESSAGE(options->gadget(n), n);
		given[n] = options->gadget(n);
	}
	std::string step = "open";
	auto check = [&]() {
		for (auto &g : given)
		{
			if (options->gadget(g.first) != g.second)
			{
				MESSAGE("after " << step << ": InitGadgets gave Options::" << g.first << " a new window");
				g.second = options->gadget(g.first);
			}
			CHECK_MESSAGE(fx.layer->gadgets().winIsAlive(g.second), "after " << step << ": the window of Options::" << g.first << " was destroyed");
		}
	};
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			fx.tick(1);
			check();
		}
	};
	auto centre = [&](GameWindow *w, float fx_, float &x, float &y) {
		int px = 0, py = 0, sw = 0, sh = 0;
		w->winGetScreenPosition(&px, &py);
		w->winGetSize(&sw, &sh);
		x = px + sw * fx_;
		y = py + sh / 2.0f;
	};
	for (const char *n : names)
	{
		GameWindow *w = options->gadget(n);
		if (!BitTest(w->winGetStatus(), WIN_STATUS_ENABLED))
		{
			continue;
		}
		step = std::string("a click on ") + n;
		float x, y;
		centre(w, 0.5f, x, y);
		fx.wm->postMouseMove(x, y);
		tick(2);
		fx.wm->postMouseButton(true);
		tick(2);
		if (std::string(n).find("Volume") != std::string::npos || std::string(n) == "Brightness" || std::string(n) == "ScrollSpeed")
		{
			step = std::string("a drag of ") + n;
			float x2, y2;
			centre(w, 0.15f, x2, y2);
			fx.wm->postMouseMove(x2, y2);
			tick(2);
		}
		fx.wm->postMouseButton(false);
		tick(30);
		// a drop-down or a popup (EAX3's "not supported" message) closes with a click outside / Escape
		fx.wm->postMouseMove(5, 5);
		tick(2);
		fx.wm->postMouseButton(true);
		tick(1);
		fx.wm->postMouseButton(false);
		tick(30);
	}
	// every visible button of the screen hovered
	std::vector<AptButtonInst *> buttons;
	std::function<void(AptCharacterInst *)> collect = [&](AptCharacterInst *c) {
		if (!c)
		{
			return;
		}
		if (AptButtonInst *b = c->asButton())
		{
			if (b->globallyVisible())
			{
				buttons.push_back(b);
			}
		}
		if (AptSpriteInst *sp = c->asSprite())
		{
			for (AptCharacterInst *k : sp->children())
			{
				collect(k);
			}
		}
	};
	collect(fx.wm->apt().level(fx.shell->top()->level()));
	const std::size_t count = buttons.size();
	CHECK(count > 0);
	for (std::size_t i = 0; i < count; ++i)
	{
		buttons.clear(); // collected again: a hover may re-place clips
		collect(fx.wm->apt().level(fx.shell->top()->level()));
		if (i >= buttons.size())
		{
			break;
		}
		AptButtonInst *b = buttons[i];
		float x0, y0, x1, y1, x, y;
		if (!b->contentBounds(x0, y0, x1, y1))
		{
			continue;
		}
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		step = "hovering " + b->targetPath();
		fx.wm->postMouseMove(x, y);
		tick(15);
	}
	// every other button clicked (Default: AptOptions::Reset), then a minute at 144 frames per second (7 ms updates)
	for (std::size_t i = 0; i < count; ++i)
	{
		buttons.clear();
		collect(fx.wm->apt().level(fx.shell->top()->level()));
		if (i >= buttons.size())
		{
			break;
		}
		AptButtonInst *b = buttons[i];
		const std::string path = b->targetPath();
		if (path.find(".Accept") != std::string::npos || path.find(".Cancel") != std::string::npos)
		{
			continue;
		}
		float x0, y0, x1, y1, x, y;
		if (!b->contentBounds(x0, y0, x1, y1))
		{
			continue;
		}
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		step = "a click on " + path;
		fx.wm->postMouseMove(x, y);
		tick(2);
		fx.wm->postMouseButton(true);
		tick(2);
		fx.wm->postMouseButton(false);
		tick(60);
		REQUIRE(fx.shell->top()->filename() == "Options.apt");
	}
	step = "a minute at 7 ms updates";
	for (int i = 0; i < 60 * 144; ++i)
	{
		fx.wm->update(7);
		if (i % 144 == 0)
		{
			check();
		}
	}
	// the advanced page's Done (CloseAdvancedSettings + GameCode('Save'): back on the basic page, lane PLAY-2), then the basic page's Accept
	if (AptButtonInst *done = firstButtonUnder(findClip(fx.wm->apt().level(fx.shell->top()->level()), "Done")))
	{
		step = "Done";
		float x0, y0, x1, y1, x, y;
		REQUIRE(done->contentBounds(x0, y0, x1, y1));
		done->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		fx.wm->postMouseMove(x, y);
		tick(2);
		fx.wm->postMouseButton(true);
		tick(2);
		fx.wm->postMouseButton(false);
		tick(90);
		REQUIRE(fx.shell->top()->filename() == "Options.apt");
	}
	step = "Accept";
	clickOptionsButton(fx, step);
}

// the owner's crash path (e204772c, 2026-10-09): Options from the main menu, Advanced (Main plays its advanced page: the basic page's placeholder clips
// are removed), Done. The gadget windows stay with the screen as RotWK's window table keeps them (RW 0x8142D2 / 0x814BA9). Lane PLAY-2: with
// EnterAdvancedSettings ported (state 2) Done saves the advanced page only and returns to the basic page (RW 0x91FCB2 / 0x9205AA); Accept there
// writes the values the player set.
TEST_CASE("wincrash1 options retail: Advanced, Done (back on the basic page), then Accept saves the basic page's values (the owner's crash path)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "Brightness = 60\nScrollFactor = 25\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	GadgetSliderSetPosition(options->gadget("Brightness"), 41);
	GadgetSliderSetPosition(options->gadget("MusicVolume"), 12);
	fx.tick(3);
	AptButtonInst *advanced = firstButtonUnder(findClip(fx.wm->apt().level(fx.shell->top()->level()), "Advanced"));
	REQUIRE_MESSAGE(advanced, "no Advanced button on Options.apt");
	float x0, y0, x1, y1, x, y;
	REQUIRE(advanced->contentBounds(x0, y0, x1, y1));
	advanced->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	fx.wm->postMouseMove(x, y);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	fx.tick(90);
	REQUIRE(fx.shell->top()->filename() == "Options.apt");
	// the advanced page is up: the basic page's clips are gone, their gadget windows are not
	CHECK(findClip(fx.wm->apt().level(fx.shell->top()->level()), "Done"));
	CHECK(fx.layer->gadgets().winIsAlive(options->gadget("Brightness")));
	// lane PLAY-2: RotWK's Done saves the advanced page (state 2: the basic page's entries are not written, RW 0x91FCB2) and goes back to the basic
	// page (RW 0x9205AA); the basic values are written by Accept there. Every gadget window lives on through both pages
	AptButtonInst *done = firstButtonUnder(findClip(fx.wm->apt().level(fx.shell->top()->level()), "Done"));
	REQUIRE(done);
	REQUIRE(done->contentBounds(x0, y0, x1, y1));
	done->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	fx.wm->postMouseMove(x, y);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	fx.tick(90);
	REQUIRE(fx.shell->top()->filename() == "Options.apt");
	CHECK(options->pageState() == 1);
	CHECK(fx.fileText().find("Brightness = 41\n") == std::string::npos);
	// the basic page's placeholders are placed again: new gadget windows (a new placement under the same name replaces the record, RW 0x8142D2) that
	// InitGadgets fills from Options.ini, as RotWK's does
	REQUIRE(options->gadget("Brightness"));
	CHECK(fx.layer->gadgets().winIsAlive(options->gadget("Brightness")));
	checkThumb(options->gadget("Brightness"), 60);
	GadgetSliderSetPosition(options->gadget("Brightness"), 41);
	GadgetSliderSetPosition(options->gadget("MusicVolume"), 12);
	clickOptionsButton(fx, "Accept");
	const std::string text = fx.fileText();
	CHECK(text.find("Brightness = 41\n") != std::string::npos);
	CHECK(text.find("MusicVolume = 12.000000\n") != std::string::npos);
	CHECK(text.find("ScrollFactor = 25\n") != std::string::npos);
}

// Sol r1 probe (WINCRASH-1 round 2): a gadget whose placeholder clip goes is hidden, and a hidden window gives up the mouse: the capture
// (ZH's windowHiding called winCapture(NULL), which refuses while a captor exists) and a drag in progress. A drag on the Brightness thumb
// while the movie switches to its advanced page: afterwards neither the slider nor its thumb holds the mouse or gets a mouse message.
TEST_CASE("wincrash1 options retail: a detached slider gets no mouse input, a drag in progress included")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "Brightness = 60\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	GameWindow *slider = options->gadget("Brightness");
	REQUIRE(slider);
	GameWindow *thumb = GadgetSliderGetThumb(slider);
	REQUIRE(thumb);
	GameWindowManager &gwm = fx.layer->gadgets();
	int x = 0, y = 0, w = 0, h = 0;
	thumb->winGetScreenPosition(&x, &y);
	thumb->winGetSize(&w, &h);
	const float tx = x + w / 2.0f, ty = y + h / 2.0f;
	// the drag starts: the button goes down on the thumb and the pointer moves
	fx.wm->postMouseMove(tx, ty);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseMove(tx + 20, ty);
	fx.tick(2);
	auto holds = [&](GameWindow *g) { return g && (g == slider || g == thumb); };
	CHECK((holds(gwm.winGetCapture()) || holds(gwm.winGetGrabWindow()))); // the drag is the slider's
	// what Options.apt's Advanced button runs (DoButtonCallBack -> ShowAdvancedSettings): the basic page's clips go
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(options->level(), "ShowAdvancedSettings", {}, nullptr, &error), error);
	fx.tick(90);
	REQUIRE(gwm.winIsAlive(slider));
	CHECK(slider->winIsHidden());
	CHECK_FALSE(holds(gwm.winGetCapture()));
	CHECK_FALSE(holds(gwm.winGetGrabWindow()));
	CHECK_FALSE(holds(gwm.winGetFocus()));
	int messages = 0;
	std::string received;
	auto counter = [&](GameWindow *, std::uint32_t msg, WindowMsgData, WindowMsgData) {
		++messages;
		received += " " + std::to_string(msg);
		return MSG_HANDLED;
	};
	const GameWinInputFunc sliderInput = slider->winGetInputFunc(), thumbInput = thumb->winGetInputFunc();
	slider->winSetInputFunc(counter);
	thumb->winSetInputFunc(counter);
	// the rest of the drag, then a click where the slider was
	fx.wm->postMouseMove(tx + 40, ty);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	fx.tick(2);
	fx.wm->postMouseMove(tx, ty);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	fx.tick(2);
	CHECK_MESSAGE(messages == 0, "messages:" << received);
	slider->winSetInputFunc(sliderInput);
	thumb->winSetInputFunc(thumbInput);
}

// lane PLAY-2: the advanced page (AptSimpleScreens.h, GameLODManager.h). The movie's rows read the templates and the option labels the screen gives it;
// Done writes the custom settings and StaticGameLOD = Custom.

TEST_CASE("play2 options retail: GameLOD.ini's presets give the nine settings of each level (RW 0x601BBD) and the advanced labels are bound")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	for (int level = 0; level < 5; ++level)
	{
		CHECK(fx.gameLOD.hasLevel(level));
	}
	CHECK_FALSE(fx.gameLOD.hasLevel(GameLODManager::kCustomLevel)); // retail defines no Custom block
	// GameLOD.ini: VeryLow = ModelLOD Low, AnimationDetail VeryLow, EffectsLOD VeryLow, ShadowLOD Off, no normal map / distance textures, WaterLOD Low,
	// TextureReductionFactor 2, ShaderLOD Low, DecalLOD Off
	CHECK(AdvancedOptionPrefs::format(fx.gameLOD.presetSettings(0)) == "0,0,0,0,0,0,0,0,0");
	// High: High, High, High, High, normal map (2), High, factor 0 (2), High, High
	CHECK(AdvancedOptionPrefs::format(fx.gameLOD.presetSettings(3)) == "2,3,3,3,2,2,2,2,2");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	for (int i = 0; i < 9; ++i)
	{
		CHECK(fx.wm->hasProvider("AdvancedOption" + std::to_string(i) + "Num"));
	}
	CHECK_FALSE(fx.wm->hasProvider("AdvancedOption9Num")); // the movie asks for ten, RotWK registers nine (RW 0x9213EE)
	const std::string *model = fx.wm->aptText("APT:AdvancedOption0");
	REQUIRE(model);
	CHECK(*model == "Model Detail");
	const std::string *ultra = fx.wm->aptText("APT:AdvancedOption0_3");
	REQUIRE(ultra);
	CHECK(*ultra == "Ultra High");
	const std::string *custom = fx.wm->aptText("APT:MasterOption0_Custom");
	REQUIRE(custom);
	CHECK(custom->find("MISSING") == std::string::npos);
	bool stop = false;
	for (const auto &n : fx.wm->notes())
	{
		stop = stop || (n.kind == "options-advanced" && n.detail.rfind("[S-2481]", 0) == 0);
	}
	CHECK(stop); // the stop the advanced page reports
}

namespace
{
void clickButton(ShellFx &fx, AptButtonInst *b)
{
	REQUIRE(b);
	float x0, y0, x1, y1, x, y;
	REQUIRE(b->contentBounds(x0, y0, x1, y1));
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	fx.wm->postMouseMove(x, y);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
}

// the advanced rows as drawn: OptionNameText / CurrentOptionText of each row (`<row>.DescreetSliderContent3`), their labels resolved as the device does
std::map<std::string, std::pair<std::string, std::string>> advancedRows(ShellFx &fx)
{
	std::map<std::string, std::pair<std::string, std::string>> rows; // label -> (name text, value text)
	AptRenderList rl;
	fx.wm->apt().buildRenderList(rl);
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind != AptRenderCommand::Kind::Text || c.path.find("DescreetSliderContent3") == std::string::npos || c.text.size() < 2 || c.text[0] != '$')
		{
			continue;
		}
		std::string shown;
		REQUIRE_MESSAGE(fx.wm->aptTextShown("APT:" + c.text.substr(1), shown), c.text);
		if (c.variable == "OptionNameText")
		{
			rows[c.text.substr(1)].first = shown;
		}
		else if (c.variable == "CurrentOptionText")
		{
			rows[c.text.substr(1, c.text.find('_') - 1)].second = c.text.substr(c.text.find('_') + 1) + " " + shown;
		}
	}
	return rows;
}
} // namespace

TEST_CASE("play2 options retail: Advanced shows the nine option rows with their names and values; a choice makes the level Custom and Done saves it")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	CHECK(options->masterOption() == 3);
	fx.tick(3);
	clickButton(fx, firstButtonUnder(findClip(fx.wm->apt().level(fx.shell->top()->level()), "Advanced")));
	fx.tick(90);
	CHECK(options->pageState() == 2);
	CHECK(options->customTemplate() == "2,3,3,3,2,2,2,2,2"); // EnterAdvancedSettings: High's settings
	// the placeholders "Option Name" / "Current Option" are gone: every row names its option and shows High's value
	const auto rows = advancedRows(fx);
	const std::map<std::string, std::pair<std::string, std::string>> want = {
		{ "AdvancedOption0", { "Model Detail", "2 High" } }, { "AdvancedOption1", { "Animation Detail", "3 High" } },
		{ "AdvancedOption2", { "VFX Detail", "3 High" } },   { "AdvancedOption3", { "Shadows", "3 High" } },
		{ "AdvancedOption4", { "Terrain Detail", "2 High" } }, { "AdvancedOption5", { "Water Detail", "2 High" } },
		{ "AdvancedOption6", { "Texture Quality", "2 High" } }, { "AdvancedOption7", { "Shader Detail", "2 High" } },
		{ "AdvancedOption8", { "Decal Detail", "2 High" } },
	};
	CHECK(rows == want);
	// Model Detail's first choice (Low): the movie's SetCustom writes the template and the level Custom through the externs
	AptCharacterInst *level = fx.wm->apt().level(fx.shell->top()->level());
	AptCharacterInst *modelRow = findClip(level, "Main.instance8"); // the ModelLOD row (MyOption AdvancedOption0)
	REQUIRE(modelRow);
	clickButton(fx, firstButtonUnder(findClip(modelRow, "DescreetSliderButton30")));
	fx.tick(30);
	CHECK(options->masterOption() == GameLODManager::kCustomLevel);
	CHECK(options->customTemplate() == "0,3,3,3,2,2,2,2,2");
	// Done: the movie's CloseAdvancedSettings and GameCode('Save') on the advanced page (state 2): RW 0x9204F3 writes the level and the custom
	// settings and, without AdvancedOnly, the screen stays up on its basic page (state 1, RW 0x9205AA)
	clickButton(fx, firstButtonUnder(findClip(level, "Done")));
	fx.tick(60);
	REQUIRE(fx.shell->top());
	CHECK(fx.shell->top()->filename() == "Options.apt");
	CHECK(options->pageState() == 1);
	const std::string text = fx.fileText();
	CHECK(text.find("StaticGameLOD = Custom\n") != std::string::npos);
	CHECK(text.find("ModelLOD = Low\n") != std::string::npos);
	CHECK(text.find("AnimationLOD = High\n") != std::string::npos);
	CHECK(text.find("TerrainLOD = High\n") != std::string::npos);
	CHECK(text.find("DecalLOD = High\n") != std::string::npos);
}

TEST_CASE("play2 options retail: a level chosen in the Detail list is saved as StaticGameLOD and clears the custom settings; Custom opens the advanced page")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = Custom\nModelLOD = Low\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	CHECK(options->masterOption() == GameLODManager::kCustomLevel);
	GameWindow *detail = options->gadget("Detail");
	REQUIRE(detail);
	// item 3 is GUI:Low (data 1): the level becomes Low
	GadgetComboBoxSetSelectedPos(detail, 3); // the list's selection reaches the screen as GCM_SELECTED (GadgetComboBox.cpp)
	CHECK(options->masterOption() == 1);
	clickOptionsButton(fx, "Accept");
	const std::string text = fx.fileText();
	CHECK(text.find("StaticGameLOD = Low\n") != std::string::npos);
	CHECK(text.find("ModelLOD") == std::string::npos);
}
