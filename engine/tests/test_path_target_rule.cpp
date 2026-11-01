// OpenBFME unit tests: the target rule of the path search's movement check (RW checkForMovement 0x6EBAA0, the enemy branch; BFME2 decomp
// PathfinderCheckForMovement.cpp): an enemy does not refuse a cell when it is the searcher's goal object or current victim, contained in one (a horde's
// member) or the target's container; any other enemy does. Synthetic data, no retail files.
#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/AIUpdate.h"

using namespace combattest;

TEST_CASE("path target rule: a horde's target horde and its members do not refuse its cells; another enemy horde's members do; without a target none")
{
	CombatWorld w;
	Object *a = w.unit("SwordHorde", 'A', 300, 300);
	Object *b = w.unit("SwordHorde", 'B', 500, 300);
	Object *c = w.unit("SwordHorde", 'B', 300, 500);
	w.frames(3);
	REQUIRE(w.membersOf(b).size() == 6); // SwordHorde: six Swordsmen (test_combat_horde.cpp)
	REQUIRE(w.membersOf(c).size() == 6);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	const ObjectPathfindAdapter &self = ai->adapter();
	const auto adapterOf = [](Object *o) -> const ObjectPathfindAdapter & { return o->getAIUpdateInterface()->adapter(); };
	// no target yet: every enemy refuses
	CHECK_FALSE(self.ignoresAsTarget(adapterOf(b)));
	CHECK_FALSE(self.ignoresAsTarget(adapterOf(w.membersOf(b).front())));
	REQUIRE(ai->aiAttackObject(b, CMD_FROM_PLAYER));
	w.frames(1);
	CHECK(self.ignoresAsTarget(adapterOf(b)));
	for (Object *m : w.membersOf(b))
	{
		CHECK(self.ignoresAsTarget(adapterOf(m)));
	}
	CHECK_FALSE(self.ignoresAsTarget(adapterOf(c)));
	for (Object *m : w.membersOf(c))
	{
		CHECK_FALSE(self.ignoresAsTarget(adapterOf(m)));
	}
}

TEST_CASE("path target rule: a lone unit whose victim is a horde member passes that member, its horde and the horde's other members; another enemy horde refuses")
{
	CombatWorld w;
	Object *lone = w.unit("Swordsman", 'A', 300, 300);
	Object *b = w.unit("SwordHorde", 'B', 500, 300);
	Object *c = w.unit("SwordHorde", 'B', 300, 500);
	w.frames(3);
	REQUIRE(w.membersOf(b).size() == 6); // SwordHorde: six Swordsmen (test_combat_horde.cpp)
	REQUIRE(w.membersOf(c).size() == 6);
	AIUpdateInterface *ai = lone->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	const ObjectPathfindAdapter &self = ai->adapter();
	const auto adapterOf = [](Object *o) -> const ObjectPathfindAdapter & { return o->getAIUpdateInterface()->adapter(); };
	Object *member = w.membersOf(b).front();
	REQUIRE(ai->aiAttackObject(member, CMD_FROM_PLAYER));
	w.frames(1);
	CHECK(self.ignoresAsTarget(adapterOf(member)));
	CHECK(self.ignoresAsTarget(adapterOf(b))); // the target's container
	for (Object *m : w.membersOf(b))
	{
		CHECK(self.ignoresAsTarget(adapterOf(m))); // the target container's members
	}
	CHECK_FALSE(self.ignoresAsTarget(adapterOf(c)));
	for (Object *m : w.membersOf(c))
	{
		CHECK_FALSE(self.ignoresAsTarget(adapterOf(m)));
	}
}

TEST_CASE("path target rule: the goal object alone and the current victim alone each make the relation; the Faithful search still refuses")
{
	CombatWorld w;
	Object *a = w.unit("SwordHorde", 'A', 300, 300);
	Object *b = w.unit("SwordHorde", 'B', 500, 300);
	w.frames(3);
	REQUIRE(w.membersOf(b).size() == 6);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	const ObjectPathfindAdapter &self = ai->adapter();
	const auto adapterOf = [](Object *o) -> const ObjectPathfindAdapter & { return o->getAIUpdateInterface()->adapter(); };
	// the goal object without a victim: the attack order sets both (AIAttack.cpp, RW 0x8DBACE); the victim cleared, the goal still relates
	REQUIRE(ai->aiAttackObject(b, CMD_FROM_PLAYER));
	w.frames(1);
	ai->setCurrentVictim(nullptr);
	CHECK(ai->currentVictim() == nullptr);
	CHECK(self.ignoresAsTarget(adapterOf(b)));
	CHECK(self.ignoresAsTarget(adapterOf(w.membersOf(b).front())));
	// the victim without a goal object: an idle unit given a victim (RW 0x668303) relates to it, its container and the container's members
	Object *c = w.unit("SwordHorde", 'B', 300, 500);
	w.frames(3);
	REQUIRE(w.membersOf(c).size() == 6);
	Object *lone = w.unit("Swordsman", 'A', 700, 700);
	w.frames(1);
	AIUpdateInterface *lai = lone->getAIUpdateInterface();
	REQUIRE(lai != nullptr);
	CHECK(lai->stateMachine().goalObject() == nullptr);
	lai->setCurrentVictim(w.membersOf(c).front());
	const ObjectPathfindAdapter &lself = lai->adapter();
	CHECK(lself.ignoresAsTarget(adapterOf(w.membersOf(c).front())));
	CHECK(lself.ignoresAsTarget(adapterOf(c)));
	CHECK(lself.ignoresAsTarget(adapterOf(w.membersOf(c).back())));
	CHECK_FALSE(lself.ignoresAsTarget(adapterOf(b)));
	// the relation is applied by the search only in Uber (UBER-PATH): the Faithful pathfinder keeps S-164
	CHECK_FALSE(w.logic->aiWorld()->pathfinder().uberLimits());
}
