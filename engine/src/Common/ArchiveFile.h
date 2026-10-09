// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Include/Common/ArchiveFile.h and
// GameEngine/Source/Common/System/ArchiveFile.cpp.

#pragma once

#include "Common/ArchiveFileSystem.h"

#include <cstdint>
#include <string>
#include <vector>

class ArchiveFile
{
public:
	ArchiveFile();
	virtual ~ArchiveFile();

	// ZH openFile() returns a RAMFile over the archived bytes; the rebuild copies them out.
	virtual bool readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error) = 0;
	virtual std::string getName() const = 0; ///< archive file name
	virtual std::string getPath() const = 0; ///< full path of the archive on disk

	// OpenBFME (ACCEPTANCE STOP S-012, docs/STOPS.md). ZH keeps the archive File* open for the life
	// of the file system, so the archive a mount verified is the archive it reads. That only holds
	// while the platform stops other writers replacing the file. True when this archive's identity
	// is enforced (deny-write handle on Windows, or in-memory bytes); false = unverified identity,
	// which the mount reports (ArchiveFileSystem::unverifiedIdentity).
	virtual bool isIdentityEnforced() const { return true; }

	void getFileListInDirectory(const std::string &currentDirectory, const std::string &originalDirectory,
		const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const;
	void getFileListInDirectory(const DetailedArchivedDirectoryInfo *dirInfo, const std::string &currentDirectory,
		const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const;

	void addFile(const std::string &path, const ArchivedFileInfo *fileInfo); ///< add this file to our directory tree

	const ArchivedFileInfo *getArchivedFileInfo(const std::string &filename) const;

protected:
	DetailedArchivedDirectoryInfo m_rootDirectory;
};

// ZH ArchiveFile.cpp SearchStringMatches ('*' and '?' wildcards). Exposed for tests.
bool SearchStringMatches(const std::string &str, const std::string &searchString);
