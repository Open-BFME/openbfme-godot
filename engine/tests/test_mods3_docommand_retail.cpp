// OpenBFME. GPL-3.0.
// Lane MODS-3 on the retail data (SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset): DoCommandUpgrade (RW 0x8B8E2E / 0x8B8DFC) presses its retail
// buttons through Object::doCommandButton (RW 0x696FD2): the trolls' rock throwing (TOGGLE_WEAPONSET), the Hall of Twilight's technologies (PLAYER_UPGRADE,
// the NeededUpgrade gate of RW 0x695338); the unported cases are a named stop (S-5305); Edain Unchained's QueueIgnoreCP (mod engine feature
// edain-unchained.queue-ignore-cp) parses only with the feature and lets a doCommandButton build past the command point refusal only.

#include "doctest.h"

#include "Common/BuildAssistant.h"
#include "Common/ModEngineFeatures.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/ControlBarCommands.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectDoCommandButton.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/WeaponSetToggle.h"
#include "ProdTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

Shared &shared()
{
	static Shared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

#define REQUIRE_RETAIL_WORLD(sh)                                                       \
	Shared &sh = shared();                                                             \
	if (!sh.mount || !sh.mount->fs)                                                    \
	{                                                                                  \
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail DoCommandUpgrade test)"); \
		return;                                                                        \
	}                                                                                  \
	REQUIRE_MESSAGE(sh.world, sh.error);                                               \
	auto contextScope = sh.world->enterContext()

// a skirmish game of one human player of the faction on the shared world (as test_upgrade_retail.cpp)
struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	Player *player = nullptr;
	explicit Game(RetailObjectWorld &world, retailtest::Mount &mount, const std::string &faction)
		: players(world.nameKeys(), world.playerTemplates(), teams)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
		setup.startingMoney = 100000;
		setup.defaultStartingCash = 100000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(world.things(), world.modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic->random().seedRandom(1);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*mount.fs, logic->settings(), &err), err);
		logic->productionSettings() = world.productionSettings();
		logic->setUpgradeTypes(&world.upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*mount.fs, logic->economy().settings(), &err), err);
		logic->economy().initAllCommandPoints();
		player = players.findPlayerWithName("Tester");
		REQUIRE(player);
	}
	Object *make(RetailObjectWorld &world, const std::string &name)
	{
		const ThingTemplate *tt = world.things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Object *o = logic->newObject(tt, player->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		return o;
	}
	bool stopNoted(const std::string &prefix) const
	{
		const GameLogic::Report r = logic->report();
		return std::any_of(r.stops.begin(), r.stops.end(), [&](const std::string &s) { return s.rfind(prefix, 0) == 0; });
	}
};

const DoCommandUpgrade *doCommandModule(const Object &o, const std::string &button)
{
	for (const auto &m : o.modules())
	{
		const DoCommandUpgradeModuleData *d = dynamic_cast<const DoCommandUpgradeModuleData *>(m->getModuleData());
		if (d && d->m_getUpgradeCommandButtonName == button)
		{
			return dynamic_cast<const DoCommandUpgrade *>(m.get());
		}
	}
	return nullptr;
}

const ProductionEntry *queuedUpgrade(ProductionUpdateInterface &pu, const UpgradeTemplate *u)
{
	for (const ProductionEntry *e = pu.firstProduction(); e; e = pu.nextProduction(e))
	{
		if (e->type == PRODUCTION_UPGRADE && e->upgradeToResearch == u)
		{
			return e;
		}
	}
	return nullptr;
}

// every test leaves the process retail
struct Retail
{
	~Retail() { ModEngineFeatures::reset(); }
};
const char kEdainUnchainedGameDat[] = "5c31f5036cacb7690a6161b104a3efdb"; // Edain Unchained 4.4.3's game.dat (ModEngineFeatures::knownBinaries)
} // namespace

TEST_CASE("MODS-3 retail: DoCommandUpgrade is a runtime class; its retail modules carry the button names of their INI")
{
	REQUIRE_RETAIL_WORLD(sh);
	// workspace/rebuild/census (module histogram): DoCommandUpgrade is on 21 objects (inheritance resolved)
	int objects = 0;
	for (const ThingTemplate *t : sh.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
		{
			if (n.name == "DoCommandUpgrade")
			{
				REQUIRE(dynamic_cast<const DoCommandUpgradeModuleData *>(n.data.get()));
				++objects;
				break;
			}
		}
	}
	CHECK(objects == 21);
	const ThingTemplate *troll = sh.world->things().findTemplate("MordorMountainTroll");
	REQUIRE(troll);
	const DoCommandUpgradeModuleData *d = nullptr;
	for (const ThingTemplate::Nugget &n : troll->getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name == "DoCommandUpgrade")
		{
			d = dynamic_cast<const DoCommandUpgradeModuleData *>(n.data.get());
		}
	}
	REQUIRE(d);
	// mountaintroll.ini: TriggeredBy = Upgrade_SwitchToRockThrowing, GetUpgradeCommandButtonName = Command_ToggleMountainTrollRockThrow, no removal button
	CHECK(d->m_triggeredBy == std::vector<std::string>{ "Upgrade_SwitchToRockThrowing" });
	CHECK(d->m_getUpgradeCommandButtonName == "Command_ToggleMountainTrollRockThrow");
	CHECK(d->m_removeUpgradeCommandButtonName.empty());
	const CommandButton *b = TheCommandStore->findCommandButton(d->m_getUpgradeCommandButtonName);
	REQUIRE(b);
	CHECK(b->m_command == GUI_COMMAND_TOGGLE_WEAPONSET);
}

TEST_CASE("MODS-3 retail: a mountain troll's Upgrade_SwitchToRockThrowing presses Command_ToggleMountainTrollRockThrow: it throws rocks (WEAPONSET_TOGGLE_1)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount, "FactionMordor");
	Object *troll = g.make(*sh.world, "MordorMountainTroll");
	const DoCommandUpgrade *m = doCommandModule(*troll, "Command_ToggleMountainTrollRockThrow");
	REQUIRE(m);
	CHECK_FALSE(m->isAlreadyUpgraded());
	const int toggleCondition = WeaponSetToggle::modelConditionForWeaponSetBit(WeaponSetToggle::WEAPONSET_TOGGLE_1);
	REQUIRE(toggleCondition >= 0); // tolerance: RW 0xC16958 maps the bit to a model condition (-1 = none)
	CHECK((troll->getWeaponSetFlags()[0] & (1u << WeaponSetToggle::WEAPONSET_TOGGLE_1)) == 0);
	CHECK_FALSE(troll->testModelCondition(toggleCondition));
	// the troll's creation grants the upgrade in retail (scripts.lua through scriptevents.xml, PLAN rule 8): the module presses the TOGGLE_WEAPONSET button
	// (RW 0x8B8E2E -> RW 0x696FD2 case 45): the object's own flag was clear, so it is set
	troll->giveUpgrade("Upgrade_SwitchToRockThrowing");
	CHECK(m->isAlreadyUpgraded());
	CHECK((troll->getWeaponSetFlags()[0] & (1u << WeaponSetToggle::WEAPONSET_TOGGLE_1)) != 0); // tolerance: the one bit of the flag word
	CHECK(troll->testModelCondition(toggleCondition));
	// the upgrade has no removal button: taking it away presses nothing (RW 0x8B8DFC with an empty name), the troll keeps throwing rocks
	troll->removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_SwitchToRockThrowing"));
	CHECK((troll->getWeaponSetFlags()[0] & (1u << WeaponSetToggle::WEAPONSET_TOGGLE_1)) != 0); // tolerance: the one bit of the flag word
}

TEST_CASE("MODS-3 retail: the Hall of Twilight's levels queue its technologies (PLAYER_UPGRADE), the second one behind its NeededUpgrade (RW 0x695338)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount, "FactionAngmar");
	Object *hall = g.make(*sh.world, "AngmarHallofTwilight");
	ProductionUpdateInterface *pu = hall->getProductionUpdate();
	REQUIRE(pu);
	const UpgradeTemplate *soulFreeze = TheUpgradeCenter->findUpgrade("Upgrade_TechnologyAngmarSoulFreeze");
	const UpgradeTemplate *wellOfSouls = TheUpgradeCenter->findUpgrade("Upgrade_TechnologyAngmarWellOfSouls");
	REQUIRE(soulFreeze);
	REQUIRE(wellOfSouls);
	CHECK(soulFreeze->getUpgradeType() == UPGRADE_TYPE_PLAYER);
	const std::uint32_t money = g.player->getMoney()->countMoney();
	CHECK(pu->getProductionCount() == 0);

	// Command_PurchaseTechnologyAngmarWellOfSouls: Options NEED_UPGRADE, NeededUpgrade = Upgrade_AngmarHallofTwilightLevel2 (an OBJECT upgrade the hall lacks):
	// pressed now, RW 0x695338 answers 9 and nothing is queued
	const CommandButton *wellButton = TheCommandStore->findCommandButton("Command_PurchaseTechnologyAngmarWellOfSouls");
	REQUIRE(wellButton);
	hall->doCommandButton(wellButton, CMD_FROM_PLAYER, false);
	CHECK(pu->getProductionCount() == 0);

	// level 1 (the GrantUpgradeCreate of a finished hall): ModuleTag_UpgradeSoulFreeze presses Command_PurchaseTechnologyAngmarSoulFreeze (NEED_UPGRADE
	// commented out in the INI): the research is queued, ANGMAR_TECH_SOUL_FREEZE_BUILDCOST = 0 withdrawn, the player has it in production
	hall->giveUpgrade("Upgrade_AngmarHallofTwilightLevel1");
	REQUIRE(pu->getProductionCount() == 1);
	CHECK(queuedUpgrade(*pu, soulFreeze));
	CHECK(g.player->hasUpgradeInProduction(soulFreeze));
	CHECK(g.player->getMoney()->countMoney() == money);
	// level 2: the WellOfSouls module presses its button, which now passes its NeededUpgrade
	hall->giveUpgrade("Upgrade_AngmarHallofTwilightLevel2");
	CHECK(pu->getProductionCount() == 2);
	CHECK(queuedUpgrade(*pu, wellOfSouls));
	CHECK(g.player->hasUpgradeInProduction(wellOfSouls));
	// pressing it again queues nothing: the upgrade is in the queue (queueUpgrade RW 0x8A0FDA)
	hall->doCommandButton(wellButton, CMD_FROM_PLAYER, false);
	CHECK(pu->getProductionCount() == 2);
}

TEST_CASE("MODS-3 retail: a disabled object presses nothing; an unported command type is the named stop S-5305")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount, "FactionAngmar");
	Object *hall = g.make(*sh.world, "AngmarHallofTwilight");
	ProductionUpdateInterface *pu = hall->getProductionUpdate();
	REQUIRE(pu);
	const CommandButton *soulButton = TheCommandStore->findCommandButton("Command_PurchaseTechnologyAngmarSoulFreeze");
	REQUIRE(soulButton);
	// RW 0x696FE2 .. 0x69706C: disabled (here DISABLED_HELD) and not SUMMONING_REPLACEMENT: nothing
	hall->setDisabled(DISABLED_HELD, 0x3FFFFFFF); // RW 0x692432's forever
	hall->doCommandButton(soulButton, CMD_FROM_PLAYER, false);
	CHECK(pu->getProductionCount() == 0);
	hall->clearDisabled(DISABLED_HELD);
	hall->doCommandButton(soulButton, CMD_FROM_PLAYER, false);
	CHECK(pu->getProductionCount() == 1);

	// HORDE_TOGGLE_FORMATION appends MSG 0x453 to the local client's message stream from inside the logic: not ported, named
	const CommandButton *formation = nullptr;
	for (const std::string &name : TheCommandStore->buttonNames())
	{
		const CommandButton *b = TheCommandStore->findCommandButton(name);
		if (b && b->m_command == GUI_COMMAND_HORDE_TOGGLE_FORMATION)
		{
			formation = b;
			break;
		}
	}
	REQUIRE(formation);
	const std::string prefix = "[S-5305] Object::doCommandButton (RW 0x696FD2 / 0x697890 / 0x6979D9): doCommandButton HORDE_TOGGLE_FORMATION";
	CHECK_FALSE(g.stopNoted(prefix));
	hall->doCommandButton(formation, CMD_FROM_PLAYER, false);
	CHECK(g.stopNoted(prefix));
}

TEST_CASE("MODS-3 retail: QueueIgnoreCP (edain-unchained.queue-ignore-cp) lets a doCommandButton build past the command point refusal only; the update gate stays")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount, "FactionMen");
	Object *barracks = g.make(*sh.world, "GondorBarracks");
	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu);
	const CommandButton *retail = TheCommandStore->findCommandButton("Command_ConstructGondorFighterHorde"); // UNIT_BUILD, Object = GondorFighterHorde
	REQUIRE(retail);
	CHECK(retail->m_command == GUI_COMMAND_UNIT_BUILD);
	CHECK_FALSE(retail->m_queueIgnoreCP);
	const ThingTemplate *horde = retail->getThingTemplate();
	REQUIRE(horde);
	REQUIRE(BuildAssistant::canMakeUnit(*barracks, horde, -1) == CANMAKE_OK);
	// no command points left: canMakeUnit refuses with 7 (RW 0x794026)
	prodtest::setUsage(*g.player, g.logic->economy().commandPointLimit(*g.player));
	CHECK(BuildAssistant::canMakeUnit(*barracks, horde, -1) == CANMAKE_UNKNOWN_7);
	barracks->doCommandButton(retail, CMD_FROM_PLAYER, false);
	CHECK(pu->getProductionCount() == 0);
	// the same button with QueueIgnoreCP = Yes (what Edain Unchained's DoCommandUpgrade buttons carry): canMakeUnit skips only the command point test
	CommandButton ignoring = *retail;
	ignoring.m_queueIgnoreCP = true;
	CHECK(BuildAssistant::canMakeUnit(*barracks, horde, -1, true) == CANMAKE_OK);
	const std::uint32_t money = g.player->getMoney()->countMoney();
	barracks->doCommandButton(&ignoring, CMD_FROM_PLAYER, false);
	REQUIRE(pu->getProductionCount() == 1);
	const ProductionEntry *e = pu->firstProduction();
	CHECK(e->objectToProduce == horde);
	CHECK(g.player->getMoney()->countMoney() == money - (std::uint32_t)e->cost);
	// the other refusals stay: no money -> 2 (CANMAKE_NO_MONEY) even with the flag
	g.player->getMoney()->withdraw(g.player->getMoney()->countMoney());
	CHECK(BuildAssistant::canMakeUnit(*barracks, horde, -1, true) == CANMAKE_NO_MONEY);
	// the update-time gate (RW 0x8A1E02 .. 0x8A1E82) still waits for command points: no progress
	for (int i = 0; i < 10; ++i)
	{
		g.logic->runLogicFrame();
	}
	CHECK(pu->getProductionCount() == 1);
	CHECK(pu->firstProduction()->framesUnderConstruction == 0.0f);
}

TEST_CASE("MODS-3: CommandButton QueueIgnoreCP parses only with edain-unchained.queue-ignore-cp (+0x10D, default No); retail refuses the field")
{
	Retail guard;
	const char kButtons[] = "CommandButton Command_A\n  Command = UNIT_BUILD\n  QueueIgnoreCP = Yes\nEnd\n";
	{
		prodtest::ProdWorld f;
		const std::string err = f.w.load(kButtons);
		CHECK_MESSAGE(err.find("Unknown field 'QueueIgnoreCP' in block") != std::string::npos, err); // INI::initFromINI's INIException 5, as retail
	}
	std::string error;
	REQUIRE_MESSAGE(ModEngineFeatures::activate({ kEdainUnchainedGameDat }, &error), error);
	REQUIRE(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedQueueIgnoreCP));
	prodtest::ProdWorld f;
	const std::string err = f.w.load(std::string(kButtons) + "CommandButton Command_B\n  Command = UNIT_BUILD\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	REQUIRE(f.commands.findCommandButton("Command_A"));
	REQUIRE(f.commands.findCommandButton("Command_B"));
	CHECK(f.commands.findCommandButton("Command_A")->m_queueIgnoreCP);
	CHECK_FALSE(f.commands.findCommandButton("Command_B")->m_queueIgnoreCP); // RW 0x75D688 (patched): the dword store zeroes + 0x10D
}
