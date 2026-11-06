// OpenBFME tests of lane PLACEDECAL-1 (the building placement's decals showed faintly): the placement ghost's decal shadow, the resource claim decals and
// the placement claim decal (SHADOW_DECAL_TERRAIN_CLAIM, its alpha weighted per terrain vertex) are drawn by the terrain and wall shaders from the selection
// decals' grid (BuildMergeDecalGrid; godot/shaders/selection_decals.gdshaderinc sel_alpha_at, sel_mask_at) with RotWK's blended alpha decal shader (RW
// 0x509A1F: ShaderClass 0x1198B3, texture x diffuse). EvaluateSelectionDecals is the shaders' arithmetic in C++. Synthetic textures only. GPL-3.0.

#include "doctest.h"

#include "GameClient/SelectionDecals.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <cmath>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
// the atlas: "full" at x 0 (every texel rgb (100, 150, 200), alpha 255), "half" at x 64 (the same, the left half alpha 255, the right 0)
struct Atlas
{
	static constexpr int kW = 128, kH = 64;
	std::vector<std::uint8_t> px = std::vector<std::uint8_t>((size_t)kW * kH * 4, 0);
	Atlas()
	{
		for (int y = 0; y < 64; ++y)
		{
			for (int x = 0; x < kW; ++x)
			{
				std::uint8_t *p = &px[((size_t)y * kW + (size_t)x) * 4];
				p[0] = 100;
				p[1] = 150;
				p[2] = 200;
				p[3] = x < 96 ? 255 : 0; // "full" whole, "half" its left half
			}
		}
	}
	DecalImage image() const { return { kW, kH, px.data() }; }
	static bool rect(const std::string &name, MergeDecalAtlasRect &r)
	{
		const int k = name == "full" ? 0 : name == "half" ? 1 : -1;
		if (k < 0)
		{
			return false;
		}
		r = { k * 64, 0, 64, 64 };
		return true;
	}
};

SelectionDecal shadow(float x, float y, float size, const char *texture = "full")
{
	SelectionDecal d;
	d.position = { x, y, 0.0f };
	d.size = size;
	d.texture = texture;
	d.style = 0x20u; // SHADOW_ALPHA_DECAL
	d.color = 0xFFFFFFFFu;
	return d;
}

// a placement claim decal (SHADOW_DECAL_TERRAIN_CLAIM) 40 wide at (x, y), its diffuse white (RotWK's), the mask `w` x `h` vertices from (vx0, vy0)
SelectionDecal claim(float x, float y, int vx0, int vy0, int w, int h, std::vector<std::uint8_t> alpha)
{
	SelectionDecal d = shadow(x, y, 40.0f);
	d.style = 0x2000u;
	d.terrainOnly = true;
	d.maskVx0 = vx0;
	d.maskVy0 = vy0;
	d.maskW = w;
	d.maskH = h;
	d.maskAlpha = std::move(alpha);
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

Rgb at(const MergeDecalGrid &g, const Atlas &a, float x, float y, float z = 0.0f, DecalReceiver r = DecalReceiver::Terrain)
{
	const std::uint8_t s[3] = { 0, 0, 0 };
	std::uint8_t out[3] = { 0, 0, 0 };
	const bool drawn = EvaluateSelectionDecals(g, a.image(), x, y, z, r, s, out);
	return { out[0], out[1], out[2], drawn };
}
} // namespace

TEST_CASE("placedecal1: a placement claim decal goes into the grid as an alpha decal with its vertex mask after the index list; a terrain-only decal is marked")
{
	SelectionDecal ring = shadow(0, 0, 40.0f);
	SelectionDecal claims = shadow(100, 0, 40.0f);
	claims.terrainOnly = true;
	const std::vector<SelectionDecal> list = { ring, claim(0, 0, -2, -1, 3, 2, { 1, 2, 3, 4, 5, 6 }), claims };
	MergeDecalGrid g;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(list, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(err.empty());
	CHECK(g.decals == 0);
	CHECK(g.alphaDecals == 3);
	CHECK(g.maskDecals == 1);
	// the records' texel 3: the turn's cosine and sine, the y size (a square's: its size), then 0 (walls too), the mask's texel or -1 (terrain only)
	CHECK(g.texels[0 * 16 + 14] == 40.0f);
	CHECK(g.texels[0 * 16 + 15] == 0.0f);
	CHECK(g.texels[2 * 16 + 15] == -1.0f);
	// the mask's header right after the index list: every cell's merge and alpha entries
	size_t entries = 0;
	for (int c = 0; c < g.cellsX * g.cellsY; ++c)
	{
		const float *cell = &g.texels[((size_t)g.cellOffset + (size_t)c) * 4];
		entries += (size_t)cell[1] + (size_t)cell[3];
	}
	const float header = g.texels[1 * 16 + 15];
	REQUIRE(header == (float)((size_t)g.indexOffset + entries));
	const float *h = &g.texels[(size_t)header * 4];
	CHECK(h[0] == -2.0f);
	CHECK(h[1] == -1.0f);
	CHECK(h[2] == 3.0f);
	CHECK(h[3] == 2.0f);
	for (int k = 0; k < 6; ++k)
	{
		CHECK(h[4 + k * 4] == (float)(k + 1));
	}
	// the rows hold the header and its 6 alphas, the last texels written
	CHECK(g.rows == (int)(((size_t)header + 7 + MergeDecalGrid::kWidth - 1) / MergeDecalGrid::kWidth));
	// the shaders' mode: alpha decals only
	CHECK(SelectionDecalShaderMode(g) == 4);
}

TEST_CASE("placedecal1: a claim decal's mask must hold one alpha a vertex; without a mask SHADOW_DECAL_TERRAIN_CLAIM stays left out; the other decals are drawn")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	REQUIRE(BuildMergeDecalGrid({ claim(0, 0, 0, 0, 2, 2, { 255, 255, 255 }), shadow(0, 0, 40.0f) }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("3 alphas for 2 x 2 vertices") != std::string::npos);
	CHECK(g.alphaDecals == 1);
	CHECK(g.maskDecals == 0);
	CHECK(at(g, a, 0.0f, 0.0f).drawn);
	SelectionDecal bare = claim(0, 0, 0, 0, 0, 0, {});
	REQUIRE(BuildMergeDecalGrid({ bare }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("0x2000") != std::string::npos);
	CHECK(g.alphaDecals == 0);
	SelectionDecal missing = claim(0, 0, 0, 0, 1, 1, { 255 });
	missing.texture = "nope";
	REQUIRE(BuildMergeDecalGrid({ missing }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("nope") != std::string::npos);
	CHECK(g.alphaDecals == 0);
	// any decal's mask is checked (an alpha decal's too): a size that is not w x h, or a mask 0 high, is refused
	SelectionDecal ring = shadow(0, 0, 40.0f);
	ring.maskW = 2;
	ring.maskH = 0;
	REQUIRE(BuildMergeDecalGrid({ ring }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("0 alphas for 2 x 0 vertices") != std::string::npos);
	CHECK(g.alphaDecals == 0);
	ring.maskH = 2;
	ring.maskAlpha = { 255, 255, 255 };
	REQUIRE(BuildMergeDecalGrid({ ring }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(err.find("3 alphas for 2 x 2 vertices") != std::string::npos);
	CHECK(g.alphaDecals == 0);
	// the grid says which inputs went in (the HUD's counts of the resource decals)
	REQUIRE(BuildMergeDecalGrid({ bare, shadow(0, 0, 40.0f), ring, claim(0, 0, 0, 0, 1, 1, { 255 }) }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(g.drawn == std::vector<std::uint8_t>{ 0, 1, 0, 1 });
	// BuildSelectionDecalGrid (the HUD's): with a merge decal's texture missing the other decals are drawn, their flags at their input index
	SelectionDecal marker = shadow(0, 0, 40.0f, "nope");
	marker.style = 0x1000u; // SHADOW_MERGE_DECAL
	marker.texture2 = "full";
	REQUIRE(BuildSelectionDecalGrid({ marker, shadow(0, 0, 40.0f), claim(0, 0, 0, 0, 1, 1, { 255 }) }, &Atlas::rect, twoPasses(), g, &err));
	CHECK(g.decals == 0);
	CHECK(g.alphaDecals == 2);
	CHECK(g.drawn == std::vector<std::uint8_t>{ 0, 1, 1 });
}

TEST_CASE("placedecal1: the mask weights the decal's alpha, bilinear between the vertices (10 apart): a = texel alpha x opacity x mask / 255")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	// a uniform mask: 255 draws the texture whole, 128 half (as an opacity of 128), 0 nothing
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claim(0, 0, -3, -3, 7, 7, std::vector<std::uint8_t>(49, 255)) }, &Atlas::rect, twoPasses(), g, &err), err);
	Rgb c = at(g, a, 1.0f, 2.0f);
	CHECK(c.drawn);
	CHECK(c.r == 100);
	CHECK(c.g == 150);
	CHECK(c.b == 200);
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claim(0, 0, -3, -3, 7, 7, std::vector<std::uint8_t>(49, 128)) }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 1.0f, 2.0f);
	CHECK(c.r == 50);
	CHECK(c.g == 75);
	CHECK(c.b == 100);
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claim(0, 0, -3, -3, 7, 7, std::vector<std::uint8_t>(49, 0)) }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK_FALSE(at(g, a, 1.0f, 2.0f).drawn);
	// along x: vertex 3 (x 30) alpha 0, vertex 4 (x 40) alpha 255: x 35 half, x 32.5 a quarter; past the mask's last vertex its alpha holds
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claim(35, 0, 3, 0, 2, 1, { 0, 255 }) }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 35.0f, 0.0f);
	CHECK(c.r == 50);
	CHECK(c.g == 75);
	CHECK(c.b == 100);
	c = at(g, a, 32.5f, 0.0f);
	CHECK(c.r == 25);
	CHECK(c.g == 38); // 37.5 rounded
	CHECK(c.b == 50);
	c = at(g, a, 45.0f, 0.0f);
	CHECK(c.r == 100);
	CHECK_FALSE(at(g, a, 28.0f, 0.0f).drawn);
	// across a quad: (0, 0) 0, the other three 255: its centre 0.75
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claim(5, 5, 0, 0, 2, 2, { 0, 255, 255, 255 }) }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 5.0f, 5.0f);
	CHECK(c.r == 75);
	CHECK(c.b == 150);
	// the decal's opacity still applies (RotWK's diffuse alpha; the placement claim decal's is 255)
	SelectionDecal faded = claim(0, 0, -3, -3, 7, 7, std::vector<std::uint8_t>(49, 255));
	faded.color = 0x80FFFFFFu;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ faded }, &Atlas::rect, twoPasses(), g, &err), err);
	c = at(g, a, 1.0f, 2.0f);
	CHECK(c.r == 50);
}

TEST_CASE("placedecal1: a terrain-only decal (RotWK's Shadow + 0x64) draws on the terrain and on no wall top; a drawable's shadow draws on both")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	SelectionDecal claims = shadow(0, 0, 40.0f);
	claims.terrainOnly = true;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claims }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Wall).drawn);
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ claim(0, 0, -3, -3, 7, 7, std::vector<std::uint8_t>(49, 255)) }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Wall).drawn);
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ shadow(0, 0, 40.0f) }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, 0.0f, 0.0f, 0.0f, DecalReceiver::Terrain).drawn);
	CHECK(at(g, a, 0.0f, 0.0f, 10.0f, DecalReceiver::Wall).drawn);
}

TEST_CASE("placedecal1: a decal that is not square spans its x size along u and its y size along v; it is binned by its longer side")
{
	const Atlas a;
	MergeDecalGrid g;
	std::string err;
	SelectionDecal d = shadow(0, 0, 40.0f);
	d.sizeY = 20.0f;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ d }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(g.texels[14] == 20.0f);
	CHECK(at(g, a, 15.0f, 0.0f).drawn);
	CHECK(at(g, a, 0.0f, 9.0f).drawn);
	CHECK_FALSE(at(g, a, 0.0f, 11.0f).drawn);
	CHECK_FALSE(at(g, a, 0.0f, -11.0f).drawn);
	// a quarter turn: the x size along y
	d.angle = 1.57079633f;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ d }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, 0.0f, 15.0f).drawn);
	CHECK(at(g, a, 9.0f, 0.0f).drawn);
	CHECK_FALSE(at(g, a, 11.0f, 0.0f).drawn);
	// taller than wide: its far end (y 35) is in the grid and drawn
	SelectionDecal tall = shadow(0, 0, 20.0f);
	tall.sizeY = 80.0f;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ tall }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(g.minY == -40.0f);
	CHECK(at(g, a, 0.0f, 35.0f).drawn);
	CHECK(at(g, a, 0.0f, -35.0f).drawn);
	CHECK_FALSE(at(g, a, 11.0f, 0.0f).drawn);
	// "half": the texture's left half (u < 0.5) along x
	SelectionDecal half = shadow(0, 0, 40.0f, "half");
	half.sizeY = 10.0f;
	REQUIRE_MESSAGE(BuildMergeDecalGrid({ half }, &Atlas::rect, twoPasses(), g, &err), err);
	CHECK(at(g, a, -10.0f, 0.0f).drawn);
	CHECK_FALSE(at(g, a, 10.0f, 0.0f).drawn);
}

TEST_CASE("placedecal1: a drawable's decal shadow: ShadowOffsetX along u, ShadowOffsetY along v (addShadow negates them, RW 0x50C07E); white at Peak")
{
	const Coord3D p{ 100.0f, 200.0f, 7.0f };
	Coord3D c = DecalShadowCentre(p, 10.0f, 5.0f, 0.0f);
	CHECK(c.x == 110.0f);
	CHECK(c.y == 195.0f); // v runs toward -y at angle 0
	CHECK(c.z == 7.0f);
	c = DecalShadowCentre(p, 10.0f, 5.0f, 1.57079633f); // a quarter turn: u along +y, v along +x
	CHECK(std::fabs(c.x - 105.0f) < 1e-4f); // tolerance: cos(pi / 2) in float
	CHECK(std::fabs(c.y - 210.0f) < 1e-4f); // tolerance: as above
	CHECK(DecalShadowColor(0xFFu) == 0xFFFFFFFFu);
	CHECK(DecalShadowColor(0x80u) == 0x80FFFFFFu);
	CHECK(DecalShadowColor(300u) == 0x2CFFFFFFu); // the low byte (RW 0x7322AA)
	CHECK(DecalShadowColor(256u) == 0x00FFFFFFu);
	CHECK(ModelConditionInfo().shadowOpacityPeak == 0xFFu); // RW 0x4C30A8
	CHECK(ModelConditionInfo().shadowOpacityStart == 0u);
}

TEST_CASE("placedecal1: the terrain and wall shaders' sel_alpha_at reads the y size and the mask as EvaluateSelectionDecals does")
{
	// the shader text (godot/shaders/selection_decals.gdshaderinc): the mirror above is the C++ of these lines
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/../godot/shaders/selection_decals.gdshaderinc", std::ios::binary);
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string s = ss.str();
	CHECK(s.find("float sel_mask_at(int header, vec2 w)") != std::string::npos);
	CHECK(s.find("if (wall && (rec3.w != 0.0 || !(abs(w.z - rec1.w) <= SEL_MAX_HEIGHT)))") != std::string::npos);
	CHECK(s.find("vec2 uv = vec2((d.x * co + d.y * si) / rec0.z + 0.5, (d.x * si - d.y * co) / rec3.z + 0.5);") != std::string::npos);
	CHECK(s.find("a *= sel_mask_at(int(rec3.w), w.xy) / 255.0;") != std::string::npos);
	CHECK(s.find("float fx = clamp(w.x / 10.0 - h.x, 0.0, float(mw - 1));") != std::string::npos);
}
