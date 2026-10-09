// OpenBFME unit tests: BIG archive parsing on synthetic bytes. GPL-3.0.

#include "doctest.h"
#include "BigTestUtil.h"

#include "Common/ArchiveFile.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFile.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <random>

using namespace bigtest;
namespace stdfs = std::filesystem;

namespace
{
struct TempDir
{
	stdfs::path path;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = stdfs::temp_directory_path() / ("openbfme-bigtest-" + std::to_string(rng()));
		stdfs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		stdfs::remove_all(path, ec);
	}
};

std::unique_ptr<ArchiveFile> openMem(Win32BIGFileSystem &fs, const std::vector<std::uint8_t> &bytes, std::string *error)
{
	return fs.openArchiveFromMemory("test.big", std::make_shared<const std::vector<std::uint8_t>>(bytes), error);
}
}

TEST_CASE("BIGF and BIG4 archives parse and every entry reads back")
{
	for (const char *magic : { "BIGF", "BIG4" })
	{
		CAPTURE(magic);
		Win32BIGFileSystem fs;
		std::string error;
		auto bytes = makeBig(magic, { { "data\\ini\\weapon.ini", "Weapon A\nEnd\n" }, { "art/w3d/gu/gumaarms_skn.w3d", "W3D!" }, { "root.txt", "" } });
		auto archive = openMem(fs, bytes, &error);
		REQUIRE_MESSAGE(archive, error);
		auto *big = static_cast<Win32BIGFile *>(archive.get());
		CHECK(big->m_magic == magic);
		CHECK(big->m_fileCount == 3);

		std::vector<std::uint8_t> out;
		REQUIRE(archive->readFile("data\\ini\\weapon.ini", out, &error));
		CHECK(asString(out) == "Weapon A\nEnd\n");
		// lookups are case-insensitive and accept either separator (ZH lower-cases, splits on \ and /)
		REQUIRE(archive->readFile("ART\\W3D\\GU\\GUMAArms_SKN.w3d", out, &error));
		CHECK(asString(out) == "W3D!");
		REQUIRE(archive->readFile("root.txt", out, &error));
		CHECK(out.empty());
		CHECK_FALSE(archive->readFile("data\\ini\\armor.ini", out, &error));
	}
}

TEST_CASE("unknown magic is rejected loudly")
{
	Win32BIGFileSystem fs;
	std::string error;
	auto bytes = makeBig("BIGX", { { "a.txt", "a" } });
	CHECK_FALSE(openMem(fs, bytes, &error));
	CHECK(error.find("BIGF/BIG4") != std::string::npos);
}

TEST_CASE("declared archive size is not trusted or enforced (ZH ignores it)")
{
	Win32BIGFileSystem fs;
	std::string error;
	for (long long declared : { 0LL, 24LL, 999999LL })
	{
		auto bytes = makeBig("BIG4", { { "a.txt", "alpha" } }, declared);
		auto archive = openMem(fs, bytes, &error);
		REQUIRE_MESSAGE(archive, error);
		std::vector<std::uint8_t> out;
		REQUIRE(archive->readFile("a.txt", out, &error));
		CHECK(asString(out) == "alpha");
	}
}

TEST_CASE("truncated directory is an error")
{
	Win32BIGFileSystem fs;
	std::string error;
	auto bytes = makeBig("BIGF", { { "long\\path\\name.txt", "x" } });
	bytes.resize(16 + 8 + 4); // cut inside the first entry name
	CHECK_FALSE(openMem(fs, bytes, &error));
	CHECK(error.find("truncated") != std::string::npos);
}

TEST_CASE("an entry pointing past the end is rejected when the directory is parsed")
{
	Win32BIGFileSystem fs;
	std::string error;
	auto bytes = makeBig("BIGF", { { "a.txt", "abcdef" } });
	bytes.resize(bytes.size() - 3);
	CHECK_FALSE(openMem(fs, bytes, &error));
	CHECK(error.find("past the end") != std::string::npos);
	CHECK(error.find("a.txt") != std::string::npos);
}

TEST_CASE("an entry size of 0xFFFFFFFF in a tiny archive is rejected before anything is allocated")
{
	// Directory entry layout: 16-byte header, then offset BE at +0 and size BE at +4 of the first entry.
	auto bytes = makeBig("BIG4", { { "a.txt", "abc" } });
	bytes[20] = bytes[21] = bytes[22] = bytes[23] = 0xFF;
	Win32BIGFileSystem fs;
	std::string error;
	auto archive = openMem(fs, bytes, &error);
	CHECK_FALSE(archive);
	CHECK(error.find("past the end") != std::string::npos);

	// same archive on disk
	TempDir tmp;
	stdfs::path path = tmp.path / "huge.big";
	{
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
	}
	error.clear();
	CHECK_FALSE(fs.openArchiveFile(path.u8string(), &error));
	CHECK(error.find("past the end") != std::string::npos);

	// an offset that wraps a 32-bit sum is caught too (offset + size computed in 64 bits)
	auto wrap = makeBig("BIG4", { { "a.txt", "abc" } });
	wrap[16] = wrap[17] = wrap[18] = 0xFF; // offset 0xFFFFFFxx
	wrap[19] = 0xF0;
	wrap[20] = wrap[21] = wrap[22] = 0; // size 0x00000020
	wrap[23] = 0x20;
	CHECK_FALSE(openMem(fs, wrap, &error));
}

TEST_CASE("a directory that declares more entries than the archive can hold is rejected")
{
	auto bytes = makeBig("BIGF", { { "a.txt", "abc" } });
	bytes[8] = 0x7F; // 0x7Fxxxxxx entries
	Win32BIGFileSystem fs;
	std::string error;
	CHECK_FALSE(openMem(fs, bytes, &error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("a disk-backed archive cannot be cut under a live mount (Windows), and a short read never returns bytes")
{
	TempDir tmp;
	stdfs::path path = tmp.path / "shrinks.big";
	auto bytes = makeBig("BIGF", { { "a.txt", std::string(4096, 'x') } });
	{
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
	}
	Win32BIGFileSystem fs;
	std::string error;
	auto archive = fs.openArchiveFile(path.u8string(), &error);
	REQUIRE_MESSAGE(archive, error);
	bool cut = false;
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc); // try to cut the archive after it was opened
		cut = out.is_open();
		if (cut)
		{
			out.write(reinterpret_cast<const char *>(bytes.data()), 100);
		}
	}
#ifdef _WIN32
	CHECK_FALSE(cut); // the backing handle is deny-write: the mounted archive cannot change
#endif
	std::vector<std::uint8_t> out;
	const bool ok = archive->readFile("a.txt", out, &error);
	if (cut)
	{
		// where the file system allowed the cut, the read must fail cleanly and return nothing
		CHECK_FALSE(ok);
		CHECK(out.empty());
	}
	else
	{
		CHECK(ok);
		CHECK(out.size() == 4096);
	}
}

TEST_CASE("same path twice inside one archive: the later directory entry wins (ZH map assignment)")
{
	Win32BIGFileSystem fs;
	std::string error;
	auto bytes = makeBig("BIGF", { { "dup.txt", "first" }, { "DUP.TXT", "second" } });
	auto archive = openMem(fs, bytes, &error);
	REQUIRE(archive);
	std::vector<std::uint8_t> out;
	REQUIRE(archive->readFile("dup.txt", out, &error));
	CHECK(asString(out) == "second");
}

TEST_CASE("archive file list and wildcard search match ZH SearchStringMatches")
{
	CHECK(SearchStringMatches("weapon.ini", "*"));
	CHECK(SearchStringMatches("weapon.ini", "*.ini"));
	CHECK(SearchStringMatches("weapon.ini", "weap?n.ini"));
	CHECK_FALSE(SearchStringMatches("weapon.ini", "*.big"));
	CHECK_FALSE(SearchStringMatches("", "*"));

	Win32BIGFileSystem fs;
	std::string error;
	auto archive = openMem(fs, makeBig("BIGF", { { "data\\ini\\a.ini", "1" }, { "data\\ini\\object\\b.ini", "2" }, { "maps\\c.map", "3" } }), &error);
	REQUIRE(archive);
	FilenameList list;
	archive->getFileListInDirectory("", "", "*.ini", list, true);
	REQUIRE(list.size() == 2);
	CHECK(list.count("data\\ini\\a.ini") == 1);
	CHECK(list.count("data\\ini\\object\\b.ini") == 1);
}
