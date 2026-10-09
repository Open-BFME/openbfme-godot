// OpenBFME retail tests of the radar picture (lane HUD-2): a map's <stem>_art.tga is the radar picture (W3DRadar RW 0x44F3AB), the picture's rectangle
// (RW 0x6D89F2) and the ScrollShroud alpha of the shroud (RW 0x44E3B6). They SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MapClassification.h"
#include "GameClient/Radar.h"
#include "GameLogic/System/ShroudManager.h"

#include <array>
#include <set>

using namespace hudtest;

namespace
{
// an independent reading of a 32-bit uncompressed TGA, shown the way a TGA viewer shows it (the origin bit honoured): RGBA rows from the top
bool viewTga(const std::vector<std::uint8_t> &d, int &w, int &h, std::vector<std::uint8_t> &rgba)
{
	if (d.size() < 18 || d[2] != 2 || d[16] != 32)
	{
		return false;
	}
	w = d[12] | (d[13] << 8);
	h = d[14] | (d[15] << 8);
	const bool topDown = (d[17] & 0x20) != 0;
	const size_t base = 18 + d[0];
	if (d.size() < base + (size_t)w * (size_t)h * 4)
	{
		return false;
	}
	rgba.assign((size_t)w * (size_t)h * 4, 0);
	for (int row = 0; row < h; ++row)
	{
		const int stored = topDown ? row : h - 1 - row;
		for (int x = 0; x < w; ++x)
		{
			const std::uint8_t *s = &d[base + ((size_t)stored * (size_t)w + (size_t)x) * 4];
			std::uint8_t *o = &rgba[((size_t)row * (size_t)w + (size_t)x) * 4];
			o[0] = s[2];
			o[1] = s[1];
			o[2] = s[0];
			o[3] = s[3];
		}
	}
	return true;
}

struct RadarRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	RadarRig(SharedWorld &s, const char *map) : rig(s, map)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
};

void checkArt(SharedWorld &s, const char *map)
{
	RadarRig r(s, map);
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	CHECK(r.hud->errors().empty());
	const std::string expectedFile = std::string("maps\\") + map + "\\" + map + "_art.tga";
	CHECK(radar.artFile() == expectedFile);
	REQUIRE(radar.hasArt());
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(expectedFile, bytes, &err), err);
	int w = 0, h = 0;
	std::vector<std::uint8_t> expected;
	REQUIRE(viewTga(bytes, w, h, expected));
	CHECK(radar.artWidth() == w);
	CHECK(radar.artHeight() == h);
	REQUIRE(radar.artImage().size() == expected.size());
	size_t diff = 0, transparent = 0, drawn = 0;
	for (size_t i = 0; i < expected.size(); i += 4)
	{
		diff += std::equal(&expected[i], &expected[i] + 4, &radar.artImage()[i]) ? 0 : 1;
		transparent += expected[i + 3] == 0 ? 1 : 0;
		drawn += expected[i + 3] == 255 ? 1 : 0;
	}
	CHECK(diff == 0);
	// the art is a drawing over the movie's background: a large part is fully transparent, some of it opaque
	CHECK(transparent > expected.size() / 4 / 4);
	CHECK(drawn > 0);
}
} // namespace

TEST_CASE("hud2 radar: a map's <stem>_art.tga is the radar picture, pixel for pixel as the file shows it (fall back 4p, evendim)")
{
	if (!haveWorld("hud2 radar art"))
	{
		return;
	}
	CHECK(Radar::artFileFor("maps\\map mp evendim\\map mp evendim.map") == "maps\\map mp evendim\\map mp evendim_art.tga");
	CHECK(Radar::artFileFor("maps\\a.b\\c") == "maps\\a_art.tga"); // RW 0x44F3EF: the LAST '.' of the whole name
	checkArt(shared(), "map mp fall back 4p");
	checkArt(shared(), "map mp evendim");
}

TEST_CASE("hud2 radar: the picture's rectangle keeps the map's aspect inside the clip (RW 0x6D89F2)")
{
	if (!haveWorld("hud2 radar rect"))
	{
		return;
	}
	RadarRig r(shared(), "map mp fall back 4p");
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	float maxX = 0, maxY = 0;
	REQUIRE(r.rig.logic().terrain()->getExtent(0, maxX, maxY));
	int ul[2], lr[2];
	radar.drawRect(100, 50, 200, 200, ul, lr);
	if (maxX >= maxY)
	{
		const float s = 1.0f / (maxX / 200.0f);
		CHECK(ul[0] == 100);
		CHECK(ul[1] == 50 + (int)((200.0f - maxY * s) * 0.5f));
		CHECK(lr[0] == 100 + (int)(maxX * s));
		CHECK(lr[1] == 50 + 200 - (int)((200.0f - maxY * s) * 0.5f));
	}
	else
	{
		const float s = 1.0f / (maxY / 200.0f);
		CHECK(ul[1] == 50);
		CHECK(ul[0] == 100 + (int)((200.0f - maxX * s) * 0.5f));
	}
	// a wide clip: the picture is centred horizontally, full height
	radar.drawRect(0, 0, 400, 100, ul, lr);
	CHECK(ul[1] == 0);
	CHECK(lr[1] == (int)(maxY / (maxY / 100.0f)));
	CHECK(ul[0] > 0);
	CHECK(lr[0] - 400 == -ul[0]);
}

TEST_CASE("hud2 radar: the shroud is ScrollShroud's alpha per texel: 255 shrouded, 127 fogged, 0 where the local player sees")
{
	if (!haveWorld("hud2 radar shroud"))
	{
		return;
	}
	RadarRig r(shared(), "map mp fall back 4p");
	Radar &radar = r.hud->radar();
	ShroudManager &sm = r.rig.game->shroud();
	CHECK(radar.shroudAlpha().empty()); // no shroud shown: nothing over the picture
	sm.setDisplayed(true);
	const Coord3D c = r.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = r.rig.make("GondorBarracks", c.x, c.y);
	REQUIRE(b != nullptr);
	r.rig.frame(5);
	const std::vector<std::int16_t> a = radar.shroudAlpha();
	const int n = Radar::kShroudTexture;
	REQUIRE(a.size() == (size_t)n * (size_t)n);
	std::set<int> values;
	for (std::int16_t v : a)
	{
		values.insert(v);
	}
	for (int v : values)
	{
		CHECK_MESSAGE((v == 0 || v == 127 || v == 255 || v == -1), v);
	}
	// the barracks' texel: seen; the texel of a cell the local player has never seen: shrouded
	float maxX = 0, maxY = 0;
	REQUIRE(r.rig.logic().terrain()->getExtent(0, maxX, maxY));
	const float side = std::max(maxX, maxY);
	auto texel = [&](float wx, float wy) {
		// the texel of a world point on a square texture of the map's longer side, the shorter side centred (RW 0x6D8960), row 0 on top
		const float offX = (side - maxX) * 0.5f, offY = (side - maxY) * 0.5f;
		const int tx = std::clamp((int)((wx + offX) / side * n), 0, n - 1);
		const int ty = std::clamp((int)((wy + offY) / side * n), 0, n - 1);
		return a[(size_t)(n - 1 - ty) * (size_t)n + (size_t)tx];
	};
	CHECK(texel(c.x, c.y) == 0);
	const int me = r.rig.local->getPlayerIndex();
	bool foundShrouded = false;
	for (int cy = 0; cy < sm.cellCountY() && !foundShrouded; cy += 7)
	{
		for (int cx = 0; cx < sm.cellCountX() && !foundShrouded; cx += 7)
		{
			if (sm.getCellStatus(me, cx, cy) == CELLSHROUD_SHROUDED && cx > 0 && cy > 0 && cx + 1 < sm.cellCountX() && cy + 1 < sm.cellCountY())
			{
				bool allShrouded = true;
				for (int dy = -1; dy <= 1; ++dy)
				{
					for (int dx = -1; dx <= 1; ++dx)
					{
						allShrouded = allShrouded && sm.getCellStatus(me, cx + dx, cy + dy) == CELLSHROUD_SHROUDED;
					}
				}
				if (allShrouded)
				{
					foundShrouded = true;
					CHECK(texel(((float)cx + 0.5f) * sm.cellSize(), ((float)cy + 0.5f) * sm.cellSize()) == 255);
				}
			}
		}
	}
	CHECK(foundShrouded);
}

TEST_CASE("hud2 radar: the art TGA shows the same picture for each of the four origins (descriptor bits 0x20 top, 0x10 right)")
{
	// a 3 x 2 picture as it looks (row 0 on top, column 0 on the left): every pixel distinct
	const int w = 3, h = 2;
	auto pix = [](int x, int y) { return std::array<std::uint8_t, 4>{ (std::uint8_t)(10 + x), (std::uint8_t)(20 + y), (std::uint8_t)(30 + x * 2 + y), (std::uint8_t)(200 + x + y) }; };
	for (int origin = 0; origin < 4; ++origin)
	{
		const bool top = (origin & 2) != 0, right = (origin & 1) != 0;
		std::vector<std::uint8_t> tga(18, 0);
		tga[2] = 2;
		tga[12] = (std::uint8_t)w;
		tga[14] = (std::uint8_t)h;
		tga[16] = 32;
		tga[17] = (std::uint8_t)(8 | (top ? 0x20 : 0) | (right ? 0x10 : 0));
		for (int s = 0; s < h; ++s)
		{
			for (int c = 0; c < w; ++c)
			{
				// storage row s / column c holds the visible pixel of the origin's corner order
				const int y = top ? s : h - 1 - s, x = right ? w - 1 - c : c;
				const auto p = pix(x, y);
				tga.push_back(p[2]);
				tga.push_back(p[1]);
				tga.push_back(p[0]);
				tga.push_back(p[3]);
			}
		}
		int ow = 0, oh = 0;
		std::vector<std::uint8_t> rgba;
		std::string err;
		REQUIRE_MESSAGE(Radar::decodeArt(tga, ow, oh, rgba, &err), err);
		REQUIRE(ow == w);
		REQUIRE(oh == h);
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				const auto p = pix(x, y);
				const std::uint8_t *o = &rgba[((size_t)y * w + x) * 4];
				CHECK_MESSAGE((o[0] == p[0] && o[1] == p[1] && o[2] == p[2] && o[3] == p[3]), "origin " << origin << " pixel " << x << "," << y);
			}
		}
	}
}
