// OpenBFME unit tests, lane WALL-2: the pathfinder's portals (AIPathfindPortals.cpp). GPL-3.0.
//
// A synthetic map split by a wall (a structure's footprint across the whole map): a path to the far side exists only through a portal pair (a postern gate:
// two Walk waypoints linked both ways with an intermediate waypoint each, as DynamicPortalBehaviour's Link From / Via / To makes them). The expected values
// come from the rules ported from RW (CheckChangeLayers RW 0x6F6286, the portal zone search RW 0x6F5547, prependCells RW 0x6EF591, the hierarchical portal
// branch RW 0x6F909A, Waypoint::isValidUnitType RW 0x68229C), derived by hand in each test.

#include "doctest.h"

#include "MoveTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIPathfind.h"

#include <memory>
#include <string>
#include <vector>

using namespace pathtest;

namespace
{
// 40 x 20 cells; the wall: a cliff line over cells 19 .. 20 the whole height (classifyMap pinches 18 and 21 to cliffs too: cells 18 .. 21 block a ground unit,
// and, unlike a structure's footprint, a cliff splits the zones in the terrain variant as well). Its owner is a structure off the map's grid use (the wall
// piece, id 50). The postern gate: waypoints A at (175, 105) west of the wall and B at (235, 105) east of it, the intermediates M at (205, 105) (A's Via) and
// N at (195, 105) (B's Via)
struct PortalWorld
{
	SyntheticTerrain terrain{ 40, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	TestObject *wall = nullptr;
	TestObject *unit = nullptr;
	int a = 0, b = 0, m = 0, n = 0;
	explicit PortalWorld(bool portals = true, bool allowEnemies = false, bool via = true)
	{
		for (int y = 0; y < 20; ++y)
		{
			terrain.cliff.insert({ 19, y });
			terrain.cliff.insert({ 20, y });
		}
		wall = new TestObject();
		wall->id = 50;
		wall->pos = { 200.0f, 100.0f, 0.0f };
		wall->structure = true;
		world.objects[wall->id] = wall;
		unit = new TestObject();
		unit->id = 1;
		unit->pos = { 55.0f, 105.0f, 0.0f };
		unit->mobile = true;
		unit->geometry.type = PATHFIND_GEOMETRY_SPHERE;
		unit->geometry.majorRadius = 5.0f;
		world.objects[unit->id] = unit;
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		zones();
		if (portals)
		{
			auto make = [&](float x, float y) {
				PathfindWaypoint w;
				w.pos = { x, y, 0.0f };
				w.type = PWAYPOINT_WALK;
				w.owner = wall->id;
				w.allowEnemies = allowEnemies;
				return pf->createWaypoint(w);
			};
			a = make(175.0f, 105.0f);
			b = make(235.0f, 105.0f);
			m = make(205.0f, 105.0f);
			n = make(195.0f, 105.0f);
			// Link From:A Via:M To:B and Link From:B Via:N To:A (DynamicPortalBehaviour::createWaypoints RW 0x86404D)
			pf->addWaypointLink(a, b);
			pf->addPortal(a);
			if (via)
			{
				pf->setWaypointNext(a, m);
			}
			pf->addWaypointLink(b, a);
			pf->addPortal(b);
			if (via)
			{
				pf->setWaypointNext(b, n);
			}
		}
	}
	void zones()
	{
		struct None : PathfindRequestHandler
		{
			void doPathfind(PathfindObjectID) override {}
		} none;
		pf->processPathfindQueue(none);
	}
	~PortalWorld()
	{
		for (auto &kv : world.objects)
		{
			delete kv.second;
		}
	}
	Path *pathTo(float x, float y, bool *partial)
	{
		Coord3D from = unit->pos, to{ x, y, 0.0f };
		return pf->findPath(unit, groundLoco(), &from, &to, partial);
	}
};

struct NodeView
{
	float x, y;
	int id;
	bool canOptimize;
};

std::vector<NodeView> nodes(const Path &p)
{
	std::vector<NodeView> out;
	for (const PathNode *nd = p.getFirstNode(); nd; nd = nd->getNext())
	{
		out.push_back({ nd->getPosition()->x, nd->getPosition()->y, nd->getWaypointID(), nd->getCanOptimize() });
	}
	return out;
}
} // namespace

TEST_CASE("wall2 portals: a dynamic waypoint takes the next id of the counter (0x40000000 at a new map); addPortal marks its cell and lists it in the cell's zone block")
{
	PortalWorld w;
	CHECK(w.a == 0x40000000);
	CHECK(w.b == 0x40000001);
	CHECK(w.m == 0x40000002);
	CHECK(w.n == 0x40000003);
	const PathfindWaypoint *a = w.pf->findWaypoint(w.a);
	REQUIRE(a);
	CHECK(a->hasCell);
	CHECK(a->cell.x == 17);
	CHECK(a->cell.y == 10);
	CHECK(a->numLinks == 1);
	CHECK(a->links[0] == w.b);
	CHECK(a->next == w.m);
	CHECK(a->linkedFrom == w.b); // B links back to A
	// the intermediates are no portals (RW 0x8642B5: + 0x48 = 0) and have no cell
	const PathfindWaypoint *m = w.pf->findWaypoint(w.m);
	REQUIRE(m);
	CHECK_FALSE(m->portal);
	CHECK_FALSE(m->hasCell);
	CHECK(w.pf->cellAt(17, 10)->getPortal() == 1); // A's slot (the first waypoint) + 1
	CHECK(w.pf->cellAt(23, 10)->getPortal() == 2); // B's
	CHECK(w.pf->cellAt(20, 10)->getPortal() == 0);
	// cell (17, 10) is in block (1, 0), cell (23, 10) in block (1, 0) as well (16 x 16 blocks): both listed there, in insertion order
	CHECK(w.pf->zoneManager().interactsWithPortal(17, 10));
	CHECK(w.pf->zoneManager().getPortal(17, 10, 0) == w.pf->cellAt(17, 10)->getPortal());
	CHECK(w.pf->zoneManager().getPortal(17, 10, 1) == w.pf->cellAt(23, 10)->getPortal());
	CHECK(w.pf->zoneManager().getPortal(17, 10, 2) == 0);
	CHECK_FALSE(w.pf->zoneManager().interactsWithPortal(5, 5));
	// removePortal undoes it (RW 0x6EA48A); the waypoint stays, its portal flag cleared
	w.pf->removePortal(w.a);
	CHECK(w.pf->cellAt(17, 10)->getPortal() == 0);
	CHECK(w.pf->zoneManager().getPortal(17, 10, 0) == w.pf->cellAt(23, 10)->getPortal()); // the last moved into the gap
	CHECK_FALSE(w.pf->findWaypoint(w.a)->portal);
	CHECK_FALSE(w.pf->findWaypoint(w.a)->hasCell);
	// a new map clears the table and restarts the counter
	w.pf->newMap(w.terrain);
	CHECK(w.pf->waypointCount() == 0);
	PathfindWaypoint fresh;
	fresh.type = PWAYPOINT_WALK;
	CHECK(w.pf->createWaypoint(fresh) == 0x40000000);
}

TEST_CASE("wall2 portals: without the postern gate the far side of the wall is another zone and the path stops short; through it the path walks the gate's waypoints")
{
	{
		PortalWorld closed(false);
		bool partial = false;
		Path *p = closed.pathTo(305.0f, 105.0f, &partial);
		CHECK(partial);
		if (p)
		{
			CHECK(p->getLastNode()->getPosition()->x < 190.0f); // tolerance: the closest reachable cell lies west of the wall (cells 19 .. 20)
		}
		delete p;
	}
	PortalWorld w;
	// the zones differ, the portal search joins them (RW 0x6F5547)
	const PathfindMovement mv = Pathfinder::makeMovement(w.unit, groundLoco());
	const zoneStorageType zW = w.pf->zoneManager().getEffectiveZone(mv, true, w.pf->cellAt(5, 10)->getZone());
	const zoneStorageType zE = w.pf->zoneManager().getEffectiveZone(mv, true, w.pf->cellAt(30, 10)->getZone());
	REQUIRE(zW != zE);
	CHECK(w.pf->portalPathExists(w.unit, groundLoco(), zW, zE));
	CHECK(w.pf->portalPathExists(w.unit, groundLoco(), zE, zW));
	const Coord3D east{ 305.0f, 105.0f, 0.0f };
	CHECK_FALSE(w.pf->clientSafeQuickDoesPathExist(groundLoco(), &w.unit->pos, &east));     // no unit: the zones alone
	CHECK(w.pf->clientSafeQuickDoesPathExist(groundLoco(), &w.unit->pos, &east, w.unit));   // with the unit: through the portal
	bool partial = true;
	Path *p = w.pathTo(305.0f, 105.0f, &partial);
	REQUIRE(p);
	CHECK_FALSE(partial);
	CHECK(p->getLastNode()->getPosition()->x == 305.0f);
	// prependCells (RW 0x6EF591): the cell reached through A's portal is B's cell; its node stands at A (the waypoint crossed), then come A's intermediate M and
	// B's own position (B's cell is a portal's start), each of them and the node after them carrying the id of the waypoint the step to them starts from
	const std::vector<NodeView> v = nodes(*p);
	int at = -1;
	for (size_t i = 0; i < v.size(); ++i)
	{
		if (v[i].x == 175.0f && v[i].y == 105.0f)
		{
			at = (int)i;
		}
	}
	REQUIRE(at >= 0); // tolerance: the index of the node at A, -1 when none
	REQUIRE((size_t)at + 3 < v.size()); // tolerance: the nodes after A exist
	CHECK(v[(size_t)at].id == PathNode::NO_WAYPOINT);
	CHECK(v[(size_t)at + 1].x == 205.0f); // M
	CHECK(v[(size_t)at + 1].id == w.a);
	CHECK_FALSE(v[(size_t)at + 1].canOptimize);
	CHECK(v[(size_t)at + 2].x == 235.0f); // B
	CHECK(v[(size_t)at + 2].id == w.m);
	CHECK(v[(size_t)at + 3].id == w.b);   // the node after B: the step to it starts at B
	CHECK_FALSE(v[(size_t)at + 3].canOptimize);
	// no other node carries an id
	int ids = 0;
	for (const NodeView &nv : v)
	{
		ids += nv.id != PathNode::NO_WAYPOINT ? 1 : 0;
	}
	CHECK(ids == 3);
	// the path asks the pathfinder about its waypoints: none is gone
	CHECK(p->waypointSource() == w.pf.get());
	delete p;
	// and back: through B's link to A, B's intermediate N
	w.unit->pos = { 305.0f, 105.0f, 0.0f };
	Path *back = w.pathTo(55.0f, 105.0f, &partial);
	REQUIRE(back);
	CHECK_FALSE(partial);
	bool sawN = false;
	for (const NodeView &nv : nodes(*back))
	{
		sawN = sawN || (nv.x == 195.0f && nv.id == w.b);
	}
	CHECK(sawN);
	delete back;
}

TEST_CASE("wall2 portals review of #63: a Via chain that loops back on itself ends at its first repeat; the path through the gate is found and walks each "
		  "Via waypoint once")
{
	PortalWorld w;
	// A's Via M now goes on to P, and P back to M (Link From:A Via:M Via:P Via:M ... as data could chain them): neither has a link of its own
	PathfindWaypoint pw;
	pw.pos = { 215.0f, 105.0f, 0.0f };
	pw.type = PWAYPOINT_WALK;
	pw.owner = w.wall->id;
	const int p = w.pf->createWaypoint(pw);
	w.pf->setWaypointNext(w.m, p);
	w.pf->setWaypointNext(p, w.m);
	bool partial = true;
	Path *path = w.pathTo(305.0f, 105.0f, &partial);
	REQUIRE(path);
	CHECK_FALSE(partial);
	int atM = 0, atP = 0;
	for (const NodeView &nv : nodes(*path))
	{
		atM += nv.x == 205.0f ? 1 : 0;
		atP += nv.x == 215.0f ? 1 : 0;
	}
	CHECK(atM == 1);
	CHECK(atP == 1);
	delete path;
}

TEST_CASE("wall2 portals: the hierarchical corridor crosses the portal and records the hop (the waypoint, its start cell); the search takes it and drops it")
{
	PortalWorld w;
	Coord3D from = w.unit->pos, to{ 305.0f, 105.0f, 0.0f }, adjusted;
	REQUIRE(w.pf->findClosestHierarchicalPath(w.unit, groundLoco(), true, &from, &to, &adjusted));
	CHECK(adjusted.x == to.x);
	CHECK(adjusted.y == to.y);
	REQUIRE(w.pf->corridorPortals().size() == 1);
	CHECK(w.pf->corridorPortals()[0].waypointId == w.a);
	CHECK(w.pf->corridorPortals()[0].cell.x == 17);
	CHECK(w.pf->corridorPortals()[0].cell.y == 10);
	// the estimate through the hop (RW 0x6F1F0D): from cell (5, 10) to (17, 10): 10 * 12 = 120, + 2 * 0 + 1400; then to the goal (30, 10): 10 * 13 = 130
	PathfindCell *c = w.pf->getCell(LAYER_GROUND, 5, 10);
	PathfindCell *g = w.pf->getCell(LAYER_GROUND, 30, 10);
	REQUIRE(c->allocateInfo(w.pf->pool(), ICoord2D{ 5, 10 }));
	REQUIRE(g->allocateInfo(w.pf->pool(), ICoord2D{ 30, 10 }));
	CHECK(w.pf->costToGoal(c, g) == 120u + 1400u + 130u);
	CHECK(c->costToGoal(g) == 250u); // the cell's own estimate ignores the hop
	c->releaseInfo(w.pf->pool());
	g->releaseInfo(w.pf->pool());
	// a full findPath empties the vector first and crosses the hop (CheckChangeLayers pops it)
	bool partial = true;
	Path *p = w.pathTo(305.0f, 105.0f, &partial);
	REQUIRE(p);
	CHECK(w.pf->corridorPortals().empty());
	delete p;
}

TEST_CASE("wall2 portals: a link whose two waypoints share a cell is skipped by the hierarchical search (it would make the cell its own parent)")
{
	PortalWorld w(false); // the cliff wall without the postern: the east side is another zone
	PathfindWaypoint p;
	p.type = PWAYPOINT_WALK;
	p.owner = w.wall->id;
	p.pos = { 175.0f, 105.0f, 0.0f }; // cell (17, 10), the reachable cell nearest the goal
	const int a = w.pf->createWaypoint(p);
	p.pos = { 176.0f, 105.0f, 0.0f }; // the same cell
	const int b = w.pf->createWaypoint(p);
	REQUIRE(w.pf->addWaypointLink(a, b));
	w.pf->addPortal(a);
	w.zones();
	// the goal beyond the wall: the block search ends at its closest cell; taken, the link made cell (17, 10) its own parent there and the corridor's
	// parent walk (makeHierarchicalPathPassable) never ended
	Coord3D from{ 165.0f, 105.0f, 0.0f }, to{ 305.0f, 105.0f, 0.0f }, adjusted{};
	w.pf->findClosestHierarchicalPath(w.unit, groundLoco(), true, &from, &to, &adjusted);
	CHECK(w.pf->corridorPortals().empty()); // no hop through a link that moves nobody
}

TEST_CASE("wall2 portals: Waypoint::isValidUnitType: an enemy of the owner may not pass without AllowEnemies, nor before the activation frame, nor when the filter refuses")
{
	{
		PortalWorld w;
		w.unit->team = 1; // the wall's enemy
		bool partial = false;
		Path *p = w.pathTo(305.0f, 105.0f, &partial);
		CHECK(partial);
		delete p;
		const PathfindWaypoint *a = w.pf->findWaypoint(w.a);
		CHECK_FALSE(w.pf->waypointAllows(*a, *w.unit));
	}
	{
		PortalWorld w(true, true); // AllowEnemies
		w.unit->team = 1;
		CHECK(w.pf->waypointAllows(*w.pf->findWaypoint(w.a), *w.unit));
		bool partial = true;
		Path *p = w.pathTo(305.0f, 105.0f, &partial);
		REQUIRE(p);
		CHECK_FALSE(partial);
		delete p;
	}
	{
		PortalWorld w;
		PathfindWaypoint *a = w.pf->findWaypoint(w.a);
		a->activationFrame = 101; // the world is at frame 100
		CHECK_FALSE(w.pf->waypointAllows(*a, *w.unit));
		w.world.frame = 101;
		CHECK(w.pf->waypointAllows(*a, *w.unit));
		a->filter = [](const PathfindObject &o) { return o.getID() != 1; };
		CHECK_FALSE(w.pf->waypointAllows(*a, *w.unit));
	}
	{
		// a waypoint without an owner lets everyone through (RW 0x6822AF)
		PortalWorld w;
		PathfindWaypoint *a = w.pf->findWaypoint(w.a);
		a->owner = PATHFIND_INVALID_ID;
		w.unit->team = 1;
		CHECK(w.pf->waypointAllows(*a, *w.unit));
	}
	{
		// a type 7 waypoint with an owner (FakePathfindPortalBehaviour's gate pair) refuses a unit without an AI and one inside a move-away search (RW 0x68230B:
		// AI + 0x3CF, set around getMoveAwayFromPath RW 0x66DBFB / 0x66DC4B)
		PortalWorld w;
		PathfindWaypoint *a = w.pf->findWaypoint(w.a);
		a->type = PWAYPOINT_TYPE7;
		CHECK(w.pf->waypointAllows(*a, *w.unit));
		w.unit->moveAwaySearch = true;
		CHECK_FALSE(w.pf->waypointAllows(*a, *w.unit));
		w.unit->moveAwaySearch = false;
		w.unit->mobile = false; // no AI
		CHECK_FALSE(w.pf->waypointAllows(*a, *w.unit));
		a->owner = PATHFIND_INVALID_ID; // RW tests the type only with an owner
		CHECK(w.pf->waypointAllows(*a, *w.unit));
	}
	// the portal types (RW 0x6E82FD)
	PathfindWaypoint t;
	for (int type = 0; type < 10; ++type)
	{
		t.type = type;
		const bool portal = type == 1 || type == 2 || type == 3 || type == 4 || type == 7 || type == 8;
		CHECK(Pathfinder::isPortalWaypoint(t) == portal);
	}
	t.portal = false;
	t.type = PWAYPOINT_WALK;
	CHECK_FALSE(Pathfinder::isPortalWaypoint(t));
}

TEST_CASE("wall2 portals: a path that names a waypoint which is gone has invalid portals (RW 0x76573F); the path follower's point ahead slows on a Climb waypoint")
{
	{
		// RW 0x76573F asks every id ahead for a PORTAL waypoint (RW 0x6E82FD): a Via waypoint has + 0x48 cleared, so a path through one counts as having invalid
		// portals (RW 0x6E8454 then drops it whenever any portal is removed: retail, ported as read)
		PortalWorld w;
		bool partial = true;
		Path *p = w.pathTo(305.0f, 105.0f, &partial);
		REQUIRE(p);
		p->updateClosestSegment(w.unit->pos);
		CHECK(p->nextPortalID() == w.a);
		CHECK(p->hasInvalidPortals());
		delete p;
	}
	PortalWorld w(true, false, false); // the gate without intermediates
	bool partial = true;
	Path *p = w.pathTo(305.0f, 105.0f, &partial);
	REQUIRE(p);
	p->updateClosestSegment(w.unit->pos);
	CHECK(p->nextPortalID() == w.a);
	CHECK_FALSE(p->hasInvalidPortals());
	w.pf->removePortal(w.b);
	CHECK(p->hasInvalidPortals()); // B is no portal any more
	w.pf->addPortal(w.b);
	CHECK_FALSE(p->hasInvalidPortals());
	w.pf->destroyWaypoint(w.b);
	CHECK(p->hasInvalidPortals()); // B is gone
	// advancePastPortal (WB 0xf1d2b0): the current node becomes the node of A's id (the one at B), then the nodes with ids while their next has one too: the
	// node after B (B's id), whose next has none
	p->advancePastPortal(w.a);
	REQUIRE(p->currentNode());
	CHECK(p->currentNode()->getWaypointID() == w.b);
	CHECK(p->currentNode()->getNextOptimized()->getWaypointID() == PathNode::NO_WAYPOINT);
	CHECK(p->currentT() == 0.0f);
	delete p;
	// Climb (RW 0x76602F): a node ahead whose waypoint is a Climb one slows the point ahead by seg / (|dz| + 1)
	Path climb;
	Coord3D p0{ 0.0f, 0.0f, 0.0f }, p1{ 10.0f, 0.0f, 30.0f }, p2{ 20.0f, 0.0f, 30.0f };
	climb.appendNode(&p0, LAYER_GROUND);
	climb.appendNode(&p1, LAYER_GROUND);
	climb.appendNode(&p2, LAYER_GROUND);
	PathfindWaypoint cw;
	cw.type = PWAYPOINT_CLIMB;
	const int cid = w.pf->createWaypoint(cw);
	climb.getFirstNode()->getNext()->setWaypointID(cid);
	climb.getFirstNode()->setNextOptimized(climb.getFirstNode()->getNext());
	climb.getFirstNode()->getNext()->setNextOptimized(climb.getLastNode());
	climb.setWaypointSource(w.pf.get());
	// seg 10, dz 30: sf = 10 / 31 below 0.5 -> times 0.85; a point 1 ahead lies sf * 1 / 10 along the segment
	const float sf = 10.0f / 31.0f * 0.85f;
	const LocomotorPathPoint ahead = climb.computePointAhead(1.0f);
	CHECK(ahead.position.x == doctest::Approx(sf * 1.0f)); // tolerance: the segment fraction in float32 (sf / 10 * 10)
	CHECK(ahead.position.z == doctest::Approx(sf * 3.0f)); // tolerance: the same fraction of the 30 rise
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// the mover (AIMove.cpp, RW 0x669E8F .. 0x66A27E)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("wall2 portals move: a walker ordered across the cliff wall walks the postern gate's waypoints (put on the point ahead each frame, WALKING) and arrives")
{
	movetest::MoveWorld w(40, 20);
	for (int y = 0; y < 20; ++y)
	{
		w.terrain.cliff.insert({ 19, y });
		w.terrain.cliff.insert({ 20, y });
	}
	Object *u = w.spawn("Walker", 55.0f, 105.0f);
	Object *owner = w.spawn("Block", 305.0f, 25.0f); // the gate's owner (the walker's ally: one team)
	w.buildMap();
	Pathfinder &pf = w.ai->pathfinder();
	auto make = [&](float x) {
		PathfindWaypoint wp;
		wp.pos = { x, 105.0f, 0.0f };
		wp.type = PWAYPOINT_WALK;
		wp.owner = owner->getID();
		return pf.createWaypoint(wp);
	};
	const int a = make(175.0f), b = make(235.0f), m = make(205.0f), n = make(195.0f);
	pf.addWaypointLink(a, b);
	pf.addPortal(a);
	pf.setWaypointNext(a, m);
	pf.addWaypointLink(b, a);
	pf.addPortal(b);
	pf.setWaypointNext(b, n);
	w.frames(2);
	AIUpdateInterface *ai = w.aiOf(u);
	REQUIRE(ai);
	ai->aiMoveToPosition(Coord3D{ 305.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	int portalFrames = 0;
	bool offLine = false, walking = false;
	const int walkingBit = AIUpdateInterface::modelConditionBit("WALKING");
	for (int f = 0; f < 600; ++f)
	{
		w.frames(1);
		const Coord3D p = *u->getPosition();
		if (ai->mover().onPortal())
		{
			++portalFrames;
			offLine = offLine || (p.x <= 235.0f && p.y != 105.0f); // A, M and B are on the line y = 105: the unit is put on points of it (past B it heads for the next cell, which carries B's id)
			walking = walking || u->testModelCondition(walkingBit);
		}
		if (p.x > 300.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *u->getPosition();
	INFO("the walker stands at " << at.x << ", " << at.y << " after " << portalFrames << " frames on the portal");
	CHECK(portalFrames > 0); // tolerance: some frames on the portal (the count follows the walk speed)
	CHECK_FALSE(offLine);
	CHECK(walking);
	CHECK(at.x > 295.0f); // tolerance: within the locomotor's close-enough distance of the goal
	CHECK_FALSE(ai->mover().onPortal());
}
