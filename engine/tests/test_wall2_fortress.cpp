// OpenBFME unit tests, lane WALL-2: a player's fortress wall with a postern gate (retail data). GPL-3.0.
//
// RotWK's player-built Men wall (MenWallHubSmallExpansion -> a span of MenWallSegmentSmall) is no WALK_ON_TOP_OF_WALL piece and has no WallBoundsMesh: its
// segments are plain footprints. Its postern gate MenWallPosternGateSmall (ReplaceSelfUpgrade of the segment, Upgrade_MenWallPosternGate) carries
// DynamicPortalBehaviour PosternGatePortal with GenerateNow = Yes (no TriggeredBy): the gate's waypoints are made when it is built (onBuildComplete), at the
// bones Post01 .. Post04 of its second draw module (ModuleTag_DoorDraw, model GBWallPGN, ExtraPublicBone). The retail tests SKIP loudly without the installs.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WallSpan.h"

#include <cmath>
#include <string>
#include <vector>

TEST_CASE("wall2 fortress retail: a Men wall span's segment upgraded to a postern gate makes the gate's waypoints when built; a hero crosses the wall through it")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player != nullptr);
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	Object *centre = nullptr, *plot = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		centre = (o && o->getControllingPlayer() == player) ? o : centre;
	}
	REQUIRE(centre != nullptr);
	float best = -1.0f;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("BASE_FOUNDATION"))
		{
			const float dx = o->getPosition()->x - centre->getPosition()->x, dy = o->getPosition()->y - centre->getPosition()->y;
			if (dx * dx + dy * dy > best)
			{
				best = dx * dx + dy * dy;
				plot = o; // the outermost pad
			}
		}
	}
	REQUIRE(plot != nullptr);
	const ThingTemplate *hubTmpl = logic.things().findTemplate("MenWallHubSmallExpansion");
	const ThingTemplate *capTmpl = logic.things().findTemplate("MenWallHubSmall");
	REQUIRE(hubTmpl != nullptr);
	REQUIRE(capTmpl != nullptr);
	Object *hub = Construction::constructOnPlot(*plot, *hubTmpl->getFinalOverride(), *plot->getPosition(), 0.0f, *player, true);
	REQUIRE(hub != nullptr);
	logic.runLogicFrame();
	// the span: 400 outward from the hub, away from the citadel
	Coord3D start = *hub->getPosition(), end = start;
	const float dx = start.x - centre->getPosition()->x, dy = start.y - centre->getPosition()->y;
	const float len = std::sqrt(dx * dx + dy * dy);
	const float ux = dx / len, uy = dy / len;
	end.x += ux * 400.0f;
	end.y += uy * 400.0f;
	WallSpan::Plan plan;
	REQUIRE(WallSpan::plan(logic, *hub, start, end, 1u << 13, plan));
	GameMessage m(MSG_WALL_HUB_CONSTRUCT_SPAN, player->getPlayerIndex());
	m.appendIntegerArgument((int)capTmpl->getFinalOverride()->getTemplateID());
	m.appendLocationArgument(start);
	m.appendLocationArgument(end);
	m.appendIntegerArgument((int)(1u << 13));
	m.appendObjectIDArgument(hub->getID());
	live.commands().append(m);
	logic.runLogicFrame();
	std::vector<ObjectID> tiles;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == hub->getID() && o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE"))
		{
			tiles.push_back(o->getID());
		}
	}
	REQUIRE(tiles.size() >= 4); // tolerance: a span of a hub, segments and a cap
	auto allBuilt = [&]() {
		for (ObjectID id : tiles)
		{
			const Object *o = logic.findObjectByID(id);
			if (o && o->getConstructionPercent() != -1.0f)
			{
				return false;
			}
		}
		return true;
	};
	for (int f = 0; f < 3000 && !allBuilt(); ++f)
	{
		logic.runLogicFrame();
	}
	REQUIRE(allBuilt());
	// the middle segment
	Object *seg = nullptr;
	float bestMid = 1e30f;
	const float midX = (start.x + end.x) * 0.5f, midY = (start.y + end.y) * 0.5f;
	for (ObjectID id : tiles)
	{
		Object *o = logic.findObjectByID(id);
		if (o && o->isKindOfName("WALL_SEGMENT"))
		{
			const float ex = o->getPosition()->x - midX, ey = o->getPosition()->y - midY;
			if (ex * ex + ey * ey < bestMid)
			{
				bestMid = ex * ex + ey * ey;
				seg = o;
			}
		}
	}
	REQUIRE(seg != nullptr);
	MESSAGE("span of " << tiles.size() << " tiles; the middle segment " << seg->getTemplate()->getName() << " at " << seg->getPosition()->x << ", " << seg->getPosition()->y);
	Pathfinder &pf = live.ai().pathfinder();
	// the two sides of the wall at the segment, 60 off its line
	const Coord3D at = *seg->getPosition();
	const Coord3D sideA{ at.x - uy * 60.0f, at.y + ux * 60.0f, 0.0f }, sideB{ at.x + uy * 60.0f, at.y - ux * 60.0f, 0.0f };
	std::string spawnError;
	Object *hero = live.createObject("GondorBoromir", player->getPlayerIndex(), sideA, 0.0f, &spawnError);
	REQUIRE_MESSAGE(hero, spawnError);
	logic.runLogicFrame();
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	auto pathLength = [&](bool &waypoints) {
		Coord3D from = *hero->getPosition(), to = sideB;
		bool partial = true;
		Path *p = pf.findPath(&ai->adapter(), ai->locomotorInfo(), &from, &to, &partial);
		float l = -1.0f;
		waypoints = false;
		if (p && !partial)
		{
			l = 0.0f;
			for (const PathNode *n = p->getFirstNode(); n && n->getNext(); n = n->getNext())
			{
				const float ax = n->getNext()->getPosition()->x - n->getPosition()->x, ay = n->getNext()->getPosition()->y - n->getPosition()->y;
				l += std::sqrt(ax * ax + ay * ay);
			}
			// the nodes the gate's waypoints mark (prependCells RW 0x6EF591; Path::hasWaypointNodes, RW 0x765522, also needs a current node: a path being
			// followed)
			for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
			{
				waypoints = waypoints || n->getWaypointID() != PathNode::NO_WAYPOINT;
			}
		}
		delete p;
		return l;
	};
	// the cell under the segment's centre is an obstacle (the wall's footprint), before and after the gate
	auto underIsObstacle = [&]() {
		const PathfindCell *c = pf.cellAt(SimMath::floorToInt(SimMath::mulf32(at.x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(at.y, 0.1f)));
		return c && c->getType() == PathfindCell::CELL_OBSTACLE;
	};
	CHECK(underIsObstacle());
	bool wpBefore = false;
	const float around = pathLength(wpBefore);
	MESSAGE("before the gate: the path across the wall is " << around << " long");
	CHECK_FALSE(wpBefore);
	CHECK(around > 300.0f); // tolerance: around the span's end, not through it (the sides are 120 apart)
	// the postern gate replaces the segment (in the port the replacement arrives built: its onBuildComplete makes the waypoints at once)
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_MenWallPosternGate") : nullptr;
	REQUIRE(u);
	const ObjectID segId = seg->getID();
	seg->giveUpgrade(u);
	Object *gate = nullptr;
	for (int f = 0; f < 60 && !gate; ++f)
	{
		logic.runLogicFrame();
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "MenWallPosternGateSmall" && o->getControllingPlayer() == player)
			{
				gate = o;
			}
		}
	}
	REQUIRE_MESSAGE(gate, "the segment did not become MenWallPosternGateSmall");
	CHECK(logic.findObjectByID(segId) == nullptr);
	DynamicPortalBehaviour *portal = DynamicPortalBehaviour::of(*gate);
	REQUIRE(portal);
	CHECK(underIsObstacle());
	for (int f = 0; f < 600 && gate->getConstructionPercent() != -1.0f; ++f)
	{
		logic.runLogicFrame();
	}
	REQUIRE(gate->getConstructionPercent() == -1.0f);
	MESSAGE("the gate at " << gate->getPosition()->x << ", " << gate->getPosition()->y);
	REQUIRE(portal->created());
	int found = 0;
	for (int id : portal->waypointIds())
	{
		const PathfindWaypoint *w = pf.findWaypoint(id);
		found += w ? 1 : 0;
		if (w)
		{
			const float ex = w->pos.x - gate->getPosition()->x, ey = w->pos.y - gate->getPosition()->y;
			CHECK(ex * ex + ey * ey < 60.0f * 60.0f); // tolerance: the Post bones stand at the gate
		}
	}
	CHECK(found == DynamicPortalBehaviour::MAX_WAYPOINTS);
	bool wpAfter = false;
	const float through = pathLength(wpAfter);
	MESSAGE("with the gate: the path across the wall is " << through << " long");
	CHECK(wpAfter);
	CHECK(through < around); // tolerance: the gate's way is shorter than the way around the span
	// the hero walks through
	ai->aiMoveToPosition(sideB, CMD_FROM_PLAYER);
	bool sawPortal = false;
	for (int f = 0; f < 600; ++f)
	{
		logic.runLogicFrame();
		sawPortal = sawPortal || ai->mover().onPortal();
		const Coord3D q = *hero->getPosition();
		if ((q.x - sideB.x) * (q.x - sideB.x) + (q.y - sideB.y) * (q.y - sideB.y) < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D q = *hero->getPosition();
	INFO("the hero stands at " << q.x << ", " << q.y);
	CHECK(sawPortal);
	CHECK((q.x - sideB.x) * (q.x - sideB.x) + (q.y - sideB.y) * (q.y - sideB.y) < 900.0f); // tolerance: within the close-enough distance of the goal
	// destroyed, the gate's portal goes (onDie): the path is around again
	gate->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	for (int f = 0; f < 5; ++f)
	{
		logic.runLogicFrame();
	}
	CHECK_FALSE(portal->created());
}
