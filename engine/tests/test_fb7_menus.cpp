// OpenBFME unit tests. GPL-3.0.
// Lane FB7-1 round 2 (FB-0007 r2, the owner's menu reports): RotWK's Options screen controls (AptOptions InitGadgets / Save, RW 0x9205C4 / 0x91FC9C), the
// horizontal slider (RW 0x7237F8 / 0x4A1130), the main menu after a return from Options (the seek's same-placement test, BFME2 0x00AF9564), the
// front-end background movie (RW 0x6224C5 / 0x6230B6 / 0x622C88) and the shell's backdrop (Shell::showShellMap RW 0x75DE01, Shell::update RW 0x75E1D3).
// The expected values are the binary's arithmetic on the retail data and on the preferences the test writes. The retail tests SKIP loudly without the
// installs.

#include "doctest.h"
#include "AptRetail.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "GameClient/GameLODManager.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptQuitMenu.h"
#include "GameClient/GUI/MenuNavigator.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/OptionPreferences.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <fstream>
#include <memory>
#include <set>
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
			gameLOD.init(prefs); // lane OPTS-2: the level in effect at boot (GodotAptPlayer: RW 0x602165 / RW 0x4465D8)
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
	CHECK(*version == "Version 2.01"); // lane MENUFIX-1: Version:Format2 is "Version %d.%02d" (RotWK 2.01); "Version 2.1" dropped the width
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
	// lane CAMPMENU-1: retail's Solo Play campaign buttons start the campaigns (the host's campaign flow), they never get the box; the BFME2 campaigns
	// (switched off) are refused by name
	CHECK(AptMainMenu::unavailableScreenName("Expansion1Campaign").empty());
	CHECK(AptMainMenu::unavailableScreenName("BonusCampaign").empty());
	CHECK(AptMainMenu::unavailableText("BFME2GoodCampaign") == "The BFME2 Good campaign is not available yet: OpenBFME does not have it yet.");
	CHECK(AptMainMenu::unavailableText("BFME2EvilCampaign") == "The BFME2 Evil campaign is not available yet: OpenBFME does not have it yet.");
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
	// lane MENUWORK-1: the box says why, in the player's words
	CHECK(menu->unavailableBox()->text() == u"War of the Ring is not available yet: its strategy map game is still being rebuilt.");
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
	// VFX Detail's first choice (VeryLow): the movie's SetCustom writes the template and the level Custom through the externs (lane OPTS-2: Model
	// Detail's row, which the device does not draw yet, is greyed and takes no click: S-3402)
	AptCharacterInst *level = fx.wm->apt().level(fx.shell->top()->level());
	AptCharacterInst *effectsRow = findClip(level, "Main.instance12"); // the EffectsLOD row (MyOption AdvancedOption2)
	REQUIRE(effectsRow);
	clickButton(fx, firstButtonUnder(findClip(effectsRow, "DescreetSliderButton30")));
	fx.tick(30);
	CHECK(options->masterOption() == GameLODManager::kCustomLevel);
	CHECK(options->customTemplate() == "2,3,0,3,2,2,2,2,2");
	// Done: the movie's CloseAdvancedSettings and GameCode('Save') on the advanced page (state 2): RW 0x9204F3 writes the level and the custom
	// settings and, without AdvancedOnly, the screen stays up on its basic page (state 1, RW 0x9205AA)
	clickButton(fx, firstButtonUnder(findClip(level, "Done")));
	fx.tick(60);
	REQUIRE(fx.shell->top());
	CHECK(fx.shell->top()->filename() == "Options.apt");
	CHECK(options->pageState() == 1);
	const std::string text = fx.fileText();
	CHECK(text.find("StaticGameLOD = Custom\n") != std::string::npos);
	CHECK(text.find("EffectsLOD = VeryLow\n") != std::string::npos);
	CHECK(text.find("ModelLOD = High\n") != std::string::npos);
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

// lane OPTS-2: the level chosen is applied (RW 0x6020B4 / 0x601C62) and the device told; the health bars box follows the detail (RW 0x91EB77 /
// 0x91EBD1)
TEST_CASE("opts2 options retail: Save applies the level the Detail list chose; VeryLow checks and disables the health bars box")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "AllHealthBars = no\nStaticGameLOD = High\n");
	REQUIRE(fx.gameLOD.staticLevel() == 3); // the fixture's boot (GameLODManager::init)
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	CHECK(options->masterOption() == 3); // + 0x310: the manager's level
	GameWindow *bars = options->gadget("HealthBars");
	REQUIRE(bars);
	CHECK_FALSE(GadgetCheckBoxIsChecked(bars));
	CHECK(bars->winGetEnabled());
	GameWindow *detail = options->gadget("Detail");
	REQUIRE(detail);
	// the list is GUI:UltraHigh .. GUI:VeryLow, GUI:Custom (item data 4 .. 0, 5): item 4 is VeryLow
	GadgetComboBoxSetSelectedPos(detail, 4);
	REQUIRE(options->masterOption() == 0);
	CHECK(GadgetCheckBoxIsChecked(bars)); // RW 0x91EBF3: checked and disabled
	CHECK_FALSE(bars->winGetEnabled());
	fx.services.appliedOptions.clear();
	clickOptionsButton(fx, "Accept");
	CHECK(fx.gameLOD.staticLevel() == 0);
	CHECK(fx.gameLOD.state().maxParticleCount == 500);
	CHECK(fx.gameLOD.state().allHealthBars);
	const std::string text = fx.fileText();
	CHECK(text.find("StaticGameLOD = VeryLow\n") != std::string::npos);
	CHECK(text.find("AllHealthBars = no\n") != std::string::npos); // the disabled box is not saved (the file keeps its value)
	bool told = false;
	for (const auto &o : fx.services.appliedOptions)
	{
		told = told || (o.first == "StaticGameLOD" && o.second == "VeryLow");
	}
	CHECK(told);
}

// lane OPTS-2 (S-3401): a device that cannot change the display mode opens the screen with AllowResolutionChange off: the list is disabled, as retail's
// lobby / in-game openings leave it, and the stop is noted
TEST_CASE("opts2 options retail: without a display mode change the Resolution list is disabled and S-3401 noted")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	fx.environment.resolutionChangeApplies = false;
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	GameWindow *res = options->gadget("Resolution");
	CHECK((!res || !res->winGetEnabled())); // the movie shows its "only from the main menu" text instead of the list (AllowResolutionChange "0")
	bool noted = false;
	for (const auto &n : fx.wm->notes())
	{
		noted = noted || (n.kind == "options-resolution" && n.detail.rfind("[S-3401]", 0) == 0);
	}
	CHECK(noted);
}

// lane OPTS-2 (S-3402, coordinator review): the advanced page greys the rows of the options the device does not draw (the text field showing
// "$AdvancedOption<i>" finds the row); EffectsLOD, TextureQualityLOD and DecalLOD stay usable; the movie / EAX / audio quality controls are disabled
// Lane OPTS-4: the Movie slider is live (the MOVIE submix slider, S-4510) and no longer disabled
TEST_CASE("opts2 options retail: the advanced rows the device cannot show are greyed; EAX3 and High Audio Quality are disabled")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	for (const char *name : { "EAX3", "HighAudioQuality" })
	{
		GameWindow *w = options->gadget(name);
		REQUIRE_MESSAGE(w, name);
		CHECK_FALSE_MESSAGE(w->winGetEnabled(), name);
	}
	CHECK(options->gadget("MusicVolume")->winGetEnabled());
	CHECK(options->gadget("MovieVolume")->winGetEnabled());
	REQUIRE(fx.wm->invokeCallback("AptOptions::EnterAdvancedSettings", ""));
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(options->level(), "ShowAdvancedSettings", {}, nullptr, &error), error);
	fx.tick(150);
	const std::set<int> want = { 0, 1, 3, 4, 5, 7 };
	CHECK(options->greyedRows() == want);
	// a greyed row takes no click: its buttons are disabled; an EffectsLOD button still is enabled
	int disabled = 0;
	std::vector<AptCharacterInst *> stack{ fx.wm->apt().level(options->level()) };
	while (!stack.empty())
	{
		AptCharacterInst *i = stack.back();
		stack.pop_back();
		if (i->asButton())
		{
			if (!i->enabled)
			{
				++disabled;
			}
		}
		if (AptSpriteInst *s = i->asSprite())
		{
			for (AptCharacterInst *c : s->children())
			{
				stack.push_back(c);
			}
		}
	}
	CHECK(disabled >= 6);
}

// ---- lane OPTS-3: the OpenBFME page of Options.apt (not retail: GUI/AptScreens/OpenBFMEOptionsPage.h) ----------------------------------------------------
namespace
{
void clickStage(ShellFx &fx, float x, float y)
{
	fx.wm->postMouseMove(x, y);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	fx.tick(2);
}

bool overlaps(const float a[4], const float b[4])
{
	return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
}

// every visible edit text of the level but the ones under `skip`, with its stage box
std::vector<std::pair<std::string, std::array<float, 4>>> visibleTexts(ShellFx &fx, int level, AptCharacterInst *skip)
{
	std::vector<std::pair<std::string, std::array<float, 4>>> out;
	std::vector<AptCharacterInst *> stack{ fx.wm->apt().level(level) };
	while (!stack.empty())
	{
		AptCharacterInst *i = stack.back();
		stack.pop_back();
		if (!i || i == skip || !i->globallyVisible())
		{
			continue;
		}
		float b[4];
		if (i->type() == AptCharacterInst::Type::EditText && !static_cast<AptTextInst *>(i)->text.empty() && WindowManager::stageBounds(*i, b))
		{
			out.push_back({ i->targetPath(), { b[0], b[1], b[2], b[3] } });
		}
		if (AptSpriteInst *s = i->asSprite())
		{
			for (AptCharacterInst *c : s->children())
			{
				stack.push_back(c);
			}
		}
	}
	return out;
}

std::string subtitleText(ShellFx &fx, int level)
{
	std::vector<AptCharacterInst *> stack{ fx.wm->apt().resolvePath(fx.wm->apt().level(level), "Main.SubTitle") };
	while (!stack.empty())
	{
		AptCharacterInst *i = stack.back();
		stack.pop_back();
		if (!i)
		{
			continue;
		}
		if (i->type() == AptCharacterInst::Type::EditText)
		{
			return static_cast<AptTextInst *>(i)->text;
		}
		if (AptSpriteInst *sp = i->asSprite())
		{
			for (AptCharacterInst *c : sp->children())
			{
				stack.push_back(c);
			}
		}
	}
	return "(no subtitle)";
}

OpenBFMEPageSpec samplePage()
{
	OpenBFMEPageSpec spec;
	spec.subtitle = u"OPENBFME SETTINGS";
	spec.tabsHeader = u"OpenBFME";
	spec.tabs = { u"Display", u"Graphics+", u"Camera" };
	spec.tab = 0;
	spec.panelHeader = u"Display";
	OpenBFMEPageRow mode;
	mode.kind = OpenBFMEPageRow::Kind::Choice;
	mode.key = "window_mode";
	mode.label = u"Window mode";
	mode.items = { { u"Windowed", true }, { u"Borderless window", true }, { u"Fullscreen", false } };
	mode.value = 0;
	OpenBFMEPageRow fps;
	fps.kind = OpenBFMEPageRow::Kind::Check;
	fps.key = "show_fps";
	fps.label = u"Show the frame rate (F11)";
	fps.help = u"The frame counter in the corner.";
	OpenBFMEPageRow scale;
	scale.kind = OpenBFMEPageRow::Kind::Slider;
	scale.key = "render_scale";
	scale.label = u"Render scale";
	scale.minValue = 50;
	scale.maxValue = 100;
	scale.value = 100;
	scale.valueText = u"100%";
	spec.rows = { mode, fps, scale };
	spec.note = u"Options of this rebuild.";
	return spec;
}
} // namespace

TEST_CASE("opts3 options retail: the OPENBFME button is a copy of the movie's CUSTOM SETTINGS button in the free corner above CANCEL, over no retail text")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	float box[4];
	CHECK_FALSE(options->entryBox(box)); // only once the device offers the page
	options->setExtensionAvailable(true);
	fx.tick(40);
	REQUIRE(options->entryBox(box));
	AptCharacterInst *entry = fx.wm->apt().resolvePath(fx.wm->apt().level(options->level()), "Main.FrameLeft.OpenBFMEEntry");
	REQUIRE(entry);
	CHECK(std::abs((box[0] + box[2]) / 2 - AptOptionsScreen::kEntryCentreX) < 2.0f);
	CHECK(std::abs((box[1] + box[3]) / 2 - AptOptionsScreen::kEntryCentreY) < 2.0f);
	CHECK((box[0] >= 0 && box[2] <= 1024 && box[1] >= 0 && box[3] <= 768));
	// its caption is OPENBFME, drawn by the copied button's own text field
	bool captioned = false;
	for (const auto &t : visibleTexts(fx, options->level(), nullptr))
	{
		captioned = captioned || t.first.find("OpenBFMEEntry") != std::string::npos;
	}
	CHECK(captioned);
	// no retail text, frame or bottom bar button under it
	for (const auto &t : visibleTexts(fx, options->level(), entry))
	{
		CHECK_FALSE_MESSAGE(overlaps(box, t.second.data()), "the OPENBFME button covers " << t.first);
	}
	for (const char *path : { "Main.FrameLeft", "Main.FrameMiddle", "Main.FrameRight", "Buttons.Cancel", "Buttons.Default", "Buttons.Accept" })
	{
		AptCharacterInst *c = fx.wm->apt().resolvePath(fx.wm->apt().level(options->level()), path);
		REQUIRE_MESSAGE(c, path);
		float b[4];
		REQUIRE(WindowManager::stageBounds(*c, b));
		if (std::string(path) == "Main.FrameLeft")
		{
			// FrameLeft's own frame art (the button is its child: the frame shapes are its other children)
			for (AptCharacterInst *k : c->asSprite()->children())
			{
				float kb[4];
				if (k != entry && k->globallyVisible() && WindowManager::stageBounds(*k, kb))
				{
					CHECK_FALSE_MESSAGE(overlaps(box, kb), "the OPENBFME button covers " << k->targetPath());
				}
			}
			continue;
		}
		CHECK_FALSE_MESSAGE(overlaps(box, b), "the OPENBFME button covers " << path);
	}
	// a click on it is the engine's: the page's Entry event, not the copied button's EnterAdvancedSettings
	clickStage(fx, (box[0] + box[2]) / 2, (box[1] + box[3]) / 2);
	fx.tick(30);
	std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Entry);
	CHECK(options->pageState() == 1);
	CHECK(fx.shell->top()->filename() == "Options.apt");
	// not on the advanced page
	REQUIRE(fx.wm->invokeCallback("AptOptions::EnterAdvancedSettings", ""));
	std::string error;
	REQUIRE_MESSAGE(fx.wm->invokeAS(options->level(), "ShowAdvancedSettings", {}, nullptr, &error), error);
	fx.tick(150);
	CHECK_FALSE(options->entryBox(box));
}

TEST_CASE("opts3 options retail: the OpenBFME page takes the frames' place with retail gadgets; its tabs, rows and the bottom bar are its own; back to the basic page")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\nScrollFactor = 50\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	options->setExtensionAvailable(true);
	fx.tick(40);
	const std::string subtitleBefore = subtitleText(fx, options->level());
	std::string error;
	REQUIRE_MESSAGE(options->openExtension(&error), error);
	OpenBFMEOptionsPage *page = options->extensionPage();
	REQUIRE(page);
	REQUIRE_MESSAGE(page->show(samplePage(), &error), error);
	fx.tick(10);
	Apt &apt = fx.wm->apt();
	AptSpriteInst *root = apt.level(options->level());
	for (const char *path : { "Main.FrameLeft", "Main.FrameMiddle", "Main.FrameRight" })
	{
		CHECK_FALSE_MESSAGE(apt.resolvePath(root, path)->globallyVisible(), path);
	}
	CHECK(options->gadget("MusicVolume")->winIsHidden()); // the frames' gadgets go with them
	CHECK(subtitleText(fx, options->level()) == "OPENBFME SETTINGS");
	// the rows are retail gadgets at the page's places, the draw list has the frames, the labels and the help line in Options.apt's fonts
	GameWindow *combo = page->gadget("window_mode");
	GameWindow *box = page->gadget("show_fps");
	GameWindow *slider = page->gadget("render_scale");
	REQUIRE(combo);
	REQUIRE(box);
	REQUIRE(slider);
	CHECK(BitTest(combo->winGetStyle(), GWS_COMBO_BOX));
	CHECK(BitTest(box->winGetStyle(), GWS_CHECK_BOX));
	CHECK(BitTest(slider->winGetStyle(), GWS_HORZ_SLIDER));
	CHECK(GadgetSliderGetPosition(slider) == 100);
	GadgetDrawList list;
	fx.layer->buildDrawList(list);
	int labels = 0, frames = 0;
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.window != "OpenBFMEPage")
		{
			continue;
		}
		if (c.kind == GadgetDrawCommand::Kind::Text)
		{
			CHECK(c.font.name == "Albertus MT");
			labels += c.text == u"Window mode" || c.text == u"Render scale" ? 1 : 0;
		}
		frames += c.kind == GadgetDrawCommand::Kind::OpenRect ? 1 : 0;
	}
	CHECK(labels == 2);
	CHECK(frames == 6); // the two frames (two rectangles each), the boxes under the combo and the slider
	// a choice and a box through the gadgets' messages; a greyed choice is refused
	REQUIRE_MESSAGE(page->choose("window_mode", 1, &error), error);
	REQUIRE_MESSAGE(page->choose("window_mode", 2, &error), error);
	REQUIRE_MESSAGE(page->choose("show_fps", 1, &error), error);
	REQUIRE_MESSAGE(page->choose("render_scale", 70, &error), error);
	std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 3);
	CHECK((events[0].key == "window_mode" && events[0].value == 1));
	CHECK((events[1].key == "show_fps" && events[1].value == 1));
	CHECK((events[2].key == "render_scale" && events[2].value == 70));
	int sel = -1;
	GadgetComboBoxGetSelectedPos(combo, &sel);
	CHECK(sel == 1);
	// the check box by a real click
	std::array<int, 4> r{};
	REQUIRE(page->rect("show_fps", r));
	clickStage(fx, (r[0] + r[2]) / 2.0f, (r[1] + r[3]) / 2.0f);
	events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK((events[0].key == "show_fps" && events[0].value == 0));
	// a tab by a click; the help line follows the pointed row
	REQUIRE(page->rect("tab:2", r));
	clickStage(fx, (r[0] + r[2]) / 2.0f, (r[1] + r[3]) / 2.0f);
	events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK((events[0].kind == OpenBFMEPageEvent::Kind::Tab && events[0].value == 2));
	REQUIRE(page->rect("show_fps", r));
	fx.wm->postMouseMove(r[2] + 40.0f, (r[1] + r[3]) / 2.0f);
	CHECK(page->helpText() == u"The frame counter in the corner.");
	fx.wm->postMouseMove(10, 10);
	CHECK(page->helpText() == u"Options of this rebuild.");
	// the bottom bar: a click is the page's (the movie's closing animation does not start), Options stays up on the page
	for (const char *name : { "Accept", "Default", "Cancel" })
	{
		AptButtonInst *b = firstButtonUnder(findClip(root, name));
		REQUIRE_MESSAGE(b, name);
		float x0, y0, x1, y1;
		REQUIRE(b->contentBounds(x0, y0, x1, y1));
		float x, y;
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		clickStage(fx, x, y);
		fx.tick(60);
		events = options->takeExtensionEvents();
		REQUIRE_MESSAGE(events.size() == 1, name);
		const OpenBFMEPageEvent::Kind want = std::string(name) == "Accept" ? OpenBFMEPageEvent::Kind::Accept
			: std::string(name) == "Default"                              ? OpenBFMEPageEvent::Kind::Reset
																		   : OpenBFMEPageEvent::Kind::Cancel;
		CHECK_MESSAGE(events[0].kind == want, name);
		REQUIRE(fx.shell->top());
		CHECK(fx.shell->top()->filename() == "Options.apt");
		CHECK(options->extensionOpen());
	}
	CHECK(options->commandCalls("AptOptions::Save") == 0);
	// back: the frames, the gadgets and the subtitle return; Options.ini's ScrollFactor (written by the page meanwhile) shows on the retail slider
	fx.prefs.set("ScrollFactor", "80");
	options->closeExtension();
	fx.tick(10);
	for (const char *path : { "Main.FrameLeft", "Main.FrameMiddle", "Main.FrameRight" })
	{
		CHECK_MESSAGE(apt.resolvePath(root, path)->globallyVisible(), path);
	}
	CHECK_FALSE(options->gadget("MusicVolume")->winIsHidden());
	CHECK(page->gadget("window_mode") == nullptr);
	CHECK(GadgetSliderGetPosition(options->gadget("ScrollSpeed")) == options->initialScrollSpeed()); // RW 0x920A0B's ftol: 79 for 80
	CHECK(options->initialScrollSpeed() >= 79);
	CHECK(subtitleText(fx, options->level()) == subtitleBefore);
	// the retail Accept closes Options again
	clickOptionsButton(fx, "Accept");
}

// lane OPTS-3, stop S-3403: the page and its button lean on Options.apt's clip names (Main.FrameLeft.Advanced, Main.FrameLeft / FrameMiddle /
// FrameRight); a movie without them (a mod's) gets no button and no page, and says why
TEST_CASE("opts3 stop S-3403: without the movie's CUSTOM SETTINGS button or a settings frame the OpenBFME button and page are refused and reported")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	Apt &apt = fx.wm->apt();
	AptSpriteInst *root = apt.level(options->level());
	AptCharacterInst *advanced = apt.resolvePath(root, "Main.FrameLeft.Advanced");
	REQUIRE(advanced);
	REQUIRE(apt.removeClipByTarget(root, AptValue::object(advanced)));
	options->setExtensionAvailable(true);
	fx.tick(10);
	float box[4];
	CHECK_FALSE(options->entryBox(box));
	bool noted = false;
	for (const auto &n : fx.wm->notes())
	{
		noted = noted || (n.kind == "options-openbfme-entry" && n.detail.rfind("[S-3403]", 0) == 0);
	}
	CHECK(noted);
	AptCharacterInst *right = apt.resolvePath(root, "Main.FrameRight");
	REQUIRE(right);
	REQUIRE(apt.removeClipByTarget(root, AptValue::object(right)));
	std::string error;
	CHECK_FALSE(options->openExtension(&error));
	CHECK(error.find("[S-3403]") == 0);
	CHECK(error.find("Main.FrameRight") != std::string::npos);
	CHECK_FALSE(options->extensionOpen());
}

// Sol review r1 (1): a retail edit not yet saved survives a visit of the OpenBFME page that changes nothing (CANCEL): Options' ACCEPT saves it
TEST_CASE("opts3 options retail: a pending retail edit survives the OpenBFME page's CANCEL and Options' ACCEPT saves it")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\nAllHealthBars = no\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	options->setExtensionAvailable(true);
	fx.tick(40);
	GameWindow *bars = options->gadget("HealthBars");
	REQUIRE(bars);
	REQUIRE(bars->winGetEnabled());
	REQUIRE_FALSE(GadgetCheckBoxIsChecked(bars));
	GadgetCheckBoxToggle(bars); // the player ticks Show All Health Bars (not saved yet)
	REQUIRE(GadgetCheckBoxIsChecked(bars));
	std::string error;
	REQUIRE_MESSAGE(options->openExtension(&error), error);
	REQUIRE_MESSAGE(options->extensionPage()->show(samplePage(), &error), error);
	fx.tick(10);
	options->closeExtension(); // CANCEL: the page wrote nothing
	fx.tick(10);
	CHECK(GadgetCheckBoxIsChecked(bars));
	clickOptionsButton(fx, "Accept");
	CHECK(fx.fileText().find("AllHealthBars = yes") != std::string::npos);
}

// Sol review r1 (2): the page's changes and the bottom bar's clicks come out in the order they happened (one FIFO), both ways round
TEST_CASE("opts3 options retail: a change and a bottom bar click in one interval keep their order")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	options->setExtensionAvailable(true);
	fx.tick(40);
	std::string error;
	REQUIRE_MESSAGE(options->openExtension(&error), error);
	OpenBFMEOptionsPage *page = options->extensionPage();
	REQUIRE_MESSAGE(page->show(samplePage(), &error), error);
	fx.tick(10);
	AptSpriteInst *root = fx.wm->apt().level(options->level());
	auto clickBar = [&](const char *name) {
		AptButtonInst *b = firstButtonUnder(findClip(root, name));
		REQUIRE_MESSAGE(b, name);
		float x0, y0, x1, y1;
		REQUIRE(b->contentBounds(x0, y0, x1, y1));
		float x, y;
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		clickStage(fx, x, y);
	};
	// a change, then ACCEPT
	REQUIRE_MESSAGE(page->choose("show_fps", 1, &error), error);
	clickBar("Accept");
	std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 2);
	CHECK((events[0].kind == OpenBFMEPageEvent::Kind::Value && events[0].key == "show_fps"));
	CHECK(events[1].kind == OpenBFMEPageEvent::Kind::Accept);
	// RESET TO DEFAULTS, then a change
	clickBar("Default");
	REQUIRE_MESSAGE(page->choose("render_scale", 60, &error), error);
	events = options->takeExtensionEvents();
	REQUIRE(events.size() == 2);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Reset);
	CHECK((events[1].kind == OpenBFMEPageEvent::Kind::Value && events[1].key == "render_scale" && events[1].value == 60));
}

// Sol review r1 (3): the page's art is checked like the gadgets' own: an image the mapped image collection lacks is reported, not drawn as nothing
TEST_CASE("opts3 gadget layer retail: a decoration image the mapped images lack is reported; the page's tab highlight resolves")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	options->setExtensionAvailable(true);
	fx.tick(40);
	std::string error;
	REQUIRE_MESSAGE(options->openExtension(&error), error);
	REQUIRE_MESSAGE(options->extensionPage()->show(samplePage(), &error), error);
	GadgetDrawList list;
	fx.layer->buildDrawList(list);
	CHECK(list.unresolvedImages.empty());
	bool highlight = false;
	for (const GadgetDrawCommand &c : list.commands)
	{
		highlight = highlight || (c.window == "OpenBFMEPage" && c.kind == GadgetDrawCommand::Kind::Image && c.image == "AptListBoxHiliteSelectedItem");
	}
	CHECK(highlight);
	// a decoration naming art that is not there (a mod without it)
	fx.layer->setDecorator([](GadgetDrawList &out) {
		GadgetDrawCommand c;
		c.kind = GadgetDrawCommand::Kind::Image;
		c.window = "OpenBFMEPage";
		c.image = "NoSuchImageOpts3";
		c.x1 = c.y1 = 10;
		out.commands.push_back(c);
	});
	GadgetDrawList missing;
	fx.layer->buildDrawList(missing);
	REQUIRE(missing.unresolvedImages.size() == 1);
	CHECK(missing.unresolvedImages[0] == "NoSuchImageOpts3");
	for (const GadgetDrawCommand &c : missing.commands)
	{
		CHECK(c.image != "NoSuchImageOpts3");
	}
	options->closeExtension();
}

// ---- lane OPTS-4: three Options bugs of v0.4.0-alpha.3 -----------------------------------------------------------------------------------------------
namespace
{
std::array<int, 4> screenRect(GameWindow *w)
{
	int x = 0, y = 0, sx = 0, sy = 0;
	w->winGetScreenPosition(&x, &y);
	w->winGetSize(&sx, &sy);
	return { x, y, x + sx, y + sy };
}

// two drop-downs one row apart, as Graphics+ has "Look" over "Interface scale"
OpenBFMEPageSpec stackedChoices()
{
	OpenBFMEPageSpec spec = samplePage();
	OpenBFMEPageRow look;
	look.kind = OpenBFMEPageRow::Kind::Choice;
	look.key = "look";
	look.label = u"Look";
	look.items = { { u"Original", true }, { u"Enhanced", true } };
	OpenBFMEPageRow scale;
	scale.kind = OpenBFMEPageRow::Kind::Choice;
	scale.key = "ui_scale";
	scale.label = u"Interface scale";
	scale.items = { { u"100%", true }, { u"125%", true }, { u"150%", true } };
	spec.rows = { look, scale };
	return spec;
}

// the fill and blank boxes of a slider's tick row in a draw list (W3DGadgetHorizontalSliderImageDraw's images)
std::vector<const GadgetDrawCommand *> sliderBoxes(const GadgetDrawList &list, const GameWindow *slider)
{
	std::vector<const GadgetDrawCommand *> out;
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.root == slider && c.kind == GadgetDrawCommand::Kind::Image && c.image == "AptHSliderOnBar")
		{
			out.push_back(&c);
		}
	}
	return out;
}

std::string volumeText(int value)
{
	char buf[64];
	std::snprintf(buf, sizeof buf, "%f", (double)(float)value);
	return buf;
}
} // namespace

// Bug 1 (the owner at 2560 x 1440, Graphics+ "Look"): the open list had the next row's value and arrow button drawn through it, and that row took the
// clicks on the list. The rows are engine gadgets without a placeholder; a row made later is nearer the window list's head (drawn later, asked first)
TEST_CASE("opts4 gadget layer retail: an open drop-down list is drawn after every other gadget and takes the clicks inside it")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	options->setExtensionAvailable(true);
	fx.tick(40);
	std::string error;
	REQUIRE_MESSAGE(options->openExtension(&error), error);
	OpenBFMEOptionsPage *page = options->extensionPage();
	REQUIRE(page);
	REQUIRE_MESSAGE(page->show(stackedChoices(), &error), error);
	fx.tick(10);
	options->takeExtensionEvents();
	GameWindow *look = page->gadget("look");
	GameWindow *scale = page->gadget("ui_scale");
	REQUIRE(look);
	REQUIRE(scale);
	GameWindow *lookList = GadgetComboBoxGetListBox(look);
	GameWindow *scaleList = GadgetComboBoxGetListBox(scale);
	REQUIRE(lookList);
	REQUIRE(scaleList);
	REQUIRE(lookList->winIsHidden());
	// open "Look" by its arrow button
	const std::array<int, 4> button = screenRect(GadgetComboBoxGetDropDownButton(look));
	clickStage(fx, (button[0] + button[2]) / 2.0f, (button[1] + button[3]) / 2.0f);
	REQUIRE_FALSE(lookList->winIsHidden());
	REQUIRE(fx.layer->gadgets().winGetLoneWindow() == look);
	REQUIRE(fx.layer->openDropDown() == look);
	const std::array<int, 4> list = screenRect(lookList);
	const std::array<int, 4> below = screenRect(scale);
	// the scenario of the report: the open list lies over the row below (stage pixels: the page's rows are 30 apart since OPTS-5, a list entry is
	// 18 tall)
	CHECK(list == std::array<int, 4>{ 605, 246, 905, 290 });  // two entries of 22
	CHECK(below == std::array<int, 4>{ 605, 250, 905, 276 }); // the next row's combo box, 4 under the list's top

	// drawn: every command of the row below comes before the first command of the open drop-down
	GadgetDrawList drawn;
	fx.layer->buildDrawList(drawn);
	std::ptrdiff_t firstLook = -1, lastScale = -1, lastOther = -1, listText = -1;
	for (std::size_t i = 0; i < drawn.commands.size(); ++i)
	{
		const GadgetDrawCommand &c = drawn.commands[i];
		if (c.root == look)
		{
			firstLook = firstLook < 0 ? (std::ptrdiff_t)i : firstLook;
			if (c.kind == GadgetDrawCommand::Kind::Text && c.text == u"Enhanced")
			{
				listText = (std::ptrdiff_t)i;
			}
		}
		else
		{
			lastOther = (std::ptrdiff_t)i;
			if (c.root == scale)
			{
				lastScale = (std::ptrdiff_t)i;
			}
		}
	}
	REQUIRE(firstLook != -1);
	REQUIRE(lastScale != -1);
	REQUIRE(listText != -1); // the list's second entry is drawn
	CHECK(lastScale < firstLook); // tolerance: draw order (an index before another), the property itself
	CHECK(lastOther < firstLook); // tolerance: draw order: nothing of the page or of another gadget is drawn over the open drop-down
	CHECK((std::size_t)(lastOther + 1) + (drawn.commands.size() - (std::size_t)firstLook) == drawn.commands.size()); // the drop-down's commands are one block at the end

	// clicked: the entry "Enhanced", at a point inside the row below, is the list's
	const float clickX = 755.0f, clickY = 275.0f; // inside the list's second entry (268 .. 290) and inside the row below (254 .. 280)
	SUBCASE("uninterrupted dropdown") {}
	SUBCASE("destroying an unrelated combo during hover preserves the open list")
	{
		GameWindow *transient = fx.layer->createEngineGadget("window/apt/combobox.wnd", 0, 0, 200, 26, nullptr, &error);
		REQUIRE_MESSAGE(transient, error);
		fx.layer->mouseMove(clickX, clickY);
		fx.layer->destroyEngineGadget(transient);
		REQUIRE(fx.layer->openDropDown() == look);
		REQUIRE_FALSE(lookList->winIsHidden());
	}
	clickStage(fx, clickX, clickY);
	const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].key == "look");
	CHECK(events[0].value == 1);
	int sel = -1;
	GadgetComboBoxGetSelectedPos(look, &sel);
	CHECK(sel == 1);
	CHECK(lookList->winIsHidden());  // chosen: the list closed
	CHECK(scaleList->winIsHidden()); // the row below never saw the click
	GadgetComboBoxGetSelectedPos(scale, &sel);
	CHECK(sel == 0);
	CHECK(fx.layer->gadgets().winGetLoneWindow() == look); // ZH: a click inside the lone window leaves it the lone window (its list is closed)
	// ... which is no open drop-down: the closed combo box is drawn in its own place again (not over what a movie draws later)
	CHECK(fx.layer->openDropDown() == nullptr);
	{
		GadgetDrawList closed;
		fx.layer->buildDrawList(closed);
		std::ptrdiff_t lookFirst = -1, scaleFirst = -1;
		for (std::size_t i = 0; i < closed.commands.size(); ++i)
		{
			if (closed.commands[i].root == look && lookFirst < 0)
			{
				lookFirst = (std::ptrdiff_t)i;
			}
			if (closed.commands[i].root == scale && scaleFirst < 0)
			{
				scaleFirst = (std::ptrdiff_t)i;
			}
		}
		REQUIRE(lookFirst != -1);
		REQUIRE(scaleFirst != -1);
		CHECK(lookFirst < scaleFirst); // tolerance: draw order (the window list's: the row made first is drawn first)
	}
	// with the list closed the row below takes its own clicks again
	const std::array<int, 4> scaleButton = screenRect(GadgetComboBoxGetDropDownButton(scale));
	clickStage(fx, (scaleButton[0] + scaleButton[2]) / 2.0f, (scaleButton[1] + scaleButton[3]) / 2.0f);
	CHECK_FALSE(scaleList->winIsHidden());
	CHECK(fx.layer->gadgets().winGetLoneWindow() == scale);
	CHECK(fx.layer->openDropDown() == scale);
}

// Bug 2 (the owner: "the % bars overflow"): RotWK's slider draw (RW 0x4A1130) scales its boxes by the display's width / 800 and height / 600 unless the
// window has status 0x08000000, which every component made from the template skins has (RW 0x815389 / 0x7190B4 / 0x718D93). The port left the bit
// off, so at its 1024 x 768 stage a box was 1.28 times the window's height: below the frame at every window size
TEST_CASE("opts4 options retail: a slider's tick boxes are as tall as its window and stay inside it (Options.apt's seven sliders, the OpenBFME page's)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\nMusicVolume = 100\nSFXVolume = 73\nVoiceVolume = 1\nAmbientVolume = 50\nMovieVolume = 70\nBrightness = 100\nScrollFactor = 50\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	auto checkSlider = [&](GameWindow *slider, const std::string &name) {
		INFO(name);
		REQUIRE(slider);
		CHECK(BitTest(slider->winGetStatus(), WIN_STATUS_APT_TEMPLATE));
		GadgetDrawList drawn;
		fx.layer->buildDrawList(drawn);
		const std::array<int, 4> r = screenRect(slider);
		const std::vector<const GadgetDrawCommand *> boxes = sliderBoxes(drawn, slider);
		REQUIRE_FALSE(boxes.empty());
		for (const GadgetDrawCommand *c : boxes)
		{
			CHECK(c->y0 == r[1]);
			CHECK(c->y1 == r[3]); // the window's height exactly (retail: (int)(height * 1.0))
			CHECK(c->x0 >= r[0]); // tolerance: containment in the window, the property itself
			CHECK(c->x1 <= r[2]); // tolerance: containment in the window
		}
		// the thumb is the window's height too (RW 0x7237F8), so the row and the thumb end on one line
		const std::array<int, 4> thumb = screenRect(GadgetSliderGetThumb(slider));
		CHECK(thumb[1] == r[1]);
		CHECK(thumb[3] == r[3]);
		// the fill ends at the thumb: no selected box starts right of the thumb's right edge
		CHECK(boxes.back()->x0 <= thumb[2]); // tolerance: an edge left of another, the property itself
	};
	for (const char *name : { "MusicVolume", "SoundFxVolume", "VoiceVolume", "AmbientVolume", "MovieVolume", "Brightness", "ScrollSpeed" })
	{
		checkSlider(options->gadget(name), name);
	}
	// the OpenBFME page's slider (an engine gadget of the same skin)
	options->setExtensionAvailable(true);
	fx.tick(40);
	std::string error;
	REQUIRE_MESSAGE(options->openExtension(&error), error);
	REQUIRE_MESSAGE(options->extensionPage()->show(samplePage(), &error), error);
	fx.tick(10);
	checkSlider(options->extensionPage()->gadget("render_scale"), "OpenBFME page: render_scale");
}

// Bug 3 (the owner: the sliders "should be able to lower the audio in real time, and it plays a sound related to that slider"): RW 0x91F90D
TEST_CASE("opts4 options retail: a volume slider sets its volume while it moves and plays its sample; the sample does not restart while it sounds; Cancel puts the saved volumes back")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const std::string saved = "StaticGameLOD = High\nMusicVolume = 40.000000\nSFXVolume = 30.000000\nVoiceVolume = 50.000000\nAmbientVolume = 20.000000\nMovieVolume = 60.000000\n";
	ShellFx fx(mount, saved);
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	RecordingShellServices &sv = fx.services;
	sv.appliedOptions.clear();
	REQUIRE(sv.miscEvents.empty());
	// every volume slider takes input, the Movie one too
	for (const char *name : { "MusicVolume", "SoundFxVolume", "VoiceVolume", "AmbientVolume", "MovieVolume" })
	{
		REQUIRE_MESSAGE(options->gadget(name), name);
		CHECK_MESSAGE(BitTest(options->gadget(name)->winGetStatus(), WIN_STATUS_ENABLED), name);
	}

	// 1. a click on the Sound FX track: the volume at once, the sound effects' sample, removed when the click is done (RotWK's message 0x4010)
	GameWindow *sfx = options->gadget("SoundFxVolume");
	std::array<int, 4> r = screenRect(sfx);
	REQUIRE(GadgetSliderGetPosition(sfx) == 30);
	clickStage(fx, r[0] + (r[2] - r[0]) * 0.8f, (r[1] + r[3]) / 2.0f);
	const int sfxNow = GadgetSliderGetPosition(sfx);
	REQUIRE(sfxNow == 50); // a track click pages the thumb a fifth of the width towards the pointer (RW 0x7234EC)
	REQUIRE(sv.appliedOptions.size() == 1);
	CHECK(sv.appliedOptions[0].first == "SFXVolume");
	CHECK(sv.appliedOptions[0].second == volumeText(sfxNow));
	REQUIRE(sv.miscEvents.size() == 1);
	CHECK(sv.miscEvents[0].second == "VolumeSampleSoundFX");
	// RW 0x91F90D removes the kept handle whenever the type changes, the empty one (0) too: 0 at the first track, then the sample when the click is done
	REQUIRE(sv.removedEvents.size() == 2);
	CHECK(sv.removedEvents[0] == 0);
	CHECK(sv.removedEvents[1] == sv.miscEvents[0].first);
	CHECK(sv.sounding.empty());
	CHECK(fx.fileText() == saved); // nothing is written before ACCEPT

	// 2. the Voice thumb dragged: a volume per step, one sample while it sounds, another once it has ended; the voice line is not removed on release
	GameWindow *voice = options->gadget("VoiceVolume");
	std::array<int, 4> thumb = screenRect(GadgetSliderGetThumb(voice));
	const float ty = (thumb[1] + thumb[3]) / 2.0f;
	float tx = (thumb[0] + thumb[2]) / 2.0f;
	sv.appliedOptions.clear();
	sv.removedEvents.clear();
	fx.wm->postMouseMove(tx, ty);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	std::vector<int> positions;
	for (int step = 0; step < 3; ++step)
	{
		tx += 12.0f;
		fx.wm->postMouseMove(tx, ty);
		fx.tick(2);
		positions.push_back(GadgetSliderGetPosition(voice));
	}
	REQUIRE(positions == std::vector<int>{ 56, 63, 70 }); // from 50: 12 stage pixels a step on a track of (width - 13) / 100 pixels a point
	REQUIRE(sv.appliedOptions.size() == 3); // one per step, while the button is down
	for (int step = 0; step < 3; ++step)
	{
		CHECK(sv.appliedOptions[(std::size_t)step].first == "VoiceVolume");
		CHECK(sv.appliedOptions[(std::size_t)step].second == volumeText(positions[(std::size_t)step]));
	}
	REQUIRE(sv.miscEvents.size() == 2); // the sound effects' sample of step 1, then one voice sample for the three steps
	CHECK(sv.miscEvents[1].second == "VolumeSampleVoice");
	const std::uint32_t voiceSample = sv.miscEvents[1].first;
	sv.sounding.erase(voiceSample); // the line has ended
	tx += 12.0f;
	fx.wm->postMouseMove(tx, ty);
	fx.tick(2);
	REQUIRE(sv.miscEvents.size() == 3); // moved again after the end: the sample again
	CHECK(sv.miscEvents[2].second == "VolumeSampleVoice");
	fx.wm->postMouseButton(false);
	fx.tick(2);
	REQUIRE(sv.removedEvents.size() == 1); // the type's change at the first step (the empty handle); nothing when the thumb is let go: type 1 plays to its end
	CHECK(sv.removedEvents[0] == 0);
	CHECK(sv.sounding.count(sv.miscEvents[2].first) == 1);

	// 3. another slider while the voice line sounds: the line is removed, the new type's sample starts
	GameWindow *ambient = options->gadget("AmbientVolume");
	r = screenRect(ambient);
	fx.wm->postMouseMove(r[0] + (r[2] - r[0]) * 0.9f, (r[1] + r[3]) / 2.0f);
	fx.tick(2);
	fx.wm->postMouseButton(true);
	fx.tick(2);
	fx.wm->postMouseButton(false);
	fx.tick(2);
	REQUIRE(sv.miscEvents.size() == 4);
	CHECK(sv.miscEvents[3].second == "VolumeSampleAmbient");
	REQUIRE(sv.removedEvents.size() == 3);
	CHECK(sv.removedEvents[1] == sv.miscEvents[2].first); // the voice line, when the type changed
	CHECK(sv.removedEvents[2] == sv.miscEvents[3].first); // the ambient sample, when the click was done
	CHECK(sv.sounding.empty());
	CHECK(sv.appliedOptions.back().first == "AmbientVolume");
	CHECK(sv.appliedOptions.back().second == volumeText(GadgetSliderGetPosition(ambient)));

	// 4. the Movie slider: its own volume and sample
	GameWindow *movie = options->gadget("MovieVolume");
	r = screenRect(movie);
	clickStage(fx, r[0] + (r[2] - r[0]) * 0.1f, (r[1] + r[3]) / 2.0f);
	REQUIRE(GadgetSliderGetPosition(movie) == 38); // from 60, a fifth of the track down
	CHECK(sv.appliedOptions.back().first == "MovieVolume");
	CHECK(sv.appliedOptions.back().second == volumeText(GadgetSliderGetPosition(movie)));
	REQUIRE(sv.miscEvents.size() == 5);
	CHECK(sv.miscEvents[4].second == "VolumeSampleMovie");
	CHECK(sv.miscEventViews == std::vector<int>(5, 2)); // every sample with the view type 2 (RW 0x91F90D -> RW 0x6DA95E + 0x30)

	// 5. the Music slider of a screen opened from the main menu (+ 0x280 set): the volume, no sample (the shell's music plays)
	GameWindow *music = options->gadget("MusicVolume");
	r = screenRect(music);
	clickStage(fx, r[0] + (r[2] - r[0]) * 0.9f, (r[1] + r[3]) / 2.0f);
	REQUIRE(GadgetSliderGetPosition(music) == 61); // from 40, a fifth of the track up
	CHECK(sv.appliedOptions.back().first == "MusicVolume");
	CHECK(sv.appliedOptions.back().second == volumeText(GadgetSliderGetPosition(music)));
	CHECK(sv.miscEvents.size() == 5);

	// 6. Cancel (RW 0x91EC52): the five saved volumes again, Options.ini as it was
	sv.appliedOptions.clear();
	REQUIRE(fx.wm->invokeCallback("AptOptions::Cancel", ""));
	REQUIRE(sv.appliedOptions.size() == 5);
	const std::pair<std::string, std::string> want[5] = { { "SFXVolume", volumeText(30) }, { "VoiceVolume", volumeText(50) }, { "MusicVolume", volumeText(40) },
		{ "AmbientVolume", volumeText(20) }, { "MovieVolume", volumeText(60) } };
	for (int t = 0; t < 5; ++t)
	{
		CHECK(sv.appliedOptions[(std::size_t)t] == want[t]);
	}
	CHECK(fx.fileText() == saved);
}

TEST_CASE("opts4 options retail: the music sample plays only outside the main menu's screen and outside a multiplayer game; Reset sets the default volumes at once; ACCEPT keeps a moved volume")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\nMusicVolume = 40.000000\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	RecordingShellServices &sv = fx.services;
	GameWindow *music = options->gadget("MusicVolume");
	REQUIRE(music);
	const std::array<int, 4> r = screenRect(music);
	auto clickAt = [&](float part) { clickStage(fx, r[0] + (r[2] - r[0]) * part, (r[1] + r[3]) / 2.0f); };
	// the quit menu's screen (RW 0x921783: every mode byte 0) in a skirmish game: the music sample, removed when the click is done
	options->setMode(false, false, false, false);
	QuitMenuContext game;
	game.inGame = true;
	game.gameMode = 2;
	fx.environment.quitMenu = &game;
	clickAt(0.9f);
	REQUIRE(sv.miscEvents.size() == 1);
	CHECK(sv.miscEvents[0].second == "VolumeSampleMusic");
	REQUIRE(sv.removedEvents.size() == 2); // the empty handle at the type's change, the sample when the click is done
	CHECK(sv.removedEvents[1] == sv.miscEvents[0].first);
	// the same screen in a LAN game (RW 0x441B7C): the volume, no sample
	game.gameMode = 1;
	sv.appliedOptions.clear();
	clickAt(0.1f);
	REQUIRE(sv.appliedOptions.size() == 1);
	CHECK(sv.appliedOptions[0].first == "MusicVolume");
	CHECK(sv.miscEvents.size() == 1);
	// and without a game (TheGameLogic null): none either
	game.gameMode = 2;
	fx.environment.quitMenu = nullptr;
	clickAt(0.9f);
	CHECK(sv.miscEvents.size() == 1);

	// Reset (RW 0x91F4D1) on the basic page: the sliders at the AudioSettings defaults and the five volumes set at once (the fixture's 70 75 55 60 80)
	options->setMode(false, true, true, false);
	sv.appliedOptions.clear();
	REQUIRE(fx.wm->invokeCallback("AptOptions::Reset", ""));
	std::vector<std::pair<std::string, std::string>> volumes;
	for (const auto &o : sv.appliedOptions)
	{
		if (o.first.size() > 6 && o.first.compare(o.first.size() - 6, 6, "Volume") == 0)
		{
			volumes.push_back(o);
		}
	}
	REQUIRE(volumes.size() == 5);
	// (int)(default * 100.0f) in float, as the binary computes it
	const float defaults[5] = { 0.70f, 0.75f, 0.55f, 0.60f, 0.80f };
	const char *const keys[5] = { "SFXVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume" };
	for (int t = 0; t < 5; ++t)
	{
		CHECK(volumes[(std::size_t)t].first == keys[t]);
		CHECK(volumes[(std::size_t)t].second == volumeText((int)(defaults[t] * 100.0f)));
	}
	CHECK(GadgetSliderGetPosition(music) == (int)(0.55f * 100.0f));

	// a moved volume is kept by ACCEPT (RW 0x91FC9C writes the slider)
	clickAt(0.9f);
	const int moved = GadgetSliderGetPosition(music);
	REQUIRE(moved == 76); // 55 and a fifth of the track
	REQUIRE(fx.wm->invokeCallback("AptOptions::Save", ""));
	CHECK(fx.fileText().find("MusicVolume = " + volumeText(moved)) != std::string::npos);
}

// the audio manager's side of the volume: RotWK's TheAudio vslot 0xE8(type, volume), and the request log's volume line (OPENBFME_AUDIO_LOG)
TEST_CASE("opts4 audio: setSliderVolume sets one submix slider's system volume, the Movie one too, and writes a volume line to the request log")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AudioIniState ini;
	INIEnvironment env;
	env.fileSystem = &mount.fs;
	ini.registerBlocks(env.blocks);
	{
		INI loader(env);
		ini.loadAll(loader);
	}
	AudioAssetCache cache(&mount.fs, 8u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager audio(ini, cache, device, RandomAlgorithm::RotWK_GameDat_LCG, 1);
	audio.enableEventLog(true);
	const float before[5] = { audio.getSliderVolume(SLIDER_SOUNDFX), audio.getSliderVolume(SLIDER_VOICE), audio.getSliderVolume(SLIDER_MUSIC), audio.getSliderVolume(SLIDER_AMBIENT),
		audio.getSliderVolume(SLIDER_MOVIE) };
	REQUIRE(audio.setSliderVolume(SLIDER_MOVIE, 0.25f));
	CHECK(audio.getSliderVolume(SLIDER_MOVIE) == 0.25f);
	for (int s = SLIDER_SOUNDFX; s <= SLIDER_AMBIENT; ++s)
	{
		CHECK(audio.getSliderVolume(s) == before[s]); // one slider only
	}
	REQUIRE(audio.setSliderVolume(SLIDER_MUSIC, 0.5f));
	CHECK(audio.getVolume(AudioAffect_Music | AudioAffect_SystemSetting) == 0.5f);
	REQUIRE(audio.setSliderVolume(SLIDER_VOICE, 7.0f));
	CHECK(audio.getSliderVolume(SLIDER_VOICE) == 1.0f);
	CHECK_FALSE(audio.setSliderVolume(SLIDER_NONE, 0.5f)); // no such slider: refused, nothing changes
	CHECK_FALSE(audio.setSliderVolume(-1, 0.5f));
	// the Movie sample is heard at the Movie slider's volume: the effective volume follows it
	AudioEventRTS sample(audio.miscAudio("VolumeSampleMovie"));
	REQUIRE(audio.isValidAudioEvent(sample.getEventName()));
	sample.setAudioEventInfo(ini.infos.find(sample.getEventName()));
	const float quarter = audio.getEffectiveVolume(sample);
	REQUIRE(audio.setSliderVolume(SLIDER_MOVIE, 0.5f));
	const float halfVolume = audio.getEffectiveVolume(sample);
	CHECK(quarter == 0x1.b33332p-3f); // VolumeSampleMovie's Volume (MOVIE_VOLUME, 85%) at the slider's 0.25
	CHECK(halfVolume == quarter * 2.0f); // twice the slider, exactly (a power of two)
	const std::vector<AudioLogEntry> log = audio.takeEventLog();
	REQUIRE(log.size() == 4);
	CHECK((log[0].volume && log[0].event == "MOVIE" && log[0].outcome == "0.25"));
	CHECK((log[1].volume && log[1].event == "MUSIC" && log[1].outcome == "0.50"));
	CHECK((log[2].volume && log[2].event == "VOICE" && log[2].outcome == "1.00"));
	CHECK((log[3].volume && log[3].event == "MOVIE" && log[3].outcome == "0.50"));
	const std::string line = AudioManager::formatLogLine(log[1], 0, "-");
	CHECK(line.find("\tvolume\t") != std::string::npos);
	CHECK(line.find("\tMUSIC\t") != std::string::npos);
	CHECK(line.substr(line.rfind('\t') + 1) == "0.50");
}

// Sol r1 on OPTS-4: one rule for drawing and input (GameWindowManager::winGetOpenDropDown): an open list, nested ones too, is drawn last with the clip
// its ancestors had set and takes the clicks inside that clip; a closed list, a modal window pushed after it and the movies' DisableComponents() end it
namespace
{
int g_parentClipBottom = 300; // the clipping parent's region bottom (stage), changed by the test

struct NestedCombos
{
	GameWindow *parent = nullptr; // a top-level window that clips what follows it (as a list box clips its cells)
	GameWindow *inner = nullptr;  // a combo box inside it, at (120, 120)
	GameWindow *lower = nullptr;  // a top-level combo box made later, under the inner one's list
};

NestedCombos makeNestedCombos(ShellFx &fx)
{
	NestedCombos n; // the inner combo box's list drops from y 146 (22 an entry), over the lower one (180 .. 206)
	GameWindowManager &gwm = fx.layer->gadgets();
	WinInstanceData inst;
	inst.m_style = GWS_USER_WINDOW;
	inst.m_decoratedNameString = "ClippingParent";
	n.parent = gwm.winCreate(nullptr, WIN_STATUS_ENABLED, 100, 100, 300, 200, nullptr, &inst);
	REQUIRE(n.parent);
	n.parent->winSetDrawFunc([](GameWindow *w, WinInstanceData *) {
		IRegion2D r;
		r.lo.x = 100;
		r.lo.y = 100;
		r.hi.x = 400;
		r.hi.y = g_parentClipBottom;
		w->manager().setClipRegion(r);
	});
	std::string error;
	n.inner = fx.layer->createEngineGadget("window/apt/combobox.wnd", 0, 0, 200, 26, nullptr, &error);
	REQUIRE_MESSAGE(n.inner, error);
	n.inner->winSetParent(n.parent);
	n.inner->winSetPosition(20, 20);
	n.lower = fx.layer->createEngineGadget("window/apt/combobox.wnd", 120, 180, 200, 26, nullptr, &error);
	REQUIRE_MESSAGE(n.lower, error);
	for (const char16_t *t : { u"One", u"Two", u"Three" })
	{
		GadgetComboBoxAddEntry(n.inner, t, 0xFFFFFFFFu);
		GadgetComboBoxAddEntry(n.lower, t, 0xFFFFFFFFu);
	}
	GadgetComboBoxSetSelectedPos(n.inner, 0);
	GadgetComboBoxSetSelectedPos(n.lower, 0);
	return n;
}

void layerClick(ShellFx &fx, float x, float y)
{
	fx.layer->mouseMove(x, y);
	fx.layer->mouseButton(true);
	fx.layer->mouseButton(false);
}

void openByButton(ShellFx &fx, GameWindow *combo)
{
	const std::array<int, 4> b = screenRect(GadgetComboBoxGetDropDownButton(combo));
	layerClick(fx, (b[0] + b[2]) / 2.0f, (b[1] + b[3]) / 2.0f);
}

// the stage box of an entry's text in a draw list (the open list's)
std::array<int, 4> entryText(const GadgetDrawList &list, const char16_t *text)
{
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.kind == GadgetDrawCommand::Kind::Text && c.text == text && c.root != nullptr && BitTest(const_cast<GameWindow *>(c.root)->winGetStyle(), GWS_COMBO_BOX))
		{
			return { c.x0, c.y0, c.x1, c.y1 };
		}
	}
	return { -1, -1, -1, -1 };
}

int selected(GameWindow *combo)
{
	int sel = -1;
	GadgetComboBoxGetSelectedPos(combo, &sel);
	return sel;
}
} // namespace

TEST_CASE("opts4 gadget layer retail: the open drop-down rule: a nested list is drawn last with its ancestor's clip and takes the clicks inside that clip only")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	g_parentClipBottom = 300;
	NestedCombos n = makeNestedCombos(fx);
	GameWindowManager &gwm = fx.layer->gadgets();
	openByButton(fx, n.inner);
	REQUIRE(gwm.winGetOpenDropDown() == n.inner);
	REQUIRE(fx.layer->openDropDown() == n.inner);
	GadgetDrawList drawn;
	fx.layer->buildDrawList(drawn);
	// the block: from the first command under the inner combo's root to the end, the parent's clip first and its end last
	std::size_t first = drawn.commands.size();
	for (std::size_t i = 0; i < drawn.commands.size(); ++i)
	{
		if (drawn.commands[i].root == n.inner)
		{
			first = i;
			break;
		}
	}
	REQUIRE(first != drawn.commands.size());
	for (std::size_t i = first; i < drawn.commands.size(); ++i)
	{
		CHECK(drawn.commands[i].root == n.inner); // one block at the end: nothing of the lower combo or the parent after it
	}
	const GadgetDrawCommand &clip = drawn.commands[first];
	CHECK(clip.kind == GadgetDrawCommand::Kind::ClipBegin);
	CHECK(std::array<int, 4>{ clip.x0, clip.y0, clip.x1, clip.y1 } == std::array<int, 4>{ 100, 100, 400, 300 });
	CHECK(drawn.commands.back().kind == GadgetDrawCommand::Kind::ClipEnd);
	const std::array<int, 4> three = entryText(drawn, u"Three");
	REQUIRE(three[1] != -1);
	const std::array<int, 4> lowerRect = screenRect(n.lower);
	const float x = 200.0f, y = (float)three[1] + 4.0f;
	// the entry lies over the lower combo box
	REQUIRE(y >= (float)lowerRect[1]); // tolerance: a point inside a rectangle, the scenario itself
	REQUIRE(y <= (float)lowerRect[3]); // tolerance: a point inside a rectangle
	layerClick(fx, x, y);
	CHECK(selected(n.inner) == 2);
	CHECK(GadgetComboBoxGetListBox(n.lower)->winIsHidden());
	CHECK(selected(n.lower) == 0);

	// closed by the choice: the lone window stays (ZH) but the rule is over, so the same point is the lower combo box's again
	CHECK(gwm.winGetLoneWindow() == n.inner);
	CHECK(gwm.winGetOpenDropDown() == nullptr);
	layerClick(fx, x, y);
	CHECK(selected(n.inner) == 2);
	CHECK_FALSE(GadgetComboBoxGetListBox(n.lower)->winIsHidden()); // the click opened the lower list
	CHECK(gwm.winGetOpenDropDown() == n.lower);
	layerClick(fx, 10, 10); // beside everything: closes it
	CHECK(gwm.winGetOpenDropDown() == nullptr);

	// the ancestor's clip ends above the third entry: the list's part outside it is neither drawn nor the list's for the pointer
	g_parentClipBottom = (int)y - 6;
	openByButton(fx, n.inner);
	REQUIRE(gwm.winGetOpenDropDown() == n.inner);
	drawn.clear();
	fx.layer->buildDrawList(drawn);
	CHECK(drawn.commands[drawn.commands.size() - 1].kind == GadgetDrawCommand::Kind::ClipEnd);
	layerClick(fx, x, y);
	CHECK(selected(n.inner) == 2);                                   // not the list's
	CHECK_FALSE(GadgetComboBoxGetListBox(n.lower)->winIsHidden()); // the lower combo box took it
	layerClick(fx, 10, 10);
}

TEST_CASE("opts4 gadget layer retail: the open drop-down rule: a modal window pushed after the list and the movies' DisableComponents() end it; a modal around it does not")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	g_parentClipBottom = 300;
	NestedCombos n = makeNestedCombos(fx);
	GameWindowManager &gwm = fx.layer->gadgets();
	openByButton(fx, n.lower);
	REQUIRE(gwm.winGetOpenDropDown() == n.lower);
	GadgetDrawList drawn;
	fx.layer->buildDrawList(drawn);
	const std::array<int, 4> two = entryText(drawn, u"Two");
	REQUIRE(two[1] != -1);
	auto promoted = [&]() {
		GadgetDrawList l;
		fx.layer->buildDrawList(l);
		return !l.commands.empty() && l.commands.back().root == n.lower && entryText(l, u"Two")[1] != -1;
	};

	// a modal window pushed after the list opened (a message box of the window system): the list is no longer above everything, its clicks are the modal's
	WinInstanceData inst;
	inst.m_style = GWS_USER_WINDOW;
	inst.m_decoratedNameString = "ModalBox";
	GameWindow *modal = gwm.winCreate(nullptr, WIN_STATUS_ENABLED, 0, 0, 1024, 768, nullptr, &inst);
	REQUIRE(modal);
	modal->winSetDrawFunc([](GameWindow *w, WinInstanceData *) { w->manager().winFillRect(0xFF000000u, 1, 300, 300, 600, 400); });
	gwm.winSetModal(modal);
	CHECK(gwm.winGetOpenDropDown() == nullptr);
	CHECK(fx.layer->openDropDown() == nullptr);
	drawn.clear();
	fx.layer->buildDrawList(drawn);
	CHECK(drawn.commands.back().root == modal); // the modal (made last, the list's head) is drawn last; the open list stays under it
	gwm.winUnsetModal(modal);
	CHECK(gwm.winGetOpenDropDown() == n.lower); // the list is still open: above everything again
	CHECK(promoted());
	// the click of an entry while the modal is up is the modal's (the list closes as ZH closes it on a click elsewhere)
	gwm.winSetModal(modal);
	layerClick(fx, 200.0f, (float)two[1] + 4.0f);
	CHECK(selected(n.lower) == 0);
	CHECK(GadgetComboBoxGetListBox(n.lower)->winIsHidden());
	gwm.winUnsetModal(modal);
	gwm.winDestroy(modal);
	gwm.processDestroyList();
	openByButton(fx, n.lower);
	REQUIRE(gwm.winGetOpenDropDown() == n.lower);

	// a modal window the list is part of (the combo box inside the modal) keeps it
	gwm.winSetModal(n.lower);
	CHECK(gwm.winGetOpenDropDown() == n.lower);
	gwm.winUnsetModal(n.lower);

	// the movies' DisableComponents() (a pop-up of a movie): no list above it, no gadget input; EnableComponents() gives both back. The inner combo
	// box's list (made before the lower one: drawn before it unless promoted)
	layerClick(fx, 10, 10);
	REQUIRE(gwm.winGetOpenDropDown() == nullptr);
	openByButton(fx, n.inner);
	REQUIRE(gwm.winGetOpenDropDown() == n.inner);
	auto innerLast = [&]() {
		GadgetDrawList l;
		fx.layer->buildDrawList(l);
		return !l.commands.empty() && l.commands.back().root == n.inner;
	};
	CHECK(innerLast());
	GadgetDrawList open;
	fx.layer->buildDrawList(open);
	const std::array<int, 4> innerTwo = entryText(open, u"Two");
	REQUIRE(innerTwo[1] != -1);
	REQUIRE(fx.wm->invokeCallback("DisableComponents", ""));
	REQUIRE(fx.wm->componentInputBlocked());
	CHECK(fx.layer->openDropDown() == nullptr);
	CHECK_FALSE(innerLast());
	fx.wm->postMouseMove(200.0f, (float)innerTwo[1] + 4.0f);
	fx.wm->postMouseButton(true);
	fx.wm->postMouseButton(false);
	CHECK(selected(n.inner) == 0);
	REQUIRE(fx.wm->invokeCallback("EnableComponents", ""));
	CHECK(fx.layer->openDropDown() == n.inner);
	CHECK(innerLast());
	layerClick(fx, 200.0f, (float)innerTwo[1] + 4.0f);
	CHECK(selected(n.inner) == 1);
}

// ---- lane OPTS-5: one Options menu, the OpenBFME one (AptOptionsScreen::kOpenBFMEMenu; NOT RETAIL LAYOUT) -------------------------------------------
namespace
{
// Options opened by the main menu (`advanced`: its CUSTOM SETTINGS) with the device offering the page; every frame of the opening the retail page's
// frames, its custom settings page and the OPENBFME button must be off screen
AptOptionsScreen *openMenu(ShellFx &fx, bool advanced)
{
	fx.environment.openbfmeOptionsMenu = true;
	fx.tick(2);
	fx.shell->push("MainMenu.apt");
	fx.tick(5);
	REQUIRE(fx.wm->invokeCallback("AptMainMenu::Options", advanced ? "true" : "false"));
	AptOptionsScreen *options = nullptr;
	for (int i = 0; i < 80; ++i)
	{
		fx.tick(1);
		options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
		REQUIRE(options);
		options->setExtensionAvailable(true); // the device's every frame (AptMenuPlayer::_process)
		CHECK(options->pageState() != 2);
		float box[4];
		CHECK_FALSE(options->entryBox(box));
		if (options->level() < 0)
		{
			continue;
		}
		AptSpriteInst *root = fx.wm->apt().level(options->level());
		for (const char *path : { "Main.FrameLeft", "Main.FrameMiddle", "Main.FrameRight" })
		{
			if (AptCharacterInst *f = fx.wm->apt().resolvePath(root, path))
			{
				CHECK_FALSE_MESSAGE(f->globallyVisible(), path << " at tick " << i);
			}
		}
	}
	REQUIRE(options->extensionOpen());
	return options;
}

std::u16string gameText(ShellFx &fx, const char *label)
{
	std::u16string t;
	REQUIRE_MESSAGE(fx.text.fetch(label, t), label);
	return t;
}

const OpenBFMEPageRow *findRow(const std::vector<OpenBFMEPageRow> &rows, const std::string &key)
{
	for (const OpenBFMEPageRow &r : rows)
	{
		if (r.key == key)
		{
			return &r;
		}
	}
	return nullptr;
}

const OpenBFMEPageRow &rowOf(const std::vector<OpenBFMEPageRow> &rows, const std::string &key)
{
	const OpenBFMEPageRow *r = findRow(rows, key);
	REQUIRE_MESSAGE(r, "no row " << key);
	return *r;
}

// the page with the original game's rows `keys` (one tab, in that order), as the device's spec puts them
OpenBFMEOptionsPage *showRetail(AptOptionsScreen *options, const std::vector<std::string> &keys, std::vector<int> columns = {})
{
	OpenBFMEPageSpec spec = samplePage();
	spec.rows.clear();
	const std::vector<OpenBFMEPageRow> all = options->retailRows();
	for (std::size_t i = 0; i < keys.size(); ++i)
	{
		const OpenBFMEPageRow *r = findRow(all, keys[i]);
		REQUIRE_MESSAGE(r, keys[i]);
		spec.rows.push_back(*r);
		spec.rows.back().column = i < columns.size() ? columns[i] : 0;
	}
	OpenBFMEOptionsPage *page = options->extensionPage();
	REQUIRE(page);
	std::string error;
	REQUIRE_MESSAGE(page->show(spec, &error), error);
	return page;
}

std::string iniLine(const std::string &text, const std::string &key)
{
	const std::string file = "\n" + text;
	const std::size_t at = file.find("\n" + key + " = ");
	if (at == std::string::npos)
	{
		return "(none)";
	}
	const std::size_t end = file.find_first_of("\r\n", at + 1);
	return file.substr(at + 1, end - at - 1);
}
} // namespace

TEST_CASE("opts5 options retail: the main menu's SETTINGS and CUSTOM SETTINGS open the OpenBFME menu at once; the retail page, its custom page and the OPENBFME button never show")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	{
		ShellFx fx(mount, "StaticGameLOD = High\n");
		AptOptionsScreen *options = openMenu(fx, false);
		CHECK(options->menuMode());
		const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
		REQUIRE(events.size() == 1);
		CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Entry);
		CHECK(events[0].value == 0);
		AptSpriteInst *root = fx.wm->apt().level(options->level());
		CHECK(fx.wm->apt().resolvePath(root, "Main.FrameLeft.OpenBFMEEntry") == nullptr); // no OPENBFME button was made
		CHECK(options->gadget("MusicVolume")->winIsHidden());
		CHECK(options->gadget("Detail")->winIsHidden()); // the retail Detail list (its Custom opens the custom page) cannot be clicked
		// over a game every press is the menu's while it is up (InGameHud::isOverGui asks this: its tabs and gadgets are not Apt buttons)
		CHECK(fx.wm->capturesMouse());
		options->finishMenu(false);
		fx.tick(30);
		CHECK_FALSE(fx.wm->capturesMouse());
	}
	{
		ShellFx fx(mount, "StaticGameLOD = High\n");
		AptOptionsScreen *options = openMenu(fx, true);
		const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
		REQUIRE(events.size() == 1);
		CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Entry);
		CHECK(events[0].value == 1); // the detail settings first
		CHECK(options->pageState() == AptOptionsScreen::kExtensionState);
		fx.tick(30);
		CHECK(options->pageState() == AptOptionsScreen::kExtensionState);
	}
}

TEST_CASE("opts5 options retail: the original game's rows carry its names and help texts from the string file and the values Options.ini holds")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "AllHealthBars = yes\nBrightness = 70\nMusicVolume = 40.000000\nSFXVolume = 30.000000\nScrollFactor = 60\nStaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	const std::vector<OpenBFMEPageRow> rows = options->retailRows();
	// every row of the table but the ones the movie lacks (none in RotWK 2.01's Options.apt)
	REQUIRE(rows.size() == AptOptionsScreen::retailRowTable().size());
	for (const AptOptionsScreen::RetailRow &info : AptOptionsScreen::retailRowTable())
	{
		const OpenBFMEPageRow *r = findRow(rows, info.key);
		REQUIRE_MESSAGE(r, info.key);
		std::u16string name = gameText(fx, info.nameLabel);
		CHECK_MESSAGE(r->label == name, info.key);
		std::u16string help = gameText(fx, info.helpLabel);
		// the help line starts with the string file's text on one line (the movie's tooltips break lines with "\n")
		std::u16string flat;
		for (char16_t c : help)
		{
			flat += c == u'\n' ? u' ' : c;
		}
		std::u16string squeezed;
		for (char16_t c : flat)
		{
			if (!(c == u' ' && (squeezed.empty() || squeezed.back() == u' ')))
			{
				squeezed += c;
			}
		}
		CHECK_MESSAGE(r->help.compare(0, squeezed.size(), squeezed) == 0, info.key);
	}
	CHECK(rowOf(rows, "Retail.MusicVolume").value == 40);
	CHECK(rowOf(rows, "Retail.SoundFxVolume").value == 30);
	CHECK(rowOf(rows, "Retail.Brightness").value == 70);
	CHECK(rowOf(rows, "Retail.ScrollSpeed").value == options->initialScrollSpeed());
	CHECK(rowOf(rows, "Retail.HealthBars").value == 1);
	// the level High (data 3) is the Graphics list's second entry; the custom rows show High's preset
	const OpenBFMEPageRow *detail = findRow(rows, "Retail.Detail");
	REQUIRE(detail->items.size() == 6);
	CHECK(detail->items[1].text == gameText(fx, "GUI:High"));
	CHECK(detail->value == 1);
	CHECK(detail->enabled);
	const std::array<int, GameLODManager::kAdvancedOptionCount> high = fx.gameLOD.presetSettings(3);
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		const OpenBFMEPageRow *r = findRow(rows, "Retail.AdvancedOption" + std::to_string(i));
		CHECK(r->value == high[(std::size_t)i]);
		CHECK(r->items.size() == (std::size_t)GameLODManager::advancedOption(i).count);
		CHECK(r->enabled == GameLODManager::advancedOptionDrawn(i));
	}
	// greyed with the reason: EAX3, High Audio Quality, Refresh NAT, and the network rows from the main menu (NetworkEnabled 0)
	for (const char *key : { "Retail.EAX3", "Retail.HighAudioQuality", "Retail.RefreshNat", "Retail.OnlineIp", "Retail.OnlinePortNum", "Retail.SendDelay" })
	{
		const OpenBFMEPageRow *r = findRow(rows, key);
		CHECK_FALSE_MESSAGE(r->enabled, key);
	}
	// the string file's text, a full stop where it has none, then OpenBFME's reason
	CHECK(gameText(fx, "ToolTip:Settings/EAX") == u"Enables EAX sound if you have an EAX compatible sound card");
	CHECK(rowOf(rows, "Retail.EAX3").help == u"Enables EAX sound if you have an EAX compatible sound card. OpenBFME has no EAX3 sound.");
	CHECK(rowOf(rows, "Retail.OnlineIp").help == gameText(fx, "TOOLTIP:OnlineIP") + u". Can be changed from the Options of the online screens.");
	CHECK(rowOf(rows, "Retail.OnlinePortNum").kind == OpenBFMEPageRow::Kind::Entry);
	CHECK(rowOf(rows, "Retail.RefreshNat").kind == OpenBFMEPageRow::Kind::Text);
}

TEST_CASE("opts5 options retail: the original rows act as on the retail screen: volumes and brightness at once, the sample stops on release; ACCEPT writes and applies them")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "AllHealthBars = no\nBrightness = 50\nSFXVolume = 30.000000\nScrollFactor = 50\nStaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	OpenBFMEOptionsPage *page = showRetail(options, { "Retail.SoundFxVolume", "Retail.Brightness", "Retail.ScrollSpeed", "Retail.HealthBars", "Retail.Detail",
														"Retail.AdvancedOption6" });
	fx.tick(2);
	RecordingShellServices &sv = fx.services;
	sv.appliedOptions.clear();
	sv.gammas.clear();
	std::string error;
	// Sound FX through its gadget: the volume at once and its sample (RW 0x91F90D); the thumb let go removes the sample (0x4010)
	REQUIRE_MESSAGE(page->choose("Retail.SoundFxVolume", 64, &error), error);
	REQUIRE(sv.appliedOptions.size() == 1);
	CHECK(sv.appliedOptions[0].first == "SFXVolume");
	CHECK(sv.appliedOptions[0].second == volumeText(64));
	REQUIRE(sv.miscEvents.size() == 1);
	CHECK(sv.miscEvents[0].second == "VolumeSampleSoundFX");
	CHECK(sv.miscEventViews[0] == 2);
	CHECK(GadgetSliderGetPosition(options->gadget("SoundFxVolume")) == 64); // the retail gadget holds the value Save reads
	GameWindow *sfxRow = page->gadget("Retail.SoundFxVolume");
	sfxRow->manager().winSendSystemMsg(sfxRow->winGetOwner(), GSM_SLIDER_CLICKED, reinterpret_cast<WindowMsgData>(sfxRow), 0);
	CHECK(sv.sounding.empty());
	// Brightness: the display's gamma at once (RW 0x91F90D's curve, as RW 0x641E8B)
	REQUIRE_MESSAGE(page->choose("Retail.Brightness", 80, &error), error);
	REQUIRE(sv.gammas.size() == 1);
	CHECK(sv.gammas[0] == OptionPreferences::displayGamma(80, 1.0f));
	// the rest at ACCEPT, as the retail screen
	REQUIRE_MESSAGE(page->choose("Retail.ScrollSpeed", 70, &error), error);
	REQUIRE_MESSAGE(page->choose("Retail.Detail", 3, &error), error); // GUI:Low, level 1
	CHECK(options->masterOption() == 1);
	// a custom setting: the level becomes Custom with Low's settings and this one changed (Texture Quality High)
	REQUIRE_MESSAGE(page->choose("Retail.AdvancedOption6", 2, &error), error);
	CHECK(options->masterOption() == GameLODManager::kCustomLevel);
	std::array<int, GameLODManager::kAdvancedOptionCount> want = fx.gameLOD.presetSettings(1);
	want[6] = 2;
	CHECK(options->shownAdvancedSettings() == want);
	CHECK(page->spec().rows[4].value == 5); // the Graphics list shows Custom now
	// Show All Health Bars after the level (a level change sets the box from the saved value again, RW 0x91F9CE -> 0x91EBD1); the pending custom
	// DecalLOD (Low's) allows it
	REQUIRE_MESSAGE(page->choose("Retail.HealthBars", 1, &error), error);
	CHECK(fx.fileText().find("SFXVolume = 30.000000") != std::string::npos); // nothing written yet
	options->finishMenu(true);
	fx.tick(30);
	CHECK((fx.shell->top() == nullptr || fx.shell->top()->filename() != "Options.apt"));
	const std::string file = fx.fileText();
	CHECK(iniLine(file, "SFXVolume") == "SFXVolume = 64.000000");
	CHECK(iniLine(file, "Brightness") == "Brightness = 80");
	CHECK(iniLine(file, "ScrollFactor") == "ScrollFactor = 70");
	CHECK(iniLine(file, "AllHealthBars") == "AllHealthBars = yes");
	CHECK(iniLine(file, "StaticGameLOD") == "StaticGameLOD = Custom");
	CHECK(iniLine(file, "TextureQualityLOD") == "TextureQualityLOD = High");
	CHECK(fx.gameLOD.staticLevel() == GameLODManager::kCustomLevel);
	CHECK(fx.gameLOD.state().textureReduction == 0); // Texture Quality High: no reduction (RW 0x517540)
}

TEST_CASE("opts5 options retail: CANCEL puts the saved volumes and brightness back and writes nothing; RESET sets retail's defaults and the level the game takes")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const std::string saved = "Brightness = 30\nSFXVolume = 30.000000\nStaticGameLOD = Low\n";
	{
		ShellFx fx(mount, saved);
		AptOptionsScreen *options = openMenu(fx, false);
		OpenBFMEOptionsPage *page = showRetail(options, { "Retail.SoundFxVolume", "Retail.Brightness", "Retail.Detail" });
		std::string error;
		REQUIRE_MESSAGE(page->choose("Retail.SoundFxVolume", 90, &error), error);
		REQUIRE_MESSAGE(page->choose("Retail.Brightness", 90, &error), error);
		REQUIRE_MESSAGE(page->choose("Retail.Detail", 0, &error), error);
		fx.services.appliedOptions.clear();
		fx.services.gammas.clear();
		options->finishMenu(false);
		fx.tick(30);
		CHECK((fx.shell->top() == nullptr || fx.shell->top()->filename() != "Options.apt"));
		REQUIRE(fx.services.gammas.size() == 1);
		CHECK(fx.services.gammas[0] == OptionPreferences::displayGamma(30, 1.0f)); // RW 0x91ECB8: the saved gamma
		bool sfx = false;
		for (const auto &o : fx.services.appliedOptions)
		{
			sfx = sfx || (o.first == "SFXVolume" && o.second == volumeText(30));
		}
		CHECK(sfx);
		CHECK(fx.fileText() == saved);
		CHECK(fx.gameLOD.staticLevel() == 1);
	}
	{
		ShellFx fx(mount, saved);
		AptOptionsScreen *options = openMenu(fx, false);
		showRetail(options, { "Retail.SoundFxVolume", "Retail.Brightness", "Retail.Detail" });
		fx.services.appliedOptions.clear();
		fx.services.gammas.clear();
		options->resetMenu();
		REQUIRE(fx.services.gammas.size() == 1);
		CHECK(fx.services.gammas[0] == 1.0f); // RW 0x91F5F3: the slider's middle, 50
		CHECK(GadgetSliderGetPosition(options->gadget("Brightness")) == 50);
		CHECK(GadgetSliderGetPosition(options->gadget("SoundFxVolume")) == 70); // AudioSettings' default (the fixture's 0.70)
		CHECK(options->masterOption() == GameLODManager::kDetectedLevel);
		const OpenBFMEOptionsPage *page = options->extensionPage();
		CHECK(page->spec().rows[0].value == 70);  // the rows follow
		CHECK(page->spec().rows[1].value == 50);
		CHECK(page->spec().rows[2].value == 0);   // GUI:UltraHigh
		options->finishMenu(true);
		fx.tick(30);
		const std::string file = fx.fileText();
		CHECK(iniLine(file, "Brightness") == "Brightness = 50");
		CHECK(iniLine(file, "SFXVolume") == "SFXVolume = 70.000000");
		CHECK(iniLine(file, "StaticGameLOD") == "StaticGameLOD = UltraHigh");
	}
}

TEST_CASE("opts5 options retail: opened in a game the detail rows are greyed with the movie's reason and refused; online rows work from the online shell; Refresh NAT is refused by name")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	{
		// the quit menu's opening (RW 0x91ED91(0, 0, 0, 0))
		ShellFx fx(mount, "StaticGameLOD = High\n");
		fx.environment.openbfmeOptionsMenu = true;
		fx.tick(2);
		AptOptionsScreen::open(*fx.shell, false, false, false, false);
		fx.tick(80);
		auto *options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
		REQUIRE(options);
		REQUIRE(options->extensionOpen());
		const std::vector<OpenBFMEPageRow> rows = options->retailRows();
		const std::u16string cannot = gameText(fx, "APT:CannotChangeGraphics");
		for (int i = -1; i < GameLODManager::kAdvancedOptionCount; ++i)
		{
			const std::string key = i < 0 ? std::string("Retail.Detail") : "Retail.AdvancedOption" + std::to_string(i);
			const OpenBFMEPageRow &r = rowOf(rows, key);
			CHECK_FALSE_MESSAGE(r.enabled, key);
			REQUIRE_MESSAGE(r.help.size() > cannot.size(), key); // tolerance: the row's own text, then the movie's reason
			CHECK_MESSAGE(r.help.compare(r.help.size() - cannot.size(), cannot.size(), cannot) == 0, key);
			std::string error;
			CHECK_FALSE(options->setRetailValue(key, 0, &error));
			CHECK(error == "the setting '" + key + "' is disabled");
		}
		CHECK(rowOf(rows, "Retail.MusicVolume").enabled); // what may change in a game
		CHECK(rowOf(rows, "Retail.Brightness").enabled);
		std::string error;
		CHECK_FALSE(options->setRetailValue("Retail.RefreshNat", 1, &error));
		CHECK(error == "the setting 'Retail.RefreshNat' is disabled");
		CHECK_FALSE(options->setRetailValue("Retail.Nothing", 1, &error));
		CHECK(error == "no setting 'Retail.Nothing'");
		CHECK_FALSE(options->setRetailValue("Retail.MusicVolume", 101, &error));
		CHECK(error == "the setting 'Retail.MusicVolume' does not take 101");
	}
	{
		// the online shell's opening (AptOnline::Options, RW 0x91D6BA: (1, 0, 1, 0))
		ShellFx fx(mount, "FirewallPortOverride = 0\nGameSpyIPAddress = 192.168.1.5\nSendDelay = no\nStaticGameLOD = High\n");
		fx.environment.openbfmeOptionsMenu = true;
		fx.tick(2);
		AptOptionsScreen::open(*fx.shell, true, false, true, false);
		fx.tick(80);
		auto *options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
		REQUIRE(options);
		REQUIRE(options->extensionOpen());
		OpenBFMEOptionsPage *page = showRetail(options, { "Retail.OnlineIp", "Retail.OnlinePortNum", "Retail.SendDelay", "Retail.RefreshNat" });
		std::string error;
		REQUIRE_MESSAGE(page->choose("Retail.OnlineIp", 1, &error), error);
		REQUIRE_MESSAGE(page->choose("Retail.OnlinePortNum", 8100, &error), error);
		REQUIRE_MESSAGE(page->choose("Retail.SendDelay", 1, &error), error);
		CHECK_FALSE(page->choose("Retail.RefreshNat", 1, &error)); // a text row: no gadget
		CHECK(GadgetTextEntryGetText(options->gadget("OnlinePortNum")) == u"8100");
		options->finishMenu(true);
		fx.tick(30);
		const std::string file = fx.fileText();
		CHECK(iniLine(file, "GameSpyIPAddress") == "GameSpyIPAddress = 10.0.0.7");
		CHECK(iniLine(file, "FirewallPortOverride") == "FirewallPortOverride = 8100");
		CHECK(iniLine(file, "SendDelay") == "SendDelay = yes");
	}
}

// An Options.ini the original game wrote (CR LF, its own number formats) shows its values in the menu and ACCEPT unchanged writes it back byte for byte:
// the new menu reads and writes the same file the same way (the original game and older builds keep reading it)
TEST_CASE("opts5 options retail: a retail Options.ini round-trips through the menu byte for byte")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const std::string retail = "AllHealthBars = yes\r\nAmbientVolume = 55.000000\r\nAudioLOD = High\r\nBrightness = 62\r\nFirewallPortOverride = 0\r\n"
							   "GameSpyIPAddress = 192.168.1.5\r\nMovieVolume = 80.000000\r\nMusicVolume = 41.000000\r\nSFXVolume = 77.000000\r\nScrollFactor = 50\r\n"
							   "SendDelay = no\r\nStaticGameLOD = Medium\r\nUseEAX3 = no\r\nVoiceVolume = 66.000000\r\n";
	ShellFx fx(mount, retail);
	AptOptionsScreen *options = openMenu(fx, false);
	const std::vector<OpenBFMEPageRow> rows = options->retailRows();
	CHECK(rowOf(rows, "Retail.AmbientVolume").value == 55);
	CHECK(rowOf(rows, "Retail.MusicVolume").value == 41);
	CHECK(rowOf(rows, "Retail.VoiceVolume").value == 66);
	CHECK(rowOf(rows, "Retail.MovieVolume").value == 80);
	CHECK(rowOf(rows, "Retail.Brightness").value == 62);
	CHECK(rowOf(rows, "Retail.HighAudioQuality").value == 1);
	CHECK(rowOf(rows, "Retail.EAX3").value == 0);
	CHECK(rowOf(rows, "Retail.Detail").value == 2); // GUI:Medium
	CHECK(rowOf(rows, "Retail.OnlineIp").value == 0);
	options->finishMenu(true);
	fx.tick(30);
	CHECK(fx.fileText() == retail);
}

TEST_CASE("opts5 options page: a tab of two columns keeps each column's rows inside its half, and labels and gadgets apart")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	std::vector<std::string> keys = { "Retail.Detail" };
	std::vector<int> columns = { 0 };
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		keys.push_back("Retail.AdvancedOption" + std::to_string(i));
		columns.push_back(0);
	}
	for (const char *k : { "Retail.HealthBars", "Retail.SoundFxVolume", "Retail.OnlinePortNum" })
	{
		keys.push_back(k);
		columns.push_back(1);
	}
	OpenBFMEOptionsPage *page = showRetail(options, keys, columns);
	std::vector<std::array<int, 4>> boxes;
	for (std::size_t i = 0; i < keys.size(); ++i)
	{
		std::array<int, 4> g{}, l{};
		REQUIRE_MESSAGE(page->rect(keys[i], g), keys[i]);
		REQUIRE(page->labelBox(keys[i], l));
		const int left = columns[i] == 0 ? OpenBFMEOptionsPage::kPanelLeft : OpenBFMEOptionsPage::kPanelMiddle;
		const int right = columns[i] == 0 ? OpenBFMEOptionsPage::kPanelMiddle : OpenBFMEOptionsPage::kPanelRight;
		CHECK_MESSAGE(l[0] == left + OpenBFMEOptionsPage::kColumnLabelInset, keys[i]);
		CHECK_MESSAGE(l[2] == g[0] - 8, keys[i]);
		CHECK_MESSAGE(g[0] == left + OpenBFMEOptionsPage::kColumnGadgetInset, keys[i]);
		CHECK_MESSAGE(g[2] <= right - OpenBFMEOptionsPage::kColumnRightInset, keys[i]); // tolerance: the gadget's width is its kind's
		CHECK_MESSAGE(g[3] <= OpenBFMEOptionsPage::kHelpTop - 6, keys[i]);             // tolerance: above the help line's rule
		boxes.push_back(g);
	}
	// the tenth row of the left column ends above the help line; no two gadgets meet
	std::array<int, 4> last{};
	REQUIRE(page->rect("Retail.AdvancedOption8", last));
	CHECK(last[1] == OpenBFMEOptionsPage::kRowTop + 9 * OpenBFMEOptionsPage::kRowPitch);
	for (std::size_t a = 0; a < boxes.size(); ++a)
	{
		for (std::size_t b = a + 1; b < boxes.size(); ++b)
		{
			const bool meet = boxes[a][0] < boxes[b][2] && boxes[b][0] < boxes[a][2] && boxes[a][1] < boxes[b][3] && boxes[b][1] < boxes[a][3];
			CHECK_FALSE_MESSAGE(meet, keys[a] << " / " << keys[b]);
		}
	}
}

// Sol review r1 (2): a modal (a message box: its movie calls DisableComponents()) takes the input first; the Options page under it hears no press
TEST_CASE("opts5 options retail: with a modal up (components disabled) a click on a tab, a gadget row or the bottom bar under it is not the menu's")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	OpenBFMEOptionsPage *page = showRetail(options, { "Retail.MusicVolume", "Retail.HealthBars" });
	options->takeExtensionEvents();
	std::array<int, 4> tab{}, bar{}, row{};
	REQUIRE(page->rect("tab:1", tab));
	REQUIRE(page->rect("Retail.HealthBars", row));
	fx.wm->enableComponents("", false); // the box's DisableComponents()
	REQUIRE(fx.wm->componentInputBlocked());
	const bool bars = GadgetCheckBoxIsChecked(options->gadget("HealthBars"));
	clickStage(fx, (tab[0] + tab[2]) / 2.0f, (tab[1] + tab[3]) / 2.0f);
	CHECK(options->takeExtensionEvents().empty()); // no tab event
	clickStage(fx, (row[0] + row[2]) / 2.0f, (row[1] + row[3]) / 2.0f);
	CHECK(GadgetCheckBoxIsChecked(options->gadget("HealthBars")) == bars);
	AptSpriteInst *root = fx.wm->apt().level(options->level());
	AptButtonInst *accept = firstButtonUnder(findClip(root, "Accept"));
	REQUIRE(accept);
	float x0, y0, x1, y1, x, y;
	REQUIRE(accept->contentBounds(x0, y0, x1, y1));
	accept->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	clickStage(fx, x, y);
	CHECK(options->takeExtensionEvents().empty()); // no ACCEPT from the page's filter either
	// the modal gone: the same tab click is the page's again
	fx.wm->enableComponents("", true);
	REQUIRE_FALSE(fx.wm->componentInputBlocked());
	clickStage(fx, (tab[0] + tab[2]) / 2.0f, (tab[1] + tab[3]) / 2.0f);
	const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Tab);
}

// Sol review r1 (4): the greyed Show All Health Bars says why (Very Low, or a custom Decal Detail of Off)
TEST_CASE("opts5 options retail: Show All Health Bars greyed by the detail level has a reason in its help line")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = VeryLow\n");
	AptOptionsScreen *options = openMenu(fx, false);
	std::vector<OpenBFMEPageRow> rows = options->retailRows();
	const std::u16string own = gameText(fx, "TOOLTIP:Options/tooltipEnableHealthBars");
	CHECK_FALSE(rowOf(rows, "Retail.HealthBars").enabled);
	CHECK(rowOf(rows, "Retail.HealthBars").help == own + u". This detail level already shows every health bar.");
	// a level that allows it: no reason; a custom Decal Detail Off: the reason again
	std::string error;
	REQUIRE(options->setRetailValue("Retail.Detail", 1, &error)); // GUI:High
	rows = options->retailRows();
	CHECK(rowOf(rows, "Retail.HealthBars").enabled);
	CHECK(rowOf(rows, "Retail.HealthBars").help == own);
	REQUIRE(options->setRetailValue("Retail.AdvancedOption8", 0, &error)); // Decal Detail Off, Custom
	rows = options->retailRows();
	CHECK_FALSE(rowOf(rows, "Retail.HealthBars").enabled);
	CHECK(rowOf(rows, "Retail.HealthBars").help == own + u". This detail level already shows every health bar.");
}

// ---- lane OPTS-6: menus that are exact, modal and usable without a mouse ---------------------------------------------------------------------------
namespace
{
// QuitMenu.apt over a "game" (the context the host passes), loaded as the device loads it (GodotAptPlayer quit_menu: its own screen, not on the stack)
std::unique_ptr<AptQuitMenu> openQuitMenu(ShellFx &fx, QuitMenuContext &context)
{
	context.inGame = true;
	fx.environment.quitMenu = &context;
	auto quit = std::make_unique<AptQuitMenu>(*fx.wm, *fx.shell, fx.environment);
	REQUIRE(quit->level() >= 0); // tolerance: any free slot (the load's answer -1 is the refusal)
	for (int i = 0; i < 30 && !fx.wm->isAptWindowLoaded(quit->level()); ++i)
	{
		fx.tick(1);
	}
	fx.tick(200); // its intro
	return quit;
}

// the quit menu's buttons the pointer can press now (each at its centre)
std::vector<std::string> pressableButtons(ShellFx &fx, int level, std::vector<std::array<float, 2>> *centres = nullptr)
{
	std::vector<std::string> out;
	std::vector<AptButtonInst *> all;
	std::function<void(AptCharacterInst *)> walk = [&](AptCharacterInst *c) {
		if (!c)
		{
			return;
		}
		if (AptButtonInst *b = c->asButton())
		{
			all.push_back(b);
		}
		if (AptSpriteInst *sp = c->asSprite())
		{
			for (AptCharacterInst *k : sp->children())
			{
				walk(k);
			}
		}
	};
	walk(fx.wm->apt().level(level));
	for (AptButtonInst *b : all)
	{
		float x0, y0, x1, y1, x, y;
		if (b->contentBounds(x0, y0, x1, y1))
		{
			b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
			if (fx.wm->apt().input().hitTestButtons(x, y) == b)
			{
				out.push_back(b->targetPath());
				if (centres)
				{
					centres->push_back({ x, y });
				}
			}
		}
	}
	return out;
}

} // namespace

// The owner (2026-10-15): the quit menu's SAVE / LOAD / RESTART / EXIT / RESUME took clicks through the OpenBFME settings opened from it. RotWK pushes
// QuitMenu.apt on the shell (RW 0x921B0F -> Shell::push RW 0x75E3B7), so Options' push (RW 0x91ED91) shuts it down and Options' pop runs its init again.
TEST_CASE("opts6 shell retail: Options opened from the quit menu hides it and takes its focus; closing Options shows it and gives the focus back")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	fx.environment.openbfmeOptionsMenu = true;
	fx.tick(2);
	QuitMenuContext context;
	std::unique_ptr<AptQuitMenu> quit = openQuitMenu(fx, context);
	const int quitLevel = quit->level();
	std::vector<std::array<float, 2>> centres;
	const std::vector<std::string> buttons = pressableButtons(fx, quitLevel, &centres);
	REQUIRE(buttons.size() == 6); // RotWK's QuitMenu.apt: OPTIONS, SAVE, LOAD, RESTART, EXIT, RESUME GAME
	fx.shell->setOverlay(quit.get()); // the device's quit_menu open
	CHECK(fx.wm->focusedLevel() == quitLevel);
	AptOptionsScreen::open(*fx.shell, false, false, false, false); // the quit menu's OptionsScreen (RW 0x921783)
	fx.tick(60);
	AptOptionsScreen *options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
	REQUIRE(options);
	CHECK(quit->isHidden());
	CHECK(quit->movieUnloaded()); // RW 0x812A37 -> AptPlayer::HideLevel: the movie leaves its level, the screen stays (lane SAVE-UI1)
	CHECK(fx.wm->apt().level(quitLevel) == nullptr);
	CHECK(pressableButtons(fx, quitLevel).empty());
	CHECK(fx.wm->focusedLevel() == options->level());
	CHECK(fx.shell->inputTop() == options);
	CHECK(fx.wm->backgroundMode() == 1); // RW 0x91EE08: the front-end background over the game
	// a click where every quit menu button was: none of its commands runs
	const int restart = quit->commandCalls("AptQuitMenu::RestartMission"), exit = quit->commandCalls("AptQuitMenu::ExitMission");
	const int resume = quit->commandCalls("AptQuitMenu::ReturnToGame"), save = quit->commandCalls("AptQuitMenu::SaveMenu");
	for (const auto &c : centres)
	{
		clickStage(fx, c[0], c[1]);
	}
	REQUIRE(fx.shell->top() == options); // nothing closed Options either
	fx.shell->pop();
	fx.tick(30);
	CHECK(fx.shell->top() == nullptr);
	CHECK_FALSE(quit->isHidden());
	CHECK(fx.wm->focusedLevel() == quitLevel);
	CHECK(fx.shell->inputTop() == quit.get());
	CHECK(fx.wm->noteCount("shell-overlay-return") == 1); // S-4980
	// RW 0x812913 -> AptPlayer::ShowLevel: the movie loads again into the same level and starts over; every button is back (the review of OPTS-6: the
	// OPTIONS button stayed in its pressed state, and a second visit failed)
	fx.tick(30);
	CHECK(quit->level() == quitLevel);
	CHECK(fx.wm->apt().level(quitLevel) != nullptr);
	fx.tick(200); // its intro
	CHECK(fx.wm->apt().level(quitLevel)->visible);
	CHECK(pressableButtons(fx, quitLevel) == buttons);
	CHECK(quit->commandCalls("AptQuitMenu::RestartMission") == restart);
	CHECK(quit->commandCalls("AptQuitMenu::ExitMission") == exit);
	CHECK(quit->commandCalls("AptQuitMenu::ReturnToGame") == resume);
	CHECK(quit->commandCalls("AptQuitMenu::SaveMenu") == save);
	// the quit menu closes (the device's quit_menu close): its focus goes, nothing of it stays on the shell
	fx.shell->clearOverlay();
	CHECK(fx.shell->inputTop() == nullptr);
	CHECK(fx.wm->focusedLevel() != quitLevel);
}

// The capture alone (no overlay: a screen under Options that is not the shell's, such as the Palantir over a game) hides the levels under Options and
// keeps the pointer from them; the background movie stays (Options shows it on purpose); Options' end shows exactly those levels again.
TEST_CASE("opts6 shell retail: Options takes the pointer from every level under it and hides them until it closes")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	fx.environment.openbfmeOptionsMenu = true;
	fx.tick(2);
	REQUIRE(fx.wm->loadBackground()); // the device's (GameClient::init RW 0x646771): under everything; Options shows it, the capture keeps it
	QuitMenuContext context;
	std::unique_ptr<AptQuitMenu> under = openQuitMenu(fx, context); // a level under Options that is not on the shell (no setOverlay)
	const std::vector<std::string> before = pressableButtons(fx, under->level());
	REQUIRE(before.size() == 6);
	AptOptionsScreen::open(*fx.shell, false, false, false, false);
	fx.tick(60);
	AptOptionsScreen *options = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
	REQUIRE(options);
	REQUIRE(under->level() < options->level()); // tolerance: the order of the two levels is the property (the screen under Options)
	CHECK(fx.wm->levelsHiddenByCapture() == std::vector<int>{ under->level() });
	CHECK(fx.wm->apt().input().inputFloor() == options->level());
	CHECK(fx.wm->noteCount("capture-hides") == 1); // S-4982
	CHECK_FALSE(fx.wm->apt().level(under->level())->visible);
	CHECK(pressableButtons(fx, under->level()).empty());
	REQUIRE(fx.wm->backgroundLevel() >= 0); // tolerance: loaded (any slot)
	REQUIRE(fx.wm->backgroundLevel() < options->level()); // tolerance: the order is the property (under Options)
	CHECK(std::find(fx.wm->levelsHiddenByCapture().begin(), fx.wm->levelsHiddenByCapture().end(), fx.wm->backgroundLevel()) == fx.wm->levelsHiddenByCapture().end());
	fx.shell->pop();
	fx.tick(30);
	CHECK(fx.wm->levelsHiddenByCapture().empty());
	CHECK(fx.wm->apt().input().inputFloor() == -1);
	CHECK(fx.wm->apt().level(under->level())->visible);
	CHECK(pressableButtons(fx, under->level()) == before);
}

// The owner (2026-10-15, 2560 x 1440): "the menu is scaled so your mouse can go way off to the left or right on some of the sliders". A window larger
// than the stage gives pointer moves under one stage pixel; each one's truncated difference was 0, so a dragged thumb stayed behind the pointer.
TEST_CASE("opts6 gadget layer retail: a thumb dragged in moves of a quarter stage pixel follows the pointer to the pixel")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	OpenBFMEOptionsPage *page = showRetail(options, { "Retail.MusicVolume" });
	GameWindow *slider = page->gadget("Retail.MusicVolume");
	REQUIRE(slider);
	REQUIRE(page->choose("Retail.MusicVolume", 0, nullptr));
	GameWindow *thumb = GadgetSliderGetThumb(slider);
	int sx = 0, sy = 0, tx0 = 0, ty0 = 0, tw = 0, th = 0;
	slider->winGetScreenPosition(&sx, &sy);
	thumb->winGetScreenPosition(&tx0, &ty0);
	thumb->winGetSize(&tw, &th);
	// press on the thumb's middle, move right 80 times by 0.25 stage pixels (20 pixels), let go
	const float x = (float)tx0 + 6.0f, y = (float)ty0 + (float)th / 2.0f;
	fx.wm->postMouseMove(x, y);
	fx.wm->postMouseButton(true);
	for (int i = 1; i <= 80; ++i)
	{
		fx.wm->postMouseMove(x + 0.25f * (float)i, y);
	}
	int tx1 = 0, ty1 = 0;
	thumb->winGetScreenPosition(&tx1, &ty1);
	CHECK(tx1 - tx0 == 20); // the pixels the pointer crossed (ZH: the grabbed window moves by the pointer's pixel steps)
	fx.wm->postMouseButton(false);
	CHECK(GadgetSliderGetPosition(slider) > 0); // tolerance: the value the 20 pixels mean is the slider's arithmetic (checkThumb), here only that it moved
}

// Hit areas equal to the drawn shapes on the OpenBFME page: the combo box's button no longer hangs under its box, a check box takes no press right of
// its drawn box, a tab takes presses on its drawn bar only. The drawn shapes are read from the gadget layer's draw list (what the device draws).
TEST_CASE("opts6 options page retail: each control's window is the shape it draws (combo box button, check box, tab bar)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	OpenBFMEOptionsPage *page = showRetail(options, { "Retail.Detail", "Retail.HealthBars" });
	fx.tick(4);
	GadgetDrawList list;
	fx.layer->buildDrawList(list);
	auto drawnBox = [&](GameWindow *root, bool images) {
		std::array<int, 4> b{ 1 << 20, 1 << 20, -(1 << 20), -(1 << 20) };
		for (const GadgetDrawCommand &c : list.commands)
		{
			if (c.root == root && (!images || c.kind == GadgetDrawCommand::Kind::Image))
			{
				b = { std::min(b[0], c.x0), std::min(b[1], c.y0), std::max(b[2], c.x1), std::max(b[3], c.y1) };
			}
		}
		return b;
	};
	auto windowBox = [](GameWindow *w) {
		int x = 0, y = 0, cw = 0, ch = 0;
		w->winGetScreenPosition(&x, &y);
		w->winGetSize(&cw, &ch);
		return std::array<int, 4>{ x, y, x + cw, y + ch };
	};
	GameWindow *combo = page->gadget("Retail.Detail");
	REQUIRE(combo);
	const std::array<int, 4> comboDrawn = drawnBox(combo, true), comboWindow = windowBox(combo);
	CHECK(comboDrawn[1] == comboWindow[1]); // the drop-down button as high as the box (OPTS-5: 4 stage pixels under it)
	CHECK(comboDrawn[3] == comboWindow[3]);
	GameWindow *box = page->gadget("Retail.HealthBars");
	REQUIRE(box);
	const std::array<int, 4> boxDrawn = drawnBox(box, true), boxWindow = windowBox(box);
	CHECK(boxDrawn[0] == boxWindow[0]);
	CHECK(boxDrawn[2] == boxWindow[2]); // as wide as the box it draws
	CHECK(boxDrawn[1] - boxWindow[1] == OpenBFMEOptionsPage::kCheckBoxMargin); // the stated margin above and below
	CHECK(boxWindow[3] - boxDrawn[3] == OpenBFMEOptionsPage::kCheckBoxMargin);
	// a press right of the box: nothing; on it: ticked
	const bool was = GadgetCheckBoxIsChecked(box);
	clickStage(fx, (float)boxWindow[2] + 2.0f, (float)(boxWindow[1] + boxWindow[3]) / 2.0f);
	CHECK(GadgetCheckBoxIsChecked(box) == was);
	fx.tick(30); // past the double click time
	clickStage(fx, (float)boxWindow[2] - 2.0f, (float)(boxWindow[1] + boxWindow[3]) / 2.0f);
	CHECK(GadgetCheckBoxIsChecked(box) != was);
	// the tab bar: the drawn selection image of the chosen tab (decorate) is the bar; a press one pixel left of it is no tab click
	std::array<int, 4> bar{ 0, 0, 0, 0 };
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.root == nullptr && c.kind == GadgetDrawCommand::Kind::Image && c.image == "AptListBoxHiliteSelectedItem")
		{
			bar = { c.x0, c.y0, c.x1, c.y1 };
		}
	}
	REQUIRE(bar[2] > bar[0]); // tolerance: the bar was found (drawn)
	options->takeExtensionEvents();
	const float tabY = (float)(bar[1] + bar[3]) / 2.0f + (float)OpenBFMEOptionsPage::kTabPitch; // the next tab, as tall as the drawn bar
	clickStage(fx, (float)bar[0] - 1.0f, tabY);
	CHECK(options->takeExtensionEvents().empty());
	clickStage(fx, (float)bar[2] + 1.0f, tabY);
	CHECK(options->takeExtensionEvents().empty());
	clickStage(fx, (float)bar[0] + 1.0f, tabY);
	const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Tab);
	CHECK(events[0].value == 1);
}

// Keyboard / controller navigation (not retail; MenuNavigator.h) on the OpenBFME page: the first key only shows the focus (the current tab), Tab walks
// the tabs and rows, Right moves a slider by a twentieth of its range through the slider's own messages, Space ticks a box, PageDown is the next tab,
// Back is CANCEL; another screen starts without the old focus.
TEST_CASE("opts6 menu navigation retail: the OpenBFME page by keys")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\nMusicVolume = 40.000000\n");
	AptOptionsScreen *options = openMenu(fx, false);
	OpenBFMEOptionsPage *page = showRetail(options, { "Retail.MusicVolume", "Retail.HealthBars" });
	fx.tick(4);
	options->takeExtensionEvents();
	MenuNavigator nav(*fx.wm);
	nav.setMenu(options->level(), options);
	CHECK(nav.focusId().empty());
	CHECK_FALSE(nav.key(MenuNavKey::Activate)); // nothing focused: Enter is the screen's own
	REQUIRE(nav.key(MenuNavKey::Next));
	CHECK(nav.focusId() == "tab:0"); // the page's first: its tab
	CHECK(fx.wm->noteCount("menu-navigation") == 1); // S-4981
	CHECK(options->takeExtensionEvents().empty()); // showing the focus does nothing
	// Tab to the music slider: the reading order of the page's targets (tabs on the left, the rows, the bottom bar)
	for (int i = 0; i < 12 && nav.focusId() != "row:Retail.MusicVolume"; ++i)
	{
		REQUIRE(nav.key(MenuNavKey::Next));
	}
	REQUIRE(nav.focusId() == "row:Retail.MusicVolume");
	const int before = GadgetSliderGetPosition(page->gadget("Retail.MusicVolume"));
	REQUIRE(nav.key(MenuNavKey::Right));
	int lo = 0, hi = 0;
	GadgetSliderGetMinMax(page->gadget("Retail.MusicVolume"), &lo, &hi);
	const int step = (hi - lo) / MenuNavigator::kSliderSteps;
	CHECK(step == 5); // 0 .. 100 (Options.apt's volume sliders)
	CHECK(GadgetSliderGetPosition(page->gadget("Retail.MusicVolume")) == before + step);
	// the effect: the original game's own Music slider (the hidden gadget the row drives, Save writes) moved with it, through RotWK's handler
	CHECK(GadgetSliderGetPosition(options->gadget("MusicVolume")) == before + step);
	std::vector<OpenBFMEPageEvent> events;
	// the focus frame is drawn around it, over everything
	GadgetDrawList list;
	nav.appendFocusFrame(list);
	REQUIRE(list.commands.size() == 2);
	std::array<int, 4> r{};
	REQUIRE(page->rect("Retail.MusicVolume", r));
	CHECK(list.commands[0].x0 == r[0] - 2);
	CHECK(list.commands[0].y1 == r[3] + 2);
	CHECK(list.commands[0].color == MenuNavigator::kFrameColor);
	// Down to the box, Space ticks it through a click there
	const bool ticked = GadgetCheckBoxIsChecked(page->gadget("Retail.HealthBars"));
	REQUIRE(nav.key(MenuNavKey::Down));
	REQUIRE(nav.focusId() == "row:Retail.HealthBars");
	REQUIRE(nav.key(MenuNavKey::Activate));
	fx.tick(2);
	CHECK(GadgetCheckBoxIsChecked(page->gadget("Retail.HealthBars")) != ticked);
	options->takeExtensionEvents();
	// PageDown: the tab after; Back: CANCEL
	REQUIRE(nav.key(MenuNavKey::PageNext));
	events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Tab);
	CHECK(events[0].value == 1);
	REQUIRE(nav.key(MenuNavKey::Back));
	events = options->takeExtensionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Cancel);
	// the pointer moved: the frame goes, the focus stays; another screen: no focus
	nav.pointerMoved();
	CHECK_FALSE(nav.frameShown());
	CHECK(nav.focusId() == "row:Retail.HealthBars");
	int other = 0;
	nav.setMenu(options->level(), &other);
	CHECK(nav.focusId().empty());
}

// ---- lane SHELLFIX-1: the Options menu in both main menu styles (the owner's v0.4.9 reports at 2560 x 1440) ---------------------------------------
namespace
{
// the movie's own ACCEPT button: the first button under the visible clip ".Accept" (found from the movie's tree, not through the screen's code)
AptButtonInst *movieAccept(ShellFx &fx, AptOptionsScreen *options)
{
	return firstButtonUnder(findClip(fx.wm->apt().level(options->level()), "Accept"));
}

// The OpenBFME menu's ACCEPT in one movie: the updates from the button's release until the main menu is on top again, the device acting on the page's
// Accept event as game.gd does (openbfme_options_screen.gd close(true): AptMenuPlayer.openbfme_menu_finish -> finishMenu)
int menuAcceptUpdates(AptRetail &mount, const std::string &buttonName)
{
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	fx.tick(30);
	options->takeExtensionEvents(); // the opening's Entry
	AptButtonInst *accept = movieAccept(fx, options);
	REQUIRE(accept);
	CHECK(accept->instName() == buttonName);
	clickButton(fx, accept);
	int updates = 0;
	bool finished = false;
	for (; updates < 120 && fx.shell->top() && fx.shell->top()->filename() == "Options.apt"; ++updates)
	{
		if (!finished)
		{
			for (const OpenBFMEPageEvent &e : options->takeExtensionEvents())
			{
				if (e.kind == OpenBFMEPageEvent::Kind::Accept)
				{
					options->finishMenu(true);
					finished = true;
				}
			}
		}
		fx.tick(1);
	}
	REQUIRE(fx.shell->top());
	CHECK(fx.shell->top()->filename() == "MainMenu.apt");
	return updates;
}
} // namespace

// Before: the page took the bottom bar's clicks only from a button named "bttn" (RotWK 2.01's Options.apt). BFME2 1.06's movie leaves it unnamed,
// so in the BFME2 style the click reached the movie, which played its closing animation before it sent AptOptions::Save: ACCEPT came back 0.55 s late
// (measured in the game: 33 frames at 60 Hz) where the RotWK style took 2 frames. Lane MENUSND-1: the bar click is the page's only once the movie's
// input has drained the press (the movie step that plays the button's click, test "menusnd1 options retail" above), so ACCEPT takes that step, then one
// update: 2 in both styles.
TEST_CASE("shellfix1 options retail: the OpenBFME menu's ACCEPT goes back to the main menu after the press's movie step in both styles' Options.apt")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	CHECK(menuAcceptUpdates(mount, "bttn") == 2);
	retailtest::Mount *bfme2 = retailtest::bfme2Mount();
	REQUIRE(bfme2);
	REQUIRE_MESSAGE(bfme2->fs, bfme2->error);
	AptRetail bfme2Only{ *bfme2->fs };
	REQUIRE(bfme2Only.fs.getArchiveFilenameForFile("Options.apt").find("options.big") != std::string::npos); // BFME2 1.06's own movie
	CHECK(menuAcceptUpdates(bfme2Only, "") == 2); // an unnamed button (the player's list_buttons calls it "instance14")
}

// Retail's own ACCEPT (the reference for the menu's): RotWK 2.01's Options.apt plays its closing animation, then sends AptOptions::Save (RW 0x91FC9C),
// whose close (RW 0x91EA39) only raises the shell's pop request (Shell + 0x310 = 1, RW 0x62215B): the main menu is back on the next shell update. The
// updates are the movie's 30 Hz frames (33 ms each): 18 updates, 0.6 s. The OpenBFME menu takes the click before the movie sees it (mouseFilter), so its
// ACCEPT skips that animation (1 update, the test above).
TEST_CASE("shellfix1 options retail: retail's own ACCEPT on RotWK 2.01's Options.apt takes the movie's closing animation, then one update")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "StaticGameLOD = High\n");
	AptOptionsScreen *options = fx.openOptionsFromMainMenu();
	REQUIRE(options);
	fx.tick(30);
	clickButton(fx, movieAccept(fx, options));
	int updates = 0;
	for (; updates < 120 && fx.shell->top() && fx.shell->top()->filename() == "Options.apt"; ++updates)
	{
		fx.tick(1);
	}
	MESSAGE("retail ACCEPT: " << updates << " updates");
	REQUIRE(fx.shell->top());
	CHECK(fx.shell->top()->filename() == "MainMenu.apt");
	CHECK(updates == 18);
}

// BFME2 1.06's Options.apt draws each settings frame's border as an unnamed clip of Main beside the named frame (depths 3, 27, 51; dump_tree of the
// movie); the menu hid only the named frames, so the borders crossed the OpenBFME page (the owner's screenshot). RotWK 2.01's Main has no such clips.
TEST_CASE("shellfix1 options retail: in the BFME2 style no frame border of the retail page shows over the OpenBFME menu")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	retailtest::Mount *bfme2 = retailtest::bfme2Mount();
	REQUIRE(bfme2);
	REQUIRE_MESSAGE(bfme2->fs, bfme2->error);
	AptRetail bfme2Only{ *bfme2->fs };
	ShellFx fx(bfme2Only, "StaticGameLOD = High\n");
	AptOptionsScreen *options = openMenu(fx, false);
	fx.tick(30);
	AptCharacterInst *main = fx.wm->apt().resolvePath(fx.wm->apt().level(options->level()), "Main");
	REQUIRE(main);
	REQUIRE(main->asSprite());
	for (int depth : { 3, 27, 51 })
	{
		AptCharacterInst *border = main->asSprite()->childAtDepth(depth);
		REQUIRE_MESSAGE(border, "depth " << depth);
		CHECK(border->instName().empty());
		CHECK_FALSE_MESSAGE(border->globallyVisible(), "depth " << depth);
	}
	// the version label's holder (depth 1: "Version 2.01" above ACCEPT) stays
	AptCharacterInst *version = main->asSprite()->childAtDepth(1);
	REQUIRE(version);
	CHECK(version->globallyVisible());
}

// lane MENUSND-1: the bottom bar of the OpenBFME menu (the Options screen as the game opens it) is the movie's own CANCEL / RESET TO DEFAULTS / ACCEPT, whose
// click the page takes (AptOptionsScreen::mouseFilter). RotWK plays the button's click on the press: MenuExport.apt's GenericButton goes to its `_down`
// frame (button action 4), which calls _root.PlaySound("Gui_ShellMapSelect") (FSCommand PlaySound, RW 0x815772); its own click (OnButtonClick) is the
// `_release end` frame. Before the fix the press never reached the movie and the bar was silent. Each press here: exactly one Gui_ShellMapSelect, on the
// press, nothing more on the release (the movie's release and its click do not run), the page's event, and the movie's input free again (a hover after it
// plays the next button's Gui_ShellMapMouseOver)
TEST_CASE("menusnd1 options retail: a press on the OpenBFME menu's bottom bar plays the button's click sound once, on the press; the release stays the page's")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	struct Case
	{
		const char *button;
		OpenBFMEPageEvent::Kind kind;
		bool fast; // press and release posted between two updates
	};
	for (const Case &c : { Case{ "Cancel", OpenBFMEPageEvent::Kind::Cancel, false }, Case{ "Default", OpenBFMEPageEvent::Kind::Reset, false },
			 Case{ "Accept", OpenBFMEPageEvent::Kind::Accept, false }, Case{ "Default", OpenBFMEPageEvent::Kind::Reset, true },
			 Case{ "Accept", OpenBFMEPageEvent::Kind::Accept, true } })
	{
		INFO(c.button << (c.fast ? " (fast click)" : ""));
		ShellFx fx(mount, "");
		AptOptionsScreen *options = openMenu(fx, false);
		AptSpriteInst *root = fx.wm->apt().level(options->level());
		auto centre = [&](const char *name, float &x, float &y) {
			AptButtonInst *b = firstButtonUnder(findClip(root, name));
			REQUIRE_MESSAGE(b, name);
			float x0, y0, x1, y1;
			REQUIRE(b->contentBounds(x0, y0, x1, y1));
			b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		};
		float x, y;
		centre(c.button, x, y);
		fx.wm->postMouseMove(x, y);
		fx.tick(20);
		REQUIRE(fx.services.sounds == std::vector<std::string>{ "Gui_ShellMapMouseOver" }); // the hover, as before
		fx.services.sounds.clear();
		options->takeExtensionEvents();
		fx.wm->postMouseButton(true);
		if (!c.fast)
		{
			fx.tick(2);
			CHECK(fx.services.sounds == std::vector<std::string>{ "Gui_ShellMapSelect" }); // on the press
			CHECK(options->takeExtensionEvents().empty());                                // the press alone does nothing
			REQUIRE(fx.shell->top() == options);
		}
		fx.wm->postMouseButton(false);
		if (c.fast)
		{
			// the movie has not stepped since the press: the click is not the page's yet (the device takes the page's events every frame; a CANCEL or
			// ACCEPT taken now would close the menu before the movie played the press)
			CHECK(options->takeExtensionEvents().empty());
			CHECK(fx.services.sounds.empty());
		}
		fx.tick(1);
		CHECK(fx.services.sounds == std::vector<std::string>{ "Gui_ShellMapSelect" }); // asked for by the time the click is the page's
		const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
		REQUIRE(events.size() == 1);
		CHECK(events[0].kind == c.kind);
		CHECK_FALSE(fx.wm->apt().input().mouseDown());
		fx.tick(60);
		CHECK(fx.services.sounds == std::vector<std::string>{ "Gui_ShellMapSelect" }); // once: the movie's release (and its click) did not run
		REQUIRE(fx.shell->top() == options); // the device acts on the page's events (AptMenuPlayer); none is taken here
		{
			// the movie's input is free: the pointer on the next button plays its hover
			fx.services.sounds.clear();
			centre(std::string(c.button) == "Cancel" ? "Accept" : "Cancel", x, y);
			fx.wm->postMouseMove(x, y);
			fx.tick(20);
			// (the button left plays its rollOut's Gui_ShellMapMouseOut too, as on any move between two buttons)
			CHECK(std::count(fx.services.sounds.begin(), fx.services.sounds.end(), std::string("Gui_ShellMapMouseOver")) == 1);
			CHECK(std::count(fx.services.sounds.begin(), fx.services.sounds.end(), std::string("Gui_ShellMapSelect")) == 0);
		}
		if (c.kind != OpenBFMEPageEvent::Kind::Reset)
		{
			// the device's answer to the page's CANCEL / ACCEPT (openbfme_options_screen.gd: the shell's Cancel / Save, AptOptionsScreen::finishMenu)
			fx.services.sounds.clear();
			options->finishMenu(c.kind == OpenBFMEPageEvent::Kind::Accept);
			fx.tick(60);
			CHECK((fx.shell->top() == nullptr || fx.shell->top()->filename() != "Options.apt"));
			CHECK(std::count(fx.services.sounds.begin(), fx.services.sounds.end(), std::string("Gui_ShellMapSelect")) == 0);
		}
	}
}

// lane MENUSND-1 (review): bar clicks wait for the movie's input (see above); several within one movie step (a slow frame delivers them together) keep
// their order, and a page event made in between (Esc: the page's CANCEL) waits behind the click before it
TEST_CASE("menusnd1 options retail: clicks within one movie step keep their order; each bar press plays its click")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ShellFx fx(mount, "");
	AptOptionsScreen *options = openMenu(fx, false);
	AptSpriteInst *root = fx.wm->apt().level(options->level());
	auto centre = [&](const char *name, float &x, float &y) {
		AptButtonInst *b = firstButtonUnder(findClip(root, name));
		REQUIRE_MESSAGE(b, name);
		float x0, y0, x1, y1;
		REQUIRE(b->contentBounds(x0, y0, x1, y1));
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	};
	float rx, ry, ax, ay;
	centre("Default", rx, ry);
	centre("Accept", ax, ay);
	fx.wm->postMouseMove(rx, ry);
	fx.tick(20);
	options->takeExtensionEvents();
	fx.services.sounds.clear();
	// one batch: RESET TO DEFAULTS clicked, Esc, the pointer to ACCEPT, ACCEPT clicked
	fx.wm->postMouseButton(true);
	fx.wm->postMouseButton(false);
	options->extensionCancel();
	fx.wm->postMouseMove(ax, ay);
	fx.wm->postMouseButton(true);
	fx.wm->postMouseButton(false);
	CHECK(options->takeExtensionEvents().empty()); // nothing before the movie has the presses
	fx.tick(1);
	const std::vector<OpenBFMEPageEvent> events = options->takeExtensionEvents();
	REQUIRE(events.size() == 3);
	CHECK(events[0].kind == OpenBFMEPageEvent::Kind::Reset);
	CHECK(events[1].kind == OpenBFMEPageEvent::Kind::Cancel);
	CHECK(events[2].kind == OpenBFMEPageEvent::Kind::Accept);
	CHECK(std::count(fx.services.sounds.begin(), fx.services.sounds.end(), std::string("Gui_ShellMapSelect")) == 2);
	CHECK_FALSE(fx.wm->apt().input().mouseDown());
	// and a later page event no longer waits
	options->extensionCancel();
	const std::vector<OpenBFMEPageEvent> later = options->takeExtensionEvents();
	REQUIRE(later.size() == 1);
	CHECK(later[0].kind == OpenBFMEPageEvent::Kind::Cancel);
}
