// OpenBFME. GPL-3.0.
//
// Lane QPATH-1: Pathfinder::quickDoesPathExist, the port of QuickDoesPathExist RW 0x6F5BB0 (BFME2 0x6F477E, Open-BFME-2 pathfinder.cpp:2951), and the
// terrain zone RW 0x938939 it remaps an obstacle end with. Synthetic maps, no retail data.

#include "doctest.h"

#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIPathfind.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using namespace pathtest;

namespace
{
class QuickUnit : public TestObject
{
public:
	bool immobileStatus = false;
	bool byte4AC = false;
	bool hasAISet = false;
	PathfindLocomotorInfo aiSet;
	bool isStatusImmobile() const override { return immobileStatus; }
	bool refusesPortals() const override { return byte4AC; }
	bool getAILocomotorInfo(PathfindLocomotorInfo &out) const override
	{
		if (hasAISet)
		{
			out = aiSet;
		}
		return hasAISet;
	}
};

struct QuickWorld
{
	SyntheticTerrain terrain{ 40, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	QuickUnit *unit = nullptr;
	explicit QuickWorld(bool cliffWall = false)
	{
		if (cliffWall)
		{
			for (int y = 0; y < 20; ++y)
			{
				terrain.cliff.insert({ 19, y });
				terrain.cliff.insert({ 20, y });
			}
		}
		unit = add<QuickUnit>(1, 55.0f, 105.0f);
		unit->mobile = true;
		unit->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		unit->geometry.majorRadius = 5.0f;
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		zones();
	}
	template <class T> T *add(PathfindObjectID id, float x, float y)
	{
		T *o = new T();
		o->id = id;
		o->pos = { x, y, 0.0f };
		world.objects[id] = o;
		return o;
	}
	TestObject *structure(PathfindObjectID id, float x, float y, float halfX, float halfY)
	{
		TestObject *s = add<TestObject>(id, x, y);
		s->structure = true;
		s->geometry.type = PATHFIND_GEOMETRY_BOX;
		s->geometry.majorRadius = halfX;
		s->geometry.minorRadius = halfY;
		pf->addObjectToPathfindMap(*s);
		return s;
	}
	void zones()
	{
		struct None : PathfindRequestHandler
		{
			void doPathfind(PathfindObjectID) override {}
		} none;
		pf->processPathfindQueue(none);
	}
	~QuickWorld()
	{
		for (auto &kv : world.objects)
		{
			delete kv.second;
		}
	}
	bool quick(const Coord3D &from, const Coord3D &to, const PathfindLocomotorInfo *set)
	{
		return pf->quickDoesPathExist(*unit, &from, &to, set);
	}
};
} // namespace

TEST_CASE("qpath1: an IMMOBILE unit (status 49, RW 0x6F5C30) has no quick path even to a reachable spot; the test comes before Object + 0x4AC")
{
	QuickWorld w;
	const PathfindLocomotorInfo ground = groundLoco();
	const Coord3D from = w.unit->pos, to{ 255.0f, 105.0f, 0.0f };
	REQUIRE(w.quick(from, to, &ground));
	w.unit->immobileStatus = true;
	CHECK_FALSE(w.quick(from, to, &ground));
	w.unit->byte4AC = true; // RW 0x68C053 == 1 answers yes, but only after the IMMOBILE test
	CHECK_FALSE(w.quick(from, to, &ground));
	w.unit->immobileStatus = false;
	CHECK(w.quick(from, to, &ground)); // not IMMOBILE, + 0x4AC == 1: "Success 1" before any cell test
}

TEST_CASE("qpath1: a set with none of the surfaces 0x8F (GROUND, WATER, CLIFF, AIR, DEEP_WATER) has no quick path (RW 0x6F5CD4); no set and no AI is no path either")
{
	QuickWorld w;
	const Coord3D from = w.unit->pos, to{ 255.0f, 105.0f, 0.0f };
	PathfindLocomotorInfo rubbleOnly;
	rubbleOnly.validSurfaces = LOCOMOTORSURFACE_RUBBLE | LOCOMOTORSURFACE_OBSTACLE;
	CHECK_FALSE(w.quick(from, to, &rubbleOnly));
	PathfindLocomotorInfo withGround = rubbleOnly;
	withGround.validSurfaces |= LOCOMOTORSURFACE_GROUND;
	CHECK(w.quick(from, to, &withGround));
	// RW 0x6F5C90 .. 0x6F5CBF: no set given and no AI (+ 0x260): "Failure 2"
	CHECK_FALSE(w.quick(from, to, nullptr));
	// no set given: the AI's (AI + 0x1CC); a set given wins over it (RW 0x6F5C9B)
	w.unit->hasAISet = true;
	w.unit->aiSet = groundLoco();
	CHECK(w.quick(from, to, nullptr));
	CHECK_FALSE(w.quick(from, to, &rubbleOnly));
	w.unit->aiSet = rubbleOnly;
	CHECK_FALSE(w.quick(from, to, nullptr));
	CHECK(w.quick(from, to, &withGround));
}

TEST_CASE("qpath1: a start inside a structure placed this frame keeps its zone until the zone update (RW 0x93485A writes no zone): a quick path, no ZH early exit needed")
{
	QuickWorld w;
	const PathfindLocomotorInfo ground = groundLoco();
	const zoneStorageType before = w.pf->cellAt(5, 10)->getZone();
	REQUIRE(before != PathfindZoneManager::UNINITIALIZED_ZONE);
	w.structure(60, 55.0f, 105.0f, 20.0f, 20.0f); // over the unit; the zones are not recomputed yet
	REQUIRE(w.pf->cellAt(5, 10)->getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(w.pf->cellAt(5, 10)->getZone() == before); // retail's footprint pass marks the block dirty (RW 0x937D96) and leaves the cell's zone
	CHECK(w.pf->zoneManager().isDirty());
	const Coord3D from = w.unit->pos, to{ 255.0f, 105.0f, 0.0f };
	// the start is an obstacle: both ends terrain(effective(terrain(zone))) (RW 0x938939 / 0x938D89): the ground's terrain zone at both ends
	CHECK(w.quick(from, to, &ground));
	w.zones();
	CHECK_FALSE(w.pf->zoneManager().isDirty());
	CHECK(w.quick(from, to, &ground));
	// the goal inside the structure is refused by IsValidMovementPositionForObject (RW 0x6EA4E7), whatever the zones say
	const Coord3D inside{ 55.0f, 105.0f, 0.0f }, outside{ 255.0f, 105.0f, 0.0f };
	CHECK_FALSE(w.quick(outside, inside, &ground));
}

TEST_CASE("qpath1: the terrain zone RW 0x938939 joins CLEAR and OBSTACLE zones of one layer only, its root is the highest id (RW 0x938B3B)")
{
	QuickWorld w(true); // a cliff wall across x = 190 .. 210
	w.structure(60, 105.0f, 105.0f, 20.0f, 20.0f);
	w.zones();
	const PathfindZoneManager &zm = w.pf->zoneManager();
	const zoneStorageType obstacle = w.pf->cellAt(10, 10)->getZone();
	const zoneStorageType west = w.pf->cellAt(3, 3)->getZone();
	const zoneStorageType east = w.pf->cellAt(30, 10)->getZone();
	const zoneStorageType cliff = w.pf->cellAt(19, 10)->getZone();
	REQUIRE(w.pf->cellAt(10, 10)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(w.pf->cellAt(19, 10)->getType() == PathfindCell::CELL_CLIFF);
	CHECK(zm.getTerrainZone(obstacle) == zm.getTerrainZone(west));
	CHECK(zm.getTerrainZone(west) != zm.getTerrainZone(east)); // the cliff is no terrain type: it splits them
	CHECK(zm.getTerrainZone(cliff) == cliff);                  // a cliff zone joins nothing
	// the root: the highest zone id among the cells 4-connected to (3, 3) through CLEAR and OBSTACLE cells (a flood over the cells, not the zone tables)
	zoneStorageType highestWest = 0;
	{
		std::vector<std::pair<int, int>> open{ { 3, 3 } };
		std::set<std::pair<int, int>> seen{ { 3, 3 } };
		while (!open.empty())
		{
			const auto c = open.back();
			open.pop_back();
			const PathfindCell *cell = w.pf->cellAt(c.first, c.second);
			highestWest = std::max(highestWest, cell->getZone());
			const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
			for (const auto &dd : d)
			{
				const std::pair<int, int> n{ c.first + dd[0], c.second + dd[1] };
				if (n.first < 0 || n.second < 0 || n.first >= 40 || n.second >= 20 || seen.count(n))
				{
					continue;
				}
				const PathfindCell *nc = w.pf->cellAt(n.first, n.second);
				if (nc && (nc->getType() == PathfindCell::CELL_CLEAR || nc->getType() == PathfindCell::CELL_OBSTACLE))
				{
					seen.insert(n);
					open.push_back(n);
				}
			}
		}
	}
	CHECK(zm.getTerrainZone(west) == highestWest);
	CHECK(zm.getTerrainZone(obstacle) == highestWest);
	CHECK(zm.getTerrainZone(PathfindZoneManager::UNINITIALIZED_ZONE) == 0);
	// QuickDoesPathExist from inside the structure: the start is an obstacle cell, both ends are remapped and meet on the west side only
	const PathfindLocomotorInfo ground = groundLoco();
	const Coord3D inBuilding{ 105.0f, 105.0f, 0.0f }, westGoal{ 35.0f, 35.0f, 0.0f }, eastGoal{ 305.0f, 105.0f, 0.0f };
	CHECK(w.quick(inBuilding, westGoal, &ground));
	CHECK_FALSE(w.quick(inBuilding, eastGoal, &ground));
}

TEST_CASE("qpath1: a goal on a raised walkway or on the ground below takes the layer TerrainLogic::getLayerForDestination names for the point (RW 0x680A75)")
{
	// the gatehouse of test_wall2_raised.cpp, reduced: a raised walkway over a ground obstacle; the point's height picks the layer
	SyntheticTerrain terrain{ 60, 20 };
	TestWorld world;
	struct Piece : TestObject
	{
		PathfindWallGeometry walls;
		const PathfindWallGeometry *getWallGeometry() const override { return walls.empty() ? nullptr : &walls; }
	};
	auto quad = [](float x0, float y0, float x1, float y1, float z) {
		PathfindWallSurface s;
		PathfindWallTriangle a, b;
		a.v[0] = { x0, y0, z };
		a.v[1] = { x1, y0, z };
		a.v[2] = { x1, y1, z };
		b.v[0] = { x0, y0, z };
		b.v[1] = { x1, y1, z };
		b.v[2] = { x0, y1, z };
		s.triangles = { a, b };
		s.loX = x0;
		s.loY = y0;
		s.hiX = x1;
		s.hiY = y1;
		s.height = z;
		return s;
	};
	Piece *walkway = new Piece();
	walkway->id = 51;
	walkway->pos = { 300.0f, 10.0f, 0.0f };
	walkway->structure = true;
	walkway->kinds.insert(PK_WALK_ON_TOP_OF_WALL);
	walkway->geometry.type = PATHFIND_GEOMETRY_BOX;
	walkway->geometry.majorRadius = 1.0f;
	walkway->geometry.minorRadius = 1.0f;
	walkway->walls.raised.push_back(quad(200.0f, 90.0f, 400.0f, 110.0f, 60.0f));
	world.objects[walkway->id] = walkway;
	TestObject *box = new TestObject();
	box->id = 60;
	box->pos = { 300.0f, 100.0f, 0.0f };
	box->structure = true;
	box->geometry.type = PATHFIND_GEOMETRY_BOX;
	box->geometry.majorRadius = 20.0f;
	box->geometry.minorRadius = 10.0f;
	world.objects[box->id] = box;
	QuickUnit *u = new QuickUnit();
	u->id = 1;
	u->pos = { 250.0f, 100.0f, 60.0f };
	u->mobile = true;
	u->geometry.type = PATHFIND_GEOMETRY_SPHERE;
	u->geometry.majorRadius = 5.0f;
	world.objects[u->id] = u;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	pf.addObjectToPathfindMap(*walkway);
	pf.addObjectToPathfindMap(*box);
	struct None : PathfindRequestHandler
	{
		void doPathfind(PathfindObjectID) override {}
	} none;
	pf.processPathfindQueue(none);
	REQUIRE(pf.cellAt(30, 10)->getType() == PathfindCell::CELL_OBSTACLE);
	const PathfindLocomotorInfo ground = groundLoco();
	// the unit is on the walkway: its start point's layer is the walkway's even though the test object reports the ground layer
	const Coord3D from = u->pos, onWalkway{ 300.0f, 100.0f, 60.0f }, belowOnGround{ 300.0f, 100.0f, 0.0f };
	CHECK(pf.searchDestinationLayer(from) != LAYER_GROUND);
	CHECK(pf.searchDestinationLayer(onWalkway) != LAYER_GROUND);
	CHECK(pf.searchDestinationLayer(belowOnGround) == LAYER_GROUND);
	CHECK(pf.quickDoesPathExist(*u, &from, &onWalkway, &ground));
	CHECK_FALSE(pf.quickDoesPathExist(*u, &from, &belowOnGround, &ground)); // the ground cell is the box: not a valid position (RW 0x6EA4E7)
	for (auto &kv : world.objects)
	{
		delete kv.second;
	}
}

TEST_CASE("qpath1: a horde chasing a fleeing target through a gate takes the gate's portals (RW 0x74A6E6 passes the horde; the zone-only test said no)")
{
	// the portal world of test_wall2_portals.cpp: a cliff wall with a gate's waypoints (Link From:A To:B and back)
	QuickWorld w(true);
	TestObject *gate = w.add<TestObject>(50, 200.0f, 100.0f);
	gate->structure = true;
	auto make = [&](float x, float y) {
		PathfindWaypoint p;
		p.pos = { x, y, 0.0f };
		p.type = PWAYPOINT_WALK;
		p.owner = gate->id;
		return w.pf->createWaypoint(p);
	};
	const int a = make(175.0f, 105.0f), b = make(235.0f, 105.0f);
	w.pf->addWaypointLink(a, b);
	w.pf->addPortal(a);
	w.pf->addWaypointLink(b, a);
	w.pf->addPortal(b);
	// a small building on the west side (review): its obstacle zone may become the highest id of the west's terrain variant class; the portals are filed
	// in the table the quick test's zones come from, so the gate is still found
	w.structure(61, 175.0f, 185.0f, 4.0f, 4.0f);
	w.zones();
	w.unit->kinds.insert(PK_HORDE);
	const PathfindLocomotorInfo ground = groundLoco();
	const Coord3D from = w.unit->pos, fleeing{ 305.0f, 105.0f, 0.0f };
	CHECK_FALSE(w.pf->clientSafeQuickDoesPathExist(ground, &from, &fleeing)); // before: HordeAIUpdateCombat asked without the horde
	CHECK(w.quick(from, fleeing, &ground));                                    // RW 0x6F5EE7 -> the portal search RW 0x6F5547
}
