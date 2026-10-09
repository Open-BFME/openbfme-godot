// OpenBFME unit tests (lane PROD-1): ProductionUpdate and QueueProductionExitUpdate on a synthetic barracks. Expected values come from the RotWK
// disassembly (caveat S-001): build time in frames (RW 0x73C39E: five times whole seconds), the float progress counter (RW 0x8A1EBC), the door and
// construction complete states (RW 0x8A0B2F), the exit gating (RW 0x8A38A9 / 0x8A3AA2), the refund of the stored cost (RW 0x8A13EE).

#include "doctest.h"
#include "ProdTestUtil.h"

#include "GameLogic/GameLogicDispatch.h"

#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

using namespace prodtest;

namespace prodtest
{
const char kBarracksObjects[] =
	"Object Soldier\n"
	"  BuildCost = 100\n"
	"  BuildTime = 10.0\n"
	"  CommandPoints = 4\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Archer\n"
	"  BuildCost = 150\n"
	"  BuildTime = 7.5\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Barracks\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = BarracksSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    NumDoorAnimations = 1\n"
	"    DoorOpeningTime = 600\n"
	"    DoorWaitOpenTime = 600\n"
	"    DoorCloseTime = 400\n"
	"    ConstructionCompleteDuration = 1000\n"
	"    MaxQueueEntries = 3\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"    ExitDelay = 400\n"
	"  End\n"
	"End\n";
const char kBarracksCommands[] =
	"CommandButton Command_BuildSoldier\n  Command = UNIT_BUILD\n  Object = Soldier\n  Options = CANCELABLE\nEnd\n"
	"CommandButton Command_BuildArcher\n  Command = UNIT_BUILD\n  Object = Archer\n  Options = NEED_UPGRADE CANCELABLE\n  NeededUpgrade = Upgrade_Barracks2\nEnd\n"
	"CommandSet BarracksSet\n  1 = Command_BuildSoldier\n  2 = Command_BuildArcher\nEnd\n";
} // namespace prodtest

namespace
{
struct BarracksFx : ProdWorld
{
	Object *barracks = nullptr;
	BarracksFx()
	{
		load(kBarracksObjects);
		load(kBarracksCommands);
		barracks = make("Barracks", teamOf("Alice"));
		REQUIRE(barracks != nullptr);
		barracks->setPosition(&kPos);
	}
	static Coord3D kPos;
	Object *soldiers() { return nullptr; }
	size_t count(const char *name)
	{
		size_t n = 0;
		for (const Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate()->getName() == name ? 1 : 0;
		}
		return n;
	}
	Object *first(const char *name)
	{
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	}
};
Coord3D BarracksFx::kPos = { 1000.0f, 2000.0f, 0.0f };
} // namespace

TEST_CASE("production: a queued soldier is paid at once, built in five frames per whole second and exits at the create point")
{
	BarracksFx f;
	ProductionUpdateInterface *pu = f.barracks->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	const ThingTemplate *soldier = f.w.get("Soldier");
	REQUIRE(BuildAssistant::canMakeUnit(*f.barracks, soldier, -1) == CANMAKE_OK);
	CHECK(BuildAssistant::calcTimeToBuild(*soldier, f.alice(), f.barracks, -1, f.logic->productionSettings(), *f.logic) == 50);
	CHECK(BuildAssistant::calcTimeToBuild(*f.w.get("Archer"), f.alice(), f.barracks, -1, f.logic->productionSettings(), *f.logic) == 35); // 7.5 s -> 7 s
	REQUIRE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	CHECK(f.alice()->getMoney()->countMoney() == money - 100);
	CHECK(pu->getProductionCount() == 1);
	const UnsignedInt queued = f.logic->getFrame();
	UnsignedInt madeAt = 0, completeAt = 0;
	for (int i = 0; i < 80 && !madeAt; ++i)
	{
		f.frames(1);
		if (!completeAt && pu->firstProduction() && pu->firstProduction()->percentComplete >= 100.0f)
		{
			completeAt = f.logic->getFrame();
		}
		if (f.count("Soldier") == 1)
		{
			madeAt = f.logic->getFrame();
		}
	}
	REQUIRE(madeAt != 0);
	// BuildTime 10 s = 50 frames: the 50th update brings the progress to 100 percent (RW 0x8A1EBC); the door then opens (DoorOpeningTime 600 ms = 3 frames,
	// compared with `>`: four frames) and only a door that waits open lets the object be made (RW 0x8A2457)
	CHECK(completeAt - queued == 50);
	CHECK(madeAt - queued == 54);
	Object *s = f.first("Soldier");
	REQUIRE(s != nullptr);
	CHECK(s->getTeam() == f.teamOf("Alice"));
	CHECK(s->getProducerID() == f.barracks->getID());
	CHECK(pu->getProductionCount() == 0);
	// exit position: UnitCreatePoint (0, -20) in the barracks' frame (orientation 0): (1000, 1980)
	CHECK(s->getPosition()->x == doctest::Approx(1000.0f));
	CHECK(s->getPosition()->y == doctest::Approx(1980.0f));
	// the AI was told to follow the exit path that ends at the natural rally point (30, -50) pushed 20 further out along its direction
	const std::vector<AICommand> &cmds = f.logic->aiCommands().issued();
	REQUIRE(cmds.size() == 1);
	CHECK(cmds[0].type == AICMD_FOLLOW_EXIT_PRODUCTION_PATH);
	CHECK(cmds[0].object == s->getID());
	REQUIRE(cmds[0].path.size() == 1);
	const float len = std::sqrt(30.0f * 30.0f + 50.0f * 50.0f);
	CHECK(cmds[0].path[0].x == doctest::Approx(1000.0f + 30.0f + 30.0f / len * 20.0f).epsilon(1e-4));
	CHECK(cmds[0].path[0].y == doctest::Approx(2000.0f + -50.0f + -50.0f / len * 20.0f).epsilon(1e-4));
}

TEST_CASE("production: door and construction complete model conditions follow the retail state machine")
{
	BarracksFx f;
	ProductionUpdateInterface *pu = f.barracks->getProductionUpdate();
	const ThingTemplate *soldier = f.w.get("Soldier");
	REQUIRE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	auto bit = [](const char *n) {
		for (int i = 0; TheModelConditionNames[i]; ++i)
		{
			if (std::string(TheModelConditionNames[i]) == n)
			{
				return i;
			}
		}
		return -1;
	};
	const UnsignedInt queued = f.logic->getFrame();
	std::vector<std::string> timeline;
	std::string last;
	for (int i = 0; i < 80; ++i)
	{
		f.frames(1);
		std::string now;
		for (const char *n : { "DOOR_1_OPENING", "DOOR_1_WAITING_OPEN", "DOOR_1_CLOSING", "CONSTRUCTION_COMPLETE" })
		{
			if (f.barracks->testModelCondition(bit(n)))
			{
				now += std::string(now.empty() ? "" : "+") + n;
			}
		}
		if (now != last)
		{
			timeline.push_back(std::to_string(f.logic->getFrame() - queued) + ":" + (now.empty() ? "-" : now));
			last = now;
		}
	}
	// the progress completes at +50 and the door state is set in that update; the model condition flags are applied by the NEXT update (RW 0x8A1D9B: the
	// dirty flush comes before the queue is looked at): OPENING and CONSTRUCTION_COMPLETE show at +51. DoorOpeningTime 600 ms = 3 frames, compared with
	// `>`: WAITING_OPEN from +54 (4 frames: DoorWaitOpenTime 600 ms), CLOSING from +58 (DoorCloseTime 400 ms = 2 frames, `>`: 3 frames), closed at +61;
	// CONSTRUCTION_COMPLETE (1000 ms = 5 frames, `>`) clears at +56
	INFO("timeline: " << [&] { std::string t; for (auto &x : timeline) { t += x + " "; } return t; }());
	REQUIRE(timeline.size() == 5);
	CHECK(timeline[0] == "51:DOOR_1_OPENING+CONSTRUCTION_COMPLETE");
	CHECK(timeline[1] == "54:DOOR_1_WAITING_OPEN+CONSTRUCTION_COMPLETE");
	CHECK(timeline[2] == "56:DOOR_1_WAITING_OPEN");
	CHECK(timeline[3] == "58:DOOR_1_CLOSING");
	CHECK(timeline[4] == "61:-");
}

namespace
{
GameMessage queueMsg(int player, const ThingTemplate *t, bool batch = false)
{
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)t->getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(batch);
	m.appendBooleanArgument(false);
	return m;
}
GameMessage selectMsg(int player, Object *o)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(o->getID());
	return m;
}
} // namespace

TEST_CASE("production: the queue command works through the selection, refuses what the command set does not offer and cancels with a full refund")
{
	BarracksFx f;
	CommandList list;
	GameLogicDispatch dispatch(*f.logic);
	dispatch.attach(list);
	const ThingTemplate *soldier = f.w.get("Soldier");
	const ThingTemplate *archer = f.w.get("Archer");
	const int alice = f.alice()->getPlayerIndex();
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	ProductionUpdateInterface *pu = f.barracks->getProductionUpdate();
	// no selection: nothing is queued
	list.append(queueMsg(alice, soldier));
	f.frames(1);
	CHECK(pu->getProductionCount() == 0);
	// a selected barracks: a soldier is queued and paid; the archer needs Upgrade_Barracks2 (NeededUpgrade of its button): refused
	list.append(selectMsg(alice, f.barracks));
	list.append(queueMsg(alice, soldier));
	list.append(queueMsg(alice, archer));
	f.frames(1);
	CHECK(pu->getProductionCount() == 1);
	CHECK(f.alice()->getMoney()->countMoney() == money - 100);
	// the upgrade (an OBJECT upgrade of the building) makes the archer's button available
	f.barracks->giveUpgrade("Upgrade_Barracks2");
	list.append(queueMsg(alice, archer));
	f.frames(1);
	CHECK(pu->getProductionCount() == 2);
	CHECK(f.alice()->getMoney()->countMoney() == money - 100 - 150);
	// cancel by type: the LAST matching entry goes and the stored cost comes back (RW 0x8A13EE)
	GameMessage cancel(MSG_CANCEL_UNIT_CREATE, alice);
	cancel.appendBooleanArgument(false);
	cancel.appendIntegerArgument((int)archer->getTemplateID());
	cancel.appendBooleanArgument(false);
	list.append(cancel);
	f.frames(1);
	CHECK(pu->getProductionCount() == 1);
	CHECK(f.alice()->getMoney()->countMoney() == money - 100);
	CHECK(dispatch.errors().empty());
}

TEST_CASE("production: a full queue, too little money and too few command points refuse the command (RW 0x793ECB)")
{
	BarracksFx f;
	ProductionUpdateInterface *pu = f.barracks->getProductionUpdate();
	const ThingTemplate *soldier = f.w.get("Soldier");
	// money: the player can afford exactly one soldier
	f.alice()->getMoney()->withdraw(f.alice()->getMoney()->countMoney() - 150);
	REQUIRE(BuildAssistant::canMakeUnit(*f.barracks, soldier, -1) == CANMAKE_OK);
	REQUIRE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	CHECK(f.alice()->getMoney()->countMoney() == 50);
	CHECK(BuildAssistant::canMakeUnit(*f.barracks, soldier, -1) == CANMAKE_NO_MONEY);
	CHECK_FALSE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	CHECK(pu->getProductionCount() == 1);
	// a refund brings the money back; the queue holds MaxQueueEntries (3) at most
	f.alice()->getMoney()->deposit(1000);
	REQUIRE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	REQUIRE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	CHECK(pu->getProductionCount() == 3);
	CHECK(BuildAssistant::canMakeUnit(*f.barracks, soldier, -1) == CANMAKE_QUEUE_FULL);
	CHECK_FALSE(pu->queueCreateUnit(soldier, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	// command points: usage + CommandPoints (4) must not pass the limit (RW 0x6A7F79)
	setUsage(*f.alice(), f.economy().commandPointLimit(*f.alice()) - 3);
	pu->cancelAllProduction();
	CHECK(pu->getProductionCount() == 0);
	CHECK(BuildAssistant::canMakeUnit(*f.barracks, soldier, -1) == CANMAKE_UNKNOWN_7);
	setUsage(*f.alice(), f.economy().commandPointLimit(*f.alice()) - 4);
	CHECK(BuildAssistant::canMakeUnit(*f.barracks, soldier, -1) == CANMAKE_OK);
}

TEST_CASE("production: the command point rule is the economy's (one model): the limit includes the upgrade records and the live accounting counts a unit once")
{
	BarracksFx f;
	const ThingTemplate *soldier = f.w.get("Soldier"); // CommandPoints 4
	Player &alice = *f.alice();
	const int limit = f.economy().commandPointLimit(alice);
	CHECK(limit == 100 + alice.commandPoints().getBonus()); // MP2: base 100
	setUsage(alice, limit - 3);
	CHECK_FALSE(f.economy().canAffordCommandPoints(alice, *soldier));
	CHECK(BuildAssistant::commandPointsAvailable(alice, *soldier, *f.logic) == f.economy().canAffordCommandPoints(alice, *soldier));
	// a CommandPointsUpgrade record (RW 0x6A81E3, ported by the economy, absent from production's earlier copy of the model) raises the limit by its value
	alice.commandPoints().addRecord(1, 77, nullptr);
	CHECK(f.economy().commandPointLimit(alice) == limit + 1);
	CHECK(BuildAssistant::commandPointsAvailable(alice, *soldier, *f.logic));
	// a produced unit is counted exactly once: usage rises by its CommandPoints, not twice (no second hook on the team)
	setUsage(alice, 0);
	Object *s1 = f.make("Soldier", f.teamOf("Alice"));
	CHECK(alice.commandPoints().getUsage() == 4);
	f.logic->destroyObject(s1);
	f.frames(1);
	CHECK(alice.commandPoints().getUsage() == 0);
}

TEST_CASE("production: a rally point set by command makes the exit path end at it")
{
	BarracksFx f;
	CommandList list;
	GameLogicDispatch dispatch(*f.logic);
	dispatch.attach(list);
	const ThingTemplate *soldier = f.w.get("Soldier");
	const int alice = f.alice()->getPlayerIndex();
	GameMessage rally(MSG_SET_RALLY_POINT, alice);
	rally.appendObjectIDArgument(f.barracks->getID());
	Coord3D at = { 1500.0f, 2500.0f, 0.0f };
	rally.appendLocationArgument(at);
	rally.appendBooleanArgument(false);
	rally.appendObjectIDArgument(INVALID_ID);
	list.append(rally);
	list.append(selectMsg(alice, f.barracks));
	list.append(queueMsg(alice, soldier));
	f.frames(70);
	REQUIRE(f.count("Soldier") == 1);
	const std::vector<AICommand> &cmds = f.logic->aiCommands().issued();
	REQUIRE(cmds.size() == 1);
	REQUIRE(cmds[0].path.size() == 2);                     // the natural rally point, then the player's rally point (RW 0x8A41B1)
	CHECK(cmds[0].path[1].x == 1500.0f);
	CHECK(cmds[0].path[1].y == 2500.0f);
	CHECK(f.logic->aiCommands().unexecuted() == 1);       // nobody runs the AI yet (stop S-201): reported, not hidden
}

#include "GameLogic/ProductionStops.h"

#include <fstream>
#include <sstream>

namespace
{
const char kOtherExits[] =
	"Object Worker\n"
	"  BuildCost = 50\n"
	"  BuildTime = 4.0\n"
	"  KindOf = SELECTABLE\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object LumberMill\n"
	"  KindOf = STRUCTURE\n"
	"  CommandSet = MillSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"  End\n"
	"  Behavior = SupplyCenterProductionExitUpdate ModuleTag_E\n"
	"    UnitCreatePoint = X:10.0 Y:-30.0 Z:0.0\n"
	"    NaturalRallyPoint = X:0.0 Y:-60.0 Z:0.0\n"
	"  End\n"
	"End\n"
	"Object Tree\n"
	"  KindOf = STRUCTURE\n"
	"  CommandSet = MillSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"  End\n"
	"  Behavior = SpawnPointProductionExitUpdate ModuleTag_E\n"
	"    SpawnPointBoneName = SPAWN\n"
	"  End\n"
	"End\n"
	"CommandButton Command_BuildWorker\n  Command = UNIT_BUILD\n  Object = Worker\n  Options = CANCELABLE\nEnd\n"
	"CommandSet MillSet\n  1 = Command_BuildWorker\nEnd\n";
} // namespace

TEST_CASE("production: a SupplyCenter producer places the worker by its create point and hands it the natural rally point (RW 0x8A9DBA)")
{
	ProdWorld f;
	f.load(kOtherExits);
	Object *mill = f.make("LumberMill", f.teamOf("Alice"));
	Coord3D at = { 300.0f, 400.0f, 0.0f };
	mill->setPosition(&at);
	ProductionUpdateInterface *pu = mill->getProductionUpdate();
	const ThingTemplate *worker = f.w.get("Worker");
	REQUIRE(pu->queueCreateUnit(worker, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	f.frames(30);
	Object *w = nullptr;
	for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		w = o->getTemplate()->getName() == "Worker" ? o : w;
	}
	REQUIRE(w != nullptr);
	CHECK(w->getPosition()->x == doctest::Approx(310.0f));
	CHECK(w->getPosition()->y == doctest::Approx(370.0f));
	const std::vector<AICommand> &c = f.logic->aiCommands().issued();
	REQUIRE(c.size() == 1);
	CHECK(c[0].type == AICMD_FOLLOW_EXIT_PRODUCTION_PATH);
	CHECK(c[0].target == mill->getID()); // the producer is the source object of this exit (RW 0x8A9E30), unlike the queue exit
	REQUIRE(c[0].path.size() == 1);      // no player rally point: the natural one only, without the 20 unit push
	CHECK(c[0].path[0].x == doctest::Approx(300.0f));
	CHECK(c[0].path[0].y == doctest::Approx(340.0f));
}

TEST_CASE("production: a SpawnPoint producer is paid, completes and reports its stop (S-209)")
{
	ProdWorld f;
	f.load(kOtherExits);
	Object *tree = f.make("Tree", f.teamOf("Alice"));
	ProductionUpdateInterface *pu = tree->getProductionUpdate();
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	REQUIRE(pu->queueCreateUnit(f.w.get("Worker"), -1, pu->requestUniqueUnitID(), -1, false, "", false));
	CHECK(f.alice()->getMoney()->countMoney() == money - 50);
	f.frames(40);
	CHECK(pu->getProductionCount() == 1); // complete, waiting for a door that never frees
	CHECK(pu->firstProduction()->percentComplete >= 100.0f);
	size_t made = 0;
	for (const Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		made += o->getTemplate()->getName() == "Worker" ? 1u : 0u;
	}
	CHECK(made == 0);
	bool reported = false;
	for (const std::string &e : f.logic->report().errors)
	{
		reported = reported || e.find("SpawnPointProductionExitUpdate") != std::string::npos;
	}
	CHECK(reported);
}

TEST_CASE("production: the lane's stops are reported and have a row in docs/STOPS.md")
{
	const std::vector<std::string> lines = ProductionStops::lines();
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string docs = ss.str();
	for (const char *id : { "S-201", "S-202", "S-203", "S-204", "S-205", "S-206", "S-207", "S-209" })
	{
		bool reported = false;
		for (const std::string &l : lines)
		{
			reported = reported || l.rfind(std::string("[") + id + "]", 0) == 0;
		}
		CHECK_MESSAGE(reported, id << " is not reported");
		CHECK_MESSAGE(docs.find(std::string("| ") + id + " |") != std::string::npos, id << " has no row in docs/STOPS.md");
	}
	// S-208 comes with the dispatcher's counts
	ProdWorld f;
	GameLogicDispatch dispatch(*f.logic);
	dispatch.dispatch(GameMessage(MSG_DO_MOVETO, 1));
	CHECK(dispatch.unhandled().at(MSG_DO_MOVETO) == 1);
	CHECK(dispatch.acceptanceStops()[0].find("MSG_DO_MOVETO x1") != std::string::npos);
	CHECK(docs.find("| S-208 |") != std::string::npos);
}

namespace
{
// one scripted production run (queue, rally, a cancel, a second batch); the state hash of every logic frame
std::vector<std::uint32_t> scriptedRunHashes()
{
	BarracksFx f;
	CommandList list;
	GameLogicDispatch dispatch(*f.logic);
	dispatch.attach(list);
	const ThingTemplate *soldier = f.w.get("Soldier");
	const int alice = f.alice()->getPlayerIndex();
	std::vector<std::uint32_t> hashes;
	for (int frame = 0; frame < 140; ++frame)
	{
		if (frame == 1)
		{
			list.append(selectMsg(alice, f.barracks));
			list.append(queueMsg(alice, soldier));
			list.append(queueMsg(alice, soldier));
		}
		if (frame == 3)
		{
			GameMessage rally(MSG_SET_RALLY_POINT, alice);
			rally.appendObjectIDArgument(f.barracks->getID());
			rally.appendLocationArgument(Coord3D{ 1100.0f, 2100.0f, 0.0f });
			rally.appendBooleanArgument(false);
			rally.appendObjectIDArgument(INVALID_ID);
			list.append(rally);
		}
		if (frame == 20)
		{
			GameMessage cancel(MSG_CANCEL_UNIT_CREATE, alice);
			cancel.appendBooleanArgument(false);
			cancel.appendIntegerArgument((int)soldier->getTemplateID());
			cancel.appendBooleanArgument(false);
			list.append(cancel);
		}
		if (frame == 60)
		{
			list.append(queueMsg(alice, soldier));
		}
		f.frames(1);
		hashes.push_back(f.logic->computeStateHash());
	}
	return hashes;
}
} // namespace

TEST_CASE("production: two identical scripted runs give the same state hash on every frame, and production changes the hash")
{
	const std::vector<std::uint32_t> a = scriptedRunHashes();
	const std::vector<std::uint32_t> b = scriptedRunHashes();
	REQUIRE(a.size() == b.size());
	size_t differs = 0, changes = 0;
	for (size_t i = 0; i < a.size(); ++i)
	{
		differs += a[i] != b[i] ? 1 : 0;
		changes += i > 0 && a[i] != a[i - 1] ? 1 : 0;
	}
	CHECK(differs == 0);
	CHECK(changes > 10); // the progress counters, the money and the new objects all move the hash
}

TEST_CASE("production: a ProductionModifier applies only to templates its ModifierFilter allows, and only with its RequiredUpgrade (RW 0x8A093D over RW 0x763543)")
{
	ProdWorld w;
	w.load(kBarracksObjects);
	w.load(
		"Object Grunt\n  BuildCost = 100\n  BuildTime = 10.0\n  KindOf = SELECTABLE INFANTRY\nEnd\n"
		"Object Zealot\n  BuildCost = 150\n  BuildTime = 10.0\n  KindOf = SELECTABLE INFANTRY\nEnd\n"
		"Object ModBarracks\n"
		"  KindOf = STRUCTURE SELECTABLE\n"
		"  Behavior = ProductionUpdate ModuleTag_PU\n"
		"    ProductionModifier\n"
		"      ModifierFilter = NONE +Zealot\n"
		"      CostMultiplier = 0.5\n"
		"      TimeMultiplier = 0.5\n"
		"    End\n"
		"    ProductionModifier\n"
		"      ModifierFilter = ALL -Zealot\n"
		"      CostMultiplier = 0.8\n"
		"    End\n"
		"    ProductionModifier\n"
		"      RequiredUpgrade = Upgrade_Barracks2\n"
		"      ModifierFilter = NONE +INFANTRY\n"
		"      TimeMultiplier = 0.9\n"
		"    End\n"
		"  End\n"
		"End\n");
	Object *b = w.make("ModBarracks", w.teamOf("Alice"));
	REQUIRE(b != nullptr);
	ProductionUpdateInterface *pu = b->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const ThingTemplate *soldier = w.w.get("Grunt"), *archer = w.w.get("Zealot");
	// the Zealots-only discount does not touch Grunts; the `ALL -Zealot` one does not touch Zealots
	CHECK(pu->productionCostMultiplier(archer) == doctest::Approx(0.5f));
	CHECK(pu->productionCostMultiplier(soldier) == doctest::Approx(0.8f));
	CHECK(pu->productionTimeMultiplier(archer) == doctest::Approx(0.5f));
	CHECK(pu->productionTimeMultiplier(soldier) == doctest::Approx(1.0f));
	// the upgrade-gated KindOf entry needs the object's upgrade, then applies to both infantry templates
	b->giveUpgrade("Upgrade_Barracks2");
	CHECK(pu->productionTimeMultiplier(soldier) == doctest::Approx(0.9f));
	CHECK(pu->productionTimeMultiplier(archer) == doctest::Approx(0.45f));
	// the cost seen by a purchase (cost 100 soldier x 0.8, 150 archer x 0.5)
	CHECK(BuildAssistant::calcCostToBuild(*soldier, w.alice(), b, -1) == 80);
	CHECK(BuildAssistant::calcCostToBuild(*archer, w.alice(), b, -1) == 75);
}

// ECON-1 review: a produced object remembers what the queue entry cost (RW 0x8A276C cvtsi2ss [entry + 0x28]; 0x8A2774 movss [object + 0x33C]) and RefundDie pays back
// from it. Derived by hand: Grunt BuildCost 100, RefundPercent 50%; the plain barracks sells it at 100 (refund 50), the discounted barracks (ProductionModifier
// CostMultiplier 0.8 for every template) at 80 (refund ceil(80 * 0.5) = 40). A cancelled entry refunds the stored cost (RW 0x8A13EE), and the unit bought at the
// discount keeps paid = 80 even after the producer is gone.
TEST_CASE("production: a produced unit remembers the cost its entry was paid at (RW 0x8A2774) and RefundDie pays back from it, discounted or not, to its death")
{
	ProdWorld w;
	w.load(
		"Object Grunt\n"
		"  BuildCost = 100\n"
		"  BuildTime = 1.0\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n"
		"  Behavior = RefundDie ModuleTag_Refund\n"
		"    RefundPercent = 50%\n"
		"  End\n"
		"End\n"
		"Object PlainBarracks\n"
		"  KindOf = STRUCTURE SELECTABLE\n"
		"  CommandSet = GruntSet\n"
		"  Behavior = ProductionUpdate ModuleTag_PU\n"
		"    MaxQueueEntries = 3\n"
		"  End\n"
		"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
		"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
		"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
		"    ExitDelay = 400\n"
		"  End\n"
		"End\n"
		"Object DiscountBarracks\n"
		"  KindOf = STRUCTURE SELECTABLE\n"
		"  CommandSet = GruntSet\n"
		"  Behavior = ProductionUpdate ModuleTag_PU\n"
		"    MaxQueueEntries = 3\n"
		"    ProductionModifier\n"
		"      ModifierFilter = ALL\n"
		"      CostMultiplier = 0.8\n"
		"    End\n"
		"  End\n"
		"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
		"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
		"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
		"    ExitDelay = 400\n"
		"  End\n"
		"End\n");
	w.load("CommandButton Command_BuildGrunt\n  Command = UNIT_BUILD\n  Object = Grunt\n  Options = CANCELABLE\nEnd\nCommandSet GruntSet\n  1 = Command_BuildGrunt\nEnd\n");
	Player *alice = w.alice();
	const ThingTemplate *grunt = w.w.get("Grunt");
	auto buyAndWait = [&](const char *barracksName, int expectedPrice) -> Object * {
		Object *b = w.make(barracksName, w.teamOf("Alice"));
		REQUIRE(b != nullptr);
		Coord3D pos{ 300.0f, 300.0f, 0.0f };
		b->setPosition(&pos);
		ProductionUpdateInterface *pu = b->getProductionUpdate();
		REQUIRE(pu != nullptr);
		const std::uint32_t before = alice->getMoney()->countMoney();
		REQUIRE(pu->queueCreateUnit(grunt, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
		CHECK(before - alice->getMoney()->countMoney() == (std::uint32_t)expectedPrice);
		size_t gruntsBefore = 0;
		for (const Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			gruntsBefore += o->getTemplate()->getName() == "Grunt" ? 1 : 0;
		}
		for (int i = 0; i < 120; ++i)
		{
			w.frames(1);
		}
		Object *made = nullptr;
		size_t grunts = 0;
		for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "Grunt")
			{
				++grunts;
				made = o;
			}
		}
		REQUIRE(grunts == gruntsBefore + 1);
		return made;
	};
	// the plain purchase: paid 100, dies, 50 comes back
	Object *plain = buyAndWait("PlainBarracks", 100);
	CHECK(plain->getBuildCostPaid() == 100.0f);
	std::uint32_t cash = alice->getMoney()->countMoney();
	plain->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->getMoney()->countMoney() == cash + 50);
	// the discounted purchase: 100 * 0.8 = 80 paid, the object remembers 80, its death returns ceil(80 * 0.5) = 40
	Object *discounted = buyAndWait("DiscountBarracks", 80);
	CHECK(discounted->getBuildCostPaid() == 80.0f);
	cash = alice->getMoney()->countMoney();
	discounted->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->getMoney()->countMoney() == cash + 40);
	// the stored paid cost is hashed (an object that paid differently is a different state)
	Object *free = w.make("Grunt", w.teamOf("Alice"));
	const std::uint32_t h0 = w.logic->computeStateHash();
	free->setBuildCostPaid(1.0f);
	CHECK(w.logic->computeStateHash() != h0);
}

TEST_CASE("production: only KindOf ARMY_OF_DEAD (bit 151, RW 0x6A7F99) passes a full command point limit; ONE_RING does not")
{
	ProdWorld w;
	w.load(
		"Object Summoned\n  CommandPoints = 4\n  KindOf = SELECTABLE INFANTRY ARMY_OF_DEAD\nEnd\n"
		"Object RingBearer\n  CommandPoints = 4\n  KindOf = SELECTABLE INFANTRY ONE_RING\nEnd\n"
		"Object Plain\n  CommandPoints = 4\n  KindOf = SELECTABLE INFANTRY\nEnd\n");
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("ARMY_OF_DEAD") == 151);
	setUsage(*w.alice(), w.economy().commandPointLimit(*w.alice())); // full
	CHECK(BuildAssistant::commandPointsAvailable(*w.alice(), *w.w.get("Summoned"), *w.logic));
	CHECK_FALSE(BuildAssistant::commandPointsAvailable(*w.alice(), *w.w.get("RingBearer"), *w.logic));
	CHECK_FALSE(BuildAssistant::commandPointsAvailable(*w.alice(), *w.w.get("Plain"), *w.logic));
}

TEST_CASE("production: holding a CLOSING door does not restart opening (RW 0x8A0DB1 .. 0x8A0DB9 need all three timestamps zero)")
{
	BarracksFx f;
	ProductionUpdateInterface *pu = f.barracks->getProductionUpdate();
	REQUIRE(pu->queueCreateUnit(f.w.get("Soldier"), -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	// run until the door is closing (opened and wait-open timestamps clear, closed set)
	const ProductionUpdate *prod = dynamic_cast<const ProductionUpdate *>(pu);
	REQUIRE(prod != nullptr);
	bool closing = false;
	for (int i = 0; i < 100 && !closing; ++i)
	{
		f.frames(1);
		closing = prod->door(0).closedFrame != 0;
	}
	REQUIRE(closing);
	const ProductionUpdate::DoorInfo before = prod->door(0);
	pu->setHoldDoorOpen(0, true);
	CHECK(prod->door(0).openedFrame == 0); // not reopened
	CHECK(prod->door(0).closedFrame == before.closedFrame);
	CHECK(prod->door(0).holdOpen);
	// a closed, idle door does open when held
	pu->setHoldDoorOpen(0, false);
	f.frames(20);
	REQUIRE(prod->door(0).closedFrame == 0);
	REQUIRE(prod->door(0).openedFrame == 0);
	pu->setHoldDoorOpen(0, true);
	CHECK(prod->door(0).openedFrame != 0);
}

// lane HERO-1 regression (PROD-1's counter): the post-exit delay (module + 0x118, BuildFadeInOnCreateTime * 5 frames) counts down inside the completion (RW 0x8A2D75 ->
// 0x8A2EEE: `if (delay) { --delay; return; }`), the counter decremented at the top of the update (RW 0x8A1D60) is the hero countdown (+ 0x128). A 1 s fade
// (5 frames) is counted by completion passes (the first one in the pass that makes the unit), so three frames with the producer SOLD (the update returns
// before the completion, RW 0x8A1D9B) push the exit back by three; the old top-of-update decrement kept counting through them.
TEST_CASE("production: the post-exit fade delay counts down in the completion pass, not at the top of the update (RW 0x8A2EEE)")
{
	ProdWorld w;
	w.load(
		"Object FadeGrunt\n"
		"  BuildCost = 100\n"
		"  BuildTime = 1.0\n"
		"  BuildFadeInOnCreateTime = 1.0\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n"
		"End\n"
		"Object FadeBarracks\n"
		"  KindOf = STRUCTURE SELECTABLE\n"
		"  CommandSet = FadeSet\n"
		"  Behavior = ProductionUpdate ModuleTag_PU\n"
		"    MaxQueueEntries = 3\n"
		"  End\n"
		"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
		"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
		"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
		"    ExitDelay = 400\n"
		"  End\n"
		"End\n");
	w.load("CommandButton Command_BuildFadeGrunt\n  Command = UNIT_BUILD\n  Object = FadeGrunt\n  Options = CANCELABLE\nEnd\nCommandSet FadeSet\n  1 = Command_BuildFadeGrunt\nEnd\n");
	Object *b = w.make("FadeBarracks", w.teamOf("Alice"));
	REQUIRE(b != nullptr);
	Coord3D pos{ 300.0f, 300.0f, 0.0f };
	b->setPosition(&pos);
	ProductionUpdate *pu = dynamic_cast<ProductionUpdate *>(b->getProductionUpdate());
	REQUIRE(pu != nullptr);
	REQUIRE(pu->queueCreateUnit(w.w.get("FadeGrunt"), -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	UnsignedInt madeAt = 0, exitedAt = 0;
	for (int i = 0; i < 60 && !exitedAt; ++i)
	{
		w.frames(1);
		if (!madeAt && pu->exitingObjectID() != INVALID_ID)
		{
			madeAt = w.logic->getFrame();
			b->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SOLD"), true);
			w.frames(3);
			b->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SOLD"), false);
		}
		else if (madeAt && pu->exitingObjectID() == INVALID_ID)
		{
			exitedAt = w.logic->getFrame();
		}
	}
	REQUIRE(madeAt != 0);
	REQUIRE(exitedAt != 0);
	CHECK(exitedAt - madeAt == 8); // 5 passes + 3 sold frames; the old top-of-update decrement finalized after 5
}
