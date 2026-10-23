// OpenBFME unit tests, lane WALL-1 (the owner's report: on Helm's Deep units cannot get up onto the walls). GPL-3.0.
//
// The wall tops (layers 0x11 .. 0x40) and ramps (layer 0x10) of a wall piece in the pathfinder's ground grid (AIPathfindWalls.cpp), on a synthetic map whose
// expected values are derived by hand from the rules named there, then on the retail Helm's Deep: its wall sections become walkable surfaces at their meshes'
// heights, and a hero and a horde ordered onto a wall top walk up a ramp onto it and stand at the wall's height. The retail tests SKIP loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"
#include "PathfindTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/DrawableWallMeshes.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/HudObjects.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>

// the wall pieces and ramps of Helm's Deep in this port's classification (printed by the retail test)
#define WALL1_PIECES 35 // lane WALL-2: HelmsDeepGatehouseCenter (a RaisedWallMesh only) is a wall piece too
#define WALL1_RAMPS 26
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace pathtest;

namespace
{
// a quad as two triangles; z0 at x = x0, z1 at x = x1 (a flat top when equal)
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
	bool canPathThroughGates() const override { return true; } // the template default (RW 0x74004B): bit 17 (wall tops, ramps) does not block it
};

// 40 x 20 cells of flat terrain at z 0. The wall piece: a box footprint 100 x 60 around (200, 100), its wall top x 150 .. 250, y 90 .. 110 at z 60, a ramp
// x 100 .. 150, y 90 .. 110 rising from z 0 to z 60 toward the wall
struct WallWorld
{
	SyntheticTerrain terrain{ 40, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	WallPieceObject *piece = nullptr;
	explicit WallWorld(bool withRamp = true)
	{
		piece = new WallPieceObject();
		piece->id = 50;
		piece->pos = { 200.0f, 100.0f, 0.0f };
		piece->structure = true;
		piece->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
		piece->geometry.type = PATHFIND_GEOMETRY_BOX;
		piece->geometry.majorRadius = 50.0f;
		piece->geometry.minorRadius = 30.0f;
		piece->walls.walls.push_back(quad(150.0f, 90.0f, 250.0f, 110.0f, 60.0f, 60.0f));
		if (withRamp)
		{
			piece->walls.ramps.push_back(quad(100.0f, 90.0f, 150.0f, 110.0f, 0.0f, 60.0f));
		}
		world.objects[piece->id] = piece;
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		pf->addObjectToPathfindMap(*piece);
		zones();
	}
	// the zone update every pathfinder tick starts with (RW 0x6F2364)
	void zones()
	{
		struct None : PathfindRequestHandler
		{
			void doPathfind(PathfindObjectID) override {}
		} none;
		pf->processPathfindQueue(none);
	}
	~WallWorld()
	{
		for (auto &kv : world.objects)
		{
			delete kv.second;
		}
	}
	LayeredUnit *unit(PathfindObjectID id, float x, float y, float z, PathfindLayerEnum layer)
	{
		LayeredUnit *u = new LayeredUnit();
		u->id = id;
		u->pos = { x, y, z };
		u->mobile = true;
		u->layer = layer;
		u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		u->geometry.majorRadius = 5.0f;
		world.objects[id] = u;
		return u;
	}
	const PathfindCell &cell(int x, int y) const { return *pf->cellAt(x, y); }
};

std::vector<PathfindLayerEnum> nodeLayers(const Path &p)
{
	std::vector<PathfindLayerEnum> out;
	for (const PathNode *n = p.getFirstNode(); n; n = n->getNext())
	{
		out.push_back(n->getLayer());
	}
	return out;
}
} // namespace

TEST_CASE("wall1 walls: a wall piece's wall top takes layer 0x11 at its height, its ramp layer 0x10; the ramp's foot joins the ground, its top the wall")
{
	WallWorld w;
	REQUIRE(w.pf->wallPieceCount() == 1);
	REQUIRE(w.pf->rampCount() == 1);
	REQUIRE(w.pf->wallHeights().size() == 1);
	CHECK(w.pf->wallHeights()[0] == 60.0f);
	// the wall top: every cell with a corner in x 150 .. 250, y 90 .. 110 (cells 15 .. 25 x 8 .. 11; cell 15 is the ramp's joint), CLEAR, the box's obstacle gone
	for (int x = 16; x <= 24; ++x)
	{
		for (int y = 8; y <= 11; ++y)
		{
			INFO("cell " << x << ", " << y);
			CHECK(w.cell(x, y).getLayer() == LAYER_WALL_FIRST);
			CHECK(w.cell(x, y).getType() == PathfindCell::CELL_CLEAR);
			CHECK(w.cell(x, y).getBit17()); // lane WALL-2: RW 0x935191 sets it on a wall cell
		}
	}
	// the box footprint (y 70 .. 130) outside the wall top's rows 8 .. 11 stays the box's obstacle on the ground
	CHECK(w.cell(20, 7).getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(w.cell(20, 7).getLayer() == LAYER_GROUND);
	CHECK(w.cell(20, 12).getType() == PathfindCell::CELL_OBSTACLE);
	// the joints: cells 14 and 15 have a corner on the top (x 150) and the ramp's height there is within 15 of the wall's (54 at cell 14's centre, 60 at
	// x 150): the wall's layer, connecting to the ramp
	for (int x = 14; x <= 15; ++x)
	{
		INFO("cell " << x);
		CHECK(w.cell(x, 10).getLayer() == LAYER_WALL_FIRST);
		CHECK(w.cell(x, 10).getConnectLayer() == LAYER_RAMP);
	}
	// the ramp: cells 9 .. 13 (cell 9's corners at x 100 are on the ramp's foot edge)
	for (int x = 9; x <= 13; ++x)
	{
		INFO("cell " << x);
		CHECK(w.cell(x, 10).getLayer() == LAYER_RAMP);
	}
	// its foot: cell 9 (corners at x 100: height 0) and cell 10 (centre x 105: 6) are within 15 of the terrain: they connect to the ground; cell 11 (18) does not
	CHECK(w.cell(9, 10).getConnectLayer() == LAYER_GROUND);
	CHECK(w.cell(10, 10).getConnectLayer() == LAYER_GROUND);
	CHECK(w.cell(11, 10).getConnectLayer() == LAYER_INVALID);
	// cell 13 (centre x 135: 42) is 18 below the joint beside it: no connection of its own (the joint connects to the ramp)
	CHECK(w.cell(13, 10).getConnectLayer() == LAYER_INVALID);
	// the ground beside the ramp and the wall stays ground
	CHECK(w.cell(5, 10).getLayer() == LAYER_GROUND);
	CHECK(w.cell(12, 5).getLayer() == LAYER_GROUND);
	// what can step to what (the search's neighbour test, RW 0x9380BC)
	CHECK(PathfindCell::layersCompatible(w.cell(8, 10), w.cell(9, 10)));   // ground -> the ramp's foot
	CHECK_FALSE(PathfindCell::layersCompatible(w.cell(12, 7), w.cell(12, 8))); // ground -> the ramp's side (no: it is up in the air)
	CHECK(PathfindCell::layersCompatible(w.cell(13, 10), w.cell(14, 10))); // the ramp's top -> the joint
	CHECK(PathfindCell::layersCompatible(w.cell(15, 10), w.cell(16, 10))); // the joint -> the wall top
	CHECK_FALSE(PathfindCell::layersCompatible(w.cell(20, 12), w.cell(20, 11))); // the box's edge (obstacle) -> the wall top
	CHECK_FALSE(PathfindCell::layersCompatible(w.cell(26, 10), w.cell(25, 10))); // the ground beyond the wall's end -> the wall top
}

TEST_CASE("wall1 walls: the layer heights: the wall top's height, the ramp's surface, the terrain elsewhere; the highest layer for a click")
{
	WallWorld w;
	CHECK(w.pf->getLayerHeight(200.0f, 100.0f, LAYER_WALL_FIRST) == 60.0f);
	CHECK(w.pf->getLayerHeight(125.0f, 100.0f, LAYER_RAMP) == 30.0f);
	CHECK(w.pf->getLayerHeight(125.0f, 100.0f, LAYER_GROUND) == 0.0f);
	CHECK(w.pf->getHighestLayerHeight(200.0f, 100.0f) == 60.0f);
	CHECK(w.pf->getHighestLayerHeight(125.0f, 100.0f) == 30.0f);
	CHECK(w.pf->getHighestLayerHeight(50.0f, 50.0f) == 0.0f);
	CHECK(w.pf->getLayerAt(200.0f, 100.0f) == LAYER_WALL_FIRST);
	CHECK(w.pf->getLayerAt(125.0f, 100.0f) == LAYER_RAMP);
	CHECK(w.pf->getLayerAt(50.0f, 50.0f) == LAYER_GROUND);
	CHECK(w.pf->isPointOnWall(Coord3D{ 200.0f, 100.0f, 60.0f }, true));
	CHECK_FALSE(w.pf->isPointOnWall(Coord3D{ 50.0f, 100.0f, 0.0f }));
	float z = 0.0f;
	CHECK(w.pf->isPointOnRamp(140.0f, 95.0f, &z));
	CHECK(z == doctest::Approx(48.0f)); // tolerance: barycentric weights of a float triangle (48 = 60 * 40 / 50 in exact arithmetic)
	CHECK_FALSE(w.pf->isPointOnRamp(160.0f, 95.0f));
}

TEST_CASE("wall1 walls: a ground unit's path to the wall top climbs the ramp (ground, then ramp, then the wall), and back down again; one beside the wall stays on the ground")
{
	WallWorld w;
	LayeredUnit *u = w.unit(1, 55.0f, 100.0f, 0.0f, LAYER_GROUND);
	Coord3D from = u->pos, to{ 225.0f, 105.0f, 0.0f };
	bool partial = true;
	Path *up = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	REQUIRE(up != nullptr);
	CHECK_FALSE(partial);
	const std::vector<PathfindLayerEnum> layers = nodeLayers(*up);
	REQUIRE(!layers.empty());
	CHECK(layers.back() == LAYER_WALL_FIRST);
	// the layers never go back and never jump from the ground to the wall: ground ... ramp ... wall
	int phase = 0;
	for (PathfindLayerEnum l : layers)
	{
		const int p = l == LAYER_GROUND ? 0 : (l == LAYER_RAMP ? 1 : 2);
		CHECK(p >= phase);     // tolerance: an order (the layers never go back), not a threshold
		CHECK(p <= phase + 1); // tolerance: an order (no step skips the ramp), not a threshold
		phase = p;
	}
	// every node on the ramp is inside the ramp's band, and the last node is at the wall's height
	for (const PathNode *n = up->getFirstNode(); n; n = n->getNext())
	{
		if (n->getLayer() == LAYER_RAMP)
		{
			CHECK(n->getPosition()->x >= 90.0f);  // tolerance: the band of the ramp cells 9 .. 14 (x 90 .. 150)
			CHECK(n->getPosition()->x <= 150.0f); // tolerance: the band of the ramp cells 9 .. 14 (x 90 .. 150)
		}
	}
	CHECK(up->getLastNode()->getPosition()->z == 60.0f);
	delete up;
	// back down from the wall top
	LayeredUnit *v = w.unit(2, 225.0f, 105.0f, 60.0f, LAYER_WALL_FIRST);
	Coord3D top = v->pos, ground{ 55.0f, 105.0f, 0.0f };
	Path *down = w.pf->findPath(v, groundLoco(), &top, &ground, &partial);
	REQUIRE(down != nullptr);
	CHECK_FALSE(partial);
	bool sawRamp = false;
	for (PathfindLayerEnum l : nodeLayers(*down))
	{
		sawRamp = sawRamp || l == LAYER_RAMP;
	}
	CHECK(sawRamp);
	CHECK(down->getLastNode()->getLayer() == LAYER_GROUND);
	delete down;
	// a goal on the ground beside the wall: the path stays on the ground
	Coord3D beside{ 200.0f, 30.0f, 0.0f };
	Path *flat = w.pf->findPath(u, groundLoco(), &from, &beside, &partial);
	REQUIRE(flat != nullptr);
	for (PathfindLayerEnum l : nodeLayers(*flat))
	{
		CHECK(l == LAYER_GROUND);
	}
	delete flat;
	// a straight line from the ground onto the wall top is not passable; along the top it is
	CHECK_FALSE(w.pf->isLinePassable(u, LOCOMOTORSURFACE_GROUND, LAYER_GROUND, Coord3D{ 200.0f, 140.0f, 0.0f }, Coord3D{ 200.0f, 100.0f, 0.0f }, false, true));
	CHECK(w.pf->isLinePassable(v, LOCOMOTORSURFACE_GROUND, LAYER_WALL_FIRST, Coord3D{ 165.0f, 100.0f, 60.0f }, Coord3D{ 235.0f, 100.0f, 60.0f }, false, true));
	// a ground unit is not at a valid position on the wall top; a wall unit is
	Coord3D onTop{ 200.0f, 100.0f, 60.0f };
	CHECK_FALSE(w.pf->validMovementPosition(u, groundLoco(), LAYER_GROUND, &onTop));
	CHECK(w.pf->validMovementPosition(v, groundLoco(), LAYER_WALL_FIRST, &onTop));
}

TEST_CASE("wall1 walls: a unit's layer follows the path node it is on (Pathfinder::UpdateLayer): onto the ramp inside the ramp, onto the wall off it near the top")
{
	WallWorld w;
	LayeredUnit *u = w.unit(1, 60.0f, 100.0f, 0.0f, LAYER_GROUND);
	// on the ground before the ramp, a ramp node ahead: still the ground (not on the ramp)
	CHECK(w.pf->updateLayer(*u, LAYER_RAMP) == LAYER_GROUND);
	// inside the ramp: the ramp
	u->pos = { 120.0f, 100.0f, 24.0f };
	CHECK(w.pf->updateLayer(*u, LAYER_RAMP) == LAYER_RAMP);
	u->layer = LAYER_RAMP;
	// a wall node while still on the ramp: the ramp
	CHECK(w.pf->updateLayer(*u, LAYER_WALL_FIRST) == LAYER_RAMP);
	// off the ramp at the wall's height: the wall
	u->pos = { 155.0f, 100.0f, 60.0f };
	CHECK(w.pf->updateLayer(*u, LAYER_WALL_FIRST) == LAYER_WALL_FIRST);
	// a ground unit never jumps onto the wall from a wall node (mode 1 of RW: a ground unit off a ramp stays on the ground)
	LayeredUnit *g = w.unit(2, 200.0f, 140.0f, 0.0f, LAYER_GROUND);
	CHECK(w.pf->updateLayer(*g, LAYER_WALL_FIRST) == LAYER_GROUND);
	// a wall unit with a ground node ahead stays on the wall until it is on the ramp
	u->layer = LAYER_WALL_FIRST;
	CHECK(w.pf->updateLayer(*u, LAYER_GROUND) == LAYER_WALL_FIRST);
	u->pos = { 120.0f, 100.0f, 60.0f };
	CHECK(w.pf->updateLayer(*u, LAYER_RAMP) == LAYER_RAMP);
	// settle: a ground unit put on the wall top takes the wall's layer; a ramp unit off the ramp on the ground takes the ground
	LayeredUnit *placed = w.unit(3, 200.0f, 100.0f, 60.0f, LAYER_GROUND);
	CHECK(w.pf->settleLayer(*placed) == LAYER_WALL_FIRST);
	LayeredUnit *off = w.unit(4, 60.0f, 100.0f, 0.0f, LAYER_RAMP);
	CHECK(w.pf->settleLayer(*off) == LAYER_GROUND);
	// a unit walking straight (no path node): a ground unit on the ramp takes the ramp, a ramp unit off its top at the wall's height the wall, a wall unit
	// stepping onto the ramp the ramp
	LayeredUnit *walker = w.unit(5, 125.0f, 100.0f, 30.0f, LAYER_GROUND);
	CHECK(w.pf->settleLayer(*walker) == LAYER_RAMP);
	LayeredUnit *topped = w.unit(6, 155.0f, 100.0f, 58.0f, LAYER_RAMP);
	CHECK(w.pf->settleLayer(*topped) == LAYER_WALL_FIRST);
	LayeredUnit *down = w.unit(7, 125.0f, 100.0f, 60.0f, LAYER_WALL_FIRST);
	CHECK(w.pf->settleLayer(*down) == LAYER_RAMP);
	// a ground unit beside the ramp, under its side, stays on the ground (the cell is the ramp's, the unit is not on the ramp's surface: x 95 is off the ramp)
	LayeredUnit *beside = w.unit(8, 95.0f, 100.0f, 0.0f, LAYER_GROUND);
	CHECK(w.pf->settleLayer(*beside) == LAYER_GROUND);
}

TEST_CASE("wall1 walls: a destroyed wall piece's cells go back to the ground; the next piece at the same height shares the layer, a taller one keeps its cells")
{
	WallWorld w;
	w.pf->removeObjectFromPathfindMap(*w.piece);
	CHECK(w.pf->wallPieceCount() == 0);
	for (int x = 9; x <= 25; ++x)
	{
		INFO("cell " << x);
		CHECK(w.cell(x, 10).getLayer() == LAYER_GROUND);
		CHECK(w.cell(x, 10).getConnectLayer() == LAYER_INVALID);
		CHECK(w.cell(x, 10).getType() == PathfindCell::CELL_CLEAR);
	}
	// again, with a second piece next to it 2 higher (within 3.5: the same layer, the height raised to 62) and a third 30 higher crossing the first
	w.pf->addObjectToPathfindMap(*w.piece);
	WallPieceObject *next = new WallPieceObject();
	next->id = 51;
	next->pos = { 300.0f, 100.0f, 0.0f };
	next->structure = true;
	next->kinds.insert(PK_WALK_ON_TOP_OF_WALL); // lane BRIDGE-1: RW 0x936B7D classifies the meshes of a WALK_ON_TOP_OF_WALL template only
	next->geometry.type = PATHFIND_GEOMETRY_BOX;
	next->geometry.majorRadius = 50.0f;
	next->geometry.minorRadius = 10.0f;
	next->walls.walls.push_back(quad(250.0f, 90.0f, 350.0f, 110.0f, 62.0f, 62.0f));
	w.world.objects[next->id] = next;
	w.pf->addObjectToPathfindMap(*next);
	REQUIRE(w.pf->wallHeights().size() == 1);
	CHECK(w.pf->wallHeights()[0] == 62.0f);
	// the seam: the next piece's box footprint does not cover the first piece's top (a wall piece's footprint never covers a wall top)
	CHECK(w.cell(25, 10).getLayer() == LAYER_WALL_FIRST);
	CHECK(w.cell(25, 10).getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.cell(30, 10).getLayer() == LAYER_WALL_FIRST);
	WallPieceObject *tall = new WallPieceObject();
	tall->id = 52;
	tall->pos = { 200.0f, 100.0f, 0.0f };
	tall->structure = true;
	tall->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
	tall->geometry.type = PATHFIND_GEOMETRY_BOX;
	tall->geometry.majorRadius = 5.0f;
	tall->geometry.minorRadius = 5.0f;
	tall->walls.walls.push_back(quad(195.0f, 60.0f, 205.0f, 140.0f, 92.0f, 92.0f));
	w.world.objects[tall->id] = tall;
	w.pf->addObjectToPathfindMap(*tall);
	REQUIRE(w.pf->wallHeights().size() == 2);
	CHECK(w.cell(20, 10).getLayer() == (PathfindLayerEnum)(LAYER_WALL_FIRST + 1));
	// the lower first piece classified again keeps off the taller piece's cells
	w.pf->removeObjectFromPathfindMap(*next);
	CHECK(w.cell(20, 10).getLayer() == (PathfindLayerEnum)(LAYER_WALL_FIRST + 1));
	CHECK(w.cell(30, 10).getLayer() == LAYER_GROUND);
	CHECK(w.cell(22, 10).getLayer() == LAYER_WALL_FIRST);
}

TEST_CASE("wall1 walls: without a wall piece every cell stays on the ground and the layer queries answer the terrain (a map without walls is unchanged)")
{
	SyntheticTerrain terrain(20, 20);
	terrain.heightFn = [](float x, float) { return x * 0.1f; };
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	TestObject *box = new TestObject();
	box->id = 7;
	box->pos = { 100.0f, 100.0f, 0.0f };
	box->structure = true;
	box->geometry.type = PATHFIND_GEOMETRY_BOX;
	box->geometry.majorRadius = 30.0f;
	box->geometry.minorRadius = 30.0f;
	world.objects[box->id] = box;
	pf.addObjectToPathfindMap(*box);
	CHECK(pf.wallPieceCount() == 0);
	for (int x = 0; x < 20; ++x)
	{
		for (int y = 0; y < 20; ++y)
		{
			CHECK(pf.cellAt(x, y)->getLayer() == LAYER_GROUND);
		}
	}
	CHECK(pf.getLayerHeight(55.0f, 5.0f, LAYER_RAMP) == terrain.getGroundHeight(55.0f, 5.0f));
	CHECK(pf.getHighestLayerHeight(55.0f, 5.0f) == terrain.getGroundHeight(55.0f, 5.0f));
	delete box;
}

TEST_CASE("wall1 walls: S-4840 is in Pathfinder::allStops() and the wall piece raises it")
{
	int n = 0;
	for (const std::string &s : Pathfinder::allStops())
	{
		n += s.rfind("S-4840 wall tops and ramps", 0) == 0 ? 1 : 0;
	}
	CHECK(n == 1);
	WallWorld w;
	bool raised = false;
	for (const std::string &s : w.pf->stops())
	{
		raised = raised || s.rfind("S-4840 wall tops and ramps", 0) == 0;
	}
	CHECK(raised);
}

// review (#63): the 48th wall-top height takes the last wall layer, 0x40, which a cell's layer field must hold (it was 6 bits: 0x40 became LAYER_INVALID)
TEST_CASE("wall1 walls: 48 wall-top heights take the layers 0x11 .. 0x40; the 48th wall's cells keep layer 0x40")
{
	SyntheticTerrain terrain{ 80, 60 };
	TestWorld world;
	std::vector<WallPieceObject *> pieces;
	for (int i = 0; i < 48; ++i)
	{
		// 8 x 6 pieces 100 apart, each a 40 x 40 wall top at its own height (10 apart: more than the 3.5 that joins two heights)
		WallPieceObject *p = new WallPieceObject();
		p->id = (PathfindObjectID)(100 + i);
		const float cx = 50.0f + (float)(i % 8) * 100.0f, cy = 50.0f + (float)(i / 8) * 100.0f;
		p->pos = { cx, cy, 0.0f };
		p->structure = true;
		p->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
		p->geometry.type = PATHFIND_GEOMETRY_BOX;
		p->geometry.majorRadius = 20.0f;
		p->geometry.minorRadius = 20.0f;
		const float h = 10.0f * (float)(i + 1);
		p->walls.walls.push_back(quad(cx - 20.0f, cy - 20.0f, cx + 20.0f, cy + 20.0f, h, h));
		world.objects[p->id] = p;
		pieces.push_back(p);
	}
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	for (WallPieceObject *p : pieces)
	{
		pf.addObjectToPathfindMap(*p);
	}
	REQUIRE(pf.wallHeights().size() == 48u);
	for (int i = 0; i < 48; ++i)
	{
		INFO("piece " << i);
		const PathfindCell *c = pf.cellAt(5 + (i % 8) * 10, 5 + (i / 8) * 10); // the cell at the piece's centre
		REQUIRE(c != nullptr);
		CHECK(c->getLayer() == (PathfindLayerEnum)((int)LAYER_WALL_FIRST + i));
	}
	CHECK(pf.cellAt(5 + 7 * 10, 5 + 5 * 10)->getLayer() == LAYER_LAST);
	for (auto &kv : world.objects)
	{
		delete kv.second;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// retail: Helm's Deep
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
using namespace starttest;

int templateIndex(Shared &s, const std::string &name)
{
	for (int i = 0; i < s.world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(i);
		if (pt && pt->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

NewGameMessage helmsDeep(Shared &s)
{
	NewGameMessage m;
	m.game.mapName = "maps/map wor helms deep/map wor helms deep.map";
	m.game.seed = 11;
	m.game.startingCash = 1000;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		sl.name = i == 0 ? u"Host" : u"Easy";
		sl.accepted = true;
		sl.playerTemplate = templateIndex(s, factions[i]);
		REQUIRE(sl.playerTemplate >= 0); // tolerance: the index of a found template (-1: not found)
		sl.startPos = i;
		sl.color = i;
	}
	return m;
}

struct WallGame
{
	Shared &s;
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	explicit WallGame(Shared &sh) : s(sh), source(*sh.mount->fs), assets(source)
	{
		std::string error;
		static std::vector<MapCacheEntry> cache;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		REQUIRE_MESSAGE(NewGame::prepareNewGame(helmsDeep(s), s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game->load(o, &error), error);
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			game->advance(0.2);
		}
	}
	// the human's start (slot 0 at start 0, Player_1_Start: Helm's Deep's fortress)
	Coord3D startOfSlot0()
	{
		const TerrainLogic *t = game->logic().terrain();
		REQUIRE(t);
		const Waypoint *w = t->findWaypointByName("Player_1_Start");
		REQUIRE(w);
		return w->location;
	}
	Player *human()
	{
		for (int i = 0; i < game->players().getPlayerCount(); ++i)
		{
			Player *p = game->players().getNthPlayer(i);
			if (p && p->getPlayerType() == PLAYER_HUMAN)
			{
				return p;
			}
		}
		return nullptr;
	}
};

// a wall-top cell that a unit at `from` reaches by a full path (searched from the nearest wall cells outward), its centre in `goal`
bool reachableWallTop(Pathfinder &pf, PathfindObject &unit, const PathfindLocomotorInfo &loco, const Coord3D &from, Coord3D &goal, PathfindLayerEnum &layer)
{
	struct Candidate
	{
		float d;
		int x, y;
	};
	std::vector<Candidate> cands;
	const ICoord2D *ext = pf.getExtent();
	for (int x = 0; x <= ext->x; ++x)
	{
		for (int y = 0; y <= ext->y; ++y)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (c && isWallLayer(c->getLayer()) && c->getConnectLayer() == LAYER_INVALID && c->getType() == PathfindCell::CELL_CLEAR)
			{
				// a cell inside the top: all eight neighbours on its layer (not the top's edge, where a unit's footprint meets the ground)
				bool inner = true;
				for (int dx = -1; dx <= 1 && inner; ++dx)
				{
					for (int dy = -1; dy <= 1; ++dy)
					{
						const PathfindCell *n = pf.cellAt(x + dx, y + dy);
						inner = inner && n && n->getLayer() == c->getLayer();
					}
				}
				if (inner)
				{
					const float fx = (float)x * 10.0f + 5.0f - from.x, fy = (float)y * 10.0f + 5.0f - from.y;
					cands.push_back({ fx * fx + fy * fy, x, y });
				}
			}
		}
	}
	std::sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) { return a.d < b.d || (a.d == b.d && (a.x < b.x || (a.x == b.x && a.y < b.y))); });
	for (size_t i = 0; i < cands.size() && i < 400; i += 7)
	{
		Coord3D to{ (float)cands[i].x * 10.0f + 5.0f, (float)cands[i].y * 10.0f + 5.0f, 0.0f };
		Coord3D f = from;
		bool partial = true;
		Path *p = pf.findPath(&unit, loco, &f, &to, &partial);
		const bool ok = p && !partial && isWallLayer(p->getLastNode()->getLayer());
		delete p;
		if (ok)
		{
			goal = to;
			layer = pf.cellAt(cands[i].x, cands[i].y)->getLayer();
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("wall1 walls retail: Helm's Deep's wall sections are walkable: their WallBoundsMesh tops take wall layers at the meshes' heights, their ramps the ramp layer")
{
	OPENBFME_REQUIRE_START(s);
	WallGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	std::map<std::string, int> pieces;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		const PathfindWallGeometry *geo = g.game->ai().adapterFor(*o).getWallGeometry();
		if (geo)
		{
			++pieces[o->getTemplate()->getName()];
			if (pieces[o->getTemplate()->getName()] == 1)
			{
				std::string surf;
				for (const PathfindWallSurface &w : geo->walls)
				{
					surf += " top z " + std::to_string(w.height) + " (" + std::to_string(w.triangles.size()) + " triangles, terrain " +
						std::to_string(g.game->logic().getGroundHeight(o->getPosition()->x, o->getPosition()->y)) + ")";
				}
				for (const PathfindWallSurface &r : geo->ramps)
				{
					surf += " ramp to z " + std::to_string(r.height) + " (" + std::to_string(r.triangles.size()) + " triangles)";
				}
				MESSAGE(o->getTemplate()->getName() << ":" << surf);
			}
		}
	}
	// the retail inconsistencies: HelmsDeepCulvert's RampMesh1 / 2 (P2 / P3) are not in its model RBHDDWCulv (retail makes no ramp from them); lane WALL-2:
	// the gatehouses' RaisedWallMesh P3 is not in RBHDGatHsL / RBHDGatHsR (the walkway is the centre's P1 alone)
	const std::vector<std::string> problems = g.game->report().wallMeshProblems;
	CHECK(problems == std::vector<std::string>{ "HelmsDeepGatehouseRight: mesh P3 is not a mesh of RBHDGatHsR", "HelmsDeepGatehouseLeft: mesh P3 is not a mesh of RBHDGatHsL",
		"HelmsDeepCulvert: mesh P2 is not a mesh of RBHDDWCulv", "HelmsDeepCulvert: mesh P3 is not a mesh of RBHDDWCulv" });
	for (const std::string &e : g.game->report().errors)
	{
		CHECK_MESSAGE(e.find("wall meshes") == std::string::npos, e);
	}
	CHECK(pieces.count("HelmsDeepSectionAi") == 1);
	CHECK(pieces.count("HelmsDeepCulvert") == 1);
	CHECK(pf.wallPieceCount() == WALL1_PIECES);
	CHECK(pf.rampCount() == WALL1_RAMPS);
	REQUIRE(!pf.wallHeights().empty());
	std::string heights;
	for (float h : pf.wallHeights())
	{
		heights += " " + std::to_string(h);
	}
	MESSAGE("wall heights:" << heights);
	std::map<int, int> cellsByLayer;
	const ICoord2D *ext = pf.getExtent();
	for (int x = 0; x <= ext->x; ++x)
	{
		for (int y = 0; y <= ext->y; ++y)
		{
			++cellsByLayer[(int)pf.cellAt(x, y)->getLayer()];
		}
	}
	for (const auto &kv : cellsByLayer)
	{
		MESSAGE("layer " << kv.first << ": " << kv.second << " cells");
	}
	// the cells of each layer on Helm's Deep (the counts of this port's classification, S-4840: not a retail dump)
	CHECK(cellsByLayer[(int)LAYER_RAMP] == 1908);
	CHECK(cellsByLayer[0x11] == 2042);
	CHECK(cellsByLayer[0x12] == 870);
	CHECK(cellsByLayer[0x13] == 58);
	CHECK(cellsByLayer[0x14] == 1547);
	CHECK(cellsByLayer[0x15] == 129);
	CHECK(cellsByLayer[0x16] == 440);
	CHECK(pf.wallHeights().size() == 6);
	int wallCells = 0;
	for (const auto &kv : cellsByLayer)
	{
		wallCells += isWallLayer((PathfindLayerEnum)kv.first) ? kv.second : 0;
	}
	CHECK(wallCells == 2042 + 870 + 58 + 1547 + 129 + 440);
	// every wall height lies above the terrain under the fortress (the meshes are at the sections' world z)
	for (float h : pf.wallHeights())
	{
		CHECK(h > 0.0f); // tolerance: a wall top above the map's zero (the meshes are at the sections' world z)
	}
}

TEST_CASE("wall1 walls retail: on Helm's Deep a hero ordered onto a wall top walks up onto it and stands at the wall's height, then walks back down")
{
	OPENBFME_REQUIRE_START(s);
	WallGame g(*s);
	g.frames(2);
	Player *me = g.human();
	REQUIRE(me);
	Pathfinder &pf = g.game->ai().pathfinder();
	// the hero at the human's start (the fortress at start 1)
	const Coord3D start = g.startOfSlot0();
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", me->getPlayerIndex(), start, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	g.frames(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	const Coord3D from = *hero->getPosition();
	Coord3D goal;
	PathfindLayerEnum wallLayer = LAYER_INVALID;
	REQUIRE_MESSAGE(reachableWallTop(pf, ai->adapter(), ai->locomotorInfo(), from, goal, wallLayer), "no wall top cell is reachable from the start at " << from.x << ", " << from.y);
	MESSAGE("hero from " << from.x << ", " << from.y << " to the wall top at " << goal.x << ", " << goal.y << " (layer " << (int)wallLayer << ")");
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	bool sawRamp = false;
	for (int f = 0; f < 900; ++f)
	{
		g.frames(1);
		sawRamp = sawRamp || ai->adapter().getLayer() == LAYER_RAMP;
		const Coord3D p = *hero->getPosition();
		const float dx = p.x - goal.x, dy = p.y - goal.y;
		if (dx * dx + dy * dy < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	const float dx = at.x - goal.x, dy = at.y - goal.y;
	INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z << " on layer " << (int)ai->adapter().getLayer());
	CHECK(dx * dx + dy * dy < 900.0f); // tolerance: the walk ends within the locomotor's close-enough distance of the cell centre it was sent to
	CHECK(sawRamp);
	CHECK(isWallLayer(ai->adapter().getLayer()));
	CHECK(at.z == pf.getLayerHeight(at.x, at.y, wallLayer));
	CHECK(at.z > g.game->logic().getGroundHeight(at.x, at.y) + 10.0f); // tolerance: up on the wall, not at the terrain (the wall stands 23 over the courtyard)
	// back down to where it came from
	ai->aiMoveToPosition(from, CMD_FROM_PLAYER);
	for (int f = 0; f < 900; ++f)
	{
		g.frames(1);
		const Coord3D p = *hero->getPosition();
		const float ex = p.x - from.x, ey = p.y - from.y;
		if (ex * ex + ey * ey < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D back = *hero->getPosition();
	INFO("back at " << back.x << ", " << back.y << ", " << back.z << " on layer " << (int)ai->adapter().getLayer());
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
	CHECK(back.z == g.game->logic().getGroundHeight(back.x, back.y));
}

TEST_CASE("wall1 walls retail: on Helm's Deep a horde ordered onto a wall top gets its members up there")
{
	OPENBFME_REQUIRE_START(s);
	WallGame g(*s);
	g.frames(2);
	Player *me = g.human();
	REQUIRE(me);
	Pathfinder &pf = g.game->ai().pathfinder();
	const Coord3D start = g.startOfSlot0();
	std::string error;
	Object *horde = g.game->createObject("GondorFighterHorde", me->getPlayerIndex(), start, 0.0f, &error);
	REQUIRE_MESSAGE(horde, error);
	g.frames(10);
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	REQUIRE(ai);
	Coord3D goal;
	PathfindLayerEnum wallLayer = LAYER_INVALID;
	REQUIRE(reachableWallTop(pf, ai->adapter(), ai->locomotorInfo(), *horde->getPosition(), goal, wallLayer));
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	// the members walk: no member moves more than a stride in a frame (a member put on its goal, RW 0x70C201, would jump; the steep stairs make big height steps)
	std::map<ObjectID, Coord3D> last;
	float maxStep = 0.0f;
	for (int f = 0; f < 1200; ++f)
	{
		g.frames(1);
		for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getContainedBy() != horde)
			{
				continue;
			}
			const Coord3D p = *o->getPosition();
			auto it = last.find(o->getID());
			if (it != last.end())
			{
				const float dx = p.x - it->second.x, dy = p.y - it->second.y;
				maxStep = std::max(maxStep, std::sqrt(dx * dx + dy * dy));
			}
			last[o->getID()] = p;
		}
	}
	// up there: above the terrain on the wall top, or at the top of the stair on its joint cells (the ramp's layer at the wall's height)
	int members = 0, up = 0;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getContainedBy() == horde && o->getAIUpdateInterface())
		{
			++members;
			const Coord3D p = *o->getPosition();
			const PathfindLayerEnum l = o->getAIUpdateInterface()->adapter().getLayer();
			if ((isWallLayer(l) || l == LAYER_RAMP) && p.z == pf.getLayerHeight(p.x, p.y, l) && p.z > g.game->logic().getGroundHeight(p.x, p.y))
			{
				++up;
			}
		}
	}
	const Coord3D hp = *horde->getPosition();
	MESSAGE("horde at " << hp.x << ", " << hp.y << " (goal " << goal.x << ", " << goal.y << "): " << up << " of " << members << " members up, largest step " << maxStep);
	CHECK(members == 15); // GondorFighterHorde's 15 soldiers
	CHECK(up == members);
	CHECK(maxStep < 20.0f); // tolerance: a walking member's stride in one logic frame (its speed / 5) stays below 20; a member put on its goal would jump much farther
}
// the owner's report after the pathfinder fix: a right click on a wall top still ordered nothing. RotWK gives a WALK_ON_TOP_OF_WALL object the selectable pick
// type (RW 0x4B583C, test_hud5_gatebutton.cpp), so the order click picks the wall section the cursor is on; it is the local player's (Helm's Deep's fortress)
// and has no command of its own, and the click on an own object ordered nothing (ZH CommandXlat.cpp:2294)
TEST_CASE("wall1 walls retail: a right click on a Helm's Deep wall top orders the selected hero onto it (MSG_DO_MOVETO), the wall section under the cursor is no target")
{
	if (!hudtest::haveWorld("wall1 right click"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map wor helms deep", "FactionMen");
	rig.frame(2);
	Pathfinder &pf = rig.game->ai().pathfinder();
	const TerrainLogic *terrain = rig.logic().terrain();
	REQUIRE(terrain);
	const Waypoint *startWp = terrain->findWaypointByName("Player_1_Start");
	REQUIRE(startWp);
	Object *hero = rig.make("GondorBoromir", startWp->location.x, startWp->location.y);
	REQUIRE(hero);
	rig.frame(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	Coord3D goal;
	PathfindLayerEnum wallLayer = LAYER_INVALID;
	REQUIRE(reachableWallTop(pf, ai->adapter(), ai->locomotorInfo(), *hero->getPosition(), goal, wallLayer));
	goal.z = pf.getLayerHeight(goal.x, goal.y, wallLayer);
	// the alternate mouse setup (the right click orders); the player's selection: a left click on the hero (MSG_CREATE_SELECTED_GROUP: the order click needs the
	// team, CommandXlat.h)
	rig.input->commandTranslator().setUseAlternateMouse(true);
	rig.lookAt(*hero->getPosition());
	rig.frame(1);
	const Coord3D hp = *hero->getPosition();
	const ICoord2D heroPx = rig.screenOf({ hp.x, hp.y, hp.z + 6.0f });
	rig.leftClick(heroPx.x, heroPx.y);
	REQUIRE(rig.input->ui().selected() == std::vector<ObjectID>{ hero->getID() });
	rig.frame(1);
	rig.lookAt(goal);
	rig.frame(1);
	const ICoord2D px = rig.screenOf(goal);
	const Object *picked = HudObjects::pickObject(rig.input->context(), px);
	REQUIRE(picked);
	CHECK(picked->isKindOfName("WALK_ON_TOP_OF_WALL")); // the wall section under the cursor
	const size_t before = rig.input->messageLog().size();
	rig.rightClick(px.x, px.y);
	std::string moveLine;
	for (size_t i = before; i < rig.input->messageLog().size(); ++i)
	{
		const std::string &l = rig.input->messageLog()[i];
		moveLine = l.rfind("MSG_DO_MOVETO", 0) == 0 ? l : moveLine;
	}
	std::string outcomes;
	for (const auto &kv : rig.input->commandTranslator().clickOutcomes())
	{
		outcomes += " " + kv.first + "=" + std::to_string(kv.second);
	}
	INFO("picked " << picked->getTemplate()->getName() << ", sent: " << moveLine << ", outcomes:" << outcomes);
	REQUIRE(!moveLine.empty());
	CHECK(rig.input->commandTranslator().clickOutcomes().at("command on an object") == 1); // the click met the wall section and ordered the move
	for (int f = 0; f < 900; ++f)
	{
		rig.frame(1);
		const Coord3D p = *hero->getPosition();
		const float dx = p.x - goal.x, dy = p.y - goal.y;
		if (dx * dx + dy * dy < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	const float dx = at.x - goal.x, dy = at.y - goal.y;
	INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z << " on layer " << (int)ai->adapter().getLayer());
	CHECK(dx * dx + dy * dy < 900.0f); // tolerance: the walk ends within the locomotor's close-enough distance of the clicked point
	CHECK(isWallLayer(ai->adapter().getLayer()));
	CHECK(at.z == pf.getLayerHeight(at.x, at.y, ai->adapter().getLayer()));
}
