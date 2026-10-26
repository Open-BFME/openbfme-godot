// OpenBFME. GPL-3.0.
// See Common/ConsoleFilter.h (lane RELEASE-1).

#include "Common/ConsoleFilter.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
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
	int stopRead = -1;  // lane ONEZIP-1: uninstall's bounded stop (a child process may hold the write end through its inherited stdout)
	int stopWrite = -1;
	std::atomic<bool> done{ false };
	std::thread reader;
	std::mutex pendingLock; // the reader's partial line; the crash path takes it too
	std::string pending;
	bool dropping = false;

	void reset()
	{
		fd = original = target = readEnd = writeEnd = stopRead = stopWrite = -1;
		done = false;
		pending.clear();
		dropping = false;
	}
};

// An overlong line is discarded until its newline. Splitting it could expose an unlabelled credential suffix.
size_t g_maxLine = LogPrivacy::kMaxFieldBytes;

std::mutex g_lock; // install / uninstall
Stream g_streams[2];
LogPrivacy::Rules g_rules;
std::atomic<bool> g_installed{ false };
std::atomic<bool> g_crashed{ false };

// lane ONEZIP-1 r3 (Sol r2): the shutdown has ONE deadline for everything (both streams' EOF wait, their draining and their writes);
// 0 while none is set
std::atomic<long long> g_deadlineMs{ 0 };

long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Writes `text` to `fd`. With `stopFd` (the reader's stop pipe) a write never blocks past the shutdown's deadline: each piece of at most
// PIPE_BUF bytes is written only once poll() says the sink takes it, and once the stop came, poll() waits no longer than the deadline
// (Sol r2: a blocked console kept uninstall from returning). What is left at the deadline is dropped.
void writeAll(int fd, const std::string &text, int stopFd = -1, long long localDeadline = 0)
{
	size_t done = 0;
	bool stopped = false;
	while (done < text.size())
	{
		if (stopFd >= 0 || localDeadline != 0)
		{
			const long long shutdown = g_deadlineMs.load();
			const long long deadline = localDeadline == 0 ? shutdown : (shutdown == 0 ? localDeadline : std::min(localDeadline, shutdown));
			int timeout = -1;
			if (deadline != 0)
			{
				const long long left = deadline - nowMs();
				if (left <= 0)
					return;
				timeout = (int)left;
			}
			pollfd fds[2] = { { fd, POLLOUT, 0 }, { stopFd, POLLIN, 0 } };
			const int r = ::poll(fds, stopped || stopFd < 0 ? 1 : 2, timeout);
			if (r < 0 && errno == EINTR)
				continue;
			if (r <= 0)
				return; // an error, or the deadline: the rest is dropped
			if (stopFd >= 0 && !stopped && (fds[1].revents & (POLLIN | POLLHUP)) != 0)
				stopped = true; // from now on bounded by the deadline (uninstall sets it before the stop)
			if ((fds[0].revents & (POLLOUT | POLLERR | POLLHUP)) == 0)
				continue;
		}
		const size_t piece = stopFd >= 0 || localDeadline != 0 ? std::min<size_t>(text.size() - done, PIPE_BUF) : text.size() - done;
		const ssize_t n = ::write(fd, text.data() + done, piece);
		if (n < 0 && errno == EINTR)
			continue;
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
	bool stopping = false;
	for (;;)
	{
		if (!stopping)
		{
			// lane ONEZIP-1: wait for text or for uninstall's stop. A child process started by the game holds the pipe's write end through its
			// inherited stdout, so the pipe's EOF can come long after uninstall (Sol r1 of ONEZIP-1: the game hung at exit until the child ended)
			pollfd fds[2] = { { s->readEnd, POLLIN, 0 }, { s->stopRead, POLLIN, 0 } };
			if (::poll(fds, 2, -1) < 0)
			{
				if (errno == EINTR)
					continue;
				break;
			}
			if ((fds[1].revents & (POLLIN | POLLHUP)) != 0)
			{
				// stop: take what the pipe holds now without waiting for its writers, then end
				stopping = true;
				::fcntl(s->readEnd, F_SETFL, ::fcntl(s->readEnd, F_GETFL) | O_NONBLOCK);
			}
		}
		else if (g_deadlineMs.load() != 0 && nowMs() >= g_deadlineMs.load())
			break; // a child that keeps writing does not keep the drain going past the deadline
		const ssize_t n = ::read(s->readEnd, buffer, sizeof(buffer));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break; // EOF (uninstall closed the write end and no child holds it), or nothing more after a stop
		std::lock_guard<std::mutex> lock(s->pendingLock);
		if (g_crashed.load())
			continue; // the crash path writes directly
		for (ssize_t i = 0; i < n; ++i)
		{
			if (buffer[i] == '\n')
			{
				if (!s->dropping)
					writeAll(s->target, LogPrivacy::redact(s->pending, g_rules) + "\n", s->stopRead);
				s->pending.clear();
				s->dropping = false;
			}
			else if (!s->dropping)
			{
				s->pending += buffer[i];
				if (s->pending.size() > g_maxLine)
				{
					s->pending.clear();
					s->dropping = true;
					writeAll(s->target, std::string(LogPrivacy::kOversized) + "\n", s->stopRead);
				}
			}
		}
	}
	std::lock_guard<std::mutex> lock(s->pendingLock);
	if (!s->pending.empty() && !g_crashed.load())
		writeAll(s->target, LogPrivacy::redactFinal(s->pending, g_rules), s->stopRead); // the last partial line, at exit
	s->pending.clear();
	s->done = true;
}

constexpr int kFlushMs = 400; // of the 1000 ms: buffered text still goes to the filter while its reader is draining

// Flushes `s`'s stdio stream into the filter's pipe without ever blocking: the pipe's write end (shared by fd 1 / 2) is made
// non-blocking, and a flush that finds the pipe full (the reader waits for a console that takes nothing) is retried only until
// `deadline`; what is still buffered then stays unwritten.
void flushBounded(Stream &s, long long deadline)
{
	FILE *f = s.fd == 1 ? stdout : stderr;
	::fcntl(s.writeEnd, F_SETFL, ::fcntl(s.writeEnd, F_GETFL) | O_NONBLOCK);
	for (;;)
	{
		errno = 0;
		if (std::fflush(f) == 0)
			return;
		std::clearerr(f);
		const long long left = deadline - nowMs();
		if (errno != EAGAIN || left <= 0)
			return;
		pollfd p = { s.writeEnd, POLLOUT, 0 };
		::poll(&p, 1, (int)std::min<long long>(left, 20));
	}
}

// uninstall waits at most kEofWaitMs for both pipes' EOF, then stops both readers; everything ends by kShutdownMs after it began
constexpr int kEofWaitMs = 500;
constexpr int kShutdownMs = 1000;
} // namespace

bool install(const LogPrivacy::Rules &rules, std::string *error, int outFd, int errFd, size_t maxLine)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_installed.load())
		return true;
	g_rules = rules;
	g_maxLine = std::min(maxLine, LogPrivacy::kMaxFieldBytes);
	g_crashed = false;
	std::fflush(stdout);
	std::fflush(stderr);
	const int targets[2] = { outFd, errFd };
	for (int i = 0; i < 2; ++i)
	{
		Stream &s = g_streams[i];
		s.fd = i + 1;
		int fds[2];
		int stop[2] = { -1, -1 };
		// lane ONEZIP-1 (Sol r1): the saved console is close-on-exec, so no child process keeps the real stdout / stderr (e.g. a test's
		// capture pipe) open after this process ended
		s.original = ::fcntl(s.fd, F_DUPFD_CLOEXEC, 0);
		bool piped = s.original >= 0 && ::pipe(fds) == 0;
		if (piped && ::pipe(stop) != 0)
		{
			::close(fds[0]);
			::close(fds[1]);
			piped = false;
		}
		if (!piped)
		{
			if (error)
				*error = "console filter: dup / pipe failed";
			for (int k = 0; k <= i; ++k)
			{
				if (g_streams[k].writeEnd >= 0)
				{
					::dup2(g_streams[k].original, g_streams[k].fd);
					::close(g_streams[k].writeEnd);
					const char stopByte = 1;
					(void)!::write(g_streams[k].stopWrite, &stopByte, 1); // the reader stops at once
					::close(g_streams[k].stopWrite);
					g_streams[k].reader.join();
					::close(g_streams[k].readEnd);
					::close(g_streams[k].stopRead);
				}
				if (g_streams[k].original >= 0)
					::close(g_streams[k].original);
				g_streams[k].reset();
			}
			return false;
		}
		::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
		::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
		::fcntl(stop[0], F_SETFD, FD_CLOEXEC);
		::fcntl(stop[1], F_SETFD, FD_CLOEXEC);
		s.readEnd = fds[0];
		s.writeEnd = fds[1];
		s.stopRead = stop[0];
		s.stopWrite = stop[1];
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
	// lane ONEZIP-1 r3 (Sol r2, r3): one deadline for the whole shutdown, both streams together, the flush of the buffered stdio
	// included (a child that still holds a stream, or a console that takes nothing, must not keep this process from ending)
	const long long t0 = nowMs();
	for (Stream &s : g_streams)
		flushBounded(s, t0 + kFlushMs);
	for (Stream &s : g_streams)
	{
		::dup2(s.original, s.fd); // the pipe's last writer is now our own write end, unless a child process inherited the stream
		::close(s.writeEnd);      // EOF for the reader once it has written the rest
	}
	while ((!g_streams[0].done.load() || !g_streams[1].done.load()) && nowMs() - t0 < kEofWaitMs)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	g_deadlineMs = t0 + kShutdownMs;
	for (Stream &s : g_streams)
	{
		// the stop: one byte on the stop pipe (not only its close: a forked child that did not exec still holds the stop pipe's write end)
		const char stopByte = 1;
		while (::write(s.stopWrite, &stopByte, 1) < 0 && errno == EINTR)
		{
		}
		::close(s.stopWrite);     // the reader writes what the pipe holds now, until the deadline, and ends
	}
	for (Stream &s : g_streams)
	{
		s.reader.join();
		::close(s.readEnd);
		::close(s.stopRead);
		::close(s.original);
		s.reset();
	}
	g_deadlineMs = 0;
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
	const long long deadline = nowMs() + 500;
	// the partial lines the readers still hold, scrubbed, then the crash text; a reader that holds its lock (it may be the crashed thread)
	// is waited for at most 100 ms
	for (Stream &s : g_streams)
	{
		std::unique_lock<std::mutex> lock(s.pendingLock, std::defer_lock);
		for (int i = 0; i < 100 && !lock.try_lock(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (lock.owns_lock() && !s.pending.empty())
		{
			writeAll(s.target, LogPrivacy::redactFinal(s.pending, g_rules) + "\n", -1, deadline);
			s.pending.clear();
		}
	}
	std::string line = LogPrivacy::redact(text, g_rules);
	if (line.empty() || line.back() != '\n')
		line += '\n';
	writeAll(g_streams[1].target, line, -1, deadline);
}

#endif

} // namespace ConsoleFilter
