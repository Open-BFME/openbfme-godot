// OpenBFME. GPL-3.0. See GodotDevice/GodotTiming.h.
#include "GodotDevice/GodotTiming.h"

#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace timing
{
double nowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
double elapsedMs(double sinceMs) { return nowMs() - sinceMs; }
double elapsedSeconds(double sinceMs) { return (nowMs() - sinceMs) / 1000.0; }
double spanMs(double laterMs, double earlierMs) { return laterMs - earlierMs; }
double sumMs(double a, double b) { return a + b; }
double threadCpuMs()
{
#ifdef _WIN32
	FILETIME created, exited, kernel, user;
	if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
	{
		return 0.0;
	}
	const auto ticks = [](const FILETIME &f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };
	return (double)(ticks(kernel) + ticks(user)) / 10000.0; // 100 ns units
#else
	timespec ts{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
}
} // namespace timing
