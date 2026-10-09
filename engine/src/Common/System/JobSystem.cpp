// OpenBFME. GPL-3.0.
// Lane PERF-2: see Common/JobSystem.h (OpenBFME infrastructure, not a port: retail runs its logic on one thread).

#include "Common/JobSystem.h"

#include "Common/NumericState.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace
{
// the pool whose chunk the calling thread is running (a dispatch from inside a chunk of the same pool runs inline)
thread_local const JobSystem *t_runningPool = nullptr;

struct RunningScope
{
	const JobSystem *previous;
	explicit RunningScope(const JobSystem *pool) : previous(t_runningPool) { t_runningPool = pool; }
	~RunningScope() { t_runningPool = previous; }
};

std::string environmentError(const std::string &pool, const char *when, std::size_t chunk)
{
	char b[96];
	std::snprintf(b, sizeof(b), "MXCSR 0x%08X, x87 control word 0x%04X", (unsigned)NumericState::readControlRegister(), (unsigned)NumericState::readX87ControlWord());
	return "JobSystem '" + pool + "': chunk " + std::to_string(chunk) + " " + when + " the canonical floating-point environment (" + b +
		", expected MXCSR 0x1F80 with the exception flags ignored and x87 0x037F)";
}

// the text of a chunk's own exception, for the environment error that replaces it
std::string describe(std::exception_ptr e)
{
	try
	{
		std::rethrow_exception(e);
	}
	catch (const std::exception &x)
	{
		return x.what();
	}
	catch (...)
	{
		return "a non-standard exception";
	}
}

int envThreads(const char *name)
{
	const char *v = std::getenv(name);
	if (v && *v)
	{
		const int n = std::atoi(v);
		if (n < 1 || n > 256)
		{
			throw std::invalid_argument(std::string(name) + "=" + v + ": the thread count must be 1 .. 256");
		}
		return n;
	}
	return JobSystem::defaultThreadCount();
}
} // namespace

JobSystem::JobSystem(int threads, FloatingPoint fp, const char *name) : m_fp(fp), m_name(name ? name : "")
{
	setThreadCount(threads);
}

JobSystem::~JobSystem()
{
	stopWorkers();
}

int JobSystem::defaultThreadCount()
{
	// every hardware thread (owner: use every core; Sol r1)
	return (int)std::max(1u, std::thread::hardware_concurrency());
}

JobSystem &JobSystem::logic()
{
	static JobSystem pool(envThreads("OPENBFME_LOGIC_THREADS"), FloatingPoint::Simulation, "logic");
	return pool;
}

JobSystem &JobSystem::client()
{
	static JobSystem pool(envThreads("OPENBFME_CLIENT_THREADS"), FloatingPoint::Client, "client");
	return pool;
}

void JobSystem::setThreadCount(int threads)
{
	if (threads < 1)
	{
		throw std::invalid_argument("JobSystem::setThreadCount: at least one thread (the caller)");
	}
	std::lock_guard<std::mutex> dispatching(m_dispatchMutex); // never while a dispatch runs
	stopWorkers();
	m_threads = threads;
	startWorkers();
}

void JobSystem::startWorkers()
{
	m_stop = false;
	m_workers.reserve((size_t)(m_threads - 1));
	for (int i = 1; i < m_threads; ++i)
	{
		m_workers.emplace_back(&JobSystem::workerMain, this, i);
	}
}

void JobSystem::stopWorkers()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	for (std::thread &t : m_workers)
	{
		t.join();
	}
	m_workers.clear();
}

JobSystem::Stats JobSystem::stats() const
{
	Stats s;
	s.dispatches = m_statDispatches.load(std::memory_order_relaxed);
	s.inlineDispatches = m_statInline.load(std::memory_order_relaxed);
	s.chunks = m_statChunks.load(std::memory_order_relaxed);
	s.workerChunks = m_statWorkerChunks.load(std::memory_order_relaxed);
	return s;
}

void JobSystem::fail(std::size_t chunk, std::exception_ptr e, bool replaceSameChunk)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_error || chunk < m_failedChunk || (replaceSameChunk && chunk == m_failedChunk))
	{
		m_error = e;
		m_failedChunk = chunk;
	}
}

void JobSystem::runInline(std::size_t count, std::size_t grain, Thunk fn, void *body)
{
	m_statInline.fetch_add(1, std::memory_order_relaxed);
	const std::size_t chunks = chunkCount(count, grain);
	RunningScope running(this);
	for (std::size_t c = 0; c < chunks; ++c)
	{
		if (m_fp == FloatingPoint::Simulation && !NumericState::floatingPointEnvironmentIsCanonical())
		{
			throw std::logic_error(environmentError(m_name, "started outside", c));
		}
		const std::size_t begin = c * grain;
		try
		{
			fn(body, c, begin, std::min(count, begin + grain));
		}
		catch (...)
		{
			// the post-chunk check runs on a throwing chunk too (Sol r1): a chunk that threw AND left another environment reports the environment (with
			// its own exception's text), and this thread is put back in the canonical one before the error leaves
			if (m_fp == FloatingPoint::Simulation && !NumericState::floatingPointEnvironmentIsCanonical())
			{
				const std::string what = environmentError(m_name, "left", c) + " after throwing: " + describe(std::current_exception());
				NumericState::normalizeFloatingPointEnvironment();
				throw std::logic_error(what);
			}
			throw;
		}
		m_statChunks.fetch_add(1, std::memory_order_relaxed);
		if (m_fp == FloatingPoint::Simulation && !NumericState::floatingPointEnvironmentIsCanonical())
		{
			const std::string what = environmentError(m_name, "left", c);
			NumericState::normalizeFloatingPointEnvironment();
			throw std::logic_error(what);
		}
	}
}

void JobSystem::runChunks(bool onWorker)
{
	RunningScope running(this);
	for (;;)
	{
		const std::size_t c = m_nextChunk.fetch_add(1, std::memory_order_relaxed);
		if (c >= m_chunks)
		{
			return;
		}
		if (m_fp == FloatingPoint::Simulation && !NumericState::floatingPointEnvironmentIsCanonical())
		{
			fail(c, std::make_exception_ptr(std::logic_error(environmentError(m_name, "started outside", c))));
			NumericState::normalizeFloatingPointEnvironment();
			continue;
		}
		const std::size_t begin = c * m_grain;
		std::exception_ptr thrown;
		try
		{
			m_fn(m_body, c, begin, std::min(m_count, begin + m_grain));
		}
		catch (...)
		{
			thrown = std::current_exception();
			fail(c, thrown);
		}
		m_statChunks.fetch_add(1, std::memory_order_relaxed);
		if (onWorker)
		{
			m_statWorkerChunks.fetch_add(1, std::memory_order_relaxed);
		}
		if (m_fp == FloatingPoint::Simulation && !NumericState::floatingPointEnvironmentIsCanonical())
		{
			// as runInline: the environment error replaces the chunk's own exception (whose text it carries)
			std::string what = environmentError(m_name, "left", c);
			if (thrown)
			{
				what += " after throwing: " + describe(thrown);
			}
			fail(c, std::make_exception_ptr(std::logic_error(what)), true);
			NumericState::normalizeFloatingPointEnvironment();
		}
	}
}

void JobSystem::dispatch(std::size_t count, std::size_t grain, Thunk fn, void *body)
{
	m_statDispatches.fetch_add(1, std::memory_order_relaxed);
	const std::size_t chunks = chunkCount(count, grain);
	if (m_threads <= 1 || chunks <= 1 || t_runningPool == this || !m_dispatchMutex.try_lock())
	{
		runInline(count, grain, fn, body);
		return;
	}
	std::lock_guard<std::mutex> dispatching(m_dispatchMutex, std::adopt_lock);
	if (m_fp == FloatingPoint::Simulation && !NumericState::floatingPointEnvironmentIsCanonical())
	{
		throw std::logic_error(environmentError(m_name, "was dispatched from a thread outside", 0));
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_fn = fn;
		m_body = body;
		m_count = count;
		m_grain = grain;
		m_chunks = chunks;
		m_nextChunk.store(0, std::memory_order_relaxed);
		m_error = nullptr;
		m_failedChunk = std::numeric_limits<std::size_t>::max();
		m_open = true;
		m_generation.fetch_add(1, std::memory_order_release);
	}
	m_wake.notify_all();
	runChunks(false);
	std::exception_ptr error;
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_open = false; // late workers do not join; the ones inside finish their chunk and leave
		m_done.wait(lock, [this] { return m_joined == 0; });
		error = m_error;
		m_error = nullptr;
		m_fn = nullptr;
		m_body = nullptr;
	}
	if (error)
	{
		std::rethrow_exception(error);
	}
}

void JobSystem::workerMain(int index)
{
	(void)index;
	NumericState::normalizeFloatingPointEnvironment();
	std::uint64_t seen = m_generation.load(std::memory_order_acquire);
	for (;;)
	{
		// a short spin first: the logic dispatches several loops a frame, a sleeping worker would miss most of a short one
		for (int spin = 0; spin < 64 && m_generation.load(std::memory_order_acquire) == seen; ++spin)
		{
			std::this_thread::yield();
		}
		std::unique_lock<std::mutex> lock(m_mutex);
		m_wake.wait(lock, [&] { return m_stop || m_generation.load(std::memory_order_relaxed) != seen; });
		if (m_stop)
		{
			return;
		}
		seen = m_generation.load(std::memory_order_relaxed);
		if (!m_open)
		{
			continue;
		}
		++m_joined;
		lock.unlock();
		if (m_fp == FloatingPoint::Simulation)
		{
			NumericState::normalizeFloatingPointEnvironment(); // whatever ran on this thread before, each dispatch starts canonical (and runChunks verifies it)
		}
		runChunks(true);
		lock.lock();
		if (--m_joined == 0)
		{
			m_done.notify_all();
		}
	}
}
