// OpenBFME unit tests: the pathfinder grid, classification, zones, reservation and A* on synthetic maps (lane PATH-1).
// Every expected value is derived by hand from the donor bodies named in AIPathfind.cpp; none is the engine's own output.

#include "doctest.h"
#include "PathfindTestUtil.h"

#include <cstring>
#include <set>

using namespace pathtest;

namespace
{
struct World
{
	SyntheticTerrain terrain;
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	World(int w, int h) : terrain(w, h) {}
	void build()
	{
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
	}
};

PathfindCell::CellType typeAt(Pathfinder &pf, int x, int y)
{
	return pf.cellAt(x, y)->getType();
}

Coord3D at(float x, float y)
{
	Coord3D c;
	c.x = x;
	c.y = y;
	return c;
}

std::vector<Coord3D> optimized(Path *p)
{
	std::vector<Coord3D> out;
	for (PathNode *n = p->getFirstNode(); n; n = n->getNextOptimized())
	{
		out.push_back(*n->getPosition());
	}
	return out;
}

TestObject *makeBox(TestWorld &w, PathfindObjectID id, float x, float y, float major, float minor)
{
	TestObject *o = new TestObject();
	o->id = id;
	o->pos = at(x, y);
	o->structure = true;
	o->geometry.type = PATHFIND_GEOMETRY_BOX;
	o->geometry.majorRadius = major;
	o->geometry.minorRadius = minor;
	w.objects[id] = o;
	return o;
}

TestObject *makeUnit(TestWorld &w, PathfindObjectID id, float x, float y, int team = 0)
{
	TestObject *o = new TestObject();
	o->id = id;
	o->pos = at(x, y);
	o->mobile = true;
	o->team = team;
	o->geometry.type = PATHFIND_GEOMETRY_SPHERE;
	o->geometry.majorRadius = 5.0f; // diameter 10: radius 0, centred
	w.objects[id] = o;
	return o;
}

void cleanup(TestWorld &w)
{
	for (auto &kv : w.objects)
	{
		delete kv.second;
	}
	w.objects.clear();
}
} // namespace

TEST_CASE("pathfind grid: cells are 10 units, the grid covers the maximum extent")
{
	World w(12, 8);
	w.build();
	CHECK(w.pf->getExtent()->x == 11);
	CHECK(w.pf->getExtent()->y == 7);
	PathfindGridStats s = w.pf->gridStats();
	CHECK(s.width == 12);
	CHECK(s.height == 8);
	CHECK(s.types[PathfindCell::CELL_CLEAR] == 96);
	ICoord2D c;
	Coord3D p = at(35.0f, 25.0f);
	CHECK(!w.pf->worldToCell(&p, &c));
	CHECK(c.x == 3);
	CHECK(c.y == 2);
	Coord3D off = at(-5.0f, 500.0f);
	CHECK(w.pf->worldToCell(&off, &c)); // clamped, reported as overflow
	CHECK(c.x == 0);
	CHECK(c.y == 7);
}

TEST_CASE("pathfind classification: cliff cells, the one-step pinch expansion that turns clear neighbours into cliff")
{
	World w(10, 10);
	w.terrain.cliff.insert({ 5, 5 });
	w.build();
	CHECK(typeAt(*w.pf, 5, 5) == PathfindCell::CELL_CLIFF);
	// BFME classifyMap (Open-BFME-1 PathfindClassifyMap.cpp, retail 0x003DC190): every CLEAR cell in the 3x3 block around
	// a cliff cell is marked pinched and becomes CLIFF
	for (int x = 4; x <= 6; ++x)
	{
		for (int y = 4; y <= 6; ++y)
		{
			CHECK(typeAt(*w.pf, x, y) == PathfindCell::CELL_CLIFF);
		}
	}
	CHECK(typeAt(*w.pf, 3, 5) == PathfindCell::CELL_CLEAR);
	CHECK(typeAt(*w.pf, 7, 7) == PathfindCell::CELL_CLEAR);
	PathfindGridStats s = w.pf->gridStats();
	CHECK(s.types[PathfindCell::CELL_CLIFF] == 9);
	CHECK(s.pinched == 8); // the 8 neighbours carry the pinched bit; the original cliff cell does not
}

TEST_CASE("pathfind classification: water above WadeWaterDepth is WATER, above DeepWaterDepth DEEP_WATER, the last corner that passes a test wins (RW 0x934A85)")
{
	World w(10, 10);
	// standing water at z = 20 over x 20..60, y 20..50; terrain z = 0 for x < 35, 14.5 for 35 <= x < 45, 18 beyond:
	// depth 20 (> deep 6), 5.5 (> wade 5, not > deep), 2 (neither)
	SyntheticTerrain::Water a{ 20.0f, 20.0f, 60.0f, 50.0f, 20.0f };
	w.terrain.water.push_back(a);
	w.terrain.heightFn = [](float x, float) { return x < 35.0f ? 0.0f : (x < 45.0f ? 14.5f : 18.0f); };
	w.build();
	// cell (2,2) = x 20..30: every corner is 20 deep: DEEP_WATER
	CHECK(typeAt(*w.pf, 2, 2) == PathfindCell::CELL_DEEP_WATER);
	// cell (3,2) = x 30..40: the corners at x = 30 are deep (type 7), then the corner (40,30) is 5.5 deep: a shallow corner
	// that passes the wade test overwrites the type with WATER (the last water corner wins, it is not a maximum)
	CHECK(typeAt(*w.pf, 3, 2) == PathfindCell::CELL_WATER);
	// cell (4,2) = x 40..50: the corners at x = 40 are 5.5 deep (WATER), the corners at x = 50 are 2 deep and change nothing
	CHECK(typeAt(*w.pf, 4, 2) == PathfindCell::CELL_WATER);
	// cell (5,2) = x 50..60: depth 2 at x = 50, x = 60 is outside the water: CLEAR
	CHECK(typeAt(*w.pf, 5, 2) == PathfindCell::CELL_CLEAR);
	CHECK(typeAt(*w.pf, 0, 0) == PathfindCell::CELL_CLEAR);
	// a cell that is a cliff is never water
	World c(10, 10);
	c.terrain.water.push_back(a);
	c.terrain.heightFn = w.terrain.heightFn;
	c.terrain.cliff.insert({ 2, 2 });
	c.build();
	CHECK(typeAt(*c.pf, 2, 2) == PathfindCell::CELL_CLIFF);
}

TEST_CASE("pathfind: a straight path in the open is the start position and the goal cell centre")
{
	World w(10, 10);
	w.build();
	Coord3D from = at(5.0f, 5.0f), to = at(55.0f, 5.0f);
	Path *p = w.pf->findPath(nullptr, groundLoco(), &from, &to);
	REQUIRE(p != nullptr);
	std::vector<Coord3D> chain = optimized(p);
	REQUIRE(chain.size() == 2);
	CHECK(chain[0].x == 5.0f);
	CHECK(chain[0].y == 5.0f);
	CHECK(chain[1].x == 55.0f);
	CHECK(chain[1].y == 5.0f);
	// the unoptimised cell path has a node per cell: from, then the centres of cells 1..5
	CHECK(p->nodeCount() == 6);
	delete p;
	// no cell info stays allocated after a search
	CHECK(w.pf->pool().freeCount() == testConfig().cellInfoPoolSize);
}

TEST_CASE("pathfind: a wall with a gap is walked around, the same request gives the same path twice")
{
	World w(14, 8);
	// a structure 10 wide, 60 deep: cells x 6..7 (the box covers x 55..65 after rounding) and y 0..5 blocked, gap at y 6..7
	TestObject *wall = makeBox(w.world, 50, 65.0f, 30.0f, 5.0f, 30.0f);
	w.build();
	w.pf->addObjectToPathfindMap(*wall);
	Coord3D from = at(25.0f, 25.0f), to = at(105.0f, 25.0f);
	Path *p = w.pf->findPath(nullptr, groundLoco(), &from, &to);
	REQUIRE(p != nullptr);
	std::vector<Coord3D> chain = optimized(p);
	REQUIRE(chain.size() >= 3);
	float maxY = 0.0f;
	for (const Coord3D &c : chain)
	{
		maxY = std::max(maxY, c.y);
	}
	// the path must climb to the gap rows (y >= 60) to pass the wall
	CHECK(maxY >= 60.0f);
	// and it never crosses an obstacle cell: every segment is passable for ground units
	for (size_t i = 0; i + 1 < chain.size(); ++i)
	{
		CHECK(w.pf->isLinePassable(nullptr, LOCOMOTORSURFACE_GROUND, LAYER_GROUND, chain[i], chain[i + 1], false, true));
	}
	Path *q = w.pf->findPath(nullptr, groundLoco(), &from, &to);
	REQUIRE(q != nullptr);
	std::vector<Coord3D> chain2 = optimized(q);
	REQUIRE(chain2.size() == chain.size());
	for (size_t i = 0; i < chain.size(); ++i)
	{
		CHECK(chain[i].x == chain2[i].x);
		CHECK(chain[i].y == chain2[i].y);
	}
	delete p;
	delete q;
	cleanup(w.world);
}

TEST_CASE("pathfind: a destination enclosed by cliffs is refused by the zone test without searching")
{
	World w(12, 12);
	// a ring of cliff cells around (6,6)
	for (int x = 4; x <= 8; ++x)
	{
		for (int y = 4; y <= 8; ++y)
		{
			if (x == 4 || x == 8 || y == 4 || y == 8)
			{
				w.terrain.cliff.insert({ x, y });
			}
		}
	}
	w.build();
	Coord3D from = at(15.0f, 15.0f), inside = at(65.0f, 65.0f);
	CHECK(typeAt(*w.pf, 6, 6) == PathfindCell::CELL_CLEAR);
	CHECK(!w.pf->clientSafeQuickDoesPathExist(groundLoco(), &from, &inside));
	// RW internalFindPath: an unreachable goal gives a path to the closest valid cell (ZH returns null), flagged partial
	bool partial = false;
	Path *toClosest = w.pf->findPath(nullptr, groundLoco(), &from, &inside, &partial);
	REQUIRE(toClosest != nullptr);
	CHECK(partial);
	const PathNode *last = toClosest->getLastNode();
	REQUIRE(last != nullptr);
	const int lx = (int)(last->getPosition()->x / 10.0f), ly = (int)(last->getPosition()->y / 10.0f);
	CHECK(!(lx >= 4 && lx <= 8 && ly >= 4 && ly <= 8)); // never inside the ring
	CHECK(std::abs(lx - 6) + std::abs(ly - 6) >= 3);    // outside the dilated ring
	delete toClosest;
	// a cliff-capable locomotor reaches it (zone 1 for ground + water + cliff; ground + cliff through the equivalence table)
	PathfindLocomotorInfo climber;
	climber.validSurfaces = LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_CLIFF;
	CHECK(w.pf->clientSafeQuickDoesPathExist(climber, &from, &inside));
}

TEST_CASE("pathfind reservation: a goal cell is exclusive for allies, adjustDestination walks the ring: x+1, then y+1")
{
	World w(12, 12);
	w.build();
	TestObject *a = makeUnit(w.world, 1, 25.0f, 25.0f);
	TestObject *b = makeUnit(w.world, 2, 85.0f, 85.0f);
	Coord3D goal = at(55.0f, 55.0f);
	w.pf->updateGoal(*a, &goal, LAYER_GROUND);
	CHECK(w.pf->pathfindGoalCell(1).x == 5);
	CHECK(w.pf->pathfindGoalCell(1).y == 5);
	CHECK(w.pf->cellAt(5, 5)->isOccupiedBy(OCC_GROUND_GOAL, 1));

	// B (an ally of A) cannot take the cell; the first ring cell it may take is (6,5), centre (65,55)
	Coord3D dest = goal;
	CHECK(w.pf->adjustDestination(*b, groundLoco(), &dest));
	CHECK(dest.x == 65.0f);
	CHECK(dest.y == 55.0f);

	// an enemy of A may stand on the goal cell (nobody is physically there)
	TestObject *e = makeUnit(w.world, 3, 85.0f, 25.0f, 1);
	Coord3D dest2 = goal;
	CHECK(w.pf->adjustDestination(*e, groundLoco(), &dest2));
	CHECK(dest2.x == 55.0f);
	CHECK(dest2.y == 55.0f);

	// A removes the goal: the cell is free again and its record is released
	w.pf->removeGoal(*a);
	CHECK(!w.pf->cellAt(5, 5)->hasOccupants(OCC_GROUND_GOAL));
	Coord3D dest3 = goal;
	CHECK(w.pf->adjustDestination(*b, groundLoco(), &dest3));
	CHECK(dest3.x == 55.0f);
	cleanup(w.world);
}

TEST_CASE("pathfind reservation: position cells follow updatePos and are removed with the unit")
{
	World w(10, 10);
	w.build();
	TestObject *a = makeUnit(w.world, 1, 25.0f, 25.0f);
	w.pf->updatePos(*a);
	CHECK(w.pf->cellAt(2, 2)->isOccupiedBy(OCC_POSITION, 1));
	a->pos = at(45.0f, 25.0f);
	w.pf->updatePos(*a);
	CHECK(!w.pf->cellAt(2, 2)->hasOccupants(OCC_POSITION));
	CHECK(w.pf->cellAt(4, 2)->isOccupiedBy(OCC_POSITION, 1));
	w.pf->removeUnitFromPathfindMap(*a);
	CHECK(!w.pf->cellAt(4, 2)->hasOccupants(OCC_POSITION));
	CHECK(w.pf->pool().freeCount() == testConfig().cellInfoPoolSize);
	cleanup(w.world);
}

TEST_CASE("pathfind footprint radius: diameter 2 * bounding radius, 10 < d < 20 becomes 20, floor(d / 10 + 0.3), halved when odd, capped at 2 (4 for machines and hordes)")
{
	World w(10, 10);
	w.build();
	TestObject o;
	o.geometry.type = PATHFIND_GEOMETRY_SPHERE;
	int r;
	bool c;
	o.geometry.majorRadius = 5.0f; // d 10: floor(1.3) = 1, odd: centred, radius 0
	w.pf->getRadiusAndCenter(&o, r, c);
	CHECK(r == 0);
	CHECK(c);
	o.geometry.majorRadius = 7.0f; // d 14 -> 20: floor(2.3) = 2, even: not centred, radius 1
	w.pf->getRadiusAndCenter(&o, r, c);
	CHECK(r == 1);
	CHECK(!c);
	o.geometry.majorRadius = 15.0f; // d 30: floor(3.3) = 3, odd: centred, radius 1
	w.pf->getRadiusAndCenter(&o, r, c);
	CHECK(r == 1);
	CHECK(c);
	o.geometry.majorRadius = 45.0f; // d 90: 9 -> radius 4 -> capped at 2, centred
	w.pf->getRadiusAndCenter(&o, r, c);
	CHECK(r == 2);
	CHECK(c);
	o.kinds.insert(PK_HORDE); // RW 0x6ED071: a horde (or ship) searches with radius 1, centred
	w.pf->getRadiusAndCenter(&o, r, c);
	CHECK(r == 1);
	CHECK(c);
	w.pf->getRadiusAndCenter(nullptr, r, c);
	CHECK(r == 0);
	CHECK(c);
}

TEST_CASE("pathfind queue: 512-slot ring, duplicate requests are suppressed, the frame budget stops the draining")
{
	World w(10, 10);
	w.build();
	TestObject *a = makeUnit(w.world, 1, 15.0f, 15.0f);
	TestObject *b = makeUnit(w.world, 2, 25.0f, 25.0f);
	TestObject *c = makeUnit(w.world, 3, 35.0f, 35.0f);
	(void)a;
	(void)b;
	(void)c;
	CHECK(w.pf->queueForPath(1));
	CHECK(w.pf->queueForPath(2));
	CHECK(w.pf->queueForPath(1)); // already queued: nothing added
	CHECK(w.pf->queuedRequests() == 2);
	struct Handler : PathfindRequestHandler
	{
		std::vector<PathfindObjectID> ids;
		void doPathfind(PathfindObjectID id) override { ids.push_back(id); }
	} h;
	w.pf->processPathfindQueue(h);
	REQUIRE(h.ids.size() == 2);
	CHECK(h.ids[0] == 1);
	CHECK(h.ids[1] == 2);
	CHECK(w.pf->queuedRequests() == 0);
	// a request for an object the world no longer knows is dropped without a call
	w.pf->queueForPath(99);
	w.pf->processPathfindQueue(h);
	CHECK(h.ids.size() == 2);
	// the ring holds 511 entries (head == next tail means full)
	for (PathfindObjectID id = 1000; id < 1000 + 511; ++id)
	{
		CHECK(w.pf->queueForPath(id));
	}
	CHECK(!w.pf->queueForPath(5000));
	CHECK(w.pf->stops().size() >= 1);
	cleanup(w.world);
}

TEST_CASE("pathfind stops: what a build and a search report is registered, every line names its S-16x row")
{
	World w(12, 12);
	for (int x = 4; x <= 8; ++x)
	{
		for (int y = 4; y <= 8; ++y)
		{
			if (x == 4 || x == 8 || y == 4 || y == 8)
			{
				w.terrain.cliff.insert({ x, y });
			}
		}
	}
	w.build();
	Coord3D from = at(15.0f, 15.0f), inside = at(65.0f, 65.0f), far = at(105.0f, 105.0f);
	bool partial = false;
	delete w.pf->findPath(nullptr, groundLoco(), &from, &inside, &partial);
	delete w.pf->findPath(nullptr, groundLoco(), &from, &far, &partial);
	const std::vector<std::string> all = Pathfinder::allStops();
	REQUIRE(all.size() == 16);
	std::set<std::string> registered(all.begin(), all.end());
	CHECK(registered.size() == all.size());
	std::set<int> rows;
	for (const std::string &s : all)
	{
		REQUIRE(s.compare(0, 4, "S-16") == 0);
		rows.insert(s[4] - '0');
	}
	// the rows the pathfinder core raises: S-160 classification, S-161 layers, S-162 zones, S-163 search, S-164 footprints, S-165 path,
	// S-167 numerics (S-166, the AI move path, has its own list: AIMover::allStops)
	CHECK(rows == std::set<int>{ 0, 1, 2, 3, 4, 5, 7 });
	REQUIRE(!w.pf->stops().empty());
	for (const std::string &s : w.pf->stops())
	{
		CHECK_MESSAGE(registered.count(s) == 1, "unregistered stop line: " << s);
	}
	auto raised = [&](const char *prefix) {
		for (const std::string &s : w.pf->stops())
		{
			if (s.compare(0, std::strlen(prefix), prefix) == 0) return true;
		}
		return false;
	};
	CHECK(raised("S-160 slope limits"));
	CHECK(raised("S-161 only the ground layer"));
	CHECK(raised("S-162 the hierarchical block search of findPath (RW 0x6F9829)"));
	CHECK(raised("S-165 path nodes"));
	CHECK(raised("S-167 float32"));
	cleanup(w.world);
}

TEST_CASE("pathfind search budget: past MaxCellsFindPathLimit the search returns a path to the closest cell it reached, flagged partial")
{
	// a serpentine of two walls forces a long walk; with the limit at 20 cells the A* cannot finish
	World w(30, 12);
	for (int x : { 8, 16 })
	{
		for (int y = 0; y < 12; ++y)
		{
			if (!((x == 8 && y >= 8) || (x == 16 && y <= 3))) // a one cell gap would be closed by the pinch dilation
			{
				w.terrain.cliff.insert({ x, y });
			}
		}
	}
	PathfindConfig cfg = testConfig();
	cfg.findPathLimit = 20;
	w.pf = std::make_unique<Pathfinder>(cfg, &w.world);
	w.pf->newMap(w.terrain);
	Coord3D from = at(15.0f, 55.0f), to = at(285.0f, 55.0f);
	bool partial = false;
	std::unique_ptr<Path> limited(w.pf->findPath(nullptr, groundLoco(), &from, &to, &partial));
	REQUIRE(limited != nullptr);
	CHECK(partial);
	const PathNode *last = limited->getLastNode();
	REQUIRE(last != nullptr);
	CHECK(last->getPosition()->x < 80.0f); // never got past the first wall
	// the same search with the retail limit finds the whole way
	World w2(30, 12);
	w2.terrain = w.terrain;
	w2.build();
	partial = true;
	std::unique_ptr<Path> full(w2.pf->findPath(nullptr, groundLoco(), &from, &to, &partial));
	REQUIRE(full != nullptr);
	CHECK(!partial);
	CHECK(std::fabs(full->getLastNode()->getPosition()->x - 285.0f) < 0.5f);
	CHECK(w2.pf->pool().freeCount() == testConfig().cellInfoPoolSize);
	CHECK(w.pf->pool().freeCount() == testConfig().cellInfoPoolSize);
}
