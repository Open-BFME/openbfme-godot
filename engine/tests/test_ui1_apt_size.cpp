// OpenBFME unit tests. GPL-3.0.
// Lane UI-1: the clip size setters _width / _height as RotWK's RW 0xB03197 / 0xB03294 (see AptCharacterInst::setProperty): a rotated clip's
// self-assignment keeps its size, a zero request leaves the minimum scale 1.132257342338562 % and a later value recovers from it.

#include "doctest.h"
#include "AptPlayerTestUtil.h"

#include <cmath>

using namespace apttest;

namespace
{
float prop(AptCharacterInst *c, const char *name)
{
	AptValue v;
	REQUIRE(c->getMember(name, v));
	return v.toNumber();
}
} // namespace

TEST_CASE("ui1 apt: _width / _height follow RW 0xB03197 / 0xB03294 (rotated self-assignment, the zero request and its recovery)")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 100, 50, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	AptCharacterInst *s = fx.at("s");
	REQUIRE(s != nullptr);
	CHECK(prop(s, "_width") == doctest::Approx(100.0f));
	// 45 degrees: the parent-space extent (100 + 50) / sqrt(2) = 106.066; `_width = _width` keeps it (the rotated branch: old + 100 * (v / current - 1))
	s->setMember("_rotation", AptValue::number(45.0f));
	const float w = prop(s, "_width");
	CHECK(w == doctest::Approx(106.066017f).epsilon(1e-4));
	s->setMember("_width", AptValue::number(w));
	CHECK(prop(s, "_width") == doctest::Approx(106.066017f).epsilon(1e-4));
	CHECK(prop(s, "_xscale") == doctest::Approx(100.0f).epsilon(1e-4));
	CHECK(prop(s, "_rotation") == doctest::Approx(45.0f).epsilon(1e-4));
	// unrotated: 0 is 1e-4, the scale stops at 1.132257342338562 %, and a later width is reached from there (100 * diagonal * v / current)
	s->setMember("_rotation", AptValue::number(0.0f));
	s->setMember("_xscale", AptValue::number(100.0f));
	s->setMember("_width", AptValue::number(0.0f));
	CHECK(prop(s, "_xscale") == doctest::Approx(1.132257342338562f).epsilon(1e-6));
	CHECK(prop(s, "_width") == doctest::Approx(1.132257342338562f).epsilon(1e-4));
	s->setMember("_width", AptValue::number(42.0f));
	CHECK(prop(s, "_width") == doctest::Approx(42.0f).epsilon(1e-4));
	CHECK(prop(s, "_xscale") == doctest::Approx(42.0f).epsilon(1e-4));
	// _height the same way; a negative value changes nothing
	s->setMember("_height", AptValue::number(25.0f));
	CHECK(prop(s, "_yscale") == doctest::Approx(50.0f).epsilon(1e-4));
	s->setMember("_height", AptValue::number(-5.0f));
	CHECK(prop(s, "_height") == doctest::Approx(25.0f).epsilon(1e-4));
}

TEST_CASE("ui1 apt: the scale / rotation record of reflected and sheared clips (RW 0xAF4890) under _width / _height self-assignment")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 100, 50, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	AptCharacterInst *s = fx.at("s");
	REQUIRE(s != nullptr);
	const float h = std::sqrt(0.5f);
	// a 45-degree clip reflected in its Y axis: the record is rotation -135, _xscale -100, _yscale 100 (the axes' wrapped half-difference)
	AptMatrix reflected;
	reflected.a = h;
	reflected.b = h;
	reflected.c = h;
	reflected.d = -h;
	const AptScaleRotation r = AptDecomposeMatrix(reflected);
	CHECK(r.rotationDegrees == doctest::Approx(-135.0f).epsilon(1e-4));
	CHECK(r.xscale == doctest::Approx(-100.0f).epsilon(1e-4));
	CHECK(r.yscale == doctest::Approx(100.0f).epsilon(1e-4));
	// `_width = _width` keeps the Y axis (c, d); the X scale, -100 + 0, stops at the minimum 1.1322 % (RW 0xD05820) and keeps the record's rotation
	s->matrix = reflected;
	s->setMember("_width", AptValue::number(prop(s, "_width")));
	CHECK(s->matrix.c == doctest::Approx(h).epsilon(1e-5));
	CHECK(s->matrix.d == doctest::Approx(-h).epsilon(1e-5));
	CHECK(s->matrix.a == doctest::Approx(-0.0080062688f).epsilon(1e-3));
	CHECK(s->matrix.b == doctest::Approx(-0.0080062681f).epsilon(1e-3));
	// a sheared clip (c = 0.5): the record is rotation -13.2825, both scales 102.7486; `_height = _height` rebuilds it without the shear
	AptMatrix sheared;
	sheared.a = 1.0f;
	sheared.b = 0.0f;
	sheared.c = 0.5f;
	sheared.d = 1.0f;
	const AptScaleRotation q = AptDecomposeMatrix(sheared);
	CHECK(q.rotationDegrees == doctest::Approx(-13.2825256f).epsilon(1e-4));
	CHECK(q.xscale == doctest::Approx(102.748630f).epsilon(1e-4));
	CHECK(q.yscale == doctest::Approx(102.748630f).epsilon(1e-4));
	s->matrix = sheared;
	s->setMember("_height", AptValue::number(prop(s, "_height")));
	CHECK(s->matrix.a == doctest::Approx(1.0f).epsilon(1e-4));
	CHECK(s->matrix.b == doctest::Approx(-0.236067977f).epsilon(1e-4));
	CHECK(s->matrix.c == doctest::Approx(0.236067977f).epsilon(1e-4));
	CHECK(s->matrix.d == doctest::Approx(1.0f).epsilon(1e-4));
	// a clip mirrored in X: atan2(0, -1) wraps to 0 (fmod by the float pi), no rotation, a signed X scale -100
	AptMatrix mirrored;
	mirrored.a = -1.0f;
	mirrored.d = 1.0f;
	const AptScaleRotation u = AptDecomposeMatrix(mirrored);
	CHECK(std::fabs(u.rotationDegrees) < 1e-3f);
	CHECK(u.xscale == doctest::Approx(-100.0f).epsilon(1e-4));
	CHECK(u.yscale == doctest::Approx(100.0f).epsilon(1e-4));
}

TEST_CASE("ui1 apt: the transform properties report their unverified gaps (stop S-1262)")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 100, 50, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	AptCharacterInst *s = fx.at("s");
	REQUIRE(s != nullptr);
	auto count = [&](const std::string &detail) {
		std::size_t n = 0;
		for (const AptNote &note : fx.apt->notes())
		{
			n += note.kind == "apt-transform-unverified" && note.detail == detail ? 1u : 0u;
		}
		return n;
	};
	CHECK(count("_xscale [S-1262]") == 0);
	prop(s, "_xscale");
	prop(s, "_yscale");
	prop(s, "_rotation");
	CHECK(count("_xscale [S-1262]") == 1);
	CHECK(count("_yscale [S-1262]") == 1);
	CHECK(count("_rotation [S-1262]") == 1);
	// once per clip instance and property: repeated reads (a HUD movie reads them every frame) add nothing
	for (int i = 0; i < 50; ++i)
	{
		prop(s, "_rotation");
		prop(s, "_xscale");
	}
	CHECK(count("_rotation [S-1262]") == 1);
	CHECK(count("_xscale [S-1262]") == 1);
	// the size setters report too (and the width read inside them is not a getter of the three)
	s->setMember("_width", AptValue::number(50.0f));
	s->setMember("_height", AptValue::number(25.0f));
	s->setMember("_width", AptValue::number(60.0f));
	s->setMember("_height", AptValue::number(30.0f));
	CHECK(count("_width [S-1262]") == 1);
	CHECK(count("_height [S-1262]") == 1);
	// a mirrored clip: the getter reads +100 where the record says -100 (the disclosed gap)
	s->matrix.a = -1.0f;
	s->matrix.b = 0.0f;
	CHECK(prop(s, "_xscale") == doctest::Approx(100.0f));
	CHECK(AptDecomposeMatrix(s->matrix).xscale == doctest::Approx(-100.0f).epsilon(1e-4));
}

