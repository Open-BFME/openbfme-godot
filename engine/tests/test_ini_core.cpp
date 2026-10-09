// OpenBFME unit tests: INI core (port-order step 2). GPL-3.0.
// Expected values come from spec ini-and-object-model.md sections 1.1, 1.4-1.7, 1.9, 3.1 and
// from the BFME1 decompile (Source/Common/INI/ini.cpp, INI_initFromINIMulti.cpp), not from
// running the code.

#include "doctest.h"
#include "IniTestUtil.h"

#include <cstddef>
#include <stdexcept>

using namespace initest;

namespace
{

struct Thing
{
	int a = 0;
	int b = 0;
	int c = 0;
	int d = 0;
	int cost = 0;
	std::uint8_t small = 0;
};

const FieldParse thingFields[] = {
	{ "A", INI::parseInt, nullptr, (int)offsetof(Thing, a) },
	{ "B", INI::parseInt, nullptr, (int)offsetof(Thing, b) },
	{ "C", INI::parseInt, nullptr, (int)offsetof(Thing, c) },
	{ "D", INI::parseInt, nullptr, (int)offsetof(Thing, d) },
	{ "BuildCost", INI::parseInt, nullptr, (int)offsetof(Thing, cost) },
	{ "Small", INI::parseUnsignedByte, nullptr, (int)offsetof(Thing, small) },
	{ nullptr, nullptr, nullptr, 0 },
};

struct Collected
{
	std::vector<Thing> things;
	std::vector<std::string> names;
};

void registerThing(INIEnvironment &env, Collected &out)
{
	env.blocks.registerBlock("Thing", [&out](INI *ini) {
		Thing t;
		out.names.push_back(ini->getNextToken());
		ini->initFromINI(&t, thingFields);
		out.things.push_back(t);
	});
}

struct Str
{
	std::string s;
};

void parseStr(INI *ini, void *, void *store, const void *)
{
	((Str *)store)->s = ini->getNextAsciiString();
}
void parseQuoted(INI *ini, void *, void *store, const void *)
{
	((Str *)store)->s = ini->getNextQuotedAsciiString();
}

// returns the string the field parser produced for `Key <value>` on one line
std::string stringField(INIFieldParseProc proc, const std::string &line)
{
	Fixture fx;
	std::string result;
	fx.env.blocks.registerBlock("S", [&](INI *ini) {
		Str str;
		const FieldParse table[] = { { "Key", proc, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
		ini->initFromINI(&str, table);
		result = str.s;
	});
	const std::string err = loadError(fx.env, "t.ini", "S x\n" + line + "\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	return result;
}

} // namespace

TEST_CASE("INI: '=' is only a separator; A = 1, A=1, A 1 and A == 1 parse the same (spec 1.4)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	const std::string err = loadError(fx.env, "t.ini", "Thing T\n A = 1\n B=2\n C  3\n\tD == 4\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	REQUIRE(out.things.size() == 1);
	CHECK(out.things[0].a == 1);
	CHECK(out.things[0].b == 2);
	CHECK(out.things[0].c == 3);
	CHECK(out.things[0].d == 4);
	CHECK(out.names[0] == "T");
}

TEST_CASE("INI: unread tokens are ignored without error (spec 1.4)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	REQUIRE(loadError(fx.env, "t.ini", "Thing T\n BuildCost = 100 200\nEnd\n").empty());
	CHECK(out.things[0].cost == 100);
}

TEST_CASE("INI: End matches case-insensitively and text after it is ignored (spec 1.6, 1.7)")
{
	for (const char *end : { "End", "END", "end", "eNd", "End trailing junk = 5", "\tEnd" })
	{
		Fixture fx;
		Collected out;
		registerThing(fx.env, out);
		const std::string err = loadError(fx.env, "t.ini", std::string("Thing T\n A = 7\n") + end + "\nThing U\n A = 8\nEnd\n");
		CHECK_MESSAGE(err.empty(), "End spelled '" << end << "': " << err);
		REQUIRE_MESSAGE(out.things.size() == 2, "End spelled '" << end << "'");
		CHECK(out.things[0].a == 7);
		CHECK(out.things[1].a == 8);
	}
}

TEST_CASE("INI: block keywords and field names are case sensitive, with the BFME error texts (spec 1.6, 1.9)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);

	int code = 0;
	std::string err = loadError(fx.env, "t.ini", "thing T\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	CHECK(err == "Unknown block 'thing'.\n\nError parsing INI block 'thing' in file 't.ini'.");

	err = loadError(fx.env, "t.ini", "Thing T\n a = 1\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	// "Unknown field '%s' in block '%s'.\n\nError parsing field ..." wrapped by the block handler
	CHECK(err.find("Unknown field 'a' in block 'Thing'.") == 0);
	CHECK(err.find("Error parsing field 'a' in block 'Thing' in file 't.ini', line 1.") != std::string::npos);
	CHECK(err.find("Error parsing INI block 'Thing' in file 't.ini'.") != std::string::npos);
}

TEST_CASE("INI: a block that runs into end of file without End is code 4 (spec 1.7)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	int code = 0;
	const std::string err = loadError(fx.env, "t.ini", "Thing T\n A = 1\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 4);
	CHECK(err.find("Missing 'END' token.") == 0);
}

TEST_CASE("INI: a missing value is 'Expected additional data after', wrapped with block, file and CR-based line (spec 1.4, 1.7)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	int code = 0;
	// CRLF: the second line is line 2 because the counter follows CR bytes
	const std::string err = loadError(fx.env, "t.ini", "Thing T\r\n A\r\nEnd\r\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(err.find("Expected additional data after ' \n\r\t='") == 0);
	CHECK(err.find("Error parsing field 'A' in block 'Thing' in file 't.ini', line 2.") != std::string::npos);

	// LF-only: every error reports line 1
	const std::string lfErr = loadError(fx.env, "t.ini", "Thing T\n A\nEnd\n");
	CHECK(lfErr.find("line 1.") != std::string::npos);
}

TEST_CASE("INI: a plain-int range error is wrapped as code 8 'Unknown error parsing field' (spec 1.8, 1.9)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	int code = 0;
	const std::string err = loadError(fx.env, "t.ini", "Thing T\n Small = 256\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 8);
	CHECK(err.find("Unknown error parsing field 'Small' in block 'Thing' in file 't.ini', line 1.") == 0);
	REQUIRE(loadError(fx.env, "t.ini", "Thing T\n Small = 255\nEnd\n").empty());
	CHECK(out.things.back().small == 255);
}

TEST_CASE("INI: a catch-all terminator row receives the field name as userData (ZH INI.cpp:324-346)")
{
	Fixture fx;
	std::vector<std::string> seen;
	struct Holder
	{
		std::vector<std::string> *seen;
	} holder{ &seen };
	static const FieldParse table[] = {
		{ "Known", INI::parseInt, nullptr, 0 },
		{ nullptr, [](INI *ini, void *instance, void *, const void *userData) {
			(void)ini;
			((Holder *)instance)->seen->push_back((const char *)userData);
		}, nullptr, 0 },
	};
	fx.env.blocks.registerBlock("C", [&](INI *ini) {
		ini->initFromINI(&holder, table);
	});
	REQUIRE(loadError(fx.env, "t.ini", "C x\n Whatever 1\n Other 2\nEnd\n").empty());
	CHECK(seen == std::vector<std::string>{ "Whatever", "Other" });
}

TEST_CASE("INI: nesting is decided by field parsers; the inner loop eats its End (spec 1.7, 3.3)")
{
	Fixture fx;
	struct Outer
	{
		Thing inner;
		int after = 0;
	} outer;
	static const FieldParse outerFields[] = {
		{ "Inner", [](INI *ini, void *, void *store, const void *) { ini->initFromINI(store, thingFields); }, nullptr, (int)offsetof(Outer, inner) },
		{ "After", INI::parseInt, nullptr, (int)offsetof(Outer, after) },
		{ nullptr, nullptr, nullptr, 0 },
	};
	fx.env.blocks.registerBlock("O", [&](INI *ini) { ini->initFromINI(&outer, outerFields); });
	const std::string err = loadError(fx.env, "t.ini", "O x\nInner\nA = 5\nEnd\nAfter = 9\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	CHECK(outer.inner.a == 5);
	CHECK(outer.after == 9);
}

TEST_CASE("INI: multi tables search in order; extra offset applies per table (ZH Include/Common/INI.h:132-157)")
{
	Fixture fx;
	struct Pair
	{
		int x = 0;
		int y = 0;
	} pair;
	static const FieldParse t1[] = { { "X", INI::parseInt, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
	static const FieldParse t2[] = { { "X", INI::parseInt, nullptr, 0 }, { "Y", INI::parseInt, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
	fx.env.blocks.registerBlock("M", [&](INI *ini) {
		MultiIniFieldParse p;
		p.add(t1, 0);
		p.add(t2, (unsigned)offsetof(Pair, y));
		ini->initFromINIMulti(&pair, p);
	});
	REQUIRE(loadError(fx.env, "t.ini", "M a\nX = 3\nY = 4\nEnd\n").empty());
	CHECK(pair.x == 3); // first table wins for X
	CHECK(pair.y == 4); // Y found in table 2 at its extra offset
}

TEST_CASE("INI: more than 16 field tables is a hard error (ZH MAX_MULTI_FIELDS)")
{
	MultiIniFieldParse p;
	for (int i = 0; i < 16; ++i)
	{
		p.add(thingFields);
	}
	CHECK_THROWS_AS(p.add(thingFields), std::logic_error);
}

TEST_CASE("INI: readLine truncates at 1027 characters silently (spec 1.1 item 3)")
{
	Fixture fx;
	std::string value;
	fx.env.blocks.registerBlock("S", [&](INI *ini) {
		Str str;
		const FieldParse table[] = { { "Key", parseStr, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
		ini->initFromINI(&str, table);
		value = str.s;
	});
	// "Key " is 4 characters, so a 1500-character value is cut to 1027 - 4 = 1023
	REQUIRE(loadError(fx.env, "t.ini", "S x\nKey " + std::string(1500, 'v') + "\nEnd\n").empty());
	CHECK(value.size() == 1023);
	// exactly 1027 characters survive intact
	REQUIRE(loadError(fx.env, "t.ini", "S x\nKey " + std::string(1023, 'w') + "\nEnd\n").empty());
	CHECK(value.size() == 1023);
	// one under the limit
	REQUIRE(loadError(fx.env, "t.ini", "S x\nKey " + std::string(1000, 'z') + "\nEnd\n").empty());
	CHECK(value.size() == 1000);
}

TEST_CASE("INI: the line hook sees every line including the empty end-of-file line (spec 1.1 item 3)")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	INI ini(fx.env);
	std::vector<std::string> seen;
	ini.setLineHook([&seen](const char *text, size_t length) { seen.push_back(std::string(text, length)); });
	ini.loadMemory("t.ini", toBytes("Thing T\n A = 1\nEnd\n"), INI_LOAD_OVERWRITE);
	CHECK(seen == std::vector<std::string>{ "Thing T", " A = 1", "End", "" });
}

TEST_CASE("INI: a block handler exception is wrapped; a foreign exception becomes code 8 (spec 1.9)")
{
	Fixture fx;
	fx.env.blocks.registerBlock("Bad", [](INI *) { throw INIException(3, "boom"); });
	fx.env.blocks.registerBlock("Worse", [](INI *) { throw std::runtime_error("plain"); });
	int code = 0;
	CHECK(loadError(fx.env, "t.ini", "Bad x\n", INI_LOAD_OVERWRITE, &code) == "boom\n\nError parsing INI block 'Bad' in file 't.ini'.");
	CHECK(code == 3);
	CHECK(loadError(fx.env, "t.ini", "Worse x\n", INI_LOAD_OVERWRITE, &code) == "Unknown error parsing INI block 'Worse' in file 't.ini'.");
	CHECK(code == 8);
}

TEST_CASE("INI: a missing file is code 7; an INI object stays usable after an error")
{
	Fixture fx;
	Collected out;
	registerThing(fx.env, out);
	INI ini(fx.env);
	try
	{
		ini.load("Data\\INI\\nope.ini", INI_LOAD_OVERWRITE);
		FAIL("expected an exception");
	}
	catch (const INIException &e)
	{
		CHECK(e.code() == 7);
		CHECK(e.message().find("INI::load, cannot open file 'Data\\INI\\nope.ini'") == 0);
	}
	CHECK_THROWS_AS(ini.loadMemory("t.ini", toBytes("nonsense\n"), INI_LOAD_OVERWRITE), INIException);
	ini.loadMemory("t.ini", toBytes("Thing T\nEnd\n"), INI_LOAD_OVERWRITE);
	CHECK(out.things.size() == 1);
}

TEST_CASE("INI: duplicate block registration is rejected")
{
	INIBlockRegistry r;
	r.registerBlock("Object", [](INI *) {});
	CHECK_THROWS_AS(r.registerBlock("Object", [](INI *) {}), std::logic_error);
	CHECK(r.find("Object") != nullptr);
	CHECK(r.find("object") == nullptr);
}

// spec 1.5 (B1 INI_getNextAsciiString_Thunk, hand-traced; ZH INI.cpp:764-803)
TEST_CASE("INI::getNextAsciiString keeps the BFME quoting algorithm byte for byte (spec 1.5)")
{
	CHECK(stringField(parseStr, "Key Foo") == "Foo");
	CHECK(stringField(parseStr, "Key = \"Hello World\"") == "Hello World");
	// a one-word quoted string loses its closing quote only when it is the last token
	CHECK(stringField(parseStr, "Key = \"Hello\"") == "Hello");
	CHECK(stringField(parseStr, "Key = \"Hello\" 5") == "Hello\"5"); // strtok already ate the space; "5" has length 1, so no space is added
	// '=' inside quotes cuts the string: token `"a`, then `b"` read with the quote separators
	CHECK(stringField(parseStr, "Key = \"a=b\"") == "ab");
	CHECK(stringField(parseStr, "Key") == ""); // no value: empty string, not an error
}

TEST_CASE("INI::getNextQuotedAsciiString strips the closing quote when the first token ends in one (spec 1.5)")
{
	CHECK(stringField(parseQuoted, "Key = \"Hi\"") == "Hi");
	CHECK(stringField(parseQuoted, "Key = \"Hello World\"") == "Hello World");
	CHECK(stringField(parseQuoted, "Key = Bare") == "Bare");
}

TEST_CASE("INI::parseAsciiStringVector clears first, Append keeps; macro values split into entries (spec 1.5)")
{
	Fixture fx;
	std::vector<std::string> keep, replace;
	static const FieldParse table[] = {
		{ "Add", INI::parseAsciiStringVectorAppend, nullptr, 0 },
		{ "Set", INI::parseAsciiStringVector, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 },
	};
	fx.env.macros.define("TWO_WORDS", "alpha beta", false);
	fx.env.blocks.registerBlock("V", [&](INI *ini) {
		ini->initFromINI(&keep, table);
	});
	REQUIRE(loadError(fx.env, "t.ini", "V x\nAdd = a b\nAdd = TWO_WORDS c\nEnd\n").empty());
	CHECK(keep == std::vector<std::string>{ "a", "b", "alpha", "beta", "c" });
	REQUIRE(loadError(fx.env, "t.ini", "V x\nAdd = z\nSet = q\nEnd\n").empty());
	CHECK(keep == std::vector<std::string>{ "q" });
}

// RW 0x42BC9D-0x42BCBA (Sol review P2): diagnostics name the file that contains the line.
TEST_CASE("INI: an error in an included file names that file and its own line number")
{
	Fixture fx(FileList{ { "Data\\INI\\bad.inc", "  Zed = 1\n" } });
	Collected out;
	registerThing(fx.env, out);
	int code = 0;
	const std::string err = loadError(fx.env, "Data\\INI\\parent.ini", "Thing T\r\n#include \"bad.inc\"\r\nEnd\r\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	CHECK(err.find("Unknown field 'Zed' in block 'Thing'.") == 0);
	CHECK(err.find("in file 'Data\\INI\\bad.inc', line 1.") != std::string::npos);
	// the block-level wrapper names the root file (B1 reads this+4 there)
	CHECK(err.find("Error parsing INI block 'Thing' in file 'Data\\INI\\parent.ini'.") != std::string::npos);

	// and a field error after the include is back in the parent
	Fixture fx2(FileList{ { "Data\\INI\\good.inc", "  B = 2\n" } });
	Collected out2;
	registerThing(fx2.env, out2);
	const std::string err2 = loadError(fx2.env, "Data\\INI\\parent.ini", "Thing T\r\n#include \"good.inc\"\r\n A = x\r\nEnd\r\n");
	CHECK(err2.find("Error parsing field 'A' in block 'Thing' in file 'Data\\INI\\parent.ini', line 3.") != std::string::npos);
}

// spec 1.4 (B2 0x42DEED getNextTokenOrNull, 0x42CBCC pushText) and 2.3 (getNextTokenPreprocess)
TEST_CASE("INI: pushText feeds the token reader before the rest of the line; readLine clears it (BFME2 pending buffer, spec 1.4)")
{
	Fixture fx;
	std::vector<std::string> seen;
	fx.env.blocks.registerBlock("T", [&](INI *ini) {
		ini->readLine();
		ini->firstToken(); // Key
		ini->pushText("x y");
		seen.push_back(ini->getNextToken()); // x
		ini->pushText("p");                  // m_pending = "p" + " " + remaining pending
		seen.push_back(ini->getNextToken()); // p
		seen.push_back(ini->getNextToken()); // y
		seen.push_back(ini->getNextToken()); // a   (pending empty: strtok on the line)
		seen.push_back(ini->getNextToken()); // b
		ini->pushText("left over");
		ini->readLine(); // clears the pending buffer
		ini->firstToken();
		seen.push_back(ini->getNextToken()); // from the new line, not "left"
	});
	REQUIRE(loadError(fx.env, "t.ini", "T h\nKey a b\nKey c\n").empty());
	CHECK(seen == std::vector<std::string>{ "x", "p", "y", "a", "b", "c" });
}

TEST_CASE("INI::getNextTokenPreprocess expands a multi-token macro to its full text (B1 INIGetNextToken.cpp:38-46)")
{
	Fixture fx;
	fx.env.macros.define("CREATE_A_HERO_INVULNERABILITY_MODIFIER", "ModifierA ModifierB", false);
	std::string first, second;
	fx.env.blocks.registerBlock("T", [&](INI *ini) {
		ini->readLine();
		ini->firstToken(); // Modifier
		first = ini->getNextTokenPreprocess();  // INVULNERABLE is not a macro: unchanged
		second = ini->getNextTokenPreprocess(); // the macro: the whole value text
	});
	REQUIRE(loadError(fx.env, "t.ini", "T h\nModifier = INVULNERABLE CREATE_A_HERO_INVULNERABILITY_MODIFIER\n").empty());
	CHECK(first == "INVULNERABLE");
	CHECK(second == "ModifierA ModifierB");
}
