// OpenBFME. GPL-3.0. Command activation follows the retail button's Command / Options,
// not the power's name or its AutoAbility flag. No target translator or simulation runs here.
#include "doctest.h"
#include "MoveTestUtil.h"
#include "HudTestUtil.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/SpellStores.h"
#include "Common/SpecialPower.h"

namespace
{
struct ButtonRig
{
	movetest::MoveWorld world{10, 10};
	SpellStores spells{world.w.keys};
	CommandStore commands;
	PinholeView view{{0, -400, 500}, {0, 0, 0}, 1024, 768, 0.7f};
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*world.logic, world.ai.get(), view, ui, stream, &commands, {}};
	ControlBar bar{ctx};
	CommandButton button;
	Object *hero = nullptr;
	ButtonRig()
	{
		spells.install();
		SpellStores::registerBlocks(world.w.fx.env.blocks);
		REQUIRE(world.w.load("SpecialPower TestPower\n Enum = SPECIAL_GENERAL_TARGETLESS\nEnd\n"
			"Object ButtonHero\n KindOf = HERO SELECTABLE\n CommandSet = ButtonSet\n"
			" Behavior = AIUpdateInterface ModuleTag_AI\n End\nEnd\n").empty());
		button.m_name = "TestButton";
		button.m_command = GUI_COMMAND_SPECIAL_POWER;
		button.m_specialPowerName = "TestPower";
		button.m_inPalantir = true;
		commands.newDynamicCommandSet("ButtonSet")->setDynamicButton(0, &button);
		hero = world.make("ButtonHero", world.teamOf("Alice"));
		world.players.setLocalPlayer(hero->getControllingPlayer());
		ui.selectObject(hero->getID());
		bar.update();
	}
	bool press() { bar.update(); return bar.pressButton(0, true); }
};
}

TEST_CASE("hero activation buttons: target options enter targeting and pressing again cancels")
{
	ButtonRig r;
	for (int command : {GUI_COMMAND_SPECIAL_POWER, GUI_COMMAND_FIRE_WEAPON})
	{
		r.button.m_command = command;
		for (std::uint32_t option : {COMMAND_OPTION_NEED_TARGET_POS, COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT,
			 COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT, COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT, COMMAND_OPTION_CONTEXTMODE_COMMAND})
		{
			INFO(command, " ", option);
			r.button.m_options = option;
			REQUIRE(r.press());
			CHECK(r.ui.getGUICommand() == &r.button);
			CHECK(r.stream.pending() == 0);
			REQUIRE(r.press());
			CHECK(r.ui.getGUICommand() == nullptr);
			CHECK(r.stream.pending() == 0);
			r.ui.setGUICommand(nullptr);
		}
	}
}

TEST_CASE("hero activation buttons: instant powers retain options and AutoAbility does not make a toggle")
{
	ButtonRig r;
	for (bool autoAbility : {false, true})
	{
		r.button.m_autoAbility = autoAbility;
		r.button.m_options = 0x100u; // OK_FOR_MULTI_SELECT, not a target option
		REQUIRE(r.press());
		CHECK(r.ui.getGUICommand() == nullptr);
		const auto messages = r.stream.propagate();
		REQUIRE(messages.size() == 1);
		CHECK(messages[0].type() == MSG_DO_SPECIAL_POWER);
		REQUIRE(messages[0].argumentCount() == 3);
		CHECK(messages[0].arg(0).integer == (int)r.spells.specialPowers().findSpecialPowerTemplate("TestPower")->getID());
		CHECK(messages[0].arg(1).integer == (int)r.button.m_options);
		CHECK(messages[0].arg(2).objectID == r.hero->getID());
	}
}

TEST_CASE("hero activation buttons: passive and nonclickable buttons cannot dispatch through direct or shortcut presses")
{
	ButtonRig r;
	SUBCASE("NONPRESSABLE passive") { r.button.m_options = COMMAND_OPTION_NONPRESSABLE; }
	SUBCASE("IsClickable No") { r.button.m_isClickable = false; }
	CHECK_FALSE(r.press());
	CHECK(r.stream.pending() == 0);
	CHECK(r.ui.getGUICommand() == nullptr);
	r.button.m_autoAbility = true;
	CHECK_FALSE(r.bar.rightPressButton(0, true));
	CHECK(r.stream.pending() == 0);
}

TEST_CASE("hero activation buttons retail: Gimli Rogash and Gloin keep their 2.01 activation options")
{
	if (!hudtest::haveWorld("hero activation buttons retail")) return;
	const auto &store = hudtest::shared().world->commands();
	struct Expected { const char *name; int command; std::uint32_t targets; bool autoAbility; };
	const Expected expected[] = {
		{"Command_RohanGimliLeap", GUI_COMMAND_SPECIAL_POWER, 0x220, false},
		{"Command_RogashLeap", GUI_COMMAND_SPECIAL_POWER, 0x220, false},
		{"Command_GimliAxeThrow", GUI_COMMAND_FIRE_WEAPON, 0x1, true},
		{"Command_RogashDominateTroll", GUI_COMMAND_SPECIAL_POWER, 0x3, false},
		{"Command_SpecialAbilityDwarvenGloinSlam", GUI_COMMAND_SPECIAL_POWER, 0x1, true},
		{"Command_SpecialAbilityDwarvenGloinShakeFoundation", GUI_COMMAND_SPECIAL_POWER, 0x1, true},
		{"Command_SpecialAbilityGloinSmash", GUI_COMMAND_SPECIAL_POWER, 0x20, false},
		{"Command_SpecialAbilitySlayer", GUI_COMMAND_SPECIAL_POWER, 0, true},
		{"Command_SpecialAbilityRageoftheNorth", GUI_COMMAND_SPECIAL_POWER, 0, false},
	};
	for (const auto &e : expected)
	{
		INFO(e.name);
		const auto *b = store.findCommandButton(e.name);
		REQUIRE(b);
		CHECK(b->m_command == e.command);
		CHECK((b->m_options & 0x227u) == e.targets);
		CHECK(b->m_autoAbility == e.autoAbility);
		CHECK_FALSE(b->hasOption(COMMAND_OPTION_NONPRESSABLE));
		CHECK(b->m_isClickable);
	}
}
