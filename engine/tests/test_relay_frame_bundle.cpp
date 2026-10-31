// OpenBFME unit tests. GPL-3.0.

#include "doctest.h"

#include "Common/Sha256.h"
#include "GameNetwork/RelayBundleChannel.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayMembership.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace
{
RelayMembership::Snapshot running()
{
	RelayMembership::Snapshot snapshot;
	snapshot.slotMask = snapshot.active = snapshot.loaded = 0x0003;
	snapshot.clockValid = true;
	snapshot.originMs = 1000;
	snapshot.firstFrame = 4;
	return snapshot;
}

RelayCommit::Stamp stamp(std::uint32_t frame, std::uint64_t sequence = 1)
{
	return { 1, 2, sequence, frame, 1 };
}

std::vector<std::uint8_t> bytes(std::size_t size, std::uint8_t value = 0x5a)
{
	return std::vector<std::uint8_t>(size, value);
}

struct TemporaryDirectory
{
	TemporaryDirectory()
	{
		static std::atomic<std::uint64_t> serial{ 0 };
		std::mt19937_64 random(std::random_device{}());
		path = std::filesystem::temp_directory_path()
			/ ("openbfme_frame_bundle_" + std::to_string(random()) + "_" + std::to_string(serial.fetch_add(1)));
		REQUIRE(std::filesystem::create_directory(path));
	}
	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}
	std::filesystem::path path;
};

// ROUTER-1: the relay's OBFMERLG game log is the durable commit target (the
// source branch's fsync journal was dropped). Append an encoded OBRC record
// synchronously, the way RelayCore::write lands game-log records.
bool appendToLog(const std::filesystem::path &logPath, const RelayCommit::Record &record, std::string &error)
{
	std::vector<std::uint8_t> bytes;
	if (!RelayCommit::encode(record, bytes, error))
	{
		return false;
	}
	FILE *file = std::fopen(logPath.string().c_str(), "ab");
	if (file == nullptr)
	{
		error = "game log open failed";
		return false;
	}
	const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
	if (std::fclose(file) != 0 || !ok)
	{
		error = "game log write failed";
		return false;
	}
	return true;
}
}

TEST_CASE("frame bundle admission enforces boundaries and independent slots")
{
	RelayFrameBundle::Builder builder(1, 2, 0x0003, 4);
	std::string error;
	auto ordinary = bytes(16u * 1024u);
	auto oversized = bytes(16u * 1024u + 1);
	auto maximum = bytes(64u * 1024u);
	CHECK(builder.admit(0, 1, 4, ordinary.data(), ordinary.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	CHECK(builder.admit(0, 2, 4, oversized.data(), oversized.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	CHECK(builder.admit(0, 3, 4, maximum.data(), maximum.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	CHECK(builder.admit(1, 1, 4, ordinary.data(), ordinary.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	CHECK(builder.pendingOrders(0) == 3);
	CHECK(builder.pendingOrders(1) == 1);

	CHECK(builder.admit(0, 4, 4, nullptr, 65537, error) == RelayFrameBundle::Builder::Admission::Malformed);
	CHECK(error == RelayFrameBundle::refusalMessage(RelayFrameBundle::RefusalCode::TooLarge));
	CHECK(builder.admit(0, 5, 4, ordinary.data(), ordinary.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	CHECK(builder.pendingOrders(0) == 4);
	CHECK(builder.admit(0, 6, 4, nullptr, 0, error) == RelayFrameBundle::Builder::Admission::Malformed);
	CHECK(error == RelayFrameBundle::refusalMessage(RelayFrameBundle::RefusalCode::Empty));
	CHECK(builder.admit(0, 7, 4, nullptr, 1, error) == RelayFrameBundle::Builder::Admission::InvalidPayload);
	CHECK(builder.pendingOrders(0) == 4);
}

TEST_CASE("frame bundle builder stages FIFO deferrals and commits them exactly once")
{
	RelayFrameBundle::Builder builder(1, 2, 1, 4);
	std::string error;
	const auto first = bytes(12u * 1024u, 1);
	const auto second = bytes(8u * 1024u, 2);
	const auto third = bytes(64u * 1024u, 3);
	const auto fourth = bytes(7, 4);
	REQUIRE(builder.admit(0, 1, 4, first.data(), first.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(builder.admit(0, 2, 4, second.data(), second.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(builder.admit(0, 3, 4, third.data(), third.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(builder.admit(0, 4, 4, fourth.data(), fourth.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	const auto before = builder.pendingBytes(0);

	RelayCommit::Record record;
	auto membership = running();
	membership.slotMask = membership.active = membership.loaded = 1;
	INFO(error);
	REQUIRE(builder.prepare(stamp(4), membership, record, error));
	CHECK(builder.pendingBytes(0) == before);
	CHECK(record.kind == RelayCommit::Kind::Frame);
	RelayFrameBundle::Frame decoded;
	REQUIRE(RelayFrameBundle::decode(record.payload, decoded, error));
	REQUIRE(decoded.orders.size() == 1);
	CHECK(decoded.orders[0].id == 1);
	REQUIRE(decoded.deferrals.size() == 3);
	CHECK(decoded.deferrals[0].id == 2);
	CHECK(decoded.deferrals[0].fromFrame == 4);
	CHECK(decoded.deferrals[0].toFrame == 5);
	CHECK(decoded.deferrals[0].digest == Sha256::hash(second.data(), second.size()));
	CHECK(decoded.deferrals[0].reason == RelayFrameBundle::DeferralReason::Budget);
	CHECK(decoded.deferrals[1].id == 3);
	CHECK(decoded.deferrals[2].id == 4);
	RelayCommit::Record copy;
	REQUIRE(builder.prepare(stamp(4), membership, copy, error));
	CHECK(copy.payload == record.payload);
	CHECK(builder.admit(0, 5, 0, nullptr, 65537, error) == RelayFrameBundle::Builder::Admission::Malformed);
	CHECK(error == RelayFrameBundle::refusalMessage(RelayFrameBundle::RefusalCode::TooLarge));
	const auto stagedWithAudit = record.payload;
	REQUIRE(builder.prepare(stamp(4), membership, copy, error));
	CHECK(copy.payload == stagedWithAudit);
	CHECK_FALSE(builder.prepare(stamp(5), membership, copy, error));
	CHECK(builder.pendingOrders(0) == 4);
	CHECK_FALSE(builder.committed(stamp(5), error));
	CHECK(builder.pendingOrders(0) == 4);
	REQUIRE(builder.committed(stamp(4), error));
	CHECK(builder.pendingOrders(0) == 3);
	CHECK(builder.counters().refusedOrders == 0);
	CHECK(builder.nextUnpublishedFrame() == 5);
	CHECK_FALSE(builder.committed(stamp(4), error));
	CHECK(builder.pendingOrders(0) == 3);

	REQUIRE(builder.prepare(stamp(5, 2), membership, record, error));
	REQUIRE(RelayFrameBundle::decode(record.payload, decoded, error));
	REQUIRE(decoded.orders.size() == 1);
	CHECK(decoded.orders[0].id == 2);
	CHECK(decoded.deferrals.size() == 2);
	CHECK(decoded.refusals.size() == 1);
	REQUIRE(builder.committed(stamp(5, 2), error));
	REQUIRE(builder.prepare(stamp(6, 3), membership, record, error));
	REQUIRE(RelayFrameBundle::decode(record.payload, decoded, error));
	CHECK(decoded.orders[0].id == 3);
	CHECK(decoded.deferrals.size() == 1);
	REQUIRE(builder.committed(stamp(6, 3), error));
	REQUIRE(builder.prepare(stamp(7, 4), membership, record, error));
	REQUIRE(RelayFrameBundle::decode(record.payload, decoded, error));
	CHECK(decoded.orders[0].id == 4);
	CHECK(decoded.deferrals.empty());
	REQUIRE(builder.committed(stamp(7, 4), error));
	CHECK(builder.pendingOrders(0) == 0);
	CHECK(builder.counters().ordinaryOrders == 3);
	CHECK(builder.counters().oversizedOrders == 1);
	CHECK(builder.counters().normalPlayerFrames == 3);
	CHECK(builder.counters().oversizedPlayerFrames == 1);
	CHECK(builder.counters().deferredOrderFrames == 6);
}

TEST_CASE("frame bundle codec is strict and transactional")
{
	RelayFrameBundle::Frame source;
	source.orders.push_back({ 0, 1, bytes(16384) });
	std::string error;
	RelayFrameBundle::Bytes encoded;
	REQUIRE(RelayFrameBundle::encode(source, encoded, error));
	RelayFrameBundle::Frame output;
	output.orders.push_back({ 1, 9, bytes(3) });
	const auto unchanged = output;
	auto malformed = encoded;
	malformed[4] = 3;
	CHECK_FALSE(RelayFrameBundle::decode(malformed, output, error));
	CHECK(output.orders.size() == unchanged.orders.size());
	CHECK(output.orders[0].id == unchanged.orders[0].id);
	malformed = encoded;
	malformed.pop_back();
	CHECK_FALSE(RelayFrameBundle::decode(malformed, output, error));
	CHECK(output.orders[0].id == unchanged.orders[0].id);
	malformed = encoded;
	malformed[9] = 0x01;
	malformed[10] = 0x00;
	malformed[11] = 0x01;
	malformed[12] = 0x00;
	malformed[18] = 1; // the reserved field
	error = "sentinel";
	CHECK_FALSE(RelayFrameBundle::decode(malformed, output, error));
	CHECK(error != "sentinel");
	CHECK(output.orders[0].id == unchanged.orders[0].id);
	malformed = encoded;
	malformed.push_back(0);
	CHECK_FALSE(RelayFrameBundle::decode(malformed, output, error));
	CHECK(output.orders[0].id == unchanged.orders[0].id);

	RelayFrameBundle::Frame overBudget;
	overBudget.orders.push_back({ 0, 1, bytes(16384) });
	overBudget.orders.push_back({ 0, 2, bytes(1) });
	CHECK_FALSE(RelayFrameBundle::encode(overBudget, encoded, error));
	CHECK(encoded.size() == 22 + 13 + 16384);

	RelayFrameBundle::Frame duplicate;
	duplicate.orders.push_back({ 0, 1, bytes(1) });
	duplicate.deferrals.push_back({ 0, 1, 1, 2, 1, {}, RelayFrameBundle::DeferralReason::Budget });
	CHECK_FALSE(RelayFrameBundle::encode(duplicate, encoded, error));
	duplicate.orders[0].id = 2;
	duplicate.deferrals[0].id = 1;
	CHECK_FALSE(RelayFrameBundle::encode(duplicate, encoded, error));
	RelayFrameBundle::Frame falseBudget;
	falseBudget.orders.push_back({ 0, 1, bytes(1) });
	falseBudget.deferrals.push_back({ 0, 2, 1, 2, 1, {}, RelayFrameBundle::DeferralReason::Budget });
	CHECK_FALSE(RelayFrameBundle::encode(falseBudget, encoded, error));
	falseBudget.orders[0].bytes = bytes(20000);
	falseBudget.deferrals[0].reason = RelayFrameBundle::DeferralReason::Budget;
	CHECK_FALSE(RelayFrameBundle::encode(falseBudget, encoded, error));
}

TEST_CASE("frame bundle validation binds stamp and active membership")
{
	RelayFrameBundle::Frame frame;
	frame.orders.push_back({ 0, 1, bytes(1) });
	std::string error;
	RelayCommit::Stamp good = stamp(4);
	auto membership = running();
	REQUIRE(RelayFrameBundle::validate(frame, good, membership, error));
	membership.paused = true;
	CHECK_FALSE(RelayFrameBundle::validate(frame, good, membership, error));
	membership = running();
	good.sequence = 0;
	CHECK_FALSE(RelayFrameBundle::validate(frame, good, membership, error));
	good = stamp(4);
	frame.orders[0].slot = 2;
	CHECK_FALSE(RelayFrameBundle::validate(frame, good, membership, error));
}

TEST_CASE("frame bundle builder bounds queues and terminal failure")
{
	RelayFrameBundle::Builder builder(1, 2, 1, 4);
	std::string error;
	const auto payload = bytes(4096);
	for (std::uint64_t id = 1; id <= 256; ++id)
	{
		CHECK(builder.admit(0, id, 4, payload.data(), payload.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	}
	CHECK(builder.admit(0, 257, 4, payload.data(), payload.size(), error) == RelayFrameBundle::Builder::Admission::Busy);
	CHECK(builder.pendingOrders(0) == 256);
	RelayFrameBundle::Builder refusals(1, 2, 1, 4);
	for (std::uint64_t id = 1; id <= 256; ++id)
	{
		CHECK(refusals.admit(0, id, 4, nullptr, 65537, error) == RelayFrameBundle::Builder::Admission::Malformed);
	}
	CHECK(refusals.admit(0, 257, 4, nullptr, 65537, error) == RelayFrameBundle::Builder::Admission::Busy);
	RelayFrameBundle::Builder terminal(1, 2, 1, 4);
	terminal.fail("game log append failed");
	CHECK(terminal.admit(0, 1, 4, payload.data(), payload.size(), error) == RelayFrameBundle::Builder::Admission::Terminal);
	RelayCommit::Record record;
	CHECK_FALSE(terminal.prepare(stamp(4), running(), record, error));
	RelayFrameBundle::Builder emptyFailure(1, 2, 1, 4);
	REQUIRE(emptyFailure.admit(0, 1, 4, payload.data(), payload.size(), error)
		== RelayFrameBundle::Builder::Admission::Accepted);
	auto oneSlot = running();
	oneSlot.slotMask = oneSlot.active = oneSlot.loaded = 1;
	REQUIRE(emptyFailure.prepare(stamp(4), oneSlot, record, error));
	const auto pendingBeforeFailure = emptyFailure.pendingOrders(0);
	emptyFailure.fail("");
	CHECK_FALSE(emptyFailure.committed(stamp(4), error));
	CHECK(error == "canonical frame builder log append failure");
	CHECK(emptyFailure.pendingOrders(0) == pendingBeforeFailure);
	CHECK_FALSE(emptyFailure.prepare(stamp(4), oneSlot, record, error));
}

TEST_CASE("frame bundle held slot waits while active slot commits")
{
	RelayFrameBundle::Builder builder(1, 2, 0x0003, 4);
	std::string error;
	const auto payload = bytes(17);
	REQUIRE(builder.admit(0, 1, 4, payload.data(), payload.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(builder.admit(1, 1, 4, payload.data(), payload.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	auto membership = running();
	membership.active = membership.loaded = 2;
	membership.held = 1;
	RelayCommit::Record record;
	INFO(error);
	REQUIRE(builder.prepare(stamp(4), membership, record, error));
	RelayFrameBundle::Frame frame;
	REQUIRE(RelayFrameBundle::decode(record.payload, frame, error));
	REQUIRE(frame.orders.size() == 1);
	CHECK(frame.orders[0].slot == 1);
	REQUIRE(builder.committed(stamp(4), error));
	CHECK(builder.pendingOrders(0) == 1);
	CHECK(builder.pendingOrders(1) == 0);
	membership.active = membership.loaded = 3;
	membership.held = 0;
	REQUIRE(builder.prepare(stamp(5, 2), membership, record, error));
	REQUIRE(RelayFrameBundle::decode(record.payload, frame, error));
	REQUIRE(frame.orders.size() == 1);
	CHECK(frame.orders[0].slot == 0);
	CHECK(frame.orders[0].id == 1);
	REQUIRE(builder.committed(stamp(5, 2), error));
	CHECK(builder.pendingOrders(0) == 0);
}

TEST_CASE("frame bundle admission rejects stale and exhausted frame eligibility")
{
	RelayFrameBundle::Builder builder(1, 2, 1, 4);
	std::string error;
	const auto payload = bytes(1);
	CHECK(builder.admit(0, 1, 3, payload.data(), payload.size(), error)
		== RelayFrameBundle::Builder::Admission::InvalidFrame);
	CHECK(builder.admit(0, 1, std::numeric_limits<std::uint32_t>::max(), nullptr, 1, error)
		== RelayFrameBundle::Builder::Admission::InvalidFrame);
	CHECK(builder.pendingOrders(0) == 0);
	RelayFrameBundle::Builder exhausted(1, 2, 1, std::numeric_limits<std::uint32_t>::max() - 1);
	auto membership = running();
	membership.slotMask = membership.active = membership.loaded = 1;
	RelayCommit::Record record;
	REQUIRE(exhausted.admit(0, 1, std::numeric_limits<std::uint32_t>::max() - 1,
		payload.data(), payload.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(exhausted.prepare(
		{ 1, 2, 1, std::numeric_limits<std::uint32_t>::max() - 1, 1 }, membership, record, error));
	REQUIRE(exhausted.committed(record.stamp, error));
	CHECK(exhausted.nextUnpublishedFrame() == std::numeric_limits<std::uint32_t>::max());
	CHECK(exhausted.admit(0, 2, std::numeric_limits<std::uint32_t>::max(), payload.data(), payload.size(), error)
		== RelayFrameBundle::Builder::Admission::Terminal);
}

TEST_CASE("frame bundle game-log channel cursor integration is durable and fragmented")
{
	TemporaryDirectory temporary;
	RelayFrameBundle::Builder builder(1, 2, 1, 0);
	std::string error;
	const auto oversized = bytes(20000, 0x31);
	const auto follower = bytes(7, 0x32);
	REQUIRE(builder.admit(0, 1, 0, oversized.data(), oversized.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(builder.admit(0, 2, 0, follower.data(), follower.size(), error) == RelayFrameBundle::Builder::Admission::Accepted);
	REQUIRE(builder.admit(0, 3, 0, nullptr, 65537, error) == RelayFrameBundle::Builder::Admission::Malformed);
	auto membership = running();
	membership.slotMask = membership.active = membership.loaded = 1;
	membership.firstFrame = 0;
	membership.originMs = 1000;
	RelayCommit::Record frameRecord;
	const RelayCommit::Stamp frameStamp{ 1, 2, 2, 0, 1 };
	REQUIRE(builder.prepare(frameStamp, membership, frameRecord, error));
	const auto pendingBeforeAppend = builder.pendingOrders(0);
	const auto countersBeforeAppend = builder.counters();

	const auto logPath = temporary.path / "game.obfmerlg";
	RelayCommit::Record membershipRecord;
	membershipRecord.kind = RelayCommit::Kind::Membership;
	membershipRecord.stamp = { 1, 2, 1, 0, 1 };
	REQUIRE(RelayMembership::encode(membership, membershipRecord.payload, error));
	REQUIRE(appendToLog(logPath, membershipRecord, error));
	REQUIRE(appendToLog(logPath, frameRecord, error));
	CHECK(builder.pendingOrders(0) == pendingBeforeAppend);
	CHECK(builder.counters().ordinaryOrders == countersBeforeAppend.ordinaryOrders);
	CHECK(builder.counters().oversizedOrders == countersBeforeAppend.oversizedOrders);
	REQUIRE(builder.committed(frameStamp, error));

	RelayBundleChannel::Options channelOptions;
	channelOptions.gameId = 1;
	channelOptions.epoch = 2;
	RelayBundleChannel sender(channelOptions), receiver(channelOptions);
	RelayBundleChannel::Bytes wire;
	REQUIRE(RelayCommit::encode(frameRecord, wire, error));
	REQUIRE(sender.queue(wire, error));
	const auto packets = sender.poll(0, 256, &error);
	REQUIRE(packets.size() > 1); // tolerance: the 20 KiB record must fragment into multiple channel packets
	RelayBundleChannel::Bytes ack;
	RelayBundleChannel::Bytes senderAck;
	const std::size_t lost = packets.size() / 2;
	for (std::size_t i = 0; i != packets.size(); ++i)
	{
		if (i == lost)
		{
			continue;
		}
		REQUIRE(receiver.receive(packets[i], 0, ack, error));
		REQUIRE(sender.receive(ack, 0, senderAck, error));
	}
	CHECK(receiver.takeDelivered().empty());
	RelayMembership::Cursor cursor(1, 2, 1, 0);
	REQUIRE(cursor.accept(membershipRecord,
		[](const RelayCommit::Record &, const RelayMembership::Snapshot &, std::string &) { return true; }, error));
	CHECK(cursor.nextFrame() == 0);
	const auto retry = sender.poll(100, 256, &error);
	REQUIRE(retry.size() == 1);
	REQUIRE(receiver.receive(retry[0], 100, ack, error));
	REQUIRE(sender.receive(ack, 100, senderAck, error));
	const auto delivered = receiver.takeDelivered();
	REQUIRE(delivered.size() == 1);
	RelayCommit::Record decodedRecord;
	REQUIRE(RelayCommit::decode(delivered[0].bytes, 2u * 1024u * 1024u, decodedRecord, error));
	unsigned validated = 0;
	REQUIRE(cursor.accept(decodedRecord,
		[&](const RelayCommit::Record &record, const RelayMembership::Snapshot &staged, std::string &failure) {
			++validated;
			RelayFrameBundle::Frame decoded;
			return RelayFrameBundle::decode(record.payload, decoded, failure)
				&& RelayFrameBundle::validate(decoded, record.stamp, staged, failure)
				&& decoded.orders.size() == 1 && decoded.orders[0].id == 1
				&& decoded.deferrals.size() == 1 && decoded.deferrals[0].id == 2
				&& decoded.refusals.size() == 1;
		}, error));
	CHECK(validated == 1);
	CHECK(cursor.nextFrame() == 1);

	RelayCommit::Record nextRecord;
	const RelayCommit::Stamp nextStamp{ 1, 2, 3, 1, 1 };
	REQUIRE(builder.prepare(nextStamp, membership, nextRecord, error));
	REQUIRE(appendToLog(logPath, nextRecord, error));
	REQUIRE(builder.committed(nextStamp, error));
	REQUIRE(RelayCommit::encode(nextRecord, wire, error));
	REQUIRE(RelayCommit::decode(wire, 2u * 1024u * 1024u, decodedRecord, error));
	REQUIRE(cursor.accept(decodedRecord,
		[](const RelayCommit::Record &record, const RelayMembership::Snapshot &staged, std::string &failure) {
			RelayFrameBundle::Frame decoded;
			return RelayFrameBundle::decode(record.payload, decoded, failure)
				&& RelayFrameBundle::validate(decoded, record.stamp, staged, failure)
				&& decoded.orders.size() == 1 && decoded.orders[0].id == 2;
		}, error));
	CHECK(builder.pendingOrders(0) == 0);
}
