// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// StateMachine / AIStateMachine and the movement states of the AI (lane MOVE-1): the machine AIUpdateInterface drives every frame.
//
// SOURCES. Target (RotWK game.dat, caveat S-001): AIUpdateInterface::update reads the machine through [ai + 0x30] (RW 0x6643FC isIdle: the
// current state's slot 0x20 answers "idle"; no current state counts as idle). DONOR (the machine is the unmodified Zero Hour shape):
// ZH Common/StateMachine.cpp (State::friend_checkForTransitions, StateMachine::internalSetState / updateStateMachine / clear / setState),
// ZH GameLogic/AI/AIStates.cpp (AIStateMachine::setTemporaryState / updateStateMachine, AIIdleState, AIMoveToState, AIFollowPathState,
// AIWaitState) and BFME1 AIStateMachineConstructor.cpp (the BFME state ids: Idle 0, MoveTo 1, FollowPath 6, FollowExitProductionPath 7,
// Wait 8, Busy 42; B1 AIIdleState::update RW-equivalent 0x188090: the idle sleep is 10 frames plus a random offset).
//
// WHAT IS INFERENCE (stop S-221, docs/STOPS.md): that the state ids of RotWK are the BFME1 ones (RW AIMove.cpp tests 0x2A = Busy, 0x47 = a
// state RotWK added); that AIIdleState::onEnter draws its random sleep offset like ZH (GameLogicRandomValue(0, 10)); the attack, guard,
// hunt, dock, enter and waypoint states are not part of this lane (M2 / M3): a command that needs one is reported through
// AIUpdateInterface::unportedCommands(), never ignored silently.
//
// Determinism: the machine keeps its states in an ordered map by id; nothing depends on pointer values.

#pragma once

#include "GameLogic/AI/AICommandSink.h" // CommandSourceType (ZH: the sources of an order; RotWK production passes CMD_FROM_AI)
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/ObjectTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class AIUpdateInterface;
class AIStateMachine;
class Object;

// ZH StateReturnType: the sleep forms are positive frame counts
inline bool IS_STATE_SLEEP(StateReturnType r) { return (int)r > 0; }
inline StateReturnType STATE_SLEEP(unsigned frames) { return (StateReturnType)(int)frames; }
inline unsigned GET_STATE_SLEEP_FRAMES(StateReturnType r) { return (unsigned)(int)r; }

enum StateExitType
{
	EXIT_NORMAL = 0,
	EXIT_RESET = 1
};

// BFME1 AIStateMachineConstructor.cpp ids (S-221: RotWK's own numbering is not read); 0xF423F is "no state" (AIMoveHost::stateId)
enum AIStateType
{
	AI_IDLE = 0,
	AI_MOVE_TO = 1,
	AI_FOLLOW_PATH = 6,
	AI_FOLLOW_EXITPRODUCTION_PATH = 7,
	AI_WAIT = 8,
	AI_MOVE_OUT_OF_THE_WAY = 26,
	AI_MOVE_AND_TIGHTEN = 27,
	AI_BUSY = 42,
	AI_NO_STATE = 0xF423F
};
static const unsigned INVALID_STATE_ID = 0xFFFFFFFFu;

class AIState
{
public:
	AIState(AIStateMachine &machine, const char *name);
	virtual ~AIState() {}
	virtual StateReturnType onEnter() { return STATE_CONTINUE; }
	virtual StateReturnType update() = 0;
	virtual void onExit(StateExitType status) { (void)status; }
	// RW State vtable slot 0x20 (RW 0x6643FC): only the idle states answer true
	virtual bool isIdle() const { return false; }
	// RW State vtable slot 0x24 (lane AI-2): false for the plain states (RW 0x9188EB: idle RW 0xC28468, follow path RW 0xC29228, busy RW 0xC274A8); AIAttackState
	// answers "its own machine's state is not idle" (RW 0x740ECD -> 0x742DD0). AIUpdate vslot 0x1BC (RW 0x662B3C) asks it of the current state (true with none)
	virtual bool isActive() const { return false; }
	// the OpenBFME state hash of the state's own fields
	virtual void crc(class StateHasher &hasher) const { (void)hasher; }
	// lane MODULES-3: the fields a state keeps between its activations (RW keeps the state objects), hashed while it is neither the current nor the temporary state
	virtual void crcPersistent(class StateHasher &hasher) const { (void)hasher; }
	// lane MODULES-3 r2: RW State slot 0x1C, called on the current state when a temporary state ends by itself (RW 0x751D9A); nothing for most states, the move
	// states clear their internal move's "waiting for a path" (RW 0x740C97)
	virtual void onTemporaryStateEnded() {}

	unsigned id() const { return m_id; }
	const std::string &name() const { return m_name; }
	AIStateMachine &machine() const { return *m_machine; }
	AIUpdateInterface &ai() const;
	Object &owner() const;
	unsigned successId() const { return m_successId; }
	unsigned failureId() const { return m_failureId; }

private:
	friend class AIStateMachine;
	unsigned m_id = INVALID_STATE_ID, m_successId = INVALID_STATE_ID, m_failureId = INVALID_STATE_ID;
	AIStateMachine *m_machine;
	std::string m_name;
};

class AIStateMachine
{
public:
	AIStateMachine(AIUpdateInterface &owner, const char *name);
	~AIStateMachine();
	AIStateMachine(const AIStateMachine &) = delete;
	AIStateMachine &operator=(const AIStateMachine &) = delete;

	AIUpdateInterface &owner() const { return *m_owner; }
	const std::string &name() const { return m_name; }

	// ZH StateMachine::defineState; the first state defined is the default state
	void defineState(unsigned id, std::unique_ptr<AIState> state, unsigned successId, unsigned failureId);
	// ZH StateMachine::initDefaultState: enters the default state
	StateReturnType initDefaultState();

	// ZH StateMachine
	void clear();                              // leaves the current state (EXIT_RESET) unless locked
	StateReturnType resetToDefaultState();
	StateReturnType setState(unsigned id);     // refused (STATE_CONTINUE) while locked
	StateReturnType internalSetState(unsigned id);
	StateReturnType updateStateMachine();      // AIStateMachine::updateStateMachine: the temporary state first
	// ZH AIStateMachine::setTemporaryState: runs `id` on top of the current state for at most `frames` frames
	StateReturnType setTemporaryState(unsigned id, unsigned frames);
	// lane MODULES-3: RotWK's own temporary state (RW 0x74168E): the goal is not saved here (the caller saves it, RW 0x741675); the AI's locomotor goal is
	// cleared before the state enters (AI vslot 0x220); `duration` -1 holds the state LOCKED until it ends by itself (no command, setState, clear or reset
	// replaces it: RW 0x667174 / 0x751E3A / 0x751DFC / 0x75464E), -2 holds it until something replaces it, >= 0 for that many frames (at most 60 *
	// LOGICFRAMES_PER_SECOND); returns the state's onEnter result (STATE_FAILURE without the state)
	StateReturnType setTemporaryStateRW(unsigned id, int duration);
	void saveGoalForTemporaryState(); // RW 0x741675
	void clearTemporaryState();       // RW 0x751DA9: the temporary state exits (EXIT_RESET), the saved goal returns
	bool inTemporaryState() const { return m_temporaryState != nullptr; }                                // RW 0x66306E
	bool temporaryStateLocked() const { return m_temporaryState && m_temporaryStateFrameEnd == kTemporaryLocked; } // RW 0x663082
	static constexpr unsigned kTemporaryLocked = 0xFFFFFFFFu;   // RW machine + 0x54 == -1
	static constexpr unsigned kTemporaryUntilReplaced = 0xFFFFFFFEu; // -2
	void halt();                               // the owner is going away: the current state exits without entering anything

	void lock() { m_locked = true; }
	void unlock() { m_locked = false; }
	bool isLocked() const { return m_locked; }

	AIState *currentState() const { return m_currentState; }
	unsigned currentStateId() const { return m_currentState ? m_currentState->id() : (unsigned)AI_NO_STATE; }
	// lane PHYS-1: the temporary state's id (AI_NO_STATE when none; RW machine + 0x50)
	unsigned temporaryStateId() const { return m_temporaryState ? m_temporaryState->id() : (unsigned)AI_NO_STATE; }
	AIState *findState(unsigned id) const;
	size_t stateCount() const { return m_states.size(); }

	// the machine's goal (ZH StateMachine::m_goalPosition / m_goalObjectID, AIStateMachine::m_goalPath)
	const Coord3D &goalPosition() const { return m_goalPosition; }
	void setGoalPosition(const Coord3D &p) { m_goalPosition = p; }
	ObjectID goalObjectID() const { return m_goalObjectID; }
	// the facing a move ends with (BFME MSG_DO_MOVE_AND_ORIENTATE / formation move; AIMoveToState::setFinalAngle)
	bool haveGoalAngle() const { return m_haveGoalAngle; }
	float goalAngle() const { return m_goalAngle; }
	void setGoalAngle(float angle) { m_goalAngle = angle; m_haveGoalAngle = true; }
	void clearGoalAngle() { m_haveGoalAngle = false; m_goalAngle = 0.0f; }
	void setGoalObject(ObjectID id) { m_goalObjectID = id; }
	Object *goalObject() const;
	const std::vector<Coord3D> &goalPath() const { return m_goalPath; }
	void setGoalPath(const std::vector<Coord3D> &path) { m_goalPath = path; }
	void addToGoalPath(const Coord3D &p);
	const Coord3D *goalPathPosition(int index) const;
	// the OpenBFME state hash of the machine: the current and temporary state, the sleep, the lock, the goals
	void crc(class StateHasher &hasher) const;

private:
	friend class AIState;
	StateReturnType checkForTransitions(AIState &state, StateReturnType status);
	void internalClear();

	AIUpdateInterface *m_owner;
	std::string m_name;
	std::map<unsigned, std::unique_ptr<AIState>> m_states;
	AIState *m_currentState = nullptr;
	AIState *m_temporaryState = nullptr;
	unsigned m_temporaryStateFrameEnd = 0;
	unsigned m_defaultStateId = INVALID_STATE_ID;
	unsigned m_sleepTill = 0;
	bool m_locked = false;
	Coord3D m_goalPosition{ 0.0f, 0.0f, 0.0f };
	ObjectID m_goalObjectID = INVALID_ID;
	bool m_haveGoalAngle = false;
	float m_goalAngle = 0.0f;
	std::vector<Coord3D> m_goalPath;
	// the persistent goal saved while a temporary state runs (ZH setTemporaryState leaves the goal to the state; the machine keeps it)
	Coord3D m_savedGoalPosition{ 0.0f, 0.0f, 0.0f };
	ObjectID m_savedGoalObjectID = INVALID_ID;
};

// ---- the states ----------------------------------------------------------------------------------------------------------------
// AIIdleState (ZH AIStates.cpp:1273-1460, B1 RW-equivalent 0x188090): stakes out the unit's cell and sleeps.
class AIIdleState : public AIState
{
public:
	explicit AIIdleState(AIStateMachine &m);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	bool isIdle() const override { return true; }
	void crc(class StateHasher &hasher) const override;

private:
	void doInitIdleState();
	unsigned short m_initialSleepOffset = 0xFFFF;
	bool m_inited = false;
};

// AIMoveToState (ZH AIStates.cpp:2076 and AIInternalMoveToState): a PATH-1 AIMoveToState driven as a machine state; with a goal object
// the goal follows the object every frame.
class AIMoveToMachineState : public AIState
{
public:
	AIMoveToMachineState(AIStateMachine &m, const char *name = "AIMoveToState");
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void onTemporaryStateEnded() override; // RW 0x740C97

protected:
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_goalPosition{ 0.0f, 0.0f, 0.0f };
	bool m_adjustsDestination = true;
};

// AIFollowPathState (ZH AIStates.cpp:3253-3420): walks the machine's goal path node by node; the exit-production variant (id 7) does not
// adjust the destination and lets the unit path through units (ZH AI_FOLLOW_EXITPRODUCTION_PATH).
class AIFollowPathState : public AIState
{
public:
	AIFollowPathState(AIStateMachine &m, bool exitProduction);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void onTemporaryStateEnded() override; // RW 0x740C97

private:
	StateReturnType enterSegment();
	bool m_exitProduction;
	int m_index = 0;
	bool m_adjustFinal = true;
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_goalPosition{ 0.0f, 0.0f, 0.0f };
};

// AIWaitState (ZH AIStates.cpp:5347): does nothing, forever
class AIWaitState : public AIState
{
public:
	explicit AIWaitState(AIStateMachine &m) : AIState(m, "AIWaitState") {}
	StateReturnType update() override { return STATE_CONTINUE; }
};

// AIBusyState (id 0x2A, which AIMover::blockedBy tests): the unit is busy and takes no moves aside
class AIBusyState : public AIState
{
public:
	explicit AIBusyState(AIStateMachine &m) : AIState(m, "AIBusyState") {}
	StateReturnType update() override { return STATE_CONTINUE; }
};
