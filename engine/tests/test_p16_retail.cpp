// OpenBFME. GPL-3.0. P16 extension tests; the retail faction data is mounted at runtime.
#include "doctest.h"
#include "P16TestUtil.h"

#include "Common/PlayerList.h"
#include "Common/ExtendedPlayerColors.h"
#include "Common/BuildAssistant.h"
#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameLogic/NewGame/SkirmishSides.h"
#include "GameLogic/NewGame/SkirmishRandom.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/VictoryConditions.h"

#include <set>

TEST_CASE("p16 fixture: generated map has sixteen canonical starts and no parse errors")
{
	LoadedMap map;
	std::string error;
	MapReadOptions options;
	REQUIRE_MESSAGE(MapReader::load(p16test::mapBytes(), "p16-generated", options, map, &error), error);
	REQUIRE(map.hasHeightMap);
	CHECK(map.sides.sides.size() == 3);
	CHECK(map.chunks.objects.size() == 80);
	std::set<std::string> names;
	int farms = 0;
	for (const MapObject &object : map.chunks.objects)
	{
		if (object.isWaypoint()) names.insert(object.getWaypointName());
		else
		{
			CHECK(object.m_objectName == "FarmTemplate");
			++farms;
		}
	}
	CHECK(farms == 64);
	for (int i = 1; i <= 16; ++i) CHECK(names.count("Player_" + std::to_string(i) + "_Start") == 1);
}

TEST_CASE("p16 retail: actual Evendim sides plus sixteen slots fit all twenty-player structures")
{
	P16_REQUIRE_START(s);
	std::vector<std::uint8_t> bytes;
	LoadedMap map;
	std::string error;
	REQUIRE_MESSAGE(s->mount->fs->readFile("maps/map mp evendim/map mp evendim.map", bytes, &error), error);
	MapReadOptions options;
	REQUIRE_MESSAGE(MapReader::load(bytes, "Evendim", options, map, &error), error);
	SkirmishSides::Built built;
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(p16test::message(*s), s->world->setupPlayerTemplates(), s->settings,
		{ p16test::cacheEntry() }, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	const NewGameMessage &message = start.message;
	REQUIRE_MESSAGE(SkirmishSides::build(map.sides, message.game, -1, s->settings.multiplayerColors, s->world->playerTemplates(), built, &error), error);
	TeamFactory teams;
	PlayerList players(s->world->nameKeys(), s->world->playerTemplates(), teams);
	const auto errors = players.newGame(built.sides, 1500);
	REQUIRE(errors.empty());
	REQUIRE(players.getPlayerCount() == 20);
	CHECK(players.getNeutralPlayer()->getPlayerIndex() == 0);
	CHECK(players.findPlayerWithName("PlyrCivilian"));
	CHECK(players.findPlayerWithName("PlyrCreeps"));
	REQUIRE(players.findPlayerWithName("ReplayObserver"));
	CHECK(players.findPlayerWithName("ReplayObserver")->getPlayerIndex() == 19);
	constexpr int maxShroudPlayers = ShroudManager::kMaxPlayers;
	CHECK(players.getPlayerCount() <= maxShroudPlayers); // tolerance: the shroud's fixed player storage must hold this full setup.
	CHECK(players.getPlayerCount() <= ScoreKeeper::MAX_PLAYERS); // tolerance: the scorekeeper's fixed arrays must hold this full setup.
	CHECK(players.getPlayerCount() <= VictoryConditions::MAX_PLAYER_COUNT); // tolerance: victory tracking must hold this full setup.
	for (int i = 1; i <= 16; ++i) CHECK(players.findPlayerWithName("Player_" + std::to_string(i)));
	map.sides.sides.push_back(*std::find_if(map.sides.sides.begin(), map.sides.sides.end(), [](const SidesInfo &side) {
		return side.dict.getAsciiString("playerName") == "PlyrCivilian";
	}));
	CHECK_FALSE(SkirmishSides::build(map.sides, message.game, -1, s->settings.multiplayerColors, s->world->playerTemplates(), built, &error));
}

TEST_CASE("p16 retail: human AI mixes all difficulties exact starts alliances and independent high-player shroud")
{
	P16_REQUIRE_START(s);
	for (int humans : { 0, 8, 16 })
	{
		INFO("human slots " << humans);
		p16test::Game game;
		std::string error;
		REQUIRE_MESSAGE(game.load(*s, p16test::message(*s, humans), &error), error);
		LiveGame &live = *game.live;
		const auto report = live.report();
		REQUIRE(report.startErrors.empty());
		REQUIRE(live.players().getPlayerCount() == 20);
		REQUIRE(live.logic().skirmishAI().infoPlayers().size() == 16);
		int aiCount = 0;
		std::set<int> colors;
		for (int i = 0; i < 16; ++i)
		{
			Player *player = live.players().findPlayerWithName("Player_" + std::to_string(i + 1));
			REQUIRE(player);
			CHECK(player->getMultiplayerStartIndex() == i);
			const int color = game.start.message.game.slots[i].color;
			REQUIRE(colors.insert(color).second);
			const auto *extra = ExtendedPlayerColors::find(color, (int)s->settings.multiplayerColors.size(), 16);
			CHECK(player->getPlayerColor() == (0xff000000u | (extra ? extra->rgb : s->settings.multiplayerColors[(size_t)color].rgb)));
			const AISkirmishPlayer *ai = live.logic().skirmishAI().findAI(player->getPlayerIndex());
			if (i >= humans)
			{
				REQUIRE(ai);
				CHECK_FALSE(ai->disabled);
				REQUIRE_FALSE(ai->build.bases.empty());
				REQUIRE(ai->build.bases.front().tmpl);
				CHECK(player->getSkirmishDifficulty() == i % 4);
				++aiCount;
			}
			else CHECK(ai == nullptr);
			bool baseAtStart = false;
			const Coord3D expected = p16test::position(i);
			for (const auto &placed : report.startingObjects)
			{
				const Object *object = live.logic().findObjectByID(placed.id);
				if (placed.structure && object && object->getControllingPlayer() == player)
				{
					CHECK(placed.position.x == expected.x);
					CHECK(placed.position.y == expected.y);
					baseAtStart = true;
				}
			}
			CHECK(baseAtStart);
			CHECK(player->getScoreKeeper().structuresAlive() > 0); // tolerance: any positive count proves the placed base is counted; exact composition is not under test.
			for (int j = 0; j < 16; ++j)
			{
				Player *other = live.players().findPlayerWithName("Player_" + std::to_string(j + 1));
				CHECK(player->getRelationship(other) == (i / 2 == j / 2 ? ALLIES : ENEMIES));
			}
		}
		CHECK(aiCount == 16 - humans);
		CHECK(colors.size() == 16);
		CHECK(s->settings.multiplayerColors.size() == 10);
		Player *last = live.players().findPlayerWithName("Player_16");
		Player *enemy = live.players().findPlayerWithName("Player_1");
		REQUIRE(last);
		REQUIRE(enemy);
		ShroudManager *shroud = live.logic().shroud();
		REQUIRE(shroud);
		const auto before = shroud->getStatusAt(enemy->getPlayerIndex(), 4900, 4900);
		shroud->revealMapForPlayerPermanently(last->getPlayerIndex());
		CHECK(shroud->getStatusAt(last->getPlayerIndex(), 4900, 4900) == CELLSHROUD_CLEAR);
		CHECK(shroud->getStatusAt(enemy->getPlayerIndex(), 4900, 4900) == before);
	}
}

TEST_CASE("p16 retail: high slots surrender through dispatch and the surviving alliance wins")
{
	P16_REQUIRE_START(s);
	p16test::Game game;
	std::string error;
	REQUIRE_MESSAGE(game.load(*s, p16test::message(*s, 16), &error), error);
	LiveGame &live = *game.live;
	for (int i = 3; i <= 16; ++i)
	{
		Player *player = live.players().findPlayerWithName("Player_" + std::to_string(i));
		REQUIRE(player);
		GameMessage surrender(MSG_SELF_DESTRUCT, player->getPlayerIndex());
		surrender.appendBooleanArgument(false);
		live.commands().append(surrender);
	}
	for (int i = 0; i < 30; ++i) REQUIRE(live.advance(0.2) == 1);
	for (int i = 1; i <= 16; ++i)
	{
		const Player *player = live.players().findPlayerWithName("Player_" + std::to_string(i));
		REQUIRE(player);
		const bool survivingAlliance = i <= 2;
		CHECK(player->isDefeated() == !survivingAlliance);
		CHECK(live.logic().victory().hasAchievedVictory(player) == survivingAlliance);
	}
	CHECK(live.logic().victory().selfDestructs() == 14);
}

TEST_CASE("p16 retail: an exhausted palette refuses rather than retrying forever")
{
	P16_REQUIRE_START(s);
	SkirmishGameInfo game = p16test::message(*s).game;
	for (int i = 0; i < 10; ++i) game.slots[i].color = i;
	SkirmishResolveInput input;
	input.templates = &s->world->setupPlayerTemplates();
	input.numColors = 10;
	GameLogicRandom random(RandomAlgorithm::ZH_CarryChain);
	random.initGameLogicRandom(game.seed, -1);
	std::string error;
	CHECK_FALSE(SkirmishRandom::populateRandomSideAndColor(game, input, random, &error));
	CHECK(error.find("no unused multiplayer colour") != std::string::npos);
}

TEST_CASE("p16 retail: extended lobby colors share the runtime palette without mutating retail settings")
{
	P16_REQUIRE_START(s);
	class Source : public WorldSkirmishSetupSource
	{
	public:
		std::vector<MapCacheEntry> entries{ p16test::cacheEntry() };
		const std::vector<MapCacheEntry> &maps() const override { return entries; }
	} source;
	std::string error;
	REQUIRE_MESSAGE(source.load(*s->world, s->settings, *s->mount->fs, &error), error);
	SkirmishGameSetup setup(source, nullptr);
	REQUIRE_MESSAGE(setup.init(u"P16", 1, p16test::mapKey, &error), error);
	const auto entries = setup.colorEntries(0);
	REQUIRE(entries.size() == 17);
	for (int c = 10; c < 16; ++c)
	{
		const auto *extra = ExtendedPlayerColors::find(c, 10, 16);
		REQUIRE(extra);
		CHECK(entries[(size_t)c + 1].itemData == c);
		CHECK(entries[(size_t)c + 1].color == (0xff000000u | extra->rgb));
	}
	REQUIRE(setup.setSlotColor(15, 15));
	CHECK_FALSE(setup.setSlotColor(0, 15));
	CHECK(setup.colorEntries(0).size() == 16);
	CHECK(source.colors().size() == 10);
	CHECK(s->settings.multiplayerColors.size() == 10);
	source.entries.front().numPlayers = 8;
	REQUIRE(setup.setMap(p16test::mapKey));
	CHECK(setup.colorEntries(0).size() == 11);
}

TEST_CASE("p16 retail: extended production explicitly uses MP8 and refuses an incomplete table")
{
	P16_REQUIRE_START(s);
	p16test::Game game;
	std::string error;
	REQUIRE_MESSAGE(game.load(*s, p16test::message(*s), &error), error);
	GameLogic &logic = game.live->logic();
	ProductionSettings settings = logic.productionSettings();
	REQUIRE(settings.multiPlayEntries == 8);
	settings.multiPlayUnitSpeedMult[7] = 2.0f;
	settings.multiPlayBuildingSpeedMult[7] = 3.0f;
	const ThingTemplate *unit = logic.things().findTemplate("GondorFighter");
	const ThingTemplate *structure = logic.things().findTemplate("MenFortress");
	REQUIRE(unit);
	REQUIRE(structure);
	CHECK(BuildAssistant::calcTimeToBuild(*unit, nullptr, nullptr, 10, settings, logic) == 100);
	CHECK(BuildAssistant::calcTimeToBuild(*structure, nullptr, nullptr, 10, settings, logic) == 150);
	settings.multiPlayEntries = 7;
	CHECK_THROWS_AS(BuildAssistant::calcTimeToBuild(*unit, nullptr, nullptr, 10, settings, logic), std::logic_error);
}

TEST_CASE("p16 retail: public startup rejects unknown states and invalid explicit starts without resolving them")
{
	P16_REQUIRE_START(s);
	const auto original = p16test::message(*s);
	const auto map = p16test::cacheEntry();
	auto refuses = [&](const NewGameMessage &message, const char *reason) {
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		std::string error;
		CHECK_FALSE(NewGame::prepareNewGame(message, s->world->setupPlayerTemplates(), s->settings,
			{ map }, RandomAlgorithm::ZH_CarryChain, start, &error));
		CHECK(error.find(reason) != std::string::npos);
	};
	for (int pos : { -2, 16 })
	{
		auto message = original;
		message.game.slots[15].startPos = pos;
		refuses(message, "invalid explicit start position");
	}
	auto message = original;
	message.game.slots[15].state = static_cast<SlotState>(7);
	refuses(message, "unknown slot state");
	message = original;
	message.game.slots[15].startPos = message.game.slots[0].startPos;
	refuses(message, "duplicate explicit start position");
}

TEST_CASE("p16 retail: every lobby slot setter respects the map capacity")
{
	P16_REQUIRE_START(s);
	class Source : public WorldSkirmishSetupSource
	{
	public:
		std::vector<MapCacheEntry> entries{ p16test::cacheEntry() };
		const std::vector<MapCacheEntry> &maps() const override { return entries; }
	} source;
	std::string error;
	REQUIRE_MESSAGE(source.load(*s->world, s->settings, *s->mount->fs, &error), error);
	SkirmishGameSetup setup(source, nullptr);
	REQUIRE_MESSAGE(setup.init(u"P16", 1, p16test::mapKey, &error), error);
	for (int count : { 8, 9, 15, 16 })
	{
		INFO(count);
		source.entries.front().numPlayers = count;
		REQUIRE(setup.setMap(p16test::mapKey));
		CHECK(setup.setSlotState(count - 1, SLOT_BRUTAL_AI, u""));
		CHECK(setup.setSlotTemplate(count - 1, 0));
		CHECK(setup.setSlotTeam(count - 1, 0));
		CHECK(setup.setSlotColor(count - 1, -1));
		CHECK(setup.setSlotCreateAHero(count - 1, nullptr, &error));
		CHECK(setup.setSlotStartPos(count - 1, count - 1));
		CHECK_FALSE(setup.setSlotState(count, SLOT_BRUTAL_AI, u""));
		CHECK_FALSE(setup.setSlotTemplate(count, 0));
		CHECK_FALSE(setup.setSlotTeam(count, 0));
		CHECK_FALSE(setup.setSlotColor(count, -1));
		CHECK_FALSE(setup.setSlotCreateAHero(count, nullptr, &error));
		CHECK_FALSE(setup.setSlotStartPos(count, 0));
		CHECK_FALSE(setup.setSlotState(1, static_cast<SlotState>(7), u""));
		CHECK_FALSE(setup.setSlotColor(0, count == 8 ? 10 : 16));
		CHECK_FALSE(setup.setSlotStartPos(0, count));
		CHECK_FALSE(setup.setSlotStartPos(0, -2));
	}
}
