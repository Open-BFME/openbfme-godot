// OpenBFME. GPL-3.0.
// See Common/NumericState.h for the target facts and what is emulated.

#include "Common/NumericState.h"

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _MSC_VER
#include <float.h>
#endif

// MXCSR access: the compiler builtins on GCC / Clang (no <xmmintrin.h>: libclang, which the simulation audit parses the sources with, cannot compile
// GCC's intrinsic headers), _mm_getcsr / _mm_setcsr on MSVC
#if defined(__SSE__) && (defined(__GNUC__) || defined(__clang__))
#define OPENBFME_HAS_MXCSR 1
#define OPENBFME_GETCSR() ((std::uint32_t)__builtin_ia32_stmxcsr())
#define OPENBFME_SETCSR(v) __builtin_ia32_ldmxcsr((unsigned int)(v))
#elif defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define OPENBFME_HAS_MXCSR 1
#define OPENBFME_GETCSR() ((std::uint32_t)_mm_getcsr())
#define OPENBFME_SETCSR(v) _mm_setcsr((unsigned int)(v))
#else
#define OPENBFME_HAS_MXCSR 0
#endif

// x87 control word access (lane PERF-2, Sol r1): fnstcw / fldcw on GCC / Clang for x86 / x64. MSVC x64 has no inline assembly and its code never runs
// x87 instructions (SSE2 for float and double, the CRT included), so the word is not read there; 32-bit MSVC reads the rounding and the exception masks
// through __control87_2 (unverified on a Windows host: S-232).
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#define OPENBFME_HAS_X87_ASM 1
#else
#define OPENBFME_HAS_X87_ASM 0
#endif

namespace NumericState
{

std::uint32_t readControlRegister()
{
#if OPENBFME_HAS_MXCSR
	return OPENBFME_GETCSR();
#else
	return 0;
#endif
}

void writeControlRegister(std::uint32_t value)
{
#if OPENBFME_HAS_MXCSR
	OPENBFME_SETCSR(value);
#else
	(void)value;
#endif
}

std::uint16_t readX87ControlWord()
{
#if OPENBFME_HAS_X87_ASM
	std::uint16_t cw = 0;
	__asm__ __volatile__("fnstcw %0" : "=m"(cw));
	return cw;
#else
	return kCanonicalX87ControlWord;
#endif
}

void writeX87ControlWord(std::uint16_t value)
{
#if OPENBFME_HAS_X87_ASM
	__asm__ __volatile__("fldcw %0" : : "m"(value));
#else
	(void)value;
#endif
}

bool x87ControlWordIsCanonical()
{
#if OPENBFME_HAS_X87_ASM
	return (readX87ControlWord() & kX87ControlWordMask) == (kCanonicalX87ControlWord & kX87ControlWordMask);
#elif defined(_MSC_VER) && defined(_M_IX86)
	unsigned int x87 = 0, sse = 0;
	__control87_2(0, 0, &x87, &sse);
	return (x87 & _MCW_RC) == _RC_NEAR && (x87 & _MCW_EM) == _MCW_EM; // the precision is setFPMode's (PC24), not checked here
#else
	return true; // no x87 code runs here (see above)
#endif
}

bool floatingPointEnvironmentIsCanonical()
{
#if OPENBFME_HAS_MXCSR
	// bit 6 DAZ, bits 7..12 exception masks, bits 13..14 rounding control, bit 15 FTZ (the low six bits are sticky exception flags)
	return (OPENBFME_GETCSR() & 0xFFC0u) == kCanonicalControlRegister && x87ControlWordIsCanonical();
#else
	return std::fegetround() == FE_TONEAREST && x87ControlWordIsCanonical();
#endif
}

void normalizeFloatingPointEnvironment()
{
	// the default environment first: x87 control word, exception flags, MXCSR (glibc: 0x1F80, MSVC: _fpreset)
	std::fesetenv(FE_DFL_ENV);
	std::fesetround(FE_TONEAREST);
	std::feclearexcept(FE_ALL_EXCEPT);
#ifdef _MSC_VER
	_fpreset();
	unsigned int current = 0;
	// nearest rounding, every exception masked, denormals kept (_DN_SAVE); precision control is not touched here (x64 rejects _MCW_PC)
	_controlfp_s(&current, _DN_SAVE | _EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT | _RC_NEAR,
		_MCW_DN | _MCW_EM | _MCW_RC);
#endif
#if OPENBFME_HAS_MXCSR
	// explicit, whatever the C library's default environment says: round to nearest, all exceptions masked, FTZ and DAZ OFF (gradual underflow)
	OPENBFME_SETCSR(kCanonicalControlRegister);
#endif
#if OPENBFME_HAS_X87_ASM
	writeX87ControlWord(kCanonicalX87ControlWord); // likewise the x87 word (glibc's default: 0x037F)
#endif
}

void setFPMode()
{
	normalizeFloatingPointEnvironment();
#if defined(_MSC_VER) && defined(_M_IX86)
	// RW 0x440809: _fpreset(); _controlfp((cw & 0xFFFEFCFF) | 0x20000, 0x30300): the x87 precision control 24 bits (32-bit x86 only; the port's
	// arithmetic is SSE2 plus the PC24 emulation below, which never reads the x87 state)
	unsigned int current = 0;
	_controlfp_s(&current, 0, 0);
	unsigned int ignored = 0;
	_controlfp_s(&ignored, (current & 0xFFFEFCFFu) | 0x20000u, 0x30300u);
#endif
	// x64 / other targets: precision control does not exist (_MCW_PC is rejected); the emulation never reads the FPU precision state
}

namespace
{

enum Kind
{
	Zero,
	Finite,
	Inf,
	NaN
};

// value = (-1)^neg * m * 2^e with m an integer (finite only)
struct Num
{
	Kind kind = Zero;
	bool neg = false;
	std::uint64_t m = 0;
	int e = 0;
	std::uint32_t nanBits = 0;
};

Num unpackFloat(float f)
{
	std::uint32_t bits;
	std::memcpy(&bits, &f, sizeof(bits));
	Num n;
	n.neg = (bits >> 31) != 0;
	const std::uint32_t exp = (bits >> 23) & 0xFF;
	const std::uint32_t frac = bits & 0x7FFFFF;
	if (exp == 0xFF)
	{
		n.kind = frac ? NaN : Inf;
		n.nanBits = bits | 0x400000u;
		return n;
	}
	if (exp == 0)
	{
		if (frac == 0)
		{
			n.kind = Zero;
			return n;
		}
		n.kind = Finite;
		n.m = frac;
		n.e = -149;
		while (!(n.m & 0x800000u)) // normalise to a 24-bit significand (x87 loads denormals exactly)
		{
			n.m <<= 1;
			--n.e;
		}
		return n;
	}
	n.kind = Finite;
	n.m = frac | 0x800000u;
	n.e = (int)exp - 127 - 23;
	return n;
}

int bitLength(std::uint64_t v)
{
	int n = 0;
	while (v)
	{
		++n;
		v >>= 1;
	}
	return n;
}

// Round the exact value (m + sticky fraction) * 2^e to a 24-bit significand, nearest-even, wide
// exponent range. Returns m in [2^23, 2^24).
void roundTo24(std::uint64_t &m, int &e, bool sticky)
{
	const int n = bitLength(m);
	if (n <= 24)
	{
		m <<= (24 - n);
		e -= (24 - n);
		return; // exact (callers never pass a sticky bit together with fewer than 24 bits)
	}
	const int shift = n - 24;
	const std::uint64_t rem = m & ((1ull << shift) - 1);
	const std::uint64_t half = 1ull << (shift - 1);
	m >>= shift;
	e += shift;
	if (rem > half || (rem == half && (sticky || (m & 1))))
	{
		++m;
		if (m == (1ull << 24))
		{
			m >>= 1;
			++e;
		}
	}
}

float quietNaN(std::uint32_t bits)
{
	float f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

float defaultNaN()
{
	return quietNaN(0xFFC00000u);
}

float makeInf(bool neg)
{
	return quietNaN(neg ? 0xFF800000u : 0x7F800000u);
}

float makeZero(bool neg)
{
	return quietNaN(neg ? 0x80000000u : 0u);
}

// Round (-1)^neg * m * 2^e (m != 0, up to 64 bits) to the IEEE binary format with a `p`-bit significand and exponents emin..emax (float: 24, -126,
// 127; double: 53, -1022, 1023), nearest even, with ONE rounding: values below the normal range are rounded straight onto the subnormal grid
// (2^(emin - p + 1)), never through an intermediate precision. Returns the encoding (sign included); overflow gives infinity. This is `fstp dword` /
// `fstp qword` of an x87 register value.
std::uint64_t roundToIeee(bool neg, std::uint64_t m, int e, int p, int emin, int emax, int expBits)
{
	const int n = bitLength(m);
	int E = e + n - 1; // exponent of the leading bit
	const int quantum = (E >= emin ? E : emin) - (p - 1); // exponent of the last kept bit
	const int drop = quantum - e;                          // bits below the grid
	std::uint64_t q;
	if (drop <= 0)
	{
		q = m << (-drop); // exact
	}
	else if (drop > 64)
	{
		q = 0; // below half of the smallest step
	}
	else
	{
		const std::uint64_t rem = drop == 64 ? m : (m & ((1ull << drop) - 1));
		const std::uint64_t half = 1ull << (drop - 1);
		q = drop == 64 ? 0 : (m >> drop);
		if (rem > half || (rem == half && (q & 1)))
		{
			++q;
		}
	}
	const std::uint64_t fracMask = (1ull << (p - 1)) - 1;
	const std::uint64_t signBit = neg ? (1ull << (p - 1 + expBits)) : 0ull;
	if (E >= emin)
	{
		if (q == (1ull << p)) // the carry out of the significand
		{
			q >>= 1;
			++E;
		}
		if (E > emax)
		{
			return signBit | ((std::uint64_t)(2 * emax + 1) << (p - 1)); // infinity
		}
		return signBit | ((std::uint64_t)(E + emax) << (p - 1)) | (q & fracMask);
	}
	// subnormal grid: q < 2^(p-1) or exactly 2^(p-1), which encodes the smallest normal number
	return signBit | q;
}

// fstp dword: store a value (m, e) as float32 with a single nearest-even rounding (subnormals included).
float storeFloat(bool neg, std::uint64_t m, int e)
{
	const std::uint32_t bits = (std::uint32_t)roundToIeee(neg, m, e, 24, -126, 127, 8);
	float f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

// An x87 result before any store: kind, sign and a 24-bit significand (m in [2^23, 2^24)) with a wide exponent. narrow() is `fstp dword`, widen() is
// `fstp qword` of a PC24 value (exact; only the double range limits the exponent).
struct Out
{
	Kind kind = Zero;
	bool neg = false;
	std::uint64_t m = 0;
	int e = 0;
	std::uint32_t nanBits = 0;
};

Out outFinite(bool neg, std::uint64_t m, int e)
{
	Out o;
	o.kind = Finite;
	o.neg = neg;
	o.m = m;
	o.e = e;
	return o;
}

Out outInf(bool neg)
{
	Out o;
	o.kind = Inf;
	o.neg = neg;
	return o;
}

Out outZero(bool neg)
{
	Out o;
	o.kind = Zero;
	o.neg = neg;
	return o;
}

Out outNaN(std::uint32_t bits)
{
	Out o;
	o.kind = NaN;
	o.nanBits = bits;
	return o;
}

Out outDefaultNaN()
{
	return outNaN(0xFFC00000u);
}

float narrow(const Out &o)
{
	switch (o.kind)
	{
		case Zero: return makeZero(o.neg);
		case Inf: return makeInf(o.neg);
		case NaN: return quietNaN(o.nanBits);
		default: return storeFloat(o.neg, o.m, o.e);
	}
}

double widen(const Out &o)
{
	std::uint64_t bits = 0;
	switch (o.kind)
	{
		case Zero:
			bits = o.neg ? 0x8000000000000000ull : 0ull;
			break;
		case Inf:
			bits = o.neg ? 0xFFF0000000000000ull : 0x7FF0000000000000ull;
			break;
		case NaN:
			bits = 0x7FF8000000000000ull | ((o.nanBits & 0x80000000u) ? 0x8000000000000000ull : 0ull);
			break;
		default:
		{
			// `fstp qword`: the 24-bit value rounds once onto the binary64 grid (exact unless it lies in the subnormal range or overflows)
			bits = roundToIeee(o.neg, o.m, o.e, 53, -1022, 1023, 11);
		}
	}
	double d;
	std::memcpy(&d, &bits, sizeof(d));
	return d;
}

Out finish(bool neg, std::uint64_t m, int e, bool sticky)
{
	roundTo24(m, e, sticky);
	return outFinite(neg, m, e);
}

bool anyNaN(const Num &a, const Num &b, Out &out)
{
	if (a.kind == NaN)
	{
		out = outNaN(a.nanBits);
		return true;
	}
	if (b.kind == NaN)
	{
		out = outNaN(b.nanBits);
		return true;
	}
	return false;
}

// `shift` is the number of guard bits added below the larger significand.  The float path uses 32 (24-bit
// significands); the wide path normalises both to 63 bits and uses 0, which still keeps the sum within 64 bits.
Out addNumsShift(Num a, Num b, int shift)
{
	Out nan;
	if (anyNaN(a, b, nan))
	{
		return nan;
	}
	if (a.kind == Inf || b.kind == Inf)
	{
		if (a.kind == Inf && b.kind == Inf && a.neg != b.neg)
		{
			return outDefaultNaN();
		}
		return outInf(a.kind == Inf ? a.neg : b.neg);
	}
	if (a.kind == Zero && b.kind == Zero)
	{
		return outZero(a.neg && b.neg); // round-to-nearest: -0 only if both are -0
	}
	if (a.kind == Zero)
	{
		return finish(b.neg, b.m, b.e, false);
	}
	if (b.kind == Zero)
	{
		return finish(a.neg, a.m, a.e, false);
	}

	// order by magnitude: x has the larger (exponent, significand)
	const bool aBigger = (a.e > b.e) || (a.e == b.e && a.m >= b.m);
	Num x = aBigger ? a : b;
	Num y = aBigger ? b : a;
	const int diff = x.e - y.e;

	const std::uint64_t X = x.m << shift;
	std::uint64_t Y;
	bool sticky = false;
	if (diff >= 64)
	{
		Y = 0;
		sticky = true;
	}
	else
	{
		const std::uint64_t full = y.m << shift;
		Y = full >> diff;
		sticky = diff > 0 && (full & ((1ull << diff) - 1)) != 0;
	}
	const int eR = x.e - shift;

	std::uint64_t R;
	if (x.neg == y.neg)
	{
		R = X + Y;
	}
	else
	{
		R = X - Y - (sticky ? 1 : 0); // the discarded fraction of Y is a borrow
		if (R == 0 && !sticky)
		{
			return outZero(false); // exact cancellation is +0 under round-to-nearest
		}
	}
	return finish(x.neg, R, eR, sticky);
}

Out addNums(Num a, Num b)
{
	return addNumsShift(a, b, 32);
}

// A double (53-bit significand, normalised: leading bit 52).  Subnormal doubles are normalised too.
Num unpackDouble(double d)
{
	std::uint64_t bits;
	std::memcpy(&bits, &d, sizeof(bits));
	Num n;
	n.neg = (bits >> 63) != 0;
	const std::uint32_t exp = (std::uint32_t)((bits >> 52) & 0x7FF);
	const std::uint64_t frac = bits & ((1ull << 52) - 1);
	if (exp == 0x7FF)
	{
		n.kind = frac ? NaN : Inf;
		n.nanBits = 0x7FC00000u | (n.neg ? 0x80000000u : 0u);
		return n;
	}
	if (exp == 0)
	{
		if (frac == 0)
		{
			n.kind = Zero;
			return n;
		}
		n.kind = Finite;
		n.m = frac;
		n.e = -1074;
		while (!(n.m & (1ull << 52)))
		{
			n.m <<= 1;
			--n.e;
		}
		return n;
	}
	n.kind = Finite;
	n.m = frac | (1ull << 52);
	n.e = (int)exp - 1075;
	return n;
}

// Shift a finite significand so its leading bit is bit `topBit` (exact: the significand never exceeds 53 bits).
void normalizeTo(Num &n, int topBit)
{
	if (n.kind != Finite)
	{
		return;
	}
	const int shift = topBit + 1 - bitLength(n.m);
	n.m <<= shift;
	n.e -= shift;
}

// 64 x 64 -> 128 bit multiplication on 32-bit limbs
void mul64(std::uint64_t a, std::uint64_t b, std::uint64_t &hi, std::uint64_t &lo)
{
	const std::uint64_t aLo = a & 0xFFFFFFFFull, aHi = a >> 32;
	const std::uint64_t bLo = b & 0xFFFFFFFFull, bHi = b >> 32;
	const std::uint64_t p0 = aLo * bLo;
	const std::uint64_t p1 = aLo * bHi;
	const std::uint64_t p2 = aHi * bLo;
	const std::uint64_t p3 = aHi * bHi;
	const std::uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFull) + (p2 & 0xFFFFFFFFull);
	lo = (p0 & 0xFFFFFFFFull) | (mid << 32);
	hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
}

} // namespace

static Out pc24AddOut(float a, float b)
{
	return addNums(unpackFloat(a), unpackFloat(b));
}

static Out pc24SubOut(float a, float b)
{
	Num nb = unpackFloat(b);
	if (nb.kind != NaN)
	{
		nb.neg = !nb.neg;
	}
	return addNums(unpackFloat(a), nb);
}

static Out pc24SubInt32Out(float a, std::int32_t b)
{
	// addNums orders its operands by (exponent, significand), so both must share one normalisation: 31 bits,
	// the most an int32 magnitude needs (a float's 24-bit significand is shifted up by 7 and stays exact).
	Num na = unpackFloat(a);
	if (na.kind == Finite)
	{
		na.m <<= 7;
		na.e -= 7;
	}
	Num nb;
	nb.neg = b >= 0; // subtraction inverts the operand's sign (an integer zero is +0, so it subtracts as -0)
	if (b != 0)
	{
		nb.kind = Finite;
		nb.m = b < 0 ? (std::uint64_t)(-(std::int64_t)b) : (std::uint64_t)b;
		nb.e = 0;
		const int shift = 31 - bitLength(nb.m); // 2^31 itself has 32 bits: shift by -1 is not needed, see below
		if (shift >= 0)
		{
			nb.m <<= shift;
			nb.e -= shift;
		}
		else
		{
			nb.m >>= 1; // only |INT32_MIN| = 2^31 gets here, a power of two: exact
			nb.e += 1;
		}
	}
	return addNums(na, nb);
}

static Out pc24AddDOut(double a, double b)
{
	Num x = unpackDouble(a);
	Num y = unpackDouble(b);
	normalizeTo(x, 62);
	normalizeTo(y, 62);
	return addNumsShift(x, y, 0);
}

static Out pc24SubDOut(double a, double b)
{
	Num x = unpackDouble(a);
	Num y = unpackDouble(b);
	if (y.kind != NaN)
	{
		y.neg = !y.neg;
	}
	normalizeTo(x, 62);
	normalizeTo(y, 62);
	return addNumsShift(x, y, 0);
}

static Out pc24MulDOut(double fa, double fb)
{
	const Num a = unpackDouble(fa);
	const Num b = unpackDouble(fb);
	Out nan;
	if (anyNaN(a, b, nan))
	{
		return nan;
	}
	const bool neg = a.neg != b.neg;
	if ((a.kind == Inf && b.kind == Zero) || (a.kind == Zero && b.kind == Inf))
	{
		return outDefaultNaN();
	}
	if (a.kind == Inf || b.kind == Inf)
	{
		return outInf(neg);
	}
	if (a.kind == Zero || b.kind == Zero)
	{
		return outZero(neg);
	}
	std::uint64_t hi, lo;
	mul64(a.m, b.m, hi, lo);
	int e = a.e + b.e;
	std::uint64_t m;
	bool sticky = false;
	if (hi == 0)
	{
		m = lo;
	}
	else
	{
		const int sh = bitLength(hi); // 1..42: keep the top 64 bits, the rest is the sticky bit
		m = (hi << (64 - sh)) | (lo >> sh);
		sticky = (lo & ((1ull << sh) - 1)) != 0;
		e += sh;
	}
	return finish(neg, m, e, sticky);
}

static Out pc24DivDOut(double fa, double fb)
{
	const Num a = unpackDouble(fa);
	const Num b = unpackDouble(fb);
	Out nan;
	if (anyNaN(a, b, nan))
	{
		return nan;
	}
	const bool neg = a.neg != b.neg;
	if ((a.kind == Zero && b.kind == Zero) || (a.kind == Inf && b.kind == Inf))
	{
		return outDefaultNaN();
	}
	if (a.kind == Inf || b.kind == Zero)
	{
		return outInf(neg);
	}
	if (a.kind == Zero || b.kind == Inf)
	{
		return outZero(neg);
	}
	// both significands are normalised to 53 bits, so the ratio is in (0.5, 2): 42 long-division steps give a
	// quotient of at least 41 bits plus an exact sticky bit from the remainder
	std::uint64_t r = a.m;
	std::uint64_t q = 0;
	for (int i = 0; i < 42; ++i)
	{
		q <<= 1;
		if (r >= b.m)
		{
			r -= b.m;
			q |= 1;
		}
		r <<= 1;
	}
	return finish(neg, q, a.e - b.e - 41, r != 0);
}

static Out pc24MulOut(float fa, float fb)
{
	const Num a = unpackFloat(fa);
	const Num b = unpackFloat(fb);
	Out nan;
	if (anyNaN(a, b, nan))
	{
		return nan;
	}
	const bool neg = a.neg != b.neg;
	if ((a.kind == Inf && b.kind == Zero) || (a.kind == Zero && b.kind == Inf))
	{
		return outDefaultNaN();
	}
	if (a.kind == Inf || b.kind == Inf)
	{
		return outInf(neg);
	}
	if (a.kind == Zero || b.kind == Zero)
	{
		return outZero(neg);
	}
	return finish(neg, a.m * b.m, a.e + b.e, false);
}

static Out pc24DivOut(float fa, float fb)
{
	const Num a = unpackFloat(fa);
	const Num b = unpackFloat(fb);
	Out nan;
	if (anyNaN(a, b, nan))
	{
		return nan;
	}
	const bool neg = a.neg != b.neg;
	if ((a.kind == Zero && b.kind == Zero) || (a.kind == Inf && b.kind == Inf))
	{
		return outDefaultNaN();
	}
	if (a.kind == Inf || b.kind == Zero)
	{
		return outInf(neg); // inf / x and x / 0
	}
	if (a.kind == Zero || b.kind == Inf)
	{
		return outZero(neg);
	}
	const std::uint64_t num = a.m << 30;
	const std::uint64_t q = num / b.m;
	const bool sticky = (num % b.m) != 0;
	return finish(neg, q, a.e - b.e - 30, sticky);
}


// ---- the fast path (lane MODULES-2, performance) ----------------------------------------------------------------------------------------------------
// The emulation above is exact for every input; for the common case a cheaper evaluation gives the same bits. With both operands binary32 values,
// one binary64 operation (correctly rounded, p = 53 >= 2 * 24 + 2) followed by the conversion to binary32 is the correctly rounded binary32 result of
// +, -, *, / and sqrt (double rounding is innocuous at that precision: S. A. Figueroa, "When is double rounding innocuous?", SIGNUM 1995). That equals
// the x87 PC24 result whenever the result lies in the binary32 NORMAL range: there the 24-bit rounding with the wide exponent and the binary32 rounding
// are the same rounding. Everything else takes the emulation: an operand that is not a binary32 value (a 53-bit double, a wide exponent), a NaN, an
// infinity, a zero or subnormal result (the subnormal grid differs: the retail 0x0077CF3C case), an overflow, and any non-canonical floating-point
// environment (another rounding mode, FTZ / DAZ: the hardware conversions would differ, the emulation does not). Checked bit for bit against the
// emulation by tests/test_numeric_state.cpp.
namespace
{
constexpr double kMinNormalFloat = 1.1754943508222875e-38; // 2^-126

inline bool fastEnvironment()
{
#if OPENBFME_HAS_MXCSR && OPENBFME_HAS_X87_ASM
	// floatingPointEnvironmentIsCanonical, inline: both words read on every call (lane PERF-2, Sol r1: an x87-only rounding change must also leave the
	// fast path)
	std::uint16_t cw = 0;
	__asm__ __volatile__("fnstcw %0" : "=m"(cw));
	return (OPENBFME_GETCSR() & 0xFFC0u) == kCanonicalControlRegister && (cw & kX87ControlWordMask) == (kCanonicalX87ControlWord & kX87ControlWordMask);
#elif OPENBFME_HAS_MXCSR
	return (OPENBFME_GETCSR() & 0xFFC0u) == kCanonicalControlRegister && x87ControlWordIsCanonical();
#else
	return floatingPointEnvironmentIsCanonical();
#endif
}

inline bool isFloatValue(double a, float &f)
{
	f = (float)a;
	return (double)f == a; // false for a NaN, a value beyond the float range or with more than 24 significant bits
}

// the binary32 rounding of r when it is a normal binary32 value (r is the binary64 result of one operation on binary32 operands)
inline bool normalFloatResult(double r, float &f)
{
	const double m = r < 0.0 ? -r : r;
	if (!(m >= kMinNormalFloat)) // a NaN fails too
	{
		return false;
	}
	f = (float)r;
	return f - f == 0.0f; // finite: an overflow to infinity fails
}

enum FastOp
{
	FAST_ADD,
	FAST_SUB,
	FAST_MUL,
	FAST_DIV
};

inline bool fastFloat(FastOp op, float a, float b, float &f)
{
	if (!fastEnvironment())
	{
		return false;
	}
	const double x = (double)a, y = (double)b;
	double r;
	switch (op)
	{
		case FAST_ADD: r = x + y; break;
		case FAST_SUB: r = x - y; break;
		case FAST_MUL: r = x * y; break;
		default: r = x / y; break;
	}
	return normalFloatResult(r, f) && fastEnvironment();
}

inline bool fastDouble(FastOp op, double a, double b, float &f)
{
	if (!fastEnvironment())
	{
		return false;
	}
	float fa, fb;
	return isFloatValue(a, fa) && isFloatValue(b, fb) && fastFloat(op, fa, fb, f);
}
} // namespace

namespace reference
{
float pc24Add(float a, float b) { return narrow(pc24AddOut(a, b)); }
float pc24Sub(float a, float b) { return narrow(pc24SubOut(a, b)); }
float pc24Mul(float a, float b) { return narrow(pc24MulOut(a, b)); }
float pc24Div(float a, float b) { return narrow(pc24DivOut(a, b)); }
float pc24AddD(double a, double b) { return narrow(pc24AddDOut(a, b)); }
float pc24SubD(double a, double b) { return narrow(pc24SubDOut(a, b)); }
float pc24MulD(double a, double b) { return narrow(pc24MulDOut(a, b)); }
float pc24DivD(double a, double b) { return narrow(pc24DivDOut(a, b)); }
double pc24AddW(double a, double b) { return widen(pc24AddDOut(a, b)); }
double pc24SubW(double a, double b) { return widen(pc24SubDOut(a, b)); }
double pc24MulW(double a, double b) { return widen(pc24MulDOut(a, b)); }
double pc24DivW(double a, double b) { return widen(pc24DivDOut(a, b)); }
} // namespace reference

float pc24Add(float a, float b) { float f; return fastFloat(FAST_ADD, a, b, f) ? f : narrow(pc24AddOut(a, b)); }
float pc24Sub(float a, float b) { float f; return fastFloat(FAST_SUB, a, b, f) ? f : narrow(pc24SubOut(a, b)); }
float pc24Mul(float a, float b) { float f; return fastFloat(FAST_MUL, a, b, f) ? f : narrow(pc24MulOut(a, b)); }
float pc24Div(float a, float b) { float f; return fastFloat(FAST_DIV, a, b, f) ? f : narrow(pc24DivOut(a, b)); }
float pc24SubInt32(float a, std::int32_t b) { return narrow(pc24SubInt32Out(a, b)); }
float pc24AddD(double a, double b) { float f; return fastDouble(FAST_ADD, a, b, f) ? f : narrow(pc24AddDOut(a, b)); }
float pc24SubD(double a, double b) { float f; return fastDouble(FAST_SUB, a, b, f) ? f : narrow(pc24SubDOut(a, b)); }
float pc24MulD(double a, double b) { float f; return fastDouble(FAST_MUL, a, b, f) ? f : narrow(pc24MulDOut(a, b)); }
float pc24DivD(double a, double b) { float f; return fastDouble(FAST_DIV, a, b, f) ? f : narrow(pc24DivDOut(a, b)); }

double pc24AddW(double a, double b) { float f; return fastDouble(FAST_ADD, a, b, f) ? (double)f : widen(pc24AddDOut(a, b)); }
double pc24SubW(double a, double b) { float f; return fastDouble(FAST_SUB, a, b, f) ? (double)f : widen(pc24SubDOut(a, b)); }
double pc24MulW(double a, double b) { float f; return fastDouble(FAST_MUL, a, b, f) ? (double)f : widen(pc24MulDOut(a, b)); }
double pc24DivW(double a, double b) { float f; return fastDouble(FAST_DIV, a, b, f) ? (double)f : widen(pc24DivDOut(a, b)); }

// `fstp dword` of a register value held as a double: ONE rounding straight to binary32 (hardware `fstp dword` rounds the register's
// significand once, whatever the precision control says; a preliminary 24-bit rounding would double-round the subnormal results)
float fstpDword(double x)
{
	// the fast path: in the canonical environment the hardware conversion is the same single IEEE rounding (nearest even, subnormals, overflow to
	// infinity); a NaN keeps the emulated payload rule
	if (fastEnvironment() && x == x)
	{
		return (float)x;
	}
	return reference::fstpDword(x);
}

namespace reference
{
float fstpDword(double x)
{
	const Num n = unpackDouble(x);
	switch (n.kind)
	{
		case NaN: return quietNaN(n.nanBits);
		case Inf: return makeInf(n.neg);
		case Zero: return makeZero(n.neg);
		default: break;
	}
	return storeFloat(n.neg, n.m, n.e);
}
} // namespace reference

double durationProduct(std::uint32_t ms, float scale)
{
	// signed fild: an input >= 2^31 loads as ms - 2^32, then fadd 2^32 restores it, rounded to 24 bits
	std::uint64_t m = ms;
	int e = 0;
	if (ms == 0)
	{
		return 0.0;
	}
	if (ms >= 0x80000000u)
	{
		roundTo24(m, e, false);
	}
	const Num s = unpackFloat(scale);
	if (s.kind == Zero)
	{
		return 0.0;
	}
	if (s.kind == Inf)
	{
		return widen(outInf(s.neg));
	}
	if (s.kind == NaN)
	{
		return widen(outNaN(s.nanBits));
	}
	std::uint64_t prod = m * s.m; // < 2^32 * 2^24
	int pe = e + s.e;
	roundTo24(prod, pe, false);
	return widen(outFinite(s.neg, prod, pe));
}

std::uint32_t ceilScaled(std::uint32_t ms, float scale)
{
	return ftol2Low32(ceilD(durationProduct(ms, scale))); // MSVCR71 ceil, then _ftol2 (callers use the low word)
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// SSE binary32 operations: one correctly rounded IEEE operation each, out of line so that no compiler can fuse a multiply into an add across a
// statement (the retail mulss / addss are separate instructions). FLT_EVAL_METHOD is asserted in the header.
// ---------------------------------------------------------------------------------------------------------------------------------------
float sseAdd(float a, float b)
{
	return a + b;
}

float sseSub(float a, float b)
{
	return a - b;
}

float sseMul(float a, float b)
{
	return a * b;
}

float sseDiv(float a, float b)
{
	return a / b;
}

float sseFromInt32(std::int32_t v)
{
	return (float)v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// Conversions with the retail results for invalid input (the x87 "integer indefinite" 0x80000000 / 0x8000000000000000).
// ---------------------------------------------------------------------------------------------------------------------------------------
std::int64_t ftol2(double x)
{
	if (!(x > -9223372036854775808.0 && x < 9223372036854775808.0)) // NaN, infinities and anything outside int64
	{
		return INT64_MIN;
	}
	return (std::int64_t)x; // in range: truncation toward zero is defined
}

std::uint32_t ftol2Low32(double x)
{
	return (std::uint32_t)((std::uint64_t)ftol2(x) & 0xFFFFFFFFull);
}

std::int32_t fistp32(double x)
{
	if (!(x > -4503599627370496.0 && x < 4503599627370496.0)) // NaN, infinities and anything the dword cannot hold
	{
		return INT32_MIN;
	}
	const double t = (double)(std::int64_t)x; // truncation
	const double diff = x - t;                // exact: |x| < 2^52
	double r = t;
	if (diff > 0.5 || (diff == 0.5 && ((std::int64_t)t & 1) != 0))
	{
		r = t + 1.0;
	}
	else if (diff < -0.5 || (diff == -0.5 && ((std::int64_t)t & 1) != 0))
	{
		r = t - 1.0;
	}
	if (!(r >= -2147483648.0 && r <= 2147483647.0))
	{
		return INT32_MIN;
	}
	return (std::int32_t)r;
}

std::int32_t cvttss2si(float f)
{
	const double d = (double)f;
	if (!(d > -2147483649.0 && d < 2147483648.0))
	{
		return INT32_MIN;
	}
	return (std::int32_t)d;
}

double fildU32(std::uint32_t v)
{
	if (v < 0x80000000u)
	{
		return (double)v;
	}
	std::uint64_t m = v;
	int e = 0;
	roundTo24(m, e, false); // fild loads v - 2^32, fadd 2^32 rounds the sum to 24 bits
	return widen(outFinite(false, m, e));
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// The CRT functions the weapon code calls, without libm.
// ---------------------------------------------------------------------------------------------------------------------------------------
double floorD(double x)
{
	if (!(x > -4503599627370496.0 && x < 4503599627370496.0) || x == 0.0)
	{
		return x; // NaN, infinities, integral magnitudes and the signed zeros
	}
	const double t = (double)(std::int64_t)x;
	return t > x ? t - 1.0 : t;
}

double ceilD(double x)
{
	if (!(x > -4503599627370496.0 && x < 4503599627370496.0) || x == 0.0)
	{
		return x;
	}
	const double t = (double)(std::int64_t)x;
	const double r = t < x ? t + 1.0 : t;
	if (r == 0.0 && x < 0.0)
	{
		return -0.0;
	}
	return r;
}

double sseAddD(double a, double b)
{
	return a + b;
}

double sseSubD(double a, double b)
{
	return a - b;
}

double sseMulD(double a, double b)
{
	return a * b;
}

double sseDivD(double a, double b)
{
	return a / b;
}

double absD(double x)
{
	std::uint64_t bits;
	std::memcpy(&bits, &x, sizeof(bits));
	bits &= 0x7FFFFFFFFFFFFFFFull;
	std::memcpy(&x, &bits, sizeof(bits));
	return x;
}

double sqrtPC24(double x)
{
	// the fast path: a positive binary32 operand (normal or subnormal: its root is normal), the binary64 root rounded to binary32 (Figueroa, above)
	float fx;
	if (fastEnvironment() && isFloatValue(x, fx) && fx > 0.0f)
	{
		float f;
		if (normalFloatResult(std::sqrt((double)fx), f) && fastEnvironment())
		{
			return (double)f;
		}
	}
	return reference::sqrtPC24(x);
}

namespace reference
{
double sqrtPC24(double x)
{
	if (x != x)
	{
		return widen(outNaN(unpackDouble(x).nanBits));
	}
	if (x == 0.0)
	{
		return x; // +-0
	}
	Num n = unpackDouble(x);
	if (n.neg)
	{
		return widen(outDefaultNaN()); // fsqrt of a negative: the default NaN
	}
	if (n.kind == Inf)
	{
		return x;
	}
	// The exact square root rounded ONCE to a 24-bit significand (fsqrt under PC24). Scale the significand to 64 bits (top bit 63 or 62) with an even
	// exponent, take the integer square root of the 64-bit value (32 root bits, digit by digit) and use the remainder as the sticky bit. A binary64
	// square root followed by a 24-bit rounding is NOT the same: the double rounding can move a value across a 24-bit midpoint.
	std::uint64_t m = n.m << (63 - (bitLength(n.m) - 1));
	int e = n.e - (63 - (bitLength(n.m) - 1));
	if (e & 1)
	{
		m >>= 1; // exact: the low bits of m are zero
		e += 1;
	}
	std::uint64_t rem = 0;
	std::uint64_t root = 0;
	for (int i = 0; i < 32; ++i)
	{
		rem = (rem << 2) | (m >> 62);
		m <<= 2;
		const std::uint64_t trial = (root << 2) | 1;
		root <<= 1;
		if (rem >= trial)
		{
			rem -= trial;
			root |= 1;
		}
	}
	return widen(finish(false, root, e / 2, rem != 0));
}
} // namespace reference

// ---------------------------------------------------------------------------------------------------------------------------------------
// Deterministic sin / cos / atan2 on doubles (lane WIN-1). See NumericState.h. Double-double ("DD": an unevaluated sum hi + lo, |lo| <= ulp(hi) / 2)
// arithmetic made of IEEE + - * / only, in a fixed order (Dekker / Knuth error-free transformations; no FMA: this file is built under the simulation
// contract, which forbids contraction), so every conforming compiler on every OS computes the same bits. The constants are exact hexadecimal text
// generated by tools/sim/gen_dd_trig_tables.py.
// ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct DD
{
	double hi, lo;
};

#include "Common/System/NumericTrig.inc"

// the three parts of pi/2 with at most 33 significant bits each (fdlibm's pio2_1, pio2_2, pio2_3) and the rest (pio2_3t): k * part is exact for |k| < 2^20
const double kPio2Part1 = 0x1.921fb544p+0;
const double kPio2Part2 = 0x1.0b4611a6p-34;
const double kPio2Part3 = 0x1.3198a2ep-69;
const double kPio2Part3Tail = 0x1.b839a252049c1p-104;
const double kTwoOverPi = 0x1.45f306dc9c883p-1;

inline DD quickTwoSum(double a, double b) // |a| >= |b|
{
	const double s = a + b;
	return { s, b - (s - a) };
}

inline DD twoSum(double a, double b)
{
	const double s = a + b;
	const double bb = s - a;
	return { s, (a - (s - bb)) + (b - bb) };
}

inline void splitDekker(double a, double &hi, double &lo)
{
	const double c = 134217729.0 * a; // 2^27 + 1
	hi = c - (c - a);
	lo = a - hi;
}

inline DD twoProd(double a, double b)
{
	const double p = a * b;
	double ah, al, bh, bl;
	splitDekker(a, ah, al);
	splitDekker(b, bh, bl);
	return { p, ((ah * bh - p) + ah * bl + al * bh) + al * bl };
}

inline DD ddAdd(DD a, DD b)
{
	DD s = twoSum(a.hi, b.hi);
	const DD t = twoSum(a.lo, b.lo);
	s.lo += t.hi;
	s = quickTwoSum(s.hi, s.lo);
	s.lo += t.lo;
	return quickTwoSum(s.hi, s.lo);
}

// a + b when |a| dominates |b| without cancellation (|b| <= |a| / 8): the cheaper sum, as exact as ddAdd there (a Horner step c[k] + p z)
inline DD ddAddDominated(DD a, DD b)
{
	DD s = quickTwoSum(a.hi, b.hi);
	s.lo += a.lo + b.lo;
	return quickTwoSum(s.hi, s.lo);
}

inline DD ddNeg(DD a)
{
	return { -a.hi, -a.lo };
}

inline DD ddSub(DD a, DD b)
{
	return ddAdd(a, ddNeg(b));
}

inline DD ddMul(DD a, DD b)
{
	DD p = twoProd(a.hi, b.hi);
	p.lo += a.hi * b.lo + a.lo * b.hi;
	return quickTwoSum(p.hi, p.lo);
}

inline DD ddMulD(DD a, double b)
{
	DD p = twoProd(a.hi, b);
	p.lo += a.lo * b;
	return quickTwoSum(p.hi, p.lo);
}

// a / b to about 2^-104: one correction step from the exact remainder
inline DD ddDiv(DD a, DD b)
{
	const double q1 = a.hi / b.hi;
	const DD r = ddSub(a, ddMulD(b, q1));
	return quickTwoSum(q1, r.hi / b.hi);
}

// a / b of two doubles to about 2^-105: the remainder a - q1 b is exact (twoProd, and a - p by Sterbenz)
inline DD ddDivD(double a, double b)
{
	const double q1 = a / b;
	const DD p = twoProd(q1, b);
	return quickTwoSum(q1, ((a - p.hi) - p.lo) / b);
}

inline bool isNaND(double x)
{
	return x != x;
}

inline bool isInfD(double x)
{
	return x - x != 0.0 && !isNaND(x);
}

inline bool signBitD(double x)
{
	std::uint64_t u;
	std::memcpy(&u, &x, sizeof u);
	return (u >> 63) != 0;
}

inline double canonicalNaN()
{
	const std::uint64_t u = 0x7FF8000000000000ull;
	double d;
	std::memcpy(&d, &u, sizeof d);
	return d;
}

// 2^e as a double (normal range only)
double pow2(int e)
{
	const std::uint64_t u = (std::uint64_t)(e + 1023) << 52;
	double d;
	std::memcpy(&d, &u, sizeof d);
	return d;
}

// x - k pi/2 for a huge |x| (Payne-Hanek, Sol r1: the k * pi/2 split loses every bit there): x = M 2^E with M the 53-bit significand, and the bits of
// 2/pi from fraction bit E - 2 on (the earlier ones only add multiples of 4 to x 2/pi) times M give x 2/pi mod 4 as an exact 245-bit integer Q 2^-189;
// 192 bits of 2/pi leave an error below 2^-141, far under the closest any double comes to a multiple of pi/2 (about 2^-61). Integer operations only.
DD reduceHalfPiHuge(double ax, int &quadrant)
{
	std::uint64_t u;
	std::memcpy(&u, &ax, sizeof u);
	const std::uint64_t m = (u & 0xFFFFFFFFFFFFFull) | (1ull << 52);
	const int e = (int)((u >> 52) & 0x7FF) - 1075;
	const int i0 = e - 2;
	// the 192-bit window of 2/pi, little-endian 32-bit limbs
	std::uint32_t w[6] = { 0, 0, 0, 0, 0, 0 };
	for (int t = 0; t < 192; ++t)
	{
		const int i = i0 + t; // fraction bit index (1 = 2^-1)
		const std::uint32_t bit = (i >= 1 && i <= 1216) ? (kTwoOverPiBits[(i - 1) / 32] >> (31 - (i - 1) % 32)) & 1u : 0u;
		const int pos = 191 - t;
		w[pos / 32] |= bit << (pos % 32);
	}
	// Q = m * window, 8 limbs
	std::uint32_t q[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	const std::uint32_t mp[2] = { (std::uint32_t)m, (std::uint32_t)(m >> 32) };
	for (int a = 0; a < 2; ++a)
	{
		std::uint64_t carry = 0;
		for (int b = 0; b < 6; ++b)
		{
			const std::uint64_t cur = (std::uint64_t)q[a + b] + (std::uint64_t)mp[a] * w[b] + carry;
			q[a + b] = (std::uint32_t)cur;
			carry = cur >> 32;
		}
		for (int k = a + 6; k < 8 && carry; ++k)
		{
			const std::uint64_t cur = (std::uint64_t)q[k] + carry;
			q[k] = (std::uint32_t)cur;
			carry = cur >> 32;
		}
	}
	auto bitOf = [&q](int i) { return (q[i / 32] >> (i % 32)) & 1u; };
	quadrant = (int)(bitOf(189) | (bitOf(190) << 1));
	// the fraction F = Q mod 2^189, rounded to the nearest integer quadrant: f = F / 2^189 - (1 when F >= 2^188)
	std::uint32_t f[6]; // 189 bits
	for (int k = 0; k < 6; ++k)
	{
		f[k] = q[k];
	}
	f[5] &= (1u << 29) - 1u; // bits 160..188
	const bool upper = bitOf(188) != 0;
	if (upper)
	{
		quadrant = (quadrant + 1) & 3;
		// G = 2^189 - F
		std::uint64_t borrow = 0;
		for (int k = 0; k < 6; ++k)
		{
			const std::uint64_t sub = (std::uint64_t)f[k] + borrow;
			const std::uint64_t res = (k == 5 ? (1ull << 29) : 0ull) - sub;
			f[k] = (std::uint32_t)res;
			borrow = (res >> 63) ? 1u : 0u;
		}
	}
	int top = -1;
	for (int i = 188; i >= 0; --i)
	{
		if ((f[i / 32] >> (i % 32)) & 1u)
		{
			top = i;
			break;
		}
	}
	if (top < 0)
	{
		return DD{ 0.0, 0.0 };
	}
	auto bitsAt = [&f](int from, int count) // the `count` bits from bit `from` downwards (bits below 0 are 0)
	{
		std::uint64_t v = 0;
		for (int i = from; i > from - count; --i)
		{
			v = (v << 1) | ((i >= 0) ? ((f[i / 32] >> (i % 32)) & 1u) : 0u);
		}
		return v;
	};
	const double hi = (double)bitsAt(top, 53) * pow2(top - 52 - 189);
	const double lo = (double)bitsAt(top - 53, 53) * pow2(top - 105 - 189);
	DD r = ddMul(quickTwoSum(hi, lo), kPiOver2);
	return upper ? ddNeg(r) : r;
}

// x - k pi/2 with |result| <= pi/4 (a hair more at the rounding boundary of k), and k mod 4. For |k| < 2^20 (|x| < 1.6e6) the three-part split of pi/2
// carries about 150 bits and is exact to far below the final rounding; beyond, the Payne-Hanek reduction (reduceHalfPiHuge) covers every finite double.
DD reduceHalfPi(double x, int &quadrant)
{
	const double t = x * kTwoOverPi;
	if (t < 1048576.0 && t > -1048576.0)
	{
		const double k = (t + 0x1.8p+52) - 0x1.8p+52; // the nearest integer (ties to even): the magic constant leaves no fraction bits
		quadrant = (int)((long long)k & 3);
		const double r0 = x - k * kPio2Part1; // exact (Sterbenz: x and k * part1 are within a factor of 2, or k is 0)
		DD r = twoSum(r0, -(k * kPio2Part2));
		r = ddSub(r, DD{ k * kPio2Part3, 0.0 });
		return ddSub(r, DD{ k * kPio2Part3Tail, 0.0 });
	}
	const bool negative = signBitD(x);
	int q = 0;
	const DD r = reduceHalfPiHuge(negative ? -x : x, q);
	quadrant = negative ? (4 - q) & 3 : q;
	return negative ? ddNeg(r) : r;
}

// sum_k (-1)^k c[k] z^k by Horner: the terms from `split` on (each below 2^-45 of the sum here) in plain double, the leading ones in double-double.
// The double tail's own rounding then stays below 2^-95 of the result, far under the final rounding, at half the cost of an all double-double
// evaluation (lane WIN-1: about 2,300 calls per logic frame in a 1,000-object battle).
DD alternatingPoly(const DD *c, int n, int split, DD z)
{
	double t = (n - 1) % 2 ? -c[n - 1].hi : c[n - 1].hi;
	for (int k = n - 2; k >= split; --k)
	{
		t = t * z.hi + (k % 2 ? -c[k].hi : c[k].hi);
	}
	DD p = { t, 0.0 };
	for (int k = split - 1; k >= 0; --k)
	{
		p = ddAddDominated(k % 2 ? ddNeg(c[k]) : c[k], ddMul(p, z)); // |p z| <= c[k] z / ((k + 1)(k + 2))-ish: below c[k] / 8 for every series here
	}
	return p;
}

// sin(r) and cos(r) for |r| <= pi/4 (z = r^2 <= 0.62) by their Taylor series; the first omitted term is below 2^-104 relative. The double tail
// starts at z^7 / 15! (sin, below 2^-45) and z^8 / 16! (cos, below 2^-50).
DD sinPoly(DD r)
{
	const int n = (int)(sizeof(kSinCoef) / sizeof(kSinCoef[0]));
	return ddMul(alternatingPoly(kSinCoef, n, 7, ddMul(r, r)), r);
}

DD cosPoly(DD r)
{
	const int n = (int)(sizeof(kCosCoef) / sizeof(kCosCoef[0]));
	return alternatingPoly(kCosCoef, n, 8, ddMul(r, r));
}

// atan(t) for 0 <= t <= 1: atan(c) + atan((t - c) / (1 + t c)) with c = k / 64 the nearest table point, the rest by its Taylor series (|u| <= 1/128,
// z = u^2 <= 2^-14: the double tail from u^8 / 9 on is below 2^-59 relative)
DD atanUnit(DD t)
{
	const int k = (int)NumericState::floorD(t.hi * 64.0 + 0.5);
	const double c = (double)k * 0.015625;
	const DD u = ddDiv(ddSub(t, DD{ c, 0.0 }), ddAdd(DD{ 1.0, 0.0 }, ddMulD(t, c)));
	const int n = (int)(sizeof(kAtanCoef) / sizeof(kAtanCoef[0]));
	return ddAdd(kAtanTable[k], ddMul(alternatingPoly(kAtanCoef, n, 4, ddMul(u, u)), u));
}
} // namespace

double sinDD(double x)
{
	if (isNaND(x) || isInfD(x))
	{
		return canonicalNaN();
	}
	if (x == 0.0)
	{
		return x; // keeps the sign of zero
	}
	int q = 0;
	const DD r = reduceHalfPi(x, q);
	const DD v = (q & 1) ? cosPoly(r) : sinPoly(r);
	return (q & 2) ? -v.hi : v.hi;
}

double cosDD(double x)
{
	if (isNaND(x) || isInfD(x))
	{
		return canonicalNaN();
	}
	int q = 0;
	const DD r = reduceHalfPi(x, q);
	const DD v = (q & 1) ? sinPoly(r) : cosPoly(r);
	return (q == 1 || q == 2) ? -v.hi : v.hi;
}

double atan2DD(double y, double x)
{
	if (isNaND(x) || isNaND(y))
	{
		return canonicalNaN();
	}
	const bool neg = signBitD(y);
	const bool left = signBitD(x); // -0 counts as the left half plane (C99 Annex F)
	double a;
	if (isInfD(y) || isInfD(x))
	{
		if (isInfD(y) && isInfD(x))
		{
			a = left ? ddSub(kPi, ddMulD(kPiOver2, 0.5)).hi : ddMulD(kPiOver2, 0.5).hi; // 3pi/4 or pi/4
		}
		else if (isInfD(y))
		{
			a = kPiOver2.hi;
		}
		else
		{
			a = left ? kPi.hi : 0.0;
		}
	}
	else if (y == 0.0)
	{
		a = left ? kPi.hi : 0.0;
	}
	else if (x == 0.0)
	{
		a = kPiOver2.hi;
	}
	else
	{
		double ax = NumericState::absD(x), ay = NumericState::absD(y);
		// keep both in [2^-600, 2^600] where the Dekker split cannot overflow (powers of two: exact unless the smaller one underflows, which only
		// happens when the ratio is below 2^-1000 and the angle rounds to it anyway)
		while (ax > 0x1p+600 || ay > 0x1p+600)
		{
			ax *= 0x1p-600;
			ay *= 0x1p-600;
		}
		while (ax < 0x1p-600 && ay < 0x1p-600)
		{
			ax *= 0x1p+600;
			ay *= 0x1p+600;
		}
		const bool steep = ay > ax;
		DD t = { 0.0, 0.0 };
		if (steep ? ax != 0.0 : ay != 0.0)
		{
			t = steep ? ddDivD(ax, ay) : ddDivD(ay, ax);
		}
		DD v = atanUnit(t);
		if (steep)
		{
			v = ddSub(kPiOver2, v);
		}
		if (left)
		{
			v = ddSub(kPi, v);
		}
		a = v.hi;
	}
	return neg ? -a : a;
}

std::vector<std::string> numericStops()
{
	return {
		"S-232: Windows / MSVC bit identity of the simulation numerics is unverified: the facade's vector corpus (tests/test_numeric_facade.cpp, test_numeric_state.cpp) ran on GCC / Linux and (lane WIN-1) on Clang / llvm-mingw Windows x64 under Wine, not on MSVC; MSVC /fp:strict, the 32-bit SSE2 build (/arch:SSE2, -msse2 -mfpmath=sse), the MSVC branch of normalizeFloatingPointEnvironment and the x87 oracle comparison were not executed on a Windows host",
		"S-233: the pc24*W / sqrtPC24 / fildU32 functions are binary64-bounded carriers, not x87 registers (exponent range and subnormal grid of binary64: (2^600 * 2^600) / 2^600 is infinity here); they are exact only for the audited float-derived chains of the weapon lane. (Lane WIN-1: SimMath's atan2d / cosd / sind / cosf32 / sinf32 no longer call the platform libm, which is not bit-identical across platforms, but the deterministic sinDD / cosDD / atan2DD; their retail parity stays S-081)",
	};
}

} // namespace NumericState
