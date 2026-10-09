// OpenBFME unit tests. GPL-3.0.
// Lane PERF-2 (Sol r1 regressions): the parallel loading reports exactly what the sequential loading reports.
//   * INI::loadDirectory parses a window of files ahead on the client pool: an exception of a LATER file's read must not overtake an EARLIER file's
//     error (it is rethrown at its file's turn);
//   * MountRetailArchives hashes on the client pool: the same archive met twice (a duplicated install) is hashed once and the second check is a cache
//     hit, as in the sequential pass (archivesHashed 1, md5CacheHits 1).
// Each runs with the client pool at 1 and at 4 threads.

#include "doctest.h"

#include "BigTestUtil.h"
#include "IniTestUtil.h"

#include "Common/JobSystem.h"
#include "Common/MD5.h"
#include "Common/RetailArchivePolicy.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct ClientThreadsGuard
{
	int before = JobSystem::client().threadCount();
	~ClientThreadsGuard() { JobSystem::client().setThreadCount(before); }
};

// Data\INI\a.ini holds an unknown block (an INI error); reading Data\INI\b.ini throws (a read failure the archive reports by exception)
class ThrowingArchive : public ArchiveFile
{
public:
	ThrowingArchive()
	{
		for (const char *name : { "a.ini", "b.ini" })
		{
			ArchivedFileInfo f;
			f.m_filename = name;
			f.m_archiveFilename = "probe.big";
			addFile("data\\ini\\", &f);
		}
	}
	bool readFile(const std::string &path, std::vector<std::uint8_t> &out, std::string *) override
	{
		if (path.find("b.ini") != std::string::npos)
		{
			throw std::runtime_error("b.ini read exception");
		}
		out = initest::toBytes("UnknownBlockOfPerfTwo x\nEnd\n");
		return true;
	}
	std::string getName() const override { return "probe.big"; }
	std::string getPath() const override { return "probe.big"; }
};

std::string errorOf(const std::function<void()> &load)
{
	try
	{
		load();
	}
	catch (const std::exception &e)
	{
		return e.what();
	}
	return std::string();
}
} // namespace

TEST_CASE("perf2 loading: INI::loadDirectory reports an earlier file's error before a later file's read exception, as the sequential loads do")
{
	ClientThreadsGuard guard;
	Win32BIGFileSystem fs;
	fs.mountArchive(std::make_unique<ThrowingArchive>(), "probe.big", false);
	INIEnvironment env;
	env.fileSystem = &fs;
	const std::string serial = errorOf([&] {
		INI ini(env);
		ini.load("Data\\INI\\a.ini", INI_LOAD_OVERWRITE);
		ini.load("Data\\INI\\b.ini", INI_LOAD_OVERWRITE);
	});
	REQUIRE(!serial.empty());
	CHECK(serial.find("b.ini read exception") == std::string::npos); // a.ini's own error comes first
	for (int threads : { 1, 4 })
	{
		JobSystem::client().setThreadCount(threads);
		INFO(threads << " client threads");
		const std::string directory = errorOf([&] {
			INI ini(env);
			ini.loadDirectory("Data\\INI", false, INI_LOAD_OVERWRITE);
		});
		CHECK(directory == serial);
		// with a per-file error handler (the subsystem loads' mode) a.ini's INI error is handed over and the load goes on to b.ini, whose read
		// exception (not an INIException) then leaves the load, as it would sequentially
		std::vector<std::string> handled;
		INILoadDirectoryOptions options;
		options.onFileError = [&handled](const std::string &file, const INIException &) { handled.push_back(file); };
		const std::string withHandler = errorOf([&] {
			INI ini(env);
			ini.loadDirectory("Data\\INI", false, INI_LOAD_OVERWRITE, options);
		});
		CHECK(handled.size() == 1u);
		CHECK(withHandler == "b.ini read exception");
	}
}

TEST_CASE("perf2 loading: a duplicated install hashes its archive once and takes the second check from the cache (archivesHashed 1, md5CacheHits 1)")
{
	ClientThreadsGuard guard;
	namespace fsys = std::filesystem;
	const fsys::path root = fsys::temp_directory_path() / "openbfme_perf2_mount_probe";
	fsys::create_directories(root);
	const std::vector<std::uint8_t> bytes = bigtest::makeBig("BIGF", { { "x.txt", "data" } });
	{
		std::ofstream o(root / "INI.big", std::ios::binary);
		o.write((const char *)bytes.data(), (std::streamsize)bytes.size());
	}
	RetailInstall install;
	install.root = root.string();
	install.label = "same";
	install.policy.game = "test";
	install.policy.patch = "test";
	install.policy.archives.push_back({ "INI.big", MD5::ofBytes(bytes.data(), bytes.size()), bytes.size() });
	for (int threads : { 1, 4 })
	{
		JobSystem::client().setThreadCount(threads);
		INFO(threads << " client threads");
		RetailMountOptions options;
		options.md5CachePath = (root / "md5cache.json").string();
		fsys::remove(options.md5CachePath);
		Win32BIGFileSystem mounts;
		const RetailMountReport report = MountRetailArchives(mounts, { install, install }, options);
		CHECK(report.ok);
		CHECK(report.archivesHashed == 1);
		CHECK(report.md5CacheHits == 1);
		// a second mount with the cache written: both checks are hits, nothing is hashed
		Win32BIGFileSystem again;
		const RetailMountReport cached = MountRetailArchives(again, { install, install }, options);
		CHECK(cached.archivesHashed == 0);
		CHECK(cached.md5CacheHits == 2);
	}
	std::error_code ec;
	fsys::remove_all(root, ec);
}
