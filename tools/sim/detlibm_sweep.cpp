// OpenBFME. GPL-3.0. Cross-compiler bit comparison driver; build separately against each compiler's openbfme_detlibm library.
// Usage and build commands: tools/sim/README.md. No host libm oracle and no NaN exclusions.
#include "Common/DeterministicLibm.h"

#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::uint64_t kSeed = 0xD1B54A32D192ED03ull;
constexpr std::uint64_t kCount = 1000000;
std::uint64_t state = kSeed;

std::uint64_t next()
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

double fromBits(std::uint64_t u)
{
	double x;
	std::memcpy(&x, &u, sizeof x);
	return x;
}

std::uint64_t bits(double x)
{
	std::uint64_t u;
	std::memcpy(&u, &x, sizeof u);
	return u;
}

double uniform(double lo, double hi)
{
	return lo + (hi - lo) * ((double)(next() >> 11) * 0x1p-53);
}

bool writeWords(std::FILE *out, const std::uint64_t *words, std::size_t count)
{
	unsigned char bytes[24 * 8];
	for (std::size_t i = 0; i < count; ++i)
	{
		for (int k = 0; k < 8; ++k)
		{
			bytes[8 * i + k] = (unsigned char)(words[i] >> (8 * k));
		}
	}
	return std::fwrite(bytes, 8, count, out) == count;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 2 || std::fesetenv(FE_DFL_ENV) != 0 || std::fegetround() != FE_TONEAREST)
	{
		std::fputs("usage: detlibm_sweep <output.bin> (requires round-to-nearest)\n", stderr);
		return 2;
	}
	std::FILE *out = std::fopen(argv[1], "wb");
	if (!out)
	{
		std::perror("detlibm_sweep output");
		return 2;
	}
	const std::uint64_t header[] = { kSeed, kCount, 24 };
	bool ok = writeWords(out, header, 3);
	const std::uint64_t edges[] = {
		0, 0x8000000000000000ull, 1, 0x8000000000000001ull,
		0x000FFFFFFFFFFFFFull, 0x0010000000000000ull,
		0x3FF0000000000000ull, 0xBFF0000000000000ull,
		0x3FEFFFFFFFFFFFFFull, 0x3FF0000000000001ull,
		0x7FEFFFFFFFFFFFFFull, 0xFFEFFFFFFFFFFFFFull,
		0x7FF0000000000000ull, 0xFFF0000000000000ull,
		0x7FF0000000000001ull, 0xFFF31C670B4FA197ull,
		0x7FF8000000000123ull, 0xFFF8000000000123ull,
	};
	constexpr std::size_t edgeCount = sizeof edges / sizeof edges[0];
	for (std::uint64_t i = 0; i < kCount && ok; ++i)
	{
		double x, y, z;
		if (i % 4 == 0)
		{
			x = fromBits(next());
			y = fromBits(next());
			z = fromBits(next());
		}
		else if (i % 4 == 1)
		{
			x = uniform(-1, 1);
			y = uniform(-1, 1);
			z = fromBits(bits(-(x * y)) + next() % 5 - 2); // FMA cancellation and adjacent rounding cases
		}
		else if (i % 4 == 2)
		{
			x = uniform(-1000, 1000);
			y = uniform(-50, 50);
			z = uniform(-1000, 1000);
		}
		else
		{
			x = fromBits(edges[(i / 4) % edgeCount]);
			y = fromBits(edges[(i / 4 + 5) % edgeCount]);
			z = fromBits(edges[(i / 4 + 11) % edgeCount]);
		}
		const int n = (int)(next() % 4400) - 2200;
		int exponent = 0;
		const double mantissa = DetLibm::frexp(x, &exponent);
		const std::uint64_t record[] = {
			bits(x), bits(y), bits(z), (std::uint64_t)(std::int64_t)n,
			bits(DetLibm::sin(x)), bits(DetLibm::cos(x)), bits(DetLibm::tan(x)),
			bits(DetLibm::asin(x)), bits(DetLibm::acos(x)), bits(DetLibm::atan(x)), bits(DetLibm::atan2(x, y)),
			bits(DetLibm::exp(x)), bits(DetLibm::log(x)), bits(DetLibm::log10(x)), bits(DetLibm::pow(x, y)),
			bits(DetLibm::sqrt(x)), bits(DetLibm::fabs(x)), bits(DetLibm::floor(x)), bits(DetLibm::ceil(x)),
			bits(DetLibm::fmod(x, y)), bits(DetLibm::ldexp(x, n)), bits(mantissa), bits(DetLibm::fma(x, y, z)),
			(std::uint64_t)(std::int64_t)exponent,
		};
		static_assert(sizeof record / sizeof record[0] == 24);
		ok = writeWords(out, record, 24);
	}
	const bool closed = std::fclose(out) == 0;
	if (!ok || !closed)
	{
		std::fputs("ERROR: could not write the complete sweep\n", stderr);
		return 2;
	}
	std::fprintf(stderr, "seed=%016llx tuples=%llu functions=19 double_results=%llu frexp_exponents=%llu\n",
		(unsigned long long)kSeed, (unsigned long long)kCount, (unsigned long long)(19 * kCount), (unsigned long long)kCount);
}
