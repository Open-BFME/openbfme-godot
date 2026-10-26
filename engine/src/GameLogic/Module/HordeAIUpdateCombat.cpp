// OpenBFME. GPL-3.0.
// HordeAIUpdate, lane COMBAT-1 part: the horde attack machine, the melee readiness rule, the Amoeba member behaviour and the rank release of a ranged horde.
// See GameLogic/Module/HordeAIUpdate.h and docs/STOPS.md S-327.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; workspace/rebuild/specs/horde-and-movement.md 2.7 / 2.8 and the addresses there):
//   * the horde machine (B1 Rva001812B0AIHordeMachineCtor.cpp:194-228, RW ctor 0x00744D73): melee branch (the horde is a MELEE_HORDE and its current weapon has MeleeWeapon):
//     Squish 0xC8 -> ApproachTarget 0xC9 -> Wait 0xCB (the melee phase) / WaitPath 0xCC; otherwise Squish -> ApproachTarget -> FireWeapon 0xCA -> WaitUntilFinishedFiring 0xCB;
//   * Amoeba isTargetReady RW 0x98FA8E: R = the weapon's AttackRange, a non-structure target within |dz| <= 20, close = edgeDist^2 < max(R + 10, 0.1)^2; ready when a member is
//     attacking, or a non-attacking member exists while close; ready refreshes a cache for 15 frames (RotWK 3 * FPS, RW 0x86BE9A);
//   * Wait RW 0x74697A / 0x74ACAD / 0x746C2A: the melee ends 15 frames after the last time the readiness held; WaitPath RW 0x746AFE retries every 7 frames;
//   * Amoeba per-frame update RW 0x9902A1: candidates = members that are not IS_MELEE_ATTACKING, not UNCONTROLLABLY_SCARED and not walking, nearest to the target first; each one first
//     attacks an enemy within its weapon's reach, else runs the idle cycle (DelayUntilIdle / DelayRandomActivate), else takes the best of the 8 neighbouring 10-unit cells scored
//     10000 - FacingBonus * |turn| / pi - edgeDist, rejecting cells in the angle limit, farther than OuterRange (OuterRangeBuildings for a structure) from the horde, in the
//     history of the last 4 cells; the cell score loses the distance beyond InnerRange; a best score below 1 with an outside rejection steps to the valid cell nearest the horde;
//   * the HordeAttackNugget's release of the ranks is HordeContain::attackTargetNow (RW 0x875221, lane ARCHER-1, HordeMemberPass.cpp): each member of the released ranks
//     picks its own target among the target horde's members (RW 0x86FA87, a randomised nearest).
// DONOR: B1 for the machine shape. INFERENCE: the nearest-member choices, the cell passability (every cell is assumed passable), the nearest-enemy distance (centres, minus radii),
// the logic RNG call site of the idle cycle (its file / line is not read).

#include "GameLogic/Module/HordeAIUpdate.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIXfer.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIApproachMath.h"
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/BloodthirstyUpdate.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/Weapon.h"

#include <algorithm>

namespace
{
const char *const kAmoebaCpp = "HordeMeleeAmoeba.cpp"; // RW string; the line of the draw is not read
const unsigned kReadyCacheFrames = 15;                 // RotWK 3 * FPS
const unsigned kWaitPathRetry = 7;                     // RotWK FPS + 2
const float kNearestMemberStart = 99999.0f;            // RW 0xBDF350 (RW 0x86FA87: the start of the nearest-member search and its range without one)
const float kCellStep = 10.0f;

HordeContain *hordeContainOf(Object &horde)
{
	return dynamic_cast<HordeContain *>(horde.getContain());
}

bool isHordeObject(const Object &o)
{
	ContainModuleInterface *c = o.getContain();
	return c && c->getHordeContainInterface();
}

// the horde a victim belongs to: itself when it is a horde, its container when it is a member, else null
Object *hordeOfTarget(Object &t)
{
	if (isHordeObject(t))
	{
		return &t;
	}
	if (Object *c = t.getContainedBy())
	{
		if (isHordeObject(*c))
		{
			return c;
		}
	}
	return nullptr;
}

// the members of a horde that are alive, in contain order
std::vector<Object *> aliveMembers(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *l = c->getContainedItemsList())
		{
			for (Object *m : *l)
			{
				if (CombatQueries::isAlive(*m))
				{
					out.push_back(m);
				}
			}
		}
	}
	return out;
}

float edgeDistance(const Object &a, const Object &b)
{
	const float d = SimMath::length2d(SimMath::subf32(a.getPosition()->x, b.getPosition()->x), SimMath::subf32(a.getPosition()->y, b.getPosition()->y));
	const float e = SimMath::subf32(SimMath::subf32(d, CombatQueries::boundingCircleRadius(a)), CombatQueries::boundingCircleRadius(b));
	return e > 0.0f ? e : 0.0f;
}

// RW 0x6634BF (this = `self`): max(0, dist2D(p, obj) - self radius - obj radius)^2
float edgeDistance2(const Coord3D &p, const Object &self, const Object &obj)
{
	const float d = SimMath::length2d(SimMath::subf32(p.x, obj.getPosition()->x), SimMath::subf32(p.y, obj.getPosition()->y));
	const float e = SimMath::subf32(SimMath::subf32(d, CombatQueries::boundingCircleRadius(self)), CombatQueries::boundingCircleRadius(obj));
	return e >= 0.0f ? SimMath::mulf32(e, e) : 0.0f;
}

float absF(float a)
{
	return a < 0.0f ? SimMath::subf32(0.0f, a) : a;
}

// ---- the horde machine's states ------------------------------------------------------------------------------------------------
// lane HORDE-2: AIAttackMeleeSquishState (state 0xC8, the first state of the horde machine RW 0x744D73; ctor RW 0x7444DD, vtable RW 0xC28EC8), the crush attack of a crusher
// horde. TARGET FACTS (RotWK, read with Ghidra; the B1 body AI/Rva001775A0.cpp is the donor of the shape):
//   * onEnter RW 0x74F21A: SUCCESS when the horde has STAND_GROUND (68), its AI byte +0x3CC is set or its template has UseCrushAttack = No (+0x60D); FAILURE without a goal;
//     SUCCESS when crushPolicy(goal, TEST_CRUSH_OR_SQUISH) (RW 0x69519A) fails or computePath fails; else IS_MELEE_ATTACKING (28) on and AIInternalMoveTo::onEnter;
//   * computePath (vslot 0x44) RW 0x742271: refused while the AI is blocked (+0x16C > 0); true while a path is pending; without a path a re-path is forced, else at most every
//     LOGICFRAMES_PER_SECOND frames and only when the victim moved more than 1/10 of its distance (RW 0x741450: moved^2 > 0.01 * dist^2, planar); the victim resolves to its
//     container (the horde); the goal point is the victim + normalize(victim - horde) * (bounding radius * 2.0, RW 0xBD889C) and a path is requested;
//   * crushPolicy RW 0x69519A / 0x6950F3: crusherLevel(horde) > crushableLevel(victim); then RAMPAGING, FLEE_OFF_MAP or CHARGING passes; else the victim must be an ENEMY and
//     the horde's current weapon absent or a MeleeWeapon (RW 0x441B59);
//   * update RW 0x74F31F: setPathExtraDistance(50.0, RW 0xBD88C4); a horde that cannot crush now (canCrush RW 0x68D524 false) and whose melee target is ready (HordeContainInterface
//     slot 0x154 RW 0x86BEEA = isMeleeTargetReady) stops (setLocomotorGoalNone) and SUCCEEDS; a dead / FLEE_OFF_MAP goal ends in the base move update; crushPolicy false ->
//     SUCCESS; a goal NOT_IN_WORLD (51) or stealthed -> FAILURE; computePath false -> FAILURE; the move update; when the move is over: dead goal -> SUCCESS, a horde that is not
//     ARMY_OF_DEAD (template + 0x11A bit 7) -> SUCCESS, an ARMY_OF_DEAD horde re-paths through the victim again; every continuing update sets IS_MELEE_ATTACKING = canCrush(horde);
//   * onExit RW 0x74B1B7: the base onExit, then IS_MELEE_ATTACKING off.
// lane FLANK-1 (re-read of RW 0x74F21A / 0x74F31F / 0x742271, BFME2 decomp AIAttackMeleeSquishState): onEnter fails on a destroyed goal (RW 0x8DBDFF
// StateMachine::isGoalObjectDestroyed: a goal id that no longer resolves or whose object is dead) and on a goal whose PhysicsBehavior answers RW 0x792973 (model condition
// STUNNED_FLAILING 127 or FREEFALL 72: a flung victim); it zeroes the path frame (+ 0x4C) and destroys the AI's path (RW 0x66276B) before computePath. The update's dead-goal
// branch (a destroyed goal, a dead goal or one FLEE_OFF_MAP) re-acquires once per success: while the state's byte + 0x64 (1 from the ctor, never reset by onEnter) is set and
// the unit can crush, the byte clears, AI::findClosestEnemy (RW 0x701443) within AIData MeleeApproachDist (+ 0x94) with flags 2 and the AI's attack info (+ 0x70) picks a
// next victim; one that passes crushPolicy becomes the machine's goal (vslot 0x38), computePath (false: FAILURE) and the byte is set again. A goal id of 0 fails the update
// with IS_MELEE_ATTACKING = canCrush. computePath is refused while the AI is blocked (+ 0x16C > 0), forces a re-path when the AI has no path and none pending (+ 0x140,
// + 0x3B1), keeps the goal point's z (no ground snap) and normalises with RW 0x405553 (1 / len, then products).
// INFERENCE (stop S-583): the AI byte +0x3CC (its only writer clears it: taken as 0), the state's own pending byte + 0x49 (the port's internal move keeps its own wait), the
// line test of crushPolicy (RW 0x6F5BB0) is not ported (taken as passing); RW 0x701443 is lane TARGET-1's AI::findClosestEnemy (merge queue q23; FLANK-1 had the
// TargetFinder stand-in) and a zero-length direction keeps the victim's point (retail's normalise gives NaN); the stealth test of the goal is RW 0x694C0D (lane STEALTH-1).
// NOT REACHED YET (S-583): RotWK's AIAttackState::update (RW 0x7510EC) keeps running this inner machine when the outer victim is dead (non-ARMY_OF_DEAD), the port's
// AIAttackState (AIAttack.cpp, ZH donor) ends the attack first, so the re-acquisition only runs in a machine of its own (test_flank1.cpp).
struct HordeSquishState : AIState
{
	explicit HordeSquishState(AIStateMachine &m) : AIState(m, "AIAttackMeleeSquishState") {}
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_victimPos{ 0.0f, 0.0f, 0.0f };
	unsigned m_pathFrame = 0;
	bool m_flagSet = false;
	bool m_reacquire = true; // + 0x64 (lane FLANK-1): the ctor's 1, cleared and set again only by the update's re-acquisition

	// RW 0x8DBDFF StateMachine::isGoalObjectDestroyed: goal id 0 -> false; an id that no longer resolves -> true; else the object's dead bit (+ 0x458 bit 0)
	bool goalDestroyed() const
	{
		const ObjectID id = machine().goalObjectID();
		if (id == INVALID_ID)
		{
			return false;
		}
		const Object *g = ai().getObject()->logic().findObjectByID(id);
		return !g || g->isEffectivelyDead();
	}

	static Object *victimHorde(Object &v)
	{
		Object *c = v.getContainedBy();
		return c && c->getContain() && c->getContain()->getHordeContainInterface() ? c : &v;
	}
	// RW 0x69519A(victim, 2)
	static bool crushPolicy(Object &self, Object &victim)
	{
		if (!(ObjectCrush::crusherLevel(self) > 0 && ObjectCrush::crusherLevel(self) > ObjectCrush::crushableLevel(victim)))
		{
			return false;
		}
		if (self.testStatus((unsigned)CombatNames::status("RAMPAGING")) || self.testStatus((unsigned)CombatNames::status("FLEE_OFF_MAP")) ||
			self.testModelCondition(CombatNames::modelCondition("CHARGING")))
		{
			return true;
		}
		if (self.getRelationship(victim) != ENEMIES)
		{
			return false;
		}
		ObjectWeapons *w = self.getWeapons();
		Weapon *weapon = w ? w->currentWeapon() : nullptr;
		return !weapon || (weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon);
	}
	// RW 0x742271 (true: a path exists or was requested)
	bool computePath(AIUpdateInterface &h, Object &self, Object &goal, bool forceIn)
	{
		if (h.mover().blockedFrames() > 0)
		{
			return false; // RW 0x742297: AI + 0x16C > 0
		}
		// RW 0x7422B4: no path (+ 0x140) and none pending (+ 0x3B1) forces the re-path (lane FLANK-1); onEnter's destroyed path forces it too
		const bool force = forceIn || (h.mover().path() == nullptr && !h.mover().isWaitingForPath());
		if (!force && h.frame() - m_pathFrame < (unsigned)LOGICFRAMES_PER_SECOND)
		{
			return true;
		}
		m_pathFrame = h.frame();
		Object &v = *victimHorde(goal);
		const Coord3D now = *v.getPosition();
		if (!force && m_move)
		{
			const float mx = SimMath::subf32(now.x, m_victimPos.x), my = SimMath::subf32(now.y, m_victimPos.y);
			const float ox = SimMath::subf32(now.x, self.getPosition()->x), oy = SimMath::subf32(now.y, self.getPosition()->y);
			const float moved = SimMath::addf32(SimMath::mulf32(my, my), SimMath::mulf32(mx, mx));
			const float dist = SimMath::mulf32(SimMath::addf32(SimMath::mulf32(oy, oy), SimMath::mulf32(ox, ox)), 0.01f);
			if (!(moved > dist))
			{
				return true; // RW 0x741450: the same position
			}
		}
		ObjectWeapons *w = self.getWeapons();
		if (!w || !w->currentWeapon())
		{
			return false;
		}
		m_victimPos = now;
		if (!crushPolicy(self, v))
		{
			return false;
		}
		float dx = SimMath::subf32(m_victimPos.x, self.getPosition()->x), dy = SimMath::subf32(m_victimPos.y, self.getPosition()->y);
		float dz = SimMath::subf32(m_victimPos.z, self.getPosition()->z);
		const float len = (float)SimMath::length3d(dx, dy, dz);
		if (len != 0.0f)
		{
			// RW 0x405553 (lane FLANK-1): inv = 1 / len (divss), x * inv, inv * y, inv * z (mulss)
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(dx, inv);
			dy = SimMath::mulf32(inv, dy);
			dz = SimMath::mulf32(inv, dz);
		}
		const float d = SimMath::mulf32(CombatQueries::boundingCircleRadius(self), 2.0f);
		// RW 0x7422E1 .. 0x742308: the point keeps its z (lane FLANK-1: the port snapped it to the ground)
		Coord3D g{ SimMath::addf32(SimMath::mulf32(dx, d), m_victimPos.x), SimMath::addf32(m_victimPos.y, SimMath::mulf32(dy, d)), SimMath::addf32(m_victimPos.z, SimMath::mulf32(dz, d)) };
		if (!m_move)
		{
			m_move = std::make_unique<AIMoveToState>(h.mover(), g, true);
		}
		else
		{
			m_move->setGoal(g);
		}
		return true;
	}
	void setFlag(Object &self, bool on)
	{
		self.setStatus((unsigned)CombatNames::statuses().isMeleeAttacking, on);
		m_flagSet = on;
	}
	StateReturnType onEnter() override
	{
		AIUpdateInterface &h = ai();
		Object &self = *h.getObject();
		if (self.testStatus((unsigned)CombatNames::status("STAND_GROUND")) || !CrushTemplateInfo::cached(self).useCrushAttack)
		{
			return STATE_SUCCESS;
		}
		if (goalDestroyed())
		{
			return STATE_FAILURE; // RW 0x74F259 (lane FLANK-1)
		}
		m_pathFrame = 0; // RW 0x74F289: + 0x4C = 0, + 0x48 = 0
		Object *goal = machine().goalObject();
		if (!goal)
		{
			return STATE_FAILURE;
		}
		// RW 0x74F29C .. 0x74F2AD (lane FLANK-1): a goal with a PhysicsBehavior that is STUNNED_FLAILING or in FREEFALL (RW 0x792973)
		static const int kFlailing = CombatNames::modelCondition("STUNNED_FLAILING"), kFreefall = CombatNames::modelCondition("FREEFALL");
		if (PhysicsBehavior::find(*goal) && (goal->testModelCondition(kFlailing) || goal->testModelCondition(kFreefall)))
		{
			return STATE_FAILURE;
		}
		if (!crushPolicy(self, *goal))
		{
			return STATE_SUCCESS;
		}
		m_victimPos = *goal->getPosition(); // RW 0x74F2BE: + 0x50 = the goal's position
		h.mover().destroyPath();             // RW 0x74F2CD: RW 0x66276B
		m_move.reset();
		if (!computePath(h, self, *goal, true))
		{
			return STATE_SUCCESS;
		}
		setFlag(self, true);
		return m_move->onEnter();
	}
	StateReturnType update() override
	{
		AIUpdateInterface &h = ai();
		Object &self = *h.getObject();
		h.setPathExtraDistance(50.0f); // RW 0xBD88C4
		Object *goal = machine().goalObject();
		// HordeContainInterface slot 0x154 of the unit itself: a lone unit (the per-unit melee machine's 0xE9, lane PHYS-1) has none
		HordeAIUpdate *horde = dynamic_cast<HordeAIUpdate *>(&h);
		if (!ObjectCrush::canCrush(self) && goal && horde && horde->isMeleeTargetReady(*goal))
		{
			h.setLocomotorGoalNone();
			return STATE_SUCCESS;
		}
		static const unsigned kFleeOffMap = (unsigned)CombatNames::status("FLEE_OFF_MAP");
		if (!goalDestroyed() && (!goal || (!goal->isEffectivelyDead() && !goal->testStatus(kFleeOffMap))))
		{
			if (!goal)
			{
				setFlag(self, ObjectCrush::canCrush(self)); // RW 0x74F4CF with FAILURE: a goal id of 0
				return STATE_FAILURE;
			}
			if (!crushPolicy(self, *goal))
			{
				return STATE_SUCCESS;
			}
			if (goal->testStatus((unsigned)CombatNames::status("NOT_IN_WORLD")) || InvisibilityManager::isStealthedAndUndetected(*goal, self.getControllingPlayer())) // RW 0x74F3F8 (lane STEALTH-1)
			{
				return STATE_FAILURE;
			}
			h.setCurrentVictim(goal);
			if (!computePath(h, self, *goal, false) || !m_move)
			{
				return STATE_FAILURE;
			}
			const StateReturnType code = m_move->update();
			if (code != STATE_CONTINUE)
			{
				if (goal->isEffectivelyDead() || !self.isKindOfName("ARMY_OF_DEAD"))
				{
					return STATE_SUCCESS;
				}
				h.mover().destroyPath(); // RW 0x74F47F: RW 0x66276B, then computePath
				if (!computePath(h, self, *goal, true))
				{
					return STATE_SUCCESS;
				}
				setFlag(self, true);
				return m_move->onEnter();
			}
			setFlag(self, ObjectCrush::canCrush(self));
			return code;
		}
		// RW 0x74F37D .. 0x74F3D5: the goal is destroyed, dead or fleeing off the map
		h.setPathExtraDistance(0.0f);
		if (m_reacquire && ObjectCrush::canCrush(self))
		{
			m_reacquire = false;
			const float range = h.world().pathfinder().config().meleeApproachDist; // AIData + 0x94
			Object *next = AI::findClosestEnemy(self, range, AI::CAN_ATTACK, h.attackInfo(), nullptr, 0); // RW 0x701443(self, range, 2, AI + 0x70, 0, 0): lane TARGET-1's port
			if (next && crushPolicy(self, *next))
			{
				machine().setGoalObject(next->getID()); // vslot 0x38
				if (!computePath(h, self, *next, false))
				{
					return STATE_FAILURE;
				}
				m_reacquire = true;
			}
		}
		if (!m_move)
		{
			return STATE_SUCCESS;
		}
		const StateReturnType code = m_move->update();
		return code == STATE_CONTINUE ? STATE_CONTINUE : code;
	}
	void onExit(StateExitType) override
	{
		if (m_move)
		{
			m_move->onExit();
			m_move.reset();
		}
		if (!ai().isDestroying())
		{
			ai().getObject()->setStatus((unsigned)CombatNames::statuses().isMeleeAttacking, false);
		}
		m_flagSet = false;
	}
	void crc(StateHasher &h) const override
	{
		h.addBool(m_move != nullptr);
		if (m_move)
		{
			m_move->crc(h);
		}
		h.addFloat(m_victimPos.x);
		h.addFloat(m_victimPos.y);
		h.addFloat(m_victimPos.z);
		h.addU32(m_pathFrame);
		h.addBool(m_flagSet);
		h.addBool(m_reacquire); // lane FLANK-1
	}
	// lane SAVE-4c (obfme:AIWorld): AIAttackMeleeSquishState::xfer RW 0x74243F is (1, 1): the internal move, a position, a cell, a frame and a bool; OpenBFME layout
	// (S-4220): crc's fields; version 2 (lane FLANK-1) adds the re-acquire byte (+ 0x64; a version 1 save keeps the ctor's 1)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 2 };
		x.xferVersion(v);
		xferMove(x, ai().mover(), m_move);
		AIXfer::coord(x, m_victimPos);
		x.xferUnsignedInt(&m_pathFrame);
		x.xferBool(&m_flagSet);
		if (v.current >= 2)
		{
			x.xferBool(&m_reacquire);
		}
		else
		{
			m_reacquire = true;
		}
	}
};

HordeAIUpdate &hordeAi(AIState &s)
{
	return static_cast<HordeAIUpdate &>(s.ai());
}

// AIAttackMeleeHordeApproachTargetState (state 0xC9; ctor RW 0x7443C0, vtable RW 0xC28D20), lane HORDE-2 from the RotWK bodies read with Ghidra:
//   * onEnter RW 0x74EB5C: no goal or a stealthed goal -> FAILURE; a goal that is fleeing (RW 0x7468D5) or not yet ready (HordeContainInterface slot 0x154) is approached:
//     unless the horde has STAND_GROUND / the AI byte +0x3CC, computePath (vslot 0x44 RW 0x74A594) then the move; a ready, not fleeing goal -> SUCCESS;
//   * update RW 0x74AA03: no goal -> FAILURE; a fleeing goal (it faces away from the horde and its locomotor is faster than a quarter of its maximum speed, RW 0x5E4CA7):
//     the desired speed becomes the horde's speed + the gap (edge distance, >= 0) and RUNNING_DOWN_FROM_BEHIND (75) is set while the gap is below the horde's maximum
//     speed; else the flag is cleared when set; then setMeleeTargetID; a fleeing goal: the run-down (slot 0x158 RW 0x87631F: with RUNNING_DOWN_FROM_BEHIND every member that
//     is not attacking attacks the goal), else a ready goal -> setLocomotorGoalNone, SUCCESS; a goal out of the world or stealthed -> FAILURE; computePath false -> SUCCESS;
//     the move's end -> SUCCESS;
//   * computePath RW 0x74A594: the goal (its horde for a member) moved more than 1/10 of its distance (RW 0x741450) or there is no path, at most every
//     LOGICFRAMES_PER_SECOND frames; path extra distance 100; a fleeing goal is led by its facing * (bounding radius * 2 + 60); else the goal's position;
//   * onExit RW 0x74AC0C: RUNNING_DOWN_FROM_BEHIND off, desired speed 999999, and a horde within sqrt(12.5) of the goal point snaps onto it.
// Lane PHYS-1 (review r4) ports computePath's non-fleeing chain with inferences (see nonFleeingGoal: the 0.7 * radius pullback, RW 0x6F2F8F / 0x746783 / 0x7463E8 /
// 0x6FB67A through the port's line test, valid-position steps and destination adjustments, the progress test, the ordinary request RW 0x667ED1).
// INFERENCE (stop S-590): not ported STAND_GROUND's RW 0x6FF7FA decision, the machine-busy test and the
// snap condition AI slot 0x224 (taken as "not moving").
static bool targetFleeing(Object &horde, Object &goalIn) // RW 0x7468D5
{
	if (!CombatQueries::isAlive(goalIn))
	{
		return false;
	}
	Object *goal = &goalIn;
	if (goal->testStatus((unsigned)CombatNames::statuses().hordeMember) && goal->getContainedBy())
	{
		goal = goal->getContainedBy();
	}
	const float fx = SimMath::cosDet(goal->getOrientation()), fy = SimMath::sinDet(goal->getOrientation());
	const float dx = SimMath::subf32(goal->getPosition()->x, horde.getPosition()->x), dy = SimMath::subf32(goal->getPosition()->y, horde.getPosition()->y);
	if (!(0.0f <= SimMath::addf32(SimMath::mulf32(fy, dy), SimMath::mulf32(fx, dx))))
	{
		return false;
	}
	AIUpdateInterface *ai = goal->getAIUpdateInterface();
	Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
	return loco && loco->isFasterThanQuarterSpeed(ai->locomotorHost());
}

struct HordeMeleeApproachState : AIState
{
	explicit HordeMeleeApproachState(AIStateMachine &m) : AIState(m, "AIAttackMeleeHordeApproachTargetState") {}
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_prev{ 0.0f, 0.0f, 0.0f };
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f };
	unsigned m_repathFrame = 0;

	bool m_failed = false; // RW + 0x5C: the last computePath found no progress
	// lane ATTACKPATH-1: RW + 0x5D, set by the constructor (RW 0x7443F1, vtable RW 0xC28D20), cleared by onEnter for a computer player (RW 0x74EB75) and by
	// onExit (RW 0x74AC1D). While set, computePath skips the second walk back (RW 0x746783), a refused AdjustHordeMeleeDestination still requests the path to the
	// unadjusted point and the request's end is reserved first (RW 0x68B3AB)
	bool m_firstApproach = true;
	unsigned m_keptRequests = 0; // lane PHYS-1: computePath calls that kept the existing request (RW 0x6FB67A refused)

	// lane PHYS-1: RW 0x74A594's non-fleeing chain. TARGET: the goal point starts at the target's position; d = normalize(target - horde) * (horde bounding circle * 0.7,
	// RW 0xBDE0A8); on the ground layer the pulled-back point p = goal - d replaces the goal when IsLineBlocked RW 0x6F2F8F(horde, surfaces, ground, goal, p) is
	// false; a STRUCTURE target (template + 0x108 bit 0x80) runs RW 0x746783 on it; RW 0x7463E8 must find the point reachable; then the reservation goes
	// (RW 0x68B401), RW 0x746783 again, RW 0x6FB67A adjusts it (a refusal keeps the old request: computePath true without touching the move, review r5); a point
	// that makes progress (half the 3D span |point - horde| at most the gain of the squared, clamped edge distance RW 0x6CA525, review r5) or with no path is
	// requested ordinarily; else + 0x5C is set and computePath returns false.
	// Lane ATTACKPATH-1 ported the helpers from the RotWK disassembly (BFME2 decomp byte-matched: AIPathfindHordeMelee.cpp): RW 0x6F2F8F is the port's
	// isLineBlocked (the port had the reverse line and the reverse answer), RW 0x746783 is Pathfinder::hordeMeleeStepBack (one-point IsHordeMeleeLinePassable
	// RW 0x6F2FFF tests over |goal - horde| / 20 - 1 steps of 10), RW 0x6FB67A is Pathfinder::adjustHordeMeleeDestination (the port used adjustDestination).
	// The state's + 0x5D (m_firstApproach: set by the constructor, cleared for a computer player and on exit) skips the second walk back, lets a refused
	// adjustment request the unadjusted point and reserves the point first.
	// INFERENCE (stop S-590): RW 0x7463E8 is Pathfinder::meleeBackOff (round 6, its own inferences in S-787); the container slot 0x154 test of the progress
	// branch is not read
	enum class Chain
	{
		Fail,         // RW + 0x5C, computePath false
		KeepRequest,  // RW 0x74A63F: RW 0x6FB67A refused: computePath true, the existing request / move untouched
		IssueRequest  // RW 0x74A9DF: the ordinary request RW 0x667ED1 to m_goal
	};

	Chain nonFleeingGoal(HordeAIUpdate &h, Object &self, Object &goal, const Coord3D &now)
	{
		Pathfinder &pf = h.world().pathfinder();
		PathfindObject &obj = h.adapter();
		const PathfindLocomotorInfo info = h.locomotorInfo();
		const Coord3D selfPos = *self.getPosition();
		m_goal = now;
		float dx = SimMath::subf32(now.x, selfPos.x), dy = SimMath::subf32(now.y, selfPos.y), dz = SimMath::subf32(now.z, selfPos.z);
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, dz)));
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(dx, inv);
			dy = SimMath::mulf32(dy, inv);
			dz = SimMath::mulf32(dz, inv);
		}
		const float k = SimMath::mulf32(CombatQueries::boundingCircleRadius(self), 0.7f);
		dx = SimMath::mulf32(k, dx);
		dy = SimMath::mulf32(dy, k);
		dz = SimMath::mulf32(dz, k);
		const Coord3D p{ SimMath::subf32(m_goal.x, dx), SimMath::subf32(m_goal.y, dy), SimMath::subf32(m_goal.z, dz) };
		// lane ATTACKPATH-1, TARGET RW 0x74A7A4 .. 0x74A7D0: Pathfinder::IsLineBlocked (RW 0x6F2F8F, the port's isLineBlocked) from the goal to p, with the AI's
		// surfaces (+ 0x1DC) on the ground layer; p replaces the goal when the line is NOT blocked (the port before took p when the reverse line was not passable)
		if (!pf.isLineBlocked(&obj, info.validSurfaces, LAYER_GROUND, m_goal, p))
		{
			m_goal = p;
		}
		if (goal.isKindOf((unsigned)CombatNames::kinds().structure))
		{
			pf.hordeMeleeStepBack(obj, info, &m_goal); // RW 0x746783 (lane ATTACKPATH-1)
		}
		// RW 0x7463E8 (Pathfinder::meleeBackOff, round 6): the open-area test, the 20-unit back-off toward the horde and RW 0x6F3C87
		{
			ObjectWeapons *w = self.getWeapons();
			if (!pf.meleeBackOff(obj, info, w ? w->currentAttackRange() : 0.0f, &m_goal))
			{
				return Chain::Fail;
			}
		}
		pf.removeGoal(obj); // RW 0x68B401
		if (!m_firstApproach)
		{
			pf.hordeMeleeStepBack(obj, info, &m_goal); // RW 0x746783 (lane ATTACKPATH-1), only with + 0x5D clear
		}
		Coord3D q = m_goal;
		// RW 0x6FB67A Pathfinder::AdjustHordeMeleeDestination (lane ATTACKPATH-1; the port used adjustDestination); a refusal keeps the request only with + 0x5D
		// clear, else the unadjusted point goes on (RW 0x74A8F2 .. 0x74A8FE)
		const bool adjusted = !ApproachMath::forceContactRefusalForTests() && pf.adjustHordeMeleeDestination(obj, info, &q);
		if (!adjusted && !m_firstApproach)
		{
			return Chain::KeepRequest; // RW 0x74A8F2 tests AL, 0x74A63F returns success before the request
		}
		m_goal = q;
		// RW 0x74A918 / 0x74A925: the squared, clamped edge distances to the target (RW 0x6CA525) before and after; the gain against half the 3D span
		if (!ApproachMath::progressAllowsRequest(now, CombatQueries::boundingCircleRadius(goal), selfPos, m_goal, h.mover().path() != nullptr))
		{
			return Chain::Fail;
		}
		if (m_firstApproach)
		{
			pf.updateGoal(obj, &m_goal, LAYER_GROUND); // RW 0x74A9CB .. 0x74A9D5: the reservation RW 0x68B3AB before the request (lane ATTACKPATH-1)
		}
		return Chain::IssueRequest;
	}

	// RW 0x74A594 (true: a path exists or was requested)
	bool computePath(HordeAIUpdate &h, Object &goalIn, bool force)
	{
		Object &self = *h.getObject();
		if (!force && m_move && h.frame() - m_repathFrame < (unsigned)LOGICFRAMES_PER_SECOND)
		{
			return true;
		}
		Object *goal = &goalIn;
		if (goal->testStatus((unsigned)CombatNames::statuses().hordeMember) && goal->getContainedBy())
		{
			goal = goal->getContainedBy();
		}
		const Coord3D now = *goal->getPosition();
		if (!force && m_move)
		{
			const float mx = SimMath::subf32(now.x, m_prev.x), my = SimMath::subf32(now.y, m_prev.y);
			const float ox = SimMath::subf32(now.x, self.getPosition()->x), oy = SimMath::subf32(now.y, self.getPosition()->y);
			if (!(SimMath::addf32(SimMath::mulf32(my, my), SimMath::mulf32(mx, mx)) > SimMath::mulf32(SimMath::addf32(SimMath::mulf32(oy, oy), SimMath::mulf32(ox, ox)), 0.01f)))
			{
				return true;
			}
		}
		m_repathFrame = h.frame();
		if (!self.getWeapons() || !self.getWeapons()->currentWeapon())
		{
			return false;
		}
		m_prev = now;
		h.mover().ignoreObstacle(PATHFIND_INVALID_ID); // RW 0x74A594 calls RW 0x66831A(0) (ignoreObstacle(0)) before the path extra distance (lane ATTACKPATH-1)
		h.setPathExtraDistance(100.0f); // RW 0xBD88D8
		m_goal = now;
		m_failed = false;
		// RW 0x74A6C2 .. 0x74A6DF: the lead needs a fleeing target AND a quick path from the horde to it (RW 0x6F5BB0, the port's clientSafeQuickDoesPathExist;
		// lane ATTACKPATH-1: the port led every fleeing target)
		if (targetFleeing(self, *goal) &&
			(h.world().mapReady() && h.world().pathfinder().clientSafeQuickDoesPathExist(h.locomotorInfo(), self.getPosition(), goal->getPosition())))
		{
			const float lead = SimMath::addf32(SimMath::mulf32(CombatQueries::boundingCircleRadius(*goal), 2.0f), 60.0f);
			m_goal.x = SimMath::addf32(m_goal.x, SimMath::mulf32(lead, SimMath::cosDet(goal->getOrientation())));
			m_goal.y = SimMath::addf32(SimMath::mulf32(SimMath::sinDet(goal->getOrientation()), lead), m_goal.y);
			m_goal.z = self.logic().getGroundHeight(m_goal.x, m_goal.y);
			if (h.world().mapReady())
			{
				h.world().pathfinder().adjustToPossibleDestination(h.adapter(), h.locomotorInfo(), &m_goal); // RW 0x6F3C87 (lane PHYS-1)
			}
		}
		else if (h.world().mapReady())
		{
			// lane PHYS-1: RW 0x74A594's non-fleeing chain (see the S-590 note above the state): the reachable contact point, then an ordinary request (RW 0x667ED1)
			const Chain c = nonFleeingGoal(h, self, *goal, now);
			if (c == Chain::Fail)
			{
				m_failed = true; // RW + 0x5C
				return false;
			}
			if (c == Chain::KeepRequest)
			{
				++m_keptRequests;
				return true; // the existing request, path and move stay as they are (review r5)
			}
		}
		else
		{
			m_goal = now;
		}
		if (!m_move)
		{
			m_move = std::make_unique<AIMoveToState>(h.mover(), m_goal, false); // RW: + 0x48 (adjust) cleared before RW 0x667ED1(goal, 0)
		}
		else
		{
			m_move->setGoal(m_goal);
		}
		return true;
	}

	StateReturnType onEnter() override
	{
		// RW 0x74EB6A .. 0x74EB75: a computer player's horde clears + 0x5D first (lane ATTACKPATH-1)
		{
			const Player *pl = ai().getObject()->getControllingPlayer();
			if (pl && pl->getPlayerType() == PLAYER_COMPUTER)
			{
				m_firstApproach = false;
			}
		}
		Object *victim = machine().goalObject();
		if (!victim || InvisibilityManager::isStealthedAndUndetected(*victim, ai().getObject()->getControllingPlayer())) // RW 0x74EB95: a stealthed goal fails (lane STEALTH-1)
		{
			return STATE_FAILURE;
		}
		if (!CombatQueries::isAlive(*victim))
		{
			return STATE_SUCCESS;
		}
		HordeAIUpdate &h = hordeAi(*this);
		Object &self = *h.getObject();
		if (!targetFleeing(self, *victim) && h.isMeleeTargetReady(*victim))
		{
			return STATE_SUCCESS;
		}
		if (self.testStatus((unsigned)CombatNames::status("STAND_GROUND")))
		{
			return STATE_SUCCESS; // RW 0x6FF7FA decides (S-590)
		}
		m_move.reset();
		if (!computePath(h, *victim, true))
		{
			return STATE_FAILURE;
		}
		if (!m_move)
		{
			// lane ATTACKPATH-1, TARGET RW 0x74EC7F .. 0x74EC85: after a computePath that returned true onEnter always runs the internal move's onEnter (RW 0x74DADA,
			// + 0x48 cleared) toward the state's goal (+ 0x20): a refused contact adjustment (the request kept) still walks to the pulled-back point. The port
			// returned with no move and the horde stood (test_exit1_motion's horde at frame 1382)
			m_move = std::make_unique<AIMoveToState>(h.mover(), m_goal, false);
		}
		return m_move->onEnter();
	}
	StateReturnType update() override
	{
		HordeAIUpdate &h = hordeAi(*this);
		Object &self = *h.getObject();
		Object *victim = machine().goalObject();
		if (!victim)
		{
			return STATE_FAILURE;
		}
		const int runningDown = CombatNames::statuses().runningDownFromBehind;
		const bool fleeing = targetFleeing(self, *victim);
		if (fleeing)
		{
			const float e = edgeDistance(self, *victim);
			Locomotor *loco = h.curLocomotor();
			float gap = SimMath::addf32(loco ? loco->speed() : 0.0f, e);
			gap = gap < 0.0f ? 0.0f : gap;
			const float maxSpeed = loco ? loco->getMaxSpeedForCondition(h.locomotorHost()) : 999999.0f;
			h.setDesiredSpeed(gap);
			self.setStatus((unsigned)runningDown, gap < maxSpeed);
		}
		else if (self.testStatus((unsigned)runningDown))
		{
			self.setStatus((unsigned)runningDown, false);
		}
		if (fleeing)
		{
			// RW 0x87631F: the run-down
			if (self.testStatus((unsigned)runningDown))
			{
				for (Object *m : aliveMembers(self))
				{
					AIUpdateInterface *ai = m->getAIUpdateInterface();
					if (ai && !ai->isAttacking())
					{
						ai->aiAttackObject(victim, CMD_FROM_AI);
					}
				}
			}
		}
		else if (h.isMeleeTargetReady(*victim))
		{
			h.setLocomotorGoalNone();
			return STATE_SUCCESS;
		}
		if (victim->testStatus((unsigned)CombatNames::status("NOT_IN_WORLD")) || InvisibilityManager::isStealthedAndUndetected(*victim, self.getControllingPlayer())) // RW 0x74ABBC (lane STEALTH-1)
		{
			return STATE_FAILURE;
		}
		h.setCurrentVictim(victim);
		if (!computePath(h, *victim, !m_move))
		{
			return STATE_SUCCESS;
		}
		if (!m_move)
		{
			return STATE_CONTINUE; // the existing request goes on (a kept request with no move of this state)
		}
		const StateReturnType code = m_move->update();
		return code == STATE_CONTINUE ? STATE_CONTINUE : STATE_SUCCESS;
	}
	void onExit(StateExitType) override
	{
		if (m_move)
		{
			m_move->onExit();
			m_move.reset();
		}
		m_firstApproach = false; // RW 0x74AC1D (lane ATTACKPATH-1)
		if (ai().isDestroying())
		{
			return;
		}
		HordeAIUpdate &h = hordeAi(*this);
		Object &self = *h.getObject();
		if (self.isDestroyed())
		{
			return;
		}
		self.setStatus((unsigned)CombatNames::statuses().runningDownFromBehind, false);
		h.setDesiredSpeed(999999.0f); // RW 0xC27440
		h.mover().ignoreObstacle(PATHFIND_INVALID_ID); // RW 0x74AC57: ignoreObstacle(0) (lane ATTACKPATH-1)
		const float dx = SimMath::subf32(m_goal.x, self.getPosition()->x), dy = SimMath::subf32(m_goal.y, self.getPosition()->y);
		if (!h.isMoving() && SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)) < 12.5f) // RW 0xC2A058
		{
			HordeContainInterface *hci = self.getContain() ? self.getContain()->getHordeContainInterface() : nullptr;
			if (hci)
			{
				hci->setLocomoting(true);
			}
			self.setPosition(&m_goal);
			if (hci)
			{
				hci->setLocomoting(false);
			}
		}
	}
	void crc(StateHasher &h) const override
	{
		h.addBool(m_move != nullptr);
		if (m_move)
		{
			m_move->crc(h);
		}
		h.addFloat(m_prev.x);
		h.addFloat(m_prev.y);
		h.addFloat(m_prev.z);
		h.addFloat(m_goal.x);
		h.addFloat(m_goal.y);
		h.addFloat(m_goal.z);
		h.addU32(m_repathFrame);
		h.addBool(m_failed); // lane PHYS-1
		h.addU32(m_keptRequests);
		h.addBool(m_firstApproach); // lane ATTACKPATH-1
	}
	void xfer(Xfer &x) override // lane SAVE-4c (obfme:AIWorld, OpenBFME layout: S-4220)
	{
		XferVersion v{ 1, 2 }; // lane ATTACKPATH-1: version 2 adds + 0x5D
		x.xferVersion(v);
		xferMove(x, ai().mover(), m_move);
		AIXfer::coord(x, m_prev);
		AIXfer::coord(x, m_goal);
		x.xferUnsignedInt(&m_repathFrame);
		x.xferBool(&m_failed);
		x.xferUnsignedInt(&m_keptRequests);
		if (v.current >= 2)
		{
			x.xferBool(&m_firstApproach);
		}
		else if (x.isLoading())
		{
			m_firstApproach = false; // a version 1 save: the port before ATTACKPATH-1 behaved as if + 0x5D were clear
		}
	}
};

// B1 AIAttackMeleeHordeWaitState: the melee phase
struct HordeMeleeWaitState : AIState
{
	explicit HordeMeleeWaitState(AIStateMachine &m) : AIState(m, "AIAttackMeleeHordeWaitState") {}
	unsigned m_waitUntil = 0;

	StateReturnType onEnter() override
	{
		Object *victim = machine().goalObject();
		if (!victim || !CombatQueries::isAlive(*victim))
		{
			return STATE_SUCCESS;
		}
		HordeAIUpdate &h = hordeAi(*this);
		if (!h.isMeleeTargetReady(*victim))
		{
			// not ready: the targets must be within 40 of each other (60 for a siege tower off layer 1: not ported)
			if (edgeDistance(*h.getObject(), *victim) > 40.0f)
			{
				return STATE_FAILURE;
			}
		}
		m_waitUntil = h.frame() + kReadyCacheFrames;
		h.beginMelee(*victim);
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		HordeAIUpdate &h = hordeAi(*this);
		Object *victim = machine().goalObject();
		if (!victim || !CombatQueries::isAlive(*victim))
		{
			return STATE_SUCCESS;
		}
		h.setCurrentVictim(victim);
		if (h.isMeleeTargetReady(*victim))
		{
			h.beginMelee(*victim);
			m_waitUntil = h.frame() + kReadyCacheFrames;
		}
		else if (h.frame() >= m_waitUntil)
		{
			return STATE_FAILURE; // the contact is over: WaitPath
		}
		// lane MOVE-2 r3: the melee behaviour's update is the contain's (RW 0x872FD8 -> 0x870A1B, HordeAIUpdate::containMeleeUpdate), not this state's
		return STATE_CONTINUE;
	}
	void onExit(StateExitType) override
	{
		if (!ai().isDestroying()) // an object being deleted: the derived AI and the modules are already gone
		{
			hordeAi(*this).endMelee();
		}
	}
	void crc(StateHasher &h) const override { h.addU32(m_waitUntil); }
	void xfer(Xfer &x) override // lane SAVE-4c: AIAttackMeleeHordeWaitState::xfer RW 0x7420C7 is (1, 1): the frame
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_waitUntil);
	}
};

// B1 AIAttackMeleeHordeWaitPathState
struct HordeMeleeWaitPathState : AIState
{
	explicit HordeMeleeWaitPathState(AIStateMachine &m) : AIState(m, "AIAttackMeleeHordeWaitPathState") {}
	unsigned m_nextCheck = 0;
	int m_failures = 0;
	StateReturnType onEnter() override
	{
		m_nextCheck = hordeAi(*this).frame() + kWaitPathRetry;
		m_failures = 0;
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		HordeAIUpdate &h = hordeAi(*this);
		Object *victim = machine().goalObject();
		if (!victim || !CombatQueries::isAlive(*victim))
		{
			return STATE_SUCCESS;
		}
		if (h.frame() < m_nextCheck)
		{
			return STATE_CONTINUE;
		}
		m_nextCheck = h.frame() + kWaitPathRetry;
		++m_failures;
		if (m_failures > 5)
		{
			return STATE_SUCCESS; // about 7 seconds without a way to the target: the attack ends
		}
		return STATE_FAILURE; // a path to the target exists (open ground): approach again
	}
	void crc(StateHasher &h) const override
	{
		h.addU32(m_nextCheck);
		h.addI32(m_failures);
	}
	void xfer(Xfer &x) override // lane SAVE-4c: AIAttackMeleeHordeWaitPathState::xfer RW 0x7420F9 is (1, 2): u32, i32 (these two)
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferUnsignedInt(&m_nextCheck);
		x.xferInt(&m_failures);
	}
};
} // namespace

std::unique_ptr<AIStateMachine> HordeAIUpdate::makeHordeAttackMachine(AIAttackState *att, int meleeChoice, bool *madeMelee)
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(*this, "AIHordeMachine");
	ObjectWeapons *w = getObject()->getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	const bool melee = meleeChoice >= 0 ? meleeChoice != 0
	                                    : getObject()->isKindOf((unsigned)CombatNames::kinds().meleeHorde) && weapon && weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon;
	if (madeMelee)
	{
		*madeMelee = melee;
	}
	m->defineState(HORDE_SQUISH, std::make_unique<HordeSquishState>(*m), HORDE_APPROACH, MACHINE_DONE_FAILURE);
	if (melee)
	{
		m->defineState(HORDE_APPROACH, std::make_unique<HordeMeleeApproachState>(*m), HORDE_WAIT, MACHINE_DONE_FAILURE);
		m->defineState(HORDE_WAIT, std::make_unique<HordeMeleeWaitState>(*m), MACHINE_DONE_SUCCESS, HORDE_WAIT_PATH);
		m->defineState(HORDE_WAIT_PATH, std::make_unique<HordeMeleeWaitPathState>(*m), MACHINE_DONE_SUCCESS, HORDE_APPROACH);
	}
	else
	{
		m->defineState(HORDE_APPROACH, std::make_unique<AIAttackApproachState>(*m, att, false), HORDE_FIRE, MACHINE_DONE_FAILURE);
		m->defineState(HORDE_FIRE, std::make_unique<AIAttackFireState>(*m, att), HORDE_WAIT, HORDE_APPROACH);
		m->defineState(HORDE_WAIT, std::make_unique<AIWaitUntilFinishedFiringState>(*m), HORDE_APPROACH, HORDE_APPROACH);
	}
	return m;
}

HordeAIUpdate::MemberRecord &HordeAIUpdate::recordOf(ObjectID id)
{
	for (MemberRecord &r : m_records)
	{
		if (r.id == id)
		{
			return r;
		}
	}
	m_records.push_back(MemberRecord());
	m_records.back().id = id;
	return m_records.back();
}

void HordeAIUpdate::resetMeleeRuntime(int kind)
{
	m_records.clear();
	for (Object *m : membersOf()) // RW 0x990B5B: resized to the members, each initialised by RW 0x98F5A5
	{
		MemberRecord r;
		r.id = m->getID();
		r.dirty = true;
		m_records.push_back(r);
	}
	m_newTarget = kind == MeleeBehaviorModuleData::AMOEBA; // RW 0x98FE62 (the other kinds' objects have no such flag)
}

std::vector<Object *> HordeAIUpdate::membersOf() const
{
	return aliveMembers(*getObject());
}

// RW 0x98FA8E
bool HordeAIUpdate::isMeleeTargetReady(Object &targetIn)
{
	Object *horde = getObject();
	Object *target = hordeOfTarget(targetIn);
	Object &t = target ? *target : targetIn;
	if (t.getID() == m_cacheTarget && frame() < m_cacheExpiry)
	{
		return true;
	}
	ObjectWeapons *w = horde->getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate() || !weapon->getTemplate()->m_meleeWeapon)
	{
		return false;
	}
	const float range = weapon->getTemplate()->m_attackRange;
	const bool structure = t.isKindOf((unsigned)CombatNames::kinds().structure);
	if (!structure && absF(SimMath::subf32(t.getPosition()->z, horde->getPosition()->z)) > 20.0f)
	{
		return false;
	}
	const float edge = edgeDistance(*horde, t);
	float reach = SimMath::addf32(range, 10.0f);
	if (reach < 0.1f)
	{
		reach = 0.1f;
	}
	const bool close = SimMath::mulf32(edge, edge) < SimMath::mulf32(reach, reach);
	// RW 0x98FA8E (read with Ghidra): the members in contain order; the first decisive one answers. A member that is not attacking (AI slot 0x1BC = the state's
	// isAttack) makes the target ready while close, unless it follows a path (then not ready); an attacking member makes it ready while close or when its goal object
	// is the target, in the target, or shares the target's container
	Object *tContainer = t.getContainedBy();
	bool ready = false;
	for (Object *m : membersOf())
	{
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		if (!ai->isAttacking())
		{
			if (close)
			{
				if (ai->mover().path() && ai->mover().isMoving()) // RW 0x5E2DF4 (inference: the mover is moving)
				{
					return false;
				}
				ready = true;
				break;
			}
			continue;
		}
		Object *v = ai->currentVictim();
		Object *vc = v ? v->getContainedBy() : nullptr;
		if (close || (v && (v == &t || vc == &t || (tContainer && vc == tContainer))))
		{
			ready = true;
			break;
		}
	}
	if (ready)
	{
		m_cacheTarget = t.getID();
		m_cacheExpiry = frame() + kReadyCacheFrames;
	}
	return ready;
}

static bool aiSlot1B8(Object &m);
static Object *amoebaReach(Object &m, Object &target);
static std::vector<Object *> amoebaReachCandidates(Object &target);
static Object *amoebaReachAmong(Object &m, const std::vector<Object *> &cands);
static bool amoebaAttack(Object &m, Object &victim);

// RW 0x86D75E (the target changes) + Amoeba onNewTarget (RW 0x99006A)
void HordeAIUpdate::beginMelee(Object &targetIn)
{
	Object *target = hordeOfTarget(targetIn);
	Object &t = target ? *target : targetIn;
	if (m_engagedTarget == t.getID())
	{
		return;
	}
	m_engagedTarget = t.getID();
	Object *horde = getObject();
	if (HordeContain *hc = hordeContainOf(*horde))
	{
		hc->setMeleeEngaged(true);
	}
	const unsigned delayUntilIdle = [&]() -> unsigned {
		if (HordeContain *hc = hordeContainOf(*horde))
		{
			if (hc->meleeBehaviorData()) // lane INTEG-1: a stance's MeleeBehavior (slot 0x260) replaces the module data's
			{
				return hc->meleeBehaviorData()->m_delayUntilIdle;
			}
		}
		return 2u * (unsigned)LOGICFRAMES_PER_SECOND;
	}();
	// MeleeBehavior::onNewTarget: Amoeba RW 0x99006A / HoldGround RW 0x98BA04 (read with Ghidra)
	m_records.clear();
	m_newTarget = true;
	const MeleeBehaviorModuleData *mb = hordeContainOf(*horde) ? hordeContainOf(*horde)->meleeBehaviorData() : nullptr;
	const bool holdGround = mb && mb->m_kind == MeleeBehaviorModuleData::HOLD_GROUND;
	for (Object *m : membersOf())
	{
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		MemberRecord &r = recordOf(m->getID());
		r.dest = *m->getPosition();
		r.hasDest = true;
		r.dirty = true;
		r.historyCount = 0;
		r.timer = delayUntilIdle;
		r.idle = false;
		if (!holdGround && ai->mover().goalType() == AIGOAL_EXPLICIT_WITH_PATH && ai->mover().path())
		{
			continue; // a member following a path keeps its order
		}
		if (!m->testStatus((unsigned)CombatNames::statuses().isMeleeAttacking) && !aiSlot1B8(*m))
		{
			ai->aiIdle(CMD_FROM_AI);
		}
		if (!holdGround)
		{
			ai->setLocomotorGoalNone(); // AI slot 0x220
		}
	}
	if (holdGround)
	{
		holdGroundTick(t); // RW 0x98BA04 ends with RW 0x69675C on every member
	}
}

// RW 0x86C0F9
void HordeAIUpdate::endMelee()
{
	if (m_engagedTarget == 0)
	{
		return;
	}
	m_engagedTarget = 0;
	m_cacheTarget = 0;
	m_cacheExpiry = 0;
	m_records.clear();
	if (HordeContain *hc = hordeContainOf(*getObject()))
	{
		hc->setMeleeEngaged(false);
	}
}

// RW 0x6F2956 (the Amoeba reach check, RW 0x98F311): the enemy of `target` (its horde) nearest to `m` (RW 0x66352C: the squared nonnegative footprint edge distance) within the member's weapon range.
// RW scans pathfinder cells in rings out to floor(range * 0.1 + 0.1); INFERENCE (S-588): the candidates are the target and its members, ties to the lower id.
static Object *amoebaReach(Object &m, Object &target)
{
	ObjectWeapons *w = m.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate() || !weapon->getTemplate()->m_meleeWeapon)
	{
		return nullptr;
	}
	return amoebaReachAmong(m, amoebaReachCandidates(target));
}

// the reach check's candidates: the target's living members, then the target itself when it is alive and not a horde object
static std::vector<Object *> amoebaReachCandidates(Object &target)
{
	std::vector<Object *> cands = aliveMembers(target);
	if (CombatQueries::isAlive(target) && !isHordeObject(target))
	{
		cands.push_back(&target);
	}
	return cands;
}

static Object *amoebaReachAmong(Object &m, const std::vector<Object *> &cands)
{
	ObjectWeapons *w = m.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate() || !weapon->getTemplate()->m_meleeWeapon)
	{
		return nullptr;
	}
	Object *best = nullptr;
	float bestD = 0.0f;
	// lane UBER-3W (Uber only): the distance first, and the weapon range (its bonus, its reach: 2.5% of a one-thread 30,000-troop clash frame, once per member
	// and candidate) only for a candidate that would beat the best so far; the nearest enemy in range is the same. Uber because the range test leaves the
	// melee reach it computed in the weapon's range host, which then holds another candidate's value (every reader but a point target's sets it first)
	const bool rangeLast = m.logic().uber();
	ObjectID sacrificeTarget = INVALID_ID;
	if (m.testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")))
	{
		if (Object *horde = m.getHordeObject(false))
		{
			if (BloodthirstyUpdate *bloodthirsty = BloodthirstyUpdate::find(*horde))
			{
				sacrificeTarget = bloodthirsty->bestTargetID();
			}
		}
	}
	for (Object *e : cands)
	{
		// RW 0x6F2956: the recorded victim or an object it produced bypasses the relationship veto.
		const bool sacrifice = sacrificeTarget != INVALID_ID && (e->getID() == sacrificeTarget || e->getProducerID() == sacrificeTarget);
		// merge queue q24: RW 0x6F2956 finds its candidates in the pathfinder's cells, so an occupant ENCLOSED in the target (a garrison's or transport's rider,
		// out of the world: RW 0x865D3D -> 0x68C18F) is never one; the member list stands in for the cell scan of a horde only
		if (!e->isInWorld())
		{
			continue;
		}
		if (rangeLast)
		{
			if (!CombatQueries::isAlive(*e) || (!sacrifice && m.getRelationship(*e) != ENEMIES))
			{
				continue;
			}
			const float d2 = CombatQueries::edgeDistanceSquared2D(m, *m.getPosition(), *e, *e->getPosition()); // RW 0x66352C
			if (best && !(d2 < bestD || (d2 == bestD && e->getID() < best->getID())))
			{
				continue;
			}
			if (!w->isWithinAttackRange(*e))
			{
				continue;
			}
			best = e;
			bestD = d2;
			continue;
		}
		// lane UBER-2B: a candidate of a list built before this member (the Uber tick's) may have died since; a fresh list holds only living ones
		if (!CombatQueries::isAlive(*e) || (!sacrifice && m.getRelationship(*e) != ENEMIES) || !w->isWithinAttackRange(*e))
		{
			continue;
		}
		const float d2 = CombatQueries::edgeDistanceSquared2D(m, *m.getPosition(), *e, *e->getPosition()); // RW 0x66352C
		if (!best || d2 < bestD || (d2 == bestD && e->getID() < best->getID()))
		{
			best = e;
			bestD = d2;
		}
	}
	return best;
}

// AI slot 0x1B8 (RW 0x6643FC): a dead unit answers yes; a HORDE_MEMBER whose horde's AI answers yes answers yes; else the current state's slot 0x20, isIdle (the readiness
// RW 0x98FA8E pairs it with slot 0x1BC, isAttack)
static bool aiSlot1B8(Object &m)
{
	if (m.isEffectivelyDead())
	{
		return true;
	}
	if (m.testStatus((unsigned)CombatNames::statuses().hordeMember) && m.getContainedBy())
	{
		if (AIUpdateInterface *h = m.getContainedBy()->getAIUpdateInterface())
		{
			if (h->isIdle())
			{
				return true;
			}
		}
	}
	AIUpdateInterface *ai = m.getAIUpdateInterface();
	return !ai || ai->isIdle();
}

// RW 0x98F327: a member with a MeleeWeapon attacks `victim` (AI RW 0x66D658) and its locomotor goal is cleared (AI vslot 0x220)
static bool amoebaAttack(Object &m, Object &victim)
{
	AIUpdateInterface *ai = m.getAIUpdateInterface();
	ObjectWeapons *w = m.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!ai || !weapon || !weapon->getTemplate() || !weapon->getTemplate()->m_meleeWeapon)
	{
		return false;
	}
	// RW 0x66D658: a unit already in an attack state only changes its goal object (no restart); otherwise a new attack order. INFERENCE (S-588): the port keeps an
	// attack on the same victim untouched and restarts it for another victim (no retarget call in this tree)
	if (ai->isAttacking() && ai->currentVictim() == &victim)
	{
		return false;
	}
	const bool ok = ai->aiAttackObject(&victim, CMD_FROM_AI);
	ai->setLocomotorGoalNone();
	return ok;
}

// RW 0x86F18F (HordeContainInterface slot 0x98, called by the Amoeba update with the horde object): the horde object moves to the member nearest to the members' centroid
// whose position is valid for the horde (RW 0x6EA4E7). INFERENCE (S-588): the first list RW sums (contain + 0x54 entries) is not read; the validity test is the pathfinder's.
void HordeAIUpdate::recentreOnMembers()
{
	Object *horde = getObject();
	const std::vector<Object *> ms = membersOf();
	if (ms.empty())
	{
		return;
	}
	float sx = 0.0f, sy = 0.0f, sz = 0.0f;
	for (Object *m : ms)
	{
		sx = SimMath::addf32(m->getPosition()->x, sx);
		sy = SimMath::addf32(m->getPosition()->y, sy);
		sz = SimMath::addf32(m->getPosition()->z, sz);
	}
	const float inv = SimMath::divf32(1.0f, SimMath::sseFromInt32((int)ms.size()));
	const float cx = SimMath::mulf32(inv, sx), cy = SimMath::mulf32(sy, inv), cz = SimMath::mulf32(sz, inv);
	float best = -1.0f; // RW 0xBD19DC
	Coord3D pos = *horde->getPosition();
	for (Object *m : ms)
	{
		Coord3D p = *m->getPosition();
		if (world().mapReady() && !world().pathfinder().validMovementPosition(&adapter(), locomotorInfo(), adapter().getLayer(), &p))
		{
			continue;
		}
		const float dx = SimMath::subf32(p.x, cx), dy = SimMath::subf32(p.y, cy), dz = SimMath::subf32(p.z, cz);
		const float d = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
		if (best < 0.0f || d < best)
		{
			best = d;
			pos = *m->getPosition();
		}
	}
	if (best >= 0.0f)
	{
		// RW 0x70C201 (Object::setPosition): only the horde object moves; the port's contain would carry the members along unless it is told the move is the horde's own
		HordeContainInterface *hci = horde->getContain() ? horde->getContain()->getHordeContainInterface() : nullptr;
		if (hci)
		{
			hci->setLocomoting(true);
		}
		horde->setPosition(&pos);
		if (hci)
		{
			hci->setLocomoting(false);
		}
	}
}

// lane MOVE-2 r3: RW 0x870A1B (read with Ghidra)
void HordeAIUpdate::containMeleeUpdate()
{
	if (m_engagedTarget == 0)
	{
		return;
	}
	Object *target = getObject()->logic().findObjectByID(m_engagedTarget);
	if (!target)
	{
		endMelee(); // slot 0x138 (RW 0x86C0F9), + 0x2A0 = 0
		return;
	}
	meleeTick(*target); // the melee behaviour's slot 0x14
	// the horde faces the target's container when it has one (target + 0x27C), else the target: its orientation plus the relative angle (RW 0x4B3D8D, 0x70C31E)
	Object *look = target->getContainedBy() ? target->getContainedBy() : target;
	Object *horde = getObject();
	const float rel = emotionRelAngle(*horde, *look->getPosition());
	// RW 0x70C31E turns the horde object only; the port's contain would carry the members along unless it is told the move is the horde's own (as recentreOnMembers)
	HordeContainInterface *hci = horde->getContain() ? horde->getContain()->getHordeContainInterface() : nullptr;
	if (hci)
	{
		hci->setLocomoting(true);
	}
	horde->setOrientation(SimMath::addf32(rel, horde->getOrientation()));
	if (hci)
	{
		hci->setLocomoting(false);
	}
}

bool HordeAIUpdate::meleeDestination(ObjectID member, Coord3D &out) const
{
	const HordeContain *hc = hordeContainOf(*const_cast<HordeAIUpdate *>(this)->getObject());
	const MeleeBehaviorModuleData *mb = hc ? hc->meleeBehaviorData() : nullptr;
	if (!mb || mb->m_kind != MeleeBehaviorModuleData::AMOEBA)
	{
		return false; // HoldGround RW 0x8F8014 (and the unported Swarm): no destination
	}
	for (const MemberRecord &r : m_records)
	{
		if (r.id == member)
		{
			if (!r.hasDest)
			{
				return false;
			}
			out = r.dest;
			return true;
		}
	}
	return false;
}

bool HordeAIUpdate::meleeAlwaysTurns() const
{
	const HordeContain *hc = hordeContainOf(*const_cast<HordeAIUpdate *>(this)->getObject());
	const MeleeBehaviorModuleData *mb = hc ? hc->meleeBehaviorData() : nullptr;
	return mb && mb->m_kind == MeleeBehaviorModuleData::AMOEBA;
}

// RW 0x9902A1: the Amoeba per-frame update (read with Ghidra). See the stop line S-588 for what is inference.
void HordeAIUpdate::meleeTick(Object &targetIn)
{
	Object *horde = getObject();
	Object *targetHorde = hordeOfTarget(targetIn);
	Object &target = targetHorde ? *targetHorde : targetIn;
	HordeContain *hc = hordeContainOf(*horde);
	if (!hc)
	{
		return;
	}
	const MeleeBehaviorModuleData *data = hc->meleeBehaviorData();
	const MeleeBehaviorModuleData::Kind kind = data ? data->m_kind : MeleeBehaviorModuleData::SWARM; // the contain makes Swarm without MeleeBehavior (RW 0x872A16)
	if (kind == MeleeBehaviorModuleData::HOLD_GROUND)
	{
		holdGroundTick(target);
		return;
	}
	if (kind != MeleeBehaviorModuleData::AMOEBA)
	{
		swarmTick(target);
		return;
	}
	const MeleeBehaviorModuleData &amoeba = *data;
	GameLogic &logic = horde->logic();
	const CombatNames::Status &st = CombatNames::statuses();
	recentreOnMembers(); // HordeContainInterface slot 0x98(horde)
	struct Cand
	{
		float key;
		Object *m;
	};
	std::vector<Cand> cands;
	for (Object *m : membersOf())
	{
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		if (!ai || ai->locomotorHost().physicsMotionDisabled())
		{
			continue;
		}
		MemberRecord &rec = recordOf(m->getID());
		if (ai->mover().goalType() == AIGOAL_EXPLICIT_WITH_PATH && ai->mover().path())
		{
			rec.historyCount = 0; // path-busy (RW +0x10 = 0, dirty)
			rec.dirty = true;
			continue;
		}
		if (m->testStatus((unsigned)st.isMeleeAttacking) || m->testStatus((unsigned)st.uncontrollablyScared))
		{
			continue;
		}
		if (!aiSlot1B8(*m)) // AI slot 0x1B8
		{
			ai->aiIdle(CMD_FROM_AI);
			rec.dest = *m->getPosition();
			rec.historyCount = 0;
			rec.hasDest = true;
			rec.dirty = true;
		}
		// RW 0x6CA525 (this = the member): max(0, dist2D - member radius)^2
		float d = SimMath::subf32((float)SimMath::length2d(SimMath::subf32(m->getPosition()->x, target.getPosition()->x), SimMath::subf32(m->getPosition()->y, target.getPosition()->y)),
			CombatQueries::boundingCircleRadius(*m));
		cands.push_back(Cand{ d >= 0.0f ? SimMath::mulf32(d, d) : 0.0f, m });
	}
	// RW 0x99025B: MSVC std::sort; at most 32 entries it is an insertion sort, i.e. stable
	std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.key < b.key; });
	const bool newTarget = m_newTarget;
	m_newTarget = false;
	const bool structure = target.isKindOf((unsigned)CombatNames::kinds().structure);
	(void)structure;
	for (const Cand &c : cands)
	{
		Object *m = c.m;
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		MemberRecord &rec = recordOf(m->getID());
		auto clearIdle = [&]() {
			if (rec.idle)
			{
				rec.idle = false;
				rec.timer = amoeba.m_delayUntilIdle;
				++m_stats.idleCycles;
			}
		};
		if (Object *e = amoebaReach(*m, target))
		{
			clearIdle();
			rec.historyCount = 0;
			rec.dirty = true;
			if (amoebaAttack(*m, *e))
			{
				++m_stats.orders;
			}
			continue;
		}
		if (newTarget)
		{
			rec.dirty = true;
			clearIdle();
		}
		const bool step = amoebaIdleCycle(rec, amoeba, logic); // RW 0x990526 .. 0x9905A1
		bool moved = false;
		if (step && world().mapReady())
		{
			Pathfinder &pf = world().pathfinder();
			ICoord2D cell{ 0, 0 };
			pf.worldToCell(m->getPosition(), &cell);
			const Coord3D mp = *m->getPosition();
			const Coord3D hp = *horde->getPosition();
			float best = 0.0f;
			bool outside = false;
			Coord3D bestPos = mp;
			ICoord2D bestCell = cell;
			bool valid[3][3] = {};
			Coord3D cellPos[3][3];
			for (int ix = -1; ix <= 1; ++ix)
			{
				for (int iy = -1; iy <= 1; ++iy)
				{
					if (ix == 0 && iy == 0)
					{
						continue;
					}
					Coord3D p{ SimMath::addf32(SimMath::sseFromInt32(ix * 10), mp.x), SimMath::addf32(SimMath::sseFromInt32(iy * 10), mp.y), mp.z };
					cellPos[ix + 1][iy + 1] = p;
					// lane PHYS-1: RW 0x9905F3 tests each candidate with RW 0x6F1C90 before the history test RW 0x98F371: the step must be valid (RW 0x6F1B3E) and must not
					// crowd the member into cells holding as many other units' goal reservations as its own, nor next to a blocked reserving unit (S-783)
					static const std::string kStopCrowd =
						"[S-783] amoeba: the step test of RW 0x6F1C90 is ported (Pathfinder::crowdingAllowsStep, footprint extents RW 0x6ED0A0 = N / 2 and N - N / 2 of RW 0x6EAF79, "
						"the reserving unit's blocked-frame counter for RW AI + 0x16C) and the step moves the member's goal reservation (RW 0x68B3BD); only the ground layer";
					logic.noteStop(kStopCrowd);
					if (!pf.crowdingAllowsStep(ai->adapter(), ai->locomotorInfo(), p))
					{
						continue;
					}
					const ICoord2D ci{ cell.x + ix, cell.y + iy };
					bool inHistory = false;
					for (int k = 0; k < rec.historyCount; ++k)
					{
						inHistory = inHistory || (rec.history[k][0] == ci.x && rec.history[k][1] == ci.y);
					}
					if (inHistory)
					{
						continue;
					}
					valid[ix + 1][iy + 1] = true;
					// RW 0x4B3D8D: the signed angle between the member's facing and the direction to the cell
					float turn = 0.0f;
					{
						float vx = SimMath::subf32(p.x, mp.x), vy = SimMath::subf32(p.y, mp.y);
						const float len = SimMath::length2d(vx, vy);
						if (len != 0.0f)
						{
							const float n = SimMath::divf32(1.0f, len);
							vx = SimMath::mulf32(n, vx);
							vy = SimMath::mulf32(n, vy);
							const float fx = SimMath::cosDet(m->getOrientation()), fy = SimMath::sinDet(m->getOrientation());
							float dot = SimMath::addf32(SimMath::mulf32(fx, vx), SimMath::mulf32(fy, vy));
							dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);
							turn = (float)SimMath::acosDet((double)dot);
						}
					}
					float score = NumericState::pc24SubD(10000.0, (double)NumericState::pc24Mul(NumericState::pc24Mul(turn, amoeba.m_facingBonus), 0.31836995f));
					// the enemy the cell is measured against: the target, or the target horde's member nearest to the cell (contain slot 0x48)
					Object *obj = &target;
					if (isHordeObject(target))
					{
						// lane MOVE-2 r4 (Sol r3): RW 0x86FA87 (the contain's slot 0x48, called with no status filter and no range): over the contain list in its order,
						// the member whose 3D distance to the cell point (Coord3D::length, the cell at the member's height) is strictly smallest, starting from
						// RW 0xBDF350; the port measured in the plane over the living members only
						obj = nullptr;
						float bd = kNearestMemberStart;
						if (const auto *list = target.getContain() ? target.getContain()->getContainedItemsList() : nullptr)
						{
							for (Object *e : *list)
							{
								if (!e)
								{
									continue;
								}
								const float d = (float)SimMath::length3d(SimMath::subf32(p.x, e->getPosition()->x), SimMath::subf32(p.y, e->getPosition()->y),
									SimMath::subf32(p.z, e->getPosition()->z));
								if (d < bd)
								{
									obj = e;
									bd = d;
								}
							}
						}
						if (!obj)
						{
							obj = &target;
						}
					}
					if (obj != &target || !isHordeObject(target))
					{
						// the angle test (RW 0x990738 .. 0x9908ED)
						const float ax0 = SimMath::subf32(mp.x, obj->getPosition()->x), ay0 = SimMath::subf32(mp.y, obj->getPosition()->y);
						const float na = SimMath::divf32(1.0f, SimMath::length2d(ax0, ay0));
						const float ax = SimMath::mulf32(na, ax0), ay = SimMath::mulf32(na, ay0);
						const float bx = SimMath::subf32(mp.x, p.x), by = SimMath::subf32(mp.y, p.y);
						const float nbx = SimMath::subf32(0.0f, bx);
						const float n = SimMath::divf32(1.0f, SimMath::length2d(nbx, by));
						const float u0 = SimMath::mulf32(n, nbx), u1 = SimMath::mulf32(n, by);
						const float r = CombatQueries::boundingCircleRadius(*obj);
						const float t28 = SimMath::mulf32(r, u0), t24 = SimMath::mulf32(r, u1);
						float c1y = SimMath::addf32(t28, by), c1x = SimMath::addf32(t24, bx);
						const float n1 = SimMath::divf32(1.0f, SimMath::length2d(c1x, c1y));
						c1x = SimMath::mulf32(n1, c1x);
						c1y = SimMath::mulf32(n1, c1y);
						const float c2x0 = SimMath::subf32(bx, t24), c2y0 = SimMath::subf32(by, t28);
						const float n2 = SimMath::divf32(1.0f, SimMath::length2d(c2y0, c2x0));
						const float dot1 = SimMath::addf32(SimMath::mulf32(c1y, ay), SimMath::mulf32(c1x, ax));
						if (amoeba.m_angleLimitCos > dot1)
						{
							const float dot2 = SimMath::addf32(SimMath::mulf32(SimMath::mulf32(n2, c2y0), ay), SimMath::mulf32(SimMath::mulf32(n2, c2x0), ax));
							if (amoeba.m_angleLimitCos > dot2)
							{
								continue;
							}
						}
					}
					// RW 0x6634BF (this = the member): the edge distance from the cell to obj
					{
						const float e = edgeDistance2(p, *m, *obj);
						score = NumericState::pc24SubD((double)score, SimMath::sqrtd((double)e));
					}
					const float ox = SimMath::subf32(hp.x, p.x), oy = SimMath::subf32(hp.y, p.y);
					const float d2 = SimMath::addf32(SimMath::mulf32(oy, oy), SimMath::mulf32(ox, ox));
					const float outer = obj->isKindOf((unsigned)CombatNames::kinds().structure) ? amoeba.m_outerRangeBuildings : amoeba.m_outerRange;
					if (d2 > SimMath::mulf32(outer, outer))
					{
						outside = true;
						continue;
					}
					if (d2 > SimMath::mulf32(amoeba.m_innerRange, amoeba.m_innerRange))
					{
						score = NumericState::pc24SubD((double)score, SimMath::sqrtd((double)d2));
					}
					if (best < score)
					{
						best = score;
						bestPos = p;
						bestCell = ci;
					}
				}
			}
			if (best < 1.0f && outside)
			{
				for (int ix = -1; ix <= 1; ++ix)
				{
					for (int iy = -1; iy <= 1; ++iy)
					{
						if ((ix == 0 && iy == 0) || !valid[ix + 1][iy + 1])
						{
							continue;
						}
						const Coord3D &p = cellPos[ix + 1][iy + 1];
						const float ox = SimMath::subf32(hp.x, p.x), oy = SimMath::subf32(hp.y, p.y);
						const float v = SimMath::subf32(1000000.0f, SimMath::addf32(SimMath::mulf32(oy, oy), SimMath::mulf32(ox, ox)));
						if (best < v)
						{
							best = v;
							bestPos = p;
							bestCell = ICoord2D{ cell.x + ix, cell.y + iy };
						}
					}
				}
			}
			if (best > 0.0f)
			{
				rec.dirty = false;
				rec.dest = bestPos;
				rec.hasDest = true;
				pushHistory(rec, bestCell.x, bestCell.y);
				const Coord3D &dest = rec.dest; // MOVE-2 r3 review (Sol): retail keeps the candidate's Z, it does not snap the destination to the terrain height
				// lane MOVE-2 r3 (S-1502): RW 0x990A8C .. 0x990AB8 only stores the step in the record (destination, + 0x39 set, history) and reserves its cell: the member
				// walks there through the contain's member pass (slot 7 RW 0x877D89 -> slot 0x34 RW 0x98F819) and the move hub, not by an order given here (the port
				// ordered hordeMemberMoveExplicit at once and ran no member pass in a melee)
				// lane PHYS-1: RW 0x990AB8 calls Object::setPathfindGoalPosition (RW 0x68B3BD -> updateGoal 0x8E24D3) with the step's destination on the member's layer
				// (RW 0x68BBE0): the member's goal reservation moves with the step (before, it stayed on the old cell and RW 0x6F1C90 refused the other members' steps)
				if (world().mapReady())
				{
					world().pathfinder().updateGoal(ai->adapter(), &dest, ai->adapter().getLayer());
				}
				++m_stats.steps;
				moved = true;
			}
		}
		if (!moved)
		{
			if (Object *e = amoebaReach(*m, target))
			{
				clearIdle();
				rec.historyCount = 0;
				rec.dirty = true;
				if (amoebaAttack(*m, *e))
				{
					++m_stats.orders;
				}
			}
			else if (world().mapReady())
			{
				ICoord2D cell{ 0, 0 };
				world().pathfinder().worldToCell(m->getPosition(), &cell);
				pushHistory(rec, cell.x, cell.y); // RW 0x98F3A3 on the current cell (inference: the argument is not read)
			}
		}
	}
}

// the Amoeba idle cycle of one candidate (RW 0x990526 .. 0x9905A1): an active member whose timer ran out goes idle for random(DelayRandomActivateMin, Max) frames, and the
// SAME update runs the idle branch (RW 0x990561, review r2): the new delay counts down at once, a drawn 0 reactivates it at once with DelayUntilIdle; true: the member steps
bool HordeAIUpdate::amoebaIdleCycle(MemberRecord &rec, const MeleeBehaviorModuleData &amoeba, GameLogic &logic)
{
	bool step = false;
	if (!rec.idle)
	{
		if (rec.timer == 0)
		{
			rec.idle = true;
			rec.timer = (unsigned)logic.random().getValue((int)amoeba.m_delayRandomActivateMin, (int)amoeba.m_delayRandomActivateMax, kAmoebaCpp, 0);
		}
		else
		{
			--rec.timer;
		}
		step = !rec.idle;
	}
	// RW 0x990561 (review r2): a member that just went idle runs the idle branch in the same update: the new delay counts down at once, a drawn 0 reactivates it at once
	if (rec.idle)
	{
		if (rec.timer == 0)
		{
			rec.idle = false;
			rec.timer = amoeba.m_delayUntilIdle;
		}
		else
		{
			--rec.timer;
		}
		step = !rec.idle;
	}
	return step;
}

// RW 0x98F3A3: the last four cells, oldest first
void HordeAIUpdate::pushHistory(MemberRecord &rec, int x, int y)
{
	if (rec.historyCount == 4)
	{
		for (int i = 0; i < 3; ++i)
		{
			rec.history[i][0] = rec.history[i + 1][0];
			rec.history[i][1] = rec.history[i + 1][1];
		}
	}
	else
	{
		++rec.historyCount;
	}
	rec.history[rec.historyCount - 1][0] = x;
	rec.history[rec.historyCount - 1][1] = y;
}

// lane ARCHER-1: RW 0x69675C(member, 0) as the HordeAttackNugget's melee branch calls it (RW 0x875350); see HordeAIUpdate.h
bool HordeAIUpdate::touchAttack(Object &member, Object &target)
{
	if (Object *e = amoebaReach(member, target))
	{
		if (amoebaAttack(member, *e))
		{
			++m_stats.orders;
			return true;
		}
	}
	return false;
}

void HordeAIUpdate::crc(StateHasher &h) const
{
	AIUpdateInterface::crc(h);
	h.addU32(m_engagedTarget);
	h.addU32(m_cacheTarget);
	h.addU32(m_cacheExpiry);
	h.addU32((std::uint32_t)m_records.size());
	for (const MemberRecord &r : m_records)
	{
		h.addU32(r.id);
		h.addFloat(r.dest.x);
		h.addFloat(r.dest.y);
		h.addFloat(r.dest.z);
		h.addBool(r.hasDest);
		h.addI32(r.historyCount);
		for (int i = 0; i < 4; ++i)
		{
			h.addI32(r.history[i][0]);
			h.addI32(r.history[i][1]);
		}
		h.addU32(r.timer);
		h.addBool(r.idle);
		h.addBool(r.dirty);
	}
	h.addU64(m_stats.orders);
	h.addU64(m_stats.steps);
	h.addU64(m_stats.idleCycles);
	h.addU64(m_contactRefreshes);
	h.addBool(m_newTarget);
	h.addU64(m_swarmTicks);
}

const char *HordeAIUpdate::squishStopLine()
{
	return "[S-583] horde crush attack: AIAttackMeleeSquishState is the RotWK body (onEnter RW 0x74F21A, update RW 0x74F31F, computePath RW 0x742271, crushPolicy RW 0x69519A, onExit "
	       "RW 0x74B1B7; lane FLANK-1: the destroyed-goal test RW 0x8DBDFF, the flung-goal test RW 0x792973, the re-acquisition of the next victim when the goal dies, the blocked / "
	       "no-path rules of computePath); NOT ported: the AI byte +0x3CC (taken as 0), crushPolicy's line test RW 0x6F5BB0 (taken as passing), the state's pending byte + 0x49, "
	       "and RW 0x701443 is lane TARGET-1's AI::findClosestEnemy (merge queue q23); the re-acquisition is not reached in a game until AIAttackState::update follows RW 0x7510EC "
	       "(retail keeps running the inner machine on a dead victim, the port's ZH-donor state ends the attack first)";
}

const char *HordeAIUpdate::approachStopLine()
{
	return "[S-590] horde melee approach: AIAttackMeleeHordeApproachTargetState is the RotWK body (onEnter RW 0x74EB5C, update RW 0x74AA03, computePath RW 0x74A594, onExit RW 0x74AC0C: "
	       "the fleeing test RW 0x7468D5, the run-down speed and RUNNING_DOWN_FROM_BEHIND, the run-down attack orders RW 0x87631F, the re-path rule, the lead point of a fleeing goal, "
	       "the snap within sqrt(12.5), RW 0x7463E8 as Pathfinder::meleeBackOff, the goal point's helpers RW 0x6F2F8F / 0x746783 / 0x6FB67A since lane ATTACKPATH-1); NOT ported: "
	       "STAND_GROUND's RW 0x6FF7FA, the machine-busy test; INFERENCE: the snap condition (AI slot 0x224) is 'not moving'";
}

// RW 0x86BE9A
void HordeAIUpdate::refreshMeleeReadiness(Object &obj)
{
	// RW 0x86BEA7 .. 0x86BEC1: the id of the object's horde (RW 0x693A1A(0): itself when HORDE, else its HORDE container), else the object's own (lane DECOMP-1:
	// the port took any container)
	const Object *horde = obj.getHordeObject(false);
	const ObjectID container = horde ? horde->getID() : obj.getID();
	if (obj.getID() == m_cacheTarget || container == m_cacheTarget)
	{
		m_cacheExpiry = frame() + 3u * (unsigned)LOGICFRAMES_PER_SECOND;
		++m_contactRefreshes;
	}
}

// RW 0x870180
bool HordeAIUpdate::isInCurrentMelee(Object *t)
{
	if (!t || m_cacheTarget == 0 || frame() >= m_cacheExpiry)
	{
		return false;
	}
	Object *target = getObject()->logic().findObjectByID(m_cacheTarget);
	if (!target)
	{
		return false;
	}
	// RW 0x8701C5 .. 0x8701D4: RW 0x66352C(t, horde) = RW 0x6634BF, the squared nonnegative footprint edge distance (max(0, d - rA - rB))^2, compared with the float at
	// RW 0xBDE8B8, which is 10000.0f: close means an edge gap below 100 units (review r1 asked for 100.0f, i.e. 10 units; the constant in the binary is 10000)
	const bool close = CombatQueries::edgeDistanceSquared2D(*t, *t->getPosition(), *getObject(), *getObject()->getPosition()) < 10000.0f;
	// RW 0x8701E0 .. 0x8701F4 (lane DECOMP-1; BFME2 decomp Rva0046CE9DFinish.cpp, tier A): both sides resolve to their horde (RW 0x693A1A(0), null outside a
	// horde) and the same answer, two nulls included, is true
	return t->getHordeObject(false) == target->getHordeObject(false) || close;
}

// RW 0x86D614
bool HordeAIUpdate::hasMeleeAttackingMember() const
{
	for (Object *m : membersOf())
	{
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		if (ai && ai->isAttacking() && m->testStatus((unsigned)CombatNames::statuses().isMeleeAttacking))
		{
			return true;
		}
	}
	return false;
}

// RW 0x98B7CC: HordeMeleeHoldGround's update: every member that is not IS_MELEE_ATTACKING attacks an enemy it touches (RW 0x69675C -> 0x6964FD, the occupants of the cells
// around the member, RW 0x6ED843). INFERENCE (S-588): the touching enemy is the reach check's enemy of the target (RW 0x6F2956 rules); no repositioning.
void HordeAIUpdate::holdGroundTick(Object &target)
{
	// lane UBER-2B (Uber only): the target's candidates once per tick instead of once per member (a list built per member and frame). Uber rule change: the
	// list is the tick's start (amoebaReachAmong still skips a soldier that died since); a soldier that joined the target during the tick waits for the next
	const bool uber = getObject()->logic().uber();
	std::vector<Object *> tickCands;
	if (uber)
	{
		tickCands = amoebaReachCandidates(target);
	}
	for (Object *m : membersOf())
	{
		if (m->testStatus((unsigned)CombatNames::statuses().isMeleeAttacking))
		{
			continue;
		}
		if (Object *e = uber ? amoebaReachAmong(*m, tickCands) : amoebaReach(*m, target))
		{
			if (amoebaAttack(*m, *e))
			{
				++m_stats.orders;
			}
		}
	}
}

// RW 0x98C6A8 HordeMeleeSwarm (the default without MeleeBehavior; no 2.01 horde uses it): NOT ported (S-588). Its members only attack the enemy in reach.
void HordeAIUpdate::swarmTick(Object &target)
{
	++m_swarmTicks;
	holdGroundTick(target);
}

const char *HordeAIUpdate::amoebaStopLine()
{
	return "[S-588] melee behaviours: HordeMeleeAmoeba's update is the RotWK body (RW 0x9902A1: the horde object recentred on the member nearest the centroid RW 0x86F18F, candidates "
	       "sorted by RW 0x6CA525, the reach check RW 0x6F2956 then the idle cycle, the 8-cell step scored 10000 - FacingBonus * |turn| / pi - edge distance (RW 0x6634BF) with the "
	       "angle test, OuterRange rejection and the full horde distance subtracted beyond InnerRange, the 1e6 - d^2 fallback, the history RW 0x98F3A3) and onNewTarget RW 0x99006A; "
	       "HoldGround's update / onNewTarget (RW 0x98B7CC / 0x98BA04) attack what the members touch; Swarm (RW 0x98C6A8, unused in 2.01) is NOT ported (its members only attack "
	       "in reach); INFERENCE: the reach scan's candidates are the target and its members (RW scans pathfinder cells), the IdleModelConditions are not set, the first list of the "
	       "recentre (contain + 0x54) is not read, the no-step history push uses the current cell, cos / sin / acos are SimMath's deterministic functions";
}

// lane PHYS-1: the per-unit melee machine (RW 0x744B71) defines its 0xE9 with the same AIAttackMeleeSquishState (ctor RW 0x7444DD, vtable RW 0xC28EC8)
std::unique_ptr<AIState> makeMeleeSquishState(AIStateMachine &m)
{
	return std::make_unique<HordeSquishState>(m);
}

bool AttackTargetFleeing(Object &self, Object &victim)
{
	return targetFleeing(self, victim);
}

bool AttackCrushPolicy(Object &self, Object &victim)
{
	return HordeSquishState::crushPolicy(self, victim);
}
