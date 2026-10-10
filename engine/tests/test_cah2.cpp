// OpenBFME unit tests. GPL-3.0.
// Lane CAH-2: the Create-a-Hero save folder (GameClient/UserDataFolder.h) and the builder's message boxes.

#include "doctest.h"
#include "RetailTestMount.h"
#include "AptRetail.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include "GameClient/UserDataFolder.h"
#include "GameClient/GUI/AptColorPicker.h"
#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"

#include <algorithm>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> giDat(const std::vector<std::pair<std::string, std::string>> &pairs, int count = -1)
{
	std::vector<std::uint8_t> b = { 'G', 'I', ' ', ' ' };
	const std::uint32_t n = count < 0 ? (std::uint32_t)pairs.size() : (std::uint32_t)count;
	for (int i = 0; i < 4; ++i)
	{
		b.push_back((std::uint8_t)(n >> (8 * i)));
	}
	for (const auto &p : pairs)
	{
		b.insert(b.end(), p.first.begin(), p.first.end());
		b.push_back(0);
		b.insert(b.end(), p.second.begin(), p.second.end());
		b.push_back(0);
	}
	return b;
}
} // namespace

TEST_CASE("cah2 user data: gi.dat's known keys are kept without case (RW 0xAAA5A0), others ignored; a bad image is an error")
{
	std::map<std::string, std::string> v;
	std::string error;
	REQUIRE(UserDataFolder::parseGameInfo(giDat({ { "skuname", "x" }, { "USERDATALEAFNAME", "My Mod Files" }, { "Other", "y" } }), v, &error));
	CHECK(v.size() == 2);
	CHECK(v["SkuName"] == "x");
	CHECK(v["UserDataLeafName"] == "My Mod Files");
	CHECK(UserDataFolder::leafName(giDat({ { "UserDataLeafName", "Leaf" } }), &error) == "Leaf");
	// errors: no magic, a count past the strings, no leaf name
	std::vector<std::uint8_t> bad = giDat({ { "UserDataLeafName", "Leaf" } });
	bad[0] = 'X';
	CHECK_FALSE(UserDataFolder::parseGameInfo(bad, v, &error));
	CHECK(error.find("magic") != std::string::npos);
	CHECK_FALSE(UserDataFolder::parseGameInfo(giDat({ { "UserDataLeafName", "Leaf" } }, 2), v, &error));
	CHECK(UserDataFolder::leafName(giDat({ { "GameName", "g" } }), &error).empty());
	CHECK(error == "gi.dat: no UserDataLeafName");
	// RW 0x644148 / 0x6DD398: <AppData>\<leaf>\Save\ (a separator is not doubled)
	CHECK(UserDataFolder::userDataFolder("D:\\AppData\\Roaming", "Leaf") == "D:\\AppData\\Roaming\\Leaf\\");
	CHECK(UserDataFolder::userDataFolder("/data/share/", "Leaf", '/') == "/data/share/Leaf/");
	CHECK(UserDataFolder::heroSaveFolder(UserDataFolder::userDataFolder("C:\\A", "Leaf")) == "C:\\A\\Leaf\\Save\\");
}

TEST_CASE("cah2 user data retail: RotWK 2.01's gi.dat names the folder \"My The Lord of the Rings, The Rise of the Witch-king Files\"")
{
	const char *install = std::getenv("ROTWK_INSTALL");
	if (!install || !*install)
	{
		retailtest::printSkip("cah2 user data retail");
		return;
	}
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(std::string(install) + "/gi.dat", bytes, &error), error);
	const std::string leaf = UserDataFolder::leafName(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), &error);
	CHECK_MESSAGE(leaf == "My The Lord of the Rings, The Rise of the Witch-king Files", error);
}

TEST_CASE("cah2 house colour: RW 0x531C77's texel recolour for 1 .. 3 colours (R * c0 + G * c1 + B * c2 per channel, >> 8 each, clamped; alpha kept)")
{
	HouseColorParams p;
	p.colors[0] = 0xFF804020u; // R 0x80, G 0x40, B 0x20
	p.colors[1] = 0xFF00FF00u;
	p.colors[2] = 0xFFFFFFFFu;
	const std::uint8_t in[4] = { 200, 100, 50, 77 }; // R G B A
	std::uint8_t out[4] = {};
	p.kind = 0;
	Recolor_House_Texel(p, in, out);
	CHECK(out[0] == 200);
	CHECK(out[3] == 77);
	p.kind = 1;
	Recolor_House_Texel(p, in, out);
	CHECK(out[0] == (200 * 0x80) >> 8);
	CHECK(out[1] == (200 * 0x40) >> 8);
	CHECK(out[2] == (200 * 0x20) >> 8);
	CHECK(out[3] == 77);
	p.kind = 2;
	Recolor_House_Texel(p, in, out);
	CHECK(out[0] == ((200 * 0x80) >> 8) + 0);
	CHECK(out[1] == ((200 * 0x40) >> 8) + ((100 * 0xFF) >> 8));
	p.kind = 3;
	Recolor_House_Texel(p, in, out);
	CHECK(out[0] == std::min(255, ((200 * 0x80) >> 8) + 0 + ((50 * 0xFF) >> 8)));
	CHECK(out[1] == std::min(255, ((200 * 0x40) >> 8) + ((100 * 0xFF) >> 8) + ((50 * 0xFF) >> 8)));
	const std::uint8_t white[4] = { 255, 255, 255, 255 };
	Recolor_House_Texel(p, white, out);
	CHECK(out[1] == 255); // clamped
}

TEST_CASE("cah2 color picker: the texel read as ARGB and the nearest texel by the sum of squared byte differences, the first one row by row (RW 0xB5500E)")
{
	// a 3 x 2 texture, RGBA rows top first
	std::vector<std::uint8_t> rgba = {
		10, 20, 30, 255, 200, 0, 0, 255, 0, 200, 0, 255,
		0, 0, 200, 255, 200, 0, 0, 255, 9, 9, 9, 0,
	};
	std::uint32_t argb = 0;
	REQUIRE(AptColorPickers::texelAt(rgba, 3, 2, 1, 0, argb));
	CHECK(argb == 0xFFC80000u);
	CHECK_FALSE(AptColorPickers::texelAt(rgba, 3, 2, 3, 0, argb));
	int bx = -1, by = -1;
	REQUIRE(AptColorPickers::nearestTexel(rgba, 3, 2, 0, 0, 3, 2, 0xFFC80000u, bx, by));
	CHECK(bx == 1); // (1, 0) and (1, 1) tie: the first row wins
	CHECK(by == 0);
	REQUIRE(AptColorPickers::nearestTexel(rgba, 3, 2, 0, 0, 3, 2, 0xFF0000C1u, bx, by));
	CHECK(bx == 0);
	CHECK(by == 1);
	REQUIRE(AptColorPickers::nearestTexel(rgba, 3, 2, 1, 1, 2, 1, 0xFF0A141Eu, bx, by)); // a sub-rectangle: places relative to it
	CHECK(by == 0);
}

TEST_CASE("cah2 retail: CahAppearance places StatName in its rows but no StatVal: the rows' `StatVal.text = $MyHeroAppearanceVal_<n>` writes to nothing in retail too (S-1408)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::string err;
	std::shared_ptr<const AptFile> f = loader.loadMovie("cahappearance", &err);
	REQUIRE_MESSAGE((bool)f, err);
	int statName = 0, statVal = 0;
	auto scan = [&](const std::vector<AptFrame> &frames) {
		for (const AptFrame &fr : frames)
		{
			for (const AptFrameItem &it : fr.items)
			{
				if (it.place && it.place->name == "StatName")
				{
					++statName;
				}
				if (it.place && it.place->name == "StatVal")
				{
					++statVal;
				}
			}
		}
	};
	scan(f->frames);
	for (const AptCharacter &c : f->characters)
	{
		scan(c.frames);
		if (c.text && c.text->variableName.find("StatVal") != std::string::npos)
		{
			++statVal; // a text field bound to a variable of that name would show it too
		}
	}
	CHECK(statName > 0);
	CHECK(statVal == 0);
}

TEST_CASE("cah2 house colour: the recolour runs on the texels before filtering (Sol r2 probe: two texels, the midpoint 127.5 as retail, not 254)")
{
	// kind 2 with two white colours; texel A (R 255, G 255) recolours to min(255, 254 + 254) = 255, texel B (0, 0) to 0
	HouseColorParams p;
	p.kind = 2;
	p.colors[0] = 0xFFFFFFFFu;
	p.colors[1] = 0xFFFFFFFFu;
	std::uint8_t img[8] = { 255, 255, 0, 255, 0, 0, 0, 255 };
	Recolor_House_Pixels(p, img, 2);
	CHECK(img[0] == 255);
	CHECK(img[4] == 0);
	CHECK(img[3] == 255); // alpha kept
	// a bilinear sample half-way between them, after the bake (the device's order): 127.5
	const double baked = (img[0] + img[4]) / 2.0;
	CHECK(baked == doctest::Approx(127.5));
	// the order the r2 shader had: filter the source first (R = G = 127.5, rounded to 128), then recolour: 254
	const int h = 128;
	const int shaderFirst = std::min(255, ((h * 255) >> 8) + ((h * 255) >> 8));
	CHECK(shaderFirst == 254);
	CHECK(baked != doctest::Approx((double)shaderFirst));
}
