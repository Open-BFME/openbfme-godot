// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the read side of ZH GameEngine/Include/Common/DataChunk.h and
// GameEngine/Source/Common/System/DataChunk.cpp (DataChunkTableOfContents::read,
// DataChunkInput, CachedFileInputStream), against the BFME1 decompile:
//   Open-BFME-1 game/GameEngine/Source/Common/System/{DataChunkInput.cpp,
//   DataChunkTableOfContents.cpp, DataChunk.cpp}.
//
// Target facts (BFME1 retail): the table of contents is "CkMp" + i32 count + {u8 len, name, u32 id};
// a chunk header is u32 id, u16 version, i32 size (10 bytes); parse() lets a parser's own
// registered userData take priority over parse()'s argument; the most recently registered parser
// is tried first. BFME additionally yields to the OS before every read; that is not modelled.
//
// Deliberate differences (rule 10, no silent fallbacks):
//  * ZH/BFME silently skip chunks that have no parser and silently skip unread tail bytes. Here
//    both are recorded (issues()) so the corpus gate can demand zero of each. Skipping still
//    happens, because that is retail behaviour for unknown chunks.
//  * Reads past the end of a chunk, a chunk that overruns its parent, a negative size and an
//    unknown table id used as a name all throw MapParseError instead of reading garbage.
//  * Dict keys and NameKeys come back as strings (see Common/Dict.h).

#pragma once

#include "Common/Dict.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

typedef std::uint16_t DataChunkVersionType;

class MapParseError : public std::runtime_error
{
public:
	explicit MapParseError(const std::string &what) : std::runtime_error(what) {}
};

// ZH CachedFileInputStream over a decoded in-memory buffer.
class ChunkInputStream
{
public:
	ChunkInputStream(const std::uint8_t *data, size_t size) : m_data(data), m_size(size) {}
	size_t read(void *dst, size_t numBytes);   // short read at the end (ZH behaviour); callers check
	size_t tell() const { return m_pos; }
	bool absoluteSeek(size_t pos);             // clamps to size like ZH
	bool eof() const { return m_pos == m_size; }
	size_t size() const { return m_size; }

private:
	const std::uint8_t *m_data;
	size_t m_size;
	size_t m_pos = 0;
};

class DataChunkTableOfContents
{
public:
	// Returns false when the 'CkMp' tag is absent (ZH: "may happen with legacy files"; not a
	// valid file for DataChunkInput::parse). Throws MapParseError on a truncated table.
	bool read(ChunkInputStream &s);

	bool isOpenedForRead() const { return m_headerOpened; }
	// Empty string for an unknown id, like DataChunkTableOfContents::getName.
	std::string getName(std::uint32_t id) const;
	bool hasId(std::uint32_t id) const;
	size_t size() const { return m_ids.size(); }
	const std::vector<std::string> &names() const { return m_names; }
	const std::vector<std::uint32_t> &ids() const { return m_ids; }

private:
	bool m_headerOpened = false;
	std::vector<std::uint32_t> m_ids; // in file order (ZH stores the list reversed; lookups scan from the end)
	std::vector<std::string> m_names;
	std::unordered_map<std::uint32_t, size_t> m_index;
};

struct DataChunkInfo
{
	std::string label;
	std::string parentLabel;
	DataChunkVersionType version = 0;
	std::int32_t dataSize = 0;
};

class DataChunkInput;
typedef bool (*DataChunkParserPtr)(DataChunkInput &file, DataChunkInfo *info, void *userData);

// One chunk the parser walked over, in file order.
struct DataChunkRecord
{
	std::uint32_t id = 0;
	std::uint32_t parentId = 0; // 0 for top level (no chunk has id 0: ids start at 1)
	DataChunkVersionType version = 0;
	std::int32_t size = 0;
	size_t offset = 0;  // file offset of the payload (just past the 10-byte header)
	int depth = 0;
	bool parsed = false; // a registered parser matched
	std::int32_t leftover = 0; // payload bytes the parser (and its children) did not consume
};

class DataChunkInput
{
public:
	enum { CHUNK_HEADER_BYTES = 4 };      // ZH constant: "2 shorts" (sic); used for the end-of-chunk test
	enum { FULL_CHUNK_HEADER_BYTES = 10 }; // u32 id + u16 version + i32 size
	// Inference (no donor bound exists: ZH/BFME recurse without limit, which a hostile map turns into a
	// stack overflow). The 181 retail maps nest 10 levels deep at most (asserted by the corpus test); a
	// deeper file is refused with MapParseError. Reviewer finding MAP-1 P1.
	enum { MAX_CHUNK_NESTING = 32 };

	explicit DataChunkInput(ChunkInputStream *pStream);

	void registerParser(const std::string &label, const std::string &parentLabel, DataChunkParserPtr parser, void *userData = nullptr);

	// Walks chunks from the current position (the start of a chunk), recursively through parsers
	// that call parse() themselves. Returns false when a parser returned false or the table of
	// contents is missing.
	bool parse(void *userData = nullptr);

	bool isValidFileType() const { return m_contents.isOpenedForRead(); }
	std::string openDataChunk(DataChunkVersionType *ver);
	void closeDataChunk();

	bool atEndOfFile() const { return m_file->eof(); }
	bool atEndOfChunk() const;
	void reset();

	std::string getChunkLabel() const;
	DataChunkVersionType getChunkVersion() const;
	std::uint32_t getChunkDataSize() const;
	std::int32_t getChunkDataSizeLeft() const;

	float readReal();
	std::int32_t readInt();
	std::uint32_t readUnsignedInt();
	std::uint8_t readByte();
	std::uint16_t readUnsignedShort();
	std::string readAsciiString();
	std::u16string readUnicodeString();
	Dict readDict();
	void readArrayOfBytes(void *ptr, std::int32_t len);
	// ZH readNameKey: i32 keyAndType, id = keyAndType >> 8, type must be Dict::DICT_ASCIISTRING (3).
	std::string readNameKey();

	const DataChunkTableOfContents &contents() const { return m_contents; }

	// Issues found while parsing: chunks nobody registered a parser for, and payload bytes left
	// unread when a chunk was closed.
	struct Issue
	{
		enum Kind { UnknownChunk, LeftoverBytes } kind;
		std::string label;
		std::string parentLabel;
		DataChunkVersionType version;
		std::int32_t bytes; // chunk size (UnknownChunk) or bytes left (LeftoverBytes)
		size_t offset;
	};
	const std::vector<Issue> &issues() const { return m_issues; }
	const std::vector<DataChunkRecord> &chunkLog() const { return m_log; }

	void *m_currentObject = nullptr; // ZH: lets one chunk's parser hand an object to a later chunk's parser
	void *m_userData = nullptr;

private:
	struct InputChunk
	{
		std::uint32_t id;
		DataChunkVersionType version;
		size_t chunkStart;
		std::int32_t dataSize;
		std::int32_t dataLeft;
		size_t logIndex;
	};
	struct UserParser
	{
		DataChunkParserPtr parser;
		std::string label;
		std::string parentLabel;
		void *userData;
	};

	void decrementDataLeft(std::int32_t size);
	void need(std::int32_t n, const char *what);
	void readRaw(void *dst, std::int32_t n, const char *what);

	ChunkInputStream *m_file;
	DataChunkTableOfContents m_contents;
	size_t m_fileposOfFirstChunk = 0;
	std::vector<UserParser> m_parserList; // most recently registered LAST; searched from the back
	std::vector<InputChunk> m_chunkStack;
	std::vector<Issue> m_issues;
	std::vector<DataChunkRecord> m_log;
};
