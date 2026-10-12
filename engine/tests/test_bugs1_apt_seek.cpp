// OpenBFME unit tests for lane BUGS-1: a seek back keeps the matrix of a clip a script changed (AptDisplayList::mergeState tests the AS changed flag,
// RW 0xB0D610 = BFME2 0xAF9410), and only an applied transform assignment sets the flag (an ignored _width / _height does not).
#include "doctest.h"
#include "AptPlayerTestUtil.h"

using namespace apttest;

namespace
{
struct SeekFx
{
	TestMovie m;
	PlayerFx fx;
	AptCharacterInst *s = nullptr;
	SeekFx()
	{
		std::uint32_t shape = m.addShape(0, 0, 100, 50, 1);
		m.addCharacter(0);
		std::uint32_t shapeId = m.addCharacter(shape);
		TestMovie::Place p = placeChar(shapeId, 1, "s");
		p.flags |= APT_PLACE_HASMATRIX;
		p.translation[0] = 10.0f;
		p.translation[1] = -80.0f;
		// three frames: the clip placed at frame 0 with y -80, then two empty frames
		m.setRootFrames({ { m.addPlaceItem(p) }, {}, {} });
		REQUIRE(fx.load(0, "A", m));
		fx.step();
		fx.root()->playing = false;
		s = fx.at("s");
		REQUIRE(s != nullptr);
	}
	float y()
	{
		AptValue v;
		REQUIRE(s->getMember("_y", v));
		return v.toNumber();
	}
};
} // namespace

TEST_CASE("bugs1 apt: a seek back re-applies the placement's matrix only to a clip no script changed (RW 0xB0D610 GetASChanged)")
{
	{
		SeekFx f;
		f.fx.root()->gotoFrame(2);
		f.fx.root()->gotoFrame(0); // backward: the commands are rebuilt from frame 0
		CHECK(f.y() == doctest::Approx(-80.0f));
		CHECK_FALSE(f.s->asChanged);
	}
	{
		SeekFx f;
		f.s->setMember("_y", AptValue::number(-90.0f));
		CHECK(f.s->asChanged);
		f.fx.root()->gotoFrame(2);
		f.fx.root()->gotoFrame(0);
		CHECK(f.y() == doctest::Approx(-90.0f)); // InGameHelpBox.apt's box eased `top` this way; the seek used to put it back at frame 3's y
	}
}

TEST_CASE("bugs1 apt: an ignored _width / _height assignment leaves the AS changed flag clear; an applied one sets it")
{
	SeekFx f;
	f.s->setMember("_width", AptValue::number(-5.0f)); // RW 0xB031AD: below 0, nothing changes
	f.s->setMember("_height", AptValue::number(-1.0f));
	CHECK_FALSE(f.s->asChanged);
	f.fx.root()->gotoFrame(2);
	f.fx.root()->gotoFrame(0);
	CHECK(f.y() == doctest::Approx(-80.0f));
	f.s->setMember("_width", AptValue::number(50.0f));
	CHECK(f.s->asChanged);
}
