// OpenBFME. GPL-3.0.
//
// NET-SYNC canonical ingress: the client -> relay input wire of a canonical
// (relay-keeps-the-beat) game. Each message travels as one complete message of
// the caller's reliable channel (RelayBundleChannel) to the slot's
// authenticated binding; it never carries the sender's slot: the binding
// supplies it, the payload is not trusted for identity.
//
// OBCI v1, little endian:
//   0  u8[4]  magic "OBCI"
//   4  u8     version (1)
//   5  u8     kind (1 order, 2 beat; 3 is reserved)
//   6  u64    id (dense per slot, starts at 1; a beat id ends a window: the
//              slot's input for that window is complete — the FRAMEINFO count
//              equivalent, which equal-delay sealing waits on)
//   14 u32    serialized GameMessage bytes (kind 1; always 0 for a beat),
//             then the bytes
//
// The order's declared size is bounded by the canonical budget
// (RelayFrameBundle::kMaxOrderBytes) and is checked before any copy: ingress
// enforcement must also happen BEFORE reassembly by configuring the inbound
// channel's maxMessageBytes to kMaxWireBytes (a first fragment's declared
// total beyond it is refused without allocating the message).
//
// Not simulation code: the order bytes stay opaque here; the adapter binds
// the player to the authenticated slot and the scheduler stamps the frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RelayCanonicalInput
{
constexpr std::size_t kHeaderBytes = 18;
// The largest valid inbound message: one maximum-size order.
constexpr std::size_t kMaxWireBytes = kHeaderBytes + (64u * 1024u);

enum class Kind : std::uint8_t { Order = 1, Beat = 2 };

struct Message
{
	Kind kind = Kind::Order;
	std::uint64_t id = 0;               ///< order id or beat id, dense per slot
	std::vector<std::uint8_t> bytes;    ///< order: the complete serialized GameMessage
};

// false + error on a bad header, unknown kind, a beat with a nonzero length,
// an order id or declared size of zero, a declared size over the budget or one
// that does not match the message length, or trailing bytes. Nothing is
// copied before the declared size is validated.
bool encode(const Message &message, std::vector<std::uint8_t> &bytes, std::string &error);
bool decode(const std::vector<std::uint8_t> &bytes, Message &message, std::string &error);
bool decode(const std::uint8_t *data, std::size_t size, Message &message, std::string &error);
}
