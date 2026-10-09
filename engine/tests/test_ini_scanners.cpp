// OpenBFME unit tests: INI scanners and field parsers (port-order step 3). GPL-3.0.
// Expected values come from spec ini-and-object-model.md section 1.8 and the cited B1/B2
// sources, not from running the code.

#include "doctest.h"
#include "IniTestUtil.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace initest;

namespace
{

// Runs `Key <value>` through `proc` into `store`; returns the error message ("" when it parsed).
std::string parseInto(INIEnvironment &env, INIFieldParseProc proc, const std::string &value, void *store, const void *userData = nullptr, int *code = nullptr)
{
	struct Ctx
	{
		void *store;
	} ctx{ store };
	struct Row
	{
		FieldParse table[2];
	} row;
	row.table[0] = { "Key", proc, userData, 0 };
	row.table[1] = { nullptr, nullptr, nullptr, 0 };
	env.blocks = INIBlockRegistry();
	env.blocks.registerBlock("P", [&](INI *ini) { ini->initFromINI(ctx.store, row.table); });
	return loadError(env, "t.ini", "P x\nKey " + value + "\nEnd\n", INI_LOAD_OVERWRITE, code);
}

template <typename T>
T parseOk(INIFieldParseProc proc, const std::string &value, const void *userData = nullptr)
{
	Fixture fx;
	T store{};
	const std::string err = parseInto(fx.env, proc, value, &store, userData);
	REQUIRE_MESSAGE(err.empty(), "Key " << value << ": " << err);
	return store;
}

template <typename T>
std::string parseErr(INIFieldParseProc proc, const std::string &value, const void *userData = nullptr, int *code = nullptr)
{
	Fixture fx;
	T store{};
	return parseInto(fx.env, proc, value, &store, userData, code);
}

} // namespace

TEST_CASE("scanInt / scanUnsignedInt / scanReal read a prefix with sscanf; trailing junk is accepted (spec 1.8)")
{
	Fixture fx;
	INI ini(fx.env);
	CHECK(ini.scanInt("42") == 42);
	CHECK(ini.scanInt("-7") == -7);
	CHECK(ini.scanInt("110%") == 110); // retail relies on this: percent in a numeric context reads as 110
	CHECK(ini.scanInt("2.9") == 2);
	CHECK(ini.scanUnsignedInt("120") == 120u);
	CHECK(ini.scanUnsignedInt("-1") == 4294967295u); // %u accepts a sign
	CHECK(ini.scanReal("1.0,") == 1.0f);             // `#DIVIDE( 1.0, 0.95 )` in livingworldautoresolvehandicaps.ini
	CHECK(ini.scanReal(".5") == 0.5f);
	CHECK(ini.scanReal("-2.5") == -2.5f);
}

TEST_CASE("scanInt / scanReal / scanUnsignedInt errors carry the BFME2 texts and the expanded text (spec 1.8)")
{
	Fixture fx;
	INI ini(fx.env);
	fx.env.macros.define("NOT_A_NUMBER", "oops", false);
	try
	{
		ini.scanInt("abc");
		FAIL("no exception");
	}
	catch (const INIException &e)
	{
		CHECK(e.code() == 3);
		CHECK(e.message() == "Expected signed integer value, math op, or predefined macro, but found 'abc'");
	}
	try
	{
		ini.scanReal("NOT_A_NUMBER");
		FAIL("no exception");
	}
	catch (const INIException &e)
	{
		// the message shows the EXPANDED text
		CHECK(e.message() == "Expected floating point value, math op, or predefined macro, but found 'oops'");
	}
	try
	{
		ini.scanUnsignedInt("x");
		FAIL("no exception");
	}
	catch (const INIException &e)
	{
		CHECK(e.message() == "Expected unsigned integer value, math op, or predefined macro, but found 'x'");
	}
}

TEST_CASE("scanBool: yes/no case-insensitive, macros expand, the error shows the ORIGINAL token (spec 1.8)")
{
	Fixture fx;
	INI ini(fx.env);
	fx.env.macros.define("SHIP_IT", "Yes", false);
	fx.env.macros.define("BAD_FLAG", "maybe", false);
	CHECK(ini.scanBool("Yes"));
	CHECK(ini.scanBool("yEs"));
	CHECK_FALSE(ini.scanBool("NO"));
	CHECK(ini.scanBool("SHIP_IT"));
	try
	{
		ini.scanBool("BAD_FLAG");
		FAIL("no exception");
	}
	catch (const INIException &e)
	{
		CHECK(e.code() == 3);
		CHECK(e.message() == "invalid boolean token BAD_FLAG -- expected Yes or No");
	}
}

TEST_CASE("parseBool stores through scanBool; parseBitInInt32 sets and clears its mask")
{
	CHECK(parseOk<bool>(INI::parseBool, "Yes") == true);
	CHECK(parseOk<bool>(INI::parseBool, "no") == false);
	Fixture fx;
	unsigned bits = 0x0Fu;
	REQUIRE(parseInto(fx.env, INI::parseBitInInt32, "No", &bits, (const void *)(std::uintptr_t)0x02u).empty());
	CHECK(bits == 0x0Du);
	REQUIRE(parseInto(fx.env, INI::parseBitInInt32, "Yes", &bits, (const void *)(std::uintptr_t)0x10u).empty());
	CHECK(bits == 0x1Du);
}

TEST_CASE("integer range checks (spec 1.8): out of range is the plain int 1, wrapped as code 8")
{
	CHECK(parseOk<std::int8_t>(INI::parseByte, "-128") == -128);
	CHECK(parseOk<std::int8_t>(INI::parseByte, "127") == 127);
	CHECK(parseOk<std::uint8_t>(INI::parseUnsignedByte, "255") == 255);
	CHECK(parseOk<std::int16_t>(INI::parseShort, "-32768") == -32768);
	CHECK(parseOk<std::uint16_t>(INI::parseUnsignedShort, "65535") == 65535);

	int code = 0;
	CHECK(parseErr<std::int8_t>(INI::parseByte, "128", nullptr, &code).find("Unknown error parsing field 'Key'") == 0);
	CHECK(code == 8);
	CHECK_FALSE(parseErr<std::int8_t>(INI::parseByte, "-129").empty());
	CHECK_FALSE(parseErr<std::uint8_t>(INI::parseUnsignedByte, "-1").empty());
	CHECK_FALSE(parseErr<std::uint8_t>(INI::parseUnsignedByte, "256").empty());
	CHECK_FALSE(parseErr<std::int16_t>(INI::parseShort, "32768").empty());
	CHECK_FALSE(parseErr<std::uint16_t>(INI::parseUnsignedShort, "65536").empty());
	CHECK(parseOk<int>(INI::parseInt, "-5") == -5);
	CHECK(parseOk<unsigned>(INI::parseUnsignedInt, "7") == 7u);
	CHECK(parseOk<float>(INI::parseReal, "2.25") == 2.25f);
}

TEST_CASE("parsePositiveNonZeroReal rejects zero and negatives (B1 ini_parsers.cpp)")
{
	int code = 0;
	const std::string err = parseErr<float>(INI::parsePositiveNonZeroReal, "0", nullptr, &code);
	CHECK(code == 3);
	CHECK(err.find("invalid Real value 0.0000000 -- expected > 0") == 0);
	CHECK(parseOk<float>(INI::parsePositiveNonZeroReal, "0.5") == 0.5f);
}

TEST_CASE("parsePercentToReal: '%' is a separator, scanReal * 0.01f; a macro or math works; math operands see default separators (spec 1.8)")
{
	CHECK(parseOk<float>(INI::parsePercentToReal, "50%") == 0.5f);
	CHECK(parseOk<float>(INI::parsePercentToReal, "110%") == 110.0f * 0.01f);
	CHECK(parseOk<float>(INI::parsePercentToReal, "100") == 100.0f * 0.01f); // no % sign is fine
	// RotWK divides nothing: 0.01f multiplication, not ZH's / 100.0f
	CHECK(parseOk<float>(INI::parsePercentToReal, "7%") == 7.0f * 0.01f);

	Fixture fx;
	fx.env.macros.define("HALF_POINT", "50", false);
	float v = 0;
	REQUIRE(parseInto(fx.env, INI::parsePercentToReal, "HALF_POINT%", &v).empty());
	CHECK(v == 0.5f);
	// `50%` inside a math expression is read with the default separators: scanReal("50%") == 50
	REQUIRE(parseInto(fx.env, INI::parsePercentToReal, "#MULTIPLY( 2 50% )", &v).empty());
	CHECK(v == 100.0f * 0.01f);
}

TEST_CASE("angle, velocity, acceleration, angular velocity constants (spec 1.8; B2src, B1 GameCommonConvertThunk.cpp)")
{
	CHECK(parseOk<float>(INI::parseAngleReal, "180") == doctest::Approx(3.14159265f).epsilon(1e-6));
	CHECK(parseOk<float>(INI::parseAngleReal, "90") == 90.0f * 0.017453292f);
	CHECK(parseOk<float>(INI::parseVelocityReal, "30") == 6.0f);           // 0.2f * 30: 30 units/s is 6 per logic frame at 5 fps
	CHECK(parseOk<float>(INI::parseVelocityReal, "25") == 5.0f);
	volatile float f = 0.2f;
	const float sq = f * f;
	CHECK(parseOk<float>(INI::parseAccelerationReal, "100") == sq * 100.0f); // (0.2f*0.2f)*v, squared at run time
	CHECK(parseOk<float>(INI::parseAccelerationReal, "100") == doctest::Approx(4.0f).epsilon(1e-6));
	CHECK(parseOk<float>(INI::parseAngularVelocityReal, "90") == 90.0f * (f * 0.017453292f)); // B1 0x000B94F0: deg * (0.2f * RADS_PER_DEGREE)
}

TEST_CASE("durations: milliseconds to frames at 5 logic fps; integer forms round UP (spec 0.1 fact 6, 1.8)")
{
	// not the 30 fps Zero Hour numbers
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "1000") == 5u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "33") == 1u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "0") == 0u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "1") == 1u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "199") == 1u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "200") == 1u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "201") == 2u);
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "12345") == 62u);
	// two different parsers: the unsigned form rounds UP (500 ms is 2.5 frames -> 3), the real form
	// keeps the fraction (RW 0x73A429 vs 0x73A403; coordinator correction to the brief)
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "500") == 3u);
	CHECK(parseOk<std::uint16_t>(INI::parseDurationUnsignedShort, "500") == 3);
	// PLAN rule 2: the integer is never rounded to float32 first
	CHECK(parseOk<unsigned>(INI::parseDurationUnsignedInt, "52428805") == 262145u);
	CHECK(parseOk<std::uint16_t>(INI::parseDurationUnsignedShort, "12345") == 62);
	CHECK(parseOk<float>(INI::parseDurationReal, "1000") == 5.0f);
	CHECK(parseOk<float>(INI::parseDurationReal, "500") == 2.5f);
	CHECK(parseOk<float>(INI::parseDurationReal, "100") == 0.005f * 100.0f);

	// spec 1.8: ceil(0.005f * ms) == ceil(ms / 200) for every integer ms < 2^24. Checked with
	// exact integer arithmetic against the parser for the range gameplay mostly uses (0..6000 ms) and for the
	// multiples of 200 (where float32 rounding could push the product just above the integer).
	Fixture fx;
	INI ini(fx.env);
	for (unsigned ms = 0; ms <= 6000; ++ms)
	{
		const unsigned expected = (ms + 199u) / 200u;
		unsigned got = 0;
		fx.env.blocks = INIBlockRegistry();
		fx.env.blocks.registerBlock("D", [&](INI *i) {
			const FieldParse t[] = { { "Key", INI::parseDurationUnsignedInt, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
			i->initFromINI(&got, t);
		});
		ini.loadMemory("d.ini", toBytes("D x\nKey " + std::to_string(ms) + "\nEnd\n"), INI_LOAD_OVERWRITE);
		if (got != expected)
		{
			FAIL("ms=" << ms << " frames=" << got << " expected=" << expected);
		}
	}
	for (unsigned k = 1; k <= 80000; k += 997)
	{
		const unsigned ms = k * 200u;
		unsigned got = 0;
		fx.env.blocks = INIBlockRegistry();
		fx.env.blocks.registerBlock("D", [&](INI *i) {
			const FieldParse t[] = { { "Key", INI::parseDurationUnsignedInt, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
			i->initFromINI(&got, t);
		});
		ini.loadMemory("d.ini", toBytes("D x\nKey " + std::to_string(ms) + "\nEnd\n"), INI_LOAD_OVERWRITE);
		CHECK_MESSAGE(got == k, "multiple of 200: ms=" << ms);
	}
}

TEST_CASE("seconds-to-millis truncates: (int)(scanReal * 1000.0f) (B2 0x42F0F7)")
{
	CHECK(parseOk<int>(INI::parseSecondsToMillis, "1.5") == 1500);
	CHECK(parseOk<int>(INI::parseSecondsToMillis, "0.0019") == 1);
	CHECK(parseOk<int>(INI::parseSecondsToMillis, "2") == 2000);
}

TEST_CASE("index lists and lookup lists compare with stricmp and report BFME texts (spec 1.6)")
{
	static const char *names[] = { "ZERO", "ONE", "TWO", nullptr };
	CHECK(parseOk<int>(INI::parseIndexList, "one", names) == 1);
	CHECK(parseOk<int>(INI::parseIndexList, "TWO", names) == 2);
	CHECK(parseOk<std::uint8_t>(INI::parseByteSizedIndexList, "Two", names) == 2);
	int code = 0;
	const std::string err = parseErr<int>(INI::parseIndexList, "three", names, &code);
	CHECK(code == 3);
	CHECK(err.find("Token 'three' is not a valid member of the index list") == 0);

	static const LookupListRec lookup[] = { { "INI", 0 }, { "STR", 7 }, { nullptr, 0 } };
	CHECK(parseOk<int>(INI::parseLookupList, "str", lookup) == 7);
	CHECK(parseErr<int>(INI::parseLookupList, "bin", lookup).find("Token 'bin' is not a valid member of the lookup list") == 0);
	CHECK(parseErr<int>(INI::parseIndexList, "x", nullptr, &code).find("INTERNAL ERROR! scanIndexList: No name list provided!") == 0);
	CHECK(code == 2);
}

TEST_CASE("bit strings: plain names clear then set, +/- edit, NONE clears, mixing throws (spec 1.8, ZH INI.cpp:903-969)")
{
	static const char *flags[] = { "A", "B", "C", "D", nullptr };
	Fixture fx;
	unsigned bits;

	bits = 0x0F;
	REQUIRE(parseInto(fx.env, INI::parseBitString32, "B D", &bits, flags).empty());
	CHECK(bits == 0x0Au); // everything cleared once, then B and D set

	bits = 0x0F;
	REQUIRE(parseInto(fx.env, INI::parseBitString32, "-A +c", &bits, flags).empty());
	CHECK(bits == 0x0Eu); // edits keep the existing value

	bits = 0x0F;
	REQUIRE(parseInto(fx.env, INI::parseBitString32, "NONE", &bits, flags).empty());
	CHECK(bits == 0u);

	bits = 0x0F;
	CHECK_FALSE(parseInto(fx.env, INI::parseBitString32, "B +C", &bits, flags).empty());
	CHECK_FALSE(parseInto(fx.env, INI::parseBitString32, "+B C", &bits, flags).empty());
	CHECK_FALSE(parseInto(fx.env, INI::parseBitString32, "B NONE", &bits, flags).empty());
	CHECK_FALSE(parseInto(fx.env, INI::parseBitString32, "-A NONE", &bits, flags).empty());
	const std::string unknown = parseInto(fx.env, INI::parseBitString32, "E", &bits, flags);
	CHECK(unknown.find("Token 'E' is not a valid member of the index list") == 0);
	CHECK(parseInto(fx.env, INI::parseBitString32, "A", &bits, nullptr).find("INTERNAL ERROR! parseBitString32") == 0);

	static const char *nine[] = { "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", nullptr };
	std::uint8_t b8 = 0;
	REQUIRE(parseInto(fx.env, INI::parseBitString8, "B7", &b8, nine).empty());
	CHECK(b8 == 0x80);
	CHECK_FALSE(parseInto(fx.env, INI::parseBitString8, "B8", &b8, nine).empty()); // bit 8 does not fit a byte
}

TEST_CASE("type flag lists start from ALL; ALL/NONE reset; +/- edit with bit = 1 << (index - first) (B1 ini.cpp:1309-1331)")
{
	static const char *names[] = { "FLAG_NONE", "SMALL_ARMS", "FLAME", "SNIPER", nullptr };
	const INITypeFlagListSpec spec{ names, 1 };
	Fixture fx;
	unsigned flags = 0;

	REQUIRE(parseInto(fx.env, INI::parseTypeFlagList, "-FLAME", &flags, &spec).empty());
	CHECK(flags == (0xFFFFFFFFu & ~2u));
	REQUIRE(parseInto(fx.env, INI::parseTypeFlagList, "NONE +SNIPER +small_arms", &flags, &spec).empty());
	CHECK(flags == 5u);
	REQUIRE(parseInto(fx.env, INI::parseTypeFlagList, "NONE ALL", &flags, &spec).empty());
	CHECK(flags == 0xFFFFFFFFu);
	int code = 0;
	const std::string err = parseInto(fx.env, INI::parseTypeFlagList, "FLAME", &flags, &spec, &code);
	CHECK(code == 5);
	CHECK(err.find("ALL, NONE, + or - expected") == 0);
}

TEST_CASE("colors and coordinates (B1 ini_parsers.cpp:208-384, ZH parseCoord*)")
{
	RGBColor c = parseOk<RGBColor>(INI::parseRGBColor, "R:255 G:0 B:51");
	CHECK(c.red == 255.0f * (1.0f / 255.0f));
	CHECK(c.green == 0.0f);
	CHECK(c.blue == 51.0f * (1.0f / 255.0f));
	CHECK(parseErr<RGBColor>(INI::parseRGBColor, "R:256 G:0 B:0").find("color value R=256 out of range (0..255)") == 0);
	CHECK(parseErr<RGBColor>(INI::parseRGBColor, "X:1 G:0 B:0").find("Expected 'R' but found 'X'") == 0);

	RGBAColorInt a = parseOk<RGBAColorInt>(INI::parseRGBAColorInt, "R:1 G:2 B:3 A:4");
	CHECK(a.red == 1);
	CHECK(a.green == 2);
	CHECK(a.blue == 3);
	CHECK(a.alpha == 4);
	CHECK(parseOk<RGBAColorInt>(INI::parseRGBAColorInt, "R:1 G:2 B:3").alpha == 255); // A may be omitted
	CHECK(parseErr<RGBAColorInt>(INI::parseRGBAColorInt, "R:1 G:2").find("can't omit value for color B") == 0);
	CHECK(parseErr<RGBAColorInt>(INI::parseRGBAColorInt, "R:1 B:2 G:3").find("expected 'G'") == 0);
	CHECK(parseOk<std::uint32_t>(INI::parseColorInt, "R:16 G:32 B:48 A:64") == 0x40102030u);

	Coord3D p = parseOk<Coord3D>(INI::parseCoord3D, "X:1.5 Y:2 Z:-3");
	CHECK(p.x == 1.5f);
	CHECK(p.y == 2.0f);
	CHECK(p.z == -3.0f);
	ICoord2D q = parseOk<ICoord2D>(INI::parseICoord2D, "X:640 Y:480");
	CHECK(q.x == 640);
	CHECK(q.y == 480);
}
