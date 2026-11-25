// OpenBFME unit tests. GPL-3.0.
// Lane FIRINGTRACKER-1 with the retail data: Legolas (LegolasBow: ContinuousFireOne = 0, ContinuousFireCoast = LEGOLAS_RELOADTIME_MAX, no ContinuousFireTwo:
// the template default 0x7FFFFFFF, RW 0x6CDE89) shooting a fixed target: the FiringTracker (RW 0x8E326D / 0x8E302A / 0x8E3174 / 0x8E30DD, the
// FiringTrackerHelper woken after each shot) sets CONTINUOUS_FIRE_MEAN at the first shot and keeps it (FAST needs more than ContinuousFireTwo shots: no
// retail weapon of either game sets ContinuousFireTwo, so FAST never plays on retail data); once he stops, the coast's cool down shows
// CONTINUOUS_FIRE_SLOW and then clears it. SKIPs without the games.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/FiringTrackerHelper.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <string>
#include <vector>

namespace
{
int ft1FactionIndex(const PlayerTemplateStore &store, const std::string &name)
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

Player *ft1PlayerAtStart(GameLogic &logic, int start)
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
} // namespace

TEST_CASE("firingtracker1 retail: Legolas firing at a fixed target gets CONTINUOUS_FIRE_MEAN (never FAST), then SLOW after the coast, then none")
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
	const char *factions[2] = { "FactionElves", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = ft1FactionIndex(s->world->playerTemplates(), factions[i]);
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
	Player *elves = ft1PlayerAtStart(logic, 0);
	Player *mordor = ft1PlayerAtStart(logic, 1);
	REQUIRE(elves != nullptr);
	REQUIRE(mordor != nullptr);
	// the clear arena of the archer tests (test_archer_spread_retail.cpp): a Mordor lumber mill 150 in front of Legolas, a target that stays put
	const float ax = 1600.0f, bx = 1750.0f, ay = 600.0f;
	Coord3D pa{ ax, ay, logic.getGroundHeight(ax, ay) }, pb{ bx, ay, logic.getGroundHeight(bx, ay) };
	Object *legolas = logic.newObject(logic.things().findTemplate("ElvenLegolas"), elves->getDefaultTeam(), {});
	REQUIRE(legolas != nullptr);
	legolas->setPosition(&pa);
	Object *mill = logic.newObject(logic.things().findTemplate("MordorLumberMill"), mordor->getDefaultTeam(), {});
	REQUIRE(mill != nullptr);
	mill->setPosition(&pb);
	REQUIRE(legolas->firingTrackerHelper() != nullptr);
	game.advance(1.0);
	const int slow = Construction::modelConditionIndex("CONTINUOUS_FIRE_SLOW");
	const int mean = Construction::modelConditionIndex("CONTINUOUS_FIRE_MEAN");
	const int fast = Construction::modelConditionIndex("CONTINUOUS_FIRE_FAST");
	CHECK(slow == 115); // RW 0x8E3076: byte 0xE bit 3 of the flag set
	CHECK(mean == 116);
	CHECK(fast == 117);
	CHECK_FALSE(legolas->testModelCondition(mean));
	const ObjectID millID = mill->getID();
	REQUIRE(legolas->getAIUpdateInterface()->aiAttackObject(mill, CMD_FROM_PLAYER));
	const FiringTracker &tracker = legolas->getWeapons()->firingTracker();
	int frames = 0, shots = 0, meanFrame = -1, fastFrame = -1;
	std::uint32_t lastShot = tracker.lastShotFrame();
	while (frames < 150)
	{
		game.advance(0.2);
		++frames;
		if (tracker.lastShotFrame() != lastShot)
		{
			lastShot = tracker.lastShotFrame();
			++shots;
		}
		if (meanFrame < 0 && legolas->testModelCondition(mean))
		{
			meanFrame = frames;
		}
		if (fastFrame < 0 && legolas->testModelCondition(fast))
		{
			fastFrame = frames;
		}
	}
	MESSAGE("shots " << shots << ", MEAN at " << meanFrame << ", FAST at " << fastFrame << ", count " << tracker.shotCount());
	CHECK(shots == 29); // measured: 150 frames, LEGOLAS_DELAYBETWEENSHOTS
	CHECK(tracker.shotCount() == shots); // one victim, the owner standing: every shot counts on
	CHECK(meanFrame == 6); // the first shot (ContinuousFireOne = 0: count 1 > 0)
	CHECK(fastFrame == -1); // never: ContinuousFireTwo is the default 0x7FFFFFFF
	CHECK(legolas->testModelCondition(mean));
	CHECK_FALSE(legolas->testModelCondition(slow));
	CHECK((legolas->weaponBonusConditionMask() & (1u << FiringTracker::CONDITION_CONTINUOUS_FIRE_MEAN)) != 0u);
	// he stops: the coast runs out and the cool down shows SLOW, then clears it; the helper goes back to sleep
	legolas->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	REQUIRE(logic.findObjectByID(millID) != nullptr);
	int slowFrame = -1, clearFrame = -1;
	for (frames = 0; frames < 200 && clearFrame < 0; ++frames)
	{
		game.advance(0.2);
		if (slowFrame < 0 && legolas->testModelCondition(slow))
		{
			slowFrame = frames;
			CHECK_FALSE(legolas->testModelCondition(mean));
		}
		if (slowFrame >= 0 && !legolas->testModelCondition(slow))
		{
			clearFrame = frames;
		}
	}
	MESSAGE("SLOW at " << slowFrame << ", cleared at " << clearFrame);
	CHECK(slowFrame == 4); // the coast (LEGOLAS_RELOADTIME_MAX after the last shot's ready frame) ran out
	CHECK(clearFrame == 10);
	CHECK(clearFrame - slowFrame == 6); // RW 0xD9F608: cooldown frame = now + 5, the next cool down when now > it
	game.advance(2.0);
	CHECK(tracker.coastEnd() == 0u);
	CHECK(legolas->firingTrackerHelper()->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}
