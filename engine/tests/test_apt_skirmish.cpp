// OpenBFME tests: the Skirmish lobby (spec menus-apt.md build step A6): the slot logic of ZH SkirmishGameOptionsMenu / GUIUtil, the interim
// setup source, the profile commands, the MpGameSetup / AptMapPreview gadget handlers and the click-through of Skirmish.apt down to the
// new-game message.  Synthetic tests use data written here; the retail tests mount the shipped archives and SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/Skirmish/RealTimeStats.h"
#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GUI/MenuNavigator.h"
#include "GameClient/GameRecords.h"

#include "Libraries/Source/Apt/AptActionDecoder.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <functional>
#include <map>
#include <set>

using namespace apttest;

namespace
{

// ---- test doubles -----------------------------------------------------------------------------------------------------------------------

class MapGameText : public GameTextSource
{
public:
	std::map<std::string, std::u16string> labels;
	std::map<std::string, std::u16string> mapLabels; // "<map str file>|<label>"
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = labels.find(label);
		if (it == labels.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
	bool fetchMapLabel(const std::string &file, const std::string &label, std::u16string &out) const override
	{
		auto it = mapLabels.find(file + "|" + label);
		if (it == mapLabels.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
	void set(const std::string &label, const std::string &text) { labels[label] = asciiToU16(text); }
};

// The labels the lobby reads; the texts are test data (the real strings live in the language archive).
void lobbyLabels(MapGameText &t)
{
	t.set("GUI:Open", "Open");
	t.set("GUI:Closed", "Closed");
	t.set("GUI:EasyAI", "Easy AI");
	t.set("GUI:MediumAI", "Medium AI");
	t.set("GUI:HardAI", "Hard AI");
	t.set("GUI:BrutalAI", "Brutal AI");
	t.set("GUI:Random", "Random");
	t.set("GUI:???", "???");
	t.set("GUI:Player", "Player");
	t.set("GUI:StartingMoneyFormat", "$%d");
	t.set("GUI:ErrorStartingGame", "Error starting game");
	t.set("GUI:CantFindMap", "Cannot find the map");
	t.set("LAN:TooManyPlayers", "Too many players: the map has %d"); // lane GAMEMODES-1: RotWK's start check (RW 0x843564)
	for (int i = 0; i <= 4; ++i)
	{
		t.set("Team:" + std::to_string(i), "Team " + std::to_string(i));
	}
	for (const char *c : { "Blue", "Red", "Gold", "Green", "Orange", "Purple", "SkyBlue", "Pink", "Gray", "White" })
	{
		t.set(std::string("Color:") + c, c);
	}
	// the DisplayName labels of the factions (INI:Faction<Name>); retail's texts are in the language archive
	for (const char *s : { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor", "Tutorial" })
	{
		t.set(std::string("INI:Faction") + s, s);
	}
}

class FakeSetup : public SkirmishSetupSource
{
public:
	std::vector<SkirmishFaction> fac;
	std::vector<SkirmishColor> col;
	std::vector<MapCacheEntry> map;
	std::vector<int> cash{ 500, 1000, 1500 };
	const std::vector<SkirmishFaction> &factions() const override { return fac; }
	const std::vector<SkirmishColor> &colors() const override { return col; }
	const std::vector<MapCacheEntry> &maps() const override { return map; }
	int defaultStartingCash() const override { return 1500; }
	const std::vector<int> &startingCashChoices() const override { return cash; }
	int mapContentsMask(const MapCacheEntry &m) const override { return 1 | (m.numPlayers == 2 ? 2 : 0); }
	std::set<std::string> files; // lane UI-2: the files fileExists answers true for
	bool fileExists(const std::string &path) const override { return files.count(path) != 0; }
	bool hasFileSystem() const override { return true; }

	static SkirmishFaction faction(const char *name, const char *side, bool playable, const char *building)
	{
		SkirmishFaction f;
		f.templateName = name;
		f.side = side;
		f.displayLabel = std::string("INI:") + name;
		f.playableSide = playable;
		f.startingBuilding = building;
		return f;
	}
	static MapCacheEntry mapEntry(const char *name, const char *label, int players, bool mp, std::uint32_t crc)
	{
		MapCacheEntry m;
		m.name = name;
		m.rawName = name;
		m.numPlayers = players;
		m.isMultiplayer = mp;
		m.fileCRC = crc;
		m.fileSize = crc + 1;
		m.displayName = asciiToU16(label);
		m.description = asciiToU16(std::string("Map:Desc") + name);
		return m;
	}
	FakeSetup()
	{
		fac.push_back(faction("FactionCivilian", "Civilian", false, ""));
		fac.push_back(faction("FactionMen", "Men", true, "MenFortress"));
		fac.push_back(faction("FactionTutorial", "Men", false, "MenFortress"));
		fac.push_back(faction("FactionElves", "Elves", true, "ElvenFortress"));
		fac.push_back(faction("FactionArnor", "Arnor", false, "ArnorFortress"));
		for (const char *n : { "Blue", "Red", "Gold" })
		{
			SkirmishColor c;
			c.name = std::string("Color") + n;
			c.tooltipName = std::string("Color:") + n;
			c.rgb = n[0] == 'B' ? 0x465B9C : (n[0] == 'R' ? 0x9E382A : 0xAFBD4C);
			col.push_back(c);
		}
		SkirmishColor hidden;
		hidden.name = "ColorGray";
		hidden.tooltipName = "Color:Gray";
		hidden.availableInWotR = false;
		col.push_back(hidden);
		// the cache order is not the list order: keys sort as maps/b.. < maps/c.. < maps/d..
		map.push_back(mapEntry("maps/dwarf/dwarf.map", "$Map:Dwarf", 4, true, 0xD0));
		map.push_back(mapEntry("maps/alpha/alpha.map", "$Map:Alpha", 2, true, 0xA0));
		map.push_back(mapEntry("maps/solo/solo.map", "$Map:Solo", 1, false, 0x50));
		map.push_back(mapEntry("maps/beta/beta.map", "Plain Beta", 2, true, 0xB0));
		map.push_back(mapEntry("maps/nameless/nameless.map", "", 2, true, 0x99));
		map.push_back(mapEntry("maps/gamma/gamma.map", "$Map:Gamma", 2, true, 0xC0));
	}
};

SkirmishGameSetup makeSetup(FakeSetup &source, MapGameText &text)
{
	lobbyLabels(text);
	return SkirmishGameSetup(source, &text);
}

TEST_CASE("localized text: lobby starting cash uses a typed amount")
{
	FakeSetup source;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(source, text);
	struct Case { const char16_t *pattern; const char16_t *expected; };
	for (const Case &c : { Case{ u"$%d", u"$1000" }, Case{ u"%s", u"1000" }, Case{ u"%n", u"%n" }, Case{ u"%x", u"%x" },
		Case{ u"%p", u"%p" }, Case{ u"%999999d", u"%999999d" }, Case{ u"%", u"%" }, Case{ u"%ls/%S/%d", u"1000//" }, Case{ u"plain", u"plain" } })
	{
		text.labels["GUI:StartingMoneyFormat"] = c.pattern;
		const auto entries = setup.startingCashEntries();
		REQUIRE(entries.size() == 3);
		CHECK(entries[1].text == c.expected);
	}
}

} // namespace

// ============================================================================================================================
// The slot logic (ZH SkirmishGameOptionsMenu.cpp, GameInfo.cpp, GUIUtil.cpp)
// ============================================================================================================================

TEST_CASE("skirmish setup: init follows SkirmishGameOptionsMenuInit (player in slot 0, an easy AI in slot 1, the rest reset, the default map, the injected seed)")
{
	FakeSetup src;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(src, text);
	std::string error;
	REQUIRE_MESSAGE(setup.init(u"Gimli", 77, std::string(), &error), error);
	const SkirmishGameInfo &g = setup.info();
	CHECK(g.slots[0].state == SLOT_PLAYER);
	CHECK(g.slots[0].name == u"Gimli");
	CHECK(g.slots[0].accepted);
	CHECK(g.slots[1].state == SLOT_EASY_AI);
	CHECK(g.slots[1].name == u"Easy AI");
	CHECK(g.slots[1].accepted);
	for (int i = 2; i < MAX_SLOTS; ++i)
	{
		CHECK(g.slots[i] == SkirmishGameSlot()); // GameSlot::reset(): closed, no name, not accepted
	}
	CHECK(g.seed == 77);
	// lane RULES-1: RotWK leaves the starting money unset (GameInfo + 0x54 = -1) and takes the mode's default rules (InitialResources 1000, RW 0x9288D0)
	CHECK(g.startingCash == -1);
	CHECK(g.rules == MpGameRules::defaults(RULES_MODE_SKIRMISH));
	CHECK(g.legendaryHeroes == LegendaryHeroes::kOff);
	// getDefaultMap(TRUE): the first multiplayer key of the cache's std::map (maps\alpha\alpha.map)
	CHECK(g.mapName == "maps/alpha/alpha.map");
	CHECK(g.mapCRC == 0xA0);
	CHECK(g.mapSize == 0xA1);
	CHECK(g.mapMask == 3);
	CHECK_FALSE(g.inProgress);
	CHECK(g.numPlayers() == 2);
	// an unknown map is an error that reaches the caller
	CHECK_FALSE(setup.init(u"Gimli", 1, "maps/nothing/nothing.map", &error));
	CHECK(error == "the map cache has no map 'maps/nothing/nothing.map'");
}

TEST_CASE("skirmish setup: the combo contents (player types with four AI levels, the factions by flags, colours no other slot holds, teams)")
{
	FakeSetup src;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(src, text);
	REQUIRE(setup.init(u"P", 1, std::string(), nullptr));
	// player type entries: the local slot one blank entry, the others Open / Closed / Easy / Medium / Hard / Brutal tagged with their SlotState
	// (RotWK 0x8419DB reads GUI:HardAI with item data 4, 0x841A1B GUI:BrutalAI with item data 5)
	auto p0 = setup.playerEntries(0);
	REQUIRE(p0.size() == 1);
	CHECK(p0[0].text.empty());
	auto p2 = setup.playerEntries(2);
	REQUIRE(p2.size() == 6);
	CHECK(p2[0].text == u"Open");
	CHECK(p2[0].itemData == SLOT_OPEN);
	CHECK(p2[1].itemData == SLOT_CLOSED);
	CHECK(p2[2].itemData == SLOT_EASY_AI);
	CHECK(p2[3].itemData == SLOT_MED_AI);
	CHECK(p2[4].text == u"Hard AI");
	CHECK(p2[4].itemData == SLOT_HARD_AI);
	CHECK(p2[5].text == u"Brutal AI");
	CHECK(p2[5].itemData == SLOT_BRUTAL_AI);
	CHECK(p2[4].itemData == 4);
	CHECK(p2[5].itemData == 5);
	CHECK(SLOT_PLAYER == 6);
	// templates: Random, then the store's playable non-observer templates with their DisplayName label: Men (index 1), Elves (index 3); Civilian,
	// Tutorial and Arnor are not PlayableSide.  A faction needs no StartingBuilding and no "SIDE:" label, and two templates of one side both show.
	auto tpl = setup.templateEntries();
	REQUIRE(tpl.size() == 3);
	CHECK(tpl[0].text == u"Random");
	CHECK(tpl[0].itemData == PLAYERTEMPLATE_RANDOM);
	CHECK(tpl[1].text == u"Men");
	CHECK(tpl[1].itemData == 1);
	CHECK(tpl[2].text == u"Elves");
	CHECK(tpl[2].itemData == 3);
	// teams: Team:0 is -1, then four teams 0..3
	auto teams = setup.teamEntries();
	REQUIRE(teams.size() == 5);
	CHECK(teams[0].itemData == -1);
	CHECK(teams[4].text == u"Team 4");
	CHECK(teams[4].itemData == 3);
	// colours (RotWK 0x8440C7): "???" (-1) and every configured colour, ColorGray (AvailableInWotR = No) included; a colour another slot holds is gone
	auto colors = setup.colorEntries(0);
	REQUIRE(colors.size() == 5);
	CHECK(colors[0].text == u"???");
	CHECK(colors[0].image == "AptRandomColor");
	CHECK(colors[1].itemData == 0);
	CHECK(colors[1].color == 0xFF465B9Cu);
	CHECK(colors[1].image == "AptWhiteBox");
	REQUIRE(setup.setSlotColor(1, 0));
	colors = setup.colorEntries(0);
	REQUIRE(colors.size() == 4);
	CHECK(colors[1].itemData == 1);
	CHECK(setup.colorEntries(1).size() == 5); // the slot that holds blue still offers it
	// only in strategic mode (the flag at MpGameSetup+0x7C == 1) are the AvailableInWotR = No colours dropped
	setup.setStrategicMode(true);
	CHECK(setup.colorEntries(0).size() == 3);
	setup.setStrategicMode(false);
	// starting cash: the format with the amount
	auto cash = setup.startingCashEntries();
	REQUIRE(cash.size() == 3);
	CHECK(cash[1].text == u"$1000");
	CHECK(cash[1].itemData == 1000);
	// a label the table lacks reads as MISSING (ZH GameText::fetch), never as a made-up text
	MapGameText empty;
	SkirmishGameSetup bare(src, &empty);
	CHECK(bare.playerEntries(2)[0].text == u"MISSING: 'GUI:Open'");
}

TEST_CASE("skirmish setup: the handlers (GameSlot::setState, colour and start position availability, map change)")
{
	FakeSetup src;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(src, text);
	REQUIRE(setup.init(u"P", 1, std::string(), nullptr));
	// slot 0 and out of range are ignored by handlePlayerSelection
	CHECK_FALSE(setup.setSlotState(0, SLOT_CLOSED, u"x"));
	CHECK_FALSE(setup.setSlotState(8, SLOT_CLOSED, u"x"));
	CHECK(setup.info().slots[0].state == SLOT_PLAYER);
	// an AI keeps its choices when it becomes another AI, loses them otherwise
	REQUIRE(setup.setSlotTemplate(1, 3));
	REQUIRE(setup.setSlotTeam(1, 2));
	REQUIRE(setup.setSlotColor(1, 1));
	REQUIRE(setup.setSlotStartPos(1, 0));
	REQUIRE(setup.setSlotState(1, SLOT_HARD_AI, u"ignored"));
	CHECK(setup.info().slots[1].name == u"Hard AI"); // GameSlot::setState: the name is the state's text, not the combo text
	CHECK(setup.info().slots[1].playerTemplate == 3);
	REQUIRE(setup.setSlotState(1, SLOT_BRUTAL_AI, u"ignored")); // Hard and Brutal are different states: an AI becoming another AI keeps its choices
	CHECK(setup.info().slots[1].state == SLOT_BRUTAL_AI);
	CHECK(setup.info().slots[1].name == u"Brutal AI");
	CHECK(setup.info().slots[1].isAI());
	CHECK_FALSE(setup.info().slots[1].isHuman());
	CHECK(setup.info().slots[1].playerTemplate == 3);
	CHECK(setup.info().slots[1].teamNumber == 2);
	CHECK(setup.info().slots[1].color == 1);
	REQUIRE(setup.setSlotState(1, SLOT_OPEN, u"ignored"));
	CHECK(setup.info().slots[1].name == u"Open");
	CHECK(setup.info().slots[1].playerTemplate == -1);
	CHECK(setup.info().slots[1].teamNumber == -1);
	CHECK(setup.info().slots[1].color == -1);
	CHECK(setup.info().slots[1].startPos == -1);
	// handleColorSelection: a taken colour is refused, -1 (random) is not a taken colour
	REQUIRE(setup.setSlotColor(0, 2));
	CHECK_FALSE(setup.setSlotColor(2, 2));
	CHECK(setup.info().slots[2].color == -1);
	REQUIRE(setup.setSlotColor(2, -1));
	REQUIRE(setup.setSlotColor(3, -1));
	// handleStartPositionSelection: a taken position is refused
	REQUIRE(setup.setSlotStartPos(0, 1));
	CHECK_FALSE(setup.setSlotStartPos(2, 1));
	REQUIRE(setup.setSlotStartPos(2, 0));
	// a new map resets every start position (SkirmishMapSelectMenu OK)
	REQUIRE(setup.setMap("maps/dwarf/dwarf.map"));
	CHECK(setup.info().mapName == "maps/dwarf/dwarf.map");
	CHECK(setup.info().mapCRC == 0xD0);
	CHECK(setup.info().mapMask == 1);
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		CHECK(setup.info().slots[i].startPos == -1);
	}
	CHECK_FALSE(setup.setMap("maps/none/none.map"));
	CHECK(setup.info().mapName == "maps/dwarf/dwarf.map");
	CHECK(setup.lastError() == "the map cache has no map 'maps/none/none.map'");
}

TEST_CASE("skirmish setup: startPressed refuses an unknown map and too many players, reallyDoStart closes the open slots and picks the game mode")
{
	FakeSetup src;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(src, text);
	REQUIRE(setup.init(u"P", 9, "maps/alpha/alpha.map", nullptr)); // two players
	std::u16string title, body;
	CHECK(setup.validateStart(title, body));
	setup.setSlotState(2, SLOT_MED_AI, u"");
	CHECK_FALSE(setup.validateStart(title, body));
	CHECK(title.empty()); // lane GAMEMODES-1: RW 0x928482 -> MessageBoxOk(L"", text)
	CHECK(body == u"Too many players: the map has 2");
	setup.setSlotState(2, SLOT_CLOSED, u"");
	REQUIRE(setup.validateStart(title, body));
	// an open slot becomes a fresh closed one when the game starts (GameInfo::closeOpenSlots)
	setup.setSlotState(3, SLOT_OPEN, u"");
	NewGameMessage m = setup.makeNewGameMessage();
	CHECK(m.mode == NewGameMode::Skirmish);
	CHECK(m.difficulty == 1);
	CHECK(m.rankPoints == 0);
	CHECK(m.game.inProgress);
	CHECK(m.game.slots[3].state == SLOT_CLOSED);
	CHECK(m.game.slots[3].name == u"Closed");
	CHECK(m.game.seed == 9);
	CHECK(m.game == setup.info());
	// nothing resolves the random choices of the message: the two occupied slots carry their start position, faction and colour as -1 (S-178)
	CHECK(m.game.unresolvedRandomChoices() == std::vector<std::string>{ "slot 0 start position", "slot 0 faction", "slot 0 colour", "slot 1 start position", "slot 1 faction", "slot 1 colour" });
	// a solo map chosen in the skirmish list starts a single-player game ("we can now select solo campaign maps in Skirmish")
	SkirmishGameSetup solo = makeSetup(src, text);
	REQUIRE(solo.init(u"P", 1, "maps/solo/solo.map", nullptr));
	CHECK(solo.makeNewGameMessage().mode == NewGameMode::SinglePlayer);
	// an unknown map in the info (a cache that changed under the lobby) is the first refusal; lane GAMEMODES-1: RotWK's check (RW 0x843263) refuses it without
	// a text (no map, no maximum player count)
	FakeSetup other;
	other.map.clear();
	other.map.push_back(FakeSetup::mapEntry("maps/other/other.map", "$Map:Other", 2, true, 1));
	SkirmishGameSetup lost(other, &text);
	REQUIRE(lost.init(u"P", 1, std::string(), nullptr));
	other.map.clear();
	CHECK_FALSE(lost.validateStart(title, body));
	CHECK(body.empty());
	CHECK(lost.checkStart().refusalLabel.empty());
}

TEST_CASE("skirmish setup: the map list is ordered by player count then display name; names resolve through the map string file with the retail fallbacks")
{
	FakeSetup src;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(src, text);
	// no map.str reader: `$` labels fall back to the file name without .map, other labels to the label itself
	CHECK(setup.mapBaseDisplayName(src.map[0]) == u"dwarf");
	CHECK(setup.mapBaseDisplayName(src.map[3]) == u"Plain Beta");
	CHECK(setup.mapDisplayName(src.map[0], true) == u"dwarf (4)");
	CHECK(setup.mapDisplayName(src.map[2], true) == u"solo");
	// the solo map, the nameless map and (not multiplayer) are not listed: alpha, beta (two players, by name), gamma, then dwarf (four)
	std::vector<std::string> keys;
	for (int i : setup.listedMaps())
	{
		keys.push_back(src.map[(std::size_t)i].name);
	}
	CHECK(keys == std::vector<std::string>{ "maps/alpha/alpha.map", "maps/gamma/gamma.map", "maps/beta/beta.map", "maps/dwarf/dwarf.map" });
	// the order follows the shown names: "Plain Beta (2)" sorts after "gamma (2)" and "alpha (2)"
	// a string-file entry wins over the fallback
	text.mapLabels["maps\\alpha\\map.str|Map:Alpha"] = u"Alpha Vale";
	CHECK(setup.mapBaseDisplayName(src.map[1]) == u"Alpha Vale");
	text.mapLabels["maps\\alpha\\map.str|Map:Descmaps/alpha/alpha.map"] = u"First line\nsecond line";
	CHECK(setup.mapDescription(src.map[1]) == u"First line\nsecond line");
	CHECK(setup.mapDescriptionFirstLine(src.map[1]) == u"First line");
	// two maps that show the same name collapse to the last (std::set + filenameMap of populateMapListboxNoReset)
	FakeSetup dup;
	dup.map.clear();
	dup.map.push_back(FakeSetup::mapEntry("maps/one/one.map", "Same", 2, true, 1));
	dup.map.push_back(FakeSetup::mapEntry("maps/two/two.map", "Same", 2, true, 2));
	SkirmishGameSetup dupSetup(dup, &text);
	REQUIRE(dupSetup.listedMaps().size() == 1);
	CHECK(dupSetup.listedMaps()[0] == 1);
}

TEST_CASE("skirmish profiles: the in-memory store compares names without case, the current profile moves on a delete")
{
	MemorySkirmishProfiles p;
	CHECK(p.current().empty());
	CHECK_FALSE(p.add(u""));
	CHECK(p.add(u"Frodo"));
	CHECK_FALSE(p.add(u"FRODO"));
	CHECK(p.add(u"Sam"));
	CHECK(p.setCurrent(u"sam"));
	CHECK(p.current() == u"Sam");
	CHECK_FALSE(p.setCurrent(u"Merry"));
	CHECK(p.remove(u"SAM"));
	CHECK(p.current() == u"Frodo");
	CHECK(p.remove(u"Frodo"));
	CHECK(p.current().empty());
	CHECK_FALSE(p.remove(u"Frodo"));
}

// ============================================================================================================================
// The interim setup source (stop S-178)
// ============================================================================================================================

TEST_CASE("setup source: the playertemplate / multiplayer / gamedata scans accept both comment styles, take the last assignment and reject malformed values")
{
	const std::string templates =
		"; header\n"
		"PlayerTemplate FactionMen\n"
		"  Side = Men ; trailing\n"
		"  PlayableSide = Yes\n"
		"  StartingBuilding = MenFortress\n"
		"  PreferredColor = R:43 G:150 B:179\n"
		"  DisplayName = INI:FactionMen\n"
		"  BuildableHeroesMP = CreateAHero RohanEowyn // Alatar\n"
		"End\n"
		"PlayerTemplate FactionNeutral\n"
		"  Side = Neutral\n"
		"  PlayableSide = No\n"
		"  IsObserver = Yes\n"
		"  StartingBuilding = None\n"
		"End\n"
		"SomethingElse Block\n  Key = Value\nEnd\n"
		"PlayerTemplate FactionMen\n"
		"  Side = Men\n  PlayableSide = No\n"
		"End\n";
	std::vector<SkirmishFaction> f;
	std::string error;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::scanPlayerTemplates(templates, f, &error), error);
	REQUIRE(f.size() == 2);
	CHECK(f[0].templateName == "FactionMen"); // the second block replaced the first in place
	CHECK_FALSE(f[0].playableSide);
	CHECK(f[0].startingBuilding.empty());
	CHECK(f[1].isObserver);
	CHECK(f[1].startingBuilding.empty()); // None
	REQUIRE(IniSkirmishSetupSource::scanPlayerTemplates(templates.substr(0, templates.find("SomethingElse")), f, &error));
	CHECK(f[0].preferredColor == 0x2B96B3);
	CHECK(f[0].buildableHeroesMP == std::vector<std::string>{ "CreateAHero", "RohanEowyn" });
	CHECK(f[0].displayLabel == "INI:FactionMen");
	CHECK_FALSE(IniSkirmishSetupSource::scanPlayerTemplates("PlayerTemplate X\n  PreferredColor = R:300 G:0 B:0\nEnd\n", f, &error));
	CHECK(error == "playertemplate.ini line 2: PreferredColor 'R:300 G:0 B:0' is not R:n G:n B:n");
	CHECK_FALSE(IniSkirmishSetupSource::scanPlayerTemplates("PlayerTemplate X\n  Side = A\n", f, &error));
	CHECK(error == "playertemplate.ini: the block 'X' has no End");

	const std::string multi =
		"OnlineChatColors\n  Default = R:1 G:2 B:3\nEnd\n"
		"MultiplayerSettings\n  InitialCreditsHigh = 2000\n  InitialCreditsLow = 1000\n  StartCountdownTimer = 5\nEnd\n"
		"// Slot 0\nMultiplayerColor ColorBlue\n  RGBColor = R:70 G:91 B:156\n  TooltipName = Color:Blue\n  AvailableInWotR = Yes\nEnd\n"
		"MultiplayerColor ColorGray\n  RGBColor = R:100 G:100 B:100\n  TooltipName = Color:Gray\n  AvailableInWotR = No\nEnd\n";
	std::vector<SkirmishColor> colors;
	std::vector<int> cash;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::scanMultiplayer(multi, colors, cash, &error), error);
	REQUIRE(colors.size() == 2);
	CHECK(colors[0].name == "ColorBlue");
	CHECK(colors[0].rgb == 0x465B9C);
	CHECK(colors[0].tooltipName == "Color:Blue");
	CHECK(colors[0].availableInWotR);
	CHECK_FALSE(colors[1].availableInWotR);
	CHECK(cash == std::vector<int>{ 1000, 2000 });
	CHECK_FALSE(IniSkirmishSetupSource::scanMultiplayer("MultiplayerColor C\n  RGBColor = R:1 G:2\nEnd\n", colors, cash, &error));
	int amount = 0;
	REQUIRE(IniSkirmishSetupSource::scanDefaultStartingCash("GameData\n  DefaultStartingCash = 100\n  DefaultStartingCash = 1500 ;x\nEnd\n", amount, &error));
	CHECK(amount == 1500);
	CHECK_FALSE(IniSkirmishSetupSource::scanDefaultStartingCash("GameData\nEnd\n", amount, &error));
	CHECK(error == "gamedata.ini: no DefaultStartingCash");
}

TEST_CASE("retail setup source: 122 cached maps, the factions in store order, the colours, the starting cash, the map contents mask")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	IniSkirmishSetupSource src;
	std::string error;
	REQUIRE_MESSAGE(src.load(mount.fs, &error), error);
	CHECK(src.maps().size() == 122);
	CHECK(src.defaultStartingCash() == 1500);
	CHECK(src.startingCashChoices() == std::vector<int>{ 500, 1000, 1500, 2000, 2500 });
	REQUIRE(src.colors().size() == 10);
	CHECK(src.colors()[0].name == "ColorBlue");
	CHECK(src.colors()[0].rgb == 0x465B9C);
	CHECK(src.colors()[0].tooltipName == "Color:Blue");
	int available = 0;
	for (const SkirmishColor &c : src.colors())
	{
		available += c.availableInWotR ? 1 : 0;
	}
	CHECK(available == 6);
	// the playable factions: a StartingBuilding, PlayableSide, one per side
	std::vector<std::string> sides;
	for (const SkirmishFaction &f : src.factions())
	{
		if (!f.startingBuilding.empty() && f.playableSide && !f.isObserver)
		{
			sides.push_back(f.side);
		}
	}
	CHECK(sides == std::vector<std::string>{ "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar" });
	// a multiplayer map has the cache bit (1); its files are looked up next to it
	const MapCacheEntry *mp = nullptr;
	for (const MapCacheEntry &m : src.maps())
	{
		if (m.isMultiplayer)
		{
			mp = &m;
			break;
		}
	}
	REQUIRE(mp);
	CHECK((src.mapContentsMask(*mp) & 1) == 1);
}

// ============================================================================================================================
// Retail: the Skirmish lobby, click-through
// ============================================================================================================================

namespace
{
struct LobbyFx
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	MapGameText text;
	IniSkirmishSetupSource setup;
	RecordingNewGameSink sink;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	AptSkirmish *screen = nullptr;

	explicit LobbyFx(AptRetail &mount, bool pushNow = true) : source(mount.fs)
	{
		std::string error;
		REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
		REQUIRE_MESSAGE(setup.load(mount.fs, &error), error);
		lobbyLabels(text);
		sink.seed = 4242;
		environment.gameText = &text;
		environment.skirmish = &setup;
		environment.newGame = &sink;
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->registerComponents();
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->init();
		if (pushNow)
		{
			shell->push("Skirmish.apt");
			screen = dynamic_cast<AptSkirmish *>(shell->top());
			REQUIRE(screen);
		}
	}
	~LobbyFx()
	{
		shell.reset();
		layer.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
	void dumpNotes(const char *what)
	{
		for (const WindowManagerNote &n : wm->notes())
		{
			printf("NOTE[%s] %s | %s\n", what, n.kind.c_str(), n.detail.c_str());
		}
		for (const std::string &e : wm->errors())
		{
			printf("ERROR[%s] %s\n", what, e.c_str());
		}
		for (const std::string &e : layer->errors())
		{
			printf("LAYERERR[%s] %s\n", what, e.c_str());
		}
	}
};
} // namespace

// ---- helpers of the click-through ---------------------------------------------------------------------------------------------------------

namespace
{
void clickAt(LobbyFx &fx, float x, float y)
{
	fx.wm->postMouseMove(x, y);
	fx.tick(1);
	fx.wm->postMouseButton(true);
	fx.tick(1);
	fx.wm->postMouseButton(false);
	fx.tick(1);
}

// Clicks the centre of the first row of a list box that GadgetListBoxGetEntryBasedOnXY maps to `row`.
void clickListRow(LobbyFx &fx, GameWindow *list, int row)
{
	int sx = 0, sy = 0, w = 0, h = 0;
	list->winGetScreenPosition(&sx, &sy);
	list->winGetSize(&w, &h);
	const int x = sx + w / 3;
	// the list opens scrolled to the selected map; the user scrolls (the wheel / slider) until the row is in view
	GadgetListBoxSetTopVisibleEntry(list, row);
	for (int y = sy; y < sy + h; ++y)
	{
		int r = -1, c = -1;
		GadgetListBoxGetEntryBasedOnXY(list, x, y, r, c);
		if (r == row)
		{
			clickAt(fx, (float)x, (float)y + 1.0f);
			return;
		}
	}
	FAIL("row " << row << " is not inside the list box " << sx << "," << sy << " " << w << "x" << h << " entries=" << GadgetListBoxGetNumEntries(list) << " hidden=" << list->winIsHidden());
}

AptButtonInst *firstButtonIn(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptButtonInst *b = firstButtonIn(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

void clickButton(LobbyFx &fx, AptButtonInst &b)
{
	float x0, y0, x1, y1;
	REQUIRE(b.contentBounds(x0, y0, x1, y1));
	float x, y;
	b.globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	// the button the pointer really hits at the centre: the target itself or a button of the same movie clip (a button clip may stack its own hit areas)
	AptButtonInst *hit = fx.wm->apt().input().hitTestButtons(x, y);
	const std::string target = b.targetPath();
	const std::string clip = target.substr(0, target.rfind('.'));
	REQUIRE_MESSAGE((hit && (hit == &b || hit->targetPath().compare(0, clip.size(), clip) == 0)), "the centre of " << target << " is covered by " << (hit ? hit->targetPath() : std::string("nothing")));
	clickAt(fx, x, y);
}

// The profile popup's pages are frames of ProfilePopup.Main (labels _delete, _change, _new): the list box of the change / delete pages exists only
// while that frame is shown.
void showProfilePage(LobbyFx &fx, const char *label)
{
	AptCharacterInst *main = fx.wm->apt().resolvePath(fx.wm->apt().level(1), "ProfilePopup.Main");
	REQUIRE(main);
	AptSpriteInst *sp = main->asSprite();
	REQUIRE(sp);
	const int frame = sp->labelFrame(label);
	REQUIRE_MESSAGE(frame >= 0, label);
	sp->gotoFrame(frame);
	fx.tick(5);
}

int itemValueOf(void *data)
{
	return static_cast<int>(reinterpret_cast<std::intptr_t>(data));
}

int selectedPos(GameWindow *combo)
{
	int s = -2;
	GadgetComboBoxGetSelectedPos(combo, &s);
	return s;
}

int factionIndex(const IniSkirmishSetupSource &src, const std::string &templateName)
{
	for (int i = 0; i < (int)src.factions().size(); ++i)
	{
		if (src.factions()[(std::size_t)i].templateName == templateName)
		{
			return i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("retail lobby: the gadgets are populated from the INI factions, colours and the map cache")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	CHECK(fx.screen->lastError().empty());
	CHECK(fx.layer->errors().empty());
	CHECK(fx.screen->initializedCount() == 1);
	const SkirmishGameSetup &setup = *fx.screen->setup();
	for (int slot = 0; slot < LEGACY_MAX_SLOTS; ++slot)
	{
		GameWindow *player = fx.screen->slotGadget(slot, "Player");
		REQUIRE(player);
		CHECK(GadgetComboBoxGetLength(player) == (slot == 0 ? 1 : 6));
		CHECK(GadgetComboBoxGetLength(fx.screen->slotGadget(slot, "PlayerTemplate")) == 8); // Random + 7 sides
		CHECK(GadgetComboBoxGetLength(fx.screen->slotGadget(slot, "Team")) == 5);
		CHECK(GadgetImageComboBoxGetLength(fx.screen->slotGadget(slot, "Color")) == 11); // ??? + the ten configured colours (strategic mode would drop four)
		REQUIRE(fx.screen->slotGadget(slot, "Handicap"));
		REQUIRE(fx.screen->slotGadget(slot, "Hero"));
	}
	// slot 1 is the easy AI: the entry whose item data is SLOT_EASY_AI is selected; slot 2 is closed
	GameWindow *p1 = fx.screen->slotGadget(1, "Player");
	CHECK(selectedPos(p1) == 2);
	CHECK(itemValueOf(GadgetComboBoxGetItemData(p1, 2)) == SLOT_EASY_AI);
	CHECK(selectedPos(fx.screen->slotGadget(2, "Player")) == 1);
	// the faction combo lists the sides in store order
	GameWindow *t0 = fx.screen->slotGadget(0, "PlayerTemplate");
	CHECK(GadgetComboBoxGetItemData(t0, 1) == reinterpret_cast<void *>(static_cast<std::intptr_t>(factionIndex(fx.setup, "FactionMen"))));
	CHECK(selectedPos(t0) == 0);
	// the map list (RW 0x8460B5): one row per listed map, the conquest image in column 0, the display name without the count in column 2 (70 %),
	// the player count in column 4; the default order is players, then name; the row keys are kept beside the list
	GameWindow *list = fx.screen->mapListWindow();
	REQUIRE(list);
	const std::vector<int> listed = setup.listedMaps();
	REQUIRE_FALSE(listed.empty());
	CHECK(GadgetListBoxGetNumColumns(list) == 5);
	CHECK(GadgetListBoxGetNumEntries(list) == (int)listed.size());
	CHECK(fx.screen->mapListKeys().size() == listed.size());
	const MapCacheEntry &first = fx.setup.maps()[(std::size_t)listed[0]];
	CHECK(fx.screen->mapListKeys()[0] == first.name);
	CHECK(GadgetListBoxGetText(list, 0, 2) == setup.mapDisplayName(first, false));
	CHECK(GadgetListBoxGetText(list, 0, 4) == std::u16string(1, (char16_t)(u'0' + first.numPlayers)));
	CHECK(GadgetListBoxGetText(list, 0, 1).empty());
	CHECK(GadgetListBoxGetText(list, 0, 3).empty());
	for (std::size_t i = 1; i < listed.size(); ++i)
	{
		const MapCacheEntry *a = nullptr, *b = nullptr;
		for (const MapCacheEntry &m : fx.setup.maps())
		{
			a = m.name == fx.screen->mapListKeys()[i - 1] ? &m : a;
			b = m.name == fx.screen->mapListKeys()[i] ? &m : b;
		}
		REQUIRE(a);
		REQUIRE(b);
		CHECK(a->numPlayers <= b->numPlayers);
	}
	// MpGameSetup::OnSortName (RW 0x840143 -> 0x84010E with 0): names ascending; again: descending
	fx.wm->invokeCallback("MpGameSetup::OnSortName", "");
	CHECK(GadgetListBoxGetNumEntries(list) == (int)listed.size());
	auto lowerName = [&](int row) {
		std::u16string t = GadgetListBoxGetText(list, row, 2);
		for (char16_t &c : t)
		{
			c = (c >= u'A' && c <= u'Z') ? (char16_t)(c - u'A' + u'a') : c;
		}
		return t;
	};
	CHECK(lowerName(0) <= lowerName(1));
	CHECK(lowerName((int)listed.size() - 2) <= lowerName((int)listed.size() - 1));
	fx.wm->invokeCallback("MpGameSetup::OnSortName", "");
	CHECK(lowerName(0) >= lowerName(1));
	// OnSortPlayers (2): players ascending, the name descending (the old primary 1 became the secondary)
	fx.wm->invokeCallback("MpGameSetup::OnSortPlayers", "");
	CHECK(GadgetListBoxGetText(list, 0, 4) == std::u16string(1, (char16_t)(u'0' + first.numPlayers)));
	if (GadgetListBoxGetText(list, 1, 4) == GadgetListBoxGetText(list, 0, 4))
	{
		CHECK(lowerName(0) >= lowerName(1));
	}
	// the movie's map title was set through APT:MapTitle, the description is the MapInfo row
	CHECK(fx.wm->aptText("APT:MapTitle") != nullptr);
	GameWindow *info = fx.screen->mapInfoWindow();
	REQUIRE(info);
	CHECK(GadgetListBoxGetNumEntries(info) == 1);
	// no notes about something missing from the environment but the profile store
	CHECK(fx.wm->noteCount("skirmish-setup-failed") == 0);
	CHECK(fx.wm->noteCount("unknown-screen-ref") == 0);
	CHECK(fx.wm->noteCount("unknown-gadget") == 0);
	CHECK(fx.wm->noteCount("extern-read-unhandled") == 0);
}

TEST_CASE("retail lobby API: handler and setter level: profile commands, map list click, setters, StartGame -> the GameInfo (unit level; the acceptance path is the input test below)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	// [S-179] OnInitialized with no profile: the engine sets `_root.vShowAddProfile` once the movie's frame-0 actions ran; the movie's own hook (a frame
	// action of the profile panel) then opens the add-profile popup and sends OnNewProfileMenu, which brings the popup's gadgets into being
	CHECK(fx.screen->initializedCount() == 1);
	CHECK(fx.screen->newProfileMenuCount() == 1);
	REQUIRE(fx.screen->profileEntryWindow());
	{
		AptValue v;
		REQUIRE(fx.wm->apt().level(1)->getMember("vShowAddProfile", v));
		CHECK_FALSE(v.toBoolean(7)); // consumed by the movie
	}
	// the new-profile popup: the entry is cleared, a name is typed, OnAddProfileAccept makes it the player's profile
	fx.wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u"  Gimli ");
	fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	CHECK(fx.screen->profiles().current() == u"Gimli");
	CHECK(GadgetTextEntryGetText(fx.screen->profileEntryWindow()).empty());
	// the change page lists it, selected
	showProfilePage(fx, "_change");
	REQUIRE(fx.screen->profileListWindow());
	CHECK(GadgetListBoxGetNumEntries(fx.screen->profileListWindow()) == 1);
	CHECK(GadgetListBoxGetText(fx.screen->profileListWindow(), 0, 0) == u"Gimli");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Gimli");
	showProfilePage(fx, "_new");
	// a second add with the same name (any case) is refused
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u"gimli");
	fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	CHECK(fx.wm->noteCount("profile-rejected") == 1);
	CHECK(fx.screen->profiles().names().size() == 1);

	// the popup is closed through the movie's own function (the player's click on OK / Cancel runs it)
	REQUIRE(fx.wm->invokeASAt(1, "ProfilePopup", "ClosePop", {}));
	fx.tick(30);

	// the MapList: a click on row 1, then on row 0
	GameWindow *list = fx.screen->mapListWindow();
	REQUIRE(list);
	const std::vector<std::string> keys = fx.screen->mapListKeys();
	REQUIRE(keys.size() >= 2);
	clickListRow(fx, list, 1);
	CHECK(fx.screen->setup()->info().mapName == keys[1]);
	clickListRow(fx, list, 0);
	CHECK(fx.screen->setup()->info().mapName == keys[0]);
	const MapCacheEntry *map = nullptr;
	for (const MapCacheEntry &m : fx.setup.maps())
	{
		if (m.name == keys[0])
		{
			map = &m;
		}
	}
	REQUIRE(map);

	// slot 0: the Elves; slot 1: the Brutal AI entry of its Player combo
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(0, "PlayerTemplate"), 2, false); // Random, Men, Elves
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(1, "Player"), 5, false); // Open, Closed, Easy, Medium, Hard, Brutal
	CHECK(fx.screen->setup()->info().slots[1].state == SLOT_BRUTAL_AI);

	// StartGame: the movie's button sends AptSkirmish::StartGame
	AptButtonInst *start = firstButtonIn(fx.wm->apt().resolvePath(fx.wm->apt().level(1), "lobby.StartGame"));
	REQUIRE_MESSAGE(start, "_root.lobby.StartGame");
	REQUIRE(fx.sink.messages.empty());
	clickButton(fx, *start);
	fx.tick(30);
	CHECK(fx.screen->commandCalls("AptSkirmish::StartGame") == 1);
	REQUIRE_MESSAGE(fx.sink.messages.size() == 1, "refusals: " << fx.sink.refusals.size());
	CHECK(fx.sink.refusals.empty());
	const NewGameMessage &msg = fx.sink.messages[0];
	CHECK(msg.mode == NewGameMode::Skirmish);
	CHECK(msg.difficulty == 1);
	CHECK(msg.rankPoints == 0);

	SkirmishGameInfo expected;
	expected.mapName = map->name;
	expected.mapCRC = map->fileCRC;
	expected.mapSize = map->fileSize;
	expected.mapMask = fx.setup.mapContentsMask(*map);
	expected.seed = 4242;
	expected.startingCash = -1; // lane RULES-1: unset; the InitialResources rule gives the money
	expected.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	expected.inProgress = true;
	expected.slots[0].state = SLOT_PLAYER;
	expected.slots[0].name = u"Gimli";
	expected.slots[0].accepted = true;
	expected.slots[0].playerTemplate = factionIndex(fx.setup, "FactionElves");
	expected.slots[1].state = SLOT_BRUTAL_AI;
	expected.slots[1].name = u"Brutal AI";
	expected.slots[1].accepted = true;
	for (int i = 2; i < MAX_SLOTS; ++i)
	{
		expected.slots[i].state = SLOT_CLOSED;
		expected.slots[i].name = u"Closed";
		expected.slots[i].accepted = true;
	}
	CHECK(msg.game == expected);
	CHECK(msg.game.numPlayers() == 2);
	// a second StartGame from the same lobby is ignored (reported)
	fx.wm->invokeCallback("AptSkirmish::StartGame", "");
	fx.tick(2); // the screen update validates and posts (RotWK state 10)
	CHECK(fx.sink.messages.size() == 1);
	CHECK(fx.wm->noteCount("start-ignored") == 1);
}

TEST_CASE("retail lobby API: StartGame with more players than the map holds is refused with the formatted message and posts nothing")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	// lane MENU-1: with no profile the add-profile box is up and its OpenPop disables the game setup's gadgets (DisableComponents, RW 0x81456F): the
	// player has a profile here, so the lobby takes the clicks
	MemorySkirmishProfiles profiles;
	REQUIRE(profiles.add(u"Gimli"));
	profiles.setCurrent(u"Gimli");
	LobbyFx fx(mount, false);
	fx.environment.profiles = &profiles;
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	GameWindow *list = fx.screen->mapListWindow();
	REQUIRE(list);
	clickListRow(fx, list, 0);
	const MapCacheEntry *map = nullptr;
	for (const MapCacheEntry &m : fx.setup.maps())
	{
		if (m.name == fx.screen->mapListKeys()[0])
		{
			map = &m;
		}
	}
	REQUIRE(map);
	REQUIRE(map->numPlayers == 2);
	for (int slot = 1; slot < 3; ++slot)
	{
		GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(slot, "Player"), 2, false); // every slot an easy AI
	}
	CHECK(fx.screen->setup()->info().numPlayers() == 3);
	fx.wm->invokeCallback("AptSkirmish::StartGame", "");
	fx.tick(2); // the screen update validates and posts (RotWK state 10)
	CHECK(fx.sink.messages.empty());
	REQUIRE(fx.sink.refusals.size() == 1);
	CHECK(fx.sink.refusals[0].first.empty()); // lane GAMEMODES-1: MessageBoxOk(L"", text), RW 0x928482
	CHECK(fx.sink.refusals[0].second == asciiToU16("Too many players: the map has " + std::to_string(map->numPlayers)));
	REQUIRE(fx.screen->ruleBox());
	CHECK(fx.screen->ruleBox()->type() == AptMessageBox::TYPE_OK);
	CHECK(fx.screen->ruleBox()->title().empty());
	CHECK(fx.screen->ruleBox()->text() == fx.sink.refusals[0].second);
	fx.screen->ruleBox()->hide(true); // the API harness also runs the movie button's hide action
	fx.wm->invokeCallback("_level" + std::to_string(fx.screen->ruleBox()->level()) + ".MessageBox_OnButtonOk", "");
	fx.tick(30);
	CHECK(fx.screen->ruleBox()->type() == AptMessageBox::TYPE_NONE);
	// slots over the map's size are closed again: the lobby starts
	for (int slot = (int)map->numPlayers; slot < LEGACY_MAX_SLOTS; ++slot)
	{
		GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(slot, "Player"), 1, false); // Closed
	}
	fx.wm->invokeCallback("AptSkirmish::StartGame", "");
	fx.tick(2); // the screen update validates and posts (RotWK state 10)
	REQUIRE(fx.sink.messages.size() == 1);
	CHECK(fx.sink.messages[0].game.numPlayers() == (int)map->numPlayers);
}

TEST_CASE("retail lobby API: a colour taken by one slot disappears from the colour combos of the others; Open / Closed slots lose their faction")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	GameWindow *c0 = fx.screen->slotGadget(0, "Color");
	GameWindow *c1 = fx.screen->slotGadget(1, "Color");
	GadgetImageComboBoxSetSelectedPos(c0, 1); // the first real colour
	CHECK(fx.screen->setup()->info().slots[0].color == 0);
	CHECK(GadgetImageComboBoxGetLength(c0) == 11);
	CHECK(GadgetImageComboBoxGetLength(c1) == 10);
	{
		int sel = -2;
		GadgetImageComboBoxGetSelectedPos(c0, &sel);
		CHECK(sel == 1);
	}
	// slot 1 takes the colour that is now first in its list (colour index 1)
	GadgetImageComboBoxSetSelectedPos(c1, 1);
	CHECK(fx.screen->setup()->info().slots[1].color == 1);
	// slot 1's faction goes away when the slot is opened
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(1, "PlayerTemplate"), 3, false);
	CHECK(fx.screen->setup()->info().slots[1].playerTemplate >= 0);
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(1, "Player"), 0, false); // Open
	CHECK(fx.screen->setup()->info().slots[1].playerTemplate == -1);
	CHECK(fx.screen->setup()->info().slots[1].color == -1);
	CHECK(selectedPos(fx.screen->slotGadget(1, "PlayerTemplate")) == 0);
	CHECK(GadgetImageComboBoxGetLength(c0) == 11);
}

TEST_CASE("retail lobby API: profile commands (change, delete) keep the list, the current profile and slot 0 in step")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	for (const char16_t *name : { u"Frodo", u"Sam" })
	{
		fx.wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
		GadgetTextEntrySetText(fx.screen->profileEntryWindow(), name);
		fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	}
	CHECK(fx.screen->profiles().current() == u"Sam");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Sam");
	// change: the change page, select row 0, OnChangeProfile
	showProfilePage(fx, "_change");
	REQUIRE(fx.screen->profileListWindow());
	fx.wm->invokeCallback("AptSkirmish::OnChangeProfileMenu", "");
	CHECK(GadgetListBoxGetNumEntries(fx.screen->profileListWindow()) == 2);
	GadgetListBoxSetSelected(fx.screen->profileListWindow(), 0);
	fx.wm->invokeCallback("AptSkirmish::OnChangeProfile", "");
	CHECK(fx.screen->profiles().current() == u"Frodo");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Frodo");
	// delete: the delete page, the current profile is selected, OnDeleteProfile removes it and the other one becomes current
	showProfilePage(fx, "_delete");
	REQUIRE(fx.screen->profileListWindow());
	fx.wm->invokeCallback("AptSkirmish::OnDeleteProfileMenu", "");
	fx.wm->invokeCallback("AptSkirmish::OnDeleteProfile", "");
	CHECK(fx.screen->profiles().names() == std::vector<std::u16string>{ u"Sam" });
	CHECK(fx.screen->profiles().current() == u"Sam");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Sam");
	CHECK(GadgetListBoxGetNumEntries(fx.screen->profileListWindow()) == 1);
	// deleting the last one leaves the default name (GUI:Player)
	fx.wm->invokeCallback("AptSkirmish::OnDeleteProfile", "");
	CHECK(fx.screen->profiles().current().empty());
	CHECK(fx.screen->setup()->info().slots[0].name == u"Player");
	// lane ONLINEHUB-1: the statistics page (Stats.swf's AptStats registrations) is made when the player opens it and goes when they leave it
	CHECK_FALSE(fx.wm->hasCommand("Stats::OnSelectFaction"));
	fx.wm->invokeCallback("AptSkirmish::OnStatsMenu", "");
	CHECK(fx.wm->hasCommand("Stats::OnSelectFaction"));
	fx.wm->invokeCallback("AptSkirmish::OnExitStatsScreen", "");
	CHECK_FALSE(fx.wm->hasCommand("Stats::OnSelectFaction"));
}

TEST_CASE("retail lobby: a screen with no setup source or sink reports it and refuses StartGame; the registered names are the binary's")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount, false);
	fx.environment.skirmish = nullptr;
	fx.shell->push("Skirmish.apt");
	fx.tick(30);
	AptSkirmish *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	CHECK(screen->setup() == nullptr);
	CHECK(fx.wm->noteCount("skirmish-environment-missing") >= 1);
	fx.wm->invokeCallback("AptSkirmish::StartGame", "");
	fx.tick(2); // the screen update validates and posts (RotWK state 10)
	CHECK(fx.sink.messages.empty());
	// every AptSkirmish and lobby name the screen owns is registered (commands, providers, screen references, the picture component)
	for (const std::string &name : AptSkirmish::retailNames())
	{
		const bool known = name == "AptSkirmish::InitGadgets" ? fx.wm->hasScreenRef(name) : fx.wm->hasCommand(name);
		CHECK_MESSAGE(known, name);
	}
	for (const char *name : { "MpGameSetup::HostMode", "MpGameSetup::IsInitialized", "AptMpGameRules::ShowChat", "AptMpGameRules::ShowClans", "MpGameRules::NumCheckBoxes",
		     "MpGameRules::NumComboBoxes", "AptMapPreview::GameMapType" })
	{
		CHECK_MESSAGE(fx.wm->hasProvider(name), name);
	}
	for (const char *name : { "MpGameSetup::InitGadgets", "AptMapPreview::MapGadgetInit", "AptMpGameRules::InitGadgets", "AptMpChat::InitGadgets", "AptMpClans::InitGadgets" })
	{
		CHECK_MESSAGE(fx.wm->hasScreenRef(name), name);
	}
}

// ============================================================================================================================
// Stops S-178 / S-179 (pinned: what the lobby reports instead of guessing)
// ============================================================================================================================

TEST_CASE("stops S-178 / S-179: the lobby reports its inferences, the profile gap and the manager's `_global.InGame`")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	auto detail = [&](const std::string &kind) {
		for (const WindowManagerNote &n : fx.wm->notes())
		{
			if (n.kind == kind)
			{
				return n.detail;
			}
		}
		return std::string("<none>");
	};
	CHECK(detail("skirmish-inferences").find("[S-178]") == 0);
	CHECK(detail("skirmish-inferences").find("faction rule") != std::string::npos);
	CHECK(detail("skirmish-profile-unverified").find("[S-179]") == 0);
	CHECK(detail("skirmish-environment-missing").find("[S-179]") != std::string::npos); // the in-memory profile store
	// lane FB7-1 / CAH-2: RW 0x842C93 fills the Hero combo only when TheCreateAHeroSystem exists; this lobby has no Create-a-Hero context, so the
	// combo stays empty (with one: "-", GUI:Random and the heroes, test_cah1.cpp). Lane OWNERFIX-2: the Handicap combo is RotWK's (RW 0x840B9D): 0% .. -95%
	// in steps of 5, item data the value, the first selected
	GameWindow *hero = fx.screen->slotGadget(0, "Hero");
	REQUIRE(hero);
	CHECK(GadgetComboBoxGetLength(hero) == 0);
	GameWindow *handicap = fx.screen->slotGadget(0, "Handicap");
	REQUIRE(handicap);
	REQUIRE(GadgetComboBoxGetLength(handicap) == 20);
	for (int i = 0; i < 20; ++i)
	{
		CHECK((std::intptr_t)GadgetComboBoxGetItemData(handicap, i) == -5 * i);
	}
	CHECK(GadgetComboBoxGetText(handicap) == u"0%");
	// the entry chosen goes through the screen's handler to the slot (RW 0x840485 -> MpOwnerSelectHandicap: GameSlot + 0x20)
	GadgetComboBoxSetSelectedPos(handicap, 5, false);
	CHECK(fx.screen->setup()->info().slots[0].handicap == -25);
	GadgetComboBoxSetSelectedPos(handicap, 0, false);
	CHECK(fx.screen->setup()->info().slots[0].handicap == 0);
	// lane RULES-1: the rule rows of mode 0 (RW 0x960A46 / 0x960A5D: 2 combos + OpenBFME's Legendary Heroes row, 3 check boxes + lane CHEAT-1's Cheat
	// Mode row); the clan tab answers 0
	std::string value;
	REQUIRE(fx.wm->getExtern("MpGameRules::NumCheckBoxes", value) == AptExternResult::Value);
	CHECK(value == "4");
	REQUIRE(fx.wm->getExtern("MpGameRules::NumComboBoxes", value) == AptExternResult::Value);
	CHECK(value == "3");
	REQUIRE(fx.wm->getExtern("AptMpGameRules::ShowClans", value) == AptExternResult::Value);
	CHECK(value == "0");
	// SuppressStrategicPromo is remembered nowhere ([S-179])
	fx.wm->invokeCallback("AptSkirmish::SuppressStrategicPromo", "");
	CHECK(detail("unported-command").size() > 0);
	// the manager sets `_global.InGame` (nothing in the movies writes it)
	AptValue v;
	REQUIRE(fx.wm->apt().vm().global()->getMember("InGame", v));
	CHECK(v.toBoolean(7));
}

// ============================================================================================================================
// Retail lobby driven through input only (mouse, wheel, text and key events); the movie sends the fscommands
// ============================================================================================================================

namespace
{
void clickWindowCentre(LobbyFx &fx, GameWindow *w)
{
	int x = 0, y = 0, a = 0, b = 0;
	w->winGetScreenPosition(&x, &y);
	w->winGetSize(&a, &b);
	clickAt(fx, x + a / 2.0f, y + b / 2.0f);
}

// Finds the screen y of a row's first pixel row by asking the list where its rows are (reading geometry is not an action); -1 when the row is not in view.
int rowScreenY(GameWindow *list, int row, int x)
{
	int sx = 0, sy = 0, w = 0, h = 0;
	list->winGetScreenPosition(&sx, &sy);
	list->winGetSize(&w, &h);
	(void)w;
	for (int y = sy + 1; y < sy + h - 1; ++y)
	{
		int r = -1, c = -1;
		GadgetListBoxGetEntryBasedOnXY(list, x, y, r, c);
		if (r == row)
		{
			return y + 1;
		}
	}
	return -1;
}

// Brings `row` into view with the mouse wheel over the list and clicks it.
void wheelAndClickRow(LobbyFx &fx, GameWindow *list, int row)
{
	int sx = 0, sy = 0, w = 0, h = 0;
	list->winGetScreenPosition(&sx, &sy);
	list->winGetSize(&w, &h);
	const int x = sx + w / 3;
	for (int guard = 0; guard < 400; ++guard)
	{
		const int y = rowScreenY(list, row, x);
		if (y >= 0)
		{
			clickAt(fx, (float)x, (float)y);
			return;
		}
		fx.wm->postMouseMove((float)x, (float)(sy + h / 2));
		fx.tick(1);
		fx.wm->postMouseWheel(GadgetListBoxGetTopVisibleEntry(list) > row ? 1 : -1);
		fx.tick(1);
	}
	FAIL("the wheel did not bring row " << row << " into view");
}

// Opens a ZH combo box with its drop-down button and clicks a row.
void chooseComboRow(LobbyFx &fx, GameWindow *combo, int row)
{
	GameWindow *drop = GadgetComboBoxGetDropDownButton(combo);
	REQUIRE(drop);
	clickWindowCentre(fx, drop);
	GameWindow *list = GadgetComboBoxGetListBox(combo);
	REQUIRE(list);
	REQUIRE_FALSE(list->winIsHidden());
	wheelAndClickRow(fx, list, row);
	CHECK(list->winIsHidden());
}

// The ImageComboBox opens on a click in its box.
void chooseImageComboRow(LobbyFx &fx, GameWindow *box, int row)
{
	int x = 0, y = 0, w = 0, h = 0;
	box->winGetScreenPosition(&x, &y);
	box->winGetSize(&w, &h);
	clickAt(fx, x + w / 4.0f, y + h / 2.0f);
	GameWindow *list = GadgetImageComboBoxGetListBox(box);
	REQUIRE(list);
	REQUIRE_FALSE(list->winIsHidden());
	wheelAndClickRow(fx, list, row);
	CHECK(list->winIsHidden());
}

void typeText(LobbyFx &fx, GameWindow *entry, const std::u16string &text)
{
	clickWindowCentre(fx, entry);
	for (char16_t c : text)
	{
		fx.wm->postTextInput(c);
		fx.tick(1);
	}
	fx.tick(3);
}

AptButtonInst *popupButton(LobbyFx &fx, const char *path)
{
	AptButtonInst *b = firstButtonIn(fx.wm->apt().resolvePath(fx.wm->apt().level(1), path));
	return b && b->globallyVisible() ? b : nullptr;
}

// The movie's own profile menu: ProfileNav.bttn opens it, then one of its entries.
void profileMenu(LobbyFx &fx, const char *entry)
{
	AptButtonInst *nav = popupButton(fx, "ProfileNav");
	REQUIRE(nav);
	clickButton(fx, *nav);
	fx.tick(30);
	AptButtonInst *item = popupButton(fx, (std::string("ProfileNav.") + entry).c_str());
	REQUIRE_MESSAGE(item, entry);
	clickButton(fx, *item);
	fx.tick(30);
}

// Types a new profile into the open popup and clicks its Select (accept) button.
void addProfileByInput(LobbyFx &fx, const std::u16string &name)
{
	REQUIRE(fx.screen->profileEntryWindow());
	typeText(fx, fx.screen->profileEntryWindow(), name);
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE_MESSAGE(select, "typing a name enables the popup's Select button");
	clickButton(fx, *select);
	fx.tick(40);
}
} // namespace

TEST_CASE("retail lobby input: the acceptance click-through: type a profile, accept, map 0, Elves, Brutal AI, Start -> the exact GameInfo and one new-game message")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	// profile first: the screen update showed the main page and, with no profile, the add-profile popup (which sent OnNewProfileMenu)
	CHECK(fx.screen->initializedCount() == 1);
	CHECK(fx.screen->newProfileMenuCount() == 1);
	CHECK(fx.wm->noteCount("movie-function-missing") == 0);
	REQUIRE(fx.screen->profileEntryWindow());
	// nothing is typed: the Select button is not there, Cancel is
	CHECK(popupButton(fx, "ProfilePopup.Main.Select") == nullptr);
	CHECK(popupButton(fx, "ProfilePopup.Main.Cancel") != nullptr);
	CHECK(fx.screen->profileAcceptDisabled());
	// typing enables it (GEM_UPDATE_TEXT -> PopupSelectBttnEnable), the click on it runs OnAddProfileAccept
	typeText(fx, fx.screen->profileEntryWindow(), u"Gimli");
	CHECK(GadgetTextEntryGetText(fx.screen->profileEntryWindow()) == u"Gimli");
	CHECK_FALSE(fx.screen->profileAcceptDisabled());
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(40);
	CHECK(fx.screen->commandCalls("AptSkirmish::OnAddProfileAccept") == 1);
	CHECK(fx.screen->profiles().current() == u"Gimli");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Gimli");
	CHECK(popupButton(fx, "ProfilePopup.Main.Cancel") == nullptr); // the movie closed the popup

	// map 0 of the list: scrolled into view with the wheel, then clicked
	GameWindow *list = fx.screen->mapListWindow();
	REQUIRE(list);
	const std::vector<std::string> keys = fx.screen->mapListKeys();
	REQUIRE(keys.size() >= 2);
	wheelAndClickRow(fx, list, 1);
	CHECK(fx.screen->setup()->info().mapName == keys[1]);
	wheelAndClickRow(fx, list, 0);
	CHECK(fx.screen->setup()->info().mapName == keys[0]);
	const MapCacheEntry *map = nullptr;
	for (const MapCacheEntry &m : fx.setup.maps())
	{
		if (m.name == keys[0])
		{
			map = &m;
		}
	}
	REQUIRE(map);

	// slot 0: Elves from its faction combo; slot 1: Brutal AI (row 5 of Open, Closed, Easy, Medium, Hard, Brutal)
	chooseComboRow(fx, fx.screen->slotGadget(0, "PlayerTemplate"), 2);
	chooseComboRow(fx, fx.screen->slotGadget(1, "Player"), 5);
	CHECK(fx.screen->setup()->info().slots[1].state == SLOT_BRUTAL_AI);
	// slot 0 takes the second colour from the image combo
	chooseImageComboRow(fx, fx.screen->slotGadget(0, "Color"), 2);

	// Start: the movie's button
	AptButtonInst *start = popupButton(fx, "lobby.StartGame");
	REQUIRE(start);
	REQUIRE(fx.sink.messages.empty());
	clickButton(fx, *start);
	fx.tick(30);
	CHECK(fx.screen->commandCalls("AptSkirmish::StartGame") == 1);
	REQUIRE(fx.sink.messages.size() == 1);
	CHECK(fx.sink.refusals.empty());
	const NewGameMessage &msg = fx.sink.messages[0];
	CHECK(msg.mode == NewGameMode::Skirmish);
	CHECK(msg.difficulty == 1);

	SkirmishGameInfo expected;
	expected.mapName = map->name;
	expected.mapCRC = map->fileCRC;
	expected.mapSize = map->fileSize;
	expected.mapMask = fx.setup.mapContentsMask(*map);
	expected.seed = 4242;
	expected.startingCash = -1; // lane RULES-1: unset; the InitialResources rule gives the money
	expected.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	expected.inProgress = true;
	expected.slots[0].state = SLOT_PLAYER;
	expected.slots[0].name = u"Gimli";
	expected.slots[0].accepted = true;
	expected.slots[0].playerTemplate = factionIndex(fx.setup, "FactionElves");
	expected.slots[0].color = 1; // the second row of the colour list is colour index 1 (row 0 is the random entry)
	expected.slots[1].state = SLOT_BRUTAL_AI;
	expected.slots[1].name = u"Brutal AI";
	expected.slots[1].accepted = true;
	for (int i = 2; i < MAX_SLOTS; ++i)
	{
		expected.slots[i].state = SLOT_CLOSED;
		expected.slots[i].name = u"Closed";
		expected.slots[i].accepted = true;
	}
	CHECK(msg.game == expected);
	CHECK(msg.game.slots[1].state == 5); // the retail item data of GUI:BrutalAI
	// the random choices are still open and reported (S-178)
	CHECK(msg.game.unresolvedRandomChoices() == std::vector<std::string>{ "slot 0 start position", "slot 1 start position", "slot 1 faction", "slot 1 colour" });
	bool reported = false;
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		reported = reported || n.kind == "skirmish-random-unresolved";
	}
	CHECK(reported);
	// the validation passed: the movie got CloseMain (no ButtonReset); a further Start, if the button is still there, posts nothing more
	CHECK(fx.wm->noteCount("movie-function-missing") == 0);
	if (AptButtonInst *again = popupButton(fx, "lobby.StartGame"))
	{
		clickButton(fx, *again);
		fx.tick(30);
	}
	CHECK(fx.sink.messages.size() == 1);
}

TEST_CASE("retail lobby input: Enter in the persona entry accepts the name through the retail accept action; a duplicate is rejected; deleting the text disables Select again")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.screen->profileEntryWindow());
	// type a letter and remove it again: Select appears, then goes (Disable is sent when the raw text is empty)
	typeText(fx, fx.screen->profileEntryWindow(), u"G");
	CHECK(popupButton(fx, "ProfilePopup.Main.Select") != nullptr);
	fx.wm->postGadgetKey(KEY_BACKSPACE, true);
	fx.tick(1);
	fx.wm->postGadgetKey(KEY_BACKSPACE, false);
	fx.tick(10);
	CHECK(GadgetTextEntryGetText(fx.screen->profileEntryWindow()).empty());
	CHECK(fx.screen->profileAcceptDisabled());
	CHECK(popupButton(fx, "ProfilePopup.Main.Select") == nullptr);
	// whitespace only: the raw text is not empty, so Select is enabled, and the add handler rejects the empty trimmed name
	typeText(fx, fx.screen->profileEntryWindow(), u"  ");
	CHECK_FALSE(fx.screen->profileAcceptDisabled());
	fx.wm->postTextInput(u'\r'); // GEM_EDIT_DONE with data2 == 0
	fx.tick(30);
	CHECK(fx.screen->profiles().current().empty());
	CHECK(fx.wm->noteCount("profile-rejected") == 1);
	// the retail accept action closes the popup first (CloseProfilePopup, RotWK 0x9294B3) and only then adds: the rejection leaves it closed
	CHECK(popupButton(fx, "ProfilePopup.Main.Cancel") == nullptr);
	// reopen it through the menu and enter a name: Enter adds the profile in the new-profile state
	profileMenu(fx, "NewProfile");
	REQUIRE(fx.screen->profileEntryWindow());
	typeText(fx, fx.screen->profileEntryWindow(), u"Gimli");
	fx.wm->postTextInput(u'\r');
	fx.tick(40);
	CHECK(fx.screen->profiles().current() == u"Gimli");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Gimli");
	CHECK(popupButton(fx, "ProfilePopup.Main.Cancel") == nullptr);
	CHECK(fx.wm->noteCount("movie-function-missing") == 0);
	// the same name again (any case) through the menu and the Select button: rejected, the profile list is unchanged
	profileMenu(fx, "NewProfile");
	REQUIRE(fx.screen->profileEntryWindow());
	typeText(fx, fx.screen->profileEntryWindow(), u"GIMLI");
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(30);
	CHECK(fx.wm->noteCount("profile-rejected") == 2);
	CHECK(fx.screen->profiles().names() == std::vector<std::u16string>{ u"Gimli" });
}

TEST_CASE("retail lobby input: Cancel in the new-profile popup with no profile leaves the screen; with a profile it closes the popup")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	REQUIRE(fx.shell->screenCount() == 1);
	AptButtonInst *cancel = popupButton(fx, "ProfilePopup.Main.Cancel");
	REQUIRE(cancel);
	clickButton(fx, *cancel);
	fx.tick(40);
	CHECK(fx.shell->screenCount() == 0); // RotWK 0x927FDA: no profile, no lobby
}

TEST_CASE("retail lobby input: change and delete profiles through the movie's profile menu, the list clicks and the double click")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	addProfileByInput(fx, u"Frodo");
	profileMenu(fx, "NewProfile");
	addProfileByInput(fx, u"Sam");
	CHECK(fx.screen->profiles().current() == u"Sam");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Sam");
	// change: the change page lists both, a click selects Frodo, the Select button of the page changes the profile
	profileMenu(fx, "ChangeProfile");
	GameWindow *list = fx.screen->profileListWindow();
	REQUIRE(list);
	CHECK(GadgetListBoxGetNumEntries(list) == 2);
	wheelAndClickRow(fx, list, 0);
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(40);
	CHECK(fx.screen->commandCalls("AptSkirmish::OnChangeProfile") == 1);
	CHECK(fx.screen->profiles().current() == u"Frodo");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Frodo");
	// the double click on a row is the accept action (RotWK 0x929589: GLM_DOUBLE_CLICKED -> 0x9294B3): it changes back to Sam
	profileMenu(fx, "ChangeProfile");
	list = fx.screen->profileListWindow();
	REQUIRE(list);
	const int samRow = GadgetListBoxGetText(list, 0, 0) == u"Sam" ? 0 : 1;
	const int x = [&] { int sx = 0, sy = 0, w = 0, h = 0; list->winGetScreenPosition(&sx, &sy); list->winGetSize(&w, &h); return sx + w / 3; }();
	const int y = rowScreenY(list, samRow, x);
	REQUIRE(y >= 0);
	clickAt(fx, (float)x, (float)y);
	clickAt(fx, (float)x, (float)y);
	fx.tick(40);
	CHECK(fx.screen->profiles().current() == u"Sam");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Sam");
	// delete: the delete page, select Sam (the current profile), the page's Select button removes it and Frodo becomes current
	profileMenu(fx, "DeleteProfile");
	list = fx.screen->profileListWindow();
	REQUIRE(list);
	const int samRow2 = GadgetListBoxGetText(list, 0, 0) == u"Sam" ? 0 : 1;
	wheelAndClickRow(fx, list, samRow2);
	select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(40);
	CHECK(fx.screen->commandCalls("AptSkirmish::OnDeleteProfile") == 1);
	CHECK(fx.screen->profiles().names() == std::vector<std::u16string>{ u"Frodo" });
	CHECK(fx.screen->profiles().current() == u"Frodo");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Frodo");
}

TEST_CASE("retail lobby input: StartGame with more players than the map holds is refused; closing the extra slots through the combos lets it start")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	addProfileByInput(fx, u"Gimli");
	GameWindow *list = fx.screen->mapListWindow();
	REQUIRE(list);
	wheelAndClickRow(fx, list, 0);
	const MapCacheEntry *map = nullptr;
	for (const MapCacheEntry &m : fx.setup.maps())
	{
		if (m.name == fx.screen->mapListKeys()[0])
		{
			map = &m;
		}
	}
	REQUIRE(map);
	for (int slot = 1; slot < LEGACY_MAX_SLOTS; ++slot)
	{
		chooseComboRow(fx, fx.screen->slotGadget(slot, "Player"), 2); // easy AI
	}
	CHECK(fx.screen->setup()->info().numPlayers() == 8);
	AptButtonInst *start = popupButton(fx, "lobby.StartGame");
	REQUIRE(start);
	clickButton(fx, *start);
	fx.tick(30);
	CHECK(fx.sink.messages.empty());
	REQUIRE(fx.sink.refusals.size() == 1);
	CHECK(fx.sink.refusals[0].first.empty()); // lane GAMEMODES-1: MessageBoxOk(L"", text), RW 0x928482
	CHECK(fx.sink.refusals[0].second == asciiToU16("Too many players: the map has " + std::to_string(map->numPlayers)));
	REQUIRE(fx.screen->ruleBox());
	CHECK(fx.screen->ruleBox()->type() == AptMessageBox::TYPE_OK);
	CHECK(fx.screen->ruleBox()->title().empty());
	CHECK(fx.screen->ruleBox()->text() == fx.sink.refusals[0].second);
	AptButtonInst *ok = firstButtonIn(fx.wm->apt().resolvePath(fx.wm->apt().level(fx.screen->ruleBox()->level()), "MessageBox.ButtonsOk.MessageBoxOk"));
	REQUIRE(ok);
	clickButton(fx, *ok);
	fx.tick(30);
	CHECK(fx.screen->ruleBox()->type() == AptMessageBox::TYPE_NONE);
	for (int slot = (int)map->numPlayers; slot < LEGACY_MAX_SLOTS; ++slot)
	{
		chooseComboRow(fx, fx.screen->slotGadget(slot, "Player"), 1); // closed
	}
	clickButton(fx, *start);
	fx.tick(30);
	REQUIRE(fx.sink.messages.size() == 1);
	CHECK(fx.sink.messages[0].game.numPlayers() == (int)map->numPlayers);
}

// ============================================================================================================================
// Lifetime of the screen's update (review P1): nothing of a destroyed screen may run
// ============================================================================================================================

TEST_CASE("skirmish lifetime: a screen removed before the next update leaves no update listener and no deferred write (ASan pins the use-after-free)")
{
	MemorySource source;
	RecordingShellServices services;
	ShellEnvironment env;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	WindowManager wm(source, services);
	Shell shell(wm, factories, services, env);
	shell.push("Skirmish.apt");
	REQUIRE(shell.top());
	CHECK(wm.updateListenerCount() == 1);
	REQUIRE(wm.invokeCallback("AptSkirmish::OnInitialized", ""));
	REQUIRE(wm.invokeCallback("AptSkirmish::StartGame", ""));
	shell.clear();
	CHECK(wm.updateListenerCount() == 0);
	wm.update(33); // the old code wrote through a freed screen here
	wm.update(33);
	// the level of the removed screen is reused by the next one: its own update runs, the old one never does
	shell.push("Skirmish.apt");
	REQUIRE(shell.top());
	CHECK(wm.updateListenerCount() == 1);
	wm.update(33);
	shell.pop();
	CHECK(wm.updateListenerCount() == 0);
	wm.update(33);
}

// Lane FB7-1 r3 (the owner's report: "clicking a start position on the map preview does nothing"): RotWK's lobby handler RW 0x845830 for the map
// window's spots (GBM_SELECTED / GBM_SELECTED_RIGHT forwarded by PassSelectedButtonsToParentSystem RW 0x6C1501) and updateMapStartSpots RW 0x7052BE
TEST_CASE("fb7 retail lobby input: a click on a start spot gives it to the player, the next click passes it to the AI, then frees it; a right click frees it; the spot shows NUMBER:<slot>")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.text.set("NUMBER:1", "1");
	fx.text.set("NUMBER:2", "2");
	fx.text.set("TOOLTIP:StartPosition", "Start position");
	fx.text.set("TOOLTIP:StartPositionN", "Player %d starts here");
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	typeText(fx, fx.screen->profileEntryWindow(), u"Gimli");
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(40);
	const SkirmishGameInfo &info = fx.screen->setup()->info();
	// the default lobby: the player in slot 0, an easy AI in slot 1 (SkirmishGameOptionsMenuInit), the default map's spots shown
	REQUIRE(info.slots[1].isAI());
	const std::vector<GameWindow *> &spots = fx.screen->mapStartSpotWindows();
	REQUIRE(spots.size() == 8);
	REQUIRE_FALSE(spots[0]->winIsHidden());
	REQUIRE_FALSE(spots[1]->winIsHidden());
	CHECK(info.slots[0].startPos == -1);
	CHECK(info.slots[1].startPos == -1);
	CHECK(spots[0]->winGetText().empty());
	CHECK(spots[0]->winGetInstanceData()->getTooltipText() == u"Start position");
	// lane ONLINE-5: the skirmish panel has no Ready column (no flag 0x20): every Ready box of the movie was told "_none" (RW 0x84211D)
	for (int slot = 0; slot < LEGACY_MAX_SLOTS; ++slot)
	{
		CHECK_MESSAGE(fx.screen->readyIcon(slot) == "_none", "slot " << slot);
	}

	// a free spot: the first selectable slot (the player) takes it, the spot shows its number
	clickWindowCentre(fx, spots[0]);
	CHECK(info.slots[0].startPos == 0);
	CHECK(spots[0]->winGetText() == u"1");
	CHECK(spots[0]->winGetInstanceData()->getTooltipText() == u"Player 1 starts here");
	// the player's spot again: passed to the next selectable slot after the holder (the AI)
	clickWindowCentre(fx, spots[0]);
	CHECK(info.slots[0].startPos == -1);
	CHECK(info.slots[1].startPos == 0);
	CHECK(spots[0]->winGetText() == u"2");
	// the AI's spot: no selectable slot after it (the rest are closed): freed
	clickWindowCentre(fx, spots[0]);
	CHECK(info.slots[1].startPos == -1);
	CHECK(spots[0]->winGetText().empty());
	// another spot for the player, then a right click on it (the button's GBM_SELECTED_RIGHT through the map window): freed
	clickWindowCentre(fx, spots[1]);
	CHECK(info.slots[0].startPos == 1);
	CHECK(spots[1]->winGetText() == u"1");
	REQUIRE(fx.screen->currentMapWindow());
	spots[1]->manager().winSendSystemMsg(fx.screen->currentMapWindow(), GBM_SELECTED_RIGHT, reinterpret_cast<WindowMsgData>(spots[1]), 0);
	CHECK(info.slots[0].startPos == -1);
	CHECK(spots[1]->winGetText().empty());
}

// Lane FB7-1 r3 (found by the menu walk): the lobby's MAIN MENU button sends AptSkirmish::Exit, which the port only noted
TEST_CASE("fb7 retail lobby input: MAIN MENU leaves the lobby after a profile was accepted")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	typeText(fx, fx.screen->profileEntryWindow(), u"Gimli");
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(60);
	AptButtonInst *back = popupButton(fx, "lobby.MainMenu");
	REQUIRE(back);
	clickButton(fx, *back);
	fx.tick(120);
	// RW 0x9280E8 -> 0x927FDA: the movie's Exit pops the screen as Back does
	CHECK(fx.shell->findScreenByFilename("Skirmish.apt") == nullptr);
}

// ============================================================================================================================
// lane RULES-1: the Rules tab (RotWK's AptMpGameRules, Common/MpGameRules.h) and OpenBFME's Legendary Heroes row
// ============================================================================================================================

namespace
{
void ruleLabels(MapGameText &t)
{
	t.set("RULE:InitialResources", "Initial Resources");
	t.set("RULE:CommandPointFactor", "Command Point Factor");
	t.set("RULE:AllowCustomHeroes", "Allow Custom Heroes");
	t.set("VALUE:Value", "%d");
	t.set("VALUE:Default", " *");
	t.set("VALUE:ThirdX", "1/3X");
	t.set("VALUE:1X", "1X");
	t.set("VALUE:2X", "2X");
	t.set("APT:CommandPointsTooHigh", "Warning: too slow");
}

// the profile Gimli, the popup closed, the Rules tab clicked (the movie places the rule rows on its TabRules frame)
void openRulesTab(LobbyFx &fx)
{
	fx.tick(60);
	fx.wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
	REQUIRE(fx.screen->profileEntryWindow());
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u"Gimli");
	fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	REQUIRE(fx.wm->invokeASAt(1, "ProfilePopup", "ClosePop", {}));
	fx.tick(30);
	AptButtonInst *rulesTab = firstButtonIn(fx.wm->apt().resolvePath(fx.wm->apt().level(1), "OpenPlay.MpMapAndSettings.TabButtons.RulesTab"));
	REQUIRE_MESSAGE(rulesTab, "_level1.OpenPlay.MpMapAndSettings.TabButtons.RulesTab");
	clickButton(fx, *rulesTab);
	fx.tick(30);
}

int selectedValue(GameWindow *combo)
{
	int pos = -1;
	GadgetComboBoxGetSelectedPos(combo, &pos);
	return pos < 0 ? -9999 : (int)reinterpret_cast<std::intptr_t>(GadgetComboBoxGetItemData(combo, pos));
}
} // namespace

TEST_CASE("rules1 setup: the Rules tab model: the mode's defaults, the rows' texts, a rule change, AllowCustomHeroes off drops the heroes, Reset, Legendary Heroes")
{
	FakeSetup src;
	MapGameText text;
	SkirmishGameSetup setup = makeSetup(src, text);
	ruleLabels(text);
	std::string error;
	REQUIRE_MESSAGE(setup.init(u"Gimli", 77, std::string(), &error), error);
	CHECK(setup.rulesMode() == RULES_MODE_SKIRMISH);
	CHECK(setup.info().rules == MpGameRules::defaults(RULES_MODE_SKIRMISH));
	// RW 0x986B1C: the label's text formatted with the value, " *" on the default
	const std::vector<SkirmishComboEntry> cash = setup.ruleComboEntries(*MpGameRules::findCombo(RULES_MODE_SKIRMISH, RULE_INITIAL_RESOURCES));
	REQUIRE(cash.size() == 11);
	CHECK(cash[0].text == u"500");
	CHECK(cash[0].itemData == 500);
	CHECK(cash[2].text == u"1000 *");
	CHECK(cash[10].itemData == 4000);
	const std::vector<SkirmishComboEntry> cp = setup.ruleComboEntries(*MpGameRules::findCombo(RULES_MODE_SKIRMISH, RULE_COMMAND_POINT_FACTOR));
	CHECK(cp[0].text == u"1/3X");
	CHECK(cp[2].text == u"1X *");
	CHECK(cp[2].itemData == 100);
	CHECK(setup.ruleName(RULE_INITIAL_RESOURCES) == u"Initial Resources");
	CHECK(setup.ruleName(10) == u"Invalid Rule");
	CHECK(setup.setRule(RULE_INITIAL_RESOURCES, 2500));
	CHECK(setup.info().rules[RULE_INITIAL_RESOURCES] == 2500);
	CHECK_FALSE(setup.setRule(10, 1));
	// AllowCustomHeroes off: every slot's hero is dropped (RW 0x842A85)
	SkirmishGameInfo info = setup.info();
	info.slots[0].hasCreateAHero = true;
	setup.replaceInfo(info);
	CHECK(setup.setRule(RULE_ALLOW_CUSTOM_HEROES, 0));
	CHECK_FALSE(setup.info().slots[0].hasCreateAHero);
	// Legendary Heroes
	CHECK(setup.setLegendaryHeroes(4));
	CHECK_FALSE(setup.setLegendaryHeroes(5));
	CHECK(setup.info().legendaryHeroes == 4);
	const std::vector<SkirmishComboEntry> legendary = setup.legendaryHeroesEntries();
	REQUIRE(legendary.size() == 4);
	CHECK(legendary[0].text == u"Off *");
	CHECK(legendary[3].text == u"4x");
	CHECK(legendary[3].itemData == 4);
	// Reset: the mode's defaults and Legendary Heroes off
	setup.resetRules();
	CHECK(setup.info().rules == MpGameRules::defaults(RULES_MODE_SKIRMISH));
	CHECK(setup.info().legendaryHeroes == LegendaryHeroes::kOff);
	// strategic mode takes the War of the Ring defaults
	setup.setStrategicMode(true);
	CHECK(setup.rulesMode() == RULES_MODE_WOTR);
	CHECK(setup.info().rules == MpGameRules::defaults(RULES_MODE_WOTR));
	// the new game message carries them
	setup.setStrategicMode(false);
	setup.setLegendaryHeroes(3);
	const NewGameMessage m = setup.makeNewGameMessage();
	CHECK(m.game.rules == MpGameRules::defaults(RULES_MODE_SKIRMISH));
	CHECK(m.game.legendaryHeroes == 3);
	CHECK(m.game.startingCash == -1);
}

TEST_CASE("rules1 retail lobby: the Rules tab shows RotWK's rows (Initial Resources, Command Point Factor, Allow Custom Heroes, Allow Ring Heroes) and the "
	  "Legendary Heroes row; the controls change the game setup, warn above 1X, Reset restores; the game starts with them")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	ruleLabels(fx.text);
	openRulesTab(fx);
	REQUIRE(fx.screen->setup());
	REQUIRE(fx.screen->rulesPopulated());
	// the stops of the tab, reported once (S-3600; S-3602 closed by lane UI-6: the kept game restores the rules)
	CHECK(fx.wm->noteCount("rules-unverified") == 1);
	CHECK(std::string(AptSkirmish::kRulesStop).find("[S-3600]") == 0);
	CHECK(std::string(AptSkirmish::kRulesStop).find("[S-3601]") == std::string::npos); // resolved: the demo mode, 0 in 2.01
	CHECK(std::string(AptSkirmish::kRulesStop).find("[S-3602]") == std::string::npos);
	// MpGameSetup.apt places six combo rows and four check rows (renamed "RuleComboBox_<n>" / "RuleCheckBox_<n>", read at RW 0x814BEC); the first
	// NumComboBoxes / NumCheckBoxes are shown
	for (int i = 0; i < 6; ++i)
	{
		REQUIRE_MESSAGE(fx.screen->ruleComboWindow(i), i);
	}
	for (int i = 0; i < 4; ++i)
	{
		REQUIRE_MESSAGE(fx.screen->ruleCheckWindow(i), i);
	}
	GameWindow *cash = fx.screen->ruleWindow(RULE_INITIAL_RESOURCES);
	GameWindow *cp = fx.screen->ruleWindow(RULE_COMMAND_POINT_FACTOR);
	GameWindow *legendary = fx.screen->legendaryHeroesWindow();
	CHECK(cash == fx.screen->ruleComboWindow(0));
	CHECK(cp == fx.screen->ruleComboWindow(1));
	CHECK(legendary == fx.screen->ruleComboWindow(2));
	for (int i = 3; i < 6; ++i)
	{
		CHECK(fx.screen->ruleComboWindow(i)->winIsHidden());
	}
	CHECK(GadgetComboBoxGetLength(cash) == 11);
	CHECK(GadgetComboBoxGetLength(cp) == 7);
	CHECK(GadgetComboBoxGetLength(legendary) == 4);
	CHECK(selectedValue(cash) == 1000);
	CHECK(selectedValue(cp) == 100);
	CHECK(selectedValue(legendary) == 1);
	CHECK(cash->winGetEnabled()); // the skirmish player is the host
	// the check rows: AllowCustomHeroes, AllowRingHeroes (both on); ClanGame is state 3 outside online play: its row stays hidden
	CHECK(fx.screen->ruleWindow(RULE_ALLOW_CUSTOM_HEROES) == fx.screen->ruleCheckWindow(0));
	CHECK(fx.screen->ruleWindow(RULE_ALLOW_RING_HEROES) == fx.screen->ruleCheckWindow(1));
	CHECK(fx.screen->ruleWindow(RULE_CLAN_GAME) == nullptr);
	CHECK(GadgetCheckBoxIsChecked(fx.screen->ruleCheckWindow(0)));
	CHECK(GadgetCheckBoxIsChecked(fx.screen->ruleCheckWindow(1)));
	// lane CHEAT-1: OpenBFME's Cheat Mode row follows RotWK's rows (off), the clan row's place stays hidden after it
	CHECK(fx.screen->cheatModeWindow() == fx.screen->ruleCheckWindow(2));
	CHECK_FALSE(fx.screen->ruleCheckWindow(2)->winIsHidden());
	CHECK_FALSE(GadgetCheckBoxIsChecked(fx.screen->ruleCheckWindow(2)));
	CHECK(fx.screen->ruleCheckWindow(3)->winIsHidden());
	// the row texts (RW 0x986B1C / 0x986810)
	const std::string *t0 = fx.wm->aptText("APT:RuleComboBox_0");
	REQUIRE(t0);
	CHECK(*t0 == "Initial Resources");
	const std::string *t2 = fx.wm->aptText("APT:RuleComboBox_2");
	REQUIRE(t2);
	CHECK(t2->find("Legendary Heroes") != std::string::npos);
	const std::string *c0 = fx.wm->aptText("APT:RuleCheckBox_0");
	REQUIRE(c0);
	CHECK(*c0 == "Allow Custom Heroes");

	// the controls (RW 0x986776): 2500, 2X (above 1X: the warning box), Allow Custom Heroes off, Legendary Heroes 4x
	GadgetComboBoxSetSelectedPos(cash, 8, false);
	CHECK(fx.screen->setup()->info().rules[RULE_INITIAL_RESOURCES] == 2500);
	CHECK(fx.screen->ruleWarnings().empty());
	GadgetComboBoxSetSelectedPos(cp, 3, false);
	CHECK(fx.screen->setup()->info().rules[RULE_COMMAND_POINT_FACTOR] == 200);
	REQUIRE(fx.screen->ruleWarnings().size() == 1);
	CHECK(fx.screen->ruleWarnings()[0].second == u"Warning: too slow");
	GadgetCheckBoxToggle(fx.screen->ruleCheckWindow(0));
	CHECK(fx.screen->setup()->info().rules[RULE_ALLOW_CUSTOM_HEROES] == 0);
	REQUIRE(fx.screen->slotGadget(0, "Hero"));
	CHECK_FALSE(fx.screen->slotGadget(0, "Hero")->winGetEnabled()); // RW 0x842A85
	GadgetComboBoxSetSelectedPos(legendary, 3, false);
	CHECK(fx.screen->setup()->info().legendaryHeroes == 4);
	// lane CHEAT-1: the host's Cheat Mode box puts the cheat seat on the host's slot
	GadgetCheckBoxToggle(fx.screen->cheatModeWindow());
	CHECK(fx.screen->setup()->info().cheatMode == 0);

	// Reset (RW 0x98674C): the defaults, the widgets follow
	fx.wm->invokeCallback("AptMpGameRules::Reset", "");
	CHECK(fx.screen->setup()->info().rules == MpGameRules::defaults(RULES_MODE_SKIRMISH));
	CHECK(fx.screen->setup()->info().legendaryHeroes == LegendaryHeroes::kOff);
	CHECK(fx.screen->setup()->info().cheatMode == -1); // lane CHEAT-1
	CHECK_FALSE(GadgetCheckBoxIsChecked(fx.screen->cheatModeWindow()));
	CHECK(selectedValue(cash) == 1000);
	CHECK(selectedValue(legendary) == 1);
	CHECK(GadgetCheckBoxIsChecked(fx.screen->ruleCheckWindow(0)));
	CHECK(fx.screen->slotGadget(0, "Hero")->winGetEnabled());

	// the game starts with the rules of the tab
	GadgetComboBoxSetSelectedPos(cash, 4, false);
	GadgetComboBoxSetSelectedPos(legendary, 3, false);
	GadgetCheckBoxToggle(fx.screen->cheatModeWindow()); // lane CHEAT-1
	fx.wm->invokeCallback("AptSkirmish::StartGame", "");
	fx.tick(2);
	REQUIRE(fx.sink.messages.size() == 1);
	const NewGameMessage &msg = fx.sink.messages[0];
	CHECK(msg.game.rules[RULE_INITIAL_RESOURCES] == 1400);
	CHECK(msg.game.rules[RULE_COMMAND_POINT_FACTOR] == 100);
	CHECK(msg.game.legendaryHeroes == 4);
	CHECK(msg.game.cheatMode == 0); // lane CHEAT-1
	CHECK(msg.game.startingCash == -1);
}

// ============================================================================================================================
// lane UI-6: a Discord tester's findings on preview.6 (GitGut, 2026-10-11)
// ============================================================================================================================

TEST_CASE("ui6 retail lobby: the map, the players and the rules are kept when the lobby is left and opened again (RW 0x927FEF -> 0x821EBD, RW 0x9288D0 -> 0x821C29)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MemorySkirmishProfiles store; // the profiles outlive the screen, as Skirmish.ini does
	LobbyFx fx(mount, false);
	fx.environment.profiles = &store;
	ruleLabels(fx.text);
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	openRulesTab(fx); // the profile Gimli, the Rules tab
	CHECK(fx.screen->restoredGame().empty()); // a new profile has no kept game: the defaults
	// the Rules tab: Initial Resources 2500
	GameWindow *cash = fx.screen->ruleWindow(RULE_INITIAL_RESOURCES);
	REQUIRE(cash);
	GadgetComboBoxSetSelectedPos(cash, 8, false);
	REQUIRE(fx.screen->setup()->info().rules[RULE_INITIAL_RESOURCES] == 2500);
	// the third map, the Elves, a brutal and a medium computer
	const std::vector<std::string> keys = fx.screen->mapListKeys();
	REQUIRE(keys.size() >= 3);
	GadgetListBoxSetSelected(fx.screen->mapListWindow(), 2);
	REQUIRE(fx.screen->setup()->info().mapName == keys[2]);
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(0, "PlayerTemplate"), 2, false);
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(1, "Player"), 5, false);
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(2, "Player"), 3, false);
	const SkirmishGameInfo before = fx.screen->setup()->info();
	REQUIRE(before.slots[1].state == SLOT_BRUTAL_AI);
	REQUIRE(before.slots[2].state == SLOT_MED_AI);
	// MAIN MENU: the screen goes, its teardown keeps the game under the profile (":0:<Gimli>:Map", GameInfo, HeroIndexes)
	AptButtonInst *back = popupButton(fx, "lobby.MainMenu");
	REQUIRE(back);
	clickButton(fx, *back);
	fx.tick(120);
	REQUIRE(fx.shell->findScreenByFilename("Skirmish.apt") == nullptr);
	const std::string record = store.profileValue(0, "GameInfo");
	CHECK(record.find("M=") == 0);
	CHECK(record.find(";GR=") != std::string::npos);
	CHECK(record.find(" 2500 ") != std::string::npos); // the Initial Resources rule among the ten
	CHECK(record.find("S=HGimli,0,0,TT,") != std::string::npos);
	CHECK(record.find(":CB,") != std::string::npos); // the brutal computer (state 5: 'B', RW 0x802620)
	CHECK(record.find(":CM,") != std::string::npos);
	std::string map = keys[2];
	std::replace(map.begin(), map.end(), '/', '\\');
	CHECK(store.profileValue(0, "Map") == map);
	CHECK(store.profileValue(0, "HeroIndexes") == ":-1:-1:-1:-1:-1:-1:-1:-1:");
	// Skirmish again: the kept game, a new seed
	fx.sink.seed = 77;
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	fx.tick(60);
	CHECK(fx.screen->restoredGame() == record);
	CHECK(fx.screen->newProfileMenuCount() == 0); // the profile is there: no add-profile box
	const SkirmishGameInfo &after = fx.screen->setup()->info();
	CHECK(after.mapName == before.mapName);
	CHECK(after.mapCRC == before.mapCRC);
	CHECK(after.rules == before.rules);
	CHECK(after.seed == 77u);
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		INFO("slot " << i);
		CHECK(after.slots[i].state == before.slots[i].state);
		if (before.slots[i].isOccupied())
		{
			CHECK(after.slots[i].name == before.slots[i].name); // a closed slot is named by GameSlot::setState now ("Closed"), not left empty
		}
		CHECK(after.slots[i].playerTemplate == before.slots[i].playerTemplate);
		CHECK(after.slots[i].color == before.slots[i].color);
		CHECK(after.slots[i].teamNumber == before.slots[i].teamNumber);
		CHECK(after.slots[i].startPos == before.slots[i].startPos);
	}
	// the gadgets show it: the map list's selection, slot 1's Player combo
	int row = -1;
	GadgetListBoxGetSelected(fx.screen->mapListWindow(), &row);
	CHECK(row == 2);
	CHECK(itemValueOf(GadgetComboBoxGetItemData(fx.screen->slotGadget(1, "Player"), selectedPos(fx.screen->slotGadget(1, "Player")))) == SLOT_BRUTAL_AI);
}

// lane OWNERFIX-2 (Sol's review of 7fe9e8e1): the kept game holds each slot's handicap at RotWK's + 0x20 (the human's 9th field, a computer's 6th: RW 0x8023C1),
// chosen through the Handicap combos, and the lobby opened again shows it; a kept + 0x20 that retail's parser takes (-100 .. 0) but the combo never offers
// leaves the lobby at its defaults with the note
TEST_CASE("ownerfix2 retail lobby: the handicaps chosen in the combos are kept with the game and shown when the lobby is opened again; an unoffered one is refused")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MemorySkirmishProfiles store;
	LobbyFx fx(mount, false);
	fx.environment.profiles = &store;
	ruleLabels(fx.text);
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	openRulesTab(fx);
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(1, "Player"), 5, false); // a brutal computer
	REQUIRE(fx.screen->setup()->info().slots[1].state == SLOT_BRUTAL_AI);
	GameWindow *h0 = fx.screen->slotGadget(0, "Handicap");
	GameWindow *h1 = fx.screen->slotGadget(1, "Handicap");
	REQUIRE(h0);
	REQUIRE(h1);
	GadgetComboBoxSetSelectedPos(h0, 7, false); // -35%
	GadgetComboBoxSetSelectedPos(h1, 19, false); // -95%
	REQUIRE(fx.screen->setup()->info().slots[0].handicap == -35);
	REQUIRE(fx.screen->setup()->info().slots[1].handicap == -95);
	const SkirmishGameSlot s0 = fx.screen->setup()->info().slots[0];
	const SkirmishGameSlot s1 = fx.screen->setup()->info().slots[1];
	AptButtonInst *back = popupButton(fx, "lobby.MainMenu");
	REQUIRE(back);
	clickButton(fx, *back);
	fx.tick(120);
	REQUIRE(fx.shell->findScreenByFilename("Skirmish.apt") == nullptr);
	const std::string record = store.profileValue(0, "GameInfo");
	// the records as RotWK writes them: H<name>,ip,port,TF,colour,template,start,team,+0x20,+0x40,hero and C<letter>,colour,template,start,team,+0x20,hero
	const std::string human = "HGimli,0,0," + std::string(s0.accepted ? "T" : "F") + (s0.hasMap ? "T" : "F") + "," + std::to_string(s0.color) + "," +
		std::to_string(s0.playerTemplate) + "," + std::to_string(s0.startPos) + "," + std::to_string(s0.teamNumber) + ",-35,0,0:";
	const std::string computer = ":CB," + std::to_string(s1.color) + "," + std::to_string(s1.playerTemplate) + "," + std::to_string(s1.startPos) + "," +
		std::to_string(s1.teamNumber) + ",-95,0:";
	CHECK_MESSAGE(record.find("S=" + human) != std::string::npos, record);
	CHECK_MESSAGE(record.find(computer) != std::string::npos, record);
	// Skirmish again: the kept handicaps, in the setup and in the combos
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	fx.tick(60);
	CHECK(fx.screen->restoredGame() == record);
	CHECK(fx.screen->setup()->info().slots[0].handicap == -35);
	CHECK(fx.screen->setup()->info().slots[1].handicap == -95);
	CHECK(fx.screen->setup()->info().slots[2].handicap == 0);
	REQUIRE(fx.screen->slotGadget(0, "Handicap"));
	REQUIRE(fx.screen->slotGadget(1, "Handicap"));
	CHECK(itemValueOf(GadgetComboBoxGetItemData(fx.screen->slotGadget(0, "Handicap"), selectedPos(fx.screen->slotGadget(0, "Handicap")))) == -35);
	CHECK(itemValueOf(GadgetComboBoxGetItemData(fx.screen->slotGadget(1, "Handicap"), selectedPos(fx.screen->slotGadget(1, "Handicap")))) == -95);
	// a kept + 0x20 of -3 (inside retail's parse range, not a combo value): the lobby starts from the defaults, named
	AptButtonInst *back2 = popupButton(fx, "lobby.MainMenu");
	REQUIRE(back2);
	clickButton(fx, *back2);
	fx.tick(120);
	REQUIRE(fx.shell->findScreenByFilename("Skirmish.apt") == nullptr);
	std::string odd = store.profileValue(0, "GameInfo");
	const std::size_t at = odd.find(",-35,0,0:");
	REQUIRE(at != std::string::npos);
	odd.replace(at, 9, ",-3,0,0:");
	store.setProfileValue(0, "GameInfo", odd);
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	fx.tick(60);
	CHECK(fx.screen->restoredGame().empty());
	CHECK(fx.screen->setup()->info().slots[0].handicap == 0);
	bool noted = false;
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		noted = noted || (n.kind == "skirmish-kept-game" && n.detail.find("slot 0 has the handicap -3, which the lobby does not offer") != std::string::npos);
	}
	CHECK(noted);
}

TEST_CASE("ui6 retail lobby input: the Profile nav opens under the pointer and closes when the pointer leaves it (Skirmish.apt sprite 34: bttn -> _show, "
		  "the surround's rollOver -> _close)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	fx.wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u"Gimli");
	fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	REQUIRE(fx.wm->invokeASAt(1, "ProfilePopup", "ClosePop", {}));
	fx.tick(60);
	AptButtonInst *nav = popupButton(fx, "ProfileNav");
	REQUIRE(nav);
	float x0, y0, x1, y1, x, y;
	REQUIRE(nav->contentBounds(x0, y0, x1, y1));
	nav->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	// the pointer on PROFILE: the nav opens without a click
	fx.wm->postMouseMove(x, y);
	fx.tick(60);
	AptButtonInst *change = popupButton(fx, "ProfileNav.ChangeProfile");
	REQUIRE_MESSAGE(change, "the pointer on PROFILE opens the nav");
	// along its entries, then off it to the middle of the game setup
	float cx0, cy0, cx1, cy1, cx, cy;
	REQUIRE(change->contentBounds(cx0, cy0, cx1, cy1));
	change->globalMatrix().apply((cx0 + cx1) / 2, (cy0 + cy1) / 2, cx, cy);
	fx.wm->postMouseMove(cx, cy);
	fx.tick(10);
	CHECK(popupButton(fx, "ProfileNav.ChangeProfile") != nullptr);
	// the stage's middle is on the nav's alpha 0 surround (depth 1, the whole stage): its rollOver plays _close (RW 0xB0E620 hits it: no alpha test)
	fx.wm->postMouseMove(512.0f, 300.0f);
	fx.tick(90);
	CHECK_MESSAGE(popupButton(fx, "ProfileNav.ChangeProfile") == nullptr, "the nav closes when the pointer leaves it");
	CHECK(popupButton(fx, "ProfileNav") != nullptr); // PROFILE is back, ready to open it again
}

TEST_CASE("ui6 retail lobby: creating, changing and deleting profiles keep each profile's own game; Legendary Heroes is kept in OpenBFME's key (Sol's review)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	MemorySkirmishProfiles store;
	LobbyFx fx(mount, false);
	fx.environment.profiles = &store;
	ruleLabels(fx.text);
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	openRulesTab(fx); // the profile Gimli
	const std::vector<std::string> keys = fx.screen->mapListKeys();
	REQUIRE(keys.size() >= 4);
	// Gimli: Legendary Heroes 4x, the third map (not saved yet)
	GameWindow *legendary = fx.screen->legendaryHeroesWindow();
	REQUIRE(legendary);
	GadgetComboBoxSetSelectedPos(legendary, 3, false);
	REQUIRE(fx.screen->setup()->info().legendaryHeroes == 4);
	GadgetListBoxSetSelected(fx.screen->mapListWindow(), 2);
	REQUIRE(fx.screen->setup()->info().mapName == keys[2]);
	// a new profile: Gimli's choices are kept first, Legolas starts from the defaults
	fx.wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u"Legolas");
	fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	REQUIRE(store.current() == u"Legolas");
	CHECK(fx.screen->setup()->info().legendaryHeroes == LegendaryHeroes::kOff);
	CHECK(fx.screen->setup()->info().slots[0].name == u"Legolas");
	GadgetListBoxSetSelected(fx.screen->mapListWindow(), 3);
	REQUIRE(fx.screen->setup()->info().mapName == keys[3]);
	// back to Gimli: his kept game, Legendary Heroes 4x included (Legolas's is kept on the way)
	showProfilePage(fx, "_change");
	REQUIRE(fx.screen->profileListWindow());
	fx.wm->invokeCallback("AptSkirmish::OnChangeProfileMenu", "");
	int gimliRow = -1;
	for (int r = 0; r < GadgetListBoxGetNumEntries(fx.screen->profileListWindow()); ++r)
	{
		gimliRow = GadgetListBoxGetText(fx.screen->profileListWindow(), r, 0) == u"Gimli" ? r : gimliRow;
	}
	REQUIRE(gimliRow >= 0);
	GadgetListBoxSetSelected(fx.screen->profileListWindow(), gimliRow);
	fx.wm->invokeCallback("AptSkirmish::OnChangeProfile", "");
	REQUIRE(store.current() == u"Gimli");
	CHECK(fx.screen->setup()->info().mapName == keys[2]);
	CHECK(fx.screen->setup()->info().legendaryHeroes == 4);
	CHECK(store.profileValue(0, "OpenBFMELegendaryHeroes") == "4");
	CHECK(store.profileValue(0, "GameInfo").find("OpenBFME") == std::string::npos); // RotWK's record stays retail's
	// deleting the current profile (Gimli): Legolas becomes current with Legolas's own game, and the screen's teardown keeps that, not Gimli's
	showProfilePage(fx, "_delete");
	REQUIRE(fx.screen->profileListWindow());
	fx.wm->invokeCallback("AptSkirmish::OnDeleteProfileMenu", "");
	for (int r = 0; r < GadgetListBoxGetNumEntries(fx.screen->profileListWindow()); ++r)
	{
		if (GadgetListBoxGetText(fx.screen->profileListWindow(), r, 0) == u"Gimli")
		{
			GadgetListBoxSetSelected(fx.screen->profileListWindow(), r);
		}
	}
	fx.wm->invokeCallback("AptSkirmish::OnDeleteProfile", "");
	REQUIRE(store.current() == u"Legolas");
	CHECK(fx.screen->setup()->info().mapName == keys[3]);
	CHECK(fx.screen->setup()->info().legendaryHeroes == LegendaryHeroes::kOff);
	CHECK(fx.screen->setup()->info().slots[0].name == u"Legolas");
	fx.shell->pop();
	fx.tick(30);
	std::string map = keys[3];
	std::replace(map.begin(), map.end(), '/', '\\');
	CHECK(store.profileValue(0, "Map") == map);
	CHECK(store.profileValue(0, "OpenBFMELegendaryHeroes") == "1");
}

TEST_CASE("rotwk-c lobby retail: handicap and faction dropdown limits follow the slot")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	fx.tick(60);
	for (int slot : { 0, 1, 4, 7 })
	{
		for (const char *leaf : { "Handicap", "PlayerTemplate" })
		{
			GameWindow *combo = fx.screen->slotGadget(slot, leaf);
			REQUIRE(combo != nullptr);
			const ComboBoxData *data = static_cast<const ComboBoxData *>(combo->winGetUserData());
			REQUIRE(data != nullptr);
			const int expected[] = { 8, 7, 6, 5, 4, 4, 4, 4 };
			CHECK(data->maxDisplay == expected[slot]);
		}
	}
}

// lane OWNERFIX-3 (the owner's playtest, 2026-10-17: "the map list's hammer icon and star column do nothing"): the first column is the profile's battle
// honors (RW 0x8460B5), the hammer header sorts by it (MpGameSetup::OnSortIcons -> RW 0x84010E with 4); a won skirmish is recorded by the score screen
// (RW 0x9270AD -> 0x9CE8F3) into the profile's RealTimeStats.ini, and the lobby shows the conquest
TEST_CASE("ownerfix3 retail lobby: a won skirmish against a Hard AI marks its map Hard-conquered, the hammer header sorts by the conquest (RW 0x8460B5 / 0x9CE8F3)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme-ownerfix3-lobby-" + std::to_string(std::random_device{}()));
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	struct Cleanup
	{
		std::filesystem::path d;
		~Cleanup() { std::filesystem::remove_all(d); }
	} cleanup{ dir };
	SkirmishPreferences profiles((dir / "Skirmish.ini").string());
	REQUIRE(profiles.add(u"Gimli"));
	REQUIRE(profiles.setCurrent(u"Gimli"));
	SkirmishHonorsGame honors;
	ScoreScreenData score;
	LobbyFx fx(mount, false);
	fx.environment.profiles = &profiles;
	fx.environment.skirmishHonors = &honors;
	fx.environment.scoreScreen = &score;
	fx.shell->push("Skirmish.apt");
	fx.screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(fx.screen);
	fx.tick(60);
	REQUIRE(fx.screen->setup());
	GameWindow *list = fx.screen->mapListWindow();
	REQUIRE(list);
	const std::vector<std::string> keys = fx.screen->mapListKeys();
	REQUIRE(keys.size() >= 3); // tolerance: the retail install lists dozens of maps; the sort checks need three rows
	auto entryOf = [&](const std::string &key) -> const MapCacheEntry * {
		for (const MapCacheEntry &m : fx.setup.maps())
		{
			if (m.name == key)
			{
				return &m;
			}
		}
		return nullptr;
	};
	// a new profile: every listed multiplayer map is not conquered (state 1, + 0x8000 for a user map), the not-conquered images
	for (std::size_t i = 0; i < keys.size(); ++i)
	{
		const MapCacheEntry *m = entryOf(keys[i]);
		REQUIRE(m);
		CAPTURE(keys[i]);
		CHECK(fx.screen->mapListStates()[i] == ((m->isMultiplayer ? 1 : 0) | (m->isOfficial ? 0 : 0x8000)));
		CHECK(fx.screen->mapListImages()[i] == (m->isOfficial ? "AptDifficultyNotConquered" : "AptUserMapNotConquered"));
	}
	// pick the second row's map and a Hard AI in slot 1, then press the movie's StartGame
	const std::string won = keys[1];
	const MapCacheEntry *wonMap = entryOf(won);
	REQUIRE(wonMap);
	REQUIRE(wonMap->isMultiplayer);
	clickListRow(fx, list, 1);
	REQUIRE(fx.screen->setup()->info().mapName == won);
	GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(1, "Player"), 4, false); // Open, Closed, Easy, Medium, Hard
	REQUIRE(fx.screen->setup()->info().slots[1].state == SLOT_HARD_AI);
	AptButtonInst *start = firstButtonIn(fx.wm->apt().resolvePath(fx.wm->apt().level(1), "lobby.StartGame"));
	REQUIRE(start);
	clickButton(fx, *start);
	fx.tick(30);
	REQUIRE(fx.sink.messages.size() == 1);
	// the game the score screen records (retail: TheGameInfo)
	REQUIRE(honors.valid);
	CHECK(honors.localSlot == 0);
	CHECK(honors.slots[0].state == SLOT_PLAYER);
	CHECK(honors.slots[1].state == SLOT_HARD_AI);
	CHECK(honors.mapKey == won);
	CHECK(honors.mapPlayers == wonMap->numPlayers);

	// the game ends in the local player's victory: the score screen (type 2) records the honors once
	score.valid = true;
	score.type = 2;
	score.localAlliedVictory = true;
	ScoreScreenData::Entry me;
	me.playerIndex = 1;
	me.name = "Gimli";
	me.side = "Dwarves";
	me.local = true;
	me.result = 0;
	score.entries.push_back(me);
	fx.shell->push("TimeLine.apt");
	fx.tick(90);
	const std::string statsPath = (dir / "GimliRealTimeStats.ini").string();
	REQUIRE(std::filesystem::exists(statsPath));
	std::ifstream in(statsPath, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	std::string retailKey = won;
	std::replace(retailKey.begin(), retailKey.end(), '/', '\\');
	CHECK(text.str().find(retailKey + "_4 = 1") != std::string::npos);
	CHECK(text.str().find("UseMapListTooltips = 1") != std::string::npos);
	CHECK(score.honorsRecorded);
	CHECK_FALSE(honors.valid); // once
	CHECK(fx.wm->noteCount("map-honors") == 1); // the record's note (the lobby's fills had the profile store and its folder: no note)

	// back in the lobby: the hammer header (OnSortIcons, the state ascending) puts the conquered map last with the Hard image; again: first
	fx.shell->pop();
	fx.tick(30);
	REQUIRE(fx.shell->top() == fx.screen);
	fx.wm->invokeCallback("MpGameSetup::OnSortIcons", "");
	REQUIRE(fx.screen->mapListKeys().size() == keys.size());
	CHECK(fx.screen->mapListKeys().back() == won);
	CHECK(fx.screen->mapListStates().back() == 4);
	CHECK(fx.screen->mapListImages().back() == "AptDifficultyHardConquered");
	fx.wm->invokeCallback("MpGameSetup::OnSortIcons", "");
	CHECK(fx.screen->mapListKeys().front() == won);
	CHECK(fx.screen->mapListImages().front() == "AptDifficultyHardConquered");
	// a defeat records nothing: the file stays as it was
	const std::string before = text.str();
	score.honorsRecorded = false;
	score.localAlliedVictory = false;
	honors.valid = true;
	fx.shell->push("TimeLine.apt");
	fx.tick(60);
	std::ifstream again(statsPath, std::ios::binary);
	std::stringstream after;
	after << again.rdbuf();
	CHECK(after.str() == before);
	CHECK(score.honorsRecorded);
}

TEST_CASE("skirmish controller: shoulder tabs and explicit dropdown confirmation")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	ruleLabels(fx.text);
	openRulesTab(fx);
	MenuNavigator nav(*fx.wm);
	nav.setMenu(fx.screen->level(), fx.screen);
	REQUIRE(nav.key(MenuNavKey::PagePrevious));
	fx.tick(30);
	bool maps = false;
	for (const auto &target : nav.targets()) maps |= target.id.find(".TabButtons.RulesTab.") != std::string::npos;
	CHECK(maps);
	REQUIRE(nav.key(MenuNavKey::PageNext));
	fx.tick(30);
	REQUIRE(fx.screen->rulesPopulated());
	GameWindow *combo = fx.screen->legendaryHeroesWindow();
	REQUIRE(combo);
	std::string id;
	for (const auto &target : nav.targets()) if (target.window == combo) id = target.id;
	REQUIRE_FALSE(id.empty());
	for (int i = 0; i < 100 && nav.focusId() != id; ++i) REQUIRE(nav.key(MenuNavKey::Next));
	REQUIRE(nav.focusId() == id);
	int before = -1;
	GadgetComboBoxGetSelectedPos(combo, &before);
	REQUIRE(nav.key(MenuNavKey::Right));
	int after = -1;
	GadgetComboBoxGetSelectedPos(combo, &after);
	CHECK(after == before); // closed dropdown navigation must not change the rule
	for (int i = 0; i < 100 && nav.focusId() != id; ++i) REQUIRE(nav.key(MenuNavKey::Next));
	REQUIRE(nav.focusId() == id);
	REQUIRE(nav.key(MenuNavKey::Activate));
	REQUIRE(fx.wm->gadgetLayer()->openDropDown() == combo);
	REQUIRE(nav.key(MenuNavKey::Down));
	GadgetComboBoxGetSelectedPos(combo, &after);
	CHECK(after == before);
	REQUIRE(nav.key(MenuNavKey::Activate));
	CHECK(fx.wm->gadgetLayer()->openDropDown() == nullptr);
	GadgetComboBoxGetSelectedPos(combo, &after);
	CHECK(after == before + 1);
	REQUIRE(nav.key(MenuNavKey::Activate));
	REQUIRE(nav.key(MenuNavKey::Up));
	REQUIRE(nav.key(MenuNavKey::Back));
	GadgetComboBoxGetSelectedPos(combo, &before);
	CHECK(before == after); // B cancels the preview
	GameWindow *color = nullptr;
	for (const auto &target : nav.targets())
		if (target.window)
		{
			using Input = WindowMsgHandledType (*)(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData);
			const auto input = target.window->winGetInputFunc();
			const auto *fn = input.target<Input>();
			if (fn && *fn == GadgetImageComboBoxInput) { color = target.window; id = target.id; break; }
		}
	REQUIRE(color);
	for (int i = 0; i < 100 && nav.focusId() != id; ++i) REQUIRE(nav.key(MenuNavKey::Next));
	REQUIRE(nav.focusId() == id);
	// Crossing the player grid must preserve the row, including at its edges.
	for (MenuNavKey direction : { MenuNavKey::Left, MenuNavKey::Right })
	{
		for (int i = 0; i < 100 && nav.focusId() != id; ++i) REQUIRE(nav.key(MenuNavKey::Next));
		REQUIRE(nav.focusId() == id);
		for (int step = 0; step < 8; ++step)
		{
			float previous[4], current[4];
			REQUIRE(nav.focusBox(previous));
			REQUIRE(nav.key(direction));
			REQUIRE(nav.focusBox(current));
			const float delta = std::fabs((previous[1] + previous[3] - current[1] - current[3]) * 0.5f);
			// tolerance: same-row controls have different heights/centres; half the smaller height admits them but excludes the adjacent player row.
			CHECK(delta <= 0.5f * std::min(previous[3] - previous[1], current[3] - current[1]));
		}
	}
	for (int i = 0; i < 100 && nav.focusId() != id; ++i) REQUIRE(nav.key(MenuNavKey::Next));
	REQUIRE(nav.focusId() == id);
	GadgetImageComboBoxGetSelectedPos(color, &before);
	REQUIRE(nav.key(MenuNavKey::Activate));
	REQUIRE(fx.wm->gadgetLayer()->openDropDown() == color);
	REQUIRE(nav.key(MenuNavKey::Down));
	GadgetImageComboBoxGetSelectedPos(color, &after);
	CHECK(after == before);
	REQUIRE(nav.key(MenuNavKey::Back));
	CHECK(fx.wm->gadgetLayer()->openDropDown() == nullptr);
}

TEST_CASE("skirmish controller: triggers confine navigation to three sections; keyboard is unrestricted")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	ruleLabels(fx.text);
	openRulesTab(fx);
	MenuNavigator nav(*fx.wm);
	nav.setMenu(fx.screen->level(), fx.screen);
	const auto all = nav.targets();
	nav.setControllerSections(true);
	for (int section : { 2, 0, 1 })
	{
		nav.switchControllerSection(1);
		CHECK(nav.controllerSection() == section);
		const auto targets = nav.targets();
		REQUIRE_FALSE(targets.empty());
		for (const auto &t : targets)
		{
			if (section == 1) CHECK(t.id.find(".PlayerList.") != std::string::npos);
			if (section == 2) CHECK(t.id.find(".MpMapAndSettings.") != std::string::npos);
		}
		if (section == 0)
		{
			bool start = false, back = false, profile = false;
			for (const auto &t : targets)
			{
				start |= t.id.find(".StartGame.") != std::string::npos;
				back |= t.id.find(".MainMenu.") != std::string::npos;
				profile |= t.id.find(".ProfileNav.") != std::string::npos;
			}
			CHECK(start); CHECK(back); CHECK(profile);
		}
		for (MenuNavKey key : { MenuNavKey::Up, MenuNavKey::Down, MenuNavKey::Left, MenuNavKey::Right })
			for (int i = 0; i < 10; ++i)
			{
				nav.key(key);
				bool inside = false;
				for (const auto &t : targets) inside |= t.id == nav.focusId();
				CHECK(inside);
			}
		GadgetDrawList draw;
		nav.appendFocusFrame(draw);
		int accents = 0;
		for (const auto &command : draw.commands)
			if (command.window == "ControllerSection")
			{
				CHECK(command.kind == GadgetDrawCommand::Kind::Line);
				CHECK(command.lineWidth == 1);
				++accents;
			}
		CHECK(accents == 8);
	}
	nav.setControllerSections(false);
	CHECK(nav.controllerSection() == -1);
	CHECK(nav.targets().size() == all.size());
}

TEST_CASE("skirmish controller: bottom bar reaches Profile and player corners stop above toolbar")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount);
	ruleLabels(fx.text);
	openRulesTab(fx);
	MenuNavigator nav(*fx.wm);
	nav.setMenu(fx.screen->level(), fx.screen);
	const auto all = nav.targets();
	float toolbarTop = 768;
	for (const auto &t : all) if (t.id.find(".lobby.") != std::string::npos) toolbarTop = std::min(toolbarTop, t.y0);
	nav.setControllerSections(true);
	nav.switchControllerSection(-1);
	for (int i = 0; i < 10 && nav.focusId().find(".MainMenu.") == std::string::npos; ++i) nav.key(MenuNavKey::Left);
	REQUIRE(nav.focusId().find(".MainMenu.") != std::string::npos);
	nav.key(MenuNavKey::Right);
	CHECK(nav.focusId().find(".ProfileNav.") != std::string::npos);
	fx.tick(60);
	bool change = false;
	for (const auto &t : nav.targets()) change |= t.id.find(".ProfileNav.ChangeProfile.") != std::string::npos;
	CHECK(change);
	// PROFILE still uses a full-stage click-away button on this non-main screen.
	REQUIRE_FALSE(nav.catchers().empty());
	REQUIRE(nav.key(MenuNavKey::Back));
	fx.tick(90);
	for (const auto &target : nav.targets())
		CHECK(target.id.find(".ProfileNav.ChangeProfile.") == std::string::npos);
	// Reopen through the same authored rollover before checking entry activation.
	for (const auto &target : all)
		if (target.id.find(".ProfileNav.bttn") != std::string::npos)
			fx.wm->postMouseMove(target.cx(), target.cy());
	fx.tick(60);
	REQUIRE_FALSE(nav.catchers().empty());
	REQUIRE(nav.key(MenuNavKey::Activate));
	fx.tick(10);
	CHECK(nav.focusId().find(".ProfileNav.") != std::string::npos);
	bool selected = false;
	for (int i = 0; i < 6 && !selected; ++i)
	{
		selected = nav.focusId().find(".ChangeProfile.") != std::string::npos;
		if (!selected) { nav.key(MenuNavKey::Up); fx.tick(10); }
	}
	REQUIRE(selected);
	REQUIRE(nav.key(MenuNavKey::Activate));
	fx.tick(60);
	CHECK(popupButton(fx, "ProfilePopup.Main.Cancel") != nullptr);
	REQUIRE(fx.wm->invokeASAt(1, "ProfilePopup", "ClosePop", {}));
	fx.tick(60);
	fx.wm->postMouseMove(512, 300);
	fx.tick(90);
	nav.switchControllerSection(1);
	fx.tick(90);
	GadgetDrawList draw;
	nav.appendFocusFrame(draw);
	int corners = 0;
	for (const auto &command : draw.commands) if (command.window == "ControllerSection")
	{
		// tolerance: the section accents must stay above the authored toolbar boundary.
		CHECK(command.y0 < toolbarTop);
		// tolerance: both endpoints of each corner stroke must stay above the toolbar.
		CHECK(command.y1 < toolbarTop);
		++corners;
	}
	CHECK(corners == 8);
}

TEST_CASE("results controller: A skips preview, More and Continue default, B returns")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	LobbyFx fx(mount, false);
	ScoreScreenData score;
	score.valid = true;
	score.type = 2;
	fx.environment.scoreScreen = &score;
	fx.shell->push("TimeLine.apt");
	fx.tick(30);
	MenuNavigator nav(*fx.wm);
	nav.setMenu(fx.shell->top()->level(), fx.shell->top());
	nav.setControllerResults(true);
	CHECK(nav.focusId().find(".MainButtons.Skip") != std::string::npos);
	REQUIRE(nav.key(MenuNavKey::Previous));
	const std::string previewFocus = nav.focusId();
	nav.setControllerResults(true);
	CHECK(nav.focusId() == previewFocus);
	REQUIRE(nav.key(MenuNavKey::Activate));
	fx.tick(60);
	nav.setControllerResults(true);
	CHECK(nav.focusId().find(".MainButtons.More") != std::string::npos);
	REQUIRE(nav.key(MenuNavKey::Activate));
	fx.tick(60);
	nav.setControllerResults(true);
	CHECK(nav.focusId().find(".MainButtons.Continue") != std::string::npos);
	REQUIRE(nav.key(MenuNavKey::Previous));
	REQUIRE(nav.key(MenuNavKey::Back));
	fx.tick(60);
	nav.setControllerResults(true);
	CHECK(nav.focusId().find(".MainButtons.More") != std::string::npos);
}

namespace
{
struct Bfme1ProfileFixture
{
	std::filesystem::path folder = std::filesystem::temp_directory_path() / ("openbfme-bfme1-profile-" + std::to_string(std::random_device{}()));
	MemorySource source;
	RecordingShellServices services;
	MapGameText text;
	FakeSetup setup;
	RecordingNewGameSink games;
	Win32BIGFileSystem fs;
	std::unique_ptr<SkirmishPreferences> profiles;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	WindowManager windows;
	AptGadgetLayer layer;
	Shell shell;
	AptSkirmish *screen = nullptr;

	explicit Bfme1ProfileFixture(bool bfme1 = true, bool existing = true, const std::string &templates = {})
		: windows(source, services), layer(windows, source, skins), shell(windows, factories, services, environment)
	{
		std::filesystem::create_directories(folder);
		profiles = std::make_unique<SkirmishPreferences>((folder / "Skirmish.ini").string());
		if (existing)
		{
			REQUIRE(profiles->add(u"Alice"));
			REQUIRE(profiles->setCurrent(u"Alice"));
			REQUIRE(profiles->add(u"Bob"));
		}
		std::ofstream(folder / "AliceSkirmishStats.ini") << "ProfileCreatedDate = 2025-04-17\nGondorWins = 7\nGondorLosses = 3\nGondorWinStreak = 2\nGondorBestWinStreak = 4\nGondorTimePlayed = 97200\nGondorPoints = 9\nRohanWins = 2\nRohanPoints = 1500\n"
			"maps\\fords\\fords.map_5 = 1\nmaps\\hard\\hard.map_4 = 1\nmaps\\medium\\medium.map_3 = 1\nmaps\\easy\\easy.map_2 = 1\n";
		fs.setDerivedFile("Data/INI/Default/SubsystemLegend.ini", {}, "test", {});
		if (!bfme1) fs.setDerivedFile("Data/INI/AwardSystem.ini", {}, "test", {});
		setup.fac = { FakeSetup::faction("FactionGondor", "Gondor", true, "Castle"), FakeSetup::faction("FactionRohan", "Rohan", true, "Castle") };
		if (!templates.empty())
		{
			std::string error;
			REQUIRE_MESSAGE(IniSkirmishSetupSource::scanPlayerTemplates(templates, setup.fac, &error), error);
		}
		for (const char *name : { "AptRankIcon0", "AptRankIcon1", "AptRankIcon2", "AptRankIcon10", "AptRankIconGondor2", "AptRankIconRohan10" })
		{
			Image image;
			image.name = name;
			image.imageSize = { 8, 8 };
			skins.images.addImage(image);
		}
		setup.map = { FakeSetup::mapEntry("maps/fords/fords.map", "Fords of Isen", 2, true, 1),
			FakeSetup::mapEntry("maps/hard/hard.map", "Hard map", 2, true, 2),
			FakeSetup::mapEntry("maps/medium/medium.map", "Medium map", 2, true, 3),
			FakeSetup::mapEntry("maps/easy/easy.map", "Easy map", 2, true, 4),
			FakeSetup::mapEntry("maps/custom/custom.map", "Custom map", 2, true, 5) };
		for (auto &map : setup.map) map.isOfficial = map.name != "maps/custom/custom.map";
		lobbyLabels(text);
		for (const auto &label : std::map<std::string, std::string>{ { "APT:DashDash", "--" }, { "APT:NewProfile", "New profile" },
			{ "APT:DeleteProfile", "Delete profile" }, { "APT:GondorCaps", "GONDOR" }, { "APT:RohanCaps", "ROHAN" },
			{ "APT:CurrentLevelFormat", "Level %s" }, { "APT:CurrentLevelNumFormat", "Level %d" }, { "APT:WinsNumFormat", "%d wins" },
			{ "APT:WinsNumFormatForOnePoint", "%d win" }, { "Apt:TimePlayed", "%d days %d hours" },
			{ "Apt:OfficialMaps", "Official maps" }, { "Apt:PlayerMadeMaps", "Player-made maps" } }) text.set(label.first, label.second);
		environment.gameText = &text;
		environment.skirmish = &setup;
		environment.profiles = profiles.get();
		environment.newGame = &games;
		environment.fileSystem = &fs;
		TestMovie level0;
		level0.setRootFrames({ {} });
		source.add("AptLevel0", level0);
		TestMovie movie;
		const auto shape = movie.addCharacter(movie.addShape(0, 0, 100, 24, 0));
		const auto sprite = movie.addCharacter(movie.addSprite({ { movie.addPlaceItem(placeChar(shape, 1, "bounds")) } }));
		movie.addExport("ProfileCombo", sprite);
		movie.addExport("ProfileList", sprite);
		movie.addExport("ProfileEntry", sprite);
		std::vector<std::uint32_t> frame;
		int depth = 0;
		for (const auto &widget : std::map<std::string, std::string>{ { "Skirmish::PlayerProfile", "AptSkirmish::InitGadgets" },
			{ "Skirmish::CreatePersonaEntry", "AptSkirmish::InitGadgets" }, { "MapList", "MpGameSetup::GadgetInit" },
			{ "MpGameSetup::MapType", "MpGameSetup::GadgetInit" }, { "0/PlayerTemplate", "MpGameSetup::GadgetInit" } })
		{
			++depth;
			auto placement = placeChar(sprite, depth, "combo" + std::to_string(depth));
			placement.flags |= APT_PLACE_HASCLIPACTION;
			placement.events.push_back({ APT_CLIP_CONSTRUCT, 0, program(movie, [&](Asm &code) {
				code.pushString("_type").setStringVar(widget.first == "MapList" ? "ProfileList" : widget.first == "Skirmish::CreatePersonaEntry" ? "ProfileEntry" : "ProfileCombo");
				code.pushString("_Init").setStringVar(widget.second);
				code.pushString("_WindowId").setStringVar(widget.first);
			}) });
			frame.push_back(movie.addPlaceItem(placement));
		}
		frame.insert(frame.begin(), movie.addActionItem(program(movie, [](Asm &code) {
			code.defineFunction("ShowSkirmishMovie", { "value" }, [](Asm &body) {
				body.getStringVar("_root").pushString("profileMovie").getStringVar("value").op(APT_OP_SETMEMBER);
			});
			code.defineFunction("SetProgressBar", { "value" }, [](Asm &body) {
				body.getStringVar("_root").pushString("profileProgress").getStringVar("value").op(APT_OP_SETMEMBER);
			});
		})));
		frame.push_back(movie.addActionItem(program(movie, [](Asm &code) { fscmd(code, "AptSkirmish::OnInitialized"); code.op(APT_OP_STOP); })));
		movie.setRootFrames({ frame });
		source.add("Skirmish", movie);
		factories.registerFactory("Skirmish.apt", [](AptScreenContext &context) { return std::make_unique<AptSkirmish>(context.windows, context.shell, context.environment); });
		for (const char *symbol : { "ProfileCombo", "ProfileList", "ProfileEntry" })
		windows.registerComponent(symbol, [this](AptComponentRequest &request) -> std::shared_ptr<GameWindow> {
			WinInstanceData instance;
			GameWindow *window = nullptr;
			if (request.type == "ProfileList")
			{
				instance.m_style = GWS_SCROLL_LISTBOX;
				ListboxData data;
				data.columns = 5;
				data.columnWidthPercentage = { 8, 2, 70, 10, 10 };
				data.listLength = 16;
				window = layer.gadgets().gogoGadgetListBox(nullptr, WIN_STATUS_ENABLED, 0, 0, 300, 120, &instance, &data, nullptr, false);
			}
			else if (request.type == "ProfileEntry")
			{
				instance.m_style = GWS_ENTRY_FIELD;
				EntryData data;
				data.maxTextLen = 110;
				window = layer.gadgets().gogoGadgetTextEntry(nullptr, WIN_STATUS_ENABLED, 0, 0, 150, 24, &instance, &data, nullptr, false);
			}
			else
			{
				instance.m_style = GWS_COMBO_BOX;
				ComboBoxData data;
				data.listboxTemplate.columns = 1;
				data.listboxTemplate.listLength = 16;
				data.maxDisplay = 8;
				window = layer.gadgets().gogoGadgetComboBox(nullptr, WIN_STATUS_ENABLED, 0, 0, 100, 24, &instance, &data, nullptr, false);
			}
			if (!window) return {};
			window->setAptInstanceName(request.instanceName);
			return { window, [this](GameWindow *gadget) { layer.gadgets().winDestroy(gadget); layer.gadgets().processDestroyList(); } };
		});
		windows.init();
		shell.push("Skirmish.apt");
		screen = dynamic_cast<AptSkirmish *>(shell.top());
		REQUIRE(screen);
		for (int tick = 0; tick < 3; ++tick) windows.update(33);
		REQUIRE(screen->setup());
	}
	~Bfme1ProfileFixture()
	{
		shell.clear();
		std::error_code error;
		std::filesystem::remove_all(folder, error);
	}
	std::string value(const std::string &name)
	{
		const auto *result = windows.aptText(name);
		return result ? *result : "<unset>";
	}
	std::string movieValue(const std::string &name)
	{
		AptValue value;
		return windows.apt().level(screen->level())->getMember(name, value) ? value.toString() : "<unset>";
	}
	void choose(GameWindow *combo, int row)
	{
		REQUIRE(combo);
		GadgetComboBoxSetSelectedPos(combo, row, true);
		layer.gadgets().winSendSystemMsg(screen->ownerWindow(), GCM_SELECTED, msgData(combo), 0);
	}
};
}

TEST_CASE("bfme1 skirmish profile: real honors, faction artwork and rank follow the selected faction")
{
	Bfme1ProfileFixture fixture;
	CHECK(fixture.value("APT:PlayerNameNum") == "Alice");
	CHECK(fixture.value("APT:SkirmishProfileCreatedNum") == "2025-04-17");
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "--");
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "7");
	CHECK(fixture.value("APT:TotalCareerLossesNum") == "3");
	CHECK(fixture.value("APT:TotalCareerGamesNum") == "10");
	CHECK(fixture.value("APT:CurrentWinStreakNumA") == "2");
	CHECK(fixture.value("APT:BestWinStreakNumA") == "4");
	CHECK(fixture.value("APT:TotalTimePlayedNum") == "1 days 3 hours");
	CHECK(fixture.value("APT:CurFactionNameStr") == "GONDOR");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 2");
	CHECK(fixture.value("APT:NextWinsNumber") == "1 win");
	CHECK(fixture.movieValue("profileMovie") == "0");
	CHECK(fixture.movieValue("profileProgress") == "80");
	REQUIRE(fixture.windows.aptImage("CreateComponents/ImageLevelIconMain"));
	CHECK(*fixture.windows.aptImage("CreateComponents/ImageLevelIconMain") == "AptRankIconGondor2");
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 2);
	CHECK(fixture.value("APT:CurFactionNameStr") == "ROHAN");
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "2");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 10");
	CHECK(fixture.value("APT:NextWinsNumber") == "0 wins");
	CHECK(fixture.movieValue("profileMovie") == "1");
	CHECK(fixture.movieValue("profileProgress") == "100");
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 0);
	CHECK(fixture.value("APT:CurFactionNameStr") == "--");
	CHECK(fixture.movieValue("profileMovie") == "0");
	CHECK(fixture.movieValue("profileProgress") == "0");
	CHECK(*fixture.windows.aptImage("CreateComponents/ImageLevelIconMain") == "AptRankIcon0");
}

TEST_CASE("bfme1 skirmish profile: loaded faction indices, renamed sides and mounted rank images drive presentation")
{
	const std::string templates =
		"PlayerTemplate FactionHidden\nSide = Hidden\nPlayableSide = No\nEnd\n"
		"PlayerTemplate FactionObserver\nSide = Observer\nPlayableSide = Yes\nIsObserver = Yes\nEnd\n"
		"PlayerTemplate FactionRohan\nSide = Rohan\nPlayableSide = Yes\nDisplayName = INI:FactionRohan\nStartingBuilding = Castle\nEnd\n"
		"PlayerTemplate FactionNorthRealm\nSide = Haven\nPlayableSide = Yes\nDisplayName = INI:FactionNorthRealm\nStartingBuilding = Castle\nEnd\n"
		"PlayerTemplate FactionGondor\nSide = Gondor\nPlayableSide = Yes\nDisplayName = INI:FactionGondor\nStartingBuilding = Castle\nEnd\n";
	Bfme1ProfileFixture fixture(true, true, templates);
	fixture.text.set("INI:FactionNorthRealm", "Northern Haven");
	fixture.text.set("APT:HavenCaps", "NORTHERN HAVEN");
	std::ofstream(fixture.folder / "AliceSkirmishStats.ini", std::ios::app) << "HavenWins = 3\nHavenLosses = 1\nHavenPoints = 7\n";
	std::string expectedIcon = "AptRankIcon2";
	SUBCASE("a faction-specific mapped rank image takes precedence")
	{
		Image image;
		image.name = "AptRankIconHaven2";
		image.imageSize = { 8, 8 };
		fixture.skins.images.addImage(image);
		expectedIcon = image.name;
	}
	SUBCASE("the native generic rank image is used when faction artwork is absent") {}
	GameWindow *combo = fixture.screen->slotGadget(0, "PlayerTemplate");
	REQUIRE(combo);
	CHECK(GadgetComboBoxGetLength(combo) == 4);
	fixture.choose(combo, 1);
	CHECK(fixture.screen->setup()->info().slots[0].playerTemplate == 2);
	CHECK(fixture.value("APT:CurFactionNameStr") == "ROHAN");
	CHECK(fixture.movieValue("profileMovie") == "2");
	fixture.choose(combo, 2);
	CHECK(fixture.screen->setup()->info().slots[0].playerTemplate == 3);
	CHECK(fixture.value("APT:CurFactionNameStr") == "NORTHERN HAVEN");
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "3");
	CHECK(fixture.value("APT:TotalCareerGamesNum") == "4");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 2");
	CHECK(fixture.value("APT:NextWinsNumber") == "3 wins");
	CHECK(fixture.movieValue("profileMovie") == "3");
	CHECK(fixture.movieValue("profileProgress") == "40");
	REQUIRE(fixture.windows.aptImage("CreateComponents/ImageLevelIconMain"));
	CHECK(*fixture.windows.aptImage("CreateComponents/ImageLevelIconMain") == expectedIcon);
	fixture.choose(combo, 3);
	CHECK(fixture.screen->setup()->info().slots[0].playerTemplate == 4);
	CHECK(fixture.value("APT:CurFactionNameStr") == "GONDOR");
	CHECK(fixture.movieValue("profileMovie") == "4");
	fixture.choose(combo, 0);
	CHECK(fixture.value("APT:CurFactionNameStr") == "--");
	CHECK(fixture.movieValue("profileMovie") == "0");
}

TEST_CASE("bfme1 skirmish profile: profile and map type combos replace retail placeholder entries")
{
	Bfme1ProfileFixture fixture;
	GameWindow *profiles = fixture.screen->profileComboWindow();
	REQUIRE(profiles);
	CHECK(GadgetComboBoxGetLength(profiles) == 4);
	CHECK(GadgetComboBoxGetText(profiles) == u"Alice");
	GameWindow *mapType = fixture.screen->mapTypeWindow();
	REQUIRE(mapType);
	CHECK(GadgetComboBoxGetLength(mapType) == 2);
	CHECK(GadgetComboBoxGetText(mapType) == u"Official maps");
	fixture.choose(mapType, 1);
	CHECK_FALSE(fixture.screen->bfme1OfficialMaps());
	CHECK(GadgetComboBoxGetText(mapType) == u"Player-made maps");
	fixture.choose(profiles, 1);
	CHECK(fixture.profiles->current() == u"Bob");
	CHECK(fixture.value("APT:PlayerNameNum") == "Bob");
	CHECK(fixture.value("APT:SkirmishProfileCreatedNum") == "--");
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "0");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 1");
	CHECK_FALSE(std::filesystem::exists(fixture.folder / "BobSkirmishStats.ini"));
}

TEST_CASE("bfme1 skirmish profile: absent profiles show dashes without inventing a creation date")
{
	Bfme1ProfileFixture fixture(true, false);
	CHECK(fixture.value("APT:PlayerNameNum") == "--");
	CHECK(fixture.value("APT:SkirmishProfileCreatedNum") == "--");
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "--");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level --");
}

TEST_CASE("bfme1 skirmish profile: BFME2 data does not receive BFME1 profile records or dropdowns")
{
	Bfme1ProfileFixture fixture(false);
	CHECK(fixture.value("APT:PlayerNameNum") == "<unset>");
	CHECK(fixture.screen->profileComboWindow() == nullptr);
	CHECK(fixture.screen->mapTypeWindow() == nullptr);
	CHECK(fixture.windows.aptImage("CreateComponents/ImageLevelIconMain") == nullptr);
	const auto images = fixture.screen->mapListImages();
	fixture.screen->recordsChanged();
	CHECK(fixture.screen->mapListImages() == images);
	CHECK(fixture.value("APT:PlayerNameNum") == "<unset>");
	REQUIRE(fixture.screen->profileEntryWindow());
	GadgetTextEntrySetText(fixture.screen->profileEntryWindow(), u"BFME2New");
	REQUIRE(fixture.windows.invokeCallback("AptSkirmish::OnAddProfileAccept", ""));
	CHECK_FALSE(std::filesystem::exists(fixture.folder / "BFME2NewSkirmishStats.ini"));
	CHECK(fixture.profiles->profileValue(0, "OpenBFMEProfileCreatedDate").empty());
}

TEST_CASE("bfme1 skirmish profile: new personas persist their real creation date without backfilling old profiles")
{
	Bfme1ProfileFixture fixture;
	fixture.choose(fixture.screen->profileComboWindow(), 2);
	REQUIRE(fixture.screen->profileEntryWindow());
	GadgetTextEntrySetText(fixture.screen->profileEntryWindow(), u"Charlie");
	const std::time_t before = std::time(nullptr);
	REQUIRE(fixture.windows.invokeCallback("AptSkirmish::OnAddProfileAccept", ""));
	const std::time_t after = std::time(nullptr);
	REQUIRE(fixture.profiles->current() == u"Charlie");
	auto date = [](std::time_t value) {
		char result[32] = {};
		const std::tm *local = std::localtime(&value);
		if (local) std::strftime(result, sizeof(result), "%Y-%m-%d", local);
		return std::string(result);
	};
	const std::string displayed = fixture.value("APT:SkirmishProfileCreatedNum");
	CHECK((displayed == date(before) || displayed == date(after)));
	OptionPreferences saved;
	std::string error;
	REQUIRE_MESSAGE(saved.load((fixture.folder / "CharlieSkirmishStats.ini").string(), &error), error);
	CHECK(saved.get("ProfileCreatedDate") == displayed);
	SkirmishPreferences reopened((fixture.folder / "Skirmish.ini").string());
	CHECK(reopened.profileValue(0, "OpenBFMEProfileCreatedDate") == displayed);
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	CHECK(fixture.value("APT:SkirmishProfileCreatedNum") == "--");
	CHECK_FALSE(std::filesystem::exists(fixture.folder / "BobSkirmishStats.ini"));
}

TEST_CASE("bfme1 skirmish profile: recreating a deleted persona does not inherit its statistics")
{
	Bfme1ProfileFixture fixture;
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "7");
	std::ofstream(fixture.folder / "AliceSkirmishStats.ini", std::ios::app) << "OpenBFMERecordedGame:deleted-game = 1\n";
	fixture.choose(fixture.screen->profileComboWindow(), 3);
	REQUIRE(fixture.windows.invokeCallback("AptSkirmish::Skirmish::PersonaRemove", ""));
	REQUIRE(fixture.profiles->current() == u"Bob");
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	REQUIRE(fixture.screen->profileEntryWindow());
	GadgetTextEntrySetText(fixture.screen->profileEntryWindow(), u"Alice");
	REQUIRE(fixture.windows.invokeCallback("AptSkirmish::OnAddProfileAccept", ""));
	REQUIRE(fixture.profiles->current() == u"Alice");
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	for (const char *label : { "APT:TotalCareerWinsNum", "APT:TotalCareerLossesNum", "APT:TotalCareerGamesNum", "APT:CurrentWinStreakNumA", "APT:BestWinStreakNumA" })
		CHECK(fixture.value(label) == "0");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 1");
	CHECK(fixture.value("APT:TotalTimePlayedNum") == "0 days 0 hours");
	CHECK(fixture.movieValue("profileProgress") == "0");
	CHECK(fixture.screen->mapListImages() == std::vector<std::string>(4, "AptDifficultyNotConquered"));
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 2);
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "0");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 1");
	OptionPreferences saved;
	std::string error;
	REQUIRE_MESSAGE(saved.load((fixture.folder / "AliceSkirmishStats.ini").string(), &error), error);
	CHECK(saved.values().size() == 1);
	CHECK(saved.get("ProfileCreatedDate") == fixture.value("APT:SkirmishProfileCreatedNum"));
	CHECK(saved.get("ProfileCreatedDate") != "2025-04-17");
	CHECK_FALSE(saved.has("OpenBFMERecordedGame:deleted-game"));
	SkirmishPreferences reopened((fixture.folder / "Skirmish.ini").string());
	CHECK(reopened.current() == u"Alice");
	CHECK(reopened.profileValue(0, "OpenBFMEProfileCreatedDate") == saved.get("ProfileCreatedDate"));
}

TEST_CASE("bfme1 skirmish profile: switching personas refreshes map badges and preserves map selection and filter")
{
	Bfme1ProfileFixture fixture;
	const auto keys = fixture.screen->mapListKeys();
	const auto images = fixture.screen->mapListImages();
	REQUIRE(keys.size() == 4);
	auto selectMap = [&](int row) {
		fixture.layer.gadgets().winSendSystemMsg(fixture.screen->ownerWindow(), GLM_SELECTED, msgData(fixture.screen->mapListWindow()), (WindowMsgData)row);
	};
	selectMap(0);
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	selectMap(1);
	fixture.choose(fixture.screen->profileComboWindow(), 0);
	auto checkSelection = [&](const std::string &map) {
		CHECK(fixture.screen->setup()->info().mapName == map);
		const auto &currentKeys = fixture.screen->mapListKeys();
		const auto selected = std::find(currentKeys.begin(), currentKeys.end(), map);
		REQUIRE(selected != currentKeys.end());
		int row = -1;
		GadgetListBoxGetSelected(fixture.screen->mapListWindow(), &row);
		CHECK(row == (int)std::distance(currentKeys.begin(), selected));
	};
	checkSelection(keys[0]);
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	CHECK(fixture.screen->mapListKeys() == keys);
	CHECK(fixture.screen->mapListImages() == std::vector<std::string>(4, "AptDifficultyNotConquered"));
	checkSelection(keys[1]);
	fixture.choose(fixture.screen->profileComboWindow(), 0);
	CHECK(fixture.screen->mapListKeys() == keys);
	CHECK(fixture.screen->mapListImages() == images);
	checkSelection(keys[0]);
	fixture.choose(fixture.screen->mapTypeWindow(), 1);
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	CHECK_FALSE(fixture.screen->bfme1OfficialMaps());
	CHECK(fixture.screen->mapListKeys() == std::vector<std::string>{ "maps/custom/custom.map" });
	CHECK(fixture.screen->mapListImages() == std::vector<std::string>{ "AptDifficultyNotConquered" });
	fixture.choose(fixture.screen->mapTypeWindow(), 0);
	CHECK(fixture.screen->mapListImages() == std::vector<std::string>(4, "AptDifficultyNotConquered"));
}

TEST_CASE("bfme1 skirmish profile: recorded matches persist per-profile faction statistics once and refresh the display")
{
	Bfme1ProfileFixture fixture;
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	GameRecord record;
	record.id = "bfme1-profile-win";
	record.kind = "skirmish";
	record.mode = "faithful";
	record.map = "maps/fords/fords.map";
	record.start = 1700000000;
	record.end = record.start + 3600;
	record.durationS = 3600;
	record.players = { { 0, "Alice", "FactionGondor", 0, true }, { 1, "AI", "FactionRohan", 1, false } };
	record.meSlot = 0;
	record.winnerTeam = 0;
	std::string error;
	REQUIRE_MESSAGE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error), error);
	REQUIRE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error));
	fixture.screen->recordsChanged();
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "8");
	CHECK(fixture.value("APT:CurrentWinStreakNumA") == "3");
	CHECK(fixture.value("APT:TotalTimePlayedNum") == "1 days 4 hours");
	CHECK(fixture.value("APT:SkirmishProfileCreatedNum") == "2025-04-17");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 2");
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	record.id = "bfme1-profile-loss";
	record.winnerTeam = 1;
	REQUIRE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error));
	fixture.choose(fixture.screen->profileComboWindow(), 0);
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	CHECK(fixture.value("APT:TotalCareerWinsNum") == "8");
	CHECK(fixture.value("APT:TotalCareerLossesNum") == "4");
	CHECK(fixture.value("APT:CurrentWinStreakNumA") == "0");
	CHECK(fixture.value("APT:TotalTimePlayedNum") == "1 days 5 hours");
	record.id = "bfme1-profile-cheat";
	record.cheatMode = true;
	REQUIRE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error));
	fixture.screen->recordsChanged();
	CHECK(fixture.value("APT:TotalCareerLossesNum") == "4");
	record.id = "bfme1-profile-lan";
	record.cheatMode = false;
	record.kind = "lan";
	record.players[0].nick = "Bob";
	REQUIRE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error));
	CHECK_FALSE(std::filesystem::exists(fixture.folder / "BobSkirmishStats.ini"));
	record.id = "bfme1-profile-old-date-unknown";
	record.kind = "skirmish";
	REQUIRE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error));
	fixture.choose(fixture.screen->profileComboWindow(), 1);
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	CHECK(fixture.value("APT:TotalCareerLossesNum") == "1");
	CHECK(fixture.value("APT:SkirmishProfileCreatedNum") == "--");
}

TEST_CASE("bfme1 skirmish profile: rank points and hard-glow honors require a matching actual difficulty snapshot")
{
	Bfme1ProfileFixture fixture;
	fixture.choose(fixture.screen->slotGadget(0, "PlayerTemplate"), 1);
	GameRecord record;
	record.id = "bfme1-profile-hard-win";
	record.kind = "skirmish";
	record.mode = "faithful";
	record.map = "maps/fords/fords.map";
	record.durationS = 3600;
	record.players = { { 0, "Alice", "FactionGondor", 0, true }, { 1, "AI", "FactionRohan", 1, false } };
	record.meSlot = 0;
	record.winnerTeam = 0;
	SkirmishHonorsGame game;
	game.localSlot = 0;
	game.mapKey = record.map;
	game.mapPlayers = 2;
	game.slots[0] = { SLOT_PLAYER, -1 };
	game.slots[1] = { SLOT_HARD_AI, -1 };
	std::string error;
	OptionPreferences saved;
	const std::string path = (fixture.folder / "AliceSkirmishStats.ini").string();
	REQUIRE(saved.load(path, &error));
	saved.erase("maps\\fords\\fords.map_5");
	REQUIRE(saved.save(path, &error));
	const auto keys = fixture.screen->mapListKeys();
	const auto found = std::find(keys.begin(), keys.end(), record.map);
	REQUIRE(found != keys.end());
	const std::size_t row = (std::size_t)std::distance(keys.begin(), found);
	fixture.screen->recordsChanged();
	CHECK(fixture.screen->mapListImages()[row] == "AptDifficultyNotConquered");
	REQUIRE_MESSAGE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error, &game), error);
	fixture.screen->recordsChanged();
	CHECK(fixture.screen->mapListKeys() == keys);
	CHECK(fixture.screen->mapListImages()[row] == "AptDifficultyHardGlowConquered");
	CHECK(fixture.value("APT:CurrentLevelFormat") == "Level 3");
	CHECK(fixture.value("APT:NextWinsNumber") == "17 wins");
	CHECK(fixture.movieValue("profileProgress") == "15");
	REQUIRE(saved.load(path, &error));
	CHECK(saved.get("GondorPoints") == "13");
	CHECK(saved.get("maps\\fords\\fords.map_5") == "1");
	record.id = "bfme1-profile-mismatched-snapshot";
	game.mapKey = "maps/other/other.map";
	REQUIRE(Bfme1SkirmishProfiles::recordGame(*fixture.profiles, fixture.setup, record, &error, &game));
	REQUIRE(saved.load(path, &error));
	CHECK(saved.get("GondorPoints") == "13");
	CHECK(saved.get("GondorWins") == "9");
}

TEST_CASE("bfme1 skirmish profile: map list uses two columns, counted names and hard-glow honors from SkirmishStats")
{
	Bfme1ProfileFixture fixture;
	GameWindow *list = fixture.screen->mapListWindow();
	REQUIRE(list);
	CHECK(GadgetListBoxGetNumColumns(list) == 2);
	const auto *data = static_cast<const ListboxData *>(list->winGetUserData());
	REQUIRE(data);
	CHECK(data->columnWidthPercentage == std::vector<int>{ 10, 90 });
	const auto &keys = fixture.screen->mapListKeys();
	REQUIRE(keys.size() == 4);
	const std::map<std::string, std::pair<std::u16string, std::string>> expected = {
		{ "maps/fords/fords.map", { u"Fords of Isen (2)", "AptDifficultyHardGlowConquered" } },
		{ "maps/hard/hard.map", { u"Hard map (2)", "AptDifficultyHardConquered" } },
		{ "maps/medium/medium.map", { u"Medium map (2)", "AptDifficultyMedConquered" } },
		{ "maps/easy/easy.map", { u"Easy map (2)", "AptDifficultyEasyConquered" } }
	};
	for (std::size_t row = 0; row < keys.size(); ++row)
	{
		const auto &entry = expected.at(keys[row]);
		CHECK(GadgetListBoxGetText(list, (int)row, 1) == entry.first);
		CHECK(fixture.screen->mapListImages()[row] == entry.second);
	}
	fixture.choose(fixture.screen->mapTypeWindow(), 1);
	CHECK(fixture.screen->mapListKeys() == std::vector<std::string>{ "maps/custom/custom.map" });
}
