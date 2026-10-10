// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AIUpdateInterface (lane MOVE-1): the AI module of a live object. It owns the AI state machine (GameLogic/AI/AIStateMachine.h), the
// locomotors of the object's LocomotorSet, PATH-1's AIMover and the LocomotorHost / AIMoveHost the movement code needs, so a unit orders,
// paths, moves and stops exactly as the merged pieces do.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; B1 = Open-BFME-1 AIUpdate.cpp, whose matched bodies the addresses below agree with):
//   * the constructor chooses LOCOMOTORSET_NORMAL and wakes the module (B1 AIUpdateInterfaceCtorThunk, ZH AIUpdate.cpp ctor); the state
//     machine is made in onObjectCreated (B1 AIUpdate.cpp:2006) and its default state (idle) entered there;
//   * update (RW 0x6695EF, B1 AIUpdate.cpp:2547): the state machine, the movement-complete bookkeeping, the due path-request timer, the
//     dead check and doLocomotor (RW 0x669932); the result is the smallest sleep any part asks for;
//   * a command (aiDoCommand, B1 :4248) reaches privateMoveToPosition (B1 :4511: a CMD_FROM_AI move of a busy unit runs as a temporary
//     state for 20 seconds, everything else clears the machine and sets AI_MOVE_TO), privateIdle (:4722), privateFollowPath (:5039);
//   * the model condition MOVING is what AIMoveToState sets and clears (RW 0x74DADA / 0x748D8A), the Locomotor sets WALKING / TURN_* and
//     friends itself (HORDE-1);
//   * isIdle RW 0x6643FC, isMoving RW 0x664485 (AIMover::isMoving), isContained RW 0x6939DF, isDoingGroundMovement and
//     isAircraftThatAdjustsDestination as B1 AIUpdate.cpp:3978-4040.
// DONOR: ZH AIUpdate.cpp (AICommandInterface, the private commands).
//
// WHAT IS INFERENCE / NOT PORTED (stop S-222, docs/STOPS.md): see AIUpdate.cpp (the command set beyond move / idle / follow path, the
// attack machine, turrets, moods, stealth, special layers); `unportedCommands()` counts every command asked and not executed.
//
// Determinism: no pointer values, no unordered containers; every float is float32 and the file is built with -ffp-contract=off.

#pragma once

#include "GameLogic/AI/AIAttack.h"
#include <array>
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/UpdateModule.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class AIWorld;
class ModuleFactory;
class TurretAI;
struct TurretAIData;
class ObjectPathfindAdapter;
struct ObjectMovementInfo;

// lane PHYS-1 (AIUpdateAllies.cpp): AI_MOVE_OUT_OF_THE_WAY (26), the move-away path of RW 0x66DA5F followed to its end
std::unique_ptr<class AIState> makeMoveOutOfTheWayState(class AIStateMachine &machine);

class AIUpdateInterface : public UpdateModule, public AIMoveHost
{
public:
	// lane SMOOTH-1 (client snapshot only): the movers' pending position (RW obj+0x198) when it was set during logic frame `frame`
	bool pendingPositionOfFrame(unsigned frame, Coord3D &out) const
	{
		if (!m_pendingValid || m_pendingFrame != frame)
		{
			return false;
		}
		out = m_pendingPosition;
		return true;
	}
	// lane SMOOTH-2 (S-812 resolved): RW 0x62618F, the end of recordTransform (RW 0x6260E1, logic phase 2), clears +0x1A6, the pending position's valid flag
	void clearPendingPosition() { m_pendingValid = false; }
	// lane COMBAT-3: PhysicsBehavior's flight step (RW 0x79350E) writes obj + 0x198 (the next curve point) and sets + 0x1A6, as the movers do
	void setPendingPosition(const Coord3D &p, unsigned frame)
	{
		m_pendingPosition = p;
		m_pendingValid = true;
		m_pendingFrame = frame;
	}
	AIUpdateInterface(Thing *thing, const ModuleData *data);
	~AIUpdateInterface() override;

	// binds the runtime class to every AI module class of the binary's registry (AIUpdateInterface, HordeAIUpdate, ...); classes whose own
	// behaviour is not ported run as the base class and report it (S-222)
	static void registerClasses(ModuleFactory &modules);

	// ---- BehaviorModule / UpdateModule ----
	AIUpdateInterface *getAIUpdateInterface() override { return this; }
	void onObjectCreated() override;
	void onDelete() override;
	void crc(StateHasher &hasher) const override;
	UpdateSleepTime update() override;
	// lane IDLE-1 r2: every AI update class's vslot 0x30 is RW 0x851E97 (`xor eax, eax; ret`): the scheduler files it in updates[0] (phases 3 / 4), so the
	// AI (and the horde member update RW 0x66C748) runs before the horde contains of updates[1] (tools/rw_object_model/update_phases.py)
	SleepyUpdatePhase getUpdatePhase() const override { return PHASE_INITIAL; }

	// ---- the command interface (ZH AICommandInterface) ----
	void aiMoveToPosition(const Coord3D &pos, CommandSourceType source);
	void aiMoveToObject(Object *obj, CommandSourceType source);
	// RW 0x770C2C (MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO): a move that ends facing `angle`
	void aiMoveToPositionAndOrientate(const Coord3D &pos, float angle, CommandSourceType source);
	void aiIdle(CommandSourceType source);
	void aiFollowPath(const std::vector<Coord3D> &path, Object *ignoreObject, CommandSourceType source, bool exitProduction);
	void aiFollowPathAppend(const Coord3D &pos, CommandSourceType source);
	void aiBusy(CommandSourceType source);
	// ---- lane COMBAT-1: attack commands, the victim, the mood, death ----
	// B1 aiAttackObject (AIUpdate.cpp privateAttackObject): clears the machine and enters AI_ATTACK_OBJECT with the victim as the goal; false when this object cannot attack it
	bool aiAttackObject(Object *victim, CommandSourceType source);
	bool aiForceAttackObject(Object *victim, CommandSourceType source);
	// ZH AIUpdateInterface::getCurrentVictim / setCurrentVictim / notifyVictimIsDead
	Object *currentVictim() const;
	ObjectID currentVictimId() const { return m_currentVictim; }
	void setCurrentVictim(Object *victim);
	void notifyVictimIsDead() { m_currentVictim = INVALID_ID; }
	// the machine is in an attack state (AI_ATTACK_OBJECT / AI_FORCE_ATTACK_OBJECT)
	// the module is being destroyed: the machine's exits run on an object whose drawable the world hooks already removed (they must not touch its model condition)
	bool isDestroying() const { return m_destroying; }
	bool isAttacking() const;
	// ZH AIAttackMoveToState (stop S-328): the unit marches to `goal` and attacks what it meets, then resumes; any other order ends it
	// lane MODULES-3 r2: the attack-move order passes aiDoCommand's gate (RW 0x667174) before anything changes: a refused order arms nothing (false)
	bool armAttackMove(const Coord3D &goal, CommandSourceType source = CMD_FROM_PLAYER);
	bool attackMoveActive() const { return m_attackMoveActive; }
	Object *attackMoveTarget();
	// the idle state's resume: false when the march is over (arrived or cancelled)
	bool resumeAttackMove();
	// the march's continuation (a player's move): false, nothing changed, when the gate refuses it (a locked temporary state, PreventPlayerCommands)
	bool aiMoveToPositionFromAttackMove(const Coord3D &goal);
	// the next member of the horde this unit was ordered against when its victim died; null when the horde is gone
	Object *replacementVictim();
	ObjectID targetHordeId() const { return m_targetHorde; }
	// RW 0x698F06 -> the AI part: the unit is dead (its machine goes to AI_DEAD, its movers stop)
	void onDie();
	bool isAiInDeadState() const { return m_mover->isAiDead(); }
	// the AI module's fields the mood scan reads (the module data is raw: S-326)
	struct CombatSettings
	{
		unsigned moodAttackCheckRate = 0;  ///< frames (INI MoodAttackCheckRate, ms; 0 = none)
		unsigned autoAcquire = 0;          ///< ZH AutoAcquireEnemiesWhenIdle flags: 1 Yes, 2 STEALTHED, 4 No, 8 NOTWHILEATTACKING, 16 ATTACK_BUILDINGS
		float stopChaseDistance = 0.0f;
		bool standGround = false;
		bool canAttackWhileContained = false; ///< lane IDLE-1: CanAttackWhileContained (RW field table 0xC0F530, + 0x25, parseBool RW 0x42E558)
		std::string attackPriority;
		std::vector<std::string> specialContactPoints; ///< lane BUILD-3: SpecialContactPoints (RW field table 0xC0F640, + 0x58, RW 0x42E59E: every token appended)
	};
	const CombatSettings &combatSettings() const { return m_combat; }
	// lane BUILD-3, RW 0x667C76 AIUpdateInterface::findNearestLabeledContactPointOnTarget(target, out, callerPos, preferred): the SpecialContactPoints labels in order;
	// the first for which the target has a contact point (Object::getWorldspaceBestContactPoint RW 0x690BD2, mode 0) gives `out`; false when none has one
	bool findNearestLabeledContactPointOnTarget(const Object &target, Coord3D &out, const Coord3D &callerPos, bool preferred) const;
	// ZH getNextMoodTarget: the nearest attackable enemy within the vision range, null when this unit does not look for targets (no weapon, no AutoAcquire flag, a horde member)
	Object *nextMoodTarget();
	// the idle scan: due every MoodAttackCheckRate frames; returns the frames until the next check (0 when this unit never scans)
	unsigned moodCheckInterval() const;
	// ---- lane SCRIPT-3: the attitude (RW AI + 0x218, 0 at creation, RW 0x66EF64): -3 (RotWK's mood 0x2000), -2 sleep, -1 passive, 0 normal, 1 alert,
	// 2 aggressive (the scripts' AI_MOOD) ----
	// RW 0x66E12A: stored; -3 also drops a current victim (the machine's goal object cleared, vslot 0x38(0), and aiIdle(CMD_FROM_AI), RW 0x5E821A)
	// and clears the victim (RW 0x6682B1(0))
	void setAttitude(int attitude);
	// lane SCRIPT-3: AI command 0x12 (RW 0x6AF150): aiDoCommand's gate, then privateHunt RW 0x6644E6 (AIHunt.cpp); false when nothing changed
	bool aiHunt(CommandSourceType source);
	// lane SCRIPT-3: AI commands 6 / 7 / 0x32 / 0x33 (RW 0x770FA8 / 0x771072 / 0x77100D / 0x7710D7): the waypoint path states from `waypointId`
	// (AIWaypointPath.cpp); false when nothing changed
	bool aiFollowWaypointPath(int waypointId, CommandSourceType source, bool asTeam, bool exact);
	// RW 0x664CD6: a HORDE_MEMBER (status 0x26) reads its horde's AI (RW 0x693A1A(0), the horde's AI + 0x260), up the chain
	int attitude() const;
	// RW 0x664D7E: 0 without a machine or a controlling player; 1 (MM_Controller_Player) for a human player (+ 0x5C == 0); else 2 (MM_Controller_AI) |
	// the mood bit of the attitude (-3 0x2000, -2 0x100, -1 0x200, 0 0x400, 1 0x800, 2 0x1000, anything else 0x400). INFERENCE: the unit type bits
	// (0x10 / 0x20 / 0x40 from + 0x1DC and the turret + 0x20C) are not computed: no reader ported here uses them
	unsigned moodMatrixValue() const;
	// RW 0x664E18 getMoodMatrixActionAdjustment(action 0 idle, 1 move, 2 attack, 3 ...): 1 (Ok) for an INFANTRY + IGNORED_IN_GUI template or a
	// player-controlled unit; else by the mood bits (idle: sleep 0x11, passive 0x21, normal 1, alert 0x41, aggressive 0x81; move: sleep 0x12, passive
	// 0x21, alert 0x44, aggressive 0x84; attack: sleep and -3 0x12; 3: sleep and -3 0x12, alert 0x41, aggressive 0x81; anything else 1)
	unsigned moodAdjustment(int action) const;
	// the attack machine the AIAttackState builds for this module (the horde object builds the horde machine)
	virtual AttackMachineKind attackMachineKind() const { return ATTACK_MACHINE_NORMAL; }
	virtual std::unique_ptr<AIStateMachine> makeHordeAttackMachine(AIAttackState *) { return nullptr; }

	// ---- lane GARRISON-2: the turret (GameLogic/AI/TurretAI.h; RW AI + 0x20C, one slot) ----
	TurretAI *turret() const { return m_turret.get(); }
	// RW 0x66243F getWhichTurretForCurWeapon: the turret that holds the current weapon, null when none
	TurretAI *turretForCurWeapon() const;
	// RW 0x662411 getTurretTurnRate
	float turretTurnRate() const;

	// ---- lane GARRISON-1 (GameLogic/AI/AIGarrisonStates.h): the enter / exit commands of RotWK's aiDoCommand (RW 0x66A7EC) ----
	// AI command 0x42 -> RW 0x66D85F privateEnter: a mobile unit goes to AIMoveToPositionAndEnterState (56) with the container as the goal (MSG_ENTER's group,
	// RW 0x774EDF -> 0x770EDE). Returns false when the unit is not mobile (RW 0x66D86E)
	bool aiEnter(Object *container, CommandSourceType source);
	// AI command 0x17 -> RW 0x664579 privateEnterObject: a horde enters through its contain (horde slot 0x80); another unit that may enter goes to AIEnterState (15)
	void aiEnterObject(Object *container, CommandSourceType source);
	// AI command 0x3D -> RW 0x6645F3: AIHordeEnterState (52)
	void aiHordeEnter(Object *container, CommandSourceType source);
	// AI command 0x1A -> RW 0x66468F privateExit (the container: the argument, else the object's own); a horde exits through its contain (slot 0x84); else AIExitState (38)
	void aiExit(Object *container, CommandSourceType source);
	// AI command 0x3E -> RW 0x664767: AIHordeExitState (53)
	void aiHordeExit(Object *container, CommandSourceType source);
	// RW 0x855830 (the update interface's slot 1): the AI runs while the object is DISABLED_HELD (3): a garrisoned rider still fights
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)(1u << 3); }

	// a command this lane does not execute (attack, guard, hunt, enter, ...): counted by name, never dropped silently
	void aiUnported(const char *command, CommandSourceType source);
	const std::map<std::string, unsigned> &unportedCommands() const { return m_unportedCommands; }

	// ---- queries ----
	bool isIdle() const override;             // RW 0x6643FC
	bool isMoving() const;                    // RW 0x664485
	// lane AI-2: RW vslot 0x1BC (RW 0x662B3C): the current state's slot 0x24 (AIState::isActive), true without a current state
	bool isStateActive() const { return !m_machine || !m_machine->currentState() ? true : m_machine->currentState()->isActive(); }
	bool isBlocked() const { return m_mover->isBlocked(); }
	bool isBusy() const { return m_machine && m_machine->currentStateId() == AI_BUSY; }
	// ---- lane EXIT-1: the horde member update (GameLogic/Module/AIUpdateHordeMember.cpp) ----
	// RW 0x66E654 .. 0x66E6F4: a HORDE_MEMBER whose machine is busy (RW 0x741724) or whose locomotor goal is explicit / an angle / explicit with a path (not a
	// DOZER with an angle goal) runs RW 0x66C748 instead of the ordinary update RW 0x6695EF
	bool runsHordeMemberUpdate() const;
	// RW 0x66C748; returns the frames to sleep
	unsigned hordeMemberUpdate();
	unsigned long long memberUpdates() const { return m_memberUpdates; } ///< frames run through RW 0x66C748 (a counter, not state)
	static std::string hordeMemberUpdateStop();
	bool isDoingGroundMovement() const;       // B1 AIUpdate.cpp:3978
	bool isAircraftThatAdjustsDestination() const; // B1 AIUpdate.cpp:4010
	bool canPathThroughUnits() const { return m_canPathThroughUnits; }
	void setCanPathThroughUnits(bool v) { m_canPathThroughUnits = v; }
	CommandSourceType lastCommandSource() const { return m_lastCommandSource; }
	unsigned currentStateId() const { return m_machine ? m_machine->currentStateId() : (unsigned)AI_NO_STATE; }

	// ---- locomotors ----
	// B1 :2342 chooseLocomotorSet: LOCOMOTORSET_NORMAL becomes NORMAL_UPGRADED when the upgrade flag is set; false when the object has no such set
	bool chooseLocomotorSet(int set);
	int curLocomotorSet() const { return m_curLocomotorSet; }
	void setLocomotorUpgrade(bool on);
	// B1 :2385 chooseGoodLocomotorFromCurrentSet: the locomotor of the set whose surfaces fit the cell under the unit
	void chooseGoodLocomotorFromCurrentSet();
	Locomotor *curLocomotor() const { return m_curLocomotor; }
	float curLocomotorSpeed() const;          // per frame, at the unit's damage state
	float locomotorSetSpeed() const { return m_locomotorSetSpeed; } // raw distance per second (RW ai+0x1F8)
	const ObjectMovementInfo &movementInfo() const { return *m_info; }

	// ---- the movement component ----
	AIMover &mover() { return *m_mover; }
	const AIMover &mover() const { return *m_mover; }
	AIStateMachine &stateMachine() { return *m_machine; }
	const AIStateMachine *stateMachineOrNull() const { return m_machine.get(); }
	AIWorld &world() const { return *m_world; }
	ObjectPathfindAdapter &adapter() const { return *m_adapter; }
	// B1 :2513 wakeUpNow
	void wakeUpNow();
	// B1 :2492 setQueueForPathTime
	void setQueueForPathTime(unsigned frames);
	void setDesiredSpeed(float s) { m_mover->setDesiredSpeed(s); }
	// the raw locomotor goal setters the horde member pass uses (B1 AIUpdate.cpp:3925-3975; RW vslots 0x1D8 / 0x1DC / 0x1E4 / 0x1E8)
	void setLocomotorGoalNone();
	void setLocomotorGoalOrientation(float angle);
	void setLocomotorGoalPositionExplicit(const Coord3D &pos);
	// the horde member's move order of the member pass (RW vslot 0x214, B1 0x1DC): a path to `dest`, followed by the locomotor without the
	// state machine
	void hordeMemberMoveTo(const Coord3D &dest);
	// the horde member's straight move (RW vslot 0x210, B1 0x1D8, used while the horde is engaged in melee): an explicit goal
	void hordeMemberMoveExplicit(const Coord3D &dest);
	// RW 0x6631AE: the distance the hub adds to the remaining path length
	void setPathExtraDistance(float d) { m_mover->setPathExtraDistance(d); }

	// the object's model condition by the binary's name (MOVING, CLIMBING, ...); a name the registry lacks is a logic error
	static int modelConditionBit(const char *name);

	// ---- AIMoveHost (PATH-1) ----
	PathfindObject &pathfindObject() override;
	LocomotorHost &locomotorHost() override;
	Locomotor *locomotor() override { return m_curLocomotor; }
	void refreshLocomotor() override { chooseGoodLocomotorFromCurrentSet(); } // RW 0x66997D: before every locomotor pass
	PathfindLocomotorInfo locomotorInfo() const override;
	unsigned frame() const override;
	void setPosition(const Coord3D &p) override;
	float orientation() const override;
	void setOrientation(float angle) override;
	float groundHeightAt(float x, float y) const override;
	void setLayer(PathfindLayerEnum layer) override;
	bool isImmobile() const override;
	bool isStatusImmobile() const override;
	bool isContained() const override;
	int stateId() const override { return (int)currentStateId(); }
	AIMover *moverOf(PathfindObjectID id) override;
	AIMover *containerMover() override;
	float speedOf(const PathfindObject &o) const override;
	Coord2D unitDirectionOf(const PathfindObject &o) const override;
	int pathPriority() const override;
	PathfindObjectID attackTargetOf(const PathfindObject &o) const override;
	float wanderFactor() const override;
	float closeEnoughDist() const override;
	float locomotorSpeed() const override;
	bool movingBackwards() const override;
	bool setModelCondition(const char *name, bool on) override;
	void startMoveSound() override; // lane AUDIO-3: RW 0x748C0B
	void stopMoveSound() override;
	float relativeAngleTo(const Coord3D &p) const override;
	bool moveToPositionFromAI(const Coord3D &p) override { aiMoveToPosition(p, CMD_FROM_AI); return true; } // lane MOVE-2 r3: RW 0x66C4CA from RW 0x66D16E
	bool isCellTypeTwo(const Coord3D &p, PathfindLayerEnum layer) const override;
	// ---- lane PHYS-1 (AIUpdateAllies.cpp) ----
	bool attackRangeFrom(const Coord3D &from, PathfindObjectID victim, const Coord3D &victimPos, float extra) override;
	bool attackRangeFromTo(const Coord3D &from, PathfindObjectID victim, const Coord3D &victimPos, float extra) override;
	const PathfindObject *findPathfindObject(PathfindObjectID id) override;
	void clearAlliesFromPath(Path &path) override;
	void noteLogicStop(const std::string &line) override;
	// RW 0x66C66E (AI command 0x34 -> RW 0x66DA5F): `unit` asks this unit to step out of its path; `requesterPos` is the start of that path
	void aiMoveAwayFromUnit(Object *unit, const Coord3D &requesterPos, CommandSourceType source);
	// the two requesters remembered by RW 0x66DA5F (AI +0x198 / +0x19C), the retry flag (+0x3BA)
	ObjectID moveAwayRequester(int i) const { return m_moveAwayRequesters[i]; }
	// RW 0x662B80 (lane MOVE-3 r2): the unit is moving out of the way (its current or temporary state is AI_MOVE_OUT_OF_THE_WAY, 26) for `requester` (one of the
	// two remembered requesters, AI + 0x198 / + 0x19C)
	bool isMovingAwayFrom(ObjectID requester) const;
	ObjectID lastMoveAwayRequester() const { return m_moveAwayRequesters[0]; } ///< lane MOVE-3 r4: the newest of the two requester slots aiMoveAwayFromUnit records (diagnostics)

	// ---- lane MODULES-3: the emotion AI (AIUpdateEmotion.cpp, AIEmotionStates.cpp) ----
	// RW 0x662FC8 (the nugget start, RW 0x8E0F98): EmotionAIState BACK_AWAY / IDLE / RUN_AWAY_PANIC run the temporary state 48 / 42 / 20 LOCKED (-1), FACE_OBJECT /
	// QUARREL the state 59 / 58 until replaced (-2); AVOID_SCARER and unknown values nothing. Nothing either when that state already is the temporary state. The
	// object's horde makes its members busy (horde interface slot 0x13C, RW 0x876D4E), the goal is saved (RW 0x741675), the source becomes the goal object
	void emotionEnterAIState(int aiState, Object *source);
	// RW 0x663053 (the nugget update once AILockDuration ran out): a locked temporary state is re-entered until replaced (-2)
	void emotionLockElapsed();
	// RW 0x66309C (the nugget stop): the temporary state leaves (RW 0x751DA9)
	void emotionLeaveAIState();
	// RW 0x667174 (aiDoCommand's gate): `command` is the AICommandType number (0 move to position, 1 move to object, ...; -1 unknown); false when the object is dead,
	// a PLAYER order meets PreventPlayerCommands (+ 0x3C5) or the model condition 206, a SCRIPT order meets + 0x3C6, the temporary state is locked, or a PLAYER
	// move order meets a locomotor set speed of 0. Every ai* command of this class asks it first
	bool allowedToRespondToCommand(CommandSourceType source, int command) const;
	// lane AUDIO-3 r2: aiDoCommand's entry: the gate, then (passed) the module's own reaction before the command runs (commandAccepted). Every ai* command calls this
	bool acceptCommand(CommandSourceType source, int command, Object *target = nullptr)
	{
		if (!allowedToRespondToCommand(source, command))
		{
			return false;
		}
		commandAccepted(source, command, target);
		return true;
	}
	// lane MOVE-2: the command number of a move variant whose RotWK number is not identified (the group move with a final angle, a queued waypoint): the gate
	// treats it as an unknown command (-1), HordeAIUpdate's member hand-off (RW 0x89E169) as a move (INFERENCE, S-1501)
	static const int kCommandUnidentifiedMove = -2;
	// a command passed the gate and is about to run (RotWK DozerAIUpdate::aiDoCommand RW 0x88E44F cancels its task on a player command; HordeAIUpdate::aiDoCommand
	// RW 0x89E169 makes its members busy, lane MOVE-2); `target` is the command's object (attack commands 0xB / 0xC), else null; default nothing
	virtual void commandAccepted(CommandSourceType source, int command, Object *target) { (void)source; (void)command; (void)target; }
	void setPreventPlayerCommands(bool on) { m_preventPlayerCommands = on; } // RW + 0x3C5 (the nugget start / stop with PreventPlayerCommands)
	bool preventPlayerCommands() const { return m_preventPlayerCommands; }
	// RW + 0x3C4: the panic cower's short cower (MinCowerTime / 4); its writer is not identified (S-1027), only state 21's exit clears it
	bool fastCower() const { return m_fastCower; }
	void setFastCower(bool on) { m_fastCower = on; }
	// AI module data + 0x34 / + 0x30 (MinCowerTime / MaxCowerTime, parseDurationUnsignedInt, default 0: RW ctor 0x66E8FF)
	unsigned minCowerTime() const { return m_minCowerTime; }
	unsigned maxCowerTime() const { return m_maxCowerTime; }
	// RW 0x68E43B: the object's vision range (Object + 0x1B0, the template's VisionRange: S-1027) x (1 + the attribute modifier sum of type 16) x the height bonus
	float objectVisionRange() const;
	static float objectVisionRangeOf(Object &object); ///< the same for any object (RW 0x68E43B is Object's; lane CAMP-1H)
	// AIMoveHost (lane MODULES-3: the safe path of the run-away-panic state)
	bool repulsorPosition(PathfindObjectID id, Coord3D &out) override;
	float safePathRadius() override;
	// the emotion states' stop lines (S-1021 / S-1027)
	static std::vector<std::string> emotionStops();

	// ZH Object::reactToTransformChange -> Pathfinder::updatePos: the unit's position cell follows it (RW 0x8E26B7)
	void reactToTransformChange();
	// RW 0x66E233 through the collision pass of AIWorld
	void onCollide(PathfindObject &other) { m_mover->onCollide(other); }

	// the stops the movers of this unit raised (AIMover, Locomotor), each once
	std::vector<std::string> stopsRaised() const;
	// the stop lines of the AI module itself (S-221, S-222): each applies to every AI in a game
	static std::vector<std::string> allStops();
	// the OpenBFME state hash of the movement state of this unit
	// (crc() above)

protected:
	// the hook HordeAIUpdate adds its per-frame work to (B1 HordeAIUpdate_update.cpp:261-293, RW 0x89E2D0)
	virtual void updateBeforeBase() {}
	virtual const char *machineName() const { return "AIUpdateInterfaceMachine"; }
	// the state machine of the class (ZH makeStateMachine): the base states; a subclass adds its own
	virtual std::unique_ptr<AIStateMachine> makeStateMachine();

private:
	class ObjectLocomotorHost;
	void privateMoveToPosition(const Coord3D &pos, CommandSourceType source);
	void goalPositionClipped(const Coord3D &pos, CommandSourceType source, Coord3D &out) const;
	void doMovementCompleteBookkeeping();
	void setLastCommandSource(CommandSourceType s) { m_lastCommandSource = s; }
	void readCombatSettings(const ModuleData *data);

	AIWorld *m_world = nullptr;
	PathfindObjectID m_id = PATHFIND_INVALID_ID; // the object id, kept for the destructor (the adapter leaves with the object)
	ObjectPathfindAdapter *m_adapter = nullptr;
	const ObjectMovementInfo *m_info = nullptr;
	std::unique_ptr<AIMover> m_mover;
	std::unique_ptr<ObjectLocomotorHost> m_locoHost;
	std::unique_ptr<AIStateMachine> m_machine;
	std::vector<std::unique_ptr<Locomotor>> m_locomotors; // the current set, in set order (B1 m_locomotorSet)
	Locomotor *m_curLocomotor = nullptr;
	int m_curLocomotorSet = -1;
	float m_locomotorSetSpeed = 0.0f;
	bool m_upgradedLocomotors = false;
	bool m_canPathThroughUnits = false;
	ObjectID m_moveAwayRequesters[2] = { INVALID_ID, INVALID_ID }; // lane PHYS-1: RW AI +0x198 / +0x19C (RW 0x66DA5F)
	bool m_isInUpdate = false;
	unsigned long long m_memberUpdates = 0; // lane EXIT-1 (a counter, not hashed)
	std::array<std::uint32_t, 19> m_luaConditionSnapshot{}; ///< lane HERO-2: + 0x290, the ModelCondition script events' snapshot (RW 0x663E32)
	bool m_moveLoopPosted = false; // lane AUDIO-3: a move loop is kept for this object on the audio side (audio bookkeeping only: no logic reads it, not hashed)
	bool m_destroying = false; // set by the destructor: the machine's exits run on an object whose drawable the world hooks already removed
	CommandSourceType m_lastCommandSource = CMD_FROM_AI;
	ObjectID m_currentVictim = INVALID_ID;
	ObjectID m_targetHorde = INVALID_ID;
	bool m_attackMoveActive = false;
	Coord3D m_attackMoveGoal{ 0.0f, 0.0f, 0.0f };
	CombatSettings m_combat;
	unsigned m_nextMoodCheck = 0;
	int m_attitude = 0; ///< lane SCRIPT-3: RW + 0x218
	std::map<std::string, unsigned> m_unportedCommands;
	// RW obj+0x198 / +0x1A6: the movers' "pending position"
	Coord3D m_pendingPosition{ 0.0f, 0.0f, 0.0f };
	bool m_pendingValid = false;
	// lane SMOOTH-1: the logic frame of the last setPendingPosition. NOT hashed and never read by the simulation: the client's render snapshot matches
	// it against the snapshot's frame. Lane SMOOTH-2: Object::recordTransform now clears m_pendingValid as RW 0x62618F clears +0x1A6 (S-812 resolved), so
	// the stamp only guards an object whose transform was not recorded in the frame
	unsigned m_pendingFrame = 0;
	bool m_stateMachineMade = false;
	std::vector<std::string> m_locomotorNotes;
	// lane MODULES-3
	bool m_preventPlayerCommands = false; // RW + 0x3C5
	bool m_fastCower = false;             // RW + 0x3C4
	unsigned m_minCowerTime = 0, m_maxCowerTime = 0;
	// lane GARRISON-2: the turret's data (the AI module's `Turret` block, the raw module data) and the turret (RW AI + 0x20C, made by the AI constructor RW 0x66ECFE)
	std::unique_ptr<TurretAIData> m_turretData;
	std::unique_ptr<TurretAI> m_turret;
};
