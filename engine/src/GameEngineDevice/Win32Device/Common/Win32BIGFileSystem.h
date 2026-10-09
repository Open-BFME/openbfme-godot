// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngineDevice/Include/Win32Device/Common/Win32BIGFileSystem.h and
// GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp, with the BFME changes:
//  * "BIG4" is accepted next to ZH's "BIGF" (BFME2/RotWK ship both; ZH only knows BIGF)
//  * loadBigFilesFromDirectory orders archives the BFME way: stable_sort with AsciiString
//    operator< (strcmp), then reverse, i.e. DESCENDING strcmp order, first loaded wins.
//    Evidence: Open-BFME-1 game/GameEngineDevice/Source/Win32Device/Common/
//    Win32BIGFileSystem_loadBigFiles.cpp (retail 0x009CDB90, length-matched step by step).
//  * init() does not glob; retail archives are mounted from an explicit policy
//    (RetailArchivePolicy), which applies the same ordering.

#pragma once

#include "Common/ArchiveFileSystem.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Win32BIGFile;

class Win32BIGFileSystem : public ArchiveFileSystem
{
public:
	Win32BIGFileSystem();
	~Win32BIGFileSystem() override;

	std::unique_ptr<ArchiveFile> openArchiveFile(const std::string &filename, std::string *error) override;

	// Test hook: false opens archives without the deny-write share mode, i.e. the situation of a
	// platform that cannot enforce it (S-012). Never used by product code.
	void setReplacementProtectionForTests(bool protect) { m_protect = protect; }

	// Parse synthetic/in-memory BIG bytes (unit tests).
	std::unique_ptr<ArchiveFile> openArchiveFromMemory(const std::string &name,
		std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::string *error);

	// BFME1 retail loadBigFilesFromDirectory: every file under dir matching fileMask
	// (recursive), in BFME load order, into the tree with the given overwrite flag.
	// Any archive that fails to parse is an error (ZH skipped it silently).
	bool loadBigFilesFromDirectory(const std::string &dir, const std::string &fileMask, bool overwrite, std::string *error);

private:
	bool m_protect = true;
};

// Retail BFME archive load order: stable_sort by strcmp, then reverse (descending strcmp).
// With overwrite == false the first entry of the result wins every conflict.
std::vector<std::string> BFMEArchiveLoadOrder(std::vector<std::string> archiveNames);
