// OpenBFME unit tests: LOCAL-12 command button border: CommandButton/ButtonBorderType (lane LOCAL-12).
//
// RotWK addresses:
//   ButtonBorderType lookup table: RW 0xC2BA90 (6 rows: NONE=0, BUILD=1, UPGRADE=2, ACTION=3, SYSTEM=4, ALTERED=5)
//   CommandButton field parse row: RW 0xC2BC48 (ButtonBorderType, INI::parseLookupList RW 0x0042EE0C, offset +0xB0)
//   Getter: RW 0x75CBA5 (checks byte ptr [ecx + 0x103] m_altered; if set returns 5 [ALTERED], else [ecx + 0xB0])
//   Constructor: RW 0x75D662 (m_altered at +0x103 initialized to 0)
//   ControlBar::setControlCommand: RW 0x71CF3E (at RW 0x71D09F calls getter RW 0x75CBA5, passes to RW 0x71C4F4)
//   ControlBar::setCommandBarBorder: RW 0x71C4F4 (maps 1..5 to colors +0x220..+0x230 and draw=1, 0/default to color=0x00FFFFFF draw=0)
//   GadgetButtonSetBorder: RW 0x729EA7 (stores draw flag at [eax + 0xC] and color at [eax + 0x10] in PushButtonData)
//   ControlBarScheme field table: RW 0xC23400 (ScreenCreationRes, Side, BuildUpClockColor, ButtonBorder*Color, CommandBarBorderColor)
//   ControlBarScheme ctor: RW 0x721C2D (defaults all 5 border colors to 0x00FFFFFF)
//   ControlBarSchemeStore::loadFromFiles: RW 0x721BA1 (loads Default\ControlBarScheme.ini and ControlBarScheme.ini)
//   ControlBarSchemeStore::findScheme: RW 0x7215B1 (case-insensitive name lookup)
//   ControlBarSchemeStore::findSchemeBySide: RW 0x7216B6 (side lookup, falls back to "Default")
//   ControlBar::setCommandBarBorderColors: RW 0x71C62F (stores 5 border colors at +0x220..+0x230)
//   ControlBar::setScheme: RW 0x7206D5 (passes colors to RW 0x71C62F)
//   W3DGadgetPushButtonImageDrawOne: RW 0x4A579C (tests bit 26 WIN_STATUS_CIRCULAR: if radial, SKIPS drawing;
//     else if drawBorder && color != 0x00FFFFFF, calls Display::drawRect with (x - 1, y - 1, w + 2, h + 2, 1.0f, color))
//   populateOCLTimer: RW 0x944D9D (ControlBar.wnd:OCLTimerSellButton, Command_Sell, border SYSTEM, non-radial)

#include "doctest.h"
#include "IniTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <memory>
#include <string>

namespace
{

std::unique_ptr<GameWindow> makePushButton(GameWindowManager &mgr, int x, int y, int w, int h, const std::string &name = "")
{
	(void)name;
	auto win = std::make_unique<GameWindow>(mgr);
	win->winSetPosition(x, y);
	win->winSetSize(w, h);
	win->winSetSystemFunc(GadgetPushButtonSystem);
	win->winSetDrawFunc(W3DGadgetPushButtonImageDraw);
	win->winSetEnabledImage(0, "TestImage");
	BitSet(win->winGetInstanceData()->m_status, WIN_STATUS_ENABLED);
	return win;
}

} // namespace

TEST_CASE("LOCAL-12: ButtonBorderType enum, member and altered override (RW 0x75CBA5)")
{
	// RW 0xC2BA90 values: NONE=0, BUILD=1, UPGRADE=2, ACTION=3, SYSTEM=4, ALTERED=5
	CHECK(BUTTON_BORDER_NONE == 0);
	CHECK(BUTTON_BORDER_BUILD == 1);
	CHECK(BUTTON_BORDER_UPGRADE == 2);
	CHECK(BUTTON_BORDER_ACTION == 3);
	CHECK(BUTTON_BORDER_SYSTEM == 4);
	CHECK(BUTTON_BORDER_ALTERED == 5);

	CommandButton btn;
	// Ctor defaults (RW 0x75D662): border is 0 (NONE), m_altered is false
	CHECK(btn.m_commandButtonBorder == BUTTON_BORDER_NONE);
	CHECK_FALSE(btn.isAltered());
	CHECK(btn.getButtonBorderType() == BUTTON_BORDER_NONE);

	// Normal border setting
	btn.m_commandButtonBorder = BUTTON_BORDER_BUILD;
	CHECK(btn.getButtonBorderType() == BUTTON_BORDER_BUILD);
	CHECK(btn.commandButtonBorder() == BUTTON_BORDER_BUILD);

	// Runtime altered state overrides m_commandButtonBorder (RW 0x75CBA5)
	btn.setAltered(true);
	CHECK(btn.isAltered());
	CHECK(btn.getButtonBorderType() == BUTTON_BORDER_ALTERED);
	// Underlying INI-parsed border remains unchanged
	CHECK(btn.commandButtonBorder() == BUTTON_BORDER_BUILD);

	// Resetting altered restores normal border
	btn.setAltered(false);
	CHECK_FALSE(btn.isAltered());
	CHECK(btn.getButtonBorderType() == BUTTON_BORDER_BUILD);
}

TEST_CASE("LOCAL-12: ButtonBorderType INI parsing via CommandButton block (RW 0xC2BC48)")
{
	const std::string iniText =
		"CommandButton Test_None\n"
		"  Command = STOP\n"
		"  ButtonBorderType = NONE\n"
		"End\n"
		"CommandButton Test_Build\n"
		"  Command = UNIT_BUILD\n"
		"  ButtonBorderType = BUILD\n"
		"End\n"
		"CommandButton Test_Upgrade\n"
		"  Command = OBJECT_UPGRADE\n"
		"  ButtonBorderType = UPGRADE\n"
		"End\n"
		"CommandButton Test_Action\n"
		"  Command = ATTACK_MOVE\n"
		"  ButtonBorderType = ACTION\n"
		"End\n"
		"CommandButton Test_System\n"
		"  Command = SELL\n"
		"  ButtonBorderType = SYSTEM\n"
		"End\n"
		"CommandButton Test_Altered\n"
		"  Command = STOP\n"
		"  ButtonBorderType = ALTERED\n"
		"End\n";

	INIEnvironment env;
	CommandStore store;
	TheCommandStore = &store;
	env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });

	INI ini(env);
	ini.loadMemory("buttons.ini", initest::toBytes(iniText), INI_LOAD_OVERWRITE);

	const CommandButton *bNone = store.findCommandButton("Test_None");
	REQUIRE(bNone);
	CHECK(bNone->getButtonBorderType() == BUTTON_BORDER_NONE);

	const CommandButton *bBuild = store.findCommandButton("Test_Build");
	REQUIRE(bBuild);
	CHECK(bBuild->getButtonBorderType() == BUTTON_BORDER_BUILD);

	const CommandButton *bUpgrade = store.findCommandButton("Test_Upgrade");
	REQUIRE(bUpgrade);
	CHECK(bUpgrade->getButtonBorderType() == BUTTON_BORDER_UPGRADE);

	const CommandButton *bAction = store.findCommandButton("Test_Action");
	REQUIRE(bAction);
	CHECK(bAction->getButtonBorderType() == BUTTON_BORDER_ACTION);

	const CommandButton *bSystem = store.findCommandButton("Test_System");
	REQUIRE(bSystem);
	CHECK(bSystem->getButtonBorderType() == BUTTON_BORDER_SYSTEM);

	const CommandButton *bAltered = store.findCommandButton("Test_Altered");
	REQUIRE(bAltered);
	CHECK(bAltered->getButtonBorderType() == BUTTON_BORDER_ALTERED);

	TheCommandStore = nullptr;
}

TEST_CASE("LOCAL-12: ControlBarScheme defaults and INI parsing (RW 0xC23400 / 0x721C2D)")
{
	// Default ctor (RW 0x721C2D): all border colours default to 0x00FFFFFF (WIN_COLOR_UNDEFINED)
	ControlBarScheme def;
	CHECK(def.m_buildUpClockColor == 0x00FFFFFFu);
	CHECK(def.m_buttonBorderBuildColor == 0x00FFFFFFu);
	CHECK(def.m_buttonBorderActionColor == 0x00FFFFFFu);
	CHECK(def.m_buttonBorderUpgradeColor == 0x00FFFFFFu);
	CHECK(def.m_buttonBorderSystemColor == 0x00FFFFFFu);
	CHECK(def.m_buttonBorderAlteredColor == 0x00FFFFFFu);
	CHECK(def.m_commandBarBorderColor == 0x00FFFFFFu);

	const std::string iniText =
		"ControlBarScheme Mordor8x6\n"
		"  ScreenCreationRes X:1024 Y:768\n"
		"  Side Mordor\n"
		"  CommandBarBorderColor R:0 G:21 B:126 A:255\n"
		"  BuildUpClockColor R:0 G:0 B:0 A:192\n"
		"  ButtonBorderBuildColor R:67 G:108 B:190 A:255\n"
		"  ButtonBorderActionColor R:1 G:175 B:2 A:255\n"
		"  ButtonBorderUpgradeColor R:208 G:108 B:0 A:255\n"
		"  ButtonBorderSystemColor R:207 G:195 B:2 A:255\n"
		"  ButtonBorderAlteredColor R:255 G:255 B:255 A:255\n"
		"  ImagePart\n"
		"    Position X:0 Y:520\n"
		"    Size X:1024 Y:248\n"
		"    ImageName SMCommandBar\n"
		"    Layer 4\n"
		"  End\n"
		"End\n";

	INIEnvironment env;
	ControlBarSchemeStore schemeStore;
	TheControlBarSchemeStore = &schemeStore;
	schemeStore.registerBlock(env.blocks);

	INI ini(env);
	ini.loadMemory("scheme.ini", initest::toBytes(iniText), INI_LOAD_OVERWRITE);

	const ControlBarScheme *mordor = schemeStore.findScheme("Mordor8x6");
	REQUIRE(mordor);
	CHECK(mordor->m_name == "Mordor8x6");
	CHECK(mordor->m_side == "Mordor");
	CHECK(mordor->m_screenCreationRes.x == 1024);
	CHECK(mordor->m_screenCreationRes.y == 768);

	// Verify exact GameMakeColor ARGB values: (alpha<<24) | (red<<16) | (green<<8) | blue
	CHECK(mordor->m_commandBarBorderColor == GameMakeColor(0, 21, 126, 255));
	CHECK(mordor->m_buildUpClockColor == GameMakeColor(0, 0, 0, 192));
	CHECK(mordor->m_buttonBorderBuildColor == GameMakeColor(67, 108, 190, 255));
	CHECK(mordor->m_buttonBorderActionColor == GameMakeColor(1, 175, 2, 255));
	CHECK(mordor->m_buttonBorderUpgradeColor == GameMakeColor(208, 108, 0, 255));
	CHECK(mordor->m_buttonBorderSystemColor == GameMakeColor(207, 195, 2, 255));
	CHECK(mordor->m_buttonBorderAlteredColor == GameMakeColor(255, 255, 255, 255));

	TheControlBarSchemeStore = nullptr;
}

TEST_CASE("LOCAL-12: ControlBarSchemeStore lookup and side fallback (RW 0x7215B1 / 0x7216B6)")
{
	const std::string iniText =
		"ControlBarScheme Default\n"
		"  Side Observer\n"
		"  ButtonBorderBuildColor R:10 G:20 B:30 A:255\n"
		"End\n"
		"ControlBarScheme Mordor8x6\n"
		"  Side Mordor\n"
		"  ButtonBorderBuildColor R:67 G:108 B:190 A:255\n"
		"End\n";

	INIEnvironment env;
	ControlBarSchemeStore schemeStore;
	TheControlBarSchemeStore = &schemeStore;
	schemeStore.registerBlock(env.blocks);
	INI ini(env);
	ini.loadMemory("scheme.ini", initest::toBytes(iniText), INI_LOAD_OVERWRITE);

	// Case-insensitive name lookup (RW 0x7215B1)
	const ControlBarScheme *byName = schemeStore.findScheme("mordor8x6");
	REQUIRE(byName);
	CHECK(byName->m_name == "Mordor8x6");

	// Side lookup (RW 0x7216B6)
	const ControlBarScheme *bySide = schemeStore.findSchemeBySide("mordor");
	REQUIRE(bySide);
	CHECK(bySide->m_name == "Mordor8x6");

	// Unknown side falls back to "Default" (RW 0x7216B6 / 0x7217C3)
	const ControlBarScheme *fallback = schemeStore.findSchemeBySide("Gondor");
	REQUIRE(fallback);
	CHECK(fallback->m_name == "Default");
	CHECK(fallback->m_side == "Observer");

	TheControlBarSchemeStore = nullptr;
}

TEST_CASE("LOCAL-12: ControlBar setScheme, borderColorForType and setCommandBarBorder (RW 0x71C62F / 0x7206D5 / 0x71C4F4)")
{
	ControlBarScheme scheme;
	scheme.m_name = "TestScheme";
	scheme.m_buttonBorderBuildColor = 0xFF112233;
	scheme.m_buttonBorderActionColor = 0xFF445566;
	scheme.m_buttonBorderUpgradeColor = 0xFF778899;
	scheme.m_buttonBorderSystemColor = 0xFFAABBCC;
	scheme.m_buttonBorderAlteredColor = 0xFFDDEEFF;
	scheme.m_commandBarBorderColor = 0xFF123456;

	prodtest::ProdWorld f;
	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{ *f.logic, nullptr, view, ui, stream, &f.commands, MouseSettings{} };
	ControlBar bar(ctx);

	bar.setScheme(&scheme);
	CHECK(bar.schemeName() == "TestScheme");
	CHECK(bar.borderBuildColor() == 0xFF112233u);
	CHECK(bar.borderActionColor() == 0xFF445566u);
	CHECK(bar.borderUpgradeColor() == 0xFF778899u);
	CHECK(bar.borderSystemColor() == 0xFFAABBCCu);
	CHECK(bar.borderAlteredColor() == 0xFFDDEEFFu);
	CHECK(bar.commandBarBorderColor() == 0xFF123456u);

	// Test borderColorForType mapping (RW 0x71C4F4)
	bool draw = false;
	CHECK(bar.borderColorForType(BUTTON_BORDER_BUILD, &draw) == 0xFF112233u);
	CHECK(draw);

	CHECK(bar.borderColorForType(BUTTON_BORDER_UPGRADE, &draw) == 0xFF778899u);
	CHECK(draw);

	CHECK(bar.borderColorForType(BUTTON_BORDER_ACTION, &draw) == 0xFF445566u);
	CHECK(draw);

	CHECK(bar.borderColorForType(BUTTON_BORDER_SYSTEM, &draw) == 0xFFAABBCCu);
	CHECK(draw);

	CHECK(bar.borderColorForType(BUTTON_BORDER_ALTERED, &draw) == 0xFFDDEEFFu);
	CHECK(draw);

	// NONE: draw=false, color=0x00FFFFFF
	CHECK(bar.borderColorForType(BUTTON_BORDER_NONE, &draw) == 0x00FFFFFFu);
	CHECK_FALSE(draw);

	// Out of range: draw=false, color=0x00FFFFFF
	CHECK(bar.borderColorForType(99, &draw) == 0x00FFFFFFu);
	CHECK_FALSE(draw);

	// Test setCommandBarBorder on window (RW 0x71C4F4 -> GadgetButtonSetBorder RW 0x729EA7)
	GameWindowManager mgr;
	auto win = makePushButton(mgr, 10, 20, 64, 64, "TestButton");
	bar.setCommandBarBorder(win.get(), BUTTON_BORDER_SYSTEM);

	auto *data = static_cast<PushButtonData *>(win->winGetUserData());
	REQUIRE(data);
	CHECK(data->drawBorder == true);
	CHECK(data->borderColor == 0xFFAABBCCu);

	bar.setCommandBarBorder(win.get(), BUTTON_BORDER_NONE);
	CHECK(data->drawBorder == false);
	CHECK(data->borderColor == 0x00FFFFFFu);
}

TEST_CASE("LOCAL-12: Radial windows never draw border, non-radial do (RW 0x4A579C / 0x729EA7)")
{
	GameWindowManager mgr;
	GadgetDrawList drawList;
	mgr.setDrawList(&drawList);

	auto win = makePushButton(mgr, 100, 200, 50, 50, "CommandButton_NonRadial");

	// Step 1: PushButton with GadgetButtonSetBorder on non-radial window
	GadgetButtonSetBorder(win.get(), 0xFFFF0000u, true);

	drawList.commands.clear();
	W3DGadgetPushButtonImageDraw(win.get(), win->winGetInstanceData());

	// Should have emitted OpenRect outline around non-radial window: (x-1, y-1) to (x+w+1, y+h+1)
	bool foundOpenRect = false;
	for (const auto &cmd : drawList.commands)
	{
		if (cmd.kind == GadgetDrawCommand::Kind::OpenRect)
		{
			foundOpenRect = true;
			CHECK(cmd.color == 0xFFFF0000u);
			CHECK(cmd.lineWidth == 1);
			CHECK(cmd.x0 == 100 - 1);
			CHECK(cmd.y0 == 200 - 1);
			CHECK(cmd.x1 == 100 + 50 + 1);
			CHECK(cmd.y1 == 200 + 50 + 1);
		}
	}
	CHECK(foundOpenRect);

	// Step 2: PushButton on radial window (WIN_STATUS_CIRCULAR) NEVER draws border (RW 0x4A579C)
	win->winSetStatus(WIN_STATUS_CIRCULAR);
	BitSet(win->winGetInstanceData()->m_status, WIN_STATUS_CIRCULAR);
	drawList.commands.clear();
	W3DGadgetPushButtonImageDraw(win.get(), win->winGetInstanceData());

	foundOpenRect = false;
	for (const auto &cmd : drawList.commands)
	{
		if (cmd.kind == GadgetDrawCommand::Kind::OpenRect)
		{
			foundOpenRect = true;
		}
	}
	CHECK_FALSE(foundOpenRect);

	// Step 3: Non-radial window with draw=false does NOT draw border
	win->winClearStatus(WIN_STATUS_CIRCULAR);
	BitClear(win->winGetInstanceData()->m_status, WIN_STATUS_CIRCULAR);
	GadgetButtonSetBorder(win.get(), 0xFFFF0000u, false);
	drawList.commands.clear();
	W3DGadgetPushButtonImageDraw(win.get(), win->winGetInstanceData());

	foundOpenRect = false;
	for (const auto &cmd : drawList.commands)
	{
		if (cmd.kind == GadgetDrawCommand::Kind::OpenRect)
		{
			foundOpenRect = true;
		}
	}
	CHECK_FALSE(foundOpenRect);

	// Step 4: Non-radial window with color=0x00FFFFFF does NOT draw border
	GadgetButtonSetBorder(win.get(), 0x00FFFFFFu, true);
	drawList.commands.clear();
	W3DGadgetPushButtonImageDraw(win.get(), win->winGetInstanceData());

	foundOpenRect = false;
	for (const auto &cmd : drawList.commands)
	{
		if (cmd.kind == GadgetDrawCommand::Kind::OpenRect)
		{
			foundOpenRect = true;
		}
	}
	CHECK_FALSE(foundOpenRect);

	mgr.setDrawList(nullptr);
}

TEST_CASE("LOCAL-12: ControlBar button evaluation sets border properties and radial suppression")
{
	ControlBarScheme scheme;
	scheme.m_name = "Mordor8x6";
	scheme.m_buttonBorderBuildColor = 0xFF436CBE;
	scheme.m_buttonBorderActionColor = 0xFF01AF02;
	scheme.m_buttonBorderUpgradeColor = 0xFFD06C00;
	scheme.m_buttonBorderSystemColor = 0xFFCFC302;
	scheme.m_buttonBorderAlteredColor = 0xFFFFFFFF;

	prodtest::ProdWorld f;
	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{ *f.logic, nullptr, view, ui, stream, &f.commands, MouseSettings{} };
	ControlBar bar(ctx);
	bar.setScheme(&scheme);

	// Synthetic non-radial button with BUILD border
	CommandButton bBuild;
	bBuild.m_name = "Build_Orcs";
	bBuild.m_commandButtonBorder = BUTTON_BORDER_BUILD;
	bBuild.m_radial = false;

	int borderType = bBuild.getButtonBorderType();
	bool draw = false;
	Color borderColor = bar.borderColorForType(borderType, &draw);
	bool drawBorder = (!bBuild.m_radial) && draw && (borderColor != 0x00FFFFFFu);

	CHECK(borderType == BUTTON_BORDER_BUILD);
	CHECK(borderColor == 0xFF436CBEu);
	CHECK(drawBorder == true);

	// Synthetic radial button with BUILD border (radial bubbles never draw borders, RW 0x4A579C)
	CommandButton bRadialBuild;
	bRadialBuild.m_name = "Radial_Build_Orcs";
	bRadialBuild.m_commandButtonBorder = BUTTON_BORDER_BUILD;
	bRadialBuild.m_radial = true;

	borderType = bRadialBuild.getButtonBorderType();
	draw = false;
	borderColor = bar.borderColorForType(borderType, &draw);
	drawBorder = (!bRadialBuild.m_radial) && draw && (borderColor != 0x00FFFFFFu);

	CHECK(borderType == BUTTON_BORDER_BUILD);
	CHECK(borderColor == 0xFF436CBEu);
	CHECK(drawBorder == false); // suppressed by radial flag!

	// Altered button
	CommandButton bAltered;
	bAltered.m_name = "Altered_Power";
	bAltered.m_commandButtonBorder = BUTTON_BORDER_ACTION;
	bAltered.setAltered(true);
	bAltered.m_radial = false;

	borderType = bAltered.getButtonBorderType();
	draw = false;
	borderColor = bar.borderColorForType(borderType, &draw);
	drawBorder = (!bAltered.m_radial) && draw && (borderColor != 0x00FFFFFFu);

	CHECK(borderType == BUTTON_BORDER_ALTERED);
	CHECK(borderColor == 0xFFFFFFFFu);
	CHECK(drawBorder == true);
}

TEST_CASE("LOCAL-12: populateOCLTimerSellButton has SYSTEM border (RW 0x944D9D)")
{
	ControlBarScheme scheme;
	scheme.m_name = "Mordor8x6";
	scheme.m_buttonBorderSystemColor = 0xFFCFC302; // yellow

	prodtest::ProdWorld f;
	PinholeView view;
	InGameUI ui;
	MessageStream stream;

	INI ini(f.w.fx.env);
	ini.loadMemory("sell.ini", initest::toBytes(
		"CommandButton Command_Sell\n"
		"  Command = SELL\n"
		"  ButtonBorderType = SYSTEM\n"
		"End\n"), INI_LOAD_OVERWRITE);

	HudContext ctx{ *f.logic, nullptr, view, ui, stream, &f.commands, MouseSettings{} };
	ControlBar bar(ctx);
	bar.setScheme(&scheme);

	GameWindowManager mgr;
	GadgetDrawList drawList;
	mgr.setDrawList(&drawList);

	auto sellWin = makePushButton(mgr, 300, 400, 32, 32, "ControlBar.wnd:OCLTimerSellButton");

	// Call populateOCLTimerSellButton (RW 0x944D54 / 0x944D9D)
	bar.populateOCLTimerSellButton(sellWin.get(), true);

	auto *data = static_cast<PushButtonData *>(sellWin->winGetUserData());
	REQUIRE(data);
	CHECK(data->drawBorder == true);
	CHECK(data->borderColor == 0xFFCFC302u);

	// Drawing this non-radial window produces the system border rectangle
	drawList.commands.clear();
	W3DGadgetPushButtonImageDraw(sellWin.get(), sellWin->winGetInstanceData());

	bool found = false;
	for (const auto &cmd : drawList.commands)
	{
		if (cmd.kind == GadgetDrawCommand::Kind::OpenRect)
		{
			found = true;
			CHECK(cmd.color == 0xFFCFC302u);
			CHECK(cmd.x0 == 299);
			CHECK(cmd.y0 == 399);
			CHECK(cmd.x1 == 333);
			CHECK(cmd.y1 == 433);
		}
	}
	CHECK(found);

	mgr.setDrawList(nullptr);
}
