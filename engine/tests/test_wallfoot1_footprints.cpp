// OpenBFME tests of lane WALLFOOT-1: a structure's footprint writes only ground-layer cells (RW Pathfinder::classifyObjectFootprint 0x936B7D: the box
// writes a wall top or ramp cell for a gate or wall object only, the cylinder never, and the removal's type 5 step and the pinch passes A / B / C skip
// them), and a gate's removal keeps the box's bit 17 (CanPathThroughGates, RW 0x6E861E). The same in BFME2 1.06 (0x930212). GPL-3.0.

#include "doctest.h"

#include "PathfindTestUtil.h"
#include "Siege1TestUtil.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/GateModules.h"

#include <vector>

using namespace pathtest;
using namespace siege1test;

namespace
{
struct World
{
	SyntheticTerrain terrain;
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	std::vector<std::unique_ptr<TestObject>> objects;
	World(int w, int h) : terrain(w, h) {}
	void build()
	{
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
	}
	PathfindCell &cell(int x, int y) { return *pf->getCell(LAYER_GROUND, x, y); }
	// a wall top cell as ClassifyWall stamps it (AIPathfindWalls.cpp stampWallSurface): the wall layer, CLEAR, bit 17
	void wallTop(int x, int y)
	{
		PathfindCell &c = cell(x, y);
		c.setType(PathfindCell::CELL_CLEAR);
		c.setLayer(LAYER_WALL_FIRST);
		c.setBit17(true);
	}
	// a structure of `kind` (none: a plain building) with a box (`major` x `minor` half sizes) or, `major` alone, a cylinder footprint at (x, y)
	TestObject &structure(PathfindObjectID id, float x, float y, float major, float minor, PathfindGeometryType type = PATHFIND_GEOMETRY_BOX)
	{
		objects.push_back(std::make_unique<TestObject>());
		TestObject &o = *objects.back();
		o.id = id;
		o.pos.x = x;
		o.pos.y = y;
		o.pos.z = 0.0f;
		o.structure = true;
		o.geometry.type = type;
		o.geometry.majorRadius = major;
		o.geometry.minorRadius = minor;
		world.objects[id] = &o;
		return o;
	}
};
} // namespace

TEST_CASE("wallfoot1: a building's box beside a wall top stamps the ground cells only; the wall top under it and beside it keeps its type, layer and bit 17")
{
	World w(40, 30);
	w.build();
	// a wall top one cell wide (x 12, y 5 .. 24) between a cliff (x 11) and the building (cells x 12 .. 17, y 12 .. 18): the old code stamped the
	// building's obstacle on it and its pinch passes made the rest beside it type 5
	for (int y = 5; y < 25; ++y)
	{
		w.wallTop(12, y);
		w.cell(11, y).setType(PathfindCell::CELL_CLIFF);
	}
	// the box (half sizes 30 at (150, 150)): steps of 5 from its turned top-left (120, 180), cell floor((v + 0.5) * 0.1): x 12 .. 17, y 18 down to 12
	TestObject &b = w.structure(7, 150.0f, 150.0f, 30.0f, 30.0f);
	w.pf->addObjectToPathfindMap(b);
	for (int y = 12; y <= 18; ++y)
	{
		// the wall top under the box: no obstacle (RW 0x93707C: not a gate or wall object)
		CHECK(w.cell(12, y).getType() == PathfindCell::CELL_CLEAR);
		CHECK(w.cell(12, y).getLayer() == LAYER_WALL_FIRST);
		CHECK(w.cell(12, y).getBit17());
		CHECK_FALSE(w.cell(12, y).isObstaclePresent(7));
		for (int x = 13; x <= 17; ++x)
		{
			CHECK(w.cell(x, y).getType() == PathfindCell::CELL_OBSTACLE); // the ground under the box
			CHECK(w.cell(x, y).isObstaclePresent(7));
		}
	}
	// beside the box the wall top's cells (fewer than 4 clear neighbours, an OBSTACLE cross neighbour: the old passes A and C pinched them) are neither
	// pinched nor made type 5 (RW 0x9375F4 / 0x937717 / 0x9377A3)
	for (int y = 5; y < 25; ++y)
	{
		CHECK(w.cell(12, y).getType() == PathfindCell::CELL_CLEAR);
		CHECK_FALSE(w.cell(12, y).getPinched());
	}
	// the ground next to the box is pinched as before (pass C: a cross neighbour of type 4)
	CHECK(w.cell(18, 14).getPinched());
	w.pf->removeObjectFromPathfindMap(b);
	for (int y = 5; y < 25; ++y)
	{
		CHECK(w.cell(12, y).getType() == PathfindCell::CELL_CLEAR);
		CHECK(w.cell(12, y).getLayer() == LAYER_WALL_FIRST);
		CHECK(w.cell(12, y).getBit17());
	}
	CHECK(w.cell(15, 15).getType() == PathfindCell::CELL_CLEAR);
}

TEST_CASE("wallfoot1: an off-ground cell keeps its pinched bit and a ramp's blocked side stays type 5 when a building beside it goes (RW 0x937587 / 0x9375F4)")
{
	World w(40, 30);
	w.build();
	// a ramp's side (LAYER_RAMP, type 5, as AIPathfindWalls.cpp stampRampSurface leaves it) and a pinched wall top cell inside the post region
	PathfindCell &side = w.cell(18, 15);
	side.setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
	side.setLayer(LAYER_RAMP);
	w.wallTop(19, 15);
	w.cell(19, 15).setPinched(true);
	TestObject &b = w.structure(7, 150.0f, 150.0f, 30.0f, 30.0f);
	w.pf->addObjectToPathfindMap(b);
	CHECK(w.cell(19, 15).getPinched());
	w.pf->removeObjectFromPathfindMap(b);
	CHECK(side.getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE);
	CHECK(side.getLayer() == LAYER_RAMP);
	CHECK(w.cell(19, 15).getPinched());
	CHECK(w.cell(19, 15).getType() == PathfindCell::CELL_CLEAR);
}

TEST_CASE("wallfoot1: a gate's box writes a wall top cell (RW 0x93706D's gate / wall rule), its cylinder does not (RW 0x93737A)")
{
	World w(40, 30);
	w.build();
	w.wallTop(14, 14); // bit 17 on, as ClassifyWall stamps a top
	TestObject &gate = w.structure(8, 150.0f, 150.0f, 30.0f, 30.0f);
	gate.kinds.insert(PK_BLOCKING_GATE);
	w.pf->addObjectToPathfindMap(gate);
	CHECK(w.cell(14, 14).getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(w.cell(14, 14).isObstaclePresent(8));
	CHECK(w.cell(14, 14).getBit17());
	CHECK(w.cell(14, 14).getLayer() == LAYER_WALL_FIRST);
	w.pf->removeObjectFromPathfindMap(gate);
	CHECK(w.cell(14, 14).getType() == PathfindCell::CELL_CLEAR);
	CHECK_FALSE(w.cell(14, 14).getBit17()); // the plain removal clears it on the top too (RW 0x93713A), as RotWK
	// a cylinder gate: the wall top cell is skipped (its bit 17 not written either)
	w.cell(14, 14).setBit17(true);
	TestObject &round = w.structure(9, 150.0f, 150.0f, 30.0f, 0.0f, PATHFIND_GEOMETRY_CYLINDER);
	round.kinds.insert(PK_BLOCKING_GATE);
	w.pf->addObjectToPathfindMap(round);
	CHECK(w.cell(14, 14).getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.cell(14, 14).getBit17()); // kept: the cylinder skipped the top (a box gate would have written it, RW 0x93713A)
	CHECK(w.cell(15, 15).getType() == PathfindCell::CELL_OBSTACLE); // the ground under it
	CHECK(w.cell(15, 15).getBit17());
	// review r1: the cylinder turns no CLIFF cell into CLEAR (RW 0x9371ED .. 0x93742C call no setType): the cliff stays, its bit 17 is written; the box
	// clears it first (RW 0x9370E3) and stamps it
	w.pf->removeObjectFromPathfindMap(round);
	w.cell(16, 16).setType(PathfindCell::CELL_CLIFF);
	w.pf->addObjectToPathfindMap(round);
	CHECK(w.cell(16, 16).getType() == PathfindCell::CELL_CLIFF);
	CHECK(w.cell(16, 16).getBit17());
	w.pf->removeObjectFromPathfindMap(round);
	CHECK(w.cell(16, 16).getType() == PathfindCell::CELL_CLIFF);
	CHECK_FALSE(w.cell(16, 16).getBit17()); // review r2: the cylinder's removal has no type filter either (RW 0x9371ED)
	w.pf->addObjectToPathfindMap(gate);
	CHECK(w.cell(16, 16).getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(w.cell(16, 16).isObstaclePresent(8));
}

TEST_CASE("wallfoot1: the gate's keeping removal (RW 0x6E861E) leaves its box's bit 17, a plain removal clears it; a cylinder clears it either way (RW 0x9373C7)")
{
	World w(40, 30);
	w.build();
	TestObject &gate = w.structure(8, 150.0f, 150.0f, 30.0f, 30.0f);
	gate.kinds.insert(PK_BLOCKING_GATE);
	w.pf->addObjectToPathfindMap(gate);
	CHECK(w.cell(15, 15).getBit17());
	w.pf->removeObjectFromPathfindMapKeepGateFlag(gate);
	CHECK(w.cell(15, 15).getType() == PathfindCell::CELL_CLEAR);
	CHECK(w.cell(15, 15).getBit17()); // CanPathThroughGates = No still keeps out while the gate is open
	w.pf->addObjectToPathfindMap(gate);
	w.pf->removeObjectFromPathfindMap(gate);
	CHECK_FALSE(w.cell(15, 15).getBit17());
	TestObject &round = w.structure(9, 150.0f, 150.0f, 30.0f, 0.0f, PATHFIND_GEOMETRY_CYLINDER);
	round.kinds.insert(PK_BLOCKING_GATE);
	w.pf->addObjectToPathfindMap(round);
	CHECK(w.cell(15, 15).getBit17());
	w.pf->removeObjectFromPathfindMapKeepGateFlag(round);
	CHECK_FALSE(w.cell(15, 15).getBit17());
}

TEST_CASE("wallfoot1 retail: Helm's Deep's main gate closed and opened again leaves every wall top and ramp cell's type, layer and pinched bit as they were; its closed footprint keeps bit 17")
{
	OPENBFME_REQUIRE_START(s);
	FortressGame g(*s, "maps/map wor helms deep/map wor helms deep.map", false);
	Object *gate = g.named("Main Gate");
	REQUIRE(gate);
	GateOpenAndCloseBehavior *b = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(b);
	REQUIRE(g.game->logic().aiWorld());
	Pathfinder &pf = g.game->logic().aiWorld()->pathfinder();
	struct Snap
	{
		int x, y, type, layer;
		bool pinched, bit17;
	};
	auto offGround = [&]() {
		std::vector<Snap> out;
		const ICoord2D *hi = pf.getExtent();
		for (int y = 0; y <= hi->y; ++y)
		{
			for (int x = 0; x <= hi->x; ++x)
			{
				const PathfindCell *c = pf.cellAt(x, y);
				if (c && c->getLayer() != LAYER_GROUND)
				{
					out.push_back({ x, y, (int)c->getType(), (int)c->getLayer(), c->getPinched(), c->getBit17() });
				}
			}
		}
		return out;
	};
	g.frames(70);
	REQUIRE(b->isSettled());
	const bool openAtStart = b->isOpen(); // OpenByDefault
	if (!openAtStart)
	{
		b->open();
		g.frames(70);
		REQUIRE(b->isSettled());
	}
	REQUIRE(b->isOpen());
	const std::vector<Snap> before = offGround(); // the open gate
	REQUIRE(!before.empty());
	b->close();
	g.frames(70);
	REQUIRE(b->isSettled());
	REQUIRE_FALSE(b->isOpen());
	PathfindCell *centre = pf.getCell(LAYER_GROUND, gate->getPosition());
	REQUIRE(centre);
	CHECK(centre->isObstaclePresent(gate->getID())); // the closed geometry's box
	CHECK(centre->getBit17());
	b->open();
	g.frames(70);
	REQUIRE(b->isSettled());
	REQUIRE(b->isOpen());
	CHECK_FALSE(centre->isObstaclePresent(gate->getID())); // the passage is clear (the open shapes are the two leaves)
	CHECK(centre->getBit17()); // RW 0x6E861E kept it: the open passage still refuses CanPathThroughGates = No
	b->close();
	g.frames(70);
	REQUIRE(b->isSettled());
	b->open();
	g.frames(70);
	REQUIRE(b->isSettled());
	REQUIRE(b->isOpen());
	const std::vector<Snap> after = offGround();
	REQUIRE(after.size() == before.size());
	int changed = 0;
	for (size_t i = 0; i < before.size(); ++i)
	{
		const Snap &p = before[i], &q = after[i];
		// bit 17 may stay on under the closed footprint (the keeping removal, as RotWK); the types, layers and pinched bits must not move
		changed += (p.x != q.x || p.y != q.y || p.type != q.type || p.layer != q.layer || p.pinched != q.pinched) ? 1 : 0;
	}
	MESSAGE("wallfoot1: " << before.size() << " wall top and ramp cells; the gate open at start: " << openAtStart);
	CHECK(changed == 0);
}
