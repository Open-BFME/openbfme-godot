// OpenBFME. GPL-3.0.
// PORT-165 (fork issue 394): states of the horde attack-on-position machine RW 0x74548D.
#include "GameLogic/AI/AIAttackHordePosition.h"

#include "Common/StateHash.h"
#include "Common/GameCommon.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <stdexcept>

namespace
{
constexpr const char *kProjectilePreciseZStop = "[S-7433] position approach: projectile locomotor precise-Z flag is not ported (RW 0x74E9EC / 0x74A51B)";
}

// RW 0x741564 (~AttackPositionHordeStateMachine), RW 0x7455B4 (deleting dtor), RW 0x7415B1 (getName)
// RW 0x86D562 (HordeContain::allMembersAttacking, 0xC5B1F8[83]), RW 0x86D438 (HordeContain::allMembersAttackingTarget, 0xC5B1F8[82])
AIHordePositionWaitState::AIHordePositionWaitState(AIStateMachine &machine)
	: AIState(machine, "AIHordePositionWaitState")
{
}

StateReturnType AIHordePositionWaitState::onEnter()
{
	// RW 0x747999: KindOf HORDE (+0x114 mask 0x2000) returns 0, otherwise -2.
	return owner().isKindOfName("HORDE") ? STATE_CONTINUE : STATE_FAILURE;
}

StateReturnType AIHordePositionWaitState::update()
{
	// RW 0x742CEA -> 0x742C8C: a dead goal fails; otherwise the horde's
	// interface is asked whether every living AI member attacks the goal (slot
	// 0x148), or is active when there is no goal (slot 0x14C).
	Object *goal = machine().goalObject();
	if (goal && goal->isEffectivelyDead())
	{
		return STATE_FAILURE;
	}
	ContainModuleInterface *contain = owner().getContain();
	if (!contain || !contain->getHordeContainInterface() || !contain->getContainedItemsList())
	{
		throw std::logic_error("AIHordePositionWaitState: HORDE has no horde containment interface");
	}
	const auto members = *contain->getContainedItemsList(); // RW 0x865598 takes a copy
	for (const Object *member : members)
	{
		if (!member || member->isEffectivelyDead())
		{
			continue;
		}
		const AIUpdateInterface *memberAI = member->getAIUpdateInterface();
		if (!memberAI)
		{
			continue;
		}
		// RW 0x0086D562 / RW 0x0086D438 (S-7505: horde-target contain order open) query AI vslot 0x1BC (active, not
		// merely the attack-state id). An empty eligible member list passes.
		if (!memberAI->isStateActive())
		{
			return STATE_FAILURE;
		}
		if (goal)
		{
			Object *victim = memberAI->currentVictim(); // RW 0x668303
			if (!victim || victim->getID() != goal->getID())
			{
				return STATE_FAILURE;
			}
			if (goal->isKindOfName("HORDE"))
			{
				ContainModuleInterface *targetContain = goal->getContain();
				if (!targetContain || !targetContain->getContainedItemsList())
				{
					throw std::logic_error("AIHordePositionWaitState: target HORDE has no containment list");
				}
				const auto &targetMembers = *targetContain->getContainedItemsList();
				if (std::find(targetMembers.begin(), targetMembers.end(), victim) == targetMembers.end())
				{
					return STATE_FAILURE; // RW 0x86D4A4: target contain vslot 0xE8
				}
			}
		}
	}
	return STATE_CONTINUE;
}

// RW 0x74C911 / 0x74C9FF / 0x74C8BE: the position variant shares
// the exit routine with the ordinary fire state, but has its own entry and
// update. Its +0x24 deferred flag is simulation state and is saved below.
AIHordePositionFireState::AIHordePositionFireState(AIStateMachine &machine, AttackFireNotify &notify)
	: AIState(machine, "AIHordePositionFireState"), m_notify(&notify)
{
}

StateReturnType AIHordePositionFireState::onEnter()
{
	Object &obj = owner();
	m_deferred = false;
	obj.setStatus((unsigned)CombatNames::status("END_FIRE_STATE"), false);
	if ((ai().moodAdjustment(2) & 1u) == 0)
	{
		return STATE_FAILURE;
	}
	ObjectWeapons *weapons = obj.getWeapons();
	Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate())
	{
		return STATE_FAILURE;
	}
	const WeaponTemplate &tmpl = *weapon->getTemplate();
	// RW 0x74C95E: an immobile owner, or a moving-fire / melee weapon,
	// bypasses the ordinary moving-owner refusal.
	if (!obj.isKindOfName("IMMOBILE") && !tmpl.m_canFireWhileMoving && !tmpl.m_meleeWeapon && ai().isMoving())
	{
		return STATE_FAILURE;
	}
	if (!weapons->isWithinAttackRange(machine().goalPosition()))
	{
		return STATE_FAILURE;
	}
	if (weapons->weaponStatusWriteBack(*weapon) != WEAPON_READY_TO_FIRE)
	{
		return STATE_SUCCESS;
	}
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) && (ai().frame() & 1u))
	{
		m_deferred = true;
		return STATE_CONTINUE;
	}
	obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, true);
	weapons->preFireCurrentWeapon(machine().goalObject(), &machine().goalPosition());
	return STATE_CONTINUE;
}

StateReturnType AIHordePositionFireState::update()
{
	Object &obj = owner();
	ObjectWeapons *weapons = obj.getWeapons();
	Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr;
	const int endFire = CombatNames::status("END_FIRE_STATE");
	if (!weapon || obj.isEffectivelyDead() || obj.testStatus((unsigned)endFire))
	{
		return STATE_FAILURE;
	}
	if (m_deferred)
	{
		m_deferred = false;
		obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, true);
		weapons->preFireCurrentWeapon(machine().goalObject(), &machine().goalPosition());
		return STATE_CONTINUE;
	}
	const int status = weapons->weaponStatusWriteBack(*weapon);
	if (status == WEAPON_PRE_ATTACK)
	{
		return STATE_CONTINUE;
	}
	if (status != WEAPON_READY_TO_FIRE || !m_notify->isWeaponSlotOkToFire(weapons->curSlot()))
	{
		return STATE_FAILURE;
	}
	weapons->setFiringConditionForCurrentWeapon(); // RW 0x69036C
	weapons->fireCurrentWeapon(nullptr, &machine().goalPosition()); // RW 0x6920AD
	obj.setStatus((unsigned)CombatNames::status("IGNORING_STEALTH"), false);
	m_notify->notifyFired();
	return STATE_SUCCESS;
}

void AIHordePositionFireState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return;
	}
	Object &obj = owner();
	obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, false);
	obj.setStatus((unsigned)CombatNames::status("IGNORING_STEALTH"), false);
	ObjectWeapons *weapons = obj.getWeapons();
	if (Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr)
	{
		if (weapons->weaponStatusWriteBack(*weapon) == WEAPON_PRE_ATTACK)
		{
			weapon->setWhenPreAttackFinished(0);
			weapons->weaponStatusWriteBack(*weapon);
		}
	}
	obj.setStatus((unsigned)CombatNames::status("END_FIRE_STATE"), false);
}

void AIHordePositionFireState::crc(StateHasher &hasher) const
{
	hasher.addBool(m_deferred);
}

void AIHordePositionFireState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_deferred);
}

// Position approach, RW 0x74E9DD / 0x74A3E2 / 0x741EF3 / 0x74A4EE.
AIHordePositionApproachState::AIHordePositionApproachState(AIStateMachine &machine)
	: AIState(machine, "AIHordePositionApproachState")
{
}

void AIHordePositionApproachState::ensureMove()
{
	if (!m_move)
	{
		m_move = std::make_unique<AIMoveToState>(ai().mover(), Coord3D{0.0f, 0.0f, 0.0f}, false);
		m_move->setComputePathHook(this);
	}
}

bool AIHordePositionApproachState::computeSpecialPath(AIMoveToState &move)
{
	// RW 0x741EF3: mobile only; a pending request keeps its goal. Refresh the
	// goal at most once per second while a path exists; only a pathless unit
	// actually queues the attack path to the position.
	if (!GarrisonRules::isMobile(owner())) return false;
	if (move.waitingForPath()) return true;
	AIMover &mover = ai().mover();
	const bool noPath = !mover.path() && !mover.isWaitingForPath();
	if (!noPath && ai().frame() - m_pathFrame < (unsigned)LOGICFRAMES_PER_SECOND) return true;
	m_pathFrame = ai().frame();
	move.setAdjustsDestination(true);
	m_stopIfInRange = false;
	move.setGoal(machine().goalPosition());
	if (noPath)
	{
		mover.requestAttackPath(PATHFIND_INVALID_ID, move.goal());
		move.setWaitingForPath(mover.isWaitingForPath());
	}
	return true;
}

StateReturnType AIHordePositionApproachState::onEnter()
{
	Object &obj = owner();
	if (obj.isKindOfName("PROJECTILE") && ai().curLocomotor())
	{
		// RW 0x74E9EC..0x74EA0A sets the shared locomotor precise-Z flag;
		// preserve the port's prior movement and report the unsupported branch.
		obj.logic().noteStop(kProjectilePreciseZStop);
	}
	m_ownerPosition = *obj.getPosition();
	m_waiting = false;
	m_pathFrame = 0u - (unsigned)LOGICFRAMES_PER_SECOND;
	ObjectWeapons *weapons = obj.getWeapons();
	if (!weapons || !weapons->currentWeapon()) return STATE_FAILURE;
	if (weapons->isWithinAttackRange(machine().goalPosition())) return STATE_SUCCESS;
	if (!AIAttackSquadState::canChase(obj) || (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) && obj.getContainedBy()))
		return STATE_FAILURE;
	if (!GarrisonRules::isMobile(obj) || ai().curLocomotorSpeed() < 0.1f)
	{
		m_waiting = true;
		m_waitFrame = ai().frame() + 2u * (unsigned)LOGICFRAMES_PER_SECOND;
		return STATE_CONTINUE;
	}
	if (TurretAI *turret = ai().turretForCurWeapon()) turret->setTurretTargetPosition(&machine().goalPosition());
	ensureMove();
	if (!computeSpecialPath(*m_move)) return STATE_FAILURE;
	m_move->setAdjustsDestination(false);
	const StateReturnType result = m_move->onEnter();
	m_move->setAdjustsDestination(true);
	return result;
}

StateReturnType AIHordePositionApproachState::update()
{
	ensureMove();
	ObjectWeapons *weapons = owner().getWeapons();
	Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr;
	// RW 0x74A3F5: stop in range after the attack request is consumed, or
	// while waiting for a speedless owner; melee weapons suppress that stop.
	m_stopIfInRange = !ai().mover().attackRequestPending() || m_waiting;
	if (weapon && weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon) m_stopIfInRange = false;
	StateReturnType result;
	if (m_stopIfInRange && weapon && weapons->isWithinAttackRange(m_move->goal())) result = STATE_SUCCESS;
	else if (!computeSpecialPath(*m_move)) result = STATE_FAILURE;
	else result = m_move->update();
	// RW 0x74A49B: during the initial approach a weapon turret can aim at a
	// mood target, separately from the machine's position goal.
	if (m_initialApproach)
	{
		if (ai().turretForCurWeapon() && ai().getNextMoodTarget(true, false))
		{
			// RW 0x74A4DC..0x74A4E1 aims the turret and sets AI +0x3C7;
			// leave the previous targeting behavior unchanged (Port 200 / S-1189).
			owner().logic().noteStop("[S-1189] position approach: turret mood-target update and AI +0x3C7 are not ported (RW 0x74A4DC)");
		}
	}
	return result;
}

void AIHordePositionApproachState::onExit(StateExitType)
{
	if (ai().isDestroying()) return;
	ensureMove();
	m_move->onExit(); // RW 0x748D8A
	ai().mover().ignoreObstacle(PATHFIND_INVALID_ID);
	// RW 0x74A51B..0x74A531 clears the same projectile locomotor flag on exit;
	// note it here as well when loading directly into an active approach.
	if (owner().isKindOfName("PROJECTILE") && ai().curLocomotor()) owner().logic().noteStop(kProjectilePreciseZStop);
	if (ai().isDoingGroundMovement())
	{
		const Coord3D &goal = m_move->goal();
		const Coord3D &pos = *owner().getPosition();
		const float dx = SimMath::subf32(goal.x, pos.x), dy = SimMath::subf32(goal.y, pos.y);
		if (SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) < 12.5f) owner().setPosition(&goal);
	}
	ai().setLocomotorGoalNone();
	m_initialApproach = false;
}

void AIHordePositionApproachState::onTemporaryStateEnded()
{
	if (m_move) m_move->clearWaitingForPath(); // RW 0x740C97
}

void AIHordePositionApproachState::crc(StateHasher &h) const
{
	h.addBool(m_move != nullptr);
	if (m_move) m_move->crc(h);
	h.addFloat(m_ownerPosition.x); h.addFloat(m_ownerPosition.y); h.addFloat(m_ownerPosition.z);
	h.addU32(m_pathFrame); h.addU32(m_waitFrame);
	h.addBool(m_stopIfInRange); h.addBool(m_initialApproach); h.addBool(m_waiting);
}

// RW 0x7443B0 initializes +0x61 once; RW 0x74A58B clears it on exit.
void AIHordePositionApproachState::crcPersistent(StateHasher &h) const
{
	h.addBool(m_initialApproach);
}

void AIHordePositionApproachState::xfer(Xfer &x)
{
	XferVersion v{1, 1};
	x.xferVersion(v);
	xferMove(x, ai().mover(), m_move);
	AIXfer::coord(x, m_ownerPosition);
	x.xferUnsignedInt(&m_pathFrame); x.xferUnsignedInt(&m_waitFrame);
	x.xferBool(&m_stopIfInRange); x.xferBool(&m_initialApproach); x.xferBool(&m_waiting);
	if (x.isLoading() && m_move) m_move->setComputePathHook(this);
}

std::unique_ptr<AIStateMachine> makeHordePositionAttackMachine(AIUpdateInterface &ai, AttackFireNotify &notify)
{
	// RW 0x74548D .. 0x7455B1; 0x270F is the failure terminal on both wait-exit edges.
	constexpr unsigned approach = 0x2BC, fire = 0x2BD, wait = 0x2BE, failedApproachWait = 0x2C0;
	auto m = std::make_unique<AIStateMachine>(ai, "AttackPositionHordeStateMachine");
	m->defineState(approach, std::make_unique<AIHordePositionApproachState>(*m), fire, failedApproachWait);
	m->defineState(failedApproachWait, std::make_unique<AIHordePositionWaitState>(*m), MACHINE_DONE_FAILURE, MACHINE_DONE_FAILURE);
	m->defineState(fire, std::make_unique<AIHordePositionFireState>(*m, notify), wait, approach);
	m->defineState(wait, std::make_unique<AIHordePositionWaitState>(*m), approach, approach);
	return m;
}
