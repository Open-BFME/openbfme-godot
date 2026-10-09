// OpenBFME. GPL-3.0. See Common/INI/Msvcr71Real.h for the target facts.
//
// Every routine below is the port of the msvcr71.dll function named in its comment, step for step: the 12-byte "_LDBL12" value is kept as the 12
// little-endian bytes the CRT keeps (bytes 0-9 the 80-bit mantissa with its leading 1 at bit 79, bytes 10-11 sign and biased exponent), and the
// dword / word accesses at the CRT's offsets are reproduced, wrap-arounds and lost carries included. Integer arithmetic only.

#include "Common/INI/Msvcr71Real.h"

#include <cstring>
#include <string>

namespace Msvcr71Real
{
namespace
{
struct Ld12Entry
{
	std::uint16_t lo16;
	std::uint32_t mid32;
	std::uint32_t hi32;
	std::uint16_t exponent;
};

#include "Common/INI/Msvcr71Pow10.inc"

struct Ld12
{
	std::uint8_t b[12] = {};

	std::uint32_t d32(int off) const
	{
		return (std::uint32_t)b[off] | ((std::uint32_t)b[off + 1] << 8) | ((std::uint32_t)b[off + 2] << 16) | ((std::uint32_t)b[off + 3] << 24);
	}
	void setD32(int off, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
		{
			b[off + i] = (std::uint8_t)(v >> (8 * i));
		}
	}
	std::uint16_t w16(int off) const { return (std::uint16_t)(b[off] | (b[off + 1] << 8)); }
	void setW16(int off, std::uint16_t v)
	{
		b[off] = (std::uint8_t)v;
		b[off + 1] = (std::uint8_t)(v >> 8);
	}
};

Ld12 fromEntry(const Ld12Entry &e)
{
	Ld12 x;
	x.setW16(0, e.lo16);
	x.setD32(2, e.mid32);
	x.setD32(6, e.hi32);
	x.setW16(10, e.exponent);
	return x;
}

// 0x7C372F35 __addl: *sum = a + b, returns the carry
std::uint32_t addl(std::uint32_t a, std::uint32_t b, std::uint32_t &sum)
{
	sum = a + b;
	return (sum < a || sum < b) ? 1u : 0u;
}

// 0x7C372F56 __add_12 on three dwords (a += b)
void add12(std::uint32_t a[3], const std::uint32_t b[3])
{
	if (addl(a[0], b[0], a[0]))
	{
		if (addl(a[1], 1, a[1]))
		{
			++a[2];
		}
	}
	if (addl(a[1], b[1], a[1]))
	{
		++a[2];
	}
	addl(a[2], b[2], a[2]);
}

// 0x7C372FB4 __shl_12 / 0x7C372FE2 __shr_12 by one bit
void shl12(std::uint32_t a[3])
{
	const std::uint32_t c0 = a[0] >> 31, c1 = a[1] >> 31;
	a[0] <<= 1;
	a[1] = (a[1] << 1) | c0;
	a[2] = (a[2] << 1) | c1;
}

void shr12(std::uint32_t a[3])
{
	const std::uint32_t c1 = a[1] << 31, c2 = a[2] << 31;
	a[1] = (a[1] >> 1) | c2;
	a[0] = (a[0] >> 1) | c1;
	a[2] >>= 1;
}

void toDwords(const Ld12 &x, std::uint32_t a[3])
{
	a[0] = x.d32(0);
	a[1] = x.d32(4);
	a[2] = x.d32(8);
}

void fromDwords(Ld12 &x, const std::uint32_t a[3])
{
	x.setD32(0, a[0]);
	x.setD32(4, a[1]);
	x.setD32(8, a[2]);
}

// 0x7C37300F __mtold12: the decimal digits (values, not characters) to a normalised 12-byte value
Ld12 mtold12(const std::uint8_t *digits, unsigned n)
{
	std::uint32_t a[3] = { 0, 0, 0 };
	std::uint16_t exponent = 0x404E;
	for (unsigned i = 0; i < n; ++i)
	{
		const std::uint32_t t[3] = { a[0], a[1], a[2] };
		shl12(a);
		shl12(a);
		add12(a, t);
		shl12(a);
		const std::uint32_t d[3] = { (std::uint32_t)(std::int32_t)(std::int8_t)digits[i], 0, 0 };
		add12(a, d);
	}
	if (a[2] == 0)
	{
		std::uint32_t top;
		do
		{
			const std::uint32_t mid = a[1], low = a[0];
			exponent = (std::uint16_t)(exponent + 0xFFF0);
			top = mid >> 16;
			a[1] = (low >> 16) | (mid << 16);
			a[0] = low << 16;
		} while (top == 0);
		a[2] = top;
	}
	while (!(a[2] & 0x8000u))
	{
		shl12(a);
		exponent = (std::uint16_t)(exponent + 0xFFFF);
	}
	Ld12 x;
	fromDwords(x, a);
	x.setW16(10, exponent);
	return x;
}

void setInfinity(Ld12 &x, std::uint16_t sign)
{
	x.setD32(4, 0);
	x.setD32(0, 0);
	x.setD32(8, sign ? 0xFFFF8000u : 0x7FFF8000u);
}

// 0x7C3730ED __ld12mul: x *= y
void ld12mul(Ld12 &x, const Ld12 &y)
{
	const std::uint16_t xe = x.w16(10), ye = y.w16(10);
	const std::uint16_t sign = (std::uint16_t)((xe ^ ye) & 0x8000);
	const std::uint16_t ex = xe & 0x7FFF, ey = ye & 0x7FFF;
	std::uint16_t e = (std::uint16_t)(ex + ey);
	if (ex >= 0x7FFF || ey >= 0x7FFF || e > 0xBFFD)
	{
		setInfinity(x, sign);
		return;
	}
	if (e <= 0x3FBF)
	{
		x.setD32(8, 0);
		x.setD32(4, 0);
		x.setD32(0, 0);
		return;
	}
	if (ex == 0)
	{
		++e;
		if ((x.d32(8) & 0x7FFFFFFFu) == 0 && x.d32(4) == 0 && x.d32(0) == 0)
		{
			x.setW16(10, 0);
			return;
		}
	}
	if (ey == 0)
	{
		++e;
		if ((y.d32(8) & 0x7FFFFFFFu) == 0 && y.d32(4) == 0 && y.d32(0) == 0)
		{
			x.setD32(8, 0);
			x.setD32(4, 0);
			x.setD32(0, 0);
			return;
		}
	}
	// the truncated partial products: word a of x times word b of y with a + b >= 4, summed into the dword at byte 2i of r (i = a + b - 4); a carry
	// out of that dword increments the 16-bit word just above it only (it wraps, nothing propagates further). i = 4 writes its carry beyond r (the
	// caller's stack slot in the CRT): the top partial sum stays below 2^32, so it never carries.
	Ld12 r;
	std::uint8_t spill[2] = { 0, 0 };
	for (int i = 0; i < 5; ++i)
	{
		for (int j = 0; j < 5 - i; ++j)
		{
			const std::uint32_t prod = (std::uint32_t)x.w16(2 * (i + j)) * (std::uint32_t)y.w16(2 * (4 - j));
			std::uint32_t sum;
			if (addl(r.d32(2 * i), prod, sum))
			{
				if (2 * i + 4 < 12)
				{
					r.setW16(2 * i + 4, (std::uint16_t)(r.w16(2 * i + 4) + 1));
				}
				else
				{
					++spill[0];
				}
			}
			r.setD32(2 * i, sum);
		}
	}
	e = (std::uint16_t)(e + 0xC002);
	std::uint32_t a[3];
	toDwords(r, a);
	bool round = false;
	if ((std::int16_t)e > 0)
	{
		while (!(a[2] & 0x80000000u) && (std::int16_t)e > 0)
		{
			shl12(a);
			e = (std::uint16_t)(e + 0xFFFF);
		}
		round = (std::int16_t)e > 0;
	}
	if (!round)
	{
		e = (std::uint16_t)(e + 0xFFFF);
		if ((std::int16_t)e < 0)
		{
			std::uint16_t n = (std::uint16_t)(0u - (std::uint32_t)e);
			e = (std::uint16_t)(e + n);
			bool sticky = false;
			for (unsigned k = 0; k < n && k < 100u; ++k) // after 96 shifts r is zero and no further bit can be set
			{
				sticky = sticky || (a[0] & 1u);
				shr12(a);
			}
			if (sticky)
			{
				a[0] |= 1u;
			}
		}
	}
	fromDwords(r, a);
	if (r.w16(0) > 0x8000 || (r.d32(0) & 0x1FFFFu) == 0x18000u)
	{
		if (r.d32(2) == 0xFFFFFFFFu)
		{
			r.setD32(2, 0);
			if (r.d32(6) == 0xFFFFFFFFu)
			{
				r.setD32(6, 0);
				if (r.w16(10) == 0xFFFF)
				{
					++e;
					r.setW16(10, 0x8000);
				}
				else
				{
					r.setW16(10, (std::uint16_t)(r.w16(10) + 1));
				}
			}
			else
			{
				r.setD32(6, r.d32(6) + 1);
			}
		}
		else
		{
			r.setD32(2, r.d32(2) + 1);
		}
	}
	if (e >= 0x7FFF)
	{
		setInfinity(x, sign);
		return;
	}
	x.setW16(0, r.w16(2));
	x.setD32(2, r.d32(4));
	x.setD32(6, r.d32(8));
	x.setW16(10, (std::uint16_t)(e | sign));
}

// 0x7C37331E __multtenpow12(x, exponent, 0)
void multtenpow12(Ld12 &x, int exponent)
{
	if (exponent == 0)
	{
		return;
	}
	const Ld12Entry(*table)[7] = kPow10Pos;
	if (exponent < 0)
	{
		exponent = -exponent;
		table = kPow10Neg;
	}
	x.setW16(0, 0); // the caller's flag is 0: the 16 guard bits of the parsed value are cleared
	for (int group = 0; exponent != 0; ++group)
	{
		const int d = exponent & 7;
		exponent >>= 3;
		if (d != 0)
		{
			ld12mul(x, fromEntry(table[group][d - 1])); // the table entries as __multtenpow12 uses them (gen_msvcr71_pow10.py)
		}
	}
}

// _ld12cvt's mantissa helpers on three dwords, man[0] the most significant (bit positions count from its bit 31)
bool zeroTail(const std::uint32_t man[3], int pos) // 0x7C372523
{
	const int word = pos / 32;
	const std::uint32_t mask = ~(0xFFFFFFFFu << (31 - pos % 32));
	if (man[word] & mask)
	{
		return false;
	}
	for (int w = word + 1; w < 3; ++w)
	{
		if (man[w] != 0)
		{
			return false;
		}
	}
	return true;
}

std::uint32_t incMan(std::uint32_t man[3], int nbit) // 0x7C372555
{
	int word = nbit / 32;
	std::uint32_t carry = addl(man[word], 1u << (31 - nbit % 32), man[word]);
	for (--word; word >= 0 && carry; --word)
	{
		carry = addl(man[word], 1u, man[word]);
	}
	return carry;
}

std::uint32_t roundMan(std::uint32_t man[3], int precision) // 0x7C3725A2
{
	const int nbit = precision - 1;
	const int word = (nbit + 1) / 32;
	const int shift = 31 - (nbit + 1) % 32;
	std::uint32_t carry = 0;
	if (man[word] & (1u << shift))
	{
		if (!zeroTail(man, nbit + 1))
		{
			carry = incMan(man, nbit);
		}
	}
	man[word] &= 0xFFFFFFFFu << shift;
	for (int w = word + 1; w < 3; ++w)
	{
		man[w] = 0;
	}
	return carry;
}

void shrMan(std::uint32_t man[3], int n) // 0x7C372648
{
	const int words = n / 32, bits = n % 32;
	const std::uint32_t lowMask = bits ? ~(0xFFFFFFFFu << bits) : 0u;
	std::uint32_t carry = 0;
	for (int i = 0; i < 3; ++i)
	{
		const std::uint32_t v = man[i];
		man[i] = (v >> bits) | carry;
		carry = bits ? (v & lowMask) << (32 - bits) : 0u;
	}
	for (int i = 2; i >= 0; --i)
	{
		man[i] = i >= words ? man[i - words] : 0u;
	}
}

// 0x7C3726C3 _ld12cvt with the float descriptor { max_exp 128, min_exp -127, precision 24, exp_width 8, format_width 32, bias 127 }
std::uint32_t ld12tof(const Ld12 &x)
{
	const int maxExp = 128, minExp = -127, precision = 24, expWidth = 8, bias = 127;
	const std::uint16_t ew = x.w16(10);
	const std::uint32_t sign = (ew & 0x8000) ? 0x80000000u : 0u;
	std::uint32_t man[3] = { x.d32(6), x.d32(2), (std::uint32_t)x.w16(0) << 16 };
	int exponent = (int)(ew & 0x7FFF) - 0x3FFF;
	std::uint32_t biased = 0;
	if (exponent == -0x3FFF)
	{
		man[0] = man[1] = man[2] = 0; // zero, or the CRT's "denormal 12-byte value": both give zero
	}
	else
	{
		const std::uint32_t saved[3] = { man[0], man[1], man[2] };
		if (roundMan(man, precision))
		{
			++exponent;
		}
		if (exponent < minExp - precision)
		{
			man[0] = man[1] = man[2] = 0;
		}
		else if (exponent <= minExp)
		{
			man[0] = saved[0];
			man[1] = saved[1];
			man[2] = saved[2];
			shrMan(man, minExp - exponent);
			roundMan(man, precision); // a carry out of the top is lost here (CRT behaviour)
			shrMan(man, expWidth + 1);
		}
		else if (exponent >= maxExp)
		{
			man[0] = 0x80000000u;
			man[1] = man[2] = 0;
			shrMan(man, expWidth);
			biased = (std::uint32_t)(bias + maxExp);
		}
		else
		{
			biased = (std::uint32_t)(bias + exponent);
			man[0] &= 0x7FFFFFFFu;
			shrMan(man, expWidth);
		}
	}
	return (biased << (31 - expWidth)) | sign | man[0];
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
} // namespace

// 0x7C3720A2 __strgtold12 on the text _input collected, then 0x7C372831 _ld12tof
std::uint32_t atofltBits(const char *s)
{
	std::uint16_t sign = 0;
	if (*s == '-')
	{
		sign = 0x8000;
		++s;
	}
	else if (*s == '+')
	{
		++s;
	}
	std::uint8_t man[25];
	unsigned manlen = 0;
	int exponent = 0;
	while (*s == '0')
	{
		++s;
	}
	for (; isDigit(*s); ++s)
	{
		if (manlen < 25)
		{
			man[manlen++] = (std::uint8_t)(*s - '0');
		}
		else
		{
			++exponent;
		}
	}
	if (*s == '.')
	{
		++s;
		if (manlen == 0)
		{
			for (; *s == '0'; ++s)
			{
				--exponent;
			}
		}
		for (; isDigit(*s); ++s)
		{
			if (manlen < 25)
			{
				man[manlen++] = (std::uint8_t)(*s - '0');
				--exponent;
			}
		}
	}
	int expValue = 0, expSign = 1;
	if (*s == 'e' || *s == 'E' || *s == 'd' || *s == 'D')
	{
		++s;
		if (*s == '-')
		{
			expSign = -1;
			++s;
		}
		else if (*s == '+')
		{
			++s;
		}
		for (; isDigit(*s); ++s)
		{
			expValue = expValue * 10 + (*s - '0');
			if (expValue > 5200)
			{
				expValue = 5201;
				break;
			}
		}
	}
	Ld12 x;
	if (manlen > 0)
	{
		if (manlen > 24)
		{
			if (man[23] >= 5)
			{
				++man[23]; // the CRT bumps the 24th digit by its OWN value (may make it 10) and drops the 25th
			}
			manlen = 24;
			++exponent;
		}
		while (man[manlen - 1] == 0)
		{
			--manlen;
			++exponent;
		}
		x = mtold12(man, manlen);
		const int total = expSign * expValue + exponent;
		if (total > 5200)
		{
			x = Ld12();
			x.setD32(6, 0x80000000u);
			x.setW16(10, 0x7FFF);
		}
		else if (total < -5200)
		{
			x = Ld12();
		}
		else
		{
			multtenpow12(x, total);
		}
	}
	x.setW16(10, (std::uint16_t)(x.w16(10) | sign));
	return ld12tof(x);
}

void powerTableEntry(bool negative, int group, int digit, std::uint8_t out[12])
{
	const Ld12 x = fromEntry((negative ? kPow10Neg : kPow10Pos)[group][digit - 1]);
	std::memcpy(out, x.b, 12);
}

// sscanf(text, "%f") of msvcr71: _input's float scanner (0x7C36D7CF-0x7C36D9D4), then _fassign -> _atoflt
bool scanfFloat(const char *text, float &out, const char **end)
{
	const char *p = text;
	while (isSpace(*p))
	{
		++p;
	}
	std::string buf;
	if (*p == '-')
	{
		buf += '-';
		++p;
	}
	else if (*p == '+')
	{
		++p;
	}
	int width = 349; // no field width given: 0x15D
	int digits = 0;
	auto take = [&width]() { return width-- > 0; };
	while (isDigit(*p) && take())
	{
		buf += *p++;
		++digits;
	}
	if (*p == '.' && take())
	{
		buf += *p++;
		while (isDigit(*p) && take())
		{
			buf += *p++;
			++digits;
		}
	}
	bool ok = digits > 0;
	if (ok && (*p == 'e' || *p == 'E') && take())
	{
		buf += 'e';
		++p;
		if (*p == '-')
		{
			buf += '-';
		}
		if (*p == '-' || *p == '+')
		{
			if (take())
			{
				++p;
			}
			else
			{
				width = 0;
			}
		}
		while (isDigit(*p) && take()) // an exponent marker without digits still converts ("3e" reads 3: __strgtold12 ignores the empty exponent)
		{
			buf += *p++;
		}
	}
	if (end)
	{
		*end = p;
	}
	if (!ok)
	{
		return false;
	}
	const std::uint32_t bits = atofltBits(buf.c_str());
	std::memcpy(&out, &bits, sizeof out);
	return true;
}

} // namespace Msvcr71Real
