// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The horde member update of AIUpdateInterface (lane EXIT-1). RotWK has no ZH counterpart: AIUpdateInterface::update (RW 0x66E58F, read with Ghidra) runs, instead
// of the ordinary update (RW 0x6695EF: the state machine, the movement-complete block, the path timer, the turret and doLocomotor RW 0x669932), the member update
// RW 0x66C748 for an object with the status HORDE_MEMBER (0x26) whose AI is busy (machine vslot 0x30 RW 0x741724: the temporary or current state's slot 0x28,
// AIBusyState) or whose locomotor goal is explicit, an angle or explicit with a path (AI + 0x1FC 2, 3, 4), unless the member scales a wall (AI + 0x3CE, set while
// SCALING_WALL holds and not (ATTACKING and not MOVING)) or a DOZER (template + 0x109 bit 0x40) has an angle goal.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), RW 0x66C748 read in full (Ghidra and the disassembly):
//   1. CLIMBING, TURN_LEFT, TURN_RIGHT, TURN_LEFT_HIGH_SPEED, TURN_RIGHT_HIGH_SPEED are cleared (one by one, RW 0x68B53C each);
//   2. a busy member with no container (+ 0x27C) and no horde (RW 0x693A1A(0)) goes idle (RW 0x5E821A(2)): 1 frame;
//   3. the turret (RW 0x6658D3); no goal (type 0): 5 frames (RW 0xD9F608);
//   4. obj + 0x458 bit 0 (INFERENCE S-1751: the effectively dead flag): the BACKING_UP condition goes and the object falls to the ground: z + Gravity * 5 (GlobalData
//      + 0xC4, RW 0xBDAE58), not below the ground (RW 0x70C0AD), 1 frame; already on the ground: 5 frames. Physics motion disabled (RW 0x5E3A1B): BACKING_UP goes,
//      5 frames;
//   5. with an active state (vslot 0x1BC) the state machine runs (vslot 0x10); BACKING_UP is cleared;
//   6. an angle goal (3): the normalised difference (RW 0x644FD0) inside (-2^-23, 2^-23): MOVING, TURN_RIGHT, TURN_LEFT cleared, the goal becomes 0, 1 frame; else
//      MOVING set, with a locomotor TURN_RIGHT when the difference exceeds half the turn rate (RW 0x5E372D, x87), TURN_LEFT when it is below minus half of it, and
//      the object takes the goal angle at once (RW 0x70C31E): 1 frame;
//   7. an explicit goal (2, or 4 without a path): the step is the locomotor's speed (99999 without a locomotor; RW 0x5E36FB the backing-up speed while
//      BACKING_UP holds, else RW 0x5E3F49), 1.5 times when the goal is more than 4 steps away and the container's AI is active and moving; BACKING_UP is set
//      while the container's horde cowers (contain vslot 0x7C -> slot 0x1A4). Farther than a step: one step straight toward the goal (Coord3D::normalize RW
//      0x403175, z kept) and MOVING set; else the goal itself. On a valid cell (RW 0x6EA4E7) a step that RW 0x6F1B3E refuses drops the path and asks RW
//      0x6F74D0 for one that reaches the goal; with none the target is the goal;
//   8. a type 4 goal with a path: the point ahead (RW 0x766173: the locomotor's speed, 40 without one) is walked by the locomotor (RW 0x5E8865 with the remaining
//      length plus the path's extra distance (x87) and 1.5 steps) and MOVING is set; when the point ahead is on the path's last node the path is dropped. 1 frame;
//   9. otherwise the member is put on the target at once: its facing is the bearing to the goal (RW 0x4B3D8D + the angle, x87), for CAVALRY within 20 of the goal
//      blended by 0.05 * the distance (RW 0xBDD760); BACKING_UP faces the container's machine's goal object or turns by pi (RW 0xBDD388); the transform is
//      the rotation of that facing (CRT cos / sin) with the target's translation, z on the ground under the target (a ground-layer member of a ground-layer
//      container: the ground; else not below it); the locomotor's speed takes the distance moved, at most a step (RW 0x5E4C40). 1 frame.
// Before this port a busy member (made busy by the hub, RW 0x87479C) was moved by the ordinary doLocomotor: its locomotor turned and accelerated it, and a
// type 4 goal kept a path that ended short of the goal for good (QA-2 rank 1's companion: produced members stranded behind their formation).
// NOT PORTED (S-1751): the layer of the target (RW 0x680A75 / 0x68BBF4) and the pathfinder's layer update RW 0x6F0741 (2.01 maps have the ground layer only,
// S-161); the in-machine-update byte AI + 0x3E0; the wall-scaling exclusion (AI + 0x3CE, SCALING_WALL: wall scaling is S-084).

#include "GameLogic/Module/AIUpdate.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <string>

namespace
{
struct MemberBits
{
	int climbing = AIUpdateInterface::modelConditionBit("CLIMBING");
	int turnLeft = AIUpdateInterface::modelConditionBit("TURN_LEFT");
	int turnRight = AIUpdateInterface::modelConditionBit("TURN_RIGHT");
	int turnLeftHigh = AIUpdateInterface::modelConditionBit("TURN_LEFT_HIGH_SPEED");
	int turnRightHigh = AIUpdateInterface::modelConditionBit("TURN_RIGHT_HIGH_SPEED");
	int backingUp = AIUpdateInterface::modelConditionBit("BACKING_UP");
	int moving = AIUpdateInterface::modelConditionBit("MOVING");
	int horde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	int cavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");
	int dozer = ObjectTemplateInfoBuilder::kindOfIndex("DOZER");
	int hordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
};
const MemberBits &memberBits()
{
	static const MemberBits b;
	return b;
}

const unsigned kMemberSleep = 5; // RW 0xD9F608 (LOGICFRAMES_PER_SECOND)
const char *const kStopMemberUpdate =
	"S-1751 the horde member update (RW 0x66C748, lane EXIT-1) is ported without the layer of its target (RW 0x680A75 / 0x68BBF4), the pathfinder's layer update (RW "
	"0x6F0741) and the wall-scaling exclusion of RW 0x66E58F (AI + 0x3CE); obj + 0x458 bit 0 is taken as the effectively dead flag (inference) and a waypoint node of a "
	"type 4 path (never made by the port, S-161) takes the ordinary doLocomotor";

void clearCondition(Object &o, int bit)
{
	if (o.testModelCondition(bit))
	{
		o.setModelConditionState(bit, false); // RW 0x68B53C after each bit
	}
}

void setCondition(Object &o, int bit)
{
	if (!o.testModelCondition(bit))
	{
		o.setModelConditionState(bit, true);
	}
}

bool isKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

// RW 0x693A1A(0): the object itself when it is a HORDE, else its container when that is a HORDE
const Object *hordeOf(const Object &o)
{
	if (isKind(o, memberBits().horde))
	{
		return &o;
	}
	const Object *c = o.getContainedBy();
	return c && isKind(*c, memberBits().horde) ? c : nullptr;
}

// RW 0x644FD0: into (-pi, pi] (two SSE loops)
float wrapAngle(float a)
{
	const float pi = 3.14159274101257324f, twoPi = 6.28318548202514648f;
	while (a > pi)
	{
		a = SimMath::subf32(a, twoPi);
	}
	while (-pi >= a)
	{
		a = SimMath::addf32(a, twoPi);
	}
	return a;
}

// RW 0x5E36FB: the locomotor's backing-up speed (template + 0x138 * AI + 0x1F8 * 0.2, x87 at PC24); 0 without an object or an AI (RW 0xC1B594)
float backingUpSpeed(const Locomotor &l, float locomotorSetSpeed)
{
	return SimMath::mulf32(SimMath::mulf32(l.getTemplate().m_backingUpSpeed, locomotorSetSpeed), 0.2f);
}
} // namespace

std::string AIUpdateInterface::hordeMemberUpdateStop()
{
	return kStopMemberUpdate;
}

bool AIUpdateInterface::runsHordeMemberUpdate() const
{
	// RW 0x66E654 .. 0x66E6F4
	const Object *obj = getObject();
	const MemberBits &b = memberBits();
	if (b.hordeMember < 0 || !obj->testStatus((unsigned)b.hordeMember))
	{
		return false;
	}
	const AIGoalType goal = m_mover->goalType();
	const bool candidate = m_machine->isBusyState() || goal == AIGOAL_EXPLICIT || goal == AIGOAL_ANGLE || goal == AIGOAL_EXPLICIT_WITH_PATH;
	if (!candidate)
	{
		return false;
	}
	return !(isKind(*obj, b.dozer) && goal == AIGOAL_ANGLE);
}

unsigned AIUpdateInterface::hordeMemberUpdate()
{
	Object *obj = getObject();
	const MemberBits &b = memberBits();
	GameLogic &logic = obj->logic();
	if (m_memberUpdates++ == 0)
	{
		m_world->noteStop(kStopMemberUpdate); // every run skips the target's layer (S-1751); noted once per object (the world keeps the set)
	}
	// 1. RW 0x66C755 .. 0x66C7C7
	for (int bit : { b.climbing, b.turnLeft, b.turnRight, b.turnLeftHigh, b.turnRightHigh })
	{
		clearCondition(*obj, bit);
	}
	// 2. RW 0x66C7CC .. 0x66C7FA
	if (isBusy() && !obj->getContainedBy() && !hordeOf(*obj))
	{
		aiIdle(CMD_FROM_AI);
		return 1;
	}
	// 3. RW 0x66C806 (the turret, as the ordinary update runs it) and the goal-less member
	if (m_turret && !obj->isEffectivelyDead() && (obj->getDisabledMask() & 0x14u) == 0)
	{
		m_turret->updateTurretAI();
	}
	if (m_mover->goalType() == AIGOAL_NONE)
	{
		return kMemberSleep;
	}
	// 4. RW 0x66C817 .. 0x66C8F4
	if (obj->isEffectivelyDead())
	{
		clearCondition(*obj, b.backingUp);
		const Coord3D p = *obj->getPosition();
		const float ground = logic.getGroundHeight(p.x, p.y);
		if (ground < p.z)
		{
			float z = SimMath::addf32(SimMath::mulf32(logic.settings().gravity, 5.0f), p.z); // GlobalData Gravity * 5 (RW 0xBDAE58), SSE
			if (ground > z)
			{
				z = ground;
			}
			Coord3D q{ p.x, p.y, z };
			obj->setPosition(&q); // RW 0x70C0AD
			return 1;
		}
		return kMemberSleep;
	}
	if (locomotorHost().physicsMotionDisabled())
	{
		clearCondition(*obj, b.backingUp);
		return kMemberSleep;
	}
	// 5. RW 0x66C8F9 .. 0x66C935
	if (isStateActive())
	{
		m_machine->updateStateMachine();
	}
	clearCondition(*obj, b.backingUp);
	Object *container = obj->getContainedBy();
	const AIGoalType goal = m_mover->goalType();
	const Coord3D goalPos = m_mover->goalPosition();
	// 6. the angle goal, RW 0x66C960 .. 0x66CA76
	if (goal == AIGOAL_ANGLE)
	{
		const float goalAngle = m_mover->goalAngle();
		const float d = wrapAngle(SimMath::subf32(obj->getOrientation(), goalAngle));
		if (-1.1920928955078125e-07f < d && d < 1.1920928955078125e-07f) // RW 0xBF7B2C / 0xBF7B30
		{
			clearCondition(*obj, b.moving);
			clearCondition(*obj, b.turnRight);
			clearCondition(*obj, b.turnLeft);
			m_mover->setGoalTypeRaw(AIGOAL_NONE);
			return 1;
		}
		setCondition(*obj, b.moving);
		if (m_curLocomotor)
		{
			const float rate = m_curLocomotor->getMaxTurnRate(locomotorHost()); // RW 0x5E372D, kept in ST0 (fst)
			if (d > SimMath::mulf32(rate, 0.5f))
			{
				setCondition(*obj, b.turnRight);
			}
			else if (SimMath::mulf32(rate, -0.5f) > d)
			{
				setCondition(*obj, b.turnLeft);
			}
		}
		obj->setOrientation(goalAngle); // RW 0x70C31E
		return 1;
	}
	if (goal != AIGOAL_EXPLICIT && goal != AIGOAL_EXPLICIT_WITH_PATH)
	{
		return kMemberSleep;
	}
	// 7. the explicit goal, RW 0x66CA7B ..
	float step = 99999.0f; // RW 0xBDF350
	if (container)
	{
		ContainModuleInterface *c = container->getContain();
		HordeContainInterface *hc = c ? c->getHordeContainInterface() : nullptr;
		if (hc && hc->isCowering())
		{
			setCondition(*obj, b.backingUp);
		}
	}
	if (m_curLocomotor)
	{
		step = obj->testModelCondition(b.backingUp) ? backingUpSpeed(*m_curLocomotor, m_locomotorSetSpeed) : m_curLocomotor->getMaxSpeedForCondition(locomotorHost());
	}
	const Coord3D start = *obj->getPosition();
	Coord3D target = start;
	if (!(goal == AIGOAL_EXPLICIT_WITH_PATH && m_mover->path()))
	{
		Coord3D d{ SimMath::subf32(goalPos.x, start.x), SimMath::subf32(goalPos.y, start.y), 0.0f };
		// RW 0x4054F5: x87 (z * z + y * y) + x * x, CRT sqrt, fstp dword
		const double sum = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(d.z, d.z), NumericState::pc24MulW(d.y, d.y)), NumericState::pc24MulW(d.x, d.x));
		const float dist = NumericState::fstpDword(NumericState::sqrtPC24(sum));
		if (dist > SimMath::mulf32(step, 4.0f) && container) // RW 0x66CB73: fmul 4.0 (RW 0xBD88C0) at PC24
		{
			AIUpdateInterface *cai = container->getAIUpdateInterface();
			if (cai && cai->isStateActive() && cai->isMoving())
			{
				step = SimMath::mulf32(step, 1.5f); // RW 0xBDE8C8
			}
		}
		if (dist > step)
		{
			// RW 0x403175 Coord3D::normalize: len = (float)sqrt of the float sum of squares; unless 0, each component times 1 / len (SSE)
			const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(d.x, d.y, d.z)));
			if (len != 0.0f)
			{
				const float inv = SimMath::divf32(1.0f, len);
				d.x = SimMath::mulf32(d.x, inv);
				d.y = SimMath::mulf32(d.y, inv);
				d.z = SimMath::mulf32(d.z, inv);
			}
			target.x = SimMath::addf32(SimMath::mulf32(d.x, step), target.x);
			target.y = SimMath::addf32(SimMath::mulf32(d.y, step), target.y);
			target.z = SimMath::addf32(target.z, SimMath::mulf32(d.z, step));
			setCondition(*obj, b.moving);
		}
		else
		{
			target = goalPos;
		}
		// RW 0x66CC42 .. 0x66CCE7: from a valid cell, a step RW 0x6F1B3E refuses drops the path and asks RW 0x6F74D0 (RW 0x6F0741 first: S-1751)
		if (m_world->mapReady() && m_world->pathfinder().validMovementPosition(&adapter(), locomotorInfo(), adapter().getLayer(), &start) &&
			!m_mover->stepValid(start, target))
		{
			m_mover->destroyPath();
			Path *p = m_mover->pathReachingGoal(start);
			m_mover->setPath(p);
			if (!p)
			{
				target = goalPos;
			}
		}
	}
	// 8. RW 0x66CCEA .. 0x66CDF9
	if (goal == AIGOAL_EXPLICIT_WITH_PATH && m_mover->path())
	{
		Path *path = m_mover->path();
		const LocomotorPathPoint ahead = path->computePointAhead(m_curLocomotor ? m_curLocomotor->speed() : 40.0f); // RW 0x766173 (RW 0x5E36F7, else 40)
		if (path->hasExplicitZ())
		{
			// RW 0x66CD25: a waypoint node (never made by the port, S-161): doLocomotor with the path goal
			m_mover->setGoalTypeRaw(AIGOAL_ON_PATH);
			m_mover->doLocomotor();
			m_mover->setGoalTypeRaw(AIGOAL_EXPLICIT_WITH_PATH);
		}
		else if (m_curLocomotor)
		{
			// RW 0x66CD74 .. 0x66CDAA: fld remaining (RW 0x765972), fadd the extra distance, fstp; 1.5 steps
			const float rem = NumericState::fstpDword(NumericState::pc24AddW((double)path->remainingDistanceFrom(ahead), (double)m_mover->pathExtraDistance()));
			m_curLocomotor->locomotorMoveTowardsPosition(locomotorHost(), ahead.position, rem, SimMath::mulf32(step, 1.5f));
		}
		setCondition(*obj, b.moving);
		Path *still = m_mover->path();
		if (!(still && still->lastAheadHasNext()))
		{
			m_mover->destroyPath(); // RW 0x66CDDA .. 0x66CDF2
		}
		return 1;
	}
	// 9. RW 0x66CDFE .. 0x66D161
	float facing = SimMath::pc24Add(emotionRelAngle(*obj, goalPos), obj->getOrientation()); // RW 0x4B3D8D; fadd dword + 0x44; fstp
	if (isKind(*obj, b.cavalry))
	{
		const float rel = emotionRelAngle(*obj, goalPos);
		const float dx = SimMath::subf32(start.x, goalPos.x), dy = SimMath::subf32(start.y, goalPos.y);
		// RW 0x405482: x87 sqrt(y * y + x * x); fmul 0.05 (RW 0xBDD760); fst dword
		const double sum2 = NumericState::pc24AddW(NumericState::pc24MulW(dy, dy), NumericState::pc24MulW(dx, dx));
		const float f = NumericState::fstpDword(NumericState::pc24MulW(NumericState::sqrtPC24(sum2), 0.05f));
		if (1.0f > f)
		{
			facing = SimMath::addf32(SimMath::mulf32(f, rel), obj->getOrientation());
		}
	}
	if (obj->testModelCondition(b.backingUp))
	{
		const Object *goalObject = nullptr;
		if (container && container->getAIUpdateInterface() && container->getAIUpdateInterface()->stateMachineOrNull())
		{
			goalObject = container->getAIUpdateInterface()->stateMachineOrNull()->goalObject(); // RW 0x8DBACE
		}
		if (goalObject)
		{
			facing = SimMath::pc24Add(emotionRelAngle(*obj, *goalObject->getPosition()), obj->getOrientation());
		}
		else
		{
			facing = SimMath::addf32(facing, 3.14159274101257324f); // RW 0xBDD388
		}
	}
	const float c = SimMath::cosf32(facing), s = SimMath::sinf32(facing); // MSVCR71 cos / sin (RW 0xA3CF84 / 0xA3CF90; the deterministic pair, S-081)
	if (!(goal == AIGOAL_EXPLICIT_WITH_PATH && m_mover->path()) && m_world->mapReady() &&
		m_world->pathfinder().validMovementPosition(&adapter(), locomotorInfo(), adapter().getLayer(), &target))
	{
		const float ground = logic.getGroundHeight(target.x, target.y);
		if (ground > target.z)
		{
			target.z = ground;
		}
		// RW 0x66D0B5: a ground-layer member (layer 1) of a ground-layer container stands on the ground (the port's layers are the ground: S-161)
		if (container)
		{
			target.z = ground;
		}
	}
	// RW 0x66D100 (RW 0x70BA76): the identity rotated by the facing about z (exact: the products with 0 and 1), the target as translation
	const float basis[9] = { c, -s, 0.0f, s, c, 0.0f, 0.0f, 0.0f, 1.0f };
	obj->setTransform(&target, basis);
	if (m_curLocomotor)
	{
		const float mx = SimMath::subf32(target.x, start.x), my = SimMath::subf32(target.y, start.y), mz = SimMath::subf32(target.z, start.z);
		const double sum = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(mz, mz), NumericState::pc24MulW(my, my)), NumericState::pc24MulW(mx, mx));
		float moved = NumericState::fstpDword(NumericState::sqrtPC24(sum));
		if (moved > step)
		{
			moved = step;
		}
		m_curLocomotor->setSpeedTowards(locomotorHost(), moved); // RW 0x5E4C40
	}
	return 1;
}
