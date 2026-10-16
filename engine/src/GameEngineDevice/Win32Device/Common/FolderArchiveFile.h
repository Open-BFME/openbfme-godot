// OpenBFME. GPL-3.0.
// Lane MODS-1: a mod folder as an archive. RotWK 2.01's -mod "<folder>" puts the folder in front of the game's files: its loose files
// (data\ini\..., art\..., maps\..., the *.const / *.apt of the menus at its root) override the archives, and the *.big inside it are mounted
// too. FolderArchiveFile is the loose part: every regular file under the folder, by its path relative to the folder (the BIG file
// layout: lower-case names, '\' separators), read from disk when asked. Programs and libraries (.exe .dll .so .dylib .bat .cmd .sh) and
// dot files are never listed. The *.big files are left to the caller (mounted as archives of their own, below the loose files).
// Identity is not enforced: the files can change on disk (isIdentityEnforced false; the mount reports it).
#pragma once

#include "Common/ArchiveFile.h"

#include <map>
#include <string>
#include <vector>

class FolderArchiveFile : public ArchiveFile
{
public:
	// Lists `root` recursively. False + *error when it is not a readable folder. `skipExtensions` (lower case, with the dot): files left out
	// (the BFME2-themed menu view leaves a mod's RotWK menu movies out: .apt / .const)
	bool open(const std::string &root, std::string *error, const std::vector<std::string> &skipExtensions = {});
	size_t skippedByExtension() const { return m_skippedByExtension; }
	bool readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error) override;
	std::string getName() const override { return m_name; }
	std::string getPath() const override { return m_root; }
	bool isIdentityEnforced() const override { return false; }

	size_t fileCount() const { return m_disk.size(); }
	// the *.big files found under the folder (disk paths, sorted by their relative path, descending: ZH's archive order, first one wins)
	const std::vector<std::string> &bigFiles() const { return m_bigs; }
	size_t skippedPrograms() const { return m_skippedPrograms; }
	// the virtual paths (key form) of the listed files, sorted
	std::vector<std::string> paths() const
	{
		std::vector<std::string> out;
		for (const auto &kv : m_disk)
			out.push_back(kv.first);
		return out;
	}

	// the key a virtual path is looked up by (lower case, '\' separators)
	static std::string key(const std::string &path);

private:
	std::string m_root, m_name;
	std::map<std::string, std::string> m_disk; // key -> disk path
	std::vector<std::string> m_bigs;
	size_t m_skippedPrograms = 0;
	size_t m_skippedByExtension = 0;
};

// Lane MODS-1: files made in memory (the merged GameData of a mod), mounted over the others like an archive
class MemoryArchiveFile : public ArchiveFile
{
public:
	explicit MemoryArchiveFile(std::string name) : m_name(std::move(name)) {}
	void put(const std::string &path, std::vector<std::uint8_t> bytes);
	bool readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error) override;
	std::string getName() const override { return m_name; }
	std::string getPath() const override { return m_name; }

private:
	std::string m_name;
	std::map<std::string, std::vector<std::uint8_t>> m_files;
};

// RotWK reads a GameData block from any INI file it loads (its block table is shared): a mod may keep its GameData in
// Data\INI\Object\*.ini (RJ RotWK leaves Data\INI\GameData.ini a comment and puts the block in Data\INI\Object\GameData.ini). The
// object files load after GameData.ini, so their rows come later. Returns the GameData.ini text whose one GameData block holds
// `gameDataIni`'s rows and then each `objectFiles` block's rows in order (the other lines of `gameDataIni`, such as #define, first).
std::string MergeGameDataBlocks(const std::string &gameDataIni, const std::vector<std::pair<std::string, std::string>> &objectFiles);
// the GameData block rows of an INI text (between "GameData" and its "End"), "" when it has none
std::string GameDataBlockRows(const std::string &text, std::string *outside = nullptr);
