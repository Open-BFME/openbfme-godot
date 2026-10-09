// OpenBFME. COMBAT-1 tests: hordes fight as hordes - the melee approach and wait states, the frozen formation while the horde is engaged, the members' attacks, the ranks
// released by a HordeAttackNugget, a dead member leaving the horde, the horde dying with its last member. Synthetic data, no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

using namespace combattest;

namespace
{
HordeAIUpdate *hordeAi(Object *h)
{
	return dynamic_cast<HordeAIUpdate *>(h->getAIUpdateInterface());
}
size_t aliveMembers(CombatWorld &w, Object *horde)
{
	size_t n = 0;
	if (horde && horde->getContain())
	{
		for (Object *m : w.membersOf(horde))
		{
			n += m->isEffectivelyDead() ? 0u : 1u;
		}
	}
	return n;
}
} // namespace

TEST_CASE("combat horde: a melee horde attacked by another engages (the formation freezes), the members fight, the loser horde dies and leaves the world, the winner re-forms")
{
	CombatWorld w;
	Object *a = w.unit("SwordHorde", 'A', 300, 300);
	Object *b = w.unit("SwordHorde", 'B', 420, 300);
	w.frames(3);
	REQUIRE(w.hordeOf(a) != nullptr);
	CHECK(aliveMembers(w, a) == 6);
	CHECK(aliveMembers(w, b) == 6);
	CHECK(w.playerOf('A')->commandPoints().getUsage() == 12); // Swordsman CommandPoints 2 x 6
	const ObjectID ia = a->getID(), ib = b->getID();
	REQUIRE(a->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER)); // the other horde acquires its attacker on its own
	bool engaged = false;
	int engagedAt = -1;
	for (int f = 0; f < 1500 && w.byId(ia) && w.byId(ib); ++f)
	{
		w.frames(1);
		if (w.byId(ia) && w.hordeOf(w.byId(ia))->meleeEngaged() && !engaged)
		{
			engaged = true;
			engagedAt = f;
		}
	}
	CHECK(engaged);
	CHECK(engagedAt < 80);  // the hordes start 120 apart, inside the melee readiness distance
	const bool aLives = w.byId(ia) != nullptr;
	const bool bLives = w.byId(ib) != nullptr;
	CHECK(aLives != bLives); // exactly one horde is gone: its last member died and took the horde object with it
	Object *winner = aLives ? w.byId(ia) : w.byId(ib);
	const char winnerSide = aLives ? 'A' : 'B';
	REQUIRE(winner != nullptr);
	CHECK(w.combat().counters().kills >= 6);
	CHECK(w.combat().counters().kills <= 11);
	const size_t left = aliveMembers(w, winner);
	CHECK(left >= 1);
	CHECK(w.hordeOf(winner)->getContainCount() == left); // the dead left the horde
	CHECK(w.playerOf(winnerSide)->commandPoints().getUsage() == (int)(2 * left));
	CHECK(w.playerOf(winnerSide == 'A' ? 'B' : 'A')->commandPoints().getUsage() == 0);
	// the fight over, the formation is released and the horde is idle (the survivors that advanced past their slots wait there: MOVE-1's UseSlowHordeMovement rule, a
	// member ahead of its slot waits for the formation; whether retail regroups a parked horde is a follow-up)
	w.frames(60);
	CHECK_FALSE(w.hordeOf(winner)->meleeEngaged());
	CHECK(winner->getAIUpdateInterface()->isIdle());
	// the hits: 10 SLASH through the 50% armour of the other side's swordsmen, five at a time
	CHECK(w.combat().counters().damageApplications >= 6 * 20);
}

TEST_CASE("combat horde: while engaged the formation is frozen, the members step to the enemy and the horde object follows its men")
{
	CombatWorld w;
	Object *a = w.unit("SwordHorde", 'A', 300, 300);
	Object *b = w.unit("SwordHorde", 'B', 400, 300);
	w.frames(3);
	a->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
	w.runUntil([&] { return w.hordeOf(a)->meleeEngaged(); }, 100);
	REQUIRE(w.hordeOf(a)->meleeEngaged());
	REQUIRE(hordeAi(a) != nullptr);
	CHECK(hordeAi(a)->engagedTarget() == b->getID());
	w.frames(10);
	// HORDE-2: the Amoeba update recentres the horde object on the member nearest the members' centroid (RW 0x86F18F): it stands where one of its men stands
	bool onMember = false;
	for (Object *m : w.membersOf(a))
	{
		onMember = onMember || (m->getPosition()->x == a->getPosition()->x && m->getPosition()->y == a->getPosition()->y);
	}
	CHECK(onMember);
	CHECK(hordeAi(a)->meleeStats().orders > 0); // members were told to attack
}

TEST_CASE("combat horde: a horde ordered onto a single unit kills it and goes idle")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("SwordHorde", 'A', 300, 300);
	Object *v = w.unit("Dummy", 'B', 400, 300);
	w.frames(3);
	const ObjectID vid = v->getID();
	a->getAIUpdateInterface()->aiAttackObject(v, CMD_FROM_PLAYER);
	w.runUntil([&] { return w.byId(vid) == nullptr; }, 600);
	CHECK(w.byId(vid) == nullptr);
	CHECK(w.combat().counters().kills == 1);
	w.frames(60);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(w.hordeOf(a)->meleeEngaged());
	CHECK(aliveMembers(w, a) == 6);
}

TEST_CASE("combat horde: an order on a member of the enemy horde is an order on the horde (the command path)")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	const int alice = w.playerIndex("Alice");
	Object *a = w.unit("SwordHorde", 'A', 300, 300);
	Object *b = w.unit("SwordHorde", 'B', 500, 300);
	w.frames(3);
	REQUIRE(w.membersOf(b).size() == 6);
	w.select(alice, { a });
	w.attackMessage(alice, w.membersOf(b)[2]);
	w.frames(1);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 1);
	CHECK(a->getAIUpdateInterface()->isAttacking());
	CHECK(a->getAIUpdateInterface()->currentVictimId() == b->getID());
}

TEST_CASE("combat horde: a dead member leaves the horde at once, the ranks close up and the horde dies with its last member")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("SwordHorde", 'B', 300, 300);
	w.frames(3);
	const ObjectID id = h->getID();
	std::vector<Object *> members = w.membersOf(h);
	REQUIRE(members.size() == 6);
	CHECK(w.playerOf('B')->commandPoints().getUsage() == 12);
	members[0]->kill(DEATH_NORMAL);
	CHECK(w.hordeOf(h)->getContainCount() == 5);
	CHECK(w.playerOf('B')->commandPoints().getUsage() == 10);
	w.frames(30);
	CHECK(w.hordeOf(h)->worstMemberSlotError() < 30.0f); // the others closed up
	for (Object *m : w.membersOf(h))
	{
		m->kill(DEATH_NORMAL);
	}
	w.frames(2);
	CHECK(w.byId(id) == nullptr); // an ImmortalBody cannot die: the horde leaves with its last member
	CHECK(w.playerOf('B')->commandPoints().getUsage() == 0);
	CHECK_FALSE(w.inPathfinder(id));
}

TEST_CASE("combat horde: an archer horde in range fires its rangefinder, the HordeAttackNugget releases the ranks and the arrows kill the target")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("ArcherHorde", 'A', 300, 300);
	Object *v = w.unit("Dummy", 'B', 420, 300); // 120 away: inside the rangefinder's 150 plus the spheres
	w.frames(12);                                // the new archers' clips reload first
	const ObjectID vid = v->getID();
	REQUIRE(a->getAIUpdateInterface()->aiAttackObject(v, CMD_FROM_PLAYER));
	w.runUntil([&] { return w.byId(vid) == nullptr; }, 400);
	CHECK(w.byId(vid) == nullptr); // 60 health at 12 per arrow (15 PIERCE through 80%) = 5 arrows
	REQUIRE(hordeAi(a) != nullptr);
	CHECK(hordeAi(a)->meleeStats().releases > 0);
	CHECK(w.combat().counters().projectilesLaunched >= 5);
	CHECK(w.combat().counters().kills == 1);
}
