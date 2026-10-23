// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Source/Common/System/ArchiveFileSystem.cpp.

#include "Common/ArchiveFileSystem.h"
#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

using AsciiStringUtil::nextToken;

namespace
{
// lane MOD-5: a path as the local folder compares it (ASCII lower case, '/' for '\')
std::string comparablePath(const std::string &path)
{
	std::string out = AsciiStringUtil::lowered(path);
	std::replace(out.begin(), out.end(), '\\', '/');
	return out;
}
} // namespace

bool FilenameLessNoCase::operator()(const std::string &a, const std::string &b) const
{
	return AsciiStringUtil::compareNoCase(a, b) < 0;
}

ArchiveFileSystem::ArchiveFileSystem() = default;
ArchiveFileSystem::~ArchiveFileSystem() = default;

void ArchiveFileSystem::mountArchive(std::unique_ptr<ArchiveFile> archiveFile, const std::string &archiveFilename, bool overwrite)
{
	loadIntoDirectoryTree(archiveFile.get(), archiveFilename, overwrite);
	m_archiveFileMap[archiveFilename] = std::move(archiveFile);
}

// OpenBFME (S-012, docs/STOPS.md): runtime report, not a print. One line per archive whose identity
// the platform cannot enforce; empty means every mounted archive is protected.
std::vector<std::string> ArchiveFileSystem::unverifiedIdentity() const
{
	std::vector<std::string> report;
	for (const auto &entry : m_archiveFileMap)
	{
		if (!entry.second->isIdentityEnforced())
		{
			report.push_back("S-012: archive identity unverified (the file can be overwritten in place while mounted): " + entry.first);
		}
	}
	return report;
}

void ArchiveFileSystem::loadIntoDirectoryTree(const ArchiveFile *archiveFile, const std::string &archiveFilename, bool overwrite)
{
	FilenameList filenameList;
	archiveFile->getFileListInDirectory(std::string(""), std::string(""), std::string("*"), filenameList, true);

	for (const std::string &listed : filenameList)
	{
		std::string path = AsciiStringUtil::lowered(listed);
		std::string token;

		ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

		bool infoInPath = nextToken(path, &token, "\\/");

		while (infoInPath && ((token.find('.') == std::string::npos) || (path.find('.') != std::string::npos)))
		{
			ArchivedDirectoryInfoMap::iterator tempiter = dirInfo->m_directories.find(token);
			if (tempiter == dirInfo->m_directories.end())
			{
				dirInfo->m_directories[token].clear();
				dirInfo->m_directories[token].m_directoryName = token;
			}

			dirInfo = &(dirInfo->m_directories[token]);
			infoInPath = nextToken(path, &token, "\\/");
		}

		// token is the filename, and dirInfo is the directory that this file is in.
		// overwrite == false: the first archive to supply a path keeps it.
		if (dirInfo->m_files.find(token) == dirInfo->m_files.end() || overwrite)
		{
			dirInfo->m_files[token] = archiveFilename;
		}
	}
}

std::string ArchiveFileSystem::getArchiveFilenameForFile(const std::string &filename) const
{
	std::string path = AsciiStringUtil::lowered(filename);
	std::string token;

	const ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	nextToken(path, &token, "\\/");

	while ((token.find('.') == std::string::npos) || (path.find('.') != std::string::npos))
	{
		ArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.find(token);
		if (it == dirInfo->m_directories.end())
		{
			return std::string();
		}
		dirInfo = &it->second;
		if (!nextToken(path, &token, "\\/"))
		{
			return std::string();
		}
	}

	ArchivedFileLocationMap::const_iterator it = dirInfo->m_files.find(token);
	if (it == dirInfo->m_files.end())
	{
		return std::string();
	}
	return it->second;
}

bool ArchiveFileSystem::doesFileExist(const std::string &filename) const
{
	return !getArchiveFilenameForFile(filename).empty() || !findLocalFile(filename).empty();
}

void ArchiveFileSystem::setLocalFolder(const std::string &folder)
{
	m_localFolder = folder;
	while (!m_localFolder.empty() && (m_localFolder.back() == '\\' || m_localFolder.back() == '/'))
	{
		m_localFolder.pop_back();
	}
}

std::string ArchiveFileSystem::findLocalFile(const std::string &filename) const
{
	namespace fs = std::filesystem;
	if (m_localFolder.empty())
	{
		return std::string();
	}
	const std::string folder = comparablePath(m_localFolder) + "/";
	if (filename.size() <= folder.size() || comparablePath(filename).compare(0, folder.size(), folder) != 0)
	{
		return std::string();
	}
	fs::path at = fs::u8path(m_localFolder);
	std::error_code ec;
	std::string rest = filename.substr(folder.size());
	std::replace(rest.begin(), rest.end(), '\\', '/');
	std::string part;
	std::size_t start = 0;
	while (start <= rest.size())
	{
		const std::size_t end = std::min(rest.find('/', start), rest.size());
		part = rest.substr(start, end - start);
		start = end + 1;
		if (part.empty())
		{
			continue;
		}
		// nothing outside the folder: no parent step, and no part with a ':' (a drive, "D:", would replace the whole path when joined on
		// Windows; "x.map:s" names an alternate data stream)
		if (part == "." || part == ".." || part.find(':') != std::string::npos)
		{
			return std::string();
		}
		fs::path next = at / fs::u8path(part);
		if (!fs::exists(next, ec))
		{
			// Windows opens a path without regard to case; elsewhere the folder's entry that matches it without case
			next.clear();
			for (fs::directory_iterator it(at, ec), last; !ec && it != last; it.increment(ec))
			{
				if (AsciiStringUtil::compareNoCase(it->path().filename().u8string(), part) == 0)
				{
					next = it->path();
					break;
				}
			}
			if (next.empty())
			{
				return std::string();
			}
		}
		at = next;
	}
	return fs::is_regular_file(at, ec) ? at.u8string() : std::string();
}

bool ArchiveFileSystem::readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	std::string archiveFilename = getArchiveFilenameForFile(filename);
	if (archiveFilename.empty())
	{
		// lane MOD-5: RW 0xA149A2 asks TheLocalFileSystem when no archive holds the file; here only the local folder's files
		const std::string local = findLocalFile(filename);
		if (!local.empty())
		{
			std::ifstream in(std::filesystem::u8path(local), std::ios::binary);
			if (!in)
			{
				if (error)
				{
					*error = "cannot read " + local;
				}
				return false;
			}
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}
		if (error && m_localFolder.empty())
		{
			*error = "file not found in any mounted archive: " + filename;
		}
		else if (error)
		{
			*error = "file not found in any mounted archive or in " + m_localFolder + ": " + filename;
		}
		return false;
	}
	ArchiveFileMap::iterator it = m_archiveFileMap.find(archiveFilename);
	if (it == m_archiveFileMap.end() || !it->second)
	{
		if (error)
		{
			*error = "directory tree names unmounted archive " + archiveFilename + " for " + filename;
		}
		return false;
	}
	return it->second->readFile(filename, out, error);
}

void ArchiveFileSystem::getFileListInDirectory(const std::string &currentDirectory, const std::string &originalDirectory,
	const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const
{
	for (const auto &entry : m_archiveFileMap)
	{
		entry.second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, filenameList, searchSubdirectories);
	}
}

// OpenBFME's original-map catalog selection: patch archives still supply map contents.
bool ArchiveFileSystem::readFileFromArchive(const std::string &archiveBasename, const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	ArchiveFile *found = nullptr;
	for (const auto &entry : m_archiveFileMap)
	{
		const std::string path = comparablePath(entry.first);
		const std::string base = path.substr(path.find_last_of('/') + 1);
		if (base != comparablePath(archiveBasename)) continue;
		if (found) { if (error) *error = "ambiguous mounted archive: " + archiveBasename; return false; }
		found = entry.second.get();
	}
	if (!found) { if (error) *error = "missing mounted archive: " + archiveBasename; return false; }
	return found->readFile(filename, out, error);
}
