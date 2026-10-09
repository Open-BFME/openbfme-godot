// OpenBFME. GPL-3.0.
//
// Lane PHYS-1: the arithmetic of the horde approach's progress test (RW 0x74A594 at 0x74A918 .. 0x74A9DF), kept apart so the tests can pin it.
//   * RW 0x6CA525 (this = the target): max(0, the x87 PC24 root of (dy^2 + dx^2) - the target's bounding circle)^2 with dx = target - point (an SSE square of the clamped
//     float); the binary subtracts the two squared values;
//   * the request goes out when half the full 3D span from the horde to the point is at most that squared gain, or when the unit has no path.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/SimMath.h"

namespace ApproachMath
{
inline float edgeSquared(const Coord3D &target, const Coord3D &a, float radius)
{
	const float dx = SimMath::pc24Sub(target.x, a.x), dy = SimMath::pc24Sub(target.y, a.y);
	const double sum = SimMath::pc24AddW(SimMath::pc24MulW((double)dy, (double)dy), SimMath::pc24MulW((double)dx, (double)dx));
	const float e = SimMath::fstpDword(SimMath::pc24SubW(SimMath::sqrtPC24(sum), (double)radius));
	return 0.0f > e ? 0.0f : SimMath::mulf32(e, e);
}

inline bool progressAllowsRequest(const Coord3D &target, float radius, const Coord3D &self, const Coord3D &point, bool hasPath)
{
	const float gained = SimMath::subf32(edgeSquared(target, self, radius), edgeSquared(target, point, radius));
	const float span = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(SimMath::subf32(point.x, self.x), SimMath::subf32(point.y, self.y),
		SimMath::subf32(point.z, self.z))));
	return SimMath::mulf32(span, 0.5f) <= gained || !hasPath;
}

// TEST HOOK ONLY (never set by game code): makes the horde approach's contact adjustment (RW 0x6FB67A's stand-in) refuse, so the KeepRequest branch can be pinned
inline bool &forceContactRefusalForTests()
{
	static bool flag = false;
	return flag;
}
} // namespace ApproachMath
