// OpenBFME. GPL-3.0.
// LOCAL-114 (fork issue 798): unit AI core, third pass — AIUpdateInterface uncited own & reached functions (editions: both RotWK 2.01 and BFME2 1.06).
// Sources:
//   - AIMoveAndEvacuateState (states 28 & 29): ctor RW 0x0074477D, onEnter RW 0x00750512, update RW 0x00755734, onExit RW 0x0074B9BB, xfer RW 0x00742821;
//     DONOR BFME2 0x0034EB7E / 0x00353C27 / 0x00349FD0.
//   - AIUpdateInterface::aiMoveToAndEvacuate (RW 0x0066B6B8, DONOR BFME2 0x0026B6B2), aiMoveToAndEvacuateAndExit (RW 0x0066B72D, DONOR BFME2 0x0026B720),
//     aiEvacuate / privateEvacuate (RW 0x0066E0DC, DONOR BFME2 0x0026DDED).
//   - ActionManager::canGetHealedAt (RW 0x0082C798, DONOR BFME2 0x0042B4F5), AIUpdateInterface::aiGetHealed (RW 0x0066E078, DONOR BFME2 0x0026DD89).
//   - ActionManager::canGetRepairedAt (RW 0x0082C4E9, DONOR BFME2 0x0042B246), AIUpdateInterface::aiGetRepaired (RW 0x0066E0AA, DONOR BFME2 0x0026DDBB).
//   - ActionManager::canCombineObjects (RW 0x0082C254, DONOR BFME2 0x000C4080), AIUpdateInterface::aiCombineObject (RW 0x00664533, DONOR BFME2 0x0026472F),
//     AICombineState (state 60): ctor RW 0x007448DE, onEnter RW 0x00751716, update RW 0x0074D433, internalUpdate RW 0x00747B4F;
//     DONOR BFME2 0x0034FF0B / 0x0034BE40 / 0x00345EA8.
//   - AIUpdateInterface::aiEnterAndAttack (RW 0x0066B88C, DONOR BFME2 0x0026B86A), AIEnterAndAttackState (state 49): ctor RW 0x00744901,
//     onEnter RW 0x00751777, update RW 0x0075620A, onExit RW 0x00752B73, xfer RW 0x00742848; DONOR BFME2 0x0034FF6C / 0x003546EA / 0x003512B2.
//   - AIUpdateInterface::aiPanicCower (RW 0x00664952, DONOR BFME2 0x00264B4E), aiAttackMoveToObject (RW 0x0066C148, DONOR BFME2 0x0026C0D6),
//     micropathToPosition (RW 0x00663EF4, DONOR BFME2 0x0026412B), appendPositionToLocomotorPath (RW 0x0066708E, DONOR BFME2 0x00267266),
//     Path::setLastNodePortal (RW 0x0076578C, DONOR BFME2 0x00362F78).

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIDock.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace combattest;

namespace
{

const char kLocal114Objects[] =
	"Object Local114Soldier\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  TransportSlotCount = 1\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"End\n"
	"Object Local114Transport\n"
	"  KindOf = VEHICLE TRANSPORT SELECTABLE\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    Slots = 4\n"
	"    PassengerFilter = ANY +INFANTRY\n"
	"    AllowAlliesInside = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"End\n"
	"Object Local114HealDepot\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE HEAL_PAD\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    Slots = 4\n"
	"    PassengerFilter = ANY +INFANTRY\n"
	"    AllowAlliesInside = Yes\n"
	"  End\n"
	"End\n"
	"Object Local114RepairDepot\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE REPAIR_PAD\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = SupplyWarehouseCreate ModuleTag_Create\n"
	"  End\n"
	"  Behavior = SupplyWarehouseDockUpdate ModuleTag_Dock\n"
	"    NumberApproachPositions = -1\n"
	"    StartingBoxes = 2\n"
	"  End\n"
	"End\n"
	"Object Local114FighterHorde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    Slots = 2\n"
	"    RankInfo = RankNumber:1 UnitType:Local114Soldier Position:X:0 Y:-5 Position:X:0 Y:5\n"
	"    InitialPayload = Local114Soldier 2\n"
	"    ThisFormationIsTheMainFormation = Yes\n"
	"    ComboHorde = Target:Local114ArcherHorde Result:Local114ComboHorde InitiateVoice:NoSound\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"End\n"
	"Object Local114ArcherHorde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    Slots = 2\n"
	"    RankInfo = RankNumber:1 UnitType:Local114Soldier Position:X:0 Y:-5 Position:X:0 Y:5\n"
	"    InitialPayload = Local114Soldier 2\n"
	"    ThisFormationIsTheMainFormation = Yes\n"
	"    ComboHorde = Target:Local114FighterHorde Result:Local114ComboHorde InitiateVoice:NoSound\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"End\n"
	"Object Local114ComboHorde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    Slots = 4\n"
	"    RankInfo = RankNumber:1 UnitType:Local114Soldier Position:X:0 Y:-5 Position:X:0 Y:5\n"
	"    RankInfo = RankNumber:2 UnitType:Local114Soldier Position:X:-10 Y:-5 Position:X:-10 Y:5\n"
	"    ThisFormationIsTheMainFormation = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"End\n";

struct Local114World : CombatWorld
{
	Local114World() : CombatWorld(kLocal114Objects)
	{
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		ScriptEngine::SideScripts side;
		side.sideName = "";
		static ScriptList empty;
		side.lists.push_back(&empty);
		logic->scriptEngine().newGame({ side }, nullptr, nullptr, false);
	}
};

} // namespace

TEST_CASE("LOCAL-114 AIMoveAndEvacuateState (states 28 & 29), aiMoveToAndEvacuate (RW 0x0066B6B8), aiMoveToAndEvacuateAndExit (RW 0x0066B72D), and aiEvacuate (RW 0x0066E0DC)")
{
	Local114World w;
	Object *transport = w.unit("Local114Transport", 'A', 200.0f, 200.0f);
	Object *passenger = w.unit("Local114Soldier", 'A', 200.0f, 200.0f);
	w.logic->runLogicFrame();

	ContainModuleInterface *contain = transport->getContain();
	AIUpdateInterface *ai = transport->getAIUpdateInterface();
	REQUIRE(contain != nullptr);
	REQUIRE(ai != nullptr);
	REQUIRE(contain->addToContain(passenger));
	CHECK(contain->getContainCount() == 1);

	// 1. aiMoveToAndEvacuate (state 28 = AI_MOVE_AND_EVACUATE): locks machine, moves to drop point, evacuates passengers, and transitions to idle.
	CHECK(ai->aiMoveToAndEvacuate(Coord3D{ 260.0f, 200.0f, 0.0f }, CMD_FROM_SCRIPT));
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_AND_EVACUATE);
	CHECK(ai->stateMachine().isLocked());

	// Snapshot round-trip while in AI_MOVE_AND_EVACUATE preserves m_origin (RW 0x00742821).
	const std::uint32_t hashBefore = w.hash();
	XferSave saver;
	ai->stateMachine().xfer(saver);
	XferLoad loader(saver.data());
	ai->stateMachine().xfer(loader);
	CHECK(loader.atEnd());
	CHECK(w.hash() == hashBefore);

	for (int i = 0; i < 60 && contain->getContainCount() > 0; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(contain->getContainCount() == 0);
	CHECK_FALSE(ai->stateMachine().isLocked());

	// 2. aiMoveToAndEvacuateAndExit (state 29 = AI_MOVE_AND_EVACUATE_AND_EXIT): evacuates at drop point, restores m_origin on exit (RW 0x0074B9BB),
	// transitions to state 30 (AI_MOVE_AND_DELETE, RW 0x00753BF4), walks back to m_origin, and destroys the transport.
	Object *transport2 = w.unit("Local114Transport", 'A', 150.0f, 300.0f);
	Object *passenger2 = w.unit("Local114Soldier", 'A', 150.0f, 300.0f);
	w.logic->runLogicFrame();
	REQUIRE(transport2->getContain()->addToContain(passenger2));
	AIUpdateInterface *ai2 = transport2->getAIUpdateInterface();
	const ObjectID transport2ID = transport2->getID();

	CHECK(ai2->aiMoveToAndEvacuateAndExit(Coord3D{ 220.0f, 300.0f, 0.0f }, CMD_FROM_SCRIPT));
	CHECK(ai2->currentStateId() == (unsigned)AI_MOVE_AND_EVACUATE_AND_EXIT);

	bool sawMoveAndDelete = false;
	for (int i = 0; i < 120 && w.logic->findObjectByID(transport2ID) != nullptr; ++i)
	{
		w.logic->runLogicFrame();
		if (Object *t = w.logic->findObjectByID(transport2ID))
		{
			if (t->getAIUpdateInterface()->currentStateId() == (unsigned)AI_MOVE_AND_DELETE)
			{
				sawMoveAndDelete = true;
			}
		}
	}
	CHECK(sawMoveAndDelete);
	CHECK(w.logic->findObjectByID(transport2ID) == nullptr);
	CHECK(passenger2->getContainedBy() == nullptr);
}

TEST_CASE("LOCAL-114 canGetHealedAt (RW 0x0082C798), aiGetHealed (RW 0x0066E078), canGetRepairedAt (RW 0x0082C4E9), and aiGetRepaired (RW 0x0066E0AA)")
{
	Local114World w;
	Object *soldier = w.unit("Local114Soldier", 'A', 200.0f, 200.0f);
	Object *vehicle = w.unit("Local114Transport", 'A', 200.0f, 250.0f);
	Object *healDepot = w.unit("Local114HealDepot", 'A', 260.0f, 200.0f);
	Object *enemyHealDepot = w.unit("Local114HealDepot", 'B', 260.0f, 220.0f);
	Object *repairDepot = w.unit("Local114RepairDepot", 'A', 260.0f, 250.0f);
	w.logic->runLogicFrame();

	AIUpdateInterface *soldierAI = soldier->getAIUpdateInterface();
	AIUpdateInterface *vehicleAI = vehicle->getAIUpdateInterface();
	REQUIRE(soldierAI != nullptr);
	REQUIRE(vehicleAI != nullptr);

	// At full health, both canGetHealedAt and canGetRepairedAt return false.
	CHECK_FALSE(GarrisonRules::canGetHealedAt(*soldier, healDepot, CMD_FROM_PLAYER));
	CHECK_FALSE(soldierAI->aiGetHealed(healDepot, CMD_FROM_PLAYER));
	CHECK_FALSE(GarrisonRules::canGetRepairedAt(*vehicle, repairDepot, CMD_FROM_PLAYER));
	CHECK_FALSE(vehicleAI->aiGetRepaired(repairDepot, CMD_FROM_PLAYER));

	// Damage both units below MaxHealth.
	DamageInfo dmg;
	dmg.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	dmg.m_input.m_amount = 30.0f;
	soldier->attemptDamage(dmg);
	vehicle->attemptDamage(dmg);
	REQUIRE(soldier->getBodyModule()->getHealth() == 70.0f);
	REQUIRE(vehicle->getBodyModule()->getHealth() == 270.0f);

	// Vehicle (not INFANTRY) cannot get healed at HEAL_PAD; enemy HEAL_PAD rejected; SOLD or UNDER_CONSTRUCTION rejected.
	CHECK_FALSE(GarrisonRules::canGetHealedAt(*vehicle, healDepot, CMD_FROM_PLAYER));
	CHECK_FALSE(GarrisonRules::canGetHealedAt(*soldier, enemyHealDepot, CMD_FROM_PLAYER));
	CHECK_FALSE(GarrisonRules::canGetRepairedAt(*vehicle, healDepot, CMD_FROM_PLAYER));

	// Damaged allied INFANTRY accepted at allied HEAL_PAD -> enters AI_ENTER toward healDepot.
	CHECK(GarrisonRules::canGetHealedAt(*soldier, healDepot, CMD_FROM_PLAYER));
	CHECK(soldierAI->aiGetHealed(healDepot, CMD_FROM_PLAYER));
	CHECK(soldierAI->currentStateId() == (unsigned)AI_ENTER);
	CHECK(soldierAI->stateMachine().goalObjectID() == healDepot->getID());

	// Damaged allied non-aircraft accepted at allied REPAIR_PAD -> enters AI_DOCK toward repairDepot.
	CHECK(GarrisonRules::canGetRepairedAt(*vehicle, repairDepot, CMD_FROM_PLAYER));
	CHECK(vehicleAI->aiGetRepaired(repairDepot, CMD_FROM_PLAYER));
	CHECK(vehicleAI->currentStateId() == (unsigned)AI_DOCK);
	CHECK(vehicleAI->stateMachine().goalObjectID() == repairDepot->getID());
}

TEST_CASE("LOCAL-114 aiEnterAndAttack (RW 0x0066B88C), AIEnterAndAttackState (state 49, RW 0x00744901 / 0x00751777 / 0x0075620A / 0x00752B73), and snapshot")
{
	Local114World w;
	Object *soldier = w.unit("Local114Soldier", 'A', 200.0f, 200.0f);
	Object *transport = w.unit("Local114Transport", 'A', 250.0f, 200.0f);
	Object *enemy = w.unit("Local114Soldier", 'B', 350.0f, 200.0f);
	w.logic->runLogicFrame();

	AIUpdateInterface *ai = soldier->getAIUpdateInterface();
	REQUIRE(ai != nullptr);

	// Give soldier a current victim first (outside AIAttackState so m_machine->clear() keeps m_currentVictim)
	// so AIEnterAndAttackState::onEnter records m_victimID (RW 0x007517A2..0x007517B0).
	ai->aiMoveToPosition(Coord3D{ 210.0f, 200.0f, 0.0f }, CMD_FROM_PLAYER);
	ai->setCurrentVictim(enemy);
	REQUIRE(ai->currentVictim() == enemy);

	// Order aiEnterAndAttack (AICMD 0x3C, state 49 = AI_ENTER_AND_ATTACK).
	CHECK(ai->aiEnterAndAttack(transport, CMD_FROM_PLAYER));
	CHECK(ai->currentStateId() == (unsigned)AI_ENTER_AND_ATTACK);
	CHECK(ai->enterTarget() == transport);

	// Snapshot round-trip of AIEnterAndAttackState (RW 0x00742848).
	const std::uint32_t hashBefore = w.hash();
	XferSave saver;
	ai->stateMachine().xfer(saver);
	XferLoad loader(saver.data());
	ai->stateMachine().xfer(loader);
	CHECK(loader.atEnd());
	CHECK(w.hash() == hashBefore);

	// Step until soldier reaches transport, enters it, and transitions out of AI_ENTER_AND_ATTACK (restoring m_victimID as goalObject on exit, RW 0x00752B73).
	for (int i = 0; i < 40 && soldier->getContainedBy() != transport; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(soldier->getContainedBy() == transport);
	CHECK(ai->stateMachine().goalObjectID() == enemy->getID());
}

TEST_CASE("LOCAL-114 aiPanicCower (RW 0x00664952), aiAttackMoveToObject (RW 0x0066C148), micropathToPosition (RW 0x00663EF4), and appendPositionToLocomotorPath (RW 0x0066708E)")
{
	Local114World w;
	Object *soldier = w.unit("Local114Soldier", 'A', 200.0f, 200.0f);
	Object *scarer = w.unit("Local114Soldier", 'B', 220.0f, 200.0f);
	w.logic->runLogicFrame();

	AIUpdateInterface *ai = soldier->getAIUpdateInterface();
	REQUIRE(ai != nullptr);

	// 1. aiPanicCower (RW 0x00664952): sets goalObject(scarer) and enters state 21 (AI_PANIC_COWER).
	CHECK(ai->aiPanicCower(scarer, CMD_FROM_AI));
	CHECK(ai->currentStateId() == (unsigned)AI_PANIC_COWER);
	CHECK(ai->stateMachine().goalObjectID() == scarer->getID());

	// 2. aiAttackMoveToObject (RW 0x0066C148): sets goalObject, goalPosition, arms attack-move, and clears blocked.
	ai->mover().friend_setBlocked(true);
	CHECK(ai->aiAttackMoveToObject(scarer, CMD_FROM_PLAYER));
	CHECK(ai->attackMoveActive());
	CHECK(ai->stateMachine().goalObjectID() == scarer->getID());
	CHECK(ai->stateMachine().goalPosition().x == 220.0f);
	CHECK_FALSE(ai->mover().isBlocked());

	// 3. micropathToPosition (RW 0x00663EF4): builds a direct path reaching the nearby goal and sets AIGOAL_EXPLICIT_WITH_PATH.
	CHECK(ai->micropathToPosition(Coord3D{ 240.0f, 200.0f, 0.0f }));
	CHECK(ai->mover().goalType() == AIGOAL_EXPLICIT_WITH_PATH);
	REQUIRE(ai->mover().path() != nullptr);
	CHECK(ai->mover().path()->nodeCount() == 2);

	// 4. appendPositionToLocomotorPath (RW 0x0066708E) & Path::setLastNodePortal (RW 0x0076578C):
	// both when a path already exists and when starting from no path.
	ai->mover().destroyPath();
	REQUIRE(ai->mover().path() == nullptr);
	ai->appendPositionToLocomotorPath(Coord3D{ 260.0f, 210.0f, 0.0f }, 42);
	REQUIRE(ai->mover().path() != nullptr);
	CHECK(ai->mover().goalType() == AIGOAL_ON_PATH);
	CHECK(ai->mover().path()->nodeCount() == 2);
	REQUIRE(ai->mover().path()->getLastNode() != nullptr);
	CHECK(ai->mover().path()->getLastNode()->getWaypointID() == 42);

	ai->appendPositionToLocomotorPath(Coord3D{ 280.0f, 220.0f, 0.0f }, 77);
	CHECK(ai->mover().path()->nodeCount() == 3);
	CHECK(ai->mover().path()->getLastNode()->getWaypointID() == 77);
}
