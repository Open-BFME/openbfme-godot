// OpenBFME. GPL-3.0.
//
// DeterministicLibm: the double-precision transcendental functions of the simulation that are not retail-specific (lane DEP-2): today the Lua 4.0.1
// math library (lmathlib.c: sin, cos, tan, asin, acos, atan, atan2, exp, log, log10 and the `^` operator's pow), which runs inside the lockstep
// simulation.
//
// WHAT IT COMPUTES: the results of glibc 2.39's libm (sysdeps/ieee754/dbl-64) as glibc runs on x86-64 Linux with an FMA-capable CPU, i.e. the
// `*_fma` variants glibc's ifunc selects there (sysdeps/x86_64/fpu/multiarch, compiled with -mfma -mavx2). That is what the simulation computed on
// Linux before DEP-2: the Deck (glibc 2.41), a CI host and the reference PC (glibc 2.39) give the same bits. The port spells every multiply-add that GCC fused
// in those variants as an explicit fused multiply-add (DetLibm::fma: one rounding in integer arithmetic, no FMA instruction and no libm, so no compiler or CPU feature decides the result) and every other operation as written;
// tests/test_deterministic_libm.cpp compares it with the host libm over a sweep and pins a hash of its results.
// The functions take and return IEEE binary64 under the canonical environment (round to nearest). errno is never set. NaN results: a NaN operand
// is returned quietened (the first NaN operand of two), except floor / ceil leave it unchanged and fabs only clears its sign bit. An invalid
// operation returns the x86 default NaN 0xFFF8000000000000, as the SSE2 code does.
//
// NOT the retail CRT: RotWK's Lua runs MSVCR71's x87 functions (RW 0xA3CF84 cos, 0xA3CF90 sin, ...); parity with them is not claimed.

#pragma once

#ifdef __cplusplus
namespace DetLibm
{

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);

// the exact functions: sqrt (the square-root instruction), fabs, floor, ceil, fmod, ldexp (round to nearest even into the subnormals), frexp. NaN and
// invalid operands as above (sqrt of a negative number and fmod (inf, y) / fmod (x, 0) give the x86 default NaN).
// a * b + c rounded once, in integer arithmetic (no FMA instruction, no libm): what the ports above use for every fused operation
double fma(double a, double b, double c);
double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double ldexp(double x, int n);
double frexp(double x, int *exponent);

} // namespace DetLibm

extern "C" {
#endif

// The same functions for C callers (the Lua library's lmathlib.c).
double openbfme_detlibm_sin(double x);
double openbfme_detlibm_cos(double x);
double openbfme_detlibm_tan(double x);
double openbfme_detlibm_asin(double x);
double openbfme_detlibm_acos(double x);
double openbfme_detlibm_atan(double x);
double openbfme_detlibm_atan2(double y, double x);
double openbfme_detlibm_exp(double x);
double openbfme_detlibm_log(double x);
double openbfme_detlibm_log10(double x);
double openbfme_detlibm_pow(double x, double y);
double openbfme_detlibm_sqrt(double x);
double openbfme_detlibm_fabs(double x);
double openbfme_detlibm_floor(double x);
double openbfme_detlibm_ceil(double x);
double openbfme_detlibm_fmod(double x, double y);
double openbfme_detlibm_ldexp(double x, int n);
double openbfme_detlibm_frexp(double x, int *exponent);

#ifdef __cplusplus
}
#endif
