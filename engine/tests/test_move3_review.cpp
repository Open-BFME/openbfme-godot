// OpenBFME unit tests. GPL-3.0.
// Lane MOVE-3 r3: the review fixes of round 1, each against the RotWK rule it ports (the probes are the reviewer's, turned around).

#include "doctest.h"
#include "MoveTestUtil.h"
#include "PathfindTestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/AI/AIGroup.h"

#include <vector>

TEST_CASE("move3 review: PlanningModeEnabled is part of the pathfinder hash")
{
	auto a = pathtest::testConfig(), b = a;
	b.planningModeEnabled = false;
	Pathfinder pa(a, nullptr), pb(b, nullptr);
	StateHasher ha, hb;
	pa.crc(ha);
	pb.crc(hb);
	CHECK(ha.value() != hb.value());
}

TEST_CASE("move3 review: a plain goal update keeps the reserved heading (RW 0x68B3BD -> 0x8E28DB passes the goal slot's angle)")
{
	movetest::MoveWorld mw;
	mw.buildMap();
	Object *o = mw.spawn("Horde", 305, 305, 0);
	mw.frames(2);
	Pathfinder &pf = mw.ai->pathfinder();
	auto &adapter = mw.ai->adapterFor(*o);
	Coord3D dest{ 705, 705, 0 };
	pf.updateGoalAngle(adapter, &dest, 1.5707963f, LAYER_GROUND);
	const float before = pf.goalAngle(o->getID());
	REQUIRE(before > 1.0f); // code 3, pi / 2
	pf.updateGoal(adapter, &dest, LAYER_GROUND);
	CHECK(pf.goalAngle(o->getID()) == before);
	// a new cell keeps the heading too
	Coord3D next{ 905, 705, 0 };
	pf.updateGoal(adapter, &next, LAYER_GROUND);
	CHECK(pf.goalAngle(o->getID()) == before);
}

TEST_CASE("move3 review: the mover hash covers the raw nodes, the optimised links and the current node")
{
	movetest::MoveWorld mw;
	mw.buildMap();
	Object *o = mw.spawn("Walker", 305, 305);
	mw.frames(2);
	auto &m = o->getAIUpdateInterface()->mover();
	Path *p = new Path();
	Coord3D p0{ 305, 305, 0 }, p1{ 405, 405, 0 }, p2{ 705, 705, 0 };
	p->appendNode(&p0, LAYER_GROUND);
	p->appendNode(&p1, LAYER_GROUND);
	p->appendNode(&p2, LAYER_GROUND);
	p->getFirstNode()->setNextOptimized(p->getLastNode());
	p->markOptimized();
	m.setPath(p);
	StateHasher a, b, c;
	m.crc(a);
	// a raw node off the optimised chain moves (what the blocked patch RW 0x6F7938 / 0x767A66 reads and splices)
	Coord3D *hidden = const_cast<Coord3D *>(p->getFirstNode()->getNext()->getPosition());
	hidden->x = 425;
	m.crc(b);
	CHECK(a.value() != b.value());
	// the optimised link changes target
	p->getFirstNode()->setNextOptimized(p->getFirstNode()->getNext());
	m.crc(c);
	CHECK(b.value() != c.value());
	// the current node's raw index: two paths A, A', B (A' on A) with the same links and the follower at t = 0, once on A (the optimised search) and once on
	// A' (the raw search skips A -> A''s NaN, RW 0x765598): only the current raw index differs
	const Coord3D pa{ 505, 505, 0 }, pb{ 605, 505, 0 };
	auto build = [&]() {
		Path *q = new Path();
		q->appendNode(&pa, LAYER_GROUND);
		q->appendNode(&pa, LAYER_GROUND);
		q->appendNode(&pb, LAYER_GROUND);
		q->getFirstNode()->setNextOptimized(q->getLastNode());
		q->getFirstNode()->getNext()->setNextOptimized(q->getLastNode());
		q->markOptimized();
		return q;
	};
	Path *onA = build();
	m.setPath(onA);
	onA->updateClosestSegment(pa);
	REQUIRE(onA->currentNode() == onA->getFirstNode());
	REQUIRE(onA->currentT() == 0.0f);
	StateHasher hA;
	m.crc(hA);
	Path *onA2 = build();
	m.setPath(onA2);
	REQUIRE(onA2->updateClosestRawSegment(pa));
	REQUIRE(onA2->currentNode() == onA2->getFirstNode()->getNext());
	REQUIRE(onA2->currentT() == 0.0f);
	StateHasher hA2;
	m.crc(hA2);
	CHECK(hA.value() != hA2.value());
}

TEST_CASE("move3 review: the closest raw segment skips a duplicate node's NaN distance (RW 0x765598)")
{
	Path p;
	Coord3D a{ 100, 100, 0 }, b{ 110, 100, 0 };
	p.prependNode(&b, LAYER_GROUND);
	p.prependNode(&a, LAYER_GROUND);
	p.prependNode(&a, LAYER_GROUND);
	REQUIRE(p.updateClosestRawSegment(a));
	// the first segment has length 0: t = 0 / 0 stays NaN through the clamp and its distance never replaces the best; the second segment is chosen
	CHECK(p.currentNode() == p.getFirstNode()->getNext());
}

TEST_CASE("move3 review: the patch cost asks for the arrival times at the candidate cell (RW 0x6ED46C -> 0x6ED049 with the candidate cell)")
{
	struct Spy : pathtest::TestObject
	{
		mutable std::vector<Coord2D> points;
		bool hasLocomotor() const override { return true; }
		float secondsToReach(float x, float y) const override
		{
			points.push_back({ x, y });
			return 1;
		}
	} self, other;
	self.id = 1;
	self.mobile = true;
	self.pos = { 100, 100, 0 };
	self.diameter = 40;
	other.id = 2;
	other.mobile = true;
	other.pos = { 700, 700, 0 };
	other.diameter = 10;
	pathtest::TestWorld world;
	world.objects[1] = &self;
	world.objects[2] = &other;
	auto cfg = pathtest::testConfig();
	Pathfinder pf(cfg, &world);
	pathtest::SyntheticTerrain terrain(100, 100);
	pf.newMap(terrain);
	// the other unit's goal lies on the fringe of the candidate's footprint, not on the candidate cell
	Coord3D fringe{ 495, 495, 0 }, point{ 505, 505, 0 };
	pf.updateGoal(other, &fringe, LAYER_GROUND);
	pf.patchPointIsFree(self, pathtest::groundLoco(), point);
	REQUIRE(!self.points.empty());
	REQUIRE(!other.points.empty());
	// both arrival times are taken at the candidate cell (50, 50), not at the scanned fringe cell
	CHECK(self.points[0].x == other.points[0].x);
	CHECK(self.points[0].y == other.points[0].y);
	CHECK(self.points[0].x > 500.0f); // the candidate cell's coordinate (510.5 here: the 40 footprint is not centred), the fringe goal's cell lies below 500
	CHECK(self.points[0].x < 515.0f);
}
