// OpenBFME unit tests. GPL-3.0.
// Lane ENTERADJUST-1 (RotWK 2.01; BFME2 0x74FB77, decomp AIEnterStateOnEnter.cpp, the same rule: both editions): AIEnterState::onEnter (RW 0x7515E1, at
// RW 0x751668 .. 0x751707) adjusts the entry position for a SHIP container only, makes the container the ignored obstacle (RW 0x7516C8 -> 0x66831A) and runs the
// internal move with adjustsDestination 0 (RW 0x7516FC); its onExit (RW 0x752B0F) clears the ignored obstacle (RW 0x752B31). AIMoveToPositionAndEnterState's
// own + 0x48 = 0 (RW 0x751AB6) is not ported (S-1101: it needs the goal-following update RW 0x755572).
// Synthetic templates: a garrisonable tower with a footprint, a transport cart, a rock.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Module/AIUpdate.h"

#include <cmath>
#include <string>

using namespace combattest;

namespace
{
const char kEnterObjects[] =
	"Object EnterTower\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 10\n"
	"  End\n"
	"End\n"
	"Object Rock\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"ChildObject Walker Swordsman\n"
	"  TransportSlotCount = 1\n"
	"End\n";

// a mobile container of KindOf `kind`
std::string carrier(const char *name, const char *kind)
{
	return std::string("Object ") + name + "\n"
	       "  KindOf = " + kind + " SELECTABLE\n"
	       "  Geometry = CYLINDER\n"
	       "  GeometryMajorRadius = 12\n"
	       "  GeometryMinorRadius = 12\n"
	       "  GeometryHeight = 20\n"
	       "  Body = ActiveBody ModuleTag_Body\n"
	       "    MaxHealth = 500\n"
	       "  End\n"
	       "  Behavior = AIUpdateInterface ModuleTag_AI\n"
	       "  End\n"
	       "  Behavior = TransportContain ModuleTag_Contain\n"
	       "    Slots = 5\n"
	       "    ContainMax = 5\n"
	       "    PassengerFilter = ANY +INFANTRY\n"
	       "  End\n"
	       "  LocomotorSet\n"
	       "    Locomotor = WalkerLoco\n"
	       "    Condition = SET_NORMAL\n"
	       "    Speed = 30\n"
	       "  End\n"
	       "End\n";
}

std::string fixture()
{
	return std::string(kEnterObjects) + carrier("Cart", "VEHICLE");
}

void requireObstacle(CombatWorld &w, Object *o)
{
	const int cx = (int)(o->getPosition()->x / 10.0f), cy = (int)(o->getPosition()->y / 10.0f);
	PathfindCell *c = w.ai->pathfinder().getCell(LAYER_GROUND, cx, cy);
	if (!c || c->getType() != PathfindCell::CELL_OBSTACLE)
	{
		w.ai->addObjectToPathfindMap(*o);
		c = w.ai->pathfinder().getCell(LAYER_GROUND, cx, cy);
	}
	REQUIRE(c);
	REQUIRE(c->getType() == PathfindCell::CELL_OBSTACLE);
}

bool sameXY(const Coord3D &a, const Coord3D &b)
{
	return a.x == b.x && a.y == b.y;
}
} // namespace

TEST_CASE("enteradjust1 (a, b, d): AIEnterState goes to the tower's entry position itself, through its footprint (the ignored obstacle), and onExit clears the ignore")
{
	const std::string objs = fixture();
	CombatWorld w(objs.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *tower = w.unit("EnterTower", 'A', 500, 300);
	Object *man = w.unit("Walker", 'A', 300, 300);
	w.frames(2);
	requireObstacle(w, tower);
	Coord3D entry;
	REQUIRE(tower->getContain()->getEntryPosition(entry));
	AIUpdateInterface *ai = man->getAIUpdateInterface();
	ai->aiEnterObject(tower, CMD_FROM_PLAYER);
	REQUIRE(ai->stateMachine().currentStateId() == (unsigned)AI_ENTER);
	CHECK(ai->mover().ignoredObstacleID() == (PathfindObjectID)tower->getID()); // RW 0x7516C8
	CHECK(sameXY(ai->mover().requestedDestination(), entry)); // the port moved the goal to a free cell beside the tower
	bool pathIntoFootprint = false;
	for (int f = 0; f < 10 && !pathIntoFootprint; ++f)
	{
		w.frames(1);
		Path *p = ai->mover().path();
		if (p && p->getLastNode())
		{
			const Coord3D *last = p->getLastNode()->getPosition();
			pathIntoFootprint = std::abs(last->x - entry.x) < 25.0f && std::abs(last->y - entry.y) < 25.0f;
		}
	}
	CHECK(pathIntoFootprint); // the path ends inside the tower's footprint
	ai->aiIdle(CMD_FROM_AI); // the enter state exits
	CHECK(ai->mover().ignoredObstacleID() == PATHFIND_INVALID_ID); // RW 0x752B31
}

TEST_CASE("enteradjust1 (a): AIEnterState aims at a transport's own position, not a nearby free cell")
{
	const std::string objs = fixture();
	CombatWorld w(objs.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *rock = w.unit("Rock", 'A', 500, 300);
	Object *cart = w.unit("Cart", 'A', 500, 300); // parked on the rock: its entry position is an obstacle cell
	Object *man = w.unit("Walker", 'A', 300, 300);
	w.frames(2);
	requireObstacle(w, rock);
	Coord3D entry;
	REQUIRE(cart->getContain()->getEntryPosition(entry));
	AIUpdateInterface *ai = man->getAIUpdateInterface();
	ai->aiEnterObject(cart, CMD_FROM_PLAYER);
	REQUIRE(ai->stateMachine().currentStateId() == (unsigned)AI_ENTER);
	CHECK(sameXY(ai->mover().requestedDestination(), entry));
}
