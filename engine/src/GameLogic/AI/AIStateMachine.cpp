// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIStateMachine.h for the sources and what is inference.

#include "GameLogic/AI/AIStateMachine.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <stdexcept>

// ---------------------------------------------------------------------------------------------------------------------------------
// AIState
// ---------------------------------------------------------------------------------------------------------------------------------
AIState::AIState(AIStateMachine &machine, const char *name)
	: m_machine(&machine)
	, m_name(name)
{
}

AIUpdateInterface &AIState::ai() const
{
	return m_machine->owner();
}

Object &AIState::owner() const
{
	return *m_machine->owner().getObject();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIStateMachine
// ---------------------------------------------------------------------------------------------------------------------------------
AIStateMachine::AIStateMachine(AIUpdateInterface &owner, const char *name)
	: m_owner(&owner)
	, m_name(name)
{
}

AIStateMachine::~AIStateMachine()
{
	halt();
}

namespace
{
thread_local int t_loadGuards = 0; // lane SAVE-4c: AIStateMachine::LoadGuard
} // namespace

AIStateMachine::LoadGuard::LoadGuard()
{
	++t_loadGuards;
}

AIStateMachine::LoadGuard::~LoadGuard()
{
	--t_loadGuards;
}

void AIStateMachine::halt()
{
	if (t_loadGuards > 0)
	{
		m_temporaryState = nullptr; // lane SAVE-4c: a machine a load replaces leaves without its exits
		m_currentState = nullptr;
		return;
	}
	// ZH StateMachine::~StateMachine: the current state exits without a state to go to
	if (m_temporaryState)
	{
		m_temporaryState->onExit(EXIT_RESET);
		m_temporaryState = nullptr;
	}
	if (m_currentState)
	{
		AIState *s = m_currentState;
		m_currentState = nullptr;
		s->onExit(EXIT_RESET);
	}
}

void AIStateMachine::defineState(unsigned id, std::unique_ptr<AIState> state, unsigned successId, unsigned failureId, std::vector<AIStateCondition> conditions)
{
	if (m_states.count(id))
	{
		throw std::logic_error("AIStateMachine " + m_name + ": duplicate state id " + std::to_string(id));
	}
	state->m_id = id;
	state->m_successId = successId;
	state->m_failureId = failureId;
	state->m_conditions = std::move(conditions);
	if (m_defaultStateId == INVALID_STATE_ID)
	{
		m_defaultStateId = id;
	}
	m_states[id] = std::move(state);
	m_persistentStates.clear(); // lane UBER-2C: in id order, as crc walked m_states
	for (const auto &e : m_states)
	{
		if (e.second->hasPersistentCrc())
		{
			m_persistentStates.push_back(e.second.get());
		}
	}
}

AIState *AIStateMachine::findState(unsigned id) const
{
	auto it = m_states.find(id);
	return it == m_states.end() ? nullptr : it->second.get();
}

Object *AIStateMachine::goalObject() const
{
	return m_goalObjectID == INVALID_ID ? nullptr : m_owner->getObject()->logic().findObjectByID(m_goalObjectID);
}

void AIStateMachine::internalClear()
{
	m_goalObjectID = INVALID_ID;
	m_goalPosition = Coord3D{ 0.0f, 0.0f, 0.0f };
	clearGoalAngle();
}

StateReturnType AIStateMachine::initDefaultState()
{
	if (m_defaultStateId == INVALID_STATE_ID)
	{
		throw std::logic_error("AIStateMachine " + m_name + ": no state defined");
	}
	return internalSetState(m_defaultStateId);
}

// ZH StateMachine::clear (and the temporary state of AIStateMachine::clear, ZH AIStates.cpp:1025)
void AIStateMachine::clear()
{
	if (m_locked)
	{
		return;
	}
	if (m_temporaryState)
	{
		// RW 0x75464E: a locked temporary state (duration -1) refuses the clear; any other leaves first (RW 0x751DA9)
		if (m_temporaryStateFrameEnd == kTemporaryLocked)
		{
			return;
		}
		clearTemporaryState();
	}
	if (m_currentState)
	{
		AIState *s = m_currentState;
		m_currentState = nullptr;
		s->onExit(EXIT_RESET);
	}
	internalClear();
}

StateReturnType AIStateMachine::resetToDefaultState()
{
	if (m_locked)
	{
		return STATE_FAILURE;
	}
	if (m_temporaryState)
	{
		// RW 0x751DFC: refused under a locked temporary state, else the temporary state leaves first
		if (m_temporaryStateFrameEnd == kTemporaryLocked)
		{
			return STATE_CONTINUE;
		}
		clearTemporaryState();
	}
	if (m_currentState)
	{
		AIState *s = m_currentState;
		m_currentState = nullptr;
		s->onExit(EXIT_RESET);
	}
	internalClear();
	return internalSetState(m_defaultStateId);
}

StateReturnType AIStateMachine::setState(unsigned id)
{
	if (m_locked)
	{
		return STATE_CONTINUE;
	}
	if (m_temporaryState)
	{
		// RW 0x751E3A: refused under a locked temporary state, else the temporary state leaves first
		if (m_temporaryStateFrameEnd == kTemporaryLocked)
		{
			return STATE_CONTINUE;
		}
		clearTemporaryState();
	}
	return internalSetState(id);
}

// RW 0x751DA9
void AIStateMachine::clearTemporaryState()
{
	if (!m_temporaryState)
	{
		return;
	}
	m_temporaryState->onExit(EXIT_RESET);
	m_temporaryState = nullptr;
	m_goalObjectID = m_savedGoalObjectID; // RW 0x8DB8CB(find(+ 0x58)): an object that is gone restores no object
	if (m_goalObjectID != INVALID_ID && !goalObject())
	{
		m_goalObjectID = INVALID_ID;
	}
	m_goalPosition = m_savedGoalPosition; // RW 0x661EFE(+ 0x5C)
	m_savedGoalObjectID = INVALID_ID;
	// RW 0x751DE3 .. 0x751DF5: the owner's EmotionTrackerUpdate (Object + 0x254) stops its current nugget when that nugget has an AIState (RW 0x8B4FA1). Retail calls it
	// while the tracker still holds the nugget, so a nugget stop that clears its own temporary state runs the stop a second time (the inner call finds no temporary
	// state and returns before this point): ported as written
	if (EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*m_owner->getObject()))
	{
		t->stopCurrentWithAIState();
	}
}

// RW 0x741675
void AIStateMachine::saveGoalForTemporaryState()
{
	m_savedGoalObjectID = m_goalObjectID;
	m_savedGoalPosition = m_goalPosition;
}

// RW 0x74168E
StateReturnType AIStateMachine::setTemporaryStateRW(unsigned id, int duration)
{
	AIState *s = findState(id);
	if (m_temporaryState)
	{
		m_temporaryState->onExit(EXIT_RESET);
		m_temporaryState = nullptr;
	}
	if (!s)
	{
		return STATE_FAILURE;
	}
	m_owner->setLocomotorGoalNone(); // AI vslot 0x220 (RW 0x662997)
	m_temporaryState = s;
	const StateReturnType ret = m_temporaryState->onEnter();
	if (ret != STATE_CONTINUE)
	{
		m_temporaryState->onExit(EXIT_NORMAL);
		m_temporaryState = nullptr;
		return ret;
	}
	const int maxFrames = 60 * (int)LOGICFRAMES_PER_SECOND; // RW 0x7416EA: [0xD9F608] * 0x3C, cmovge
	if (duration >= maxFrames)
	{
		duration = maxFrames;
	}
	if (duration == -2)
	{
		m_temporaryStateFrameEnd = kTemporaryUntilReplaced;
	}
	else if (duration < 0)
	{
		m_temporaryStateFrameEnd = kTemporaryLocked;
	}
	else
	{
		m_temporaryStateFrameEnd = m_owner->frame() + (unsigned)duration;
	}
	return STATE_CONTINUE;
}

// ZH StateMachine::internalSetState (Common/StateMachine.cpp)
StateReturnType AIStateMachine::internalSetState(unsigned id)
{
	m_sleepTill = 0; // anytime the state changes, stop sleeping
	// ZH StateMachine: a transition to the DONE states leaves the machine (the attack sub machine's exits, B1 ids 0x270E success / 0x270F failure)
	if (id == 0x270Eu || id == 0x270Fu)
	{
		if (m_currentState)
		{
			AIState *old = m_currentState;
			m_currentState = nullptr;
			old->onExit(EXIT_NORMAL);
		}
		return id == 0x270Eu ? STATE_SUCCESS : STATE_FAILURE;
	}
	if (id == INVALID_STATE_ID)
	{
		id = m_defaultStateId;
	}
	AIState *next = findState(id);
	if (!next)
	{
		throw std::logic_error("AIStateMachine " + m_name + ": no state " + std::to_string(id));
	}
	if (m_currentState)
	{
		AIState *old = m_currentState;
		old->onExit(EXIT_NORMAL);
	}
	m_currentState = next;
	AIState *beforeEnter = m_currentState;
	StateReturnType status = m_currentState->onEnter();
	if (!m_currentState)
	{
		return STATE_FAILURE; // the state destroyed itself
	}
	if (beforeEnter != m_currentState)
	{
		status = STATE_CONTINUE; // onEnter changed the state: the new one runs next
	}
	if (IS_STATE_SLEEP(status))
	{
		m_sleepTill = m_owner->frame() + GET_STATE_SLEEP_FRAMES(status);
		return status;
	}
	return checkForTransitions(*m_currentState, status);
}

// ZH State::friend_checkForTransitions: success / failure move to the state's successor
StateReturnType AIStateMachine::checkForTransitions(AIState &state, StateReturnType status)
{
	static thread_local int depth = 0;
	struct Guard
	{
		Guard() { ++depth; }
		~Guard() { --depth; }
	} guard;
	if (depth >= 20)
	{
		return STATE_FAILURE; // ZH: "checkfortransitionsnum is > 20"
	}
	switch (status)
	{
	case STATE_SUCCESS:
		return internalSetState(state.successId());
	case STATE_FAILURE:
		return internalSetState(state.failureId());
	default:
	{
		bool matched = false;
		const StateReturnType r = testConditions(state, matched);
		if (matched)
		{
			return r;
		}
		// lane PLAY-4: a state's own condition transition (the guard machine's "attacked me" condition, RotWK's AIGuardMachine constructor RW 0x949A10)
		const unsigned to = state.conditionTransition();
		if (to != INVALID_STATE_ID)
		{
			return internalSetState(to);
		}
		return STATE_CONTINUE;
	}
	}
}

StateReturnType AIStateMachine::testConditions(AIState &state, bool &matched)
{
	matched = false;
	for (const AIStateCondition &c : state.conditions())
	{
		if (!c.test || !c.test(state, c.userData))
		{
			continue;
		}
		matched = true;
		// ZH: an exit id answers the result without leaving the state (the caller's machine leaves the state that runs this machine)
		if (c.toId == 0x270Eu)
		{
			return STATE_SUCCESS;
		}
		if (c.toId == 0x270Fu)
		{
			return STATE_FAILURE;
		}
		return internalSetState(c.toId);
	}
	return STATE_CONTINUE;
}

StateReturnType AIStateMachine::checkForSleepTransitions(AIState &state, StateReturnType status)
{
	bool matched = false;
	const StateReturnType r = testConditions(state, matched);
	return matched ? r : status;
}

// ZH StateMachine::updateStateMachine
StateReturnType AIStateMachine::updateStateMachine()
{
	if (m_temporaryState)
	{
		// RW 0x751D34: the end frame compares unsigned (-1 / -2 never run out); a continuing or sleeping temporary state keeps the machine
		StateReturnType status = m_temporaryState->update();
		if (m_temporaryStateFrameEnd < m_owner->frame())
		{
			if (status == STATE_CONTINUE)
			{
				status = STATE_SUCCESS; // ran out of time
			}
		}
		if ((int)status >= 0)
		{
			return status;
		}
		if (m_temporaryState)
		{
			m_temporaryState->onExit(EXIT_NORMAL);
			m_temporaryState = nullptr;
			m_goalObjectID = m_savedGoalObjectID; // RW 0x751DBD .. 0x751DDB
			if (m_goalObjectID != INVALID_ID && !goalObject())
			{
				m_goalObjectID = INVALID_ID;
			}
			m_goalPosition = m_savedGoalPosition;
			m_savedGoalObjectID = INVALID_ID;
			if (m_currentState)
			{
				m_currentState->onTemporaryStateEnded(); // RW 0x751D9A: the current state's slot 0x1C (the move states' RW 0x740C97)
			}
		}
	}
	const unsigned now = m_owner->frame();
	if (m_sleepTill != 0 && now < m_sleepTill)
	{
		if (!m_currentState)
		{
			return STATE_FAILURE;
		}
		return checkForSleepTransitions(*m_currentState, STATE_SLEEP(m_sleepTill - now));
	}
	m_sleepTill = 0;
	if (!m_currentState)
	{
		return STATE_FAILURE;
	}
	AIState *beforeUpdate = m_currentState;
	StateReturnType status = m_currentState->update();
	if (!m_currentState)
	{
		return STATE_FAILURE;
	}
	if (beforeUpdate != m_currentState)
	{
		status = STATE_CONTINUE; // update changed the state: the new one runs at once
	}
	if (IS_STATE_SLEEP(status))
	{
		m_sleepTill = now + GET_STATE_SLEEP_FRAMES(status);
		return checkForSleepTransitions(*m_currentState, STATE_SLEEP(m_sleepTill - now));
	}
	return checkForTransitions(*m_currentState, status);
}

// ZH AIStateMachine::setTemporaryState
StateReturnType AIStateMachine::setTemporaryState(unsigned id, unsigned frames)
{
	AIState *s = findState(id);
	if (!s)
	{
		return STATE_FAILURE;
	}
	if (m_temporaryState)
	{
		m_temporaryState->onExit(EXIT_RESET);
		m_temporaryState = nullptr;
	}
	else
	{
		// the persistent goal is kept while the temporary state uses the machine's goal
		m_savedGoalPosition = m_goalPosition;
		m_savedGoalObjectID = m_goalObjectID;
	}
	m_temporaryState = s;
	const StateReturnType ret = m_temporaryState->onEnter();
	if (ret != STATE_CONTINUE)
	{
		m_temporaryState->onExit(EXIT_NORMAL);
		m_temporaryState = nullptr;
		m_goalPosition = m_savedGoalPosition;
		m_goalObjectID = m_savedGoalObjectID;
		return ret;
	}
	const unsigned maxFrames = 60u * (unsigned)LOGICFRAMES_PER_SECOND; // ZH FRAME_COUNT_MAX
	if (frames > maxFrames)
	{
		frames = maxFrames;
	}
	m_temporaryStateFrameEnd = m_owner->frame() + frames;
	return ret;
}

void AIStateMachine::addToGoalPath(const Coord3D &p)
{
	if (m_goalPath.empty() || m_goalPath.back().x != p.x || m_goalPath.back().y != p.y || m_goalPath.back().z != p.z)
	{
		m_goalPath.push_back(p);
	}
}

const Coord3D *AIStateMachine::goalPathPosition(int index) const
{
	if (index < 0 || (size_t)index >= m_goalPath.size())
	{
		return nullptr;
	}
	return &m_goalPath[(size_t)index];
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIIdleState (ZH AIStates.cpp:1273-1460; B1 AIIdleState::update RVA 0x188090: the sleep is 10 frames plus the random offset)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
const unsigned kIdleCountdownDelay = 2u * (unsigned)LOGICFRAMES_PER_SECOND; // B1 update: 10
}

AIIdleState::AIIdleState(AIStateMachine &m)
	: AIState(m, "AIIdleState")
{
}

StateReturnType AIIdleState::onEnter()
{
	m_inited = true;
	// lane TARGET-1: RW 0x7489B6 (AIIdleState::onEnter): the AI's next mood check is pushed back (RW 0x662DD9: ForceIdleMSEC, randomly offset)
	ai().resetNextMoodCheckTime();
	// ZH: the idle countdown is randomised so the idle checks of many units do not spike on the same frame (S-221: the RW draw is not read)
	m_initialSleepOffset = (unsigned short)ai().getObject()->logic().random().getValue(0, (int)kIdleCountdownDelay, "AIStates.cpp", 0x520);
	return STATE_CONTINUE; // never sleep at the start: the cell check runs the first time through
}

// ZH AIStates.cpp:1335: only once, in the first update (onEnter of idle runs during object creation).
// lane OFFMAP-1: TARGET RW 0x745A64 (BFME2 0x74405D, decomp AIIdleStateUpdate.cpp:279, tier B same-shape; disassembly read): + 0x25 m_inited; a
// HORDE_MEMBER (status 0x26, RW 0x44DDEC) needs its container's AI idle (vslot 0x1B8); then, idle and doing ground movement (vslots 0x1B8 / 0x224), a
// non-zero position only updates the pathfinder's goal (RW 0x68B3BD -> 0x8E28DB with the layer of RW 0x68BBE0); vslot 0x220 setLocomotorGoalNone and
// setCurrentVictim(0) (RW 0x6682B1). RotWK has none of ZH's goalPosition / "frame <= 1: setPosition(goal)" cheat move: a unit stays where the map
// placed it, so one placed outside the pathfind grid (MAP ANG Angmar's counterattack wolf riders, y < 0) is not pulled onto cell 0 and stays OFF_MAP
void AIIdleState::doInitIdleState()
{
	if (!m_inited)
	{
		return;
	}
	m_inited = false;
	AIUpdateInterface &a = ai();
	Object *obj = a.getObject();
	static const int hordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
	bool containerIdle = true;
	if (hordeMember >= 0 && obj->testStatus((unsigned)hordeMember))
	{
		containerIdle = false;
		const Object *container = obj->getContainedBy();
		if (container && container->getAIUpdateInterface())
		{
			containerIdle = container->getAIUpdateInterface()->isIdle();
		}
	}
	if (a.isIdle() && a.isDoingGroundMovement() && containerIdle)
	{
		const Coord3D goalPos = *obj->getPosition();
		if (goalPos.x != 0.0f || goalPos.y != 0.0f || goalPos.z != 0.0f)
		{
			a.world().pathfinder().updateGoal(a.adapter(), &goalPos, a.adapter().getLayer());
		}
	}
	a.setLocomotorGoalNone();
	a.setCurrentVictim(nullptr);
}

// stop S-5148 (review of #73): RotWK's idle update (RW 0x7553AB) fails the state every fifth update (the countdown + 0x22, 5 again at onEnter RW 0x7489B6)
// for an owner that is not status 0x3A, has no temporary state machine (AI + 0x34) and has a StancesBehavior (RW 0x861E6E / 0x68BDA5) while its machine
// defines state 0x4E (RW 0x8DBA9A); the idle state's failure goes to 0x4E (RW 0x7537E0 .. 0x7537EE: defineState(0, idle, 0, 0x4E)), AIGoingIdleState, whose
// onEnter tells the StancesBehavior onMoveEnd (RW 0x743864): a stanced unit idle after an attack guards again where it stands (RW 0x662A21). The port's idle
// state never fails and its failure goes to idle (AIUpdate.cpp), so such a unit stays idle (its plain mood scan, no guard); a FACE_OBJECT state (59) is not
// ended by it either
const char *idleStanceStopLine()
{
	return "[S-5148] idle stances: RotWK's idle state (RW 0x7553AB) fails every fifth update for a unit with a StancesBehavior and its failure goes to "
	       "AIGoingIdleState 0x4E (RW 0x7537E0), which guards the unit again where it stands (RW 0x743864 -> 0x86228E); not ported: the unit stays idle";
}

StateReturnType AIIdleState::update()
{
	doInitIdleState();
	if (machine().findState(AI_GOING_IDLE) && StancesBehavior::of(*ai().getObject()))
	{
		ai().getObject()->logic().noteStop(idleStanceStopLine()); // S-5148
	}
	AIUpdateInterface &self = ai();
	if (self.attackMoveActive())
	{
		if (self.resumeAttackMove())
		{
			return STATE_CONTINUE; // the march goes on (the attack that interrupted it is over)
		}
	}
	// lane TARGET-1, RotWK's AIIdleState::update (RW 0x7553AB, BFME2 decomp AIIdleStateUpdate.cpp same shape): the sleep is 2 seconds (RW 0xDE784C) plus the
	// onEnter offset, which is used once; unless the object is disabled EMP / PARALYZED / TEMPORARILY_BUSY (+ 0x1C8 & 0x14 / 0x100) or the mood matrix's idle
	// action has IgnoreAll (RW 0x664E18(0) & 0x10), the mood scan getNextMoodTarget(1, 1) (RW 0x66844A) runs; a target is attacked (RW 0x66C536, AI command
	// source 2); none: the sleep is shortened to the next mood check (AI + 0x21C) when that comes sooner, and then the offset is kept for the next time.
	// Not ported here: the weapon's frame-window test (RW 0x6CA2C3), the stances countdown (+ 0x22) with state 0x4E (S-5148), the hunt status 0x49, the HORDE_MEMBER
	// melee wait, the repulsors (KindOf + 0x10D bit 5) and the crate (RW 0x66810B: none in RotWK) (S-326)
	const unsigned short oldSleepOffset = m_initialSleepOffset;
	unsigned timeToSleep = kIdleCountdownDelay + oldSleepOffset;
	m_initialSleepOffset = 0;
	const Object &obj = *self.getObject();
	const bool disabled = obj.isDisabledByType(DISABLED_EMP) || obj.isDisabledByType(DISABLED_PARALYZED) || obj.isDisabledByType(DISABLED_TEMPORARILY_BUSY);
	if (!disabled && !(self.moodAdjustment(0) & 0x10u))
	{
		if (Object *target = self.getNextMoodTarget(true, true))
		{
			if (self.aiAttackObject(target, CMD_FROM_AI))
			{
				return STATE_CONTINUE; // the machine changed state: the new state runs next
			}
		}
	}
	const unsigned now = self.frame();
	const unsigned next = self.nextMoodCheckTime();
	if (next > now)
	{
		const unsigned moodSleep = next - now;
		if (moodSleep < timeToSleep)
		{
			timeToSleep = moodSleep;
			m_initialSleepOffset = oldSleepOffset; // RW 0x7555C8: saved for the next time
		}
	}
	return STATE_SLEEP(timeToSleep);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIMoveToMachineState (ZH AIMoveToState + AIInternalMoveToState; PATH-1 owns the internal state)
// ---------------------------------------------------------------------------------------------------------------------------------
AIMoveToMachineState::AIMoveToMachineState(AIStateMachine &m, const char *name)
	: AIState(m, name)
{
}

StateReturnType AIMoveToMachineState::onEnter()
{
	AIUpdateInterface &a = ai();
	m_adjustsDestination = true;
	Object *goalObj = machine().goalObject();
	if (goalObj && goalObj->getID() == a.mover().ignoredObstacleID())
	{
		m_adjustsDestination = false; // units trying to get really close ignore the object as an obstacle (ZH AIMoveToState::onEnter)
	}
	m_goalPosition = goalObj ? *goalObj->getPosition() : machine().goalPosition();
	m_move = std::make_unique<AIMoveToState>(a.mover(), m_goalPosition, m_adjustsDestination);
	if (machine().haveGoalAngle())
	{
		m_move->setFinalAngle(machine().goalAngle());
	}
	return m_move->onEnter();
}

StateReturnType AIMoveToMachineState::update()
{
	if (!m_move)
	{
		return STATE_FAILURE;
	}
	if (Object *goalObj = machine().goalObject())
	{
		m_goalPosition = *goalObj->getPosition(); // the goal object may have moved
		m_move->setGoal(m_goalPosition);
	}
	return m_move->update();
}

// RW 0x740C97 (slot 0x1C): the internal move's "waiting for a path" (+ 0x49) is cleared when a temporary state ends: the next update asks for a new path
void AIMoveToMachineState::onTemporaryStateEnded()
{
	if (m_move)
	{
		m_move->clearWaitingForPath();
	}
}

void AIFollowPathState::onTemporaryStateEnded()
{
	if (m_move)
	{
		m_move->clearWaitingForPath();
	}
}

void AIMoveToMachineState::onExit(StateExitType)
{
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIFollowPathState (ZH AIStates.cpp:3253-3420)
// ---------------------------------------------------------------------------------------------------------------------------------
AIFollowPathState::AIFollowPathState(AIStateMachine &m, bool exitProduction)
	: AIState(m, exitProduction ? "AIFollowExitProductionPathState" : "AIFollowPathState")
	, m_exitProduction(exitProduction)
{
}

StateReturnType AIFollowPathState::enterSegment()
{
	AIUpdateInterface &a = ai();
	const Coord3D *pos = machine().goalPathPosition(m_index);
	if (!pos)
	{
		return STATE_FAILURE;
	}
	m_goalPosition = *pos;
	const Coord3D *next = machine().goalPathPosition(m_index + 1);
	m_adjustFinal = true;
	bool adjusts = true;
	if (m_exitProduction)
	{
		a.setCanPathThroughUnits(true);
		adjusts = false;
		m_adjustFinal = true;
	}
	if (next)
	{
		adjusts = false; // in the middle of a path the final goal location is not set yet
	}
	else
	{
		adjusts = m_exitProduction ? false : m_adjustFinal;
	}
	m_move = std::make_unique<AIMoveToState>(a.mover(), m_goalPosition, adjusts);
	const StateReturnType ret = m_move->onEnter();
	if (next)
	{
		const float dx = SimMath::subf32(next->x, pos->x), dy = SimMath::subf32(next->y, pos->y);
		float offset = SimMath::length2d(dx, dy);
		if (machine().goalPathPosition(m_index + 2))
		{
			offset = SimMath::addf32(offset, 40.0f); // 4 * PATHFIND_CELL_SIZE_F
		}
		a.setPathExtraDistance(offset);
	}
	else
	{
		a.setPathExtraDistance(0.0f);
	}
	return ret;
}

StateReturnType AIFollowPathState::onEnter()
{
	m_index = 0;
	return enterSegment();
}

void AIFollowPathState::onExit(StateExitType status)
{
	(void)status;
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
	if (m_exitProduction)
	{
		ai().setCanPathThroughUnits(false);
	}
}

StateReturnType AIFollowPathState::update()
{
	if (!m_move)
	{
		return STATE_FAILURE;
	}
	const StateReturnType ret = m_move->update();
	if (ret == STATE_SUCCESS)
	{
		// the next node of the path, or done (ZH: the path index advances when the segment's move succeeds)
		m_move->onExit();
		m_move.reset();
		++m_index;
		if (!machine().goalPathPosition(m_index))
		{
			return STATE_SUCCESS;
		}
		const StateReturnType e = enterSegment();
		return e == STATE_SUCCESS ? STATE_CONTINUE : e;
	}
	return ret;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the OpenBFME state hash
// ---------------------------------------------------------------------------------------------------------------------------------
void AIStateMachine::crc(StateHasher &h) const
{
	h.addU32(m_currentState ? m_currentState->id() : (unsigned)AI_NO_STATE);
	h.addU32(m_temporaryState ? m_temporaryState->id() : (unsigned)AI_NO_STATE);
	h.addU32(m_temporaryStateFrameEnd);
	h.addU32(m_defaultStateId);
	h.addU32(m_sleepTill);
	h.addBool(m_locked);
	h.addFloat(m_goalPosition.x);
	h.addFloat(m_goalPosition.y);
	h.addFloat(m_goalPosition.z);
	h.addU32(m_goalObjectID);
	h.addBool(m_haveGoalAngle);
	h.addFloat(m_goalAngle);
	h.addFloat(m_savedGoalPosition.x);
	h.addFloat(m_savedGoalPosition.y);
	h.addFloat(m_savedGoalPosition.z);
	h.addU32(m_savedGoalObjectID);
	h.addU32((std::uint32_t)m_goalPath.size());
	for (const Coord3D &p : m_goalPath)
	{
		h.addFloat(p.x);
		h.addFloat(p.y);
		h.addFloat(p.z);
	}
	if (m_currentState)
	{
		m_currentState->crc(h);
	}
	if (m_temporaryState)
	{
		m_temporaryState->crc(h);
	}
	for (const AIState *s : m_persistentStates) // lane UBER-2C: the states without kept fields hash nothing and are left out
	{
		if (s != m_currentState && s != m_temporaryState)
		{
			s->crcPersistent(h); // lane MODULES-3
		}
	}
}

void AIIdleState::crc(StateHasher &h) const
{
	h.addU32(m_initialSleepOffset);
	h.addBool(m_inited);
}

void AIMoveToMachineState::crc(StateHasher &h) const
{
	h.addFloat(m_goalPosition.x);
	h.addFloat(m_goalPosition.y);
	h.addFloat(m_goalPosition.z);
	h.addBool(m_adjustsDestination);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}

void AIFollowPathState::crc(StateHasher &h) const
{
	h.addI32(m_index);
	h.addBool(m_adjustFinal);
	h.addFloat(m_goalPosition.x);
	h.addFloat(m_goalPosition.y);
	h.addFloat(m_goalPosition.z);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}
