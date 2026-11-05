// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIAttack.h for the target facts and the donors.

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

namespace
{
const float kRelativeAngleThreshold = 0.035f; // ZH AIAttackAimAtTargetState: about 2 degrees
const unsigned kMinRecomputeFrames = 10;      // ZH MIN_RECOMPUTE_TIME

Object *victimOf(const AIStateMachine &m)
{
	return m.goalObject();
}

ObjectWeapons *weaponsOf(const AIStateMachine &m)
{
	return m.owner().getObject()->getWeapons();
}

// RW 0x44DDEC(0x4B) on the owner
bool isRunningDown(const Object &o)
{
	const int bit = CombatNames::statuses().runningDownFromBehind;
	return bit >= 0 && o.testStatus((unsigned)bit);
}

void setStatusBit(Object &o, int bit, bool on)
{
	o.setStatus((unsigned)bit, on);
}

bool victimGone(const Object *victim)
{
	return !victim || !CombatQueries::isAlive(*victim);
}

// RW 0x644FD0 (two SSE loops, the same as the movers use)
float normalizeAngle(float a)
{
	const float pi = 3.14159274f, twoPi = 6.28318548f;
	if (a > pi)
	{
		do
		{
			a = SimMath::subf32(a, twoPi);
		} while (a > pi);
	}
	if (-pi < a)
	{
		return a;
	}
	do
	{
		a = SimMath::addf32(a, twoPi);
	} while (-pi >= a);
	return a;
}

// RW 0x6939DF: the object is a horde member: with status HORDE_MEMBER (0x26) it has a container id (+ 0x78); without, its container exists and is a HORDE
// (template + 0x115 bit 0x20); its horde is then that container (RW 0x693A1A(0) / the id at + 0x78)
Object *hordeOf(Object &o)
{
	static const int kHordeMember = CombatNames::status("HORDE_MEMBER");
	static const int kHorde = CombatNames::kindOf("HORDE");
	Object *c = o.getContainedBy();
	if (o.testStatus((unsigned)kHordeMember))
	{
		return c;
	}
	return c && c->isKindOf((unsigned)kHorde) ? c : nullptr;
}

const char *const kStopAim =
	"[S-7175] AIAttackAimAtTargetState::updateInternal (RW 0x75232F, lane AIMAIM-1): an owner whose template sets FiringArc (+ 0x54C below 360.0, RW 0xBDC1F4) "
	"is not handled: RW 0x7523B7 .. 0x7523F6 (the victim inside the arc, RW 0x68FA3D, unless AI vslot 0x23C answers 2) and RW 0x752572 .. 0x7525D3 (AI vslot "
	"0x238, Object + 0x1C0) are not ported";

// RW 0x741798 (ZH AIStates.cpp cannotPossiblyAttackObject; lane SPELL-3 review r3): the owner (machine + 0x14) and the goal object (RW 0x8DBACE). A victim that is
// effectively dead (+ 0x458 bit 0) while the owner's current weapon (RW 0x68B58C(0)) needs no victim (template + 0x16B) keeps the attack (false); without
// an owner or a victim: false; an owner Object::isAbleToAttack (RW 0x691269) refuses: true (NO_ATTACK: the Citadel's members while they build, an
// ordered attack included). INFERENCE (S-325): RW 0x68D6A6, getAbleToAttackSpecificObject(userData, victim, the AI's last command source (AI vslot 0x23C))
// answering neither 2 nor 3, is not tested here (the port's canAttackObject stand-in also refuses shrouded and non-enemy victims, which this test was not
// read to do)
bool cannotPossiblyAttackObject(const AIState &state, std::uintptr_t userData)
{
	(void)userData;
	Object &obj = state.owner();
	Object *victim = state.machine().goalObject();
	if (victim && victim->isEffectivelyDead())
	{
		const ObjectWeapons *w = obj.getWeapons();
		const Weapon *cur = w ? w->currentWeapon() : nullptr;
		if (cur && cur->getTemplate() && cur->getTemplate()->m_noVictimNeeded)
		{
			return false; // RW 0x7417AF .. 0x7417CF
		}
	}
	if (!victim)
	{
		return false; // RW 0x7417D1 .. 0x7417D7
	}
	return !CombatQueries::isAbleToAttack(obj); // RW 0x7417DB .. 0x7417E2 -> 0x741809
}

// lane GARRISON-2: RW 0x66243F / 0x6622C7 / 0x662317: the turret that holds the current weapon aims at the attack's target; false without one
bool aimTurret(AIUpdateInterface &a, const AIAttackState &att, Object *victim, const Coord3D &pos)
{
	TurretAI *t = a.turretForCurWeapon();
	if (!t)
	{
		return false;
	}
	if (att.isAttackingObject())
	{
		t->setTurretTargetObject(victim, att.isForceAttacking());
	}
	else
	{
		t->setTurretTargetPosition(&pos);
	}
	return true;
}
} // namespace

bool AttackVictimInRange(const AIStateMachine &machine)
{
	ObjectWeapons *w = weaponsOf(machine);
	Object *victim = victimOf(machine);
	if (!w)
	{
		return false;
	}
	if (victim)
	{
		return w->isWithinAttackRange(*victim);
	}
	return w->isWithinAttackRange(machine.goalPosition());
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIAttackState (ZH AIStates.cpp:5358)
// ---------------------------------------------------------------------------------------------------------------------------------
AIAttackState::AIAttackState(AIStateMachine &m, bool follow, bool attackingObject, bool forceAttacking, const AttackExitConditions *exitConditions)
	: AIState(m, "AIAttackState")
	, m_follow(follow)
	, m_isAttackingObject(attackingObject)
	, m_isForceAttacking(forceAttacking)
	, m_exitConditions(exitConditions)
{
}

AIAttackState::~AIAttackState() = default;

bool AIAttackState::chooseWeapon()
{
	// lane PLAY-2, INFERENCE: an attack on a position chooses with no victim (RW 0x66E037 clears the goal object only after setState(9), so the parent's
	// goal object is stale at onEnter; ZH chooseWeapon asks the machine's goal object)
	Object *victim = m_isAttackingObject ? machine().goalObject() : nullptr;
	if (m_isAttackingObject && !victim)
	{
		return false;
	}
	ObjectWeapons *w = owner().getWeapons();
	if (!w)
	{
		return false;
	}
	const bool found = w->chooseBestWeaponForTarget(victim, PREFER_TEMPLATE_DEFAULT, ai().lastCommandSource()); // RW 0x742D95: criteria 5 (lane DECOMP-1)
	w->updateWeaponStatusConditions(); // ZH Object::adjustModelConditionForWeaponStatus
	return found;
}

void AIAttackState::notifyNewVictimChosen(Object *victim)
{
	machine().setGoalObject(victim ? victim->getID() : (ObjectID)INVALID_ID);
	if (m_attackMachine)
	{
		m_attackMachine->setGoalObject(victim ? victim->getID() : (ObjectID)INVALID_ID);
	}
}

std::unique_ptr<AIStateMachine> AIAttackState::createAttackMachine()
{
	AIUpdateInterface &a = ai();
	std::unique_ptr<AIStateMachine> m;
	if (a.attackMachineKind() == ATTACK_MACHINE_HORDE)
	{
		bool melee = false;
		m = a.makeHordeAttackMachine(this, -1, &melee);
		if (m)
		{
			m_machineMade = melee ? MADE_HORDE_MELEE : MADE_HORDE_RANGED;
			return m;
		}
	}
	// lane PHYS-1: attack kind 2 of RW 0x74CED6 makes the melee machine RW 0x744B71 (AIAttackMelee.cpp); the weapon is already chosen
	m_meleeMachine = AttackUsesMeleeMachine(owner(), m_isAttackingObject ? machine().goalObject() : nullptr, m_isAttackingObject);
	if (m_meleeMachine)
	{
		m_machineMade = MADE_MELEE;
		return makeMeleeAttackMachine(a, this);
	}
	m_machineMade = MADE_GENERIC;
	return makeGenericAttackMachine();
}

// B1 AttackStateMachineCtor.cpp:253-297 (the portable structure / continue-state variants are S-325)
std::unique_ptr<AIStateMachine> AIAttackState::makeGenericAttackMachine()
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(ai(), "AIAttackMachine");
	// the default state is the first defined: Pursue 0x64 (here a pass through to Approach, see AIAttack.h)
	struct PursueStub : AIState
	{
		explicit PursueStub(AIStateMachine &mm) : AIState(mm, "AIAttackPursueTargetState") {}
		StateReturnType update() override { return STATE_SUCCESS; }
		void xfer(Xfer &x) override { xferNoFields(x); } // lane SAVE-4c
	};
	// lane SPELL-3 (review r3): the Aim and Fire states of an attack on an object leave the machine with failure when the owner cannot possibly attack it
	// (B1 AttackStateMachineCtor.cpp: d_0016af70 with ATTACK_CONTINUED_TARGET 2 / _FORCED 3 in both the normal and the immobile condition lists; RotWK RW
	// 0x741798, see cannotPossiblyAttackObject). The other object conditions (to Pursue) are not ported (S-325); an attack on a position has none of these
	std::vector<AIStateCondition> conditions;
	if (m_isAttackingObject)
	{
		conditions.push_back({ &cannotPossiblyAttackObject, MACHINE_DONE_FAILURE, (std::uintptr_t)(m_isForceAttacking ? 3 : 2) });
	}
	m->defineState(ATTACK_PURSUE, std::make_unique<PursueStub>(*m), ATTACK_APPROACH, ATTACK_APPROACH);
	m->defineState(ATTACK_APPROACH, std::make_unique<AIAttackApproachState>(*m, this, m_follow), ATTACK_AIM, MACHINE_DONE_FAILURE);
	m->defineState(ATTACK_AIM, std::make_unique<AIAttackAimState>(*m, this), ATTACK_FIRE, MACHINE_DONE_FAILURE, conditions);
	m->defineState(ATTACK_FIRE, std::make_unique<AIAttackFireState>(*m, this), ATTACK_WAIT, ATTACK_APPROACH, conditions);
	m->defineState(ATTACK_WAIT, std::make_unique<AIWaitUntilFinishedFiringState>(*m), ATTACK_AIM, MACHINE_DONE_FAILURE);
	return m;
}

StateReturnType AIAttackState::onEnter()
{
	Object &source = owner();
	AIUpdateInterface &a = ai();
	ObjectWeapons *w = source.getWeapons();
	if (!w || source.testStatus((unsigned)CombatNames::statuses().underConstruction))
	{
		return STATE_FAILURE;
	}
	if (w->isOutOfAmmo() && !source.isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return STATE_FAILURE;
	}
	if (m_isAttackingObject)
	{
		Object *victim = machine().goalObject();
		if (victimGone(victim))
		{
			a.notifyVictimIsDead();
			return STATE_FAILURE;
		}
		m_victimTeam = victim->getTeam() ? victim->getTeam()->getID() : 0u;
		m_originalVictimPos = *victim->getPosition();
	}
	else
	{
		m_originalVictimPos = machine().goalPosition();
	}
	// lane PHYS-1: the weapon is chosen before the machine is made (RW 0x74CED6 reads the current weapon for the attack kind)
	if (!chooseWeapon())
	{
		return STATE_FAILURE;
	}
	m_attackMachine = createAttackMachine();
	if (m_isAttackingObject)
	{
		m_attackMachine->setGoalObject(machine().goalObject()->getID());
	}
	else
	{
		m_attackMachine->setGoalPosition(machine().goalPosition());
	}
	// lane PLAY-2: RW 0x74D0FC (BFME2 decomp AIAttackStateOnEnter.cpp:AIAttackState::onEnter, tier B for RW 0x74CED6): the current weapon's shot limit is
	// lifted (0x7FFFFFFF); privateAttackPosition sets its own limit after the state is entered (RW 0x66E04B)
	if (Weapon *cur = w->currentWeapon())
	{
		cur->setMaxShotCount(0x7FFFFFFF);
	}
	m_lockedSlotOnEnter = w->isCurWeaponLocked() ? w->curSlot() : -1;
	const StateReturnType ret = m_attackMachine->initDefaultState();
	if (ret == STATE_CONTINUE)
	{
		// a horde member with a melee weapon is "melee attacking" while it fights (B1 AIAttackMeleeEngageState): the horde's melee behaviour leaves it alone
		if (!m_meleeMachine && source.testStatus((unsigned)CombatNames::statuses().hordeMember) && w->currentWeapon() && w->currentWeapon()->getTemplate() &&
			w->currentWeapon()->getTemplate()->m_meleeWeapon)
		{
			setStatusBit(source, CombatNames::statuses().isMeleeAttacking, true);
		}
		setStatusBit(source, CombatNames::statuses().isAttacking, true);
		source.setModelConditionState(CombatNames::modelCondition("ATTACKING"), true);
	}
	return ret;
}

StateReturnType AIAttackState::update()
{
	// ZH AIStates.cpp:5582 (lane PLAY-4): the exit conditions met count as a success
	if (m_exitConditions && m_exitConditions->shouldExit(machine()))
	{
		return STATE_SUCCESS;
	}
	Object &source = owner();
	AIUpdateInterface &a = ai();
	ObjectWeapons *w = source.getWeapons();
	if (!w || !m_attackMachine)
	{
		return STATE_FAILURE;
	}
	if (w->isOutOfAmmo() && !source.isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return STATE_FAILURE;
	}
	if (m_isAttackingObject)
	{
		Object *victim = machine().goalObject();
		if (victimGone(victim))
		{
			// a lone unit fighting a horde moves on to the next member while the horde lives
			if (Object *next = a.replacementVictim())
			{
				notifyNewVictimChosen(next);
				victim = next;
			}
			else
			{
				a.notifyVictimIsDead();
				return STATE_SUCCESS; // my, that was easy
			}
		}
		// lane STEALTH-1 (RW 0x75232F, the attack sub-state: RW 0x752382 .. 0x7523B2): a victim that is stealthed and undetected for the attacker's player (RW 0x694C0D) ends
		// the attack (STATE_FAILURE); INFERENCE (S-1040): checked here every update of the attack state, the sub-state RW reads it in is not identified
		static const int kIgnoringStealth = CombatNames::status("IGNORING_STEALTH");
		if (!source.testStatus((unsigned)kIgnoringStealth) && InvisibilityManager::isStealthedAndUndetected(*victim, source.getControllingPlayer()))
		{
			return STATE_FAILURE;
		}
		a.setCurrentVictim(victim);
		// the victim's team changed while we fought it (a capture, a defection): keep attacking only while it is an enemy (ZH AIAttackState::update)
		const ObjectID team = victim->getTeam() ? victim->getTeam()->getID() : 0u;
		if (team != m_victimTeam)
		{
			if (source.getRelationship(*victim) != ENEMIES)
			{
				machine().setGoalObject(INVALID_ID);
				a.notifyVictimIsDead();
				return STATE_FAILURE;
			}
			m_victimTeam = team;
		}
		if (victim->getID() != m_attackMachine->goalObjectID())
		{
			m_attackMachine->setGoalObject(victim->getID());
		}
	}
	if (!chooseWeapon())
	{
		return STATE_FAILURE;
	}
	if (m_lockedSlotOnEnter >= 0 && w->curSlot() != m_lockedSlotOnEnter)
	{
		return STATE_FAILURE; // the locked weapon changed: leave attack mode at once
	}
	// lane PLAY-2: RW 0x751488 .. 0x751494: no current weapon, or it fired its shot limit (an attack on a position with a limit): the attack ends (ZH
	// AIStates.cpp:5677 "we've shot as many times as we are allowed to")
	if (!w->currentWeapon() || w->currentWeapon()->maxShotCount() <= 0)
	{
		return STATE_FAILURE;
	}
	StateReturnType r;
	{
		KeptSubMachines<AIStateMachine>::Run run(m_keptMachines); // the shot can end this state inside the update (onExit below)
		r = m_attackMachine->updateStateMachine();
	}
	if (IS_STATE_SLEEP(r))
	{
		r = STATE_CONTINUE; // ZH CONVERT_SLEEP_TO_CONTINUE
	}
	return r;
}

void AIAttackState::onExit(StateExitType)
{
	m_keptMachines.drop(m_attackMachine); // halted, and kept while its update runs (review of #64)
	Object &obj = owner();
	const CombatNames::Status &st = CombatNames::statuses();
	setStatusBit(obj, st.isFiringWeapon, false);
	setStatusBit(obj, st.isAimingWeapon, false);
	setStatusBit(obj, st.isAttacking, false);
	setStatusBit(obj, st.isMeleeAttacking, false);
	AIUpdateInterface &a = ai();
	a.setCurrentVictim(nullptr);
	machine().setGoalObject(INVALID_ID);
	if (a.isDestroying())
	{
		return; // the drawable is gone: no model condition is touched
	}
	if (TurretAI *t = a.turret())
	{
		t->setTurretTargetObject(nullptr, false); // lane GARRISON-2: RW 0x74D25D -> 0x6622C7(turret 0, none) (ZH AIStates.cpp:5722)
	}
	obj.setModelConditionState(CombatNames::modelCondition("ATTACKING"), false);
	if (ObjectWeapons *w = obj.getWeapons())
	{
		w->updateWeaponStatusConditions();
	}
}

void AIAttackState::crc(StateHasher &h) const
{
	h.addBool(m_attackMachine != nullptr);
	if (m_attackMachine)
	{
		m_attackMachine->crc(h);
		if (m_meleeMachine)
		{
			// the machine hashes only its current state; the retained ones (E5's retry timestamp, E2's wait, ...) are hashed here, in the machine's order (review r6)
			for (const unsigned id : { MELEE_APPROACH, MELEE_ENGAGE, MELEE_REACQUIRE, MELEE_AIM, MELEE_FIRE, MELEE_WAIT, MELEE_SQUISH })
			{
				h.addU32(id);
				m_attackMachine->findState(id)->crc(h);
			}
		}
	}
	h.addBool(m_follow);
	h.addBool(m_isAttackingObject);
	h.addBool(m_isForceAttacking);
	h.addFloat(m_originalVictimPos.x);
	h.addFloat(m_originalVictimPos.y);
	h.addFloat(m_originalVictimPos.z);
	h.addU32(m_victimTeam);
	h.addI32(m_lockedSlotOnEnter);
	h.addU32(m_shotsFired);
	h.addBool(m_meleeMachine);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Approach (ZH AIAttackApproachTargetState, AIStates.cpp:2555-2790)
// ---------------------------------------------------------------------------------------------------------------------------------
AIAttackApproachState::AIAttackApproachState(AIStateMachine &m, AIAttackState *parent, bool follow)
	: AIState(m, "AIAttackApproachTargetState")
	, m_att(parent)
	, m_follow(follow)
{
}

bool AIAttackApproachState::inRange() const
{
	return AttackVictimInRange(machine());
}

StateReturnType AIAttackApproachState::onEnter()
{
	Object &source = owner();
	AIUpdateInterface &a = ai();
	Object *victim = victimOf(machine());
	if (m_att->isAttackingObject() && victimGone(victim))
	{
		return STATE_SUCCESS; // already killed the victim
	}
	m_prevVictimPos = Coord3D{ 0.0f, 0.0f, 0.0f };
	m_approachFrame = a.frame() >= kMinRecomputeFrames ? a.frame() - kMinRecomputeFrames : 0u;
	ObjectWeapons *w = source.getWeapons();
	if (!w || !w->currentWeapon())
	{
		return STATE_FAILURE;
	}
	if (inRange())
	{
		return STATE_SUCCESS; // the view-blocked test (the pathfinder's line of sight) is S-325
	}
	if (source.isKindOf((unsigned)CombatNames::kinds().immobile) || a.isImmobile())
	{
		return STATE_FAILURE; // a structure cannot walk to its target
	}
	const Coord3D goal = victim ? *victim->getPosition() : machine().goalPosition();
	m_prevVictimPos = goal;
	aimTurret(a, *m_att, victim, machine().goalPosition()); // lane GARRISON-2: RW 0x74E6CE before the path
	// lane PHYS-1: the approach's computePath RW 0x749A3E asks for an attack path (RW 0x663802 -> computeAttackPath 0x6668CA -> findAttackPath 0x6FC18E): a free cell in
	// weapon reach, reserved, instead of a move to the victim's centre (the destination is not adjusted). A HORDE object attacks through its horde machine (the approach
	// state 0xC9, computePath RW 0x74A594, S-590); a HORDE that reaches this generic approach keeps the move to the victim (RW 0x749A3E's branch distinctions and the
	// melee machine 0xE1 / 0xE9 of RW 0x744B71 are not ported: S-784 / S-786)
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	if (source.isKindOf((unsigned)kHorde))
	{
		m_move = std::make_unique<AIMoveToState>(a.mover(), goal, true);
		return m_move->onEnter();
	}
	m_move = std::make_unique<AIMoveToState>(a.mover(), goal, false);
	m_move->setAttackTarget(victim ? victim->getID() : PATHFIND_INVALID_ID);
	return m_move->onEnter();
}

StateReturnType AIAttackApproachState::update()
{
	AIUpdateInterface &a = ai();
	Object *victim = victimOf(machine());
	if (m_att->isAttackingObject() && victimGone(victim))
	{
		a.notifyVictimIsDead();
		a.setCurrentVictim(nullptr);
		return STATE_FAILURE;
	}
	if (victim)
	{
		a.setCurrentVictim(victim);
	}
	// stop as soon as the weapon reaches (ZH m_stopIfInRange)
	if (inRange())
	{
		return STATE_SUCCESS;
	}
	if (!m_move)
	{
		return STATE_FAILURE;
	}
	const Coord3D goal = victim ? *victim->getPosition() : machine().goalPosition();
	if (victim && a.frame() - m_approachFrame >= kMinRecomputeFrames)
	{
		// ZH computePath: a victim that moved gets a new path at most every MIN_RECOMPUTE_TIME frames
		m_approachFrame = a.frame();
		if (CombatQueries::centerDistanceSquared2D(goal, m_prevVictimPos) > 1.0f)
		{
			m_prevVictimPos = goal;
			m_move->setGoal(goal);
		}
	}
	const StateReturnType code = m_move->update();
	if (code != STATE_CONTINUE)
	{
		return STATE_SUCCESS; // always success: failure would leave the attack (the aim state re-checks the range)
	}
	return STATE_CONTINUE;
}

void AIAttackApproachState::onExit(StateExitType)
{
	// lane ANIM-1: ZH AIAttackApproachTargetState::onExit (AIStates.cpp:2792) calls AIInternalMoveToState::onExit whether or not the move was entered (the RotWK
	// melee twins do the same, RW 0x74933F / 0x74B716): an archer that was already in range keeps no MOVING from its formation walk
	exitMove(ai().mover(), m_move, m_prevVictimPos);
}

void AIAttackApproachState::crc(StateHasher &h) const
{
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
	h.addFloat(m_prevVictimPos.x);
	h.addFloat(m_prevVictimPos.y);
	h.addFloat(m_prevVictimPos.z);
	h.addU32(m_approachFrame);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Aim (ZH AIAttackAimAtTargetState, AIStates.cpp:4904)
// ---------------------------------------------------------------------------------------------------------------------------------
AIAttackAimState::AIAttackAimState(AIStateMachine &m, AIAttackState *parent, unsigned approachId)
	: AIState(m, "AIAttackAimAtTargetState")
	, m_att(parent)
	, m_approachId(approachId)
{
}

StateReturnType AIAttackAimState::onEnter()
{
	AIUpdateInterface &a = ai();
	ObjectWeapons *w = weaponsOf(machine());
	if (!w || !w->currentWeapon())
	{
		return STATE_FAILURE;
	}
	Locomotor *loco = a.curLocomotor();
	m_canTurnInPlace = loco ? loco->getTemplate().m_minSpeed == 0.0f : false;
	m_setLocomotor = false;
	// lane GARRISON-2: RW 0x753295 aims the turret first
	aimTurret(a, *m_att, victimOf(machine()), machine().goalPosition());
	// lane MODCOMPAT-MOD1 (a pre-existing gap): RW 0x753426 .. 0x753433: an owner with RUNNING_DOWN_FROM_BEHIND (status 0x4B, RW 0x44DDEC) is done aiming at
	// once (STATE_SUCCESS), before IS_AIMING_WEAPON (0x19, RW 0x62684D) is set and the first turn (RW 0x75232F(1)) is made: a unit running a fleeing target
	// down strikes without turning to it
	if (isRunningDown(owner()))
	{
		return STATE_SUCCESS;
	}
	setStatusBit(owner(), CombatNames::statuses().isAimingWeapon, true);
	// lane AIMAIM-1: the FiringArc gates of RW 0x75232F are not ported (reported here, once per aim; no retail template sets FiringArc: S-1022)
	if (static_cast<const ThingTemplate *>(owner().getTemplate())->getFinalOverride()->findField("FiringArc"))
	{
		owner().logic().noteStop(kStopAim);
	}
	// lane AIMAIM-1: RW 0x753443 .. 0x753447: onEnter answers updateInternal(first call): the first turn is made at once
	return aim(true);
}

StateReturnType AIAttackAimState::update()
{
	// the victim moved out of reach: walk again. RotWK's aim never does this itself (RW 0x75232F); the attack machine's state condition
	// outOfWeaponRangeObject (to the chase state, BFME2 AttackStateMachine.cpp) does, and those conditions are not ported (S-325): this test stands in for it
	// (B1 conditions of 0x66; the melee machine's 0xE1)
	ObjectWeapons *w = weaponsOf(machine());
	Object *victim = victimOf(machine());
	if (w && w->hasAnyWeapon() && !(m_att->isAttackingObject() && victimGone(victim)) && !AttackVictimInRange(machine()))
	{
		if (ai().isImmobile())
		{
			return STATE_FAILURE;
		}
		machine().setState(m_approachId);
		return STATE_CONTINUE;
	}
	return aim(false); // RW 0x753456
}

// RW 0x75232F AIAttackAimAtTargetState::updateInternal(firstCall) (lane AIMAIM-1; BFME2 1.06 the same rule, decomp AIAttackAimAtTargetStateUpdateInternal.cpp:196)
StateReturnType AIAttackAimState::aim(bool firstCall)
{
	AIUpdateInterface &a = ai();
	Object &self = owner();
	ObjectWeapons *w = weaponsOf(machine());
	// RW 0x752345 .. 0x75235D: no AI or no weapon (RW 0x68B4AF) -> FAILURE
	if (!w || !w->hasAnyWeapon())
	{
		return STATE_FAILURE;
	}
	Object *victim = victimOf(machine());
	const Weapon *weapon = w->currentWeapon(); // RW 0x68B58C(0)
	const WeaponTemplate *wt = weapon ? weapon->getTemplate() : nullptr;
	if (m_att->isAttackingObject())
	{
		// RW 0x752382 .. 0x7523B2: no victim, a dead one (+ 0x458 bit 0), a victim with status FLEE_OFF_MAP (0x32) or stealthed and undetected for the owner's
		// player (RW 0x694C0D) -> FAILURE
		static const int kFleeOffMap = CombatNames::status("FLEE_OFF_MAP");
		static const int kIgnoringStealth = CombatNames::status("IGNORING_STEALTH");
		if (victimGone(victim) || victim->testStatus((unsigned)kFleeOffMap))
		{
			return STATE_FAILURE;
		}
		if (!self.testStatus((unsigned)kIgnoringStealth) && InvisibilityManager::isStealthedAndUndetected(*victim, self.getControllingPlayer()))
		{
			return STATE_FAILURE;
		}
	}
	// lane GARRISON-2: RW 0x7523F8 .. 0x752443: the turret that holds the current weapon aims (and fires: TurretAI's machine); a turret that turns keeps the
	// body in this state
	if (aimTurret(a, *m_att, victim, machine().goalPosition()) && a.turretTurnRate() != 0.0f)
	{
		return STATE_CONTINUE;
	}
	// RW 0x752448 .. 0x7524B0: the tolerance is the weapon's AcceptableAimDelta (+ 0x2C, 0 without a weapon), at least 0.035 (RW 0xC2A1BC), at least 0.3
	// (RW 0xBE29D4) for a DISABLED_HELD owner (Object + 0x1C8 bit 3); the turn rate is the current locomotor's (RW 0x5E372D), 0 without one
	float tolerance = wt ? wt->m_aimDelta : 0.0f;
	if (kRelativeAngleThreshold > tolerance)
	{
		tolerance = kRelativeAngleThreshold;
	}
	float turnRate = 0.0f;
	if (Locomotor *loco = a.curLocomotor())
	{
		turnRate = loco->getMaxTurnRate(a.locomotorHost());
	}
	if ((self.getDisabledMask() & 0x8u) != 0 && 0.3f > tolerance)
	{
		tolerance = 0.3f;
	}
	// lane MODCOMPAT-MOD1 (a pre-existing gap): RW 0x7524B5 .. 0x7524C1, before the turn (vslot 0x21C / 0x210): an owner with RUNNING_DOWN_FROM_BEHIND (status
	// 0x4B) succeeds without turning. The port turned it every frame: a horde member running a fleeing target down (the run-down RW 0x87631F orders it to
	// attack) took an angle goal each frame, which replaced the walk its horde's pass gave it (RW 0x877B69: a running-down member of a moving horde is driven
	// to its slot), so it stood with MOVING beside its marching horde until the leash took it (RW 0x877B4F)
	if (isRunningDown(self))
	{
		return STATE_SUCCESS;
	}
	// RW 0x7524C6 .. 0x752561: the point aimed at is the machine's goal position (machine + 0x24); on an object, a melee weapon (RW 0x441B59; RW 0x9188EB, the
	// other test, answers false) at a WALK_ON_TOP_OF_WALL (60) or BLOCKING_GATE (137) victim aims at the victim's "Ram" contact point (RW 0xC2A1C0; RW
	// 0x690BD2(caller position, mode 0, not preferred)); without one, a victim that is not a WALL_UPGRADE (150) is aimed at its position
	Coord3D target = machine().goalPosition();
	if (m_att->isAttackingObject())
	{
		static const int kWallTop = CombatNames::kindOf("WALK_ON_TOP_OF_WALL");
		static const int kBlockingGate = CombatNames::kindOf("BLOCKING_GATE");
		static const int kWallUpgrade = CombatNames::kindOf("WALL_UPGRADE");
		bool contact = false;
		if (wt && (victim->isKindOf((unsigned)kWallTop) || victim->isKindOf((unsigned)kBlockingGate)) && wt->m_meleeWeapon)
		{
			contact = ObjectGeometry::worldspaceBestContactPoint(*victim, *self.getPosition(), "Ram", false, target);
		}
		if (!victim->isKindOf((unsigned)kWallUpgrade) && !contact)
		{
			target = *victim->getPosition();
		}
	}
	float relAngle = emotionRelAngle(self, target); // RW 0x4B3D8D
	// RW 0x7525D9 .. 0x752602: a weapon with an AimDirection (+ 0x30) above 0 aims that much off: normalizeAngle(rel - AimDirection) (RW 0x644FD0)
	if (wt && wt->m_aimDirection > 0.0f)
	{
		relAngle = normalizeAngle(SimMath::subf32(relAngle, wt->m_aimDirection));
	}
	// RW 0x752606 .. 0x752664: a unit that turns in place takes the goal angle (vslot 0x21C, its angle + rel, SSE) while the tolerance is below pi (RW 0xBDD388)
	// and the angle is above it or this is the first call; another walks at the point (vslot 0x210)
	const double absAngle = SimMath::absD((double)relAngle);
	if (m_canTurnInPlace)
	{
		if (3.14159274f > tolerance && ((double)tolerance < absAngle || firstCall))
		{
			a.setLocomotorGoalOrientation(SimMath::addf32(self.getOrientation(), relAngle));
			m_setLocomotor = true;
		}
	}
	else
	{
		a.setLocomotorGoalPositionExplicit(target);
	}
	// RW 0x75266A .. 0x752749: an owner INSIDE_GARRISON (0x3A) shoots with its horde's weapon when it is a horde member (RW 0x6939DF, the container id + 0x78),
	// else its own: no current weapon there, or the victim (the goal position) out of its range (RW 0x6CC653 / 0x6CC622, extra 0, min range checked) -> FAILURE
	static const int kInsideGarrison = CombatNames::status("INSIDE_GARRISON");
	if (self.testStatus((unsigned)kInsideGarrison))
	{
		Object *shooter = hordeOf(self);
		if (!shooter)
		{
			shooter = &self;
		}
		ObjectWeapons *sw = shooter->getWeapons();
		if (!sw || !sw->currentWeapon())
		{
			return STATE_FAILURE;
		}
		if (!(victim ? sw->isWithinAttackRange(*victim) : sw->isWithinAttackRange(machine().goalPosition())))
		{
			return STATE_FAILURE;
		}
	}
	// RW 0x7526CC .. 0x7526DD: RW 0x744B25(victim) fails the aim when the victim (its horde, for a member) has an AI container (+ 0x27C); a horde is in none,
	// and the port keeps no other container there: INFERENCE (S-7175), always passes
	if (firstCall)
	{
		return STATE_CONTINUE; // RW 0x7526E7
	}
	// RW 0x7526ED .. 0x752775: aimed when the angle is within the tolerance or within half the turn rate (RW 0xBD869C): then the victim's AI slot 0x204 (this
	// owner's id, 1) and slot 0x208 may hold the attacker (CONTINUE); INFERENCE (S-7175, as S-1106): every ported AI class answers no (RW 0x9188EB)
	if ((double)tolerance > absAngle || (double)SimMath::mulf32(turnRate, 0.5f) > absAngle)
	{
		return STATE_SUCCESS;
	}
	return STATE_CONTINUE;
}

void AIAttackAimState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return;
	}
	if (m_canTurnInPlace && m_setLocomotor)
	{
		ai().setLocomotorGoalNone();
	}
	setStatusBit(owner(), CombatNames::statuses().isAimingWeapon, false);
}

void AIAttackAimState::crc(StateHasher &h) const
{
	h.addBool(m_setLocomotor);
	h.addBool(m_canTurnInPlace);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Fire (ZH AIAttackFireWeaponState; RW 0x74C258 / 0x74C4A7)
// ---------------------------------------------------------------------------------------------------------------------------------
AIAttackFireState::AIAttackFireState(AIStateMachine &m, AttackFireNotify *notify)
	: AIState(m, "AIAttackFireWeaponState")
	, m_att(notify)
{
}

StateReturnType AIAttackFireState::onEnter()
{
	Object &obj = owner();
	ObjectWeapons *w = obj.getWeapons();
	if (!w || !w->currentWeapon())
	{
		return STATE_FAILURE;
	}
	if (!AttackVictimInRange(machine()))
	{
		return STATE_FAILURE;
	}
	if (w->currentStatus() != WEAPON_READY_TO_FIRE)
	{
		return STATE_SUCCESS;
	}
	m_deferred = false;
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) && (ai().frame() & 1u))
	{
		m_deferred = true; // a horde member on an odd frame starts one frame later (RW 0x74C258)
		return STATE_CONTINUE;
	}
	setStatusBit(obj, CombatNames::statuses().isFiringWeapon, true);
	Object *victim = victimOf(machine());
	w->preFireCurrentWeapon(victim, victim ? nullptr : &machine().goalPosition());
	return STATE_CONTINUE;
}

StateReturnType AIAttackFireState::update()
{
	Object &obj = owner();
	Object *victim = victimOf(machine());
	if (m_att->isAttackingObject() && victimGone(victim))
	{
		return STATE_FAILURE;
	}
	ObjectWeapons *w = obj.getWeapons();
	if (!w || !w->currentWeapon())
	{
		return STATE_FAILURE;
	}
	if (m_deferred)
	{
		m_deferred = false;
		setStatusBit(obj, CombatNames::statuses().isFiringWeapon, true);
		w->preFireCurrentWeapon(victim, victim ? nullptr : &machine().goalPosition());
	}
	const int status = w->currentStatus();
	if (status == WEAPON_PRE_ATTACK)
	{
		return STATE_CONTINUE;
	}
	if (status != WEAPON_READY_TO_FIRE)
	{
		return STATE_FAILURE;
	}
	// lane GARRISON-2: RW 0x74C4A7 asks the owner (notify slot 8) whether the current slot may fire: a turret fires only its own slots (ZH AIStates.cpp:5229)
	if (!m_att->isWeaponSlotOkToFire(w->curSlot()))
	{
		return STATE_FAILURE;
	}
	// RW 0x74C4A7 does not test the reach again once the wind-up started (only onEnter RW 0x74C258 asks RW 0x6CC653): the shot is fired and spent; whether it
	// lands is decided by the weapon's delivery range gate (RW 0x6CC915, ObjectWeapons::deliveryRangeAllows): a victim that escaped the range is not struck,
	// unless the weapon is a LeechRangeWeapon inside its leech window (Weapon + 0x50), as the Angmar Dunedain's sword
	w->setFiringConditionForCurrentWeapon(); // RW 0x74C4A7 -> 0x69036C (lane PROJ-2): FIRING_x before the shot, so the launch bone is the attack pose
	w->fireCurrentWeapon(victim, victim ? nullptr : &machine().goalPosition());
	m_att->notifyFired();
	return STATE_SUCCESS;
}

void AIAttackFireState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return; // the object is being deleted: its modules are going away
	}
	// lane ATKRESET-1: AIAttackFireWeaponState::onExit RW 0x74C8BE (vtable 0xC27858 slot 5; BFME2 0x74B08A identical, decomp
	// AIAttackFireWeaponStateOnExit.cpp): IS_FIRING_WEAPON (0x0D) and IGNORING_STEALTH (0x1B) are cleared, the current weapon's status is written back
	// (RW 0x6CDCE7); a swing cancelled before it landed (PRE_ATTACK) ends (+ 0x1C = 0) and the status is written back again; then END_FIRE_STATE (0x52)
	static const int kIgnoringStealth = CombatNames::status("IGNORING_STEALTH");
	static const int kEndFireState = CombatNames::status("END_FIRE_STATE");
	owner().setStatus((unsigned)CombatNames::statuses().isFiringWeapon, false);
	owner().setStatus((unsigned)kIgnoringStealth, false);
	ObjectWeapons *w = owner().getWeapons();
	if (Weapon *cur = w ? w->currentWeapon() : nullptr)
	{
		if (w->weaponStatusWriteBack(*cur) == WEAPON_PRE_ATTACK)
		{
			cur->setWhenPreAttackFinished(0);
			w->weaponStatusWriteBack(*cur);
		}
	}
	owner().setStatus((unsigned)kEndFireState, false);
}

void AIAttackFireState::crc(StateHasher &h) const
{
	h.addBool(m_deferred);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// WaitUntilFinishedFiring (RW 0x7479B7 / 0x742CEF / 0x742D42, decisions: WEAPON-1)
// ---------------------------------------------------------------------------------------------------------------------------------
StateReturnType AIWaitUntilFinishedFiringState::onEnter()
{
	Object &obj = owner();
	ObjectWeapons *w = obj.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate())
	{
		return STATE_FAILURE;
	}
	const bool oddFrame = obj.testStatus((unsigned)CombatNames::statuses().hordeMember) && (ai().frame() & 1u);
	const WaitEnterResult r = WeaponWaitUntilFinishedFiringEnter(*weapon->getTemplate(), w->currentStatus(), oddFrame, ai().frame());
	if (r.lockWeapon)
	{
		w->setWeaponLock(w->curSlot(), LOCKED_TEMPORARILY);
	}
	if (r.setDisabled)
	{
		obj.setDisabled(8, r.disabledUntil);
	}
	return (StateReturnType)r.state;
}

StateReturnType AIWaitUntilFinishedFiringState::update()
{
	ObjectWeapons *w = owner().getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate())
	{
		return STATE_FAILURE;
	}
	return (StateReturnType)WeaponWaitUntilFinishedFiringUpdate(*weapon->getTemplate(), w->currentStatus(), weapon->lastFireFrame(), ai().frame());
}

void AIWaitUntilFinishedFiringState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return;
	}
	if (ObjectWeapons *w = owner().getWeapons())
	{
		w->releaseWeaponLock(LOCKED_TEMPORARILY);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIDeadState (ZH AIStates.cpp:1476-1545): dying objects are never firing or moving; the DYING condition stays until the state exits
// ---------------------------------------------------------------------------------------------------------------------------------
StateReturnType AIDeadState::onEnter()
{
	Object &obj = owner();
	Object::ModelConditionBits clear{};
	const char *const names[] = { "USING_WEAPON_A", "USING_WEAPON_B", "USING_WEAPON_C", "USING_WEAPON_D", "USING_WEAPON_E", "FIRING_A", "FIRING_B", "FIRING_C", "FIRING_D",
		"FIRING_E", "BETWEEN_FIRING_SHOTS_A", "BETWEEN_FIRING_SHOTS_B", "BETWEEN_FIRING_SHOTS_C", "BETWEEN_FIRING_SHOTS_D", "BETWEEN_FIRING_SHOTS_E", "RELOADING_A",
		"RELOADING_B", "RELOADING_C", "RELOADING_D", "RELOADING_E", "PREATTACK_A", "PREATTACK_B", "PREATTACK_C", "PREATTACK_D", "PREATTACK_E", "FIRING_OR_PREATTACK_A",
		"FIRING_OR_PREATTACK_B", "FIRING_OR_PREATTACK_C", "FIRING_OR_PREATTACK_D", "FIRING_OR_PREATTACK_E", "FIRING_OR_RELOADING_A", "FIRING_OR_RELOADING_B",
		"FIRING_OR_RELOADING_C", "FIRING_OR_RELOADING_D", "FIRING_OR_RELOADING_E", "ATTACKING", "MOVING" };
	for (const char *n : names)
	{
		const int bit = CombatNames::modelCondition(n);
		clear[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	Object::ModelConditionBits set{};
	const int dying = CombatNames::modelCondition("DYING");
	set[(size_t)dying >> 5] |= 1u << (dying & 31);
	obj.clearAndSetModelConditionFlags(clear, set);
	const CombatNames::Status &st = CombatNames::statuses();
	obj.setStatus((unsigned)st.isAttacking, false);
	obj.setStatus((unsigned)st.isFiringWeapon, false);
	obj.setStatus((unsigned)st.isAimingWeapon, false);
	obj.setStatus((unsigned)st.isMeleeAttacking, false);
	return STATE_CONTINUE;
}

StateReturnType AIDeadState::update()
{
	ai().setLocomotorGoalNone();
	return STATE_CONTINUE;
}

void AIDeadState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return;
	}
	owner().setModelConditionState(CombatNames::modelCondition("DYING"), false);
}

// lane AI-2: RW 0x740ECD (AIAttackState vtable RW 0xC2A1D8 slot 0x24) = !RW 0x742DD0 (slot 0x20)
bool AIAttackState::isActive() const
{
	if (!m_attackMachine)
	{
		return true; // RW 0x742DEA: slot 0x20 answers false
	}
	const AIState *cur = m_attackMachine->currentState();
	return cur ? !cur->isIdle() : false; // RW 0x742DE7: no current state -> slot 0x20 true
}
