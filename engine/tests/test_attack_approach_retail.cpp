// OpenBFME retail tests (ROTWK_INSTALL). GPL-3.0.
// A unit that cannot move (Object::isMobile false, or a locomotor whose maximum speed is below 0.1, as the fortress catapults' FloatingCatapultLocomotor) and
// an enemy out of its reach: retail's attack approach asks such a unit for no path, waits two seconds and fails unless the target came into range
// (BFME2 decomp AIAttackApproachTargetStateOnEnter.cpp, onEnter retail 0x0034CD3A; updateInternal).

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponState.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
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

struct Match
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *mordor = nullptr, *men = nullptr, *creeps = nullptr;
	NewGameMessage message;
};

const Player *playerAtStart(GameLogic &logic, int start)
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

// Medium Mordor (start 0) against Medium Men (start 1) on Evendim
void startMatch(starttest::Shared &s, Match &m)
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
	const char *factions[2] = { "FactionMordor", "FactionMen" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = msg.game.slots[i];
		slot.state = SLOT_MED_AI;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	m.message = msg;
	m.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(msg, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *m.start, &error), error);
	m.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	m.assets = std::make_unique<WW3DAssetManager>(*m.source);
	m.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *m.assets, s.options);
	LiveGame::Options o;
	o.start = m.start.get();
	REQUIRE_MESSAGE(m.game->load(o, &error), error);
	m.mordor = const_cast<Player *>(playerAtStart(m.game->logic(), 0));
	m.men = const_cast<Player *>(playerAtStart(m.game->logic(), 1));
	for (int i = 0; i < m.game->logic().players().getPlayerCount(); ++i)
	{
		Player *p = m.game->logic().players().getNthPlayer(i);
		if (p && p->getPlayerName() == "PlyrCreeps")
		{
			m.creeps = p;
		}
	}
	REQUIRE(m.mordor != nullptr);
	REQUIRE(m.men != nullptr);
	REQUIRE(m.creeps != nullptr);
}

Object *placeAt(GameLogic &logic, const char *name, Player *p, float x, float y)
{
	Object *o = logic.newObject(logic.things().findTemplate(name), p->getDefaultTeam(), {});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D pos{ x, y, logic.getGroundHeight(x, y) };
	o->setPosition(&pos);
	return o;
}

Object *placeAtPosition(GameLogic &logic, const char *name, Player *p, const Coord3D &pos)
{
	Object *o = logic.newObject(logic.things().findTemplate(name), p->getDefaultTeam(), {});
	REQUIRE_MESSAGE(o != nullptr, name);
	o->setPosition(&pos);
	return o;
}

void advance(Match &m, int frames)
{
	for (int i = 0; i < frames; ++i)
	{
		m.game->advance(0.2);
	}
}

// a clear land cell at least `clear` units from every object not owned by `owner`: the guard machine's scan and the idle mood scan find nothing there (the
// map's creep lairs sit inside an archer horde's 1,300-unit guard range near the eastern shore the other cases use)
Coord3D quietSpot(GameLogic &logic, const Player &owner, float clear)
{
	AIWorld *world = logic.aiWorld();
	REQUIRE(world != nullptr);
	Pathfinder &pathfinder = world->pathfinder();
	for (float x = 300.0f; x <= 4700.0f; x += 40.0f)
	{
		for (float y = 300.0f; y <= 4700.0f; y += 40.0f)
		{
			Coord3D p{ x, y, logic.getGroundHeight(x, y) };
			const PathfindCell *c = pathfinder.getCell(LAYER_GROUND, &p);
			if (!c || c->getType() != PathfindCell::CELL_CLEAR)
			{
				continue;
			}
			bool ok = true;
			for (Object *o = logic.getFirstObject(); o && ok; o = o->getNextObject())
			{
				if (o->isEffectivelyDead() || o->getControllingPlayer() == &owner)
				{
					continue;
				}
				const float dx = o->getPosition()->x - x, dy = o->getPosition()->y - y;
				if (dx * dx + dy * dy < clear * clear)
				{
					ok = false;
				}
			}
			if (ok)
			{
				return p;
			}
		}
	}
	REQUIRE_MESSAGE(false, "no quiet spot on Evendim");
	return Coord3D{};
}

unsigned lastShot(const Object &unit)
{
	unsigned last = 0;
	if (const ObjectWeapons *w = unit.getWeapons())
	{
		for (int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
		{
			if (const Weapon *weapon = w->weaponInSlot(slot))
			{
				last = std::max(last, weapon->lastFireFrame());
			}
		}
	}
	return last;
}
} // namespace

// MordorFortressCatapult (data/ini/object/evilfaction/units/mordor/catapult.ini): KindOf without IMMOBILE, Locomotor FloatingCatapultLocomotor without a
// Speed (0), primary weapon MordorCatapultRock_Structural (range 500). Retail (BFME2 decomp AIAttackApproachTargetStateOnEnter.cpp, onEnter retail 0x0034CD3A:
// Object::isMobile false or the locomotor's max speed below 0.1 -> wait 2 * LOGICFRAMES_PER_SECOND, no path; updateInternal: the wait over -> STATE_FAILURE,
// which leaves the attack machine) gives up an attack on a victim it cannot reach; the port walked at speed 0 and held it.
TEST_CASE("attack approach: a fortress catapult (no locomotor speed) ordered at a structure beyond its range gives the attack up after two seconds (RW 0x0034CD3A)")
{
	OPENBFME_REQUIRE_START(s);
	Match m;
	startMatch(*s, m);
	GameLogic &logic = m.game->logic();
	const Coord3D spot = quietSpot(logic, *m.mordor, 1000.0f);
	Object *catapult = placeAtPosition(logic, "MordorFortressCatapult", m.mordor, spot);
	const ObjectID catapultId = catapult->getID();
	const float fx = spot.x >= 1000.0f ? spot.x - 800.0f : spot.x + 800.0f; // beyond the 500 range
	Object *farm = placeAt(logic, "GondorFarm", m.men, fx, spot.y);
	advance(m, 10);
	REQUIRE(logic.findObjectByID(catapultId) != nullptr);
	AIUpdateInterface *ai = catapult->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->curLocomotorSpeed() == 0.0f); // FloatingCatapultLocomotor has no Speed
	REQUIRE(ai->aiAttackObject(farm, CMD_FROM_AI));
	advance(m, 2);
	REQUIRE(ai->isAttacking());
	const unsigned ordered = logic.getFrame();
	int gaveUp = -1;
	for (int f = 0; f < 300 && gaveUp < 0; ++f)
	{
		advance(m, 1);
		if (!ai->isAttacking())
		{
			gaveUp = (int)(logic.getFrame() - ordered);
		}
	}
	MESSAGE("the attack ended after " << gaveUp << " frames (state " << ai->currentStateId() << ")");
	CHECK(gaveUp == 2 * LOGICFRAMES_PER_SECOND); // the wait: two seconds (10 frames) after the attack was ordered
}

TEST_CASE("attack approach: a guarding fortress catapult that picked a structure out of range lets it go and fires at one placed in range")
{
	OPENBFME_REQUIRE_START(s);
	Match m;
	startMatch(*s, m);
	GameLogic &logic = m.game->logic();
	const Coord3D spot = quietSpot(logic, *m.mordor, 1000.0f);
	Object *catapult = placeAtPosition(logic, "MordorFortressCatapult", m.mordor, spot);
	const ObjectID catapultId = catapult->getID();
	const float dir = spot.x >= 1000.0f ? -1.0f : 1.0f;
	Object *farFarm = placeAt(logic, "GondorFarm", m.men, spot.x + dir * 800.0f, spot.y); // inside the guard range, beyond the weapon's
	advance(m, 10);
	AIUpdateInterface *ai = catapult->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(StancesBehavior::of(*catapult) != nullptr);
	StancesBehavior::of(*catapult)->setStance(STANCE_AGGRESSIVE);
	ai->guardHere(0);
	int picked = -1;
	for (int f = 0; f < 200 && picked < 0; ++f)
	{
		advance(m, 1);
		if (ai->currentVictim() == farFarm)
		{
			picked = f;
		}
	}
	MESSAGE("the guard picked the far farm after " << picked << " frames");
	REQUIRE(picked == 2); // the guard took the far farm; a miss here means a later shot is not the give-up
	const unsigned before = lastShot(*catapult);
	Object *nearFarm = placeAt(logic, "GondorFarm", m.men, spot.x + dir * 300.0f, spot.y + 40.0f); // inside the 500 range, beyond the minimum 150
	REQUIRE(nearFarm != nullptr);
	int abandonedAt = -1;
	for (int f = 0; f < 300; ++f)
	{
		advance(m, 1);
		REQUIRE(logic.findObjectByID(catapultId) != nullptr);
		if (abandonedAt < 0 && ai->currentVictim() != farFarm)
		{
			abandonedAt = f;
		}
	}
	MESSAGE("last shot " << lastShot(*catapult) << " (before " << before << "), victim " << (ai->currentVictim() ? (int)ai->currentVictim()->getID() : 0)
		<< ", far farm dropped after " << abandonedAt << " frames");
	// The two-second wait (RW 0x0034CD3A) is what drops the unreachable farm. A guard that never took it, or that drops it on the
	// frame the near farm appears, does not show that wait. Holding the far farm for the whole window does not either.
	CHECK(abandonedAt == 2 * LOGICFRAMES_PER_SECOND);
	CHECK(lastShot(*catapult) > before); // tolerance: any shot after the near farm is placed proves the guard fired; its frame is reported
}

// RW 0x749D46 (the approach's update) reads the wait of RW 0x74E6CE only for an object target. An attack on a ground position runs computePath RW 0x749A3E on
// every update instead: a speedless but mobile owner (Object::isMobile RW 0x690E97 true: FloatingCatapultLocomotor) gets its path and holds the attack while
// the move runs at speed 0; it does not give up after the two seconds
TEST_CASE("attack approach: a fortress catapult ordered to attack the ground beyond its range keeps the order (the wait is for object targets, RW 0x749D46)")
{
	OPENBFME_REQUIRE_START(s);
	Match m;
	startMatch(*s, m);
	GameLogic &logic = m.game->logic();
	const Coord3D spot = quietSpot(logic, *m.mordor, 1000.0f);
	Object *catapult = placeAtPosition(logic, "MordorFortressCatapult", m.mordor, spot);
	const ObjectID catapultId = catapult->getID();
	advance(m, 10);
	REQUIRE(logic.findObjectByID(catapultId) != nullptr);
	AIUpdateInterface *ai = catapult->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->curLocomotorSpeed() == 0.0f);
	const float gx = spot.x >= 1000.0f ? spot.x - 800.0f : spot.x + 800.0f; // beyond the 500 range
	const Coord3D ground{ gx, spot.y, logic.getGroundHeight(gx, spot.y) };
	REQUIRE(ai->aiAttackPosition(ground, 0x7FFFFFFF, CMD_FROM_AI));
	advance(m, 2);
	REQUIRE(ai->isAttacking());
	const unsigned ordered = logic.getFrame();
	int ended = -1;
	for (int f = 0; f < 6 * LOGICFRAMES_PER_SECOND && ended < 0; ++f)
	{
		advance(m, 1);
		if (!ai->isAttacking())
		{
			ended = (int)(logic.getFrame() - ordered);
		}
	}
	MESSAGE("the ground attack ended after " << ended << " frames (-1: still attacking after six seconds; state " << ai->currentStateId() << ")");
	CHECK(ended == -1);
}
