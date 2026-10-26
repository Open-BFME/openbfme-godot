// OpenBFME unit tests, lane WALL-2: the raised layers (RaisedWallMesh walkways, AIPathfindRaised.cpp). GPL-3.0.
//
// A synthetic gatehouse on 60 x 20 cells of flat terrain at z 0: the left gatehouse (piece 50) has a wall top x 100 .. 190, y 90 .. 110 at z 60, a ramp up
// from the ground x 40 .. 100 (z 0 .. 60) and a flat ramp x 185 .. 215 at z 60 toward the walkway; the centre (piece 51) is the walkway x 200 .. 400, y 90 ..
// 110 at z 60 (RaisedWallMesh only: the ground under it, the gate's passage, stays the ground); the right gatehouse (piece 52) mirrors the left (flat ramp x
// 385 .. 415, wall top x 410 .. 500, ramp down x 500 .. 560). The expected cells come from the rules of AIPathfindRaised.cpp, derived by hand.

#include "doctest.h"

#include "PathfindTestUtil.h"

#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIXfer.h"

#include <algorithm>
#include <iterator>
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
	bool canPathThroughGates() const override { return true; } // the template default (RW 0x74004B): bit 17 (wall tops, ramps) does not block it
};

struct GatehouseWorld
{
	SyntheticTerrain terrain{ 60, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	WallPieceObject *left = nullptr, *centre = nullptr, *right = nullptr;
	explicit GatehouseWorld(bool withWalkway = true, const PathfindConfig &config = testConfig())
	{
		auto piece = [&](PathfindObjectID id, float x) {
			WallPieceObject *p = new WallPieceObject();
			p->id = id;
			p->pos = { x, 100.0f, 0.0f };
			p->structure = true;
			p->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
			p->geometry.type = PATHFIND_GEOMETRY_BOX;
			p->geometry.majorRadius = 1.0f; // a footprint of one cell under the piece's centre (the test reads the layers, not the footprint)
			p->geometry.minorRadius = 1.0f;
			world.objects[id] = p;
			return p;
		};
		left = piece(50, 145.0f);
		left->walls.walls.push_back(quad(100.0f, 90.0f, 190.0f, 110.0f, 60.0f, 60.0f));
		left->walls.ramps.push_back(quad(40.0f, 90.0f, 100.0f, 110.0f, 0.0f, 60.0f));
		left->walls.ramps.push_back(quad(185.0f, 90.0f, 215.0f, 110.0f, 60.0f, 60.0f));
		right = piece(52, 455.0f);
		right->walls.walls.push_back(quad(410.0f, 90.0f, 500.0f, 110.0f, 60.0f, 60.0f));
		right->walls.ramps.push_back(quad(500.0f, 90.0f, 560.0f, 110.0f, 60.0f, 0.0f));
		right->walls.ramps.push_back(quad(385.0f, 90.0f, 415.0f, 110.0f, 60.0f, 60.0f));
		if (withWalkway)
		{
			centre = piece(51, 300.0f);
			centre->pos = { 300.0f, 10.0f, 0.0f }; // its one-cell footprint away from the passage
			centre->walls.raised.push_back(quad(200.0f, 90.0f, 400.0f, 110.0f, 60.0f, 60.0f));
		}
		pf = std::make_unique<Pathfinder>(config, &world);
		pf->newMap(terrain);
		pf->addObjectToPathfindMap(*left);
		pf->addObjectToPathfindMap(*right);
		if (centre)
		{
			pf->addObjectToPathfindMap(*centre);
		}
		zones();
	}
	void zones()
	{
		struct None : PathfindRequestHandler
		{
			void doPathfind(PathfindObjectID) override {}
		} none;
		pf->processPathfindQueue(none);
	}
	~GatehouseWorld()
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
} // namespace

TEST_CASE("wall2 raised: a RaisedWallMesh takes raised layer 2 at its height; its cells are CLEAR where a corner is on the walkway, else type 5; the ground under it stays")
{
	GatehouseWorld w;
	REQUIRE(w.pf->raisedLayerCount() == 1);
	REQUIRE(w.pf->raisedLayerInUse(LAYER_TOP));
	CHECK(w.pf->raisedLayerHeight(LAYER_TOP) == 60.0f);
	// the cells: x floor((200 - 0.1) * 0.1) - 1 = 18 .. ceil((400 + 0.1) * 0.1) + 1 = 42 (exclusive), y 7 .. 12
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 18, 8) != nullptr);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 17, 8) == nullptr);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 41, 8) != nullptr);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 42, 8) == nullptr);
	// cell 19 (x 190 .. 200) has its corners at x 200 on the walkway's edge: CLEAR; cell 18 (180 .. 190) has none: type 5
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 19, 10)->getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 18, 10)->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 30, 10)->getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 30, 10)->getLayer() == LAYER_TOP);
	// rows: y 90 .. 110 -> cells 8 .. 11 have corners on it (row 8's corners at y 90, row 11's at 110), row 7 and 12 none... row 12's corners at y 120 are off
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 30, 8)->getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 30, 11)->getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 30, 7)->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 30, 12)->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE);
	// getCell: the raised layer answers its CLEAR cell, the ground grid elsewhere; the ground under the walkway is the ground
	CHECK(w.pf->getCell(LAYER_TOP, 30, 10) == w.pf->raisedCellAt(LAYER_TOP, 30, 10));
	CHECK(w.pf->getCell(LAYER_TOP, 30, 12) == w.pf->cellAt(30, 12));
	CHECK(w.cell(30, 10).getLayer() == LAYER_GROUND);
	CHECK(w.cell(30, 10).getType() == PathfindCell::CELL_CLEAR);
	// the joints: the flat ramp x 185 .. 215 covers cell 21 (x 210 .. 220) in part (corners at 210 on it, 220 off) under a CLEAR walkway cell at the ramp's
	// height: a ramp cell connecting to layer 2, the walkway cell connecting to the ramp; cell 20 is covered whole: a ramp cell without a walkway joint (lane
	// WALL-1's second pass joins it to the wall top beside it, cell 19)
	CHECK(w.cell(21, 10).getLayer() == LAYER_RAMP);
	CHECK(w.cell(21, 10).getConnectLayer() == LAYER_TOP);
	CHECK(w.pf->raisedCellAt(LAYER_TOP, 21, 10)->getConnectLayer() == LAYER_RAMP);
	CHECK(w.cell(20, 10).getLayer() == LAYER_RAMP);
	CHECK(w.cell(20, 10).getConnectLayer() == LAYER_WALL_FIRST);
	CHECK(w.cell(19, 10).getLayer() == LAYER_WALL_FIRST);
	// the heights and the layer at a point: on the walkway at its height layer 2; under it on the ground (60 above: not within 25) the ground
	CHECK(w.pf->getLayerHeight(300.0f, 100.0f, LAYER_TOP) == 60.0f);
	CHECK(w.pf->getLayerAt(300.0f, 100.0f, 60.0f) == LAYER_TOP);
	CHECK(w.pf->getLayerAt(300.0f, 100.0f, 0.0f) == LAYER_GROUND);
	CHECK(w.pf->getHighestLayerHeight(300.0f, 100.0f) == 60.0f);
	CHECK(w.pf->getHighestLayerHeight(300.0f, 150.0f) == 0.0f);
	CHECK(w.pf->stops().size() > 0); // tolerance: the stop list holds the S-4856 line (checked below), not a count
	bool raisedStop = false;
	for (const std::string &s : w.pf->stops())
	{
		raisedStop = raisedStop || s.rfind("S-4856 raised layers", 0) == 0;
	}
	CHECK(raisedStop);
}

TEST_CASE("wall2 raised: a unit on the left wall top walks to the right one over the walkway (wall, ramp, layer 2, ramp, wall); one below passes under it on the ground")
{
	GatehouseWorld w;
	LayeredUnit *u = w.unit(1, 150.0f, 100.0f, 60.0f, LAYER_WALL_FIRST);
	Coord3D from = u->pos, to{ 455.0f, 105.0f, 60.0f };
	bool partial = true;
	Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	bool sawRaised = false, sawGround = false;
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		sawRaised = sawRaised || n->getLayer() == LAYER_TOP;
		sawGround = sawGround || n->getLayer() == LAYER_GROUND;
		if (n->getLayer() == LAYER_TOP)
		{
			CHECK(n->getPosition()->z == 60.0f);
		}
	}
	CHECK(sawRaised);
	CHECK_FALSE(sawGround); // the walkway, not down the ramps and through the passage
	CHECK(p->getLastNode()->getLayer() == LAYER_WALL_FIRST);
	delete p;
	// the same zones: the walkway's zone joins the two ramp joints'
	const PathfindMovement mv = Pathfinder::makeMovement(u, groundLoco());
	const zoneStorageType zl = w.pf->zoneManager().getEffectiveZone(mv, false, w.cell(15, 10).getZone());
	const zoneStorageType zr = w.pf->zoneManager().getEffectiveZone(mv, false, w.cell(45, 10).getZone());
	CHECK(zl == zr);
	// a ground unit crossing the passage under the walkway stays on the ground
	LayeredUnit *g = w.unit(2, 300.0f, 30.0f, 0.0f, LAYER_GROUND);
	Coord3D gf = g->pos, gt{ 300.0f, 170.0f, 0.0f };
	Path *under = w.pf->findPath(g, groundLoco(), &gf, &gt, &partial);
	REQUIRE(under != nullptr);
	CHECK_FALSE(partial);
	for (const PathNode *n = under->getFirstNode(); n; n = n->getNext())
	{
		CHECK(n->getLayer() == LAYER_GROUND);
	}
	delete under;
	// a unit's layer: on the walkway at a node there it keeps layer 2; walking off the walkway's cells onto the right ramp it takes the ramp
	LayeredUnit *k = w.unit(3, 300.0f, 100.0f, 60.0f, LAYER_TOP);
	CHECK(w.pf->updateLayer(*k, LAYER_TOP) == LAYER_TOP);
	CHECK(w.pf->settleLayer(*k) == LAYER_TOP);
	k->pos = { 450.0f, 100.0f, 60.0f };
	CHECK(w.pf->settleLayer(*k) == LAYER_WALL_FIRST);
}

TEST_CASE("wall2 raised: without the walkway the wall tops meet only through the ground; removing the walkway frees layer 2 and its joints")
{
	{
		GatehouseWorld w(false);
		CHECK(w.pf->raisedLayerCount() == 0);
		LayeredUnit *u = w.unit(1, 150.0f, 100.0f, 60.0f, LAYER_WALL_FIRST);
		Coord3D from = u->pos, to{ 455.0f, 105.0f, 60.0f };
		bool partial = true;
		Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
		REQUIRE(p != nullptr);
		CHECK_FALSE(partial);
		bool sawGround = false;
		for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
		{
			sawGround = sawGround || n->getLayer() == LAYER_GROUND;
			CHECK(n->getLayer() != LAYER_TOP);
		}
		CHECK(sawGround);
		delete p;
	}
	GatehouseWorld w;
	w.pf->removeObjectFromPathfindMap(*w.centre);
	w.zones();
	CHECK(w.pf->raisedLayerCount() == 0);
	CHECK_FALSE(w.pf->raisedLayerInUse(LAYER_TOP));
	CHECK(w.pf->getCell(LAYER_TOP, 30, 10) == w.pf->cellAt(30, 10));
	CHECK(w.cell(21, 10).getConnectLayer() == LAYER_INVALID);
	CHECK(w.pf->getLayerAt(300.0f, 100.0f, 60.0f) == LAYER_GROUND);
}

namespace
{
class NoGatesUnit : public TestObject
{
public:
	bool canPathThroughGates() const override { return false; } // CanPathThroughGates = No (the mumakil, Grond, the siege towers)
};
} // namespace

TEST_CASE("wall2 raised review of #63: a snapshot whose ground and raised cell infos each fit the pool but not together is refused by name, the "
		  "loading pathfinder as it was")
{
	PathfindConfig config = testConfig();
	config.cellInfoPoolSize = 1000; // the pool both pathfinders are configured with (the snapshot's configuration hash must match)
	GatehouseWorld saved(true, config), loaded(true, config);
	saved.pf->setUberLimits(true); // the saving game's pool grows past its 1000 (an Uber game); the snapshot is made Faithful below
	PathfindCellInfoPool &pool = saved.pf->pool();
	int raised = 0, ground = 0;
	// a cell info on every passable walkway cell (x 18 .. 41, y 7 .. 12: the first test), then on ground cells until the ground holds exactly 1000
	for (int x = 18; x < 42; ++x)
	{
		for (int y = 7; y < 13; ++y)
		{
			PathfindCell *c = saved.pf->getCell(LAYER_TOP, x, y);
			if (c && c == saved.pf->raisedCellAt(LAYER_TOP, x, y))
			{
				if (!c->hasInfo())
				{
					REQUIRE(c->allocateInfo(pool, ICoord2D{ x, y }));
				}
				++raised;
			}
		}
	}
	for (int x = 0; x < 60; ++x)
	{
		for (int y = 0; y < 20; ++y)
		{
			ground += saved.pf->cellAt(x, y)->hasInfo() ? 1 : 0;
		}
	}
	for (int x = 0; x < 60 && ground < 1000; ++x)
	{
		for (int y = 0; y < 20 && ground < 1000; ++y)
		{
			PathfindCell *c = saved.pf->getCell(LAYER_GROUND, x, y);
			if (!c->hasInfo())
			{
				REQUIRE(c->allocateInfo(pool, ICoord2D{ x, y }));
				++ground;
			}
		}
	}
	REQUIRE(ground == 1000);
	REQUIRE(raised > 0); // tolerance: a precondition (the walkway has passable cells; the first test pins them)
	std::vector<std::uint8_t> bytes;
	{
		AIXfer::StringTableScope strings;
		XferSave save;
		saved.pf->xfer(save);
		bytes = save.data();
	}
	// the load's limit is the snapshot's profile (review of #62, Greptile P1): an Uber snapshot loads into a pool that grows. Cleared, its Uber flag
	// (followed by the two ring lengths, 16,384 each) makes it a Faithful snapshot, whose pool of 1000 holds the two counts only apart
	const std::uint8_t uberTail[] = { 1, 0x00, 0x40, 0, 0, 0x00, 0x40, 0, 0 };
	auto flag = std::search(bytes.begin(), bytes.end(), std::begin(uberTail), std::end(uberTail));
	REQUIRE(flag != bytes.end());
	REQUIRE(std::search(flag + 1, bytes.end(), std::begin(uberTail), std::end(uberTail)) == bytes.end());
	*flag = 0;
	StateHasher before;
	loaded.pf->crc(before);
	std::string error;
	try
	{
		AIXfer::StringTableScope strings;
		XferLoad load(bytes);
		loaded.pf->xfer(load);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(error == "pathfinder: the snapshot holds 1000 ground and " + std::to_string(raised) + " raised cell infos, more than the pool of 1000");
	StateHasher after;
	loaded.pf->crc(after);
	CHECK(after.value() == before.value());
}

TEST_CASE("wall2 raised: wall tops and ramps carry bit 17 (RW 0x935051 / 0x93527F): a unit with CanPathThroughGates = No cannot path up the ramp onto the wall")
{
	GatehouseWorld w;
	CHECK(w.cell(15, 10).getBit17());       // the left wall top
	CHECK(w.cell(7, 10).getBit17());        // the left ramp
	CHECK_FALSE(w.cell(30, 10).getBit17()); // the passage under the walkway
	NoGatesUnit *u = new NoGatesUnit();
	u->id = 9;
	u->pos = { 15.0f, 105.0f, 0.0f };
	u->mobile = true;
	u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
	u->geometry.majorRadius = 5.0f;
	w.world.objects[u->id] = u;
	Coord3D from = u->pos, to{ 150.0f, 105.0f, 60.0f };
	bool partial = false;
	Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	if (p)
	{
		CHECK(partial);
		for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
		{
			CHECK(n->getLayer() == LAYER_GROUND);
		}
	}
	delete p;
}

// lane WALL-2, the melee searches on layers: RW 0x7463E8's mark (RW 0x74661A) on a plateau no ramp reaches, and RW 0x6F37B3's layer and WALL_UPGRADE retry
// on an isolated walkway (a RaisedWallMesh with no ramps: its zone is its own)
namespace
{
class ReachRange : public PathfindAttackRange
{
public:
	ReachRange(Coord3D centre, float reach) : m_centre(centre), m_reach(reach) {}
	bool inRangeFrom(const Coord3D &from, float extra) const override { return inRangeFromTo(from, m_centre, extra); }
	bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const override
	{
		const float dx = from.x - victimPos.x, dy = from.y - victimPos.y;
		return dx * dx + dy * dy + extra * extra <= m_reach * m_reach;
	}

private:
	Coord3D m_centre;
	float m_reach;
};

struct IslandWorld
{
	SyntheticTerrain terrain{ 60, 100 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	WallPieceObject *piece = nullptr;
	// raised: a wide walkway x 200 .. 400, y 40 .. 160 at z 60 (wider than the first search budget reaches from its middle); else the wall top x 100 .. 300, y 50 .. 250 at z 60; neither has a ramp
	explicit IslandWorld(bool raised)
	{
		piece = new WallPieceObject();
		piece->id = 50;
		piece->pos = { 550.0f, 950.0f, 0.0f }; // its one-cell footprint in a corner
		piece->structure = true;
		piece->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
		piece->geometry.type = PATHFIND_GEOMETRY_BOX;
		piece->geometry.majorRadius = 1.0f;
		piece->geometry.minorRadius = 1.0f;
		if (raised)
		{
			piece->walls.raised.push_back(quad(200.0f, 40.0f, 400.0f, 160.0f, 60.0f, 60.0f));
		}
		else
		{
			piece->walls.walls.push_back(quad(100.0f, 50.0f, 300.0f, 250.0f, 60.0f, 60.0f));
		}
		world.objects[piece->id] = piece;
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		pf->addObjectToPathfindMap(*piece);
		struct None : PathfindRequestHandler
		{
			void doPathfind(PathfindObjectID) override {}
		} none;
		pf->processPathfindQueue(none);
	}
	~IslandWorld()
	{
		for (auto &kv : world.objects)
		{
			delete kv.second;
		}
	}
	template <class T> T *add(PathfindObjectID id, float x, float y, float z)
	{
		T *u = new T();
		u->id = id;
		u->pos = { x, y, z };
		u->mobile = true;
		u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		u->geometry.majorRadius = 5.0f;
		world.objects[id] = u;
		return u;
	}
};
} // namespace

TEST_CASE("wall2 raised: the melee back-off marks a refused step on a wall top (RW 0x74661A) and stands back CastleSiegeStandBackDistance more")
{
	IslandWorld w(false);
	LayeredUnit *u = w.add<LayeredUnit>(1, 150.0f, 350.0f, 0.0f);
	REQUIRE(w.pf->getLayerAt(150.0f, 100.0f, 60.0f) == LAYER_WALL_FIRST);
	// a destination on the plateau: the steps back toward the unit (20 each) are refused up to y 240, all on the wall top (marked); y 260 is reached,
	// then the mark moves the point CastleSiegeStandBackDistance (500) plus the unit's radius further back
	Coord3D dest{ 150.0f, 100.0f, 60.0f };
	REQUIRE(w.pf->meleeBackOff(*u, groundLoco(), 20.0f, &dest));
	INFO("backed off to " << dest.x << ", " << dest.y);
	CHECK(dest.x == 150.0f);
	CHECK(dest.y == 765.0f); // 260 + 500 + the radius 5
}

TEST_CASE("wall2 raised: the melee engagement search runs on the destination's layer; a WALL_UPGRADE target on a walkway no unit reaches is retried on the ground (RW 0x6F37B3)")
{
	IslandWorld w(true);
	REQUIRE(w.pf->getLayerAt(300.0f, 100.0f, 60.0f) == LAYER_TOP);
	TestObject *unit = w.add<TestObject>(1, 500.0f, 100.0f, 0.0f); // east of the walkway: the step toward it (+x) keeps the destination on it
	// a WALL_UPGRADE target on the walkway: the walkway's cells are another zone and no ground cell is one of layer 2's: the ground retry finds the
	// passage under the walkway, in reach of the target
	TestObject *upgrade = w.add<TestObject>(2, 300.0f, 100.0f, 60.0f);
	upgrade->mobile = false;
	upgrade->team = 1;
	upgrade->kinds.insert(PK_WALL_UPGRADE);
	const ReachRange reach(upgrade->pos, 40.0f);
	Coord3D dest = upgrade->pos;
	REQUIRE(w.pf->findMeleeEngagementLocation(*unit, groundLoco(), *upgrade, reach, &dest));
	MESSAGE("engagement at " << dest.x << ", " << dest.y << ", " << dest.z);
	CHECK(reach.inRangeFrom(dest, 0.0f));
	CHECK(w.pf->getLayerAt(dest.x, dest.y, dest.z) == LAYER_GROUND);
	CHECK(dest.x == 305.0f); // the cell under the destination cell (31, 10), on the ground
	CHECK(dest.y == 105.0f);
	// the same target as no WALL_UPGRADE: no retry, nothing on the walkway is in the unit's zone
	TestObject *plain = w.add<TestObject>(3, 300.0f, 100.0f, 60.0f);
	plain->team = 1;
	Coord3D none = plain->pos;
	const bool plainFound = w.pf->findMeleeEngagementLocation(*unit, groundLoco(), *plain, ReachRange(plain->pos, 40.0f), &none);
	MESSAGE("the plain target: " << plainFound << " at " << none.x << ", " << none.y << ", " << none.z);
	CHECK_FALSE(plainFound);
	// a unit on the walkway reaches the plain target there: the search runs on layer 2 and stays on it
	LayeredUnit *top = w.add<LayeredUnit>(4, 250.0f, 100.0f, 60.0f);
	top->layer = LAYER_TOP;
	Coord3D onTop = plain->pos;
	REQUIRE(w.pf->findMeleeEngagementLocation(*top, groundLoco(), *plain, ReachRange(plain->pos, 40.0f), &onTop));
	INFO("on the walkway at " << onTop.x << ", " << onTop.y << ", " << onTop.z);
	CHECK(w.pf->getLayerAt(onTop.x, onTop.y, onTop.z) == LAYER_TOP);
	CHECK(onTop.z == 60.0f);
}

namespace
{
// a wall piece with one raised walkway quad at height z, its one-cell footprint at (fx, fy) away from the passage
WallPieceObject *walkwayPiece(GatehouseWorld &w, PathfindObjectID id, float fx, float fy, float x0, float x1, float z)
{
	WallPieceObject *p = new WallPieceObject();
	p->id = id;
	p->pos = { fx, fy, 0.0f };
	p->structure = true;
	p->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
	p->geometry.type = PATHFIND_GEOMETRY_BOX;
	p->geometry.majorRadius = 1.0f;
	p->geometry.minorRadius = 1.0f;
	p->walls.raised.push_back(quad(x0, 90.0f, x1, 110.0f, z, z));
	w.world.objects[id] = p;
	w.pf->addObjectToPathfindMap(*p);
	w.zones();
	return p;
}
} // namespace

// review (#63): a search toward a point on a raised walkway aims at the walkway's cell (TerrainLogic::getLayerForDestination RW 0x680A75), not the ground
// under it
TEST_CASE("wall2 raised: a unit on the walkway ordered to another point of it stays on the walkway")
{
	GatehouseWorld w;
	LayeredUnit *u = w.unit(1, 250.0f, 100.0f, 60.0f, LAYER_TOP);
	Coord3D from = u->pos, to{ 350.0f, 100.0f, 60.0f };
	CHECK(w.pf->searchDestinationLayer(to) == LAYER_TOP);
	CHECK(w.pf->searchDestinationLayer(Coord3D{ 150.0f, 100.0f, 60.0f }) == LAYER_GROUND); // a wall top: a layer of the ground grid's cells
	bool partial = true;
	Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		CHECK(n->getLayer() == LAYER_TOP);
	}
	delete p;
}

// review (#63): a change that rebuilds a raised layer re-stamps every ramp: the far end's ramp keeps its joint with the walkway
TEST_CASE("wall2 raised: a piece added at the walkway's left end (its layer rebuilt) leaves the right ramp's joint: the walkway still reaches the right wall")
{
	GatehouseWorld w;
	walkwayPiece(w, 53, 210.0f, 10.0f, 200.0f, 220.0f, 60.0f); // the same height: the walkway's layer, made again
	LayeredUnit *u = w.unit(1, 300.0f, 100.0f, 60.0f, LAYER_TOP);
	Coord3D from = u->pos, to{ 455.0f, 105.0f, 60.0f };
	bool partial = true;
	Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	bool sawGround = false;
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		sawGround = sawGround || n->getLayer() == LAYER_GROUND;
	}
	CHECK_FALSE(sawGround);
	CHECK(p->getLastNode()->getLayer() == LAYER_WALL_FIRST);
	delete p;
}

// review (#63): the highest layer for a click is the highest surface, not the first raised layer in index order
TEST_CASE("wall2 raised: under a second walkway at 90 over the first (60) the highest surface is 90; beside it 60")
{
	GatehouseWorld w;
	walkwayPiece(w, 54, 300.0f, 30.0f, 250.0f, 350.0f, 90.0f);
	CHECK(w.pf->getHighestLayerHeight(300.0f, 100.0f) == 90.0f);
	CHECK(w.pf->getHighestLayerHeight(220.0f, 100.0f) == 60.0f);
}

// review (#63): a goal slot on a raised walkway reserves the walkway's cells (Pathfinder::registerSlot / releaseSlot take the slot's layer)
TEST_CASE("wall2 raised: a goal reserved on the walkway marks the walkway's cells, not the ground under them; removing it clears them")
{
	GatehouseWorld w;
	LayeredUnit *u = w.unit(1, 250.0f, 100.0f, 60.0f, LAYER_TOP);
	Coord3D goal{ 300.0f, 100.0f, 60.0f };
	w.pf->updateGoal(*u, &goal, LAYER_TOP);
	auto reserved = [&](const PathfindCell *c) {
		for (const PathfindOccupant *n = c ? c->occupants(OCC_GROUND_GOAL) : nullptr; n; n = n->next)
		{
			if (n->owner == u->id)
			{
				return true;
			}
		}
		return false;
	};
	const PathfindCell *top = w.pf->getCell(LAYER_TOP, 30, 10);
	REQUIRE(top != nullptr);
	REQUIRE(top->getLayer() == LAYER_TOP);
	CHECK(reserved(top));
	CHECK_FALSE(reserved(w.pf->cellAt(30, 10)));
	w.pf->removeGoal(*u);
	CHECK_FALSE(reserved(top));
}

// review (#63): the quick path test before the search takes the ends' layers too: a walkway point above a ground obstacle is a destination
TEST_CASE("wall2 raised: a walkway point above a ground obstacle is a destination: the quick path test and the search take the walkway's cell")
{
	GatehouseWorld w;
	TestObject *box = new TestObject();
	box->id = 60;
	box->pos = { 300.0f, 100.0f, 0.0f };
	box->structure = true;
	box->geometry.type = PATHFIND_GEOMETRY_BOX;
	box->geometry.majorRadius = 20.0f;
	box->geometry.minorRadius = 10.0f;
	w.world.objects[box->id] = box;
	w.pf->addObjectToPathfindMap(*box);
	w.zones();
	REQUIRE(w.pf->cellAt(30, 10)->getType() == PathfindCell::CELL_OBSTACLE);
	LayeredUnit *u = w.unit(1, 250.0f, 100.0f, 60.0f, LAYER_TOP);
	Coord3D from = u->pos, to{ 300.0f, 100.0f, 60.0f };
	CHECK(w.pf->clientSafeQuickDoesPathExist(groundLoco(), &from, &to, u));
	bool partial = true;
	Path *p = w.pf->findPath(u, groundLoco(), &from, &to, &partial);
	REQUIRE(p != nullptr);
	CHECK_FALSE(partial);
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		CHECK(n->getLayer() == LAYER_TOP);
	}
	delete p;
}

// review (#64): a rebuilt raised layer registers its units' reservations again on the new cells
TEST_CASE("wall2 raised: a goal reserved on the walkway stays reserved when a change rebuilds the walkway's layer")
{
	GatehouseWorld w;
	LayeredUnit *u = w.unit(1, 250.0f, 100.0f, 60.0f, LAYER_TOP);
	Coord3D goal{ 300.0f, 100.0f, 60.0f };
	w.pf->updateGoal(*u, &goal, LAYER_TOP);
	walkwayPiece(w, 53, 210.0f, 10.0f, 200.0f, 220.0f, 60.0f); // the same height: layer 2 made again
	bool reserved = false;
	const PathfindCell *top = w.pf->getCell(LAYER_TOP, 30, 10);
	REQUIRE(top != nullptr);
	REQUIRE(top->getLayer() == LAYER_TOP);
	for (const PathfindOccupant *n = top->occupants(OCC_GROUND_GOAL); n; n = n->next)
	{
		reserved = reserved || n->owner == u->id;
	}
	CHECK(reserved);
}

// review (#63): a goal whose footprint reaches over the walkway's edge holds ground cells too; a rebuild of the walkway leaves one node of it there, and
// removing the goal clears it
TEST_CASE("wall2 raised: a walkway goal whose footprint reaches the ground beside the walkway keeps one node there through a rebuild; removing it clears it")
{
	GatehouseWorld w;
	LayeredUnit *u = w.unit(1, 250.0f, 105.0f, 60.0f, LAYER_TOP);
	u->geometry.majorRadius = 25.0f; // a footprint of 5 x 5 cells: rows 8 .. 12 around the goal's row 10, row 12 beside the walkway (y 90 .. 110)
	Coord3D goal{ 300.0f, 105.0f, 60.0f };
	w.pf->updateGoal(*u, &goal, LAYER_TOP);
	auto nodes = [&](const PathfindCell *c) {
		int n = 0;
		for (const PathfindOccupant *o = c ? c->occupants(OCC_GROUND_GOAL) : nullptr; o; o = o->next)
		{
			n += o->owner == u->id ? 1 : 0;
		}
		return n;
	};
	const PathfindCell *ground = w.pf->cellAt(30, 12);
	REQUIRE(ground != nullptr);
	REQUIRE(w.pf->getCell(LAYER_TOP, 30, 12) == ground); // no walkway cell there: the slot's layer reads the ground grid
	REQUIRE(nodes(ground) == 1);
	walkwayPiece(w, 53, 210.0f, 10.0f, 200.0f, 220.0f, 60.0f); // layer 2 made again
	CHECK(nodes(ground) == 1);
	CHECK(nodes(w.pf->getCell(LAYER_TOP, 30, 10)) == 1);
	w.pf->removeGoal(*u);
	CHECK(nodes(ground) == 0);
}

// review (#64): a group's formation search (findGroundPath) takes the destination's layer at its height: a walkway point is not the ground under it. The
// formation search does not reach a walkway, so it makes no path, and the group's march is refused (AIGroup: the generic path, each unit its own move,
// whose search reaches the walkway) instead of marching to the ground below
TEST_CASE("wall2 raised: a group's formation search to a walkway point does not end on the ground under it")
{
	GatehouseWorld w;
	LayeredUnit *u = w.unit(1, 150.0f, 100.0f, 60.0f, LAYER_WALL_FIRST);
	Coord3D from = u->pos, to{ 300.0f, 100.0f, 60.0f };
	Path *p = w.pf->findGroundPath(u, &from, &to, 1, 0.0f);
	CHECK(p == nullptr);
	delete p;
	bool partial = true;
	Path *own = w.pf->findPath(u, groundLoco(), &from, &to, &partial); // a unit's own move
	REQUIRE(own != nullptr);
	CHECK_FALSE(partial);
	CHECK(own->getLastNode()->getLayer() == LAYER_TOP);
	delete own;
}

TEST_CASE("wall2 raised: S-4858, the attack search's closest-cell fallback for a unit off the ground layer without a corridor, is in Pathfinder::allStops()")
{
	// lane WALL-2 with ATTACKPATH-1 (AIPathfindAttack.cpp): RotWK ranks the fallback cell only through its corridor, which the port builds on the ground grid
	// only; the distance-ranked fallback off the ground is OpenBFME's own (Grond put on a Minas Tirith wall top reaches the gate through it)
	const std::vector<std::string> all = Pathfinder::allStops();
	CHECK(std::find(all.begin(), all.end(), std::string("S-4858 findAttackPath (RW 0x6FC18E) keeps its closest-cell fallback only with the corridor of RW 0x6F9829 (its RW 0x93AD93 ranking runs only"
			  " when the corridor was found); the port's corridor runs on the ground grid only (S-162, S-4856), so a unit on a wall top, a raised walkway o"
			  "r a ramp often has none and would get no path at all: such a search keeps the cell closest to the goal cell by its squared cell distance ins"
			  "tead (OpenBFME's own, pending the corridor's raised branch)")) != all.end());
}
