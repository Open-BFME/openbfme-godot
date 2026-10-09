// OpenBFME. GPL-3.0.
// See Common/ConsoleFilter.h (lane RELEASE-1).

#include "Common/ConsoleFilter.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ConsoleFilter
{

#ifdef _WIN32

bool install(const LogPrivacy::Rules &, std::string *error, int, int, size_t)
{
	if (error)
		*error = "the console filter is not available on Windows (the release executable has no console)";
	return false;
}
void uninstall() {}
bool installed() { return false; }
void crashWrite(const std::string &) {}

#else

namespace
{
struct Stream
{
	int fd = -1;        // 1 or 2
	int original = -1;  // dup of the original descriptor (restored on uninstall)
	int target = -1;    // where redacted text goes (original unless a test gave another)
	int readEnd = -1;
	int writeEnd = -1;
	std::thread reader;
	std::mutex pendingLock; // the reader's partial line; the crash path takes it too
	std::string pending;

	void reset()
	{
		fd = original = target = readEnd = writeEnd = -1;
		pending.clear();
	}
};

// a line longer than this without a newline is written in part (a stream that never ends its line must not grow without bound): up to
// LogPrivacy::safeCut, scrubbed, the rest kept (review r4: the whole buffer was flushed, cutting through a split path)
size_t g_maxLine = 8u << 20;

std::mutex g_lock; // install / uninstall
Stream g_streams[2];
LogPrivacy::Rules g_rules;
std::atomic<bool> g_installed{ false };
std::atomic<bool> g_crashed{ false };

void writeAll(int fd, const std::string &text)
{
	size_t done = 0;
	while (done < text.size())
	{
		const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
		if (n <= 0)
			return;
		done += (size_t)n;
	}
}

// Strictly line-buffered (review r3: a time-based flush of a partial line leaked a path written in two parts): nothing reaches the console
// before its line is complete and scrubbed as a whole. A partial line waits for its newline, for uninstall() or for the crash path.
void readLoop(Stream *s)
{
	char buffer[4096];
	for (;;)
	{
		const ssize_t n = ::read(s->readEnd, buffer, sizeof(buffer));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break; // EOF: uninstall closed the write end
		std::lock_guard<std::mutex> lock(s->pendingLock);
		if (g_crashed.load())
			continue; // the crash path writes directly
		s->pending.append(buffer, (size_t)n);
		const size_t lastNewline = s->pending.rfind('\n');
		if (lastNewline != std::string::npos)
		{
			writeAll(s->target, LogPrivacy::redact(s->pending.substr(0, lastNewline + 1), g_rules));
			s->pending.erase(0, lastNewline + 1);
		}
		else if (s->pending.size() > g_maxLine)
		{
			size_t cut = LogPrivacy::safeCut(s->pending, g_rules);
			if (cut == 0)
				cut = s->pending.size() / 2; // cannot happen with names shorter than half a line; never an endless hold
			cut = LogPrivacy::safeCut(s->pending.substr(0, cut), g_rules);
			writeAll(s->target, LogPrivacy::redact(s->pending.substr(0, cut), g_rules));
			s->pending.erase(0, cut);
		}
	}
	std::lock_guard<std::mutex> lock(s->pendingLock);
	if (!s->pending.empty() && !g_crashed.load())
		writeAll(s->target, LogPrivacy::redactFinal(s->pending, g_rules)); // the last partial line, at exit
	s->pending.clear();
}
} // namespace

bool install(const LogPrivacy::Rules &rules, std::string *error, int outFd, int errFd, size_t maxLine)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_installed.load())
		return true;
	g_rules = rules;
	g_maxLine = maxLine;
	g_crashed = false;
	std::fflush(stdout);
	std::fflush(stderr);
	const int targets[2] = { outFd, errFd };
	for (int i = 0; i < 2; ++i)
	{
		Stream &s = g_streams[i];
		s.fd = i + 1;
		int fds[2];
		s.original = ::dup(s.fd);
		if (s.original < 0 || ::pipe(fds) != 0)
		{
			if (error)
				*error = "console filter: dup / pipe failed";
			for (int k = 0; k <= i; ++k)
			{
				if (g_streams[k].writeEnd >= 0)
				{
					::dup2(g_streams[k].original, g_streams[k].fd);
					::close(g_streams[k].writeEnd);
					g_streams[k].reader.join();
					::close(g_streams[k].readEnd);
				}
				if (g_streams[k].original >= 0)
					::close(g_streams[k].original);
				g_streams[k].reset();
			}
			return false;
		}
		::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
		::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
		s.readEnd = fds[0];
		s.writeEnd = fds[1];
		s.target = targets[i] >= 0 ? targets[i] : s.original;
		s.reader = std::thread(readLoop, &s);
		::dup2(s.writeEnd, s.fd);
	}
	g_installed = true;
	return true;
}

void uninstall()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_installed.load())
		return;
	std::fflush(stdout);
	std::fflush(stderr);
	for (Stream &s : g_streams)
	{
		::dup2(s.original, s.fd); // the pipe's last writer is now our own write end
		::close(s.writeEnd);      // EOF for the reader once it has written the rest
		s.reader.join();
		::close(s.readEnd);
		::close(s.original);
		s.reset();
	}
	g_installed = false;
}

bool installed()
{
	return g_installed.load();
}

void crashWrite(const std::string &text)
{
	if (!g_installed.load())
		return;
	g_crashed = true;
	// the partial lines the readers still hold, scrubbed, then the crash text; a reader that holds its lock (it may be the crashed thread)
	// is waited for at most 100 ms
	for (Stream &s : g_streams)
	{
		std::unique_lock<std::mutex> lock(s.pendingLock, std::defer_lock);
		for (int i = 0; i < 100 && !lock.try_lock(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (lock.owns_lock() && !s.pending.empty())
		{
			writeAll(s.target, LogPrivacy::redactFinal(s.pending, g_rules) + "\n");
			s.pending.clear();
		}
	}
	std::string line = LogPrivacy::redact(text, g_rules);
	if (line.empty() || line.back() != '\n')
		line += '\n';
	writeAll(g_streams[1].target, line);
}

#endif

} // namespace ConsoleFilter
