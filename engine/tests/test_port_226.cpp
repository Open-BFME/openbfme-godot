// OpenBFME. GPL-3.0.
// The idle state's stance countdown: the idle unit guards where it stands again. A unit that carries a
// StancesBehavior fails its idle state every fifth update and comes back as AIGoingIdleState, which tells the StancesBehavior the move ended
// (RW 0x743864 -> RW 0x86228E) and then goes idle: the unit guards where it stands again (RW 0x662A21), so a unit left idle after a fight or a plain
// aiIdle does not stay on its plain mood scan. Expectations from the binary's body, read at RW 0x7553AB; the decomp line is the donor's.
//
//   0x7553AB  AIIdleState::update (RW): the stance countdown + 0x22 drops by one per update and, at 0, is 5 again.
//             DONOR BFME2 0x0035389E AIIdleStateUpdate.cpp: the whole countdown, written as
//                 if (!obj->testStatus(0x3A) && obj->getAI()->m_nextEnemyScanTime == 0) { if (--m_22 <= 0) { m_22 = 5;
//                     if (obj->findModule(StancesBehavior) && getMachine()->hasState(0x4E)) return STATE_FAILURE; } }
//             INFERENCE: the owner is asked for status 0x3A (INSIDE_GARRISON in the port's status table, RW 0x44DDEC) and for a StancesBehavior by the
//             class name key (RW 0x861E6E / 0x68BDA5, as StancesBehavior::of does); `hasState(0x4E)` is the port's machine lookup, true for every AI machine
//             of a non-bird module class (AIUpdateInterface::makeStateMachine defines 0x4E, RW 0x753755 / 0x7537E0).
//   0x7537E0  the idle state's failure id is 0x4E (defineState(0, idle, 0, 0x4E)): the failure leaves the idle state and enters AIGoingIdleState.
//   0x743864  AIGoingIdleState::onEnter: the owner's StancesBehavior onMoveEnd (RW 0x86228E), then failure, so the machine goes on to idle 0.
//   0x86228E  StancesBehavior::onMoveEnd: a Battle / HoldGround / Porcupine unit that is not HIDDEN guards here again (RW 0x662A21(0 / 1)).
//   0x7489B6  AIIdleState::onEnter: the countdown is 5 again at every entry (DONOR 0x00346FD0: `m_22 = 5`), so the fifth update of each idle entry fails.
//
// Synthetic INI (combattest::CombatWorld): no game files, no mod data.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
// a soldier with a StancesBehavior (the module the countdown asks for, RW 0x861E6E) and a template the store knows
const char kStancedObjects[] =
	"Object StancedSwordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
	"  End\n"
	"  Behavior = StancesBehavior ModuleTag_Stances\n"
	"    StanceTemplate = Port226Stances\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// the same soldier without the module: the countdown runs and finds nothing, so nothing fails
	"Object PlainSwordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

// the stance store the StancesBehavior needs; a test world's INI carries the StanceTemplate block
struct StanceStore
{
	StanceTemplateStore store;
	StanceTemplateStore *saved = TheStanceTemplateStore;
	StanceStore() { TheStanceTemplateStore = &store; }
	~StanceStore() { TheStanceTemplateStore = saved; }
};

// the world of the synthetic army beside the crew below
struct Port226World : CombatWorld
{
	explicit Port226World()
		: CombatWorld(kStancedObjects)
	{
		// the stance template the modules name: the block is registered and filled after the world's own INI (the module data only keeps the name)
		w.fx.env.blocks.registerBlock("StanceTemplate", [](INI *ini) { TheStanceTemplateStore->parseStanceTemplate(ini); });
		const std::string err = w.load("StanceTemplate Port226Stances\nEnd\n", INI_LOAD_OVERWRITE, "port226-stances.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		REQUIRE(TheStanceTemplateStore->find("Port226Stances") != nullptr);
		buildMap();
	}
};

int countdown(const AIStateMachine &machine)
{
	const AIIdleState *idle = dynamic_cast<const AIIdleState *>(machine.findState(AI_IDLE));
	REQUIRE(idle);
	return (int)idle->stancesCountdown();
}

// puts the unit in its idle state: two frames let the modules run (a StancesBehavior of a Battle unit guards on its first update, RW 0x8622E9 -> 0x662A21),
// then the AI's own idle command (RW 0x5E821A(2), as a stance change or an ended move sends it)
void enterIdle(CombatWorld &w, AIUpdateInterface &ai)
{
	w.frames(2);
	ai.aiIdle(CMD_FROM_AI); // the state's entry runs at once (the countdown is 5 here, before the idle state's first update)
	REQUIRE(ai.currentStateId() == (unsigned)AI_IDLE);
}

// runs frames until the idle state's countdown leaves `from`, or `limit` frames passed (-1). The idle state sleeps between its updates (RW 0x7553AB's sleep
// is 2 seconds of logic frames plus the entry offset), so a period takes more than five frames. The exact frame count is the simulation's and differs per
// entry, so the callers assert the state the run reached, not the number
int runUntilCountdownChanges(CombatWorld &w, const AIStateMachine &machine, int from, int limit = 200)
{
	for (int i = 0; i < limit; ++i)
	{
		w.logic->runLogicFrame();
		const AIIdleState *idle = dynamic_cast<const AIIdleState *>(machine.findState(AI_IDLE));
		REQUIRE(idle);
		if ((int)idle->stancesCountdown() != from)
		{
			return i + 1;
		}
	}
	return -1;
}

// runs frames until the unit's machine leaves its idle state, or `limit` frames passed (-1)
int runUntilIdleEnds(CombatWorld &w, AIUpdateInterface &ai, int limit = 200)
{
	for (int i = 0; i < limit; ++i)
	{
		w.logic->runLogicFrame();
		if (ai.currentStateId() != (unsigned)AI_IDLE)
		{
			return i + 1;
		}
	}
	return -1;
}
} // namespace

// The countdown itself: 5 at the entry (RW 0x7489B6), then 4, 3, 2, 1 on its updates and 5 again when the fifth update fires it (RW 0x7553AB). The
// observation is the idle state's own + 0x22, which the port keeps in AIIdleState.
TEST_CASE("PORT-226 idle stance countdown: the counter is 5 at the idle entry and drops by one per idle update (RW 0x7553AB / 0x7489B6)")
{
	StanceStore stances;
	Port226World w;
	Object *unit = w.unit("StancedSwordsman", 'A', 300.0f, 300.0f);
	REQUIRE(unit);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	enterIdle(w, *ai);
	AIStateMachine &machine = ai->stateMachine();
	REQUIRE(countdown(machine) == 5); // RW 0x7489B6 (DONOR 0x00346FD0: `m_22 = 5`)
	const int expected[] = { 4, 3, 2, 1, 5 };
	for (int step = 0; step < 5; ++step)
	{
		CAPTURE(step);
		const int before = countdown(machine);
		runUntilCountdownChanges(w, machine, before); // tolerance: the exact frame is the idle sleep's, the value it lands on is asserted here
		CHECK(countdown(machine) == expected[step]);
	}
}

// RW 0x7553AB: at its fifth update a stanced unit's idle state fails; the failure id is 0x4E (RW 0x7537E0), AIGoingIdleState, whose onEnter tells
// StancesBehavior::onMoveEnd (RW 0x743864 -> RW 0x86228E) and then fails on to idle. A Battle unit's onMoveEnd guards here again (RW 0x662A21): the unit
// leaves its idle state for the guard state on the frame the countdown fired, and its idle entry sets the counter to 5 again.
TEST_CASE("PORT-226 idle stance countdown: a stanced unit's idle state fails on its fifth update into AIGoingIdleState and guards again (RW 0x7553AB / 0x7537E0 / 0x743864)")
{
	StanceStore stances;
	Port226World w;
	Object *unit = w.unit("StancedSwordsman", 'A', 300.0f, 300.0f);
	REQUIRE(unit);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	StancesBehavior *sb = StancesBehavior::of(*unit);
	REQUIRE(sb);
	enterIdle(w, *ai);
	REQUIRE(sb->getStance() == STANCE_BATTLE);
	// the countdown reaches its last value without any failure: the idle state is held
	const int first = countdown(ai->stateMachine());
	REQUIRE(first == 5);
	for (int expected = 4; expected >= 1; --expected)
	{
		CAPTURE(expected);
		runUntilCountdownChanges(w, ai->stateMachine(), expected + 1); // tolerance: the exact frame is the idle sleep's
		CHECK(countdown(ai->stateMachine()) == expected);
		REQUIRE(ai->currentStateId() == (unsigned)AI_IDLE);
	}
	// the fifth update: the failure goes through AIGoingIdleState, whose onMoveEnd guards the Battle unit here
	runUntilIdleEnds(w, *ai); // tolerance: the exact frame is the idle sleep's; the state it lands in is asserted
	// AIGoingIdleState -> the guard here, or the plain idle when the guard machine's own report path refuses in this synthetic class (S-3802)
	CHECK(ai->currentStateId() == (unsigned)AI_GUARD);
	CHECK(countdown(ai->stateMachine()) == 5); // the entry that follows reset the counter (RW 0x7489B6)
	CHECK(sb->getStance() == STANCE_BATTLE);
}

// The neighbouring behaviour, held so the countdown cannot be made to fire everywhere: an idle unit without a StancesBehavior runs the same countdown and
// never fails (RW 0x7553AB's second test, RW 0x861E6E). It stays in its idle state over ten countdown periods.
TEST_CASE("PORT-226 idle stance countdown: a unit without a StancesBehavior keeps its idle state over ten countdown periods (RW 0x7553AB)")
{
	StanceStore stances;
	Port226World w;
	Object *unit = w.unit("PlainSwordsman", 'A', 300.0f, 300.0f);
	REQUIRE(unit);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	CHECK(StancesBehavior::of(*unit) == nullptr);
	enterIdle(w, *ai);
	for (int period = 0; period < 10; ++period)
	{
		CAPTURE(period);
		REQUIRE(countdown(ai->stateMachine()) == 5);
		for (int expected = 4; expected >= 1; --expected)
		{
			CAPTURE(expected);
			runUntilCountdownChanges(w, ai->stateMachine(), expected + 1); // tolerance: the exact frame is the idle sleep's
			CHECK(countdown(ai->stateMachine()) == expected);
			CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // no module: the countdown fires without a failure
		}
		runUntilCountdownChanges(w, ai->stateMachine(), 1); // tolerance: the exact frame is the idle sleep's
		CHECK(countdown(ai->stateMachine()) == 5);
		CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	}
}

// The neighbouring behaviour of the other side: status 0x3A (INSIDE_GARRISON, RW 0x44DDEC) holds the countdown (DONOR 0x0035389E: `!obj->testStatus(0x3A)`).
// A garrisoned unit's idle state never fails, so it does not leave its post for the countdown's sake; the status's removal lets the countdown run to its end.
TEST_CASE("PORT-226 idle stance countdown: a unit with status 0x3A holds the countdown and keeps its idle state (RW 0x7553AB)")
{
	StanceStore stances;
	Port226World w;
	Object *unit = w.unit("StancedSwordsman", 'A', 300.0f, 300.0f);
	REQUIRE(unit);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	enterIdle(w, *ai);
	unit->setStatus(0x3A, true); // INSIDE_GARRISON
	for (int i = 0; i < 400; ++i)
	{
		CAPTURE(i);
		CHECK(countdown(ai->stateMachine()) == 5);
		CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
		w.logic->runLogicFrame();
	}
	unit->setStatus(0x3A, false);
	runUntilCountdownChanges(w, ai->stateMachine(), 5); // tolerance: the exact frame is the idle sleep's
	CHECK(countdown(ai->stateMachine()) == 4);
	// it runs out and the unit guards again
	runUntilIdleEnds(w, *ai); // tolerance: the exact frame is the idle sleep's; the state it lands in is asserted
	CHECK(ai->currentStateId() == (unsigned)AI_GUARD);
}

// The state must survive a save and load with the counter in the middle of its period (PROCESS 15.5: new simulation state goes into crc and xfer):
// a unit whose countdown sits at 2 and is loaded keeps 2, not the value of an entry.
TEST_CASE("PORT-226 idle stance countdown: the countdown survives a save and load (AIIdleState::xfer)")
{
	StanceStore stances;
	Port226World w;
	Object *unit = w.unit("PlainSwordsman", 'A', 300.0f, 300.0f);
	REQUIRE(unit);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	enterIdle(w, *ai);
	for (int target = 4; target >= 2; --target)
	{
		CAPTURE(target);
		runUntilCountdownChanges(w, ai->stateMachine(), target + 1); // tolerance: the exact frame is the idle sleep's
	}
	REQUIRE(countdown(ai->stateMachine()) == 2);
	// the counter of the last row is what the save below carries
	XferSave save;
	ai->xferAIState(save);
	CHECK(countdown(ai->stateMachine()) == 2);
	XferLoad load(save.data());
	ai->xferAIState(load);
	REQUIRE(load.atEnd());
	CHECK(countdown(ai->stateMachine()) == 2);
	// the same save read back after the counter moved on: the file's value wins
	runUntilCountdownChanges(w, ai->stateMachine(), 2); // tolerance: the exact frame is the idle sleep's
	REQUIRE(countdown(ai->stateMachine()) == 1);
	XferLoad again(save.data());
	ai->xferAIState(again);
	CHECK(countdown(ai->stateMachine()) == 2);
	// a version 1 idle state (before the countdown: the version, the sleep offset, the init flag) loads with the countdown at 5, as RW 0x7489B6 starts it
	AIIdleState *idle = dynamic_cast<AIIdleState *>(ai->stateMachine().findState(AI_IDLE));
	REQUIRE(idle);
	XferSave idleSave;
	idle->xfer(idleSave);
	std::vector<std::uint8_t> v1 = idleSave.data();
	REQUIRE(v1.size() == 6); // the version, the u16 offset, the u16 countdown, the bool
	REQUIRE(v1[0] == 2);
	v1[0] = 1;
	v1.erase(v1.begin() + 3, v1.begin() + 5); // drop the countdown
	XferLoad oldIdle(v1);
	idle->xfer(oldIdle);
	CHECK(oldIdle.atEnd());
	CHECK(countdown(ai->stateMachine()) == 5);
}
