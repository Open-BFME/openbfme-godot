// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIAttack.h for the target facts and the donors.

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include "Common/StateHash.h"
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

void setStatusBit(Object &o, int bit, bool on)
{
	o.setStatus((unsigned)bit, on);
}

bool victimGone(const Object *victim)
{
	return !victim || !CombatQueries::isAlive(*victim);
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
AIAttackState::AIAttackState(AIStateMachine &m, bool follow, bool attackingObject, bool forceAttacking)
	: AIState(m, "AIAttackState")
	, m_follow(follow)
	, m_isAttackingObject(attackingObject)
	, m_isForceAttacking(forceAttacking)
{
}

AIAttackState::~AIAttackState() = default;

bool AIAttackState::chooseWeapon()
{
	Object *victim = machine().goalObject();
	if (m_isAttackingObject && !victim)
	{
		return false;
	}
	ObjectWeapons *w = owner().getWeapons();
	if (!w)
	{
		return false;
	}
	const bool found = w->chooseBestWeaponForTarget(victim, PREFER_MOST_DAMAGE, ai().lastCommandSource());
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
		m = a.makeHordeAttackMachine(this);
		if (m)
		{
			return m;
		}
	}
	// lane PHYS-1: attack kind 2 of RW 0x74CED6 makes the melee machine RW 0x744B71 (AIAttackMelee.cpp); the weapon is already chosen
	m_meleeMachine = AttackUsesMeleeMachine(owner(), m_isAttackingObject ? machine().goalObject() : nullptr, m_isAttackingObject);
	if (m_meleeMachine)
	{
		return makeMeleeAttackMachine(a, this);
	}
	// B1 AttackStateMachineCtor.cpp:253-297 (the portable structure / continue-state variants are S-325)
	m = std::make_unique<AIStateMachine>(a, "AIAttackMachine");
	// the default state is the first defined: Pursue 0x64 (here a pass through to Approach, see AIAttack.h)
	struct PursueStub : AIState
	{
		explicit PursueStub(AIStateMachine &mm) : AIState(mm, "AIAttackPursueTargetState") {}
		StateReturnType update() override { return STATE_SUCCESS; }
	};
	const bool immobile = owner().testStatus((unsigned)CombatNames::statuses().canAttack) && false;
	(void)immobile;
	m->defineState(ATTACK_PURSUE, std::make_unique<PursueStub>(*m), ATTACK_APPROACH, ATTACK_APPROACH);
	m->defineState(ATTACK_APPROACH, std::make_unique<AIAttackApproachState>(*m, this, m_follow), ATTACK_AIM, MACHINE_DONE_FAILURE);
	m->defineState(ATTACK_AIM, std::make_unique<AIAttackAimState>(*m, this), ATTACK_FIRE, MACHINE_DONE_FAILURE);
	m->defineState(ATTACK_FIRE, std::make_unique<AIAttackFireState>(*m, this), ATTACK_WAIT, ATTACK_APPROACH);
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
	StateReturnType r = m_attackMachine->updateStateMachine();
	if (IS_STATE_SLEEP(r))
	{
		r = STATE_CONTINUE; // ZH CONVERT_SLEEP_TO_CONTINUE
	}
	return r;
}

void AIAttackState::onExit(StateExitType)
{
	if (m_attackMachine)
	{
		m_attackMachine->halt();
		m_attackMachine.reset();
	}
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
	setStatusBit(owner(), CombatNames::statuses().isAimingWeapon, true);
	return STATE_CONTINUE;
}

StateReturnType AIAttackAimState::update()
{
	AIUpdateInterface &a = ai();
	ObjectWeapons *w = weaponsOf(machine());
	if (!w || !w->hasAnyWeapon())
	{
		return STATE_FAILURE;
	}
	Object *victim = victimOf(machine());
	if (m_att->isAttackingObject() && victimGone(victim))
	{
		return STATE_FAILURE; // cannot aim at dead things
	}
	if (!AttackVictimInRange(machine()))
	{
		if (a.isImmobile())
		{
			return STATE_FAILURE;
		}
		machine().setState(m_approachId); // the victim moved out of reach: walk again (B1 conditions of 0x66; the melee machine's 0xE1)
		return STATE_CONTINUE;
	}
	// lane GARRISON-2: RW 0x75232F: the turret that holds the current weapon aims (and fires: TurretAI's machine); a turret that turns keeps the body in this state
	if (aimTurret(a, *m_att, victim, machine().goalPosition()) && a.turretTurnRate() != 0.0f)
	{
		return STATE_CONTINUE;
	}
	const Coord3D target = victim ? *victim->getPosition() : machine().goalPosition();
	const float relAngle = a.relativeAngleTo(target);
	float aimDelta = w->aimDelta();
	if (aimDelta < kRelativeAngleThreshold)
	{
		aimDelta = kRelativeAngleThreshold;
	}
	const float absAngle = relAngle < 0.0f ? SimMath::subf32(0.0f, relAngle) : relAngle;
	if (m_canTurnInPlace)
	{
		if (absAngle > aimDelta)
		{
			a.setLocomotorGoalOrientation(SimMath::addf32(owner().getOrientation(), relAngle));
			m_setLocomotor = true;
		}
	}
	else
	{
		a.setLocomotorGoalPositionExplicit(target);
	}
	if (absAngle < aimDelta)
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
	owner().setStatus((unsigned)CombatNames::statuses().isFiringWeapon, false);
	ObjectWeapons *w = owner().getWeapons();
	if (w && w->currentWeapon() && w->currentStatus() == WEAPON_PRE_ATTACK)
	{
		// a swing that was cancelled before it landed (the victim moved away): the pre-attack ends (ZH AIAttackFireWeaponState::onExit)
		w->currentWeapon()->setWhenPreAttackFinished(0);
	}
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
