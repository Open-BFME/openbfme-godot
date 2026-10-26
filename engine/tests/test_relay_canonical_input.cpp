// OpenBFME unit tests. GPL-3.0.

#include "doctest.h"

#include "GameNetwork/RelayCanonicalInput.h"
#include "GameNetwork/RelayFrameBundle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
RelayCanonicalInput::Message order(std::uint64_t id, std::size_t size)
{
	RelayCanonicalInput::Message message;
	message.kind = RelayCanonicalInput::Kind::Order;
	message.id = id;
	message.bytes.assign(size, 0x41);
	return message;
}

RelayCanonicalInput::Message beat(std::uint64_t id)
{
	RelayCanonicalInput::Message message;
	message.kind = RelayCanonicalInput::Kind::Beat;
	message.id = id;
	return message;
}
}

TEST_CASE("canonical input: order and beat round trip")
{
	std::vector<std::uint8_t> wire;
	std::string error;
	RelayCanonicalInput::Message decoded;

	CHECK(RelayCanonicalInput::encode(order(7, 300), wire, error));
	CHECK(wire.size() == RelayCanonicalInput::kHeaderBytes + 300);
	CHECK(RelayCanonicalInput::decode(wire, decoded, error));
	CHECK(decoded.kind == RelayCanonicalInput::Kind::Order);
	CHECK(decoded.id == 7);
	CHECK(decoded.bytes.size() == 300);
	CHECK(decoded.bytes[0] == 0x41);

	CHECK(RelayCanonicalInput::encode(beat(9), wire, error));
	CHECK(wire.size() == RelayCanonicalInput::kHeaderBytes);
	CHECK(RelayCanonicalInput::decode(wire, decoded, error));
	CHECK(decoded.kind == RelayCanonicalInput::Kind::Beat);
	CHECK(decoded.id == 9);
	CHECK(decoded.bytes.empty());
}

TEST_CASE("canonical input: encode rejects zero ids, beat payloads and oversized orders")
{
	std::vector<std::uint8_t> wire;
	std::string error;

	CHECK_FALSE(RelayCanonicalInput::encode(order(0, 4), wire, error));
	CHECK_FALSE(RelayCanonicalInput::encode(beat(0), wire, error));

	auto badBeat = beat(1);
	badBeat.bytes.push_back(1);
	CHECK_FALSE(RelayCanonicalInput::encode(badBeat, wire, error));

	auto oversized = order(1, RelayFrameBundle::kMaxOrderBytes + 1);
	CHECK_FALSE(RelayCanonicalInput::encode(oversized, wire, error));

	auto atLimit = order(1, RelayFrameBundle::kMaxOrderBytes);
	CHECK(RelayCanonicalInput::encode(atLimit, wire, error));
	CHECK(wire.size() == RelayCanonicalInput::kMaxWireBytes);
}

TEST_CASE("canonical input: decode rejects malformed headers")
{
	std::vector<std::uint8_t> wire;
	std::string error;
	RelayCanonicalInput::Message decoded;

	CHECK(RelayCanonicalInput::encode(order(1, 8), wire, error));

	auto shortWire = std::vector<std::uint8_t>(wire.begin(), wire.begin() + RelayCanonicalInput::kHeaderBytes - 1);
	CHECK_FALSE(RelayCanonicalInput::decode(shortWire, decoded, error));

	auto badMagic = wire;
	badMagic[0] = 'X';
	CHECK_FALSE(RelayCanonicalInput::decode(badMagic, decoded, error));

	auto badVersion = wire;
	badVersion[4] = 2;
	CHECK_FALSE(RelayCanonicalInput::decode(badVersion, decoded, error));

	auto badKind = wire;
	badKind[5] = 0;
	CHECK_FALSE(RelayCanonicalInput::decode(badKind, decoded, error));

	auto zeroId = wire;
	for (unsigned byte = 6; byte < 14; ++byte)
		zeroId[byte] = 0;
	CHECK_FALSE(RelayCanonicalInput::decode(zeroId, decoded, error));
}

TEST_CASE("canonical input: declared order size enforced before any copy")
{
	std::vector<std::uint8_t> wire;
	std::string error;
	RelayCanonicalInput::Message decoded;

	CHECK(RelayCanonicalInput::encode(order(1, 8), wire, error));

	// A declared size beyond the 64 KiB budget fails on the header alone.
	auto overBudget = wire;
	overBudget[14] = 0xff;
	overBudget[15] = 0xff;
	overBudget[16] = 0xff;
	overBudget[17] = 0xff;
	CHECK_FALSE(RelayCanonicalInput::decode(overBudget, decoded, error));
	CHECK(error.find("exceeds") != std::string::npos);

	// A declared size that disagrees with the message length fails too
	// (trailing bytes are part of the same check).
	auto trailing = wire;
	trailing.push_back(0);
	CHECK_FALSE(RelayCanonicalInput::decode(trailing, decoded, error));

	auto truncated = std::vector<std::uint8_t>(wire.begin(), wire.end() - 1);
	CHECK_FALSE(RelayCanonicalInput::decode(truncated, decoded, error));

	// A beat declaring bytes is malformed on the wire.
	auto badBeat = wire;
	badBeat[5] = (std::uint8_t)RelayCanonicalInput::Kind::Beat;
	CHECK_FALSE(RelayCanonicalInput::decode(badBeat, decoded, error));
}
