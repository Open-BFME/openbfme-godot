// OpenBFME unit tests. GPL-3.0.
// Lane PERF-2: the job system (Common/JobSystem.h): the fixed chunking, results equal to the serial run whatever the thread count, the canonical
// floating-point environment on every thread of a simulation pool (set, verified, a violation thrown on the caller), errors, nested and concurrent
// dispatches. tools/smooth/sanitize_threaded.sh-style TSan runs cover this file (see tools/perf2/sanitize_jobs.sh).

#include "doctest.h"

#include "Common/JobSystem.h"
#include "Common/NumericState.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::vector<int> threadCounts()
{
	std::vector<int> t = { 1, 2, 3, 4, 8 };
	const int n = JobSystem::defaultThreadCount();
	if (n > 8)
	{
		t.push_back(n);
	}
	return t;
}

// a deliberately order-sensitive combination: each chunk folds its items (rotate-add, the StateHasher step), the chunks fold in chunk order
std::uint32_t foldSerial(std::size_t count, std::size_t grain)
{
	std::uint32_t total = 0;
	for (std::size_t c = 0; c < JobSystem::chunkCount(count, grain); ++c)
	{
		std::uint32_t h = 0;
		for (std::size_t i = c * grain; i < count && i < (c + 1) * grain; ++i)
		{
			h = ((h << 1) | (h >> 31)) + (std::uint32_t)(i * 2654435761u);
		}
		total = ((total << 1) | (total >> 31)) + h;
	}
	return total;
}
} // namespace

TEST_CASE("perf2 jobs: chunk k covers [k * grain, min(count, (k + 1) * grain)) for every thread count; every item runs once")
{
	for (int threads : threadCounts())
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Client, "test");
		CHECK(pool.threadCount() == threads);
		for (std::size_t count : { (std::size_t)1, (std::size_t)7, (std::size_t)64, (std::size_t)1000, (std::size_t)4099 })
		{
			for (std::size_t grain : { (std::size_t)1, (std::size_t)3, (std::size_t)64, (std::size_t)5000 })
			{
				const std::size_t chunks = JobSystem::chunkCount(count, grain);
				std::vector<std::atomic<int>> hits(count);
				std::vector<std::size_t> begins(chunks, ~(std::size_t)0), ends(chunks, ~(std::size_t)0);
				pool.parallelFor(count, grain, [&](std::size_t chunk, std::size_t begin, std::size_t end) {
					begins[chunk] = begin;
					ends[chunk] = end;
					for (std::size_t i = begin; i < end; ++i)
					{
						hits[i].fetch_add(1);
					}
				});
				INFO("threads " << threads << " count " << count << " grain " << grain);
				for (std::size_t c = 0; c < chunks; ++c)
				{
					CHECK(begins[c] == c * grain);
					CHECK(ends[c] == std::min(count, (c + 1) * grain));
				}
				int bad = 0;
				for (std::size_t i = 0; i < count; ++i)
				{
					bad += hits[i].load() != 1;
				}
				CHECK(bad == 0);
			}
		}
	}
}

TEST_CASE("perf2 jobs: per-chunk results combined in chunk order equal the serial result with 1, 2, 3, 4, 8 and N threads, run after run")
{
	const std::size_t count = 100000, grain = 777;
	const std::uint32_t want = foldSerial(count, grain);
	for (int threads : threadCounts())
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Simulation, "test");
		std::vector<std::uint32_t> part(JobSystem::chunkCount(count, grain));
		for (int run = 0; run < 20; ++run)
		{
			std::fill(part.begin(), part.end(), 0u);
			pool.parallelFor(count, grain, [&](std::size_t chunk, std::size_t begin, std::size_t end) {
				std::uint32_t h = 0;
				for (std::size_t i = begin; i < end; ++i)
				{
					h = ((h << 1) | (h >> 31)) + (std::uint32_t)(i * 2654435761u);
				}
				part[chunk] = h;
			});
			std::uint32_t total = 0;
			for (std::uint32_t h : part)
			{
				total = ((total << 1) | (total >> 31)) + h;
			}
			INFO("threads " << threads << " run " << run);
			CHECK(total == want);
		}
		const JobSystem::Stats s = pool.stats();
		CHECK(s.dispatches == 20u);
		CHECK(s.chunks == 20u * part.size());
		if (threads == 1)
		{
			CHECK(s.inlineDispatches == 20u);
			CHECK(s.workerChunks == 0u);
		}
		else
		{
			CHECK(s.inlineDispatches == 0u);
		}
	}
}

TEST_CASE("perf2 jobs: the worker threads really run chunks (a pool of N threads uses its workers)")
{
	JobSystem pool(4, JobSystem::FloatingPoint::Client, "test");
	std::atomic<int> sum{ 0 };
	// long chunks: the caller cannot finish them all before the workers wake
	for (int run = 0; run < 50 && pool.stats().workerChunks == 0; ++run)
	{
		pool.parallelFor(64, 1, [&](std::size_t, std::size_t, std::size_t) {
			std::this_thread::sleep_for(std::chrono::microseconds(200));
			sum.fetch_add(1);
		});
	}
	CHECK(pool.stats().workerChunks > 0u);
	CHECK(sum.load() % 64 == 0);
}

TEST_CASE("perf2 jobs: a simulation pool runs every chunk, on every thread, in the canonical floating-point environment of the numeric facade")
{
	NumericState::normalizeFloatingPointEnvironment();
	for (int threads : threadCounts())
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Simulation, "test");
		std::vector<std::uint32_t> csr(256, 0);
		std::vector<int> canonical(256, 0);
		pool.parallelFor(csr.size(), 1, [&](std::size_t chunk, std::size_t, std::size_t) {
			csr[chunk] = NumericState::readControlRegister();
			canonical[chunk] = NumericState::floatingPointEnvironmentIsCanonical() ? 1 : 0;
		});
		INFO("threads " << threads);
		for (size_t i = 0; i < csr.size(); ++i)
		{
			CHECK(canonical[i] == 1);
#if defined(__SSE__) || defined(_M_X64)
			CHECK((csr[i] & 0xFFC0u) == NumericState::kCanonicalControlRegister);
#endif
		}
	}
}

#if defined(__SSE__) || defined(_M_X64)
TEST_CASE("perf2 jobs: a chunk that leaves flush-to-zero on is an error thrown on the caller; the next dispatch starts canonical again")
{
	NumericState::normalizeFloatingPointEnvironment();
	for (int threads : { 1, 4 })
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Simulation, "test");
		bool threw = false;
		try
		{
			pool.parallelFor(16, 1, [&](std::size_t chunk, std::size_t, std::size_t) {
				if (chunk == 5)
				{
					NumericState::writeControlRegister(NumericState::kCanonicalControlRegister | 0x8040u); // FTZ | DAZ
				}
			});
		}
		catch (const std::logic_error &e)
		{
			threw = true;
			INFO(e.what());
			CHECK(std::string(e.what()).find("chunk 5 left the canonical floating-point environment") != std::string::npos);
		}
		CHECK(threw);
		NumericState::normalizeFloatingPointEnvironment(); // threads == 1: chunk 5 ran on this thread
		std::atomic<int> bad{ 0 };
		pool.parallelFor(64, 1, [&](std::size_t, std::size_t, std::size_t) {
			bad.fetch_add(NumericState::floatingPointEnvironmentIsCanonical() ? 0 : 1);
		});
		CHECK(bad.load() == 0);
	}
	// the caller's own environment is verified before a simulation dispatch
	JobSystem pool(4, JobSystem::FloatingPoint::Simulation, "test");
	NumericState::writeControlRegister(NumericState::kCanonicalControlRegister | 0x6000u); // round toward zero
	CHECK_THROWS_AS(pool.parallelFor(16, 1, [](std::size_t, std::size_t, std::size_t) {}), std::logic_error);
	NumericState::normalizeFloatingPointEnvironment();
	// a client pool has no such requirement
	JobSystem client(4, JobSystem::FloatingPoint::Client, "test");
	NumericState::writeControlRegister(NumericState::kCanonicalControlRegister | 0x6000u);
	CHECK_NOTHROW(client.parallelFor(16, 1, [](std::size_t, std::size_t, std::size_t) {}));
	NumericState::normalizeFloatingPointEnvironment();
}
#endif

TEST_CASE("perf2 jobs: the exception of the lowest failing chunk reaches the caller after every chunk ran")
{
	for (int threads : threadCounts())
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Client, "test");
		std::atomic<int> ran{ 0 };
		std::string what;
		try
		{
			pool.parallelFor(100, 1, [&](std::size_t chunk, std::size_t, std::size_t) {
				ran.fetch_add(1);
				if (chunk == 90 || chunk == 17 || chunk == 60)
				{
					throw std::runtime_error("chunk " + std::to_string(chunk));
				}
			});
		}
		catch (const std::runtime_error &e)
		{
			what = e.what();
		}
		INFO("threads " << threads);
		CHECK(what == "chunk 17");
		if (threads > 1)
		{
			CHECK(ran.load() == 100); // the pool runs the rest; the inline run stops at the first failure
		}
	}
}

TEST_CASE("perf2 jobs: a dispatch from inside a chunk, or while another thread holds the pool, runs inline with the same result")
{
	JobSystem pool(4, JobSystem::FloatingPoint::Client, "test");
	std::vector<std::uint64_t> inner(8, 0);
	pool.parallelFor(8, 1, [&](std::size_t chunk, std::size_t, std::size_t) {
		std::uint64_t s = 0;
		pool.parallelFor(1000, 10, [&](std::size_t, std::size_t b, std::size_t e) {
			for (std::size_t i = b; i < e; ++i)
			{
				s += i; // inline: one thread, chunk order
			}
		});
		inner[chunk] = s;
	});
	for (std::uint64_t s : inner)
	{
		CHECK(s == 499500u);
	}
	// two dispatching threads share one pool: one gets the workers, the other runs inline; both results are right
	std::vector<std::uint64_t> results(2, 0);
	std::vector<std::thread> callers;
	for (int t = 0; t < 2; ++t)
	{
		callers.emplace_back([&, t] {
			for (int run = 0; run < 200; ++run)
			{
				std::vector<std::uint64_t> part(JobSystem::chunkCount(5000, 100), 0);
				pool.parallelFor(5000, 100, [&](std::size_t chunk, std::size_t b, std::size_t e) {
					std::uint64_t s = 0;
					for (std::size_t i = b; i < e; ++i)
					{
						s += i;
					}
					part[chunk] = s;
				});
				std::uint64_t sum = 0;
				for (std::uint64_t s : part)
				{
					sum += s;
				}
				results[(size_t)t] += sum == 12497500u ? 1 : 0;
			}
		});
	}
	for (std::thread &c : callers)
	{
		c.join();
	}
	CHECK(results[0] == 200u);
	CHECK(results[1] == 200u);
}

TEST_CASE("perf2 jobs: the thread count can change between dispatches; the process pools are sized by the environment or the hardware")
{
	JobSystem pool(1, JobSystem::FloatingPoint::Client, "test");
	for (int threads : { 4, 1, 2, 8, 3 })
	{
		pool.setThreadCount(threads);
		CHECK(pool.threadCount() == threads);
		std::atomic<int> n{ 0 };
		pool.parallelFor(1000, 7, [&](std::size_t, std::size_t b, std::size_t e) { n.fetch_add((int)(e - b)); });
		CHECK(n.load() == 1000);
	}
	CHECK_THROWS_AS(pool.setThreadCount(0), std::invalid_argument);
	CHECK(JobSystem::defaultThreadCount() == (int)std::max(1u, std::thread::hardware_concurrency())); // every core (Sol r1)
	CHECK(JobSystem::logic().threadCount() >= 1);
	CHECK(JobSystem::client().threadCount() >= 1);
}

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
// Sol r1: the x87 control word is part of the canonical environment (an x87-only rounding change used to pass), and the post-chunk check also runs
// when a chunk throws
struct ScopedX87
{
	std::uint16_t saved = NumericState::readX87ControlWord();
	~ScopedX87()
	{
		NumericState::writeX87ControlWord(saved);
	}
};

TEST_CASE("perf2 jobs: an x87-only rounding change is not canonical (environment check and the PC24 fast path); normalisation restores 0x037F")
{
	NumericState::normalizeFloatingPointEnvironment();
	ScopedX87 guard;
	CHECK(NumericState::readX87ControlWord() == NumericState::kCanonicalX87ControlWord);
	CHECK(NumericState::floatingPointEnvironmentIsCanonical());
	const float a = 1.0f, b = 3.0f;
	const float want = NumericState::reference::pc24Div(a, b);
	for (std::uint16_t bad : { (std::uint16_t)0x0F7F /* toward zero */, (std::uint16_t)0x077F /* down */, (std::uint16_t)0x027F /* PC53 */,
			 (std::uint16_t)0x037E /* invalid unmasked */ })
	{
		INFO("x87 control word " << bad);
		NumericState::writeX87ControlWord(bad);
		CHECK((NumericState::readControlRegister() & 0xFFC0u) == NumericState::kCanonicalControlRegister); // MXCSR untouched
		CHECK_FALSE(NumericState::x87ControlWordIsCanonical());
		CHECK_FALSE(NumericState::floatingPointEnvironmentIsCanonical());
		CHECK(NumericState::pc24Div(a, b) == want); // off the fast path: the emulation, same value
		NumericState::normalizeFloatingPointEnvironment();
		CHECK(NumericState::readX87ControlWord() == NumericState::kCanonicalX87ControlWord);
		CHECK(NumericState::floatingPointEnvironmentIsCanonical());
	}
}

TEST_CASE("perf2 jobs: a chunk that changes only the x87 rounding, or throws after changing it, is an environment error at 1 and N threads")
{
	NumericState::normalizeFloatingPointEnvironment();
	ScopedX87 guard;
	for (int threads : { 1, 4 })
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Simulation, "test");
		for (bool alsoThrow : { false, true })
		{
			INFO(threads << " threads, throwing " << alsoThrow);
			std::string what;
			try
			{
				pool.parallelFor(16, 1, [&](std::size_t chunk, std::size_t, std::size_t) {
					if (chunk == 3)
					{
						NumericState::writeX87ControlWord(0x0F7F); // round toward zero, x87 only
						if (alsoThrow)
						{
							throw std::runtime_error("chunk three failed");
						}
					}
				});
			}
			catch (const std::logic_error &e)
			{
				what = e.what();
			}
			CHECK(what.find("chunk 3 left the canonical floating-point environment") != std::string::npos);
			CHECK(what.find("x87 control word 0x0F7F") != std::string::npos);
			CHECK((what.find("after throwing: chunk three failed") != std::string::npos) == alsoThrow);
			CHECK(NumericState::floatingPointEnvironmentIsCanonical()); // the caller is back in the canonical environment (1 thread: chunk 3 ran here)
			std::atomic<int> bad{ 0 };
			pool.parallelFor(64, 1, [&](std::size_t, std::size_t, std::size_t) { bad.fetch_add(NumericState::floatingPointEnvironmentIsCanonical() ? 0 : 1); });
			CHECK(bad.load() == 0);
		}
	}
}
#endif
