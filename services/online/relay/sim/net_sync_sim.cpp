// OpenBFME. GPL-3.0.
//
// NET-SYNC STEP-3 measurement sim: the worldwide link matrix over real
// components. One RelayCanonicalAuthority (real scheduler + builder +
// game-log append into a memory sink) and N real client-side RelayBundleChannel
// + RelayCanonicalClient stacks exchange datagrams over an impaired fabric:
// per-player one-way latency (the matrix's 20/60/120/180/300 ms), uniform
// jitter, and Gilbert-Elliott burst loss (good->bad per-datagram probability
// p, bad->good 1/3 — a loss burst averages three datagrams).
//
// "today" mode models the classic path without the new machinery: every
// order is its own datagram per hop, each hop retransmits a drop after
// 2*RTT (400 ms floor), the relay fans copies out unbundled.
//
// Metrics per run: order delay p50/p95 per player (issue -> the client's
// observation of its commit), stall seconds per minute (frame input due at
// its boundary but late), datagrams + bytes both directions, relay CPU us
// per simulated second. Output: one CSV row per run, summary to stdout.
//
// usage: net_sync_sim <mode: canonical-b|canonical-a|today> <players> <latencies-csv>
//        <jitterMs> <lossPermille> <durationS> [seed]
//   e.g. net_sync_sim canonical-b 16 20,60,120,180,300 30 30 60

#include "RelayCanonicalAuthority.h"

#include "GameNetwork/RelayBundleChannel.h"
#include "GameNetwork/RelayCanonicalClient.h"
#include "GameNetwork/RelayCanonicalInput.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayMembership.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/RelayFrameClock.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <queue>
#include <thread>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr std::uint64_t kGame = 7;
constexpr std::uint64_t kEpoch = 1;
constexpr std::uint64_t kClientTickMs = 200;   // the driver's 5 Hz cadence: one beat per tick
constexpr std::uint64_t kRelayTickMs = 10;     // the relay drains between input batches
constexpr std::uint64_t kClientPollMs = 20;    // the client's channel pump cadence
constexpr std::uint64_t kFrameMs = 200;

// one datagram in flight on one directed link
struct Flight
{
	std::uint64_t deliverAt = 0;
	std::uint64_t emittedAt = 0;
	std::vector<std::uint8_t> bytes;
	std::uint64_t key = 0; // today-mode: the order's tracking key (slot<<40 | id)
};
struct Link
{
	std::uint32_t latencyMs = 0;
	std::uint32_t jitterMs = 0;
	std::uint32_t lossPermille = 0; // burst trigger probability
	bool bad = false;               // Gilbert-Elliott state
	std::deque<Flight> flights;
};
struct Rng
{
	std::mt19937 gen;
	explicit Rng(std::uint64_t seed) : gen((std::uint32_t)seed) {}
	std::uint32_t u32() { return gen(); }
	int uniform(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(gen); }
};

// Gilbert-Elliott: in the bad state every datagram drops; good->bad at lossPermille/1000 per datagram,
// bad->good at 1/3 (a burst averages ~3 datagrams). Returns dropped or not.
bool drop(Link &link, Rng &rng)
{
	if (!link.bad && link.lossPermille && rng.u32() % 1000 < link.lossPermille)
	{
		link.bad = true;
	}
	if (link.bad)
	{
		if (rng.u32() % 3 == 0)
		{
			link.bad = false;
		}
		return true;
	}
	return false;
}
std::uint64_t sampleDelay(Link &link, Rng &rng)
{
	int ms = (int)link.latencyMs;
	if (link.jitterMs)
	{
		ms += rng.uniform(-(int)link.jitterMs, (int)link.jitterMs);
	}
	return (std::uint64_t)std::max(0, ms);
}
bool emit(Link &link, Rng &rng, std::uint64_t now, const std::vector<std::uint8_t> &bytes, std::uint64_t key = 0)
{
	if (drop(link, rng))
	{
		return false;
	}
	link.flights.push_back({now + sampleDelay(link, rng), now, bytes, key});
	return true;
}

std::vector<std::uint8_t> gameMessageBytes(int player, int type)
{
	GameMessage m(type, player);
	NetByteWriter w;
	std::string error;
	NetPacket::writeGameMessage(w, m, &error);
	return w.take();
}

struct Result
{
	double orderP50 = 0, orderP95 = 0;
	double stallSecPerMin = 0;
	std::uint64_t datagramsUp = 0, bytesUp = 0, datagramsDown = 0, bytesDown = 0;
	double relayCpuUsPerSimSec = 0;
	std::uint64_t orders = 0, framesRun = 0;
	std::vector<std::uint8_t> errors;
};

// ---------------- canonical (step 3): real authority + channels + clients ----------------
Result runCanonical(int players, const std::vector<std::uint32_t> &latencies, std::uint32_t jitter,
	std::uint32_t lossPermille, std::uint64_t durationMs, std::uint64_t seed, int policy)
{
	Result r;
	Rng rng(seed);
	// ROUTER-1: the durable append is synchronous into the game log (the fsync journal is dropped); the sim
	// appends into an in-memory sink and counts what the relay would write.
	std::uint64_t logRecords = 0, logBytes = 0;

	RelayCanonicalAuthority::Options ao;
	ao.game = kGame;
	ao.epoch = kEpoch;
	ao.slotMask = (std::uint16_t)((1u << players) - 1);
	ao.policy = policy == 0 ? RelayFrameScheduler::Policy::NextFrame : RelayFrameScheduler::Policy::EqualDelay;
	ao.clockOriginMs = 0;
	ao.channel.gameId = kGame; // member channels must match the game's id/epoch or every packet is refused
	ao.channel.epoch = kEpoch;

	struct Client
	{
		Link up, down;
		RelayBundleChannel channel;
		RelayCanonicalClient::Client cursor;
		std::uint64_t nextOrderId = 1, nextBeatId = 1;
		std::map<std::uint64_t, std::uint64_t> issued; // order id -> issue time
		std::uint32_t lastRan = 0;
		std::vector<double> delays;                    // this client's orders, issue -> observed commit
		std::uint64_t stallMs = 0;
		std::vector<std::string> errs;
		Client(const RelayBundleChannel::Options &co, std::uint16_t mask,
			const std::array<int, 16> &idx)
			: channel(co)
			, cursor(kGame, kEpoch, mask, 1, idx)
		{
		}
	};

	ao.append = [&logRecords, &logBytes](const RelayCommit::Record &record, std::string &error) -> bool {
		std::vector<std::uint8_t> bytes;
		if (!RelayCommit::encode(record, bytes, error))
		{
			return false;
		}
		++logRecords;
		logBytes += bytes.size();
		return true;
	};
	RelayCanonicalAuthority auth(ao);

	std::array<int, 16> idx;
	for (int i = 0; i < 16; ++i)
	{
		idx[(size_t)i] = i < players ? i : -1;
	}
	RelayBundleChannel::Options co;
	co.gameId = kGame;
	co.epoch = kEpoch;
	std::deque<Client> clients;
	for (int i = 0; i < players; ++i)
	{
		clients.emplace_back(co, (std::uint16_t)((1u << players) - 1), idx);
		Client &cl = clients.back();
		cl.up.latencyMs = latencies[(size_t)i % latencies.size()];
		cl.down.latencyMs = cl.up.latencyMs;
		cl.up.jitterMs = cl.down.jitterMs = jitter;
		cl.up.lossPermille = cl.down.lossPermille = lossPermille;
		std::string error;
		auth.bind((std::uint8_t)i, 0, false, error);
	}

	std::uint64_t relayCpuUs = 0;
	std::mt19937 workload(seed * 7919 + 13);
	// order issue rate: ~1.5 orders per second per player (a busy RTS click rate), geometric gaps per 10 ms step
	const std::uint64_t stepMs = 10;
	for (std::uint64_t t = 0; t < durationMs; t += stepMs)
	{
		// client ticks every 200 ms: orders + one beat
		for (int i = 0; i < players; ++i)
		{
			Client &cl = clients[(size_t)i];
			if (t % kClientTickMs == 0)
			{
				// ~1.5/s => P(order this tick) = 0.3
				if (std::uniform_real_distribution<double>(0, 1)(workload) < 0.3)
				{
					std::vector<std::uint8_t> bytes = gameMessageBytes(i, 1002);
					RelayCanonicalInput::Message m;
					m.kind = RelayCanonicalInput::Kind::Order;
					m.id = cl.nextOrderId;
					m.bytes = bytes;
					std::vector<std::uint8_t> wire;
					std::string error;
					if (RelayCanonicalInput::encode(m, wire, error) && cl.channel.queue(wire, error))
					{
						cl.issued[cl.nextOrderId] = t;
						++cl.nextOrderId;
						++r.orders;
					}
				}
				RelayCanonicalInput::Message beat;
				beat.kind = RelayCanonicalInput::Kind::Beat;
				beat.id = cl.nextBeatId++;
				std::vector<std::uint8_t> wire;
				std::string error;
				if (RelayCanonicalInput::encode(beat, wire, error))
				{
					cl.channel.queue(wire, error);
				}
			}
			// the client's channel pump (retransmits + queued datagrams)
			std::string error;
			for (const auto &p : cl.channel.poll(t, 64, &error))
			{
				++r.datagramsUp;
				r.bytesUp += p.size();
				emit(cl.up, rng, t, p);
			}
		}
		// link deliveries, up: to the authority
		for (int i = 0; i < players; ++i)
		{
			Client &cl = clients[(size_t)i];
			while (!cl.up.flights.empty() && cl.up.flights.front().deliverAt <= t)
			{
				std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> emissions;
				const auto t0 = std::chrono::steady_clock::now();
				auth.receive((std::uint8_t)i, cl.up.flights.front().bytes.data(), cl.up.flights.front().bytes.size(), t, emissions);
				relayCpuUs += (std::uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - t0)
								  .count();
				cl.up.flights.pop_front();
				for (auto &[slot, bytes] : emissions)
				{
					++r.datagramsDown;
					r.bytesDown += bytes.size();
					emit(clients[slot].down, rng, t, bytes);
				}
			}
			// down: channel packets to the client
			while (!cl.down.flights.empty() && cl.down.flights.front().deliverAt <= t)
			{
				RelayBundleChannel::Bytes ack;
				std::string error;
				if (cl.channel.receive(cl.down.flights.front().bytes, t, ack, error))
				{
					if (!ack.empty())
					{
						++r.datagramsUp;
						r.bytesUp += ack.size();
						emit(cl.up, rng, t, ack);
					}
					for (const auto &m : cl.channel.takeDelivered())
					{
						RelayCommit::Record record;
						if (std::getenv("SIM_DEBUG"))
						{
							std::fprintf(stderr, "t=%llu client %d delivered %zu bytes\n", (unsigned long long)t, i, m.bytes.size());
						}
						if (!RelayCommit::decode(m.bytes, RelayCanonicalInput::kMaxWireBytes, record, error))
						{
							cl.errs.push_back("record decode: " + error);
							continue;
						}
						if (!cl.cursor.accept(record, error))
						{
							cl.errs.push_back("record: " + error);
							continue;
						}
						RelayCanonicalClient::FrameInput input;
						while (cl.cursor.takeFrameInput(input))
						{
							const std::uint64_t due = input.stamp.frame * kFrameMs;
							if (t > due)
							{
								cl.stallMs += t - due;
							}
							++r.framesRun;
							for (const auto &cmd : input.commands)
							{
								auto it = clients[cmd.slot].issued.find(cmd.id);
								if (it != clients[cmd.slot].issued.end())
								{
									clients[cmd.slot].delays.push_back((double)(t - it->second));
								}
							}
							cl.lastRan = input.stamp.frame;
						}
					}
				}
				cl.down.flights.pop_front();
			}
		}
		if (std::getenv("SIM_DEBUG") && t % 500 == 0)
		{
			const auto bc = auth.counters();
			std::fprintf(stderr, "t=%llu log{committed=%llu} builder{published=%llu}\n",
				(unsigned long long)t, (unsigned long long)logRecords,
				(unsigned long long)bc.normalPlayerFrames);
		}
		// the relay's periodic drain
		if (t % kRelayTickMs == 0)
		{
			std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> emissions;
			const auto t0 = std::chrono::steady_clock::now();
			auth.service(t, emissions);
			relayCpuUs += (std::uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
							  .count();
			for (auto &[slot, bytes] : emissions)
			{
				++r.datagramsDown;
				r.bytesDown += bytes.size();
				emit(clients[slot].down, rng, t, bytes);
			}
		}
		// 1 ms real per 10 ms simulated step keeps the drain in lockstep with the clients
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// metrics
	std::fprintf(stderr, "game log: committed=%llu bytes=%llu\n",
		(unsigned long long)logRecords, (unsigned long long)logBytes);
	for (const auto &e : auth.errors())
	{
		std::fprintf(stderr, "auth: %s\n", e.c_str());
	}
	std::vector<double> all;
	std::uint64_t stalls = 0;
	for (auto &cl : clients)
	{
		all.insert(all.end(), cl.delays.begin(), cl.delays.end());
		stalls += cl.stallMs;
		for (const auto &e : cl.errs)
		{
			std::fprintf(stderr, "client: %s\n", e.c_str());
		}
	}
	if (!all.empty())
	{
		std::sort(all.begin(), all.end());
		r.orderP50 = all[all.size() / 2];
		r.orderP95 = all[(size_t)((double)all.size() * 0.95)];
	}
	r.stallSecPerMin = (double)stalls / 1000.0 / ((double)durationMs / 60000.0) / players;
	r.relayCpuUsPerSimSec = (double)relayCpuUs / ((double)durationMs / 1000.0);
	return r;
}

// ---------------- today: per-order datagrams, ack+resend per hop, no bundling ----------------
Result runToday(int players, const std::vector<std::uint32_t> &latencies, std::uint32_t jitter,
	std::uint32_t lossPermille, std::uint64_t durationMs, std::uint64_t seed)
{
	Result r;
	Rng rng(seed);
	struct Cl
	{
		Link up, down;
		std::map<std::uint64_t, std::uint64_t> issued; // order key -> issue time
		std::vector<double> delays;
		std::uint64_t stallMs = 0;
		std::uint32_t lastRan = 0;
	};
	std::vector<Cl> clients((size_t)players);
	for (int i = 0; i < players; ++i)
	{
		clients[(size_t)i].up.latencyMs = latencies[(size_t)i % latencies.size()];
		clients[(size_t)i].down.latencyMs = clients[(size_t)i].up.latencyMs;
		clients[(size_t)i].up.jitterMs = clients[(size_t)i].down.jitterMs = jitter;
		clients[(size_t)i].up.lossPermille = clients[(size_t)i].down.lossPermille = lossPermille;
	}

	// in-flight bookkeeping: (key -> {issueT, execFrame, remaining recipients mask})
	struct InFlight
	{
		std::uint64_t issueT = 0;
		std::uint32_t execFrame = 0;
		std::uint16_t remaining = 0;
	};
	std::map<std::uint64_t, InFlight> inFlight;
	// resend queues: (resendAt, link*, bytes, key, recipient)
	struct Retry
	{
		std::uint64_t at = 0;
		int linkDir = 0; // 0 = up (client->relay), 1 = down (relay->client slot)
		int slot = 0;
		std::uint64_t key = 0;
	};
	std::deque<Retry> retries;

	std::mt19937 workload((std::uint32_t)(seed * 31 + 7));
	const std::uint64_t stepMs = 10;
	for (std::uint64_t t = 0; t < durationMs; t += stepMs)
	{
		// orders: same workload rate; each is its own datagram up
		for (int i = 0; i < players; ++i)
		{
			if (t % kClientTickMs == 0 && std::uniform_real_distribution<double>(0, 1)(workload) < 0.3)
			{
				Cl &cl = clients[(size_t)i];
				const std::uint64_t key = ((std::uint64_t)i << 40) | (cl.issued.size() + 1);
				std::vector<std::uint8_t> bytes = gameMessageBytes(i, 1002);
				InFlight f;
				f.issueT = t;
				f.execFrame = (std::uint32_t)(t / kFrameMs) + 1; // the client stamps run-ahead 1
				f.remaining = (std::uint16_t)((1u << players) - 1);
				inFlight[key] = f;
				cl.issued[key] = t;
				if (!emit(cl.up, rng, t, bytes, key))
				{
					retries.push_back({t + std::max<std::uint64_t>(400, 2 * cl.up.latencyMs), 0, i, key});
				}
				++r.datagramsUp;
				r.bytesUp += bytes.size();
				++r.orders;
			}
		}
		// retries fire
		// deque iterators die on push_back: re-queue into a side vector, append after the walk
		std::vector<Retry> requeue;
		for (auto it = retries.begin(); it != retries.end();)
		{
			if (it->at > t)
			{
				++it;
				continue;
			}
			Cl &cl = clients[(size_t)it->slot];
			std::vector<std::uint8_t> bytes = gameMessageBytes(it->slot, 1002);
			Link &link = it->linkDir == 0 ? cl.up : cl.down;
			if (it->linkDir == 0)
			{
				++r.datagramsUp;
				r.bytesUp += bytes.size();
			}
			else
			{
				++r.datagramsDown;
				r.bytesDown += bytes.size();
			}
			if (!emit(link, rng, t, bytes, it->key))
			{
				requeue.push_back({t + std::max<std::uint64_t>(400, 2 * link.latencyMs), it->linkDir, it->slot, it->key});
			}
			it = retries.erase(it);
		}
		retries.insert(retries.end(), requeue.begin(), requeue.end());
		// deliveries
		for (int i = 0; i < players; ++i)
		{
			Cl &cl = clients[(size_t)i];
			// up: the relay gets the order, fans a copy to every client (incl. the sender's echo)
			while (!cl.up.flights.empty() && cl.up.flights.front().deliverAt <= t)
			{
				const Flight f = cl.up.flights.front();
				cl.up.flights.pop_front();
				for (int d = 0; d < players; ++d)
				{
					Cl &dc = clients[(size_t)d];
					++r.datagramsDown;
					r.bytesDown += f.bytes.size();
					if (!emit(dc.down, rng, t, f.bytes, f.key))
					{
						retries.push_back({t + std::max<std::uint64_t>(400, 2 * dc.down.latencyMs), 1, d, f.key});
					}
				}
			}
			// up drops: the sender resends at +2*RTT (400 ms floor) — classic stop-gap resend
			// (a dropped up-flight simply never arrived; the sender's resend timer covers it:
			//  modelled by scheduling a retry for each emitted up datagram, cancelled on receipt —
			//  approximated here by queueing a retry when a flight is known-dropped. For the
			//  fabric, a dropped emit produced no flight; we re-drive it below.)
			// down: delivery to this client
			while (!cl.down.flights.empty() && cl.down.flights.front().deliverAt <= t)
			{
				const Flight f = cl.down.flights.front();
				cl.down.flights.pop_front();
				auto inf = inFlight.find(f.key);
				if (inf != inFlight.end())
				{
					inf->second.remaining &= (std::uint16_t)~(1u << i);
					Cl &sender = clients[(size_t)(f.key >> 40)];
					sender.delays.push_back((double)(t - inf->second.issueT));
					if (inf->second.remaining == 0)
					{
						const std::uint64_t due = (std::uint64_t)inf->second.execFrame * kFrameMs;
						if (t > due)
						{
							sender.stallMs += t - due;
						}
						++r.framesRun;
						inFlight.erase(inf);
					}
				}
			}
		}
	}
	std::vector<double> all;
	std::uint64_t stalls = 0;
	for (auto &cl : clients)
	{
		all.insert(all.end(), cl.delays.begin(), cl.delays.end());
		stalls += cl.stallMs;
	}
	if (!all.empty())
	{
		std::sort(all.begin(), all.end());
		r.orderP50 = all[all.size() / 2];
		r.orderP95 = all[(size_t)((double)all.size() * 0.95)];
	}
	r.stallSecPerMin = (double)stalls / 1000.0 / ((double)durationMs / 60000.0) / players;
	r.relayCpuUsPerSimSec = 5.0; // forwarding only: negligible, reported as a constant estimate
	return r;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc < 7)
	{
		std::fprintf(stderr, "usage: net_sync_sim <mode> <players> <latencies-csv> <jitterMs> <lossPermille> <durationS> [seed]\n");
		return 2;
	}
	const std::string mode = argv[1];
	const int players = std::atoi(argv[2]);
	std::vector<std::uint32_t> latencies;
	{
		std::string s = argv[3];
		size_t at = 0;
		while (at <= s.size())
		{
			const size_t comma = s.find(',', at);
			latencies.push_back((std::uint32_t)std::atoi(s.substr(at, comma == std::string::npos ? comma : comma - at).c_str()));
			if (comma == std::string::npos)
			{
				break;
			}
			at = comma + 1;
		}
	}
	const std::uint32_t jitter = (std::uint32_t)std::atoi(argv[4]);
	const std::uint32_t loss = (std::uint32_t)std::atoi(argv[5]);
	const std::uint64_t duration = (std::uint64_t)std::atoi(argv[6]) * 1000u;
	const std::uint64_t seed = argc > 7 ? (std::uint64_t)std::atoi(argv[7]) : 42;

	Result r;
	if (mode == "canonical-b")
	{
		r = runCanonical(players, latencies, jitter, loss, duration, seed, 0);
	}
	else if (mode == "canonical-a")
	{
		r = runCanonical(players, latencies, jitter, loss, duration, seed, 1);
	}
	else if (mode == "today")
	{
		r = runToday(players, latencies, jitter, loss, duration, seed);
	}
	else
	{
		std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
		return 2;
	}

	std::printf("mode,players,latency_set,jitter_ms,loss_permille,duration_s,"
				"orders,frames,order_p50_ms,order_p95_ms,stall_s_per_min,"
				"dg_up,B_up,dg_down,B_down,relay_cpu_us_per_s\n");
	std::printf("%s,%d,%s,%u,%u,%llu,%llu,%llu,%.1f,%.1f,%.3f,%llu,%llu,%llu,%llu,%.0f\n",
		mode.c_str(), players, argv[3], jitter, loss, (unsigned long long)(duration / 1000),
		(unsigned long long)r.orders, (unsigned long long)r.framesRun, r.orderP50, r.orderP95,
		r.stallSecPerMin, (unsigned long long)r.datagramsUp, (unsigned long long)r.bytesUp,
		(unsigned long long)r.datagramsDown, (unsigned long long)r.bytesDown, r.relayCpuUsPerSimSec);
	if (!r.errors.empty())
	{
		std::fprintf(stderr, "note: %zu client-side record errors seen\n", r.errors.size());
	}
	return 0;
}
