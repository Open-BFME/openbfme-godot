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
// Not simulation code: time and sockets only decide WHEN bytes arrive, never what a frame contains (Network.h).

#pragma once

#include "GameNetwork/Network.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
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
		// test harness (ZH DelayedTransportMessage / NET-4): drop this per-mille of outgoing datagrams (seeded, reproducible), delay them by this much plus a
		// seeded 0 .. jitterMs (the jitter reorders them)
		int dropPerMille = 0;
		int delayMs = 0;
		int jitterMs = 0;
		std::uint32_t dropSeed = 1;
	};
	Transport(UDP &socket, int localSlot, const Options &options);

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

	struct Stats
	{
		unsigned long long packetsSent = 0, packetsReceived = 0, commandsSent = 0, recordsSent = 0, commandsResent = 0, commandsDelivered = 0, duplicates = 0;
		unsigned long long badPackets = 0, unknownSenders = 0, dropped = 0, sendErrors = 0, fragmentedCommands = 0, peersRetired = 0;
	};
	const Stats &stats() const { return m_stats; }
	// socket send failures and protocol violations of known peers (at most 100 kept), for the reports
	const std::vector<std::string> &errors() const { return m_errors; }
	// lane MP-2: the smoothed round trip to the slot and its variation (ms, RFC 6298 from acknowledged records sent once); -1 before the first sample
	int roundTripMs(int slot) const override { return m_peers[(size_t)slot].rttSamples ? (int)m_peers[(size_t)slot].srtt : -1; }
	int roundTripVarMs(int slot) const override { return m_peers[(size_t)slot].rttSamples ? (int)m_peers[(size_t)slot].rttvar : -1; }
	// the last time anything arrived from the slot (NetMilliseconds), 0 never
	std::uint64_t lastHeardFrom(int slot) const override { return m_peers[(size_t)slot].lastHeard; }
	// every record sent to the slot is acked (a retired or unknown slot: true)
	bool allAcked(int slot) const { return m_peers[(size_t)slot].unacked.empty(); }
	// the records waiting for an ack, every peer
	size_t unackedRecords() const;
	bool isPeer(int slot) const { return m_peers[(size_t)slot].known; }

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
	};
	void flush(int slot, std::uint64_t now);
	void sendDatagram(const NetAddress &to, std::vector<std::uint8_t> datagram, std::uint64_t now);
	void handleDatagram(const NetAddress &from, const std::vector<std::uint8_t> &d, std::uint64_t now);
	void deliver(int slot, Incoming &&record);
	void error(const std::string &e);

	UDP &m_socket;
	int m_localSlot;
	Options m_options;
	std::array<Peer, MAX_SLOTS> m_peers;
	std::deque<std::pair<int, std::vector<std::uint8_t>>> m_delivered;
	std::multimap<std::uint64_t, std::pair<NetAddress, std::vector<std::uint8_t>>> m_delayed;
	std::vector<std::pair<NetAddress, std::vector<std::uint8_t>>> m_foreign;
	std::uint32_t m_dropState;
	Stats m_stats;
	std::vector<std::string> m_errors;
};
