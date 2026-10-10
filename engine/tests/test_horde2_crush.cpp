// OpenBFME. HORDE-2 tests: crush / trample (SquishCollide RW 0x8BFBAE, the crush levels, canCrush, onCrush's deceleration, the crush and revenge weapons) and flanking
// (RW 0x68FB63 / 0x876FC4) on synthetic data. The retail cases are in test_hud_horde2_retail.cpp.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"

#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"

using namespace combattest;
using namespace horde2test;

namespace
{
// a rider that tramples (CrusherLevel 1, a CrushWeapon) and footmen that can be trampled (CrushableLevel 0) or not (a pikeman at CrushableLevel 1 with a revenge weapon)


} // namespace

TEST_CASE("horde2 crush: a charging rider tramples an enemy footman it runs into: the crush weapon hits, the rider slows down")
{
	CombatWorld w(kCrushObjects);
	REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
	w.combat().setAutoAcquireEnabled(false);
	Object *rider = w.unit("Rider", 'A', 200, 300);
	Object *foot = w.unit("Footman", 'B', 300, 300);
	w.frames(2);
	REQUIRE(rider->getWeapons() != nullptr);
	REQUIRE(rider->getWeapons()->crushWeapon() != nullptr);
	CHECK(ObjectCrush::crusherLevel(*rider) == 1);
	CHECK(ObjectCrush::crushableLevel(*foot) == 0);
	CHECK(ObjectCrush::crushableLevel(*rider) == 3);
	const ObjectID fid = foot->getID();
	rider->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 400.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
	w.runUntil([&] { return w.combat().counters().crushes > 0; }, 100);
	CHECK(w.combat().counters().crushes >= 1);
	CHECK(w.combat().counters().crushWeaponShots >= 1);
	CHECK(w.combat().counters().crushDecelerations >= 1);
	w.frames(3);
	Object *f = w.byId(fid);
	// two trample hits of 60 kill the 100 health footman (or one hit and the footman still stands at 40)
	CHECK((f == nullptr || f->isEffectivelyDead() || w.health(f) == doctest::Approx(40.0f)));
}

TEST_CASE("horde2 crush: no crush when the levels do not allow it, the units are allies, or the crusher stands still; a revenge weapon answers a crush")
{
	SUBCASE("a pikeman (CrushableLevel 1) is not crushed by a CrusherLevel 1 rider")
	{
		CombatWorld w(kCrushObjects);
		REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
		w.combat().setAutoAcquireEnabled(false);
		Object *rider = w.unit("Rider", 'A', 200, 300);
		Object *pike = w.unit("Pikeman", 'B', 300, 300);
		w.frames(2);
		CHECK(ObjectCrush::crushableLevel(*pike) == 1);
		rider->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 400.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
		w.frames(40);
		CHECK(w.combat().counters().crushes == 0);
	}
	SUBCASE("an allied footman is not crushed")
	{
		CombatWorld w(kCrushObjects);
		REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
		w.combat().setAutoAcquireEnabled(false);
		Object *rider = w.unit("Rider", 'A', 200, 300);
		w.unit("Footman", 'A', 300, 300);
		w.frames(2);
		rider->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 400.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
		w.frames(40);
		CHECK(w.combat().counters().crushes == 0);
	}
	SUBCASE("a rider that stands still bumps (canCrush: MinCrushVelocityPercent 40%) and the footman lives")
	{
		CombatWorld w(kCrushObjects);
		REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
		w.combat().setAutoAcquireEnabled(false);
		Object *rider = w.unit("Rider", 'A', 300, 300);
		w.unit("Footman", 'B', 304, 300); // overlapping and in front of the rider (facing east)
		w.frames(3);
		CHECK_FALSE(ObjectCrush::canCrush(*rider));
		CHECK(w.combat().counters().crushes == 0);
		CHECK(w.combat().counters().crushBumps > 0);
	}
	SUBCASE("a rider that runs AWAY from a footman behind it does not crush it")
	{
		CombatWorld w(kCrushObjects);
		REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
		w.combat().setAutoAcquireEnabled(false);
		Object *rider = w.unit("Rider", 'A', 300, 300);
		w.unit("Footman", 'B', 290, 300); // behind the rider
		w.frames(1);
		rider->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 500.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
		w.frames(20);
		CHECK(w.combat().counters().crushes == 0);
	}
}

TEST_CASE("horde2 crush: the crush lines are in the combat report, the counters are hashed")
{
	CombatWorld w(kCrushObjects);
	REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
	const std::vector<std::string> r = w.combat().report();
	for (const char *id : { "[S-580]", "[S-582]", "[S-583]", "[S-584]", "[S-587]", "[S-588]", "[S-589]", "[S-590]" })
	{
		bool found = false;
		for (const std::string &s : r)
		{
			found = found || s.compare(0, std::string(id).size(), id) == 0;
		}
		CHECK_MESSAGE(found, id);
	}
	const std::uint32_t h0 = w.hash();
	w.combat().counters().crushes += 1;
	CHECK(w.hash() != h0);
	w.combat().counters().crushes -= 1;
	CHECK(w.hash() == h0);
	w.combat().counters().flanks += 1;
	CHECK(w.hash() != h0);
}

TEST_CASE("horde2 crush: the same trample twice gives the same hash in every frame")
{
	auto run = [] {
		CombatWorld w(kCrushObjects);
		REQUIRE(w.w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
		w.combat().setAutoAcquireEnabled(false);
		Object *rider = w.unit("Rider", 'A', 200, 300);
		w.unit("Footman", 'B', 300, 300);
		w.unit("Footman", 'B', 330, 310);
		w.frames(2);
		rider->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 420.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
		std::vector<std::uint32_t> hashes;
		for (int f = 0; f < 40; ++f)
		{
			w.frames(1);
			hashes.push_back(w.hash());
		}
		return hashes;
	};
	const std::vector<std::uint32_t> a = run(), b = run();
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); ++i)
	{
		REQUIRE_MESSAGE(a[i] == b[i], "frame " << i);
	}
}

// S-532 (found by the SPELL-1 smoke test, Sol's diagnosis): a member stood 282 units off its slot for 400 seconds, idle, with a type 4 (EXPLICIT_WITH_PATH) goal, a stale
// fallback path and no pathfinder goal cell; the member order (RW 0x877A7A) leaves a member with a type 4 goal and a path alone, so only doLocomotor can drop the path.
// RW drops it when the straight step is valid (RW 0x669B14), and RW 0x6F1B3E's FIRST test (RW 0x6EF865) calls a step that ends in the goal's own cell valid whatever the
// cell holds; the port missed that test, so a member whose goal cell was otherwise invalid kept the path for good. And RW 0x6F74D0 never hands back a partial path.
struct AIMoverTestAccess
{
	static void forcePath(AIMover &mv, Path *p) { mv.m_path = p; }
	static bool stepEndsInGoalCell(AIMover &mv, const Coord3D &now) { return mv.stepEndsInGoalCell(now); }
	static bool goalCellOnGridAndNotStart(AIMover &mv, const Coord3D &from) { return mv.goalCellOnGridAndNotStart(from); }
};

TEST_CASE("horde2 recovery: a member stuck with a stale fallback path in its goal's cell drops the path and rejoins its slot (S-532)")
{
	movetest::MoveWorld mw;
	mw.terrain.cliff.insert({ 71, 50 }); // the stale goal's cell: invalid for the straight-step test
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	mw.frames(8);
	HordeContain *hc = dynamic_cast<HordeContain *>(h->getContain());
	REQUIRE(hc != nullptr);
	REQUIRE(hc->worstMemberSlotError() < 8.0f);
	Object *m = mw.membersOf(h)[2];
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	// the stranded state: the member at the end of its fallback path, in its stale goal's cell, the goal type 4
	const Coord3D goal{ 715.0f, 505.0f, 0.0f };
	Coord3D at{ 712.0f, 503.0f, 0.0f };
	m->setPosition(&at);
	ai->hordeMemberMoveTo(goal);
	Path *stale = new Path();
	stale->appendNode(&at, LAYER_GROUND);
	stale->appendNode(&goal, LAYER_GROUND);
	AIMoverTestAccess::forcePath(ai->mover(), stale);
	REQUIRE(ai->mover().path() != nullptr);
	mw.frames(3);
	CHECK(ai->mover().path() == nullptr); // RW 0x6EF865: the step ends in the goal's cell, the path goes (RW 0x669B14)
	mw.frames(300);
	CHECK(hc->worstMemberSlotError() < 3.0f); // the member order took it back to its slot
}

// RW 0x870180 ("in current melee"): RW 0x66352C, the squared nonnegative footprint edge distance, compared with RW 0xBDE8B8 = 10000.0f: an edge gap below 100 units.
// (Review r1's probe expected a 10 unit cutoff; the constant in the binary is 10000, so 9 / 10 / 11 are all inside and 99 / 100 / 101 are the boundary.)
TEST_CASE("horde2 melee: the current-melee edge cutoff is an edge gap below 100 units (RW 0xBDE8B8 = 10000 against the squared edge distance)")
{
	std::string fixture(kCombatObjects);
	fixture = fixture.substr(fixture.find("Object SwordHorde"));
	fixture.replace(fixture.find("Object SwordHorde"), std::string("Object SwordHorde").size(), "Object ExactHorde");
	fixture.replace(fixture.find("Geometry = BOX"), std::string("Geometry = BOX").size(), "Geometry = CYLINDER");
	fixture.replace(fixture.find("GeometryMinorRadius = 45"), std::string("GeometryMinorRadius = 45").size(), "GeometryMinorRadius = 30");
	CombatWorld w(fixture.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *self = w.unit("ExactHorde", 'A', 300, 300);
	Object *target = w.unit("ExactHorde", 'B', 340, 300);
	Object *probe = w.unit("Dummy", 'B', 800, 300);
	w.frames(2);
	auto *ai = dynamic_cast<HordeAIUpdate *>(self->getAIUpdateInterface());
	REQUIRE(ai != nullptr);
	REQUIRE(ai->isMeleeTargetReady(*target));
	REQUIRE(ai->meleeTargetId() == target->getID());
	const float radii = SimMath::addf32(CombatQueries::boundingCircleRadius(*self), CombatQueries::boundingCircleRadius(*probe));
	for (float edge : { 9.0f, 10.0f, 11.0f, 99.0f, 100.0f, 101.0f })
	{
		Coord3D position{ SimMath::addf32(self->getPosition()->x, SimMath::addf32(radii, edge)), self->getPosition()->y, self->getPosition()->z };
		probe->setPosition(&position);
		INFO("edge separation " << edge);
		CHECK(ai->isInCurrentMelee(probe) == (edge < 100.0f));
	}
}

// review r2: RW 0x6EF865 measures the cells as RW 0x6ECFC5 does for the unit: an EVEN footprint adds the half-cell offset (floor(x / 10 + 0.5)). A horde member (radius 8:
// diameter 16 -> 20, footprint 2) stepping to 502 with its goal at 508 is NOT in the goal's cell (50 vs 51); stepping to 508 with the goal at 512 IS (51 vs 51).
TEST_CASE("horde2 recovery: the goal-cell test of a two-cell footprint uses the half-cell offset (502 -> 508 is not the goal cell, 508 -> 512 is)")
{
	movetest::MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 300.0f, 300.0f);
	mw.frames(4);
	Object *m = mw.membersOf(h)[0];
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE((mw.ai->pathfinder().footprintSize(&ai->adapter()) & 1) == 0);
	ai->hordeMemberMoveTo(Coord3D{ 508.0f, 500.0f, 0.0f });
	CHECK_FALSE(AIMoverTestAccess::stepEndsInGoalCell(ai->mover(), Coord3D{ 502.0f, 500.0f, 0.0f }));
	ai->hordeMemberMoveTo(Coord3D{ 512.0f, 500.0f, 0.0f });
	CHECK(AIMoverTestAccess::stepEndsInGoalCell(ai->mover(), Coord3D{ 508.0f, 500.0f, 0.0f }));
}

// lane INTEG-1 (found by PATH-2's produced-horde test after the HORDE-2 merge): a barracks on the map's top edge sends its members to an exit point in the map border.
// RW 0x6F74D0 measures the goal cell without clamping (RW 0x6E8CE6) and returns no path when RW 0x5E2E9C finds no cell there (or when it is the start cell); the unit is
// then put on its goal (RW 0x70C201). The port's Pathfinder::findPath clipped the goal to the grid's edge and returned a "full" path to the edge cell: the member stood at
// its end with a type 4 goal and a path, which the member order (RW 0x877A7A) leaves alone, for good. (This synthetic map pins RW's early outs and the outcome; the
// clipped search only returned a path in the retail case, a member standing in the barracks footprint: `path2 retail: a barracks placed on a legal site ...` is the
// test that fails without the fix, 8 of 15 members settled.)
TEST_CASE("horde2 recovery: a member whose type 4 goal lies off the pathfinder grid gets no path to the clipped edge and rejoins its slot")
{
	movetest::MoveWorld mw; // 100 x 100 cells: the grid ends at y = 1000
	for (int x = 40; x <= 60; ++x)
	{
		mw.terrain.cliff.insert({ x, 97 }); // a wall below the top rows: the straight step into it is invalid, the edge cell behind it is reachable around it
	}
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 905.0f);
	mw.frames(8);
	HordeContain *hc = dynamic_cast<HordeContain *>(h->getContain());
	REQUIRE(hc != nullptr);
	REQUIRE(hc->worstMemberSlotError() < 8.0f);
	Object *m = mw.membersOf(h)[2];
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	// the early outs of RW 0x6F74D0
	ai->hordeMemberMoveTo(Coord3D{ 505.0f, 1080.0f, 0.0f });
	CHECK_FALSE(AIMoverTestAccess::goalCellOnGridAndNotStart(ai->mover(), Coord3D{ 505.0f, 955.0f, 0.0f })); // off the grid
	ai->hordeMemberMoveTo(Coord3D{ 505.0f, 900.0f, 0.0f });
	CHECK(AIMoverTestAccess::goalCellOnGridAndNotStart(ai->mover(), Coord3D{ 505.0f, 955.0f, 0.0f }));
	CHECK_FALSE(AIMoverTestAccess::goalCellOnGridAndNotStart(ai->mover(), Coord3D{ 505.0f, 900.0f, 0.0f })); // the start cell
	// the member below the wall, its goal in the border: the straight step into the wall is invalid, and RW makes no path (the port's clipped search made one to the
	// edge cell (50, 99) behind the wall); the unit is put on its goal
	Coord3D at{ 505.0f, 955.0f, 0.0f };
	m->setPosition(&at);
	ai->hordeMemberMoveTo(Coord3D{ 505.0f, 1080.0f, 0.0f });
	bool everPath = false;
	for (int f = 0; f < 6; ++f)
	{
		mw.frames(1);
		everPath = everPath || ai->mover().path() != nullptr;
	}
	CHECK_FALSE(everPath);
	CHECK(m->getPosition()->y > 1000.0f); // RW 0x70C201: on its goal in the border
	mw.frames(300);
	CHECK(hc->worstMemberSlotError() < 3.0f); // the member order took it back to its slot
}
