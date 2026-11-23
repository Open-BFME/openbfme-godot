// OpenBFME tests, lane HUD-6: RotWK's radial command bubbles over a structure (RW 0x9D6320 / 0x9D5CCE / 0x9457FE), the side command bar's frame
// (SetButtonState, RW 0x92F082 / 0x92F015, gate RW 0x92F27D) and the help box (RW 0x807A81, InGameCommandButtonHelp RW 0x974EEB / 0x9744E4 / 0x974795,
// InGameHelpBoxMovieClip RW 0x92E462). GPL-3.0.
// The retail tests SKIP loudly without the installs; the layout and text cases need nothing.

#include "doctest.h"

#include "HudTestUtil.h"
#include "HudAudioTestUtil.h"

#include "Common/INI.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHelpBox.h"
#include "GameClient/InGameHud.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpawnUnitBehavior.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
std::vector<RadialMenuLayout::Rect> ring(int n, int w, int h, int cx, int cy)
{
	RadialMenuLayout l;
	l.setCount(n);
	l.updateCenter(cx, cy);
	l.update(w, h);
	std::vector<RadialMenuLayout::Rect> out;
	l.place(out);
	return out;
}
// the centre of a button rectangle
int midX(const RadialMenuLayout::Rect &r) { return r.x + r.w / 2; }
int midY(const RadialMenuLayout::Rect &r) { return r.y + r.h / 2; }

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

void loadPrerequisiteFixture(SharedWorld &s, const std::string &filename, const std::string &contents, INILoadType loadType = INI_LOAD_OVERWRITE)
{
	std::vector<std::uint8_t> bytes(contents.begin(), contents.end());
	INI ini(s.world->iniEnvironment());
	ini.loadMemory(filename, bytes, loadType);
}

void addPrerequisiteHelpText(MapText &text)
{
	text.t["CONTROLBAR:OrRequirement"] = u"or";
	text.t["CONTROLBAR:GeneralsPromotion"] = u"Promotion";
	text.t["CONTROLBAR:UpgradeRequired"] = u"Upgrade required";
	text.t["CONTROLBAR:Requirements"] = u"Requires: %s";
	text.t["TOOLTIP:PrerequisiteFixtureDescription"] = u"Build this.";
}
} // namespace

TEST_CASE("hud6 radial layout: RotWK's button size, radius and ring (RW 0x9D6320 / 0x9D5CCE)")
{
	RadialMenuLayout l;
	l.setCount(3);
	l.update(1024, 768);
	// ftol(1024 * 0.046875) x ftol(768 * 0.0625); radius max(ftol(24 * 1.175 / sin(pi / 3)) = 32, 48 * 0.6666667 = 32.0)
	CHECK(l.buttonW() == 48);
	CHECK(l.buttonH() == 48);
	CHECK(l.radius() == 32);
	// the first straight above the centre, then clockwise by 2 pi / 3 (the fortress's three bubbles of the owner's shot: top, lower right, lower left)
	std::vector<RadialMenuLayout::Rect> r = ring(3, 1024, 768, 500, 400);
	REQUIRE(r.size() == 3);
	CHECK(r[0].x == 476);
	CHECK(r[0].y == 344);
	CHECK(midX(r[0]) == 500);
	CHECK(midY(r[0]) == 368);
	CHECK(midX(r[1]) == 527);
	CHECK(midY(r[1]) == 416);
	CHECK(midX(r[2]) == 472);
	CHECK(midY(r[2]) == 416);
	// seven heroes (the owner's second shot): radius ftol(28.2 / sin(pi / 7)) = 64
	RadialMenuLayout seven;
	seven.setCount(7);
	seven.update(1024, 768);
	CHECK(seven.radius() == 64);
	// one button sits on the centre; two: above and below at 2/3 of the width
	r = ring(1, 1024, 768, 300, 200);
	REQUIRE(r.size() == 1);
	CHECK(midX(r[0]) == 300);
	CHECK(midY(r[0]) == 200);
	r = ring(2, 1024, 768, 300, 200);
	REQUIRE(r.size() == 2);
	CHECK(midY(r[0]) == 168);
	CHECK(midY(r[1]) == 232);
	// the size follows the display: 1600 x 900 -> 75 x 56
	RadialMenuLayout wide;
	wide.setCount(4);
	wide.update(1600, 900);
	CHECK(wide.buttonW() == 75);
	CHECK(wide.buttonH() == 56);
	// the centre follows only a move of more than 2 pixels (RW 0x9D5B77)
	RadialMenuLayout c;
	c.setCount(3);
	CHECK(c.updateCenter(100, 100));
	CHECK_FALSE(c.updateCenter(101, 101));
	CHECK(c.centerX() == 100);
	CHECK(c.updateCenter(102, 101));
	CHECK(c.centerX() == 102);
}

TEST_CASE("hud6 help: the name's '&' key, the shortcut line, the measured height and the drawn lines (RW 0x97434E / 0x9744E4 / 0x974795)")
{
	MapText text;
	text.t["TOOLTIP:Shortcut"] = u"Shortcut: %s";
	CommandButtonHelp::Text t;
	t.name = u"Morgu&l Blade";
	t.description = u"Requires Level 4 \n Targeted enemy takes poison damage over \n time and becomes a wight upon death";
	CommandButtonHelp help(t, &text, nullptr, nullptr);
	CHECK(help.name() == u"Morgul Blade");
	CHECK(help.shortcutKey() == u'L');
	HelpBoxSettings settings;
	settings.name = { "Albertus MT", 16, false, 0xFFFFFFFFu };
	settings.cost = { "Albertus MT", 14, false, 0xFFFFCC00u };
	settings.shortcut = { "Albertus MT", 14, false, 0xFFFFCC00u };
	settings.description = { "Albertus MT", 12, false, 0xFFFFCC00u };
	DefaultFontMetrics metrics; // fontHeight = size + 4
	const int height = help.setWidthAndComputeHeight(400, settings, 1.0f, 1.0f, 1.0f, metrics);
	// name 20 + shortcut 18 + three description lines of 16
	CHECK(height == 20 + 18 + 3 * 16);
	std::vector<HelpDrawOp> ops;
	help.render(100.0f, 50.0f, 400.0f, (float)height, ops);
	std::vector<std::string> lines;
	for (const HelpDrawOp &op : ops)
	{
		if (op.kind == HelpDrawOp::TEXT)
		{
			lines.push_back(op.text);
		}
	}
	REQUIRE(lines.size() == 5);
	CHECK(lines[0] == "Morgul Blade");
	CHECK(lines[1] == "Shortcut: L");
	CHECK(lines[2] == "Requires Level 4");
	CHECK(lines[3] == "Targeted enemy takes poison damage over");
	CHECK(lines[4] == "time and becomes a wight upon death");
	// the name is centred in the box, the description lines below the shortcut line
	CHECK(ops[0].y == doctest::Approx(50.0f));
	CHECK(ops[2].y == doctest::Approx(50.0f + 20.0f + 18.0f));
}

TEST_CASE("hud6 help box: the movie clip's states and the provider's delay and hide (RW 0x92E462 / 0x92E2AC / 0x807848 / 0x71FC08)")
{
	std::vector<std::string> calls;
	InGameHelpBox box;
	box.loaded("_level0.helpBox", [&](const std::string &path, const std::string &fn, const std::vector<std::string> &args) {
		calls.push_back(path + "." + fn + (args.empty() ? std::string() : "(" + args[0] + ")"));
		return true;
	});
	CHECK(box.renderName() == "_level0.helpBox_Content");
	auto measure = [](CommandButtonHelp &, int width) { return width / 4; };
	box.update(measure, 0.5f); // state 0 -> SampleContentWidth
	REQUIRE(calls.size() == 1);
	CHECK(calls[0] == "helpBox.SampleContentWidth");
	std::vector<HelpDrawOp> ops;
	box.render(10.0f, 20.0f, 299.6f, 50.0f, ops); // the sampled width: (int)(299.6 + 0.5)
	CHECK(box.sampledWidth() == 300);
	box.update(measure, 0.5f); // state 1 with a width -> Hide
	CHECK(calls.back() == "helpBox.Hide");
	CHECK(box.state() == 2);
	// a provider is kept, then shown once the delay passed (RW 0x8078E0: now > start + delay)
	InGameHelpBox::Provider p{ 2, 7, nullptr };
	int composed = 0;
	auto compose = [&]() {
		++composed;
		return std::make_shared<CommandButtonHelp>();
	};
	box.beginFrame();
	box.showHelp(p, 1000, 1, compose);
	CHECK(composed == 0);
	box.beginFrame();
	box.showHelp(p, 1001, 1, compose);
	CHECK(composed == 0); // 1001 <= 1000 + 1
	box.beginFrame();
	box.showHelp(p, 1002, 1, compose);
	CHECK(composed == 1);
	box.update(measure, 0.5f); // state 2 with a help -> Show(75 * 0.5)
	CHECK(calls.back() == "helpBox.Show(37.500000)");
	CHECK(box.isShown());
	// a frame without a hover: the box hides after 5 updates
	box.beginFrame();
	box.beginFrame();
	CHECK(box.state() == 4);
	for (int i = 0; i < 5; ++i)
	{
		box.update(measure, 0.5f);
	}
	CHECK(box.state() == 2);
	CHECK(calls.back() == "helpBox.Hide");
}

namespace
{
struct Hud6Rig
{
	Rig rig;
	hudaudio::RecordingShellServices services;
	GameTextTableSource text;
	MappedImageCollection images;
	INIEnvironment imageEnv;
	std::unique_ptr<InGameHud> hud;
	explicit Hud6Rig(SharedWorld &s, const char *faction, std::function<int()> endGameTimer = {}) : rig(s, "map mp fall back 4p", faction)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(text.table.loadRetail(*s.mount->fs, &error), error); // data/lotr.str, or a non-English install's lotr.csf
		imageEnv.fileSystem = s.mount->fs.get();
		images.registerBlocks(imageEnv.blocks);
		INI ini(imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		cfg.endGameTimer = std::move(endGameTimer);
		hud = std::make_unique<InGameHud>(cfg);
		hud->setMappedImages(&images);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~Hud6Rig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	void select(Object *o)
	{
		hud->input().ui().deselectAll(true);
		hud->input().ui().selectObject(o->getID());
	}
	// a left click through the HUD's raw input: the selection reaches the logic (MSG_CREATE_SELECTED_GROUP), as test_hero_hud.cpp's rig does
	void clickSelect(Object *o)
	{
		rig.lookAt(*o->getPosition());
		frames(5);
		const ICoord2D px = rig.screenOf({ o->getPosition()->x, o->getPosition()->y, 10.0f });
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, rig.timeMs += 40);
		frames(20);
	}
};

CommandButtonHelp::Text composePrerequisiteHelp(Hud6Rig &h, const std::string &templateName, Player *player, Player *localPlayer, MapText &text,
	const std::string &description = std::string())
{
	CommandButton command;
	command.m_command = GUI_COMMAND_UNIT_BUILD;
	command.m_objectName = templateName;
	command.m_thingTemplate = shared().world->things().findTemplate(templateName);
	ControlBarButton button;
	button.button = &command;
	button.descriptLabel = description;
	return CommandButtonHelp::compose(button, &text, h.rig.game->logic(), player, localPlayer, nullptr, "Men");
}
} // namespace

TEST_CASE("hud click audio: radial synthetic availability states and command classes")
{
	if (!haveWorld("hud click audio radial"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	Hud6Rig h(s, "FactionMen");
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *source = h.rig.make("GondorBarracks", at.x, at.y);
	REQUIRE(source);
	h.rig.lookAt({ at.x, at.y, 0.0f });
	h.frames(20);
	auto context = h.hud->enterContext();
	ControlBar bar(h.hud->input().context());
	struct Scenario
	{
		const char *name;
		ButtonState state;
	};
	const Scenario scenarios[] = {
		{ "enabled", ButtonState::Enabled },
		{ "restricted", ButtonState::Restricted },
		{ "cant-afford", ButtonState::CantAfford },
		{ "not-ready", ButtonState::NotReady },
		{ "purchased", ButtonState::Purchased },
		{ "active", ButtonState::Active },
	};
	struct CommandClass
	{
		const char *name;
		int command;
	};
	const CommandClass commandClasses[] = {
		{ "FOUNDATION_CONSTRUCT", GUI_COMMAND_FOUNDATION_CONSTRUCT },
		{ "CASTLE_UPGRADE", GUI_COMMAND_CASTLE_UPGRADE },
		{ "UNIT_BUILD", GUI_COMMAND_UNIT_BUILD },
		{ "OBJECT_UPGRADE", GUI_COMMAND_OBJECT_UPGRADE },
		{ "SPECIAL_POWER", GUI_COMMAND_SPECIAL_POWER },
		{ "REVIVE", GUI_COMMAND_REVIVE },
	};
	for (const Scenario &scenario : scenarios)
	{
		for (const CommandClass &commandClass : commandClasses)
		{
			for (const bool left : { true, false })
			{
				CommandButton command;
				command.m_command = commandClass.command;
				ControlBarButton button;
				button.slot = 0;
				button.button = &command;
				button.state = scenario.state;
				std::vector<ControlBarButton> buttons{ button };
				h.hud->radialMenu().update(h.rig.logic(), &h.rig.game->drawables(), h.rig.view, 1024, 768, source->getID(), buttons, true);
				REQUIRE(h.hud->radialMenu().buttons().size() == 1);
				const RadialMenuLayout::Rect &rect = h.hud->radialMenu().buttons().front().rect;
				const int x = rect.x + rect.w / 2;
				const int y = rect.y + rect.h / 2;
				audio.manager->takeEventLog();
				h.hud->radialMenu().mouseButton(left, true, x, y, bar);
				const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD radial press");
				h.hud->radialMenu().mouseButton(left, false, x, y, bar);
				const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD radial press");
				const std::vector<std::string> expected = (scenario.state == ButtonState::Enabled || scenario.state == ButtonState::Active)
					? std::vector<std::string>{ "Gui_PalantirCommandButtonClick" }
					: std::vector<std::string>{ "Gui_PalantirCommandButtonDisabledClick" };
				hudaudio::printSequence(std::string("radial synthetic availability ") + scenario.name + " " + commandClass.name + (left ? " left" : " right"),
					down, up);
				CHECK(down == expected);
				CHECK(up.empty());
				audio.manager->takeEventLog();
				h.hud->radialMenu().mouseButton(left, true, -20, -20, bar);
				const std::vector<std::string> outsideDown = hudaudio::takeRequests(*audio.manager, "HUD radial press");
				h.hud->radialMenu().mouseButton(left, false, -20, -20, bar);
				const std::vector<std::string> outsideUp = hudaudio::takeRequests(*audio.manager, "HUD radial press");
				CHECK(outsideDown.empty());
				CHECK(outsideUp.empty());
			}
		}
	}
	CommandButton cancelableCommand;
	cancelableCommand.m_command = GUI_COMMAND_UNIT_BUILD;
	cancelableCommand.m_options = COMMAND_OPTION_CANCELABLE;
	ControlBarButton cancelableButton;
	cancelableButton.slot = 0;
	cancelableButton.button = &cancelableCommand;
	cancelableButton.state = ButtonState::Restricted;
	h.hud->radialMenu().update(h.rig.logic(), &h.rig.game->drawables(), h.rig.view, 1024, 768, source->getID(), { cancelableButton }, true);
	const RadialMenuLayout::Rect &cancelRect = h.hud->radialMenu().buttons().front().rect;
	audio.manager->takeEventLog();
	h.hud->radialMenu().mouseButton(false, true, cancelRect.x + cancelRect.w / 2, cancelRect.y + cancelRect.h / 2, bar);
	const std::vector<std::string> cancelDown = hudaudio::takeRequests(*audio.manager, "HUD radial press");
	h.hud->radialMenu().mouseButton(false, false, cancelRect.x + cancelRect.w / 2, cancelRect.y + cancelRect.h / 2, bar);
	const std::vector<std::string> cancelUp = hudaudio::takeRequests(*audio.manager, "HUD radial press");
	hudaudio::printSequence("radial synthetic availability restricted right cancelable", cancelDown, cancelUp);
	CHECK(cancelDown == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" });
	CHECK(cancelUp.empty());
}

TEST_CASE("hud6 recovered help guards retail")
{
	if (!haveWorld("hud6 help guards"))
	{
		return;
	}
	int timer = -1;
	Hud6Rig h(shared(), "FactionMen", [&]() { return timer; });
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", at.x, at.y);
	h.rig.lookAt({ at.x, at.y, 0 });
	h.select(pit);
	h.frames(40);
	REQUIRE_FALSE(h.hud->radialMenu().buttons().empty());
	const auto button = h.hud->radialMenu().buttons().front();
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(2);
	REQUIRE(h.hud->helpBox().state() == 2);
	auto enter = [&]() { h.hud->mouseMove(midX(button.rect), midY(button.rect)); h.frames(3); };
	auto leave = [&]() { h.hud->mouseMove(5, 5); h.frames(10); };

	enter();
	CHECK(h.hud->helpBox().helpsShown() == 1);
	CHECK(h.hud->helpBox().isShown());
	leave();
	for (int blocked : { 25, 0 })
	{
		timer = blocked;
		enter();
		CHECK(h.hud->helpBox().helpsShown() == 1);
		CHECK_FALSE(h.hud->helpBox().isShown());
		leave();
	}
	timer = -1;
	enter();
	CHECK(h.hud->helpBox().helpsShown() == 2);
}

TEST_CASE("hud6 recovered disconnect help retail: the late guard consumes the hover")
{
	if (!haveWorld("hud6 recovered disconnect help retail"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto context = h.hud->enterContext();
	h.frames(40);
	auto &box = h.hud->helpBox();
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(2);
	REQUIRE(box.state() == 2);
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	Shell shell(h.hud->windows(), factories, h.services, environment);
	auto screen = std::make_unique<AptDisconnectScreen>(h.hud->windows(), shell);
	REQUIRE(h.hud->windows().hasScreenRef("DisconnectScreen::InitGadgets"));
	h.frames(1);
	int composed = 0;
	auto compose = [&]() { ++composed; return std::make_shared<CommandButtonHelp>(); };
	const InGameHelpBox::Provider p{ 2, 0, nullptr };
	box.beginFrame(); box.showHelp(p, 1000, 0, compose);
	box.beginFrame(); box.showHelp(p, 1001, 0, compose);
	CHECK(composed == 0);
	CHECK_FALSE(box.isShown());
	screen.reset();
	CHECK_FALSE(h.hud->windows().hasScreenRef("DisconnectScreen::InitGadgets"));
	h.frames(1); // updates the guard, retaining the marked provider
	box.showHelp(p, 1002, 0, compose);
	CHECK(composed == 0); // dismissal alone does not retry a consumed hover
	box.beginFrame(); box.beginFrame(); // pointer leaves for a frame
	box.showHelp(p, 1003, 0, compose);
	box.beginFrame(); box.showHelp(p, 1004, 0, compose);
	CHECK(composed == 1);
}

TEST_CASE("hud6 recovered help provider replacement")
{
	InGameHelpBox box;
	box.loaded("_level0.helpBox", [](const std::string &, const std::string &, const std::vector<std::string> &) { return true; });
	auto measure = [](CommandButtonHelp &, int) { return 50; };
	std::vector<HelpDrawOp> ops;
	box.update(measure, 1);
	box.render(0, 0, 300, 100, ops);
	box.update(measure, 1);
	const InGameHelpBox::Provider a{ 2, 0, nullptr }, b{ 2, 1, nullptr };
	auto compose = []() { return std::make_shared<CommandButtonHelp>(); };
	box.beginFrame(); box.showHelp(a, 100, 0, compose);
	box.beginFrame(); box.showHelp(a, 101, 0, compose); box.update(measure, 1);
	REQUIRE(box.isShown());
	box.beginFrame(); box.showHelp(b, 102, 0, compose);
	CHECK(box.provider().slot == -1); // RW 0xA0CB16: the old provider is gone even during hideSoon.
	box.beginFrame(); box.showHelp(a, 103, 0, compose);
	box.beginFrame(); box.showHelp(a, 104, 0, compose); box.update(measure, 1);
	CHECK(box.helpsShown() == 2); // Returning to A must not reuse its old 'already shown' flag.
	CHECK(box.isShown());
}

TEST_CASE("hud6 retail: the help provider's equality (RW 0x8075A3)")
{
	CommandButton a, b;
	InGameHelpBox::Provider p{ 2, 3, &a, 11 };
	CHECK(p == InGameHelpBox::Provider{ 2, 3, &a, 11 });
	CHECK_FALSE(p == InGameHelpBox::Provider{ 1, 3, &a, 11 });  // the kind
	CHECK_FALSE(p == InGameHelpBox::Provider{ 2, 4, &a, 11 });  // the window / slot
	CHECK_FALSE(p == InGameHelpBox::Provider{ 2, 3, &b, 11 });  // the button
	CHECK_FALSE(p == InGameHelpBox::Provider{ 2, 3, &a, 12 });  // the UnitHelpSource object
}

TEST_CASE("hud6 retail: the help box's tooltip and +0x810 guards (RW 0x807861 / 0x807978)")
{
	InGameHelpBox box;
	box.loaded("_level0.helpBox", [](const std::string &, const std::string &, const std::vector<std::string> &) { return true; });
	auto measure = [](CommandButtonHelp &, int) { return 50; };
	std::vector<HelpDrawOp> ops;
	box.update(measure, 1);
	box.render(0, 0, 300, 100, ops);
	box.update(measure, 1);
	REQUIRE(box.state() == 2);

	bool tooltipsDisabled = false, modeGate = false;
	box.setUiGuards([&]() { return tooltipsDisabled; }, [&]() { return modeGate; });
	int composed = 0;
	auto compose = [&]() {
		++composed;
		return std::make_shared<CommandButtonHelp>();
	};
	const InGameHelpBox::Provider a{ 2, 0, nullptr }, b{ 2, 1, nullptr };

	// RW 0x807861: the tooltip policy is read before the provider is kept or compared.
	tooltipsDisabled = true;
	box.beginFrame();
	box.showHelp(a, 100, 0, compose);
	CHECK_FALSE(box.hasProvider());
	CHECK(composed == 0);
	CHECK_FALSE(box.isShown());

	// With tooltips enabled the same provider is kept, then shown after the delay.
	tooltipsDisabled = false;
	box.beginFrame();
	box.showHelp(a, 200, 0, compose);
	box.beginFrame();
	box.showHelp(a, 201, 0, compose);
	CHECK(composed == 1);
	box.update(measure, 1);
	REQUIRE(box.isShown());

	// RW 0x807978: an equal provider is consumed BEFORE the +0x810 read, so clearing the gate does not retry it.
	box.dropProvider(); // the shown box counts down (state 4), the provider is gone
	REQUIRE_FALSE(box.hasProvider());
	modeGate = true;
	box.beginFrame();
	box.showHelp(b, 300, 0, compose); // kept
	box.beginFrame();
	box.showHelp(b, 301, 0, compose); // elapsed: consumed, +0x810 suppresses composition
	CHECK(box.hasProvider()); // retained while the hover is marked
	CHECK(composed == 1);
	modeGate = false;
	box.beginFrame();
	box.showHelp(b, 302, 0, compose); // already consumed for this hover
	CHECK(composed == 1);
	// leaving and returning makes a new hover: it shows again.
	box.beginFrame();
	box.beginFrame();
	box.showHelp(b, 303, 0, compose);
	box.beginFrame();
	box.showHelp(b, 304, 0, compose);
	CHECK(composed == 2);
}

TEST_CASE("hud6 retail: InGameUI tooltip policy and the +0x810 setter (RW 0x69BB14 / 0x69B6A8)")
{
	if (!haveWorld("hud6 tooltip policy"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	InGameUI &ui = h.hud->input().ui();
	CHECK_FALSE(ui.areTooltipsDisabled()); // the setting is on and the deadline is zero

	ui.setTooltipsEnabled(false); // GlobalData +0xDD0 == 0
	CHECK(ui.areTooltipsDisabled());
	ui.setTooltipsEnabled(true);
	CHECK_FALSE(ui.areTooltipsDisabled());

	const unsigned frame = h.rig.logic().getFrame();
	ui.disableTooltipsUntilFrame(frame); // never lowered; the current frame is not below it
	CHECK_FALSE(ui.areTooltipsDisabled());
	ui.disableTooltipsUntilFrame(frame + 3);
	CHECK(ui.areTooltipsDisabled());
	// the RotWK Living World query wins over the setting and the deadline (RW 0x69BB33 -> false)
	ui.setLivingWorldTooltipQuery([]() { return true; });
	CHECK_FALSE(ui.areTooltipsDisabled());
	ui.setLivingWorldTooltipQuery({});
	CHECK(ui.areTooltipsDisabled());
	h.frames(30);
	CHECK_FALSE(ui.areTooltipsDisabled()); // the logic frame passed +0x7EC

	// RW 0x69B6A8: false-to-true clears selecting, the radius cursor, the four mapped mode flags; no stream message.
	ui.setSelecting(true);
	ui.setForceAttackMode(true);
	ui.setForceMoveToMode(true);
	ui.setWaypointMode(true);
	ui.setPreferSelectionMode(true);
	ui.setModeGate810(true);
	CHECK(ui.modeGate810());
	CHECK_FALSE(ui.isSelecting());
	CHECK_FALSE(ui.isInForceAttackMode());
	CHECK_FALSE(ui.isInForceMoveToMode());
	CHECK(ui.isInWaypointMode()); // +0x8B0 is outside the setter's +0x8B8 .. +0x8C2 range
	CHECK_FALSE(ui.isInPreferSelectionMode());
	ui.setSelecting(true);
	ui.setModeGate810(true); // already true: no false-to-true edge, no side effects
	CHECK(ui.isSelecting());
	ui.setModeGate810(false);
	CHECK_FALSE(ui.modeGate810());
}

TEST_CASE("hud6 retail: input off drops the button help and does not compose it again (cutscene)")
{
	if (!haveWorld("hud6 cutscene help"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", at.x, at.y);
	h.rig.lookAt({ at.x, at.y, 0 });
	h.select(pit);
	h.frames(40);
	REQUIRE_FALSE(h.hud->radialMenu().buttons().empty());
	const auto button = h.hud->radialMenu().buttons().front();
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(2);
	REQUIRE(h.hud->helpBox().state() == 2);

	auto enter = [&]() {
		h.hud->mouseMove(midX(button.rect), midY(button.rect));
		h.frames(3);
	};
	auto leave = [&]() {
		h.hud->mouseMove(5, 5);
		h.frames(10);
	};
	InGameUI &ui = h.hud->input().ui();

	enter();
	CHECK(h.hud->helpBox().helpsShown() == 1);
	CHECK(h.hud->helpBox().isShown());
	// The input-disable edge drops the provider; the next frame must not re-compose from the button/Apt branch.
	ui.setInputEnabled(false);
	h.frames(8);
	CHECK_FALSE(h.hud->helpBox().hasProvider());
	CHECK_FALSE(h.hud->helpBox().isShown());
	const unsigned shownWhileOff = h.hud->helpBox().helpsShown();
	h.hud->mouseMove(midX(button.rect), midY(button.rect));
	h.frames(6);
	CHECK(h.hud->helpBox().helpsShown() == shownWhileOff);
	CHECK_FALSE(h.hud->helpBox().isShown());

	// Re-enabling the input allows a fresh hover to show again.
	ui.setInputEnabled(true);
	leave();
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shownWhileOff + 1);

	// The actual cutscene path (DISABLE_INPUT) also drops the provider and leaves no help under the pointer.
	h.hud->scriptInput(false);
	h.frames(8);
	CHECK_FALSE(h.hud->helpBox().hasProvider());
	CHECK_FALSE(h.hud->helpBox().isShown());
	const unsigned afterScript = h.hud->helpBox().helpsShown();
	h.hud->mouseMove(midX(button.rect), midY(button.rect));
	h.frames(8);
	CHECK(h.hud->helpBox().helpsShown() == afterScript);
	CHECK_FALSE(h.hud->helpBox().isShown());
	h.hud->scriptInput(true);
}

TEST_CASE("hud6 retail: tooltips off / deadline / +0x810 suppress button help in the HUD")
{
	if (!haveWorld("hud6 tooltip gates"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", at.x, at.y);
	h.rig.lookAt({ at.x, at.y, 0 });
	h.select(pit);
	h.frames(40);
	REQUIRE_FALSE(h.hud->radialMenu().buttons().empty());
	const auto button = h.hud->radialMenu().buttons().front();
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(2);
	InGameUI &ui = h.hud->input().ui();
	auto enter = [&]() {
		h.hud->mouseMove(midX(button.rect), midY(button.rect));
		h.frames(3);
	};
	auto leave = [&]() {
		h.hud->mouseMove(5, 5);
		h.frames(10);
	};
	enter();
	const unsigned shown = h.hud->helpBox().helpsShown();
	REQUIRE(shown >= 1);
	leave();

	// RW 0x69BB14: the global setting off.
	ui.setTooltipsEnabled(false);
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown);
	CHECK_FALSE(h.hud->helpBox().hasProvider());
	leave();
	ui.setTooltipsEnabled(true);
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 1);
	leave();

	// RW 0x69BAF9: the deadline. The hover time advances 3 frames; the deadline is far ahead.
	ui.disableTooltipsUntilFrame(h.rig.logic().getFrame() + 100);
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 1);
	CHECK_FALSE(h.hud->helpBox().hasProvider());
	leave();
	ui.clearTooltipDisable();
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 2);
	leave();

	// RW 0x69B6A8 / 0x807978: the late mode gate.
	ui.setModeGate810(true);
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 2);
	CHECK(h.hud->helpBox().hasProvider()); // RW 0x807978: the provider is consumed and retained, not released
	leave();
	ui.setModeGate810(false);
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 3);
}

TEST_CASE("hud6 retail: tooltips off / the end-game timer while shown hide through the unmarked frame")
{
	InGameHelpBox box;
	box.loaded("_level0.helpBox", [](const std::string &, const std::string &, const std::vector<std::string> &) { return true; });
	auto measure = [](CommandButtonHelp &, int) { return 50; };
	std::vector<HelpDrawOp> ops;
	box.update(measure, 1);
	box.render(0, 0, 300, 100, ops);
	box.update(measure, 1);
	REQUIRE(box.state() == 2);

	bool tooltipsDisabled = false;
	box.setUiGuards([&]() { return tooltipsDisabled; }, []() { return false; });
	auto compose = []() { return std::make_shared<CommandButtonHelp>(); };
	const InGameHelpBox::Provider a{ 2, 0, nullptr };
	auto show = [&](std::uint32_t now) {
		box.beginFrame();
		box.showHelp(a, now, 0, compose);
		box.beginFrame();
		box.showHelp(a, now + 1, 0, compose);
		box.update(measure, 1);
	};

	show(100);
	REQUIRE(box.isShown());
	// RW 0x807861: the tooltip guard returns BEFORE the frame is marked. The box stays for this frame; the next
	// ControlBar::update sees no mark, calls hideSoon, and the 5-update countdown releases the help.
	tooltipsDisabled = true;
	box.beginFrame();
	box.showHelp(a, 102, 0, compose);
	CHECK(box.isShown());
	box.beginFrame();
	CHECK(box.state() == 4);
	for (int i = 0; i < 5; ++i)
	{
		box.update(measure, 1);
	}
	CHECK(box.state() == 2);
	CHECK(box.help() == nullptr);

	// The end-game timer is the other early guard: it also returns without marking, so the same unmarked frame hides.
	tooltipsDisabled = false;
	show(200);
	REQUIRE(box.isShown());
	box.setEndGameTimer(0); // ScriptEngine + 0x1A204 >= 0
	box.beginFrame();
	box.showHelp(a, 202, 0, compose);
	CHECK(box.isShown());
	box.beginFrame();
	CHECK(box.state() == 4);
	for (int i = 0; i < 5; ++i)
	{
		box.update(measure, 1);
	}
	CHECK(box.state() == 2);
	CHECK(box.help() == nullptr);
}

TEST_CASE("hud6 retail: tooltips off / the end-game timer while the box is shown hide it in the HUD")
{
	if (!haveWorld("hud6 shown early guards"))
	{
		return;
	}
	int timer = -1;
	Hud6Rig h(shared(), "FactionMen", [&]() { return timer; });
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", at.x, at.y);
	h.rig.lookAt({ at.x, at.y, 0 });
	h.select(pit);
	h.frames(40);
	REQUIRE_FALSE(h.hud->radialMenu().buttons().empty());
	const auto button = h.hud->radialMenu().buttons().front();
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(2);
	InGameUI &ui = h.hud->input().ui();
	auto enter = [&]() {
		h.hud->mouseMove(midX(button.rect), midY(button.rect));
		h.frames(3);
	};
	auto leave = [&]() {
		h.hud->mouseMove(5, 5);
		h.frames(10);
	};
	enter();
	const unsigned shown = h.hud->helpBox().helpsShown();
	REQUIRE(shown >= 1);
	REQUIRE(h.hud->helpBox().isShown());

	// RW 0x807861: tooltips off returns before the frame is marked; the next update's no-mark hide runs the 5-update countdown.
	ui.setTooltipsEnabled(false);
	h.frames(8);
	CHECK_FALSE(h.hud->helpBox().isShown());
	CHECK(h.hud->helpBox().helpsShown() == shown);
	ui.setTooltipsEnabled(true);
	leave();
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 1);
	leave();

	// The end-game timer is the other early guard and follows the same path.
	timer = 0;
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 1);
	h.frames(8);
	CHECK_FALSE(h.hud->helpBox().isShown());
	timer = -1;
	leave();
	enter();
	CHECK(h.hud->helpBox().helpsShown() == shown + 2);
}

TEST_CASE("hud6 retail: a consumed provider that composes no help is not retried")
{
	InGameHelpBox box;
	box.loaded("_level0.helpBox", [](const std::string &, const std::string &, const std::vector<std::string> &) { return true; });
	auto measure = [](CommandButtonHelp &, int) { return 50; };
	std::vector<HelpDrawOp> ops;
	box.update(measure, 1);
	box.render(0, 0, 300, 100, ops);
	box.update(measure, 1);
	REQUIRE(box.state() == 2);
	const InGameHelpBox::Provider a{ 2, 0, nullptr };
	int calls = 0;
	auto empty = [&]() {
		++calls;
		return std::shared_ptr<CommandButtonHelp>();
	};
	box.beginFrame();
	box.showHelp(a, 100, 0, empty); // kept
	box.beginFrame();
	box.showHelp(a, 101, 0, empty); // elapsed: consumed, composition empty
	CHECK(calls == 1);
	box.update(measure, 1);
	CHECK(box.help() == nullptr);
	CHECK_FALSE(box.isShown());
	box.beginFrame();
	box.showHelp(a, 102, 0, empty); // consumed for this hover: no retry
	CHECK(calls == 1);
	// leaving and returning is a new hover and composes again.
	box.beginFrame();
	box.beginFrame();
	auto help = [&]() {
		++calls;
		return std::make_shared<CommandButtonHelp>();
	};
	box.showHelp(a, 200, 0, help);
	box.beginFrame();
	box.showHelp(a, 201, 0, help);
	box.update(measure, 1);
	CHECK(calls == 2);
	CHECK(box.help() != nullptr);
	CHECK(box.isShown());
}

TEST_CASE("hud6 retail: a shown help box stays through the +0x810 gate until the hover ends")
{
	if (!haveWorld("hud6 mode gate shown"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", at.x, at.y);
	h.rig.lookAt({ at.x, at.y, 0 });
	h.select(pit);
	h.frames(40);
	REQUIRE_FALSE(h.hud->radialMenu().buttons().empty());
	const auto button = h.hud->radialMenu().buttons().front();
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(2);
	h.hud->mouseMove(midX(button.rect), midY(button.rect));
	h.frames(4);
	REQUIRE(h.hud->helpBox().isShown());

	// RW 0x807848, equal provider: the frame is marked and the consumed flag is read BEFORE vslot 0x17C. A box
	// already shown therefore stays; the gate only stops a new composition.
	InGameUI &ui = h.hud->input().ui();
	ui.setModeGate810(true);
	h.frames(6);
	CHECK(h.hud->helpBox().isShown());
	// leaving ends the hover: the box hides and a new composition under the gate is refused.
	h.hud->mouseMove(5, 5);
	h.frames(10);
	CHECK_FALSE(h.hud->helpBox().isShown());
	const unsigned shown = h.hud->helpBox().helpsShown();
	h.hud->mouseMove(midX(button.rect), midY(button.rect));
	h.frames(6);
	CHECK(h.hud->helpBox().helpsShown() == shown);
	ui.setModeGate810(false);
	// a fresh hover now shows.
	h.hud->mouseMove(5, 5);
	h.frames(10);
	h.hud->mouseMove(midX(button.rect), midY(button.rect));
	h.frames(6);
	CHECK(h.hud->helpBox().helpsShown() == shown + 1);
}

TEST_CASE("hud6 retail: the world hover refreshes without a mouse move; the unit help waits Mouse.ini's TooltipDelayTime")
{
	if (!haveWorld("hud6 refresh hover"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *horde = h.rig.make("GondorFighter", at.x, at.y);
	REQUIRE(horde);
	h.rig.lookAt({ at.x, at.y, 0 });
	h.frames(20);
	const ICoord2D px = h.rig.screenOf({ horde->getPosition()->x, horde->getPosition()->y, 10.0f });
	h.hud->mouseMove(px.x, px.y);
	h.frames(2);
	InGameUI &ui = h.hud->input().ui();
	CHECK(ui.mouseoverObject() != INVALID_ID);
	CHECK(ui.cursor() == MouseCursorName::Select);
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	h.frames(10); // ~330 ms: Mouse.ini TooltipDelayTime 800 ms not reached
	CHECK(h.hud->helpBox().helpsShown() == 0);
	h.frames(30); // > 800 ms
	CHECK(h.hud->helpBox().helpsShown() >= 1);
	CHECK(h.hud->helpBox().provider().bar == 6);
	// the horde leaves the pointer: no mouse event, but the next frames re-pick and clear the help / cursor
	Coord3D away = *horde->getPosition();
	away.x += 2000.0f;
	horde->setPosition(&away);
	h.frames(12);
	CHECK(ui.mouseoverObject() == INVALID_ID);
	CHECK(ui.cursor() == MouseCursorName::Arrow);
	h.frames(10);
	CHECK_FALSE(h.hud->helpBox().isShown());
}

TEST_CASE("hud6 retail: the command hint's cursor wins over the base Select / Arrow on a still pointer")
{
	if (!haveWorld("hud6 cursor precedence"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f, false, { { 400.0f, 0.0f } });
	Object *own = h.rig.make("GondorFighterHorde", at.x, at.y);
	Player *enemyPlayer = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemyPlayer);
	Object *foe = h.rig.make("MordorFighterHorde", at.x + 400.0f, at.y, enemyPlayer);
	REQUIRE(own);
	REQUIRE(foe);
	h.rig.lookAt({ at.x + 200.0f, at.y, 0.0f });
	h.frames(20);
	InGameUI &ui = h.hud->input().ui();
	h.select(own);
	// the selection a click makes, so the command translator knows the team exists (m_teamExists); its move hint is the green Move cursor
	{
		ClientMessage &m = h.hud->input().stream().append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
		m.appendBoolean(true);
		m.appendObjectID(own->getID());
	}
	h.frames(2);
	// an enemy under a selected army: the red attack cursor, and it stays through frames without a mouse event (the refresh re-applies
	// SelectionTranslator's base cursor first and the command hint after it, as retail's translator order does)
	const ICoord2D pxFoe = h.rig.screenOf({ foe->getPosition()->x, foe->getPosition()->y, 10.0f });
	h.hud->mouseMove(pxFoe.x, pxFoe.y);
	h.frames(2);
	CHECK(ui.cursor() == MouseCursorName::AttackObj);
	h.frames(12);
	CHECK(ui.cursor() == MouseCursorName::AttackObj);
	// empty ground under the same selection: the green move cursor, not Arrow
	const ICoord2D pxGround = h.rig.screenOf({ at.x + 200.0f, at.y + 40.0f, 0.0f });
	h.hud->mouseMove(pxGround.x, pxGround.y);
	h.frames(2);
	CHECK(ui.cursor() == MouseCursorName::Move);
	h.frames(12);
	CHECK(ui.cursor() == MouseCursorName::Move);
	// our own battalion under the same selection: still the green move cursor
	const ICoord2D pxOwn = h.rig.screenOf({ own->getPosition()->x, own->getPosition()->y, 10.0f });
	h.hud->mouseMove(pxOwn.x, pxOwn.y);
	h.frames(2);
	CHECK(ui.cursor() == MouseCursorName::Move);
	h.frames(12);
	CHECK(ui.cursor() == MouseCursorName::Move);
	// a builder over our damaged structure: the repair cursor stays over the base Select
	Object *worker = h.rig.make("GondorWorker", at.x - 400.0f, at.y + 200.0f);
	Object *barracks = h.rig.make("GondorBarracks", at.x + 400.0f, at.y + 200.0f);
	auto *body = dynamic_cast<ActiveBody *>(barracks->getBodyModule());
	REQUIRE(body);
	body->friend_setHealthRaw(body->getMaxHealth() * 0.5f);
	h.select(worker);
	{
		ClientMessage &m = h.hud->input().stream().append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
		m.appendBoolean(true);
		m.appendObjectID(worker->getID());
	}
	h.frames(2);
	const ICoord2D pxBarracks = h.rig.screenOf({ barracks->getPosition()->x, barracks->getPosition()->y, 10.0f });
	h.hud->mouseMove(pxBarracks.x, pxBarracks.y);
	h.frames(2);
	CHECK(ui.cursor() == MouseCursorName::DoRepair);
	h.frames(12);
	CHECK(ui.cursor() == MouseCursorName::DoRepair);
	// nothing selected: the base cursor is the square Select over a selectable object
	ui.deselectAll(true);
	h.frames(2);
	CHECK(ui.cursor() == MouseCursorName::Select);
	h.frames(12);
	CHECK(ui.cursor() == MouseCursorName::Select);
}

TEST_CASE("hud6 retail: a mixed selection of three fighter hordes and a tower shield guard horde drives the HUD")
{
	if (!haveWorld("hud6 mixed selection"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	auto ctx = h.hud->enterContext();
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	std::vector<Object *> hordes;
	hordes.push_back(h.rig.make("GondorFighterHorde", at.x - 180.0f, at.y));
	hordes.push_back(h.rig.make("GondorFighterHorde", at.x, at.y));
	hordes.push_back(h.rig.make("GondorFighterHorde", at.x + 180.0f, at.y));
	hordes.push_back(h.rig.make("GondorTowerShieldGuardHorde", at.x, at.y + 180.0f));
	for (Object *o : hordes)
	{
		REQUIRE(o);
	}
	h.rig.lookAt({ at.x, at.y + 90.0f, 0.0f });
	h.frames(30);
	InGameUI &ui = h.hud->input().ui();
	ui.deselectAll(true);
	for (Object *o : hordes)
	{
		ui.selectObject(o->getID());
	}
	h.frames(30);
	CHECK(ui.selected().size() == 4);
	// the control bar and the decorations of the mixed selection, then the help of each member under the pointer
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(0, 0, 360, 120, ops);
	for (Object *o : hordes)
	{
		const Coord3D &p = *o->getPosition();
		const ICoord2D px = h.rig.screenOf({ p.x, p.y, 10.0f });
		h.hud->mouseMove(px.x, px.y);
		h.frames(40);
		CHECK(ui.mouseoverObject() != INVALID_ID);
		CHECK(h.hud->helpBox().provider().bar == 6);
		h.hud->helpBoxRender(0, 0, 360, 120, ops);
		CHECK_FALSE(ops.empty());
		ops.clear();
	}
}

TEST_CASE("hud6 retail: a structure's Radial buttons ring over it, a builder's go to the side bar's frames (SetButtonState) and the help box shows")
{
	if (!haveWorld("hud6 radial and side bar"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	h.frames(30);
	REQUIRE(h.hud->palantir());
	for (const std::string &l : h.hud->stops())
	{
		if (l.rfind("[S-2703] help box settings", 0) == 0 || l.rfind("[S-2703] help box delay", 0) == 0)
		{
			FAIL_CHECK(l);
		}
	}
	REQUIRE(h.hud->helpBoxSettings().loaded);
	{
		// the lane's stops are reported (docs/STOPS.md S-2700 .. S-2703)
		const std::vector<std::string> stops = h.hud->stops();
		for (const char *id : { "[S-2700]", "[S-2701]", "[S-2702]", "[S-2703]" })
		{
			CHECK_MESSAGE(std::any_of(stops.begin(), stops.end(), [id](const std::string &l) { return l.rfind(id, 0) == 0; }), id);
		}
	}
	CHECK(h.hud->helpBoxSettings().description.pointSize == 12); // Language.ini's HelpBoxDescriptionFont replaces InGameUI's 14
	CHECK(h.hud->tooltipDelayMs() == 1);                         // ControlBar.wnd:ButtonCommand TOOLTIPDELAY
	// the help box movie came with the Palantir (OnHelpBoxLoaded)
	CHECK(h.hud->helpBox().isLoaded());
	CHECK(h.hud->helpBox().renderName().find("_Content") != std::string::npos);

	// a builder: its Radial buttons fill the side bar's frames, each shown by SetButtonState(i, "_show")
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *builder = h.rig.make("MenPorter", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(builder);
	h.frames(40);
	const size_t sideButtons = h.hud->controlBar().sideButtons().size();
	REQUIRE(sideButtons > 0);
	CHECK(h.hud->palantir()->sideBarShown());
	CHECK(h.hud->palantir()->sideBarCount() == (int)std::min<size_t>(sideButtons, 12));
	const std::vector<std::string> &calls = h.hud->palantir()->sideBarCalls();
	CHECK(std::find(calls.begin(), calls.end(), "0 _show") != calls.end());
	CHECK(h.hud->radialMenu().ops().empty()); // a DOZER's ring sits at (-999, -999)

	// a structure: the side bar hides, its Radial buttons ring over it
	const Coord3D c2 = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", c2.x, c2.y);
	h.rig.lookAt({ c2.x, c2.y, 0 });
	h.frames(10);
	h.select(pit);
	h.frames(40);
	CHECK_FALSE(h.hud->palantir()->sideBarShown());
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	const size_t n = h.hud->controlBar().sideButtons().size();
	REQUIRE(n > 0);
	REQUIRE(radial.buttons().size() == n);
	CHECK(radial.layout().buttonW() == 48);
	// the ring's centre is the structure's projected point; the first button straight above it
	CHECK(radial.layout().centerX() > 0);
	if (n >= 2)
	{
		CHECK(radial.buttons()[0].rect.x + radial.buttons()[0].rect.w / 2 == radial.layout().centerX());
		CHECK(radial.buttons()[0].rect.y < radial.layout().centerY());
	}
	bool border = false;
	for (const RadialDrawOp &op : radial.ops())
	{
		border = border || (op.kind == RadialDrawOp::OVERLAY && op.image == "RadialBorder");
	}
	CHECK(border);
	CHECK(radial.errors().empty());

	// the pointer on the first bubble: RadialOver, the help box samples its width, then shows the button's help
	const ControlBarRadialMenu::Button first = radial.buttons()[0];
	const int px = first.rect.x + first.rect.w / 2, py = first.rect.y + first.rect.h / 2;
	h.hud->mouseMove(px, py, 0);
	h.frames(2);
	CHECK(radial.hilitedSlot() == first.slot);
	bool over = false;
	for (const RadialDrawOp &op : radial.ops())
	{
		over = over || (op.kind == RadialDrawOp::OVERLAY && op.image == "RadialOver");
	}
	CHECK(over);
	CHECK(h.hud->isOverGui(px, py));
	// the device's render of the content clip gives the sampled width (state 1)
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(400.0f, 500.0f, 360.0f, 40.0f, ops);
	h.frames(6);
	CHECK(h.hud->helpBox().isShown());
	REQUIRE(h.hud->helpBox().help() != nullptr);
	{
		// its frame is PalantirExport's images, imported (S-2704): the top's fill resolves to apt_PalantirExport_2.tga
		AptRenderList rl;
		h.hud->apt().buildRenderList(rl);
		bool top = false;
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.path.find("helpBox.box.top.instance1") != std::string::npos)
			{
				for (const AptRenderFill &f : c.fills)
				{
					top = top || (f.imageResolved && f.textureName == "apt_PalantirExport_2.tga");
				}
			}
		}
		CHECK(top);
	}
	CHECK_FALSE(h.hud->helpBox().help()->name().empty());
	ops.clear();
	h.hud->helpBoxRender(400.0f, 500.0f, 360.0f, 120.0f, ops);
	CHECK_FALSE(ops.empty());
	// the pointer leaves: the box hides soon
	h.hud->mouseMove(5, 5, 0);
	h.frames(10);
	CHECK_FALSE(h.hud->helpBox().isShown());
}

TEST_CASE("hud6 retail: the Mordor fortress rings its three bubbles (the spire's fireball hides while disabled, RW 0x943B39) and its hero bubble opens the hero ring")
{
	if (!haveWorld("hud6 mordor fortress"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMordor");
	h.frames(10);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 300.0f);
	Object *citadel = h.rig.make("MordorFortressCitadel", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(citadel);
	h.frames(30);
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	std::vector<std::string> names;
	const ControlBarRadialMenu::Button *heroes = nullptr;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		names.push_back(b.source.button ? b.source.button->m_name : std::string());
		if (b.source.button && b.source.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE && b.source.button->m_name.find("Revivables") != std::string::npos)
		{
			heroes = &b;
		}
	}
	// retail-1.png: the builder (top), the upgrades and the heroes; Command_FireWeaponMordorFortressSpireFireball (HIDE_WHILE_DISABLED) is not shown
	CHECK(std::find(names.begin(), names.end(), "Command_FireWeaponMordorFortressSpireFireball") == names.end());
	CHECK(names.size() == 3);
	CHECK(radial.layout().radius() == 32);
	REQUIRE(heroes != nullptr);
	const int px = heroes->rect.x + heroes->rect.w / 2, py = heroes->rect.y + heroes->rect.h / 2;
	h.hud->mouseMove(px, py, 0);
	h.hud->mouseButton(HudInput::Button::Left, true, px, py, 0, 1000);
	h.hud->mouseButton(HudInput::Button::Left, false, px, py, 0, 1040);
	h.frames(10);
	// the hero ring (retail-2.png): the revive slots and the back button, more than the first ring had
	CHECK(radial.presses() == 1);
	CHECK(radial.buttons().size() > 3);
	bool back = false;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		back = back || (b.source.button && b.source.button->m_command == GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE);
	}
	CHECK(back);
}

TEST_CASE("hud6 retail: a right press on a bubble cancels only a CANCELABLE button's queue (RW 0x940C70); the edges are inclusive (RW 0x715A84)")
{
	if (!haveWorld("hud6 right press"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMordor");
	h.frames(10);
	h.rig.local->getMoney()->deposit(5000, false);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 300.0f);
	Object *citadel = h.rig.make("MordorFortressCitadel", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.clickSelect(citadel);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ citadel->getID() });
	h.frames(30);
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	ControlBarRadialMenu::Button porter, heroes;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		if (b.source.button && b.source.button->m_name == "Command_ConstructMordorPorter")
		{
			porter = b; // UNIT_BUILD, Options = CANCELABLE
		}
		if (b.source.button && b.source.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE && b.source.button->m_name.find("Revivables") != std::string::npos)
		{
			heroes = b; // not CANCELABLE
		}
	}
	REQUIRE(porter.slot >= 0);
	REQUIRE(heroes.slot >= 0);
	REQUIRE(porter.source.button->hasOption(COMMAND_OPTION_CANCELABLE));
	REQUIRE_FALSE(heroes.source.button->hasOption(COMMAND_OPTION_CANCELABLE));
	auto press = [&](HudInput::Button which, int x, int y) {
		h.hud->mouseMove(x, y, 0);
		h.hud->mouseButton(which, true, x, y, 0, 1000);
		h.hud->mouseButton(which, false, x, y, 0, 1040);
		h.frames(4);
	};
	// the bottom-right corner pixel itself is the button's (lo <= p <= lo + size)
	const int ex = porter.rect.x + porter.rect.w, ey = porter.rect.y + porter.rect.h;
	CHECK(radial.hit(ex, ey) == porter.slot);
	CHECK(radial.hit(ex + 1, ey) != porter.slot);
	press(HudInput::Button::Left, ex, ey); // queues a Porter
	h.frames(30);
	ProductionUpdateInterface *pu = citadel->getProductionUpdate();
	REQUIRE(pu != nullptr);
	CHECK(pu->getProductionCount() == 1);
	// a right press on the non-cancelable hero bubble: the ring takes it (the selection stays), nothing is cancelled
	const std::vector<ObjectID> before = h.hud->input().ui().selected();
	press(HudInput::Button::Right, heroes.rect.x + heroes.rect.w / 2, heroes.rect.y + heroes.rect.h / 2);
	h.frames(60);
	CHECK(radial.cancels() == 0);
	CHECK(h.hud->input().ui().selected() == before);
	CHECK(pu->getProductionCount() == 1);
	// a right press on the CANCELABLE Porter bubble cancels its queue
	press(HudInput::Button::Right, porter.rect.x + porter.rect.w / 2, porter.rect.y + porter.rect.h / 2);
	h.frames(60);
	CHECK(radial.cancels() == 1);
	CHECK(pu->getProductionCount() == 0);
}

TEST_CASE("fbsim1 retail: a right press on a CANCELABLE upgrade bubble cancels the research and refunds it (FB-0037, RW 0x940E77 / 0x940FB3 / 0x8A1140)")
{
	if (!haveWorld("fbsim1 upgrade cancel"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMordor");
	h.frames(10);
	h.rig.local->getMoney()->deposit(5000, false);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 300.0f);
	Object *citadel = h.rig.make("MordorFortressCitadel", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.clickSelect(citadel);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ citadel->getID() });
	h.frames(30);
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	auto find = [&](const char *name) {
		for (const ControlBarRadialMenu::Button &b : radial.buttons())
		{
			if (b.source.button && b.source.button->m_name == name)
			{
				return b;
			}
		}
		return ControlBarRadialMenu::Button{};
	};
	auto press = [&](HudInput::Button which, const ControlBarRadialMenu::Button &b) {
		const int x = b.rect.x + b.rect.w / 2, y = b.rect.y + b.rect.h / 2;
		h.hud->mouseMove(x, y, 0);
		h.hud->mouseButton(which, true, x, y, 0, 1000);
		h.hud->mouseButton(which, false, x, y, 0, 1040);
		h.frames(4);
	};
	const ControlBarRadialMenu::Button upgrades = find("Command_SelectUpgradesMordorFortress");
	REQUIRE(upgrades.slot >= 0); // tolerance: found on the ring (-1 = absent); the slot number is the layout's
	press(HudInput::Button::Left, upgrades); // opens the upgrades ring
	h.frames(10);
	const ControlBarRadialMenu::Button moat = find("Command_PurchaseUpgradeMordorFortressLavaMoat");
	REQUIRE(moat.slot >= 0); // tolerance: found on the ring (-1 = absent)
	REQUIRE(moat.source.button->hasOption(COMMAND_OPTION_CANCELABLE)); // retail commandbutton.ini: Options = CANCELABLE
	const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(moat.source.button->m_upgradeName);
	REQUIRE(u != nullptr);
	ProductionUpdateInterface *pu = citadel->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const unsigned before = h.rig.local->getMoney()->countMoney();
	press(HudInput::Button::Left, moat); // MSG_QUEUE_UPGRADE
	h.frames(30);
	REQUIRE(pu->isUpgradeInQueue(u));
	const unsigned paid = before - h.rig.local->getMoney()->countMoney();
	CHECK(paid > 0); // tolerance: the price is the upgrade's INI cost; the property is the refund of exactly what was paid
	// the in-progress bubble's right press: MSG_CANCEL_UPGRADE { mask bit }, the logic's cancelUpgrade refunds the stored cost
	press(HudInput::Button::Right, find("Command_PurchaseUpgradeMordorFortressLavaMoat"));
	h.frames(30);
	CHECK(radial.cancels() == 1);
	CHECK_FALSE(pu->isUpgradeInQueue(u));
	CHECK(pu->getProductionCount() == 0);
	CHECK_FALSE(citadel->hasUpgrade(u));
	CHECK(h.rig.local->getMoney()->countMoney() == before);
}

TEST_CASE("hud6 retail: the help's upgrade warnings follow RW 0x807D8F .. 0x807EB0: only after a DescriptLabel, queue full first, the local player's money")
{
	if (!haveWorld("hud6 help guards"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	h.frames(10);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *barracks = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(barracks);
	h.frames(20);
	// an OBJECT_UPGRADE button of the barracks with a DescriptLabel
	ControlBarButton up;
	for (const auto *v : { &h.hud->controlBar().palantirButtons(), &h.hud->controlBar().sideButtons(), &h.hud->controlBar().offBarButtons() })
	{
		for (const ControlBarButton &b : *v)
		{
			if (b.button && b.button->m_command == GUI_COMMAND_OBJECT_UPGRADE && !b.descriptLabel.empty() && up.slot < 0)
			{
				up = b;
			}
		}
	}
	REQUIRE(up.slot >= 0);
	Player *local = h.rig.local;
	std::u16string poorText, fullText;
	REQUIRE(h.text.fetch("TOOLTIP:TooltipNotEnoughMoneyToBuild", poorText));
	REQUIRE(h.text.fetch("TOOLTIP:TooltipCannotPurchaseBecauseQueueFull", fullText));
	auto ends = [](const std::u16string &s, const std::u16string &tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; };
	// no money: the warning follows the description
	local->getMoney()->init();
	CommandButtonHelp::Text t = CommandButtonHelp::compose(up, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
	CHECK(ends(t.description, u"\n\n" + poorText));
	// a richer selection owner does not help: the money is the LOCAL player's (RW 0xDE4928 + 0x10)
	local->getMoney()->deposit(100000, false);
	t = CommandButtonHelp::compose(up, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
	CHECK_FALSE(ends(t.description, poorText));
	local->getMoney()->init();
	// no DescriptLabel: no warning at all (the warnings live in the label's branch)
	ControlBarButton bare = up;
	bare.descriptLabel.clear();
	t = CommandButtonHelp::compose(bare, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
	CHECK(t.description.empty());
	// a queue of 0x14 entries: the queue-full text wins over the money
	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const ThingTemplate *unit = nullptr;
	for (const ControlBarButton &b : h.hud->controlBar().palantirButtons())
	{
		if (b.button && b.button->m_command == GUI_COMMAND_UNIT_BUILD && b.button->getThingTemplate() && !unit)
		{
			unit = b.button->getThingTemplate();
		}
	}
	REQUIRE(unit != nullptr);
	local->getMoney()->deposit(1000000, false);
	for (int i = 0; i < 0x14; ++i)
	{
		pu->queueCreateUnit(unit, -1, pu->requestUniqueUnitID(), 0, false, std::string(), false);
	}
	local->getMoney()->init();
	if (pu->getProductionCount() == 0x14)
	{
		t = CommandButtonHelp::compose(up, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
		CHECK(ends(t.description, u"\n\n" + fullText));
		CHECK(t.description.find(poorText) == std::u16string::npos);
	}
	else
	{
		MESSAGE("the barracks' queue holds ", pu->getProductionCount(), " entries at most: the queue-full case is not reachable with it");
	}
}

TEST_CASE("hud6 recovered upgrade selection help retail")
{
	if (!haveWorld("hud6 recovered upgrade selection help"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	Object *fighter = h.rig.make("GondorFighter", 1800, 1200);
	Object *porter = h.rig.make("MenPorter", 1900, 1200);
	REQUIRE(fighter);
	REQUIRE(porter);
	Player *player = h.rig.local;
	MapText text;
	text.t["TOOLTIP:Cost"] = u"Cost: %d";
	text.t["TOOLTIP:LacksPrerequisiteUpgradeDefault"] = u"Need technology";
	text.t["TOOLTIP:HasConflictingUpgradeDefault"] = u"Cannot apply";
	text.t["TOOLTIP:AlreadyUpgradedDefault"] = u"Already owned";
	text.t["CustomNeed"] = u"Custom prerequisite";
	text.t["CustomConflict"] = u"Custom conflict";
	CommandButton command;
	command.m_command = GUI_COMMAND_OBJECT_UPGRADE;
	command.m_upgradeName = "Upgrade_GondorHeavyArmor";
	command.m_options = COMMAND_OPTION_NEED_UPGRADE;
	command.m_neededUpgrade = { "Upgrade_TechnologyGondorHeavyArmor" };
	ControlBarButton button;
	button.button = &command;
	std::vector<ObjectID> selection{ fighter->getID() };
	auto compose = [&] {
		return CommandButtonHelp::compose(button, &text, h.rig.game->logic(), player, player,
			h.rig.game->logic().findObjectByID(selection.front()), "Men", &selection);
	};
	CHECK(compose().description == u"Need technology");
	CHECK(compose().cost == u"Cost: 300");
	command.m_lacksPrerequisiteLabel = "CustomNeed";
	CHECK(compose().description == u"Custom prerequisite");
	command.m_lacksPrerequisiteLabel.clear();
	command.m_neededUpgrade.clear();
	command.m_neededUpgradeAny = true;
	CHECK(compose().description == u"Need technology");
	command.m_neededUpgradeAny = false;
	CHECK(compose().description == u"Cannot apply");
	Object *forge = h.rig.make("GondorForge", 2200, 1200);
	REQUIRE(forge);
	CHECK(compose().description.empty());
	// lane DEVPORT-3C: RW 0x7A075A skips a STRUCTURE with AWAITING_CONSTRUCTION / PARTIALLY_CONSTRUCTED (Object + 0x114 bits 3 / 4 = model conditions 67 / 68),
	// and RW 0x7A089B never takes a STRUCTURE: an unfinished forge does not meet the RequiredObjectFilter. The DOOR_* bits 35 / 36 of the first reading do not
	for (const char *condition : { "AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED" })
	{
		const int bit = CombatNames::modelCondition(condition);
		forge->setModelConditionState(bit, true);
		CHECK(compose().description == u"Cannot apply");
		forge->setModelConditionState(bit, false);
		CHECK(compose().description.empty());
	}
	for (const char *condition : { "DOOR_3_CLOSING", "DOOR_4_WAITING_OPEN", "DOOR_4_WAITING_TO_CLOSE" })
	{
		const int bit = CombatNames::modelCondition(condition);
		forge->setModelConditionState(bit, true);
		CHECK(compose().description.empty());
		forge->setModelConditionState(bit, false);
	}
	selection = { porter->getID() };
	CHECK(compose().description == u"Cannot apply");
	command.m_conflictingLabel = "CustomConflict";
	CHECK(compose().description == u"Custom conflict");
	selection.push_back(fighter->getID());
	CHECK(compose().description.empty());
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade(command.m_upgradeName);
	REQUIRE(upgrade);
	fighter->giveUpgrade(upgrade);
	selection = { fighter->getID() };
	CHECK(compose().description == u"Already owned");
	CHECK(compose().cost.empty());
	selection.push_back(porter->getID());
	CHECK(compose().description == u"Custom conflict");
	CHECK(compose().cost == u"Cost: 300");
	porter->giveUpgrade(upgrade);
	CHECK(compose().description == u"Already owned");
	CHECK(compose().cost.empty());
}

TEST_CASE("hud6 recovered neighborhood repair help retail")
{
	if (!haveWorld("hud6 recovered neighborhood repair help"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	Object *left = h.rig.make("MenWallSegmentSmall", 1800, 1200);
	Object *middle = h.rig.make("MenWallSegmentSmall", 1900, 1200);
	Object *right = h.rig.make("MenWallSegmentSmall", 2000, 1200);
	REQUIRE(left);
	REQUIRE(middle);
	REQUIRE(right);
	REQUIRE(left->getID() + 1 == middle->getID());
	REQUIRE(middle->getID() + 1 == right->getID());
	for (Object *object : { left, middle, right })
	{
		auto *body = dynamic_cast<ActiveBody *>(object->getBodyModule());
		auto *building = dynamic_cast<GettingBuiltBehavior *>(object->findModule("GettingBuiltBehavior"));
		REQUIRE(body);
		REQUIRE(building);
		building->stopConstruction();
		body->friend_setHealthRaw(0.0f);
		object->friend_setEffectivelyDead(true);
	}
	MapText text;
	text.t["TOOLTIP:Cost"] = u"Cost: %d";
	CommandButton command;
	command.m_command = GUI_COMMAND_START_NEIGHBORHOOD_REPAIR;
	ControlBarButton button;
	button.button = &command;
	auto *building = dynamic_cast<GettingBuiltBehavior *>(middle->findModule("GettingBuiltBehavior"));
	REQUIRE(building);
	REQUIRE(building->canRebuild());
	const int one = (int)building->rebuildCost(*h.rig.local);
	// tolerance: require a usable retail per-building amount; aggregate multiples are compared exactly below.
	REQUIRE(one > 0);
	auto expected = [](int cost) { const std::string s = std::to_string(cost); return u"Cost: " + std::u16string(s.begin(), s.end()); };
	auto compose = [&] { return CommandButtonHelp::compose(button, &text, h.rig.game->logic(), h.rig.local, h.rig.local, middle, "Men"); };
	CHECK(compose().cost == expected(3 * one));
	CHECK(compose().unported.empty());
	// A live ID ends that direction even when its template and health still match.
	right->friend_setEffectivelyDead(false);
	CHECK(compose().cost == expected(2 * one));
	left->friend_setEffectivelyDead(false);
	CHECK(compose().cost == expected(one));
	middle->friend_setEffectivelyDead(false);
	CHECK(compose().cost == expected(0));
	middle->friend_setEffectivelyDead(true);
	auto *body = dynamic_cast<ActiveBody *>(middle->getBodyModule());
	REQUIRE(body);
	body->friend_setHealthRaw(body->getMaxHealth());
	CHECK_FALSE(building->canRebuild());
	CHECK(compose().cost.empty()); // RW 0x807C5B: at full health a non-FS_* object shows no repair cost at all (the gate both repair commands pass)
}

TEST_CASE("hud6 horde damage provider ordering and removal cache retail")
{
	if (!haveWorld("hud6 horde damage provider"))
	{
		return;
	}
	Rig rig(shared());
	rig.frame(40);
	Object *horde = rig.make("GondorFighterHorde", 1800, 1200);
	REQUIRE(horde);
	auto *contain = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(contain);
	const auto *members = contain->getContainedItemsList();
	// tolerance: two contained members are needed to assert source priority.
	REQUIRE(members->size() >= 2);
	auto it = members->begin();
	Object *first = *it++;
	Object *second = *it;
	Object *source = rig.make("GondorFighter", 2400, 1200);
	Object *other = rig.make("GondorFighter", 2500, 1200);
	REQUIRE(source);
	REQUIRE(other);
	auto hit = [&](Object *member, Object *by) {
		DamageInfo damage;
		damage.m_input.m_sourceID = by->getID();
		damage.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		damage.m_input.m_amount = 1.0f;
		damage.m_input.m_shouldPlayUnderAttackEva = false;
		member->attemptDamage(damage);
		auto *body = dynamic_cast<ActiveBody *>(member->getBodyModule());
		REQUIRE(body);
		REQUIRE(body->lastDamageFrame() == rig.game->logic().getFrame());
	};
	ObjectID attacker = source->getID();
	CHECK_FALSE(contain->attackedWithin(1, attacker));
	CHECK(attacker == INVALID_ID);
	hit(first, source);
	CHECK_FALSE(contain->attackedWithin(rig.game->logic().getFrame(), attacker));
	CHECK(attacker == INVALID_ID);
	CHECK_FALSE(contain->attackedWithin(rig.game->logic().getFrame() + 1, attacker));
	CHECK(contain->attackedWithin(1, attacker));
	CHECK(attacker == source->getID());
	contain->releaseMemberForGarrison(first);
	CHECK(first->getContainedBy() == nullptr);
	CHECK(contain->attackedWithin(1, attacker));
	CHECK(attacker == source->getID());
	hit(second, other);
	CHECK(contain->attackedWithin(1, attacker));
	CHECK(attacker == other->getID()); // contained list wins over the registered-ID set
	rig.frame(20);
	CHECK(contain->attackedWithin(1, attacker)); // members use four seconds, not the outer one-frame window
	rig.frame(1);
	CHECK_FALSE(contain->attackedWithin(1, attacker));
	// Removal writes the current frame with the old attacker, even though the hit is already expired.
	contain->removeFromContain(second);
	CHECK(contain->attackedWithin(5, attacker));
	CHECK(attacker == other->getID());
	rig.frame(5);
	CHECK(contain->attackedWithin(5, attacker));
	CHECK(attacker == other->getID());
	rig.frame(1);
	CHECK_FALSE(contain->attackedWithin(5, attacker));
	CHECK(attacker == INVALID_ID);
	// An undamaged garrison release must not arm the removal cache.
	REQUIRE_FALSE(members->empty());
	contain->releaseMemberForGarrison(members->front());
	CHECK_FALSE(contain->attackedWithin(5, attacker));
}

TEST_CASE("hud6 horde damage provider repair eligibility retail")
{
	if (!haveWorld("hud6 horde repair eligibility"))
	{
		return;
	}
	SharedWorld &s = shared();
	loadPrerequisiteFixture(s, "hud6 horde repair.ini",
		"ChildObject Hud6RepairHorde GondorFighterHorde\n"
		"  Behavior = GettingBuiltBehavior Hud6RepairBehavior\n"
		"    SpawnTimer = -1\n"
		"  End\n"
		"End\n");
	Rig rig(s);
	GameLogic &logic = rig.game->logic();
	logic.settings().repairDamageDelayLoaded = true;
	logic.settings().timeAfterDamageUntilRepairAllowed = 1;
	REQUIRE(logic.settings().repairDamageDelayLoaded);
	rig.frame(1 + (int)logic.settings().timeAfterDamageUntilRepairAllowed * 5);
	Object *horde = rig.make("Hud6RepairHorde", 1800, 1200);
	REQUIRE(horde);
	auto *contain = dynamic_cast<HordeContain *>(horde->getContain());
	auto *body = dynamic_cast<ActiveBody *>(horde->getBodyModule());
	auto *building = dynamic_cast<GettingBuiltBehavior *>(horde->findModule("GettingBuiltBehavior"));
	REQUIRE(contain);
	REQUIRE(body);
	REQUIRE(building);
	building->stopConstruction();
	body->friend_setHealthRaw(body->getMaxHealth() * 0.5f);
	CHECK(building->canRebuild());
	REQUIRE_FALSE(contain->getContainedItemsList()->empty());
	DamageInfo damage;
	damage.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	damage.m_input.m_amount = 1.0f;
	damage.m_input.m_shouldPlayUnderAttackEva = false;
	contain->getContainedItemsList()->front()->attemptDamage(damage);
	CHECK_FALSE(building->canRebuild());
	rig.frame(20);
	body->friend_setHealthRaw(body->getMaxHealth() * 0.5f);
	CHECK_FALSE(building->canRebuild());
	rig.frame(1);
	body->friend_setHealthRaw(body->getMaxHealth() * 0.5f);
	CHECK(building->canRebuild());
}

TEST_CASE("hud6 recovered science help retail")
{
	if (!haveWorld("hud6 recovered science help"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	Player *player = h.rig.local;
	REQUIRE(TheScienceStore);
	const ScienceType heal = TheScienceStore->getScienceFromInternalName("SCIENCE_Heal");
	const ScienceType wood = TheScienceStore->getScienceFromInternalName("SCIENCE_ElvenWood");
	REQUIRE(heal != SCIENCE_INVALID);
	REQUIRE(wood != SCIENCE_INVALID);
	MapText text;
	text.t["TOOLTIP:ScienceCost"] = u"Points: %d";
	text.t["TOOLTIP:ScienceDisabled"] = u"Disabled";
	text.t["Description"] = u"Ordinary description";
	auto points = [](int n) { const std::string s = std::to_string(n); return u"Points: " + std::u16string(s.begin(), s.end()); };
	CommandButton command;
	command.m_command = GUI_COMMAND_PURCHASE_SCIENCE;
	command.m_science = { "SCIENCE_Heal", "SCIENCE_ElvenWood" };
	ControlBarButton button;
	button.button = &command;
	button.descriptLabel = "Description";
	auto compose = [&] { return CommandButtonHelp::compose(button, &text, h.rig.game->logic(), player, player, nullptr, "Men"); };
	REQUIRE_FALSE(player->science().hasScience(heal));
	REQUIRE_FALSE(player->science().hasScience(wood));
	CHECK(compose().cost == points(TheScienceStore->getSciencePurchaseCost(heal, true)));
	REQUIRE(player->science().addScience(heal));
	CHECK(compose().cost == points(TheScienceStore->getSciencePurchaseCost(wood, true)));
	REQUIRE(player->science().addScience(wood));
	CHECK(compose().cost == points(TheScienceStore->getSciencePurchaseCost(wood, true)));
	command.m_command = GUI_COMMAND_SPECIAL_POWER;
	CHECK(compose().cost.empty()); // non-purchase multi-science buttons do not show a science cost
	command.m_science = { "SCIENCE_Heal" };
	CHECK(compose().cost == points(TheScienceStore->getSciencePurchaseCost(heal, true)));
	command.m_science.clear();
	command.m_command = GUI_COMMAND_SPELL_BOOK;
	command.m_specialPowerName = "SpellBookHeal";
	REQUIRE(TheSpecialPowerStore->findSpecialPowerTemplate(command.m_specialPowerName));
	CHECK(compose().description == u"Ordinary description");
	player->setScienceAvailability(heal, 1);
	CHECK(compose().description == u"Disabled");
	player->setScienceAvailability(heal, 2);
	CHECK(compose().description == u"Disabled");
	player->setScienceAvailability(heal, 0);
	CHECK(compose().description == u"Ordinary description");
	// The selected owner must not replace the local viewer in the disabled-science test.
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	player->setScienceAvailability(heal, 1);
	CHECK(CommandButtonHelp::compose(button, &text, h.rig.game->logic(), enemy, player, nullptr, "Men").description == u"Disabled");
	REQUIRE_FALSE(enemy->science().hasScience(heal));
	REQUIRE_FALSE(TheScienceStore->playerHasPrereqsForScience(enemy->science(), heal));
	REQUIRE(enemy->science().addScience(heal));
	enemy->setScienceAvailability(heal, 0);
	CHECK(CommandButtonHelp::compose(button, &text, h.rig.game->logic(), enemy, enemy, nullptr, "Men").description == u"Ordinary description");
}

TEST_CASE("hud6 recovered command costs retail")
{
	if (!haveWorld("hud6 recovered command costs"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMen");
	Player *player = h.rig.local;
	MapText text;
	text.t["TOOLTIP:Cost"] = u"Cost: %d";
	text.t["TOOLTIP:UnitCost"] = u"Units: %d";
	text.t["TOOLTIP:CommandPoints"] = u"CP: %d";
	text.t["APT:RankLabel"] = u"Rank: %d";
	text.t["CONTROLBAR:GenericRecruitHero"] = u"Recruit %s";
	text.t["CONTROLBAR:GenericReviveHero"] = u"Revive %s";
	auto number = [](int n) { const std::string s = std::to_string(n); return std::u16string(s.begin(), s.end()); };
	CommandButton command;
	ControlBarButton button;
	button.button = &command;
	Object *producer = h.rig.make("GondorHeroStatue", 1800, 1200);
	REQUIRE(producer);
	auto compose = [&] { return CommandButtonHelp::compose(button, &text, h.rig.game->logic(), player, player, producer, "Men"); };
	command.m_command = GUI_COMMAND_REVIVE;
	// tolerance: the retail roster is only used to exercise an available revive record.
	REQUIRE(player->heroes().size() > 0);
	button.reviveIndex = 0;
	HeroRecord *record = player->heroes().at(0);
	REQUIRE(record);
	CHECK(compose().name.find(u"Recruit ") == 0);
	CHECK(compose().cost == u"Cost: " + number(player->heroes().costAt(h.rig.game->logic(), *player, 0, producer)));
	CHECK(compose().description.find(u"\nRank: " + number(record->rank)) != std::u16string::npos);
	const ThingTemplate *hero = player->heroes().templateAt(h.rig.game->logic(), 0);
	REQUIRE(hero);
	// tolerance: the retail roster needs a positive-CP hero to exercise the command-points line.
	REQUIRE(BuildAssistant::commandPoints(*hero) > 0);
	CHECK(compose().commandPoints == u"CP: " + number(BuildAssistant::commandPoints(*hero)));
	record->dead = true;
	record->rank = 4;
	record->cost = 321;
	CHECK(compose().name.find(u"Revive ") == 0);
	CHECK(compose().description.find(u"\nRank: 4") != std::u16string::npos);
	CHECK(compose().cost == u"Cost: " + number(player->heroes().costAt(h.rig.game->logic(), *player, 0, producer)));
	button.reviveIndex = -1;
	CHECK(compose().cost.empty());
	command.m_command = GUI_COMMAND_START_SELF_REPAIR;
	auto *building = dynamic_cast<GettingBuiltBehavior *>(producer->findModule("GettingBuiltBehavior"));
	auto *body = dynamic_cast<ActiveBody *>(producer->getBodyModule());
	REQUIRE(building);
	REQUIRE(body);
	body->setInitialHealth(100);
	CHECK(compose().cost.empty());
	body->friend_setHealthRaw(body->getMaxHealth() * 0.5f);
	CHECK(compose().cost == u"Cost: " + number((int)building->rebuildCost(*player)));
	command.m_command = GUI_COMMAND_SPECIAL_POWER;
	const SpecialPowerTemplate *sacrifice = nullptr;
	for (size_t i = 0; i < TheSpecialPowerStore->getNumSpecialPowers(); ++i)
	{
		const auto *p = TheSpecialPowerStore->getSpecialPowerByIndex(i);
		if (p->m_unitCost != 0) { sacrifice = p; break; }
	}
	REQUIRE(sacrifice);
	command.m_specialPowerName = sacrifice->getName();
	CHECK(compose().cost == u"Units: " + number(sacrifice->m_unitCost));
	command.m_specialPowerName = "SpellBookHeal";
	CHECK(compose().cost.empty());
	producer = h.rig.make("CampFlag", 2200, 1400);
	REQUIRE(producer);
	auto *castle = dynamic_cast<CastleBehavior *>(producer->findModule("CastleBehavior"));
	REQUIRE(castle);
	REQUIRE(castle->canUnpack(false));
	command.m_command = GUI_COMMAND_CASTLE_UNPACK;
	CHECK(compose().cost == u"Cost: 2000"); // retail CampFlag's Men entry
	producer = nullptr;
	CHECK(compose().cost.empty());
}

TEST_CASE("hud6 module offered template retail")
{
	if (!haveWorld("hud6 module offered template"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionWild");
	Object *producer = h.rig.make("WildFortressCitadel", 1800, 1200);
	REQUIRE(producer);
	auto *module = dynamic_cast<SpawnUnitBehavior *>(producer->findModule("SpawnUnitBehavior"));
	REQUIRE(module);
	const auto *data = dynamic_cast<const SpawnUnitBehaviorModuleData *>(module->getModuleData());
	REQUIRE(data);
	REQUIRE_FALSE(data->m_unitName.empty());
	CHECK(module->getUnitName() == data->m_unitName);
	CHECK(module->isUnported()); // automatic spawning is a separate S-140 gap
	const ThingTemplate *offered = s.world->things().findTemplate(data->m_unitName);
	REQUIRE(offered);
	producer->setCommandSetOverride("Hud6S207MissingCommandSet");
	REQUIRE_FALSE(TheCommandStore->findCommandSet(producer->getCommandSetName()));
	CHECK(BuildAssistant::isInProducersCommandSet(*producer, offered, -1));
	CHECK(BuildAssistant::isInProducersCommandSet(*producer, offered, 999));
	CHECK_FALSE(BuildAssistant::isInProducersCommandSet(*producer, nullptr, -1));
	CHECK_FALSE(BuildAssistant::isInProducersCommandSet(*producer, nullptr, 999));
	// A provider match must not consult the command store or player build gates.
	h.rig.local->setCanBuildUnits(false);
	CHECK_FALSE(BuildAssistant::playerCanBuild(*h.rig.local, offered, h.rig.game->logic()));
	CommandStore *saved = TheCommandStore;
	struct RestoreStore { CommandStore *store; ~RestoreStore() { TheCommandStore = store; } } restore{ saved };
	TheCommandStore = nullptr;
	CHECK_NOTHROW(BuildAssistant::isInProducersCommandSet(*producer, offered, -1));
	TheCommandStore = saved;
	h.rig.local->setCanBuildUnits(true);
	// The outer factory-disabled gate still precedes the provider query.
	producer->setScriptStatus(1, true);
	CHECK(BuildAssistant::canMakeUnit(*producer, offered, -1) == CANMAKE_FACTORY_IS_DISABLED);
}

TEST_CASE("hud6 module offered template exact names retail")
{
	if (!haveWorld("hud6 module offered template names"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	loadPrerequisiteFixture(s, "hud6 s207 module offers.ini",
		"ChildObject Hud6S207Offered GondorFighter\n"
		"  Buildable = No\n"
		"  EquivalentTo = GondorFighter\n"
		"End\n"
		"ChildObject Hud6S207OfferProducer GondorFighter\n"
		"  CommandSet = Hud6S207EmptyCommandSet\n"
		"  Behavior = SpawnUnitBehavior ModuleTag_FirstOffer\n"
		"    UnitName = hud6s207offered\n"
		"    UnitCommand = Command_None\n"
		"    SpawnOnce = Yes\n"
		"  End\n"
		"  Behavior = SpawnUnitBehavior ModuleTag_SecondOffer\n"
		"    UnitName = Hud6S207Offered\n"
		"  End\n"
		"End\n"
		"ChildObject Hud6S207WrongCase GondorFighter\n"
		"  CommandSet = Hud6S207EmptyCommandSet\n"
		"  Behavior = SpawnUnitBehavior ModuleTag_OnlyOffer\n"
		"    UnitName = hud6s207offered\n"
		"  End\n"
		"End\n"
		"CommandSet Hud6S207EmptyCommandSet\n"
		"End\n");
	Object *producer = h.rig.make("Hud6S207OfferProducer", 1800, 1200);
	Object *wrongCase = h.rig.make("Hud6S207WrongCase", 2000, 1200);
	REQUIRE(producer);
	REQUIRE(wrongCase);
	const ThingTemplate *offered = s.world->things().findTemplate("Hud6S207Offered");
	const ThingTemplate *equivalent = s.world->things().findTemplate("GondorFighter");
	REQUIRE(offered);
	REQUIRE(equivalent);
	REQUIRE(BuildAssistant::isEquivalentTo(offered, equivalent));
	CHECK_FALSE(BuildAssistant::playerCanBuild(*h.rig.local, offered, h.rig.game->logic()));
	CHECK(BuildAssistant::isInProducersCommandSet(*producer, offered, -1));
	CHECK_FALSE(BuildAssistant::isInProducersCommandSet(*wrongCase, offered, -1));
	CHECK_FALSE(BuildAssistant::isInProducersCommandSet(*producer, equivalent, -1));
}

TEST_CASE("hud6 prerequisite combined requirements retail")
{
	if (!haveWorld("hud6 prerequisite combined requirements"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	loadPrerequisiteFixture(s, "hud6 s207 combined.ini",
		"ChildObject Hud6S207CombinedA GondorFighter\n"
		"  DisplayName = HUD6:S207CombinedA\n"
		"End\n"
		"ChildObject Hud6S207CombinedB GondorFighter\n"
		"  DisplayName = HUD6:S207CombinedB\n"
		"End\n"
		"ChildObject Hud6S207CombinedTarget GondorFighter\n"
		"  Buildable = Yes\n"
		"  Prerequisites\n"
		"    Object = Hud6S207CombinedA Hud6S207CombinedB\n"
		"    Science = SCIENCE_Heal\n"
		"    PlayerUpgrade = Upgrade_TechnologyGondorHeavyArmor\n"
		"  End\n"
		"End\n");
	const ThingTemplate *target = s.world->things().findTemplate("Hud6S207CombinedTarget");
	REQUIRE(target);
	Player *player = h.rig.local;
	REQUIRE(player);
	const ScienceType heal = TheScienceStore->getScienceFromInternalName("SCIENCE_Heal");
	REQUIRE(heal != SCIENCE_INVALID);
	REQUIRE_FALSE(player->hasScience(heal));
	const UpgradeTemplate *upgrade = s.world->upgrades().findUpgrade("Upgrade_TechnologyGondorHeavyArmor");
	REQUIRE(upgrade);
	REQUIRE(upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER);
	REQUIRE_FALSE(player->hasUpgradeComplete(upgrade));
	MapText text;
	addPrerequisiteHelpText(text);
	text.t["HUD6:S207CombinedA"] = u"Combined A";
	text.t["HUD6:S207CombinedB"] = u"Combined B";
	text.t["TOOLTIP:PrerequisiteFixtureDescription"] = u"Build this.";
	CHECK_FALSE(BuildAssistant::playerCanBuild(*player, target, h.rig.game->logic()));
	CHECK(composePrerequisiteHelp(h, target->getName(), player, player, text, "TOOLTIP:PrerequisiteFixtureDescription").description ==
		u"Build this.\nRequires: Combined A or Combined B, Promotion, Upgrade required");

	Object *alternative = h.rig.make("Hud6S207CombinedB", 1800, 1200);
	REQUIRE(alternative);
	REQUIRE(player->science().grantScience(heal));
	player->addUpgrade(upgrade, Player::UPGRADE_STATUS_COMPLETE, true);
	CHECK(BuildAssistant::playerCanBuild(*player, target, h.rig.game->logic()));
	CHECK(composePrerequisiteHelp(h, target->getName(), player, player, text, "TOOLTIP:PrerequisiteFixtureDescription").description == u"Build this.");
}

TEST_CASE("hud6 prerequisite ownership and construction retail")
{
	if (!haveWorld("hud6 prerequisite ownership and construction"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	loadPrerequisiteFixture(s, "hud6 s207 ownership.ini",
		"ChildObject Hud6S207OwnerUnit GondorFighter\n"
		"  DisplayName = HUD6:S207OwnerUnit\n"
		"End\n"
		"ChildObject Hud6S207OwnerTarget GondorFighter\n"
		"  Buildable = Yes\n"
		"  Prerequisites\n"
		"    Object = Hud6S207OwnerUnit\n"
		"  End\n"
		"End\n");
	Player *localPlayer = h.rig.local;
	REQUIRE(localPlayer);
	Player *owner = h.rig.game->logic().players().getNthPlayer(1);
	REQUIRE(owner);
	Object *localUnit = h.rig.make("Hud6S207OwnerUnit", 1800, 1200, localPlayer);
	Object *ownerUnit = h.rig.make("Hud6S207OwnerUnit", 2000, 1200, owner);
	REQUIRE(localUnit);
	REQUIRE(ownerUnit);
	ownerUnit->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	MapText text;
	addPrerequisiteHelpText(text);
	text.t["HUD6:S207OwnerUnit"] = u"Owner unit";
	CHECK_FALSE(BuildAssistant::playerCanBuild(*owner, s.world->things().findTemplate("Hud6S207OwnerTarget"), h.rig.game->logic()));
	CHECK(composePrerequisiteHelp(h, "Hud6S207OwnerTarget", owner, localPlayer, text).description == u"Requires: Owner unit");
	ownerUnit->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
	ownerUnit->friend_setEffectivelyDead(true);
	CHECK(BuildAssistant::playerCanBuild(*owner, s.world->things().findTemplate("Hud6S207OwnerTarget"), h.rig.game->logic()));
	CHECK(composePrerequisiteHelp(h, "Hud6S207OwnerTarget", owner, localPlayer, text).description.empty());
}

TEST_CASE("hud6 prerequisite three way or retail")
{
	if (!haveWorld("hud6 prerequisite three-way OR"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	loadPrerequisiteFixture(s, "hud6 s207 three-way.ini",
		"ChildObject Hud6S207ThreeA GondorFighter\n"
		"  DisplayName = HUD6:S207ThreeA\n"
		"End\n"
		"ChildObject Hud6S207ThreeB GondorFighter\n"
		"  DisplayName = HUD6:S207ThreeB\n"
		"End\n"
		"ChildObject Hud6S207ThreeC GondorFighter\n"
		"  DisplayName = HUD6:S207ThreeC\n"
		"End\n"
		"ChildObject Hud6S207ThreeTarget GondorFighter\n"
		"  Prerequisites\n"
		"    Object = Hud6S207ThreeA Hud6S207ThreeB Hud6S207ThreeC\n"
		"  End\n"
		"End\n");
	MapText text;
	addPrerequisiteHelpText(text);
	text.t["HUD6:S207ThreeA"] = u"Three A";
	text.t["HUD6:S207ThreeB"] = u"Three B";
	text.t["HUD6:S207ThreeC"] = u"Three C";
	CHECK(composePrerequisiteHelp(h, "Hud6S207ThreeTarget", h.rig.local, h.rig.local, text).description == u"Requires: Three B or Three C");
	CHECK_FALSE(BuildAssistant::playerCanBuild(*h.rig.local, s.world->things().findTemplate("Hud6S207ThreeTarget"), h.rig.game->logic()));
}

TEST_CASE("hud6 prerequisite override clears inherited rows retail")
{
	if (!haveWorld("hud6 prerequisite override clears inherited rows"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	loadPrerequisiteFixture(s, "hud6 s207 override base.ini",
		"Object Hud6S207OverrideUnit\n"
		"  DisplayName = HUD6:S207OverrideUnit\n"
		"End\n"
		"Object Hud6S207OverrideTarget\n"
		"  Prerequisites\n"
		"    Object = Hud6S207OverrideUnit\n"
		"    Science = SCIENCE_Heal\n"
		"  End\n"
		"End\n");
	loadPrerequisiteFixture(s, "hud6 s207 override map.ini",
		"Object Hud6S207OverrideTarget\n"
		"  Prerequisites\n"
		"    Science = SCIENCE_Heal\n"
		"  End\n"
		"End\n",
		INI_LOAD_CREATE_OVERRIDES);
	MapText text;
	addPrerequisiteHelpText(text);
	text.t["HUD6:S207OverrideUnit"] = u"Override unit";
	CHECK(composePrerequisiteHelp(h, "Hud6S207OverrideTarget", h.rig.local, h.rig.local, text).description == u"Requires: Promotion");
}

TEST_CASE("bugs1 retail: the help box over a barracks bubble keeps its content: a seek back keeps a clip a script moved (RW 0xB0D610 mergeState, GetASChanged)")
{
	// Devlog #4's empty help box: Gondor Soldiers' help is taller than the box's minimum, so InGameHelpBox.apt's box eases `top._y` toward its target in a
	// frame 5 / 6 loop (gotoAndPlay("loop")) and places `content` (the native text) at frame 6 once it arrives. Re-applying frame 3's matrix on each seek
	// back kept `top` short of the target, the loop never ended and the content never stayed
	if (!haveWorld("bugs1 help box content"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	h.frames(30);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *barracks = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(barracks);
	h.frames(40);
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	const ControlBarRadialMenu::Button *unit = nullptr;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		if (!unit && b.source.button && b.source.button->m_command == GUI_COMMAND_UNIT_BUILD)
		{
			unit = &b;
		}
	}
	REQUIRE(unit != nullptr);
	h.hud->mouseMove(unit->rect.x + unit->rect.w / 2, unit->rect.y + unit->rect.h / 2, 0);
	h.frames(2);
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(400.0f, 500.0f, 360.0f, 40.0f, ops); // the content's sampled width (state 1)
	auto contentShown = [&]() {
		AptRenderList rl;
		h.hud->apt().buildRenderList(rl);
		for (const AptRenderCommand &cmd : rl.commands)
		{
			if (cmd.kind == AptRenderCommand::Kind::Placeholder && cmd.path.find("helpBox.box.content") != std::string::npos)
			{
				return true;
			}
		}
		return false;
	};
	h.frames(60);
	REQUIRE(h.hud->helpBox().isShown());
	REQUIRE(h.hud->helpBox().help() != nullptr);
	MESSAGE("help: " << std::string(h.hud->helpBox().help()->name().begin(), h.hud->helpBox().help()->name().end()));
	// the box arrived (frame 6 placed `content` and the loop went on to loopFade) and stays: every one of the next frames draws the content
	int shown = 0;
	for (int i = 0; i < 20; ++i)
	{
		h.frames(1);
		shown += contentShown() ? 1 : 0;
	}
	CHECK(shown == 20);
	ops.clear();
	h.hud->helpBoxRender(400.0f, 500.0f, 360.0f, 120.0f, ops);
	CHECK(std::count_if(ops.begin(), ops.end(), [](const HelpDrawOp &o) { return o.kind == HelpDrawOp::TEXT; }) >= 3); // name, cost, description
}

// ---------------------------------------------------------------------------------------------------------------------------------
// lane OWNERFIX-3 (the owner's playtest, 2026-10-17): another player's command set shown disabled (RW 0x71EBDA / 0x94397F) and the production counts of the
// command windows (RW 0x729D9A: the bubbles' number RW 0x4A4CD3, the Palantir's RW 0x9D7054)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
// a fixed text measure: every character 6 pixels wide, every line 13 high (the device measures with its fonts; the placement rule is what is tested)
struct FixedMetrics : FontMetricsSource
{
	int fontHeight(const GameFont &) override { return 13; }
	int textWidth(const GameFont &, const UnicodeString &text) override { return 6 * (int)text.size(); }
	int wrappedHeight(const GameFont &f, const UnicodeString &, int) override { return fontHeight(f); }
};

// every button the bar shows now, by CommandButton name -> state
std::map<std::string, ButtonState> shownStates(ControlBar &bar)
{
	std::map<std::string, ButtonState> out;
	for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons(), &bar.offBarButtons() })
	{
		for (const ControlBarButton &b : *list)
		{
			if (b.button)
			{
				out[b.button->m_name] = b.state;
			}
		}
	}
	return out;
}

const ControlBarButton *shownButton(const std::vector<ControlBarButton> &list, const std::string &name)
{
	for (const ControlBarButton &b : list)
	{
		if (b.button && b.button->m_name == name)
		{
			return &b;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("ownerfix3 retail: another player's hero, barracks and fortress show their command set with every button disabled (RW 0x71EBDA / 0x94397F)")
{
	if (!haveWorld("ownerfix3 foreign command set"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	h.frames(10);
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	REQUIRE(enemy != h.rig.local);
	// the retail CommandSet names of the templates (CommandSet = ... in gothmog.ini / the barracks' and fortress' INI): the bar shows the object's own set
	const struct
	{
		const char *tmpl, *set;
	} cases[] = { { "MordorGothmog", "GothmogCommandSet" }, { "GondorBarracks", "GondorBarracksCommandSet" }, { "MordorFortressCitadel", nullptr } };
	float x = 2200.0f;
	for (const auto &c : cases)
	{
		CAPTURE(c.tmpl);
		// the oracle: the same template the local player owns, as its owner sees it
		const Coord3D a = h.rig.freeSpot(x, 1400, 260.0f);
		Object *own = h.rig.make(c.tmpl, a.x, a.y);
		const Coord3D b = h.rig.freeSpot(x, 2000, 260.0f);
		Object *foreign = h.rig.make(c.tmpl, b.x, b.y, enemy);
		x += 400.0f;
		h.rig.lookAt(a);
		h.frames(10);
		h.select(own);
		h.frames(20);
		ControlBar &bar = h.hud->controlBar();
		REQUIRE(bar.sourceObject() == own->getID());
		CHECK_FALSE(bar.foreignView());
		const std::map<std::string, ButtonState> ownView = shownStates(bar);
		REQUIRE_FALSE(ownView.empty());
		const bool anyEnabled = std::any_of(ownView.begin(), ownView.end(), [](const auto &e) { return e.second == ButtonState::Enabled; });
		CHECK(anyEnabled); // the owner can use something: the foreign view's disabling is visible

		h.rig.lookAt(b);
		h.frames(10);
		h.select(foreign);
		h.frames(20);
		REQUIRE(bar.sourceObject() == foreign->getID());
		CHECK(bar.foreignView());
		if (c.set)
		{
			CHECK(bar.commandSetName() == c.set);
		}
		const std::map<std::string, ButtonState> view = shownStates(bar);
		// the same buttons as the owner's view (the fortress's spire fireball, HIDE_WHILE_DISABLED and disabled for both, hides for both: RW 0x943B39)
		std::vector<std::string> ownNames, names;
		for (const auto &e : ownView)
		{
			ownNames.push_back(e.first);
		}
		for (const auto &e : view)
		{
			names.push_back(e.first);
			CHECK_MESSAGE(e.second == ButtonState::Restricted, e.first); // availability 0: the "_disabled" clip, the grey image
		}
		CHECK(names == ownNames);
		if (std::string(c.tmpl) == "MordorFortressCitadel")
		{
			CHECK(view.count("Command_FireWeaponMordorFortressSpireFireball") == 0);
		}
		// no clock, glow or count for a foreign view (RW 0x943C7A, RW 0x9D2B49)
		for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons() })
		{
			for (const ControlBarButton &cb : *list)
			{
				CHECK(cb.timer == -1.0f); // no clock: the "none" value
				CHECK_FALSE(cb.autoAbility);
				CHECK(cb.productionCount == 0);
			}
		}
		// the ring is cleared for another player's object while the local player is active (RW 0x945938); the owner's own had one when it has Radial buttons
		CHECK(h.hud->radialMenu().buttons().empty());
		// the Palantir arc shows them (its clips for the arc's buttons were made)
		REQUIRE(h.hud->palantir());
		// presses send nothing: left, right, every shown button
		const size_t sent = h.hud->input().messageLog().size();
		for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons(), &bar.offBarButtons() })
		{
			const std::vector<ControlBarButton> copy = *list;
			for (const ControlBarButton &cb : copy)
			{
				CHECK_FALSE(bar.pressButton(cb.slot, cb.inPalantir));
				CHECK_FALSE(bar.rightPressButton(cb.slot, cb.inPalantir));
				CHECK_FALSE(bar.cancelQueued(cb.slot, cb.inPalantir));
			}
		}
		h.frames(5);
		CHECK(h.hud->input().messageLog().size() == sent);
		// selecting the own object again: the set is usable again (no stale foreign state)
		h.select(own);
		h.frames(10);
		CHECK_FALSE(bar.foreignView());
		std::vector<std::string> again;
		for (const auto &e : shownStates(bar))
		{
			again.push_back(e.first);
		}
		CHECK(again == ownNames);
	}
}


TEST_CASE("ownerfix3 retail: the production count on the barracks' bubbles and the Palantir (RW 0x729D9A / 0x4A4CD3 / 0x9D7054)")
{
	if (!haveWorld("ownerfix3 production count"))
	{
		return;
	}
	SharedWorld &s = shared();
	FixedMetrics metrics;
	Hud6Rig h(s, "FactionMen");
	auto context = h.hud->enterContext();
	h.hud->setFontMetrics(&metrics);
	h.frames(10);
	h.rig.local->getMoney()->deposit(20000, false);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 300.0f);
	Object *barracks = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.clickSelect(barracks);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ barracks->getID() });
	h.hud->mouseMove(5, 5, 0); // the pointer off the bubbles: the enabled colours
	h.frames(10);
	ControlBar &bar = h.hud->controlBar();
	// the soldiers (ShowProductionCount = Yes in commandbutton.ini) and the archers (Yes); the barracks' level upgrade has none
	const char *soldiers = "Command_ConstructGondorFighterHorde";
	const ControlBarButton *sb = shownButton(bar.sideButtons(), soldiers);
	REQUIRE(sb != nullptr);
	REQUIRE(sb->button->m_showProductionCount);
	REQUIRE(sb->state == ButtonState::Enabled);
	CHECK(sb->productionCount == 0);
	const int slot = sb->slot;
	for (int i = 0; i < 3; ++i)
	{
		REQUIRE(bar.pressButton(slot, false));
		h.frames(2);
	}
	h.frames(5);
	sb = shownButton(bar.sideButtons(), soldiers);
	REQUIRE(sb != nullptr);
	CHECK(sb->productionCount == 3); // three presses, three queue entries (a horde is one entry)
	CHECK(sb->queued == 3); // three hordes of one each: the quantity count agrees here
	for (const ControlBarButton &b : bar.sideButtons())
	{
		if (b.button && !b.button->m_showProductionCount)
		{
			CHECK_MESSAGE(b.productionCount == 0, b.button->m_name);
		}
	}
	// the bubble: "3" above the clock/frame in ControlBar.wnd:ButtonCommand's font and ENABLED / ENABLEDBORDER colours
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	CHECK(radial.errors().empty());
	const ControlBarRadialMenu::Button *bubble = nullptr;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		bubble = b.slot == slot ? &b : bubble;
	}
	REQUIRE(bubble != nullptr);
	int texts = 0, textAt = -1, borderAt = -1;
	for (size_t i = 0; i < radial.ops().size(); ++i)
	{
		const RadialDrawOp &op = radial.ops()[i];
		if (op.slot == slot && op.kind == RadialDrawOp::OVERLAY)
		{
			borderAt = (int)i;
		}
		if (op.kind == RadialDrawOp::TEXT)
		{
			++texts;
			REQUIRE(op.slot == slot);
			textAt = (int)i;
			CHECK(op.text == "3");
			CHECK(op.font.name == "Arial");
			CHECK(op.font.pointSize == 8);
			CHECK_FALSE(op.font.bold);
			// x = (int)(48 * 0.5f - 6 * 0.5f + left), y = top + 48 - 13 (the exact WND formula)
			CHECK((int)op.x == (int)((float)bubble->rect.w * 0.5f - 3.0f + (float)bubble->rect.x));
			CHECK((int)op.y == bubble->rect.y + bubble->rect.h - 13);
			CHECK(op.color == 0xFFFEFEFEu);    // ENABLED: 254 254 254 255
			CHECK(op.dropColor == 0xFF000000u); // ENABLEDBORDER: 0 0 0 255
		}
	}
	CHECK(texts == 1);
	REQUIRE(textAt != -1);
	CHECK(textAt == borderAt - 1); // the count is drawn after the clock and before the RadialBorder (RW 0x4A5112)
	// the pointer over the bubble: the HILITE pair
	h.hud->mouseMove(bubble->rect.x + bubble->rect.w / 2, bubble->rect.y + bubble->rect.h / 2, 0);
	h.frames(2);
	for (const RadialDrawOp &op : radial.ops())
	{
		if (op.kind == RadialDrawOp::TEXT)
		{
			CHECK(op.color == 0xFFFFFF00u);     // HILITE: 255 255 0 255
			CHECK(op.dropColor == 0xFF000080u); // HILITEBORDER: 0 0 128 255
		}
	}
	h.hud->mouseMove(5, 5, 0);
	// the Palantir arc's clip of the same button: "3" (L"%d", no "x")
	const ControlBarButton *pb = shownButton(bar.palantirButtons(), soldiers);
	if (pb)
	{
		h.frames(5);
		REQUIRE(h.hud->palantir());
		const std::string *t = h.hud->palantir()->productionCountText(pb->slot, true);
		REQUIRE(t != nullptr);
		CHECK(*t == "3");
	}
	// a cancel (right press on the CANCELABLE button) takes one entry off: "2"; cancelling the rest leaves no number (" " on the Palantir)
	REQUIRE(bar.cancelQueued(slot, false));
	h.frames(10);
	sb = shownButton(bar.sideButtons(), soldiers);
	REQUIRE(sb != nullptr);
	CHECK(sb->productionCount == 2);
	REQUIRE(bar.cancelQueued(slot, false, true));
	h.frames(10);
	sb = shownButton(bar.sideButtons(), soldiers);
	REQUIRE(sb != nullptr);
	CHECK(sb->productionCount == 0);
	CHECK(std::none_of(radial.ops().begin(), radial.ops().end(), [](const RadialDrawOp &op) { return op.kind == RadialDrawOp::TEXT; }));
	if (pb)
	{
		h.frames(5);
		const std::string *t = h.hud->palantir()->productionCountText(pb->slot, true);
		REQUIRE(t != nullptr);
		CHECK(*t == " ");
	}
}

TEST_CASE("ownerfix3 retail: ControlBar.wnd:ButtonCommand's font and text colours; the scanner refuses what it cannot read")
{
	if (!haveWorld("ownerfix3 command window look"))
	{
		return;
	}
	SharedWorld &s = shared();
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(s.mount->fs->readFile("window\\controlbar.wnd", bytes, &error), error);
	const std::string text(bytes.begin(), bytes.end());
	CommandWindowLook look;
	REQUIRE_MESSAGE(CommandWindowLook::scan(text, look, &error), error);
	CHECK(look.loaded);
	CHECK(look.font.name == "Arial");
	CHECK(look.font.pointSize == 8);
	CHECK_FALSE(look.font.bold);
	CHECK(look.enabled == 0xFFFEFEFEu);
	CHECK(look.enabledBorder == 0xFF000000u);
	CHECK(look.disabled == 0xFFC0C0C0u);
	CHECK(look.disabledBorder == 0xFF404040u);
	CHECK(look.hilite == 0xFFFFFF00u);
	CHECK(look.hiliteBorder == 0xFF000080u);
	// refusals, each named; the look stays unloaded
	const size_t at = text.find("\"ControlBar.wnd:ButtonCommand\"");
	REQUIRE(at != std::string::npos);
	const size_t font = text.find("FONT", at), color = text.find("TEXTCOLOR", at);
	REQUIRE(font != std::string::npos);
	REQUIRE(color != std::string::npos);
	auto refused = [](const std::string &t, const char *why) {
		CommandWindowLook l;
		std::string e;
		CHECK_FALSE(CommandWindowLook::scan(t, l, &e));
		CHECK_FALSE(l.loaded);
		CHECK_MESSAGE(e.find(why) != std::string::npos, e);
	};
	refused("", "no such window");
	refused(text.substr(0, at), "no such window"); // truncated before the window
	std::string noFont = text;
	noFont.replace(font, 4, "FXNT");
	refused(noFont, "no FONT");
	std::string noColor = text;
	noColor.replace(color, 9, "TEXTCOLXR");
	refused(noColor, "no TEXTCOLOR");
	std::string badLabel = text;
	badLabel.replace(badLabel.find("DISABLED:", color), 9, "DISABLEX:");
	refused(badLabel, "is not DISABLED");
	std::string badValue = text;
	badValue.replace(badValue.find("254 254 254 255", color), 15, "254 254 999 255");
	refused(badValue, "bad component");
	std::string shortColor = text;
	const size_t hb = shortColor.find("HILITEBORDER", color);
	shortColor.erase(hb, shortColor.find(';', hb) - hb);
	refused(shortColor, "tokens");
	std::string badSize = text;
	badSize.replace(badSize.find("SIZE: 8", font), 7, "SIZE: x");
	refused(badSize, "FONT is not");
}

TEST_CASE("hud6 retail: the resource barrel and faction crest mapped images and textures are loadable")
{
	if (!haveWorld("hud6 resource icon assets"))
	{
		return;
	}
	SharedWorld &s = shared();
	MappedImageCollection images;
	INIEnvironment env;
	env.fileSystem = s.mount->fs.get();
	images.registerBlocks(env.blocks);
	INI ini(env);
	ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
	for (const char *name : { "Resource_Icon", "ResourceBar_Mordor", "ResourceBar_Gondor" })
	{
		INFO(name);
		const ::Image *image = images.findImageByName(name);
		REQUIRE(image != nullptr);
		CHECK(image->imageSize.x > 0);
		CHECK(image->imageSize.y > 0);
		CHECK_FALSE(image->filename.empty());
		// The texture name is resolved by HudDevice::textureFor/imageFor (the retail texture search), not by a raw
		// readFile of the mapped-image texture name: ResourceBarIcons.tga is an atlas name, not a loose file path.
	}
}

TEST_CASE("hud6 inventory: RenderImage clips without _imageMap in the loaded HUD movies")
{
	if (!haveWorld("hud6 render image inventory"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMordor");
	h.frames(30);
	AptRenderList rl;
	h.hud->apt().buildRenderList(rl);
	std::set<std::string> paths;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.symbolName != "RenderImage")
		{
			continue;
		}
		std::string imageMap;
		for (const auto &v : c.nativeVars)
		{
			if (v.first == "_imageMap")
			{
				imageMap = v.second;
			}
		}
		if (imageMap.empty())
		{
			paths.insert(c.path);
		}
	}
	for (const std::string &p : paths)
	{
		std::printf("RenderImage without _imageMap: %s\n", p.c_str());
	}
	CHECK(paths.count("_level1.ResourceBar.ResourceIcon.~Enabled") == 1);
	CHECK(paths.count("_level1.CommandUI.Portrait") == 1);
}

TEST_CASE("hud6 retail: a unit behind a NeededUpgrade is _disabled, not _extraDisabled")
{
	if (!haveWorld("hud6 needed upgrade extra disabled"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMordor");
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *palace = h.rig.make("MordorHaradrimPalace", at.x, at.y);
	REQUIRE(palace != nullptr);
	h.rig.lookAt({ at.x, at.y, 0.0f });
	h.frames(15);
	h.select(palace);
	h.frames(10);
	const ControlBarButton *button = nullptr;
	for (const ControlBarButton &b : h.hud->controlBar().palantirButtons())
	{
		if (b.button && b.button->m_name == "Command_ConstructMordorHaradrimArcherHorde")
		{
			button = &b;
		}
	}
	REQUIRE(button != nullptr);
	// Command_ConstructMordorHaradrimArcherHorde has Options = NEED_UPGRADE and
	// NeededUpgrade = Upgrade_MordorHaradrimPalaceLevel2; the level 1 palace does not have it.
	// RW_0x942733.md: a missing NeededUpgrade restricts with availability 0 (the _disabled frame), not 4.
	CHECK(button->state == ButtonState::Restricted);
	CHECK(std::string(AptPalantir::commandStateName(button->state, button->button->m_options)) == "_disabled");
}


TEST_CASE("hud6 retail: a NONPRESSABLE passive ability uses the passive ButtonGlass")
{
	if (!haveWorld("hud6 passive button glass"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionIsengard");
	const Coord3D at = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *lurtz = h.rig.make("IsengardLurtz", at.x, at.y);
	REQUIRE(lurtz != nullptr);
	h.rig.lookAt({ at.x, at.y, 0.0f });
	h.frames(30);
	h.select(lurtz);
	h.frames(10);
	const ControlBarButton *passive = nullptr;
	for (const ControlBarButton &b : h.hud->controlBar().palantirButtons())
	{
		if (b.button && b.button->m_name == "Command_LurtzLeadership")
		{
			passive = &b;
		}
	}
	REQUIRE(passive != nullptr);
	REQUIRE(passive->button->hasOption(COMMAND_OPTION_NONPRESSABLE));
	AptSpriteInst *root = h.hud->apt().level(h.hud->palantir()->level());
	REQUIRE(root != nullptr);
	AptCharacterInst *clip = h.hud->apt().resolvePath(root, "CommandButtons.glass" + std::to_string(passive->position));
	REQUIRE(clip != nullptr);
	AptSpriteInst *glass = clip->asSprite();
	REQUIRE(glass != nullptr);
	const int passiveStart = glass->labelFrame("_passive");
	REQUIRE(passiveStart >= 0);
	CHECK(glass->frame >= passiveStart); // the `_passive` segment, the yellow ability glass
	// negative: a normal command set must put the same glass slot back on `_active`, not leave a passive glow behind
	const Coord3D other = h.rig.freeSpot(2800, 1600, 250.0f);
	Object *pit = h.rig.make("IsengardUrukPit", other.x, other.y);
	REQUIRE(pit != nullptr);
	h.select(pit);
	h.frames(10);
	clip = h.hud->apt().resolvePath(root, "CommandButtons.glass" + std::to_string(passive->position));
	REQUIRE(clip != nullptr);
	glass = clip->asSprite();
	REQUIRE(glass != nullptr);
	CHECK(glass->frame < passiveStart); // the `_active` segment
}

TEST_CASE("hud6 retail: a RenderImage without _imageMap finds its image by the stripped path and the tilde base (BFME2 0x623AC4)")
{
	if (!haveWorld("hud6 renderimage path"))
	{
		return;
	}
	Hud6Rig h(shared(), "FactionMordor");
	h.frames(2);
	AptPalantir *p = h.hud->palantir();
	REQUIRE(p != nullptr);
	// 0x812845: `_level<digits><separator>` is removed
	CHECK(AptPalantir::stripLevelPrefix("_level1.CommandUI.Portrait") == "CommandUI.Portrait");
	CHECK(AptPalantir::stripLevelPrefix("_level10/ResourceBar/ResourceIcon/~Enabled") == "ResourceBar/ResourceIcon/~Enabled");
	CHECK(AptPalantir::stripLevelPrefix("CommandUI.Portrait") == "CommandUI.Portrait");
	// 0x623B96: the part before `~`, dropping the separator (retail `/`, the port's dot paths too)
	CHECK(AptPalantir::tildeBase("ResourceBar.ResourceIcon.~Enabled") == "ResourceBar.ResourceIcon");
	CHECK(AptPalantir::tildeBase("ResourceBar/ResourceIcon/~Enabled") == "ResourceBar/ResourceIcon");
	CHECK(AptPalantir::tildeBase("ResourceBar.ResourceIcon") == "ResourceBar.ResourceIcon");
	// the barrel: no `_imageMap`, so the full path misses the dynamic map and the stripped `~` base resolves the image entry
	CHECK(p->imageForClip("_level1.ResourceBar.ResourceIcon.~Enabled", "") == nullptr);
	const AptPalantir::NativeImage *barrel = p->imageForPath("_level1.ResourceBar.ResourceIcon.~Enabled");
	REQUIRE(barrel != nullptr);
	CHECK(barrel->image == "Resource_Icon");
	// an `_imageMap` that names the dynamic entry directly also resolves it
	barrel = p->imageForClip("_level1.ResourceBar.ResourceIcon.~Enabled", "ResourceBar.ResourceIcon");
	REQUIRE(barrel != nullptr);
	CHECK(barrel->image == "Resource_Icon");
	// a path with no entry and no `~` stays unresolved
	CHECK(p->imageForPath("_level1.No.Such.Image") == nullptr);
}
