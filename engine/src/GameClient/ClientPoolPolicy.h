// OpenBFME. GPL-3.0.
// The client job pool's size while the logic worker is behind its clock (lane PERF-CLIENT).
//
// The logic runs on its own worker thread (SMOOTH-1) and the render frame's parallel passes (the drawable sync, the view refresh, the
// instancer's poses) run on the client job pool. Both pools default to every hardware thread, so while the logic is behind its clock its
// single-threaded passes compete with the client's workers for the same cores. Measured (30,000 troops, Uber, 8 cores / 16 threads, the
// real client): a client pool fixed at 4 threads instead of 16 gives the logic 21 % more frames a second and costs the render 16 % of
// its frames (the rule's own effect is in docs/UBER.md). The logic rate only matters while the logic is behind, so the pool shrinks then
// and grows back once the worker is well under its clock again (kLeaveFrames frames in a row under recoveredMs, not merely on time).
//
// ClientPoolPolicy is pure: the worker feeds it every frame it completes (the frame's wall time) and reads back whether the logic is
// behind. It lives in the LiveGame and is fed on the worker thread, under the publication mutex, so every worker frame counts whatever the
// render rate (the #194 review: a render thread sampling the worker's last frame would skip the completions between two of its advances
// and stretch the streaks). Integer milliseconds, and the only floating-point operations are comparisons (GodotGameWorld.cpp is a
// simulation source: tools/sim/sim_audit.py).
// ClientPoolSizer applies the answer to a pool on the main thread (every client dispatch is the render thread's; JobSystem::setThreadCount
// resizes between dispatches) and restores the full size when it dies, so a game that ends while behind leaves the pool as it found it
// for the menu and the next load (the parallel loaders run on the same pool). A sizer touches the pool only when the size it wants
// changes, so a second live world on the same pool (a parity test's two worlds, a view switch's) that never shrank it never resizes it
// either, and two sizers do not trade sizes every frame.
// Nothing of the simulation changes: the pool's size only decides which thread runs a client slice.
#pragma once

#include "Common/GameCommon.h"
#include "Common/JobSystem.h"

#include <algorithm>

class ClientPoolPolicy
{
public:
	// a worker frame over budgetMs() is late; kEnterFrames consecutive late worker frames shrink the pool, kLeaveFrames consecutive worker frames
	// under recoveredMs() grow it back (hysteresis: a frame between the two does neither, and a late frame while recovering starts the count again).
	// A worker hovering about the two marks still flips the pool, at most once every kEnterFrames + kLeaveFrames worker frames (8: the late ones
	// over 200 ms, the recovered under 150, so about a second), each flip joining and starting the threads under the pool's dispatch mutex;
	// never every frame
	static constexpr int kEnterFrames = 3;
	static constexpr int kLeaveFrames = 5;
	static constexpr int budgetMs() { return 1000 / LOGICFRAMES_PER_SECOND; } // the logic clock's frame (200)
	static constexpr int recoveredMs() { return budgetMs() * 3 / 4; }         // 150
	// the shrunk size: a quarter of the hardware threads, at least 2, never more than there are (16 -> 4, 8 -> 2, 4 -> 2, 2 -> 2, 1 -> 1)
	static constexpr int behindThreadsFor(int hardwareThreads) { return std::min(hardwareThreads, std::max(2, hardwareThreads / 4)); }

	bool behind() const { return m_isBehind; }

	// a completed worker frame of `ms` wall time; whether the logic is behind its clock after it
	bool frame(double ms)
	{
		if (ms > (double)budgetMs())
		{
			m_under = 0;
			if (!m_isBehind && ++m_over >= kEnterFrames)
			{
				m_isBehind = true;
			}
		}
		else
		{
			m_over = 0;
			if (m_isBehind && ms < (double)recoveredMs())
			{
				if (++m_under >= kLeaveFrames)
				{
					m_isBehind = false;
				}
			}
			else
			{
				m_under = 0;
			}
		}
		return m_isBehind;
	}

	// the worker starts (a new game, the logic back from the calling thread): not behind, the counts from the start
	void reset()
	{
		m_isBehind = false;
		m_over = m_under = 0;
	}

private:
	bool m_isBehind = false;
	int m_over = 0;  // late worker frames in a row (while not behind)
	int m_under = 0; // recovered worker frames in a row (while behind)
};

// the pool's size follows the policy's answer: `fullThreads` (the hardware's) or `behindThreads` while the logic is behind. One per live
// game, made with it and restored when the game ends; the sizer resizes only when the size it wants changes (`m_applied`), see above
class ClientPoolSizer
{
public:
	ClientPoolSizer(JobSystem &pool, int fullThreads, int behindThreads)
		: m_pool(pool), m_full(fullThreads), m_behind(std::min(behindThreads, fullThreads)), m_applied(fullThreads)
	{
	}
	~ClientPoolSizer() { restore(); }
	ClientPoolSizer(const ClientPoolSizer &) = delete;
	ClientPoolSizer &operator=(const ClientPoolSizer &) = delete;

	int fullThreads() const { return m_full; }
	int behindThreads() const { return m_behind; }
	bool behind() const { return m_isBehind; } // the answer last applied

	void apply(bool behind)
	{
		m_isBehind = behind;
		set(behind ? m_behind : m_full);
	}
	void restore()
	{
		m_isBehind = false;
		set(m_full);
	}
	bool shrunk() const { return m_pool.threadCount() < m_full; }

private:
	void set(int threads)
	{
		if (threads == m_applied)
		{
			return; // the size this sizer wants is unchanged: another sizer's choice stands
		}
		m_applied = threads;
		if (m_pool.threadCount() != threads)
		{
			m_pool.setThreadCount(threads);
		}
	}
	JobSystem &m_pool;
	int m_full;
	int m_behind;
	bool m_isBehind = false;
	int m_applied; // the size this sizer last set
};
