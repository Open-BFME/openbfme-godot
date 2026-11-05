// OpenBFME unit tests, lane SIEGE-1: the retail siege engines against the fortress maps' gates and walls (Siege1TestUtil.h). GPL-3.0.
//
// The weapon each engine picks against a gate (OnlyAgainst BLOCKING_GATE) and a wall section (OnlyAgainst WALK_ON_TOP_OF_WALL), where it stands when it
// hits, and what its hits do: Helm's Deep's main gate falls, its ImmortalBody walls stop at 1 (retail data: ImmortalBody, armour HelmsDeepWalls: SIEGE 25%),
// Grond's ram breaks Minas Tirith's gate (2000 SIEGE damage against DefaultWallArmor 100%: one blow). The frames and health values are this port's
// deterministic results (a fixed seed): regression pins, not retail measurements; re-measured when lanes WALL-2 (walkable walls and ramps, bit 17, the
// CanPathThroughGates default) and SIEGE-1 were merged: the same weapons, damage and outcomes, the drives' frames moved. SKIPs loudly without the installs.
// Merge queue q26b: these runs keep #64's Easy AI defender (siegeAttack's aiDefender true). SIEGEFIX-1 made the helper's default a human defender that
// gives no order (test_siege1_gate.cpp); with it the ram, the demolisher and the mumak ordered from Player_2_Start's side stop about 230 short of the gate
// (attack state, the gate their victim) and never strike: a follow-up, measured, not pinned here.

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
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "IsengardBatteringRam", false, 250.0f, 600, true);
	print("IsengardBatteringRam", r);
	CHECK(r.ordered);
	CHECK(r.target == "RBHelmsDeepGateDoorBig");
	CHECK(r.slotAtHit == 1); // IsengardBatteringRamGate (AttackRange 58)
	// merge queue q34 (#161's block search, block zones RW 0x9389A6 and raised branch RW 0x9389CE, gives the siege engine and the AI defender other routes at Helm's Deep;
	// measured once on the merged tree and at each merge point, SPELLFIX-1 and #160 keep the archive's values): first damage 269 -> 237, death 359 -> 322
	// lane PATH-REBUILD (the found paths rebuilt from their optimised nodes, RW 0x767B0E; a WHEELS_FOUR unit's first leg bent from its facing or reversed, RW 0x766F7F / 0x767800; the AI defender and the random stream move with every unit's lines; measured twice on the lane's tree): first damage 237 -> 273, death 322 -> 355
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): first damage 273 -> 249
	// pin: merge queue q41 (DECOMPFIX-1104B: AIMAIM-1's aim, RW 0x75232F, with BUMP-1's give-way rules and STARTRANGE-1's start range; measured once on the merged tree and at its merge point, NECRODIE-1 keeps it): first damage 249 -> 248
	CHECK(r.firstHit == 248); // the drive round to the causeway, then the PreAttackDelay
	CHECK(r.healthAfterFirst == 3000.0f); // ISENGARD_BATTERINGRAM_DAMAGE 1000, the gate's armour full
	// merge queue q27: death 335 -> 366, GATEPUSH-1 (the gate repels the units colliding with it when it opens or closes, RepelCollidingUnits; measured
	// at each merge point)
	// merge queue q31 (#68's ambient wanderers draw from the game's random numbers; measured at each merge point): death 366 -> 364
	// merge queue q32 (#159's ramp cells (RW 0x93527F: Helm's Deep's ramps meet the ground and the wall tops only at their two ends) move the AI defender and the random stream; measured once on the merged tree, the change appears at #159's merge point): death 364 -> 359
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): death 355 -> 315
	CHECK(r.deathFrame == 315);
	CHECK(r.dead);
}

TEST_CASE("siege1 attack retail: the battering ram hits a Helm's Deep wall with its wall weapon (TERTIARY); the ImmortalBody wall stops at 1")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "IsengardBatteringRam", true, 250.0f, 600, true);
	print("IsengardBatteringRam", r);
	CHECK(r.ordered);
	CHECK(r.target == "HelmsDeepGatehouseRight");
	CHECK(r.slotAtHit == 2); // IsengardBatteringRamWall (the PRIMARY ram's DamageScalar is 0% against the Helm's Deep pieces)
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): first damage 48 -> 43
	CHECK(r.firstHit == 43); // lane WARG-1 (FindClosestPath RW 0x6FB869 at the attack path's adjust RW 0x666E51 and computePath's fallback, the unclamped cells RW 0x6E8CE6; measured once on the lane's tree after the archive merge, a CI host): 47 -> 48
	CHECK(r.healthAfterFirst == 5750.0f); // 1000 x SIEGE 25%
	CHECK(r.healthAfter == 1.0f);
	CHECK_FALSE(r.dead);
}

TEST_CASE("siege1 attack retail: Gondor's trebuchet hits a Helm's Deep wall from its range (PRIMARY, SIEGE damage against HelmsDeepWalls 25%)")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "GondorTrebuchet", true, 500.0f, 300, true);
	print("GondorTrebuchet", r);
	CHECK(r.ordered);
	CHECK(r.slotAtHit == 0);
	// merge queue q26 (re-measured on the archive with SIEGE-1's pair rule and the rest): 20 / 5918.74951 / 5293.74854 -> 16 / 5929.4707 / 5506.29492
	// merge queue q31 (#68's ambient wanderers draw from the game's random numbers; measured at each merge point): 16 / 5974.28516 / 5597.16553 -> 17 / 5929.73 / 5557.55
	// pin: merge queue q41 (DECOMPFIX-1104B: AIMAIM-1's aim, RW 0x75232F, with BUMP-1's give-way rules and STARTRANGE-1's start range; measured once on the merged tree and at its merge point, NECRODIE-1 keeps it): first damage 17 -> 16
	CHECK(r.firstHit == 16); // in range at once: no drive, the PreAttackDelay and the stone's flight
	// lane FBSIM-1 (the scene's computer players build through their dozers, whose sites are now RotWK's phantoms until placed, RW 0x88D272 / 0x88D44F: the shared random stream and the AIs' play move), measured on the lane's tree (a CI host) with the AI defender: 5929.4707 / 5506.29492 -> 5974.28516 / 5597.16553
	// lane MODCOMPAT-MOD1 (GeometryUpgrade: every object reads its own GeometryInfo, RW Object + 0xA8, for its combat circle and sphere (+ 0xB8 / + 0xBC), partition cells and pathfinder footprint, and the gate's open / closed switch runs the full refresh RW 0x68B244; measured at its commit, a CI host):
	// 5974.28516 / 5597.16553 -> 5974.30127 / 5597.28369
	// merge queue q32 (#159's ramp cells (RW 0x93527F: Helm's Deep's ramps meet the ground and the wall tops only at their two ends) move the AI defender and the random stream; measured once on the merged tree, the change appears at #159's merge point): 5557.54834 -> 5565.90918
	// merge queue q33 (measured once on the merged tree and at each merge point): the archive's values hold through every item (MODCOMPAT-MOD1's own pair
	// was measured on its older base)
	// merge queue q38 (LOCODIST-1's retail locomotor distance to the goal, RW 0x664127, moves the AI defender and the random stream; measured once on the merged tree and at each merge point, #172 keeps it): 5929.73486 -> 5959.89404
	// pin: merge queue q41 (DECOMPFIX-1104B: AIMAIM-1's aim, RW 0x75232F, with BUMP-1's give-way rules and STARTRANGE-1's start range; measured once on the merged tree and at its merge point, NECRODIE-1 keeps it): 5959.89404 -> 5929.73389
	CHECK(r.healthAfterFirst == 5929.73389f); // the stone's damage (its scatter and splash: PROJ-2)
	// merge queue q34 (#161's block search, block zones RW 0x9389A6 and raised branch RW 0x9389CE, gives the siege engine and the AI defender other routes at Helm's Deep;
	// measured once on the merged tree and at each merge point, SPELLFIX-1 and #160 keep the archive's values): 5565.90918 -> 5530.59473
	// lane PATH-REBUILD (the found paths rebuilt from their optimised nodes, RW 0x767B0E; a WHEELS_FOUR unit's first leg bent from its facing or reversed, RW 0x766F7F / 0x767800; the AI defender and the random stream move with every unit's lines; measured twice on the lane's tree): 5530.59473 -> 5581.5; with the optimiser's layer rule (RW 0x766532: the causeway's ramp keeps its anchors) 5596.86768
	// merge queue q37 (#187's rebuilt paths on top of WARG-1's FindClosestPath; measured once on the merged tree, ATKRESET-1 keeps it): 5547.61523 -> 5577.25928
	// merge queue q38 (LOCODIST-1's retail locomotor distance to the goal, RW 0x664127, moves the AI defender and the random stream; measured once on the merged tree and at each merge point, #172 keeps it): 5577.25928 -> 5587.55957
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): 5587.55957 -> 5607.5376
	// pin: merge queue q41 (DECOMPFIX-1104B: AIMAIM-1's aim, RW 0x75232F, with BUMP-1's give-way rules and STARTRANGE-1's start range; measured once on the merged tree and at its merge point, NECRODIE-1 keeps it): 5607.5376 -> 5538.26074
	CHECK(r.healthAfter == 5538.26074f); // with the arc's segment count at x87 precision control 24 (RW 0x765E00; measured twice on the lane's tree): 5596.86768 -> 5547.61523
}

TEST_CASE("siege1 attack retail: Grond breaks Minas Tirith's gate with its ram (PRIMARY MordorGrondRam, OnlyAgainst BLOCKING_GATE): one blow")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kMinasTirith, "Gate", "MordorGrond", false, 300.0f, 900, true);
	print("MordorGrond", r);
	CHECK(r.ordered);
	CHECK(r.target == "MinisGateDoor");
	CHECK(r.slotAtHit == 0);
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): first damage 291 -> 206
	CHECK(r.firstHit == 206); // Grond (CanPathThroughGates No) keeps off Minas Tirith's wall and ramp cells (bit 17, lane WALL-2): the longer drive (q26: 297 -> 291)
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): death 291 -> 206
	CHECK(r.deathFrame == 206);
	CHECK(r.dead);
}

TEST_CASE("siege1 attack retail: Mordor's catapult made inside its MinimumAttackRange (150) of Helm's Deep's gate backs off and then hits it")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorCatapult", false, 100.0f, 600, true);
	print("MordorCatapult", r);
	CHECK(r.ordered);
	CHECK(r.slotAtHit == 0);
	// merge queue q32 (#159's ramp cells (RW 0x93527F: Helm's Deep's ramps meet the ground and the wall tops only at their two ends) move the AI defender and the random stream; measured once on the merged tree, the change appears at #159's merge point): first damage 48 -> 49
	// merge queue q33 (MODCOMPAT-MOD1's modules move the AI defender and the random stream; measured at each merge point): 49 -> 59, at the end 2869.34985 ->
	// 2935.85986
	// lane PATH-REBUILD (the found paths rebuilt from their optimised nodes, RW 0x767B0E; a WHEELS_FOUR unit's first leg bent from its facing or reversed, RW 0x766F7F / 0x767800; the AI defender and the random stream move with every unit's lines; measured twice on the lane's tree): 59 -> 57, at the end 2935.87988 -> 2935.84985; with the optimiser's layer rule (RW 0x766532) 56 and 2869.33984
	CHECK(r.firstHit == 56); // merge queue q26: 47 -> 63; lane MODCOMPAT-MOD1 (GeometryUpgrade's own GeometryInfo per object, RW 0x68B244): 63 -> 66
	CHECK(r.distAtHit > 150.0f); // tolerance: it fired from outside its minimum range (the drive ends where the attack state's approach puts it: 243.2)
	// merge queue q26 (the archive's pair rule, SIEGE-1 / test_siege1_gate.cpp): from its backed-off spot most stones are caught before the gate; at frame 600
	// the gate still stands at 2935.86 (a follow-up: test_siege1_gate.cpp's back-off case)
	CHECK(r.deathFrame == -1);
	// lane MODCOMPAT-MOD1 (GeometryUpgrade's own GeometryInfo per object, RW 0x68B244, measured at its commit): 2935.87988 -> 2935.84985
	// merge queue q32 (#159's ramp cells (RW 0x93527F: Helm's Deep's ramps meet the ground and the wall tops only at their two ends) move the AI defender and the random stream; measured once on the merged tree, the change appears at #159's merge point): 2869.33984 -> 2869.34985
	// merge queue q34 (#161's block search, block zones RW 0x9389A6 and raised branch RW 0x9389CE, gives the siege engine and the AI defender other routes at Helm's Deep;
	// measured once on the merged tree and at each merge point, SPELLFIX-1 and #160 keep the archive's values): 2935.85986 -> 2935.87988
	// merge queue q37 (measured once on the merged tree): 2869.35986 -> 2869.32983
	// merge queue q38 (LOCODIST-1's retail locomotor distance to the goal, RW 0x664127, moves the AI defender and the random stream; measured once on the merged tree and at each merge point, #172 keeps it): 2869.32983 -> 2869.34985
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): 2869.34985 -> 2869.33984
	CHECK(r.healthAfter == 2869.33984f); // with the arc's segment count at x87 precision control 24 (RW 0x765E00; measured twice on the lane's tree): 2869.33984 -> 2869.35986; lane FBSIM-1 (the AI defender's phantom sites move the random stream): 2935.85986 -> 2935.87988
}

TEST_CASE("siege1 attack retail: a mumak and the Dwarven demolisher break Helm's Deep's main gate; Mordor's catapult from 500 toward Player_2_Start hits the flanking tower")
{
	OPENBFME_REQUIRE_START(s);
	const AttackResult m = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorMumakil", false, 300.0f, 600, true);
	print("MordorMumakil", m);
	CHECK(m.slotAtHit == 1); // MordorMumakilKnockback
	// merge queue q26: the mumak's first damage 66 / 3400 -> 18 / 3997.75 (a knockback graze before its first full hit), death 282 -> 424
	// merge queue q27: first damage 18 -> 184, death 424 -> 415, LOS-1 (the terrain line of sight in the target search and the attack view; measured at
	// each merge point)
	// merge queue q31 (#68's ambient wanderers draw from the game's random numbers; measured at each merge point): first damage 184 -> 196 (3997.75 -> 3999.25), death 415 -> 423
	// merge queue q32 (#159's ramp cells (RW 0x93527F: Helm's Deep's ramps meet the ground and the wall tops only at their two ends) move the AI defender and the random stream; measured once on the merged tree, the change appears at #159's merge point): the first graze 3999.25 -> 3998.5
	// merge queue q34 (#161's block search, block zones RW 0x9389A6 and raised branch RW 0x9389CE, gives the siege engine and the AI defender other routes at Helm's Deep;
	// measured once on the merged tree and at each merge point, SPELLFIX-1 and #160 keep the archive's values): first damage 196 -> 140, death 407 -> 374 (#160's own 3996.25 graze was
	// measured on its base; on the archive's tree the graze stays 3998.5)
	// lane PATH-REBUILD (the found paths rebuilt from their optimised nodes, RW 0x767B0E; a WHEELS_FOUR unit's first leg bent from its facing or reversed, RW 0x766F7F / 0x767800; the AI defender and the random stream move with every unit's lines; measured twice on the lane's tree): the mumak's first damage 140 -> 134 (3998.5 -> 3997.75), death 374 -> 366; the demolisher's first damage 216 -> 241, death 283 -> 307;
	// with the optimiser's layer rule (RW 0x766532): the mumak's graze 3999.25, the demolisher's first damage 239, death 305; with the arc's segment count at x87 precision control 24 (RW 0x765E00; measured twice on the lane's tree): the graze
	// 3997, the demolisher's first damage 243, death 309
	// merge queue q37 (measured once on the merged tree): the mumak's graze 3997 -> 3997.75
	CHECK(m.firstHit == 134);
	// merge queue q38 (LOCODIST-1's retail locomotor distance to the goal, RW 0x664127, moves the AI defender and the random stream; measured once on the merged tree and at each merge point, #172 keeps it): the mumak's graze 3997.75 -> 3997
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): the mumak's graze 3997 -> 3997.75
	CHECK(m.healthAfterFirst == 3997.75f);
	// merge queue q33 (#133's straight production exits move the scene's computer players; measured at each merge point): death 423 -> 407
	CHECK(m.deathFrame == 366);
	const AttackResult d = siegeAttack(*s, kHelmsDeep, "Main Gate", "DwarvenDemolisher", false, 250.0f, 600, true);
	print("DwarvenDemolisher", d);
	CHECK(d.slotAtHit == 1); // DwarvenBatteringRamGate
	// merge queue q27: first damage 254 -> 316, death 320 -> 382, GATEPUSH-1 (the gate's RepelCollidingUnits; measured at each merge point)
	// merge queue q29: first damage 316 -> 315, death 382 -> 381, FBSIM-1 (the scene's computer defender builds phantom sites; measured at each merge point)
	// merge queue q31 (#68's ambient wanderers draw from the game's random numbers; measured at each merge point): first damage 315 -> 317, death 381 -> 383
	// merge queue q32 (#159's ramp cells (RW 0x93527F: Helm's Deep's ramps meet the ground and the wall tops only at their two ends) move the AI defender and the random stream; measured once on the merged tree, the change appears at #159's merge point): first damage 317 -> 313, death 383 -> 379
	// merge queue q34 (#161's block search, block zones RW 0x9389A6 and raised branch RW 0x9389CE, gives the siege engine and the AI defender other routes at Helm's Deep;
	// measured once on the merged tree and at each merge point, SPELLFIX-1 and #160 keep the archive's values): first damage 313 -> 209, death 379 -> 275
	// merge queue q35 (#132's ground-structure melee reach, measured once on the merged tree and at each merge point; SIMSMALL-1, #178 and #179 keep the archive's values): first damage 209 -> 216, death 275 -> 283
	// merge queue q37 (measured once on the merged tree): first damage 243 -> 245, death 309 -> 312
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): the demolisher's first damage 245 -> 242
	// pin: merge queue q41 (DECOMPFIX-1104B: AIMAIM-1's aim, RW 0x75232F, with BUMP-1's give-way rules and STARTRANGE-1's start range; measured once on the merged tree and at its merge point, NECRODIE-1 keeps it): first damage 242 -> 250
	CHECK(d.firstHit == 250);
	CHECK(d.healthAfterFirst == 3000.0f);
	// pin: #207 (ClassifyWall's edge pass RW 0x936686 .. 0x936811: the wall tops' rims refuse every locomotor, the closest height layer RW 0x9362A3, every ramp stamped again newest first RW 0x935BFF; routes and the random stream move at Helm's Deep), measured once on the merged tree and at each merge point (#211, QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it): its death 312 -> 309
	// pin: merge queue q41 (DECOMPFIX-1104B: AIMAIM-1's aim, RW 0x75232F, with BUMP-1's give-way rules and STARTRANGE-1's start range; measured once on the merged tree and at its merge point, NECRODIE-1 keeps it): death 309 -> 318
	CHECK(d.deathFrame == 318);
	const AttackResult c = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorCatapult", false, 500.0f, 600, true);
	print("MordorCatapult", c);
	// on the archive (SIEGE-1, test_siege1_gate.cpp): from 500 toward Player_2_Start the catapult's stones meet HelmsDeepGatehouseRight in their line of fire,
	// as RotWK's rules give, so the gate takes nothing (#64 measured 24 / 3245.99 / 262 before FLANK-1)
	CHECK(c.firstHit == -1);
	CHECK(c.healthAfter == 4000.0f);
	CHECK(c.deathFrame == -1);
}
