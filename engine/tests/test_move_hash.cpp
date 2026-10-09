// OpenBFME unit tests: every piece of logic state the movement lane adds is in the state hash (lane MOVE-1; the lockstep rule: state that is not hashed cannot
// be compared between peers). One mutation per field group: the same world with the field changed hashes differently; the same world twice hashes the same.

#include "doctest.h"
#include "MoveTestUtil.h"

#include "GameLogic/Object/Contain/HordeContainCore.h"

#include <functional>

using namespace movetest;

namespace
{
using Mutator = std::function<void(MoveWorld &, Object *walker)>;

// a world with one walking unit (frame 3 of an order to the east), `mutate` applied, the hash taken at once
std::uint32_t walkerHash(const Mutator &mutate)
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	mw.frames(2);
	mw.aiOf(u)->aiMoveToPosition(Coord3D{ 505.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(3);
	if (mutate)
	{
		mutate(mw, u);
	}
	return mw.logic->computeStateHash();
}

std::uint32_t hordeHash(const Mutator &mutate)
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 305.0f, 305.0f);
	mw.frames(8);
	if (mutate)
	{
		mutate(mw, h);
	}
	return mw.logic->computeStateHash();
}
} // namespace

TEST_CASE("move hash: the same world twice hashes the same")
{
	CHECK(walkerHash(nullptr) == walkerHash(nullptr));
	CHECK(hordeHash(nullptr) == hordeHash(nullptr));
}

TEST_CASE("move hash: the locomotor's mutable state (caps, speed, flags, matrix, preferred point) is hashed")
{
	const std::uint32_t base = walkerHash(nullptr);
	const std::vector<std::pair<const char *, Mutator>> mutations = {
		{ "speed", [](MoveWorld &mw, Object *u) { Locomotor *l = mw.aiOf(u)->curLocomotor(); l->setSpeed(l->speed() + 0.25f); } },
		{ "flags", [](MoveWorld &mw, Object *u) { Locomotor *l = mw.aiOf(u)->curLocomotor(); l->setFlags(l->flags() ^ 0x40u); } },
		{ "max speed cap", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setMaxSpeedCap(3.0f); } },
		{ "temporary speed cap", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setTemporarySpeedCap(2.0f); } },
		{ "acceleration cap", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setAccelerationCap(1.0f); } },
		{ "braking cap", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setBrakingCap(1.0f); } },
		{ "turn rate cap", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setTurnRateCap(0.5f); } },
		{ "desired speed cap", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setDesiredSpeedCap(1.0f, 99); } },
		{ "preferred point", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setPreferredPoint(Coord3D{ 1.0f, 2.0f, 3.0f }); } },
		{ "matrix angle", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->curLocomotor()->setMatrixAngle(1.0f); } },
	};
	for (const auto &m : mutations)
	{
		INFO("mutation: " << m.first);
		CHECK(walkerHash(m.second) != base);
	}
}

TEST_CASE("move hash: the AI's own state (desired speed, goal, command source, state machine, model condition, pathfinder reservations) is hashed")
{
	const std::uint32_t base = walkerHash(nullptr);
	const std::vector<std::pair<const char *, Mutator>> mutations = {
		{ "desired speed", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->setDesiredSpeed(1.5f); } },
		{ "mover goal type", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->mover().setGoalNone(); } },
		{ "mover explicit goal", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->mover().setGoalExplicit(Coord3D{ 300.0f, 300.0f, 0.0f }); } },
		{ "mover goal angle", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->mover().setGoalAngle(1.0f); } },
		{ "model condition", [](MoveWorld &, Object *u) { u->setModelConditionState(AIUpdateInterface::modelConditionBit("PANICKING"), true); } },
		{ "position", [](MoveWorld &, Object *u) { Coord3D p = *u->getPosition(); p.y += 1.0f; u->setPosition(&p); } },
		{ "orientation", [](MoveWorld &, Object *u) { u->setOrientation(u->getOrientation() + 0.5f); } },
		{ "state machine (a stop order)", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->aiIdle(CMD_FROM_PLAYER); mw.logic->runLogicFrame(); mw.logic->runLogicFrame(); } },
		{ "command source", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->aiMoveToPosition(Coord3D{ 505.0f, 105.0f, 0.0f }, CMD_FROM_AI); } },
		{ "another destination", [](MoveWorld &mw, Object *u) { mw.aiOf(u)->aiMoveToPosition(Coord3D{ 505.0f, 205.0f, 0.0f }, CMD_FROM_PLAYER); } },
	};
	for (const auto &m : mutations)
	{
		INFO("mutation: " << m.first);
		CHECK(walkerHash(m.second) != base);
	}
}

TEST_CASE("move hash: the pathfinder's queues are hashed through the AIWorld's contributor")
{
	auto hashWith = [](bool queued) {
		MoveWorld mw;
		mw.buildMap();
		Object *u = mw.spawn("Walker", 105.0f, 105.0f);
		mw.frames(2);
		if (queued)
		{
			mw.ai->pathfinder().queueForPath(u->getID());
		}
		return mw.logic->computeStateHash();
	};
	CHECK(hashWith(false) != hashWith(true));
	CHECK(hashWith(true) == hashWith(true));
}

TEST_CASE("move hash: the order of the pathfinder's request queue is hashed, not just its length")
{
	auto hashWith = [](bool swapped) {
		MoveWorld mw;
		mw.buildMap();
		Object *a = mw.spawn("Walker", 105.0f, 105.0f);
		Object *b = mw.spawn("Walker", 205.0f, 105.0f);
		mw.frames(2);
		mw.ai->pathfinder().queueForPath(swapped ? b->getID() : a->getID());
		mw.ai->pathfinder().queueForPath(swapped ? a->getID() : b->getID());
		return mw.logic->computeStateHash();
	};
	CHECK(hashWith(false) != hashWith(true));
	CHECK(hashWith(true) == hashWith(true));
}

TEST_CASE("move hash: the horde's member slots, dirty flag and refresh frame are hashed")
{
	const std::uint32_t base = hordeHash(nullptr);
	const std::vector<std::pair<const char *, Mutator>> mutations = {
		{ "refresh frame and dirty flag", [](MoveWorld &mw, Object *h) { mw.hordeOf(h)->updateFormation(); mw.logic->runLogicFrame(); } },
		{ "a member displaced", [](MoveWorld &mw, Object *h) { Object *m = mw.membersOf(h)[4]; Coord3D p = *m->getPosition(); p.x += 5.0f; m->setPosition(&p); } },
		{ "a member's slot", [](MoveWorld &mw, Object *h) {
			  const std::vector<Object *> ms = mw.membersOf(h);
			  HordeContain *hc = mw.hordeOf(h);
			  // swap the slots of two members of the same rank
			  const int a = hc->getMemberSlot(ms[0]), b = hc->getMemberSlot(ms[1]);
			  const_cast<HordeContainCore &>(hc->core()).setMemberSlot(ms[0]->getID(), b);
			  const_cast<HordeContainCore &>(hc->core()).setMemberSlot(ms[1]->getID(), a); } },
		{ "a member's model condition", [](MoveWorld &mw, Object *h) { mw.membersOf(h)[0]->setModelConditionState(AIUpdateInterface::modelConditionBit("TRANSPORT_MOVING"), true); } },
		{ "the horde moving", [](MoveWorld &mw, Object *h) { mw.aiOf(h)->aiMoveToPosition(Coord3D{ 605.0f, 305.0f, 0.0f }, CMD_FROM_PLAYER); mw.logic->runLogicFrame(); } },
	};
	for (const auto &m : mutations)
	{
		INFO("mutation: " << m.first);
		CHECK(hordeHash(m.second) != base);
	}
}

TEST_CASE("move hash: the players' selections (what the next move message moves) are hashed")
{
	auto hashWith = [](int which) {
		MoveWorld mw;
		mw.buildMap();
		Object *a = mw.spawn("Walker", 105.0f, 105.0f);
		Object *b = mw.spawn("Walker", 205.0f, 105.0f);
		mw.frames(2);
		const int alice = mw.playerIndex("Alice");
		if (which == 1)
		{
			mw.select(alice, { a });
		}
		else if (which == 2)
		{
			mw.select(alice, { a, b });
		}
		else if (which == 3)
		{
			mw.select(alice, { b, a }); // the same units in another order
		}
		mw.frames(1);
		return mw.logic->computeStateHash();
	};
	const std::uint32_t none = hashWith(0), one = hashWith(1), two = hashWith(2), swapped = hashWith(3);
	CHECK(none != one);
	CHECK(one != two);
	CHECK(two != swapped); // the selection order decides which unit is the group's first
	CHECK(hashWith(2) == two);
}

TEST_CASE("move hash: the AI world's hooks and hash contributor go with it (a destroyed world leaves nothing in the logic), and re-attaching installs exactly one")
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	mw.frames(2);
	CHECK(mw.logic->extraWorldHookCount() == 1);
	CHECK(mw.logic->hashContributorCount() == 1);
	const std::uint32_t withWorld = mw.logic->computeStateHash();
	mw.logic->destroyObject(u);
	mw.frames(2);
	mw.ai->detach();
	mw.ai->attach(); // detach / attach / attach: one registration each, not two
	mw.ai->attach();
	CHECK(mw.logic->extraWorldHookCount() == 1);
	CHECK(mw.logic->hashContributorCount() == 1);
	mw.ai->detach();
	CHECK(mw.logic->extraWorldHookCount() == 0);
	CHECK(mw.logic->hashContributorCount() == 0);
	mw.ai.reset(); // the world is gone, the logic stays: hashing and creating / deleting objects touch no dead callback (ASan)
	(void)mw.logic->computeStateHash();
	Object *v = mw.spawn("Block", 305.0f, 305.0f);
	mw.logic->destroyObject(v);
	mw.frames(2);
	(void)withWorld;
	// a fresh world on the same logic hashes as one attached once
	mw.ai = std::make_unique<AIWorld>(*mw.logic, mw.config, mw.w.fx.env.macros);
	mw.ai->attach();
	CHECK(mw.logic->extraWorldHookCount() == 1);
	CHECK(mw.logic->hashContributorCount() == 1);
}

namespace
{
// the world hash after `mutate` changed one persistent pathfinder field of cell (20, 20) (or the zone manager)
std::uint32_t pathfinderHash(const std::function<void(Pathfinder &, PathfindCell &)> &mutate)
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 305.0f, 305.0f);
	mw.frames(2);
	(void)u;
	Pathfinder &pf = mw.ai->pathfinder();
	if (mutate)
	{
		mutate(pf, *pf.getCell(LAYER_GROUND, 20, 20));
	}
	return mw.logic->computeStateHash();
}
} // namespace

TEST_CASE("move hash: every persistent pathfinder field is hashed on its own (grid class, layers, cell bits, zone, obstacle, occupant chains, zone manager)")
{
	const std::uint32_t base = pathfinderHash(nullptr);
	CHECK(pathfinderHash(nullptr) == base);
	using Mut = std::function<void(Pathfinder &, PathfindCell &)>;
	const std::vector<std::pair<const char *, Mut>> mutations = {
		{ "type clear -> cliff", [](Pathfinder &, PathfindCell &c) { c.setType(PathfindCell::CELL_CLIFF); } },
		{ "type clear -> water", [](Pathfinder &, PathfindCell &c) { c.setType(PathfindCell::CELL_WATER); } },
		{ "layer", [](Pathfinder &, PathfindCell &c) { c.setLayer(LAYER_WALL); } },
		{ "connects-to layer", [](Pathfinder &, PathfindCell &c) { c.setConnectLayer(LAYER_WALL); } },
		{ "bit 17", [](Pathfinder &, PathfindCell &c) { c.setBit17(true); } },
		{ "impassable to players", [](Pathfinder &, PathfindCell &c) { c.setImpassableToPlayers(true); } },
		{ "extra pass", [](Pathfinder &, PathfindCell &c) { c.setExtraPass(true); } },
		{ "bit 22", [](Pathfinder &, PathfindCell &c) { c.setBit22(true); } },
		{ "bit 23", [](Pathfinder &, PathfindCell &c) { c.setBit23(true); } },
		{ "pinched", [](Pathfinder &, PathfindCell &c) { c.setPinched(true); } },
		{ "zone", [](Pathfinder &, PathfindCell &c) { c.setZone((zoneStorageType)(c.getZone() + 1)); } },
		{ "ground goal occupant", [](Pathfinder &pf, PathfindCell &c) { c.addOccupant(pf.pool(), OCC_GROUND_GOAL, 900, ICoord2D{ 20, 20 }); } },
		{ "position occupant", [](Pathfinder &pf, PathfindCell &c) { c.addOccupant(pf.pool(), OCC_POSITION, 900, ICoord2D{ 20, 20 }); } },
		{ "horde occupant", [](Pathfinder &pf, PathfindCell &c) { c.addOccupant(pf.pool(), OCC_HORDE_POSITION, 900, ICoord2D{ 20, 20 }); } },
		{ "occupant owner id", [](Pathfinder &pf, PathfindCell &c) {
			  c.addOccupant(pf.pool(), OCC_POSITION, 900, ICoord2D{ 20, 20 });
			  c.removeOccupant(pf.pool(), OCC_POSITION, 900);
			  c.addOccupant(pf.pool(), OCC_POSITION, 901, ICoord2D{ 20, 20 }); } },
		{ "obstacle id", [](Pathfinder &pf, PathfindCell &c) {
			  c.addOccupant(pf.pool(), OCC_POSITION, 900, ICoord2D{ 20, 20 });
			  c.info()->m_obstacleID = 77; } },
		{ "obstacle fence flag", [](Pathfinder &pf, PathfindCell &c) {
			  c.addOccupant(pf.pool(), OCC_POSITION, 900, ICoord2D{ 20, 20 });
			  c.info()->m_obstacleIsFence = 1; } },
		{ "blocked by ally", [](Pathfinder &pf, PathfindCell &c) {
			  c.addOccupant(pf.pool(), OCC_POSITION, 900, ICoord2D{ 20, 20 });
			  c.setBlockedByAlly(true); } },
		{ "zone block passable", [](Pathfinder &pf, PathfindCell &) { pf.zoneManager().setPassable(20, 20, !pf.zoneManager().isPassable(20, 20)); } },
		{ "zone block dirty", [](Pathfinder &pf, PathfindCell &) { pf.zoneManager().markCellDirty(20, 20); } },
	};
	for (const auto &m : mutations)
	{
		INFO("mutation: " << m.first);
		CHECK(pathfinderHash(m.second) != base);
	}
	// the order of an occupant chain is history: two owners added in the other order hash differently
	auto two = [](bool swapped) {
		return pathfinderHash([swapped](Pathfinder &pf, PathfindCell &c) {
			c.addOccupant(pf.pool(), OCC_POSITION, swapped ? 902 : 901, ICoord2D{ 20, 20 });
			c.addOccupant(pf.pool(), OCC_POSITION, swapped ? 901 : 902, ICoord2D{ 20, 20 });
		});
	};
	CHECK(two(false) != two(true));
}
