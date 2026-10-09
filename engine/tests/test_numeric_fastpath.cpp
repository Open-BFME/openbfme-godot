// OpenBFME unit tests (lane MODULES-2, performance): the fast path of the PC24 facade (Common/System/NumericState.cpp: one binary64 operation on
// binary32 operands, rounded to binary32, when the result is a normal binary32 value in the canonical environment) gives the same bits as the integer
// emulation (NumericState::reference) for every input class: random bit patterns, operands and results at the binade edges (2^-126, 2^128), subnormal
// operands and results, exact rounding ties, non-binary32 doubles (53-bit significands, wide exponents), NaN, infinities and zeros.

#include "doctest.h"

#include "Common/NumericState.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <ios>
#include <vector>

namespace
{
using namespace NumericState;

struct Rng
{
	std::uint64_t s = 0x9E3779B97F4A7C15ull;
	std::uint64_t next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
	std::uint32_t u32() { return (std::uint32_t)(next() >> 16); }
};

std::uint32_t bitsOf(float f)
{
	std::uint32_t b;
	std::memcpy(&b, &f, sizeof(b));
	return b;
}
std::uint64_t bitsOf(double d)
{
	std::uint64_t b;
	std::memcpy(&b, &d, sizeof(b));
	return b;
}
float floatOf(std::uint32_t b)
{
	float f;
	std::memcpy(&f, &b, sizeof(f));
	return f;
}
double doubleOf(std::uint64_t b)
{
	double d;
	std::memcpy(&d, &b, sizeof(d));
	return d;
}

// a float drawn from one of the input classes
float sampleFloat(Rng &r)
{
	const std::uint32_t k = r.u32() % 8;
	const std::uint32_t mant = r.u32() & 0x7FFFFFu;
	const std::uint32_t sign = (r.u32() & 1u) << 31;
	switch (k)
	{
		case 0: return floatOf(r.u32()); // any pattern (NaN, infinities, subnormals included)
		case 1: return floatOf(sign | ((r.u32() % 6u) << 23) | mant);               // subnormal and the lowest normal binades
		case 2: return floatOf(sign | ((248u + r.u32() % 7u) << 23) | mant);        // the highest binades
		case 3: return floatOf(sign | ((60u + r.u32() % 8u) << 23) | mant);         // products / quotients land near 2^-126
		case 4: return floatOf(sign | ((180u + r.u32() % 16u) << 23) | mant);       // products near 2^128
		case 5: return floatOf(sign | ((120u + r.u32() % 16u) << 23) | (mant & 0x7FFF00u)); // short significands (exact ties in sums)
		case 6: return floatOf(sign | ((127u + r.u32() % 30u) << 23) | mant);       // game coordinates
		default: return floatOf(sign | ((100u + r.u32() % 60u) << 23) | mant);
	}
}

// a double: a float value, a 53-bit significand near one, a wide exponent beyond the float range, or any pattern
double sampleDouble(Rng &r)
{
	switch (r.u32() % 5)
	{
		case 0:
		case 1: return (double)sampleFloat(r);
		case 2: return doubleOf((std::uint64_t)(r.u32() & 0x80000000u) << 32 | (std::uint64_t)(1023u - 60u + r.u32() % 120u) << 52 | (r.next() & 0xFFFFFFFFFFFFFull));
		case 3: return doubleOf((std::uint64_t)(r.u32() & 0x80000000u) << 32 | (std::uint64_t)(r.u32() % 2u ? 1023u + 120u + r.u32() % 20u : 1023u - 140u - r.u32() % 20u) << 52 |
		                        ((r.next() & 0xFFFFFE0000000ull)));
		default: return doubleOf(r.next());
	}
}
} // namespace

TEST_CASE("numeric fast path: pc24 Add / Sub / Mul / Div (float, D, W), fstpDword and sqrtPC24 give the emulation's bits for every input class")
{
	normalizeFloatingPointEnvironment();
	Rng r;
	const int n = 400000;
	int diffs = 0;
	auto checkF = [&](const char *what, float fast, float ref, double a, double b) {
		if (bitsOf(fast) != bitsOf(ref) && ++diffs <= 10)
		{
			FAIL_CHECK(what << "(" << a << ", " << b << "): fast " << std::hex << bitsOf(fast) << " reference " << bitsOf(ref));
		}
	};
	auto checkD = [&](const char *what, double fast, double ref, double a, double b) {
		if (bitsOf(fast) != bitsOf(ref) && ++diffs <= 10)
		{
			FAIL_CHECK(what << "(" << a << ", " << b << "): fast " << std::hex << bitsOf(fast) << " reference " << bitsOf(ref));
		}
	};
	for (int i = 0; i < n; ++i)
	{
		const float a = sampleFloat(r), b = sampleFloat(r);
		checkF("pc24Add", pc24Add(a, b), reference::pc24Add(a, b), a, b);
		checkF("pc24Sub", pc24Sub(a, b), reference::pc24Sub(a, b), a, b);
		checkF("pc24Mul", pc24Mul(a, b), reference::pc24Mul(a, b), a, b);
		checkF("pc24Div", pc24Div(a, b), reference::pc24Div(a, b), a, b);
		const double x = sampleDouble(r), y = sampleDouble(r);
		checkF("pc24AddD", pc24AddD(x, y), reference::pc24AddD(x, y), x, y);
		checkF("pc24SubD", pc24SubD(x, y), reference::pc24SubD(x, y), x, y);
		checkF("pc24MulD", pc24MulD(x, y), reference::pc24MulD(x, y), x, y);
		checkF("pc24DivD", pc24DivD(x, y), reference::pc24DivD(x, y), x, y);
		checkD("pc24AddW", pc24AddW(x, y), reference::pc24AddW(x, y), x, y);
		checkD("pc24SubW", pc24SubW(x, y), reference::pc24SubW(x, y), x, y);
		checkD("pc24MulW", pc24MulW(x, y), reference::pc24MulW(x, y), x, y);
		checkD("pc24DivW", pc24DivW(x, y), reference::pc24DivW(x, y), x, y);
		checkF("fstpDword", fstpDword(x), reference::fstpDword(x), x, 0.0);
		checkD("sqrtPC24", sqrtPC24(x), reference::sqrtPC24(x), x, 0.0);
		const double fx = (double)a; // a binary32 operand, the fast path's case
		checkD("sqrtPC24(float)", sqrtPC24(fx), reference::sqrtPC24(fx), fx, 0.0);
		checkF("fstpDword(product)", fstpDword(x * y), reference::fstpDword(x * y), x, y);
	}
	// exact ties of a sum: m * 2^e plus half an ulp (and a quarter, three quarters), both rounding directions
	for (int i = 0; i < 100000; ++i)
	{
		const std::uint32_t m = 0x800000u | (r.u32() & 0x7FFFFFu);
		const int e = (int)(r.u32() % 200u) - 100;
		const float a = (float)std::ldexp((double)m, e);
		const float half = (float)std::ldexp(1.0, e - 1), quarter = (float)std::ldexp(1.0, e - 2);
		for (float b : { half, -half, quarter, 3.0f * quarter, -quarter })
		{
			checkF("tie pc24Add", pc24Add(a, b), reference::pc24Add(a, b), a, b);
			checkD("tie pc24AddW", pc24AddW(a, b), reference::pc24AddW(a, b), a, b);
		}
	}
	// the edges: results just below / at / above 2^-126 and at the top of the float range
	const float edges[] = { 1.1754942e-38f, 1.17549435e-38f, 1.1754945e-38f, 5.439772e-38f, 0.20226517f, 3.4028235e38f, 1.7014118e38f, 2.0f, 0.5f, 1.0e-20f, 1.0e-19f };
	for (float a : edges)
	{
		for (float b : edges)
		{
			checkF("edge pc24Mul", pc24Mul(a, b), reference::pc24Mul(a, b), a, b);
			checkF("edge pc24Div", pc24Div(a, b), reference::pc24Div(a, b), a, b);
			checkF("edge pc24Add", pc24Add(a, -b), reference::pc24Add(a, -b), a, -b);
			checkD("edge pc24MulW", pc24MulW(a, b), reference::pc24MulW(a, b), a, b);
		}
	}
	// the retail INI fact of the header: 5.439772e-38 * 0.20226517 is 0x0077CF3C under PC24 (a subnormal result: the emulation's)
	CHECK(bitsOf(pc24Mul(5.439772e-38f, 0.20226517f)) == 0x0077CF3Cu);
	CHECK(diffs == 0);
	// a non-canonical environment takes the emulation (the hardware conversions would round otherwise)
	const std::uint32_t saved = readControlRegister();
	if (saved != 0)
	{
		writeControlRegister((saved & ~0x6000u) | 0x6000u); // round toward zero
		CHECK_FALSE(floatingPointEnvironmentIsCanonical());
		const float a = 1.0f, b = 3.0f;
		CHECK(bitsOf(pc24Div(a, b)) == bitsOf(reference::pc24Div(a, b)));
		CHECK(bitsOf(fstpDword(1.0 / 3.0)) == bitsOf(reference::fstpDword(1.0 / 3.0)));
		normalizeFloatingPointEnvironment();
	}
}
