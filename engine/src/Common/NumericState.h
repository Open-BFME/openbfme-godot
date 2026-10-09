// OpenBFME. GPL-3.0.
//
// NumericState: the numeric state of the retail game and the exact arithmetic the retail INI code
// performs under it. Everything that needs "retail numerics" goes through here so the simulation
// numeric facade (docs/PLAN.md rule 3) can reuse and extend it.
//
// TARGET FACTS (RotWK game.dat; PLAN rule 9: the exe is community-modified, but these routines
// sit in ordinary .text):
//   * setFPMode, RW 0x440809:
//        _fpreset();  cw = _controlfp(0, 0);
//        _controlfp((cw & 0xFFFEFCFF) | 0x20000, 0x30300);
//     = x87 precision control 24-bit (_PC_24), rounding control nearest. INI::load calls it first
//     (spec 1.1 item 5, RW load at 0x42D2C1).
//   * The INI math evaluator does `fld a; f<op> b; fstp dword r` for every step (RW 0x42E28F,
//     0x42E2B2), i.e. the operation is rounded to a 24-bit significand with x87's wide exponent
//     range and THEN stored as float32, which rounds a second time when the value is subnormal.
//   * parseDurationUnsignedInt, RW 0x73A429 (sequence 0x73A440-0x73A458, PLAN rule 2): signed
//     `fild`, then, if the input was >= 2^31, `fadd 2^32` (that addition is rounded to 24 bits
//     under PC24), `fmul 0.005f`, `fstp qword`, MSVCR71 `ceil`.
//   * the retail 5.439772e-38 * 0.20226517 gives 0x0077CF3C, not the SSE/float32 result 0x0077CF3B.
//
// WHAT THIS MODULE DOES (inference, tested against real hardware):
//   * On x64 the CRT cannot select x87 precision (_MCW_PC is unsupported; the sequence above ends
//     with the x87 control word at 53-bit precision, MXCSR round-to-nearest). setFPMode therefore
//     only restores the rounding mode there, and NOTHING relies on the FPU precision state: the
//     arithmetic below is exact integer emulation of "operate at PC24 with a wide exponent, then
//     store as float32". Plain float arithmetic is NOT a substitute (subnormal double rounding).
//   * pc24Add/Sub/Mul/Div and durationProduct are verified bit-for-bit against a 32-bit x86 helper
//     that runs the real x87 instructions (tools/x87_oracle, tests/test_numeric_state.cpp).
// NaN payloads are not emulated: any invalid operation yields the x87 default NaN (0xFFC00000), an
// operand NaN yields that NaN made quiet.

#pragma once

#include <cfloat>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// The SSE binary32 facade below assumes binary32 arithmetic evaluated in binary32 (SSE2); an x87 build of this code is not supported.
static_assert(FLT_EVAL_METHOD == 0, "binary32 arithmetic must be evaluated in binary32 (SSE2)");

// The facade reads and writes the bit patterns of float / double through std::memcpy (no cast, no union), which is only meaningful for IEEE 754
// binary32 / binary64 stored in an integer of the same width: refuse to build anywhere else.
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559, "IEEE 754 float and double are required");
static_assert(sizeof(float) == 4 && sizeof(double) == 8 && sizeof(std::uint32_t) == 4 && sizeof(std::uint64_t) == 8,
	"binary32 / binary64 must be 32 / 64 bits wide");
static_assert(std::numeric_limits<float>::radix == 2 && std::numeric_limits<float>::digits == 24 && std::numeric_limits<float>::max_exponent == 128 &&
		std::numeric_limits<float>::min_exponent == -125,
	"float must be binary32 (24-bit significand, exponents -126..127)");
static_assert(std::numeric_limits<double>::radix == 2 && std::numeric_limits<double>::digits == 53 && std::numeric_limits<double>::max_exponent == 1024 &&
		std::numeric_limits<double>::min_exponent == -1021,
	"double must be binary64 (53-bit significand, exponents -1022..1023)");
static_assert(std::numeric_limits<float>::denorm_min() > 0.0f && std::numeric_limits<double>::denorm_min() > 0.0,
	"subnormal numbers must be supported (no flush to zero)");

namespace NumericState
{

// The canonical MXCSR of the simulation: round to nearest, every exception masked, flush-to-zero OFF, denormals-are-zero OFF (x86 / x64).
constexpr std::uint32_t kCanonicalControlRegister = 0x1F80u;

// Reset the whole floating-point environment: the default environment (fesetenv(FE_DFL_ENV)), nearest rounding, cleared and masked exceptions and, on
// x86, MXCSR = 0x1F80 (so FTZ / DAZ are off and a minimum-normal float times 0.5 is the subnormal 0x00400000, not zero). Called at every simulation-thread
// entry and at the outermost GameLogic::update; any code that changes the environment (a graphics driver, a plug-in, a test) is undone by it.
void normalizeFloatingPointEnvironment();
// true when the calling thread's environment is the canonical one (MXCSR & 0xFFC0 == kCanonicalControlRegister and the x87 control word canonical on
// x86; nearest rounding elsewhere)
bool floatingPointEnvironmentIsCanonical();
// the calling thread's MXCSR (x86 / x64; 0 on other targets) and a way to set it: test hooks for the environment normalisation
std::uint32_t readControlRegister();
void writeControlRegister(std::uint32_t value);

// The canonical x87 control word (lane PERF-2, Sol r1): every exception masked (bits 0..5), precision control 64-bit extended (bits 8..9, glibc's default
// environment), rounding to nearest (bits 10..11). floatingPointEnvironmentIsCanonical checks it beside MXCSR on GCC / Clang x86 / x64 (the bits of
// kX87ControlWordMask: exceptions, precision, rounding); 32-bit MSVC checks the rounding and the masks (its precision is setFPMode's PC24); MSVC x64 runs
// no x87 code and does not check it. The read / write functions are test hooks (they return the canonical word / do nothing where there is no access).
constexpr std::uint16_t kCanonicalX87ControlWord = 0x037Fu;
constexpr std::uint16_t kX87ControlWordMask = 0x0F3Fu;
std::uint16_t readX87ControlWord();
void writeX87ControlWord(std::uint16_t value);
bool x87ControlWordIsCanonical();

// Retail setFPMode (RW 0x440809): normalizeFloatingPointEnvironment, then on 32-bit x86 MSVC the x87 precision control 24 bits. On x64 there is no x87
// precision control to select (see above).
void setFPMode();

// fld a; f<op> b; fstp dword: the operation rounded to a 24-bit significand (wide exponent range),
// then stored as float32 (round-to-nearest-even, subnormals included).
float pc24Add(float a, float b);
float pc24Sub(float a, float b);
float pc24Mul(float a, float b);
float pc24Div(float a, float b);

// fld a; fisub dword b; fstp dword: the 32-bit integer operand is NOT rounded to float first (fild/fisub
// read it exactly); only the difference is rounded to 24 bits.  BFME2 0x00B03664 (Apt Equals2).
float pc24SubInt32(float a, std::int32_t b);

// Operands that stay WIDE in the x87 register: an exact integer (fild), a double (a string's atof result is
// returned in ST0 without being stored: BFME2 0x00ADD460) or a float.  Every double is exact in the register;
// only the operation's RESULT is rounded to 24 bits (PC24), then stored as float32.  `fld a; f<op> b; fstp dword`.
// Used by the Apt handlers, which store the TOP operand as float and operate on the UNDER operand directly in ST0
// (Subtract 0x00B00957-0x00B00962, Multiply 0x00B00AB8-0x00B00AC3, Divide 0x00B00BD9-0x00B00C01).
float pc24AddD(double a, double b);
float pc24SubD(double a, double b);
float pc24MulD(double a, double b);
float pc24DivD(double a, double b);

// RW 0x73A440-0x73A458: signed fild of `ms`, fadd 2^32 (rounded to 24 bits) if the signed value is
// negative, fmul `scale`, store as a double. The result is exact (24 significant bits).
double durationProduct(std::uint32_t ms, float scale);

// durationProduct followed by the MSVCR71 ceil, as parseDurationUnsignedInt / ...Short store it.
std::uint32_t ceilScaled(std::uint32_t ms, float scale);

// ---------------------------------------------------------------------------------------------------------------------------------------
// WIDE results (lane WEAPON-1 review). An x87 operation leaves its result in a register: rounded to 24 bits but with the 15-bit exponent, so
// 2^64 * 2^64 is 2^128, not infinity. It is narrowed only where retail stores it (`fstp dword`, or `fstp qword` into a double). The pc24Add /
// Sub / Mul / Div functions above are `op; fstp dword` (they narrow); the *W functions below are the register result as a double (24 significant
// bits), and fstpDword is the narrowing store. Chain the W functions and call fstpDword where the retail instruction sequence has its store.
//
// BOUNDS: the *W functions (and sqrtPC24, fildU32) are binary64-bounded CARRIERS, not x87 registers. Operands and results are doubles, so the
// exponent range is the double range (about 2^-1074 .. 2^1024, with the result rounded onto the binary64 grid, subnormals included) instead of the
// register's 15-bit exponent: (2^600 * 2^600) / 2^600 is infinity here although the x87 holds 2^600. They are exact emulations only for the audited
// float-derived chains of the weapon lane (every operand a binary32 value or a small integer, so intermediates stay far inside the double range).
// A caller outside those bounds needs a (significand, exponent) carrier and explicit `fstp qword` stores first; do not widen the use of these.
// ---------------------------------------------------------------------------------------------------------------------------------------
double pc24AddW(double a, double b);
double pc24SubW(double a, double b);
double pc24MulW(double a, double b);
double pc24DivW(double a, double b);
// `fstp dword`: round a register value to binary32 (nearest even, overflow to infinity, subnormals rounded)
float fstpDword(double x);
// `fild dword` of an unsigned frame count: values >= 2^31 load as negative and `fadd 2^32` rounds to 24 bits (as durationProduct)
double fildU32(std::uint32_t v);
// the MSVCR71 sqrt as the x87 runs it under PC24: fsqrt of the exact value rounded ONCE to a 24-bit significand (integer square root, not a binary64
// sqrt rounded again); the register result, a binary64-bounded carrier
double sqrtPC24(double x);

// ---------------------------------------------------------------------------------------------------------------------------------------
// SSE binary32 operations (retail addss / subss / mulss / divss, cvtsi2ss). One correctly rounded IEEE binary32 operation per call, evaluated in
// the order the caller writes them: use these where the retail code uses SSE, never a bare `a * b + c` (a compiler may fuse it into an FMA or
// evaluate it wider). Out of line on purpose: no call result can be contracted with the next operation.
// ---------------------------------------------------------------------------------------------------------------------------------------
float sseAdd(float a, float b);
float sseSub(float a, float b);
float sseMul(float a, float b);
float sseDiv(float a, float b);
float sseFromInt32(std::int32_t v); // cvtsi2ss (round to nearest even)

// ---------------------------------------------------------------------------------------------------------------------------------------
// Float to integer conversions with the retail results, including invalid input (NaN, infinity, out of range): the x87 "integer indefinite".
//   ftol2       MSVCR71 _ftol2 (RW 0xA3CFA4): FISTP qword with truncation. The callers use the LOW word (eax): ftol2Low32. Invalid -> 0x8000000000000000.
//   fistp32     FISTP dword, round to nearest even (the game's control word). Invalid or out of the int32 range -> 0x80000000.
//   cvttss2si   SSE CVTTSS2SI: truncation. Invalid or out of the int32 range -> 0x80000000.
// ---------------------------------------------------------------------------------------------------------------------------------------
std::int64_t ftol2(double x);
std::uint32_t ftol2Low32(double x);
std::int32_t fistp32(double x);
std::int32_t cvttss2si(float f);

// ---------------------------------------------------------------------------------------------------------------------------------------
// The CRT floor / ceil / fabs on doubles without libm (exact, so the same bits everywhere).
// ---------------------------------------------------------------------------------------------------------------------------------------
double floorD(double x);
double ceilD(double x);
double absD(double x);

// ---------------------------------------------------------------------------------------------------------------------------------------
// binary64 SSE2 arithmetic (addsd / subsd / mulsd / divsd): one IEEE rounding per operation, never contracted (this file is compiled under the
// simulation contract). The double maths retail's MSVC code keeps in SSE2 registers (MOVE-1's pathfinder and movement).
// ---------------------------------------------------------------------------------------------------------------------------------------
double sseAddD(double a, double b);
double sseSubD(double a, double b);
double sseMulD(double a, double b);
double sseDivD(double a, double b);

// ---------------------------------------------------------------------------------------------------------------------------------------
// Deterministic sin / cos / atan2 on doubles (lane WIN-1; the CRT functions, glibc's and the UCRT's, differ in the last bit, which desynchronised a
// Windows and a Linux player: S-081 / S-233). Double-double evaluation (about 2^-100 relative before the final rounding, so in practice the correctly
// rounded result) with IEEE + - * / only: the same bits on every OS and compiler. sin / cos reduce exactly for |x| < 1.6e6 and stay deterministic
// (less accurate) beyond. NaN or infinite input gives the default quiet NaN 0x7FF8000000000000; atan2's zero, infinity and sign cases are C99 Annex F's.
// Retail's x87 fsin / fcos / MSVCR71 results are NOT claimed (S-081 stays open for retail parity).
// ---------------------------------------------------------------------------------------------------------------------------------------
double sinDD(double x);
double cosDD(double x);
double atan2DD(double y, double x);

// The acceptance stops of the shared numeric foundation (docs/STOPS.md S-232, S-233; the tooling stops S-230, S-231 are reported by tools/sim/sim_audit.py).
std::vector<std::string> numericStops();

// The emulation itself, without the fast path of the functions above (lane MODULES-2, performance): the reference the tests compare the fast path with,
// bit for bit. Same contracts as the functions of the same names.
namespace reference
{
float pc24Add(float a, float b);
float pc24Sub(float a, float b);
float pc24Mul(float a, float b);
float pc24Div(float a, float b);
float pc24AddD(double a, double b);
float pc24SubD(double a, double b);
float pc24MulD(double a, double b);
float pc24DivD(double a, double b);
double pc24AddW(double a, double b);
double pc24SubW(double a, double b);
double pc24MulW(double a, double b);
double pc24DivW(double a, double b);
float fstpDword(double x);
double sqrtPC24(double x);
} // namespace reference

} // namespace NumericState
