// OpenBFME tests: the Skirmish lobby (spec menus-apt.md build step A6): the slot logic of ZH SkirmishGameOptionsMenu / GUIUtil, the interim
// setup source, the profile commands, the MpGameSetup / AptMapPreview gadget handlers and the click-through of Skirmish.apt down to the
// new-game message.  Synthetic tests use data written here; the retail tests mount the shipped archives and SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/GUI/WindowManager.h"

#include "Libraries/Source/Apt/AptActionDecoder.h"

#include <algorithm>
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
	t.set("GUI:TooManyPlayers", "Too many players: the map has %d");
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
	CHECK(g.startingCash == 1500);
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
	CHECK(title == u"Error starting game");
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
	// an unknown map in the info (a cache that changed under the lobby) is the first refusal
	FakeSetup other;
	other.map.clear();
	other.map.push_back(FakeSetup::mapEntry("maps/other/other.map", "$Map:Other", 2, true, 1));
	SkirmishGameSetup lost(other, &text);
	REQUIRE(lost.init(u"P", 1, std::string(), nullptr));
	other.map.clear();
	CHECK_FALSE(lost.validateStart(title, body));
	CHECK(body == u"Cannot find the map");
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
	for (int slot = 0; slot < MAX_SLOTS; ++slot)
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
	expected.startingCash = 1500;
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
	LobbyFx fx(mount);
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
	for (int slot = 1; slot < MAX_SLOTS; ++slot)
	{
		GadgetComboBoxSetSelectedPos(fx.screen->slotGadget(slot, "Player"), 2, false); // every slot an easy AI
	}
	CHECK(fx.screen->setup()->info().numPlayers() == 8);
	fx.wm->invokeCallback("AptSkirmish::StartGame", "");
	fx.tick(2); // the screen update validates and posts (RotWK state 10)
	CHECK(fx.sink.messages.empty());
	REQUIRE(fx.sink.refusals.size() == 1);
	CHECK(fx.sink.refusals[0].first == u"Error starting game");
	CHECK(fx.sink.refusals[0].second == asciiToU16("Too many players: the map has " + std::to_string(map->numPlayers)));
	// slots over the map's size are closed again: the lobby starts
	for (int slot = (int)map->numPlayers; slot < MAX_SLOTS; ++slot)
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
	// the stats screens are not ported and say so
	fx.wm->invokeCallback("AptSkirmish::OnStatsMenu", "");
	CHECK(fx.wm->noteCount("unported-command") >= 1);
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
	// combo stays empty (with one: "-", GUI:Random and the heroes, test_cah1.cpp). The Handicap combo stays empty
	GameWindow *hero = fx.screen->slotGadget(0, "Hero");
	REQUIRE(hero);
	CHECK(GadgetComboBoxGetLength(hero) == 0);
	CHECK(GadgetComboBoxGetLength(fx.screen->slotGadget(0, "Handicap")) == 0);
	// the rule rows and the chat / clan tabs answer 0
	std::string value;
	REQUIRE(fx.wm->getExtern("MpGameRules::NumCheckBoxes", value) == AptExternResult::Value);
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
	expected.startingCash = 1500;
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
	for (int slot = 1; slot < MAX_SLOTS; ++slot)
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
	CHECK(fx.sink.refusals[0].first == u"Error starting game");
	CHECK(fx.sink.refusals[0].second == asciiToU16("Too many players: the map has " + std::to_string(map->numPlayers)));
	for (int slot = (int)map->numPlayers; slot < MAX_SLOTS; ++slot)
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
