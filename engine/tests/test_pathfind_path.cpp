// OpenBFME unit tests: Path / PathNode follower queries (lane PATH-1). Expected values are derived by hand from the RotWK
// bodies named in AIPathfindPath.cpp (RW 0x765598 updateClosestSegment, 0x765F31 computePointAhead, 0x765972 remaining
// length, 0x5E2DBA near-end test).

#include "doctest.h"
#include "PathfindTestUtil.h"

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
std::unique_ptr<Path> makePath(const std::vector<Coord3D> &pts)
{
	std::unique_ptr<Path> p(new Path());
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
