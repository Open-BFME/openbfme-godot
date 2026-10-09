// OpenBFME unit tests: the deterministic maths the pathfinder uses (GameLogic/SimMath.h: sinCosDet, length2d, floorToInt, ceilToInt).
// Expected values are closed form (cos 0 = 1, cos(pi/2) = 0, the Pythagorean triples) or the correctly rounded float of the true value.

#include "doctest.h"

#include "GameLogic/SimMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
std::uint32_t bits(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}
} // namespace

TEST_CASE("SimMath::sinCosDet: exact at the quadrant points, within 1e-15 of the libm over -50 .. 50")
{
	double s, c;
	SimMath::sinCosDet(0.0, s, c);
	CHECK(s == 0.0);
	CHECK(c == 1.0);
	const double halfPi = 1.5707963267948966;
	SimMath::sinCosDet(halfPi, s, c);
	CHECK(std::fabs(s - 1.0) < 1e-15);
	CHECK(std::fabs(c) < 1e-15);
	SimMath::sinCosDet(-halfPi, s, c);
	CHECK(std::fabs(s + 1.0) < 1e-15);
	SimMath::sinCosDet(2.0 * halfPi, s, c);
	CHECK(std::fabs(s) < 1e-15);
	CHECK(std::fabs(c + 1.0) < 1e-15);
	double worst = 0.0;
	for (int i = -50000; i <= 50000; ++i)
	{
		const double x = (double)i * 0.001;
		SimMath::sinCosDet(x, s, c);
		worst = std::fmax(worst, std::fmax(std::fabs(s - std::sin(x)), std::fabs(c - std::cos(x))));
		CHECK(std::fabs(s * s + c * c - 1.0) < 1e-15);
	}
	CHECK(worst < 1e-15);
	// not an angle: a fixed answer, never a libm dependent one
	SimMath::sinCosDet(1.0e30, s, c);
	CHECK(s == 0.0);
	CHECK(c == 1.0);
}

TEST_CASE("SimMath::cosDet / sinDet: the float of the true value at known angles (bit pinned)")
{
	// cos(1) = 0.5403023058681398 -> float 0x3F0A5140; sin(1) = 0.8414709848078965 -> float 0x3F576AA4
	CHECK(bits(SimMath::cosDet(1.0f)) == 0x3F0A5140u);
	CHECK(bits(SimMath::sinDet(1.0f)) == 0x3F576AA4u);
	CHECK(SimMath::cosDet(0.0f) == 1.0f);
	CHECK(SimMath::sinDet(0.0f) == 0.0f);
	// quarter turn of a float pi/2 is within a float ulp of the exact values
	CHECK(std::fabs(SimMath::sinDet(1.57079637f) - 1.0f) < 1e-7f);
	CHECK(std::fabs(SimMath::cosDet(1.57079637f)) < 1e-6f);
}

TEST_CASE("SimMath::length2d, floorToInt, ceilToInt")
{
	CHECK(SimMath::length2d(3.0f, 4.0f) == 5.0f);
	CHECK(SimMath::length2d(-6.0f, 8.0f) == 10.0f);
	CHECK(SimMath::length2d(0.0f, 0.0f) == 0.0f);
	CHECK(SimMath::floorToInt(2.5f) == 2);
	CHECK(SimMath::floorToInt(-2.5f) == -3);
	CHECK(SimMath::floorToInt(-3.0f) == -3);
	CHECK(SimMath::floorToInt(0.0f) == 0);
	CHECK(SimMath::floorToInt(-0.0001f) == -1);
	CHECK(SimMath::ceilToInt(2.5f) == 3);
	CHECK(SimMath::ceilToInt(-2.5f) == -2);
	CHECK(SimMath::ceilToInt(4.0f) == 4);
	for (int i = -2000; i <= 2000; ++i)
	{
		const float f = (float)i * 0.37f;
		CHECK(SimMath::floorToInt(f) == (int)std::floor(f));
		CHECK(SimMath::ceilToInt(f) == (int)std::ceil(f));
	}
}
