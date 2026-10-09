// OpenBFME infrastructure (no ZH/BFME counterpart): a fork/join pool for the instancer's per-instance pose and buffer jobs.
//
// Each WorkerPool object is one generation: its threads are created by the constructor and joined by the destructor, and every
// run() publishes a fresh, reference-counted Job. A worker can therefore never see the job state, counters or closure of another
// run or another pool: nothing is reused. A worker starts at the generation the pool has when it is created, so a late-starting
// thread cannot pick up an earlier job. run() returns only after every worker has finished the job it was handed.
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class WorkerPool
{
public:
	using Fn = std::function<void(size_t, size_t)>;

	// threads = number of helper threads; the calling thread of run() is one more worker.
	explicit WorkerPool(int threads)
	{
		const int n = std::max(0, threads);
		Threads.reserve((size_t)n);
		for (int i = 0; i < n; ++i)
		{
			// Gen is 0 and cannot change before the constructor returns: no run() can happen yet.
			Threads.emplace_back([this]() { main_loop(); });
		}
	}

	~WorkerPool()
	{
		{
			std::lock_guard<std::mutex> lock(Mutex);
			Stop = true;
		}
		Wake.notify_all();
		for (std::thread &t : Threads)
		{
			if (t.joinable()) t.join();
		}
	}

	WorkerPool(const WorkerPool &) = delete;
	WorkerPool &operator=(const WorkerPool &) = delete;

	int helper_threads() const { return (int)Threads.size(); }
	std::uint64_t jobs_run() const { return JobsRun.load(); }

	// Calls fn(begin, end) over [0, count) in chunks of `chunk`, on the helpers and on the calling thread.
	void run(size_t count, const Fn &fn, size_t chunk = 16)
	{
		if (Threads.empty() || count == 0)
		{
			if (count) fn(0, count);
			return;
		}
		std::shared_ptr<Job> job = std::make_shared<Job>();
		job->Body = fn;
		job->Count = count;
		job->Chunk = chunk ? chunk : 1;
		job->Remaining.store((int)Threads.size());
		{
			std::lock_guard<std::mutex> lock(Mutex);
			Current = job;
			++Gen;
		}
		Wake.notify_all();
		work(*job);
		{
			std::unique_lock<std::mutex> lock(job->DoneMutex);
			job->Done.wait(lock, [&]() { return job->Remaining.load() == 0; });
		}
		{
			std::lock_guard<std::mutex> lock(Mutex);
			Current.reset();
		}
		++JobsRun;
	}

private:
	struct Job
	{
		Fn Body;
		size_t Count = 0;
		size_t Chunk = 16;
		std::atomic<size_t> Next{ 0 };
		std::atomic<int> Remaining{ 0 }; // helpers that have not finished this job; counted down once per helper
		std::mutex DoneMutex;
		std::condition_variable Done;
	};

	static void work(Job &job)
	{
		for (;;)
		{
			const size_t begin = job.Next.fetch_add(job.Chunk);
			if (begin >= job.Count) break;
			job.Body(begin, std::min(job.Count, begin + job.Chunk));
		}
	}

	void main_loop()
	{
		std::uint64_t seen = 0;
		for (;;)
		{
			std::shared_ptr<Job> job;
			{
				std::unique_lock<std::mutex> lock(Mutex);
				Wake.wait(lock, [&]() { return Stop || Gen != seen; });
				if (Stop) return;
				seen = Gen;
				job = Current;
			}
			if (!job) continue;
			work(*job);
			if (job->Remaining.fetch_sub(1) == 1)
			{
				std::lock_guard<std::mutex> lock(job->DoneMutex);
				job->Done.notify_all();
			}
		}
	}

	std::vector<std::thread> Threads;
	std::mutex Mutex;
	std::condition_variable Wake;
	std::shared_ptr<Job> Current;
	std::uint64_t Gen = 0;
	bool Stop = false;
	std::atomic<std::uint64_t> JobsRun{ 0 };
};
