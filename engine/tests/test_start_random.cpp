// OpenBFME tests: the start of a skirmish in the logic (lane START-1): the resolution of the lobby's random choices in retail's order, the sides and players built from
// a resolved GameInfo, and the starting units' placement arithmetic.  Synthetic data written here; the golden vectors of the resolution come from an INDEPENDENT model
// (tools/start/model.py, the generator and the algorithm written separately from the C++ out of the RotWK disassembly notes), the unit offsets likewise.  The retail
// end-to-end tests are in test_start_retail.cpp.  GPL-3.0.

#include "doctest.h"

#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/NewGame/SkirmishRandom.h"
#include "GameLogic/NewGame/SkirmishSides.h"
#include "GameLogic/NewGame/StartingBase.h"
#include "GameLogic/Map/SidesList.h"

#include "StartGolden.h"

#include <array>
#include <cfenv>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
// The factions of the golden vectors, in store order: 0 Civilian 1 Neutral 2 Observer 3 Men* 4 Tutorial 5 Elves* 6 Dwarves* 7 Isengard* 8 Mordor* 9 Wild* 10 Angmar* 11 Arnor
// (* = PlayableSide: the candidates 3, 5, 6, 7, 8, 9, 10).
const char kStoreText[] =
	"PlayerTemplate FactionCivilian\n  Side = Civilian\nEnd\n"
	"PlayerTemplate FactionNeutral\n  Side = Neutral\nEnd\n"
	"PlayerTemplate FactionObserver\n  Side = Observer\n  IsObserver = Yes\nEnd\n"
	"PlayerTemplate FactionMen\n  Side = Men\n  PlayableSide = Yes\n  StartingBuilding = MenFortress\nEnd\n"
	"PlayerTemplate FactionTutorial\n  Side = Men\n  StartingBuilding = MenFortress\nEnd\n"
	"PlayerTemplate FactionElves\n  Side = Elves\n  PlayableSide = Yes\n  StartingBuilding = ElvenFortress\nEnd\n"
	"PlayerTemplate FactionDwarves\n  Side = Dwarves\n  PlayableSide = Yes\n  StartingBuilding = DwarvenFortress\nEnd\n"
	"PlayerTemplate FactionIsengard\n  Side = Isengard\n  PlayableSide = Yes\n  StartingBuilding = IsengardFortress\nEnd\n"
	"PlayerTemplate FactionMordor\n  Side = Mordor\n  PlayableSide = Yes\n  StartingBuilding = MordorFortress\nEnd\n"
	"PlayerTemplate FactionWild\n  Side = Wild\n  PlayableSide = Yes\n  StartingBuilding = WildFortress\nEnd\n"
	"PlayerTemplate FactionAngmar\n  Side = Angmar\n  PlayableSide = Yes\n  StartingBuilding = AngmarFortress\nEnd\n"
	"PlayerTemplate FactionArnor\n  Side = Arnor\n  StartingBuilding = ArnorFortress\nEnd\n";

struct Store
{
	NameKeyGenerator keys;
	INIEnvironment env;
	PlayerTemplateStore store;
	Store() : store(keys)
	{
		store.registerBlock(env.blocks);
		INI ini(env);
		const std::string text = kStoreText;
		ini.loadMemory("start.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
	}
};

} // namespace

using namespace startgolden;

TEST_CASE("skirmish random: the resolution equals the independent model for 30 (scenario, seed) pairs: slots and every RNG draw (call log)")
{
	Store st;
	MapCacheEntry map = evendim();
	SkirmishResolveInput in;
	in.templates = &st.store;
	in.numColors = 10;
	in.map = &map;
	REQUIRE(st.store.getPlayerTemplateCount() == 12);
	for (const Golden &g : goldens())
	{
		SkirmishGameInfo info = makeInfo(scenario(g.name));
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.enableCallLog(true);
		rng.initGameLogicRandom(g.seed, -1);
		std::string error;
		INFO(g.name << " seed " << g.seed);
		REQUIRE_MESSAGE(SkirmishRandom::resolve(info, in, rng, &error), error);
		size_t k = 0;
		for (const SkirmishGameSlot &s : info.slots)
		{
			if (!s.isOccupied())
			{
				continue;
			}
			REQUIRE(k < g.slots.size());
			CHECK(s.startPos == g.slots[k][0]);
			CHECK(s.playerTemplate == g.slots[k][1]);
			CHECK(s.color == g.slots[k][2]);
			++k;
		}
		CHECK(k == g.slots.size());
		REQUIRE(rng.callLog().size() == g.draws.size());
		for (size_t i = 0; i < g.draws.size(); ++i)
		{
			CHECK(rng.callLog()[i].lo == g.draws[i][0]);
			CHECK(rng.callLog()[i].hi == g.draws[i][1]);
			CHECK(rng.callLog()[i].result == g.draws[i][2]);
		}
		CHECK(info.unresolvedRandomChoices().empty());
	}
}

TEST_CASE("skirmish random: explicit valid choices draw nothing, the originals are saved, the order is start positions, then faction before colour per slot")
{
	Store st;
	MapCacheEntry map = evendim();
	SkirmishResolveInput in;
	in.templates = &st.store;
	in.numColors = 10;
	in.map = &map;
	// all explicit: no draw, nothing changes
	SkirmishGameInfo info = makeInfo({ { 0, false, 5, 1, 0, 0 }, { 1, false, 6, 2, 4, 1 } });
	GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.enableCallLog(true);
	rng.initGameLogicRandom(99, -1);
	const GameLogicRandom::Seed before = rng.seedArray();
	std::string error;
	REQUIRE_MESSAGE(SkirmishRandom::resolve(info, in, rng, &error), error);
	CHECK(rng.callLog().empty());
	CHECK(rng.seedArray() == before);
	CHECK(info.slots[0].origPlayerTemplate == 5);
	CHECK(info.slots[1].origStartPos == 4);
	CHECK(info.slots[1].origColor == 2);
	// everything random: the start position draw comes first (bounds 0..7), then per slot: the discards (0..1), the faction (0..1000), the colour (0..9)
	SkirmishGameInfo r = makeInfo({ { 0 }, { 1 } });
	GameLogicRandom rng2(RandomAlgorithm::ZH_CarryChain);
	rng2.enableCallLog(true);
	rng2.initGameLogicRandom(7, -1);
	REQUIRE_MESSAGE(SkirmishRandom::resolve(r, in, rng2, &error), error);
	REQUIRE(rng2.callLog().size() >= 5);
	CHECK(rng2.callLog()[0].hi == 7);
	CHECK(rng2.callLog()[0].lo == 0);
	// seed 7: 7 % 7 == 0 discards, so after the position draw come, per slot: the faction draw (0..1000), then the colour draw (0..9)
	size_t i = 1;
	for (int slot = 0; slot < 2; ++slot)
	{
		REQUIRE(i + 1 < rng2.callLog().size());
		CHECK(rng2.callLog()[i].hi == 1000);
		CHECK(rng2.callLog()[i + 1].hi == 9);
		i += 2;
	}
}

TEST_CASE("skirmish random: errors reach the caller (no playable faction, no colour, no free start, an out-of-range position) and nothing is substituted")
{
	Store st;
	MapCacheEntry map = evendim();
	SkirmishResolveInput in;
	in.templates = &st.store;
	in.numColors = 10;
	in.map = &map;
	std::string error;
	{
		SkirmishGameInfo g = makeInfo({ { 0 }, { 1 } });
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		SkirmishResolveInput none = in;
		none.numColors = 0;
		CHECK_FALSE(SkirmishRandom::resolve(g, none, rng, &error));
		CHECK(error.find("no colour") != std::string::npos);
	}
	{
		// a store with no playable side
		NameKeyGenerator keys;
		INIEnvironment env;
		PlayerTemplateStore empty(keys);
		empty.registerBlock(env.blocks);
		INI ini(env);
		const std::string text = "PlayerTemplate FactionNeutral\n  Side = Neutral\nEnd\n";
		ini.loadMemory("e.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		SkirmishResolveInput bad = in;
		bad.templates = &empty;
		SkirmishGameInfo g = makeInfo({ { 0 }, { 1 } });
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		CHECK_FALSE(SkirmishRandom::resolve(g, bad, rng, &error));
		CHECK(error.find("no playable faction") != std::string::npos);
	}
	{
		SkirmishGameInfo g = makeInfo({ { 0, false, 5, 1, 9, -1 } }); // position 9 on an 8 player map, nothing picked yet
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		CHECK_FALSE(SkirmishRandom::resolve(g, in, rng, &error));
		CHECK(error.find("outside") != std::string::npos);
	}
	{
		MapCacheEntry two = evendim();
		two.numPlayers = 2;
		SkirmishResolveInput small = in;
		small.map = &two;
		SkirmishGameInfo g = makeInfo({ { 0, false, 5, 1, 0, -1 }, { 1, false, 5, 2, 1, -1 }, { 2 } });
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		CHECK_FALSE(SkirmishRandom::resolve(g, small, rng, &error));
		CHECK(error.find("no free start position") != std::string::npos);
	}
}

TEST_CASE("skirmish random: with the SAME seed two resolutions agree, a different seed changes the draws (mutation check of the seed path)")
{
	Store st;
	MapCacheEntry map = evendim();
	SkirmishResolveInput in;
	in.templates = &st.store;
	in.numColors = 10;
	in.map = &map;
	auto run = [&](std::uint32_t seed) {
		SkirmishGameInfo g = makeInfo(scenario("four_teams"));
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(seed, -1);
		std::string error;
		REQUIRE_MESSAGE(SkirmishRandom::resolve(g, in, rng, &error), error);
		std::vector<int> v;
		for (const SkirmishGameSlot &s : g.slots)
		{
			v.push_back(s.startPos);
			v.push_back(s.playerTemplate);
			v.push_back(s.color);
		}
		return v;
	};
	CHECK(run(5) == run(5));
	CHECK(run(5) != run(6));
}

TEST_CASE("starting base: the unit offset arithmetic equals the independent model bit for bit")
{
	struct Case
	{
		float ox, oy, oz;
		std::uint32_t x, y, z;
	};
	const Case cases[] = {
		{ 1, 130, 0, 0x44889435u, 0x44ee9914u, 0x420e0000u },
		{ 30, 200, 0, 0x4491544eu, 0x44eaf957u, 0x420e0000u },
		{ 1, 160, 0, 0x448b3b06u, 0x44ebf241u, 0x420e0000u },
		{ -60, 185, 0, 0x44880c6du, 0x44e45848u, 0x420e0000u },
		{ 1, 230, 0, 0x44916ad2u, 0x44e5c236u, 0x420e0000u },
		{ 30, 250, 0, 0x4495bfacu, 0x44e68df7u, 0x420e0000u },
		{ 100, -50, 0, 0x44816b5fu, 0x4503a10eu, 0x420e0000u },
	};
	Coord3D base;
	base.x = 1000.0f;
	base.y = 2000.0f;
	base.z = 35.5f;
	for (const Case &c : cases)
	{
		Coord3D off;
		off.x = c.ox;
		off.y = c.oy;
		off.z = c.oz;
		const Coord3D p = StartingBase::unitPositionFromOffset(base, off);
		std::uint32_t bx, by, bz;
		std::memcpy(&bx, &p.x, 4);
		std::memcpy(&by, &p.y, 4);
		std::memcpy(&bz, &p.z, 4);
		INFO("offset " << c.ox << " " << c.oy);
		CHECK(bx == c.x);
		CHECK(by == c.y);
		CHECK(bz == c.z);
	}
}

namespace
{
SidesList mapSides()
{
	SidesList s;
	s.sidesVersion = 5;
	auto side = [&](const char *name, const char *faction) {
		SidesInfo i;
		i.dict.setAsciiString("playerName", name);
		i.dict.setAsciiString("playerFaction", faction);
		i.dict.setBool("playerIsHuman", true);
		s.sides.push_back(i);
	};
	side("", "FactionNeutral");
	side("PlyrCivilian", "FactionCivilian");
	side("PlyrCreeps", "FactionNeutral");
	side("Player_1", "FactionMen");
	side("Player_2", "FactionMen");
	auto team = [&](const char *name, const char *owner) {
		Dict d;
		d.setAsciiString("teamName", name);
		d.setAsciiString("teamOwner", owner);
		d.setBool("teamIsSingleton", true);
		s.teams.push_back(d);
	};
	team("team", "");
	team("teamPlyrCivilian", "PlyrCivilian");
	team("teamPlyrCreeps", "PlyrCreeps");
	team("teamPlayer_1", "Player_1");
	team("teamPlayer_2", "Player_2");
	return s;
}
std::vector<GameLogicSettings::MultiplayerColorDef> colours()
{
	std::vector<GameLogicSettings::MultiplayerColorDef> c;
	for (int i = 0; i < 10; ++i)
	{
		GameLogicSettings::MultiplayerColorDef d;
		d.name = "Color" + std::to_string(i);
		d.rgb = 0x101010u * (std::uint32_t)(i + 1);
		d.nightRgb = d.rgb + 1;
		c.push_back(d);
	}
	return c;
}
} // namespace

TEST_CASE("skirmish sides: the map's own players go, the civilian and creeps sides stay, slots become Player_<start+1>, observers Observer_<slot+1>, ReplayObserver is last")
{
	Store st;
	SkirmishGameInfo g = makeInfo({ { 0, false, 3, 4, 2, 0 }, { 1, false, 5, 1, 0, 1 }, { 2, false, 6, 2, 5, 0 }, { 3, true, -2, 0, 1, -1 } });
	g.slots[1].state = SLOT_BRUTAL_AI;
	g.slots[2].state = SLOT_MED_AI;
	g.slots[3].state = SLOT_PLAYER;
	g.startingCash = 2500;
	SkirmishSides::Built built;
	std::string error;
	REQUIRE_MESSAGE(SkirmishSides::build(mapSides(), g, 0, colours(), st.store, built, &error), error);
	CHECK(built.slotPlayerNames[0] == "Player_3");
	CHECK(built.slotPlayerNames[1] == "Player_1");
	CHECK(built.slotPlayerNames[2] == "Player_6");
	CHECK(built.slotPlayerNames[3] == "Observer_4");
	CHECK(built.slotPlayerNames[4].empty());
	std::vector<std::string> names;
	for (const SidesInfo &s : built.sides.sides)
	{
		names.push_back(s.dict.getAsciiString("playerName"));
	}
	CHECK(names == std::vector<std::string>{ "", "PlyrCivilian", "PlyrCreeps", "Player_3", "Player_1", "Player_6", "Observer_4", "ReplayObserver" });
	auto side = [&](const std::string &n) -> const Dict & {
		for (const SidesInfo &s : built.sides.sides)
		{
			if (s.dict.getAsciiString("playerName") == n)
			{
				return s.dict;
			}
		}
		FAIL("no side " << n);
		return built.sides.sides[0].dict;
	};
	// relationships: teams 0 / 1 / 0, the observer has team -1; PlyrCreeps is an enemy of all
	CHECK(side("Player_3").getAsciiString("playerAllies") == "Player_6");
	CHECK(side("Player_3").getAsciiString("playerEnemies") == "Player_1 Observer_4 PlyrCreeps");
	CHECK(side("Player_1").getAsciiString("playerAllies").empty());
	CHECK(side("Player_1").getAsciiString("playerEnemies") == "Player_3 Player_6 Observer_4 PlyrCreeps");
	CHECK(side("PlyrCreeps").getAsciiString("playerAllies").empty());
	CHECK(side("PlyrCreeps").getAsciiString("playerEnemies") == "Player_3 Player_1 Player_6 Observer_4");
	CHECK_FALSE(side("PlyrCivilian").getBool("playerIsHuman"));
	// colours, start index, money, faction names, local flag, AI difficulty
	CHECK(side("Player_3").getInt("playerColor") == (std::int32_t)0xFF505050u);
	CHECK(side("Player_3").getInt("playerNightColor") == (std::int32_t)0xFF505051u);
	CHECK(side("Player_3").getInt("multiplayerStartIndex") == 2);
	CHECK(side("Player_3").getInt("playerStartMoney") == 2500);
	CHECK(side("Player_3").getAsciiString("playerFaction") == "FactionMen");
	CHECK(side("Player_3").getBool("multiplayerIsLocal"));
	CHECK_FALSE(side("Observer_4").getBool("multiplayerIsLocal")); // a human whose name is not the local slot's
	CHECK(side("Observer_4").getAsciiString("playerFaction") == "FactionObserver");
	CHECK(side("Player_1").getInt("skirmishDifficulty") == 3);
	CHECK(side("Player_6").getInt("skirmishDifficulty") == 1);
	CHECK_FALSE(side("Player_3").known("skirmishDifficulty"));
	CHECK(side("Player_3").getBool("playerIsSkirmish"));
	CHECK(side("ReplayObserver").getInt("playerColor") == (std::int32_t)0xFF101010u);
	// teams: the civilian and creeps teams stay, the map's players' teams go, every slot side gets its singleton default team
	std::vector<std::string> teams;
	for (const Dict &t : built.sides.teams)
	{
		teams.push_back(t.getAsciiString("teamName"));
	}
	CHECK(teams == std::vector<std::string>{ "team", "teamPlyrCivilian", "teamPlyrCreeps", "teamPlayer_3", "teamPlayer_1", "teamPlayer_6", "teamObserver_4", "teamReplayObserver" });
	// the PlayerList takes them: indices in side order, the local player, money, colours, difficulty
	TeamFactory teamFactory;
	PlayerList players(st.keys, st.store, teamFactory);
	const std::vector<std::string> notes = players.newGame(built.sides, 1500);
	for (const std::string &n : notes)
	{
		INFO(n);
	}
	CHECK(players.getPlayerCount() == 8); // neutral + civilian + creeps + 4 slots + ReplayObserver - the nameless side = 8
	REQUIRE(players.findPlayerWithName("Player_3"));
	CHECK(players.getLocalPlayer() == players.findPlayerWithName("Player_3"));
	CHECK(players.findPlayerWithName("Player_3")->getMoney()->countMoney() == 2500u);
	CHECK(players.findPlayerWithName("Player_1")->getSkirmishDifficulty() == 3);
	CHECK(players.findPlayerWithName("Player_1")->getPlayerNightColor() == 0xFF202021u);
	CHECK(players.findPlayerWithName("Player_3")->getPlayerColor() == 0xFF505050u);
	CHECK(players.findPlayerWithName("Player_3")->getRelationship(players.findPlayerWithName("Player_6")) == ALLIES);
	CHECK(players.findPlayerWithName("Player_3")->getRelationship(players.findPlayerWithName("Player_1")) == ENEMIES);
	CHECK(players.findPlayerWithName("Observer_4")->isObserver());
	CHECK(players.findPlayerWithName("Player_3")->getPlayerDisplayName() == "p0");
}

TEST_CASE("skirmish sides: a slot with an unresolved faction or colour, two slots on one start position and a missing observer template are errors")
{
	Store st;
	std::string error;
	SkirmishSides::Built built;
	{
		SkirmishGameInfo g = makeInfo({ { 0, false, 3, 4, 2, 0 }, { 1, false, -1, 1, 0, 1 } });
		CHECK_FALSE(SkirmishSides::build(mapSides(), g, 0, colours(), st.store, built, &error));
		CHECK(error.find("unresolved faction") != std::string::npos);
	}
	{
		SkirmishGameInfo g = makeInfo({ { 0, false, 3, -1, 2, 0 } });
		CHECK_FALSE(SkirmishSides::build(mapSides(), g, 0, colours(), st.store, built, &error));
		CHECK(error.find("colour") != std::string::npos);
	}
	{
		SkirmishGameInfo g = makeInfo({ { 0, false, 3, 4, 2, 0 }, { 1, false, 5, 1, 2, 1 } });
		CHECK_FALSE(SkirmishSides::build(mapSides(), g, 0, colours(), st.store, built, &error));
		CHECK(error.find("both get the side name") != std::string::npos);
	}
}

TEST_CASE("skirmish sides: the state hash of the players follows the new fields (mutation checks)")
{
	Store st;
	SkirmishGameInfo g = makeInfo({ { 0, false, 3, 4, 2, 0 }, { 1, false, 5, 1, 0, 1 } });
	g.startingCash = 1000;
	auto hashOf = [&](const std::function<void(PlayerList &)> &mutate) {
		SkirmishSides::Built built;
		std::string error;
		REQUIRE_MESSAGE(SkirmishSides::build(mapSides(), g, 0, colours(), st.store, built, &error), error);
		TeamFactory teamFactory;
		PlayerList players(st.keys, st.store, teamFactory);
		players.newGame(built.sides, 1500);
		mutate(players);
		StateHasher h;
		players.crc(h);
		return h.value();
	};
	const std::uint32_t base = hashOf([](PlayerList &) {});
	CHECK(base == hashOf([](PlayerList &) {}));
	CHECK(base != hashOf([](PlayerList &p) { p.findPlayerWithName("Player_3")->setPlayerNightColor(7); }));
	CHECK(base != hashOf([](PlayerList &p) { p.findPlayerWithName("Player_3")->setSkirmishDifficulty(2); }));
	CHECK(base != hashOf([](PlayerList &p) { *p.findPlayerWithName("Player_3")->getMoney() = Money(5); }));
	CHECK(base != hashOf([](PlayerList &p) { p.findPlayerWithName("Player_3")->setPlayerColor(0xFF000001u); }));
}

TEST_CASE("skirmish random: the caller's floating-point environment does not reach the resolution (rounding mode normalised at entry)")
{
	Store st;
	MapCacheEntry map = evendim();
	SkirmishResolveInput in;
	in.templates = &st.store;
	in.numColors = 10;
	in.map = &map;
	auto run = [&](int mode) {
		SkirmishGameInfo g = makeInfo(scenario("four_teams"));
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(5, -1);
		std::fesetround(mode);
		std::string error;
		const bool ok = SkirmishRandom::resolve(g, in, rng, &error);
		std::fesetround(FE_TONEAREST);
		REQUIRE_MESSAGE(ok, error);
		std::vector<std::uint32_t> v;
		for (const SkirmishGameSlot &s : g.slots)
		{
			v.push_back((std::uint32_t)s.startPos);
			v.push_back((std::uint32_t)s.playerTemplate);
			v.push_back((std::uint32_t)s.color);
		}
		v.push_back(rng.randomValue()); // the generator state after the resolution
		return v;
	};
	const std::vector<std::uint32_t> nearest = run(FE_TONEAREST);
	for (int mode : { FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO })
	{
		CHECK(run(mode) == nearest);
	}
}

TEST_CASE("round2 review: tied start distances preserve slots and all seed words across rounding modes")
{
 Store st;
 MapCacheEntry map;
 map.name = "maps/probe/probe.map";
 map.numPlayers = 3;
 map.startPositions[1] = {0, 0, 0};
 map.startPositions[2] = {100000, 0, 0};
 map.startPositions[3] = {100000, 1, 0};
 for (bool prepare : {false, true}) {
  auto run = [&](int mode) {
   SkirmishGameInfo g = makeInfo({{0, false, 5, 0, 0, -1}, {1, false, 5, 1, -1, -1}});
   g.mapName = map.name;
   g.seed = 1;
   GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
   rng.initGameLogicRandom(1, -1);
   std::fesetround(mode);
   std::string error;
   bool ok;
   if (prepare) {
    NewGameMessage message;
    message.game = g;
    GameLogicSettings settings;
    settings.multiplayerColors.resize(2);
    NewGameStart start(RandomAlgorithm::ZH_CarryChain);
    ok = NewGame::prepareNewGame(message, st.store, settings, {map}, RandomAlgorithm::ZH_CarryChain, start, &error);
    g = start.message.game;
    rng = start.random;
   } else {
    SkirmishResolveInput in;
    in.templates = &st.store;
    in.numColors = 2;
    in.map = &map;
    ok = SkirmishRandom::resolve(g, in, rng, &error);
   }
   std::fesetround(FE_TONEAREST);
   REQUIRE_MESSAGE(ok, error);
   std::printf("round2 tie: prepare=%d mode=%d slot1=%d\n", prepare, mode, g.slots[1].startPos);
   std::vector<std::uint32_t> state;
   for (const auto &slot : g.slots) {
    state.push_back((std::uint32_t)slot.startPos);
    state.push_back((std::uint32_t)slot.playerTemplate);
    state.push_back((std::uint32_t)slot.color);
   }
   const auto &seed = rng.seedArray();
   state.insert(state.end(), seed.begin(), seed.end());
   return state;
  };
  const auto nearest = run(FE_TONEAREST);
  CHECK(nearest[3] == 1);
  for (int mode : {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
   INFO("prepare=" << prepare << " rounding=" << mode);
   CHECK(run(mode) == nearest);
  }
 }
}
