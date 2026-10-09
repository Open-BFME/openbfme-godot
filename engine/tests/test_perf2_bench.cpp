// OpenBFME unit tests. GPL-3.0.
// Lane PERF-2: the benchmark of the parallel logic (skipped by default: openbfme_tests -tc="perf2 bench*" --no-skip). PERF-1's big battle (about 1080
// objects) with the logic job pool at 1 thread and at OPENBFME_BENCH_THREADS (default: the hardware threads), each frame's logic step and state hash
// timed on the wall clock (several threads work on them) and the process CPU time per frame (cores busy = CPU / wall). Prints a line per thread count
// and checks that every frame's hash is the same at every thread count. OPENBFME_BENCH_FRAMES (default 400).

#include "doctest.h"

#include "PerfBenchUtil.h"
#include "PerfScenarios.h"
#include "StartTestUtil.h"

#include "Common/JobSystem.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{
double wallMs()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double processCpuMs()
{
#ifdef _WIN32
	return wallMs();
#else
	timespec ts{};
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
}

struct BenchRun
{
	std::vector<double> logicMs, hashMs;
	double cpuMs = 0.0, wallTotalMs = 0.0;
	std::vector<std::uint32_t> hashes;
};

BenchRun runBattle(hudtest::SharedWorld &sh, int frames)
{
	BenchRun out;
	auto scope = sh.world->enterContext();
	hudtest::Rig rig(sh);
	GameLogic &logic = rig.logic();
	perf1::setUpBigBattle(rig);
	const double c0 = processCpuMs(), w0 = wallMs();
	for (int f = 0; f < frames; ++f)
	{
		const double t0 = wallMs();
		logic.runLogicFrame();
		const double t1 = wallMs();
		out.hashes.push_back(logic.computeStateHash());
		out.logicMs.push_back(t1 - t0);
		out.hashMs.push_back(wallMs() - t1);
	}
	out.cpuMs = processCpuMs() - c0;
	out.wallTotalMs = wallMs() - w0;
	return out;
}
} // namespace

TEST_CASE("perf2 bench: the big battle's logic step and state hash on the wall clock with the logic job pool at 1 thread and at N threads" * doctest::skip())
{
	if (!hudtest::haveWorld("perf2 bench"))
	{
		return;
	}
	const int frames = perfbench::envInt("OPENBFME_BENCH_FRAMES", 400);
	const int n = perfbench::envInt("OPENBFME_BENCH_THREADS", JobSystem::defaultThreadCount());
	const int before = JobSystem::logic().threadCount();
	std::vector<std::uint32_t> reference;
	for (int threads : { 1, n, 1, n })
	{
		JobSystem::logic().setThreadCount(threads);
		const BenchRun r = runBattle(hudtest::shared(), frames);
		const perfbench::FrameStats l = perfbench::stats(r.logicMs), h = perfbench::stats(r.hashMs);
		std::printf("  info: perf2 bench big battle %d frames, %2d logic threads: logic step (wall) mean %.3f ms p95 %.3f ms max %.3f ms; state hash (wall) mean "
		            "%.3f ms p95 %.3f ms; process CPU %.3f ms per frame over %.3f ms wall per frame (%.2f cores busy); hash chain %08x\n",
			frames, threads, l.mean, l.p95, l.max, h.mean, h.p95, r.cpuMs / frames, r.wallTotalMs / frames, r.cpuMs / r.wallTotalMs,
			(unsigned)perfbench::chain(r.hashes));
		if (reference.empty())
		{
			reference = r.hashes;
		}
		CHECK(r.hashes == reference);
	}
	JobSystem::logic().setThreadCount(before);
}

TEST_CASE("perf2 bench: the job system's dispatch cost and the scaling of a compute-bound loop with 1 .. N threads" * doctest::skip())
{
	std::vector<int> counts = { 1, 2, 3, 4, 6, 8 };
	if (JobSystem::defaultThreadCount() > 8)
	{
		counts.push_back(JobSystem::defaultThreadCount());
	}
	for (int threads : counts)
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Simulation, "bench");
		// an empty loop of 64 chunks: the hand-over alone
		const int dispatches = 2000;
		double t0 = wallMs();
		for (int i = 0; i < dispatches; ++i)
		{
			pool.parallelFor(64, 1, [](size_t, size_t, size_t) {});
		}
		const double emptyUs = (wallMs() - t0) * 1000.0 / dispatches;
		// 64 chunks of pure arithmetic (about 1 ms in all on one thread)
		std::vector<std::uint32_t> out(64);
		std::vector<double> ms;
		for (int rep = 0; rep < 50; ++rep)
		{
			t0 = wallMs();
			pool.parallelFor(64, 1, [&out](size_t chunk, size_t, size_t) {
				std::uint32_t x = (std::uint32_t)chunk + 1;
				for (int i = 0; i < 60000; ++i)
				{
					x = x * 1664525u + 1013904223u;
					x ^= x >> 13;
				}
				out[chunk] = x;
			});
			ms.push_back(wallMs() - t0);
		}
		std::sort(ms.begin(), ms.end());
		std::printf("  info: perf2 bench jobs, %2d threads: empty dispatch of 64 chunks %.1f us; compute loop p50 %.3f ms, min %.3f ms\n", threads, emptyUs, ms[ms.size() / 2],
			ms.front());
	}
}

TEST_CASE("perf2 bench: the skirmish map load (LiveGame::load of map mp fall back 4p, 4 computer players) with the job pools at 1 thread and at N" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	const int n = perfbench::envInt("OPENBFME_BENCH_THREADS", JobSystem::defaultThreadCount());
	const int logicBefore = JobSystem::logic().threadCount(), clientBefore = JobSystem::client().threadCount();
	for (int threads : { 1, n, 1, n, 1, n })
	{
		JobSystem::logic().setThreadCount(threads);
		JobSystem::client().setThreadCount(threads);
		perf1::Run run;
		perf1::runAISkirmish(*s->mount->fs, *s->world, s->options, s->settings, perf1::fourFactions(), 1, run);
		std::printf("  info: perf2 bench skirmish map load, %2d threads: %.1f ms (%zu objects)\n", threads, run.loadMs, run.objectsStart);
	}
	JobSystem::logic().setThreadCount(logicBefore);
	JobSystem::client().setThreadCount(clientBefore);
}
