// OpenBFME unit tests. GPL-3.0.
// Lane IDLE-1 r2 (community FB-0001: "soldiers / horse riders run in place"): horde members that turn where they stand end the frame without MOVING, in the two
// cases the review named: hordes battering a building (the members turn to their attack positions around it) and a cavalry melee (the riders' combat clips:
// RW 0x4B4443 takes the first ModelConditionState that matches, so MOVING ATTACKING can pick a running clip).
// TARGET FACTS: RW's scheduler (RW 0x62E982) runs updates[0] in phases 3 / 4 and updates[1] in phase 5; every AI update's vslot 0x30 is RW 0x851E97 (0) and
// HordeContain's RW 0x490AC4 (1), so a member's own update (the angle goal RW 0x66C960, which sets MOVING while it turns) runs before its horde's member pass,
// whose hub clears MOVING again (RW 0x8750B8; the active member's hold RW 0x877BB8) and leaves a member whose physics motion is disabled without an order and
// without MOVING (RW 0x874724). The expectations are outcomes (no member stands with MOVING for 5 frames, 1 s), never this engine's frame-exact output.
// The aim state's own turn (RW 0x75232F with vslot 0x21C, then its exit RW 0x74BEE7 clears the goal with vslot 0x220 before the member update clears MOVING)
// can leave MOVING for the few frames the member's update sleeps (RW 0x66C8F4's 5 frames): counted, and bounded by the 5 frame rule.

#include "doctest.h"

#include "HudTestUtil.h"
#include "Idle1TurnProbe.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <string>

namespace
{
struct TurnScenario
{
	TurnProbe probe;
	int alive = 0;              ///< attacker members alive at the end
	float targetDamage = 0.0f;  ///< the target's health lost (the attack happened)
};

// `attacker` (the local player's) is ordered to attack `target` (Player_2's, made `distance` away); the members of every horde are sampled each frame
TurnScenario runTurns(const char *attacker, const char *target, float distance, int frames)
{
	TurnScenario s;
	hudtest::Rig rig(hudtest::shared());
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const ICoord2D *ext = rig.game->ai().pathfinder().getExtent();
	const float cx = (float)(ext->x * 5), cy = (float)(ext->y * 5);
	Object *a = rig.make(attacker, cx - distance * 0.5f, cy);
	Object *d = rig.make(target, cx + distance * 0.5f, cy, enemy);
	REQUIRE(a != nullptr);
	REQUIRE(d != nullptr);
	const ObjectID aid = a->getID(), did = d->getID();
	const float health0 = d->getBodyModule() ? d->getBodyModule()->getHealth() : 0.0f;
	for (int i = 0; i < 3; ++i)
	{
		rig.game->advance(0.2);
	}
	REQUIRE(a->getAIUpdateInterface()->aiAttackObject(d, CMD_FROM_PLAYER));
	if (d->getAIUpdateInterface())
	{
		d->getAIUpdateInterface()->aiAttackObject(a, CMD_FROM_PLAYER);
	}
	for (int f = 0; f < frames; ++f)
	{
		rig.game->advance(0.2);
		s.probe.sample(rig.logic());
	}
	const Object *dh = rig.logic().findObjectByID(did);
	s.targetDamage = health0 - (dh && dh->getBodyModule() ? dh->getBodyModule()->getHealth() : 0.0f);
	if (Object *h = rig.logic().findObjectByID(aid))
	{
		s.alive = h->getContain() ? (int)h->getContain()->getContainCount() : 0;
	}
	return s;
}

std::string reportOf(const char *name, const TurnScenario &s)
{
	std::ostringstream os;
	os << name << ": member-frames turning on the spot " << s.probe.turned << " (with MOVING " << s.probe.turnedMoving << "); attacking " << s.probe.turnedAttacking << " ("
	   << s.probe.turnedAttackingMoving << "); cavalry " << s.probe.turnedCavalry << " (" << s.probe.turnedCavalryMoving << "); members standing with MOVING for 5 frames "
	   << s.probe.stillMovingStreaks << "; attackers left " << s.alive << ", damage to the target " << s.targetDamage << "\n"
	   << s.probe.samples.str();
	return os.str();
}
} // namespace

TEST_CASE("idle1 retail: hordes battering a building turn their members without a run clip on the spot (RW 0x851E97 / 0x490AC4, RW 0x8750B8)")
{
	if (!hudtest::haveWorld("idle1 building"))
	{
		return;
	}
	for (const char *attacker : { "MordorFighterHorde", "GondorKnightHorde" })
	{
		const TurnScenario s = runTurns(attacker, "GondorBarracks", 260.0f, 300);
		MESSAGE(reportOf(attacker, s));
		CHECK(s.targetDamage > 0.0f);         // not vacuous: the horde batters the building
		CHECK(s.probe.turned >= 30);          // and its members turn where they stand
		CHECK(s.probe.stillMovingStreaks == 0);
	}
}

TEST_CASE("idle1 retail: a cavalry melee leaves no rider running on the spot (RW 0x851E97 / 0x490AC4, RW 0x8750B8 / 0x877BB8, RW 0x874724)")
{
	if (!hudtest::haveWorld("idle1 cavalry"))
	{
		return;
	}
	for (const char *cavalry : { "GondorKnightHorde", "IsengardWargRiderHorde" })
	{
		const TurnScenario s = runTurns(cavalry, "MordorFighterHorde", 260.0f, 300);
		MESSAGE(reportOf(cavalry, s));
		CHECK(s.probe.turnedCavalry >= 30); // not vacuous: riders turn where they stand in the melee
		CHECK(s.probe.turnedCavalryMoving == 0);
		CHECK(s.probe.stillMovingStreaks == 0);
	}
}
