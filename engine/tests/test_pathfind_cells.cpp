// OpenBFME unit tests: PathfindCell (lane PATH-1). Expected values are written out literally from the donor
// bodies they port (ZH AIPathfind.cpp:1100-1760; Open-BFME-1 PathfindCell_costSoFar.cpp, retail 0x003F6D20).

#include "doctest.h"
#include "PathfindTestUtil.h"

namespace
{
struct Fixture
{
	PathfindCellInfoPool pool;
	std::vector<PathfindCell> cells;
	int w = 8;
	Fixture()
	{
		pool.allocate(64);
		cells.resize((size_t)w * (size_t)w);
	}
	PathfindCell &at(int x, int y)
	{
		PathfindCell &c = cells[(size_t)x * (size_t)w + (size_t)y];
		ICoord2D p;
		p.x = x;
		p.y = y;
		REQUIRE(c.allocateInfo(pool, p));
		return c;
	}
};
} // namespace

TEST_CASE("pathfind cell: costSoFar follows retail 10 / 14 plus pinched 14 plus turn penalties 4 / 8 / 16")
{
	Fixture f;
	// straight line east: (0,0) <- (1,0) <- (2,0)
	PathfindCell &a = f.at(0, 0);
	PathfindCell &b = f.at(1, 0);
	PathfindCell &c = f.at(2, 0);
	a.setCostSoFar(0);
	b.setParentCell(&a);
	b.setCostSoFar(b.costSoFar(&a));
	CHECK(b.getCostSoFar() == 10); // orthogonal
	c.setParentCell(&b);
	CHECK(c.costSoFar(&b) == 20);  // still straight, no turn

	// a diagonal step costs 14
	PathfindCell &d = f.at(1, 1);
	CHECK(d.costSoFar(&a) == 14);

	// a pinched cell adds 14
	d.setPinched(true);
	CHECK(d.costSoFar(&a) == 28);
	d.setPinched(false);

	// turns: parent chain a(0,0) <- b(1,0); the next cell relative to b decides the turn
	//   (2,0): straight                       -> +0
	//   (2,1): 45 degrees (dot of the two step directions is > 0) -> +4
	//   (1,1): 90 degrees (dot 0)             -> +8
	//   (0,1): 135 degrees (dot < 0)          -> +16
	// cost of the step is 10 or 14 on top of b's 10
	PathfindCell &e45 = f.at(2, 1);
	CHECK(e45.costSoFar(&b) == 10 + 14 + 4);
	PathfindCell &e90 = f.at(1, 1 + 0);
	CHECK(e90.costSoFar(&b) == 10 + 10 + 8);
	PathfindCell &e135 = f.at(0, 1);
	CHECK(e135.costSoFar(&b) == 10 + 14 + 16);
}

TEST_CASE("pathfind cell: costToGoal is 10 * max + 4 * min on cell indices (RW 0x6F1F0D)")
{
	Fixture f;
	PathfindCell &a = f.at(1, 1);
	PathfindCell &goal = f.at(6, 3);
	CHECK(a.costToGoal(&goal) == 10 * 5 + 4 * 2); // dx 5, dy 2
	CHECK(goal.costToGoal(&a) == 58);
	PathfindCell &g2 = f.at(1, 7);
	CHECK(a.costToGoal(&g2) == 10 * 6);
}

TEST_CASE("pathfind open heap: hand-simulated SGI push / pop order, ties go to the right child")
{
	Fixture f;
	PathfindOpenHeap heap;
	PathfindCell &c1 = f.at(0, 0);
	PathfindCell &c2 = f.at(1, 0);
	PathfindCell &c3 = f.at(2, 0);
	PathfindCell &c4 = f.at(3, 0);
	c1.setTotalCost(50);
	c2.setTotalCost(30);
	c3.setTotalCost(50);
	c4.setTotalCost(30);
	heap.push(&c1); // [c1]
	heap.push(&c2); // c1 > c2 moves down: [c2 c1]
	heap.push(&c3); // [c2 c1 c3]
	heap.push(&c4); // sifts above c1 but not above the equal c2: [c2 c4 c3 c1]
	CHECK(c1.getOpen());
	REQUIRE(heap.items().size() == 4);
	CHECK(heap.items()[0] == &c2);
	CHECK(heap.items()[1] == &c4);
	CHECK(heap.items()[2] == &c3);
	CHECK(heap.items()[3] == &c1);
	// pop: c2; the hole takes the smaller child c4 (the right child c3 is larger), c1 settles below: [c4 c1 c3]
	CHECK(heap.pop() == &c2);
	CHECK(!c2.getOpen());
	CHECK(heap.items()[0] == &c4);
	CHECK(heap.items()[1] == &c1);
	CHECK(heap.items()[2] == &c3);
	CHECK(heap.pop() == &c4);
	CHECK(heap.pop() == &c1);
	CHECK(heap.pop() == &c3);
	CHECK(heap.empty());
}

TEST_CASE("pathfind cell: occupant lists insert at the head, the info record is given back with the last node")
{
	Fixture f;
	ICoord2D p;
	p.x = 3;
	p.y = 3;
	PathfindCell &c = f.cells[3 * (size_t)f.w + 3];
	CHECK(!c.hasAnyOccupant());
	c.addOccupant(f.pool, OCC_GROUND_GOAL, 7, p);
	c.addOccupant(f.pool, OCC_GROUND_GOAL, 8, p);
	c.addOccupant(f.pool, OCC_POSITION, 9, p);
	REQUIRE(c.occupants(OCC_GROUND_GOAL) != nullptr);
	CHECK(c.occupants(OCC_GROUND_GOAL)->owner == 8); // head insertion
	CHECK(c.occupants(OCC_GROUND_GOAL)->next->owner == 7);
	CHECK(c.isOccupiedBy(OCC_POSITION, 9));
	CHECK(!c.isOccupiedBy(OCC_POSITION, 7));
	CHECK(c.removeOccupant(f.pool, OCC_GROUND_GOAL, 7));
	CHECK(!c.removeOccupant(f.pool, OCC_GROUND_GOAL, 7));
	CHECK(c.hasAnyOccupant());
	CHECK(c.removeOccupant(f.pool, OCC_GROUND_GOAL, 8));
	CHECK(c.removeOccupant(f.pool, OCC_POSITION, 9));
	CHECK(!c.hasInfo());
	CHECK(f.pool.freeCount() == 64);
}

TEST_CASE("pathfind cell: the cost fields are 16 bit like the retail struct")
{
	Fixture f;
	PathfindCell &c = f.at(0, 0);
	c.setCostSoFar(70000); // 65536 + 4464
	CHECK(c.getCostSoFar() == 4464);
	c.setTotalCost(65535);
	CHECK(c.getTotalCost() == 65535);
}

TEST_CASE("pathfind cell: info pool hands out and takes back records, reports exhaustion")
{
	PathfindCellInfoPool pool;
	pool.allocate(2);
	std::vector<PathfindCell> cells(3);
	ICoord2D p;
	CHECK(cells[0].allocateInfo(pool, p));
	CHECK(cells[1].allocateInfo(pool, p));
	CHECK(!pool.exhaustedOnce());
	CHECK(!cells[2].allocateInfo(pool, p));
	CHECK(pool.exhaustedOnce());
	cells[0].releaseInfo(pool);
	CHECK(cells[2].allocateInfo(pool, p));
}
