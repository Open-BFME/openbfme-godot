// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// UDP and Transport (ZH GameNetwork/udp.cpp, Transport.cpp, Connection.cpp), lane MP-1: the socket and the per-peer reliable, ordered delivery the lockstep
// Network needs (NetTransport).
//
// DONOR FACTS (ZH): a packet starts with a packet CRC and a magic number (NetworkDefs.h TransportMessageHeader: UnsignedInt crc first, UnsignedShort
// magic); commands that need an ack are kept and resent until acked (Connection::doRetryMetrics / m_retryTime, ACKBOTH); packets stay below MAX_PACKET_SIZE
// (476 bytes: 512 minus the UDP/IP headers); a command too big for one packet is split (NETCOMMANDTYPE_WRAPPER, NetCommandWrapperList reassembles it).
// OpenBFME DIFFERENCES (enhanced profile; retail's encoding is cross-play gate 1, stop S-721): one cumulative ack per peer instead of per-command acks, and
// fragments are plain sequenced records (not a wrapper command).
//
// Packet (version 2): u32 CRC-32 of everything after it, u16 magic 0x4642 ("BF"), u8 version 2, u8 sender slot, u32 cumulative ack (the next sequence
// number the sender expects FROM the receiver), u16 record count, then per record u32 sequence number, u16 fragment index, u16 fragment count, u32 length,
// the bytes. A command (NetPacket::encodeCommand) is one record, or `fragment count` records with consecutive sequence numbers when it does not fit one
// packet: every datagram stays within Options::packetBytes. Records are delivered once, in sequence order; the fragments of a command are joined before the
// command is handed out.
// A packet is validated WHOLE before anything is applied (CRC, magic, version, a known sender at its address, every record's layout, the exact end, the ack
// not beyond the sequence numbers issued, every record inside the receive window); an invalid packet changes no delivery state and is counted.
// BOUNDS (enforced on the receive side during validation, before any state changes): a transport datagram is at most Options::maxPacketBytes (the
// receive ceiling; every peer of a game uses the same packet size, packetBytes <= maxPacketBytes); a whole (unfragmented) command at most maxCommandBytes;
// at most Options::window records are in flight per peer and accepted ahead of the next expected one; and the bytes a peer may hold in the reorder buffer
// plus the partial command being joined are at most Options::maxBufferedBytes (records that the packet itself completes in order do not count). The
// constructor raises the budget to the invariant maxCommandBytes + (window - 1) * (maxPacketBytes - 26) (a maximum-size partial command plus a maximum-size
// early record in every other window slot) and keeps the window within the serial half-range, so the next expected fragment of an unfinished command
// always fits and valid traffic cannot deadlock at the budget; the budget refuses only what the window and record limits already forbid (defence). A command (sendTo) is at most maxCommandBytes.
// ACK RANGE: the transport counts the records issued to a peer (and the records acknowledged, monotonic) in 64 bits; an ack names the next sequence number
// the peer expects, so (nextSeq - ack) mod 2^32 must not exceed the count issued (a half-range or far-future value is refused atomically), and the
// extended ack = issued - that distance: at or below the acknowledged count it is stale and removes nothing, above it only the records with a lower ordinal
// are removed. No signed serial comparison is used for trimming. After 2^32 records a wire value aliases an earlier cycle (the limit of a 32-bit field).
// A peer that left the game (Network: PLAYERLEAVE) is retired: its queues are dropped, one last ack goes out, then it is forgotten.
//
// RELAY MODE (lane ONLINE-2, docs/ONLINE.md phase O1): with Options::relay set, every peer's address is the relay's; each packet goes out in the relay
// envelope (GameNetwork/RelayProtocol.h: DATA + the target slot) and comes in as DATA_FROM + the source slot, which the relay stamped from the session its
// ticket bound. The WELCOME's session token is kept and sent with every later HELLO: only it lets the relay move the slot to a new address. The "known sender at its address" check so becomes a slot check: the packet's sender byte must equal the envelope's source slot (the relay
// already refused a packet whose sender byte is not the bound slot of its address). The transport sends HELLO (the ticket) until the relay WELCOMEs it, and
// again when the relay answers NOT_BOUND (a NAT rebinding, a relay restart); a REFUSED is an error (errors(), relayRefusal()). The packets inside are
// unchanged: acks, resends and fragments stay end to end.
// Lane ONLINE-3 (reconnect): the ticket may be a rejoin ticket (version 2, 34 bytes); the relay's STATUS (each slot's generation, silent at the relay) is
// kept for the Network (peerGeneration / silentAtRelay; nothing here acts on it), and so is the time anything last came from the relay (localLinkAlive:
// the disconnect path's ping test). resetPeer starts a slot's streams afresh (its new incarnation). The relay's command log is fetched in LOG_CHUNKs
// (requestRelayLog / takeRelayLogChunks). sendToLog: a frame command that reaches no other player goes in a stream to the local slot's own entry,
// which the relay logs and acknowledges (DATA_FROM of the local slot with an ack-only packet) and forwards nowhere; it sends no keep-alives.
//
// Not simulation code: time and sockets only decide WHEN bytes arrive, never what a frame contains (Network.h).

#pragma once

#include "GameNetwork/NetImpairment.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/RelayAuth.h"
#include "GameNetwork/RelayBundleChannel.h"
#include "GameNetwork/RelayCanonicalInput.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// an IPv4 address and port (host byte order)
struct NetAddress
{
	std::uint32_t ip = 0;
	std::uint16_t port = 0;
	bool operator==(const NetAddress &o) const { return ip == o.ip && port == o.port; }
	bool operator<(const NetAddress &o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
	std::string text() const;
	static bool parse(const std::string &text, NetAddress &out); // "a.b.c.d:port"
};

// ZH UDP: a non-blocking IPv4 datagram socket
class UDP
{
public:
	UDP();
	~UDP();
	UDP(const UDP &) = delete;
	UDP &operator=(const UDP &) = delete;
	// binds to ip:port (port 0: any free port); false + *error
	bool bind(std::uint32_t ip, std::uint16_t port, std::string *error);
	bool isOpen() const;
	// lane MP-2: allows datagrams to broadcast addresses (the LAN lobby, ZH UDP::AllowBroadcasts)
	bool enableBroadcast();
	NetAddress localAddress() const;
	bool sendTo(const NetAddress &to, const std::uint8_t *data, size_t size);
	// one waiting datagram, false when none
	bool receiveFrom(NetAddress &from, std::vector<std::uint8_t> &data);

private:
	long long m_socket;
};

// lane FB7-1: the machine's IPv4 addresses as ZH IPEnumeration::getAddresses finds them (gethostname, then gethostbyname's address list, in its order;
// RotWK's Options address list RW 0x719C53 / 0x9122FC is that enumeration): host-order values; empty when the host name does not resolve
std::vector<std::uint32_t> LocalIPv4Addresses();

// monotonic milliseconds (transport pacing only)
std::uint64_t NetMilliseconds();
void NetSleepMilliseconds(int ms);

class Transport : public NetTransport
{
public:
	struct Options
	{
		int resendMs = 60;              ///< unacked records go again after this (ZH Connection retry); lane MP-2: at least the measured round trip, doubled per resend
		int keepAliveMs = 100;          ///< an empty packet (ack only) at least this often
		size_t packetBytes = 1200;      ///< every datagram sent is at most this big (at least 64, at most 65507)
		size_t maxPacketBytes = 0;      ///< the receive ceiling for a transport datagram; 0 = packetBytes (peers of a game share the packet size)
		size_t maxBufferedBytes = 16u << 20; ///< receive budget per peer: early records + the partial command (at least maxCommandBytes)
		size_t maxCommandBytes = 8u << 20; ///< a bigger command is refused
		std::uint32_t window = 16384;   ///< records in flight / accepted ahead of the next expected one, per peer
		// lane MP-3 (NET-4): the test harness's faults (GameNetwork/NetImpairment.h: per-link latency, jitter, loss, duplication, reordering; blackouts
		// through impairment()); none by default
		NetImpairment::Config impairment;
		// lane MP-3: the fast resend and the bounded backoff (Transport.cpp, S-1891); false = MP-2's timing (A/B measurements)
		bool fastResend = true;
		// lane ONLINE-2: the online relay (RELAY MODE above); disabled = LAN (peer to peer)
		struct Relay
		{
			bool enabled = false;
			NetAddress address;               ///< the relay's UDP address (every peer's address in this mode)
			std::vector<std::uint8_t> ticket; ///< the lobby's ticket for this client's slot (32 bytes; lane ONLINE-3: 34, a rejoin ticket)
			std::vector<std::uint8_t> macKey; ///< lane AUTH-1: the START's per-slot secret (32 bytes; empty = the legacy unauthenticated envelope)
			int helloMs = 250;                ///< HELLO again after this while not welcomed
			int spectatorIndex = -1;          ///< lane ONLINE-3: a spectator ticket's index (WELCOME names 0x80 + it); -1: a player
		} relay;
	};
	Transport(UDP &socket, int localSlot, const Options &options);
	~Transport() override; ///< lane ONLINE-2: in relay mode, says BYE to the relay

	// sendStart / receiveStart: the first sequence number of the records sent to / expected from the peer (0 in a game; a test uses others to cross the 32-bit wrap)
	void setPeer(int slot, const NetAddress &address, std::uint32_t sendStart = 0, std::uint32_t receiveStart = 0);
	// NetTransport
	bool sendTo(int slot, const std::vector<std::uint8_t> &bytes, std::string *error) override;
	bool receive(int &fromSlot, std::vector<std::uint8_t> &bytes) override;
	// test harness: as if `records` more records had been sent to the slot and all acknowledged (to reach the lifetime boundaries of the 32-bit sequence
	// numbers without sending 2^31 records)
	void fastForwardSend(int slot, std::uint64_t records);
	const Options &options() const { return m_options; } ///< the effective options (the constructor raises / clamps some)
	// drops everything queued for / buffered from the slot, sends it one last ack, then forgets it
	void retire(int slot) override;
	// sends what is due, reads every waiting datagram; call often
	void service() override;
	// Test clock, as in Network: set before servicing; monotonic milliseconds, real time by default.
	void setClock(std::function<std::uint64_t()> clock) { m_clock = std::move(clock); }

	struct Stats
	{
		unsigned long long packetsSent = 0, packetsReceived = 0, commandsSent = 0, recordsSent = 0, commandsResent = 0, commandsDelivered = 0, duplicates = 0;
		unsigned long long badPackets = 0, unknownSenders = 0, dropped = 0, sendErrors = 0, fragmentedCommands = 0, peersRetired = 0;
		// lane ONLINE-1 (the relay's bandwidth budget, docs/ONLINE.md): the transport datagrams' bytes (UDP payload, IP / UDP headers not included) handed to
		// the socket and read from it, and the datagrams sent that carry no record (an ack or a keep-alive only)
		unsigned long long bytesSent = 0, bytesReceived = 0, ackOnlyPacketsSent = 0;
		unsigned long long fastResends = 0; ///< lane MP-3: records resent on ack evidence before their timeout
		// lane ONLINE-2 (relay mode): HELLOs sent, WELCOMEs and NOT_BOUNDs received, envelope bytes (5 per data datagram) included in bytesSent
		unsigned long long relayHellos = 0, relayWelcomes = 0, relayNotBound = 0, relaySlotMismatches = 0;
		unsigned long long relayAuthDropped = 0; ///< lane AUTH-1: relay datagrams dropped on a bad tag / replayed counter
		unsigned long long relayAuthStale = 0;   ///< ONLINE16-FIX: of those, the ones with a good tag the replay window refused
		unsigned long long relayLegacyEnvelope = 0; ///< ONLINE16-FIX: a version-3-only relay was met (the client then speaks version 3)
	};
	const Stats &stats() const { return m_stats; }
	// socket send failures and protocol violations of known peers (at most 100 kept), for the reports
	const std::vector<std::string> &errors() const { return m_errors; }
	// lane MP-2: the smoothed round trip to the slot and its variation (ms, RFC 6298 from acknowledged records sent once); -1 before the first sample
	int roundTripMs(int slot) const override { return m_peers[(size_t)slot].rttSamples ? (int)m_peers[(size_t)slot].srtt : -1; }
	int roundTripVarMs(int slot) const override { return m_peers[(size_t)slot].rttSamples ? (int)m_peers[(size_t)slot].rttvar : -1; }
	// lane NETLAG-2: the 80th percentile of the last kRttWindow round-trip samples (the run-ahead's input), -1 before kRttPlanSamples of them: a few
	// outliers (one late ack after a hitch of the other client, the load barrier) leave it where the link is; a slower link moves it within seconds
	int plannedRoundTripMs(int slot) const override;
	static constexpr size_t kRttWindow = 32;      ///< about six seconds of samples (a record goes to every peer each 200 ms frame)
	static constexpr size_t kRttPlanSamples = 16; ///< about three seconds
	// the last time anything arrived from the slot (NetMilliseconds), 0 never
	std::uint64_t lastHeardFrom(int slot) const override { return m_peers[(size_t)slot].lastHeard; }
	// every record sent to the slot is acked (a retired or unknown slot: true)
	bool allAcked(int slot) const { return m_peers[(size_t)slot].unacked.empty(); }
	// the records waiting for an ack, every peer
	size_t unackedRecords() const;
	bool isPeer(int slot) const { return m_peers[(size_t)slot].known; }
	// lane MP-3: the fault injector (a test or the peer's --blackout-at cuts the endpoint off with impairment().blackout(NetMilliseconds(), ms))
	NetImpairment &impairment() { return m_impairment; }
	const NetImpairment &impairment() const { return m_impairment; }

	// lane ONLINE-2: relay mode: the relay welcomed this client (its address is bound to the ticket's game and slot); the refusal reason
	// (RelayProtocol::RefuseReason) of a REFUSED, 0 none
	bool relayBound() const { return m_relayBound; }
	// lane NET-SYNC: the canonical mode: queued records (OBRC bytes) drain via takeCanonicalRecord; orders leave via sendCanonicalOrder
	bool relayCanonical() const override { return m_relayCanonical; }
	bool sendCanonicalOrder(std::uint64_t id, const std::vector<std::uint8_t> &bytes, std::uint64_t now, std::string &error) override;
	bool sendCanonicalBeat(std::uint64_t id, std::uint64_t now, std::string &error) override;
	bool takeCanonicalRecord(std::vector<std::uint8_t> &bytes) override;
	std::uint64_t canonicalGame() const override;
	std::uint64_t canonicalEpoch() const override { return m_relayEpoch; }
	int relayRefusal() const { return m_relayRefusal; }
	bool relayLegacyEnvelope() const { return m_relayLegacy; } ///< ONLINE16-FIX: speaking the previous release's envelope (version 3)

	// lane ONLINE-3 (relay mode): NetTransport's view of the relay's STATUS and the local link
	int peerGeneration(int slot) const override { return slot >= 0 && slot < MAX_SLOTS ? m_relayStatus[(size_t)slot].generation : 0; }
	bool silentAtRelay(int slot) const override { return slot >= 0 && slot < MAX_SLOTS && m_relayStatus[(size_t)slot].silent; }
	bool localLinkAlive(std::uint32_t ms) const override;
	void resetPeer(int slot) override;
	// lane ONLINE-3: the log-only stream (records to the local slot's own entry, which the relay logs and acknowledges and forwards nowhere)
	bool sendToLog(const std::vector<std::uint8_t> &bytes) override;
	// lane ONLINE-3: the relay's command log of this game: asks for the bytes from `offset` (one LOG_CHUNK answers); the chunks that arrived since the last
	// call (offset -> bytes) and the log's size the relay last reported; a REFUSED (no log) shows in relayRefusal()
	void requestRelayLog(std::uint32_t offset);
	std::map<std::uint32_t, std::vector<std::uint8_t>> takeRelayLogChunks() { return std::move(m_logChunks); }
	std::uint32_t relayLogSize() const { return m_logSize; }
	bool relayLogRefused() const { return m_logRefused; } ///< the relay keeps no log of this game (REFUSED: REFUSE_NO_LOG)
	unsigned long long spectatorRefusals() const { return m_spectatorRefusals; } ///< lane ONLINE-3: REFUSED REFUSE_SPECTATOR (a spectator's DATA)
	unsigned long long relayStatuses() const { return m_relayStatuses; }

	// the lobby's datagrams share the socket: a datagram that is not a transport packet is handed here (null: counted as bad)
	std::vector<std::pair<NetAddress, std::vector<std::uint8_t>>> &foreignDatagrams() { return m_foreign; }

private:
	struct Outgoing
	{
		std::uint32_t seq;
		std::uint64_t ordinal;           ///< zero-based count of the records issued to the peer before this one
		std::uint16_t fragIndex, fragCount;
		std::vector<std::uint8_t> bytes;
		std::uint64_t lastSent = 0;
		bool sent = false;
		bool resent = false; ///< lane MP-2: sent more than once (no round-trip sample)
		int resends = 0;     ///< lane MP-2: how often
		bool fastDue = false;  ///< lane MP-3: an ack arrived without it a round trip after it went out: resend at once (no backoff)
		bool fastDone = false; ///< lane MP-3: at most one fast resend per record
	};
	struct Incoming
	{
		std::uint16_t fragIndex = 0, fragCount = 1;
		std::vector<std::uint8_t> bytes;
	};
	struct Peer
	{
		bool known = false;
		bool retiring = false;          ///< retire(): one last ack, then forgotten
		NetAddress address;
		std::uint64_t issued = 0;       ///< records given to this peer since setPeer (an ack can name at most this many)
		std::uint64_t acked = 0;        ///< records the peer has acknowledged (monotonic): the unacked records are the ordinals acked .. issued - 1
		std::uint32_t nextSeq = 0;      ///< next sequence number to give a record to this peer
		std::deque<Outgoing> unacked;
		std::uint32_t expected = 0;     ///< the next sequence number expected from this peer
		std::map<std::uint32_t, Incoming> early; ///< received ahead of `expected`
		std::vector<std::uint8_t> partial; ///< the fragments of the command being joined
		std::uint16_t partialNext = 0, partialCount = 0;
		std::uint64_t lastSent = 0, lastHeard = 0;
		bool ackOwed = false;
		std::uint64_t srtt = 0, rttvar = 0; ///< lane MP-2: the smoothed round trip and its variation (ms)
		std::uint64_t rto = 0;              ///< lane MP-2: the resend timeout (ms; at least Options::resendMs)
		unsigned long long rttSamples = 0;
		std::array<std::uint32_t, kRttWindow> recentRtt{}; ///< lane NETLAG-2: the last samples, a ring (rttSamples counts every sample)
	};
	void flush(int slot, std::uint64_t now);
	void sendDatagram(int slot, const NetAddress &to, std::vector<std::uint8_t> datagram, std::uint64_t now);
	void sendNow(const NetAddress &to, const std::vector<std::uint8_t> &datagram);
	void handleDatagram(const NetAddress &from, std::vector<std::uint8_t> &d, std::uint64_t now);
	// envelopeSlot: relay mode, the source slot the relay stamped (-1: peer to peer)
	void handlePacket(const NetAddress &from, const std::vector<std::uint8_t> &d, std::uint64_t now, int envelopeSlot);
	void handleRelay(std::vector<std::uint8_t> &d, std::uint64_t now); ///< lane ONLINE-2 (AUTH-1: the tag check rewrites the buffer in place)
	void sendHello(std::uint64_t now);
	void deliver(int slot, Incoming &&record);
	void error(const std::string &e);
	std::uint64_t nowMs() const;

	UDP &m_socket;
	int m_localSlot;
	Options m_options;
	std::function<std::uint64_t()> m_clock;
	std::array<Peer, MAX_SLOTS> m_peers;
	std::deque<std::pair<int, std::vector<std::uint8_t>>> m_delivered;
	std::multimap<std::uint64_t, std::pair<NetAddress, std::vector<std::uint8_t>>> m_delayed;
	std::vector<std::pair<NetAddress, std::vector<std::uint8_t>>> m_foreign;
	NetImpairment m_impairment; ///< lane MP-3
	std::vector<std::uint64_t> m_sendTimes;
	Stats m_stats;
	std::vector<std::string> m_errors;
	bool m_relayBound = false;   ///< lane ONLINE-2
	int m_relayRefusal = 0;
	std::uint64_t m_lastHello = 0;
	bool m_helloSent = false;
	std::vector<std::uint8_t> m_relaySession; ///< ONLINE-2 r2: the relay's session token of this slot (from WELCOME), sent with every later HELLO
	// lane ONLINE-3
	struct RelaySlotStatus
	{
		int generation = 0;
		bool silent = false;
	};
	std::array<RelaySlotStatus, MAX_SLOTS> m_relayStatus{};
	// lane NET-SYNC: canonical games (the ticket's mode has TICKET_MODE_CANONICAL): the relay keeps the beat. The
	// WELCOME's epoch tail opens this client's NS channel; orders leave as OBCI messages, committed OBRC records
	// arrive back. `m_canonicalRecords` is the ordered commit stream Network consumes.
	bool m_relayCanonical = false;
	std::uint64_t m_relayEpoch = 0;
	std::unique_ptr<RelayBundleChannel> m_relayChannel;
	std::deque<std::vector<std::uint8_t>> m_canonicalRecords;
	bool emitCanonicalMessage(const RelayCanonicalInput::Message &message, std::uint64_t now, std::string &error);
	std::uint64_t m_lastFromRelay = 0;
	unsigned long long m_relayStatuses = 0;
	std::map<std::uint32_t, std::vector<std::uint8_t>> m_logChunks;
	std::uint32_t m_logSize = 0;
	bool m_logRefused = false;
	unsigned long long m_spectatorRefusals = 0;
	// lane AUTH-1: the START's macKey derives the direction keys once; c2r seals every outgoing datagram, r2c +
	// the window verify every incoming one. Empty macKey = the legacy unauthenticated path (an old lobby's START)
	bool m_auth = false;
	RelayAuth::Keys m_authKeys;
	std::uint64_t m_authSent = 0; ///< the c2r counter (HELLO counts too)
	RelayAuth::Window m_authRecv; ///< the r2c replay window
	bool m_authVerified = false;  ///< ONLINE16-FIX: a sealed datagram of the relay verified (unsigned refusals stop counting)
	bool m_relayLegacy = false;   ///< ONLINE16-FIX: the relay speaks only version 3 (never together with m_auth)
	void sealRelay(std::vector<std::uint8_t> &d); ///< lane AUTH-1: upgrades a built v4 datagram to v5 when authed
};
