#include "doctest.h"

#include "Common/Crc32.h"
#include "GameNetwork/RelayProtocol.h"
#include "RelayCore.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> testKey()
{
	std::vector<std::uint8_t> key(32);
	for (std::size_t i = 0; i < key.size(); ++i)
	{
		key[i] = (std::uint8_t)i;
	}
	return key;
}

RelayProtocol::KeyRing testRing()
{
	return { { 1, testKey() } };
}

RelayProtocol::Ticket ticket(std::uint8_t slot, std::uint16_t mask, std::uint64_t game = 0x1122334455667788ull, std::uint16_t generation = 0)
{
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = game;
	t.slot = slot;
	t.slotMask = mask;
	t.expires = 2000000000u;
	t.generation = generation;
	return t;
}

RelayAddress address(std::uint8_t slot, std::uint16_t base = 21000)
{
	return RelayAddress{ 0x0a000001u, (std::uint16_t)(base + slot) };
}

class TemporaryDirectory
{
public:
	TemporaryDirectory()
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		for (unsigned int attempt = 0; attempt < 1000; ++attempt)
		{
			path = std::filesystem::temp_directory_path() / ("openbfme_relay_resources_" + std::to_string(now) + "_" + std::to_string(attempt));
			if (std::filesystem::create_directory(path))
			{
				return;
			}
		}
		throw std::runtime_error("could not create a unique relay resource test directory");
	}
	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}
	const std::filesystem::path &get() const { return path; }

private:
	std::filesystem::path path;
};

RelayCore::Options optionsFor(const TemporaryDirectory &directory, size_t maxGameBuffer = 16u << 20)
{
	RelayCore::Options options;
	options.keys = testRing();
	options.logDir = directory.get().string();
	options.maxGameBuffer = maxGameBuffer;
	return options;
}

RelayCore::Action bind(RelayCore &relay, std::uint8_t slot, std::uint16_t mask, std::uint64_t game = 0x1122334455667788ull,
	std::uint16_t generation = 0, std::uint16_t base = 21000)
{
	const auto signedTicket = generation == 0 ? RelayProtocol::signTicket(ticket(slot, mask, game), testKey())
		: RelayProtocol::signRejoinTicket(ticket(slot, mask, game, generation), testKey());
	std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(signedTicket);
	return relay.handle(address(slot, base), hello.data(), hello.size(), 1000, 1700000000u);
}

void bindSlots(RelayCore &relay, std::uint8_t count, std::uint16_t mask, std::uint64_t game = 0x1122334455667788ull, std::uint16_t base = 21000)
{
	for (std::uint8_t slot = 0; slot < count; ++slot)
	{
		RelayCore::Action action = bind(relay, slot, mask, game, 0, base);
		REQUIRE(action.kind == RelayCore::Action::REPLY);
	}
}

void put16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
	bytes.push_back((std::uint8_t)value);
	bytes.push_back((std::uint8_t)(value >> 8));
}

void put32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
	bytes.push_back((std::uint8_t)value);
	bytes.push_back((std::uint8_t)(value >> 8));
	bytes.push_back((std::uint8_t)(value >> 16));
	bytes.push_back((std::uint8_t)(value >> 24));
}

std::vector<std::uint8_t> frameInfo(std::uint8_t sender, std::uint32_t frame, std::uint16_t commandCount = 1)
{
	std::vector<std::uint8_t> bytes = { 3, sender };
	put32(bytes, frame);
	put16(bytes, commandCount);
	return bytes;
}

std::vector<std::uint8_t> transportPacket(std::uint8_t sender, std::uint32_t sequence, const std::vector<std::uint8_t> &command)
{
	std::vector<std::uint8_t> packet(4, 0);
	put16(packet, 0x4642);
	packet.push_back(2);
	packet.push_back(sender);
	put32(packet, 0);
	put16(packet, 1);
	put32(packet, sequence);
	put16(packet, 0);
	put16(packet, 1);
	put32(packet, (std::uint32_t)command.size());
	packet.insert(packet.end(), command.begin(), command.end());
	const std::uint32_t crc = crc32Bytes(packet.data() + 4, packet.size() - 4);
	packet[0] = (std::uint8_t)crc;
	packet[1] = (std::uint8_t)(crc >> 8);
	packet[2] = (std::uint8_t)(crc >> 16);
	packet[3] = (std::uint8_t)(crc >> 24);
	return packet;
}

RelayCore::Action sendCommand(RelayCore &relay, std::uint8_t sender, std::uint8_t target, std::uint32_t sequence, std::uint32_t frame,
	std::uint64_t nowMs, std::uint16_t commandCount = 1, std::uint16_t base = 21000)
{
	const std::vector<std::uint8_t> command = frameInfo(sender, frame, commandCount);
	std::vector<std::uint8_t> data = RelayProtocol::encodeData(target, transportPacket(sender, sequence, command));
	return relay.handle(address(sender, base), data.data(), data.size(), nowMs, 1700000000u);
}
} // namespace

TEST_CASE("relay resources: sixteen-slot shared-count accounting")
{
	// the 16-slot pin (coordinator, 2026-10-17): the shared count table's charge per byte, and where it comes from.
	// A command's (frame, hash) entry in one half of the split table costs 4 x kMetaNodeBytes = 256 bytes: the frame
	// map node and the command map node, each charged at 2 x kMetaNodeBytes -- the real map node plus the per-stream
	// perFrame node it consolidates (the old model's stream + sender pair for a delivery). Another target in the same
	// half on the same (frame, hash) adds nothing (its column is already held); the first target in the other half
	// adds that half's own 256. A game of eight or fewer slots therefore keeps exactly the old 256-byte entry, while
	// the cost of a sixteen-slot game scales with the slot ranges its traffic actually uses. An already-logged hash is
	// never logged twice, whichever half the next delivery lands in.
	TemporaryDirectory directory;
	RelayCore relay(optionsFor(directory));
	bindSlots(relay, 16, 0xffff);
	const size_t base = relay.heldBytes();

	CHECK(sendCommand(relay, 0, 1, 0, 3, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(relay.heldBytes() - base == 256);
	CHECK(sendCommand(relay, 0, 2, 0, 3, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(relay.heldBytes() - base == 256); // a second target in the same half: its column was already paid for
	CHECK(sendCommand(relay, 0, 8, 0, 3, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(relay.heldBytes() - base == 256 + 256); // the other half's own entry
	CHECK(relay.stats().commandsLogged == 1);     // one command, once, across both halves
	CHECK(sendCommand(relay, 0, 1, 1, 4, 1200).kind == RelayCore::Action::FORWARD);
	CHECK(relay.heldBytes() - base == 256 + 256 + 256);
	CHECK(relay.stats().commandsLogged == 2);
	CHECK(relay.heldBytes() == relay.retainedBytes());
	CHECK(relay.stats().accountingErrors == 0);
}

TEST_CASE("relay resources: sixteen-slot sequential shared-count workload")
{
	TemporaryDirectory directory;
	// every sender broadcasts every frame to all 15 targets: per sender per frame the table is charged 256 for the low
	// half's entry + 256 for the high half's = 512 bytes; x16 senders x1500 frames = 12,288,000, under the 16 MiB
	// default game buffer -- the same headroom the consolidation gave the eight-slot workload. The frame span (1500)
	// stays under kKeepFrames (4000) so nothing is trimmed; the whole workload is held at once
	RelayCore relay(optionsFor(directory));
	const std::uint16_t mask = 0xffff;
	bindSlots(relay, 16, mask);

	for (std::uint32_t frame = 0; frame < 1500; ++frame)
	{
		const std::uint64_t nowMs = 1000 + (std::uint64_t)frame * 200;
		bool allForwarded = true;
		for (std::uint8_t sender = 0; sender < 16; ++sender)
		{
			for (std::uint8_t target = 0; target < 16; ++target)
			{
				if (target == sender)
				{
					continue;
				}
				const RelayCore::Action action = sendCommand(relay, sender, target, frame, frame, nowMs);
				allForwarded = allForwarded && action.kind == RelayCore::Action::FORWARD;
			}
		}
		CHECK(allForwarded);
		if ((frame + 1) % 250 == 0)
		{
			CHECK(relay.heldBytes() == relay.retainedBytes());
		}
	}
	CHECK(relay.stats().logGaps == 0);
	CHECK(relay.stats().logErrors == 0);
	CHECK(relay.stats().commandsLogged == 24000);
	CHECK(relay.stats().foreignFrameCommands == 0);
	CHECK(relay.heldBytes() == relay.retainedBytes());
}

TEST_CASE("relay resources: occurrence deduplication and incarnation resets")
{
	TemporaryDirectory directory;
	RelayCore relay(optionsFor(directory));
	const std::uint16_t mask = 0x0003;
	bindSlots(relay, 2, mask);

	CHECK(sendCommand(relay, 0, 1, 0, 7, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(relay.stats().commandsLogged == 1);
	CHECK(sendCommand(relay, 0, 1, 1, 7, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(relay.stats().commandsLogged == 2);
	CHECK(sendCommand(relay, 0, 0, 0, 7, 1000).kind == RelayCore::Action::REPLY);
	CHECK(sendCommand(relay, 0, 0, 1, 7, 1000).kind == RelayCore::Action::REPLY);
	CHECK(relay.stats().commandsLogged == 2);
	CHECK(sendCommand(relay, 0, 0, 2, 7, 1000).kind == RelayCore::Action::REPLY);
	CHECK(relay.stats().commandsLogged == 3);
	CHECK(relay.heldBytes() == relay.retainedBytes());

	RelayCore::Action targetRejoin = bind(relay, 1, mask, 0x1122334455667788ull, 1, 22000);
	REQUIRE(targetRejoin.kind == RelayCore::Action::REPLY);
	CHECK(sendCommand(relay, 0, 1, 0, 7, 1100).kind == RelayCore::Action::FORWARD);
	CHECK(relay.stats().commandsLogged == 3);
	CHECK(relay.heldBytes() == relay.retainedBytes());

	RelayCore::Action senderRejoin = bind(relay, 0, mask, 0x1122334455667788ull, 1, 23000);
	REQUIRE(senderRejoin.kind == RelayCore::Action::REPLY);
	CHECK(sendCommand(relay, 0, 1, 0, 7, 1200, 1, 23000).kind == RelayCore::Action::FORWARD);
	CHECK(relay.stats().commandsLogged == 4);
	CHECK(relay.heldBytes() == relay.retainedBytes());
}

TEST_CASE("relay resources: caps, gap release, close release, and frame trimming")
{
	{
		TemporaryDirectory directory;
		RelayCore::Options options = optionsFor(directory);
		options.maxStreamBuffer = 128;
		RelayCore relay(options);
		bindSlots(relay, 2, 0x0003);
		CHECK(sendCommand(relay, 0, 1, 0, 3, 1000).kind == RelayCore::Action::FORWARD);
		CHECK(sendCommand(relay, 0, 1, 1, 3, 1000, 2).kind == RelayCore::Action::FORWARD);
		CHECK(relay.stats().commandsLogged == 1);
		CHECK(relay.stats().logGaps == 1);
		CHECK(relay.heldBytes() == relay.retainedBytes());
		CHECK(relay.stats().accountingErrors == 0);
	}
	{
		TemporaryDirectory directory;
		RelayCore::Options options = optionsFor(directory);
		options.maxGameBuffer = 255;
		RelayCore relay(options);
		bindSlots(relay, 2, 0x0003);
		CHECK(sendCommand(relay, 0, 1, 0, 3, 1000).kind == RelayCore::Action::FORWARD);
		CHECK(relay.stats().commandsLogged == 0);
		CHECK(relay.stats().logGaps == 1);
		CHECK(relay.heldBytes() == relay.retainedBytes());
		CHECK(relay.stats().accountingErrors == 0);
	}
	{
		TemporaryDirectory directory;
		RelayCore relay(optionsFor(directory));
		bindSlots(relay, 2, 0x0003);
		for (std::uint32_t frame = 0; frame < 4002; ++frame)
		{
			CHECK(sendCommand(relay, 0, 1, frame, frame, 1000 + (std::uint64_t)frame * 200).kind == RelayCore::Action::FORWARD);
		}
		CHECK(relay.stats().commandsLogged == 4002);
		const size_t afterTrim = relay.heldBytes();
		CHECK(afterTrim == relay.retainedBytes());
		CHECK(sendCommand(relay, 0, 1, 4002, 4002, 802000).kind == RelayCore::Action::FORWARD);
		CHECK(relay.heldBytes() == afterTrim);
		CHECK(relay.heldBytes() == relay.retainedBytes());
		relay.shutdown(900000);
		CHECK(relay.openGames() == 0);
		CHECK(relay.heldBytes() == 0);
		CHECK(relay.retainedBytes() == 0);
	}
}
