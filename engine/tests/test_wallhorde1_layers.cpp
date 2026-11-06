// OpenBFME tests of lane WALLHORDE-1: a horde's member slots on walls follow RotWK's slot test (RW 0x6EA5B2: the layer rule, and the line to the horde's
// cell RW 0x6E95B2 on every ground-grid layer, RW 0x76550F; BFME2 0x6E916A / 0x6E8145). Synthetic walls (the meshes as quads). GPL-3.0.

#include "doctest.h"

#include "MoveTestUtil.h"
#include "PathfindTestUtil.h"

#include <memory>
#include <vector>

using namespace pathtest;
using namespace movetest;

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

// 40 x 30 cells at z 0. Piece A: a box footprint 100 x 60 around (200, 100), its top x 150 .. 250, y 90 .. 110 at z 60, a ramp x 100 .. 150 rising to it.
// Piece B (optional): the same at (200, 200), its top y 190 .. 210 at z 60, no ramp; the two footprints' obstacle rows lie between the tops
struct Walls
{
	SyntheticTerrain terrain{ 40, 30 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	std::vector<std::unique_ptr<TestObject>> owned;
	explicit Walls(bool second)
	{
		add(50, 100.0f, true);
		if (second)
		{
			add(51, 200.0f, false);
		}
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		for (auto &o : owned)
		{
			pf->addObjectToPathfindMap(*o);
		}
	}
	void add(PathfindObjectID id, float y, bool ramp)
	{
		auto p = std::make_unique<WallPieceObject>();
		p->id = id;
		p->pos = { 200.0f, y, 0.0f };
		p->structure = true;
		p->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
		p->geometry.type = PATHFIND_GEOMETRY_BOX;
		p->geometry.majorRadius = 50.0f;
		p->geometry.minorRadius = 30.0f;
		p->walls.walls.push_back(quad(150.0f, y - 10.0f, 250.0f, y + 10.0f, 60.0f, 60.0f));
		if (ramp)
		{
			p->walls.ramps.push_back(quad(100.0f, y - 10.0f, 150.0f, y + 10.0f, 0.0f, 60.0f));
		}
		world.objects[id] = p.get();
		owned.push_back(std::move(p));
	}
	LayeredUnit &unit(PathfindObjectID id, float x, float y, float z, PathfindLayerEnum layer, bool horde)
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
	const PathfindCell &cell(int x, int y) const { return *pf->cellAt(x, y); }
};

// the slot fits (adjustHordeMemberDestination keeps it) or it is moved toward the horde
bool keeps(Walls &w, LayeredUnit &member, LayeredUnit &horde, Coord3D slot)
{
	const Coord3D before = slot;
	const bool ok = w.pf->adjustHordeMemberDestination(member, groundLoco(), horde, &slot);
	return ok && slot.x == before.x && slot.y == before.y;
}
} // namespace

TEST_CASE("wallhorde1: RW 0x6EA5B2's layer rule: a horde on the ramp takes a ground slot, a ground horde a ramp cell slot; a ground horde no wall-top slot")
{
	Walls w(false);
	REQUIRE(w.cell(12, 10).getLayer() == LAYER_RAMP);
	REQUIRE(w.cell(8, 10).getLayer() == LAYER_GROUND);
	REQUIRE(isWallLayer(w.cell(20, 10).getLayer()));
	LayeredUnit &member = w.unit(1, 85.0f, 100.0f, 0.0f, LAYER_GROUND, false);
	// a horde on the ramp: any cell passes (main refused the ground cell: allowsLayer)
	LayeredUnit &onRamp = w.unit(2, 125.0f, 100.0f, 30.0f, LAYER_RAMP, true);
	CHECK(keeps(w, member, onRamp, Coord3D{ 85.0f, 100.0f, 0.0f }));
	// a horde on the ground: a ramp cell passes (main refused it)
	LayeredUnit &onGround = w.unit(3, 75.0f, 100.0f, 0.0f, LAYER_GROUND, true);
	CHECK(keeps(w, member, onGround, Coord3D{ 115.0f, 100.0f, 0.0f }));
	CHECK(keeps(w, member, onGround, Coord3D{ 85.0f, 140.0f, 0.0f })); // a ground cell
	// but no wall-top cell: the ground horde needs its own layer there
	LayeredUnit &farGround = w.unit(4, 200.0f, 160.0f, 0.0f, LAYER_GROUND, true);
	CHECK_FALSE(keeps(w, member, farGround, Coord3D{ 205.0f, 100.0f, 60.0f }));
}

TEST_CASE("wallhorde1: a wall-top horde's slot on another wall top of its layer across the footprints is refused: the line test runs on wall layers (RW 0x76550F)")
{
	Walls w(true);
	REQUIRE(isWallLayer(w.cell(20, 10).getLayer()));
	REQUIRE(w.cell(20, 20).getLayer() == w.cell(20, 10).getLayer()); // the same height: the same layer
	REQUIRE(w.cell(20, 12).getType() == PathfindCell::CELL_OBSTACLE); // piece A's footprint edge, between the tops
	const PathfindLayerEnum top = w.cell(20, 10).getLayer();
	LayeredUnit &member = w.unit(1, 205.0f, 100.0f, 60.0f, top, false);
	LayeredUnit &horde = w.unit(2, 205.0f, 100.0f, 60.0f, top, true);
	CHECK(keeps(w, member, horde, Coord3D{ 225.0f, 100.0f, 60.0f })); // on its own top
	CHECK_FALSE(keeps(w, member, horde, Coord3D{ 205.0f, 200.0f, 60.0f })); // main kept it: no line test off the ground
}

TEST_CASE("wallhorde1: a parked horde takes RW 0x6EE600's layer at its place and its members theirs (RW 0x872EFC); the recentre takes the winner's layer (RW 0x86F18F)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	mw.frames(5);
	AIUpdateInterface *ai = mw.aiOf(h);
	REQUIRE(ai);
	REQUIRE_FALSE(ai->isMoving());
	const std::vector<Object *> ms = mw.membersOf(h);
	REQUIRE(ms.size() == 15);
	// a wrong layer on the parked horde and on one member: HordeContain::update's sync puts both on the layer at their place (the ground here). Called
	// directly: in a frame the horde's own mover (AIMover::refreshLayer, port-only, S-1750) corrects the horde first, and RotWK's sync then leaves the
	// members alone (it runs only when the horde's layer changes)
	ai->setLayer(LAYER_WALL_FIRST);
	mw.aiOf(ms[3])->setLayer(LAYER_WALL_FIRST);
	mw.hordeOf(h)->update();
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
	CHECK(mw.aiOf(ms[3])->adapter().getLayer() == LAYER_GROUND);
	// the recentre (the garrison exit's) gives the horde the winning member's layer (the members on the ramp's layer, so not the ground's default; the
	// validity test reads the horde's layer: the ground's, which a ground cell passes also with #220's wall-top layer test, RW 0x6EA52E .. 0x6EA547)
	ai->setLayer(LAYER_GROUND);
	for (Object *m : ms)
	{
		mw.aiOf(m)->setLayer(LAYER_RAMP);
	}
	mw.hordeOf(h)->recentreOnMembers(h);
	CHECK(ai->adapter().getLayer() == LAYER_RAMP);
}

TEST_CASE("wallhorde1: objectLayerAt is RW 0x6EE600: the ground cell's own layer at any height, the ramp's, and the point moved by 5 for an even footprint")
{
	Walls w(false);
	LayeredUnit &odd = w.unit(1, 0.0f, 0.0f, 0.0f, LAYER_GROUND, false); // radius 5: an odd footprint
	const PathfindLayerEnum top = w.cell(20, 10).getLayer();
	REQUIRE(isWallLayer(top));
	CHECK(w.pf->objectLayerAt(odd, Coord3D{ 205.0f, 105.0f, 0.0f }) == top); // no height test on a wall top's cell
	CHECK(w.pf->objectLayerAt(odd, Coord3D{ 205.0f, 105.0f, 60.0f }) == top);
	CHECK(w.pf->objectLayerAt(odd, Coord3D{ 125.0f, 105.0f, 30.0f }) == LAYER_RAMP);
	CHECK(w.pf->objectLayerAt(odd, Coord3D{ 145.0f, 105.0f, 50.0f }) == LAYER_RAMP); // cell 14: the ramp
	CHECK(w.pf->objectLayerAt(odd, Coord3D{ 75.0f, 105.0f, 0.0f }) == LAYER_GROUND);
	LayeredUnit &even = w.unit(2, 0.0f, 0.0f, 0.0f, LAYER_GROUND, false);
	even.diameter = 20.0f; // a footprint 2 cells wide (footprintSize, RW 0x6EAF79): the point moves by 5 (RW 0x6EE62D .. 0x6EE64C)
	CHECK(w.pf->objectLayerAt(odd, Coord3D{ 145.0f, 95.0f, 50.0f }) == LAYER_RAMP); // cell (14, 9): the ramp
	CHECK(w.pf->objectLayerAt(even, Coord3D{ 145.0f, 95.0f, 50.0f }) == top);      // moved to cell (15, 10): the wall top
}

TEST_CASE("wallhorde1: a garrisoned horde gets no layer sync: RW 0x872EFC returns on the contained flag (+0x294, set by RW 0x872ADF) before the parked branch")
{
	// BFME2 0x86FC1B the same (decomp HordeContainRva0046FC1B.cpp:121-124: the + 0x294 return comes before the layer sync)
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	mw.frames(5);
	AIUpdateInterface *ai = mw.aiOf(h);
	REQUIRE(ai);
	REQUIRE_FALSE(ai->isMoving());
	const std::vector<Object *> ms = mw.membersOf(h);
	REQUIRE(ms.size() == 15);
	mw.hordeOf(h)->setGarrisoned(true);
	ai->setLayer(LAYER_WALL_FIRST);
	mw.aiOf(ms[3])->setLayer(LAYER_WALL_FIRST);
	mw.hordeOf(h)->update();
	CHECK(ai->adapter().getLayer() == LAYER_WALL_FIRST);
	CHECK(mw.aiOf(ms[3])->adapter().getLayer() == LAYER_WALL_FIRST);
	mw.hordeOf(h)->setGarrisoned(false);
}
