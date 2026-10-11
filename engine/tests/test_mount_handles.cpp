// OpenBFME unit tests: archive backing handles. GPL-3.0.
// A disk archive keeps ONE backing handle (ArchiveHandle), shared by every mount of it and closed
// when the last owner goes away. The directory is parsed and every entry read through that handle,
// so a live mount can never read bytes from a file it did not verify. (Reopening by path per read
// let a same-size replacement silently change the bytes behind a mount, and cost ~45% on the full
// W3D read.)

#include "doctest.h"
#include "BigTestUtil.h"
#include "RetailTestMount.h"

#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"
#include "GameEngineDevice/Win32Device/Common/ArchiveHandle.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{

unsigned long handleCount()
{
#ifdef _WIN32
	DWORD n = 0;
	GetProcessHandleCount(GetCurrentProcess(), &n);
	return (unsigned long)n;
#else
	return 0;
#endif
}

struct TempDir
{
	fs::path dir;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		dir = fs::temp_directory_path() / ("openbfme-handles-" + std::to_string(rng()));
		fs::create_directories(dir);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
	std::string write(const std::string &name, const std::vector<std::uint8_t> &bytes) const
	{
		std::ofstream out(dir / name, std::ios::binary);
		out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
		return (dir / name).string();
	}
};

bool mountOne(Win32BIGFileSystem &fsys, const std::string &path, std::string *error)
{
	auto a = fsys.openArchiveFile(path, error);
	if (!a)
	{
		return false;
	}
	fsys.mountArchive(std::move(a), path, false);
	return true;
}

std::string readVirtual(Win32BIGFileSystem &fsys, const std::string &name)
{
	std::vector<std::uint8_t> out;
	std::string error;
	if (!fsys.readFile(name, out, &error))
	{
		return "<" + error + ">";
	}
	return bigtest::asString(out);
}

} // namespace

// Sol review round 3, P1: a same-size replacement changed what a live mount read (a.ini gave BBB)
TEST_CASE("a replaced archive cannot change what a live mount reads")
{
	TempDir tmp;
	const std::string path = tmp.write("x.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" }, { "b.ini", "BBB" } }));
	const std::vector<std::uint8_t> reversed = bigtest::makeBig("BIGF", { { "b.ini", "AAA" }, { "a.ini", "BBB" } }); // same size, entries swapped
	REQUIRE(reversed.size() == fs::file_size(path));

	Win32BIGFileSystem fsys;
	std::string error;
	REQUIRE_MESSAGE(mountOne(fsys, path, &error), error);
	CHECK(readVirtual(fsys, "a.ini") == "AAA");

	// attempt 1: overwrite in place
#ifdef _WIN32
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		CHECK_FALSE(out.is_open()); // the backing handle is deny-write
	}
#else
	// ACCEPTANCE STOP S-012 (docs/STOPS.md): an in-place overwrite cannot be prevented here and WOULD
	// change what the live mount reads, so it is deliberately not attempted; the mount REPORTS it
	// instead (test below: "the mount reports S-012 ...")
#endif
	// attempt 2: write a replacement next to it and rename it over the archive
	const std::string other = tmp.write("y.big", reversed);
	std::error_code ec;
	fs::rename(other, path, ec);
#ifdef _WIN32
	CHECK(ec); // deny-delete: the archive cannot be replaced while mounted
#endif
	// whichever attempts succeeded, the live mount still reads the original bytes
	CHECK(readVirtual(fsys, "a.ini") == "AAA");
	CHECK(readVirtual(fsys, "b.ini") == "BBB");
}

// Sol review round 4, P1: S-012 must be reported at runtime, not printed by a test. The mount report
// (ArchiveFileSystem::unverifiedIdentity) is asserted: empty where the platform enforces deny-write
// (_WIN32), an explicit S-012 line per archive elsewhere.
TEST_CASE("the mount reports S-012 exactly when the platform cannot enforce deny-write")
{
	TempDir tmp;
	const std::string path = tmp.write("s012.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" } }));
	Win32BIGFileSystem fsys;
	std::string error;
	REQUIRE_MESSAGE(mountOne(fsys, path, &error), error);
	const std::vector<std::string> report = fsys.unverifiedIdentity();
#ifdef _WIN32
	CHECK(report.empty());
#else
	REQUIRE(report.size() == 1);
	CHECK(report[0].rfind("S-012:", 0) == 0);
	CHECK(report[0].find(path) != std::string::npos);
#endif
}

TEST_CASE("an archive opened without deny-write is reported as S-012 on every platform")
{
	TempDir tmp;
	const std::string path = tmp.write("s012b.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" } }));
	Win32BIGFileSystem fsys;
	fsys.setReplacementProtectionForTests(false);
	std::string error;
	REQUIRE_MESSAGE(mountOne(fsys, path, &error), error);
	const std::vector<std::string> report = fsys.unverifiedIdentity();
	REQUIRE(report.size() == 1);
	CHECK(report[0].rfind("S-012:", 0) == 0);
	CHECK(report[0].find(path) != std::string::npos);
	CHECK(readVirtual(fsys, "a.ini") == "AAA"); // still readable: the report is not a refusal

	// in-memory archives cannot change underneath a mount: never reported
	Win32BIGFileSystem mem;
	auto bytes = std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", { { "m.ini", "M" } }));
	auto archive = mem.openArchiveFromMemory("mem.big", bytes, &error);
	REQUIRE_MESSAGE(archive, error);
	mem.mountArchive(std::move(archive), "mem.big", false);
	CHECK(mem.unverifiedIdentity().empty());
}

// Sol review round 4, P2: the registry is keyed on the file's identity, not on how the path is spelled
TEST_CASE("two paths to one file share one handle (hard link)")
{
	TempDir tmp;
	const std::string path = tmp.write("orig.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" } }));
	const std::string link = (tmp.dir / "link.big").string();
	std::error_code ec;
	fs::create_hard_link(path, link, ec);
	if (ec)
	{
		MESSAGE("SKIP: hard links unavailable here: " << ec.message());
		return;
	}
	std::string error;
	auto h1 = ArchiveHandle::open(path, &error);
	REQUIRE_MESSAGE(h1, error);
	auto h2 = ArchiveHandle::open(link, &error);
	REQUIRE_MESSAGE(h2, error);
	CHECK(h1.get() == h2.get());
}

// Sol review round 5, P2: where the platform cannot protect (POSIX), every default open used to create a
// new handle and overwrite the registry entry: descriptors leaked while liveHandles() under-counted.
TEST_CASE("retained opens of one file share one handle and the live count stays exact")
{
	TempDir tmp;
	const std::string path = tmp.write("many.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" } }));
	const std::string link = (tmp.dir / "many-link.big").string();
	std::error_code ec;
	const bool linked = !(fs::create_hard_link(path, link, ec), ec);
	const size_t base = ArchiveHandle::liveHandles();
	std::string error;
	std::vector<std::shared_ptr<ArchiveHandle>> held;
	for (int i = 0; i < 600; ++i) // more than the CRT's 512 streams: only sharing makes this possible
	{
		auto h = ArchiveHandle::open((linked && i % 2) ? link : path, &error);
		REQUIRE_MESSAGE(h, "open " << i << ": " << error);
		held.push_back(h);
	}
	for (const auto &h : held)
	{
		CHECK(h.get() == held[0].get());
	}
	CHECK(ArchiveHandle::liveHandles() == base + 1);
	held.erase(held.begin() + 1, held.end());
	CHECK(ArchiveHandle::liveHandles() == base + 1);
	std::uint8_t first[4] = {};
	CHECK(held[0]->readAt(0, first, 4)); // the surviving handle is still open and readable
	held.clear();
	CHECK(ArchiveHandle::liveHandles() == base);
}

TEST_CASE("a protected request meets an existing unprotected handle: shared where protection is impossible, exact count where it is not")
{
	TempDir tmp;
	const std::string path = tmp.write("mix.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" } }));
	const size_t base = ArchiveHandle::liveHandles();
	std::string error;
	auto unprotected = ArchiveHandle::open(path, &error, false);
	REQUIRE_MESSAGE(unprotected, error);
	CHECK_FALSE(unprotected->replacementProtected());
	auto requested = ArchiveHandle::open(path, &error, true);
	REQUIRE_MESSAGE(requested, error);
#ifdef _WIN32
	// Windows can protect, so the protected request gets its own deny-write handle and both are counted
	CHECK(requested.get() != unprotected.get());
	CHECK(requested->replacementProtected());
	CHECK(ArchiveHandle::liveHandles() == base + 2);
	requested.reset(); // releasing the newest handle must not hide the older one
	CHECK(ArchiveHandle::liveHandles() == base + 1);
#else
	// cannot protect: reuse the existing handle (the mount reports S-012), never a second descriptor
	CHECK(requested.get() == unprotected.get());
	CHECK_FALSE(requested->replacementProtected());
	CHECK(ArchiveHandle::liveHandles() == base + 1);
	requested.reset();
	CHECK(ArchiveHandle::liveHandles() == base + 1);
#endif
	std::uint8_t b[4] = {};
	CHECK(unprotected->readAt(0, b, 4));
	unprotected.reset();
	CHECK(ArchiveHandle::liveHandles() == base);
}

TEST_CASE("two files with the same size and contents get two handles")
{
	TempDir tmp;
	const std::vector<std::uint8_t> bytes = bigtest::makeBig("BIGF", { { "a.ini", "AAA" } });
	const std::string p1 = tmp.write("one.big", bytes);
	const std::string p2 = tmp.write("two.big", bytes);
	std::string error;
	auto h1 = ArchiveHandle::open(p1, &error);
	auto h2 = ArchiveHandle::open(p2, &error);
	REQUIRE(h1);
	REQUIRE(h2);
	CHECK(h1.get() != h2.get());
}

#ifdef _WIN32
TEST_CASE("a case-variant spelling of the same path shares one handle")
{
	TempDir tmp;
	const std::string path = tmp.write("MixedCase.big", bigtest::makeBig("BIGF", { { "a.ini", "AAA" } }));
	std::string lower = path;
	for (char &c : lower)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	std::string upper = path;
	for (char &c : upper)
	{
		c = (char)std::toupper((unsigned char)c);
	}
	REQUIRE(lower != path);
	std::string error;
	auto h1 = ArchiveHandle::open(path, &error);
	REQUIRE_MESSAGE(h1, error);
	auto h2 = ArchiveHandle::open(lower, &error);
	REQUIRE_MESSAGE(h2, error);
	auto h3 = ArchiveHandle::open(upper, &error);
	REQUIRE_MESSAGE(h3, error);
	CHECK(h1.get() == h2.get());
	CHECK(h1.get() == h3.get());
}
#endif

TEST_CASE("retail: _patch201.big in two spellings shares the handle the pure mount already holds")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail handle identity");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	const char *install = std::getenv("ROTWK_INSTALL");
	REQUIRE(install != nullptr);
	std::string base = install;
	if (!base.empty() && base.back() != '\\' && base.back() != '/')
	{
#ifdef _WIN32
		base += '\\';
#else
		base += '/'; // a backslash is a file-name character on Linux: "RotWK\\_patch201.big" is no file (the CI hosts' only suite failure)
#endif
	}
	const size_t before = ArchiveHandle::liveHandles();
	std::string error;
	auto a = ArchiveHandle::open(base + "_patch201.big", &error);
	REQUIRE_MESSAGE(a, error);
	CHECK(ArchiveHandle::liveHandles() == before); // the mount already owned it: shared, not reopened
#ifdef _WIN32
	std::string mangled = base;
	for (char &c : mangled)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	mangled += "_PATCH201.BIG";
	auto b = ArchiveHandle::open(mangled, &error);
	REQUIRE_MESSAGE(b, error);
	CHECK(a.get() == b.get());
	CHECK(ArchiveHandle::liveHandles() == before);
#endif
}

TEST_CASE("one backing handle per archive, shared by every mount and released with the last owner")
{
	TempDir tmp;
	const std::string path = tmp.write("shared.big", bigtest::makeBig("BIGF", { { "data\\ini\\f.ini", "shared" } }));
	const size_t baseLive = ArchiveHandle::liveHandles();
	const unsigned long baseHandles = handleCount();
	{
		Win32BIGFileSystem first;
		std::string error;
		REQUIRE_MESSAGE(mountOne(first, path, &error), error);
		CHECK(ArchiveHandle::liveHandles() == baseLive + 1);
		{
			Win32BIGFileSystem second;
			REQUIRE_MESSAGE(mountOne(second, path, &error), error);
			CHECK(ArchiveHandle::liveHandles() == baseLive + 1); // shared, not reopened
			CHECK(readVirtual(second, "data\\ini\\f.ini") == "shared");
		}
		CHECK(ArchiveHandle::liveHandles() == baseLive + 1); // the first mount still owns it
		CHECK(readVirtual(first, "data\\ini\\f.ini") == "shared");
	}
	CHECK(ArchiveHandle::liveHandles() == baseLive); // last owner gone: closed
#ifdef _WIN32
	CHECK(handleCount() <= baseHandles + 2);
#endif
	(void)baseHandles;
}

TEST_CASE("concurrent reads from several threads each get their own correct bytes")
{
	TempDir tmp;
	std::vector<std::pair<std::string, std::string>> files;
	for (int i = 0; i < 64; ++i)
	{
		std::string content;
		for (int k = 0; k < 200 + i * 13; ++k)
		{
			content.push_back((char)('a' + (i * 7 + k) % 26));
		}
		files.push_back({ "dir\\f" + std::to_string(i) + ".ini", content });
	}
	const std::string path = tmp.write("c.big", bigtest::makeBig("BIGF", files));
	Win32BIGFileSystem fsys;
	std::string error;
	REQUIRE_MESSAGE(mountOne(fsys, path, &error), error);

	std::atomic<int> mismatches{ 0 };
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; ++t)
	{
		threads.emplace_back([&, t]() {
			std::mt19937 rng((unsigned)t * 977 + 1);
			for (int i = 0; i < 2000; ++i)
			{
				const size_t k = rng() % files.size();
				std::vector<std::uint8_t> out;
				std::string err;
				if (!fsys.readFile(files[k].first, out, &err) || bigtest::asString(out) != files[k].second)
				{
					++mismatches;
				}
			}
		});
	}
	for (std::thread &th : threads)
	{
		th.join();
	}
	CHECK(mismatches == 0);
}

// Sequentially destroyed mounts never leaked (each Win32BIGFile closes its handle on destruction);
// only mounts retained SIMULTANEOUSLY can exhaust the CRT's 512-stream limit. One handle per archive
// and sharing between mounts keeps even simultaneous mounts cheap.
TEST_CASE("many sequential mounts of 150 archives return every handle")
{
	TempDir tmp;
	std::vector<std::string> paths;
	for (int i = 0; i < 150; ++i)
	{
		paths.push_back(tmp.write("a" + std::to_string(i) + ".big", bigtest::makeBig("BIGF", { { "data\\ini\\f" + std::to_string(i) + ".ini", "file " + std::to_string(i) } })));
	}
	const unsigned long before = handleCount();
	const size_t baseLive = ArchiveHandle::liveHandles();
	for (int round = 0; round < 5; ++round)
	{
		Win32BIGFileSystem fsys;
		for (const std::string &p : paths)
		{
			std::string error;
			REQUIRE_MESSAGE(mountOne(fsys, p, &error), "round " << round << ": " << error);
		}
		REQUIRE(fsys.getArchiveCount() == 150);
		CHECK(readVirtual(fsys, "data\\ini\\f149.ini") == "file 149");
	}
	CHECK(ArchiveHandle::liveHandles() == baseLive);
#ifdef _WIN32
	CHECK_MESSAGE(handleCount() <= before + 8, "handles after 5 mount cycles: " << handleCount() << " before: " << before);
#endif
	(void)before;
	// the CRT stream table is untouched: 200 fresh C streams still open (the retail mount of other tests already holds ~213 of the 512)
	std::vector<std::FILE *> files;
	for (int i = 0; i < 200; ++i)
	{
		std::FILE *f = std::fopen(paths[(size_t)i % paths.size()].c_str(), "rb");
		if (!f)
		{
			break;
		}
		files.push_back(f);
	}
	const size_t opened = files.size();
	for (std::FILE *f : files)
	{
		std::fclose(f);
	}
	CHECK(opened == 200);
}

// Re-measurement for the review (printed; the only assertion is that every byte was read)
TEST_CASE("retail: time to read every W3D file through the shared handles")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("W3D read timing");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	FilenameList list;
	// the whole tree, like the W3D corpus test: the pure mount holds 14,475 .w3d files, 4 of which sit
	// outside art\w3d (an earlier version of this test listed only art\w3d and so read 14,471)
	mount->fs->getFileListInDirectory(std::string(), "", "*.w3d", list, true);
	size_t files = 0;
	std::uint64_t bytes = 0;
	const auto t0 = std::chrono::steady_clock::now();
	for (const std::string &name : list)
	{
		std::vector<std::uint8_t> data;
		std::string error;
		REQUIRE_MESSAGE(mount->fs->readFile(name, data, &error), name << ": " << error);
		bytes += data.size();
		++files;
	}
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("W3D read: %zu files, %llu bytes in %.2f s\n", files, (unsigned long long)bytes, seconds);
	size_t outside = 0;
	for (const std::string &name : list)
	{
		if (AsciiStringUtil::lowered(name).compare(0, 8, "art\\w3d\\") != 0)
		{
			std::printf("  .w3d outside art\\w3d: %s\n", name.c_str());
			++outside;
		}
	}
	CHECK(files == 14475); // tests/data/w3d-survey.json files_w3d
	CHECK(outside == 4);
	CHECK(bytes > 1000000000ull);
}
