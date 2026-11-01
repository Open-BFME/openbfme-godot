// OpenBFME. GPL-3.0.
//
// RelayAuth (lane AUTH-1, docs/ONLINE.md "per-packet authentication"): the key material, tag, counter and replay
// window of the authenticated relay envelope (version 5; RelayProtocol.h).
//
// A datagram authenticated per AUTH-1 is the version-4 envelope with the version byte 5, a per-direction 64-bit
// counter after the kind byte, and a truncated 16-byte MAC over the whole datagram (header, counter, payload):
//   'B' 'R' 5 kind | u64 counter (little-endian) | <the kind's v4 payload> | tag[16]
//
// Key material (the secret never crosses UDP):
//   session key     = the first 16 bytes of HMAC-SHA256(ticketKey[keyId], "OBFME-SESSION-1" || the exact ticket bytes) (ONLINE16-FIX: the one
//                     scheme with ANTICHEAT-1; its derivation and pin). The lobby computes
//                     it and hands it to the player over HTTPS in the START ("session_key", hex; ONLINE16-FIX: the one
//                     packet-authentication scheme, which subsumes ANTICHEAT-1's signed input); the relay recomputes
//                     the same k from the ticket in the HELLO plus its own copy of the ticket key: nothing new is
//                     stored and k is never on the wire (an on-path reader of HELLO + WELCOME learns the ticket and
//                     the session token but not k).
//   c2r / r2c keys  = HKDF-SHA256 (expand-only: k is already uniform) with info = label || u64 gameId || u8 slot ||
//                     u16 generation, label "obfme-relay-c2r" / "obfme-relay-r2c". A rejoin ticket carries the fresh
//                     generation, so a rejoin's k and direction keys are its own.
// The packet MAC is Monocypher's keyed BLAKE2b truncated to 16 bytes — measured ~4x cheaper than HMAC-SHA256 on the
// packet sizes the game sends (the AUTH-1 cost test in test_relay_auth.cpp). The session token stays only the
// rebind credential; it is no longer key material.
//
// Not simulation code: it decides whose bytes are real, never what a frame contains.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RelayAuth
{
constexpr size_t kKeyBytes = 32;      // the direction keys
constexpr size_t kSessionKeyBytes = 16; // the per-slot secret: the START's "session_key" (base64url)
constexpr size_t kTagBytes = 16;      // the truncated tag appended to a v5 datagram
constexpr size_t kCounterBytes = 8;   // the per-direction sequence number in the v5 header
constexpr size_t kOverhead = kCounterBytes + kTagBytes; // the bytes the v5 envelope adds to a v4 one

using Key = std::array<std::uint8_t, kKeyBytes>;
using SessionKey = std::array<std::uint8_t, kSessionKeyBytes>;

// the per-slot secret, from the ticket key and the exact ticket bytes (lobby = relay)
SessionKey sessionKey(const std::uint8_t *ticketKey, size_t keySize, const std::uint8_t *ticket, size_t ticketBytes);

struct Keys
{
	Key c2r{}, r2c{}; // client->relay / relay->client packet keys
};
// `slot`: the ticket's slot byte (a spectator ticket's index); `generation`: the ticket's generation field (0 on
// player START and spectator tickets)
Keys packetKeys(const SessionKey &sessionKey, std::uint64_t gameId, std::uint8_t slot, std::uint16_t generation);

// the per-packet tag: keyed BLAKE2b-16 over the whole datagram minus the tag field
void tag(const Key &key, const std::uint8_t *data, size_t size, std::uint8_t *out);
bool tagEqual(const std::uint8_t *a, const std::uint8_t *b); // constant-time compare of two kTagBytes tags (Monocypher crypto_verify16)

// the replay window (IPsec-style): a counter ahead of the newest seen is accepted and slides it; within it a set
// bit is a replay, behind it the datagram is too old. ONLINE16-FIX (AUTH-1 review 5): 256 counters wide — 64 covered
// only about 130-400 ms of the relay's datagrams into a client of a 16-slot game, so ordinary reordering dropped
struct Window
{
	static constexpr std::uint64_t kSize = 256;
	std::uint64_t newest = 0;
	std::array<std::uint64_t, kSize / 64> bits{}; // bit i (word i / 64): counter newest - i arrived (bit 0 = newest)
	bool haveAny = false;
	bool accept(std::uint64_t counter); // false: a replay or older than the window
};

// turns a complete version-4 datagram (RelayProtocol::encode*) into the version-5 one: the counter goes in after
// the kind byte, the tag is appended. `counter` is the sender's per-direction sequence number.
void seal(std::vector<std::uint8_t> &datagram, const Key &key, std::uint64_t counter);
// the reverse, in place: verifies the tag, admits the counter to the window (window may be null: checked only),
// then compacts the datagram to its version-4 view (the counter and tag go, the version byte becomes 4) and
// returns the compacted size; 0 when the tag or the window fails
size_t open(std::uint8_t *datagram, size_t size, const Key &key, Window *window);
} // namespace RelayAuth
