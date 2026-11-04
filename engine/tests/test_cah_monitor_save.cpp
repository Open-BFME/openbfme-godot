// OpenBFME unit tests. GPL-3.0.
// Both editions, native save reconstruction: the existing monitor's saved command-set reference remains usable after loading.
#include "doctest.h"
#include "LogicTestUtil.h"
#include "Common/GameState.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponSetToggle.h"
#include <map>
#include <string>
#include <vector>

namespace
{
std::map<std::string, std::uint32_t> monitorSaveSections(const GameLogic &logic)
{
 std::vector<GameLogic::StateHashSection> sections;
 logic.computeStateHashBreakdown(sections, nullptr);
 std::map<std::string, std::uint32_t> out;
 for (const auto &section : sections) out[section.name] = section.value;
 return out;
}
} // namespace

TEST_CASE("create-a-hero save: a monitored temporary replacement retains the returning generated power buttons")
{
	// Both editions: native snapshot reconstruction, not a change to rank/power rules.
	ExperienceLevelSystem levels;
	CreateAHeroSystem cah;
	CommandStore commands, freshCommands;
	struct RestoreGlobals
	{
		ExperienceLevelSystem *levels = TheExperienceLevelSystem;
		CreateAHeroSystem *cah = TheCreateAHeroSystem;
		CommandStore *commands = TheCommandStore;
		~RestoreGlobals() { TheExperienceLevelSystem = levels; TheCreateAHeroSystem = cah; TheCommandStore = commands; }
	} restore;
	TheExperienceLevelSystem = &levels;
	TheCreateAHeroSystem = &cah;
	TheCommandStore = &commands;
	logictest::LogicWorld world;
	LogicModules::registerAll(world.w.modules);
	levels.registerBlocks(world.w.fx.env.blocks);
	world.w.fx.env.blocks.registerBlock("CommandButton", CommandStore::parseCommandButtonDefinitionGlobal);
	world.w.fx.env.blocks.registerBlock("CommandSet", CommandStore::parseCommandSetDefinitionGlobal);
	cah.canBuildUpgradeName = "Upgrade_AllowCAH";
	cah.mapModeUpgradeName = "Upgrade_MapCAH";
	cah.gameModeUpgradeName = "Upgrade_GameCAH";
	cah.commandSetTemplate = "CahTemplate";
	REQUIRE(world.w.load(R"ini(
Upgrade Upgrade_AllowCAH
 Type = PLAYER
End
Upgrade Upgrade_MapCAH
End
Upgrade Upgrade_GameCAH
End
Upgrade Upgrade_ClassCAH
End
ExperienceLevel CreateAHeroLevel1
 TargetNames = CreateAHero
 RequiredExperience = 1
 Rank = 1
End
ExperienceLevel CreateAHeroLevel2
 TargetNames = CreateAHero
 RequiredExperience = 100
 Rank = 2
End
Object CreateAHero
 KindOf = CREATE_A_HERO HERO
 Behavior = MonitorConditionUpdate ModuleTag_Monitor
  WeaponSetFlags = WEAPONSET_TOGGLE_1
  WeaponToggleCommandSet = TemporaryCommands
 End
End
)ini").empty());
	CreateAHeroClass cls;
	cls.upgradeName = "Upgrade_ClassCAH";
	CreateAHeroSubClass sub;
	sub.upgradeName = "Upgrade_ClassCAH";
	cls.subClasses.push_back(sub);
	cah.addClass(cls, nullptr);
	const std::string commandIni = R"ini(
CommandButton Command_CahLow
End
CommandButton Command_CahHigh
End
CommandButton Command_CahOther
End
CommandButton Command_AttackMove
End
CommandSet CahTemplate
 2 = Command_CahOther
End
CommandSet TemporaryCommands
 1 = Command_CahOther
End
)ini";
	REQUIRE(world.w.load(commandIni).empty());
	CreateAHeroHero record;
	record.uniqueID = "PowerSnapshotHero";
	record.powers[0].commandButton = "Command_CahLow";
	record.powers[0].buttonIndex = 0;
	record.powers[1].commandButton = "Command_CahHigh";
	record.powers[1].buttonIndex = 0;
	record.powers[1].expLevel = 1;
	Player *owner = world.players.findPlayerWithName("Alice");
	REQUIRE(owner);
	world.logic->createAHeroes().assign(*owner, record);
	world.logic->createAHeroes().startGame();
	Object *hero = world.make("CreateAHero", owner->getDefaultTeam());
	hero->setName("NamedHero");
	hero->getExperienceTracker()->gainLevel(false);
	REQUIRE(hero->getExperienceTracker()->getRank() == 2);
	int commandRank = 2;
	SUBCASE("saved current-rank set returns") {}
	SUBCASE("a pending rank grant keeps the earlier displaced set") { commandRank = 1; }
	world.logic->createAHeroes().buildCommandSet(*hero, commandRank);
	const ObjectID id = hero->getID();
	const std::string savedSet = hero->getCommandSetOverride();
	const std::string expectedPower = commandRank == 1 ? "Command_CahLow" : "Command_CahHigh";
	auto *monitor = dynamic_cast<MonitorConditionUpdate *>(hero->findModule("MonitorConditionUpdate"));
	REQUIRE(monitor);
	hero->setWeaponSetFlag(WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
	monitor->update();
	REQUIRE(hero->getCommandSetOverride() == "TemporaryCommands");
	REQUIRE(monitor->savedCommandSet() == savedSet);
	const std::vector<std::string> blocks = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random",
		"obfme:Objects", "obfme:UpdateScheduler", "obfme:TunnelTracker", "obfme:Combat", "obfme:SellList", "obfme:Experience",
		"obfme:Invisibility", "obfme:DamageOverTime", "obfme:CreateAHero" };
	std::vector<std::uint8_t> bytes;
	world.logic->gameState().saveBlocks(blocks, bytes);
	const auto before = monitorSaveSections(*world.logic);
	TheCommandStore = &freshCommands;
	REQUIRE(world.w.load(commandIni).empty());
	REQUIRE_FALSE(freshCommands.findCommandSet(savedSet));
	world.activateUpgrades();
	world.logic->gameState().loadBlocks(blocks, bytes);
	hero = world.logic->findObjectByID(id);
	REQUIRE(hero);
	monitor = dynamic_cast<MonitorConditionUpdate *>(hero->findModule("MonitorConditionUpdate"));
	REQUIRE(monitor);
	CHECK(hero->getCommandSetOverride() == "TemporaryCommands");
	CHECK(monitor->savedCommandSet() == savedSet);
	const auto after = monitorSaveSections(*world.logic);
	for (const char *section : { "objects", "create-a-hero", "experience", "players and teams", "frame, object ids, RNG" })
	{
		CAPTURE(section);
		CHECK(after.at(section) == before.at(section));
	}
	std::vector<std::uint8_t> again;
	world.logic->gameState().saveBlocks(blocks, again);
	CHECK(again == bytes);
	const CommandSet *rebuilt = freshCommands.findCommandSet(savedSet);
	REQUIRE(rebuilt);
	REQUIRE(rebuilt->getCommandButton(0));
	CHECK(rebuilt->getCommandButton(0)->m_name == expectedPower);
	world.logic->createAHeroes().finishLoad();
	CHECK(hero->getCommandSetOverride() == "TemporaryCommands");
	CHECK(monitorSaveSections(*world.logic) == after);
	hero->setWeaponSetFlag(WeaponSetToggle::WEAPONSET_TOGGLE_1, false);
	monitor->update();
	CHECK(hero->getCommandSetOverride() == savedSet);
	CHECK(monitor->savedCommandSet().empty());
	REQUIRE(TheCommandStore->findCommandSet(hero->getCommandSetName()));
	CHECK(TheCommandStore->findCommandSet(hero->getCommandSetName())->getCommandButton(0)->m_name == expectedPower);
}
