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
// readFile() filling a byte vector; archives are owned by unique_ptr; readFile() and doesFileExist()
// also take the local step of retail's FileSystem::openFile for one folder (setLocalFolder, lane MOD-5).
//
// Lane MODS-1: RotWK's FileSystem puts local (loose) files in front of this tree when the
// prefer-local-files flag RW 0xDEC490 is set (`-mod`, RW 0x7BADB9). FileSystem::openFile RW 0xA149A2,
// doesFileExist RW 0xA14AEB and getFileInfo RW 0xA14BA8 then try "<modDir>\<file>" (RW 0xDEC498, the
// mod folder), "lang\<language>\<file>" and "<file>" on the local file system, and only then the
// archives; getFileListInDirectory RW 0xA14D2B adds the mod folder's files to the union. The port
// keeps that order in readFile / doesFileExist / getFileListInDirectory. The mod folder is a frozen
// index taken at mount (ModLooseIndex, Common/ModMount.h): retail asks the disk on every open, the
// port reads the files the identity hashed, matched case-insensitively as Windows does. The two
// install-folder lookups never find a file here: the -mod mount refuses an install whose loose files
// would answer them (Common/ModMount.h, PLAN rule 7, stop S-5300).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class ArchiveFile;

// Lane MODS-1: the loose files of the mounted mod folder (RW 0xDEC498), taken once at mount.
struct ModLooseFile
{
	std::string virtualPath; // as listed: the path below the mod folder with '\\' separators, on-disk case
	std::string diskPath;
	std::uint64_t size = 0;
	long long mtime = 0;     // last-write time at mount: a file changed after the mount is a read error
	std::string md5;
};

struct ModLooseIndex
{
	std::string root;                         // the mod folder
	std::vector<ModLooseFile> files;          // byte order of virtualPath
	std::map<std::string, size_t> byKey;      // ModLooseIndex::key(virtualPath) -> files index

	// lower case, '/' -> '\\', repeated separators collapsed, a leading separator and "." dropped, ".." applied
	// ("" when the path leaves the root): how Windows finds "<modDir>\\<file>" in a case-insensitive folder
	static std::string key(const std::string &path);
	const ModLooseFile *find(const std::string &path) const;
};

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

	// Read a virtual file from whichever archive owns it, else from the local folder (below). False + *error if absent/unreadable.
	bool readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error);
	bool doesFileExist(const std::string &filename) const;
	std::string getArchiveFilenameForFile(const std::string &filename) const;

	// OpenBFME, lane MOD-5: the one folder read from disk. Retail's FileSystem::openFile (RW 0xA149A2) asks the archives (TheArchiveFileSystem,
	// RW 0xDEC9A4) first and, when they do not hold the file and local files are not preferred (RW 0xDEC490, set by -mod), TheLocalFileSystem
	// (RW 0xDEC9AC); its filter (RW 0xA147FB) lets every path with a drive letter through. That is how a user map (UserDataFolder::userMapFolder)
	// and its map.ini are read. OpenBFME has no local file system otherwise (loose files in the install folders are refused: PLAN rule 7, stop
	// S-039), so only a file the archives do not hold, whose path lies under this folder (absolute, either separator, compared without case),
	// is read from disk; each part of its path below the folder is matched without case, as Windows opens it, and a part may not step out
	// of it ("..", or a ':' for a drive or a stream); links inside it are followed, as retail follows them. "" (the default) reads nothing
	// from disk. The archives' file lists (getFileListInDirectory) do not include it (retail's FileSystem lists local files too: stop
	// S-5160). Retail's -mod precedence (RW 0xDEC490, lane MOD-1) asks the local files first; this step comes after the archives only.
	void setLocalFolder(const std::string &folder);
	const std::string &localFolder() const { return m_localFolder; }

	// OpenBFME, lane PROFILE-3 (the simdata coverage check): every path asked of this file system from now on goes into *trace as
	// "read <path>", "exists <path>" or "list <directory><pattern>" (lower case, '\\' separators); nullptr stops it. A test hook: the
	// headless peer's --read-trace. Never set in a product game
	void setReadTrace(std::set<std::string> *trace) { m_readTrace = trace; }

	// Union of every mounted archive's file list (ZH getFileListInDirectory).
	void getFileListInDirectory(const std::string &currentDirectory, const std::string &originalDirectory,
		const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const;

	// Adds an already-opened archive under archiveFilename. ZH does this inline in
	// loadBigFilesFromDirectory; split out so ordered mounting (RetailArchivePolicy) and the
	// tests can drive it directly.
	void mountArchive(std::unique_ptr<ArchiveFile> archiveFile, const std::string &archiveFilename, bool overwrite);

	size_t getArchiveCount() const { return m_archiveFileMap.size(); }

	// Lane MODS-1: the prefer-local-files flag (RW 0xDEC490) and the mod folder's loose layer (RW 0xDEC498).
	// Set together by the -mod mount only (MountRetailMod); without them every lookup is the archive tree's, as before.
	void setModLooseLayer(ModLooseIndex index);
	bool preferLocalFiles() const { return m_preferLocalFiles; }
	const ModLooseIndex &modLooseLayer() const { return m_modLoose; }

	// Lane MODS-3: a file the mount derives from the mod's own files (ModMount: a mod's GameData blocks outside data\ini\gamedata.ini merged into it).
	// It answers before every other layer; `origin` names it in sourcesOf ("derived:<origin>").
	// `sources`: the files it was derived from (a later mount re-derives it from them and its own); clearDerivedFile drops it (the real file answers again)
	void setDerivedFile(const std::string &filename, std::vector<std::uint8_t> bytes, const std::string &origin, std::vector<std::string> sources = {});
	void clearDerivedFile(const std::string &filename);
	std::vector<std::string> derivedSources(const std::string &filename) const;

	// Lane MODS-1 resolver: every source holding `filename`, the winner first ("mod-folder:<virtual path>", then the
	// archives: the owner the tree resolves first, then every other mounted archive that has the entry, by archive name).
	std::vector<std::string> sourcesOf(const std::string &filename) const;

	// OpenBFME mount report for ACCEPTANCE STOP S-012: one "S-012: ..." line per mounted archive
	// whose identity the platform cannot enforce (ArchiveFile::isIdentityEnforced() == false).
	// Empty when every mounted archive is protected. Callers must surface a non-empty result.
	std::vector<std::string> unverifiedIdentity() const;

protected:
	virtual void loadIntoDirectoryTree(const ArchiveFile *archiveFile, const std::string &archiveFilename, bool overwrite = false);

	ArchiveFileMap m_archiveFileMap;
	ArchivedDirectoryInfo m_rootDirectory;

private:
	// the file on disk for `filename` under the local folder ("" when there is none)
	std::string findLocalFile(const std::string &filename) const;

	std::string m_localFolder;
	bool m_preferLocalFiles = false; // RW 0xDEC490
	std::set<std::string> *m_readTrace = nullptr; // lane PROFILE-3: setReadTrace
	void trace(const char *what, const std::string &path) const;
	ModLooseIndex m_modLoose;        // RW 0xDEC498's folder, frozen at mount
	struct DerivedFile
	{
		std::vector<std::uint8_t> bytes;
		std::string origin;
		std::vector<std::string> sources;
	};
	std::map<std::string, DerivedFile> m_derived; ///< lane MODS-3: by ModLooseIndex::key
	const DerivedFile *derived(const std::string &filename) const;
};
