// OpenBFME unit tests. GPL-3.0.

#include "doctest.h"

#include "GameNetwork/NetPacket.h"
#include "GameNetwork/RelayCanonicalClient.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
std::array<int, 16> players()
{
	std::array<int, 16> slots;
	slots.fill(-1);
	slots[0] = 0;
	slots[1] = 1;
	slots[2] = 2;
	return slots;
}

RelayMembership::Snapshot running()
{
	RelayMembership::Snapshot snapshot;
	snapshot.slotMask = snapshot.active = snapshot.loaded = 0x0007;
	snapshot.clockValid = true;
	snapshot.originMs = 1000;
	snapshot.firstFrame = 4;
	return snapshot;
}

RelayCommit::Record record(std::uint64_t seq, std::uint32_t frame, std::uint64_t gen,
	RelayCommit::Kind kind, const std::vector<std::uint8_t> &payload)
{
	RelayCommit::Record r;
	r.stamp.game = 1;
	r.stamp.epoch = 1;
	r.stamp.sequence = seq;
	r.stamp.frame = frame;
	r.stamp.membershipGeneration = gen;
	r.kind = kind;
	r.payload = payload;
	return r;
}

RelayCommit::Record membershipRecord(std::uint64_t seq, std::uint32_t frame,
	std::uint64_t gen, const RelayMembership::Snapshot &snapshot)
{
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE(RelayMembership::encode(snapshot, bytes, error));
	return record(seq, frame, gen, RelayCommit::Kind::Membership, bytes);
}

std::vector<std::uint8_t> frameBytes(std::uint8_t slot, std::uint64_t id, int player, int type = 1002)
{
	GameMessage message(type, player);
	NetByteWriter writer;
	std::string error;
	REQUIRE(NetPacket::writeGameMessage(writer, message, &error));
	RelayFrameBundle::Frame frame;
	frame.orders.push_back({ slot, id, writer.take() });
	std::vector<std::uint8_t> bytes;
	REQUIRE(RelayFrameBundle::encode(frame, bytes, error));
	return bytes;
}
}

TEST_CASE("canonical client: a committed frame yields its command list exactly once")
{
	RelayCanonicalClient::Client client(1, 1, 0x0007, 4, players());
	std::string error;

	CHECK(client.accept(membershipRecord(1, 4, 1, running()), error));
	RelayCanonicalClient::FrameInput input;
	CHECK_FALSE(client.takeFrameInput(input));   // membership alone makes no frame input

	CHECK(client.accept(record(2, 4, 1, RelayCommit::Kind::Frame, frameBytes(1, 1, 1)), error));
	REQUIRE(client.takeFrameInput(input));
	CHECK(input.stamp.frame == 4);
	CHECK(input.departed == 0);
	REQUIRE(input.commands.size() == 1);
	CHECK(input.commands[0].slot == 1);
	CHECK(input.commands[0].id == 1);
	CHECK(input.commands[0].message->getPlayerIndex() == 1);
	CHECK(input.commands[0].message->getType() == 1002);
	CHECK_FALSE(client.takeFrameInput(input));   // exactly once
}

TEST_CASE("canonical client: an order claiming another player rejects the whole record")
{
	RelayCanonicalClient::Client client(1, 1, 0x0007, 4, players());
	std::string error;

	CHECK(client.accept(membershipRecord(1, 4, 1, running()), error));
	// slot 1's order carries player 0's identity: the record is not consumed.
	CHECK_FALSE(client.accept(record(2, 4, 1, RelayCommit::Kind::Frame, frameBytes(1, 1, 0)), error));
	CHECK(client.nextFrame() == 4);

	// An order for a slot with no network player rejects alike.
	CHECK_FALSE(client.accept(record(2, 4, 1, RelayCommit::Kind::Frame, frameBytes(3, 1, 0)), error));
	CHECK(client.nextFrame() == 4);
}

TEST_CASE("canonical client: departures stage at the frame and apply in slot order")
{
	RelayCanonicalClient::Client client(1, 1, 0x0007, 4, players());
	std::string error;

	CHECK(client.accept(membershipRecord(1, 4, 1, running()), error));
	CHECK(client.accept(record(2, 4, 1, RelayCommit::Kind::Frame, frameBytes(0, 1, 0)), error));

	auto leaving = running();
	leaving.active = leaving.loaded = 0x0003;  // slot 2 leaves, stamped at frame 5
	leaving.departed = 0x0004;
	CHECK(client.accept(membershipRecord(3, 5, 2, leaving), error));
	CHECK(client.accept(record(4, 5, 2, RelayCommit::Kind::Frame, frameBytes(0, 2, 0)), error));

	RelayCanonicalClient::FrameInput input;
	REQUIRE(client.takeFrameInput(input));
	CHECK(input.stamp.frame == 4);
	CHECK(input.departed == 0);
	REQUIRE(client.takeFrameInput(input));
	CHECK(input.stamp.frame == 5);
	CHECK(input.departed == 0x0004);           // MSG_SELF_DESTRUCT(true) for slot 2 first
	CHECK(client.applied().departed == 0x0004);
}

TEST_CASE("canonical client: out-of-order and wrong-game records are refused")
{
	RelayCanonicalClient::Client client(1, 1, 0x0007, 4, players());
	std::string error;

	// A frame cannot precede the first membership.
	CHECK_FALSE(client.accept(record(1, 4, 1, RelayCommit::Kind::Frame, frameBytes(0, 1, 0)), error));
	CHECK(client.accept(membershipRecord(1, 4, 1, running()), error));

	// Sequences and frames stay dense.
	CHECK_FALSE(client.accept(record(4, 4, 1, RelayCommit::Kind::Frame, frameBytes(0, 1, 0)), error));
	auto otherGame = record(2, 4, 1, RelayCommit::Kind::Frame, frameBytes(0, 1, 0));
	otherGame.stamp.game = 9;
	CHECK_FALSE(client.accept(otherGame, error));
	CHECK_FALSE(client.ended());
}
