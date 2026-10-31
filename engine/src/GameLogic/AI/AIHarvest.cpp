// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIHarvest.h for the target facts.

#include "GameLogic/AI/AIHarvest.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const char *const kHarvestFile = "AIHarvest.cpp"; // RW 0xC80DB0 (the full build path in retail)
const float kTopplePullMin = 0.5f;                // RW 0xBD869C
const float kTopplePullMax = 5.0f;                // RW 0xBDAE58
const float kTopplePullStep = 0.5f;               // RW 0xBD869C
const int kTopplePullTries = 5;                   // RW 0x94B667

SupplyTruckAIInterface *supplyOf(AIState &s)
{
	return s.ai().getSupplyTruckAIInterface(); // AI vslot 0x17C
}

// Object + 0x12C bit 0 / bit 1 with RW 0x68B53C when it changes
void setHarvestCondition(Object &obj, const char *name, bool on)
{
	const int bit = CombatNames::modelCondition(name);
	if (obj.testModelCondition(bit) != on)
	{
		obj.setModelConditionState(bit, on);
	}
}

TerrainTrees::Record *closestTree(GameLogic &logic, const Coord3D &where, float range, int mode)
{
	return logic.trees().findClosestTree(where, range, false, mode); // RW 0x67F52B(where, range, 0, mode)
}

// state 0: RW 0x94B704 (an AIInternalMoveToState with its own onEnter)
class AIHarvestMoveToState : public AIState
{
public:
	explicit AIHarvestMoveToState(AIStateMachine &m) : AIState(m, "AIHarvestMoveToState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override { return m_move ? m_move->update() : STATE_FAILURE; } // RW 0x749497 -> 0x748E46
	void onExit(StateExitType) override { exitMove(ai().mover(), m_move, m_goal); }       // RW 0x74933F -> 0x748D8A
	void onTemporaryStateEnded() override                                                  // RW 0x740C97
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
		h.addBool(m_move != nullptr);
		if (m_move)
		{
			m_move->crc(h);
		}
	}
	// retail's xfer is empty (RW 0x9F3A3C); OpenBFME keeps the internal move so a loaded game goes on the same way (S-4220's own layout)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferReal(&m_goal.x);
		x.xferReal(&m_goal.y);
		x.xferReal(&m_goal.z);
		xferMove(x, ai().mover(), m_move);
	}

private:
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f }; ///< + 0x20 (the internal move's goal)
	std::unique_ptr<AIMoveToState> m_move;
};

// RW 0x94B4B5
StateReturnType AIHarvestMoveToState::onEnter()
{
	SupplyTruckAIInterface *sup = supplyOf(*this);
	if (!sup)
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	GameLogic &logic = obj.logic();
	const Coord3D goal = machine().goalPosition(); // the harvest machine's goal (+ 0x24)
	TerrainTrees::Record *tree = closestTree(logic, goal, sup->getHarvestActivationRange(), 0);
	if (!tree)
	{
		sup->setForceWantingState(true); // RW 0x94B51F
		return STATE_FAILURE;
	}
	sup->setCurrentTree(tree->index); // slot 0x44 with the record's + 0x10
	m_goal = goal;
	if (tree->toppled)
	{
		// RW 0x94B53D .. 0x94B5A5: the offset along the fall, scaled by one logic draw
		const float r = logic.random().getValueReal(kTopplePullMin, kTopplePullMax, kHarvestFile, 0x5F);
		float oy = SimMath::mulf32(tree->toppleDirection.y, r);
		float ox = SimMath::mulf32(r, tree->toppleDirection.x);
		float oz = SimMath::mulf32(tree->toppleDirection.z, r);
		AIUpdateInterface &a = ai();
		for (int i = 0; i < kTopplePullTries; ++i)
		{
			Coord3D cand{ SimMath::addf32(m_goal.x, ox), SimMath::addf32(oy, m_goal.y), oz }; // RW 0x94B5D1 .. 0x94B5EB: z is the offset's alone
			bool adjusted = false;
			if (a.world().mapReady())
			{
				adjusted = a.world().pathfinder().adjustDestination(a.adapter(), a.locomotorInfo(), &cand, nullptr); // RW 0x6FE456(obj, AI + 0x1CC, &c, 0)
			}
			// RW 0x6EA857 (a wall cell) and RW 0x6E89EE (a bridge): neither layer is in the port's grid (S-161), both answer false
			if (adjusted)
			{
				m_goal = cand;
				break;
			}
			ox = SimMath::mulf32(ox, kTopplePullStep);
			oy = SimMath::mulf32(oy, kTopplePullStep);
			oz = SimMath::mulf32(oz, kTopplePullStep);
		}
	}
	m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, true);
	return m_move->onEnter(); // RW 0x74DADA
}

// state 1: RW 0x94B456
class AIHarvestPrepareSiteState : public AIState
{
public:
	explicit AIHarvestPrepareSiteState(AIStateMachine &m) : AIState(m, "AIHarvestPrepareSiteState") {}
	// RW 0x94B68E
	StateReturnType onEnter() override
	{
		SupplyTruckAIInterface *sup = supplyOf(*this);
		if (!sup)
		{
			return STATE_FAILURE;
		}
		const unsigned now = owner().logic().getFrame();
		m_endFrame = sup->getHarvestPreparationTime() + now;
		return STATE_CONTINUE;
	}
	StateReturnType update() override;
	// RW 0x94B908
	void onExit(StateExitType) override { setHarvestCondition(owner(), "HARVEST_PREPARATION", false); }
	void crc(StateHasher &h) const override { h.addU32(m_endFrame); }
	// RW 0x94B306: version 1, the end frame
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_endFrame);
	}

private:
	unsigned m_endFrame = 0; ///< + 0x20
};

// RW 0x94B819
StateReturnType AIHarvestPrepareSiteState::update()
{
	Object &obj = owner();
	SupplyTruckAIInterface *sup = supplyOf(*this);
	if (sup)
	{
		GameLogic &logic = obj.logic();
		TerrainTrees::Record *tree = closestTree(logic, *obj.getPosition(), sup->getHarvestActivationRange(), 2);
		if (!tree)
		{
			if (!closestTree(logic, machine().goalPosition(), sup->getHarvestActivationRange(), 2))
			{
				sup->setForceWantingState(true);
			}
			return STATE_FAILURE; // back to the move (a tree near the goal) or out of the machine
		}
		sup->setCurrentTree(tree->index);
		setHarvestCondition(obj, "HARVEST_PREPARATION", true);
		if (!tree->toppled)
		{
			if (logic.getFrame() < m_endFrame)
			{
				setHarvestCondition(obj, "HARVEST_PREPARATION", true);
				return STATE_CONTINUE;
			}
			logic.trees().toppleTree(*tree, *obj.getPosition(), nullptr); // RW 0x684BAA(tree, worker + 0x38)
		}
		return STATE_SUCCESS;
	}
	setHarvestCondition(obj, "HARVEST_PREPARATION", true);
	return STATE_CONTINUE;
}

// state 2: RW 0x94B47D
class AIHarvestWorkSiteState : public AIState
{
public:
	explicit AIHarvestWorkSiteState(AIStateMachine &m) : AIState(m, "AIHarvestWorkSiteState") {}
	// RW 0x94B6C9
	StateReturnType onEnter() override
	{
		SupplyTruckAIInterface *sup = supplyOf(*this);
		if (!sup)
		{
			return STATE_FAILURE;
		}
		const unsigned now = owner().logic().getFrame();
		m_endFrame = sup->getHarvestActionTime() + now;
		return STATE_CONTINUE;
	}
	StateReturnType update() override;
	// RW 0x94BA46
	void onExit(StateExitType) override { setHarvestCondition(owner(), "HARVEST_ACTION", false); }
	void crc(StateHasher &h) const override { h.addU32(m_endFrame); }
	void xfer(Xfer &x) override // RW 0x94B306 (shared with the prepare state)
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_endFrame);
	}

private:
	unsigned m_endFrame = 0; ///< + 0x20
};

// RW 0x94B926
StateReturnType AIHarvestWorkSiteState::update()
{
	Object &obj = owner();
	SupplyTruckAIInterface *sup = supplyOf(*this);
	if (sup)
	{
		GameLogic &logic = obj.logic();
		TerrainTrees::Record *tree = closestTree(logic, *obj.getPosition(), sup->getHarvestActivationRange(), 2);
		// TheGameClient vslot 0x80 with the client id (RW 0x67D734): INFERENCE (S-930) true for a record the query found
		if (!tree || tree->index != sup->getCurrentTree() || tree->clientId == 0)
		{
			sup->setForceWantingState(true); // RW 0x94BA3E
			return STATE_FAILURE;
		}
		if (logic.getFrame() >= m_endFrame)
		{
			const int want = sup->getMaxBoxes() - sup->getNumberBoxes(); // RW 0x94B9B1 .. 0x94B9C5
			const int before = tree->value;
			const int taken = logic.trees().harvestTree(*tree, want); // RW 0x67D6E1
			if (taken > 0)
			{
				const int remaining = before - taken;
				for (int i = taken; i != 0; --i)
				{
					sup->gainOneBox(remaining); // slot 0x0C
				}
			}
			return STATE_SUCCESS;
		}
		AIUpdateInterface &a = ai();
		if (Locomotor *loco = a.curLocomotor()) // AI + 0x1F0
		{
			// RW 0x5E6810(worker, tree, 0): the turn towards the tree's position (RW 0x5E60C7), then the working matrix becomes the transform (RW 0x70BA76)
			LocomotorHost &host = a.locomotorHost();
			loco->rotateTowardsWrapper(host, tree->position, nullptr);
			host.setTransform(loco->matrix());
		}
	}
	setHarvestCondition(obj, "HARVEST_ACTION", true);
	return STATE_CONTINUE;
}
} // namespace

std::string harvestStopLine()
{
	return "[S-5200] AI harvest (lane LUMBER1, AIHarvest.cpp RW 0x740F2C / 0x94B727): INFERENCE: TheGameClient's tree check (vslot 0x80, RW 0x67D734) is true "
		   "for a record the logic query found; the wall-cell (RW 0x6EA857) and bridge (RW 0x6E89EE) tests of a toppled tree's approach answer false (S-161)";
}

// ---- the AI command ------------------------------------------------------------------------------------------------------------
// RW 0x771655 aiHarvest (AI command 0x19) -> RW 0x664657 privateHarvest
void AIUpdateInterface::aiHarvest(const Coord3D &pos, CommandSourceType source)
{
	if (!acceptCommand(source, 0x19))
	{
		return; // RW 0x667174: aiDoCommand's gate
	}
	privateHarvest(pos, source);
}

void AIUpdateInterface::privateHarvest(const Coord3D &pos, CommandSourceType source)
{
	if (!GarrisonRules::isMobile(*getObject()))
	{
		return; // RW 0x66465D
	}
	m_machine->clear();             // vslot 0x14
	m_machine->setGoalPosition(pos); // RW 0x661EFE
	setLastCommandSource(source);   // AI + 0x48
	m_machine->setState(AI_HARVEST); // vslot 0x20 with 0x2F
	wakeUpNow();
}

// ---- AIHarvestState ------------------------------------------------------------------------------------------------------------
AIHarvestState::AIHarvestState(AIStateMachine &m)
	: AIState(m, "AIHarvestState")
{
}

AIHarvestState::~AIHarvestState()
{
}

// RW 0x94B727
std::unique_ptr<AIStateMachine> AIHarvestState::makeHarvestMachine()
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(ai(), "AIHarvestMachine");
	m->defineState(HARVEST_MOVE, std::make_unique<AIHarvestMoveToState>(*m), HARVEST_PREPARE, 0x270Fu);
	m->defineState(HARVEST_PREPARE, std::make_unique<AIHarvestPrepareSiteState>(*m), HARVEST_WORK, HARVEST_MOVE);
	m->defineState(HARVEST_WORK, std::make_unique<AIHarvestWorkSiteState>(*m), 0x270Eu, 0x270Fu);
	return m;
}

// RW 0x743287
StateReturnType AIHarvestState::onEnter()
{
	owner().logic().noteStop(harvestStopLine());
	m_harvestMachine = makeHarvestMachine();
	m_harvestMachine->setGoalPosition(machine().goalPosition()); // RW 0x7432C6
	return m_harvestMachine->initDefaultState();                // vslot 0x1C
}

// RW 0x743326
StateReturnType AIHarvestState::update()
{
	ai().setCanPathThroughUnits(true); // AI + 0x3BA = 1
	if (!m_harvestMachine)
	{
		return STATE_FAILURE;
	}
	StateReturnType r = m_harvestMachine->updateStateMachine();
	if (IS_STATE_SLEEP(r))
	{
		r = STATE_CONTINUE;
	}
	return r;
}

// RW 0x7432E0
void AIHarvestState::onExit(StateExitType)
{
	if (m_harvestMachine)
	{
		m_harvestMachine->haltWithoutExit(); // slot 0x3C = RW 0x8DB758: the machine's state does not exit (its model condition and move stay)
		m_harvestMachine.reset();
	}
	ai().setCanPathThroughUnits(false);
}

void AIHarvestState::crc(StateHasher &h) const
{
	h.addBool(m_unusedFlag);
	h.addBool(m_harvestMachine != nullptr);
	if (m_harvestMachine)
	{
		m_harvestMachine->crc(h);
	}
}

// RW 0x7431E7 (version 1): whether the machine exists, the machine, the byte
void AIHarvestState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	bool has = m_harvestMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_harvestMachine)
		{
			m_harvestMachine->xfer(x);
		}
	}
	else
	{
		std::unique_ptr<AIStateMachine> loaded; // made and loaded complete, then attached
		if (has)
		{
			loaded = makeHarvestMachine();
			loaded->xfer(x);
		}
		m_harvestMachine = std::move(loaded);
	}
	x.xferBool(&m_unusedFlag);
}
