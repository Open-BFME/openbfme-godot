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
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
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
} // namespace

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

// RW 0x6F0889 (B1 0x3E5010): the slot is used when its cell is passable for the member, else it slides toward the horde's centre in 1/16 steps
bool HordeContain::adjustMemberDestination(Object &member, Coord3D &dest) const
{
	AIUpdateInterface *ai = member.getAIUpdateInterface();
	Object *horde = getObject();
	if (!ai || !ai->world().mapReady())
	{
		return true;
	}
	Pathfinder &pf = ai->world().pathfinder();
	const PathfindLocomotorInfo info = ai->locomotorInfo();
	if (info.validSurfaces == 0)
	{
		return true;
	}
	if (pf.validMovementPosition(&ai->adapter(), info, ai->adapter().getLayer(), &dest))
	{
		return true;
	}
	const Coord3D &c = *horde->getPosition();
	for (int i = 1; i < 16; ++i)
	{
		const float t = SimMath::mulf32((float)i, 0.0625f);
		const float s = SimMath::subf32(1.0f, t);
		Coord3D p;
		p.x = SimMath::addf32(SimMath::mulf32(dest.x, s), SimMath::mulf32(c.x, t));
		p.y = SimMath::addf32(SimMath::mulf32(dest.y, s), SimMath::mulf32(c.y, t));
		p.z = dest.z;
		if (pf.validMovementPosition(&ai->adapter(), info, ai->adapter().getLayer(), &p))
		{
			dest = p;
			dest.z = horde->logic().getGroundHeight(p.x, p.y);
			return true;
		}
	}
	return false;
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
	// the leash while marching (B1 245420 lines 0xCF..): the nearer of the horde and the slot beyond MeleeAttackLeashDistance gets the command (0, 2)
	if (ownerMoving)
	{
		const Coord3D &mp = *member.getPosition();
		const Coord3D &hp = *horde->getPosition();
		const float hx = SimMath::subf32(hp.x, mp.x), hy = SimMath::subf32(hp.y, mp.y), hz = SimMath::subf32(hp.z, mp.z);
		const float distHorde = SimMath::addf32(SimMath::addf32(SimMath::mulf32(hx, hx), SimMath::mulf32(hy, hy)), SimMath::mulf32(hz, hz));
		const float sx = SimMath::subf32(mp.x, slotPos.x), sy = SimMath::subf32(mp.y, slotPos.y);
		const float distSlot = SimMath::addf32(SimMath::mulf32(sx, sx), SimMath::mulf32(sy, sy));
		const float nearest = distHorde < distSlot ? distHorde : distSlot;
		const float leash = m_data->horde.m_meleeAttackLeashDistance;
		if (nearest > SimMath::mulf32(leash, leash) && !a->isBusy())
		{
			// RW 0x877B4F: RW 0x852E2A(0, 2) = AI command 0x31 from the AI (busy, RW 0x66498A). Lane AI-2 identified it but does not apply it (S-892): RW tests the
			// horde's isMoving RW 0x664485 (already implemented by AIMover); enabling the leash exposes unresolved attack / member lifecycle differences and stops some
			// firing archer hordes' released members (proj retail tests)
			++m_stats.leashCommands;
		}
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
	// lane AI-2, RW 0x87471B .. 0x874772 (the H + 0x2A0 == 0 branch): a moving or idle member (isIdle first follows an idle containing horde), unless both its state and the horde's
	// are active (AIUpdate vslot 0x1BC) or it is already busy (vslot 0x1C4 -> machine slot 0x30 RW 0x741724: the state's slot 0x28, true only for AIBusyState), gets
	// AI command 0x31 from the AI (RW 0x852E2A(0, 2) -> aiDoCommand case 0x31 -> vslot 0x140 RW 0x66498A: clear the machine, state 0x2A busy): the hub alone drives it
	// to its slot from then on. Before, a member produced into a horde that had already left kept its stale AI_FOLLOW_EXITPRODUCTION_PATH goal and fought the hub's
	// goal every frame, so it never caught up (the displaced archers of S-420). INFERENCE (S-892): H + 0x2A0 is taken as the melee freeze (meleeEngaged()); its
	// other branch (RW 0x87478C: aiIdle for a non-idle, inactive member) is not ported. PARTIAL (S-892): RW also makes an IDLE member busy ((moving || idle)); the port
	// applies only the moving, non-idle half: the full rule exposes unresolved attack / member lifecycle differences (combat / projectile tests). Idle hordes do
	// acquire targets at horde level; HORDE_MEMBER mood scans are disabled. Reconcile the attack idle / exit behaviour and memberOrder's active-member guard.
	if (!m_meleeEngaged && (!a->isStateActive() || !b->isStateActive()) && a->isMoving() && !a->isIdle() && !a->isBusy())
	{
		a->aiBusy(CMD_FROM_AI);
		++m_stats.busyOrders;
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
	// block 5: the extra distance handed to the member's locomotor: the horde's remaining path less the preferred height, plus the distance
	float adjusted = distance;
	if (b->mover().path())
	{
		LocomotorPath *path = b->mover().path();
		const LocomotorPathPoint ahead = path->computePointAhead(k->speed() > 0.0f ? k->speed() : 40.0f);
		const float remaining = path->remainingDistanceFrom(ahead);
		adjusted = SimMath::addf32(SimMath::subf32(remaining, k->getTemplate().m_preferredHeight), distance);
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
		if (angle > kTurnThreshold)
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
	if (ownerMoving && world.mapReady())
	{
		pf.removeGoal(a->adapter()); // B1 0x3E3D20: the member's old goal reservation goes while the horde moves
	}
	a->mover().ignoreObstacle(horde->getProducerID()); // a member made by a factory walks out of its footprint (inference, S-224)
	a->hordeMemberMoveTo(dest);        // RW vslot 0x214
	a->setPathExtraDistance(adjusted); // RW 0x6631AE
	member.setModelConditionState(bits().moving, true);
	++m_stats.walks;
}
