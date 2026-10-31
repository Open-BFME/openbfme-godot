// OpenBFME unit tests. GPL-3.0.
// Lane MENUFIX-1: the Options screen's version line as RotWK shows it. RW 0x6446A1 formats the game text Version:Format2 ("Version %d.%02d") with the
// build's major and minor version (2, 1: RW 0x644CC2 parses "VERSION=2.1.2614.37001"), so retail RotWK 2.01 shows "Version 2.01". Before this lane the
// port dropped the "%02d" width and showed "Version 2.1". And the font of an Apt text field under a horizontal stretch (AptTextFontScale): the skirmish
// loading screen's player names were drawn at 1.84 times the other columns. The retail-data cases SKIP loudly without the installs.

#include "doctest.h"
#include "AptRetail.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/GameTextSource.h"
#include "Libraries/Source/Apt/AptFile.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct OneText : GameTextSource
{
	std::map<std::string, std::u16string> texts;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = texts.find(label);
		if (it == texts.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
};
} // namespace

TEST_CASE("menufix1 version: Version:Format2's %02d keeps its width (RotWK 2.01 shows Version 2.01)")
{
	OneText text;
	text.texts["Version:Format2"] = u"Version %d.%02d"; // lotr.str's Version:Format2 (RotWK 2.01)
	CHECK(AptOptionsScreen::versionText(&text) == u"Version 2.01");
	text.texts["Version:Format2"] = u"V%d-%3d|%%"; // non-retail widths remain literal and do not consume arguments
	CHECK(AptOptionsScreen::versionText(&text) == u"V2-%3d|%");
	text.texts["Version:Format2"] = u"%-3d|%d"; // non-retail alignment is literal too
	CHECK(AptOptionsScreen::versionText(&text) == u"%-3d|2");
	OneText none;
	CHECK(AptOptionsScreen::versionText(&none) == u"Version:Format2"); // a missing label shows as the label
}

TEST_CASE("menufix1 version retail: the 2.01 string file's Version:Format2 gives Version 2.01")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	std::vector<std::uint8_t> str;
	std::string error;
	REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", str, &error), error);
	GameTextTableSource text;
	REQUIRE_MESSAGE(text.table.parse(str, &error), error);
	CHECK(AptOptionsScreen::versionText(&text) == u"Version 2.01");
}

TEST_CASE("menufix1 text scale: a field's font follows its vertical scale; a horizontal stretch (_width) widens the box only")
{
	AptMatrix m;
	CHECK(AptTextFontScale(m) == 1.0f);
	m.a = 2.0f;
	m.d = 2.0f;
	CHECK(AptTextFontScale(m) == 2.0f); // uniform: as sqrt(|det|) before
	m.a = 3.5f;
	m.d = 1.0f;
	CHECK(AptTextFontScale(m) == 1.0f); // LoadScreen.apt's PlayerName row: _width 160 on a 47-unit field (the old rule: sqrt(3.5) = 1.87)
	m.a = 1.0f;
	m.d = -1.5f;
	CHECK(AptTextFontScale(m) == 1.5f); // mirrored vertically
	m.a = 0.0f; // rotated a quarter turn, scale 2: the vertical unit vector (c, d) = (-2, 0)
	m.b = 2.0f;
	m.c = -2.0f;
	m.d = 0.0f;
	CHECK(AptTextFontScale(m) == 2.0f);
}

TEST_CASE("menufix1 text scale retail: LoadScreen.apt's row field (TheString) keeps its font whatever width the row gives it")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::string error;
	std::shared_ptr<const AptFile> f = loader.loadMovie("LoadScreen", &error);
	const bool loaded = f != nullptr;
	REQUIRE_MESSAGE(loaded, error);
	// the row component: a sprite placing the edit text "TheString" (character 2: Albertus MT 14, box -2 .. 45: 47 units wide), matrix (1, 0, 0, 1.002)
	const AptPlaceObject *place = nullptr;
	for (const AptCharacter &c : f->characters)
	{
		for (const AptFrame &fr : c.frames)
		{
			for (const AptFrameItem &it : fr.items)
			{
				if (it.place && it.place->name == "TheString")
				{
					place = it.place.get();
				}
			}
		}
	}
	REQUIRE(place != nullptr);
	REQUIRE(place->characterId == 2);
	const AptTextInfo &field = *f->characters[2].text;
	CHECK(field.fontHeight == 14.0f);
	CHECK(field.bounds[2] - field.bounds[0] == 47.0f);
	// the rows' stringWidth (the movie's InitActions): PlayerName 160, ArmyName 70, Rank / TeamNumber 60; `_width = stringWidth` sets the x scale only
	// (RW 0xB03197, AptCharacterInst::setProperty): the font scale is the placement's vertical 1.002 for every column
	for (float width : { 160.0f, 70.0f, 60.0f })
	{
		AptMatrix m;
		m.a = width / 47.0f * place->matrix[0];
		m.d = place->matrix[3];
		CHECK(AptTextFontScale(m) == std::fabs(place->matrix[3]));
	}
}
