// OpenBFME. GPL-3.0.
// Narrow test for TEAM_WANDER and AIWanderState (RW 0x7CC3FF -> 0x7C955B, 0x74469E).

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIWaypointPath.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DeployStyleAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>
#include <vector>

namespace
{
WorldHeightMap flatWanderMap(int w, int h)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = 0;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, 0);
	m.m_hasBlendTileData = true;
	m.m_tileNdxes.assign((size_t)w * h, 0);
	m.m_blendTileNdxes.assign((size_t)w * h, 0);
	m.m_extraBlendTileNdxes.assign((size_t)w * h, 0);
	m.m_cliffInfoNdxes.assign((size_t)w * h, 0);
	m.m_planeStride = (w + 7) / 8;
	m.m_blendedTiles.assign(1, TBlendTileInfo());
	m.m_cliffInfo.assign(1, TCliffInfo());
	m.m_boundaries = { { w, h } };
	return m;
}

MapObject makeWaypoint(int id, const char *name, float x, float y, const char *label)
{
	MapObject a;
	a.m_objectName = "*Waypoints/Waypoint";
	a.m_location = { x, y, 0.0f };
	a.m_properties.setInt("waypointID", id);
	a.m_properties.setAsciiString("waypointName", name);
	a.m_properties.setAsciiString("waypointPathLabel1", label);
	a.m_isWaypoint = true;
	return a;
}
} // namespace

TEST_CASE("LOCAL-57 synthetic: TEAM_WANDER enters AI_WANDER, sets LOCOMOTORSET_WANDER and WANDER flag, xfers, and restores on exit (RW 0x7C955B, 0x750B75, 0x74BD02)")
{
	const char *extraIni = R"(
Locomotor WanderLoco
  Surfaces = GROUND
  Speed = 25
  WanderWidthFactor = 1.5
End
Object WanderCivilian
  KindOf = INFANTRY SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100
  End
  Behavior = AIUpdateInterface ModuleTag_AI
  End
  LocomotorSet
    Locomotor = WalkerLoco
    Condition = SET_NORMAL
    Speed = 10
  End
  LocomotorSet
    Locomotor = WanderLoco
    Condition = SET_WANDER
    Speed = 25
  End
End
Object ImmobileProp
  KindOf = STRUCTURE IMMOBILE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100
  End
  Behavior = AIUpdateInterface ModuleTag_AI
  End
End
)";
	combattest::CombatWorld w(extraIni, 101);
	WorldHeightMap hmap = flatWanderMap(101, 101);
	MapChunks chunks;
	chunks.objects = {
		makeWaypoint(1, "WP1", 200.0f, 200.0f, "WanderPath"),
		makeWaypoint(2, "WP2", 400.0f, 200.0f, "WanderPath"),
		makeWaypoint(3, "WP3", 600.0f, 200.0f, "WanderPath"),
	};
	chunks.waypointLinks = { { 1, 2 }, { 2, 3 } };
	TerrainLogic terrain;
	std::vector<std::string> problems;
	terrain.init(hmap, chunks, &problems);
	REQUIRE(problems.empty());
	w.logic->setTerrain(&terrain);

	std::vector<ScriptList> lists(w.players.getPlayerCount());
	std::vector<ScriptEngine::SideScripts> sides((size_t)w.players.getPlayerCount());
	for (int i = 0; i < w.players.getPlayerCount(); ++i)
	{
		sides[(size_t)i].sideName = w.players.getNthPlayer(i)->getPlayerName();
		sides[(size_t)i].lists.push_back(&lists[(size_t)i]);
	}
	w.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);

	Object *civ = w.unit("WanderCivilian", 'A', 380.0f, 200.0f);
	Object *prop = w.unit("ImmobileProp", 'A', 100.0f, 100.0f);
	AIUpdateInterface *ai = civ->getAIUpdateInterface();
	AIUpdateInterface *propAi = prop->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(propAi != nullptr);
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);
	// RW 0x664858: immobile objects refuse aiWander
	CHECK(!propAi->aiWander(2, CMD_FROM_SCRIPT));

	// Issue TEAM_WANDER on teamA along "WanderPath" (RW 0x7CC3FF -> 0x7C955B)
	Team *t = w.teamA();
	const std::string teamQual = "Alice/" + t->getPrototype()->getName();
	ScriptActionRec act = scripttest::act("TEAM_WANDER", { scripttest::sp(3, teamQual), scripttest::sp(24, "WanderPath") });
	ScriptActions::execute(w.logic->scriptEngine(), act);
	CHECK(w.logic->scriptEngine().stats().unportedActions.empty());

	w.logic->runLogicFrame();
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_WANDER);
	CHECK(ai->currentStateId() == (unsigned)AI_WANDER);
	const int wanderBit = CombatNames::modelCondition("WANDER");
	const bool wanderBitValid = wanderBit >= 0;
	REQUIRE(wanderBitValid);
	const bool wanderSet = (civ->getModelConditionBits()[(size_t)wanderBit / 32u] & (1u << (wanderBit % 32))) != 0;
	CHECK(wanderSet);

	// xfer round-trip mid-wander (RW 0x742BB6)
	std::vector<std::uint8_t> bytes;
	{
		XferSave save;
		ai->stateMachine().xfer(save);
		bytes = save.data();
	}
	{
		XferLoad load(bytes);
		ai->stateMachine().xfer(load);
		REQUIRE(load.atEnd());
	}
	CHECK(ai->currentStateId() == (unsigned)AI_WANDER);

	// Step until end of WanderPath (WP3 has no links) -> AI_IDLE, WANDER cleared, LOCOMOTORSET_NORMAL restored (RW 0x74BD02)
	for (int i = 0; i < 150; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);
	CHECK(((civ->getModelConditionBits()[(size_t)wanderBit / 32u] & (1u << (wanderBit % 32))) == 0));
	CHECK(civ->getPosition()->x > 550.0f); // tolerance: WanderWidthFactor 1.5 adds +/-20 cell offset around WP3 (x=600)

	w.logic->setTerrain(nullptr);
}

namespace
{
// the three-waypoint "WanderPath" on a flat 101 x 101 map, as the test above builds it
struct WanderTerrain
{
	WorldHeightMap hmap = flatWanderMap(101, 101);
	MapChunks chunks;
	TerrainLogic terrain;
	explicit WanderTerrain(combattest::CombatWorld &w)
	{
		chunks.objects = {
			makeWaypoint(1, "WP1", 200.0f, 200.0f, "WanderPath"),
			makeWaypoint(2, "WP2", 400.0f, 200.0f, "WanderPath"),
			makeWaypoint(3, "WP3", 600.0f, 200.0f, "WanderPath"),
		};
		chunks.waypointLinks = { { 1, 2 }, { 2, 3 } };
		std::vector<std::string> problems;
		terrain.init(hmap, chunks, &problems);
		REQUIRE(problems.empty());
		w.logic->setTerrain(&terrain);
	}
};

bool stopNoted(GameLogic &logic, const char *id)
{
	for (const std::string &l : logic.report().stops)
	{
		if (l.rfind(id, 0) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("LOCAL-57 synthetic: a wander kept while a DeployStyleAIUpdate unit unpacks runs once it is deployed instead of aborting the match (RW 0x891F9B replays through aiDoCommand)")
{
	const char *extraIni = R"(
Object WanderDeployer
  KindOf = VEHICLE SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100
  End
  Behavior = DeployStyleAIUpdate ModuleTag_AI
    UnpackTime = 1000
    PackTime = 1000
    MustDeployToAttack = No
  End
  LocomotorSet
    Locomotor = WalkerLoco
    Condition = SET_NORMAL
    Speed = 10
  End
End
)";
	combattest::CombatWorld w(extraIni, 101);
	WanderTerrain terrain(w);
	Object *unit = w.unit("WanderDeployer", 'A', 380.0f, 200.0f);
	DeployStyleAIUpdate *ai = DeployStyleAIUpdate::of(*unit);
	REQUIRE(ai != nullptr);
	ai->requestDeploy(); // RW 0x891D3B
	int f = 0;
	for (; f < 10 && ai->state() != DeployStyleAIUpdate::DEPLOY; ++f)
	{
		w.logic->runLogicFrame();
	}
	REQUIRE(ai->state() == DeployStyleAIUpdate::DEPLOY); // unpacking: a command now waits (RW 0x892120)
	CHECK(ai->aiWander(1, CMD_FROM_SCRIPT));
	CHECK(ai->hasPendingCommand());
	bool wandered = false;
	CHECK_NOTHROW([&] {
		for (int i = 0; i < 40; ++i)
		{
			w.logic->runLogicFrame();
			wandered = wandered || ai->currentStateId() == (unsigned)AI_WANDER;
		}
	}());
	CHECK_FALSE(ai->hasPendingCommand());
	CHECK(ai->replays() == 2u); // once deployed (state 2), once packed again for the wander's path (state 0), as the kept move of siege1 demolisher
	CHECK(wandered); // the kept wander ran (AICMD_WANDER 0x2B through the replay)
	w.logic->setTerrain(nullptr);
}

TEST_CASE("LOCAL-57 synthetic: a CAN_BE_REPULSED wanderer reaching the unported repulsor scan reports S-7581 (RW 0x74BBCA)")
{
	const char *extraIni = R"(
Object RepulsedWanderer
  KindOf = INFANTRY SELECTABLE CAN_BE_REPULSED
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100
  End
  Behavior = AIUpdateInterface ModuleTag_AI
  End
  LocomotorSet
    Locomotor = WalkerLoco
    Condition = SET_NORMAL
    Speed = 10
  End
End
Object PlainWanderer
  KindOf = INFANTRY SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 100
  End
  Behavior = AIUpdateInterface ModuleTag_AI
  End
  LocomotorSet
    Locomotor = WalkerLoco
    Condition = SET_NORMAL
    Speed = 10
  End
End
)";
	{
		combattest::CombatWorld w(extraIni, 101);
		WanderTerrain terrain(w);
		Object *plain = w.unit("PlainWanderer", 'A', 380.0f, 200.0f);
		REQUIRE(plain->getAIUpdateInterface()->aiWander(1, CMD_FROM_SCRIPT));
		for (int i = 0; i < 20; ++i)
		{
			w.logic->runLogicFrame();
		}
		CHECK_FALSE(stopNoted(*w.logic, "[S-7581]")); // a unit that cannot be repulsed never reaches the scan
		w.logic->setTerrain(nullptr);
	}
	combattest::CombatWorld w(extraIni, 101);
	WanderTerrain terrain(w);
	Object *civ = w.unit("RepulsedWanderer", 'A', 380.0f, 200.0f);
	REQUIRE(civ->getAIUpdateInterface()->aiWander(1, CMD_FROM_SCRIPT));
	for (int i = 0; i < 20; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(civ->getAIUpdateInterface()->currentStateId() == (unsigned)AI_WANDER);
	CHECK(stopNoted(*w.logic, "[S-7581]"));
	w.logic->setTerrain(nullptr);
}
