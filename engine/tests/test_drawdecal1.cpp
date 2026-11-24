// OpenBFME. GPL-3.0.
//
// Lane DRAWDECAL-1: a drawable's alpha / additive decal shadow (GameClient/BlobShadow.h resolveDecal / decal): which template or model state has one,
// its style, texture, size, offset and opacity, the ShadowLOD gate (ShadowOverrideLODVisibility, RW 0x50BF7A) and the selection decal the HUD draws
// for it. Synthetic INI with the binary's own field names (ThingTemplate table RW 0xDA44E8); the retail case reads the crows' and a lamp's rows.
// The retail case SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"
#include "ObjectTestUtil.h"

#include "GameClient/BlobShadow.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/SelectionDecals.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

TEST_CASE("drawdecal1: an alpha / additive decal shadow takes its style, texture, size, offset and opacity; a blob or a volume is none")
{
	objtest::World w;
	REQUIRE(w.load("Object Eye\n Shadow = SHADOW_ADDITIVE_DECAL_DYNAMIC\n ShadowTexture = MUSauronEye\n ShadowSizeX = 400\n ShadowSizeY = 300\n"
	               " ShadowOffsetX = 10\n ShadowOverrideLODVisibility = Yes\n ShadowOpacityStart = 0\n ShadowOpacityFadeInTime = 1000\n ShadowOpacityPeak = 200\n"
	               " ShadowOpacityFadeOutTime = 500\n ShadowOpacityEnd = 10\nEnd\n"
	               "Object Lamp\n Shadow = SHADOW_ADDITIVE_DECAL\n ShadowTexture = LULamp1Shdw\n ShadowSizeX = 120\n ShadowSizeY = 120\n ShadowMaxHeight = 120\nEnd\n"
	               "Object Soldier\n Shadow = SHADOW_DECAL\n ShadowSizeX = 19\n ShadowSizeY = 21\nEnd\n"
	               "Object Barracks\n Shadow = SHADOW_VOLUME\nEnd\n")
	          .empty());
	BlobShadow::DecalSpec s;
	REQUIRE(BlobShadow::resolveDecal(w.get("Eye"), nullptr, nullptr, s));
	CHECK(s.type == 0x800); // SHADOW_ADDITIVE_DECAL_DYNAMIC
	CHECK(s.texture == "MUSauronEye.tga");
	CHECK(s.sizeX == 400.0f);
	CHECK(s.sizeY == 300.0f);
	CHECK(s.offsetX == 10.0f);
	CHECK(s.maxHeight == 20.0f); // the constructor's default (RW 0x740102 ..)
	CHECK(s.opacityPeak == 200u);
	CHECK(s.opacityStart == 0u);
	CHECK(s.fadeInMs == 1000.0f);
	CHECK(s.fadeOutMs == 500.0f);
	CHECK(s.opacityEnd == 10u);
	CHECK(s.overrideLOD);
	// ShadowOverrideLODVisibility keeps it at ShadowLOD Off / High / UltraHigh (RW 0x50BF7A); the lamp's goes with UseShadowDecals
	CHECK(BlobShadow::drawnAtLOD(s, false));
	REQUIRE(BlobShadow::resolveDecal(w.get("Lamp"), nullptr, nullptr, s));
	CHECK(s.type == 0x40); // SHADOW_ADDITIVE_DECAL
	CHECK(s.maxHeight == 120.0f);
	CHECK_FALSE(BlobShadow::drawnAtLOD(s, false));
	CHECK(BlobShadow::drawnAtLOD(s, true));
	CHECK(BlobShadow::belowMaxHeight(s, 100.0f, 0.0f));       // a lamp post's light under it
	CHECK_FALSE(BlobShadow::belowMaxHeight(s, 120.0f, 0.0f)); // ShadowMaxHeight or more above the ground: none (BFME2 rva0010C841)

	// a blob, a volume, a template without a shadow: none (the blob is BLOBSHADOW-1's)
	CHECK_FALSE(BlobShadow::resolveDecal(w.get("Soldier"), nullptr, nullptr, s));
	CHECK_FALSE(BlobShadow::resolveDecal(w.get("Barracks"), nullptr, nullptr, s));
	CHECK_FALSE(BlobShadow::resolveDecal(nullptr, nullptr, nullptr, s));

	// ShadowForceDisable: none
	W3DModelDrawModuleData module;
	module.m_shadowForceDisable = true;
	CHECK_FALSE(BlobShadow::resolveDecal(w.get("Eye"), &module, nullptr, s));

	// a model state's shadow decides over the template's: an alpha decal with its own Peak, MaxHeight and override
	ModelConditionInfo state;
	state.hasShadow = true;
	state.shadow.type = 1; // SHADOW_DECAL: the state names a blob, so no decal
	CHECK_FALSE(BlobShadow::resolveDecal(w.get("Eye"), nullptr, &state, s));
	state.shadow.type = 0x20; // SHADOW_ALPHA_DECAL
	state.shadow.texture = "decal_hero_good";
	state.shadow.sizeX = state.shadow.sizeY = 440.0f;
	state.shadow.maxHeight = 50.0f;
	state.shadowOpacityPeak = 0x80;
	REQUIRE(BlobShadow::resolveDecal(w.get("Lamp"), nullptr, &state, s));
	CHECK(s.type == 0x20);
	CHECK(s.texture == "decal_hero_good.tga");
	CHECK(s.maxHeight == 50.0f);
	CHECK(s.opacityPeak == 0x80u);
	CHECK_FALSE(s.overrideLOD);
}

TEST_CASE("drawdecal1: the decal is the placement ghost's: centre moved by the offsets along its axes, white at the opacity peak; none without a size")
{
	BlobShadow::DecalSpec s;
	s.type = 0x40;
	s.texture = "MUCatapultFP01.tga";
	s.sizeX = 150.0f;
	s.sizeY = 100.0f;
	s.offsetX = 10.0f;
	s.opacityPeak = 0x80;
	SelectionDecal d;
	REQUIRE(BlobShadow::decal(s, 500.0f, 600.0f, 30.0f, 0.0f, d));
	CHECK(d.style == 0x40u);
	CHECK(d.texture == "MUCatapultFP01.tga");
	CHECK(d.color == 0x80FFFFFFu); // DecalShadowColor: white, Peak's low byte as the alpha
	CHECK(d.size == 150.0f);
	CHECK(d.sizeY == 100.0f);
	CHECK(d.angle == 0.0f);
	CHECK(d.position.x == 510.0f); // ShadowOffsetX along u = (cos, sin)
	CHECK(d.position.y == 600.0f);
	CHECK(d.drawZ == 30.0f);
	CHECK_FALSE(d.terrainOnly); // the height test applies (only the claim decals skip it, RW 0x50C792)
	s.sizeY = 0.0f;
	CHECK_FALSE(BlobShadow::decal(s, 500.0f, 600.0f, 30.0f, 0.0f, d));
	s.sizeY = 100.0f;
	s.type = 0;
	CHECK_FALSE(BlobShadow::decal(s, 500.0f, 600.0f, 30.0f, 0.0f, d));
}

TEST_CASE("drawdecal1: a _DYNAMIC decal fades in from Start to Peak, and when it goes it fades from Peak to End and ends (RW 0x7323CE / 0x50B8AD / 0x508B40)")
{
	CHECK(BlobShadow::isDynamic(0x400));
	CHECK(BlobShadow::isDynamic(0x800));
	CHECK(BlobShadow::isDynamic(0x10000));
	CHECK_FALSE(BlobShadow::isDynamic(0x40));
	CHECK_FALSE(BlobShadow::isDynamic(0x20));

	// the crows' rows: Start 0, FadeInTime 1000, Peak 255, FadeOutTime 1000, End 0 (30 frames each way)
	BlobShadow::DecalSpec s;
	s.type = 0x800;
	s.opacityStart = 0;
	s.opacityPeak = 255;
	s.opacityEnd = 0;
	s.fadeInMs = 1000.0f;
	s.fadeOutMs = 1000.0f;
	const BlobShadow::DecalFade in = BlobShadow::fadeIn(s, 100);
	CHECK(in.t0 == 100u);
	CHECK(in.t1 == 130u); // 1000 ms * 0.03, truncated
	CHECK(in.fadeOutFrames == 30u);
	unsigned o = 999;
	REQUIRE(BlobShadow::opacityAt(in, 99, o));
	CHECK(o == 0u); // before t0
	REQUIRE(BlobShadow::opacityAt(in, 100, o));
	CHECK(o == 0u); // Start
	REQUIRE(BlobShadow::opacityAt(in, 115, o));
	CHECK(o == 128u); // 127.5 rounded up
	REQUIRE(BlobShadow::opacityAt(in, 130, o));
	CHECK(o == 255u); // Peak from t1 on, held without end
	REQUIRE(BlobShadow::opacityAt(in, 100000, o));
	CHECK(o == 255u);

	// released at 140: straight to Peak, then down to End over the 30 frames, and gone at 170
	const BlobShadow::DecalFade out = BlobShadow::fadeOut(in, 140);
	REQUIRE(BlobShadow::opacityAt(out, 140, o));
	CHECK(o == 255u);
	REQUIRE(BlobShadow::opacityAt(out, 155, o));
	CHECK(o == 128u); // 255 - 127.5, rounded up
	REQUIRE(BlobShadow::opacityAt(out, 169, o));
	CHECK(o == 9u); // 255 / 30 = 8.5, rounded up
	CHECK_FALSE(BlobShadow::opacityAt(out, 170, o));

	// no FadeInTime: Peak at once; no FadeOutTime: gone at once
	s.fadeInMs = 0.0f;
	s.fadeOutMs = 0.0f;
	const BlobShadow::DecalFade quick = BlobShadow::fadeIn(s, 10);
	REQUIRE(BlobShadow::opacityAt(quick, 10, o));
	CHECK(o == 255u);
	CHECK_FALSE(BlobShadow::opacityAt(BlobShadow::fadeOut(quick, 20), 20, o));
}

namespace
{
// the first model draw of the object's drawable that names a decal shadow
bool decalOf(hudtest::Rig &rig, const Object &o, BlobShadow::DecalSpec &spec)
{
	const Drawable *d = rig.game->drawables().findByObject(o.getID());
	REQUIRE(d != nullptr);
	for (const DrawEntry &e : d->entries())
	{
		const auto *module = e.kind == W3D_DRAWKIND_MODEL && e.draw ? dynamic_cast<const W3DModelDrawModuleData *>(e.data) : nullptr;
		if (module && BlobShadow::resolveDecal(d->getTemplate(), module, e.draw->currentModelState(), spec))
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("drawdecal1 retail: the crows carry the Eye as an additive decal at every ShadowLOD; a lamp's light only at Low / Medium; a soldier has none")
{
	if (!hudtest::haveWorld("drawdecal1"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	const Coord3D c = rig.freeSpot(2800, 1400, 200.0f);
	Object *crows = rig.make("IsengardMurderOfCrows", c.x, c.y);
	Object *lamp = rig.make("LothlorienLamp1", c.x + 80.0f, c.y);
	Object *soldier = rig.make("GondorFighter", c.x + 160.0f, c.y);
	for (int i = 0; i < 5; ++i)
	{
		rig.game->advance(0.033);
	}
	BlobShadow::DecalSpec spec;
	REQUIRE(decalOf(rig, *crows, spec));
	CHECK(spec.type == 0x800);
	CHECK(spec.texture == "MUSauronEye.tga");
	CHECK(spec.sizeX == 400.0f);
	CHECK(spec.overrideLOD);
	REQUIRE(decalOf(rig, *lamp, spec));
	CHECK(spec.type == 0x40);
	CHECK(spec.texture == "LULamp1Shdw.tga");
	CHECK_FALSE(spec.overrideLOD);
	CHECK_FALSE(decalOf(rig, *soldier, spec));
}
