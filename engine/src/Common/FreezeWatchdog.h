// OpenBFME. GPL-3.0.
//
// Lane FREEZE-1: tells a frozen game from a slow one, and writes what it was doing into the session log while it is still frozen.
// The main thread beats once per frame (scripts/release/release.gd's _process); a thread of its own checks the beats:
//   * a frame longer than hitchMs (and shorter than a freeze) is a HITCH line, at most one per hitchQuietMs (the rest counted);
//   * no frame for stallMs is a FREEZE: a line with the last frame and the last log line before it, then every thread's stack (capture()),
//     a second capture at secondCaptureMs, a "still stalled" line every stillFrozenEveryMs, and a "FREEZE over" line once frames come again.
// With the window in the background (setBackground) the threshold is backgroundStallMs and no stacks are captured: a hidden window may be
// throttled by the system. The lines go through write() only (the session log's file, never stdout: a blocked console was FREEZE-1's cause).
// No Godot dependency: the clock, the sink and the capture are injected (engine/tests/test_freeze1_watchdog.cpp drives check() itself).

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class FreezeWatchdog
{
public:
	struct Config
	{
		int stallMs = 5000;
		int backgroundStallMs = 30000;
		int hitchMs = 1000;
		int hitchQuietMs = 5000;
		int secondCaptureMs = 30000;
		int stillFrozenEveryMs = 30000;
		int pollMs = 250;
	};
	struct Stats
	{
		unsigned freezes = 0, hitches = 0, captures = 0;
	};
	using Write = std::function<void(const std::string &line)>;
	// captures every thread's stack for the n-th capture (1-based); returns one line: where it went, or why there is none
	using Capture = std::function<std::string(unsigned n)>;
	using Context = std::function<std::string()>; // the last log line before the stall ("" = none)
	using Clock = std::function<long long()>;     // milliseconds, monotonic

	FreezeWatchdog(Config config, Write write, Capture capture, Context context = nullptr, Clock clock = nullptr);
	~FreezeWatchdog();
	FreezeWatchdog(const FreezeWatchdog &) = delete;
	FreezeWatchdog &operator=(const FreezeWatchdog &) = delete;

	void start(); // the checking thread (check() every pollMs)
	void stop();
	void beat();  // the main thread, once per frame
	void setBackground(bool background);
	void check(); // one step of the checking thread
	Stats stats() const;

private:
	Config m_config;
	Write m_write;
	Capture m_capture;
	Context m_context;
	Clock m_clock;
	mutable std::mutex m_lock; // never held while writing or capturing (the main thread's beat must not wait for a capture)
	long long m_lastBeat = 0;  // 0 = no frame yet
	unsigned long long m_frames = 0;
	bool m_background = false;
	bool m_frozen = false;
	long long m_frozenSince = 0, m_nextStill = 0;
	bool m_secondCaptured = false;
	long long m_lastHitchLine = -1000000000;
	unsigned m_quietHitches = 0;
	Stats m_stats;
	std::thread m_thread;
	std::mutex m_wakeLock;
	std::condition_variable m_wake;
	std::atomic<bool> m_stop{ false };
};

namespace ThreadStacks
{
// Writes every thread's stack of this process to `path` (macOS: /usr/bin/sample; Linux: gdb, else eu-stack; Windows: not available),
// waits at most `timeoutMs` for the tool, then passes the file through `redact` (the home folder and the user name, as the log does).
// Returns one line: "<n> lines in <file name>" or why there is none.
std::string capture(const std::string &path, const std::function<std::string(const std::string &)> &redact, int timeoutMs = 20000);
} // namespace ThreadStacks
