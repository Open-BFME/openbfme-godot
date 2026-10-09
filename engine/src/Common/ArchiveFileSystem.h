// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Include/Common/ArchiveFileSystem.h and
// GameEngine/Source/Common/System/ArchiveFileSystem.cpp.
//
// Semantics kept from ZH:
//  * one global directory tree maps every lower-cased virtual path to the archive that owns it
//  * loadIntoDirectoryTree(overwrite = false) keeps the FIRST archive that supplied a path, so
//    load order is precedence: whatever is mounted first wins
//  * directory tokenisation quirk: a token containing '.' is still treated as a directory while
//    the rest of the path contains another '.'
// Changes: std::string replaces AsciiString; openFile() returning a RAMFile is replaced by
// readFile() filling a byte vector; archives are owned by unique_ptr.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class ArchiveFile;

struct FilenameLessNoCase
{
	bool operator()(const std::string &a, const std::string &b) const;
};

// ZH FileSystem.h: typedef std::set<AsciiString, rts::less_than_nocase<AsciiString> > FilenameList;
typedef std::set<std::string, FilenameLessNoCase> FilenameList;

class ArchivedFileInfo
{
public:
	std::string m_filename;
	std::string m_archiveFilename;
	std::uint32_t m_offset = 0;
	std::uint32_t m_size = 0;
};

class ArchivedDirectoryInfo;
class DetailedArchivedDirectoryInfo;

typedef std::map<std::string, DetailedArchivedDirectoryInfo> DetailedArchivedDirectoryInfoMap;
typedef std::map<std::string, ArchivedDirectoryInfo> ArchivedDirectoryInfoMap;
typedef std::map<std::string, ArchivedFileInfo> ArchivedFileInfoMap;
typedef std::map<std::string, std::unique_ptr<ArchiveFile>> ArchiveFileMap;
typedef std::map<std::string, std::string> ArchivedFileLocationMap; // file name -> archive file name

class ArchivedDirectoryInfo
{
public:
	std::string m_directoryName;
	ArchivedDirectoryInfoMap m_directories;
	ArchivedFileLocationMap m_files;

	void clear()
	{
		m_directoryName.clear();
		m_directories.clear();
		m_files.clear();
	}
};

class DetailedArchivedDirectoryInfo
{
public:
	std::string m_directoryName;
	DetailedArchivedDirectoryInfoMap m_directories;
	ArchivedFileInfoMap m_files;

	void clear()
	{
		m_directoryName.clear();
		m_directories.clear();
		m_files.clear();
	}
};

class ArchiveFileSystem
{
public:
	ArchiveFileSystem();
	virtual ~ArchiveFileSystem();

	// Parse an archive's directory. Returns nullptr and sets *error on failure.
	virtual std::unique_ptr<ArchiveFile> openArchiveFile(const std::string &filename, std::string *error) = 0;

	// Read a virtual file from whichever archive owns it. False + *error if absent/unreadable.
	bool readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error);
	bool doesFileExist(const std::string &filename) const;
	std::string getArchiveFilenameForFile(const std::string &filename) const;

	// Union of every mounted archive's file list (ZH getFileListInDirectory).
	void getFileListInDirectory(const std::string &currentDirectory, const std::string &originalDirectory,
		const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const;

	// Adds an already-opened archive under archiveFilename. ZH does this inline in
	// loadBigFilesFromDirectory; split out so ordered mounting (RetailArchivePolicy) and the
	// tests can drive it directly.
	void mountArchive(std::unique_ptr<ArchiveFile> archiveFile, const std::string &archiveFilename, bool overwrite);

	size_t getArchiveCount() const { return m_archiveFileMap.size(); }

	// OpenBFME mount report for ACCEPTANCE STOP S-012: one "S-012: ..." line per mounted archive
	// whose identity the platform cannot enforce (ArchiveFile::isIdentityEnforced() == false).
	// Empty when every mounted archive is protected. Callers must surface a non-empty result.
	std::vector<std::string> unverifiedIdentity() const;

protected:
	virtual void loadIntoDirectoryTree(const ArchiveFile *archiveFile, const std::string &archiveFilename, bool overwrite = false);

	ArchiveFileMap m_archiveFileMap;
	ArchivedDirectoryInfo m_rootDirectory;
};
