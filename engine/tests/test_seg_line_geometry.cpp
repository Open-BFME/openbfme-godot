// OpenBFME. GPL-3.0.
//
// SegLineGeometry: the camera-space ribbon of WW3D's SegmentedLineClass (SegLineRendererClass::Render without the device; RW setters 0x55DBC0 width,
// 0x55DC20 colour, 0x55DC50 opacity, 0x55DCC0 mapping mode, 0x55DCE0 -> 0x590A40 tile factor). Client / draw only.

#include "doctest.h"

#include "GameClient/SegLineGeometry.h"

#include <algorithm>
#include <cmath>

TEST_CASE("SegLineGeometry: WW3D's SegmentedLine geometry: a straight line is one quad of the width turned to the camera, TILED V by point index, "
	"the UV offset added; a bend joins its edges at one shared pair of vertices (seglinerenderer Render)")
{
	SegLineSettings s;
	s.width = 1.5f;
	s.color[0] = 0.96f;
	s.color[1] = 0.77f;
	s.color[2] = 0.47f;
	s.opacity = 1.0f;
	s.tileFactor = 4.0f;
	s.mapMode = SegLineSettings::TILED;
	s.uvOffset[1] = 0.25f;
	// a horizontal line 10 in front of the camera (camera space, -Z ahead)
	SegLineMesh m;
	SegLineGeometry::build({ { 0, 0, -10 }, { 10, 0, -10 } }, s, m);
	REQUIRE(m.vertices.size() == 4);
	REQUIRE(m.indices.size() == 6);
	// top / bottom are the width apart, across the line and the view (the ribbon faces the camera)
	CHECK(std::fabs(m.vertices[0].y - m.vertices[1].y) == doctest::Approx(1.5f).epsilon(0.01)); // tolerance: the edges come from float plane intersections (x87 in retail), not one exact sum
	CHECK(m.vertices[0].x == doctest::Approx(0.0f).epsilon(0.001)); // tolerance: float plane intersection
	CHECK(m.vertices[2].x == doctest::Approx(10.0f).epsilon(0.001)); // tolerance: float plane intersection
	CHECK(m.vertices[0].z == doctest::Approx(-10.0f).epsilon(0.01)); // tolerance: float plane intersection
	// U 0 / 1 across, V = point index * 4 + the offset
	CHECK(m.u[0] == 0.0f);
	CHECK(m.u[1] == 1.0f);
	CHECK(m.v[0] == 0.25f);
	CHECK(m.v[2] == 4.25f);
	// the vertex colour is (Color, Opacity) (subdivision_util without per-point colours)
	CHECK(m.rgba[0] == 0.96f);
	CHECK(m.rgba[3] == 1.0f);

	// a right-angle bend: the middle point is one pair of vertices where both segments' edges meet (no gap, no overlap): 6 vertices, 4 triangles
	SegLineMesh b;
	SegLineGeometry::build({ { 0, 0, -10 }, { 10, 0, -10 }, { 10, 10, -10 } }, s, b);
	CHECK(b.vertices.size() == 6);
	CHECK(b.indices.size() == 12);
	// the outer corner's vertex lies beyond both segments' edges (the miter), the inner one inside
	float maxX = -1e9f, minY = 1e9f;
	for (const SegLineVec3 &v : b.vertices)
	{
		maxX = std::max(maxX, v.x);
		minY = std::min(minY, v.y);
	}
	CHECK(maxX > 10.5f); // tolerance: the miter corner lies beyond the half width (0.75) past the bend; its exact place is a float intersection
	CHECK(minY < -0.5f); // tolerance: the outer edge lies below the line by about the half width (0.75)
}
