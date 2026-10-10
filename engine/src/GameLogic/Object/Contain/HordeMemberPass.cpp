// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// HordeContain's member pass (lane MOVE-1): the per-member order and the move hub that put a horde's members on their slots, as horde-and-movement.md
// sections 2.2 and 2.3 describe them.
//
// TARGET FACTS (RotWK game.dat, caveat S-001). The member order is RW 0x877A7A (the twin of B1 HordeMemberOrder00245420.cpp), the move hub RW 0x87468B
// (the twin of B1 0x002417E0, read in full: targets/game/reverse/attempts/0x002417e0.cpp and analysis/0x002417e0.md). The AI vtable slots the hub
// calls were identified in the RotWK binary: +0x210 (RW 0x662916) sets the locomotor goal explicit (type 2), +0x214 (0x662934) sets it explicit with
// path (type 4; the old path is deleted unless the goal already was type 4), +0x21C (0x66297C) sets the goal angle (type 3), +0x220 (0x662997) clears
// it (type 0); RW doLocomotor (0x669932) moves a type 2 / 4 goal straight at the goal with a validity check and a fallback path (AIMover::doLocomotor).
// The members therefore walk STRAIGHT to their slots with their own locomotors (they are not pathfound individually: the horde object paths, spec 2.2
// claims otherwise); the horde-owner distances use the owner's path remaining length.
//   Hub constants: snap threshold t = min(maxAcceleration, 0.2 * maxSpeed) (RW 0xBDAD78 = 0.2); turn threshold 10 degrees (0.17453294, RW 0xC5B69C).
//   The slot is "behind" a member when (slot - member) . owner forward < 0 (B1 BfmeFacingDotProduct.cpp: the facing is the OWNER's).
//   The slot cell is clear when nothing but the owner, its members and other hordes occupies the cells around the member (B1 0x23BE60).
//
// WHAT IS INFERENCE (stop S-222, docs/STOPS.md): the orientation handed to the hub is the horde's angle plus the slot record's angle (0 in all data); the
// leash command (0, 2) of the order (B1 0x1F1370, "pull a member off its target") is AI command 0x31, busy (lane AI-2: RW 0x852E2A -> RW 0x66498A), counted only (S-892);
// the member pass re-arms itself while any member walked, turned or waited (the retail bookkeeping H+0xE8 / H+0xD8 is only partly known); the RotWK-only
// crowded-slot search of the near arm (RW 0x874C63-0x874EE9: it walks a probe point from the member towards a crowded slot cell and keeps it in its own locals;
// no write of the destination or of the member was found, so leaving it out changes nothing: inference) and the frozen / melee branches (H+0x1FC, H+0x204) are not
// ported; the slot's kind-1 reservation (RW 0x68B399 -> 0x8E2615, S-164) is not made. Lane SMOOTH-3: the near arm is RotWK's (RW 0x874F09 .. 0x8751C1), not
// BFME1's: turn first, snap only while the horde stands, no walk to a free cell (that walk is the BFME1 hub's block 22).

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cmath>

namespace
{
const float kPi = 3.14159274f;
const float kTwoPi = 6.28318548f;
const float kTurnThreshold = 0.17453294f; // RW 0xC5B69C
const float kSpeedFraction = 0.2f;         // RW 0xBDAD78

// |a| of a binary32 (the sign bit cleared: no rounding, no libm)
float absF32(float a)
{
	return a < 0.0f ? SimMath::subf32(0.0f, a) : a;
}

float normalizeAngle(float a)
{
	if (a > kPi)
	{
		do
		{
			a = SimMath::subf32(a, kTwoPi);
		} while (a > kPi);
	}
	if (-kPi < a)
	{
		return a;
	}
	do
	{
		a = SimMath::addf32(a, kTwoPi);
	} while (-kPi >= a);
	return a;
}

struct Bits
{
	int machine = ObjectTemplateInfoBuilder::kindOfIndex("MACHINE");
	int dozer = ObjectTemplateInfoBuilder::kindOfIndex("DOZER");
	int horde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	int bannerCarrier = ObjectTemplateInfoBuilder::kindOfIndex("HORDE_BANNER_CARRIER");
	int moving = AIUpdateInterface::modelConditionBit("MOVING");
	int transportMoving = AIUpdateInterface::modelConditionBit("TRANSPORT_MOVING");
	int climbing = AIUpdateInterface::modelConditionBit("CLIMBING");               // condition 0x67
	int rappelling = AIUpdateInterface::modelConditionBit("RAPPELLING");           // condition 0x69
	int scalingWall = AIUpdateInterface::modelConditionBit("SCALING_WALL_HORDE");  // condition 0x1BB
	int runningDown = ObjectTemplateInfoBuilder::objectStatusIndex("RUNNING_DOWN_FROM_BEHIND"); // status 0x4B
};
const Bits &bits()
{
	static const Bits b;
	return b;
}
bool hasKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

// RW 0x693A1A(0): the object itself when it is a HORDE, else its container when that is a HORDE, else none
const Object *hordeOfObject(const Object *o)
{
	if (!o)
	{
		return nullptr;
	}
	if (hasKind(*o, bits().horde))
	{
		return o;
	}
	const Object *c = o->getContainedBy();
	return c && hasKind(*c, bits().horde) ? c : nullptr;
}

// RW 0x86BDD3 (member AI, target): the member attacks (AI vslot 0x1BC) and its machine's goal object (machine + 0x20, RW 0x8DBACE) is `target` or shares its horde
bool attacksTarget(AIUpdateInterface &ai, Object &member, Object *target)
{
	if (!target || !ai.isStateActive())
	{
		return false;
	}
	const AIStateMachine *machine = ai.stateMachineOrNull();
	Object *victim = machine ? machine->goalObject() : nullptr;
	if (!victim)
	{
		return false;
	}
	if (victim == target)
	{
		return true;
	}
	const Object *vh = hordeOfObject(victim);
	(void)member;
	return vh && vh == hordeOfObject(target);
}
} // namespace

// lane MOVE-2: slot 0x14, RW 0x87594C (read with Ghidra), called by HordeAIUpdate::aiDoCommand RW 0x89E169 before the horde's command runs
void HordeContain::prepareMembersForCommand(Object *target)
{
	Object *horde = getObject();
	// interface + 0x58 (members on their way into a garrison): slot 0x10 = RW 0x8759FF(0): they rejoin the horde (INFERENCE S-1501: as TransportContain's removal runs
	// RW 0x8759FF(1), through acceptMemberFromGarrison; RW's re-form without the snap of the argument 1)
	if (!m_garrisonEntering.empty())
	{
		const std::vector<ObjectID> entering(m_garrisonEntering.begin(), m_garrisonEntering.end());
		for (ObjectID id : entering)
		{
			if (Object *m = horde->logic().findObjectByID(id))
			{
				acceptMemberFromGarrison(m);
			}
		}
	}
	// interface + 0x184 (the melee): slot 0x138 = RW 0x86C0F9, the fight ends (HordeAIUpdate::commandAccepted calls the AI's endMelee before this)
	if (m_meleeEngaged)
	{
		setMeleeEngaged(false);
	}
	// the contain list (RW 0x865598 copy), in its order: a member with an AI that does not attack `target` and is not busy (AI vslot 0x1C4) gets RW 0x852E2A(0, 2)
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members)
	{
		AIUpdateInterface *ai = m ? m->getAIUpdateInterface() : nullptr;
		if (!ai || attacksTarget(*ai, *m, target) || ai->isBusy())
		{
			continue;
		}
		ai->aiBusy(CMD_FROM_AI);
		++m_stats.handoffBusy;
	}
}

// TransportContain::update, the part BFME added (B1 TransportContainUpdate.cpp): the owner's MOVING (condition 60 in BFME1, 61 in RotWK's registry) is
// mirrored onto the passengers as TRANSPORT_MOVING (88 / 89) whenever the owner's flag changes
void HordeContain::mirrorMovingCondition()
{
	const bool owner = getObject()->testModelCondition(bits().moving);
	if (owner == m_propagatedMoving)
	{
		return;
	}
	m_propagatedMoving = owner;
	for (Object *m : m_members)
	{
		if (m)
		{
			m->setModelConditionState(bits().transportMoving, owner);
		}
	}
}

// B1 BfmeIsPositionAhead (the facing is the OWNER's): the slot lies behind the member along the horde's forward direction
bool HordeContain::isMemberAheadOfSlot(const Object &member, const Coord3D &slotPos) const
{
	const Object *horde = getObject();
	const float *b = horde->getBasis();
	const float dx = SimMath::subf32(slotPos.x, member.getPosition()->x);
	const float dy = SimMath::subf32(slotPos.y, member.getPosition()->y);
	return SimMath::addf32(SimMath::mulf32(*(volatile float *)&dx, b[0]), SimMath::mulf32(*(volatile float *)&dy, b[3])) < 0.0f;
}

// B1 Rva0023BE60NearbyObjectsCheck.cpp: nothing but the owner, its members and other hordes stands in the cells around the member
bool HordeContain::slotCellClear(Object &member) const
{
	Object *horde = getObject();
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	if (!ai || !ai->world().mapReady())
	{
		return true;
	}
	if (hasKind(member, bits().machine))
	{
		return true; // MACHINE members always pass
	}
	AIWorld &w = ai->world();
	Pathfinder &pf = w.pathfinder();
	ICoord2D cell;
	if (!pf.worldToCell(member.getPosition(), &cell))
	{
		// worldToCell clamps: the position is outside the grid, nothing can stand there
	}
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			const PathfindCell *c = pf.cellAt(cell.x + dx, cell.y + dy);
			if (!c)
			{
				continue;
			}
			for (PathfindOccupantKind kind : { OCC_POSITION, OCC_HORDE_POSITION, OCC_GROUND_GOAL })
			{
				for (const PathfindOccupant *o = c->occupants(kind); o; o = o->next)
				{
					if (o->owner == horde->getID() || o->owner == member.getID())
					{
						continue;
					}
					Object *other = w.findObject(o->owner);
					if (!other)
					{
						return false; // B1: an id no object answers to is not clear
					}
					if (other->getContainedBy() == horde || hasKind(*other, bits().horde))
					{
						continue;
					}
					return false;
				}
			}
		}
	}
	return true;
}

// RW 0x871897 (the member order's destination step): unless the horde scales a wall (contain slot 0x238, RW 0x86BD54: wall scaling is not ported, S-084, so
// that branch RW 0x86E214 is never taken) the pathfinder's RW 0x6F0889 (lane MOVE-2: Pathfinder::adjustHordeMemberDestination). Before MOVE-2 the port tested
// only the slot's own cell (trunc, not the footprint), slid toward the horde's centre and, when nothing passed, left the slot as it was: a member whose slot lay
// on a cliff or behind an obstacle walked into it, its straight steps refused, and stood at the end of a fallback path for good (a member of a
// MordorFighterHorde never left its spawn on "map mp fall back 4p", 1809 from its slot at the end of the march). RW also refuses a slot whose cell line to
// the horde's cell crosses a cliff / obstacle / pinched cell or whose footprint is not level, and falls back to the horde's own cell.
bool HordeContain::adjustMemberDestination(Object &member, Coord3D &dest) const
{
	AIUpdateInterface *ai = member.getAIUpdateInterface();
	AIUpdateInterface *hordeAi = getObject()->getAIUpdateInterface();
	if (!ai || !hordeAi || !ai->world().mapReady())
	{
		return true;
	}
	const PathfindLocomotorInfo info = ai->locomotorInfo();
	return ai->world().pathfinder().adjustHordeMemberDestination(ai->adapter(), info, hordeAi->adapter(), &dest);
}

float HordeContain::worstMemberSlotError() const
{
	const Object *horde = getObject();
	HordeContainCore::Placement owner;
	owner.position = *horde->getPosition();
	owner.angle = horde->getOrientation();
	float worst = 0.0f;
	for (const Object *m : m_members)
	{
		if (!m || m_core->slotOf(m->getID()) < 0)
		{
			continue;
		}
		float slotAngle = 0.0f;
		const Coord3D slot = m_core->getSlotWorldPos(m->getID(), owner, &slotAngle);
		const float d = SimMath::length2d(SimMath::subf32(slot.x, m->getPosition()->x), SimMath::subf32(slot.y, m->getPosition()->y));
		worst = d > worst ? d : worst;
	}
	return worst;
}

// the member pass: B1 FormationRefresh0023FA80 slot080 over the contained list in list order (spec 2.2: "nothing sorts members by rank")
bool HordeContain::runMemberPass()
{
	Object *horde = getObject();
	HordeContainCore::Placement owner;
	owner.position = *horde->getPosition();
	owner.angle = horde->getOrientation();
	m_workDone = false;
	++m_stats.passes;
	// lane HORDE-2: the RotWK member pass (RW 0x873FE8, read with Ghidra) calls the member order (vslot 0x90 = RW 0x877A7A) with force = 1 - isMoving(horde)
	// (`'\x01' - (cVar1 != '\0')` at RW 0x8744D0): a PARKED horde never lets a member wait ahead of its slot, so the survivors of a melee re-form; the wait of
	// UseSlowHordeMovement only applies while the horde moves
	AIUpdateInterface *hordeAi = horde->getAIUpdateInterface();
	const bool force = !(hordeAi && hordeAi->isMoving());
	// lane MODULES-3: the scarer of the back-up records (+ 0x2B0, RW 0x874043), looked up once per pass
	Object *scarer = m_scarer != INVALID_ID ? horde->logic().findObjectByID(m_scarer) : nullptr;
	const std::vector<Object *> passMembers(m_members.begin(), m_members.end()); // endQuarrel (below) does not change the list; the copy keeps the walk independent of it
	const HordeAIUpdate *meleeHai = m_meleeEngaged ? dynamic_cast<const HordeAIUpdate *>(hordeAi) : nullptr;
	for (Object *m : passMembers)
	{
		if (m && !m->isDestroyed() && m_core->slotOf(m->getID()) < 0)
		{
			Coord3D bannerPos;
			if (bannerMemberSlot(*m, bannerPos)) // HORDE-2: the banner carrier walks to its BannerCarrierPosition (S-587)
			{
				memberOrder(*m, bannerPos, owner.angle, force);
			}
			continue;
		}
		if (!m || m->isDestroyed() || m_core->slotOf(m->getID()) < 0)
		{
			continue; // a member without a slot stands where creation put it (S-149)
		}
		float slotAngle = 0.0f;
		Coord3D slot = m_core->getSlotWorldPos(m->getID(), owner, &slotAngle);
		slot.z = horde->logic().getGroundHeight(slot.x, slot.y);
		// lane MOVE-2 r3: the pass's slot is the contain's slot 0x1C (RW 0x874080 -> 0x877D89): in a melee the member's melee destination (RW 0x98F819)
		if (meleeHai)
		{
			Coord3D dest;
			if (meleeHai->meleeDestination(m->getID(), dest))
			{
				slot = dest;
			}
		}
		float angle = SimMath::addf32(owner.angle, slotAngle);
		if (!m_data->horde.m_isPorcupineFormation)
		{
			// lane MODULES-3: the emotion branches (RW 0x874155 .. 0x874671, HordeEmotion.cpp)
			const int r = emotionMemberOrder(*m, scarer, slot, angle);
			if (r == 1)
			{
				continue; // a record still waiting: no order (the update reruns the pass while records exist)
			}
			if (r == 2)
			{
				break;
			}
		}
		memberOrder(*m, slot, angle, force);
	}
	return m_workDone;
}

// B1 HordeMemberOrder00245420::apply (RW 0x877A7A); the frozen formation (H+0x1FC) and the engaged-target destination (H+0x204) are melee state (M2)
void HordeContain::memberOrder(Object &member, const Coord3D &slotPos, float orientation, bool force)
{
	Object *horde = getObject();
	AIUpdateInterface *a = member.getAIUpdateInterface();
	AIUpdateInterface *b = horde->getAIUpdateInterface();
	if (!a || !b)
	{
		return;
	}
	++m_stats.orders;
	const bool ownerMoving = b->isMoving();
	// the leash while marching (B1 245420 lines 0xCF..): the nearer of the horde and the slot beyond MeleeAttackLeashDistance gets the command (0, 2); RW 0x877AE3: only
	// while H + 0x2A0 (the melee target) is 0
	if (ownerMoving && !m_meleeEngaged)
	{
		const Coord3D &mp = *member.getPosition();
		// RW 0x877AF0: RW 0x66352C(horde, member) = RW 0x6634BF, the squared 2D distance of the two bounding circles' edges (lane MOVE-2 r2: the port took the
		// squared 3D centre distance)
		const float distHorde = CombatQueries::edgeDistanceSquared2D(*horde, *horde->getPosition(), member, mp);
		const float sx = SimMath::subf32(mp.x, slotPos.x), sy = SimMath::subf32(mp.y, slotPos.y);
		const float distSlot = SimMath::addf32(SimMath::mulf32(sx, sx), SimMath::mulf32(sy, sy));
		const float nearest = distHorde < distSlot ? distHorde : distSlot;
		const float leash = m_data->horde.m_meleeAttackLeashDistance;
		if (nearest > SimMath::mulf32(leash, leash) && !a->isBusy())
		{
			// RW 0x877B4F: RW 0x852E2A(0, 2) = AI command 0x31 from the AI (busy, RW 0x66498A): a member that strayed beyond the leash while its horde marches drops
			// what it does. Lane AI-2 identified it and only counted it (S-892); lane MOVE-2 r2 applies it with its prerequisite, the horde's command hand-off (RW
			// 0x89E169 -> slot 0x14 RW 0x87594C: a horde order first makes the members busy, so they leave their attacks), and the active-member rule below
			a->aiBusy(CMD_FROM_AI);
			++m_stats.leashCommands;
		}
	}
	// lane MOVE-2 r2, RW 0x877B69 .. 0x877C32 (read with Ghidra): a member whose AI state is active (AI vslot 0x1BC: attacking, hunting, or no state) is not driven to
	// its slot unless the horde moves and the member is RUNNING_DOWN_FROM_BEHIND (status 0x4B): its walk ends (RW 0x6627CB), MOVING is cleared, its locomotor goal
	// is cleared (vslot 0x220) and nothing else is ordered; a member still SCALING_WALL_HORDE (condition 0x1BB) also loses CLIMBING / RAPPELLING /
	// SCALING_WALL_HORDE (RW 0x665074 + 0x5E3B79) and is put on the ground (RW 0x70C0AD with the terrain's height; the port sets the position, INFERENCE S-1501).
	// Before, the hub walked an attacking member to its slot while its attack state moved it to its target: the two fought every frame (F4's jerk)
	if (a->isStateActive())
	{
		const bool chase = bits().runningDown >= 0 && member.testStatus((unsigned)bits().runningDown);
		if (!ownerMoving || !chase)
		{
			if (a->isMoving())
			{
				a->mover().endingMove();
			}
			member.setModelConditionState(bits().moving, false);
			a->setLocomotorGoalNone();
			++m_stats.attackHolds;
			if (bits().scalingWall >= 0 && member.testModelCondition(bits().scalingWall))
			{
				member.setModelConditionState(bits().climbing, false);
				member.setModelConditionState(bits().rappelling, false);
				member.setModelConditionState(bits().scalingWall, false);
				Coord3D p = *member.getPosition();
				p.z = horde->logic().getGroundHeight(p.x, p.y);
				member.setPosition(&p);
			}
			return;
		}
	}
	// lane MOVE-2 r3, RW 0x877C37 .. 0x877C72 (H + 0x2A0, the melee target, set): no destination adjustment, no wait: unless the member follows a path whose current
	// node is a waypoint (RW 0x5E2DF4, never in the port: S-161) the hub walks it to the destination it was given (its melee destination)
	if (m_meleeEngaged)
	{
		if (a->mover().goalType() == AIGOAL_EXPLICIT_WITH_PATH && a->mover().path() && a->mover().path()->hasExplicitZ())
		{
			m_workDone = true;
			return;
		}
		moveHub(member, slotPos, orientation);
		return;
	}
	// MACHINE members ride on the horde object: position and facing copied, the goal refreshed when the horde is parked (B1 lines 0x122..)
	if (hasKind(member, bits().machine))
	{
		member.setPosition(horde->getPosition());
		member.setOrientation(horde->getOrientation());
		if (!ownerMoving && b->world().mapReady())
		{
			b->world().pathfinder().updateGoal(a->adapter(), member.getPosition(), a->adapter().getLayer());
		}
		return;
	}
	// a member that follows an explicit goal with a path (the fallback path of a blocked straight walk) keeps going
	if (a->mover().goalType() == AIGOAL_EXPLICIT_WITH_PATH && a->mover().path())
	{
		m_workDone = true;
		return;
	}
	// UseSlowHordeMovement: a member ahead of its slot waits for the formation instead of overshooting (spec 2.2 step 6)
	if (!force && isMemberAheadOfSlot(member, slotPos) && m_data->horde.m_useSlowHordeMovement && (ownerMoving || slotCellClear(member)))
	{
		if (a->mover().isMoving())
		{
			a->mover().endingMove();
		}
		member.setModelConditionState(bits().moving, false);
		a->setLocomotorGoalNone();
		++m_stats.waits; // B1 sets H+0xE8 here; the port does not count a waiting member as work (S-222): it would re-run the pass every frame for a member
		                 // that stays ahead of its slot until the next once-a-second refresh
		return;
	}
	Coord3D dest = slotPos;
	adjustMemberDestination(member, dest);
	moveHub(member, dest, orientation);
}

// B1 0x002417E0 / RW 0x87468B
void HordeContain::moveHub(Object &member, const Coord3D &destIn, float orientation)
{
	Object *horde = getObject();
	AIUpdateInterface *a = member.getAIUpdateInterface();
	AIUpdateInterface *b = horde->getAIUpdateInterface();
	if (!a || !b)
	{
		return;
	}
	Locomotor *l = a->curLocomotor();
	Locomotor *k = b->curLocomotor();
	if (!l || !k)
	{
		return; // block 1: no locomotor on either side
	}
	// lane IDLE-1 r2, RW 0x874724 .. 0x874747 (read with the disassembly): a member whose physics motion is disabled (RW 0x5E3A1B: its PhysicsBehavior + 0x5C, a
	// shockwave's throw or a stun) loses MOVING (+ 0x113 bit 0x20) and gets no order: no goal, no busy command, no work flag. The port gave it its walk: the hub
	// set MOVING every frame while the member update (RW 0x66C8DE, the same test) left it standing: a stunned soldier ran on the spot for the whole stun
	if (a->locomotorHost().physicsMotionDisabled())
	{
		if (member.testModelCondition(bits().moving))
		{
			member.setModelConditionState(bits().moving, false); // RW 0x874731 .. 0x87473F
		}
		return;
	}
	// lane AI-2, RW 0x87471B .. 0x874772 (the H + 0x2A0 == 0 branch): a moving or idle member (isIdle first follows an idle containing horde), unless both its state and the horde's
	// are active (AIUpdate vslot 0x1BC) or it is already busy (vslot 0x1C4 -> machine slot 0x30 RW 0x741724: the state's slot 0x28, true only for AIBusyState), gets
	// AI command 0x31 from the AI (RW 0x852E2A(0, 2) -> aiDoCommand case 0x31 -> vslot 0x140 RW 0x66498A: clear the machine, state 0x2A busy): the hub alone drives it
	// to its slot from then on. Before, a member produced into a horde that had already left kept its stale AI_FOLLOW_EXITPRODUCTION_PATH goal and fought the hub's
	// goal every frame, so it never caught up (the displaced archers of S-420). INFERENCE (S-892): H + 0x2A0 is taken as the melee freeze (meleeEngaged()); its
	// other branch (RW 0x87478C: aiIdle for a non-idle, inactive member) is not ported. PARTIAL (S-892): RW also makes an IDLE member busy ((moving || idle)); the port
	// applies only the moving, non-idle half: the full rule exposes unresolved attack / member lifecycle differences (combat / projectile tests). Idle hordes do
	// acquire targets at horde level; HORDE_MEMBER mood scans are disabled. Reconcile the attack idle / exit behaviour and memberOrder's active-member guard.
	// lane MOVE-2 r2 (RW 0x874749 .. 0x8747A3, read again): the isMoving is the HORDE's (RW 0x664485 with ECX = the horde's AI), the second test the member's
	// isIdle (vslot 0x1B8): a member is made busy unless both it and its horde are active, when its horde moves or it is idle, unless it is busy already. The port
	// tested the member's own isMoving and !isIdle (the half S-892 called partial). The other branch (H + 0x2A0 set, RW 0x874967: a member neither idle nor active
	// gets aiIdle) is the melee freeze, where the port runs no member pass
	if (!m_meleeEngaged && (!a->isStateActive() || !b->isStateActive()) && (b->isMoving() || a->isIdle()) && !a->isBusy())
	{
		a->aiBusy(CMD_FROM_AI);
		++m_stats.busyOrders;
	}
	else if (m_meleeEngaged && !a->isIdle() && !a->isStateActive())
	{
		a->aiIdle(CMD_FROM_AI); // lane MOVE-2 r3: RW 0x874967 .. 0x87498C, the H + 0x2A0 branch: a member neither idle nor active goes idle (RW 0x5E821A)
	}
	AIWorld &world = a->world();
	Pathfinder &pf = world.pathfinder();
	LocomotorHost &host = a->locomotorHost();
	Coord3D dest = destIn;
	const Coord3D oldPosition = *member.getPosition();
	const bool ownerMoving = b->isMoving();
	// (lane HORDE-2) B1's block 3 (a parked horde and a member standing on its own goal cell keep the member where it stands, unless H+0x1AC) is NOT in the RotWK hub:
	// RW 0x87468B goes from the locomotor checks straight to the planar distance and never reads H+0x1AC (an embedded object in RotWK, ctor RW 0x8728E5). So the members of a parked horde
	// walk back to their slots, the survivors of a melee included (the COMBAT-1 finding); a member AHEAD of its slot still waits (the member order RW 0x877A7A).
	// block 4: the planar distance to the destination
	const float dx = SimMath::subf32(dest.x, oldPosition.x), dy = SimMath::subf32(dest.y, oldPosition.y);
	const float distance = SimMath::length2d(dx, dy);
	// block 5 (RW 0x874BF7 .. 0x874C2E): the extra distance handed to the member's locomotor: the horde's remaining path from its point ahead plus the distance,
	// less the horde locomotor's current speed (RW 0x5E36F7 reads locomotor + 0x40, the speed; lane MOVE-2: the port subtracted the preferred height, 0 on the
	// ground). The point ahead is RW 0x766173 with the horde's locomotor: its speed, which RW 0x765F31 raises to 0.1 (the port used 40 for a standing horde: RW
	// takes 40 only without a locomotor). The sum is x87: (remaining + distance) rounded to float, then less the speed
	float adjusted = distance;
	if (b->mover().path())
	{
		LocomotorPath *path = b->mover().path();
		const LocomotorPathPoint ahead = path->computePointAhead(k->speed());
		const float remaining = path->remainingDistanceFrom(ahead);
		adjusted = SimMath::subf32(SimMath::addf32(remaining, distance), k->speed());
	}
	// block 8: the snap threshold
	float t = l->getMaxAcceleration(host);
	const float speedLimit = SimMath::mulf32(l->getMaxSpeedForCondition(host), kSpeedFraction);
	if (speedLimit < t)
	{
		t = speedLimit;
	}
	if (distance < t)
	{
		// ---- the near arm (RotWK RW 0x874F09 .. 0x8751C1; lane SMOOTH-3 replaced the BFME1 form) ----
		// q: the slot at the member's height, on the ground where the member may stand there (RW 0x874F30 .. 0x874F9C)
		Coord3D q = dest;
		if (l->getTemplate().m_appearance != LOCO_TREADS)
		{
			q.z = oldPosition.z;
			if (world.mapReady() && pf.validMovementPosition(&a->adapter(), a->locomotorInfo(), a->adapter().getLayer(), &q))
			{
				q.z = horde->logic().getGroundHeight(q.x, q.y);
			}
		}
		// the turn comes first (RW 0x874FA6 .. 0x8750E1): a member more than 10 degrees off the slot's facing turns where it stands; nothing moves it this frame
		const float angle = absF32(normalizeAngle(SimMath::subf32(orientation, member.getOrientation())));
		// lane MOVE-2 r3: in a melee the melee behaviour decides (RW 0x874FBC .. 0x87505F, its slots 0x1C / 0x20 / 0x24): the Amoeba always turns the member
		const HordeAIUpdate *meleeHai = m_meleeEngaged ? dynamic_cast<const HordeAIUpdate *>(b) : nullptr;
		const bool meleeTurn = meleeHai && meleeHai->meleeAlwaysTurns();
		if (meleeTurn || angle > kTurnThreshold)
		{
			if (hasKind(member, bits().dozer))
			{
				member.setOrientation(orientation); // RW 0x875093 (0x70C31E) for the kind the template flag names (B1 kind 14), then the goal cleared (vslot 0x220)
				a->setLocomotorGoalNone();
			}
			else
			{
				member.setModelConditionState(bits().moving, false); // RW 0x8750B5 .. 0x8750C1: +0x113 bit 0x20 (MOVING) cleared
				a->setLocomotorGoalOrientation(orientation);         // vslot 0x21C; then RW 0x8750DC = 0x662552 (the scheduler wake) only: NOT startingMove (0x6627AF, review r1)
				a->wakeUpNow();
			}
			m_workDone = true; // RW 0x8750A3: +0x120 = 1
			++m_stats.turns;
			return;
		}
		// no turn (RW 0x8750E3 .. 0x8751C1): the walk ends, MOVING is cleared, the goal is cleared
		if (a->mover().isMoving())
		{
			a->mover().endingMove(); // RW 0x6627CB
		}
		member.setModelConditionState(bits().moving, false);
		a->setLocomotorGoalNone(); // vslot 0x220
		++m_stats.snaps;
		// RW 0x875183: only while the horde stands is the member put on its slot (0x70C201), its pending position set (+0x198 / +0x1A6) and its pathfinder goal
		// updated where it now stands (RW 0x68B3BD); the pass then does not count as work. While the horde moves the member is left where it is (work done).
		// RotWK has no "slot cell not clear: walk to the nearest free cell" step here (the BFME1 hub's block 22): with it a member whose slot cell another unit
		// shared was snapped onto the slot and sent 1 .. 5 units away again every few frames (the cavalry's melee jitter, lane SMOOTH-3).
		if (!ownerMoving)
		{
			member.setPosition(&q);
			host.setPendingPosition(q);
			if (world.mapReady())
			{
				pf.updateGoal(a->adapter(), member.getPosition(), a->adapter().getLayer());
			}
			return;
		}
		m_workDone = true;
		return;
	}
	// ---- the far arm: walk ----
	m_workDone = true;
	// lane MOVE-3: the BFME1 hub removed the member's goal reservation here while the horde moves (B1 0x3E3D20); RotWK's hub (RW 0x87468B .. 0x8751C3) calls no
	// removeGoal (RW 0x68B401): the member keeps the goal its horde's updateGoal reserved at its slot around the horde's goal (RW 0x8E24D3 -> 0x86EF13), which is
	// what a second horde's destination adjustment meets. The hub only releases and sets the member's kind-1 slot record (RW 0x68B3F1 at 0x8747D3, RW 0x68B399
	// at 0x874EF5; S-163: that record is not ported)
	a->mover().ignoreObstacle(horde->getProducerID()); // a member made by a factory walks out of its footprint (inference, S-224)
	if (m_meleeEngaged)
	{
		a->hordeMemberMoveExplicit(dest); // RW vslot 0x210 (H + 0x2A0 set: an explicit goal without a path)
	}
	else
	{
		a->hordeMemberMoveTo(dest); // RW vslot 0x214
	}
	a->setPathExtraDistance(adjusted); // RW 0x6631AE
	member.setModelConditionState(bits().moving, true);
	++m_stats.walks;
}
