// OpenBFME unit tests. GPL-3.0.
// Lane LUMBER1: the Isengard / Mordor lumber mills earn money. The workers a lumber mill makes cut trees (the harvest AI state: the move to a tree, the
// preparation that fells it, the work that takes its lumber) and carry the lumber to the mill (the dock AI state and SupplyCenterDockUpdate), which pays
// ValuePerSupplyBox per box, doubled by Fuel the Fires. Retail data on a retail map ("map mp fall back 4p", its trees); the expected values are the retail
// data's (gamedata.ini ValuePerSupplyBox 10 / SupplyBoxesPerTree 90, worker.ini's Harvest* / SupplyCenterActionDelay milliseconds at 5 logic frames a
// second, LumberMill's BonusScienceMultiplier 200%) and the binary's rules (see GameLogic/AI/AIHarvest.h, AIDock.h, Module/DockUpdate.h, WorkerAIUpdate.h).

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/ResourceGatheringManager.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIDock.h"
#include "GameLogic/AI/AIHarvest.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DockUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace
{
WorkerAIUpdate *workerAI(Object *o)
{
	return o ? dynamic_cast<WorkerAIUpdate *>(o->getAIUpdateInterface()) : nullptr;
}

SupplyCenterDockUpdate *millDock(Object *mill)
{
	return mill ? dynamic_cast<SupplyCenterDockUpdate *>(mill->findModule("SupplyCenterDockUpdate")) : nullptr;
}

// the harvest machine's state of a worker in AI_HARVEST, else -1
int harvestState(Object *w)
{
	AIUpdateInterface *ai = w->getAIUpdateInterface();
	if (!ai || ai->aiStateType() != (unsigned)AI_HARVEST)
	{
		return -1;
	}
	const AIHarvestState *h = dynamic_cast<const AIHarvestState *>(ai->stateMachine().currentState());
	return h && h->harvestMachine() ? (int)h->harvestMachine()->currentStateId() : -1;
}

// the dock machine's state of a worker in AI_DOCK, else -1
int dockState(Object *w)
{
	AIUpdateInterface *ai = w->getAIUpdateInterface();
	if (!ai || ai->aiStateType() != (unsigned)AI_DOCK)
	{
		return -1;
	}
	const AIDockState *d = dynamic_cast<const AIDockState *>(ai->stateMachine().currentState());
	return d && d->dockMachine() ? (int)d->dockMachine()->currentStateId() : -1;
}

// a standing harvestable tree of the map with no other harvestable tree within `alone`, and a clear spot `away` from it for the mill
struct Grove
{
	const TerrainTrees::Record *tree = nullptr;
	Coord3D mill{};
};

Grove findGrove(hudtest::Rig &rig, float alone = 80.0f, float away = 260.0f)
{
	const std::vector<TerrainTrees::Record> &records = rig.logic().trees().records();
	for (const TerrainTrees::Record &t : records)
	{
		if (t.clientId == 0 || !t.harvestable || t.toppled)
		{
			continue;
		}
		bool lone = true;
		for (const TerrainTrees::Record &o : records)
		{
			if (&o != &t && o.clientId != 0 && o.harvestable && std::hypot(o.position.x - t.position.x, o.position.y - t.position.y) < alone)
			{
				lone = false;
				break;
			}
		}
		if (!lone || !rig.clearGround(t.position.x + 40.0f, t.position.y, 20.0f))
		{
			continue;
		}
		Coord3D at{};
		if (rig.tryFreeSpot(t.position.x + away, t.position.y, 140.0f, at))
		{
			Grove g;
			g.tree = &t;
			g.mill = at;
			return g;
		}
	}
	return Grove();
}

std::vector<Object *> ownedOf(GameLogic &logic, const char *templateName, const Player *owner)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == templateName && o->getControllingPlayer() == owner && !o->isDestroyed())
		{
			out.push_back(o);
		}
	}
	return out;
}

// the lumber mill's SpawnBehavior makes its own workers: a scene that follows one worker removes the others as they appear
void cullOtherWorkers(hudtest::Rig &rig, ObjectID keep)
{
	for (Object *o : ownedOf(rig.logic(), "MordorWorker", rig.local))
	{
		if (o->getID() != keep)
		{
			rig.logic().destroyObject(o);
		}
	}
}

// one logic frame, then the other workers removed
void frameAlone(hudtest::Rig &rig, ObjectID keep)
{
	rig.frame(1);
	cullOtherWorkers(rig, keep);
}

// the money paid for `docker`'s deliveries so far (the income log the dock's action writes, RW 0x8A470A's floating text)
int paidTo(GameLogic &logic, ObjectID docker)
{
	int sum = 0;
	for (const Economy::IncomeEvent &e : logic.economy().incomeLog())
	{
		sum += e.object == docker ? e.amount : 0;
	}
	return sum;
}
} // namespace

TEST_CASE("LUMBER1 retail: the Mordor worker's supply data and the lumber mill's dock data, as the binary parses them")
{
	if (!hudtest::haveWorld("LUMBER1 data"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	Object *worker = rig.make("MordorWorker", 1000.0f, 1000.0f);
	WorkerAIUpdate *w = workerAI(worker);
	REQUIRE(w);
	// worker.ini: MaxBoxes 1, SupplyWarehouseScanDistance 5000, HarvestTrees Yes, HarvestActivationRange 25, and the durations at 5 frames a second
	// (parseDurationUnsignedInt: ceil(ms * 0.005)): HarvestPreparationTime 5000 -> 25, HarvestActionTime 3000 -> 15, SupplyCenterActionDelay 2000 -> 10
	CHECK(w->getMaxBoxes() == 1);
	CHECK(w->isHarvestTrees());
	CHECK(w->getHarvestActivationRange() == 25.0f);
	CHECK(w->getHarvestPreparationTime() == 25u);
	CHECK(w->getHarvestActionTime() == 15u);
	CHECK(w->getWarehouseScanDistance() == 5000.0f); // a HUMAN player's (a COMPUTER player's is doubled, RW 0x8ACDEB)
	CHECK(w->getNumberBoxes() == 0);
	CHECK(w->isAvailableForSupplying());
	Object *mill = rig.make("MordorLumberMill", 1600.0f, 1000.0f);
	SupplyCenterDockUpdate *dock = millDock(mill);
	REQUIRE(dock);
	CHECK(w->getActionDelayForDock(mill) == 10u);
	CHECK(w->getActionDelayForDock(worker) == 0u);
	CHECK_FALSE(dock->isAllowPassthroughType()); // LumberMill: AllowsPassthrough = No
	CHECK(dock->approachOwners().size() == 10u); // NumberApproachPositions = -1: ten slots to start with (RW 0x99231E)
	CHECK(mill->getDockUpdateInterface() == static_cast<DockUpdateInterface *>(dock));
	CHECK(worker->getDockUpdateInterface() == nullptr);
	CHECK(rig.logic().economy().settings().valuePerSupplyBox == 10);
	CHECK(rig.logic().economy().settings().supplyBoxesPerTree == 90);
}

TEST_CASE("LUMBER1 retail: a delivery pays ValuePerSupplyBox per box; Fuel the Fires (BonusScience, 200%) doubles it; a mill registers with every player once built")
{
	if (!hudtest::haveWorld("LUMBER1 delivery"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	Object *mill = rig.make("MordorLumberMill", 1600.0f, 1000.0f);
	SupplyCenterDockUpdate *dock = millDock(mill);
	REQUIRE(dock);
	// not registered until built (SupplyCenterCreate::onBuildComplete RW 0x8BCF6C), then with every player (ownership is checked when used)
	CHECK(rig.local->resourceGatheringManager().supplyCenters().empty());
	mill->friend_onBuildComplete();
	for (int i = 0; i < rig.game->players().getPlayerCount(); ++i)
	{
		const Player *p = rig.game->players().getNthPlayer(i);
		REQUIRE(p);
		REQUIRE(p->resourceGatheringManager().supplyCenters().size() == 1u);
		CHECK(p->resourceGatheringManager().supplyCenters().front() == mill->getID());
	}
	mill->friend_onBuildComplete(); // once only (the create module's flag)
	CHECK(rig.local->resourceGatheringManager().supplyCenters().size() == 1u);
	// one box: 10 (ValuePerSupplyBox), three: 30; the skirmish's MultiPlayMoneyMult is 1.0 for every player count
	SupplyCenterDockUpdate::Delivery d1 = dock->computeDelivery(*mill, 1);
	CHECK(d1.paid);
	CHECK(d1.amount == 10);
	CHECK(d1.value == 10.0f);
	CHECK(dock->computeDelivery(*mill, 3).amount == 30);
	CHECK_FALSE(dock->computeDelivery(*mill, 0).paid); // nothing to pay
	// Fuel the Fires: LumberMill's BonusScience = SCIENCE_FueltheFires, BonusScienceMultiplier = 200%
	REQUIRE(TheScienceStore);
	const ScienceType fuel = TheScienceStore->getScienceFromInternalName("SCIENCE_FueltheFires");
	REQUIRE(fuel != SCIENCE_INVALID);
	const int economyBonus = CombatNames::modelCondition("UPGRADE_ECONOMY_BONUS");
	rig.frame(1);
	CHECK_FALSE(mill->testModelCondition(economyBonus));
	rig.local->science().grantScience(fuel);
	CHECK(dock->computeDelivery(*mill, 1).amount == 20);
	CHECK(dock->computeDelivery(*mill, 3).amount == 60);
	rig.frame(1);
	CHECK(mill->testModelCondition(economyBonus)); // SupplyCenterDockUpdate::update (RW 0x8A47C4) shows the bonus
}

TEST_CASE("LUMBER1 retail: the dock queue (RW 0x99221D / 0x991B80): approach slots in order, entering removes the docker and shifts the queue down")
{
	if (!hudtest::haveWorld("LUMBER1 dock queue"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	Object *mill = rig.make("MordorLumberMill", 1600.0f, 1000.0f);
	mill->friend_onBuildComplete();
	SupplyCenterDockUpdate *dock = millDock(mill);
	REQUIRE(dock);
	Object *a = rig.make("MordorWorker", 1400.0f, 1000.0f);
	Object *b = rig.make("MordorWorker", 1400.0f, 1100.0f);
	Object *c = rig.make("MordorWorker", 1400.0f, 900.0f);
	Coord3D pos{};
	int ia = -1, ib = -1, ic = -1;
	REQUIRE(dock->reserveApproachPosition(*a, &pos, &ia));
	REQUIRE(dock->reserveApproachPosition(*b, &pos, &ib));
	REQUIRE(dock->reserveApproachPosition(*c, &pos, &ic));
	CHECK(ia == 0);
	CHECK(ib == 1);
	CHECK(ic == 2);
	int again = -1;
	REQUIRE(dock->reserveApproachPosition(*b, &pos, &again)); // a docker keeps its slot
	CHECK(again == 1);
	dock->onApproachReached(*b);
	CHECK(dock->approachReached()[1]);
	// the dock's update makes the first reached owner the active docker
	rig.frame(1);
	CHECK(dock->activeDocker() == b->getID());
	CHECK(dock->isClearToEnter(*b));
	CHECK_FALSE(dock->isClearToEnter(*a));
	// b enters: its slot goes, the owners after it move down one (c to slot 1); the reached flags move with them
	dock->onEnterReached(*b);
	CHECK(dock->dockerInside());
	CHECK(dock->approachOwners()[0] == a->getID());
	CHECK(dock->approachOwners()[1] == c->getID());
	CHECK(dock->approachOwners()[2] == INVALID_ID);
	CHECK_FALSE(dock->approachReached()[1]);
	CHECK(b->testModelCondition(CombatNames::modelCondition("DOCKING")));
	dock->onDockReached(*b);
	CHECK(b->testModelCondition(CombatNames::modelCondition("DOCKING_ACTIVE")));
	CHECK_FALSE(b->testModelCondition(CombatNames::modelCondition("DOCKING_BEGINNING")));
	dock->onExitReached(*b);
	CHECK(dock->activeDocker() == INVALID_ID);
	CHECK_FALSE(dock->dockerInside());
	CHECK(b->testModelCondition(CombatNames::modelCondition("DOCKING_ENDING")));
	CHECK_FALSE(b->testModelCondition(CombatNames::modelCondition("DOCKING")));
	// a cancels: c moves to slot 0
	dock->cancelDock(*a);
	CHECK(dock->approachOwners()[0] == c->getID());
	CHECK(dock->approachOwners()[1] == INVALID_ID);
}

TEST_CASE("LUMBER1 retail: a worker sent harvesting fells the closest tree after HarvestPreparationTime, takes a box after HarvestActionTime and is paid "
		  "SupplyCenterActionDelay after it docks at the mill")
{
	if (!hudtest::haveWorld("LUMBER1 harvest cycle"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	rig.game->shroud().setDisplayed(false);
	const Grove grove = findGrove(rig);
	REQUIRE_MESSAGE(grove.tree, "no lone harvestable tree with a clear spot beside it on the map");
	const int treeIndex = grove.tree->index;
	const int startValue = grove.tree->value;
	std::vector<int> startValues;
	for (const TerrainTrees::Record &t : rig.logic().trees().records())
	{
		startValues.push_back(t.value);
	}
	CHECK(startValue == 90); // SupplyBoxesPerTree
	// the mill (built) and one worker by the tree; the mill's own spawns are removed so the worker is the only harvester near the tree
	Object *mill = rig.make("MordorLumberMill", grove.mill.x, grove.mill.y);
	mill->friend_onBuildComplete();
	Object *worker = rig.make("MordorWorker", grove.tree->position.x + 40.0f, grove.tree->position.y);
	const ObjectID workerID = worker->getID();
	WorkerAIUpdate *w = workerAI(worker);
	REQUIRE(w);
	frameAlone(rig, workerID);
	frameAlone(rig, workerID);
	// the lumber mill's exit sets this (RW 0x8A9FC4); here the worker is told the same
	w->setForceWantingState(true);
	const std::uint32_t money0 = rig.local->getMoney()->countMoney();
	// per frame: the harvest machine's state, the tree, the boxes, the dock machine's state, the money
	int prepareSeen = -1, toppleSeen = -1, boxSeen = -1, processSeen = -1, paidSeen = -1;
	bool wentSupply = false;
	for (int f = 0; f < 1500 && paidSeen < 0; ++f)
	{
		frameAlone(rig, workerID);
		REQUIRE(rig.logic().findObjectByID(workerID));
		const int now = (int)rig.logic().getFrame();
		wentSupply = wentSupply || w->workerState() == WorkerAIUpdate::AS_SUPPLY_TRUCK;
		if (prepareSeen < 0 && harvestState(worker) == AIHarvestState::HARVEST_PREPARE)
		{
			prepareSeen = now;
		}
		const TerrainTrees::Record &t = rig.logic().trees().records()[(size_t)treeIndex];
		if (toppleSeen < 0 && t.toppled)
		{
			toppleSeen = now;
		}
		if (boxSeen < 0 && w->getNumberBoxes() == 1)
		{
			boxSeen = now;
			CHECK(worker->testModelCondition(CombatNames::modelCondition("CARRYING")));
		}
		if (processSeen < 0 && dockState(worker) == AIDockMachine::PROCESS_DOCK)
		{
			processSeen = now;
		}
		if (paidSeen < 0 && paidTo(rig.logic(), workerID) > 0)
		{
			paidSeen = now;
		}
	}
	CHECK(wentSupply);
	REQUIRE(prepareSeen != -1);
	REQUIRE(toppleSeen != -1);
	REQUIRE(boxSeen != -1);
	REQUIRE(processSeen != -1);
	REQUIRE(paidSeen != -1);
	MESSAGE("LUMBER1 cycle: prepare " << prepareSeen << ", felled " << toppleSeen << ", box " << boxSeen << ", processing " << processSeen << ", paid " << paidSeen);
	CHECK(toppleSeen - prepareSeen == 25); // HarvestPreparationTime 5000 ms
	CHECK(boxSeen - toppleSeen == 15);     // HarvestActionTime 3000 ms (the work state starts in the frame the tree falls)
	CHECK(paidSeen - processSeen == 10);   // SupplyCenterActionDelay 2000 ms
	CHECK(paidTo(rig.logic(), workerID) == 10); // one box of ValuePerSupplyBox 10
	CHECK(rig.local->getMoney()->countMoney() == money0 + 10u);
	CHECK(w->getNumberBoxes() == 0);
	CHECK_FALSE(worker->testModelCondition(CombatNames::modelCondition("CARRYING")));
	CHECK(rig.logic().trees().records()[(size_t)treeIndex].value == startValue - 1);
	// the worker goes back for more: a second delivery from the same (felled) tree
	for (int f = 0; f < 1500 && paidTo(rig.logic(), workerID) < 20; ++f)
	{
		frameAlone(rig, workerID);
	}
	CHECK(paidTo(rig.logic(), workerID) == 20);
	// the second load comes from the harvestable tree closest to the worker when it set out again (it stood at the mill): two boxes taken in all
	int taken = 0;
	for (size_t i = 0; i < rig.logic().trees().records().size(); ++i)
	{
		const TerrainTrees::Record &t = rig.logic().trees().records()[i];
		taken += t.clientId != 0 && t.changed ? startValues[i] - t.value : 0;
	}
	CHECK(taken == 2);
	CHECK(w->supplyTruckState() != -1);
}

TEST_CASE("LUMBER1 retail: a built Mordor lumber mill's own workers go harvesting by themselves and the mill earns money")
{
	if (!hudtest::haveWorld("LUMBER1 mill workers"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	rig.game->shroud().setDisplayed(false);
	const Grove grove = findGrove(rig, 0.0f, 300.0f);
	REQUIRE(grove.tree);
	Object *mill = rig.make("MordorLumberMill", grove.mill.x, grove.mill.y);
	mill->friend_onBuildComplete();
	const std::uint32_t money0 = rig.local->getMoney()->countMoney();
	rig.frame(2);
	const std::vector<Object *> workers = ownedOf(rig.logic(), "MordorWorker", rig.local);
	// the mill's SpawnBehavior makes its workers (SpawnNumber) through the mill's exit, which sets them wanting (RW 0x8A9FC4)
	REQUIRE_FALSE(workers.empty());
	for (Object *o : workers)
	{
		WorkerAIUpdate *w = workerAI(o);
		REQUIRE(w);
		CHECK((w->isForcedIntoWantingState() || w->workerState() == WorkerAIUpdate::AS_SUPPLY_TRUCK));
	}
	for (int f = 0; f < 2500 && rig.local->getMoney()->countMoney() < money0 + 50u; ++f)
	{
		rig.frame(1);
	}
	const std::uint32_t gained = rig.local->getMoney()->countMoney() - money0;
	MESSAGE("LUMBER1 mill: " << workers.size() << " workers, +" << gained << " in " << rig.logic().getFrame() << " frames");
	CHECK(gained >= 50u); // tolerance: the workers' walks decide when the fifth delivery lands; the money comes in multiples of ValuePerSupplyBox
	CHECK(gained % 10u == 0u);
}

TEST_CASE("LUMBER1 retail: a worker given a move order leaves the harvest; the Harvest button (SPECIAL_HARVEST) sends it back to the trees")
{
	if (!hudtest::haveWorld("LUMBER1 harvest button"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	rig.game->shroud().setDisplayed(false);
	const Grove grove = findGrove(rig);
	REQUIRE(grove.tree);
	Object *mill = rig.make("MordorLumberMill", grove.mill.x, grove.mill.y);
	mill->friend_onBuildComplete();
	Object *worker = rig.make("MordorWorker", grove.tree->position.x + 40.0f, grove.tree->position.y);
	const ObjectID workerID = worker->getID();
	WorkerAIUpdate *w = workerAI(worker);
	REQUIRE(w);
	w->setForceWantingState(true);
	for (int f = 0; f < 200 && worker->getAIUpdateInterface()->aiStateType() != (unsigned)AI_HARVEST; ++f)
	{
		frameAlone(rig, workerID);
	}
	REQUIRE(worker->getAIUpdateInterface()->aiStateType() == (unsigned)AI_HARVEST);
	// a player's move: the harvest ends, the supply machine goes BUSY and the worker acts as a dozer again
	worker->getAIUpdateInterface()->aiMoveToPosition(grove.mill, CMD_FROM_PLAYER);
	frameAlone(rig, workerID);
	frameAlone(rig, workerID);
	frameAlone(rig, workerID);
	CHECK(worker->getAIUpdateInterface()->aiStateType() != (unsigned)AI_HARVEST);
	CHECK(w->workerState() == WorkerAIUpdate::AS_DOZER);
	CHECK_FALSE(worker->testModelCondition(CombatNames::modelCondition("HARVEST_PREPARATION"))); // the command cleared it (RW 0x8AE798)
	CHECK_FALSE(worker->testModelCondition(CombatNames::modelCondition("HARVEST_ACTION")));
	// the Harvest button: the worker's SpecialPowerModule of SpecialAbilityHarvest (enum SPECIAL_HARVEST) sends it to the closest tree (RW 0x897EC6)
	const SpecialPowerTemplate *harvest = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityHarvest");
	REQUIRE(harvest);
	CHECK(harvest->getSpecialPowerType() == 0x8D);
	SpecialPowerModuleInterface *button = SpecialPowerModules::findModule(*worker, harvest);
	REQUIRE(button);
	button->doSpecialPower(0);
	CHECK(worker->getAIUpdateInterface()->aiStateType() == (unsigned)AI_HARVEST);
	frameAlone(rig, workerID);
	CHECK(w->workerState() == WorkerAIUpdate::AS_SUPPLY_TRUCK);
}

TEST_CASE("LUMBER1 retail: a game saved while workers harvest and deliver resumes to the same frames (harvest and dock states, the managers, the trees)")
{
	if (!hudtest::haveWorld("LUMBER1 save resume"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	rig.game->shroud().setDisplayed(false);
	const Grove grove = findGrove(rig);
	REQUIRE(grove.tree);
	Object *mill = rig.make("MordorLumberMill", grove.mill.x, grove.mill.y);
	mill->friend_onBuildComplete();
	Object *worker = rig.make("MordorWorker", grove.tree->position.x + 40.0f, grove.tree->position.y);
	const ObjectID workerID = worker->getID();
	workerAI(worker)->setForceWantingState(true);
	// save in the middle of the preparation (the harvest machine's end frame), then again while docking
	std::vector<int> savePoints;
	for (int f = 0; f < 1500 && savePoints.size() < 2; ++f)
	{
		frameAlone(rig, workerID);
		Object *wk = rig.logic().findObjectByID(workerID);
		REQUIRE(wk);
		const bool preparing = harvestState(wk) == AIHarvestState::HARVEST_PREPARE && savePoints.empty();
		const bool docking = dockState(wk) == AIDockMachine::PROCESS_DOCK && savePoints.size() == 1;
		if (!preparing && !docking)
		{
			continue;
		}
		std::vector<std::uint8_t> snapshot;
		std::string error;
		REQUIRE_MESSAGE(rig.logic().saveSnapshot(snapshot, &error), error);
		const UnsignedInt savedFrame = rig.logic().getFrame();
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i < 60; ++i)
		{
			frameAlone(rig, workerID);
			hashes.push_back(rig.logic().computeStateHash());
		}
		REQUIRE_MESSAGE(rig.game->loadSnapshot(snapshot, &error), error);
		CHECK(rig.logic().getFrame() == savedFrame);
		for (size_t i = 0; i < hashes.size(); ++i)
		{
			frameAlone(rig, workerID);
			CHECK(rig.logic().computeStateHash() == hashes[i]);
		}
		savePoints.push_back(f);
	}
	CHECK(savePoints.size() == 2u);
}

TEST_CASE("LUMBER1: the stop lines of the lane reach the report and STOPS")
{
	CHECK(DockUpdate::stopLine().rfind("[S-5201]", 0) == 0);
	CHECK(ResourceGatheringManager::stopLine().rfind("[S-5202]", 0) == 0);
	CHECK(WorkerAIUpdate::stopLine().rfind("[S-5203]", 0) == 0);
	CHECK(harvestStopLine().rfind("[S-5200]", 0) == 0);
}
