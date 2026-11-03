// OpenBFME tests of lane SELWALL-1 (the owner: the selection marker of a unit on a Helm's Deep wall is drawn on the ground below it): RotWK projects every
// selection decal onto the WALK_ON_TOP_OF_WALL objects near it and leaves out its terrain part when the decal stands kSelectionDecalMaxHeight (20) or more
// above the terrain (W3DProjectedShadowManager RW 0x50C23A, GameClient/SelectionDecals.h). MarkRaisedSelectionDecals, the merge grid's height data, the
// terrain / wall evaluation (selection_decals.gdshaderinc sel_merge_at in C++) and the wall shader's text. Synthetic textures only. GPL-3.0.

#include "doctest.h"

#include "GameClient/SelectionDecals.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
// a 64 x 64 disc of radius r (alpha 255 inside, 0 outside), its colour (r, g, b) = (100, 150, 200)
void disc(std::vector<std::uint8_t> &atlas, int atlasW, int ox, float r)
{
	for (int y = 0; y < 64; ++y)
	{
		for (int x = 0; x < 64; ++x)
		{
			const float dx = (float)x + 0.5f - 32.0f, dy = (float)y + 0.5f - 32.0f;
			std::uint8_t *p = &atlas[((size_t)y * (size_t)atlasW + (size_t)(ox + x)) * 4];
			p[0] = 100;
			p[1] = 150;
			p[2] = 200;
			p[3] = std::sqrt(dx * dx + dy * dy) <= r ? 255 : 0;
		}
	}
}

struct Atlas
{
	static constexpr int kW = 128, kH = 64;
	std::vector<std::uint8_t> px = std::vector<std::uint8_t>((size_t)kW * kH * 4, 0);
	Atlas()
	{
		disc(px, kW, 0, 30.0f);  // "rim" at x 0
		disc(px, kW, 64, 24.0f); // "cut" at x 64
	}
	DecalImage image() const { return { kW, kH, px.data() }; }
	static bool rect(const std::string &name, MergeDecalAtlasRect &r)
	{
		if (name == "rim" || name == "cut")
		{
			r = { name == "rim" ? 0 : 64, 0, 64, 64 };
			return true;
		}
		return false;
	}
};

SelectionDecal member(float x, float y, float z, bool raised)
{
	SelectionDecal d;
	d.position = { x, y, z };
	d.size = 40.0f;
	d.texture = "rim";
	d.texture2 = "cut";
	d.style = 0x1000u;
	d.color = 0xFFFFFFFFu;
	d.raised = raised;
	return d;
}

SelectionDecalSettings twoPasses()
{
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	s.useSimpleMergeDecals = false;
	return s;
}

bool drawsAt(const MergeDecalGrid &g, const Atlas &a, float x, float y, float z, DecalReceiver r)
{
	std::uint8_t rgba[4] = { 0, 0, 0, 0 };
	return EvaluateMergeDecalGrid(g, a.image(), x, y, z, r, rgba);
}
} // namespace

TEST_CASE("selwall1: a decal 20 or more above the terrain's floor is raised, 19.99 above is not (RW 0x50CDE4: the terrain part while z - floor < 20)")
{
	CHECK(kSelectionDecalMaxHeight == 20.0f);
	std::vector<SelectionDecal> list = { member(0, 0, 119.99f, false), member(50, 0, 120.0f, false), member(100, 0, 100.0f, true) };
	const auto floorAt = [](float, float) { return 100.0f; };
	CHECK(MarkRaisedSelectionDecals(list, floorAt)); // two flags changed
	CHECK_FALSE(list[0].raised);
	CHECK(list[1].raised);
	CHECK_FALSE(list[2].raised);
	// the same list again: nothing changes
	CHECK_FALSE(MarkRaisedSelectionDecals(list, floorAt));
	// the floor at the decal's own place
	list[0].position.z = 130.0f;
	const auto slope = [](float x, float) { return x < 25.0f ? 100.0f : 200.0f; };
	CHECK(MarkRaisedSelectionDecals(list, slope));
	CHECK(list[0].raised);       // 30 above 100
	CHECK_FALSE(list[1].raised); // 80 below 200
	CHECK_FALSE(list[2].raised);
}

TEST_CASE("selwall1: the merge grid carries each decal's height and gives a raised decal's index entries as -(i + 1)")
{
	const std::vector<SelectionDecal> list = { member(0, 0, 5.0f, false), member(30, 0, 61.5f, true), member(60, 0, 7.0f, false) };
	MergeDecalGrid g;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(list, &Atlas::rect, twoPasses(), g, &err), err);
	REQUIRE(g.decals == 3);
	CHECK(g.texels[0 * 16 + 7] == 5.0f);
	CHECK(g.texels[1 * 16 + 7] == 61.5f);
	CHECK(g.texels[2 * 16 + 7] == 7.0f);
	// cells 40 wide (the median decal) from x -20: decal 0 in cells 0 and 1, decal 1 in 0 and 1, decal 2 in 1 and 2; one row
	int seen[3] = { 0, 0, 0 };
	for (int c = 0; c < g.cellsX * g.cellsY; ++c)
	{
		const float *cell = &g.texels[((size_t)g.cellOffset + (size_t)c) * 4];
		for (int k = 0; k < (int)cell[1]; ++k)
		{
			const float *e = &g.texels[((size_t)g.indexOffset + (size_t)((int)cell[0] + k)) * 4];
			const int i = e[3] < 0.0f ? (int)(-e[3]) - 1 : (int)e[3];
			REQUIRE(i >= 0); // tolerance: an index into the three decals
			REQUIRE(i < 3);  // tolerance: as above
			CHECK(e[0] == list[(size_t)i].position.x);
			CHECK(e[3] == (list[(size_t)i].raised ? -(float)(i + 1) : (float)i));
			++seen[i];
		}
	}
	CHECK(seen[0] == 2);
	CHECK(seen[1] == 2);
	CHECK(seen[2] == 2);
}

TEST_CASE("selwall1: the terrain draws no part of a raised decal; a wall surface draws a decal within 20 of its height, the terrain's decal as before")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	// (17, 0) lies on the decal's rim (between the cut-out's 24 / 64 and the disc's 30 / 64 of its 40 units), (0, 0) under its cut-out
	{
		const std::vector<SelectionDecal> ground = { member(0, 0, 3.0f, false) };
		REQUIRE_MESSAGE(BuildMergeDecalGrid(ground, &Atlas::rect, twoPasses(), g, &err), err);
		std::uint8_t old[4] = { 0, 0, 0, 0 }, now[4] = { 0, 0, 0, 0 };
		CHECK(EvaluateMergeDecalGrid(g, a.image(), 17.0f, 0.0f, old));
		CHECK(EvaluateMergeDecalGrid(g, a.image(), 17.0f, 0.0f, 999.0f, DecalReceiver::Terrain, now)); // the terrain ignores the height
		CHECK(old[0] == now[0]);
		CHECK(old[1] == now[1]);
		CHECK(old[2] == now[2]);
		CHECK(old[3] == now[3]);
		CHECK(now[0] == 100);
		CHECK(now[3] == 255);
		CHECK_FALSE(drawsAt(g, a, 0.0f, 0.0f, 3.0f, DecalReceiver::Terrain));
		// a wall surface beside a ground unit: within 20 of the decal's height
		CHECK(drawsAt(g, a, 17.0f, 0.0f, 23.0f, DecalReceiver::Wall));
		CHECK_FALSE(drawsAt(g, a, 17.0f, 0.0f, 23.5f, DecalReceiver::Wall));
	}
	{
		const std::vector<SelectionDecal> up = { member(0, 0, 60.0f, true) };
		REQUIRE_MESSAGE(BuildMergeDecalGrid(up, &Atlas::rect, twoPasses(), g, &err), err);
		CHECK_FALSE(drawsAt(g, a, 17.0f, 0.0f, 0.0f, DecalReceiver::Terrain));
		CHECK(drawsAt(g, a, 17.0f, 0.0f, 60.0f, DecalReceiver::Wall));
		CHECK(drawsAt(g, a, 17.0f, 0.0f, 80.0f, DecalReceiver::Wall));
		CHECK(drawsAt(g, a, 17.0f, 0.0f, 40.0f, DecalReceiver::Wall));
		CHECK_FALSE(drawsAt(g, a, 17.0f, 0.0f, 80.5f, DecalReceiver::Wall));
		CHECK_FALSE(drawsAt(g, a, 17.0f, 0.0f, 39.5f, DecalReceiver::Wall));
		CHECK_FALSE(drawsAt(g, a, 0.0f, 0.0f, 60.0f, DecalReceiver::Wall)); // its own cut-out
	}
	{
		// a unit on the wall above a unit on the ground: each surface sees only its own decal's cut-out and rim
		const std::vector<SelectionDecal> both = { member(0, 0, 0.0f, false), member(10, 0, 60.0f, true) };
		REQUIRE_MESSAGE(BuildMergeDecalGrid(both, &Atlas::rect, twoPasses(), g, &err), err);
		// (27, 0) is on the raised decal's rim and outside the ground decal; (-17, 0) on the ground decal's rim, under no other decal
		CHECK(drawsAt(g, a, 27.0f, 0.0f, 60.0f, DecalReceiver::Wall));
		CHECK_FALSE(drawsAt(g, a, 27.0f, 0.0f, 0.0f, DecalReceiver::Terrain));
		CHECK(drawsAt(g, a, -17.0f, 0.0f, 0.0f, DecalReceiver::Terrain));
		CHECK_FALSE(drawsAt(g, a, -17.0f, 0.0f, 60.0f, DecalReceiver::Wall));
		// (17, 0): the ground decal's rim, under the raised decal's cut-out: the terrain draws (the raised cut-out is not in its stencil), the wall does not
		CHECK(drawsAt(g, a, 17.0f, 0.0f, 0.0f, DecalReceiver::Terrain));
		CHECK_FALSE(drawsAt(g, a, 17.0f, 0.0f, 60.0f, DecalReceiver::Wall));
	}
}

TEST_CASE("selwall1: a wall model's opaque surface draws the merge decals (W3DShaderKey::SelectionDecals); other surfaces and keys are unchanged")
{
	W3DShaderKey base;
	base.Texturing = true;
	base.Tex[0] = true;
	W3DShaderKey sel = base;
	sel.SelectionDecals = true;
	CHECK(sel.Id() == base.Id() + "_sel");
	const std::string plain = Generate_W3D_Shader_Code(base);
	const std::string code = Generate_W3D_Shader_Code(sel);
	CHECK(plain.find("selection_decals") == std::string::npos);
	// an opaque surface hands its colour over once, converted (RENDER-3), with the bit or without
	for (const std::string *c : { &plain, &code })
	{
		const std::string once = "ALBEDO = w3d_to_linear(clamp(cur.rgb, 0.0, 1.0));";
		const size_t at = c->find(once);
		CHECK(at != std::string::npos);
		CHECK(c->find(once, at + 1) == std::string::npos);
		CHECK(c->find("ALBEDO = clamp(cur.rgb, 0.0, 1.0);") == std::string::npos);
	}
	// the decal comes after that hand-over
	CHECK(code.find("vec3 sel_ground = ALBEDO;") > code.find("ALBEDO = w3d_to_linear(clamp(cur.rgb, 0.0, 1.0));")); // tolerance: an order of two text positions, not a value
	CHECK(code.find("#include \"res://shaders/selection_decals.gdshaderinc\"") != std::string::npos);
	// only on the surface's upward faces (the walkable top, not the side faces)
	CHECK(code.find("vec4 sel = normalize(v_wnrm).y >= SEL_WALL_MIN_UP ? sel_merge_at(vec3(sel_world.x, -sel_world.z, sel_world.y), true) : vec4(0.0);") != std::string::npos);
	CHECK(code.find("vec3 sel_world = (INV_VIEW_MATRIX * vec4(v_vpos, 1.0)).xyz;") != std::string::npos);
	// over the lit colour, kept above the object shadows as on the terrain
	CHECK(code.find("EMISSION = ALBEDO - sel_ground * (1.0 - sel.a) * shadow_enabled;") != std::string::npos);
	CHECK(code.find("ALBEDO = vec3((1.0 - sel.a) * shadow_enabled);") != std::string::npos);
	// the generator ignores the bit for a blended surface, a translucent drawable's and an occlusion pass
	W3DShaderKey blended = sel;
	blended.Blend = W3D_GODOT_BLEND_MIX;
	W3DShaderKey blendedPlain = base;
	blendedPlain.Blend = W3D_GODOT_BLEND_MIX;
	CHECK(Generate_W3D_Shader_Code(blended) == Generate_W3D_Shader_Code(blendedPlain));
	W3DShaderKey ghost = sel;
	ghost.AlphaOverride = true;
	W3DShaderKey ghostPlain = base;
	ghostPlain.AlphaOverride = true;
	CHECK(Generate_W3D_Shader_Code(ghost) == Generate_W3D_Shader_Code(ghostPlain));
	W3DShaderKey mark = sel;
	mark.OcclusionPass = W3D_OCCLUSION_PASS_MARK;
	W3DShaderKey markPlain = base;
	markPlain.OcclusionPass = W3D_OCCLUSION_PASS_MARK;
	CHECK(Generate_W3D_Shader_Code(mark) == Generate_W3D_Shader_Code(markPlain));
}

TEST_CASE("selwall1: an AIRBORNE_TARGET decal takes its layer's height first (RW 0x68DB35), so a flying unit's decal lies on the ground and is not raised")
{
	std::vector<SelectionDecal> list = { member(0, 0, 300.0f, false), member(50, 0, 300.0f, false) };
	list[0].object = (ObjectID)7; // airborne
	list[1].object = (ObjectID)8; // a unit up on something, not airborne
	const auto floorAt = [](float, float) { return 100.0f; };
	const auto grounded = [](ObjectID id, const Coord3D &, float &z) {
		if (id != (ObjectID)7)
		{
			return false;
		}
		z = 100.0f;
		return true;
	};
	CHECK(MarkRaisedSelectionDecals(list, floorAt, grounded));
	CHECK(list[0].position.z == 100.0f);
	CHECK_FALSE(list[0].raised);
	CHECK(list[1].position.z == 300.0f);
	CHECK(list[1].raised);
	// again: the height and the flags stay
	CHECK_FALSE(MarkRaisedSelectionDecals(list, floorAt, grounded));
	CHECK(list[0].position.z == 100.0f);
}

TEST_CASE("selwall1: the state report's composite (ComposeMergeDecals) leaves a raised decal out, as the terrain shader does")
{
	const Atlas a;
	const std::vector<SelectionDecal> ground = { member(0, 0, 0.0f, false) };
	const std::vector<SelectionDecal> both = { member(0, 0, 0.0f, false), member(10, 0, 60.0f, true) };
	const auto image = [&](const std::string &name) {
		// each texture on its own (rows of 64 from the atlas)
		static std::vector<std::uint8_t> own[2];
		const int t = name == "rim" ? 0 : 1;
		if (own[t].empty())
		{
			own[t].resize((size_t)64 * 64 * 4);
			for (int y = 0; y < 64; ++y)
			{
				for (int x = 0; x < 64 * 4; ++x)
				{
					own[t][(size_t)y * 64 * 4 + (size_t)x] = a.px[((size_t)y * Atlas::kW + (size_t)t * 64) * 4 + (size_t)x];
				}
			}
		}
		return DecalImage{ 64, 64, own[t].data() };
	};
	MergedSelectionDecals one, two;
	std::string err;
	REQUIRE_MESSAGE(ComposeMergeDecals(ground, image, twoPasses(), one, &err), err);
	REQUIRE_MESSAGE(ComposeMergeDecals(both, image, twoPasses(), two, &err), err);
	CHECK(two.decals == 1);
	CHECK(two.width == one.width);
	CHECK(two.height == one.height);
	CHECK(two.minX == one.minX);
	CHECK(two.maxX == one.maxX);
	CHECK(two.rgba == one.rgba);
}
