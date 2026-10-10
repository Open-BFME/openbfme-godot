// OpenBFME retail tests for lane MOVE-3 (community feedback FB-0012: hostile hordes running through each other; FB-0006: two archer hordes standing on one spot).
// They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP): pure RotWK 2.01 + BFME2 1.06, the shared retail world of the structure tests.
//
// The statistic is PHYS-1's (Phys1TestUtil.h: the members' bounding circles in the plane). The scenarios are move orders, not attacks.
#include "doctest.h"
#include "Phys1TestUtil.h"
#include "StructureArena.h"

#include "GameLogic/AI/AICommands.h"
#include "GameLogic/GameLogicDispatch.h"
#include "Common/Money.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace structtest;

namespace
{
struct HordeCentre
{
	float x = 0.0f, y = 0.0f;
	int n = 0;
};

// the mean position of the horde's living members (the horde object itself is the formation's anchor)
HordeCentre membersCentre(Object *horde)
{
	HordeCentre c;
	if (horde == nullptr || horde->getContain() == nullptr)
	{
		return c;
	}
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		if (m->isEffectivelyDead())
		{
			continue;
		}
		c.x += m->getPosition()->x;
		c.y += m->getPosition()->y;
		c.n += 1;
	}
	if (c.n)
	{
		c.x /= (float)c.n;
		c.y /= (float)c.n;
	}
	return c;
}

void print(const char *what, const phystest::OverlapStats &st)
{
	const double n = st.frames ? st.frames : 1;
	std::printf("  info: %s over %d frames: max enemy overlap %.2f (mean %.2f, %s), max allied overlap across hordes %.2f (mean %.2f, %s), same horde %.2f (mean %.2f)\n", what,
		st.frames, st.worst.enemyOverlap, st.meanEnemyOverlap / n, st.worst.worstEnemyPair.c_str(), st.worst.allyOverlap, st.meanAllyOverlap / n, st.worst.worstAllyPair.c_str(),
		st.worst.sameHordeOverlap, st.meanSameHordeOverlap / n);
}

struct CrossRun
{
	phystest::OverlapStats stats;
	int frames = 0;
	float endA = 0.0f, endB = 0.0f; // how far each horde's members got along its move (their centre's progress)
	int deepPairFrames = 0;          // the enemy member pairs overlapping by more than half the smaller circle, summed over the frames
	float minHordeDistance = 1e9f;   // the closest the two horde objects came
	float maxSideA = 0.0f, maxSideB = 0.0f; // the largest sideways step of each horde object off the line of its move
};

// the enemy member pairs whose circles overlap by more than half the smaller radius
int deepEnemyPairs(GameLogic &logic)
{
	const std::vector<phystest::Body> all = phystest::bodies(logic);
	int n = 0;
	for (size_t i = 0; i < all.size(); ++i)
	{
		for (size_t j = i + 1; j < all.size(); ++j)
		{
			const phystest::Body &a = all[i], &b = all[j];
			if (a.structure || b.structure || !a.owner || !b.owner || a.owner->getRelationship(b.owner) != ENEMIES)
			{
				continue;
			}
			const float dx = a.x - b.x, dy = a.y - b.y;
			const float pen = a.r + b.r - std::sqrt(dx * dx + dy * dy);
			n += pen > 0.5f * std::min(a.r, b.r) ? 1 : 0;
		}
	}
	return n;
}

enum class Cross
{
	HeadOn,        // A from the west, B from the east, on one line
	Perpendicular, // A from the west, B from the south, meeting in the middle
	Idle,          // A from the west through B standing in the middle
};

const char *crossName(Cross c)
{
	return c == Cross::HeadOn ? "head-on" : c == Cross::Perpendicular ? "perpendicular" : "through an idle horde";
}

// side A's horde moves from (300, 600) to (900, 600); side B's horde per `mode`. Move orders only (no attack)
CrossRun crossing(const char *ta, const char *tb, Cross mode, int frames)
{
	SharedWorld &s = shared();
	CrossRun out;
	Arena ar(s, "FactionMen", "FactionMordor");
	const float y = 600.0f;
	// head-on: each goal lies 100 short of the other horde's start, so neither order's path has to avoid the other horde where it stands
	const Coord3D bStart = mode == Cross::HeadOn ? Coord3D{ 1000.0f, y, 0.0f } : mode == Cross::Perpendicular ? Coord3D{ 600.0f, 300.0f, 0.0f } : Coord3D{ 600.0f, y, 0.0f };
	const Coord3D bGoal = mode == Cross::HeadOn ? Coord3D{ 300.0f, y, 0.0f } : Coord3D{ 600.0f, 900.0f, 0.0f };
	Object *a = ar.place(ta, 0, 200.0f, y);
	Object *b = ar.place(tb, 1, bStart.x, bStart.y);
	if (mode == Cross::Perpendicular)
	{
		b->setOrientation(1.5707963f);
	}
	ar.logic.runLogicFrame();
	ar.logic.runLogicFrame();
	a->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 900.0f, y, 0.0f }, CMD_FROM_PLAYER);
	if (mode != Cross::Idle)
	{
		b->getAIUpdateInterface()->aiMoveToPosition(bGoal, CMD_FROM_PLAYER);
	}
	for (int f = 0; f < frames; ++f)
	{
		ar.logic.runLogicFrame();
		out.stats.add(phystest::measure(ar.logic));
		out.deepPairFrames += deepEnemyPairs(ar.logic);
		const Coord3D &pa = *a->getPosition(), &pb = *b->getPosition();
		out.minHordeDistance = std::min(out.minHordeDistance, std::sqrt((pa.x - pb.x) * (pa.x - pb.x) + (pa.y - pb.y) * (pa.y - pb.y)));
		out.maxSideA = std::max(out.maxSideA, std::fabs(pa.y - y));
		out.maxSideB = std::max(out.maxSideB, mode == Cross::Perpendicular ? std::fabs(pb.x - bStart.x) : std::fabs(pb.y - bStart.y));
	}
	const HordeCentre ca = membersCentre(a), cb = membersCentre(b);
	out.endA = ca.x - 200.0f;
	out.endB = mode == Cross::HeadOn ? bStart.x - cb.x : mode == Cross::Perpendicular ? cb.y - bStart.y : 0.0f;
	out.frames = frames;
	return out;
}
} // namespace

TEST_CASE("move3 retail: hostile hordes crossing with move orders (FB-0012)")
{
	if (!hudtest::haveWorld("move3 retail"))
	{
		return;
	}
	struct Case
	{
		const char *a, *b;
		Cross mode;
	};
	for (const Case &c : { Case{ "GondorFighterHorde", "MordorFighterHorde", Cross::HeadOn }, Case{ "GondorFighterHorde", "MordorFighterHorde", Cross::Perpendicular },
			 Case{ "GondorFighterHorde", "MordorFighterHorde", Cross::Idle }, Case{ "GondorKnightHorde", "MordorFighterHorde", Cross::HeadOn },
			 Case{ "GondorKnightHorde", "MordorFighterHorde", Cross::Perpendicular }, Case{ "GondorArcherHorde", "MordorArcherHorde", Cross::HeadOn },
			 Case{ "GondorArcherHorde", "MordorArcherHorde", Cross::Perpendicular } })
	{
		const CrossRun r = crossing(c.a, c.b, c.mode, 300);
		char what[320];
		std::snprintf(what, sizeof what, "%s -> %s %s (A progress %.1f, B progress %.1f, deep enemy pair-frames %d, hordes min distance %.1f, sideways A %.1f B %.1f)", c.a, c.b,
			crossName(c.mode), r.endA, r.endB, r.deepPairFrames, r.minHordeDistance, r.maxSideA, r.maxSideB);
		print(what, r.stats);
	}
}

namespace
{
struct SweepTotals
{
	int runs = 0, deepPairFrames = 0, runsWithDeepPairs = 0, runsCloserThan60 = 0;
	float minHordeDistance = 1e9f;
	bool patchStopRaised = false; // the pathfinder reported S-1830 (a blocked horde patched its path)
};

bool raised(const std::vector<std::string> &lines, const char *prefix)
{
	for (const std::string &l : lines)
	{
		if (l.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

// B's line is shifted sideways by `side` and its start moved back by `back` along its line (both moving with move orders)
SweepTotals sweep(const char *ta, const char *tb, Cross mode)
{
	SweepTotals t;
	for (float side : { -60.0f, -30.0f, 0.0f, 30.0f, 60.0f })
	{
		for (float back : { 0.0f, 50.0f, 100.0f })
		{
			SharedWorld &s = shared();
			Arena ar(s, "FactionMen", "FactionMordor");
			const float y = 600.0f;
			Coord3D bStart, bGoal;
			if (mode == Cross::HeadOn)
			{
				bStart = Coord3D{ 1000.0f + back, y + side, 0.0f };
				bGoal = Coord3D{ 300.0f, y + side, 0.0f };
			}
			else
			{
				bStart = Coord3D{ 600.0f + side, 300.0f - back, 0.0f };
				bGoal = Coord3D{ 600.0f + side, 1000.0f, 0.0f };
			}
			Object *a = ar.place(ta, 0, 200.0f, y);
			Object *b = ar.place(tb, 1, bStart.x, bStart.y);
			b->setOrientation(mode == Cross::HeadOn ? 3.14159265f : 1.5707963f);
			ar.logic.runLogicFrame();
			ar.logic.runLogicFrame();
			a->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 900.0f, y, 0.0f }, CMD_FROM_PLAYER);
			b->getAIUpdateInterface()->aiMoveToPosition(bGoal, CMD_FROM_PLAYER);
			int deep = 0;
			float minD = 1e9f;
			for (int f = 0; f < 250; ++f)
			{
				ar.logic.runLogicFrame();
				deep += deepEnemyPairs(ar.logic);
				const Coord3D &pa = *a->getPosition(), &pb = *b->getPosition();
				minD = std::min(minD, std::sqrt((pa.x - pb.x) * (pa.x - pb.x) + (pa.y - pb.y) * (pa.y - pb.y)));
			}
			t.patchStopRaised = t.patchStopRaised || raised(ar.ai->pathfinder().stops(), "S-1830 ");
			t.runs += 1;
			t.deepPairFrames += deep;
			t.runsWithDeepPairs += deep > 0 ? 1 : 0;
			t.runsCloserThan60 += minD < 60.0f ? 1 : 0;
			t.minHordeDistance = std::min(t.minHordeDistance, minD);
		}
	}
	return t;
}
} // namespace

TEST_CASE("move3 retail: crossing sweep of hostile infantry hordes with move orders (FB-0012)")
{
	if (!hudtest::haveWorld("move3 retail"))
	{
		return;
	}
	struct Case
	{
		const char *a, *b;
	};
	int deep = 0, headOnClose = 0;
	for (const Case &c : { Case{ "GondorFighterHorde", "MordorFighterHorde" }, Case{ "MordorFighterHorde", "GondorFighterHorde" }, Case{ "GondorFighterHorde", "IsengardFighterHorde" } })
	{
		for (Cross mode : { Cross::HeadOn, Cross::Perpendicular })
		{
			const SweepTotals t = sweep(c.a, c.b, mode);
			std::printf("  info: sweep %s -> %s %s: %d runs, deep enemy pair-frames %d (in %d runs), %d runs with the hordes closer than 60, closest %.1f\n", c.a, c.b,
				crossName(mode), t.runs, t.deepPairFrames, t.runsWithDeepPairs, t.runsCloserThan60, t.minHordeDistance);
			deep += t.deepPairFrames;
			CHECK(t.patchStopRaised);
			headOnClose += mode == Cross::HeadOn ? t.runsCloserThan60 : 0;
		}
	}
	// Measured on this scenario (OpenBFME timings, not retail execution). Before lane MOVE-3 the blocked horde's repath was a whole findPath whose line-of-sight
	// optimisation dropped the detour and it remembered the blocker at once: 4040 deep pair-frames, 12 head-on runs with the hordes closer than 60. With RotWK's
	// patch (RW 0x6631BF / 0x6F7938 / 0x767A66), the horde footprint of RW 0x6ED071 and the member goal reservation (RW 0x86EF13): 3377 and 3. What remains is
	// RotWK's rule as far as it was read (stop S-1830): only the blocked horde (the lower path priority, then the lower id, RW 0x66D16E) patches; the other walks on,
	// and horde members never collide (RW 0x66E233 returns for a horde member, RW 0x6939DF). Merge of IDLE-1 (MOVE-3 r4: the AI updates in updates[0] and
	// HordeContain in updates[1], RW 0x851E97 / 0x490AC4, so every member steps before its horde's member pass; the hub leaves a member whose physics motion is
	// disabled alone, RW 0x874724): 3798 deep pair-frames, 16 head-on runs closer than 60 (3 + 9 + 4). Engine measurements, not retail values
	CHECK(deep < 3900);
	CHECK(headOnClose <= 16);
}

TEST_CASE("move3 retail: two allied hordes sent to one point (FB-0006)")
{
	if (!hudtest::haveWorld("move3 retail"))
	{
		return;
	}
	for (const char *t : { "ElvenLorienArcherHorde", "ElvenMirkwoodArcherHorde", "GondorArcherHorde", "GondorFighterHorde", "MordorArcherHorde", "GoblinArcherHorde", "IsengardUrukCrossbowHorde", "RohanArcherHorde", "DwarvenAxeThrowerHorde", "AngmarDarkRangerHorde" })
	{
		for (int mode = 0; mode < 2; ++mode)
		{
			SharedWorld &s = shared();
			Arena ar(s, "FactionMen", "FactionMordor");
			CommandList commands; // the lockstep command path (outlives the logic's use of it: the Arena is destroyed first)
			GameLogicDispatch dispatcher(ar.logic);
			AICommands aiCommands;
			aiCommands.registerHandlers(dispatcher);
			dispatcher.attach(commands);
			Object *h1 = ar.place(t, 0, 300.0f, 500.0f);
			Object *h2 = ar.place(t, 0, 300.0f, 700.0f);
			ar.logic.runLogicFrame();
			ar.logic.runLogicFrame();
			const Coord3D goal{ 800.0f, 600.0f, 0.0f };
			if (mode == 0)
			{
				h1->getAIUpdateInterface()->aiMoveToPosition(goal, CMD_FROM_PLAYER);
				h2->getAIUpdateInterface()->aiMoveToPosition(goal, CMD_FROM_PLAYER);
			}
			else
			{
				// the player's selection and MSG_DO_MOVETO through the lockstep command path (RW 0x77BAAA)
				const int player = ar.player(0)->getPlayerIndex();
				GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
				sel.appendBooleanArgument(true);
				sel.appendObjectIDArgument(h1->getID());
				sel.appendObjectIDArgument(h2->getID());
				commands.append(sel);
				GameMessage mv(MSG_DO_MOVETO, player);
				mv.appendLocationArgument(goal);
				commands.append(mv);
				ar.logic.runLogicFrame();
			}
			phystest::OverlapStats st;
			for (int f = 0; f < 400; ++f)
			{
				ar.logic.runLogicFrame();
				st.add(phystest::measure(ar.logic));
			}
			const HordeCentre c1 = membersCentre(h1), c2 = membersCentre(h2);
			const float d = std::sqrt((c1.x - c2.x) * (c1.x - c2.x) + (c1.y - c2.y) * (c1.y - c2.y));
			const Coord3D &p1 = *h1->getPosition(), &p2 = *h2->getPosition();
			char what[256];
			std::snprintf(what, sizeof what, "%s x2 %s: member centres %.1f apart (%.1f,%.1f / %.1f,%.1f), horde objects (%.1f,%.1f / %.1f,%.1f)", t,
				mode == 0 ? "each ordered" : "one group order", d, c1.x, c1.y, c2.x, c2.y, p1.x, p1.y, p2.x, p2.y);
			const phystest::OverlapSample last = phystest::measure(ar.logic);
			print(what, st);
			std::printf("  info:   at the end: allied overlap %.2f (%s)\n", last.allyOverlap, last.worstAllyPair.c_str());
			const std::string name(t);
			CHECK(raised(ar.logic.report().stops, "[S-1832] "));
			if (name == "ElvenLorienArcherHorde" || name == "DwarvenAxeThrowerHorde")
			{
				// stop S-1832: every rank of these hordes stands 30 or 50 ahead of the horde object; their goal reservations (RW 0x86EF13) lie outside the 6 x 10 cell
				// rectangle the second horde's destination check tests (RW 0x6F1584 / 0x6F0195), so RotWK's rules as read let both hordes take the one point
				CHECK(d < 1.0f);
			}
			else if (name != "RohanArcherHorde") // the Rohan archers do not settle on their slots in this run (informative only)
			{
				// before lane MOVE-3: each ordered 0 .. 11.8 apart (the second horde's adjustment met no reservation: horde goals are not reserved and the member goals
				// were not moved to the horde's goal); the ZH group order kept the 200 of the start (RotWK's MSG_DO_MOVETO sends both to the one point)
				CHECK(d > 50.0f);
				CHECK(last.allyOverlap < 10.0f); // the Uruk crossbows keep 9.15 at the edge of the two formations (stacked: 12 .. 16)
			}
		}
	}
}


namespace
{
struct RallyRun
{
	std::vector<Object *> hordes;
	float minCentreDistance = 1e9f; // the closest two produced hordes' member centres are at the end
	float endAllyOverlap = 0.0f;
};

// `count` hordes of `unit` from a Gondor barracks; `rally` sets the barracks' rally point 250 east of it first
RallyRun produce(const char *barracks, const char *unit, int count, bool rally, int frames, const char *faction = "FactionMen")
{
	SharedWorld &s = shared();
	RallyRun out;
	Arena ar(s, faction, "FactionMordor");
	Object *b = ar.place(barracks, 0, 400.0f, 600.0f);
	ar.player(0)->getMoney()->deposit(100000, false);
	ar.logic.productionSettings() = s.world->productionSettings();
	ar.logic.setUpgradeTypes(&s.world->upgradeTypes());
	ar.player(0)->commandPoints().setFromScript(100000, 100000); // test setup: command points are not what is tested here
	ar.logic.runLogicFrame();
	if (rally)
	{
		const Coord3D r{ 650.0f, 600.0f, 0.0f };
		REQUIRE(b->getObjectExitInterface() != nullptr);
		b->getObjectExitInterface()->setRallyPoint(&r);
	}
	ProductionUpdateInterface *pu = b->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const ThingTemplate *t = s.world->things().findTemplate(unit);
	REQUIRE(t != nullptr);
	for (int i = 0; i < count; ++i)
	{
		REQUIRE(pu->queueCreateUnit(t, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	}
	for (int f = 0; f < frames; ++f)
	{
		ar.logic.runLogicFrame();
	}
	for (Object *o = ar.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate() == t && !o->isDestroyed())
		{
			out.hordes.push_back(o);
		}
	}
	for (size_t i = 0; i < out.hordes.size(); ++i)
	{
		for (size_t j = i + 1; j < out.hordes.size(); ++j)
		{
			const HordeCentre a = membersCentre(out.hordes[i]), c = membersCentre(out.hordes[j]);
			out.minCentreDistance = std::min(out.minCentreDistance, std::sqrt((a.x - c.x) * (a.x - c.x) + (a.y - c.y) * (a.y - c.y)));
		}
	}
	out.endAllyOverlap = phystest::measure(ar.logic).allyOverlap;
	for (Object *h : out.hordes)
	{
		const HordeCentre c = membersCentre(h);
		std::printf("  info:   horde %u at (%.1f, %.1f), members' centre (%.1f, %.1f)\n", h->getID(), h->getPosition()->x, h->getPosition()->y, c.x, c.y);
	}
	return out;
}
} // namespace

TEST_CASE("move3 retail: hordes from one barracks with and without a rally point (FB-0006)")
{
	if (!hudtest::haveWorld("move3 retail"))
	{
		return;
	}
	// FB-0006 is two ElvenLorienArcherHorde on one spot: two of them from one Lorien barracks without a rally point
	{
		const RallyRun r = produce("ElvenBarracks", "ElvenLorienArcherHorde", 2, false, 2500, "FactionElves");
		std::printf("  info: ElvenLorienArcherHorde x2 from an ElvenBarracks (natural rally point): %zu hordes, member centres %.1f apart, allied overlap at the end %.2f\n",
			r.hordes.size(), r.minCentreDistance, r.endAllyOverlap);
		REQUIRE(r.hordes.size() == 2);
		CHECK(r.minCentreDistance > 60.0f);
		CHECK(r.endAllyOverlap < 5.0f);
	}
	for (bool rally : { false, true })
	{
		for (int count : { 2, 3 })
		{
			const RallyRun r = produce("GondorBarracks", "GondorFighterHorde", count, rally, 2500 + 1200 * (count - 2));
			std::printf("  info: GondorFighterHorde x%d from a barracks %s: %zu hordes, the closest member centres %.1f apart, allied overlap at the end %.2f\n", count,
				rally ? "with a rally point" : "(natural rally point)", r.hordes.size(), r.minCentreDistance, r.endAllyOverlap);
			REQUIRE(r.hordes.size() == (size_t)count);
			if (count == 3)
			{
				// informative: the third horde's exit moves the second aside, which may stop beside the first only partly clear of it (measured: the closest
				// member centres 56.4 apart, an overlap of 15.6; with a rally point 100 apart)
				continue;
			}
			if (rally)
			{
				// the next horde's move to the rally point (RW 0x8A3BF8 -> aiMoveToPosition, its destination adjusted against the member goal reservations) ends
				// beside the other: 80.8 apart. MOVE-3 r1 measured 112.1 apart without overlap, but with the 9 x 9 line test a horde does not have (RotWK tests a
				// HORDE's line with radius 1, RW 0x6EE12D); with the retail line test a rank of the second horde touches the first (allied
				// overlap 9.29 at the end, as before MOVE-3; S-1834)
				CHECK(r.minCentreDistance > 60.0f);
				CHECK(r.endAllyOverlap < 10.0f);
			}
			else
			{
				// before MOVE-3 r2 the hordes stood on the natural rally point together (0.0 apart, an overlap of 16). The exit's moveAlliesAwayFromDestination
				// (RW 0x6F85A6) asks the previous horde's members on the line to the natural rally point to move away, and a member hands the request to its
				// horde (RW 0x6F53AF / 0x66DA5F), which steps aside
				CHECK(r.minCentreDistance > 60.0f);
				CHECK(r.endAllyOverlap < 5.0f);
			}
		}
	}
}

namespace
{
// lane MOVE-3 r4: a non-horde unit leaves a Gondor barracks through its queue exit (QueueProductionExitUpdate::exitObjectViaDoor) toward a rally point; an idle
// allied GondorFighter stands on the line to the rally point, away from the line to the natural rally point. `rallyOnCliff` puts the rally point in the middle of
// a cliff block wider than the adjustment's ring search reaches, so adjustDestination (RW 0x6FE456) fails and the rally point is not appended to the exit path;
// `staleIgnore` gives the new unit the ally as its ignored obstacle before the exit. Returns whether the ally was asked to move away
bool exitAsksAlly(bool rallyOnCliff, bool staleIgnore, bool &rallyAppended)
{
	SharedWorld &s = shared();
	Arena ar(s, "FactionMen", "FactionMordor", 200);
	const Coord3D rally{ 1300.0f, 1000.0f, 0.0f };
	if (rallyOnCliff)
	{
		// the ring search's layers (4, 12, 20 ... cells, each charged ring + 2 against MaxCellsAdjustDestination) spiral outward: twice the layer count is a safe bound
		int limit = ar.ai->pathfinder().config().adjustDestinationLimit, d = 1, ring = 4, reach = 0;
		while (limit > 0)
		{
			limit -= ring + 2;
			reach = (d + 1) / 2;
			d += 2;
			ring += 8;
		}
		const int half = 2 * reach + 5, cx = (int)(rally.x / 10.0f), cy = (int)(rally.y / 10.0f);
		std::printf("  info: adjustment reach %d cells (MaxCellsAdjustDestination %d)\n", reach, ar.ai->pathfinder().config().adjustDestinationLimit);
		REQUIRE(cx + half < 200);
		for (int x = cx - half; x <= cx + half; ++x)
		{
			for (int y = cy - half; y <= cy + half; ++y)
			{
				ar.terrain.cliff.insert({ x, y });
			}
		}
		ar.ai->newMap(ar.terrain);
	}
	Object *b = ar.place("GondorBarracks", 0, 400.0f, 1000.0f);
	ar.logic.runLogicFrame();
	ExitInterface *exit = b->getObjectExitInterface();
	REQUIRE(exit != nullptr);
	exit->setRallyPoint(&rally);
	Coord3D natural;
	exit->getNaturalRallyPoint(&natural, true);
	// the ally: on the line from the barracks to the rally point, far from the natural rally point
	const Coord3D allyAt{ 1000.0f, 1000.0f, 0.0f };
	REQUIRE(std::hypot(allyAt.x - natural.x, allyAt.y - natural.y) > 300.0f);
	Object *ally = ar.place("GondorFighter", 0, allyAt.x, allyAt.y);
	for (int i = 0; i < 5; ++i)
	{
		ar.logic.runLogicFrame();
	}
	REQUIRE(ally->getAIUpdateInterface()->lastMoveAwayRequester() == INVALID_ID);
	Object *unit = ar.place("GondorFighter", 0, 400.0f, 1000.0f);
	if (staleIgnore)
	{
		unit->getAIUpdateInterface()->mover().ignoreObstacle((PathfindObjectID)ally->getID());
	}
	exit->exitObjectViaDoor(unit, DOOR_1);
	AIUpdateInterface *ua = unit->getAIUpdateInterface();
	const std::vector<Coord3D> &path = ua->stateMachine().goalPath();
	rallyAppended = !path.empty() && std::hypot(path.back().x - rally.x, path.back().y - rally.y) < 30.0f;
	// the exit command's ignored obstacle (the queue exit passes no source object) is what stays: RW 0x8A424F restores it after the clearing
	CHECK(ua->mover().ignoredObstacleID() == PATHFIND_INVALID_ID);
	// asked: aiMoveAwayFromUnit recorded the new unit as its requester (RW 0x66DA5F's requester slots; with the rally point among the cliffs the ally may find
	// no way out and stay idle)
	return ally->getAIUpdateInterface()->lastMoveAwayRequester() == unit->getID();
}
} // namespace

TEST_CASE("move3 retail: the queue exit clears allies toward the rally point after its exit command (RW 0x8A4214 .. 0x8A424F)")
{
	if (!hudtest::haveWorld("move3 retail"))
	{
		return;
	}
	bool appended = false;
	// a rally point the adjustment reaches: appended, the ally on the line is asked
	CHECK(exitAsksAlly(false, false, appended));
	CHECK(appended);
	// a failed adjustment (RW 0x8A41BD): the rally point is not appended, but [ebp-0x44] still holds it, so the clearing still runs toward it (the exit path's
	// last point is the natural rally point, whose line does not pass the ally)
	CHECK(exitAsksAlly(true, false, appended));
	CHECK_FALSE(appended);
	// the ignored obstacle the clearing uses is read after the exit command (RW 0x8A421B), which resets it: an ignore set before the exit does not spare the ally
	CHECK(exitAsksAlly(false, true, appended));
}

TEST_CASE("move3 retail: the clearing line's ends are not clamped to the grid (RW 0x6E8CE6), so a line starting off the grid asks nobody (RW 0x6F57C3)")
{
	if (!hudtest::haveWorld("move3 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena ar(s, "FactionMen", "FactionMordor", 120);
	Object *ally = ar.place("GondorFighter", 0, 55.0f, 600.0f);
	Object *unit = ar.place("GondorFighter", 0, 300.0f, 600.0f);
	for (int i = 0; i < 5; ++i)
	{
		ar.logic.runLogicFrame();
	}
	REQUIRE(ally->getAIUpdateInterface()->lastMoveAwayRequester() == INVALID_ID);
	// from cell (-5, 60), off the grid: the walk ends on its first cell (a clamped start, cell (0, 60), would walk over the ally's cell (5, 60))
	ar.ai->moveAlliesAwayFromDestination(*unit, Coord3D{ -50.0f, 600.0f, 0.0f }, Coord3D{ 500.0f, 600.0f, 0.0f }, INVALID_ID);
	CHECK(ally->getAIUpdateInterface()->lastMoveAwayRequester() == INVALID_ID);
	// the same line from on the grid asks the ally
	ar.ai->moveAlliesAwayFromDestination(*unit, Coord3D{ 15.0f, 600.0f, 0.0f }, Coord3D{ 500.0f, 600.0f, 0.0f }, INVALID_ID);
	CHECK(ally->getAIUpdateInterface()->lastMoveAwayRequester() == unit->getID());
}
