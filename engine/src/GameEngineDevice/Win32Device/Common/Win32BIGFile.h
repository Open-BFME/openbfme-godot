// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngineDevice/Include/Win32Device/Common/Win32BIGFile.h and
// GameEngineDevice/Source/Win32Device/Common/Win32BIGFile.cpp. The name is kept for diffing;
// the implementation is portable C++ (a shared ArchiveHandle, kept open like ZH).

#pragma once

#include "Common/ArchiveFile.h"
#include "GameEngineDevice/Win32Device/Common/ArchiveHandle.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

class Win32BIGFile : public ArchiveFile
{
public:
	Win32BIGFile();
	~Win32BIGFile() override;

	bool readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error) override;
	std::string getName() const override { return m_name; }
	std::string getPath() const override { return m_path; }
	// S-012: a disk archive is enforced only while its handle denies writers; in-memory bytes cannot change.
	bool isIdentityEnforced() const override { return !m_handle || m_handle->replacementProtected(); }

	// Replaces ZH attachFile(File*): the archive shares one backing handle (ArchiveHandle) that the
	// directory was parsed through and every read uses.
	void attachHandle(std::shared_ptr<ArchiveHandle> handle);

	// In-memory archives, used by unit tests (synthetic BIG bytes built in the test).
	void attachMemory(const std::string &name, std::shared_ptr<const std::vector<std::uint8_t>> bytes);

	// Header facts, kept for diagnostics only. ZH never validates them and neither do we:
	// retail archives exist whose declared size differs from the file size.
	std::string m_magic;
	std::uint32_t m_declaredArchiveSize = 0; // little-endian in the header
	std::uint32_t m_declaredHeaderEnd = 0;   // big-endian first-data offset
	std::uint32_t m_fileCount = 0;

private:
	std::string m_name;
	std::string m_path;
	std::shared_ptr<ArchiveHandle> m_handle;
	std::shared_ptr<const std::vector<std::uint8_t>> m_memory;
};
