// OpenBFME tests of lane HERORING-1 (a selected hero's ring did not show): the non-merge selection decals (SHADOW_ALPHA_DECAL, a hero's decal_hero_good)
// are drawn by the terrain and wall shaders from the merge decals' grid (BuildMergeDecalGrid; godot/shaders/selection_decals.gdshaderinc sel_alpha_at),
// blended as RotWK's flushDecals draws them (RW 0x509A1F: ShaderClass 0x1198B3, SRC_ALPHA / ONE_MINUS_SRC_ALPHA, no alpha test, texture x diffuse) before
// the merge list. EvaluateSelectionDecals is the shaders' arithmetic in C++. Synthetic textures only. GPL-3.0.

#include "doctest.h"

#include "GameClient/SelectionDecals.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
// the atlas: "rim" at x 0 (a disc of radius 30 / 64, alpha 255 inside, 0 outside, rgb (100, 150, 200)), "cut" at x 64 (the same of radius 24), "half" at
// x 128 (the left half alpha 255, the right 0, rgb (100, 150, 200))
struct Atlas
{
	static constexpr int kW = 192, kH = 64;
	std::vector<std::uint8_t> px = std::vector<std::uint8_t>((size_t)kW * kH * 4, 0);
	Atlas()
	{
		for (int y = 0; y < 64; ++y)
		{
			for (int x = 0; x < 64; ++x)
			{
				const float dx = (float)x + 0.5f - 32.0f, dy = (float)y + 0.5f - 32.0f;
				const float d = std::sqrt(dx * dx + dy * dy);
				const int alpha[3] = { d <= 30.0f ? 255 : 0, d <= 24.0f ? 255 : 0, x < 32 ? 255 : 0 };
				for (int k = 0; k < 3; ++k)
				{
					std::uint8_t *p = &px[((size_t)y * kW + (size_t)(k * 64 + x)) * 4];
					p[0] = 100;
					p[1] = 150;
					p[2] = 200;
					p[3] = (std::uint8_t)alpha[k];
				}
			}
		}
	}
	DecalImage image() const { return { kW, kH, px.data() }; }
	static bool rect(const std::string &name, MergeDecalAtlasRect &r)
	{
		const int k = name == "rim" ? 0 : name == "cut" ? 1 : name == "half" ? 2 : -1;
		if (k < 0)
		{
			return false;
		}
		r = { k * 64, 0, 64, 64 };
		return true;
	}
};

SelectionDecal hero(float x, float y, float z, std::uint32_t color, const char *texture = "rim")
{
	SelectionDecal d;
	d.position = { x, y, z };
	d.size = 40.0f;
	d.texture = texture;
	d.style = 0x20u; // SHADOW_ALPHA_DECAL
	d.color = color;
	return d;
}

SelectionDecal member(float x, float y, float z)
{
	SelectionDecal d;
	d.position = { x, y, z };
	d.size = 40.0f;
	d.texture = "rim";
	d.texture2 = "cut";
	d.style = 0x1000u; // SHADOW_MERGE_DECAL
	d.color = 0xFFFFFFFFu;
	return d;
}

SelectionDecalSettings twoPasses()
{
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	s.useSimpleMergeDecals = false;
	return s;
}

struct Rgb
{
	int r, g, b;
	bool drawn;
};

Rgb at(const MergeDecalGrid &g, const Atlas &a, float x, float y, float z, DecalReceiver r, std::uint8_t surface)
{
	const std::uint8_t s[3] = { surface, surface, surface };
	std::uint8_t out[3] = { 0, 0, 0 };
	const bool drawn = EvaluateSelectionDecals(g, a.image(), x, y, z, r, s, out);
	return { out[0], out[1], out[2], drawn };
}
} // namespace

TEST_CASE("heroring1: the grid carries an alpha decal after the merge decals: its own list in each cell, its turn and its colour")
{
	const std::vector<SelectionDecal> list = { member(0, 0, 0), hero(60, 0, 7.0f, 0x80FFFFFFu), member(120, 0, 0) };
	MergeDecalGrid g;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(list, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(g.decals == 2);
	CHECK(g.alphaDecals == 1);
	// the merge decals keep records 0 and 1, the hero is record 2: its opacity 128, its height, its turn's cosine 1 and sine 0 where Texture2's rect was
	CHECK(g.texels[0 * 16 + 0] == 0.0f);
	CHECK(g.texels[1 * 16 + 0] == 120.0f);
	CHECK(g.texels[2 * 16 + 0] == 60.0f);
	CHECK(g.texels[2 * 16 + 3] == 128.0f);
	CHECK(g.texels[2 * 16 + 7] == 7.0f);
	CHECK(g.texels[2 * 16 + 12] == 1.0f);
	CHECK(g.texels[2 * 16 + 13] == 0.0f);
	// every cell: its merge list holds only merge records, its alpha list only the hero
	int heroEntries = 0;
	for (int c = 0; c < g.cellsX * g.cellsY; ++c)
	{
		const float *cell = &g.texels[((size_t)g.cellOffset + (size_t)c) * 4];
		for (int k = 0; k < (int)cell[1]; ++k)
		{
			CHECK(g.texels[((size_t)g.indexOffset + (size_t)((int)cell[0] + k)) * 4 + 3] != 2.0f);
		}
		for (int k = 0; k < (int)cell[3]; ++k)
		{
			CHECK(g.texels[((size_t)g.indexOffset + (size_t)((int)cell[2] + k)) * 4 + 3] == 2.0f);
			++heroEntries;
		}
	}
	// cells 40 wide from x -20: the hero (x 40 .. 80) in cells 1 and 2 of the one row
	CHECK(heroEntries == 2);
	CHECK(g.maxAlphaPerCell == 1);
	// lane ECOCIRCLE-2: an additive decal (SHADOW_ADDITIVE_DECAL 0x40) is now an alpha decal of the grid too, added to the surface rather than blended
	std::vector<SelectionDecal> additive = list;
	additive[1].style = 0x40u;
	CHECK(BuildMergeDecalGrid(additive, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("0x40") == std::string::npos);
	CHECK(g.decals == 2);
	CHECK(g.alphaDecals == 1);
	CHECK(g.texels[2 * 16 + 15] == -3.0f); // additive, wall capable (rec3.w)
	// SHADOW_DECAL_TERRAIN_CLAIM (0x2000) is no alpha decal (RotWK puts it on the claim grid and colours it from the TerrainResourceManager)
	additive[1].style = 0x2000u;
	CHECK(BuildMergeDecalGrid(additive, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("0x2000") != std::string::npos);
	CHECK(g.alphaDecals == 0);
	// so is an alpha decal whose texture is missing; a merge decal's missing texture still fails the grid
	std::vector<SelectionDecal> missing = list;
	missing[1].texture = "nope";
	CHECK(BuildMergeDecalGrid(missing, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("nope") != std::string::npos);
	CHECK(g.decals == 2);
	CHECK(g.alphaDecals == 0);
	missing[0].texture = "nope";
	CHECK_FALSE(BuildMergeDecalGrid(missing, &Atlas::rect, twoPasses(), g, &err));
	// the shaders' grid (BuildSelectionDecalGrid, the HUD's): a merge decal's missing texture takes the merge list, not the hero's ring
	REQUIRE(BuildSelectionDecalGrid({ member(0, 0, 0), hero(60, 0, 0, 0xFFFFFFFFu) }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(g.decals == 1);
	CHECK(g.alphaDecals == 1);
	std::vector<SelectionDecal> broken = { member(0, 0, 0), hero(60, 0, 0, 0xFFFFFFFFu) };
	broken[0].texture = "nope";
	REQUIRE(BuildSelectionDecalGrid(broken, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("nope") != std::string::npos);
	CHECK(g.decals == 0);
	CHECK(g.alphaDecals == 1);
	CHECK(g.texels[0 * 16 + 0] == 60.0f); // the ring is record 0
	broken[1].texture = "nope"; // the ring's texture missing too: an empty grid (nothing drawn), both errors named
	REQUIRE(BuildSelectionDecalGrid(broken, &Atlas::rect, twoPasses(), g, &err));
	CHECK(g.alphaDecals == 0);
	CHECK(SelectionDecalShaderMode(g) == 0);
	broken.pop_back(); // the merge decal alone
	CHECK_FALSE(BuildSelectionDecalGrid(broken, &Atlas::rect, twoPasses(), g, &err));
	// the notes of the decals after the broken merge decal reach the report too (the first build stops at it): a missing ring texture, an
	// unsupported style; each note once. Lane ECOCIRCLE-2: 0x40 is an alpha decal now, so the multiplicative 0x01 stands in for an unsupported style
	std::vector<SelectionDecal> later = { member(0, 0, 0), hero(60, 0, 0, 0xFFFFFFFFu, "ringgone"), hero(-60, 0, 0, 0xFFFFFFFFu) };
	later[0].texture = "mergegone";
	later[2].style = 0x01u; // SHADOW_MUL_DECAL handled otherwise: not drawn
	REQUIRE(BuildSelectionDecalGrid(later, &Atlas::rect, twoPasses(), g, &err)); // nothing drawn (the grid is empty)
	CHECK(SelectionDecalShaderMode(g) == 0);
	CHECK(err.find("mergegone") != std::string::npos);
	CHECK(err.find("ringgone") != std::string::npos);
	CHECK(err.find("0x1") != std::string::npos);
	std::vector<SelectionDecal> before = { later[1], later[0] }; // the ring's note is already in the first build's: named once
	REQUIRE(BuildSelectionDecalGrid(before, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("ringgone") != std::string::npos);
	CHECK(err.find("ringgone") == err.rfind("ringgone"));
	CHECK(err.find("mergegone") != std::string::npos);
	// a skipped alpha decal before another: the other takes the first alpha record and draws
	const Atlas atlas;
	REQUIRE(BuildMergeDecalGrid({ hero(0, 0, 0, 0xFFFF0000u, "nope"), hero(0, 0, 0, 0xFFFFFFFFu) }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(g.alphaDecals == 1);
	CHECK(g.texels[0 * 16 + 4] == 255.0f); // record 0: the white one's colour
	CHECK(g.texels[0 * 16 + 5] == 255.0f);
	const Rgb c = at(g, atlas, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 100);
	CHECK(c.g == 150);
	CHECK(c.b == 200);
	// the shaders' mode: the merge decals' (two passes 1, simple 2), + 4 with alpha decals
	REQUIRE(BuildMergeDecalGrid(list, &Atlas::rect, twoPasses(), g, &err));
	CHECK(SelectionDecalShaderMode(g) == 5);
	REQUIRE(BuildMergeDecalGrid({ list[1] }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(SelectionDecalShaderMode(g) == 4);
	REQUIRE(BuildMergeDecalGrid({ list[0] }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(SelectionDecalShaderMode(g) == 1);
	SelectionDecalSettings simple = twoPasses();
	simple.useSimpleMergeDecals = true;
	REQUIRE(BuildMergeDecalGrid(list, &Atlas::rect, simple, g, &err));
	CHECK(SelectionDecalShaderMode(g) == 6);
	REQUIRE(BuildMergeDecalGrid({}, &Atlas::rect, twoPasses(), g, &err));
	CHECK(SelectionDecalShaderMode(g) == 0);
}

TEST_CASE("heroring1: an alpha decal blends over the surface as RotWK's shader does: C (1 - a) + texel x colour x a, a = texel alpha x opacity")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ hero(0, 0, 0, 0x80FFFFFFu) }, &Atlas::rect, twoPasses(), g, &err), err);
	// a = 128 / 255 at the disc's inside: over black 100 x a, 150 x a, 200 x a; over white 255 - 155 a, 255 - 105 a, 255 - 55 a
	const Rgb black = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(black.drawn);
	CHECK(black.r == 50);
	CHECK(black.g == 75);
	CHECK(black.b == 100);
	const Rgb white = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 255);
	CHECK(white.r == 177);
	CHECK(white.g == 202);
	CHECK(white.b == 227);
	// outside the disc (texel alpha 0) the surface stays
	const Rgb outside = at(g, a, 19.0f, 19.0f, 0.0f, DecalReceiver::Terrain, 40);
	CHECK_FALSE(outside.drawn);
	CHECK(outside.r == 40);
	CHECK(outside.g == 40);
	CHECK(outside.b == 40);
}

TEST_CASE("heroring1: an additive decal (lane ECOCIRCLE-2) adds texel x colour x alpha to the surface and keeps its colour")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	// the same ring at opacity 128 in SHADOW_ADDITIVE_DECAL: over black 0 + 100 x a, 150 x a, 200 x a; over white every channel saturates at 255
	std::vector<SelectionDecal> d = { hero(0, 0, 0, 0x80FFFFFFu) };
	d[0].style = 0x40u;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(d, &Atlas::rect, twoPasses(), g, &err), err);
	const Rgb black = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(black.drawn);
	CHECK(black.r == 50);
	CHECK(black.g == 75);
	CHECK(black.b == 100);
	const Rgb white = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 255);
	CHECK(white.drawn);
	CHECK(white.r == 255);
	CHECK(white.g == 255);
	CHECK(white.b == 255);
	// the list order: red alpha at full opacity under the additive share gives (150, 75, 100); the additive share under the red gives the red only
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ hero(0, 0, 0, 0xFFFF0000u), d[0] }, &Atlas::rect, twoPasses(), g, &err), err);
	Rgb c = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 150);
	CHECK(c.g == 75);
	CHECK(c.b == 100);
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ d[0], hero(0, 0, 0, 0xFFFF0000u) }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 100);
	CHECK(c.g == 0);
	CHECK(c.b == 0);
	// the texel's alpha takes no part (setOpacity RW 0x73228B gives an additive decal no diffuse alpha, SC_ADDITIVE is ONE / ONE): the "half"
	// texture's transparent half (u 0.75) adds its rgb x colour x opacity as its opaque half does
	std::vector<SelectionDecal> half = { hero(0, 0, 0, 0x80FFFFFFu, "half") };
	half[0].style = 0x40u;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(half, &Atlas::rect, twoPasses(), g, &err), err);
	for (const float x : { -10.0f, 10.0f })
	{
		const Rgb h = at(g, a, x, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
		CHECK(h.drawn);
		CHECK(h.r == 50);
		CHECK(h.g == 75);
		CHECK(h.b == 100);
	}
	// 0x800 (SHADOW_ADDITIVE_DECAL_DYNAMIC) is additive too; without terrainOnly it draws on a wall as well (rec3.w -3)
	d[0].style = 0x800u;
	d[0].terrainOnly = false;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(d, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(g.texels[0 * 16 + 15] == -3.0f);
	CHECK(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Wall, 0).drawn);
}

TEST_CASE("heroring1: several alpha decals blend in their list order; the merge decals are drawn over them")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	// a red one at full opacity, then a white one at half (128): red first, the white over it
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ hero(0, 0, 0, 0xFFFF0000u), hero(0, 0, 0, 0x80FFFFFFu) }, &Atlas::rect, twoPasses(), g, &err), err);
	Rgb c = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 100);
	CHECK(c.g == 75);
	CHECK(c.b == 100);
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ hero(0, 0, 0, 0x80FFFFFFu), hero(0, 0, 0, 0xFFFF0000u) }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 100);
	CHECK(c.g == 0);
	CHECK(c.b == 0);
	// a horde member's merge decal at the same place: on its rim (17, 0) the merge colour covers the red ring; under its cut-out (0, 0) the ring shows
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ member(0, 0, 0), hero(0, 0, 0, 0xFFFF0000u) }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 17.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 100);
	CHECK(c.g == 150);
	CHECK(c.b == 200);
	c = at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0);
	CHECK(c.r == 100);
	CHECK(c.g == 0);
	CHECK(c.b == 0);
}

TEST_CASE("heroring1: a raised alpha decal is left out of the terrain and drawn on a wall within 20 of its height, as the merge decals (lane SELWALL-1)")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	std::vector<SelectionDecal> up = { hero(0, 0, 60.0f, 0xFFFFFFFFu) };
	up[0].raised = true;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(up, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK_FALSE(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
	CHECK(at(g, a, 0.0f, 0.0f, 60.0f, DecalReceiver::Wall, 0).drawn);
	CHECK(at(g, a, 0.0f, 0.0f, 80.0f, DecalReceiver::Wall, 0).drawn);
	CHECK(at(g, a, 0.0f, 0.0f, 40.0f, DecalReceiver::Wall, 0).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 0.0f, 80.5f, DecalReceiver::Wall, 0).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 0.0f, 39.5f, DecalReceiver::Wall, 0).drawn);
	// a hero on the ground: the terrain, and a wall top within 20 of it
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ hero(0, 0, 3.0f, 0xFFFFFFFFu) }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
	CHECK(at(g, a, 0.0f, 0.0f, 23.0f, DecalReceiver::Wall, 0).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 0.0f, 23.5f, DecalReceiver::Wall, 0).drawn);
}

TEST_CASE("heroring1: an alpha decal's turn rotates its texture (u along the turned x axis, v toward -y at angle 0)")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	SelectionDecal d = hero(0, 0, 0, 0xFFFFFFFFu, "half"); // the texture's left half (u < 0.5) drawn
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ d }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, -10.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
	CHECK_FALSE(at(g, a, 10.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
	d.angle = 1.57079633f; // a quarter turn: u along +y
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ d }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, 0.0f, -10.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 10.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
	// an eighth turn: the decal is binned by its circumcircle (half extent 20 x sqrt 2), so its corner at (-26, 0) (u = v = 0.04, the texture's drawn
	// half) is in the grid and drawn, outside the unturned square's 20
	d.angle = 0.78539816f;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ d }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(g.minX == -20.0f * 1.41421356f);
	CHECK(at(g, a, -26.0f, 0.0f, 0.0f, DecalReceiver::Terrain, 0).drawn);
}

TEST_CASE("heroring1: the wall shader blends the alpha decals over the surface's gamma colour, then the merge decals; the shadow split keeps both above")
{
	W3DShaderKey sel;
	sel.Texturing = true;
	sel.Tex[0] = true;
	sel.SelectionDecals = true;
	const std::string code = Generate_W3D_Shader_Code(sel);
	CHECK(code.find("vec4 sela = sel_up ? sel_alpha_at(sel_at, true) : vec4(0.0);") != std::string::npos);
	// review r2 (#208): the surface's shadowed share and the ring over it go to light() (one colour space), the shadow split keeps the merge share
	CHECK(code.find("varying vec3 v_shadow_over;") != std::string::npos);
	CHECK(code.find("vec3 sel_shadow = clamp(cur.rgb, 0.0, 1.0);") != std::string::npos);
	CHECK(code.find("if (sela.a > 0.0 || any(greaterThan(sela.rgb, vec3(0.0)))) { sel_shadow *= 1.0 - sela.a; sel_over = sela.rgb; ALBEDO = w3d_to_linear(sel_shadow + sel_over); sel_ground = ALBEDO; }") !=
		std::string::npos);
	CHECK(code.find("float sel_keep = 1.0 - sel.a;") != std::string::npos);
	CHECK(code.find("(1.0 - sela.a) * (1.0 - sel.a)") == std::string::npos);
	CHECK(code.find("v_shadow_color = sel_shadow;") != std::string::npos);
	CHECK(code.find("v_shadow_over = sel_over;") != std::string::npos);
	CHECK(code.find("DIFFUSE_LIGHT += shadow_to_linear(v_shadow_color * mix(w3d_shadow.rgb, vec3(1.0), ATTENUATION) + v_shadow_over);") != std::string::npos);
	CHECK(code.find("EMISSION = ALBEDO - sel_ground * sel_keep * shadow_enabled;") != std::string::npos);
}

TEST_CASE("heroring1: under an object shadow the ring is drawn over the shadowed surface, as the shaders split it (review r2, #208)")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ hero(0, 0, 0, 0x80FFFFFFu) }, &Atlas::rect, twoPasses(), g, &err), err);
	const std::uint8_t white[3] = { 255, 255, 255 }, black[3] = { 0, 0, 0 };
	std::uint8_t out[3] = { 0, 0, 0 };
	// half shadowed white ground: 128 (1 - a) + texel x a, a = 128 / 255
	const float half[3] = { 128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f };
	REQUIRE(EvaluateSelectionDecals(g, a.image(), 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, white, out, half));
	CHECK(out[0] == 114);
	CHECK(out[1] == 139);
	CHECK(out[2] == 164);
	// fully shadowed: the ring's own share only, never below it
	const float none[3] = { 0.0f, 0.0f, 0.0f };
	REQUIRE(EvaluateSelectionDecals(g, a.image(), 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, white, out, none));
	CHECK(out[0] == 50);
	CHECK(out[1] == 75);
	CHECK(out[2] == 100);
	// over black any shadow is the same
	REQUIRE(EvaluateSelectionDecals(g, a.image(), 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain, black, out, half));
	CHECK(out[0] == 50);
	CHECK(out[1] == 75);
	CHECK(out[2] == 100);
}
