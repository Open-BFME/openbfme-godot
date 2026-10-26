// OpenBFME unit tests.
#include "doctest.h"

#include "Common/Crc32.h"
#include "GameNetwork/RelayBundleChannel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
RelayBundleChannel::Options options()
{
	RelayBundleChannel::Options result;
	result.gameId = 0x0102030405060708ull;
	result.epoch = 0x1112131415161718ull;
	return result;
}

void put16(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint16_t value)
{
	bytes[at] = (std::uint8_t)value;
	bytes[at + 1] = (std::uint8_t)(value >> 8);
}

void put32(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t value)
{
	for (int i = 0; i != 4; ++i)
	{
		bytes[at + (std::size_t)i] = (std::uint8_t)(value >> (8 * i));
	}
}

void put64(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint64_t value)
{
	for (int i = 0; i != 8; ++i)
	{
		bytes[at + (std::size_t)i] = (std::uint8_t)(value >> (8 * i));
	}
}

std::uint32_t read32(const std::vector<std::uint8_t> &bytes, std::size_t at)
{
	return (std::uint32_t)bytes[at] | ((std::uint32_t)bytes[at + 1] << 8) |
		((std::uint32_t)bytes[at + 2] << 16) | ((std::uint32_t)bytes[at + 3] << 24);
}

void fixCrc(std::vector<std::uint8_t> &bytes)
{
	const std::uint32_t headerCrc = crc32Bytes(bytes.data(), RelayBundleChannel::kWireCrcOffset);
	const std::uint32_t packetCrc = crc32Bytes(bytes.data() + RelayBundleChannel::kWireHeaderBytes,
		bytes.size() - RelayBundleChannel::kWireHeaderBytes, headerCrc);
	put32(bytes, RelayBundleChannel::kWireCrcOffset, packetCrc);
}

std::vector<std::uint8_t> asAck(std::vector<std::uint8_t> data)
{
	data.resize(RelayBundleChannel::kWireHeaderBytes);
	data[3] = 2;
	put16(data, 40, 0);
	fixCrc(data);
	return data;
}

std::vector<std::uint8_t> body(std::size_t size, std::uint8_t seed)
{
	std::vector<std::uint8_t> result(size);
	for (std::size_t i = 0; i < result.size(); ++i)
	{
		result[i] = (std::uint8_t)(seed + i * 13u);
	}
	return result;
}
} // namespace

TEST_CASE("NET-SYNC bundle: fragments are bounded, selectively acknowledged, retried and delivered once")
{
	RelayBundleChannel::Options senderOptions = options();
	RelayBundleChannel sender(senderOptions);
	RelayBundleChannel receiver(senderOptions);
	std::string error;
	const std::vector<std::uint8_t> message = body(3000, 7);
	REQUIRE(sender.queue(message, error));
	const auto packets = sender.poll(0, 100, &error);
	REQUIRE(packets.size() == 3);
	CHECK(packets[0].size() == 1195);
	CHECK(packets[1].size() == 1195);
	CHECK(packets[2].size() == 748);

	std::vector<std::vector<std::uint8_t>> acknowledgements;
	std::vector<std::uint8_t> ack;
	REQUIRE(receiver.receive(packets[2], 0, ack, error));
	REQUIRE(!ack.empty());
	acknowledgements.push_back(ack);
	REQUIRE(receiver.receive(packets[0], 0, ack, error));
	acknowledgements.push_back(ack);
	CHECK(receiver.takeDelivered().empty());
	REQUIRE(receiver.receive(packets[2], 1, ack, error));
	acknowledgements.push_back(ack);
	CHECK(receiver.takeDelivered().empty());

	REQUIRE(sender.receive(acknowledgements[1], 1, ack, error));
	CHECK(sender.poll(99, 100, &error).empty());
	const auto retry = sender.poll(100, 100, &error);
	REQUIRE(retry.size() == 2);
	REQUIRE(receiver.receive(retry[0], 100, ack, error));
	acknowledgements.push_back(ack);
	REQUIRE(receiver.receive(retry[1], 100, ack, error));
	acknowledgements.push_back(ack);
	REQUIRE(sender.receive(acknowledgements[2], 100, ack, error));
	REQUIRE(sender.receive(acknowledgements[3], 100, ack, error));
	CHECK(sender.poll(100, 100, &error).empty());
	const auto delivered = receiver.takeDelivered();
	REQUIRE(delivered.size() == 1);
	CHECK(delivered[0].sequence == 1);
	CHECK(delivered[0].bytes == message);
	CHECK(receiver.takeDelivered().empty());
	CHECK(receiver.counters().retries == 0);
	CHECK(sender.counters().retries == 2);
	CHECK(sender.counters().dataPacketsSent == 5);
	CHECK(receiver.counters().ackPacketsSent == 5);
	CHECK(sender.queuedBytes() == 0);
}

TEST_CASE("NET-SYNC bundle: complete later messages stay head-of-line blocked")
{
	const auto senderOptions = options();
	RelayBundleChannel sender(senderOptions);
	RelayBundleChannel receiver(senderOptions);
	std::string error;
	const auto first = body(2000, 1);
	const auto second = body(3, 99);
	REQUIRE(sender.queue(first, error));
	REQUIRE(sender.queue(second, error));
	const auto packets = sender.poll(0, 100, &error);
	REQUIRE(packets.size() == 3);
	std::vector<std::uint8_t> ack;
	REQUIRE(receiver.receive(packets[2], 0, ack, error));
	CHECK(receiver.takeDelivered().empty());
	REQUIRE(receiver.receive(packets[0], 0, ack, error));
	REQUIRE(receiver.receive(packets[1], 0, ack, error));
	REQUIRE(receiver.receive(packets[2], 0, ack, error));
	const auto delivered = receiver.takeDelivered();
	REQUIRE(delivered.size() == 2);
	CHECK(delivered[0].sequence == 1);
	CHECK(delivered[0].bytes == first);
	CHECK(delivered[1].sequence == 2);
	CHECK(delivered[1].bytes == second);
}

TEST_CASE("NET-SYNC bundle: malformed identity, CRC, size and ACK packets do not mutate state")
{
	const auto senderOptions = options();
	RelayBundleChannel sender(senderOptions);
	RelayBundleChannel receiver(senderOptions);
	std::string error;
	REQUIRE(sender.queue(body(3, 4), error));
	const auto packet = sender.poll(0, 1, &error)[0];
	const std::size_t heldBefore = receiver.bufferedBytes();
	std::vector<std::uint8_t> ackOutput{ 0xAA };

	auto wrongGame = packet;
	put64(wrongGame, 4, senderOptions.gameId + 1);
	fixCrc(wrongGame);
	CHECK_FALSE(receiver.receive(wrongGame, 0, ackOutput, error));
	CHECK(receiver.bufferedBytes() == heldBefore);
	CHECK(ackOutput == std::vector<std::uint8_t>{ 0xAA });

	auto wrongEpoch = packet;
	put64(wrongEpoch, 12, senderOptions.epoch + 1);
	fixCrc(wrongEpoch);
	CHECK_FALSE(receiver.receive(wrongEpoch, 0, ackOutput, error));
	CHECK(receiver.bufferedBytes() == heldBefore);

	auto badCrc = packet;
	badCrc.back() ^= 0x01;
	CHECK_FALSE(receiver.receive(badCrc, 0, ackOutput, error));
	CHECK(receiver.bufferedBytes() == heldBefore);

	auto trailing = packet;
	trailing.push_back(0);
	fixCrc(trailing);
	CHECK_FALSE(receiver.receive(trailing, 0, ackOutput, error));
	CHECK(error == "relay bundle packet has trailing or missing bytes");
	CHECK(receiver.bufferedBytes() == heldBefore);

	auto badIndex = packet;
	put32(badIndex, 28, 1);
	fixCrc(badIndex);
	CHECK_FALSE(receiver.receive(badIndex, 0, ackOutput, error));
	CHECK(receiver.bufferedBytes() == heldBefore);

	auto zeroSequence = packet;
	put64(zeroSequence, 20, 0);
	fixCrc(zeroSequence);
	CHECK_FALSE(receiver.receive(zeroSequence, 0, ackOutput, error));
	CHECK(receiver.bufferedBytes() == heldBefore);
	CHECK(receiver.counters().dataPacketsReceived == 0);

	RelayBundleChannel emptySender(senderOptions);
	auto beforeSend = asAck(packet);
	REQUIRE(emptySender.queue(body(3, 4), error));
	CHECK_FALSE(emptySender.receive(beforeSend, 0, ackOutput, error));
	CHECK(emptySender.queuedBytes() == 195);
	CHECK(emptySender.bufferedBytes() == 0);

	std::vector<std::uint8_t> validAck;
	REQUIRE(receiver.receive(packet, 0, validAck, error));
	CHECK(validAck.size() == RelayBundleChannel::kWireHeaderBytes);
	CHECK(receiver.bufferedBytes() == 195);
}

TEST_CASE("NET-SYNC bundle: bounds retain admitted bytes and reserve the expected message")
{
	auto senderOptions = options();
	senderOptions.maxMessageBytes = 100;
	senderOptions.maxQueuedBytes = 300;
	RelayBundleChannel sender(senderOptions);
	std::string error;
	const auto one = body(1, 1);
	REQUIRE(sender.queue(one, error));
	const auto two = body(1, 2);
	CHECK_FALSE(sender.queue(two, error));
	CHECK(sender.queuedBytes() == 193);

	senderOptions.maxBufferedBytes = 300;
	senderOptions.maxQueuedBytes = 600;
	RelayBundleChannel packetSender(senderOptions);
	REQUIRE(packetSender.queue(body(100, 3), error));
	REQUIRE(packetSender.queue(body(100, 4), error));
	const auto packets = packetSender.poll(0, 100, &error);
	REQUIRE(packets.size() == 2);
	RelayBundleChannel receiver(senderOptions);
	std::vector<std::uint8_t> ack;
	CHECK_FALSE(receiver.receive(packets[1], 0, ack, error));
	CHECK(receiver.takeDelivered().empty());
	CHECK(receiver.bufferedBytes() == 0);
	REQUIRE(receiver.receive(packets[0], 0, ack, error));
	CHECK(receiver.bufferedBytes() == 292);
	const auto drained = receiver.takeDelivered();
	REQUIRE(drained.size() == 1);
	CHECK(drained[0].sequence == 1);
	CHECK(receiver.bufferedBytes() == 0);
	REQUIRE(receiver.receive(packets[1], 1, ack, error));
	CHECK(receiver.takeDelivered().size() == 1);
}

TEST_CASE("NET-SYNC bundle: invalid configuration and duplicate conflicts are explicit")
{
	auto invalid = options();
	invalid.gameId = 0;
	std::string error;
	RelayBundleChannel invalidChannel(invalid, &error);
	CHECK_FALSE(invalidChannel.valid());
	CHECK_FALSE(error.empty());
	CHECK_FALSE(invalidChannel.queue(body(1, 1), error));

	const auto channelOptions = options();
	RelayBundleChannel sender(channelOptions);
	RelayBundleChannel receiver(channelOptions);
	REQUIRE(sender.queue(body(3, 8), error));
	const auto packet = sender.poll(0, 1, &error)[0];
	std::vector<std::uint8_t> ack;
	REQUIRE(receiver.receive(packet, 0, ack, error));
	auto conflict = packet;
	conflict[RelayBundleChannel::kWireHeaderBytes] ^= 0x01;
	fixCrc(conflict);
	CHECK_FALSE(receiver.receive(conflict, 1, ack, error));
	CHECK(receiver.bufferedBytes() == 195);
	CHECK(receiver.takeDelivered().size() == 1);
}

TEST_CASE("NET-SYNC bundle: receive and send windows preserve the missing sequence")
{
	auto channelOptions = options();
	channelOptions.maxPendingMessages = 2;
	channelOptions.maxMessageBytes = 100;
	RelayBundleChannel sender(channelOptions);
	RelayBundleChannel receiver(channelOptions);
	std::string error;
	REQUIRE(sender.queue(body(1, 1), error));
	REQUIRE(sender.queue(body(1, 2), error));
	CHECK_FALSE(sender.queue(body(1, 3), error));
	const auto packets = sender.poll(0, 10, &error);
	REQUIRE(packets.size() == 2);
	std::vector<std::uint8_t> ack;
	REQUIRE(receiver.receive(packets[1], 0, ack, error));
	REQUIRE(sender.receive(ack, 0, ack, error));
	CHECK_FALSE(sender.queue(body(1, 3), error));
	auto seq3 = packets[1];
	put64(seq3, 20, 3);
	fixCrc(seq3);
	CHECK_FALSE(receiver.receive(seq3, 0, ack, error));
	REQUIRE(receiver.receive(packets[0], 0, ack, error));
	const auto delivered = receiver.takeDelivered();
	REQUIRE(delivered.size() == 2);
	CHECK(delivered[0].sequence == 1);
	CHECK(delivered[1].sequence == 2);

	channelOptions.maxPendingMessages = 1;
	RelayBundleChannel sequentialSender(channelOptions);
	RelayBundleChannel sequentialReceiver(channelOptions);
	REQUIRE(sequentialSender.queue(body(1, 4), error));
	auto first = sequentialSender.poll(0, 1, &error);
	REQUIRE(first.size() == 1);
	REQUIRE(sequentialReceiver.receive(first[0], 0, ack, error));
	REQUIRE(sequentialSender.receive(ack, 0, ack, error));
	REQUIRE(sequentialReceiver.takeDelivered().size() == 1);
	REQUIRE(sequentialSender.queue(body(1, 5), error));
	auto second = sequentialSender.poll(1, 1, &error);
	REQUIRE(second.size() == 1);
	REQUIRE(sequentialReceiver.receive(second[0], 1, ack, error));
	REQUIRE(sequentialSender.receive(ack, 1, ack, error));
	CHECK(sequentialReceiver.takeDelivered().size() == 1);
}

TEST_CASE("NET-SYNC bundle: drained history validates retransmits and bounds retention")
{
	auto channelOptions = options();
	channelOptions.maxPendingMessages = 2;
	RelayBundleChannel sender(channelOptions);
	RelayBundleChannel receiver(channelOptions);
	std::string error;
	REQUIRE(sender.queue(body(2000, 12), error));
	const auto packets = sender.poll(0, 10, &error);
	REQUIRE(packets.size() == 2);
	std::vector<std::uint8_t> ack;
	REQUIRE(receiver.receive(packets[0], 0, ack, error));
	REQUIRE(sender.receive(ack, 0, ack, error));
	REQUIRE(receiver.receive(packets[1], 0, ack, error));
	CHECK(receiver.takeDelivered().size() == 1);
	const auto retry = sender.poll(100, 10, &error);
	REQUIRE(retry.size() == 1);
	const auto finalAck = ack;
	REQUIRE(receiver.receive(retry[0], 100, ack, error));
	REQUIRE(sender.receive(ack, 100, ack, error));
	CHECK(sender.pendingMessages() == 0);
	CHECK(receiver.takeDelivered().empty());
	CHECK(receiver.historyBytes() == 192);
	CHECK(receiver.counters().heldBytes == 192);

	auto changed = retry[0];
	changed[RelayBundleChannel::kWireHeaderBytes] ^= 0x01;
	fixCrc(changed);
	CHECK_FALSE(receiver.receive(changed, 101, ack, error));
	REQUIRE(sender.receive(finalAck, 101, ack, error));
	auto badAck = finalAck;
	put32(badAck, 36, 1);
	fixCrc(badAck);
	CHECK_FALSE(sender.receive(badAck, 101, ack, error));

	std::vector<std::uint8_t> oldest;
	for (std::uint8_t seed = 20; seed != 23; ++seed)
	{
		REQUIRE(sender.queue(body(1, seed), error));
		const auto packet = sender.poll(200 + seed, 10, &error);
		REQUIRE(packet.size() == 1);
		if (seed == 20)
		{
			oldest = packet[0];
		}
		REQUIRE(receiver.receive(packet[0], 200 + seed, ack, error));
		REQUIRE(sender.receive(ack, 200 + seed, ack, error));
		REQUIRE(receiver.takeDelivered().size() == 1);
	}
	CHECK(receiver.receiverHistoryMessages() == 2);
	CHECK_FALSE(receiver.receive(oldest, 300, ack, error));
}

TEST_CASE("NET-SYNC bundle: packet budget, fair polling and clock regression are explicit")
{
	auto oversized = options();
	oversized.packetBytes = 1201;
	std::string error;
	RelayBundleChannel invalid(oversized, &error);
	CHECK_FALSE(invalid.valid());
	CHECK_FALSE(error.empty());
	auto insufficientHistory = options();
	insufficientHistory.maxPendingMessages = 1;
	insufficientHistory.maxHistoryBytes = 1;
	RelayBundleChannel invalidHistory(insufficientHistory, &error);
	CHECK_FALSE(invalidHistory.valid());

	const auto channelOptions = options();
	RelayBundleChannel sender(channelOptions);
	REQUIRE(sender.queue(body(3000, 30), error));
	auto first = sender.poll(0, 1, &error);
	auto second = sender.poll(100, 1, &error);
	auto third = sender.poll(200, 1, &error);
	REQUIRE(first.size() == 1);
	REQUIRE(second.size() == 1);
	REQUIRE(third.size() == 1);
	CHECK(read32(second[0], 28) == 1);
	std::vector<std::uint8_t> ack;
	RelayBundleChannel receiver(channelOptions);
	REQUIRE(receiver.receive(second[0], 200, ack, error));
	REQUIRE(sender.receive(ack, 200, ack, error));
	const auto retryFirst = sender.poll(300, 1, &error);
	CHECK(retryFirst.size() == 1);
	CHECK(read32(retryFirst[0], 28) == 0);
	CHECK(sender.poll(300, 1, &error).empty());
	const auto retryThird = sender.poll(300, 1, &error);
	REQUIRE(retryThird.size() == 1);
	CHECK(read32(retryThird[0], 28) == 2);

	REQUIRE(sender.poll(301, 1, &error).empty());
	CHECK(sender.poll(300, 1, &error).empty());
	CHECK(error == "relay bundle clock moved backwards");
}
