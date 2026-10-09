// OpenBFME unit tests. GPL-3.0.
// Lane AI-2 (review r2): a cavalry horde (HorseHordeContain, e.g. GondorKnightHorde) runs the horde melee machine through its contain interface like an
// infantry horde. RW starts the member combat through the contain interface (Wait RW 0x74697A / 0x74ACAD -> contain + 0x134 = RW 0x86D75E -> Amoeba
// onNewTarget RW 0x99006A), never by the module's class name; the port looked the contain up by the literal "HordeContain", so HorseHordeContain hordes
// skipped the formation freeze and the member orders and stood next to their enemy forever (the AI-vs-AI stall of S-420).

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
int cavalryFactionIndex(const PlayerTemplateStore &store, const std::string &name)
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

const Player *cavalryPlayerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

struct CavalryFight
{
	bool knightFroze = false;          // the knight horde's contain entered the melee freeze
	unsigned long long knightOrders = 0, knightSteps = 0;
	unsigned long long damage = 0;
	bool knightAlive = true, infantryAlive = true;
	float worstStraggle = 0.0f;        // the largest 2D distance of a living knight from its horde, sampled every frame
	int frames = 0;
};

void fightKnightsAgainstOrcs(starttest::Shared &s, CavalryFight &out)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
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
		slot.playerTemplate = cavalryFactionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	logic.combat().setAutoAcquireEnabled(false);
	Player *men = const_cast<Player *>(cavalryPlayerAtStart(logic, 0));
	Player *mordor = const_cast<Player *>(cavalryPlayerAtStart(logic, 1));
	REQUIRE(men != nullptr);
	REQUIRE(mordor != nullptr);
	Object *knights = logic.newObject(logic.things().findTemplate("GondorKnightHorde"), men->getDefaultTeam(), {});
	Object *orcs = logic.newObject(logic.things().findTemplate("MordorFighterHorde"), mordor->getDefaultTeam(), {});
	REQUIRE(knights != nullptr);
	REQUIRE(orcs != nullptr);
	// the arena: clear ground (lane AI-2 r6). The first arena, (1200, 1200) .. (1317, 1200), lies on Evendim's CLIFF cells; HORDE-2's Amoeba step tests every
	// neighbour cell with RW 0x6F1C90 (validMovementPosition), which rejects a cliff for a ground locomotor, so no member could take a step and the fight never closed
	const float ax = 1600.0f, bx = 1717.0f, ay = 600.0f;
	{
		Pathfinder &pf = logic.aiWorld()->pathfinder();
		for (int cx = (int)(ax / 10.0f) - 20; cx <= (int)(bx / 10.0f) + 20; ++cx)
		{
			for (int cy = (int)(ay / 10.0f) - 15; cy <= (int)(ay / 10.0f) + 15; ++cy)
			{
				const PathfindCell *c = pf.getCell(LAYER_GROUND, cx, cy);
				REQUIRE(c != nullptr);
				REQUIRE_MESSAGE((c->getType() == PathfindCell::CELL_CLEAR && !c->getImpassableToPlayers()), "arena cell " << cx << "," << cy);
			}
		}
	}
	Coord3D pa{ ax, ay, logic.getGroundHeight(ax, ay) }, pb{ bx, ay, logic.getGroundHeight(bx, ay) };
	knights->setPosition(&pa);
	orcs->setPosition(&pb);
	const ObjectID kid = knights->getID(), oid = orcs->getID();
	game.advance(0.2);
	HordeAIUpdate *hk = dynamic_cast<HordeAIUpdate *>(knights->getAIUpdateInterface());
	HordeAIUpdate *ho = dynamic_cast<HordeAIUpdate *>(orcs->getAIUpdateInterface());
	HordeContain *ck = dynamic_cast<HordeContain *>(knights->getContain());
	REQUIRE(hk != nullptr);
	REQUIRE(ho != nullptr);
	REQUIRE(ck != nullptr);
	CHECK(knights->findModule("HorseHordeContain") != nullptr); // the retail template's contain class
	REQUIRE(hk->aiAttackObject(orcs, CMD_FROM_AI));
	REQUIRE(ho->aiAttackObject(knights, CMD_FROM_AI));
	for (int f = 0; f < 600 && logic.findObjectByID(kid) && logic.findObjectByID(oid); ++f)
	{
		game.advance(0.2);
		++out.frames;
		Object *k = logic.findObjectByID(kid);
		if (!k)
		{
			break;
		}
		out.knightFroze = out.knightFroze || ck->meleeEngaged();
		out.knightOrders = hk->meleeStats().orders;
		out.knightSteps = hk->meleeStats().steps;
		if (const ContainModuleInterface::ContainedItemsList *items = k->getContain()->getContainedItemsList())
		{
			for (const Object *x : *items)
			{
				if (x->isEffectivelyDead())
				{
					continue;
				}
				const float dx = x->getPosition()->x - k->getPosition()->x, dy = x->getPosition()->y - k->getPosition()->y;
				out.worstStraggle = std::max(out.worstStraggle, std::sqrt(dx * dx + dy * dy));
			}
		}
	}
	out.damage = logic.combat().counters().damageApplications;
	out.knightAlive = logic.findObjectByID(kid) != nullptr;
	out.infantryAlive = logic.findObjectByID(oid) != nullptr;
}
} // namespace

TEST_CASE("horde cavalry retail: a GondorKnightHorde (HorseHordeContain) engages a MordorFighterHorde 117 apart and the fight ends with a dead horde")
{
	OPENBFME_REQUIRE_START(s);
	CavalryFight f;
	fightKnightsAgainstOrcs(*s, f);
	MESSAGE("frames " << f.frames << ", knight freeze " << f.knightFroze << ", knight member orders " << f.knightOrders << ", steps " << f.knightSteps << ", damage "
					  << f.damage << ", alive knights / orcs " << f.knightAlive << " / " << f.infantryAlive << ", worst knight straggle " << f.worstStraggle);
	CHECK(f.knightFroze);
	CHECK(f.knightOrders > 0);
	CHECK(f.damage > 0);
	CHECK((!f.knightAlive || !f.infantryAlive));
}

TEST_CASE("horde cavalry retail: a knight horde's members stay with their horde while it fights")
{
	OPENBFME_REQUIRE_START(s);
	CavalryFight f;
	fightKnightsAgainstOrcs(*s, f);
	// the probe of the S-420 stall had the sole knight 600 from its horde; a fighting member stays within its horde's melee reach (engine pin, not a retail value)
	CHECK(f.worstStraggle < 250.0f);
}

TEST_CASE("horde lifetime retail: a member produced into a horde that is already destroyed goes with it (no dangling container; the AI-vs-AI use-after-free of r4)")
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
	m.game.seed = 7;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = cavalryFactionIndex(s->world->playerTemplates(), factions[i]);
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
	Player *mordor = const_cast<Player *>(cavalryPlayerAtStart(logic, 1));
	REQUIRE(mordor != nullptr);
	Object *horde = logic.newObject(logic.things().findTemplate("MordorFighterHorde"), mordor->getDefaultTeam(), {});
	REQUIRE(horde != nullptr);
	Coord3D p{ 1200.0f, 1200.0f, logic.getGroundHeight(1200.0f, 1200.0f) };
	horde->setPosition(&p);
	game.advance(0.2);
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc != nullptr);
	const ObjectID hordeId = horde->getID();
	std::vector<Object *> members(hc->getContainedItemsList()->begin(), hc->getContainedItemsList()->end());
	REQUIRE(!members.empty());
	for (Object *x : members)
	{
		x->kill(0); // the horde dies with its last member (HordeContain::removeFromContain)
	}
	REQUIRE(horde->isDestroyed());
	// the queue exit hands the next produced member to the horde while it is still findable (RW 0x8A402D: findObjectByID, the contain's acceptance slot)
	Object *late = logic.newObject(logic.things().findTemplate("MordorFighter"), mordor->getDefaultTeam(), {});
	REQUIRE(late != nullptr);
	const ObjectID lateId = late->getID();
	late->setPosition(&p);
	hc->acceptCreatedMember(late);
	CHECK(hc->membersRefusedAfterDelete() == 1);
	for (int i = 0; i < 5; ++i)
	{
		game.advance(0.2);
	}
	CHECK(logic.findObjectByID(hordeId) == nullptr);
	// before the fix the late member stayed alive with getContainedBy() pointing at the deleted horde (ASan: heap-use-after-free in AIWorld::adapterFor)
	CHECK(logic.findObjectByID(lateId) == nullptr);
}
