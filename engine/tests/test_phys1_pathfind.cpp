// OpenBFME unit tests (lane PHYS-1, review r3): the pathfinder configuration the attack path, the allied clearing and the melee approach read enters the state hash, and the
// attack and move-away searches give every cell-info record back to the pool (successful, failed and budget-exhausted searches, repeated). Synthetic map, no retail data.
#include "doctest.h"

#include "PathfindTestUtil.h"

#include "Common/StateHash.h"

using namespace pathtest;

namespace
{
std::uint32_t configHash(const PathfindConfig &c)
{
	TestWorld world;
	Pathfinder pf(c, &world);
	StateHasher h;
	pf.crc(h);
	return h.value();
}

// a weapon test answering a fixed reach: `reach` around `centre` (extra enters squared like RW 0x6CC07C), or never
class FixedRange : public PathfindAttackRange
{
public:
	FixedRange(Coord3D centre, float reach) : m_centre(centre), m_reach(reach) {}
	bool inRangeFrom(const Coord3D &from, float extra) const override
	{
		if (m_reach <= 0.0f)
		{
			return false;
		}
		const float dx = from.x - m_centre.x, dy = from.y - m_centre.y;
		return dx * dx + dy * dy + extra * extra <= m_reach * m_reach;
	}
	bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const override
	{
		if (m_reach <= 0.0f)
		{
			return false;
		}
		const float dx = from.x - victimPos.x, dy = from.y - victimPos.y;
		return dx * dx + dy * dy + extra * extra <= m_reach * m_reach;
	}

private:
	Coord3D m_centre;
	float m_reach;
};
} // namespace

TEST_CASE("phys1 pathfind: MaxCellsFindAttackPath, HordesWaitForHordes and MeleeApproachDist are in the pathfinder's hash (review r3)")
{
	const PathfindConfig base = testConfig();
	const std::uint32_t h0 = configHash(base);
	PathfindConfig c = base;
	c.findAttackPathLimit += 1;
	CHECK(configHash(c) != h0);
	c = base;
	c.hordesWaitForHordes = !c.hordesWaitForHordes;
	CHECK(configHash(c) != h0);
	c = base;
	c.meleeApproachDist += 1.0f;
	CHECK(configHash(c) != h0);
	c = base;
	c.adjustToMeleeLimit += 1;
	CHECK(configHash(c) != h0);
	c = base;
	c.findMeleeEngagementLimit += 1;
	CHECK(configHash(c) != h0);
	c = base;
	c.meleeApproachTolerance += 1.0f;
	CHECK(configHash(c) != h0);
	c = base;
	c.castleSiegeStandBackDistance += 1.0f; // round 6: RW 0x7463E8's margin
	CHECK(configHash(c) != h0);
	CHECK(configHash(base) == h0);
}

TEST_CASE("phys1 pathfind: findAttackPath and getMoveAwayFromPath give every cell record back (budget exhausted, failed, successful, repeated)")
{
	SyntheticTerrain terrain(20, 20);
	TestWorld world;
	PathfindConfig cfg = testConfig();
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 55.0f, 0.0f };
	unit.mobile = true; // hasAI
	world.objects[unit.id] = &unit;
	const Coord3D target{ 155.0f, 155.0f, 0.0f };
	auto freeRecords = [](Pathfinder &pf) { return pf.pool().freeCount(); };
	for (int limit : { 1, 2500 })
	{
		cfg.findAttackPathLimit = limit;
		Pathfinder pf(cfg, &world);
		pf.newMap(terrain);
		const int before = freeRecords(pf);
		for (int round = 0; round < 3; ++round)
		{
			bool fallback = false;
			// never in reach: the budget runs out (limit 1) or the whole map is searched; the closest cell is the fallback
			Path *p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &target, 0.0f, FixedRange(target, 0.0f), false, &fallback);
			delete p;
			CHECK(freeRecords(pf) == before);
			// a reach of 30 around the target: a successful endpoint
			p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &target, 0.0f, FixedRange(target, 30.0f), true, &fallback);
			CHECK(p != nullptr);
			delete p;
			CHECK(freeRecords(pf) == before);
			// a successful move-away search
			p = pf.getMoveAwayFromPath(&unit, groundLoco(), nullptr, nullptr, nullptr, nullptr);
			CHECK(p != nullptr);
			delete p;
			CHECK(freeRecords(pf) == before);
		}
	}
}

// RW 0x6EEBC1 (Pathfinder::adjustToMeleeDestination) on an open 30 x 30 map, a one-cell unit at (55, 155) and the destination (205.3, 155.7)
TEST_CASE("phys1 pathfind: the melee destination check keeps a free destination, spirals only to closer cells and refuses enemies' positions and others' goals")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	PathfindConfig cfg = testConfig();
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	TestObject ally;
	ally.id = 4;
	ally.mobile = true;
	ally.team = 0;
	world.objects[ally.id] = &ally;
	TestObject enemy;
	enemy.id = 5;
	enemy.mobile = true;
	enemy.team = 1;
	world.objects[enemy.id] = &enemy;
	Pathfinder pf(cfg, &world);
	pf.newMap(terrain);
	const Coord3D original{ 205.3f, 155.7f, 0.0f };
	auto dist2 = [&](const Coord3D &c) { return (c.x - unit.pos.x) * (c.x - unit.pos.x) + (c.y - unit.pos.y) * (c.y - unit.pos.y); };
	// a free destination in the unit's zone: true, the coordinate unchanged (not snapped to its cell)
	Coord3D d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK(d.x == original.x);
	CHECK(d.y == original.y);
	// an ally's goal on the destination (RW 0x9344DE: no other unit's goal): the spiral's first acceptable cell, strictly closer to the unit
	ally.pos = original;
	pf.updateGoal(ally, &ally.pos, LAYER_GROUND);
	d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK((d.x != original.x || d.y != original.y));
	CHECK(dist2(d) < dist2(original));
	pf.removeGoal(ally);
	// an enemy standing on the destination (its position, RW 0x6EBE89): refused there too
	enemy.pos = original;
	pf.updatePos(enemy);
	d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK(dist2(d) < dist2(original));
	// an ally standing there is no blocker (only enemies' positions are)
	pf.removePos(enemy);
	ally.pos = original;
	pf.updatePos(ally);
	d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK(d.x == original.x);
	pf.removePos(ally);
	// no budget: a blocked destination is refused
	PathfindConfig none = cfg;
	none.adjustToMeleeLimit = 0;
	Pathfinder pf0(none, &world);
	pf0.newMap(terrain);
	pf0.updateGoal(ally, &ally.pos, LAYER_GROUND);
	d = original;
	CHECK_FALSE(pf0.adjustToMeleeDestination(unit, groundLoco(), &d));
}

// review r5: RW 0x6E8DD5 gives x = -1 off the map and RW 0x6EEBC1 refuses before searching; the port's converter clips, so the overflow is honoured
TEST_CASE("phys1 pathfind: the melee destination check refuses a destination off the map on each side, the coordinate unchanged")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 155.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	for (const Coord3D &probe : { Coord3D{ -5.0f, 155.0f, 0.0f }, Coord3D{ 305.0f, 155.0f, 0.0f }, Coord3D{ 155.0f, -5.0f, 0.0f }, Coord3D{ 155.0f, 305.0f, 0.0f } })
	{
		Coord3D d = probe;
		CHECK_FALSE(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
		CHECK(d.x == probe.x);
		CHECK(d.y == probe.y);
	}
	Coord3D inside{ 155.0f, 205.0f, 0.0f };
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &inside));
}

// round 6: RW 0x6F37B3 (Pathfinder::findMeleeEngagementLocation) on an open 30 x 30 map: a one-cell unit at (55, 155) facing +x, a target at (205, 155) with a reach of 15
TEST_CASE("phys1 pathfind: the member engagement destination stays put in reach, else takes the reachable free cell nearest the unit, and refuses with no reach")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	TestObject target;
	target.id = 5;
	target.mobile = true;
	target.team = 1;
	target.pos = Coord3D{ 205.0f, 155.0f, 0.0f };
	world.objects[target.id] = &target;
	TestObject ally;
	ally.id = 4;
	ally.mobile = true;
	world.objects[ally.id] = &ally;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	const FixedRange reach(target.pos, 15.0f);
	auto d2 = [](const Coord3D &a, const Coord3D &b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); };
	// far: the destination is a cell from which the target is in reach, on the unit's side
	Coord3D dest = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &dest));
	CHECK(d2(dest, target.pos) <= 15.0f * 15.0f);
	CHECK(dest.x < target.pos.x);
	const Coord3D first = dest;
	// deterministic: the same answer again
	Coord3D again = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &again));
	CHECK(again.x == first.x);
	CHECK(again.y == first.y);
	// another unit's goal on that cell: the next best cell
	ally.pos = first;
	pf.updateGoal(ally, &ally.pos, LAYER_GROUND);
	Coord3D other = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &other));
	CHECK((other.x != first.x || other.y != first.y));
	CHECK(d2(other, target.pos) <= 15.0f * 15.0f);
	pf.removeGoal(ally);
	// in reach with a free own footprint (RW 0x6EE6DD): the unit's own position
	unit.pos = Coord3D{ 195.0f, 155.0f, 0.0f };
	CHECK(pf.meleeOwnFootprintFree(unit));
	Coord3D stay = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &stay));
	CHECK(stay.x == unit.pos.x);
	CHECK(stay.y == unit.pos.y);
	// another unit's goal under the unit: RW 0x6EE6DD fails and the search runs instead
	ally.pos = unit.pos;
	pf.updateGoal(ally, &ally.pos, LAYER_GROUND);
	CHECK_FALSE(pf.meleeOwnFootprintFree(unit));
	Coord3D moved = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &moved));
	CHECK((moved.x != unit.pos.x || moved.y != unit.pos.y));
	pf.removeGoal(ally);
	// no reach anywhere: false
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	Coord3D none = target.pos;
	CHECK_FALSE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, FixedRange(target.pos, 0.0f), &none));
}

// round 6: RW 0x7463E8 (Pathfinder::meleeBackOff): an open target area with a path keeps the destination
TEST_CASE("phys1 pathfind: the contact back-off keeps a reachable destination in the open")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	Coord3D dest{ 205.0f, 155.0f, 0.0f };
	CHECK(pf.meleeBackOff(unit, groundLoco(), 20.0f, &dest));
	CHECK(dest.x == 205.0f);
	CHECK(dest.y == 155.0f);
}
