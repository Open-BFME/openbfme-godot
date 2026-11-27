// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIAttackCharge.h (PORT-133, fork issue 335).

#include "GameLogic/AI/AIAttackCharge.h"

#include "Common/Audio/AudioRequests.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

namespace
{
const float kGoalMovedSq = 2500.0f; // RW 0xBE5A00
const float kRunThrough = 50.0f;    // RW 0xBD88C4
const float kMinAimDelta = 0.035f;  // RW 0xC2A1BC
const int kVoiceStartCharging = 0x7EA; // RW 0x7549A2

unsigned chargingCondition()
{
	static const unsigned bit = (unsigned)CombatNames::modelCondition("CHARGING"); // 0x84: RW 0x75190B / 0x7519E0 (Object + 0x11C bit 4)
	return bit;
}
} // namespace

bool AttackUsesChargeMachine(const Object &source)
{
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	// RW 0x74CFDB (status 0x25 -> kind 5) and RW 0x74CFF1 (HORDE -> kinds 6 / 7) come before the weapon's flag
	if (source.testStatus((unsigned)CombatNames::status("CONTESTING_BUILDING")) || (kHorde >= 0 && source.isKindOf((unsigned)kHorde)))
	{
		return false;
	}
	ObjectWeapons *w = const_cast<Object &>(source).getWeapons();
	const Weapon *weapon = w ? w->currentWeapon() : nullptr;
	const WeaponTemplate *t = weapon ? weapon->getTemplate() : nullptr;
	return t && t->m_canFireWhileCharging; // RW 0x74D021: template + 0x82
}

std::unique_ptr<AIStateMachine> makeChargeAttackMachine(AIUpdateInterface &ai)
{
	auto m = std::make_unique<AIStateMachine>(ai, "AttackFireWhileChargingMachine");
	m->defineState(CHARGE_TARGET, std::make_unique<AIChargeTargetState>(*m), MACHINE_DONE_SUCCESS, MACHINE_DONE_FAILURE); // RW 0x745287 .. 0x74529C
	return m;
}

bool AIChargeTargetState::charging() const
{
	return owner().testModelCondition(chargingCondition());
}

// RW 0x748318: the goal object is more than 50 (planar) from the goal
bool AIChargeTargetState::goalObjectMoved(const Object &victim) const
{
	const Coord3D &p = *victim.getPosition();
	const float dx = SimMath::subf32(p.x, m_goal.x), dy = SimMath::subf32(p.y, m_goal.y);
	return SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) > kGoalMovedSq; // RW 0x66137C, `jbe` -> false
}

// RW 0x74D8EF
bool AIChargeTargetState::canStrike(const Object &victim) const
{
	Object &obj = owner();
	ObjectWeapons *w = obj.getWeapons();
	const Weapon *weapon = w ? w->currentWeapon() : nullptr; // RW 0x74D919
	const WeaponTemplate *t = weapon ? weapon->getTemplate() : nullptr;
	if (!t)
	{
		return false;
	}
	const Coord3D &s = *obj.getPosition(), &v = *victim.getPosition();
	const float dx = SimMath::subf32(s.x, v.x), dy = SimMath::subf32(s.y, v.y), dz = SimMath::subf32(s.z, v.z);
	// RW 0x74D962 .. 0x74D981: z * z + y * y, then + x * x (SSE)
	const float distSq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
	const float range = t->m_attackRange; // RW 0x9E68DC: template + 0x14
	if (!(SimMath::mulf32(range, range) > distSq)) // RW 0x74D98E `comiss range2, dist2; jbe`
	{
		return false;
	}
	float threshold = t->m_aimDelta; // template + 0x2C
	if (kMinAimDelta > threshold)    // RW 0x74D9A3
	{
		threshold = kMinAimDelta;
	}
	return threshold > emotionRelAngle(obj, v); // RW 0x74D9B5 .. 0x74D9C1: the signed angle (RW 0x4B3D8D), no fabs
}

void AIChargeTargetState::repath()
{
	AIUpdateInterface &a = ai();
	AIWorld &w = a.world();
	if (w.mapReady())
	{
		w.pathfinder().removeGoal(a.adapter()); // RW 0x68B401
	}
	if (m_move)
	{
		m_move->setGoal(m_goal);                // + 0x20
		m_move->setAdjustsDestination(false);   // + 0x48 = 0
	}
	a.mover().requestPath(m_goal, true);        // RW 0x667ED1(goal, 1)
}

// RW 0x748348
void AIChargeTargetState::runThrough(const Object &victim)
{
	const Coord3D &s = *owner().getPosition(), &v = *victim.getPosition();
	float dx = SimMath::subf32(v.x, s.x), dy = SimMath::subf32(v.y, s.y), dz = SimMath::subf32(v.z, s.z);
	// RW 0x748391: Coord3D::normalize RW 0x405553 (the port's form of it: AIPathfindGround.cpp)
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, dz)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(dy, inv);
		dz = SimMath::mulf32(dz, inv);
	}
	Coord3D dest;
	dest.x = SimMath::addf32(v.x, SimMath::mulf32(dx, kRunThrough));
	dest.y = SimMath::addf32(v.y, SimMath::mulf32(dy, kRunThrough));
	dest.z = SimMath::addf32(v.z, SimMath::mulf32(dz, kRunThrough));
	machine().setGoalObject((ObjectID)INVALID_ID); // RW 0x7483EC: machine vslot 0x38(0) (RW 0x8DB8CB; this machine is never locked)
	m_goal = dest;
	repath();
}

// RW 0x74D9CE
void AIChargeTargetState::fireAtOwnPosition()
{
	Object &obj = owner();
	ObjectWeapons *w = obj.getWeapons();
	if (!w || !w->currentWeapon())
	{
		return;
	}
	obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, true); // RW 0x62684D(0xD, 1)
	const Coord3D at = *obj.getPosition();
	w->fireCurrentWeapon(nullptr, &at); // RW 0x6920AD(object + 0x38)
}

StateReturnType AIChargeTargetState::onEnter()
{
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	Object *victim = machine().goalObject();
	if (!victim) // RW 0x754945 .. 0x75494B
	{
		return STATE_FAILURE;
	}
	m_goal = *victim->getPosition();
	m_setBeforeCharge = a.curLocomotorSet(); // fork issue 395 Q3: the set the strike's LOCOMOTORSET_PANIC (RW 0x7519FE) displaces
	ObjectWeapons::setObjectWeaponLock(obj, PRIMARY_WEAPON, LOCKED_TEMPORARILY); // RW 0x69121A(0, 1)
	// RW 0x754969 .. 0x7549A8: { owner->getDrawable() } -> RW 0x8DEDBB(list, 0x7EA, 0) (VoiceStartCharging; the port's voice request, as AIEmotionStates.cpp)
	AudioApi::postUnitVoice(kVoiceStartCharging, obj.getID());
	m_move = std::make_unique<AIMoveToState>(a.mover(), m_goal, false); // + 0x48 = 0
	return m_move->onEnter();                                             // RW 0x74DADA
}

StateReturnType AIChargeTargetState::update()
{
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	Object *victim = machine().goalObject();
	if ((!victim && !charging()) || m_count <= 0 || !m_move) // RW 0x751907 .. 0x751923
	{
		return STATE_FAILURE;
	}
	if (victim && goalObjectMoved(*victim) && !charging()) // RW 0x751929 .. 0x751986
	{
		m_goal = *victim->getPosition();
		repath();
	}
	ObjectWeapons *w = obj.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr; // RW 0x75199B
	if (!weapon)
	{
		return STATE_FAILURE;
	}
	if (victim && canStrike(*victim) && !charging() && w->weaponStatus(*weapon) == WEAPON_READY_TO_FIRE) // RW 0x7519A7 .. 0x7519D2
	{
		runThrough(*victim);                          // RW 0x7519DB
		a.setModelCondition("CHARGING", true);        // RW 0x7519E0 .. 0x7519F2
		a.chooseLocomotorSet(LOCOMOTORSET_PANIC);     // RW 0x7519FE: AI vslot 0x238(4)
		fireAtOwnPosition();                          // RW 0x751A07
	}
	if (m_done) // RW 0x751A0C
	{
		return STATE_SUCCESS;
	}
	return m_move->update(); // RW 0x751A19 -> 0x748E46
}

void AIChargeTargetState::onExit(StateExitType)
{
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	if (charging()) // RW 0x74D8AB
	{
		a.setModelCondition("CHARGING", false);
	}
	// RW 0x74D8C8: AI vslot 0x238(0) — the exit pushes literal 0 (SET_NORMAL). Vanilla charges from SET_NORMAL (Khamul's NazgulPlowAttackKhamulNew),
	// where 0 IS the set the charge displaced; the fork's mounted chargers (fork issue 395 Q3, Erkenbrand from SET_MOUNTED) would land on SET_NORMAL
	// and drop from the mounted speed to the human one ("afterwards Erkenbrand moves slowly"). The fork restores the recorded entry set instead,
	// which is the literal 0 for every charger that entered from SET_NORMAL and the fork's check (the speed after == the speed before) otherwise.
	a.chooseLocomotorSet(m_setBeforeCharge);
	if (ObjectWeapons *w = obj.getWeapons()) { w->releaseWeaponLock(LOCKED_TEMPORARILY); }      // RW 0x68DF11(1)
	if (m_move)
	{
		m_move->onExit(); // RW 0x74D8E4 -> 0x748D8A
		m_move.reset();
	}
}

void AIChargeTargetState::onTemporaryStateEnded()
{
	if (m_move)
	{
		m_move->clearWaitingForPath();
	}
}

void AIChargeTargetState::crc(StateHasher &h) const
{
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addFloat(m_goal.z);
	h.addBool(m_done);
	h.addI32(m_count);
	h.addI32(m_setBeforeCharge);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}

void AIChargeTargetState::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 }; // 2: m_setBeforeCharge (fork issue 395 Q3); v1 omits it and defaults to LOCOMOTORSET_NORMAL (RW 0x74D8C8)
	x.xferVersion(v);
	AIXfer::coord(x, m_goal);
	x.xferBool(&m_done);
	x.xferInt(&m_count);
	if (v.current >= 2)
	{
		x.xferInt(&m_setBeforeCharge);
		if (x.isLoading())
		{
			// RW 0x5E9AB1 (0x5E9B06): indices of the SET_* table. The fork remembers the entry set for
			// onExit (RW 0x74D892 uses NORMAL); curLocomotorSet() can also be -1 before a set is chosen.
			AIXfer::inRange(m_setBeforeCharge, -1, LOCOMOTORSET_COUNT - 1, "charge: the locomotor set before charging");
		}
	}
	else if (x.isLoading())
	{
		m_setBeforeCharge = LOCOMOTORSET_NORMAL;
	}
	xferMove(x, ai().mover(), m_move);
}
