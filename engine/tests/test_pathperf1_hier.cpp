// OpenBFME unit tests. GPL-3.0.
// Lane PATHPERF-1: findPath's hierarchical corridor (RW 0x6F8601, AIPathfindHierarchical.cpp) on synthetic terrain: no retail data.
#include "doctest.h"
#include "PathfindTestUtil.h"
#include "Common/StateHash.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace pathtest;

namespace
{
struct HierWorld
{
	SyntheticTerrain terrain;
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	HierWorld(int w, int h) : terrain(w, h) {}
	void build()
	{
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
	}
};

Coord3D cellCentre(int x, int y)
{
	Coord3D c;
	c.x = (float)x * 10.0f + 5.0f;
	c.y = (float)y * 10.0f + 5.0f;
	c.z = 0.0f;
	return c;
}

// a 128 x 64 cell map split by a cliff wall filling the block column x = 64..79 below y = 48, the top block row open: the way from the lower left to the
// lower right is a detour. The wall fills whole blocks, so the block search sees it (a wall inside a block too: the block zones split there, see the
// wall-inside-a-block test below)
void wallWithGap(HierWorld &w)
{
	for (int x = 64; x < 80; ++x)
	{
		for (int y = 0; y < 48; ++y)
		{
			w.terrain.cliff.insert({ x, y });
		}
	}
	w.build();
}

std::vector<Coord3D> nodes(const Path *p)
{
	std::vector<Coord3D> out;
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		out.push_back(*n->getPosition());
	}
	return out;
}
} // namespace

TEST_CASE("pathperf1: costToHierGoal is RW 0x9345AA, floor(sqrt(dx*dx + dy*dy) * 10 + 0.5) of the cell index difference")
{
	HierWorld w(32, 32);
	w.build();
	Pathfinder &pf = *w.pf;
	auto cell = [&](int x, int y) {
		PathfindCell *c = pf.getCell(LAYER_GROUND, x, y);
		REQUIRE(c != nullptr);
		REQUIRE(c->allocateInfo(pf.pool(), ICoord2D{ x, y }));
		return c;
	};
	PathfindCell *o = cell(0, 0);
	CHECK(o->costToHierGoal(o) == 0u);
	CHECK(o->costToHierGoal(cell(3, 4)) == 50u);  // 5 * 10
	CHECK(o->costToHierGoal(cell(1, 1)) == 14u);  // 14.142 + 0.5
	CHECK(o->costToHierGoal(cell(7, 9)) == 114u); // sqrt(130) * 10 = 114.018
	CHECK(o->costToHierGoal(cell(0, 16)) == 160u);
	CHECK(cell(7, 9)->costToHierGoal(o) == 114u); // symmetric
}

TEST_CASE("pathperf1: a detour around a wall runs inside the hierarchical corridor: a full path, fewer cells than the search of every block, the corridor numbered "
		  "from the start's block to the goal's")
{
	HierWorld w(128, 64);
	wallWithGap(w);
	Pathfinder &pf = *w.pf;
	const Coord3D from = cellCentre(10, 10), to = cellCentre(110, 10);
	bool partial = true;
	const int before = pf.cumulativeCellsAllocated();
	std::unique_ptr<Path> p(pf.findPath(nullptr, groundLoco(), &from, &to, &partial));
	const int corridorCells = pf.cumulativeCellsAllocated() - before;
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(end.x == doctest::Approx(to.x).epsilon(0.001));
	CHECK(end.y == doctest::Approx(to.y).epsilon(0.001));
	// the path goes through the opening
	bool throughGap = false;
	for (const Coord3D &c : nodes(p.get()))
	{
		throughGap = throughGap || (c.x >= 640.0f && c.x <= 800.0f && c.y >= 480.0f);
	}
	CHECK(throughGap);
	// the corridor: the start's block is number 0, the goal's the last; a block on the far side of the wall's foot is off it
	const int last = pf.hierarchicalLastIndex();
	CHECK(last >= 2);
	CHECK(pf.hierarchicalPositionOf(10, 10) == 0);
	CHECK(pf.hierarchicalPositionOf(110, 10) == last);
	CHECK(pf.zoneManager().isPassable(10, 10));
	CHECK(pf.zoneManager().isPassable(110, 10));
	CHECK(pf.hierarchicalPositionOf(120, 60) == -1);
	CHECK_FALSE(pf.zoneManager().isPassable(120, 60));
	// the same search over every block (the search before PATHPERF-1) reaches the goal with more cells
	const int before2 = pf.cumulativeCellsAllocated();
	bool partial2 = true;
	std::unique_ptr<Path> q(pf.findPathWithoutCorridor(nullptr, groundLoco(), &from, &to, &partial2));
	const int allCells = pf.cumulativeCellsAllocated() - before2;
	REQUIRE(q != nullptr);
	CHECK_FALSE(partial2);
	MESSAGE("cells: with the corridor " << corridorCells << " (the block search included), every block " << allCells);
	CHECK(corridorCells < allCells);
}

TEST_CASE("pathperf1: a goal the unit cannot reach gets the corridor to the closest block cell and a partial path")
{
	HierWorld w(96, 64);
	// a closed ring of cliffs around the goal
	for (int i = 60; i <= 80; ++i)
	{
		for (int t = 0; t < 2; ++t)
		{
			w.terrain.cliff.insert({ i, 20 + t });
			w.terrain.cliff.insert({ i, 40 + t });
			w.terrain.cliff.insert({ 60 + t, 20 + (i - 60) });
			w.terrain.cliff.insert({ 79 + t, 20 + (i - 60) });
		}
	}
	w.build();
	Pathfinder &pf = *w.pf;
	const Coord3D from = cellCentre(10, 30), to = cellCentre(70, 30);
	bool partial = false;
	std::unique_ptr<Path> p(pf.findPath(nullptr, groundLoco(), &from, &to, &partial));
	REQUIRE(p != nullptr);
	CHECK(partial);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(end.x < 600.0f); // it stops outside the ring
}

TEST_CASE("pathperf1: the corridor search is repeatable: two worlds give the same path node for node")
{
	std::vector<Coord3D> got[2];
	for (int k = 0; k < 2; ++k)
	{
		HierWorld w(128, 64);
		wallWithGap(w);
		const Coord3D from = cellCentre(12, 5), to = cellCentre(115, 20);
		std::unique_ptr<Path> p(w.pf->findPath(nullptr, groundLoco(), &from, &to, nullptr));
		REQUIRE(p != nullptr);
		got[k] = nodes(p.get());
	}
	REQUIRE(got[0].size() == got[1].size());
	for (size_t i = 0; i < got[0].size(); ++i)
	{
		CHECK(got[0][i].x == got[1][i].x);
		CHECK(got[0][i].y == got[1][i].y);
	}
}

TEST_CASE("pathperf1: the block search's closest-cell key is RW's binary32 dy*dy + dx*dx (RW 0x6F937E): above 2^24 keys that differ as integers tie")
{
	CHECK(Pathfinder::hierarchicalClosestKey(3, -4) == 25.0f);
	CHECK(Pathfinder::hierarchicalClosestKey(-3, 4) == 25.0f);
	// 4096^2 + 1 = 16777217 is not a binary32: addss rounds it to 16777216, the key of (4096, 0); as integers the two differ
	CHECK(Pathfinder::hierarchicalClosestKey(4096, 1) == Pathfinder::hierarchicalClosestKey(4096, 0));
	CHECK(Pathfinder::hierarchicalClosestKey(4096, 1) == 16777216.0f);
	// 4097^2 = 16785409 rounds to 16785408 (mulss), 9 + 16785408 = 16785417 to 16785416 (addss, ties to even): the order of the rounded keys
	CHECK(Pathfinder::hierarchicalClosestKey(4097, 0) == 16785408.0f);
	CHECK(Pathfinder::hierarchicalClosestKey(4097, 3) == 16785416.0f);
}

TEST_CASE("pathperf1: a wall inside one block splits it into two block zones: the block search goes round through the opening (RW 0x9389A6, BFME2 "
		  "0x005329C8's block-local links)")
{
	// a cliff wall at x = 60..61 below y = 52 lies inside the block column x = 48..63. Retail's hierarchical search compares block zones (the even union
	// tables, linked inside a block only): the cells west and east of the wall are two zones of each of the three lower blocks, one zone of the top block
	// (the opening at y >= 52 joins them there). The block search therefore climbs to the top row, crosses at the opening and comes down
	HierWorld w(128, 64);
	for (int x = 60; x <= 61; ++x)
	{
		for (int y = 0; y < 52; ++y)
		{
			w.terrain.cliff.insert({ x, y });
		}
	}
	w.build();
	Pathfinder &pf = *w.pf;
	const PathfindZoneManager &zm = pf.zoneManager();
	const PathfindMovement mv = pf.makeMovement(nullptr, groundLoco());
	auto zoneAt = [&](int x, int y) { return pf.getCell(LAYER_GROUND, x, y)->getZone(); };
	// the block zones: split below the opening, joined in the block that holds it; the global zone is one ground zone everywhere
	CHECK(zm.getBlockZone(mv, zoneAt(55, 10)) != zm.getBlockZone(mv, zoneAt(63, 10)));
	CHECK(zm.getBlockZone(mv, zoneAt(55, 58)) == zm.getBlockZone(mv, zoneAt(63, 58)));
	CHECK(zm.getEffectiveZone(mv, false, zoneAt(55, 10)) == zm.getEffectiveZone(mv, false, zoneAt(63, 10)));
	const Coord3D from = cellCentre(10, 10), to = cellCentre(110, 10);
	bool partial = true;
	std::unique_ptr<Path> p(pf.findPath(nullptr, groundLoco(), &from, &to, &partial));
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	CHECK(pf.hierarchicalLastIndex() >= 2);
	CHECK(pf.hierarchicalPositionOf(60, 56) >= 0); // tolerance: on the corridor at any index (-1 is off it); the opening's block is on the corridor
	bool throughOpening = false;
	for (const Coord3D &c : nodes(p.get()))
	{
		throughOpening = throughOpening || (c.x >= 600.0f && c.x <= 620.0f && c.y >= 520.0f);
	}
	CHECK(throughOpening);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(end.x == doctest::Approx(to.x).epsilon(0.001));
}

TEST_CASE("pathperf1: from a plateau whose only way down is behind the unit, the corridor leaves by that way (Nanduhirion's Signal Fire hills)")
{
	// a plateau ringed by a cliff two cells thick, x = 40..81, y = 40..81, open only in the middle of its north side (x = 58..63); its cliffs run through
	// the middle of blocks. The goal lies due south of the start. With one global zone for the whole ground the block search went straight south across
	// the plateau's south cliff and the cell search, held to that corridor, came back partial at the cliff's foot (an AI horde stood there at the
	// Signal Fire); with block zones the corridor goes north through the opening and round
	// A battalion's search under the cell budget of the horde tests above (findPathLimit 3000): the cells a +1000 detour off the corridor needs
	// exceed it, as on the real map
	SyntheticTerrain terrain(128, 128);
	for (int i = 40; i <= 81; ++i)
	{
		for (int t = 0; t < 2; ++t)
		{
			terrain.cliff.insert({ i, 40 + t });  // south side
			terrain.cliff.insert({ 40 + t, i });  // west side
			terrain.cliff.insert({ 80 + t, i });  // east side
			if (i < 58 || i > 63)
			{
				terrain.cliff.insert({ i, 80 + t }); // north side, open at x = 58..63
			}
		}
	}
	TestWorld world;
	auto config = testConfig();
	config.findPathLimit = 3000;
	Pathfinder pf(config, &world);
	pf.newMap(terrain);
	TestObject battalion;
	battalion.mobile = true;
	battalion.kinds.insert(PK_HORDE);
	battalion.pos = cellCentre(60, 50);
	const Coord3D from = battalion.pos, to = cellCentre(60, 10);
	bool partial = true;
	std::unique_ptr<Path> p(pf.findPath(&battalion, groundLoco(), &from, &to, &partial));
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(end.x == to.x); // the goal is a cell centre: the path ends on it
	CHECK(end.y == to.y);
	bool northExit = false;
	for (const Coord3D &c : nodes(p.get()))
	{
		northExit = northExit || (c.y >= 800.0f && c.x >= 580.0f && c.x <= 640.0f);
	}
	CHECK(northExit);
}

TEST_CASE("pathperf1: a goal inside the obstacle the unit ignores gets no corridor (INFERENCE, S-162): the cell search reaches it; not ignored, the block search "
		  "ends beside it")
{
	HierWorld w(64, 64);
	w.build();
	TestObject *site = new TestObject();
	site->id = 7;
	site->pos = cellCentre(32, 32);
	site->structure = true;
	site->geometry.type = PATHFIND_GEOMETRY_BOX;
	site->geometry.majorRadius = 40.0f;
	site->geometry.minorRadius = 40.0f;
	w.world.objects[site->id] = site;
	Pathfinder &pf = *w.pf;
	pf.addObjectToPathfindMap(*site);
	REQUIRE(pf.cellAt(32, 32)->getType() == PathfindCell::CELL_OBSTACLE);
	// the zones as processPathfindQueue keeps them every frame (the footprint marked its blocks dirty)
	pf.zoneManager().updateDirty(pf.gridView(), PathfindRegion{ ICoord2D{ 0, 0 }, *pf.getExtent() });
	const Coord3D from = cellCentre(4, 4), to = cellCentre(32, 32);
	// a builder walking onto its own site: the site is the ignored obstacle
	pf.setIgnoreObstacleID(site->id);
	bool partial = true;
	std::unique_ptr<Path> p(pf.findPath(nullptr, groundLoco(), &from, &to, &partial));
	REQUIRE(p != nullptr);
	CHECK(pf.hierarchicalLastIndex() == -1); // no corridor
	CHECK_FALSE(partial);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(std::fabs(end.x - to.x) < 10.0f);
	CHECK(std::fabs(end.y - to.y) < 10.0f);
	// any other unit: the block search runs and can only end beside the obstacle
	pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
	bool partial2 = false;
	std::unique_ptr<Path> q(pf.findPath(nullptr, groundLoco(), &from, &to, &partial2));
	CHECK(pf.hierarchicalLastIndex() >= 0);
	CHECK(partial2);
	w.world.objects.clear();
	delete site;
}

TEST_CASE("pathperf1: the S-162 and S-163 stop lines, whole (the inferences and the unported branches of the corridor and the closest cell)")
{
	const std::vector<std::string> all = Pathfinder::allStops();
	auto has = [&](const std::string &line) { return std::find(all.begin(), all.end(), line) != all.end(); };
	CHECK(has("S-162 the hierarchical block search of findPath (RW 0x6F9829) compares block zones (RW 0x9389A6) with its raised links (RW 0x9389CE) and block "
			  "portals (RW 0x937F2D / 0x937F7F); the mineshaft network's intermediate goal (RW 0x77559F, type 8 waypoints) is not ported, and the block-edge passability "
			  "(RW 0x938240) is the port's zone pair test (INFERRED equal); "
			  "a goal inside the obstacle the unit ignores (a builder's own site) gets no corridor (INFERENCE: the port's builders aim inside the footprint)"));
	// lane ATTACKPATH-1: findAttackPath's own corridor (RW 0x6FC4CB .. 0x6FC4F9) is ported and left the S-162 line
	CHECK(has("S-163 a complete path skips the rebuild RW applies to it from the unit's facing (RW 0x767B0E, called at RW 0x6FDCDA; the path to the closest cell gets none), the closest cell's third test (RW 0x6EF3C3) and the search-free path of an object whose + 0x4AC flag is set (RW 0x6FD66D) are not ported"));
}

// PR #111 (Uber profile only): an incomplete battalion corridor route is retried without the corridor. RW 0x6FE7CB has no second search, so the
// Faithful profile keeps the corridor's partial route, its cell count and its pathfinder state (the hash a build without the retry gives)
TEST_CASE("pathperf1: a battalion's route round a wall inside a block is complete in one search, Faithful and Uber alike (block zones: the corridor takes "
		  "the opening, nothing is left for the Uber retry)")
{
	SyntheticTerrain terrain(128, 64);
	for (int x = 60; x <= 61; ++x)
		for (int y = 0; y < 52; ++y)
			terrain.cliff.insert({ x, y });
	TestWorld world;
	auto config = testConfig();
	config.findPathLimit = 3000;
	TestObject battalion;
	battalion.mobile = true;
	battalion.kinds.insert(PK_HORDE);
	battalion.pos = cellCentre(10, 10);
	const Coord3D to = cellCentre(110, 10);
	struct Outcome
	{
		std::unique_ptr<Path> path;
		bool partial = false;
		int cells = 0;
		std::uint32_t hash = 0;
	};
	auto run = [&](bool uber, TestObject *obj) {
		Pathfinder pf(config, &world);
		pf.newMap(terrain);
		pf.setUberLimits(uber);
		Outcome o;
		o.path.reset(pf.findPath(obj, groundLoco(), &battalion.pos, &to, &o.partial));
		o.cells = pf.cumulativeCellsAllocated();
		StateHasher h;
		pf.crc(h);
		o.hash = h.value();
		return o;
	};
	for (bool uber : { false, true })
	{
		INFO(uber);
		for (TestObject *obj : { &battalion, (TestObject *)nullptr })
		{
			const Outcome o = run(uber, obj);
			REQUIRE(o.path != nullptr);
			CHECK_FALSE(o.partial);
			CHECK(o.path->getLastNode()->getPosition()->x == to.x);
			CHECK(o.path->getLastNode()->getPosition()->y == to.y);
		}
	}
	// the battalion searches once in both modes: the same cells and the same state
	const Outcome faithful = run(false, &battalion), uber = run(true, &battalion);
	CHECK(uber.cells == faithful.cells);
	CHECK(faithful.cells == 1768); // the corridor search alone (3360 and partial before block zones)
	CHECK(faithful.hash == 1620720418u);
}

TEST_CASE("pathperf1: an Uber battalion's retry preserves an incomplete route when a wall has no opening")
{
	SyntheticTerrain terrain(128, 64);
	for (int x = 60; x <= 61; ++x)
		for (int y = 0; y < 64; ++y)
			terrain.cliff.insert({ x, y });
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	pf.setUberLimits(true);
	TestObject battalion;
	battalion.mobile = true;
	battalion.kinds.insert(PK_HORDE);
	battalion.pos = cellCentre(10, 10);
	const Coord3D to = cellCentre(110, 10);
	bool partial = false;
	std::unique_ptr<Path> path(pf.findPath(&battalion, groundLoco(), &battalion.pos, &to, &partial));
	REQUIRE(path != nullptr);
	CHECK(partial);
	// tolerance: a sealed barrier at cell 60 must keep every path node strictly on the starting side.
	for (const Coord3D &node : nodes(path.get())) CHECK(node.x < 600.0f);
}
