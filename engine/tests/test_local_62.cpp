// OpenBFME unit tests for Local 62 (P-122): capturing a production building cancels its production queue,
// refunds the previous owner for unspawned entries, pushes any doorway unit out on the previous owner's team
// and releases the last exit (partial horde) before changing the building's team.
//
// TARGET FACTS (RotWK 2.01 game.dat, MD5 daed3668006cd90f01c34e5a7da1901f; DONOR BFME2 1.06 game.dat):
//   * SpecialAbilityUpdate::triggerAbilityEffect (RW 0x8544F2..0x854772, DONOR BFME2 0x00451360) for enums
//     0x1D (SPECIAL_INFANTRY_CAPTURE_BUILDING) and 0x1A (SPECIAL_BLACKLOTUS_CAPTURE_BUILDING) calls
//     Object::setCapturedTeam(newTeam) (RW 0x6996DC, DONOR BFME2 0x00298C76 Object::defect) on the capture
//     flag (RW 0x854702) and on the building (RW 0x854725, either the target or the closest LINKED_TO_FLAG
//     within 150.0f).
//   * Object::setCapturedTeam (RW 0x6996DC..0x6998CD) calls getProductionUpdateInterface(false) (RW 0x68C327(0))
//     at RW 0x69973D..0x69974D BEFORE setTeam(newTeam) (RW 0x6997B0) and invokes ProductionUpdateInterface
//     vtable (RW 0xC67DB0) slot 16 (+0x40, RW 0x8A1666..0x8A16FF, DONOR BFME2 0x0049DEC8).
//   * RW 0x8A1666:
//     1. calls slot 15 (+0x3C) cancelAllProduction() (RW 0x8A05B2, DONOR BFME2 0x0049CE6E), which sets
//        m_cancellingAll = true (+0x11D) and cancels all queued entries while getControllingPlayer() is still
//        the old owner:
//        - unit/hero entries (RW 0x8A13EE): when !(percentComplete >= 100.0f && m_exitingObjectID != INVALID_ID),
//          refunds stored e->cost (+0x28) to the old owner via depositMoney(e->cost, true) (RW 0x7B18B8) and
//          scoreKeeper.addMoneySpentByKind(logic, e->objectToProduce, -e->cost) (RW 0x79F10B), cancels hero
//          production on the old owner (RW 0x8A1485 -> 0x780C64) for PRODUCTION_BUILD_INDEX, unreserves the exit
//          door (RW 0x8A0CF2) and removes the entry; when completedWaiting is true, removes the entry without
//          refunding e->cost (RW 0x8A1422..0x8A1443);
//        - upgrade entries (RW 0x8A1140): refunds stored e->cost to the old owner (RW 0x8A119E), removes the
//          entry (RW 0x8A11A7) and for UPGRADE_TYPE_PLAYER clears the old owner's in-production bit via
//          oldOwner->removeUpgrade(upgrade) (RW 0x8A11C6 -> 0x6AE60C);
//     2. if m_exitingObjectID (+0x120) != INVALID_ID (RW 0x8A1681), finds the object (RW 0x449681) and unless it
//        has RespawnUpdate (RW 0x68BDA5), calls exit->exitObjectViaDoor(obj, DOOR_1) (RW 0x8A16E4 -> 0x8A3DD5),
//        pushing the doorway unit out on the old owner's team and clearing DISABLED_HELD (RW 0x8A41FA);
//     3. if exit != nullptr, calls exit->releaseLastExit() (RW 0x8A16EF -> 0x8A3BF8), releasing any partial
//        horde already spawned on the old owner's team.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/ScoreKeeper.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"

namespace
{

const char kLocal62Ini[] =
	"Upgrade Upgrade_Local62Object\n"
	"  Type = OBJECT\n"
	"  BuildCost = 150\n"
	"  BuildTime = 10.0\n"
	"End\n"
	"Upgrade Upgrade_Local62Player\n"
	"  Type = PLAYER\n"
	"  BuildCost = 200\n"
	"  BuildTime = 10.0\n"
	"End\n"
	"SpecialPower SpecialAbilityCaptureBuildingTest\n"
	"  Enum = SPECIAL_INFANTRY_CAPTURE_BUILDING\n"
	"  ReloadTime = 0\n"
	"End\n"
	"Object Local62Flag\n"
	"  KindOf = STRUCTURE IMMOBILE CAPTURABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object Local62Capturer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialAbilityCaptureBuildingTest\n"
	"  End\n"
	"  Behavior = SpecialAbilityUpdate ModuleTag_Ability\n"
	"    SpecialPowerTemplate = SpecialAbilityCaptureBuildingTest\n"
	"    PreparationTime = 1000\n"
	"    StartAbilityRange = 50.0\n"
	"    UnpackTime = 0\n"
	"    PackTime = 0\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n"
	"Object Local62Inn\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE FS_FACTORY LINKED_TO_FLAG\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 30\n"
	"  GeometryHeight = 40\n"
	"  CommandSet = Local62InnSet\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 6\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:10.0 Y:0.0 Z:0.0\n"
	"    NaturalRallyPoint = X:40.0 Y:0.0 Z:0.0\n"
	"    ExitDelay = 1000\n"
	"    InitialBurst = 0\n"
	"  End\n"
	"  Behavior = CommandSetUpgrade ModuleTag_Upgrade2\n"
	"    TriggeredBy = Upgrade_Local62Object\n"
	"    CommandSet = BarracksSet2\n"
	"  End\n"
	"End\n"
	"Object FadeSoldier\n"
	"  BuildCost = 100\n"
	"  BuildTime = 2.0\n"
	"  BuildFadeInOnCreateTime = 2.0\n"
	"  CommandPoints = 5\n"
	"  KindOf = PRELOAD SELECTABLE CAN_ATTACK INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Local62Horde\n"
	"  BuildCost = 300\n"
	"  BuildTime = 10.0\n"
	"  CommandPoints = 15\n"
	"  KindOf = SELECTABLE CAN_ATTACK INFANTRY HORDE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    Slots = 3\n"
	"    InitialPayload = Soldier 3\n"
	"    RankInfo = RankNumber:1 UnitType:Soldier Position:X:0 Y:0\n"
	"    RankInfo = RankNumber:1 UnitType:Soldier Position:X:0 Y:10\n"
	"    RankInfo = RankNumber:1 UnitType:Soldier Position:X:0 Y:20\n"
	"  End\n"
	"End\n"
	"Object Local62Hero\n"
	"  BuildCost = 250\n"
	"  BuildTime = 10.0\n"
	"  CommandPoints = 10\n"
	"  KindOf = PRELOAD SELECTABLE CAN_ATTACK INFANTRY HERO\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"CommandButton Command_Local62Soldier\n  Command = UNIT_BUILD\n  Object = Soldier\n  Options = CANCELABLE\nEnd\n"
	"CommandButton Command_Local62FadeSoldier\n  Command = UNIT_BUILD\n  Object = FadeSoldier\n  Options = CANCELABLE\nEnd\n"
	"CommandButton Command_Local62Horde\n  Command = UNIT_BUILD\n  Object = Local62Horde\n  Options = CANCELABLE\nEnd\n"
	"CommandButton Command_Local62ReviveHero\n  Command = REVIVE\n  Options = CANCELABLE\nEnd\n"
	"CommandSet Local62InnSet\n"
	"  1 = Command_Local62Soldier\n"
	"  2 = Command_Local62FadeSoldier\n"
	"  3 = Command_Local62Horde\n"
	"  4 = Command_Local62ReviveHero\n"
	"End\n";

struct Local62World : combattest::CombatWorld
{
	CommandStore commands;
	CommandStore *savedStore = nullptr;
	UpgradeTypeTable upgrades;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;

	Local62World()
	{
		savedStore = TheCommandStore;
		TheCommandStore = &commands;
		commands.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		logic->productionSettings() = prodtest::defaultSettings();
		std::string upgErr;
		upgrades.scan("Upgrade Upgrade_Local62Object\n  Type = OBJECT\n  BuildCost = 150\n  BuildTime = 10.0\nEnd\nUpgrade Upgrade_Local62Player\n  Type = PLAYER\n  BuildCost = 200\n  BuildTime = 10.0\nEnd\n", &upgErr);
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		logic->setUpgradeTypes(&upgrades);
		std::string err = w.load(prodtest::kBarracksObjects, INI_LOAD_OVERWRITE, "barracks_obj.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(prodtest::kBarracksCommands, INI_LOAD_OVERWRITE, "barracks_cmd.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kLocal62Ini, INI_LOAD_OVERWRITE, "local62.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		upgrades.assign(upgradeCenter);
		alice()->getMoney()->init();
		bob()->getMoney()->init();
	}
	~Local62World()
	{
		TheSpecialPowerStore = savedPowers;
		TheCommandStore = savedStore;
	}
	Player *alice() { return players.findPlayerWithName("Alice"); }
	Player *bob() { return players.findPlayerWithName("Bob"); }

	int countByTemplate(const std::string &name, const Team *team = nullptr) const
	{
		int n = 0;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->isEffectivelyDead() || o->isDestroyed())
			{
				continue;
			}
			if (o->getTemplate() && o->getTemplate()->getName() == name && (!team || o->getTeam() == team))
			{
				++n;
			}
		}
		return n;
	}
};

} // namespace

TEST_CASE("LOCAL-62: capturing a building with queued units cancels the queue, refunds the previous owner and produces nothing for the capturer (RW 0x6996DC -> RW 0x8A1666)")
{
	Local62World f;
	Player *alice = f.alice();
	Player *bob = f.bob();
	REQUIRE(alice != nullptr);
	REQUIRE(bob != nullptr);
	alice->setPlayerRelationship(bob, ENEMIES);
	bob->setPlayerRelationship(alice, ENEMIES);

	alice->getMoney()->deposit(1000, false);
	bob->getMoney()->deposit(500, false);

	// Place a CaptureFlag and a LINKED_TO_FLAG production building (Local62Inn) within 150 units of the flag, owned by Alice.
	Object *flag = f.spawn("Local62Flag", 200.0f, 200.0f, 0.0f, alice->getDefaultTeam());
	Object *inn = f.spawn("Local62Inn", 240.0f, 200.0f, 0.0f, alice->getDefaultTeam());
	REQUIRE(flag != nullptr);
	REQUIRE(inn != nullptr);
	flag->friend_onBuildComplete();
	inn->friend_onBuildComplete();

	ProductionUpdateInterface *pu = inn->getProductionUpdate();
	REQUIRE(pu != nullptr);

	const ThingTemplate *soldierTmpl = f.w.things.findTemplate("Soldier");
	REQUIRE(soldierTmpl != nullptr);

	// Alice queues two Soldiers (BuildCost = 100 each, BuildTime = 10.0s = 50 frames).
	REQUIRE(pu->queueCreateUnit(soldierTmpl, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	REQUIRE(pu->queueCreateUnit(soldierTmpl, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	CHECK(pu->getProductionCount() == 2);
	CHECK(alice->getMoney()->countMoney() == 800u);
	CHECK(alice->getScoreKeeper().moneySpentOnUnits() == 200);
	CHECK(alice->getScoreKeeper().moneySpentByKind() == 200);

	// Advance 20 frames (40% of the first Soldier's 50-frame build time).
	f.frames(20);
	REQUIRE(pu->getProductionCount() == 2);
	REQUIRE(pu->firstProduction() != nullptr);
	CHECK(pu->firstProduction()->framesUnderConstruction == 20.0f);
	CHECK(pu->firstProduction()->percentComplete == SimMath::mulf32(SimMath::divf32(20.0f, 50.0f), 100.0f));
	CHECK(f.countByTemplate("Soldier") == 0);

	// Bob's capturer captures the flag via SpecialAbilityCaptureBuilding (enum 0x1D, RW 0x8544F2..0x854725),
	// which captures both the flag and the linked Local62Inn within 150.0f.
	Object *capturer = f.spawn("Local62Capturer", 210.0f, 200.0f, 0.0f, bob->getDefaultTeam());
	REQUIRE(capturer != nullptr);
	capturer->friend_onBuildComplete();
	auto *ability = dynamic_cast<SpecialAbilityUpdate *>(capturer->findModule("SpecialAbilityUpdate"));
	REQUIRE(ability != nullptr);
	const SpecialPowerTemplate *spt = ability->abilityData()->m_specialPowerTemplate;
	REQUIRE(spt != nullptr);
	REQUIRE(spt->m_type == 0x1D);
	ability->initiateIntentToDoSpecialPower(spt, flag, nullptr, 0, 0);

	for (int i = 0; i < 30 && inn->getTeam() != bob->getDefaultTeam(); ++i)
	{
		f.frames(1);
	}
	REQUIRE(flag->getTeam() == bob->getDefaultTeam());
	REQUIRE(inn->getTeam() == bob->getDefaultTeam());

	// Queue must be cancelled at capture time, and the full 200 stored cost refunded to Alice (the previous owner), not Bob.
	CHECK(pu->getProductionCount() == 0);
	CHECK(pu->firstProduction() == nullptr);
	CHECK(alice->getMoney()->countMoney() == 1000u);
	CHECK(bob->getMoney()->countMoney() == 500u);
	CHECK(alice->getScoreKeeper().moneySpentOnUnits() == 0);
	CHECK(alice->getScoreKeeper().moneySpentByKind() == 0);

	// Run 100 more frames: no Soldier may ever exit for Bob or Alice.
	f.frames(100);
	CHECK(pu->getProductionCount() == 0);
	CHECK(f.countByTemplate("Soldier", bob->getDefaultTeam()) == 0);
	CHECK(f.countByTemplate("Soldier", alice->getDefaultTeam()) == 0);
}

TEST_CASE("LOCAL-62: capturing a building with a doorway unit, partial horde, upgrade in research or hero entry handles each per RW 0x8A1666")
{
	Local62World f;
	Player *alice = f.alice();
	Player *bob = f.bob();
	REQUIRE(alice != nullptr);
	REQUIRE(bob != nullptr);

	Object *inn = f.spawn("Local62Inn", 200.0f, 200.0f, 0.0f, alice->getDefaultTeam());
	REQUIRE(inn != nullptr);
	inn->friend_onBuildComplete();
	auto *pu = dynamic_cast<ProductionUpdate *>(inn->getProductionUpdate());
	REQUIRE(pu != nullptr);

	SUBCASE("unit already created and waiting in doorway (m_exitingObjectID != INVALID_ID) exits for old owner without refund while queued unit behind it is refunded")
	{
		alice->getMoney()->deposit(1000, false);
		bob->getMoney()->deposit(500, false);

		const ThingTemplate *fadeTmpl = f.w.things.findTemplate("FadeSoldier");
		const ThingTemplate *soldierTmpl = f.w.things.findTemplate("Soldier");
		REQUIRE(fadeTmpl != nullptr);
		REQUIRE(soldierTmpl != nullptr);

		// FadeSoldier: BuildTime = 2.0s (10 frames), BuildFadeInOnCreateTime = 2000ms (10 frames), BuildCost = 100.
		// Soldier: BuildTime = 10.0s (50 frames), BuildCost = 100.
		REQUIRE(pu->queueCreateUnit(fadeTmpl, -1, pu->requestUniqueUnitID(), -1, false, "", false));
		REQUIRE(pu->queueCreateUnit(soldierTmpl, -1, pu->requestUniqueUnitID(), -1, false, "", false));
		CHECK(alice->getMoney()->countMoney() == 800u);

		// Frame 10: FadeSoldier reaches 100%, is created on Alice's team as m_exitingObjectID and waits in the doorway (postExitDelay == 9, DISABLED_HELD set).
		f.frames(10);
		const ObjectID exitingId = pu->exitingObjectID();
		REQUIRE(exitingId != INVALID_ID);
		CHECK(pu->postExitDelay() == 9u);
		Object *doorUnit = f.logic->findObjectByID(exitingId);
		REQUIRE(doorUnit != nullptr);
		CHECK(doorUnit->getTeam() == alice->getDefaultTeam());
		CHECK(doorUnit->isDisabledByType(3)); // DISABLED_HELD while fading in

		// Bob captures the building while FadeSoldier is waiting in the doorway and Soldier is queued behind it.
		inn->setCapturedTeam(bob->getDefaultTeam());
		CHECK(inn->getTeam() == bob->getDefaultTeam());

		// Both entries are removed from the queue; only the unspawned second Soldier (100) is refunded to Alice (800 + 100 = 900).
		CHECK(pu->getProductionCount() == 0);
		CHECK(alice->getMoney()->countMoney() == 900u);
		CHECK(bob->getMoney()->countMoney() == 500u);

		// RW 0x8A1681..0x8A16E4 pushed the doorway FadeSoldier out via exitObjectViaDoor(DOOR_1) on Alice's team, clearing DISABLED_HELD.
		CHECK(doorUnit->getTeam() == alice->getDefaultTeam());
		CHECK_FALSE(doorUnit->isDisabledByType(3));

		// Run 60 more frames: the cancelled second Soldier never spawns.
		f.frames(60);
		CHECK(f.countByTemplate("Soldier") == 0);
		CHECK(f.countByTemplate("FadeSoldier", alice->getDefaultTeam()) == 1);
		CHECK(f.countByTemplate("FadeSoldier", bob->getDefaultTeam()) == 0);
	}

	SUBCASE("horde mid-exit cancels remaining members without refund and calls releaseLastExit on old owner's partial horde")
	{
		alice->getMoney()->deposit(1000, false);
		bob->getMoney()->deposit(500, false);

		const ThingTemplate *hordeTmpl = f.w.things.findTemplate("Local62Horde");
		REQUIRE(hordeTmpl != nullptr);

		// Local62Horde: BuildCost = 300, BuildTime = 2.0s (10 frames), 3 members, ExitDelay = 1000ms (5 frames).
		REQUIRE(pu->queueCreateUnit(hordeTmpl, -1, pu->requestUniqueUnitID(), -1, false, "", false));
		CHECK(alice->getMoney()->countMoney() == 700u);

		// Frame 10: the horde container + 1st member exit; 2 members remain in the entry (flag34 == true, cost == 0),
		// and the horde has UNSELECTABLE and UNDER_CONSTRUCTION set until releaseLastExit().
		f.frames(55);
		REQUIRE(pu->getProductionCount() == 1);
		CHECK(pu->firstProduction()->flag34);
		CHECK(pu->firstProduction()->cost == 0);
		CHECK(pu->firstProduction()->quantityRemaining() == 2);

		auto *exit = dynamic_cast<QueueProductionExitUpdate *>(inn->getObjectExitInterface());
		REQUIRE(exit != nullptr);
		const ObjectID hordeId = exit->lastExitID();
		REQUIRE(hordeId != INVALID_ID);
		Object *horde = f.logic->findObjectByID(hordeId);
		REQUIRE(horde != nullptr);
		const unsigned kUnselectable = (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
		const unsigned kUnderConstruction = (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
		CHECK(horde->testStatus(kUnselectable));
		CHECK(horde->testStatus(kUnderConstruction));
		CHECK(f.countByTemplate("Soldier", alice->getDefaultTeam()) == 1);

		// Bob captures the building mid-horde exit.
		inn->setCapturedTeam(bob->getDefaultTeam());
		CHECK(inn->getTeam() == bob->getDefaultTeam());
		CHECK(pu->getProductionCount() == 0);
		// Horde entry cost was zeroed when the horde container spawned (RW 0x8A2CC8), so 0 is refunded.
		CHECK(alice->getMoney()->countMoney() == 700u);
		CHECK(bob->getMoney()->countMoney() == 500u);

		// releaseLastExit() (RW 0x8A16EF -> 0x8A3BF8) cleared lastExitId and released Alice's partial 1-member horde.
		CHECK(exit->lastExitID() == INVALID_ID);
		CHECK_FALSE(horde->testStatus(kUnselectable));
		CHECK_FALSE(horde->testStatus(kUnderConstruction));
		CHECK(horde->getTeam() == alice->getDefaultTeam());

		// Advance 40 frames: the 2 cancelled members are never spawned.
		f.frames(40);
		CHECK(f.countByTemplate("Soldier", alice->getDefaultTeam()) == 1);
		CHECK(f.countByTemplate("Soldier", bob->getDefaultTeam()) == 0);
	}

	SUBCASE("upgrades (PLAYER and OBJECT) and hero build-index entries in queue are cancelled, refunded to old owner, and cleared on old owner")
	{
		alice->getMoney()->deposit(1000, false);
		bob->getMoney()->deposit(500, false);

		const UpgradeTemplate *playerUpg = f.upgradeCenter.findUpgrade("Upgrade_Local62Player");
		const UpgradeTemplate *objUpg = f.upgradeCenter.findUpgrade("Upgrade_Local62Object");
		const ThingTemplate *heroTmpl = f.w.things.findTemplate("Local62Hero");
		REQUIRE(playerUpg != nullptr);
		REQUIRE(objUpg != nullptr);
		REQUIRE(heroTmpl != nullptr);

		// Register Local62Hero in Alice's hero list at index 0.
		alice->heroes().addPurchase(*heroTmpl);
		REQUIRE(alice->heroes().isAvailable(0));
		const ProductionID heroProdId = pu->requestUniqueUnitID();
		REQUIRE(pu->queueCreateUnit(heroTmpl, 0, heroProdId, -1, false, "", false));
		REQUIRE(pu->queueUpgrade(playerUpg));
		REQUIRE(pu->queueUpgrade(objUpg));

		// Costs: Local62Hero (250) + Upgrade_Local62Player (200) + Upgrade_Local62Object (150) = 600 -> 400 left.
		CHECK(pu->getProductionCount() == 3);
		CHECK(alice->getMoney()->countMoney() == 400u);
		CHECK(alice->hasUpgradeInProduction(playerUpg));
		CHECK_FALSE(alice->heroes().isAvailable(0));
		CHECK(alice->heroes().findByProductionID(heroProdId) != nullptr);

		f.frames(15);
		REQUIRE(pu->getProductionCount() == 3);

		// Bob captures the building while the hero and both upgrades are in the queue.
		inn->setCapturedTeam(bob->getDefaultTeam());
		CHECK(inn->getTeam() == bob->getDefaultTeam());
		CHECK(pu->getProductionCount() == 0);

		// All 600 refunded to Alice; Alice's player upgrade in-production bit and hero production state are cleared.
		CHECK(alice->getMoney()->countMoney() == 1000u);
		CHECK(bob->getMoney()->countMoney() == 500u);
		CHECK_FALSE(alice->hasUpgradeInProduction(playerUpg));
		CHECK_FALSE(alice->hasUpgradeComplete(playerUpg));
		CHECK_FALSE(bob->hasUpgradeComplete(playerUpg));
		CHECK_FALSE(inn->hasUpgrade(objUpg));
		CHECK(alice->heroes().isAvailable(0));
		CHECK(alice->heroes().findByProductionID(heroProdId) == nullptr);

		f.frames(60);
		CHECK(f.countByTemplate("Local62Hero") == 0);
		CHECK_FALSE(alice->hasUpgradeComplete(playerUpg));
		CHECK_FALSE(bob->hasUpgradeComplete(playerUpg));
		CHECK_FALSE(inn->hasUpgrade(objUpg));
	}
}

TEST_CASE("LOCAL-62: capturing a building with an empty queue leaves money, upgrades and unit counts untouched")
{
	Local62World f;
	Player *alice = f.alice();
	Player *bob = f.bob();
	REQUIRE(alice != nullptr);
	REQUIRE(bob != nullptr);

	alice->getMoney()->deposit(1000, false);
	bob->getMoney()->deposit(500, false);

	Object *inn = f.spawn("Local62Inn", 200.0f, 200.0f, 0.0f, alice->getDefaultTeam());
	REQUIRE(inn != nullptr);
	inn->friend_onBuildComplete();
	ProductionUpdateInterface *pu = inn->getProductionUpdate();
	REQUIRE(pu != nullptr);
	REQUIRE(pu->getProductionCount() == 0);

	inn->setCapturedTeam(bob->getDefaultTeam());
	CHECK(inn->getTeam() == bob->getDefaultTeam());
	CHECK(pu->getProductionCount() == 0);
	CHECK(alice->getMoney()->countMoney() == 1000u);
	CHECK(bob->getMoney()->countMoney() == 500u);
}

