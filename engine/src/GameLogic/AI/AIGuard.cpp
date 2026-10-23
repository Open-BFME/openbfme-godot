// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIGuard.h for the sources of every rule.

#include "GameLogic/AI/AIGuard.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/AI/AIXfer.h"

namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
const char *const kGuardFile = "AIGuard.cpp";

const GameLogicSettings &settingsOf(const Object &obj)
{
	const GameLogicSettings &s = obj.logic().settings();
	if (!s.guardRulesLoaded)
	{
		obj.logic().reportError("AIGuard: AIData GuardChaseUnitsDuration / GuardEnemyScanRate / GuardEnemyReturnScanRate were not read [S-3802]");
	}
	return s;
}

bool attackCommonTarget(const Team *t)
{
	return t && t->getPrototype() && t->getPrototype()->getDict().getBool("teamAttackCommonTarget");
}

float distSqr2D(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y);
	return SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
}

// the idle scan's TargetFinder flags (AIUpdateInterface::nextMoodTarget): a horde looks for hordes and lone units, a lone unit for lone units and members;
// measured centre to centre in 3D (review of #73: RW 0x70146A adds the qualifier 0x100 when the point is not the unit's position, Object + 0x38, which skips one of
// RW 0x7006D7's filters, and calls RW 0x7006D7, as RW 0x701443 does, whose partition
// queries RW 0xA39090 / 0xA39340 take distance type 1)
unsigned scanFlags(const Object &obj, const AIUpdateInterface &ai)
{
	unsigned flags = TargetFinder::CENTER_3D;
	if (ai.autoAcquireFlags() & 16u)
	{
		flags |= TargetFinder::ALLOW_STRUCTURES;
	}
	flags |= obj.isKindOf((unsigned)CombatNames::kinds().horde) ? TargetFinder::HORDES_ONLY : TargetFinder::MEMBERS_ONLY;
	return flags;
}

// RW 0x691269 (Object::isAbleToAttack) in part, as AITacticalAI's canAttackAtAll (S-1301): weapons, not NO_ATTACK, UNDER_CONSTRUCTION or SOLD
bool ableToAttack(const Object &o)
{
	static const int sold = CombatNames::status("SOLD");
	return o.getWeapons() && !o.testStatus((unsigned)CombatNames::statuses().noAttack) && !o.isUnderConstruction() && !(sold >= 0 && o.testStatus((unsigned)sold));
}

AIGuardMachine &guardOf(AIState &s)
{
	return static_cast<AIGuardMachine &>(s.machine());
}

// RW 0x949994 (the condition of the return and idle states, -> AIGuardAttackAggressorState)
bool hasAttackedMeAndICanReturnFire(Object &obj)
{
	ActiveBody *body = dynamic_cast<ActiveBody *>(obj.getBodyModule());
	if (!body)
	{
		return false;
	}
	const ObjectID id = body->takeLastAttacker(); // body vslot 0x4C (RW 0x8C3A33)
	if (id == INVALID_ID)
	{
		return false;
	}
	Object *target = obj.logic().findObjectByID(id);
	if (!target || obj.getRelationship(*target) != ENEMIES || target->isEffectivelyDead())
	{
		return false;
	}
	ObjectWeapons *w = obj.getWeapons();
	return ableToAttack(obj) && w && w->canAttackObject(*target, CMD_FROM_AI, false); // RW 0x691269 / 0x68D6A6 (ObjectWeapons::canAttackObject stands in, as for the turret)
}

// ---- the guard machine's states ----------------------------------------------------------------------------------------------------------------

// RW vtable 0xC80830: AIGuardReturnState (an AIInternalMoveToState)
class GuardReturnState : public AIState
{
public:
	explicit GuardReturnState(AIStateMachine &m) : AIState(m, "AIGuardReturnState") {}
	// RW 0x94A571
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		m_nextReturnScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)settingsOf(obj).guardEnemyReturnScanRate, kGuardFile, 0x371);
		AIGuardMachine &g = guardOf(*this);
		m_goal = g.guardPosition();
		// Coord3D::GetLengthEstimate (RW 0x4035AC) of the offset below 5.0 (RW 0xBDAE58): already there. INFERENCE (S-3802): the exact length stands for the
		// estimate (an estimate built from fabs maxima; only the 5.0 threshold reads it)
		const Coord3D &p = *obj.getPosition();
		const float dx = SimMath::subf32(p.x, m_goal.x), dy = SimMath::subf32(p.y, m_goal.y), dz = SimMath::subf32(p.z, m_goal.z);
		if (SimMath::length3d(dx, dy, dz) < 5.0)
		{
			return STATE_SUCCESS;
		}
		ai().mover().destroyPath();
		m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, true); // setAdjustsDestination(true)
		m_move->setFinalAngle(g.guardAngle());                                // the machine's goal angle (+ 0x64)
		return m_move->onEnter();
	}
	// RW 0x94A69E
	StateReturnType update() override
	{
		Object &obj = owner();
		if (SimMath::subf32(ai().mover().locomotorDistanceToGoal(), AIGuardMachine::getStdGuardRange(obj)) < 0.0f)
		{
			const unsigned now = obj.logic().getFrame();
			if (now >= m_nextReturnScanTime)
			{
				m_nextReturnScanTime = now + settingsOf(obj).guardEnemyReturnScanRate;
				if (guardOf(*this).lookForInnerTarget())
				{
					return STATE_FAILURE; // -> the inner state
				}
			}
		}
		return m_move ? m_move->update() : STATE_FAILURE;
	}
	void onExit(StateExitType) override
	{
		if (m_move)
		{
			m_move->onExit();
			m_move.reset();
		}
	}
	void onTemporaryStateEnded() override
	{
		if (m_move)
		{
			m_move->clearWaitingForPath();
		}
	}
	unsigned conditionTransition() override { return hasAttackedMeAndICanReturnFire(owner()) ? (unsigned)AI_GUARD_ATTACK_AGGRESSOR : INVALID_STATE_ID; }
	void crc(StateHasher &h) const override
	{
		h.addU32(m_nextReturnScanTime);
		h.addFloat(m_goal.x);
		h.addFloat(m_goal.y);
		h.addBool(m_move != nullptr);
	}
	// lane SAVE-4c: AIGuardReturnState::xfer RW 0x6DCE1C is (1, 1) with its move; OpenBFME layout (S-4220): the scan frame, the goal, the move
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_nextReturnScanTime);
		AIXfer::coord(x, m_goal);
		xferMove(x, ai().mover(), m_move);
	}

private:
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f };
	unsigned m_nextReturnScanTime = 0; // + 0x4C
};

// RW vtable 0xC806D0: AIGuardIdleState
class GuardIdleState : public AIState
{
public:
	explicit GuardIdleState(AIStateMachine &m) : AIState(m, "AIGuardIdleState") {}
	// RW 0x949630
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		m_nextEnemyScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)settingsOf(obj).guardEnemyScanRate, kGuardFile, 0x3F5);
		return STATE_CONTINUE;
	}
	// RW 0x94A70A (the guarded object's / team's move test has nothing to test for a LOCATION guard; the crate: none in the port)
	StateReturnType update() override
	{
		Object &obj = owner();
		const unsigned now = obj.logic().getFrame();
		if (now < m_nextEnemyScanTime)
		{
			return STATE_SLEEP(m_nextEnemyScanTime - now);
		}
		m_nextEnemyScanTime = now + settingsOf(obj).guardEnemyScanRate;
		if (guardOf(*this).lookForInnerTarget())
		{
			return STATE_SUCCESS; // -> the inner state
		}
		return STATE_SLEEP(m_nextEnemyScanTime - now);
	}
	bool isIdle() const override { return true; } // RW 0x8BD372
	unsigned conditionTransition() override { return hasAttackedMeAndICanReturnFire(owner()) ? (unsigned)AI_GUARD_ATTACK_AGGRESSOR : INVALID_STATE_ID; }
	void crc(StateHasher &h) const override { h.addU32(m_nextEnemyScanTime); }
	void xfer(Xfer &x) override // lane SAVE-4c
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_nextEnemyScanTime);
	}

private:
	unsigned m_nextEnemyScanTime = 0; // + 0x20
};

// the attack the inner, outer and aggressor states run (ZH: new AIAttackState(machine, follow, true, false, &exitConditions))
class GuardAttackHolder
{
protected:
	void startAttack(AIStateMachine &m, bool follow, Object &nemesis)
	{
		m_attack = std::make_unique<AIAttackState>(m, follow, true, false, &m_exit);
		m.setGoalObject(nemesis.getID());
	}
	void stopAttack(StateExitType status)
	{
		if (m_attack)
		{
			m_attack->onExit(status);
			m_attack.reset();
		}
	}
	void crcAttack(StateHasher &h) const
	{
		h.addU32((std::uint32_t)m_exit.conditions);
		h.addFloat(m_exit.center.x);
		h.addFloat(m_exit.center.y);
		h.addFloat(m_exit.radiusSqr);
		h.addU32(m_exit.attackGiveUpFrame);
		h.addBool(m_attack != nullptr);
		if (m_attack)
		{
			m_attack->crc(h);
		}
	}
	// lane SAVE-4c: the exit conditions, then the attack (made again with its `follow` and these conditions, then loaded)
	void xferAttack(Xfer &x, AIStateMachine &m)
	{
		x.xferInt(&m_exit.conditions);
		if (x.isLoading() && (m_exit.conditions & ~7))
		{
			throw XferException("ai guard: the exit conditions 0x" + std::to_string(m_exit.conditions) + " hold bits beyond ExitIfOutsideRadius | ExpiredDuration | NoUnitFound");
		}
		AIXfer::coord(x, m_exit.center);
		x.xferReal(&m_exit.radiusSqr);
		x.xferUnsignedInt(&m_exit.attackGiveUpFrame);
		bool has = m_attack != nullptr;
		bool follow = m_attack ? m_attack->follows() : false;
		x.xferBool(&has);
		x.xferBool(&follow);
		if (!x.isLoading())
		{
			if (m_attack)
			{
				m_attack->xfer(x);
			}
			return;
		}
		std::unique_ptr<AIAttackState> loaded; // made and loaded complete, then attached
		if (has)
		{
			loaded = std::make_unique<AIAttackState>(m, follow, true, false, &m_exit);
			loaded->xfer(x);
		}
		m_attack = std::move(loaded);
	}
	GuardExitConditions m_exit;             // + 0x20
	std::unique_ptr<AIAttackState> m_attack; // + 0x3C
};

// RW vtable 0xC80780: AIGuardInnerState
class GuardInnerState : public AIState, private GuardAttackHolder
{
public:
	explicit GuardInnerState(AIStateMachine &m) : AIState(m, "AIGuardInnerState") {}
	// RW 0x94A048
	StateReturnType onEnter() override
	{
		AIGuardMachine &g = guardOf(*this);
		Object &obj = owner();
		Object *nemesis = obj.logic().findObjectByID(g.nemesis());
		if (!nemesis)
		{
			return STATE_SUCCESS;
		}
		m_exit.center = g.positionToGuard();
		const float range = AIGuardMachine::getStdGuardRange(obj);
		m_exit.radiusSqr = SimMath::mulf32(range, range);
		m_exit.conditions = GuardExitConditions::ATTACK_ExitIfOutsideRadius | GuardExitConditions::ATTACK_ExitIfNoUnitFound;
		startAttack(machine(), false, *nemesis);
		m_deadline = obj.logic().getFrame() + 3u * (unsigned)LOGICFRAMES_PER_SECOND;
		const StateReturnType r = m_attack->onEnter();
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS;
	}
	// RW 0x94A183
	StateReturnType update() override
	{
		if (!m_attack)
		{
			return STATE_SUCCESS;
		}
		const StateReturnType ret = m_attack->update();
		Object &obj = owner();
		Object *goal = machine().goalObject();
		if (!goal || goal->isEffectivelyDead()) // RW: the goal gone or its + 0x438 bit 0 (the dead flag) set
		{
			if (guardOf(*this).lookForInnerTarget())
			{
				onExit(EXIT_NORMAL);
				return onEnter();
			}
		}
		if (goal && goal->isKindOf((unsigned)CombatNames::kinds().structure) && obj.logic().getFrame() >= m_deadline)
		{
			// RW: once a second against a STRUCTURE (template + 0x108 & 0x80) while the current weapon's status is not 4 (computeStatus RW 0x6CAxxx)
			const ObjectWeapons *w = obj.getWeapons();
			if (w && w->currentStatus() != WEAPON_PRE_ATTACK)
			{
				m_deadline = obj.logic().getFrame() + (unsigned)LOGICFRAMES_PER_SECOND;
				if (guardOf(*this).lookForInnerTarget())
				{
					onExit(EXIT_NORMAL);
					return onEnter();
				}
			}
		}
		return ret;
	}
	// RW 0x949575 (= the aggressor's RW 0x942C87 body in BFME2): the attack ends, the team target is cleared
	void onExit(StateExitType status) override
	{
		stopAttack(status);
		if (Team *t = owner().getTeam())
		{
			t->setTeamTargetId(0);
		}
	}
	void crc(StateHasher &h) const override
	{
		crcAttack(h);
		h.addU32(m_deadline);
	}
	void xfer(Xfer &x) override // lane SAVE-4c: AIGuardInnerState::xfer RW 0x949575 is (1, 2) (its attack and a u32); OpenBFME layout (S-4220)
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferAttack(x, machine());
		x.xferUnsignedInt(&m_deadline);
	}

private:
	unsigned m_deadline = 0; // + 0x44
};

// RW vtable 0xC807D8: AIGuardOuterState
class GuardOuterState : public AIState, private GuardAttackHolder
{
public:
	explicit GuardOuterState(AIStateMachine &m) : AIState(m, "AIGuardOuterState") {}
	// RW 0x94A29F
	StateReturnType onEnter() override
	{
		AIGuardMachine &g = guardOf(*this);
		if (g.guardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT)
		{
			return STATE_SUCCESS; // no pursuit outside the guard area
		}
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		Object *nemesis = logic.findObjectByID(g.nemesis());
		if (!nemesis)
		{
			return STATE_SUCCESS;
		}
		// RW 0x68BBE0 / 0x6FF7FA: the owner's layer (Object + 0x428) is the ground or the nemesis can be reached across layers: S-3802, always the ground here
		const float range = AIGuardMachine::getAdjustedVisionRange(obj); // OWNERTYPE | MOOD
		m_exit.center = g.positionToGuard();
		m_exit.radiusSqr = SimMath::mulf32(range, range);
		m_exit.attackGiveUpFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames;
		m_exit.conditions = GuardExitConditions::ATTACK_ExitIfExpiredDuration | GuardExitConditions::ATTACK_ExitIfOutsideRadius |
			GuardExitConditions::ATTACK_ExitIfNoUnitFound;
		startAttack(machine(), false, *nemesis);
		const StateReturnType r = m_attack->onEnter();
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS;
	}
	// RW 0x94A407
	StateReturnType update() override { return m_attack ? m_attack->update() : STATE_SUCCESS; }
	// RW 0x9495C4
	void onExit(StateExitType status) override { stopAttack(status); }
	void crc(StateHasher &h) const override { crcAttack(h); }
	void xfer(Xfer &x) override // lane SAVE-4c (OpenBFME layout, S-4220)
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferAttack(x, machine());
	}
};

// RW vtable 0xC2DE48 (AIPickUpCrateState): the port's AI makes no crates (AI + 0x238), so the state has nothing to pick up
class GuardPickUpCrateState : public AIState
{
public:
	explicit GuardPickUpCrateState(AIStateMachine &m) : AIState(m, "AIPickUpCrateState") {}
	StateReturnType onEnter() override { return STATE_SUCCESS; }
	StateReturnType update() override { return STATE_SUCCESS; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};

// AIGuardAttackAggressorState (RW 0x94A805 onEnter, 0x94A8E8 update)
class GuardAttackAggressorState : public AIState, private GuardAttackHolder
{
public:
	explicit GuardAttackAggressorState(AIStateMachine &m) : AIState(m, "AIGuardAttackAggressorState") {}
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		AIGuardMachine &g = guardOf(*this);
		if (const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule()))
		{
			if (body->lastDamager() != INVALID_ID) // the body's last damage record's source (vslot 15 + 8)
			{
				g.setNemesis(body->lastDamager());
			}
		}
		Object *nemesis = logic.findObjectByID(g.nemesis());
		if (!nemesis)
		{
			return STATE_SUCCESS;
		}
		m_exit.attackGiveUpFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames;
		m_exit.conditions = GuardExitConditions::ATTACK_ExitIfExpiredDuration | GuardExitConditions::ATTACK_ExitIfNoUnitFound;
		startAttack(machine(), true, *nemesis);
		const StateReturnType r = m_attack->onEnter();
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS;
	}
	StateReturnType update() override { return m_attack ? m_attack->update() : STATE_SUCCESS; }
	void onExit(StateExitType status) override
	{
		stopAttack(status);
		if (Team *t = owner().getTeam())
		{
			t->setTeamTargetId(0);
		}
	}
	void crc(StateHasher &h) const override { crcAttack(h); }
	void xfer(Xfer &x) override // lane SAVE-4c (OpenBFME layout, S-4220)
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferAttack(x, machine());
	}
};
} // namespace

const char *guardStopLine()
{
	return "[S-3802] AI guard (RW 0x662A21 -> state 16, the guard machine RW 0x949A10): ported for the LOCATION guard of the stances; not ported: the object, team "
		   "and area guards, the turret's target in lookForInnerTarget, the AI + 0x3CD layer flag and the outer state's layer test (RW 0x68BBE0 / 0x6FF7FA: "
		   "every object is on the ground layer), AI crates, the inner state's restart flag; TheAI's closest enemy is the port's TargetFinder (S-326)";
}

// RW 0x9497F0
bool GuardExitConditions::shouldExit(const AIStateMachine &machine) const
{
	const Object *goal = machine.goalObject();
	if (!goal)
	{
		return (conditions & ATTACK_ExitIfNoUnitFound) != 0;
	}
	if ((conditions & ATTACK_ExitIfExpiredDuration) && goal->logic().getFrame() >= attackGiveUpFrame)
	{
		return true;
	}
	if (conditions & ATTACK_ExitIfOutsideRadius)
	{
		// RW: a HORDE_MEMBER (status 0x26) goal whose container (+ 0x274) is a HORDE (+ 0x115 & 0x20) is measured by its horde
		if (goal->testStatus((unsigned)CombatNames::statuses().hordeMember))
		{
			if (const Object *horde = goal->getHordeObject(false))
			{
				goal = horde;
			}
		}
		if (distSqr2D(*goal->getPosition(), center) > radiusSqr) // the planar distance RW 0x6C97E8 (as RW 0x2C97E8 in BFME2)
		{
			return true;
		}
	}
	return false;
}

AIGuardMachine::AIGuardMachine(AIUpdateInterface &ai)
	: AIStateMachine(ai, "AIGuardMachine")
{
	// RW 0x949A10: the first state defined is the default
	defineState(AI_GUARD_RETURN, std::make_unique<GuardReturnState>(*this), AI_GUARD_IDLE, AI_GUARD_INNER);
	defineState(AI_GUARD_IDLE, std::make_unique<GuardIdleState>(*this), AI_GUARD_INNER, AI_GUARD_RETURN);
	defineState(AI_GUARD_INNER, std::make_unique<GuardInnerState>(*this), AI_GUARD_OUTER, AI_GUARD_OUTER);
	defineState(AI_GUARD_OUTER, std::make_unique<GuardOuterState>(*this), AI_GUARD_GET_CRATE, AI_GUARD_GET_CRATE);
	defineState(AI_GUARD_GET_CRATE, std::make_unique<GuardPickUpCrateState>(*this), AI_GUARD_RETURN, AI_GUARD_RETURN);
	defineState(AI_GUARD_ATTACK_AGGRESSOR, std::make_unique<GuardAttackAggressorState>(*this), AI_GUARD_RETURN, AI_GUARD_RETURN);
}

float AIGuardMachine::getAdjustedVisionRange(const Object &obj)
{
	// RW 0x6FF412 with OWNERTYPE | MOOD (RotWK, like BFME2, has no inner / outer factors): the vision range (RW 0x68E43B: Object + 0x1B0 with the VISION
	// attribute modifiers and the height factor) plus Object + 0xB8, the object's bounding circle radius (review of #73: before, + 0xB8 was taken as 0), then
	// by the mood of a computer unit: asleep (0x100) 0, alert (0x800) * AlertRangeModifier, aggressive (0x1000) * AggressiveRangeModifier; 0 without an AI
	AIUpdateInterface *ai = const_cast<Object &>(obj).getAIUpdateInterface();
	if (!ai)
	{
		return 0.0f;
	}
	float range = SimMath::addf32(CombatQueries::boundingCircleRadius(obj), AIUpdateInterface::objectVisionRangeOf(const_cast<Object &>(obj))); // RW 0x6FF42D .. 0x6FF435: addss
	const unsigned mood = ai->moodMatrixValue();
	if (!(mood & 1u))
	{
		switch (mood & 0x1F00u)
		{
		case 0x100u: return 0.0f;
		case 0x800u: range = SimMath::mulf32(obj.logic().settings().alertRangeModifier, range); break;
		case 0x1000u: range = SimMath::mulf32(obj.logic().settings().aggressiveRangeModifier, range); break;
		default: break;
		}
	}
	return range;
}

float AIGuardMachine::getStdGuardRange(const Object &obj)
{
	return getAdjustedVisionRange(obj); // RW 0x949567: OWNERTYPE | MOOD | GUARDINNER (the inner factor is not read by RW 0x6FF412)
}

Coord3D AIGuardMachine::guardPosition() const
{
	// RW 0x949BD3: the position to guard (a LOCATION guard); the owner's when it is still 0, 0
	Coord3D p = m_positionToGuard;
	if (p.x == 0.0f && p.y == 0.0f)
	{
		p = *const_cast<AIGuardMachine *>(this)->owner().getObject()->getPosition();
	}
	return p;
}

bool AIGuardMachine::lookForInnerTarget()
{
	Object &obj = *owner().getObject();
	GameLogic &logic = obj.logic();
	if (!ableToAttack(obj))
	{
		return false; // RW 0x691269
	}
	if (!logic.combat().autoAcquireEnabled())
	{
		return false; // the port's switch for scenes that are not battles (COMBAT-1's sweeps; default on, no retail counterpart): the guard looks for nothing either
	}
	Team *team = obj.getTeam();
	if (attackCommonTarget(team))
	{
		if (Object *victim = AIHuntState::teamTarget(*team, logic)) // RW 0x7A3E36
		{
			m_nemesis = victim->getID();
			return true;
		}
	}
	Coord3D pos = m_positionToGuard;
	const float range = getStdGuardRange(obj);
	if (pos.x == 0.0f && pos.y == 0.0f)
	{
		pos = *obj.getPosition();
	}
	// RW 0x68FC06: INVISIBLE_STEALTH finds nothing; INVISIBLE_CAMOUFLAGE nothing in a stance of class 3 (HoldGround, Porcupine, HoldGroundMoving)
	static const int stealth = ModelCondition::indexOf("INVISIBLE_STEALTH"), camouflage = ModelCondition::indexOf("INVISIBLE_CAMOUFLAGE");
	if (stealth >= 0 && obj.testModelCondition(stealth))
	{
		return false;
	}
	if (camouflage >= 0 && obj.testModelCondition(camouflage))
	{
		if (const StancesBehavior *sb = dynamic_cast<const StancesBehavior *>(obj.findModule("StancesBehavior")))
		{
			if (StancesBehavior::stanceClass(sb->getStance()) == 3)
			{
				return false;
			}
		}
	}
	AIUpdateInterface &ai = owner();
	const unsigned flags = scanFlags(obj, ai);
	TargetFinder &finder = logic.combat().targets();
	Object *victim = finder.findClosestEnemyNear(obj, pos, range, flags, CMD_FROM_AI); // RW 0x70146A
	if (!victim)
	{
		const Coord3D guard = guardPosition();
		const float half = SimMath::mulf32(range, 0.5f); // RW 0xBD869C
		if (distSqr2D(guard, *obj.getPosition()) > SimMath::mulf32(half, half))
		{
			victim = finder.findClosestEnemy(obj, half, flags, CMD_FROM_AI); // RW 0x701443
		}
	}
	if (victim)
	{
		m_nemesis = victim->getID(); // RW 0x68D30E (the adjustment of a HORDE victim) is the TargetFinder's horde / member rule
		return true;
	}
	return false;
}

void AIGuardMachine::crcGuard(StateHasher &h) const
{
	h.addFloat(m_positionToGuard.x);
	h.addFloat(m_positionToGuard.y);
	h.addFloat(m_positionToGuard.z);
	h.addFloat(m_guardAngle);
	h.addU32(m_nemesis);
	h.addI32(m_guardMode);
	crc(h);
}

void AIGuardMachine::xferGuard(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::coord(x, m_positionToGuard);
	x.xferReal(&m_guardAngle);
	AIXfer::objectId(x, m_nemesis, "ai guard: the object id nemesis");
	x.xferInt(&m_guardMode);
	if (x.isLoading())
	{
		AIXfer::inRange(m_guardMode, GUARDMODE_NORMAL, GUARDMODE_GUARD_FLYING_UNITS_ONLY, "ai guard: the guard mode");
	}
	xfer(x);
}

// ---- AIGuardState --------------------------------------------------------------------------------------------------------------------------------

AIGuardState::~AIGuardState()
{
	if (m_guardMachine)
	{
		m_guardMachine->halt();
	}
}

// RW 0x752C4D
StateReturnType AIGuardState::onEnter()
{
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	static const std::string kStop = guardStopLine();
	obj.logic().noteStop(kStop);
	m_guardMachine = std::make_unique<AIGuardMachine>(a);
	if (a.guardTargetType() != GUARDTARGET_LOCATION)
	{
		return STATE_FAILURE; // S-3802: only the LOCATION guard is ported (the stances' guard)
	}
	m_guardMachine->setPositionToGuard(a.guardLocation(), obj.getOrientation()); // the radius slot of BFME2's setter is the owner's angle (Object + 0x44)
	m_guardMachine->setGuardMode(a.guardMode());
	if (m_guardMachine->initDefaultState() == STATE_FAILURE)
	{
		return STATE_FAILURE;
	}
	return m_guardMachine->setState(AI_GUARD_RETURN); // the default state again (RW: setState(machine + 0x1C))
}

// RW 0x743818
StateReturnType AIGuardState::update()
{
	if (!m_guardMachine)
	{
		return STATE_FAILURE;
	}
	machine().lock();
	const StateReturnType r = m_guardMachine->updateStateMachine();
	machine().unlock();
	return r;
}

// RW 0x752DC7
void AIGuardState::onExit(StateExitType)
{
	if (m_guardMachine)
	{
		m_guardMachine->halt();
		m_guardMachine.reset();
	}
}

bool AIGuardState::isIdle() const
{
	// RW 0x74379D
	if (!m_guardMachine || !m_guardMachine->currentState())
	{
		return true;
	}
	return m_guardMachine->currentState()->isIdle();
}

void AIGuardState::crc(StateHasher &h) const
{
	h.addBool(m_guardMachine != nullptr);
	if (m_guardMachine)
	{
		m_guardMachine->crcGuard(h);
	}
}

// lane SAVE-4c (obfme:AIWorld, OpenBFME layout: S-4220)
void AIGuardState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	bool has = m_guardMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_guardMachine)
		{
			m_guardMachine->xferGuard(x);
		}
		return;
	}
	std::unique_ptr<AIGuardMachine> loaded; // made and loaded complete, then attached
	if (has)
	{
		loaded = std::make_unique<AIGuardMachine>(ai());
		loaded->xferGuard(x);
	}
	m_guardMachine = std::move(loaded);
}

// RW 0x743864
StateReturnType AIGoingIdleState::onEnter()
{
	if (StancesBehavior *sb = StancesBehavior::of(owner()))
	{
		sb->onMoveEnd();
	}
	return STATE_FAILURE;
}

// ---- AIUpdateInterface: RW 0x662A21 ------------------------------------------------------------------------------------------------------------------

void AIUpdateInterface::guardHere(int mode)
{
	m_guardMode = mode;
	Object *obj = getObject();
	if (!obj)
	{
		return;
	}
	if (m_guardTargetType != GUARDTARGET_AREA)
	{
		m_guardTargetType = GUARDTARGET_LOCATION;
	}
	m_guardLocation = *obj->getPosition();
	m_guardAngle = obj->getOrientation();
	m_machine->setState(AI_GUARD);
	wakeUpNow();
}

void AIUpdateInterface::stancesOnCommand(CommandSourceType source)
{
	if (source == CMD_FROM_AI)
	{
		return;
	}
	if (StancesBehavior *sb = StancesBehavior::of(*getObject()))
	{
		sb->onMoveStart();
	}
}
