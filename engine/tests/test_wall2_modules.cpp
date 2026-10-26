// OpenBFME unit tests, lane WALL-2: the portal modules (GameLogic/Module/PortalModules.h) on retail data. GPL-3.0.
//
// Helm's Deep's HelmsDeepCulvert carries DynamicPortalBehaviour PosternGatePortal (TriggeredBy Upgrade_PosternGate; BonePrefix Post, 4 bones; six Walk
// waypoints at the bones 0, 1, 2, 3, 2, 1; Link From:0 Via:4 Via:5 To:3 and From:3 Via:1 Via:2 To:0). The expected structure comes from createWaypoints
// as read (RW 0x86404D); the positions are the culvert drawable's Post01 .. Post04 bones at the object's transform.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace starttest;

int templateIndex(Shared &s, const std::string &name)
{
	for (int i = 0; i < s.world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(i);
		if (pt && pt->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// slot 0 (start 0, Player_1_Start: the fortress) is the human, or the skirmish AI when `aiFortress`
NewGameMessage helmsDeep(Shared &s, bool aiFortress = false, const char *mapName = "maps/map wor helms deep/map wor helms deep.map")
{
	NewGameMessage m;
	m.game.mapName = mapName;
	m.game.seed = 11;
	m.game.startingCash = 1000;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		const bool human = (i == 0) != aiFortress;
		sl.state = human ? SLOT_PLAYER : SLOT_EASY_AI;
		sl.name = human ? u"Host" : u"Easy";
		sl.accepted = true;
		sl.playerTemplate = templateIndex(s, factions[i]);
		REQUIRE(sl.playerTemplate >= 0); // tolerance: the index of a found template (-1: not found)
		sl.startPos = i;
		sl.color = i;
	}
	return m;
}

struct PortalGame
{
	Shared &s;
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	explicit PortalGame(Shared &sh, bool aiFortress = false, const char *mapName = "maps/map wor helms deep/map wor helms deep.map")
		: s(sh), source(*sh.mount->fs), assets(source)
	{
		std::string error;
		static std::vector<MapCacheEntry> cache;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		REQUIRE_MESSAGE(NewGame::prepareNewGame(helmsDeep(s, aiFortress, mapName), s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game->load(o, &error), error);
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			game->advance(0.2);
		}
	}
	Object *first(const char *name)
	{
		for (Object *o = game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	}
};
} // namespace

TEST_CASE("wall2 modules: the stop lines of the portal modules are S-4853 .. S-4855 in id order")
{
	const std::vector<std::string> stops = PortalModules::stopLines();
	REQUIRE(stops.size() == 3);
	CHECK(stops[0].rfind("[S-4853] DynamicPortalBehaviour (RW 0x86404D)", 0) == 0);
	CHECK(stops[1].rfind("[S-4854] DynamicPortalBehaviour (RW 0x864357): a WallBoundsMesh", 0) == 0);
	CHECK(stops[2].rfind("[S-4855] DynamicPortalBehaviour (RW 0x864357 / 0x863D3A)", 0) == 0);
}

TEST_CASE("wall2 modules retail: Upgrade_PosternGate on Helm's Deep's culvert makes the six waypoints at its Post bones, the two portals and the INI's links")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *culvert = g.first("HelmsDeepCulvert");
	REQUIRE(culvert);
	DynamicPortalBehaviour *portal = DynamicPortalBehaviour::of(*culvert);
	REQUIRE(portal);
	CHECK_FALSE(portal->created()); // TriggeredBy Upgrade_PosternGate, no GenerateNow
	const size_t before = pf.waypointCount();
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_PosternGate") : nullptr;
	REQUIRE(u);
	culvert->giveUpgrade(u);
	REQUIRE(portal->created());
	CHECK(pf.waypointCount() == before + 6);
	const std::array<int, DynamicPortalBehaviour::MAX_WAYPOINTS> &id = portal->waypointIds();
	const PathfindWaypoint *w[6];
	for (int i = 0; i < 6; ++i)
	{
		w[i] = pf.findWaypoint(id[(size_t)i]);
		REQUIRE(w[i]);
		CHECK(w[i]->type == PWAYPOINT_WALK);
		CHECK(w[i]->owner == (PathfindObjectID)culvert->getID());
		CHECK(w[i]->activationFrame == 0u); // no ActivationDelaySeconds
		MESSAGE("waypoint " << i << " id " << w[i]->id << " at " << w[i]->pos.x << ", " << w[i]->pos.y << ", " << w[i]->pos.z);
	}
	// ids in WayPoint order from the counter
	for (int i = 1; i < 6; ++i)
	{
		CHECK(id[(size_t)i] == id[(size_t)i - 1] + 1);
	}
	// the bones: WayPoint 4 is bone 2 (WayPoint 2's), 5 is bone 1
	CHECK(w[4]->pos.x == w[2]->pos.x);
	CHECK(w[4]->pos.y == w[2]->pos.y);
	CHECK(w[5]->pos.x == w[1]->pos.x);
	CHECK(w[5]->pos.y == w[1]->pos.y);
	// Link From:0 Via:4 Via:5 To:3: 0 -> 3, the chain 0 -> 4 -> 5; From:3 Via:1 Via:2 To:0: 3 -> 0, 3 -> 1 -> 2
	CHECK(w[0]->numLinks == 1);
	CHECK(w[0]->links[0] == id[3]);
	CHECK(w[3]->numLinks == 1);
	CHECK(w[3]->links[0] == id[0]);
	CHECK(w[0]->next == id[4]);
	CHECK(w[4]->next == id[5]);
	CHECK(w[5]->next == PathfindWaypoint::NO_ID);
	CHECK(w[3]->next == id[1]);
	CHECK(w[1]->next == id[2]);
	// the portals: the two ends (AddPortal), the vias are none (+ 0x48 = 0)
	CHECK(Pathfinder::isPortalWaypoint(*w[0]));
	CHECK(Pathfinder::isPortalWaypoint(*w[3]));
	for (int i : { 1, 2, 4, 5 })
	{
		CHECK_FALSE(Pathfinder::isPortalWaypoint(*w[i]));
	}
	CHECK(w[0]->hasCell);
	CHECK(w[3]->hasCell);
	// the ends lie on the ground on either side of the wall: different zones of the ground grid
	const PathfindCell *c0 = pf.getCell(LAYER_GROUND, &w[0]->pos);
	const PathfindCell *c3 = pf.getCell(LAYER_GROUND, &w[3]->pos);
	REQUIRE(c0);
	REQUIRE(c3);
	MESSAGE("cell types " << (int)c0->getType() << " / " << (int)c3->getType() << ", zones " << c0->getZone() << " / " << c3->getZone());
	// the upgrade's removal or the culvert's death takes them away again (RW 0x8639EF)
	DieModuleInterface::Event e;
	portal->onDie(e);
	CHECK_FALSE(portal->created());
	CHECK(portal->disabled());
	CHECK(pf.waypointCount() == before);
	CHECK(pf.findWaypoint(id[0]) == nullptr);
}

TEST_CASE("wall2 modules retail: through Helm's Deep's postern gate: a hero ordered across the culvert walks its waypoints, the gate's portal joins the wall's two sides")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *culvert = g.first("HelmsDeepCulvert");
	REQUIRE(culvert);
	Player *owner = culvert->getControllingPlayer();
	REQUIRE(owner);
	MESSAGE("culvert at " << culvert->getPosition()->x << ", " << culvert->getPosition()->y << " owned by player " << owner->getPlayerIndex() << " type " << (int)owner->getPlayerType());
	const Coord3D outside{ 1730.0f, 3110.0f, 0.0f }, inside{ 1730.0f, 2810.0f, 0.0f };
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", owner->getPlayerIndex(), outside, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	g.frames(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	Coord3D from = *hero->getPosition(), to = inside;
	MESSAGE("ground: outside " << g.game->logic().getGroundHeight(outside.x, outside.y) << ", inside " << g.game->logic().getGroundHeight(inside.x, inside.y));
	bool partial = false;
	Path *p = pf.findPath(&ai->adapter(), ai->locomotorInfo(), &from, &to, &partial);
	MESSAGE("before the gate: " << (p ? (int)p->nodeCount() : -1) << " nodes, partial " << partial
	                            << (p ? ", ends at " + std::to_string(p->getLastNode()->getPosition()->x) + ", " + std::to_string(p->getLastNode()->getPosition()->y) : std::string()));
	// without the gate the wall (the culvert's footprint) stops the path at its outer face
	REQUIRE(p);
	CHECK(partial);
	CHECK(p->getLastNode()->getPosition()->y == 2930.5f);
	delete p;
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_PosternGate") : nullptr;
	REQUIRE(u);
	culvert->giveUpgrade(u);
	DynamicPortalBehaviour *portal = DynamicPortalBehaviour::of(*culvert);
	REQUIRE(portal);
	REQUIRE(portal->created());
	partial = true;
	p = pf.findPath(&ai->adapter(), ai->locomotorInfo(), &from, &to, &partial);
	REQUIRE(p);
	CHECK_FALSE(partial);
	CHECK(p->nodeCount() == 17);
	// the path through the gate (prependCells RW 0x6EF591): the node at Post01 (the portal crossed), then the Via chain 4, 5 prepended in reverse (Post02,
	// Post03: the INI lists the vias backwards for that), then the far end Post04, each of them and the node after carrying the id of the waypoint the step to
	// it starts from
	const std::array<int, DynamicPortalBehaviour::MAX_WAYPOINTS> &id = portal->waypointIds();
	std::vector<std::pair<float, int>> marked;
	const float w0y = pf.findWaypoint(id[0]) ? pf.findWaypoint(id[0])->pos.y : 0.0f;
	int ids = 0;
	for (const PathNode *nd = p->getFirstNode(); nd; nd = nd->getNext())
	{
		ids += nd->getWaypointID() != PathNode::NO_WAYPOINT ? 1 : 0;
		CHECK(nd->getLayer() == LAYER_GROUND);
		if (nd->getWaypointID() != PathNode::NO_WAYPOINT || nd->getPosition()->y == w0y)
		{
			marked.push_back({ nd->getPosition()->y, nd->getWaypointID() });
		}
	}
	CHECK(ids == 4);
	const PathfindWaypoint *w0 = pf.findWaypoint(id[0]), *w1 = pf.findWaypoint(id[1]), *w2 = pf.findWaypoint(id[2]), *w3 = pf.findWaypoint(id[3]);
	REQUIRE(w0);
	REQUIRE(w1);
	REQUIRE(w2);
	REQUIRE(w3);
	const std::vector<std::pair<float, int>> expected = { { w0->pos.y, (int)PathNode::NO_WAYPOINT }, { w1->pos.y, id[0] }, { w2->pos.y, id[5] },
		{ w3->pos.y, id[4] }, { 2860.5f, id[3] } };
	CHECK(marked == expected);
	delete p;
	ai->aiMoveToPosition(inside, CMD_FROM_PLAYER);
	bool sawPortal = false;
	for (int f = 0; f < 600; ++f)
	{
		g.frames(1);
		sawPortal = sawPortal || ai->mover().onPortal();
		const Coord3D q = *hero->getPosition();
		if ((q.x - inside.x) * (q.x - inside.x) + (q.y - inside.y) * (q.y - inside.y) < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z);
	CHECK(sawPortal);
	CHECK(at.x == 1730.5f);
	CHECK(at.y == 2810.5f);
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
	CHECK_FALSE(ai->mover().onPortal());
}

TEST_CASE("wall2 modules retail: a horde ordered across Helm's Deep's culvert takes its postern gate: its front ranks end inside the wall (q26 follow-up: the rest stop in the culvert); without the gate none does")
{
	OPENBFME_REQUIRE_START(s);
	for (const bool gate : { false, true })
	{
		PortalGame g(*s);
		g.frames(2);
		Object *culvert = g.first("HelmsDeepCulvert");
		REQUIRE(culvert);
		Player *owner = culvert->getControllingPlayer();
		REQUIRE(owner);
		if (gate)
		{
			const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_PosternGate") : nullptr;
			REQUIRE(u);
			culvert->giveUpgrade(u);
		}
		const Coord3D outside{ 1730.0f, 3150.0f, 0.0f }, inside{ 1730.0f, 2790.0f, 0.0f };
		std::string error;
		Object *horde = g.game->createObject("GondorFighterHorde", owner->getPlayerIndex(), outside, 0.0f, &error);
		REQUIRE_MESSAGE(horde, error);
		g.frames(10);
		AIUpdateInterface *ai = horde->getAIUpdateInterface();
		REQUIRE(ai);
		ai->aiMoveToPosition(inside, CMD_FROM_PLAYER);
		int hordeOnPortal = 0;
		for (int f = 0; f < 600; ++f)
		{
			g.frames(1);
			hordeOnPortal += ai->mover().onPortal() ? 1 : 0;
		}
		// inside: south of the culvert's inner face (Post04 at y 2872.9)
		int members = 0, inner = 0;
		for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getContainedBy() == horde && o->getAIUpdateInterface())
			{
				++members;
				inner += o->getPosition()->y < 2872.0f ? 1 : 0; // tolerance: south of Post04, the gate's inner end
			}
		}
		const Coord3D hp = *horde->getPosition();
		MESSAGE(std::string(gate ? "with" : "without") << " the gate: horde at " << hp.x << ", " << hp.y << ", " << hordeOnPortal << " frames on a portal, " << inner << " of " << members
		                                     << " members inside");
		CHECK(members == 15); // GondorFighterHorde's 15 soldiers
		if (gate)
		{
			// the horde's own path crosses the gate: its point ahead on the waypoint nodes for 19 frames (AI + 0x3BB; a HORDE appearance locomotor walks them on
			// the ground, RW 0x669E47 .. 0x669E89); the members follow their formation slots with straight steps (RW 0x6F1B3E asks the target cell only)
			CHECK(hordeOnPortal == 19);
			// merge queue q26: since the slot table is centred on the horde (community #73, RW 0x86C4EA) only the 7 members of the front ranks end inside;
			// the other 8 stop in the culvert's passage (y 2900 .. 2921) and stay there (measured to frame 1600). An engine pin of a follow-up, not retail:
			// the members' own paths through the gate's portal are the open part (likely S-1751: the member's pathfinder layer update RW 0x6F0741)
			CHECK(inner == 7);
			CHECK(hp.y < 2872.0f); // tolerance: the horde itself stands south of Post04
		}
		else
		{
			CHECK(hordeOnPortal == 0);
			CHECK(inner == 0);
		}
	}
}

TEST_CASE("wall2 modules retail: FakePathfindPortalBehaviour: a skirmish AI's closed Helm's Deep gate is a portal pair of type 7 waypoints; opened, the pair is unregistered")
{
	OPENBFME_REQUIRE_START(s);
	for (const bool aiFortress : { false, true })
	{
		PortalGame g(*s, aiFortress);
		g.frames(3);
		Pathfinder &pf = g.game->ai().pathfinder();
		int gates = 0, made = 0;
		for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			FakePathfindPortalBehaviour *fp = dynamic_cast<FakePathfindPortalBehaviour *>(o->findModule("FakePathfindPortalBehaviour"));
			if (!fp)
			{
				continue;
			}
			++gates;
			GateOpenAndCloseBehavior *gate = GateOpenAndCloseBehavior::findGate(*o);
			const Player *owner = o->getControllingPlayer();
			const bool ai = owner && g.game->logic().skirmishAI().findAI(owner->getPlayerIndex()) != nullptr;
			MESSAGE(o->getTemplate()->getName() << " at " << o->getPosition()->x << ", " << o->getPosition()->y << " angle " << o->getOrientation() << ": gate "
			                                    << (gate != nullptr) << ", skirmish AI " << ai << ", created " << fp->created() << ", registered " << fp->registered());
			if (!ai || !gate)
			{
				CHECK_FALSE(fp->created()); // RW 0x864EAA: no AI or no gate -> sleep forever, nothing made
				continue;
			}
			++made;
			REQUIRE(fp->created());
			const PathfindWaypoint *a = pf.findWaypoint(fp->waypoint(0));
			const PathfindWaypoint *b = pf.findWaypoint(fp->waypoint(1));
			REQUIRE(a);
			REQUIRE(b);
			CHECK(a->type == PWAYPOINT_TYPE7);
			CHECK(b->type == PWAYPOINT_TYPE7);
			CHECK(a->owner == (PathfindObjectID)o->getID());
			CHECK_FALSE(a->allowEnemies);       // AllowEnemies = No
			CHECK_FALSE(a->allowNonSkirmishAI); // AllowNonSkirmishAIUnits = No
			CHECK(a->numLinks == 1);
			CHECK(a->links[0] == b->id);
			CHECK(b->links[0] == a->id);
			// the pair is symmetric about the gate's position (the offset and 0 - the offset), each on the terrain
			CHECK(SimMath::addf32(a->pos.x, b->pos.x) == SimMath::addf32(o->getPosition()->x, o->getPosition()->x));
			CHECK(a->pos.z == g.game->logic().getGroundHeight(a->pos.x, a->pos.y));
			CHECK(b->pos.z == g.game->logic().getGroundHeight(b->pos.x, b->pos.y));
			// registered while the gate is not open (RW 0x86498E), unregistered once it is (RW 0x8649B7). The AI's own AIGateUpdate opens the gate for an ally
			// in its trigger box (RW 0x8B4D34)
			CHECK(gate->isOpen() == !fp->registered());
			if (gates == 1 && !gate->isOpen())
			{
				CHECK(Pathfinder::isPortalWaypoint(*a));
				std::string error;
				// an AI unit beyond one end paths to beyond the other through the pair (the closed gate blocks the ground)
				const Coord3D &gp = *o->getPosition();
				const Coord3D from{ SimMath::addf32(a->pos.x, SimMath::subf32(a->pos.x, gp.x)), SimMath::addf32(a->pos.y, SimMath::subf32(a->pos.y, gp.y)), 0.0f };
				Coord3D to{ SimMath::addf32(b->pos.x, SimMath::subf32(b->pos.x, gp.x)), SimMath::addf32(b->pos.y, SimMath::subf32(b->pos.y, gp.y)), 0.0f };
				Object *walker = g.game->createObject("GondorFighter", owner->getPlayerIndex(), from, 0.0f, &error);
				REQUIRE_MESSAGE(walker, error);
				AIUpdateInterface *wai = walker->getAIUpdateInterface();
				REQUIRE(wai);
				Coord3D start = *walker->getPosition();
				bool partial = true;
				Path *p = pf.findPath(&wai->adapter(), wai->locomotorInfo(), &start, &to, &partial);
				REQUIRE(p);
				int ids = 0;
				for (const PathNode *nd = p->getFirstNode(); nd; nd = nd->getNext())
				{
					ids += nd->getWaypointID() != PathNode::NO_WAYPOINT ? 1 : 0;
				}
				MESSAGE("AI unit from " << from.x << ", " << from.y << " to " << to.x << ", " << to.y << ": " << p->nodeCount() << " nodes, partial " << partial << ", "
				                        << ids << " waypoint nodes");
				// the closed door is no obstacle of the ground grid here (the port's gate footprint, S-1940): the search walks straight through and takes no
				// portal (a portal hop costs 1400 more, RW 0x6F1F0D)
				CHECK_FALSE(partial);
				CHECK(ids == 0);
				delete p;
				g.game->logic().destroyObject(walker);
				g.frames(1);
				Object *ally = g.game->createObject("GondorFighter", owner->getPlayerIndex(), *o->getPosition(), 0.0f, &error);
				REQUIRE_MESSAGE(ally, error);
				int f = 0;
				for (; f < 150 && !gate->isOpen(); ++f)
				{
					g.frames(1);
				}
				REQUIRE(gate->isOpen());
				g.frames(1);
				CHECK_FALSE(fp->registered());
				CHECK_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(fp->waypoint(0))));
			}
		}
		MESSAGE((aiFortress ? std::string("AI fortress: ") : std::string("human fortress: ")) << gates << " gates with the module, " << made << " made their pair");
		CHECK(gates > 0); // tolerance: Helm's Deep places gates with the module
		CHECK((made > 0) == aiFortress);
	}
}

TEST_CASE("wall2 raised retail: Helm's Deep's gatehouse walkway (HelmsDeepGatehouseCenter's RaisedWallMesh P1) is raised layer 2 over the gate's passage")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *centre = g.first("HelmsDeepGatehouseCenter");
	REQUIRE(centre);
	MESSAGE("centre at " << centre->getPosition()->x << ", " << centre->getPosition()->y << ", " << centre->getPosition()->z << "; raised layers " << pf.raisedLayerCount()
	                     << ", layer 2 height " << pf.raisedLayerHeight(LAYER_TOP));
	const Coord3D c = *centre->getPosition();
	const int ccx = SimMath::floorToInt(SimMath::mulf32(c.x, 0.1f)), ccy = SimMath::floorToInt(SimMath::mulf32(c.y, 0.1f));
	PathfindLayerEnum walkway = LAYER_INVALID;
	for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
	{
		if (pf.raisedLayerInUse((PathfindLayerEnum)l))
		{
			const PathfindCell *rc = pf.raisedCellAt((PathfindLayerEnum)l, ccx, ccy);
			MESSAGE("raised layer " << l << " height " << pf.raisedLayerHeight((PathfindLayerEnum)l) << (rc ? " has a cell at the centre" : ""));
			if (rc && rc->getType() == PathfindCell::CELL_CLEAR)
			{
				walkway = (PathfindLayerEnum)l;
			}
		}
	}
	REQUIRE(walkway != LAYER_INVALID);
	int clear = 0, joints = 0, rampJoints = 0;
	const ICoord2D *ext = pf.getExtent();
	for (int x = 0; x <= ext->x; ++x)
	{
		for (int y = 0; y <= ext->y; ++y)
		{
			const PathfindCell *rc = pf.raisedCellAt(walkway, x, y);
			if (rc && rc->getType() == PathfindCell::CELL_CLEAR)
			{
				++clear;
				joints += rc->getConnectLayer() == LAYER_RAMP ? 1 : 0;
			}
			rampJoints += pf.cellAt(x, y)->getConnectLayer() == walkway ? 1 : 0;
		}
	}
	MESSAGE("walkway cells " << clear << ", walkway joints " << joints << ", ramp cells joining it " << rampJoints);
	CHECK(clear > 0); // tolerance: the walkway has cells (counted above)
	// the passage under the walkway is the ground
	const PathfindCell *under = pf.getCell(LAYER_GROUND, &c);
	REQUIRE(under);
	MESSAGE("the cell under the centre: layer " << (int)under->getLayer() << " type " << (int)under->getType());
}

namespace
{
// the CLEAR wall-top cell (all eight neighbours on its layer) nearest `pos`, its centre in `out`
bool nearestWallTop(Pathfinder &pf, const Coord3D &pos, Coord3D &out)
{
	const int cx = SimMath::floorToInt(SimMath::mulf32(pos.x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(pos.y, 0.1f));
	int best = -1;
	for (int x = cx - 20; x <= cx + 20; ++x)
	{
		for (int y = cy - 20; y <= cy + 20; ++y)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (!c || !isWallLayer(c->getLayer()) || c->getType() != PathfindCell::CELL_CLEAR || c->getConnectLayer() != LAYER_INVALID)
			{
				continue;
			}
			bool inner = true;
			for (int dx = -1; dx <= 1 && inner; ++dx)
			{
				for (int dy = -1; dy <= 1; ++dy)
				{
					const PathfindCell *n = pf.cellAt(x + dx, y + dy);
					inner = inner && n && n->getLayer() == c->getLayer();
				}
			}
			const int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
			if (inner && (best < 0 || d < best))
			{
				best = d;
				out = Coord3D{ (float)(x * 10 + 5), (float)(y * 10 + 5), pf.getLayerHeight((float)(x * 10 + 5), (float)(y * 10 + 5), c->getLayer()) };
			}
		}
	}
	return best >= 0;
}
// the wall-top cell inside the top (all eight neighbours on its layer) nearest the cell (cx, cy), ring by ring, that `ai`'s unit has a full path to
bool reachableWallTop(Pathfinder &pf, AIUpdateInterface &ai, int cx, int cy, Coord3D &top, int rings = 30)
{
	for (int r = 1; r <= rings; ++r)
	{
		for (int dx = -r; dx <= r; ++dx)
		{
			for (int dy = -r; dy <= r; ++dy)
			{
				const int x = cx + dx, y = cy + dy;
				const PathfindCell *c = pf.cellAt(x, y);
				if ((dx != -r && dx != r && dy != -r && dy != r) || !c || !isWallLayer(c->getLayer()) || c->getType() != PathfindCell::CELL_CLEAR)
				{
					continue;
				}
				bool inner = true;
				for (int ex = -1; ex <= 1; ++ex)
				{
					for (int ey = -1; ey <= 1; ++ey)
					{
						const PathfindCell *n = pf.cellAt(x + ex, y + ey);
						inner = inner && n && n->getLayer() == c->getLayer();
					}
				}
				if (!inner)
				{
					continue;
				}
				Coord3D from = *ai.getObject()->getPosition(), to{ (float)x * 10.0f + 5.0f, (float)y * 10.0f + 5.0f, 0.0f };
				bool partial = true;
				Path *p = pf.findPath(&ai.adapter(), ai.locomotorInfo(), &from, &to, &partial);
				const bool ok = p && !partial && isWallLayer(p->getLastNode()->getLayer());
				delete p;
				if (ok)
				{
					top = to;
					return true;
				}
			}
		}
	}
	return false;
}
} // namespace

TEST_CASE("wall2 raised retail: a hero on Helm's Deep's left gatehouse walks to the right one over the walkway above the gate (wall, ramp, layer 2, ramp, wall)")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *left = g.first("HelmsDeepGatehouseLeft");
	Object *right = g.first("HelmsDeepGatehouseRight");
	REQUIRE(left);
	REQUIRE(right);
	Coord3D a, b;
	REQUIRE(nearestWallTop(pf, *left->getPosition(), a));
	REQUIRE(nearestWallTop(pf, *right->getPosition(), b));
	MESSAGE("left top " << a.x << ", " << a.y << ", " << a.z << "; right top " << b.x << ", " << b.y << ", " << b.z);
	Player *owner = left->getControllingPlayer();
	REQUIRE(owner);
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", owner->getPlayerIndex(), a, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	hero->setPosition(&a);
	g.frames(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	MESSAGE("the hero starts at " << hero->getPosition()->x << ", " << hero->getPosition()->y << ", " << hero->getPosition()->z << " on layer " << (int)ai->adapter().getLayer());
	REQUIRE(isWallLayer(ai->adapter().getLayer()));
	Coord3D from = *hero->getPosition(), to = b;
	bool partial = true;
	Path *p = pf.findPath(&ai->adapter(), ai->locomotorInfo(), &from, &to, &partial);
	REQUIRE(p);
	bool pathRaised = false, pathGround = false;
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		pathRaised = pathRaised || isRaisedLayer(n->getLayer());
		pathGround = pathGround || n->getLayer() == LAYER_GROUND;
	}
	MESSAGE(p->nodeCount() << " nodes, partial " << partial << ", over the walkway " << pathRaised << ", on the ground " << pathGround);
	CHECK_FALSE(partial);
	CHECK(pathRaised);
	CHECK_FALSE(pathGround);
	delete p;
	ai->aiMoveToPosition(b, CMD_FROM_PLAYER);
	bool sawRaised = false;
	float raisedZ = 0.0f;
	for (int f = 0; f < 900; ++f)
	{
		g.frames(1);
		if (isRaisedLayer(ai->adapter().getLayer()))
		{
			sawRaised = true;
			raisedZ = hero->getPosition()->z;
		}
		const Coord3D q = *hero->getPosition();
		if ((q.x - b.x) * (q.x - b.x) + (q.y - b.y) * (q.y - b.y) < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z << " on layer " << (int)ai->adapter().getLayer());
	CHECK(sawRaised);
	CHECK(raisedZ == 225.0f); // the walkway's height (its layer's int height)
	CHECK(isWallLayer(ai->adapter().getLayer()));
	CHECK((at.x - b.x) * (at.x - b.x) + (at.y - b.y) * (at.y - b.y) < 900.0f); // tolerance: within the locomotor's close-enough distance of the goal cell
}

TEST_CASE("wall2 walls retail: a wall piece is classified with the model of its current state: Helm's Deep's culvert as rubble (RBHDDWCulv_D3)")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *culvert = g.first("HelmsDeepCulvert");
	REQUIRE(culvert);
	auto wallCellsNear = [&](const Object &o) {
		const int cx = SimMath::floorToInt(SimMath::mulf32(o.getPosition()->x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(o.getPosition()->y, 0.1f));
		int n = 0;
		for (int x = cx - 25; x <= cx + 25; ++x)
		{
			for (int y = cy - 25; y <= cy + 25; ++y)
			{
				const PathfindCell *c = pf.cellAt(x, y);
				n += c && isWallLayer(c->getLayer()) ? 1 : 0;
			}
		}
		return n;
	};
	const int before = wallCellsNear(*culvert);
	const size_t problemsBefore = g.game->report().wallMeshProblems.size();
	culvert->setModelConditionState(AIUpdateInterface::modelConditionBit("RUBBLE"), true);
	g.game->ai().removeObjectFromPathfindMap(*culvert);
	g.game->ai().addObjectToPathfindMap(*culvert);
	const int after = wallCellsNear(*culvert);
	MESSAGE("wall cells near the culvert: " << before << " whole, " << after << " as rubble; wall mesh problems " << problemsBefore << " -> "
	                                         << g.game->report().wallMeshProblems.size());
	for (const std::string &p : g.game->report().wallMeshProblems)
	{
		MESSAGE("  " << p);
	}
	// the rubble model RBHDDWCulv_D3 has no P1: the culvert's wall top is gone (the cells left are the neighbouring sections'); a state's missing mesh is no
	// data problem
	CHECK(before == 512);
	CHECK(after == 74);
	CHECK(g.game->report().wallMeshProblems.size() == problemsBefore);
	// whole again: the wall top comes back
	culvert->setModelConditionState(AIUpdateInterface::modelConditionBit("RUBBLE"), false);
	g.game->ai().removeObjectFromPathfindMap(*culvert);
	g.game->ai().addObjectToPathfindMap(*culvert);
	CHECK(wallCellsNear(*culvert) == 512);
}

// RW 0x82C366 (stop S-1872): an AI walking a path with a portal's waypoint nodes while it stands on a wall top refuses every power; standing on the wall,
// or walking a path without waypoint nodes, it does not
TEST_CASE("wall2 modules retail: ActionManager RW 0x82C366: a hero on Helm's Deep's wall walking a path through the postern gate is refused powers on the wall-top cells")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s);
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *culvert = g.first("HelmsDeepCulvert");
	REQUIRE(culvert);
	Player *owner = culvert->getControllingPlayer();
	REQUIRE(owner);
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_PosternGate") : nullptr;
	REQUIRE(u);
	culvert->giveUpgrade(u);
	const SpecialPowerTemplate *power = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilitySmite") : nullptr;
	REQUIRE(power);
	const Coord3D inside{ 1730.0f, 2810.0f, 0.0f }, outside{ 1730.0f, 3110.0f, 0.0f };
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", owner->getPlayerIndex(), inside, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	g.frames(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	// the nearest wall-top cell inside the top (all eight neighbours on its layer) the hero reaches from inside
	Coord3D top{};
	const bool found = reachableWallTop(pf, *ai, 173, 281, top);
	REQUIRE_MESSAGE(found, "no reachable wall-top cell near the culvert");
	ai->aiMoveToPosition(top, CMD_FROM_PLAYER);
	for (int f = 0; f < 900; ++f)
	{
		g.frames(1);
		const Coord3D q = *hero->getPosition();
		if ((q.x - top.x) * (q.x - top.x) + (q.y - top.y) * (q.y - top.y) < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	INFO("the hero stands at " << hero->getPosition()->x << ", " << hero->getPosition()->y << " on layer " << (int)ai->adapter().getLayer());
	REQUIRE(isWallLayer(ai->adapter().getLayer()));
	// standing on the wall: not refused for the layer
	const unsigned long long standing = ActionManager::stats().movingOnLayer;
	ActionManager::canDoSpecialPower(*hero, power);
	CHECK(ActionManager::stats().movingOnLayer == standing);
	// back inside: this wall top's way down is the stairs HelmsDeepdwStairsA1 on the `outside` point's side, so the path takes the postern gate (its
	// waypoint nodes); on a wall-top cell with that
	// path every power is refused, on the ground's cells none is
	ai->aiMoveToPosition(inside, CMD_FROM_PLAYER);
	int refusedOnWall = 0, framesOnWallWithPortal = 0, refusedOffWall = 0;
	bool leftWall = false;
	for (int f = 0; f < 900; ++f)
	{
		g.frames(1);
		const Path *path = ai->mover().path();
		// RW 0x6EACB9: the ground grid's cell under the hero (a wall top's cells are ground grid cells; the passage under the culvert's top is one too)
		const Coord3D h = *hero->getPosition();
		const PathfindCell *under = pf.cellAt(SimMath::floorToInt(SimMath::mulf32(h.x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(h.y, 0.1f)));
		const bool onWall = under && isWallLayer(under->getLayer());
		leftWall = leftWall || !isWallLayer(ai->adapter().getLayer());
		const unsigned long long before = ActionManager::stats().movingOnLayer;
		const bool can = ActionManager::canDoSpecialPower(*hero, power);
		const bool refused = ActionManager::stats().movingOnLayer != before;
		CHECK(!(refused && can));
		if (onWall && path && path->hasWaypointNodes())
		{
			++framesOnWallWithPortal;
			refusedOnWall += refused ? 1 : 0;
		}
		else
		{
			refusedOffWall += refused ? 1 : 0;
		}
		const Coord3D q = *hero->getPosition();
		if ((q.x - inside.x) * (q.x - inside.x) + (q.y - inside.y) * (q.y - inside.y) < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	MESSAGE("frames on the wall with the portal path " << framesOnWallWithPortal << ", refused " << refusedOnWall << "; refused elsewhere " << refusedOffWall);
	CHECK(leftWall);
	CHECK(framesOnWallWithPortal > 0); // tolerance: some frames walk the wall before the ramp
	CHECK(refusedOnWall == framesOnWallWithPortal);
	CHECK(refusedOffWall == 0);
}

// Minas Tirith: MinisGate1 names RaisedWallMesh P1, RampMesh1 P2, RampMesh2 P3; the walls' tops and ramps are the ground grid's wall and ramp layers
TEST_CASE("wall2 raised retail: Minas Tirith: MinisGate1's RaisedWallMesh P1 is no mesh of its model (no walkway); the walls have tops a hero walks onto")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s, false, "maps/map wor minas tirith/map wor minas tirith.map");
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	int wallCells = 0, rampCells = 0;
	const ICoord2D *ext = pf.getExtent();
	for (int x = 0; x <= ext->x; ++x)
	{
		for (int y = 0; y <= ext->y; ++y)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			wallCells += c && isWallLayer(c->getLayer()) ? 1 : 0;
			rampCells += c && c->getLayer() == LAYER_RAMP ? 1 : 0;
		}
	}
	int gates = 0, overPassage = 0;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() != "MinisGate1")
		{
			continue;
		}
		++gates;
		const Coord3D c = *o->getPosition();
		const int cx = SimMath::floorToInt(SimMath::mulf32(c.x, 0.1f)), cy = SimMath::floorToInt(SimMath::mulf32(c.y, 0.1f));
		PathfindLayerEnum walkway = LAYER_INVALID;
		for (int l = (int)LAYER_TOP; l <= (int)LAYER_RAISED_LAST; ++l)
		{
			const PathfindCell *rc = pf.raisedLayerInUse((PathfindLayerEnum)l) ? pf.raisedCellAt((PathfindLayerEnum)l, cx, cy) : nullptr;
			if (rc && rc->getType() == PathfindCell::CELL_CLEAR)
			{
				walkway = (PathfindLayerEnum)l;
			}
		}
		const PathfindCell *under = pf.cellAt(cx, cy);
		MESSAGE("MinisGate1 at " << c.x << ", " << c.y << ", " << c.z << ": walkway layer " << (int)walkway << (walkway != LAYER_INVALID ? " height " + std::to_string(pf.raisedLayerHeight(walkway)) : std::string())
		                         << "; the cell under it layer " << (under ? (int)under->getLayer() : -1) << " type " << (under ? (int)under->getType() : -1));
		overPassage += walkway != LAYER_INVALID ? 1 : 0;
	}
	MESSAGE("wall-top cells " << wallCells << ", ramp cells " << rampCells << ", raised layers " << pf.raisedLayerCount() << ", gates " << gates << " (" << overPassage
	                          << " with a walkway); wall mesh problems " << g.game->report().wallMeshProblems.size());
	for (const std::string &p : g.game->report().wallMeshProblems)
	{
		MESSAGE("  " << p);
	}
	// the data: MinisGate1's model GBMinGate1 has the sub objects GBMGATE9 and GBMGATE1A only (no P1 .. P3): no walkway and no ramps, as retail's
	// getPolygon / getRamp find none; the 7 problems name the meshes the Minas Tirith models lack (lane BRIDGE-1: 13 before RW 0x936B7D's KindOf gate;
	// the other 6 were meshes of structures without WALK_ON_TOP_OF_WALL, which retail never classifies; the wall-top and ramp cells are the same)
	CHECK(gates == 1);
	CHECK(overPassage == 0);
	CHECK(pf.raisedLayerCount() == 0);
	CHECK(g.game->report().wallMeshProblems.size() == 7);
	CHECK(wallCells == 8291);
	CHECK(rampCells == 1874);
	// a hero beside the fortress's command centre walks onto the nearest wall top it reaches and stands at its height
	Object *keep = nullptr;
	for (Object *o = g.game->logic().getFirstObject(); o && !keep; o = o->getNextObject())
	{
		keep = o->getControllingPlayer() && o->isKindOfName("COMMANDCENTER") && o->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN ? o : nullptr;
	}
	REQUIRE(keep);
	std::string error;
	Coord3D spawn = *keep->getPosition();
	spawn.x += 100.0f;
	Object *hero = g.game->createObject("GondorBoromir", keep->getControllingPlayer()->getPlayerIndex(), spawn, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	g.frames(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	Coord3D top{};
	REQUIRE(reachableWallTop(pf, *ai, SimMath::floorToInt(SimMath::mulf32(hero->getPosition()->x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(hero->getPosition()->y, 0.1f)), top, 60));
	ai->aiMoveToPosition(top, CMD_FROM_PLAYER);
	for (int f = 0; f < 1500; ++f)
	{
		g.frames(1);
		const Coord3D q = *hero->getPosition();
		if ((q.x - top.x) * (q.x - top.x) + (q.y - top.y) * (q.y - top.y) < 400.0f && !ai->isMoving())
		{
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	MESSAGE("the hero from " << spawn.x << ", " << spawn.y << " to the wall top at " << top.x << ", " << top.y << " stands at " << at.x << ", " << at.y << ", " << at.z
	                         << " on layer " << (int)ai->adapter().getLayer());
	CHECK(isWallLayer(ai->adapter().getLayer()));
	CHECK(at.z == pf.getLayerHeight(at.x, at.y, ai->adapter().getLayer()));
	CHECK((at.x - top.x) * (at.x - top.x) + (at.y - top.y) * (at.y - top.y) < 900.0f); // tolerance: within the close-enough distance of the goal cell
}

// From lane BRIDGE-1 (its test "MAP WOR Erebor: DaleBridge ..."): RotWK's classifyObjectFootprint (RW 0x936B7D) calls ClassifyWall (RW 0x9378A3) only with
// the WALK_ON_TOP_OF_WALL flag (template + 0x10F bit 4, tested at RW 0x937881). Erebor's seven DaleBridges have a WallBoundsMesh and ramps but not the
// KindOf: their deck cells stay on the ground layer and no wall height comes from their deck (300.6; EreborEdgeBridge's, a WALK_ON_TOP_OF_WALL bridge, is 303.6)
TEST_CASE("wall2 walls retail: MAP WOR Erebor: DaleBridge (meshes, no WALK_ON_TOP_OF_WALL) is no wall top or ramp: RW 0x936B7D calls ClassifyWall only for the KindOf")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s, false, "maps/map wor erebor/map wor erebor.map");
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	int dale = 0;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() != "DaleBridge")
		{
			continue;
		}
		++dale;
		CHECK_FALSE(o->isKindOfName("WALK_ON_TOP_OF_WALL"));
		const PathfindWallGeometry *geo = g.game->ai().adapterFor(*o).getWallGeometry();
		REQUIRE(geo); // the meshes are there (WallBoundsMesh P1, RampMesh1 R1, RampMesh2 R2) ...
		for (const PathfindWallSurface &w : geo->walls)
		{
			const float cx = (w.loX + w.hiX) * 0.5f, cy = (w.loY + w.hiY) * 0.5f;
			const PathfindCell *c = pf.cellAt((int)(cx / 10.0f), (int)(cy / 10.0f));
			REQUIRE(c);
			CHECK(c->getLayer() == LAYER_GROUND); // ... but the deck's cells stay on the ground layer
			CHECK(pf.getLayerAt(cx, cy) == LAYER_GROUND);
		}
	}
	CHECK(dale == 7);
	for (float h : pf.wallHeights())
	{
		CHECK(h > 302.0f); // tolerance: no wall height at DaleBridge's deck (300.6); EreborEdgeBridge's is 303.6
	}
}

namespace
{
// a point on the deck of the wall piece `piece` whose cell is a CLEAR wall-top cell of that piece, and its layer (searched from the deck box's centre outward)
bool deckPoint(Pathfinder &pf, const PathfindWallSurface &deck, PathfindObjectID piece, Coord3D &out, PathfindLayerEnum &layer)
{
	const float cx = (deck.loX + deck.hiX) * 0.5f, cy = (deck.loY + deck.hiY) * 0.5f;
	for (int r = 0; r < 20; ++r)
	{
		for (int dx = -r; dx <= r; ++dx)
		{
			for (int dy = -r; dy <= r; ++dy)
			{
				const Coord3D p{ cx + (float)dx * 10.0f, cy + (float)dy * 10.0f, 0.0f };
				float z = 0.0f;
				const PathfindCell *c = pf.cellAt((int)(p.x / 10.0f), (int)(p.y / 10.0f));
				if (deck.heightAt(p.x, p.y, z) && c && isWallLayer(c->getLayer()) && c->getType() == PathfindCell::CELL_CLEAR && pf.wallPieceAt(p) == piece)
				{
					out = p;
					layer = c->getLayer();
					return true;
				}
			}
		}
	}
	return false;
}
} // namespace

// From lane BRIDGE-1 (its test "on MAP MP Anfalas a GenericTreeBridge ... deleted with a hero on its deck ..."): AIMover::refreshLayer, on the frame a
// unit's settled layer becomes the ground (its wall top or bridge deck went away), puts a NO_Z_MOTIVE_FORCE non-horde unit at the terrain height there, as
// RotWK's NO_Z_MOTIVE_FORCE height (RW 0x5E76AC case 0, RW 0x5E774A .. 0x5E7816; maintainCurrentPosition's tail RW 0x5E7CC7 -> RW 0x70BA76 for an idle
// one) does; RotWK has no fall and no fall damage. An idle hero on a GenericTreeBridge's deck ends on the ground under it, alive and unhurt, when the
// bridge is deleted (a script's delete, GameLogic::destroyObject)
TEST_CASE("wall2 walls retail: on MAP MP Anfalas a hero idle on a GenericTreeBridge's deck ends on the ground under it, alive, when the bridge is deleted")
{
	OPENBFME_REQUIRE_START(s);
	PortalGame g(*s, false, "maps/map mp anfalas/map mp anfalas.map");
	g.frames(2);
	Player *me = nullptr;
	for (int i = 0; i < g.game->players().getPlayerCount() && !me; ++i)
	{
		Player *p = g.game->players().getNthPlayer(i);
		me = p && p->getPlayerType() == PLAYER_HUMAN ? p : nullptr;
	}
	REQUIRE(me);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *bridge = g.first("GenericTreeBridge");
	REQUIRE(bridge);
	const ObjectID bridgeID = bridge->getID();
	const PathfindWallGeometry *geo = g.game->ai().adapterFor(*bridge).getWallGeometry();
	REQUIRE(geo);
	REQUIRE(!geo->walls.empty());
	Coord3D onDeck;
	PathfindLayerEnum deckLayer = LAYER_INVALID;
	REQUIRE(deckPoint(pf, geo->walls.front(), (PathfindObjectID)bridgeID, onDeck, deckLayer));
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", me->getPlayerIndex(), onDeck, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	// on the deck's surface (createObject puts it on the terrain; a map's units on walls carry their height)
	onDeck.z = pf.getLayerHeight(onDeck.x, onDeck.y, deckLayer);
	hero->setPosition(&onDeck);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	ai->aiIdle(CMD_FROM_AI); // not the guard of its creation point on the terrain under the deck
	g.frames(3);
	const Coord3D before = *hero->getPosition();
	REQUIRE(ai->adapter().getLayer() == deckLayer);
	REQUIRE(before.z > g.game->logic().getGroundHeight(before.x, before.y) + 5.0f); // tolerance: up on the deck
	const float healthBefore = hero->getBodyModule()->getHealth();
	g.game->logic().destroyObject(bridge);
	g.frames(1);
	CHECK(g.game->logic().findObjectByID(bridgeID) == nullptr);
	g.frames(5);
	const Coord3D after = *hero->getPosition();
	INFO("the hero at " << after.x << ", " << after.y << ", " << after.z << " on layer " << (int)ai->adapter().getLayer());
	CHECK_FALSE(hero->isEffectivelyDead());
	CHECK(hero->getBodyModule()->getHealth() == healthBefore);
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
	CHECK(after.z == g.game->logic().getGroundHeight(after.x, after.y));
	CHECK(pf.cellAt((int)(onDeck.x / 10.0f), (int)(onDeck.y / 10.0f))->getLayer() == LAYER_GROUND);
}
