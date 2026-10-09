// OpenBFME unit tests: macros and the math evaluator (port-order steps 4 and 5). GPL-3.0.
// Expected values come from spec ini-and-object-model.md sections 2.2-2.4. The hash values are
// the output of the spec's own scratch script (workspace/rebuild/scratch/ini-spec/macrohash.py),
// the independent oracle for the hash; the same script produced the collision pairs.

#include "doctest.h"
#include "IniTestUtil.h"

#include <cmath>
#include <cstdint>

using namespace initest;

namespace
{

// Loads `file` once (no blocks are registered, so it may only hold #defines) and returns the error.
std::string defineOnly(INIEnvironment &env, const std::string &file, const std::string &text, int *code = nullptr, INILoadType type = INI_LOAD_OVERWRITE)
{
	return loadError(env, file, text, type, code);
}

float real(INIEnvironment &env, const std::string &first, const std::string &rest = "")
{
	// Evaluates `first` as the first token of a line followed by `rest`, the way a field parser
	// would: the line is "Key <first> <rest>".
	INI ini(env);
	float result = 0;
	std::string error;
	env.blocks = INIBlockRegistry();
	env.blocks.registerBlock("M", [&](INI *i) {
		i->readLine();
		i->firstToken(); // Key
		const std::string token = i->getNextToken();
		result = i->scanReal(token.c_str());
	});
	ini.loadMemory("m.ini", toBytes("M x\nKey " + first + " " + rest + "\n"), INI_LOAD_OVERWRITE);
	return result;
}

int integer(INIEnvironment &env, const std::string &text)
{
	INI ini(env);
	int result = 0;
	env.blocks = INIBlockRegistry();
	env.blocks.registerBlock("M", [&](INI *i) {
		i->readLine();
		i->firstToken();
		const std::string token = i->getNextToken();
		result = i->scanInt(token.c_str());
	});
	ini.loadMemory("m.ini", toBytes("M x\nKey " + text + "\n"), INI_LOAD_OVERWRITE);
	return result;
}

unsigned unsignedValue(INIEnvironment &env, const std::string &text)
{
	INI ini(env);
	unsigned result = 0;
	env.blocks = INIBlockRegistry();
	env.blocks.registerBlock("M", [&](INI *i) {
		i->readLine();
		i->firstToken();
		const std::string token = i->getNextToken();
		result = i->scanUnsignedInt(token.c_str());
	});
	ini.loadMemory("m.ini", toBytes("M x\nKey " + text + "\n"), INI_LOAD_OVERWRITE);
	return result;
}

std::string mathError(INIEnvironment &env, const std::string &text, int *code = nullptr)
{
	try
	{
		real(env, text);
	}
	catch (const INIException &e)
	{
		if (code)
		{
			*code = e.code();
		}
		return e.message();
	}
	return std::string();
}

} // namespace

// ------------------------------------------------------------------------------------------------
// step 4: the hash and the table
// ------------------------------------------------------------------------------------------------
// The spec (and the first port) used BFME1's xor/shift hash. The target (RW 0x42B6C1-0x42B6D8,
// 0x42BC44, 0x42C6EF-0x42C72B; BFME2 0x42BF8C, 0x42BA61, 0x42C9E7) lower-cases the name, hashes
// h = h * 5 + (signed char)c, and compares COMPLETE names in the bucket chain. The expected hash
// values below were computed independently in python from that definition.
TEST_CASE("macro hash: lower-case the name, h = h*5 + (signed char)c (RW 0x42B6C1, 0x42BC44)")
{
	CHECK(INIMacroTable::hash("") == 0x0u);
	CHECK(INIMacroTable::hash("A") == 0x61u); // 'a'
	CHECK(INIMacroTable::hash("a") == 0x61u);
	CHECK(INIMacroTable::hash("AB") == 0x247u); // 97*5 + 98
	CHECK(INIMacroTable::hash("ABC") == 0xBC6u);
	CHECK(INIMacroTable::hash("AF") == 0x24Bu);
	CHECK(INIMacroTable::hash("GZ") == 0x27Du);
	CHECK(INIMacroTable::hash("GLORFINDEL_LVL3_EXP_NEEDED") == 0x573A75F6u);
	CHECK(INIMacroTable::hash("Glorfindel_LVL3_EXP_NEEDED") == 0x573A75F6u); // same: lower-cased first
	CHECK(INIMacroTable::hash("ISENGARD_TAVERN_BUILDCOST") == 0x51DAB394u);
	CHECK(INIMacroTable::hash("Isengard_TAVERN_BUILDCOST") == 0x51DAB394u);
	// bytes >= 0x80 are signed: 0xE9 0xE8 is -23, -24, and the 32-bit sum wraps
	CHECK(INIMacroTable::hash("\xE9\xE8") == 0xFFFFFF75u);
}

TEST_CASE("macro hash: the BFME1 alias pairs from the spec are not aliases in RotWK")
{
	// spec 2.3 claimed these collide (BFME1 hash). Under the RotWK hash they differ, and even a
	// real collision would not merge entries because whole names are compared.
	CHECK(INIMacroTable::hash("Upgrade_ArmorAttribute02") == 0x7DBCAF66u);
	CHECK(INIMacroTable::hash("WILD_MARAUDER_VISION_RANGE") == 0x0160B00Fu);
	CHECK(INIMacroTable::hash("MOWBase") == 0x20A64Eu);
	CHECK(INIMacroTable::hash("MORDOR_ARMOR_SCALAR") == 0x9C4A3398u);

	Fixture fx;
	REQUIRE(loadError(fx.env, "gamedata.ini", "#define WILD_MARAUDER_VISION_RANGE 250\n").empty());
	INI ini(fx.env);
	const char *token = "Upgrade_ArmorAttribute02";
	CHECK(ini.preprocessMacro(token) == token); // no false expansion
}

TEST_CASE("macro lookup is case-insensitive by complete name: mixed-case references resolve (RW 0x42BC44)")
{
	Fixture fx;
	REQUIRE(loadError(fx.env, "gamedata.ini", "#define GLORFINDEL_LVL3_EXP_NEEDED 150000\n").empty());
	INI ini(fx.env);
	CHECK(ini.scanInt("GLORFINDEL_LVL3_EXP_NEEDED") == 150000);
	CHECK(ini.scanInt("Glorfindel_LVL3_EXP_NEEDED") == 150000); // retail relies on this
	CHECK(std::string(ini.preprocessMacro("glorfindel_lvl3_exp_needed")) == "150000");
	// a prefix or an extension of a macro name is a different name
	const char *longer = "GLORFINDEL_LVL3_EXP_NEEDED2";
	CHECK(ini.preprocessMacro(longer) == longer);
	const char *shorter = "GLORFINDEL_LVL3_EXP_NEEDE";
	CHECK(ini.preprocessMacro(shorter) == shorter);
}

TEST_CASE("macros whose names hash alike are distinct entries: #define AF 1 then #define GZ 2 is valid (Sol review P1)")
{
	// the BFME1 hash gave AF and GZ the same value (0x82) and rejected the second define
	Fixture fx;
	REQUIRE_MESSAGE(loadError(fx.env, "a.ini", "#define AF 1\n#define GZ 2\n").empty(), "AF then GZ must both define");
	CHECK(fx.env.macros.size() == 2);
	INI ini(fx.env);
	CHECK(ini.scanInt("AF") == 1);
	CHECK(ini.scanInt("GZ") == 2);
	// a genuine hash collision under the RotWK hash: "ab" and "B?" are not both uppercase-legal, so
	// drive the table directly with two names of equal hash (97*5+98 = 583; 'b'*5 + 'X'-... find one)
	INIMacroTable t;
	// 'a' 'b' -> 583 ; 'b' 'X' (lower 'x'=120): 98*5+120 = 610 ; need 583 = 98*5 + 93 -> c = ']' (93)
	REQUIRE(INIMacroTable::hash("AB") == INIMacroTable::hash("B]"));
	CHECK(t.define("AB", "1", false));
	CHECK(t.define("B]", "2", false)); // same hash, different name: a second entry
	CHECK(t.find("AB")->value == "1");
	CHECK(t.find("B]")->value == "2");
}

TEST_CASE("macro substitution: a leading digit skips the lookup, '-' and '.' do not (RW/B2 0x42D0A9)")
{
	Fixture fx;
	REQUIRE(loadError(fx.env, "g.ini", "#define 1ST 111\n#define -DASH 222\n#define .DOT 333\n").empty());
	INI ini(fx.env);
	CHECK(ini.scanInt("1ST") == 1);    // digit fast path: never looked up (then sscanf reads the 1)
	CHECK(ini.scanInt("-DASH") == 222); // '-' skips the fast path and is looked up
	CHECK(ini.scanInt(".DOT") == 333);  // so does '.'
	CHECK(ini.scanInt("1") == 1);
}

TEST_CASE("INIMacroTable: a duplicate name (any case) is rejected, load type 5 overwrites (spec 2.2)")
{
	INIMacroTable t;
	CHECK(t.define("AAZ", "1", false));
	CHECK_FALSE(t.define("aaz", "2", false)); // same name, different case
	CHECK(t.size() == 1);
	CHECK(t.find("AAZ")->value == "1");
	CHECK(t.define("aaz", "2", true));
	CHECK(t.find("AAZ")->value == "2");
}

// ------------------------------------------------------------------------------------------------
// step 4: the #define pre-pass
// ------------------------------------------------------------------------------------------------
TEST_CASE("#define: column 0 only; the line is emptied; the table is global across files (spec 2.2)")
{
	Fixture fx;
	CHECK(defineOnly(fx.env, "a.ini", "#define ONE 1\n").empty());
	CHECK(defineOnly(fx.env, "b.ini", "#define TWO 2\n").empty());
	CHECK(fx.env.macros.size() == 2);
	INI ini(fx.env);
	CHECK(ini.scanInt("ONE") + ini.scanInt("TWO") == 3);

	// an indented #define is an ordinary line: it reaches the block dispatcher and fails there
	int code = 0;
	const std::string err = defineOnly(fx.env, "c.ini", "  #define THREE 3\n", &code);
	CHECK(code == 5);
	CHECK(err.find("Unknown block '#define'.") == 0);
	CHECK(fx.env.macros.size() == 2);
}

// Sol review round 2: the lone-slash duplicate entries share one text buffer (RW 0xA158E9-0xA158FE) and
// #define clears its first byte (RW 0x42D104), so the second entry is empty and the macro defines once
TEST_CASE("#define with a lone slash in its value defines once, not twice")
{
	Fixture fx;
	std::string err = defineOnly(fx.env, "t.ini", "#define PATH A/B\n");
	CHECK_MESSAGE(err.empty(), err);
	REQUIRE(fx.env.macros.find("PATH") != nullptr);
	CHECK(fx.env.macros.find("PATH")->value == "A/B");
	CHECK(fx.env.macros.size() == 1);

	// the INI CRC hook still sees both entries, both empty
	INI ini(fx.env);
	std::vector<std::string> seen;
	ini.setLineHook([&seen](const char *text, size_t length) { seen.push_back(std::string(text, length)); });
	ini.loadMemory("u.ini", toBytes("#define PATH2 X/Y/Z\n"), INI_LOAD_OVERWRITE);
	CHECK(seen == std::vector<std::string>{ "", "", "", "" }); // 2 slashes: 3 entries, plus the empty end-of-file line
	CHECK(fx.env.macros.find("PATH2")->value == "X/Y/Z");
}

TEST_CASE("#define is a pre-pass: a macro defined at the bottom of a file is usable above it (spec 0.1 fact 2)")
{
	Fixture fx;
	int value = 0;
	fx.env.blocks.registerBlock("V", [&](INI *ini) {
		ini->readLine();
		ini->firstToken();
		value = ini->scanInt(ini->getNextToken());
		ini->readLine(); // End
	});
	REQUIRE(loadError(fx.env, "t.ini", "V x\nKey LATE\nEnd\n#define LATE 99\n").empty());
	CHECK(value == 99);
}

TEST_CASE("#define: names must be upper case, but the check is 'a' < c < 'z' exclusive (spec 2.2 item 1.4)")
{
	Fixture fx;
	int code = 0;
	std::string err = defineOnly(fx.env, "t.ini", "#define Name 5\n", &code);
	CHECK(code == 8);
	CHECK(err.find("t.ini:\nMACRO names must use UPPERCASE letters.\n") == 0);
	CHECK(defineOnly(fx.env, "t.ini", "#define AbcX 5\n", &code).find("UPPERCASE") != std::string::npos);
	// 'a' and 'z' themselves pass; digits, '_' and capitals pass
	CHECK(defineOnly(fx.env, "t.ini", "#define A_a_Z_z_9 5\n").empty());
	CHECK(fx.env.macros.findByName("A_a_Z_z_9") != nullptr);
}

TEST_CASE("#define: empty value, duplicate name and the map.ini ban (spec 2.2 items 1.3, 1.6, 1.7)")
{
	Fixture fx;
	int code = 0;
	std::string err = defineOnly(fx.env, "t.ini", "#define LONELY\n", &code);
	CHECK(code == 8);
	CHECK(err.find("t.ini:\nError parsing MACRO.\nLONELY has no value") == 0);

	REQUIRE(defineOnly(fx.env, "a.ini", "#define TWICE 1\n").empty());
	err = defineOnly(fx.env, "b.ini", "#define TWICE 2\n", &code);
	CHECK(code == 8);
	CHECK(err.find("b.ini:\nDuplicate MACRO names.\nTWICE.") == 0);
	// within one file too
	err = defineOnly(fx.env, "c.ini", "#define SAME 1\n#define SAME 2\n", &code);
	CHECK(err.find("Duplicate MACRO names.") != std::string::npos);
	// the same name in another case is a duplicate; different names are not (even if their hashes agree)
	REQUIRE(defineOnly(fx.env, "d.ini", "#define AAZ 1\n").empty());
	CHECK(defineOnly(fx.env, "e.ini", "#define aaz 1\n").find("Duplicate MACRO names.") != std::string::npos);

	// map.ini: byte compare on the file name, case sensitive
	err = defineOnly(fx.env, "Data\\Maps\\Test\\map.ini", "#define NOT_ALLOWED 1\n", &code);
	CHECK(code == 8);
	CHECK(err.find("Data\\Maps\\Test\\map.ini:\nMACROs not allowed in map.ini.\n") == 0);
	CHECK(defineOnly(fx.env, "Data\\Maps\\Test\\Map.ini", "#define ALLOWED 1\n").empty());
}

TEST_CASE("#define value is every remaining token joined with single spaces (B2/RW; B1 kept only the first) (spec 2.2 item 1.5)")
{
	Fixture fx;
	REQUIRE(defineOnly(fx.env, "t.ini",
		"#define AWARD_BASE_RING_HERO 120  (with 110% award increase)\n"
		"#define MULTI \t a   b\tc  ; comment is already gone\n"
		"#define FILTER OBJECTFILTER_A OBJECTFILTER_B\n").empty());
	CHECK(fx.env.macros.findByName("AWARD_BASE_RING_HERO")->value == "120 (with 110% award increase)");
	CHECK(fx.env.macros.findByName("MULTI")->value == "a b c");
	INI ini(fx.env);
	CHECK(ini.scanInt("AWARD_BASE_RING_HERO") == 120); // sscanf reads the prefix
}

TEST_CASE("#define under load type 5 overwrites silently (spec 2.2 item 1.7)")
{
	Fixture fx;
	REQUIRE(defineOnly(fx.env, "t.ini", "#define RELOADED 1\n").empty());
	REQUIRE(defineOnly(fx.env, "t.ini", "#define RELOADED 2\n", nullptr, INI_LOAD_RELOAD).empty());
	CHECK(fx.env.macros.findByName("RELOADED")->value == "2");
}

TEST_CASE("macros inside included files define before the including file is parsed (spec 2.2: pre-pass runs after include expansion)")
{
	Fixture fx({ { "data\\ini\\common.inc", "#define FROM_INC 7\n" } });
	REQUIRE(defineOnly(fx.env, "Data\\INI\\Main.ini", "#include \"Common.inc\"\n").empty());
	CHECK(fx.env.macros.findByName("FROM_INC")->value == "7");
}

// ------------------------------------------------------------------------------------------------
// step 5: the math evaluator
// ------------------------------------------------------------------------------------------------
TEST_CASE("#ADD and #MULTIPLY take any number of operands (spec 2.4)")
{
	Fixture fx;
	CHECK(real(fx.env, "#ADD(", "1 2 3 4 )") == 10.0f);
	CHECK(real(fx.env, "#ADD(", "5 )") == 5.0f);
	CHECK(real(fx.env, "#MULTIPLY(", "2 3 4 )") == 24.0f);
	CHECK(real(fx.env, "#MULTIPLY(", "7 )") == 7.0f);
	CHECK(integer(fx.env, "#ADD( 1 2 3 4 5 )") == 15);
	CHECK(integer(fx.env, "#MULTIPLY( 2 3 4 5 )") == 120);
	CHECK(unsignedValue(fx.env, "#ADD( 10 20 30 )") == 60u);
}

TEST_CASE("#SUBTRACT and #DIVIDE take exactly two operands, code 8 otherwise (spec 2.4)")
{
	Fixture fx;
	CHECK(real(fx.env, "#SUBTRACT(", "10 4 )") == 6.0f);
	CHECK(real(fx.env, "#DIVIDE(", "10 4 )") == 2.5f);
	CHECK(integer(fx.env, "#SUBTRACT( 10 4 )") == 6);
	CHECK(integer(fx.env, "#DIVIDE( 7 2 )") == 3);     // idiv truncates toward zero
	CHECK(integer(fx.env, "#DIVIDE( -7 2 )") == -3);
	CHECK(unsignedValue(fx.env, "#DIVIDE( 7 2 )") == 3u);

	int code = 0;
	CHECK(mathError(fx.env, "#SUBTRACT( 1 2 3 )", &code) == "#SUBTRACT takes only 2 operands\n\nError parsing INI block 'M' in file 'm.ini'.");
	CHECK(code == 8);
	CHECK(mathError(fx.env, "#DIVIDE( 1 2 3 )", &code).find("#DIVIDE takes only 2 operands") == 0);
	// too few operands: ")" is read as a value
	CHECK(mathError(fx.env, "#SUBTRACT( 1 )", &code).find("Expected floating point value, math op, or predefined macro, but found ')'") == 0);
	CHECK(mathError(fx.env, "#ADD( )", &code).find("but found ')'") != std::string::npos);
}

TEST_CASE("math syntax: operator and '(' are one token, ')' is its own token, operator names are case sensitive (spec 2.4)")
{
	Fixture fx;
	int code = 0;
	CHECK(mathError(fx.env, "#ADD ( 1 2 )", &code).find("Expected known math operation after #, but found '#ADD'") == 0);
	CHECK(code == 3);
	CHECK(mathError(fx.env, "#add( 1 2 )", &code).find("Expected known math operation after #, but found '#add('") == 0);
	CHECK(mathError(fx.env, "#POWER( 1 2 )", &code).find("but found '#POWER('") != std::string::npos);
	// ")" glued to the last operand is not a terminator: "2)" scans as 2 and the group never closes
	CHECK(mathError(fx.env, "#ADD( 1 2)", &code).find("Expected additional data after") == 0);
}

TEST_CASE("math operands may be macros, numbers or nested groups; macro values may themselves be expressions (spec 2.3, 2.4)")
{
	Fixture fx;
	REQUIRE(defineOnly(fx.env, "g.ini",
		"#define BASE 100\n"
		"#define DOUBLE_BASE #MULTIPLY( BASE 2 )\n"
		"#define SCALED #ADD( DOUBLE_BASE #MULTIPLY( 3 4 ) 1 )\n").empty());
	CHECK(real(fx.env, "#ADD(", "BASE 5 )") == 105.0f);
	CHECK(real(fx.env, "DOUBLE_BASE") == 200.0f);
	CHECK(real(fx.env, "SCALED") == 213.0f);
	CHECK(integer(fx.env, "SCALED") == 213);
	// the expression continues on the line after a macro-valued first operand
	CHECK(real(fx.env, "#SUBTRACT(", "SCALED DOUBLE_BASE )") == 13.0f);
}

TEST_CASE("math: real results round to float32 after EVERY step (spec 0.1 fact 4, 2.4)")
{
	Fixture fx;
	// 16777216 + 1 does not fit float32: every step rounds back down, so three operands stay at 2^24.
	// A double accumulator rounded once would give 16777218.
	CHECK(real(fx.env, "#ADD(", "16777216 1 1 )") == 16777216.0f);
	// the same sum in the int path wraps/keeps full precision
	CHECK(integer(fx.env, "#ADD( 16777216 1 1 )") == 16777218);
	// per-step float32 product: 0.1f * 3 * 3 rounds after each multiply
	volatile float tenth = 0.1f, three = 3.0f, one = 1.0f, nearlyOne = 0.95f, fifth = 0.2f; // keep the compiler from folding
	const float step = (tenth * three) * three;
	CHECK(real(fx.env, "#MULTIPLY(", "0.1 3 3 )") == step);
	const float div = one / nearlyOne; // livingworldautoresolvehandicaps.ini: #DIVIDE( 1.0, 0.95 )
	CHECK(real(fx.env, "#DIVIDE(", "1.0, 0.95 )") == div);
	// float32 operands: 0.95 is read as the nearest float, not as a double
	CHECK(real(fx.env, "#ADD(", "0.1 0.2 )") == tenth + fifth);
}

TEST_CASE("math: no divide-by-zero check for reals (inf/NaN), integer wrap-around and division faults (spec 2.4)")
{
	Fixture fx;
	CHECK(std::isinf(real(fx.env, "#DIVIDE(", "1 0 )")));
	CHECK(std::isnan(real(fx.env, "#DIVIDE(", "0 0 )")));
	CHECK(integer(fx.env, "#ADD( 2147483647 1 )") == (int)0x80000000u);          // 32-bit add wraps
	CHECK(integer(fx.env, "#MULTIPLY( 65536 65536 )") == 0);                     // imul wraps
	CHECK(unsignedValue(fx.env, "#SUBTRACT( 0 1 )") == 4294967295u);
	// retail raises a CPU exception; here the failure is an INIException (never a silent value)
	INI ini(fx.env);
	fx.env.blocks = INIBlockRegistry();
	fx.env.blocks.registerBlock("M", [](INI *i) {
		i->readLine();
		i->firstToken();
		i->scanInt(i->getNextToken());
	});
	CHECK_THROWS_AS(ini.loadMemory("m.ini", toBytes("M x\nKey #DIVIDE( 1 0 )\n"), INI_LOAD_OVERWRITE), INIException);
}

TEST_CASE("math evaluates inside the scanners that field parsers call: scanBool and string vectors do not (spec 2.3)")
{
	Fixture fx;
	REQUIRE(defineOnly(fx.env, "g.ini", "#define HALF #MULTIPLY( 50 0.01 )\n").empty());
	CHECK(real(fx.env, "HALF") == 50.0f * 0.01f);
	INI ini(fx.env);
	// scanBool expands macros but has no math: "#MULTIPLY(" is just a bad token
	CHECK_THROWS_AS(ini.scanBool("HALF"), INIException);
}
