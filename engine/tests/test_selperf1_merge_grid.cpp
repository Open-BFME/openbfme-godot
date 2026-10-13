// OpenBFME tests of the merge decals' GPU grid (lane SELPERF-1, GameClient/SelectionDecals.h): BuildMergeDecalGrid bins the decals in the list's order and
// EvaluateMergeDecalGrid (the terrain shader's arithmetic, godot/shaders/selection_decals.gdshaderinc) draws what ComposeMergeDecals, the CPU
// reference of RotWK's two stencil passes, composes. Synthetic textures only. GPL-3.0.

#include "doctest.h"

#include "GameClient/SelectionDecals.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
// a 64 x 64 disc of radius r, alpha 255 inside, a soft one-texel edge (the bilinear alpha test sees in-between values), its rgb a gradient
void disc(std::vector<std::uint8_t> &atlas, int atlasW, int ox, float r)
{
	for (int y = 0; y < 64; ++y)
	{
		for (int x = 0; x < 64; ++x)
		{
			const float dx = (float)x + 0.5f - 32.0f, dy = (float)y + 0.5f - 32.0f;
			const float d = std::sqrt(dx * dx + dy * dy);
			std::uint8_t *p = &atlas[((size_t)y * (size_t)atlasW + (size_t)(ox + x)) * 4];
			p[0] = (std::uint8_t)(128 + x);
			p[1] = (std::uint8_t)(255 - y);
			p[2] = 200;
			p[3] = d <= r ? 255 : (d <= r + 1.0f ? (std::uint8_t)(255.0f * (r + 1.0f - d)) : 0);
		}
	}
}

struct Fixture
{
	static constexpr int kW = 128, kH = 64;
	std::vector<std::uint8_t> atlas = std::vector<std::uint8_t>((size_t)kW * kH * 4, 0);
	std::vector<std::vector<std::uint8_t>> own; // each texture on its own, for ComposeMergeDecals
	Fixture()
	{
		disc(atlas, kW, 0, 30.0f);  // "rim" at x 0
		disc(atlas, kW, 64, 24.0f); // "cut" at x 64
		for (int t = 0; t < 2; ++t)
		{
			std::vector<std::uint8_t> px((size_t)64 * 64 * 4);
			for (int y = 0; y < 64; ++y)
			{
				for (int x = 0; x < 64; ++x)
				{
					for (int c = 0; c < 4; ++c)
					{
						px[((size_t)y * 64 + (size_t)x) * 4 + (size_t)c] = atlas[((size_t)y * kW + (size_t)(t * 64 + x)) * 4 + (size_t)c];
					}
				}
			}
			own.push_back(px);
		}
	}
	DecalImage image(const std::string &name) const
	{
		if (name == "rim")
		{
			return { 64, 64, own[0].data() };
		}
		if (name == "cut")
		{
			return { 64, 64, own[1].data() };
		}
		return {};
	}
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

SelectionDecal member(float x, float y, float size, std::uint32_t color)
{
	SelectionDecal d;
	d.position = { x, y, 0.0f };
	d.size = size;
	d.texture = "rim";
	d.texture2 = "cut";
	d.style = 0x1000u;
	d.color = color;
	return d;
}

// a block of hordes: rows of members 11 units apart, two decal sizes, two colours, the opacity of the throb
std::vector<SelectionDecal> block(int n)
{
	std::vector<SelectionDecal> out;
	unsigned seed = 12345;
	for (int i = 0; i < n; ++i)
	{
		seed = seed * 1103515245u + 12345u;
		const float jitter = (float)((seed >> 16) % 100) / 25.0f;
		out.push_back(member((float)(i % 17) * 11.0f + jitter, (float)(i / 17) * 13.0f - jitter, (i % 5) == 0 ? 64.0f : 40.0f,
			((i % 3) == 0 ? 0xCC3050E0u : 0xE6E04030u)));
	}
	return out;
}

// every pixel centre of the reference image: the grid's result must be the composed pixel (alpha exactly, the colour within one step)
void compare(const std::vector<SelectionDecal> &list, const SelectionDecalSettings &s, size_t &pixels, size_t &drawn, size_t &mismatches)
{
	Fixture f;
	MergedSelectionDecals ref;
	std::string err;
	REQUIRE_MESSAGE(ComposeMergeDecals(list, [&](const std::string &n) { return f.image(n); }, s, ref, &err), err);
	MergeDecalGrid grid;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(list, &Fixture::rect, s, grid, &err), err);
	CHECK(grid.decals == ref.decals);
	const DecalImage atlas{ Fixture::kW, Fixture::kH, f.atlas.data() };
	// ComposeMergeDecals' density: the textures' own (64 texels over the smallest decal, 40 units), at most 1024 pixels a side
	const float density = std::min(64.0f / 40.0f, 1024.0f / std::max(ref.maxX - ref.minX, ref.maxY - ref.minY));
	pixels = drawn = mismatches = 0;
	for (int py = 0; py < ref.height; ++py)
	{
		const float y = ref.maxY - ((float)py + 0.5f) / density;
		for (int px = 0; px < ref.width; ++px)
		{
			const float x = ref.minX + ((float)px + 0.5f) / density;
			const std::uint8_t *r = &ref.rgba[((size_t)py * (size_t)ref.width + (size_t)px) * 4];
			std::uint8_t g[4] = { 0, 0, 0, 0 };
			const bool on = EvaluateMergeDecalGrid(grid, atlas, x, y, g);
			++pixels;
			drawn += r[3] != 0 ? 1 : 0;
			bool same = on == (r[3] != 0);
			if (same && on)
			{
				// the simple pass's alpha is the blend weight: one step apart where the two round a product at .5 differently
				same = std::abs((int)g[3] - (int)r[3]) <= 1 && std::abs((int)g[0] - (int)r[0]) <= 1 && std::abs((int)g[1] - (int)r[1]) <= 1 && std::abs((int)g[2] - (int)r[2]) <= 1;
			}
			if (!same && mismatches < 8)
			{
				MESSAGE("at (" << x << ", " << y << ") ref " << (int)r[0] << "," << (int)r[1] << "," << (int)r[2] << "," << (int)r[3] << " grid " << on << " "
					<< (int)g[0] << "," << (int)g[1] << "," << (int)g[2] << "," << (int)g[3]);
			}
			mismatches += same ? 0 : 1;
		}
	}
}
} // namespace

TEST_CASE("selperf1: the merge decal grid keeps every decal in each cell it touches, in the list's order")
{
	const std::vector<SelectionDecal> list = block(300);
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	MergeDecalGrid g;
	std::string err;
	REQUIRE_MESSAGE(BuildMergeDecalGrid(list, &Fixture::rect, s, g, &err), err);
	CHECK(g.decals == 300);
	CHECK(g.cellSize == doctest::Approx(40.0f)); // the median decal (one in five is 64 wide)
	CHECK(g.cellsX <= MergeDecalGrid::kMaxCellsPerAxis);
	CHECK(g.rows * MergeDecalGrid::kWidth >= g.indexOffset);
	CHECK(g.texels.size() == (size_t)g.rows * MergeDecalGrid::kWidth * 4);
	for (int c = 0; c < g.cellsX * g.cellsY; ++c)
	{
		const float *cell = &g.texels[((size_t)g.cellOffset + (size_t)c) * 4];
		const int first = (int)cell[0], count = (int)cell[1];
		CHECK(count <= g.maxPerCell);
		int prev = -1;
		for (int k = 0; k < count; ++k)
		{
			const float *e = &g.texels[((size_t)g.indexOffset + (size_t)(first + k)) * 4];
			const int i = (int)e[3];
			CHECK(e[0] == list[(size_t)i].position.x); // the entry repeats the decal's place and size
			CHECK(e[2] == list[(size_t)i].size);
			CHECK(i > prev); // the list's order
			prev = i;
			// the decal's square touches the cell
			const float h = list[(size_t)i].size * 0.5f;
			const float cx0 = g.minX + (float)(c % g.cellsX) * g.cellSize, cy0 = g.minY + (float)(c / g.cellsX) * g.cellSize;
			CHECK(list[(size_t)i].position.x + h >= cx0 - 0.01f);
			CHECK(list[(size_t)i].position.x - h <= cx0 + g.cellSize + 0.01f);
			CHECK(list[(size_t)i].position.y + h >= cy0 - 0.01f);
			CHECK(list[(size_t)i].position.y - h <= cy0 + g.cellSize + 0.01f);
		}
	}
	// the decal texels: place, size, opacity, colour, both atlas rects
	CHECK(g.texels[0] == list[0].position.x);
	CHECK(g.texels[2] == list[0].size);
	CHECK(g.texels[3] == (float)0xCC);
	CHECK(g.texels[4] == (float)0x30);
	CHECK(g.texels[12] == 64.0f); // Texture2 "cut" at atlas x 64
	// a missing texture is an error, as in ComposeMergeDecals
	std::vector<SelectionDecal> bad = list;
	bad[7].texture2 = "nope";
	CHECK_FALSE(BuildMergeDecalGrid(bad, &Fixture::rect, s, g, &err));
	CHECK(err.find("nope") != std::string::npos);
	// nothing to merge: no decal, no texels (the shader draws nothing)
	std::vector<SelectionDecal> alpha = list;
	for (SelectionDecal &d : alpha)
	{
		d.style = 0x20u;
	}
	REQUIRE(BuildMergeDecalGrid(alpha, &Fixture::rect, s, g, &err));
	CHECK(g.decals == 0);
	CHECK(g.rows == 0);
}

TEST_CASE("selperf1: the shader's two stencil passes draw what ComposeMergeDecals composes (one outline around the block, nothing inside)")
{
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	s.useSimpleMergeDecals = false;
	size_t pixels = 0, drawn = 0, mismatches = 0;
	compare(block(250), s, pixels, drawn, mismatches);
	MESSAGE("two passes: " << pixels << " pixels, " << drawn << " drawn, " << mismatches << " differ");
	CHECK(drawn > 1000);
	CHECK(mismatches <= pixels / 2000); // float rounding at the alpha test's edge only
}

TEST_CASE("selperf1: the shader's simple pass draws what ComposeMergeDecals composes (each pixel once, the first disc, blended)")
{
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	s.useSimpleMergeDecals = true;
	s.opacityOfSimpleMergeDecals = 0.5f;
	size_t pixels = 0, drawn = 0, mismatches = 0;
	compare(block(250), s, pixels, drawn, mismatches);
	MESSAGE("simple: " << pixels << " pixels, " << drawn << " drawn, " << mismatches << " differ");
	CHECK(drawn > 1000);
	CHECK(mismatches <= pixels / 2000);
}

// ---- the per-frame refresh (retail data) ----------------------------------------------------------------------------------------------------------------

#include "HudTestUtil.h"

#include "GameLogic/Object/Object.h"

TEST_CASE("selperf1 retail: between two logic frames RefreshSelectionDecals gives the markers BuildSelectionDecals would (the throb, the drawables' places)")
{
	using namespace hudtest;
	if (!haveWorld("selperf1 refresh"))
	{
		return;
	}
	Rig rig(shared());
	const Coord3D c = rig.freeSpot(2800, 1400, 300.0f);
	Object *hero = rig.make("GondorBoromir", c.x, c.y);
	Object *horde = rig.make("GondorFighterHorde", c.x - 200.0f, c.y);
	Object *horde2 = rig.make("GondorArcherHorde", c.x - 200.0f, c.y + 150.0f);
	rig.frame(30); // the hordes spawn their members
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	const std::vector<ObjectID> sel{ hero->getID(), horde->getID(), horde2->getID() };
	std::vector<SelectionDecal> refreshed;
	BuildSelectionDecals(rig.logic(), &rig.game->drawables(), rig.local, sel, s, 3, refreshed);
	REQUIRE(refreshed.size() > 10);
	CHECK_FALSE(RefreshSelectionDecals(&rig.game->drawables(), s, 3, refreshed)); // the same frame and places: nothing changed (the device keeps its grid)
	for (unsigned clientFrame : { 4u, 17u, 45u, 46u, 200u })
	{
		RefreshSelectionDecals(&rig.game->drawables(), s, clientFrame, refreshed);
		const std::vector<SelectionDecal> built = BuildSelectionDecals(rig.logic(), &rig.game->drawables(), rig.local, sel, s, clientFrame);
		REQUIRE(built.size() == refreshed.size());
		for (size_t i = 0; i < built.size(); ++i)
		{
			CHECK(built[i].object == refreshed[i].object);
			CHECK(built[i].color == refreshed[i].color);
			CHECK(built[i].angle == refreshed[i].angle);
			CHECK(built[i].size == refreshed[i].size);
			CHECK(built[i].position.x == refreshed[i].position.x);
			CHECK(built[i].position.y == refreshed[i].position.y);
			CHECK(built[i].texture == refreshed[i].texture);
		}
	}
	// the in-game UI hidden: the refresh keeps the markers at opacity 0, as the build does
	SelectionDecalSettings hidden = s;
	hidden.drawIconUI = false;
	RefreshSelectionDecals(&rig.game->drawables(), hidden, 50, refreshed);
	for (const SelectionDecal &d : refreshed)
	{
		CHECK((d.color >> 24) == 0);
	}
}

TEST_CASE("selperf1: the merge decal atlas is committed whole: a missing texture leaves the others' places valid, and joins once it exists")
{
	Fixture f;
	bool haveLate = false;
	std::vector<std::uint8_t> late((size_t)32 * 32 * 4, 255);
	auto image = [&](const std::string &n) -> DecalImage {
		if (n == "late")
		{
			return haveLate ? DecalImage{ 32, 32, late.data() } : DecalImage{};
		}
		return f.image(n);
	};
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	MergeDecalAtlas atlas;
	std::vector<std::string> errors;
	auto rect = [&](const std::string &n, MergeDecalAtlasRect &r) { return atlas.rect(n, r); };
	// a selection whose second unit's texture is missing: the atlas takes the textures that exist, the grid fails with the missing one's name
	std::vector<SelectionDecal> bad{ member(0.0f, 0.0f, 40.0f, 0xCC3050E0u), member(30.0f, 0.0f, 40.0f, 0xCC3050E0u) };
	bad[1].texture = "late";
	CHECK(atlas.update(bad, s, image, errors));
	CHECK(atlas.textures() == 2);
	MergeDecalAtlasRect r;
	REQUIRE(atlas.rect("rim", r));
	CHECK(r.w == 64);
	CHECK(r.h == 64);
	REQUIRE(atlas.rect("cut", r));
	CHECK(r.w == 64);
	CHECK_FALSE(atlas.rect("late", r));
	MergeDecalGrid g;
	std::string err;
	CHECK_FALSE(BuildMergeDecalGrid(bad, rect, s, g, &err));
	CHECK(err.find("late") != std::string::npos);
	// the bad selection cleared: the good one draws from the same atlas (nothing to repack)
	const std::vector<SelectionDecal> good{ member(0.0f, 0.0f, 40.0f, 0xCC3050E0u) };
	CHECK_FALSE(atlas.update(good, s, image, errors));
	CHECK(BuildMergeDecalGrid(good, rect, s, g, &err));
	CHECK(g.decals == 1);
	// the missing texture appears: it joins, the others keep valid places and pixels
	haveLate = true;
	CHECK(atlas.update(bad, s, image, errors));
	CHECK(atlas.textures() == 3);
	REQUIRE(atlas.rect("late", r));
	CHECK(r.w == 32);
	CHECK(atlas.pixels()[((size_t)r.y * MergeDecalAtlas::kWidth + (size_t)r.x) * 4 + 3] == 255);
	REQUIRE(atlas.rect("rim", r));
	CHECK(atlas.pixels()[(((size_t)r.y + 32) * MergeDecalAtlas::kWidth + (size_t)r.x + 32) * 4 + 3] == 255); // the rim disc's centre
	CHECK(BuildMergeDecalGrid(bad, rect, s, g, &err));
	CHECK(errors.empty());
	// a texture wider than the atlas: an error once, never in it
	std::vector<std::uint8_t> wide((size_t)2048 * 4 * 4, 255);
	auto withWide = [&](const std::string &n) -> DecalImage { return n == "wide" ? DecalImage{ 2048, 4, wide.data() } : image(n); };
	std::vector<SelectionDecal> w = good;
	w[0].texture = "wide";
	CHECK_FALSE(atlas.update(w, s, withWide, errors));
	CHECK(errors.size() == 1);
	CHECK_FALSE(atlas.update(w, s, withWide, errors));
	CHECK(errors.size() == 1);
	CHECK_FALSE(atlas.rect("wide", r));
}
