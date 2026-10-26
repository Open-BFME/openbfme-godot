// OpenBFME. GPL-3.0. Lane DEVPORT-7: TestChild (TestChild.h), the helper process a test starts (the online relay), never outlives the test or holds its
// output: a relay that survived a failed test kept the suite runner's pipe open and hung a gate's job for hours. One case per rule.
#include "doctest.h"
#include "TestChild.h"

#ifndef _WIN32
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
// gone: no such process, or (Linux) a zombie its new parent has yet to reap
bool gone(pid_t pid)
{
	if (kill(pid, 0) == -1 && errno == ESRCH)
	{
		return true;
	}
#ifdef __linux__
	std::ifstream st("/proc/" + std::to_string(pid) + "/stat");
	std::string pidField, comm, state;
	if (!(st >> pidField >> comm >> state))
	{
		return true;
	}
	return state == "Z";
#else
	return false;
#endif
}

bool goneWithin(pid_t pid, int ms)
{
	for (int t = 0; t < ms; t += 20)
	{
		if (gone(pid))
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return gone(pid);
}

std::string logPath(const char *name)
{
	return (std::filesystem::temp_directory_path() / (std::string("openbfme-testchild-") + std::to_string(getpid()) + "-" + name)).string();
}
} // namespace

TEST_CASE("test child: an exception (a failed REQUIRE) leaving the scope stops the child")
{
	pid_t pid = -1;
	try
	{
		TestChild child("/bin/sleep", { "60" }, logPath("throw.log"));
		REQUIRE(child.started());
		pid = child.pid();
		CHECK_FALSE(gone(pid));
		throw std::runtime_error("the test failed here");
	}
	catch (const std::runtime_error &)
	{
	}
	REQUIRE(pid > 0); // tolerance: the child's pid, any positive value
	CHECK(gone(pid));
}

TEST_CASE("test child: the child's output goes to its log file, not to the test's stdout / stderr")
{
	const std::string log = logPath("out.log");
	std::filesystem::remove(log);
	{
		TestChild child("/bin/sh", { "-c", "echo to-out; echo to-err 1>&2; exec sleep 60" }, log);
		REQUIRE(child.started());
#ifdef __linux__
		for (int fd : { 1, 2 })
		{
			std::error_code ec;
			std::filesystem::path target;
			for (int t = 0; t < 100 && target.empty(); ++t) // the exec of sleep keeps the descriptors
			{
				target = std::filesystem::read_symlink("/proc/" + std::to_string(child.pid()) + "/fd/" + std::to_string(fd), ec);
				if (ec || target.string().find("openbfme-testchild") == std::string::npos)
				{
					target.clear();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
			}
			CHECK(target.string() == log);
		}
#endif
		// lane MACGATE-1: wait for the child's two lines (bounded) before the destructor kills it; a fixed 200 ms lost the race on a loaded
		// macOS host, where no /proc wait above has given the shell time to run
		for (int t = 0; t < 500; ++t) // tolerance: 10 s at most
		{
			std::error_code ec;
			const auto size = std::filesystem::file_size(log, ec);
			if (!ec && size >= 14) // "to-out\nto-err\n"
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}
	std::ifstream in(log);
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(text == "to-out\nto-err\n");
}

#ifdef __linux__
TEST_CASE("test child: the child dies with the process that started it, also when that process ends without its destructors (a crash)")
{
	int fds[2];
	REQUIRE(pipe(fds) == 0);
	const pid_t starter = fork();
	REQUIRE(starter >= 0); // tolerance: fork gives any process id, -1 is its failure
	if (starter == 0)
	{
		close(fds[0]);
		TestChild *child = new TestChild("/bin/sleep", { "60" }, logPath("orphan.log"));
		const pid_t pid = child->pid();
		(void)!write(fds[1], &pid, sizeof pid);
		_exit(0); // no destructor runs, as in a crash
	}
	close(fds[1]);
	pid_t pid = -1;
	REQUIRE(read(fds[0], &pid, sizeof pid) == (ssize_t)sizeof pid);
	close(fds[0]);
	int st = 0;
	waitpid(starter, &st, 0);
	REQUIRE(pid > 0); // tolerance: the child's pid, any positive value
	CHECK(goneWithin(pid, 3000));
	if (!gone(pid))
	{
		kill(pid, SIGKILL); // the check failed: do not leave it behind either
	}
}
#endif
#endif
