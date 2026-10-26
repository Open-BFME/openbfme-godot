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
#include <chrono>
#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <vector>

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
	CHECK(o.find("mount <user>/Games/RotWK ok\n") != std::string::npos);
	CHECK(o.find("a raw write of <user> without newline") != std::string::npos);
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
		CHECK(readAll(outPath) == "home=<user>/private\npath <user>/x user=<user>;\n");
		rawWrite(1, "er");
		ConsoleFilter::uninstall(); // the last partial line at exit, scrubbed as a whole
		::close(o3);
		::close(e3);
		CHECK(readAll(outPath) == "home=<user>/private\npath <user>/x user=<user>;\nprogress 50% bye <user>");
		CHECK(readAll(errPath) == "<user> failed\n");
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
		CHECK(readAll(errPath) == "user=<user>\nhandle_crash: Program crashed with signal 11 in <user>/OpenBFME.x86_64\n");
	}

	// A line longer than the cap is omitted as a whole; even a credential split across reads must never be flushed in chunks. The line
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
		CHECK(o == std::string(LogPrivacy::kOversized) + "\n");
		CHECK(o.find("/srv/players") == std::string::npos);
		CHECK(o.find("tester") == std::string::npos);
	}
	fs::remove_all(dir);
}


// lane ONEZIP-1 (Sol r1): a child process that inherited the filtered stdout (the game starts the launcher) must not keep the game from
// ending: uninstall waits for the pipe's EOF a bounded time, then takes what the pipe holds and ends. And the saved original streams are
// close-on-exec: a child started with its own stdio keeps nothing of the parent's console open.
TEST_CASE("onezip1: the console filter ends while a child holds the stream, and children do not inherit the saved console")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("openbfme-onezip1-console-" + std::to_string((long long)::getpid()));
	fs::create_directories(dir);
	const std::string outPath = (dir / "out.txt").string(), errPath = (dir / "err.txt").string();
	const int out = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	const int err = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	REQUIRE(out >= 0); // tolerance: open() returns any descriptor number, -1 on failure
	REQUIRE(err >= 0); // tolerance: open() returns any descriptor number, -1 on failure
	char before[512] = {};
	const ssize_t beforeLen = ::readlink("/proc/self/fd/1", before, sizeof(before) - 1);
	// lane MACGATE-1: the descriptors open before the install and the original stdout / stderr, for the close-on-exec check below (macOS
	// has no /proc, so there the check is on the descriptors the install opened rather than on what an exec'd child holds)
	struct stat consoleOut = {}, consoleErr = {};
	REQUIRE(::fstat(1, &consoleOut) == 0);
	REQUIRE(::fstat(2, &consoleErr) == 0);
	const int fdLimit = 1024;
	std::vector<bool> openBefore(fdLimit, false);
	for (int fd = 0; fd < fdLimit; ++fd)
		openBefore[fd] = ::fcntl(fd, F_GETFD) != -1;
	LogPrivacy::Rules rules;
	std::string error;
	REQUIRE(ConsoleFilter::install(rules, &error, out, err));
	// every descriptor the install opened that is the original console (the saved streams) is close-on-exec: no child inherits it
	{
		int saved = 0;
		std::vector<std::string> inheritable;
		for (int fd = 3; fd < fdLimit; ++fd)
		{
			struct stat st = {};
			if (openBefore[fd] || ::fstat(fd, &st) != 0)
				continue;
			const bool console = (st.st_dev == consoleOut.st_dev && st.st_ino == consoleOut.st_ino) ||
								 (st.st_dev == consoleErr.st_dev && st.st_ino == consoleErr.st_ino);
			if (!console)
				continue;
			++saved;
			if (!(::fcntl(fd, F_GETFD) & FD_CLOEXEC))
				inheritable.push_back(std::to_string(fd));
		}
		CHECK_MESSAGE(saved >= 1, "the install saved no copy of the original console"); // tolerance: 1 when stdout and stderr are one file, else 2
		CHECK_MESSAGE(inheritable.empty(), "a saved console descriptor is inheritable: fd " << (inheritable.empty() ? "" : inheritable[0]));
	}
	std::printf("before the child\n");
	std::fflush(stdout);

	// a child that keeps the inherited stdout (the pipe's write end) for 5 s, as a launcher started without a redirect did
	const pid_t holder = ::fork();
	REQUIRE(holder >= 0); // tolerance: fork() returns any process id, -1 on failure
	if (holder == 0)
	{
		::sleep(5);
		::_exit(0);
	}
	// a child started the way update_handover.gd starts the launcher (its stdio on /dev/null), then exec'd: it must not see the saved console
	const pid_t clean = ::fork();
	REQUIRE(clean >= 0); // tolerance: fork() returns any process id, -1 on failure
	if (clean == 0)
	{
		const int null = ::open("/dev/null", O_RDWR);
		::dup2(null, 0);
		::dup2(null, 1);
		::dup2(null, 2);
		::execl("/bin/sleep", "sleep", "3", (char *)nullptr);
		::_exit(127);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the exec has happened
#ifdef __linux__
	// Linux: what the exec'd child really holds (/proc/<pid>/fd)
	std::vector<std::string> leaked;
	for (const auto &e : fs::directory_iterator("/proc/" + std::to_string((long long)clean) + "/fd"))
	{
		const std::string n = e.path().filename().string();
		if (n == "0" || n == "1" || n == "2")
			continue;
		std::error_code ec;
		const std::string target = fs::read_symlink(e.path(), ec).string();
		if (beforeLen > 0 && target == std::string(before, (size_t)beforeLen))
			leaked.push_back(n + " -> " + target);
	}
	CHECK_MESSAGE(leaked.empty(), "the child inherited the saved console: " << (leaked.empty() ? "" : leaked[0]));
#else
	(void)beforeLen;
#endif

	const auto t0 = std::chrono::steady_clock::now();
	ConsoleFilter::uninstall();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	// ONE shutdown deadline for both streams (500 ms for their EOF, everything ended by 1000 ms; Sol r2: both streams kept took 1025 ms)
	// tolerance: wall time on a loaded machine against that one deadline, far below the child's 5 s
	CHECK_MESSAGE(ms < 1000, "uninstall waited " << ms << " ms for a child holding the stream");
	CHECK(readAll(outPath) == "before the child\n");
	::kill(holder, SIGKILL);
	::kill(clean, SIGKILL);
	::waitpid(holder, nullptr, 0);
	::waitpid(clean, nullptr, 0);
	::close(out);
	::close(err);
	fs::remove_all(dir);
}

// lane ONEZIP-1 r3 (Sol r2): a console that takes nothing (a pipe nobody reads, full) must not keep uninstall from returning: the
// reader's writes wait for the sink only until the shutdown's deadline, and what is left then is dropped
TEST_CASE("onezip1: the console filter ends when its console takes nothing")
{
	int sink[2];
	REQUIRE(::pipe(sink) == 0);
	// fill the pipe: nobody reads it
	::fcntl(sink[1], F_SETFL, ::fcntl(sink[1], F_GETFL) | O_NONBLOCK);
	const std::string block(4096, 'x');
	while (::write(sink[1], block.data(), block.size()) > 0)
	{
	}
	::fcntl(sink[1], F_SETFL, ::fcntl(sink[1], F_GETFL) & ~O_NONBLOCK);
	::fcntl(sink[0], F_SETFD, FD_CLOEXEC);
	::fcntl(sink[1], F_SETFD, FD_CLOEXEC);
	LogPrivacy::Rules rules;
	std::string error;
	REQUIRE(ConsoleFilter::install(rules, &error, sink[1], sink[1]));
	for (int i = 0; i < 64; ++i)
		std::printf("a line the blocked console never takes %d\n", i);
	std::fflush(stdout);
	std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the reader is now waiting for the sink
	const auto t0 = std::chrono::steady_clock::now();
	ConsoleFilter::uninstall();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	// tolerance: the shutdown's deadline is 1000 ms; wall time on a loaded machine
	CHECK_MESSAGE(ms < 1500, "uninstall waited " << ms << " ms for a console that takes nothing");
	CHECK_FALSE(ConsoleFilter::installed());
	::close(sink[0]);
	::close(sink[1]);
}

TEST_CASE("release1: a blocked console cannot hang the crash writer")
{
	int sink[2];
	REQUIRE(::pipe(sink) == 0);
	::fcntl(sink[1], F_SETFL, ::fcntl(sink[1], F_GETFL) | O_NONBLOCK);
	const std::string block(4096, 'x');
	while (::write(sink[1], block.data(), block.size()) > 0) {}
	::fcntl(sink[1], F_SETFL, ::fcntl(sink[1], F_GETFL) & ~O_NONBLOCK);
	std::string error;
	REQUIRE(ConsoleFilter::install({}, &error, sink[1], sink[1]));
	const auto start = std::chrono::steady_clock::now();
	ConsoleFilter::crashWrite("Authorization: Bearer synthetic");
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	ConsoleFilter::uninstall();
	// tolerance: a 500 ms crash-write deadline plus scheduler jitter, not an indefinitely blocked write.
	CHECK(ms < 1500);
	::close(sink[0]);
	::close(sink[1]);
}

// lane ONEZIP-1 r3 (Sol r3): the flush of the buffered stdio at shutdown is inside the same deadline. A console that takes nothing,
// a filter input pipe that is full and a megabyte of buffered stdout used to hang uninstall's fflush (killed at 3 s).
TEST_CASE("onezip1: the shutdown flush is bounded when the console takes nothing and stdio is buffered")
{
	int sink[2];
	REQUIRE(::pipe(sink) == 0);
	::fcntl(sink[1], F_SETFL, ::fcntl(sink[1], F_GETFL) | O_NONBLOCK);
	const std::string block(4096, 'x');
	while (::write(sink[1], block.data(), block.size()) > 0)
	{
	}
	::fcntl(sink[1], F_SETFL, ::fcntl(sink[1], F_GETFL) & ~O_NONBLOCK);
	::fcntl(sink[0], F_SETFD, FD_CLOEXEC);
	::fcntl(sink[1], F_SETFD, FD_CLOEXEC);
	static char buffer[1 << 20];
	std::fflush(stdout);
	REQUIRE(std::setvbuf(stdout, buffer, _IOFBF, sizeof(buffer)) == 0);
	LogPrivacy::Rules rules;
	std::string error;
	REQUIRE(ConsoleFilter::install(rules, &error, sink[1], sink[1]));
	const std::string line(99, 'y');
	for (int i = 0; i < 2000; ++i) // 200 KB: more than the pipe to the filter holds, less than the buffer: nothing reaches it before the flush
		std::printf("%s\n", line.c_str());
	const auto t0 = std::chrono::steady_clock::now();
	ConsoleFilter::uninstall();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	// tolerance: the shutdown's deadline is 1000 ms (the flush gets 400 of it); wall time on a loaded machine, far below the 3 s the hang was killed at
	CHECK_MESSAGE(ms < 1500, "uninstall took " << ms << " ms with buffered stdio and a console that takes nothing");
	CHECK_FALSE(ConsoleFilter::installed());
	std::fflush(stdout);
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	::close(sink[0]);
	::close(sink[1]);
}

#endif
