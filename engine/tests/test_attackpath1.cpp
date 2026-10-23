// OpenBFME unit tests (lane ATTACKPATH-1): the attack view test (RW 0x6F4557 with its cell callback RW 0x6F2D31) and findAttackPath's retail body (RW 0x6FC18E:
// the view test in the probe, the own hierarchical corridor, no fallback without a corridor). Synthetic maps, no retail data. The expected answers follow the
// rules read from the RotWK disassembly (AIPathfindAttackView.cpp / AIPathfindAttack.cpp file comments), not the port's output.
#include "doctest.h"

#include "PathfindTestUtil.h"

#include "Common/StateHash.h"

using namespace pathtest;

namespace
{
// a weapon test answering a fixed reach around `centre` (extra enters squared like RW 0x6CC07C), or never
class FixedRange : public PathfindAttackRange
{
public:
	FixedRange(Coord3D centre, float reach, float distance = 0.0f) : m_centre(centre), m_reach(reach), m_distance(distance) {}
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
		(void)victimPos;
		return inRangeFrom(from, extra);
	}
	float attackDistance() const override { return m_distance; }

private:
	Coord3D m_centre;
	float m_reach;
	float m_distance;
};

TestObject *structureAt(TestWorld &w, PathfindObjectID id, float x, float y, float major, float minor, PathfindKind kind = PK_COUNT)
{
	TestObject *o = new TestObject();
	o->id = id;
	o->pos = Coord3D{ x, y, 0.0f };
	o->structure = true;
	o->team = 2;
	o->geometry.type = PATHFIND_GEOMETRY_BOX;
	o->geometry.majorRadius = major;
	o->geometry.minorRadius = minor;
	if (kind != PK_COUNT)
	{
		o->kinds.insert(kind);
	}
	w.objects[id] = o;
	return o;
}

struct ViewScene
{
	SyntheticTerrain terrain{ 40, 30 };
	TestWorld world;
	TestObject attacker, victim;
	std::vector<std::unique_ptr<TestObject>> owned;
	std::unique_ptr<Pathfinder> pf;

	explicit ViewScene(bool attackUsesLineOfSight)
	{
		attacker.id = 3;
		attacker.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
		attacker.mobile = true;
		attacker.kinds.insert(PK_ATTACK_NEEDS_LINE_OF_SIGHT);
		world.objects[attacker.id] = &attacker;
		victim.id = 5;
		victim.pos = Coord3D{ 255.0f, 155.0f, 0.0f };
		victim.mobile = true;
		victim.team = 1;
		world.objects[victim.id] = &victim;
		PathfindConfig cfg = testConfig();
		cfg.attackUsesLineOfSight = attackUsesLineOfSight; // retail default/aidata.ini: AttackUsesLineOfSight = Yes
		pf = std::make_unique<Pathfinder>(cfg, &world);
		pf->newMap(terrain);
	}
	TestObject &add(PathfindObjectID id, float x, float y, float major, float minor, PathfindKind kind = PK_COUNT)
	{
		owned.emplace_back(structureAt(world, id, x, y, major, minor, kind));
		pf->addObjectToPathfindMap(*owned.back());
		return *owned.back();
	}
	bool blocked() { return pf->isAttackViewBlockedByObstacle(attacker, attacker.pos, &victim, victim.pos); }
};
} // namespace

// RW 0x6F4557: the gates (AIData + 0x67, a victim, KindOf 59 / 214), then RW 0x6F2D31's ladder for a plain building (no wall KindOf) halfway between the two
TEST_CASE("attackpath1 view: a building between them blocks an ATTACK_NEEDS_LINE_OF_SIGHT attacker; the gates and the over-walls rule do not")
{
	ViewScene s(true);
	s.add(7, 155.0f, 155.0f, 15.0f, 15.0f);
	CHECK(s.blocked());
	// no victim object: never blocked (RW 0x6F4577)
	CHECK_FALSE(s.pf->isAttackViewBlockedByObstacle(s.attacker, s.attacker.pos, nullptr, s.victim.pos));
	// an attacker with neither KindOf: never tested (RW 0x6F4588 .. 0x6F4598)
	s.attacker.kinds.clear();
	CHECK_FALSE(s.blocked());
	// CAN_SHOOT_OVER_WALLS alone: a non-wall obstacle blocks only an attacker that is not CAN_SHOOT_OVER_WALLS or is ATTACK_NEEDS_LINE_OF_SIGHT (RW 0x6F2E96)
	s.attacker.kinds.insert(PK_CAN_SHOOT_OVER_WALLS);
	CHECK_FALSE(s.blocked());
	s.attacker.kinds.insert(PK_ATTACK_NEEDS_LINE_OF_SIGHT);
	CHECK(s.blocked());
	// AttackUsesLineOfSight = No: never blocked (RW 0x6F4565)
	ViewScene off(false);
	off.add(7, 155.0f, 155.0f, 15.0f, 15.0f);
	CHECK_FALSE(off.blocked());
}

// RW 0x6F2D31's exemptions: the victim's own footprint, the attacker's own footprint, and the obstacle of the victim's cell
TEST_CASE("attackpath1 view: the victim's footprint, the attacker's footprint and a clear line do not block")
{
	ViewScene s(true);
	CHECK_FALSE(s.blocked()); // nothing on the line
	// the victim is the building: its cells are exempt (RW 0x6F2D94)
	TestObject &b = s.add(7, 255.0f, 155.0f, 15.0f, 15.0f);
	CHECK_FALSE(s.pf->isAttackViewBlockedByObstacle(s.attacker, s.attacker.pos, &b, b.pos));
	// a building under the attacker that is the attacker itself (a tower shooting out of its own footprint, RW 0x6F2D7D)
	TestObject &tower = s.add(9, 55.0f, 255.0f, 15.0f, 15.0f);
	tower.kinds.insert(PK_ATTACK_NEEDS_LINE_OF_SIGHT);
	const Coord3D target{ 255.0f, 255.0f, 0.0f };
	CHECK_FALSE(s.pf->isAttackViewBlockedByObstacle(tower, tower.pos, &s.victim, target));
	// another building under the attacker blocks it
	CHECK(s.pf->isAttackViewBlockedByObstacle(s.attacker, tower.pos, &s.victim, target));
}

// RW 0x6F2EB0 .. 0x6F2F43: a wall (WALL_SEGMENT here) blocks a ground CAN_SHOOT_OVER_WALLS attacker only when the wall cell's corner is not farther from the victim
// than from the attacker; an attacker without CAN_SHOOT_OVER_WALLS is blocked by any wall
TEST_CASE("attackpath1 view: CAN_SHOOT_OVER_WALLS shoots over a wall near itself, not over a wall near its victim")
{
	{
		ViewScene s(true);
		s.attacker.kinds = { PK_CAN_SHOOT_OVER_WALLS };
		s.add(7, 85.0f, 155.0f, 5.0f, 40.0f, PK_WALL_SEGMENT); // cells x = 8: |55 - 80|^2 < |255 - 80|^2
		CHECK_FALSE(s.blocked());
		s.attacker.kinds = { PK_ATTACK_NEEDS_LINE_OF_SIGHT };
		CHECK(s.blocked());
	}
	{
		ViewScene s(true);
		s.attacker.kinds = { PK_CAN_SHOOT_OVER_WALLS };
		s.add(7, 225.0f, 155.0f, 5.0f, 40.0f, PK_WALL_SEGMENT); // cells x = 22: |255 - 220|^2 <= |55 - 220|^2
		CHECK(s.blocked());
	}
}

// RW 0x6FC2C0 .. 0x6FC3D2: the probe's point must also pass the view test. The reach (160 around the victim) already holds 5 ahead of the archer; a building on the
// line makes the probe fail at every one of its nine points and the search must end where the view is clear
TEST_CASE("attackpath1 findAttackPath: the forward probe and the endpoints obey the view test")
{
	for (bool los : { false, true })
	{
		ViewScene s(los);
		s.victim.pos = Coord3D{ 205.0f, 155.0f, 0.0f };
		s.add(7, 155.0f, 155.0f, 15.0f, 15.0f);
		bool fallback = true;
		Path *p = s.pf->findAttackPath(&s.attacker, groundLoco(), &s.attacker.pos, &s.victim, &s.victim.pos, 0.0f, FixedRange(s.victim.pos, 160.0f, 160.0f), false,
			&fallback);
		REQUIRE(p != nullptr);
		CHECK_FALSE(fallback);
		const Coord3D end = *p->getLastNode()->getPosition();
		if (!los)
		{
			// the first probe point: unit + 1 * (10, 0) * 0.5
			CHECK(end.x == 60.0f);
			CHECK(end.y == 155.0f);
		}
		else
		{
			CHECK_FALSE(s.pf->isAttackViewBlockedByObstacle(s.attacker, end, &s.victim, s.victim.pos));
			CHECK_FALSE((end.x == 60.0f && end.y == 155.0f)); // not the probe's point
		}
		delete p;
	}
}

// RW 0x6FC4B1 .. 0x6FC4F9: findAttackPath builds its own corridor (RW 0x6F9829) from the unit toward the goal; RW 0x93B3D8 numbers the chain's cells from the
// start (0) on: the start, one cell across each of the three block edges (RW 0x6F6454) and the goal, so the last index is 4 and the goal's block holds it
TEST_CASE("attackpath1 findAttackPath: the search runs inside its own hierarchical corridor")
{
	SyntheticTerrain terrain(64, 16);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 85.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	const Coord3D target{ 605.0f, 85.0f, 0.0f };
	bool fallback = false;
	Path *p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &target, 0.0f, FixedRange(target, 30.0f, 30.0f), false, &fallback);
	REQUIRE(p != nullptr);
	CHECK_FALSE(fallback);
	CHECK(pf.hierarchicalLastIndex() == 4);
	CHECK(pf.hierarchicalPositionOf(5, 8) == 0);
	CHECK(pf.hierarchicalPositionOf(60, 8) == 4);
	delete p;
}

// RW 0x6FC757 / 0x6FC977 .. 0x6FC9A6: the fallback cell is only kept while a corridor exists; a unit walled in by cliffs gets no corridor (RW 0x6F8601 finds no
// block edge in its zone), so a target it can never reach gives no path at all (the port before lane ATTACKPATH-1 returned the closest cell)
TEST_CASE("attackpath1 findAttackPath: no corridor, no endpoint: no path and no fallback")
{
	SyntheticTerrain terrain(32, 32);
	for (int i = 2; i <= 8; ++i)
	{
		for (int j : { 2, 8 })
		{
			terrain.cliff.insert({ i, j });
			terrain.cliff.insert({ j, i });
		}
	}
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 55.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	const Coord3D target{ 255.0f, 255.0f, 0.0f };
	const int before = pf.pool().freeCount();
	bool fallback = true;
	Path *p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &target, 0.0f, FixedRange(target, 0.0f), false, &fallback);
	CHECK(p == nullptr);
	CHECK_FALSE(fallback);
	CHECK(pf.hierarchicalLastIndex() == -1);
	CHECK(pf.pool().freeCount() == before);
	// the same unit with the target inside its walled area: an endpoint is found
	const Coord3D near{ 65.0f, 65.0f, 0.0f };
	p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &near, 0.0f, FixedRange(near, 5.0f), true, &fallback);
	CHECK(p != nullptr);
	delete p;
}

// RW 0x746783: from the goal toward the unit in steps of 10, |goal - unit| / 20 - 1 points (RW 0xC29938: -0.05, _ftol), the first whose one-point line
// RW 0x6F2FFF passes; none -> adjustToPossibleDestination and false. A building covering cells x 23 .. 27: the goal (255, 155) and (245 / 235, 155) are in it,
// (225, 155) (cell 22) is the first free point
TEST_CASE("attackpath1 horde melee: the approach goal walks back toward the unit to the first point its footprint fits")
{
	ViewScene s(true);
	s.add(7, 255.0f, 155.0f, 20.0f, 20.0f);
	Coord3D goal{ 255.0f, 155.0f, 0.0f };
	CHECK(s.pf->hordeMeleeStepBack(s.attacker, groundLoco(), &goal));
	CHECK(goal.x == 225.0f);
	CHECK(goal.y == 155.0f);
	// 40 from the unit: one point (the goal itself) is tried
	s.attacker.pos = Coord3D{ 215.0f, 155.0f, 0.0f };
	Coord3D near{ 255.0f, 155.0f, 0.0f };
	CHECK_FALSE(s.pf->hordeMeleeStepBack(s.attacker, groundLoco(), &near));
}

// RW 0x6FB67A with the ring RW 0x6FA733: a free destination cell is kept (its centre); a destination inside a building moves to the first ring that holds a free
// cell, and inside that ring to the nearest one. The building's footprint covers cells x 23 .. 27, y 14 .. 17 (checked below; the low row 13 stays free), so
// ring 1 and ring 3 around the centre cell (25, 15) are blocked except ring 3's last cell (-2, -2) = (23, 13): it wins with distance 8 although ring 5 holds
// (25, 13) at distance 4 (a hit ends the search with its ring)
TEST_CASE("attackpath1 horde melee: AdjustHordeMeleeDestination keeps a free cell and takes the first ring with a free cell")
{
	ViewScene s(true);
	Coord3D free{ 153.0f, 151.0f, 0.0f };
	CHECK(s.pf->adjustHordeMeleeDestination(s.attacker, groundLoco(), &free));
	CHECK(free.x == 155.0f);
	CHECK(free.y == 155.0f);
	s.add(7, 255.0f, 155.0f, 20.0f, 20.0f);
	for (int x = 23; x <= 27; ++x)
	{
		for (int y = 14; y <= 17; ++y)
		{
			CAPTURE(x);
			CAPTURE(y);
			REQUIRE(s.pf->getCell(LAYER_GROUND, x, y)->getType() == PathfindCell::CELL_OBSTACLE);
		}
	}
	REQUIRE(s.pf->getCell(LAYER_GROUND, 23, 13)->getType() != PathfindCell::CELL_OBSTACLE);
	Coord3D inside{ 255.0f, 155.0f, 0.0f };
	CHECK(s.pf->adjustHordeMeleeDestination(s.attacker, groundLoco(), &inside));
	CHECK(inside.x == 235.0f);
	CHECK(inside.y == 135.0f);
}

// the AttackUsesLineOfSight flag is part of the pathfinder's state hash (lane ATTACKPATH-1)
TEST_CASE("attackpath1: AttackUsesLineOfSight enters the pathfinder's hash")
{
	TestWorld world;
	PathfindConfig a = testConfig(), b = testConfig();
	b.attackUsesLineOfSight = !a.attackUsesLineOfSight;
	Pathfinder pa(a, &world), pb(b, &world);
	StateHasher ha, hb;
	pa.crc(ha);
	pb.crc(hb);
	CHECK(ha.value() != hb.value());
}
