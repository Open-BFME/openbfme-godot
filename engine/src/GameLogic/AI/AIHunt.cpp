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
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIXfer.h"

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
	void xfer(Xfer &x) override { xferNoFields(x); } // lane SAVE-4c: nothing is kept (the draw is made at every entry)
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
	return "[S-1188] AI hunt (TEAM_HUNT / NAMED_HUNT / PLAYER_HUNT, RW 0x741133): the crate pick-up (AI + 0x238) is not part of the port (lane TARGET-1: the "
		   "attack priority info AI + 0x70 and RotWK's closest enemy search are); PLAYER_HUNT (RW 0x6AF4B1, lane DECOMP-2) walks the player's objects in "
		   "object list order";
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

std::unique_ptr<AIStateMachine> AIHuntState::makeHuntMachine()
{
	// the machine's vslot 0x24 (lane FLY-1: a giant bird's swoop-then-idle machine), else the base attack-then-idle machine
	std::unique_ptr<AIStateMachine> m = ai().makeAttackThenIdleMachine(AI_ATTACK_OBJECT);
	if (!m)
	{
		m = std::make_unique<AIStateMachine>(ai(), "AIAttackThenIdleStateMachine");
		m->defineState(AI_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m, false, true, false), AI_IDLE, AI_IDLE);
		m->defineState(AI_IDLE, std::make_unique<HuntIdleState>(*m), AI_IDLE, AI_IDLE);
	}
	return m;
}

// lane SAVE-4c (obfme:AIWorld): OpenBFME layout (AIHuntState has no retail xfer in docs/SAVES-RETAIL.md's table by that name; stop S-4220)
void AIHuntState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_nextEnemyScanTime);
	bool has = m_huntMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_huntMachine)
		{
			m_huntMachine->xfer(x);
		}
		return;
	}
	std::unique_ptr<AIStateMachine> loaded; // made and loaded complete, then attached
	if (has)
	{
		loaded = makeHuntMachine();
		loaded->xfer(x);
	}
	m_huntMachine = std::move(loaded);
}

StateReturnType AIHuntState::onEnter()
{
	m_huntMachine = makeHuntMachine();
	GameLogic &logic = owner().logic();
	m_nextEnemyScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)kEnemyScanRate, "AIStates.cpp", 0x3C92);
	return m_huntMachine->initDefaultState();
}

void AIHuntState::onExit(StateExitType)
{
	m_keptMachines.drop(m_huntMachine); // halted, and kept while its update runs (review of #64)
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
	// RW 0x748069 .. 0x74816E (lane TARGET-1: the attack priority info AI + 0x70 and RotWK's search)
	GameLogic &logic = obj.logic();
	Team *team = obj.getTeam();
	const bool common = attackCommonTarget(team);
	Object *teamVictim = common ? teamTarget(*team, logic) : nullptr;
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	const AttackPriorityInfo *info = ai ? ai->attackInfo() : nullptr;
	if (teamVictim && !info)
	{
		return teamVictim; // RW 0x7480AE
	}
	const unsigned q = AI::CAN_ATTACK | AI::ATTACK_BUILDINGS | AI::AUTO_ACQUIRE; // 0x4A
	Object *victim = AI::findClosestEnemy(obj, kHuntRange, q, info, nullptr, 0);   // RW 0x701443
	const Player *p = obj.getControllingPlayer();
	if (!victim && p && p->unitsShouldHunt())
	{
		victim = AI::findClosestEnemy(obj, kHuntRange, q, nullptr, nullptr, 0); // RW 0x7480E5: again without the info
	}
	if (common)
	{
		if (teamVictim && info)
		{
			// RW 0x748123 .. 0x74815E: the team's target keeps it unless the new victim's priority is higher (x87: the team target's priority stored as a float,
			// the victim's compared wide)
			if (!victim)
			{
				victim = teamVictim;
			}
			const float teamPriority = info->getPriority(obj, *teamVictim, true);
			const float victimPriority = info->getPriority(obj, *victim, true);
			if (victimPriority <= teamPriority)
			{
				victim = teamVictim;
			}
		}
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
			const Player *p = obj.getControllingPlayer();
			const bool unitsShouldHunt = p && p->unitsShouldHunt(); // Player + 0x35D (PLAYER_HUNT, lane DECOMP-2)
			if (p && !unitsShouldHunt && m_huntMachine->currentStateId() == AI_IDLE && !victim)
			{
				return STATE_SUCCESS;
			}
		}
	}
	// (4) RW 0x74820F
	machine().lock();
	StateReturnType ret;
	{
		KeptSubMachines<AIStateMachine>::Run run(m_keptMachines); // an attack inside can end this state (onExit below)
		ret = m_huntMachine->updateStateMachine();
	}
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
