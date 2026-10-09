// OpenBFME retail tests for the economy (lane ECON-1). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise they print SKIP).
// They mount pure RotWK 2.01 + BFME2 1.06, load the object world and the GameData economy values, and run all seven skirmish factions in ONE game:
// seven players (so the multiplayer tables of seven live players apply), each with its own start fortress citadel, its own resource building and one hero,
// far enough apart that no claim overlaps. Every expectation below is derived by hand from the retail INI text (data\ini\gamedata.ini defines and the
// object files named on each line), not from the code under test.
//
// HAND DERIVATION (RotWK 2.01 INI; the logic runs at 5 frames a second, an INI duration in msec becomes ceil(msec * 0.005) frames):
//   * GameData: GoodCommandPointsMP7 = EvilCommandPointsMP7 = "100 575": with seven live playable players every player's base is 100 and the cap 575
//     (RW 0x6A7E11: n >= 7 and < 8 -> slot MP7). MultiPlayMoneyMult MP7 = 1.0. TerrainResourceCellSize = 20.
//   * the fortress citadel of every faction (MenFortressCitadel, ElvenCitadel, DwarvenFortressCitadel, IsengardFortressCitadel, MordorFortressCitadel,
//     WildFortressCitadel, AngmarFortressCitadel): CommandPointBonus = GENERIC_FORTRESS_COMMAND_POINT_BONUS = 100; AutoDepositUpdate DepositTiming =
//     GENERIC_KEEP_MONEY_TIME = 6000 msec = 30 frames, DepositAmount = GENERIC_KEEP_MONEY_AMOUNT = 25; a dead spot TerrainResourceBehavior (MaxIncome 0,
//     Radius GENERIC_KEEP_MONEY_RANGE = 50, HighPriority).
//   * the resource building of every faction (GondorFarm, ElvenMallornTree, DwarvenMineShaft, IsengardFurnace, MordorSlaughterHouse, WildMineShaft,
//     AngmarMill): TerrainResourceBehavior Radius 300, MaxIncome 25, IncomeInterval 6000 msec = 30 frames; CommandPointBonus =
//     GENERIC_ECONOMY_COMMAND_POINT_BONUS = 50. Alone on open ground it holds its whole disc: share 1.0, so each payment is ceil(25 * 1.0) = 25.
//   * the heroes (GondorAragorn 50, ElvenElrond 50, DwarvenDain 50, IsengardSaruman 75, MordorMouthOfSauron 50, WildShelob 50, AngmarKarsh 50 command
//     points, SELECTABLE, not STRUCTURE, not HORDE): each costs its CommandPoints.
//   Command points of a player: limit = min(575, 100 + 100 + 50) = 250; used = the hero's; available = 250 - used.
//   Income: the resource building pays at its first update (frame 1) and every 30 frames: frames 1, 31, ..., 571 are inside 600 frames = 2 minutes: 20
//   payments of 25 = 500 (250 a minute: 10 payments per 300 frames); the citadel pays at frames 30, 60, ..., 600: 20 payments = 500 (250 a minute).
//   A player's cash after 600 frames: 0 + 500 + 500 = 1000, after 300 frames 250 + 250 = 500.

#include "doctest.h"

#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameClient/MapObjectDrawables.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "RetailTestMount.h"


#include <cstdio>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
// a flat world with no blocked cell (EconTestUtil.h cannot be included next to GameClient/LiveGame.h: two ModelConditionFlags typedefs)
class FlatTerrain : public TerrainResourceTerrain
{
public:
	FlatTerrain(float width, float height) : m_w(width), m_h(height) {}
	void getExtent(float &loX, float &loY, float &hiX, float &hiY) const override
	{
		loX = loY = 0.0f;
		hiX = m_w;
		hiY = m_h;
	}
	bool cellTypeAt(float x, float y, int &type) const override
	{
		type = 0;
		return x >= 0 && y >= 0 && x < m_w && y < m_h;
	}

private:
	float m_w, m_h;
};

struct SharedWorld
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

SharedWorld &shared()
{
	static SharedWorld s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

bool haveWorld()
{
	SharedWorld &s = shared();
	if (!s.mount)
	{
		retailtest::printSkip("econ retail");
		return false;
	}
	REQUIRE_MESSAGE(s.mount->fs != nullptr, s.mount->error);
	REQUIRE_MESSAGE(s.world != nullptr, s.error);
	return true;
}

struct Faction
{
	const char *templateName; ///< PlayerTemplate
	const char *player;
	const char *citadel;
	const char *resource;
	const char *hero;
	int heroPoints;
};

const Faction kFactions[7] = {
	{ "FactionMen", "Men", "MenFortressCitadel", "GondorFarm", "GondorAragorn", 50 },
	{ "FactionElves", "Elves", "ElvenCitadel", "ElvenMallornTree", "ElvenElrond", 50 },
	{ "FactionDwarves", "Dwarves", "DwarvenFortressCitadel", "DwarvenMineShaft", "DwarvenDain", 50 },
	{ "FactionIsengard", "Isengard", "IsengardFortressCitadel", "IsengardFurnace", "IsengardSaruman", 75 },
	{ "FactionMordor", "Mordor", "MordorFortressCitadel", "MordorSlaughterHouse", "MordorMouthOfSauron", 50 },
	{ "FactionWild", "Wild", "WildFortressCitadel", "WildMineShaft", "WildShelob", 50 },
	{ "FactionAngmar", "Angmar", "AngmarFortressCitadel", "AngmarMill", "AngmarKarsh", 50 },
};

// the seven-faction game: objects placed 1000 units apart along x (citadel at y 500, resource building at y 2000, hero next to the citadel)
struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	FlatTerrain terrain{ 8000.0f, 3000.0f };
	std::vector<std::string> errors;
	std::vector<Object *> citadels, resources, heroes;

	Game()
		: players(shared().world->nameKeys(), shared().world->playerTemplates(), teams)
	{
		SharedWorld &s = shared();
		SkirmishSetup setup;
		setup.startingMoney = 0;
		for (int i = 0; i < 7; ++i)
		{
			SkirmishPlayer p;
			p.name = kFactions[i].player;
			p.faction = kFactions[i].templateName;
			p.human = i == 0;
			p.team = i; // seven teams: everyone an enemy
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		for (const std::string &e : players.setupSkirmish(setup))
		{
			errors.push_back(e);
		}
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic->castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*s.mount->fs)); // BUILD-1
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic->economy().settings(), &err), err);
		logic->random().seedRandom(7);
		logic->economy().initAllCommandPoints();
		logic->economy().resources().init(terrain, logic->economy().settings().terrainResourceCellSize);
		for (int i = 0; i < 7; ++i)
		{
			Team *team = players.findPlayerWithName(kFactions[i].player)->getDefaultTeam();
			const float x = 500.0f + 1000.0f * (float)i;
			citadels.push_back(place(kFactions[i].citadel, team, x, 500.0f));
			resources.push_back(place(kFactions[i].resource, team, x, 2000.0f));
			heroes.push_back(place(kFactions[i].hero, team, x + 200.0f, 500.0f));
		}
	}
	Object *place(const char *templateName, Team *team, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic->newObject(t, team, ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->friend_onBuildComplete(); // a map object is complete at birth (RW 0x62E176): the claim of a resource building
		return o;
	}
	Player *player(int i) { return players.findPlayerWithName(kFactions[i].player); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic->runLogicFrame();
		}
	}
};
} // namespace

TEST_CASE("econ retail: GameData's economy values of the install")
{
	if (!haveWorld())
	{
		return;
	}
	EconomySettings es;
	std::string err;
	REQUIRE_MESSAGE(EconomySettings::load(*shared().mount->fs, es, &err), err);
	// data\ini\gamedata.ini of RotWK 2.01 (the same text in INI.big and _patch201ini.big)
	CHECK(es.terrainResourceCellSize == 20.0f);
	CHECK(es.goodSolo.start == 100);
	CHECK(es.goodSolo.cap == 150);
	CHECK(es.evilSolo.start == 300);
	CHECK(es.evilSolo.cap == 350);
	CHECK(es.goodBonus == 20);
	CHECK(es.evilBonus == 50);
	CHECK(es.goodAI.start == 600);
	CHECK(es.goodAI.cap == 650);
	const int caps[7] = { 1000, 875, 750, 675, 625, 575, 500 }; // MP2 .. MP8
	for (int i = 0; i < 7; ++i)
	{
		INFO("MP" << i + 2);
		CHECK(es.goodMP[i].start == 100);
		CHECK(es.goodMP[i].cap == caps[i]);
		CHECK(es.evilMP[i].start == 100);
		CHECK(es.evilMP[i].cap == caps[i]);
	}
	for (int i = 0; i < 8; ++i)
	{
		CHECK(es.multiPlayMoneyMult[i] == 1.0f);
	}
	CHECK(es.goodLimit == 300);
	CHECK(es.evilLimit == 600);
	CHECK(es.powerLimit == 60);
}

TEST_CASE("econ retail: seven factions, command points and income per minute derived by hand")
{
	if (!haveWorld())
	{
		return;
	}
	Game g;
	for (const std::string &e : g.errors)
	{
		INFO(e);
		CHECK(false);
	}
	REQUIRE(g.players.getPlayerCount() == 8); // the neutral player and seven factions
	REQUIRE(g.logic->economy().livePlayableCount(false) == 7);
	CHECK(g.logic->economy().isMultiplayerGame());
	CHECK(g.logic->economy().multiPlayMoneyMult(7) == 1.0f);
	for (int i = 0; i < 7; ++i)
	{
		INFO(kFactions[i].player);
		Player *p = g.player(i);
		REQUIRE(p != nullptr);
		CHECK(p->getMoney()->countMoney() == 0);
		// base 100 (MP7), the citadel 100, the resource building 50: 250, far below the cap 575
		CHECK(p->commandPoints().getBase() == 100);
		CHECK(p->commandPoints().getCap() == 575);
		CHECK(p->commandPoints().getBonus() == 150);
		CHECK(p->commandPointLimit() == 250);
		CHECK(p->commandPoints().getUsage() == kFactions[i].heroPoints);
		CHECK(p->commandPointsAvailable() == 250 - kFactions[i].heroPoints);
		// the resource building holds its whole disc: nothing overlaps (1000 units apart, radii 300 and 50)
		const TerrainResourceManager::Claimant *claim = nullptr;
		for (const TerrainResourceManager::Claimant &c : g.logic->economy().resources().claimants())
		{
			if (c.id == g.resources[(size_t)i]->getID())
			{
				claim = &c;
			}
		}
		REQUIRE(claim != nullptr);
		CHECK(claim->radius == 300.0f);
		CHECK(claim->complete);
	}
	// 300 frames = one minute
	g.frames(300);
	for (int i = 0; i < 7; ++i)
	{
		INFO(kFactions[i].player);
		CHECK(g.player(i)->getMoney()->countMoney() == 500); // 10 payments of 25 from the resource building (frames 1 .. 271) and 10 from the citadel (30 .. 300)
	}
	g.frames(300); // minute two
	for (int i = 0; i < 7; ++i)
	{
		INFO(kFactions[i].player);
		CHECK(g.player(i)->getMoney()->countMoney() == 1000);
		CHECK(g.player(i)->getScoreKeeper().moneyEarned() == 1000);
	}
	// the income log: 7 players x (20 + 20) payments of 25
	size_t payments = 0;
	for (const Economy::IncomeEvent &e : g.logic->economy().incomeLog())
	{
		CHECK(e.amount == 25);
		++payments;
	}
	CHECK(payments == 7 * 40);
	std::printf("  info: econ retail: %zu payments of 25 over two minutes, 1000 each\n", payments);
}

TEST_CASE("econ retail: a sixth citadel of one player runs into the command point cap of seven players (575)")
{
	if (!haveWorld())
	{
		return;
	}
	Game g;
	// Men: base 100 + 150 (citadel, farm) = 250; five more citadels add 500 -> 750, capped at 575
	Team *team = g.player(0)->getDefaultTeam();
	for (int i = 0; i < 5; ++i)
	{
		g.place("MenFortressCitadel", team, 500.0f + 1000.0f * (float)i, 2800.0f);
	}
	CHECK(g.player(0)->commandPoints().getBonus() == 650);
	CHECK(g.player(0)->commandPointLimit() == 575);
	CHECK(g.player(0)->commandPointsAvailable() == 575 - 50);
	// the other factions are untouched
	CHECK(g.player(1)->commandPointLimit() == 250);
}

TEST_CASE("econ retail: the same game twice has the same state hash at every frame, and the report names the economy stops")
{
	if (!haveWorld())
	{
		return;
	}
	std::vector<std::uint32_t> a, b;
	GameLogic::Report rep;
	for (int run = 0; run < 2; ++run)
	{
		Game g;
		std::vector<std::uint32_t> &h = run == 0 ? a : b;
		h.push_back(g.logic->computeStateHash());
		for (int i = 0; i < 6; ++i)
		{
			g.frames(50);
			h.push_back(g.logic->computeStateHash());
		}
		if (run == 0)
		{
			rep = g.logic->report();
		}
	}
	CHECK(a == b);
	CHECK(a.front() != a.back());
	for (size_t i = 1; i < a.size(); ++i)
	{
		CHECK(a[i] != a[i - 1]); // the money moves between the samples
	}
	std::string stops;
	for (const std::string &s : rep.stops)
	{
		stops += s.substr(0, 7) + " ";
	}
	for (const char *id : { "[S-250]", "[S-252]", "[S-253]", "[S-254]" })
	{
		INFO(id << " in: " << stops);
		CHECK(stops.find(id) != std::string::npos);
	}
}

TEST_CASE("econ retail: a live map builds the claim grid over its extent and the command points of its players")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	MapObjectOptions options;
	std::string err;
	REQUIRE_MESSAGE(MapObjectGameData::load(*s.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(*s.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapCreationHooks::load(*s.mount->fs, options.creationScripts, &err), err);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 99;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Economy &economy = game.logic().economy();
	const TerrainResourceManager &grid = economy.resources();
	REQUIRE(grid.hasGrid());
	CHECK(grid.cellSize() == 20.0f);
	// the extent of the map: the largest boundary of the file times 10 on each axis (the origin is 0), cells = ceil(extent / 20)
	int maxX = 0, maxY = 0;
	for (const ICoord2D &b : game.map().heightMap.m_boundaries)
	{
		maxX = std::max(maxX, b.x);
		maxY = std::max(maxY, b.y);
	}
	CHECK(grid.width() == (maxX * 10 + 19) / 20);
	CHECK(grid.height() == (maxY * 10 + 19) / 20);
	CHECK(grid.originX() == 0.0f);
	CHECK(grid.originY() == 0.0f);
	// a skirmish map has water, cliffs or both: some cells are blocked, never all
	CHECK(grid.blockedCells() > 0);
	CHECK(grid.blockedCells() < (size_t)grid.width() * (size_t)grid.height());
	// every playable player: base 100 and the cap of the live player count (MP2 1000 .. MP8 500 by the retail table), no command point used by a structure
	int playable = 0;
	for (int i = 0; i < game.players().getPlayerCount(); ++i)
	{
		const Player *p = game.players().getNthPlayer(i);
		playable += p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !p->getPlayerTemplate()->m_isObserver ? 1 : 0;
	}
	CHECK(playable >= 2);
	CHECK(economy.livePlayableCount(false) == playable);
	const int tableCap[9] = { 0, 0, 1000, 875, 750, 675, 625, 575, 500 };
	const int n = playable > 8 ? 8 : playable;
	for (const char *name : { "Player_1", "Player_2" })
	{
		INFO(name << " n=" << n);
		const Player *p = game.players().findPlayerWithName(name);
		REQUIRE(p != nullptr);
		CHECK(p->commandPoints().getBase() == 100);
		CHECK(p->commandPoints().getCap() == tableCap[n]);
		CHECK(p->commandPointLimit() == std::min(tableCap[n], 100 + p->commandPoints().getBonus()));
	}
	const LiveGame::Report rep = game.report();
	for (const std::string &ps : rep.playerSummary)
	{
		std::printf("  info: player %s\n", ps.c_str());
	}
	std::set<std::string> ids;
	for (const std::string &st : rep.stops)
	{
		ids.insert(st.substr(0, 7));
	}
	CHECK(ids.count("[S-250]") == 1);
	std::printf("  info: econ retail map: grid %d x %d cells, %zu blocked, %d playable players, %zu claimants\n", grid.width(), grid.height(), grid.blockedCells(), playable, grid.claimants().size());
}

// The seven factions on a retail skirmish map, as a player places them: each faction's citadel, resource building and hero on free ground of "map mp fall back 4p"
// (the sides of that map with a playable faction: Player_1 = Men by the lobby slot, SkirmishElves / Dwarves / Isengard / Mordor / Wild / Angmar).
// Free ground: every cell of a 31 x 31 block (radius 300 / cell 20 = 15 cells, plus the centre, plus one cell of margin) is free in the claim grid, and the blocks are
// at least 32 cells apart, so each building's disc is whole and overlaps nobody. The derivation of the numbers is at the top of this file; the map's own objects
// owned by those sides are measured first (the baseline), so everything asserted below is a difference.
TEST_CASE("econ retail: the seven factions on a skirmish map earn 250 a minute per resource building and per citadel and gain the command points of their buildings")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	MapObjectOptions options;
	std::string err;
	REQUIRE_MESSAGE(MapObjectGameData::load(*s.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(*s.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapCreationHooks::load(*s.mount->fs, options.creationScripts, &err), err);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 5;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	REQUIRE_MESSAGE(game.load(o, &err), err);
	GameLogic &logic = game.logic();
	Economy &economy = logic.economy();
	const TerrainResourceManager &grid = economy.resources();
	REQUIRE(grid.hasGrid());

	const char *owners[7] = { "Player_1", "SkirmishElves", "SkirmishDwarves", "SkirmishIsengard", "SkirmishMordor", "SkirmishWild", "SkirmishAngmar" };
	int factionOf[7] = { 0, 1, 2, 3, 4, 5, 6 };
	Player *players[7];
	for (int i = 0; i < 7; ++i)
	{
		players[i] = game.players().findPlayerWithName(owners[i]);
		REQUIRE_MESSAGE(players[i] != nullptr, owners[i]);
		REQUIRE(players[i]->getPlayerTemplate() != nullptr);
		INFO(owners[i]);
		CHECK(std::string(players[i]->getPlayerTemplate()->getName()) == kFactions[factionOf[i]].templateName);
	}

	// the free ground: 7 blocks of 31 x 31 free cells for the resource buildings (at least 32 cells apart, so the discs of radius 15 are whole and disjoint), and
	// 7 blocks of 9 x 9 free cells for the citadels (dead spot radius 50 / 20 -> 3 cells), each at least 20 cells (15 + 3 + a margin) from every resource centre and 8 from
	// the other citadels
	struct Block
	{
		int cx, cy;
	};
	auto freeBlock = [&](int cx, int cy, int half) {
		for (int y = cy - half; y <= cy + half; ++y)
		{
			for (int x = cx - half; x <= cx + half; ++x)
			{
				const TerrainResourceManager::Cell *c = grid.cellAt(x, y);
				if (!c || c->state != TerrainResourceManager::CELL_FREE)
				{
					return false;
				}
			}
		}
		return true;
	};
	const int w = grid.width(), h = grid.height();
	std::vector<Block> resourceBlocks, citadelBlocks;
	for (int cy = 16; cy + 16 < h && resourceBlocks.size() < 7; cy += 2)
	{
		for (int cx = 16; cx + 16 < w && resourceBlocks.size() < 7; cx += 2)
		{
			bool taken = false;
			for (const Block &b : resourceBlocks)
			{
				taken = taken || (std::abs(b.cx - cx) < 32 && std::abs(b.cy - cy) < 32);
			}
			if (!taken && freeBlock(cx, cy, 15))
			{
				resourceBlocks.push_back({ cx, cy });
			}
		}
	}
	REQUIRE_MESSAGE(resourceBlocks.size() == 7, "the map has room for " << resourceBlocks.size() << " whole resource discs");
	for (int cy = 6; cy + 6 < h && citadelBlocks.size() < 7; cy += 2)
	{
		for (int cx = 6; cx + 6 < w && citadelBlocks.size() < 7; cx += 2)
		{
			bool near = false;
			for (const Block &b : resourceBlocks)
			{
				near = near || (std::abs(b.cx - cx) < 20 && std::abs(b.cy - cy) < 20);
			}
			for (const Block &b : citadelBlocks)
			{
				near = near || (std::abs(b.cx - cx) < 8 && std::abs(b.cy - cy) < 8);
			}
			if (!near && freeBlock(cx, cy, 4))
			{
				citadelBlocks.push_back({ cx, cy });
			}
		}
	}
	REQUIRE_MESSAGE(citadelBlocks.size() == 7, "the map has room for " << citadelBlocks.size() << " citadels");

	// the baseline: what the map's own objects already give each side
	int baseMoney[7], baseBonus[7], baseUsage[7];
	for (int i = 0; i < 7; ++i)
	{
		baseMoney[i] = (int)players[i]->getMoney()->countMoney();
		baseBonus[i] = players[i]->commandPoints().getBonus();
		baseUsage[i] = players[i]->commandPoints().getUsage();
	}
	// the live playable players (a template with PlayableSide that is not an observer): the seven sides above (SkirmishMen has FactionTutorial, PlayableSide = No;
	// Player_2 .. Player_4 keep the map's FactionCivilian), so the multiplayer tier MP7 applies: base 100, cap 575
	int playable = 0;
	for (int i = 0; i < game.players().getPlayerCount(); ++i)
	{
		const Player *p = game.players().getNthPlayer(i);
		playable += p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !p->getPlayerTemplate()->m_isObserver ? 1 : 0;
	}
	REQUIRE(playable == 7);
	for (int i = 0; i < 7; ++i)
	{
		CHECK(players[i]->commandPoints().getBase() == 100);
		CHECK(players[i]->commandPoints().getCap() == 575);
	}

	std::set<ObjectID> mine;
	auto place = [&](const char *templateName, Player *owner, const Block &b) {
		const ThingTemplate *t = s.world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, templateName);
		Object *obj = logic.newObject(t, owner->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(obj != nullptr, templateName);
		Coord3D p{ (float)b.cx * 20.0f + 10.0f, (float)b.cy * 20.0f + 10.0f, 0.0f }; // the centre of cell (cx, cy): the disc is centred on it
		obj->setPosition(&p);
		obj->friend_onBuildComplete();
		mine.insert(obj->getID());
		return obj;
	};
	for (int i = 0; i < 7; ++i)
	{
		const Faction &f = kFactions[factionOf[i]];
		place(f.citadel, players[i], citadelBlocks[(size_t)i]);
		Object *res = place(f.resource, players[i], resourceBlocks[(size_t)i]);
		place(f.hero, players[i], citadelBlocks[(size_t)i]);
		// the whole disc of the resource building is claimed (share 1.0): the claim holds, and the commands points rose by 100 + 50 and the hero's cost
		const TerrainResourceManager::Claimant *claim = nullptr;
		for (const TerrainResourceManager::Claimant &c : grid.claimants())
		{
			claim = c.id == res->getID() ? &c : claim;
		}
		REQUIRE(claim != nullptr);
		CHECK(claim->radius == 300.0f);
		CHECK(players[i]->commandPoints().getBonus() == baseBonus[i] + 150);
		CHECK(players[i]->commandPoints().getUsage() == baseUsage[i] + f.heroPoints);
		CHECK(players[i]->commandPointLimit() == std::min(575, 100 + baseBonus[i] + 150));
	}
	logic.economy().clearIncomeLog();
	const int start = (int)logic.getFrame();
	for (int i = 0; i < 300; ++i)
	{
		logic.runLogicFrame();
	}
	for (int i = 0; i < 7; ++i)
	{
		INFO(owners[i] << " frame " << logic.getFrame() << " from " << start);
		CHECK((int)players[i]->getMoney()->countMoney() - baseMoney[i] == 500); // minute one: 10 payments of 25 from the citadel and 10 from the resource building
	}
	for (int i = 0; i < 300; ++i)
	{
		logic.runLogicFrame();
	}
	for (int i = 0; i < 7; ++i)
	{
		INFO(owners[i]);
		CHECK((int)players[i]->getMoney()->countMoney() - baseMoney[i] == 1000); // minute two: 1000 in all
	}
	// every payment the economy logged for those sides came from the buildings placed here, 25 each
	size_t fromMine = 0;
	for (const Economy::IncomeEvent &e : economy.incomeLog())
	{
		if (mine.count(e.object))
		{
			CHECK(e.amount == 25);
			++fromMine;
		}
	}
	CHECK(fromMine >= 7 * 40 - 7); // TerrainResourceBehavior logs its payments; AutoDepositUpdate's deposits go through the same call
	std::printf("  info: econ retail map: 7 factions earned exactly 1000 each in two minutes; %zu logged payments of 25\n", fromMine);
}
