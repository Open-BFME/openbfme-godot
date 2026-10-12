// OpenBFME unit tests. GPL-3.0.
//
// Lane MENU-1: which loading screen a game start shows (GameLogic::getLoadScreen RW 0x626A87) and the campaign's picture screen (RW 0x7583CC),
// GameClient/GUI/StillImageLoadScreen.h. The owner's campaign mission loaded behind the skirmish players' screen, empty.

#include "doctest.h"
#include "AptRetail.h"
#include "StartShellFx.h"

#include <set>

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/StillImageLoadScreen.h"

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

TEST_CASE("menu1 load screen rank icons (RW 0x73BDA3 / 0x960621 / 0x9604E7): side from the army text, computer levels by difficulty, the player's by points")
{
	std::set<std::string> images = { "AptRankIconDwarves4", "AptRankIconWild1", "AptRankIconMen9", "AptRankIcon1", "AptRankIconIsengard10", "AptRankIconElves2" };
	auto exists = [&](const std::string &n) { return images.count(n) != 0; };
	SkirmishGameSlot slot;
	slot.state = SLOT_MED_AI;
	CHECK(rankIconFor(slot, u"Dwarves", 0, exists) == "AptRankIconDwarves4");
	slot.state = SLOT_BRUTAL_AI;
	CHECK(rankIconFor(slot, u"men", 0, exists) == "AptRankIconMen9"); // the side matches without case
	slot.state = SLOT_PLAYER;
	CHECK(rankIconFor(slot, u"Goblins", 0, exists) == "AptRankIconWild1"); // Goblins is the Wild side; 0 points: level 1
	CHECK(rankIconFor(slot, u"Elves", 5, exists) == "AptRankIconElves2");   // 5 points reach the second threshold
	CHECK(rankIconFor(slot, u"Isengard", 99999, exists) == "AptRankIconIsengard10");
	CHECK(rankIconFor(slot, u"Hobbits", 0, exists) == "AptRankIcon1"); // no side: AptRankIcon<level>
	slot.state = SLOT_EASY_AI;
	CHECK(rankIconFor(slot, u"Mordor", 0, exists) == "AptRankIcon1"); // no AptRankIconMordor1: the plain icon of the level
	images.erase("AptRankIcon1");
	CHECK(rankIconFor(slot, u"Mordor", 0, exists).empty()); // none at all
	slot.state = SLOT_OPEN;
	CHECK(rankIconFor(slot, u"Men", 0, exists).empty()); // no level for an open slot
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
			forced += n.detail.find("is level 1") != std::string::npos;
			rule += n.detail.find("name rule") != std::string::npos;
		}
	}
	CHECK(forced == 1);
	CHECK(rule == 1);
}
