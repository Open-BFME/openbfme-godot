// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (AIStates.cpp AIFollowPathState).
// See GameLogic/AI/AIFollowPathAsTeam.h.

#include "GameLogic/AI/AIFollowPathAsTeam.h"

#include "Common/StateHash.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const float kPi = 3.14159274f;
const float kTwoPi = 6.28318548f;
const float kFacedAngle = 0.31415927f; // RW 0xC28A34
const float kNearPoint = 100.0f;       // RW 0xBD88D8 (squared: 10)
const float kExtraAfterNext = 40.0f;   // RW 0xBDD28C (4 * PATHFIND_CELL_SIZE_F)

// RW 0x644FD0
float normalizeAngle(float a)
{
	while (a > kPi)
	{
		a = SimMath::subf32(a, kTwoPi);
	}
	while (a <= -kPi)
	{
		a = SimMath::addf32(a, kTwoPi);
	}
	return a;
}

// RW 0x4B3D8D (Object::GetRelativeAngle): the angle from the object's facing to the point, in (-pi, pi]
float relativeAngle(const Object &obj, const Coord3D &p)
{
	float dx = SimMath::subf32(p.x, obj.getPosition()->x), dy = SimMath::subf32(p.y, obj.getPosition()->y);
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(dx, dy)));
	float angle = 0.0f;
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		dx = SimMath::mulf32(inv, dx);
		dy = SimMath::mulf32(inv, dy);
		const float dirX = SimMath::cosf32(obj.getOrientation()), dirY = SimMath::sinf32(obj.getOrientation()); // RW 0x70B9E0
		float dot = SimMath::addf32(SimMath::mulf32(dirX, dx), SimMath::mulf32(dirY, dy));
		dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);
		angle = (float)SimMath::acosDet((double)dot);
		if (SimMath::subf32(SimMath::mulf32(dirX, dy), SimMath::mulf32(dirY, dx)) < 0.0f)
		{
			angle = SimMath::subf32(0.0f, angle);
		}
	}
	return normalizeAngle(angle);
}

// owner -> point . point -> next < 0: the point is behind the unit on the way to the next one
bool pointBehind(const Object &obj, const Coord3D &p, const Coord3D &next)
{
	const float fx = SimMath::subf32(p.x, obj.getPosition()->x), fy = SimMath::subf32(p.y, obj.getPosition()->y);
	const float nx = SimMath::subf32(next.x, p.x), ny = SimMath::subf32(next.y, p.y);
	return SimMath::addf32(SimMath::mulf32(nx, fx), SimMath::mulf32(ny, fy)) < 0.0f;
}
} // namespace

const char *AIFollowPathAsTeamState::stopLine()
{
	return "[S-4463] the formation march's states 54 and 61 (AIFollowPathAsTeamState, lanes BACKLOG-8 / PORT-13: commands 0x24 / 0x25, RW 0x74FD39 / 0x74FF49) "
		   "run; NOT ported: the attack-move sub-machine's crate state 0x27 and AI + 0x3C7, AI + 0x164 (cleared before each point), the wait for the others at "
		   "the first points (AIData WaitForOthers, No in retail), computePath's outside-the-map quick path; INFERENCE: AI + 0x44 is the desired speed, the "
		   "internal move is the port's AIMoveToState";
}

AIFollowPathAsTeamState::AIFollowPathAsTeamState(AIStateMachine &m, bool isAttack)
	: AIState(m, "AIFollowPathAsTeamState")
	, m_isAttack(isAttack)
{
}

AIFollowPathAsTeamState::~AIFollowPathAsTeamState() = default;

std::unique_ptr<AIStateMachine> AIFollowPathAsTeamState::makeAttackMachine()
{
	// RW 0x74E083: idle 0 (HuntIdleState), crate 0x27 (not ported, S-1189), attack 10 -> AI_ATTACK_OBJECT; each ends in idle
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(ai(), "AIAttackMoveStateMachine");
	m->defineState(AI_IDLE, makeHuntIdleState(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m, false, true, false), AI_IDLE, AI_IDLE);
	return m;
}

bool AIFollowPathAsTeamState::attackMachineIdle() const
{
	const AIState *cur = m_attackMachine ? m_attackMachine->currentState() : nullptr;
	return !cur || cur->isIdle();
}

bool AIFollowPathAsTeamState::isActive() const
{
	// RW 0x7427EA (vtable slot 0x24): + 0x5C != null and its current state is not AI_IDLE
	return m_attackMachine && !attackMachineIdle();
}

StateReturnType AIFollowPathAsTeamState::startMove(bool adjusts)
{
	AIUpdateInterface &a = ai();
	if (m_move)
	{
		m_move->onExit();
	}
	m_move = std::make_unique<AIMoveToState>(a.mover(), m_goal, adjusts);
	return m_move->onEnter();
}

StateReturnType AIFollowPathAsTeamState::onEnter()
{
	// RW 0x74FD39
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	obj.logic().noteStop(stopLine());
	if (m_isAttack)
	{
		if (!m_attackMachine)
		{
			m_attackMachine = makeAttackMachine();
		}
		else
		{
			m_attackMachine->clear(); // vslot 0x14
		}
		m_attackMachine->setState(AI_IDLE); // RW 0x74FD5A .. 0x74FD67
	}
	m_savedCommandSource = a.lastCommandSource(); // RW 0x74FD6D .. 0x74FD73 (+ 0x58)
	m_index = 0;
	m_retries = 5;
	m_atPoint = false;
	m_turning = false;
	m_steppedBack = false;
	const Coord3D *pos = machine().goalPathPosition(0);
	if (!pos)
	{
		return STATE_FAILURE;
	}
	m_angle = a.teamPathAngle();
	m_goal = *pos;
	const Coord3D *next = machine().goalPathPosition(1);
	m_adjustFinal = true;
	while (next && pointBehind(obj, m_goal, *next))
	{
		++m_index;
		m_goal = *next;
		next = machine().goalPathPosition(m_index + 1);
	}
	a.setCurrentGoalPathIndex(m_index);
	const StateReturnType ret = startMove(next != nullptr);
	if (next)
	{
		// the first point of the path (`pos`), as RW computes it (BFME2 decomp: `delta = nextPos - pos`)
		float offset = SimMath::length2d(SimMath::subf32(next->x, pos->x), SimMath::subf32(next->y, pos->y));
		if (machine().goalPathPosition(m_index + 2))
		{
			offset = SimMath::addf32(offset, kExtraAfterNext);
		}
		a.setPathExtraDistance(offset);
		m_move->setAdjustsDestination(false);
	}
	else
	{
		m_move->setAdjustsDestination(m_adjustFinal);
		a.setPathExtraDistance(0.0f);
	}
	return ret;
}

StateReturnType AIFollowPathAsTeamState::update()
{
	// RW 0x74FF49
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	bool retarget = false;
	bool justFinishedAttack = false;
	if (m_attackMachine)
	{
		if (!attackMachineIdle())
		{
			// RW 0x74FF74 .. 0x74FFDC: sync a changed parent goal object, step the attack machine, and stay in combat until it returns to AI_IDLE
			if (Object *goal = machine().goalObject(); goal && goal != m_attackMachine->goalObject())
			{
				m_attackMachine->setGoalObject(goal->getID());
			}
			m_attackMachine->updateStateMachine();
			if (!attackMachineIdle())
			{
				return STATE_CONTINUE;
			}
			a.setLastCommandSource(m_savedCommandSource); // RW 0x74FFCD -> 0x6627F0
			retarget = true;
			justFinishedAttack = true;
		}
		// RW 0x75002F .. 0x750072: crate pickup (RW 0x66810B -> 0x27) is not ported; mood target scan interrupts the march into AI_ATTACK_OBJECT
		if (Object *victim = a.getNextMoodTarget(!retarget, false))
		{
			a.mover().endingMove(); // RW 0x750048 -> 0x6627CB
			m_attackMachine->setGoalObject(victim->getID());
			m_attackMachine->setState(AI_ATTACK_OBJECT);
			a.setLastCommandSource(CMD_FROM_AI); // RW 0x750060
			return STATE_CONTINUE;
		}
	}
	machine().setGoalPosition(m_goal); // RW 0x661EFE
	if (m_turning)
	{
		const float rel = normalizeAngle(SimMath::subf32(m_angle, obj.getOrientation()));
		if (kFacedAngle > rel)
		{
			obj.setOrientation(m_angle); // RW 0x70C31E
			return STATE_SUCCESS;
		}
		a.setLocomotorGoalOrientation(m_angle); // AI vslot 0x21C
		return STATE_CONTINUE;
	}
	StateReturnType status = STATE_SUCCESS;
	if (!m_atPoint)
	{
		status = m_move ? m_move->update() : STATE_FAILURE; // RW 0x748E46
		if (status == STATE_FAILURE)
		{
			if (m_retries > 0)
			{
				--m_retries; // RW 0x7502C4: the failure is kept; the point is asked again below
			}
			else
			{
				status = STATE_SUCCESS;
			}
		}
		if (status != STATE_SUCCESS && status != STATE_FAILURE && !justFinishedAttack)
		{
			return status; // RW 0x7502E9: when justFinishedAttack is set, fall through to RW 0x7500F0 to skip passed points and repath to m_index
		}
	}
	else
	{
		static const int moving = AIUpdateInterface::modelConditionBit("MOVING");
		obj.setModelConditionState(moving, false); // RW 0x7500D6
	}
	if (!m_atPoint && status == STATE_SUCCESS)
	{
		// arrived (RW 0x750103 ..)
		if (m_steppedBack && m_index > 0)
		{
			--m_index;
		}
		const Coord3D *next = machine().goalPathPosition(m_index + 1);
		if (next)
		{
			if (a.canFollowTeamPath())
			{
				Locomotor *l = a.curLocomotor();
				a.setDesiredSpeed(l ? l->getMaxSpeedForCondition(a.locomotorHost()) : 0.0f); // RW 0x74280F -> 0x6624C9, AI + 0x44 (INFERENCE: the desired speed)
			}
			a.setLocomotorGoalOrientation(normalizeAngle(SimMath::addf32(obj.getOrientation(), relativeAngle(obj, *next))));
		}
		++m_index;
		a.setCurrentGoalPathIndex(m_index);
		m_atPoint = next != nullptr;
	}
	// canMoveToNextFormationPathPoint (RW 0x74B85F) without AIData WaitForOthers (No in retail) lets the unit go at once (S-4463)
	m_atPoint = false;
	m_steppedBack = false;
	const Coord3D *p = machine().goalPathPosition(m_index);
	const Coord3D pos = *obj.getPosition();
	bool skip = true;
	while (p && skip)
	{
		const float dy = SimMath::subf32(p->y, pos.y), dx = SimMath::subf32(p->x, pos.x);
		skip = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)) < kNearPoint;
		const Coord3D *q = machine().goalPathPosition(m_index + 1);
		if (q && pointBehind(obj, *p, *q))
		{
			skip = true;
		}
		if (skip)
		{
			++m_index;
			p = q;
		}
	}
	if (!p)
	{
		m_turning = true;
		a.setLocomotorGoalOrientation(m_angle);
		return STATE_CONTINUE;
	}
	a.mover().startingMove(); // RW 0x6627AF
	m_goal = *p;
	const Coord3D *q = machine().goalPathPosition(m_index + 1);
	bool adjusts = false;
	if (q)
	{
		float offset = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(SimMath::subf32(q->x, p->x), SimMath::subf32(q->y, p->y)))); // RW 0x4032DD
		if (machine().goalPathPosition(m_index + 2))
		{
			offset = SimMath::addf32(offset, kExtraAfterNext);
		}
		a.setPathExtraDistance(offset);
	}
	else
	{
		a.setPathExtraDistance(0.0f);
		// RW 0x75040D: + 0x54 and (+ 0x55 or AI vslot 0x224); + 0x55 is cleared at RW 0x74FCD7 and nothing in this state sets it
		adjusts = m_adjustFinal && a.canFollowTeamPath();
		if (adjusts && a.world().mapReady())
		{
			const PathfindLocomotorInfo info = a.locomotorInfo();
			if (!a.world().pathfinder().adjustDestination(a.adapter(), info, &m_goal, nullptr))
			{
				if (--m_retries <= 0)
				{
					return STATE_FAILURE;
				}
				m_goal.x = SimMath::mulf32(SimMath::addf32(pos.x, m_goal.x), 0.5f);
				m_goal.y = SimMath::mulf32(SimMath::addf32(pos.y, m_goal.y), 0.5f);
				m_goal.z = SimMath::mulf32(SimMath::addf32(pos.z, m_goal.z), 0.5f);
				m_steppedBack = true;
			}
			a.world().pathfinder().updateGoal(a.adapter(), &m_goal, a.world().pathfinder().getLayerAt(m_goal.x, m_goal.y)); // RW 0x68B3AB (lane WALL-1: the goal's layer)
		}
	}
	startMove(adjusts); // the internal move's computePath (vslot 0x44)
	return STATE_CONTINUE;
}

void AIFollowPathAsTeamState::onExit(StateExitType)
{
	// RW 0x74B800
	if (m_attackMachine)
	{
		m_attackMachine->setState(AI_IDLE);
	}
	AIUpdateInterface &a = ai();
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
	a.setCanPathThroughUnits(false);
	a.setDesiredSpeed(AI_FAST_SPEED);
	a.setCurrentGoalPathIndex(-1);
}

void AIFollowPathAsTeamState::onTemporaryStateEnded()
{
	if (m_move)
	{
		m_move->clearWaitingForPath();
	}
}

void AIFollowPathAsTeamState::crc(StateHasher &h) const
{
	h.addI32(m_index);
	h.addI32(m_retries);
	h.addBool(m_adjustFinal);
	h.addBool(m_atPoint);
	h.addBool(m_steppedBack);
	h.addFloat(m_angle);
	h.addBool(m_turning);
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addFloat(m_goal.z);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
	if (m_isAttack)
	{
		h.addI32((int)m_savedCommandSource);
		h.addBool(m_attackMachine != nullptr);
		if (m_attackMachine)
		{
			m_attackMachine->crc(h);
		}
	}
}

// merge queue q19 (SAVE-4c made AIState::xfer pure; BACKLOG-8 added state 54, PORT-13 added state 61's RW 0x742721 fields + 0x58 / + 0x5C)
void AIFollowPathAsTeamState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferInt(&m_index);
	x.xferInt(&m_retries);
	x.xferBool(&m_adjustFinal);
	x.xferBool(&m_atPoint);
	x.xferBool(&m_steppedBack);
	x.xferReal(&m_angle);
	x.xferBool(&m_turning);
	AIXfer::coord(x, m_goal);
	if (x.isLoading())
	{
		AIXfer::inRange(m_index, 0, 1 << 20, "ai follow path as team: the index");
		AIXfer::inRange(m_retries, 0, 5, "ai follow path as team: the retries");
	}
	xferMove(x, ai().mover(), m_move);
	if (m_isAttack)
	{
		AIXfer::enumIn(x, m_savedCommandSource, (int)CMD_FROM_PLAYER, (int)CMD_FROM_AI, "ai follow path as team: the command source");
		bool has = m_attackMachine != nullptr;
		x.xferBool(&has);
		if (!x.isLoading())
		{
			if (m_attackMachine)
			{
				m_attackMachine->xfer(x);
			}
			return;
		}
		std::unique_ptr<AIStateMachine> loaded;
		if (has)
		{
			loaded = makeAttackMachine();
			loaded->xfer(x);
		}
		m_attackMachine = std::move(loaded);
	}
}
