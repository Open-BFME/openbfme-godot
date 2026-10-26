// OpenBFME. GPL-3.0.
//
// NET-SYNC: the canonical mode through the Network driver itself — local
// orders and beats leave as OBCI messages, the committed OBRC record stream
// becomes the frame's whole input, and the local client's own orders run
// once, from the relay's echo. A NetTransport fake carries the canonical
// surface (relayCanonical / sendCanonicalOrder / sendCanonicalBeat /
// takeCanonicalRecord); the records are produced exactly as the relay's
// journal emits them.

#include "doctest.h"

#include "GameNetwork/Network.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/RelayCanonicalClient.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayMembership.h"

#include <deque>
#include <utility>
#include <vector>

namespace
{
struct CanonTransport : NetTransport
{
	bool sendTo(int, const std::vector<std::uint8_t> &, std::string *) override { return false; }
	bool receive(int &, std::vector<std::uint8_t> &) override { return false; }
	void service() override { ++services; }
	bool relayCanonical() const override { return true; }
	std::uint64_t canonicalGame() const override { return 1; }
	std::uint64_t canonicalEpoch() const override { return 1; }
	bool sendCanonicalOrder(std::uint64_t id, const std::vector<std::uint8_t> &bytes, std::uint64_t, std::string &) override
	{
		orders.push_back({id, bytes});
		return true;
	}
	bool sendCanonicalBeat(std::uint64_t id, std::uint64_t, std::string &) override
	{
		beats.push_back(id);
		return true;
	}
	bool takeCanonicalRecord(std::vector<std::uint8_t> &bytes) override
	{
		if (records.empty())
		{
			return false;
		}
		bytes = std::move(records.front());
		records.pop_front();
		return true;
	}

	std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> orders;
	std::vector<std::uint64_t> beats;
	std::deque<std::vector<std::uint8_t>> records;
	int services = 0;
};

NetworkConfig twoPlayers()
{
	NetworkConfig config;
	config.localSlot = 0;
	config.slotPlayerIndex[0] = 0;
	config.slotPlayerIndex[1] = 1;
	return config;
}

RelayMembership::Snapshot membership(std::uint16_t active, std::uint16_t departed = 0)
{
	RelayMembership::Snapshot s;
	s.slotMask = 0x0003;
	s.active = active;
	s.loaded = active;
	s.departed = departed;
	s.clockValid = true;
	s.originMs = 0;
	s.firstFrame = 1;
	return s;
}

void push(std::deque<std::vector<std::uint8_t>> &records, const RelayCommit::Record &record)
{
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE(RelayCommit::encode(record, bytes, error));
	records.push_back(std::move(bytes));
}

RelayCommit::Record record(std::uint64_t seq, std::uint32_t frame, std::uint64_t gen, RelayCommit::Kind kind,
	const std::vector<std::uint8_t> &payload)
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

std::vector<std::uint8_t> framePayload(std::initializer_list<std::pair<std::uint8_t, int>> orders)
{
	RelayFrameBundle::Frame frame;
	std::uint64_t id = 0;
	for (const auto &[slot, player] : orders)
	{
		GameMessage message(1002, player);
		NetByteWriter writer;
		std::string error;
		REQUIRE(NetPacket::writeGameMessage(writer, message, &error));
		frame.orders.push_back({slot, ++id, writer.take()});
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE(RelayFrameBundle::encode(frame, bytes, error));
	return bytes;
}
} // namespace

TEST_CASE("network canonical: local orders and beats leave as OBCI, the committed bundle runs once")
{
	CanonTransport transport;
	Network network(twoPlayers(), transport);

	// the membership and one committed frame (the local slot's echo + the peer's order) ride the record stream
	std::vector<std::uint8_t> mem;
	{
		std::string error;
		REQUIRE(RelayMembership::encode(membership(0x0003), mem, error));
	}
	push(transport.records, record(1, 1, 1, RelayCommit::Kind::Membership, mem));
	push(transport.records, record(2, 1, 1, RelayCommit::Kind::Frame, framePayload({{0, 0}, {1, 1}})));

	CommandList pending;
	pending.append(GameMessage(1002, 0));
	network.update(0, pending);

	REQUIRE(transport.orders.size() == 1);
	CHECK(transport.orders[0].first == 1); // the dense per-slot order id
	REQUIRE(transport.beats.size() == 1);
	CHECK(transport.beats[0] == 1);        // the dense beat id
	CHECK(pending.messages().empty());     // everything pending went out

	// the frame's input arrived whole: ready, and relayed exactly once
	CHECK(network.isFrameReady(1));
	CommandList ran;
	network.relayCommands(1, ran);
	REQUIRE(ran.messages().size() == 2);   // the echo of the local order + the peer's order — not the local copy twice
	CHECK(ran.messages()[0].getPlayerIndex() == 0);
	CHECK(ran.messages()[1].getPlayerIndex() == 1);
	CHECK(network.errors().empty());
}

TEST_CASE("network canonical: an empty update emits only the beat; a departed slot yields its self-destruct")
{
	CanonTransport transport;
	Network network(twoPlayers(), transport);

	CommandList empty;
	network.update(0, empty);
	CHECK(transport.orders.empty());
	REQUIRE(transport.beats.size() == 1);
	CHECK(transport.beats[0] == 1);

	// slot 1 departs at frame 1's commit: the bundle reports it departed, the frame runs its self-destruct
	std::vector<std::uint8_t> mem;
	{
		std::string error;
		REQUIRE(RelayMembership::encode(membership(0x0001, 0x0002), mem, error));
	}
	push(transport.records, record(1, 1, 1, RelayCommit::Kind::Membership, mem));
	push(transport.records, record(2, 1, 1, RelayCommit::Kind::Frame, framePayload({{0, 0}})));
	CommandList pending;
	network.update(1, pending);

	CHECK(network.isFrameReady(1));
	CommandList ran;
	network.relayCommands(1, ran);
	REQUIRE(ran.messages().size() == 2);
	CHECK(ran.messages()[0].getType() == MSG_SELF_DESTRUCT); // the departed slot first
	CHECK(ran.messages()[0].getPlayerIndex() == 1);
	CHECK(ran.messages()[1].getPlayerIndex() == 0);
}

TEST_CASE("network canonical: a malformed record surfaces as an error, never a silent skip")
{
	CanonTransport transport;
	Network network(twoPlayers(), transport);
	transport.records.push_back({0x99, 0x88, 0x77});

	CommandList pending;
	network.update(0, pending);
	REQUIRE(network.errors().size() == 1);
	CHECK(network.errors()[0].find("canonical record decode") != std::string::npos);
	CHECK_FALSE(network.isFrameReady(1));
}
