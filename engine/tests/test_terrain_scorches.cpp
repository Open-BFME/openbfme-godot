// OpenBFME unit tests: the terrain's scorch marks (TerrainScorchBuffer, stops S-117 / S-033). Every expected
// value is read off the RotWK bodies named in each case (tools/mod/rw.py RotWK\game.dat dis <address>), not off the port.

#include "doctest.h"

#include "Common/MapObject.h"
#include "GameEngineDevice/W3DDevice/GameClient/TerrainScorches.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
Coord3D at(float x, float y, float z = 0.0f)
{
	Coord3D c;
	c.x = x;
	c.y = y;
	c.z = z;
	return c;
}

// a flat map: `w` x `h` grid points, border `b`, every height `h0`
TerrainScorchBuffer::Terrain flatTerrain(int w, int h, int b, std::uint16_t h0)
{
	TerrainScorchBuffer::Terrain t;
	t.xExtent = w;
	t.yExtent = h;
	t.border = b;
	t.heights.assign((size_t)w * (size_t)h, h0);
	return t;
}
} // namespace

TEST_CASE("terrain scorches: addScorch keeps 500 marks and a full list replaces the oldest slot (RW 0x46765B)")
{
	TerrainScorchBuffer buf;
	// distinct marks: 100 apart, radius 10 (match distance 2.5)
	for (int i = 0; i < 500; ++i)
	{
		buf.addScorch(at((float)(i * 100), 0.0f), 10.0f, 1);
	}
	CHECK(buf.numScorches() == 500);
	CHECK(buf.nextScorch() == 0);
	// RW 0x4676EE .. 0x46771C: the 501st goes into slot 0 (the oldest), the oldest moves to 1, the count stays 500
	buf.addScorch(at(-1000.0f, 50.0f), 7.0f, 3);
	CHECK(buf.numScorches() == 500);
	CHECK(buf.nextScorch() == 1);
	CHECK(buf.scorch(0).location.x == -1000.0f);
	CHECK(buf.scorch(0).location.y == 50.0f);
	CHECK(buf.scorch(0).radius == 7.0f);
	CHECK(buf.scorch(0).type == 3);
	CHECK(buf.scorch(1).location.x == 100.0f);
	buf.addScorch(at(-2000.0f, 50.0f), 7.0f, 3);
	CHECK(buf.nextScorch() == 2);
	CHECK(buf.scorch(1).location.x == -2000.0f);
	// RW 0x46CE22: the map reset empties it
	buf.clearAllScorches();
	CHECK(buf.numScorches() == 0);
	CHECK(buf.nextScorch() == 0);
}

TEST_CASE("terrain scorches: a mark like one in the list is not added, it shows that one again (RW 0x467686 .. 0x467778)")
{
	TerrainScorchBuffer buf;
	buf.addScorch(at(100.0f, 100.0f), 20.0f, 2);
	REQUIRE(buf.numScorches() == 1);
	// within the NEW mark's radius * 0.25 = 6 (RW 0xBD1904) on x, y and radius, the same type: not added
	buf.addScorch(at(104.9f, 95.1f), 24.0f, 2);
	CHECK(buf.numScorches() == 1);
	CHECK(buf.scorch(0).location.x == 100.0f); // the old entry is kept as it was
	// another type: added (RW 0x4676D9)
	buf.addScorch(at(100.0f, 100.0f), 20.0f, 3);
	CHECK(buf.numScorches() == 2);
	// x off by exactly the limit 20 * 0.25 = 5 (|dx| < limit fails): added
	buf.addScorch(at(105.0f, 100.0f), 20.0f, 2);
	CHECK(buf.numScorches() == 3);
	// the limit is the NEW mark's radius * 0.25: radius 4 -> limit 1, |20 - 4| fails
	buf.addScorch(at(100.0f, 100.0f), 4.0f, 2);
	CHECK(buf.numScorches() == 4);
}

TEST_CASE("terrain scorches: hiding in a radius and the rebuild flag (RW 0x46947C, RW 0x467086, RW 0x467778)")
{
	TerrainScorchBuffer buf;
	TerrainScorchBuffer::Terrain t = flatTerrain(64, 64, 0, 0);
	TerrainScorchBuffer::Shade shade;
	buf.addScorch(at(100.0f, 100.0f), 10.0f, 0);
	buf.addScorch(at(300.0f, 300.0f), 10.0f, 0);
	CHECK(buf.needsUpdate());
	CHECK(buf.updateScorches(t, shade));
	CHECK(buf.scorchesInBuffer() == 2);
	// two marks drawn: no rebuild until something changes (RW 0x467086: + 0x3794 > 1)
	CHECK_FALSE(buf.needsUpdate());
	CHECK_FALSE(buf.updateScorches(t, shade));
	// RW 0x4694D8: radius + radius (10 + 5) > the 3D distance hides; the mark at 300 is 282.8 away
	buf.hideScorchesInRadius(at(110.0f, 100.0f, 0.0f), 5.0f);
	CHECK(buf.scorch(0).hidden);
	CHECK_FALSE(buf.scorch(1).hidden);
	CHECK(buf.needsUpdate());
	CHECK(buf.updateScorches(t, shade));
	CHECK(buf.scorchesInBuffer() == 1);
	// one mark drawn: rebuilt at every draw (+ 0x3794 <= 1)
	CHECK(buf.needsUpdate());
	// a third, drawn: two in the buffer after the rebuild
	buf.addScorch(at(500.0f, 100.0f), 10.0f, 0);
	CHECK(buf.updateScorches(t, shade));
	CHECK(buf.scorchesInBuffer() == 2);
	CHECK_FALSE(buf.needsUpdate());
	// the hidden one again: shown (hidden byte cleared) but the buffer is NOT marked for a rebuild (RW 0x467778 skips RW 0x46776A)
	buf.addScorch(at(100.0f, 100.0f), 10.0f, 0);
	CHECK_FALSE(buf.scorch(0).hidden);
	CHECK(buf.numScorches() == 3);
	CHECK_FALSE(buf.needsUpdate());
	// the z distance counts: 15 above the mark with radius 4 (10 + 4 < 15) does not hide
	buf.hideScorchesInRadius(at(300.0f, 300.0f, 15.0f), 4.0f);
	CHECK_FALSE(buf.scorch(1).hidden);
}

TEST_CASE("terrain scorches: the vertex colour (RW 0x4670E1 .. 0x4671A4)")
{
	TerrainScorchBuffer::Shade s;
	for (int k = 0; k < 3; ++k)
	{
		s.ambient[k] = 0.2f;
		s.diffuse[k] = 0.6f;
	}
	// 0.6 * 0.5 + 0.2 = 0.5, times 255 = 127.5 truncated: 0x7F, alpha 0xFF
	CHECK(TerrainScorchBuffer::scorchDiffuse(s) == 0xFF7F7F7Fu);
	// overbright doubles to 1.0: 0xFF
	s.overbright = true;
	CHECK(TerrainScorchBuffer::scorchDiffuse(s) == 0xFFFFFFFFu);
	// the map's light scale multiplies the diffuse term only: red 0.5 * 0.6 * 0.5 + 0.2 = 0.35, times 2 = 0.7 -> 178.5 -> 178 (0xB2)
	s.scale[0] = 0.5f;
	s.diffuse[2] = 1.0f; // blue 1.0 * 0.5 + 0.2 = 0.7 * 2 = 1.4 clamped to 1.0
	CHECK(TerrainScorchBuffer::scorchDiffuse(s) == 0xFFB2FFFFu);
}

TEST_CASE("terrain scorches: one mark's grid, atlas cell and heights (RW 0x467073)")
{
	TerrainScorchBuffer buf;
	TerrainScorchBuffer::Terrain t = flatTerrain(40, 40, 2, 256);
	t.heights[(size_t)(9 + 2) * 40 + (8 + 2)] = 512; // the grid point (8, 9) of the map
	TerrainScorchBuffer::Shade shade;
	// type 4 (SCORCH_5): the atlas cell (4 % 3, 4 / 3) = (1, 1), offsets 1/3
	buf.addScorch(at(100.0f, 100.0f), 20.0f, 4);
	REQUIRE(buf.updateScorches(t, shade));
	// minX = floor((100 - 20) * 0.1) - 1 = 7, maxX = ceil((100 + 20) * 0.1) + 1 = 13: 6 x 6 points, 5 x 5 cells
	const auto &v = buf.vertices();
	REQUIRE(v.size() == 36);
	CHECK(buf.indices().size() == 150);
	CHECK(v[0].x == 70.0f);
	CHECK(v[0].y == 70.0f);
	CHECK(v[5].x == 120.0f);
	CHECK(v[6].y == 80.0f);
	// u = (X - left) / width / 3 + 1/3 with left 70, width (13 - 7 - 1) * 10 = 50
	const float third = 1.0f / 3.0f;
	CHECK(v[0].u == third); // (70 - 70) / 50 / 3 + 1/3
	CHECK(v[5].u == 2.0f * third); // 50 / 50 * 1/3 + 1/3: a float doubling, exact
	CHECK(v[30].v == 2.0f * third);
	// z = height * 0.0390625 + 1 (RW 0xBDB8EC): 256 -> 11, the grid point (8, 9) -> 512 -> 21
	CHECK(v[0].z == 11.0f);
	CHECK(v[(9 - 7) * 6 + (8 - 7)].z == 21.0f);
	// the first cell unflipped (RW 0x4675A0): (i, i + 1 + yOffset, i + yOffset, i, i + 1, i + 1 + yOffset), yOffset 6
	const std::vector<std::uint16_t> first(buf.indices().begin(), buf.indices().begin() + 6);
	CHECK(first == std::vector<std::uint16_t>{ 0, 7, 6, 0, 1, 7 });
}

TEST_CASE("terrain scorches: a flipped cell's diagonal, the type outside 0 .. 8 and the border clamp (RW 0x46755B, 0x46720E, 0x4672ED)")
{
	TerrainScorchBuffer buf;
	TerrainScorchBuffer::Terrain t = flatTerrain(40, 40, 2, 0);
	t.flip.assign(40 * 40, 0);
	// the mark's first cell is grid (7, 7) of the map = (9, 9) of the height grid
	t.flip[(size_t)9 * 40 + 9] = 1;
	TerrainScorchBuffer::Shade shade;
	buf.addScorch(at(100.0f, 100.0f), 20.0f, 12); // type 12 draws as type 0
	REQUIRE(buf.updateScorches(t, shade));
	const std::vector<std::uint16_t> first(buf.indices().begin(), buf.indices().begin() + 6);
	CHECK(first == std::vector<std::uint16_t>{ 1, 6, 0, 1, 7, 6 });
	CHECK(buf.vertices()[0].u == 0.0f);
	CHECK(buf.vertices()[0].v == 0.0f);
	// near the corner: minX = floor(-0.5) - 1 = -2, clamped at -border = -2; at the far side maxX = ceil(40.5) + 1 = 42 > 40 - 2 = 38
	TerrainScorchBuffer corner;
	corner.addScorch(at(5.0f, 395.0f), 10.0f, 0);
	REQUIRE(corner.updateScorches(t, shade));
	// x: [-2, ceil(1.5) + 1 = 3): 5 points; y: [floor(38.5) - 1 = 37, min(ceil(40.5) + 1, 40 - 2) = 38): 1 row, no cell
	CHECK(corner.vertices().size() == 5);
	CHECK(corner.vertices()[0].x == -20.0f);
	CHECK(corner.indices().empty());
}

TEST_CASE("terrain scorches: the map's Scorch objects, objectRadius and scorchType (RW 0x491E36 .. 0x491EC3)")
{
	std::vector<MapObject> objects(3);
	objects[0].m_location = at(50.0f, 60.0f, 3.0f);
	objects[0].m_isScorch = true;
	objects[0].m_properties.setReal("objectRadius", 25.0f);
	objects[0].m_properties.setInt("scorchType", 5);
	objects[1].m_location = at(10.0f, 10.0f); // not a scorch (RW 0x491E3C: + 0x44 bit 3 clear)
	objects[1].m_properties.setReal("objectRadius", 25.0f);
	objects[2].m_location = at(400.0f, 60.0f);
	objects[2].m_isScorch = true;
	objects[2].m_properties.setInt("scorchType", 1); // no REAL objectRadius: an error, not added
	TerrainScorchBuffer buf;
	std::vector<std::string> errors;
	CHECK(TerrainScorchBuffer::addMapScorches(buf, objects, errors) == 1);
	REQUIRE(buf.numScorches() == 1);
	CHECK(buf.scorch(0).location.x == 50.0f);
	CHECK(buf.scorch(0).location.z == 3.0f);
	CHECK(buf.scorch(0).radius == 25.0f);
	CHECK(buf.scorch(0).type == 5);
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].find("objectRadius") != std::string::npos);
}

TEST_CASE("terrain scorches: the vertex budget takes the last mark's vertices back and ends the update (RW 0x46741B / 0x467616)")
{
	TerrainScorchBuffer buf;
	TerrainScorchBuffer::Terrain t = flatTerrain(200, 200, 0, 0);
	TerrainScorchBuffer::Shade shade;
	// the list is drawn from the last index: index 1 first, then index 0
	buf.addScorch(at(1500.0f, 1500.0f), 450.0f, 0); // index 0: [floor(105) - 1 = 104, ceil(195) + 1 = 196): 92 x 92 = 8464 vertices
	buf.addScorch(at(500.0f, 500.0f), 40.0f, 0);    // index 1: [floor(46) - 1 = 45, ceil(54) + 1 = 55): 10 x 10 = 100 vertices
	REQUIRE(buf.updateScorches(t, shade));
	// 100 + 8464 > 0x2002 (8194): the big mark's vertices are taken back, its indices never written
	CHECK(buf.vertices().size() == 100);
	CHECK(buf.indices().size() == 9 * 9 * 6);
	CHECK(buf.scorchesInBuffer() == 2);
}

TEST_CASE("terrain scorches: the draw's depth bias is the eye-space scale 1 / (1 + 1.0 * 0.000625) (RW 0x468C43, RW 0x51EB60)")
{
	CHECK(TerrainScorchBuffer::DEPTH_BIAS == 1.0f);       // RW 0x468C43 fld1
	CHECK(TerrainScorchBuffer::ZBIAS_SCALE == 0.000625f); // RW 0xBE6CF8
	CHECK(TerrainScorchBuffer::depthBiasViewScale(1.0f) == 1.0f / 1.000625f);
	CHECK(TerrainScorchBuffer::depthBiasViewScale(0.0f) == 1.0f); // RW 0x468CAE: back to 0, no bias
	// RW 0x51EB60 takes zbias * 0.000625 * zfar * znear / (zfar - znear) off _43: the biased depth of a point at d is the unbiased depth of the
	// point at d * depthBiasViewScale, whatever the clip planes
	const float planes[][2] = { { 1.0f, 1000.0f }, { 10.0f, 5000.0f }, { 4.0f, 2600.0f } };
	for (const auto &p : planes)
	{
		for (float d : { 20.0f, 300.0f, 900.0f })
		{
			const float biased = TerrainScorchBuffer::zBiasedDepth(1.0f, p[0], p[1], d);
			const float moved = TerrainScorchBuffer::zBiasedDepth(0.0f, p[0], p[1], d * TerrainScorchBuffer::depthBiasViewScale(1.0f));
			CHECK(biased < TerrainScorchBuffer::zBiasedDepth(0.0f, p[0], p[1], d)); // tolerance: the bias draws the mark nearer, a strict order
			CHECK(biased == doctest::Approx(moved).epsilon(1e-6)); // tolerance: the two forms round differently in float (a few ulps)
		}
	}
}
