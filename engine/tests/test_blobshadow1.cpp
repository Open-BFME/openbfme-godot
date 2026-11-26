// OpenBFME. GPL-3.0.
//
// Lane BLOBSHADOW-1: retail's SHADOW_DECAL blob shadows as data (GameClient/BlobShadow.h): which template or model state has one, its texture,
// size, offset and the ShadowLOD gate, and the ground quad it covers. Synthetic INI with the binary's own field names (ThingTemplate table
// RW 0xDA44E8).

#include "doctest.h"
#include "ObjectTestUtil.h"

#include "GameClient/BlobShadow.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <cmath>

using namespace objtest;

TEST_CASE("blobshadow1: a SHADOW_DECAL template's blob takes its texture, size and offset; others have none (RW 0x49A2E0 / 0x50BF6A)")
{
	World w;
	REQUIRE(w.load("Object Soldier\n Shadow = SHADOW_DECAL\n ShadowTexture = ShadowI\n ShadowSizeX = 19\n ShadowSizeY = 21\n ShadowOffsetX = -4\nEnd\n"
	               "Object Tree\n Shadow = SHADOW_DECAL\nEnd\n"
	               "Object Barracks\n Shadow = SHADOW_VOLUME\nEnd\n"
	               "Object Either\n Shadow = SHADOW_VOLUME_OR_DECAL\nEnd\n"
	               "Object Plain\nEnd\n")
	          .empty());
	BlobShadow::Spec s;
	REQUIRE(BlobShadow::resolve(w.get("Soldier"), nullptr, nullptr, false, s));
	CHECK(s.texture == "ShadowI.tga"); // RW 0x50B690 appends ".tga"
	CHECK(s.sizeX == 19.0f);
	CHECK(s.sizeY == 21.0f);
	CHECK(s.offsetX == -4.0f);
	CHECK(s.offsetY == 0.0f);
	CHECK(s.maxHeight == 20.0f); // the constructor's default (RW 0x740102 ..)
	CHECK(s.rotating);           // ShadowIsRotatingWithObject defaults to Yes
	CHECK_FALSE(s.overrideLOD);

	// no ShadowTexture: "shadow" (RW 0x50BFA6); no size: the bounding box decides later
	REQUIRE(BlobShadow::resolve(w.get("Tree"), nullptr, nullptr, false, s));
	CHECK(s.texture == "shadow.tga");
	CHECK(s.sizeX == 0.0f);

	// a volume is no blob; an OR_DECAL form is a blob only while volumes are off (GlobalData + 0x60)
	CHECK_FALSE(BlobShadow::resolve(w.get("Barracks"), nullptr, nullptr, false, s));
	CHECK(BlobShadow::resolve(w.get("Either"), nullptr, nullptr, false, s));
	CHECK_FALSE(BlobShadow::resolve(w.get("Either"), nullptr, nullptr, true, s));
	CHECK_FALSE(BlobShadow::resolve(w.get("Plain"), nullptr, nullptr, false, s));
	CHECK_FALSE(BlobShadow::resolve(nullptr, nullptr, nullptr, false, s));

	// ShadowForceDisable: none
	W3DModelDrawModuleData module;
	module.m_shadowForceDisable = true;
	CHECK_FALSE(BlobShadow::resolve(w.get("Soldier"), &module, nullptr, false, s));

	// a model state's shadow decides over the template's
	ModelConditionInfo state;
	state.hasShadow = true;
	state.shadow.type = 2; // SHADOW_VOLUME
	CHECK_FALSE(BlobShadow::resolve(w.get("Soldier"), nullptr, &state, false, s));
	state.shadow.type = 1;
	state.shadow.texture = "ShadowSpider2";
	state.shadow.sizeX = 22.0f;
	state.shadow.sizeY = 18.0f;
	state.shadow.maxHeight = 35.0f;
	REQUIRE(BlobShadow::resolve(w.get("Barracks"), nullptr, &state, false, s));
	CHECK(s.texture == "ShadowSpider2.tga");
	CHECK(s.sizeX == 22.0f);
	CHECK(s.maxHeight == 35.0f);
}

TEST_CASE("blobshadow1: the ShadowLOD gate, the height limit and the ground quad (RW 0x50BF7A, BFME2 rva0010C841, RW 0x50C07E)")
{
	BlobShadow::Spec s;
	s.texture = "ShadowI.tga";
	s.sizeX = 20.0f;
	s.sizeY = 10.0f;
	// UseShadowDecals: ShadowLOD Low / Medium; ShadowOverrideLODVisibility keeps it at the others
	CHECK(BlobShadow::drawnAtLOD(s, true));
	CHECK_FALSE(BlobShadow::drawnAtLOD(s, false));
	s.overrideLOD = true;
	CHECK(BlobShadow::drawnAtLOD(s, false));
	s.overrideLOD = false;
	// ShadowMaxHeight 20 above the ground: no blob
	CHECK(BlobShadow::belowMaxHeight(s, 119.0f, 100.0f));
	CHECK_FALSE(BlobShadow::belowMaxHeight(s, 120.0f, 100.0f));

	// facing 0: u along +x (half 10), v along -y (half 5)
	BlobShadow::Quad q;
	REQUIRE(BlobShadow::quad(s, 100.0f, 200.0f, 0.0f, 0.0f, 0.0f, q));
	CHECK(q.cx == 100.0f);
	CHECK(q.cy == 200.0f);
	CHECK(q.ux == 10.0f);
	CHECK(q.uy == 0.0f);
	CHECK(q.vx == 0.0f);
	CHECK(q.vy == -5.0f);

	// facing a quarter turn: u along +y; ShadowOffsetX moves the centre along u (DecalShadowCentre)
	s.offsetX = -4.0f;
	const float quarter = 1.5707964f;
	REQUIRE(BlobShadow::quad(s, 100.0f, 200.0f, quarter, 0.0f, 0.0f, q));
	CHECK(q.cx == doctest::Approx(100.0f).epsilon(1e-6)); // tolerance: cos of a float quarter turn is 4e-8, not 0
	CHECK(q.cy == doctest::Approx(196.0f).epsilon(1e-6)); // tolerance: as above
	CHECK(q.uy == doctest::Approx(10.0f).epsilon(1e-6));  // tolerance: as above

	// not rotating with the object: the world axes whatever the facing
	s.rotating = false;
	REQUIRE(BlobShadow::quad(s, 100.0f, 200.0f, quarter, 0.0f, 0.0f, q));
	CHECK(q.ux == 10.0f);
	CHECK(q.uy == 0.0f);
	CHECK(q.cx == 96.0f);

	// a zero size takes twice the bounding box extent, Y negated (RW 0x50BFE8 .. 0x50C06F); no extent: nothing drawn
	BlobShadow::Spec t;
	t.rotating = false;
	REQUIRE(BlobShadow::quad(t, 0.0f, 0.0f, 0.0f, 6.0f, 4.0f, q));
	CHECK(q.ux == 6.0f);
	CHECK(q.vy == 4.0f); // -0.5 * (-2 * 4)
	CHECK_FALSE(BlobShadow::quad(t, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, q));
}
