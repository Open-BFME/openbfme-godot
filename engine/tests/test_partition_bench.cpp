// OpenBFME retail benchmark (lane MODULES-2, Sol r1 item 3): a big battle on a retail map in a LiveGame (several hundred units fighting: Gondor and Mordor
// hordes with auras, the Mordor group bonus, attack trolls radiating fear) and the logic time of its frames. Skipped by default (a measurement, not a check);
// run it with
//
//   openbfme_tests -tc="partition bench*" --no-skip
//
// It prints the per-frame logic time (mean, p95, max) and the state hash of the last frame: the same hash before and after a performance change shows the
// change kept the results and the order. OPENBFME_BENCH_FRAMES overrides the fight frames (default 400). SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "HudTestUtil.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifndef _WIN32
#include <time.h>
#endif
#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace hudtest;

namespace
{
// the thread's CPU time in ms (the machine runs other jobs: wall time would count their load); the steady clock on Windows
double nowMs()
{
#ifdef _WIN32
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
	timespec ts{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
}

// the thread's user-mode instructions (Linux perf counter; -1 where it is unavailable): unlike the time, nearly independent of the machine's load
struct InstructionCounter
{
	int fd = -1;
	explicit InstructionCounter(bool cycles = false)
	{
#ifdef __linux__
		perf_event_attr a{};
		a.type = PERF_TYPE_HARDWARE;
		a.size = sizeof(a);
		a.config = cycles ? PERF_COUNT_HW_CPU_CYCLES : PERF_COUNT_HW_INSTRUCTIONS;
		a.exclude_kernel = 1;
		a.exclude_hv = 1;
		fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
#endif
	}
	~InstructionCounter()
	{
#ifdef __linux__
		if (fd >= 0)
		{
			close(fd);
		}
#endif
	}
	long long read() const
	{
#ifdef __linux__
		long long v = 0;
		if (fd >= 0 && ::read(fd, &v, sizeof(v)) == (ssize_t)sizeof(v))
		{
			return v;
		}
#endif
		return -1;
	}
};
} // namespace

TEST_CASE("partition bench: a big retail battle (hordes, heroes, attack trolls) - logic time per frame and the final state hash" * doctest::skip())
{
	if (!haveWorld("partition bench"))
	{
		return;
	}
	SharedWorld &sh = shared();
	auto scope = sh.world->enterContext();
	Rig rig(sh);
	GameLogic &logic = rig.logic();
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	// the middle of the map's objects (hero_viewer's focus)
	float sx = 0.0f, sy = 0.0f;
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		sx += o->getPosition()->x;
		sy += o->getPosition()->y;
		++n;
	}
	REQUIRE(n > 0);
	const float cx = sx / (float)n, cy = sy / (float)n;
	const char *ours[] = { "GondorFighterHorde", "GondorFighterHorde", "GondorTowerShieldGuardHorde", "GondorArcherHorde" };
	const char *theirs[] = { "MordorFighterHorde", "MordorFighterHorde", "MordorFighterHorde", "MordorArcherHorde" };
	std::vector<Object *> good, evil;
	const int rows = 4, cols = 6;
	for (int r = 0; r < rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			const float y = cy + ((float)c - (cols - 1) * 0.5f) * 90.0f;
			good.push_back(rig.make(ours[(r + c) % 4], cx - 260.0f - 90.0f * (float)r, y));
			evil.push_back(rig.make(theirs[(r + c) % 4], cx + 260.0f + 90.0f * (float)r, y, enemy));
		}
	}
	good.push_back(rig.make("GondorAragorn", cx - 200.0f, cy));
	good.push_back(rig.make("GondorBoromir", cx - 200.0f, cy + 60.0f));
	for (int i = 0; i < 4; ++i)
	{
		evil.push_back(rig.make("MordorAttackTroll", cx + 200.0f, cy + ((float)i - 1.5f) * 120.0f, enemy));
	}
	logic.runLogicFrame();
	logic.runLogicFrame();
	size_t objects = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		++objects;
	}
	// every unit attacks the enemy unit facing it
	for (size_t i = 0; i < good.size(); ++i)
	{
		if (AIUpdateInterface *ai = good[i]->getAIUpdateInterface())
		{
			ai->aiAttackObject(evil[i % evil.size()], CMD_FROM_PLAYER);
		}
	}
	for (size_t i = 0; i < evil.size(); ++i)
	{
		if (AIUpdateInterface *ai = evil[i]->getAIUpdateInterface())
		{
			ai->aiAttackObject(good[i % good.size()], CMD_FROM_PLAYER);
		}
	}
	int frames = 400;
	if (const char *env = std::getenv("OPENBFME_BENCH_FRAMES"))
	{
		frames = std::max(1, std::atoi(env));
	}
	std::vector<double> ms;
	ms.reserve((size_t)frames);
	InstructionCounter counter;
	InstructionCounter cycles(true);
	const long long c0 = cycles.read();
	const long long i0 = counter.read();
	for (int f = 0; f < frames; ++f)
	{
		const double t0 = nowMs();
		logic.runLogicFrame();
		ms.push_back(nowMs() - t0);
	}
	const long long i1 = counter.read();
	const long long c1 = cycles.read();
	size_t alive = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		++alive;
	}
	std::vector<double> sorted = ms;
	std::sort(sorted.begin(), sorted.end());
	double sum = 0.0;
	for (double v : ms)
	{
		sum += v;
	}
	std::printf("  info: partition bench: %zu objects at the start (%zu hordes / heroes / trolls ordered), %zu at the end; %d frames: logic (thread CPU) mean %.3f ms, p95 %.3f ms, "
	            "max %.3f ms; %.2f M user instructions, %.2f M user cycles per frame; final state hash %08x\n",
		objects, good.size() + evil.size(), alive, frames, sum / (double)frames, sorted[(size_t)((double)frames * 0.95) < sorted.size() ? (size_t)((double)frames * 0.95) : sorted.size() - 1],
		sorted.back(), (i0 >= 0 && i1 >= 0) ? (double)(i1 - i0) / (double)frames / 1.0e6 : -1.0,
		(c0 >= 0 && c1 >= 0) ? (double)(c1 - c0) / (double)frames / 1.0e6 : -1.0, (unsigned)logic.computeStateHash());
	CHECK(alive < objects); // the armies fought

	// the queries alone (Sol r1's measurement): 200 local scans sorted near to far (radius 150, an "enemy of the scanner" filter) around the living objects,
	// then 20 full-range scans (radius 10000, no filter)
	std::vector<Object *> centres;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		centres.push_back(o);
	}
	REQUIRE(!centres.empty());
	PartitionManager &pm = logic.partition();
	size_t localHits = 0, fullHits = 0;
	const long long q0 = counter.read();
	const double t0 = nowMs();
	for (int i = 0; i < 200; ++i)
	{
		Object *me = centres[(size_t)i * 7919u % centres.size()];
		PartitionFilterFn enemies([me](Object &o) { return o.getControllingPlayer() != me->getControllingPlayer(); });
		const auto hits = pm.iterateObjectsInRange(*me->getPosition(), 150.0f, FROM_CENTER_2D, { &enemies }, ITER_SORTED_NEAR_TO_FAR);
		localHits += hits.size();
	}
	const double t1 = nowMs();
	const long long q1 = counter.read();
	for (int i = 0; i < 20; ++i)
	{
		const auto hits = pm.iterateObjectsInRange(*centres[(size_t)i % centres.size()]->getPosition(), 10000.0f, FROM_CENTER_2D, {}, ITER_FASTEST);
		fullHits += hits.size();
	}
	const double t2 = nowMs();
	const long long q2 = counter.read();
	std::printf("  info: partition bench queries: 200 local sorted scans %.3f ms in all (%.1f k instructions each, %zu hits), a full-range scan %.3f ms (%.1f k "
	            "instructions, %zu hits each)\n",
		t1 - t0, (q0 >= 0 && q1 >= 0) ? (double)(q1 - q0) / 200.0 / 1.0e3 : -1.0, localHits, (t2 - t1) / 20.0,
		(q1 >= 0 && q2 >= 0) ? (double)(q2 - q1) / 20.0 / 1.0e3 : -1.0, fullHits / 20);
}
