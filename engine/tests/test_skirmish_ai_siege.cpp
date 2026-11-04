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
