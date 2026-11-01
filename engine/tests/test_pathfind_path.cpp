// OpenBFME unit tests: Path / PathNode follower queries (lane PATH-1). Expected values are derived by hand from the RotWK
// bodies named in AIPathfindPath.cpp (RW 0x765598 updateClosestSegment, 0x765F31 computePointAhead, 0x765972 remaining
// length, 0x5E2DBA near-end test).

#include "doctest.h"
#include "PathfindTestUtil.h"

#include <cstdint>
#include <cstring>

namespace
{
Coord3D pt(float x, float y, float z = 0.0f)
{
	Coord3D c;
	c.x = x;
	c.y = y;
	c.z = z;
	return c;
}

// a path A -> B -> C built the way the pathfinder does (prepend from the goal back): nextOpti starts as next
std::unique_ptr<Path> makePath(const std::vector<Coord3D> &pts, bool laterLegOnTie = false)
{
	std::unique_ptr<Path> p(new Path(laterLegOnTie));
	for (size_t i = pts.size(); i-- > 0;)
	{
		p->prependNode(&pts[i], LAYER_GROUND);
	}
	return p;
}
} // namespace

TEST_CASE("path: prependNode chains next and nextOpti, appendNode on an optimised path extends the chain")
{
	std::unique_ptr<Path> p = makePath({ pt(0, 0), pt(30, 0), pt(30, 40) });
	REQUIRE(p->nodeCount() == 3);
	PathNode *a = p->getFirstNode();
	CHECK(a->getNextOptimized() == a->getNext());
	CHECK(p->getLastNode()->getPosition()->y == 40.0f);
	CHECK(!p->isOptimized());
	p->markOptimized();
	Coord3D e = pt(30, 60);
	p->appendNode(&e, LAYER_GROUND);
	CHECK(p->getLastNode()->getPosition()->y == 60.0f);
	CHECK(p->getFirstNode()->getNext()->getNext()->getNextOptimized() == p->getLastNode());
	// a duplicate x / y is ignored once optimised (RW 0x665429)
	Coord3D dup = pt(30, 60, 5.0f);
	p->appendNode(&dup, LAYER_GROUND);
	CHECK(p->nodeCount() == 4);
}

TEST_CASE("path: computePointAhead walks the optimised segments from the current segment point")
{
	std::unique_ptr<Path> p = makePath({ pt(0, 0), pt(30, 0), pt(30, 40) });
	LocomotorPathPoint a = p->computePointAhead(15.0f);
	CHECK(a.position.x == 15.0f);
	CHECK(a.position.y == 0.0f);
	LocomotorPathPoint b = p->computePointAhead(40.0f); // 30 along AB, then 10 along BC
	CHECK(b.position.x == 30.0f);
	CHECK(b.position.y == 10.0f);
	LocomotorPathPoint c = p->computePointAhead(500.0f); // past the end: the tail
	CHECK(c.position.x == 30.0f);
	CHECK(c.position.y == 40.0f);
	// the distance is never below 0.1
	LocomotorPathPoint z = p->computePointAhead(0.0f);
	CHECK(z.position.x == doctest::Approx(0.1f));
}

TEST_CASE("path: updateClosestSegment finds the nearest segment and the fraction along it, computePointAhead continues from there")
{
	std::unique_ptr<Path> p = makePath({ pt(0, 0), pt(30, 0), pt(30, 40) });
	p->updateClosestSegment(pt(10, 5));
	REQUIRE(p->currentNode() != nullptr);
	CHECK(p->currentNode()->getPosition()->x == 0.0f);
	CHECK(p->currentT() == doctest::Approx(10.0f / 30.0f));
	LocomotorPathPoint a = p->computePointAhead(15.0f);
	CHECK(a.position.x == doctest::Approx(10.0f + 15.0f));
	CHECK(a.position.y == doctest::Approx(0.0f));
	// a position beside the second segment
	p->updateClosestSegment(pt(35, 20));
	CHECK(p->currentNode()->getPosition()->x == 30.0f);
	CHECK(p->currentNode()->getPosition()->y == 0.0f);
	CHECK(p->currentT() == doctest::Approx(0.5f));
	// the search starts at the cached node: it never looks behind
	p->updateClosestSegment(pt(0, 0));
	CHECK(p->currentNode()->getPosition()->x == 30.0f);
	CHECK(p->currentT() == 0.0f);
}

// the Uber follower rule (AIPathfindPath.cpp, updateClosestSegment; UBER-PATH): a path that doubles back along one line, as a patch spliced into a blocked
// unit's path can (down x = 10 from y 40 to y 20, back up to y 40, on to x 30). A unit standing on the doubled line at (10, 30) is at distance 0 from both
// legs: an Uber path takes the later leg, so the point ahead leads on up the path, not back down to the tip; a Faithful path keeps the earlier leg, as RW
TEST_CASE("path: on a path doubling back along one line, an Uber path takes the later leg the unit stands on and a Faithful path keeps the earlier, as RW")
{
	std::unique_ptr<Path> p = makePath({ pt(10, 40), pt(10, 20), pt(10, 40), pt(30, 40) }, true);
	p->updateClosestSegment(pt(10, 30));
	REQUIRE(p->currentNode() != nullptr);
	CHECK(p->currentNode()->getPosition()->y == 20.0f); // the up leg (10, 20) -> (10, 40)
	CHECK(p->currentT() == 0.5f);
	LocomotorPathPoint a = p->computePointAhead(5.0f);
	CHECK(a.position.x == 10.0f);
	CHECK(a.position.y == 35.0f); // ahead, up the line
	// off the line the closest leg wins as before: (12, 30) is 2 from both legs and 10 from the last leg (its t 0.1); the earlier leg is kept
	std::unique_ptr<Path> q = makePath({ pt(10, 40), pt(10, 20), pt(10, 40), pt(30, 40) }, true);
	q->updateClosestSegment(pt(12, 30));
	CHECK(q->currentNode() == q->getFirstNode()); // the down leg (10, 40) -> (10, 20)
	CHECK(q->currentT() == 0.5f); // the down leg's middle (the node (10, 40) also starts the last leg, whose t would be 0.1)
	// on the turning node (10, 20) both legs are at 0, but the unit stands at the down leg's end: it keeps the earlier leg, as RW does
	std::unique_ptr<Path> r = makePath({ pt(10, 40), pt(10, 20), pt(10, 40), pt(30, 40) }, true);
	r->updateClosestSegment(pt(10, 20));
	CHECK(r->currentNode() == r->getFirstNode());
	CHECK(r->currentT() == 1.0f);
	// a Faithful path (the default) keeps RW's tie: at (10, 30) the earlier, down leg, whose point ahead runs back down to the tip
	std::unique_ptr<Path> f = makePath({ pt(10, 40), pt(10, 20), pt(10, 40), pt(30, 40) });
	f->updateClosestSegment(pt(10, 30));
	CHECK(f->currentNode() == f->getFirstNode());
	CHECK(f->currentT() == 0.5f);
	const LocomotorPathPoint fa = f->computePointAhead(5.0f);
	CHECK(fa.position.y == 25.0f); // down toward the tip (10, 20)
}

// the legs and positions captured from the Rivendell campaign's starting horde (MAP GOOD Rivendell, the bfme2camp1 tests, with the target rule on and
// RW's tie): a spliced patch runs down x = 695 from y 1135 to y 1125 and back up to y 1145, and the horde flipped between
// these two positions every frame. Without a later leg the down leg is taken with the fractions RW 0x765598 gives on the whole patch, bit for bit
// (tools/retail_oracle/test_path_follower_oracle.py: 0x3EC19666, 0x3F059B33); with the up leg there an Uber path takes it, whose point ahead leads up
TEST_CASE("path: on the Rivendell horde's doubled-back patch, an Uber path takes the up leg at both positions the horde flipped between")
{
	auto bits = [](float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; };
	struct Case
	{
		float y;
		std::uint32_t downT, upT, aheadY;
	};
	for (const Case c : { Case{ 1131.218994140625f, 0x3EC19666u, 0x3E9F34CDu, 0x448E0702u }, Case{ 1129.781005859375f, 0x3F059B33u, 0x3E74C99Au, 0x448DD8FEu } })
	{
		std::unique_ptr<Path> d = makePath({ pt(685, 1145, 300), pt(695, 1135, 300), pt(695, 1125, 300) });
		d->updateClosestSegment(pt(695, c.y, 300));
		REQUIRE(d->currentNode() != nullptr);
		CHECK(d->currentNode()->getPosition()->y == 1135.0f); // the down leg (695, 1135) -> (695, 1125), as retail on the whole patch
		CHECK(bits(d->currentT()) == c.downT);
		std::unique_ptr<Path> p = makePath({ pt(685, 1145, 300), pt(695, 1135, 300), pt(695, 1125, 300), pt(695, 1145, 300), pt(705, 1155, 300) }, true);
		p->updateClosestSegment(pt(695, c.y, 300));
		REQUIRE(p->currentNode() != nullptr);
		CHECK(p->currentNode()->getPosition()->y == 1125.0f); // the up leg (695, 1125) -> (695, 1145)
		CHECK(bits(p->currentT()) == c.upT);
		const LocomotorPathPoint a = p->computePointAhead(5.0f);
		CHECK(a.position.x == 695.0f);
		CHECK(bits(a.position.y) == c.aheadY); // 5 ahead, up the line (1136.219 / 1134.781)
	}
}

TEST_CASE("path: the remaining length uses the retail approximation max + 0.25 * min per segment")
{
	std::unique_ptr<Path> p = makePath({ pt(0, 0), pt(30, 0), pt(30, 40) });
	LocomotorPathPoint a = p->computePointAhead(15.0f);
	// from (15,0): to B 15 + 0.25 * 0, then BC: max(0, 40) + 0.25 * 0
	CHECK(p->remainingDistanceFrom(a) == doctest::Approx(55.0f));
	std::unique_ptr<Path> d = makePath({ pt(0, 0), pt(30, 40) });
	LocomotorPathPoint b = d->computePointAhead(1.0f);
	// from the start-ish point to (30, 40): max 40 + 0.25 * 30 = 47.5 measured from the returned point
	const float rem = d->remainingDistanceFrom(b);
	const float dx = 30.0f - b.position.x, dy = 40.0f - b.position.y;
	const float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	CHECK(rem == doctest::Approx(ax > ay ? ax + 0.25f * ay : ay + 0.25f * ax));
}

TEST_CASE("path: near the end means the current segment is shorter than 10")
{
	std::unique_ptr<Path> p = makePath({ pt(0, 0), pt(30, 0), pt(30, 5) });
	CHECK(!p->isNearPathEnd()); // no current node yet
	p->updateClosestSegment(pt(5, 0));
	CHECK(!p->isNearPathEnd()); // segment AB is 30 long
	p->updateClosestSegment(pt(30, 3));
	CHECK(p->isNearPathEnd());  // segment BC is 5 long
}

TEST_CASE("path: positionTwoAhead, explicit-z test and the waypoint id default")
{
	std::unique_ptr<Path> p = makePath({ pt(0, 0), pt(10, 0), pt(20, 0), pt(30, 0) });
	p->updateClosestSegment(pt(0, 0));
	Coord3D two = p->positionTwoAhead();
	CHECK(two.x == 20.0f);
	CHECK(!p->hasExplicitZ());
	CHECK(p->getFirstNode()->getWaypointID() == PathNode::NO_WAYPOINT);
	p->getFirstNode()->getNext()->setWaypointID(7);
	CHECK(p->hasExplicitZ()); // (current.nextOpti ?: current).waypointID != none
}
