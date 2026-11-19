// OpenBFME unit tests. GPL-3.0.
// PORT-7: the enemy scan of RotWK AnimalAIUpdate::update (RW 0x882058), ported into AmbientAIUpdate: the UpdateTimer gate
// (RW 0x882288 .. RW 0x8822A6) where it sits relative to the idle test (both skip branches land on RW 0x882508), its three
// filters (relationship flags 3 at RW 0x88235B, alive at RW 0x88233A, the KindOf mask at RW 0x882324 .. RW 0x88232C) and
// the flee step the port substitutes for aiFleeFromObject (RW 0x881FF3 -> RW 0x8824BC, S-5910). Synthetic data only.

#include "doctest.h"
#include "MoveTestUtil.h"

#include "GameLogic/Module/AmbientAIUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>

using namespace movetest;

namespace
{

// The animals of these tests: Critter scans every 4th frame, Wanderer never scans (UpdateTimer 0) and EveryFrame scans on
// every frame. All but Wanderer wander never (WanderPercentage 0), so the only order they can receive is the flee step.
// FleeRange / FleeDistance are the scan's range and the emulated step; SET_PANIC is the locomotor set the flee step selects
// (S-5910: what stands in for aiFleeFromObject), given its own speed so the selection is visible through curLocomotorSet.
const char *kPort7Objects =
	"ChildObject Critter Walker\n"
	"  Behavior = AnimalAIUpdate ModuleTag_AI\n"
	"    WanderPercentage = 0\n"
	"    FleeRange = 40\n"
	"    FleeDistance = 30\n"
	"    UpdateTimer = 4\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_PANIC\n"
	"    Speed = 30\n"
	"  End\n"
	"End\n"
	"ChildObject Wanderer Walker\n"
	"  Behavior = AnimalAIUpdate ModuleTag_AI\n"
	"    WanderPercentage = 101\n"
	"    MaxWanderDistance = 50\n"
	"    MaxWanderRadius = 100\n"
	"    FleeRange = 40\n"
	"    FleeDistance = 30\n"
	"    UpdateTimer = 0\n"
	"  End\n"
	"End\n"
	"ChildObject EveryFrame Walker\n"
	"  Behavior = AnimalAIUpdate ModuleTag_AI\n"
	"    WanderPercentage = 0\n"
	"    FleeRange = 40\n"
	"    FleeDistance = 30\n"
	"    UpdateTimer = 1\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_PANIC\n"
	"    Speed = 30\n"
	"  End\n"
	"End\n"
	// a tree: no INFANTRY, CAVALRY, MONSTER or MACHINE, so the KindOf mask of the scan rejects it
	"Object Sapling\n"
	"  KindOf = IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"End\n";

} // namespace

TEST_CASE("PORT-7: the enemy scan runs on UpdateTimer frames only (RW 0x882288 .. RW 0x8822A6)")
{
	MoveWorld mw(100, 100, kPort7Objects);
	mw.buildMap();
	// The partition's region is the terrain's extent (RW 0x62FCCD), which MoveWorld::buildMap does not set; without it the
	// scan's range walk finds nothing.
	mw.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *critter = mw.spawn("Critter", 205, 205);
	mw.spawn("Walker", 225, 205, 0.0f, mw.teamOf("Bob")); // another player and INFANTRY: a threat in range
	AIUpdateInterface *ai = mw.aiOf(critter);
	REQUIRE(ai->isIdle());
	// frames 1 to 3: frame / UpdateTimer leaves a remainder (RW 0x8822A2 div ecx / RW 0x8822A4 test edx,edx / RW 0x8822A6
	// jne), so the scan does not run and the animal stays idle
	for (int i = 0; i < 3; ++i)
	{
		mw.frames(1);
		CHECK(mw.logic->getFrame() == (unsigned)(i + 1));
		CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
		CHECK(ai->curLocomotorSet() == (int)LOCOMOTORSET_NORMAL);
	}
	// frame 4: the remainder is zero, the scan finds the enemy and the flee step orders a move
	mw.frames(1);
	CHECK(mw.logic->getFrame() == 4u);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(ai->curLocomotorSet() == (int)LOCOMOTORSET_PANIC); // the emulated flee takes the panic set (S-5910)
	// FleeDistance (30) away from the enemy at x 225, so 30 units along -x from the animal at x 205; the enemy shares the
	// animal's y, so the step leaves y alone
	const Coord3D goal = ai->stateMachine().goalPosition();
	CHECK(goal.x == 175.0f);
	CHECK(goal.y == 205.0f);
}

TEST_CASE("PORT-7: UpdateTimer 0 turns the scan off and leaves the wander dice of every idle frame running (RW 0x882292)")
{
	MoveWorld mw(100, 100, kPort7Objects);
	mw.buildMap();
	mw.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *wanderer = mw.spawn("Wanderer", 205, 205);
	mw.spawn("Walker", 225, 205, 0.0f, mw.teamOf("Bob")); // an enemy in range of an animal that never scans
	AIUpdateInterface *ai = mw.aiOf(wanderer);
	mw.frames(4);
	// The enemy changes nothing: the gate's zero test skips the scan (RW 0x88228B jbe RW 0x882508) and never once selects
	// the panic set. The animal still wanders, so the gate covers the scan alone and not the wander dice below it.
	CHECK(ai->curLocomotorSet() == (int)LOCOMOTORSET_NORMAL);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO); // the wander order (WanderPercentage 101)
}

TEST_CASE("PORT-7: the scan takes not-an-ally, alive, INFANTRY/CAVALRY/MONSTER/MACHINE threats (RW 0x882324 .. RW 0x88236D)")
{
	MoveWorld mw(100, 100, kPort7Objects);
	mw.buildMap();
	mw.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	// an ally infantry in range: relationship 0 (ALLIES) is not one of the flags' bits (3 = ENEMIES | NEUTRAL)
	Object *byAlly = mw.spawn("Critter", 205, 205);
	mw.spawn("Walker", 225, 205); // the animal's own team
	// an enemy structure in range: STRUCTURE is not in the KindOf mask { 8, 9, 10, 11 }
	Object *byStructure = mw.spawn("Critter", 205, 605);
	mw.spawn("Block", 225, 605, 0.0f, mw.teamOf("Bob"));
	// an enemy tree in range: IMMOBILE is not in the mask either
	Object *byTree = mw.spawn("Critter", 605, 205);
	mw.spawn("Sapling", 625, 205);
	// an enemy infantry in range: the control that must flee
	Object *byEnemy = mw.spawn("Critter", 605, 605);
	mw.spawn("Walker", 625, 605, 0.0f, mw.teamOf("Bob"));
	mw.frames(4);
	CHECK(mw.aiOf(byAlly)->currentStateId() == (unsigned)AI_IDLE);
	CHECK(mw.aiOf(byAlly)->curLocomotorSet() == (int)LOCOMOTORSET_NORMAL);
	CHECK(mw.aiOf(byStructure)->currentStateId() == (unsigned)AI_IDLE);
	CHECK(mw.aiOf(byStructure)->curLocomotorSet() == (int)LOCOMOTORSET_NORMAL);
	CHECK(mw.aiOf(byTree)->currentStateId() == (unsigned)AI_IDLE);
	CHECK(mw.aiOf(byTree)->curLocomotorSet() == (int)LOCOMOTORSET_NORMAL);
	CHECK(mw.aiOf(byEnemy)->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(mw.aiOf(byEnemy)->curLocomotorSet() == (int)LOCOMOTORSET_PANIC);
}

TEST_CASE("PORT-7: the scan runs before the idle test, so a moving animal flees too (RW 0x882508)")
{
	MoveWorld mw(100, 100, kPort7Objects);
	mw.buildMap();
	mw.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *animal = mw.spawn("EveryFrame", 205, 205); // UpdateTimer 1: the scan runs on every frame
	mw.spawn("Walker", 225, 205, 0.0f, mw.teamOf("Bob"));
	AIUpdateInterface *ai = mw.aiOf(animal);
	ai->aiMoveToPosition(Coord3D{ 805, 205, 0 }, CMD_FROM_PLAYER); // an order of its own, not the scan's
	REQUIRE(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(ai->curLocomotorSet() == (int)LOCOMOTORSET_NORMAL);
	mw.frames(1);
	// The animal is not idle, so the idle test would skip the rest of the update (RW 0x882508); the scan runs before it and
	// replaces the player's far goal with a flee step: the panic set and a goal that is no longer the far one.
	CHECK(ai->curLocomotorSet() == (int)LOCOMOTORSET_PANIC);
	CHECK(ai->stateMachine().goalPosition().x != 805.0f);
}

TEST_CASE("PORT-7: the ambient stop line names the ported scan and what stays emulated or unported (S-5910)")
{
	const std::string line = AmbientAIUpdate::stopLine();
	CHECK(line.find("[S-5910]") == 0u);
	// what this port brings in: the scan of every UpdateTimer frame with its three filters
	CHECK(line.find("the enemy scan of every UpdateTimer frame with its not-an-ally, alive and KindOf filters") != std::string::npos);
	// what is emulated rather than ported, and what is still missing
	CHECK(line.find("aiFleeFromObject (RW 0x881FF3) becoming a step of FleeDistance away with LOCOMOTORSET_PANIC") != std::string::npos);
	CHECK(line.find("the special-layer check (RW 0x881FCC, with the returning state)") != std::string::npos);
}
