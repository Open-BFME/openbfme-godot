// OpenBFME unit tests, lane WALL-1 (the owner's report: on Helm's Deep units cannot get up onto the walls). GPL-3.0.
//
// The wall tops (layers 0x11 .. 0x40) and ramps (layer 0x10) of a wall piece in the pathfinder's ground grid (AIPathfindWalls.cpp), on a synthetic map whose
// expected values are derived by hand from the rules named there, then on the retail Helm's Deep: its wall sections become walkable surfaces at their meshes'
// heights, and a hero and a horde ordered onto a wall top walk up a ramp onto it and stand at the wall's height. The retail tests SKIP loudly without the installs.

#include "doctest.h"

#include "Common/SimulationProfile.h"

#include "HudTestUtil.h"
#include "PathfindTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/DrawableWallMeshes.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/HudObjects.h"
#include "GameClient/SelectionDecals.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
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
	// the wall top: every cell with a corner in x 150 .. 250, y 90 .. 110 (cells 15 .. 25 x 8 .. 11; cell 15 is the ramp's joint), the box's obstacle gone;
	// lane WALL-5: its rim (rows 8 and 11, column 25) is ClassifyWall's edge (type 8), its inside (rows 9 and 10) CLEAR
	for (int x = 16; x <= 24; ++x)
	{
		for (int y = 8; y <= 11; ++y)
		{
			INFO("cell " << x << ", " << y);
			CHECK(w.cell(x, y).getLayer() == LAYER_WALL_FIRST);
			CHECK(w.cell(x, y).getType() == (y == 8 || y == 11 ? PathfindCell::CELL_WALL_EDGE : PathfindCell::CELL_CLEAR));
			CHECK(w.cell(x, y).getBit17()); // lane WALL-2: RW 0x935191 sets it on a wall cell
		}
	}
	CHECK(w.cell(25, 9).getType() == PathfindCell::CELL_WALL_EDGE);
	CHECK(w.cell(25, 10).getType() == PathfindCell::CELL_WALL_EDGE);
	// the ramp's joint: the edge pass made it type 8, the ramp's cell rule (stamped after every wall top, RW 0x93527F) CLEAR again
	CHECK(w.cell(15, 10).getType() == PathfindCell::CELL_CLEAR);
	// the box footprint (y 70 .. 130) outside the wall top's rows 8 .. 11 stays the box's obstacle on the ground
	CHECK(w.cell(20, 7).getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(w.cell(20, 7).getLayer() == LAYER_GROUND);
	CHECK(w.cell(20, 12).getType() == PathfindCell::CELL_OBSTACLE);
	// RotWK's ramp cell RW 0x93527F on the Bridge getRamp builds from the quad: from (150, 100, 60), to (100, 100, 0), width 20, so its start edge is x 150
	// and its end edge x 100, y 90 .. 110. The edge test RW 0x67CAC6 shortens each edge by 10 at both ends: for a ramp 20 wide each is the point at y 100.
	// The joint: cell 15 (x 150 .. 160) has its corners at x 150 on the ramp and the start edge's point (150, 100) on its square: it keeps the wall's
	// layer (60, the ramp's 60 there) and connects to the ramp
	CHECK(w.cell(15, 10).getLayer() == LAYER_WALL_FIRST);
	CHECK(w.cell(15, 10).getConnectLayer() == LAYER_RAMP);
	// the ramp: cells 10 .. 14 have all four corners on it: layer 0x10, no connect layer
	for (int x = 10; x <= 14; ++x)
	{
		INFO("cell " << x);
		CHECK(w.cell(x, 10).getLayer() == LAYER_RAMP);
		CHECK(w.cell(x, 10).getConnectLayer() == LAYER_INVALID);
	}
	// its foot: cell 9 (x 90 .. 100) has its corners at x 100 on the ramp and the end edge's point (100, 100): the ground (0) is within 15 of the ramp's
	// plane at the cell's corner (90, 100): -12. It stays the ground and connects to the ramp
	CHECK(w.cell(9, 10).getLayer() == LAYER_GROUND);
	CHECK(w.cell(9, 10).getConnectLayer() == LAYER_RAMP);
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

namespace
{
// lane WALL-5: the wall piece of WallWorld without its ramp and with a footprint of one cell (its wall top's edges meet the ground, not the box's obstacle)
struct EdgeWorld
{
	SyntheticTerrain terrain{ 40, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	WallPieceObject *piece = nullptr;
	EdgeWorld()
	{
		piece = new WallPieceObject();
		piece->id = 50;
		piece->pos = { 200.0f, 100.0f, 0.0f };
		piece->structure = true;
		piece->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
		piece->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		piece->geometry.majorRadius = 1.0f;
		piece->walls.walls.push_back(quad(150.0f, 90.0f, 250.0f, 110.0f, 60.0f, 60.0f));
		world.objects[piece->id] = piece;
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		pf->addObjectToPathfindMap(*piece);
	}
	~EdgeWorld()
	{
		for (auto &kv : world.objects)
		{
			delete kv.second;
		}
	}
	const PathfindCell &cell(int x, int y) const { return *pf->cellAt(x, y); }
};
} // namespace

// lane WALL-5: ClassifyWall's edge pass (RW 0x936686 .. 0x936811, BFME2 decomp PathfinderClassifyWall.cpp 244-258): after the stamp, a CLEAR cell of the
// top's window with 2 or more of its 8 neighbours on another layer is pinched; a stamped cell that is pinched becomes type 8, every stamped cell is unpinched
TEST_CASE("wall5 edges: a wall top's rim is ClassifyWall's edge (type 8, no locomotor), its inside CLEAR; the ground ring beside it pinched; removal undoes both")
{
	EdgeWorld w;
	CHECK(Pathfinder::validLocomotorSurfacesForCellType(PathfindCell::CELL_WALL_EDGE) == 0u); // RW 0xDA2464
	// the top's cells (a corner in x 150 .. 250, y 90 .. 110: the edges lie on cell lines, so cells 14 and 25, 8 and 11 have a corner on them): 14 .. 25 x
	// 8 .. 11, all on the wall's layer and unpinched
	for (int x = 14; x <= 25; ++x)
	{
		for (int y = 8; y <= 11; ++y)
		{
			INFO("cell " << x << ", " << y);
			const bool rim = x == 14 || x == 25 || y == 8 || y == 11;
			CHECK(w.cell(x, y).getLayer() == LAYER_WALL_FIRST);
			CHECK(w.cell(x, y).getType() == (rim ? PathfindCell::CELL_WALL_EDGE : PathfindCell::CELL_CLEAR));
			CHECK_FALSE(w.cell(x, y).getPinched());
		}
	}
	// the ground ring: rows 7 and 12 under columns 14 .. 25, columns 13 and 26 beside rows 8 .. 11 (2 or 3 wall neighbours) pinched; the corners (one) not
	for (int x = 12; x <= 27; ++x)
	{
		for (int y = 6; y <= 13; ++y)
		{
			if (x >= 14 && x <= 25 && y >= 8 && y <= 11)
			{
				continue;
			}
			INFO("cell " << x << ", " << y);
			const bool ring = ((y == 7 || y == 12) && x >= 14 && x <= 25) || ((x == 13 || x == 26) && y >= 8 && y <= 11);
			CHECK(w.cell(x, y).getLayer() == LAYER_GROUND);
			CHECK(w.cell(x, y).getType() == PathfindCell::CELL_CLEAR);
			CHECK(w.cell(x, y).getPinched() == ring);
		}
	}
	const PathfindGridStats stats = w.pf->gridStats();
	CHECK(stats.types[PathfindCell::CELL_WALL_EDGE] == 28); // 2 x 12 + 2 x 2
	CHECK(stats.pinched == 32);                             // 2 x 12 + 2 x 4
	// a wall unit's path along the top stays on its inside
	LayeredUnit *u = new LayeredUnit();
	u->id = 1;
	u->pos = { 165.0f, 95.0f, 60.0f };
	u->mobile = true;
	u->layer = LAYER_WALL_FIRST;
	u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
	u->geometry.majorRadius = 5.0f;
	w.world.objects[u->id] = u;
	Coord3D from = u->pos, to{ 235.0f, 105.0f, 60.0f };
	bool partial = true;
	Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		const int cx = (int)std::floor(n->getPosition()->x * 0.1f), cy = (int)std::floor(n->getPosition()->y * 0.1f);
		INFO("node at " << n->getPosition()->x << ", " << n->getPosition()->y);
		CHECK(w.cell(cx, cy).getType() == PathfindCell::CELL_CLEAR);
		CHECK(n->getLayer() == LAYER_WALL_FIRST);
	}
	delete p;
	// the removal (RW: the unpinch pass, then the reset of the piece's cells): the ground everywhere, CLEAR, unpinched
	w.pf->removeObjectFromPathfindMap(*w.piece);
	for (int x = 12; x <= 27; ++x)
	{
		for (int y = 6; y <= 13; ++y)
		{
			INFO("cell " << x << ", " << y);
			CHECK(w.cell(x, y).getLayer() == LAYER_GROUND);
			CHECK(w.cell(x, y).getConnectLayer() == LAYER_INVALID);
			CHECK(w.cell(x, y).getType() == PathfindCell::CELL_CLEAR);
			CHECK_FALSE(w.cell(x, y).getPinched());
		}
	}
	CHECK(w.pf->gridStats().pinched == 0);
}

// lane WALL-5: a wall unit's goal on the rim is moved off it (the search's goal adjustment: the rim refuses every locomotor). A ground unit standing in a rim
// cell searches from the top's layer (the cell's): RotWK's search start there is not read (S-4840), so no case pins it
TEST_CASE("wall5 edges: a wall unit sent onto the rim ends on the top's inside")
{
	EdgeWorld w;
	auto unitAt = [&](PathfindObjectID id, float x, float y, float z, PathfindLayerEnum layer) {
		LayeredUnit *u = new LayeredUnit();
		u->id = id;
		u->pos = { x, y, z };
		u->mobile = true;
		u->layer = layer;
		u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		u->geometry.majorRadius = 5.0f;
		w.world.objects[id] = u;
		return u;
	};
	LayeredUnit *top = unitAt(1, 165.0f, 95.0f, 60.0f, LAYER_WALL_FIRST);
	REQUIRE(w.cell(20, 8).getType() == PathfindCell::CELL_WALL_EDGE);
	Coord3D from = top->pos, to{ 205.0f, 85.0f, 60.0f }; // cell (20, 8): the rim
	bool partial = true;
	Path *p = w.pf->findPath(top, groundLoco(), &from, &to, &partial);
	REQUIRE(p != nullptr);
	const Coord3D end = *p->getLastNode()->getPosition();
	INFO("the path ends at " << end.x << ", " << end.y);
	CHECK(w.cell((int)std::floor(end.x * 0.1f), (int)std::floor(end.y * 0.1f)).getType() == PathfindCell::CELL_CLEAR);
	CHECK(p->getLastNode()->getLayer() == LAYER_WALL_FIRST);
	delete p;
}

// lane WALL-5: ClassifyWall's height layer (RW 0x9362A3 .. 0x93650A): the closest existing height, a tie the first, a new layer only more than 3.5 away
TEST_CASE("wall5 edges: a wall top joins the closest height's layer (a tie the first), a new layer only more than 3.5 from every height")
{
	struct Case
	{
		std::vector<float> heights;
		std::vector<float> expect; // the layers' heights after the pieces
		int lastLayer;             // the last piece's layer - LAYER_WALL_FIRST
	};
	const Case cases[] = {
		{ { 100.0f, 105.0f, 102.75f }, { 100.0f, 105.0f }, 1 }, // 2.75 from 100, 2.25 from 105: 105's (the port took the first within 3.5: 100's)
		{ { 100.0f, 105.0f, 102.5f }, { 102.5f, 105.0f }, 0 },  // 2.5 from both: the first, raised to 102.5
		{ { 100.0f, 103.5f }, { 103.5f }, 0 },                   // exactly 3.5: one layer (the port's < 3.5 made a second)
		{ { 100.0f, 103.75f }, { 100.0f, 103.75f }, 1 },         // more than 3.5: a new layer
	};
	for (const Case &c : cases)
	{
		SyntheticTerrain terrain{ 80, 20 };
		TestWorld world;
		std::vector<WallPieceObject *> pieces;
		for (size_t i = 0; i < c.heights.size(); ++i)
		{
			WallPieceObject *p = new WallPieceObject();
			p->id = (PathfindObjectID)(100 + i);
			const float cx = 100.0f + (float)i * 200.0f;
			p->pos = { cx, 100.0f, 0.0f };
			p->structure = true;
			p->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
			p->geometry.type = PATHFIND_GEOMETRY_SPHERE;
			p->geometry.majorRadius = 1.0f;
			p->walls.walls.push_back(quad(cx - 40.0f, 60.0f, cx + 40.0f, 140.0f, c.heights[i], c.heights[i]));
			world.objects[p->id] = p;
			pieces.push_back(p);
		}
		Pathfinder pf(testConfig(), &world);
		pf.newMap(terrain);
		for (WallPieceObject *p : pieces)
		{
			pf.addObjectToPathfindMap(*p);
		}
		INFO("heights " << c.heights.size() << ", the last " << c.heights.back());
		REQUIRE(pf.wallHeights().size() == c.expect.size());
		for (size_t k = 0; k < c.expect.size(); ++k)
		{
			CHECK(pf.wallHeights()[k] == c.expect[k]);
		}
		const float lastX = 100.0f + (float)(c.heights.size() - 1) * 200.0f;
		CHECK(pf.cellAt((int)(lastX * 0.1f), 10)->getLayer() == (PathfindLayerEnum)((int)LAYER_WALL_FIRST + c.lastLayer));
		for (auto &kv : world.objects)
		{
			delete kv.second;
		}
	}
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
	// lane PATH-REBUILD (the found paths rebuilt from their optimised nodes, RW 0x767B0E): the chain is the optimiser's anchors. RW 0x7661A3's scan ends
	// a leg where the layer changes after its third node (RW 0x766532 .. 0x76653B), so the path is the start, the last ground node at the ramp's foot,
	// the last ramp node at its top and the goal on the wall (the port cut a chord across the ramp before, which the raw chain's cells hid)
	CHECK(layers.size() == 4);
	CHECK(layers.front() == LAYER_GROUND);
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
	// lane PATH-REBUILD: the same four anchors downhill (the wall's last node at the ramp's top, the ramp's last at its foot)
	CHECK(nodeLayers(*down).size() == 4);
	bool sawRamp = false;
	for (PathfindLayerEnum l : nodeLayers(*down))
	{
		sawRamp = sawRamp || l == LAYER_RAMP;
	}
	CHECK(sawRamp);
	CHECK(down->getFirstNode()->getLayer() == LAYER_WALL_FIRST);
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
	// lane WALLSEARCH-1: RW 0x6EA4E7 tests a wall layer only, exactly: a ground unit's test takes the wall top's cell (it is valid there and searches from it
	// without tunnelling), a wall unit is valid on the top and not on a ground cell
	Coord3D onTop{ 200.0f, 100.0f, 60.0f };
	Coord3D offTop{ 200.0f, 160.0f, 0.0f };
	CHECK(w.pf->validMovementPosition(u, groundLoco(), LAYER_GROUND, &onTop));
	CHECK(w.pf->validMovementPosition(v, groundLoco(), LAYER_WALL_FIRST, &onTop));
	CHECK_FALSE(w.pf->validMovementPosition(v, groundLoco(), LAYER_WALL_FIRST, &offTop));
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
	// lane WALL-4: UpdateLayer ends with AdjustLayer (RW 0x6F278D): a ground unit at the terrain inside the wall's cells with a ground node ahead stays on the
	// ground by mode 3, and AdjustLayer puts it on the wall top 60 above it
	LayeredUnit *inside = w.unit(3, 200.0f, 100.0f, 0.0f, LAYER_GROUND);
	CHECK(w.pf->updateLayer(*inside, LAYER_GROUND) == LAYER_WALL_FIRST);
}

TEST_CASE("wall4 layers: Pathfinder::AdjustLayer (RW 0x6F0741): a unit takes the cell's layer only when that surface is more than 10 above it; the surface loss rule")
{
	WallWorld w;
	// below the wall top (60) by more than 10: onto the wall; at its height: the unit keeps its layer (RotWK has no lift for a ground unit at the wall's height)
	LayeredUnit *under = w.unit(1, 200.0f, 100.0f, 0.0f, LAYER_GROUND);
	CHECK(w.pf->adjustLayer(*under, LAYER_GROUND) == LAYER_WALL_FIRST);
	LayeredUnit *level = w.unit(2, 200.0f, 100.0f, 60.0f, LAYER_GROUND);
	CHECK(w.pf->adjustLayer(*level, LAYER_GROUND) == LAYER_GROUND);
	// the ramp's surface at x 125 is 30: from the ground below it onto the ramp; from 30 itself not
	LayeredUnit *foot = w.unit(3, 125.0f, 100.0f, 0.0f, LAYER_GROUND);
	CHECK(w.pf->adjustLayer(*foot, LAYER_GROUND) == LAYER_RAMP);
	LayeredUnit *on = w.unit(4, 125.0f, 100.0f, 30.0f, LAYER_GROUND);
	CHECK(w.pf->adjustLayer(*on, LAYER_GROUND) == LAYER_GROUND);
	// AdjustLayer never puts a unit down onto the ground grid's surface: a ramp unit over the ground beside the ramp, a wall unit over the ramp, keep their layers
	LayeredUnit *off = w.unit(5, 60.0f, 100.0f, 0.0f, LAYER_RAMP);
	CHECK(w.pf->adjustLayer(*off, LAYER_RAMP) == LAYER_RAMP);
	LayeredUnit *down = w.unit(6, 125.0f, 100.0f, 60.0f, LAYER_WALL_FIRST);
	CHECK(w.pf->adjustLayer(*down, LAYER_WALL_FIRST) == LAYER_WALL_FIRST);
	// the cell's own layer: nothing to do
	LayeredUnit *top = w.unit(7, 200.0f, 100.0f, 60.0f, LAYER_WALL_FIRST);
	CHECK(w.pf->adjustLayer(*top, LAYER_WALL_FIRST) == LAYER_WALL_FIRST);
	// a ground unit at the terrain under the ramp's side (cell (12, 8), a type 5 cell of the ramp's layer, off the ramp's triangles): the ramp's plane there is
	// 30 above it, so RotWK's rule puts it on the ramp (no unit stands on a type 5 cell in play; lane WALL-1's settle kept it on the ground)
	LayeredUnit *side = w.unit(9, 125.0f, 85.0f, 0.0f, LAYER_GROUND);
	CHECK(w.pf->adjustLayer(*side, LAYER_GROUND) == LAYER_RAMP);
	// the surface loss rule (INFERENCE S-1750): a ramp unit over a plain ground cell off the ramp takes the ground; a wall unit on its wall, a ramp unit on the
	// ramp and a ground unit keep their layers
	CHECK(w.pf->layerAfterSurfaceLoss(*off) == LAYER_GROUND);
	CHECK(w.pf->layerAfterSurfaceLoss(*top) == LAYER_WALL_FIRST);
	LayeredUnit *rampUnit = w.unit(8, 125.0f, 100.0f, 30.0f, LAYER_RAMP);
	CHECK(w.pf->layerAfterSurfaceLoss(*rampUnit) == LAYER_RAMP);
	// a wall unit over a plain ground cell beside the wall (the wall top gone from under it) takes the ground
	LayeredUnit *lost = w.unit(10, 200.0f, 140.0f, 60.0f, LAYER_WALL_FIRST);
	CHECK(w.pf->layerAfterSurfaceLoss(*lost) == LAYER_GROUND);
}

TEST_CASE("wall4 layers: GetWallHeight (RW 0x6F0B27) takes the layer of the cell under the point; off the ramp's triangles the ramp's plane at the point")
{
	WallWorld w;
	// a wall layer over a ground cell: the terrain; over the wall's cells: its height
	CHECK(w.pf->getLayerHeight(200.0f, 140.0f, LAYER_WALL_FIRST) == 0.0f);
	CHECK(w.pf->getLayerHeight(200.0f, 100.0f, LAYER_WALL_FIRST) == 60.0f);
	// the ramp layer over the wall top's joint (cell 15, off the ramp's surface): the wall's height; over the ramp's surface: the ramp's
	CHECK(w.pf->getLayerHeight(155.0f, 100.0f, LAYER_RAMP) == 60.0f);
	CHECK(w.pf->getLayerHeight(125.0f, 100.0f, LAYER_RAMP) == 30.0f);
	// a wall layer over the ramp's surface (a ramp cell, the point on the ramp): the ramp's
	CHECK(w.pf->getLayerHeight(125.0f, 100.0f, LAYER_WALL_FIRST) == 30.0f);
	// the ramp's side cell (12, 8): the point (125, 85) is off the triangles, the corner (135, 95) on them: the Bridge's plane at (125, 85), the ramp's 30
	// there (the plane rises 60 over x 100 .. 150 and does not change along y)
	REQUIRE(w.cell(12, 8).getLayer() == LAYER_RAMP);
	CHECK_FALSE(w.pf->isPointOnRamp(125.0f, 85.0f));
	CHECK(w.pf->getLayerHeight(125.0f, 85.0f, LAYER_RAMP) == 30.0f);
}

TEST_CASE("wall4 layers: the layer test of IsValidHordeMemberRegularMovement (RW 0x6F1B3E): from the ground or a wall only onto a ramp; RotWK's connect test")
{
	WallWorld w;
	LayeredUnit *u = w.unit(1, 85.0f, 100.0f, 0.0f, LAYER_GROUND);
	auto step = [&](float fx, float fy, float fz, float tx, float ty, float tz) {
		const Coord3D from{ fx, fy, fz }, to{ tx, ty, tz };
		u->pos = from;
		return w.pf->straightStepKeepsLayer(*u, from, to, w.pf->getLayerAt(*u, to));
	};
	// on one layer: valid (the ground, the ramp, the wall top)
	CHECK(step(60.0f, 100.0f, 0.0f, 70.0f, 100.0f, 0.0f));
	CHECK(step(120.0f, 100.0f, 24.0f, 130.0f, 100.0f, 36.0f));
	CHECK(step(180.0f, 100.0f, 60.0f, 190.0f, 100.0f, 60.0f));
	// the ground onto the ramp: valid (the plain cell 8 onto the ramp's cell 10)
	CHECK(step(85.0f, 100.0f, 0.0f, 105.0f, 100.0f, 6.0f));
	// the ground onto the wall top, the wall top onto the ground: refused (only a ramp takes a step off the ground or a wall)
	CHECK_FALSE(step(200.0f, 140.0f, 0.0f, 200.0f, 110.0f, 0.0f));
	CHECK_FALSE(step(200.0f, 100.0f, 60.0f, 200.0f, 125.0f, 60.0f));
	// from the ramp onto the wall top's joint (cell 15 connects to the ramp): valid (a step from the ramp may change its layer)
	CHECK(step(145.0f, 100.0f, 58.0f, 155.0f, 100.0f, 60.0f));
	// RW 0x6F1C49 .. 0x6F1C66: from a cell that connects to the target's layer onto a cell that does not connect back, refused: the ramp's foot (cell 9, the
	// ground connected to the ramp) onto the ramp (cell 10, no connect layer), and the wall's joint (cell 15) down onto the ramp's top (cell 14)
	CHECK_FALSE(step(95.0f, 100.0f, 0.0f, 105.0f, 100.0f, 6.0f));
	CHECK_FALSE(step(155.0f, 100.0f, 60.0f, 145.0f, 100.0f, 54.0f));
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

// review (Greptile, #159): getRamp answers false for a flat RampMesh, so RotWK records no ramp and its ramp cell rule never runs for it; the port's
// Bridge of such a mesh is all zeros, which must not touch the cell at the map's origin
TEST_CASE("wall1 walls: a flat RampMesh is no ramp: a piece whose ramp is flat changes no cell, the one at the map's origin included")
{
	SyntheticTerrain terrain(20, 20);
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	WallPieceObject *piece = new WallPieceObject();
	piece->id = 9;
	piece->pos = { 150.0f, 150.0f, 0.0f }; // its footprint far from the ramp's cells
	piece->structure = true;
	piece->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
	piece->geometry.type = PATHFIND_GEOMETRY_BOX;
	piece->geometry.majorRadius = 5.0f;
	piece->geometry.minorRadius = 5.0f;
	piece->walls.ramps.push_back(quad(0.0f, 0.0f, 30.0f, 20.0f, 0.0f, 0.0f));
	world.objects[piece->id] = piece;
	pf.addObjectToPathfindMap(*piece);
	CHECK(pf.wallPieceCount() == 1);
	for (int x = 0; x < 10; ++x) // the ramp mesh's cells and around them
	{
		for (int y = 0; y < 10; ++y)
		{
			INFO(x << ", " << y);
			const PathfindCell *c = pf.cellAt(x, y);
			CHECK(c->getLayer() == LAYER_GROUND);
			CHECK(c->getConnectLayer() == LAYER_INVALID);
			CHECK_FALSE(c->getBit17());
			CHECK(c->getType() == PathfindCell::CELL_CLEAR);
		}
	}
	pf.removeObjectFromPathfindMap(*piece);
	delete piece;
}

// review (Greptile, #159): a ramp whose Bridge plane is vertical (a RampMesh of no width: its two bands are single points) has no height (RW 0x68044E's
// ray meets no plane): the ramp is left out of the map and S-4840 says so, no height is made up
TEST_CASE("wall1 walls: a RampMesh of no width is left out and reported (S-4840): its Bridge's plane is vertical, so it has no height")
{
	SyntheticTerrain terrain(30, 20);
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	WallPieceObject *piece = new WallPieceObject();
	piece->id = 9;
	piece->pos = { 250.0f, 150.0f, 0.0f }; // its footprint away from the ramp's cells
	piece->structure = true;
	piece->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
	piece->geometry.type = PATHFIND_GEOMETRY_BOX;
	piece->geometry.majorRadius = 5.0f;
	piece->geometry.minorRadius = 5.0f;
	PathfindWallSurface ramp;
	PathfindWallTriangle a;
	a.v[0] = { 100.0f, 100.0f, 0.0f };
	a.v[1] = { 200.0f, 100.0f, 60.0f };
	a.v[2] = { 200.0f, 100.0f, 60.0f };
	ramp.triangles = { a, a };
	ramp.loX = 100.0f;
	ramp.loY = 100.0f;
	ramp.hiX = 200.0f;
	ramp.hiY = 100.0f;
	ramp.height = 60.0f;
	piece->walls.ramps.push_back(ramp);
	world.objects[piece->id] = piece;
	pf.addObjectToPathfindMap(*piece);
	CHECK(pf.wallPieceCount() == 1);
	CHECK(std::find(pf.stops().begin(), pf.stops().end(), std::string(pathstops::kRampNoHeight)) != pf.stops().end());
	for (int x = 5; x < 25; ++x) // the ramp mesh's cells and around them
	{
		for (int y = 5; y < 15; ++y)
		{
			INFO(x << ", " << y);
			const PathfindCell *c = pf.cellAt(x, y);
			CHECK(c->getLayer() == LAYER_GROUND);
			CHECK(c->getConnectLayer() == LAYER_INVALID);
			CHECK_FALSE(c->getBit17());
			CHECK(c->getType() == PathfindCell::CELL_CLEAR);
		}
	}
	pf.removeObjectFromPathfindMap(*piece);
	delete piece;
}

// review (Greptile, #159): a ramp's cells are the ones its Bridge covers (RW 0x9356DF adds and removes through the same cell rule); a two-triangle ramp
// whose ends differ in width has a Bridge of the two ends' mean width, wider than its triangles near the narrow end. Removing the piece takes back every
// cell the Bridge gave the ramp
TEST_CASE("wall1 walls: a removed ramp gives back every cell its Bridge took, where the Bridge is wider than the mesh's triangles too")
{
	SyntheticTerrain terrain(30, 20);
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	WallPieceObject *piece = new WallPieceObject();
	piece->id = 9;
	piece->pos = { 150.0f, 100.0f, 0.0f };
	piece->structure = true;
	piece->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
	piece->geometry.type = PATHFIND_GEOMETRY_BOX;
	piece->geometry.majorRadius = 50.0f;
	piece->geometry.minorRadius = 45.0f;
	// the narrow end at x = 100 (y 95 .. 105, z 0), the wide end at x = 200 (y 55 .. 145, z 60): the Bridge spans y 75 .. 125 along its length
	PathfindWallSurface ramp;
	PathfindWallTriangle a, b;
	a.v[0] = { 100.0f, 95.0f, 0.0f };
	a.v[1] = { 200.0f, 55.0f, 60.0f };
	a.v[2] = { 200.0f, 145.0f, 60.0f };
	b.v[0] = { 100.0f, 95.0f, 0.0f };
	b.v[1] = { 200.0f, 145.0f, 60.0f };
	b.v[2] = { 100.0f, 105.0f, 0.0f };
	ramp.triangles = { a, b };
	ramp.loX = 100.0f;
	ramp.loY = 55.0f;
	ramp.hiX = 200.0f;
	ramp.hiY = 145.0f;
	ramp.height = 60.0f;
	piece->walls.ramps.push_back(ramp);
	world.objects[piece->id] = piece;
	pf.addObjectToPathfindMap(*piece);
	CHECK(pf.wallPieceCount() == 1);
	// cell (10, 7) (x 100 .. 110, y 70 .. 80) lies in the Bridge, outside both triangles: the ramp's side, type 5
	CHECK(pf.cellAt(10, 7)->getLayer() == LAYER_RAMP);
	CHECK(pf.cellAt(10, 7)->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE);
	pf.removeObjectFromPathfindMap(*piece);
	for (int x = 0; x < 30; ++x)
	{
		for (int y = 0; y < 20; ++y)
		{
			INFO(x << ", " << y);
			const PathfindCell *c = pf.cellAt(x, y);
			CHECK(c->getLayer() == LAYER_GROUND);
			CHECK(c->getConnectLayer() == LAYER_INVALID);
			CHECK_FALSE(c->getBit17());
			CHECK(c->getType() == PathfindCell::CELL_CLEAR);
		}
	}
	delete piece;
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
	explicit WallGame(Shared &sh, SimulationProfile profile = SimulationProfile::Faithful) : s(sh), source(*sh.mount->fs), assets(source)
	{
		std::string error;
		static std::vector<MapCacheEntry> cache;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		NewGameMessage m = helmsDeep(s);
		m.game.simulationProfile = profile; // lane WALLSEARCH-1: the Uber profile's search too
		REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
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
// `near`: the point the wall-top cells are taken nearest to (default: `from`, the unit's start)
bool reachableWallTop(Pathfinder &pf, PathfindObject &unit, const PathfindLocomotorInfo &loco, const Coord3D &from, Coord3D &goal, PathfindLayerEnum &layer,
	const Coord3D *near = nullptr)
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
					const Coord3D &o = near ? *near : from;
					const float fx = (float)x * 10.0f + 5.0f - o.x, fy = (float)y * 10.0f + 5.0f - o.y;
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
	// the cells of each layer on Helm's Deep (the counts of this port's classification, S-4840: not a retail dump). fix/ramp-bridge-cells (RotWK's ramp
	// cell RW 0x93527F on getRamp's Bridge): ramp 1908 -> 1756, walls 2042 / 870 / 58 / 1547 / 129 / 440 -> 2019 / 702 / 28 / 1545 / 106 / 431. A cell all
	// of whose corners are on a ramp is the ramp's even over a wall top, a ramp's foot cell stays the ground (connecting to the ramp), and the joints are
	// only at the ramp's two end edges
	// lane WALL-5: every ramp is stamped after a change, newest first (RW 0x935BFF), and a wall top's stamp keeps the layer of a top at least as tall
	// (RW 0x93511E): ramp 1756 -> 1781, 0x12 702 -> 694, 0x14 1545 -> 1528
	CHECK(cellsByLayer[(int)LAYER_RAMP] == 1781);
	CHECK(cellsByLayer[0x11] == 2019);
	CHECK(cellsByLayer[0x12] == 694);
	CHECK(cellsByLayer[0x13] == 28);
	CHECK(cellsByLayer[0x14] == 1528);
	CHECK(cellsByLayer[0x15] == 106);
	CHECK(cellsByLayer[0x16] == 431);
	CHECK(pf.wallHeights().size() == 6);
	int wallCells = 0;
	for (const auto &kv : cellsByLayer)
	{
		wallCells += isWallLayer((PathfindLayerEnum)kv.first) ? kv.second : 0;
	}
	CHECK(wallCells == 2019 + 694 + 28 + 1528 + 106 + 431);
	// lane WALL-5: ClassifyWall's edge pass (RW 0x936686 .. 0x936811): the tops' rims (type 8) and the pinched cells (the ground beside the tops; the counts of
	// this port's classification, S-4840: not a retail dump)
	const PathfindGridStats stats = pf.gridStats();
	MESSAGE("rim cells " << stats.types[PathfindCell::CELL_WALL_EDGE] << ", pinched " << stats.pinched);
	CHECK(stats.types[PathfindCell::CELL_WALL_EDGE] == 765);
	// pin: #211 (footprint passes skip off-ground cells, RW 0x9375F4): Helm's Deep's pinched cells 2989 -> 2979, measured once on the merged tree and at each merge point (QPATH-1, SIEGEFALL-1 and TOPPLE-1 keep it)
	CHECK(stats.pinched == 2979);
	// every wall height lies above the terrain under the fortress (the meshes are at the sections' world z)
	for (float h : pf.wallHeights())
	{
		CHECK(h > 0.0f); // tolerance: a wall top above the map's zero (the meshes are at the sections' world z)
	}
}

TEST_CASE("wall4 layers retail: Helm's Deep's map objects take their layer at placement (RW 0x62E14C): none with an AI on a wall cell at its wall's height is on the ground")
{
	OPENBFME_REQUIRE_START(s);
	WallGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	int onWallCells = 0, groundAtWallHeight = 0;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || o->getContainedBy())
		{
			continue;
		}
		const Coord3D &p = *o->getPosition();
		const PathfindCell *c = pf.cellAt((int)std::floor(p.x / 10.0f), (int)std::floor(p.y / 10.0f));
		if (!c || !isWallLayer((PathfindLayerEnum)c->getLayer()))
		{
			continue;
		}
		++onWallCells;
		if (ai->adapter().getLayer() == LAYER_GROUND && p.z == pf.getLayerHeight(p.x, p.y, (PathfindLayerEnum)c->getLayer()))
		{
			++groundAtWallHeight;
		}
	}
	MESSAGE(onWallCells << " objects with an AI on wall cells, " << groundAtWallHeight << " of them on the ground layer at the wall's height");
	CHECK(onWallCells == 1); // measured on the retail map (an engine pin: the number of the map's AI objects standing on wall cells)
	CHECK(groundAtWallHeight == 0);
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

// lane SELWALL-1, the owner's report: the selection marker of a unit on a Helm's Deep wall was drawn on the ground below it. RotWK leaves out the terrain part of
// a selection decal that stands kSelectionDecalMaxHeight (20) or more above the terrain (RW 0x50CDE4 .. 0x50CDF9) and projects it onto the
// WALK_ON_TOP_OF_WALL objects near it (RW 0x50C876); the decal stands at the drawable's height (RW 0x68DB35). The HUD marks it raised
// (MarkRaisedSelectionDecals with the terrain's height, InGameHud::markRaisedSelectionDecals)
TEST_CASE("selwall1 retail: on Helm's Deep a hero on the Deeping Wall has a raised selection decal at its height; at its start the decal is on the terrain")
{
	OPENBFME_REQUIRE_START(s);
	WallGame g(*s);
	g.frames(2);
	Player *me = g.human();
	REQUIRE(me);
	Pathfinder &pf = g.game->ai().pathfinder();
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", me->getPlayerIndex(), g.startOfSlot0(), 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	g.frames(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	SelectionDecalSettings settings;
	settings.showSelectedUnitMarker = true;
	auto decals = [&] {
		std::vector<SelectionDecal> d = BuildSelectionDecals(g.game->logic(), &g.game->drawables(), me, { hero->getID() }, settings, 7);
		MarkRaisedSelectionDecals(d, g.game->logic()); // the HUD's (InGameHud::markRaisedSelectionDecals)
		return d;
	};
	std::vector<SelectionDecal> d = decals();
	REQUIRE(d.size() == 1);
	CHECK_FALSE(d[0].raised);
	CHECK(d[0].position.z == hero->getPosition()->z);
	// the Deeping Wall: the HelmsDeepSection pieces across the valley (Ai, B, C and the Culvert), not the keep's walls and stairs
	const Object *section = nullptr;
	for (Object *o = g.game->logic().getFirstObject(); o && !section; o = o->getNextObject())
	{
		section = o->getTemplate() && o->getTemplate()->getName() == "HelmsDeepSectionB" ? o : nullptr;
	}
	REQUIRE(section);
	const Coord3D sectionAt = *section->getPosition();
	Coord3D goal;
	PathfindLayerEnum wallLayer = LAYER_INVALID;
	REQUIRE(reachableWallTop(pf, ai->adapter(), ai->locomotorInfo(), *hero->getPosition(), goal, wallLayer, &sectionAt));
	MESSAGE("HelmsDeepSectionB at " << sectionAt.x << ", " << sectionAt.y << "; the hero walks to the reachable wall top nearest it at " << goal.x << ", " << goal.y
		<< " (layer " << (int)wallLayer << ")");
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	for (int f = 0; f < 3000; ++f)
	{
		g.frames(1);
		const Coord3D p = *hero->getPosition();
		const float dx = p.x - goal.x, dy = p.y - goal.y;
		if (dx * dx + dy * dy < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z << " on layer " << (int)ai->adapter().getLayer() << ", the terrain at "
		<< g.game->logic().getGroundHeight(at.x, at.y));
	REQUIRE(isWallLayer(ai->adapter().getLayer()));
	// the wall piece the hero stands on is a Deeping Wall section (the WALK_ON_TOP_OF_WALL object nearest it)
	std::string piece;
	float best = 1e18f;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOfName("WALK_ON_TOP_OF_WALL"))
		{
			const float px = o->getPosition()->x - at.x, py = o->getPosition()->y - at.y;
			if (px * px + py * py < best)
			{
				best = px * px + py * py;
				piece = o->getTemplate()->getName();
			}
		}
	}
	MESSAGE("the hero stands on " << piece << " at " << at.x << ", " << at.y << ", " << at.z << ", the terrain at " << g.game->logic().getGroundHeight(at.x, at.y));
	CHECK(piece.rfind("HelmsDeepSection", 0) == 0);
	d = decals();
	REQUIRE(d.size() == 1);
	CHECK(d[0].position.z == at.z);
	CHECK(d[0].raised);
	CHECK(at.z - g.game->logic().getGroundHeight(at.x, at.y) >= kSelectionDecalMaxHeight); // tolerance: the wall top stands about 23 over the courtyard; raised needs 20 or more
	// lane HERORING-1: the hero's SHADOW_ALPHA_DECAL goes into the shaders' grid as a raised alpha decal (record 0, its entries -1: the terrain leaves it out)
	CHECK(d[0].style == 0x20u);
	MergeDecalGrid grid;
	std::string gridError;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(d, [](const std::string &, MergeDecalAtlasRect &r) { r = { 0, 0, 64, 64 }; return true; }, settings, grid, &gridError), gridError);
	CHECK(grid.decals == 0);
	CHECK(grid.alphaDecals == 1);
	CHECK(SelectionDecalShaderMode(grid) == 4);
	const float *cell = &grid.texels[(size_t)grid.cellOffset * 4];
	REQUIRE(cell[3] == 1.0f);
	CHECK(grid.texels[((size_t)grid.indexOffset + (size_t)cell[2]) * 4 + 3] == -1.0f);
	CHECK(grid.texels[7] == at.z);
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
	// up there: above the terrain on the wall top, or at the top of the stair on its joint cells (the ramp's layer at the wall's height)
	int members = 0, up = 0;
	auto countUp = [&] {
		members = up = 0;
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
	};
	// the walk ends when the move is over (the horde and every member stopped) with every member up, not at a fixed frame: the idle horde then guards its
	// spot (state 16), and the Easy AI's first wave, whose route moves with the game's random draws and the pathfinder, can pass within its guard range
	// and draw it off the wall. A member still on the stair is not stopped, so the strides of the whole walk are watched
	int arrived = -1;
	for (int f = 0; f < 1200 && arrived < 0; ++f)
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
		countUp();
		bool stopped = !ai->isMoving();
		for (Object *o = g.game->logic().getFirstObject(); o && stopped; o = o->getNextObject())
		{
			if (o->getContainedBy() == horde && o->getAIUpdateInterface() && o->getAIUpdateInterface()->isMoving())
			{
				stopped = false;
			}
		}
		if (stopped && members > 0 && up == members)
		{
			arrived = f;
		}
	}
	const Coord3D hp = *horde->getPosition();
	MESSAGE("horde at " << hp.x << ", " << hp.y << " (goal " << goal.x << ", " << goal.y << "): " << up << " of " << members << " members up at frame " << arrived << ", largest step " << maxStep);
	CHECK(arrived >= 0); // tolerance: the frame the move ended varies with the walk; -1 is a move that never ended with every member up
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

// lane WALL-3, the owner's report: a right click on an enemy's Helm's Deep wall only ever attacked it. RotWK's evaluateContextCommand (RW 0x81E60B) attacks only on the
// selection's POSSIBLE / POSSIBLE_AFTER_MOVING (RW 0x81F18E); a selection none of whose weapons damages the wall (WeaponSet RW 0x6C8652: INVALID_SHOT) on a
// WALK_ON_TOP_OF_WALL object that is no WALL_UPGRADE reaches the move (RW 0x81F618 .. 0x81F63A). HelmsDeepSectionAi's armour HelmsDeepWalls takes no HERO damage
// (DEFAULT 0 %) and 25 % of SIEGE: Boromir (BoromirSword, HERO) moves, a Mordor catapult (its rock's warhead has a SIEGE nugget) attacks
TEST_CASE("wall3 walls retail: a right click on an enemy's Helm's Deep wall moves a hero whose weapons do the wall no damage (MSG_DO_MOVETO), a catapult attacks it")
{
	if (!hudtest::haveWorld("wall3 enemy wall"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map wor helms deep", "FactionMen");
	rig.frame(2);
	Object *wall = nullptr;
	for (Object *o = rig.logic().getFirstObject(); o && !wall; o = o->getNextObject())
	{
		wall = o->getTemplate() && o->getTemplate()->getName() == "HelmsDeepSectionAi" ? o : nullptr;
	}
	REQUIRE(wall);
	REQUIRE(wall->getControllingPlayer() == rig.local); // the fortress' walls are Player_1's (the test above)
	// the player is Player_2 (the rig's FactionMordor enemy), who sees the whole map
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	rig.game->players().setLocalPlayer(enemy);
	rig.game->shroud().revealMapForPlayerPermanently(enemy->getPlayerIndex());
	rig.input->commandTranslator().setUseAlternateMouse(true);
	REQUIRE(HudObjects::relationshipToLocal(rig.input->context(), *wall) == ENEMIES);
	const TerrainLogic *terrain = rig.logic().terrain();
	REQUIRE(terrain);
	const Waypoint *startWp = terrain->findWaypointByName("Player_2_Start");
	REQUIRE(startWp);
	const auto evaluateWith = [&](const char *templateName, float dx) {
		Object *unit = rig.make(templateName, startWp->location.x + dx, startWp->location.y, enemy);
		rig.frame(2);
		rig.lookAt(*unit->getPosition());
		rig.frame(1);
		const Coord3D up = *unit->getPosition();
		const ICoord2D px = rig.screenOf({ up.x, up.y, up.z + 10.0f });
		rig.leftClick(px.x, px.y);
		REQUIRE(rig.input->ui().selected() == std::vector<ObjectID>{ unit->getID() });
		rig.frame(1);
		return rig.input->commandTranslator().evaluateContextCommand(wall, wall->getPosition(), EvaluateType::EvaluateOnly);
	};
	CHECK(evaluateWith("GondorBoromir", 0.0f) == MSG_DO_MOVETO);
	CHECK(evaluateWith("MordorCatapult", 120.0f) == MSG_DO_ATTACK_OBJECT);
}

// lane WALL-3, the owner's report: the move hint of a right click on a wall stood on the floor. RotWK's drawMoveHints (RW 0x48EDED) places it with
// TerrainLogic::alignOnTerrain (vslot 0x48, RW 0x6819D9, stickToGround): the height of the layer getLayerForDestination (RW 0x680A75) names at the point,
// + 2.5 (RW 0xBE5AE8) on any layer but the ground; move_hints.gd takes that height (GameWorld::get_align_on_terrain_height), not the order's own z
TEST_CASE("wall3 walls retail: the move hint stands 2.5 above a Helm's Deep wall top (alignOnTerrain RW 0x6819D9) and on the terrain elsewhere")
{
	if (!hudtest::haveWorld("wall3 move hint height"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map wor helms deep", "FactionMen");
	rig.frame(2);
	Pathfinder &pf = rig.game->ai().pathfinder();
	GameLogic &logic = rig.logic();
	const ICoord2D *ext = pf.getExtent();
	Coord3D top{ 0.0f, 0.0f, 0.0f };
	PathfindLayerEnum layer = LAYER_INVALID;
	// the first clear wall-top cell that stands above the terrain (where a wall meets Helm's Deep's cliff, the layer's height is under the ground's)
	for (int x = 0; x <= ext->x && layer == LAYER_INVALID; ++x)
	{
		for (int y = 0; y <= ext->y && layer == LAYER_INVALID; ++y)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (!c || !isWallLayer(c->getLayer()) || c->getType() != PathfindCell::CELL_CLEAR)
			{
				continue;
			}
			const float cx = (float)x * 10.0f + 5.0f, cy = (float)y * 10.0f + 5.0f;
			if (pf.getLayerHeight(cx, cy, c->getLayer()) > logic.getGroundHeight(cx, cy))
			{
				top = { cx, cy, 0.0f };
				layer = c->getLayer();
			}
		}
	}
	REQUIRE(layer != LAYER_INVALID);
	const float ground = logic.getGroundHeight(top.x, top.y);
	const float wall = pf.getLayerHeight(top.x, top.y, layer);
	INFO("wall cell at " << top.x << ", " << top.y << ": ground " << ground << ", wall top " << wall);
	REQUIRE(wall > ground); // tolerance: a precondition (the cell is a raised wall top); the checks below assert exact heights
	// the click's point (the highest layer, TacticalView::screenToTerrain) and an order at the terrain's z both stand on the wall top's cell layer
	CHECK(logic.getAlignOnTerrainHeight(top.x, top.y, wall) == SimMath::addf32(wall, 2.5f));
	CHECK(logic.getAlignOnTerrainHeight(top.x, top.y, ground) == SimMath::addf32(wall, 2.5f));
	// the ground: the terrain's height, no lift
	const Waypoint *startWp = logic.terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(startWp);
	const Coord3D &s = startWp->location;
	REQUIRE(pf.getLayerAt(s.x, s.y, logic.getGroundHeight(s.x, s.y)) == LAYER_GROUND);
	CHECK(logic.getAlignOnTerrainHeight(s.x, s.y, logic.getGroundHeight(s.x, s.y)) == logic.getGroundHeight(s.x, s.y));
}

// lane WALLSEARCH-1: a goal on a wall top or ramp is searched on its own layer (internalFindPath RW 0x6FD2B2 -> RW 0x680A75; BFME2 0x6FBE2E), so the goal's
// validMovementPosition test (RW 0x6FD85D) passes and the search is exact: a long order from across the map reaches the top it was given
namespace
{
// every 97th inner wall-top cell, its count and how many of them a full path from `from` reaches. The goal is the cell's centre; internalFindPath adds 5 for an
// even footprint (the hero's), so the cell searched is the next one up and right, which the "inner" filter keeps on the same top
void wallTopsFrom(Pathfinder &pf, PathfindObject &unit, const PathfindLocomotorInfo &loco, const Coord3D &from, int &tried, int &reached)
{
	tried = reached = 0;
	const ICoord2D *ext = pf.getExtent();
	int k = 0;
	for (int x = 0; x <= ext->x; ++x)
	{
		for (int y = 0; y <= ext->y; ++y)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (!c || !isWallLayer(c->getLayer()) || c->getConnectLayer() != LAYER_INVALID || c->getType() != PathfindCell::CELL_CLEAR)
			{
				continue;
			}
			bool inner = true;
			for (int dx = -1; dx <= 1 && inner; ++dx)
			{
				for (int dy = -1; dy <= 1; ++dy)
				{
					const PathfindCell *n = pf.cellAt(x + dx, y + dy);
					inner = inner && n && n->getLayer() == c->getLayer();
				}
			}
			if (!inner || (k++ % 97) != 0)
			{
				continue;
			}
			++tried;
			Coord3D to{ (float)x * 10.0f + 5.0f, (float)y * 10.0f + 5.0f, pf.getLayerHeight((float)x * 10.0f + 5.0f, (float)y * 10.0f + 5.0f, c->getLayer()) };
			Coord3D f = from;
			bool partial = true;
			Path *path = pf.findPath(&unit, loco, &f, &to, &partial);
			reached += path && !partial && path->getLastNode()->getLayer() == c->getLayer() ? 1 : 0;
			delete path;
		}
	}
}
} // namespace

TEST_CASE("wallsearch1 retail: on Helm's Deep a hero ordered from the enemy's start onto sampled wall tops gets a full path to 32 of 35 (before #220: 19, Uber 5)")
{
	OPENBFME_REQUIRE_START(s);
	for (const SimulationProfile profile : { SimulationProfile::Faithful, SimulationProfile::Uber })
	{
		WallGame g(*s, profile);
		g.frames(2);
		Player *me = g.human();
		REQUIRE(me);
		Pathfinder &pf = g.game->ai().pathfinder();
		REQUIRE(g.game->logic().simulationProfile() == profile);
		const Waypoint *far = g.game->logic().terrain()->findWaypointByName("Player_2_Start");
		REQUIRE(far);
		// review r1: on a clear ground cell next to the start (the first in the smallest ring around it; the waypoint's own cell is an obstacle, which gets no
		// hierarchical corridor, RW 0x6F8601)
		Coord3D at = far->location;
		{
			const int cx = (int)(at.x / 10.0f), cy = (int)(at.y / 10.0f);
			bool found = false;
			for (int r = 0; r < 30 && !found; ++r)
			{
				for (int dy = -r; dy <= r && !found; ++dy)
				{
					for (int dx = -r; dx <= r && !found; ++dx)
					{
						const PathfindCell *c = pf.cellAt(cx + dx, cy + dy);
						if (c && c->getType() == PathfindCell::CELL_CLEAR && c->getLayer() == LAYER_GROUND)
						{
							at = { (float)(cx + dx) * 10.0f + 5.0f, (float)(cy + dy) * 10.0f + 5.0f, 0.0f };
							found = true;
						}
					}
				}
			}
			REQUIRE(found);
		}
		std::string error;
		Object *hero = g.game->createObject("GondorBoromir", me->getPlayerIndex(), at, 0.0f, &error);
		REQUIRE_MESSAGE(hero, error);
		g.frames(2);
		AIUpdateInterface *ai = hero->getAIUpdateInterface();
		REQUIRE(ai);
		int tried = 0, reached = 0;
		wallTopsFrom(pf, ai->adapter(), ai->locomotorInfo(), *hero->getPosition(), tried, reached);
		MESSAGE("wallsearch1: profile " << (int)profile << ", from " << hero->getPosition()->x << ", " << hero->getPosition()->y << ": " << reached << " of "
										<< tried << " wall tops reached by a full path");
		CHECK(tried == 35); // every 97th inner wall-top cell
		// main before lane WALLSEARCH-1 (measured once, from the same start): 18 of the 35 (Faithful) and 5 (Uber, whose stall cut-off ends the approximate
		// searches): the goal searched on the ground layer, approximate. The two left end partial for reasons not traced from this start
		// pin: #220, measured once on the merged tree with #207 (the wall tops' impassable rims, RW 0x936686) and #211: 33 (public main + #220) -> 32 of 35, both profiles; the archive before #220, measured once from the same start: 19 (Faithful) and 5 (Uber)
		CHECK(reached == 32);
	}
}

TEST_CASE("wallsearch1: validMovementPosition tests a wall layer only, exactly (RW 0x6EA4E7): the ramp and the ground take any cell of their grid")
{
	WallWorld w;
	LayeredUnit *u = w.unit(1, 200.0f, 160.0f, 0.0f, LAYER_GROUND);
	const Coord3D ground{ 200.0f, 160.0f, 0.0f }, top{ 200.0f, 100.0f, 60.0f };
	REQUIRE(w.cell(20, 16).getLayer() == LAYER_GROUND);
	REQUIRE(w.cell(20, 10).getLayer() == LAYER_WALL_FIRST);
	CHECK(w.pf->validMovementPosition(u, groundLoco(), LAYER_RAMP, &ground)); // a goal on a ramp polygon over a ground cell: no layer test
	CHECK(w.pf->validMovementPosition(u, groundLoco(), LAYER_RAMP, &top));
	CHECK(w.pf->validMovementPosition(u, groundLoco(), LAYER_GROUND, &top));
	CHECK(w.pf->validMovementPosition(u, groundLoco(), LAYER_WALL_FIRST, &top));
	CHECK_FALSE(w.pf->validMovementPosition(u, groundLoco(), LAYER_WALL_FIRST, &ground));
	CHECK_FALSE(w.pf->validMovementPosition(u, groundLoco(), (PathfindLayerEnum)((int)LAYER_WALL_FIRST + 1), &top)); // another wall's layer
}
