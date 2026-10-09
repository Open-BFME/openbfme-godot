// OpenBFME unit tests: the shared numeric facade additions (Common/NumericState.h): wide PC24 results, the SSE binary32 operations, the retail
// float -> integer conversions (_ftol2, FISTP dword, CVTTSS2SI) including invalid input, and the libm-free floor / ceil / fabs / sqrt.
// GPL-3.0. Lane WEAPON-1 review. Expected values are the x87 / SSE instruction semantics (Intel SDM: "integer indefinite" on invalid conversion,
// FISTP rounds with the control word's mode, FLD/FMUL keep the 15-bit exponent) and exact rational arithmetic.

#include "doctest.h"

#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ios>
#include <limits>
#include <string>
#include <vector>

namespace
{
std::uint32_t bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}

std::uint64_t bitsOf(double d)
{
	std::uint64_t u;
	std::memcpy(&u, &d, 8);
	return u;
}
}

using namespace NumericState;

TEST_CASE("facade _ftol2: truncation to signed 64 bit, callers use the low word (the frame 0xC0000000 vector)")
{
	CHECK(ftol2(3.9) == 3);
	CHECK(ftol2(-3.9) == -3);
	CHECK(ftol2(0.9) == 0);
	// the P1 vector: 3221225472.0 does not fit an int32, but the 64 bit result is exact and its low word is 0xC0000000
	CHECK(ftol2(3221225472.0) == 3221225472ll);
	CHECK(ftol2Low32(3221225472.0) == 0xC0000000u);
	CHECK(ftol2Low32(4294967296.0 + 7.0) == 7u); // wraps like the low word
	CHECK(ftol2Low32(-1.5) == 0xFFFFFFFFu);
	// invalid input: the integer indefinite 0x8000000000000000, low word 0
	CHECK(ftol2(std::numeric_limits<double>::quiet_NaN()) == INT64_MIN);
	CHECK(ftol2(std::numeric_limits<double>::infinity()) == INT64_MIN);
	CHECK(ftol2(-std::numeric_limits<double>::infinity()) == INT64_MIN);
	CHECK(ftol2(9223372036854775808.0) == INT64_MIN);
	CHECK(ftol2Low32(1.0e300) == 0u);
	CHECK(ftol2(9223372036854774784.0) == 9223372036854774784ll); // the largest double below 2^63
}

TEST_CASE("facade FISTP dword: round to nearest even, integer indefinite outside int32")
{
	CHECK(fistp32(2.5) == 2);
	CHECK(fistp32(3.5) == 4);
	CHECK(fistp32(-2.5) == -2);
	CHECK(fistp32(-3.5) == -4);
	CHECK(fistp32(0.5) == 0);
	CHECK(fistp32(1.5) == 2);
	CHECK(fistp32(-0.5) == 0);
	CHECK(fistp32(2.4999) == 2);
	CHECK(fistp32(2.5001) == 3);
	CHECK(fistp32(2147483647.0) == 2147483647);
	CHECK(fistp32(2147483647.4) == 2147483647);
	CHECK(fistp32(2147483647.5) == INT32_MIN); // rounds to 2^31: out of range
	CHECK(fistp32(-2147483648.0) == INT32_MIN);
	CHECK(fistp32(-2147483648.4) == INT32_MIN);
	CHECK(fistp32(-2147483648.6) == INT32_MIN); // -2^31 - 1: out of range -> indefinite (which is the same pattern)
	CHECK(fistp32(2147483648.0) == INT32_MIN);
	CHECK(fistp32(std::numeric_limits<double>::quiet_NaN()) == INT32_MIN);
	CHECK(fistp32(std::numeric_limits<double>::infinity()) == INT32_MIN);
	CHECK(fistp32(-std::numeric_limits<double>::infinity()) == INT32_MIN);
	CHECK(fistp32(1.0e18) == INT32_MIN);
}

TEST_CASE("facade CVTTSS2SI: truncation, 0x80000000 for NaN, infinity and out of range")
{
	CHECK(cvttss2si(2.9f) == 2);
	CHECK(cvttss2si(-2.9f) == -2);
	CHECK(cvttss2si(0.99f) == 0);
	CHECK(cvttss2si(2147483520.0f) == 2147483520); // the largest float below 2^31
	CHECK(cvttss2si(2147483648.0f) == INT32_MIN);
	CHECK(cvttss2si(-2147483648.0f) == INT32_MIN);
	CHECK(cvttss2si(-2147483904.0f) == INT32_MIN);
	CHECK(cvttss2si(std::numeric_limits<float>::quiet_NaN()) == INT32_MIN);
	CHECK(cvttss2si(std::numeric_limits<float>::infinity()) == INT32_MIN);
	CHECK(cvttss2si(-std::numeric_limits<float>::infinity()) == INT32_MIN);
	CHECK(cvttss2si(3.0e38f) == INT32_MIN);
}

TEST_CASE("facade wide PC24 results keep the exponent: 2^64 * 2^64 is 2^128, narrowing happens at fstp dword (the P2 vector)")
{
	const double big = std::ldexp(1.0, 64);
	CHECK(pc24MulW(big, big) == std::ldexp(1.0, 128));
	CHECK(pc24Mul((float)big, (float)big) == std::numeric_limits<float>::infinity()); // op; fstp dword
	CHECK(fstpDword(pc24MulW(big, big)) == std::numeric_limits<float>::infinity());
	// sqrt(2^128 + 0) - 2^64 stays finite and exact in the register
	const double sum = pc24AddW(pc24MulW(big, big), 0.0);
	CHECK(sqrtPC24(sum) == big);
	CHECK(pc24SubW(sqrtPC24(sum), big) == 0.0);
	// results carry 24 significant bits: 1 + 2^-24 does not survive
	CHECK(pc24AddW(1.0, std::ldexp(1.0, -24)) == 1.0);
	CHECK(pc24AddW(1.0, std::ldexp(1.0, -23)) == 1.0 + std::ldexp(1.0, -23));
	// the wide result is the same as the narrowed one where the float range suffices
	CHECK(fstpDword(pc24DivW(1.0, 3.0)) == pc24Div(1.0f, 3.0f));
	CHECK(fstpDword(pc24SubW(1.5, 0.25)) == 1.25f);
	CHECK(bitsOf(fstpDword(pc24SubW(1.5, 1.5))) == 0x00000000u);
	CHECK(bitsOf(pc24SubW(1.5, 1.5)) == 0u);
	CHECK(std::isnan(pc24DivW(0.0, 0.0)));
	CHECK(pc24DivW(1.0, 0.0) == std::numeric_limits<double>::infinity());
	CHECK(pc24MulW(-2.0, 0.0) == 0.0);
	CHECK(std::signbit(pc24MulW(-2.0, 0.0)));
	// fstp dword rounds a wide register to binary32 (nearest even), overflow to infinity, subnormals round again
	CHECK(fstpDword(1.0 + std::ldexp(1.0, -24)) == 1.0f);                         // a tie rounds to even
	CHECK(fstpDword(1.0 + std::ldexp(3.0, -24)) == 1.0f + std::ldexp(1.0f, -22)); // 1 + 3 * 2^-24 is above the tie
	CHECK(fstpDword(std::ldexp(1.0, 128)) == std::numeric_limits<float>::infinity());
	CHECK(fstpDword(std::ldexp(1.0, -149)) == std::ldexp(1.0f, -149));
	CHECK(fstpDword(std::ldexp(1.0, -151)) == 0.0f);
	// a wide product above the float range stays usable for the next operation: (2^100 * 2^100) / 2^100 = 2^100
	CHECK(pc24DivW(pc24MulW(std::ldexp(1.0, 100), std::ldexp(1.0, 100)), std::ldexp(1.0, 100)) == std::ldexp(1.0, 100));
	// the float entry points still narrow
	CHECK(pc24AddD(1.0, 2.0) == 3.0f);
}

TEST_CASE("facade fild of an unsigned frame count: values from 2^31 are rounded to 24 bits by the +2^32 fix-up")
{
	CHECK(fildU32(0) == 0.0);
	CHECK(fildU32(12345) == 12345.0);
	CHECK(fildU32(0x7FFFFFFFu) == 2147483647.0);
	CHECK(fildU32(0x80000000u) == 2147483648.0);
	CHECK(fildU32(0x80000001u) == 2147483648.0);       // 24 significant bits
	CHECK(fildU32(0xC0000000u) == 3221225472.0);       // exact
	CHECK(fildU32(0xC0000100u) == 3221225728.0);       // exact: the low bits are zero
	CHECK(fildU32(0xC0000080u) == 3221225472.0);       // 0x80 is half an ulp (0x100): ties to even
	CHECK(fildU32(0xFFFFFFFFu) == 4294967296.0);
}

TEST_CASE("facade SSE binary32 operations: one rounded operation per call, never fused")
{
	CHECK(sseAdd(1.0f, 2.0f) == 3.0f);
	CHECK(sseSub(1.0f, 3.0f) == -2.0f);
	CHECK(sseMul(1.5f, 4.0f) == 6.0f);
	CHECK(sseDiv(1.0f, 4.0f) == 0.25f);
	CHECK(sseFromInt32(16777217) == 16777216.0f); // round to nearest even
	CHECK(sseFromInt32(-3) == -3.0f);
	CHECK(sseMul(std::numeric_limits<float>::max(), 2.0f) == std::numeric_limits<float>::infinity()); // binary32: overflow, not a wide value
	// a product that an FMA would not round before the add: a * a - c with a = 1 + 2^-12 and c = the rounded square
	const float a = 1.0f + 1.0f / 4096.0f;
	const float rounded = sseMul(a, a);
	const float diff = sseSub(sseMul(a, a), rounded);
	CHECK(diff == 0.0f); // the fused form would give the lost low bits (2^-24)
	// bit-exact against exact double arithmetic rounded once
	const float xs[] = { 0.1f, 3.3333333f, 1.0e-20f, 123456.789f, -0.7f, 1.0f / 3.0f };
	for (float x : xs)
	{
		for (float y : xs)
		{
			CHECK(bitsOf(sseMul(x, y)) == bitsOf((float)((double)x * (double)y)));
			CHECK(bitsOf(sseAdd(x, y)) == bitsOf((float)((double)x + (double)y)));
			CHECK(bitsOf(sseSub(x, y)) == bitsOf((float)((double)x - (double)y)));
		}
	}
}

TEST_CASE("facade floor / ceil / fabs / sqrt without libm")
{
	const double samples[] = { 0.0, -0.0, 0.3, -0.3, 1.0, -1.0, 2.5, -2.5, 1234567.89, -1234567.89, 4503599627370495.5, 4503599627370496.0, 1.0e300, -1.0e300,
		0.9999999999999999, -0.9999999999999999 };
	for (double x : samples)
	{
		INFO("x = " << x);
		CHECK(bitsOf(floorD(x)) == bitsOf(std::floor(x)));
		CHECK(bitsOf(ceilD(x)) == bitsOf(std::ceil(x)));
		CHECK(bitsOf(absD(x)) == bitsOf(std::fabs(x)));
	}
	CHECK(std::isnan(floorD(std::numeric_limits<double>::quiet_NaN())));
	CHECK(floorD(std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity());
	CHECK(absD(-std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity());
	CHECK(std::signbit(ceilD(-0.5))); // ceil(-0.5) is -0
	// sqrt under PC24: a 24 bit result; exact roots stay exact
	CHECK(sqrtPC24(16.0) == 4.0);
	CHECK(sqrtPC24(0.0) == 0.0);
	CHECK(std::isnan(sqrtPC24(-1.0)));
	CHECK(sqrtPC24(2.0) == (double)(float)std::sqrt(2.0));
	CHECK(sqrtPC24(std::ldexp(1.0, 128)) == std::ldexp(1.0, 64));
	for (int i = 1; i < 2000; ++i)
	{
		const double x = (double)i * 0.37;
		CHECK(sqrtPC24(x) == (double)(float)std::sqrt(x)); // the 24-bit rounding of the correctly rounded root
	}
}

// ---- round 2 review: boundary vectors (expected bit patterns are the same on every compiler; the corpus runs on GCC / Linux and MSVC / Windows) ----

TEST_CASE("facade fsqrt under PC24 rounds the EXACT root once (integer square root), not a binary64 root rounded again")
{
	// review vector: 0x1.0000020000011p+0 has the exact root 1 + 2^-24 + 8.5 * 2^-53 - tiny, above the 24-bit midpoint 1 + 2^-24: PC24 fsqrt gives
	// 0x1.000002p+0; the binary64 root is the tie 1 + 2^-24 exactly, which rounds again to even (1.0)
	const double x = std::ldexp((double)0x10000020000011ull, -52);
	CHECK(bitsOf(sqrtPC24(x)) == bitsOf(std::ldexp((double)0x800001, -23)));
	CHECK(bitsOf(sqrtPC24(x)) == 0x3FF0000020000000ull); // 0x1.000002p+0
	// exact roots, powers of two (odd and even exponents), subnormal doubles, the double extremes
	CHECK(sqrtPC24(4.0) == 2.0);
	CHECK(sqrtPC24(2.25) == 1.5);
	CHECK(bitsOf(sqrtPC24(std::ldexp(1.0, -1074))) == bitsOf(std::ldexp(1.0, -537)));
	CHECK(bitsOf(sqrtPC24(std::ldexp(1.0, -1073))) == bitsOf(std::ldexp((double)(float)std::sqrt(2.0), -537))); // sqrt(2) * 2^-537, 24 bits
	CHECK(bitsOf(sqrtPC24(std::numeric_limits<double>::max())) == bitsOf(std::ldexp((double)(float)std::sqrt(std::ldexp(std::numeric_limits<double>::max(), -1000)), 500)));
	CHECK(sqrtPC24(std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity());
	CHECK(std::signbit(sqrtPC24(-0.0)));
	CHECK(std::isnan(sqrtPC24(-std::numeric_limits<double>::infinity())));
#if defined(__GNUC__) && LDBL_MANT_DIG == 64
	// an x87 long double root (64-bit significand) rounded once to float is the PC24 fsqrt result: a 53-bit input never has a root within 2^-64 of a
	// 24-bit midpoint without being on it, so the two roundings cannot disagree
	std::uint64_t state = 0x9E3779B97F4A7C15ull;
	for (int i = 0; i < 200000; ++i)
	{
		state = state * 6364136223846793005ull + 1442695040888963407ull;
		const std::uint64_t mant = (state >> 11) | (1ull << 52);
		const int exp = (int)((state >> 3) & 0x7F) - 64 + (int)(i % 3) - 1;
		const double x2 = std::ldexp((double)mant, exp - 52);
		INFO("x = " << std::hexfloat << x2);
		CHECK(bitsOf(sqrtPC24(x2)) == bitsOf((double)(float)std::sqrt((long double)x2)));
	}
#endif
}

TEST_CASE("facade fstp dword rounds a register value ONCE to binary32 (no preliminary 24-bit rounding)")
{
	// review vector: just below the midpoint between the largest subnormal float and the smallest normal one: hardware `fstp dword` gives 007fffff
	const double belowMid = std::nextafter(std::ldexp((double)0xFFFFFF, -150), 0.0); // 0x1.fffffep-127 is 0xFFFFFF * 2^-150
	CHECK(bitsOf(fstpDword(belowMid)) == 0x007FFFFFu);
	CHECK(bitsOf(fstpDword(std::ldexp((double)0xFFFFFF, -150))) == 0x00800000u); // the tie goes to even (the smallest normal)
	CHECK(bitsOf(fstpDword(std::nextafter(std::ldexp((double)0xFFFFFF, -150), 1.0))) == 0x00800000u);
	CHECK(bitsOf(fstpDword(std::ldexp(1.0, -149))) == 0x00000001u);
	CHECK(bitsOf(fstpDword(std::ldexp(1.0, -150))) == 0x00000000u);                   // a tie to even: zero
	CHECK(bitsOf(fstpDword(std::nextafter(std::ldexp(1.0, -150), 1.0))) == 0x00000001u);
	CHECK(bitsOf(fstpDword(-std::ldexp(3.0, -150))) == 0x80000002u);                  // 1.5 * 2^-149 ties to even (2)
	CHECK(bitsOf(fstpDword(std::ldexp(1.0, -1074))) == 0x00000000u);                  // a binary64 subnormal
	CHECK(bitsOf(fstpDword(3.4028235677973366e38)) == 0x7F800000u);                   // the first value that rounds to infinity
	CHECK(bitsOf(fstpDword(3.4028234663852886e38)) == 0x7F7FFFFFu);                   // FLT_MAX
	CHECK(bitsOf(fstpDword(0.1)) == bitsOf(0.1f));
	CHECK(bitsOf(fstpDword(std::ldexp((double)0x3FFFFFF, -25))) == 0x40000000u);      // a carry out of the significand: 2 - 2^-25 is 2.0
}

TEST_CASE("facade wide results are packed onto the binary64 grid: subnormal doubles are not flushed to zero")
{
	CHECK(bitsOf(pc24AddW(std::ldexp(1.0, -1023), 0.0)) == bitsOf(std::ldexp(1.0, -1023)));
	CHECK(bitsOf(pc24AddW(std::ldexp(1.0, -1023), 0.0)) == 0x0008000000000000ull);
	CHECK(bitsOf(pc24MulW(std::ldexp(1.0, -1000), std::ldexp(1.0, -30))) == bitsOf(std::ldexp(1.0, -1030)));
	// 0x1.000002p-1070 does not fit the subnormal grid (2^-1074): one rounding gives 2^-1070
	CHECK(bitsOf(pc24MulW(std::ldexp((double)0x800001, -1070), std::ldexp(1.0, -23))) == bitsOf(std::ldexp(1.0, -1070)));
	CHECK(bitsOf(pc24MulW(std::ldexp(1.0, -1074), 0.5)) == 0x0000000000000000ull); // the tie 2^-1075 goes to even: zero
	CHECK(bitsOf(pc24MulW(std::ldexp(1.0, -1074), 0.75)) == bitsOf(std::ldexp(1.0, -1074)));
	CHECK(bitsOf(pc24DivW(std::ldexp(1.0, -1070), 4.0)) == bitsOf(std::ldexp(1.0, -1072)));
	CHECK(bitsOf(pc24MulW(std::ldexp(1.0, 1000), std::ldexp(1.0, 100))) == 0x7FF0000000000000ull); // overflows the carrier
	// the W functions are carriers bounded by binary64 (documented in NumericState.h): 2^600 * 2^600 / 2^600 overflows here
	CHECK(pc24DivW(pc24MulW(std::ldexp(1.0, 600), std::ldexp(1.0, 600)), std::ldexp(1.0, 600)) == std::numeric_limits<double>::infinity());
}

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
namespace
{
struct ScopedControlRegister
{
	std::uint32_t saved = readControlRegister();
	~ScopedControlRegister() { writeControlRegister(saved); }
};
} // namespace

TEST_CASE("setFPMode establishes the canonical MXCSR: FTZ / DAZ off, exceptions masked, round to nearest")
{
	ScopedControlRegister restore;
	const float minNormal = std::numeric_limits<float>::min();
	const float half = 0.5f;
	// FTZ (bit 15) and DAZ (bit 6) on, upward rounding, the invalid-operation exception unmasked
	writeControlRegister(0x8040u | 0x4000u | (0x1F80u & ~0x0080u));
	REQUIRE(!floatingPointEnvironmentIsCanonical());
	CHECK(bitsOf(sseMul(minNormal, half)) == 0u); // the premise: flushed to zero
	setFPMode();
	CHECK(floatingPointEnvironmentIsCanonical());
	CHECK((readControlRegister() & 0xFFC0u) == kCanonicalControlRegister);
	CHECK(bitsOf(sseMul(minNormal, half)) == 0x00400000u); // gradual underflow: the subnormal 2^-127
	CHECK(bitsOf(sseMul(std::ldexp(1.0f, -127), 1.0f)) == 0x00400000u); // DAZ off: a subnormal operand is read as itself
	CHECK(bitsOf(sseAdd(std::ldexp(1.0f, -127), std::ldexp(1.0f, -127))) == 0x00800000u);

	writeControlRegister(kCanonicalControlRegister | 0x8000u);
	normalizeFloatingPointEnvironment();
	CHECK((readControlRegister() & 0xFFC0u) == kCanonicalControlRegister);

	writeControlRegister(kCanonicalControlRegister & ~0x1F80u); // every exception unmasked
	normalizeFloatingPointEnvironment();
	CHECK((readControlRegister() & 0x1F80u) == 0x1F80u);
	CHECK((readControlRegister() & 0x6000u) == 0u); // nearest
}
#endif

// ---- SimMath is THE simulation API: thin forwards, aliases and the retail results for invalid input ----

TEST_CASE("SimMath forwards to the facade: the LOGIC-1 names are aliases of the sse* / cvttss2si operations")
{
	const float vs[] = { 0.1f, -3.5f, 1.0e30f, 1.0e-40f, 0.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
	for (float a : vs)
	{
		for (float b : vs)
		{
			const auto same = [](float x, float y) {
				return (x != x && y != y) || bitsOf(x) == bitsOf(y);
			};
			CHECK(same(SimMath::addf32(a, b), sseAdd(a, b)));
			CHECK(same(SimMath::subf32(a, b), sseSub(a, b)));
			CHECK(same(SimMath::mulf32(a, b), sseMul(a, b)));
			CHECK(same(SimMath::divf32(a, b), sseDiv(a, b)));
		}
		CHECK(SimMath::truncToInt32(a) == cvttss2si(a));
	}
	CHECK(SimMath::truncToInt32(3.9f) == 3);
	CHECK(SimMath::truncToInt32(1.0e30f) == INT32_MIN);
	// the forwards are the same functions (not copies)
	CHECK(&SimMath::sseAdd == &NumericState::sseAdd);
	CHECK(&SimMath::pc24MulW == &NumericState::pc24MulW);
	CHECK(&SimMath::fstpDword == &NumericState::fstpDword);
	CHECK(&SimMath::sqrtPC24 == &NumericState::sqrtPC24);
	CHECK(&SimMath::ftol2Low32 == &NumericState::ftol2Low32);
	CHECK(&SimMath::fistp32 == &NumericState::fistp32);
}

TEST_CASE("SimMath::floorToInt / ceilToInt are floorD / ceilD + fistp32: the integer indefinite for invalid input")
{
	CHECK(SimMath::floorToInt(2.5f) == 2);
	CHECK(SimMath::floorToInt(-2.5f) == -3);
	CHECK(SimMath::ceilToInt(-0.5f) == 0);
	CHECK(SimMath::ceilToInt(2.0000002f) == 3);
	CHECK(SimMath::floorToInt(2147483520.0f) == 2147483520);    // the largest float below 2^31
	CHECK(SimMath::floorToInt(2147483648.0f) == INT32_MIN);     // out of range: indefinite, not UB
	CHECK(SimMath::ceilToInt(-2147483648.0f) == INT32_MIN);     // exactly INT_MIN
	CHECK(SimMath::ceilToInt(-2147483904.0f) == INT32_MIN);
	CHECK(SimMath::floorToInt(1.0e30f) == INT32_MIN);
	CHECK(SimMath::floorToInt(std::numeric_limits<float>::quiet_NaN()) == INT32_MIN);
	CHECK(SimMath::ceilToInt(std::numeric_limits<float>::infinity()) == INT32_MIN);
	CHECK(SimMath::ceilToInt(-std::numeric_limits<float>::infinity()) == INT32_MIN);
}

TEST_CASE("SimMath::length2d / length3d use the shared SSE sum of squares (each product and sum rounded, left to right)")
{
	const float x = 1.0f + 1.0f / 4096.0f;
	CHECK(bitsOf(SimMath::sumSquares2(x, x)) == bitsOf(sseAdd(sseMul(x, x), sseMul(x, x))));
	CHECK(bitsOf(SimMath::sumSquares3(x, 3.0f, x)) == bitsOf(sseAdd(sseAdd(sseMul(x, x), 9.0f), sseMul(x, x))));
	CHECK(SimMath::length2d(3.0f, 4.0f) == 5.0f);
	CHECK(SimMath::length3d(2.0f, 3.0f, 6.0f) == 7.0);
	CHECK(bitsOf(SimMath::length2d(0.1f, 0.2f)) == bitsOf((float)SimMath::sqrtd((double)SimMath::sumSquares2(0.1f, 0.2f))));
	CHECK(std::isnan(SimMath::length2d(std::numeric_limits<float>::quiet_NaN(), 1.0f)));
	CHECK(SimMath::length3d(1.0e30f, 1.0e30f, 1.0e30f) == std::numeric_limits<double>::infinity()); // the float sum overflows to infinity
}

TEST_CASE("numeric facade: the acceptance stops S-232 and S-233 are reported (docs/STOPS.md)")
{
	const std::vector<std::string> stops = numericStops();
	REQUIRE(stops.size() == 2);
	CHECK(stops[0].substr(0, 5) == "S-232");
	CHECK(stops[1].substr(0, 5) == "S-233");
	CHECK(stops[0].find("MSVC") != std::string::npos);
	CHECK(stops[1].find("binary64-bounded") != std::string::npos);
}
