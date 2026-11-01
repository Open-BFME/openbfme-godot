// OpenBFME. GPL-3.0.
//
// NET-SYNC canonical frame scheduler: the relay-side stamping and seal policy
// feeding RelayFrameBundle::Builder, driven by RelayCanonicalInput ingress.
// The authority (game-log append + cursor + channel, owned elsewhere) admits decoded
// ingress through here, seals frame F when sealDue reports it, then stages
// with Builder::prepare and only publishes after a durable game-log append.
//
// Both policies stamp an order for the first frame boundary still open when
// it reaches admission (never before the committed clock origin) and publish
// one canonical bundle per frame; they differ in what closes a frame:
//
//   EqualDelay (policy A, today's semantics): a frame seals once every slot
//     the membership requires has delivered the beat ending its window —
//     ordered per-slot beat markers mirror FRAMEINFO's "input done". A late
//     slot holds the bundle and the whole frame runs late for everyone.
//     Orders stamp to the frame their slot's window covers (the frame the
//     slot's next beat completes), so a lagging slot's window still lands
//     together; an order received after its window's beat stamps the next
//     window's frame.
//   NextFrame (policy B, per-player delay): a frame seals at its wall-clock
//     RelayFrameClock boundary. An order stamped for a frame already sealed
//     (received past the boundary) runs on the next unpublished frame: only
//     its sender is delayed. Beats are accepted for audit but never gate a
//     seal.
//
// Counters (order admissions, late restamps, beats) are the measurement
// hooks for the worldwide matrix.
#pragma once

#include "GameNetwork/RelayCanonicalInput.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayFrameClock.h"
#include "GameNetwork/RelayMembership.h"

#include <array>
#include <cstdint>
#include <string>

namespace RelayFrameScheduler
{
enum class Policy : std::uint8_t { EqualDelay, NextFrame };

class Scheduler
{
public:
	// firstFrame must equal the builder's committed first frame.
	Scheduler(Policy policy, std::uint32_t firstFrame);

	// Decoded ingress for one bound slot (the caller authenticates; the slot
	// is never read from the payload). Beats validate a dense per-slot id
	// sequence starting at 1. Orders stamp per the policy, then feed
	// builder.admit; the Admission result is returned unchanged. A beat that
	// would exceed the frame counter is a decode-level error (false).
	RelayFrameBundle::Builder::Admission admit(std::uint8_t slot,
		const RelayCanonicalInput::Message &message, std::uint64_t receiptMs,
		const RelayFrameClock &clock, RelayFrameBundle::Builder &builder, std::string &error);

	// NextFrame: true once nowMs reaches the boundary of the next unpublished
	// frame. EqualDelay: true once every required slot's window covering the
	// next unpublished frame has completed. Required slots are
	// membership.active minus held and departed (held-for-rejoin and gone
	// slots cannot stall the canonical beat).
	bool sealDue(std::uint64_t nowMs, const RelayFrameClock &clock,
		const RelayMembership::Snapshot &membership, const RelayFrameBundle::Builder &builder,
		std::string &error) const;

	// Slots still owed a window for the open frame (EqualDelay diagnostics).
	std::uint16_t awaiting(const RelayMembership::Snapshot &membership,
		const RelayFrameBundle::Builder &builder) const;

	struct Counters
	{
		std::uint64_t orders = 0;
		std::uint64_t lateOrders = 0;   ///< restamped past the received boundary (NextFrame)
		std::uint64_t beats = 0;
	};
	const Counters &counters() const { return m_counters; }

private:
	std::uint16_t requiredMask(const RelayMembership::Snapshot &membership) const;

	Policy m_policy;
	std::array<std::uint32_t, RelayFrameBundle::kSlots> m_beatFrame{};   ///< frame the slot's next beat completes
	std::array<std::uint64_t, RelayFrameBundle::kSlots> m_nextBeatId{};
	Counters m_counters;
};
}
