// OpenBFME. GPL-3.0.
//
// Local 56: Loading screen map preview and start position numbers.
// RotWK TARGET FACTS (game.dat RW 0x..., caveat S-001):
//   * MultiplayerSettings::MultiplayerSettings (RW 0x7836C0): +0x1E (m_showRandomStartPos) defaults to 1; INI table RW 0xC2F798
//     parses "ShowRandomStartPos" as Bool at offset 0x1E.
//   * GameInfo::isSlotLocalAlly (RW 0x801190): true if slot == localSlot (>= 0), or same non-negative teamNumber, or localSlot is observer
//     (origPlayerTemplate == PLAYERTEMPLATE_OBSERVER = -2, RW 0x8011CF).
//   * GameSlot::getApparentStartPos (RW 0x801249): when TheMultiplayerSettings +0x1E != 0 and !isSlotLocalAlly(slot), returns
//     m_origStartPos (+0x28, hiding unresolved Random = -1 for opponents); otherwise returns m_startPos (+0x10).
//   * AptLoadScreen ctor (RW 0x81D356) embeds AptMapPreview (ctor RW 0x976240) at +0x18 and keeps +0x61 (m_enableStartSpots) = 0 (false).
//   * AptLoadScreen init/update (RW 0x81C5D3 -> 0x977799 -> 0x97759F): updates Picture (RW 0x97640C), CurrentMap + start spot buttons
//     (RW 0x9772E6 -> 0x976F59 -> winEnable(0) at 0x976FFE), MapInfo (RW 0x976497), then RW 0x977542(1) -> RW 0x7052BE(onLoadScreen=true):
//     sets text "NUMBER:<slot + 1>" via GadgetButtonSetText (RW 0x729E4A) for non-observers without tooltips (RW 0x705336..0x705395).
#include "doctest.h"

#include "AptRetail.h"
#include "StartShellFx.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

#include <string>
#include <vector>

namespace
{
bool hasNoteKind(const std::vector<WindowManagerNote> &notes, const std::string &kind)
{
	for (const WindowManagerNote &n : notes)
	{
		if (n.kind == kind)
		{
			return true;
		}
	}
	return false;
}

const MapCacheEntry *findMultiplayerMapWithPlayers(const SkirmishSetupSource &setup, int exactPlayers, int minPlayers)
{
	for (const MapCacheEntry &m : setup.maps())
	{
		if (!m.isMultiplayer || m.numPlayers < minPlayers || (int)m.startPositions.size() < m.numPlayers)
		{
			continue;
		}
		if (exactPlayers > 0 && m.numPlayers != exactPlayers)
		{
			continue;
		}
		bool allPresent = true;
		for (int p = 1; p <= m.numPlayers; ++p)
		{
			if (!m.startPositions.count(p))
			{
				allPresent = false;
				break;
			}
		}
		if (allPresent)
		{
			return &m;
		}
	}
	return nullptr;
}

void addStartSpotLabels(starttest::MapGameText &text)
{
	text.set("GUI:Blank", "Blank");
	for (int i = 1; i <= MAX_SLOTS; ++i)
	{
		text.set("NUMBER:" + std::to_string(i), std::to_string(i));
	}
}
} // namespace

TEST_CASE("LOCAL-56 RW 0x801190 / 0x801249: isSlotLocalAlly and getApparentStartPos match RotWK")
{
	SkirmishGameInfo game;
	for (int i = 0; i < 6; ++i)
	{
		game.slots[i].state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		game.slots[i].playerTemplate = 0;
		game.slots[i].origPlayerTemplate = 0;
	}
	// Slot 0 (local): team 0, random -> resolved 2
	game.slots[0].teamNumber = 0;
	game.slots[0].origStartPos = -1;
	game.slots[0].startPos = 2;
	// Slot 1 (ally): team 0, random -> resolved 3
	game.slots[1].teamNumber = 0;
	game.slots[1].origStartPos = -1;
	game.slots[1].startPos = 3;
	// Slot 2 (enemy on team 1): random -> resolved 1
	game.slots[2].teamNumber = 1;
	game.slots[2].origStartPos = -1;
	game.slots[2].startPos = 1;
	// Slot 3 (enemy with no team): random -> resolved 0
	game.slots[3].teamNumber = -1;
	game.slots[3].origStartPos = -1;
	game.slots[3].startPos = 0;
	// Slot 4 (enemy with fixed start pos 4)
	game.slots[4].teamNumber = 1;
	game.slots[4].origStartPos = 4;
	game.slots[4].startPos = 4;
	// Slot 5 (observer)
	game.slots[5].playerTemplate = PLAYERTEMPLATE_OBSERVER;
	game.slots[5].origPlayerTemplate = PLAYERTEMPLATE_OBSERVER;
	game.slots[5].teamNumber = -1;

	// Local player is slot 0 (team 0): sees self + team 0 ally; hides random enemies when showRandomStartPos = true
	CHECK(game.isSlotLocalAlly(0, 0));
	CHECK(game.isSlotLocalAlly(1, 0));
	CHECK_FALSE(game.isSlotLocalAlly(2, 0));
	CHECK_FALSE(game.isSlotLocalAlly(3, 0));
	CHECK_FALSE(game.isSlotLocalAlly(4, 0));

	CHECK(game.apparentStartPos(0, 0, true) == 2);
	CHECK(game.apparentStartPos(1, 0, true) == 3);
	CHECK(game.apparentStartPos(2, 0, true) == -1);
	CHECK(game.apparentStartPos(3, 0, true) == -1);
	CHECK(game.apparentStartPos(4, 0, true) == 4); // fixed opponent position is still shown

	// With showRandomStartPos = false, resolved startPos is shown for everyone
	CHECK(game.apparentStartPos(2, 0, false) == 1);
	CHECK(game.apparentStartPos(3, 0, false) == 0);

	// Two players both with teamNumber == -1 are NOT allies (RW 0x8011D7: test edx,edx; jl false)
	CHECK_FALSE(game.isSlotLocalAlly(2, 3));
	CHECK(game.apparentStartPos(2, 3, true) == -1);
	CHECK(game.apparentStartPos(3, 3, true) == 0);

	// Observer local slot sees all allies/opponents (RW 0x8011CF: cmp -2 -> return 1)
	CHECK(game.isSlotLocalAlly(2, 5));
	CHECK(game.isSlotLocalAlly(3, 5));
	CHECK(game.apparentStartPos(2, 5, true) == 1);
	CHECK(game.apparentStartPos(3, 5, true) == 0);
}

TEST_CASE("LOCAL-56 RW 0x7836C0 / 0xC2F798: ShowRandomStartPos parses and gates LoadScreenInfo")
{
	// Synthetic INI parse test for ShowRandomStartPos = Yes / No (RW 0xC2F798)
	std::string error;
	GameLogicSettings settingsDefault;
	CHECK(settingsDefault.showRandomStartPos); // RW 0x7836C0: [esi+0x1E] = 1
	auto mpBlock = [](const char *showRandom) {
		return std::string(
			"MultiplayerSettings\n"
			"  InitialCreditsVeryLow = 500\n"
			"  InitialCreditsLow = 1000\n"
			"  InitialCreditsMedium = 2000\n"
			"  InitialCreditsHigh = 4000\n"
			"  InitialCreditsVeryHigh = 10000\n"
			"  ShowRandomStartPos = ") + showRandom + "\nEnd\n";
	};
	CHECK_MESSAGE(GameLogicSettingsLoader::scanMultiplayer(mpBlock("No"), settingsDefault, &error), error);
	CHECK_FALSE(settingsDefault.showRandomStartPos);
	CHECK_MESSAGE(GameLogicSettingsLoader::scanMultiplayer(mpBlock("Yes"), settingsDefault, &error), error);
	CHECK(settingsDefault.showRandomStartPos);

	bool scannedFlag = true;
	std::vector<SkirmishColor> colors;
	std::vector<int> choices;
	CHECK_MESSAGE(IniSkirmishSetupSource::scanMultiplayer("MultiplayerSettings\n  ShowRandomStartPos = No\nEnd\n", colors, choices, &error, &scannedFlag), error);
	CHECK_FALSE(scannedFlag);

	// Retail RotWK multiplayer.ini check (skips loudly when ROTWK_INSTALL / BFME2_INSTALL are unset)
	OPENBFME_REQUIRE_START(s);
	IniSkirmishSetupSource iniSource;
	REQUIRE_MESSAGE(iniSource.load(*s->mount->fs, &error), error);
	CHECK(iniSource.showRandomStartPos());
	GameLogicSettings settings;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s->mount->fs, settings, &error), error);
	CHECK(settings.showRandomStartPos == iniSource.showRandomStartPos());

	const MapCacheEntry *map2p = findMultiplayerMapWithPlayers(iniSource, 2, 2);
	const MapCacheEntry *map4p = findMultiplayerMapWithPlayers(iniSource, 4, 4);
	REQUIRE(map2p != nullptr);
	REQUIRE(map4p != nullptr);

	SkirmishGameInfo game;
	game.mapName = map4p->name;
	game.slots[0].state = SLOT_PLAYER;
	game.slots[0].name = u"Player1";
	game.slots[0].playerTemplate = 0;
	game.slots[0].origPlayerTemplate = 0;
	game.slots[0].teamNumber = 0;
	game.slots[0].origStartPos = -1;
	game.slots[0].startPos = 1;
	game.slots[1].state = SLOT_MED_AI;
	game.slots[1].playerTemplate = 0;
	game.slots[1].origPlayerTemplate = 0;
	game.slots[1].teamNumber = 0;
	game.slots[1].origStartPos = -1;
	game.slots[1].startPos = 2;
	game.slots[2].state = SLOT_MED_AI;
	game.slots[2].playerTemplate = 0;
	game.slots[2].origPlayerTemplate = 0;
	game.slots[2].teamNumber = 1;
	game.slots[2].origStartPos = 3;
	game.slots[2].startPos = 3;
	game.slots[3].state = SLOT_MED_AI;
	game.slots[3].playerTemplate = 0;
	game.slots[3].origPlayerTemplate = 0;
	game.slots[3].teamNumber = 1;
	game.slots[3].origStartPos = -1;
	game.slots[3].startPos = 0;

	const LoadScreenInfo info = makeLoadScreenInfo(game, iniSource, nullptr, {}, nullptr, false, 0);
	CHECK(info.slotStartSpots[0] == 1);  // local player's resolved random pos
	CHECK(info.slotStartSpots[1] == 2);  // ally's resolved random pos
	CHECK(info.slotStartSpots[2] == 3);  // fixed enemy pos
	CHECK(info.slotStartSpots[3] == -1); // random enemy pos hidden when ShowRandomStartPos = true
}

TEST_CASE("LOCAL-56 RW 0x81C5D3 / 0x976240 / 0x7052BE: AptLoadScreen renders start position numbers on CurrentMap over Skirmish stack")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 4242);
	addStartSpotLabels(fx.text);
	fx.tick(2);

	const MapCacheEntry *map = findMultiplayerMapWithPlayers(fx.setup, 4, 4);
	REQUIRE(map != nullptr);
	REQUIRE(map->numPlayers == 4);

	// Stack LoadScreen.apt on top of Skirmish.apt (the real game flow)
	fx.shell->push("Skirmish.apt");
	fx.tick(30);
	CHECK(fx.wm->hasScreenRef("AptMapPreview::MapGadgetInit"));
	CHECK(fx.wm->hasProvider("AptMapPreview::GameMapType"));
	CHECK(fx.wm->renderPicture("AptMapPreview::Picture") != nullptr);

	SkirmishGameInfo game;
	game.mapName = map->name;
	game.slots[0].state = SLOT_PLAYER;
	game.slots[0].name = u"Hero";
	game.slots[0].playerTemplate = 0;
	game.slots[0].origPlayerTemplate = 0;
	game.slots[0].color = 0;
	game.slots[0].teamNumber = 0;
	game.slots[0].origStartPos = -1;
	game.slots[0].startPos = 2; // resolved random -> button index 2 shows "1"

	game.slots[1].state = SLOT_MED_AI;
	game.slots[1].playerTemplate = 0;
	game.slots[1].origPlayerTemplate = 0;
	game.slots[1].color = 1;
	game.slots[1].teamNumber = 0;
	game.slots[1].origStartPos = -1;
	game.slots[1].startPos = 0; // ally random -> button index 0 shows "2"

	game.slots[2].state = SLOT_MED_AI;
	game.slots[2].playerTemplate = 0;
	game.slots[2].origPlayerTemplate = 0;
	game.slots[2].color = 2;
	game.slots[2].teamNumber = 1;
	game.slots[2].origStartPos = 1;
	game.slots[2].startPos = 1; // fixed enemy -> button index 1 shows "3"

	game.slots[3].state = SLOT_MED_AI;
	game.slots[3].playerTemplate = 0;
	game.slots[3].origPlayerTemplate = 0;
	game.slots[3].color = 3;
	game.slots[3].teamNumber = 1;
	game.slots[3].origStartPos = -1;
	game.slots[3].startPos = 3; // random enemy -> hidden when ShowRandomStartPos = true

	fx.loadScreen = makeLoadScreenInfo(game, fx.setup, &fx.text, [&](const std::string &n) {
		return fx.skins.images.findImageByName(n) != nullptr;
	}, nullptr, false, 0);
	CHECK(fx.loadScreen.slotStartSpots[0] == 2);
	CHECK(fx.loadScreen.slotStartSpots[1] == 0);
	CHECK(fx.loadScreen.slotStartSpots[2] == 1);
	CHECK(fx.loadScreen.slotStartSpots[3] == -1);

	fx.shell->push("LoadScreen.apt");
	fx.tick(30);
	CHECK_FALSE(hasNoteKind(fx.wm->notes(), "registration-refused"));

	auto *ls = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(ls != nullptr);
	REQUIRE(ls->currentMap() != nullptr);
	REQUIRE(ls->startSpotButtons().size() == LEGACY_MAX_SLOTS);
	CHECK(ls->currentMap()->winGetEnabled());
	CHECK(ls->currentMap()->winGetUserData() == map);

	const auto &buttons = ls->startSpotButtons();
	for (int p = 0; p < map->numPlayers; ++p)
	{
		REQUIRE(buttons[(std::size_t)p] != nullptr);
		CHECK_FALSE(buttons[(std::size_t)p]->winIsHidden());
		// RW 0x976240 + 0x81D356 + 0x976FFE: load screen keeps m_enableStartSpots false
		CHECK_FALSE(buttons[(std::size_t)p]->winGetEnabled());
		// RW 0x705336..0x705395: no tooltips set on load screen
		CHECK(buttons[(std::size_t)p]->winGetInstanceData()->getTooltipText().empty());
	}
	CHECK(buttons[0]->winGetText() == u"2");
	CHECK(buttons[1]->winGetText() == u"3");
	CHECK(buttons[2]->winGetText() == u"1");
	CHECK(buttons[3]->winGetText().empty());

	for (std::size_t i = (std::size_t)map->numPlayers; i < buttons.size(); ++i)
	{
		CHECK(buttons[i]->winIsHidden());
		// RW 0x705336: when the map is present, updateStartSpots clears text on every start spot button before setting NUMBER:<n>
		CHECK(buttons[i]->winGetText().empty());
	}

	// Progress bar continues working seamlessly
	CHECK(ls->setLocalProgress(55));
	fx.tick(2);
	CHECK(ls->progressCalls().back() == std::pair<int, int>{ 0, 55 });

	// Popping LoadScreen returns ownership of AptMapPreview::* cleanly to Skirmish
	fx.shell->pop();
	fx.tick(10);
	auto *sk = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(sk != nullptr);
	CHECK(fx.wm->hasScreenRef("AptMapPreview::MapGadgetInit"));
	CHECK(fx.wm->hasProvider("AptMapPreview::GameMapType"));
	CHECK(fx.wm->renderPicture("AptMapPreview::Picture") != nullptr);
}

TEST_CASE("LOCAL-56 RW 0x81C5D3 / 0x8011CF: LoadScreen.apt 2p observer visibility, 4p start spots, and missing map fallback")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 4242);
	addStartSpotLabels(fx.text);
	fx.tick(2);

	const MapCacheEntry *map2p = findMultiplayerMapWithPlayers(fx.setup, 2, 2);
	REQUIRE(map2p != nullptr);

	SkirmishGameInfo game;
	game.mapName = map2p->name;
	game.slots[0].state = SLOT_PLAYER;
	game.slots[0].name = u"ObserverLocal";
	game.slots[0].playerTemplate = PLAYERTEMPLATE_OBSERVER;
	game.slots[0].origPlayerTemplate = PLAYERTEMPLATE_OBSERVER;
	game.slots[0].color = 0;
	game.slots[1].state = SLOT_MED_AI;
	game.slots[1].playerTemplate = 0;
	game.slots[1].origPlayerTemplate = 0;
	game.slots[1].color = 1;
	game.slots[1].origStartPos = -1;
	game.slots[1].startPos = 1; // random opponent, but observer sees it (RW 0x8011CF)

	fx.loadScreen = makeLoadScreenInfo(game, fx.setup, &fx.text, [&](const std::string &n) {
		return fx.skins.images.findImageByName(n) != nullptr;
	}, nullptr, false, 0);
	CHECK(fx.loadScreen.slotStartSpots[0] == -1);
	CHECK(fx.loadScreen.slotStartSpots[1] == 1);

	fx.shell->push("LoadScreen.apt");
	fx.tick(30);
	auto *ls = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(ls != nullptr);
	REQUIRE(ls->currentMap() != nullptr);
	REQUIRE(ls->startSpotButtons().size() == LEGACY_MAX_SLOTS);
	CHECK(ls->startSpotButtons()[0]->winGetText().empty());
	CHECK(ls->startSpotButtons()[1]->winGetText() == u"2");
	CHECK_FALSE(ls->startSpotButtons()[0]->winGetEnabled());
	CHECK_FALSE(ls->startSpotButtons()[1]->winGetEnabled());

	// 4p map on LoadScreen.apt (local fixed, ally random, opponent fixed, opponent random)
	const MapCacheEntry *map4p = findMultiplayerMapWithPlayers(fx.setup, 4, 4);
	REQUIRE(map4p != nullptr);
	fx.shell->pop();
	fx.tick(5);
	SkirmishGameInfo game4p;
	game4p.mapName = map4p->name;
	game4p.slots[0].state = SLOT_PLAYER;
	game4p.slots[0].name = u"HostPlayer";
	game4p.slots[0].playerTemplate = 0;
	game4p.slots[0].origPlayerTemplate = 0;
	game4p.slots[0].teamNumber = 0;
	game4p.slots[0].origStartPos = 0;
	game4p.slots[0].startPos = 0;
	game4p.slots[1].state = SLOT_MED_AI;
	game4p.slots[1].playerTemplate = 0;
	game4p.slots[1].origPlayerTemplate = 0;
	game4p.slots[1].teamNumber = 0;
	game4p.slots[1].origStartPos = -1;
	game4p.slots[1].startPos = 3; // ally random -> spot index 3 shows "2"
	game4p.slots[2].state = SLOT_MED_AI;
	game4p.slots[2].playerTemplate = 0;
	game4p.slots[2].origPlayerTemplate = 0;
	game4p.slots[2].teamNumber = 1;
	game4p.slots[2].origStartPos = 2;
	game4p.slots[2].startPos = 2; // enemy fixed -> spot index 2 shows "3"
	game4p.slots[3].state = SLOT_MED_AI;
	game4p.slots[3].playerTemplate = 0;
	game4p.slots[3].origPlayerTemplate = 0;
	game4p.slots[3].teamNumber = 1;
	game4p.slots[3].origStartPos = -1;
	game4p.slots[3].startPos = 1; // enemy random -> hidden
	fx.loadScreen = makeLoadScreenInfo(game4p, fx.setup, &fx.text, [&](const std::string &n) {
		return fx.skins.images.findImageByName(n) != nullptr;
	}, nullptr, false, 0);
	fx.shell->push("LoadScreen.apt");
	fx.tick(30);
	ls = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(ls != nullptr);
	REQUIRE(ls->startSpotButtons().size() == LEGACY_MAX_SLOTS);
	CHECK(ls->startSpotButtons()[0]->winGetText() == u"1");
	CHECK(ls->startSpotButtons()[1]->winGetText().empty());
	CHECK(ls->startSpotButtons()[2]->winGetText() == u"3");
	CHECK(ls->startSpotButtons()[3]->winGetText() == u"2");

	// Missing map falls back to MissingMap and GUI:Blank without crashing (RW 0x9774C6 / 0x7052F5)
	fx.shell->pop();
	fx.tick(5);
	game.mapName = "maps/non_existent_local56/non_existent_local56.map";
	fx.loadScreen = makeLoadScreenInfo(game, fx.setup, &fx.text, [&](const std::string &n) {
		return fx.skins.images.findImageByName(n) != nullptr;
	}, nullptr, false, 0);
	fx.shell->push("LoadScreen.apt");
	fx.tick(30);
	ls = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(ls != nullptr);
	REQUIRE(ls->currentMap() != nullptr);
	CHECK_FALSE(ls->currentMap()->winGetEnabled());
	CHECK(ls->currentMap()->winGetEnabledImage(0) == "MissingMap");
	for (GameWindow *b : ls->startSpotButtons())
	{
		if (b)
		{
			CHECK(b->winGetText() == u"Blank");
			CHECK(b->winIsHidden());
		}
	}
}
