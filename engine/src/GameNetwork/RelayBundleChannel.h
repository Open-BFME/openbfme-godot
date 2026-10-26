// OpenBFME. GPL-3.0.
//
// NET-SYNC substrate: a bounded, reliable, bidirectional byte channel for a
// caller-authenticated peer binding. This layer does not authenticate,
// durably commit, schedule, or interpret its bytes.
//
// Every channel packet is little-endian and has this fixed layout:
//   0  u8[2]  magic "NS"
//   2  u8     version (1)
//   3  u8     kind (1 data, 2 selective ACK)
//   4  u64    game id
//  12  u64    epoch
//  20  u64    message sequence (starts at 1 and never wraps)
//  28  u32    fragment index
//  32  u32    fragment count
//  36  u32    total message bytes
//  40  u16    fragment bytes (zero for an ACK)
//  42  u32    CRC-32 of every byte before the CRC field and the data payload
//  46  u8[]   fragment payload (data only)
//
// The packet's channel bytes, including the 46-byte header, are bounded by
// packetBytes - outerEnvelopeBytes. A data message is split canonically into
// consecutive fragments of the maximum payload size. ACKs repeat all message
// metadata and acknowledge exactly one fragment. CRC validation, exact
// lengths, identity, and all metadata are checked before channel state changes.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

class RelayBundleChannel
{
public:
	using Bytes = std::vector<std::uint8_t>;

	struct Options
	{
		std::uint64_t gameId = 0;
		std::uint64_t epoch = 0;
		std::size_t packetBytes = 1200;
		std::size_t outerEnvelopeBytes = 5;
		std::size_t maxMessageBytes = 8u * 1024u * 1024u;
		std::size_t maxQueuedBytes = 16u * 1024u * 1024u;
		std::size_t maxBufferedBytes = 16u * 1024u * 1024u;
		std::size_t maxPendingMessages = 32;
		std::size_t maxHistoryBytes = 16u * 1024u * 1024u;
		std::uint64_t retryMs = 100;
	};

	struct Message
	{
		std::uint64_t sequence = 0;
		Bytes bytes;
	};

	struct Counters
	{
		std::uint64_t dataPacketsSent = 0;
		std::uint64_t dataPacketsReceived = 0;
		std::uint64_t dataBytesSent = 0;
		std::uint64_t dataBytesReceived = 0;
		std::uint64_t ackPacketsSent = 0;
		std::uint64_t ackPacketsReceived = 0;
		std::uint64_t ackBytesSent = 0;
		std::uint64_t ackBytesReceived = 0;
		std::uint64_t retries = 0;
		std::size_t heldBytes = 0;
	};

	static constexpr std::uint8_t kVersion = 1;
	static constexpr std::size_t kWireHeaderBytes = 46;
	static constexpr std::size_t kWireCrcOffset = 42;
	static constexpr std::size_t kDefaultOuterEnvelopeBytes = 5;

	explicit RelayBundleChannel(Options options, std::string *error = nullptr);

	bool valid() const { return m_configError.empty(); }
	const std::string &configError() const { return m_configError; }
	const Options &options() const { return m_options; }

	// The caller retains bytes when this returns false. A successful call
	// admits the complete logical message and never silently drops it.
	bool queue(const Bytes &bytes, std::string &error);

	// Returns at most maxDatagrams channel packets. nowMs is injected by the
	// caller; time regression is rejected and does not mutate channel state.
	std::vector<Bytes> poll(std::uint64_t nowMs, std::size_t maxDatagrams, std::string *error = nullptr);

	// A valid data packet produces one immediate ACK in ackOutput. ACK input
	// packets leave it empty. On failure, ackOutput and channel state are
	// unchanged.
	bool receive(const Bytes &packet, std::uint64_t nowMs, Bytes &ackOutput, std::string &error);

	// Draining is explicit: delivered bytes remain accounted against the
	// receive bound until this call.
	std::vector<Message> takeDelivered();

	Counters counters() const;
	std::size_t queuedBytes() const { return m_queuedBytes; }
	std::size_t bufferedBytes() const { return m_bufferedBytes; }
	std::size_t historyBytes() const { return m_historyBytes; }
	std::size_t pendingMessages() const { return m_pending.size(); }
	std::size_t bufferedMessages() const { return m_incoming.size(); }
	std::size_t senderHistoryMessages() const { return m_senderHistory.size(); }
	std::size_t receiverHistoryMessages() const { return m_receiverHistory.size(); }

private:
	struct PendingMessage
	{
		std::uint64_t sequence = 0;
		std::uint32_t fragmentCount = 0;
		std::size_t accounted = 0;
		Bytes bytes;
		std::vector<std::uint8_t> sent;
		std::vector<std::uint8_t> acknowledged;
		std::vector<std::uint64_t> sentAt;
		std::size_t unacknowledgedFragments = 0;
		bool complete = false;
	};

	struct IncomingMessage
	{
		std::uint64_t sequence = 0;
		std::uint32_t fragmentCount = 0;
		std::uint32_t totalBytes = 0;
		std::size_t accounted = 0;
		Bytes bytes;
		std::vector<std::uint8_t> received;
		std::vector<std::array<std::uint8_t, 32>> fingerprints;
		std::uint32_t receivedCount = 0;
		bool delivered = false;
	};

	struct SenderHistory
	{
		std::uint64_t sequence = 0;
		std::uint32_t fragmentCount = 0;
		std::uint32_t totalBytes = 0;
		std::size_t accounted = 0;
	};

	struct ReceiverHistory
	{
		std::uint64_t sequence = 0;
		std::uint32_t fragmentCount = 0;
		std::uint32_t totalBytes = 0;
		std::size_t accounted = 0;
		std::vector<std::array<std::uint8_t, 32>> fingerprints;
	};

	struct Header
	{
		std::uint8_t kind = 0;
		std::uint64_t gameId = 0;
		std::uint64_t epoch = 0;
		std::uint64_t sequence = 0;
		std::uint32_t fragmentIndex = 0;
		std::uint32_t fragmentCount = 0;
		std::uint32_t totalBytes = 0;
		std::uint16_t fragmentBytes = 0;
	};

	static constexpr std::size_t kMessageMetadataBytes = 128;
	static constexpr std::size_t kFragmentMetadataBytes = 64;
	static constexpr std::size_t kHistoryMessageMetadataBytes = 128;
	static constexpr std::size_t kFingerprintBytes = 32;

	bool calculateAccounting(std::uint32_t totalBytes, std::uint32_t fragmentCount, std::size_t &out) const;
	bool calculateFragmentCount(std::uint32_t totalBytes, std::uint32_t &out) const;
	bool calculateHistoryAccounting(std::uint32_t fragmentCount, std::size_t &out) const;
	bool validateFragment(const Header &header, std::size_t packetBytes, std::string &error) const;
	bool decode(const Bytes &packet, Header &header, std::string &error) const;
	bool timeIsValid(std::uint64_t nowMs, std::string &error) const;
	void commitTime(std::uint64_t nowMs);
	Bytes encode(const Header &header, const std::uint8_t *payload, std::size_t payloadBytes) const;
	Bytes encodeAck(const Header &header) const;
	void markDelivered();
	void retirePending();
	void addSenderHistory(const PendingMessage &message);
	void addReceiverHistory(const IncomingMessage &message);
	bool receiveRetiredData(const Header &header, const Bytes &packet, std::uint64_t nowMs, Bytes &ackOutput, std::string &error);
	bool receiveAck(const Header &header, std::uint64_t nowMs, Bytes &ackOutput, std::string &error);
	bool receiveData(const Header &header, const Bytes &packet, std::uint64_t nowMs, Bytes &ackOutput, std::string &error);

	Options m_options;
	std::string m_configError;
	std::size_t m_channelPacketBytes = 0;
	std::size_t m_fragmentPayloadBytes = 0;
	std::size_t m_expectedReservation = 0;
	std::map<std::uint64_t, PendingMessage> m_pending;
	std::map<std::uint64_t, IncomingMessage> m_incoming;
	std::map<std::uint64_t, SenderHistory> m_senderHistory;
	std::map<std::uint64_t, ReceiverHistory> m_receiverHistory;
	std::deque<std::uint64_t> m_ready;
	std::size_t m_queuedBytes = 0;
	std::size_t m_bufferedBytes = 0;
	std::size_t m_historyBytes = 0;
	std::size_t m_pendingFragmentSlots = 0;
	std::uint64_t m_nextSendSequence = 1;
	std::uint64_t m_nextReceiveSequence = 1;
	std::uint64_t m_pollCursorSequence = 1;
	std::uint32_t m_pollCursorFragment = 0;
	bool m_sendExhausted = false;
	bool m_receiveExhausted = false;
	bool m_haveTime = false;
	std::uint64_t m_lastTimeMs = 0;
	Counters m_counters;
};
