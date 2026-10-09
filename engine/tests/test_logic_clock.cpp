// OpenBFME unit tests: 5 Hz fixed-step logic clock. GPL-3.0.

#include "doctest.h"

#include "Common/GameCommon.h"
#include "Common/LogicFrameClock.h"

#include <initializer_list>

TEST_CASE("BFME logic runs at 5 frames per second, not ZH's 30")
{
	CHECK(LOGICFRAMES_PER_SECOND == 5);
	CHECK(MSEC_PER_LOGICFRAME_REAL == doctest::Approx(200.0f));
}

TEST_CASE("one second of wall time is exactly 5 logic frames at any render rate")
{
	for (int fps : { 144, 60, 59, 30, 7, 5, 3, 1 })
	{
		CAPTURE(fps);
		LogicFrameClock clock(LOGICFRAMES_PER_SECOND);
		int total = 0;
		for (int i = 0; i < fps; ++i)
		{
			int due = clock.advance(1.0 / fps);
			REQUIRE(due >= 0);
			total += due;
			double alpha = clock.getAlpha();
			CHECK(alpha >= 0.0);
			CHECK(alpha < 1.0);
		}
		CHECK(total == 5);
		CHECK(clock.getFrame() == 5u);
		CHECK(clock.getAlpha() == doctest::Approx(0.0).epsilon(1e-6));
	}
}

TEST_CASE("144 fps for ten minutes stays exact")
{
	LogicFrameClock clock(LOGICFRAMES_PER_SECOND);
	long long total = 0;
	for (int i = 0; i < 144 * 600; ++i)
	{
		total += clock.advance(1.0 / 144.0);
	}
	CHECK(total == 5 * 600);
}

TEST_CASE("alpha is the elapsed fraction of the next frame")
{
	LogicFrameClock clock(LOGICFRAMES_PER_SECOND);
	CHECK(clock.advance(0.1) == 0);
	CHECK(clock.getAlpha() == doctest::Approx(0.5));
	CHECK(clock.advance(0.15) == 1);
	CHECK(clock.getAlpha() == doctest::Approx(0.25));
	CHECK(clock.advance(1.0) == 5); // a long hitch runs every due frame, none are dropped
	CHECK(clock.getFrame() == 6u);
}

TEST_CASE("negative time is rejected without moving the clock")
{
	LogicFrameClock clock(LOGICFRAMES_PER_SECOND);
	clock.advance(0.1);
	CHECK(clock.advance(-0.05) == -1);
	CHECK(clock.getAlpha() == doctest::Approx(0.5));
	CHECK(clock.getFrame() == 0u);
}
