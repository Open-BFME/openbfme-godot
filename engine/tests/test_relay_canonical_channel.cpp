// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"

#include "GameNetwork/RelayBundleChannel.h"
#include "GameNetwork/RelayMembership.h"

TEST_CASE("NET-SYNC canonical record channel preserves staged membership across lost fragments")
{
	RelayBundleChannel::Options options;
	options.gameId = 1;
	options.epoch = 2;
	RelayBundleChannel sender(options), receiver(options);
	RelayMembership::Cursor cursor(1, 2, 0x8001, 4);
	std::string error;
	RelayMembership::Snapshot membership;
	membership.slotMask = membership.active = membership.loaded = 0x8001;
	membership.clockValid = true;
	membership.originMs = 1000;
	membership.firstFrame = 4;
	RelayCommit::Record record;
	record.kind = RelayCommit::Kind::Membership;
	record.stamp = { 1, 2, 1, 4, 1 };
	REQUIRE(RelayMembership::encode(membership, record.payload, error));
	auto queue = [&](const RelayCommit::Record &value) {
		RelayBundleChannel::Bytes encoded;
		REQUIRE(RelayCommit::encode(value, encoded, error));
		REQUIRE(sender.queue(encoded, error));
	};
	queue(record);
	membership.active = membership.loaded = 1;
	membership.departed = 0x8000;
	record.stamp.sequence = 2;
	record.stamp.membershipGeneration = 2;
	REQUIRE(RelayMembership::encode(membership, record.payload, error));
	queue(record);

	// Opaque test payloads, not a production frame/order codec.
	const RelayBundleChannel::Bytes firstPayload(4000, 42), nextPayload{ 17 };
	record.kind = RelayCommit::Kind::Frame;
	record.stamp.sequence = 3;
	record.payload = firstPayload;
	queue(record);
	record.stamp.sequence = 4;
	record.stamp.frame = 5;
	record.payload = nextPayload;
	queue(record);
	unsigned validations = 0;
	auto validate = [&](const RelayCommit::Record &frame, const RelayMembership::Snapshot &staged, std::string &failure) {
		++validations;
		if (staged.departed != 0x8000 || frame.payload != (frame.stamp.frame == 4 ? firstPayload : nextPayload))
		{
			failure = "unexpected canonical test payload or membership";
			return false;
		}
		return true;
	};
	auto accept = [&](const RelayBundleChannel::Message &message) {
		RelayCommit::Record decoded;
		REQUIRE(RelayCommit::decode(message.bytes, 4096, decoded, error));
		REQUIRE(cursor.accept(decoded, validate, error));
	};
	const auto packets = sender.poll(0, 100, &error);
	REQUIRE(packets.size() == 7);
	RelayBundleChannel::Bytes ack;
	for (std::size_t i = 0; i != 2; ++i)
	{
		REQUIRE(receiver.receive(packets[i], 0, ack, error));
		REQUIRE(sender.receive(ack, 0, ack, error));
	}
	const auto lifecycle = receiver.takeDelivered();
	REQUIRE(lifecycle.size() == 2);
	for (const auto &message : lifecycle)
	{
		accept(message);
	}
	CHECK(cursor.staged().departed == 0x8000);
	CHECK(cursor.applied().departed == 0);
	CHECK_FALSE(cursor.hasAppliedFrame());

	// Lose the first frame fragment; deliver all later fragments and the next frame backwards.
	for (std::size_t i = packets.size(); i != 3;)
	{
		--i;
		REQUIRE(receiver.receive(packets[i], 0, ack, error));
		REQUIRE(sender.receive(ack, 0, ack, error));
		CHECK(receiver.takeDelivered().empty());
		CHECK(cursor.nextFrame() == 4);
		CHECK(cursor.applied().departed == 0);
		CHECK(validations == 0);
	}
	const auto retry = sender.poll(100, 100, &error);
	REQUIRE(retry.size() == 1);
	CHECK(retry[0] == packets[2]);
	REQUIRE(receiver.receive(retry[0], 100, ack, error));
	REQUIRE(sender.receive(ack, 100, ack, error));
	const auto frames = receiver.takeDelivered();
	REQUIRE(frames.size() == 2);
	accept(frames[0]);
	CHECK(cursor.nextFrame() == 5);
	CHECK(cursor.departedAtFrame() == 0x8000);
	CHECK(cursor.applied().departed == 0x8000);
	accept(frames[1]);
	CHECK(cursor.nextFrame() == 6);
	CHECK(cursor.departedAtFrame() == 0);
	CHECK(validations == 2);

	REQUIRE(receiver.receive(retry[0], 101, ack, error));
	REQUIRE(sender.receive(ack, 101, ack, error));
	CHECK(receiver.takeDelivered().empty());
	CHECK(validations == 2);
	CHECK(sender.queuedBytes() == 0);
}
