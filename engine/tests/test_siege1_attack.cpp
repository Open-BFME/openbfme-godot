// OpenBFME unit tests, lane SIEGE-1: the retail siege engines against the fortress maps' gates and walls (Siege1TestUtil.h). GPL-3.0.
//
// The weapon each engine picks against a gate (OnlyAgainst BLOCKING_GATE) and a wall section (OnlyAgainst WALK_ON_TOP_OF_WALL), where it stands when it
// hits, and what its hits do: Helm's Deep's main gate falls, its ImmortalBody walls stop at 1 (retail data: ImmortalBody, armour HelmsDeepWalls: SIEGE 25%),
// Grond's ram breaks Minas Tirith's gate (2000 SIEGE damage against DefaultWallArmor 100%: one blow). The frames and health values are this port's
// deterministic results (a fixed seed): regression pins, not retail measurements; re-measured when lanes WALL-2 (walkable walls and ramps, bit 17, the
// CanPathThroughGates default) and SIEGE-1 were merged: the same weapons, damage and outcomes, the drives' frames moved. SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include <cstdio>

using namespace siege1test;

namespace
{
const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";
const char *const kMinasTirith = "maps/map wor minas tirith/map wor minas tirith.map";

void print(const std::string &unit, const AttackResult &r)
{
	std::printf("  info: %s: health after the first hit %.9g, at the end %.9g\n", unit.c_str(), (double)r.healthAfterFirst, (double)r.healthAfter);
	MESSAGE(unit << " vs " << r.target << ": ordered " << r.ordered << ", first damage at frame " << r.firstHit << " from " << r.distAtHit << " with slot "
				 << r.slotAtHit << ", health " << r.healthBefore << " -> " << r.healthAfterFirst << " -> " << r.healthAfter << ", death frame " << r.deathFrame);
}
} // namespace

TEST_CASE("siege1 attack retail: Isengard's battering ram breaks Helm's Deep's main gate with its gate weapon (SECONDARY, OnlyAgainst BLOCKING_GATE)")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "IsengardBatteringRam", false, 250.0f, 600);
	print("IsengardBatteringRam", r);
	CHECK(r.ordered);
	CHECK(r.target == "RBHelmsDeepGateDoorBig");
	CHECK(r.slotAtHit == 1); // IsengardBatteringRamGate (AttackRange 58)
	CHECK(r.firstHit == 269); // the drive round to the causeway, then the PreAttackDelay
	CHECK(r.healthAfterFirst == 3000.0f); // ISENGARD_BATTERINGRAM_DAMAGE 1000, the gate's armour full
	CHECK(r.deathFrame == 335);
	CHECK(r.dead);
}

TEST_CASE("siege1 attack retail: the battering ram hits a Helm's Deep wall with its wall weapon (TERTIARY); the ImmortalBody wall stops at 1")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "IsengardBatteringRam", true, 250.0f, 600);
	print("IsengardBatteringRam", r);
	CHECK(r.ordered);
	CHECK(r.target == "HelmsDeepGatehouseRight");
	CHECK(r.slotAtHit == 2); // IsengardBatteringRamWall (the PRIMARY ram's DamageScalar is 0% against the Helm's Deep pieces)
	CHECK(r.firstHit == 47);
	CHECK(r.healthAfterFirst == 5750.0f); // 1000 x SIEGE 25%
	CHECK(r.healthAfter == 1.0f);
	CHECK_FALSE(r.dead);
}

TEST_CASE("siege1 attack retail: Gondor's trebuchet hits a Helm's Deep wall from its range (PRIMARY, SIEGE damage against HelmsDeepWalls 25%)")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "GondorTrebuchet", true, 500.0f, 300);
	print("GondorTrebuchet", r);
	CHECK(r.ordered);
	CHECK(r.slotAtHit == 0);
	CHECK(r.firstHit == 20); // in range at once: no drive, the PreAttackDelay and the stone's flight
	CHECK(r.healthAfterFirst == 5918.74951f); // the stone's damage (its scatter and splash: PROJ-2)
	CHECK(r.healthAfter == 5293.74854f);
}

TEST_CASE("siege1 attack retail: Grond breaks Minas Tirith's gate with its ram (PRIMARY MordorGrondRam, OnlyAgainst BLOCKING_GATE): one blow")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kMinasTirith, "Gate", "MordorGrond", false, 300.0f, 900);
	print("MordorGrond", r);
	CHECK(r.ordered);
	CHECK(r.target == "MinisGateDoor");
	CHECK(r.slotAtHit == 0);
	CHECK(r.firstHit == 297); // Grond (CanPathThroughGates No) keeps off Minas Tirith's wall and ramp cells (bit 17, lane WALL-2): the longer drive
	CHECK(r.deathFrame == 297);
	CHECK(r.dead);
}

TEST_CASE("siege1 attack retail: Mordor's catapult made inside its MinimumAttackRange (150) of Helm's Deep's gate backs off and then hits it")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorCatapult", false, 100.0f, 600);
	print("MordorCatapult", r);
	CHECK(r.ordered);
	CHECK(r.slotAtHit == 0);
	CHECK(r.firstHit == 47);
	CHECK(r.distAtHit > 150.0f); // tolerance: it fired from outside its minimum range (the drive ends where the attack state's approach puts it: 242.7)
	CHECK(r.deathFrame == 217);
}

TEST_CASE("siege1 attack retail: a mumak, the Dwarven demolisher and Mordor's catapult break Helm's Deep's main gate")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult m = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorMumakil", false, 300.0f, 600);
	print("MordorMumakil", m);
	CHECK(m.slotAtHit == 1); // MordorMumakilKnockback
	CHECK(m.firstHit == 66);
	CHECK(m.healthAfterFirst == 3400.0f);
	CHECK(m.deathFrame == 282);
	const AttackResult d = siegeAttack(*s, kHelmsDeep, "Main Gate", "DwarvenDemolisher", false, 250.0f, 600);
	print("DwarvenDemolisher", d);
	CHECK(d.slotAtHit == 1); // DwarvenBatteringRamGate
	CHECK(d.firstHit == 254);
	CHECK(d.healthAfterFirst == 3000.0f);
	CHECK(d.deathFrame == 320);
	const AttackResult c = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorCatapult", false, 500.0f, 600);
	print("MordorCatapult", c);
	CHECK(c.slotAtHit == 0);
	CHECK(c.firstHit == 24);
	CHECK(c.healthAfterFirst == 3245.99341f);
	CHECK(c.deathFrame == 262);
}
