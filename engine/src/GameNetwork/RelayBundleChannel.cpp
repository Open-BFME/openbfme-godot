// OpenBFME. GPL-3.0.
// See GameNetwork/RelayBundleChannel.h.

#include "GameNetwork/RelayBundleChannel.h"

#include "Common/Crc32.h"
#include "Common/Sha256.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr std::uint8_t kMagic0 = 'N';
constexpr std::uint8_t kMagic1 = 'S';
constexpr std::uint8_t kData = 1;
constexpr std::uint8_t kAck = 2;

void put16(std::uint8_t *p, std::uint16_t value)
{
	p[0] = (std::uint8_t)value;
	p[1] = (std::uint8_t)(value >> 8);
}

void put32(std::uint8_t *p, std::uint32_t value)
{
	for (int i = 0; i != 4; ++i)
	{
		p[i] = (std::uint8_t)(value >> (8 * i));
	}
}

void put64(std::uint8_t *p, std::uint64_t value)
{
	for (int i = 0; i != 8; ++i)
	{
		p[i] = (std::uint8_t)(value >> (8 * i));
	}
}

std::uint16_t get16(const std::uint8_t *p)
{
	return (std::uint16_t)p[0] | (std::uint16_t)((std::uint16_t)p[1] << 8);
}

std::uint32_t get32(const std::uint8_t *p)
{
	return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

std::uint64_t get64(const std::uint8_t *p)
{
	std::uint64_t value = 0;
	for (int i = 7; i >= 0; --i)
	{
		value = (value << 8) | p[i];
	}
	return value;
}

bool addSize(std::size_t left, std::size_t right, std::size_t &out)
{
	if (right > std::numeric_limits<std::size_t>::max() - left)
	{
		return false;
	}
	out = left + right;
	return true;
}

bool multiplySize(std::size_t left, std::size_t right, std::size_t &out)
{
	if (right && left > std::numeric_limits<std::size_t>::max() / right)
	{
		return false;
	}
	out = left * right;
	return true;
}

std::uint32_t packetCrc(const std::uint8_t *packet, std::size_t packetBytes)
{
	const std::uint32_t headerCrc = crc32Bytes(packet, RelayBundleChannel::kWireCrcOffset);
	return crc32Bytes(packet + RelayBundleChannel::kWireHeaderBytes, packetBytes - RelayBundleChannel::kWireHeaderBytes, headerCrc);
}

std::array<std::uint8_t, 32> fragmentFingerprint(const std::uint8_t *payload, std::size_t payloadBytes)
{
	return Sha256::hash(payload, payloadBytes);
}
} // namespace

RelayBundleChannel::RelayBundleChannel(Options options, std::string *error) : m_options(options)
{
	auto fail = [&](const char *why) {
		m_configError = why;
		if (error)
		{
			*error = m_configError;
		}
	};
	if (!m_options.gameId || !m_options.epoch)
	{
		fail("relay bundle game id and epoch must be nonzero");
		return;
	}
	if (!m_options.packetBytes || m_options.outerEnvelopeBytes >= m_options.packetBytes || m_options.packetBytes > 1200)
	{
		fail("relay bundle total packet size must be between its envelope and 1200 bytes");
		return;
	}
	m_channelPacketBytes = m_options.packetBytes - m_options.outerEnvelopeBytes;
	if (m_channelPacketBytes <= kWireHeaderBytes || m_channelPacketBytes > kWireHeaderBytes + std::numeric_limits<std::uint16_t>::max())
	{
		fail("relay bundle packet size cannot represent a fragment payload");
		return;
	}
	m_fragmentPayloadBytes = m_channelPacketBytes - kWireHeaderBytes;
	if (!m_options.maxMessageBytes || m_options.maxMessageBytes > std::numeric_limits<std::uint32_t>::max())
	{
		fail("relay bundle maximum message size is invalid");
		return;
	}
	if (!m_options.maxQueuedBytes || !m_options.maxBufferedBytes || !m_options.maxPendingMessages || !m_options.maxHistoryBytes || !m_options.retryMs)
	{
		fail("relay bundle bounds and retry interval must be nonzero");
		return;
	}
	std::uint32_t maxFragments = 0;
	if (!calculateFragmentCount((std::uint32_t)m_options.maxMessageBytes, maxFragments) ||
		!calculateAccounting((std::uint32_t)m_options.maxMessageBytes, maxFragments, m_expectedReservation))
	{
		fail("relay bundle maximum message accounting overflows");
		return;
	}
	std::size_t receiverHistoryMax = 0;
	if (!calculateHistoryAccounting(maxFragments, receiverHistoryMax))
	{
		fail("relay bundle history accounting overflows");
		return;
	}
	std::size_t historyPerWindow = 0;
	if (!addSize(receiverHistoryMax, kHistoryMessageMetadataBytes, historyPerWindow) ||
		!multiplySize(historyPerWindow, m_options.maxPendingMessages, historyPerWindow))
	{
		fail("relay bundle history window accounting overflows");
		return;
	}
	if (m_options.maxHistoryBytes < historyPerWindow)
	{
		fail("relay bundle history bound cannot retain its configured sequence window");
		return;
	}
	std::size_t heldBounds = 0;
	if (!addSize(m_options.maxQueuedBytes, m_options.maxBufferedBytes, heldBounds) ||
		!addSize(heldBounds, m_options.maxHistoryBytes, heldBounds))
	{
		fail("relay bundle combined held-byte bound overflows");
		return;
	}
	if (maxFragments && m_options.maxPendingMessages > std::numeric_limits<std::size_t>::max() / maxFragments)
	{
		fail("relay bundle pending fragment window overflows");
		return;
	}
	std::size_t minimumAccounting = 0;
	if (!calculateAccounting(0, 1, minimumAccounting) || m_options.maxQueuedBytes < minimumAccounting)
	{
		fail("relay bundle send bound cannot admit a message");
		return;
	}
	if (m_options.maxBufferedBytes < m_expectedReservation)
	{
		fail("relay bundle receive bound cannot reserve one maximum expected message");
		return;
	}
	if (error)
	{
		error->clear();
	}
}

bool RelayBundleChannel::calculateFragmentCount(std::uint32_t totalBytes, std::uint32_t &out) const
{
	if (!m_fragmentPayloadBytes)
	{
		return false;
	}
	const std::uint64_t total = totalBytes;
	const std::uint64_t capacity = m_fragmentPayloadBytes;
	const std::uint64_t count = total ? (total + capacity - 1) / capacity : 1;
	if (!count || count > std::numeric_limits<std::uint32_t>::max())
	{
		return false;
	}
	out = (std::uint32_t)count;
	return true;
}

bool RelayBundleChannel::calculateAccounting(std::uint32_t totalBytes, std::uint32_t fragmentCount, std::size_t &out) const
{
	if (!fragmentCount || totalBytes > m_options.maxMessageBytes)
	{
		return false;
	}
	std::size_t result = 0;
	if (!addSize(kMessageMetadataBytes, totalBytes, result))
	{
		return false;
	}
	std::size_t fragmentMeta = 0;
	if (!multiplySize(kFragmentMetadataBytes, fragmentCount, fragmentMeta) || !addSize(result, fragmentMeta, result))
	{
		return false;
	}
	out = result;
	return true;
}

bool RelayBundleChannel::calculateHistoryAccounting(std::uint32_t fragmentCount, std::size_t &out) const
{
	std::size_t fingerprints = 0;
	if (!multiplySize(kFingerprintBytes, fragmentCount, fingerprints) ||
		!addSize(kHistoryMessageMetadataBytes, fingerprints, out))
	{
		return false;
	}
	return true;
}

bool RelayBundleChannel::validateFragment(const Header &header, std::size_t packetBytes, std::string &error) const
{
	if (!header.sequence)
	{
		error = "relay bundle sequence is zero";
		return false;
	}
	if (header.totalBytes > m_options.maxMessageBytes)
	{
		error = "relay bundle message exceeds its maximum size";
		return false;
	}
	if (!header.fragmentCount || header.fragmentIndex >= header.fragmentCount)
	{
		error = "relay bundle fragment index or count is invalid";
		return false;
	}
	std::uint32_t expectedCount = 0;
	if (!calculateFragmentCount(header.totalBytes, expectedCount) || header.fragmentCount != expectedCount)
	{
		error = "relay bundle fragment count does not match total size";
		return false;
	}
	const std::uint64_t offset = (std::uint64_t)header.fragmentIndex * m_fragmentPayloadBytes;
	if (offset > header.totalBytes)
	{
		error = "relay bundle fragment offset exceeds total size";
		return false;
	}
	const std::uint32_t remaining = header.totalBytes - (std::uint32_t)offset;
	if (header.kind == kAck && header.fragmentBytes != 0)
	{
		error = "relay bundle ACK carries a payload";
		return false;
	}
	if (header.kind == kData)
	{
		const std::uint16_t expectedBytes = (std::uint16_t)std::min<std::uint64_t>(m_fragmentPayloadBytes, remaining);
		if (header.fragmentBytes != expectedBytes)
		{
			error = "relay bundle fragment size does not match its index";
			return false;
		}
	}
	const std::size_t expectedPacketBytes = kWireHeaderBytes + (header.kind == kAck ? 0 : header.fragmentBytes);
	if (packetBytes != expectedPacketBytes)
	{
		error = "relay bundle packet has trailing or missing bytes";
		return false;
	}
	return true;
}

bool RelayBundleChannel::decode(const Bytes &packet, Header &header, std::string &error) const
{
	if (packet.size() > m_channelPacketBytes || packet.size() < kWireHeaderBytes)
	{
		error = "relay bundle packet size is outside the channel bound";
		return false;
	}
	if (packet[0] != kMagic0 || packet[1] != kMagic1)
	{
		error = "relay bundle packet magic is invalid";
		return false;
	}
	if (packet[2] != kVersion)
	{
		error = "relay bundle packet version is unsupported";
		return false;
	}
	if (packet[3] != kData && packet[3] != kAck)
	{
		error = "relay bundle packet kind is invalid";
		return false;
	}
	if (packetCrc(packet.data(), packet.size()) != get32(packet.data() + kWireCrcOffset))
	{
		error = "relay bundle packet CRC is invalid";
		return false;
	}
	header.kind = packet[3];
	header.gameId = get64(packet.data() + 4);
	header.epoch = get64(packet.data() + 12);
	header.sequence = get64(packet.data() + 20);
	header.fragmentIndex = get32(packet.data() + 28);
	header.fragmentCount = get32(packet.data() + 32);
	header.totalBytes = get32(packet.data() + 36);
	header.fragmentBytes = get16(packet.data() + 40);
	return validateFragment(header, packet.size(), error);
}

bool RelayBundleChannel::timeIsValid(std::uint64_t nowMs, std::string &error) const
{
	if (m_haveTime && nowMs < m_lastTimeMs)
	{
		error = "relay bundle clock moved backwards";
		return false;
	}
	return true;
}

void RelayBundleChannel::commitTime(std::uint64_t nowMs)
{
	m_haveTime = true;
	m_lastTimeMs = nowMs;
}

RelayBundleChannel::Bytes RelayBundleChannel::encode(const Header &header, const std::uint8_t *payload, std::size_t payloadBytes) const
{
	Bytes out(kWireHeaderBytes + payloadBytes, 0);
	out[0] = kMagic0;
	out[1] = kMagic1;
	out[2] = kVersion;
	out[3] = header.kind;
	put64(out.data() + 4, header.gameId);
	put64(out.data() + 12, header.epoch);
	put64(out.data() + 20, header.sequence);
	put32(out.data() + 28, header.fragmentIndex);
	put32(out.data() + 32, header.fragmentCount);
	put32(out.data() + 36, header.totalBytes);
	put16(out.data() + 40, header.fragmentBytes);
	if (payloadBytes)
	{
		std::copy(payload, payload + payloadBytes, out.begin() + kWireHeaderBytes);
	}
	put32(out.data() + kWireCrcOffset, packetCrc(out.data(), out.size()));
	return out;
}

RelayBundleChannel::Bytes RelayBundleChannel::encodeAck(const Header &header) const
{
	Header ack = header;
	ack.kind = kAck;
	ack.fragmentBytes = 0;
	return encode(ack, nullptr, 0);
}

bool RelayBundleChannel::queue(const Bytes &bytes, std::string &error)
{
	if (!valid())
	{
		error = m_configError;
		return false;
	}
	if (m_sendExhausted)
	{
		error = "relay bundle send sequence exhausted";
		return false;
	}
	if (bytes.size() > m_options.maxMessageBytes || bytes.size() > std::numeric_limits<std::uint32_t>::max())
	{
		error = "relay bundle message exceeds its maximum size";
		return false;
	}
	if (m_pending.size() >= m_options.maxPendingMessages)
	{
		error = "relay bundle pending sequence window is full";
		return false;
	}
	std::uint32_t fragmentCount = 0;
	if (!calculateFragmentCount((std::uint32_t)bytes.size(), fragmentCount))
	{
		error = "relay bundle fragment count is exhausted";
		return false;
	}
	std::size_t accounted = 0;
	if (!calculateAccounting((std::uint32_t)bytes.size(), fragmentCount, accounted) ||
		accounted > m_options.maxQueuedBytes - m_queuedBytes)
	{
		error = "relay bundle send queue is full";
		return false;
	}
	PendingMessage message;
	message.sequence = m_nextSendSequence;
	message.fragmentCount = fragmentCount;
	message.accounted = accounted;
	message.bytes = bytes;
	message.sent.assign(fragmentCount, 0);
	message.acknowledged.assign(fragmentCount, 0);
	message.sentAt.assign(fragmentCount, 0);
	message.unacknowledgedFragments = fragmentCount;
	m_pending.emplace(message.sequence, std::move(message));
	m_queuedBytes += accounted;
	m_pendingFragmentSlots += fragmentCount;
	if (m_nextSendSequence == std::numeric_limits<std::uint64_t>::max())
	{
		m_sendExhausted = true;
	}
	else
	{
		++m_nextSendSequence;
	}
	error.clear();
	return true;
}

std::vector<RelayBundleChannel::Bytes> RelayBundleChannel::poll(std::uint64_t nowMs, std::size_t maxDatagrams, std::string *error)
{
	std::vector<Bytes> out;
	std::string localError;
	if (!valid())
	{
		localError = m_configError;
	}
	else if (!timeIsValid(nowMs, localError))
	{
	}
	else
	{
		commitTime(nowMs);
		const std::size_t inspectionBudget = std::min(maxDatagrams, m_pendingFragmentSlots);
		for (std::size_t inspected = 0; inspected < inspectionBudget && !m_pending.empty(); ++inspected)
		{
			auto it = m_pending.lower_bound(m_pollCursorSequence);
			if (it == m_pending.end())
			{
				it = m_pending.begin();
				m_pollCursorSequence = it->first;
				m_pollCursorFragment = 0;
			}
			else if (it->first != m_pollCursorSequence)
			{
				m_pollCursorSequence = it->first;
				m_pollCursorFragment = 0;
			}
			PendingMessage &message = it->second;
			if (m_pollCursorFragment >= message.fragmentCount)
			{
				m_pollCursorSequence = message.sequence == std::numeric_limits<std::uint64_t>::max() ? m_pending.begin()->first : message.sequence + 1;
				m_pollCursorFragment = 0;
				continue;
			}
			const std::uint32_t index = m_pollCursorFragment;
			if (index + 1 < message.fragmentCount)
			{
				m_pollCursorSequence = message.sequence;
				m_pollCursorFragment = index + 1;
			}
			else
			{
				m_pollCursorSequence = message.sequence == std::numeric_limits<std::uint64_t>::max() ? m_pending.begin()->first : message.sequence + 1;
				m_pollCursorFragment = 0;
			}
			if (message.complete || message.acknowledged[index])
			{
				continue;
			}
			const bool retry = message.sent[index] && nowMs - message.sentAt[index] >= m_options.retryMs;
			if (!message.sent[index] || retry)
			{
				const std::size_t offset = (std::size_t)index * m_fragmentPayloadBytes;
				const std::size_t fragmentBytes = std::min(m_fragmentPayloadBytes, message.bytes.size() - offset);
				Header header;
				header.kind = kData;
				header.gameId = m_options.gameId;
				header.epoch = m_options.epoch;
				header.sequence = message.sequence;
				header.fragmentIndex = index;
				header.fragmentCount = message.fragmentCount;
				header.totalBytes = (std::uint32_t)message.bytes.size();
				header.fragmentBytes = (std::uint16_t)fragmentBytes;
				const std::uint8_t *payload = message.bytes.empty() ? nullptr : message.bytes.data() + offset;
				out.push_back(encode(header, payload, fragmentBytes));
				message.sent[index] = 1;
				message.sentAt[index] = nowMs;
				++m_counters.dataPacketsSent;
				m_counters.dataBytesSent += out.back().size();
				if (retry)
				{
					++m_counters.retries;
				}
			}
			if (out.size() >= maxDatagrams)
			{
				break;
			}
		}
	}
	if (error)
	{
		*error = localError;
	}
	return out;
}

void RelayBundleChannel::addSenderHistory(const PendingMessage &message)
{
	while (m_senderHistory.size() >= m_options.maxPendingMessages)
	{
		m_historyBytes -= m_senderHistory.begin()->second.accounted;
		m_senderHistory.erase(m_senderHistory.begin());
	}
	SenderHistory history;
	history.sequence = message.sequence;
	history.fragmentCount = message.fragmentCount;
	history.totalBytes = (std::uint32_t)message.bytes.size();
	history.accounted = kHistoryMessageMetadataBytes;
	m_senderHistory.emplace(history.sequence, history);
	m_historyBytes += history.accounted;
}

void RelayBundleChannel::retirePending()
{
	while (!m_pending.empty() && m_pending.begin()->second.complete)
	{
		const PendingMessage &message = m_pending.begin()->second;
		addSenderHistory(message);
		m_queuedBytes -= message.accounted;
		m_pendingFragmentSlots -= message.fragmentCount;
		m_pending.erase(m_pending.begin());
	}
}

bool RelayBundleChannel::receive(const Bytes &packet, std::uint64_t nowMs, Bytes &ackOutput, std::string &error)
{
	if (!valid())
	{
		error = m_configError;
		return false;
	}
	if (!timeIsValid(nowMs, error))
	{
		return false;
	}
	Header header;
	if (!decode(packet, header, error))
	{
		return false;
	}
	if (header.gameId != m_options.gameId)
	{
		error = "relay bundle game identity is wrong";
		return false;
	}
	if (header.epoch != m_options.epoch)
	{
		error = "relay bundle epoch identity is wrong";
		return false;
	}
	if (header.kind == kAck)
	{
		return receiveAck(header, nowMs, ackOutput, error);
	}
	return receiveData(header, packet, nowMs, ackOutput, error);
}

bool RelayBundleChannel::receiveAck(const Header &header, std::uint64_t nowMs, Bytes &ackOutput, std::string &error)
{
	const auto it = m_pending.find(header.sequence);
	if (it != m_pending.end())
	{
		PendingMessage &message = it->second;
		if (header.totalBytes != message.bytes.size() || header.fragmentCount != message.fragmentCount)
		{
			error = "relay bundle ACK metadata does not match the sent message";
			return false;
		}
		if (header.fragmentIndex >= message.fragmentCount)
		{
			error = "relay bundle ACK fragment is outside the sent message";
			return false;
		}
		if (!message.sent[header.fragmentIndex])
		{
			error = "relay bundle ACK names a fragment that has not been sent";
			return false;
		}
		ackOutput.clear();
		commitTime(nowMs);
		++m_counters.ackPacketsReceived;
		m_counters.ackBytesReceived += kWireHeaderBytes;
		if (!message.acknowledged[header.fragmentIndex])
		{
			message.acknowledged[header.fragmentIndex] = 1;
			--message.unacknowledgedFragments;
			if (!message.unacknowledgedFragments)
			{
				message.complete = true;
			}
		}
		retirePending();
		error.clear();
		return true;
	}
	const auto retired = m_senderHistory.find(header.sequence);
	if (retired != m_senderHistory.end())
	{
		const SenderHistory &history = retired->second;
		if (header.totalBytes != history.totalBytes || header.fragmentCount != history.fragmentCount)
		{
			error = "relay bundle retired ACK metadata differs";
			return false;
		}
		if (header.fragmentIndex >= history.fragmentCount)
		{
			error = "relay bundle retired ACK fragment is invalid";
			return false;
		}
		ackOutput.clear();
		commitTime(nowMs);
		++m_counters.ackPacketsReceived;
		m_counters.ackBytesReceived += kWireHeaderBytes;
		error.clear();
		return true;
	}
	error = "relay bundle ACK sequence is outside the retained sender history";
	return false;
}

bool RelayBundleChannel::receiveRetiredData(const Header &header, const Bytes &packet, std::uint64_t nowMs, Bytes &ackOutput, std::string &error)
{
	const auto retired = m_receiverHistory.find(header.sequence);
	if (retired == m_receiverHistory.end())
	{
		error = "relay bundle data sequence is outside the retained receiver history";
		return false;
	}
	const ReceiverHistory &history = retired->second;
	if (header.totalBytes != history.totalBytes || header.fragmentCount != history.fragmentCount ||
		header.fragmentIndex >= history.fragmentCount)
	{
		error = "relay bundle retired data metadata differs";
		return false;
	}
	const auto fingerprint = fragmentFingerprint(packet.data() + kWireHeaderBytes, header.fragmentBytes);
	if (!std::equal(fingerprint.begin(), fingerprint.end(), history.fingerprints[header.fragmentIndex].begin()))
	{
		error = "relay bundle retired data bytes differ";
		return false;
	}
	ackOutput = encodeAck(header);
	commitTime(nowMs);
	++m_counters.dataPacketsReceived;
	m_counters.dataBytesReceived += packet.size();
	++m_counters.ackPacketsSent;
	m_counters.ackBytesSent += ackOutput.size();
	error.clear();
	return true;
}

bool RelayBundleChannel::receiveData(const Header &header, const Bytes &packet, std::uint64_t nowMs, Bytes &ackOutput, std::string &error)
{
	auto existing = m_incoming.find(header.sequence);
	if (existing != m_incoming.end())
	{
		IncomingMessage &message = existing->second;
		if (message.totalBytes != header.totalBytes || message.fragmentCount != header.fragmentCount)
		{
			error = "relay bundle duplicate metadata differs while retained";
			return false;
		}
		const std::size_t offset = (std::size_t)header.fragmentIndex * m_fragmentPayloadBytes;
		const std::uint8_t *incoming = packet.data() + kWireHeaderBytes;
		if (message.received[header.fragmentIndex])
		{
			if (!std::equal(incoming, incoming + header.fragmentBytes, message.bytes.begin() + offset))
			{
				error = "relay bundle duplicate fragment bytes differ while retained";
				return false;
			}
		}
		else
		{
			std::copy(incoming, incoming + header.fragmentBytes, message.bytes.begin() + offset);
			message.fingerprints[header.fragmentIndex] = fragmentFingerprint(incoming, header.fragmentBytes);
			message.received[header.fragmentIndex] = 1;
			++message.receivedCount;
		}
	}
	else if (header.sequence < m_nextReceiveSequence || m_receiveExhausted)
	{
		return receiveRetiredData(header, packet, nowMs, ackOutput, error);
	}
	else
	{
		if (header.sequence > m_nextReceiveSequence)
		{
			const std::uint64_t distance = header.sequence - m_nextReceiveSequence;
			if (distance >= m_options.maxPendingMessages)
			{
				error = "relay bundle data sequence is outside the receive window";
				return false;
			}
			if (m_incoming.size() + 1 >= m_options.maxPendingMessages)
			{
				error = "relay bundle receive window reserves a message slot for the expected sequence";
				return false;
			}
		}
		else if (m_incoming.size() >= m_options.maxPendingMessages)
		{
			error = "relay bundle receive pending message bound is full";
			return false;
		}
		std::size_t accounted = 0;
		if (!calculateAccounting(header.totalBytes, header.fragmentCount, accounted))
		{
			error = "relay bundle receive accounting overflows";
			return false;
		}
		if (header.sequence > m_nextReceiveSequence)
		{
			const std::size_t availableAfterExpected = m_options.maxBufferedBytes - m_expectedReservation;
			if (m_bufferedBytes > availableAfterExpected || accounted > availableAfterExpected - m_bufferedBytes)
			{
				error = "relay bundle receive bound reserves the next expected message";
				return false;
			}
		}
		else if (accounted > m_options.maxBufferedBytes - m_bufferedBytes)
		{
			error = "relay bundle receive buffer is full";
			return false;
		}
		IncomingMessage message;
		message.sequence = header.sequence;
		message.fragmentCount = header.fragmentCount;
		message.totalBytes = header.totalBytes;
		message.accounted = accounted;
		message.bytes.resize(header.totalBytes);
		message.received.assign(header.fragmentCount, 0);
		message.fingerprints.resize(header.fragmentCount);
		const std::size_t offset = (std::size_t)header.fragmentIndex * m_fragmentPayloadBytes;
		std::copy(packet.begin() + kWireHeaderBytes, packet.end(), message.bytes.begin() + offset);
		message.fingerprints[header.fragmentIndex] = fragmentFingerprint(packet.data() + kWireHeaderBytes, header.fragmentBytes);
		message.received[header.fragmentIndex] = 1;
		message.receivedCount = 1;
		m_bufferedBytes += accounted;
		m_incoming.emplace(header.sequence, std::move(message));
	}
	auto inserted = m_incoming.find(header.sequence);
	if (inserted != m_incoming.end() && inserted->second.receivedCount == inserted->second.fragmentCount)
	{
		markDelivered();
	}
	ackOutput = encodeAck(header);
	commitTime(nowMs);
	++m_counters.dataPacketsReceived;
	m_counters.dataBytesReceived += packet.size();
	++m_counters.ackPacketsSent;
	m_counters.ackBytesSent += ackOutput.size();
	error.clear();
	return true;
}

void RelayBundleChannel::markDelivered()
{
	while (true)
	{
		const auto it = m_incoming.find(m_nextReceiveSequence);
		if (it == m_incoming.end() || it->second.receivedCount != it->second.fragmentCount || it->second.delivered)
		{
			return;
		}
		it->second.delivered = true;
		m_ready.push_back(it->first);
		if (m_nextReceiveSequence == std::numeric_limits<std::uint64_t>::max())
		{
			m_receiveExhausted = true;
			return;
		}
		++m_nextReceiveSequence;
	}
}

void RelayBundleChannel::addReceiverHistory(const IncomingMessage &message)
{
	while (m_receiverHistory.size() >= m_options.maxPendingMessages)
	{
		m_historyBytes -= m_receiverHistory.begin()->second.accounted;
		m_receiverHistory.erase(m_receiverHistory.begin());
	}
	ReceiverHistory history;
	history.sequence = message.sequence;
	history.fragmentCount = message.fragmentCount;
	history.totalBytes = message.totalBytes;
	calculateHistoryAccounting(message.fragmentCount, history.accounted);
	history.fingerprints = message.fingerprints;
	m_receiverHistory.emplace(history.sequence, std::move(history));
	m_historyBytes += m_receiverHistory.find(message.sequence)->second.accounted;
}

std::vector<RelayBundleChannel::Message> RelayBundleChannel::takeDelivered()
{
	std::vector<Message> result;
	result.reserve(m_ready.size());
	while (!m_ready.empty())
	{
		const std::uint64_t sequence = m_ready.front();
		m_ready.pop_front();
		const auto it = m_incoming.find(sequence);
		if (it == m_incoming.end())
		{
			continue;
		}
		addReceiverHistory(it->second);
		Message message;
		message.sequence = sequence;
		message.bytes = std::move(it->second.bytes);
		m_bufferedBytes -= it->second.accounted;
		m_incoming.erase(it);
		result.push_back(std::move(message));
	}
	return result;
}

RelayBundleChannel::Counters RelayBundleChannel::counters() const
{
	Counters result = m_counters;
	result.heldBytes = m_queuedBytes + m_bufferedBytes + m_historyBytes;
	return result;
}
