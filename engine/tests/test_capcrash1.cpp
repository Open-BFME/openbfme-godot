// OpenBFME unit tests. GPL-3.0.
// lane CAPCRASH-1: capturing a barracks after (and while) its horde production leaves the building, on real RotWK 2.01 data.
//
// The report this lane closes: a probe that captured a GondorBarracks placed in the middle of "map mp evendim" 400 advances after queueing a
// GondorFighterHorde crashed with SIGSEGV reading the barracks' owner. Under AddressSanitizer the read is a heap-use-after-free of the barracks
// itself: the map centre is the Barrow Wight lairs' ground (PlyrCreeps), the wights attack the horde as it walks out and then the barracks
// (3000 -> 0 health over ~170 frames), GameLogic::processDestroyList frees it, and the probe went on using its raw Object pointer. The capture
// path (Object::setCapturedTeam, RW 0x6996DC; setTeam RW 0x69954A; the exit's releaseLastExit RW 0x8A3BF8, which finds the horde by its id,
// RW 0x449681, never by a pointer) holds no pointer to the produced horde. These cases pin that: the creeps are cleared, the barracks is held by
// id, and the capture runs after the horde has left (the exit's last exit id released) and while its members are still leaving. They pass on the
// archive with the capture's production cancel (RW 0x6997B0 -> ProductionUpdate slot +0x40, RW 0x8A1666, #324) and run clean under AddressSanitizer
// (the owner checks also held before #324; the queue checks need it). In 2.01 data no horde member has a BuildFadeInOnCreateTime, so a member never
// waits in the doorway (ProductionUpdate + 0x120) across frames. Editions: the path is shared by RotWK 2.01 and BFME2 1.06; the data here is RotWK's.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
struct CapMatch
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *men = nullptr;
	Player *mordor = nullptr;
};

int capFaction(const PlayerTemplateStore &store, const std::string &name)
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

Player *capAtStart(GameLogic &logic, int start)
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

void capStart(starttest::Shared &s, CapMatch &m)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameMessage msg;
	msg.game.mapName = "maps/map mp evendim/map mp evendim.map";
	msg.game.seed = 5;
	msg.game.startingCash = 1500;
	const char *factions[] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = msg.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		slot.name = std::u16string(u"Player") + (char16_t)(u'0' + i);
		slot.playerTemplate = capFaction(s.world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: capFaction returns -1 when the faction is missing; any index is the template
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	m.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(msg, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *m.start, &error), error);
	m.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	m.assets = std::make_unique<WW3DAssetManager>(*m.source);
	m.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *m.assets, s.options);
	LiveGame::Options o;
	o.start = m.start.get();
	REQUIRE_MESSAGE(m.game->load(o, &error), error);
	m.men = capAtStart(m.game->logic(), 0);
	m.mordor = capAtStart(m.game->logic(), 1);
	REQUIRE(m.men != nullptr);
	REQUIRE(m.mordor != nullptr);
}

void capAdvance(CapMatch &m, int n)
{
	for (int i = 0; i < n; ++i)
	{
		m.game->advance(0.2);
	}
}

// the Barrow Wight lairs and their wights at the map centre (PlyrCreeps) kill a barracks placed there before a capture can be tested
void clearCreeps(GameLogic &logic)
{
	std::vector<Object *> creeps;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Player *p = o->getControllingPlayer();
		if (p && p->getPlayerName() == "PlyrCreeps")
		{
			creeps.push_back(o);
		}
	}
	REQUIRE(!creeps.empty());
	for (Object *o : creeps)
	{
		logic.destroyObject(o);
	}
}

enum class CapWhen
{
	AfterExit,   // the horde has left: the queue is empty and the exit's last exit id (RW +0x40) is released
	DuringExit,  // the horde object is out with some members, the rest are still being produced (the exit's last exit id is the horde)
};

void captureAfterProduction(starttest::Shared &s, CapWhen when)
{
	CapMatch m;
	capStart(s, m);
	GameLogic &logic = m.game->logic();
	clearCreeps(logic);
	// after the capture the producer's own horde stands beside a now hostile barracks and (rightly) attacks it (3000 -> 0 in ~300 frames): the auto
	// acquire is off so the barracks outlives the observation window; it is not part of the capture path
	logic.combat().setAutoAcquireEnabled(false);
	float maxX = 0.0f, maxY = 0.0f;
	REQUIRE(logic.terrain()->getExtent(0, maxX, maxY));
	const float cx = maxX * 0.5f, cy = maxY * 0.5f;
	const ThingTemplate *barracksTemplate = logic.things().findTemplate("GondorBarracks");
	const ThingTemplate *hordeTemplate = logic.things().findTemplate("GondorFighterHorde");
	REQUIRE(barracksTemplate != nullptr);
	REQUIRE(hordeTemplate != nullptr);
	Object *placed = logic.newObject(barracksTemplate, m.men->getDefaultTeam(), {});
	REQUIRE(placed != nullptr);
	Coord3D pos{ cx, cy, logic.getGroundHeight(cx, cy) };
	placed->setPosition(&pos);
	placed->friend_onBuildComplete();
	const ObjectID barracksId = placed->getID();
	auto barracks = [&]() { return logic.findObjectByID(barracksId); }; // by id: a raw pointer outlives a destroyed object (the probe's crash)
	capAdvance(m, 5);
	REQUIRE(barracks() != nullptr);
	ProductionUpdateInterface *pu = barracks()->getProductionUpdate();
	REQUIRE(pu != nullptr);
	QueueProductionExitUpdate *exit = dynamic_cast<QueueProductionExitUpdate *>(barracks()->getObjectExitInterface());
	REQUIRE(exit != nullptr);
	m.men->getMoney()->deposit(5000);
	REQUIRE(pu->queueCreateUnit(hordeTemplate, -1, pu->requestUniqueUnitID(), -1, false, "", false));

	auto members = [&](ObjectID id) -> std::vector<ObjectID> {
		std::vector<ObjectID> ids;
		Object *o = logic.findObjectByID(id);
		if (const ContainModuleInterface *c = o ? o->getContain() : nullptr)
		{
			if (const ContainModuleInterface::ContainedItemsList *l = c->getContainedItemsList())
			{
				for (Object *x : *l)
				{
					ids.push_back(x->getID());
				}
			}
		}
		return ids;
	};
	ObjectID hordeId = INVALID_ID;
	bool ready = false;
	for (int f = 0; f < 3000 && !ready; ++f)
	{
		capAdvance(m, 1);
		Object *b = barracks();
		REQUIRE_MESSAGE(b != nullptr, "the barracks died before the capture (frame " << logic.getFrame() << ")");
		REQUIRE(!b->isEffectivelyDead());
		if (exit->lastExitID() != INVALID_ID)
		{
			hordeId = exit->lastExitID();
		}
		ready = when == CapWhen::DuringExit ? exit->lastExitID() != INVALID_ID && pu->getProductionCount() != 0 && !members(hordeId).empty()
		                                    : hordeId != INVALID_ID && exit->lastExitID() == INVALID_ID && pu->getProductionCount() == 0;
	}
	REQUIRE_MESSAGE(ready, "the horde production never reached the capture point");
	if (when == CapWhen::AfterExit)
	{
		capAdvance(m, 30); // the horde walks off; the capture comes some frames after the release, as in the report
	}
	Object *horde = logic.findObjectByID(hordeId);
	REQUIRE(horde != nullptr);
	CHECK(horde->getTemplate() == hordeTemplate);
	CHECK(horde->getControllingPlayer() == m.men);

	const std::vector<ObjectID> membersAtCapture = members(hordeId);
	REQUIRE(!membersAtCapture.empty());
	Object *b = barracks();
	REQUIRE(b != nullptr);
	REQUIRE(b->getControllingPlayer() == m.men);
	b->setCapturedTeam(m.mordor->getDefaultTeam());
	CHECK(b->getControllingPlayer() == m.mordor);
	for (int f = 0; f < 600; ++f)
	{
		capAdvance(m, 1);
		Object *bk = barracks();
		REQUIRE_MESSAGE(bk != nullptr, "the captured barracks vanished (frame " << logic.getFrame() << ")");
		REQUIRE(bk->getControllingPlayer() == m.mordor);
	}
	// the produced horde is not the barracks': it stays its producer's (the capture takes the building only)
	Object *h = logic.findObjectByID(hordeId);
	REQUIRE(h != nullptr);
	CHECK(h->getControllingPlayer() == m.men);
	// the capture cancels what was left of the production (RW 0x6997B0 -> slot +0x40, RW 0x8A1666) and releases the last exit: a horde caught mid-production
	// keeps the members it had, all of them its producer's, and gains none
	CHECK(pu->getProductionCount() == 0);
	CHECK(exit->lastExitID() == INVALID_ID);
	const std::vector<ObjectID> membersAfter = members(hordeId);
	CHECK(membersAfter == membersAtCapture);
	for (ObjectID id : membersAfter)
	{
		Object *o = logic.findObjectByID(id);
		REQUIRE(o != nullptr);
		CHECK(o->getControllingPlayer() == m.men);
	}
}
} // namespace

TEST_CASE("CAPCRASH-1: a barracks captured after its horde left keeps a sane owner")
{
	OPENBFME_REQUIRE_START(sp);
	captureAfterProduction(*sp, CapWhen::AfterExit);
}

TEST_CASE("CAPCRASH-1: a barracks captured while its horde's members leave keeps a sane owner")
{
	OPENBFME_REQUIRE_START(sp);
	captureAfterProduction(*sp, CapWhen::DuringExit);
}
