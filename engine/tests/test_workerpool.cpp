// OpenBFME unit tests: the instancer's fork/join pool (WWLib/workerpool.h). Review finding P1: workers started with seen=0 and picked
// up a previous generation's job, counter and closure. The expectations here are arithmetic on the inputs (each index visited exactly
// once, sum of 0..n-1), never read back from the pool.

#include "doctest.h"

#include "Libraries/WWVegas/WWLib/workerpool.h"

#include <atomic>
#include <vector>

TEST_CASE("WorkerPool visits every index exactly once, on helpers and the caller")
{
	WorkerPool pool(3);
	REQUIRE(pool.helper_threads() == 3);
	const size_t n = 1000;
	std::vector<std::atomic<int>> hits(n);
	for (auto &h : hits) h = 0;
	pool.run(n, [&](size_t b, size_t e) {
		for (size_t i = b; i < e; ++i) hits[i]++;
	});
	for (size_t i = 0; i < n; ++i) CHECK(hits[i] == 1);
	CHECK(pool.jobs_run() == 1);
}

TEST_CASE("WorkerPool with no helpers runs the whole range on the caller")
{
	WorkerPool pool(0);
	size_t sum = 0;
	pool.run(100, [&](size_t b, size_t e) {
		for (size_t i = b; i < e; ++i) sum += i;
	});
	CHECK(sum == 4950);
}

TEST_CASE("WorkerPool stress: new pools every iteration, 64+ items, closures over per-iteration stack data")
{
	// set_worker_threads() builds a new pool each time. Every iteration owns fresh stack variables; a worker that ran a stale closure from
	// an earlier iteration would write into them (or into dead frames) and break the count.
	for (int iter = 0; iter < 300; ++iter)
	{
		const int helpers = 1 + (iter % 7);
		const size_t n = 64 + (size_t)(iter % 50);
		WorkerPool pool(helpers);
		for (int rep = 0; rep < 3; ++rep)
		{
			std::atomic<size_t> sum{ 0 };
			std::atomic<size_t> calls{ 0 };
			std::vector<std::uint8_t> seen(n, 0);
			pool.run(n, [&](size_t b, size_t e) {
				calls++;
				for (size_t i = b; i < e; ++i)
				{
					seen[i]++; // distinct indices per call: no two threads touch the same byte
					sum += i;
				}
			}, 4);
			CHECK(sum == n * (n - 1) / 2);
			for (size_t i = 0; i < n; ++i) CHECK(seen[i] == 1);
			CHECK(calls == (n + 3) / 4);
		}
		CHECK(pool.jobs_run() == 3);
	}
}

TEST_CASE("WorkerPool stress: back-to-back runs on one pool never mix completion counters")
{
	WorkerPool pool(7);
	for (int iter = 0; iter < 2000; ++iter)
	{
		const size_t n = 64 + (size_t)(iter % 100);
		std::atomic<size_t> count{ 0 };
		pool.run(n, [&](size_t b, size_t e) { count += e - b; }, 1 + (size_t)(iter % 9));
		REQUIRE(count == n); // run() returns only once every helper has finished this job
	}
	CHECK(pool.jobs_run() == 2000);
}

TEST_CASE("WorkerPool destroyed straight after construction and after a run joins cleanly")
{
	for (int i = 0; i < 200; ++i)
	{
		{
			WorkerPool idle(4);
		}
		WorkerPool p(4);
		std::atomic<int> n{ 0 };
		p.run(64, [&](size_t b, size_t e) { n += (int)(e - b); });
		CHECK(n == 64);
	}
}
