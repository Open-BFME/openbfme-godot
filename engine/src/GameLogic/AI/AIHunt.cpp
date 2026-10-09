// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIHunt.h.

#include "GameLogic/AI/AIHunt.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "Common/StateHash.h"

namespace
{

const unsigned kEnemyScanRate = 3u * (unsigned)LOGICFRAMES_PER_SECOND; // RW DAT_00D9F608 * 3
const float kHuntRange = 9999.9f;                                       // RW 0xC1D8E4

// the hunt machine's state 0 (RW 0x741812(machine, 1)): the idle state that does not look for targets
class HuntIdleState : public AIState
{
public:
	explicit HuntIdleState(AIStateMachine &m) : AIState(m, "AIIdleState") {}
	// RW 0x748A4F (the idle state's onEnter, AIStates.cpp line 0x94A): the initial sleep offset draws the logic generator on every entry (review r1)
	StateReturnType onEnter() override
	{
		owner().logic().random().getValue(0, 2 * (int)LOGICFRAMES_PER_SECOND, "AIStates.cpp", 0x94A);
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return STATE_CONTINUE; }
	bool isIdle() const override { return true; }
};

bool kindOf(const Object &o, const char *name)
{
	const int b = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return b >= 0 && o.isKindOf((unsigned)b);
}

bool attackCommonTarget(const Team *t)
{
	return t && t->getPrototype() && t->getPrototype()->getDict().getBool("teamAttackCommonTarget");
}

// RW 0x6AA61B: the player's AI's difficulty, else the script engine's (INFERENCE: a side's AI difficulty is its skirmish level)
int playerDifficulty(const Player &p, GameLogic &logic)
{
	return p.getSkirmishDifficulty() >= 0 ? p.getSkirmishDifficulty() : logic.scriptEngine().gameDifficulty();
}

} // namespace

const char *huntStopLine()
{
	return "[S-1188] AI hunt (TEAM_HUNT / NAMED_HUNT, RW 0x741133): the crate pick-up (AI + 0x238), the attack priority info (AI + 0x70) and the player-wide "
		   "hunt flag (+ 0x35D, PLAYER_HUNT) are not part of the port; the closest enemy is the port's TargetFinder (S-326)";
}

bool AIUpdateInterface::aiHunt(CommandSourceType source)
{
	if (!acceptCommand(source, 0x12))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (!GarrisonRules::isMobile(obj) || obj.isKindOf((unsigned)CombatNames::kinds().projectile) ||
		obj.testStatus((unsigned)CombatNames::statuses().hordeMember))
	{
		return false; // RW 0x66450F
	}
	m_machine->clear();
	setLastCommandSource(source);
	m_machine->setState(AI_HUNT);
	wakeUpNow();
	return true;
}

AIHuntState::AIHuntState(AIStateMachine &m)
	: AIState(m, "AIHuntState")
{
}

AIHuntState::~AIHuntState()
{
	if (m_huntMachine)
	{
		m_huntMachine->halt();
	}
}

Object *AIHuntState::teamTarget(Team &team, GameLogic &logic)
{
	if (team.teamTargetId() == 0)
	{
		return nullptr;
	}
	Object *o = logic.findObjectByID(team.teamTargetId());
	if (o && InvisibilityManager::isStealthedAndUndetected(*o, team.getControllingPlayer()))
	{
		o = nullptr;
	}
	if (o && (o->isEffectivelyDead() || o->getContainedBy()))
	{
		o = nullptr;
	}
	if (!o)
	{
		team.setTeamTargetId(0);
	}
	return o;
}

void AIHuntState::setTeamTarget(Team &team, Object *victim, GameLogic &logic)
{
	if (!victim)
	{
		team.setTeamTargetId(0);
		return;
	}
	const Player *p = team.getControllingPlayer();
	if (p && p->getPlayerType() == PLAYER_COMPUTER && playerDifficulty(*p, logic) != 0)
	{
		team.setTeamTargetId(victim->getID());
	}
}

StateReturnType AIHuntState::onEnter()
{
	m_huntMachine = std::make_unique<AIStateMachine>(ai(), "AIAttackThenIdleStateMachine");
	m_huntMachine->defineState(AI_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m_huntMachine, false, true, false), AI_IDLE, AI_IDLE);
	m_huntMachine->defineState(AI_IDLE, std::make_unique<HuntIdleState>(*m_huntMachine), AI_IDLE, AI_IDLE);
	GameLogic &logic = owner().logic();
	m_nextEnemyScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)kEnemyScanRate, "AIStates.cpp", 0x3C92);
	return m_huntMachine->initDefaultState();
}

void AIHuntState::onExit(StateExitType)
{
	if (m_huntMachine)
	{
		m_huntMachine->halt();
		m_huntMachine.reset();
	}
	if (ObjectWeapons *w = owner().getWeapons())
	{
		w->releaseWeaponLock(LOCKED_TEMPORARILY);
	}
}

bool AIHuntState::isActive() const
{
	return m_huntMachine && m_huntMachine->currentState() && !m_huntMachine->currentState()->isIdle();
}

Object *AIHuntState::findVictim(Object &obj)
{
	GameLogic &logic = obj.logic();
	Team *team = obj.getTeam();
	const bool common = attackCommonTarget(team);
	Object *teamVictim = common ? teamTarget(*team, logic) : nullptr;
	if (teamVictim)
	{
		return teamVictim; // RW 0x7480AE: no attack priority info (AI + 0x70) in the port
	}
	unsigned flags = TargetFinder::ALLOW_STRUCTURES;
	flags |= obj.isKindOf((unsigned)CombatNames::kinds().horde) ? TargetFinder::HORDES_ONLY : TargetFinder::MEMBERS_ONLY;
	Object *victim = logic.combat().targets().findClosestEnemy(obj, kHuntRange, flags, CMD_FROM_AI);
	if (common)
	{
		setTeamTarget(*team, victim, logic); // RW 0x748169
	}
	return victim;
}

StateReturnType AIHuntState::update()
{
	if (!m_huntMachine)
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	GameLogic &logic = obj.logic();
	// (1) RW 0x747EFE: the parent machine's goal object
	if (machine().goalObjectID() != INVALID_ID && machine().goalObjectID() != m_huntMachine->goalObjectID())
	{
		if (Object *goal = machine().goalObject())
		{
			m_huntMachine->setGoalObject(goal->getID());
			if (m_huntMachine->currentStateId() == AI_IDLE)
			{
				m_huntMachine->setState(AI_ATTACK_OBJECT);
			}
			m_nextEnemyScanTime = logic.getFrame() + kEnemyScanRate;
		}
		else
		{
			machine().setGoalObject(INVALID_ID);
		}
	}
	const unsigned now = logic.getFrame();
	// (2) RW 0x747F6D: no scan while fighting in melee
	bool busy = obj.testStatus((unsigned)CombatNames::statuses().isMeleeAttacking);
	if (!busy && kindOf(obj, "HORDE"))
	{
		Object *goal = m_huntMachine->goalObject();
		HordeAIUpdate *horde = dynamic_cast<HordeAIUpdate *>(obj.getAIUpdateInterface());
		if (horde && goal && !goal->isKindOf((unsigned)CombatNames::kinds().structure) && horde->isInCurrentMelee(goal))
		{
			busy = true;
		}
	}
	if (!busy && now >= m_nextEnemyScanTime)
	{
		// (3) RW 0x747FE2
		ObjectWeapons *w = obj.getWeapons();
		if (w && w->isOutOfAmmo() && !obj.isKindOf((unsigned)CombatNames::kinds().projectile))
		{
			return STATE_FAILURE;
		}
		bool rescan = true;
		if (kindOf(obj, "GIANT_BIRD")) // AI vslot 0x168 (GiantBirdAIUpdate RW 0x8BD372)
		{
			const AIState *cur = m_huntMachine->currentState();
			const bool selfLoop = !cur || (cur->successId() == cur->id() && cur->failureId() == cur->id());
			if (!selfLoop && !cur->isIdle())
			{
				rescan = false;
			}
		}
		m_nextEnemyScanTime = now + kEnemyScanRate;
		if (rescan)
		{
			Object *victim = findVictim(obj);
			const ObjectID victimId = victim ? victim->getID() : (ObjectID)INVALID_ID;
			if (victimId != m_huntMachine->goalObjectID())
			{
				m_huntMachine->setGoalObject(victimId);
				m_huntMachine->setState(AI_ATTACK_OBJECT); // RW 0x7481C8 (RW 0x66B1D0 before it, for a first victim, is the client's notification)
			}
			else if ((!m_huntMachine->currentState() || m_huntMachine->currentState()->isIdle()) && victim)
			{
				m_huntMachine->setState(AI_ATTACK_OBJECT);
			}
			const bool unitsShouldHunt = false; // Player + 0x35D (PLAYER_HUNT, S-1188)
			const Player *p = obj.getControllingPlayer();
			if (p && !unitsShouldHunt && m_huntMachine->currentStateId() == AI_IDLE && !victim)
			{
				return STATE_SUCCESS;
			}
		}
	}
	// (4) RW 0x74820F
	machine().lock();
	StateReturnType ret = m_huntMachine->updateStateMachine();
	machine().unlock();
	if (IS_STATE_SLEEP(ret))
	{
		ret = STATE_CONTINUE;
	}
	return ret;
}

void AIHuntState::crc(StateHasher &h) const
{
	h.addU32(m_nextEnemyScanTime);
	h.addBool(m_huntMachine != nullptr);
	if (m_huntMachine)
	{
		m_huntMachine->crc(h);
	}
}
