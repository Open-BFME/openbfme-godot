// OpenBFME online relay. GPL-3.0.
//
// RelayHealth (lane NETDIAG-1, docs/ONLINE.md "Reading the relay health"): passive, bounded per-game diagnostics of the relay. Fixed-size state only
// (no allocation after the game's first HELLO); it never influences forwarding, logging or simulation. RelayCore feeds it the unwrapped transport
// packet (after AUTH-1's envelope is opened) of every DATA it forwards or follows; healthJson sorts and serialises at reporting time only.
//
// Not simulation code.

#pragma once

#include "GameNetwork/RelayProtocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

struct RelayHealth
{
	struct Samples
	{
		std::array<std::uint64_t, 128> values{};
		size_t count = 0, next = 0;
		void add(std::uint64_t value)
		{
			values[next] = value;
			next = (next + 1) % values.size();
			count = std::min(count + 1, values.size());
		}
		// nearest-rank percentiles over the retained samples (sorted on a copy: reporting time only)
		void json(std::ostream &out) const
		{
			auto sorted = values;
			std::sort(sorted.begin(), sorted.begin() + (std::ptrdiff_t)count);
			out << "{\"samples\":" << count;
			for (unsigned p : { 50u, 95u, 99u })
			{
				out << ",\"p" << p << "Ms\":";
				if (count)
				{
					out << sorted[(count * p + 99) / 100 - 1];
				}
				else
				{
					out << "null";
				}
			}
			out << '}';
		}
	};
	// a packet whose cumulative-ACK turnaround is timed: records [first, end) sent at `at` (relay clock)
	struct Pending
	{
		std::uint32_t first = 0, end = 0;
		std::uint64_t at = 0;
		bool active = false, resent = false;
	};
	struct Stream
	{
		std::array<Pending, 4> pending{};
		size_t next = 0;
		std::uint32_t highest = 0, frame = 0;
		std::uint64_t seen = 0, lastRtt = 0;
		bool hasRtt = false;
		// a 64-record duplicate window (bit n: highest - n was seen); older records are outside diagnostic coverage
		bool fresh(std::uint32_t seq, bool &duplicate)
		{
			duplicate = false;
			if (!seen || (std::int32_t)(seq - highest) > 0)
			{
				const std::uint32_t delta = seq - highest;
				seen = !seen || delta >= 64 ? 1 : (seen << delta) | 1;
				highest = seq;
				return true;
			}
			const std::uint32_t behind = highest - seq;
			if (behind >= 64)
			{
				return false;
			}
			const std::uint64_t bit = std::uint64_t{ 1 } << behind;
			duplicate = (seen & bit) != 0;
			seen |= bit;
			return !duplicate;
		}
	};
	// [sender][target], every slot of the relay's games (RelayCore static_asserts the same extent as its own streams)
	std::array<std::array<Stream, RelayProtocol::kMaxSlots>, RelayProtocol::kMaxSlots> streams;
	Samples rtt, jitter;
	// dataDrops: DATA the relay refused (RelayCore::data: rate limit, malformed datagram or packet, slot mismatch, bad target) — the per-slot
	// `dropped` the log's END record carries; never DATA that was simply not forwarded (an unbound target, canonical NS input)
	std::uint64_t dataDrops = 0, resends = 0, lateFrames = 0;
};
