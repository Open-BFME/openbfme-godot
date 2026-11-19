// OpenBFME tests of lane WALLMELEE-1: melee on walls. The melee destinations take the destination's layer for the mover and that layer's height
// (AdjustHordeMeleeDestination RW 0x6FB67A, adjustToMeleeDestination RW 0x6EEBC1; BFME2 0x6FA202 / 0x6ED7F7); the reach test RW 0x6FF7FA's melee pass
// needs one layer (BFME2 0x6FE193), and the weapon range test RW 0x6CC07C keeps a ground source off a wall-top victim (BFME2 0x6CB35C). Synthetic walls
// (the meshes as quads) and a logic world. GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/AIUpdate.h"

#include <memory>
#include <vector>

using namespace pathtest;

namespace
{
PathfindWallSurface quad(float x0, float y0, float x1, float y1, float z0, float z1)
{
	PathfindWallSurface s;
	PathfindWallTriangle a, b;
	a.v[0] = { x0, y0, z0 };
	a.v[1] = { x1, y0, z1 };
	a.v[2] = { x1, y1, z1 };
	b.v[0] = { x0, y0, z0 };
	b.v[1] = { x1, y1, z1 };
	b.v[2] = { x0, y1, z0 };
	s.triangles = { a, b };
	s.loX = x0;
	s.loY = y0;
	s.hiX = x1;
	s.hiY = y1;
	s.height = z0 > z1 ? z0 : z1;
	return s;
}

class WallPieceObject : public TestObject
{
public:
	PathfindWallGeometry walls;
	const PathfindWallGeometry *getWallGeometry() const override { return walls.empty() ? nullptr : &walls; }
};

class LayeredUnit : public TestObject
{
public:
	PathfindLayerEnum layer = LAYER_GROUND;
	PathfindLayerEnum getLayer() const override { return layer; }
	bool canPathThroughGates() const override { return true; }
};

// 40 x 20 cells at z 0. The wall piece: a box footprint 100 x 60 around (200, 100), its top x 150 .. 250, y 90 .. 110 at z 60, a ramp x 100 .. 150 rising to it
struct Walls
{
	SyntheticTerrain terrain{ 40, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	std::vector<std::unique_ptr<TestObject>> owned;
	Walls()
	{
		auto p = std::make_unique<WallPieceObject>();
		p->id = 50;
		p->pos = { 200.0f, 100.0f, 0.0f };
		p->structure = true;
		p->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
		p->geometry.type = PATHFIND_GEOMETRY_BOX;
		p->geometry.majorRadius = 50.0f;
		p->geometry.minorRadius = 30.0f;
		p->walls.walls.push_back(quad(150.0f, 90.0f, 250.0f, 110.0f, 60.0f, 60.0f));
		p->walls.ramps.push_back(quad(100.0f, 90.0f, 150.0f, 110.0f, 0.0f, 60.0f));
		world.objects[p->id] = p.get();
		WallPieceObject &piece = *p;
		owned.push_back(std::move(p));
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		pf->addObjectToPathfindMap(piece);
		// the zone update every pathfinder tick starts with (RW 0x6F2364)
		struct None : PathfindRequestHandler
		{
			void doPathfind(PathfindObjectID) override {}
		} none;
		pf->processPathfindQueue(none);
	}
	LayeredUnit &unit(PathfindObjectID id, float x, float y, float z, PathfindLayerEnum layer, bool horde = false)
	{
		auto u = std::make_unique<LayeredUnit>();
		u->id = id;
		u->pos = { x, y, z };
		u->mobile = true;
		u->layer = layer;
		u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		u->geometry.majorRadius = 5.0f;
		if (horde)
		{
			u->kinds.insert(PK_HORDE);
		}
		LayeredUnit &r = *u;
		world.objects[id] = u.get();
		owned.push_back(std::move(u));
		return r;
	}
	const PathfindCell &cellAtPoint(const Coord3D &p) const { return *pf->cellAt((int)(p.x / 10.0f), (int)(p.y / 10.0f)); }
};
} // namespace

TEST_CASE("wallmelee1: adjustToMeleeDestination (RW 0x6EEBC1) searches the destination's layer for the unit: a taken wall-top goal moves along the top, at its height")
{
	Walls w;
	const PathfindLayerEnum top = w.pf->cellAt(20, 10)->getLayer();
	REQUIRE(isWallLayer(top));
	LayeredUnit &u = w.unit(1, 205.0f, 105.0f, 60.0f, top);
	// another unit's goal on the cell the melee goal names: the footprint test refuses it (RW 0x6EBE89) and the spiral looks for a closer cell
	LayeredUnit &other = w.unit(2, 225.0f, 105.0f, 60.0f, top);
	Coord3D taken{ 225.0f, 105.0f, 60.0f };
	w.pf->updateGoal(other, &taken, top);
	Coord3D dest = taken;
	REQUIRE(w.pf->getLayerAt(u, dest) == top);
	// main searched the ground layer: every wall-top cell was refused (its layer is not the ground), and the obstacle rows beside the top leave no ground cell
	CHECK(w.pf->adjustToMeleeDestination(u, groundLoco(), &dest));
	INFO("the melee goal at " << dest.x << ", " << dest.y << ", " << dest.z);
	CHECK(w.cellAtPoint(dest).getLayer() == top);
	CHECK(dest.z == 60.0f);
	CHECK(dest.x == 215.0f); // the spiral's first cell that is free and closer to the unit: (21, 10), the next one toward it
	CHECK(dest.y == 105.0f);
}

TEST_CASE("wallmelee1: AdjustHordeMeleeDestination (RW 0x6FB67A) takes the destination's layer and its height: a horde's goal on a wall top stays on the top")
{
	Walls w;
	const PathfindLayerEnum top = w.pf->cellAt(20, 10)->getLayer();
	REQUIRE(isWallLayer(top));
	LayeredUnit &h = w.unit(1, 205.0f, 105.0f, 60.0f, top, true);
	Coord3D dest{ 225.0f, 105.0f, 60.0f };
	REQUIRE(w.pf->getLayerHeight(dest.x, dest.y, top) == 60.0f);
	// main took the terrain's height (0): a wall-top cell at 60 was outside the 50 window (RW 0xBD88C4), so no cell on the top was accepted
	CHECK(w.pf->adjustHordeMeleeDestination(h, groundLoco(), &dest));
	INFO("the horde's melee goal at " << dest.x << ", " << dest.y << ", " << dest.z);
	CHECK(w.cellAtPoint(dest).getLayer() == top);
	CHECK(dest.z == 60.0f);
	CHECK(dest.x == 225.0f); // the destination's own cell, accepted at once
	CHECK(dest.y == 105.0f);
}

TEST_CASE("wallmelee1: RW 0x6FF7FA: in range on any layer but a ground source against a wall top (RW 0x6CC07C); the melee pass by sight on one layer only")
{
	// a swordsman that is also WALL_UPGRADE (template + 0x11A & 0x40): RW 0x6CC07C keeps the reach test's answer for it on any layer
	combattest::CombatWorld cw("ChildObject WallSwordsman Swordsman\n  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE WALL_UPGRADE\nEnd\n");
	// two swordsmen side by side (the edges touch: within the sword's 11.5) and a third 100 away, within the vision range 150
	Object *a = cw.unit("Swordsman", 'A', 500.0f, 500.0f);
	Object *b = cw.unit("Swordsman", 'B', 516.0f, 500.0f);
	Object *c = cw.unit("Swordsman", 'B', 600.0f, 500.0f);
	REQUIRE(a);
	REQUIRE(b);
	REQUIRE(c);
	AIUpdateInterface *aa = a->getAIUpdateInterface(), *ab = b->getAIUpdateInterface(), *ac = c->getAIUpdateInterface();
	REQUIRE(aa);
	REQUIRE(ab);
	REQUIRE(ac);
	// the layers are set by hand and the frames not run (the port's surface loss rule, AIMover::refreshLayer S-1750, would take them back to the ground)
	CHECK(AI::canAttackFromHere(*a, *b));
	CHECK(AI::canAttackFromHere(*a, *c)); // out of range: the melee pass, the same layer and in sight
	// the victim on a wall top: RW 0x6CC07C refuses the ground source in range, and the melee pass needs one layer (main: both answered true)
	ab->setLayer(LAYER_WALL_FIRST);
	ac->setLayer(LAYER_WALL_FIRST);
	CHECK(AIWorld::objectLayer(*b) == LAYER_WALL_FIRST);
	CHECK_FALSE(AI::canAttackFromHere(*a, *b));
	CHECK_FALSE(AI::canAttackFromHere(*a, *c));
	// both on the wall top: in range and by sight again
	aa->setLayer(LAYER_WALL_FIRST);
	CHECK(AI::canAttackFromHere(*a, *b));
	CHECK(AI::canAttackFromHere(*a, *c));
	// the source on the wall top, the victim on the ramp (0x10): RW 0x6CC07C's rule is the ground's only, so in range counts; by sight it needs one layer
	ab->setLayer(LAYER_RAMP);
	ac->setLayer(LAYER_RAMP);
	CHECK(AI::canAttackFromHere(*a, *b));
	CHECK_FALSE(AI::canAttackFromHere(*a, *c));
	// the source on the wall top, the victim on the ground: in range only through the victim's created portal (RW 0x863983 + 0x3C); a swordsman has none
	ab->setLayer(LAYER_GROUND);
	CHECK_FALSE(AI::canAttackFromHere(*a, *b));
	// a WALL_UPGRADE victim on the ground beside the wall-top source: the layer rule is skipped, in range counts
	Object *u = cw.unit("WallSwordsman", 'B', 484.0f, 500.0f);
	REQUIRE(u);
	CHECK(AIWorld::objectLayer(*u) == LAYER_GROUND);
	CHECK(AI::canAttackFromHere(*a, *u));
}
