// OpenBFME unit tests. GPL-3.0.
//
// Lane UI-6: a Discord tester's menu findings on v0.3.0-preview.6 (GitGut, 2026-10-11): the text placement of RotWK's GDI fonts
// (GameClient/GdiFontMetrics.h), the kept skirmish setup (Skirmish.ini's game record) and the widescreen view.

#include "doctest.h"

#include "RetailTestMount.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/GdiFontMetrics.h"
#include "GameClient/TacticalCamera.h"
#include "GameClient/GUI/Skirmish/SkirmishGameRecord.h"
#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
void put16(std::vector<std::uint8_t> &b, std::size_t at, int v)
{
	b[at] = (std::uint8_t)((v >> 8) & 0xFF);
	b[at + 1] = (std::uint8_t)(v & 0xFF);
}

void put32(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		b[at + (std::size_t)i] = (std::uint8_t)(v >> (24 - 8 * i));
	}
}

// a font file with only the head, hhea and OS/2 tables the metrics read
std::vector<std::uint8_t> fakeFont(int upem, int hheaAscent, int hheaDescent, int lineGap, int winAscent, int winDescent, bool withOs2 = true)
{
	const int tables = withOs2 ? 3 : 2;
	const std::size_t dir = 12 + 16 * (std::size_t)tables;
	std::vector<std::uint8_t> b(dir + 54 + 36 + (withOs2 ? 78 : 0), 0);
	put16(b, 4, tables);
	std::size_t at = dir;
	auto record = [&](int i, const char *tag, std::size_t length) {
		const std::size_t r = 12 + 16 * (std::size_t)i;
		for (int k = 0; k < 4; ++k)
		{
			b[r + (std::size_t)k] = (std::uint8_t)tag[k];
		}
		put32(b, r + 8, (std::uint32_t)at);
		put32(b, r + 12, (std::uint32_t)length);
		const std::size_t start = at;
		at += length;
		return start;
	};
	const std::size_t head = record(0, "head", 54);
	put16(b, head + 18, upem);
	const std::size_t hhea = record(1, "hhea", 36);
	put16(b, hhea + 4, hheaAscent);
	put16(b, hhea + 6, hheaDescent);
	put16(b, hhea + 8, lineGap);
	if (withOs2)
	{
		const std::size_t os2 = record(2, "OS/2", 78);
		put16(b, os2 + 74, winAscent);
		put16(b, os2 + 76, winDescent);
	}
	return b;
}
} // namespace

TEST_CASE("ui6 GDI text cell: tmAscent / tmDescent from the OS/2 win metrics, the external leading by the hhea line gap rule, the baseline under half of it")
{
	// Omnia LT Std's tables (the retail menu font): hhea 580 / -420 / gap 200, win 648 / 176, 1000 units per em
	GdiFaceMetrics face;
	std::string error;
	const std::vector<std::uint8_t> omnia = fakeFont(1000, 580, -420, 200, 648, 176);
	REQUIRE_MESSAGE(ReadGdiFaceMetrics(omnia.data(), omnia.size(), face, &error), error);
	CHECK(face.unitsPerEm == 1000);
	CHECK(face.winAscent == 648);
	CHECK(face.winDescent == 176);
	CHECK(face.hheaAscent == 580);
	CHECK(face.hheaDescent == -420);
	CHECK(face.hheaLineGap == 200);
	// 20 pixels, supersampled 4 times (RW 0x6186E0): the GDI font of 80 pixels has tmAscent round(51.84) = 52, tmDescent round(14.08) = 14, the external
	// leading 200 - (824 - 1000) = 376 units -> round(30.08) = 30; reduced: the char height (4 + 30 + 66 - 1) / 4 = 24, + 0x30 = 30 / 4 = 7, the baseline
	// 7 / 2 + 52 / 4 = 16 below the line's top
	const GdiTextCell c20 = GdiTextCellAt(face, 20);
	CHECK(c20.ascent == 52);
	CHECK(c20.descent == 14);
	CHECK(c20.externalLeading == 30);
	CHECK(c20.height == 24);
	CHECK(c20.leading == 7);
	CHECK(c20.baseline == 16.0f);
	// so a string centred in a box has its baseline 4 px below the box's middle (16 - 24 / 2), where Godot's hhea metrics put it 1.5 px (12 - 21 / 2)
	CHECK(c20.baseline - c20.height * 0.5f == 4.0f);
	// 12 pixels (48 supersampled): 31.104 -> 31, 8.448 -> 8, 18.048 -> 18; (4 + 18 + 39 - 1) / 4 = 15, 18 / 4 = 4, 4 / 2 + 31 / 4 = 9.75
	const GdiTextCell c12 = GdiTextCellAt(face, 12);
	CHECK(c12.ascent == 31);
	CHECK(c12.descent == 8);
	CHECK(c12.externalLeading == 18);
	CHECK(c12.height == 15);
	CHECK(c12.baseline == 9.75f);
	// without supersampling (a size record's style 1): GetTextMetrics at 20 pixels as it is
	const GdiTextCell one = GdiTextCellAt(face, 20, 1);
	CHECK(one.ascent == 13);
	CHECK(one.descent == 4);
	CHECK(one.externalLeading == 8);
	CHECK(one.height == 25);
	CHECK(one.baseline == 17.0f);
	// Albertus MT's tables: the win metrics take the whole line gap, no external leading; 75.68 -> 76, 20, (4 + 96 - 1) / 4 = 24, 76 / 4 = 19
	const std::vector<std::uint8_t> albertus = fakeFont(1000, 750, -250, 196, 946, 250);
	REQUIRE(ReadGdiFaceMetrics(albertus.data(), albertus.size(), face, &error));
	const GdiTextCell a20 = GdiTextCellAt(face, 20);
	CHECK(a20.externalLeading == 0);
	CHECK(a20.ascent == 76);
	CHECK(a20.descent == 20);
	CHECK(a20.height == 24);
	CHECK(a20.baseline == 19.0f);
	CHECK(GdiTextCellAt(face, 0).height == 0);
	CHECK(GdiTextCellAt(face, 20, 0).height == 0);
}

TEST_CASE("ui6 GDI text cell: a font file without the OS/2 win metrics, or a broken table directory, is an error, never a guess")
{
	GdiFaceMetrics face;
	std::string error;
	const std::vector<std::uint8_t> noOs2 = fakeFont(1000, 580, -420, 200, 0, 0, false);
	CHECK_FALSE(ReadGdiFaceMetrics(noOs2.data(), noOs2.size(), face, &error));
	CHECK(error.find("OS/2") != std::string::npos);
	const std::vector<std::uint8_t> zero = fakeFont(1000, 580, -420, 200, 0, 0);
	CHECK_FALSE(ReadGdiFaceMetrics(zero.data(), zero.size(), face, &error));
	std::vector<std::uint8_t> cut = fakeFont(1000, 580, -420, 200, 648, 176);
	cut.resize(40);
	CHECK_FALSE(ReadGdiFaceMetrics(cut.data(), cut.size(), face, &error));
	CHECK_FALSE(ReadGdiFaceMetrics(nullptr, 0, face, &error));
}

TEST_CASE("ui6 retail: every font the archives carry has GDI metrics; Omnia LT Std's are the ones the menus are placed with")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("ui6 retail fonts");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	int fonts = 0;
	for (const char *pattern : { "*.ttf", "*.otf" })
	{
		FilenameList list;
		mount->fs->getFileListInDirectory(std::string(), std::string(), std::string(pattern), list, true);
		for (const std::string &file : list)
		{
			if (file.find('/') != std::string::npos || file.find('\\') != std::string::npos)
			{
				continue;
			}
			std::vector<std::uint8_t> bytes;
			std::string error;
			REQUIRE_MESSAGE(mount->fs->readFile(file, bytes, &error), error);
			GdiFaceMetrics face;
			CHECK_MESSAGE(ReadGdiFaceMetrics(bytes.data(), bytes.size(), face, &error), (file + ": " + error));
			++fonts;
			std::string lower = file;
			for (char &ch : lower)
			{
				ch = (char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
			}
			if (lower == "omnialtstd.ttf")
			{
				CHECK(face.winAscent == 648);
				CHECK(face.winDescent == 176);
				CHECK(face.hheaLineGap == 200);
				CHECK(GdiTextCellAt(face, 20).baseline == 16.0f);
			}
		}
	}
	CHECK(fonts == 3); // AlbertusMT.otf, OmniaLTStd.ttf, SachWT__.ttf
}

// ---- the kept skirmish game (SkirmishGameRecord.h) ------------------------------------------------------------------------------------------------

namespace
{
SkirmishGameInfo sampleGame()
{
	SkirmishGameInfo g;
	g.mapName = "maps/map mp evendim/map mp evendim.map";
	g.mapCRC = 0x1A2B3C4Du;
	g.mapSize = 123456;
	g.mapMask = 0x1F;
	g.seed = 4242;
	g.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	g.rules[RULE_INITIAL_RESOURCES] = 2500;
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].name = u"Gimli";
	g.slots[0].accepted = true;
	g.slots[0].playerTemplate = 3;
	g.slots[0].color = 2;
	g.slots[0].teamNumber = 0;
	g.slots[1].state = SLOT_BRUTAL_AI;
	g.slots[1].accepted = true;
	g.slots[1].startPos = 1;
	g.slots[2].state = SLOT_HARD_AI;
	g.slots[2].teamNumber = 1;
	g.slots[3].state = SLOT_EASY_AI;
	g.slots[4].state = SLOT_MED_AI;
	g.slots[5].state = SLOT_OPEN;
	return g;
}
} // namespace

TEST_CASE("ui6 kept game: GameInfoToAsciiString's text (RW 0x8023C1): header, ten rules, the slot records with RotWK's computer letters")
{
	const SkirmishGameInfo g = sampleGame();
	const std::string text = SkirmishGameInfoToString(g, 0);
	std::string rules;
	for (int i = 0; i < NUM_GAME_RULES; ++i)
	{
		rules += std::to_string(g.rules[(std::size_t)i]) + " ";
	}
	const std::string expected = "M=01fmaps\\map mp evendim\\map mp evendim.map;MC=1A2B3C4D;MS=123456;SD=4242;GSID=0;GT=0;SI=-1;GR=" + rules + ";S="
		"HGimli,0,0,TT,2,3,-1,0,0,0,0:CB,-1,-1,1,-1,0,0:CH,-1,-1,-1,1,0,0:CE,-1,-1,-1,-1,0,0:CM,-1,-1,-1,-1,0,0:O:X:X:;";
	CHECK(text == expected);
	CHECK(SkirmishGameInfoToString(g, 1).find(";GT=1;") != std::string::npos);
	CHECK(SkirmishHeroIndexesString() == ":-1:-1:-1:-1:-1:-1:-1:-1:");
}

TEST_CASE("ui6 kept game: ParseAsciiStringToGameInfo (RW 0x802DBA) gives the written game back and refuses what retail refuses")
{
	const SkirmishGameInfo g = sampleGame();
	SkirmishGameRecordLimits limits;
	limits.numColors = 10;
	limits.numTemplates = 12;
	SkirmishGameInfo back;
	int mode = -5;
	std::string error;
	REQUIRE_MESSAGE(ParseSkirmishGameInfo(SkirmishGameInfoToString(g, 1), limits, back, &mode, &error), error);
	CHECK(mode == 1);
	CHECK(back.mapName == g.mapName);
	CHECK(back.mapCRC == g.mapCRC);
	CHECK(back.mapSize == g.mapSize);
	CHECK(back.mapMask == g.mapMask);
	CHECK(back.seed == g.seed);
	CHECK(back.rules == g.rules);
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		INFO("slot " << i);
		CHECK(back.slots[i].state == g.slots[i].state);
		CHECK(back.slots[i].color == g.slots[i].color);
		CHECK(back.slots[i].playerTemplate == g.slots[i].playerTemplate);
		CHECK(back.slots[i].startPos == g.slots[i].startPos);
		CHECK(back.slots[i].teamNumber == g.slots[i].teamNumber);
	}
	CHECK(back.slots[0].name == u"Gimli");
	CHECK(back.slots[0].accepted);
	CHECK(back.slots[1].name.empty()); // a computer: setState with the empty name
	// GR as RW 0x9609D9 reads it: ten atoi()s from just past each space; past the end, 0
	const std::string text = SkirmishGameInfoToString(g, 0);
	const std::size_t gr = text.find("GR="), end = text.find(';', gr);
	REQUIRE_MESSAGE(ParseSkirmishGameInfo(text.substr(0, gr) + "GR=1 2 3" + text.substr(end), limits, back, &mode, &error), error);
	CHECK(back.rules[0] == 1);
	CHECK(back.rules[2] == 3);
	CHECK(back.rules[3] == 0);
	CHECK(back.rules[9] == 0);
	// refusals (the game stays as it was)
	const SkirmishGameInfo kept = back;
	auto refused = [&](const std::string &bad) {
		SkirmishGameInfo out = kept;
		std::string why;
		const bool ok = ParseSkirmishGameInfo(bad, limits, out, nullptr, &why);
		CHECK_MESSAGE(!ok, bad);
		CHECK(out == kept);
		return why;
	};
	CHECK(refused("").find("missing") != std::string::npos);
	CHECK(refused(text + "ZZ=1;").find("unknown key") != std::string::npos);
	std::string noSeed = text;
	noSeed.erase(noSeed.find("SD=4242;"), 8);
	CHECK(refused(noSeed).find("missing") != std::string::npos);
	std::string badColour = text;
	badColour.replace(badColour.find("TT,2,"), 5, "TT,10,");
	refused(badColour);
	std::string badTeam = text;
	badTeam.replace(badTeam.find("CH,-1,-1,-1,1,"), 14, "CH,-1,-1,-1,4,");
	refused(badTeam);
	std::string badLetter = text;
	badLetter.replace(badLetter.find(":CE,"), 4, ":CZ,");
	refused(badLetter);
	std::string badKind = text;
	badKind.replace(badKind.find(":O:"), 3, ":Q:");
	refused(badKind);
	std::string shortList = text;
	shortList.replace(shortList.find("O:X:X:"), 6, "O:");
	refused(shortList);
	std::string emptyValue = text;
	emptyValue.replace(emptyValue.find("MS=123456"), 9, "MS=");
	refused(emptyValue);
}

TEST_CASE("ui6 kept game: Skirmish.ini keeps a profile's entries under \":<mode>:<quoted name>:<name>\" (RW 0x821AB4) and drops them with the profile")
{
	const std::filesystem::path p = std::filesystem::temp_directory_path() / "openbfme-ui6-skirmish.ini";
	std::filesystem::remove(p);
	{
		SkirmishPreferences prefs(p.string());
		CHECK(prefs.profileValue(0, "Map").empty()); // no profile
		REQUIRE(prefs.add(u"Gimli"));
		REQUIRE(prefs.setCurrent(u"Gimli"));
		REQUIRE(prefs.add(u"Legolas"));
		prefs.setProfileValue(0, "Map", "maps\\map mp evendim\\map mp evendim.map");
		prefs.setProfileValue(1, "GameInfo", "M=000x;");
		prefs.flush();
		CHECK(prefs.errors().empty());
		CHECK(SkirmishPreferences::profileKey(0, "G_00i_00m_00l_00i_00", "Map") == ":0:G_00i_00m_00l_00i_00:Map");
	}
	{
		SkirmishPreferences prefs(p.string());
		CHECK(prefs.current() == u"Gimli");
		CHECK(prefs.profileValue(0, "Map") == "maps\\map mp evendim\\map mp evendim.map");
		CHECK(prefs.profileValue(1, "GameInfo") == "M=000x;");
		CHECK(prefs.profileValue(1, "Map").empty());
		REQUIRE(prefs.setCurrent(u"Legolas"));
		CHECK(prefs.profileValue(0, "Map").empty()); // the entries are the profile's own
		REQUIRE(prefs.remove(u"Gimli"));
	}
	{
		SkirmishPreferences prefs(p.string());
		REQUIRE(prefs.add(u"Gimli")); // a new Gimli starts without the old one's entries
		REQUIRE(prefs.setCurrent(u"Gimli"));
		CHECK(prefs.profileValue(0, "Map").empty());
		CHECK(prefs.profileValue(1, "GameInfo").empty());
	}
	std::filesystem::remove(p);
	// the in-memory store behaves the same
	MemorySkirmishProfiles mem;
	mem.setProfileValue(0, "Map", "x");
	CHECK(mem.profileValue(0, "Map").empty());
	REQUIRE(mem.add(u"Gimli"));
	REQUIRE(mem.setCurrent(u"Gimli"));
	mem.setProfileValue(0, "Map", "x");
	CHECK(mem.profileValue(0, "Map") == "x");
	CHECK(mem.profileValue(1, "Map").empty());
	REQUIRE(mem.remove(u"Gimli"));
	REQUIRE(mem.add(u"Gimli"));
	REQUIRE(mem.setCurrent(u"Gimli"));
	CHECK(mem.profileValue(0, "Map").empty());
}

// ---- the widescreen view (FB-0021) ----------------------------------------------------------------------------------------------------------------

TEST_CASE("ui6 widescreen: Original is RotWK's Vert- view (RW 0x4874A7 / 0x533630); Wider keeps 4:3's vertical view and widens the horizontal one")
{
	CameraSettings settings;
	TacticalCamera cam(settings);
	const double deg = 180.0 / 3.14159265358979323846;
	const double h = 50.0 / deg;
	auto vertical = [&](double aspect) { return 2.0 * std::atan(std::tan(h / 2) / aspect) * deg; };
	CHECK(cam.horizontalFov() == doctest::Approx(h));
	CHECK_FALSE(cam.widescreenView()); // Original by default
	struct Screen
	{
		int w, h;
	};
	for (const Screen s : { Screen{ 1024, 768 }, Screen{ 1920, 1080 }, Screen{ 2560, 1080 }, Screen{ 3440, 1440 }, Screen{ 5120, 1440 }, Screen{ 1280, 1024 } })
	{
		INFO(s.w << " x " << s.h);
		const double aspect = (double)s.w / s.h;
		cam.setViewport(s.w, s.h);
		cam.setWidescreenView(false);
		// Original: 50 degrees across at every aspect, the vertical angle from the aspect (Vert-)
		CHECK(cam.verticalFov() * deg == doctest::Approx(vertical(aspect)).epsilon(1e-5));
		CHECK(cam.viewHorizontalFov() * deg == doctest::Approx(50.0).epsilon(1e-5));
		cam.setWidescreenView(true);
		if (aspect > 4.0 / 3.0)
		{
			// Wider: 4:3's 38.58 degrees vertically, more than 50 across
			CHECK(cam.verticalFov() * deg == doctest::Approx(vertical(4.0 / 3.0)).epsilon(1e-5));
			CHECK(cam.viewHorizontalFov() * deg > 50.0);
		}
		else
		{
			// 4:3 and narrower screens are RotWK's in both
			CHECK(cam.verticalFov() * deg == doctest::Approx(vertical(aspect)).epsilon(1e-5));
			CHECK(cam.viewHorizontalFov() * deg == doctest::Approx(50.0).epsilon(1e-5));
		}
	}
	// Original's vertical angles: 38.55 at 4:3, 29.39 at 16:9, 22.60 at 21:9, 14.94 at 32:9
	CHECK(vertical(4.0 / 3.0) == doctest::Approx(38.55).epsilon(1e-3));
	CHECK(vertical(16.0 / 9.0) == doctest::Approx(29.39).epsilon(1e-3));
	CHECK(vertical(21.0 / 9.0) == doctest::Approx(22.60).epsilon(1e-3));
	CHECK(vertical(32.0 / 9.0) == doctest::Approx(14.94).epsilon(1e-3));
	// Wider at 16:9: 2 atan(tan(19.28 degrees) * 16 / 9) = 63.74 degrees across
	cam.setViewport(1920, 1080);
	cam.setWidescreenView(true);
	CHECK(cam.viewHorizontalFov() * deg == doctest::Approx(63.74).epsilon(1e-3));
}
