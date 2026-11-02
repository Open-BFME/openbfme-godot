// OpenBFME unit tests: the client job pool's size while the logic worker is behind its clock (lane PERF-CLIENT, GameClient/ClientPoolPolicy.h).
// Exact values: the policy is a pure counter of worker frames; the measured sizes behind it are in the header.

#include "doctest.h"
#include "GameClient/ClientPoolPolicy.h"

namespace
{
// `frames` completed worker frames of `ms` each, every one seen by `views` render frames; the last answer
int feed(ClientPoolPolicy &p, unsigned long long &completed, double ms, int frames, int views = 1)
{
	int last = 0;
	for (int i = 0; i < frames; ++i)
	{
		++completed;
		for (int v = 0; v < views; ++v)
		{
			last = p.update(completed, ms);
		}
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
	ClientPoolPolicy p(2, 4);
	CHECK(p.behindThreads() == 2); // never more than the pool has
}

TEST_CASE("client pool policy: the pool keeps every thread until kEnterFrames worker frames in a row were late, however many render frames saw each")
{
	ClientPoolPolicy p(16, 4);
	unsigned long long completed = 0;
	CHECK(p.update(0, 0.0) == 16); // no worker frame yet
	CHECK(feed(p, completed, 250.0, ClientPoolPolicy::kEnterFrames - 1, 40) == 16); // two late frames, 40 render frames each
	CHECK_FALSE(p.behind());
	CHECK(feed(p, completed, 250.0, 1, 1) == 4); // the third
	CHECK(p.behind());
}

TEST_CASE("client pool policy: a worker frame on time resets the late count, so a late frame now and then does not shrink the pool")
{
	ClientPoolPolicy p(16, 4);
	unsigned long long completed = 0;
	for (int round = 0; round < 5; ++round)
	{
		CHECK(feed(p, completed, 300.0, ClientPoolPolicy::kEnterFrames - 1) == 16);
		CHECK(feed(p, completed, 100.0, 1) == 16);
	}
	CHECK_FALSE(p.behind());
	CHECK(feed(p, completed, 200.0, ClientPoolPolicy::kEnterFrames) == 16); // exactly the budget is on time
	CHECK(feed(p, completed, 200.001, ClientPoolPolicy::kEnterFrames) == 4);
}

TEST_CASE("client pool policy: the pool grows back after kLeaveFrames worker frames under recoveredMs, not sooner, and a late frame restarts that count")
{
	ClientPoolPolicy p(16, 4);
	unsigned long long completed = 0;
	feed(p, completed, 250.0, ClientPoolPolicy::kEnterFrames);
	REQUIRE(p.behind());
	CHECK(feed(p, completed, 190.0, 100) == 4); // under the budget but not recovered: neither count moves
	CHECK(feed(p, completed, 120.0, ClientPoolPolicy::kLeaveFrames - 1, 30) == 4);
	CHECK(feed(p, completed, 300.0, 1) == 4); // late while recovering
	CHECK(feed(p, completed, 120.0, ClientPoolPolicy::kLeaveFrames - 1) == 4); // the count started again
	CHECK(feed(p, completed, 120.0, 1) == 16); // the kLeaveFrames-th recovered frame
	CHECK_FALSE(p.behind());
	feed(p, completed, 250.0, ClientPoolPolicy::kEnterFrames);
	REQUIRE(p.behind());
	p.reset();
	CHECK_FALSE(p.behind());
	CHECK(p.update(completed, 250.0) == 16); // after a reset the first completion is new again, and one late frame is not enough
}

TEST_CASE("client pool sizer: resizes the pool as the policy says and restores its full size on reset and when it dies")
{
	JobSystem pool(8, JobSystem::FloatingPoint::Client, "client pool sizer test");
	{
		ClientPoolSizer sizer(pool, 8, ClientPoolPolicy::behindThreadsFor(8));
		unsigned long long completed = 0;
		for (int i = 0; i < ClientPoolPolicy::kEnterFrames; ++i)
		{
			sizer.apply(++completed, 250.0);
		}
		CHECK(pool.threadCount() == 2);
		CHECK(sizer.shrunk());
		for (int i = 0; i < ClientPoolPolicy::kLeaveFrames; ++i)
		{
			sizer.apply(++completed, 100.0);
		}
		CHECK(pool.threadCount() == 8);
		CHECK_FALSE(sizer.shrunk());
		for (int i = 0; i < ClientPoolPolicy::kEnterFrames; ++i)
		{
			sizer.apply(++completed, 250.0);
		}
		REQUIRE(pool.threadCount() == 2);
		sizer.restore();
		CHECK(pool.threadCount() == 8);
		CHECK_FALSE(sizer.policy().behind());
		for (int i = 0; i < ClientPoolPolicy::kEnterFrames; ++i)
		{
			sizer.apply(++completed, 250.0);
		}
		REQUIRE(pool.threadCount() == 2);
		{
			// a second sizer on the same pool (another live world) that never wants another size neither resizes the pool nor restores it when it dies
			ClientPoolSizer other(pool, 8, ClientPoolPolicy::behindThreadsFor(8));
			other.apply(1, 100.0);
			other.restore();
			CHECK(pool.threadCount() == 2);
		}
		CHECK(pool.threadCount() == 2);
	}
	CHECK(pool.threadCount() == 8); // the sizer died
}
