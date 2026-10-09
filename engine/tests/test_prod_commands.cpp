// OpenBFME tests (lane PROD-1): CommandButton / CommandSet INI blocks (RW 0x5DA711 / 0x7205B9, tables RW 0xC2BAC8 / 0xC4F3D8).
// Synthetic cases pin the binary's acceptance and rejection rules (error codes and texts read from the disassembly); the retail case loads the
// real files through RetailObjectWorld and checks the counts the census and the binary-reading research found (1286 buttons, 674 sets).

#include "doctest.h"
#include "ObjectTestUtil.h"
#include "RetailTestMount.h"

#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Object/RetailObjectWorld.h"

namespace
{
struct CmdFixture
{
	objtest::World w;
	CommandStore store;
	CommandStore *saved;
	CmdFixture()
	{
		saved = TheCommandStore;
		TheCommandStore = &store;
		store.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
	}
	~CmdFixture() { TheCommandStore = saved; }
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr) { return w.load(text, type, "cmd.ini", code); }
};
} // namespace

TEST_CASE("commands: a CommandButton keeps every field the table accepts, with the constructor's defaults")
{
	CmdFixture f;
	REQUIRE(f.load("Object Unit\nEnd\n") == "");
	REQUIRE(f.load(
		"CommandButton Command_BuildUnit\n"
		"  Command = unit_build\n"
		"  Object = Unit\n"
		"  Options = NEED_UPGRADE CANCELABLE\n"
		"  NeededUpgrade = Upgrade_A Upgrade_B\n"
		"  NeededUpgrade = Upgrade_C\n"
		"  TextLabel = CONTROLBAR:Build\n"
		"  ButtonBorderType = BUILD\n"
		"  Radial = Yes\n"
		"  ShowProductionCount = Yes\n"
		"  AffectsKindOf = STRUCTURE\n"
		"End\n") == "");
	const CommandButton *b = f.store.findCommandButton("Command_BuildUnit");
	REQUIRE(b != nullptr);
	CHECK(b->m_command == GUI_COMMAND_UNIT_BUILD); // Command names are matched case insensitively (RW 0x75CB37)
	CHECK(b->m_options == (COMMAND_OPTION_NEED_UPGRADE | COMMAND_OPTION_CANCELABLE));
	CHECK(b->m_objectName == "Unit");
	CHECK(b->getThingTemplate() == f.w.get("Unit"));
	CHECK(b->m_neededUpgrade == std::vector<std::string>{ "Upgrade_A", "Upgrade_B", "Upgrade_C" }); // NeededUpgrade appends (RW 0x73B304)
	CHECK(b->m_commandButtonBorder == 1);
	CHECK(b->m_radial);
	CHECK(b->m_showProductionCount);
	CHECK(b->m_isClickable);          // constructor RW 0x75D516: IsClickable and ShowButton start true
	CHECK(b->m_showButton);
	CHECK(b->m_maxShotsToFire == 0x7FFFFFFF);
	CHECK(b->m_commandRangeCount == 33);
	CHECK(b->m_weaponSlotToggle1 == 5);
	CHECK(f.store.findCommandButton("command_buildunit") == nullptr); // names are case sensitive
}

TEST_CASE("commands: an unknown Object is NULL without an error, a missing factory is the plain int fault")
{
	CmdFixture f;
	REQUIRE(f.load("CommandButton A\n  Command = UNIT_BUILD\n  Object = NoSuchThing\nEnd\n") == "");
	CHECK(f.store.findCommandButton("A")->getThingTemplate() == nullptr);
	CHECK(f.store.findCommandButton("A")->m_objectName == "NoSuchThing");
	f.store.setThingFactory(nullptr);
	int code = 0;
	CHECK(f.load("CommandButton B\n  Object = Whatever\nEnd\n", INI_LOAD_OVERWRITE, &code) != "");
	CHECK(code == 8); // initFromINIMulti wraps the plain int (RW 0xDEAD0001) as "Unknown error parsing field"
}

TEST_CASE("commands: CommandButton rejections carry the retail error codes")
{
	CmdFixture f;
	int code = 0;
	CHECK(f.load("CommandButton A\n  Command = NOT_A_COMMAND\nEnd\n", INI_LOAD_OVERWRITE, &code).find("Command 'NOT_A_COMMAND' not found") != std::string::npos);
	CHECK(code == 3);
	CHECK(f.load("CommandButton B\n  Options = CANCELABLE +NEED_UPGRADE\nEnd\n", INI_LOAD_OVERWRITE, &code).find("may not mix") != std::string::npos);
	CHECK(code == 2); // RW 0x42E840 pushes 2 (the generic parseBitString32 of the repo says 3)
	CHECK(f.load("CommandButton C\n  Options = NOT_AN_OPTION\nEnd\n", INI_LOAD_OVERWRITE, &code) != "");
	CHECK(code == 3);
	CHECK(f.load("CommandButton D\n  ButtonBorderType = HUGE\nEnd\n", INI_LOAD_OVERWRITE, &code).find("lookup list") != std::string::npos);
	CHECK(f.load("CommandButton E\n  NoSuchField = 1\nEnd\n", INI_LOAD_OVERWRITE, &code) != "");
	CHECK(code == 5);
}

TEST_CASE("commands: a duplicate CommandButton is parsed and discarded, an override chains behind the base")
{
	CmdFixture f;
	REQUIRE(f.load("CommandButton A\n  Command = STOP\n  Radial = No\nEnd\n") == "");
	REQUIRE(f.load("CommandButton A\n  Command = SELL\n  Radial = Yes\nEnd\n") == ""); // RW 0x5DA7C5: no error, the first definition stays
	CHECK(f.store.findCommandButton("A")->m_command == GUI_COMMAND_STOP);
	CHECK_FALSE(f.store.findCommandButton("A")->m_radial);
	int code = 0;
	CHECK(f.load("CommandButton A\n  Command = NOT_A_COMMAND\nEnd\n", INI_LOAD_OVERWRITE, &code) != ""); // the discarded parse still throws
	REQUIRE(f.load("CommandButton A\n  Command = SELL\nEnd\n", INI_LOAD_CREATE_OVERRIDES) == ""); // RW 0x72054C
	const CommandButton *final = f.store.findCommandButton("A");
	CHECK(final->m_command == GUI_COMMAND_SELL);
	CHECK(final->m_isOverride);
	CHECK(f.store.findNonConstCommandButton("A")->m_command == GUI_COMMAND_STOP); // the base is untouched
	CHECK(f.store.buttonCount() == 1);
}

TEST_CASE("commands: a CommandSet resolves its buttons at parse time, has 33 slots and InitialVisible")
{
	CmdFixture f;
	REQUIRE(f.load("CommandButton A\n  Command = STOP\nEnd\nCommandButton B\n  Command = SELL\nEnd\n") == "");
	REQUIRE(f.load("CommandSet S\n  InitialVisible = 7\n  1 = A\n  33 = B\n  2 = A\nEnd\n") == "");
	const CommandSet *s = f.store.findCommandSet("S");
	REQUIRE(s != nullptr);
	CHECK(s->m_initialVisible == 7);
	CHECK(s->getCommandButton(0) == f.store.findCommandButton("A"));
	CHECK(s->getCommandButton(32) == f.store.findCommandButton("B"));
	CHECK(s->getCommandButton(5) == nullptr);
	CHECK(s->getCommandButton(33) == nullptr);
	REQUIRE(f.load("CommandSet Empty\nEnd\n") == "");
	CHECK(f.store.findCommandSet("Empty")->m_initialVisible == 33); // RW 0x80C949
	int code = 0;
	std::string e = f.load("CommandSet Bad\n  1 = Missing\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(e.find("Unknown command 'Missing' found in command set. File: cmd.ini Line: 2") != std::string::npos);
	CHECK(code == 3);
	CHECK(f.load("CommandSet Bad2\n  34 = A\nEnd\n", INI_LOAD_OVERWRITE, &code) != "");
	CHECK(code == 5); // only "1" .. "33" are keys
	e = f.load("CommandSet S\n  1 = A\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(e.find("Duplicate commandset S found!") != std::string::npos);
	CHECK(code == 3);
	CHECK(f.store.findCommandSet("s") == nullptr); // case sensitive
}

TEST_CASE("commands retail: the real CommandButton.ini and CommandSet.ini load clean (1286 buttons, 674 sets)")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("commands retail");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	RetailObjectWorld world(*mount->fs);
	std::string error;
	REQUIRE_MESSAGE(world.load(&error), error);
	for (const SubsystemLoadReport::FileError &e : world.report().errors)
	{
		INFO(e.file << ": " << e.message);
		CHECK(e.file != "ControlBar");
	}
	bool s200 = false;
	for (const std::string &l : world.acceptanceStops())
	{
		s200 = s200 || l.rfind("[S-200]", 0) == 0;
	}
	CHECK(s200); // the unresolved cross references are reported, not silent
	CommandStore &c = world.commands();
	CHECK(c.buttonCount() == 1286);
	CHECK(c.setCount() == 674);
	// a barracks set: slot 1 builds the guardian horde (commandset.ini DwarvenBarracksCommandSet)
	const CommandSet *barracks = c.findCommandSet("DwarvenBarracksCommandSet");
	REQUIRE(barracks != nullptr);
	const CommandButton *b = barracks->getCommandButton(0);
	REQUIRE(b != nullptr);
	CHECK(b->m_name == "Command_ConstructDwarvenGuardianHorde");
	CHECK(b->m_command == GUI_COMMAND_UNIT_BUILD);
	CHECK(b->hasOption(COMMAND_OPTION_CANCELABLE));
	REQUIRE(b->getThingTemplate() != nullptr);
	CHECK(b->getThingTemplate()->getName() == "DwarvenGuardianHorde");
	// the zerker button needs the level 3 upgrade
	const CommandButton *zerker = c.findCommandButton("Command_ConstructDwarvenZerkerHorde");
	REQUIRE(zerker != nullptr);
	CHECK(zerker->hasOption(COMMAND_OPTION_NEED_UPGRADE));
	CHECK(zerker->m_neededUpgrade == std::vector<std::string>{ "Upgrade_DwarvenBarracksLevel3" });
	// the research's command histogram: 136 UNIT_BUILD, 15 REVIVE
	size_t unitBuild = 0, revive = 0;
	for (const std::string &n : c.buttonNames())
	{
		const CommandButton *x = c.findCommandButton(n);
		unitBuild += x->m_command == GUI_COMMAND_UNIT_BUILD ? 1u : 0u;
		revive += x->m_command == GUI_COMMAND_REVIVE ? 1u : 0u;
	}
	CHECK(unitBuild == 136);
	CHECK(revive == 15);
}
