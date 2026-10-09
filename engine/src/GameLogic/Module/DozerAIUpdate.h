// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (DozerAIUpdate is the model; RotWK changed it).
//
// DozerAIUpdate (lane BUILD-1, machines lane BUILD-3): the AI module of the builders (the Porters of the eight factions). Registered as an AI class (RW 0x64CA56, data
// RW 0x64CA91, mask 17), data = the AI fields + RepairHealthPercentPerSecond (parsePercentToReal +0x64), BoredTime (parseDurationReal +0x68), BoredRange (parseReal +0x6C)
// (RW table 0xC61EB0).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat): BuildAssistant slot 0x38 (RW 0x797796) hands a DOZER builder's order to its AI's vslot 0x1F8 (construct(tmpl, pos, angle, owner,
// isRotated)), after the builder's AI has been told to idle (ZH buildObjectNow); the dispatcher RW 0x77B011 (MSG_DOZER_CONSTRUCT 1050) requires KindOf DOZER (RW 0x77B067
// bit 0x4000 of the template's word at 0x108), a controlling player whose RW + 0x770 is 0, and Player::canBuild (RW 0x6AAA2E); RW 0x77B1A0 (MSG_DOZER_CANCEL_CONSTRUCT 1051).
//
// THE MACHINES (lane BUILD-3; RW vtables and bodies below; DONOR ZH DozerAIUpdate.cpp has the same shape, ZH StateMachine.cpp the machine semantics):
//   * the dozer interface (RW AI + 0x4A8, vtable RW 0xC622D8): three tasks BUILD 0 / REPAIR 1 / FORTIFY 2, each a target id and the frame it was ordered (+ 4 / + 8),
//     three dock points START / ACTION / END per task (+ 0x2C: valid byte, then the position, 16 bytes each), the current task (slot 0x28 / 0x24), the build sub task
//     (slot 0x48 / 0x4C), the structure a construct order made and that is not placed yet (+ 0xC0, slot 0x74);
//   * newTask(task, target) RW 0x88CCFC: a pending task of that kind is cancelled; the dock position is findGoodBuildOrRepairPosition (RW 0x88CC08 -> 0x88C2FE: the
//     BRIDGE branch picks the nearest bridge tower and is not ported, S-1282); START = ACTION = that position, END = it + 50 (RW 0xBD88C4) along the normalised
//     (position - target) in x / y (Coord3D::normalize); the target id and the frame are kept and the primary machine is reset;
//   * findGoodBuildOrRepairPosition RW 0x88C2FE: v = the dozer's position - the target's (3D), normalised by the fast inverse square root RW 0x441C56 (PC24 products)
//     unless its length^2 is 0; start = target position + v * (the target's bounding circle radius * 0.5) (SSE); the AI's labelled contact point nearest to `start`
//     (RW 0x667C76, preferred) is the position; else TerrainLogic::FindPositionAround(start, { minRadius 0, maxRadius 100, RANDOM_START_ANGLE, maxZDelta 1e10 (10
//     for a dozer whose locomotor has no AIR surface, RW 0x68BEED), ignoreObject the target for an AIR dozer, sourceToPathToDest the dozer }) or, failing that,
//     the target's position;
//   * the primary machine (RW 0x88C06B, made on the first update / order RW 0x88C1C1): IDLE 0 (RW 0x88BAA7, vtable RW 0xC61D50) with the transitions RW 0xC61F70
//     "the AI is idle and the most recently ordered pending task (slot 0x14, RW 0x88BE47) is BUILD / REPAIR / FORTIFY" -> 1 / 2 / 3; the task states 1 .. 3 (RW
//     0x88BB4B, vtable RW 0xC61DB0: enter sets the current task and resets the action machine, update runs it, exit clears the current task and the building sound)
//     go back to IDLE on success and failure; state 4 (RW 0x88BC33) is not entered by anything ported. IDLE's update (RW 0x88E582) marks the dozer an idle worker
//     (TheInGameUI vslot 0x1A4 / 0x1A8) while its AI is idle and it is alive, and restarts its boredom clock while the AI is busy;
//   * the action machine of a task state (RW 0x88B9BE): PICK_ACTION_POS 0 (RW 0x88D7D2: the task target, else the unplaced structure, else cancel the task and fail;
//     the START dock point, else FindPositionAround(target, { min = max = the target's bounding sphere radius, the angle target -> dozer }) or the target's position;
//     the goal object / position set, the target ignored as an obstacle (RW 0x66831A), aiMoveToPosition(CMD_FROM_AI); success) -> MOVE_TO_ACTION_POS 1 (RW 0x88C5C7:
//     arrived when the squared edge distance (RW 0x6CA525: (sqrt(dx^2 + dy^2) - the dozer's bounding circle radius), 0 when negative, squared) to the goal position
//     is <= max(70 (RW 0xC61BC4), the dozer's bounding sphere radius + 15 (RW 0xBDC6CC))^2; a build then places the unplaced structure (slot 0x68, RW 0x88D44F) and
//     turns AWAITING_CONSTRUCTION into PARTIALLY_CONSTRUCTED + ACTIVELY_BEING_CONSTRUCTED on the goal; an idle AI that has not arrived fails back to PICK) ->
//     DO_ACTION 2 (RW 0x88D993: sub task 0 walks to the ACTION dock point, 1 waits for the AI to be idle and faces the goal (AI command 0x26), 2 waits again and
//     starts the building sound, 3 works: see workOnBuild / workOnRepair); DO_ACTION's success / failure leave the machine.
//   * the placement RW 0x88D44F (U4): the site must still be legal (BuildAssistant::isLocationLegalToBuild flags 0x15), else the unplaced structure is removed (slot
//     0x6C RW 0x88CFFE: the paid price back, truncated, the money spent score taken back, the object destroyed); a legal site: the body's health is changed by
//     1.0 - health (body vslots 0x10 / 0x84: the structure rises from 1 hit point).
//   * DozerAIUpdate::update (RW 0x88CAF5): the base AI update, then a current REPAIR task whose target the ActionManager no longer lets it repair is cancelled
//     (RW 0x82D7F2, S-1282), then the primary machine. The dozer never sleeps (returns 1).
// TARGET (lane BUILD-2, DozerActionDoActionState::update RW 0x88D993, sub task 3): a build task adds 100 / calcTimeToBuild(template, the dozer's owner, the dozer)
// to the construction percent each frame (SSE, RW 0x88DE72..0x88DE99) and changes the body's health by MaxHealth / that (x87, RW 0x88DEA8..0x88DEB5), sets itself as
// producer / builder when none is set, and completes the structure when the percent reaches 100 (RW 0x88DEE0 ff); a repair task heals the body by RepairHealthPercentPerSecond
// * MaxHealth / 5 through attemptHealingFromSoleBenefactor (duration 2, RW 0x88DC46..0x88DC6A) and ends when the health is full or the heal is refused. Both completions
// end through the tail RW 0x88DBB3: internalTaskComplete (slot 0x38), the goal cleared, the AI player told the dozer is free (slot 0x7C -> RW 0x88BF4E), success.
//   * a porter (a dozer without a supply interface, RW 0x88B897) that reaches a structure it is to place goes into it (RW 0x88C51B: UNSELECTABLE, UNATTACKABLE,
//     DO_NOT_PICK_ME and WONT_RIDE_WITH_YOU set, out of the world RW 0x68C18F) and comes out (RW 0x88D6AE) when the build ends (walking BuilderMoveFromNewStructure
//     Distance away from it, a PLAYER-sourced move) or the task is cancelled: back in the world RW 0x68E31F, the statuses cleared.
// INFERENCE / NOT PORTED (stops S-304, S-1282): the structure is made at the order with every BUILD-1 effect (RW makes it PHANTOM and places it on arrival); the
// client side of the hiding (the selection removed, the fade in over BuilderFadeInTime); the face command 0x26 (the AI face state 36) runs as nothing; the bridge tower branch of the dock position; the bored wander (RW 0x88E17F, an AI player's dozer); the TheInGameUI idle worker
// list is the marked flag (isIdleWorker); the primary machine reset after a PLAYER command runs before the command (RW: after it); the dozer's "a task ended"
// notification to the AI player is also sent when a task is cancelled or fails (S-890).
//
// Simulation maths goes through SimMath.

#pragma once

#include "GameLogic/Module/AIUpdate.h"

class Object;
class Player;
class ThingTemplate;

class DozerAIUpdate : public AIUpdateInterface
{
public:
	// ZH DozerTask / DozerDockPoint / DozerBuildSubTask (RotWK adds the facing sub task 2)
	enum DozerTask
	{
		DOZER_TASK_INVALID = -1,
		DOZER_TASK_BUILD = 0,
		DOZER_TASK_REPAIR = 1,
		DOZER_TASK_FORTIFY = 2,
		DOZER_NUM_TASKS = 3
	};
	enum DozerDockPoint
	{
		DOZER_DOCK_POINT_START = 0,
		DOZER_DOCK_POINT_ACTION = 1,
		DOZER_DOCK_POINT_END = 2,
		DOZER_NUM_DOCK_POINTS = 3
	};
	enum DozerBuildSubTask
	{
		DOZER_SELECT_BUILD_DOCK_LOCATION = 0,
		DOZER_MOVING_TO_BUILD_DOCK_LOCATION = 1,
		DOZER_FACING_BUILD = 2,
		DOZER_DO_BUILD_AT_DOCK = 3
	};
	// the primary machine's states (RW 0x88C06B) and the action machine's (RW 0x88B9BE)
	enum
	{
		PRIMARY_IDLE = 0,
		PRIMARY_BUILD = 1,
		PRIMARY_REPAIR = 2,
		PRIMARY_FORTIFY = 3,
		ACTION_PICK_ACTION_POS = 0,
		ACTION_MOVE_TO_ACTION_POS = 1,
		ACTION_DO_ACTION = 2,
		ACTION_NONE = -1
	};

	DozerAIUpdate(Thing *thing, const ModuleData *data);
	~DozerAIUpdate() override; // RW 0x88D1AC: finishBuildingSound (RW 0x88BFA9) first
	// RW 0x88E44F (DozerAIUpdate::aiDoCommand): a PLAYER command other than repair (0x13), resume construction (0x14) and move away (0x34) cancels the current task
	// before it runs (dozer interface slot 0x34) and resets the primary machine
	void commandAccepted(CommandSourceType source, int command, Object *target) override;
	static void registerClass(ModuleFactory &modules);
	// lane BUILD-2: WorkerAIUpdate (create RW 0x64EF9C, data RW 0x654238, mask 0x11; ctor RW 0x8ADAA0, size 0x594): ZH's WorkerAIUpdate = a dozer plus a supply truck. Field
	// table RW 0xC058C8: MaxBoxes, RepairHealthPercentPerSecond (+0x68), BoredTime, BoredRange, the supply / harvest fields. The dozer half runs as this class (the same named
	// fields); the supply-truck half (MaxBoxes, SupplyCenterActionDelay, Harvest*) is not ported (stop S-655). The workers GettingBuiltBehavior spawns (WorkerName) use it
	static void registerWorkerClass(ModuleFactory &modules);
	bool isWorker() const { return m_isWorker; }

	// RW 0x88C217 -> 0x88C2A5: the legality gate of the construct body, flags 0x49F (ZH CLEAR_PATH | TERRAIN_RESTRICTIONS | NO_OBJECT_OVERLAP | USE_QUICK_PATHFIND | SHROUD_REVEALED = 0x1F
	// plus the RotWK bits 0x80 and 0x400 whose meaning is not read, S-301): a nonzero result rejects the order. The dispatcher asks it before it changes the builder's state
	static constexpr unsigned kConstructLegalFlags = 0x49F;
	bool siteIsLegal(const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner);
	// RW vslot 0x1F8 (RW 0x88C217): makes the foundation of `what` at `pos` and starts the build task; null when refused (illegal site, no money)
	Object *construct(const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner);
	// MSG_RESUME_CONSTRUCTION: RW 0x8ACEA9 (vslot 0xB4): the ActionManager's test, then newTask(BUILD, structure); the command path first idles a dozer without a task
	bool resumeConstruction(Object &structure);
	// lane BUILD-2: RW 0x88BD8D (vslot 0xB0, MSG_DO_REPAIR, a worker spawned to repair): the ActionManager's test and a free sole benefactor, then newTask(REPAIR)
	bool repair(Object &structure);
	bool repairing() const { return m_currentTask == DOZER_TASK_REPAIR; }
	// the frames of work (sub task 3) this dozer put into its current / last task
	UnsignedInt taskWorkFrames() const { return m_taskWorkFrames; }
	// MSG_DOZER_CANCEL_CONSTRUCT / a new order: cancels the current task (RW slot 0x34), the structure stays
	void cancelTask();
	// the dozer is working on `structure` right now (sub task 3 of its current task)
	bool isBuilding(const Object &structure) const;
	// the target of the current task, else of the most recently ordered pending one (INVALID_ID: none)
	ObjectID taskTarget() const;
	bool working() const { return m_working; }
	float repairHealthPercentPerSecond() const { return m_repairPercentPerSecond; }

	// ---- the dozer interface (RW vtable 0xC622D8) ----
	bool isTaskPending(int task) const { return task >= 0 && task < DOZER_NUM_TASKS && m_task[task].target != INVALID_ID; } // slot 0x18
	bool isAnyTaskPending() const;                                                                                         // slot 0x20
	int getMostRecentCommand() const;                                                                                       // slot 0x14
	int getCurrentTask() const { return m_currentTask; }                                                                    // slot 0x24
	ObjectID getTaskTarget(int task) const { return isTaskPending(task) ? m_task[task].target : INVALID_ID; }              // slot 0x1C
	const Coord3D *getDockPoint(int task, int point) const;                                                                 // slot 0x44
	int getBuildSubTask() const { return m_buildSubTask; }                                                                  // slot 0x4C
	ObjectID unplacedStructure() const { return m_unplacedStructure; }                                                      // + 0xC0
	int primaryState() const { return m_primaryState; }
	// the action machine state of the current task (ACTION_NONE without one)
	int actionState() const { return m_currentTask >= 0 ? m_action[m_currentTask].state : (int)ACTION_NONE; }
	// lane BUILD-3 (U3): the primary IDLE state marked this dozer an idle worker (RW 0x88E5F3: TheInGameUI addIdleWorker); a dozer with a task is never one
	bool isIdleWorker() const { return m_markedIdle; }
	// lane BUILD-3: the dock position newTask computed (RW 0x88C2FE), for the tests
	static bool findGoodBuildOrRepairPosition(const Object &dozer, const Object &target, Coord3D &out);

	void crc(StateHasher &hasher) const override;

	// RW 0x88CAF5: the base update, the repair validity check, the primary machine; never sleeps
	UpdateSleepTime update() override;

protected:
	const char *machineName() const override { return "DozerAIUpdateMachine"; }

private:
	struct TaskInfo
	{
		ObjectID target = INVALID_ID; ///< + 4
		UnsignedInt orderFrame = 0;   ///< + 8
	};
	struct DockPoint
	{
		bool valid = false;
		Coord3D location{ 0.0f, 0.0f, 0.0f };
	};

	void newTask(int task, Object &target);         // slot 0x30, RW 0x88CCFC
	void cancelTaskInternal(int task);              // slot 0x34, RW 0x88E3CE
	void internalTaskComplete(int task);            // slot 0x38, RW 0x88BE18
	void internalCancelTask(int task);              // slot 0x3C, RW 0x88E40A
	void removeUnplacedStructure();                 // slot 0x6C, RW 0x88CFFE
	bool placeUnplacedStructure();                  // slot 0x68, RW 0x88D44F: false when the site is no longer legal
	void notifyAIPlayerDozerFree();                 // slot 0x7C, RW 0x88BF4E
	void hideInStructure();                         // RW 0x88C51B: a porter (not a worker) goes into the structure it starts
	void showFromStructure(const Object *built);    // RW 0x88D6AE: it comes out (and walks away from `built` when given)

	// the primary machine (ZH StateMachine semantics)
	int primarySetState(int id);
	int primaryCheckTransitions(int status);
	void primaryReset();
	int primaryEnter(int id);
	void primaryExit(int id);
	int primaryUpdateIdle();
	// the action machine of the current task state
	int actionSetState(int t, int id);
	int actionCheckTransitions(int t, int status);
	void actionReset(int t);
	int actionUpdate(int t);
	int updatePickActionPos(int t);
	int updateMoveToActionPos(int t);
	int updateDoAction(int t);

	int workOnBuild(int t, Object &structure);
	int workOnRepair(int t, Object &structure);
	int taskSucceeded(int t); // the tail RW 0x88DBB3
	void setWorking(bool on);

	TaskInfo m_task[DOZER_NUM_TASKS];
	DockPoint m_dockPoint[DOZER_NUM_TASKS][DOZER_NUM_DOCK_POINTS];
	int m_currentTask = DOZER_TASK_INVALID;
	int m_buildSubTask = DOZER_SELECT_BUILD_DOCK_LOCATION;
	ObjectID m_unplacedStructure = INVALID_ID;
	// primary machine
	int m_primaryState = -1;          ///< -1 until the machine is made (RW 0x88C1C1)
	UnsignedInt m_idleTimestamp = 0;  ///< IDLE + 0x20
	int m_idlePlayer = -1;            ///< IDLE + 0x24
	bool m_markedIdle = false;        ///< IDLE + 0x28
	int m_transitionDepth = 0;        ///< ZH State::friend_checkForTransitions' recursion guard (20)
	// action machine (of the current task state)
	// the action machine of each task state (RW 0x88BB4B makes one per state, RW 0x88B9BE): its state, goal object and position, DO_ACTION's enter frame
	struct ActionMachine
	{
		int state = ACTION_NONE;
		ObjectID goalObject = INVALID_ID;
		Coord3D goalPosition{ 0.0f, 0.0f, 0.0f };
		UnsignedInt doActionEnterFrame = 0;
	};
	ActionMachine m_action[DOZER_NUM_TASKS];

	UnsignedInt m_taskWorkFrames = 0;
	bool m_working = false;
	// lane AUDIO-3: the building sound this dozer started (RW startBuildingSound 0x88C4A3 keeps its handle at + 0x24 of the dozer interface); the handle lives on
	// the audio side (AudioApi::postHeldSound HELD_BUILDING_LOOP keyed by this dozer's id). Audio bookkeeping only: no logic reads it, not hashed
	ObjectID m_buildingSoundHolder = INVALID_ID; ///< this dozer's id while its building sound is posted
	void startBuildingSound(Object &structure, const char *soundField);
	void finishBuildingSound();
	bool m_isWorker = false;               // a WorkerAIUpdate (RW 0x88B897: the AI has a supply interface, vslot 0x190): it does not hide while it builds
	float m_repairPercentPerSecond = 0.0f; // +0x64
	float m_boredTime = 0.0f;              // +0x68 (frames * 1.0: parseDurationReal)
	float m_boredRange = 0.0f;             // +0x6C
};
