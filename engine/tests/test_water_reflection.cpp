// OpenBFME unit tests: the planar water reflection's maths (GameClient/WaterReflection.h, lane WATER-REFLECT, stop S-035). GPL-3.0.
//
// The expected values are worked out here from the definitions (the reflection of the camera's axes and position in the plane z = Z; the geometry of a mirror)
// or read from the retail files by an independent script (tools/render/test_water_binary_facts.py reads the same game.dat facts; the map.ini keys by a text
// scan), never from this engine's output. Float comparisons carry `// tolerance:`.

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/MapWeather.h"
#include "GameClient/WaterReflection.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{

// a camera: columns right, up, back (the camera looks along -back), translation
Matrix3D cameraOf(const Vector3 &right, const Vector3 &up, const Vector3 &back, const Vector3 &pos)
{
	Matrix3D m;
	m.Row[0][0] = right.X; m.Row[0][1] = up.X; m.Row[0][2] = back.X; m.Row[0][3] = pos.X;
	m.Row[1][0] = right.Y; m.Row[1][1] = up.Y; m.Row[1][2] = back.Y; m.Row[1][3] = pos.Y;
	m.Row[2][0] = right.Z; m.Row[2][1] = up.Z; m.Row[2][2] = back.Z; m.Row[2][3] = pos.Z;
	return m;
}

Vector3 col(const Matrix3D &m, int c)
{
	return Vector3(m.Row[0][c], m.Row[1][c], m.Row[2][c]);
}

float dot(const Vector3 &a, const Vector3 &b)
{
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

Vector3 sub(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
}

Vector3 cross(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
}

// A camera at `pos` looking along `forward` (unit) with the world's z as up, as a game camera pitched down at the map.
Matrix3D lookCamera(const Vector3 &pos, Vector3 forward)
{
	const float len = std::sqrt(dot(forward, forward));
	forward = Vector3(forward.X / len, forward.Y / len, forward.Z / len);
	Vector3 right = cross(forward, Vector3(0, 0, 1));
	const float rl = std::sqrt(dot(right, right));
	right = Vector3(right.X / rl, right.Y / rl, right.Z / rl);
	const Vector3 up = cross(right, forward);
	return cameraOf(right, up, Vector3(-forward.X, -forward.Y, -forward.Z), pos);
}

// the ndc of a world point seen by a camera with vertical field of view `tanHalf` (tan of half the angle) and the aspect; nothing when behind
bool ndcOf(const Matrix3D &cam, const Vector3 &p, float tanHalf, float aspect, float &x, float &y)
{
	const Vector3 d = sub(p, Vector3(cam.Row[0][3], cam.Row[1][3], cam.Row[2][3]));
	const float depth = -dot(d, col(cam, 2));
	if (depth <= 0.0f)
	{
		return false;
	}
	x = dot(d, col(cam, 0)) / (depth * tanHalf * aspect);
	y = dot(d, col(cam, 1)) / (depth * tanHalf);
	return true;
}

} // namespace

TEST_CASE("water reflection: the mirror camera of RW 0x47EA2B, worked out by hand")
{
	// right (1,0,0), up (0,.8,.6), back = right x up = (0,-.6,.8); position (10,20,100); plane z = 40
	const Matrix3D cam = cameraOf(Vector3(1, 0, 0), Vector3(0, 0.8f, 0.6f), Vector3(0, -0.6f, 0.8f), Vector3(10, 20, 100));
	Matrix3D out;
	out.Row[0][0] = 99.0f;
	REQUIRE(WaterReflection::mirrorCameraTransform(cam, 40.0f, out));
	// each axis loses twice its z component, the reflected up is negated; the position goes to 2 * 40 - 100 = -20
	CHECK(col(out, 0).X == 1.0f); CHECK(col(out, 0).Y == 0.0f); CHECK(col(out, 0).Z == 0.0f);
	CHECK(col(out, 1).X == 0.0f); CHECK(col(out, 1).Y == -0.8f); CHECK(col(out, 1).Z == 0.6f); // -(0, .8, .6 - 1.2)
	CHECK(col(out, 2).X == 0.0f); CHECK(col(out, 2).Y == -0.6f); CHECK(col(out, 2).Z == -0.8f);
	CHECK(out.Row[0][3] == 10.0f); CHECK(out.Row[1][3] == 20.0f); CHECK(out.Row[2][3] == -20.0f);
}

TEST_CASE("water reflection: the mirror camera is a proper rotation and its position is the mirror image")
{
	const float planes[] = { -50.0f, 0.0f, 13.0f, 90.0f, 294.0f };
	for (float planeZ : planes)
	{
		const Matrix3D cam = lookCamera(Vector3(120.0f, -340.0f, planeZ + 410.0f), Vector3(0.2f, 0.9f, -0.55f));
		Matrix3D out;
		REQUIRE(WaterReflection::mirrorCameraTransform(cam, planeZ, out));
		const Vector3 r = col(out, 0), u = col(out, 1), n = col(out, 2);
		// orthonormal
		CHECK(std::fabs(dot(r, r) - 1.0f) < 1e-5f); // tolerance: float rounding of unit vectors
		CHECK(std::fabs(dot(u, u) - 1.0f) < 1e-5f); // tolerance: float rounding of unit vectors
		CHECK(std::fabs(dot(n, n) - 1.0f) < 1e-5f); // tolerance: float rounding of unit vectors
		CHECK(std::fabs(dot(r, u)) < 1e-5f); // tolerance: float rounding of orthogonal vectors
		CHECK(std::fabs(dot(u, n)) < 1e-5f); // tolerance: float rounding of orthogonal vectors
		// determinant +1: r x u = n (a mirror image would give -n; the negated up is what keeps this a rotation)
		const Vector3 c = cross(r, u);
		CHECK(std::fabs(c.X - n.X) < 1e-5f); // tolerance: float rounding of the cross product
		CHECK(std::fabs(c.Y - n.Y) < 1e-5f); // tolerance: float rounding of the cross product
		CHECK(std::fabs(c.Z - n.Z) < 1e-5f); // tolerance: float rounding of the cross product
		CHECK(out.Row[0][3] == cam.Row[0][3]);
		CHECK(out.Row[1][3] == cam.Row[1][3]);
		CHECK(out.Row[2][3] == 2.0f * planeZ - cam.Row[2][3]);
	}
}

TEST_CASE("water reflection: a non-finite camera is refused and leaves the output alone")
{
	Matrix3D cam;
	cam.Row[0][0] = std::numeric_limits<float>::quiet_NaN();
	Matrix3D out;
	out.Row[1][1] = 7.0f;
	CHECK_FALSE(WaterReflection::mirrorCameraTransform(cam, 59.0f, out));
	CHECK(out.Row[1][1] == 7.0f);
	cam.Row[0][0] = std::numeric_limits<float>::infinity();
	CHECK_FALSE(WaterReflection::mirrorCameraTransform(cam, 59.0f, out));
}

TEST_CASE("water reflection: the mirror camera sees a point above the water where the viewing camera sees its reflection in the water")
{
	// A pixel of the water at W shows what a ray from the viewing camera C through W finds after bouncing: the point P above the plane with P' = (P.x, P.y, 2Z - P.z)
	// on the line C -> W. The water shader samples the texture at ((ndc.x + 1) / 2, (ndc.y + 1) / 2) of W in the viewing camera, so the mirror camera must put P at the
	// same x and at the negated y (its up is negated: the texture is upside down, and v = (ndc.y + 1) / 2 does not flip it back).
	const float planeZ = 59.0f, tanHalf = 0.5f, aspect = 16.0f / 9.0f;
	const Matrix3D viewing = lookCamera(Vector3(-200.0f, -900.0f, 640.0f), Vector3(0.25f, 0.85f, -0.45f));
	Matrix3D mirror;
	REQUIRE(WaterReflection::mirrorCameraTransform(viewing, planeZ, mirror));
	const Vector3 above[] = { Vector3(100, 200, 300), Vector3(-350, 120, 95), Vector3(40, 700, 260), Vector3(300, 500, 80) };
	for (const Vector3 &p : above)
	{
		// W: where C -> P' meets the plane
		const Vector3 c(viewing.Row[0][3], viewing.Row[1][3], viewing.Row[2][3]);
		const Vector3 pm(p.X, p.Y, 2.0f * planeZ - p.Z);
		const float t = (planeZ - c.Z) / (pm.Z - c.Z);
		const Vector3 w(c.X + (pm.X - c.X) * t, c.Y + (pm.Y - c.Y) * t, planeZ);
		float wx, wy, px, py;
		REQUIRE(ndcOf(viewing, w, tanHalf, aspect, wx, wy));
		REQUIRE(ndcOf(mirror, p, tanHalf, aspect, px, py));
		CAPTURE(p.X);
		CHECK(std::fabs(px - wx) < 1e-4f); // tolerance: float rounding of two projections
		CHECK(std::fabs(py + wy) < 1e-4f); // tolerance: float rounding of two projections
	}
}

namespace
{

// D3D view (row vectors) and a left-handed perspective projection for a camera transform: x right, y up, z forward (= -back)
WaterReflection::Mat4 d3dView(const Matrix3D &cam)
{
	const Vector3 r = col(cam, 0), u = col(cam, 1);
	const Vector3 f(-cam.Row[0][2], -cam.Row[1][2], -cam.Row[2][2]);
	const Vector3 pos(cam.Row[0][3], cam.Row[1][3], cam.Row[2][3]);
	WaterReflection::Mat4 v = {};
	v.m[0][0] = r.X; v.m[0][1] = u.X; v.m[0][2] = f.X;
	v.m[1][0] = r.Y; v.m[1][1] = u.Y; v.m[1][2] = f.Y;
	v.m[2][0] = r.Z; v.m[2][1] = u.Z; v.m[2][2] = f.Z;
	v.m[3][0] = -dot(pos, r); v.m[3][1] = -dot(pos, u); v.m[3][2] = -dot(pos, f); v.m[3][3] = 1.0f;
	return v;
}

WaterReflection::Mat4 d3dProjection(float tanHalf, float aspect, float zn, float zf)
{
	WaterReflection::Mat4 p = {};
	p.m[0][0] = 1.0f / (tanHalf * aspect);
	p.m[1][1] = 1.0f / tanHalf;
	p.m[2][2] = zf / (zf - zn);
	p.m[2][3] = 1.0f;
	p.m[3][2] = -zn * zf / (zf - zn);
	return p;
}

void clipOf(const Vector3 &world, const WaterReflection::Mat4 &view, const WaterReflection::Mat4 &proj, float clip[4])
{
	const float v[4] = { world.X, world.Y, world.Z, 1.0f };
	float cam[4];
	WaterReflection::transform(v, view, cam);
	WaterReflection::transform(cam, proj, clip);
}

} // namespace

TEST_CASE("water reflection: the oblique projection puts the near plane on the water plane and keeps what is above it")
{
	const float planeZ = 59.0f, tanHalf = 0.5f, aspect = 16.0f / 9.0f;
	const Matrix3D viewing = lookCamera(Vector3(-200.0f, -900.0f, 640.0f), Vector3(0.25f, 0.85f, -0.45f));
	Matrix3D mirror;
	REQUIRE(WaterReflection::mirrorCameraTransform(viewing, planeZ, mirror));
	const WaterReflection::Mat4 view = d3dView(mirror);
	const WaterReflection::Mat4 plain = d3dProjection(tanHalf, aspect, 10.0f, 5000.0f);
	WaterReflection::Mat4 oblique;
	REQUIRE(WaterReflection::obliqueClipProjection(view, plain, planeZ, oblique));
	// only the third column changes
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			if (c != 2)
			{
				CHECK(oblique.m[r][c] == plain.m[r][c]);
			}
		}
	}
	// the mirror camera sits below the plane looking up: points of the plane in front of it have clip z = 0, points above it z > 0 (kept), below it z < 0 (clipped)
	const Vector3 onPlane[] = { Vector3(0, 0, planeZ), Vector3(300, 400, planeZ), Vector3(-250, 150, planeZ) };
	for (const Vector3 &p : onPlane)
	{
		float clip[4];
		clipOf(p, view, oblique, clip);
		REQUIRE(clip[3] > 0.0f); // tolerance: sign test, the point is in front of the camera
		CHECK(std::fabs(clip[2] / clip[3]) < 1e-3f); // tolerance: the oblique plane is computed in float
	}
	const Vector3 aboveP[] = { Vector3(0, 0, planeZ + 40), Vector3(300, 400, planeZ + 5), Vector3(-250, 150, planeZ + 200) };
	for (const Vector3 &p : aboveP)
	{
		float clip[4];
		clipOf(p, view, oblique, clip);
		CHECK(clip[3] > 0.0f); // tolerance: sign test, the point is in front of the camera
		CHECK(clip[2] > 0.0f); // tolerance: sign test, the kept side of the plane
	}
	const Vector3 belowP[] = { Vector3(0, 0, planeZ - 40), Vector3(300, 400, planeZ - 5), Vector3(-250, 150, planeZ - 200) };
	for (const Vector3 &p : belowP)
	{
		float clip[4];
		clipOf(p, view, oblique, clip);
		CHECK(clip[3] > 0.0f); // tolerance: sign test, the point is in front of the camera
		CHECK(clip[2] < 0.0f); // tolerance: sign test, the clipped side of the plane
	}
}

TEST_CASE("water reflection: the oblique projection refuses a singular view and a projection without x/y scale")
{
	WaterReflection::Mat4 zero = {};
	WaterReflection::Mat4 proj = d3dProjection(0.5f, 1.0f, 10.0f, 1000.0f);
	WaterReflection::Mat4 out;
	CHECK_FALSE(WaterReflection::obliqueClipProjection(zero, proj, 59.0f, out));
	WaterReflection::Mat4 identity = {};
	for (int i = 0; i < 4; ++i)
	{
		identity.m[i][i] = 1.0f;
	}
	proj.m[0][0] = 0.0f;
	CHECK_FALSE(WaterReflection::obliqueClipProjection(identity, proj, 59.0f, out));
}

TEST_CASE("water reflection: IsReflectionVisible's polygon test clips against the frustum planes")
{
	// the cube [-1,1]^3 as inward planes
	std::vector<WaterReflection::Plane> cube;
	for (int axis = 0; axis < 3; ++axis)
	{
		for (int side = 0; side < 2; ++side)
		{
			WaterReflection::Plane p = { { 0, 0, 0 }, 1.0f };
			p.n[axis] = side == 0 ? 1.0f : -1.0f;
			cube.push_back(p);
		}
	}
	auto quad = [](float x0, float y0, float x1, float y1, float z) {
		return std::vector<Vector3>{ Vector3(x0, y0, z), Vector3(x1, y0, z), Vector3(x1, y1, z), Vector3(x0, y1, z) };
	};
	CHECK(WaterReflection::polygonInFrustum(quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), cube));  // inside
	CHECK(WaterReflection::polygonInFrustum(quad(0.5f, 0.5f, 3.0f, 3.0f, 0.0f), cube));    // one corner inside
	CHECK(WaterReflection::polygonInFrustum(quad(-9.0f, -9.0f, 9.0f, 9.0f, 0.0f), cube));  // covers the frustum with no vertex in it
	CHECK_FALSE(WaterReflection::polygonInFrustum(quad(2.0f, 2.0f, 4.0f, 4.0f, 0.0f), cube));   // beside it
	CHECK_FALSE(WaterReflection::polygonInFrustum(quad(-0.5f, -0.5f, 0.5f, 0.5f, 2.0f), cube));  // above it
	CHECK_FALSE(WaterReflection::polygonInFrustum({ Vector3(0, 0, 0), Vector3(1, 0, 0) }, cube)); // fewer than three vertices
	// touching only along an edge leaves a degenerate polygon of >= 3 vertices on the boundary: it still counts, as the clip keeps vertices on the plane
	CHECK(WaterReflection::polygonInFrustum(quad(1.0f, -0.5f, 3.0f, 0.5f, 0.0f), cube));
}

TEST_CASE("water reflection: the sampled coordinate is the viewing ndc mapped to 0..1 plus the perturbation")
{
	const float none[2] = { 0.0f, 0.0f };
	float uv[2];
	WaterReflection::reflectionUv(-1.0f, -1.0f, 0.005f, none, uv);
	CHECK(uv[0] == 0.0f); CHECK(uv[1] == 0.0f);
	WaterReflection::reflectionUv(0.0f, 0.0f, 0.005f, none, uv);
	CHECK(uv[0] == 0.5f); CHECK(uv[1] == 0.5f);
	const float normal[2] = { 0.2f, -0.4f };
	WaterReflection::reflectionUv(1.0f, 1.0f, 0.005f, normal, uv);
	CHECK(uv[0] == 1.0f + 0.005f * 0.2f);
	CHECK(uv[1] == 1.0f + 0.005f * -0.4f);
	CHECK(WaterReflection::kTextureSize == 512); // RW 0x47E1D7: push 0x200 twice before Create_Render_Target
}

TEST_CASE("water reflection: WaterTransparency's ReflectionPlaneZ and ReflectionOn, water.ini then the map's map.ini")
{
	WaterTransparencyScan::Result r;
	std::string error;
	// water.ini's block, then a map override that sets the plane with a trailing ';' comment, as the retail maps write it
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("WaterTransparency\n  TransparentWaterDepth = 3.0\n  ReflectionPlaneZ = 59.0;\n  ReflectionOn = No\nEnd\n", r, &error), error);
	CHECK(r.reflectionPlaneZSet);
	CHECK(r.reflectionPlaneZ == 59.0f);
	CHECK_FALSE(r.reflectionOn);
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("; MP ARGONATH\nWaterTransparency\n  ReflectionPlaneZ = 89.0;\n  ReflectionOn = Yes\nEnd\nWeather\n  HardwareFogEnable = Yes\nEnd\n", r, &error, true), error);
	CHECK(r.reflectionPlaneZ == 89.0f);
	CHECK(r.reflectionOn);
	// keys outside the block and other blocks are not read
	WaterTransparencyScan::Result other;
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("Weather\n  ReflectionPlaneZ = 5\nEnd\n", other, &error), error);
	CHECK_FALSE(other.reflectionPlaneZSet);
	CHECK_FALSE(other.blockSeen);
}

TEST_CASE("water reflection: ReflectionPlaneZ is scanned as RW 0x42ED00 scans a real: macros, expressions, the %f prefix; nan, inf and garbage fail")
{
	struct Row
	{
		const char *value;
		bool ok;
		float expected;       // when ok
		const char *errorHas; // when not ok
	};
	const Row rows[] = {
		{ "89.0", true, 89.0f, "" },
		{ "-12", true, -12.0f, "" },
		{ "13.", true, 13.0f, "" },
		// scanReal resolves #ADD / #SUBTRACT / #MULTIPLY / #DIVIDE expressions (spaces between the tokens, as the INI math writes them)
		{ "#ADD( 10 79 )", true, 89.0f, "" },
		{ "#MULTIPLY( 2 45 )", true, 90.0f, "" },
		{ "#SUBTRACT( 100 11 )", true, 89.0f, "" },
		{ "#DIVIDE( 180 2 )", true, 90.0f, "" },
		// sscanf("%f") of MSVCR71 reads the numeric prefix and ignores the rest: "3e" is 3, "59.5abc" is 59.5
		{ "3e", true, 3.0f, "" },
		{ "59.5abc", true, 59.5f, "" },
		{ "110%", true, 110.0f, "" },
		// no number at the start: scanReal's error, and neither nan nor inf is one (MSVCR71's converter does not read them)
		{ "nan", false, 0.0f, "Expected floating point value, math op, or predefined macro, but found 'nan'" },
		{ "NaN", false, 0.0f, "Expected floating point value" },
		{ "inf", false, 0.0f, "Expected floating point value, math op, or predefined macro, but found 'inf'" },
		{ "-inf", false, 0.0f, "Expected floating point value" },
		{ "infinity", false, 0.0f, "Expected floating point value" },
		{ "high", false, 0.0f, "Expected floating point value, math op, or predefined macro, but found 'high'" },
		{ "e5", false, 0.0f, "Expected floating point value" },
		{ "#BOGUS( 1 )", false, 0.0f, "" },
	};
	for (const Row &row : rows)
	{
		CAPTURE(row.value);
		WaterTransparencyScan::Result r;
		std::string error;
		const std::string text = std::string("WaterTransparency\n  ReflectionPlaneZ = ") + row.value + "\nEnd\n";
		const bool ok = WaterTransparencyScan::applyText(text, r, &error);
		CHECK(ok == row.ok);
		if (row.ok)
		{
			CHECK(r.reflectionPlaneZ == row.expected);
			CHECK(std::isfinite(r.reflectionPlaneZ));
			CHECK(r.reflectionPlaneZSet);
		}
		else
		{
			CHECK_MESSAGE(error.find(row.errorHas) != std::string::npos, error);
			CHECK_FALSE(r.reflectionPlaneZSet);
		}
	}
	// a #define resolves through the macro table
	WaterTransparencyScan::Result r;
	std::string error;
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("#define PLANE_Z 77.5\nWaterTransparency\n  ReflectionPlaneZ = PLANE_Z\nEnd\n", r, &error), error);
	CHECK(r.reflectionPlaneZ == 77.5f);
	// no token at all
	WaterTransparencyScan::Result missing;
	CHECK_FALSE(WaterTransparencyScan::applyText("WaterTransparency\n  ReflectionPlaneZ =\nEnd\n", missing, &error));
}

TEST_CASE("water reflection: ReflectionOn is Yes or No (RW 0x42E558); every other word fails; the block's keys are the table's; a second block is an error")
{
	for (const char *good : { "Yes", "yes", "YES", "No", "no" })
	{
		CAPTURE(good);
		WaterTransparencyScan::Result r;
		std::string error;
		REQUIRE_MESSAGE(WaterTransparencyScan::applyText(std::string("WaterTransparency\n  ReflectionOn = ") + good + "\nEnd\n", r, &error), error);
		CHECK(r.reflectionOn == (good[0] == 'Y' || good[0] == 'y'));
	}
	for (const char *bad : { "1", "true", "On", "yes!", "0" })
	{
		CAPTURE(bad);
		WaterTransparencyScan::Result r;
		std::string error;
		CHECK_FALSE(WaterTransparencyScan::applyText(std::string("WaterTransparency\n  ReflectionOn = ") + bad + "\nEnd\n", r, &error));
		CHECK_MESSAGE(error.find(std::string("invalid boolean token ") + bad + " -- expected Yes or No") != std::string::npos, error);
	}
	// every key of RW 0xC1E570..0xC1E610 parses (with its own parse proc), an unknown key is an error
	WaterTransparencyScan::Result all;
	std::string error;
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("WaterTransparency\n  TransparentWaterMinOpacity = 1.0\n  TransparentWaterDepth = 3.0\n  RiverTransparencyMultiplier = 1.0\n  StandingWaterColor = R:255 G:255 B:255\n"
		"  StandingWaterTexture = TWWater01.tga\n  AdditiveBlending = no\n  RadarWaterColor = R:140 G:140 B:255\n  ReflectionPlaneZ = 59.0\n  ReflectionOn = No\n  ReflectionGuard = X:1 Y:2\nEnd\n", all, &error), error);
	WaterTransparencyScan::Result unknown;
	CHECK_FALSE(WaterTransparencyScan::applyText("WaterTransparency\n  ReflectionPlaneX = 5\nEnd\n", unknown, &error));
	WaterTransparencyScan::Result badColor;
	CHECK_FALSE(WaterTransparencyScan::applyText("WaterTransparency\n  StandingWaterColor = R:300 G:0 B:0\nEnd\n", badColor, &error));
	// "WaterTransparency found twice": a second block in a file that is not loaded as overrides
	WaterTransparencyScan::Result twice;
	CHECK_FALSE(WaterTransparencyScan::applyText("WaterTransparency\n  ReflectionOn = No\nEnd\nWaterTransparency\n  ReflectionOn = Yes\nEnd\n", twice, &error));
	CHECK_MESSAGE(error.find("WaterTransparency found twice") != std::string::npos, error);
	WaterTransparencyScan::Result overrides;
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("WaterTransparency\n  ReflectionOn = No\nEnd\n", overrides, &error), error);
	REQUIRE_MESSAGE(WaterTransparencyScan::applyText("WaterTransparency\n  ReflectionOn = Yes\nEnd\n", overrides, &error, true), error);
	CHECK(overrides.reflectionOn);
}

TEST_CASE("water reflection: the retail maps' ReflectionPlaneZ against the mounted files")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water reflection retail planes");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	struct Row { const char *map; float planeZ; bool on; };
	// read from each map.ini by tools/render/test_water_binary_facts.py's text scan (water.ini: 59.0 / No)
	const Row rows[] = {
		{ "map mp adorn river", 13.0f, true }, { "map mp anfalas", 69.0f, true }, { "map mp fords of isen ii", 294.0f, true }, { "map mp evendim", 59.0f, true },
		{ "map mp harlindon", 90.0f, true }, { "map mp argonath", 89.0f, true }, { "map mp umbar", 59.0f, true },
		{ "map mp the heubris", 59.0f, false }, // no WaterTransparency block in its map.ini: water.ini's values
	};
	for (const Row &row : rows)
	{
		CAPTURE(row.map);
		WaterTransparencyScan::Result r;
		std::string error;
		REQUIRE_MESSAGE(WaterTransparencyScan::load(*mount->fs, std::string("maps\\") + row.map, r, &error), error);
		CHECK(r.reflectionPlaneZSet);
		CHECK(r.reflectionPlaneZ == row.planeZ);
		CHECK(r.reflectionOn == row.on);
	}
}

TEST_CASE("water reflection: every retail map.ini reads through the INI pipeline; 66 of 123 set ReflectionOn = Yes")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water reflection retail corpus");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	FilenameList list;
	mount->fs->getFileListInDirectory("", "", "map.ini", list, true);
	int total = 0, on = 0;
	for (const std::string &path : list)
	{
		std::string lowered = path;
		for (char &c : lowered)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		const std::string suffix = "map.ini";
		if (lowered.size() <= suffix.size() + 1 || lowered.compare(lowered.size() - suffix.size(), suffix.size(), suffix) != 0 || lowered.rfind("maps", 0) != 0)
		{
			continue;
		}
		const std::string mapDir = path.substr(0, path.size() - suffix.size() - 1);
		CAPTURE(mapDir);
		WaterTransparencyScan::Result r;
		std::string error;
		REQUIRE_MESSAGE(WaterTransparencyScan::load(*mount->fs, mapDir, r, &error), error);
		CHECK(r.reflectionPlaneZSet); // water.ini sets it
		CHECK(std::isfinite(r.reflectionPlaneZ));
		++total;
		on += r.reflectionOn ? 1 : 0;
	}
	CHECK(total == 123);
	CHECK(on == 66);
}
