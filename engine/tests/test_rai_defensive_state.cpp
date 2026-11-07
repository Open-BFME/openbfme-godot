// OpenBFME unit tests. GPL-3.0.
// Lane RAI-DEF: the DEFENSIVE target's defense size (+ 0x2C) and a SimpleDefense team's recruit origin (+ 0x324 / + 0x328), their hashing and
// their place in the AI snapshot (record / block version 5). RotWK RW 0x97D74F writes the size, RW 0x9B4EF0 reads it and sets the origin.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
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

struct DefenseStateGame
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *human = nullptr;
	Player *computer = nullptr;
	AISkirmishPlayer *ai = nullptr;
};

void loadUdun(starttest::Shared &s, DefenseStateGame &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp tournament udun/map mp tournament udun.map";
	m.game.seed = 1234;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMordor", "FactionMen" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Human" : u"Computer";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: a precondition (the faction exists; its index is the store's)
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	GameLogic &logic = g.game->logic();
	// no advance: the test places its structures and the enemy before the AI's first update, so the DEFENSIVE target is the AI's first
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		if (!g.human && p->getMultiplayerStartIndex() == 0)
		{
			g.human = p;
		}
		if (!g.computer && p->getMultiplayerStartIndex() == 1)
		{
			g.computer = p;
		}
	}
	REQUIRE(g.human != nullptr);
	REQUIRE(g.computer != nullptr);
	for (const auto &grp : logic.skirmishAI().groups())
	{
		auto it = grp->players.find(g.computer->getPlayerIndex());
		if (it != grp->players.end())
		{
			g.ai = it->second.get();
		}
	}
	REQUIRE(g.ai != nullptr);
	REQUIRE(g.ai->store != nullptr);
}

Object *makeBuilding(DefenseStateGame &g, const char *name, float x, float y, float z)
{
	std::string error;
	const Coord3D pos{ x, y, z };
	Object *o = g.game->createObject(name, g.computer->getPlayerIndex(), pos, 0.0f, &error);
	REQUIRE_MESSAGE(o, error);
	o->setPosition(&pos); // createObject snaps z to the ground; the test's exact geometry uses this z for both structures
	return o;
}

Team *enemyTeamOf(GameLogic &logic, const Player &me, const Player &them)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &them && o->getTeam() && me.getRelationship(o->getTeam()) == ENEMIES)
		{
			return o->getTeam();
		}
	}
	return nullptr;
}

AISkirmishPlayer *findAI(GameLogic &logic, int playerIndex)
{
	for (const auto &grp : logic.skirmishAI().groups())
	{
		auto it = grp->players.find(playerIndex);
		if (it != grp->players.end())
		{
			return it->second.get();
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("rai-def defensive state: the target's defense size and a team's recruit origin are hashed and survive the AI snapshot (record / block version 5)")
{
	OPENBFME_REQUIRE_START(s);
	DefenseStateGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	const int player = g.computer->getPlayerIndex();
	AISkirmishPlayer *ai = g.ai;
	const Waypoint *humanStart = logic.terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(humanStart != nullptr);
	const Coord3D aiStart = ai->build.startPosition;
	float ux = humanStart->location.x - aiStart.x, uy = humanStart->location.y - aiStart.y;
	const float ulen = std::sqrt(ux * ux + uy * uy);
	REQUIRE(ulen > 0.0f); // tolerance: a precondition (the two start points differ)
	ux /= ulen;
	uy /= ulen;
	const float px = -uy, py = ux;
	const float farX = aiStart.x + ux * 900.0f, farY = aiStart.y + uy * 900.0f;
	const float z = logic.getGroundHeight(farX, farY);
	for (int i = 0; i < 3; ++i)
	{
		std::string error;
		const Coord3D at{ aiStart.x + px * (float)(i * 40), aiStart.y + py * (float)(i * 40), logic.getGroundHeight(aiStart.x, aiStart.y) };
		Object *unit = g.game->createObject("GondorFighterHorde", g.computer->getPlayerIndex(), at, 0.0f, &error);
		REQUIRE_MESSAGE(unit, error);
	}
	makeBuilding(g, "GondorBarracks", farX, farY, z);
	makeBuilding(g, "GondorBarracks", farX + 100.0f, farY, z);
	if (AIStatCollector *live = logic.skirmishAI().record(g.computer->getPlayerIndex()))
	{
		live->update(logic); // the sampler add is deferred until the object's position is final
	}
	logic.partition().update();
	Team *enemyTeam = enemyTeamOf(logic, *g.computer, *g.human);
	REQUIRE(enemyTeam != nullptr);
	const ThingTemplate *hero = logic.things().findTemplate("MordorWitchKing");
	REQUIRE(hero != nullptr);
	Object *raider = logic.newObject(hero, enemyTeam, ObjectStatusMaskType{});
	REQUIRE(raider != nullptr);
	Coord3D raiderPos{ farX + 50.0f, farY + 10.0f, z };
	raider->setPosition(&raiderPos);
	logic.partition().update();

	int targetIndex = -1;
	for (int f = 0; f < 300 && targetIndex < 0; ++f)
	{
		g.game->advance(0.2);
		for (size_t i = 0; i < ai->brain.targets.size(); ++i)
		{
			if (ai->brain.targets[i].type == 1 && !ai->brain.targets[i].inactive)
			{
				targetIndex = (int)i;
			}
		}
	}
	// tolerance: a precondition (the target exists); the index is the game state, not a rule
	REQUIRE(targetIndex >= 0);
	const AITarget &target = ai->brain.targets[(size_t)targetIndex];
	CHECK(target.defenseSize == 1); // two structures in the group (RW 0x97D74F's count-2 case)
	const Coord3D origin = target.position;
	int tacticIndex = -1;
	for (size_t i = 0; i < ai->brain.tactics.size(); ++i)
	{
		if (ai->brain.tactics[i].kind == "SimpleDefense")
		{
			tacticIndex = (int)i;
		}
	}
	// tolerance: a precondition (the tactic exists)
	REQUIRE(tacticIndex >= 0);
	REQUIRE(ai->brain.tactics[(size_t)tacticIndex].teams.size() == 1u);
	CHECK(ai->brain.tactics[(size_t)tacticIndex].teams[0].hasRecruitOrigin); // RW 0x79FD4D
	CHECK(ai->brain.tactics[(size_t)tacticIndex].teams[0].recruitOrigin.x == target.position.x);
	CHECK(ai->brain.tactics[(size_t)tacticIndex].teams[0].recruitOrigin.y == target.position.y);
	CHECK(ai->brain.tactics[(size_t)tacticIndex].teams[0].recruitOrigin.z == target.position.z);

	// the state hash covers both fields: changing either changes it
	StateHasher before;
	logic.skirmishAI().crc(before);
	const std::uint32_t hashBefore = before.value();
	ai->brain.targets[(size_t)targetIndex].defenseSize = 2;
	StateHasher a;
	logic.skirmishAI().crc(a);
	CHECK(a.value() != hashBefore);
	ai->brain.targets[(size_t)targetIndex].defenseSize = 1;
	ai->brain.tactics[(size_t)tacticIndex].teams[0].recruitOrigin.x += 1.0f;
	StateHasher b;
	logic.skirmishAI().crc(b);
	CHECK(b.value() != hashBefore);
	ai->brain.tactics[(size_t)tacticIndex].teams[0].recruitOrigin.x -= 1.0f;

	// the block round trip: the version is 5 and the fields come back
	std::vector<std::uint8_t> bytes;
	logic.gameState().saveBlocks({ "obfme:SkirmishAI" }, bytes);
	const std::uint32_t stateBefore = logic.computeStateHash();
	logic.gameState().loadBlocks({ "obfme:SkirmishAI" }, bytes);
	CHECK(logic.computeStateHash() == stateBefore);
	ai = findAI(logic, player);
	REQUIRE(ai != nullptr);
	// tolerance: a precondition (the index still addresses the target after the load)
	REQUIRE((size_t)targetIndex < ai->brain.targets.size());
	CHECK(ai->brain.targets[(size_t)targetIndex].defenseSize == 1);
	const AITactic *restored = nullptr;
	for (const AITactic &tac : ai->brain.tactics)
	{
		if (tac.kind == "SimpleDefense")
		{
			restored = &tac;
		}
	}
	REQUIRE(restored != nullptr);
	REQUIRE(restored->teams.size() == 1u);
	CHECK(restored->teams[0].hasRecruitOrigin);
	CHECK(restored->teams[0].recruitOrigin.x == origin.x);
	CHECK(restored->teams[0].recruitOrigin.y == origin.y);
	CHECK(restored->teams[0].recruitOrigin.z == origin.z);
}
