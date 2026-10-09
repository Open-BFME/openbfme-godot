// OpenBFME unit tests. GPL-3.0.
// Shared helpers of the HORDE-1 tests: the golden TSV readers and the retail corpus scanner.

#pragma once

#include "GameLogic/Module/HordeContain.h"
#include "IniTestUtil.h"
#include "RetailTestMount.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace horde1
{

struct TableRow
{
	int index;
	std::string token;
	std::uint32_t parse, user, offset;
};

inline std::vector<std::string> readGoldenLines(const std::string &name)
{
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/horde1/" + name, bytes, &error), error);
	std::vector<std::string> lines;
	std::string cur;
	for (unsigned char c : bytes)
	{
		if (c == '\n')
		{
			lines.push_back(cur);
			cur.clear();
		}
		else
		{
			cur.push_back((char)c);
		}
	}
	if (!cur.empty())
	{
		lines.push_back(cur);
	}
	return lines;
}

inline std::vector<TableRow> readGoldenTable(const std::string &name)
{
	std::vector<TableRow> rows;
	for (const std::string &line : readGoldenLines(name))
	{
		std::istringstream in(line);
		TableRow r;
		std::string parse, user, off;
		std::getline(in, parse, '\t');
		r.index = std::stoi(parse);
		std::getline(in, r.token, '\t');
		std::getline(in, parse, '\t');
		std::getline(in, user, '\t');
		std::getline(in, off, '\t');
		r.parse = (std::uint32_t)std::stoul(parse, nullptr, 16);
		r.user = (std::uint32_t)std::stoul(user, nullptr, 16);
		r.offset = (std::uint32_t)std::stoul(off, nullptr, 16);
		rows.push_back(r);
	}
	return rows;
}

inline std::vector<std::string> readGoldenList(const std::string &name)
{
	std::vector<std::string> out;
	for (const std::string &line : readGoldenLines(name))
	{
		out.push_back(line.substr(line.find('\t') + 1));
	}
	return out;
}

inline std::vector<std::string> namesOfList(const char *const *list)
{
	std::vector<std::string> out;
	for (; *list; ++list)
	{
		out.push_back(*list);
	}
	return out;
}

inline std::string lowerCopy(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

inline std::vector<std::string> splitLines(const std::vector<std::uint8_t> &bytes)
{
	std::vector<std::string> lines;
	std::string cur;
	for (std::uint8_t c : bytes)
	{
		if (c == '\n')
		{
			if (!cur.empty() && cur.back() == '\r')
			{
				cur.pop_back();
			}
			lines.push_back(cur);
			cur.clear();
		}
		else
		{
			cur.push_back((char)c);
		}
	}
	lines.push_back(cur);
	return lines;
}

// First whitespace/'='-delimited token of a line with its ';' comment removed ("" for blank lines).
inline std::string firstTokenNoComment(const std::string &line)
{
	const std::string body = line.substr(0, line.find(';'));
	const size_t b = body.find_first_not_of(" \t=");
	if (b == std::string::npos)
	{
		return std::string();
	}
	const size_t e = body.find_first_of(" \t=", b);
	return body.substr(b, e == std::string::npos ? std::string::npos : e - b);
}

inline size_t indentOf(const std::string &line)
{
	size_t n = 0;
	while (n < line.size() && (line[n] == ' ' || line[n] == '\t'))
	{
		++n;
	}
	return n;
}

// The second token (after the first and any '=') of a line, ';' comment removed.
inline std::string secondTokenNoComment(const std::string &line)
{
	const std::string body = line.substr(0, line.find(';'));
	size_t b = body.find_first_not_of(" \t=");
	if (b == std::string::npos)
	{
		return std::string();
	}
	size_t e = body.find_first_of(" \t=", b);
	if (e == std::string::npos)
	{
		return std::string();
	}
	b = body.find_first_not_of(" \t=", e);
	if (b == std::string::npos)
	{
		return std::string();
	}
	e = body.find_first_of(" \t=", b);
	return body.substr(b, e == std::string::npos ? std::string::npos : e - b);
}

// A module body found in a retail INI: the lines after the `Behavior = <Class> <Tag>` header up to and
// including the first `End` whose indentation is not deeper than the header's.
struct ModuleBody
{
	std::string file;
	std::string objectName; // the enclosing `Object` / `ChildObject` / `ObjectReskin` name
	std::string cls;
	std::string tag;
	int headerLine = 0;
	std::string text; // lines after the header, ending with the module's End
};

// Scans every INI file for `<kind> = <className> <tag>` module headers whose class is in `classes`
// (compared case-insensitively on the first token after '=').
inline std::vector<ModuleBody> scanModuleBodies(Win32BIGFileSystem *fsys, const FilenameList &files, const std::vector<std::string> &kinds,
	const std::vector<std::string> &classes)
{
	std::vector<ModuleBody> out;
	for (const std::string &file : files)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(fsys->readFile(file, bytes, &error), error);
		const std::vector<std::string> lines = splitLines(bytes);
		std::string object;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			const std::string tok = lowerCopy(firstTokenNoComment(lines[i]));
			if ((tok == "object" || tok == "childobject" || tok == "objectreskin") && indentOf(lines[i]) == 0)
			{
				object = secondTokenNoComment(lines[i]);
			}
			bool isKind = false;
			for (const std::string &k : kinds)
			{
				if (tok == k)
				{
					isKind = true;
				}
			}
			if (!isKind)
			{
				continue;
			}
			// `Behavior = HordeContain ModuleTag_x`: the class is the second token (the '=' is a separator)
			const std::string cls = secondTokenNoComment(lines[i]);
			bool match = false;
			for (const std::string &c : classes)
			{
				if (lowerCopy(cls) == lowerCopy(c))
				{
					match = true;
				}
			}
			if (!match)
			{
				continue;
			}
			ModuleBody body;
			body.file = file;
			body.objectName = object;
			body.cls = cls;
			body.headerLine = (int)i + 1;
			// tag = third token
			{
				std::string rest = lines[i].substr(0, lines[i].find(';'));
				size_t b = rest.find_first_not_of(" \t=");
				for (int skip = 0; skip < 2 && b != std::string::npos; ++skip)
				{
					size_t e = rest.find_first_of(" \t=", b);
					b = e == std::string::npos ? std::string::npos : rest.find_first_not_of(" \t=", e);
				}
				if (b != std::string::npos)
				{
					size_t e = rest.find_first_of(" \t=", b);
					body.tag = rest.substr(b, e == std::string::npos ? std::string::npos : e - b);
				}
			}
			const size_t headerIndent = indentOf(lines[i]);
			bool closed = false;
			for (size_t j = i + 1; j < lines.size() && !closed; ++j)
			{
				body.text += lines[j] + "\n";
				if (lowerCopy(firstTokenNoComment(lines[j])) == "end" && indentOf(lines[j]) <= headerIndent)
				{
					closed = true;
				}
			}
			REQUIRE_MESSAGE(closed, "unterminated module at " << file << ":" << body.headerLine);
			out.push_back(std::move(body));
		}
	}
	return out;
}

// The shared retail world for the HORDE-1 corpus tests: pure 2.01 mount, the global #define table
// filled by a pre-pass over every INI (what retail's INI::load does before parsing blocks).
struct Corpus
{
	bool available = false;
	Win32BIGFileSystem *fsys = nullptr;
	INIEnvironment env;
	FilenameList files; // every Data\*.ini
};

inline Corpus &corpus()
{
	static Corpus c;
	static bool built = false;
	if (built)
	{
		return c;
	}
	built = true;
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		return c;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	c.available = true;
	c.fsys = mount->fs.get();
	c.env.fileSystem = c.fsys;
	c.fsys->getFileListInDirectory(std::string(), "Data\\", "*.ini", c.files, true);
	REQUIRE(c.files.size() > 600);
	INI pre(c.env);
	for (const std::string &f : c.files)
	{
		REQUIRE_NOTHROW(pre.preprocessFile(f, INI_LOAD_OVERWRITE));
	}
	return c;
}


struct StopSentinel
{
};

struct ParsedModule
{
	ModuleBody body;
	HordeContainModuleData data;
	std::string error;
};

// Parses one retail module body through the real field tables. The extent is whatever the field parsers
// consume (retail's rule): the synthetic file holds a window of the lines after the header, the handler
// parses, records where it stopped, and throws a sentinel that the loader turns into code 8.
inline ParsedModule parseRetailModule(Corpus &c, const ModuleBody &body)
{
	ParsedModule out;
	out.body = body;
	static HordeContainModuleData *target = nullptr;
	static int stoppedAtLine = 0;
	target = &out.data;
	stoppedAtLine = 0;
	if (!c.env.blocks.contains("HordeBody"))
	{
		c.env.blocks.registerBlock("HordeBody", [](INI *ini) {
			target->parseFromINI(ini);
			stoppedAtLine = ini->currentSourceLine();
			throw StopSentinel();
		});
	}
	INI ini(c.env);
	try
	{
		ini.loadMemory(body.file + "#module", initest::toBytes("HordeBody\n" + body.text), INI_LOAD_OVERWRITE);
		out.error = "block handler returned without the sentinel";
	}
	catch (const INIException &e)
	{
		// the sentinel surfaces as the loader's code 8 "Unknown error parsing INI block"; anything else is a parse error
		if (!(e.code() == 8 && stoppedAtLine != 0 && e.message().find("Unknown error parsing INI block 'HordeBody'") == 0))
		{
			out.error = e.message();
		}
	}
	return out;
}

// Every `Behavior = HordeContain | HorseHordeContain` header of the corpus with a body window of the next 400
// lines (the longest module is far shorter; the field parsers decide where the module really ends).
inline std::vector<ModuleBody> collectHordeBodies(Corpus &c)
{
	std::vector<ModuleBody> bodies;
	for (const std::string &file : c.files)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(c.fsys->readFile(file, bytes, &error), error);
		const std::vector<std::string> lines = splitLines(bytes);
		std::string object;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			const std::string tok = lowerCopy(firstTokenNoComment(lines[i]));
			if ((tok == "object" || tok == "childobject" || tok == "objectreskin") && indentOf(lines[i]) == 0)
			{
				object = secondTokenNoComment(lines[i]);
			}
			if (tok != "behavior")
			{
				continue;
			}
			const std::string cls = lowerCopy(secondTokenNoComment(lines[i]));
			if (cls != "hordecontain" && cls != "horsehordecontain")
			{
				continue;
			}
			ModuleBody body;
			body.file = file;
			body.objectName = object;
			body.cls = cls;
			body.headerLine = (int)i + 1;
			for (size_t j = i + 1; j < lines.size() && j < i + 401; ++j)
			{
				body.text += lines[j] + "\n";
			}
			bodies.push_back(std::move(body));
		}
	}
	return bodies;
}

} // namespace horde1
