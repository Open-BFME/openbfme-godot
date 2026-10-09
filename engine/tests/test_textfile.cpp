// OpenBFME unit tests: TextFile (port-order step 1). GPL-3.0.
// Expected values come from spec ini-and-object-model.md sections 1.2, 1.3 and 2.1 (B2
// TextFile::ParseFile), not from running the code.

#include "doctest.h"

#include "Libraries/file/TextFile.h"

#include <map>
#include <string>
#include <vector>

namespace
{

std::vector<std::uint8_t> bytesOf(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

TextFile::FileReader readerFor(const std::map<std::string, std::string> &files)
{
	return [files](const std::string &path, std::vector<std::uint8_t> &out, std::string *error) {
		auto it = files.find(path);
		if (it == files.end())
		{
			if (error)
			{
				*error = "missing " + path;
			}
			return false;
		}
		out = bytesOf(it->second);
		return true;
	};
}

std::vector<std::string> texts(const TextFile &tf)
{
	std::vector<std::string> out;
	for (const TextFile::Line &l : tf.lines())
	{
		out.push_back(l.text);
	}
	return out;
}

bool parse(TextFile &tf, const std::string &content, std::string *error = nullptr, const std::map<std::string, std::string> &files = {})
{
	return tf.parseBytes("Main.ini", bytesOf(content), readerFor(files), error);
}

typedef std::vector<std::string> Strs;

} // namespace

// spec 1.2 (B2 0xA016C9)
TEST_CASE("TextFile character classes: 0 normal, 1 EOL (NUL LF CR), 2 whitespace (isspace minus LF CR), 3 ';', 4 '/'")
{
	for (int c = 0; c < 256; ++c)
	{
		int expected = 0;
		if (c == 0 || c == '\n' || c == '\r')
		{
			expected = 1;
		}
		else if (c == '\t' || c == '\v' || c == '\f' || c == ' ')
		{
			expected = 2;
		}
		else if (c == ';')
		{
			expected = 3;
		}
		else if (c == '/')
		{
			expected = 4;
		}
		CHECK_MESSAGE(TextFile::charClass((unsigned char)c) == expected, "byte " << c);
	}
	// the quote and the high bytes are ordinary characters
	CHECK(TextFile::charClass('"') == 0);
	CHECK(TextFile::charClass(0xEF) == 0);
	CHECK(TextFile::charClass(0x1F) == 0); // a control character isspace rejects
}

TEST_CASE("TextFile splits lines on LF, CR and CRLF and drops empty and whitespace-only lines (spec 1.3)")
{
	TextFile tf;
	REQUIRE(parse(tf, "A = 1\nB = 2\r\nC = 3\r   \n\t\n\nD\n"));
	CHECK(texts(tf) == Strs{ "A = 1", "B = 2", "C = 3", "D" });
}

TEST_CASE("TextFile: ';' and '//' blank the rest of the line, with no quote awareness (spec 1.3)")
{
	TextFile tf;
	REQUIRE(parse(tf, "Name = \"a ; b\"\nX = 1 // tail\nY = 2 ; tail\n; whole line\n// whole line\n   ; indented comment\nZ = 3\n"));
	// a ';' inside quotes still starts a comment; the text before it (with its trailing space) is kept
	CHECK(texts(tf) == Strs{ "Name = \"a ", "X = 1 ", "Y = 2 ", "Z = 3" });
}

// RW 0xA15C1F-0xA15D2F, B2 0xA01EC8-0xA01FD8 (Sol review P2): the in-line loop stops on '/', the
// line is emitted, the '/' is reclassified as ordinary and scanning goes on from the SAME line
// start, so the line is emitted again at its end. n lone slashes give n + 1 entries; a line that
// STARTS with the slash is only emitted once.
TEST_CASE("TextFile: a lone '/' appends the line twice (once per slash plus the end of line)")
{
	TextFile tf;
	REQUIRE(parse(tf, "UIName = Foo / Bar\n/\nA/B\nURL = a//b\nP/Q/R\n"));
	CHECK(texts(tf) == Strs{ "UIName = Foo / Bar", "UIName = Foo / Bar", "/", "A/B", "A/B", "URL = a", "P/Q/R", "P/Q/R", "P/Q/R" });
	// both entries carry the same line number
	CHECK(tf.lines()[0].lineNumber == tf.lines()[1].lineNumber);
}

TEST_CASE("TextFile: the lone-slash duplicate also happens before a comment")
{
	TextFile tf;
	REQUIRE(parse(tf, "A/B ; note\nC\n"));
	CHECK(texts(tf) == Strs{ "A/B ", "A/B ", "C" });
}

TEST_CASE("TextFile: leading whitespace then a lone '/' is an error (retail hands the parser a NULL line)")
{
	TextFile tf;
	std::string error;
	CHECK_FALSE(parse(tf, "A = 1\n  / x\n", &error));
	CHECK(error.find("NULL line") != std::string::npos);
}

TEST_CASE("TextFile: the line counter advances once per CR byte, so LF-only files report line 1 (spec 1.3)")
{
	TextFile crlf;
	REQUIRE(parse(crlf, "A\r\nB\r\n\r\nC\r\n"));
	REQUIRE(crlf.lines().size() == 3);
	CHECK(crlf.lines()[0].lineNumber == 1);
	CHECK(crlf.lines()[1].lineNumber == 2);
	CHECK(crlf.lines()[2].lineNumber == 4);

	TextFile lf;
	REQUIRE(parse(lf, "A\nB\n\nC\n"));
	REQUIRE(lf.lines().size() == 3);
	CHECK(lf.lines()[0].lineNumber == 1);
	CHECK(lf.lines()[1].lineNumber == 1);
	CHECK(lf.lines()[2].lineNumber == 1);
}

TEST_CASE("TextFile: whitespace inside a line becomes a space, the very first byte is kept (spec 1.3)")
{
	TextFile tf;
	REQUIRE(parse(tf, "\tA\t=\v1\f2\n  B\n"));
	CHECK(texts(tf) == Strs{ "\tA = 1 2", "  B" });
}

TEST_CASE("TextFile: no BOM handling, no line continuation, NUL ends a line (spec 1.3)")
{
	TextFile tf;
	const std::string content = std::string("\xEF\xBB\xBF") + "A = 1\nB = 2 \\\nC = 3\n" + std::string("D\0E\n", 4);
	REQUIRE(parse(tf, content));
	// the BOM bytes become part of the first token; the backslash joins nothing
	CHECK(texts(tf) == Strs{ std::string("\xEF\xBB\xBF") + "A = 1", "B = 2 \\", "C = 3", "D", "E" });
}

// spec 2.1
TEST_CASE("TextFile #include expands in place, relative to the including file, recursively")
{
	std::map<std::string, std::string> files;
	files["Data\\INI\\Sub\\a.inc"] = "A1\n#include \"b.inc\"\nA2\n";
	files["Data\\INI\\Sub\\b.inc"] = "B1\n";
	files["Data\\INI\\Common\\c.inc"] = "C1\n";
	TextFile tf;
	std::string error;
	const std::string main = "Before\n#include \"Sub\\a.inc\"\n\t#include \"Common\\c.inc\" ; indented, with a comment\n#include Sub\\b.inc\nAfter\n";
	REQUIRE_MESSAGE(tf.parseBytes("Data\\INI\\Main.ini", bytesOf(main), readerFor(files), &error), error);
	CHECK(texts(tf) == Strs{ "Before", "A1", "B1", "A2", "C1", "B1", "After" });
	// included lines name their own file
	REQUIRE(tf.lines().size() == 7);
	CHECK(tf.fileName(tf.lines()[0].fileIndex) == "Data\\INI\\Main.ini");
	CHECK(tf.fileName(tf.lines()[1].fileIndex) == "Data\\INI\\Sub\\a.inc");
	CHECK(tf.fileName(tf.lines()[2].fileIndex) == "Data\\INI\\Sub\\b.inc");
	CHECK(tf.fileName(tf.lines()[6].fileIndex) == "Data\\INI\\Main.ini");
}

// RW 0xA1553C (Sol review P2): the operand run stops at the first non-class-0 byte, but the operand
// is the C STRING from the run start, so only trailing quotes at the end of the run are NUL-ed and
// a '/' does not cut the path.
TEST_CASE("TextFile include operand extraction follows RW 0xA1553C")
{
	std::string op;
	REQUIRE(TextFile::findIncludeOperand("#include \"Common\\c.inc\" ; note", 260, op));
	CHECK(op == "Common\\c.inc"); // the run ends at the space; the quote before it is NUL-ed
	REQUIRE(TextFile::findIncludeOperand("#include Sub/b.inc", 260, op));
	CHECK(op == "Sub/b.inc"); // '/' ends the class-0 run but the string goes on
	REQUIRE(TextFile::findIncludeOperand("#include \"Sub/b.inc\"", 260, op));
	CHECK(op == "Sub/b.inc\""); // a quote after a '/' is outside the run and stays
	REQUIRE(TextFile::findIncludeOperand("#include \t \"\"\"x.inc\"\"\"", 260, op));
	CHECK(op == "x.inc"); // leading quotes skipped, every trailing quote NUL-ed
	REQUIRE(TextFile::findIncludeOperand("#include a.inc ; trailing text", 260, op));
	CHECK(op == "a.inc ; trailing text"); // unquoted: the comment stays in the path
	CHECK_FALSE(TextFile::findIncludeOperand("#includ x", 260, op));
	CHECK_FALSE(TextFile::findIncludeOperand("K#include x", 5, op)); // beyond the copy limit
}

TEST_CASE("TextFile: an unquoted include with '/' works; the lone-slash rule makes it fire twice")
{
	std::map<std::string, std::string> files;
	files["Sub\\b.inc"] = "B1\n";
	TextFile tf;
	std::string error;
	REQUIRE_MESSAGE(parse(tf, "#include Sub/b.inc\nTail = 1\n", &error, files), error);
	CHECK(texts(tf) == Strs{ "B1", "B1", "Tail = 1" });
}

// RW 0xA15C27-0xA15C3F, 0xA15557-0xA1555E (Sol review round 2): the copy limit is
// min(260, n - (lineStart + 1)) and the copy stops one byte short of it. For a short unquoted
// include on the LAST line of a file that costs: nothing with a CRLF terminator, one operand byte
// with LF, two with no terminator. (No retail include sits on a file's last line.)
TEST_CASE("TextFile: a last-line include keeps its operand with CRLF, loses one byte with LF, two with no terminator")
{
	std::map<std::string, std::string> files;
	files["x.inc"] = "FULL\n";
	files["x.in"] = "LOST1\n";
	files["x.i"] = "LOST2\n";
	TextFile crlf, lf, none;
	std::string error;
	REQUIRE_MESSAGE(parse(crlf, "#include x.inc\r\n", &error, files), error);
	CHECK(texts(crlf) == Strs{ "FULL" });
	REQUIRE_MESSAGE(parse(lf, "#include x.inc\n", &error, files), error);
	CHECK(texts(lf) == Strs{ "LOST1" });
	REQUIRE_MESSAGE(parse(none, "#include x.inc", &error, files), error);
	CHECK(texts(none) == Strs{ "LOST2" });
	// with a following line there is no loss at all
	TextFile more;
	REQUIRE_MESSAGE(parse(more, "#include x.inc\nTail\n", &error, files), error);
	CHECK(texts(more) == Strs{ "FULL", "Tail" });
}

// Sol review round 2: lone-slash entries share one buffer (RW 0xA158E9-0xA158FE)
TEST_CASE("TextFile: lone-slash duplicates carry one shared buffer id; other lines have their own")
{
	TextFile tf;
	REQUIRE(parse(tf, "A/B\nPlain\nC/D/E\n"));
	REQUIRE(tf.lines().size() == 6);
	CHECK(tf.lines()[0].bufferId == tf.lines()[1].bufferId);
	CHECK(tf.lines()[2].bufferId != tf.lines()[0].bufferId);
	CHECK(tf.lines()[3].bufferId == tf.lines()[4].bufferId);
	CHECK(tf.lines()[4].bufferId == tf.lines()[5].bufferId);
	CHECK(tf.lines()[3].bufferId != tf.lines()[0].bufferId);
}

TEST_CASE("TextFile: a quoted include with '/' keeps the trailing quote and fails to open")
{
	std::map<std::string, std::string> files;
	files["Sub\\b.inc"] = "B1\n";
	TextFile tf;
	std::string error;
	CHECK_FALSE(parse(tf, "#include \"Sub/b.inc\"\nTail = 1\n", &error, files));
	CHECK(error.find("Could not open include") != std::string::npos);
	CHECK(error.find("Sub\\b.inc\"") != std::string::npos);
}

// RW 0xA15B58-0xA15C3F (Sol review P2): the line is emitted when the in-line loop stops on ';', before
// the comment is blanked, and the include scan reads the raw bytes up to the line end.
TEST_CASE("TextFile: an include after a ';' on the same line fires, and replaces the line")
{
	std::map<std::string, std::string> files;
	files["x.inc"] = "X\n";
	TextFile tf;
	std::string error;
	REQUIRE_MESSAGE(parse(tf, "Foo = 1 ; #include \"x.inc\"\nBar = 2 // #include \"x.inc\"\nBaz = 3\n", &error, files), error);
	CHECK(texts(tf) == Strs{ "X", "X", "Baz = 3" });
}

TEST_CASE("TextFile #include operand: '..' folds, '/' becomes '\\', trailing quotes are stripped")
{
	std::string out;
	REQUIRE(TextFile::resolveIncludePath("Data\\INI\\Campaigns\\RiskCampaign.ini", "Scenarios\\WOTRTutorial.inc", out));
	CHECK(out == "Data\\INI\\Campaigns\\Scenarios\\WOTRTutorial.inc");
	REQUIRE(TextFile::resolveIncludePath("a\\b\\c.ini", "..\\d.inc", out));
	CHECK(out == "a\\d.inc");
	REQUIRE(TextFile::resolveIncludePath("Data\\INI\\x\\y.inc", "../Common/z.inc", out));
	CHECK(out == "Data\\INI\\Common\\z.inc");
	REQUIRE(TextFile::resolveIncludePath("top.ini", "z.inc", out)); // no directory part
	CHECK(out == "z.inc");
	// Sol review round 2: excess ".." is not an error; the real shlwapi calls give "\x.inc"
	REQUIRE(TextFile::resolveIncludePath("a\\b.ini", "..\\..\\x.inc", out));
	CHECK(out == "\\x.inc");
	// drive-qualified operands replace the directory; a rooted operand loses its leading backslash
	REQUIRE(TextFile::resolveIncludePath("a\\b.ini", "C:\\x\\y.inc", out));
	CHECK(out == "C:\\x\\y.inc");
	REQUIRE(TextFile::resolveIncludePath("a\\b.ini", "D:x.inc", out));
	CHECK(out == "D:x.inc");
	REQUIRE(TextFile::resolveIncludePath("C:\\a\\b.ini", "\\x.inc", out));
	CHECK(out == "C:\\a\\x.inc");
	// forms outside the ported shlwapi domain are refused, not guessed
	CHECK_FALSE(TextFile::resolveIncludePath("a\\b.ini", "\\\\srv\\share\\x.inc", out));
	CHECK_FALSE(TextFile::resolveIncludePath("a\\b.ini", "x\\\\y.inc", out));
}

TEST_CASE("TextFile #include is case sensitive and found anywhere in the first 260 bytes of the line")
{
	std::map<std::string, std::string> files;
	files["x.inc"] = "X\n";
	TextFile tf;
	std::string error;
	REQUIRE_MESSAGE(parse(tf, "#INCLUDE \"x.inc\"\nKey = #include x.inc\nTail = 1\n", &error, files), error);
	// upper case is not a directive and stays an ordinary line; a mid-line directive fires and
	// the whole line is replaced by the included file
	CHECK(texts(tf) == Strs{ "#INCLUDE \"x.inc\"", "X", "Tail = 1" });

	// beyond the 260-byte scratch buffer the directive is invisible
	TextFile far;
	const std::string longLine = "K" + std::string(300, ' ') + "#include \"x.inc\"\n";
	REQUIRE(parse(far, longLine, &error, files));
	REQUIRE(far.lines().size() == 1);
	CHECK(far.lines()[0].text.find("#include") != std::string::npos);
}

// A line that STARTS with a comment never reaches the emit step, so retail's commented-out includes
// (riskcampaign.ini line 10 ";#include", line 96 "//#include") do not fire (RW 0xA159AA).
TEST_CASE("TextFile: a whole-line commented-out include does not fire")
{
	TextFile tf;
	std::string error;
	// no files at all: if either comment fired the parse would fail with 'Could not open include'
	REQUIRE_MESSAGE(parse(tf, ";#include \"Scenarios\\WOTRTutorial.inc\"\n//#include \"Common\\LivingWorldCityArmies.inc\"\nReal = 1\n", &error), error);
	CHECK(texts(tf) == Strs{ "Real = 1" });
}

TEST_CASE("TextFile: circular include (case-insensitive) and missing include are errors")
{
	std::map<std::string, std::string> files;
	files["a.inc"] = "#include \"b.inc\"\n";
	files["b.inc"] = "#include \"A.INC\"\n";
	TextFile tf;
	std::string error;
	CHECK_FALSE(tf.parseBytes("Main.ini", bytesOf("#include \"a.inc\"\n"), readerFor(files), &error));
	CHECK(error.find("Circular include") != std::string::npos);

	TextFile self;
	CHECK_FALSE(self.parseBytes("Main.ini", bytesOf("#include \"main.ini\"\n"), readerFor({}), &error));
	CHECK(error.find("Circular include") != std::string::npos);

	TextFile missing;
	CHECK_FALSE(missing.parseBytes("Main.ini", bytesOf("#include \"nope.inc\"\n"), readerFor({}), &error));
	CHECK(error.find("Could not open include") != std::string::npos);
}
