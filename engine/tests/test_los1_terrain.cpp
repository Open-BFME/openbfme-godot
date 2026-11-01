// OpenBFME unit tests. GPL-3.0.
// Lane LOS-1: the terrain line of sight of attackers on a retail map (TerrainLogic::isClearLineOfSight RW 0x466289, the line of sight filter RW 0x6616AC,
// the weapon terrain line RW 0x6CC6DD). SKIPs without the games (ROTWK_INSTALL / BFME2_INSTALL).

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatLineOfSight.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/System/ShroudManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace
{
int losFactionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

Player *losPlayerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

// a two-player skirmish (Men at start 0, Mordor at start 1) on a retail map
struct LosGame
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *men = nullptr;
	Player *mordor = nullptr;

	bool load(starttest::Shared &s, const char *map, std::string &error)
	{
		static std::vector<MapCacheEntry> cache;
		if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error))
		{
			return false;
		}
		NewGameMessage m;
		m.game.mapName = map;
		m.game.seed = 2;
		m.game.startingCash = 1500;
		const char *factions[2] = { "FactionMen", "FactionMordor" };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishGameSlot &slot = m.game.slots[i];
			slot.state = SLOT_PLAYER;
			slot.name = i == 0 ? u"Player0" : u"Player1";
			slot.playerTemplate = losFactionIndex(s.world->playerTemplates(), factions[i]);
			slot.startPos = i;
			slot.color = i;
			slot.teamNumber = i;
		}
		if (!NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error))
		{
			return false;
		}
		source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
		assets = std::make_unique<WW3DAssetManager>(*source);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
		LiveGame::Options o;
		o.start = &start;
		if (!game->load(o, &error))
		{
			return false;
		}
		men = losPlayerAtStart(game->logic(), 0);
		mordor = losPlayerAtStart(game->logic(), 1);
		if (men && mordor && game->logic().shroud())
		{
			// the shroud would hide the far side of the hill from both players as well; revealed, only the line of sight decides
			game->logic().shroud()->revealMapForPlayerPermanently(men->getPlayerIndex());
			game->logic().shroud()->revealMapForPlayerPermanently(mordor->getPlayerIndex());
		}
		return men && mordor;
	}
};

const char *const kMap = "maps/map mp tournament rhudaur/map mp tournament rhudaur.map";
// a dry, walkable hill on Tournament Rhudaur (found by the scan below: no cliff cell on or beside the line, no standing water): the ground at x 3950 and x 4110
// (y 1900) is 42.3, the hill between rises to 58
const float kRidgeY = 1900.0f, kRidgeA = 3950.0f, kRidgeB = 4110.0f;
// flat ground near it (the scan's flat lines): x 3950 from y 1600 to y 1760, all at 42.3
const float kPlainX = 3950.0f, kPlainA = 1600.0f, kPlainB = 1760.0f;

Object *placeAt(GameLogic &logic, const char *tmpl, Player *owner, float x, float y)
{
	Object *o = logic.newObject(logic.things().findTemplate(tmpl), owner->getDefaultTeam(), {});
	if (o)
	{
		const Coord3D p{ x, y, logic.getGroundHeight(x, y) };
		o->setPosition(&p);
		testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
	}
	return o;
}

const unsigned kHeroScan = AI::CAN_ATTACK | AI::CAN_SEE | AI::AUTO_ACQUIRE; // the idle scan of an ATTACK_NEEDS_LINE_OF_SIGHT unit (RW 0x6687BB)
} // namespace

TEST_CASE("los1 terrain: the height map walk (RW 0x466289) is blocked by a Tournament Rhudaur hill, clear above it and over flat ground")
{
	OPENBFME_REQUIRE_START(s);
	LosGame g;
	std::string error;
	REQUIRE_MESSAGE(g.load(*s, kMap, error), error);
	GameLogic &logic = g.game->logic();
	const float ha = logic.getGroundHeight(kRidgeA, kRidgeY), hb = logic.getGroundHeight(kRidgeB, kRidgeY);
	// 10 above the ground on each side of the hill (52.3 < 58), both ways
	CHECK_FALSE(CombatLineOfSight::isClearLineOfSightTerrain(logic, Coord3D{ kRidgeA, kRidgeY, ha + 10.0f }, Coord3D{ kRidgeB, kRidgeY, hb + 10.0f }));
	CHECK_FALSE(CombatLineOfSight::isClearLineOfSightTerrain(logic, Coord3D{ kRidgeB, kRidgeY, hb + 10.0f }, Coord3D{ kRidgeA, kRidgeY, ha + 10.0f }));
	// Haldir's line: his geometry top (19.2, RW 0x6CA33E) to the orc's centre height (9.6, RW 0xB4E370) is blocked as well
	CHECK_FALSE(CombatLineOfSight::isClearLineOfSightTerrain(logic, Coord3D{ kRidgeA, kRidgeY, ha + 19.2f }, Coord3D{ kRidgeB, kRidgeY, hb + 9.6f }));
	// 200 above the ground the line passes over the hill top (58)
	CHECK(CombatLineOfSight::isClearLineOfSightTerrain(logic, Coord3D{ kRidgeA, kRidgeY, ha + 200.0f }, Coord3D{ kRidgeB, kRidgeY, hb + 200.0f }));
	const float pa = logic.getGroundHeight(kPlainX, kPlainA), pb = logic.getGroundHeight(kPlainX, kPlainB);
	CHECK(CombatLineOfSight::isClearLineOfSightTerrain(logic, Coord3D{ kPlainX, kPlainA, pa + 20.0f }, Coord3D{ kPlainX, kPlainB, pb + 20.0f }));
	// one step: the walk never reads past its first cell (numpixels 0)
	CHECK(CombatLineOfSight::isClearLineOfSightTerrain(logic, Coord3D{ kRidgeA, kRidgeY, -1000.0f }, Coord3D{ kRidgeA + 1.0f, kRidgeY, -1000.0f }));
}

TEST_CASE("los1 acquire: Haldir does not pick an orc behind a Tournament Rhudaur hill (the line of sight filter RW 0x6616AC), and picks it in plain view")
{
	OPENBFME_REQUIRE_START(s);
	LosGame g;
	std::string error;
	REQUIRE_MESSAGE(g.load(*s, kMap, error), error);
	GameLogic &logic = g.game->logic();
	logic.combat().setAutoAcquireEnabled(false);
	Object *haldir = placeAt(logic, "ElvenHaldir", g.men, kRidgeA, kRidgeY);
	Object *orc = placeAt(logic, "MordorFighter", g.mordor, kRidgeB, kRidgeY);
	REQUIRE(haldir != nullptr);
	REQUIRE(orc != nullptr);
	g.game->advance(1.0);
	AIUpdateInterface *ai = haldir->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	// the orc is in range; without the vision test (the shroud hides it behind the hill too) only the line of sight filter refuses it
	CHECK(AI::findClosestEnemy(*haldir, 400.0f, (kHeroScan & ~AI::CAN_SEE) | AI::NO_VISION_TEST, ai->attackInfo(), nullptr, 0) == orc);
	CHECK(AI::findClosestEnemy(*haldir, 400.0f, kHeroScan | AI::NO_VISION_TEST, ai->attackInfo(), nullptr, 0) == nullptr);
	CHECK_FALSE(CombatLineOfSight::filterAllows(*haldir, *orc));
	// the same pair 200 apart on the flat arena: picked
	Coord3D p{ kPlainX, kPlainA, logic.getGroundHeight(kPlainX, kPlainA) };
	haldir->setPosition(&p);
	p = Coord3D{ kPlainX, kPlainB, logic.getGroundHeight(kPlainX, kPlainB) };
	orc->setPosition(&p);
	g.game->advance(0.4);
	CHECK(CombatLineOfSight::filterAllows(*haldir, *orc));
	CHECK(AI::findClosestEnemy(*haldir, 400.0f, kHeroScan, ai->attackInfo(), nullptr, 0) == orc);
}

TEST_CASE("los1 idle: with auto-acquire on, Haldir and an orc on either side of the hill leave each other alone; on flat ground they fight")
{
	OPENBFME_REQUIRE_START(s);
	LosGame g;
	std::string error;
	REQUIRE_MESSAGE(g.load(*s, kMap, error), error);
	GameLogic &logic = g.game->logic();
	logic.combat().setAutoAcquireEnabled(true);
	// both are ATTACK_NEEDS_LINE_OF_SIGHT: their mood scans (RW 0x66844A) ask the CAN_SEE filter
	Object *haldir = placeAt(logic, "ElvenHaldir", g.men, kRidgeA, kRidgeY);
	Object *orc = placeAt(logic, "MordorFighter", g.mordor, kRidgeB, kRidgeY);
	REQUIRE(haldir != nullptr);
	REQUIRE(orc != nullptr);
	const float haldirFull = haldir->getBodyModule()->getHealth(), orcFull = orc->getBodyModule()->getHealth();
	g.game->advance(10.0);
	CHECK(haldir->getBodyModule()->getHealth() == haldirFull);
	CHECK(orc->getBodyModule()->getHealth() == orcFull);
	CHECK_FALSE(haldir->getAIUpdateInterface()->isAttacking());
	CHECK_FALSE(orc->getAIUpdateInterface()->isAttacking());
	// the same two on flat ground, as far apart: the fight starts
	Object *haldir2 = placeAt(logic, "ElvenHaldir", g.men, kPlainX, kPlainA);
	Object *orc2 = placeAt(logic, "MordorFighter", g.mordor, kPlainX, kPlainB);
	REQUIRE(haldir2 != nullptr);
	REQUIRE(orc2 != nullptr);
	g.game->advance(10.0);
	CHECK((haldir2->getAIUpdateInterface()->isAttacking() || orc2->getAIUpdateInterface()->isAttacking()));
}

TEST_CASE("los1 attack: Haldir ordered at an orc behind the hill shoots only once his bow sees it (the weapon terrain line RW 0x6CC6DD at the first hit)")
{
	OPENBFME_REQUIRE_START(s);
	LosGame g;
	std::string error;
	REQUIRE_MESSAGE(g.load(*s, kMap, error), error);
	GameLogic &logic = g.game->logic();
	logic.combat().setAutoAcquireEnabled(false);
	Object *haldir = placeAt(logic, "ElvenHaldir", g.men, kRidgeA, kRidgeY);
	Object *orc = placeAt(logic, "MordorFighter", g.mordor, kRidgeB, kRidgeY);
	REQUIRE(haldir != nullptr);
	REQUIRE(orc != nullptr);
	g.game->advance(1.0);
	const Coord3D start = *haldir->getPosition();
	ObjectWeapons *w = haldir->getWeapons();
	REQUIRE(w != nullptr);
	REQUIRE(w->currentWeapon() != nullptr);
	CHECK_FALSE(CombatLineOfSight::isClearGoalFiringLineOfSightTerrain(*w->currentWeapon(), *haldir, start, orc));
	REQUIRE(haldir->getAIUpdateInterface()->aiAttackObject(orc, CMD_FROM_PLAYER));
	const float full = orc->getBodyModule()->getHealth();
	Coord3D shotFrom = start;
	bool hit = false;
	for (int f = 0; f < 5 * 60 && !hit; ++f) // a minute of game time
	{
		g.game->advance(0.2);
		if (orc->getBodyModule()->getHealth() < full)
		{
			hit = true;
			shotFrom = *haldir->getPosition();
		}
	}
	REQUIRE(hit);
	const float dx = shotFrom.x - start.x, dy = shotFrom.y - start.y;
	CHECK(dx * dx + dy * dy > 400.0f); // tolerance: he walked at least 20 toward a firing spot (the exact spot is the attack path's, not this lane's)
	CHECK(CombatLineOfSight::isClearGoalFiringLineOfSightTerrain(*w->currentWeapon(), *haldir, shotFrom, orc));
}

TEST_CASE("los1 over walls: an IMMOBILE CAN_SHOOT_OVER_WALLS tower has no terrain line (RW 0x6F4589 drops its weapon), so the hill does not hide Haldir from it")
{
	OPENBFME_REQUIRE_START(s);
	LosGame g;
	std::string error;
	REQUIRE_MESSAGE(g.load(*s, kMap, error), error);
	GameLogic &logic = g.game->logic();
	logic.combat().setAutoAcquireEnabled(false);
	Object *tower = placeAt(logic, "MordorBattleTower", g.mordor, kRidgeA, kRidgeY);
	Object *haldir = placeAt(logic, "ElvenHaldir", g.men, kRidgeB, kRidgeY);
	REQUIRE(tower != nullptr);
	REQUIRE(haldir != nullptr);
	g.game->advance(1.0);
	REQUIRE(tower->getWeapons() != nullptr);
	REQUIRE(tower->getWeapons()->currentWeapon() != nullptr);
	// the filter: a current weapon skips the filter's own terrain test, and RW 0x6F4557 asks no weapon of an IMMOBILE attacker: allowed
	CHECK(CombatLineOfSight::filterAllows(*tower, *haldir));
}


// the tool that found the hill and the flat ground above (skipped; `-tc="los1 scan" --no-skip` lists the candidate lines of kMap: no cliff cell on or beside the
// line, no standing water, a peak 15 above both ends that blocks Haldir's line, and the flat lines near it)
TEST_CASE("los1 scan" * doctest::skip(true))
{
	OPENBFME_REQUIRE_START(s);
	const char *maps[] = { kMap };
	for (const char *map : maps)
	{
		LosGame g;
		std::string error;
		REQUIRE_MESSAGE(g.load(*s, map, error), error);
		GameLogic &logic = g.game->logic();
		const PathfindTerrain *tv = logic.aiWorld()->pathfinder().terrainView();
		float loX, loY, hiX, hiY;
		tv->getExtent(loX, loY, hiX, hiY);
		std::printf("map %s extent %f %f\n", map, hiX, hiY);
		int found = 0;
		for (int axis = 0; axis < 2; ++axis)
		{
			for (float y = loY + 300.0f; y < hiY - 300.0f && found < 40; y += 25.0f)
			{
				for (float x = loX + 300.0f; x < hiX - 300.0f && found < 40; x += 25.0f)
				{
					const float d = 160.0f;
					const float ux = axis == 0 ? 1.0f : 0.0f, uy = axis == 0 ? 0.0f : 1.0f;
					const float bx = x + ux * d, by = y + uy * d;
					const float ha = logic.getGroundHeight(x, y), hb = logic.getGroundHeight(bx, by);
					float wz = 0.0f;
					if (ha - hb > 10.0f || hb - ha > 10.0f || logic.terrain()->getStandingWaterHeight(x, y, wz) || logic.terrain()->getStandingWaterHeight(bx, by, wz))
					{
						continue;
					}
					float peak = -1e9f;
					bool bad = false;
					for (float u = -20.0f; u <= d + 20.0f && !bad; u += 5.0f)
					{
						const float px = x + ux * u, py = y + uy * u;
						peak = std::max(peak, logic.getGroundHeight(px, py));
						bad = tv->isCliffCell(px, py) || tv->isCliffCell(px + uy * 10.0f, py + ux * 10.0f) || tv->isCliffCell(px - uy * 10.0f, py - ux * 10.0f) ||
						      logic.terrain()->getStandingWaterHeight(px, py, wz);
					}
					if (!bad && peak <= std::max(ha, hb) + 0.5f && axis == 1 && x > 3700.0f && x < 4300.0f && y > 1500.0f && y < 2300.0f)
					{
						std::printf("flat axis %d %f %f -> %f %f h %f %f\n", axis, x, y, bx, by, ha, hb);
					}
					if (bad || peak < std::max(ha, hb) + 15.0f)
					{
						continue;
					}
					if (logic.terrain()->isClearLineOfSight(Coord3D{ x, y, ha + 19.2f }, Coord3D{ bx, by, hb + 9.6f }, nullptr))
					{
						continue;
					}
					std::printf("ridge axis %d %f %f -> %f %f h %f %f peak %f\n", axis, x, y, bx, by, ha, hb, peak);
					++found;
				}
			}
		}
	}
}
