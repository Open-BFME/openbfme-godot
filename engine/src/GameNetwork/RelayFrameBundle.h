// OpenBFME. GPL-3.0.
// Enhanced canonical frame payload; not the legacy NetPacket frame format.
#pragma once

#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayMembership.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace RelayFrameBundle
{
constexpr std::size_t kPlayerFrameBytes = 16u * 1024u;
constexpr std::size_t kMaxOrderBytes = 64u * 1024u;
constexpr std::size_t kMaxPendingOrdersPerSlot = 256;
constexpr std::size_t kMaxPendingBytesPerSlot = 1u << 20;
constexpr std::size_t kMaxFrameBytes = 2u << 20;
constexpr std::size_t kSlots = 16;
using Bytes = std::vector<std::uint8_t>;

struct Order
{
	std::uint8_t slot = 0;
	std::uint64_t id = 0;
	// Complete serialized GameMessage, including its type/player/argument fields.
	// Budget excludes canonical/fragment headers. Adapters validate the message
	// and bind its player to the authenticated slot; the codec treats it as opaque.
	Bytes bytes;
};

enum class DeferralReason : std::uint8_t { Budget = 1, OversizedAlone = 2 };
struct Deferral
{
	std::uint8_t slot = 0;
	std::uint64_t id = 0;
	std::uint32_t fromFrame = 0, toFrame = 0;
	std::uint32_t orderBytes = 0;
	std::array<std::uint8_t, 32> digest{};
	DeferralReason reason = DeferralReason::Budget;
};

enum class RefusalCode : std::uint8_t { TooLarge = 1, Empty = 2 };
struct Refusal
{
	std::uint8_t slot = 0;
	std::uint64_t id = 0;
	std::uint64_t declaredBytes = 0;
	RefusalCode code = RefusalCode::TooLarge;
};
const char *refusalMessage(RefusalCode code);

struct Frame
{
	std::vector<Order> orders;
	std::vector<Deferral> deferrals;
	std::vector<Refusal> refusals;
};

// OBFB v2, little endian (the header's u32 at offset 18 is reserved and must be 0). Structural/budget validation is mandatory; caller
// supplies the enclosing committed stamp and membership for semantic validation.
bool encode(const Frame &frame, Bytes &bytes, std::string &error);
bool decode(const Bytes &bytes, Frame &frame, std::string &error);
bool validate(const Frame &frame, const RelayCommit::Stamp &stamp,
	const RelayMembership::Snapshot &membership, std::string &error);

class Builder
{
public:
	enum class Admission { Accepted, Malformed, Busy, InvalidSlot, InvalidSequence, InvalidFrame, InvalidPayload, Terminal };
	struct Counters
	{
		std::uint64_t ordinaryOrders = 0, oversizedOrders = 0;
		std::uint64_t normalPlayerFrames = 0, oversizedPlayerFrames = 0;
		std::uint64_t deferredOrderFrames = 0, refusedOrders = 0;
	};
	Builder(std::uint64_t game, std::uint64_t epoch, std::uint16_t slotMask, std::uint32_t firstFrame);

	// id starts at 1 per authenticated slot. Accepted and Malformed consume it;
	// Busy does not. Size is checked before dereferencing or copying data.
	// Malformed queues a bounded canonical refusal; the return/error is the
	// sender-only response. A full audit queue backpressures just that slot.
	Admission admit(std::uint8_t slot, std::uint64_t id, std::uint32_t earliestFrame,
		const std::uint8_t *data, std::size_t size, std::string &error);

	// One immutable in-flight frame. Admission may continue for later frames.
	// prepare never releases inputs; only a successful game-log append may call
	// committed. On append failure call fail; neither publish nor execute.
	bool prepare(const RelayCommit::Stamp &stamp, const RelayMembership::Snapshot &membership,
		RelayCommit::Record &record, std::string &error);
	bool committed(const RelayCommit::Stamp &stamp, std::string &error);
	void fail(const std::string &error);
	std::uint32_t nextUnpublishedFrame() const;
	const Counters &counters() const { return m_counters; }
	std::size_t pendingOrders(std::uint8_t slot) const;
	std::size_t pendingBytes(std::uint8_t slot) const;

private:
	struct Pending
	{
		Order order;
		std::uint32_t eligibleFrame = 0;
	};
	struct Slot
	{
		std::uint64_t nextId = 1;
		std::deque<Pending> orders;
		std::deque<Refusal> refusals;
		std::size_t bytes = 0;
	};
	std::uint64_t m_game, m_epoch;
	std::uint16_t m_slotMask;
	std::uint32_t m_nextFrame;
	std::array<Slot, kSlots> m_slots;
	bool m_prepared = false;
	std::string m_failure;
	RelayCommit::Record m_record;
	RelayMembership::Snapshot m_membership;
	std::array<std::size_t, kSlots> m_consumed{}, m_deferred{}, m_refused{};
	Counters m_counters, m_delta;
};
}
