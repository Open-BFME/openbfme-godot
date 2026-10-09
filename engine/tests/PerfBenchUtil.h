// OpenBFME unit tests. GPL-3.0.
// Lane PERF-1: the measuring tools of the benchmarks (thread CPU time, user-mode instruction counter, frame-time statistics, per-frame state hash
// chains). Test-only; nothing here touches the simulation.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
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

namespace perfbench
{
// the thread's CPU time in ms (the machine runs other jobs: wall time would count their load); the steady clock on Windows
inline double nowMs()
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
	InstructionCounter()
	{
#ifdef __linux__
		perf_event_attr a{};
		a.type = PERF_TYPE_HARDWARE;
		a.size = sizeof(a);
		a.config = PERF_COUNT_HW_INSTRUCTIONS;
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
	InstructionCounter(const InstructionCounter &) = delete;
	InstructionCounter &operator=(const InstructionCounter &) = delete;
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

struct FrameStats
{
	double mean = 0.0, p95 = 0.0, p99 = 0.0, max = 0.0;
	int worstFrame = -1;
};

inline FrameStats stats(const std::vector<double> &ms)
{
	FrameStats s;
	if (ms.empty())
	{
		return s;
	}
	std::vector<double> sorted = ms;
	std::sort(sorted.begin(), sorted.end());
	double sum = 0.0;
	for (size_t i = 0; i < ms.size(); ++i)
	{
		sum += ms[i];
		if (s.worstFrame < 0 || ms[i] > ms[(size_t)s.worstFrame])
		{
			s.worstFrame = (int)i;
		}
	}
	s.mean = sum / (double)ms.size();
	s.p95 = sorted[std::min(sorted.size() - 1, (size_t)((double)sorted.size() * 0.95))];
	s.p99 = sorted[std::min(sorted.size() - 1, (size_t)((double)sorted.size() * 0.99))];
	s.max = sorted.back();
	return s;
}

// one 32-bit value over the per-frame hashes (the order matters): equal chains mean equal hashes in every frame, for a one-line comparison
inline std::uint32_t chain(const std::vector<std::uint32_t> &hashes)
{
	std::uint32_t c = 0x811C9DC5u;
	for (std::uint32_t h : hashes)
	{
		for (int k = 0; k < 4; ++k)
		{
			c = (c ^ ((h >> (8 * k)) & 0xFFu)) * 0x01000193u;
		}
	}
	return c;
}

// OPENBFME_BENCH_HASHES=<file>: every frame's hash, one hex value a line (diff two runs to find the first frame that differs)
inline void writeHashes(const char *scenario, const std::vector<std::uint32_t> &hashes)
{
	const char *path = std::getenv("OPENBFME_BENCH_HASHES");
	if (!path || !*path)
	{
		return;
	}
	const std::string file = std::string(path) + "." + scenario + ".txt";
	if (FILE *f = std::fopen(file.c_str(), "w"))
	{
		for (std::uint32_t h : hashes)
		{
			std::fprintf(f, "%08x\n", (unsigned)h);
		}
		std::fclose(f);
	}
}

inline int envInt(const char *name, int defaultValue)
{
	const char *v = std::getenv(name);
	return v && *v ? std::max(1, std::atoi(v)) : defaultValue;
}
} // namespace perfbench
