// OpenBFME unit tests: Path / PathNode follower queries (lane PATH-1). Expected values are derived by hand from the RotWK
// bodies named in AIPathfindPath.cpp (RW 0x765598 updateClosestSegment, 0x765F31 computePointAhead, 0x765972 remaining
// length, 0x5E2DBA near-end test).

#include "doctest.h"
#include "PathfindTestUtil.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

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

// ---------------------------------------------------------------------------------------------------------
// lane PATH-REBUILD: Path::rebuild (RW 0x767B0E), the finish of a found path. Expected values: tools/retail_oracle/refs.py path_rebuild_plain,
// the transcription of the BFME2 decomp that test_path_rebuild_oracle.py holds against RotWK's own function; the arc points carry the port's
// trigonometry (S-167), so they are compared with a tolerance, the counts and the plain copies exactly
// ---------------------------------------------------------------------------------------------------------
namespace
{
struct RebuildRig
{
	pathtest::SyntheticTerrain terrain{ 40, 40 };
	pathtest::TestWorld world;
	Pathfinder pf{ pathtest::testConfig(), &world };
	pathtest::TestObject obj;
	RebuildRig()
	{
		pf.newMap(terrain);
		obj.pos = pt(5, 5);
		obj.mobile = true;
		obj.geometry.type = PATHFIND_GEOMETRY_SPHERE; // the line of sight of the optimiser needs a footprint
		obj.geometry.majorRadius = 5.0f;
	}
	// the search's chain as the optimiser leaves it where the line of sight is blocked at the corners: the raw cells (prependNode from the goal back, each
	// cell's nextOpti its raw next) and the optimised links between the anchors (the head, the corners, the tail), isOptimized set
	std::unique_ptr<Path> searched(const std::vector<Coord3D> &cells, const std::vector<size_t> &anchors)
	{
		std::unique_ptr<Path> p = makePath(cells);
		std::vector<PathNode *> nodes;
		for (PathNode *n = p->getFirstNode(); n; n = n->getNext())
		{
			nodes.push_back(n);
		}
		for (size_t i = 0; i + 1 < anchors.size(); ++i)
		{
			nodes[anchors[i]]->setNextOptimized(nodes[anchors[i + 1]]);
		}
		p->markOptimized();
		return p;
	}
	void rebuild(Path &p, float facingX = 1.0f, float facingY = 0.0f)
	{
		Coord2D facing;
		facing.x = facingX;
		facing.y = facingY;
		p.rebuild(pf, &obj, pathtest::groundLoco(), facing, pathtest::groundLoco().validSurfaces, false);
	}
};

std::vector<Coord3D> cellsAlong(const std::vector<Coord3D> &corners)
{
	// the cells of an axis-aligned polyline through `corners` (10 apart), each once
	std::vector<Coord3D> out;
	for (size_t i = 0; i + 1 < corners.size(); ++i)
	{
		Coord3D c = corners[i];
		const float dx = corners[i + 1].x > c.x ? 10.0f : corners[i + 1].x < c.x ? -10.0f : 0.0f;
		const float dy = corners[i + 1].y > c.y ? 10.0f : corners[i + 1].y < c.y ? -10.0f : 0.0f;
		for (;;)
		{
			if (out.empty() || out.back().x != c.x || out.back().y != c.y)
			{
				out.push_back(c);
			}
			if (c.x == corners[i + 1].x && c.y == corners[i + 1].y)
			{
				break;
			}
			c.x += dx;
			c.y += dy;
		}
	}
	return out;
}

bool rawEqualsOptimised(const Path &p)
{
	for (const PathNode *n = p.getFirstNode(); n; n = n->getNext())
	{
		if (n->getNext() != n->getNextOptimized())
		{
			return false;
		}
	}
	return true;
}

void checkChain(const Path &p, const std::vector<Coord3D> &want, float tolerance)
{
	REQUIRE(p.nodeCount() == want.size());
	size_t i = 0;
	for (const PathNode *n = p.getFirstNode(); n; n = n->getNext(), ++i)
	{
		CAPTURE(i);
		CHECK(std::fabs(n->getPosition()->x - want[i].x) <= tolerance); // tolerance: the arc points' trigonometry (S-167); the copies are exact (0)
		CHECK(std::fabs(n->getPosition()->y - want[i].y) <= tolerance); // tolerance: as above
		CHECK(n->getPosition()->z == want[i].z);
	}
}
} // namespace

TEST_CASE("path: rebuild (RW 0x767B0E) without a locomotor copies the head and the tail, rounds the corner with radius 5 and frees the raw cells; raw == optimised after it")
{
	RebuildRig rig;
	// 17 cells east then north: the search's raw chain; optimised, the anchors are (5, 5), (85, 5), (85, 85)
	std::unique_ptr<Path> p = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	REQUIRE(p->nodeCount() == 17);
	REQUIRE(p->getFirstNode()->getNextOptimized()->getPosition()->x == 85.0f);
	REQUIRE(p->getFirstNode()->getNextOptimized()->getNextOptimized() == p->getLastNode());
	CHECK_FALSE(rawEqualsOptimised(*p));
	rig.rebuild(*p);
	// refs.path_rebuild_plain([(5,5,0),(85,5,0),(85,85,0)]): the corner's arc of radius 5 (2 segments) between (80, 5) and (85, 10)
	checkChain(*p, { pt(5, 5), pt(80, 5), pt(83.5355f, 6.4645f), pt(85, 10), pt(85, 85) }, 0.001f);
	CHECK(p->isOptimized());
	CHECK(rawEqualsOptimised(*p));
	CHECK(p->getFirstNode()->getPrevious() == nullptr);
	CHECK(p->getLastNode()->getNext() == nullptr);
	CHECK(p->getLastNode()->getNextOptimized() == nullptr);
	CHECK(p->currentNode() == nullptr); // a fresh path: the follower has selected nothing
	// the follower works on the rebuilt chain: a unit at (82, 5) is on the first leg, the point ahead on the arc
	p->updateClosestSegment(pt(82, 5));
	CHECK(p->currentNode() == p->getFirstNode()->getNextOptimized());
	const LocomotorPathPoint ahead = p->computePointAhead(2.0f);
	CHECK(std::fabs(ahead.position.x - 83.5355f) <= 0.01f); // tolerance: the arc point
}

TEST_CASE("path: rebuild rounds every corner in turn (the second from where the first arc left the node), keeps z, and copies a shallow corner")
{
	RebuildRig rig;
	std::unique_ptr<Path> p = rig.searched(cellsAlong({ pt(85, 145, 300), pt(155, 145, 300), pt(155, 215, 300), pt(225, 215, 300) }), { 0, 7, 14, 21 });
	rig.rebuild(*p);
	// refs.path_rebuild_plain([(85,145,300),(155,145,300),(155,215,300),(225,215,300)]) (test_path_rebuild_oracle.py holds RotWK to the same chain): the
	// first corner turns right (the arc's interior point between its ends), the second left (RW appends the interior point before rotating it, so it
	// repeats the arc's start and is dropped)
	checkChain(*p, { pt(85, 145, 300), pt(150, 145, 300), pt(153.5355f, 146.4645f, 300), pt(155, 150, 300), pt(155, 210, 300), pt(160, 215, 300), pt(225, 215, 300) }, 0.001f);
	CHECK(rawEqualsOptimised(*p));
	// a corner of 0.197 rad: the radius grows to 8.75 but the arc has one segment, so the corner is copied as it is
	std::unique_ptr<Path> q = makePath({ pt(5, 5), pt(105, 5), pt(205, 25) });
	q->markOptimized();
	rig.rebuild(*q);
	checkChain(*q, { pt(5, 5), pt(105, 5), pt(205, 25) }, 0.0f);
	CHECK(rawEqualsOptimised(*q));
}

TEST_CASE("path: rebuild keeps a node's waypoint id and rounds no corner at it (RW 0x767B0E's portal test), and a one-node path is copied")
{
	RebuildRig rig;
	std::unique_ptr<Path> p = makePath({ pt(5, 5), pt(85, 5), pt(85, 85) });
	p->markOptimized();
	p->getFirstNode()->getNext()->setWaypointID(7);
	rig.rebuild(*p);
	checkChain(*p, { pt(5, 5), pt(85, 5), pt(85, 85) }, 0.0f);
	CHECK(p->getFirstNode()->getNext()->getWaypointID() == 7);
	CHECK(p->getLastNode()->getWaypointID() == PathNode::NO_WAYPOINT);
	std::unique_ptr<Path> one = makePath({ pt(15, 25, 3) });
	rig.rebuild(*one);
	checkChain(*one, { pt(15, 25, 3) }, 0.0f);
	CHECK(one->isOptimized());
}

TEST_CASE("path: rebuild with a locomotor bends the first segment from the unit's facing into an arc of the turn radius, then rounds the corner after it")
{
	RebuildRig rig;
	PathfindLocomotorInfo loco = pathtest::groundLoco();
	loco.present = true;
	loco.fastTurnRadius = 10.0f;
	loco.slowTurnRadius = 20.0f;
	loco.fasterThanQuarterSpeed = true; // the fast radius
	loco.maxTurnWithoutReform = 3.0f;
	loco.turnThresholdHS = 3.0f;
	Coord2D facing;
	facing.x = 0.0f; // facing north, the path goes east: a right-angle bend
	facing.y = 1.0f;
	std::unique_ptr<Path> p = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	p->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	REQUIRE(p->nodeCount() >= 6); // tolerance: the head, the bend's three arc points and at least the corner's arc and the tail; the bend's shape is pinned below
	CHECK(rawEqualsOptimised(*p));
	const PathNode *n = p->getFirstNode();
	CHECK(n->getPosition()->x == 5.0f); // the head's copy
	CHECK(n->getPosition()->y == 5.0f);
	// the bend: RW 0x766F7F turns right (the facing north, the leg east), so its centre is (15, 5) and the 3 arc points are 10 from it, east of the start
	for (int i = 1; i <= 3; ++i)
	{
		n = n->getNext();
		REQUIRE(n != nullptr);
		CAPTURE(i);
		const float dx = n->getPosition()->x - 15.0f, dy = n->getPosition()->y - 5.0f;
		CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - 10.0f) <= 0.01f); // tolerance: the arc's trigonometry (S-167)
		CHECK(n->getPosition()->x > 5.0f);         // tolerance: east of the start, the arc's side
	}
	// the bend sweeps past the leg (RW 0x766F7F adds the remainder angle to the sweep), then the corner after it (the node moved to the midpoint of
	// the arc's end and the leg's end) is rounded with the fast radius and the leg goes on to the corner at (85, 5), itself rounded; the tail (85, 85) copied
	CHECK(n->getPosition()->y > 5.0f); // tolerance: the arc's end is the port's trigonometry (S-167); its radius is pinned above, its side here
	CHECK(p->getLastNode()->getPosition()->x == 85.0f);
	CHECK(p->getLastNode()->getPosition()->y == 85.0f);
	bool cornerRounded = false;
	for (const PathNode *c = p->getFirstNode(); c; c = c->getNext())
	{
		if (c->getPosition()->x == 85.0f && c->getPosition()->y == 5.0f)
		{
			cornerRounded = true; // the corner node itself survives only when nothing bent it
		}
	}
	CHECK_FALSE(cornerRounded);
	// below a quarter of the maximum speed the slow radius (20, under half the first leg) draws the bend: its first arc point is 20 from (25, 5)
	loco.fasterThanQuarterSpeed = false;
	std::unique_ptr<Path> q = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	q->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	const PathNode *m = q->getFirstNode()->getNext();
	REQUIRE(m != nullptr);
	const float dx = m->getPosition()->x - 25.0f, dy = m->getPosition()->y - 5.0f;
	CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - 20.0f) <= 0.01f); // tolerance: the arc's trigonometry (S-167)
	// a bend below 0.196 rad or above MaxTurnWithoutReform is not taken: the head is copied
	facing.x = 1.0f;
	facing.y = 0.0f;
	std::unique_ptr<Path> r = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	r->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(r->getFirstNode()->getNext()->getPosition()->x == 75.0f); // straight to the corner's arc start: the segment speed is the fast radius, 10
	loco.maxTurnWithoutReform = 1.0f;
	facing.x = 0.0f;
	facing.y = 1.0f;
	std::unique_ptr<Path> s = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	s->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(s->getFirstNode()->getNext()->getPosition()->x == 75.0f);
}

TEST_CASE("path: the arc's segment count is RW 0x765E00's x87 product at precision control 24 (a right angle at radius 15 counts 4, not a double's 5)")
{
	// the table tools/retail_oracle/test_path_rebuild_oracle.py holds to RW 0x765E00 itself; a double accumulator lands on 5 / 9 / 17 at radius 15 / 30 / 60
	const float right = 1.5707964f, eighth = 0.7853982f, three8 = 1.1780972f, three4 = 2.3561945f;
	CHECK(Path::arcSegments(right, 5.0f) == 2);
	CHECK(Path::arcSegments(right, 10.0f) == 3);
	CHECK(Path::arcSegments(right, 15.0f) == 4);
	CHECK(Path::arcSegments(right, 20.0f) == 6);
	CHECK(Path::arcSegments(right, 30.0f) == 8);
	CHECK(Path::arcSegments(right, 60.0f) == 16);
	CHECK(Path::arcSegments(eighth, 15.0f) == 2);
	CHECK(Path::arcSegments(eighth, 10.0f) == 2);
	CHECK(Path::arcSegments(three8, 5.0f) == 1);
	CHECK(Path::arcSegments(three4, 5.0f) == 2);
	CHECK(Path::arcSegments(0.0f, 5.0f) == 1);     // the clamp below
	CHECK(Path::arcSegments(3.1415927f, 2000.0f) == 128); // and above
}

TEST_CASE("path: rebuild with a locomotor that can reverse backs a unit facing away from a long first segment out through two arcs (RW 0x767800)")
{
	RebuildRig rig;
	PathfindLocomotorInfo loco = pathtest::groundLoco();
	loco.present = true;
	loco.fastTurnRadius = 10.0f;
	loco.slowTurnRadius = 10.0f;
	loco.fasterThanQuarterSpeed = true;
	loco.maxTurnWithoutReform = 3.0f;
	loco.turnThresholdHS = 3.0f;
	Coord2D facing;
	facing.x = -1.0f; // facing west, the leg goes east: the unit faces away (RW 0x5E389C: the dot product negative)
	facing.y = 0.0f;
	// without the reverse (RW 0x5E389C: WHEELS_FOUR with CanMoveBackwards) a turn of pi exceeds MaxTurnWithoutReform: the head is copied and the leg
	// runs straight to the corner's arc at (75, 5)
	std::unique_ptr<Path> straight = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	straight->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(straight->getFirstNode()->getNext()->getPosition()->x == 75.0f);
	CHECK(straight->getFirstNode()->getNext()->getPosition()->y == 5.0f);
	// with it, on a first segment longer than three turn radii (80 > 30), RW 0x767800 bends twice with the larger radius and no corner arc after
	// either: out of the start beside the leg (the side RW 0x767800 picks from the cross product: north here), then back onto the leg; the chain
	// leaves the start sideways instead of along the leg, every arc point 10 from its centre, and the leg's corner is rounded as before
	loco.canReverse = true;
	std::unique_ptr<Path> p = rig.searched(cellsAlong({ pt(5, 5), pt(85, 5), pt(85, 85) }), { 0, 8, 16 });
	p->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(rawEqualsOptimised(*p));
	REQUIRE(p->nodeCount() > straight->nodeCount()); // tolerance: the two bends' arc points on top of the straight chain; the shape is pinned below
	const PathNode *n = p->getFirstNode();
	CHECK(n->getPosition()->x == 5.0f);
	CHECK(n->getPosition()->y == 5.0f);
	n = n->getNext();
	REQUIRE(n != nullptr);
	CHECK(n->getPosition()->x < 75.0f); // tolerance: the first node after the head is the first arc's, not the corner's
	float maxY = 5.0f;
	int arcPoints = 0;
	for (const PathNode *c = p->getFirstNode(); c; c = c->getNext())
	{
		if (c->getPosition()->x < 70.0f)
		{
			maxY = std::max(maxY, c->getPosition()->y); // the swing beside the start, before the leg's corner
		}
		if (c->getPosition()->x < 70.0f && c->getPosition()->y != 5.0f)
		{
			++arcPoints;
		}
		const bool atCorner = c->getPosition()->x == 85.0f && c->getPosition()->y == 5.0f;
		CHECK_FALSE(atCorner); // the corner is rounded
	}
	CHECK(maxY >= 12.0f);  // tolerance: the first arc's end lies about 13.3 north of the leg (a radius plus the sag toward the side point two radii out)
	CHECK(arcPoints >= 4); // tolerance: two bends of at least two arc points each off the leg's line
	CHECK(p->getLastNode()->getPosition()->x == 85.0f);
	CHECK(p->getLastNode()->getPosition()->y == 85.0f);
	// a first segment of three radii or less is not reversed out of: the head is copied, the leg runs straight (RW 0x767B0E's length test)
	std::unique_ptr<Path> q = rig.searched(cellsAlong({ pt(5, 5), pt(25, 5), pt(25, 85) }), { 0, 2, 10 });
	q->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(q->getFirstNode()->getNext()->getPosition()->x == 15.0f); // straight to the corner's arc start (radius 10 before the corner at 25)
	CHECK(q->getFirstNode()->getNext()->getPosition()->y == 5.0f);
}

TEST_CASE("path: rebuild with a locomotor whose bend is blocked falls back to an unvalidated arc of radius 5 (RW 0x767B0E's two dry runs)")
{
	RebuildRig rig;
	// a structure 10 x 10 on the pathfinder map, on the arc of the bend
	pathtest::TestObject *block = new pathtest::TestObject();
	block->id = 50;
	block->pos = pt(55, 13);
	block->structure = true;
	block->geometry.type = PATHFIND_GEOMETRY_BOX;
	block->geometry.majorRadius = 5.0f;
	block->geometry.minorRadius = 5.0f;
	rig.world.objects[block->id] = block;
	rig.pf.addObjectToPathfindMap(*block);
	PathfindLocomotorInfo loco = pathtest::groundLoco();
	loco.present = true;
	loco.fastTurnRadius = 10.0f;
	loco.slowTurnRadius = 10.0f;
	loco.fasterThanQuarterSpeed = true;
	loco.maxTurnWithoutReform = 3.0f;
	loco.turnThresholdHS = 3.0f;
	Coord2D facing;
	facing.x = 0.0f; // facing north at (45, 5), the leg goes east: the bend is a right turn about (55, 5), whose arc crosses the structure's cells
	facing.y = 1.0f;
	rig.obj.pos = pt(45, 5);
	// the first dry run fails (RW 0x766F7F validates every arc point's cell); the other way is a left turn sweeping three quarters round, below the
	// map; so the bend is drawn the first way with radius 5 and no validation (RW 0x767B0E: its points lie in the structure's cells)
	std::unique_ptr<Path> p = rig.searched(cellsAlong({ pt(45, 5), pt(125, 5), pt(125, 85) }), { 0, 8, 16 });
	p->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(rawEqualsOptimised(*p));
	const PathNode *n = p->getFirstNode()->getNext();
	REQUIRE(n != nullptr);
	const float dx = n->getPosition()->x - 50.0f, dy = n->getPosition()->y - 5.0f;
	CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - 5.0f) <= 0.01f); // tolerance: the arc's trigonometry (S-167)
	CHECK(n->getPosition()->x > 45.0f);                                // tolerance: east of the start, the first way's side
	CHECK(n->getPosition()->y > 5.0f);                                 // tolerance: north of it, the turn
	// a bend past a right angle may go the other way round (RW 0x766F7F refuses the other way under a right angle): facing north-west at (45, 45)
	// with the leg east, the natural turn is a right turn about (52.07, 52.07) whose arc crosses the structure moved onto it; the other way is a
	// left turn about (37.93, 37.93), clear, so the bend is drawn there with the full radius 10 (not the radius-5 fallback)
	rig.pf.removeObjectFromPathfindMap(*block);
	block->pos = pt(58, 60);
	rig.pf.addObjectToPathfindMap(*block);
	facing.x = -0.70710677f;
	facing.y = 0.70710677f;
	rig.obj.pos = pt(45, 45);
	std::unique_ptr<Path> o = rig.searched(cellsAlong({ pt(45, 45), pt(125, 45), pt(125, 125) }), { 0, 8, 16 });
	o->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	CHECK(rawEqualsOptimised(*o));
	const PathNode *w = o->getFirstNode()->getNext();
	REQUIRE(w != nullptr);
	{
		const float dx = w->getPosition()->x - 37.928932f, dy = w->getPosition()->y - 37.928932f;
		CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - 10.0f) <= 0.01f); // tolerance: the arc's trigonometry (S-167)
		CHECK(w->getPosition()->x < 45.0f);                                 // tolerance: west of the start (the arc leaves along the facing, north-west); the right turn's first point lies east
	}
	facing.x = 0.0f;
	facing.y = 1.0f;
	rig.obj.pos = pt(45, 5);
	// without the structure the same bend is drawn with the full radius about (55, 5)
	std::unique_ptr<Path> q = rig.searched(cellsAlong({ pt(45, 5), pt(125, 5), pt(125, 85) }), { 0, 8, 16 });
	rig.pf.removeObjectFromPathfindMap(*block);
	q->rebuild(rig.pf, &rig.obj, loco, facing, loco.validSurfaces, false);
	const PathNode *m = q->getFirstNode()->getNext();
	REQUIRE(m != nullptr);
	const float ex = m->getPosition()->x - 55.0f, ey = m->getPosition()->y - 5.0f;
	CHECK(std::fabs(std::sqrt(ex * ex + ey * ey) - 10.0f) <= 0.01f); // tolerance: the arc's trigonometry (S-167)
}
