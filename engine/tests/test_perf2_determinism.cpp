// OpenBFME unit tests. GPL-3.0.
// Lane PERF-2: the determinism matrix of the parallel logic. The logic job pool (JobSystem::logic()) runs the pure phases of a frame (the collision
// snapshot and pair search) on 1, 2, 4 or N threads; the state after every frame must be the single-threaded build's, bit for bit, with
// any thread count and in every run. The references are PERF-1's per-frame hashes (tests/data/perf1: the big battle and the 4-player computer
// skirmish measured with the single-threaded build before any optimisation).
//
//   * default: the big battle's first 150 frames with 1, 2, 4 and N threads, the skirmish's first 600 frames with 1 and N threads;
//   * "perf2 long": the whole 400-frame battle and 6000-frame (20 game minutes) skirmish with 1, 2, 4 and N threads and N twice (skipped by default:
//     openbfme_tests -tc="perf2 long*" --no-skip).
//
// SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "PerfScenarios.h"
#include "RetailTestMount.h"

#include "Common/JobSystem.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <cstdio>
#include <thread>
#include <string>
#include <vector>

namespace
{
// restores the logic pool's thread count when the test ends
struct LogicThreadsGuard
{
	int before = JobSystem::logic().threadCount();
	~LogicThreadsGuard() { JobSystem::logic().setThreadCount(before); }
};

// 1, 2, 4, 6 and every hardware thread (N, the default pool size)
std::vector<int> matrix()
{
	std::vector<int> t = { 1, 2, 4 };
	for (int n : { 6, JobSystem::defaultThreadCount(), (int)std::thread::hardware_concurrency() }) // 6 explicitly (Sol r1), then every hardware thread
	{
		if (n > t.back())
		{
			t.push_back(n);
		}
	}
	return t;
}

std::vector<std::uint32_t> prefix(const std::vector<std::uint32_t> &v, size_t n)
{
	return std::vector<std::uint32_t>(v.begin(), v.begin() + (long)std::min(n, v.size()));
}

// the battle (`battleFrames`) with every thread count of `battleThreads`, then the skirmish (`skirmishFrames`) with every count of `skirmishThreads`,
// each compared frame by frame with the reference prefix; with more than one thread the workers must have run chunks
void runMatrix(int battleFrames, const std::vector<int> &battleThreads, int skirmishFrames, const std::vector<int> &skirmishThreads)
{
	const std::vector<std::uint32_t> battleRef = perf1::readHashes("big_battle_400.hashes");
	const std::vector<std::uint32_t> skirmishRef = perf1::readHashes("skirmish4_6000.hashes");
	REQUIRE(battleRef.size() == 400u);
	REQUIRE(skirmishRef.size() == 6000u);
	if (!retailtest::pureMount())
	{
		retailtest::printSkip("perf2 determinism matrix");
		return;
	}
	LogicThreadsGuard guard;
	hudtest::SharedWorld w;
	GameLogicSettings settings;
	std::string error;
	REQUIRE_MESSAGE(perf1::loadFreshWorld(w, settings, error), error);
	for (int threads : battleThreads)
	{
		JobSystem::logic().setThreadCount(threads);
		const JobSystem::Stats before = JobSystem::logic().stats();
		perf1::Run run;
		perf1::runBigBattle(w, battleFrames, run);
		const JobSystem::Stats after = JobSystem::logic().stats();
		INFO("big battle, " << threads << " logic threads");
		CHECK(run.hashes.size() == (size_t)battleFrames);
		CHECK(perf1::firstDifference(run.hashes, prefix(battleRef, (size_t)battleFrames)) == -1);
		CHECK(after.dispatches > before.dispatches);
		if (threads > 1)
		{
			CHECK(after.workerChunks > before.workerChunks); // the phases really ran on several threads
		}
		std::printf("  info: perf2 big battle %d frames, %d logic threads: dispatches %llu (inline %llu), chunks %llu (on workers %llu)\n", battleFrames,
			threads, (unsigned long long)(after.dispatches - before.dispatches), (unsigned long long)(after.inlineDispatches - before.inlineDispatches),
			(unsigned long long)(after.chunks - before.chunks), (unsigned long long)(after.workerChunks - before.workerChunks));
	}
	for (int threads : skirmishThreads)
	{
		JobSystem::logic().setThreadCount(threads);
		perf1::Run run;
		perf1::runAISkirmish(*w.mount->fs, *w.world, w.options, settings, perf1::fourFactions(), skirmishFrames, run);
		INFO("4-player skirmish, " << threads << " logic threads");
		CHECK(run.hashes.size() == (size_t)skirmishFrames);
		CHECK(perf1::firstDifference(run.hashes, prefix(skirmishRef, (size_t)skirmishFrames)) == -1);
	}
}
} // namespace

TEST_CASE("perf2: the big battle (150 frames: 1, 2, 4, N logic threads) and the 4-player AI skirmish (600 frames: 1, N) hash as the single-threaded "
		  "reference in every frame")
{
	runMatrix(150, matrix(), 600, { 1, std::max(2, matrix().back()) });
}

TEST_CASE("perf2 long: the whole big battle (400 frames) and 20-minute skirmish (6000 frames) with 1, 2, 4, N and N again logic threads equal the "
		  "reference in every frame" * doctest::skip())
{
	std::vector<int> t = matrix();
	t.push_back(t.back()); // a repeated run: the scheduling of the threads must not matter
	runMatrix(400, t, 6000, t);
}
