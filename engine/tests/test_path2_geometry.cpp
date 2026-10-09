// OpenBFME unit tests. GPL-3.0.
// Lane PATH-2: the multi-shape geometry of RotWK (S-341) in the pathfinder and the combat radii. Synthetic objects on a flat synthetic terrain, no retail data.
// Expected values are worked out by hand from the RW routines named in GameLogic/AI/AIPathfind.cpp (0xAD2700 / 0xAD2770 / 0xAD2860 / 0xAD14F0 / 0xAD1D60) and
// AIPathfindFootprints.cpp (0x936B7D), never from this engine's output.

#include "doctest.h"

#include "PathfindTestUtil.h"

#include <cmath>

using namespace pathtest;

namespace
{
PathfindShape box(float major, float minor, float ox = 0.0f, float oy = 0.0f, float oz = 0.0f, float height = 10.0f)
{
	PathfindShape s;
	s.type = PATHFIND_GEOMETRY_BOX;
	s.majorRadius = major;
	s.minorRadius = minor;
	s.offsetX = ox;
	s.offsetY = oy;
	s.offsetZ = oz;
	s.height = height;
	return s;
}

PathfindShape round(PathfindGeometryType type, float major, float height, float ox = 0.0f, float oy = 0.0f, float oz = 0.0f)
{
	PathfindShape s;
	s.type = type;
	s.majorRadius = major;
	s.height = height;
	s.offsetX = ox;
	s.offsetY = oy;
	s.offsetZ = oz;
	return s;
}

int obstacleCellsOf(const Pathfinder &pf, PathfindObjectID id)
{
	int n = 0;
	const ICoord2D *e = pf.getExtent();
	for (int x = 0; x <= e->x; ++x)
	{
		for (int y = 0; y <= e->y; ++y)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (c->getType() == PathfindCell::CELL_OBSTACLE && c->getObstacleID() == id)
			{
				++n;
			}
		}
	}
	return n;
}
} // namespace

TEST_CASE("path2 geometry: the bounding circle and sphere are the largest of the active shapes (RW 0xAD2860 / 0xAD2700 / 0xAD2770)")
{
	PathfindGeometry g;
	// a BOX 30 x 20 offset (10, -5): sqrt((|10| + 30)^2 + (|-5| + 20)^2) = sqrt(2225)
	g.shapes.push_back(box(30.0f, 20.0f, 10.0f, -5.0f, 0.0f, 20.0f));
	CHECK(g.boundingCircleRadius() == (float)std::sqrt(2225.0));
	// its sphere: c = |0| + 20 * 0.5 = 10: sqrt(2225 + 100)
	CHECK(g.boundingSphereRadius() == (float)std::sqrt(2325.0));
	// a SPHERE r 10 at offset (40, 0, 3): circle sqrt(1600) + 10 = 50 (larger: it wins), sphere sqrt(9 + 1600) + 10
	g.shapes.push_back(round(PATHFIND_GEOMETRY_SPHERE, 10.0f, 0.0f, 40.0f, 0.0f, 3.0f));
	CHECK(g.boundingCircleRadius() == 50.0f);
	CHECK(g.boundingSphereRadius() == (float)(std::sqrt(1609.0f) + 10.0f));
	// an inactive shape does not count
	g.shapes.push_back(round(PATHFIND_GEOMETRY_SPHERE, 500.0f, 0.0f));
	g.shapes.back().active = false;
	CHECK(g.boundingCircleRadius() == 50.0f);
	// a CYLINDER r 10, height 30, no offset: the sphere is d + sqrt(r^2 + h^2) = 0 + sqrt(100 + 225) (ZH's max(r, h / 2) = 15 is NOT retail)
	PathfindGeometry c;
	c.shapes.push_back(round(PATHFIND_GEOMETRY_CYLINDER, 10.0f, 30.0f));
	CHECK(c.boundingCircleRadius() == 10.0f);
	CHECK(c.boundingSphereRadius() == (float)std::sqrt(325.0));
	// a cylinder offset (3, 4, 0): circle 5 + 10; sphere d = 5, r = 15, h = 15: 5 + sqrt(450)
	PathfindGeometry co;
	co.shapes.push_back(round(PATHFIND_GEOMETRY_CYLINDER, 10.0f, 30.0f, 3.0f, 4.0f));
	CHECK(co.boundingCircleRadius() == 15.0f);
	CHECK(co.boundingSphereRadius() == (float)(5.0f + (float)std::sqrt(450.0)));
	// nothing active: the circle stays at its start 0.01f, the sphere at 0
	PathfindGeometry none;
	none.shapes.push_back(box(30.0f, 20.0f));
	none.shapes.back().active = false;
	CHECK(none.boundingCircleRadius() == 0.01f);
	CHECK(none.boundingSphereRadius() == 0.0f);
	// the single-shape form (no list) is that one shape at no offset
	PathfindGeometry single;
	single.type = PATHFIND_GEOMETRY_BOX;
	single.majorRadius = 3.0f;
	single.minorRadius = 4.0f;
	CHECK(single.boundingCircleRadius() == 5.0f);
}

TEST_CASE("path2 geometry: a shape's offset turns with the object (RW 0xAD14F0) and the 2D box covers every active shape (RW 0xAD1D60)")
{
	const PathfindShape s = box(10.0f, 5.0f, 60.0f, 0.0f, 2.0f);
	Coord3D p{ 100.0f, 200.0f, 7.0f };
	PathfindGeometry::applyShapeOffset(s, 0.0f, p);
	CHECK(p.x == 160.0f);
	CHECK(p.y == 200.0f);
	CHECK(p.z == 9.0f);
	Coord3D q{ 100.0f, 200.0f, 0.0f };
	PathfindGeometry::applyShapeOffset(s, 1.5707964f, q); // a quarter turn: (60, 0) -> (0, 60)
	CHECK(std::fabs(q.x - 100.0f) < 0.001f);
	CHECK(std::fabs(q.y - 260.0f) < 0.001f);
	PathfindGeometry g;
	g.shapes.push_back(box(10.0f, 5.0f));
	g.shapes.push_back(s);
	g.shapes.push_back(round(PATHFIND_GEOMETRY_SPHERE, 400.0f, 0.0f));
	g.shapes.back().active = false;
	float b[4];
	g.boundingBox2D(Coord3D{ 100.0f, 200.0f, 0.0f }, 0.0f, b);
	CHECK(b[0] == 90.0f);
	CHECK(b[1] == 195.0f);
	CHECK(b[2] == 170.0f);
	CHECK(b[3] == 205.0f);
}

TEST_CASE("path2 geometry: every active, low shape of a structure enters the grid at its turned offset, and leaves it again (RW 0x936B7D)")
{
	SyntheticTerrain terrain(100, 100);
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	TestObject obj;
	obj.id = 7;
	obj.structure = true;
	obj.pos = Coord3D{ 300.0f, 300.0f, 0.0f };
	obj.geometry.type = PATHFIND_GEOMETRY_BOX;
	obj.geometry.majorRadius = 15.0f;
	obj.geometry.minorRadius = 15.0f;
	obj.geometry.shapes.push_back(box(15.0f, 15.0f));
	obj.geometry.shapes.push_back(box(15.0f, 15.0f, 60.0f, 0.0f));
	obj.geometry.shapes.push_back(box(15.0f, 15.0f, 0.0f, -60.0f, 20.0f)); // z offset above 10: not a footprint
	obj.geometry.shapes.push_back(box(15.0f, 15.0f, -60.0f, 0.0f));
	obj.geometry.shapes.back().active = false;                             // inactive: not a footprint
	world.objects[obj.id] = &obj;
	pf.addObjectToPathfindMap(obj);
	auto type = [&](int x, int y) { return pf.cellAt(x, y)->getType(); };
	CHECK(type(30, 30) == PathfindCell::CELL_OBSTACLE); // shape 0
	CHECK(type(36, 30) == PathfindCell::CELL_OBSTACLE); // shape 1 at +60 x
	CHECK(type(30, 24) != PathfindCell::CELL_OBSTACLE); // the high shape
	CHECK(type(24, 30) != PathfindCell::CELL_OBSTACLE); // the inactive shape
	CHECK(type(33, 30) != PathfindCell::CELL_OBSTACLE); // the gap between the two boxes is no obstacle
	const int both = obstacleCellsOf(pf, obj.id);
	// two 30 x 30 boxes: ceil(15 * 0.4) = 6 steps of 5 per axis from the corner; at angle 0 the x samples 285 .. 310 give cells 28 .. 31 and the y samples
	// 315 .. 290 give cells 31 .. 29: 4 x 3 cells each (the rasteriser of RW 0x936EE8 / ZH)
	CHECK(both == 2 * 12);
	// the post region reaches the second shape: a CLEAR cross neighbour of it is pinched (pass C)
	CHECK(pf.cellAt(38, 30)->getType() == PathfindCell::CELL_CLEAR);
	CHECK(pf.cellAt(38, 30)->getPinched());
	pf.removeObjectFromPathfindMap(obj);
	CHECK(obstacleCellsOf(pf, obj.id) == 0);
	const PathfindGridStats st = pf.gridStats();
	CHECK(st.types[PathfindCell::CELL_OBSTACLE] == 0);
	CHECK(st.types[PathfindCell::CELL_BRIDGE_IMPASSABLE] == 0);

	// turned a quarter: the second box is now at +60 y
	obj.angle = 1.5707964f;
	pf.addObjectToPathfindMap(obj);
	CHECK(type(30, 36) == PathfindCell::CELL_OBSTACLE);
	CHECK(type(36, 30) != PathfindCell::CELL_OBSTACLE);
	pf.removeObjectFromPathfindMap(obj);

	// mutation check: the shape-0-only geometry (the single-shape fields the pre-PATH-2 code read) covers half the cells
	TestObject single = obj;
	single.id = 8;
	single.angle = 0.0f;
	single.geometry.shapes.clear();
	world.objects[single.id] = &single;
	pf.addObjectToPathfindMap(single);
	CHECK(obstacleCellsOf(pf, single.id) == 12);
	CHECK(obstacleCellsOf(pf, single.id) < both);
}

TEST_CASE("path2 geometry: the footprint size of a mobile unit reads the multi-shape bounding circle (RW 0x6EAF79 reads GeometryInfo + 0x10)")
{
	SyntheticTerrain terrain(40, 40);
	TestWorld world;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	TestObject unit;
	unit.mobile = true;
	unit.geometry.type = PATHFIND_GEOMETRY_SPHERE;
	unit.geometry.majorRadius = 4.0f;
	unit.geometry.shapes.push_back(round(PATHFIND_GEOMETRY_SPHERE, 4.0f, 10.0f));
	// d = 2 * 4 = 8: floor(0.8 + 0.3) = 1
	CHECK(pf.footprintSize(&unit) == 1);
	// a second shape at offset 15 makes the circle 19: d = 38, floor(3.8 + 0.3) = 4, not above the cap 2 * maxR = 4 (not a MONSTER / HORDE / SHIP)
	unit.geometry.shapes.push_back(round(PATHFIND_GEOMETRY_SPHERE, 4.0f, 10.0f, 15.0f, 0.0f));
	CHECK(pf.footprintSize(&unit) == 4);
}
