// OpenBFME. GPL-3.0.
// See GameNetwork/RelayAuth.h.

#include "GameNetwork/RelayAuth.h"

#include "Common/Sha256.h"

#include <cstring>

extern "C" {
#include "monocypher/monocypher.h"
}

namespace RelayAuth
{
namespace
{
void put64(std::vector<std::uint8_t> &b, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i)
	{
		b.push_back((std::uint8_t)(v >> (8 * i)));
	}
}
} // namespace

SessionKey sessionKey(const std::uint8_t *ticketKey, size_t keySize, const std::uint8_t *ticket, size_t ticketBytes)
{
	// ONLINE16-FIX: ANTICHEAT-1's session key, the lobby's START "session_key": HMAC-SHA256(ticketKey, "OBFME-SESSION-1" || ticket)[0 .. 16)
	static constexpr char label[] = "OBFME-SESSION-1";
	// ONLINE16-FIX (AUTH-1 review 6): the ticket's own MAC is HMAC(ticketKey, ticket body), and a ticket body starts with
	// its version byte; a label whose first byte is never a ticket version keeps the two HMAC inputs apart
	static_assert((unsigned char)label[0] > 8, "the k label must not start with a ticket version byte (RelayProtocol ticketSize)");
	std::vector<std::uint8_t> msg(label, label + sizeof(label) - 1);
	msg.insert(msg.end(), ticket, ticket + ticketBytes);
	SessionKey k;
	const std::array<std::uint8_t, 32> h = HmacSha256(ticketKey, keySize, msg.data(), msg.size());
	std::memcpy(k.data(), h.data(), k.size());
	return k;
}

Keys packetKeys(const SessionKey &macKey_, std::uint64_t gameId, std::uint8_t slot, std::uint16_t generation)
{
	// HKDF-SHA256 expand-only (RFC 5869 §3.3: skip the extract when the input key material is already uniform, the session key is
	// 16 bytes of an HMAC output): T(1) = HMAC(prk, info || 0x01) is the whole 32-byte key; info = label || ids as documented
	Keys out;
	for (int direction = 0; direction < 2; ++direction)
	{
		static const char *const labels[2] = { "obfme-relay-c2r", "obfme-relay-r2c" };
		std::vector<std::uint8_t> info(labels[direction], labels[direction] + 15);
		for (int i = 0; i < 8; ++i)
		{
			info.push_back((std::uint8_t)(gameId >> (8 * i)));
		}
		info.push_back(slot);
		info.push_back((std::uint8_t)generation);
		info.push_back((std::uint8_t)(generation >> 8));
		info.push_back(1); // the HKDF block counter
		const std::array<std::uint8_t, 32> h = HmacSha256(macKey_.data(), macKey_.size(), info.data(), info.size());
		std::memcpy(direction == 0 ? out.c2r.data() : out.r2c.data(), h.data(), out.c2r.size());
	}
	return out;
}

void tag(const Key &key, const std::uint8_t *data, size_t size, std::uint8_t *out)
{
	crypto_blake2b_keyed(out, kTagBytes, key.data(), key.size(), data, size);
}

bool tagEqual(const std::uint8_t *a, const std::uint8_t *b)
{
	static_assert(kTagBytes == 16, "crypto_verify16 compares 16 bytes");
	return crypto_verify16(a, b) == 0;
}

bool Window::accept(std::uint64_t counter)
{
	constexpr size_t words = kSize / 64;
	if (!haveAny)
	{
		newest = counter;
		bits = {};
		bits[0] = 1;
		haveAny = true;
		return true;
	}
	if (counter > newest)
	{
		// the window slides by `shift` counters: the bitmap shifts towards the older end (word 0 bit 0 is the newest)
		const std::uint64_t shift = counter - newest;
		std::array<std::uint64_t, words> moved{};
		if (shift < kSize)
		{
			const size_t wordShift = (size_t)(shift / 64), bitShift = (size_t)(shift % 64);
			for (size_t i = words; i-- > wordShift;)
			{
				const size_t from = i - wordShift;
				moved[i] = bits[from] << bitShift;
				if (bitShift && from > 0)
				{
					moved[i] |= bits[from - 1] >> (64 - bitShift);
				}
			}
		}
		bits = moved;
		bits[0] |= 1;
		newest = counter;
		return true;
	}
	const std::uint64_t back = newest - counter;
	if (back >= kSize)
	{
		return false;
	}
	std::uint64_t &word = bits[(size_t)(back / 64)];
	const std::uint64_t bit = 1ull << (back % 64);
	if (word & bit)
	{
		return false;
	}
	word |= bit;
	return true;
}

void seal(std::vector<std::uint8_t> &datagram, const Key &key, std::uint64_t counter)
{
	if (datagram.size() < 4)
	{
		return; // never reached: every caller hands a complete envelope
	}
	datagram[2] = 5; // RelayProtocol::kVersionAuth
	std::vector<std::uint8_t> c;
	c.reserve(kCounterBytes);
	put64(c, counter);
	datagram.insert(datagram.begin() + 4, c.begin(), c.end());
	std::uint8_t t[kTagBytes];
	tag(key, datagram.data(), datagram.size(), t);
	datagram.insert(datagram.end(), t, t + kTagBytes);
}

size_t open(std::uint8_t *datagram, size_t size, const Key &key, Window *window)
{
	if (size < 4 + kCounterBytes + kTagBytes || datagram[2] != 5)
	{
		return 0;
	}
	std::uint8_t want[kTagBytes];
	tag(key, datagram, size - kTagBytes, want);
	if (!tagEqual(want, datagram + size - kTagBytes))
	{
		return 0;
	}
	std::uint64_t counter = 0;
	for (int i = 0; i < 8; ++i)
	{
		counter |= (std::uint64_t)datagram[4 + i] << (8 * i);
	}
	if (window && !window->accept(counter))
	{
		return 0;
	}
	const size_t payload = size - 4 - kCounterBytes - kTagBytes;
	std::memmove(datagram + 4, datagram + 4 + kCounterBytes, payload);
	datagram[2] = 4; // RelayProtocol::kVersion
	return 4 + payload;
}
} // namespace RelayAuth
