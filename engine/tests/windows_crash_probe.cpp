// OpenBFME. GPL-3.0. Destructive tests: run only as subprocesses.
#include "Common/WindowsCrashDump.h"
#include "Common/PathIO.h"
#include "Common/JobSystem.h"
#include "Libraries/WWVegas/WWLib/workerpool.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>

__declspec(noinline) void crashProbeFault()
{
	volatile int *volatile pointer = nullptr;
	*pointer = 42;
}

void handledFaults()
{
	__try { crashProbeFault(); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { RaiseException(0xC0000409u, 0, 0, nullptr); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

LONG WINAPI replacedFilter(EXCEPTION_POINTERS *) { return EXCEPTION_CONTINUE_SEARCH; }

static int crashMain(int argc, char **argv);

int main(int argc, char **argv)
{
	return PathIO::runMain(argc, argv, crashMain);
}

static int crashMain(int argc, char **argv)
{
	if (argc != 3)
		return 2;
	const std::string mode = argv[1];
	std::atomic<bool> go{false};
	std::thread existingWorker([&] {
		while (!go.load()) std::this_thread::yield();
		if (mode == "existing-worker") crashProbeFault();
	});
	std::string error;
	if (!WindowsCrashDump::install(argv[2], error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		go = true;
		existingWorker.join();
		return 3;
	}
	std::printf("MAIN_THREAD=%lu\n", GetCurrentThreadId());
	std::fflush(stdout);
	go = true;
	existingWorker.join();
	if (mode == "main") crashProbeFault();
	else if (mode == "worker") std::thread(crashProbeFault).join();
	else if (mode == "terminate") std::thread([] { throw std::runtime_error("uncaught worker exception"); }).join();
	else if (mode == "fatal")
	{
		// Only the vectored path can write this dump after our top-level filter is replaced.
		SetUnhandledExceptionFilter(replacedFilter);
		RaiseException(0xC0000409u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	}
	else if (mode == "handled")
	{
		handledFaults();
		try { throw std::runtime_error("handled C++ exception"); } catch (...) {}
	}
	else if (mode == "reassert")
	{
		SetUnhandledExceptionFilter(replacedFilter);
		if (!WindowsCrashDump::reassertHandlers() || WindowsCrashDump::reassertHandlers())
			return 4;
		std::thread(crashProbeFault).join();
	}
	else if (mode == "job" || mode == "pool")
	{
		const DWORD mainThread = GetCurrentThreadId();
		auto body = [mainThread] {
			if (GetCurrentThreadId() != mainThread) crashProbeFault();
			// Keep the calling thread from consuming both chunks before a worker starts.
			Sleep(200);
		};
		if (mode == "job")
		{
			JobSystem pool(2, JobSystem::FloatingPoint::Client, "crash-probe");
			for (int run = 0; run < 50 && pool.stats().workerChunks == 0; ++run)
			{
				pool.parallelFor(64, 1, [](size_t, size_t, size_t) { Sleep(1); });
			}
			if (pool.stats().workerChunks == 0)
				return 7;
			pool.parallelFor(2, 1, [&](size_t, size_t, size_t) { body(); });
		}
		else
		{
			WorkerPool pool(1);
			pool.run(2, [&](size_t, size_t) { body(); }, 1);
		}
		return 5; // a pool test must crash on a helper, never silently pass
	}
	else if (mode != "handled") return 2;
	WindowsCrashDump::uninstall();
	// No callbacks may survive unloading the extension; exercise a fresh lifecycle.
	if (!WindowsCrashDump::install(argv[2], error)) return 6;
	WindowsCrashDump::uninstall();
	return 0;
}
