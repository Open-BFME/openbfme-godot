// OpenBFME unit tests. GPL-3.0.
//
// Lane MENU-1: which loading screen a game start shows (GameLogic::getLoadScreen RW 0x626A87) and the campaign's picture screen (RW 0x7583CC),
// GameClient/GUI/StillImageLoadScreen.h. The owner's campaign mission loaded behind the skirmish players' screen, empty.

#include "doctest.h"
#include "AptRetail.h"
#include "StartShellFx.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <set>

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/StillImageLoadScreen.h"
#include "GameClient/GUI/Skirmish/RealTimeStats.h"

TEST_CASE("menu1 load screen choice (RW 0x626A87): a campaign mission shows its picture, a skirmish the players' screen, a replay a film still")
{
	int draws = 0;
	auto film7 = [&](int lo, int hi) {
		++draws;
		CHECK(lo == 1);
		CHECK(hi == 21);
		return 7;
	};
	LoadScreenChoice c = chooseLoadScreen(0, 0, false, "CampaignAngmar_Angmar_Loadscreen", film7);
	CHECK(c.kind == LoadScreenKind::StillImage);
	CHECK(c.picture == "CampaignAngmar_Angmar_Loadscreen");
	CHECK(draws == 0);
	c = chooseLoadScreen(0, 0, false, "", film7); // a mission without a picture: a film still
	CHECK(c.picture == "StillImage_Film07");
	c = chooseLoadScreen(0, 0, true, "CampaignAngmar_Angmar_Loadscreen", film7); // a save game loading: a film still
	CHECK(c.picture == "StillImage_Film07");
	c = chooseLoadScreen(3, 0, false, "CampaignAngmar_Angmar_Loadscreen", film7); // a replay never shows the picture
	CHECK(c.kind == LoadScreenKind::StillImage);
	CHECK(c.picture == "StillImage_Film07");
	CHECK(chooseLoadScreen(2, 3, false, "", film7).kind == LoadScreenKind::Players); // a skirmish from the lobby
	CHECK(chooseLoadScreen(1, 3, false, "", film7).kind == LoadScreenKind::Players); // LAN
	CHECK(chooseLoadScreen(2, 0, false, "", film7).kind == LoadScreenKind::StillImage); // GameLogic + 0x114 == 0
	CHECK(chooseLoadScreen(4, 0, false, "", film7).kind == LoadScreenKind::Other);
	CHECK(draws == 4);
	// lane MENU-1 review: no random source is an error, never a silent Film01
	c = chooseLoadScreen(3, 0, false, "", nullptr);
	CHECK(c.picture.empty());
	CHECK_FALSE(c.error.empty());
}

TEST_CASE("menu1 load screen retail: the picture screen of Angmar's first mission is RotWK's layout (RW 0x7583CC)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
	StillImageLoadScreenLayout layout;
	REQUIRE_MESSAGE(layoutStillImageLoadScreen(skins.images, "CampaignAngmar_Angmar_Loadscreen", layout, &error), error);
	CHECK(layout.slots[2].on);
	CHECK(layout.slots[2].image == "LoadScreenForeground");
	CHECK(layout.slots[2].x1 == 1.0f);
	CHECK(layout.slots[2].y1 == 1.0f);
	CHECK(layout.slots[0].on);
	CHECK(layout.slots[0].y0 == 0.1640625f);
	// the picture is 1024 x 512 of its texture (Coords 0..1023, 0..511): 511 high in 768ths of the screen
	const Image *img = skins.images.findImageByName("CampaignAngmar_Angmar_Loadscreen");
	REQUIRE(img);
	CHECK(layout.slots[0].y1 == doctest::Approx(0.1640625 + img->getImageHeight() / 768.0).epsilon(1e-6));
	CHECK_FALSE(layout.slots[1].on);
	CHECK(layout.movie == "SmallRing");
	CHECK(layout.textLabel == "GUI:Loading");
	CHECK(layout.unverified.find("[S-3505]") == 0);
	// every retail campaign mission's picture and every film still is a mapped image
	for (int i = 1; i <= 21; ++i)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "StillImage_Film%02d", i);
		CHECK_MESSAGE(skins.images.findImageByName(name) != nullptr, name);
	}
	CHECK_FALSE(layoutStillImageLoadScreen(skins.images, "NoSuchPicture", layout, &error));
}

TEST_CASE("menu1 load screen rank icons (RW 0x73BDA3 / 0x960621 / 0x96054C): side from the army text, computer levels by difficulty, the player's by points")
{
	std::set<std::string> images = { "AptRankIconDwarves4", "AptRankIconWild1", "AptRankIconMen9", "AptRankIcon1", "AptRankIconIsengard10", "AptRankIconElves2" };
	auto exists = [&](const std::string &n) { return images.count(n) != 0; };
	SkirmishGameSlot slot;
	slot.state = SLOT_MED_AI;
	CHECK(rankIconFor(slot, u"Dwarves", RankPointTable::LocalRealTime, 0, exists) == "AptRankIconDwarves4");
	slot.state = SLOT_BRUTAL_AI;
	CHECK(rankIconFor(slot, u"men", RankPointTable::LocalRealTime, 0, exists) == "AptRankIconMen9"); // the side matches without case
	slot.state = SLOT_PLAYER;
	CHECK(rankIconFor(slot, u"Goblins", RankPointTable::LocalRealTime, 0, exists) == "AptRankIconWild1"); // Goblins is the Wild side; 0 points: level 1
	CHECK(rankIconFor(slot, u"Elves", RankPointTable::LocalRealTime, 5, exists) == "AptRankIconElves2");   // 5 points reach the second threshold
	CHECK(rankIconFor(slot, u"Isengard", RankPointTable::LocalRealTime, 99999, exists) == "AptRankIconIsengard10");
	CHECK(rankIconFor(slot, u"Hobbits", RankPointTable::LocalRealTime, 0, exists) == "AptRankIcon1"); // no side: AptRankIcon<level>
	slot.state = SLOT_EASY_AI;
	CHECK(rankIconFor(slot, u"Mordor", RankPointTable::LocalRealTime, 0, exists) == "AptRankIcon1"); // no AptRankIconMordor1: the plain icon of the level
	images.erase("AptRankIcon1");
	CHECK(rankIconFor(slot, u"Mordor", RankPointTable::LocalRealTime, 0, exists).empty()); // none at all
	slot.state = SLOT_OPEN;
	CHECK(rankIconFor(slot, u"Men", RankPointTable::LocalRealTime, 0, exists).empty()); // no level for an open slot
}


namespace
{
// lane RANKICON-1: synthetic fixtures (no retail text): two playable factions, a text table, a profile store with a user data folder
class RankSetup : public SkirmishSetupSource
{
public:
	RankSetup()
	{
		SkirmishFaction men;
		men.templateName = "FactionMen";
		men.displayLabel = "INI:FactionMen";
		SkirmishFaction elves;
		elves.templateName = "FactionElves";
		elves.displayLabel = "INI:FactionElves";
		m_factions = { men, elves };
	}
	const std::vector<SkirmishFaction> &factions() const override { return m_factions; }
	const std::vector<SkirmishColor> &colors() const override { return m_colors; }
	const std::vector<MapCacheEntry> &maps() const override { return m_maps; }
	int defaultStartingCash() const override { return 0; }
	const std::vector<int> &startingCashChoices() const override { return m_cash; }
	int mapContentsMask(const MapCacheEntry &) const override { return 0; }
	bool fileExists(const std::string &) const override { return false; }
	bool hasFileSystem() const override { return false; }

private:
	std::vector<SkirmishFaction> m_factions;
	std::vector<SkirmishColor> m_colors;
	std::vector<MapCacheEntry> m_maps;
	std::vector<int> m_cash;
};

class RankText : public GameTextSource
{
public:
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		if (label == "INI:FactionMen")
		{
			out = u"Men";
			return true;
		}
		if (label == "INI:FactionElves")
		{
			out = u"Elves";
			return true;
		}
		return false;
	}
};

class RankProfiles : public MemorySkirmishProfiles
{
public:
	explicit RankProfiles(std::string folder) : m_folder(std::move(folder)) {}
	std::string folder() const override { return m_folder; }

private:
	std::string m_folder;
};

struct RankTempFolder
{
	std::filesystem::path dir;
	RankTempFolder()
	{
		dir = std::filesystem::temp_directory_path() / ("openbfme-rankicon1-" + std::to_string(std::random_device{}()) + "-" + std::to_string(std::random_device{}()));
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
	}
	~RankTempFolder() { std::filesystem::remove_all(dir); }
	std::string folder() const { return dir.string() + "/"; }
	void write(const std::string &leaf, const std::string &text) const
	{
		std::ofstream out(dir / leaf, std::ios::binary);
		out << text;
	}
};
} // namespace

TEST_CASE("rankicon1 rank tables (RW 0x96054C local, RW 0x9604E7 online, RW 0x960597): thresholds, levels and the table a load screen picks")
{
	const int local[11] = { -1, 0, 5, 10, 30, 50, 150, 300, 500, 800, 1500 };
	const int online[11] = { -1, 0, 5, 10, 30, 50, 150, 500, 1000, 2000, 5000 };
	for (int i = 0; i < 11; ++i)
	{
		CHECK(rankPointThresholds(RankPointTable::LocalStrategic)[i] == local[i]);
		CHECK(rankPointThresholds(RankPointTable::LocalRealTime)[i] == local[i]);
		CHECK(rankPointThresholds(RankPointTable::OnlineStrategic)[i] == online[i]);
		CHECK(rankPointThresholds(RankPointTable::OnlineRealTime)[i] == online[i]);
	}
	// the choice: local vs online, real-time vs War of the Ring (TheGameInfo game mode 1)
	CHECK(loadScreenRankTable(false, false) == RankPointTable::LocalRealTime);
	CHECK(loadScreenRankTable(false, true) == RankPointTable::LocalStrategic);
	CHECK(loadScreenRankTable(true, false) == RankPointTable::OnlineRealTime);
	CHECK(loadScreenRankTable(true, true) == RankPointTable::OnlineStrategic);
	// the local table: 300 / 800 / 1500 are levels 7 / 9 / 10 (the online table gives 6 / 7 / 8)
	const RankPointTable L = RankPointTable::LocalRealTime, O = RankPointTable::OnlineRealTime;
	CHECK(rankLevelForPoints(L, -5) == 0);
	CHECK(rankLevelForPoints(L, 0) == 1);
	CHECK(rankLevelForPoints(L, 4) == 1);
	CHECK(rankLevelForPoints(L, 5) == 2);
	CHECK(rankLevelForPoints(L, 150) == 6);
	CHECK(rankLevelForPoints(L, 299) == 6);
	CHECK(rankLevelForPoints(L, 300) == 7);
	CHECK(rankLevelForPoints(L, 800) == 9);
	CHECK(rankLevelForPoints(L, 1499) == 9);
	CHECK(rankLevelForPoints(L, 1500) == 10);
	CHECK(rankLevelForPoints(L, 4999) == 10);
	CHECK(rankLevelForPoints(RankPointTable::LocalStrategic, 800) == 9);
	CHECK(rankLevelForPoints(O, 300) == 6);
	CHECK(rankLevelForPoints(O, 800) == 7);
	CHECK(rankLevelForPoints(O, 1500) == 8);
	CHECK(rankLevelForPoints(O, 4999) == 9);
	CHECK(rankLevelForPoints(O, 5000) == 10);
	// the icon of a human slot in the local table; a computer's is unchanged (1 / 4 / 6 / 9 by difficulty)
	auto any = [](const std::string &n) { return n.rfind("AptRankIcon", 0) == 0; };
	SkirmishGameSlot human;
	human.state = SLOT_PLAYER;
	human.playerTemplate = 0;
	CHECK(rankIconFor(human, u"Men", L, 800, any, 2) == "AptRankIconMen9");
	CHECK(rankIconFor(human, u"Men", O, 800, any, 2) == "AptRankIconMen7");
	human.playerTemplate = -2; // an observer: no player template, no icon (RW 0x81CF10)
	CHECK(rankIconFor(human, u"Men", L, 800, any, 2).empty());
	SkirmishGameSlot ai;
	ai.state = SLOT_HARD_AI;
	ai.playerTemplate = 0;
	CHECK(rankIconFor(ai, u"Men", L, 1500, any, 2) == "AptRankIconMen6");
	CHECK(rankIconFor(ai, u"Men", O, 1500, any, 2) == "AptRankIconMen6");
}

TEST_CASE("rankicon1 load screen: a human card's level is its army's <army>Points in the current profile's RealTimeStats.ini / StrategicStats.ini")
{
	RankTempFolder t;
	RankSetup setup;
	RankText text;
	RankProfiles profiles(t.folder());
	REQUIRE(profiles.add(u"Tester"));
	REQUIRE(profiles.setCurrent(u"Tester"));
	auto any = [](const std::string &n) { return n.rfind("AptRankIcon", 0) == 0; };
	SkirmishGameInfo game;
	game.slots[0].state = SLOT_PLAYER;
	game.slots[0].name = u"one";
	game.slots[0].playerTemplate = 0; // Men
	game.slots[1].state = SLOT_PLAYER;
	game.slots[1].name = u"two";
	game.slots[1].playerTemplate = 1; // Elves
	game.slots[2].state = SLOT_BRUTAL_AI;
	game.slots[2].playerTemplate = 1;
	// no stats file yet: 0 points, level 1 (retail's preferences object reads a missing file as no keys)
	LoadScreenInfo info = makeLoadScreenInfo(game, setup, &text, any, &profiles);
	CHECK(info.rankStatsRead);
	CHECK(info.rankStatsError.empty());
	CHECK(info.rankStatsPath == t.folder() + "TesterRealTimeStats.ini");
	CHECK(info.cards[0].levelImage == "AptRankIconMen1");
	CHECK(info.cards[1].levelImage == "AptRankIconElves1");
	CHECK(info.cards[2].levelImage == "AptRankIconElves9");
	// a real-time stats file: each human card reads its own army's key (retail reads the current profile for every human card of a skirmish)
	t.write("TesterRealTimeStats.ini", "MenPoints = 150\r\nElvesPoints = 800\r\nMordorPoints = 5000\r\n");
	info = makeLoadScreenInfo(game, setup, &text, any, &profiles);
	CHECK(info.rankStatsError.empty());
	CHECK(info.cards[0].rankPoints == 150);
	CHECK(info.cards[0].levelImage == "AptRankIconMen6");
	CHECK(info.cards[1].rankPoints == 800);
	CHECK(info.cards[1].levelImage == "AptRankIconElves9"); // the online table would give 7
	CHECK(info.cards[2].rankPoints == 0);
	CHECK(info.cards[2].levelImage == "AptRankIconElves9"); // a computer: brutal, unchanged
	// a War of the Ring battle (game mode 1) reads StrategicStats.ini instead: none yet, level 1
	game.gameMode = 1;
	info = makeLoadScreenInfo(game, setup, &text, any, &profiles);
	CHECK(info.rankStatsPath == t.folder() + "TesterStrategicStats.ini");
	CHECK(info.cards[0].levelImage == "AptRankIconMen1");
	t.write("TesterStrategicStats.ini", "MenPoints = 1500\r\n");
	info = makeLoadScreenInfo(game, setup, &text, any, &profiles);
	CHECK(info.cards[0].levelImage == "AptRankIconMen10");
	CHECK(info.cards[1].levelImage == "AptRankIconElves1");
	// a stats path that cannot be read is reported, not a silent default: the cards show 0 points
	game.gameMode = 0;
	std::filesystem::remove(t.dir / "TesterRealTimeStats.ini");
	std::filesystem::create_directories(t.dir / "TesterRealTimeStats.ini");
	info = makeLoadScreenInfo(game, setup, &text, any, &profiles);
	CHECK(info.rankStatsError.find("is not a file") != std::string::npos);
	CHECK(info.cards[1].levelImage == "AptRankIconElves1");
	// an online game reads no local stats (retail: the online records, RW 0x81CDAB): 0 points
	std::filesystem::remove(t.dir / "TesterRealTimeStats.ini");
	t.write("TesterRealTimeStats.ini", "ElvesPoints = 800\r\n");
	info = makeLoadScreenInfo(game, setup, &text, any, &profiles);
	CHECK(info.cards[1].levelImage == "AptRankIconElves9");
	info = makeLoadScreenInfo(game, setup, &text, any, &profiles, true);
	CHECK_FALSE(info.rankStatsRead);
	CHECK(info.cards[1].rankPoints == 0);
	CHECK(info.cards[1].levelImage == "AptRankIconElves1");
	// no profile store: nothing read
	info = makeLoadScreenInfo(game, setup, &text, any);
	CHECK_FALSE(info.rankStatsRead);
	CHECK(info.cards[1].levelImage == "AptRankIconElves1");
	// the strategic stats file's own reader
	StrategicStatsPreferences strategic;
	std::string error;
	t.write("TesterStrategicStats.ini", "ElvesPoints = 42\r\n");
	REQUIRE_MESSAGE(strategic.load(t.folder(), u"Tester", &error), error);
	CHECK(strategic.armyPoints("Elves") == 42);
	CHECK(strategic.armyPoints("Men") == 0);
}

TEST_CASE("menu1 load screen retail: the Level column shows each card's rank icon and reports the player's forced level 1 (S-3506)")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 4242);
	fx.tick(2);
	SkirmishGameInfo game;
	game.slots[0].state = SLOT_PLAYER;
	game.slots[0].name = u"asdasd";
	game.slots[1].state = SLOT_MED_AI;
	int dwarves = -1;
	for (int f = 0; f < (int)fx.setup.factions().size(); ++f)
	{
		dwarves = fx.setup.factions()[(size_t)f].templateName == "FactionDwarves" ? f : dwarves;
	}
	REQUIRE(dwarves >= 0);
	fx.text.set(fx.setup.factions()[(size_t)dwarves].displayLabel, "Dwarves"); // the army text the side is read from (English retail)
	game.slots[0].playerTemplate = dwarves;
	game.slots[1].playerTemplate = dwarves;
	game.slots[0].color = 0;
	game.slots[1].color = 1;
	fx.loadScreen = makeLoadScreenInfo(game, fx.setup, &fx.text, [&](const std::string &n) { return fx.skins.images.findImageByName(n) != nullptr; });
	REQUIRE(fx.loadScreen.cards[0].occupied);
	CHECK(fx.loadScreen.cards[0].levelImage == "AptRankIconDwarves1");
	CHECK(fx.loadScreen.cards[1].levelImage == "AptRankIconDwarves4");
	fx.shell->push("LoadScreen.apt");
	fx.tick(30);
	REQUIRE(fx.wm->aptImage("UIClip/Level/0"));
	CHECK(*fx.wm->aptImage("UIClip/Level/0") == "AptRankIconDwarves1");
	REQUIRE(fx.wm->aptImage("UIClip/Level/1"));
	CHECK(*fx.wm->aptImage("UIClip/Level/1") == "AptRankIconDwarves4");
	CHECK(fx.wm->aptImage("UIClip/Level/2") == nullptr);
	int forced = 0, rule = 0;
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		if (n.kind == "loadscreen-level" && n.detail.find("[S-3506]") == 0)
		{
			forced += n.detail.find("is for 0 points") != std::string::npos;
			rule += n.detail.find("name rule") != std::string::npos;
		}
	}
	CHECK(forced == 1);
	CHECK(rule == 1);
}
