// OpenBFME unit tests. GPL-3.0.
// Lane ARCHER-1 with the retail data: a GondorArcherHorde (15 archers, RanksToReleaseWhenAttacking = 1 2 3, GoodArcherMissileHordeRangefinder's HordeAttackNugget)
// ordered against a MordorFighterHorde spreads its volley over the orcs (HordeContain::attackTargetNow RW 0x875221, the member pick RW 0x86FA87). SKIPs without
// the games (ROTWK_INSTALL / BFME2_INSTALL).

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <set>
#include <string>
#include <vector>

namespace
{
int archerFactionIndex(const PlayerTemplateStore &store, const std::string &name)
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

Player *archerPlayerAtStart(GameLogic &logic, int start)
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

Object *attackGoal(Object *m)
{
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	const AIStateMachine *sm = ai ? ai->stateMachineOrNull() : nullptr;
	return ai && ai->isAttacking() && sm ? sm->goalObject() : nullptr;
}
} // namespace

TEST_CASE("archer1 retail: a GondorArcherHorde's first volley at a MordorFighterHorde is spread over many orcs, every archer released and none at the same nearest man")
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 2;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = archerFactionIndex(s->world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	logic.combat().setAutoAcquireEnabled(false);
	Player *men = archerPlayerAtStart(logic, 0);
	Player *mordor = archerPlayerAtStart(logic, 1);
	REQUIRE(men != nullptr);
	REQUIRE(mordor != nullptr);
	Object *archers = logic.newObject(logic.things().findTemplate("GondorArcherHorde"), men->getDefaultTeam(), {});
	Object *orcs = logic.newObject(logic.things().findTemplate("MordorFighterHorde"), mordor->getDefaultTeam(), {});
	REQUIRE(archers != nullptr);
	REQUIRE(orcs != nullptr);
	// the clear arena of the cavalry test (test_horde_cavalry_retail.cpp), the orcs 200 in front of the archers
	const float ax = 1600.0f, bx = 1800.0f, ay = 600.0f;
	Coord3D pa{ ax, ay, logic.getGroundHeight(ax, ay) }, pb{ bx, ay, logic.getGroundHeight(bx, ay) };
	archers->setPosition(&pa);
	orcs->setPosition(&pb);
	game.advance(2.0); // the members take their slots and the clips load
	HordeContain *ca = dynamic_cast<HordeContain *>(archers->getContain());
	HordeContain *co = dynamic_cast<HordeContain *>(orcs->getContain());
	REQUIRE(ca != nullptr);
	REQUIRE(co != nullptr);
	const std::vector<Object *> archerList(ca->getContainedItemsList()->begin(), ca->getContainedItemsList()->end());
	const std::vector<Object *> orcList(co->getContainedItemsList()->begin(), co->getContainedItemsList()->end());
	REQUIRE(archerList.size() == 15);
	REQUIRE(orcList.size() >= 10);
	REQUIRE(archers->getAIUpdateInterface()->aiAttackObject(orcs, CMD_FROM_AI));
	int frames = 0;
	while (frames < 600 && ca->attackStats().orders < archerList.size())
	{
		game.advance(0.2);
		++frames;
	}
	MESSAGE("frames " << frames << ", fires " << ca->attackStats().fires << ", orders " << ca->attackStats().orders << ", out of reach " << ca->attackStats().outOfReach);
	CHECK(ca->attackStats().orders >= archerList.size());
	std::set<ObjectID> victims;
	std::set<ObjectID> nearest; // what the old rule (each archer's nearest orc) would have given
	for (Object *a : archerList)
	{
		if (Object *v = attackGoal(a))
		{
			CHECK(v->getContainedBy() == orcs);
			victims.insert(v->getID());
		}
		const Object *best = nullptr;
		float bestD = 0.0f;
		for (const Object *e : orcList)
		{
			const float dx = e->getPosition()->x - a->getPosition()->x, dy = e->getPosition()->y - a->getPosition()->y;
			const float d = dx * dx + dy * dy;
			if (!best || d < bestD)
			{
				best = e;
				bestD = d;
			}
		}
		if (best)
		{
			nearest.insert(best->getID());
		}
	}
	MESSAGE("distinct orcs targeted " << victims.size() << " of " << orcList.size() << " (each archer's nearest orc: " << nearest.size() << " distinct)");
	CHECK(victims.size() >= 6); // 7 with this seed (engine measurement, 2026-10-10)
	CHECK(victims.size() > nearest.size());
}
