// OpenBFME. GPL-3.0.
// See GameNetwork/RelayProtocol.h.

#include "GameNetwork/RelayProtocol.h"

#include "Common/Sha256.h"

#include <cstring>
#include <set>
#include <sstream>

namespace RelayProtocol
{
namespace
{
const char kUrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
const char kStdAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeWith(const char *alphabet, const std::uint8_t *data, size_t size, bool pad)
{
	std::string s;
	s.reserve((size + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < size; i += 3)
	{
		const std::uint32_t v = ((std::uint32_t)data[i] << 16) | ((std::uint32_t)data[i + 1] << 8) | data[i + 2];
		s.push_back(alphabet[(v >> 18) & 63]);
		s.push_back(alphabet[(v >> 12) & 63]);
		s.push_back(alphabet[(v >> 6) & 63]);
		s.push_back(alphabet[v & 63]);
	}
	if (size - i == 1)
	{
		const std::uint32_t v = (std::uint32_t)data[i] << 16;
		s.push_back(alphabet[(v >> 18) & 63]);
		s.push_back(alphabet[(v >> 12) & 63]);
		if (pad)
		{
			s += "==";
		}
	}
	else if (size - i == 2)
	{
		const std::uint32_t v = ((std::uint32_t)data[i] << 16) | ((std::uint32_t)data[i + 1] << 8);
		s.push_back(alphabet[(v >> 18) & 63]);
		s.push_back(alphabet[(v >> 12) & 63]);
		s.push_back(alphabet[(v >> 6) & 63]);
		if (pad)
		{
			s += "=";
		}
	}
	return s;
}

bool decodeWith(const char *alphabet, const std::string &text, bool padded, std::vector<std::uint8_t> &out)
{
	std::string t = text;
	if (padded)
	{
		if (t.size() % 4 != 0)
		{
			return false;
		}
		size_t pads = 0;
		while (!t.empty() && t.back() == '=' && pads < 2)
		{
			t.pop_back();
			++pads;
		}
	}
	if (t.size() % 4 == 1)
	{
		return false;
	}
	int map[256];
	for (int &m : map)
	{
		m = -1;
	}
	for (int i = 0; i < 64; ++i)
	{
		map[(unsigned char)alphabet[i]] = i;
	}
	out.clear();
	std::uint32_t acc = 0;
	int bits = 0;
	for (char c : t)
	{
		const int v = map[(unsigned char)c];
		if (v < 0)
		{
			return false;
		}
		acc = (acc << 6) | (std::uint32_t)v;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back((std::uint8_t)((acc >> bits) & 0xFF));
		}
	}
	// the unused low bits of the last character must be zero (one canonical text per byte string)
	return (acc & ((1u << bits) - 1)) == 0;
}

void put16(std::uint8_t *p, std::uint16_t v)
{
	p[0] = (std::uint8_t)v;
	p[1] = (std::uint8_t)(v >> 8);
}
std::uint16_t get16(const std::uint8_t *p)
{
	return (std::uint16_t)(p[0] | ((std::uint16_t)p[1] << 8));
}
void put32(std::uint8_t *p, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		p[i] = (std::uint8_t)(v >> (8 * i));
	}
}
std::uint32_t get32(const std::uint8_t *p)
{
	return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
} // namespace

const char *refuseReasonName(int reason)
{
	switch (reason)
	{
	case REFUSE_BAD_TICKET: return "bad ticket";
	case REFUSE_EXPIRED: return "ticket expired";
	case REFUSE_GAME_CLOSED: return "game closed";
	case REFUSE_FULL: return "relay full";
	case REFUSE_VERSION: return "relay protocol version";
	case REFUSE_SLOT_TAKEN: return "the slot is in use from another address";
	case REFUSE_STALE: return "a newer rejoin ticket of the slot was used";
	case REFUSE_NO_LOG: return "the relay keeps no log of the game";
	case REFUSE_SPECTATOR: return "a spectator sends no commands";
	case REFUSE_REJOIN_BEFORE_BIND: return "cannot rejoin: the original player never connected to the relay";
	case REFUSE_CAPABILITY: return "the client lacks the capability the game's mode requires";
	case REFUSE_MODE_MIXED: return "the ticket's mode disagrees with the game's";
	case REFUSE_AUTH_REQUIRED: return "the game requires authenticated datagrams (update the client)";
	default: return "unknown reason";
	}
}

size_t ticketSize(std::uint8_t version)
{
	switch (version)
	{
	case 1: return kTicketBytes;
	case 2:
	case 3: return kTicketBytesV2;
	case 4: return kTicketBytes16;
	case 5:
	case 6: return kTicketBytesV2_16;
	case 7: return kTicketBytesFlags;
	default: return 0;
	}
}

namespace
{
std::vector<std::uint8_t> sign(const Ticket &t, const std::vector<std::uint8_t> &key, std::uint8_t kind)
{
	// lane NET-SYNC: a mode-flagged START ticket is version 7 (version 4 plus the u8 mode byte)
	if (kind == 1 && t.mode)
	{
		std::vector<std::uint8_t> b(ticketSize(7), 0);
		b[0] = 7;
		b[1] = t.keyId;
		put32(&b[2], (std::uint32_t)(t.gameId & 0xFFFFFFFFu));
		put32(&b[6], (std::uint32_t)(t.gameId >> 32));
		b[10] = t.slot;
		b[11] = (std::uint8_t)t.slotMask;
		b[12] = (std::uint8_t)(t.slotMask >> 8);
		b[13] = t.mode;
		put32(&b[14], t.expires);
		put16(&b[18], t.frameMs); // ROUTER-1
		const size_t signedBytes = b.size() - kTicketMacBytes;
		const std::array<std::uint8_t, 32> mac = HmacSha256(key.data(), key.size(), b.data(), signedBytes);
		std::memcpy(b.data() + signedBytes, mac.data(), kTicketMacBytes);
		return b;
	}
	const size_t extra = t.slotMask > 0xff ? 1 : 0;
	const std::uint8_t version = (std::uint8_t)(kind + (extra ? 3 : 0));
	std::vector<std::uint8_t> b(ticketSize(version), 0);
	b[0] = version;
	b[1] = t.keyId;
	put32(&b[2], (std::uint32_t)(t.gameId & 0xFFFFFFFFu));
	put32(&b[6], (std::uint32_t)(t.gameId >> 32));
	b[10] = t.slot;
	b[11] = (std::uint8_t)t.slotMask;
	if (extra)
	{
		b[12] = (std::uint8_t)(t.slotMask >> 8);
	}
	put32(&b[12 + extra], t.expires);
	if (kind != 1)
	{
		const std::uint16_t value = kind == 3 ? t.delaySeconds : t.generation;
		b[16 + extra] = (std::uint8_t)value;
		b[17 + extra] = (std::uint8_t)(value >> 8);
	}
	const size_t signedBytes = b.size() - kTicketMacBytes;
	const std::array<std::uint8_t, 32> mac = HmacSha256(key.data(), key.size(), b.data(), signedBytes);
	std::memcpy(b.data() + signedBytes, mac.data(), kTicketMacBytes);
	return b;
}
} // namespace

std::vector<std::uint8_t> signTicket(const Ticket &t, const std::vector<std::uint8_t> &key)
{
	return sign(t, key, 1);
}

std::vector<std::uint8_t> signRejoinTicket(const Ticket &t, const std::vector<std::uint8_t> &key)
{
	return sign(t, key, 2);
}

std::vector<std::uint8_t> signSpectatorTicket(const Ticket &t, const std::vector<std::uint8_t> &key)
{
	return sign(t, key, 3);
}

bool readTicketFields(const std::uint8_t *bytes, size_t size, Ticket &out, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	if (!bytes || !size || !ticketSize(bytes[0]) || size != ticketSize(bytes[0]))
	{
		return fail("a ticket of " + std::to_string(size) + " bytes or another ticket version");
	}
	const size_t extra = bytes[0] >= 4 ? 1 : 0;
	const int kind = bytes[0] == 7 ? 1 : bytes[0] - (extra ? 3 : 0);
	Ticket t;
	t.keyId = bytes[1];
	t.gameId = (std::uint64_t)get32(bytes + 2) | ((std::uint64_t)get32(bytes + 6) << 32);
	t.slot = bytes[10];
	t.slotMask = (std::uint16_t)(bytes[11] | (extra ? bytes[12] << 8 : 0));
	t.mode = bytes[0] == 7 ? bytes[13] : 0;
	t.frameMs = bytes[0] == 7 ? get16(bytes + 18) : 0; // ROUTER-1: 0 = the 200 ms default
	t.expires = get32(bytes + 12 + extra + (bytes[0] == 7 ? 1 : 0));
	t.generation = kind == 2 ? (std::uint16_t)(bytes[16 + extra] | (bytes[17 + extra] << 8)) : 0;
	t.spectator = kind == 3;
	t.delaySeconds = t.spectator ? (std::uint16_t)(bytes[16 + extra] | (bytes[17 + extra] << 8)) : 0;
	if (kind == 2 && !t.generation)
	{
		return fail("a rejoin ticket of generation 0");
	}
	if (t.spectator ? t.slot >= kMaxSpectators : (t.slot >= kMaxSlots || !(t.slotMask & (1u << t.slot))))
	{
		return fail(t.spectator ? "spectator index " + std::to_string(t.slot) + " (0 .. 15)"
			: "ticket slot " + std::to_string(t.slot) + " is not one of the game's network slots");
	}
	if (!t.slotMask)
	{
		return fail("a ticket with no network slots");
	}
	out = t;
	return true;
}

bool verifyTicket(const std::uint8_t *bytes, size_t size, const KeyRing &keys, std::uint32_t nowUnix, Ticket &out, std::string *error, RefuseReason *reason)
{
	auto fail = [&](RefuseReason r, const std::string &why) {
		if (error)
		{
			*error = why;
		}
		if (reason)
		{
			*reason = r;
		}
		return false;
	};
	if (!bytes || !size || !ticketSize(bytes[0]) || size != ticketSize(bytes[0]))
	{
		return fail(REFUSE_BAD_TICKET, "a ticket of " + std::to_string(size) + " bytes or another ticket version");
	}
	const auto key = keys.find(bytes[1]);
	if (key == keys.end())
	{
		return fail(REFUSE_BAD_TICKET, "ticket key id " + std::to_string(bytes[1]) + " is not in the key ring");
	}
	const size_t signedBytes = size - kTicketMacBytes;
	const std::array<std::uint8_t, 32> mac = HmacSha256(key->second.data(), key->second.size(), bytes, signedBytes);
	if (!ConstantTimeEqual(mac.data(), bytes + signedBytes, kTicketMacBytes))
	{
		return fail(REFUSE_BAD_TICKET, "the ticket's signature does not match");
	}
	Ticket t;
	std::string why;
	if (!readTicketFields(bytes, size, t, &why))
	{
		return fail(REFUSE_BAD_TICKET, why);
	}
	if (nowUnix > t.expires)
	{
		return fail(REFUSE_EXPIRED, "the ticket expired");
	}
	out = t;
	return true;
}

bool parseKeyRing(const std::string &text, KeyRing &out, std::string *error)
{
	out.clear();
	std::istringstream in(text);
	std::string line;
	int n = 0;
	while (std::getline(in, line))
	{
		++n;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
		{
			line.pop_back();
		}
		size_t start = line.find_first_not_of(" \t");
		if (start == std::string::npos || line[start] == '#')
		{
			continue;
		}
		line = line.substr(start);
		const size_t colon = line.find(':');
		auto fail = [&](const std::string &why) {
			if (error)
			{
				*error = "key line " + std::to_string(n) + ": " + why;
			}
			return false;
		};
		if (colon == std::string::npos || colon == 0 || colon > 3)
		{
			return fail("wants keyId:hex");
		}
		int id = 0;
		for (size_t i = 0; i < colon; ++i)
		{
			if (line[i] < '0' || line[i] > '9')
			{
				return fail("the key id is not a number");
			}
			id = id * 10 + (line[i] - '0');
		}
		if (id > 255)
		{
			return fail("the key id is above 255");
		}
		const std::string hex = line.substr(colon + 1);
		if (hex.size() % 2 != 0 || hex.size() < 32)
		{
			return fail("a key must be at least 16 bytes of hex");
		}
		std::vector<std::uint8_t> key;
		for (size_t i = 0; i < hex.size(); i += 2)
		{
			auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
			const int hi = nib(hex[i]), lo = nib(hex[i + 1]);
			if (hi < 0 || lo < 0)
			{
				return fail("not hex");
			}
			key.push_back((std::uint8_t)(hi * 16 + lo));
		}
		if (out.count((std::uint8_t)id))
		{
			return fail("key id " + std::to_string(id) + " twice");
		}
		out[(std::uint8_t)id] = key;
	}
	if (out.empty())
	{
		if (error)
		{
			*error = "the key ring is empty";
		}
		return false;
	}
	return true;
}

std::string base64UrlEncode(const std::uint8_t *data, size_t size)
{
	return encodeWith(kUrlAlphabet, data, size, false);
}
bool base64UrlDecode(const std::string &text, std::vector<std::uint8_t> &out)
{
	return decodeWith(kUrlAlphabet, text, false, out);
}
std::string base64Encode(const std::uint8_t *data, size_t size)
{
	return encodeWith(kStdAlphabet, data, size, true);
}
bool base64Decode(const std::string &text, std::vector<std::uint8_t> &out)
{
	return decodeWith(kStdAlphabet, text, true, out);
}

bool isRelayDatagram(const std::uint8_t *d, size_t size)
{
	return size >= kHeaderBytes && d[0] == kMagic0 && d[1] == kMagic1;
}

void putHeader(std::vector<std::uint8_t> &out, Kind kind)
{
	out.push_back(kMagic0);
	out.push_back(kMagic1);
	out.push_back(kVersion);
	out.push_back((std::uint8_t)kind);
}

std::vector<std::uint8_t> encodeHello(const std::vector<std::uint8_t> &ticket, const std::vector<std::uint8_t> &sessionToken, std::uint8_t caps)
{
	std::vector<std::uint8_t> d;
	putHeader(d, HELLO);
	d.insert(d.end(), ticket.begin(), ticket.end());
	d.insert(d.end(), sessionToken.begin(), sessionToken.end());
	d.push_back(caps); // lane NET-SYNC: the v4 HELLO's capability bits (always present on this envelope)
	return d;
}

std::vector<std::uint8_t> encodeWelcome(std::uint64_t gameId, std::uint8_t slot,
	const std::array<std::uint8_t, kSessionTokenBytes> &sessionToken, std::uint64_t epoch)
{
	std::vector<std::uint8_t> d;
	putHeader(d, WELCOME);
	for (int i = 0; i < 8; ++i)
	{
		d.push_back((std::uint8_t)(gameId >> (8 * i)));
	}
	d.push_back(slot);
	d.insert(d.end(), sessionToken.begin(), sessionToken.end());
	if (epoch) // lane NET-SYNC: canonical games tell the client the commit epoch their channel belongs to
	{
		for (int i = 0; i < 8; ++i)
		{
			d.push_back((std::uint8_t)(epoch >> (8 * i)));
		}
	}
	return d;
}

std::vector<std::uint8_t> encodeRefused(RefuseReason reason)
{
	std::vector<std::uint8_t> d;
	putHeader(d, REFUSED);
	d.push_back((std::uint8_t)reason);
	return d;
}

std::vector<std::uint8_t> encodeEmpty(Kind kind)
{
	std::vector<std::uint8_t> d;
	putHeader(d, kind);
	return d;
}

std::vector<std::uint8_t> encodeData(std::uint8_t targetSlot, const std::vector<std::uint8_t> &packet)
{
	std::vector<std::uint8_t> d;
	d.reserve(kHeaderBytes + 1 + packet.size());
	putHeader(d, DATA);
	d.push_back(targetSlot);
	d.insert(d.end(), packet.begin(), packet.end());
	return d;
}
std::vector<std::uint8_t> encodeStatus(const std::vector<SlotStatus> &slots)
{
	std::vector<std::uint8_t> d;
	putHeader(d, STATUS);
	d.push_back((std::uint8_t)slots.size());
	for (const SlotStatus &s : slots)
	{
		d.push_back(s.slot);
		d.push_back((std::uint8_t)((s.silent ? 1 : 0) | (s.bound ? 2 : 0)));
		d.push_back((std::uint8_t)(s.generation & 0xFF));
		d.push_back((std::uint8_t)(s.generation >> 8));
	}
	return d;
}

bool decodeStatus(const std::uint8_t *d, size_t size, std::vector<SlotStatus> &out)
{
	out.clear();
	if (size < kHeaderBytes + 1 || d[3] != STATUS)
	{
		return false;
	}
	const size_t n = d[kHeaderBytes];
	if (n > (size_t)kMaxSlots || size != kHeaderBytes + 1 + n * 4)
	{
		return false;
	}
	for (size_t i = 0; i < n; ++i)
	{
		const std::uint8_t *e = d + kHeaderBytes + 1 + i * 4;
		SlotStatus s;
		s.slot = e[0];
		s.silent = (e[1] & 1) != 0;
		s.bound = (e[1] & 2) != 0;
		s.generation = (std::uint16_t)(e[2] | (e[3] << 8));
		if (s.slot >= kMaxSlots || (e[1] & ~3u) != 0)
		{
			out.clear();
			return false;
		}
		out.push_back(s);
	}
	return true;
}

std::vector<std::uint8_t> encodeLogRequest(std::uint32_t offset)
{
	std::vector<std::uint8_t> d;
	putHeader(d, LOG_REQUEST);
	d.resize(kHeaderBytes + 4);
	put32(&d[kHeaderBytes], offset);
	return d;
}

std::vector<std::uint8_t> encodeLogChunk(std::uint32_t offset, std::uint32_t total, const std::uint8_t *bytes, size_t size)
{
	std::vector<std::uint8_t> d;
	putHeader(d, LOG_CHUNK);
	d.resize(kHeaderBytes + 8);
	put32(&d[kHeaderBytes], offset);
	put32(&d[kHeaderBytes + 4], total);
	d.insert(d.end(), bytes, bytes + size);
	return d;
}
} // namespace RelayProtocol
