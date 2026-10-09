// OpenBFME. GPL-3.0.
//
// The INI reader. Port of ZH GameEngine/Include/Common/INI.h with the BFME / RotWK changes
// from the BFME1 decompile (Source/Common/INI/ini.cpp, ini_parsers.cpp, INILoadDirectory.cpp,
// INIPreprocessMacro.cpp, INI_initFromINIMulti.cpp) and the BFME2 / RotWK game.dat
// disassembly as recorded in spec ini-and-object-model.md sections 1-3.
//
// Differences from ZH, in one place:
//   * the file is read whole into TextFile lines before parsing (not streamed)
//   * block keywords come from a registry (INIBlockRegistry), case-sensitive strcmp
//   * macros: a global hash table of #define values (INIMacroTable), consulted by the
//     numeric / bool scanners, with #ADD( #SUBTRACT( #MULTIPLY( #DIVIDE( math
//   * a pending-token buffer lets macro text run through the normal token reader
//   * durations convert at 5 logic frames per second (GameCommon.h)
//   * errors are INIException with the BFME message texts
//
// Deliberate port decisions:
//   * strtok state lives in the INI instance rather than in the C runtime. Retail relies on
//     one global strtok; one INI is active at a time, so behaviour is identical.
//   * load() calls NumericState::setFPMode (retail setFPMode, RW 0x440809: round-to-nearest, 24-bit
//     precision). Float math is SSE float32, which rounds to a 24-bit significand after every
//     operation, as 24-bit x87 does; the duration product, whose operand is wider than float32,
//     is emulated exactly (NumericState::product24).
//   * the INI CRC hook (s_xfer->xferUser) is a std::function (setLineHook).

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/INIDataTypes.h"
#include "Common/INIException.h"
#include "Libraries/file/TextFile.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class INI;

// ZH Include/Common/INI.h INILoadType, plus the BFME-internal types
// (spec 0.1 fact 9: map.ini is type 2; 4 is ChildObject parsing; 5 is the developer reload).
enum INILoadType
{
	INI_LOAD_INVALID = 0,
	INI_LOAD_OVERWRITE = 1,        ///< create new or update existing data instance
	INI_LOAD_CREATE_OVERRIDES = 2, ///< create new or create override data instance (map.ini, solo.ini)
	INI_LOAD_MULTIFILE = 3,        ///< create new or continue loading into existing data instance
	INI_LOAD_CHILD_OBJECT = 4,     ///< internal: ChildObject field parse (spec 4.2)
	INI_LOAD_RELOAD = 5            ///< developer reload; macros may be silently redefined
};

enum
{
	INI_MAX_CHARS_PER_LINE = 1028 ///< ZH Include/Common/INI.h:63
};

typedef void (*INIFieldParseProc)(INI *ini, void *instance, void *store, const void *userData);

typedef const char *ConstCharPtr;
typedef const ConstCharPtr *ConstCharPtrArray;

struct LookupListRec
{
	const char *name;
	int value;
};
typedef const LookupListRec *ConstLookupListRecArray;

// ZH Include/Common/INI.h FieldParse. A row whose token is NULL but whose parse is not is the
// table's catch-all (called with userData = the field name).
struct FieldParse
{
	const char *token;
	INIFieldParseProc parse;
	const void *userData;
	int offset;
};

// ZH Include/Common/INI.h:132-157, at most 16 tables.
class MultiIniFieldParse
{
public:
	enum { MAX_MULTI_FIELDS = 16 };
	void add(const FieldParse *f, unsigned e = 0);
	int getCount() const { return m_count; }
	const FieldParse *getNthFieldParse(int i) const { return m_fieldParse[i]; }
	unsigned getNthExtraOffset(int i) const { return m_extraOffset[i]; }

private:
	const FieldParse *m_fieldParse[MAX_MULTI_FIELDS] = {};
	unsigned m_extraOffset[MAX_MULTI_FIELDS] = {};
	int m_count = 0;
};

typedef std::function<void(INI *ini)> INIBlockParse;
typedef void (*BuildMultiIniFieldProc)(MultiIniFieldParse &p);

// ---------------------------------------------------------------------------------------------
// Macro table. The single global shared by every file (spec 2.2).
//
// TARGET FACTS (RotWK game.dat; the Sol review of INI-1 corrected the spec, which had copied
// BFME1's hash): the name is lower-cased, hashed with
//     h = 0; for each byte c: h = h * 5 + (signed char)c            (RW 0x42B6C1-0x42B6D8)
// (the lower-casing copy and lookup are RW 0x42BC44; the bucket chain walk is RW 0x42C6EF-0x42C72B,
// bucket = h % bucketCount) and the chain is searched comparing the COMPLETE names. BFME2
// corroborates at 0x42BF8C, 0x42BA61, 0x42C9E7. Two different names that hash alike are therefore
// two entries, and lookup is by case-insensitive full name. The table below is keyed by the
// lower-cased name, which gives the same answers as the hash-and-chain; hash() is kept so tests
// can pin the retail hash. DONOR (not the target): BFME1 INIPreprocessMacro.cpp:83-110 uses a
// shift/xor hash compared by hash alone.
// ---------------------------------------------------------------------------------------------
class INIMacroTable
{
public:
	struct Entry
	{
		std::string name;  ///< as defined
		std::string value;
	};

	// RW 0x42B6C1 over the lower-cased name, 32-bit wrap-around, bytes sign-extended.
	static std::uint32_t hash(const char *name);

	// Inserts; returns false when a macro with the same name (case-insensitively) exists
	// (retail: "Duplicate MACRO names"). With allowOverwrite (load type 5) the value is replaced.
	bool define(const std::string &name, const std::string &value, bool allowOverwrite);

	const Entry *find(const char *name) const;
	const Entry *findByName(const char *name) const { return find(name); }
	size_t size() const { return m_entries.size(); }
	void clear() { m_entries.clear(); }
	// keyed by the lower-cased name
	const std::unordered_map<std::string, Entry> &entries() const { return m_entries; }

private:
	std::unordered_map<std::string, Entry> m_entries;
};

// ---------------------------------------------------------------------------------------------
// Block registry. Retail walks a linked list of {next, token, parse} with strcmp
// (RW list head 0xDC51C0; B1 ini.cpp:376-386); the order never matters because tokens are
// unique, so a hash map gives the same answers.
// ---------------------------------------------------------------------------------------------
class INIBlockRegistry
{
public:
	// Throws std::logic_error on a duplicate token (retail registers each keyword once).
	void registerBlock(const std::string &token, INIBlockParse parse);
	const INIBlockParse *find(const char *token) const;
	size_t size() const { return m_blocks.size(); }
	bool contains(const std::string &token) const { return m_blocks.count(token) != 0; }

private:
	std::unordered_map<std::string, INIBlockParse> m_blocks;
};

// Everything an INI load shares: the file system, the global macro table, the block registry.
struct INIEnvironment
{
	ArchiveFileSystem *fileSystem = nullptr; ///< where files and #includes come from
	INIMacroTable macros;
	INIBlockRegistry blocks;
};

// loadDirectory options. ExcludePath / IncludePathCinematics come from the subsystem legend.
struct INILoadDirectoryOptions
{
	// Files whose path starts with one of these (plain case-insensitive prefix, no separator
	// boundary; TARGET RW 0x4352E0-0x435321) are skipped in the subdirectory pass (spec 3.4 item
	// 4, B2 call 0x42C5E6).
	std::vector<std::string> excludePaths;
	// When set, a file that throws an INIException is reported here and loading continues with
	// the next file; when empty the exception propagates (retail behaviour).
	std::function<void(const std::string &file, const INIException &e)> onFileError;
};

class INI
{
public:
	explicit INI(INIEnvironment &env);
	~INI();
	INI(const INI &) = delete;
	INI &operator=(const INI &) = delete;

	// ---- loading --------------------------------------------------------------------------
	// B1 ini.cpp:464-510 (INI::load), B2 0x42D2C1-0x42DD35.
	void load(const std::string &filename, INILoadType loadType);
	void loadFile(const std::string &filename, INILoadType loadType) { load(filename, loadType); }
	// The first half of load() only: TextFile split, #include expansion and the #define pre-pass
	// (spec 2.2). For files whose blocks have no parser yet (corpus checks of the macro table).
	void preprocessFile(const std::string &filename, INILoadType loadType);
	// Same, but the bytes are given (tests; includes still resolve through the file system).
	void loadMemory(const std::string &filename, const std::vector<std::uint8_t> &bytes, INILoadType loadType);
	// ZH INI.cpp:217-261, B1 INILoadDirectory.cpp:42-87, B2 0x42E63B.
	void loadDirectory(std::string dirName, bool subdirs, INILoadType loadType, const INILoadDirectoryOptions &options = INILoadDirectoryOptions());

	// Every file that finished loading through this instance, in order.
	const std::vector<std::string> &loadedFiles() const { return m_loadedFiles; }

	// Optional per-line observer. Retail feeds every line, including the empty EOF line, to the
	// INI CRC (B1 ini.cpp:524-542).
	void setLineHook(std::function<void(const char *, size_t)> hook) { m_lineHook = std::move(hook); }

	// ---- state ----------------------------------------------------------------------------
	// The file that CONTAINS the line last read, so an error inside an included file names that
	// file (RW 0x42BC9D-0x42BCBA asks the line array for the current line's file). Before any line
	// is read, or outside a load, it is the root file.
	const std::string &getFilename() const;
	// The file passed to load(); block-level diagnostics name it (B1 reads this+4 at those sites).
	const std::string &getRootFilename() const { return m_filename; }
	INILoadType getLoadType() const { return m_loadType; }
	// Retail's parseObjectDefinition stores load type 4 into the INI while it parses a ChildObject's fields and
	// restores it afterwards (RW 0x6D29D9 / 0x6D29E5); nothing else changes the type of a load in progress.
	void setLoadType(INILoadType type) { m_loadType = type; }
	unsigned getLineNum() const { return m_lineNum; }
	bool isEOF() const { return m_endOfFile; }
	INIEnvironment &environment() { return m_env; }
	const char *getSeps() const { return m_seps; }
	const char *getSepsPercent() const { return m_sepsPercent; }
	const char *getSepsColon() const { return m_sepsColon; }
	const char *getSepsQuote() const { return m_sepsQuote; }
	const char *getCurBlockStart() const { return m_curBlockStart; }
	// Source line number of the line readLine returned last (for diagnostics).
	int currentSourceLine() const;
	// The raw text of the line the next readLine would return, or nullptr at EOF. Used by block
	// handlers that must skip a block without field tables (the recording stubs).
	const std::string *peekNextLine() const;
	// The text of the line last read by readLine (before tokenising it), or "" before the first.
	const std::string &currentLineText() const;
	// Random access to the loaded line array (OBJ-1: blocks whose typed parser is not ported are stored
	// verbatim). Index i is the line that was the (i + 1)th readLine of this file: getLineNum() before a
	// block's first readLine is its first body index, after the block it is one past its End.
	size_t lineCount() const { return m_lines.size(); }
	const std::string &lineTextAt(size_t index) const { return m_lines.at(index).text; }
	int sourceLineAt(size_t index) const { return m_lines.at(index).lineNumber; }
	const std::string &sourceFileAt(size_t index) const { return m_lineFileNames.at((size_t)m_lines.at(index).fileIndex); }

	// ---- tokens ---------------------------------------------------------------------------
	void readLine();
	// strtok(m_buffer, seps): first token of the current line.
	const char *firstToken(const char *seps = nullptr);
	const char *getNextToken(const char *seps = nullptr);
	const char *getNextTokenOrNull(const char *seps = nullptr);
	const char *getNextSubToken(const char *expected);
	// BFME2 pushText (B2 0x42CBCC): m_pending = text + " " + m_pending.
	void pushText(const std::string &text);
	// Macro-aware token reader (B1 INIGetNextToken.cpp:38-46).
	const char *getNextTokenPreprocess(const char *seps = nullptr);
	std::string getNextAsciiString();
	std::string getNextQuotedAsciiString();

	// ---- field tables ---------------------------------------------------------------------
	void initFromINI(void *what, const FieldParse *parseTable);
	void initFromINIMulti(void *what, const MultiIniFieldParse &parseTableList);
	void initFromINIMultiProc(void *what, BuildMultiIniFieldProc proc);

	// ---- macros and scanners (non-static: the math evaluator needs the pending buffer) ------
	const char *preprocessMacro(const char *token);
	int scanInt(const char *token);
	unsigned scanUnsignedInt(const char *token);
	float scanReal(const char *token);
	bool scanBool(const char *token);
	float scanPercentToReal(const char *token);
	static int scanIndexList(const char *token, ConstCharPtrArray nameList);
	static int scanLookupList(const char *token, ConstLookupListRecArray lookupList);

	// ---- field parsers (ZH INI.cpp, B1 ini_parsers.cpp) -------------------------------------
	static void parseByte(INI *ini, void *instance, void *store, const void *userData);
	static void parseUnsignedByte(INI *ini, void *instance, void *store, const void *userData);
	static void parseShort(INI *ini, void *instance, void *store, const void *userData);
	static void parseUnsignedShort(INI *ini, void *instance, void *store, const void *userData);
	static void parseInt(INI *ini, void *instance, void *store, const void *userData);
	static void parseUnsignedInt(INI *ini, void *instance, void *store, const void *userData);
	static void parseReal(INI *ini, void *instance, void *store, const void *userData);
	static void parsePositiveNonZeroReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseBool(INI *ini, void *instance, void *store, const void *userData);
	static void parseBitInInt32(INI *ini, void *instance, void *store, const void *userData);
	static void parseAsciiString(INI *ini, void *instance, void *store, const void *userData);
	static void parseQuotedAsciiString(INI *ini, void *instance, void *store, const void *userData);
	static void parseAsciiStringVector(INI *ini, void *instance, void *store, const void *userData);
	static void parseAsciiStringVectorAppend(INI *ini, void *instance, void *store, const void *userData);
	static void parsePercentToReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseAngleReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseDurationReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseDurationUnsignedInt(INI *ini, void *instance, void *store, const void *userData);
	static void parseDurationUnsignedShort(INI *ini, void *instance, void *store, const void *userData);
	static void parseVelocityReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseAccelerationReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseAngularVelocityReal(INI *ini, void *instance, void *store, const void *userData);
	static void parseSecondsToMillis(INI *ini, void *instance, void *store, const void *userData);
	static void parseIndexList(INI *ini, void *instance, void *store, const void *userData);
	static void parseByteSizedIndexList(INI *ini, void *instance, void *store, const void *userData);
	static void parseLookupList(INI *ini, void *instance, void *store, const void *userData);
	static void parseBitString8(INI *ini, void *instance, void *store, const void *userData);
	static void parseBitString32(INI *ini, void *instance, void *store, const void *userData);
	static void parseTypeFlagList(INI *ini, void *instance, void *store, const void *userData);
	static void parseRGBColor(INI *ini, void *instance, void *store, const void *userData);
	static void parseRGBAColorInt(INI *ini, void *instance, void *store, const void *userData);
	static void parseColorInt(INI *ini, void *instance, void *store, const void *userData);
	static void parseCoord3D(INI *ini, void *instance, void *store, const void *userData);
	static void parseCoord2D(INI *ini, void *instance, void *store, const void *userData);
	static void parseICoord2D(INI *ini, void *instance, void *store, const void *userData);

private:
	// `parsed`: the file's text already parsed (lane PERF-2: loadDirectory parses its files on the client job pool ahead of loading them in order)
	void prepFile(const std::string &filename, INILoadType loadType, const std::vector<std::uint8_t> *bytes, const TextFile *parsed = nullptr);
	void unPrepFile();
	void parseLine();
	void runDefinePass();
	void loadPrepared(const std::string &filename);
	const char *tok(char *str, const char *seps);
	float parseMathReal(const char *text);
	int parseMathInt(const char *text);
	unsigned parseMathUnsigned(const char *text);

	INIEnvironment &m_env;

	bool m_fileOpen = false;
	std::vector<TextFile::Line> m_lines;
	std::vector<std::string> m_lineFileNames;
	std::string m_filename;
	INILoadType m_loadType = INI_LOAD_INVALID;
	unsigned m_lineNum = 0;
	bool m_endOfFile = false;
	char m_buffer[INI_MAX_CHARS_PER_LINE];
	char m_curBlockStart[INI_MAX_CHARS_PER_LINE];
	char *m_tokPos = nullptr;

	const char *m_seps;
	const char *m_sepsPercent;
	const char *m_sepsColon;
	const char *m_sepsQuote;
	const char *m_blockEndToken;

	std::string m_pending;             ///< BFME2 pending-token buffer (B2 +0x86C)
	std::deque<std::string> m_tokenBank; ///< BFME2 token bank (B2 +0x870), stable references
	std::function<void(const char *, size_t)> m_lineHook;
	std::vector<std::string> m_loadedFiles;
};

// Spec of one type-flag-list field for INI::parseTypeFlagList (userData). Flag bit for name
// index i is 1 << (i - firstBitIndex). The damage-type list uses firstBitIndex = 1
// (B1 ini.cpp:1309-1331: `1 << (dt - 1)`).
struct INITypeFlagListSpec
{
	ConstCharPtrArray names;
	int firstBitIndex;
};
