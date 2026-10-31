// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/Transport.h.

#include "GameNetwork/Transport.h"

#include "Common/Crc32.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _WIN32
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS // lane FB7-1: gethostbyname, as ZH IPEnumeration uses it
#endif
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
#include <netdb.h>
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
	int n;
	for (;;)
	{
		socklen_type len = sizeof(a);
		n = (int)recvfrom((decltype(socket(0, 0, 0)))m_socket, (char *)data.data(), (int)data.size(), 0, (sockaddr *)&a, &len);
#ifdef _WIN32
		// Winsock reports ICMP port-unreachable from earlier sends here. It is not an empty receive queue.
		if (n < 0 && WSAGetLastError() == WSAECONNRESET)
		{
			continue;
		}
#endif
		break;
	}
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
	, m_impairment(options.impairment)
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
	const auto &ticket = m_options.relay.ticket;
	if (m_options.relay.enabled && (ticket.empty() || RelayProtocol::ticketSize(ticket.front()) == 0
		|| ticket.size() != RelayProtocol::ticketSize(ticket.front())))
	{
		error("relay mode needs a supported ticket version with its exact encoded length, got "
			+ std::to_string(ticket.size()) + " bytes: the relay will refuse it");
	}
	// lane AUTH-1: a START with a macKey authenticates every datagram (the authenticated envelope, version 5);
	// an old lobby's START has none and the game stays unauthenticated (never a silent weak key)
	if (m_options.relay.enabled && !m_options.relay.macKey.empty())
	{
		RelayProtocol::Ticket t;
		std::string why;
		if (m_options.relay.macKey.size() != RelayAuth::kSessionKeyBytes)
		{
			error("the START's session_key (macKey) is " + std::to_string(m_options.relay.macKey.size()) + " bytes, not "
				+ std::to_string(RelayAuth::kSessionKeyBytes) + ": the game stays unauthenticated");
		}
		else if (!RelayProtocol::readTicketFields(ticket.data(), ticket.size(), t, &why))
		{
			error("the START's session_key (macKey) cannot pair with its ticket (" + why + "): the game stays unauthenticated");
		}
		else
		{
			RelayAuth::SessionKey k;
			std::memcpy(k.data(), m_options.relay.macKey.data(), k.size());
			m_authKeys = RelayAuth::packetKeys(k, t.gameId, t.slot, t.generation);
			m_auth = true;
			// ONLINE16-FIX (AUTH-1 review 5): the c2r counter starts at wall-clock microseconds, never a fixed 0, so a restarted
			// client process of the same ticket is ahead of the relay's window for it
			m_authSent = (std::uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
		}
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

Transport::~Transport()
{
	// lane ONLINE-2: the relay closes the game's log once every bound slot said BYE (two copies: it is not acked; a lost one costs the relay's silence timeout)
	if (m_options.relay.enabled && m_relayBound)
	{
		const std::vector<std::uint8_t> bye = RelayProtocol::encodeEmpty(RelayProtocol::BYE);
		for (int i = 0; i < 2; ++i)
		{
			std::vector<std::uint8_t> d = bye;
			sealRelay(d); // lane AUTH-1
			m_socket.sendTo(m_options.relay.address, d.data(), d.size());
		}
	}
}

void Transport::sealRelay(std::vector<std::uint8_t> &d)
{
	if (m_relayLegacy)
	{
		d[2] = RelayProtocol::kVersionLegacy; // ONLINE16-FIX: a version-3-only relay (never with m_auth)
	}
	if (m_auth)
	{
		RelayAuth::seal(d, m_authKeys.c2r, m_authSent++); // lane AUTH-1
	}
}

void Transport::sendHello(std::uint64_t now)
{
	std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(m_options.relay.ticket, m_relaySession, // ONLINE-2 r2: the token proves the slot
		(std::uint8_t)(RelayProtocol::CAP_CANONICAL | (m_auth ? RelayProtocol::CAP_AUTH : 0))); // NET-SYNC + AUTH-1 capabilities
	if (m_relayLegacy)
	{
		hello.pop_back(); // ONLINE16-FIX: the previous release's HELLO declares no capabilities (sealRelay stamps version 3)
	}
	if (m_impairment.blackedOut(now))
	{
		return; // lane MP-3: a blackout cuts the HELLO too
	}
	sealRelay(hello); // lane AUTH-1: the HELLO itself carries the tag its k proves
	sendNow(m_options.relay.address, hello);
	++m_stats.relayHellos;
	m_lastHello = now;
	m_helloSent = true;
}

void Transport::handleRelay(std::vector<std::uint8_t> &d, std::uint64_t now)
{
	namespace RP = RelayProtocol; // (this file has its own kVersion / kHeaderBytes)
	// lane AUTH-1: the authenticated envelope verifies before anything reads it; only the two negotiation
	// answers stay acceptable unsigned (a forged NOT_BOUND costs an early re-HELLO, never an auth break)
	const bool refuseVersion = d.size() == RP::kHeaderBytes + 1 && d[3] == RP::REFUSED && d[RP::kHeaderBytes] == RP::REFUSE_VERSION
		&& (d[2] == RP::kVersion || d[2] == RP::kVersionLegacy);
	if (d[2] == RP::kVersionAuth)
	{
		const size_t n = m_auth ? RelayAuth::open(d.data(), d.size(), m_authKeys.r2c, &m_authRecv) : 0;
		if (!n)
		{
			++m_stats.relayAuthDropped;
			// ONLINE16-FIX: which check refused it — a refused datagram is untouched, so the tag alone is checked again (no window): a good tag
			// means the replay window refused a stale or repeated counter
			if (m_auth && RelayAuth::open(d.data(), d.size(), m_authKeys.r2c, nullptr))
			{
				++m_stats.relayAuthStale;
			}
			return;
		}
		d.resize(n); // open() compacted the datagram to its v4 view
		m_authVerified = true;
	}
	else if (m_auth)
	{
		// ONLINE16-FIX (AUTH-1 review 4): an unsigned answer can be anyone's. A NOT_BOUND (the relay forgot this address:
		// a restart, a NAT rebinding — it has no key to sign with) only earns a rate-limited HELLO; it touches neither the
		// binding nor the link timer. A REFUSE_VERSION counts only before the relay ever proved itself (a relay without
		// AUTH-1); afterwards it is a forgery
		if (d.size() == RP::kHeaderBytes && d[3] == RP::NOT_BOUND && (d[2] == RP::kVersion || d[2] == RP::kVersionLegacy))
		{
			++m_stats.relayNotBound;
			if (!m_helloSent || now - m_lastHello >= (std::uint64_t)m_options.relay.helloMs)
			{
				sendHello(now);
			}
			return;
		}
		if (refuseVersion && !m_authVerified && m_relayRefusal == 0)
		{
			m_relayRefusal = RP::REFUSE_VERSION;
			m_relayBound = false;
			error(d[2] == RP::kVersionLegacy
					? "the relay is an older release (envelope version 3) than this game's lobby: it cannot authenticate this game"
					: std::string("the relay refused this client: ") + RP::refuseReasonName(RP::REFUSE_VERSION));
			return;
		}
		++m_stats.relayAuthDropped; // an authenticated client hears the relay authenticated only
		return;
	}
	else if (d[2] == RP::kVersionLegacy && !m_relayLegacy && refuseVersion && !m_relayBound)
	{
		// ONLINE16-FIX: a version-3-only relay (the previous release's) refused this build's version 4. A START without a
		// macKey whose ticket the previous release reads (versions 1-3: eight slots, no mode) plays on in its envelope; any
		// other ticket would be refused there anyway — a clear refusal instead
		const std::uint8_t ticketVersion = m_options.relay.ticket.empty() ? 0 : m_options.relay.ticket.front();
		if (ticketVersion >= 1 && ticketVersion <= 3)
		{
			m_relayLegacy = true;
			++m_stats.relayLegacyEnvelope;
			sendHello(now);
			return;
		}
		if (m_relayRefusal == 0)
		{
			m_relayRefusal = RP::REFUSE_VERSION;
			error("the relay speaks only the previous release's envelope (version 3), which cannot carry this game's version-"
				+ std::to_string(ticketVersion) + " ticket");
		}
		return;
	}
	if (m_relayLegacy && refuseVersion)
	{
		return; // ONLINE16-FIX: the answer to a version-4 datagram sent before the switch (a version-3 relay never refuses version 3)
	}
	if (d[2] != (m_relayLegacy ? RP::kVersionLegacy : RP::kVersion))
	{
		++m_stats.badPackets;
		error("the relay speaks envelope version " + std::to_string(d[2]) + ", this client " + std::to_string(m_relayLegacy ? RP::kVersionLegacy : RP::kVersion));
		return;
	}
	m_lastFromRelay = now; // lane ONLINE-3: the ping test (anything from the relay)
	switch (d[3])
	{
	case RP::STATUS:
	{
		// lane ONLINE-3: every slot's generation and whether the relay hears it; the Network acts on it (it alone decides who is in the game)
		std::vector<RP::SlotStatus> st;
		if (!RP::decodeStatus(d.data(), d.size(), st))
		{
			++m_stats.badPackets;
			return;
		}
		++m_relayStatuses;
		for (const RP::SlotStatus &e : st)
		{
			m_relayStatus[e.slot].generation = std::max(m_relayStatus[e.slot].generation, (int)e.generation);
			m_relayStatus[e.slot].silent = e.silent;
		}
		return;
	}
	case RP::LOG_CHUNK:
	{
		// lane ONLINE-3: a piece of the relay's command log (requestRelayLog)
		if (d.size() < RP::kHeaderBytes + 8 || d.size() > RP::kHeaderBytes + 8 + RP::kLogChunkBytes)
		{
			++m_stats.badPackets;
			return;
		}
		auto u32 = [&](size_t at) { return (std::uint32_t)d[at] | ((std::uint32_t)d[at + 1] << 8) | ((std::uint32_t)d[at + 2] << 16) | ((std::uint32_t)d[at + 3] << 24); };
		const std::uint32_t offset = u32(RP::kHeaderBytes), total = u32(RP::kHeaderBytes + 4);
		m_logSize = std::max(m_logSize, total);
		if (m_logChunks.size() < 4096)
		{
			m_logChunks[offset].assign(d.begin() + (long)(RP::kHeaderBytes + 8), d.end());
		}
		return;
	}
	case RP::DATA_FROM:
	{
		if (d.size() < RP::kHeaderBytes + 1)
		{
			++m_stats.badPackets;
			return;
		}
		const int source = d[RP::kHeaderBytes];
		const std::vector<std::uint8_t> packet(d.begin() + (long)(RP::kHeaderBytes + 1), d.end());
		if (m_relayCanonical && m_relayChannel)
		{
			// lane NET-SYNC: an NS channel packet, not a transport packet: acks return at once; delivered OBRC
			// records queue for the canonical consumer
			RelayBundleChannel::Bytes ack;
			std::string error;
			if (!m_relayChannel->receive(packet, now, ack, error))
			{
				++m_stats.badPackets;
				return;
			}
			if (!ack.empty())
			{
				std::vector<std::uint8_t> envelope = ack;
				sealRelay(envelope); // lane AUTH-1: the channel's ack packet is already a DATA envelope (queue made it)
				m_socket.sendTo(m_options.relay.address, envelope.data(), envelope.size());
			}
			for (const RelayBundleChannel::Message &m : m_relayChannel->takeDelivered())
			{
				m_canonicalRecords.push_back(m.bytes);
			}
			return;
		}
		handlePacket(m_options.relay.address, packet, now, source);
		return;
	}
	case RP::WELCOME:
		if ((d.size() == RP::kHeaderBytes + 9 + RP::kSessionTokenBytes
				|| d.size() == RP::kHeaderBytes + 9 + RP::kSessionTokenBytes + 8)
			&& d[RP::kHeaderBytes + 8] == (m_options.relay.spectatorIndex >= 0 ? (std::uint8_t)(RP::kSpectatorWelcome + m_options.relay.spectatorIndex) : (std::uint8_t)m_localSlot))
		{
			m_relayBound = true;
			++m_stats.relayWelcomes;
			// ONLINE-2 r2: the slot's session token: a later HELLO (after NOT_BOUND: a NAT rebinding) proves with it that this client owns the slot
			m_relaySession.assign(d.begin() + (long)(RP::kHeaderBytes + 9), d.begin() + (long)(RP::kHeaderBytes + 9 + RP::kSessionTokenBytes));
			// lane NET-SYNC: the epoch tail names a canonical game: open the client's NS channel to the relay
			if (d.size() == RP::kHeaderBytes + 9 + RP::kSessionTokenBytes + 8)
			{
				m_relayEpoch = 0;
				for (int i = 0; i < 8; ++i)
				{
					m_relayEpoch |= (std::uint64_t)d[RP::kHeaderBytes + 9 + RP::kSessionTokenBytes + i] << (8 * i);
				}
				RelayBundleChannel::Options co;
				co.gameId = 0;
				for (int i = 0; i < 8; ++i)
				{
					co.gameId |= (std::uint64_t)d[RP::kHeaderBytes + i] << (8 * i);
				}
				co.epoch = m_relayEpoch;
				m_relayChannel = std::make_unique<RelayBundleChannel>(co);
				m_relayCanonical = true;
			}
		}
		else
		{
			++m_stats.badPackets;
		}
		return;
	case RP::NOT_BOUND:
		// the relay lost this address (a NAT rebinding or a relay restart): HELLO again at once (at most one per helloMs)
		++m_stats.relayNotBound;
		m_relayBound = false;
		if (now - m_lastHello >= (std::uint64_t)m_options.relay.helloMs)
		{
			sendHello(now);
		}
		return;
	case RP::REFUSED:
		if (d.size() == RP::kHeaderBytes + 1 && d[RP::kHeaderBytes] == RP::REFUSE_NO_LOG)
		{
			m_logRefused = true; // lane ONLINE-3: an answer to LOG_REQUEST; the binding stays
			return;
		}
		if (d.size() == RP::kHeaderBytes + 1 && d[RP::kHeaderBytes] == RP::REFUSE_SPECTATOR)
		{
			++m_spectatorRefusals; // lane ONLINE-3: a spectator's DATA was refused; the binding stays
			return;
		}
		if (d.size() == RP::kHeaderBytes + 1 && m_relayRefusal == 0)
		{
			m_relayRefusal = d[RP::kHeaderBytes];
			m_relayBound = false;
			error(std::string("the relay refused this client: ") + RP::refuseReasonName(m_relayRefusal));
		}
		return;
	default:
		++m_stats.badPackets; // HELLO / DATA / BYE go to the relay, never from it
		return;
	}
}

std::uint64_t Transport::nowMs() const
{
	return m_clock ? m_clock() : NetMilliseconds();
}

bool Transport::localLinkAlive(std::uint32_t ms) const
{
	// lane ONLINE-3: in relay mode the local link is alive while something arrived from the relay within `ms` (WELCOME, DATA_FROM, STATUS: a client hears
	// its peers' keep-alives every 100 ms through it); peer to peer there is no such test (RotWK's GameSpy ping test does not run in a LAN game)
	return !m_options.relay.enabled || (m_lastFromRelay != 0 && nowMs() - m_lastFromRelay <= (std::uint64_t)ms);
}

void Transport::resetPeer(int slot)
{
	// lane ONLINE-3: a new incarnation of the slot (its game restarted and rejoined): both directions start at sequence number 0, as at setPeer
	if (slot < 0 || slot >= MAX_SLOTS || slot == m_localSlot)
	{
		return;
	}
	setPeer(slot, m_options.relay.enabled ? m_options.relay.address : m_peers[(size_t)slot].address);
}

bool Transport::sendToLog(const std::vector<std::uint8_t> &bytes)
{
	// lane ONLINE-3: the stream to the relay's log (the local slot's own entry: the relay answers its acks as that slot)
	if (!m_options.relay.enabled)
	{
		return false;
	}
	Peer &sink = m_peers[(size_t)m_localSlot];
	if (!sink.known)
	{
		sink = Peer();
		sink.known = true;
		sink.address = m_options.relay.address;
	}
	std::string why;
	if (!sendTo(m_localSlot, bytes, &why))
	{
		error("the relay log stream: " + why);
		return false;
	}
	return true;
}

void Transport::requestRelayLog(std::uint32_t offset)
{
	if (!m_options.relay.enabled || !m_relayBound)
	{
		return;
	}
	std::vector<std::uint8_t> r = RelayProtocol::encodeLogRequest(offset);
	sealRelay(r); // lane AUTH-1
	if (!m_impairment.blackedOut(nowMs()))
	{
		sendNow(m_options.relay.address, r);
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
	p.address = m_options.relay.enabled ? m_options.relay.address : address; // lane ONLINE-2: every peer is behind the relay
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
	if (slot < 0 || slot >= MAX_SLOTS || !m_peers[(size_t)slot].known || m_peers[(size_t)slot].retiring || m_peers[(size_t)slot].dropped)
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

void Transport::retireDropped(int slot, std::uint32_t holdMs)
{
	if (slot < 0 || slot >= MAX_SLOTS || !m_peers[(size_t)slot].known || m_peers[(size_t)slot].retiring)
	{
		return;
	}
	Peer &p = m_peers[(size_t)slot];
	if (p.unacked.empty())
	{
		retire(slot); // nothing it has not acknowledged: as a leave
		return;
	}
	size_t bytes = 0;
	for (const Outgoing &o : p.unacked)
	{
		bytes += o.bytes.size() + kRecordHeaderBytes;
	}
	if (p.unacked.size() > kDroppedBacklogRecords || bytes > kDroppedBacklogBytes)
	{
		error("the dropped slot " + std::to_string(slot) + "'s backlog of " + std::to_string(p.unacked.size()) + " records (" + std::to_string(bytes)
			+ " bytes) is beyond the cap: not resent (if its game runs again it may not learn it was dropped)");
		retire(slot);
		return;
	}
	p.early.clear();
	p.partial.clear();
	p.partialNext = p.partialCount = 0;
	p.dropped = true;
	p.droppedAt = nowMs();
	p.holdMs = holdMs;
	++m_stats.peersRetired;
}

size_t Transport::droppedBacklog(int slot, size_t *bytes) const
{
	size_t b = 0, n = 0;
	if (slot >= 0 && slot < MAX_SLOTS && m_peers[(size_t)slot].dropped)
	{
		for (const Outgoing &o : m_peers[(size_t)slot].unacked)
		{
			b += o.bytes.size() + kRecordHeaderBytes;
		}
		n = m_peers[(size_t)slot].unacked.size();
	}
	if (bytes)
	{
		*bytes = b;
	}
	return n;
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

void Transport::sendNow(const NetAddress &to, const std::vector<std::uint8_t> &datagram)
{
	if (!m_socket.sendTo(to, datagram.data(), datagram.size()))
	{
		++m_stats.sendErrors;
		error("sending " + std::to_string(datagram.size()) + " bytes to " + to.text() + " failed (the records are resent)");
		return;
	}
	++m_stats.packetsSent;
	m_stats.bytesSent += datagram.size();
}

void Transport::sendDatagram(int slot, const NetAddress &to, std::vector<std::uint8_t> datagram, std::uint64_t now)
{
	if (!m_impairment.active())
	{
		sendNow(to, datagram);
		return;
	}
	// lane MP-3 (NET-4): the harness decides the datagram's fate on its link: lost, delayed (reordered), duplicated, or swallowed by a blackout
	m_impairment.schedule(slot, now, m_sendTimes);
	if (m_sendTimes.empty())
	{
		++m_stats.dropped;
		return;
	}
	for (size_t i = 0; i < m_sendTimes.size(); ++i)
	{
		if (m_sendTimes[i] <= now && m_delayed.empty())
		{
			sendNow(to, datagram);
		}
		else
		{
			m_delayed.insert({ m_sendTimes[i], { to, i + 1 < m_sendTimes.size() ? datagram : std::move(datagram) } });
		}
	}
}

int Transport::plannedRoundTripMs(int slot) const
{
	const Peer &p = m_peers[(size_t)slot];
	if (p.rttSamples < kRttPlanSamples)
	{
		return -1;
	}
	const size_t n = (size_t)std::min<unsigned long long>(p.rttSamples, kRttWindow);
	std::array<std::uint32_t, kRttWindow> sorted = p.recentRtt;
	std::sort(sorted.begin(), sorted.begin() + (std::ptrdiff_t)n);
	return (int)std::min<std::uint32_t>(sorted[n * 4 / 5], 0x7FFFFFFF); // the 80th percentile: the 26th of 32
}

void Transport::flush(int slot, std::uint64_t now)
{
	Peer &p = m_peers[(size_t)slot];
	if (p.dropped)
	{
		// lane MPDROP-3: a dropped peer gets its unacknowledged records (its DISCONNECTPLAYER last) until it acknowledges them: resent while the drop or
		// the last datagram from it is within holdMs, at most once a second; no acks, no keep-alives
		if (p.unacked.empty())
		{
			p = Peer(); // it has everything: forgotten (later datagrams from it count as unknown senders)
			return;
		}
		if (now - std::max(p.droppedAt, p.lastHeard) >= (std::uint64_t)p.holdMs)
		{
			// quiet until it sends again: a game hung far longer than NetworkPlayerTimeoutTime that runs again is answered with its drop (its packets set
			// lastHeard, handlePacket), a dead one gets nothing more
			return;
		}
		if (p.lastDroppedRound != 0 && now - p.lastDroppedRound < 1000)
		{
			return; // one round a second
		}
		p.lastDroppedRound = now;
		p.ackOwed = false;
		p.lastSent = now; // no keep-alive
		// a record goes again once per pass over the backlog (the cap's number of rounds): each round takes the next records in order, the front first
		p.rto = 1000 * ((kDroppedBacklogBytes + kDroppedRoundBytes - 1) / kDroppedRoundBytes) - 500;
	}
	size_t roundBytes = 0;
	size_t i = 0;
	bool sentAny = false;
	// an ack or a keep-alive is due (lane ONLINE-3: never to the relay log stream, which only carries records)
	const bool wantPacket = slot != m_localSlot && (p.ackOwed || now - p.lastSent >= (std::uint64_t)m_options.keepAliveMs);
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
	while ((i < inFlight || (!sentAny && wantPacket)) && !(p.dropped && roundBytes >= kDroppedRoundBytes))
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
			if (o.sent && now - o.lastSent < p.rto && !o.fastDue)
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
				if (o.fastDue)
				{
					++m_stats.fastResends; // lane MP-3: a fast resend is not a timeout: no backoff
				}
				else
				{
					timedOut = true;
				}
			}
			o.fastDue = false;
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
		if (count == 0)
		{
			++m_stats.ackOnlyPacketsSent;
		}
		if (m_options.relay.enabled)
		{
			d = RelayProtocol::encodeData((std::uint8_t)slot, d); // lane ONLINE-2: the envelope names the target slot
			sealRelay(d);                                         // lane AUTH-1
		}
		roundBytes += d.size();
		sendDatagram(slot, p.address, std::move(d), now);
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
		// lane MP-2: Karn's backoff. Lane MP-3: on a link with a measured round trip, at most twice its RFC 6298 timeout (a lockstep game sends a few
		// hundred bytes a second: the backoff guards against a timeout below the round trip, not against congestion; doubling up to 2 s after two losses
		// in a row stalled every peer for seconds)
		const std::uint64_t cap = p.rttSamples && m_options.fastResend ? std::max<std::uint64_t>((std::uint64_t)m_options.resendMs, std::min<std::uint64_t>(2000, 2 * (p.srtt + 4 * p.rttvar))) : 2000;
		p.rto = std::min<std::uint64_t>(cap, std::max<std::uint64_t>(p.rto, std::min<std::uint64_t>(cap, p.rto * 2)));
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

void Transport::handleDatagram(const NetAddress &from, std::vector<std::uint8_t> &d, std::uint64_t now)
{
	if (m_options.relay.enabled)
	{
		// lane ONLINE-2: only the relay talks to a client in relay mode, always in the envelope
		if (!(from == m_options.relay.address))
		{
			++m_stats.unknownSenders;
			return;
		}
		if (!RelayProtocol::isRelayDatagram(d))
		{
			++m_stats.badPackets;
			return;
		}
		handleRelay(d, now);
		return;
	}
	handlePacket(from, d, now, -1);
}

void Transport::handlePacket(const NetAddress &from, const std::vector<std::uint8_t> &d, std::uint64_t now, int envelopeSlot)
{
	if (d.size() < kHeaderBytes || get16(d.data() + 4) != kMagic)
	{
		if (envelopeSlot >= 0)
		{
			++m_stats.badPackets; // lane ONLINE-2: the relay carries transport packets only
			return;
		}
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
	if (envelopeSlot >= 0 && envelopeSlot != slot)
	{
		++m_stats.relaySlotMismatches; // lane ONLINE-2: the packet claims another slot than the session the relay stamped
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
				p.recentRtt[(size_t)(p.rttSamples % kRttWindow)] = (std::uint32_t)std::min<long long>(sample, 0xFFFFFFFFLL);
				++p.rttSamples;
				p.rto = std::max<std::uint64_t>((std::uint64_t)m_options.resendMs, std::min<std::uint64_t>(2000, p.srtt + 4 * p.rttvar));
			}
		}
		// lane MP-3: fast resend (TCP's fast retransmit by time, the wire unchanged): this packet's cumulative ack left the peer at most a one-way trip
		// ago, so a record that went out more than a smoothed round trip plus its variation before now and is still not covered was most likely lost: it
		// goes again with the next flush instead of after the full timeout (each record once; later losses wait for the timeout)
		if (p.rttSamples > 0 && m_options.fastResend)
		{
			const std::uint64_t threshold = p.srtt + std::max<std::uint64_t>(p.rttvar, 20);
			size_t marked = 0, scanned = 0;
			for (Outgoing &o : p.unacked)
			{
				if (!o.sent || marked >= 64 || ++scanned > 256)
				{
					break; // the unsent ones come last; a bounded scan per packet
				}
				if (now < o.lastSent + threshold)
				{
					continue; // sent (or resent) too recently to be judged
				}
				if (!o.fastDone)
				{
					o.fastDue = o.fastDone = true;
					++marked;
				}
			}
		}
	}
	if (p.dropped)
	{
		// lane MPDROP-3: a dropped peer's packet: its ack is taken (above) and lastHeard brings back its resends (flush); its records are not delivered
		++m_stats.droppedPeerPackets;
		return;
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

bool Transport::emitCanonicalMessage(const RelayCanonicalInput::Message &message, std::uint64_t now, std::string &error)
{
	if (!m_relayCanonical || !m_relayChannel)
	{
		error = "canonical send outside a canonical game";
		return false;
	}
	std::vector<std::uint8_t> wire;
	if (!RelayCanonicalInput::encode(message, wire, error) || !m_relayChannel->queue(wire, error))
	{
		return false;
	}
	for (const RelayBundleChannel::Bytes &packet : m_relayChannel->poll(now, 64))
	{
		// lane NET-SYNC: an NS packet leaves in a DATA envelope whose target byte is this slot (the relay reads it
		// as the channel's envelope byte, not a route)
		std::vector<std::uint8_t> datagram = RelayProtocol::encodeData((std::uint8_t)m_localSlot, packet);
		sealRelay(datagram); // lane AUTH-1
		m_socket.sendTo(m_options.relay.address, datagram.data(), datagram.size());
	}
	return true;
}

bool Transport::sendCanonicalOrder(std::uint64_t id, const std::vector<std::uint8_t> &bytes,
	std::uint64_t now, std::string &error)
{
	RelayCanonicalInput::Message message;
	message.kind = RelayCanonicalInput::Kind::Order;
	message.id = id;
	message.bytes = bytes;
	return emitCanonicalMessage(message, now, error);
}

bool Transport::sendCanonicalBeat(std::uint64_t id, std::uint64_t now, std::string &error)
{
	RelayCanonicalInput::Message message;
	message.kind = RelayCanonicalInput::Kind::Beat;
	message.id = id;
	return emitCanonicalMessage(message, now, error);
}

std::uint64_t Transport::canonicalGame() const
{
	return m_relayChannel ? m_relayChannel->options().gameId : 0;
}

bool Transport::takeCanonicalRecord(std::vector<std::uint8_t> &bytes)
{
	if (m_canonicalRecords.empty())
	{
		return false;
	}
	bytes = std::move(m_canonicalRecords.front());
	m_canonicalRecords.pop_front();
	return true;
}

void Transport::service()
{
	const std::uint64_t now = nowMs();
	NetAddress from;
	std::vector<std::uint8_t> d;
	while (m_socket.receiveFrom(from, d))
	{
		m_stats.bytesReceived += d.size();
		if (!m_impairment.acceptIncoming(now))
		{
			++m_stats.dropped; // lane MP-3: the cable is pulled
			continue;
		}
		handleDatagram(from, d, now);
	}
	if (m_relayCanonical && m_relayChannel)
	{
		std::string channelError;
		for (const RelayBundleChannel::Bytes &packet : m_relayChannel->poll(now, 64, &channelError))
		{
			std::vector<std::uint8_t> datagram = RelayProtocol::encodeData((std::uint8_t)m_localSlot, packet);
			sealRelay(datagram); // lane AUTH-1
			m_socket.sendTo(m_options.relay.address, datagram.data(), datagram.size());
		}
		if (!channelError.empty())
		{
			error(channelError); // the channel's own diagnostics (a fatal channel ends the game, it never silently defaults)
		}
	}
	if (m_options.relay.enabled && !m_relayBound && m_relayRefusal == 0 && (!m_helloSent || now - m_lastHello >= (std::uint64_t)m_options.relay.helloMs))
	{
		sendHello(now); // lane ONLINE-2: until the relay welcomes this client (data sent before is dropped there and resent by the records' timers)
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_peers[(size_t)s].known && (s != m_localSlot || m_options.relay.enabled)) // lane ONLINE-3: the local entry is the relay log stream
		{
			flush(s, now);
		}
	}
	while (!m_delayed.empty() && m_delayed.begin()->first <= now)
	{
		if (m_impairment.releaseDue(now))
		{
			sendNow(m_delayed.begin()->second.first, m_delayed.begin()->second.second);
		}
		else
		{
			++m_stats.dropped; // in flight when the blackout began
		}
		m_delayed.erase(m_delayed.begin());
	}
}

std::vector<std::uint32_t> LocalIPv4Addresses()
{
	// ZH IPEnumeration::getAddresses: gethostname, gethostbyname, every h_addr_list entry (network order -> host order)
	std::vector<std::uint32_t> out;
#ifdef _WIN32
	ensureWinsock();
#endif
	char host[256] = {};
	if (gethostname(host, sizeof host - 1) != 0)
	{
		return out;
	}
	const hostent *he = gethostbyname(host);
	if (!he || he->h_addrtype != AF_INET || he->h_length != 4)
	{
		return out;
	}
	for (int i = 0; he->h_addr_list[i]; ++i)
	{
		std::uint32_t a = 0;
		std::memcpy(&a, he->h_addr_list[i], 4);
		out.push_back(ntohl(a));
	}
	return out;
}
