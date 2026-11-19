// OpenBFME unit tests (Local 91): synthetic tests for the five previously missing RotWK 2.01 functions ported in fork issue 717:
//   - ProductionUpdate::isUnitInQueue (slot 9, RW 0x008A03C1)
//   - ProductionUpdate::countValue30InQueue (slot 19, RW 0x008A0625)
//   - ProductionUpdate::cancelUnitCreateByTemplate (slot 13, RW 0x008A30A0) & deferred drain (RW 0x008A1B52) + xfer v2
//   - BuildableHeroListUpgrade::processUpgradeRemoval (slot 8, RW 0x008BC5CB / RW 0x008BC519) & PlayerHeroList::removePurchase (RW 0x0078138B)
//   - SimpleProductionExitUpdate::getUnitCreatePoint (primary slot 13, RW 0x008A9CBC)

#include "doctest.h"
#include "ProdTestUtil.h"

#include "Common/PlayerHeroList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/ProductionExitModules.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <cmath>
#include <cstdint>

using namespace prodtest;

namespace
{
const char kLocal91Upgrades[] =
	"Upgrade Upgrade_TestUpg\n"
	"  Type = PLAYER\n"
	"  BuildTime = 5.0\n"
	"  BuildCost = 50\n"
	"End\n"
	"Upgrade Upgrade_RingHero\n"
	"  Type = OBJECT\n"
	"End\n";

const char kLocal91Objects[] =
	"Object L91_Soldier\n"
	"  BuildCost = 100\n"
	"  BuildTime = 10.0\n"
	"  CommandPoints = 2\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L91_Archer\n"
	"  BuildCost = 150\n"
	"  BuildTime = 10.0\n"
	"  CommandPoints = 2\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L91_RingHero\n"
	"  BuildCost = 500\n"
	"  BuildTime = 20.0\n"
	"  CommandPoints = 0\n"
	"  KindOf = SELECTABLE HERO\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L91_Fortress\n"
	"  KindOf = STRUCTURE SELECTABLE FS_FACTORY\n"
	"  CommandSet = L91_FortressSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 5\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:0.0 Y:-40.0 Z:0.0\n"
	"  End\n"
	"  Behavior = BuildableHeroListUpgrade ModuleTag_RingHeroList\n"
	"    TriggeredBy = Upgrade_RingHero\n"
	"  End\n"
	"End\n"
	"Object L91_LumberMill\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Behavior = SupplyCenterProductionExitUpdate ModuleTag_SCExit\n"
	"    UnitCreatePoint = X:10.0 Y:20.0 Z:5.0\n"
	"    NaturalRallyPoint = X:30.0 Y:0.0 Z:0.0\n"
	"  End\n"
	"End\n";

const char kLocal91Commands[] =
	"CommandButton Command_L91_Soldier\n"
	"  Command = UNIT_BUILD\n"
	"  Object = L91_Soldier\n"
	"  Options = CANCELABLE\n"
	"End\n"
	"CommandButton Command_L91_Archer\n"
	"  Command = UNIT_BUILD\n"
	"  Object = L91_Archer\n"
	"  Options = CANCELABLE\n"
	"End\n"
	"CommandButton Command_L91_RingHero\n"
	"  Command = UNIT_BUILD\n"
	"  Object = L91_RingHero\n"
	"  Options = CANCELABLE\n"
	"End\n"
	"CommandButton Command_L91_TestUpg\n"
	"  Command = PLAYER_UPGRADE\n"
	"  Upgrade = Upgrade_TestUpg\n"
	"  Options = CANCELABLE\n"
	"End\n"
	"CommandSet L91_FortressSet\n"
	"  1 = Command_L91_Soldier\n"
	"  2 = Command_L91_Archer\n"
	"  3 = Command_L91_RingHero\n"
	"  4 = Command_L91_TestUpg\n"
	"End\n";

struct Local91World : ProdWorld
{
	Local91World()
	{
		std::string upgErr;
		REQUIRE_MESSAGE(upgrades.scan(kLocal91Upgrades, &upgErr), upgErr);
		load(kLocal91Upgrades);
		load(kLocal91Objects);
		load(kLocal91Commands);
	}
	Player *alice()
	{
		Team *t = teamOf("Alice");
		return t ? t->getControllingPlayer() : nullptr;
	}
};
} // namespace

TEST_CASE("LOCAL-91 ProductionUpdate: isUnitInQueue and countValue30InQueue (RW 0x008A03C1, RW 0x008A0625)")
{
	Local91World w;
	Player *alice = w.alice();
	REQUIRE(alice != nullptr);
	alice->depositMoney(2000, false);

	Object *fort = w.make("L91_Fortress", w.teamOf("Alice"));
	REQUIRE(fort != nullptr);
	ProductionUpdateInterface *pu = fort->getProductionUpdate();
	REQUIRE(pu != nullptr);

	const ThingTemplate *soldier = w.logic->things().findTemplate("L91_Soldier");
	const ThingTemplate *archer = w.logic->things().findTemplate("L91_Archer");
	const UpgradeTemplate *upg = TheUpgradeCenter->findUpgrade("Upgrade_TestUpg");
	REQUIRE(soldier != nullptr);
	REQUIRE(archer != nullptr);
	REQUIRE(upg != nullptr);

	const ProductionID id1 = pu->requestUniqueUnitID();
	const ProductionID id2 = pu->requestUniqueUnitID();
	CHECK(pu->queueCreateUnit(soldier, -1, id1, 7, false, "", false));
	CHECK(pu->queueCreateUnit(archer, -1, id2, 9, false, "", false));
	CHECK(pu->queueUpgrade(upg));

	// RW 0x008A03C1: matches unit/hero entries (type 1 or 3), not upgrade entries (type 2) or absent IDs
	CHECK(pu->isUnitInQueue(id1));
	CHECK(pu->isUnitInQueue(id2));
	CHECK_FALSE(pu->isUnitInQueue(9999));

	// RW 0x008A0625: counts entries with matching value30 (+0x30)
	CHECK(pu->countValue30InQueue(7) == 1);
	CHECK(pu->countValue30InQueue(9) == 1);
	CHECK(pu->countValue30InQueue(42) == 0);

	pu->cancelUnitCreate(id1);
	CHECK_FALSE(pu->isUnitInQueue(id1));
	CHECK(pu->isUnitInQueue(id2));
	CHECK(pu->countValue30InQueue(7) == 0);
	CHECK(pu->countValue30InQueue(9) == 1);
}

TEST_CASE("LOCAL-91 ProductionUpdate: cancelUnitCreateByTemplate immediate, deferred and xfer (RW 0x008A30A0, RW 0x008A1B52)")
{
	Local91World w;
	Player *alice = w.alice();
	REQUIRE(alice != nullptr);
	alice->depositMoney(2000, false);

	Object *fort = w.make("L91_Fortress", w.teamOf("Alice"));
	REQUIRE(fort != nullptr);
	ProductionUpdate *pu = dynamic_cast<ProductionUpdate *>(fort->getProductionUpdate());
	REQUIRE(pu != nullptr);

	const ThingTemplate *soldier = w.logic->things().findTemplate("L91_Soldier");
	const ThingTemplate *archer = w.logic->things().findTemplate("L91_Archer");
	REQUIRE(soldier != nullptr);
	REQUIRE(archer != nullptr);

	// Immediate cancellation (arg2 = true): cancels all unfinished matching units, leaves other templates
	const ProductionID s1 = pu->requestUniqueUnitID();
	const ProductionID a1 = pu->requestUniqueUnitID();
	const ProductionID s2 = pu->requestUniqueUnitID();
	CHECK(pu->queueCreateUnit(soldier, -1, s1, 0, false, "", false));
	CHECK(pu->queueCreateUnit(archer, -1, a1, 0, false, "", false));
	CHECK(pu->queueCreateUnit(soldier, -1, s2, 0, false, "", false));
	const std::uint32_t moneyBefore = alice->getMoney()->countMoney();

	pu->cancelUnitCreateByTemplate(soldier, true);
	CHECK_FALSE(pu->isUnitInQueue(s1));
	CHECK_FALSE(pu->isUnitInQueue(s2));
	CHECK(pu->isUnitInQueue(a1));
	CHECK(alice->getMoney()->countMoney() == moneyBefore + 200);

	// Deferred cancellation (arg2 = false): stays queued until next update() (RW 0x008A1B52), survives xfer
	const ProductionID s3 = pu->requestUniqueUnitID();
	CHECK(pu->queueCreateUnit(soldier, -1, s3, 0, false, "", false));
	pu->cancelUnitCreateByTemplate(soldier, false);
	CHECK(pu->isUnitInQueue(s3));

	XferSave saver;
	pu->xfer(saver);
	XferLoad loader(saver.data().data(), saver.data().size());
	pu->xfer(loader);
	CHECK(loader.position() == saver.data().size());
	CHECK(pu->isUnitInQueue(s3));

	w.frames(1);
	CHECK_FALSE(pu->isUnitInQueue(s3));
	CHECK(pu->isUnitInQueue(a1));
}

TEST_CASE("LOCAL-91 BuildableHeroListUpgrade: processUpgradeRemoval removes hero and cancels factory queue (RW 0x008BC5CB, RW 0x008BC519, RW 0x0078138B)")
{
	Local91World w;
	Player *alice = w.alice();
	REQUIRE(alice != nullptr);

	Team *aliceTeam = w.teamOf("Alice");
	PlayerTemplate tpl;
	tpl.m_buildableRingHeroesMP.push_back("L91_RingHero");
	alice->init(&tpl, 2000);
	alice->setDefaultTeam(aliceTeam);
	w.logic->economy().initAllCommandPoints();

	Object *fort = w.make("L91_Fortress", aliceTeam);
	REQUIRE(fort != nullptr);
	ProductionUpdateInterface *pu = fort->getProductionUpdate();
	REQUIRE(pu != nullptr);

	const UpgradeTemplate *ringUpg = TheUpgradeCenter->findUpgrade("Upgrade_RingHero");
	const ThingTemplate *ringHero = w.logic->things().findTemplate("L91_RingHero");
	const ThingTemplate *soldier = w.logic->things().findTemplate("L91_Soldier");
	REQUIRE(ringUpg != nullptr);
	REQUIRE(ringHero != nullptr);
	REQUIRE(soldier != nullptr);

	// Granting Upgrade_RingHero adds L91_RingHero to PlayerHeroList (RW 0x8BC4C6)
	auto *mod = fort->findModule("BuildableHeroListUpgrade");
	REQUIRE(mod != nullptr);
	REQUIRE(fort->getControllingPlayer() == alice);
	REQUIRE(alice->getPlayerTemplate() == &tpl);
	fort->giveUpgrade(ringUpg);
	REQUIRE(alice->heroes().size() == 1);
	CHECK(alice->heroes().at(0)->templateName == "L91_RingHero");

	// Queue L91_RingHero and L91_Soldier on the FS_FACTORY
	const ProductionID heroProdId = pu->requestUniqueUnitID();
	const ProductionID soldierProdId = pu->requestUniqueUnitID();
	CHECK(pu->queueCreateUnit(ringHero, -1, heroProdId, 0, false, "", false));
	CHECK(pu->queueCreateUnit(soldier, -1, soldierProdId, 0, false, "", false));
	REQUIRE(pu->isUnitInQueue(heroProdId));
	REQUIRE(pu->isUnitInQueue(soldierProdId));

	// Removing Upgrade_RingHero triggers BuildableHeroListUpgrade::processUpgradeRemoval (RW 0x008BC5CB):
	// immediately erases L91_RingHero from PlayerHeroList (RW 0x0078138B) and schedules factory cancel (RW 0x008BC519)
	fort->removeUpgrade(ringUpg);
	CHECK(alice->heroes().size() == 0);
	CHECK(pu->isUnitInQueue(heroProdId)); // deferred until next ProductionUpdate::update()

	w.frames(1);
	CHECK_FALSE(pu->isUnitInQueue(heroProdId));
	CHECK(pu->isUnitInQueue(soldierProdId));
}

TEST_CASE("LOCAL-91 SupplyCenterProductionExitUpdate: getUnitCreatePoint transforms UnitCreatePoint (RW 0x008A9CBC)")
{
	Local91World w;
	Object *mill = w.make("L91_LumberMill", w.teamOf("Alice"));
	REQUIRE(mill != nullptr);

	Coord3D pos{ 100.0f, 200.0f, 10.0f };
	mill->setPosition(&pos);
	mill->setOrientation(1.57079632679f); // +90 deg around Z: (10, 20, 5) -> (-20, 10, 5)

	auto *exit = dynamic_cast<SupplyCenterProductionExitUpdate *>(mill->getObjectExitInterface());
	REQUIRE(exit != nullptr);

	// Primary vtable slot 13 (RW 0x008A9CBC): transforms UnitCreatePoint by object matrix
	Coord3D world{};
	CHECK(exit->getUnitCreatePoint(&world));
	CHECK(world.x == doctest::Approx(80.0f).epsilon(1e-3)); // tolerance: float32 cos/sin(pi/2)
	CHECK(world.y == doctest::Approx(210.0f).epsilon(1e-3)); // tolerance: float32 cos/sin(pi/2)
	CHECK(world.z == 15.0f);

	// ExitInterface slot 10 (RW 0x8655DC) stays false on SupplyCenterProductionExitUpdate
	Coord3D unusedPos{};
	float unusedAngle = 0.0f;
	CHECK_FALSE(exit->getExitPosition(&unusedPos, &unusedAngle));
}
