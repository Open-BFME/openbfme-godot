#include "doctest.h"
#include "GameClient/GUI/PlacementTextStyle.h"

TEST_CASE("placement text: resolution scales the font, pointer gap and shadow")
{
	struct ExpectedStyle
	{
		int height, fontSize, pointerGap, shadowOffset;
	};
	const ExpectedStyle cases[] = {
		{ 480, 14, 20, 1 },
		{ 600, 14, 20, 1 },
		{ 720, 17, 24, 1 },
		{ 1080, 25, 36, 2 },
		{ 1440, 34, 48, 2 },
		{ 2160, 50, 72, 4 }
	};
	for (const ExpectedStyle &expected : cases)
	{
		CAPTURE(expected.height);
		const PlacementTextStyle style = PlacementTextStyle::forViewport(14, expected.height, 1.0f);
		CHECK(style.fontSize == expected.fontSize);
		CHECK(style.pointerGap == expected.pointerGap);
		CHECK(style.shadowOffset == expected.shadowOffset);
	}
}

TEST_CASE("placement text: configured font and UI scale combine with resolution")
{
	const PlacementTextStyle enlarged = PlacementTextStyle::forViewport(20, 1080, 1.5f);
	CHECK(enlarged.fontSize == 54);
	CHECK(enlarged.pointerGap == 54);
	CHECK(enlarged.shadowOffset == 3);

	const PlacementTextStyle reduced = PlacementTextStyle::forViewport(20, 2160, 0.75f);
	CHECK(reduced.fontSize == 54);
	CHECK(reduced.pointerGap == 54);
	CHECK(reduced.shadowOffset == 3);
}

TEST_CASE("placement text: missing font size retains the existing default")
{
	CHECK(PlacementTextStyle::forViewport(0, 1080, 1.0f).fontSize == 25);
	CHECK(PlacementTextStyle::forViewport(-1, 1080, 1.0f).fontSize == 25);
}
