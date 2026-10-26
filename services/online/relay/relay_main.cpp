// OpenBFME online relay. GPL-3.0.
//
// openbfme_relay (lane ONLINE-2, docs/ONLINE.md phase O1): one UDP socket, one thread. Read up to 64 datagrams, handle each in RelayCore, then send replies.
// Linux batches with recvmmsg / sendmmsg; macOS uses recvmsg / sendmsg. RelayCore::tick runs once a second.
//
// Usage: openbfme_relay --bind A.B.C.D:PORT --keys FILE [--log-dir DIR] [--idle-s N] [--status FILE] [--stats-s N] [--no-metrics]
//                       [--rate BYTES] [--burst BYTES] [--silent-ms N]
//   --rate / --burst  each player's budget: bytes a second (0: the burst only, never refilled) and the burst
//   --silent-ms N     lane ONLINE-3: a player not heard for N ms is reported silent to the others (STATUS; default 3000)
//   --keys      the ticket key ring ("keyId:hex" lines, RelayProtocol::parseKeyRing); the lobby holds the same file. Never in git.
//   --log-dir   where the command logs go (<game id>.obfmerlog; .part while the game runs); none = no logs
//   --status    a JSON status file rewritten every 10 s (open games, counters): the health check reads it
//   --stats-s   a stats line on stdout every N seconds (journald), default 60; with metrics also a JSON relay_health line
//   --no-metrics lane NETDIAG-1: no passive health collection (RelayHealth), no "health" in the status file, no relay_health line
// SIGTERM / SIGINT: every open game is closed (END reason 3) and the process exits 0.

#include "RelayCore.h"
#include "DatagramBatch.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace
{
volatile std::sig_atomic_t g_stop = 0;
void onSignal(int)
{
	g_stop = 1;
}

std::uint64_t monotonicMs()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (std::uint64_t)ts.tv_sec * 1000u + (std::uint64_t)ts.tv_nsec / 1000000u;
}

bool parseAddress(const std::string &text, RelayAddress &out)
{
	unsigned a, b, c, d, p;
	char tail;
	if (std::sscanf(text.c_str(), "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail) != 5 || a > 255 || b > 255 || c > 255 || d > 255 || p > 65535)
	{
		return false;
	}
	out.ip = (a << 24) | (b << 16) | (c << 8) | d;
	out.port = (std::uint16_t)p;
	return true;
}

// lane NETDIAG-1: the process's resident set size in bytes (0: unknown, reported as null)
std::uint64_t residentBytes()
{
#if defined(__linux__)
	std::ifstream statm("/proc/self/statm");
	std::uint64_t total = 0, resident = 0;
	const long page = sysconf(_SC_PAGESIZE);
	if (page > 0 && statm >> total >> resident)
	{
		return resident * (std::uint64_t)page;
	}
#elif defined(__APPLE__)
	mach_task_basic_info info{};
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
	{
		return info.resident_size;
	}
#endif
	return 0;
}

// `health`: RelayCore::healthJson's snapshot ("" = metrics off: the file is the legacy one)
void writeStatus(const std::string &path, const RelayCore &core, std::uint64_t uptimeMs, const std::string &health)
{
	if (path.empty())
	{
		return;
	}
	const RelayCore::Stats &s = core.stats();
	std::ostringstream o;
	o << "{\"v\":1,\"uptimeMs\":" << uptimeMs << ",\"openGames\":" << core.openGames() << ",\"datagrams\":" << s.datagrams << ",\"bytesIn\":" << s.bytesIn
	  << ",\"forwarded\":" << s.forwarded << ",\"bytesForwarded\":" << s.bytesForwarded << ",\"hellos\":" << s.hellos << ",\"refused\":" << s.refused
	  << ",\"notBound\":" << s.notBound << ",\"bad\":" << s.badDatagrams << ",\"slotMismatches\":" << s.slotMismatches << ",\"rateLimited\":" << s.rateLimited
	  << ",\"gamesOpened\":" << s.gamesOpened << ",\"gamesClosed\":" << s.gamesClosed << ",\"commandsLogged\":" << s.commandsLogged
	  << ",\"logGaps\":" << s.logGaps << ",\"logErrors\":" << s.logErrors << ",\"legacyLogAuthenticated\":" << s.legacyLogAuthenticated << ",\"takeoversRefused\":" << s.takeoversRefused
	  << ",\"invalidPackets\":" << s.invalidPackets << ",\"peakBuffered\":" << s.peakBuffered << ",\"rejoins\":" << s.rejoins << ",\"staleTickets\":"
	  << s.staleTickets << ",\"statusesSent\":" << s.statusesSent << ",\"logChunksSent\":" << s.logChunksSent << ",\"logBytesSent\":" << s.logBytesSent
	  << ",\"logRefused\":" << s.logRefused << ",\"logRateLimited\":" << s.logRateLimited << ",\"logStreamPackets\":" << s.logStreamPackets << ",\"spectatorsBound\":" << s.spectatorsBound
	  << ",\"spectatorDataRefused\":" << s.spectatorDataRefused << ",\"spectatorsDropped\":" << s.spectatorsDropped << ",\"spectatorLogBytes\":"
	  << s.spectatorLogBytes << ",\"accountingErrors\":" << s.accountingErrors << ",";
	if (!health.empty())
	{
		o << "\"health\":" << health << ","; // lane NETDIAG-1 (before authDropped: the readers match the file's last key with its closing brace)
	}
	o << "\"authDropped\":" << s.authDropped << "}\n";
	const std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		f << o.str();
	}
	std::rename(tmp.c_str(), path.c_str());
}
} // namespace

int main(int argc, char **argv)
{
	std::string bind = "0.0.0.0:7350", keysPath, statusPath;
	RelayCore::Options options;
	int statsSeconds = 60;
	for (int i = 1; i < argc; ++i)
	{
		const std::string k = argv[i];
		const bool has = i + 1 < argc;
		if (k == "--bind" && has) bind = argv[++i];
		else if (k == "--keys" && has) keysPath = argv[++i];
		else if (k == "--log-dir" && has) options.logDir = argv[++i];
		else if (k == "--idle-s" && has) options.idleMs = (std::uint64_t)std::atoll(argv[++i]) * 1000u;
		else if (k == "--status" && has) statusPath = argv[++i];
		else if (k == "--stats-s" && has) statsSeconds = std::atoi(argv[++i]);
		else if (k == "--no-metrics") options.metrics = false; // lane NETDIAG-1
		else if (k == "--rate" && has) options.bytesPerSecond = (std::uint32_t)std::atol(argv[++i]);
		else if (k == "--burst" && has) options.burstBytes = (std::uint32_t)std::atol(argv[++i]);
		else if (k == "--silent-ms" && has) options.silentMs = (std::uint64_t)std::atoll(argv[++i]); // lane ONLINE-3: STATUS's silence
		else
		{
			std::fprintf(stderr, "openbfme_relay: unknown or incomplete argument %s\n", k.c_str());
			return 1;
		}
	}
	RelayAddress local;
	if (!parseAddress(bind, local))
	{
		std::fprintf(stderr, "openbfme_relay: bad --bind %s (wants a.b.c.d:port)\n", bind.c_str());
		return 1;
	}
	if (keysPath.empty())
	{
		std::fprintf(stderr, "openbfme_relay: --keys FILE is required (the ticket key ring)\n");
		return 1;
	}
	{
		std::ifstream f(keysPath, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		std::string error;
		if (!f || !RelayProtocol::parseKeyRing(ss.str(), options.keys, &error))
		{
			std::fprintf(stderr, "openbfme_relay: %s: %s\n", keysPath.c_str(), f ? error.c_str() : "cannot read it");
			return 1;
		}
	}
	const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0)
	{
		std::perror("openbfme_relay: socket");
		return 1;
	}
	int buf = 8 << 20;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
	sockaddr_in sa{};
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(local.ip);
	sa.sin_port = htons(local.port);
	if (::bind(fd, (sockaddr *)&sa, sizeof sa) != 0)
	{
		std::fprintf(stderr, "openbfme_relay: cannot bind %s: %s\n", bind.c_str(), std::strerror(errno));
		return 1;
	}
	std::signal(SIGTERM, onSignal);
	std::signal(SIGINT, onSignal);
	std::printf("openbfme_relay: listening on %s, %zu key(s), logs %s\n", bind.c_str(), options.keys.size(), options.logDir.empty() ? "off" : options.logDir.c_str());
	std::fflush(stdout);

	RelayCore core(options);
	constexpr int kBatch = 64;
	constexpr size_t kMax = 2048; // a transport datagram is at most packetBytes (1,200) + the 5-byte envelope; bigger ones are truncated and dropped
	std::vector<std::uint8_t> in((size_t)kBatch * kMax);
	relayio::Message msgs[kBatch];
	iovec iovs[kBatch];
	sockaddr_in from[kBatch];
	// what goes out: forwards point into `in`, replies into `replies`
	relayio::Message outMsgs[kBatch];
	iovec outIovs[kBatch];
	sockaddr_in outTo[kBatch];
	std::vector<std::vector<std::uint8_t>> replies(kBatch);
	const std::uint64_t started = monotonicMs();
	std::uint64_t lastTick = started, lastStatus = 0, lastStats = started;
	while (!g_stop)
	{
		pollfd p{ fd, POLLIN, 0 };
		const int ready = poll(&p, 1, 200);
		const std::uint64_t now = monotonicMs();
		if (ready > 0)
		{
			for (int i = 0; i < kBatch; ++i)
			{
				iovs[i].iov_base = in.data() + (size_t)i * kMax;
				iovs[i].iov_len = kMax;
				msgs[i].msg_hdr = msghdr{};
				msgs[i].msg_hdr.msg_iov = &iovs[i];
				msgs[i].msg_hdr.msg_iovlen = 1;
				msgs[i].msg_hdr.msg_name = &from[i];
				msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
			}
			const int n = relayio::receiveBatch(fd, msgs, kBatch);
			const std::uint32_t unixNow = (std::uint32_t)std::time(nullptr);
			int out = 0;
			for (int i = 0; i < n; ++i)
			{
				if (msgs[i].msg_hdr.msg_flags & MSG_TRUNC)
				{
					core.noteTruncated(); // lane NETDIAG-1: counted (invalidDatagrams), never handled
					continue; // beyond any transport datagram
				}
				RelayAddress a;
				a.ip = ntohl(from[i].sin_addr.s_addr);
				a.port = ntohs(from[i].sin_port);
				std::uint8_t *d = in.data() + (size_t)i * kMax;
				RelayCore::Action act = core.handle(a, d, msgs[i].msg_len, now, unixNow);
				// lane NET-SYNC: canonical emits past the first reply (ACKs, retries, record fanout)
				{
					std::vector<RelayCore::Action> pending;
					core.takePending(pending);
					for (const RelayCore::Action &p : pending)
					{
						if (p.kind != RelayCore::Action::NONE)
						{
							sockaddr_in t{};
							t.sin_family = AF_INET;
							t.sin_addr.s_addr = htonl(p.to.ip);
							t.sin_port = htons(p.to.port);
							const bool ok = sendto(fd, p.reply.data(), p.reply.size(), 0, (sockaddr *)&t, sizeof t) == (ssize_t)p.reply.size();
							core.noteSend(ok ? 1 : 0, ok ? 0 : 1);
						}
					}
				}
				if (act.kind == RelayCore::Action::NONE)
				{
					continue;
				}
				if (act.kind == RelayCore::Action::FORWARD)
				{
					outIovs[out].iov_base = d;
					outIovs[out].iov_len = msgs[i].msg_len;
				}
				else
				{
					replies[(size_t)out] = std::move(act.reply);
					outIovs[out].iov_base = replies[(size_t)out].data();
					outIovs[out].iov_len = replies[(size_t)out].size();
				}
				outTo[out] = sockaddr_in{};
				outTo[out].sin_family = AF_INET;
				outTo[out].sin_addr.s_addr = htonl(act.to.ip);
				outTo[out].sin_port = htons(act.to.port);
				outMsgs[out].msg_hdr = msghdr{};
				outMsgs[out].msg_hdr.msg_iov = &outIovs[out];
				outMsgs[out].msg_hdr.msg_iovlen = 1;
				outMsgs[out].msg_hdr.msg_name = &outTo[out];
				outMsgs[out].msg_hdr.msg_namelen = sizeof(sockaddr_in);
				++out;
			}
			for (int sent = 0; sent < out;)
			{
				const int r = relayio::sendBatch(fd, outMsgs + sent, (unsigned)(out - sent));
				if (r <= 0)
				{
					core.noteSend(0, (size_t)(out - sent));
					break; // a full send buffer drops the rest: the transports resend
				}
				sent += r;
				core.noteSend((size_t)r, 0);
			}
		}
		if (now - lastTick >= 1000)
		{
			lastTick = now;
			std::vector<RelayCore::Action> statuses; // lane ONLINE-3: the games' STATUS datagrams (one per bound slot, small: sent one by one)
			core.tick(now, &statuses);
			for (const RelayCore::Action &a : statuses)
			{
				sockaddr_in to{};
				to.sin_family = AF_INET;
				to.sin_addr.s_addr = htonl(a.to.ip);
				to.sin_port = htons(a.to.port);
				const bool ok = sendto(fd, a.reply.data(), a.reply.size(), 0, (const sockaddr *)&to, sizeof to) == (ssize_t)a.reply.size();
				core.noteSend(ok ? 1 : 0, ok ? 0 : 1);
			}
			for (const std::string &e : core.takeErrors())
			{
				std::fprintf(stderr, "openbfme_relay: %s\n", e.c_str());
			}
		}
		// lane NETDIAG-1: one health snapshot serves the status file and the stats line due now (its rates span the time since the last one)
		const bool statusDue = now - lastStatus >= 10000;
		const bool statsDue = statsSeconds > 0 && now - lastStats >= (std::uint64_t)statsSeconds * 1000u;
		std::string health;
		if (options.metrics && ((statusDue && !statusPath.empty()) || statsDue))
		{
			health = core.healthJson(now - started, residentBytes());
		}
		if (statusDue)
		{
			lastStatus = now;
			writeStatus(statusPath, core, now - started, health);
		}
		if (statsDue)
		{
			lastStats = now;
			const RelayCore::Stats &s = core.stats();
			std::printf("openbfme_relay: games %zu open %llu closed, %llu datagrams in, %llu forwarded, %llu refused, %llu rate-limited, %llu commands logged\n",
				core.openGames(), s.gamesClosed, s.datagrams, s.forwarded, s.refused, s.rateLimited, s.commandsLogged);
			if (!health.empty())
			{
				std::printf("%s\n", health.c_str());
			}
			std::fflush(stdout);
		}
	}
	core.shutdown(monotonicMs());
	const std::uint64_t uptime = monotonicMs() - started;
	writeStatus(statusPath, core, uptime, options.metrics ? core.healthJson(uptime, residentBytes()) : std::string());
	std::printf("openbfme_relay: stopped, %zu log(s) finished\n", core.closedLogs().size());
	close(fd);
	return 0;
}
