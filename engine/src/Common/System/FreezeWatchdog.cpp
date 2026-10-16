// OpenBFME. GPL-3.0.
// See Common/FreezeWatchdog.h (lane FREEZE-1).

#include "Common/FreezeWatchdog.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace
{
long long steadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string seconds(long long ms)
{
	char text[32];
	std::snprintf(text, sizeof text, "%.1f s", (double)ms / 1000.0);
	return text;
}
} // namespace

FreezeWatchdog::FreezeWatchdog(Config config, Write write, Capture capture, Context context, Clock clock)
	: m_config(config)
	, m_write(std::move(write))
	, m_capture(std::move(capture))
	, m_context(std::move(context))
	, m_clock(clock ? std::move(clock) : Clock(steadyMs))
{
}

FreezeWatchdog::~FreezeWatchdog()
{
	stop();
}

void FreezeWatchdog::start()
{
	if (m_thread.joinable())
		return;
	m_stop = false;
	m_thread = std::thread([this] {
		std::unique_lock<std::mutex> lock(m_wakeLock);
		while (!m_stop.load())
		{
			m_wake.wait_for(lock, std::chrono::milliseconds(m_config.pollMs), [this] { return m_stop.load(); });
			if (m_stop.load())
				break;
			lock.unlock();
			check();
			lock.lock();
		}
	});
}

void FreezeWatchdog::stop()
{
	{
		std::lock_guard<std::mutex> lock(m_wakeLock);
		m_stop = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

void FreezeWatchdog::beat()
{
	const long long now = m_clock();
	std::string line;
	{
		std::lock_guard<std::mutex> lock(m_lock);
		++m_frames;
		const long long took = m_lastBeat ? now - m_lastBeat : 0;
		if (m_frozen)
		{
			line = "FREEZE over: the main thread was stalled for " + seconds(now - m_lastBeat) + " (frame " + std::to_string(m_frames) + " finished)";
			m_frozen = false;
		}
		else if (m_lastBeat && took >= m_config.hitchMs && !m_background)
		{
			++m_stats.hitches;
			if (now - m_lastHitchLine >= m_config.hitchQuietMs)
			{
				line = "HITCH frame " + std::to_string(m_frames) + " took " + std::to_string(took) + " ms";
				if (m_quietHitches)
					line += " (+" + std::to_string(m_quietHitches) + " more since the last HITCH line)";
				m_quietHitches = 0;
				m_lastHitchLine = now;
			}
			else
				++m_quietHitches;
		}
		m_lastBeat = now;
	}
	if (!line.empty())
		m_write(line);
}

void FreezeWatchdog::setBackground(bool background)
{
	std::lock_guard<std::mutex> lock(m_lock);
	m_background = background;
}

void FreezeWatchdog::check()
{
	const long long now = m_clock();
	enum class Act { none, freeze, still } act = Act::none;
	bool captureNow = false;
	unsigned captureIndex = 0;
	long long stalled = 0;
	unsigned long long frames = 0;
	bool background = false;
	{
		std::lock_guard<std::mutex> lock(m_lock);
		if (!m_lastBeat)
			return; // no frame yet: start-up is not a freeze
		stalled = now - m_lastBeat;
		frames = m_frames;
		background = m_background;
		const int threshold = m_background ? m_config.backgroundStallMs : m_config.stallMs;
		if (!m_frozen)
		{
			if (stalled < threshold)
				return;
			m_frozen = true;
			m_frozenSince = m_lastBeat;
			m_nextStill = now + m_config.stillFrozenEveryMs;
			m_secondCaptured = false;
			++m_stats.freezes;
			act = Act::freeze;
			captureNow = !m_background;
		}
		else
		{
			if (!m_secondCaptured && !m_background && stalled >= m_config.secondCaptureMs)
			{
				m_secondCaptured = true;
				captureNow = true;
				act = Act::still;
				m_nextStill = now + m_config.stillFrozenEveryMs;
			}
			else if (now >= m_nextStill)
			{
				m_nextStill = now + m_config.stillFrozenEveryMs;
				act = Act::still;
			}
		}
		if (captureNow)
			captureIndex = ++m_stats.captures;
	}
	if (act == Act::freeze)
	{
		std::string line = "FREEZE the main thread has not finished a frame for " + seconds(stalled) + " (after frame " + std::to_string(frames) + ")";
		if (background)
			line += ", window in the background";
		const std::string last = m_context ? m_context() : std::string();
		if (!last.empty())
			line += "; last log line before it: " + last;
		m_write(line);
	}
	else if (act == Act::still)
		m_write("FREEZE still stalled after " + seconds(stalled));
	if (captureNow && m_capture)
		m_write("FREEZE thread stacks: " + m_capture(captureIndex));
}

FreezeWatchdog::Stats FreezeWatchdog::stats() const
{
	std::lock_guard<std::mutex> lock(m_lock);
	return m_stats;
}

namespace ThreadStacks
{

std::string capture(const std::string &path, const std::function<std::string(const std::string &)> &redact, int timeoutMs)
{
#ifdef _WIN32
	(void)path;
	(void)redact;
	(void)timeoutMs;
	return "not available on Windows";
#else
	const std::string pid = std::to_string((long long)::getpid());
	std::vector<std::string> args;
	bool toStdout = false; // the tool prints the stacks (Linux) instead of writing the file itself (macOS sample -file)
#ifdef __APPLE__
	if (::access("/usr/bin/sample", X_OK) == 0)
		args = { "/usr/bin/sample", pid, "2", "-mayDie", "-file", path };
#else
	for (const char *gdb : { "/usr/bin/gdb", "/usr/local/bin/gdb" })
		if (args.empty() && ::access(gdb, X_OK) == 0)
			args = { gdb, "-p", pid, "-batch", "-nx", "-ex", "thread apply all bt" };
	if (args.empty() && ::access("/usr/bin/eu-stack", X_OK) == 0)
		args = { "/usr/bin/eu-stack", "-p", pid };
	toStdout = true;
#endif
	if (args.empty())
		return "no stack tool on this system (macOS: /usr/bin/sample; Linux: gdb or eu-stack)";
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	// the child must not write into this process's console pipes (they may be what is stuck)
	posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
	if (toStdout)
		posix_spawn_file_actions_addopen(&actions, 1, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	else
		posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
	std::vector<char *> argv;
	for (std::string &a : args)
		argv.push_back(a.data());
	argv.push_back(nullptr);
	pid_t child = -1;
	const int spawned = ::posix_spawn(&child, argv[0], &actions, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	if (spawned != 0)
		return std::string("could not start ") + args[0] + ": " + std::strerror(spawned);
	const long long end = steadyMs() + timeoutMs;
	int status = 0;
	pid_t done = 0;
	while ((done = ::waitpid(child, &status, WNOHANG)) == 0 && steadyMs() < end)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	if (done == 0)
	{
		::kill(child, SIGKILL);
		::waitpid(child, &status, 0);
		return args[0] + " did not finish in " + seconds(timeoutMs);
	}
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return args[0] + " wrote no file";
	std::ostringstream text;
	text << in.rdbuf();
	in.close();
	const std::string clean = redact ? redact(text.str()) : text.str();
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << clean;
	size_t lines = 0;
	for (char c : clean)
		lines += c == '\n' ? 1 : 0;
	const size_t slash = path.find_last_of("/\\");
	return std::to_string(lines) + " lines in " + (slash == std::string::npos ? path : path.substr(slash + 1));
#endif
}

} // namespace ThreadStacks
