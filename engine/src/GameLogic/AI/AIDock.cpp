// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIDock.h for the target facts.

#include "GameLogic/AI/AIDock.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DockUpdate.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

namespace
{
const unsigned kWaitTimeoutFrames = 5u * 30u; // RW 0xD9F608 (LOGICFRAMES_PER_SECOND) * 30

AIDockMachine &dockMachineOf(const AIState &s)
{
	return static_cast<AIDockMachine &>(s.machine());
}

DockUpdateInterface *dockOf(Object *goal)
{
	return goal ? goal->getDockUpdateInterface() : nullptr; // RW 0x68C383
}

// RW 0x94A949
bool ableToAdvance(const AIState &s, std::uintptr_t)
{
	Object *goal = s.machine().goalObject();
	DockUpdateInterface *dock = dockOf(goal);
	return dock && dock->isClearToAdvance(s.owner(), dockMachineOf(s).approachPosition());
}

// the AIInternalMoveToState the move states of the dock machine derive from (RW 0x740C39: the goal + 0x20, adjusts destination + 0x48 = 1)
class AIDockMoveState : public AIState
{
public:
	AIDockMoveState(AIStateMachine &m, const char *name) : AIState(m, name) {}
	void onTemporaryStateEnded() override // RW 0x740C97
	{
		if (m_move)
		{
			m_move->clearWaitingForPath();
		}
	}
	void crc(StateHasher &h) const override
	{
		h.addFloat(m_goal.x);
		h.addFloat(m_goal.y);
		h.addFloat(m_goal.z);
		h.addBool(m_adjusts);
		h.addBool(m_move != nullptr);
		if (m_move)
		{
			m_move->crc(h);
		}
	}
	// version 1 (RW 0x743395: the version, then the internal move's xfer RW 0x741923)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		AIXfer::coord(x, m_goal);
		x.xferBool(&m_adjusts);
		xferMove(x, ai().mover(), m_move);
	}

protected:
	StateReturnType enterMove() // RW 0x74DADA
	{
		m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, m_adjusts);
		return m_move->onEnter();
	}
	StateReturnType updateMove() // RW 0x748E46
	{
		if (!m_move)
		{
			return STATE_FAILURE;
		}
		m_move->setGoal(m_goal);
		return m_move->update();
	}
	void exitMove() { ::exitMove(ai().mover(), m_move, m_goal); } // RW 0x748D8A

	Coord3D m_goal{ 0.0f, 0.0f, 0.0f }; ///< + 0x20
	bool m_adjusts = true;               ///< + 0x48
	std::unique_ptr<AIMoveToState> m_move;
};

// APPROACH (RW 0x94B040) and ADVANCE_POSITION (RW 0x94B063): onEnter RW 0x94A986 / 0x94AAC4, update RW 0x94AB40, onExit RW 0x94AB5C
class AIDockApproachState : public AIDockMoveState
{
public:
	AIDockApproachState(AIStateMachine &m, bool advance) : AIDockMoveState(m, advance ? "AIDockAdvancePositionState" : "AIDockApproachState"), m_advance(advance) {}
	StateReturnType onEnter() override
	{
		Object *goal = machine().goalObject();
		DockUpdateInterface *dock = dockOf(goal);
		if (!dock)
		{
			return STATE_FAILURE;
		}
		if (!dock->isDockOpen())
		{
			dock->cancelDock(owner());
			return STATE_FAILURE;
		}
		int &index = dockMachineOf(*this).approachPositionRef();
		const bool ok = m_advance ? dock->advanceApproachPosition(owner(), &m_goal, &index) : dock->reserveApproachPosition(owner(), &m_goal, &index);
		if (!ok)
		{
			return STATE_FAILURE;
		}
		ai().mover().ignoreObstacle(PATHFIND_INVALID_ID); // RW 0x66831A(0)
		m_adjusts = true;
		return enterMove();
	}
	StateReturnType update() override
	{
		if (!machine().goalObject())
		{
			return STATE_FAILURE;
		}
		return updateMove();
	}
	void onExit(StateExitType status) override
	{
		Object *goal = machine().goalObject();
		if (DockUpdateInterface *dock = dockOf(goal))
		{
			if (status == EXIT_RESET || !dock->isDockOpen())
			{
				dock->cancelDock(owner());
			}
			else
			{
				dock->onApproachReached(owner());
			}
		}
		exitMove();
	}

private:
	bool m_advance;
};

// WAIT_FOR_CLEARANCE (RW 0x94A8E8): onEnter RW 0x94AA02, update RW 0x94AA10, onExit RW 0x94AA83
class AIDockWaitForClearanceState : public AIState
{
public:
	explicit AIDockWaitForClearanceState(AIStateMachine &m) : AIState(m, "AIDockWaitForClearanceState") {}
	StateReturnType onEnter() override
	{
		m_enterFrame = owner().logic().getFrame();
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		Object *goal = machine().goalObject();
		DockUpdateInterface *dock = dockOf(goal);
		if (!dock)
		{
			return STATE_FAILURE;
		}
		if (!dock->isDockOpen())
		{
			dock->cancelDock(owner());
			return STATE_FAILURE;
		}
		if (dock->isClearToEnter(owner()))
		{
			return STATE_SUCCESS;
		}
		return m_enterFrame + kWaitTimeoutFrames < owner().logic().getFrame() ? STATE_FAILURE : STATE_CONTINUE;
	}
	void onExit(StateExitType status) override
	{
		if (DockUpdateInterface *dock = dockOf(machine().goalObject()))
		{
			if (!dock->isDockOpen() || status == EXIT_RESET)
			{
				dock->cancelDock(owner());
			}
		}
	}
	void crc(StateHasher &h) const override { h.addU32(m_enterFrame); }
	// RW 0x94B306: version 1, the enter frame
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_enterFrame);
	}

private:
	unsigned m_enterFrame = 0; ///< + 0x20
};

// MOVE_TO_ENTRY (RW 0x94B080) / MOVE_TO_DOCK (RW 0x94B09D) / MOVE_TO_EXIT (RW 0x94B0D7)
class AIDockMoveToPointState : public AIDockMoveState
{
public:
	enum Kind
	{
		ENTRY,
		DOCK,
		EXIT
	};
	AIDockMoveToPointState(AIStateMachine &m, Kind k)
		: AIDockMoveState(m, k == ENTRY ? "AIDockMoveToEntryState" : k == DOCK ? "AIDockMoveToDockState" : "AIDockMoveToExitState")
		, m_kind(k)
	{
	}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;

private:
	void point(DockUpdateInterface &dock)
	{
		switch (m_kind)
		{
		case ENTRY:
			dock.getEnterPosition(owner(), &m_goal); // + 0x14
			break;
		case DOCK:
			dock.getDockPosition(owner(), &m_goal); // + 0x18
			break;
		case EXIT:
			dock.getExitPosition(owner(), &m_goal); // + 0x1C
			break;
		}
	}
	Kind m_kind;
};

// RW 0x94ABB7 / 0x94ACD3 / 0x94AECD
StateReturnType AIDockMoveToPointState::onEnter()
{
	Object *goal = machine().goalObject();
	DockUpdateInterface *dock = dockOf(goal);
	if (!dock)
	{
		return STATE_FAILURE;
	}
	if (m_kind != EXIT && !dock->isDockOpen())
	{
		dock->cancelDock(owner());
		return STATE_FAILURE;
	}
	m_adjusts = true;
	if (m_kind == ENTRY)
	{
		if (dock->isAllowPassthroughType())
		{
			ai().mover().ignoreObstacle(goal->getID());
		}
		point(*dock);
		dockMachineOf(*this).setApproachPosition(-1);
	}
	else
	{
		point(*dock);
		if (dock->isAllowPassthroughType())
		{
			ai().mover().ignoreObstacle(goal->getID());
			m_adjusts = false; // RW 0x94AD3C / 0x94AF1E ("CritterDesync: setAdjustDestination(FALSE)")
		}
		if (m_kind == DOCK)
		{
			machine().lock(); // + 0x38 = 1
		}
	}
	return enterMove();
}

// RW 0x94AC40 / 0x94AD7D / 0x94AF5A: the goal is the dock's point again before the internal move's update
StateReturnType AIDockMoveToPointState::update()
{
	Object *goal = machine().goalObject();
	DockUpdateInterface *dock = dockOf(goal);
	if (!dock)
	{
		return STATE_FAILURE;
	}
	if (m_kind == DOCK && !dock->isDockOpen())
	{
		return STATE_FAILURE; // RW 0x94AD9B: no cancel
	}
	point(*dock);
	return updateMove();
}

// RW 0x94AC78 / 0x94ADC5 / 0x94AF92
void AIDockMoveToPointState::onExit(StateExitType status)
{
	Object *goal = machine().goalObject();
	if (DockUpdateInterface *dock = dockOf(goal))
	{
		if (m_kind == EXIT)
		{
			dock->onExitReached(owner());
		}
		else if (status == EXIT_RESET || !dock->isDockOpen())
		{
			dock->cancelDock(owner());
		}
		else if (m_kind == ENTRY)
		{
			dock->onEnterReached(owner());
		}
		else
		{
			dock->onDockReached(owner());
		}
	}
	if (m_kind != ENTRY)
	{
		machine().unlock(); // + 0x38 = 0
	}
	exitMove();
}

// PROCESS_DOCK (RW 0x94AE27): onEnter RW 0x94AE99, update RW 0x94B3E9, onExit RW 0x94AEC3
class AIDockProcessDockState : public AIState
{
public:
	explicit AIDockProcessDockState(AIStateMachine &m) : AIState(m, "AIDockProcessDockState") {}
	StateReturnType onEnter() override
	{
		if (!dockOf(machine().goalObject()))
		{
			return STATE_FAILURE;
		}
		setNextDockActionFrame();
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		Object *goal = machine().goalObject();
		DockUpdateInterface *dock = dockOf(goal);
		if (!dock)
		{
			return STATE_FAILURE;
		}
		if (owner().logic().getFrame() < m_nextDockActionFrame)
		{
			return STATE_CONTINUE;
		}
		setNextDockActionFrame();
		Object *drone = findMyDrone();
		if (!dock->isDockOpen() || !dock->action(owner(), drone)) // + 0x30
		{
			return STATE_SUCCESS;
		}
		return STATE_CONTINUE;
	}
	void onExit(StateExitType) override { machine().unlock(); }
	void crc(StateHasher &h) const override
	{
		h.addU32(m_nextDockActionFrame);
		h.addU32(m_droneID);
	}
	// RW 0x94B325: version 2, the drone id, the next action frame
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 2 };
		x.xferVersion(v);
		AIXfer::objectId(x, m_droneID, "dock process: the drone");
		if (v.current >= 2)
		{
			x.xferUnsignedInt(&m_nextDockActionFrame);
		}
	}

private:
	// RW 0x94AE52
	void setNextDockActionFrame()
	{
		const unsigned now = owner().logic().getFrame();
		const SupplyTruckAIInterface *sup = ai().getSupplyTruckAIInterface(); // AI vslot 0x17C
		m_nextDockActionFrame = sup ? now + sup->getActionDelayForDock(machine().goalObject()) : now;
	}
	// RW 0x94B394: the cached drone, else the first DRONE (template + 0x111 bit 1) of the owner's player whose producer is the owner (callback RW 0x94B366)
	Object *findMyDrone()
	{
		GameLogic &logic = owner().logic();
		if (Object *d = logic.findObjectByID(m_droneID))
		{
			return d;
		}
		const Player *p = owner().getControllingPlayer();
		if (!p)
		{
			return nullptr;
		}
		static const int drone = ObjectTemplateInfoBuilder::kindOfIndex("DRONE");
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject()) // the player's objects (RW 0x6ABABD): the logic list in order (S-482)
		{
			if (o->getControllingPlayer() == p && drone >= 0 && o->isKindOf((unsigned)drone) && o->getProducerID() == owner().getID())
			{
				m_droneID = o->getID();
				return o;
			}
		}
		return nullptr;
	}
	unsigned m_nextDockActionFrame = 0; ///< + 0x20
	ObjectID m_droneID = INVALID_ID;    ///< + 0x24
};

// MOVE_TO_RALLY (RW 0x94B0BA): onEnter RW 0x94AFCF; update / onExit the internal move's (RW 0x749497 / 0x74933F)
class AIDockMoveToRallyState : public AIDockMoveState
{
public:
	explicit AIDockMoveToRallyState(AIStateMachine &m) : AIDockMoveState(m, "AIDockMoveToRallyState") {}
	StateReturnType onEnter() override
	{
		Object *goal = machine().goalObject();
		DockUpdateInterface *dock = dockOf(goal);
		if (!dock)
		{
			return STATE_FAILURE;
		}
		if (dock->isRallyPointAfterDockType())
		{
			if (ExitInterface *exit = goal->getObjectExitInterface()) // RW 0x68BB14
			{
				if (const Coord3D *rally = exit->getRallyPoint()) // exit vslot 0x20
				{
					m_goal = *rally;
					m_adjusts = true;
					return enterMove();
				}
			}
		}
		return STATE_SUCCESS;
	}
	StateReturnType update() override { return updateMove(); }
	void onExit(StateExitType) override { exitMove(); }
};
} // namespace

// ---- the AI command ------------------------------------------------------------------------------------------------------------
// RW 0x66C609 aiDock (AI command 0x18) -> AI vslot 0xCC privateDock
void AIUpdateInterface::aiDock(Object *dock, CommandSourceType source)
{
	if (!acceptCommand(source, 0x18))
	{
		return; // RW 0x667174: aiDoCommand's gate
	}
	privateDock(dock, source);
}

// RW 0x66461F
void AIUpdateInterface::privateDock(Object *dock, CommandSourceType source)
{
	if (!GarrisonRules::isMobile(*getObject()))
	{
		return;
	}
	m_machine->clear();
	m_machine->setGoalObject(dock ? dock->getID() : INVALID_ID); // vslot 0x38
	setLastCommandSource(source);
	m_machine->setState(AI_DOCK);
	wakeUpNow();
}

// ---- AIDockMachine ------------------------------------------------------------------------------------------------------------
// RW 0x94B0F4
AIDockMachine::AIDockMachine(AIUpdateInterface &owner)
	: AIStateMachine(owner, "AIDockMachine")
{
	defineState(APPROACH, std::make_unique<AIDockApproachState>(*this, false), WAIT_FOR_CLEARANCE, 0x270Fu);
	defineState(WAIT_FOR_CLEARANCE, std::make_unique<AIDockWaitForClearanceState>(*this), MOVE_TO_ENTRY, 0x270Fu,
		{ AIStateCondition{ ableToAdvance, ADVANCE_POSITION, 0 } }); // RW 0xC80C78
	defineState(ADVANCE_POSITION, std::make_unique<AIDockApproachState>(*this, true), WAIT_FOR_CLEARANCE, 0x270Fu);
	defineState(MOVE_TO_ENTRY, std::make_unique<AIDockMoveToPointState>(*this, AIDockMoveToPointState::ENTRY), MOVE_TO_DOCK, MOVE_TO_EXIT);
	defineState(MOVE_TO_DOCK, std::make_unique<AIDockMoveToPointState>(*this, AIDockMoveToPointState::DOCK), PROCESS_DOCK, MOVE_TO_EXIT);
	defineState(PROCESS_DOCK, std::make_unique<AIDockProcessDockState>(*this), MOVE_TO_EXIT, MOVE_TO_EXIT);
	defineState(MOVE_TO_EXIT, std::make_unique<AIDockMoveToPointState>(*this, AIDockMoveToPointState::EXIT), MOVE_TO_RALLY, 0x270Fu);
	defineState(MOVE_TO_RALLY, std::make_unique<AIDockMoveToRallyState>(*this), 0x270Eu, 0x270Fu);
	m_approachPosition = -1;
}

// RW 0x94A920
void AIDockMachine::haltDock()
{
	if (DockUpdateInterface *dock = dockOf(goalObject()))
	{
		dock->cancelDock(*owner().getObject());
	}
	haltWithoutExit(); // RW 0x8DB758
}

// RW 0x94B2E1
void AIDockMachine::xferDock(Xfer &x)
{
	AIStateMachine::xfer(x);
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferInt(&m_approachPosition);
}

void AIDockMachine::crcDock(StateHasher &h) const
{
	AIStateMachine::crc(h);
	h.addI32(m_approachPosition);
}

// ---- AIDockState ------------------------------------------------------------------------------------------------------------
// RW 0x7430B1
StateReturnType AIDockState::onEnter()
{
	Object *goal = machine().goalObject(); // RW 0x8DBACE
	if (!goal || !dockOf(goal))
	{
		return STATE_FAILURE;
	}
	ai().mover().ignoreObstacle(goal->getID()); // RW 0x66831A(goal)
	m_dockMachine = std::make_unique<AIDockMachine>(ai());
	m_dockMachine->setGoalObject(goal->getID()); // slot 0x38 (RW 0x8DB7E9: the id and the object's position)
	m_dockMachine->setGoalPosition(*goal->getPosition());
	return m_dockMachine->initDefaultState();
}

// RW 0x743326
StateReturnType AIDockState::update()
{
	ai().setCanPathThroughUnits(true); // AI + 0x3BA = 1
	if (!m_dockMachine)
	{
		return STATE_FAILURE;
	}
	StateReturnType r = m_dockMachine->updateStateMachine();
	if (IS_STATE_SLEEP(r))
	{
		r = STATE_CONTINUE;
	}
	return r;
}

// RW 0x74313F
void AIDockState::onExit(StateExitType)
{
	if (m_dockMachine)
	{
		m_dockMachine->haltDock(); // slot 0x3C
		m_dockMachine.reset();
	}
	ai().setCanPathThroughUnits(false);
	ai().mover().ignoreObstacle(PATHFIND_INVALID_ID);
}

void AIDockState::crc(StateHasher &h) const
{
	h.addBool(m_unusedFlag);
	h.addBool(m_dockMachine != nullptr);
	if (m_dockMachine)
	{
		m_dockMachine->crcDock(h);
	}
}

// RW 0x743011 (version 1): whether the machine exists, the machine, the byte
void AIDockState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	bool has = m_dockMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_dockMachine)
		{
			m_dockMachine->xferDock(x);
		}
	}
	else
	{
		std::unique_ptr<AIDockMachine> loaded; // made and loaded complete, then attached
		if (has)
		{
			loaded = std::make_unique<AIDockMachine>(ai());
			loaded->xferDock(x);
		}
		m_dockMachine = std::move(loaded);
	}
	x.xferBool(&m_unusedFlag);
}
