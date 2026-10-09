// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp.

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFile.h"
#include "Common/AsciiString.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>

namespace
{

const char *BIGFileIdentifier = "BIGF"; // ZH
const char *BIG4FileIdentifier = "BIG4"; // BFME2 / RotWK

std::uint32_t readBE32(const std::uint8_t *p)
{
	return ((std::uint32_t)p[0] << 24) | ((std::uint32_t)p[1] << 16) | ((std::uint32_t)p[2] << 8) | (std::uint32_t)p[3];
}

std::uint32_t readLE32(const std::uint8_t *p)
{
	return ((std::uint32_t)p[3] << 24) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[1] << 8) | (std::uint32_t)p[0];
}

// Sequential byte source: either a stream or a memory buffer.
typedef std::function<bool(void *dst, size_t n)> ReadFn;

// ZH Win32BIGFileSystem::openArchiveFile body. Layout (ZH comments + retail files):
//   0x00 char[4]  "BIGF" | "BIG4"
//   0x04 uint32   archive size, little-endian (ZH reads it unswapped and never uses it)
//   0x08 uint32   number of entries, big-endian
//   0x0C uint32   end of directory / first data offset, big-endian (ZH seeks past it unread)
//   0x10 entries: uint32 BE offset, uint32 BE size, NUL-terminated path
//
// OpenBFME addition: archiveLength is the real length of the archive (file size or buffer size).
// ZH never checks entries against it; here every entry's offset+size must lie inside it, and the
// entry count must fit the directory bytes available, or the whole archive is rejected before any
// entry (or an allocation sized from one) is used. The sizes declared in the header stay untrusted.
bool parseDirectory(const ReadFn &read, std::uint64_t archiveLength, const std::string &archiveFileNameIn, Win32BIGFile *archiveFile, std::string *error)
{
	std::uint8_t header[16];
	if (!read(header, sizeof(header)))
	{
		if (error)
		{
			*error = "archive " + archiveFileNameIn + " is shorter than a BIG header";
		}
		return false;
	}
	char magic[5] = { 0 };
	std::memcpy(magic, header, 4);
	if (std::strcmp(magic, BIGFileIdentifier) != 0 && std::strcmp(magic, BIG4FileIdentifier) != 0)
	{
		if (error)
		{
			*error = "archive " + archiveFileNameIn + " has no BIGF/BIG4 identifier";
		}
		return false;
	}
	archiveFile->m_magic = magic;
	archiveFile->m_declaredArchiveSize = readLE32(header + 4);
	archiveFile->m_fileCount = readBE32(header + 8);
	archiveFile->m_declaredHeaderEnd = readBE32(header + 12);

	// Each directory entry is at least 8 bytes + a 1-byte name terminator.
	if ((std::uint64_t)archiveFile->m_fileCount * 9 > archiveLength - sizeof(header))
	{
		if (error)
		{
			*error = "archive " + archiveFileNameIn + " declares " + std::to_string(archiveFile->m_fileCount) +
				" entries, more than its " + std::to_string(archiveLength) + " bytes can hold";
		}
		return false;
	}

	std::string archiveFileName = AsciiStringUtil::lowered(archiveFileNameIn);
	ArchivedFileInfo fileInfo;

	for (std::uint32_t i = 0; i < archiveFile->m_fileCount; ++i)
	{
		std::uint8_t entry[8];
		if (!read(entry, sizeof(entry)))
		{
			if (error)
			{
				*error = "archive " + archiveFileNameIn + " directory truncated at entry " + std::to_string(i);
			}
			return false;
		}
		fileInfo.m_archiveFilename = archiveFileName;
		fileInfo.m_offset = readBE32(entry);
		fileInfo.m_size = readBE32(entry + 4);

		std::string buffer;
		char c = 0;
		for (;;)
		{
			if (!read(&c, 1))
			{
				if (error)
				{
					*error = "archive " + archiveFileNameIn + " entry name truncated at entry " + std::to_string(i);
				}
				return false;
			}
			if (c == 0)
			{
				break;
			}
			buffer.push_back(c);
		}

		if ((std::uint64_t)fileInfo.m_offset + fileInfo.m_size > archiveLength)
		{
			if (error)
			{
				*error = "archive " + archiveFileNameIn + " entry " + std::to_string(i) + " '" + buffer + "' (offset " +
					std::to_string(fileInfo.m_offset) + ", size " + std::to_string(fileInfo.m_size) +
					") runs past the end of the archive (" + std::to_string(archiveLength) + " bytes)";
			}
			return false;
		}

		// Split at the last separator: directory part keeps its trailing separator.
		size_t sep = buffer.find_last_of("\\/");
		std::string path;
		if (sep == std::string::npos)
		{
			fileInfo.m_filename = buffer;
		}
		else
		{
			fileInfo.m_filename = buffer.substr(sep + 1);
			path = buffer.substr(0, sep + 1);
		}
		AsciiStringUtil::toLower(fileInfo.m_filename);

		archiveFile->addFile(path, &fileInfo);
	}
	return true;
}

} // namespace

std::vector<std::string> BFMEArchiveLoadOrder(std::vector<std::string> archiveNames)
{
	std::stable_sort(archiveNames.begin(), archiveNames.end(), AsciiStringUtil::lessStrcmp);
	std::reverse(archiveNames.begin(), archiveNames.end());
	return archiveNames;
}

Win32BIGFileSystem::Win32BIGFileSystem() = default;
Win32BIGFileSystem::~Win32BIGFileSystem() = default;

std::unique_ptr<ArchiveFile> Win32BIGFileSystem::openArchiveFile(const std::string &filename, std::string *error)
{
	std::shared_ptr<ArchiveHandle> handle = ArchiveHandle::open(filename, error, m_protect);
	if (!handle)
	{
		return nullptr;
	}
	std::unique_ptr<Win32BIGFile> archiveFile = std::make_unique<Win32BIGFile>();

	// The directory is parsed through the same handle every later read uses, in 64 KiB blocks.
	std::vector<std::uint8_t> block;
	std::uint64_t blockStart = 0;
	std::uint64_t position = 0;
	ReadFn read = [&](void *dst, size_t n) {
		std::uint8_t *out = static_cast<std::uint8_t *>(dst);
		while (n > 0)
		{
			if (position < blockStart || position >= blockStart + block.size())
			{
				const std::uint64_t want = std::min<std::uint64_t>(65536, handle->length() > position ? handle->length() - position : 0);
				if (want == 0)
				{
					return false;
				}
				block.resize((size_t)want);
				blockStart = position;
				if (!handle->readAt(position, block.data(), block.size()))
				{
					block.clear();
					return false;
				}
			}
			const size_t offsetInBlock = (size_t)(position - blockStart);
			const size_t take = std::min(n, block.size() - offsetInBlock);
			std::memcpy(out, block.data() + offsetInBlock, take);
			out += take;
			n -= take;
			position += take;
		}
		return true;
	};
	if (!parseDirectory(read, handle->length(), filename, archiveFile.get(), error))
	{
		return nullptr;
	}
	archiveFile->attachHandle(handle);
	return archiveFile;
}

std::unique_ptr<ArchiveFile> Win32BIGFileSystem::openArchiveFromMemory(const std::string &name,
	std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::string *error)
{
	std::unique_ptr<Win32BIGFile> archiveFile = std::make_unique<Win32BIGFile>();
	size_t pos = 0;
	const std::vector<std::uint8_t> &data = *bytes;
	ReadFn read = [&data, &pos](void *dst, size_t n) {
		if (pos + n > data.size())
		{
			return false;
		}
		std::memcpy(dst, data.data() + pos, n);
		pos += n;
		return true;
	};
	if (!parseDirectory(read, (std::uint64_t)data.size(), name, archiveFile.get(), error))
	{
		return nullptr;
	}
	archiveFile->attachMemory(name, std::move(bytes));
	return archiveFile;
}

bool Win32BIGFileSystem::loadBigFilesFromDirectory(const std::string &dir, const std::string &fileMask, bool overwrite, std::string *error)
{
	namespace fs = std::filesystem;
	std::vector<std::string> filenameList;
	std::error_code ec;
	fs::path root = fs::u8path(dir);
	for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file())
		{
			continue;
		}
		std::string name = it->path().filename().u8string();
		if (SearchStringMatches(AsciiStringUtil::lowered(name), AsciiStringUtil::lowered(fileMask)))
		{
			// Win32LocalFileSystem lists "<dir><sub>\<name>" with backslashes.
			std::string rel = fs::relative(it->path(), root).u8string();
			std::replace(rel.begin(), rel.end(), '/', '\\');
			filenameList.push_back(rel);
		}
	}
	if (ec)
	{
		if (error)
		{
			*error = "could not list " + dir + ": " + ec.message();
		}
		return false;
	}

	// OpenBFME: open every archive before mounting any, so a bad archive mounts nothing.
	std::vector<std::pair<std::string, std::unique_ptr<ArchiveFile>>> opened;
	for (const std::string &rel : BFMEArchiveLoadOrder(filenameList))
	{
		std::string full = (root / fs::u8path(rel)).u8string();
		std::unique_ptr<ArchiveFile> archiveFile = openArchiveFile(full, error);
		if (!archiveFile)
		{
			return false;
		}
		opened.emplace_back(full, std::move(archiveFile));
	}
	for (auto &o : opened)
	{
		mountArchive(std::move(o.second), o.first, overwrite);
	}
	return !opened.empty();
}
