// OpenBFME unit tests. GPL-3.0.
// Lane UI-2: the player's Options.ini (ZH UserPreferences as RotWK's OptionPreferences) and the Options screen's OpenBFME "Soft particles" box (owner
// decision: soft by default, the retail look one click away), saved by AptOptions::Save with the other entries of the file. The retail Save / Cancel
// close the screen (RW 0x91EA39 -> 0x62215B). The retail-data test SKIPs loudly without the installs.

#include "doctest.h"
#include "AptRetail.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/OptionPreferences.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

TEST_CASE("ui2 OptionPreferences: ZH UserPreferences' key = value lines, sorted write, retail yes / no, every entry kept; SoftParticles defaults to yes")
{
	OptionPreferences p;
	p.parse("Resolution = 1024 768\r\n  UseEAX3=no\n= \n=Brightness = 50\nNoValue =\njunk line\nAllHealthBars = YES\n");
	CHECK(p.get("Resolution") == "1024 768");
	CHECK(p.get("UseEAX3") == "no");
	CHECK(p.get("Brightness") == "50"); // AsciiString::nextToken skips the leading '='
	CHECK_FALSE(p.has("NoValue"));      // an empty value is skipped
	CHECK_FALSE(p.has("junk line"));
	CHECK(p.getYesNo("AllHealthBars", false));
	CHECK_FALSE(p.getYesNo("UseEAX3", true));
	CHECK(p.softParticles()); // absent: soft
	p.setSoftParticles(false);
	CHECK(p.get(OptionPreferences::kSoftParticles) == "no");
	CHECK(p.text() == "AllHealthBars = YES\nBrightness = 50\nResolution = 1024 768\nSoftParticles = no\nUseEAX3 = no\n");
	OptionPreferences q;
	q.parse(p.text());
	CHECK(q.values() == p.values());
}

TEST_CASE("ui2 retail: Options.apt shows the Soft particles box; Save writes Options.ini (other entries kept), applies it and closes the screen")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const std::string file = (std::filesystem::temp_directory_path() / "openbfme-ui2-options-test.ini").string();
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << "Resolution = 1024 768\nSoftParticles = yes\n";
	}
	OptionPreferences prefs;
	REQUIRE(prefs.load(file));
	AptArchiveFileSource source(mount.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	environment.options = &prefs;
	environment.optionsFile = file;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
	auto wm = std::make_unique<WindowManager>(source, services);
	auto layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
	layer->registerComponents();
	auto shell = std::make_unique<Shell>(*wm, factories, services, environment);
	wm->setShell(shell.get());
	wm->init();
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	};
	tick(2);
	shell->push("MainMenu.apt");
	tick(5);
	shell->push("Options.apt");
	tick(30);
	AptOptionsScreen *options = dynamic_cast<AptOptionsScreen *>(shell->top());
	REQUIRE(options);
	GameWindow *box = options->softParticlesBox();
	REQUIRE(box);
	CHECK(GadgetCheckBoxIsChecked(box)); // the file says yes
	CHECK_FALSE(box->winIsHidden());
	int x, y;
	box->winGetPosition(&x, &y);
	CHECK(x == AptOptionsScreen::kSoftBoxX);
	CHECK(y == AptOptionsScreen::kSoftBoxY);
	// a click on the box toggles it and tells the screen (GBM_SELECTED to the owner)
	box->winGetScreenPosition(&x, &y);
	wm->postMouseMove((float)x + 5.0f, (float)y + 5.0f);
	tick(1);
	wm->postMouseButton(true);
	tick(1);
	wm->postMouseButton(false);
	tick(1);
	CHECK_FALSE(GadgetCheckBoxIsChecked(box));
	CHECK_FALSE(options->pendingSoftParticles());
	// Save: the file keeps Resolution and gets SoftParticles = no; the device is told; the screen closes (back to the main menu)
	REQUIRE(wm->invokeCallback("AptOptions::Save", ""));
	tick(30);
	std::ifstream in(file, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	CHECK(text.str() == "Resolution = 1024 768\nSoftParticles = no\n");
	REQUIRE_FALSE(services.appliedOptions.empty());
	CHECK(services.appliedOptions.back() == std::make_pair(std::string("SoftParticles"), std::string("no")));
	REQUIRE(shell->top());
	CHECK(shell->top()->filename() == "MainMenu.apt");
	bool noted = false;
	for (const WindowManagerNote &n : wm->notes())
	{
		noted = noted || (n.kind == "options-soft-particles" && n.detail.rfind("[S-1484]", 0) == 0);
	}
	CHECK(noted);
	shell.reset();
	layer.reset();
	std::remove(file.c_str());
}
