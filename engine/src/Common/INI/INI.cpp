// OpenBFME. GPL-3.0.
//
// INI core: prepFile / load / readLine / parseLine, tokens, block registry, initFromINIMulti,
// loadDirectory. Citations are on each function.

#include "Common/INI.h"
#include "Common/JobSystem.h"

#include "Common/AsciiString.h"
#include "Common/NumericState.h"

#include <algorithm>
#include <exception>
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------------------------
// MultiIniFieldParse::add
// ZH Include/Common/INI.h:132-157 (MAX_MULTI_FIELDS = 16). Retail DEBUG_ASSERTs on overflow; an
// overflow here is a programming error and must not be silent.
// ---------------------------------------------------------------------------------------------
void MultiIniFieldParse::add(const FieldParse *f, unsigned e)
{
	if (m_count >= MAX_MULTI_FIELDS)
	{
		throw std::logic_error("MultiIniFieldParse: more than 16 field tables");
	}
	m_fieldParse[m_count] = f;
	m_extraOffset[m_count] = e;
	++m_count;
}

// ---------------------------------------------------------------------------------------------
// INIBlockRegistry
// B1 Source/Common/INI/ini.cpp:376-386 findBlockParse (strcmp over the registration list).
// ---------------------------------------------------------------------------------------------
void INIBlockRegistry::registerBlock(const std::string &token, INIBlockParse parse)
{
	if (!m_blocks.emplace(token, std::move(parse)).second)
	{
		throw std::logic_error("INIBlockRegistry: duplicate block keyword '" + token + "'");
	}
}

const INIBlockParse *INIBlockRegistry::find(const char *token) const
{
	auto it = m_blocks.find(token);
	return it == m_blocks.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------------------------
// INI construction
// B1 Source/Common/INI/ini_parsers.cpp:563-582 (separator sets); ZH INI.cpp:192-196.
// ---------------------------------------------------------------------------------------------
INI::INI(INIEnvironment &env)
	: m_env(env)
	, m_filename("None")
	, m_seps(" \n\r\t=")
	, m_sepsPercent(" \n\r\t=%")
	, m_sepsColon(" \n\r\t=:")
	, m_sepsQuote("\"\n=")
	, m_blockEndToken("END")
{
	m_buffer[0] = 0;
	std::strcpy(m_curBlockStart, "NO_BLOCK");
}

INI::~INI() = default;

// ---------------------------------------------------------------------------------------------
// prepFile
// B2 0x42D2C1-0x42D3AB; B1T attempts/0x00853610.cpp:190-206; B1 ini.cpp:325-345.
//   code 6 "INI::load, cannot open file '%s', file already open\n"
//   code 7 "INI::load, cannot open file '%s'\n"
// then TextFile::ParseFile fills the lines, then the #define pre-pass (runDefinePass).
// ---------------------------------------------------------------------------------------------
namespace
{
TextFile::FileReader archiveReader(ArchiveFileSystem *fs)
{
	return [fs](const std::string &path, std::vector<std::uint8_t> &out, std::string *error) {
		if (!fs)
		{
			if (error)
			{
				*error = "no file system mounted";
			}
			return false;
		}
		return fs->readFile(path, out, error);
	};
}
} // namespace

void INI::prepFile(const std::string &filename, INILoadType loadType, const std::vector<std::uint8_t> *bytes, const TextFile *parsed)
{
	if (m_fileOpen)
	{
		throw INIException(6, "INI::load, cannot open file '%s', file already open\n", filename.c_str());
	}

	TextFile own;
	if (!parsed)
	{
		const TextFile::FileReader reader = archiveReader(m_env.fileSystem);
		std::string error;
		const bool ok = bytes ? own.parseBytes(filename, *bytes, reader, &error) : own.parseFile(filename, reader, &error);
		if (!ok)
		{
			throw INIException(7, "INI::load, cannot open file '%s'\n%s", filename.c_str(), error.c_str());
		}
	}
	const TextFile &text = parsed ? *parsed : own;

	m_lines = text.lines();
	m_lineFileNames.clear();
	for (size_t i = 0; i < text.fileCount(); ++i)
	{
		m_lineFileNames.push_back(text.fileName((int)i));
	}
	m_filename = filename;
	m_loadType = loadType;
	m_lineNum = 0;
	m_endOfFile = false;
	m_fileOpen = true;

	try
	{
		runDefinePass();
	}
	catch (...)
	{
		unPrepFile();
		throw;
	}
}

// B1 ini.cpp:352-364 (inlined into the tail of load in retail).
void INI::unPrepFile()
{
	m_lines.clear();
	m_lineFileNames.clear();
	m_filename = "None";
	m_loadType = INI_LOAD_INVALID;
	m_lineNum = 0;
	m_endOfFile = false;
	m_fileOpen = false;
}

// ---------------------------------------------------------------------------------------------
// load
// B1 ini.cpp:464-510; B2 0x42DC75-0x42DD35. TARGET: load starts with setFPMode (RW 0x440809:
// 24-bit precision, round-to-nearest), so every float parsed during the load uses that state;
// NumericState::setFPMode does the same (the x87 precision half is emulated by float32 storage,
// see NumericState.h). Retail rethrows the first INIException without unPrepFile and leaves a dead INI object; the
// port always unpreps so one INI can be reused after an error (needed by the test harness).
// ---------------------------------------------------------------------------------------------
void INI::loadPrepared(const std::string &)
{
	try
	{
		while (m_endOfFile == false)
		{
			readLine();
			parseLine();
		}
	}
	catch (...)
	{
		unPrepFile();
		throw;
	}
	m_loadedFiles.push_back(m_filename);
	unPrepFile();
}

void INI::load(const std::string &filename, INILoadType loadType)
{
	NumericState::setFPMode();
	prepFile(filename, loadType, nullptr);
	loadPrepared(filename);
}

void INI::preprocessFile(const std::string &filename, INILoadType loadType)
{
	prepFile(filename, loadType, nullptr);
	unPrepFile();
}

void INI::loadMemory(const std::string &filename, const std::vector<std::uint8_t> &bytes, INILoadType loadType)
{
	NumericState::setFPMode();
	prepFile(filename, loadType, &bytes);
	loadPrepared(filename);
}

// ---------------------------------------------------------------------------------------------
// readLine
// B1 ini.cpp:524-542; B2 0x42D669-0x42D6F7. Copies line i into m_buffer with
// strncpy(.., 1027) and a NUL at byte 1027 (longer lines are truncated silently), sets
// m_endOfFile with an empty buffer past the last line, clears the BFME2 pending-token buffer
// and token bank, and feeds the line (including the empty EOF line) to the INI CRC hook.
// ---------------------------------------------------------------------------------------------
void INI::readLine()
{
	if (m_lineNum < (unsigned)m_lines.size())
	{
		const std::string &line = m_lines[m_lineNum++].text;
		std::strncpy(m_buffer, line.c_str(), INI_MAX_CHARS_PER_LINE - 1);
		m_buffer[INI_MAX_CHARS_PER_LINE - 1] = 0;
	}
	else
	{
		m_endOfFile = true;
		m_buffer[0] = 0;
	}
	m_pending.clear();
	m_tokenBank.clear();
	m_tokPos = nullptr;

	if (m_lineHook)
	{
		m_lineHook(m_buffer, std::strlen(m_buffer));
	}
}

int INI::currentSourceLine() const
{
	// The decompile calls bfmeAt(m_lineNum) on the line array after readLine has already
	// advanced the counter; which element that returns is not recoverable (UNVERIFIED), so the
	// diagnostics report the line that was just read.
	if (m_lineNum == 0 || m_lineNum > (unsigned)m_lines.size())
	{
		return 0;
	}
	return m_lines[m_lineNum - 1].lineNumber;
}

// TARGET: RW 0x42BC9D-0x42BCBA fetch the file of the current line from the line array, so an
// included file's errors name that file.
const std::string &INI::getFilename() const
{
	if (m_lineNum == 0 || m_lineNum > (unsigned)m_lines.size())
	{
		return m_filename;
	}
	return m_lineFileNames[(size_t)m_lines[m_lineNum - 1].fileIndex];
}

const std::string &INI::currentLineText() const
{
	static const std::string empty;
	if (m_lineNum == 0 || m_lineNum > (unsigned)m_lines.size())
	{
		return empty;
	}
	return m_lines[m_lineNum - 1].text;
}

const std::string *INI::peekNextLine() const
{
	return m_lineNum < (unsigned)m_lines.size() ? &m_lines[m_lineNum].text : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Tokens. strtok semantics over m_buffer (spec 1.4).
// ---------------------------------------------------------------------------------------------
const char *INI::tok(char *str, const char *seps)
{
	if (str)
	{
		m_tokPos = str;
	}
	if (!m_tokPos)
	{
		return nullptr;
	}
	char *s = m_tokPos;
	while (*s && std::strchr(seps, *s))
	{
		++s;
	}
	if (!*s)
	{
		m_tokPos = nullptr;
		return nullptr;
	}
	char *start = s;
	while (*s && !std::strchr(seps, *s))
	{
		++s;
	}
	if (*s)
	{
		*s = 0;
		m_tokPos = s + 1;
	}
	else
	{
		m_tokPos = s; // at the terminator: the next call finds nothing
	}
	return start;
}

const char *INI::firstToken(const char *seps)
{
	return tok(m_buffer, seps ? seps : m_seps);
}

// B1 ini.cpp:1147-1155; B2 0x42DF97: code 3 "Expected additional data after '%s'" (the
// separator set is the argument).
const char *INI::getNextToken(const char *seps)
{
	if (!seps)
	{
		seps = m_seps;
	}
	const char *token = getNextTokenOrNull(seps);
	if (!token)
	{
		throw INIException(3, "Expected additional data after '%s'", seps);
	}
	return token;
}

// B2 0x42DEED: tokens come first from the pending buffer (whitespace-delimited), each copied
// into the token bank so the pointer stays valid until the next readLine; then strtok(NULL).
const char *INI::getNextTokenOrNull(const char *seps)
{
	if (!seps)
	{
		seps = m_seps;
	}
	if (!m_pending.empty())
	{
		static const char *kPendingSeps = " \n\r\t";
		size_t b = m_pending.find_first_not_of(kPendingSeps);
		if (b == std::string::npos)
		{
			m_pending.clear();
		}
		else
		{
			size_t e = m_pending.find_first_of(kPendingSeps, b);
			if (e == std::string::npos)
			{
				e = m_pending.size();
			}
			m_tokenBank.push_back(m_pending.substr(b, e - b));
			m_pending.erase(0, e);
			return m_tokenBank.back().c_str();
		}
	}
	return tok(nullptr, seps);
}

// B2 0x42CBCC pushText: m_pending = text + " " + m_pending.
void INI::pushText(const std::string &text)
{
	m_pending = text + " " + m_pending;
}

// B1 ini_parsers.cpp getNextSubToken: tag compared with stricmp, value read with m_sepsColon.
const char *INI::getNextSubToken(const char *expected)
{
	const char *token = getNextToken(getSepsColon());
	if (AsciiStringUtil::compareNoCase(token, expected) != 0)
	{
		throw INIException(3, "Expected '%s' but found '%s'", expected, token);
	}
	return getNextToken(getSepsColon());
}

// B1 Source/Common/INI/INIGetNextToken.cpp:38-46 (getNextTokenPreprocess).
const char *INI::getNextTokenPreprocess(const char *seps)
{
	return preprocessMacro(getNextToken(seps));
}

// ---------------------------------------------------------------------------------------------
// Strings. Spec 1.5; ZH INI.cpp:764-803; B1 INI.cpp getNextAsciiString (INI_getNextAsciiString_Thunk).
// Ported byte for byte, including the odd closing-quote behaviour.
// ---------------------------------------------------------------------------------------------
std::string INI::getNextAsciiString()
{
	std::string result;
	const char *token = getNextTokenOrNull();
	if (token != nullptr)
	{
		if (token[0] != '"')
		{
			result = token;
		}
		else
		{
			std::string buff;
			if (std::strlen(token) > 1)
			{
				buff = &token[1];
			}
			const char *t = getNextTokenOrNull(getSepsQuote());
			if (t)
			{
				if (std::strlen(t) > 1 && t[1] != '\t')
				{
					buff += " ";
				}
				buff += t;
				result = buff;
			}
			else
			{
				if (!buff.empty() && buff.back() == '"')
				{
					buff.pop_back();
				}
				result = buff;
			}
		}
	}
	return result;
}

// B1 ini_parsers.cpp getNextQuotedAsciiString (the "fixed" version); ZH INI.cpp:715-760.
std::string INI::getNextQuotedAsciiString()
{
	std::string result;
	const char *token = getNextTokenOrNull();
	if (token != nullptr)
	{
		if (token[0] != '"')
		{
			result = token;
		}
		else
		{
			size_t strLen = std::strlen(token);
			bool done = false;
			std::string buff;
			if (strLen > 1)
			{
				buff = &token[1];
				// end of quoted string on the same token: skip the closing quote
				if (buff[strLen - 2] == '"')
				{
					buff.erase(strLen - 2);
					done = true;
				}
			}
			if (!done)
			{
				const char *t = getNextToken(getSepsQuote());
				if (std::strlen(t) > 1 && t[1] != '\t')
				{
					buff += " ";
					buff += t;
				}
				else
				{
					if (!buff.empty() && buff.back() == '"')
					{
						buff.pop_back();
					}
				}
			}
			result = buff;
		}
	}
	return result;
}

// ---------------------------------------------------------------------------------------------
// parseLine: top-level block dispatch.
// B1 ini.cpp:414-454; RW 0x42BDCF-0x42BE10; B2 0x42C0F5. Texts:
//   unknown keyword  code 5 "Unknown block '%s'.\n\nError parsing INI block '%s' in file '%s'."
//   parser threw     "%s\n\nError parsing INI block '%s' in file '%s'."  (same code)
//   anything else    code 8 "Unknown error parsing INI block '%s' in file '%s'."
// m_curBlockStart is strcpy'd from m_buffer AFTER strtok, so it holds only the keyword.
// The keyword is copied before dispatch: the retail code prints through a pointer into
// m_buffer, which the block parser has long since overwritten.
// ---------------------------------------------------------------------------------------------
void INI::parseLine()
{
	const char *token = firstToken(m_seps);
	if (!token)
	{
		return;
	}
	const std::string keyword = token;
	const INIBlockParse *parse = m_env.blocks.find(keyword.c_str());
	if (parse)
	{
		std::strcpy(m_curBlockStart, m_buffer);
		try
		{
			(*parse)(this);
		}
		catch (INIException &e)
		{
			throw INIException(e.code(), "%s\n\nError parsing INI block '%s' in file '%s'.", e.message().c_str(), keyword.c_str(), m_filename.c_str());
		}
		catch (...)
		{
			throw INIException(8, "Unknown error parsing INI block '%s' in file '%s'.", keyword.c_str(), m_filename.c_str());
		}
		std::strcpy(m_curBlockStart, "NO_BLOCK");
	}
	else
	{
		throw INIException(5, "Unknown block '%s'.\n\nError parsing INI block '%s' in file '%s'.", keyword.c_str(), keyword.c_str(), m_filename.c_str());
	}
}

// ---------------------------------------------------------------------------------------------
// initFromINIMulti
// ZH INI.cpp:1475-1556; B1 INI_initFromINIMulti.cpp:34-130; spec 1.7.
// Field names match with strcmp (case sensitive), End with _strcmpi against "END".
// ---------------------------------------------------------------------------------------------
void INI::initFromINI(void *what, const FieldParse *parseTable)
{
	MultiIniFieldParse p;
	p.add(parseTable);
	initFromINIMulti(what, p);
}

void INI::initFromINIMultiProc(void *what, BuildMultiIniFieldProc proc)
{
	MultiIniFieldParse p;
	(*proc)(p);
	initFromINIMulti(what, p);
}

namespace
{
// ZH INI.cpp:324-346, B1 ini_parsers.cpp findFieldParse.
INIFieldParseProc findFieldParse(const FieldParse *parseTable, const char *token, int &offset, const void *&userData)
{
	const FieldParse *parse = parseTable;
	for (; parse->token; ++parse)
	{
		if (std::strcmp(parse->token, token) == 0)
		{
			offset = parse->offset;
			userData = parse->userData;
			return parse->parse;
		}
	}
	// terminator row: token NULL but parse set means catch-all, called with the field name
	if (!parse->token && parse->parse)
	{
		offset = parse->offset;
		userData = token;
		return parse->parse;
	}
	return nullptr;
}
}

void INI::initFromINIMulti(void *what, const MultiIniFieldParse &parseTableList)
{
	if (what == nullptr)
	{
		throw INIException(1, "INI::initFromINI - Invalid parameters supplied!");
	}

	bool done = false;
	while (!done)
	{
		readLine();
		const char *fieldPtr = firstToken(m_seps);
		if (fieldPtr != nullptr)
		{
			const std::string field = fieldPtr; // nested parsers overwrite m_buffer
			if (AsciiStringUtil::compareNoCase(field, m_blockEndToken) == 0)
			{
				done = true;
			}
			else
			{
				bool found = false;
				for (int tableIndex = 0; tableIndex < parseTableList.getCount(); ++tableIndex)
				{
					int offset = 0;
					const void *userData = nullptr;
					INIFieldParseProc parse = findFieldParse(parseTableList.getNthFieldParse(tableIndex), field.c_str(), offset, userData);
					if (parse != nullptr)
					{
						try
						{
							parse(this, what, (char *)what + offset + parseTableList.getNthExtraOffset(tableIndex), const_cast<void *>(userData));
						}
						catch (INIException &e)
						{
							throw INIException(e.code(), "%s\n\nError parsing field '%s' in block '%s' in file '%s', line %i.\n", e.message().c_str(),
								field.c_str(), m_curBlockStart, getFilename().c_str(), currentSourceLine());
						}
						catch (...)
						{
							throw INIException(8, "Unknown error parsing field '%s' in block '%s' in file '%s', line %i.\n", field.c_str(), m_curBlockStart,
								getFilename().c_str(), currentSourceLine());
						}
						found = true;
						break;
					}
				}

				if (!found)
				{
					throw INIException(5, "Unknown field '%s' in block '%s'.\n\nError parsing field '%s' in block '%s' in file '%s', line %i.\n", field.c_str(),
						m_curBlockStart, field.c_str(), m_curBlockStart, getFilename().c_str(), currentSourceLine());
				}
			}
		}

		if (done == false && isEOF() == true)
		{
			throw INIException(4, "Missing '%s' token.\n\nError parsing block '%s' in file '%s', line %i.\n", m_blockEndToken, m_curBlockStart, getFilename().c_str(),
				currentSourceLine());
		}
	}
}

// ---------------------------------------------------------------------------------------------
// loadDirectory
// ZH INI.cpp:217-261; B1 INILoadDirectory.cpp:42-87; B2 0x42E63B; spec 3.4 item 4.
// "*.ini" is listed recursively into a FilenameList (std::set sorted with compareNoCase on the
// full path). Pass 1 loads files directly in the directory, in that order. Pass 2 (only when
// subdirs) loads files in subdirectories, in sorted full-path order, skipping ExcludePath
// matches.
// ---------------------------------------------------------------------------------------------
void INI::loadDirectory(std::string dirName, bool subdirs, INILoadType loadType, const INILoadDirectoryOptions &options)
{
	if (dirName.empty())
	{
		throw INIException(0, "INI::loadDirectory called with an empty directory name");
	}
	if (!m_env.fileSystem)
	{
		throw INIException(7, "INI::loadDirectory '%s': no file system mounted", dirName.c_str());
	}

	if (dirName.back() != '\\')
	{
		dirName += '\\';
	}
	FilenameList filenameList;
	m_env.fileSystem->getFileListInDirectory(std::string(), dirName, "*.ini", filenameList, true);

	auto hasSeparator = [&](const std::string &path) {
		const std::string rest = path.substr(std::min(path.size(), dirName.size()));
		return rest.find('\\') != std::string::npos || rest.find('/') != std::string::npos;
	};

	// the files to load, in load order: pass 1 the files directly in the directory, pass 2 (subdirs) the others not excluded
	std::vector<std::string> files;
	for (const std::string &file : filenameList)
	{
		if (!hasSeparator(file))
		{
			files.push_back(file);
		}
	}
	if (subdirs)
	{
		for (const std::string &file : filenameList)
		{
			if (!hasSeparator(file))
			{
				continue;
			}
			bool excluded = false;
			for (const std::string &ex : options.excludePaths)
			{
				// TARGET RW 0x4352E0-0x435321: an empty prefix matches; a shorter string does
				// not; otherwise a plain case-insensitive prefix compare (_strnicmp over the
				// prefix length) with NO separator boundary, so "Data\INI\Object\Foo" also
				// excludes "Data\INI\Object\FooExtra\Unit.ini".
				if (ex.empty() || (file.size() >= ex.size() && AsciiStringUtil::compareNoCase(file.substr(0, ex.size()), ex) == 0))
				{
					excluded = true;
					break;
				}
			}
			if (!excluded)
			{
				files.push_back(file);
			}
		}
	}

	// lane PERF-2: the text of each file (reading it and its includes from the archives, decoding, splitting the lines: a function of the mounted
	// files) is parsed on the client job pool, a window of files ahead; the files are then loaded strictly in order on this thread, so the #define
	// pass, the blocks and every error happen exactly as in the sequential load (a file whose text failed to parse, or whose read threw, reports
	// its error at its turn: an earlier file's error comes first)
	struct Parsed
	{
		TextFile text;
		std::string error;
		bool ok = false;
		std::exception_ptr thrown; ///< an exception of the read / parse (Sol r1: rethrown at the file's turn, not when the window is parsed)
	};
	const size_t window = 64;
	std::vector<Parsed> parsed;
	const TextFile::FileReader reader = archiveReader(m_env.fileSystem);
	for (size_t first = 0; first < files.size(); first += window)
	{
		const size_t count = std::min(window, files.size() - first);
		parsed.clear();
		parsed.resize(count);
		JobSystem::client().parallelFor(count, 1, [&](size_t, size_t begin, size_t end) {
			for (size_t i = begin; i < end; ++i)
			{
				try
				{
					parsed[i].ok = parsed[i].text.parseFile(files[first + i], reader, &parsed[i].error);
				}
				catch (...)
				{
					parsed[i].thrown = std::current_exception();
				}
			}
		});
		for (size_t i = 0; i < count; ++i)
		{
			const std::string &file = files[first + i];
			try
			{
				if (parsed[i].thrown)
				{
					std::rethrow_exception(parsed[i].thrown); // where the sequential load's read would have thrown it
				}
				if (!parsed[i].ok)
				{
					throw INIException(7, "INI::load, cannot open file '%s'\n%s", file.c_str(), parsed[i].error.c_str());
				}
				NumericState::setFPMode(); // as INI::load
				prepFile(file, loadType, nullptr, &parsed[i].text);
				loadPrepared(file);
			}
			catch (const INIException &e)
			{
				if (!options.onFileError)
				{
					throw;
				}
				options.onFileError(file, e);
			}
		}
	}
}
