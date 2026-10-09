// OpenBFME. SMOOTH-1 tests: the render pose between two logic frames (GameClient/RenderInterpolation). alpha 0 / 0.5 / 1 give the recorded, the
// middle and the current pose (worked out by hand); the angle goes the short way round across +-pi; a terrain-aligned basis is slerped (stays
// orthonormal, the midpoint of a 90 degree turn is the 45 degree basis); and a 5 Hz clock driving a unit at constant speed renders evenly spaced,
// continuous positions at 144 fps (no step at the logic frame boundaries, no frame without motion).
#include "doctest.h"

#include "Common/GameCommon.h"
#include "Common/LogicFrameClock.h"
#include "GameClient/RenderInterpolation.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
const float IDENTITY[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

void zBasis(float angle, float out[9])
{
	const float c = std::cos(angle), s = std::sin(angle);
	const float b[9] = { c, -s, 0, s, c, 0, 0, 0, 1 };
	for (int i = 0; i < 9; ++i)
	{
		out[i] = b[i];
	}
}

// a rotation about X (a slope across the unit's way): not a pure Z rotation
void xBasis(float angle, float out[9])
{
	const float c = std::cos(angle), s = std::sin(angle);
	const float b[9] = { 1, 0, 0, 0, c, -s, 0, s, c };
	for (int i = 0; i < 9; ++i)
	{
		out[i] = b[i];
	}
}
} // namespace

TEST_CASE("smooth1: alpha 0, 0.5 and 1 give the recorded, middle and current position")
{
	const Coord3D rec{ 100.0f, 200.0f, 10.0f };
	const Coord3D cur{ 110.0f, 196.0f, 12.0f };
	RenderInterpolation::Pose p = RenderInterpolation::interpolate(true, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, 0.0);
	CHECK(p.position.x == 100.0f);
	CHECK(p.position.y == 200.0f);
	CHECK(p.position.z == 10.0f);
	p = RenderInterpolation::interpolate(true, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, 0.5);
	CHECK(p.position.x == 105.0f);
	CHECK(p.position.y == 198.0f);
	CHECK(p.position.z == 11.0f);
	p = RenderInterpolation::interpolate(true, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, 0.25);
	CHECK(p.position.x == 102.5f);
	CHECK(p.position.y == 199.0f);
	p = RenderInterpolation::interpolate(true, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, 1.0);
	CHECK(p.position.x == 110.0f);
	CHECK(p.position.y == 196.0f);
	// a stall hands more than 1 (clamped: never ahead of the logic) and a negative alpha is the recorded pose
	p = RenderInterpolation::interpolate(true, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, 3.0);
	CHECK(p.position.x == 110.0f);
	p = RenderInterpolation::interpolate(true, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, -1.0);
	CHECK(p.position.x == 100.0f);
	// no recorded transform yet (a new object before its first phase 2): the current pose
	p = RenderInterpolation::interpolate(false, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, 0.5);
	CHECK(p.position.x == 110.0f);
	CHECK(p.zRotation);
}

TEST_CASE("smooth1: the facing turns the short way round, also across +-pi")
{
	float b0[9], b1[9];
	zBasis(0.2f, b0);
	zBasis(0.6f, b1);
	RenderInterpolation::Pose p = RenderInterpolation::interpolate(true, Coord3D{}, 0.2f, b0, Coord3D{}, 0.6f, b1, 0.5);
	CHECK(p.zRotation);
	CHECK(p.angle == doctest::Approx(0.4f));
	// 170 degrees to -170 degrees: 20 degrees through pi, not 340 back through 0
	const float pi = 3.14159265358979f;
	const float from = 170.0f * pi / 180.0f, to = -170.0f * pi / 180.0f;
	zBasis(from, b0);
	zBasis(to, b1);
	p = RenderInterpolation::interpolate(true, Coord3D{}, from, b0, Coord3D{}, to, b1, 0.5);
	CHECK(std::fabs(std::fabs(p.angle) - pi) < 1e-4f);
	p = RenderInterpolation::interpolate(true, Coord3D{}, from, b0, Coord3D{}, to, b1, 0.25);
	CHECK(p.angle == doctest::Approx(175.0f * pi / 180.0f).epsilon(1e-5));
	CHECK(RenderInterpolation::lerpAngle(-3.0f, 3.0f, 0.5f) == doctest::Approx(-pi).epsilon(1e-4));
}

TEST_CASE("smooth1: a terrain-aligned basis is slerped, not snapped to the current one")
{
	float b0[9], b1[9], mid[9];
	xBasis(0.0f, b0);
	xBasis(0.5f, b1);
	RenderInterpolation::Pose p = RenderInterpolation::interpolate(true, Coord3D{}, 0.0f, b0, Coord3D{}, 0.0f, b1, 0.5);
	CHECK_FALSE(p.zRotation);
	xBasis(0.25f, mid);
	for (int i = 0; i < 9; ++i)
	{
		CAPTURE(i);
		CHECK(p.basis[i] == doctest::Approx(mid[i]).epsilon(1e-5));
	}
	// a 90 degree turn on a slope: the midpoint is the 45 degree basis and the result stays orthonormal
	float s0[9], s1[9];
	zBasis(0.0f, s0);
	zBasis(1.5707963f, s1);
	s0[5] = 1e-3f; // marks both as "not a pure Z rotation" so the slerp path runs (the error is far below the tolerance below)
	s1[5] = 1e-3f;
	float out[9];
	RenderInterpolation::slerpBasis(s0, s1, 0.5f, out);
	float z45[9];
	zBasis(0.78539816f, z45);
	for (int i = 0; i < 9; ++i)
	{
		CAPTURE(i);
		CHECK(out[i] == doctest::Approx(z45[i]).epsilon(2e-3));
	}
	for (int r = 0; r < 3; ++r)
	{
		const float len = out[r * 3] * out[r * 3] + out[r * 3 + 1] * out[r * 3 + 1] + out[r * 3 + 2] * out[r * 3 + 2];
		CHECK(len == doctest::Approx(1.0f).epsilon(1e-5));
	}
	// the end points are exact
	RenderInterpolation::slerpBasis(b0, b1, 0.0f, out);
	for (int i = 0; i < 9; ++i)
	{
		CHECK(out[i] == b0[i]);
	}
	RenderInterpolation::slerpBasis(b0, b1, 1.0f, out);
	for (int i = 0; i < 9; ++i)
	{
		CHECK(out[i] == b1[i]);
	}
}

TEST_CASE("smooth1: a unit at constant speed renders evenly spaced, continuous positions at 144 fps on the 5 Hz clock")
{
	// the logic: 10 units per logic frame along x. Phase 2 records the transform of the frame start, the frame's updates move the object.
	LogicFrameClock clock(LOGICFRAMES_PER_SECOND);
	Coord3D cur{ 0.0f, 0.0f, 0.0f };
	Coord3D rec = cur;
	bool haveRecorded = false;
	std::vector<float> shown;
	for (int i = 0; i < 144 * 3; ++i)
	{
		const int due = clock.advance(1.0 / 144.0);
		for (int f = 0; f < due; ++f)
		{
			rec = cur;
			haveRecorded = true;
			cur.x += 10.0f;
		}
		shown.push_back(RenderInterpolation::interpolate(haveRecorded, rec, 0.0f, IDENTITY, cur, 0.0f, IDENTITY, clock.getAlpha()).position.x);
	}
	// after the first logic frame every render frame moves by 50 / 144 units (5 frames x 10 units per second), within float rounding
	const float step = 50.0f / 144.0f;
	size_t first = 0;
	while (first < shown.size() && !(shown[first] > 0.0f))
	{
		++first;
	}
	REQUIRE(first < 40);
	int stills = 0;
	for (size_t i = first + 1; i < shown.size(); ++i)
	{
		CAPTURE(i);
		const float d = shown[i] - shown[i - 1];
		CHECK(d == doctest::Approx(step).epsilon(1e-3));
		stills += d == 0.0f ? 1 : 0;
	}
	CHECK(stills == 0);
}

TEST_CASE("smooth1: the smooth-motion stops are reported (S-810 live readers, S-811 presentation beyond retail, S-813 teleport gather, S-1140 SMOOTH-2's pose and pacing)")
{
	const std::vector<std::string> lines = RenderInterpolation::stopLines();
	REQUIRE(lines.size() == 6);
	CHECK(lines[0].rfind("[S-810] ", 0) == 0);
	CHECK(lines[1].rfind("[S-811] ", 0) == 0);
	CHECK(lines[2].rfind("[S-813] ", 0) == 0); // S-812 resolved by SMOOTH-2 (Object::recordTransform clears the pending position, RW 0x62618F)
	CHECK(lines[3].rfind("[S-814] ", 0) == 0);
	CHECK(lines[4].rfind("[S-815] ", 0) == 0);
	CHECK(lines[5].rfind("[S-1140] ", 0) == 0);
}

TEST_CASE("smooth1: D3DXVec3CatmullRom as retail's drawn position (RW 0x6765B9): even spacing is linear, an uneven path curves, a stop overshoots")
{
	// evenly spaced points: the spline is the straight line
	Coord3D p = RenderInterpolation::catmullRom(Coord3D{ 0, 0, 0 }, Coord3D{ 10, 0, 0 }, Coord3D{ 20, 0, 0 }, Coord3D{ 30, 0, 0 }, 0.5f);
	CHECK(p.x == doctest::Approx(15.0f));
	// P0 = 0, P1 = 100, P2 = 140, P3 = 140 (no pending position): by hand 0.5 * (200 + 140 s - 80 s^2 + 20 s^3)
	p = RenderInterpolation::catmullRom(Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 }, Coord3D{ 140, 0, 0 }, Coord3D{ 140, 0, 0 }, 0.5f);
	CHECK(p.x == doctest::Approx(126.25f));
	p = RenderInterpolation::catmullRom(Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 }, Coord3D{ 140, 0, 0 }, Coord3D{ 140, 0, 0 }, 0.25f);
	CHECK(p.x == doctest::Approx(115.15625f));
	// the end points
	p = RenderInterpolation::catmullRom(Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 }, Coord3D{ 140, 0, 0 }, Coord3D{ 140, 0, 0 }, 0.0f);
	CHECK(p.x == doctest::Approx(100.0f));
	p = RenderInterpolation::catmullRom(Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 }, Coord3D{ 140, 0, 0 }, Coord3D{ 140, 0, 0 }, 1.0f);
	CHECK(p.x == doctest::Approx(140.0f));
	// a unit that stopped this frame (P1 = P2 = P3 = 100, P0 = 0) moves on past its stop and comes back: 0.5 * (200 + 50 - 50 + 12.5) = 106.25 at s = 0.5. Retail
	// draws this overshoot (no clamp); the stale rule ends it two frames later
	p = RenderInterpolation::catmullRom(Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 }, Coord3D{ 100, 0, 0 }, Coord3D{ 100, 0, 0 }, 0.5f);
	CHECK(p.x == doctest::Approx(106.25f));
}

TEST_CASE("smooth1: retail's drawn pose from a snapshot record: the Catmull-Rom position, the slerped facing, and the stale snap two frames after the last move")
{
	ObjectSnapshot r;
	r.hasRecorded = true;
	r.previousPos = Coord3D{ 0, 0, 0 };
	r.recordedPos = Coord3D{ 100, 0, 0 };
	r.position = Coord3D{ 140, 0, 0 };
	r.nextPos = r.position;
	r.recordedAngle = 0.0f;
	r.angle = 0.5f;
	float b0[9], b1[9];
	zBasis(0.0f, b0);
	zBasis(0.5f, b1);
	std::memcpy(r.recordedBasis, b0, sizeof(b0));
	std::memcpy(r.basis, b1, sizeof(b1));
	r.lastMovedFrame = 7;
	// moved in the snapshot's own frame (7): interpolated
	RenderInterpolation::Pose p = RenderInterpolation::retailPose(r, 7, 0.5);
	CHECK(p.position.x == doctest::Approx(126.25f));
	CHECK(p.zRotation);
	CHECK(p.angle == doctest::Approx(0.25f));
	// RW 0x6765F1: stale when the last move is before (snapshot frame + 1) - 2: moved in 6 (frame 7 shown): still interpolated; moved in 5: the current transform
	r.lastMovedFrame = 6;
	CHECK(RenderInterpolation::retailPose(r, 7, 0.5).position.x == doctest::Approx(126.25f));
	r.lastMovedFrame = 5;
	p = RenderInterpolation::retailPose(r, 7, 0.5);
	CHECK(p.position.x == 140.0f);
	CHECK(p.angle == 0.5f);
	// the first frames: (0 + 1) - 2 wraps (unsigned, as the binary compares), so a fresh snapshot shows the current transform
	r.lastMovedFrame = 0;
	CHECK(RenderInterpolation::retailPose(r, 0, 0.5).position.x == 140.0f);
	// the alpha is clamped to [0, 1] (RW 0x63256F)
	r.lastMovedFrame = 7;
	CHECK(RenderInterpolation::retailPose(r, 7, 2.0).position.x == doctest::Approx(140.0f));
}
