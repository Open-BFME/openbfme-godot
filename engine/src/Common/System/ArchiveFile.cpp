// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Source/Common/System/ArchiveFile.cpp.

#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"

using AsciiStringUtil::nextToken;

bool SearchStringMatches(const std::string &strIn, const std::string &searchStringIn)
{
	if (strIn.empty())
	{
		return searchStringIn.empty();
	}
	if (searchStringIn.empty())
	{
		return false;
	}

	const char *c1 = strIn.c_str();
	const char *c2 = searchStringIn.c_str();

	while ((*c1 == *c2) || (*c2 == '?') || (*c2 == '*'))
	{
		if ((*c1 == *c2) || (*c2 == '?'))
		{
			++c1;
			++c2;
		}
		else if (*c2 == '*')
		{
			++c2;
			if (*c2 == 0)
			{
				return true;
			}
			while (*c1 != 0)
			{
				if (SearchStringMatches(std::string(c1), std::string(c2)))
				{
					return true;
				}
				++c1;
			}
		}
		if (*c1 == 0)
		{
			return *c2 == 0;
		}
		if (*c2 == 0)
		{
			return false;
		}
	}
	return false;
}

ArchiveFile::ArchiveFile() = default;
ArchiveFile::~ArchiveFile() = default;

void ArchiveFile::addFile(const std::string &path, const ArchivedFileInfo *fileInfo)
{
	std::string temp = AsciiStringUtil::lowered(path);
	std::string token;

	DetailedArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	nextToken(temp, &token, "\\/");

	while (!token.empty())
	{
		if (dirInfo->m_directories.find(token) == dirInfo->m_directories.end())
		{
			dirInfo->m_directories[token].clear();
			dirInfo->m_directories[token].m_directoryName = token;
		}

		dirInfo = &(dirInfo->m_directories[token]);
		if (!nextToken(temp, &token, "\\/"))
		{
			// ZH relies on nextToken clearing the token once only separators remain; when the
			// remainder is completely empty AsciiString::nextToken leaves the old token in place
			// and the ZH loop would spin. The path handed in always ends in a separator (see
			// Win32BIGFileSystem::openArchiveFile), so this only guards malformed input.
			token.clear();
		}
	}

	// Same name twice inside ONE archive: the later directory entry wins (map assignment).
	dirInfo->m_files[fileInfo->m_filename] = *fileInfo;
}

void ArchiveFile::getFileListInDirectory(const std::string &currentDirectory, const std::string &originalDirectory,
	const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const
{
	(void)currentDirectory;
	std::string searchDir = AsciiStringUtil::lowered(originalDirectory);
	std::string token;
	const DetailedArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	nextToken(searchDir, &token, "\\/");

	while (!token.empty())
	{
		DetailedArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.find(token);
		if (it == dirInfo->m_directories.end())
		{
			return; // directory does not exist, no files to be had
		}
		dirInfo = &it->second;
		if (!nextToken(searchDir, &token, "\\/"))
		{
			token.clear();
		}
	}

	getFileListInDirectory(dirInfo, originalDirectory, searchName, filenameList, searchSubdirectories);
}

void ArchiveFile::getFileListInDirectory(const DetailedArchivedDirectoryInfo *dirInfo, const std::string &currentDirectory,
	const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const
{
	// ZH recurses unconditionally; searchSubdirectories is ignored there too.
	for (const auto &dirEntry : dirInfo->m_directories)
	{
		const DetailedArchivedDirectoryInfo *tempDirInfo = &dirEntry.second;
		std::string tempdirname = currentDirectory;
		if (!tempdirname.empty() && tempdirname.back() != '\\')
		{
			tempdirname += '\\';
		}
		tempdirname += tempDirInfo->m_directoryName;
		getFileListInDirectory(tempDirInfo, tempdirname, searchName, filenameList, searchSubdirectories);
	}

	for (const auto &fileEntry : dirInfo->m_files)
	{
		if (SearchStringMatches(fileEntry.second.m_filename, searchName))
		{
			std::string tempfilename = currentDirectory;
			if (!tempfilename.empty() && tempfilename.back() != '\\')
			{
				tempfilename += '\\';
			}
			tempfilename += fileEntry.second.m_filename;
			filenameList.insert(tempfilename);
		}
	}
}

const ArchivedFileInfo *ArchiveFile::getArchivedFileInfo(const std::string &filename) const
{
	std::string path = AsciiStringUtil::lowered(filename);
	std::string token;

	const DetailedArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	nextToken(path, &token, "\\/");

	while ((token.find('.') == std::string::npos) || (path.find('.') != std::string::npos))
	{
		DetailedArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.find(token);
		if (it == dirInfo->m_directories.end())
		{
			return nullptr;
		}
		dirInfo = &it->second;
		if (!nextToken(path, &token, "\\/"))
		{
			return nullptr; // path ran out before a file name; ZH would re-test the stale token
		}
	}

	ArchivedFileInfoMap::const_iterator it = dirInfo->m_files.find(token);
	if (it == dirInfo->m_files.end())
	{
		return nullptr;
	}
	return &it->second;
}
