// OpenBFME unit tests, lane MECH-D: wall climbing. GPL-3.0.
//
// A unit that can scale walls (KindOf CAN_CLIMB_WALLS and a ScalesWalls locomotor) walks up a SCALEABLE_WALL and over it (the locomotor dispatcher's
// look-ahead, RW 0x5E8956 .. 0x5E8FF8); a footman goes up a deployed siege ladder's portal onto the wall top and fights there; a climbing unit on a wall is
// in melee reach of a ground unit (RW 0x6EDAD4). SKIPs loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"
#include "PathfindTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/System/ShroudManager.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using hudtest::SharedWorld;
using hudtest::shared;

namespace
{
bool haveWorld(const char *what)
{
	return hudtest::haveWorld(what);
}

// two enemy sides on a flat arena with the retail economy and AI data (the COMBAT-1 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(seed);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		return o;
	}
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic.runLogicFrame();
		}
	}
};

// the player's special power order at an object (as test_siege1_deploy)
void orderPowerAt(hudtest::Rig &r, Object &unit, const char *powerName, Object *target)
{
	REQUIRE(TheSpecialPowerStore);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(powerName);
	REQUIRE(t);
	r.game->dispatch().dispatch([&] {
		r.local->selection() = { unit.getID() };
		GameMessage m(target ? MSG_DO_SPECIAL_POWER_AT_OBJECT : MSG_DO_SPECIAL_POWER, r.index());
		m.appendIntegerArgument((int)t->getID());
		if (target)
		{
			m.appendObjectIDArgument(target->getID());
		}
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(INVALID_ID);
		if (!target)
		{
			m.appendObjectIDArgument(INVALID_ID);
		}
		return m;
	}());
}

// the first ladder dock point of Helm's Deep (HelmsDeepSectionAi's SIEGELADDER bone) and a ladder 250 south of it (as test_siege1_deploy's LadderScene)
struct LadderScene
{
	Object *wall = nullptr;
	SiegeDockingBehavior::Entry entry;
	Object *ladder = nullptr;
	SiegeDeploySpecialPower *deploy = nullptr;
	explicit LadderScene(hudtest::Rig &r, const char *unit = "IsengardSiegeLadder")
	{
		for (Object *o = r.logic().getFirstObject(); o && !wall; o = o->getNextObject())
		{
			SiegeDockingBehavior *d = SiegeDockingBehavior::of(*o);
			for (size_t i = 0; d && i < d->entries().size() && !wall; ++i)
			{
				if (d->entries()[i].type == 1) // SIEGELADDER
				{
					wall = o;
					entry = d->entries()[i];
				}
			}
		}
		REQUIRE(wall);
		const Coord3D from = r.freeSpot(entry.position.x - entry.direction.x * 250.0f, entry.position.y - entry.direction.y * 250.0f, 60.0f);
		ladder = r.make(unit, from.x, from.y);
		r.frame(2);
		deploy = SiegeDeploySpecialPower::of(*ladder);
		REQUIRE(deploy);
	}
};
} // namespace

// ---- the locomotor's ScalesWalls look-ahead (RW 0x5E8956 .. 0x5E8FF8) ----
TEST_CASE("mechd: a unit that can climb walls (CAN_CLIMB_WALLS, a ScalesWalls locomotor) walks up a SCALEABLE_WALL's face, over its top and down the far side")
{
	if (!haveWorld("mechd climb"))
	{
		return;
	}
	const int climbing = AIUpdateInterface::modelConditionBit("CLIMBING");
	const int rappelling = AIUpdateInterface::modelConditionBit("RAPPELLING");
	Arena a(shared(), "FactionMen", "FactionMordor");
	// the wall across the path: its length along y (orientation pi/2), its face toward the climber at x 600
	const float wallX = 700.0f;
	Object *wall = a.place("DolGoldurWallSegmentSmall", 0, wallX, 600.0f, 1.5707964f);
	a.ai->addObjectToPathfindMap(*wall);
	a.run(2);
	// a retail 2.01 climber: Shelob (KindOf CAN_CLIMB_WALLS, ShelobWallScalingLocomotor with ScalesWalls)
	Object *climber = a.place("MordorShelob", 1, 600.0f, 600.0f, 0.0f);
	a.run(2);
	AIUpdateInterface *ai = climber->getAIUpdateInterface();
	REQUIRE(ai);
	REQUIRE(ai->curLocomotor());
	CHECK(ai->curLocomotor()->getTemplate().m_scalesWalls);
	CHECK(climber->isKindOfName("CAN_CLIMB_WALLS"));
	ai->aiMoveToPosition(Coord3D{ 800.0f, 600.0f, 0.0f }, CMD_FROM_PLAYER);
	float highest = 0.0f, farX = 0.0f, endZ = -1.0f;
	bool sawClimb = false, sawRappel = false;
	int crossedAt = -1;
	for (int f = 0; f < 600; ++f)
	{
		a.run(1);
		const Coord3D p = *climber->getPosition();
		highest = p.z > highest ? p.z : highest;
		farX = p.x > farX ? p.x : farX;
		sawClimb = sawClimb || climber->testModelCondition(climbing);
		sawRappel = sawRappel || climber->testModelCondition(rappelling);
		if (crossedAt < 0 && p.x > wallX + 12.0f)
		{
			crossedAt = f;
		}
		endZ = p.z;
	}
	const Coord3D at = *climber->getPosition();
	std::printf("  info: climb: highest z %.3f, far x %.3f, end (%.1f %.1f %.1f), CLIMBING %d RAPPELLING %d, crossed at %d\n", (double)highest, (double)farX,
				(double)at.x, (double)at.y, (double)at.z, sawClimb ? 1 : 0, sawRappel ? 1 : 0, crossedAt);
	CHECK(highest > 30.0f);          // tolerance: the wall is 40 high; a float step may stop a little below its top
	CHECK(farX > wallX + 10.0f);     // tolerance: past the wall's face (its half thickness is 10) on the far side
	CHECK(crossedAt >= 0); // tolerance: -1 means never crossed; a frame index starts at 0
	CHECK(sawClimb);
	CHECK(sawRappel);
	CHECK(endZ < 5.0f);              // tolerance: the arena's ground is 0 and the last rappel step lands exactly on it
}

// ---- the ladder's portal onto the wall and the fight there ----
TEST_CASE("mechd: a footman ordered onto a Helm's Deep wall top goes up a deployed siege ladder's portal, reaches the wall and fights a defender there")
{
	if (!haveWorld("mechd ladder"))
	{
		return;
	}
	hudtest::Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext();
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	// deploy the retail ladder; its own DynamicPortalBehaviour answers AboveWall and makes the climb's waypoints
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	int deployed = -1;
	for (int f = 1; f <= 160 && deployed < 0; ++f)
	{
		r.frame(1);
		if (sc.deploy->isDeployed())
		{
			deployed = f;
		}
	}
	REQUIRE(deployed > 0); // tolerance: -1 means the deploy never reached DEPLOYED; a frame index starts at 0
	const DynamicPortalBehaviour *portal = DynamicPortalBehaviour::of(*sc.ladder);
	REQUIRE(portal);
	REQUIRE(portal->created());
	Coord3D top{};
	REQUIRE(portal->isEndPortalAboveWall(&top));
	REQUIRE(r.logic().aiWorld());
	Pathfinder &pf = r.logic().aiWorld()->pathfinder();
	REQUIRE(pf.isPointOnWall(top, false));
	const PathfindLayerEnum wallLayer = pf.getLayerAt(top.x, top.y, top.z);
	REQUIRE(isWallLayer(wallLayer));
	// the footman on the ground beside the ladder
	const Coord3D from = r.freeSpot(sc.entry.position.x - sc.entry.direction.x * 190.0f, sc.entry.position.y - sc.entry.direction.y * 190.0f, 30.0f);
	REQUIRE(pf.getLayerAt(from.x, from.y, 0.0f) == LAYER_GROUND);
	Object *man = r.make("IsengardFighter", from.x, from.y);
	r.frame(2);
	AIUpdateInterface *manAi = man->getAIUpdateInterface();
	REQUIRE(manAi);
	// the wall-top goal a little along the wall from the ladder's top
	const Coord3D goal{ top.x, top.y, pf.getLayerHeight(top.x, top.y, wallLayer) };
	REQUIRE(pf.isPointOnWall(goal, false));
	std::printf("  info: man start (%.1f %.1f %.1f) layer %d, start cell layer %d, goal layer %d\n", (double)man->getPosition()->x, (double)man->getPosition()->y,
				(double)man->getPosition()->z, (int)manAi->adapter().getLayer(), (int)pf.getLayerAt(from.x, from.y, 0.0f), (int)pf.getLayerAt(goal.x, goal.y, goal.z));
	manAi->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	r.frame(1);
	bool sawPortalNode = false;
	if (const Path *path = manAi->mover().path())
	{
		int nodes = 0, ids = 0;
		for (const PathNode *n = path->getFirstNode(); n; n = n->getNext())
		{
			++nodes;
			ids += n->getWaypointID() != PathNode::NO_WAYPOINT ? 1 : 0;
			sawPortalNode = sawPortalNode || n->getWaypointID() != PathNode::NO_WAYPOINT;
		}
		std::printf("  info: path nodes %d ids %d first (%.1f %.1f %.1f) last (%.1f %.1f %.1f)\n", nodes, ids, (double)path->getFirstNode()->getPosition()->x,
					(double)path->getFirstNode()->getPosition()->y, (double)path->getFirstNode()->getPosition()->z, (double)path->getLastNode()->getPosition()->x,
					(double)path->getLastNode()->getPosition()->y, (double)path->getLastNode()->getPosition()->z);
	}
	else
	{
		std::printf("  info: no path after the order\n");
	}
	int arrived = -1;
	for (int f = 0; f < 900 && arrived < 0; ++f)
	{
		r.frame(1);
		const Coord3D p = *man->getPosition();
		const float dx = p.x - goal.x, dy = p.y - goal.y;
		if (dx * dx + dy * dy < 400.0f && !manAi->isMoving())
		{
			arrived = f;
		}
	}
	const Coord3D at = *man->getPosition();
	std::printf("  info: ladder climb: deployed %d, top (%.1f %.1f %.1f) layer %d, man at (%.1f %.1f %.1f) layer %d, arrived %d, portal node %d\n", deployed,
				(double)top.x, (double)top.y, (double)top.z, (int)wallLayer, (double)at.x, (double)at.y, (double)at.z, (int)manAi->adapter().getLayer(), arrived,
				sawPortalNode ? 1 : 0);
	CHECK(sawPortalNode); // the path crossed the ladder's portal, not a ramp
	REQUIRE(arrived >= 0); // tolerance: -1 means the man never arrived; a frame index starts at 0
	CHECK(isWallLayer(manAi->adapter().getLayer()));
	CHECK(at.z > 10.0f); // tolerance: the wall is about 50 above the terrain; 10 tells the wall top from the courtyard
	// off the ladder's top bone and onto the wall top proper: a short walk along the wall
	const float *wb = sc.wall->getBasis();
	const Coord3D walkGoal{ top.x + wb[0] * 25.0f, top.y + wb[3] * 25.0f, pf.getLayerHeight(top.x + wb[0] * 25.0f, top.y + wb[3] * 25.0f, wallLayer) };
	REQUIRE(pf.isPointOnWall(walkGoal, false));
	manAi->aiMoveToPosition(walkGoal, CMD_FROM_PLAYER);
	for (int f = 0; f < 120; ++f)
	{
		r.frame(1);
		const Coord3D p = *man->getPosition();
		const float dx = p.x - walkGoal.x, dy = p.y - walkGoal.y;
		if (dx * dx + dy * dy < 25.0f && !manAi->isMoving())
		{
			break;
		}
	}
	const Coord3D onWall = *man->getPosition();
	CHECK(isWallLayer(manAi->adapter().getLayer()));
	CHECK(onWall.z == pf.getLayerHeight(onWall.x, onWall.y, manAi->adapter().getLayer()));
	// the defender on the wall, and the fight there
	Object *defender = r.make("MordorFighter", onWall.x - 2.0f, onWall.y - 12.0f, r.game->players().findPlayerWithName("Player_2"));
	r.frame(1);
	// lane DECOMPFIX-1109 (FLEECHASE-1): the defender stands. Made beside the wall, its first frame gives it a move (and its locomotor that move's speed); kept,
	// it walks on against the wall's foot with the speed held, faces away from the footman and is a FLEEING target (RW 0x7468D5: facing away and the locomotor's
	// speed + 0x40 above a quarter of its maximum, RW 0x5E4CA7), so the melee approach hands over to the engage state at once (RW 0x746F51 .. 0x747010) and the
	// case no longer tested a fight with a standing defender
	if (AIUpdateInterface *dai = defender->getAIUpdateInterface())
	{
		dai->aiIdle(CMD_FROM_AI);
		dai->mover().destroyPath();
	}
	const Coord3D dp{ onWall.x - 2.0f, onWall.y - 12.0f, pf.getLayerHeight(onWall.x - 2.0f, onWall.y - 12.0f, wallLayer) };
	defender->setPosition(&dp);
	if (AIUpdateInterface *dai = defender->getAIUpdateInterface())
	{
		dai->setLayer(wallLayer);
	}
	const BodyModuleInterface *body = defender->getBodyModule();
	REQUIRE(body);
	const float health0 = body->getHealth();
	REQUIRE(manAi->aiAttackObject(defender, CMD_FROM_PLAYER));
	int firstHit = -1;
	for (int f = 0; f < 400 && firstHit < 0; ++f)
	{
		r.frame(1);
		const Object *d = r.logic().findObjectByID(defender->getID());
		if (d && d->getBodyModule() && d->getBodyModule()->getHealth() < health0)
		{
			firstHit = f;
		}
	}
	std::printf("  info: wall fight: first hit at %d\n", firstHit);
	CHECK(firstHit >= 0); // tolerance: -1 means no hit; a frame index starts at 0
}
