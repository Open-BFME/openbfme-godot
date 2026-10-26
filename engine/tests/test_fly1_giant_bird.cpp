// OpenBFME unit tests. GPL-3.0.
// Lane FLY-1: the GIANT_BIRD flyers (GameLogic/AI/AerialPathfinder.h, GameLogic/Module/GiantBirdAIUpdate.h, the GIANT_BIRD movers of
// GameLogic/Object/LocomotorGiantBird.cpp). The retail cases SKIP without the games (ROTWK_INSTALL / BFME2_INSTALL).

#include "doctest.h"

#include "CampaignTestUtil.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AerialPathfinder.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
// ---- the spline alone ---------------------------------------------------------------------------------------------------------------------------------
AerialSpline straightSpline()
{
	AerialSpline s;
	// five control points on the x axis, 100 apart: segments between points 1-2 and 2-3 (count - 3 = 2)
	for (int i = 0; i < 5; ++i)
	{
		s.append(Coord3D{ (float)(i * 100), 0.0f, 50.0f });
	}
	s.prepare();
	s.build();
	return s;
}
} // namespace

TEST_CASE("fly1 spline: a Catmull-Rom path through evenly spaced points on a line is that line, walked by arc length (RW 0x7139E6 / 0x713242 / 0x777DAE)")
{
	AerialSpline s = straightSpline();
	REQUIRE(s.records().size() == 5);
	// each segment's duration is the sum of its ten chord samples: 100 for equal spacing (the Catmull-Rom of collinear equal steps is linear)
	CHECK(s.records()[0].duration == doctest::Approx(100.0f).epsilon(1e-5));
	CHECK(s.records()[1].duration == doctest::Approx(100.0f).epsilon(1e-5));
	CHECK(s.endPoint().x == 300.0f); // RW 0x713FFB: the control point count - 2
	Coord3D p;
	REQUIRE(AerialPathfinder::query(s, 0.0f, p));
	CHECK(p.x == doctest::Approx(100.0f));
	REQUIRE(AerialPathfinder::query(s, 150.0f, p));
	CHECK(p.x == doctest::Approx(250.0f).epsilon(1e-4));
	CHECK(p.y == 0.0f);
	CHECK(p.z == 50.0f);
	// past the last segment: no point (the mover's "reached")
	CHECK_FALSE(AerialPathfinder::query(s, 200.5f, p));
	CHECK_FALSE(AerialPathfinder::query(s, -1.0f, p));
}

TEST_CASE("fly1 spline: fixEndpoints puts the phantom end points on the line through their neighbours at the next point's distance (RW 0x71343E)")
{
	AerialSpline s;
	s.append(Coord3D{ 0.0f, 50.0f, 0.0f }); // off the line
	s.append(Coord3D{ 100.0f, 0.0f, 0.0f });
	s.append(Coord3D{ 140.0f, 0.0f, 0.0f });
	s.append(Coord3D{ 200.0f, 0.0f, 0.0f });
	s.append(Coord3D{ 300.0f, 30.0f, 0.0f });
	s.fixEndpoints();
	// the first point stays on its own direction from point 1 but at |p1 - p2| = 40; the last on its own direction from point 3 at |p3 - p2| = 60
	const Coord3D a = s.controlPoint(0), b = s.controlPoint(4);
	CHECK(std::sqrt((a.x - 100.0f) * (a.x - 100.0f) + a.y * a.y) == doctest::Approx(40.0f).epsilon(1e-5));
	CHECK(std::sqrt((b.x - 200.0f) * (b.x - 200.0f) + b.y * b.y) == doctest::Approx(60.0f).epsilon(1e-5));
	CHECK(a.y > 0.0f);
	CHECK(b.y > 0.0f);
}

// ---- retail ----------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
int flyFactionIndex(const PlayerTemplateStore &store, const std::string &name)
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

Player *flyPlayerAtStart(GameLogic &logic, int start)
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

// a retail skirmish game on Evendim between two factions, its starting castles in their corners; objects are made at will in the open middle
struct FlyArena
{
	starttest::Shared &s;
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *a = nullptr, *b = nullptr;

	FlyArena(starttest::Shared &shared, const char *factionA, const char *factionB, unsigned seed = 2, const char *map = "maps/map mp evendim/map mp evendim.map")
		: s(shared)
	{
		static std::vector<MapCacheEntry> cache;
		std::string error;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		NewGameMessage m;
		m.game.mapName = map;
		m.game.seed = seed;
		m.game.startingCash = 1500;
		const char *factions[2] = { factionA, factionB };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishGameSlot &slot = m.game.slots[i];
			slot.state = SLOT_PLAYER;
			slot.name = i == 0 ? u"Player0" : u"Player1";
			slot.playerTemplate = flyFactionIndex(s.world->playerTemplates(), factions[i]);
			REQUIRE(slot.playerTemplate >= 0);
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
		a = flyPlayerAtStart(logic(), 0);
		b = flyPlayerAtStart(logic(), 1);
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

GiantBirdAIUpdate *birdAI(Object *o)
{
	return o ? dynamic_cast<GiantBirdAIUpdate *>(o->getAIUpdateInterface()) : nullptr;
}
} // namespace

TEST_CASE("fly1 retail: a fell beast ordered to a point takes off, climbs to its PreferredHeight 121 over the ground, flies there and idles in the air")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	GameLogic &logic = arena.logic();
	Object *beast = arena.make("MordorWitchKingOnFellBeast", arena.a, 1500.0f, 800.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE_MESSAGE(ai != nullptr, "MordorWitchKingOnFellBeast's GiantBirdAIUpdate runs as its own class (S-222 / S-1994 closed)");
	arena.step(2);
	const Coord3D goal{ 2100.0f, 800.0f, 0.0f };
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FLY_TO);
	CHECK(ai->goalType() == 1);
	float highest = 0.0f;
	float startX = beast->getPosition()->x;
	bool movingSeen = false;
	int f = 0;
	for (; f < 300; ++f)
	{
		arena.step();
		highest = std::max(highest, arena.above(*beast));
		movingSeen = movingSeen || beast->testModelCondition(AIUpdateInterface::modelConditionBit("MOVING"));
		// lane PLAY-4 (merge q7): the move's end goes through AIGoingIdleState 0x4E, whose StancesBehavior::onMoveEnd (RW 0x86228E) puts the unit on guard
		// (AIGuardState 16, RW 0x74109B): the arrived beast is in the guard state, not the idle one
		if (ai->currentStateId() == (unsigned)AI_GUARD && f > 5)
		{
			break;
		}
	}
	MESSAGE("frames " << f << ", highest " << highest << ", at " << beast->getPosition()->x << "," << beast->getPosition()->y << "," << beast->getPosition()->z
					  << ", above " << arena.above(*beast) << ", state " << ai->currentStateId());
	CHECK(f < 300);
	CHECK(movingSeen);
	CHECK(beast->getPosition()->x > startX + 500.0f);
	CHECK(std::fabs(beast->getPosition()->x - goal.x) < 40.0f);
	CHECK(std::fabs(beast->getPosition()->y - goal.y) < 40.0f);
	CHECK(highest > 100.0f);
	CHECK(arena.above(*beast) > 90.0f); // the idle hover holds it near the preferred height (RW 0x5E4CD3)
	CHECK(beast->testStatus((unsigned)CombatNames::status("AIRBORNE_TARGET"))); // over the AirborneTargetingHeight 25
	// the path build noted the lane's two stops in the world's report, once each (AIWorld::noteStop)
	const std::vector<std::string> raised = logic.aiWorld()->stops();
	const std::string bird = GiantBirdAIUpdate::stopLines()[0], aerial = AerialPathfinder::stopLines()[0];
	CHECK(std::count(raised.begin(), raised.end(), bird) == 1);
	CHECK(std::count(raised.begin(), raised.end(), aerial) == 1);
	CHECK(bird.rfind("S-4040: ", 0) == 0);
	CHECK(bird.find("the attack-move's mood re-check clock (RW 0x662E01) is the MoodAttackCheckRate interval (INFERENCE)") != std::string::npos);
	CHECK(bird.find("the collision push of onCollide") != std::string::npos);
	CHECK(aerial.rfind("S-4041: ", 0) == 0);
	CHECK(aerial.find("without the no-fly zones") != std::string::npos);
}

// ---- combat ----------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
float health(Object *o)
{
	return o && o->getBodyModule() ? o->getBodyModule()->getHealth() : -1.0f;
}

// the beast's strikes so far (AIGiantBirdAttack fired a weapon: GiantBirdAIUpdate::strikes)
unsigned long long strikesOf(Object *beast)
{
	GiantBirdAIUpdate *ai = birdAI(beast);
	return ai ? ai->strikes() : 0;
}
} // namespace

TEST_CASE("fly1 retail: a fell beast ordered against an enemy barracks swoops and each strike of NazgulPlowAttack takes the retail 565 health (350 + 175 SIEGE at 100%, 40 FLY_INTO at the DEFAULT 40%)")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	GameLogic &logic = arena.logic();
	Object *beast = arena.make("MordorWitchKingOnFellBeast", arena.a, 1500.0f, 800.0f);
	Object *barracks = arena.make("GondorBarracks", arena.b, 1900.0f, 800.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	arena.step(2);
	const float full = health(barracks);
	REQUIRE(full > 1000.0f);
	REQUIRE(ai->aiAttackObject(barracks, CMD_FROM_PLAYER));
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_SWOOP);
	// the first strike: AIGiantBirdAttack fires (RW 0x76B668); the plow's nuggets land at once (500 inner) and a frame later (250 outer, 100 FLY_INTO)
	int f = 0;
	for (; f < 300 && strikesOf(beast) == 0; ++f)
	{
		arena.step();
	}
	REQUIRE_MESSAGE(strikesOf(beast) == 1, "no strike in " << f << " frames: state " << ai->currentStateId() << ", swoops " << ai->swoopsStarted());
	ObjectWeapons *w = beast->getWeapons();
	REQUIRE(w != nullptr);
	REQUIRE(w->currentWeapon() != nullptr);
	CHECK(w->currentWeapon()->getTemplate()->getName() == "NazgulPlowAttack"); // PreferredAgainst STRUCTURE; the grab cannot hold a barracks
	arena.step(3);
	MESSAGE("first strike after " << f << " frames; barracks " << full << " -> " << health(barracks));
	CHECK(full - health(barracks) == 565.0f);
	// the follow-through ends with the barracks alive: the swoop comes again (state 0x3F5's failure id 0x3F2) and strikes again
	for (f = 0; f < 400 && strikesOf(beast) < 2; ++f)
	{
		arena.step();
	}
	REQUIRE(strikesOf(beast) == 2);
	arena.step(3);
	CHECK(full - health(barracks) == 1130.0f);
	(void)logic;
}

TEST_CASE("fly1 retail: a fell beast ordered against a GondorFighterHorde grabs a soldier (GRAB_AND_DROP) with NazgulGrabAttack, carries it up, tosses it past GrabTossHeightTrigger 100 and the fall kills it")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	GameLogic &logic = arena.logic();
	Object *beast = arena.make("KhamulFellBeast", arena.a, 1500.0f, 800.0f);
	Object *horde = arena.make("GondorFighterHorde", arena.b, 1900.0f, 800.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	// the weapon set shares its ready status (ReadyStatusSharedWithinSet): the swoop's choice (RW 0x6C8A23 with PREFER_GRAB_OVER_DAMAGE) keeps the current weapon
	// while any is not ready, and a new beast's NazgulClawAttack loads its clip (ClipReloadTime 2 s) first
	arena.step(15);
	REQUIRE(horde->getContain() != nullptr);
	const unsigned members0 = horde->getContain()->getContainCount();
	REQUIRE(members0 > 0);
	{
		Object *m0 = horde->getContain()->getContainedItemsList()->front();
		REQUIRE(beast->getContain() != nullptr); // TransportContain (Slots 1, AllowEnemiesInside, PassengerFilter +GRAB_AND_DROP)
		CHECK(beast->getContain()->isValidContainerFor(*m0, true, false));
	}
	REQUIRE(ai->aiAttackObject(horde, CMD_FROM_AI));
	ObjectID grabbed = INVALID_ID;
	bool carried = false, freefall = false;
	// the beast's height over the ground at the end of the last two frames it held the soldier
	float birdCarrying = -1.0f, birdCarryingBefore = -1.0f;
	int grabFrame = -1, releaseFrame = -1;
	int f = 0;
	for (; f < 400; ++f)
	{
		arena.step();
		if (grabbed == INVALID_ID && ai->riderId() != INVALID_ID)
		{
			grabbed = ai->riderId();
		}
		if (f == 0)
		{
			const Weapon *cw = beast->getWeapons() ? beast->getWeapons()->currentWeapon() : nullptr;
			CHECK((cw && cw->getTemplate()->getName() == "NazgulGrabAttack")); // PREFER_GRAB_OVER_DAMAGE: the grab weapon's contain takes the soldier
			CHECK(((ai->flight().flags >> 4) & 1u) == 1u);                       // the grab flag (+ 0x57C bit 4) with the drop-kill bit 8
			CHECK(((ai->flight().flags >> 8) & 1u) == 1u);
		}
		if (grabbed != INVALID_ID)
		{
			Object *soldier = logic.findObjectByID(grabbed);
			if (soldier && soldier->getContainedBy() == beast)
			{
				grabFrame = carried ? grabFrame : f;
				carried = true;
				birdCarryingBefore = birdCarrying;
				birdCarrying = arena.above(*beast);
			}
			else if (carried && releaseFrame < 0)
			{
				releaseFrame = f;
			}
			if (soldier && soldier->testModelCondition(AIUpdateInterface::modelConditionBit("FREEFALL")) && !soldier->getContainedBy())
			{
				freefall = true;
			}
			if (carried && (!soldier || soldier->isEffectivelyDead()))
			{
				break;
			}
		}
	}
	MESSAGE("grabbed #" << grabbed << ", strikes " << ai->strikes() << ", grabbed at frame " << grabFrame << ", released at frame " << releaseFrame << ": the beast " << birdCarryingBefore << " then "
						<< birdCarrying << " over the ground before it, freefall " << freefall << ", ended at frame " << f);
	REQUIRE(grabbed != INVALID_ID);
	Object *soldier = logic.findObjectByID(grabbed);
	if (soldier)
	{
		CHECK(soldier->getTemplate()->getName() == "GondorFighter");
	}
	CHECK(carried);
	// the toss (RW 0x76CB71): the soldier goes once the beast's height over the terrain (RW 0x70B8AE) passes GrabTossHeightTrigger 100 (+ 0x70, RW 0x76CC2E), or
	// once he was held GrabTossTimeTrigger 2.5 s (+ 0x6C). The toss runs first in the update (RW 0x76CDFB, before the move), so it reads the height the frame
	// before left: the height came first, held under 2.5 s, at or below 100 two frames before the release, above 100 the frame before it
	CHECK(releaseFrame > grabFrame); // tolerance: the release follows the grab, its frame is the flight's
	CHECK(grabFrame >= 0); // tolerance: the grab happened, its frame is the swoop's
	CHECK((float)(releaseFrame - grabFrame) < 2.5f * (float)LOGICFRAMES_PER_SECOND); // tolerance: RW 0x76CC13's time trigger not reached
	CHECK(birdCarryingBefore <= 100.0f); // tolerance: RW 0x76CC2E's comparison, the trigger not yet passed
	CHECK(birdCarrying > 100.0f);        // tolerance: RW 0x76CC2E's comparison, the trigger passed
	CHECK(ai->strikes() == 1);
	CHECK(freefall);
	CHECK((!soldier || soldier->isEffectivelyDead()));
	CHECK(ai->riderId() == INVALID_ID);
}

TEST_CASE("fly1 retail: a GondorArcherHorde ordered against a hovering fell beast (AIRBORNE_TARGET, the bow's AntiAirborneMonster) hits it")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	// Khamul's fell beast: no Dread Visage (the Witch-king's RadiateFearUpdate puts the archers in a locked panic that refuses orders, as in retail)
	Object *beast = arena.make("KhamulFellBeast", arena.a, 1500.0f, 800.0f, 121.0f);
	Object *archers = arena.make("GondorArcherHorde", arena.b, 1620.0f, 800.0f);
	arena.step(5);
	REQUIRE(beast->testStatus((unsigned)CombatNames::status("AIRBORNE_TARGET")));
	const float full = health(beast);
	AIUpdateInterface *hai = archers->getAIUpdateInterface();
	REQUIRE(hai != nullptr);
	REQUIRE(hai->aiAttackObject(beast, CMD_FROM_AI)); // a fresh horde refuses a PLAYER order while it forms (the gate RW 0x667174)
	int f = 0;
	for (; f < 200 && health(beast) == full; ++f)
	{
		arena.step();
	}
	MESSAGE("first loss after " << f << " frames: " << full << " -> " << health(beast) << ", the beast " << arena.above(*beast) << " above the ground");
	CHECK(health(beast) < full);
	CHECK(arena.above(*beast) > 60.0f); // hit in the air, not after it landed
}

TEST_CASE("fly1 retail: control: a GondorArcherHorde without an order leaves the hovering fell beast unhurt (the arena turns auto-acquire off)")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	Object *beast = arena.make("KhamulFellBeast", arena.a, 1500.0f, 800.0f, 121.0f);
	arena.make("GondorArcherHorde", arena.b, 1620.0f, 800.0f);
	arena.step(5);
	const float full = health(beast);
	arena.step(100);
	CHECK(health(beast) == full); // auto-acquire is off in the arena: any loss in the case above is the ordered attack's
}

TEST_CASE("fly1 retail: an eagle (GondorGwaihir) ordered against a hovering fell beast swoops and claws it: EagleClawAttack 500 x the HERO scalar 30% x NazgulArmor SIEGE 100% = 150 a strike")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMen", "FactionMordor");
	Object *eagle = arena.make("GondorGwaihir", arena.a, 1300.0f, 800.0f, 121.0f);
	Object *beast = arena.make("MordorWitchKingOnFellBeast", arena.b, 1700.0f, 800.0f, 121.0f);
	GiantBirdAIUpdate *eai = birdAI(eagle);
	REQUIRE(eai != nullptr);
	arena.step(3);
	const float full = health(beast);
	REQUIRE(eai->aiAttackObject(beast, CMD_FROM_PLAYER));
	int f = 0;
	for (; f < 300 && strikesOf(eagle) == 0; ++f)
	{
		arena.step();
	}
	REQUIRE_MESSAGE(strikesOf(eagle) == 1, "no strike in " << f << " frames");
	ObjectWeapons *w = eagle->getWeapons();
	REQUIRE(w != nullptr);
	REQUIRE(w->currentWeapon() != nullptr);
	CHECK(w->currentWeapon()->getTemplate()->getName() == "EagleClawAttack"); // OnlyAgainst MONSTER: the fell beast is one
	arena.step(3);
	MESSAGE("eagle strike after " << f << " frames: " << full << " -> " << health(beast));
	CHECK(full - health(beast) == 150.0f);
}

TEST_CASE("fly1 retail: Drogoth's breath: his fireball (SpecialAbilityDrogothFireball, DrogothFireball's warhead) cast at an enemy horde from the air hurts it")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionWild", "FactionMen");
	GameLogic &logic = arena.logic();
	Object *drogoth = arena.make("Drogoth", arena.a, 1500.0f, 800.0f, 121.0f);
	Object *horde = arena.make("GondorFighterHorde", arena.b, 1750.0f, 800.0f);
	REQUIRE(birdAI(drogoth) != nullptr);
	arena.step(3);
	const SpecialPowerModule *sp = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : drogoth->modules())
	{
		const SpecialPowerModule *x = dynamic_cast<const SpecialPowerModule *>(m.get());
		if (x && x->getSpecialPowerTemplate() && x->getSpecialPowerTemplate()->getName() == "SpecialAbilityDrogothFireball")
		{
			sp = x;
		}
	}
	REQUIRE(sp != nullptr);
	float total0 = 0.0f;
	std::vector<ObjectID> members;
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
		total0 += health(m);
	}
	GameMessage cast(MSG_DO_SPECIAL_POWER_AT_OBJECT, drogoth->getControllingPlayer()->getPlayerIndex());
	cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
	cast.appendObjectIDArgument(horde->getContain()->getContainedItemsList()->front()->getID());
	cast.appendIntegerArgument(0);
	cast.appendObjectIDArgument(drogoth->getID());
	arena.game->commands().append(cast);
	auto hordeHealth = [&]() {
		float t = 0.0f;
		for (ObjectID id : members)
		{
			Object *m = logic.findObjectByID(id);
			t += m && !m->isEffectivelyDead() ? health(m) : 0.0f;
		}
		return t;
	};
	int f = 0;
	for (; f < 200 && hordeHealth() == total0; ++f)
	{
		arena.step();
	}
	MESSAGE("the fireball's first damage after " << f << " frames: " << total0 << " -> " << hordeHealth() << ", Drogoth " << arena.above(*drogoth) << " above the ground");
	// DrogothFireballWarhead on each GondorFighter within DROGOTH_FIREBALL_RADIUS 30: 400 FLAME + 50 MAGIC + 400 MAGIC (DROGOTH_FIREBALL_DAMAGE_SIEGE, its 300 %
	// only on fell beasts and Gwaihir) at SoldierArmor's 100 % is 850, above GONDOR_SOLDIER_HEALTH 200: the soldiers the blast covers die, 200 each. Lane FLANK-1:
	// DrogothFireballProjectile is not NO_COLLIDE and its warhead's ProjectileCollidesWith has ENEMIES (RW 0x6CC79C), so the ball goes off on the first soldier it
	// touches, backed off its path (RW 0x85EA5F), not at the aimed soldier at the path's end: nine soldiers are inside the blast there (six before), 9 x 200
	CHECK(total0 - hordeHealth() == 1800.0f);
	int killed = 0;
	for (ObjectID id : members)
	{
		Object *m = logic.findObjectByID(id);
		killed += !m || m->isEffectivelyDead() ? 1 : 0;
	}
	CHECK(killed == 9);
}

TEST_CASE("fly1 retail: every template of the retail data with a GiantBirdAIUpdate, by exact name, runs the class and flies to a point")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	GameLogic &logic = arena.logic();
	std::vector<std::string> birds;
	for (const ThingTemplate *tt : logic.things().templates())
	{
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->behaviorModules().nuggets())
		{
			if (n.name == "GiantBirdAIUpdate")
			{
				birds.push_back(tt->getName());
				break;
			}
		}
	}
	std::sort(birds.begin(), birds.end());
	// RotWK 2.01 + BFME2 1.06 data (FactionMordor's fell beasts and their template, FactionWild's Drogoth, the eagles of Men and Elves, the spell book's dragon,
	// a cinematic dragon)
	const std::vector<std::string> expected = { "CINE_GrnDrgn_Flying", "Drogoth", "ElvenFortressEagle", "GondorGwaihir", "GondorGwaihir_Summoned", "KhamulFellBeast",
		"MordorFellBeast", "MordorFellBeastInterface", "MordorWitchKingOnFellBeast", "MorgomirFellBeast", "SpellBookDragonStrikeDragon" };
	CHECK(birds == expected);
	// one at a time on the same course (each removed before the next: the goal reservations and the separation of a hovering bird would steer the next one); the
	// goal is a clear cell (the flight's goal snap, RW 0x6FE456 in RW 0x778413, moves a goal off an obstacle cell)
	const Coord3D from{ 1500.0f, 800.0f, 0.0f }, to{ 2100.0f, 800.0f, 0.0f };
	for (const std::string &name : birds)
	{
		Object *o = arena.make(name.c_str(), arena.a, from.x, from.y);
		GiantBirdAIUpdate *ai = birdAI(o);
		CHECK_MESSAGE(ai != nullptr, name);
		if (!ai)
		{
			continue;
		}
		const ObjectID id = o->getID();
		arena.step(1);
		ai->aiMoveToPosition(to, CMD_FROM_PLAYER);
		int f = 0;
		for (; f < 150; ++f)
		{
			arena.step();
			Object *b = logic.findObjectByID(id);
			if (!b || (ai->currentStateId() == (unsigned)AI_IDLE && f > 3))
			{
				break;
			}
		}
		Object *b = logic.findObjectByID(id);
		CHECK_MESSAGE(b != nullptr, name << " is gone (a lifetime?)");
		if (b)
		{
			CHECK_MESSAGE((std::fabs(b->getPosition()->y - to.y) < 40.0f && std::fabs(b->getPosition()->x - to.x) < 40.0f), name << " at " << b->getPosition()->x << "," << b->getPosition()->y << " after " << f
																					   << " frames, anchor " << ai->flight().anchor.x << "," << ai->flight().anchor.y
																					   << ", goal " << ai->stateMachine().goalPosition().x << ","
																					   << ai->stateMachine().goalPosition().y);
			CHECK_MESSAGE(arena.above(*b) > ai->locomotorPreferredHeight() * 0.5f, name << " " << arena.above(*b) << " above the ground, PreferredHeight "
																						 << ai->locomotorPreferredHeight());
			logic.destroyObject(b);
			arena.step(2);
		}
	}
}

TEST_CASE("fly1 retail: the skirmish AI's attack-move (AITacticalAI: the group move, then armAttackMove) flies a fell beast toward a point and it strikes the enemy barracks it meets")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	GameLogic &logic = arena.logic();
	logic.combat().setAutoAcquireEnabled(true); // the attack-move's mood scan (RW 0x66844A)
	arena.clearCreeps(); // MoriarGoblinLair and its goblins beside the course would be the scan's first enemies
	Object *beast = arena.make("MordorWitchKingOnFellBeast", arena.a, 1300.0f, 800.0f);
	Object *barracks = arena.make("GondorBarracks", arena.b, 1700.0f, 820.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	arena.step(2);
	const float full = health(barracks);
	// RW 0x8F17EE AIGroup::groupAttackMoveToPosition: the member's attack move (AI command 0xF -> RW 0x76A09D: GiantBirdAttackMoveToState 0x3FB)
	ai->aiMoveToPosition(Coord3D{ 2300.0f, 800.0f, 0.0f }, CMD_FROM_AI);
	REQUIRE(ai->armAttackMove(ai->stateMachine().goalPosition(), CMD_FROM_AI));
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_ATTACK_MOVE);
	int f = 0;
	for (; f < 400 && health(barracks) == full; ++f)
	{
		arena.step();
	}
	MESSAGE("the barracks lost health after " << f << " frames: " << full << " -> " << health(barracks) << ", strikes " << ai->strikes());
	// one strike of NazgulPlowAttack: the inner nugget (DelayTime 0) lands at once, 500 SIEGE x 70 % STRUCTURE x FactoryArmor's SIEGE 100 % = 350; the outer
	// 250 SIEGE (x 70 % x 100 % = 175) and the 100 FLY_INTO (FactoryArmor DEFAULT 40 % = 40) a frame later: 565
	CHECK(ai->strikes() == 1);
	CHECK(full - health(barracks) == 350.0f);
	arena.step(3);
	CHECK(ai->strikes() == 1);
	CHECK(full - health(barracks) == 565.0f);
}

TEST_CASE("fly1 retail: a hunting fell beast (AI command 0x12: the hunt state with the bird's swoop-then-idle machine, RW 0x769335) picks the closest enemy, swoops and strikes it")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	GameLogic &logic = arena.logic();
	Object *beast = arena.make("KhamulFellBeast", arena.a, 1300.0f, 800.0f);
	arena.make("GondorBarracks", arena.b, 1800.0f, 820.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	arena.step(20);
	REQUIRE(ai->aiHunt(CMD_FROM_SCRIPT));
	// the hunt's victim (TheAI's closest enemy, the bird's victim + 0x584 once its swoop starts) and its health then; the strike takes from it
	ObjectID victim = INVALID_ID;
	float before = 0.0f;
	int f = 0;
	for (; f < 400; ++f)
	{
		arena.step();
		if (victim == INVALID_ID && ai->victimId() != INVALID_ID)
		{
			victim = ai->victimId();
			before = health(logic.findObjectByID(victim));
		}
		if (victim != INVALID_ID && ai->strikes() >= 1)
		{
			break;
		}
	}
	arena.step(3);
	Object *v = logic.findObjectByID(victim);
	MESSAGE("the hunt chose #" << victim << " (" << (v ? v->getTemplate()->getName() : std::string("gone")) << "), struck after " << f << " frames: " << before << " -> "
							   << (v ? health(v) : 0.0f));
	REQUIRE(victim != INVALID_ID);
	REQUIRE(v != nullptr);
	// the closest enemy is the map's MoriarGoblinLair beside the course (a creep structure, enemy of both players), nearer than the barracks
	CHECK(v->getTemplate()->getName() == "MoriarGoblinLair");
	CHECK(ai->strikes() == 1);
	// NazgulPlowAttack on MonsterLair: 500 inner + 250 outer SIEGE x 70 % STRUCTURE x SIEGE 150 % = 525 + 262.5, 100 FLY_INTO x DEFAULT 50 % = 50: 837.5
	CHECK(before == 2000.0f);
	CHECK(before - health(v) == 837.5f);
}

// ---- lane STEALTHFLY1: MinFlightHeight, the follow states, command 0x39 ---------------------------------------------------------------------------------
namespace
{
// the map directory (lower case, backslashes) of a "...\map.ini" path
std::string mapDirOf(const std::string &mapIni)
{
	std::string d = mapIni.substr(0, mapIni.size() - std::string("\\map.ini").size());
	for (char &c : d)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return d;
}

// the 2D distance between two points
float dist2d(const Coord3D &a, const Coord3D &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// RW 0x76D84B's walk from a waypoint: its location, then along the first link until a waypoint without links or the first one again (true: a loop)
bool waypointChain(const TerrainLogic &t, const Waypoint &first, std::vector<Coord3D> &out)
{
	const Waypoint *w = &first;
	for (size_t guard = 0; guard <= t.waypoints().size(); ++guard)
	{
		out.push_back(w->location);
		if (w->linksTo.empty())
		{
			return false;
		}
		w = t.findWaypointById(w->linksTo.front());
		if (!w)
		{
			return false;
		}
		if (w == &first)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("stealthfly1 retail: a map's map.ini AIData block sets MinFlightHeight (row RW 0xC1CE60, AIData + 0xB0): map wor dol guldur 76 and map evil rivendell 300; "
		  "every other map leaves the constructor's FLT_MAX, and every retail map's AIData reads without an error")
{
	OPENBFME_REQUIRE_START(s);
	ArchiveFileSystem &fs = *s->mount->fs;
	FilenameList maps;
	fs.getFileListInDirectory("", "maps", "map.ini", maps, true);
	REQUIRE(maps.size() == 123); // the mounted RotWK 2.01 + BFME2 1.06 archives hold 123 maps/*/map.ini (a census of the effective file system)
	CHECK(s->settings.minFlightHeight == FLT_MAX); // data\ini\default\aidata.ini sets none
	std::map<std::string, float> set;
	std::vector<std::string> errors;
	for (const std::string &path : maps)
	{
		GameLogicSettings g = s->settings;
		CHECK_MESSAGE(GameLogicSettingsLoader::applyMapAIData(fs, mapDirOf(path), g, errors), path);
		if (g.minFlightHeight != FLT_MAX)
		{
			set[mapDirOf(path)] = g.minFlightHeight;
		}
		// the rest of the AIData is the global one (the two retail rows are MinFlightHeight and the client's LowLodTreeName)
		CHECK(g.formationColumnWidth == s->settings.formationColumnWidth);
		CHECK(g.repulsedDistance == s->settings.repulsedDistance);
	}
	for (const std::string &e : errors)
	{
		MESSAGE(e);
	}
	CHECK(errors.empty());
	const std::map<std::string, float> expected = { { "maps\\map evil rivendell", 300.0f }, { "maps\\map wor dol guldur", 76.0f } };
	CHECK(set == expected);
}

TEST_CASE("stealthfly1 retail: on map wor dol guldur the game reads its MinFlightHeight 76 and the aerial path's ground height is never below it (RW 0x777ED7: "
		  "max(terrain, no-fly height) raised to AIData + 0xB0); on Evendim it is the terrain")
{
	OPENBFME_REQUIRE_START(s);
	{
		FlyArena arena(*s, "FactionMordor", "FactionMen", 2, "maps/map wor dol guldur/map wor dol guldur.map");
		GameLogic &logic = arena.logic();
		CHECK(logic.settings().minFlightHeight == 76.0f);
		int below = 0, samples = 0;
		for (float x = 100.0f; x < 4000.0f; x += 300.0f) // tolerance: a sampling grid over the map, not an assertion
		{
			for (float y = 100.0f; y < 4000.0f; y += 300.0f) // tolerance: a sampling grid
			{
				const float ground = logic.getGroundHeight(x, y);
				const float flight = AerialPathfinder::flightGroundHeight(logic, x, y);
				CHECK(flight == (ground < 76.0f ? 76.0f : ground)); // tolerance: the retail rule's two cases, each asserted exactly
				below += ground < 76.0f ? 1 : 0; // tolerance: counting the samples below the floor
				++samples;
			}
		}
		MESSAGE(below << " of " << samples << " samples lie below 76");
		CHECK(below != 0); // tolerance: a precondition (some of the grid lies below the floor, so the rule is exercised)
	}
	{
		FlyArena arena(*s, "FactionMordor", "FactionMen");
		GameLogic &logic = arena.logic();
		CHECK(logic.settings().minFlightHeight == FLT_MAX);
		CHECK(AerialPathfinder::flightGroundHeight(logic, 1500.0f, 800.0f) == logic.getGroundHeight(1500.0f, 800.0f));
	}
}

TEST_CASE("stealthfly1 retail: a fell beast's queued waypoints (shift-click: AI vslot 0x84, RW 0x76D659) fly its own follow path state 0x3F6 (RW 0x76B491), "
		  "from its flight goal through each queued point, then idle in the air")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	Object *beast = arena.make("MordorWitchKingOnFellBeast", arena.a, 1500.0f, 800.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	arena.step(2);
	ai->aiMoveToPosition(Coord3D{ 1900.0f, 800.0f, 0.0f }, CMD_FROM_PLAYER);
	REQUIRE(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FLY_TO);
	arena.step(2);
	const Coord3D flightGoal = ai->stateMachine().goalPosition();
	const Coord3D second{ 1900.0f, 1200.0f, 0.0f }, third{ 1500.0f, 1200.0f, 0.0f };
	// in a flight state (0x3E9): a new path from the machine's goal position through the point
	ai->aiFollowPathAppend(second, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH);
	CHECK(ai->goalType() == 1);
	REQUIRE(ai->stateMachine().goalPath().size() == 2);
	CHECK(ai->stateMachine().goalPath()[0].x == flightGoal.x);
	CHECK(ai->stateMachine().goalPath()[0].y == flightGoal.y);
	CHECK(ai->stateMachine().goalPath()[1].x == second.x);
	CHECK(ai->stateMachine().goalPath()[1].y == second.y);
	CHECK(ai->stateMachine().goalPosition().x == second.x); // RW 0x8DB810: the path's last point
	CHECK((ai->flight().flags & 0x80u) == 0x80u);           // a look-ahead point: the second
	// RW 0x76A320: the leg to point 0 with point 1 as the look-ahead, both at the terrain height + the path height (x87 sum stored); RW 0x778413 with a
	// look-ahead appends point 0 and ends the spline at the look-ahead (the control point count - 2), which becomes the anchor the update steers to
	const float h = ai->pathHeight();
	CHECK(h == ai->curLocomotor()->preferredHeight());
	CHECK(ai->flight().anchor.x == second.x);
	CHECK(ai->flight().anchor.y == second.y);
	CHECK(ai->flight().anchor.z == SimMath::fstpDword(SimMath::pc24AddW((double)arena.logic().getGroundHeight(second.x, second.y), (double)h)));
	// following a path with points: the point is appended (RW 0x75536B)
	ai->aiFollowPathAppend(third, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH);
	REQUIRE(ai->stateMachine().goalPath().size() == 3);
	CHECK(ai->stateMachine().goalPath()[2].x == third.x);
	CHECK(ai->stateMachine().goalPath()[2].y == third.y);
	float closest[3] = { 1e9f, 1e9f, 1e9f };
	int closestFrame[3] = { -1, -1, -1 };
	const Coord3D pts[3] = { flightGoal, second, third };
	int f = 0;
	for (; f < 600; ++f)
	{
		arena.step();
		for (int i = 0; i < 3; ++i)
		{
			const float d = dist2d(*beast->getPosition(), pts[i]);
			if (d < closest[i]) // tolerance: tracking the nearest approach
			{
				closest[i] = d;
				closestFrame[i] = f;
			}
		}
		if (ai->currentStateId() != (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH && f > 2)
		{
			break;
		}
	}
	MESSAGE("left 0x3F6 after " << f << " frames in state " << ai->currentStateId() << "; nearest " << closest[0] << " @" << closestFrame[0] << ", " << closest[1] << " @"
								<< closestFrame[1] << ", " << closest[2] << " @" << closestFrame[2] << "; above the ground " << arena.above(*beast));
	CHECK(f < 600); // tolerance: the whole path flown within 120 s
	// the arrival test puts the bird on each leg's anchor (within one speed, RW 0x70C201): the anchors are the look-ahead points (the second, then the
	// third), so it passes over those in order; point 0 is only a control point of the first leg's spline, which the steering's rebuild toward the anchor
	// (RW 0x76A933 -> RW 0x769EA5 without a look-ahead) may cut
	CHECK(closest[1] < 40.0f); // tolerance: within one speed of the anchor (the arrival test)
	CHECK(closest[2] < 40.0f); // tolerance: as above
	CHECK(closestFrame[1] < closestFrame[2]); // tolerance: an order of frames
	CHECK((ai->flight().flags & 0x80u) == 0u); // the exit (RW 0x76AC9F) clears the look-ahead bit
	CHECK(arena.above(*beast) > 90.0f);         // tolerance: it hovers at about its PreferredHeight 121
	CHECK_FALSE(beast->testModelCondition(AIUpdateInterface::modelConditionBit("MOVING")));
}

TEST_CASE("stealthfly1 retail: a script's waypoint path order (AI commands 6 / 0x33, RW 0x76D84B) flies a fell beast along a campaign map's waypoint chain in "
		  "state 0x3F7 / 0x3F8 (RW 0x76A451 / 0x76B14A), the chain read along each waypoint's first link (the skirmish maps link none)")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map ang angmar");
	GameLogic &logic = m.logic();
	logic.combat().setAutoAcquireEnabled(false); // the bird flies its path, it does not stop for the mission's enemies
	REQUIRE(logic.terrain() != nullptr);
	const TerrainLogic &t = *logic.terrain();
	// the first waypoint (map order) that starts an open chain of at least three points
	const Waypoint *start = nullptr;
	std::vector<Coord3D> chain;
	int linked = 0;
	for (const Waypoint &w : t.waypoints())
	{
		linked += w.linksTo.empty() ? 0 : 1;
		std::vector<Coord3D> c;
		const bool loops = waypointChain(t, w, c);
		if (!start && !loops && c.size() >= 3)
		{
			start = &w;
			chain = c;
		}
	}
	MESSAGE(t.waypoints().size() << " waypoints, " << linked << " with links");
	REQUIRE_MESSAGE(start != nullptr, "map ang angmar has an open linked waypoint path");
	MESSAGE("waypoint '" << start->name << "' #" << start->id << ", a chain of " << chain.size() << " points");
	Player *me = logic.players().getLocalPlayer();
	REQUIRE(me != nullptr);
	const ThingTemplate *tt = logic.things().findTemplate("MordorWitchKingOnFellBeast");
	REQUIRE(tt != nullptr);
	Object *beast = logic.newObject(tt, me->getDefaultTeam(), {});
	REQUIRE(beast != nullptr);
	Coord3D at{ chain[0].x, chain[0].y, logic.getGroundHeight(chain[0].x, chain[0].y) };
	beast->setPosition(&at);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	const ObjectID id = beast->getID();
	m.step(2);
	REQUIRE(ai->aiFollowWaypointPath(start->id, CMD_FROM_SCRIPT, false, false)); // command 6
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_WAYPOINTS);
	CHECK_FALSE(ai->loopPath());
	REQUIRE(ai->stateMachine().goalPath().size() == chain.size());
	for (size_t i = 0; i < chain.size(); ++i)
	{
		CHECK(ai->stateMachine().goalPath()[i].x == chain[i].x);
		CHECK(ai->stateMachine().goalPath()[i].y == chain[i].y);
	}
	CHECK(ai->stateMachine().goalPosition().x == chain.back().x); // RW 0x8DB810: the path's last point
	CHECK(ai->pathHeight() == ai->curLocomotor()->preferredHeight()); // RW 0x76D86F: the command sets the path height
	// every leg's anchor is its look-ahead point (the next waypoint): the bird passes within one speed of each waypoint after the first and the open chain
	// ends (success: AIGoingIdleState 0x4E)
	std::vector<float> nearest(chain.size(), 1e9f);
	int f = 0;
	for (; f < 1500; ++f)
	{
		m.step();
		REQUIRE(logic.findObjectByID(id) != nullptr);
		for (size_t i = 0; i < chain.size(); ++i)
		{
			nearest[i] = std::min(nearest[i], dist2d(*beast->getPosition(), chain[i]));
		}
		if (ai->currentStateId() != (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_WAYPOINTS)
		{
			break;
		}
	}
	MESSAGE("left 0x3F7 after " << f << " frames for state " << ai->currentStateId() << ", nearest to the last waypoint " << nearest.back());
	CHECK(f < 1500); // tolerance: the open chain flown within 300 s
	for (size_t i = 1; i < chain.size(); ++i)
	{
		CHECK_MESSAGE(nearest[i] < 40.0f, "waypoint " << i << " nearest " << nearest[i]); // tolerance: within one speed of the anchor (the arrival test)
	}
	CHECK((ai->flight().flags & 0x80u) == 0u); // the exit (RW 0x76B228) clears the look-ahead bit
	// command 0x33 (exact, as a team) is the team state 0x3F8
	REQUIRE(ai->aiFollowWaypointPath(start->id, CMD_FROM_SCRIPT, true, true));
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_WAYPOINTS_TEAM);
}

TEST_CASE("stealthfly1 retail: AI command 0x39 (the scripts' attack of a giant bird, RW 0x77131B) swoops like the attack order and NazgulPlowAttack takes the "
		  "retail 565 from a barracks; an order queued while it attacks waits for the attack's end (RW 0x76D9B6)")
{
	OPENBFME_REQUIRE_START(s);
	FlyArena arena(*s, "FactionMordor", "FactionMen");
	Object *beast = arena.make("MordorWitchKingOnFellBeast", arena.a, 1300.0f, 800.0f);
	Object *barracks = arena.make("GondorBarracks", arena.b, 1700.0f, 820.0f);
	GiantBirdAIUpdate *ai = birdAI(beast);
	REQUIRE(ai != nullptr);
	arena.step(2);
	const float full = health(barracks);
	REQUIRE(ai->aiSwoopAttackObject(barracks, CMD_FROM_SCRIPT));
	CHECK(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_SWOOP);
	const Coord3D queued{ 1300.0f, 1200.0f, 0.0f };
	bool queuedWhileAttacking = false;
	int f = 0;
	for (; f < 400 && ai->strikes() == 0; ++f)
	{
		arena.step();
		if (!queuedWhileAttacking && ai->attacking())
		{
			ai->aiFollowPathAppend(queued, CMD_FROM_PLAYER);
			queuedWhileAttacking = true;
			CHECK(ai->currentStateId() != (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH); // kept for later
		}
	}
	REQUIRE(queuedWhileAttacking);
	arena.step(3);
	CHECK(ai->strikes() == 1);
	CHECK(full - health(barracks) == 565.0f); // 350 + 175 SIEGE at 100 %, 40 FLY_INTO at 40 % (the fly1 case)
	for (f = 0; f < 200 && ai->currentStateId() != (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH; ++f)
	{
		arena.step();
	}
	MESSAGE("the queued point's path after " << f << " frames past the strike");
	REQUIRE(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH);
	CHECK(ai->stateMachine().goalPath().back().x == queued.x);
	CHECK(ai->stateMachine().goalPath().back().y == queued.y);
}
