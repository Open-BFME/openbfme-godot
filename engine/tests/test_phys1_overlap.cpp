// OpenBFME retail tests for lane PHYS-1 (collision and separation). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP): pure RotWK 2.01 +
// BFME2 1.06, the shared retail world of the HUD / combat / structure tests.
//
// The overlap statistic (Phys1TestUtil.h) over a big melee between two barracks: two Gondor fighter hordes against two Mordor fighter hordes.
#include "doctest.h"
#include "Phys1TestUtil.h"
#include "StructureArena.h"

#include "GameLogic/Combat/ObjectWeapons.h"

#include <cstdio>

using namespace structtest;

namespace
{
struct MeleeRun
{
	phystest::OverlapStats stats;
	std::vector<std::uint32_t> hashes;
	std::vector<std::string> stops; // the logic report's stop lines at the end
};

// two Gondor fighter hordes (side A) attack two Mordor fighter hordes (side B) on the ground between a Gondor barracks (north) and a Mordor barracks (south); the hordes of a
// side stand 140 apart so their formations (about 100 deep) do not overlap at the start
MeleeRun bigMelee(int frames, bool hashes)
{
	SharedWorld &s = shared();
	MeleeRun out;
	Arena a(s, "FactionMen", "FactionMordor");
	a.place("GondorBarracks", 0, 600.0f, 640.0f);
	a.place("MordorBarracks", 1, 600.0f, 380.0f);
	Object *g1 = a.place("GondorFighterHorde", 0, 420.0f, 570.0f);
	Object *g2 = a.place("GondorFighterHorde", 0, 420.0f, 430.0f);
	Object *m1 = a.place("MordorFighterHorde", 1, 780.0f, 570.0f);
	Object *m2 = a.place("MordorFighterHorde", 1, 780.0f, 430.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	g1->getAIUpdateInterface()->aiAttackObject(m2, CMD_FROM_PLAYER); // crossing orders: the hordes cut across each other's lanes
	g2->getAIUpdateInterface()->aiAttackObject(m1, CMD_FROM_PLAYER);
	m1->getAIUpdateInterface()->aiAttackObject(g1, CMD_FROM_PLAYER);
	m2->getAIUpdateInterface()->aiAttackObject(g2, CMD_FROM_PLAYER);
	for (int f = 0; f < frames; ++f)
	{
		a.logic.runLogicFrame();
		out.stats.add(phystest::measure(a.logic));
		if (hashes)
		{
			out.hashes.push_back(a.logic.computeStateHash());
		}
	}
	out.stops = a.logic.report().stops;
	return out;
}

void print(const char *what, const phystest::OverlapStats &st)
{
	const double n = st.frames ? st.frames : 1;
	std::printf("  info: %s over %d frames: max enemy overlap %.2f (mean %.2f, %s), max allied overlap %.2f (mean %.2f, %s), same horde %.2f (mean %.2f), deepest unit centre in a structure %.2f (%s), deepest "
				"circle reach %.2f, at most %d units inside structures, %d frames with a unit inside\n",
		what, st.frames, st.worst.enemyOverlap, st.meanEnemyOverlap / n, st.worst.worstEnemyPair.c_str(), st.worst.allyOverlap, st.meanAllyOverlap / n,
		st.worst.worstAllyPair.c_str(), st.worst.sameHordeOverlap, st.meanSameHordeOverlap / n,
		st.worst.centreDepth, st.worst.worstStructurePair.c_str(), st.worst.circleDepth, st.worst.unitsInsideStructures, st.framesWithUnitsInside);
}
} // namespace

TEST_CASE("phys1 retail: the overlap statistic of a big melee between two barracks")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	const MeleeRun r = bigMelee(600, false);
	print("big melee", r.stats);
	CHECK(r.stats.frames == 600);
	// lane PHYS-1 round 2: the Amoeba's steps pass RW 0x6F1C90 (Pathfinder::crowdingAllowsStep). Without it the melee's members stepped into the barracks (measured: 5
	// units, a centre 22.44 deep, 118 of 600 frames with a unit inside; mean enemy overlap 6.27, mean allied overlap across hordes 8.32); with it no unit centre is inside a
	// structure shape (mean enemy overlap 2.85, allied 4.26). A scenario measurement on template geometry, not a bound. Lane SMOOTH-3: the members' angle goal
	// turns at the locomotor's rate (RW 0x5E98D6) instead of setting the heading, and the hub's near arm is RotWK's: mean enemy overlap 4.15, allied 0.71.
	CHECK(r.stats.worst.centreDepth == 0.0f);
	CHECK(r.stats.framesWithUnitsInside == 0);
	CHECK(r.stats.meanEnemyOverlap / r.stats.frames < 5.0);
	CHECK(r.stats.meanAllyOverlap / r.stats.frames < 6.0);
	// the collision pass reports what it is not (S-780) and that it left allied overlaps alone (S-781)
	size_t s780 = 0, s781 = 0;
	for (const std::string &line : r.stops)
	{
		s780 += line.rfind("[S-780] ", 0) == 0 ? 1u : 0u;
		s781 += line.rfind("[S-781] ", 0) == 0 ? 1u : 0u;
	}
	CHECK(s780 == 1);
	CHECK(s781 == 1);
}

// a horde of `templ` (side A) attacks a Mordor barracks, or (attack false) is ordered to the point straight across it
phystest::OverlapStats structureRun(const char *templ, bool attack, int frames)
{
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	Object *b = a.place("MordorBarracks", 1, 600.0f, 500.0f);
	Object *h = a.place(templ, 0, 400.0f, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	if (attack)
	{
		h->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
	}
	else
	{
		const Coord3D across{ 800.0f, 500.0f, 0.0f };
		h->getAIUpdateInterface()->aiMoveToPosition(across, CMD_FROM_PLAYER);
	}
	phystest::OverlapStats st;
	for (int f = 0; f < frames; ++f)
	{
		a.logic.runLogicFrame();
		st.add(phystest::measure(a.logic));
	}
	return st;
}

TEST_CASE("phys1 retail: the overlap statistic of hordes against a structure")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	for (const char *t : { "GondorFighterHorde", "GondorKnightHorde", "RohanRohirrimHorde", "MordorFighterHorde" })
	{
		if (!shared().world->things().findTemplate(t))
		{
			std::printf("  info: no template %s\n", t);
			continue;
		}
		const phystest::OverlapStats attack = structureRun(t, true, 400), across = structureRun(t, false, 400);
		print((std::string(t) + " attacks a barracks").c_str(), attack);
		print((std::string(t) + " moves across a barracks").c_str(), across);
		CHECK(attack.worst.unitsInsideStructures == 0);
		// a unit centre may touch the pathfinder's raster edge of the structure (a cell is an obstacle when the footprint covers it, RW 0x936B7D): round 4 measured one
		// Gondor fighter 1.46 inside for one frame while the horde walks around the barracks
		CHECK(across.worst.centreDepth < 1.5f);
	}
}

// stop S-781: the peak overlap of two members of one horde attacking a structure (a maximum over the run; it does not say why or for how long). Round 2 measured 16.00
// (two members on one point); with the attack path (S-784), allied clearing (S-785) and the Amoeba's goal reservation following its steps the peak is 13.50
TEST_CASE("phys1 retail: stop S-781: a horde attacking a structure still reaches a large member-circle overlap")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	const phystest::OverlapStats st = structureRun("GondorFighterHorde", true, 120);
	CHECK(st.worst.sameHordeOverlap > 10.0f);
	CHECK(st.worst.sameHordeOverlap < 15.9f); // no longer two members on one point
	CHECK(st.worst.unitsInsideStructures == 0);
}

// two runs of the big melee give the same state hash in every frame
TEST_CASE("phys1 retail: the big melee is deterministic")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	const MeleeRun a = bigMelee(200, true);
	const MeleeRun b = bigMelee(200, true);
	REQUIRE(a.hashes.size() == 200);
	CHECK(a.hashes == b.hashes);
	CHECK(a.stats.worst.enemyOverlap == b.stats.worst.enemyOverlap);
}

// stop S-783: the Amoeba's step search reports what of RW 0x6F1C90 is inference and that the attack approach is not retail's attack path request
TEST_CASE("phys1 retail: stop S-783: the big melee reports the Amoeba step test")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	const MeleeRun r = bigMelee(150, false);
	size_t n = 0;
	for (const std::string &line : r.stops)
	{
		n += line.rfind("[S-783] ", 0) == 0 ? 1u : 0u;
	}
	CHECK(n == 1);
}

namespace
{
struct StructureFight
{
	int deathFrame = -1;
	int steadyContributors = 0; // the members attacking with the structure in weapon reach, the most frequent count between frames 100 and the death
	int idleMembers = 0;        // members idle (state 0) at frame 150
	phystest::OverlapStats stats;
};

// lane PHYS-1: the structure fixture of test_hud_combat_structure.cpp (a Gondor fighter horde at (500, 500) attacks the structure at (640, 500))
StructureFight structureFight(const char *factionB, const char *templ)
{
	SharedWorld &s = shared();
	StructureFight out;
	Arena a(s, "FactionMen", factionB);
	Object *horde = a.place("GondorFighterHorde", 0, 500.0f, 500.0f);
	Object *b = a.place(templ, 1, 640.0f, 500.0f);
	const ObjectID id = b->getID();
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	horde->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
	std::map<int, int> histogram;
	for (int f = 0; f < 2000; ++f)
	{
		a.logic.runLogicFrame();
		out.stats.add(phystest::measure(a.logic));
		Object *o = a.logic.findObjectByID(id);
		if (!o || o->isEffectivelyDead())
		{
			out.deathFrame = f;
			break;
		}
		int contributors = 0, idle = 0;
		for (Object *m = a.logic.getFirstObject(); m; m = m->getNextObject())
		{
			if (m->getContainedBy() != horde || m->isEffectivelyDead() || !m->getAIUpdateInterface())
			{
				continue;
			}
			AIUpdateInterface *ai = m->getAIUpdateInterface();
			idle += ai->currentStateId() == AI_IDLE ? 1 : 0;
			if (ai->isAttacking() && m->getWeapons() && m->getWeapons()->isWithinAttackRange(*o))
			{
				++contributors;
			}
		}
		if (f >= 100)
		{
			++histogram[contributors];
		}
		if (f == 150)
		{
			out.idleMembers = idle;
		}
	}
	int bestCount = -1;
	for (const auto &kv : histogram)
	{
		if (kv.second > bestCount)
		{
			bestCount = kv.second;
			out.steadyContributors = kv.first;
		}
	}
	return out;
}
} // namespace

// lane PHYS-1 round 3: the contributors of the structure fights of test_hud_combat_structure.cpp (Sol's r2 trace: with the Amoeba step test alone 8 / 11 / 8 / 8 / 7 / 7 / 8
// members contribute, 15 with the test bypassed)
TEST_CASE("phys1 retail: the contributors of a horde attacking each faction's barracks")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	const struct
	{
		const char *faction, *templ;
	} rows[] = { { "FactionMen", "GondorBarracks" }, { "FactionElves", "ElvenBarracks" }, { "FactionDwarves", "DwarfBarracks" }, { "FactionIsengard", "IsengardUrukPit" },
		{ "FactionMordor", "MordorBarracks" }, { "FactionWild", "GoblinCave" }, { "FactionAngmar", "AngmarBarracks" } };
	for (const auto &r : rows)
	{
		const StructureFight f = structureFight(r.faction, r.templ);
		std::printf("  info: Gondor horde vs %s: died at %d, steady contributors %d, idle members at frame 150 %d, deepest circle reach %.2f, units inside %d, same horde %.2f (mean %.2f)\n",
			r.templ, f.deathFrame, f.steadyContributors, f.idleMembers, f.stats.worst.circleDepth, f.stats.worst.unitsInsideStructures, f.stats.worst.sameHordeOverlap,
			f.stats.meanSameHordeOverlap / (f.stats.frames ? f.stats.frames : 1));
		CHECK(f.deathFrame > 0);
		CHECK(f.stats.worst.unitsInsideStructures == 0);
	}
}

// stops S-784 / S-785 / S-787: a ranged soldier asks for attack paths, the big melee runs the melee machine; a hero walking straight through a parked allied horde clears it (its path is blocked by allies, RW 0x6F503B
// asks the members to move away, RW 0x66DA5F); the report says what of both is not ported
TEST_CASE("phys1 retail: stop S-784 / S-785: attack paths and allied clearing report themselves")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	// round 6: the fighters' members attack through the melee machine (S-787) and never ask for attack paths; a ranged soldier still does (kinds 3 / 4: the generic
	// machine's approach, RW 0x749A3E -> 0x663802)
	const MeleeRun r = bigMelee(150, false);
	size_t s784 = 0, s787 = 0;
	for (const std::string &line : r.stops)
	{
		s784 += line.rfind("[S-784] ", 0) == 0 ? 1u : 0u;
		s787 += line.rfind("[S-787] ", 0) == 0 ? 1u : 0u;
	}
	CHECK(s787 == 1);
	CHECK(s784 == 0);
	{
		Arena ra(shared(), "FactionMen", "FactionMordor");
		Object *archer = ra.place("GondorArcher", 0, 300.0f, 500.0f);
		Object *orc = ra.place("MordorFighter", 1, 900.0f, 500.0f);
		ra.logic.runLogicFrame();
		orc->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
		archer->getAIUpdateInterface()->aiAttackObject(orc, CMD_FROM_PLAYER);
		for (int f = 0; f < 30; ++f)
		{
			ra.logic.runLogicFrame();
		}
		size_t ranged784 = 0;
		for (const std::string &line : ra.logic.report().stops)
		{
			ranged784 += line.rfind("[S-784] ", 0) == 0 ? 1u : 0u;
		}
		CHECK(ranged784 == 1);
	}
	// the requester must not be PATH_THROUGH_EACH_OTHER like the fighters (RW 0x6F503B skips such pairs: the Gondor heroes and infantry walk through each other)
	const char *hero = nullptr;
	for (const char *t : { "GondorTrebuchet", "GondorAragorn", "GondorFaramir" })
	{
		const ThingTemplate *tt = shared().world->things().findTemplate(t);
		if (tt)
		{
			Arena probe(shared(), "FactionMen", "FactionMordor");
			Object *o = probe.place(t, 0, 300.0f, 300.0f);
			if (!o->isKindOfName("PATH_THROUGH_EACH_OTHER") && o->getAIUpdateInterface())
			{
				hero = t;
				break;
			}
		}
	}
	REQUIRE(hero != nullptr);
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *horde = a.place("GondorFighterHorde", 0, 500.0f, 500.0f);
	Object *h = a.place(hero, 0, 380.0f, 510.0f);
	for (int f = 0; f < 5; ++f)
	{
		a.logic.runLogicFrame();
	}
	// the hero walks around the parked horde (an allied cell costs 14 more, the path is not blocked); RW 0x6F503B is driven directly with a path through the horde
	// that the search marked blocked by an ally
	Path *through = new Path();
	const Coord3D p0{ 380.0f, 510.0f, 0.0f }, p1{ 640.0f, 510.0f, 0.0f };
	through->prependNode(&p1, LAYER_GROUND);
	through->prependNode(&p0, LAYER_GROUND);
	through->setBlockedByAlly(true);
	h->getAIUpdateInterface()->mover().setPath(through);
	CHECK(a.ai->moveAllies(*h->getAIUpdateInterface(), *through, false));
	int movedAway = 0;
	phystest::OverlapStats st;
	for (int f = 0; f < 150; ++f)
	{
		a.logic.runLogicFrame();
		st.add(phystest::measure(a.logic));
		for (Object *m = a.logic.getFirstObject(); m; m = m->getNextObject())
		{
			if (m->getContainedBy() == horde && m->getAIUpdateInterface() &&
				(m->getAIUpdateInterface()->currentStateId() == AI_MOVE_OUT_OF_THE_WAY || m->getAIUpdateInterface()->stateMachine().temporaryStateId() == AI_MOVE_OUT_OF_THE_WAY))
			{
				++movedAway;
			}
		}
	}
	print((std::string(hero) + " walks through a parked allied horde").c_str(), st);
	std::printf("  info: member-frames in AI_MOVE_OUT_OF_THE_WAY: %d\n", movedAway);
	size_t s785 = 0;
	for (const std::string &line : a.logic.report().stops)
	{
		s785 += line.rfind("[S-785] ", 0) == 0 ? 1u : 0u;
	}
	CHECK(s785 == 1);
	// RW 0x66DA5F: a member steps aside (state 26), or an idle member IN_FORMATION_TEMPLATE lets the requester path through units (the requester's AI + 0x3BA)
	std::printf("  info: %s may path through units: %d\n", hero, (int)h->getAIUpdateInterface()->canPathThroughUnits());
	CHECK((movedAway > 0 || h->getAIUpdateInterface()->canPathThroughUnits()));
}

// stop S-786: RW 0x6639CF (AIMover::requestMeleeApproachPath) called directly on a Gondor fighter (its retail caller, AIAttackMeleeApproachState 0xE1, is not ported):
// a far target queues the request with its flags and reserves a destination at most MeleeApproachDist (48) from the target point; a target within 20 is refused after the
// reservation (nothing queued); repeat runs hash alike; the report names what is not retail
TEST_CASE("phys1 retail: stop S-786: requestMeleeApproachPath queues far requests, refuses near ones after reserving, and reports itself")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	auto run = [](std::vector<std::uint32_t> &hashes, bool &farQueued, float &reservedToTarget, bool &nearRefused, bool &nearReserved, std::vector<std::string> &stops) {
		Arena a(shared(), "FactionMen", "FactionMordor");
		Object *u = a.place("GondorFighter", 0, 300.0f, 500.0f);
		a.logic.runLogicFrame();
		a.logic.runLogicFrame();
		AIMover &mv = u->getAIUpdateInterface()->mover();
		const Coord3D farTarget{ 600.0f, 505.0f, 0.0f };
		farQueued = mv.requestMeleeApproachPath(farTarget, false) && mv.meleeApproachPending();
		const ICoord2D c = a.ai->pathfinder().pathfindGoalCell(u->getID());
		const float cx = c.x * 10.0f + 5.0f, cy = c.y * 10.0f + 5.0f;
		reservedToTarget = std::sqrt((cx - farTarget.x) * (cx - farTarget.x) + (cy - farTarget.y) * (cy - farTarget.y));
		hashes.push_back(a.logic.computeStateHash());
		for (int f = 0; f < 10; ++f)
		{
			a.logic.runLogicFrame();
			hashes.push_back(a.logic.computeStateHash());
		}
		const Coord3D here = *u->getPosition();
		const Coord3D nearTarget{ here.x + 12.0f, here.y, 0.0f };
		nearRefused = !mv.requestMeleeApproachPath(nearTarget, true) && !mv.meleeApproachPending();
		const ICoord2D c2 = a.ai->pathfinder().pathfindGoalCell(u->getID());
		nearReserved = std::abs(c2.x * 10.0f + 5.0f - nearTarget.x) <= 10.0f;
		hashes.push_back(a.logic.computeStateHash());
		stops = a.logic.report().stops;
	};
	std::vector<std::uint32_t> h1, h2;
	bool farQueued = false, nearRefused = false, nearReserved = false;
	float reserved = 0.0f;
	std::vector<std::string> stops;
	run(h1, farQueued, reserved, nearRefused, nearReserved, stops);
	CHECK(farQueued);
	std::printf("  info: requestMeleeApproachPath reserved a destination %.1f from the target point (MeleeApproachDist 48)\n", reserved);
	CHECK(reserved <= 48.0f + 10.0f);
	CHECK(nearRefused);
	CHECK(nearReserved); // RW: the reservation (RW 0x68B3AB) comes before the 20 test
	size_t s786 = 0;
	for (const std::string &line : stops)
	{
		s786 += line.rfind("[S-786] ", 0) == 0 ? 1u : 0u;
	}
	CHECK(s786 == 1);
	bool a1 = false, a2 = false, a3 = false;
	float r2 = 0.0f;
	run(h2, a1, r2, a2, a3, stops);
	CHECK(h1 == h2);
}
