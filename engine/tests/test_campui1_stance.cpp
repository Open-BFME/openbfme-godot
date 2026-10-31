// OpenBFME retail tests, lane CAMPUI-1: the stance button of the Palantir pops out its three stances (retail RotWK's TOGGLE_STANCE radial). GPL-3.0.
//
// The Witch-king's Command_ToggleStance (command set slot 1, arc window 0): its content is an InGameToggleStanceCommandButton (RW 0x92FF5C: TOGGLE_STANCE
// and the slot's subMenu0 / toggleFlash0 clips reported by PalantirCommandUI::OnSubMenuLoaded / OnToggleFlashLoaded), its press (RW 0x96FAC6) adds one item
// per entry of the button's Stances and opens the sub-menu (subMenu0.Show(3), RW 0x9D32F9); the movie's trays report their frames (<subMenu>_OnButtonFrameLoaded,
// RW 0x9D305F), each gets a CommandButton content "Button" (RW 0x96F7B7 -> 0x9D72DB) with the image of the stance's SET_STANCE button (RW 0x96F6E7); an item's
// press (RW 0x96F85A -> 0x96F53C) sends MSG_CHANGE_STANCE with its stance, asks the close (FadeOut, RW 0x9D2F92) and plays toggleFlash0.Go(); the movie's
// <subMenu>_OnClosed (RW 0x9D3235) closes it. A second press of the toggle closes an open radial; a selection change hides it (RW 0x96FCCB).
// The values asserted come from the retail INI (CommandButton.ini: Command_ToggleStance Stances = HoldGround Battle Aggressive, its TextLabels, the
// Command_SetStance* ButtonImages) and the binary (the stance numbers Battle 1, Aggressive 2, HoldGround 3 of RW 0x96F6E7 / 0xC84178). SKIP without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/INI.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/InGameHelpBox.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/InGameCommandButtonSubMenu.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct StanceRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit StanceRig(SharedWorld &s) : rig(s, "map mp fall back 4p", "FactionAngmar")
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~StanceRig() { hud.reset(); }
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
		rig.lookAt(*o->getPosition());
		frames(5);
		const ICoord2D px = rig.screenOf({ o->getPosition()->x, o->getPosition()->y, 10.0f });
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, rig.timeMs += 40);
		frames(20);
	}
	AptPalantir &palantir() { return *hud->palantir(); }
	std::string level() { return "_level" + std::to_string(hud->palantir()->level()) + "."; }
	const ControlBarButton *toggle()
	{
		for (const ControlBarButton &b : hud->controlBar().palantirButtons())
		{
			if (b.button && b.button->m_name == "Command_ToggleStance")
			{
				return &b;
			}
		}
		return nullptr;
	}
	// the movie's own left press of a content clip (the FSCommand its button sends on a click)
	bool press(const std::string &content)
	{
		const std::string name = content + "_OnPress";
		hud->windows().setPressButton(0);
		return hud->windows().hasCommand(name) && hud->windows().invokeCallback(name, "");
	}
	bool pressToggle(const ControlBarButton &b) { return press(level() + "CommandButtons." + std::to_string(b.position) + ".content"); }
	std::vector<std::string> stanceMessages()
	{
		std::vector<std::string> out;
		for (const std::string &l : hud->input().messageLog())
		{
			if (l.rfind("MSG_CHANGE_STANCE", 0) == 0)
			{
				out.push_back(l);
			}
		}
		return out;
	}
	static int count(const std::vector<std::string> &calls, const std::string &c) { return (int)std::count(calls.begin(), calls.end(), c); }
	// frames until the sub-menu's three items have their contents initialized (the trays' fan-in), at most `limit`
	bool waitItems(const InGameCommandButtonSubMenu &menu, int limit)
	{
		for (int i = 0; i < limit; ++i)
		{
			bool all = menu.itemCount() == 3;
			for (int k = 0; all && k < 3; ++k)
			{
				all = menu.item(k).content && menu.item(k).initialized && !menu.item(k).sig.empty();
			}
			if (all)
			{
				return true;
			}
			frames(1);
		}
		return false;
	}
	bool waitClosed(const InGameCommandButtonSubMenu &menu, int limit)
	{
		for (int i = 0; i < limit && menu.isOpen(); ++i)
		{
			frames(1);
		}
		return !menu.isOpen();
	}
};

// RW 0x96F6E7's table and RW 0xC84178's stance numbers (StancesBehavior's StanceType: Battle 1, Aggressive 2, HoldGround 3)
const std::string kHoldGroundImage = "UCCommon_HoldGroundStance"; // CommandButton.ini Command_SetStanceHoldGround ButtonImage
const std::string kBattleImage = "UCCommon_BattleStance";         // Command_SetStanceBattle
const std::string kAggressiveImage = "UCCommon_AggresiveStance";  // Command_SetStanceAggressive (retail's spelling)
} // namespace

TEST_CASE("campui1 stance retail: the Witch-king's stance button pops out HoldGround / Battle / Aggressive, an item sends its MSG_CHANGE_STANCE (HoldGround 3, Battle 1) and the radial fades out")
{
	if (!haveWorld("campui1 stance"))
	{
		return;
	}
	SharedWorld &s = shared();
	StanceRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *wk = h.rig.make("AngmarWitchking", c.x, c.y);
	h.frames(10);
	h.select(wk);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ wk->getID() });
	StancesBehavior *sb = StancesBehavior::of(*wk);
	REQUIRE(sb);

	const ControlBarButton *toggle = h.toggle();
	REQUIRE(toggle);
	REQUIRE(toggle->button->m_command == GUI_COMMAND_TOGGLE_STANCE);
	CHECK(toggle->slot == 0);     // AngmarWitchKingCommandSet 1 = Command_ToggleStance
	CHECK(toggle->position == 0); // arc window 0
	CHECK(toggle->button->m_stances == std::vector<int>{ STANCE_HOLD_GROUND, STANCE_BATTLE, STANCE_AGGRESSIVE });
	AptPalantir &pal = h.palantir();
	const InGameCommandButtonSubMenu *menu = pal.subMenu(0);
	REQUIRE(menu);
	CHECK(menu->path() == "CommandButtons.subMenu0"); // palantir.apt sprite 114 places subMenu0 .. 3 and toggleFlash0 .. 3 in the arc
	CHECK(pal.toggleFlashPath(0) == "CommandButtons.toggleFlash0");
	CHECK(pal.isToggleStanceContent(0));
	CHECK_FALSE(menu->isOpen());
	for (int i = 1; i < 4; ++i)
	{
		REQUIRE(pal.subMenu(i)); // the arc's sub-menus 0 .. 3 (positions 4 and 5 have none)
	}
	CHECK_FALSE(pal.subMenu(4));
	CHECK_FALSE(pal.subMenu(5));

	// the press: one item per stance of the list, Show(3); nothing sent
	REQUIRE(h.pressToggle(*toggle));
	CHECK(menu->isOpen());
	REQUIRE(menu->itemCount() == 3);
	CHECK(menu->calls() == std::vector<std::string>{ "Show(3)" });
	CHECK(menu->item(0).value == STANCE_HOLD_GROUND);
	CHECK(menu->item(1).value == STANCE_BATTLE);
	CHECK(menu->item(2).value == STANCE_AGGRESSIVE);
	REQUIRE(h.waitItems(*menu, 90));
	const std::string images[3] = { kHoldGroundImage, kBattleImage, kAggressiveImage };
	const char *setStance[3] = { "Command_SetStanceHoldGround", "Command_SetStanceBattle", "Command_SetStanceAggressive" };
	const char *labels[3] = { "CONTROLBAR:ToggleStanceHoldGround", "CONTROLBAR:ToggleStanceBattle", "CONTROLBAR:ToggleStanceAggressive" };
	for (int k = 0; k < 3; ++k)
	{
		const InGameCommandButtonSubMenu::Item &it = menu->item(k);
		CAPTURE(k);
		CHECK(it.frame.rfind("CommandButtons.subMenu0.", 0) == 0); // the tray's frame inside the sub-menu clip
		const std::string content = h.level() + it.frame + ".Button";
		const AptPalantir::NativeImage *img = pal.imageFor(content + "_Image");
		REQUIRE(img);
		CHECK(img->image == images[k]);
		CHECK_FALSE(img->grayscale);
		CHECK(AptPalantir::setStanceButtonName(it.value) == std::string(setStance[k]));
		// the item's help: the toggle's TextLabel / DescriptLabel at the stance's index (RW 0x96F33E)
		AptPalantir::StanceItemHit hit;
		REQUIRE(pal.stanceItemAt(content + ".bttn", hit));
		CHECK(hit.slot == toggle->slot);
		CHECK(hit.stance == it.value);
		CHECK(hit.listIndex == k);
		REQUIRE(hit.toggle == toggle->button);
		CHECK(hit.toggle->m_textLabel[(size_t)k] == labels[k]);
	}
	CHECK(h.stanceMessages().empty());
	CHECK(pal.stanceErrors().empty());
	for (const std::string &e : pal.callErrors())
	{
		CHECK_MESSAGE(e.find("subMenu") == std::string::npos, e);
		CHECK_MESSAGE(e.find("toggleFlash") == std::string::npos, e);
	}

	// the HoldGround item: MSG_CHANGE_STANCE 3, FadeOut, toggleFlash0.Go(); the movie's _OnClosed closes it
	REQUIRE(h.press(h.level() + menu->item(0).frame + ".Button"));
	CHECK(pal.toggleFlashGoes() == 1u);
	CHECK(menu->closeRequested());
	h.frames(3);
	CHECK(h.stanceMessages() == std::vector<std::string>{ "MSG_CHANGE_STANCE i3" });
	CHECK(StanceRig::count(menu->calls(), "FadeOut()") == 1);
	REQUIRE(h.waitClosed(*menu, 90));
	CHECK(menu->itemCount() == 0);
	CHECK_FALSE(menu->fading());
	CHECK(StanceRig::count(menu->calls(), "Hide()") == 0); // the movie closes itself after the fade
	h.frames(5);
	CHECK(sb->getStance() == STANCE_HOLD_GROUND);
	toggle = h.toggle();
	REQUIRE(toggle);
	CHECK(toggle->image == kHoldGroundImage); // the toggle follows the stance (RW 0x96F5DE): Command_ToggleStance ButtonImage 1

	// open again and pick Battle: MSG_CHANGE_STANCE 1
	REQUIRE(h.pressToggle(*toggle));
	REQUIRE(h.waitItems(*menu, 90));
	CHECK(menu->item(1).value == STANCE_BATTLE);
	REQUIRE(h.press(h.level() + menu->item(1).frame + ".Button"));
	h.frames(3);
	CHECK(h.stanceMessages() == std::vector<std::string>{ "MSG_CHANGE_STANCE i3", "MSG_CHANGE_STANCE i1" });
	CHECK(pal.toggleFlashGoes() == 2u);
	CHECK(menu->calls() == std::vector<std::string>{ "Show(3)", "FadeOut()", "Show(3)", "FadeOut()" });
	REQUIRE(h.waitClosed(*menu, 90));
	h.frames(5);
	CHECK(sb->getStance() == STANCE_BATTLE);
	toggle = h.toggle();
	REQUIRE(toggle);
	CHECK(toggle->image == kBattleImage); // Command_ToggleStance ButtonImage 2
	CHECK(h.hud->controlBar().unportedPresses().count("TOGGLE_STANCE (no sub-menu)") == 0u);
	CHECK(pal.stanceErrors().empty());
}

TEST_CASE("campui1 stance retail: the toggle shows the stance's class (RW 0x96FF2F -> 0x861D8E): Uninitialized as Battle, HoldGroundMoving as HoldGround")
{
	if (!haveWorld("campui1 stance"))
	{
		return;
	}
	SharedWorld &s = shared();
	StanceRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *wk = h.rig.make("AngmarWitchking", c.x, c.y);
	h.frames(10);
	h.select(wk);
	StancesBehavior *sb = StancesBehavior::of(*wk);
	REQUIRE(sb);
	const ControlBarButton *toggle = h.toggle();
	REQUIRE(toggle);
	const CommandButton &button = *toggle->button;
	REQUIRE(button.m_stances == std::vector<int>{ STANCE_HOLD_GROUND, STANCE_BATTLE, STANCE_AGGRESSIVE });
	REQUIRE(sb->getStance() == STANCE_BATTLE);
	CHECK(ControlBar::stanceSlotOf(button, *wk) == 1);
	REQUIRE(sb->setStance(STANCE_HOLD_GROUND));
	sb->onMoveStart(); // RW 0x862275: HoldGround -> HoldGroundMoving, class 3
	REQUIRE(sb->getStance() == STANCE_HOLD_GROUND_MOVING);
	CHECK(ControlBar::stanceSlotOf(button, *wk) == 0);
	// a fresh object has not run its first update (RW 0x8622E9) yet: Uninitialized is class 1, the Battle image, not the list's first (HoldGround)
	const Coord3D c2 = h.rig.freeSpot(c.x + 300.0f, c.y, 200.0f);
	Object *fresh = h.rig.make("AngmarWitchking", c2.x, c2.y);
	StancesBehavior *freshStance = StancesBehavior::of(*fresh);
	REQUIRE(freshStance);
	REQUIRE(freshStance->getStance() == STANCE_UNINITIALIZED);
	CHECK(ControlBar::stanceSlotOf(button, *fresh) == 1);
}

TEST_CASE("campui1 stance retail: a second press of the stance button fades the radial out without a stance; a selection change hides it")
{
	if (!haveWorld("campui1 stance"))
	{
		return;
	}
	SharedWorld &s = shared();
	StanceRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *wk = h.rig.make("AngmarWitchking", c.x, c.y);
	h.frames(10);
	h.select(wk);
	const ControlBarButton *toggle = h.toggle();
	REQUIRE(toggle);
	AptPalantir &pal = h.palantir();
	const InGameCommandButtonSubMenu *menu = pal.subMenu(toggle->position);
	REQUIRE(menu);

	// open, then the second press: the close request, FadeOut at the next update, _OnClosed from the movie
	REQUIRE(h.pressToggle(*toggle));
	REQUIRE(h.waitItems(*menu, 90));
	REQUIRE(h.pressToggle(*toggle));
	CHECK(menu->isOpen());
	CHECK(menu->closeRequested());
	h.frames(1);
	CHECK(menu->fading());
	CHECK(menu->calls() == std::vector<std::string>{ "Show(3)", "FadeOut()" });
	REQUIRE(h.waitClosed(*menu, 90));
	CHECK(menu->itemCount() == 0);
	CHECK(h.stanceMessages().empty());
	CHECK(pal.toggleFlashGoes() == 0u);

	// open again, then the selection goes: the content's destructor hides the radial (RW 0x96FCCB)
	REQUIRE(h.pressToggle(*toggle));
	REQUIRE(h.waitItems(*menu, 90));
	h.hud->input().ui().deselectAll();
	h.frames(3);
	CHECK_FALSE(menu->isOpen());
	CHECK(menu->itemCount() == 0);
	CHECK(menu->calls() == std::vector<std::string>{ "Show(3)", "FadeOut()", "Show(3)", "Hide()" });
	CHECK(h.stanceMessages().empty());
	CHECK(pal.stanceErrors().empty());
}

TEST_CASE("campui1 stance retail: hovering a radial item shows the toggle's TextLabel / DescriptLabel of that stance in the help box (RW 0x96F33E)")
{
	if (!haveWorld("campui1 stance"))
	{
		return;
	}
	SharedWorld &s = shared();
	// the string file and the mapped images, as the help box needs them (test_hover1.cpp's HoverRig)
	Rig rig(s, "map mp fall back 4p", "FactionAngmar");
	RecordingShellServices services;
	GameTextTableSource text;
	MappedImageCollection mapped;
	INIEnvironment imageEnv;
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &error), error);
	REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
	imageEnv.fileSystem = s.mount->fs.get();
	mapped.registerBlocks(imageEnv.blocks);
	INI ini(imageEnv);
	ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
	InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
	auto hud = std::make_unique<InGameHud>(cfg);
	hud->setMappedImages(&mapped);
	REQUIRE_MESSAGE(hud->boot(&error), error);
	hud->setWindowSize(1024, 768);
	rig.view.setScreen(1024, 768);
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	};
	const Coord3D c = rig.freeSpot(2800, 1400, 200.0f);
	Object *wk = rig.make("AngmarWitchking", c.x, c.y);
	frames(10);
	hud->input().ui().deselectAll(true);
	hud->input().ui().selectObject(wk->getID());
	frames(20);
	const ControlBarButton *toggle = nullptr;
	for (const ControlBarButton &b : hud->controlBar().palantirButtons())
	{
		toggle = b.button && b.button->m_command == GUI_COMMAND_TOGGLE_STANCE ? &b : toggle;
	}
	REQUIRE(toggle);
	const CommandButton *button = toggle->button;
	const std::string open = "_level" + std::to_string(hud->palantir()->level()) + ".CommandButtons." + std::to_string(toggle->position) + ".content_OnPress";
	hud->windows().setPressButton(0);
	REQUIRE(hud->windows().invokeCallback(open, ""));
	frames(40);
	const InGameCommandButtonSubMenu *menu = hud->palantir()->subMenu(toggle->position);
	REQUIRE(menu);
	REQUIRE(menu->isOpen());
	// a window point over each item's content (the Apt input's topmost button under it lies inside the item)
	AptStageMapping m;
	m.windowW = 1024.0f;
	m.windowH = 768.0f;
	ICoord2D at[3] = {};
	bool found[3] = {};
	for (int y = 2; y < 768; y += 3)
	{
		for (int x = 2; x < 1024; x += 3)
		{
			float sx, sy;
			m.windowToStage((float)x, (float)y, sx, sy);
			AptButtonInst *b = hud->apt().input().hitTestButtons(sx, sy);
			AptPalantir::StanceItemHit hit;
			if (b && hud->palantir()->stanceItemAt(b->targetPath(), hit) && hit.listIndex >= 0 && hit.listIndex < 3 && !found[hit.listIndex])
			{
				found[hit.listIndex] = true;
				at[hit.listIndex] = ICoord2D{ x, y };
			}
		}
	}
	for (int k = 0; k < 3; ++k)
	{
		CAPTURE(k);
		REQUIRE(found[k]);
		hud->mouseMove(2, 2, 0);
		for (int i = 0; i < 12 && hud->helpBox().state() != 0; ++i)
		{
			hud->update(0.033);
		}
		hud->mouseMove(at[k].x, at[k].y, 0);
		const CommandButtonHelp *help = nullptr;
		for (int i = 0; i < 12 && !help; ++i)
		{
			hud->update(0.033);
			std::vector<HelpDrawOp> ops;
			hud->helpBoxRender(400.0f, 500.0f, 360.0f, 120.0f, ops);
			help = hud->helpBox().isShown() ? hud->helpBox().help() : nullptr;
		}
		REQUIRE(help);
		CHECK(hud->helpBox().provider().bar == 5);
		CHECK(help->name() == fetchOrMissing(&text, button->m_textLabel[(size_t)k]));         // CONTROLBAR:ToggleStance<Name>
		CHECK(help->longText() == fetchOrMissing(&text, button->m_descriptLabel[(size_t)k])); // CONTROLBAR:ToolTipToggleStance<Name> ("Shortcut: ...": the box's long text)
		CHECK(help->title().empty()); // no cost / command points (RW 0x975070 with empty texts)
		CHECK(help->description().empty());
		bool exists = false;
		fetchOrMissing(&text, button->m_descriptLabel[(size_t)k], &exists);
		CHECK(exists);
	}
	CHECK(menu->isOpen()); // hovering does not close it
	hud.reset();
}

TEST_CASE("campui1 stance: the sub-menu protocol (RW 0x9D3380 .. 0x9D3550) without a movie")
{
	std::vector<std::string> calls;
	InGameCommandButtonSubMenu menu(5, "CommandButtons.subMenu1", [&calls](const std::string &path, const std::string &fn, const std::vector<std::string> &args) {
		calls.push_back(path + "." + fn + "(" + (args.empty() ? std::string() : args.front()) + ")");
		return true;
	});
	CHECK(menu.commandName(InGameCommandButtonSubMenu::kFrameLoaded) == "_level5.CommandButtons.subMenu1_OnButtonFrameLoaded");
	CHECK(menu.commandName(InGameCommandButtonSubMenu::kFrameUnloaded) == "_level5.CommandButtons.subMenu1_OnButtonFrameUnloaded");
	CHECK(menu.commandName(InGameCommandButtonSubMenu::kClosed) == "_level5.CommandButtons.subMenu1_OnClosed");
	CHECK(menu.addItem(3));
	CHECK(menu.addItem(1));
	CHECK(menu.addItem(2));
	CHECK_FALSE(menu.addItem(1)); // three records
	CHECK(menu.open());
	CHECK(calls == std::vector<std::string>{ "CommandButtons.subMenu1.Show(3)" });
	// RW 0x9D305F: an index past the count, or a record that has its frame, is ignored; the level prefix goes
	menu.onButtonFrameLoaded("index=3&name=_level5.X");
	menu.onButtonFrameLoaded("index=1&name=_level5.CommandButtons.subMenu1.trays.1.rot.bttnFrame");
	menu.onButtonFrameLoaded("index=1&name=_level5.Other");
	CHECK(menu.item(1).frame == "CommandButtons.subMenu1.trays.1.rot.bttnFrame");
	CHECK(menu.item(0).frame.empty());
	int made = 0;
	std::vector<int> dropped;
	menu.setDropItemContent([&dropped](const InGameCommandButtonSubMenu::Item &it) { dropped.push_back(it.value); });
	menu.update(
		[&made](int) {
			++made;
			return true;
		},
		nullptr);
	CHECK(made == 1);
	CHECK(menu.item(1).content);
	// _OnClosed before a fade: nothing (RW 0x9D3235 tests + 0x44)
	menu.onClosed();
	CHECK(menu.isOpen());
	menu.requestClose();
	menu.requestClose();
	menu.update(nullptr, nullptr);
	menu.requestClose();
	menu.update(nullptr, nullptr); // fading: no second FadeOut
	CHECK(calls == std::vector<std::string>{ "CommandButtons.subMenu1.Show(3)", "CommandButtons.subMenu1.FadeOut()" });
	menu.onClosed();
	CHECK_FALSE(menu.isOpen());
	CHECK_FALSE(menu.fading());
	CHECK(menu.itemCount() == 0);
	CHECK(dropped == std::vector<int>{ 1 }); // only the item whose content was made
	menu.hide();                             // closed: no call
	CHECK(calls.size() == 2u);
	CHECK(menu.addItem(1));
	CHECK(menu.open());
	menu.hide();
	CHECK(calls.back() == "CommandButtons.subMenu1.Hide()");
	CHECK_FALSE(menu.isOpen());
}
