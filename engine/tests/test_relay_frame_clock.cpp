// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"
#include "GameNetwork/RelayFrameClock.h"

TEST_CASE("NET-SYNC: first relay boundary has no safety frame")
{
	RelayFrameClock clock(1000, 0);
	std::string error;
	std::uint32_t frame = 99;
	std::uint64_t boundary = 0;
	for (std::uint64_t phase = 0; phase < 200; ++phase)
	{
		REQUIRE(clock.frameAfter(1000 + phase, frame, error));
		CHECK(frame == 0);
		REQUIRE(clock.boundaryOf(frame, boundary, error));
		CHECK(boundary == 1200);
		// tolerance: receipts must remain strictly before their batch boundary.
		CHECK(boundary > 1000 + phase);
		// tolerance: the first batch is exactly one 200 ms window.
		CHECK(boundary - (1000 + phase) <= 200);
	}
	REQUIRE(clock.frameAfter(1200, frame, error));
	CHECK(frame == 1);
	REQUIRE(clock.frameAfter(1201, frame, error));
	CHECK(frame == 1);
	REQUIRE(clock.frameAfter(1399, frame, error));
	CHECK(frame == 1);
	REQUIRE(clock.frameAfter(1400, frame, error));
	CHECK(frame == 2);
}

TEST_CASE("ROUTER-1: the frame period comes from the game's START")
{
	// A 20 Hz game (50 ms periods): same boundary arithmetic, per-game clock.
	RelayFrameClock clock(1000, 0, 50);
	std::string error;
	std::uint32_t frame = 99;
	std::uint64_t boundary = 0;
	for (std::uint64_t phase = 0; phase < 50; ++phase)
	{
		REQUIRE(clock.frameAfter(1000 + phase, frame, error));
		CHECK(frame == 0);
		REQUIRE(clock.boundaryOf(frame, boundary, error));
		CHECK(boundary == 1050);
	}
	REQUIRE(clock.frameAfter(1050, frame, error));
	CHECK(frame == 1);
	REQUIRE(clock.frameAfter(1099, frame, error));
	CHECK(frame == 1);
	REQUIRE(clock.frameAfter(1100, frame, error));
	CHECK(frame == 2);
}

TEST_CASE("NET-SYNC: resumed boundary uses the committed origin and frame")
{
	RelayFrameClock clock(9000, 41);
	std::string error;
	std::uint32_t frame = 99;
	std::uint64_t boundary = 0;
	CHECK_FALSE(clock.frameAfter(8999, frame, error));
	CHECK(frame == 99);
	CHECK_FALSE(error.empty());
	REQUIRE(clock.frameAfter(9199, frame, error));
	CHECK(frame == 41);
	CHECK(error.empty());
	REQUIRE(clock.boundaryOf(frame, boundary, error));
	CHECK(boundary == 9200);
	REQUIRE(clock.frameAfter(9200, frame, error));
	CHECK(frame == 42);
	CHECK_FALSE(clock.boundaryOf(40, boundary, error));
	CHECK(boundary == 9200);
}

TEST_CASE("NET-SYNC: boundary and frame exhaustion refuse without wrapping")
{
	std::string error;
	std::uint32_t frame = 7;
	std::uint64_t boundary = 9;
	RelayFrameClock lastFrame(0, std::numeric_limits<std::uint32_t>::max());
	REQUIRE(lastFrame.frameAfter(199, frame, error));
	CHECK(frame == std::numeric_limits<std::uint32_t>::max());
	CHECK_FALSE(lastFrame.frameAfter(200, frame, error));
	CHECK(frame == std::numeric_limits<std::uint32_t>::max());
	RelayFrameClock lastTime(std::numeric_limits<std::uint64_t>::max() - 199, 0);
	CHECK_FALSE(lastTime.frameAfter(std::numeric_limits<std::uint64_t>::max(), frame, error));
	CHECK_FALSE(lastTime.boundaryOf(0, boundary, error));
	CHECK(boundary == 9);
}
