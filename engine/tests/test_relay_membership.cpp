// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"
#include "GameNetwork/RelayMembership.h"

namespace
{
RelayMembership::Snapshot running()
{
	RelayMembership::Snapshot s;
	s.slotMask = s.active = s.loaded = 0x8001;
	s.clockValid = true;
	s.originMs = 1000;
	s.firstFrame = 4;
	return s;
}

RelayCommit::Record membership(const RelayMembership::Snapshot &s, std::uint64_t sequence, std::uint32_t frame, std::uint64_t generation)
{
	RelayCommit::Record r;
	r.kind = RelayCommit::Kind::Membership;
	r.stamp = { 1, 2, sequence, frame, generation };
	std::string error;
	REQUIRE(RelayMembership::encode(s, r.payload, error));
	return r;
}

bool frameValid(const RelayCommit::Record &r, const RelayMembership::Snapshot &, std::string &)
{
	return r.payload == std::vector<std::uint8_t>{ 42 };
}
}

TEST_CASE("canonical membership snapshot is strict and keeps failed decode output")
{
	auto s = running();
	s.ticketGenerations[15] = 0x1234;
	std::string error;
	std::vector<std::uint8_t> bytes;
	REQUIRE(RelayMembership::encode(s, bytes, error));
	REQUIRE(bytes.size() == 62);
	CHECK(bytes[6] == 2);
	CHECK(bytes[8] == 1);
	CHECK(bytes[9] == 0x80);
	CHECK(bytes[48] == 0x34);
	CHECK(bytes[49] == 0x12);
	RelayMembership::Snapshot decoded;
	REQUIRE(RelayMembership::decode(bytes, decoded, error));
	CHECK(decoded.ticketGenerations == s.ticketGenerations);
	CHECK(decoded.originMs == 1000);
	CHECK(decoded.firstFrame == 4);
	for (size_t offset : { 4u, 6u, 7u })
	{
		auto malformed = bytes;
		malformed[offset] = 0xff;
		CHECK_FALSE(RelayMembership::decode(malformed, decoded, error));
		CHECK(decoded.ticketGenerations == s.ticketGenerations);
	}
	bytes.pop_back();
	CHECK_FALSE(RelayMembership::decode(bytes, decoded, error));
	s.held = 1;
	CHECK_FALSE(RelayMembership::encode(s, bytes, error));
	s = running();
	s.paused = true;
	CHECK_FALSE(RelayMembership::encode(s, bytes, error));
}

TEST_CASE("canonical cursor stages lifecycle before fully validated frame without client veto")
{
	RelayMembership::Cursor cursor(1, 2, 0x8001, 4);
	std::string error;
	auto s = running();
	REQUIRE(cursor.accept(membership(s, 1, 4, 1), frameValid, error));
	CHECK_FALSE(cursor.hasAppliedFrame());
	s.active = s.loaded = 1;
	s.departed = 0x8000;
	REQUIRE(cursor.accept(membership(s, 2, 4, 2), frameValid, error));
	RelayCommit::Record frame;
	frame.stamp = { 1, 2, 3, 4, 2 };
	frame.kind = RelayCommit::Kind::Frame;
	frame.payload = { 0xff };
	CHECK_FALSE(cursor.accept(frame, frameValid, error));
	CHECK(cursor.stamp().sequence == 2);
	CHECK(cursor.staged().departed == 0x8000);
	CHECK(cursor.applied().departed == 0);
	CHECK(cursor.nextFrame() == 4);
	CHECK(cursor.departedAtFrame() == 0);
	frame.payload = { 42 };
	REQUIRE(cursor.accept(frame, frameValid, error));
	CHECK(cursor.applied().departed == 0x8000);
	CHECK(cursor.departedAtFrame() == 0x8000);
	CHECK_FALSE(cursor.accept(frame, frameValid, error));
	CHECK_FALSE(cursor.accept(membership(s, 4, 4, 3), frameValid, error));
	frame.stamp.sequence = 4;
	frame.stamp.frame = 5;
	REQUIRE(cursor.accept(frame, frameValid, error));
	CHECK(cursor.departedAtFrame() == 0);
	CHECK(cursor.nextFrame() == 6);
	CHECK_FALSE(cursor.accept(membership(running(), 5, 6, 3), frameValid, error));
}

TEST_CASE("canonical cursor refuses loading frames and freezes clock until committed resume")
{
	RelayMembership::Cursor cursor(1, 2, 0x8001, 4);
	std::string error;
	auto s = running();
	s.clockValid = false;
	s.loaded = 0;
	s.originMs = 0;
	s.firstFrame = 0;
	REQUIRE(cursor.accept(membership(s, 1, 4, 1), frameValid, error));
	RelayCommit::Record frame;
	frame.stamp = { 1, 2, 2, 4, 1 };
	frame.payload = { 42 };
	CHECK_FALSE(cursor.accept(frame, frameValid, error));
	s = running();
	REQUIRE(cursor.accept(membership(s, 2, 4, 2), frameValid, error));
	s.originMs = 1200;
	CHECK_FALSE(cursor.accept(membership(s, 3, 4, 3), frameValid, error));
	s.clockValid = false;
	s.paused = true;
	s.originMs = 0;
	s.firstFrame = 0;
	REQUIRE(cursor.accept(membership(s, 3, 4, 3), frameValid, error));
	s = running();
	s.originMs = 1200;
	REQUIRE(cursor.accept(membership(s, 4, 4, 4), frameValid, error));
	frame.stamp.sequence = 5;
	frame.stamp.membershipGeneration = 4;
	REQUIRE(cursor.accept(frame, frameValid, error));
	CHECK(cursor.applied().originMs == 1200);
	RelayCommit::Record end;
	end.stamp = { 1, 2, 6, 5, 4 };
	end.kind = RelayCommit::Kind::End;
	REQUIRE(cursor.accept(end, frameValid, error));
	CHECK(cursor.ended());
	CHECK_FALSE(cursor.accept(end, frameValid, error));
}
