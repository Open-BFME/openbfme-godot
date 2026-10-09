// OpenBFME unit tests: Apt values and the EA coercion/arithmetic rules (spec step A1).
//
// Expected values are derived by hand from the BFME1 decompile sources named in each case
// (game/Libraries/Source/EA/Apt/...), not from this engine's output.  GPL-3.0.

#include "doctest.h"

#include "Libraries/Source/Apt/AptObject.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
AptValue S(const char *s) { return AptValue::string(s); }
AptValue I(int i) { return AptValue::integer(i); }
AptValue F(float f) { return AptValue::number(f); }
} // namespace

TEST_CASE("atof/atoi/strtol helpers follow the MSVC 7.1 C runtime the EA code calls")
{
	CHECK(AptAtof("3.5") == 3.5f);
	CHECK(AptAtof("  12abc") == 12.0f);
	CHECK(AptAtof("abc") == 0.0f);
	CHECK(AptAtof("") == 0.0f);
	CHECK(AptAtof("0x10") == 0.0f); // atof stops at the 'x'; no hex floats in the 7.1 CRT
	CHECK(AptAtof("1e3") == 1000.0f);
	CHECK(AptAtof(".5") == 0.5f);
	CHECK(AptAtof("-.5e1") == -5.0f);
	CHECK(AptAtof("5.") == 5.0f);
	CHECK(AptAtof("+3") == 3.0f);
	CHECK(AptAtof("1e") == 1.0f);
	CHECK(AptAtof("inf") == 0.0f);
	CHECK(AptAtof("nan") == 0.0f);
	CHECK(AptAtof("1e999") == std::numeric_limits<float>::infinity()); // overflow: HUGE_VAL
	CHECK(AptAtof("-1e999") == -std::numeric_limits<float>::infinity());
	CHECK(AptAtof("1e-999") == 0.0f);                                  // underflow
	CHECK(AptAtof("0.1") == 0.1f);
	// overflow and underflow are classified by the MAGNITUDE of the result, not by the text (retail ToNumber 0x00ADD460,
	// observed through the retail oracle): a tiny value without "e-" is 0, a huge value WITH "e-1" is +infinity
	CHECK(AptAtof("0." + std::string(324, '0') + "1") == 0.0f);
	CHECK(AptAtof(std::string(350, '1') + "e-1") == std::numeric_limits<float>::infinity());
	CHECK(AptAtof("-" + std::string(350, '1') + "e-1") == -std::numeric_limits<float>::infinity());
	CHECK(AptAtof("1e400") == std::numeric_limits<float>::infinity());
	CHECK(AptAtof("-0." + std::string(400, '0') + "5") == 0.0f);
	CHECK(AptAtof("00001e-400") == 0.0f);
	CHECK(AptAtof("1" + std::string(310, '0')) == std::numeric_limits<float>::infinity());

	CHECK(AptAtoi("12") == 12);
	CHECK(AptAtoi("  -7x") == -7);
	CHECK(AptAtoi("3.9") == 3);
	CHECK(AptAtoi("") == 0);
	// MSVCR71 atoi wraps modulo 2^32 (retail 0x00A29838, observed through the retail oracle): only strtol saturates
	CHECK(AptAtoi("4294967296") == 0);
	CHECK(AptAtoi("2147483648") == (-2147483647 - 1));
	CHECK(AptAtoi("99999999999") == 1215752191);
	CHECK(AptAtoi("-99999999999") == -1215752191);

	CHECK(AptStrtolHex("0x1F") == 31);
	CHECK(AptStrtolHex("0xffffffff") == 2147483647); // overflows a 32-bit long: saturates
	CHECK(AptStrtolHex("-0x10") == -16);
	CHECK(AptStrtolHex("zz") == 0);

	CHECK(AptFloatToInt(3.9f) == 3);
	CHECK(AptFloatToInt(-3.9f) == -3);
	CHECK(AptFloatToInt(3e10f) == -64770048); // 3e10 mod 2^32: BFME2 0x00A29228 keeps the low 32 bits of the 64-bit integer
	CHECK(AptFloatToInt(std::numeric_limits<float>::quiet_NaN()) == 0); // the 64-bit integer indefinite has a zero low word
}

namespace
{
// The exact decimal text of 2^-k: 5^k / 10^k.
std::string exactInversePowerOfTwo(int k)
{
	std::vector<int> d{ 1 }; // little-endian decimal digits of 5^k
	for (int i = 0; i < k; ++i)
	{
		int carry = 0;
		for (int &x : d)
		{
			int v = x * 5 + carry;
			x = v % 10;
			carry = v / 10;
		}
		while (carry)
		{
			d.push_back(carry % 10);
			carry /= 10;
		}
	}
	std::string digits;
	for (std::size_t i = d.size(); i-- > 0;)
	{
		digits.push_back((char)('0' + d[i]));
	}
	return "0." + std::string((std::size_t)k - digits.size(), '0') + digits;
}

std::uint64_t doubleBits(double v)
{
	std::uint64_t b;
	std::memcpy(&b, &v, sizeof(b));
	return b;
}
} // namespace

TEST_CASE("S-017 is decided from the exact value of the TEXT: 2^-1075 (rounds to 0), a text that rounds up to DBL_MIN, and the exact DBL_MIN")
{
	// Sol review r6, observed through retail ToNumber 0x00ADD460 (retail bits are in tools/retail_oracle test_stop_s017):
	// the exact decimal of 2^-1075 is 0x0000000000000001 in retail and 0 here; the text below is 0x000FFFFFFFFFFFFF in retail
	// and 0x0010000000000000 here. Both are reported even though the engine's rounded result is zero / a normal number.
	AptTakeNumericStop();
	const std::string tie = exactInversePowerOfTwo(1075);
	CHECK(doubleBits(AptAtofDouble(tie)) == 0x0000000000000000ull); // round-half-to-even
	REQUIRE(AptNumericStopPending());
	CHECK(AptTakeNumericStop().find("S-017") != std::string::npos);

	const std::string roundsUp = "2.225073858507201259573821257020768020077017763406988739288377e-308";
	CHECK(doubleBits(AptAtofDouble(roundsUp)) == 0x0010000000000000ull);
	REQUIRE(AptNumericStopPending());
	CHECK(AptTakeNumericStop().find("S-017") != std::string::npos);

	// the exact DBL_MIN itself and anything above it is a normal conversion: no report
	CHECK(doubleBits(AptAtofDouble(exactInversePowerOfTwo(1022))) == 0x0010000000000000ull);
	CHECK_FALSE(AptNumericStopPending());
	CHECK(AptAtofDouble("2.2250738585072014e-308") > 0.0);
	CHECK_FALSE(AptNumericStopPending());
	// the floor: 2e-324 is in the band, 1.9e-324 and 1e-400 underflow to zero without a report
	CHECK(AptAtofDouble("1.9e-324") == 0.0);
	CHECK_FALSE(AptNumericStopPending());
	CHECK(AptAtofDouble("1e-400") == 0.0);
	CHECK_FALSE(AptNumericStopPending());
	CHECK(AptAtofDouble("2e-324") == 0.0);
	CHECK(AptNumericStopPending());
	AptTakeNumericStop();
	// the boolean consequence: retail Boolean(<2^-1075 text>) is true, the engine's rounded 0 gives false, and the report fires
	AptValue tieValue = AptValue::string(tie);
	CHECK_FALSE(AptOps::booleanNative(&tieValue).asBool());
	CHECK(AptTakeNumericStop().find("S-017") != std::string::npos);
}

TEST_CASE("atof of a result in the subnormal double range leaves an S-017 report; normal results do not")
{
	AptTakeNumericStop(); // clear
	CHECK(AptAtofDouble("1.5") == 1.5);
	CHECK_FALSE(AptNumericStopPending());
	CHECK(AptAtofDouble("1e-400") == 0.0); // underflow to zero is classified, not subnormal
	CHECK_FALSE(AptNumericStopPending());
	CHECK(AptAtofDouble("2.2250738585072014e-308") > 0.0); // the smallest normal double
	CHECK_FALSE(AptNumericStopPending());
	CHECK(AptAtofDouble("1e-320") > 0.0);
	REQUIRE(AptNumericStopPending());
	std::string report = AptTakeNumericStop();
	CHECK(report.find("S-017") != std::string::npos);
	CHECK(report.find("1e-320") != std::string::npos);
	CHECK_FALSE(AptNumericStopPending());
}

TEST_CASE("toNumber: AptValue_toNumber.cpp")
{
	CHECK(S("2.25").toNumber() == 2.25f);
	CHECK(S("0x10").toNumber() == 0.0f);                // atof, not strtol
	CHECK(AptValue::boolean(true).toNumber() == 1.0f);  // g_bfmeDefaultBU
	CHECK(AptValue::boolean(false).toNumber() == 0.0f);
	CHECK(I(-4).toNumber() == -4.0f);
	CHECK(F(1.5f).toNumber() == 1.5f);
	CHECK(AptValue().toNumber() == 0.0f);               // isUndefined -> BfmeZeroRange
	AptGC gc;
	CHECK(AptValue::object(gc.create<AptObject>()).toNumber() == 1.0f); // default arm
}

TEST_CASE("toInteger: AptValueToInteger.cpp")
{
	CHECK(S("0x1F").toInteger() == 31);   // length > 2 and a "0x" prefix: strtol base 16
	CHECK(S("0x").toInteger() == 0);      // length 2 is not enough: atoi("0x") == 0
	CHECK(S("12").toInteger() == 12);
	CHECK(S("3.9").toInteger() == 3);
	CHECK(S("0X10").toInteger() == 0);    // only lower-case "0x" is tested
	CHECK(F(3.9f).toInteger() == 3);
	CHECK(F(-3.9f).toInteger() == -3);
	CHECK(AptValue::boolean(true).toInteger() == 1);
	CHECK(I(7).toInteger() == 7);
	CHECK(AptValue().toInteger() == 0);
	AptGC gc;
	CHECK(AptValue::object(gc.create<AptObject>()).toInteger() == 1); // this != gpNullValue
}

TEST_CASE("truthiness: AptValue_rva00898480Check.cpp (SWF 7 strings are non-empty tests, older movies parse them)")
{
	CHECK_FALSE(S("").toBoolean(7));
	CHECK(S("0").toBoolean(7));      // SWF 7: any non-empty string
	CHECK(S("false").toBoolean(7));
	CHECK_FALSE(S("0").toBoolean(6));  // SWF 6: atof("0") == 0
	CHECK_FALSE(S("0.0").toBoolean(6));
	CHECK(S("1").toBoolean(6));
	CHECK_FALSE(S("abc").toBoolean(6)); // atof("abc") == 0
	CHECK_FALSE(S("0x0").toBoolean(6)); // hex branch: strtol == 0
	CHECK(S("0x10").toBoolean(6));
	CHECK(AptValue::boolean(true).toBoolean(7));
	CHECK_FALSE(AptValue::boolean(false).toBoolean(6));
	CHECK(I(5).toBoolean(7));
	CHECK_FALSE(I(0).toBoolean(7));
	CHECK(F(0.25f).toBoolean(7));
	CHECK_FALSE(F(0.0f).toBoolean(7));
	CHECK_FALSE(AptValue().toBoolean(7));
	AptGC gc;
	CHECK(AptValue::object(gc.create<AptObject>()).toBoolean(7));
}

TEST_CASE("toString: Rva008985C0ValueString.cpp (getName)")
{
	CHECK(I(-5).toString() == "-5");
	CHECK(AptValue::boolean(true).toString() == "true");
	CHECK(AptValue::boolean(false).toString() == "false");
	CHECK(F(3.0f).toString() == "3");           // whole floats print through "%d"
	CHECK(F(-7.0f).toString() == "-7");
	CHECK(F(1.5f).toString() == "1.500000");    // everything else through "%f"
	CHECK(F(0.1f).toString() == "0.100000");
	CHECK(F(-2.25f).toString() == "-2.250000");
	CHECK(F(1e10f).toString() == "1410065408"); // "%d" of the retail (int)1e10: 10000000000 mod 2^32
	CHECK(AptValue().toString() == "");
	CHECK(S("keep").toString() == "keep");
	AptGC gc;
	AptArray *a = gc.create<AptArray>();
	a->items = { I(1), S("b"), AptValue() };
	CHECK(AptValue::object(a).toString() == "1,b,"); // arrays join with commas
	AptFunction *fn = gc.create<AptFunction>();
	CHECK(AptValue::object(fn).toString() == "[function]");
	CHECK(AptValue::object(gc.create<AptObject>()).toString() == "[object Object]");
	CHECK(AptValue::externValue().toString() == "[Extern]");
}

TEST_CASE("typeof: Rva008C8350TypeOf.cpp")
{
	AptGC gc;
	CHECK(std::string(AptValue().typeOf()) == "undefined");
	// BFME1 Rva008C8350TypeOf.cpp:86-112: type 11 (extern) matches no branch, the result string stays empty
	CHECK(std::string(AptValue::externValue().typeOf()) == "");
	CHECK(std::string(I(1).typeOf()) == "number");
	CHECK(std::string(F(1.5f).typeOf()) == "number");
	CHECK(std::string(AptValue::boolean(true).typeOf()) == "boolean");
	CHECK(std::string(S("x").typeOf()) == "string");
	CHECK(std::string(AptValue::object(gc.create<AptObject>()).typeOf()) == "object");
	CHECK(std::string(AptValue::object(gc.create<AptArray>()).typeOf()) == "object");
	CHECK(std::string(AptValue::object(gc.create<AptFunction>()).typeOf()) == "function");
	CHECK(std::string(AptValue::object(gc.create<AptObject>(AptObjectKind::Clip)).typeOf()) == "movieclip");
}

TEST_CASE("Add2 (AddValues008C8500.cpp): 1.5+1.5 is the float 3, strings concatenate, integers add as integers")
{
	AptValue r = AptOps::add2(F(1.5f), F(1.5f), 7);
	CHECK(r.isFloat());
	CHECK(r.asFloat() == 3.0f);
	CHECK(AptOps::equals2(I(3), r, 7).asBool());   // 1.5+1.5==3 (the archived VM truncated to 2): mixed int/float epsilon

	r = AptOps::add2(I(2), I(3), 7);
	CHECK(r.isInteger());
	CHECK(r.asInteger() == 5);
	r = AptOps::add2(I(2), F(0.5f), 7);
	CHECK(r.isFloat());
	CHECK(r.asFloat() == 2.5f);
	r = AptOps::add2(S("a"), I(1), 7);
	CHECK(r.asString() == "a1");
	r = AptOps::add2(I(1), S("a"), 7);
	CHECK(r.asString() == "1a");
	r = AptOps::add2(S("x"), F(1.5f), 7);
	CHECK(r.asString() == "x1.500000");
	r = AptOps::add2(AptValue::boolean(true), AptValue::boolean(true), 7);
	CHECK(r.isFloat());                          // neither is an integer: the float branch
	CHECK(r.asFloat() == 2.0f);
	r = AptOps::add2(AptValue::boolean(true), I(1), 7);
	CHECK(r.isInteger());                        // one integer, no float: the integer branch
	CHECK(r.asInteger() == 2);
	r = AptOps::add2(F(0.1f), F(0.2f), 7);
	CHECK(r.asFloat() == 0.1f + 0.2f);           // single precision, not double

	// SWF 7: undefined operands
	CHECK(AptOps::add2(AptValue(), I(1), 7).isUndefined());
	CHECK(AptOps::add2(I(1), AptValue(), 7).isUndefined());
	CHECK(AptOps::add2(AptValue(), F(1.5f), 7).isUndefined());
	CHECK(AptOps::add2(S("a"), AptValue(), 7).asString() == "aundefined"); // the "undefined" string block
	CHECK(AptOps::add2(AptValue(), S("a"), 7).asString() == "undefineda");
	// SWF 6: undefined is coerced
	CHECK(AptOps::add2(AptValue(), I(1), 6).asInteger() == 1);
	CHECK(AptOps::add2(AptValue(), F(1.5f), 6).asFloat() == 1.5f);
	CHECK(AptOps::add2(S("a"), AptValue(), 6).asString() == "a");
}

TEST_CASE("Subtract/Multiply/Divide/Modulo: float arithmetic, SWF 7 undefined and division by zero give the fallback")
{
	// BFME2 0x00B00880 (target; the BFME1 donor always makes a float): two integers subtract as integers
	CHECK(AptOps::subtract(I(7), I(2), 7).isInteger());
	CHECK(AptOps::subtract(I(7), I(2), 7).asInteger() == 5);
	CHECK(AptOps::subtract(I(7), F(2.0f), 7).isFloat());                          // one float: the float path
	CHECK(AptOps::subtract(I(7), F(2.0f), 7).asFloat() == 5.0f);
	CHECK(AptOps::subtract(I(-2147483647 - 1), I(1), 7).asInteger() == 2147483647); // wraps like `sub eax, esi`
	// BFME2 0x00B009E0: integer * integer is an integer (`imul eax, esi`), wrapping
	CHECK(AptOps::multiply(I(6), I(7), 7).isInteger());
	CHECK(AptOps::multiply(I(6), I(7), 7).asInteger() == 42);
	CHECK(AptOps::multiply(I(65536), I(65536), 7).asInteger() == 0);
	CHECK(AptOps::multiply(F(1.5f), I(4), 7).asFloat() == 6.0f);
	AptValue d = AptOps::divide(I(7), I(2), 7);
	CHECK(d.isFloat());
	CHECK(d.asFloat() == 3.5f);
	CHECK(AptOps::divide(I(1), I(0), 7).isUndefined());     // top == BfmeZeroRange -> g_bfmeFallbackDB
	CHECK(AptOps::divide(I(0), I(0), 7).isUndefined());
	CHECK(AptOps::divide(I(1), AptValue(), 7).isUndefined());
	CHECK(AptOps::divide(I(6), AptValue(), 6).isUndefined()); // SWF 6: undefined -> 0 -> division by zero
	CHECK(AptOps::subtract(AptValue(), I(1), 7).isUndefined());
	CHECK(AptOps::subtract(AptValue(), I(1), 6).asFloat() == -1.0f);
	CHECK(AptOps::modulo(I(7), I(3), 7).asFloat() == 1.0f);
	CHECK(AptOps::modulo(F(7.5f), I(2), 7).asFloat() == 1.5f);
	CHECK(AptOps::modulo(I(-7), I(3), 7).asFloat() == -1.0f); // fmod keeps the dividend's sign
	CHECK(AptOps::modulo(I(7), I(0), 7).isUndefined());
}

TEST_CASE("Less2 (LessThan008C8840.cpp) and Greater")
{
	CHECK(AptOps::less2(I(1), I(2), 7).asBool());
	CHECK_FALSE(AptOps::less2(I(2), I(2), 7).asBool());
	CHECK(AptOps::less2(F(1.25f), I(2), 7).asBool());
	CHECK(AptOps::less2(S("a"), S("b"), 7).asBool());
	CHECK(AptOps::less2(S("10"), S("9"), 7).asBool());    // both strings: strcmp
	CHECK_FALSE(AptOps::less2(S("10"), I(9), 7).asBool()); // mixed: integer compare 10 < 9
	CHECK(AptOps::less2(AptValue(), I(1), 7).isUndefined());
	CHECK(AptOps::less2(I(1), AptValue(), 7).isUndefined());
	CHECK(AptOps::less2(AptValue(), I(1), 6).asBool());    // SWF 6: 0 < 1
	CHECK(AptOps::less2(F(0.5f), F(0.75f), 7).asBool());
	CHECK(AptOps::less2(I(1), F(1.5f), 7).asBool());       // either float: float compare

	CHECK(AptOps::greater(I(3), I(2), 7).asBool());
	CHECK_FALSE(AptOps::greater(I(2), I(2), 7).asBool());
	CHECK(AptOps::greater(AptValue(), I(1), 7).isUndefined());
}

// Expected values in the Equals2 / ToNumber / Increment tests come from tracing the clean BFME2 1.06 game.dat
// handlers by hand (virtual addresses): Equals2 0x00B031E0..0x00B0372F, ToNumber 0x00B03730, Increment 0x00B03F40,
// Decrement 0x00B04020, isNonNumeric 0x00AFC370.  The handler reads `top` first (edi) and `under` second (ebp);
// eqTop(a, b) below is "a on top, b below".
namespace
{
bool eqTop(const AptValue &a, const AptValue &b, std::uint32_t version = 7)
{
	return AptOps::equals2(b, a, version).asBool();
}
} // namespace

TEST_CASE("Equals2 (BFME2 handler 0x00B031E0): mixed integer/float uses fabs(a-b) < 0.001, float/float is exact")
{
	// 0x00B03690: fabs; fcomp dword [0x00BC28F8] (the float 0.001); result = (|diff| < 0.001), strict.
	CHECK(eqTop(I(1), F(1.0005f)));          // |1 - 1.0005| = 0.0005 < 0.001
	CHECK(eqTop(F(1.0005f), I(1)));
	CHECK_FALSE(eqTop(I(1), F(1.002f)));     // 0.002 is not < 0.001
	CHECK_FALSE(eqTop(F(1.0005f), F(1.0f))); // float/float is exact (fucompp), no epsilon
	CHECK(eqTop(F(1.5f), F(1.5f)));
	CHECK(eqTop(I(5), I(5)));                // int/int exact
	CHECK_FALSE(eqTop(I(5), I(6)));
	CHECK(eqTop(I(3), F(3.0f)));
	// an integer against a numeric string: floatB (a '.' in the text) selects the epsilon test, else integers
	CHECK(eqTop(I(3), S("3")));
	CHECK(eqTop(I(3), S("3.0004")));
	CHECK_FALSE(eqTop(I(3), S("3.002")));
	CHECK(eqTop(S("3"), I(3)));
	// float against a numeric string: neither side is an integer, |a - b| < 0.001 (0x00B0367A..0x00B03692)
	CHECK(eqTop(F(1.5f), S("1.5")));
	CHECK(eqTop(F(1.5f), S("1.5002")));
	CHECK(eqTop(S("1.5002"), F(1.5f)));
	CHECK_FALSE(eqTop(F(1.5f), S("1.502")));
	float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(eqTop(F(nan), F(nan))); // fucompp: unordered is not equal
}

TEST_CASE("PC24 arithmetic (setFPMode 0x00440EA9..0x00440EC5): float Add2/Subtract/Multiply/Divide round like fld; f<op>; fstp dword")
{
	auto bits = [](float f) {
		std::uint32_t b;
		std::memcpy(&b, &f, 4);
		return b;
	};
	// the review case NumericState pins from real x87 hardware (test_numeric_state.cpp): 5.439772e-38 * 0.20226517 is
	// 0x0077CF3C under PC24 (double rounding of the subnormal result); plain float32 arithmetic gives 0x0077CF3B
	CHECK(bits(AptOps::multiply(F(5.439772e-38f), F(0.20226517f), 7).asFloat()) == 0x0077CF3Cu);
	// normal-range results equal plain float arithmetic
	CHECK(AptOps::add2(F(1.5f), F(1.5f), 7).asFloat() == 3.0f);
	CHECK(AptOps::subtract(F(1.5f), F(0.25f), 7).asFloat() == 1.25f);
	CHECK(AptOps::divide(F(1.0f), F(3.0f), 7).asFloat() == 1.0f / 3.0f);
	// 16777216 + 1 stays 16777216 (round to nearest even at 24 bits), and Increment rounds the same way
	CHECK(AptOps::add2(F(16777216.0f), F(1.0f), 7).asFloat() == 16777216.0f);
	CHECK(AptOps::increment(F(16777216.0f), 7).asFloat() == 16777216.0f);
	CHECK(AptOps::decrement(F(-16777216.0f), 7).asFloat() == -16777216.0f);
}

// The expected values in the next tests are RETAIL results: the real BFME2 handlers were run through lane
// ORACLE-1's native harness (Sol review r4); they are external truth, not this engine's output.

TEST_CASE("retail Equals2: a numeric string keeps its DOUBLE value into the subtraction (BFME2 0x00ADD460 returns it in ST0)")
{
	for (std::uint32_t version : { 6u, 7u })
	{
		// under = "1.00099999", top = Integer(1): TRUE in retail (the port used to round the string to float first)
		CHECK_MESSAGE(AptOps::equals2(S("1.00099999"), I(1), version).asBool(), "SWF " << version);
		// reversed
		CHECK_MESSAGE(AptOps::equals2(I(1), S("1.00099999"), version).asBool(), "SWF " << version);
		// under = that string, top = Float(1)
		CHECK_MESSAGE(AptOps::equals2(S("1.00099999"), F(1.0f), version).asBool(), "SWF " << version);
	}
}

TEST_CASE("retail toInteger of a float: through a 64-bit integer, low 32 bits (BFME2 0x00ADD400 -> 0x00A29228)")
{
	CHECK(F(4294967296.0f).toInteger() == 0);
	CHECK(F(3.0e9f).toInteger() == -1294967296);       // 3000000000 - 2^32
	CHECK(F(1.0e10f).toInteger() == 1410065408);       // 10000000000 mod 2^32
	CHECK(F(2147483648.0f).toInteger() == (std::int32_t)0x80000000);
	CHECK(F(-2147483649.0f).toInteger() == (std::int32_t)0x80000000); // float(-2^31 - 1) is -2^31
	CHECK(F(-2.7f).toInteger() == -2);                 // truncation toward zero
	CHECK(F(2.7f).toInteger() == 2);
	CHECK(F(-4294967296.0f).toInteger() == 0);
	CHECK(F(std::numeric_limits<float>::quiet_NaN()).toInteger() == 0);
	CHECK(F(std::numeric_limits<float>::infinity()).toInteger() == 0);
	CHECK(F(-std::numeric_limits<float>::infinity()).toInteger() == 0);
	CHECK(F(1.0e19f).toInteger() == 0);                 // >= 2^63: the 64-bit integer indefinite has a zero low word
	CHECK(F(-1.0e19f).toInteger() == 0);
	// the review's Equals2 case: under = Float(4294967296), top = Boolean(false): both convert to 0
	for (std::uint32_t version : { 6u, 7u })
	{
		CHECK_MESSAGE(AptOps::equals2(F(4294967296.0f), AptValue::boolean(false), version).asBool(), "SWF " << version);
	}
}

TEST_CASE("retail Subtract/Multiply/Divide: the TOP operand is stored as float, the UNDER operand stays wide (BFME2 0x00B00957, 0x00B00AB8, 0x00B00BD9)")
{
	for (std::uint32_t version : { 6u, 7u })
	{
		const AptValue under = I(16777217);
		AptValue r = AptOps::subtract(under, F(1.0f), version);
		CHECK_MESSAGE(r.isFloat(), "SWF " << version);
		CHECK_MESSAGE(r.asFloat() == 16777216.0f, "SWF " << version);
		float topBits;
		const std::uint32_t one1 = 0x3F800001u;
		std::memcpy(&topBits, &one1, 4);
		CHECK_MESSAGE(AptOps::multiply(under, F(topBits), version).asFloat() == 16777220.0f, "SWF " << version);
		CHECK_MESSAGE(AptOps::divide(under, F(topBits), version).asFloat() == 16777215.0f, "SWF " << version);
		// a numeric string keeps its double under: 1.0005 - 1 is 0x3A03126F, not 0x3A031000
		AptValue s = AptOps::subtract(S("1.0005"), I(1), version);
		float got = s.asFloat();
		std::uint32_t bits;
		std::memcpy(&bits, &got, 4);
		CHECK_MESSAGE(bits == 0x3A03126Fu, "SWF " << version);
	}
}

TEST_CASE("Less2 and Greater (BFME2 0x00B02F20, 0x00B04710): under is stored as float, top stays wide; Less2 rejects non-numeric operands, Greater does not")
{
	// Less2 true iff top_wide > float(under).  under = "0.99999999" is 1.0f as a float, top = 1.0f: not greater
	CHECK_FALSE(AptOps::less2(S("0.99999999"), F(1.0f), 7).asBool());
	CHECK(AptOps::less2(F(1.0f), S("1.00000001"), 7).asBool());                // top wide 1.00000001 > 1.0f
	// Greater true iff top_wide < float(under): under = Float(1), top = "0.99999999" (wide) is below
	CHECK(AptOps::greater(F(1.0f), S("0.99999999"), 7).asBool());
	CHECK_FALSE(AptOps::greater(S("1.00000001"), F(1.0f), 7).asBool());         // under 1.0f, top 1.0f: not below
	// integers compare as integers
	CHECK(AptOps::less2(I(1), I(2), 7).asBool());
	CHECK(AptOps::greater(I(2), I(1), 7).asBool());
	CHECK_FALSE(AptOps::greater(I(1), I(2), 7).asBool());
	// strings compare as text, top first for Greater
	CHECK(AptOps::greater(S("b"), S("a"), 7).asBool());
	CHECK(AptOps::less2(S("a"), S("b"), 7).asBool());
	// Less2 returns the undefined singleton for a non-numeric operand (0x00B03174); Greater has no such check
	CHECK(AptOps::less2(I(1), AptValue::boolean(true), 7).isUndefined());
	CHECK(AptOps::less2(S("abc"), I(1), 7).isUndefined());
	CHECK_FALSE(AptOps::greater(I(1), AptValue::boolean(true), 7).isUndefined());
	// SWF 7 with an undefined operand: both give the undefined singleton
	CHECK(AptOps::less2(AptValue(), I(1), 7).isUndefined());
	CHECK(AptOps::greater(AptValue(), I(1), 7).isUndefined());
}

TEST_CASE("Equals2 (BFME2 0x00B0368C subtraction, 0x00B03692 compare): the difference is rounded to 24 bits before the strict epsilon compare")
{
	// under = 2^-35 as text, top = 0.001f.  The exact difference 0.001f - 2^-35 is below 0.001f, but a quarter ulp
	// of 0.001f (ulp 2^-33) rounds back to 0.001f under PC24, so |diff| == epsilon and `<` is false.  A double
	// subtraction gets this wrong.
	AptValue under = S("0.00000000002910383045673370361328125");
	REQUIRE(under.toNumber() == std::ldexp(1.0f, -35));
	CHECK_FALSE(AptOps::equals2(under, F(0.001f), 7).asBool());
	// 1e-10 is 0.86 ulp of 0.001f: the rounded difference is one full ulp below 0.001f, so it compares less
	CHECK(AptOps::equals2(S("0.0000000001"), F(0.001f), 7).asBool());
}

TEST_CASE("Equals2 (BFME2 handler 0x00B031E0): the integer is rounded to float on top (0x00B0361E) and exact underneath (fisub 0x00B03664)")
{
	// 16777217 = 2^24 + 1 is not representable as a float; it rounds to 16777216
	// under = integer, top = float: fisub keeps the integer exact: 16777216 - 16777217 = -1, not < 0.001
	CHECK_FALSE(AptOps::equals2(I(16777217), F(16777216.0f), 7).asBool());
	// reversed (integer on top): fild + fstp rounds it to 16777216 first, the difference is 0
	CHECK(AptOps::equals2(F(16777216.0f), I(16777217), 7).asBool());
	// away from the rounding edge both orders agree
	CHECK(AptOps::equals2(I(16777216), F(16777216.0f), 7).asBool());
	CHECK(AptOps::equals2(F(16777216.0f), I(16777216), 7).asBool());
}

TEST_CASE("Equals2 (BFME2 handler 0x00B031E0): the operand-order asymmetries of the type dispatch")
{
	AptGC gc;
	AptObject *a = gc.create<AptObject>();
	AptObject *b = gc.create<AptObject>();
	// non numeric string next to an integer: not the numeric path (isNonNumeric 0x00AFC370 is true)
	CHECK_FALSE(eqTop(I(3), S("3x")));             // a=int: falls to pointer identity
	CHECK_FALSE(eqTop(S("3x"), I(3)));             // a=string, b not boolean: getName compare "3x" vs "3"
	// boolean on top compares toInteger(b) == toInteger(a); boolean underneath falls to identity
	CHECK(eqTop(AptValue::boolean(true), I(1)));
	CHECK_FALSE(eqTop(I(1), AptValue::boolean(true)));
	CHECK(eqTop(AptValue::boolean(false), I(0)));
	CHECK_FALSE(eqTop(AptValue::boolean(true), S("1"))); // a boolean against a string reaches identity
	CHECK_FALSE(eqTop(S("1"), AptValue::boolean(true)));
	CHECK(eqTop(AptValue::boolean(true), AptValue::boolean(true)));
	// strings
	CHECK(eqTop(S("a"), S("a")));
	CHECK_FALSE(eqTop(S("a"), S("A")));
	// a primitive against an object is a type mismatch (not both primitive, types differ): never equal,
	// whichever side is on top (0x00B031E0 returns isUndefined(a) && isUndefined(b))
	CHECK_FALSE(eqTop(S("[object Object]"), AptValue::object(a)));
	CHECK_FALSE(eqTop(AptValue::object(a), S("[object Object]")));
	// objects: identity
	CHECK(eqTop(AptValue::object(a), AptValue::object(a)));
	CHECK_FALSE(eqTop(AptValue::object(a), AptValue::object(b)));
	CHECK(eqTop(AptValue::externValue(), AptValue::externValue()));
}

TEST_CASE("Equals2 (BFME2 handler 0x00B031E0): undefined is the only undefined, SWF 7 decides by count")
{
	// SWF 7: esi = isUndefined(a) + isUndefined(b); any undefined operand returns (esi == 2)
	CHECK(eqTop(AptValue(), AptValue(), 7));
	CHECK_FALSE(eqTop(AptValue(), I(0), 7));
	CHECK_FALSE(eqTop(I(0), AptValue(), 7));
	CHECK_FALSE(eqTop(AptValue(), S(""), 7));
	// SWF 6: undefined compares through the type dispatch: two undefined values are equal, nothing else is
	CHECK(eqTop(AptValue(), AptValue(), 6));
	CHECK_FALSE(eqTop(AptValue(), I(0), 6));
	CHECK_FALSE(eqTop(I(0), AptValue(), 6));
	CHECK_FALSE(eqTop(AptValue(), S(""), 6));
}

TEST_CASE("isNonNumeric (BFME2 0x00AFC370): the numeric string validator, quirks included")
{
	auto nn = [](const AptValue &v, std::uint32_t ver = 7) { return AptOps::isNonNumeric(v, ver); };
	CHECK_FALSE(nn(I(1)));
	CHECK_FALSE(nn(F(1.5f)));
	CHECK(nn(S("")));                // an empty string is not numeric
	CHECK(nn(S("abc")));             // last character is not a digit or one of -+e.
	CHECK_FALSE(nn(S("12")));
	CHECK_FALSE(nn(S("1.5")));
	CHECK_FALSE(nn(S("-3")));
	CHECK_FALSE(nn(S("+.5")));
	CHECK_FALSE(nn(S("0x1F")));      // strtol base 16 consumes the whole string
	CHECK(nn(S("0xZZ")));
	CHECK(nn(S("1.2.3")));           // a second '.'
	CHECK(nn(S("12abc")));
	CHECK(nn(S("e5")));              // first character must be a digit, '.', '-' or '+'
	CHECK(nn(S("1e5")));             // an 'e' at index 1 is rejected (cmp esi,1; je 0x00AFC5C2)
	CHECK_FALSE(nn(S("12e5")));
	CHECK(nn(S("+1e5")));            // 'e' at index 2 after a sign
	CHECK(nn(AptValue::boolean(true)));
	AptGC gc;
	CHECK(nn(AptValue::object(gc.create<AptObject>())));
	CHECK(nn(AptValue(), 7));        // undefined: non numeric only in SWF 7
	CHECK_FALSE(nn(AptValue(), 6));
}

TEST_CASE("ToNumber (BFME2 handler 0x00B03730): numbers stay, text decides integer or float, the rest is undefined")
{
	CHECK(AptOps::toNumberOp(I(3), 7).isInteger());
	CHECK(AptOps::toNumberOp(F(2.5f), 7).isFloat());
	AptValue r = AptOps::toNumberOp(S("12"), 7);
	CHECK(r.isInteger());
	CHECK(r.asInteger() == 12);
	r = AptOps::toNumberOp(S("1.5"), 7);
	CHECK(r.isFloat());
	CHECK(r.asFloat() == 1.5f);
	r = AptOps::toNumberOp(S("1."), 7);
	CHECK(r.isFloat());                       // a '.' anywhere makes a float
	r = AptOps::toNumberOp(S("0x10"), 7);
	CHECK(r.isInteger());                     // EA's integer path: toInteger("0x10") == 16
	CHECK(r.asInteger() == 16);
	CHECK(AptOps::toNumberOp(S("abc"), 7).isUndefined());
	CHECK(AptOps::toNumberOp(S(""), 7).isUndefined());
	CHECK(AptOps::toNumberOp(AptValue::boolean(true), 7).isUndefined()); // booleans are "non numeric" for the handler
	CHECK(AptOps::toNumberOp(AptValue(), 7).isUndefined());              // SWF 7 undefined is preserved
	r = AptOps::toNumberOp(AptValue(), 6);                               // SWF 6: numeric, text "" -> integer 0
	CHECK(r.isInteger());
	CHECK(r.asInteger() == 0);
}

TEST_CASE("bit operations, ToInteger and increment (AptActionInterpreterBitAnd.cpp, Rva008C7BF0Action.cpp)")
{
	CHECK(AptOps::bitAnd(I(0xF0F0), I(0x0FF0), 7).asInteger() == 0x00F0);
	CHECK(AptOps::bitAnd(S("12"), F(10.9f), 7).asInteger() == (12 & 10)); // toInteger of each operand
	CHECK(AptOps::bitAnd(AptValue(), I(1), 7).isUndefined());
	CHECK(AptOps::bitAnd(AptValue(), I(1), 6).asInteger() == 0);
	CHECK(AptOps::bitRShift(I(-16), I(2), 7).asInteger() == -4);     // signed shift
	CHECK(AptOps::bitRShift(I(256), I(33), 7).asInteger() == 128);   // x86 masks the count to 5 bits
	CHECK(AptOps::toIntegerOp(F(3.7f), 7).asInteger() == 3);
	CHECK(AptOps::toIntegerOp(S("0x10"), 7).asInteger() == 16);
	CHECK(AptOps::toIntegerOp(AptValue(), 7).isUndefined());
	CHECK(AptOps::toIntegerOp(AptValue(), 6).asInteger() == 0);
	// Increment 0x00B03F40 / Decrement 0x00B04020: SWF 7 undefined is preserved, integers stay integers,
	// everything else is toNumber +/- 1.0f
	CHECK(AptOps::increment(I(4), 7).asInteger() == 5);
	CHECK(AptOps::increment(I(4), 7).isInteger());
	CHECK(AptOps::increment(F(1.5f), 7).asFloat() == 2.5f);
	CHECK(AptOps::increment(S("5"), 7).asFloat() == 6.0f);
	CHECK(AptOps::decrement(I(4), 7).asInteger() == 3);
	CHECK(AptOps::increment(AptValue(), 7).isUndefined());
	CHECK(AptOps::decrement(AptValue(), 7).isUndefined());
	CHECK(AptOps::increment(AptValue(), 6).asFloat() == 1.0f);   // SWF 6: toNumber(undefined) + 1
	CHECK(AptOps::increment(AptValue(), 6).isFloat());
	CHECK(AptOps::decrement(AptValue(), 6).asFloat() == -1.0f);
}

TEST_CASE("ToString and ToNumber opcodes (Rva008C9B70StackString.cpp)")
{
	CHECK(AptOps::toStringOp(F(2.5f), 7).asString() == "2.500000");
	CHECK(AptOps::toStringOp(AptValue(), 7).asString() == "undefined"); // SWF 7
	CHECK(AptOps::toStringOp(AptValue(), 6).asString() == "");
	CHECK(AptOps::toStringOp(S("s"), 7).asString() == "s");
	CHECK(AptOps::toNumberOp(S("2.5"), 7).asFloat() == 2.5f);
	CHECK(AptOps::toNumberOp(I(3), 7).isInteger());
}

TEST_CASE("object model: case-insensitive members, prototype chain, arrays, mark and sweep")
{
	AptGC gc;
	AptObject *proto = gc.create<AptObject>();
	AptObject *o = gc.create<AptObject>();
	o->setProto(proto);
	proto->setMember("Shared", I(1));
	o->setMember("Own", S("x"));
	AptValue v;
	REQUIRE(o->getMember("OWN", v));
	CHECK(v.asString() == "x");
	REQUIRE(o->getMember("shared", v)); // through the prototype
	CHECK(v.asInteger() == 1);
	o->setMember("OWN", I(2));          // same slot, spelling kept from the first assignment
	std::vector<std::string> names;
	o->ownNames(names);
	REQUIRE(names.size() == 1);
	CHECK(names[0] == "Own");
	CHECK(o->deleteOwn("own"));
	CHECK_FALSE(o->hasMember("own"));

	AptArray *arr = gc.create<AptArray>();
	arr->setMember("2", S("c"));
	CHECK(arr->items.size() == 3);
	REQUIRE(arr->getMember("length", v));
	CHECK(v.asInteger() == 3);
	CHECK(arr->items[0].isUndefined());
	arr->setMember("length", I(1));
	CHECK(arr->items.size() == 1);

	// mark and sweep: a reference cycle that nothing roots is freed, a rooted graph survives
	AptObject *x = gc.create<AptObject>();
	AptObject *y = gc.create<AptObject>();
	x->setMember("y", AptValue::object(y));
	y->setMember("x", AptValue::object(x));
	AptObject *keep = gc.create<AptObject>();
	AptObject *child = gc.create<AptObject>();
	keep->setMember("child", AptValue::object(child));
	std::size_t before = gc.objectCount();
	std::size_t freed = gc.collect([&](AptGC &g) { g.mark(keep); });
	CHECK(freed == before - 2);          // keep and child remain
	CHECK(gc.objectCount() == 2);
	AptValue c2;
	REQUIRE(keep->getMember("child", c2));
	CHECK(c2.asObject() == child);
}

// ---- the EA property hash table -----------------------------------------------------------------
// External expected values: the hash constants 0x6BBD ("__proto__") and 0x0699 ("prototype") are asserted by
// the BFME2 AptNativeHash constructor (0x00B0A740, decompiled in Open-BFME-2 AptNativeHashBFME2.cpp:81-108).
// The slot layouts below come from an independent model of the table written from the disassembly of Set
// 0x00B0AC90, Find 0x00B0AF90, Remove 0x00B0B2C0 and resize 0x00B0ABC0 (engine/tests/data/apt/apt_hash_model.py, run
// separately; its output is pasted here as constants).

TEST_CASE("property hash (BFME2 0x00AD3D10): FNV-1a over lowercased bytes, 16 bits")
{
	CHECK(AptPropertyMap::hash16("__proto__") == 0x6BBD);
	CHECK(AptPropertyMap::hash16("prototype") == 0x0699);
	CHECK(AptPropertyMap::hash16("PROTOTYPE") == 0x0699); // case-insensitive
	CHECK(AptPropertyMap::hash16("one") == 0x19EF);
	CHECK(AptPropertyMap::hash16("two") == 0x8829);
	CHECK(AptPropertyMap::hash16("three") == 0x03C3);
	CHECK(AptPropertyMap::hash16("four") == 0xF5A5);
	CHECK(AptPropertyMap::hash16("five") == 0x4395);
	CHECK(AptPropertyMap::hash16("shared") == 0x44D4);
	CHECK(AptPropertyMap::hash16("own") == 0xAFBF);
}

TEST_CASE("property hash: slot layout, probing window, growth and tombstones follow the BFME2 table")
{
	// five names in a size-8 table: home slots one=7 two=1 three=3 four=5 five=5 -> five probes forward to 6
	AptPropertyMap a;
	for (const char *n : { "one", "two", "three", "four", "five" })
	{
		a.set(n, I(1));
	}
	CHECK(a.tableSize() == 8);
	CHECK(a.slotOf("two") == 1);
	CHECK(a.slotOf("three") == 3);
	CHECK(a.slotOf("four") == 5);
	CHECK(a.slotOf("five") == 6);
	CHECK(a.slotOf("one") == 7);
	std::vector<std::string> names;
	a.enumerate(names);
	CHECK(names == std::vector<std::string>{ "two", "three", "four", "five", "one" });

	// ten names overflow the 8 slots: the table doubles to 16 and live entries are re-inserted in old slot order
	AptPropertyMap b;
	for (const char *n : { "one", "two", "three", "four", "five", "six", "seven", "a", "b", "c" })
	{
		b.set(n, I(1));
	}
	CHECK(b.tableSize() == 16);
	names.clear();
	b.enumerate(names);
	CHECK(names == std::vector<std::string>{ "c", "three", "four", "five", "b", "two", "six", "a", "seven", "one" });
	CHECK(b.slotOf("c") == 2);
	CHECK(b.slotOf("two") == 9);
	CHECK(b.slotOf("one") == 15);

	// twenty-one names grow the table to 32
	AptPropertyMap c;
	for (const char *n : { "one", "two", "three", "four", "five", "six", "seven", "a", "b", "c", "x", "y", "z", "name", "value", "shared", "own", "alpha", "beta", "gamma", "delta" })
	{
		c.set(n, I(1));
	}
	CHECK(c.tableSize() == 32);
	names.clear();
	c.enumerate(names);
	CHECK(names == std::vector<std::string>{ "delta", "three", "four", "b", "x", "name", "two", "value", "six", "a", "z", "alpha", "one", "beta", "gamma", "c", "shared", "y", "five", "seven", "own" });

	// tombstones: Remove blanks the key but the slot still counts as used for probing; Set only reuses a
	// tombstone that sat at the home slot (the compiled condition at 0x00B0ADE4)
	AptPropertyMap d;
	for (const char *n : { "one", "two", "three", "four", "five" })
	{
		d.set(n, I(1));
	}
	CHECK(d.erase("four"));
	names.clear();
	d.enumerate(names);
	CHECK(names == std::vector<std::string>{ "two", "three", "five", "one" });
	d.set("b", I(1)); // home slot 5 is the tombstone; the scan finds empty slot 4 first and takes it
	CHECK(d.slotOf("b") == 4);
	d.set("four", I(1)); // home slot 5 still a tombstone; first empty slot found by the scan is 2
	CHECK(d.slotOf("four") == 2);
	names.clear();
	d.enumerate(names);
	CHECK(names == std::vector<std::string>{ "two", "four", "three", "b", "five", "one" });

	// a full table with one tombstone at the home slot of a new key reuses it instead of growing
	AptPropertyMap e;
	for (const char *n : { "one", "two", "three", "four", "five", "six", "seven", "a" })
	{
		e.set(n, I(1));
	}
	CHECK(e.tableSize() == 8);
	CHECK(e.erase("one")); // slot 7
	e.set("x", I(1));      // home slot 7 is that tombstone and no slot is empty
	CHECK(e.tableSize() == 8);
	CHECK(e.slotOf("x") == 7);
	names.clear();
	e.enumerate(names);
	CHECK(names == std::vector<std::string>{ "a", "two", "seven", "three", "six", "four", "five", "x" });
}

TEST_CASE("property hash: prototype is a separate field, natives are never enumerated, empty names are ignored")
{
	AptGC gc;
	AptObject *o = gc.create<AptObject>();
	o->setMember("one", I(1));
	o->setMember("Prototype", I(5)); // case-insensitive match of the reserved name
	o->setNativeMember("builtin", I(7));
	o->setMember("", I(9));          // SetMember ignores an empty key (0x00B0B432)
	AptValue v;
	REQUIRE(o->getMember("PROTOTYPE", v));
	CHECK(v.asInteger() == 5);
	REQUIRE(o->getMember("builtin", v));
	CHECK(v.asInteger() == 7);
	CHECK_FALSE(o->hasMember(""));
	CHECK(o->props.size() == 1);
	CHECK(o->props.slotOf("prototype") == -1); // never occupies a slot
	std::vector<std::string> names;
	o->ownNames(names);
	CHECK(names == std::vector<std::string>{ "one" });
	CHECK(o->deleteOwn("prototype"));
	CHECK_FALSE(o->hasMember("prototype"));
	CHECK(o->hasMember("builtin"));
}

TEST_CASE("for-in order (BFME2 0x00B00170): own slots then the __proto__ chain, reserved names skipped, no de-duplication")
{
	AptGC gc;
	AptObject *base = gc.create<AptObject>();
	base->setMember("shared", I(1)); // slot 4
	base->setMember("own", I(2));    // slot 7
	AptObject *mid = gc.create<AptObject>();
	mid->setProto(base);
	mid->setMember("three", I(3));
	AptObject *o = gc.create<AptObject>();
	o->setProto(mid);
	for (const char *n : { "one", "two", "three", "four", "five" })
	{
		o->setMember(n, I(1));
	}
	o->setMember("prototype", I(9));
	AptObject *other = gc.create<AptObject>();
	o->setMember("__proto__", AptValue::object(mid)); // the link itself is not a member either
	std::vector<std::string> names;
	std::string error;
	REQUIRE(o->enumerateForIn(names, error));
	// own: two three four five one; mid: three (repeated, EA does not de-duplicate); base: shared own
	CHECK(names == std::vector<std::string>{ "two", "three", "four", "five", "one", "three", "shared", "own" });
	CHECK(error.empty());
	(void)other;

	AptValue link;
	REQUIRE(o->getOwn("__proto__", link));
	CHECK(link.asObject() == mid);

	AptArray *arr = gc.create<AptArray>();
	names.clear();
	CHECK_FALSE(arr->enumerateForIn(names, error));
	CHECK(error.find("acceptance stop") != std::string::npos);
}

TEST_CASE("UTF-8 validation: truncated, stray and invalid bytes are rejected, standard sequences accepted")
{
	std::string why;
	CHECK(AptUtf8Validate("", why));
	CHECK(AptUtf8Validate("abc", why));
	CHECK(AptUtf8Validate("\xC3\xA9", why));
	CHECK(AptUtf8Validate("\xE2\x82\xAC", why));
	CHECK(AptUtf8Validate("\xF0\x9F\x98\x80", why));
	CHECK_FALSE(AptUtf8Validate(std::string("\xF0\x80", 2), why));
	CHECK(why.find("truncated") != std::string::npos);
	CHECK_FALSE(AptUtf8Validate("\x80", why));
	CHECK_FALSE(AptUtf8Validate("\xC3z", why));
	CHECK(why.find("continuation") != std::string::npos);
	CHECK_FALSE(AptUtf8Validate("\xFF", why));
}
