// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/Transport.h.

#include "GameNetwork/Transport.h"

#include "Common/Crc32.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_type;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t socklen_type;
#endif

namespace
{
constexpr std::uint16_t kMagic = 0x4642; // "BF"
constexpr std::uint8_t kVersion = 2; // 2: fragment index / count per record, whole-packet validation
constexpr long long kNoSocket = -1;
constexpr size_t kHeaderBytes = 4 + 2 + 1 + 1 + 4 + 2;
constexpr size_t kRecordHeaderBytes = 4 + 2 + 2 + 4;
constexpr size_t kMaxDatagram = 65507;
constexpr size_t kMaxErrors = 100;

#ifdef _WIN32
struct WinsockInit
{
	WinsockInit()
	{
		WSADATA d;
		WSAStartup(MAKEWORD(2, 2), &d);
	}
};
void ensureWinsock()
{
	static WinsockInit init;
}
#else
void ensureWinsock() {}
#endif

void put16(std::vector<std::uint8_t> &b, std::uint16_t v)
{
	b.push_back((std::uint8_t)(v & 0xFF));
	b.push_back((std::uint8_t)(v >> 8));
}
void put32(std::vector<std::uint8_t> &b, std::uint32_t v)
{
	put16(b, (std::uint16_t)(v & 0xFFFF));
	put16(b, (std::uint16_t)(v >> 16));
}
std::uint32_t get32(const std::uint8_t *p)
{
	return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
std::uint16_t get16(const std::uint8_t *p)
{
	return (std::uint16_t)(p[0] | (p[1] << 8));
}
// sequence comparison with wrap-around
bool seqBefore(std::uint32_t a, std::uint32_t b)
{
	return (std::int32_t)(a - b) < 0;
}
} // namespace

std::string NetAddress::text() const
{
	char b[32];
	std::snprintf(b, sizeof(b), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
	return b;
}

bool NetAddress::parse(const std::string &text, NetAddress &out)
{
	unsigned a, b, c, d, p;
	char tail;
	if (std::sscanf(text.c_str(), "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail) != 5 || a > 255 || b > 255 || c > 255 || d > 255 || p > 65535)
	{
		return false;
	}
	out.ip = (a << 24) | (b << 16) | (c << 8) | d;
	out.port = (std::uint16_t)p;
	return true;
}

std::uint64_t NetMilliseconds()
{
	return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void NetSleepMilliseconds(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---- UDP ---------------------------------------------------------------------------------------------------------------------
UDP::UDP() : m_socket(kNoSocket)
{
	ensureWinsock();
}

UDP::~UDP()
{
	if (m_socket != kNoSocket)
	{
#ifdef _WIN32
		closesocket((SOCKET)m_socket);
#else
		close((int)m_socket);
#endif
	}
}

bool UDP::enableBroadcast()
{
	// lane MP-2: the LAN lobby broadcasts (SO_BROADCAST; ZH UDP::AllowBroadcasts)
	if (m_socket == kNoSocket)
	{
		return false;
	}
	int on = 1;
	return setsockopt((decltype(socket(0, 0, 0)))m_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on)) == 0;
}

bool UDP::isOpen() const
{
	return m_socket != kNoSocket;
}

bool UDP::bind(std::uint32_t ip, std::uint16_t port, std::string *error)
{
#ifdef _WIN32
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
#else
	int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
#endif
	{
		if (error)
		{
			*error = "cannot create a UDP socket";
		}
		return false;
	}
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(ip);
	a.sin_port = htons(port);
	if (::bind(s, (sockaddr *)&a, sizeof(a)) != 0)
	{
#ifdef _WIN32
		closesocket(s);
#else
		close(s);
#endif
		if (error)
		{
			*error = "cannot bind UDP " + NetAddress{ ip, port }.text();
		}
		return false;
	}
#ifdef _WIN32
	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);
#else
	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
	int buf = 1 << 20;
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&buf, sizeof(buf));
	setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&buf, sizeof(buf));
	m_socket = (long long)s;
	return true;
}

NetAddress UDP::localAddress() const
{
	NetAddress out;
	if (m_socket == kNoSocket)
	{
		return out;
	}
	sockaddr_in a{};
	socklen_type len = sizeof(a);
	if (getsockname((decltype(socket(0, 0, 0)))m_socket, (sockaddr *)&a, &len) == 0)
	{
		out.ip = ntohl(a.sin_addr.s_addr);
		out.port = ntohs(a.sin_port);
	}
	return out;
}

bool UDP::sendTo(const NetAddress &to, const std::uint8_t *data, size_t size)
{
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(to.ip);
	a.sin_port = htons(to.port);
	return sendto((decltype(socket(0, 0, 0)))m_socket, (const char *)data, (int)size, 0, (sockaddr *)&a, sizeof(a)) == (int)size;
}

bool UDP::receiveFrom(NetAddress &from, std::vector<std::uint8_t> &data)
{
	data.resize(65536);
	sockaddr_in a{};
	socklen_type len = sizeof(a);
	const int n = (int)recvfrom((decltype(socket(0, 0, 0)))m_socket, (char *)data.data(), (int)data.size(), 0, (sockaddr *)&a, &len);
	if (n < 0)
	{
		data.clear();
		return false;
	}
	data.resize((size_t)n);
	from.ip = ntohl(a.sin_addr.s_addr);
	from.port = ntohs(a.sin_port);
	return true;
}

// ---- Transport ------------------------------------------------------------------------------------------------------------------
Transport::Transport(UDP &socket, int localSlot, const Options &options)
	: m_socket(socket)
	, m_localSlot(localSlot)
	, m_options(options)
	, m_dropState(options.dropSeed ? options.dropSeed : 1)
{
	if (m_options.packetBytes < 64 || m_options.packetBytes > kMaxDatagram)
	{
		error("packetBytes " + std::to_string(m_options.packetBytes) + " outside 64 .. 65507: using 1200");
		m_options.packetBytes = 1200;
	}
	if (m_options.window < 16)
	{
		m_options.window = 16;
	}
	if (m_options.maxPacketBytes == 0)
	{
		m_options.maxPacketBytes = m_options.packetBytes;
	}
	if (m_options.maxPacketBytes < m_options.packetBytes || m_options.maxPacketBytes > kMaxDatagram)
	{
		error("maxPacketBytes " + std::to_string(m_options.maxPacketBytes) + " is not in packetBytes .. 65507: using packetBytes");
		m_options.maxPacketBytes = m_options.packetBytes;
	}
	if (m_options.window > 0x7FFFFFFFu)
	{
		error("window " + std::to_string(m_options.window) + " is beyond the serial-number half-range: using 2147483647");
		m_options.window = 0x7FFFFFFFu;
	}
	// the budget invariant: a maximum-size partial command (the in-order fragments already joined, maxCommandBytes) plus an early record of the largest
	// size in every other slot of the window, so the next expected fragment of an unfinished command can always be accepted (overflow checked in 64 bits)
	{
		const std::uint64_t record = m_options.maxPacketBytes - kHeaderBytes - kRecordHeaderBytes;
		const std::uint64_t minimum = (std::uint64_t)m_options.maxCommandBytes + (std::uint64_t)(m_options.window - 1) * record;
		const std::uint64_t cap = (std::uint64_t)SIZE_MAX;
		if (minimum > cap)
		{
			error("the receive budget invariant needs " + std::to_string(minimum) + " bytes, beyond what this build can address: reduce window / maxPacketBytes");
			m_options.maxBufferedBytes = (size_t)cap;
		}
		else if (m_options.maxBufferedBytes < minimum)
		{
			m_options.maxBufferedBytes = (size_t)minimum; // raised to the invariant
		}
	}
}

void Transport::error(const std::string &e)
{
	if (m_errors.size() < kMaxErrors)
	{
		m_errors.push_back(e);
	}
}

void Transport::setPeer(int slot, const NetAddress &address, std::uint32_t sendStart, std::uint32_t receiveStart)
{
	Peer &p = m_peers[(size_t)slot];
	p = Peer();
	p.known = true;
	p.address = address;
	p.nextSeq = sendStart;
	p.expected = receiveStart;
}

size_t Transport::unackedRecords() const
{
	size_t n = 0;
	for (const Peer &p : m_peers)
	{
		n += p.unacked.size();
	}
	return n;
}

bool Transport::sendTo(int slot, const std::vector<std::uint8_t> &bytes, std::string *err)
{
	auto fail = [&](const std::string &why) {
		if (err)
		{
			*err = why;
		}
		return false;
	};
	if (slot < 0 || slot >= MAX_SLOTS || !m_peers[(size_t)slot].known || m_peers[(size_t)slot].retiring)
	{
		return fail("slot " + std::to_string(slot) + " is not a connected peer");
	}
	if (bytes.empty() || bytes.size() > m_options.maxCommandBytes)
	{
		return fail("a command of " + std::to_string(bytes.size()) + " bytes (1 .. " + std::to_string(m_options.maxCommandBytes) + ")");
	}
	// a command that does not fit one packet goes as consecutive fragments, every one fitting a packet alone
	const size_t chunk = m_options.packetBytes - kHeaderBytes - kRecordHeaderBytes;
	const size_t count = (bytes.size() + chunk - 1) / chunk;
	if (count > 0xFFFF)
	{
		return fail("a command of " + std::to_string(bytes.size()) + " bytes needs more than 65535 fragments");
	}
	Peer &p = m_peers[(size_t)slot];
	for (size_t i = 0; i < count; ++i)
	{
		const size_t from = i * chunk, to = std::min(bytes.size(), from + chunk);
		p.unacked.push_back(Outgoing{ p.nextSeq++, p.issued++, (std::uint16_t)i, (std::uint16_t)count,
			std::vector<std::uint8_t>(bytes.begin() + (long)from, bytes.begin() + (long)to), 0, false });
	}
	++m_stats.commandsSent;
	m_stats.fragmentedCommands += count > 1 ? 1 : 0;
	return true;
}

void Transport::fastForwardSend(int slot, std::uint64_t records)
{
	Peer &p = m_peers[(size_t)slot];
	p.unacked.clear();
	p.nextSeq += (std::uint32_t)records;
	p.issued += records;
	p.acked = p.issued;
}

void Transport::retire(int slot)
{
	if (slot < 0 || slot >= MAX_SLOTS || !m_peers[(size_t)slot].known)
	{
		return;
	}
	Peer &p = m_peers[(size_t)slot];
	p.unacked.clear();
	p.early.clear();
	p.partial.clear();
	p.partialNext = p.partialCount = 0;
	p.retiring = true;
	p.ackOwed = true; // the final ack (of its leave) goes out with the next flush
	++m_stats.peersRetired;
}

bool Transport::receive(int &fromSlot, std::vector<std::uint8_t> &bytes)
{
	if (m_delivered.empty())
	{
		return false;
	}
	fromSlot = m_delivered.front().first;
	bytes = std::move(m_delivered.front().second);
	m_delivered.pop_front();
	return true;
}

void Transport::sendDatagram(const NetAddress &to, std::vector<std::uint8_t> datagram, std::uint64_t now)
{
	if (m_options.dropPerMille > 0 || m_options.jitterMs > 0)
	{
		m_dropState = m_dropState * 1664525u + 1013904223u; // Numerical Recipes LCG: reproducible loss / jitter for the harness
	}
	if (m_options.dropPerMille > 0 && (int)((m_dropState >> 16) % 1000u) < m_options.dropPerMille)
	{
		++m_stats.dropped;
		return;
	}
	if (m_options.delayMs > 0 || m_options.jitterMs > 0)
	{
		const std::uint64_t jitter = m_options.jitterMs > 0 ? (std::uint64_t)((m_dropState >> 8) % (std::uint32_t)(m_options.jitterMs + 1)) : 0;
		m_delayed.insert({ now + (std::uint64_t)m_options.delayMs + jitter, { to, std::move(datagram) } });
		return;
	}
	if (!m_socket.sendTo(to, datagram.data(), datagram.size()))
	{
		++m_stats.sendErrors;
		error("sending " + std::to_string(datagram.size()) + " bytes to " + to.text() + " failed (the records are resent)");
		return;
	}
	++m_stats.packetsSent;
}

void Transport::flush(int slot, std::uint64_t now)
{
	Peer &p = m_peers[(size_t)slot];
	size_t i = 0;
	bool sentAny = false;
	const bool wantPacket = p.ackOwed || now - p.lastSent >= (std::uint64_t)m_options.keepAliveMs; // an ack or a keep-alive is due
	// only the first `window` unacked records are in flight (the receiver accepts no more ahead of what it expects)
	const size_t inFlight = std::min(p.unacked.size(), (size_t)m_options.window);
	// lane MP-2: the connection's resend timeout (Karn / RFC 6298): at least resendMs; a resend for a timeout doubles it (at most 2 s), a clean round-trip
	// sample sets it to the smoothed round trip plus four times its variation. On a link slower than resendMs the timeout so grows past the round trip,
	// records are acked from their first transmission and the round trip is measured (Karn's rule takes no sample from a resent record): the adaptive
	// run-ahead follows it
	if (p.rto < (std::uint64_t)m_options.resendMs)
	{
		p.rto = (std::uint64_t)m_options.resendMs;
	}
	bool timedOut = false;
	while (i < inFlight || (!sentAny && wantPacket))
	{
		std::vector<std::uint8_t> d(4, 0); // the CRC goes first, written last
		put16(d, kMagic);
		d.push_back(kVersion);
		d.push_back((std::uint8_t)m_localSlot);
		put32(d, p.expected);
		const size_t countAt = d.size();
		put16(d, 0);
		std::uint16_t count = 0;
		for (; i < inFlight && count < 0xFFFF; ++i)
		{
			Outgoing &o = p.unacked[i];
			if (o.sent && now - o.lastSent < p.rto)
			{
				continue;
			}
			if (d.size() + kRecordHeaderBytes + o.bytes.size() > m_options.packetBytes)
			{
				break; // a fragment always fits an empty packet (sendTo cut it so)
			}
			put32(d, o.seq);
			put16(d, o.fragIndex);
			put16(d, o.fragCount);
			put32(d, (std::uint32_t)o.bytes.size());
			d.insert(d.end(), o.bytes.begin(), o.bytes.end());
			if (o.sent)
			{
				++m_stats.commandsResent;
				o.resent = true; // lane MP-2: no round-trip sample from it (Karn's rule)
				++o.resends;
				timedOut = true;
			}
			o.sent = true;
			o.lastSent = now;
			++count;
			++m_stats.recordsSent;
		}
		if (count == 0 && (sentAny || !wantPacket))
		{
			break;
		}
		d[countAt] = (std::uint8_t)(count & 0xFF);
		d[countAt + 1] = (std::uint8_t)(count >> 8);
		const std::uint32_t crc = crc32Bytes(d.data() + 4, d.size() - 4);
		d[0] = (std::uint8_t)(crc & 0xFF);
		d[1] = (std::uint8_t)((crc >> 8) & 0xFF);
		d[2] = (std::uint8_t)((crc >> 16) & 0xFF);
		d[3] = (std::uint8_t)(crc >> 24);
		sendDatagram(p.address, std::move(d), now);
		sentAny = true;
		p.lastSent = now;
		p.ackOwed = false;
	}
	if (p.retiring && sentAny)
	{
		p = Peer(); // the last ack went out: forgotten (later datagrams from it count as unknown senders)
	}
	if (timedOut)
	{
		p.rto = std::min<std::uint64_t>(2000, p.rto * 2); // lane MP-2: Karn's backoff
	}
}

void Transport::deliver(int slot, Incoming &&r)
{
	Peer &p = m_peers[(size_t)slot];
	if (r.fragCount == 1)
	{
		if (p.partialCount != 0)
		{
			error("slot " + std::to_string(slot) + ": a whole command inside a fragmented one (the partial command is dropped)");
			p.partial.clear();
			p.partialNext = p.partialCount = 0;
		}
		m_delivered.push_back({ slot, std::move(r.bytes) });
		++m_stats.commandsDelivered;
		return;
	}
	if (p.partialCount == 0 ? r.fragIndex != 0 : (r.fragCount != p.partialCount || r.fragIndex != p.partialNext))
	{
		error("slot " + std::to_string(slot) + ": fragment " + std::to_string(r.fragIndex) + " of " + std::to_string(r.fragCount) + " out of order (dropped)");
		p.partial.clear();
		p.partialNext = p.partialCount = 0;
		return;
	}
	if (p.partial.size() + r.bytes.size() > m_options.maxCommandBytes)
	{
		error("slot " + std::to_string(slot) + ": a fragmented command beyond " + std::to_string(m_options.maxCommandBytes) + " bytes (dropped)");
		p.partial.clear();
		p.partialNext = p.partialCount = 0;
		return;
	}
	p.partialCount = r.fragCount;
	p.partial.insert(p.partial.end(), r.bytes.begin(), r.bytes.end());
	if (++p.partialNext == p.partialCount)
	{
		m_delivered.push_back({ slot, std::move(p.partial) });
		++m_stats.commandsDelivered;
		p.partial.clear();
		p.partialNext = p.partialCount = 0;
	}
}

void Transport::handleDatagram(const NetAddress &from, const std::vector<std::uint8_t> &d, std::uint64_t now)
{
	if (d.size() < kHeaderBytes || get16(d.data() + 4) != kMagic)
	{
		m_foreign.push_back({ from, d }); // not a transport packet: the lobby's
		return;
	}
	if (d.size() > m_options.maxPacketBytes || crc32Bytes(d.data() + 4, d.size() - 4) != get32(d.data()) || d[6] != kVersion)
	{
		++m_stats.badPackets; // beyond the receive ceiling (checked before the CRC is even computed), a bad CRC or another version
		return;
	}
	const int slot = d[7];
	if (slot >= MAX_SLOTS || !m_peers[(size_t)slot].known || m_peers[(size_t)slot].retiring || !(m_peers[(size_t)slot].address == from))
	{
		++m_stats.unknownSenders;
		return;
	}
	Peer &p = m_peers[(size_t)slot];
	// ---- validate the whole packet first: nothing below changes state until it all holds ----
	const std::uint32_t ack = get32(d.data() + 8);
	// the ack names how many records the peer has received: at most the number issued to it (64-bit count, so a half-range or far-future value is refused
	// whatever the wrap state); an older one is stale and valid. The serial comparisons below only ever see in-range values.
	if ((std::uint32_t)(p.nextSeq - ack) > p.issued)
	{
		++m_stats.badPackets;
		return;
	}
	struct Parsed
	{
		std::uint32_t seq;
		std::uint16_t fragIndex, fragCount;
		size_t at, len;
	};
	std::vector<Parsed> records;
	const size_t count = get16(d.data() + 12);
	size_t at = kHeaderBytes;
	for (size_t c = 0; c < count; ++c)
	{
		if (d.size() - at < kRecordHeaderBytes)
		{
			++m_stats.badPackets;
			return;
		}
		Parsed r;
		r.seq = get32(d.data() + at);
		r.fragIndex = get16(d.data() + at + 4);
		r.fragCount = get16(d.data() + at + 6);
		r.len = get32(d.data() + at + 8);
		at += kRecordHeaderBytes;
		if (r.fragCount == 0 || r.fragIndex >= r.fragCount || r.len == 0 || d.size() - at < r.len
			|| (r.fragCount == 1 && r.len > m_options.maxCommandBytes) // a whole command beyond the command limit
			|| !seqBefore(r.seq, p.expected + m_options.window))        // beyond the receive window
		{
			++m_stats.badPackets;
			return;
		}
		r.at = at;
		at += r.len;
		records.push_back(r);
	}
	if (at != d.size())
	{
		++m_stats.badPackets; // trailing bytes
		return;
	}
	// the receive budget: what would stay buffered (early records and the partial command) after this packet's records are filed and the in-order ones
	// drained, simulated on temporaries; the packet is refused whole when that exceeds it (the expected record drains and so never counts)
	{
		std::map<std::uint32_t, std::pair<size_t, std::pair<std::uint16_t, std::uint16_t>>> view; // seq -> (bytes, (fragIndex, fragCount))
		size_t buffered = 0;
		for (const auto &e : p.early)
		{
			view[e.first] = { e.second.bytes.size(), { e.second.fragIndex, e.second.fragCount } };
		}
		for (const Parsed &r : records)
		{
			if (!seqBefore(r.seq, p.expected) && !view.count(r.seq))
			{
				view[r.seq] = { r.len, { r.fragIndex, r.fragCount } };
			}
		}
		size_t partial = p.partial.size();
		std::uint32_t next = p.expected;
		for (auto it = view.find(next); it != view.end(); it = view.find(next))
		{
			const auto &rec = it->second;
			partial = rec.second.second == 1 || rec.second.first + 1 == rec.second.second ? 0 : partial + rec.first;
			view.erase(it);
			++next;
		}
		for (const auto &e : view)
		{
			buffered += e.second.first;
		}
		if (buffered + partial > m_options.maxBufferedBytes)
		{
			++m_stats.badPackets;
			return;
		}
	}
	// ---- commit ----
	++m_stats.packetsReceived;
	p.lastHeard = now;
	// the extended ack: the latest non-future count congruent to the wire value; at or below what is acknowledged already it is stale and removes nothing.
	// No serial comparison is made here, so an ancient ack cannot reach a current record whatever the lifetime count (after 2^32 records the wire value
	// aliases a past cycle: the unavoidable limit of a 32-bit field)
	{
		const std::uint64_t candidate = p.issued - (std::uint32_t)(p.nextSeq - ack);
		if (candidate > p.acked)
		{
			p.acked = candidate;
			long long sample = -1;
			while (!p.unacked.empty() && p.unacked.front().ordinal < p.acked)
			{
				const Outgoing &o = p.unacked.front();
				if (o.sent && !o.resent && now >= o.lastSent)
				{
					sample = (long long)(now - o.lastSent); // lane MP-2: the newest record sent once and acknowledged now
				}
				p.unacked.pop_front();
			}
			if (sample >= 0)
			{
				// lane MP-2: RFC 6298's smoothed round trip (alpha 1/8, beta 1/4), in ms
				if (p.rttSamples == 0)
				{
					p.srtt = (std::uint64_t)sample;
					p.rttvar = (std::uint64_t)sample / 2;
				}
				else
				{
					const std::uint64_t err = p.srtt > (std::uint64_t)sample ? p.srtt - (std::uint64_t)sample : (std::uint64_t)sample - p.srtt;
					p.rttvar = (3 * p.rttvar + err) / 4;
					p.srtt = (7 * p.srtt + (std::uint64_t)sample) / 8;
				}
				++p.rttSamples;
				p.rto = std::max<std::uint64_t>((std::uint64_t)m_options.resendMs, std::min<std::uint64_t>(2000, p.srtt + 4 * p.rttvar));
			}
		}
	}
	for (const Parsed &r : records)
	{
		p.ackOwed = true;
		if (seqBefore(r.seq, p.expected) || p.early.count(r.seq))
		{
			++m_stats.duplicates;
			continue;
		}
		Incoming in;
		in.fragIndex = r.fragIndex;
		in.fragCount = r.fragCount;
		in.bytes.assign(d.begin() + (long)r.at, d.begin() + (long)(r.at + r.len));
		p.early[r.seq] = std::move(in);
	}
	// in sequence order only
	for (auto it = p.early.find(p.expected); it != p.early.end(); it = p.early.find(p.expected))
	{
		Incoming in = std::move(it->second);
		p.early.erase(it);
		++p.expected;
		deliver(slot, std::move(in));
	}
}

void Transport::service()
{
	const std::uint64_t now = NetMilliseconds();
	NetAddress from;
	std::vector<std::uint8_t> d;
	while (m_socket.receiveFrom(from, d))
	{
		handleDatagram(from, d, now);
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (s != m_localSlot && m_peers[(size_t)s].known)
		{
			flush(s, now);
		}
	}
	while (!m_delayed.empty() && m_delayed.begin()->first <= now)
	{
		const auto &e = m_delayed.begin()->second;
		if (m_socket.sendTo(e.first, e.second.data(), e.second.size()))
		{
			++m_stats.packetsSent;
		}
		else
		{
			++m_stats.sendErrors;
			error("sending " + std::to_string(e.second.size()) + " bytes to " + e.first.text() + " failed (the records are resent)");
		}
		m_delayed.erase(m_delayed.begin());
	}
}
