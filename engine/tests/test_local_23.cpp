// OpenBFME unit tests (Local 23, issue #178): ProductionQueueHordeContain and ProductionUpdate slot 14.
// Expected values and behavior are proven from RotWK 2.01 game.dat:
// - slot 14 (RW 0x8A03A0, DONOR BFME2 0x0049CC15) sets entry +0x3C (hordeTarget)
// - ProductionQueueHordeContain (RW 0x658518 / 0x88459B, DONOR BFME2 0x00481343)
// - DestinationTemplate parsing (table RW 0xC5F964, parser RW 0x88494C, DONOR BFME2 0x004816F4)
// - Entry slot 29 (RW 0x884729) & slot 30 (RW 0x884691)
// - CreateTemplate (RW 0x88460B, DONOR BFME2 0x004813B3)

#include "doctest.h"
#include "ProdTestUtil.h"

#include "GameLogic/Module/ProductionQueueHordeContain.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/ObjectFilterMatch.h"

using namespace prodtest;

namespace
{
const char kLocal23Objects[] =
	"Object Soldier\n"
	"  BuildCost = 100\n"
	"  BuildTime = 10.0\n"
	"  CommandPoints = 4\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object WarShip\n"
	"  BuildCost = 200\n"
	"  BuildTime = 15.0\n"
	"  CommandPoints = 10\n"
	"  KindOf = SELECTABLE SHIP\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object TransportShip\n"
	"  BuildCost = 150\n"
	"  BuildTime = 12.0\n"
	"  CommandPoints = 8\n"
	"  KindOf = SELECTABLE SHIP\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Barracks\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = BarracksSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 5\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"  End\n"
	"End\n"
	"Object EvilPortQueue\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = BarracksSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 5\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"  End\n"
	"  Behavior = ProductionQueueHordeContain ModuleTag_PortContain\n"
	"    ContainMax = 10\n"
	"    DestinationTemplate = WarShip ANY +INFANTRY\n"
	"  End\n"
	"End\n"
	"Object EvilPortPlain\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = BarracksSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 5\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"  End\n"
	"  Behavior = ProductionQueueHordeContain ModuleTag_PortContain\n"
	"    ContainMax = 10\n"
	"  End\n"
	"End\n"
	"Object MultiTemplatePort\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Behavior = ProductionQueueHordeContain ModuleTag_PortContain\n"
	"    ContainMax = 10\n"
	"    DestinationTemplate = WarShip ANY +INFANTRY\n"
	"    DestinationTemplate = TransportShip ANY +CAVALRY\n"
	"  End\n"
	"End\n"
	"Object TestHorde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Soldier Position:X:0 Y:0 Position:X:10 Y:0\n"
	"    InitialPayload = Soldier 2\n"
	"    Slots = 2\n"
	"  End\n"
	"End\n";

const char kLocal23Commands[] =
	"CommandButton Command_BuildSoldier\n  Command = UNIT_BUILD\n  Object = Soldier\n  Options = CANCELABLE\nEnd\n"
	"CommandButton Command_BuildWarShip\n  Command = UNIT_BUILD\n  Object = WarShip\n  Options = CANCELABLE\nEnd\n"
	"CommandButton Command_BuildTransportShip\n  Command = UNIT_BUILD\n  Object = TransportShip\n  Options = CANCELABLE\nEnd\n"
	"CommandSet BarracksSet\n  1 = Command_BuildSoldier\n  2 = Command_BuildWarShip\n  3 = Command_BuildTransportShip\nEnd\n";

struct Local23Fixture : ProdWorld
{
	Object *barracks = nullptr;
	Local23Fixture()
	{
		load(kLocal23Objects);
		load(kLocal23Commands);
		barracks = make("Barracks", teamOf("Alice"));
		REQUIRE(barracks != nullptr);
	}
};
} // namespace

TEST_CASE("LOCAL-23: ProductionUpdate slot 14 setEntryHordeTarget stores horde target by productionID")
{
	Local23Fixture fx;
	ProductionUpdateInterface *pu = fx.barracks->getProductionUpdate();
	REQUIRE(pu != nullptr);

	const ThingTemplate *soldier = fx.w.things.findTemplate("Soldier");
	REQUIRE(soldier != nullptr);

	ProductionID id1 = pu->requestUniqueUnitID();
	ProductionID id2 = pu->requestUniqueUnitID();
	CHECK(id1 != id2);

	// Queue two units
	bool q1 = pu->queueCreateUnit(soldier, -1, id1, -1, false, "", false);
	bool q2 = pu->queueCreateUnit(soldier, -1, id2, -1, false, "", false);
	CHECK(q1);
	CHECK(q2);
	CHECK(pu->getProductionCount() == 2);

	const ProductionEntry *e1 = pu->firstProduction();
	REQUIRE(e1 != nullptr);
	CHECK(e1->productionID == id1);
	CHECK(e1->hordeTarget == INVALID_ID);

	const ProductionEntry *e2 = pu->nextProduction(e1);
	REQUIRE(e2 != nullptr);
	CHECK(e2->productionID == id2);
	CHECK(e2->hordeTarget == INVALID_ID);

	// Slot 14 (RW 0x8A03A0): set horde target for id1 to 12345
	pu->setEntryHordeTarget(id1, 12345);
	CHECK(e1->hordeTarget == 12345);
	CHECK(e2->hordeTarget == INVALID_ID);

	// Slot 14: set horde target for id2 to 67890
	pu->setEntryHordeTarget(id2, 67890);
	CHECK(e1->hordeTarget == 12345);
	CHECK(e2->hordeTarget == 67890);

	// Non-existent productionID does nothing
	pu->setEntryHordeTarget(999999, 11111);
	CHECK(e1->hordeTarget == 12345);
	CHECK(e2->hordeTarget == 67890);
}

TEST_CASE("LOCAL-23: ProductionQueueHordeContain parses DestinationTemplate")
{
	Local23Fixture fx;
	Object *port = fx.make("MultiTemplatePort", fx.teamOf("Alice"));
	REQUIRE(port != nullptr);

	auto *contain = dynamic_cast<ProductionQueueHordeContain *>(port->getContain());
	REQUIRE(contain != nullptr);

	const auto &templates = contain->destinationTemplates();
	REQUIRE(templates.size() == 2);

	// Entry 0: WarShip ANY +INFANTRY
	CHECK(templates[0].templateName == "WarShip");
	CHECK(templates[0].filter.rule == ObjectFilter::RULE_ANY);
	CHECK(templates[0].filter.flag == true);

	// Entry 1: TransportShip ANY +CAVALRY
	CHECK(templates[1].templateName == "TransportShip");
	CHECK(templates[1].filter.rule == ObjectFilter::RULE_ANY);
	CHECK(templates[1].filter.flag == true);
}

TEST_CASE("LOCAL-23: ProductionQueueHordeContain single infantry entering triggers CreateTemplate and queues unit")
{
	Local23Fixture fx;
	Object *port = fx.make("EvilPortQueue", fx.teamOf("Alice"));
	REQUIRE(port != nullptr);

	ProductionUpdateInterface *pu = port->getProductionUpdate();
	REQUIRE(pu != nullptr);
	CHECK(pu->getProductionCount() == 0);

	auto *contain = dynamic_cast<ProductionQueueHordeContain *>(port->getContain());
	REQUIRE(contain != nullptr);

	Object *soldier = fx.make("Soldier", fx.teamOf("Alice"));
	REQUIRE(soldier != nullptr);
	const ObjectID soldierID = soldier->getID();

	// Soldier enters port
	bool added = contain->addToContain(soldier);
	CHECK(added);

	// CreateTemplate (RW 0x88460B) should have queued WarShip with hordeTarget == soldierID
	CHECK(pu->getProductionCount() == 1);
	const ProductionEntry *entry = pu->firstProduction();
	REQUIRE(entry != nullptr);
	CHECK(entry->objectToProduce != nullptr);
	CHECK(entry->objectToProduce->getName() == "WarShip");
	CHECK(entry->hordeTarget == soldierID);
}

TEST_CASE("LOCAL-23: ProductionQueueHordeContain horde entering triggers CreateTemplate only when all members entered")
{
	Local23Fixture fx;
	Object *port = fx.make("EvilPortQueue", fx.teamOf("Alice"));
	REQUIRE(port != nullptr);

	ProductionUpdateInterface *pu = port->getProductionUpdate();
	REQUIRE(pu != nullptr);
	CHECK(pu->getProductionCount() == 0);

	auto *contain = dynamic_cast<ProductionQueueHordeContain *>(port->getContain());
	REQUIRE(contain != nullptr);

	Object *horde = fx.make("TestHorde", fx.teamOf("Alice"));
	REQUIRE(horde != nullptr);
	const ObjectID hordeID = horde->getID();

	auto *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc != nullptr);
	const auto *items = hc->getContainedItemsList();
	REQUIRE(items != nullptr);
	REQUIRE(items->size() == 2);
	auto it = items->begin();
	Object *m1 = *it++;
	Object *m2 = *it;
	REQUIRE(m1 != nullptr);
	REQUIRE(m2 != nullptr);

	// Step 1: members are released to enter garrison
	hc->releaseMemberForGarrison(m1);
	hc->releaseMemberForGarrison(m2);
	CHECK_FALSE(hc->allMembersEntered());

	// Step 2: horde object itself enters garrison (slot 29)
	bool hordeAdded = contain->addToContain(horde);
	CHECK(hordeAdded);

	// Members haven't entered yet -> nothing queued
	CHECK(pu->getProductionCount() == 0);

	// Step 3: first member enters (slot 30)
	bool m1Added = contain->addToContain(m1);
	CHECK(m1Added);
	CHECK_FALSE(hc->allMembersEntered());
	// Still 1 member outside -> nothing queued yet
	CHECK(pu->getProductionCount() == 0);

	// Step 4: second (last) member enters (slot 30)
	bool m2Added = contain->addToContain(m2);
	CHECK(m2Added);
	CHECK(hc->allMembersEntered());

	// All members entered -> CreateTemplate triggered, WarShip queued with hordeTarget = hordeID
	CHECK(pu->getProductionCount() == 1);
	const ProductionEntry *entry = pu->firstProduction();
	REQUIRE(entry != nullptr);
	CHECK(entry->objectToProduce != nullptr);
	CHECK(entry->objectToProduce->getName() == "WarShip");
	CHECK(entry->hordeTarget == hordeID);
}

TEST_CASE("LOCAL-23: ProductionQueueHordeContain empty DestinationTemplate (EvilPort) holds garrisoned units without queueing")
{
	Local23Fixture fx;
	Object *port = fx.make("EvilPortPlain", fx.teamOf("Alice"));
	REQUIRE(port != nullptr);

	ProductionUpdateInterface *pu = port->getProductionUpdate();
	REQUIRE(pu != nullptr);
	CHECK(pu->getProductionCount() == 0);

	auto *contain = dynamic_cast<ProductionQueueHordeContain *>(port->getContain());
	REQUIRE(contain != nullptr);
	CHECK(contain->destinationTemplates().empty());

	Object *soldier = fx.make("Soldier", fx.teamOf("Alice"));
	REQUIRE(soldier != nullptr);

	// Soldier enters EvilPort
	bool added = contain->addToContain(soldier);
	CHECK(added);

	// No DestinationTemplate -> nothing queued, soldier remains in contain
	CHECK(pu->getProductionCount() == 0);
	CHECK(contain->getContainedItemsList() != nullptr);
	CHECK(contain->getContainedItemsList()->size() == 1);
	CHECK(contain->getContainedItemsList()->front() == soldier);
}
