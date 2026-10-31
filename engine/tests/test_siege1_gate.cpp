// OpenBFME unit tests, lane SIEGE-1: siege weapons against the fortress gates after lane FLANK-1 made projectiles meet objects in flight. GPL-3.0.
//
// Test from community PR #64 (NotNBR, test_siege1_attack.cpp and its fixture Siege1TestUtil.h, kept unchanged): a Mordor catapult made 500 from Helm's Deep's
// main gate toward Player_2_Start never damaged the gate since lane FLANK-1. The cause was the pair rule of the in-flight contact test: RotWK runs a pair's
// narrow test from the side of its lower ObjectID (RW 0xB6D730 orders the pair by the id RW 0x670216 returns, RW 0xB6D060 calls first->test(second's object),
// RW 0x68C7B2 -> 0x68BA2A -> GeometryInfo::CollidesWith RW 0xAD2CE0 with the first object's own GeometryInfo), and RW 0xAD2CE0's height rules are not
// symmetric. FLANK-1 always tested from the stone's side, so the lintel of the gate arch (HelmsDeepGatehouseCenter, a BOX at offset z 110, height 30) reached
// down to its base and caught every stone falling on the gate. The frames below are this port's deterministic results (seed 9): regression pins, not retail
// measurements. SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include "GameLogic/Combat/CombatState.h"

#include <cstdio>

using namespace siege1test;

namespace
{
const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";
const char *const kMinasTirith = "maps/map wor minas tirith/map wor minas tirith.map";

struct GateRun
{
	bool ordered = false;
	int shots = 0, contacts = 0;         // projectiles launched; in-flight contacts canCollideWith accepted (CombatState counters)
	int contactsWhileStanding = 0;       // the in-flight contacts before the gate died (after it, the weapon picks other targets)
	int firstHit = -1, deathFrame = -1;  // the gate's first damage and death frames, -1: none
	float healthAfter = 0.0f;
	float distAtHit = -1.0f;             // the unit's 2D distance from the gate at its first damage
	std::string otherDamaged;            // the other objects that lost health, in object list order
};

// `unit` made `d` in front of the gate named `gateName` (along the gate's own facing, on the side of Player_2_Start), ordered to attack it for `frames` frames
GateRun attackFromFront(starttest::Shared &s, const char *map, const char *gateName, const char *unit, float d, int frames)
{
	GateRun r;
	FortressGame g(s, map, false);
	// The map's creeps are removed for their random draws, not for anything they do to the siege: their lairs' spawns (production exits) and the
	// creeps' guard scans draw from the game's random numbers, so correcting the exit paths moves when those draws happen and with them the
	// stones' scatter. Both sides are idle humans.
	Player *attacker = g.ofFaction("FactionMordor");
	Player *defender = g.ofFaction("FactionMen");
	REQUIRE(attacker);
	REQUIRE(defender);
	std::vector<Object *> creeps;
	for (Object *x = g.game->logic().getFirstObject(); x; x = x->getNextObject())
	{
		if (attacker->getRelationship(x->getTeam()) == ENEMIES && defender->getRelationship(x->getTeam()) == ENEMIES)
		{
			creeps.push_back(x);
		}
	}
	for (Object *x : creeps) g.game->logic().destroyObject(x);
	g.frames(2);
	Object *gate = g.named(gateName);
	REQUIRE(gate);
	Player *me = g.ofFaction("FactionMordor");
	REQUIRE(me);
	g.game->shroud().revealMapForPlayerPermanently(me->getPlayerIndex());
	const Waypoint *w = g.game->logic().terrain()->findWaypointByName("Player_2_Start");
	REQUIRE(w);
	const Coord3D tp = *gate->getPosition();
	float ax = std::cos(gate->getOrientation()), ay = std::sin(gate->getOrientation());
	if (ax * (w->location.x - tp.x) + ay * (w->location.y - tp.y) < 0.0f)
	{
		ax = -ax;
		ay = -ay;
	}
	std::string error;
	Object *o = g.game->createObject(unit, me->getPlayerIndex(), Coord3D{ tp.x + ax * d, tp.y + ay * d, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(o, error);
	g.frames(5);
	std::vector<std::pair<ObjectID, float>> health;
	for (Object *x = g.game->logic().getFirstObject(); x; x = x->getNextObject())
	{
		health.emplace_back(x->getID(), healthOf(x));
	}
	const CombatState::Counters &c = g.game->logic().combat().counters();
	const unsigned long long launched = c.projectilesLaunched, hits = c.projectileContactHits;
	AIUpdateInterface *ai = o->getAIUpdateInterface();
	r.ordered = ai && ai->aiAttackObject(gate, CMD_FROM_PLAYER);
	const ObjectID gid = gate->getID();
	const float before = healthOf(gate);
	for (int f = 1; f <= frames; ++f)
	{
		g.frames(1);
		Object *t = g.game->logic().findObjectByID(gid);
		if (r.firstHit < 0 && (!t || healthOf(t) < before))
		{
			r.firstHit = f;
			r.distAtHit = dist2(*o->getPosition(), tp);
		}
		if (r.deathFrame < 0 && (!t || t->isEffectivelyDead()))
		{
			r.deathFrame = f;
		}
		if (r.deathFrame < 0)
		{
			r.contactsWhileStanding = (int)(c.projectileContactHits - hits);
		}
	}
	Object *t = g.game->logic().findObjectByID(gid);
	r.healthAfter = t ? healthOf(t) : 0.0f;
	r.shots = (int)(c.projectilesLaunched - launched);
	r.contacts = (int)(c.projectileContactHits - hits);
	for (const std::pair<ObjectID, float> &h : health)
	{
		Object *x = g.game->logic().findObjectByID(h.first);
		if (x && x != o && h.first != gid && healthOf(x) < h.second)
		{
			r.otherDamaged += x->getTemplate()->getName() + " ";
		}
	}
	MESSAGE(std::string(unit) << " in front of " << std::string(gateName) << " from " << d << ": ordered " << r.ordered << ", shots " << r.shots << ", contacts " << r.contacts << " (" << r.contactsWhileStanding << " while it stood), first damage "
				  << r.firstHit << " from " << r.distAtHit << ", death " << r.deathFrame << ", health at the end " << r.healthAfter << ", others damaged: " << r.otherDamaged);
	return r;
}
// a pinned run made twice: the two runs must agree (the port is deterministic; a difference is a bug, not a re-pin)
GateRun pinnedRun(starttest::Shared &s, const char *map, const char *gateName, const char *unit, float d, int frames)
{
	const GateRun a = attackFromFront(s, map, gateName, unit, d, frames);
	const GateRun b = attackFromFront(s, map, gateName, unit, d, frames);
	CHECK(a.shots == b.shots);
	CHECK(a.contacts == b.contacts);
	CHECK(a.contactsWhileStanding == b.contactsWhileStanding);
	CHECK(a.firstHit == b.firstHit);
	CHECK(a.deathFrame == b.deathFrame);
	CHECK(a.distAtHit == b.distAtHit);
	CHECK(a.otherDamaged == b.otherDamaged);
	return a;
}
} // namespace

TEST_CASE("siege1 gate: #64's catapult 500 from Helm's Deep's gate toward Player_2_Start throws into the gatehouse tower in its line of fire")
{
	// The direction toward Player_2_Start is 26 degrees off the gate's facing: HelmsDeepGatehouseRight (a BOX 74 x 95, height 140, on the ground 130 from
	// the gate) stands in the stone's path, whose arc is FirstHeight 53 above the highest terrain along the line (RW 0x85E658 -> TheTerrainLogic vslot 0x40,
	// RW 0x466621: the height map only). The tower was made before the stone, so the pair is tested from the tower's side, and a stone at z ~209 is inside
	// its 100 .. 240: the warhead's ProjectileCollidesWith (MONSTERS NEUTRAL STRUCTURES) meets it (RW 0x6CC79C; the WALK_ON_TOP_OF_WALL pass-over needs the
	// gate on a raised layer, RW 0x68BBE0 > 1). Before FLANK-1 the stone flew through and hit the gate at frame 24 (the #64 pin).
	OPENBFME_REQUIRE_START(s);
	for (int run = 0; run < 2; ++run) // twice: the same result both times
	{
		const AttackResult r = siegeAttack(*s, kHelmsDeep, "Main Gate", "MordorCatapult", false, 500.0f, 600);
		MESSAGE("MordorCatapult toward Player_2_Start: first damage " << r.firstHit << ", health " << r.healthBefore << " -> " << r.healthAfter);
		CHECK(r.ordered);
		CHECK(r.target == "RBHelmsDeepGateDoorBig");
		CHECK(r.firstHit == -1);
		CHECK(r.healthAfter == 4000.0f);
	}
}

// SIEGEFIX-1: the pins below were measured once more with the fortress side a second human slot that gives no order (Siege1TestUtil.h, aiDefender false).
// SIEGE-1 measured them with the Easy AI's porters walking about and drawing from the game's random numbers, and with the porters snapped onto a cell
// centre on their first idle frame; RotWK has no such snap (RW 0x745A64 only updates the pathfinder's goal; lane OFFMAP-1 removed it), and the porters'
// moves changed the stones' scatter and, once the gate fell, gave the weapon new targets. Without the AI the runs depend only on the weapon and the
// structures. `contactsWhileStanding` counts the in-flight contacts before the gate fell: the pair rule's guard (lane SIEGE-1: with the stone's side
// testing every pair, the arch caught the stones meant for the gate, e.g. 17 contacts in 17 shots); the non-zero ones are the side towers'
// (HelmsDeepGatehouseLeft, MinisGate1) but for the Dwarven catapult's two short stones at the arch's front (see there).

TEST_CASE("siege1 gate: #64's catapult made inside its MinimumAttackRange of Helm's Deep's gate backs off and breaks it (the stones clear the arch)")
{
	// SIEGEFIX-1: made 120 in front of the gate along its facing. #64's spot (100 toward Player_2_Start) now backs off up the causeway's raised walkway
	// (lanes WALL-1 / WALL-2: z ~184 instead of the ground's ~138) and every stone from there meets the arch (HelmsDeepGatehouseCenter): the gate does not
	// fall in 900 frames; that placement is reported for a follow-up, not pinned
	OPENBFME_REQUIRE_START(s);
	const GateRun r = pinnedRun(*s, kHelmsDeep, "Main Gate", "MordorCatapult", 120.0f, 600);
	CHECK(r.ordered);
	CHECK(r.distAtHit > 150.0f); // tolerance: it fired from outside its minimum range (150)
	// merge queue q31 (measured at each merge point: #68's ambient wanderers draw from the game's random numbers, so the stones scatter differently): two
	// contacts while the gate stands (was one), first hit 48 (49), death 321 (287)
	// merge queue q32 (#159's ramp cells, RW 0x93527F, move the stones' scatter at Helm's Deep; measured once on the merged tree, the change appears at #159's merge point): one contact while the gate stands (two), first hit and death unchanged
	// merge queue q33 (MODCOMPAT-MOD1's modules move the stones' scatter; measured at each merge point and named with a contact log): two contacts while the
	// gate stands, both the gatehouse in the line of fire (HelmsDeepGatehouseLeft at frame 259, HelmsDeepGatehouseCenter at 294), first hit 48, death 319
	CHECK(r.contactsWhileStanding == 2); // HelmsDeepGatehouseLeft, the tower beside the gate, and HelmsDeepGatehouseCenter
	// lane MODCOMPAT-MOD1 (GeometryUpgrade: every object reads its own GeometryInfo, RW Object + 0xA8, for its combat circle and sphere (+ 0xB8 / + 0xBC), partition cells and pathfinder footprint, and the gate's open / closed switch runs the full refresh RW 0x68B244; measured at its commit, a CI host):
	// first hit 49 -> 48, the gate's death 287 -> 252
	CHECK(r.firstHit == 48);
	CHECK(r.deathFrame == 319);
}

TEST_CASE("siege1 gate: the stone and bolt throwers in front of Helm's Deep's gate break it; the arch takes no stone meant for the gate")
{
	OPENBFME_REQUIRE_START(s);
	const GateRun c = pinnedRun(*s, kHelmsDeep, "Main Gate", "MordorCatapult", 400.0f, 600);
	CHECK(c.ordered);
	// merge queue q31 (measured at each merge point; #68's ambient wanderers draw from the game's random numbers and move every stone's scatter): the
	// catapult 6 shots (9), no contact (1), first hit 20 (17), death 190 (290); the trebuchet one contact while the gate stands (0; what it met was not identified), first hit 19 (17),
	// death 387 (385); the Dwarven catapult no contact (4), death 335 (401); the ballista death 428 (389)
	// merge queue q32 (#159's ramp cells, RW 0x93527F, move the stones' scatter at Helm's Deep; measured once on the merged tree, the change appears at #159's merge point): the catapult 9 shots (6), death 292 (190); the trebuchet
	// unchanged; the Dwarven catapult death 369 (335); the ballista no contact while the gate stands (1), death 389 (428)
	// merge queue q34 (#161's block search, block zones RW 0x9389A6 and raised branch RW 0x9389CE, moves the AI defender and the random stream at Helm's
	// Deep; measured once on the merged tree and at each merge point, SPELLFIX-1 and #160 keep the archive's values): the catapult 10 shots (9), death 326
	// (292); the ballista two contacts while the gate stands (0; what they met was not identified, as the trebuchet's one since q31), death 467 (389)
	CHECK(c.shots == 10);
	CHECK(c.contactsWhileStanding == 0);
	CHECK(c.firstHit == 20);
	CHECK(c.deathFrame == 326);
	const GateRun t = pinnedRun(*s, kHelmsDeep, "Main Gate", "GondorTrebuchet", 400.0f, 600);
	CHECK(t.contactsWhileStanding == 1);
	CHECK(t.firstHit == 19);
	CHECK(t.deathFrame == 387);
	const GateRun dc = pinnedRun(*s, kHelmsDeep, "Main Gate", "DwarvenCatapult", 400.0f, 600);
	// its two stones a throw (until q31): both met HelmsDeepGatehouseLeft at frame 102 and, at frame 341, the front of the arch (HelmsDeepGatehouseCenter) 51 before
	// its centre at z 174, below the lintel (the stones fell short; not the lintel reaching down, which the pair rule removed)
	CHECK(dc.contactsWhileStanding == 0);
	CHECK(dc.deathFrame == 369);
	const GateRun b = pinnedRun(*s, kHelmsDeep, "Main Gate", "IsengardBallista", 400.0f, 600);
	// merge queue q27: one contact while the gate stands (was 0) and death 311 -> 389, LOS-1 (the terrain line of sight with the wall layers' heights;
	// measured at each merge point)
	CHECK(b.contactsWhileStanding == 2);
	CHECK(b.deathFrame == 467);
}

TEST_CASE("siege1 gate: Mordor's catapult and Gondor's trebuchet in front of Minas Tirith's gate break it")
{
	OPENBFME_REQUIRE_START(s);
	const GateRun c = pinnedRun(*s, kMinasTirith, "Gate", "MordorCatapult", 400.0f, 600);
	CHECK(c.ordered);
	// merge queue q28 (measured once on the merged tree and at each merge point): the catapult's stones no longer meet MinisGate1 while the gate stands
	// (1 -> 0) and the first hit is at 20 (21); the trebuchet breaks the gate at 158 (203)
	CHECK(c.contactsWhileStanding == 0);
	// With map creeps removed, both first-hit engine pins are frame 21.
	// Contact limits and the original gate-death pins remain unchanged.
	CHECK(c.firstHit == 21);
	CHECK(c.deathFrame == 123);
	const GateRun t = pinnedRun(*s, kMinasTirith, "Gate", "GondorTrebuchet", 400.0f, 600);
	CHECK(t.contactsWhileStanding == 0);
	CHECK(t.firstHit == 21);
	CHECK(t.deathFrame == 158);
}

TEST_CASE("siege1 gate: the rams, Grond and the mumak in front of the gates (no projectile: unchanged by the pair rule)")
{
	OPENBFME_REQUIRE_START(s);
	CHECK(pinnedRun(*s, kHelmsDeep, "Main Gate", "IsengardBatteringRam", 250.0f, 600).deathFrame == 99);
	CHECK(pinnedRun(*s, kHelmsDeep, "Main Gate", "DwarvenDemolisher", 250.0f, 600).deathFrame == 102); // lane MODCOMPAT-MOD1: 101 -> 102 (GeometryUpgrade's own GeometryInfo per object, RW 0x68B244)
	CHECK(pinnedRun(*s, kHelmsDeep, "Main Gate", "MordorGrond", 250.0f, 600).deathFrame == 128);
	CHECK(pinnedRun(*s, kHelmsDeep, "Main Gate", "MordorMumakil", 250.0f, 600).deathFrame == 247); // merge queue q27: 246 -> 247, LOS-1
	CHECK(pinnedRun(*s, kMinasTirith, "Gate", "IsengardBatteringRam", 250.0f, 600).deathFrame == 57);
	CHECK(pinnedRun(*s, kMinasTirith, "Gate", "MordorGrond", 250.0f, 600).deathFrame == 83);
}
