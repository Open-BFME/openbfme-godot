// OpenBFME unit tests. GPL-3.0.
// Fixture for the movement tests (lane MOVE-1): the LOGIC-1 logic world, a synthetic flat terrain, TheAI (AIWorld) with the pathfinder on it, the
// Locomotor store and the unit / horde templates the tests declare. No retail data.

#pragma once

#include "LogicTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/GameLogicDispatch.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace movetest
{

// Locomotors: the retail shapes (locomotor.ini HumanLocomotor and NormalMeleeHordeLocomotor, spec 1.5) with the values the golden tests compute from
const char kLocomotors[] =
	"Locomotor WalkerLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 500\n"
	"  TurnTimeDamaged = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  MinSpeed = 0\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  StickToGround = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n"
	"Locomotor HordeLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 2000\n"
	"  TurnTimeDamaged = 2000\n"
	"  Acceleration = 500\n"
	"  Braking = 500\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = HORDE\n"
	"  MaxTurnWithoutReform = 45\n"
	"  TurnWhileMoving = No\n"
	"  WaitForFormation = Yes\n"
	"  FormationPriority = MELEE1\n"
	"  CloseEnoughDist = 1\n"
	"End\n"
	"Locomotor WheelingHordeLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 2000\n"
	"  TurnTimeDamaged = 2000\n"
	"  Acceleration = 500\n"
	"  Braking = 500\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = HORDE\n"
	"  MaxTurnWithoutReform = 45\n"
	"  TurnWhileMoving = Yes\n"
	"  WaitForFormation = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n";

const char kObjects[] =
	"Object Walker\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Block\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 40\n"
	"  GeometryMinorRadius = 40\n"
	"  GeometryHeight = 40\n"
	"End\n"
	"Object MemberOfHorde\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Horde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:MemberOfHorde Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20 Position:X:50 Y:40 Position:X:50 Y:-40\n"
	"    RankInfo = RankNumber:2 UnitType:MemberOfHorde Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20 Position:X:30 Y:40 Position:X:30 Y:-40\n"
	"    RankInfo = RankNumber:3 UnitType:MemberOfHorde Position:X:10 Y:0 Position:X:10 Y:20 Position:X:10 Y:-20 Position:X:10 Y:40 Position:X:10 Y:-40\n"
	"    InitialPayload = MemberOfHorde 15\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	"Object WheelingHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:MemberOfHorde Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20 Position:X:50 Y:40 Position:X:50 Y:-40\n"
	"    RankInfo = RankNumber:2 UnitType:MemberOfHorde Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20 Position:X:30 Y:40 Position:X:30 Y:-40\n"
	"    RankInfo = RankNumber:3 UnitType:MemberOfHorde Position:X:10 Y:0 Position:X:10 Y:20 Position:X:10 Y:-20 Position:X:10 Y:40 Position:X:10 Y:-40\n"
	"    InitialPayload = MemberOfHorde 15\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WheelingHordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

inline pathtest::SyntheticTerrain &noTerrain()
{
	static pathtest::SyntheticTerrain t(1, 1);
	return t;
}

struct MoveWorld : logictest::LogicWorld
{
	LocomotorStore store;
	LocomotorStore *savedStore;
	pathtest::SyntheticTerrain terrain;
	AIWorldConfig config;
	std::unique_ptr<AIWorld> ai;
	CommandList commands;
	GameLogicDispatch dispatcher;
	AICommands aiCommands;
	bool mapBuilt = false;

	explicit MoveWorld(int cellsW = 100, int cellsH = 100, const char *extraObjects = "")
		: terrain(cellsW, cellsH)
		, dispatcher(*logic)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		savedStore = TheLocomotorStore;
		TheLocomotorStore = &store;
		w.fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
		LogicModules::registerAll(w.modules);
		std::string err = w.load(kLocomotors, INI_LOAD_OVERWRITE, "loco.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(std::string(kObjects) + extraObjects, INI_LOAD_OVERWRITE, "objects.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		config.pathfind = pathtest::testConfig();
		config.movementPenaltyDamageState = 2;
		ai = std::make_unique<AIWorld>(*logic, config, w.fx.env.macros);
		ai->attach();
		aiCommands.registerHandlers(dispatcher);
		dispatcher.attach(commands);
	}
	~MoveWorld()
	{
		logic->reset(); // the objects leave the pathfinder through the world hooks first
		ai.reset();
		TheLocomotorStore = savedStore;
	}

	// builds the grid over the terrain (after the structures exist), one logic frame later every object has its first update
	void buildMap()
	{
		ai->newMap(terrain);
		mapBuilt = true;
	}

	Object *spawn(const std::string &name, float x, float y, float angle = 0.0f, Team *team = nullptr)
	{
		Object *o = make(name, team ? team : teamOf("Alice"));
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		return o;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic->runLogicFrame();
		}
	}
	AIUpdateInterface *aiOf(Object *o) { return o->getAIUpdateInterface(); }
	int playerIndex(const char *name) { return players.findPlayerWithName(name)->getPlayerIndex(); }
	// the lockstep command path: the message goes into the command list, the next logic frame executes it
	void send(const GameMessage &m) { commands.append(m); }
	void select(int player, const std::vector<Object *> &objs, bool createNew = true)
	{
		GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
		m.appendBooleanArgument(createNew);
		for (Object *o : objs)
		{
			m.appendObjectIDArgument(o->getID());
		}
		send(m);
	}
	void moveTo(int player, float x, float y, int type = MSG_DO_MOVETO)
	{
		GameMessage m(type, player);
		m.appendLocationArgument(Coord3D{ x, y, 0.0f });
		send(m);
	}
	HordeContain *hordeOf(Object *o) { return dynamic_cast<HordeContain *>(o->findModule("HordeContain")); }
	std::vector<Object *> membersOf(Object *horde)
	{
		const ContainModuleInterface::ContainedItemsList *l = horde->getContain()->getContainedItemsList();
		return std::vector<Object *>(l->begin(), l->end());
	}
};

inline float dist2d(const Coord3D &a, const Coord3D &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

} // namespace movetest
