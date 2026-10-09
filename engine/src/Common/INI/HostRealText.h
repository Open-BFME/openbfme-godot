// OpenBFME. GPL-3.0.
//
// HostRealText (lane WIN-1): number text read without the host C runtime, so a Windows and a Linux build read the same bits.
//
// MEASURED (lane WIN-1): the host CRTs' strtof / sscanf("%f") differ from each other (Wine's UCRT is not correctly rounded on hard inputs, glibc is,
// and the edge forms "3e", "0x." and "nan" read differently), so the simulation calls neither:
//   * strtofPortable is retail's reading of a real: MSVCR71's sscanf("%f") ported instruction by instruction (Common/INI/Msvcr71Real.h). The INI
//     reals of the port's own field re-parsers go through it, like INI::scanReal (retail reads every such field with sscanf "%f").
//   * scanRealText / strtodPortable are the correctly rounded reading (the C++ library's std::from_chars, implemented in libc++, libstdc++ and the
//     MSVC STL themselves): for a double the port needs that retail does not read through the CRT.
#pragma once

#include "Common/INI/Msvcr71Real.h"

#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>

// Reads a real from the start of `text` the way strtod reads one (white space, sign, digits with one point, an exponent only when digits follow it,
// "inf" / "infinity"; no hex, no nan: neither is in MSVCR71's grammar) into `out` (float or double), CORRECTLY ROUNDED straight to T (no detour through
// another type): a value beyond T's range is +-infinity, one that rounds below T's smallest subnormal +-0. Returns the end of the number, or nullptr
// when there is none (`out` is then untouched).
template <typename T>
inline const char *scanRealText(const char *text, T &out)
{
	const char *p = text;
	while (std::isspace((unsigned char)*p))
	{
		++p;
	}
	const bool negative = *p == '-';
	if (*p == '+')
	{
		++p; // from_chars takes a '-' but no '+'
	}
	const char *number = p; // from here (with its '-') to the end goes to from_chars
	if (negative)
	{
		++p;
	}
	const T inf = std::numeric_limits<T>::infinity();
	if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') && (p[2] == 'f' || p[2] == 'F'))
	{
		p += 3;
		const char *rest = "inity";
		const char *q = p;
		while (*rest && std::tolower((unsigned char)*q) == *rest)
		{
			++q;
			++rest;
		}
		out = negative ? -inf : inf;
		return *rest ? p : q;
	}
	int digits = 0, leadExponent = 0;
	bool seenNonZero = false, pastPoint = false;
	for (;; ++p)
	{
		if (std::isdigit((unsigned char)*p))
		{
			++digits;
			if (*p != '0')
			{
				seenNonZero = true;
			}
			if (!pastPoint && seenNonZero)
			{
				++leadExponent; // integer digits from the first non-zero one
			}
			else if (pastPoint && !seenNonZero)
			{
				--leadExponent; // fraction zeros before the first non-zero digit
			}
		}
		else if (*p == '.' && !pastPoint)
		{
			pastPoint = true;
		}
		else
		{
			break;
		}
	}
	if (digits == 0)
	{
		return nullptr;
	}
	int exponent = 0;
	if (*p == 'e' || *p == 'E')
	{
		const char *q = p + 1;
		const bool expNegative = *q == '-';
		if (*q == '+' || *q == '-')
		{
			++q;
		}
		if (std::isdigit((unsigned char)*q))
		{
			for (; std::isdigit((unsigned char)*q); ++q)
			{
				if (exponent < 100000)
				{
					exponent = exponent * 10 + (*q - '0');
				}
			}
			if (expNegative)
			{
				exponent = -exponent;
			}
			p = q;
		}
	}
	T v = T(0);
	const std::from_chars_result r = std::from_chars(number, p, v, std::chars_format::general);
	if (r.ec == std::errc::result_out_of_range)
	{
		// the libraries report a result that overflows, or one that rounds to zero, as out of range (and leave v alone): the decimal exponent of the
		// leading digit tells which (a value of T's subnormal range is in range and converted above, never here)
		const bool overflow = seenNonZero && leadExponent + exponent > 0;
		out = overflow ? (negative ? -inf : inf) : (negative ? -T(0) : T(0));
		return p;
	}
	if (r.ec != std::errc() || r.ptr != p)
	{
		return nullptr; // not reached: the prefix above is in from_chars' grammar
	}
	out = v;
	return p;
}

// strtof as RETAIL reads a real (MSVCR71's sscanf "%f", Msvcr71Real.h): the value, *end after the number (or at `text` when there is none, the value 0)
inline float strtofPortable(const char *text, char **end)
{
	float v = 0.0f;
	const char *e = nullptr;
	const bool ok = Msvcr71Real::scanfFloat(text, v, &e);
	if (end)
	{
		*end = const_cast<char *>(ok ? e : text);
	}
	return ok ? v : 0.0f;
}

// strtod without the host C runtime, correctly rounded: the value, *end after the number (or at `text` when there is none, the value then 0)
inline double strtodPortable(const char *text, char **end)
{
	double v = 0.0;
	const char *e = scanRealText(text, v);
	if (end)
	{
		*end = const_cast<char *>(e ? e : text);
	}
	return e ? v : 0.0;
}
