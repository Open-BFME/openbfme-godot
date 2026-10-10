// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// NetImpairment (lane MP-3, NET-4): the network test harness's fault injector. The Transport (Transport.h) asks it what happens to every datagram it puts on
// the wire and to every datagram it reads: per link (the destination slot) a base latency, a symmetric jitter, loss, duplication and explicit reordering,
// and for the whole endpoint blackouts (the cable pulled: nothing goes out and nothing comes in for a while). Every decision comes from a seeded RNG per
// link, so the n-th datagram on a link meets the same fate in every run with the same seed (the wall clock only decides WHEN datagrams are sent).
//
// DONOR FACTS (ZH GameNetwork/Transport.cpp): ZH's Transport has a debug-only DelayedTransportMessage queue (m_delayedInBuffer, the "Latency" and
// "PacketLoss" options of the debug build) that holds or drops incoming packets; this is the same idea on the sending side, extended to duplication,
// reordering and blackouts. Nothing here exists in retail and nothing here is enabled unless a test or a peer's command line asks for it.
//
// Semantics of one outgoing datagram on a link with faults F (schedule()):
//   * during a blackout: dropped (counted as blackoutOut);
//   * else lost with probability F.lossPerMille / 1000;
//   * else it leaves after max(0, F.latencyMs + U[-F.jitterMs, +F.jitterMs]) ms; independent delays reorder datagrams whose spacing is below the jitter;
//   * with probability F.reorderPerMille / 1000 it is additionally held back F.reorderMs (default: 2 * jitter + 20 ms), so later datagrams overtake it;
//   * with probability F.duplicatePerMille / 1000 a second copy follows with its own independent delay.
// A datagram that becomes due during a blackout is dropped then (it was "in flight" when the cable was pulled). Incoming datagrams read during a blackout
// are dropped (blackoutIn).
//
// Not simulation code: it decides when and whether bytes arrive, never what a logic frame contains (excluded in tools/sim/sim_policy.json).

#pragma once

#include "GameNetwork/GameInfo.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// the faults of one link (one direction: the datagrams this endpoint sends to one peer)
struct NetLinkFaults
{
	int latencyMs = 0;         ///< base one-way delay
	int jitterMs = 0;          ///< the delay varies uniformly by +- this much (clamped at 0)
	int lossPerMille = 0;      ///< datagrams lost
	int duplicatePerMille = 0; ///< datagrams that arrive twice
	int reorderPerMille = 0;   ///< datagrams held back so that later ones overtake them
	int reorderMs = -1;        ///< how long a reordered datagram is held back (-1: 2 * jitterMs + 20)
	bool any() const { return latencyMs > 0 || jitterMs > 0 || lossPerMille > 0 || duplicatePerMille > 0 || reorderPerMille > 0; }
	// "latency=150,jitter=50,loss=50,dup=10,reorder=20,reorder_ms=80" (every key optional, values >= 0, per-mille values <= 1000); false + *error
	static bool parse(const std::string &text, NetLinkFaults &out, std::string *error);
	std::string text() const;
};

class NetImpairment
{
public:
	struct Config
	{
		std::uint32_t seed = 1;
		NetLinkFaults all;                                ///< every link without its own entry
		std::array<NetLinkFaults, MAX_SLOTS> link{};      ///< per destination slot, used when linkSet[slot]
		std::array<bool, MAX_SLOTS> linkSet{};
		bool active() const;
	};
	struct Stats
	{
		unsigned long long scheduled = 0, lost = 0, duplicated = 0, reordered = 0, delayed = 0, blackoutOut = 0, blackoutIn = 0, blackouts = 0;
	};

	NetImpairment() : NetImpairment(Config()) {}
	explicit NetImpairment(const Config &config);

	const Config &config() const { return m_config; }
	const NetLinkFaults &faults(int slot) const;
	// any faults configured or a blackout ever requested (a Transport without either sends straight to the socket)
	bool active() const { return m_config.active() || m_blackoutUntil > 0; }

	// the endpoint is cut off from `now` for `durationMs` (a later call extends or replaces the window)
	void blackout(std::uint64_t now, std::uint64_t durationMs);
	bool blackedOut(std::uint64_t now) const { return now >= m_blackoutFrom && now < m_blackoutUntil; }

	// an outgoing datagram to `slot` (-1: not a game peer: only blackouts apply) at `now`: the times its copies reach the wire (empty: dropped)
	void schedule(int slot, std::uint64_t now, std::vector<std::uint64_t> &sendTimes);
	// a datagram due at `now` from the delay queue: false when a blackout swallows it
	bool releaseDue(std::uint64_t now);
	// an incoming datagram read at `now`: false when a blackout swallows it
	bool acceptIncoming(std::uint64_t now);

	const Stats &stats() const { return m_stats; }
	std::string statsText() const;

	// the acceptance stops of lane MP-3 (docs/STOPS.md S-1890 .. S-1892), one line each; every peer report carries them
	static std::vector<std::string> stopLines();

private:
	std::uint32_t next(int slot); ///< the link's RNG (xorshift32 seeded per link)
	int uniform(int slot, int lo, int hi); ///< lo .. hi inclusive

	Config m_config;
	std::array<std::uint32_t, MAX_SLOTS + 1> m_rng{}; ///< per destination slot; the last one for non-peer datagrams
	std::uint64_t m_blackoutFrom = 0, m_blackoutUntil = 0;
	Stats m_stats;
};
