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
// ClientPoolPolicy is pure: a completed worker frame (its completion count and its wall time) goes in, the thread count the pool should
// have comes out. A render frame that sees the same completion again changes nothing: the counts are worker frames, whatever the render
// rate. Integer milliseconds throughout, and the only floating-point operations are comparisons (GodotGameWorld.cpp is a simulation
// source: tools/sim/sim_audit.py). ClientPoolSizer applies the policy to a pool and restores the full size when it dies, so a game that
// ends while behind leaves the pool as it found it for the menu and the next load (the parallel loaders run on the same pool). A sizer
// touches the pool only when the size it wants changes, so a second live world on the same pool (a parity test's two worlds, a view
// switch's) that never shrank it never resizes it either, and two sizers do not trade sizes every frame.
// Nothing of the simulation changes: the pool's size only decides which thread runs a client slice. Main thread only (every client
// dispatch is the render thread's; JobSystem::setThreadCount resizes between dispatches).
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

	ClientPoolPolicy(int fullThreads, int behindThreads)
		: m_full(fullThreads), m_behind(std::min(behindThreads, fullThreads))
	{
	}

	int fullThreads() const { return m_full; }
	int behindThreads() const { return m_behind; }
	bool behind() const { return m_isBehind; }

	// a render frame's view of the worker: `completed` worker frames so far, the last one's wall time; the threads the pool should have now
	int update(unsigned long long completed, double lastFrameMs)
	{
		if (completed == m_seen)
		{
			return m_isBehind ? m_behind : m_full; // the same worker frame as last time
		}
		m_seen = completed;
		if (lastFrameMs > (double)budgetMs())
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
			if (m_isBehind && lastFrameMs < (double)recoveredMs())
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
		return m_isBehind ? m_behind : m_full;
	}

	// a new game, or the logic back on the calling thread: the pool's full size, the counts from the start
	void reset()
	{
		m_isBehind = false;
		m_over = m_under = 0;
		m_seen = 0;
	}

private:
	int m_full;
	int m_behind;
	bool m_isBehind = false;
	int m_over = 0;
	int m_under = 0;
	unsigned long long m_seen = 0;
};

// Applies the policy to a pool; the pool has its full size again when the sizer dies or is reset. The pool is at its full size when the
// sizer is made (every sizer restores it); the sizer resizes only when the size it wants changes (`m_applied`), see above
class ClientPoolSizer
{
public:
	ClientPoolSizer(JobSystem &pool, int fullThreads, int behindThreads)
		: m_pool(pool), m_policy(fullThreads, behindThreads), m_applied(fullThreads)
	{
	}
	~ClientPoolSizer() { restore(); }
	ClientPoolSizer(const ClientPoolSizer &) = delete;
	ClientPoolSizer &operator=(const ClientPoolSizer &) = delete;

	void apply(unsigned long long completed, double lastFrameMs) { set(m_policy.update(completed, lastFrameMs)); }
	void restore()
	{
		m_policy.reset();
		set(m_policy.fullThreads());
	}
	bool shrunk() const { return m_pool.threadCount() < m_policy.fullThreads(); }
	const ClientPoolPolicy &policy() const { return m_policy; }

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
	ClientPoolPolicy m_policy;
	int m_applied; // the size this sizer last set
};
