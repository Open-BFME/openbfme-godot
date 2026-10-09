// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Source/Common/System/ArchiveFileSystem.cpp.

#include "Common/ArchiveFileSystem.h"
#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"

using AsciiStringUtil::nextToken;

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
	return !getArchiveFilenameForFile(filename).empty();
}

bool ArchiveFileSystem::readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	std::string archiveFilename = getArchiveFilenameForFile(filename);
	if (archiveFilename.empty())
	{
		if (error)
		{
			*error = "file not found in any mounted archive: " + filename;
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
