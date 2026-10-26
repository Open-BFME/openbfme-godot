// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"
#include "GameNetwork/RelayFrameBundle.h"

#include <array>
#include <iostream>

TEST_CASE("NET-SYNC frame bundle synthetic sixteen-slot budget counters")
{
	using namespace RelayFrameBundle;
	Builder builder(1, 2, 0xffff, 0);
	RelayMembership::Snapshot membership;
	membership.slotMask = membership.active = membership.loaded = 0xffff;
	membership.clockValid = true;
	membership.originMs = 1000;
	std::string error;
	const std::array<std::size_t, 4> sizes{16384, 16385, 65536, 512};
	for (std::uint8_t slot = 0; slot < 16; ++slot)
	{
		for (std::size_t i = 0; i < sizes.size(); ++i)
		{
			const Bytes bytes(sizes[i], static_cast<std::uint8_t>(0xa0 + slot));
			REQUIRE(builder.admit(slot, i + 1, 0, bytes.data(), bytes.size(), error) == Builder::Admission::Accepted);
		}
		REQUIRE(builder.admit(slot, 5, 0, nullptr, 65537, error) == Builder::Admission::Malformed);
		CHECK(error == refusalMessage(RefusalCode::TooLarge));
	}
	std::uint64_t bytesExecuted = 0;
	std::uint64_t observedOrders = 0, observedDeferrals = 0, observedRefusals = 0;
	for (std::uint32_t f = 0; f < 4; ++f)
	{
		RelayCommit::Record record;
		const RelayCommit::Stamp stamp{1, 2, f + 2, f, 1};
		REQUIRE(builder.prepare(stamp, membership, record, error));
		Frame decoded;
		REQUIRE(decode(record.payload, decoded, error));
		REQUIRE(validate(decoded, stamp, membership, error));
		REQUIRE(decoded.orders.size() == 16);
		CHECK(decoded.deferrals.size() == (3 - f) * 16);
		CHECK(decoded.refusals.size() == (f == 0 ? 16 : 0));
		for (std::uint8_t slot = 0; slot < 16; ++slot)
		{
			const auto &order = decoded.orders[slot];
			CHECK(order.slot == slot);
			CHECK(order.id == f + 1);
			CHECK(order.bytes == Bytes(sizes[f], static_cast<std::uint8_t>(0xa0 + slot)));
			bytesExecuted += order.bytes.size();
		}
		observedOrders += decoded.orders.size();
		observedDeferrals += decoded.deferrals.size();
		observedRefusals += decoded.refusals.size();
		// Algorithm-only metric fixture: model a successful completion here.
		// Real journal + fragment-channel coverage lives in test_relay_frame_bundle.
		REQUIRE(builder.committed(stamp, error));
	}
	const auto &counts = builder.counters();
	CHECK(observedOrders == 64);
	CHECK(observedDeferrals == 96);
	CHECK(observedRefusals == 16);
	CHECK(bytesExecuted == 1581072);
	CHECK(counts.ordinaryOrders == 32);
	CHECK(counts.oversizedOrders == 32);
	CHECK(counts.normalPlayerFrames == 32);
	CHECK(counts.oversizedPlayerFrames == 32);
	CHECK(counts.deferredOrderFrames == observedDeferrals);
	CHECK(counts.refusedOrders == observedRefusals);
	for (std::uint8_t slot = 0; slot < 16; ++slot)
	{
		CHECK(builder.pendingOrders(slot) == 0);
		CHECK(builder.pendingBytes(slot) == 0);
	}
	std::cout << "NET_SYNC_BUDGET_METRICS {\"fixture\":\"synthetic-boundaries-not-gameplay\","
		<< "\"slots\":16,\"frames\":4,\"ordinary_orders\":" << counts.ordinaryOrders
		<< ",\"oversized_orders\":" << counts.oversizedOrders
		<< ",\"normal_player_frames\":" << counts.normalPlayerFrames
		<< ",\"oversized_player_frames\":" << counts.oversizedPlayerFrames
		<< ",\"deferred_order_frames\":" << counts.deferredOrderFrames
		<< ",\"refused_orders\":" << counts.refusedOrders
		<< ",\"executed_payload_bytes\":" << bytesExecuted << "}\n";
}
