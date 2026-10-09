// OpenBFME unit tests. GPL-3.0.
// Lane RELEASE-1: the console filter (Common/ConsoleFilter.h) sends stdout and stderr through the log privacy filter (review r1: the console,
// where the crash handler also dumps, kept the home folder).

#include "doctest.h"

#include "Common/ConsoleFilter.h"

#ifndef _WIN32

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace
{
std::string readAll(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream o;
	o << in.rdbuf();
	return o.str();
}
} // namespace

TEST_CASE("release1: the console filter redacts stdout and stderr, partial lines and the crash path")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("openbfme-release1-console-" + std::to_string((long long)::getpid()));
	fs::create_directories(dir);
	const std::string outPath = (dir / "out.txt").string(), errPath = (dir / "err.txt").string();
	const int out = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	const int err = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	REQUIRE(out >= 0);
	REQUIRE(err >= 0);
	LogPrivacy::Rules rules;
	rules.home = "/srv/players/tester";
	rules.user = "tester";
	std::string error;
	REQUIRE(ConsoleFilter::install(rules, &error, out, err));
	CHECK(ConsoleFilter::installed());
	std::printf("mount /srv/players/tester/Games/RotWK ok\n");
	std::fflush(stdout);
	std::fprintf(stderr, "ERROR: user=tester failed\n");
	const char raw[] = "a raw write of /srv/players/tester without newline";
	CHECK(::write(1, raw, sizeof(raw) - 1) == (ssize_t)(sizeof(raw) - 1));
	ConsoleFilter::uninstall();
	CHECK_FALSE(ConsoleFilter::installed());
	::close(out);
	::close(err);
	const std::string o = readAll(outPath), e = readAll(errPath);
	CHECK(o.find("mount ~/Games/RotWK ok\n") != std::string::npos);
	CHECK(o.find("a raw write of ~ without newline") != std::string::npos);
	CHECK(e == "ERROR: user=<user> failed\n");
	CHECK(o.find("tester") == std::string::npos);

	// review r3: strictly line-buffered. Nothing reaches the console before its line is complete, however long the writer pauses, so a path
	// or a name split across writes is scrubbed as a whole (the time-based flush of r2 leaked "home=<home>/" + 200 ms + "private\n").
	{
		const int o3 = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		const int e3 = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		REQUIRE(ConsoleFilter::install(rules, &error, o3, e3));
		auto rawWrite = [](int fd, const char *t) { CHECK(::write(fd, t, std::strlen(t)) == (ssize_t)std::strlen(t)); };
		rawWrite(1, "home=/srv/players/tester/");
		::usleep(200000);
		CHECK(readAll(outPath).empty()); // the partial line is held, not written
		rawWrite(1, "private\n");
		rawWrite(1, "path /srv/players/te");
		::usleep(200000);
		rawWrite(1, "ster/x user=tes");
		::usleep(200000);
		rawWrite(1, "ter;\n");
		rawWrite(2, "/srv/");
		::usleep(200000);
		rawWrite(2, "players/");
		::usleep(200000);
		rawWrite(2, "tester failed\n");
		rawWrite(1, "progress 50% bye /srv/players/test");
		::usleep(200000);
		CHECK(readAll(outPath) == "home=~/private\npath ~/x user=<user>;\n");
		rawWrite(1, "er");
		ConsoleFilter::uninstall(); // the last partial line at exit, scrubbed as a whole
		::close(o3);
		::close(e3);
		CHECK(readAll(outPath) == "home=~/private\npath ~/x user=<user>;\nprogress 50% bye ~");
		CHECK(readAll(errPath) == "~ failed\n");
	}

	// the crash path: the held partial line (scrubbed), then the crash text; nothing written to the pipe after it
	{
		const int o4 = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		const int e4 = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		REQUIRE(ConsoleFilter::install(rules, &error, o4, e4));
		CHECK(::write(2, "user=tester", 11) == 11);
		::usleep(100000);
		ConsoleFilter::crashWrite("handle_crash: Program crashed with signal 11 in /srv/players/tester/OpenBFME.x86_64");
		std::fprintf(stderr, "after the crash line\n");
		ConsoleFilter::uninstall();
		::close(o4);
		::close(e4);
		CHECK(readAll(errPath) == "user=<user>\nhandle_crash: Program crashed with signal 11 in ~/OpenBFME.x86_64\n");
	}

	// review r4: a line longer than the cap is written in parts, cut where no name can be split; here the cap is 32 bytes and the line
	// arrives in 7-byte pieces, so cuts fall everywhere around the home folder and the user name
	{
		const int o5 = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		const int e5 = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		REQUIRE(ConsoleFilter::install(rules, &error, o5, e5, 32));
		const std::string line = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa home=/srv/players/tester/private bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb user=tester;"
								 " cccccccccccccccccccccccccc /srv/players/tester dddddddddddddddddddddddddddddd";
		for (size_t i = 0; i < line.size(); i += 7)
		{
			const std::string piece = line.substr(i, 7);
			CHECK(::write(1, piece.data(), piece.size()) == (ssize_t)piece.size());
			::usleep(20000);
		}
		CHECK(::write(1, "\n", 1) == 1);
		ConsoleFilter::uninstall();
		::close(o5);
		::close(e5);
		const std::string o = readAll(outPath);
		CHECK(o == LogPrivacy::redact(line, rules) + "\n");
		CHECK(o.find("/srv/players") == std::string::npos);
		CHECK(o.find("tester") == std::string::npos);
	}
	fs::remove_all(dir);
}

#endif
