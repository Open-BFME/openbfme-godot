// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIAttackMelee.h for the machine. Lane PHYS-1 round 6.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * 0xE1 onEnter RW 0x74EF4B: no victim -> FAILURE; with STAND_GROUND or the AI byte + 0x3CC RW 0x6FF7FA decides; IS the victim crushable (RW 0x69519A(victim, 2)) and
//     the unit able to crush now (RW 0x68D524) -> setState(0xE9); the victim's physics test (RW 0x792973), no weapon, RW 0x744B25(victim) -> FAILURE; the weapon reaches
//     (RW 0x6CC653) -> SUCCESS; else the victim's position is stored (+ 0x50); not crushable and nearer than the gate -> SUCCESS; a horde member inside its horde ->
//     FAILURE; the path is destroyed (RW 0x66276B); beyond the gate computePath (vslot 0x44) and, when true, the move's onEnter (RW 0x74DADA, + 0x48 = 1) is the result;
//     otherwise SUCCESS;
//   * 0xE1 update RW 0x74B001: IS_MELEE_ATTACKING off; no victim, NOT_IN_WORLD (51) or a stealthed victim -> FAILURE; crushable and able to crush -> 0xE9; the weapon
//     reaches and (RW 0x6EE6DD passes or there is no path) -> SUCCESS; the current victim is set (RW 0x6682B1); with no path the victim's position is stored and a unit
//     nearer than the gate -> SUCCESS; computePath true -> the move's update (RW 0x748E46): anything but CONTINUE -> SUCCESS, else CONTINUE; false -> FAILURE;
//   * 0xE1 computePath RW 0x746DB6: false while the AI is blocked (+ 0x16C > 0); true while + 0x49 is set; with a path (or a pending one) at most every
//     LOGICFRAMES_PER_SECOND frames and only when the victim moved more than 1/10 of its distance (RW 0x741450); no victim or no weapon -> false; the victim's position
//     is stored; a crushable victim: an ordinary request (RW 0x667ED1(pos, 0)) to it, true; a unit nearer than the gate, or a WALL_UPGRADE victim -> false; a fleeing
//     victim (RW 0x7468D5): led by its facing * (bounding radius * 2 + 60), + 0x48 = 0, the ordinary request, the result is the AI's waiting flag; else the point
//     victim + normalize(unit - victim) * the victim's bounding radius goes to requestMeleeApproachPath (RW 0x6639CF, offset applied): true -> + 0x49 = waiting, true;
//     false -> false;
//   * 0xE2 onEnter RW 0x74F599: + 0x50 = -1, + 0x48 = 0, the path frame, + 0x70 and + 0x6C cleared; a victim that exists, a unit not FLEE_OFF_MAP and a weapon: the
//     weapon reaches -> the goal reservation on the unit's own position (RW 0x68B3BD), IS_MELEE_ATTACKING on, SUCCESS; else the victim's position (+ 0x58); a horde
//     member inside its horde -> IS_MELEE_ATTACKING off, FAILURE; the path destroyed, computePath true -> (no wait) the move's onEnter, + 0x48 = 1 / (waiting) CONTINUE;
//     otherwise FAILURE;
//   * 0xE2 update RW 0x74B1D6: a victim that exists, the unit in the world, not stealthed: the current victim; crushable with UseCrushAttack (+ 0x60D) and able to crush
//     -> SUCCESS; without the wait: a victim that is not fleeing sets the desired speed 999999 and clears RUNNING_DOWN_FROM_BEHIND; a unit that is IS_MELEE_ATTACKING
//     (and the victim not fleeing) loses its path; (both CONTESTING_BUILDING) or (no path, a weapon, in reach and RW 0x6EE6DD) -> the own-position reservation,
//     IS_MELEE_ATTACKING on, SUCCESS; computePath true: (no wait) the move's update; while it continues with a path or a pending one -> CONTINUE (a fleeing victim: while
//     it continues); then in reach and (RW 0x6EE6DD or no path) -> the reservation, SUCCESS; else the wait starts: + 0x70 = 1, + 0x6C = now + 5 * FPS when no
//     destination was found (+ 0x71), else now + 2 * FPS; CONTINUE; computePath false -> FAILURE; during the wait CONTINUE until the frame passes + 0x6C, then FAILURE;
//   * 0xE2 computePath RW 0x7471CF: the 0xE1 throttle; no victim / weapon -> false; the victim's position (+ 0x58); a fleeing victim: led by its facing *
//     (radius * 2 + 60), RW 0x6F3C87, + 0x48 = 0, the request RW 0x667ED1(dest, 0), + 0x49 = waiting (0 with a path), true; with a path, a weapon that reaches the
//     victim from the current destination (RW 0x6CC07C from + 0x20) -> true; else dest = the victim's position and RW 0x6F37B3: found -> the reservation RW 0x68B3AB,
//     RW 0x667ED1(dest, 1), + 0x49 = waiting; else dest = the victim's position and RW 0x7463E8: true -> RW 0x6FE456 (adjustDestination) and RW 0x667ED1(dest, 1);
//     false -> + 0x70 = 1, + 0x6C = now + 10 * FPS; true;
//   * 0xE5 onEnter RW 0x74D46D: within LOGICFRAMES_PER_SECOND / 4 frames of the last run -> FAILURE; IS_MELEE_ATTACKING off; a horde member inside its horde, no weapon
//     -> FAILURE; a victim that is gone or stealthed is replaced by the nearest enemy within 2 * MeleeApproachDist (RW 0x660D2C / 0x660EC6 filters, RW 0xA39090),
//     else the victim stays the goal; the current victim, the frame stored, SUCCESS.
// INFERENCE (stop S-787): the machine-busy test (RW 0x8DBDFF), RW 0x694C0D (stealth: none), RW 0x792973, RW 0x744B25, AI vslot 0x168 and the + 0x3CC byte are not
// ported (taken as false); STAND_GROUND's RW 0x6FF7FA is "the weapon reaches"; the fire-during-approach sub-state of 0xE2 (+ 0x4C / + 0x50, RW 0x740E39) and the
// run-down speed of a fleeing victim are not ported; the own-position reservation does not write AI + 0x180 / + 0x3B0 (RW 0x68B411); the re-acquisition filters of
// 0xE5 are TargetFinder's (S-326); RW 0x68C9AC is the victim's position; the siege-deploy branches are not ported.

#include "GameLogic/AI/AIAttackMelee.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

namespace
{
const char *const kStopMeleeMachine =
	"[S-787] melee machine: the per-unit melee attack machine RW 0x744B71 (kind 2 of RW 0x74CED6) is ported: AIAttackMeleeApproachState 0xE1 (RW 0x74EF4B / 0x74B001, "
	"computePath RW 0x746DB6 -> requestMeleeApproachPath), AIAttackMeleeSquishState 0xE9 (the horde 0xC8 class), AIAttackMeleeEngageState 0xE2 (RW 0x74F599 / 0x74B1D6, "
	"computePath RW 0x7471CF with the engagement destination RW 0x6F37B3 and the back-off RW 0x7463E8), AIMeleeReAcquireState 0xE5 (RW 0x74D46D), aim / fire / wait "
	"0xE6 / 0xE7 / 0xE8; an ordinary non-crushing horde member inside its horde requests no path through 0xE1 / 0xE2 (0xE5 always refuses it; a crushing member can dispatch 0xE9, which can path); INFERENCE: the machine-busy test, stealth, RW 0x792973 / 0x744B25 / AI vslot 0x168 / + 0x3CC "
	"are false, STAND_GROUND's RW 0x6FF7FA is the reach test, the fire-during-approach sub-state and the run-down speed of 0xE2 are not ported, the own-position "
	"reservation does not write AI + 0x180, 0xE5's re-acquisition uses TargetFinder, RW 0x6F37B3's obstacle-zone mapping / siege branches and RW 0x7463E8's line "
	"callback (isLinePassable) and quick path test (clientSafeQuickDoesPathExist) are stand-ins";

Object *victimOf(const AIState &s)
{
	return s.machine().goalObject();
}

bool victimGone(const Object *v)
{
	return !v || !CombatQueries::isAlive(*v);
}

bool hasWeapon(Object &o)
{
	ObjectWeapons *w = o.getWeapons();
	return w && w->currentWeapon();
}

bool inReach(Object &self, const Object &victim)
{
	ObjectWeapons *w = self.getWeapons();
	return w && w->isWithinAttackRange(victim);
}

// HORDE_MEMBER and obj + 0x27C (the horde it is contained in)
bool memberInHorde(const Object &o)
{
	return o.testStatus((unsigned)CombatNames::statuses().hordeMember) && o.getContainedBy() != nullptr;
}

// MeleeApproachTolerance + MeleeApproachDist (fld; fadd) against the root of the planar distance left in the register (RW 0x403111)
bool nearerThanGate(AIUpdateInterface &ai, const Object &self, const Coord3D &victimPos)
{
	const PathfindConfig &c = ai.world().pathfinder().config();
	const float dx = SimMath::subf32(self.getPosition()->x, victimPos.x), dy = SimMath::subf32(self.getPosition()->y, victimPos.y);
	const double len = SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f));
	return len < SimMath::pc24AddW((double)c.meleeApproachTolerance, (double)c.meleeApproachDist);
}

// RW 0x741450: the victim moved more than 1/10 of its distance from the unit (moved^2 > 0.01 * dist^2, RW 0xBE5600)
bool victimMoved(const Object &self, const Coord3D &now, const Coord3D &stored)
{
	const float mx = SimMath::subf32(now.x, stored.x), my = SimMath::subf32(now.y, stored.y);
	const float ox = SimMath::subf32(now.x, self.getPosition()->x), oy = SimMath::subf32(now.y, self.getPosition()->y);
	return SimMath::addf32(SimMath::mulf32(my, my), SimMath::mulf32(mx, mx)) > SimMath::mulf32(SimMath::addf32(SimMath::mulf32(oy, oy), SimMath::mulf32(ox, ox)), 0.01f);
}

// the fleeing lead of RW 0x746DB6 / 0x7471CF: the victim's position + its facing * (bounding radius * 2 (RW 0xBD889C) + 60 (RW 0xBDC1F8))
Coord3D fleeingLead(const Object &victim, const Coord3D &victimPos)
{
	const float k = SimMath::addf32(SimMath::mulf32(CombatQueries::boundingCircleRadius(victim), 2.0f), 60.0f);
	return Coord3D{ SimMath::addf32(victimPos.x, SimMath::mulf32(k, SimMath::cosDet(victim.getOrientation()))),
		SimMath::addf32(victimPos.y, SimMath::mulf32(SimMath::sinDet(victim.getOrientation()), k)), victimPos.z };
}

void setMeleeFlag(Object &o, bool on)
{
	o.setStatus((unsigned)CombatNames::statuses().isMeleeAttacking, on);
}

bool crushNow(Object &self, Object &victim)
{
	return AttackCrushPolicy(self, victim) && ObjectCrush::canCrush(self);
}

// the weapon test of the engagement search (RW 0x6CC07C from a point)
class UnitAttackRange : public PathfindAttackRange
{
public:
	UnitAttackRange(AIUpdateInterface &ai, PathfindObjectID victim, const Coord3D &pos) : m_ai(ai), m_victim(victim), m_pos(pos) {}
	bool inRangeFrom(const Coord3D &from, float extra) const override { return m_ai.attackRangeFrom(from, m_victim, m_pos, extra); }
	bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const override { return m_ai.attackRangeFromTo(from, m_victim, victimPos, extra); }

private:
	AIUpdateInterface &m_ai;
	PathfindObjectID m_victim;
	Coord3D m_pos;
};

void hashCoord(StateHasher &h, const Coord3D &c)
{
	h.addFloat(c.x);
	h.addFloat(c.y);
	h.addFloat(c.z);
}
} // namespace

bool AttackUsesMeleeMachine(const Object &source, const Object *victim, bool attackingObject)
{
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const CombatNames::Status &st = CombatNames::statuses();
	if (source.isKindOf((unsigned)kHorde) || source.testStatus((unsigned)CombatNames::status("CONTESTING_BUILDING")))
	{
		return false;
	}
	ObjectWeapons *w = const_cast<Object &>(source).getWeapons();
	const Weapon *weapon = w ? w->currentWeapon() : nullptr;
	const WeaponTemplate *t = weapon ? weapon->getTemplate() : nullptr;
	if (!t || t->m_canFireWhileCharging)
	{
		return false;
	}
	(void)st;
	if (!attackingObject || !victim || !t->m_meleeWeapon || source.isKindOf((unsigned)CombatNames::kinds().immobile))
	{
		return false;
	}
	return !t->m_canFireWhileMoving;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 0xE1
// ---------------------------------------------------------------------------------------------------------------------------------
bool AIAttackMeleeApproachState::computePath()
{
	AIUpdateInterface &a = ai();
	AIMover &mv = a.mover();
	Object &self = owner();
	if (mv.blockedFrames() > 0)
	{
		return false;
	}
	if (m_pending)
	{
		return true;
	}
	const bool noPath = mv.path() == nullptr && !mv.isWaitingForPath();
	if (!noPath && a.frame() - m_pathFrame < (unsigned)LOGICFRAMES_PER_SECOND)
	{
		return true;
	}
	m_pathFrame = a.frame();
	Object *victim = victimOf(*this);
	if (!victim)
	{
		return false;
	}
	if (!noPath && !victimMoved(self, *victim->getPosition(), m_victimPos))
	{
		return true;
	}
	if (!hasWeapon(self))
	{
		return false;
	}
	m_victimPos = *victim->getPosition();
	if (AttackCrushPolicy(self, *victim))
	{
		m_dest = m_victimPos;
		mv.requestPath(m_dest, false);
		return true;
	}
	if (nearerThanGate(a, self, m_victimPos) || victim->isKindOfName("WALL_UPGRADE"))
	{
		return false;
	}
	if (AttackTargetFleeing(self, *victim))
	{
		m_dest = fleeingLead(*victim, m_victimPos);
		m_moveEntered = false;
		mv.requestPath(m_dest, false);
		m_pending = mv.isWaitingForPath();
		return m_pending;
	}
	float dx = SimMath::subf32(self.getPosition()->x, m_victimPos.x), dy = SimMath::subf32(self.getPosition()->y, m_victimPos.y);
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(dy, inv);
	}
	const float r = CombatQueries::boundingCircleRadius(*victim);
	m_dest = Coord3D{ SimMath::addf32(m_victimPos.x, SimMath::mulf32(r, dx)), SimMath::addf32(m_victimPos.y, SimMath::mulf32(dy, r)), m_victimPos.z };
	if (!mv.requestMeleeApproachPath(m_dest, false))
	{
		return false;
	}
	m_pending = mv.isWaitingForPath();
	return true;
}

StateReturnType AIAttackMeleeApproachState::onEnter()
{
	AIUpdateInterface &a = ai();
	Object &self = owner();
	a.noteLogicStop(kStopMeleeMachine);
	Object *victim = victimOf(*this);
	if (self.testStatus((unsigned)CombatNames::status("STAND_GROUND")))
	{
		// RW 0x6FF7FA (S-787: the reach test)
		if (victim && hasWeapon(self) && inReach(self, *victim))
		{
			return STATE_SUCCESS;
		}
		machine().setGoalObject(INVALID_ID);
		return STATE_FAILURE;
	}
	m_pending = false;
	m_moveEntered = false;
	if (!victim)
	{
		return STATE_FAILURE;
	}
	if (crushNow(self, *victim))
	{
		machine().setState(0xE9);
		return STATE_CONTINUE;
	}
	if (!hasWeapon(self))
	{
		return STATE_FAILURE;
	}
	if (inReach(self, *victim))
	{
		return STATE_SUCCESS;
	}
	m_victimPos = *victim->getPosition();
	if (!AttackCrushPolicy(self, *victim) && nearerThanGate(a, self, m_victimPos))
	{
		return STATE_SUCCESS;
	}
	if (memberInHorde(self))
	{
		return STATE_FAILURE;
	}
	a.mover().destroyPath();
	if (!nearerThanGate(a, self, m_victimPos))
	{
		if (computePath())
		{
			m_move = std::make_unique<AIMoveToState>(a.mover(), m_dest, false);
			m_move->setExternalRequest(true);
			const StateReturnType r = m_move->onEnter();
			m_moveEntered = true;
			return r;
		}
	}
	return STATE_SUCCESS;
}

StateReturnType AIAttackMeleeApproachState::update()
{
	AIUpdateInterface &a = ai();
	Object &self = owner();
	setMeleeFlag(self, false);
	Object *victim = victimOf(*this);
	if (!victim || self.testStatus((unsigned)CombatNames::status("NOT_IN_WORLD")))
	{
		return STATE_FAILURE;
	}
	if (crushNow(self, *victim))
	{
		machine().setState(0xE9);
		return STATE_CONTINUE;
	}
	const bool noPath = a.mover().path() == nullptr;
	if (hasWeapon(self) && inReach(self, *victim) && (a.world().pathfinder().meleeOwnFootprintFree(a.adapter()) || noPath))
	{
		return STATE_SUCCESS;
	}
	a.setCurrentVictim(victim);
	if (noPath)
	{
		m_victimPos = *victim->getPosition();
		if (nearerThanGate(a, self, m_victimPos))
		{
			return STATE_SUCCESS;
		}
	}
	if (!computePath())
	{
		return STATE_FAILURE;
	}
	if (!m_move)
	{
		m_move = std::make_unique<AIMoveToState>(a.mover(), m_dest, false);
		m_move->setExternalRequest(true);
		const StateReturnType r = m_move->onEnter();
		m_moveEntered = true;
		return r == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS;
	}
	m_move->setGoal(m_dest);
	if (m_pending && !a.mover().isWaitingForPath())
	{
		m_pending = false;
	}
	return m_move->update() != STATE_CONTINUE ? STATE_SUCCESS : STATE_CONTINUE;
}

void AIAttackMeleeApproachState::onExit(StateExitType)
{
	// lane ANIM-1: the 0xE1 vtable's onExit (RW 0xC28E74 -> 0x74933F) is a jump to AIMoveToState::onExit (RW 0x748D8A): it runs whether or not the
	// approach entered its move (MOVING / BACKING_UP / CLIMBING / RAPPELLING cleared, the move loop stopped, endingMove). A horde member that reached
	// its victim in formation (no path) used to keep the MOVING its walk set: the treadmill of FEEDBACK-2 G1
	exitMove(ai().mover(), m_move, m_dest);
	m_pending = false;
}

void AIAttackMeleeApproachState::crc(StateHasher &h) const
{
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
	hashCoord(h, m_victimPos);
	hashCoord(h, m_dest);
	h.addU32(m_pathFrame);
	h.addBool(m_pending);
	h.addBool(m_moveEntered);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 0xE2
// ---------------------------------------------------------------------------------------------------------------------------------
bool AIAttackMeleeEngageState::reserveOwnPosition()
{
	// RW 0x68B3BD(own position, layer); the AI + 0x180 / + 0x3B0 writes of RW 0x68B411 are not ported (S-787)
	AIUpdateInterface &a = ai();
	Coord3D p = *owner().getPosition();
	a.world().pathfinder().updateGoal(a.adapter(), &p, LAYER_GROUND);
	setMeleeFlag(owner(), true);
	return true;
}

bool AIAttackMeleeEngageState::computePath()
{
	AIUpdateInterface &a = ai();
	AIMover &mv = a.mover();
	Object &self = owner();
	if (mv.blockedFrames() > 0)
	{
		return false;
	}
	if (m_pending)
	{
		if (mv.path() != nullptr || mv.isWaitingForPath())
		{
			return true;
		}
		m_pending = false;
	}
	const bool noPath = mv.path() == nullptr && !mv.isWaitingForPath();
	if (!noPath && a.frame() - m_pathFrame < (unsigned)LOGICFRAMES_PER_SECOND)
	{
		return true;
	}
	m_pathFrame = a.frame();
	Object *victim = victimOf(*this);
	if (!victim)
	{
		return false;
	}
	if (!noPath && !victimMoved(self, *victim->getPosition(), m_victimPos))
	{
		return true;
	}
	if (!hasWeapon(self))
	{
		return false;
	}
	m_victimPos = *victim->getPosition();
	Pathfinder &pf = a.world().pathfinder();
	PathfindObject &obj = a.adapter();
	const PathfindLocomotorInfo info = a.locomotorInfo();
	if (AttackTargetFleeing(self, *victim))
	{
		m_dest = fleeingLead(*victim, m_victimPos);
		pf.adjustToPossibleDestination(obj, info, &m_dest);
		m_moveEntered = false;
		mv.requestPath(m_dest, false);
		m_pending = mv.isWaitingForPath() && mv.path() == nullptr;
		return true;
	}
	if (!noPath && a.attackRangeFromTo(m_dest, victim->getID(), m_victimPos, 0.0f))
	{
		return true;
	}
	m_dest = m_victimPos;
	const UnitAttackRange range(a, victim->getID(), m_victimPos);
	const PathfindObject *target = a.findPathfindObject(victim->getID());
	m_noDest = !(target && pf.findMeleeEngagementLocation(obj, info, *target, range, &m_dest));
	if (!m_noDest)
	{
		pf.updateGoal(obj, &m_dest, LAYER_GROUND); // RW 0x68B3AB
		mv.requestPath(m_dest, true);
		m_pending = mv.isWaitingForPath();
		return true;
	}
	m_dest = m_victimPos;
	ObjectWeapons *w = self.getWeapons();
	if (pf.meleeBackOff(obj, info, w ? w->currentAttackRange() : 0.0f, &m_dest))
	{
		pf.adjustDestination(obj, info, &m_dest, nullptr); // RW 0x6FE456
		mv.requestPath(m_dest, true);
	}
	else
	{
		m_wait = true;
		m_waitUntil = a.frame() + 10u * (unsigned)LOGICFRAMES_PER_SECOND;
	}
	return true;
}

StateReturnType AIAttackMeleeEngageState::onEnter()
{
	AIUpdateInterface &a = ai();
	Object &self = owner();
	m_moveEntered = false;
	m_pathFrame = 0;
	m_wait = false;
	m_waitUntil = 0;
	m_pending = false;
	Object *victim = victimOf(*this);
	if (victimGone(victim) || self.testStatus((unsigned)CombatNames::status("FLEE_OFF_MAP")) || !hasWeapon(self))
	{
		return STATE_FAILURE;
	}
	if (inReach(self, *victim))
	{
		reserveOwnPosition();
		return STATE_SUCCESS;
	}
	m_victimPos = *victim->getPosition();
	if (memberInHorde(self))
	{
		setMeleeFlag(self, false);
		return STATE_FAILURE;
	}
	a.mover().destroyPath();
	if (!computePath())
	{
		return STATE_FAILURE;
	}
	if (m_wait)
	{
		return STATE_CONTINUE;
	}
	m_move = std::make_unique<AIMoveToState>(a.mover(), m_dest, false);
	m_move->setExternalRequest(true);
	const StateReturnType r = m_move->onEnter();
	m_moveEntered = true;
	return r;
}

StateReturnType AIAttackMeleeEngageState::update()
{
	AIUpdateInterface &a = ai();
	Object &self = owner();
	Object *victim = victimOf(*this);
	if (victimGone(victim) || self.testStatus((unsigned)CombatNames::status("NOT_IN_WORLD")))
	{
		return STATE_FAILURE;
	}
	a.setCurrentVictim(victim);
	if (AttackCrushPolicy(self, *victim) && CrushTemplateInfo::of(*self.getTemplate()).useCrushAttack && ObjectCrush::canCrush(self))
	{
		return STATE_SUCCESS;
	}
	if (m_wait)
	{
		if (a.frame() <= m_waitUntil)
		{
			return STATE_CONTINUE;
		}
		m_wait = false;
		return STATE_FAILURE;
	}
	const bool fleeing = AttackTargetFleeing(self, *victim);
	if (!fleeing)
	{
		a.mover().setDesiredSpeed(AI_FAST_SPEED); // RW 0x6625DA(999999)
		if (self.testStatus((unsigned)CombatNames::statuses().runningDownFromBehind))
		{
			self.setStatus((unsigned)CombatNames::statuses().runningDownFromBehind, false);
		}
		if (self.testStatus((unsigned)CombatNames::statuses().isMeleeAttacking))
		{
			a.mover().destroyPath();
		}
	}
	Pathfinder &pf = a.world().pathfinder();
	const bool weapon = hasWeapon(self);
	const CombatNames::Status &st = CombatNames::statuses();
	(void)st;
	const int contesting = CombatNames::status("CONTESTING_BUILDING");
	if ((self.testStatus((unsigned)contesting) && victim->testStatus((unsigned)contesting)) ||
		(a.mover().path() == nullptr && weapon && inReach(self, *victim) && pf.meleeOwnFootprintFree(a.adapter())))
	{
		reserveOwnPosition();
		return STATE_SUCCESS;
	}
	if (!computePath())
	{
		return STATE_FAILURE;
	}
	if (m_wait)
	{
		return STATE_CONTINUE;
	}
	if (!m_move)
	{
		m_move = std::make_unique<AIMoveToState>(a.mover(), m_dest, false);
		m_move->setExternalRequest(true);
		m_move->onEnter();
		m_moveEntered = true;
	}
	m_move->setGoal(m_dest);
	const StateReturnType r = m_move->update();
	if (r == STATE_CONTINUE && (fleeing || a.mover().path() != nullptr || m_pending))
	{
		return STATE_CONTINUE;
	}
	const bool ok = pf.meleeOwnFootprintFree(a.adapter()) || a.mover().path() == nullptr;
	if (weapon && inReach(self, *victim) && ok)
	{
		reserveOwnPosition();
		return STATE_SUCCESS;
	}
	m_wait = true;
	m_waitUntil = a.frame() + (m_noDest ? 5u : 2u) * (unsigned)LOGICFRAMES_PER_SECOND;
	return STATE_CONTINUE;
}

void AIAttackMeleeEngageState::onExit(StateExitType)
{
	// lane ANIM-1, RW 0x74B716 (the 0xE2 vtable's onExit, RW 0xC287C4): nothing for a destroyed owner (+ 0x94 bit 0); else the desired speed back to 999999
	// (RW 0x6625DA, float RW 0xC27440) when there is an AI, then for an owner that is not HORDE_MEMBER (status 0x26) RW 0x748650(0x4B, 0) and
	// setStatus(RUNNING_DOWN_FROM_BEHIND, false) (RW 0x62684D), and always AIMoveToState::onExit (RW 0x748D8A), entered move or not. RW 0x748650(0x4B, 0) is
	// RW 0x694569 on that one bit (lane DECOMP-1, BFME2 decomp Rva00346C53Mask.cpp tier A / ObjectConditionAndPassengerWeaponSet.cpp tier B): across the
	// owner's horde, members first (S-1581 closed)
	Object &self = owner();
	if (!self.isDestroyed())
	{
		ai().mover().setDesiredSpeed(AI_FAST_SPEED);
		if (!self.testStatus((unsigned)CombatNames::statuses().hordeMember))
		{
			self.setStatusAcrossHorde((unsigned)CombatNames::statuses().runningDownFromBehind, false); // RW 0x748650(0x4B, 0)
			self.setStatus((unsigned)CombatNames::statuses().runningDownFromBehind, false);            // RW 0x62684D
		}
		exitMove(ai().mover(), m_move, m_dest);
	}
	m_move.reset();
	m_pending = false;
}

void AIAttackMeleeEngageState::crc(StateHasher &h) const
{
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
	hashCoord(h, m_dest);
	hashCoord(h, m_victimPos);
	h.addU32(m_pathFrame);
	h.addU32(m_waitUntil);
	h.addBool(m_wait);
	h.addBool(m_noDest);
	h.addBool(m_pending);
	h.addBool(m_moveEntered);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 0xE5
// ---------------------------------------------------------------------------------------------------------------------------------
StateReturnType AIMeleeReAcquireState::onEnter()
{
	AIUpdateInterface &a = ai();
	Object &self = owner();
	if (m_ran && a.frame() - m_lastFrame < (unsigned)LOGICFRAMES_PER_SECOND / 4u)
	{
		return STATE_FAILURE;
	}
	setMeleeFlag(self, false);
	if (memberInHorde(self) || !hasWeapon(self))
	{
		return STATE_FAILURE;
	}
	Object *victim = victimOf(*this);
	if (victimGone(victim))
	{
		const float range = SimMath::mulf32(a.world().pathfinder().config().meleeApproachDist, 2.0f);
		victim = self.logic().combat().targets().findClosestEnemy(self, range, TargetFinder::MEMBERS_ONLY, CMD_FROM_AI);
	}
	machine().setGoalObject(victim ? victim->getID() : (ObjectID)INVALID_ID);
	a.setCurrentVictim(victim);
	m_lastFrame = a.frame();
	m_ran = true;
	return STATE_SUCCESS;
}

void AIMeleeReAcquireState::crc(StateHasher &h) const
{
	h.addU32(m_lastFrame);
	h.addBool(m_ran);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x744B71
// ---------------------------------------------------------------------------------------------------------------------------------
std::unique_ptr<AIStateMachine> makeMeleeAttackMachine(AIUpdateInterface &ai, AIAttackState *parent)
{
	auto m = std::make_unique<AIStateMachine>(ai, "AIAttackMeleeMachine");
	m->defineState(MELEE_APPROACH, std::make_unique<AIAttackMeleeApproachState>(*m), MELEE_ENGAGE, MACHINE_DONE_FAILURE);
	m->defineState(MELEE_SQUISH, makeMeleeSquishState(*m), MELEE_ENGAGE, MACHINE_DONE_FAILURE);
	m->defineState(MELEE_ENGAGE, std::make_unique<AIAttackMeleeEngageState>(*m), MELEE_AIM, MELEE_REACQUIRE);
	m->defineState(MELEE_REACQUIRE, std::make_unique<AIMeleeReAcquireState>(*m), MELEE_APPROACH, MACHINE_DONE_FAILURE);
	m->defineState(MELEE_AIM, std::make_unique<AIAttackAimState>(*m, parent, MELEE_APPROACH), MELEE_FIRE, MELEE_APPROACH);
	m->defineState(MELEE_FIRE, std::make_unique<AIAttackFireState>(*m, parent), MELEE_WAIT, MELEE_APPROACH);
	m->defineState(MELEE_WAIT, std::make_unique<AIWaitUntilFinishedFiringState>(*m), MELEE_APPROACH, MELEE_APPROACH);
	return m;
}
