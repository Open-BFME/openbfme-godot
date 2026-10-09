// OpenBFME. GPL-3.0.
//
// JobSystem (lane PERF-2): a fixed pool of worker threads that runs data-parallel loops for the simulation and the client.
//
// NOT A PORT. Retail RotWK runs its whole logic frame on one thread (RW GameLogic::update 0x62E4E8 has no parallel section) and neither Zero Hour nor the
// Open-BFME decompiles have a job system: this is OpenBFME infrastructure (owner 2026-10-06: "make the game multithreaded vs the single thread of the
// original game"). The simulation stays retail-exact because only work that is a pure function of the frame's state runs here, and its results are
// applied afterwards by the caller in retail's fixed order.
//
// DETERMINISM CONTRACT (lockstep: every peer must compute bit-identical state whatever its core count):
//   * parallelFor(count, grain, body) cuts [0, count) into chunks of `grain` items (the last one shorter). The cut depends only on count and grain, never on
//     the number of threads or on scheduling; chunk k always covers [k * grain, min(count, (k + 1) * grain)).
//   * body(chunk, begin, end) may only read state shared with other chunks and write what chunk `chunk` (or the items begin..end) own; the caller combines
//     the per-chunk results in chunk order after parallelFor returns. Then the result is the same as running the chunks one after another, in order,
//     on one thread, which is exactly what a pool of 1 thread (or a busy / nested dispatch, below) does.
//   * nothing in a body may draw logic RNG, create or destroy objects, or depend on which thread runs it.
//   * every thread that runs a chunk of a simulation pool runs it in the canonical floating-point environment of the numeric facade (MXCSR 0x1F80 and
//     the x87 control word 0x037F, NumericState::normalizeFloatingPointEnvironment): a worker sets it when it starts and before each dispatch and VERIFIES
//     it before and after each chunk, a throwing chunk included; a chunk that starts or leaves the thread in another environment is an error thrown on
//     the caller (it replaces the chunk's own exception, whose text it carries; the thread is put back in the canonical environment only after the error
//     is recorded). The caller's own environment is verified too (GameLogic::update establishes it).
//   * a worker has no world context (RetailObjectWorld::ContextScope): the thread_local stores are null there, so a body that reached for one would fail
//     loudly instead of reading another world's data.
//
// No allocation per dispatch: the body is passed by reference through a function pointer; workers are created once (setThreadCount). A dispatch while
// the pool is busy with another caller, or from inside a body, runs inline on the calling thread in chunk order (same results, no deadlock).

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

class JobSystem
{
public:
	enum class FloatingPoint
	{
		Simulation, ///< every chunk runs in, and must leave, the canonical environment of the numeric facade
		Client,     ///< no requirement (client work: animation, particles, textures, loading)
	};

	// `threads`: the threads that run chunks, the caller included (1: no workers, everything on the caller)
	JobSystem(int threads, FloatingPoint fp, const char *name);
	~JobSystem();
	JobSystem(const JobSystem &) = delete;
	JobSystem &operator=(const JobSystem &) = delete;

	// stops the workers and starts threads - 1 new ones (call between dispatches only)
	void setThreadCount(int threads);
	int threadCount() const { return m_threads; }

	// chunk k covers [k * grain, min(count, (k + 1) * grain)); body(chunk, begin, end) for every chunk, on any thread of the pool; returns when all ran
	template <class F>
	void parallelFor(std::size_t count, std::size_t grain, F &&body)
	{
		if (count == 0)
		{
			return;
		}
		if (grain == 0)
		{
			grain = 1;
		}
		dispatch(count, grain, &JobSystem::thunk<typename std::remove_reference<F>::type>, (void *)&body);
	}
	static std::size_t chunkCount(std::size_t count, std::size_t grain) { return grain == 0 ? count : (count + grain - 1) / grain; }

	struct Stats
	{
		std::uint64_t dispatches = 0;       ///< parallelFor calls with work
		std::uint64_t inlineDispatches = 0; ///< of those, run on the caller alone (1 thread, busy pool, nested, single chunk)
		std::uint64_t chunks = 0;           ///< chunks run
		std::uint64_t workerChunks = 0;     ///< of those, run by a worker thread
	};
	Stats stats() const;

	// the process-wide pools. logic(): the simulation's (FloatingPoint::Simulation); client(): render / audio / loading work. Their size is
	// OPENBFME_LOGIC_THREADS / OPENBFME_CLIENT_THREADS when set (>= 1), else defaultThreadCount().
	static JobSystem &logic();
	static JobSystem &client();
	// every hardware thread (at least 1)
	static int defaultThreadCount();

private:
	using Thunk = void (*)(void *body, std::size_t chunk, std::size_t begin, std::size_t end);
	template <class F>
	static void thunk(void *body, std::size_t chunk, std::size_t begin, std::size_t end)
	{
		(*static_cast<F *>(body))(chunk, begin, end);
	}

	void dispatch(std::size_t count, std::size_t grain, Thunk fn, void *body);
	void runInline(std::size_t count, std::size_t grain, Thunk fn, void *body);
	void runChunks(bool onWorker);
	void workerMain(int index);
	void startWorkers();
	void stopWorkers();
	void fail(std::size_t chunk, std::exception_ptr e, bool replaceSameChunk = false);

	const FloatingPoint m_fp;
	const std::string m_name;
	int m_threads = 1;
	std::vector<std::thread> m_workers;

	std::mutex m_dispatchMutex; ///< one dispatcher at a time; a second one (or a nested call) runs inline
	std::mutex m_mutex;         ///< the job hand-over below
	std::condition_variable m_wake, m_done;
	bool m_stop = false;
	bool m_open = false;                       ///< workers may join the current job
	std::atomic<std::uint64_t> m_generation{ 0 };
	int m_joined = 0;                          ///< workers inside the current job

	// the current job (written by the dispatcher before m_generation changes, read by the workers that joined it)
	Thunk m_fn = nullptr;
	void *m_body = nullptr;
	std::size_t m_count = 0, m_grain = 1, m_chunks = 0;
	std::atomic<std::size_t> m_nextChunk{ 0 };
	std::size_t m_failedChunk = 0;              ///< the lowest failing chunk (its exception is the one rethrown)
	std::exception_ptr m_error;

	std::atomic<std::uint64_t> m_statDispatches{ 0 }, m_statInline{ 0 }, m_statChunks{ 0 }, m_statWorkerChunks{ 0 };
};
