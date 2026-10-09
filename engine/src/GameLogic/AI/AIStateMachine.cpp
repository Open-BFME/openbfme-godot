// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIStateMachine.h for the sources and what is inference.

#include "GameLogic/AI/AIStateMachine.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
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

void AIStateMachine::halt()
{
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

void AIStateMachine::defineState(unsigned id, std::unique_ptr<AIState> state, unsigned successId, unsigned failureId)
{
	if (m_states.count(id))
	{
		throw std::logic_error("AIStateMachine " + m_name + ": duplicate state id " + std::to_string(id));
	}
	state->m_id = id;
	state->m_successId = successId;
	state->m_failureId = failureId;
	if (m_defaultStateId == INVALID_STATE_ID)
	{
		m_defaultStateId = id;
	}
	m_states[id] = std::move(state);
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
		return STATE_CONTINUE;
	}
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
		return STATE_SLEEP(m_sleepTill - now);
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
		return STATE_SLEEP(m_sleepTill - now);
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
	// ZH: the idle countdown is randomised so the idle checks of many units do not spike on the same frame (S-221: the RW draw is not read)
	m_initialSleepOffset = (unsigned short)ai().getObject()->logic().random().getValue(0, (int)kIdleCountdownDelay, "AIStates.cpp", 0x520);
	return STATE_CONTINUE; // never sleep at the start: the cell check runs the first time through
}

// ZH AIStates.cpp:1335: only once, in the first update (onEnter of idle runs during object creation)
void AIIdleState::doInitIdleState()
{
	if (!m_inited)
	{
		return;
	}
	m_inited = false;
	AIUpdateInterface &a = ai();
	if (a.isIdle() && a.isDoingGroundMovement())
	{
		// a unit stopped between grids: stake out the cell it is on and snap onto its goal (the pathfinder's goal for an idle unit is its own cell)
		Pathfinder &pf = a.world().pathfinder();
		Coord3D goalPos = *a.getObject()->getPosition();
		if (goalPos.x != 0.0f || goalPos.y != 0.0f || goalPos.z != 0.0f)
		{
			pf.updateGoal(a.adapter(), &goalPos, a.adapter().getLayer());
			if (pf.goalPosition(a.adapter(), &goalPos))
			{
				if (a.frame() <= 1)
				{
					a.getObject()->setPosition(&goalPos);
				}
				pf.updateGoal(a.adapter(), &goalPos, a.adapter().getLayer());
			}
		}
		pf.updatePos(a.adapter());
	}
	a.setLocomotorGoalNone();
}

StateReturnType AIIdleState::update()
{
	doInitIdleState();
	// COMBAT-1 (ZH AIIdleState::update + AIUpdateInterface mood): a unit with a weapon and an AutoAcquire flag looks for an enemy every MoodAttackCheckRate frames and attacks
	// the nearest one it finds (stop S-326); the scan replaces the long idle sleep
	AIUpdateInterface &self = ai();
	if (self.attackMoveActive())
	{
		if (self.resumeAttackMove())
		{
			return STATE_CONTINUE; // the march goes on (the attack that interrupted it is over)
		}
	}
	const unsigned mood = self.moodCheckInterval();
	if (mood != 0)
	{
		m_initialSleepOffset = 0;
		// lane SCRIPT-3 (RW 0x75550D): the mood adjustment of the idle action (RW 0x664E18(0)) with IgnoreAll (0x10, a sleeping computer unit) skips the scan
		if (Object *target = (self.moodAdjustment(0) & 0x10u) ? nullptr : self.nextMoodTarget())
		{
			if (self.aiAttackObject(target, CMD_FROM_AI))
			{
				return STATE_CONTINUE; // the machine changed state: the new state runs next
			}
		}
		return STATE_SLEEP(mood);
	}
	const unsigned timeToSleep = kIdleCountdownDelay + m_initialSleepOffset;
	m_initialSleepOffset = 0;
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
	for (const auto &e : m_states)
	{
		if (e.second.get() != m_currentState && e.second.get() != m_temporaryState)
		{
			e.second->crcPersistent(h); // lane MODULES-3 (nothing for the states without kept fields)
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
