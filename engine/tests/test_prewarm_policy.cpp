// OpenBFME unit tests: PERF-4's prewarm wave cap (OPENBFME_PREWARM_WAVES_MAX).
// GPL-3.0.

#include "doctest.h"

#include "GodotDevice/GodotPrewarmPolicy.h"

TEST_CASE("prewarm waves: OPENBFME_PREWARM_WAVES_MAX unset or malformed keeps 120, 0 queues every remaining state, N caps at N")
{
	CHECK(prewarm::waveLimit(nullptr, 500) == 120);
	CHECK(prewarm::waveLimit("", 500) == 120);
	CHECK(prewarm::waveLimit("abc", 500) == 120);
	CHECK(prewarm::waveLimit("12x", 500) == 120);
	CHECK(prewarm::waveLimit("-1", 500) == 120);
	CHECK(prewarm::waveLimit("0", 500) == 500);
	CHECK(prewarm::waveLimit("0", 0) == 0);
	CHECK(prewarm::waveLimit("30", 500) == 30);
}
