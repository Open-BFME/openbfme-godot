// OpenBFME. GPL-3.0.
//
// The floating-point arithmetic of a sim role Luau script's math library (lane SDK-2, GameLogic/Sdk/LuauScriptState.h), in one registered
// facade file (tools/sim/sim_policy.json facade_files): built on the deterministic libm (Common/DeterministicLibm.h) under the simulation
// floating-point contract, so every peer gets the same bits.

#pragma once

#include <cstdint>

namespace SdkSimMath
{
double sinh(double x);
double cosh(double x);
double tanh(double x);
double logBase(double x, double base); // log(x) / log(base); base 10 is log10
// Luau's vector.angle (lveclib.cpp vector_angle, its float cross product) with DetLibm::atan2; `axis` may be null
double vectorAngle(const float *a, const float *b, const float *axis);
double unitFromBits(std::uint32_t bits); // [0, 1): bits / 2^32
// an integer in [lo, hi] (as a Luau number) from 32 random bits (lo <= hi): lo + floor(bits * (hi - lo + 1) / 2^32)
double rangeFromBits(std::uint32_t bits, std::int64_t lo, std::int64_t hi);
} // namespace SdkSimMath
