// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AITNGuard.h for the target addresses and rules.

#include "GameLogic/AI/AITNGuard.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"

namespace
{
const char *const kTNGuardFile = "AITNGuard.cpp";

const GameLogicSettings &settingsOf(const Object &obj)
{
	return obj.logic().settings();
}

bool attackCommonTarget(const Team *t)
{
	return t && t->getPrototype() && t->getPrototype()->getDict().getBool("teamAttackCommonTarget");
}

bool ableToAttack(const Object &o)
{
	static const int sold = CombatNames::status("SOLD");
	return o.getWeapons() && !o.testStatus((unsigned)CombatNames::statuses().noAttack) && !o.isUnderConstruction() && !(sold >= 0 && o.testStatus((unsigned)sold));
}

AITNGuardMachine &tnGuardOf(AIState &s)
{
	return static_cast<AITNGuardMachine &>(s.machine());
}

// RW 0x94C7DF: AITNGuardHasAttackedMeCondition (on 5001 Idle and 5003 Return -> 5005 AttackAggressor)
bool hasAttackedMeTNGuard(Object &obj)
{
	ActiveBody *body = dynamic_cast<ActiveBody *>(obj.getBodyModule());
	if (!body)
	{
		return false;
	}
	const ObjectID id = body->takeLastAttacker(); // RW 0x94C803: body vslot 0x4C
	if (id == INVALID_ID)
	{
		return false;
	}
	Object *attacker = obj.logic().findObjectByID(id);
	if (!attacker || obj.getRelationship(*attacker) != ENEMIES || attacker->isEffectivelyDead())
	{
		return false;
	}
	ObjectWeapons *w = obj.getWeapons();
	return ableToAttack(obj) && w && w->canAttackObject(*attacker, CMD_FROM_AI, false); // RW 0x94C837
}

class TNGuardAttackHolder
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
		h.addU32(m_exit.exitFrame);
		h.addBool(m_attack != nullptr);
		if (m_attack)
		{
			m_attack->crc(h);
		}
	}
	void xferAttack(Xfer &x, AIStateMachine &m)
	{
		x.xferUnsignedInt(&m_exit.exitFrame);
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
		std::unique_ptr<AIAttackState> loaded;
		if (has)
		{
			loaded = std::make_unique<AIAttackState>(m, follow, true, false, &m_exit);
			loaded->xfer(x);
		}
		m_attack = std::move(loaded);
	}

	TNGuardExitConditions m_exit;            // + 0x20
	std::unique_ptr<AIAttackState> m_attack; // + 0x28 / + 0x2C
};

// 5003 AITNGuardReturnState (RW 0x94CEB4 onEnter, 0x94C517 update, 0x94C574 onExit, 0x94C4E7 xfer)
class TNGuardReturnState : public AIState
{
public:
	explicit TNGuardReturnState(AIStateMachine &m)
		: AIState(m, "AITNGuardReturnState")
		, m_enter(makeEnterState(m))
	{
	}
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		// RW 0x94CEBF .. 0x94CEDF
		m_nextReturnScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)settingsOf(obj).guardEnemyReturnScanRate, kTNGuardFile, 0x1FC);
		Object *tunnel = AITNGuardMachine::findBestTunnel(logic, obj.getControllingPlayer(), *obj.getPosition()); // RW 0x94CEF3
		if (!tunnel)
		{
			return STATE_FAILURE; // RW 0x94CEFE
		}
		machine().setGoalObject(tunnel->getID()); // RW 0x94CF0B
		ai().mover().ignoreObstacle(tunnel->getID()); // RW 0x94CF1B: RW 0x6627F0
		return m_enter ? m_enter->onEnter() : STATE_FAILURE; // RW 0x94CF25 -> 0x7515E1
	}
	StateReturnType update() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		if (Team *team = obj.getTeam())
		{
			if (Object *tt = AIHuntState::teamTarget(*team, logic)) // RW 0x94C538 -> 0x7A3E36
			{
				tnGuardOf(*this).setNemesis(tt->getID());
				return STATE_FAILURE; // -> 5000 (AI_TN_GUARD_INNER)
			}
		}
		if (Player *player = obj.getControllingPlayer())
		{
			if (Object *nem = player->tunnelTracker().getCurNemesis(logic)) // RW 0x94C55D -> 0x8FA451
			{
				tnGuardOf(*this).setNemesis(nem->getID());
				return STATE_FAILURE; // -> 5000 (AI_TN_GUARD_INNER)
			}
		}
		const StateReturnType r = m_enter ? m_enter->update() : STATE_FAILURE; // RW 0x94C568 -> 0x756067
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS; // RW 0x94C56D: neg eax; sbb eax, eax -> non-continue becomes SUCCESS (5001)
	}
	void onExit(StateExitType status) override
	{
		if (m_enter)
		{
			m_enter->onExit(status); // RW 0x94C574 -> 0x752B0F
		}
	}
	void onTemporaryStateEnded() override
	{
		if (m_enter)
		{
			m_enter->onTemporaryStateEnded(); // RW 0x740C97
		}
	}
	unsigned conditionTransition() override { return hasAttackedMeTNGuard(owner()) ? (unsigned)AI_TN_GUARD_ATTACK_AGGRESSOR : INVALID_STATE_ID; }
	void crc(StateHasher &h) const override
	{
		h.addU32(m_nextReturnScanTime);
		if (m_enter)
		{
			m_enter->crc(h);
		}
	}
	void xfer(Xfer &x) override // RW 0x94C4E7
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		if (!m_enter)
		{
			m_enter = makeEnterState(machine());
		}
		m_enter->xfer(x);
		x.xferUnsignedInt(&m_nextReturnScanTime);
	}

private:
	std::unique_ptr<AIState> m_enter;
	unsigned m_nextReturnScanTime = 0; // + 0x54
};

// 5001 AITNGuardIdleState (RW 0x94C579 onEnter, 0x94CF2A update, isIdle false RW 0x769CD2)
class TNGuardIdleState : public AIState
{
public:
	explicit TNGuardIdleState(AIStateMachine &m) : AIState(m, "AITNGuardIdleState") {}
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		// RW 0x94C58B .. 0x94C5A3
		m_nextEnemyScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)settingsOf(obj).guardEnemyScanRate, kTNGuardFile, 0x24A);
		ai().mover().ignoreObstacle(PATHFIND_INVALID_ID); // RW 0x94C5B7: RW 0x6627F0(0)
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		const unsigned now = logic.getFrame();
		if (now < m_nextEnemyScanTime) // RW 0x94CF39
		{
			return STATE_SLEEP(m_nextEnemyScanTime - now);
		}
		m_nextEnemyScanTime = now + settingsOf(obj).guardEnemyScanRate; // RW 0x94CF4E
		ai().mover().ignoreObstacle(PATHFIND_INVALID_ID); // RW 0x94CF62
		AITNGuardMachine &g = tnGuardOf(*this);
		if (g.lookForInnerTarget()) // RW 0x94CF8F
		{
			Object *nemesis = logic.findObjectByID(g.nemesis());
			if (!nemesis)
			{
				return STATE_CONTINUE; // RW 0x94CFAB -> 0x94CFF8
			}
			if (obj.getContainedBy()) // RW 0x94CFB0
			{
				Object *bestTunnel = AITNGuardMachine::findBestTunnel(logic, obj.getControllingPlayer(), *nemesis->getPosition()); // RW 0x94CFC3
				ContainModuleInterface *contain = bestTunnel ? bestTunnel->getContain() : nullptr;
				if (!contain)
				{
					return STATE_FAILURE; // RW 0x94CFDF -> 0x94D024: push -2
				}
				if (!contain->isExitBusy()) // RW 0x94CFE5
				{
					contain->exitObjectInAHurry(&obj); // RW 0x94CFF5
				}
				return STATE_CONTINUE; // RW 0x94CFF8
			}
			return STATE_SUCCESS; // RW 0x94CFFC -> 5000 (AI_TN_GUARD_INNER)
		}
		if (!obj.getContainedBy()) // RW 0x94D001
		{
			if (AITNGuardMachine::findBestTunnel(logic, obj.getControllingPlayer(), *obj.getPosition())) // RW 0x94D017
			{
				return STATE_FAILURE; // RW 0x94D024 -> 5003 (AI_TN_GUARD_RETURN)
			}
		}
		return STATE_SLEEP(m_nextEnemyScanTime - now); // RW 0x94CF85
	}
	bool isIdle() const override { return false; } // RW 0x769CD2
	unsigned conditionTransition() override { return hasAttackedMeTNGuard(owner()) ? (unsigned)AI_TN_GUARD_ATTACK_AGGRESSOR : INVALID_STATE_ID; }
	void crc(StateHasher &h) const override { h.addU32(m_nextEnemyScanTime); }
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_nextEnemyScanTime);
	}

private:
	unsigned m_nextEnemyScanTime = 0; // + 0x20
};

// 5000 AITNGuardInnerState (RW 0x94CBFC onEnter, 0x94CC98 update, 0x94C481 onExit, 0x94C45F xfer)
class TNGuardInnerState : public AIState, private TNGuardAttackHolder
{
public:
	explicit TNGuardInnerState(AIStateMachine &m) : AIState(m, "AITNGuardInnerState") {}
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		Object *nemesis = logic.findObjectByID(tnGuardOf(*this).nemesis());
		if (!nemesis)
		{
			return STATE_SUCCESS; // RW 0x94CC26
		}
		m_exit.exitFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames; // RW 0x94CC3B: AIData + 0x3C
		startAttack(machine(), false, *nemesis); // RW 0x94CC5A: follow = false
		const StateReturnType r = m_attack->onEnter();
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS; // RW 0x94CC86: neg eax; sbb eax, eax
	}
	StateReturnType update() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		Object *goal = machine().goalObject();
		Player *player = obj.getControllingPlayer();
		TunnelTracker *tunnels = player ? &player->tunnelTracker() : nullptr;
		Team *team = obj.getTeam();
		Object *teamTarget = team ? AIHuntState::teamTarget(*team, logic) : nullptr; // RW 0x94CCD4 -> 0x7A3E36
		if (!goal) // RW 0x94CCE1
		{
			if (teamTarget) // RW 0x94CCE7
			{
				machine().setGoalObject(teamTarget->getID());
				m_exit.exitFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames; // RW 0x94CD1F
				return STATE_CONTINUE;
			}
			if (Object *nem = tunnels ? tunnels->getCurNemesis(logic) : nullptr) // RW 0x94CCFC
			{
				machine().setGoalObject(nem->getID());
				m_exit.exitFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames;
				return STATE_CONTINUE;
			}
			if (m_scanForEnemy) // RW 0x94CD2D
			{
				m_scanForEnemy = false;
				if (Object *enemy = AITNGuardMachine::lookForEnemy(obj)) // RW 0x94CD39 -> 0x94C6BD
				{
					if (m_attack)
					{
						m_attack->onExit(EXIT_RESET); // RW 0x94CD4B
					}
					machine().setGoalObject(enemy->getID()); // RW 0x94CD57
					if (tunnels)
					{
						tunnels->updateNemesis(enemy, logic); // RW 0x94CD61
					}
					return m_attack ? m_attack->onEnter() : STATE_SUCCESS; // RW 0x94CD72
				}
			}
			return m_attack ? m_attack->update() : STATE_SUCCESS; // RW 0x94CD9A
		}
		if (goal != teamTarget && teamTarget != nullptr) // RW 0x94CD75
		{
			if (tunnels)
			{
				tunnels->updateNemesis(goal, logic); // RW 0x94CD80
			}
			machine().setGoalObject(teamTarget->getID()); // RW 0x94CD8B
		}
		return m_attack ? m_attack->update() : STATE_SUCCESS; // RW 0x94CD9A
	}
	void onExit(StateExitType status) override { stopAttack(status); } // RW 0x94C481
	void crc(StateHasher &h) const override
	{
		crcAttack(h);
		h.addBool(m_scanForEnemy);
	}
	void xfer(Xfer &x) override // RW 0x94C45F
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferBool(&m_scanForEnemy);
		xferAttack(x, machine());
	}

private:
	bool m_scanForEnemy = false; // + 0x28
};

// 5002 AITNGuardOuterState (RW 0x94CD9D onEnter, 0x94CE46 update, 0x94C4B4 onExit)
class TNGuardOuterState : public AIState, private TNGuardAttackHolder
{
public:
	explicit TNGuardOuterState(AIStateMachine &m) : AIState(m, "AITNGuardOuterState") {}
	StateReturnType onEnter() override
	{
		AITNGuardMachine &g = tnGuardOf(*this);
		if (g.guardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT) // RW 0x94CDAF
		{
			return STATE_SUCCESS;
		}
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		Object *nemesis = logic.findObjectByID(g.nemesis());
		if (!nemesis)
		{
			return STATE_SUCCESS; // RW 0x94CDD4
		}
		m_exit.exitFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames; // RW 0x94CDE4
		startAttack(machine(), false, *nemesis); // RW 0x94CE0A: follow = false
		const StateReturnType r = m_attack->onEnter();
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS; // RW 0x94CE34
	}
	StateReturnType update() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		Object *goal = machine().goalObject();
		if (!goal && !logic.findObjectByID(tnGuardOf(*this).nemesis())) // RW 0x94CE5E .. 0x94CE75
		{
			Object *newGoal = nullptr;
			if (Team *team = obj.getTeam())
			{
				if (attackCommonTarget(team)) // RW 0x94CE80
				{
					newGoal = AIHuntState::teamTarget(*team, logic); // RW 0x94CE88 -> 0x7A3E36
				}
			}
			machine().setGoalObject(newGoal ? newGoal->getID() : INVALID_ID); // RW 0x94CE9B
			return m_attack ? m_attack->onEnter() : STATE_SUCCESS; // RW 0x94CEA6
		}
		return m_attack ? m_attack->update() : STATE_SUCCESS; // RW 0x94CEB1
	}
	void onExit(StateExitType status) override { stopAttack(status); } // RW 0x94C4B4
	void crc(StateHasher &h) const override { crcAttack(h); }
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferAttack(x, machine());
	}
};

// 5004 AIPickUpCrateState (stub: no AI crates in port)
class TNGuardGetCrateState : public AIState
{
public:
	explicit TNGuardGetCrateState(AIStateMachine &m) : AIState(m, "AIPickUpCrateState") {}
	StateReturnType onEnter() override { return STATE_SUCCESS; }
	StateReturnType update() override { return STATE_SUCCESS; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};

// 5005 AITNGuardAttackAggressorState (RW 0x94D02C onEnter, 0x94D11D update, 0x94C5C6 onExit)
class TNGuardAttackAggressorState : public AIState, private TNGuardAttackHolder
{
public:
	explicit TNGuardAttackAggressorState(AIStateMachine &m) : AIState(m, "AITNGuardAttackAggressorState") {}
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		AITNGuardMachine &g = tnGuardOf(*this);
		if (const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule())) // RW 0x94D042 .. 0x94D068
		{
			if (body->lastDamager() != INVALID_ID)
			{
				g.setNemesis(body->lastDamager());
			}
		}
		Object *nemesis = logic.findObjectByID(g.nemesis()); // RW 0x94D071
		if (!nemesis)
		{
			return STATE_SUCCESS; // RW 0x94D083
		}
		if (Player *player = obj.getControllingPlayer()) // RW 0x94D08E .. 0x94D0A6
		{
			player->tunnelTracker().updateNemesis(nemesis, logic);
		}
		m_exit.exitFrame = logic.getFrame() + settingsOf(obj).guardChaseUnitsFrames; // RW 0x94D0BE
		startAttack(machine(), true, *nemesis); // RW 0x94D0E1: follow = true
		const StateReturnType r = m_attack->onEnter();
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS; // RW 0x94D10B
	}
	StateReturnType update() override
	{
		if (!m_attack)
		{
			return STATE_SUCCESS;
		}
		Object &obj = owner();
		GameLogic &logic = obj.logic();
		AIStateMachine *sub = m_attack->attackMachine();
		const unsigned subId = sub && sub->currentState() ? sub->currentState()->id() : (unsigned)AI_NO_STATE; // RW 0x94D120 .. 0x94D132
		if (subId == (unsigned)ATTACK_FIRE) // RW 0x94D137: 0x67
		{
			if (Player *player = obj.getControllingPlayer())
			{
				if (Object *nem = logic.findObjectByID(tnGuardOf(*this).nemesis()))
				{
					player->tunnelTracker().updateNemesis(nem, logic); // RW 0x94D16A -> 0x8FA702
				}
			}
		}
		return m_attack->update(); // RW 0x94D177
	}
	void onExit(StateExitType status) override
	{
		stopAttack(status); // RW 0x94C5C6
		if (Team *t = owner().getTeam())
		{
			t->setTeamTargetId(0); // RW 0x94C60A -> 0x79FDEA(0)
		}
	}
	void crc(StateHasher &h) const override { crcAttack(h); }
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferAttack(x, machine());
	}
};
} // namespace

// RW 0x94C43C
bool TNGuardExitConditions::shouldExit(const AIStateMachine &machine) const
{
	const Object *owner = machine.owner().getObject();
	return owner && owner->logic().getFrame() >= exitFrame;
}

// RW 0x94C8D0
AITNGuardMachine::AITNGuardMachine(AIUpdateInterface &ai)
	: AIStateMachine(ai, "AITNGuardMachine")
{
	defineState(AI_TN_GUARD_RETURN, std::make_unique<TNGuardReturnState>(*this), AI_TN_GUARD_IDLE, AI_TN_GUARD_INNER);
	defineState(AI_TN_GUARD_IDLE, std::make_unique<TNGuardIdleState>(*this), AI_TN_GUARD_INNER, AI_TN_GUARD_RETURN);
	defineState(AI_TN_GUARD_INNER, std::make_unique<TNGuardInnerState>(*this), AI_TN_GUARD_OUTER, AI_TN_GUARD_OUTER);
	defineState(AI_TN_GUARD_OUTER, std::make_unique<TNGuardOuterState>(*this), AI_TN_GUARD_GET_CRATE, AI_TN_GUARD_GET_CRATE);
	defineState(AI_TN_GUARD_GET_CRATE, std::make_unique<TNGuardGetCrateState>(*this), AI_TN_GUARD_RETURN, AI_TN_GUARD_RETURN);
	defineState(AI_TN_GUARD_ATTACK_AGGRESSOR, std::make_unique<TNGuardAttackAggressorState>(*this), AI_TN_GUARD_RETURN, AI_TN_GUARD_RETURN);
}

// RW 0x94C84F
Object *AITNGuardMachine::findBestTunnel(GameLogic &logic, const Player *player, const Coord3D &pos)
{
	if (!player)
	{
		return nullptr;
	}
	const TunnelTracker *tunnels = player->tunnelTrackerIfAny();
	if (!tunnels)
	{
		return nullptr;
	}
	Object *best = nullptr;
	float bestDistSqr = 0.0f;
	for (ObjectID id : tunnels->tunnelIds())
	{
		Object *tunnel = logic.findObjectByID(id);
		if (!tunnel)
		{
			continue;
		}
		const float dx = SimMath::subf32(tunnel->getPosition()->x, pos.x);
		const float dy = SimMath::subf32(tunnel->getPosition()->y, pos.y);
		const float d2 = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		if (!best || d2 < bestDistSqr) // RW 0x94C8B6: comiss xmm0, xmm1; jbe
		{
			bestDistSqr = d2;
			best = tunnel;
		}
	}
	return best;
}

// RW 0x94C6BD
Object *AITNGuardMachine::lookForEnemy(Object &obj)
{
	const float range = AIGuardMachine::getStdGuardRange(obj); // RW 0x94C72A -> RW 0x949567
	PartitionFilterFn rel([&](Object &cand) {
		return obj.getRelationship(cand) == ENEMIES; // RW 0x94C704 -> 0x660B85
	});
	PartitionFilterFn able([&](Object &cand) {
		if (cand.isEffectivelyDead())
		{
			return false;
		}
		ObjectWeapons *w = obj.getWeapons();
		return ableToAttack(obj) && w && w->canAttackObject(cand, CMD_FROM_AI, false); // RW 0x94C6E3 -> 0x660CA4
	});
	return obj.logic().partition().getClosestObject(*obj.getPosition(), range, FROM_CENTER_2D, { &rel, &able }); // RW 0x94C73C -> 0xA39090
}

// RW 0x94CA71
bool AITNGuardMachine::lookForInnerTarget()
{
	Object &obj = *owner().getObject();
	GameLogic &logic = obj.logic();
	Team *team = obj.getTeam();
	if (team && attackCommonTarget(team)) // RW 0x94CA89
	{
		if (Object *tt = AIHuntState::teamTarget(*team, logic)) // RW 0x94CA92 -> 0x7A3E36
		{
			m_nemesis = tt->getID();
			return true;
		}
	}
	Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	TunnelTracker &tunnels = player->tunnelTracker();
	if (Object *nem = tunnels.getCurNemesis(logic)) // RW 0x94CAC9 -> 0x8FA451
	{
		m_nemesis = nem->getID();
		return true;
	}
	const unsigned now = logic.getFrame();
	const unsigned scanRate = settingsOf(obj).guardEnemyScanRate; // RW 0x94CB6F .. 0x94CB78
	for (ObjectID tunnelId : tunnels.tunnelIds()) // RW 0x94CAE3 .. 0x94CBCA
	{
		Object *tunnel = logic.findObjectByID(tunnelId);
		if (!tunnel)
		{
			continue;
		}
		if (AIUpdateInterface *tai = tunnel->getAIUpdateInterface())
		{
			if (Object *goal = tai->stateMachine().goalObject())
			{
				if (obj.getRelationship(*goal) == ENEMIES)
				{
					m_nemesis = goal->getID(); // RW 0x94CBCF
					return true;
				}
			}
		}
		if (const ActiveBody *tbody = dynamic_cast<const ActiveBody *>(tunnel->getBodyModule()))
		{
			if (tbody->lastDamageFrame() != 0xFFFFFFFFu && tbody->lastDamager() != INVALID_ID &&
				tbody->lastDamageFrame() + scanRate > now)
			{
				if (Object *attacker = logic.findObjectByID(tbody->lastDamager()))
				{
					ObjectWeapons *w = obj.getWeapons();
					if (obj.getRelationship(*attacker) == ENEMIES && ableToAttack(obj) && w && w->canAttackObject(*attacker, CMD_FROM_AI, false))
					{
						m_nemesis = attacker->getID(); // RW 0x94CBDF
						if (team)
						{
							team->setTeamTargetId(attacker->getID()); // RW 0x94CBEC -> 0x79FDEA
						}
						tunnels.updateNemesis(attacker, logic); // RW 0x94CBF5 -> 0x8FA702
						return true;
					}
				}
			}
		}
	}
	return false;
}

void AITNGuardMachine::crcGuard(StateHasher &h) const
{
	h.addFloat(m_positionToGuard.x);
	h.addFloat(m_positionToGuard.y);
	h.addFloat(m_positionToGuard.z);
	h.addU32(m_nemesis);
	h.addI32(m_guardMode);
	crc(h);
}

// RW 0x94C781
void AITNGuardMachine::xferGuard(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::coord(x, m_positionToGuard);
	AIXfer::objectId(x, m_nemesis, "ai tunnel guard: the object id nemesis");
	x.xferInt(&m_guardMode);
	if (x.isLoading())
	{
		AIXfer::inRange(m_guardMode, GUARDMODE_NORMAL, GUARDMODE_GUARD_FLYING_UNITS_ONLY, "ai tunnel guard: the guard mode");
	}
	xfer(x);
}

// ---- AITunnelNetworkGuardState (0x18, RW 0x754259) -----------------------------------------------------------------------------

AITunnelNetworkGuardState::~AITunnelNetworkGuardState()
{
	if (m_guardMachine)
	{
		m_guardMachine->halt();
	}
}

// RW 0x743AA6
StateReturnType AITunnelNetworkGuardState::onEnter()
{
	AIUpdateInterface &a = ai();
	m_guardMachine = std::make_unique<AITNGuardMachine>(a);
	m_guardMachine->setPositionToGuard(a.guardLocation()); // RW 0x743AF5 (AI vslot 0x1D8)
	m_guardMachine->setGuardMode(a.guardMode());          // RW 0x743B0B (AI vslot 0x1F4)
	if (m_guardMachine->initDefaultState() == STATE_FAILURE) // RW 0x743B1C
	{
		return STATE_FAILURE;
	}
	return m_guardMachine->setState(AI_TN_GUARD_RETURN); // RW 0x743B27 -> 5003
}

// RW 0x747EAA
StateReturnType AITunnelNetworkGuardState::update()
{
	if (!m_guardMachine) // RW 0x747EAD
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	if (obj.isEffectivelyDead() && !obj.isKindOf((unsigned)CombatNames::kinds().projectile)) // RW 0x747EC1 .. 0x747ED4
	{
		return STATE_FAILURE;
	}
	machine().lock(); // RW 0x747EDE
	StateReturnType r;
	{
		KeptSubMachines<AITNGuardMachine>::Run run(m_keptMachines);
		r = m_guardMachine->updateStateMachine(); // RW 0x747EE5
	}
	machine().unlock(); // RW 0x747EEB
	return r;
}

// RW 0x743B40
void AITunnelNetworkGuardState::onExit(StateExitType)
{
	m_keptMachines.drop(m_guardMachine);
}

void AITunnelNetworkGuardState::crc(StateHasher &h) const
{
	h.addBool(m_guardMachine != nullptr);
	if (m_guardMachine)
	{
		m_guardMachine->crcGuard(h);
	}
}

// RW 0x743A14
void AITunnelNetworkGuardState::xfer(Xfer &x)
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
	std::unique_ptr<AITNGuardMachine> loaded;
	if (has)
	{
		loaded = std::make_unique<AITNGuardMachine>(ai());
		loaded->xferGuard(x);
	}
	m_guardMachine = std::move(loaded);
}
