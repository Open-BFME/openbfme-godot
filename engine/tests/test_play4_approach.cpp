// OpenBFME unit tests (lane PLAY-4, review r1): RW 0x6FB754 Pathfinder::CanApproachToTarget and its line callback RW 0x6F61BD on a synthetic map (no retail data).
// The target stands in a pocket the unit cannot path into; the pocket is closed by a cliff ring, a structure ring (obstacle cells, type 4) or a gate ring
// (BLOCKING_GATE: obstacle cells with bit 17). Each of the 8 lines starts in the pocket: its first usable cell fails the path test. What follows decides:
//   * a MeleeWeapon (record + 0x1D) ends the line at that first miss -> false;
//   * another weapon goes on past a cliff (not a usable cell) or a structure (type 4, skipped) to the reachable cells outside -> true;
//   * after a miss, a bit 17 cell ends the line for a weapon that is not melee (step 1 of the callback) -> false through a gate ring;
//   * the caller's flag (+ 0x20) ends the line at the first miss -> false.
#include "doctest.h"

#include "PathfindTestUtil.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace pathtest;

namespace
{
enum class Ring
{
	None,
	Cliff,
	Structure,
	Gate
};

// a 40 x 40 cell map; the unit at (55, 205), the target at (255, 205); the ring 60 around the target (cells 6 away), the lines run 150 (15 cells)
struct Pocket
{
	SyntheticTerrain terrain{ 40, 40 };
	TestWorld world;
	TestObject unit;
	std::vector<std::unique_ptr<TestObject>> walls;
	std::unique_ptr<Pathfinder> pf;
	Coord3D target{ 255.0f, 205.0f, 0.0f };

	explicit Pocket(Ring ring)
	{
		unit.id = 3;
		unit.pos = Coord3D{ 55.0f, 205.0f, 0.0f };
		unit.mobile = true;
		world.objects[unit.id] = &unit;
		const int tx = 25, ty = 20;
		if (ring == Ring::Cliff)
		{
			for (int dx = -6; dx <= 6; ++dx)
			{
				for (int dy = -6; dy <= 6; ++dy)
				{
					if (std::max(std::abs(dx), std::abs(dy)) == 6)
					{
						terrain.cliff.insert({ tx + dx, ty + dy });
					}
				}
			}
		}
		std::vector<PathfindObject *> objects;
		if (ring == Ring::Structure || ring == Ring::Gate)
		{
			// four boxes 10 thick, their half-lengths 70 so the corners overlap: sides at +- 60 from the target
			const float sides[4][3] = { { 0.0f, 60.0f, 0.0f }, { 0.0f, -60.0f, 0.0f }, { 60.0f, 0.0f, 1.5707964f }, { -60.0f, 0.0f, 1.5707964f } };
			PathfindObjectID id = 100;
			for (const auto &s : sides)
			{
				auto o = std::make_unique<TestObject>();
				o->id = id++;
				o->pos = Coord3D{ target.x + s[0], target.y + s[1], 0.0f };
				o->angle = s[2];
				o->structure = true;
				o->geometry.type = PATHFIND_GEOMETRY_BOX;
				o->geometry.majorRadius = 70.0f;
				o->geometry.minorRadius = 5.0f;
				if (ring == Ring::Gate)
				{
					o->kinds.insert(PK_BLOCKING_GATE);
				}
				world.objects[o->id] = o.get();
				objects.push_back(o.get());
				walls.push_back(std::move(o));
			}
		}
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain, &objects);
	}

	bool approach(bool melee, bool stopAtFirstMiss = false) { return pf->canApproachToTarget(unit, groundLoco(), target, 20.0f, melee, stopAtFirstMiss); }
	bool reachable(float x, float y)
	{
		const Coord3D to{ x, y, 0.0f };
		return pf->clientSafeQuickDoesPathExist(groundLoco(), &unit.pos, &to);
	}
	const PathfindCell *cell(int x, int y) const { return pf->cellAt(x, y); }
};
} // namespace

TEST_CASE("play4 approach: the pockets are what the cases need (the target unreachable inside, the cells outside reachable, the gate ring's cells bit 17)")
{
	Pocket open(Ring::None);
	CHECK(open.reachable(255.0f, 205.0f));
	for (Ring r : { Ring::Cliff, Ring::Structure, Ring::Gate })
	{
		Pocket p(r);
		CHECK_FALSE(p.reachable(255.0f, 205.0f));
		CHECK(p.reachable(255.0f + 120.0f, 205.0f));
		CHECK(p.reachable(255.0f + 120.0f, 205.0f + 120.0f));
	}
	Pocket gate(Ring::Gate), wall(Ring::Structure);
	CHECK(gate.cell(31, 20)->getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(gate.cell(31, 20)->getBit17());
	CHECK(wall.cell(31, 20)->getType() == PathfindCell::CELL_OBSTACLE);
	CHECK_FALSE(wall.cell(31, 20)->getBit17());
}

TEST_CASE("play4 approach: an open target area answers true for any weapon")
{
	Pocket p(Ring::None);
	CHECK(p.approach(true));
	CHECK(p.approach(false));
	CHECK(p.approach(false, true));
}

TEST_CASE("play4 approach: a MeleeWeapon ends each line at its first miss (+ 0x1D), another weapon looks past a cliff ring")
{
	Pocket p(Ring::Cliff);
	CHECK_FALSE(p.approach(true));
	CHECK(p.approach(false));
}

TEST_CASE("play4 approach: obstacle cells (type 4) are skipped: a weapon that is not melee reaches the cells beyond a structure ring")
{
	Pocket p(Ring::Structure);
	CHECK_FALSE(p.approach(true));
	CHECK(p.approach(false));
}

TEST_CASE("play4 approach: after a miss a gate cell (bit 17) ends the line for a weapon that is not melee (RW 0x6F61D3)")
{
	Pocket p(Ring::Gate);
	CHECK_FALSE(p.approach(false));
	CHECK_FALSE(p.approach(true));
}

TEST_CASE("play4 approach: the caller's flag (+ 0x20) ends each line at its first miss")
{
	Pocket p(Ring::Cliff);
	CHECK_FALSE(p.approach(false, true));
}
