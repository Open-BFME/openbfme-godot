// OpenBFME unit tests. GPL-3.0.
// Lane FREEZE-1: the freeze watchdog (Common/FreezeWatchdog.h) with a test clock: hitches, a freeze with its stack captures, the background
// threshold and the end of a freeze.

#include "doctest.h"

#include "Common/FreezeWatchdog.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct Rig
{
	long long now = 1000;
	std::vector<std::string> lines;
	std::vector<unsigned> captures;
	FreezeWatchdog dog;
	Rig()
		: dog(FreezeWatchdog::Config{}, [this](const std::string &l) { lines.push_back(l); },
			  [this](unsigned n) {
				  captures.push_back(n);
				  return std::string("captured ") + std::to_string(n);
			  },
			  [] { return std::string("GAME screen: MainMenu.apt"); }, [this] { return now; })
	{
	}
	void frames(int n, int ms = 16)
	{
		for (int i = 0; i < n; ++i)
		{
			now += ms;
			dog.beat();
			dog.check();
		}
	}
	void stall(int ms)
	{
		for (int t = 0; t < ms; t += 250)
		{
			now += 250;
			dog.check();
		}
	}
	bool has(const std::string &part) const
	{
		for (const std::string &l : lines)
			if (l.find(part) != std::string::npos)
				return true;
		return false;
	}
};
} // namespace

TEST_CASE("freeze1 watchdog: nothing before the first frame or while frames come")
{
	Rig r;
	r.stall(60000); // start-up (loading before the first frame) is not a freeze
	CHECK(r.lines.empty());
	r.frames(600);
	CHECK(r.lines.empty());
	CHECK(r.dog.stats().freezes == 0);
}

TEST_CASE("freeze1 watchdog: a long frame is a HITCH line, repeated ones are counted into the next line")
{
	Rig r;
	r.frames(10);
	r.frames(1, 1500);
	REQUIRE(r.lines.size() == 1);
	CHECK(r.lines[0] == "HITCH frame 11 took 1500 ms");
	r.frames(2, 1200); // within hitchQuietMs: counted, no line
	CHECK(r.lines.size() == 1);
	r.frames(300);
	r.frames(1, 2000);
	REQUIRE(r.lines.size() == 2);
	CHECK(r.lines[1].find("took 2000 ms (+2 more since the last HITCH line)") != std::string::npos);
	CHECK(r.dog.stats().hitches == 4);
}

TEST_CASE("freeze1 watchdog: a stall is reported once with context and stacks, captured again at 30 s, and its end is logged")
{
	Rig r;
	r.frames(100);
	r.stall(4750);
	CHECK(r.lines.empty());
	r.stall(250);
	REQUIRE(r.lines.size() == 2);
	CHECK(r.lines[0] == "FREEZE the main thread has not finished a frame for 5.0 s (after frame 100); last log line before it: GAME screen: MainMenu.apt");
	CHECK(r.lines[1] == "FREEZE thread stacks: captured 1");
	r.stall(24750); // 29.75 s stalled
	CHECK(r.lines.size() == 2);
	r.stall(250); // 30 s: the second capture, one still-stalled line
	REQUIRE(r.lines.size() == 4);
	CHECK(r.lines[2] == "FREEZE still stalled after 30.0 s");
	CHECK(r.lines[3] == "FREEZE thread stacks: captured 2");
	r.stall(30000);
	CHECK(r.has("FREEZE still stalled after 60.0 s"));
	CHECK(r.captures.size() == 2); // no third capture
	r.frames(1);
	CHECK(r.lines.back() == "FREEZE over: the main thread was stalled for 60.0 s (frame 101 finished)");
	CHECK(r.dog.stats().freezes == 1);
	CHECK_FALSE(r.has("HITCH")); // the frame that ends a freeze is not also a hitch
	// a second freeze is reported again
	r.frames(10);
	r.stall(5000);
	CHECK(r.dog.stats().freezes == 2);
	CHECK(r.captures.size() == 3);
}

TEST_CASE("freeze1 watchdog: a window in the background waits 30 s and captures no stacks")
{
	Rig r;
	r.frames(10);
	r.dog.setBackground(true);
	r.frames(1, 3000); // a throttled frame in the background is no hitch
	r.stall(29750);
	CHECK(r.lines.empty());
	r.stall(250);
	REQUIRE(r.lines.size() == 1);
	CHECK(r.lines[0].find(", window in the background") != std::string::npos);
	r.stall(60000);
	CHECK(r.captures.empty());
}

#ifndef _WIN32
TEST_CASE("freeze1 watchdog: the stack capture writes every thread of this process, redacted")
{
	const std::string path = "freeze1-capture-test.txt";
	std::remove(path.c_str());
	const std::string result = ThreadStacks::capture(path, [](const std::string &t) { return "REDACTED\n" + t; }, 30000);
	if (result.find("no stack tool") != std::string::npos)
	{
		MESSAGE("SKIP: " << result);
		return;
	}
	INFO(result);
	CHECK(result.find(" lines in freeze1-capture-test.txt") != std::string::npos);
	std::ifstream in(path);
	std::string first;
	std::getline(in, first);
	CHECK(first == "REDACTED");
	std::remove(path.c_str());
}
#endif
