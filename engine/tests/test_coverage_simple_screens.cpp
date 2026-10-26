// Synthetic no-game coverage tests. GPL-3.0.
#include "AptPlayerTestUtil.h"

#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/TributeInfo.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/OptionPreferences.h"

#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using namespace apttest;

std::string blankDrawData(const char *key)
{
	std::string text = std::string("  ") + key + " = ";
	for (int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		text += std::string(i ? "                    " : "") + "IMAGE: NoImage, COLOR: 255 255 255 0, BORDERCOLOR: 255 255 255 0" +
			(i + 1 == MAX_DRAW_DATA ? ";\n" : ",\n");
	}
	return text;
}

std::string syntheticSkin(const std::string &kind)
{
	const bool slider = kind == "HORZSLIDER";
	const std::string extra = slider ? "  SLIDERDATA = MINVALUE: 0,\n               MAXVALUE: 100;\n" : "  TEXT = \"APT:Null\";\n";
	const std::string prefix = "FILE_VERSION = 2;\nSTARTLAYOUTBLOCK\n  LAYOUTINIT = [None];\n  LAYOUTUPDATE = [None];\n  LAYOUTSHUTDOWN = [None];\nENDLAYOUTBLOCK\nWINDOW\n"
		"  WINDOWTYPE = " + kind + ";\n  SCREENRECT = UPPERLEFT: 0 0,\n               BOTTOMRIGHT: 120 30,\n               CREATIONRESOLUTION: 1024 768;\n"
		"  NAME = \"Synthetic.wnd:\";\n  STATUS = ENABLED+IMAGE;\n  STYLE = " + (slider ? "HORZSLIDER+MOUSETRACK" : "CHECKBOX+MOUSETRACK") +
		";\n  SYSTEMCALLBACK = \"Apt:None\";\n  INPUTCALLBACK = \"Apt:None\";\n  TOOLTIPCALLBACK = \"Apt:None\";\n"
		"  DRAWCALLBACK = \"Apt:None\";\n  FONT = NAME: \"Albertus MT\", SIZE: 10, BOLD: 0;\n"
		"  HEADERTEMPLATE = \"AptGadgets\";\n  TEXTCOLOR = ENABLED: 255 255 255 255, ENABLEDBORDER: 0 0 0 255,\n"
		"    DISABLED: 192 192 192 255, DISABLEDBORDER: 64 64 64 255, HILITE: 92 148 47 255, HILITEBORDER: 0 0 0 255;\n";
	std::string data = prefix + extra;
	for (const char *key : { "ENABLEDDRAWDATA", "DISABLEDDRAWDATA", "HILITEDRAWDATA" })
	{
		data += blankDrawData(key);
	}
	if (slider)
	{
		for (const char *key : { "SLIDERTHUMBENABLEDDRAWDATA", "SLIDERTHUMBDISABLEDDRAWDATA", "SLIDERTHUMBHILITEDRAWDATA" })
		{
			data += blankDrawData(key);
		}
	}
	return data + "END\n";
}

struct SyntheticGadget
{
	std::string id, type, init;
};

// No retail paths are read: every named screen is a real, one-frame APT/const pair
// assembled into MemorySource. The loading movie additionally implements SetBarTo.
struct ScreensFx
{
	MemorySource source;
	RecordingShellServices services;
	ShellEnvironment env;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;

	ScreensFx()
	{
		for (const char *name : { "AptLevel0", "LoadScreen", "PlayerTribute", "Options", "Background", "GuiFX" })
		{
			TestMovie movie;
			if (std::string(name) == "LoadScreen")
			{
				const auto script = program(movie, [](Asm &a) {
					a.defineFunction("SetBarTo", { "card", "percent" }, [](Asm &f) { fscmd(f, "SyntheticBarCalled"); });
				});
				movie.setRootFrames({ { movie.addActionItem(script) } });
			}
			else
			{
				movie.setRootFrames({ {} });
			}
			source.add(name, movie);
		}
		wm = std::make_unique<WindowManager>(source, services);
		wm->registerCommand("SyntheticBarCalled", [](const std::string &) {});
		shell = std::make_unique<Shell>(*wm, factories, services, env);
		factories.registerFactory("LoadScreen.apt", [](AptScreenContext &c) {
			return std::make_unique<AptLoadScreen>(c.windows, c.shell, c.environment);
		});
		factories.registerFactory("PlayerTribute.apt", [](AptScreenContext &c) {
			return std::make_unique<AptPlayerTribute>(c.windows, c.shell, c.services, c.environment);
		});
		factories.registerFactory("Options.apt", [](AptScreenContext &c) {
			return std::make_unique<AptOptionsScreen>(c.windows, c.shell, c.environment, c.services);
		});
		factories.registerFactory("Background.apt", [](AptScreenContext &c) {
			return std::make_unique<AptBackgroundScreen>(c.windows, c.shell);
		});
		factories.registerFactory("GuiFX.apt", [](AptScreenContext &c) {
			return std::make_unique<AptGuiFXScreen>(c.windows, c.shell);
		});
		wm->init();
	}
	// Place retail-style placeholders in the synthetic movie; the real gadget layer
	// reads synthetic WND skins and resolves _Init through the screen's public ref.
	void addGadgets(const std::string &movieName, const std::vector<SyntheticGadget> &controls)
	{
		if (!layer)
		{
			skins.headers.addTemplate({ "AptGadgets", "Albertus MT", 14, false });
			for (const auto &kind : { std::pair<const char *, const char *>{ "horzslider", "HORZSLIDER" },
					 std::pair<const char *, const char *>{ "checkbox", "CHECKBOX" } })
			{
				const std::string skin = syntheticSkin(kind.second);
				source.files[std::string("window/apt/") + kind.first + ".wnd"] = { skin.begin(), skin.end() };
			}
			layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
			layer->registerComponents();
		}
		TestMovie movie;
		movie.addCharacter(0);
		const auto shape = movie.addCharacter(movie.addShape(0, 0, 120, 30, 1));
		const auto sprite = movie.addCharacter(movie.addSprite({ { movie.addPlaceItem(placeChar(shape, 1)) } }));
		std::vector<std::uint32_t> items;
		int depth = 1;
		for (const SyntheticGadget &control : controls)
		{
			auto place = placeChar(sprite, depth++, control.id);
			const auto script = program(movie, [&](Asm &a) {
				a.pushString("_type").setStringVar(control.type);
				a.pushString("_Load").setStringVar(std::string("Apt/") + (control.type == "HorzSlider" ? "horzslider" : "checkbox") + ".wnd");
				a.pushString("_Init").setStringVar(control.init);
			});
			place.flags |= APT_PLACE_HASCLIPACTION;
			place.events.push_back({ APT_CLIP_CONSTRUCT, 0, script });
			items.push_back(movie.addPlaceItem(place));
		}
		movie.setRootFrames({ items });
		source.add(movieName, movie);
	}
	void tick(int count = 2)
	{
		for (int i = 0; i < count; ++i)
		{
			wm->update(33);
		}
	}
	template <class T> T &push(const char *movie)
	{
		shell->push(movie);
		auto *screen = dynamic_cast<T *>(shell->top());
		REQUIRE(screen != nullptr);
		tick();
		REQUIRE(wm->isAptWindowLoaded(screen->level()));
		return *screen;
	}
	std::string externValue(const std::string &key)
	{
		std::string value;
		CHECK(wm->getExtern(key, value) == AptExternResult::Value);
		return value;
	}
};

struct TributeRecorder : TributeSource
{
	bool active = true, allowed = false;
	std::vector<TributePlayerRow> players;
	std::vector<std::tuple<int, int, std::uint32_t>> sent;
	bool localActive() const override { return active; }
	bool transferAllowed() const override { return allowed; }
	std::vector<TributePlayerRow> tributePlayers() override { return players; }
	void send(int from, int to, std::uint32_t amount) override { sent.emplace_back(from, to, amount); }
};

bool hasName(const std::vector<std::string> &names, const std::string &name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}
} // namespace

TEST_CASE("coverage1: loading screen populates occupied and vacant cards with live colour and mode providers")
{
	ScreensFx fx;
	LoadScreenInfo info;
	info.gameLoadingType = 2;
	info.cards[0].occupied = true;
	info.cards[0].playerName = u"Player";
	info.cards[0].armyName = u"Army";
	info.cards[0].color = 0x00A1B2C3u;
	info.cards[0].levelImage = "RankOne";
	info.cards[1].occupied = true;
	info.cards[1].human = true;
	info.cards[1].playerName = u"Second";
	info.cards[1].levelImage = "RankTwo";
	fx.env.loadScreen = &info;
	auto &screen = fx.push<AptLoadScreen>("LoadScreen.apt");
	CHECK(screen.cardCount() == 2);
	REQUIRE(fx.wm->aptText("LoadingScreen::PlayerName0") != nullptr);
	CHECK(*fx.wm->aptText("LoadingScreen::PlayerName0") == "Player");
	REQUIRE(fx.wm->aptText("LoadingScreen::ArmyName0") != nullptr);
	CHECK(*fx.wm->aptText("LoadingScreen::ArmyName0") == "Army");
	REQUIRE(fx.wm->aptText("LoadingScreen::PlayerName2") != nullptr);
	CHECK(*fx.wm->aptText("LoadingScreen::PlayerName2") == " ");
	REQUIRE(fx.wm->aptImage("UIClip/Level/0") != nullptr);
	CHECK(*fx.wm->aptImage("UIClip/Level/0") == "RankOne");
	CHECK(fx.wm->aptImage("UIClip/Level/2") == nullptr);
	CHECK(fx.wm->aptImage("UIClip/Fellowship/0") == nullptr);
	CHECK(fx.externValue("GameLoadingType") == "_skirmish");
	CHECK(fx.externValue("GameLoading:PlayerColor:0") == std::to_string((std::int32_t)0xFFA1B2C3u));
	std::string value;
	CHECK(fx.wm->getExtern("GameLoading:PlayerColor:2", value) == AptExternResult::Undefined);
	info.gameLoadingType = 1;
	CHECK(fx.externValue("GameLoadingType") == "_lan");
	info.gameLoadingType = 5;
	CHECK(fx.externValue("GameLoadingType") == "_internetAdv");
	info.gameLoadingType = 42;
	CHECK(fx.wm->getExtern("GameLoadingType", value) == AptExternResult::Undefined);
	CHECK(screen.progressCalls() == std::vector<std::pair<int, int>>{ { 0, 0 }, { 1, 0 } });
}

TEST_CASE("coverage1: loading progress queues before a synthetic movie is loaded and targets the local card")
{
	ScreensFx fx;
	LoadScreenInfo info;
	info.localCard = 3;
	info.cards[3].occupied = true;
	info.cards[3].playerName = u"Local";
	fx.env.loadScreen = &info;
	fx.shell->push("LoadScreen.apt");
	auto *screen = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(screen != nullptr);
	CHECK_FALSE(screen->setLocalProgress(37));
	CHECK(screen->progressCalls().empty());
	fx.tick();
	REQUIRE(fx.wm->isAptWindowLoaded(screen->level()));
	CHECK(screen->progressCalls() == std::vector<std::pair<int, int>>{ { 3, 0 }, { 3, 37 } });
	CHECK(screen->setProgress(3, 99));
	CHECK(screen->progressCalls().back() == std::pair<int, int>{ 3, 99 });
}

TEST_CASE("coverage1: loading screen without game information reports its missing host without inventing cards")
{
	ScreensFx fx;
	auto &screen = fx.push<AptLoadScreen>("LoadScreen.apt");
	CHECK(screen.cardCount() == 0);
	std::string value;
	CHECK(fx.wm->getExtern("GameLoadingType", value) == AptExternResult::Undefined);
	CHECK(fx.wm->getExtern("GameLoading:PlayerColor:7", value) == AptExternResult::Undefined);
	CHECK(fx.wm->noteCount("loadscreen-unwired") == 1);
	CHECK(fx.wm->noteCount("provider-unwired") == 2);
	CHECK(screen.progressCalls().empty());
	// no game information: no card exists, so the progress is refused and noted (the P16 loading screen invents no card 0)
	CHECK_FALSE(screen.setLocalProgress(25));
	CHECK(screen.progressCalls().empty());
	CHECK(fx.wm->noteCount("loadscreen-invalid-card") == 1);
}

TEST_CASE("coverage1: status page creates text and colour rows, rejects bad indices, and unloads its providers")
{
	ScreensFx fx;
	PlayerStatusInfo status;
	status.inSkirmish = true;
	PlayerStatusRow row;
	row.fields[0] = "Friend";
	row.fields[1] = "Dwarves";
	row.fields[2] = "Team 1";
	row.fields[3] = "Alive";
	row.color = -112233;
	status.rows.push_back(row);
	fx.env.playerStatus = &status;
	auto &screen = fx.push<AptPlayerTribute>("PlayerTribute.apt");
	const std::string root = screen.pathPrefix();
	CHECK(fx.wm->invokeCallback(root + "_OnPageLoaded", "name=" + root + ".Status&type=Other"));
	CHECK_FALSE(screen.statusPageLoaded());
	CHECK(fx.wm->invokeCallback(root + "_OnPageLoaded", "name=" + root + ".Status&type=StatusPage"));
	REQUIRE(screen.statusPageLoaded());
	CHECK(fx.externValue(root + ".Status_NumOfPlayers") == "1");
	CHECK(fx.externValue(root + ".Status_InSkirmish") == "1");
	const std::string command = root + ".Status_OnRowShown";
	CHECK(fx.wm->invokeCallback(command, "index=-1&name=" + root + ".Row"));
	CHECK(fx.wm->invokeCallback(command, "index=1&name=" + root + ".Row"));
	CHECK(screen.statusRowsShown() == 0);
	CHECK(fx.wm->invokeCallback(command, "index=0&name=_level99.Row"));
	CHECK(screen.statusRowsShown() == 0);
	CHECK(fx.wm->invokeCallback(command, "index=0&name=" + root + ".Row"));
	CHECK(screen.statusRowsShown() == 1);
	REQUIRE(fx.wm->aptText("APT:" + root + ".Row_field0") != nullptr);
	CHECK(*fx.wm->aptText("APT:" + root + ".Row_field0") == "Friend");
	CHECK(*fx.wm->aptText("APT:" + root + ".Row_field3") == "Alive");
	CHECK(fx.externValue(root + ".Row_color") == "-112233");
	CHECK(fx.wm->invokeCallback(root + ".Status_OnRowHidden", "index=0"));
	CHECK(screen.statusRowsShown() == 0);
	CHECK_FALSE(hasName(fx.wm->providerNames(), root + ".Row_color"));
	CHECK(fx.wm->invokeCallback(command, "index=0&name=" + root + ".Row"));
	CHECK(screen.statusRowsShown() == 1);
	CHECK(fx.wm->invokeCallback(root + "_OnPageUnloaded", root + ".Status"));
	CHECK_FALSE(screen.statusPageLoaded());
	CHECK_FALSE(hasName(fx.wm->providerNames(), root + ".Status_NumOfPlayers"));
	CHECK_FALSE(hasName(fx.wm->providerNames(), root + ".Row_color"));
}

TEST_CASE("coverage1: tribute content unload removes dynamic row callbacks, externs and gadget refs before reload")
{
	ScreensFx fx;
	TributeRecorder tribute;
	tribute.players = { { 0, "Local", 90, true, true, 1 }, { 1, "Ally", 30, false, true, 2 } };
	fx.env.tribute = &tribute;
	auto &screen = fx.push<AptPlayerTribute>("PlayerTribute.apt");
	const std::string root = screen.pathPrefix();
	const std::string page = root + ".Tribute";
	const std::string content = root + ".Tribute.Content";
	CHECK(fx.wm->invokeCallback(root + "_OnPageLoaded", "name=" + page + "&type=TributePage"));
	CHECK(fx.wm->invokeCallback(page + "_OnEnabledContentLoaded", "name=" + root + ".Tribute.Content"));
	REQUIRE(screen.tributeRows().size() == 2);
	CHECK(fx.externValue(content + "_NumOfPlayers") == "2");
	CHECK(fx.wm->invokeCallback(content + "_OnRowShown", "index=1&name=" + root + ".Tribute.Content.Row"));
	const std::string row = content + ".Row";
	REQUIRE(fx.wm->aptText("APT:" + row + "_field0") != nullptr);
	CHECK(*fx.wm->aptText("APT:" + row + "_field0") == "Ally");
	CHECK(fx.externValue(row + "_color") == "2");
	CHECK(hasName(fx.wm->screenRefNames(), row + "_InitSlider"));
	CHECK(fx.wm->invokeCallback(content + "_OnRowHidden", "index=1"));
	CHECK_FALSE(hasName(fx.wm->providerNames(), row + "_color"));
	CHECK_FALSE(hasName(fx.wm->screenRefNames(), row + "_InitSlider"));
	CHECK(fx.wm->invokeCallback(content + "_OnRowShown", "index=1&name=" + root + ".Tribute.Content.Row"));
	CHECK(fx.externValue(row + "_color") == "2");
	CHECK(fx.wm->invokeCallback(page + "_OnEnabledContentUnloaded", ""));
	CHECK(screen.tributeRows().empty());
	CHECK_FALSE(hasName(fx.wm->providerNames(), row + "_color"));
	CHECK_FALSE(hasName(fx.wm->providerNames(), content + "_NumOfPlayers"));
	CHECK_FALSE(hasName(fx.wm->commandNames(), content + "_Send"));
	CHECK_FALSE(hasName(fx.wm->screenRefNames(), row + "_InitTextEntry"));
	CHECK(fx.wm->invokeCallback(page + "_OnEnabledContentLoaded", "name=" + root + ".Tribute.Content"));
	CHECK(fx.externValue(content + "_NumOfPlayers") == "2");
	CHECK(fx.wm->invokeCallback(content + "_OnRowShown", "index=1&name=" + root + ".Tribute.Content.Row"));
	CHECK(fx.externValue(row + "_color") == "2");
}

TEST_CASE("coverage1: tribute rows rebalance against changing cash and transfer availability")
{
	ScreensFx fx;
	TributeRecorder tribute;
	tribute.players = { { 0, "Local", 70, true, true, 0 }, { 1, "Ally", 15, false, true, 1 }, { 2, "Out", 8, false, false, 2 } };
	fx.env.tribute = &tribute;
	auto &screen = fx.push<AptPlayerTribute>("PlayerTribute.apt");
	const std::string root = screen.pathPrefix();
	const std::string page = root + ".Tribute";
	const std::string content = page + ".Content";
	CHECK(fx.externValue(root + "_TributeEnabled") == "1");
	CHECK(fx.wm->invokeCallback(root + "_OnPageLoaded", "name=" + page + "&type=TributePage"));
	CHECK(fx.wm->invokeCallback(page + "_OnEnabledContentLoaded", "name=" + content));
	REQUIRE(screen.tributeRows().size() == 3);
	CHECK(screen.tributeRows()[1].maximum == 70);
	CHECK(screen.tributeRows()[2].maximum == 0);
	CHECK(fx.wm->invokeCallback(content + "_OnRowShown", "index=1&name=" + content + ".Ally"));
	REQUIRE(fx.wm->aptText("APT:" + content + ".Ally_field1") != nullptr);
	CHECK(*fx.wm->aptText("APT:" + content + ".Ally_field1") == "15");
	tribute.players[0].cash = 18;
	fx.tick();
	CHECK(screen.tributeRows()[1].maximum == 18);
	tribute.players[1].active = false;
	fx.tick();
	CHECK(screen.tributeRows()[1].maximum == 0);
	tribute.active = false;
	CHECK(fx.externValue(root + "_TributeEnabled") == "0");
	CHECK(fx.wm->invokeCallback(content + "_Reset", ""));
	CHECK(fx.wm->invokeCallback(content + "_Send", ""));
	CHECK(tribute.sent.empty());
}

TEST_CASE("coverage1: options providers honor opening modes and writable master choice on a loaded movie")
{
	ScreensFx fx;
	fx.env.resolutionChangeApplies = false;
	OptionPreferences prefs;
	prefs.set("StaticGameLOD", "Medium");
	fx.env.options = &prefs;
	AptOptionsScreen::open(*fx.shell, true, true, true, true);
	auto *screen = dynamic_cast<AptOptionsScreen *>(fx.shell->top());
	REQUIRE(screen != nullptr);
	fx.tick();
	REQUIRE(fx.wm->isAptWindowLoaded(screen->level()));
	CHECK(fx.externValue("NetworkEnabled") == "1");
	CHECK(fx.externValue("AllowResolutionChange") == "0");
	CHECK(fx.externValue("AllowAdvancedOptions") == "1");
	CHECK(fx.externValue("AdvancedOnly") == "1");
	CHECK(fx.externValue("MasterOption0Num") == "5");
	CHECK(fx.externValue("MasterOption0Current") == "2");
	CHECK(fx.wm->setExtern("MasterOption0Current", "Custom"));
	CHECK(screen->masterOption() == 5);
	CHECK(fx.externValue("MasterOption0Current") == "Custom");
	std::string value;
	CHECK(fx.wm->getExtern("MasterOption0ResetDefault", value) == AptExternResult::Undefined);
	CHECK(fx.wm->aptText("APT:CannotChangeResolution") != nullptr);
	CHECK(fx.wm->aptText("APT:VersionNum") != nullptr);
	AptOptionsScreen::open(*fx.shell, false, false, false, false);
	CHECK(fx.shell->screenCount() == 1);
}

TEST_CASE("coverage1: options templates and advanced page commands retain a custom selection")
{
	ScreensFx fx;
	OptionPreferences prefs;
	prefs.set("StaticGameLOD", "High");
	fx.env.options = &prefs;
	auto &screen = fx.push<AptOptionsScreen>("Options.apt");
	screen.setMode(false, true, true, false);
	CHECK(fx.externValue("AdvancedOption0Num") == std::to_string(GameLODManager::advancedOption(0).count));
	// No custom template has been entered yet; entering advanced loads the
	// saved nine advanced-option values, not this page's transient edit.
	CHECK(fx.externValue("MasterOption0TemplateCustom").empty());
	const std::string custom = AdvancedOptionPrefs::format(prefs);
	CHECK(fx.wm->setExtern("MasterOption0TemplateCustom", "0,1,2,3,4,5,6,7,8"));
	CHECK(fx.externValue("MasterOption0TemplateCustom") == "0,1,2,3,4,5,6,7,8");
	CHECK(fx.wm->setExtern("MasterOption0Current", "Custom"));
	CHECK(fx.wm->invokeCallback("AptOptions::EnterAdvancedSettings", ""));
	CHECK(screen.pageState() == 2);
	CHECK(screen.customTemplate() == custom); // EnterAdvancedSettings reloads the saved custom preset.
	CHECK(fx.wm->invokeCallback("AptOptions::OnInitialized", ""));
	CHECK(screen.pageState() == 1);
}

TEST_CASE("coverage1: synthetic options sliders initialize, preview audio, reset and save their native values")
{
	ScreensFx fx;
	OptionPreferences prefs;
	prefs.set("SFXVolume", "63");
	prefs.set("VoiceVolume", "27");
	prefs.set("Brightness", "76");
	prefs.set("ScrollFactor", "130");
	fx.env.options = &prefs;
	fx.env.haveDefaultVolumes = true;
	fx.env.defaultVolumes[0] = 0.4f;
	fx.env.defaultVolumes[1] = 0.2f;
	fx.env.keyboardDefaultScrollSpeedFactor = 0.5f;
	fx.addGadgets("Options", { { "Options::SoundFxVolume", "HorzSlider", "AptOptions::InitGadgets" },
		{ "Options::VoiceVolume", "HorzSlider", "AptOptions::InitGadgets" }, { "Options::Brightness", "HorzSlider", "AptOptions::InitGadgets" },
		{ "Options::ScrollSpeed", "HorzSlider", "AptOptions::InitGadgets" }, { "Options::HealthBars", "CheckBox", "AptOptions::InitGadgets" } });
	auto &screen = fx.push<AptOptionsScreen>("Options.apt");
	CHECK(fx.layer->errors().empty());
	GameWindow *sfx = screen.gadget("SoundFxVolume");
	GameWindow *voice = screen.gadget("VoiceVolume");
	GameWindow *brightness = screen.gadget("Brightness");
	GameWindow *scroll = screen.gadget("ScrollSpeed");
	REQUIRE(sfx != nullptr);
	REQUIRE(voice != nullptr);
	REQUIRE(brightness != nullptr);
	REQUIRE(scroll != nullptr);
	CHECK(GadgetSliderGetPosition(sfx) == 63);
	CHECK(GadgetSliderGetPosition(voice) == 27);
	CHECK(GadgetSliderGetPosition(brightness) == 76);
	CHECK(GadgetSliderGetPosition(scroll) == 100); // the invalid saved ScrollFactor is clamped
	CHECK(fx.wm->invokeCallback("AptOptions::OnInitialized", ""));
	fx.layer->gadgets().winSendSystemMsg(screen.ownerWindow(), GSM_SLIDER_TRACK, (WindowMsgData)(std::uintptr_t)sfx, 55);
	REQUIRE(fx.services.miscEvents.size() == 1);
	CHECK(fx.services.miscEvents[0].second == "VolumeSampleSoundFX");
	REQUIRE_FALSE(fx.services.appliedOptions.empty());
	CHECK(fx.services.appliedOptions.back() == std::pair<std::string, std::string>{ "SFXVolume", "55.000000" });
	fx.layer->gadgets().winSendSystemMsg(screen.ownerWindow(), GSM_SLIDER_TRACK, (WindowMsgData)(std::uintptr_t)voice, 22);
	REQUIRE_FALSE(fx.services.removedEvents.empty());
	CHECK(fx.services.removedEvents.back() == 1);
	REQUIRE(fx.services.miscEvents.size() == 2);
	CHECK(fx.services.miscEvents[1].second == "VolumeSampleVoice");
	fx.layer->gadgets().winSendSystemMsg(screen.ownerWindow(), GSM_SLIDER_CLICKED, (WindowMsgData)(std::uintptr_t)voice, 0);
	CHECK(fx.services.removedEvents.size() == 2); // the voice sample is allowed to finish
	fx.layer->gadgets().winSendSystemMsg(screen.ownerWindow(), GSM_SLIDER_TRACK, (WindowMsgData)(std::uintptr_t)brightness, 40);
	CHECK_FALSE(fx.services.gammas.empty());
	CHECK(fx.wm->invokeCallback("AptOptions::Reset", ""));
	CHECK(GadgetSliderGetPosition(sfx) == 40);
	CHECK(GadgetSliderGetPosition(voice) == 20);
	CHECK(GadgetSliderGetPosition(brightness) == 50);
	CHECK(fx.wm->invokeCallback("AptOptions::Save", ""));
	CHECK(prefs.get("SFXVolume") == "40.000000");
	CHECK(prefs.get("Brightness") == "50");
	CHECK(prefs.get("ScrollFactor") == "25");
}

TEST_CASE("coverage1: tribute slider clamps transfers, rebalances remaining cash, resets and sends to active allies")
{
	ScreensFx fx;
	TributeRecorder tribute;
	tribute.players = { { 4, "Sender", 85, true, true, 0 }, { 5, "Ally A", 13, false, true, 0 },
		{ 6, "Ally B", 9, false, true, 0 } };
	fx.env.tribute = &tribute;
	// The row refs are registered by the content's OnRowShown commands after
	// the movie has placed its placeholders; pending _Init callbacks then run.
	fx.addGadgets("PlayerTribute", { { "SliderA", "HorzSlider", "_level1.Tribute.Content.AllyA_InitSlider" },
		{ "SliderB", "HorzSlider", "_level1.Tribute.Content.AllyB_InitSlider" } });
	auto &screen = fx.push<AptPlayerTribute>("PlayerTribute.apt");
	CHECK(fx.layer->errors().empty());
	const std::string root = screen.pathPrefix();
	const std::string page = root + ".Tribute";
	const std::string content = page + ".Content";
	CHECK(fx.wm->invokeCallback(root + "_OnPageLoaded", "name=" + page + "&type=TributePage"));
	CHECK(fx.wm->invokeCallback(page + "_OnEnabledContentLoaded", "name=" + content));
	CHECK(fx.wm->invokeCallback(content + "_OnRowShown", "index=1&name=" + content + ".AllyA"));
	CHECK(fx.wm->invokeCallback(content + "_OnRowShown", "index=2&name=" + content + ".AllyB"));
	fx.tick();
	REQUIRE(screen.tributeRows().size() == 3);
	REQUIRE(screen.tributeRows()[1].slider);
	REQUIRE(screen.tributeRows()[2].slider);
	CHECK_FALSE(screen.setRowAmountBySlider(-1, 3));
	CHECK(screen.setRowAmountBySlider(1, 60));
	CHECK(screen.tributeRows()[1].amount == 60);
	CHECK(screen.tributeRows()[2].maximum == 25);
	CHECK(screen.setRowAmountBySlider(2, 100));
	CHECK(screen.tributeRows()[2].amount == 25); // no more local cash
	CHECK(screen.setRowAmountBySlider(1, -5));
	CHECK(screen.tributeRows()[1].amount == 0);
	CHECK(screen.tributeRows()[2].maximum == 85);
	CHECK(screen.setRowAmountBySlider(1, 8));
	tribute.allowed = true;
	CHECK(fx.wm->invokeCallback(content + "_Send", ""));
	CHECK(tribute.sent == std::vector<std::tuple<int, int, std::uint32_t>>{ { 4, 5, 8 }, { 4, 6, 25 } });
	CHECK(screen.sends() == 2);
	CHECK(fx.wm->invokeCallback(content + "_Reset", ""));
	CHECK(screen.tributeRows()[1].amount == 0);
	CHECK(screen.tributeRows()[2].amount == 0);
}
