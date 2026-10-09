// OpenBFME. GPL-3.0.
//
// SimMath: THE simulation maths API. Every simulation lane (PATH-1, LOGIC-1, WEAPON-1, PROD-1, MOVE-1, ECON-1, ...) does its floating-point and
// float -> integer work through the names below and nothing else (PLAN rule 3). The names forward to NumericState (Common/NumericState.h), which is
// the implementation backend and the compatibility API of the INI and Apt code; new simulation code includes THIS header.
//
// WHY: lockstep multiplayer needs the Windows / MSVC and the Linux / GCC builds to produce the same bits. A bare `a * b + c` may be fused into an FMA,
// evaluated in a wider format or reassociated by a compiler; and the retail binary mixes SSE binary32 work with x87 work at 24-bit precision. So every
// retail operation has a NAMED counterpart here, called in the retail operand order, one rounded operation per call.
//
// WHICH NAME (pick the retail instruction):
//   sseAdd / sseSub / sseMul / sseDiv / sseFromInt32   `addss / subss / mulss / divss`, `cvtsi2ss`: one correctly rounded binary32 operation.
//   pc24Add / Sub / Mul / Div (float operands)         `fld a; f<op> b; fstp dword` under x87 precision control 24: rounded to a 24-bit significand
//                                                      with the wide x87 exponent range, then stored as binary32 (a second rounding for subnormals).
//   pc24AddD / SubD / MulD / DivD (double operands)    the same with an operand that stays wide in the register (a double, an fild integer).
//   pc24AddW / SubW / MulW / DivW + fstpDword          the register result kept WIDE (a binary64-bounded carrier: read the bounds in NumericState.h)
//                                                      and the `fstp dword` store, for chains like `fld; fmul; fadd; fdiv` that store once at the end.
//   fildU32, sqrtPC24                                  `fild` of an unsigned frame count (+2^32 fix-up rounded to 24 bits); the CRT sqrt under PC24.
//   ftol2 / ftol2Low32, fistp32, cvttss2si             float -> integer with the retail results for NaN, infinity and out of range (the x87 "integer
//                                                      indefinite" 0x80000000): MSVCR71 _ftol2, FISTP dword (nearest even), CVTTSS2SI (truncation).
//   floorD / ceilD / absD                              the CRT floor / ceil / fabs on doubles, no libm. floorToInt / ceilToInt: floor / ceil as an int.
//   sqrtd                                              the IEEE correctly rounded binary64 square root (NOT the PC24 root: sqrtPC24 is distinct).
//   length2d / length3d                                square root of the float32 sum of squares in the retail order (sumSquares2 / sumSquares3).
//   sinCosDet / sinDet / cosDet                        deterministic sin / cos (PATH-1, stop S-167).
//
// RULES for simulation code: no bare float or double `+ - * /` (integer arithmetic is fine), no std::sqrt / sin / cos / floor / ceil / fabs / pow / exp
// or any other <cmath> function, no float -> int cast (use cvttss2si / fistp32 / ftol2: a C++ cast of an out-of-range float is undefined). Float
// COMPARISONS, copies and float <-> double widening are fine. Build flags: every simulation target carries openbfme_sim_fp (engine/cmake/SimFp.cmake:
// /fp:strict resp. -ffp-contract=off -fno-fast-math -frounding-math, SSE evaluation on 32-bit x86, no LTO). tools/sim/sim_audit.py enforces both in CI:
// the compile commands carry the flags, and a type-aware AST scan of the simulation sources and their headers rejects floating arithmetic and math
// calls outside the registered facade implementations. The addf32 / subf32 / mulf32 / divf32 / truncToInt32 names below are the LOGIC-1 spellings,
// kept as aliases of the sse* / cvttss2si operations.
//
// TRANSCENDENTALS (HORDE-1 / PATH-1), TARGET FACTS (RotWK game.dat):
//   * atan2: MSVCR71 atan2 on doubles (RW 0xA3D68C via 0x441BD2); the result stays a DOUBLE in ST0 when the
//     caller subtracts from it (RW 0x5E5081 `fsub`), or is stored as float (`fstp dword`).
//   * cos / sin: either the x87 instructions `fcos` / `fsin` on a float argument (RW 0x42F4E0 / 0x42F4D0)
//     or MSVCR71 cos / sin (RW 0xA3CF84 / 0xA3CF90, double); both end in `fstp dword`.
//   * length: sqrt of the float32 sum of squares, widened to double (RW 0x403111), the CRT sqrt (RW 0xA3CF96).
//   * fabs: MSVCR71 fabs on a double.
// WHAT THIS MODULE DOES (inference, acceptance stop S-081): atan2d / cosd / sind / cosf32 / sinf32 are computed in double and rounded to float32
// where the retail code stores a float. x87 fsin / fcos / fpatan carry a 64-bit significand and agree with the correctly rounded double result to far
// below the float32 rounding boundary except on rare near-ties; bit parity with the retail CRT is NOT proven (S-081). Lane WIN-1: they no longer call
// the platform libm, which is not bit-identical between Windows and Linux (glibc's cos and the UCRT's differ in the last bit: a mixed LAN game
// desynchronised on it), but NumericState's deterministic double-double sinDD / cosDD / atan2DD (in practice the correctly rounded result, the
// same bits on every OS and compiler). New code uses sinDet / cosDet.

#pragma once

#include "Common/NumericState.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace SimMath
{

// ---- binary32 SSE operations ----
using NumericState::sseAdd;
using NumericState::sseDiv;
using NumericState::sseFromInt32;
using NumericState::sseMul;
using NumericState::sseSub;

// ---- x87 PC24 operations: `op; fstp dword` ----
using NumericState::pc24Add;
using NumericState::pc24Div;
using NumericState::pc24Mul;
using NumericState::pc24Sub;
using NumericState::pc24SubInt32;
// operands that stay wide in the register (double / fild integer), result stored as float
using NumericState::pc24AddD;
using NumericState::pc24DivD;
using NumericState::pc24MulD;
using NumericState::pc24SubD;
// the register result kept wide (binary64-bounded carriers) and the narrowing store
using NumericState::fildU32;
using NumericState::fstpDword;
using NumericState::pc24AddW;
using NumericState::pc24DivW;
using NumericState::pc24MulW;
using NumericState::pc24SubW;
using NumericState::sqrtPC24;

// ---- float to integer conversions (the retail results for invalid input) ----
using NumericState::cvttss2si;
using NumericState::fistp32;
using NumericState::ftol2;
using NumericState::ftol2Low32;

// ---- CRT floor / ceil / fabs on doubles, no libm ----
using NumericState::absD;
using NumericState::sseAddD;
using NumericState::sseDivD;
using NumericState::sseMulD;
using NumericState::sseSubD;
using NumericState::ceilD;
using NumericState::floorD;

// ---- LOGIC-1 spellings: compatibility aliases of the operations above ----
inline float addf32(float a, float b) { return sseAdd(a, b); }
inline float subf32(float a, float b) { return sseSub(a, b); }
inline float mulf32(float a, float b) { return sseMul(a, b); }
inline float divf32(float a, float b) { return sseDiv(a, b); }
// `cvttss2si`: truncation toward zero; a value that does not fit in 32 bits (and NaN) gives the "integer indefinite" 0x80000000
inline int truncToInt32(float f) { return cvttss2si(f); }

// ---- the float32 sum of squares of RW 0x403111, left to right (SSE: mulss and addss, each rounded) ----
inline float sumSquares2(float x, float y)
{
	const float s = sseMul(x, x);
	const float t = sseMul(y, y);
	return sseAdd(s, t);
}

inline float sumSquares3(float x, float y, float z)
{
	const float s = sumSquares2(x, y);
	const float u = sseMul(z, z);
	return sseAdd(s, u);
}

// the IEEE correctly rounded square root of a double (identical on every platform; NOT the PC24 root, which is sqrtPC24). Registered facade implementation.
inline double sqrtd(double x)
{
	return std::sqrt(x);
}

// RW 0x403111: sqrt((double)(x*x + y*y + z*z)) with the sum formed in float32, left to right.
inline double length3d(float x, float y, float z)
{
	return sqrtd((double)sumSquares3(x, y, z));
}

// Square root of the float32 sum of squares in the retail order (RW 0x403111 without the z term), stored as float32 (lane PATH-1, stop S-167).
inline float length2d(float x, float y)
{
	return (float)sqrtd((double)sumSquares2(x, y));
}

// floor / ceil of a float as an int: the CRT floor / ceil, then FISTP (the value is integral, so the rounding mode does not matter). NaN, infinities
// and values outside the int range give the integer indefinite 0x80000000, like the retail conversion (a C++ cast is undefined there).
inline int floorToInt(float f)
{
	return fistp32(floorD((double)f));
}

inline int ceilToInt(float f)
{
	return fistp32(ceilD((double)f));
}

// binary64 operations (one SSE2 rounding each, never contracted; MOVE-1's pathfinder and movement maths). |x| is NumericState::absD above.
inline double addD(double a, double b) { return sseAddD(a, b); }
inline double subD(double a, double b) { return sseSubD(a, b); }
inline double mulD(double a, double b) { return sseMulD(a, b); }
inline double divD(double a, double b) { return sseDivD(a, b); }

// CRT atan2(y, x) in double (kept wide by callers that subtract before storing).
inline double atan2d(float y, float x)
{
	return NumericState::atan2DD((double)y, (double)x);
}

// cos / sin kept wide (the x87 `fcos` / `fsin` result stays in ST0 until the next operation).
inline double cosd(float a)
{
	return NumericState::cosDD((double)a);
}

inline double sind(float a)
{
	return NumericState::sinDD((double)a);
}

// cos / sin of a float argument, stored as float32 (`fcos` / `fsin` then `fstp dword`).
inline float cosf32(float a)
{
	return (float)NumericState::cosDD((double)a);
}

inline float sinf32(float a)
{
	return (float)NumericState::sinDD((double)a);
}

// ---------------------------------------------------------------------------------------------------------
// Deterministic maths for the pathfinder and the AI move path (lane PATH-1, acceptance stop S-167).
// ---------------------------------------------------------------------------------------------------------

// Deterministic sin / cos in double. Cody-Waite reduction with the fdlibm pi/2 split, then a Taylor polynomial (r^17 / r^18) on
// |r| <= pi/4, error below 1e-19. Only IEEE + - * / on doubles in a fixed order: every compiler that does not contract (the PATH-1
// sources are built with -ffp-contract=off; MSVC x64 does not without /arch:AVX2 and /fp:fast) gives the same bits, which std::sin / std::cos
// (the platform libm) do not promise. NOT the retail functions (RW x87 fsin / fcos or MSVCR71 CRT: parity is stop S-167).
inline void sinCosDet(double x, double &s, double &c)
{
	const double twoOverPi = 0.63661977236758134308;
	const double pio2_1 = 1.57079632673412561417e+00;  // first 33 bits of pi/2
	const double pio2_2 = 6.07710050630396597660e-11;  // the next 33 bits
	const double pio2_3 = 2.02226624879595063154e-21;  // the remainder
	if (!(x > -1.0e6 && x < 1.0e6))
	{
		x = 0.0; // not an angle: a fixed, platform independent answer
	}
	const double kd = x * twoOverPi;
	const long long k = (long long)(kd < 0.0 ? kd - 0.5 : kd + 0.5);
	const double kf = (double)k;
	double r = x - kf * pio2_1;
	r = r - kf * pio2_2;
	r = r - kf * pio2_3;
	const double r2 = r * r;
	double ps = 2.8114572543455206e-15;       // 1 / 17!
	ps = -7.647163731819816e-13 + r2 * ps;     // -1 / 15!
	ps = 1.6059043836821613e-10 + r2 * ps;     // 1 / 13!
	ps = -2.505210838544172e-08 + r2 * ps;     // -1 / 11!
	ps = 2.7557319223985893e-06 + r2 * ps;     // 1 / 9!
	ps = -0.0001984126984126984 + r2 * ps;     // -1 / 7!
	ps = 0.008333333333333333 + r2 * ps;       // 1 / 5!
	ps = -0.16666666666666666 + r2 * ps;       // -1 / 3!
	double pc = -1.5619206968586225e-16;       // -1 / 18!
	pc = 4.779477332387385e-14 + r2 * pc;      // 1 / 16!
	pc = -1.1470745597729725e-11 + r2 * pc;    // -1 / 14!
	pc = 2.08767569878681e-09 + r2 * pc;       // 1 / 12!
	pc = -2.755731922398589e-07 + r2 * pc;     // -1 / 10!
	pc = 2.48015873015873e-05 + r2 * pc;       // 1 / 8!
	pc = -0.001388888888888889 + r2 * pc;      // -1 / 6!
	pc = 0.041666666666666664 + r2 * pc;       // 1 / 4!
	pc = -0.5 + r2 * pc;                       // -1 / 2!
	const double r3 = r2 * r;
	const double sr = r + r3 * ps;
	const double cr = 1.0 + r2 * pc;
	switch ((int)(k & 3))
	{
	case 0: s = sr; c = cr; break;
	case 1: s = cr; c = -sr; break;
	case 2: s = -sr; c = -cr; break;
	default: s = -cr; c = sr; break;
	}
}

inline float cosDet(float a)
{
	double s, c;
	sinCosDet((double)a, s, c);
	return (float)c;
}

inline float sinDet(float a)
{
	double s, c;
	sinCosDet((double)a, s, c);
	return (float)s;
}


// Deterministic arc cosine in double (lane START-1). RETAIL: the starting unit placement calls MSVCR71 acos (RW 0xA3D6C2 -> IAT 0xBD0568, called at RW 0x62AF7E). Built
// from IEEE + - * / and the correctly rounded sqrt only, in a fixed order (two half angle reductions, then a Taylor series of degree 23 on |t| <= tan(pi/16)), so
// every compiler gives the same bits; error < 2e-16 (NOT the CRT's bits: parity is stop S-167, like sinCosDet). A value outside [-1, 1] is the CRT's domain error, the
// x87 default NaN (0xFFF8000000000000, "indefinite"). Registered facade implementation.
inline double atanDetUnit(double t) // atan for 0 <= t <= 1
{
	const double t1 = t / (1.0 + std::sqrt(1.0 + t * t));
	const double t2 = t1 / (1.0 + std::sqrt(1.0 + t1 * t1));
	const double r = t2 * t2;
	double p = -1.0 / 23.0;
	p = 1.0 / 21.0 + r * p;
	p = -1.0 / 19.0 + r * p;
	p = 1.0 / 17.0 + r * p;
	p = -1.0 / 15.0 + r * p;
	p = 1.0 / 13.0 + r * p;
	p = -1.0 / 11.0 + r * p;
	p = 1.0 / 9.0 + r * p;
	p = -1.0 / 7.0 + r * p;
	p = 1.0 / 5.0 + r * p;
	p = -1.0 / 3.0 + r * p;
	p = 1.0 + r * p;
	return 4.0 * (t2 * p);
}

inline double acosDet(double x)
{
	if (!(x >= -1.0 && x <= 1.0))
	{
		const std::uint64_t indefinite = 0xFFF8000000000000ull;
		double nan;
		std::memcpy(&nan, &indefinite, sizeof nan);
		return nan;
	}
	const double halfPi = 1.5707963267948966;
	if (x == 1.0)
	{
		return 0.0;
	}
	if (x == -1.0)
	{
		return 2.0 * halfPi;
	}
	// acos(x) = 2 atan(sqrt((1 - x) / (1 + x)))
	const double t = std::sqrt((1.0 - x) / (1.0 + x));
	const double a = t <= 1.0 ? atanDetUnit(t) : halfPi - atanDetUnit(1.0 / t);
	return 2.0 * a;
}

// The retail conversion of a colour channel (RGBColor -> 8 bit): RW 0x5EEB8B / 0x5EEB9D -> 0x783106 / 0x78311E -> 0x4049C3: an x87 multiply by 255.0
// (precision control 24) then _ftol2, the low word (review of START-1).
inline std::uint32_t packRgb8(float red, float green, float blue)
{
	const std::uint32_t r = ftol2Low32(pc24MulW((double)red, 255.0));
	const std::uint32_t g = ftol2Low32(pc24MulW((double)green, 255.0));
	const std::uint32_t b = ftol2Low32(pc24MulW((double)blue, 255.0));
	return (r << 16) | (g << 8) | b;
}

} // namespace SimMath
