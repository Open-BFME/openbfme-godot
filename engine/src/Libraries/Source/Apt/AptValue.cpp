// OpenBFME. GPL-3.0.
// See AptValue.h for the citations.

#include "Libraries/Source/Apt/AptValue.h"

#include "Common/NumericState.h"

#include "Libraries/Source/Apt/AptObject.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{

const std::string &emptyString()
{
	static const std::string e;
	return e;
}

bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

int hexDigit(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return -1;
}

std::int32_t saturate(bool negative, unsigned long long magnitude)
{
	if (negative)
	{
		return magnitude > 2147483648ull ? std::numeric_limits<std::int32_t>::min() : (std::int32_t)(-(long long)magnitude);
	}
	return magnitude > 2147483647ull ? std::numeric_limits<std::int32_t>::max() : (std::int32_t)magnitude;
}

bool hasHexPrefix(const std::string &s)
{
	// AptValueToInteger.cpp: m_length > 2 && text[0] == '0' && text[1] == 'x'
	return s.size() > 2 && s[0] == '0' && s[1] == 'x';
}

} // namespace

// ---- construction ---------------------------------------------------------------------------

AptValue AptValue::boolean(bool b)
{
	AptValue v;
	v.m_type = AptValueType::Boolean;
	v.m_int = 0;
	v.m_bool = b;
	return v;
}

AptValue AptValue::integer(std::int32_t i)
{
	AptValue v;
	v.m_type = AptValueType::Integer;
	v.m_int = i;
	return v;
}

AptValue AptValue::number(float f)
{
	AptValue v;
	v.m_type = AptValueType::Float;
	v.m_float = f;
	return v;
}

AptValue AptValue::string(std::string s)
{
	AptValue v;
	v.m_type = AptValueType::String;
	v.m_str = std::make_shared<const std::string>(std::move(s));
	return v;
}

AptValue AptValue::object(AptObject *o)
{
	AptValue v;
	v.m_type = AptValueType::Object;
	v.m_obj = o;
	return v;
}

AptValue AptValue::externValue()
{
	AptValue v;
	v.m_type = AptValueType::Extern;
	return v;
}

const std::string &AptValue::asString() const
{
	return m_str ? *m_str : emptyString();
}

// ---- C runtime helpers ----------------------------------------------------------------------

float AptAtof(const std::string &text)
{
	return (float)AptAtofDouble(text);
}

namespace
{
thread_local std::string g_numericStop;

// The exact decimal digits of DBL_MIN = 2^-1022 (leading digit first, trailing zeros removed): 2.2250738585072013830...e-308.
// S-017 compares the TEXT's exact magnitude with it, not the engine's rounded result.
const char *const kDblMinDigits =
	"2225073858507201383090232717332404064219215980462331830553327416887204434813918195854283159012511020"
	"5640673397310358110051524341615534601088560123853777188211307779935320023304796101474425836360719215"
	"6504694250373420837525080665061665815894872049117996859163964850063590877011830487479978088775374994"
	"9451580451605050915399856582470818645113537935804992115981085766051992433352114352390148795699609591"
	"2888916029926415110634663133936634775865130293717620473256317814856643508721228286376420448468114076"
	"1391147706280168985324411002416144742161856716615054015428508471675290190316132277889672970737312333"
	"4086988983175067838846926092773977972858659654941091369095406136467568702398678315290680984617210924"
	"625396728515625";
const long long kDblMinExponent = -308;

struct DecimalMagnitude
{
	std::string digits;     // significant digits, no leading zeros, trailing zeros removed
	long long exponent = 0; // scientific exponent of the first digit
	bool zero = true;
};

// The decimal in `buf` (digits, optional '.', optional exponent; no sign).
DecimalMagnitude parseDecimalMagnitude(const std::string &buf)
{
	DecimalMagnitude m;
	std::size_t i = 0;
	long long intDigits = 0;
	long long index = 0;
	long long firstNonZero = -1;
	bool seenPoint = false;
	std::string all;
	for (; i < buf.size(); ++i)
	{
		char c = buf[i];
		if (c == '.')
		{
			seenPoint = true;
			continue;
		}
		if (c < '0' || c > '9')
		{
			break;
		}
		if (c != '0' && firstNonZero < 0)
		{
			firstNonZero = index;
		}
		if (firstNonZero >= 0)
		{
			all.push_back(c);
		}
		++index;
		if (!seenPoint)
		{
			++intDigits;
		}
	}
	if (firstNonZero < 0)
	{
		return m;
	}
	long long exponent = 0;
	if (i < buf.size() && (buf[i] == 'e' || buf[i] == 'E'))
	{
		bool negative = false;
		++i;
		if (i < buf.size() && (buf[i] == '+' || buf[i] == '-'))
		{
			negative = buf[i] == '-';
			++i;
		}
		while (i < buf.size() && buf[i] >= '0' && buf[i] <= '9')
		{
			exponent = exponent * 10 + (buf[i] - '0');
			if (exponent > 1000000000LL)
			{
				exponent = 1000000000LL;
			}
			++i;
		}
		if (negative)
		{
			exponent = -exponent;
		}
	}
	while (!all.empty() && all.back() == '0')
	{
		all.pop_back();
	}
	m.zero = false;
	m.digits = all;
	m.exponent = intDigits - firstNonZero - 1 + exponent; // the value is d.ddd * 10^exponent
	return m;
}

// -1, 0, +1: the decimal's exact value against DBL_MIN.
int compareToDblMin(const DecimalMagnitude &m)
{
	if (m.exponent != kDblMinExponent)
	{
		return m.exponent < kDblMinExponent ? -1 : 1;
	}
	const std::string min = kDblMinDigits;
	const std::size_t n = std::min(m.digits.size(), min.size());
	for (std::size_t k = 0; k < n; ++k)
	{
		if (m.digits[k] != min[k])
		{
			return m.digits[k] < min[k] ? -1 : 1;
		}
	}
	if (m.digits.size() == min.size())
	{
		return 0;
	}
	return m.digits.size() < min.size() ? -1 : 1;
}

// True when the text denotes a value from 2e-324 (just below the rounding boundary of the smallest subnormal double,
// 2^-1075 = 2.47e-324) up to, not including, DBL_MIN: the range where retail MSVCR71 and the engine are known to differ
// (S-017).  It is decided from the exact value of the TEXT, so it also covers texts the engine rounds to zero or up to
// DBL_MIN.
bool inSubnormalBand(const DecimalMagnitude &m)
{
	if (m.zero)
	{
		return false;
	}
	const bool atLeastFloor = m.exponent > -324 || (m.exponent == -324 && m.digits[0] >= '2');
	return atLeastFloor && compareToDblMin(m) < 0;
}
} // namespace

double AptAtofDouble(const std::string &text)
{
	// MSVC 7.1 atof: whitespace, sign, digits, '.', digits, exponent; no hex, no inf/nan.
	std::size_t i = 0;
	while (i < text.size() && isSpace(text[i]))
	{
		++i;
	}
	std::string buf;
	if (i < text.size() && (text[i] == '+' || text[i] == '-'))
	{
		buf.push_back(text[i++]);
	}
	std::size_t digits = 0;
	while (i < text.size() && isDigit(text[i]))
	{
		buf.push_back(text[i++]);
		++digits;
	}
	if (i < text.size() && text[i] == '.')
	{
		buf.push_back(text[i++]);
		while (i < text.size() && isDigit(text[i]))
		{
			buf.push_back(text[i++]);
			++digits;
		}
	}
	if (digits == 0)
	{
		return 0.0;
	}
	if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
	{
		std::size_t j = i + 1;
		std::string exp = "e";
		if (j < text.size() && (text[j] == '+' || text[j] == '-'))
		{
			exp.push_back(text[j++]);
		}
		std::size_t expDigits = 0;
		while (j < text.size() && isDigit(text[j]))
		{
			exp.push_back(text[j++]);
			++expDigits;
		}
		if (expDigits)
		{
			buf += exp;
		}
	}
	// std::from_chars is locale independent (strtod would honour a decimal comma) and correctly rounded.
	const char *begin = buf.c_str();
	bool negative = false;
	if (*begin == '+' || *begin == '-')
	{
		negative = *begin == '-';
		++begin;
	}
	// S-017 is decided from the exact value of the text, before any rounding
	if (g_numericStop.empty() && inSubnormalBand(parseDecimalMagnitude(std::string(begin))))
	{
		g_numericStop = "atof(\"" + text.substr(0, 40) + "\") denotes a value in the subnormal double range [2e-324, DBL_MIN): retail MSVCR71 "
						"returns twice the value for exact powers of two there, rounds ties up and rounds up to DBL_MIN differently, and the engine "
						"rounds correctly (acceptance stop S-017)";
	}
	double value = 0.0;
	auto res = std::from_chars(begin, buf.c_str() + buf.size(), value);
	if (res.ec == std::errc::result_out_of_range)
	{
		// atof: HUGE_VAL on overflow, 0 on underflow.  Which one is decided by the MAGNITUDE of the value the text
		// denotes (its decimal order), not by the text: "0.000...1" with 324 zeros is 0 and 350 digits followed by
		// "e-1" is +infinity (retail ToNumber 0x00ADD460, checked through the retail oracle)
		const DecimalMagnitude magnitudeOfText = parseDecimalMagnitude(std::string(begin));
		double magnitude = (magnitudeOfText.zero || magnitudeOfText.exponent < 0) ? 0.0 : std::numeric_limits<double>::infinity();
		return negative ? -magnitude : magnitude;
	}
	if (res.ec != std::errc())
	{
		return 0.0;
	}
	return negative ? -value : value;
}

bool AptNumericStopPending()
{
	return !g_numericStop.empty();
}

std::string AptTakeNumericStop()
{
	std::string s;
	s.swap(g_numericStop);
	return s;
}

std::int32_t AptAtoi(const std::string &text)
{
	std::size_t i = 0;
	while (i < text.size() && isSpace(text[i]))
	{
		++i;
	}
	bool neg = false;
	if (i < text.size() && (text[i] == '+' || text[i] == '-'))
	{
		neg = text[i] == '-';
		++i;
	}
	// MSVCR71 atoi (atox.c): `total = 10 * total + digit` in a 32-bit long, so it WRAPS: "4294967296" is 0 and
	// "2147483648" is -2147483648 (retail 0x00A29838, checked against the real handlers by the retail oracle).
	// Only strtol (the hex path below) saturates.
	std::uint32_t total = 0;
	while (i < text.size() && isDigit(text[i]))
	{
		total = total * 10u + (std::uint32_t)(text[i] - '0');
		++i;
	}
	return (std::int32_t)(neg ? 0u - total : total);
}

std::int32_t AptStrtolHex(const std::string &text)
{
	std::size_t i = 0;
	while (i < text.size() && isSpace(text[i]))
	{
		++i;
	}
	bool neg = false;
	if (i < text.size() && (text[i] == '+' || text[i] == '-'))
	{
		neg = text[i] == '-';
		++i;
	}
	if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X') && i + 2 < text.size() && hexDigit(text[i + 2]) >= 0)
	{
		i += 2;
	}
	unsigned long long mag = 0;
	while (i < text.size() && hexDigit(text[i]) >= 0)
	{
		if (mag < (1ull << 40))
		{
			mag = mag * 16 + (unsigned)hexDigit(text[i]);
		}
		++i;
	}
	return saturate(neg, mag);
}

std::int32_t AptFloatToInt(float f)
{
	// BFME2 0x00A29228: fistp qword rounds to nearest, then the routine corrects the result to truncation; the
	// 64-bit "integer indefinite" (NaN, infinity, |f| >= 2^63) has a zero low word.  The low word is returned.
	if (std::isnan(f) || std::fabs(f) >= 9223372036854775808.0f)
	{
		return 0;
	}
	const std::int64_t t = (std::int64_t)f; // exact: truncation toward zero of an in-range float
	return (std::int32_t)(std::uint32_t)((std::uint64_t)t & 0xFFFFFFFFull);
}

std::string AptFloatToString(float f)
{
	if (std::isnan(f))
	{
		return "NaN";
	}
	if (std::isinf(f))
	{
		return f > 0 ? "Infinity" : "-Infinity";
	}
	char buf[512];
	// Rva008985C0ValueString.cpp: fmod(f, g_bfmeSubB3) == 0 -> "%d" of (int)f, else "%f".  g_bfmeSubB3
	// is the 1.0 whole-number test (UNVERIFIED value: the constant lives in game.dat's data segment).
	if (std::fmod((double)f, 1.0) == 0.0)
	{
		std::snprintf(buf, sizeof(buf), "%d", AptFloatToInt(f));
	}
	else
	{
		std::snprintf(buf, sizeof(buf), "%f", (double)f);
	}
	return buf;
}

// ---- coercions ------------------------------------------------------------------------------

float AptValue::toNumber() const
{
	switch (m_type)
	{
		case AptValueType::Undefined:
			return 0.0f; // isUndefined -> BfmeZeroRange
		case AptValueType::String:
			return AptAtof(asString());
		case AptValueType::Boolean:
			return m_bool ? 1.0f : 0.0f; // g_bfmeDefaultBU is 1.0f
		case AptValueType::Integer:
			return (float)m_int;
		case AptValueType::Float:
			return m_float;
		case AptValueType::Object:
		case AptValueType::Extern:
			return 1.0f; // default arm: (this != g_bfmeFallbackDB) ? g_bfmeDefaultBU : BfmeZeroRange
	}
	return 0.0f;
}

double AptValue::toNumberWide() const
{
	switch (m_type)
	{
		case AptValueType::String:
			return AptAtofDouble(asString());
		case AptValueType::Integer:
			return (double)m_int; // fild: exact
		default:
			return (double)toNumber();
	}
}

std::int32_t AptValue::toInteger() const
{
	switch (m_type)
	{
		case AptValueType::Undefined:
			return 0;
		case AptValueType::String:
		{
			const std::string &s = asString();
			return hasHexPrefix(s) ? AptStrtolHex(s) : AptAtoi(s);
		}
		case AptValueType::Boolean:
			return m_bool ? 1 : 0;
		case AptValueType::Integer:
			return m_int;
		case AptValueType::Float:
			return AptFloatToInt(m_float);
		case AptValueType::Object:
		case AptValueType::Extern:
			return 1; // default arm: this != gpNullValue (the undefined singleton)
	}
	return 0;
}

bool AptValue::toBoolean(std::uint32_t swfVersion) const
{
	switch (m_type)
	{
		case AptValueType::Undefined:
			return false;
		case AptValueType::Boolean:
			return m_bool;
		case AptValueType::Integer:
			return m_int != 0;
		case AptValueType::Float:
			return m_float != 0.0f;
		case AptValueType::String:
		{
			const std::string &s = asString();
			if (swfVersion == 7)
			{
				return !s.empty(); // string != the default (empty) string block
			}
			if (hasHexPrefix(s))
			{
				return AptStrtolHex(s) != 0;
			}
			return AptAtof(s) != 0.0f;
		}
		case AptValueType::Object:
		case AptValueType::Extern:
			return true;
	}
	return false;
}

std::string AptValue::toString() const
{
	switch (m_type)
	{
		case AptValueType::Undefined:
			return std::string();
		case AptValueType::Boolean:
			return m_bool ? "true" : "false";
		case AptValueType::Integer:
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%d", m_int);
			return buf;
		}
		case AptValueType::Float:
			return AptFloatToString(m_float);
		case AptValueType::String:
			return asString();
		case AptValueType::Object:
			return m_obj ? m_obj->displayString() : std::string("[object Object]");
		case AptValueType::Extern:
			return "[Extern]";
	}
	return std::string();
}

const char *AptValue::typeOf() const
{
	switch (m_type)
	{
		case AptValueType::Undefined:
			return "undefined";
		case AptValueType::Boolean:
			return "boolean";
		case AptValueType::Integer:
		case AptValueType::Float:
			return "number";
		case AptValueType::String:
			return "string";
		case AptValueType::Extern:
			return ""; // BFME1 Rva008C8350TypeOf.cpp:86-112 has no branch for type 11: the result string stays empty
		case AptValueType::Object:
			if (!m_obj)
			{
				return "object";
			}
			switch (m_obj->kind())
			{
				case AptObjectKind::Function:
					return "function";
				case AptObjectKind::Clip:
					return "movieclip";
				default:
					return "object";
			}
	}
	return "undefined";
}

// ---- operations -----------------------------------------------------------------------------

namespace AptOps
{

namespace
{

bool v7Undefined(const AptValue &a, const AptValue &b, std::uint32_t swfVersion)
{
	return swfVersion == 7 && (a.isUndefined() || b.isUndefined());
}

bool strictlyInteger(const AptValue &v)
{
	return v.isInteger();
}

} // namespace

AptValue add2(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// AddValues008C8500.cpp
	if (top.isString() || under.isString())
	{
		// SWF 7: an undefined operand becomes the string "undefined" (g_stringBlock01338724+1, the
		// same block ActionTypeOf appends for undefined values).  Older versions use getName: "".
		std::string left = (swfVersion == 7 && under.isUndefined()) ? std::string("undefined") : under.toString();
		std::string right = (swfVersion == 7 && top.isUndefined()) ? std::string("undefined") : top.toString();
		return AptValue::string(left + right);
	}
	if ((strictlyInteger(top) || strictlyInteger(under)) && !top.isFloat() && !under.isFloat())
	{
		if (v7Undefined(under, top, swfVersion))
		{
			return AptValue::undefined();
		}
		// AptInteger::Create(left + right): 32-bit wrap-around like the retail int add
		std::uint32_t sum = (std::uint32_t)under.toInteger() + (std::uint32_t)top.toInteger();
		return AptValue::integer((std::int32_t)sum);
	}
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	// BFME2 0x00B02B60: float path `fld; fadd; fstp dword` under the PC24 control word (setFPMode 0x00440EA9..0x00440EC5)
	return AptValue::number(NumericState::pc24Add(under.toNumber(), top.toNumber()));
}

AptValue subtract(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// AptActionInterpreterSubtract.cpp (0x008C6EE0)
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	// BFME2 0x00B00880 (target; the BFME1 donor 0x008C6EE0 always makes a float): two integers subtract as
	// integers (0x00B00928..0x00B0094E: toInteger, `sub eax, esi`, AptInteger::Create); otherwise
	// `fld under; fsub top; fstp dword` at PC24 (0x00B00952..0x00B00967)
	if (under.isInteger() && top.isInteger())
	{
		return AptValue::integer((std::int32_t)((std::uint32_t)under.asInteger() - (std::uint32_t)top.asInteger()));
	}
	// the TOP operand is stored as float (fstp dword [esp+0x14]); the UNDER operand stays wide in ST0 (an exact
	// integer or a string's double) and is subtracted from directly (0x00B00957..0x00B00962)
	return AptValue::number(NumericState::pc24SubD(under.toNumberWide(), (double)top.toNumber()));
}

AptValue multiply(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// AptActionInterpreterDivide.cpp _FunctionRva008C6FA0 (0x008C6FA0)
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	// BFME2 0x00B009E0 (target; the BFME1 donor 0x008C6FA0 always makes a float): two integers multiply as
	// integers (0x00B00A74..0x00B00AAA: `imul eax, esi`, AptInteger::Create); otherwise `fld; fmul; fstp dword` at
	// PC24 (0x00B00AB3..0x00B00AC8)
	if (under.isInteger() && top.isInteger())
	{
		return AptValue::integer((std::int32_t)((std::uint32_t)under.asInteger() * (std::uint32_t)top.asInteger()));
	}
	// top stored as float, under wide in ST0 (0x00B00AB8..0x00B00AC3)
	return AptValue::number(NumericState::pc24MulD(under.toNumberWide(), (double)top.toNumber()));
}

AptValue divide(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// AptActionInterpreterDivide.cpp _FunctionRva008C7060: division by zero yields the fallback value
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	float divisor = top.toNumber();
	if (divisor == 0.0f)
	{
		return AptValue::undefined();
	}
	// BFME2 0x00B00B40: `fld under; fdiv top; fstp dword` at PC24 (0x00B00BD4..0x00B00C06); always a float
	// divisor (top) stored as float, dividend (under) wide in ST0 (0x00B00BD9..0x00B00C01)
	return AptValue::number(NumericState::pc24DivD(under.toNumberWide(), (double)divisor));
}

AptValue modulo(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// Remainder008C8160.cpp: fmod(under, top) as float; zero divisor -> fallback
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	float divisor = top.toNumber();
	if (divisor == 0.0f)
	{
		return AptValue::undefined();
	}
	// BFME2 0x00B02600: top stored as float (0x00B0269A), under wide in ST0 into the CRT fmod (0x00B026C8), the
	// exact remainder stored as float
	// The retail CRT fmod (0x00A29A36, MSVCR71) differs from C's in two ways, observed by running the real handler
	// through the retail oracle: an infinite divisor gives the default NaN (0xFFC00000) instead of the dividend, and a
	// zero dividend gives +0 whatever its sign (-0 mod 2 is +0, while -4 mod 2 is -0 as in C).
	const double dividend = under.toNumberWide();
	if (std::isinf(divisor))
	{
		const std::uint32_t defaultNaN = 0xFFC00000u;
		float nan;
		std::memcpy(&nan, &defaultNaN, sizeof(nan));
		return AptValue::number(nan);
	}
	if (dividend == 0.0 && !std::isnan(divisor)) // a NaN divisor still gives NaN
	{
		return AptValue::number(0.0f);
	}
	return AptValue::number((float)std::fmod(dividend, (double)divisor));
}

AptValue less2(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// BFME2 0x00B02F20 (target; BFME1 donor LessThan008C8840.cpp).  Operands: edi = top, ebx = under.
	//   SWF 7 and an undefined operand           -> the undefined singleton (0x00B02FA7)
	//   both strings                             -> strcmp(under, top) < 0 (0x00B03020..0x00B03086)
	//   either operand non-numeric (0x00AFC370)  -> the undefined singleton (0x00B03174)
	//   either operand a float                   -> under is stored as FLOAT (fstp dword [esp+0x10], 0x00B030DD), top
	//                                               stays wide in ST0 (0x00B030E8); true when top > under (fcomp, then
	//                                               `test ah,0x41`: below, equal and unordered are false)
	//   otherwise                                -> toInteger(under) < toInteger(top) (0x00B030C0..0x00B030D4)
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	if (top.isString() && under.isString())
	{
		return AptValue::boolean(std::strcmp(under.asString().c_str(), top.asString().c_str()) < 0);
	}
	if (AptOps::isNonNumeric(top, swfVersion) || AptOps::isNonNumeric(under, swfVersion))
	{
		return AptValue::undefined();
	}
	if (top.isFloat() || under.isFloat())
	{
		return AptValue::boolean(top.toNumberWide() > (double)under.toNumber());
	}
	return AptValue::boolean(under.toInteger() < top.toInteger());
}

AptValue greater(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// BFME2 0x00B04710 (target; no BFME1 body is available).  Operands: edi = top, esi = under.  Unlike Less2 it
	// has NO non-numeric check.
	//   SWF 7 and an undefined operand -> the undefined singleton (0x00B0479E)
	//   both strings                   -> strcmp(top, under) < 0 (0x00B04822..0x00B04888: the operands are compared
	//                                     top first)
	//   either operand a float         -> under stored as FLOAT (0x00B048BD), top wide in ST0 (0x00B048C8); true when
	//                                     top < under (fcomp, `test ah,5`: only "below" is true)
	//   otherwise                      -> toInteger(under) > toInteger(top) (0x00B048A0..0x00B048B4)
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	if (top.isString() && under.isString())
	{
		return AptValue::boolean(std::strcmp(top.asString().c_str(), under.asString().c_str()) < 0);
	}
	if (top.isFloat() || under.isFloat())
	{
		return AptValue::boolean(top.toNumberWide() < (double)under.toNumber());
	}
	return AptValue::boolean(under.toInteger() > top.toInteger());
}

namespace
{

bool isPrimitive(const AptValue &v)
{
	// 0x00B031E0: isInteger (0xADC170), isFloat (0xADC210), isBoolean (0xADC120), isString (0xADC0D0)
	return v.isInteger() || v.isFloat() || v.isBoolean() || v.isString();
}

// getType (0x00ADBB30: flags >> 25) distinguishes more kinds than this model has; two values share a
// type when they are the same primitive or the same object kind.
bool sameType(const AptValue &a, const AptValue &b)
{
	if (a.type() != b.type())
	{
		return false;
	}
	if (a.isObject() && a.asObject() && b.asObject())
	{
		return a.asObject()->kind() == b.asObject()->kind();
	}
	return true;
}

bool isHexNumber(const std::string &s)
{
	// strtol(text, &end, 16) with "0x" and more than two characters; valid when it stops at the NUL
	if (s.size() <= 2 || s[0] != '0' || s[1] != 'x')
	{
		return false;
	}
	for (std::size_t i = 2; i < s.size(); ++i)
	{
		char c = s[i];
		bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex)
		{
			return false;
		}
	}
	return true;
}

bool isDigitChar(char c)
{
	return c >= '0' && c <= '9';
}

bool containsDot(const AptValue &v)
{
	return v.isString() && v.asString().find('.') != std::string::npos;
}

} // namespace

bool isNonNumeric(const AptValue &v, std::uint32_t swfVersion)
{
	// BFME2 game.dat 1.06 0x00AFC370.
	if (v.isInteger() || v.isFloat())
	{
		return false;
	}
	if (!v.isString())
	{
		// undefined (and the null type): "non numeric" only in SWF 7; every other value is non numeric
		if (v.isUndefined())
		{
			return swfVersion == 7;
		}
		return true;
	}
	const std::string &s = v.asString();
	std::size_t n = s.size();
	if (n == 0)
	{
		return true;
	}
	if (isHexNumber(s))
	{
		return false;
	}
	char last = s[n - 1];
	if (!(last == '-' || last == '+' || last == 'e' || last == '.' || isDigitChar(last)))
	{
		return true;
	}
	char first = s[0];
	if (!(first == '.' || first == '-' || first == '+' || isDigitChar(first)))
	{
		return true;
	}
	bool seenDot = false;
	std::size_t i = 1;
	while (n > i)
	{
		char c = s[i];
		if (c == '.' && !seenDot)
		{
			seenDot = true;
		}
		else if (c == 'e' && i != 1)
		{
			if (i == 2 && (s[0] == '+' || s[0] == '-'))
			{
				return true;
			}
			std::size_t next = i + 1;
			if (next < n)
			{
				char c2 = s[next];
				if (!(c2 == '-' || c2 == '+' || isDigitChar(c2)))
				{
					return true;
				}
				i = next;
			}
		}
		else if (!isDigitChar(c))
		{
			return true;
		}
		++i;
	}
	return false;
}

AptValue equals2(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// BFME2 game.dat 1.06 handler 0x00B031E0 (opcode 0x49; BFME1 has the same shape).  `top` is the first
	// operand the handler reads (edi), `under` the second (ebp); the asymmetric cases below are EA's.
	const AptValue &a = top;
	const AptValue &b = under;
	if (swfVersion == 7)
	{
		int undefinedCount = (a.isUndefined() ? 1 : 0) + (b.isUndefined() ? 1 : 0);
		if (undefinedCount > 0)
		{
			return AptValue::boolean(undefinedCount == 2);
		}
	}
	bool same = (isPrimitive(a) && isPrimitive(b)) || sameType(a, b);
	if (!same)
	{
		// different, not both primitive: equal only when both are undefined (SWF 6)
		return AptValue::boolean(a.isUndefined() && b.isUndefined());
	}
	if (a.isUndefined())
	{
		return AptValue::boolean(true);
	}
	if (a.isInteger() && b.isInteger())
	{
		return AptValue::boolean(a.toInteger() == b.toInteger());
	}
	if (a.isFloat() && b.isFloat())
	{
		return AptValue::boolean(a.toNumber() == b.toNumber()); // fucompp: exact, NaN is unordered
	}
	if (a.isString() && b.isString())
	{
		return AptValue::boolean(a.asString() == b.asString());
	}
	bool numericA = a.isInteger() || a.isFloat();
	bool numericB = b.isInteger() || b.isFloat();
	bool mixed = false;
	if (numericA && !isNonNumeric(b, swfVersion))
	{
		mixed = true;
	}
	else if (numericB && !isNonNumeric(a, swfVersion))
	{
		mixed = true;
	}
	if (mixed)
	{
		bool floatA = a.isFloat() || containsDot(a);
		bool floatB = b.isFloat() || containsDot(b);
		const float kEpsilon = 0.001f; // 0x00BC28F8
		if (a.isInteger())
		{
			std::int32_t ia = a.toInteger();
			if (floatB)
			{
				// 0x00B0361A: fild; fstp dword - the integer is ROUNDED to float - then b - that (fsubr), with the
				// subtraction itself exact in the x87 register (a difference of two floats; computed in double)
				// the difference is `fsubr` at PC24 (0x00B0361A..0x00B0362B): NumericState::pc24Sub, then fabs, then
				// the strict float compare against 0.001f at 0x00B03692
				// the integer is rounded to float (fild; fstp dword), the OTHER operand stays wide in ST0 (a string is its
				// atof double: BFME2 0x00ADD460), and fsubr computes float(ia) - b at PC24
				float diff = NumericState::pc24SubD((double)(float)ia, b.toNumberWide());
				return AptValue::boolean(std::fabs(diff) < kEpsilon);
			}
			return AptValue::boolean(ia == b.toInteger());
		}
		if (b.isInteger())
		{
			std::int32_t ib = b.toInteger();
			if (floatA)
			{
				// 0x00B03664: fisub - the integer under the float is subtracted EXACT (not rounded to float):
				// float 16777216 against integer 16777217 differs by 1 here, but equals in the branch above
				// fld a; fisub ib at PC24: the integer stays exact, only the difference is rounded to 24 bits
				float diff = NumericState::pc24SubD(a.toNumberWide(), (double)ib);
				return AptValue::boolean(std::fabs(diff) < kEpsilon);
			}
			return AptValue::boolean(a.toInteger() == ib);
		}
		// 0x00B0367A..0x00B03690: neither side is an integer (a float and a numeric string, or two numeric
		// strings that were not both strings): |a - b| < 0.001, not an exact compare
		// a (top) is stored as float (fstp dword [esp+0x1c]); b (under) stays wide: `fsubr` is float(a) - b
		float diff = NumericState::pc24SubD((double)a.toNumber(), b.toNumberWide());
		return AptValue::boolean(std::fabs(diff) < kEpsilon);
	}
	if (a.isString() && !b.isBoolean())
	{
		return AptValue::boolean(a.toString() == b.toString()); // getName of both, compared as strings
	}
	if (a.isBoolean() && !b.isString())
	{
		return AptValue::boolean(b.toInteger() == a.toInteger());
	}
	// identity
	if (a.isObject() && b.isObject())
	{
		return AptValue::boolean(a.asObject() == b.asObject());
	}
	if (a.isExtern() && b.isExtern())
	{
		return AptValue::boolean(true); // the one extern singleton
	}
	return AptValue::boolean(false);
}

AptValue bitAnd(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// AptActionInterpreterBitAnd.cpp: toInteger both, SWF 7 undefined stays undefined
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	return AptValue::integer(under.toInteger() & top.toInteger());
}

AptValue bitRShift(const AptValue &under, const AptValue &top, std::uint32_t swfVersion)
{
	// _FunctionAptActionBitRShift: `underValue >> shift` (x86 sar masks the count to 5 bits)
	if (v7Undefined(under, top, swfVersion))
	{
		return AptValue::undefined();
	}
	std::int32_t shift = top.toInteger();
	return AptValue::integer(under.toInteger() >> (shift & 31));
}

AptValue toIntegerOp(const AptValue &v, std::uint32_t swfVersion)
{
	// Rva008C7BF0Action.cpp: SWF 7 undefined stays undefined, else AptInteger::Create(toInteger)
	if (swfVersion == 7 && v.isUndefined())
	{
		return AptValue::undefined();
	}
	return AptValue::integer(v.toInteger());
}

AptValue booleanNative(const AptValue *arg)
{
	// BFME2 0x00AFF850; see AptNativeHash.cpp for the walk through the disassembly.  Note `toNumber` here is the
	// WIDE value: 0x00AFF90C calls 0x00ADD460 and compares ST0 with 0.0f without storing it first.
	if (!arg)
	{
		return AptValue(); // argc == 0: the undefined singleton (0x00AFF880)
	}
	const AptValue &v = *arg;
	if (v.isObject() && v.asObject())
	{
		return AptValue::boolean(true);
	}
	if (v.isUndefined() || v.isObject())
	{
		return AptValue::boolean(false);
	}
	if (!(v.isInteger() || v.isFloat()) && isNonNumeric(v, 7))
	{
		return AptValue::boolean(false);
	}
	return AptValue::boolean(v.toNumberWide() != 0.0);
}

AptValue toNumberOp(const AptValue &v, std::uint32_t swfVersion)
{
	// BFME2 game.dat 1.06 handler 0x00B03730 (opcode 0x4A): numbers stay; anything non numeric (see
	// isNonNumeric) and SWF 7 undefined become undefined; otherwise the text decides: a '.' makes a float
	// from toNumber, anything else an integer from toInteger ("0x10" -> 16).
	if (v.isFloat() || v.isInteger())
	{
		return v;
	}
	if (isNonNumeric(v, swfVersion))
	{
		return AptValue::undefined();
	}
	if (swfVersion == 7 && v.isUndefined())
	{
		return AptValue::undefined();
	}
	std::string text = v.toString();
	std::size_t dot = text.find('.');
	if (dot != std::string::npos && dot != text.size())
	{
		return AptValue::number(v.toNumber());
	}
	return AptValue::integer(v.toInteger());
}

AptValue toStringOp(const AptValue &v, std::uint32_t swfVersion)
{
	// Rva008C9B70StackString.cpp: strings stay; SWF 7 undefined becomes "undefined"; else getName
	if (v.isString())
	{
		return v;
	}
	if (swfVersion == 7 && v.isUndefined())
	{
		return AptValue::string("undefined");
	}
	return AptValue::string(v.toString());
}

AptValue increment(const AptValue &v, std::uint32_t swfVersion)
{
	// BFME2 game.dat 1.06 handler 0x00B03F40 (opcode 0x50): SWF 7 undefined stays undefined; integers
	// stay integers; everything else is toNumber + 1.0f (constant 0x00BBB8D8).
	if (swfVersion == 7 && v.isUndefined())
	{
		return AptValue::undefined();
	}
	if (v.isInteger())
	{
		return AptValue::integer((std::int32_t)((std::uint32_t)v.asInteger() + 1u));
	}
	// BFME2 0x00B03FB1..0x00B03FBD: toNumber stays wide in ST0, `fadd [0x00BBB8D8]` (1.0f), fstp dword at PC24
	return AptValue::number(NumericState::pc24AddD(v.toNumberWide(), 1.0));
}

AptValue decrement(const AptValue &v, std::uint32_t swfVersion)
{
	// BFME2 game.dat 1.06 handler 0x00B04020 (opcode 0x51), the mirror of increment.
	if (swfVersion == 7 && v.isUndefined())
	{
		return AptValue::undefined();
	}
	if (v.isInteger())
	{
		return AptValue::integer((std::int32_t)((std::uint32_t)v.asInteger() - 1u));
	}
	return AptValue::number(NumericState::pc24SubD(v.toNumberWide(), 1.0));
}

AptValue stringConcat(const AptValue &under, const AptValue &top)
{
	// Rva008C7C80 / StringConcatRva008C6C20: both operands through getName (SWF 7 undefined as "undefined"
	// is applied by the caller, which knows the version).
	return AptValue::string(under.toString() + top.toString());
}

AptValue stringEquals(const AptValue &under, const AptValue &top)
{
	// SWF ActionStringEquals: compare the getName strings.
	return AptValue::boolean(under.toString() == top.toString());
}

} // namespace AptOps

bool AptUtf8Validate(const std::string &text, std::string &why)
{
	std::size_t i = 0;
	while (i < text.size())
	{
		unsigned char lead = (unsigned char)text[i];
		std::size_t len;
		if (lead < 0x80)
		{
			len = 1;
		}
		else if ((lead & 0xE0) == 0xC0)
		{
			len = 2;
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			len = 3;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			len = 4;
		}
		else
		{
			why = "invalid UTF-8 lead byte 0x" + std::to_string(lead) + " (decimal) at byte " + std::to_string(i);
			return false;
		}
		if (i + len > text.size())
		{
			why = "truncated UTF-8 sequence at byte " + std::to_string(i);
			return false;
		}
		for (std::size_t k = 1; k < len; ++k)
		{
			if (((unsigned char)text[i + k] & 0xC0) != 0x80)
			{
				why = "UTF-8 continuation byte missing at byte " + std::to_string(i + k);
				return false;
			}
		}
		i += len;
	}
	return true;
}
