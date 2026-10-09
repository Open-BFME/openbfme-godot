// OpenBFME unit tests (lane WIN-1). GPL-3.0.
//
// The deterministic sin / cos / atan2 of the numeric facade (NumericState::sinDD / cosDD / atan2DD, behind SimMath's atan2d / cosd / sind / cosf32 /
// sinf32). Before lane WIN-1 they were the platform libm: a Linux (glibc) and a Windows (UCRT) peer of one LAN game computed cos(-pi/2) of a turning
// Mordor porter one bit apart and desynchronised (tools/net/cross_os_lockstep.sh). The golden file holds the correctly rounded results computed with an
// 80 digit decimal reference (tools/sim/gen_dd_trig_tables.py --golden); every build on every OS must reproduce them bit for bit, which is what keeps
// Windows and Linux players in sync. Retail's x87 / MSVCR71 results are not claimed (S-081).

#include "doctest.h"

#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
std::uint64_t bits(double d)
{
	std::uint64_t u;
	std::memcpy(&u, &d, sizeof u);
	return u;
}

double hex(const std::string &s)
{
	return std::strtod(s.c_str(), nullptr); // C99 hexadecimal floating text: exact on every conforming CRT
}
} // namespace

TEST_CASE("win1 trig: sinDD / cosDD / atan2DD return the correctly rounded result for every golden input (the same bits on every OS)")
{
	std::ifstream in(std::string(OPENBFME_TEST_DATA_DIR) + "/win1/trig_golden.txt");
	REQUIRE(in.good());
	std::string line;
	int compared = 0, bad = 0, libmDiffers = 0;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		std::istringstream ls(line);
		std::string kind, a, b, c;
		ls >> kind >> a >> b;
		double got = 0.0, want = 0.0, host = 0.0;
		if (kind == "a")
		{
			ls >> c;
			got = NumericState::atan2DD(hex(a), hex(b));
			host = std::atan2(hex(a), hex(b));
			want = hex(c);
		}
		else
		{
			got = kind == "s" ? NumericState::sinDD(hex(a)) : NumericState::cosDD(hex(a));
			host = kind == "s" ? std::sin(hex(a)) : std::cos(hex(a));
			want = hex(b);
		}
		++compared;
		if (bits(got) != bits(want) && ++bad <= 10)
		{
			MESSAGE("differs: " << line);
		}
		libmDiffers += bits(host) != bits(want) ? 1 : 0;
	}
	// information only: how often this host's libm misses the correctly rounded result (glibc and the UCRT miss different inputs)
	MESSAGE(compared << " golden values; the host libm differs from the correctly rounded result on " << libmDiffers);
	CHECK(compared > 5000);
	CHECK(bad == 0);
}

TEST_CASE("win1 trig: the special values (zeros, infinities, NaN) follow C99 Annex F with one canonical NaN")
{
	const double inf = std::numeric_limits<double>::infinity();
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double pi = 0x1.921fb54442d18p+1, halfPi = 0x1.921fb54442d18p+0, quarterPi = 0x1.921fb54442d18p-1;
	CHECK(bits(NumericState::sinDD(0.0)) == bits(0.0));
	CHECK(bits(NumericState::sinDD(-0.0)) == bits(-0.0));
	CHECK(NumericState::cosDD(0.0) == 1.0);
	CHECK(NumericState::cosDD(-0.0) == 1.0);
	CHECK(bits(NumericState::sinDD(inf)) == 0x7FF8000000000000ull);
	CHECK(bits(NumericState::cosDD(-inf)) == 0x7FF8000000000000ull);
	CHECK(bits(NumericState::sinDD(nan)) == 0x7FF8000000000000ull);
	CHECK(bits(NumericState::atan2DD(nan, 1.0)) == 0x7FF8000000000000ull);
	CHECK(bits(NumericState::atan2DD(0.0, 1.0)) == bits(0.0));
	CHECK(bits(NumericState::atan2DD(-0.0, 1.0)) == bits(-0.0));
	CHECK(NumericState::atan2DD(0.0, -1.0) == pi);
	CHECK(NumericState::atan2DD(-0.0, -0.0) == -pi);
	CHECK(bits(NumericState::atan2DD(0.0, 0.0)) == bits(0.0));
	CHECK(NumericState::atan2DD(2.0, 0.0) == halfPi);
	CHECK(NumericState::atan2DD(-2.0, -0.0) == -halfPi);
	CHECK(NumericState::atan2DD(inf, inf) == quarterPi);
	CHECK(NumericState::atan2DD(-inf, -inf) == -0x1.2d97c7f3321d2p+1); // -3pi/4
	CHECK(NumericState::atan2DD(inf, 5.0) == halfPi);
	CHECK(NumericState::atan2DD(5.0, -inf) == pi);
	CHECK(bits(NumericState::atan2DD(-5.0, inf)) == bits(-0.0));
	// extreme magnitudes stay finite and ordered (the scaling keeps the Dekker split in range)
	CHECK(NumericState::atan2DD(1e300, 1e-300) == halfPi);
	CHECK(bits(NumericState::atan2DD(1e-300, 1e300)) == bits(0.0)); // 1e-600 rounds to +0
	CHECK(NumericState::atan2DD(3e300, 3e300) == quarterPi);
	CHECK(NumericState::atan2DD(3e-300, 3e-300) == quarterPi);
}

TEST_CASE("win1 trig: SimMath's atan2d / cosd / sind / cosf32 / sinf32 are the deterministic functions (no platform libm in the simulation)")
{
	const float angles[] = { -1.5707964f, 0.25f, 3.0f, -6.5f, 1e-6f };
	for (float a : angles)
	{
		CHECK(bits(SimMath::cosd(a)) == bits(NumericState::cosDD((double)a)));
		CHECK(bits(SimMath::sind(a)) == bits(NumericState::sinDD((double)a)));
		CHECK(SimMath::cosf32(a) == (float)NumericState::cosDD((double)a));
		CHECK(SimMath::sinf32(a) == (float)NumericState::sinDD((double)a));
		CHECK(bits(SimMath::atan2d(a, 0.75f)) == bits(NumericState::atan2DD((double)a, 0.75)));
	}
	// the porter of the cross-OS desync: cos of the float nearest -pi/2, whose last bit glibc and the UCRT disagree on
	CHECK(bits(SimMath::cosd(-1.5707963705062866f)) == bits(-0x1.777a5cf72ceccp-25));
}

TEST_CASE("win1 trig (Sol r1): sin / cos of huge finite arguments are reduced over the full range (Payne-Hanek), still correctly rounded")
{
	// before: sinDD(double(1e20f)) returned about 2^240 and cosDD(double(1e30f)) NaN (the k * pi/2 split was used far beyond its exact range)
	const double x20 = 0x1.5af1d8p+66, x30 = 0x1.93e594p+99; // 1e20f and 1e30f widened
	CHECK(bits(NumericState::sinDD(x20)) == bits(hex("0x1.502ad17d67516p-1")));
	CHECK(bits(NumericState::cosDD(x20)) == bits(hex("0x1.822e45d2fb241p-1")));
	CHECK(bits(NumericState::sinDD(x30)) == bits(hex("-0x1.95135fcc7c53cp-1")));
	CHECK(bits(NumericState::cosDD(x30)) == bits(hex("-0x1.3924432ee4d87p-1")));
	CHECK(bits(NumericState::sinDD(-x30)) == bits(-hex("-0x1.95135fcc7c53cp-1")));
	CHECK(bits(NumericState::cosDD(-x30)) == bits(hex("-0x1.3924432ee4d87p-1")));
	CHECK(bits(NumericState::sinDD(0x1.fffffffffffffp+1023)) == bits(0x1.452fc98b34e97p-8)); // DBL_MAX
	CHECK(bits(NumericState::cosDD(0x1.fffffffffffffp+1023)) == bits(-0x1.fffe62ecfab75p-1));
	for (double x : { x20, x30, 1e300, -1e200, 0x1.fffffffffffffp+1023 })
	{
		CHECK(std::fabs(NumericState::sinDD(x)) <= 1.0);
		CHECK(std::fabs(NumericState::cosDD(x)) <= 1.0);
	}
}
