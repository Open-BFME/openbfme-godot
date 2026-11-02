// OpenBFME unit tests: the client job pool's size while the logic worker is behind its clock (lane PERF-CLIENT, GameClient/ClientPoolPolicy.h).
// Exact values: the policy is a pure counter of the worker's frames (every one, fed on the worker); the measured sizes behind it are in the header.

#include "doctest.h"
#include "GameClient/ClientPoolPolicy.h"

namespace
{
// `frames` completed worker frames of `ms` each; the last answer
bool feed(ClientPoolPolicy &p, double ms, int frames)
{
	bool last = false;
	for (int i = 0; i < frames; ++i)
	{
		last = p.frame(ms);
	}
	return last;
}
} // namespace

TEST_CASE("client pool policy: the sizes and the budget are the logic clock's and the hardware's")
{
	CHECK(ClientPoolPolicy::budgetMs() == 200);
	CHECK(ClientPoolPolicy::recoveredMs() == 150);
	CHECK(ClientPoolPolicy::behindThreadsFor(16) == 4);
	CHECK(ClientPoolPolicy::behindThreadsFor(12) == 3);
	CHECK(ClientPoolPolicy::behindThreadsFor(8) == 2);
	CHECK(ClientPoolPolicy::behindThreadsFor(4) == 2);
	CHECK(ClientPoolPolicy::behindThreadsFor(2) == 2);
	CHECK(ClientPoolPolicy::behindThreadsFor(1) == 1);
	JobSystem pool(2, JobSystem::FloatingPoint::Client, "client pool sizes test");
	ClientPoolSizer sizer(pool, 2, 4);
	CHECK(sizer.behindThreads() == 2); // never more than the pool has
}

TEST_CASE("client pool policy: the logic is behind after kEnterFrames late worker frames in a row, not before")
{
	ClientPoolPolicy p;
	CHECK_FALSE(p.behind()); // no worker frame yet
	CHECK_FALSE(feed(p, 250.0, ClientPoolPolicy::kEnterFrames - 1));
	CHECK_FALSE(p.behind());
	CHECK(feed(p, 250.0, 1)); // the third
	CHECK(p.behind());
}

TEST_CASE("client pool policy: a worker frame on time resets the late count, so a late frame now and then does not shrink the pool")
{
	ClientPoolPolicy p;
	for (int round = 0; round < 5; ++round)
	{
		CHECK_FALSE(feed(p, 300.0, ClientPoolPolicy::kEnterFrames - 1));
		CHECK_FALSE(feed(p, 100.0, 1));
	}
	CHECK_FALSE(p.behind());
	CHECK_FALSE(feed(p, 200.0, ClientPoolPolicy::kEnterFrames)); // exactly the budget is on time
	CHECK(feed(p, 200.001, ClientPoolPolicy::kEnterFrames));
}

TEST_CASE("client pool policy: the logic is back after kLeaveFrames worker frames under recoveredMs, not sooner, and a late frame restarts that count")
{
	ClientPoolPolicy p;
	feed(p, 250.0, ClientPoolPolicy::kEnterFrames);
	REQUIRE(p.behind());
	CHECK(feed(p, 190.0, 100)); // under the budget but not recovered: neither count moves
	CHECK(feed(p, 120.0, ClientPoolPolicy::kLeaveFrames - 1));
	CHECK(feed(p, 300.0, 1)); // late while recovering
	CHECK(feed(p, 120.0, ClientPoolPolicy::kLeaveFrames - 1)); // the count started again
	CHECK_FALSE(feed(p, 120.0, 1)); // the kLeaveFrames-th recovered frame
	CHECK_FALSE(p.behind());
	feed(p, 250.0, ClientPoolPolicy::kEnterFrames);
	REQUIRE(p.behind());
	p.reset();
	CHECK_FALSE(p.behind());
	CHECK_FALSE(p.frame(250.0)); // after a reset one late frame is not enough
}

TEST_CASE("client pool sizer: resizes the pool as the policy's answer says and restores its full size on restore and when it dies")
{
	JobSystem pool(8, JobSystem::FloatingPoint::Client, "client pool sizer test");
	{
		ClientPoolSizer sizer(pool, 8, ClientPoolPolicy::behindThreadsFor(8));
		sizer.apply(false);
		CHECK(pool.threadCount() == 8);
		sizer.apply(true);
		CHECK(pool.threadCount() == 2);
		CHECK(sizer.shrunk());
		CHECK(sizer.behind());
		sizer.apply(true); // the same answer again: nothing to do
		CHECK(pool.threadCount() == 2);
		sizer.apply(false);
		CHECK(pool.threadCount() == 8);
		CHECK_FALSE(sizer.shrunk());
		CHECK_FALSE(sizer.behind());
		sizer.apply(true);
		REQUIRE(pool.threadCount() == 2);
		sizer.restore();
		CHECK(pool.threadCount() == 8);
		CHECK_FALSE(sizer.behind());
		sizer.apply(true);
		REQUIRE(pool.threadCount() == 2);
		{
			// a second sizer on the same pool (another live world) that never wants another size neither resizes the pool nor restores it when it dies
			ClientPoolSizer other(pool, 8, ClientPoolPolicy::behindThreadsFor(8));
			other.apply(false);
			other.restore();
			CHECK(pool.threadCount() == 2);
		}
		CHECK(pool.threadCount() == 2);
	}
	CHECK(pool.threadCount() == 8); // the sizer died
}
