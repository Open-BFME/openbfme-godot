// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The AI state machines' part of the obfme:AIWorld block (lane SAVE-4c, docs/SAVES.md section 8). OpenBFME native layout (stop S-4220). Retail:
// StateMachine::xfer RW 0x8DBAEC (window (1, 4): the sleep, the lock, the current and default ids, the current state's and the temporary state's snapshots,
// the goal object, position and angle) and AIStateMachine::xfer RW 0x755174 ((1, 2): StateMachine's, then the goal path and the temporary state's frame),
// both tier A (docs/SAVES-RETAIL.md section 6); ZH StateMachine::xfer (donor) saves every state of the machine by id (each state object is kept between its
// activations, so its fields are state whether it runs or not). This port's machine keeps a different set of fields (the saved goal of RotWK's own temporary
// state, the goal angle as a flag and a value), so the layout is OpenBFME's own, in the crc order, then every defined state in id order with its id and its
// xfer. The states themselves come from the class's constructor (makeStateMachine): a load checks that the machine has exactly the saved state ids.

#include "GameLogic/AI/AIStateMachine.h"

#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Module/AIUpdate.h"

void AIState::xferNoFields(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
}

void AIStateMachine::xfer(Xfer &x)
{
	// version 2 (lane BFME2CAMP1): the goal squad (RW AIStateMachine::xfer RW 0x755174 saves it as a "has squad" bool, then the squad); written only by a
	// machine that has one, so other saves keep version 1
	XferVersion v{ 1, (std::uint8_t)(x.isLoading() || m_goalSquad ? 2 : 1) };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	auto xferStatePtr = [&](AIState *&state, const char *what) {
		std::uint32_t id = state ? state->id() : (std::uint32_t)AI_NO_STATE;
		x.xferUnsignedInt(&id);
		if (loading)
		{
			state = id == (std::uint32_t)AI_NO_STATE ? nullptr : findState(id);
			if (id != (std::uint32_t)AI_NO_STATE && !state)
			{
				throw XferException("ai machine " + m_name + ": the " + what + " state " + std::to_string(id) + " is not one of its states");
			}
		}
	};
	xferStatePtr(m_currentState, "current");
	xferStatePtr(m_temporaryState, "temporary");
	x.xferUnsignedInt(&m_temporaryStateFrameEnd);
	std::uint32_t def = m_defaultStateId;
	x.xferUnsignedInt(&def);
	if (loading && def != m_defaultStateId)
	{
		throw XferException("ai machine " + m_name + ": the default state " + std::to_string(def) + " is not its " + std::to_string(m_defaultStateId));
	}
	x.xferUnsignedInt(&m_sleepTill);
	x.xferBool(&m_locked);
	AIXfer::coord(x, m_goalPosition);
	AIXfer::objectId(x, m_goalObjectID, "ai machine: the object id goalObjectID");
	x.xferBool(&m_haveGoalAngle);
	x.xferReal(&m_goalAngle);
	AIXfer::coords(x, m_goalPath);
	AIXfer::coord(x, m_savedGoalPosition);
	AIXfer::objectId(x, m_savedGoalObjectID, "ai machine: the object id savedGoalObjectID");
	if (v.current >= 2)
	{
		bool has = m_goalSquad != nullptr;
		x.xferBool(&has);
		if (loading)
		{
			m_goalSquad = has ? std::make_unique<AISquad>() : nullptr;
		}
		if (has)
		{
			x.xferVectorSize(m_goalSquad->objectIDs, 4);
			for (ObjectID &id : m_goalSquad->objectIDs)
			{
				AIXfer::objectId(x, id, "ai machine: a goal squad member id");
			}
		}
	}
	else if (loading)
	{
		m_goalSquad.reset();
	}
	std::uint32_t n = (std::uint32_t)m_states.size();
	x.xferUnsignedInt(&n);
	if (!loading)
	{
		for (auto &kv : m_states)
		{
			std::uint32_t id = kv.first;
			x.xferUnsignedInt(&id);
			kv.second->xfer(x);
		}
		return;
	}
	// lane STEALTHFLY1 (SAVES.md section 5, mechanism 1): a save made before a build added states to this machine (GiantBirdAIUpdate's follow states) lists
	// fewer: each saved state loads into the machine's state of its id, in id order, and a state the save does not list keeps its constructor's fields (it
	// was never entered). A saved state the machine does not have, or more saved states than it has, is refused
	if (n > (std::uint32_t)m_states.size())
	{
		throw XferException("ai machine " + m_name + ": " + std::to_string(n) + " saved states, the machine has " + std::to_string(m_states.size()));
	}
	auto it = m_states.begin();
	for (std::uint32_t i = 0; i < n; ++i)
	{
		std::uint32_t id = 0;
		x.xferUnsignedInt(&id);
		while (it != m_states.end() && it->first < id)
		{
			++it;
		}
		if (it == m_states.end() || it->first != id)
		{
			throw XferException("ai machine " + m_name + ": the saved state " + std::to_string(id) + " is not one of its states");
		}
		it->second->xfer(x);
		++it;
	}
}

// AIIdleState::xfer RW 0x741850 is (1, 2): u16, two bools, u16; this port keeps the countdown offset and the init flag
void AIIdleState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedShort(&m_initialSleepOffset);
	x.xferBool(&m_inited);
}

void AIMoveToMachineState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::coord(x, m_goalPosition);
	x.xferBool(&m_adjustsDestination);
	xferMove(x, ai().mover(), m_move);
}

void AIFollowPathState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferInt(&m_index);
	x.xferBool(&m_adjustFinal);
	AIXfer::coord(x, m_goalPosition);
	xferMove(x, ai().mover(), m_move);
}
