// OpenBFME. GPL-3.0.
// Test driver: runs the engine's NumericState (engine/src/Common/System/NumericState.cpp) on lines
// "<ms hex> <scale bits hex>" from stdin and prints "<durationProduct double bits> <ceilScaled>".
// tools/retail_oracle/test_retail_oracle.py compares this with the retail duration code.
#include "Common/NumericState.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

int main()
{
	unsigned ms, scaleBits;
	while (std::scanf("%x %x", &ms, &scaleBits) == 2)
	{
		float scale;
		std::memcpy(&scale, &scaleBits, sizeof(scale));
		const double product = NumericState::durationProduct(ms, scale);
		std::uint64_t bits;
		std::memcpy(&bits, &product, sizeof(bits));
		std::printf("%016llx %u\n", (unsigned long long)bits, (unsigned)NumericState::ceilScaled(ms, scale));
		std::fflush(stdout);
	}
	return 0;
}
