// OpenBFME. GPL-3.0.
//
// NET-SYNC canonical frame scheduler; see RelayFrameScheduler.h for the two
// late-order policies and the seal contract.

#include "GameNetwork/RelayFrameScheduler.h"

#include <limits>

namespace RelayFrameScheduler
{
Scheduler::Scheduler(Policy policy, std::uint32_t firstFrame)
	: m_policy(policy)
{
	m_beatFrame.fill(firstFrame);
	m_nextBeatId.fill(1);
}

std::uint16_t Scheduler::requiredMask(const RelayMembership::Snapshot &membership) const
{
	return (std::uint16_t)(membership.active & ~(membership.held | membership.departed));
}

std::uint16_t Scheduler::awaiting(const RelayMembership::Snapshot &membership,
	const RelayFrameBundle::Builder &builder) const
{
	const std::uint16_t required = requiredMask(membership);
	std::uint16_t mask = 0;
	for (unsigned slot = 0; slot < RelayFrameBundle::kSlots; ++slot)
		if ((required >> slot) & 1u && m_beatFrame[slot] <= builder.nextUnpublishedFrame())
			mask |= (std::uint16_t)(1u << slot);
	return mask;
}

bool Scheduler::sealDue(std::uint64_t nowMs, const RelayFrameClock &clock,
	const RelayMembership::Snapshot &membership, const RelayFrameBundle::Builder &builder,
	std::string &error) const
{
	error.clear();
	if (m_policy == Policy::NextFrame)
	{
		std::uint64_t boundary = 0;
		if (!clock.boundaryOf(builder.nextUnpublishedFrame(), boundary, error))
			return false;
		error.clear();
		return nowMs >= boundary;
	}
	return awaiting(membership, builder) == 0;
}

RelayFrameBundle::Builder::Admission Scheduler::admit(std::uint8_t slot,
	const RelayCanonicalInput::Message &message, std::uint64_t receiptMs,
	const RelayFrameClock &clock, RelayFrameBundle::Builder &builder, std::string &error)
{
	using Builder = RelayFrameBundle::Builder;
	if (slot >= RelayFrameBundle::kSlots)
	{
		error = "slot " + std::to_string(slot);
		return Builder::Admission::InvalidSlot;
	}
	if (message.kind == RelayCanonicalInput::Kind::Beat)
	{
		if (message.id != m_nextBeatId[slot])
		{
			error = "beat id " + std::to_string(message.id) + " expected "
				+ std::to_string(m_nextBeatId[slot]);
			return Builder::Admission::InvalidSequence;
		}
		if (m_beatFrame[slot] == std::numeric_limits<std::uint32_t>::max())
		{
			error = "beat frame counter exhausted";
			return Builder::Admission::Malformed;
		}
		++m_nextBeatId[slot];
		++m_beatFrame[slot];
		++m_counters.beats;
		error.clear();
		return Builder::Admission::Accepted;
	}

	std::uint32_t stamped = builder.nextUnpublishedFrame();
	if (m_policy == Policy::NextFrame)
	{
		std::uint32_t boundary = 0;
		if (!clock.frameAfter(receiptMs, boundary, error))
			return Builder::Admission::InvalidFrame;
		if (boundary < stamped)
			++m_counters.lateOrders;   // its stamped frame already sealed: runs on the next frame
		else
			stamped = boundary;
	}
	else
	{
		// EqualDelay: the slot's window mapping is stream-ordered. Orders
		// preceding the next beat belong to the window that beat completes.
		if (m_beatFrame[slot] > stamped)
			stamped = m_beatFrame[slot];
	}
	const auto result = builder.admit(slot, message.id, stamped,
		message.bytes.data(), message.bytes.size(), error);
	if (result == Builder::Admission::Accepted)
		++m_counters.orders;
	return result;
}
}
