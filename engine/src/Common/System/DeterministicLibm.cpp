// OpenBFME. GPL-3.0. Lane DEP-2: see Common/DeterministicLibm.h for what this computes and why.
//
// A C++ translation of glibc 2.39's double-precision libm (sysdeps/ieee754/dbl-64: s_sin.c, branred.c, s_tan.c, s_atan.c, e_atan2.c, e_asin.c,
// e_exp.c, e_log.c, e_pow.c, e_log10.c, math_err.c and their tables, DeterministicLibmTables.inc) as glibc's x86-64 build runs it on a CPU with
// FMA: s_sin, s_tan, s_atan, e_atan2, e_asin, e_exp, e_log and e_pow are compiled a second time with -mfma -mavx2 (sysdeps/x86_64/fpu/multiarch/
// Makefile) and selected at load time, so GCC contracts a multiplication into an FMA wherever its result is only added or subtracted in the same
// basic block, and e_log / e_pow take their `__FP_FAST_FMA` paths. branred.c and e_log10.c are not in that list (no contraction). Each fused
// operation is written out below (fmadd = a*b+c, fnma = c-a*b, fms = a*b-c, one rounding: fmaSoft, integer arithmetic), verified against GCC's widening_mul dump of
// the -mfma build and against the host libm (tests/test_deterministic_libm.cpp). This file is compiled under the simulation floating-point
// contract (no further contraction).
//
// IBM Accurate Mathematical Library, written by International Business Machines Corp.; Copyright (C) 2001-2024 Free Software Foundation, Inc.
// e_exp.c, e_log.c, e_pow.c, math_err.c: Copyright (C) 2018-2024 Free Software Foundation, Inc. (contributed by Arm Ltd.).
// These are free software: you can redistribute them and/or modify them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version. This translation is used under the
// GNU GPL version 3 as section 3 of the LGPL 2.1 allows (NOTICE). It is distributed WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// e_log10.c: Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved. Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely granted, provided that this notice is preserved.

#include "Common/DeterministicLibm.h"

#include <cmath> // std::sqrt only: the correctly rounded square-root instruction (sqrtsd; fsqrt on AArch64), compiled without errno
#include <cstdint>
#include <cstring>

namespace
{

#include "DeterministicLibmTables.inc"

inline std::uint64_t asU64(double x)
{
	std::uint64_t u;
	std::memcpy(&u, &x, sizeof u);
	return u;
}

inline double asDouble(std::uint64_t u)
{
	double x;
	std::memcpy(&x, &u, sizeof x);
	return x;
}

// glibc's mynumber: i[HIGH_HALF] / i[LOW_HALF] as signed 32-bit words (little endian)
inline std::int32_t highWord(double x)
{
	return (std::int32_t)(std::uint32_t)(asU64(x) >> 32);
}

inline std::int32_t lowWord(double x)
{
	return (std::int32_t)(std::uint32_t)asU64(x);
}

inline double withHighWord(double x, std::int32_t high)
{
	return asDouble((asU64(x) & 0xFFFFFFFFull) | ((std::uint64_t)(std::uint32_t)high << 32));
}

inline double T(const std::uint64_t *table, int i)
{
	return asDouble(table[i]);
}

// ---- fma: a * b + c with one rounding, in integer arithmetic (lane DEP-2 review: no libm, no FMA instruction, the same bits on every CPU) ----
// The exact product of the two 53-bit significands (106 bits) and the addend are aligned in a 128-bit integer with a sticky bit, added, and
// rounded once to nearest even, subnormal results and overflow included. Special operands are decided explicitly (SSE2's rules): a NaN operand
// comes back quietened (the first of a, b, c), inf * 0 and inf - inf give the x86 default NaN. tests/test_deterministic_libm.cpp compares it
// with the hardware FMA instruction over a sweep.

struct U128
{
	std::uint64_t hi, lo;
};

inline U128 mul64(std::uint64_t a, std::uint64_t b)
{
	const std::uint64_t a0 = a & 0xFFFFFFFFull, a1 = a >> 32, b0 = b & 0xFFFFFFFFull, b1 = b >> 32;
	const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
	const std::uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
	return U128{ p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (mid << 32) | (p00 & 0xFFFFFFFFull) };
}

inline int topBit(U128 v) // the index of the highest set bit; -1 for zero
{
	int n = 127;
	std::uint64_t w = v.hi;
	if (w == 0)
	{
		w = v.lo;
		n = 63;
		if (w == 0)
		{
			return -1;
		}
	}
	while (!(w >> 63))
	{
		w <<= 1;
		--n;
	}
	return n;
}

inline U128 shl(U128 v, int n) // 0 <= n < 128
{
	if (n == 0)
	{
		return v;
	}
	if (n >= 64)
	{
		return U128{ v.lo << (n - 64), 0 };
	}
	return U128{ (v.hi << n) | (v.lo >> (64 - n)), v.lo << n };
}

inline U128 shrSticky(U128 v, int n) // shift right by n >= 0; any bit shifted out is ORed into bit 0
{
	if (n == 0)
	{
		return v;
	}
	if (n >= 128)
	{
		return U128{ 0, (v.hi | v.lo) ? 1ull : 0ull };
	}
	U128 r;
	std::uint64_t lost;
	if (n >= 64)
	{
		lost = (n == 64 ? 0 : (v.hi << (128 - n))) | v.lo;
		r = U128{ 0, n == 64 ? v.hi : v.hi >> (n - 64) };
	}
	else
	{
		lost = v.lo << (64 - n);
		r = U128{ v.hi >> n, (v.lo >> n) | (v.hi << (64 - n)) };
	}
	if (lost)
	{
		r.lo |= 1;
	}
	return r;
}

inline U128 add128(U128 a, U128 b)
{
	const std::uint64_t lo = a.lo + b.lo;
	return U128{ a.hi + b.hi + (lo < a.lo ? 1 : 0), lo };
}

inline U128 sub128(U128 a, U128 b) // a >= b
{
	return U128{ a.hi - b.hi - (a.lo < b.lo ? 1 : 0), a.lo - b.lo };
}

inline bool less128(U128 a, U128 b)
{
	return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
}

// a finite non-zero double as significand * 2^exponent with the significand normalised to 53 bits (subnormals included)
inline void unpack(std::uint64_t bits, std::uint64_t &m, int &e)
{
	const int be = (int)((bits >> 52) & 0x7FF);
	m = bits & 0x000FFFFFFFFFFFFFull;
	if (be == 0)
	{
		e = -1074;
		while (!(m >> 52))
		{
			m <<= 1;
			--e;
		}
	}
	else
	{
		m |= 1ull << 52;
		e = be - 1075;
	}
}

double fmaSoft(double a, double b, double c)
{
	const std::uint64_t ba = asU64(a), bb = asU64(b), bc = asU64(c);
	const std::uint64_t absMask = 0x7FFFFFFFFFFFFFFFull, infBits = 0x7FF0000000000000ull;
	const std::uint64_t aa = ba & absMask, ab = bb & absMask, ac = bc & absMask;
	if (aa > infBits || ab > infBits || ac > infBits)
	{
		const std::uint64_t n = aa > infBits ? ba : (ab > infBits ? bb : bc);
		return asDouble(n | 0x0008000000000000ull);
	}
	const std::uint64_t productSign = (ba ^ bb) & 0x8000000000000000ull;
	if (aa == infBits || ab == infBits)
	{
		if (aa == 0 || ab == 0)
		{
			return asDouble(0xFFF8000000000000ull); // inf * 0
		}
		if (ac == infBits && (bc & 0x8000000000000000ull) != productSign)
		{
			return asDouble(0xFFF8000000000000ull); // inf - inf
		}
		return asDouble(productSign | infBits);
	}
	if (ac == infBits)
	{
		return c;
	}
	if (aa == 0 || ab == 0)
	{
		// the product is an exact signed zero: the sum is one IEEE addition (round to nearest: +0 unless both are -0)
		if (ac == 0)
		{
			return asDouble((productSign & bc) ? 0x8000000000000000ull : 0);
		}
		return c;
	}

	std::uint64_t ma, mb, mc = 0;
	int ea, eb, ec = 0;
	unpack(aa, ma, ea);
	unpack(ab, mb, eb);
	// the product: P * 2^ep with P < 2^106, moved up so its top bit is bit 125
	U128 P = mul64(ma, mb);
	int ep = ea + eb;
	const int pt = topBit(P);
	P = shl(P, 125 - pt);
	ep -= 125 - pt;
	U128 R;
	int e;
	std::uint64_t sign = productSign;
	if (ac == 0)
	{
		R = P;
		e = ep;
	}
	else
	{
		unpack(ac, mc, ec);
		U128 C = shl(U128{ 0, mc }, 125 - 52);
		ec -= 125 - 52;
		const std::uint64_t cSign = bc & 0x8000000000000000ull;
		// align to the larger exponent (the smaller operand's lost bits become the sticky bit)
		if (ep >= ec)
		{
			C = shrSticky(C, ep - ec);
			e = ep;
		}
		else
		{
			P = shrSticky(P, ec - ep);
			e = ec;
		}
		if (cSign == productSign)
		{
			R = add128(P, C);
		}
		else if (less128(P, C))
		{
			R = sub128(C, P);
			sign = cSign;
		}
		else
		{
			R = sub128(P, C);
		}
		if (R.hi == 0 && R.lo == 0)
		{
			return 0.0; // exact cancellation: +0 in round to nearest
		}
	}
	// round R * 2^e to 53 bits (the result's last bit has exponent >= -1074)
	const int t = topBit(R);
	int shift = t - 52;
	if (e + shift < -1074)
	{
		shift = -1074 - e;
	}
	std::uint64_t q;
	if (shift <= 0)
	{
		q = shl(R, -shift).lo; // exact
	}
	else
	{
		// q = floor(R / 2^shift), rem = R mod 2^shift
		if (shift > 128)
		{
			return asDouble(sign); // R < 2^128 <= half an ulp: rounds to a signed zero
		}
		U128 qq;
		U128 rem;
		if (shift == 128)
		{
			qq = U128{ 0, 0 };
			rem = R;
		}
		else if (shift >= 64)
		{
			qq = U128{ 0, shift == 64 ? R.hi : R.hi >> (shift - 64) };
			rem = U128{ shift == 64 ? 0 : (R.hi & ((1ull << (shift - 64)) - 1)), R.lo };
		}
		else
		{
			qq = U128{ R.hi >> shift, (R.lo >> shift) | (R.hi << (64 - shift)) };
			rem = U128{ 0, R.lo & ((1ull << shift) - 1) };
		}
		const U128 half = shift - 1 >= 64 ? U128{ 1ull << (shift - 1 - 64), 0 } : U128{ 0, 1ull << (shift - 1) };
		q = qq.lo;
		if (less128(half, rem) || (!less128(rem, half) && (q & 1)))
		{
			++q;
		}
		e += shift;
		if (q == (1ull << 53))
		{
			q >>= 1;
			++e;
		}
	}
	if (shift <= 0)
	{
		e += shift;
	}
	// compose: q * 2^e, q < 2^53; q < 2^52 only for a subnormal (e == -1074)
	if (q < (1ull << 52))
	{
		return asDouble(sign | q);
	}
	const int biased = e + 52 + 1023;
	if (biased >= 2047)
	{
		return asDouble(sign | infBits);
	}
	return asDouble(sign | ((std::uint64_t)biased << 52) | (q & 0x000FFFFFFFFFFFFFull));
}

// the fused operations of the -mfma build, one rounding each
inline double fmadd(double a, double b, double c)
{
	return fmaSoft(a, b, c);
}

inline double fnma(double a, double b, double c) // c - a*b
{
	return fmaSoft(-a, b, c);
}

inline double fms(double a, double b, double c) // a*b - c
{
	return fmaSoft(a, b, -c);
}

inline double fabsD(double x)
{
	return asDouble(asU64(x) & 0x7FFFFFFFFFFFFFFFull);
}

inline double copySign(double x, double y)
{
	return asDouble((asU64(x) & 0x7FFFFFFFFFFFFFFFull) | (asU64(y) & 0x8000000000000000ull));
}

inline bool isNaN(double x)
{
	return (asU64(x) & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull;
}

// SSE2's NaN rules, made explicit so no compiler operand order or CPU default NaN can change them: an operation with a NaN operand returns the
// first NaN operand quietened; an invalid operation (inf - inf, 0 / 0, inf / inf) returns the x86 default NaN.
const std::uint64_t kDefaultNaN = 0xFFF8000000000000ull;
// glibc's NAN constant, the result of the SVID compatibility wrappers (w_asin_compat.c, w_acos_compat.c, w_log10_compat.c: k_standard.c)
const std::uint64_t kPositiveNaN = 0x7FF8000000000000ull;

inline double quiet(double x)
{
	return asDouble(asU64(x) | 0x0008000000000000ull);
}

inline double nan2(double a, double b) // a + b with a or b NaN
{
	return isNaN(a) ? quiet(a) : quiet(b);
}

inline double invalid(double x) // (x - x) / (x - x), x / x for x infinite or NaN
{
	return isNaN(x) ? quiet(x) : asDouble(kDefaultNaN);
}

// math_err.c (the results; errno is not set)
double mathOflow(std::uint32_t sign)
{
	const double y = 0x1p769;
	return (sign ? -y : y) * y;
}

double mathUflow(std::uint32_t sign)
{
	const double y = 0x1p-767;
	return (sign ? -y : y) * y;
}

double mathDivzero(std::uint32_t sign)
{
	const double y = sign ? -1.0 : 1.0;
	return y / 0.0;
}

// ---- s_sin.c, usncs.h, branred.c, branred.h ----------------------------------------------------------------------------------------------

const double s1 = -0x1.5555555555555p-3;
const double s2 = 0x1.1111111110ECEp-7;
const double s3 = -0x1.A01A019DB08B8p-13;
const double s4 = 0x1.71DE27B9A7ED9p-19;
const double s5 = -0x1.ADDFFC2FCDF59p-26;
const double sinBig = 0x1.8000000000000p45;
const double hp0 = 0x1.921FB54442D18p0;
const double hp1 = 0x1.1A62633145C07p-54;
const double mp1 = 0x1.921FB58000000p0;
const double mp2 = -0x1.DDE973C000000p-27;
const double pp3 = -0x1.CB3B398000000p-55;
const double pp4 = -0x1.d747f23e32ed7p-83;
const double hpinv = 0x1.45F306DC9C883p-1;
const double toint = 0x1.8000000000000p52;
const double sn3 = -1.66666666666664880952546298448555E-01;
const double sn5 = 8.33333214285722277379541354343671E-03;
const double cs2 = 4.99999999999999999999950396842453E-01;
const double cs4 = -4.16666666666664434524222570944589E-02;
const double cs6 = 1.38888874007937613028114285595617E-03;

// branred.c (compiled without FMA in glibc: plain operations)
int branred(double x, double *a, double *aa)
{
	static const double toverp[75] = {
		10680707.0, 7228996.0, 1387004.0, 2578385.0, 16069853.0, 12639074.0, 9804092.0, 4427841.0, 16666979.0, 11263675.0,
		12935607.0, 2387514.0, 4345298.0, 14681673.0, 3074569.0, 13734428.0, 16653803.0, 1880361.0, 10960616.0, 8533493.0,
		3062596.0, 8710556.0, 7349940.0, 6258241.0, 3772886.0, 3769171.0, 3798172.0, 8675211.0, 12450088.0, 3874808.0,
		9961438.0, 366607.0, 15675153.0, 9132554.0, 7151469.0, 3571407.0, 2607881.0, 12013382.0, 4155038.0, 6285869.0,
		7677882.0, 13102053.0, 15825725.0, 473591.0, 9065106.0, 15363067.0, 6271263.0, 9264392.0, 5636912.0, 4652155.0,
		7056368.0, 13614112.0, 10155062.0, 1944035.0, 9527646.0, 15080200.0, 6658437.0, 6231200.0, 6832269.0, 16767104.0,
		5075751.0, 3212806.0, 1398474.0, 7579849.0, 6349435.0, 12618859.0, 4703257.0, 12806093.0, 14477321.0, 2786137.0,
		12875403.0, 9837734.0, 14528324.0, 13719321.0, 343717.0};
	const double split = 134217729.0; // CN, 2^27 + 1
	const double t576 = 0x1p576;
	const double tm600 = 0x1p-600;
	const double tm24 = 0x1p-24;
	const double big = 6755399441055744.0;
	const double big1 = 27021597764222976.0;
	const double bmp2 = -0x1.DDE9740000000p-27; // branred.h's mp2 (differs from usncs.h's)

	double r[6], s, t, sum, b, bb, sum1, sum2, b1, bb1, b2, bb2, x1, x2, t1, t2;
	x *= tm600;
	t = x * split;
	x1 = t - (t - x);
	x2 = x - x1;

	double halves[2] = {x1, x2};
	double bs[2], bbs[2], sums[2];
	for (int half = 0; half < 2; half++)
	{
		const double xh = halves[half];
		sum = 0;
		int k = (highWord(xh) >> 20) & 2047;
		k = (k - 450) / 24;
		if (k < 0)
		{
			k = 0;
		}
		double gor = withHighWord(t576, highWord(t576) - ((k * 24) << 20));
		for (int i = 0; i < 6; i++)
		{
			r[i] = xh * toverp[k + i] * gor;
			gor *= tm24;
		}
		for (int i = 0; i < 3; i++)
		{
			s = (r[i] + big) - big;
			sum += s;
			r[i] -= s;
		}
		t = 0;
		for (int i = 0; i < 6; i++)
		{
			t += r[5 - i];
		}
		bb = (((((r[0] - t) + r[1]) + r[2]) + r[3]) + r[4]) + r[5];
		s = (t + big) - big;
		sum += s;
		t -= s;
		b = t + bb;
		bb = (t - b) + bb;
		s = (sum + big1) - big1;
		sum -= s;
		bs[half] = b;
		bbs[half] = bb;
		sums[half] = sum;
	}
	b1 = bs[0];
	bb1 = bbs[0];
	sum1 = sums[0];
	b2 = bs[1];
	bb2 = bbs[1];
	sum2 = sums[1];

	sum = sum1 + sum2;
	b = b1 + b2;
	bb = (fabsD(b1) > fabsD(b2)) ? (b1 - b) + b2 : (b2 - b) + b1;
	if (b > 0.5)
	{
		b -= 1.0;
		sum += 1.0;
	}
	else if (b < -0.5)
	{
		b += 1.0;
		sum -= 1.0;
	}
	s = b + (bb + bb1 + bb2);
	t = ((b - s) + bb) + (bb1 + bb2);
	b = s * split;
	t1 = b - (b - s);
	t2 = s - t1;
	b = s * hp0;
	bb = (((t1 * mp1 - b) + t1 * bmp2) + t2 * mp1) + (t2 * bmp2 + s * hp1 + t * hp0);
	s = b + bb;
	t = (b - s) + bb;
	*a = s;
	*aa = t;
	return ((int)sum) & 3;
}

// TAYLOR_SIN (xx, x, dx): t = ((POLYNOMIAL (xx) * x - 0.5 * dx) * xx + dx); x + t
inline double taylorSin(double xx, double x, double dx)
{
	double p = fmadd(xx, s5, s4);
	p = fmadd(xx, p, s3);
	p = fmadd(xx, p, s2);
	p = fmadd(xx, p, s1);
	const double half = dx * 0.5;
	const double t = fmadd(xx, fms(p, x, half), dx);
	return x + t;
}

// the table entry of |x| (x = |x| rounded to 1/128 through `big + |x|`): sn, ssn, cs, ccs
inline int sinCosIndex(double u)
{
	return lowWord(u) << 2;
}

double doCos(double x, double dx)
{
	if (x < 0)
	{
		dx = -dx;
	}
	const double u = sinBig + fabsD(x);
	x = fabsD(x) - (u - sinBig) + dx;
	const double xx = x * x;
	const double s = fmadd(x * xx, fmadd(xx, sn5, sn3), x);
	const double c = xx * fmadd(xx, fmadd(xx, cs6, cs4), cs2);
	const int k = sinCosIndex(u);
	const double sn = T(kSinCosTab, k), ssn = T(kSinCosTab, k + 1), cs = T(kSinCosTab, k + 2), ccs = T(kSinCosTab, k + 3);
	// cor = (ccs - s * ssn - cs * c) - sn * s
	const double cor = fnma(s, sn, fnma(c, cs, fnma(s, ssn, ccs)));
	return cs + cor;
}

double doSin(double x, double dx)
{
	const double xold = x;
	if (fabsD(x) < 0.126)
	{
		return taylorSin(x * x, x, dx);
	}
	if (x <= 0)
	{
		dx = -dx;
	}
	const double u = sinBig + fabsD(x);
	x = fabsD(x) - (u - sinBig);
	const double xx = x * x;
	// s = x + (dx + x * xx * (sn3 + xx * sn5)); c = x * dx + xx * (cs2 + xx * (cs4 + xx * cs6))
	const double s = x + fmadd(x * xx, fmadd(xx, sn5, sn3), dx);
	const double c = fmadd(x, dx, xx * fmadd(xx, fmadd(xx, cs6, cs4), cs2));
	const int k = sinCosIndex(u);
	const double sn = T(kSinCosTab, k), ssn = T(kSinCosTab, k + 1), cs = T(kSinCosTab, k + 2), ccs = T(kSinCosTab, k + 3);
	// cor = (ssn + s * ccs - sn * c) + cs * s
	const double cor = fmadd(s, cs, fnma(c, sn, fmadd(s, ccs, ssn)));
	return copySign(sn + cor, xold);
}

int reduceSinCos(double x, double *a, double *da)
{
	const double t = fmadd(x, hpinv, toint);
	const double xn = t - toint;
	const double y = fnma(xn, mp2, fnma(xn, mp1, x));
	const int n = lowWord(t) & 3;
	// t1 = xn * pp3 (fused into both uses), t2 = y - t1, db = (y - t2) - t1
	const double t2 = fnma(xn, pp3, y);
	double db = fnma(xn, pp3, y - t2);
	// t1 = xn * pp4, b = t2 - t1, db += (t2 - b) - t1
	const double b = fnma(xn, pp4, t2);
	db = db + fnma(xn, pp4, t2 - b);
	*a = b;
	*da = db;
	return n;
}

double doSinCos(double a, double da, int n)
{
	const double retval = (n & 1) ? doCos(a, da) : doSin(a, da);
	return (n & 2) ? -retval : retval;
}

// ---- s_tan.c, utan.h, utan.tbl -----------------------------------------------------------------------------------------------------------

const double td3 = asDouble(0x3FD5555555555555ull);
const double td5 = asDouble(0x3FC11111111107C6ull);
const double td7 = asDouble(0x3FABA1BA1CDB8745ull);
const double td9 = asDouble(0x3F9664ED49CFC666ull);
const double td11 = asDouble(0x3F82385A3CF2E4EAull);
const double te0 = asDouble(0x3FD5555555554DBDull);
const double te1 = asDouble(0x3FC11112E0A6B45Full);
const double tmfftnhf = asDouble(0xc02f000000000000ull);
const double tg1 = asDouble(0x3e4b096c00000000ull);
const double tg2 = asDouble(0x3faf212d00000000ull);
const double tg3 = asDouble(0x3fe92f1a00000000ull);
const double tg4 = asDouble(0x4039000000000000ull);
const double tg5 = asDouble(0x4197d78400000000ull);
const double tgy2 = asDouble(0x3faf212d00000000ull);
const double tmp1 = asDouble(0x3FF921FB58000000ull);
const double tmp2 = asDouble(0xBE4DDE973C000000ull);
const double tmp3 = asDouble(0xBC8CB3B399D747F2ull);
const double tpp3 = asDouble(0xBC8CB3B398000000ull);
const double tpp4 = asDouble(0xbacd747f23e32ed7ull);
const double thpinv = asDouble(0x3FE45F306DC9C883ull);
const double ttoint = asDouble(0x4338000000000000ull);

inline double xfg(int i, int j)
{
	return T(kTanXfg, i * 4 + j);
}

// EADD (x, y, z, zz)
inline void eadd(double x, double y, double &z, double &zz)
{
	z = x + y;
	zz = (fabsD(x) > fabsD(y)) ? ((x - z) + y) : ((y - z) + x);
}

// ESUB (x, y, z, zz)
inline void esub(double x, double y, double &z, double &zz)
{
	z = x - y;
	zz = (fabsD(x) > fabsD(y)) ? ((x - z) - y) : (x - (y + z));
}

// the odd polynomial of |y| <= 0.0608: da + a * a2 * (d3 + a2 * (... d11))
inline double tanPoly(double a, double da)
{
	const double a2 = a * a;
	double t2 = fmadd(a2, td11, td9);
	t2 = fmadd(a2, t2, td7);
	t2 = fmadd(a2, t2, td5);
	t2 = fmadd(a2, t2, td3);
	return fmadd(a * a2, t2, da);
}

// -cot of a + t2 (EADD, then DIV2 (1.0, 0.0, b, db, c, dc, ...) with MUL12 = EMULV through fma)
inline double minusCot(double a, double t2)
{
	double b, db;
	eadd(a, t2, b, db);
	const double c = 1.0 / b;
	const double u = c * b;
	const double uu = fms(c, b, u);
	const double cc = fnma(c, db, ((1.0 - u) - uu) + 0.0) / b;
	const double z = c + cc;
	const double zz = (c - z) + cc;
	const double y = z + zz;
	return -y;
}

// the table cases (0.0608 < |y| <= 0.787) after a reduction: ya + yya, sign sy, n odd: -cot
inline double tanTable(double ya, double yya, double sy, int n)
{
	const int i = (int)(tmfftnhf + 256 * ya);
	const double z = (ya - xfg(i, 0)) + yya;
	const double z2 = z * z;
	const double pz = fmadd(z * z2, fmadd(z2, te1, te0), z);
	const double fi = xfg(i, 1);
	const double gi = xfg(i, 2);
	if (n)
	{
		const double t2 = pz * (fi + gi) / (fi + pz);
		const double y = gi - t2;
		return -sy * y;
	}
	const double t2 = pz * (gi + fi) / (gi - pz);
	const double y = fi + t2;
	return sy * y;
}

// the reduced cases: a + da, n odd: -cot
inline double tanReduced(double a, double da, int n)
{
	double ya, yya, sy;
	if (a < 0.0)
	{
		ya = -a;
		yya = -da;
		sy = -1;
	}
	else
	{
		ya = a;
		yya = da;
		sy = 1;
	}
	if (ya <= tgy2)
	{
		const double t2 = tanPoly(a, da);
		if (n)
		{
			return minusCot(a, t2);
		}
		return a + t2;
	}
	return tanTable(ya, yya, sy, n);
}

// ---- s_atan.c, e_atan2.c, atnat.h, atnat2.h, uatan.tbl --------------------------------------------------------------------------------

const double ad3 = asDouble(0xbfd5555555555555ull);
const double ad5 = asDouble(0x3fc99999999997fdull);
const double ad7 = asDouble(0xbfc24924923f7603ull);
const double ad9 = asDouble(0x3fbc71c6e5129a3bull);
const double ad11 = asDouble(0xbfb7458022b13c25ull);
const double ad13 = asDouble(0x3fb375f08b31cbceull);
const double aA = asDouble(0x3e4bb67a00000000ull);
const double aB = asDouble(0x3fb0000000000000ull);
const double aC = asDouble(0x3ff0000000000000ull);
const double aD = asDouble(0x4030000000000000ull);
const double aE = asDouble(0x43349ff200000000ull);
const double aHPI = asDouble(0x3ff921fb54442d18ull);
const double aMHPI = asDouble(0xbff921fb54442d18ull);
const double aHPI1 = asDouble(0x3c91a62633145c07ull);
const double aInv16 = asDouble(0x3fb0000000000000ull);
const double aOPI = asDouble(0x400921fb54442d18ull);
const double aOPI1 = asDouble(0x3ca1a62633145c07ull);
const double aMOPI = asDouble(0xc00921fb54442d18ull);
const double aQPI = asDouble(0x3fe921fb54442d18ull);
const double aMQPI = asDouble(0xbfe921fb54442d18ull);
const double aTQPI = asDouble(0x4002d97c7f3321d2ull);
const double aMTQPI = asDouble(0xc002d97c7f3321d2ull);
const double aTwo500 = asDouble(0x5f30000000000000ull);
const double aTwom500 = asDouble(0x20b0000000000000ull);
const double TWO52 = 0x1.0p52;

inline double cij(int i, int j)
{
	return T(kAtanCij, i * 7 + j);
}

// i = (TWO52 + 256 * u) - TWO52 (256 * u is exact: the fused and the plain form agree)
inline int atanIndex(double u)
{
	return (int)(fmadd(256, u, TWO52) - TWO52) - 16;
}

// d3 + v * (d5 + v * (d7 + v * (d9 + v * (d11 + v * d13))))
inline double atanPoly(double v)
{
	double p = fmadd(v, ad13, ad11);
	p = fmadd(v, p, ad9);
	p = fmadd(v, p, ad7);
	p = fmadd(v, p, ad5);
	return fmadd(v, p, ad3);
}

// cij[i][2] + v * (cij[i][3] + v * (cij[i][4] + v * (cij[i][5] + v * cij[i][6])))
inline double cijPoly2(int i, double v)
{
	double p = fmadd(v, cij(i, 6), cij(i, 5));
	p = fmadd(v, p, cij(i, 4));
	p = fmadd(v, p, cij(i, 3));
	return fmadd(v, p, cij(i, 2));
}

// ---- e_asin.c, uasncs.h, asincos.tbl, root.tbl, powtwo.tbl ----------------------------------------------------------------------------

const double f1 = 1.66666666666664110590506577996662E-01;
const double f2 = 7.50000000026122686814431784722623E-02;
const double f3 = 4.46428561421059750978517350006940E-02;
const double f4 = 3.03821268582119319911193410625235E-02;
const double f5 = 2.23551211026525610742786300334557E-02;
const double f6 = 1.81382903404565056280372531963613E-02;
const double t24 = 16777216.0;
const double t27 = 134217728.0;
const double rt0 = 9.99999999859990725855365213134618E-01;
const double rt1 = 4.99999999495955425917856814202739E-01;
const double rt2 = 3.75017500867345182581453026130850E-01;
const double rt3 = 3.12523626554518656309172508769531E-01;

inline double asncs(int i)
{
	return T(kAsinCos, i);
}

// ((((f6 * z + f5) * z + f4) * z + f3) * z + f2) * z + f1
inline double asinF(double z)
{
	double p = fmadd(f6, z, f5);
	p = fmadd(p, z, f4);
	p = fmadd(p, z, f3);
	p = fmadd(p, z, f2);
	return fmadd(p, z, f1);
}

// the table segments of 0.125 <= |x| < 0.96875: t = asncs[n+1] * xx + (xx * xx * P(xx) + asncs[n+deg+2]); returns t, sets the table index of
// the base value (asncs[n+deg+3])
inline double asinSegment(double ax, int n, int degree, int &baseIndex)
{
	const double xx = ax - asncs(n);
	// P = asncs[n+2] + xx * (asncs[n+3] + ... + xx * asncs[n+2+degree])
	double p = asncs(n + 2 + degree);
	for (int j = n + 1 + degree; j >= n + 2; j--)
	{
		p = fmadd(xx, p, asncs(j));
	}
	p = fmadd(xx * xx, p, asncs(n + 3 + degree));
	baseIndex = n + 4 + degree;
	return fmadd(asncs(n + 1), xx, p);
}

// the table segment of |x| (k = |x|'s high word), or -1 outside 0.125 <= |x| < 0.96875
inline int asinSegmentOf(std::int32_t k, int &degree)
{
	if (k < 0x3fc00000 || k >= 0x3fef0000)
	{
		return -1;
	}
	if (k < 0x3fe00000)
	{
		degree = 4;
		return k < 0x3fd00000 ? 11 * ((k & 0x000fffff) >> 15) : 11 * ((k & 0x000fffff) >> 14) + 352;
	}
	if (k < 0x3fe80000)
	{
		degree = 5;
		return 1056 + ((k & 0x000fe000) >> 11) * 3;
	}
	if (k < 0x3fed8000)
	{
		degree = 6;
		return 992 + ((k & 0x000fe000) >> 13) * 13;
	}
	if (k < 0x3fee8000)
	{
		degree = 7;
		return 884 + ((k & 0x000fe000) >> 13) * 14;
	}
	degree = 8;
	return 768 + ((k & 0x000fe000) >> 13) * 15;
}

// ---- e_exp.c, e_pow.c, math_config.h, e_exp_data.c ---------------------------------------------------------------------------------------

const int EXP_TABLE_BITS = 7;
const int EXP_N = 1 << EXP_TABLE_BITS;

inline std::uint32_t top12(double x)
{
	return (std::uint32_t)(asU64(x) >> 52);
}

// scale * (1 + tmp) without intermediate overflow / underflow (e_exp.c specialcase; pow's with the sign in sbits)
double expSpecialCase(double tmp, std::uint64_t sbits, std::uint64_t ki, bool signedScale)
{
	if ((ki & 0x80000000) == 0)
	{
		sbits -= 1009ull << 52;
		const double scale = asDouble(sbits);
		return 0x1p1009 * fmadd(scale, tmp, scale);
	}
	sbits += 1022ull << 52;
	const double scale = asDouble(sbits);
	const double st = scale * tmp; // used in two blocks: not fused
	double y = scale + st;
	if (signedScale ? (fabsD(y) < 1.0) : (y < 1.0))
	{
		const double one = (signedScale && y < 0.0) ? -1.0 : 1.0;
		double lo = scale - y + st;
		const double hi = one + y;
		lo = one - hi + y + lo;
		y = (hi + lo) - one;
		if (y == 0.0)
		{
			y = signedScale ? asDouble(sbits & 0x8000000000000000ull) : 0.0;
		}
	}
	return 0x1p-1022 * y;
}

// exp (x + xtail) with the sign bias of pow, or exp (x) (e_exp.c __exp, e_pow.c exp_inline)
double expCore(double x, double xtail, std::uint32_t signBias, bool fromPow)
{
	const double InvLn2N = T(kExpData, 0);
	const double Shift = T(kExpData, 1);
	const double NegLn2hiN = T(kExpData, 2);
	const double NegLn2loN = T(kExpData, 3);
	const double C2 = T(kExpPoly, 0), C3 = T(kExpPoly, 1), C4 = T(kExpPoly, 2), C5 = T(kExpPoly, 3);

	std::uint32_t abstop = top12(x) & 0x7ff;
	if (abstop - top12(0x1p-54) >= top12(512.0) - top12(0x1p-54))
	{
		if (abstop - top12(0x1p-54) >= 0x80000000)
		{
			const double one = 1.0 + x;
			return signBias ? -one : one;
		}
		if (abstop >= top12(1024.0))
		{
			if (!fromPow)
			{
				if (asU64(x) == asU64(-INFINITY))
				{
					return 0.0;
				}
				if (abstop >= top12(INFINITY))
				{
					return isNaN(x) ? quiet(x) : 1.0 + x;
				}
			}
			return (asU64(x) >> 63) ? mathUflow(signBias) : mathOflow(signBias);
		}
		abstop = 0;
	}
	// z = InvLn2N * x; kd = z + Shift (fused); ki = asuint64 (kd); kd -= Shift
	double kd = fmadd(InvLn2N, x, Shift);
	const std::uint64_t ki = asU64(kd);
	kd -= Shift;
	double r = fmadd(kd, NegLn2loN, fmadd(kd, NegLn2hiN, x));
	if (fromPow)
	{
		r += xtail; // exp_inline only (e_pow.c)
	}
	const std::uint64_t idx = 2 * (ki % EXP_N);
	const std::uint64_t top = (ki + signBias) << (52 - EXP_TABLE_BITS);
	const double tail = asDouble(kExpTab[idx]);
	const std::uint64_t sbits = kExpTab[idx + 1] + top;
	const double r2 = r * r;
	// tmp = tail + r + r2 * (C2 + r * C3) + r2 * r2 * (C4 + r * C5)
	const double tmp = fmadd(r2 * r2, fmadd(r, C5, C4), fmadd(r2, fmadd(r, C3, C2), tail + r));
	if (abstop == 0)
	{
		return expSpecialCase(tmp, sbits, ki, fromPow);
	}
	const double scale = asDouble(sbits);
	return fmadd(scale, tmp, scale);
}

// ---- e_log.c, e_log_data.c ----------------------------------------------------------------------------------------------------------------

inline std::uint32_t top16(double x)
{
	return (std::uint32_t)(asU64(x) >> 48);
}

// ---- e_pow.c, e_pow_log_data.c --------------------------------------------------------------------------------------------------------------

// log (x) as y + tail (e_pow.c log_inline, the __FP_FAST_FMA path)
double powLogInline(std::uint64_t ix, double *tail)
{
	const int N = 128;
	const std::uint64_t OFF = 0x3fe6955500000000ull;
	const double Ln2hi = T(kPowLogData, 0), Ln2lo = T(kPowLogData, 1);
	const std::uint64_t tmp = ix - OFF;
	const int i = (int)((tmp >> (52 - 7)) % N);
	const int k = (int)((std::int64_t)tmp >> 52);
	const std::uint64_t iz = ix - (tmp & 0xfffull << 52);
	const double z = asDouble(iz);
	const double kd = (double)k;
	const double invc = T(kPowLogTab, 4 * i), logc = T(kPowLogTab, 4 * i + 2), logctail = T(kPowLogTab, 4 * i + 3);
	const double r = fmadd(z, invc, -1.0);
	const double t1 = fmadd(kd, Ln2hi, logc);
	const double t2 = t1 + r;
	const double lo1 = fmadd(kd, Ln2lo, logctail);
	const double lo2 = t1 - t2 + r;
	const double A0 = T(kPowLogPoly, 0);
	const double ar = A0 * r;
	const double ar2 = r * ar;
	const double ar3 = r * ar2;
	const double hi = t2 + ar2;
	const double lo3 = fmadd(ar, r, -ar2);
	const double lo4 = t2 - hi + ar2;
	// p = ar3 * (A1 + r * A2 + ar2 * (A3 + r * A4 + ar2 * (A5 + r * A6))); lo = lo1 + lo2 + lo3 + lo4 + p (p fused)
	const double q3 = fmadd(r, T(kPowLogPoly, 6), T(kPowLogPoly, 5));
	const double q2 = fmadd(ar2, q3, fmadd(r, T(kPowLogPoly, 4), T(kPowLogPoly, 3)));
	const double q1 = fmadd(ar2, q2, fmadd(r, T(kPowLogPoly, 2), T(kPowLogPoly, 1)));
	const double lo = fmadd(ar3, q1, lo1 + lo2 + lo3 + lo4);
	const double y = hi + lo;
	*tail = hi - y + lo;
	return y;
}

// 0 if not an integer, 1 if odd, 2 if even (iy: a non-zero finite value)
int checkint(std::uint64_t iy)
{
	const int e = (int)(iy >> 52 & 0x7ff);
	if (e < 0x3ff)
	{
		return 0;
	}
	if (e > 0x3ff + 52)
	{
		return 2;
	}
	if (iy & ((1ull << (0x3ff + 52 - e)) - 1))
	{
		return 0;
	}
	if (iy & (1ull << (0x3ff + 52 - e)))
	{
		return 1;
	}
	return 2;
}

inline bool zeroinfnan(std::uint64_t i)
{
	return 2 * i - 1 >= 2 * asU64(INFINITY) - 1;
}

inline bool isSignalingNaN(double x)
{
	const std::uint64_t ix = asU64(x);
	return 2 * (ix ^ 0x0008000000000000ull) > 2 * 0x7ff8000000000000ull;
}

} // namespace

namespace DetLibm
{

double sin(double x)
{
	const std::int32_t k = 0x7fffffff & highWord(x);
	if (k < 0x3e500000)
	{
		return x;
	}
	if (k < 0x3feb6000)
	{
		return doSin(x, 0);
	}
	if (k < 0x400368fd)
	{
		const double t = hp0 - fabsD(x);
		return copySign(doCos(t, hp1), x);
	}
	if (k < 0x419921FB)
	{
		double a, da;
		const int n = reduceSinCos(x, &a, &da);
		return doSinCos(a, da, n);
	}
	if (k < 0x7ff00000)
	{
		double a, da;
		const int n = branred(x, &a, &da);
		return doSinCos(a, da, n);
	}
	return invalid(x);
}

double cos(double x)
{
	const std::int32_t k = 0x7fffffff & highWord(x);
	if (k < 0x3e400000)
	{
		return 1.0;
	}
	if (k < 0x3feb6000)
	{
		return doCos(x, 0);
	}
	if (k < 0x400368fd)
	{
		const double y = hp0 - fabsD(x);
		const double a = y + hp1;
		const double da = (y - a) + hp1;
		return doSin(a, da);
	}
	if (k < 0x419921FB)
	{
		double a, da;
		const int n = reduceSinCos(x, &a, &da);
		return doSinCos(a, da, n + 1);
	}
	if (k < 0x7ff00000)
	{
		double a, da;
		const int n = branred(x, &a, &da);
		return doSinCos(a, da, n + 1);
	}
	return invalid(x);
}

double tan(double x)
{
	const std::int32_t ux = highWord(x);
	if ((ux & 0x7ff00000) == 0x7ff00000)
	{
		return invalid(x); // x - x
	}
	const double w = (x < 0.0) ? -x : x;
	if (w <= tg1)
	{
		return x;
	}
	if (w <= tg2)
	{
		// t2 = (d3 + x2 * (... d11)) * (x * x2); y = x + t2 (fused)
		const double x2 = x * x;
		double t2 = fmadd(x2, td11, td9);
		t2 = fmadd(x2, t2, td7);
		t2 = fmadd(x2, t2, td5);
		t2 = fmadd(x2, t2, td3);
		return fmadd(t2, x * x2, x);
	}
	if (w <= tg3)
	{
		const int i = (int)(tmfftnhf + 256 * w);
		const double z = w - xfg(i, 0);
		const double z2 = z * z;
		const double s = (x < 0.0) ? -1 : 1;
		const double pz = fmadd(z * z2, fmadd(z2, te1, te0), z);
		const double fi = xfg(i, 1);
		const double gi = xfg(i, 2);
		const double t2 = pz * (gi + fi) / (gi - pz);
		const double y = fi + t2;
		return s * y;
	}
	if (w <= tg4)
	{
		// range reduction by algorithm i
		const double t = fmadd(x, thpinv, ttoint);
		const double xn = t - ttoint;
		const double t1 = fnma(xn, tmp2, fnma(xn, tmp1, x));
		const int n = lowWord(t) & 0x00000001;
		// da = xn * mp3 (fused into both uses): a = t1 - da; da = (t1 - a) - da
		const double a = fnma(xn, tmp3, t1);
		const double da = fnma(xn, tmp3, t1 - a);
		return tanReduced(a, da, n);
	}
	if (w <= tg5)
	{
		// range reduction by algorithm ii
		double t = fmadd(x, thpinv, ttoint);
		const double xn = t - ttoint;
		const double t1 = fnma(xn, tmp2, fnma(xn, tmp1, x));
		const int n = lowWord(t) & 0x00000001;
		// da = xn * pp3: t = t1 - da; da = (t1 - t) - da
		t = fnma(xn, tpp3, t1);
		double da = fnma(xn, tpp3, t1 - t);
		// t1 = xn * pp4: a = t - t1; da = ((t - a) - t1) + da
		double a = fnma(xn, tpp4, t);
		da = fnma(xn, tpp4, t - a) + da;
		double e1, e2;
		eadd(a, da, e1, e2);
		return tanReduced(e1, e2, n);
	}
	double a, da;
	const int n = branred(x, &a, &da) & 0x00000001;
	double e1, e2;
	eadd(a, da, e1, e2);
	return tanReduced(e1, e2, n);
}

double atan(double x)
{
	const std::int32_t ux = highWord(x);
	const std::int32_t dx = lowWord(x);
	if (((ux & 0x7ff00000) == 0x7ff00000) && (((ux & 0x000fffff) | dx) != 0x00000000))
	{
		return quiet(x); // x + x
	}
	const double u = (x < 0) ? -x : x;
	if (u < aC)
	{
		if (u < aB)
		{
			if (u < aA)
			{
				return x;
			}
			// yy = P(v) * (x * v); y = x + yy (fused)
			const double v = x * x;
			return fmadd(atanPoly(v), x * v, x);
		}
		const int i = atanIndex(u);
		const double z = u - cij(i, 0);
		double yy = fmadd(z, cij(i, 6), cij(i, 5));
		yy = fmadd(z, yy, cij(i, 4));
		yy = fmadd(z, yy, cij(i, 3));
		yy = fmadd(z, yy, cij(i, 2));
		// yy *= z; y = t1 + yy (fused)
		return copySign(fmadd(yy, z, cij(i, 1)), x);
	}
	if (u < aD)
	{
		const double w = 1 / u;
		// EMULV (w, u, t1, t2): t1 = w * u; t2 = fma (w, u, -t1)
		const double t1 = w * u;
		const double t2 = fms(w, u, t1);
		const int i = atanIndex(w);
		// ww = w * ((1 - t1) - t2); z = (w - cij[i][0]) + ww (fused)
		const double z = fmadd(w, (1 - t1) - t2, w - cij(i, 0));
		double yy = fmadd(z, cij(i, 6), cij(i, 5));
		yy = fmadd(z, yy, cij(i, 4));
		yy = fmadd(z, yy, cij(i, 3));
		yy = fmadd(z, yy, cij(i, 2));
		yy = fnma(z, yy, aHPI1);
		const double tt = aHPI - cij(i, 1);
		return copySign(tt + yy, x);
	}
	if (u < aE)
	{
		const double w = 1 / u;
		const double v = w * w;
		const double t1 = w * u;
		const double t2 = fms(w, u, t1);
		const double p = atanPoly(v);
		double t3, cor;
		esub(aHPI, w, t3, cor);
		// ww = w * ((1 - t1) - t2) and yy = P * (w * v), both fused into yy = ((HPI1 + cor) - ww) - yy
		const double yy = fnma(p, w * v, fnma(w, (1 - t1) - t2, aHPI1 + cor));
		return copySign(t3 + yy, x);
	}
	return x > 0 ? aHPI : aMHPI;
}

double atan2(double y, double x)
{
	const std::int32_t ux = highWord(x), dx = lowWord(x);
	if ((ux & 0x7ff00000) == 0x7ff00000)
	{
		if (((ux & 0x000fffff) | dx) != 0x00000000)
		{
			return nan2(x, y); // x + y
		}
	}
	const std::int32_t uy = highWord(y), dy = lowWord(y);
	if ((uy & 0x7ff00000) == 0x7ff00000)
	{
		if (((uy & 0x000fffff) | dy) != 0x00000000)
		{
			return quiet(y); // y + y
		}
	}
	if (uy == 0x00000000)
	{
		if (dy == 0x00000000)
		{
			return ((ux & 0x80000000) == 0x00000000) ? 0 : aOPI;
		}
	}
	else if (uy == (std::int32_t)0x80000000)
	{
		if (dy == 0x00000000)
		{
			return ((ux & 0x80000000) == 0x00000000) ? -0.0 : aMOPI;
		}
	}
	if (x == 0)
	{
		return ((uy & 0x80000000) == 0x00000000) ? aHPI : aMHPI;
	}
	if (ux == 0x7ff00000)
	{
		if (dx == 0x00000000)
		{
			if (uy == 0x7ff00000)
			{
				if (dy == 0x00000000)
				{
					return aQPI;
				}
			}
			else if (uy == (std::int32_t)0xfff00000)
			{
				if (dy == 0x00000000)
				{
					return aMQPI;
				}
			}
			else
			{
				return ((uy & 0x80000000) == 0x00000000) ? 0 : -0.0;
			}
		}
	}
	else if (ux == (std::int32_t)0xfff00000)
	{
		if (dx == 0x00000000)
		{
			if (uy == 0x7ff00000)
			{
				if (dy == 0x00000000)
				{
					return aTQPI;
				}
			}
			else if (uy == (std::int32_t)0xfff00000)
			{
				if (dy == 0x00000000)
				{
					return aMTQPI;
				}
			}
			else
			{
				return ((uy & 0x80000000) == 0x00000000) ? aOPI : aMOPI;
			}
		}
	}
	if (uy == 0x7ff00000)
	{
		if (dy == 0x00000000)
		{
			return aHPI;
		}
	}
	else if (uy == (std::int32_t)0xfff00000)
	{
		if (dy == 0x00000000)
		{
			return aMHPI;
		}
	}

	const int ep = 59768832, em = -59768832;
	double ax = (x < 0) ? -x : x;
	double ay = (y < 0) ? -y : y;
	const int de = (uy & 0x7ff00000) - (ux & 0x7ff00000);
	if (de >= ep)
	{
		return (y > 0) ? aHPI : aMHPI;
	}
	if (de <= em)
	{
		if (x > 0)
		{
			return copySign(ay / ax, y);
		}
		return (y > 0) ? aOPI : aMOPI;
	}
	if (ax < aTwom500 || ay < aTwom500)
	{
		ax *= aTwo500;
		ay *= aTwo500;
	}
	if (ax > aTwo500 || ay > aTwo500)
	{
		ax *= aTwom500;
		ay *= aTwom500;
	}

	double u, du;
	if (ay < ax)
	{
		u = ay / ax;
		const double v = ax * u;
		const double vv = fms(ax, u, v);
		du = ((ay - v) - vv) / ax;
	}
	else
	{
		u = ax / ay;
		const double v = ay * u;
		const double vv = fms(ay, u, v);
		du = ((ax - v) - vv) / ay;
	}

	if (x > 0)
	{
		// (i) x > 0, |y| < |x|: atan (ay / ax)
		if (ay < ax)
		{
			if (u < aInv16)
			{
				const double v = u * u;
				// zz = du + u * v * P(v) (fused); z = u + zz
				const double zz = fmadd(u * v, atanPoly(v), du);
				return copySign(u + zz, y);
			}
			const int i = atanIndex(u);
			const double t3 = u - cij(i, 0);
			double v, dv;
			eadd(t3, du, v, dv);
			const double t1 = cij(i, 1);
			const double t2 = cij(i, 2);
			// zz = v * t2 + (dv * t2 + v * v * (c3 + v * (c4 + v * (c5 + v * c6))))
			double q = fmadd(v, cij(i, 6), cij(i, 5));
			q = fmadd(v, q, cij(i, 4));
			q = fmadd(v, q, cij(i, 3));
			const double zz = fmadd(v, t2, fmadd(dv, t2, (v * v) * q));
			return copySign(t1 + zz, y);
		}
		// (ii) x > 0, |x| <= |y|: pi/2 - atan (ax / ay)
		if (u < aInv16)
		{
			const double v = u * u;
			double t2, cor;
			esub(aHPI, u, t2, cor);
			// zz = u * v * P(v) is computed before ESUB's branch and used after it: not fused
			const double zz = (u * v) * atanPoly(v);
			const double t3 = ((aHPI1 + cor) - du) - zz;
			return copySign(t2 + t3, y);
		}
		const int i = atanIndex(u);
		const double v = (u - cij(i, 0)) + du;
		const double zz = fnma(v, cijPoly2(i, v), aHPI1);
		const double t1 = aHPI - cij(i, 1);
		return copySign(t1 + zz, y);
	}

	// (iii) x < 0, |x| < |y|: pi/2 + atan (ax / ay)
	if (ax < ay)
	{
		if (u < aInv16)
		{
			const double v = u * u;
			double t2, cor;
			eadd(aHPI, u, t2, cor);
			const double zz = (u * v) * atanPoly(v); // before EADD's branch: not fused
			const double t3 = ((aHPI1 + cor) + du) + zz;
			return copySign(t2 + t3, y);
		}
		const int i = atanIndex(u);
		const double v = (u - cij(i, 0)) + du;
		const double zz = fmadd(v, cijPoly2(i, v), aHPI1);
		const double t1 = aHPI + cij(i, 1);
		return copySign(t1 + zz, y);
	}

	// (iv) x < 0, |y| <= |x|: pi - atan (ax / ay)
	if (u < aInv16)
	{
		const double v = u * u;
		double t2, cor;
		esub(aOPI, u, t2, cor);
		const double zz = (u * v) * atanPoly(v); // before ESUB's branch: not fused
		const double t3 = ((aOPI1 + cor) - du) - zz;
		return copySign(t2 + t3, y);
	}
	const int i = atanIndex(u);
	const double v = (u - cij(i, 0)) + du;
	const double zz = fnma(v, cijPoly2(i, v), aOPI1);
	const double t1 = aOPI - cij(i, 1);
	return copySign(t1 + zz, y);
}

double asin(double x)
{
	if (fabsD(x) > 1.0)
	{
		return asDouble(kPositiveNaN); // w_asin_compat.c: __kernel_standard (x, x, 2), NAN
	}
	const std::int32_t m = highWord(x);
	std::int32_t k = 0x7fffffff & m;
	if (k < 0x3e500000)
	{
		return x;
	}
	if (k < 0x3fc00000)
	{
		// t = P(x2) * (x2 * x); res = x + t (fused)
		const double x2 = x * x;
		return fmadd(asinF(x2), x2 * x, x);
	}
	int degree;
	const int n = asinSegmentOf(k, degree);
	if (n >= 0)
	{
		int base;
		const double t = asinSegment(m > 0 ? x : -x, n, degree, base);
		const double res = asncs(base) + t;
		return (m > 0) ? res : -res;
	}
	if (k < 0x3ff00000)
	{
		const double z = 0.5 * ((m > 0) ? (1.0 - x) : (1.0 + x));
		k = highWord(z);
		double t = T(kInRoot, (k & 0x001fffff) >> 14) * T(kPowTwo, 511 - (k >> 21));
		const double r = fnma(t * t, z, 1.0);
		t = t * fmadd(r, fmadd(r, fmadd(r, rt3, rt2), rt1), rt0);
		const double c = t * z;
		// t = c * (1.5 - 0.5 * t * c), only used in (t + y) (fused)
		const double h = fnma(0.5 * t, c, 1.5);
		const double y = (c + t24) - t24;
		const double cc = fnma(y, y, z) / fmadd(c, h, y);
		const double p = asinF(z) * z;
		// cor = (hp1 - 2.0 * cc) - 2.0 * (y + cc) * p
		const double cor = fnma(2.0 * (y + cc), p, fnma(2.0, cc, hp1));
		const double res1 = fnma(2.0, y, hp0);
		const double res = res1 + cor;
		return (m > 0) ? res : -res;
	}
	if (k == 0x3ff00000 && lowWord(x) == 0)
	{
		return (m > 0) ? hp0 : -hp0;
	}
	return invalid(x);
}

double acos(double x)
{
	if (fabsD(x) > 1.0)
	{
		return asDouble(kPositiveNaN); // w_acos_compat.c: __kernel_standard (x, x, 1), NAN
	}
	const std::int32_t m = highWord(x);
	std::int32_t k = 0x7fffffff & m;
	if (k < 0x3c880000)
	{
		return hp0;
	}
	if (k < 0x3fc00000)
	{
		const double x2 = x * x;
		const double r = hp0 - x;
		// t = P(x2) * (x2 * x); cor = (((hp0 - r) - x) + hp1) - t (fused)
		const double cor = fnma(asinF(x2), x2 * x, ((hp0 - r) - x) + hp1);
		return r + cor;
	}
	int degree;
	const int n = asinSegmentOf(k, degree);
	if (n >= 0)
	{
		int base;
		double t = asinSegment(m > 0 ? x : -x, n, degree, base);
		const double y = (m > 0) ? (hp0 - asncs(base)) : (hp0 + asncs(base));
		t = (m > 0) ? (hp1 - t) : (hp1 + t);
		return y + t;
	}
	if (k < 0x3ff00000)
	{
		const double z = 0.5 * ((m > 0) ? (1.0 - x) : (1.0 + x));
		k = highWord(z);
		double t = T(kInRoot, (k & 0x001fffff) >> 14) * T(kPowTwo, 511 - (k >> 21));
		const double r = fnma(t * t, z, 1.0);
		t = t * fmadd(r, fmadd(r, fmadd(r, rt3, rt2), rt1), rt0);
		const double c = t * z;
		const double h = fnma(0.5 * t, c, 1.5);
		// y = (t27 * c + c) - t27 * c (t27 * c is exact: fused into both)
		const double y = fnma(t27, c, fmadd(t27, c, c));
		const double cc = fnma(y, y, z) / fmadd(c, h, y);
		const double p = asinF(z) * z;
		// GCC computes p * (y + cc) once above the branch (both branches use it), so it is fused into neither
		const double pyc = p * (y + cc);
		if (m < 0)
		{
			const double cor = (hp1 - cc) - pyc;
			const double res1 = hp0 - y;
			const double res = res1 + cor;
			return res + res;
		}
		const double cor = cc + pyc;
		const double res = y + cor;
		return res + res;
	}
	if (k == 0x3ff00000 && lowWord(x) == 0)
	{
		return (m > 0) ? 0 : 2.0 * hp0;
	}
	return invalid(x);
}

double exp(double x)
{
	return expCore(x, 0.0, 0, false);
}

double log(double x)
{
	const double Ln2hi = T(kLogData, 0), Ln2lo = T(kLogData, 1);
	std::uint64_t ix = asU64(x);
	const std::uint32_t top = top16(x);
	const std::uint64_t LO = asU64(1.0 - 0x1p-4);
	const std::uint64_t HI = asU64(1.0 + 0x1.09p-4);
	if (ix - LO < HI - LO)
	{
		if (ix == asU64(1.0))
		{
			return 0;
		}
		const double r = x - 1.0;
		const double r2 = r * r;
		const double r3 = r * r2;
		// y = r3 * (B1 + r * B2 + r2 * B3 + r3 * (B4 + r * B5 + r2 * B6 + r3 * (B7 + r * B8 + r2 * B9 + r3 * B10)))
		const double q3 = fmadd(r3, T(kLogPoly1, 10), fmadd(r2, T(kLogPoly1, 9), fmadd(r, T(kLogPoly1, 8), T(kLogPoly1, 7))));
		const double q2 = fmadd(r3, q3, fmadd(r2, T(kLogPoly1, 6), fmadd(r, T(kLogPoly1, 5), T(kLogPoly1, 4))));
		const double q1 = fmadd(r3, q2, fmadd(r2, T(kLogPoly1, 3), fmadd(r, T(kLogPoly1, 2), T(kLogPoly1, 1))));
		const double B0 = T(kLogPoly1, 0);
		// w = r * 0x1p27 (exact): rhi = r + w - w
		const double rhi = fnma(r, 0x1p27, fmadd(r, 0x1p27, r));
		const double rlo = r - rhi;
		// w = rhi * rhi * B0 (exact): hi = r + w; lo = r - hi + w
		const double rr = rhi * rhi;
		const double hi = fmadd(rr, B0, r);
		double lo = fmadd(rr, B0, r - hi);
		// lo += B0 * rlo * (rhi + r)
		lo = fmadd(B0 * rlo, rhi + r, lo);
		// y += lo (y = r3 * q1 fused); y += hi
		double y = fmadd(r3, q1, lo);
		y += hi;
		return y;
	}
	if (top - 0x0010 >= 0x7ff0 - 0x0010)
	{
		if (ix * 2 == 0)
		{
			return mathDivzero(1);
		}
		if (ix == asU64(INFINITY))
		{
			return x;
		}
		if ((top & 0x8000) || (top & 0x7ff0) == 0x7ff0)
		{
			return invalid(x);
		}
		ix = asU64(x * 0x1p52);
		ix -= 52ull << 52;
	}
	const int N = 128;
	const std::uint64_t OFF = 0x3fe6000000000000ull;
	const std::uint64_t tmp = ix - OFF;
	const int i = (int)((tmp >> (52 - 7)) % N);
	const int k = (int)((std::int64_t)tmp >> 52);
	const std::uint64_t iz = ix - (tmp & 0xfffull << 52);
	const double invc = T(kLogTab, 2 * i);
	const double logc = T(kLogTab, 2 * i + 1);
	const double z = asDouble(iz);
	const double r = fmadd(z, invc, -1.0);
	const double kd = (double)k;
	const double w = fmadd(kd, Ln2hi, logc);
	const double hi = w + r;
	const double lo = fmadd(kd, Ln2lo, w - hi + r);
	const double r2 = r * r;
	// y = lo + r2 * A0 + r * r2 * (A1 + r * A2 + r2 * (A3 + r * A4)) + hi
	const double q = fmadd(r2, fmadd(r, T(kLogPoly, 4), T(kLogPoly, 3)), fmadd(r, T(kLogPoly, 2), T(kLogPoly, 1)));
	const double y = fmadd(r * r2, q, fmadd(r2, T(kLogPoly, 0), lo)) + hi;
	return y;
}

double log10(double x)
{
	// e_log10.c (fdlibm): not in glibc's FMA list, plain operations; its __ieee754_log is the FMA log above
	const double two54 = 1.80143985094819840000e+16;
	const double ivln10 = 4.34294481903251816668e-01;
	const double log10_2hi = 3.01029995663611771306e-01;
	const double log10_2lo = 3.69423907715893078616e-13;
	// w_log10_compat.c: x <= 0 goes to __kernel_standard: log10 (0) = -HUGE_VAL, log10 (x < 0) = NAN
	if (x <= 0.0)
	{
		return x == 0.0 ? -INFINITY : asDouble(kPositiveNaN);
	}
	std::int64_t hx = (std::int64_t)asU64(x);
	std::int32_t k = 0;
	if (hx < (std::int64_t)0x0010000000000000ll)
	{
		if ((hx & 0x7fffffffffffffffll) == 0)
		{
			return -two54 / 0.0; // -two54 / fabs (x): -inf
		}
		if (hx < 0)
		{
			return invalid(x);
		}
		k -= 54;
		x *= two54;
		hx = (std::int64_t)asU64(x);
	}
	if ((std::uint64_t)hx >= 0x7ff0000000000000ull)
	{
		return isNaN(x) ? quiet(x) : x + x; // +inf stays +inf
	}
	k += (std::int32_t)((hx >> 52) - 1023);
	const std::int64_t i = (std::int64_t)(((std::uint64_t)(std::int64_t)k & 0x8000000000000000ull) >> 63);
	hx = (hx & 0x000fffffffffffffll) | ((0x3ff - i) << 52);
	const double y = (double)(k + i);
	x = asDouble((std::uint64_t)hx);
	const double a = y * log10_2lo;
	const double b = ivln10 * DetLibm::log(x);
	const double z = a + b;
	const double c = y * log10_2hi;
	return z + c;
}

double pow(double x, double y)
{
	const std::uint32_t SIGN_BIAS = 0x800 << EXP_TABLE_BITS;
	std::uint32_t signBias = 0;
	std::uint64_t ix = asU64(x);
	const std::uint64_t iy = asU64(y);
	std::uint32_t topx = top12(x);
	const std::uint32_t topy = top12(y);
	if (topx - 0x001 >= 0x7ff - 0x001 || (topy & 0x7ff) - 0x3be >= 0x43e - 0x3be)
	{
		if (zeroinfnan(iy))
		{
			if (2 * iy == 0)
			{
				return isSignalingNaN(x) ? quiet(x) : 1.0; // x + y
			}
			if (ix == asU64(1.0))
			{
				return isSignalingNaN(y) ? quiet(y) : 1.0; // x + y with x = 1
			}
			if (2 * ix > 2 * asU64(INFINITY) || 2 * iy > 2 * asU64(INFINITY))
			{
				return nan2(x, y);
			}
			if (2 * ix == 2 * asU64(1.0))
			{
				return 1.0;
			}
			if ((2 * ix < 2 * asU64(1.0)) == !(iy >> 63))
			{
				return 0.0;
			}
			return y * y;
		}
		if (zeroinfnan(ix))
		{
			double x2 = x * x;
			if ((ix >> 63) && checkint(iy) == 1)
			{
				x2 = -x2;
				signBias = 1;
			}
			if (2 * ix == 0 && (iy >> 63))
			{
				return mathDivzero(signBias);
			}
			return (iy >> 63) ? 1 / x2 : x2;
		}
		if (ix >> 63)
		{
			const int yint = checkint(iy);
			if (yint == 0)
			{
				return invalid(x); // __math_invalid (x), x finite: the default NaN
			}
			if (yint == 1)
			{
				signBias = SIGN_BIAS;
			}
			ix &= 0x7fffffffffffffffull;
			topx &= 0x7ff;
		}
		if ((topy & 0x7ff) - 0x3be >= 0x43e - 0x3be)
		{
			if (ix == asU64(1.0))
			{
				return 1.0;
			}
			if ((topy & 0x7ff) < 0x3be)
			{
				return ix > asU64(1.0) ? 1.0 + y : 1.0 - y;
			}
			return (ix > asU64(1.0)) == (topy < 0x800) ? mathOflow(0) : mathUflow(0);
		}
		if (topx == 0)
		{
			ix = asU64(x * 0x1p52);
			ix &= 0x7fffffffffffffffull;
			ix -= 52ull << 52;
		}
	}
	double lo;
	const double hi = powLogInline(ix, &lo);
	const double ehi = y * hi;
	const double elo = fmadd(y, lo, fms(y, hi, ehi));
	return expCore(ehi, elo, signBias, true);
}


// ---- the exact functions (lane DEP-2 review): one correct result exists, so any correct code gives the same finite bits; the special operands
// are decided here explicitly (no host NaN rules) ----

double fma(double a, double b, double c)
{
	return fmaSoft(a, b, c);
}

double sqrt(double x)
{
	if (isNaN(x))
	{
		return quiet(x);
	}
	if (x < 0.0)
	{
		return asDouble(kDefaultNaN); // sqrtsd's invalid result (AArch64's fsqrt would give +NaN: decided here instead)
	}
	return std::sqrt(x); // correctly rounded by IEEE 754 on every target: the instruction (the target is built with -fno-math-errno)
}

double fabs(double x)
{
	return fabsD(x);
}

double floor(double x)
{
	const std::uint64_t u = asU64(x);
	const int be = (int)((u >> 52) & 0x7FF);
	if (be == 0x7FF)
	{
		return x; // infinities and NaN unchanged, even signalling ones (GCC's inline floor / ceil, not every host libm)
	}
	if (be >= 1075)
	{
		return x; // integral already
	}
	if (be < 1023) // |x| < 1
	{
		if ((u & 0x7FFFFFFFFFFFFFFFull) == 0)
		{
			return x; // +-0
		}
		return (u >> 63) ? -1.0 : 0.0;
	}
	const std::uint64_t frac = 0x000FFFFFFFFFFFFFull >> (be - 1023);
	if ((u & frac) == 0)
	{
		return x;
	}
	const double t = asDouble(u & ~frac); // toward zero
	return (u >> 63) ? t - 1.0 : t;
}

double ceil(double x)
{
	const std::uint64_t u = asU64(x);
	const int be = (int)((u >> 52) & 0x7FF);
	if (be == 0x7FF)
	{
		return x; // infinities and NaN unchanged, even signalling ones (GCC's inline floor / ceil, not every host libm)
	}
	if (be >= 1075)
	{
		return x;
	}
	if (be < 1023)
	{
		if ((u & 0x7FFFFFFFFFFFFFFFull) == 0)
		{
			return x;
		}
		return (u >> 63) ? -0.0 : 1.0;
	}
	const std::uint64_t frac = 0x000FFFFFFFFFFFFFull >> (be - 1023);
	if ((u & frac) == 0)
	{
		return x;
	}
	const double t = asDouble(u & ~frac);
	return (u >> 63) ? t : t + 1.0;
}

double fmod(double x, double y)
{
	if (isNaN(x) || isNaN(y))
	{
		// one NaN operand: glibc's x * y, the NaN quietened. Two NaN operands: the first (glibc's pick there is its compiler's operand order:
		// the Deck's glibc 2.41 differs between the two branches of e_fmod.c; not a contract)
		return nan2(x, y);
	}
	const std::uint64_t ux = asU64(x), uy = asU64(y);
	const std::uint64_t ax = ux & 0x7FFFFFFFFFFFFFFFull, ay = uy & 0x7FFFFFFFFFFFFFFFull;
	if (ax == 0x7FF0000000000000ull || ay == 0)
	{
		return asDouble(kDefaultNaN); // fmod (inf, y), fmod (x, 0): invalid
	}
	if (ay == 0x7FF0000000000000ull || ax < ay)
	{
		return x; // |x| < |y| (zeros included)
	}
	if (ax == ay)
	{
		return asDouble(ux & 0x8000000000000000ull);
	}
	std::uint64_t mx, my;
	int ex, ey;
	unpack(ax, mx, ex);
	unpack(ay, my, ey);
	// remainder of mx * 2^(ex - ey) by my, 11 bits at a time (mx < 2^53, so r << 11 < 2^64)
	std::uint64_t r = mx % my;
	int d = ex - ey;
	while (d > 0 && r != 0)
	{
		const int step = d < 11 ? d : 11;
		r = (r << step) % my;
		d -= step;
	}
	if (r == 0)
	{
		return asDouble(ux & 0x8000000000000000ull);
	}
	// r * 2^ey, exact (r < my, so it fits the format; it may be subnormal). x and y lie on the 2^-1074 grid, so the remainder does too: a
	// subnormal y (normalised below 2^-1074) leaves r divisible by 2^(-1074 - ey)
	int e = ey;
	if (e < -1074)
	{
		r >>= (-1074 - e);
		e = -1074;
	}
	while (!(r >> 52) && e > -1074)
	{
		r <<= 1;
		--e;
	}
	const std::uint64_t sign = ux & 0x8000000000000000ull;
	if (!(r >> 52))
	{
		return asDouble(sign | r); // subnormal (e == -1074)
	}
	return asDouble(sign | ((std::uint64_t)(e + 1075) << 52) | (r & 0x000FFFFFFFFFFFFFull));
}

double ldexp(double x, int n)
{
	const std::uint64_t u = asU64(x);
	const std::uint64_t ax = u & 0x7FFFFFFFFFFFFFFFull, sign = u & 0x8000000000000000ull;
	if (ax >= 0x7FF0000000000000ull || ax == 0)
	{
		return isNaN(x) ? quiet(x) : x;
	}
	std::uint64_t m;
	int e;
	unpack(ax, m, e); // x = m * 2^e, m in [2^52, 2^53)
	const long long target = (long long)e + n; // the exponent of the last bit
	if (target > 1023 - 52)
	{
		return asDouble(sign | 0x7FF0000000000000ull);
	}
	if (target >= -1074)
	{
		return asDouble(sign | ((std::uint64_t)(target + 1075) << 52) | (m & 0x000FFFFFFFFFFFFFull));
	}
	// subnormal: drop (-1074 - target) bits, round to nearest even
	const long long drop = -1074 - target;
	if (drop > 53)
	{
		return asDouble(sign); // below half the smallest subnormal
	}
	std::uint64_t q = m >> drop;
	const std::uint64_t rem = m & ((1ull << drop) - 1), half = 1ull << (drop - 1);
	if (rem > half || (rem == half && (q & 1)))
	{
		++q; // may become 2^52: the smallest normal, which the same bit pattern encodes
	}
	return asDouble(sign | q);
}

double frexp(double x, int *exponent)
{
	const std::uint64_t u = asU64(x);
	const std::uint64_t ax = u & 0x7FFFFFFFFFFFFFFFull;
	if (ax >= 0x7FF0000000000000ull || ax == 0)
	{
		*exponent = 0;
		return isNaN(x) ? quiet(x) : x;
	}
	std::uint64_t m;
	int e;
	unpack(ax, m, e);
	*exponent = e + 53;
	return asDouble((u & 0x8000000000000000ull) | (1022ull << 52) | (m & 0x000FFFFFFFFFFFFFull));
}
} // namespace DetLibm

extern "C" {
double openbfme_detlibm_sin(double x) { return DetLibm::sin(x); }
double openbfme_detlibm_cos(double x) { return DetLibm::cos(x); }
double openbfme_detlibm_tan(double x) { return DetLibm::tan(x); }
double openbfme_detlibm_asin(double x) { return DetLibm::asin(x); }
double openbfme_detlibm_acos(double x) { return DetLibm::acos(x); }
double openbfme_detlibm_atan(double x) { return DetLibm::atan(x); }
double openbfme_detlibm_atan2(double y, double x) { return DetLibm::atan2(y, x); }
double openbfme_detlibm_exp(double x) { return DetLibm::exp(x); }
double openbfme_detlibm_log(double x) { return DetLibm::log(x); }
double openbfme_detlibm_log10(double x) { return DetLibm::log10(x); }
double openbfme_detlibm_pow(double x, double y) { return DetLibm::pow(x, y); }
double openbfme_detlibm_sqrt(double x) { return DetLibm::sqrt(x); }
double openbfme_detlibm_fabs(double x) { return DetLibm::fabs(x); }
double openbfme_detlibm_floor(double x) { return DetLibm::floor(x); }
double openbfme_detlibm_ceil(double x) { return DetLibm::ceil(x); }
double openbfme_detlibm_fmod(double x, double y) { return DetLibm::fmod(x, y); }
double openbfme_detlibm_ldexp(double x, int n) { return DetLibm::ldexp(x, n); }
double openbfme_detlibm_frexp(double x, int *exponent) { return DetLibm::frexp(x, exponent); }
}
