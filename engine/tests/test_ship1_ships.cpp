// OpenBFME unit tests. GPL-3.0.
// Lane SHIP-1: ships and hover units on a retail water map (the SHIP / HOVER movers and SEA_LEVEL of GameLogic/Object/LocomotorMove.cpp, the transports' water
// legs, ship combat). The retail cases SKIP without the games (ROTWK_INSTALL / BFME2_INSTALL).

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// ---- retail ----------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
int shipFactionIndex(const PlayerTemplateStore &store, const std::string &name)
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

Player *shipPlayerAtStart(GameLogic &logic, int start)
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

// a retail skirmish game on Evendim (a map with standing water and port sites) between two factions; objects are made at will (lane FLY-1's arena)
struct ShipArena
{
	starttest::Shared &s;
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *a = nullptr, *b = nullptr;

	ShipArena(starttest::Shared &shared, const char *factionA, const char *factionB, unsigned seed = 2)
		: s(shared)
	{
		static std::vector<MapCacheEntry> cache;
		std::string error;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		NewGameMessage m;
		m.game.mapName = "maps/map mp evendim/map mp evendim.map";
		m.game.seed = seed;
		m.game.startingCash = 1500;
		const char *factions[2] = { factionA, factionB };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishGameSlot &slot = m.game.slots[i];
			slot.state = SLOT_PLAYER;
			slot.name = i == 0 ? u"Player0" : u"Player1";
			slot.playerTemplate = shipFactionIndex(s.world->playerTemplates(), factions[i]);
			REQUIRE(slot.playerTemplate >= 0); // tolerance: an index of the faction found by name (-1 when absent)
			slot.startPos = i;
			slot.color = i;
			slot.teamNumber = i;
		}
		start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *start, &error), error);
		source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
		assets = std::make_unique<WW3DAssetManager>(*source);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
		LiveGame::Options o;
		o.start = start.get();
		REQUIRE_MESSAGE(game->load(o, &error), error);
		a = shipPlayerAtStart(logic(), 0);
		b = shipPlayerAtStart(logic(), 1);
		REQUIRE(a != nullptr);
		REQUIRE(b != nullptr);
		logic().combat().setAutoAcquireEnabled(false); // the cases order every attack themselves
	}
	GameLogic &logic() { return game->logic(); }
	// the map's creeps and their lairs (enemies of both players, the neutral creep player's) go: a case whose mood scan is on meets only its own enemies
	void clearCreeps()
	{
		std::vector<Object *> gone;
		for (Object *o = logic().getFirstObject(); o; o = o->getNextObject())
		{
			const Player *owner = o->getControllingPlayer();
			if (owner != a && owner != b && a->getRelationship(o->getTeam()) == ENEMIES && b->getRelationship(o->getTeam()) == ENEMIES)
			{
				gone.push_back(o);
			}
		}
		for (Object *o : gone)
		{
			logic().destroyObject(o);
		}
		step();
	}
	Object *make(const char *name, Player *owner, float x, float y, float zAbove = 0.0f)
	{
		const ThingTemplate *tt = logic().things().findTemplate(name);
		REQUIRE_MESSAGE(tt != nullptr, name);
		Object *o = logic().newObject(tt, owner->getDefaultTeam(), {});
		REQUIRE(o != nullptr);
		Coord3D p{ x, y, logic().getGroundHeight(x, y) + zAbove };
		o->setPosition(&p);
		return o;
	}
	void step(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			game->advance(0.2);
		}
	}
	float above(const Object &o) { return o.getPosition()->z - logic().getGroundHeight(o.getPosition()->x, o.getPosition()->y); }
};

bool underwater(GameLogic &logic, float x, float y, float *z = nullptr)
{
	const PathfindTerrain *t = logic.aiWorld()->pathfinder().terrainView();
	REQUIRE(t != nullptr);
	return t->isUnderwater(x, y, z, nullptr);
}

// every frame of a sail: the ship's position is under water and its height is the water's (SEA_LEVEL, RW 0x5E781F)
struct WaterWatch
{
	int frames = 0, dry = 0, offLevel = 0;
	void see(GameLogic &logic, const Object &ship)
	{
		++frames;
		float wz = 0.0f;
		const Coord3D p = *ship.getPosition();
		if (!underwater(logic, p.x, p.y, &wz))
		{
			++dry;
		}
		else if (p.z != wz)
		{
			++offLevel;
		}
	}
};

float dist2(const Object &a, const Coord3D &b)
{
	const float dx = a.getPosition()->x - b.x, dy = a.getPosition()->y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

std::vector<Object *> membersOf(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}
} // namespace

TEST_CASE("ship1 retail: a transport ship sails around Evendim's island on the water at the water height; ordered inland it stays on the water")
{
	OPENBFME_REQUIRE_START(s);
	ShipArena arena(*s, "FactionMen", "FactionMordor");
	GameLogic &logic = arena.logic();
	Object *ship = arena.make("EvilMenTransportShip", arena.b, 2400.0f, 1800.0f);
	arena.step(2);
	AIUpdateInterface *ai = ship->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->curLocomotor() != nullptr);
	CHECK(ai->curLocomotor()->getTemplate().m_name == "TransportShipLocomotor");
	float wz = 0.0f;
	REQUIRE(underwater(logic, 2400.0f, 1800.0f, &wz));
	// the lake's island stands between (2400, 1800) and (2400, 3300): the path goes round it on water
	REQUIRE_FALSE(underwater(logic, 2400.0f, 2500.0f));
	const Coord3D goal{ 2400.0f, 3300.0f, 0.0f };
	REQUIRE(underwater(logic, goal.x, goal.y));
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	WaterWatch w;
	float farthestFromLine = 0.0f;
	int f = 0;
	for (; f < 600; ++f)
	{
		arena.step();
		w.see(logic, *ship);
		farthestFromLine = std::max(farthestFromLine, std::fabs(ship->getPosition()->x - 2400.0f));
		if (f > 5 && ai->currentStateId() == (unsigned)AI_IDLE)
		{
			break;
		}
	}
	MESSAGE("sailed " << f << " frames to " << ship->getPosition()->x << "," << ship->getPosition()->y << " (" << farthestFromLine << " off the straight line)");
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // the order ended inside the loop's cap
	CHECK(w.dry == 0);
	CHECK(w.offLevel == 0);
	CHECK(dist2(*ship, goal) < 20.0f); // tolerance: the locomotor stops within its braking overshoot of the goal (TransportShipLocomotor's Braking)
	CHECK(farthestFromLine > 400.0f); // tolerance: the island is 400+ wide at x = 2400 (the probe of the map's water): a straight line would cross it
	CHECK(logic.aiWorld()->movementFailures().empty());

	// ordered onto the dry shore, the ship ends on the water next to it (its locomotor's surfaces: WATER and DEEP_WATER; the destination adjustment RW 0x6F3C87
	// finds the nearest water cell; an order far inland needs findClosestPath, S-166)
	const Coord3D inland{ 2000.0f, 950.0f, 0.0f };
	REQUIRE_FALSE(underwater(logic, inland.x, inland.y));
	ai->aiMoveToPosition(inland, CMD_FROM_PLAYER);
	WaterWatch w2;
	for (f = 0; f < 600; ++f)
	{
		arena.step();
		w2.see(logic, *ship);
		if (f > 5 && ai->currentStateId() == (unsigned)AI_IDLE)
		{
			break;
		}
	}
	MESSAGE("the inland order ended after " << f << " frames at " << ship->getPosition()->x << "," << ship->getPosition()->y);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // the order ended inside the loop's cap
	CHECK(w2.dry == 0);
	CHECK(w2.offLevel == 0);
	CHECK(ship->getPosition()->y < 1300.0f); // tolerance: it sailed south to the shore (the lake's south shore is near y = 1000 at x = 2000)
}

TEST_CASE("ship1 retail: a horde boards a transport ship at the south shore, the ship crosses the lake and the horde lands on the north shore")
{
	OPENBFME_REQUIRE_START(s);
	ShipArena arena(*s, "FactionMen", "FactionMordor");
	GameLogic &logic = arena.logic();
	arena.clearCreeps();
	// the ship lies off the south shore, its bow to the land (the boarding AI that brings a ship to the shore is S-4160's)
	Object *ship = arena.make("EvilMenTransportShip", arena.b, 2000.0f, 1140.0f);
	ship->setOrientation(-1.57079637f);
	Object *horde = arena.make("MordorFighterHorde", arena.b, 2000.0f, 850.0f);
	arena.step(3);
	Pathfinder &pf = logic.aiWorld()->pathfinder();
	REQUIRE(underwater(logic, 2000.0f, 1140.0f));
	REQUIRE(pf.getCell(LAYER_GROUND, 200, 114)->getType() == PathfindCell::CELL_DEEP_WATER);
	REQUIRE_FALSE(underwater(logic, 2000.0f, 850.0f));
	ContainModuleInterface *t = ship->getContain();
	REQUIRE(t != nullptr);
	Coord3D entry;
	REQUIRE(t->getEntryOffset(entry));
	// the shore spot (RW 0x6EFBB8): the ship's box (major 60, minor 15) plus 20 a side is 16 x 7 cells, turned south: cells x 196 .. 203, y 106 .. 122 around the
	// centre cell (200, 114). Evendim's cells there (read off the map's classification): the CLEAR ones with y >= 108 are (199 .. 203, 108) and (203, 109); the
	// nearest is (203, 109), 58.3 away ((200, 108) is 60)
	CHECK(entry.x == 2030.0f);
	CHECK(entry.y == 1090.0f);
	CHECK(pf.getCell(LAYER_GROUND, 203, 109)->getType() == PathfindCell::CELL_CLEAR);
	const std::vector<Object *> members = membersOf(*horde);
	REQUIRE(!members.empty());
	REQUIRE(horde->getAIUpdateInterface()->aiEnter(ship, CMD_FROM_PLAYER));
	for (int f = 0; f < 300 && horde->getContainedBy() != ship; ++f)
	{
		arena.step();
	}
	REQUIRE_MESSAGE(horde->getContainedBy() == ship, "the horde never boarded: at " << horde->getPosition()->x << "," << horde->getPosition()->y);
	arena.step(60);
	CHECK(t->getContainCount() == 1u);
	// the crossing
	const Coord3D across{ 1550.0f, 4150.0f, 0.0f };
	REQUIRE(underwater(logic, across.x, across.y));
	AIUpdateInterface *ai = ship->getAIUpdateInterface();
	ai->aiMoveToPosition(across, CMD_FROM_PLAYER);
	WaterWatch w;
	int f = 0;
	for (; f < 900; ++f)
	{
		arena.step();
		w.see(logic, *ship);
		if (f > 5 && ai->currentStateId() == (unsigned)AI_IDLE)
		{
			break;
		}
	}
	MESSAGE("crossed in " << f << " frames to " << ship->getPosition()->x << "," << ship->getPosition()->y);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // the order ended inside the loop's cap
	CHECK(w.dry == 0);
	CHECK(w.offLevel == 0);
	CHECK(horde->getContainedBy() == ship);
	// the evacuate order: the horde lands on the shore spot and walks on
	Coord3D spot;
	REQUIRE(OpenContain::shipShoreSpot(*ship, *ship->getPosition(), spot));
	t->orderAllPassengersToExit((int)CMD_FROM_PLAYER);
	for (int g = 0; g < 200 && horde->getContainedBy(); ++g)
	{
		arena.step();
	}
	REQUIRE_MESSAGE(horde->getContainedBy() == nullptr, "the horde never left the ship");
	MESSAGE("landed at " << horde->getPosition()->x << "," << horde->getPosition()->y << " (spot " << spot.x << "," << spot.y << ")");
	CHECK(pf.getCell(LAYER_GROUND, SimMath::floorToInt(spot.x * 0.1f), SimMath::floorToInt(spot.y * 0.1f))->getType() == PathfindCell::CELL_CLEAR);
	arena.step(60);
	CHECK(horde->isInWorld());
	CHECK_FALSE(underwater(logic, horde->getPosition()->x, horde->getPosition()->y));
	size_t ashore = 0;
	for (Object *m : members)
	{
		ashore += (m->isInWorld() && !underwater(logic, m->getPosition()->x, m->getPosition()->y)) ? 1u : 0u;
	}
	CHECK(ashore == members.size());
	CHECK(horde->getPosition()->y > 4000.0f); // tolerance: the north shore of the lake's north lobe (the probe: water up to y = 4250 at x = 1550)
	const std::vector<std::string> raised = logic.aiWorld()->stops();
	CHECK(std::count(raised.begin(), raised.end(), std::string(OpenContain::shipStop())) == 1);
}

// every damaging hit the bodies apply, as CombatState's damage trace reports it (the victim, the source, the amount dealt and the health it took)
struct Hit
{
	ObjectID victim, source;
	float dealt, taken;
};
TEST_CASE("ship1 retail: an Elven battle ship sails into range, its five archers sink an enemy transport ship (12.5 an arrow) and shoot an orc pit on the shore (0.5 an arrow)")
{
	OPENBFME_REQUIRE_START(s);
	ShipArena arena(*s, "FactionMen", "FactionMordor");
	GameLogic &logic = arena.logic();
	arena.clearCreeps();
	Object *bs = arena.make("ElvenBattleShip", arena.a, 2000.0f, 1800.0f);
	Object *enemyShip = arena.make("EvilMenTransportShip", arena.b, 2250.0f, 1300.0f);
	Object *pit = arena.make("MordorOrcPit", arena.b, 1900.0f, 950.0f);
	arena.step(3);
	REQUIRE(bs->getContain() != nullptr);
	CHECK(bs->getContain()->getContainCount() == 5u); // InitialPayload = InternalShipGoodArcher 5
	auto hp = [](Object *o) { return o->getBodyModule()->getHealth(); };
	CHECK(hp(enemyShip) == 250.0f); // EvilMenTransportShip's MaxHealth
	CHECK(hp(pit) == 1500.0f);
	REQUIRE_FALSE(underwater(logic, 1900.0f, 950.0f)); // the pit stands on the shore
	// out of the archers' 350 at the start: the ship sails into range on the water (auto-acquire is off during the sail)
	AIUpdateInterface *ai = bs->getAIUpdateInterface();
	ai->aiMoveToPosition(Coord3D{ 2000.0f, 1300.0f, 0.0f }, CMD_FROM_PLAYER);
	WaterWatch w;
	for (int f = 0; f < 300; ++f)
	{
		arena.step();
		w.see(logic, *bs);
		if (f > 5 && !ai->isMoving())
		{
			break;
		}
	}
	CHECK_FALSE(ai->isMoving()); // arrived (a ship with StancesBehavior ends a move in AI_GUARD, lane PLAY-4)
	CHECK(w.dry == 0);
	CHECK(w.offLevel == 0);
	CHECK(hp(enemyShip) == 250.0f);
	CHECK(hp(pit) == 1500.0f);
	// the archers (AutoAcquireEnemiesWhenIdle ATTACK_BUILDINGS, CanAttackWhileContained) take their targets: GoodShipRangedAntiShipWarhead is 25 PIERCE;
	// LargeShipArmor is DEFAULT 50% (12.5 an arrow), FactoryArmor PIERCE 2% (0.5 an arrow)
	std::vector<Hit> hits;
	logic.combat().setDamageTrace([&hits](const Object &victim, std::uint32_t source, float dealt, float taken) {
		hits.push_back(Hit{ victim.getID(), (ObjectID)source, dealt, taken });
	});
	std::vector<ObjectID> archers;
	for (Object *a : *bs->getContain()->getContainedItemsList())
	{
		archers.push_back(a->getID());
	}
	REQUIRE(archers.size() == 5u);
	const ObjectID shipId = enemyShip->getID(), pitId = pit->getID();
	logic.combat().setAutoAcquireEnabled(true);
	arena.step(300);
	logic.combat().setDamageTrace(nullptr);
	// every hit on the two targets, counted one by one: each is one arrow of an archer, 12.5 into the ship and 0.5 of the pit's health
	int shipHits = 0, pitHits = 0, otherHits = 0, wrongAmount = 0;
	float shipTaken = 0.0f, pitTaken = 0.0f;
	for (const Hit &h : hits)
	{
		const bool byArcher = std::find(archers.begin(), archers.end(), h.source) != archers.end();
		if (h.victim == shipId && byArcher)
		{
			++shipHits;
			shipTaken += h.taken;
			wrongAmount += (h.dealt == 12.5f && h.taken == 12.5f) ? 0 : 1;
		}
		else if (h.victim == pitId && byArcher)
		{
			++pitHits;
			pitTaken += h.taken;
			// 25 x FactoryArmor's PIERCE 2% as the armour parse stores it is the float 0.49999952 (0x1.ffffep-2, measured: not 0.5); the pit's health
			// near 1500 moves in steps of 1/8192, so each hit takes exactly 0.5
			wrongAmount += (h.dealt == 0x1.ffffep-2f && h.taken == 0.5f) ? 0 : 1;
		}
		else if (h.victim == shipId || h.victim == pitId)
		{
			++otherHits;
		}
	}
	Object *pitNow = logic.findObjectByID(pitId);
	REQUIRE(pitNow != nullptr);
	MESSAGE("hits: " << shipHits << " into the ship, " << pitHits << " into the pit, pit at " << hp(pitNow));
	CHECK(wrongAmount == 0);
	CHECK(otherHits == 0);
	CHECK(shipHits == 20); // 250 / 12.5: the twentieth arrow sank the ship
	CHECK(shipTaken == 250.0f);
	CHECK(pitHits == 484); // measured on this tree: the archers turn to the pit after the sinking and shoot it for the rest of the 300 frames (lane TARGET-1: 487, was 489: the turn to the pit is RotWK's mood scan, on its own cadence; ATTACKPATH-1 alone 488; merge queue q23 with both and FLANK-1's in-flight hits: 484)
	CHECK(pitTaken == 1500.0f - hp(pitNow));
	Object *shipNow = logic.findObjectByID(shipId);
	CHECK((shipNow == nullptr || shipNow->isEffectivelyDead()));
}

TEST_CASE("ship1 retail: every template with a SHIP or HOVER locomotor, by name: ships sail on the water at its height, hover units move, speed-0 ones stay")
{
	OPENBFME_REQUIRE_START(s);
	ShipArena arena(*s, "FactionMen", "FactionMordor");
	GameLogic &logic = arena.logic();
	arena.clearCreeps();
	// the inventory: every template whose LocomotorSet names a SHIP or HOVER locomotor (data\ini\locomotor.ini: LargeShipLocomotor, TransportShipLocomotor(ForUnit
	// Interaction) SHIP / SEA_LEVEL; EyeLocomotor, TornadoLocomotor HOVER / NO_Z_MOTIVE_FORCE; CrebainLocomotor HOVER / SURFACE_RELATIVE_HEIGHT; BoatLocomotor HOVER /
	// ABSOLUTE_HEIGHT; WhirlpoolLocomotor HOVER / FLOATING_Z)
	const std::vector<std::string> ships = { "EvilShip_Interface", "EvilMenTestCorsairShip", "EvilMenCorsairShip", "EvilShoreBombardShip", "EvilFireShip",
		"EvilMenTransportShip", "EGH_SlowTransport", "ElvenShip_Interface", "ElvenBattleShip", "ElvenShoreBombardShip", "ElvenFireShip", "TutorialElvenBattleShip",
		"ElvenTransportShip" };
	const std::vector<std::string> hover = { "CINE_HobbitBoat", "CINE_TrebeardA", "CINE_TrebeardB", "CINE_TrebeardC", "Cine_Wave", "CreateAHeroTornado_Level1",
		"CreateAHeroTornado_Level2", "CreateAHeroTornado_Level3", "EyeOfSauron", "EyeOfSauronFromRing", "Marker", "Crebain", "WildCaveBats", "WildBatCloud",
		"IsengardMurderOfCrows", "GaladrielTornado02", "GaladrielTornado", "ElrondWhirlwind", "InGameMarker", "InGameMarker2" };
	// LocomotorSet Speed 0: still by data
	const std::vector<std::string> still = { "CreateAHeroFamiliar_Base", "CreateAHeroFamiliar_Level1", "CreateAHeroFamiliar_Level2", "CreateAHeroFamiliar_Level3",
		"ElvenWhirlpool" };
	std::vector<std::string> inventory;
	for (ThingTemplate *tt : logic.things().templates())
	{
		for (const auto &kv : logic.aiWorld()->movementInfo(*tt).locomotorSets.slots())
		{
			bool any = false;
			for (const LocomotorTemplate *lt : kv.second.locomotors)
			{
				any = any || (lt && (lt->m_appearance == LOCO_SHIP || lt->m_appearance == LOCO_HOVER));
			}
			if (any)
			{
				inventory.push_back(tt->getName());
				break;
			}
		}
	}
	std::vector<std::string> expected = ships;
	expected.insert(expected.end(), hover.begin(), hover.end());
	expected.insert(expected.end(), still.begin(), still.end());
	std::sort(inventory.begin(), inventory.end());
	std::sort(expected.begin(), expected.end());
	CHECK(inventory == expected);

	// the object is looked up by id every frame: ElvenWhirlpool's LifetimeUpdate ends it inside the run (the last position seen stands)
	auto run = [&](const std::string &name, Player *owner, const Coord3D &from, const Coord3D &to, bool ship) {
		Object *o = arena.make(name.c_str(), owner, from.x, from.y);
		const ObjectID id = o->getID();
		arena.step(2);
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		REQUIRE_MESSAGE(ai != nullptr, name);
		const Coord3D start = *o->getPosition();
		const float speed = ai->locomotorSetSpeed();
		ai->aiMoveToPosition(to, CMD_FROM_PLAYER);
		WaterWatch w;
		Coord3D last = start;
		int lived = 0;
		for (int f = 0; f < 150; ++f)
		{
			arena.step();
			o = logic.findObjectByID(id);
			if (!o || o->isEffectivelyDead())
			{
				break;
			}
			++lived;
			last = *o->getPosition();
			if (ship)
			{
				w.see(logic, *o);
			}
		}
		const float moved = std::sqrt((last.x - start.x) * (last.x - start.x) + (last.y - start.y) * (last.y - start.y));
		const float left = std::sqrt((last.x - to.x) * (last.x - to.x) + (last.y - to.y) * (last.y - to.y));
		MESSAGE(name << ": moved " << moved << ", " << left << " from the goal, speed " << speed << ", z " << last.z << ", " << lived << " frames");
		if ((o = logic.findObjectByID(id)) != nullptr)
		{
			logic.destroyObject(o);
		}
		arena.step();
		return std::make_tuple(moved, left, w, speed);
	};
	for (const std::string &n : ships)
	{
		// from deep water 300 along the lake's south strip
		const auto r = run(n, arena.b, Coord3D{ 2400.0f, 1800.0f, 0.0f }, Coord3D{ 2700.0f, 1800.0f, 0.0f }, true);
		CHECK_MESSAGE(std::get<1>(r) < 20.0f, n); // tolerance: arrived within the braking overshoot (150 frames is past every ship's 300 / speed)
		CHECK_MESSAGE(std::get<2>(r).dry == 0, n);
		CHECK_MESSAGE(std::get<2>(r).offLevel == 0, n);
	}
	for (const std::string &n : hover)
	{
		// on the south shore's land, 150 east
		const auto r = run(n, arena.b, Coord3D{ 2000.0f, 600.0f, 0.0f }, Coord3D{ 2150.0f, 600.0f, 0.0f }, false);
		CHECK_MESSAGE(std::get<1>(r) < 20.0f, n); // tolerance: arrived within the braking overshoot
	}
	for (const std::string &n : still)
	{
		const auto r = run(n, arena.b, Coord3D{ 2000.0f, 600.0f, 0.0f }, Coord3D{ 2150.0f, 600.0f, 0.0f }, false);
		CHECK_MESSAGE(std::get<3>(r) == 0.0f, n);
		CHECK_MESSAGE(std::get<0>(r) == 0.0f, n);
	}
	std::string failures;
	for (const auto &f : logic.aiWorld()->movementFailures())
	{
		failures += f.first + "; ";
	}
	CHECK_MESSAGE(failures.empty(), failures);
}

TEST_CASE("ship1 retail: S-4161: the polygon walk's stale first backward edge is reported for a polygon that has one; a ship's rectangle never has one")
{
	OPENBFME_REQUIRE_START(s);
	ShipArena arena(*s, "FactionMen", "FactionMordor");
	Pathfinder &pf = arena.logic().aiWorld()->pathfinder();
	auto noted = [&pf]() {
		int n = 0;
		for (const std::string &st : pf.stops())
		{
			n += st == pathstops::kShoreScanStaleEdge ? 1 : 0;
		}
		return n;
	};
	REQUIRE(noted() == 0);
	// the rectangles RW 0x6EC931 builds: the transport ships' 16 x 7 cells and the battle ships' 24 x 10 (GeometryMajorRadius 60 / 100, minor 15 / 30, plus 20 a
	// side) and every small size, at every thousandth of a radian: the top corner's row-mate is always its forward neighbour, the stop is never raised
	for (const ICoord2D size : { ICoord2D{ 16, 7 }, ICoord2D{ 24, 10 }, ICoord2D{ 2, 2 }, ICoord2D{ 3, 1 }, ICoord2D{ 1, 4 }, ICoord2D{ 5, 3 } })
	{
		for (int k = 0; k < 6284; ++k)
		{
			pf.iterateCellsInRotatedRect(ICoord2D{ 200, 114 }, size, (float)k * 0.001f, LAYER_GROUND, [](PathfindCell &, int, int) { return 0; });
		}
	}
	CHECK(noted() == 0);
	// a triangle whose top corner (10, 10) has its backward neighbour (15, 10) on the same row: the first backward edge spans 0 rows. RW keeps that edge's
	// stale x / step (a stack slot); the port's start at 0 makes the first row run from cell 0 to the forward edge's x, (10 x 256 + 102 / 2 + 128) / 256 = 10
	int xs[3] = { 10, 12, 15 }, ys[3] = { 10, 15, 10 };
	std::vector<ICoord2D> row10;
	pf.iterateCellsInPolygon(xs, ys, 3, LAYER_GROUND, [&row10](PathfindCell &, int x, int y) {
		if (y == 10)
		{
			row10.push_back(ICoord2D{ x, y });
		}
		return 0;
	});
	CHECK(noted() == 1);
	REQUIRE(row10.size() == 11u);
	CHECK(row10.front().x == 0);
	CHECK(row10.back().x == 10);
	CHECK(std::string(pathstops::kShoreScanStaleEdge).rfind("S-4161 ", 0) == 0);
}
