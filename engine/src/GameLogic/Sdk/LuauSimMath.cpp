// OpenBFME. GPL-3.0. See GameLogic/Sdk/LuauSimMath.h (lane SDK-2): a registered facade file of the simulation audit.

#include "GameLogic/Sdk/LuauSimMath.h"

#include "Common/DeterministicLibm.h"

namespace SdkSimMath
{

double sinh(double x)
{
	return (DetLibm::exp(x) - DetLibm::exp(-x)) * 0.5;
}

double cosh(double x)
{
	return (DetLibm::exp(x) + DetLibm::exp(-x)) * 0.5;
}

double tanh(double x)
{
	if (x > 20.0)
		return 1.0;
	if (x < -20.0)
		return -1.0;
	const double e = DetLibm::exp(2.0 * x);
	return (e - 1.0) / (e + 1.0);
}

double logBase(double x, double base)
{
	return base == 10.0 ? DetLibm::log10(x) : DetLibm::log(x) / DetLibm::log(base);
}

double vectorAngle(const float *a, const float *b, const float *axis)
{
	const float cross[] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	const double sinA = DetLibm::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
	const double cosA = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	double angle = DetLibm::atan2(sinA, cosA);
	if (axis && cross[0] * axis[0] + cross[1] * axis[1] + cross[2] * axis[2] < 0.0)
		angle = -angle;
	return angle;
}

double unitFromBits(std::uint32_t bits)
{
	return double(bits) / 4294967296.0;
}

double rangeFromBits(std::uint32_t bits, std::int64_t lo, std::int64_t hi)
{
	const std::uint64_t span = std::uint64_t(hi - lo) + 1u;
	// the 128-bit product's high 64 bits, without a 128-bit type: bits < 2^32, so split span
	const std::uint64_t high = (std::uint64_t(bits) * (span >> 32)) + ((std::uint64_t(bits) * (span & 0xffffffffu)) >> 32);
	return double(lo + std::int64_t(high));
}

} // namespace SdkSimMath
