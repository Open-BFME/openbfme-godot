// OpenBFME. GPL-3.0. NEW FILE of the Lua patch layer (engine/src/Libraries/Lua/README.md).
//
// The numeric behaviour of the retail Lua VM that is not plain C double arithmetic: 24 bit precision control,
// number to text and text to number as MSVCR71 does them, the hash of a number key.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * Retail runs with the x87 precision control at 24 bits (setFPMode RW 0x440809, PLAN rule 3, NumericState.h). The
//     VM's arithmetic is `fld qword; fadd/fsub/fmul/fdiv qword; fstp qword` (RW 0xB63553, 0xB6360C, ...): the result of
//     each operation is rounded ONCE to a 24 bit significand (round to nearest even, extended exponent range) and
//     then stored as a double. luaEA_round24 reproduces that exactly for results in the double normal range.
//   * luaV_tostring RW 0xB625B0 formats a number with the format string at RW 0xD0AC04, "%.16g", through MSVCR71's
//     sprintf (IAT 0xBD06C0). MSVCR71 prints a 3 digit exponent ("1e+020") and spells the special values 1.#INF,
//     -1.#INF, 1.#QNAN, -1.#IND, 1.#SNAN (the pre-VS2015 CRT). The unit tests pin these spellings; they are CRT
//     facts, not read from the binary (the binary only holds the call).
//   * strtod of MSVCR71 reads [white space][sign]digits[.digits][e|E[sign]digits]; it does not read "inf", "nan" or
//     hexadecimal. luaO_str2d requires the whole string (after trailing white space) to be consumed.
//   * luaH_getnum RW: the hash of a number key is `(unsigned long)(long)key`, an _ftol (truncating, 64 bit store, low 32
//     bits; integer indefinite for NaN, infinities and |key| >= 2^63).
// INFERENCE: the precision state during script execution is assumed PC24 (stop S-123).

#include "lua_ea.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {

double luaEA_round24(double r, int errsign)
{
	std::uint64_t bits;
	std::memcpy(&bits, &r, sizeof bits);
	const unsigned exponent = (unsigned)((bits >> 52) & 0x7FF);
	if (exponent == 0 || exponent == 0x7FF)
	{
		return r; // zero, subnormal, infinity, NaN
	}
	const std::uint64_t dropMask = (std::uint64_t(1) << 29) - 1; // 53 - 24 bits
	const std::uint64_t half = std::uint64_t(1) << 28;
	const std::uint64_t rem = bits & dropMask;
	// sign of (|exact| - |r|)
	const int magnitudeSign = (bits >> 63) ? -errsign : errsign;
	bool up;
	if (rem > half)
	{
		up = true;
	}
	else if (rem < half)
	{
		up = false;
	}
	else if (magnitudeSign > 0)
	{
		up = true;
	}
	else if (magnitudeSign < 0)
	{
		up = false;
	}
	else
	{
		up = ((bits >> 29) & 1) != 0; // exact tie: to even
	}
	bits &= ~dropMask;
	if (up)
	{
		bits += std::uint64_t(1) << 29; // a carry into the exponent is the correct result (and may reach infinity)
	}
	std::memcpy(&r, &bits, sizeof r);
	return r;
}

static int signOf(double v) { return (v > 0) - (v < 0); }

double luaEA_add(int mode, double a, double b)
{
	const double r = a + b;
	if (mode != LUA_EA_NUM_PC24 || !std::isfinite(r) || r == 0.0)
	{
		return r;
	}
	// Knuth TwoSum: a + b == r + err exactly
	const double bb = r - a;
	const double err = (a - (r - bb)) + (b - bb);
	return luaEA_round24(r, signOf(err));
}

double luaEA_sub(int mode, double a, double b)
{
	return luaEA_add(mode, a, -b);
}

double luaEA_mul(int mode, double a, double b)
{
	const double r = a * b;
	if (mode != LUA_EA_NUM_PC24 || !std::isfinite(r) || r == 0.0)
	{
		return r;
	}
	const double err = std::fma(a, b, -r); // a * b == r + err exactly
	return luaEA_round24(r, signOf(err));
}

double luaEA_div(int mode, double a, double b)
{
	const double r = a / b;
	if (mode != LUA_EA_NUM_PC24 || !std::isfinite(r) || r == 0.0)
	{
		return r;
	}
	const double rem = std::fma(-r, b, a); // a - r * b: the exact quotient is r + rem / b
	return luaEA_round24(r, b < 0 ? -signOf(rem) : signOf(rem));
}

unsigned long luaEA_numhash(double n)
{
	if (!(n > -9223372036854775808.0 && n < 9223372036854775808.0))
	{
		return 0;
	}
	const std::int64_t v = (std::int64_t)n; // truncating
	return (unsigned long)(std::uint32_t)((std::uint64_t)v & 0xFFFFFFFFu);
}

// ---- the decimal text of a double, exact and independent of the host --------------------------------------------------------------
// The exact decimal expansion of the double (an integer times a power of two, so a finite decimal) is built with a small base 10^9 big number and
// then rounded the way MSVCR71 does it (formatG16: two stages, 17 then 16 significant digits, each by its guard digit alone). No FPU rounding mode,
// no C library formatting and no locale is involved. The algorithm agrees with the DLL's own instructions (x86 emulation, not a running Windows
// oracle) on 72,590 sampled doubles; what no execution covers (the 80 bit power of ten table of `$I10_OUTPUT` could differ from the exact
// expansion for a value outside the samples) stays in stop S-123.
namespace
{
typedef std::vector<std::uint32_t> Big; // base 1e9, least significant first

void bigMulSmall(Big &b, std::uint32_t m)
{
	std::uint64_t carry = 0;
	for (std::uint32_t &limb : b)
	{
		const std::uint64_t cur = (std::uint64_t)limb * m + carry;
		limb = (std::uint32_t)(cur % 1000000000u);
		carry = cur / 1000000000u;
	}
	while (carry)
	{
		b.push_back((std::uint32_t)(carry % 1000000000u));
		carry /= 1000000000u;
	}
}

std::string bigToDecimal(const Big &b)
{
	std::string out;
	char buf[16];
	for (size_t i = b.size(); i-- > 0;)
	{
		std::snprintf(buf, sizeof buf, i + 1 == b.size() ? "%u" : "%09u", b[i]);
		out += buf;
	}
	return out;
}

// digits (no leading zeros, "0" for zero) and the decimal exponent: value = 0.DIGITS * 10^exp10 is not used; here value = DIGITS * 10^(-scale)
void exactDecimal(double x, std::string &digits, int &scale)
{
	std::uint64_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	const int e = (int)((bits >> 52) & 0x7FF);
	std::uint64_t mant = bits & ((std::uint64_t(1) << 52) - 1);
	int exp2;
	if (e == 0)
	{
		exp2 = -1074;
	}
	else
	{
		mant |= std::uint64_t(1) << 52;
		exp2 = e - 1075;
	}
	Big b;
	b.push_back((std::uint32_t)(mant % 1000000000u));
	if (mant / 1000000000u)
	{
		b.push_back((std::uint32_t)((mant / 1000000000u) % 1000000000u));
		if (mant / 1000000000000000000u)
		{
			b.push_back((std::uint32_t)(mant / 1000000000000000000u));
		}
	}
	if (exp2 >= 0)
	{
		for (int i = 0; i < exp2; ++i)
		{
			bigMulSmall(b, 2);
		}
		scale = 0;
	}
	else
	{
		// m * 2^-k = m * 5^k / 10^k
		for (int i = 0; i < -exp2; ++i)
		{
			bigMulSmall(b, 5);
		}
		scale = -exp2;
	}
	digits = bigToDecimal(b);
	size_t z = digits.find_first_not_of('0');
	digits = z == std::string::npos ? std::string("0") : digits.substr(z);
}

// keep the first n digits of d; the digit after them decides alone: >= '5' adds one at the last kept digit (a carry out of the front is a new
// leading 1 and X + 1). Nothing happens when d has n digits or fewer.
void roundGuard(std::string &d, size_t n, int &X)
{
	if (d.size() <= n)
	{
		return;
	}
	const bool up = d[n] >= '5';
	d.resize(n);
	if (!up)
	{
		return;
	}
	size_t i = n;
	while (i > 0 && d[i - 1] == '9')
	{
		d[i - 1] = '0';
		--i;
	}
	if (i > 0)
	{
		++d[i - 1];
	}
	else
	{
		d.insert(d.begin(), '1');
		d.resize(n);
		++X;
	}
}

// MSVCR71 printf("%.16g"): at least three exponent digits, %g's rule for the style, trailing zeros removed
void formatG16(char *out, double n)
{
	const int P = 16;
	std::string d;
	int scale = 0;
	exactDecimal(n < 0 || (n == 0 && std::signbit(n)) ? -n : n, d, scale);
	std::string sign = std::signbit(n) ? "-" : "";
	if (d == "0")
	{
		std::strcpy(out, "0"); // either zero sign: the DLL's zero path clears the sign (0x7C37208D): -0.0 prints "0"
		return;
	}
	// decimal exponent of the first digit: value = d[0].d[1]... * 10^X
	int X = (int)d.size() - 1 - scale;
	// MSVCR71's two stage conversion (executed from the DLL's own instructions in an x86 emulator, 0x7C372A35 _fltout, $I10_OUTPUT 0x7C37203C,
	// the digit copy 0x7C355AA0): 17 significant digits first, then the requested 16. Each stage rounds UP when the next digit is >= '5',
	// looking at that one digit only (no parity test, no look at the digits after it), so the result is not the correctly rounded one:
	// 1000000000000002.5 prints 1000000000000003 and 1.2345678901234567 prints 1.234567890123457.
	roundGuard(d, 17, X);
	roundGuard(d, (size_t)P, X);
	while (d.size() > 1 && d.back() == '0')
	{
		d.pop_back();
	}
	std::string text = sign;
	if (X < -4 || X >= P)
	{
		text += d[0];
		if (d.size() > 1)
		{
			text += '.';
			text += d.substr(1);
		}
		char ex[16];
		std::snprintf(ex, sizeof ex, "e%c%03d", X < 0 ? '-' : '+', X < 0 ? -X : X);
		text += ex;
	}
	else if (X >= 0)
	{
		if ((int)d.size() <= X + 1)
		{
			text += d + std::string((size_t)(X + 1 - (int)d.size()), '0');
		}
		else
		{
			text += d.substr(0, (size_t)X + 1) + "." + d.substr((size_t)X + 1);
		}
	}
	else
	{
		text += "0." + std::string((size_t)(-X - 1), '0') + d;
	}
	std::strcpy(out, text.c_str());
}
} // namespace

void luaEA_number2str(char *s, double n)
{
	std::uint64_t bits;
	std::memcpy(&bits, &n, sizeof bits);
	const bool negative = (bits >> 63) != 0;
	if (std::isnan(n))
	{
		if (bits == 0xFFF8000000000000ull)
		{
			std::strcpy(s, "-1.#IND"); // the x87 default NaN
		}
		else if (bits & (std::uint64_t(1) << 51))
		{
			std::strcpy(s, negative ? "-1.#QNAN" : "1.#QNAN");
		}
		else
		{
			std::strcpy(s, negative ? "-1.#SNAN" : "1.#SNAN");
		}
		return;
	}
	if (std::isinf(n))
	{
		std::strcpy(s, negative ? "-1.#INF" : "1.#INF");
		return;
	}
	formatG16(s, n);
}

static bool isSpace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static bool isDigit(char c) { return c >= '0' && c <= '9'; }

double luaEA_str2number(const char *s, char **endptr)
{
	const char *p = s;
	while (isSpace((unsigned char)*p))
	{
		++p;
	}
	const char *numberStart = p;
	bool negative = false;
	if (*p == '+' || *p == '-')
	{
		negative = *p == '-';
		++p;
	}
	const char *mantissaStart = p;
	size_t digits = 0;
	while (isDigit(*p))
	{
		++p;
		++digits;
	}
	if (*p == '.')
	{
		++p;
		while (isDigit(*p))
		{
			++p;
			++digits;
		}
	}
	if (digits == 0)
	{
		if (endptr)
		{
			*endptr = const_cast<char *>(s); // no conversion
		}
		return 0.0;
	}
	if (*p == 'e' || *p == 'E')
	{
		const char *q = p + 1;
		if (*q == '+' || *q == '-')
		{
			++q;
		}
		if (isDigit(*q))
		{
			while (isDigit(*q))
			{
				++q;
			}
			p = q;
		}
	}
	(void)numberStart;
	double value = 0.0;
	const std::from_chars_result res = std::from_chars(mantissaStart, p, value, std::chars_format::general);
	if (res.ec == std::errc::result_out_of_range)
	{
		// overflow: HUGE_VAL; underflow: 0 (MSVCR71 returns 0 and sets ERANGE below the smallest subnormal).
		// value = 0.DIGITS * 10^(pointPosition - firstNonZero + explicitExponent): positive means overflow.
		long long pointPos = -1, index = 0, firstNonZero = -1;
		const char *q = mantissaStart;
		for (; q < p && *q != 'e' && *q != 'E'; ++q)
		{
			if (*q == '.')
			{
				pointPos = index;
			}
			else
			{
				if (firstNonZero < 0 && *q != '0')
				{
					firstNonZero = index;
				}
				++index;
			}
		}
		if (pointPos < 0)
		{
			pointPos = index;
		}
		long long exponent = 0;
		if (q < p)
		{
			++q;
			const bool expNegative = *q == '-';
			if (*q == '+' || *q == '-')
			{
				++q;
			}
			for (; q < p && exponent < 100000000; ++q)
			{
				exponent = exponent * 10 + (*q - '0');
			}
			if (expNegative)
			{
				exponent = -exponent;
			}
		}
		value = (pointPos - (firstNonZero < 0 ? 0 : firstNonZero) + exponent > 0) ? HUGE_VAL : 0.0;
	}
	else if (res.ec != std::errc())
	{
		if (endptr)
		{
			*endptr = const_cast<char *>(s);
		}
		return 0.0;
	}
	if (endptr)
	{
		*endptr = const_cast<char *>(p);
	}
	return negative ? -value : value;
}

int luaEA_ftol(double n)
{
	if (!(n > -9223372036854775808.0 && n < 9223372036854775808.0))
	{
		return 0;
	}
	return (int)(std::int32_t)(std::uint32_t)((std::uint64_t)(std::int64_t)n & 0xFFFFFFFFu);
}

unsigned int luaEA_strtoul(const char *s, char **endptr, int base)
{
	const char *p = s;
	while (isSpace((unsigned char)*p))
	{
		++p;
	}
	bool negative = false;
	if (*p == '+' || *p == '-')
	{
		negative = *p == '-';
		++p;
	}
	if ((base == 16 || base == 0) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
	{
		const char c = p[2];
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (hex)
		{
			p += 2;
			base = 16;
		}
	}
	if (base == 0)
	{
		base = p[0] == '0' ? 8 : 10;
	}
	std::uint64_t value = 0;
	bool any = false, overflow = false;
	for (;; ++p)
	{
		const char c = *p;
		int digit;
		if (c >= '0' && c <= '9')
		{
			digit = c - '0';
		}
		else if (c >= 'a' && c <= 'z')
		{
			digit = c - 'a' + 10;
		}
		else if (c >= 'A' && c <= 'Z')
		{
			digit = c - 'A' + 10;
		}
		else
		{
			break;
		}
		if (digit >= base)
		{
			break;
		}
		any = true;
		if (!overflow)
		{
			value = value * (std::uint64_t)base + (std::uint64_t)digit;
			if (value > 0xFFFFFFFFull)
			{
				overflow = true;
			}
		}
	}
	if (endptr)
	{
		*endptr = const_cast<char *>(any ? p : s);
	}
	if (!any)
	{
		return 0;
	}
	if (overflow)
	{
		return 0xFFFFFFFFu; // ULONG_MAX (and ERANGE), negative or not
	}
	const std::uint32_t v = (std::uint32_t)value;
	return negative ? (std::uint32_t)(0u - v) : v;
}

} // extern "C"
