// OpenBFME. GPL-3.0.
// See GameNetwork/RelayFrameBundle.h.

#include "GameNetwork/RelayFrameBundle.h"

#include "Common/Sha256.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace RelayFrameBundle
{
namespace
{
constexpr std::size_t kHeaderBytes = 22; // v2: + a reserved u32 (0)
constexpr std::size_t kOrderFixedBytes = 13;
constexpr std::size_t kDeferralBytes = 54;
constexpr std::size_t kRefusalBytes = 18;
constexpr std::uint16_t kVersion = 2;

bool fail(std::string &error, const char *message)
{
	error = message;
	return false;
}

bool addSize(std::size_t left, std::size_t right, std::size_t &out)
{
	if (right > std::numeric_limits<std::size_t>::max() - left)
	{
		return false;
	}
	out = left + right;
	return true;
}

bool multiplySize(std::size_t left, std::size_t right, std::size_t &out)
{
	if (right && left > std::numeric_limits<std::size_t>::max() / right)
	{
		return false;
	}
	out = left * right;
	return true;
}

void put(std::vector<std::uint8_t> &bytes, std::uint64_t value, unsigned width)
{
	for (unsigned i = 0; i != width; ++i)
	{
		bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
	}
}

std::uint64_t get(const std::vector<std::uint8_t> &bytes, std::size_t offset, unsigned width)
{
	std::uint64_t value = 0;
	for (unsigned i = 0; i != width; ++i)
	{
		value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
	}
	return value;
}

bool keyLess(std::uint8_t leftSlot, std::uint64_t leftId, std::uint8_t rightSlot, std::uint64_t rightId)
{
	return leftSlot < rightSlot || (leftSlot == rightSlot && leftId < rightId);
}

bool validSlot(std::uint8_t slot)
{
	return slot < kSlots;
}

bool validOrderList(const Frame &frame, std::string &error)
{
	std::set<std::pair<std::uint8_t, std::uint64_t>> ids;
	std::array<std::size_t, kSlots> ordinaryBytes{};
	std::array<unsigned, kSlots> oversized{};
	std::uint8_t previousSlot = 0;
	std::uint64_t previousId = 0;
	bool first = true;
	for (const Order &order : frame.orders)
	{
		if (!validSlot(order.slot) || !order.id || order.bytes.empty() || order.bytes.size() > kMaxOrderBytes)
		{
			return fail(error, "invalid canonical frame order");
		}
		if (!first && !keyLess(previousSlot, previousId, order.slot, order.id))
		{
			return fail(error, "canonical frame orders are not strictly ordered");
		}
		first = false;
		previousSlot = order.slot;
		previousId = order.id;
		if (!ids.emplace(order.slot, order.id).second)
		{
			return fail(error, "duplicate canonical frame identifier");
		}
		if (order.bytes.size() > kPlayerFrameBytes)
		{
			++oversized[order.slot];
		}
		else
		{
			if (!addSize(ordinaryBytes[order.slot], order.bytes.size(), ordinaryBytes[order.slot]))
			{
				return fail(error, "canonical frame order byte accounting overflow");
			}
		}
	}
	for (std::size_t slot = 0; slot != kSlots; ++slot)
	{
		if (oversized[slot] && (oversized[slot] != 1 || ordinaryBytes[slot]))
		{
			return fail(error, "oversized canonical frame order must be alone");
		}
		if (ordinaryBytes[slot] > kPlayerFrameBytes)
		{
			return fail(error, "canonical frame player budget is exceeded");
		}
	}
	return true;
}

bool validDeferralList(const Frame &frame, std::set<std::pair<std::uint8_t, std::uint64_t>> &ids, std::string &error)
{
	std::array<std::uint64_t, kSlots> lastOrder{};
	std::array<bool, kSlots> hasOrder{};
	std::array<std::size_t, kSlots> ordinaryBytes{};
	std::array<bool, kSlots> oversizedOrder{};
	for (const Order &order : frame.orders)
	{
		lastOrder[order.slot] = order.id;
		hasOrder[order.slot] = true;
		if (order.bytes.size() > kPlayerFrameBytes)
		{
			oversizedOrder[order.slot] = true;
		}
		else
		{
			ordinaryBytes[order.slot] += order.bytes.size();
		}
	}
	std::uint8_t previousSlot = 0;
	std::uint64_t previousId = 0;
	bool first = true;
	for (const Deferral &deferral : frame.deferrals)
	{
		const bool firstForSlot = first || previousSlot != deferral.slot;
		if (!validSlot(deferral.slot) || !deferral.id || !deferral.orderBytes || deferral.orderBytes > kMaxOrderBytes)
		{
			return fail(error, "invalid canonical frame deferral");
		}
		if (!first && !keyLess(previousSlot, previousId, deferral.slot, deferral.id))
		{
			return fail(error, "canonical frame deferrals are not strictly ordered");
		}
		first = false;
		previousSlot = deferral.slot;
		previousId = deferral.id;
		if (!ids.emplace(deferral.slot, deferral.id).second)
		{
			return fail(error, "duplicate canonical frame identifier");
		}
		if (hasOrder[deferral.slot] && deferral.id <= lastOrder[deferral.slot])
		{
			return fail(error, "canonical frame deferral precedes an executed order");
		}
		if (deferral.fromFrame == std::numeric_limits<std::uint32_t>::max() || deferral.toFrame != deferral.fromFrame + 1)
		{
			return fail(error, "canonical frame deferral range is invalid");
		}
		if (deferral.reason != DeferralReason::Budget && deferral.reason != DeferralReason::OversizedAlone)
		{
			return fail(error, "canonical frame deferral reason is invalid");
		}
		if (oversizedOrder[deferral.slot] != (deferral.reason == DeferralReason::OversizedAlone))
		{
			return fail(error, "canonical frame deferral reason disagrees with executed orders");
		}
		if (!oversizedOrder[deferral.slot] && deferral.reason == DeferralReason::Budget)
		{
			const std::size_t remaining = kPlayerFrameBytes - ordinaryBytes[deferral.slot];
			if (firstForSlot && (!hasOrder[deferral.slot] || deferral.orderBytes <= remaining))
			{
				return fail(error, "canonical budget deferral does not exceed remaining budget");
			}
		}
	}
	return true;
}

bool validRefusalList(const Frame &frame, const std::set<std::pair<std::uint8_t, std::uint64_t>> &orderIds, std::string &error)
{
	std::set<std::pair<std::uint8_t, std::uint64_t>> ids = orderIds;
	std::uint8_t previousSlot = 0;
	std::uint64_t previousId = 0;
	bool first = true;
	for (const Refusal &refusal : frame.refusals)
	{
		if (!validSlot(refusal.slot) || !refusal.id)
		{
			return fail(error, "invalid canonical frame refusal");
		}
		if (!first && !keyLess(previousSlot, previousId, refusal.slot, refusal.id))
		{
			return fail(error, "canonical frame refusals are not strictly ordered");
		}
		first = false;
		previousSlot = refusal.slot;
		previousId = refusal.id;
		if (!ids.emplace(refusal.slot, refusal.id).second)
		{
			return fail(error, "duplicate canonical frame identifier");
		}
		if (refusal.code == RefusalCode::TooLarge)
		{
			if (refusal.declaredBytes <= kMaxOrderBytes)
			{
				return fail(error, "canonical too-large refusal has an admissible size");
			}
		}
		else if (refusal.code == RefusalCode::Empty)
		{
			if (refusal.declaredBytes != 0)
			{
				return fail(error, "canonical empty refusal has a nonzero size");
			}
		}
		else
		{
			return fail(error, "canonical frame refusal code is invalid");
		}
	}
	return true;
}

bool validateStructure(const Frame &frame, std::string &error)
{
	if (frame.orders.size() > 4096 || frame.deferrals.size() > 4096 || frame.refusals.size() > 4096)
	{
		return fail(error, "canonical frame entry count exceeds its bound");
	}
	if (!validOrderList(frame, error))
	{
		return false;
	}
	std::set<std::pair<std::uint8_t, std::uint64_t>> ids;
	for (const Order &order : frame.orders)
	{
		ids.emplace(order.slot, order.id);
	}
	if (!validDeferralList(frame, ids, error) || !validRefusalList(frame, ids, error))
	{
		return false;
	}
	return true;
}

bool encodedSize(const Frame &frame, std::size_t &size, std::string &error)
{
	size = kHeaderBytes;
	for (const Order &order : frame.orders)
	{
		if (!addSize(size, kOrderFixedBytes, size) || !addSize(size, order.bytes.size(), size))
		{
			return fail(error, "canonical frame encoded size overflows");
		}
	}
	std::size_t deferralBytes = 0;
	if (!multiplySize(frame.deferrals.size(), kDeferralBytes, deferralBytes) || !addSize(size, deferralBytes, size))
	{
		return fail(error, "canonical frame encoded size overflows");
	}
	std::size_t refusalBytes = 0;
	if (!multiplySize(frame.refusals.size(), kRefusalBytes, refusalBytes)
		|| !addSize(size, refusalBytes, size) || size > kMaxFrameBytes)
	{
		return fail(error, "canonical frame exceeds its byte bound");
	}
	return true;
}

bool sameStamp(const RelayCommit::Stamp &left, const RelayCommit::Stamp &right)
{
	return left.game == right.game && left.epoch == right.epoch && left.sequence == right.sequence
		&& left.frame == right.frame && left.membershipGeneration == right.membershipGeneration;
}

bool sameMembership(const RelayMembership::Snapshot &left, const RelayMembership::Snapshot &right)
{
	return left.slotMask == right.slotMask && left.active == right.active && left.held == right.held
		&& left.departed == right.departed && left.loaded == right.loaded && left.ticketGenerations == right.ticketGenerations
		&& left.paused == right.paused && left.clockValid == right.clockValid && left.originMs == right.originMs
		&& left.firstFrame == right.firstFrame;
}

} // namespace

const char *refusalMessage(RefusalCode code)
{
	switch (code)
	{
	case RefusalCode::TooLarge:
		return "canonical order refused: serialized GameMessage exceeds 65536 bytes";
	case RefusalCode::Empty:
		return "canonical order refused: serialized GameMessage is empty";
	}
	return "canonical order refused";
}

bool encode(const Frame &frame, Bytes &bytes, std::string &error)
{
	if (!validateStructure(frame, error))
	{
		return false;
	}
	std::size_t size = 0;
	if (!encodedSize(frame, size, error))
	{
		return false;
	}
	Bytes out;
	out.reserve(size);
	out.insert(out.end(), { 'O', 'B', 'F', 'B' });
	put(out, kVersion, 2);
	put(out, frame.orders.size(), 4);
	put(out, frame.deferrals.size(), 4);
	put(out, frame.refusals.size(), 4);
	put(out, 0, 4); // reserved
	for (const Order &order : frame.orders)
	{
		put(out, order.slot, 1);
		put(out, order.id, 8);
		put(out, order.bytes.size(), 4);
		out.insert(out.end(), order.bytes.begin(), order.bytes.end());
	}
	for (const Deferral &deferral : frame.deferrals)
	{
		put(out, deferral.slot, 1);
		put(out, deferral.id, 8);
		put(out, deferral.fromFrame, 4);
		put(out, deferral.toFrame, 4);
		put(out, deferral.orderBytes, 4);
		out.insert(out.end(), deferral.digest.begin(), deferral.digest.end());
		put(out, static_cast<std::uint8_t>(deferral.reason), 1);
	}
	for (const Refusal &refusal : frame.refusals)
	{
		put(out, refusal.slot, 1);
		put(out, refusal.id, 8);
		put(out, refusal.declaredBytes, 8);
		put(out, static_cast<std::uint8_t>(refusal.code), 1);
	}
	bytes.swap(out);
	error.clear();
	return true;
}

bool decode(const Bytes &bytes, Frame &frame, std::string &error)
{
	if (bytes.size() < kHeaderBytes || bytes.size() > kMaxFrameBytes
		|| bytes[0] != 'O' || bytes[1] != 'B' || bytes[2] != 'F' || bytes[3] != 'B'
		|| get(bytes, 4, 2) != kVersion)
	{
		return fail(error, "unsupported or malformed canonical frame header");
	}
	const std::uint64_t orderCount = get(bytes, 6, 4);
	const std::uint64_t deferralCount = get(bytes, 10, 4);
	const std::uint64_t refusalCount = get(bytes, 14, 4);
	if (orderCount > 4096 || deferralCount > 4096 || refusalCount > 4096)
	{
		return fail(error, "canonical frame entry count exceeds its bound");
	}
	if (get(bytes, 18, 4) != 0)
	{
		return fail(error, "canonical frame reserved field is not zero");
	}
	std::size_t minimum = kHeaderBytes;
	std::size_t orderMinimum = 0, deferralMinimum = 0, refusalMinimum = 0;
	if (!multiplySize(static_cast<std::size_t>(orderCount), kOrderFixedBytes, orderMinimum)
		|| !multiplySize(static_cast<std::size_t>(deferralCount), kDeferralBytes, deferralMinimum)
		|| !multiplySize(static_cast<std::size_t>(refusalCount), kRefusalBytes, refusalMinimum)
		|| !addSize(minimum, orderMinimum, minimum)
		|| !addSize(minimum, deferralMinimum, minimum)
		|| !addSize(minimum, refusalMinimum, minimum)
		|| minimum > bytes.size())
	{
		return fail(error, "canonical frame entry lengths exceed packet bytes");
	}
	Frame out;
	out.orders.reserve(static_cast<std::size_t>(orderCount));
	out.deferrals.reserve(static_cast<std::size_t>(deferralCount));
	out.refusals.reserve(static_cast<std::size_t>(refusalCount));
	std::size_t offset = kHeaderBytes;
	for (std::uint64_t i = 0; i != orderCount; ++i)
	{
		const std::size_t fixedEnd = offset + kOrderFixedBytes;
		if (fixedEnd > bytes.size())
		{
			return fail(error, "truncated canonical frame order header");
		}
		Order order;
		order.slot = bytes[offset];
		order.id = get(bytes, offset + 1, 8);
		const std::uint64_t orderBytes = get(bytes, offset + 9, 4);
		if (orderBytes == 0 || orderBytes > kMaxOrderBytes || orderBytes > bytes.size() - fixedEnd)
		{
			return fail(error, "invalid canonical frame order length");
		}
		order.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(fixedEnd),
			bytes.begin() + static_cast<std::ptrdiff_t>(fixedEnd + orderBytes));
		out.orders.push_back(std::move(order));
		offset = fixedEnd + static_cast<std::size_t>(orderBytes);
	}
	for (std::uint64_t i = 0; i != deferralCount; ++i)
	{
		if (offset > bytes.size() || kDeferralBytes > bytes.size() - offset)
		{
			return fail(error, "truncated canonical frame deferral");
		}
		Deferral deferral;
		deferral.slot = bytes[offset];
		deferral.id = get(bytes, offset + 1, 8);
		deferral.fromFrame = static_cast<std::uint32_t>(get(bytes, offset + 9, 4));
		deferral.toFrame = static_cast<std::uint32_t>(get(bytes, offset + 13, 4));
		deferral.orderBytes = static_cast<std::uint32_t>(get(bytes, offset + 17, 4));
		std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 21),
			bytes.begin() + static_cast<std::ptrdiff_t>(offset + 53), deferral.digest.begin());
		deferral.reason = static_cast<DeferralReason>(bytes[offset + 53]);
		out.deferrals.push_back(deferral);
		offset += kDeferralBytes;
	}
	for (std::uint64_t i = 0; i != refusalCount; ++i)
	{
		if (offset > bytes.size() || kRefusalBytes > bytes.size() - offset)
		{
			return fail(error, "truncated canonical frame refusal");
		}
		Refusal refusal;
		refusal.slot = bytes[offset];
		refusal.id = get(bytes, offset + 1, 8);
		refusal.declaredBytes = get(bytes, offset + 9, 8);
		refusal.code = static_cast<RefusalCode>(bytes[offset + 17]);
		out.refusals.push_back(refusal);
		offset += kRefusalBytes;
	}
	if (offset != bytes.size())
	{
		return fail(error, "canonical frame has trailing bytes");
	}
	if (!validateStructure(out, error))
	{
		return false;
	}
	frame = std::move(out);
	error.clear();
	return true;
}

bool validate(const Frame &frame, const RelayCommit::Stamp &stamp, const RelayMembership::Snapshot &membership, std::string &error)
{
	if (!validateStructure(frame, error))
	{
		return false;
	}
	Bytes encodedMembership;
	if (!RelayMembership::encode(membership, encodedMembership, error))
	{
		return false;
	}
	if (!stamp.game || !stamp.epoch || !stamp.sequence || !stamp.membershipGeneration || !membership.slotMask)
	{
		return fail(error, "canonical frame stamp or membership is incomplete");
	}
	if (!frame.orders.empty() && (membership.paused || membership.loaded != membership.active || !membership.clockValid))
	{
		return fail(error, "canonical frame orders require an active fully loaded clock");
	}
	for (const Order &order : frame.orders)
	{
		if (!(membership.slotMask & (std::uint16_t(1) << order.slot)) || !(membership.active & (std::uint16_t(1) << order.slot)))
		{
			return fail(error, "canonical frame order names an inactive slot");
		}
	}
	for (const Deferral &deferral : frame.deferrals)
	{
		if (!(membership.slotMask & (std::uint16_t(1) << deferral.slot))
			|| !(membership.active & (std::uint16_t(1) << deferral.slot)) || deferral.fromFrame != stamp.frame)
		{
			return fail(error, "canonical frame deferral names an invalid slot or frame");
		}
	}
	for (const Refusal &refusal : frame.refusals)
	{
		if (!(membership.slotMask & (std::uint16_t(1) << refusal.slot)))
		{
			return fail(error, "canonical frame refusal names an original slot outside membership");
		}
	}
	error.clear();
	return true;
}

Builder::Builder(std::uint64_t game, std::uint64_t epoch, std::uint16_t slotMask, std::uint32_t firstFrame)
	: m_game(game), m_epoch(epoch), m_slotMask(slotMask), m_nextFrame(firstFrame)
{
	if (!game || !epoch || !slotMask || firstFrame == std::numeric_limits<std::uint32_t>::max())
	{
		m_failure = "canonical frame builder constructor arguments are invalid";
	}
}

Builder::Admission Builder::admit(std::uint8_t slot, std::uint64_t id, std::uint32_t earliestFrame,
	const std::uint8_t *data, std::size_t size, std::string &error)
{
	if (!m_failure.empty())
	{
		error = m_failure;
		return Admission::Terminal;
	}
	if (!validSlot(slot) || !(m_slotMask & (std::uint16_t(1) << slot)))
	{
		error = "canonical frame admission slot is not in the original mask";
		return Admission::InvalidSlot;
	}
	if (!id || id == std::numeric_limits<std::uint64_t>::max() || id != m_slots[slot].nextId)
	{
		error = "canonical frame admission id is not the next slot id";
		return Admission::InvalidSequence;
	}
	const std::uint32_t floor = nextUnpublishedFrame();
	if (floor == std::numeric_limits<std::uint32_t>::max())
	{
		error = "canonical frame builder cannot admit after frame exhaustion";
		return Admission::Terminal;
	}
	RefusalCode refusalCode;
	if (size == 0)
	{
		refusalCode = RefusalCode::Empty;
	}
	else if (size > kMaxOrderBytes)
	{
		refusalCode = RefusalCode::TooLarge;
	}
	else
	{
		if (earliestFrame == std::numeric_limits<std::uint32_t>::max()
			|| earliestFrame < floor
			|| (!m_slots[slot].orders.empty() && earliestFrame < m_slots[slot].orders.back().eligibleFrame))
		{
			error = "canonical frame admission eligibility is out of order";
			return Admission::InvalidFrame;
		}
	}
	if (size > 0 && size <= kMaxOrderBytes)
	{
		if (!data)
		{
			error = "canonical frame admission payload is null";
			return Admission::InvalidPayload;
		}
		if (m_slots[slot].orders.size() >= kMaxPendingOrdersPerSlot
			|| size > kMaxPendingBytesPerSlot - m_slots[slot].bytes)
		{
			error = "canonical frame admission queue is busy";
			return Admission::Busy;
		}
		Pending pending;
		pending.order.slot = slot;
		pending.order.id = id;
		pending.order.bytes.assign(data, data + size);
		pending.eligibleFrame = earliestFrame;
		m_slots[slot].orders.push_back(std::move(pending));
		m_slots[slot].bytes += size;
		++m_slots[slot].nextId;
		error.clear();
		return Admission::Accepted;
	}
	if (m_slots[slot].refusals.size() >= kMaxPendingOrdersPerSlot)
	{
		error = "canonical frame refusal audit queue is busy";
		return Admission::Busy;
	}
	Refusal refusal;
	refusal.slot = slot;
	refusal.id = id;
	refusal.declaredBytes = size;
	refusal.code = refusalCode;
	m_slots[slot].refusals.push_back(refusal);
	++m_slots[slot].nextId;
	error = refusalMessage(refusalCode);
	return Admission::Malformed;
}

bool Builder::prepare(const RelayCommit::Stamp &stamp, const RelayMembership::Snapshot &membership,
	RelayCommit::Record &record, std::string &error)
{
	if (!m_failure.empty())
	{
		error = m_failure;
		return false;
	}
	if (m_prepared)
	{
		if (!sameStamp(stamp, m_record.stamp) || !sameMembership(membership, m_membership))
		{
			error = "canonical frame builder has a different frame staged";
			return false;
		}
		record = m_record;
		error.clear();
		return true;
	}
	if (stamp.game != m_game || stamp.epoch != m_epoch || stamp.frame != m_nextFrame
		|| stamp.frame == std::numeric_limits<std::uint32_t>::max()
		|| !stamp.sequence || !stamp.membershipGeneration || membership.slotMask != m_slotMask)
	{
		error = "canonical frame builder stamp or membership does not match";
		return false;
	}
	Frame frame;
	std::array<std::size_t, kSlots> consumed{}, deferred{}, refused{};
	Counters delta;
	for (std::size_t slot = 0; slot != kSlots; ++slot)
	{
		const Slot &state = m_slots[slot];
		const bool active = (membership.active & (std::uint16_t(1) << slot)) != 0;
		std::size_t used = 0;
		bool oversized = false;
		for (const Pending &pending : state.orders)
		{
			if (!active)
			{
				break;
			}
			if (pending.eligibleFrame > m_nextFrame)
			{
				break;
			}
			if (pending.order.bytes.size() > kPlayerFrameBytes)
			{
				if (consumed[slot] == 0)
				{
					frame.orders.push_back(pending.order);
					++consumed[slot];
					++delta.oversizedOrders;
					++delta.oversizedPlayerFrames;
					oversized = true;
				}
				break;
			}
			if (pending.order.bytes.size() > kPlayerFrameBytes - used)
			{
				break;
			}
			frame.orders.push_back(pending.order);
			used += pending.order.bytes.size();
			++consumed[slot];
			++delta.ordinaryOrders;
		}
		if (consumed[slot] && !oversized)
		{
			++delta.normalPlayerFrames;
		}
		std::size_t end = consumed[slot];
		while (active && end < state.orders.size() && state.orders[end].eligibleFrame <= m_nextFrame)
		{
			const Pending &pending = state.orders[end];
			Deferral deferral;
			deferral.slot = pending.order.slot;
			deferral.id = pending.order.id;
			deferral.fromFrame = m_nextFrame;
			deferral.toFrame = m_nextFrame + 1;
			deferral.orderBytes = static_cast<std::uint32_t>(pending.order.bytes.size());
			deferral.digest = Sha256::hash(pending.order.bytes.data(), pending.order.bytes.size());
			deferral.reason = oversized ? DeferralReason::OversizedAlone : DeferralReason::Budget;
			frame.deferrals.push_back(deferral);
			++end;
			++deferred[slot];
			++delta.deferredOrderFrames;
		}
		for (const Refusal &refusal : state.refusals)
		{
			frame.refusals.push_back(refusal);
			++refused[slot];
			++delta.refusedOrders;
		}
	}
	RelayCommit::Record candidate;
	candidate.stamp = stamp;
	candidate.kind = RelayCommit::Kind::Frame;
	if (!encode(frame, candidate.payload, error) || !validate(frame, stamp, membership, error))
	{
		return false;
	}
	record = candidate;
	m_record = candidate;
	m_membership = membership;
	m_consumed = consumed;
	m_deferred = deferred;
	m_refused = refused;
	m_delta = delta;
	m_prepared = true;
	error.clear();
	return true;
}

bool Builder::committed(const RelayCommit::Stamp &stamp, std::string &error)
{
	if (!m_failure.empty())
	{
		error = m_failure;
		return false;
	}
	if (!m_prepared || !sameStamp(stamp, m_record.stamp))
	{
		error = "canonical frame builder has no matching prepared record";
		return false;
	}
	for (std::size_t slot = 0; slot != kSlots; ++slot)
	{
		Slot &state = m_slots[slot];
		for (std::size_t i = 0; i != m_consumed[slot]; ++i)
		{
			state.bytes -= state.orders.front().order.bytes.size();
			state.orders.pop_front();
		}
		for (std::size_t i = 0; i != m_deferred[slot]; ++i)
		{
			state.orders[i].eligibleFrame = m_nextFrame + 1;
		}
		for (std::size_t i = 0; i != m_refused[slot]; ++i)
		{
			state.refusals.pop_front();
		}
	}
	m_counters.ordinaryOrders += m_delta.ordinaryOrders;
	m_counters.oversizedOrders += m_delta.oversizedOrders;
	m_counters.normalPlayerFrames += m_delta.normalPlayerFrames;
	m_counters.oversizedPlayerFrames += m_delta.oversizedPlayerFrames;
	m_counters.deferredOrderFrames += m_delta.deferredOrderFrames;
	m_counters.refusedOrders += m_delta.refusedOrders;
	++m_nextFrame;
	m_prepared = false;
	m_record = RelayCommit::Record{};
	m_membership = RelayMembership::Snapshot{};
	m_consumed.fill(0);
	m_deferred.fill(0);
	m_refused.fill(0);
	m_delta = Counters{};
	error.clear();
	return true;
}

void Builder::fail(const std::string &error)
{
	if (m_failure.empty())
	{
		m_failure = error.empty() ? "canonical frame builder log append failure" : error;
	}
}

std::uint32_t Builder::nextUnpublishedFrame() const
{
	if (m_prepared)
	{
		return m_nextFrame == std::numeric_limits<std::uint32_t>::max() ? m_nextFrame : m_nextFrame + 1;
	}
	return m_nextFrame;
}

std::size_t Builder::pendingOrders(std::uint8_t slot) const
{
	return validSlot(slot) ? m_slots[slot].orders.size() : 0;
}

std::size_t Builder::pendingBytes(std::uint8_t slot) const
{
	return validSlot(slot) ? m_slots[slot].bytes : 0;
}
} // namespace RelayFrameBundle
