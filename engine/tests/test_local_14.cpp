// OpenBFME unit tests: LOCAL-14 object decal shadows.
//
// RotWK addresses:
//   W3DModelDraw::allocateShadows: RW 0x4B931E (called on model state changes across 7 draw classes)
//   W3DScriptedModelDraw::setModelState: RW 0x4C4EE0..0x4C5175
//     Branch A: RW 0x4C4EE0 (condition shadow precedence, isRotatingWithObject = true at RW 0x4C4F50)
//     Branch B: RW 0x4C4FD6 (ThingTemplate shadow: Shadow, ShadowTexture, ShadowSizeX/Y, ShadowOffsetX/Y,
//       ShadowSunAngle, ShadowMaxHeight, ShadowOverrideLODVisibility, ShadowUseHouseColor, ShadowIsRotatingWithObject,
//       ShadowOpacityStart/Peak/End, ShadowOpacityFadeIn/OutTime)
//     ShadowForceDisable: RW 0x4C4EF8 / RW 0x4C5013
//   W3DHordeModelDraw::setModelState: RW 0x47A0AD
//   W3DProjectedShadowManager::addShadow: RW 0x50BF6A
//     RW 0x50BF93: ShadowTexture defaults to "shadow" if absent or size <= 1
//     RW 0x50BFF3..0x50C066: size fallback from bounding box if sizeX or sizeY == 0
//     RW 0x50C07B: DecalShadowCentre(drawablePos, offsetX, offsetY, angle)
//   flushDecals: RW 0x509A70 (SHADOW_DECAL multiplicative blend: SRCBLEND_ZERO / DSTBLEND_SRC_COLOR, MODULATE)
//   Drawable::shadowsEnabled: RW 0x672D28 (switched off for dead units with SlowDeathBehavior/ShadowWhenDead = No)
//   House color: RW 0x4C50D2, RW 0x73232B (Shadow::setColor with player color when ShadowUseHouseColor is true)
//   ThingTemplate field defaults: RW 0x740108..0x740175

#include "doctest.h"

#include "ObjectTestUtil.h"
#include "W3dSynth.h"

#include "Common/ModelState.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/ObjectShadow.h"
#include "GameClient/SelectionDecals.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{

struct MockDecalAtlas
{
	static constexpr int kW = 64, kH = 64;
	std::vector<std::uint8_t> px = std::vector<std::uint8_t>((size_t)kW * kH * 4, 0);

	MockDecalAtlas()
	{
		// 64x64 gray texture (128, 128, 128, 255)
		for (int y = 0; y < kH; ++y)
		{
			for (int x = 0; x < kW; ++x)
			{
				std::uint8_t *p = &px[((size_t)y * kW + (size_t)x) * 4];
				p[0] = 128;
				p[1] = 128;
				p[2] = 128;
				p[3] = 255;
			}
		}
	}

	DecalImage image() const { return { kW, kH, px.data() }; }

	static bool rect(const std::string &name, MergeDecalAtlasRect &r)
	{
		if (name == "shadowi" || name == "shadowi.tga")
		{
			r = { 0, 0, 64, 64 };
			return true;
		}
		return false;
	}
};

} // namespace

TEST_CASE("LOCAL-14 synthetic decal shadow: fields, resolution and building decal (RW 0x4C4FD6, RW 0x740108)")
{
	objtest::World w;
	const std::string ini =
		"Object DecalWarrior\n"
		"  Shadow = SHADOW_DECAL\n"
		"  ShadowTexture = shadowi\n"
		"  ShadowSizeX = 30\n"
		"  ShadowSizeY = 20\n"
		"  ShadowOffsetX = 5\n"
		"  ShadowOffsetY = -3\n"
		"  ShadowSunAngle = 45\n"
		"  ShadowMaxHeight = 25\n"
		"  ShadowOverrideLODVisibility = Yes\n"
		"  ShadowUseHouseColor = Yes\n"
		"  ShadowIsRotatingWithObject = Yes\n"
		"  ShadowOpacityStart = 50\n"
		"  ShadowOpacityFadeInTime = 100\n"
		"  ShadowOpacityPeak = 200\n"
		"  ShadowOpacityFadeOutTime = 150\n"
		"  ShadowOpacityEnd = 10\n"
		"End\n";

	REQUIRE(w.load(ini).empty());
	const ThingTemplate *tt = w.get("DecalWarrior");
	REQUIRE(tt != nullptr);

	ObjectShadow::DecalShadowDescriptor desc;
	CHECK(ObjectShadow::resolveDecalShadow(tt, nullptr, nullptr, desc));
	CHECK(desc.enabled);
	CHECK(desc.style == 0x1u); // SHADOW_DECAL
	CHECK(desc.texture == "shadowi");
	CHECK(desc.sizeX == 30.0f);
	CHECK(desc.sizeY == 20.0f);
	CHECK(desc.offsetX == 5.0f);
	CHECK(desc.offsetY == -3.0f);
	CHECK(desc.sunAngle == 45.0f);
	CHECK(desc.maxHeight == 25.0f);
	CHECK(desc.overrideLODVisibility == true);
	CHECK(desc.useHouseColor == true);
	CHECK(desc.isRotatingWithObject == true);
	CHECK(desc.opacityStart == 50u);
	CHECK(desc.opacityFadeInTime == 100.0f);
	CHECK(desc.opacityPeak == 200u);
	CHECK(desc.opacityFadeOutTime == 150.0f);
	CHECK(desc.opacityEnd == 10u);

	// Volume shadow map mask must NOT be set for SHADOW_DECAL
	CHECK_FALSE(ObjectShadow::castsMap(tt, nullptr, nullptr));
}

TEST_CASE("LOCAL-14 condition shadow precedence and rotation (RW 0x4C4EE0, RW 0x4C4F50)")
{
	objtest::World w;
	const std::string ini =
		"Object TemplateWithDecal\n"
		"  Shadow = SHADOW_DECAL\n"
		"  ShadowTexture = shadow_template\n"
		"  ShadowSizeX = 20\n"
		"  ShadowSizeY = 20\n"
		"End\n";

	REQUIRE(w.load(ini).empty());
	const ThingTemplate *tt = w.get("TemplateWithDecal");
	REQUIRE(tt != nullptr);

	// Condition state shadow takes precedence over template (Branch A, RW 0x4C4EE0)
	ModelConditionInfo state;
	state.hasShadow = true;
	state.shadow.type = 0x20; // SHADOW_ALPHA_DECAL
	state.shadow.texture = "decal_hero_good";
	state.shadow.sizeX = 45.0f;
	state.shadow.sizeY = 55.0f;
	state.shadow.offsetX = 2.0f;
	state.shadow.offsetY = -1.0f;
	state.shadow.sunAngle = 30.0f;
	state.shadow.maxHeight = 35.0f;
	state.shadowOpacityPeak = 180;

	ObjectShadow::DecalShadowDescriptor desc;
	CHECK(ObjectShadow::resolveDecalShadow(tt, nullptr, &state, desc));
	CHECK(desc.enabled);
	CHECK(desc.style == 0x20u); // SHADOW_ALPHA_DECAL
	CHECK(desc.texture == "decal_hero_good");
	CHECK(desc.sizeX == 45.0f);
	CHECK(desc.sizeY == 55.0f);
	CHECK(desc.offsetX == 2.0f);
	CHECK(desc.offsetY == -1.0f);
	CHECK(desc.sunAngle == 30.0f);
	CHECK(desc.maxHeight == 35.0f);
	// RW 0x4C4F50: condition shadow sets isRotatingWithObject = true
	CHECK(desc.isRotatingWithObject == true);
	CHECK(desc.opacityPeak == 180u);
}

TEST_CASE("LOCAL-14 volume shadow exclusion and fallback defaults (RW 0x4C4FD6, RW 0x50BF93, RW 0x50BFF3)")
{
	objtest::World w;
	const std::string ini =
		"Object VolumeCaster\n"
		"  Shadow = SHADOW_VOLUME\n"
		"End\n"
		"Object DefaultDecal\n"
		"  Shadow = SHADOW_DECAL\n"
		"  ShadowSizeX = 15\n"
		"End\n"
		"Object DisabledShadow\n"
		"  Shadow = SHADOW_DECAL\n"
		"  ShadowSizeX = 20\n"
		"End\n";

	REQUIRE(w.load(ini).empty());

	// 1. Volume shadow: not a decal shadow, but casts map
	const ThingTemplate *vol = w.get("VolumeCaster");
	REQUIRE(vol != nullptr);
	ObjectShadow::DecalShadowDescriptor vDesc;
	CHECK_FALSE(ObjectShadow::resolveDecalShadow(vol, nullptr, nullptr, vDesc));
	CHECK(ObjectShadow::castsMap(vol, nullptr, nullptr));

	// 2. Default decal: missing ShadowSizeY is 0.0f (RW 0x4C502B..0x4C509D copies as is),
	//    missing ShadowTexture defaults to "shadow" (RW 0x50BF93)
	const ThingTemplate *def = w.get("DefaultDecal");
	REQUIRE(def != nullptr);
	ObjectShadow::DecalShadowDescriptor dDesc;
	CHECK(ObjectShadow::resolveDecalShadow(def, nullptr, nullptr, dDesc));
	CHECK(dDesc.enabled);
	CHECK(dDesc.texture == "shadow");
	CHECK(dDesc.sizeX == 15.0f);
	CHECK(dDesc.sizeY == 0.0f);
	CHECK(dDesc.maxHeight == 20.0f); // RW 0x74015E default
	CHECK(dDesc.opacityPeak == 255u); // RW 0x740136 default
	CHECK(dDesc.isRotatingWithObject == false); // RW 0x740172 default
	CHECK(dDesc.useHouseColor == false);

	// 3. ShadowForceDisable skips shadow resolution (RW 0x4C4EF8 / RW 0x4C5013)
	const ThingTemplate *dis = w.get("DisabledShadow");
	REQUIRE(dis != nullptr);
	W3DModelDrawModuleData mod;
	mod.m_shadowForceDisable = true;
	ObjectShadow::DecalShadowDescriptor disDesc;
	CHECK_FALSE(ObjectShadow::resolveDecalShadow(dis, &mod, nullptr, disDesc));
}

TEST_CASE("LOCAL-14 dead unit shadow switch-off (RW 0x672D28)")
{
	// SlowDeathBehavior ShadowWhenDead = No switches off drawable shadows.
	// Verify buildDrawableDecalShadow honors shadowsEnabled().
	objtest::World w;
	const std::string ini =
		"Object ShadowTrooper\n"
		"  Shadow = SHADOW_DECAL\n"
		"  ShadowTexture = shadowi\n"
		"  ShadowSizeX = 20\n"
		"  ShadowSizeY = 20\n"
		"End\n";

	REQUIRE(w.load(ini).empty());
	const ThingTemplate *tt = w.get("ShadowTrooper");
	REQUIRE(tt != nullptr);

	w3dsynth::MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpt;
	DrawServices services(assets, w.modules, hostOpt);
	std::vector<std::string> problems;
	Drawable d(services, tt, (DrawableID)1, (ObjectID)10, {}, problems);
	const Coord3D pos{ 100.0f, 200.0f, 10.0f };
	d.setPosition(&pos);

	SelectionDecal sd;
	// 1. Alive: shadowsEnabled == true -> decal shadow built
	CHECK(d.shadowsEnabled());
	CHECK(ObjectShadow::buildDrawableDecalShadow(d, sd, true));
	CHECK(sd.object == (ObjectID)10);
	CHECK(sd.drawable == 1u);
	CHECK(sd.position.x == 100.0f);
	CHECK(sd.position.y == 200.0f);
	CHECK(sd.position.z == 10.0f);
	CHECK(sd.size == 20.0f);
	CHECK(sd.sizeY == 20.0f);
	CHECK(sd.style == 0x1u); // SHADOW_DECAL
	CHECK(sd.texture == "shadowi.tga");
	CHECK(sd.terrainOnly == false);

	// 2. Dead: SlowDeathBehavior switches shadows off (RW 0x672D28(false))
	d.setShadowsEnabled(false);
	CHECK_FALSE(d.shadowsEnabled());
	SelectionDecal sdDead;
	CHECK_FALSE(ObjectShadow::buildDrawableDecalShadow(d, sdDead, true));

	// 3. Client hidden: clientHidden == true -> no shadow
	d.setShadowsEnabled(true);
	d.setClientHidden(true);
	SelectionDecal sdHidden;
	CHECK_FALSE(ObjectShadow::buildDrawableDecalShadow(d, sdHidden, true));

	// 4. Review of Local 14: addShadow's gate (RW 0x50BF7B .. 0x50BF8E): with UseShadowDecals off (GlobalData + 0x61, cleared by UseShadowMapping at
	// High / UltraHigh, RW 0x601C62) a shadow without ShadowOverrideLODVisibility is not made
	d.setClientHidden(false);
	SelectionDecal sdOff;
	CHECK_FALSE(ObjectShadow::buildDrawableDecalShadow(d, sdOff, false));
	CHECK(ObjectShadow::buildDrawableDecalShadow(d, sdOff, true));
}

TEST_CASE("LOCAL-14 review: ShadowOverrideLODVisibility keeps a decal shadow with UseShadowDecals off (RW 0x50BF86, template + 0x5FD)")
{
	objtest::World w;
	const std::string ini =
		"Object ShadowOverride\n"
		"  Shadow = SHADOW_DECAL\n"
		"  ShadowTexture = shadowi\n"
		"  ShadowSizeX = 20\n"
		"  ShadowSizeY = 20\n"
		"  ShadowOverrideLODVisibility = Yes\n"
		"End\n";
	REQUIRE(w.load(ini).empty());
	const ThingTemplate *tt = w.get("ShadowOverride");
	REQUIRE(tt != nullptr);
	w3dsynth::MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpt;
	DrawServices services(assets, w.modules, hostOpt);
	std::vector<std::string> problems;
	Drawable d(services, tt, (DrawableID)1, (ObjectID)10, {}, problems);
	const Coord3D pos{ 100.0f, 200.0f, 10.0f };
	d.setPosition(&pos);
	SelectionDecal sd;
	CHECK(ObjectShadow::buildDrawableDecalShadow(d, sd, false));
}

TEST_CASE("LOCAL-14 multiplicative decal blend arithmetic (RW 0x509A70)")
{
	// RW 0x509A70: SHADOW_DECAL uses multiplicative blend (SRCBLEND_ZERO / DSTBLEND_SRC_COLOR, MODULATE).
	// Verify EvaluateSelectionDecals computes c[ch] *= (1.0 - op + factor * op).
	MockDecalAtlas atlas;
	SelectionDecal sd;
	sd.position = { 50.0f, 50.0f, 0.0f };
	sd.size = 20.0f;
	sd.sizeY = 20.0f;
	sd.texture = "shadowi";
	sd.style = 0x1u; // SHADOW_DECAL (kMultiplicativeStyle)
	sd.color = 0xFFFFFFFFu; // opacity 255, white diffuse

	MergeDecalGrid grid;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ sd }, MockDecalAtlas::rect, SelectionDecalSettings{}, grid, &err), err);

	// Initial ground color = (200, 200, 200)
	// Texture texel = 128 (factor = 128 / 255.0f ~ 0.50196)
	// Expected result = floor(200 * (128 / 255.0f) + 0.5) = 100
	const std::uint8_t ground[3] = { 200, 200, 200 };
	std::uint8_t out[3] = { 0, 0, 0 };
	CHECK(EvaluateSelectionDecals(grid, atlas.image(), 50.0f, 50.0f, 0.0f, DecalReceiver::Terrain, ground, out));
	CHECK(out[0] == 100);
	CHECK(out[1] == 100);
	CHECK(out[2] == 100);

	// Outside decal footprint: ground color remains untouched
	std::uint8_t outFar[3] = { 0, 0, 0 };
	CHECK_FALSE(EvaluateSelectionDecals(grid, atlas.image(), 100.0f, 100.0f, 0.0f, DecalReceiver::Terrain, ground, outFar));
	CHECK(outFar[0] == 200);
	CHECK(outFar[1] == 200);
	CHECK(outFar[2] == 200);

	// Half opacity: opacity = 128 (0x80FFFFFF)
	// op = 128 / 255.0f ~ 0.50196
	// factor = 128 / 255.0f ~ 0.50196
	// m = 1.0 - op + factor * op ~ 0.7500
	// out = floor(200 * 0.75 + 0.5) = 150
	SelectionDecal sdHalf = sd;
	sdHalf.color = 0x80FFFFFFu;
	MergeDecalGrid gridHalf;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ sdHalf }, MockDecalAtlas::rect, SelectionDecalSettings{}, gridHalf, &err), err);
	std::uint8_t outHalf[3] = { 0, 0, 0 };
	CHECK(EvaluateSelectionDecals(gridHalf, atlas.image(), 50.0f, 50.0f, 0.0f, DecalReceiver::Terrain, ground, outHalf));
	CHECK(outHalf[0] == 150);
	CHECK(outHalf[1] == 150);
	CHECK(outHalf[2] == 150);
}
