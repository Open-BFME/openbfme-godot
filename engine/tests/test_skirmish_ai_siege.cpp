// OpenBFME unit tests, lane RAI-SIEGE: the skirmish AI's shared path-to-object test RW 0x6F5F65 that RW 0x9EF9AF asks, on retail Helm's Deep (a walled
// fortress with its Main Gate). The siege tactics' applicability RW 0x9B333D / 0x9B313C is the observable: both apply only when the target cannot be pathed
// to, SimpleSiege only with a siege engine and SiegeGates only without one. SKIPs loudly without the installs.

#include "doctest.h"

#include "Siege1TestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";

struct WallTest
{
	siege1test::FortressGame game;
	Object *gate = nullptr;
	Object *inside = nullptr;
	Player *men = nullptr;
	Player *mordor = nullptr;
	Coord3D outsideDir{ 1.0f, 0.0f, 0.0f };

	explicit WallTest(starttest::Shared &s) : game(s, kHelmsDeep, false)
	{
		game.frames(2);
		GameLogic &logic = game.game->logic();
		gate = game.named("Main Gate");
		REQUIRE(gate != nullptr);
		men = game.ofFaction("FactionMen");
		mordor = game.ofFaction("FactionMordor");
		REQUIRE(men != nullptr);
		REQUIRE(mordor != nullptr);
		const Waypoint *w = logic.terrain()->findWaypointByName("Player_2_Start");
		REQUIRE(w != nullptr);
		const Coord3D gatePos = *gate->getPosition();
		float dx = w->location.x - gatePos.x, dy = w->location.y - gatePos.y;
		const float len = std::sqrt(dx * dx + dy * dy);
		REQUIRE(len > 1.0f); // tolerance: a map distance, only used to normalise the outside direction; the gate and the start waypoint are far apart
		outsideDir = Coord3D{ dx / len, dy / len, 0.0f };
		// a Men structure behind the gate (on the far side from the Mordor start): the target a walled base hides. The nearest one, so the teams' walk at the
		// end of the tactic is short
		float best = -1000.0f;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o == gate || o->getControllingPlayer() != men || !o->isKindOfName("STRUCTURE"))
			{
				continue;
			}
			const float dot = (o->getPosition()->x - gatePos.x) * outsideDir.x + (o->getPosition()->y - gatePos.y) * outsideDir.y;
			if (dot < -100.0f && (!inside || dot > best))
			{
				inside = o;
				best = dot;
			}
		}
		REQUIRE(inside != nullptr);
		// the gate stands shut for the closed-wall checks (the map may open it by default)
		GateOpenAndCloseBehavior *gateModule = GateOpenAndCloseBehavior::findGate(*gate);
		REQUIRE(gateModule != nullptr);
		gateModule->close();
		for (int f = 0; f < 600 && gateModule->state() != GateOpenAndCloseBehavior::CLOSED; ++f)
		{
			game.frames(1);
		}
		REQUIRE(gateModule->state() == GateOpenAndCloseBehavior::CLOSED);
		// the map's creeps are removed: their guard scans and lairs draw from the game's random numbers and would attack the test's units (as test_siege1_gate)
		std::vector<Object *> creeps;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o != gate && o->getTeam() && mordor->getRelationship(o->getTeam()) == ENEMIES && men->getRelationship(o->getTeam()) == ENEMIES)
			{
				creeps.push_back(o);
			}
		}
		for (Object *o : creeps)
		{
			logic.destroyObject(o);
		}
		game.frames(2);
	}

	Object *spawn(const char *name, float distanceOutside)
	{
		const Coord3D gatePos = *gate->getPosition();
		std::string error;
		Object *o = game.game->createObject(name, mordor->getPlayerIndex(),
			Coord3D{ gatePos.x + outsideDir.x * distanceOutside, gatePos.y + outsideDir.y * distanceOutside,
				game.game->logic().getGroundHeight(gatePos.x + outsideDir.x * distanceOutside, gatePos.y + outsideDir.y * distanceOutside) },
			0.0f, &error);
		REQUIRE_MESSAGE(o != nullptr, error);
		return o;
	}

	void frames(int n) { game.frames(n); }
};

void useAI(AISkirmishPlayer &ai, Player &player, GameLogic &logic)
{
	ai.playerIndex = player.getPlayerIndex();
	ai.store = logic.skirmishAI().store();
	REQUIRE(ai.store != nullptr);
}

bool pathTo(GameLogic &logic, Object &unit, Object &target)
{
	AIUpdateInterface *u = unit.getAIUpdateInterface();
	REQUIRE(u != nullptr);
	AIWorld *world = logic.aiWorld();
	REQUIRE(world != nullptr);
	return world->pathfinder().pathToObject(u->adapter(), u->locomotorInfo(), *unit.getPosition(), world->adapterFor(target));
}
} // namespace

TEST_CASE("skirmish ai path: RW 0x9EF9AF asks RW 0x6F5F65 for the target object, so the siege tactics apply exactly when a walled target cannot be pathed to")
{
	OPENBFME_REQUIRE_START(s);
	WallTest w(*s);
	GameLogic &logic = w.game.game->logic();
	Object *troll = w.spawn("MordorCaveTroll", 300.0f);
	w.frames(5);
	REQUIRE(logic.findObjectByID(troll->getID()) != nullptr);
	const std::vector<ObjectID> &army = AITacticalAI::recordOf(logic, w.mordor->getPlayerIndex()).army();
	REQUIRE(std::find(army.begin(), army.end(), troll->getID()) != army.end());

	AISkirmishPlayer ai;
	useAI(ai, *w.mordor, logic);
	AIBrainState brain;
	brain.currentEnemy = w.men->getPlayerIndex();
	AITarget target;
	target.type = 0; // ENEMY_STRUCTURE
	target.position = *w.inside->getPosition();
	target.object = w.inside->getID();
	target.inactive = false;

	// the gate stands: RW 0x9EF9AF's unit cannot path to the target object, so a siege tactic applies (RW's vslot 0x34 XOR)
	CHECK_FALSE(pathTo(logic, *troll, *w.inside));
	CHECK(AITacticalAI::offensiveAppliesForTest(logic, ai, brain, "SiegeGates", target));
	CHECK_FALSE(AITacticalAI::offensiveAppliesForTest(logic, ai, brain, "SimpleSiege", target));

	// the gate opens: the path is there, so no siege tactic applies
	GateOpenAndCloseBehavior *gateModule = GateOpenAndCloseBehavior::findGate(*w.gate);
	REQUIRE(gateModule != nullptr);
	gateModule->open();
	for (int f = 0; f < 600 && !gateModule->isOpen(); ++f)
	{
		logic.runLogicFrame();
	}
	REQUIRE(gateModule->isOpen());
	w.frames(30);
	CHECK(pathTo(logic, *troll, *w.inside));
	CHECK_FALSE(AITacticalAI::offensiveAppliesForTest(logic, ai, brain, "SiegeGates", target));

	// closed again, with a siege engine in the army: SimpleSiege applies and SiegeGates does not (RW 0x9B333D / 0x9B313C with the engine test RW 0x9B3734)
	gateModule->close();
	for (int f = 0; f < 600 && !(gateModule->state() == GateOpenAndCloseBehavior::CLOSED); ++f)
	{
		logic.runLogicFrame();
	}
	Object *catapult = w.spawn("MordorCatapult", 350.0f);
	w.frames(30);
	REQUIRE(logic.findObjectByID(catapult->getID()) != nullptr);
	CHECK_FALSE(pathTo(logic, *catapult, *w.inside));
	CHECK(AITacticalAI::offensiveAppliesForTest(logic, ai, brain, "SimpleSiege", target));
	CHECK_FALSE(AITacticalAI::offensiveAppliesForTest(logic, ai, brain, "SiegeGates", target));
}

namespace
{
// lane RAI-SIEGE review: RW 0x6F5F65 written straight from the disassembly (RW 0x6F5F65 .. 0x6F61B7, the walk RW 0x6F5EF3 with RW 0x938D89 = the
// strict odd table, getEffectiveZone(mv, false, ..)): an OBSTACLE centre cell walks at once; any other cell retries at the first shape's offset
bool retailPathToObject(Pathfinder &pf, const PathfindObject &unit, const PathfindLocomotorInfo &loco, const Coord3D &from, const PathfindObject &target)
{
	if ((loco.validSurfaces & 0x8F) == 0 || !unit.hasAI())
	{
		return false;
	}
	Coord3D to = target.getPosition();
	const PathfindLayerEnum layer = pf.searchDestinationLayer(to);
	ICoord2D c;
	pf.worldToCell(&to, true, &c);
	const PathfindCell *goal = pf.getCell(layer, c.x, c.y);
	REQUIRE(goal != nullptr);
	if (goal->getType() != PathfindCell::CELL_OBSTACLE)
	{
		const PathfindShape first = target.getGeometry().shape(0);
		to.x = SimMath::addf32(to.x, first.offsetX);
		to.y = SimMath::addf32(to.y, first.offsetY);
		to.z = SimMath::addf32(to.z, first.offsetZ);
		pf.worldToCell(&to, true, &c);
		goal = pf.getCell(layer, c.x, c.y);
		REQUIRE(goal != nullptr);
		if (goal->getType() != PathfindCell::CELL_OBSTACLE)
		{
			return pf.quickDoesPathExist(unit, &from, &to, &loco);
		}
	}
	const PathfindMovement mv = Pathfinder::makeMovement(&unit, loco);
	const PathfindCell *fromCell = pf.getClippedCell(pf.searchDestinationLayer(from), &from);
	REQUIRE(fromCell != nullptr);
	const zoneStorageType fromZone = pf.zoneManager().getEffectiveZone(mv, false, fromCell->getZone());
	const PathfindObjectID obstacle = goal->getObstacleID();
	const int d = 2 - SimMath::truncToInt32(SimMath::mulf32(target.getGeometry().boundingCircleRadius(), -0.1f));
	const int dirs[4][2] = { { d, 0 }, { -d, 0 }, { 0, d }, { 0, -d } };
	for (const auto &dir : dirs)
	{
		const int steps = std::max(std::abs(dir[0]), std::abs(dir[1]));
		for (int k = 0; k <= steps; ++k)
		{
			const PathfindCell *cell = pf.getCell(layer, c.x + (steps ? dir[0] * k / steps : 0), c.y + (steps ? dir[1] * k / steps : 0));
			if (!cell)
			{
				break;
			}
			if (cell->getObstacleID() != obstacle)
			{
				const zoneStorageType z = pf.zoneManager().getEffectiveZone(mv, false, cell->getZone());
				if (z == fromZone || pf.portalPathExists(&unit, loco, fromZone, z, false))
				{
					return true;
				}
				break;
			}
		}
	}
	return false;
}

void compareAll(GameLogic &logic, Object &unit, const char *where, int &checked, int &differ)
{
	AIWorld *world = logic.aiWorld();
	REQUIRE(world != nullptr);
	AIUpdateInterface *u = unit.getAIUpdateInterface();
	REQUIRE(u != nullptr);
	Pathfinder &pf = world->pathfinder();
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o == &unit || !o->isKindOfName("STRUCTURE"))
		{
			continue;
		}
		const bool port = pf.pathToObject(u->adapter(), u->locomotorInfo(), *unit.getPosition(), world->adapterFor(*o));
		const bool retail = retailPathToObject(pf, u->adapter(), u->locomotorInfo(), *unit.getPosition(), world->adapterFor(*o));
		++checked;
		if (port != retail)
		{
			++differ;
			const PathfindShape first = world->adapterFor(*o).getGeometry().shape(0);
			MESSAGE(where << ": " << o->getTemplate()->getName() << " '" << o->getName() << "' at (" << o->getPosition()->x << ", " << o->getPosition()->y
							<< ") first shape offset (" << first.offsetX << ", " << first.offsetY << ") port " << port << " retail " << retail);
		}
	}
}
} // namespace

TEST_CASE("skirmish ai path: Pathfinder::pathToObject answers as RW 0x6F5F65 for every structure (Helm's Deep, gate shut and open)")
{
	OPENBFME_REQUIRE_START(s);
	WallTest w(*s);
	GameLogic &logic = w.game.game->logic();
	Object *outside = w.spawn("MordorCaveTroll", 300.0f);
	Object *far = w.spawn("MordorCaveTroll", 1500.0f);
	w.frames(5);
	int checked = 0, differ = 0;
	compareAll(logic, *outside, "shut, at the gate", checked, differ);
	compareAll(logic, *far, "shut, far", checked, differ);
	GateOpenAndCloseBehavior *gateModule = GateOpenAndCloseBehavior::findGate(*w.gate);
	REQUIRE(gateModule != nullptr);
	gateModule->open();
	for (int f = 0; f < 600 && !gateModule->isOpen(); ++f)
	{
		logic.runLogicFrame();
	}
	w.frames(30);
	compareAll(logic, *outside, "open, at the gate", checked, differ);
	MESSAGE("checked " << checked << " differ " << differ);
	CHECK(checked > 0); // tolerance: the map's structure count is retail data; the probe only needs some
	CHECK(differ == 0);
}

TEST_CASE("skirmish ai siege: SiegeGates attacks the wall gate while the path is blocked, then sends every team at the target once it opens (RW 0x9B31E6 / 0x9B3184 / 0x9EF829)")
{
	OPENBFME_REQUIRE_START(s);
	WallTest w(*s);
	GameLogic &logic = w.game.game->logic();
	AISkirmishPlayer ai;
	useAI(ai, *w.mordor, logic);
	AIBrainState brain;
	brain.currentEnemy = w.men->getPlayerIndex();
	AIBrainState owner;
	AITarget target;
	target.type = 0; // ENEMY_STRUCTURE
	target.position = *w.inside->getPosition();
	target.object = w.inside->getID();
	target.inactive = false;
	target.teams = 1;
	owner.targets.push_back(target);
	AITactic tac;
	tac.kindIndex = 7;
	tac.kind = "SiegeGates";
	tac.targetIndex = 0;
	tac.started = true;
	AITacticTeam team;
	team.index = 0;
	team.type = 0;
	team.handedOver = true;
	std::vector<Object *> units;
	for (int i = 0; i < 5; ++i)
	{
		Object *u = w.spawn("MordorCaveTroll", 300.0f + 12.0f * (float)i);
		units.push_back(u);
		team.members.push_back(u->getID());
	}
	w.frames(5);
	tac.teams.push_back(team);

	AITacticalAI::launchTacticForTest(logic, ai, brain, tac, owner); // RW 0x9B31C5 -> 0x9B3184
	CHECK(tac.squadTarget == w.gate->getID());
	CHECK(tac.step == 0); // RW 0x9B31E6: the wall stage
	w.frames(3);
	for (Object *u : units)
	{
		AIUpdateInterface *uai = u->getAIUpdateInterface();
		REQUIRE(uai != nullptr);
		CHECK(uai->stateMachine().goalObjectID() == w.gate->getID()); // RW 0x8F1792: team 0 attacks the wall gate
	}
	// the wall target lives and the path is blocked: the stage stays at the wall
	AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner);
	CHECK(tac.step == 0);

	// the breach: the gate is destroyed, the path opens; every team attack-moves to the target and the stage becomes 1
	logic.destroyObject(w.gate);
	w.gate = nullptr;
	w.frames(30);
	AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner); // RW 0x9EF783 -> RW 0x8F19E9
	CHECK(tac.step == 1);
	for (Object *u : units)
	{
		AIUpdateInterface *uai = u->getAIUpdateInterface();
		REQUIRE(uai != nullptr);
		CHECK(uai->attackMoveActive());
	}
	// the target is gone and the teams are idle: every team idle for TeamTimeUntilConsideredIdle ends it with success
	logic.destroyObject(w.inside);
	w.inside = nullptr;
	for (Object *u : units)
	{
		if (AIUpdateInterface *uai = u->getAIUpdateInterface())
		{
			uai->aiIdle(CMD_FROM_AI);
		}
	}
	for (int f = 0; f < 1200 && !tac.ended; ++f)
	{
		logic.runLogicFrame();
		AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner);
	}
	CHECK(tac.ended);
	CHECK(tac.success);
}

TEST_CASE("skirmish ai siege: SimpleSiege attacks the wall with team 0 and stands team 1 500 off, then sends it through the breach (RW 0x9B3548 / 0x9B3453)")
{
	OPENBFME_REQUIRE_START(s);
	WallTest w(*s);
	GameLogic &logic = w.game.game->logic();
	AISkirmishPlayer ai;
	useAI(ai, *w.mordor, logic);
	AIBrainState brain;
	brain.currentEnemy = w.men->getPlayerIndex();
	AIBrainState owner;
	AITarget target;
	target.type = 0; // ENEMY_STRUCTURE
	target.position = *w.inside->getPosition();
	target.object = w.inside->getID();
	target.inactive = false;
	target.teams = 1;
	owner.targets.push_back(target);
	AITactic tac;
	tac.kindIndex = 6;
	tac.kind = "SimpleSiege";
	tac.targetIndex = 0;
	tac.started = true;
	AITacticTeam team0;
	team0.index = 0;
	team0.type = 0;
	team0.handedOver = true;
	AITacticTeam team1;
	team1.index = 1;
	team1.type = 0;
	team1.handedOver = true;
	Object *catapult = w.spawn("MordorCatapult", 300.0f);
	Object *troll0 = w.spawn("MordorCaveTroll", 312.0f);
	team0.members.push_back(catapult->getID());
	team0.members.push_back(troll0->getID());
	std::vector<Object *> assault;
	for (int i = 0; i < 3; ++i)
	{
		Object *u = w.spawn("MordorCaveTroll", 330.0f + 12.0f * (float)i);
		assault.push_back(u);
		team1.members.push_back(u->getID());
	}
	w.frames(5);
	tac.teams.push_back(team0);
	tac.teams.push_back(team1);

	AITacticalAI::launchTacticForTest(logic, ai, brain, tac, owner); // RW 0x9B3380: state 0
	CHECK(tac.step == 0);
	const Coord3D leader = *catapult->getPosition();
	AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner); // RW 0x9B3453
	CHECK(tac.step == 1);
	CHECK(tac.squadTarget == w.gate->getID());
	for (Object *u : { catapult, troll0 })
	{
		AIUpdateInterface *uai = u->getAIUpdateInterface();
		REQUIRE(uai != nullptr);
		CHECK(uai->stateMachine().goalObjectID() == w.gate->getID()); // team 0 attacks the wall
	}
	for (Object *u : assault)
	{
		AIUpdateInterface *uai = u->getAIUpdateInterface();
		REQUIRE(uai != nullptr);
		CHECK(uai->attackMoveActive()); // team 1 moves to the stand-back point (the group's own slots)
	}
	const Coord3D stand = AITacticalAI::siegeStandBackPointForTest(leader, *w.gate->getPosition()); // RW 0xC894AC: 500, in 3D
	const float sx = stand.x - w.gate->getPosition()->x, sy = stand.y - w.gate->getPosition()->y, sz = stand.z - w.gate->getPosition()->z;
	CHECK(std::fabs(std::sqrt(sx * sx + sy * sy + sz * sz) - 500.0f) < 0.05f); // tolerance: float32 normalise and the map's position bits
	CHECK(sx * (leader.x - w.gate->getPosition()->x) >= 0.0f); // tolerance: the sign only says the leader lies on the same side as the stand-back step
	CHECK(sy * (leader.y - w.gate->getPosition()->y) >= 0.0f); // tolerance: the sign only says the leader lies on the same side as the stand-back step

	// the breach: the gate is destroyed, the path opens; stage 1 -> 2, then team 1 goes at the target (2 -> 3)
	logic.destroyObject(w.gate);
	w.gate = nullptr;
	w.frames(30);
	AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner); // RW 0x9EF783
	CHECK(tac.step == 2);
	AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner); // RW 0x8F17EE(1, target)
	CHECK(tac.step == 3);
	for (Object *u : assault)
	{
		AIUpdateInterface *uai = u->getAIUpdateInterface();
		REQUIRE(uai != nullptr);
		CHECK(uai->attackMoveActive());
	}
	// the target is gone and the teams are idle: team 1 idle for TeamTimeUntilConsideredIdle ends it with success (RW 0x8F1725)
	logic.destroyObject(w.inside);
	w.inside = nullptr;
	for (Object *u : { catapult, troll0 })
	{
		if (AIUpdateInterface *uai = u->getAIUpdateInterface())
		{
			uai->aiIdle(CMD_FROM_AI);
		}
	}
	for (Object *u : assault)
	{
		if (AIUpdateInterface *uai = u->getAIUpdateInterface())
		{
			uai->aiIdle(CMD_FROM_AI);
		}
	}
	for (int f = 0; f < 1200 && !tac.ended; ++f)
	{
		logic.runLogicFrame();
		AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner);
	}
	CHECK(tac.ended);
	CHECK(tac.success);
}

// review of #228: team 0's first member is the first one of the team's object list (RW 0x7A11FF -> RW 0x66362D), which never holds a destroyed object; the
// port keeps a dead member's id in `members` until the whole team is gone, so the wall search and the stand-back point must skip it
TEST_CASE("skirmish ai siege: a destroyed first member of team 0 does not stop the wall search (RW 0x9EF829 / 0x9B3453 take the team's first object)")
{
	OPENBFME_REQUIRE_START(s);
	WallTest w(*s);
	GameLogic &logic = w.game.game->logic();
	AISkirmishPlayer ai;
	useAI(ai, *w.mordor, logic);
	AIBrainState brain;
	brain.currentEnemy = w.men->getPlayerIndex();
	AIBrainState owner;
	AITarget target;
	target.type = 0; // ENEMY_STRUCTURE
	target.position = *w.inside->getPosition();
	target.object = w.inside->getID();
	target.inactive = false;
	target.teams = 1;
	owner.targets.push_back(target);

	SUBCASE("SiegeGates")
	{
		AITactic tac;
		tac.kindIndex = 7;
		tac.kind = "SiegeGates";
		tac.targetIndex = 0;
		tac.started = true;
		AITacticTeam team;
		team.index = 0;
		team.type = 0;
		team.handedOver = true;
		std::vector<Object *> units;
		for (int i = 0; i < 3; ++i)
		{
			Object *u = w.spawn("MordorCaveTroll", 300.0f + 12.0f * (float)i);
			units.push_back(u);
			team.members.push_back(u->getID());
		}
		tac.teams.push_back(team);
		logic.destroyObject(units[0]);
		w.frames(5);
		REQUIRE(logic.findObjectByID(tac.teams[0].members[0]) == nullptr);
		AITacticalAI::launchTacticForTest(logic, ai, brain, tac, owner); // RW 0x9B31C5 -> 0x9B3184
		AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner);
		CHECK(tac.squadTarget == w.gate->getID());
		CHECK_FALSE(tac.ended);
	}
	SUBCASE("SimpleSiege")
	{
		AITactic tac;
		tac.kindIndex = 6;
		tac.kind = "SimpleSiege";
		tac.targetIndex = 0;
		tac.started = true;
		AITacticTeam team0;
		team0.index = 0;
		team0.type = 0;
		team0.handedOver = true;
		AITacticTeam team1;
		team1.index = 1;
		team1.type = 0;
		team1.handedOver = true;
		Object *first = w.spawn("MordorCaveTroll", 300.0f);
		Object *catapult = w.spawn("MordorCatapult", 312.0f);
		team0.members.push_back(first->getID());
		team0.members.push_back(catapult->getID());
		for (int i = 0; i < 3; ++i)
		{
			team1.members.push_back(w.spawn("MordorCaveTroll", 330.0f + 12.0f * (float)i)->getID());
		}
		tac.teams.push_back(team0);
		tac.teams.push_back(team1);
		logic.destroyObject(first);
		w.frames(5);
		REQUIRE(logic.findObjectByID(tac.teams[0].members[0]) == nullptr);
		AITacticalAI::launchTacticForTest(logic, ai, brain, tac, owner); // RW 0x9B3380
		AITacticalAI::updateTacticForTest(logic, ai, brain, tac, owner); // RW 0x9B3453 with the catapult as the first object
		CHECK_FALSE(tac.ended);
		CHECK(tac.step == 1);
		CHECK(tac.squadTarget == w.gate->getID());
	}
}
