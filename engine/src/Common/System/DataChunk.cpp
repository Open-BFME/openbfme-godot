// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the read side of ZH GameEngine/Source/Common/System/DataChunk.cpp against the BFME1
// decompile (DataChunkInput.cpp, DataChunkTableOfContents.cpp). See Common/DataChunk.h for the
// deliberate differences.

#include "Common/DataChunk.h"

#include <cstring>
#include <unordered_map>

//----------------------------------------------------------------------
// ChunkInputStream  (ZH CachedFileInputStream::read / absoluteSeek / eof)
//----------------------------------------------------------------------
size_t ChunkInputStream::read(void *dst, size_t numBytes)
{
	if (numBytes + m_pos > m_size)
	{
		numBytes = m_size - m_pos;
	}
	if (numBytes)
	{
		std::memcpy(dst, m_data + m_pos, numBytes);
		m_pos += numBytes;
	}
	return numBytes;
}

bool ChunkInputStream::absoluteSeek(size_t pos)
{
	if (pos > m_size)
	{
		pos = m_size;
	}
	m_pos = pos;
	return true;
}

//----------------------------------------------------------------------
// DataChunkTableOfContents  (ZH DataChunk.cpp read(); BFME1 DataChunkTableOfContents.cpp read())
//----------------------------------------------------------------------
bool DataChunkTableOfContents::read(ChunkInputStream &s)
{
	char tag[4] = { 'x', 'x', 'x', 'x' };
	size_t got = s.read(tag, sizeof(tag));
	if (got != sizeof(tag) || tag[0] != 'C' || tag[1] != 'k' || tag[2] != 'M' || tag[3] != 'p')
	{
		return false; // ZH: "Don't throw, may happen with legacy files."
	}
	std::int32_t count = 0;
	if (s.read(&count, sizeof(count)) != sizeof(count))
	{
		throw MapParseError("chunk table of contents truncated (count)");
	}
	if (count < 0)
	{
		throw MapParseError("chunk table of contents has a negative count");
	}
	for (std::int32_t i = 0; i < count; ++i)
	{
		std::uint8_t len = 0;
		if (s.read(&len, 1) != 1)
		{
			throw MapParseError("chunk table of contents truncated (name length)");
		}
		std::string name(len, '\0');
		if (len && s.read(&name[0], len) != len)
		{
			throw MapParseError("chunk table of contents truncated (name)");
		}
		std::uint32_t id = 0;
		if (s.read(&id, sizeof(id)) != sizeof(id))
		{
			throw MapParseError("chunk table of contents truncated (id)");
		}
		m_index[id] = m_names.size(); // the LAST entry for an id wins, like ZH's prepended list
		m_ids.push_back(id);
		m_names.push_back(std::move(name));
	}
	m_headerOpened = count > 0 && !s.eof();
	return true;
}

bool DataChunkTableOfContents::hasId(std::uint32_t id) const
{
	return m_index.count(id) != 0;
}

std::string DataChunkTableOfContents::getName(std::uint32_t id) const
{
	auto f = m_index.find(id);
	return f == m_index.end() ? std::string() : m_names[f->second];
}

//----------------------------------------------------------------------
// DataChunkInput
//----------------------------------------------------------------------
DataChunkInput::DataChunkInput(ChunkInputStream *pStream) : m_file(pStream)
{
	m_contents.read(*m_file);
	m_fileposOfFirstChunk = m_file->tell();
}

void DataChunkInput::registerParser(const std::string &label, const std::string &parentLabel, DataChunkParserPtr parser, void *userData)
{
	UserParser p;
	p.label = label;
	p.parentLabel = parentLabel;
	p.parser = parser;
	p.userData = userData;
	m_parserList.push_back(std::move(p)); // searched from the back = ZH's prepend
}

bool DataChunkInput::parse(void *userData)
{
	if (!m_contents.isOpenedForRead())
	{
		return false;
	}

	std::string parentLabel;
	std::uint32_t parentId = 0;
	if (!m_chunkStack.empty())
	{
		parentId = m_chunkStack.back().id;
		parentLabel = m_contents.getName(parentId);
	}

	while (!atEndOfFile())
	{
		if (!m_chunkStack.empty())
		{
			if (m_chunkStack.back().dataLeft < CHUNK_HEADER_BYTES)
			{
				// ZH: DEBUG_ASSERTCRASH(dataLeft==0, "Unexpected extra data in chunk."). Whatever
				// is left is reported as leftover when this chunk is closed.
				break;
			}
		}
		DataChunkVersionType ver = 0;
		std::string label = openDataChunk(&ver);
		// ZH breaks here if the stream is at its end after the header ("FILE * returns eof after
		// you read past end of file"). Our reads never run past the end (they throw), so a
		// chunk header at the very end with an empty payload is still dispatched.

		const UserParser *match = nullptr;
		for (size_t i = m_parserList.size(); i-- > 0;)
		{
			const UserParser &p = m_parserList[i];
			if (p.label == label && p.parentLabel == parentLabel)
			{
				match = &p;
				break;
			}
		}

		if (match)
		{
			m_log[m_chunkStack.back().logIndex].parsed = true;
			DataChunkInfo info;
			info.label = label;
			info.parentLabel = parentLabel;
			info.version = ver;
			info.dataSize = (std::int32_t)getChunkDataSize();
			// BFME1 DataChunkInput::parse: a parser registered with its own userData wins.
			if (!match->parser(*this, &info, match->userData ? match->userData : userData))
			{
				return false;
			}
		}
		else
		{
			Issue is;
			is.kind = Issue::UnknownChunk;
			is.label = label.empty() ? "<id " + std::to_string(m_chunkStack.back().id) + ">" : label;
			is.parentLabel = parentLabel;
			is.version = ver;
			is.bytes = m_chunkStack.back().dataSize;
			is.offset = m_chunkStack.back().chunkStart;
			m_issues.push_back(std::move(is));
		}
		(void)parentId;
		closeDataChunk();
	}
	return true;
}

void DataChunkInput::reset()
{
	m_chunkStack.clear();
	m_file->absoluteSeek(m_fileposOfFirstChunk);
}

std::string DataChunkInput::openDataChunk(DataChunkVersionType *ver)
{
	size_t remaining = m_file->size() - m_file->tell();
	if (remaining < (size_t)FULL_CHUNK_HEADER_BYTES)
	{
		throw MapParseError("truncated chunk header at file offset " + std::to_string(m_file->tell()));
	}
	if (!m_chunkStack.empty() && m_chunkStack.back().dataLeft < FULL_CHUNK_HEADER_BYTES)
	{
		throw MapParseError("chunk header overruns parent chunk '" + m_contents.getName(m_chunkStack.back().id) + "'");
	}

	if (m_chunkStack.size() >= (size_t)MAX_CHUNK_NESTING)
	{
		throw MapParseError("chunks nested deeper than " + std::to_string((int)MAX_CHUNK_NESTING) + " levels at file offset "
			+ std::to_string(m_file->tell()));
	}

	InputChunk c;
	c.id = 0;
	c.version = 0;
	c.dataSize = 0;
	m_file->read(&c.id, sizeof(std::uint32_t));
	decrementDataLeft(sizeof(std::uint32_t));
	m_file->read(&c.version, sizeof(DataChunkVersionType));
	decrementDataLeft(sizeof(DataChunkVersionType));
	m_file->read(&c.dataSize, sizeof(std::int32_t));
	decrementDataLeft(sizeof(std::int32_t));

	size_t afterHeader = m_file->size() - m_file->tell();
	if (c.dataSize < 0 || (size_t)c.dataSize > afterHeader)
	{
		throw MapParseError("chunk '" + m_contents.getName(c.id) + "' size " + std::to_string(c.dataSize)
			+ " overruns the file at offset " + std::to_string(m_file->tell()));
	}
	if (!m_chunkStack.empty() && c.dataSize > m_chunkStack.back().dataLeft)
	{
		throw MapParseError("chunk '" + m_contents.getName(c.id) + "' size " + std::to_string(c.dataSize)
			+ " overruns parent chunk '" + m_contents.getName(m_chunkStack.back().id) + "'");
	}

	c.dataLeft = c.dataSize;
	c.chunkStart = m_file->tell();
	*ver = c.version;

	DataChunkRecord rec;
	rec.id = c.id;
	rec.parentId = m_chunkStack.empty() ? 0 : m_chunkStack.back().id;
	rec.version = c.version;
	rec.size = c.dataSize;
	rec.offset = c.chunkStart;
	rec.depth = (int)m_chunkStack.size();
	c.logIndex = m_log.size();
	m_log.push_back(rec);

	std::string name = m_contents.getName(c.id);
	m_chunkStack.push_back(c);
	return name;
}

void DataChunkInput::closeDataChunk()
{
	if (m_chunkStack.empty())
	{
		return;
	}
	InputChunk &top = m_chunkStack.back();
	if (top.dataLeft > 0)
	{
		// ZH skips the remainder silently. We record it (the corpus gate wants zero of these); a chunk
		// nobody parsed is already reported as UnknownChunk, not as leftover bytes as well.
		if (!m_log[top.logIndex].parsed)
		{
			std::int32_t skip = top.dataLeft;
			m_file->absoluteSeek(m_file->tell() + (size_t)skip);
			decrementDataLeft(skip);
			m_chunkStack.pop_back();
			return;
		}
		Issue is;
		is.kind = Issue::LeftoverBytes;
		is.label = m_contents.getName(top.id);
		is.parentLabel = m_chunkStack.size() > 1 ? m_contents.getName(m_chunkStack[m_chunkStack.size() - 2].id) : std::string();
		is.version = top.version;
		is.bytes = top.dataLeft;
		is.offset = top.chunkStart;
		m_issues.push_back(std::move(is));
		m_log[top.logIndex].leftover = top.dataLeft;
		std::int32_t skip = top.dataLeft;
		m_file->absoluteSeek(m_file->tell() + (size_t)skip);
		decrementDataLeft(skip);
	}
	m_chunkStack.pop_back();
}

std::string DataChunkInput::getChunkLabel() const
{
	if (m_chunkStack.empty())
	{
		throw MapParseError("getChunkLabel with no open chunk");
	}
	return m_contents.getName(m_chunkStack.back().id);
}

DataChunkVersionType DataChunkInput::getChunkVersion() const
{
	if (m_chunkStack.empty())
	{
		throw MapParseError("getChunkVersion with no open chunk");
	}
	return m_chunkStack.back().version;
}

std::uint32_t DataChunkInput::getChunkDataSize() const
{
	if (m_chunkStack.empty())
	{
		throw MapParseError("getChunkDataSize with no open chunk");
	}
	return (std::uint32_t)m_chunkStack.back().dataSize;
}

std::int32_t DataChunkInput::getChunkDataSizeLeft() const
{
	if (m_chunkStack.empty())
	{
		throw MapParseError("getChunkDataSizeLeft with no open chunk");
	}
	return m_chunkStack.back().dataLeft;
}

bool DataChunkInput::atEndOfChunk() const
{
	if (!m_chunkStack.empty())
	{
		return m_chunkStack.back().dataLeft <= 0;
	}
	return true;
}

void DataChunkInput::decrementDataLeft(std::int32_t size)
{
	for (InputChunk &c : m_chunkStack)
	{
		c.dataLeft -= size;
	}
}

void DataChunkInput::need(std::int32_t n, const char *what)
{
	if (m_chunkStack.empty())
	{
		throw MapParseError(std::string("read of ") + what + " outside any chunk");
	}
	if (n < 0 || m_chunkStack.back().dataLeft < n)
	{
		throw MapParseError(std::string("read past end of chunk '") + m_contents.getName(m_chunkStack.back().id) + "' ("
			+ what + ", " + std::to_string(n) + " bytes wanted, " + std::to_string(m_chunkStack.back().dataLeft) + " left)");
	}
}

void DataChunkInput::readRaw(void *dst, std::int32_t n, const char *what)
{
	need(n, what);
	if (n && m_file->read(dst, (size_t)n) != (size_t)n)
	{
		throw MapParseError(std::string("file truncated while reading ") + what);
	}
	decrementDataLeft(n);
}

float DataChunkInput::readReal()
{
	float r;
	readRaw(&r, sizeof(r), "real");
	return r;
}

std::int32_t DataChunkInput::readInt()
{
	std::int32_t i;
	readRaw(&i, sizeof(i), "int");
	return i;
}

std::uint32_t DataChunkInput::readUnsignedInt()
{
	std::uint32_t i;
	readRaw(&i, sizeof(i), "unsigned int");
	return i;
}

std::uint8_t DataChunkInput::readByte()
{
	std::uint8_t b;
	readRaw(&b, sizeof(b), "byte");
	return b;
}

std::uint16_t DataChunkInput::readUnsignedShort()
{
	std::uint16_t s;
	readRaw(&s, sizeof(s), "unsigned short");
	return s;
}

void DataChunkInput::readArrayOfBytes(void *ptr, std::int32_t len)
{
	readRaw(ptr, len, "byte array");
}

std::string DataChunkInput::readAsciiString()
{
	std::uint16_t len = readUnsignedShort();
	std::string s(len, '\0');
	if (len)
	{
		readRaw(&s[0], len, "ascii string");
	}
	return s;
}

std::u16string DataChunkInput::readUnicodeString()
{
	std::uint16_t len = readUnsignedShort(); // u16 CHARACTER count (ZH readUnicodeString)
	std::u16string s(len, u'\0');
	if (len)
	{
		readRaw(&s[0], (std::int32_t)len * 2, "unicode string");
	}
	return s;
}

std::string DataChunkInput::readNameKey()
{
	std::int32_t keyAndType = readInt();
	if ((keyAndType & 0xff) != Dict::DICT_ASCIISTRING)
	{
		throw MapParseError("NameKey with type " + std::to_string(keyAndType & 0xff) + " (expected 3 = ascii)");
	}
	std::uint32_t id = (std::uint32_t)keyAndType >> 8;
	if (!m_contents.hasId(id))
	{
		throw MapParseError("NameKey refers to id " + std::to_string(id) + " which is not in the table of contents");
	}
	return m_contents.getName(id);
}

Dict DataChunkInput::readDict()
{
	std::uint16_t len = readUnsignedShort();
	Dict d;
	for (int i = 0; i < len; ++i)
	{
		std::int32_t keyAndType = readInt();
		int t = keyAndType & 0xff;
		std::uint32_t id = (std::uint32_t)keyAndType >> 8;
		if (!m_contents.hasId(id))
		{
			throw MapParseError("Dict key id " + std::to_string(id) + " is not in the table of contents");
		}
		std::string kname = m_contents.getName(id);
		switch (t)
		{
		case Dict::DICT_BOOL:
			d.setBool(kname, readByte() ? true : false);
			break;
		case Dict::DICT_INT:
			d.setInt(kname, readInt());
			break;
		case Dict::DICT_REAL:
			d.setReal(kname, readReal());
			break;
		case Dict::DICT_ASCIISTRING:
			d.setAsciiString(kname, readAsciiString());
			break;
		case Dict::DICT_UNICODESTRING:
			d.setUnicodeString(kname, readUnicodeString());
			break;
		default:
			throw MapParseError("Dict value type " + std::to_string(t) + " for key '" + kname + "' (ERROR_CORRUPT_FILE_FORMAT)");
		}
	}
	return d;
}
