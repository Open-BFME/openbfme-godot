// OpenBFME unit tests. GPL-3.0. Lane DEP-2: the deterministic libm (Common/DeterministicLibm.h) and the Lua math library on top of it.
//
// 1. The same bits as the host glibc over a sweep, on the hosts where glibc runs its FMA variants (x86-64 Linux, glibc >= 2.29, a CPU with FMA
//    and AVX2), excluding compiler-dependent NaN results: what the simulation computed on Linux before DEP-2. Elsewhere it skips loudly (the host's
//    libm is a different function there); the pinned hashes of (2) still run.
// 2. The pinned hashes of the results over a fixed sweep: the same on every platform built and run (Linux and Windows, x86-64 with and without FMA, AArch64 under QEMU; macOS not run).
// 3. The special values (NaN, infinities, zeros, domain errors) bit for bit.
// 4. The game's Lua runtime: sin / cos / tan / asin / acos / atan2 / exp / log / log10 and `^` give these bits, on inputs where glibc's result
//    is not the correctly rounded one (MPFR, lane DEP-2), so a correctly rounded or any other libm would fail.

#include "doctest.h"
#ifndef DEP2_PORTABLE_HARNESS
#include "LuaTestUtil.h"
#endif

#include "Common/DeterministicLibm.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__)
#include <gnu/libc-version.h>
#define DEP2_GLIBC_X64 1
#else
#define DEP2_GLIBC_X64 0
#endif

namespace
{
std::uint64_t bitsOf(double x)
{
	std::uint64_t u;
	std::memcpy(&u, &x, sizeof u);
	return u;
}

double fromBits(std::uint64_t u)
{
	double x;
	std::memcpy(&x, &u, sizeof x);
	return x;
}

struct Rng
{
	std::uint64_t s;
	std::uint64_t next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
	double uniform(double lo, double hi) { return lo + (hi - lo) * ((double)(next() >> 11) * 0x1.0p-53); }
	double anyBits() { return fromBits(next()); }
};

enum class Fn
{
	Sin, Cos, Tan, Asin, Acos, Atan, Atan2, Exp, Log, Log10, Pow, Sqrt, Floor, Ceil, Fmod, Ldexp, Frexp
};

// frexp's two results as one value for the comparison and the hash: the mantissa's bits with the exponent folded in
double packFrexp(double m, int e)
{
	return fromBits(bitsOf(m) ^ ((std::uint64_t)(std::uint32_t)e << 17));
}

double ours(Fn f, double x, double y)
{
	switch (f)
	{
	case Fn::Sin: return DetLibm::sin(x);
	case Fn::Cos: return DetLibm::cos(x);
	case Fn::Tan: return DetLibm::tan(x);
	case Fn::Asin: return DetLibm::asin(x);
	case Fn::Acos: return DetLibm::acos(x);
	case Fn::Atan: return DetLibm::atan(x);
	case Fn::Atan2: return DetLibm::atan2(x, y);
	case Fn::Exp: return DetLibm::exp(x);
	case Fn::Log: return DetLibm::log(x);
	case Fn::Log10: return DetLibm::log10(x);
	case Fn::Pow: return DetLibm::pow(x, y);
	case Fn::Sqrt: return DetLibm::sqrt(x);
	case Fn::Floor: return DetLibm::floor(x);
	case Fn::Ceil: return DetLibm::ceil(x);
	case Fn::Fmod: return DetLibm::fmod(x, y);
	case Fn::Ldexp: return DetLibm::ldexp(x, (int)y);
	case Fn::Frexp:
	{
		int e = 0;
		const double m = DetLibm::frexp(x, &e);
		return packFrexp(m, e);
	}
	}
	return 0;
}

// one range of the sweep: x (and y for atan2 / pow) uniform in [lo, hi), or random bit patterns (every exponent, NaN and infinities included)
struct Range
{
	const char *name;
	Fn fn;
	double lo, hi, ylo, yhi;
	bool bits;
};

const Range kRanges[] = {
	{ "sin [-10,10)", Fn::Sin, -10, 10, 0, 0, false },
	{ "sin [-1e9,1e9)", Fn::Sin, -1e9, 1e9, 0, 0, false },
	{ "sin bits", Fn::Sin, 0, 0, 0, 0, true },
	{ "cos [-10,10)", Fn::Cos, -10, 10, 0, 0, false },
	{ "cos [-1e9,1e9)", Fn::Cos, -1e9, 1e9, 0, 0, false },
	{ "cos bits", Fn::Cos, 0, 0, 0, 0, true },
	{ "tan [-10,10)", Fn::Tan, -10, 10, 0, 0, false },
	{ "tan [-1e8,1e8)", Fn::Tan, -1e8, 1e8, 0, 0, false },
	{ "tan bits", Fn::Tan, 0, 0, 0, 0, true },
	{ "asin [-1,1)", Fn::Asin, -1, 1, 0, 0, false },
	{ "asin bits", Fn::Asin, 0, 0, 0, 0, true },
	{ "acos [-1,1)", Fn::Acos, -1, 1, 0, 0, false },
	{ "acos bits", Fn::Acos, 0, 0, 0, 0, true },
	{ "atan [-100,100)", Fn::Atan, -100, 100, 0, 0, false },
	{ "atan bits", Fn::Atan, 0, 0, 0, 0, true },
	{ "atan2 [-100,100)^2", Fn::Atan2, -100, 100, -100, 100, false },
	{ "atan2 bits", Fn::Atan2, 0, 0, 0, 0, true },
	{ "exp [-745,710)", Fn::Exp, -745, 710, 0, 0, false },
	{ "exp [-2,2)", Fn::Exp, -2, 2, 0, 0, false },
	{ "exp bits", Fn::Exp, 0, 0, 0, 0, true },
	{ "log (0,1e6)", Fn::Log, 0, 1e6, 0, 0, false },
	{ "log [0.9,1.1)", Fn::Log, 0.9, 1.1, 0, 0, false },
	{ "log bits", Fn::Log, 0, 0, 0, 0, true },
	{ "log10 (0,1e6)", Fn::Log10, 0, 1e6, 0, 0, false },
	{ "log10 bits", Fn::Log10, 0, 0, 0, 0, true },
	{ "pow [0,1000) x [-50,50)", Fn::Pow, 0, 1000, -50, 50, false },
	{ "pow [0,2) x [-5,5)", Fn::Pow, 0, 2, -5, 5, false },
	{ "pow [-100,0) x integers", Fn::Pow, -100, 0, -30, 30, false },
	{ "pow bits", Fn::Pow, 0, 0, 0, 0, true },
	{ "sqrt [0,1e6)", Fn::Sqrt, 0, 1e6, 0, 0, false },
	{ "sqrt bits", Fn::Sqrt, 0, 0, 0, 0, true },
	{ "floor [-1e6,1e6)", Fn::Floor, -1e6, 1e6, 0, 0, false },
	{ "floor bits", Fn::Floor, 0, 0, 0, 0, true },
	{ "ceil [-1e6,1e6)", Fn::Ceil, -1e6, 1e6, 0, 0, false },
	{ "ceil bits", Fn::Ceil, 0, 0, 0, 0, true },
	{ "fmod [-1e4,1e4) x [-100,100)", Fn::Fmod, -1e4, 1e4, -100, 100, false },
	{ "fmod bits", Fn::Fmod, 0, 0, 0, 0, true },
	{ "ldexp bits x [-2200,2200)", Fn::Ldexp, 0, 0, -2200, 2200, true },
	{ "frexp bits", Fn::Frexp, 0, 0, 0, 0, true },
};

void draw(const Range &r, Rng &rng, double &x, double &y)
{
	if (r.bits)
	{
		x = rng.anyBits();
		y = rng.anyBits();
		if (r.fn == Fn::Ldexp)
		{
			y = rng.uniform(r.ylo, r.yhi); // the exponent argument is an int
		}
		return;
	}
	x = rng.uniform(r.lo, r.hi);
	y = rng.uniform(r.ylo, r.yhi);
	if (r.fn == Fn::Pow && r.lo < 0)
	{
		y = std::trunc(y); // a negative base needs an integral exponent
	}
}

// FNV-1a over the result bits of `count` draws of every range
std::uint64_t sweepHash(const Range &r, int count)
{
	Rng rng{ 0x9E3779B97F4A7C15ull };
	std::uint64_t h = 1469598103934665603ull;
	for (int i = 0; i < count; ++i)
	{
		double x, y;
		draw(r, rng, x, y);
		const std::uint64_t b = bitsOf(ours(r.fn, x, y));
		for (int k = 0; k < 8; ++k)
		{
			h = (h ^ ((b >> (8 * k)) & 0xFF)) * 1099511628211ull;
		}
	}
	return h;
}

double host(Fn f, double x, double y)
{
	switch (f)
	{
	case Fn::Sin: return std::sin(x);
	case Fn::Cos: return std::cos(x);
	case Fn::Tan: return std::tan(x);
	case Fn::Asin: return std::asin(x);
	case Fn::Acos: return std::acos(x);
	case Fn::Atan: return std::atan(x);
	case Fn::Atan2: return std::atan2(x, y);
	case Fn::Exp: return std::exp(x);
	case Fn::Log: return std::log(x);
	case Fn::Log10: return std::log10(x);
	case Fn::Pow: return std::pow(x, y);
	case Fn::Sqrt: return std::sqrt(x);
	case Fn::Floor: return std::floor(x);
	case Fn::Ceil: return std::ceil(x);
	case Fn::Fmod: return std::fmod(x, y);
	case Fn::Ldexp: return std::ldexp(x, (int)y);
	case Fn::Frexp:
	{
		int e = 0;
		const double m = std::frexp(x, &e);
		return packFrexp(m, e);
	}
	}
	return 0;
}

bool hostComparable(Fn f, double x, double y)
{
	if (f == Fn::Fmod && std::isnan(x) && std::isnan(y))
	{
		return false; // glibc's pick of two NaN operands is its compiler's operand order; ours is pinned below
	}
	const std::uint64_t u = bitsOf(x) & 0x7FFFFFFFFFFFFFFFull;
	if ((f == Fn::Floor || f == Fn::Ceil) && (u & 0x7FF8000000000000ull) == 0x7FF0000000000000ull && (u & 0x000FFFFFFFFFFFFFull) != 0)
	{
		// GCC's inline floor / ceil preserve sNaN; Clang's calls into glibc quieten it. The port's unchanged bits are pinned below.
		return false;
	}
	return true;
}

#if DEP2_GLIBC_X64
// glibc runs its *_fma variants (the ones the port reproduces) when the CPU has FMA and AVX2 (sysdeps/x86_64/fpu/multiarch/ifunc-fma.h); exp, log
// and pow are the 2.29 versions when the headers and the library are 2.29 or newer
std::string hostSkipReason()
{
	int major = 0, minor = 0;
	if (std::sscanf(gnu_get_libc_version(), "%d.%d", &major, &minor) != 2 || major < 2 || (major == 2 && minor < 29) || __GLIBC__ < 2 ||
		(__GLIBC__ == 2 && __GLIBC_MINOR__ < 29))
	{
		return std::string("glibc ") + gnu_get_libc_version() + " is older than 2.29";
	}
	__builtin_cpu_init();
	if (!__builtin_cpu_supports("fma") || !__builtin_cpu_supports("avx2"))
	{
		return "the CPU lacks FMA or AVX2, so glibc runs its non-FMA variants";
	}
	return "";
}
#endif
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
// the CPU's vfmadd231sd (only called when the CPU has it)
__attribute__((target("fma"))) double fmaInstruction(double a, double b, double c)
{
	return __builtin_fma(a, b, c);
}
#endif

const std::uint64_t kFmaSweepHash = 0x57090d85e9fbd0e4ull; // computed on the Deck; equal to the hash of the CPU's FMA instruction there
} // namespace

TEST_CASE("dep2 libm: the same bits as the host glibc's FMA variants over a sweep of every function (what Linux computed before DEP-2)")
{
#if DEP2_GLIBC_X64
	const std::string skip = hostSkipReason();
	if (!skip.empty())
	{
		MESSAGE("SKIP dep2 host comparison: " << skip << " (the pinned hashes below still run)");
		return;
	}
	const int perRange = 200000;
	long compared = 0, excluded = 0;
	for (const Range &r : kRanges)
	{
		Rng rng{ 0xD1B54A32D192ED03ull };
		long mismatches = 0;
		std::string first;
		for (int i = 0; i < perRange; ++i)
		{
			double x, y;
			draw(r, rng, x, y);
			if (!hostComparable(r.fn, x, y))
			{
				++excluded;
				continue;
			}
			const double a = ours(r.fn, x, y);
			const double b = host(r.fn, x, y);
			++compared;
			if (bitsOf(a) != bitsOf(b))
			{
				if (mismatches == 0)
				{
					char line[200];
					std::snprintf(line, sizeof line, "(%016llx, %016llx): ours %016llx, host %016llx", (unsigned long long)bitsOf(x),
						(unsigned long long)bitsOf(y), (unsigned long long)bitsOf(a), (unsigned long long)bitsOf(b));
					first = line;
				}
				++mismatches;
			}
		}
		INFO(std::string(r.name) << ": first difference " << first);
		CHECK(mismatches == 0);
	}
	MESSAGE(compared << " results compared with the host glibc " << std::string(gnu_get_libc_version()) << "; " << excluded
		<< " compiler-dependent NaN results excluded from the host comparison (the port's bits are pinned)");
#else
	// information only: how often this host's libm (another library) disagrees with what Linux computed, the desync the port removes
	long differ = 0, compared = 0;
	for (const Range &r : kRanges)
	{
		Rng rng{ 0xD1B54A32D192ED03ull };
		for (int i = 0; i < 20000; ++i)
		{
			double x, y;
			draw(r, rng, x, y);
			if (!hostComparable(r.fn, x, y))
			{
				continue;
			}
			differ += bitsOf(ours(r.fn, x, y)) != bitsOf(host(r.fn, x, y)) ? 1 : 0;
			++compared;
		}
	}
	MESSAGE("SKIP dep2 host comparison: not x86-64 Linux with glibc (the host's libm is a different function here; the pinned hashes below still "
		"run). Information: this host's libm differs from the port on " << differ << " of " << compared << " results");
#endif
}

TEST_CASE("dep2 libm: the pinned hashes of every function's results over a fixed sweep (the same bits on every platform run: Linux, Windows, x86-64 without FMA, AArch64)")
{
	// computed with the port on the Deck (glibc 2.41), where the host libm gives the same hashes (and test 1 shows the same bits)
	struct Pin
	{
		const char *name;
		std::uint64_t hash;
	};
	const Pin pins[] = {
		{ "sin [-10,10)", 0xd0bb508dbea4c366ull },
		{ "sin [-1e9,1e9)", 0x3a0a226049229307ull },
		{ "sin bits", 0xfe8a44f452bcb89dull },
		{ "cos [-10,10)", 0x2ad37b78184c979cull },
		{ "cos [-1e9,1e9)", 0x3c97fdf0a611d694ull },
		{ "cos bits", 0xba4861bebfbe1c55ull },
		{ "tan [-10,10)", 0x85c548a3da74a28full },
		{ "tan [-1e8,1e8)", 0xce80bfdf34217a70ull },
		{ "tan bits", 0xb22c13c7616ff21dull },
		{ "asin [-1,1)", 0x231857cd93937eb6ull },
		{ "asin bits", 0x53476acbdac942ceull },
		{ "acos [-1,1)", 0xad3e3f40b2304aacull },
		{ "acos bits", 0xf4a649b2cf676c7aull },
		{ "atan [-100,100)", 0x7787acc8f3887f0bull },
		{ "atan bits", 0xa568725db743d0e1ull },
		{ "atan2 [-100,100)^2", 0x174338b2b36c54deull },
		{ "atan2 bits", 0xc4e31e2076044da2ull },
		{ "exp [-745,710)", 0x42682f69f49d06efull },
		{ "exp [-2,2)", 0x8951c3d5476aea96ull },
		{ "exp bits", 0x8fa6b85d971e6aa6ull },
		{ "log (0,1e6)", 0xbcef04ee1e4c20dfull },
		{ "log [0.9,1.1)", 0x1762385669eb527dull },
		{ "log bits", 0x6b270daca0730a12ull },
		{ "log10 (0,1e6)", 0xf680fdd2f5c0bee0ull },
		{ "log10 bits", 0x45145d3c21873656ull },
		{ "pow [0,1000) x [-50,50)", 0x265fc2fd68f2606eull },
		{ "pow [0,2) x [-5,5)", 0x72620adbab40cd01ull },
		{ "pow [-100,0) x integers", 0x4590df65cdf8a7f5ull },
		{ "pow bits", 0xc064dc3729b42fa2ull },
		{ "sqrt [0,1e6)", 0xa96f802e43773439ull },
		{ "sqrt bits", 0x67d2de80ba7549ecull },
		{ "floor [-1e6,1e6)", 0x3641822511ba75a5ull },
		{ "floor bits", 0x34faf9442e6050aaull },
		{ "ceil [-1e6,1e6)", 0x2bd68d7c5d2655c4ull },
		{ "ceil bits", 0x4dafd8e459120826ull },
		{ "fmod [-1e4,1e4) x [-100,100)", 0x974433a828176875ull },
		{ "fmod bits", 0x892ed101ecd24af5ull },
		{ "ldexp bits x [-2200,2200)", 0xe303b205f8c6b1dbull },
		{ "frexp bits", 0xc789f9627e573fc4ull },
	};
	REQUIRE(sizeof pins / sizeof pins[0] == sizeof kRanges / sizeof kRanges[0]);
	for (std::size_t i = 0; i < sizeof pins / sizeof pins[0]; ++i)
	{
		INFO(kRanges[i].name);
		REQUIRE(std::string(pins[i].name) == kRanges[i].name);
		CHECK(sweepHash(kRanges[i], 4096) == pins[i].hash);
	}
}

TEST_CASE("dep2 libm: special values bit for bit: NaN propagation, invalid operations, infinities and zeros")
{
	const double inf = INFINITY;
	const std::uint64_t defaultNaN = 0xFFF8000000000000ull; // SSE2's result of an invalid operation (inf - inf, 0 / 0)
	const std::uint64_t positiveNaN = 0x7FF8000000000000ull; // glibc's NAN, returned by the SVID wrappers of asin, acos, log10
	const double sNaN = fromBits(0x7FF0000000000001ull);
	const double qNaN = fromBits(0x7FF8000000000123ull);
	struct Case
	{
		const char *what;
		double got;
		std::uint64_t want;
	};
	const Case cases[] = {
		{ "sin(inf)", DetLibm::sin(inf), defaultNaN },
		{ "cos(-inf)", DetLibm::cos(-inf), defaultNaN },
		{ "tan(inf)", DetLibm::tan(inf), defaultNaN },
		{ "sin(sNaN)", DetLibm::sin(sNaN), 0x7FF8000000000001ull },
		{ "sin(-0)", DetLibm::sin(-0.0), 0x8000000000000000ull },
		{ "cos(0)", DetLibm::cos(0.0), bitsOf(1.0) },
		{ "tan(-0)", DetLibm::tan(-0.0), 0x8000000000000000ull },
		{ "asin(2)", DetLibm::asin(2.0), positiveNaN },
		{ "acos(-inf)", DetLibm::acos(-inf), positiveNaN },
		{ "asin(qNaN)", DetLibm::asin(qNaN), bitsOf(qNaN) },
		{ "asin(1)", DetLibm::asin(1.0), 0x3FF921FB54442D18ull },
		{ "acos(-1)", DetLibm::acos(-1.0), 0x400921FB54442D18ull },
		{ "atan(inf)", DetLibm::atan(inf), 0x3FF921FB54442D18ull },
		{ "atan(sNaN)", DetLibm::atan(sNaN), 0x7FF8000000000001ull },
		{ "atan2(0, -0)", DetLibm::atan2(0.0, -0.0), 0x400921FB54442D18ull },
		{ "atan2(-0, -0)", DetLibm::atan2(-0.0, -0.0), 0xC00921FB54442D18ull },
		{ "atan2(-0, 1)", DetLibm::atan2(-0.0, 1.0), 0x8000000000000000ull },
		{ "atan2(inf, -inf)", DetLibm::atan2(inf, -inf), 0x4002D97C7F3321D2ull },
		{ "atan2(qNaN, sNaN)", DetLibm::atan2(qNaN, sNaN), 0x7FF8000000000001ull },
		{ "exp(-inf)", DetLibm::exp(-inf), 0 },
		{ "exp(1000)", DetLibm::exp(1000.0), bitsOf(inf) },
		{ "exp(-1000)", DetLibm::exp(-1000.0), 0 },
		{ "exp(qNaN)", DetLibm::exp(qNaN), bitsOf(qNaN) },
		{ "exp(-745.1)", DetLibm::exp(-745.1), 1 }, // the smallest subnormal
		{ "log(0)", DetLibm::log(0.0), bitsOf(-inf) },
		{ "log(-1)", DetLibm::log(-1.0), defaultNaN },
		{ "log(inf)", DetLibm::log(inf), bitsOf(inf) },
		{ "log(1)", DetLibm::log(1.0), 0 },
		{ "log10(-0)", DetLibm::log10(-0.0), bitsOf(-inf) },
		{ "log10(-1)", DetLibm::log10(-1.0), positiveNaN },
		{ "log10(inf)", DetLibm::log10(inf), bitsOf(inf) },
		{ "log10(1000)", DetLibm::log10(1000.0), bitsOf(3.0) },
		{ "pow(-8, 1/3)", DetLibm::pow(-8.0, 1.0 / 3.0), defaultNaN },
		{ "pow(-0, -1)", DetLibm::pow(-0.0, -1.0), bitsOf(-inf) },
		{ "pow(-2, 3)", DetLibm::pow(-2.0, 3.0), bitsOf(-8.0) },
		{ "pow(sNaN, 0)", DetLibm::pow(sNaN, 0.0), 0x7FF8000000000001ull },
		{ "pow(qNaN, 0)", DetLibm::pow(qNaN, 0.0), bitsOf(1.0) },
		{ "pow(1, qNaN)", DetLibm::pow(1.0, qNaN), bitsOf(1.0) },
		{ "pow(2, 1024)", DetLibm::pow(2.0, 1024.0), bitsOf(inf) },
		{ "pow(2, -1075)", DetLibm::pow(2.0, -1075.0), 0 },
		{ "pow(2, -1074)", DetLibm::pow(2.0, -1074.0), 1 },
		// signed zeros and infinities of every function
		{ "sin(+0)", DetLibm::sin(0.0), 0 },
		{ "tan(+inf)", DetLibm::tan(inf), defaultNaN },
		{ "asin(-0)", DetLibm::asin(-0.0), 0x8000000000000000ull },
		{ "acos(qNaN)", DetLibm::acos(qNaN), bitsOf(qNaN) },
		{ "atan(-0)", DetLibm::atan(-0.0), 0x8000000000000000ull },
		{ "atan(-inf)", DetLibm::atan(-inf), 0xBFF921FB54442D18ull },
		{ "atan2(-0, +0)", DetLibm::atan2(-0.0, 0.0), 0x8000000000000000ull },
		{ "atan2(-inf, +inf)", DetLibm::atan2(-inf, inf), 0xBFE921FB54442D18ull },
		{ "atan2(1, sNaN)", DetLibm::atan2(1.0, sNaN), 0x7FF8000000000001ull },
		{ "exp(-0)", DetLibm::exp(-0.0), bitsOf(1.0) },
		{ "exp(sNaN)", DetLibm::exp(sNaN), 0x7FF8000000000001ull },
		{ "log(-0)", DetLibm::log(-0.0), bitsOf(-inf) },
		{ "log(-inf)", DetLibm::log(-inf), defaultNaN },
		{ "log(sNaN)", DetLibm::log(sNaN), 0x7FF8000000000001ull },
		{ "log10(0)", DetLibm::log10(0.0), bitsOf(-inf) },
		{ "log10(-inf)", DetLibm::log10(-inf), positiveNaN },
		{ "log10(qNaN)", DetLibm::log10(qNaN), bitsOf(qNaN) },
		{ "pow(-0, 3)", DetLibm::pow(-0.0, 3.0), 0x8000000000000000ull },
		{ "pow(-0, 2)", DetLibm::pow(-0.0, 2.0), 0 },
		{ "pow(-inf, 3)", DetLibm::pow(-inf, 3.0), bitsOf(-inf) },
		{ "pow(-inf, -3)", DetLibm::pow(-inf, -3.0), 0x8000000000000000ull },
		{ "pow(-1, inf)", DetLibm::pow(-1.0, inf), bitsOf(1.0) },
		{ "pow(0.5, -inf)", DetLibm::pow(0.5, -inf), bitsOf(inf) },
		{ "pow(qNaN, sNaN)", DetLibm::pow(qNaN, sNaN), bitsOf(qNaN) },
		// the exact functions
		{ "sqrt(-0)", DetLibm::sqrt(-0.0), 0x8000000000000000ull },
		{ "sqrt(-1)", DetLibm::sqrt(-1.0), defaultNaN },
		{ "sqrt(-inf)", DetLibm::sqrt(-inf), defaultNaN },
		{ "sqrt(inf)", DetLibm::sqrt(inf), bitsOf(inf) },
		{ "sqrt(sNaN)", DetLibm::sqrt(sNaN), 0x7FF8000000000001ull },
		{ "sqrt(2)", DetLibm::sqrt(2.0), 0x3FF6A09E667F3BCDull },
		{ "floor(-0)", DetLibm::floor(-0.0), 0x8000000000000000ull },
		{ "floor(-0.5)", DetLibm::floor(-0.5), bitsOf(-1.0) },
		{ "floor(sNaN)", DetLibm::floor(sNaN), 0x7FF0000000000001ull }, // the port preserves NaNs, including the signaling bit
		{ "ceil(-0.5)", DetLibm::ceil(-0.5), 0x8000000000000000ull },
		{ "ceil(0.5)", DetLibm::ceil(0.5), bitsOf(1.0) },
		{ "ceil(-inf)", DetLibm::ceil(-inf), bitsOf(-inf) },
		{ "fmod(-5, 3)", DetLibm::fmod(-5.0, 3.0), bitsOf(-2.0) },
		{ "fmod(-0, 1)", DetLibm::fmod(-0.0, 1.0), 0x8000000000000000ull },
		{ "fmod(-6, 3)", DetLibm::fmod(-6.0, 3.0), 0x8000000000000000ull },
		{ "fmod(1, inf)", DetLibm::fmod(1.0, inf), bitsOf(1.0) },
		{ "fmod(inf, 1)", DetLibm::fmod(inf, 1.0), defaultNaN },
		{ "fmod(1, 0)", DetLibm::fmod(1.0, 0.0), defaultNaN },
		{ "fmod(qNaN, sNaN)", DetLibm::fmod(qNaN, sNaN), bitsOf(qNaN) }, // two NaN operands: the first (not glibc's contract)
		{ "fmod(1, sNaN)", DetLibm::fmod(1.0, sNaN), 0x7FF8000000000001ull },
		{ "ldexp(1, -1075)", DetLibm::ldexp(1.0, -1075), 0 },
		{ "ldexp(1.5, -1074)", DetLibm::ldexp(1.5, -1074), 2 }, // a tie, to even
		{ "ldexp(-1, 1024)", DetLibm::ldexp(-1.0, 1024), bitsOf(-inf) },
		{ "ldexp(-0, 5)", DetLibm::ldexp(-0.0, 5), 0x8000000000000000ull },
		{ "ldexp(sNaN, 1)", DetLibm::ldexp(sNaN, 1), 0x7FF8000000000001ull },
		{ "fma(inf, 0, 1)", DetLibm::fma(inf, 0.0, 1.0), defaultNaN },
		{ "fma(inf, 1, -inf)", DetLibm::fma(inf, 1.0, -inf), defaultNaN },
		{ "fma(inf, 0, qNaN)", DetLibm::fma(inf, 0.0, qNaN), bitsOf(qNaN) },
		{ "fma(-0, 1, +0)", DetLibm::fma(-0.0, 1.0, 0.0), 0 },
		{ "fma(-0, 1, -0)", DetLibm::fma(-0.0, 1.0, -0.0), 0x8000000000000000ull },
		{ "fma(1+2^-52, 1-2^-52, -1)", DetLibm::fma(1.0 + 0x1p-52, 1.0 - 0x1p-52, -1.0), bitsOf(-0x1p-104) },
		{ "fma(2^-600, 2^-600, 0)", DetLibm::fma(0x1p-600, 0x1p-600, 0.0), 0 },
		{ "fma(-2^-537, 2^-537, -0)", DetLibm::fma(-0x1p-537, 0x1p-537, -0.0), 0x8000000000000001ull },
	};
	int e = 7;
	CHECK(bitsOf(DetLibm::frexp(-0.0, &e)) == 0x8000000000000000ull);
	CHECK(e == 0);
	CHECK(bitsOf(DetLibm::frexp(fromBits(1), &e)) == bitsOf(0.5));
	CHECK(e == -1073);
	CHECK(bitsOf(DetLibm::frexp(-inf, &e)) == bitsOf(-inf));
	CHECK(e == 0);
	for (const Case &c : cases)
	{
		INFO(c.what);
		CHECK(bitsOf(c.got) == c.want);
	}
}

TEST_CASE("dep2 libm: floor and ceil preserve NaN sign, payload and signaling bit independently of the host")
{
	const std::uint64_t inputs[] = {
		0x7FF0000000000001ull, 0xFFF0000000000001ull,
		0x7FF31C670B4FA197ull, 0xFFF31C670B4FA197ull,
		0x7FF8000000000001ull, 0xFFF8000000000001ull,
		0x7FFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
	};
	for (std::uint64_t u : inputs)
	{
		INFO(u);
		CHECK(bitsOf(DetLibm::floor(fromBits(u))) == u);
		CHECK(bitsOf(DetLibm::ceil(fromBits(u))) == u);
	}
}

// (DEP2_PORTABLE_HARNESS: the cases above and below also build without the engine, with a minimal doctest stand-in, for runs on emulated CPUs)
#ifndef DEP2_PORTABLE_HARNESS
TEST_CASE("dep2 libm: the game's Lua runtime computes sin / cos / tan / asin / acos / atan / atan2 / exp / log / log10 and ^ with these bits (glibc's, not the correctly rounded ones)")
{
	LuaReportSink sink;
	LuaRuntime rt{ sink, "dep2" };
	lua_State *L = rt.state();
	REQUIRE(L);
	// lmathlib.c works in degrees: TORAD (a) = a * (PI / 180.0), FROMRAD (a) = a / (PI / 180.0)
	const double rpd = 3.14159265358979323846 / 180.0;
	struct Case
	{
		const char *code;
		std::uint64_t want;     // pinned: the bits the Lua global holds
		double fromPort;        // the same value through the port
		std::uint64_t radians;  // the function's result before FROMRAD, where glibc's differs from the correctly rounded one (MPFR)
		std::uint64_t correctlyRounded;
	};
	const Case cases[] = {
		{ "v = sin(297)", 0xBFEC83201D3D2C6Eull, DetLibm::sin(297.0 * rpd), 0xBFEC83201D3D2C6Eull, 0xBFEC83201D3D2C6Dull },
		{ "v = cos(101.5)", 0xBFC984E37318B212ull, DetLibm::cos(101.5 * rpd), 0xBFC984E37318B212ull, 0xBFC984E37318B211ull },
		{ "v = tan(72)", 0x40089F188BDCD7AEull, DetLibm::tan(72.0 * rpd), 0x40089F188BDCD7AEull, 0x40089F188BDCD7ADull },
		{ "v = asin(0.115234375)", 0, DetLibm::asin(0.115234375) / rpd, 0x3FBD90D0803393B9ull, 0x3FBD90D0803393BAull },
		{ "v = acos(0.8125)", 0, DetLibm::acos(0.8125) / rpd, 0x3FE3EA71520CF3D4ull, 0x3FE3EA71520CF3D3ull },
		{ "v = atan2(0.375, 3.5)", 0, DetLibm::atan2(0.375, 3.5) / rpd, 0x3FBB53076F5837A8ull, 0x3FBB53076F5837A9ull },
		{ "v = atan(1)", bitsOf(45.0), DetLibm::atan(1.0) / rpd, 0, 0 },
		{ "v = exp(65.484375)", 0x45D63917ED783568ull, DetLibm::exp(65.484375), 0x45D63917ED783568ull, 0x45D63917ED783569ull },
		{ "v = log(408.234375)", 0x40180C202ACB6877ull, DetLibm::log(408.234375), 0x40180C202ACB6877ull, 0x40180C202ACB6876ull },
		{ "v = log10(1.0625)", 0x3F9AF5F92B00E60Full, DetLibm::log10(1.0625), 0x3F9AF5F92B00E60Full, 0x3F9AF5F92B00E610ull },
		{ "v = 0.75 ^ 9.875", 0x3FADE361DEC9E961ull, DetLibm::pow(0.75, 9.875), 0x3FADE361DEC9E961ull, 0x3FADE361DEC9E962ull },
		// the exact functions' exceptional results (no host NaN rules)
		{ "v = sqrt(-1)", 0xFFF8000000000000ull, DetLibm::sqrt(-1.0), 0, 0 },
		{ "v = mod(5, 0)", 0xFFF8000000000000ull, DetLibm::fmod(5.0, 0.0), 0, 0 },
		{ "v = mod(-6, 3)", 0x8000000000000000ull, DetLibm::fmod(-6.0, 3.0), 0, 0 },
		{ "v = ceil(-0.5)", 0x8000000000000000ull, DetLibm::ceil(-0.5), 0, 0 },
		{ "v = ldexp(1.5, -1074)", 2, DetLibm::ldexp(1.5, -1074), 0, 0 },
		{ "v = abs(-0.25)", bitsOf(0.25), DetLibm::fabs(-0.25), 0, 0 },
	};
	for (const Case &c : cases)
	{
		INFO(c.code);
		lua_settop(L, 0);
		REQUIRE(lua_dostring(L, c.code) == 0);
		lua_getglobal(L, "v");
		REQUIRE(lua_isnumber(L, -1));
		const double v = lua_tonumber(L, -1);
		lua_settop(L, 0);
		CHECK(bitsOf(v) == bitsOf(c.fromPort));
		if (c.want != 0)
		{
			CHECK(bitsOf(v) == c.want);
		}
		if (c.radians != 0)
		{
			CHECK(c.radians != c.correctlyRounded); // the case tells glibc's function from a correctly rounded one
		}
	}
	// the degree-returning functions: pinned through the port's radians above (the division by PI / 180 is plain IEEE)
	CHECK(bitsOf(DetLibm::asin(0.115234375)) == 0x3FBD90D0803393B9ull);
	CHECK(bitsOf(DetLibm::acos(0.8125)) == 0x3FE3EA71520CF3D4ull);
	CHECK(bitsOf(DetLibm::atan2(0.375, 3.5)) == 0x3FBB53076F5837A8ull);
}

#endif

TEST_CASE("dep2 libm: the integer fma is the IEEE fused multiply-add: the same bits as the CPU's FMA instruction over a sweep (and pinned)")
{
	Rng rng{ 0x243F6A8885A308D3ull };
	std::uint64_t h = 1469598103934665603ull;
	long compared = 0, mismatches = 0;
	std::string first;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
	__builtin_cpu_init();
	const bool hardware = __builtin_cpu_supports("fma");
#else
	const bool hardware = false;
#endif
	for (int i = 0; i < 300000; ++i)
	{
		double t[3][3];
		// random bit patterns; a product cancelled by its own rounding; small and huge exponents
		t[0][0] = rng.anyBits();
		t[0][1] = rng.anyBits();
		t[0][2] = rng.anyBits();
		t[1][0] = rng.uniform(-1e3, 1e3);
		t[1][1] = rng.uniform(-1e3, 1e3);
		t[1][2] = fromBits(bitsOf(-(t[1][0] * t[1][1])) + (rng.next() % 5) - 2);
		t[2][0] = fromBits((rng.next() & 0x800FFFFFFFFFFFFFull) | ((1 + rng.next() % 600) << 52));
		t[2][1] = fromBits((rng.next() & 0x800FFFFFFFFFFFFFull) | ((1 + rng.next() % 600) << 52));
		t[2][2] = fromBits(rng.next() & 0x801FFFFFFFFFFFFFull);
		for (auto &v : t)
		{
			const std::uint64_t b = bitsOf(DetLibm::fma(v[0], v[1], v[2]));
			for (int k = 0; k < 8; ++k)
			{
				h = (h ^ ((b >> (8 * k)) & 0xFF)) * 1099511628211ull;
			}
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
			// with two or three NaN operands the instruction's pick is the compiler's choice of vfmadd form (GCC's: the first, clang's: another);
			// DetLibm::fma's rule (the first NaN of a, b, c) is pinned by the hash and the special values instead
			const int nanOperands = (std::isnan(v[0]) ? 1 : 0) + (std::isnan(v[1]) ? 1 : 0) + (std::isnan(v[2]) ? 1 : 0);
			if (hardware && nanOperands < 2)
			{
				const std::uint64_t want = bitsOf(fmaInstruction(v[0], v[1], v[2]));
				++compared;
				if (b != want)
				{
					if (mismatches++ == 0)
					{
						char line[200];
						std::snprintf(line, sizeof line, "fma(%a, %a, %a): ours %016llx, instruction %016llx", v[0], v[1], v[2], (unsigned long long)b,
							(unsigned long long)want);
						first = line;
					}
				}
			}
#endif
		}
	}
	if (hardware)
	{
		INFO("first difference " << first);
		CHECK(mismatches == 0);
		MESSAGE(compared << " fused multiply-adds compared with the CPU's FMA instruction");
	}
	else
	{
		MESSAGE("SKIP dep2 fma instruction comparison: no x86-64 FMA instruction to compare with on this CPU or target; the pinned hash below still runs");
	}
	CHECK(h == kFmaSweepHash);
}
