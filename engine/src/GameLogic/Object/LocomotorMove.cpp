// OpenBFME. GPL-3.0.
//
// Locomotor kinematics: the per-frame speed getters, moveForward, rotateTowardsPosition, the Legs / Wheels /
// HORDE / OTHER movers, the dispatcher and handleBehaviorZ. Port of ZH Locomotor.cpp as it appears in RotWK
// game.dat (the retail movers are kinematic, spec 2.13); see GameLogic/Locomotor.h for the list of what is and
// is not ported. Lane HORDE-1.
//
// Every SSE float operation in the retail code is a plain float32 operation here; every x87 operation is a
// NumericState call (operate at PC24, store as float32). The comments give the retail address of each step.
// Acceptance stops reported through Locomotor::unverified():
//   S-081  numeric parity: the movers mix x87 and SSE (moveForward RW 0x5E586F) and call the CRT / x87
//          trigonometry; the facade reproduces the operation order and precision, but sin / cos / atan2 come
//          from the platform libm (SimMath.h) and no retail oracle compares full sequences.
//   S-084  movers and cases not ported (HOVER and SHIP: lane SHIP-1): GIANT_BIRD's rotateTowardsPosition branch (the GIANT_BIRD movers: LocomotorGiantBird.cpp), ScalesWalls look-ahead, ZAxisBehavior other
//          than NO_Z_MOTIVE_FORCE, FLOATING_Z and SEA_LEVEL and their layer / projectile / falling special cases, the HORDE formation
//          path nodes.

#include "GameLogic/Locomotor.h"

#include "Common/StateHash.h"

#include "Common/GameCommon.h"
#include "Common/NumericState.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
const float kPi = 3.14159274f;            // RW 0xBDD388
const float kTwoPi = 6.28318548f;         // RW 0xBDD38C
const float kFourOverPi = 1.27323949f;    // RW 0xBF4ACC
const float kTinyAngle = 0.1f;            // RW 0xBD83D4 (also the HORDE slow factor and the point-ahead minimum)

// Named model condition / object status bits, resolved against the binary's registries (no hard-coded index).
int bitIndex(const char *const *names, const char *name)
{
	for (int i = 0; names[i]; ++i)
	{
		if (std::strcmp(names[i], name) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("name missing from the registry: ") + name);
}

struct Bits
{
	int moving, walking, accelerate, decelerate, backingUp, turnLeft, turnRight, turnLeftHigh, turnRightHigh, emotionTerror, charging, overWater;
	int statusAttacking, statusLeashed, statusContesting, statusHordeMember;
	Bits()
		: moving(bitIndex(TheModelConditionNames, "MOVING"))
		, walking(bitIndex(TheModelConditionNames, "WALKING"))
		, accelerate(bitIndex(TheModelConditionNames, "ACCELERATE"))
		, decelerate(bitIndex(TheModelConditionNames, "DECELERATE"))
		, backingUp(bitIndex(TheModelConditionNames, "BACKING_UP"))
		, turnLeft(bitIndex(TheModelConditionNames, "TURN_LEFT"))
		, turnRight(bitIndex(TheModelConditionNames, "TURN_RIGHT"))
		, turnLeftHigh(bitIndex(TheModelConditionNames, "TURN_LEFT_HIGH_SPEED"))
		, turnRightHigh(bitIndex(TheModelConditionNames, "TURN_RIGHT_HIGH_SPEED"))
		, emotionTerror(bitIndex(TheModelConditionNames, "EMOTION_TERROR"))
		, charging(bitIndex(TheModelConditionNames, "CHARGING"))
		, overWater(bitIndex(TheModelConditionNames, "OVER_WATER"))
		, statusAttacking(bitIndex(TheObjectStatusNames, "IS_ATTACKING"))
		, statusLeashed(bitIndex(TheObjectStatusNames, "LEASHED_RETURNING"))
		, statusContesting(bitIndex(TheObjectStatusNames, "CONTESTING_BUILDING"))
		, statusHordeMember(bitIndex(TheObjectStatusNames, "HORDE_MEMBER"))
	{
	}
};

const Bits &bits()
{
	static const Bits b;
	return b;
}

// fild of an unsigned frame count: `test eax, eax; fild; jge; fadd 2^32` (RW 0x5E40C1). The fild result is exact,
// the unsigned correction is an x87 add at PC24.
double unsignedToWide(std::uint32_t v)
{
	if ((std::int32_t)v >= 0)
	{
		return (double)(std::int32_t)v;
	}
	return (double)NumericState::pc24AddD((double)(std::int32_t)v, 4294967296.0);
}

// RW 0x644FD0: wrap an angle into [-pi, pi] with two SSE loops.
float normalizeAngle(float a)
{
	if (a > kPi)
	{
		do
		{
			a = SimMath::subf32(a, kTwoPi);
		} while (a > kPi);
	}
	const float negPi = -kPi; // RW 0xBDD390
	if (negPi < a)
	{
		return a;
	}
	do
	{
		a = SimMath::addf32(a, kTwoPi);
	} while (negPi >= a);
	return a;
}

float fabsd(float v)
{
	return SimMath::absD(v);
}

// RW 0x403720: the 2D length approximation |big| + 0.25 * |small| (x87 multiply and add at PC24); z is ignored.
float approxLength2D(float x, float y)
{
	const float ax = SimMath::absD(x);
	const float ay = SimMath::absD(y);
	const float smaller = ax <= ay ? ax : ay;
	const float bigger = ax <= ay ? ay : ax;
	const float q = NumericState::pc24Mul(smaller, 0.25f); // RW 0xBD1904
	return NumericState::pc24AddD((double)bigger, (double)q);
}

void setConditionIf(LocomotorHost &host, int bit, bool value)
{
	if (host.testModelCondition(bit) != value)
	{
		host.setModelCondition(bit, value);
	}
}
}

LocomotorMatrix LocomotorMatrix::identity()
{
	LocomotorMatrix r;
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			r.m[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return r;
}

// RW 0x5E3D0x (constructor): caps 99999, temporary cap -1, matrix identity (RW 0x5E3DE3), flags 0.
Locomotor::Locomotor(const LocomotorTemplate *tmpl)
	: m_template(tmpl ? tmpl->getFinalOverride() : nullptr)
	, m_closeEnoughDist(tmpl ? tmpl->getFinalOverride()->m_closeEnoughDist : 0.0f)
	, m_matrix(LocomotorMatrix::identity())
	, m_preferredHeight(tmpl ? tmpl->getFinalOverride()->m_preferredHeight : 0.0f) // lane FLY-1: RW 0x5E3DAA .. 0x5E3DB2 (template + 0x58 -> + 0x48)
	, m_preferredAttackHeight(tmpl ? tmpl->getFinalOverride()->m_preferredAttackHeight : 0.0f) // lane SHIP-1: RW 0x5E3DB5 (template + 0x5C -> + 0x4C)
	, m_preferredHeightDamping(tmpl ? tmpl->getFinalOverride()->m_preferredHeightDamping : 1.0f) // lane SHIP-1: RW 0x5E3DBD (template + 0x60 -> + 0x50)
{
	if (!m_template)
	{
		throw std::logic_error("Locomotor: null template");
	}
}

std::vector<std::string> Locomotor::allStops()
{
	return {
		"S-081: locomotor numeric parity unproven: the movers mix x87 and SSE (moveForward RW 0x5E586F) and use the CRT / x87 sin, cos and atan2; "
		"operation order and PC24 precision are reproduced through NumericState, the trigonometry is NumericState's deterministic double-double "
		"(lane WIN-1: the same bits on every OS), not the retail CRT / x87 results.",
		"S-084: locomotor cases not ported: the GIANT_BIRD branch of rotateTowardsPosition (the GIANT_BIRD movers are GiantBirdAIUpdate's, lane FLY-1), the ScalesWalls look-ahead (RW 0x5E8956-0x5E8FF8), "
		"ZAxisBehavior other than NO_Z_MOTIVE_FORCE, FLOATING_Z, SEA_LEVEL, SURFACE_RELATIVE_HEIGHT and ABSOLUTE_HEIGHT (the last three: lane SHIP-1; the stealth reveal of a climbing object, RW 0x5E7C0A, is not ported) and the layer / projectile / falling special cases of RW 0x5E774A / 0x5E5785, "
		"and the HORDE mover's formation path nodes (types 2, 3, 7, 8; RW 0x5E6E20-0x5E7148).",
		"S-1750: maintainCurrentPosition (RW 0x5E7CC7) is ported without its tail: the held position's z behaviour (RW 0x5E76AC on +0x08) with its "
		"constant-calling result, the transform write (RW 0x70BA76) are not run for non-WINGS appearances; an idle unit's height and sleep follow "
		"the movers' last frame."
	};
}

void Locomotor::noteStop(const char *line)
{
	for (const std::string &s : m_unverified)
	{
		if (s == line)
		{
			return;
		}
	}
	m_unverified.push_back(line);
}

// ---- getters ---------------------------------------------------------------------------------------------------
bool Locomotor::isCharging(const LocomotorHost &host) const
{
	// RW 0x5E3EF7
	const Bits &b = bits();
	if (m_template->m_chargeAvailable)
	{
		if (host.testObjectStatus(b.statusAttacking) || host.testObjectStatus(b.statusLeashed))
		{
			return true;
		}
	}
	if (host.isChargeOrdered() && host.testModelCondition(b.charging))
	{
		return true;
	}
	return false;
}

// RW 0x5E3F49
float Locomotor::getMaxSpeedForCondition(const LocomotorHost &host) const
{
	const LocomotorTemplate &t = *m_template;
	const float speed = host.locomotorSetSpeed(); // ai+0x1F8
	const float kSecondsPerFrame = SECONDS_PER_LOGICFRAME_REAL; // RW 0xD9F61C = 0.2f
	float s;
	const bool penalty = host.damageState() >= host.movementPenaltyDamageState();
	if (penalty && !t.m_chargeIgnoresCondition)
	{
		s = SimMath::mulf32(t.m_maxSpeedDamaged, kSecondsPerFrame); // mulss, mulss (SSE)
		s = SimMath::mulf32(s, speed);
	}
	else
	{
		const float f = isCharging(host) ? SimMath::mulf32(t.m_chargeSpeed, kSecondsPerFrame) : kSecondsPerFrame;
		s = SimMath::mulf32(f, speed);
	}
	if (s > m_maxSpeedCap)
	{
		s = m_maxSpeedCap; // RW 0x5E3FDA
	}
	if (t.m_crewPowered)
	{
		s = NumericState::pc24Mul(host.crewPowerMultiplier(), s); // x87 fmul, RW 0x5E3FFC
	}
	float modifier = 0.0f;
	if (host.speedAttributeModifier(modifier))
	{
		s = SimMath::mulf32(modifier, s); // RW 0x5E401E
	}
	if (t.m_riverModifier != 1.0f)
	{
		const Coord3D pos = host.getPosition();
		if (host.isInRiver(pos.x, pos.y))
		{
			s = SimMath::mulf32(t.m_riverModifier, s); // RW 0x5E4074
		}
	}
	if (m_temporarySpeedCap != -1.0f && !(m_temporarySpeedCap > s))
	{
		s = m_temporarySpeedCap; // RW 0x5E4098
	}
	return s;
}

// RW 0x5E40AD: maxSpeed / AccelerationFrames, capped.
float Locomotor::getMaxAcceleration(const LocomotorHost &host) const
{
	const float maxSpeed = getMaxSpeedForCondition(host);
	const float a = NumericState::pc24DivD((double)maxSpeed, unsignedToWide(m_template->m_acceleration));
	return a > m_accelerationCap ? m_accelerationCap : a;
}

// RW 0x5E40F2: maxSpeed / BrakingFrames, capped.
float Locomotor::getBraking(const LocomotorHost &host) const
{
	const float maxSpeed = getMaxSpeedForCondition(host);
	const float b = NumericState::pc24DivD((double)maxSpeed, unsignedToWide(m_template->m_braking));
	return b > m_brakingCap ? m_brakingCap : b;
}

// RW 0x5E372D: 2*pi / TurnTime frames (the damaged time from the movement-penalty damage state on), capped.
float Locomotor::getMaxTurnRate(const LocomotorHost &host) const
{
	const bool penalty = host.damageState() >= host.movementPenaltyDamageState();
	const std::uint32_t frames = penalty ? m_template->m_turnTimeDamaged : m_template->m_turnTime;
	float rate = NumericState::pc24DivD((double)kTwoPi, unsignedToWide(frames)); // fdivr
	if (rate > m_turnRateCap)
	{
		rate = m_turnRateCap;
	}
	return rate;
}

// RW 0x5E4CA7
bool Locomotor::isFasterThanQuarterSpeed(const LocomotorHost &host) const
{
	const float quarter = SimMath::mulf32(getMaxSpeedForCondition(host), 0.25f);
	return m_speed > quarter;
}

// ---- moveForward (RW 0x5E586F) -------------------------------------------------------------------------------------
// The core kinematics: speed update with braking, then the position along the path (or straight at the goal),
// recorded as the working matrix translation plus the object's pending position; then the acceleration /
// walking model conditions.
void Locomotor::moveForward(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	noteStop(allStops()[0].c_str()); // S-081 applies to every move

	const float maxSpeed = getMaxSpeedForCondition(host); // [ebp-0xc]
	if (desiredSpeed > maxSpeed)
	{
		desiredSpeed = maxSpeed;
	}
	float target = desiredSpeed;      // [ebp+8]
	const float c0 = m_speed;         // [ebp-8]
	const float brake = getBraking(host); // [ebp-4]
	const float minSpeed = t.m_minSpeed;  // RW +0x28 of the template, used as a speed (RW 0x5E58BE)

	// stopping distance (RW 0x5E58C8-0x5E58F4): ((c - m) / b + 1) * ((c - m) * 0.5 + m) * 1.05
	float slow = 0.0f;
	float cm = SimMath::subf32(c0, minSpeed);
	if (!(0.0f >= cm))
	{
		slow = SimMath::divf32(cm, brake);
		slow = SimMath::addf32(slow, 1.0f);
		float mid = SimMath::mulf32(cm, 0.5f);
		mid = SimMath::addf32(mid, minSpeed);
		slow = SimMath::mulf32(slow, mid);
		slow = SimMath::mulf32(slow, 1.05f); // RW 0xBF3DCC
	}
	if (onPathDistToGoal > SimMath::mulf32(slow, 3.0f))
	{
		m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BRAKING; // RW 0x5E5911
	}
	const float cMinusB = SimMath::subf32(m_speed, brake);
	if (cMinusB > desiredSpeed)
	{
		target = cMinusB;
		if (brake > cMinusB)
		{
			target = brake;
		}
	}
	if (slow > onPathDistToGoal && !(m_flags & LOCOMOTOR_FLAG_NO_BRAKE))
	{
		m_flags |= LOCOMOTOR_FLAG_BRAKING; // RW 0x5E594A
		const float r = NumericState::pc24Sub(m_speed, getBraking(host)); // fsubr [edi+0x40]
		target = r;
		if (brake > r)
		{
			target = brake;
		}
	}

	if (target > m_speed && !(m_flags & LOCOMOTOR_FLAG_BRAKING))
	{
		m_speed = NumericState::pc24Add(getMaxAcceleration(host), m_speed); // fadd [edi+0x40], RW 0x5E59A9
	}
	{
		float c = m_speed;
		if (c > target)
		{
			c = SimMath::subf32(c, brake);
			m_speed = c;
			if (target > c)
			{
				m_speed = target;
			}
		}
	}

	// ---- position ----
	const float step0 = m_speed; // [ebp+0x14]
	Coord3D newPos = goal;       // [ebp-0x1c..]
	LocomotorPath *path = host.getPath();
	if (path)
	{
		const LocomotorPathPoint p1 = path->computePointAhead(m_speed); // RW 0x766173(L) -> 0x765F31
		newPos = p1.position;
		path->updateClosestSegment(newPos);                              // RW 0x765598(&pos, 1)
		const LocomotorPathPoint p2 = path->computePointAhead(m_speed);
		host.setPendingPosition(p2.position);                            // RW 0x5E5A87-0x5E5A99
	}
	else
	{
		const Coord3D cur = host.getPosition();
		newPos = cur; // RW 0x5E5AAB: the position copy
		float vx = SimMath::subf32(goal.x, cur.x);
		float vy = SimMath::subf32(goal.y, cur.y);
		const float vz = 0.0f;
		const float len = (float)SimMath::length3d(vx, vy, vz); // RW 0x403111, stored with fstp dword
		const float frames = (float)LOGICFRAMES_PER_SECOND;     // cvtsi2ss [0xD9F608]
		const float minStep = SimMath::divf32(10.0f, frames);                   // RW 0xBD83D8 = 10.0
		float step = step0;
		if (minStep > step)
		{
			step = minStep;
		}
		if (step > len)
		{
			step = len;
		}
		if (len > 0.001f) // RW 0xBD88A0
		{
			// RW 0x403175: normalise (length again, fucompi against 0, then multiply by 1/length)
			const float len2 = (float)SimMath::length3d(vx, vy, vz);
			if (len2 != 0.0f)
			{
				const float inv = SimMath::divf32(1.0f, len2);
				vx = SimMath::mulf32(vx, inv);
				vy = SimMath::mulf32(vy, inv);
			}
			const float ax = SimMath::mulf32(vx, step);
			const float ay = SimMath::mulf32(vy, step);
			newPos.x = SimMath::addf32(cur.x, ax);
			newPos.y = SimMath::addf32(cur.y, ay);
		}
	}
	// RW 0x5E5B42: record the object's pending position and the new working translation
	const Coord3D curNow = host.getPosition();
	const Coord3D effective = host.hasPendingPosition() ? host.getPendingPosition() : curNow;
	host.setPendingPosition(Coord3D{ effective.x, effective.y, curNow.z });
	m_matrix.m[0][3] = newPos.x;
	m_matrix.m[1][3] = newPos.y;
	m_matrix.m[2][3] = curNow.z;

	// ---- acceleration / walking model conditions (RW 0x5E5BA7-0x5E5D77) ----
	const float delta = NumericState::pc24Sub(m_speed, c0);
	const float thresh = SimMath::mulf32(t.m_accDecTrigger, maxSpeed); // [ebp+0x14]
	const float accelK = NumericState::pc24Mul(getMaxAcceleration(host), 0.4f); // fmul [0xBE0DE4]
	if (delta > accelK && thresh > c0)
	{
		if (host.testModelCondition(b.decelerate))
		{
			host.setModelCondition(b.decelerate, false);
		}
		if (t.m_walkDistance > onPathDistToGoal)
		{
			if (host.testModelCondition(b.accelerate))
			{
				host.setModelCondition(b.accelerate, false);
			}
			if (!host.testModelCondition(b.walking))
			{
				host.setModelCondition(b.walking, true);
			}
		}
		else
		{
			if (host.testModelCondition(b.walking))
			{
				host.setModelCondition(b.walking, false);
			}
			if (!host.testModelCondition(b.accelerate))
			{
				host.setModelCondition(b.accelerate, true);
			}
		}
		return;
	}
	if ((m_flags & LOCOMOTOR_FLAG_BRAKING) && thresh > m_speed)
	{
		if (host.testModelCondition(b.accelerate))
		{
			host.setModelCondition(b.accelerate, false);
		}
		if (m_byte98)
		{
			if (host.testModelCondition(b.walking))
			{
				host.setModelCondition(b.walking, false);
			}
			if (!host.testModelCondition(b.decelerate))
			{
				host.setModelCondition(b.decelerate, true);
			}
		}
		else
		{
			if (host.testModelCondition(b.decelerate))
			{
				host.setModelCondition(b.decelerate, false);
			}
			if (!host.testModelCondition(b.walking))
			{
				host.setModelCondition(b.walking, true);
			}
		}
		return;
	}
	if (host.testModelCondition(b.accelerate))
	{
		host.setModelCondition(b.accelerate, false);
	}
	if (host.testModelCondition(b.decelerate))
	{
		host.setModelCondition(b.decelerate, false);
	}
	if (c0 > thresh)
	{
		if (onPathDistToGoal > t.m_walkDistance)
		{
			if (host.testModelCondition(b.walking))
			{
				host.setModelCondition(b.walking, false);
			}
		}
		if (!host.testModelCondition(b.walking))
		{
			m_byte98 = true;
		}
	}
}

// ---- rotateTowardsPosition (RW 0x5E4F44, ground branch from RW 0x5E5418) -------------------------------------------
void Locomotor::rotateTowardsPosition(LocomotorHost &host, const Coord3D &goal, float maxTurnRate, float *outRelAngle)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	if (t.m_appearance == LOCO_GIANT_BIRD)
	{
		noteStop(allStops()[1].c_str());
		throw std::logic_error("S-084: rotateTowardsPosition for GIANT_BIRD locomotors is not ported");
	}

	const float objAngle = host.getAngle();                 // [ebp+0x54]
	const float pivotOffset = t.m_turnPivotOffset;          // [ebp+0x64]
	float turnThreshold = t.m_turnThreshold;                // [ebp+0x70]
	if (m_nearPathEnd)
	{
		turnThreshold = SimMath::mulf32(turnThreshold, 0.0625f);            // RW 0xBDD39C
	}
	const bool turnLimited = host.isTurnLimited();          // bl
	m_turnDirection = 0;                                    // and [edi+0xa8], 0

	const float pivot = SimMath::mulf32(host.getBoundingRadius(), pivotOffset); // obj+0xB8 * offset
	const Coord3D pos = host.getPosition();
	const Coord2D dir = host.getUnitDirectionVector2D();   // RW 0x70B9E0
	float px = SimMath::mulf32(dir.x, pivot);
	px = SimMath::addf32(px, pos.x);
	float py = SimMath::mulf32(dir.y, pivot);
	py = SimMath::addf32(py, pos.y);
	const float dx = SimMath::subf32(goal.x, px);
	const float dy = SimMath::subf32(goal.y, py);
	if (fabsd(dx) < kTinyAngle && fabsd(dy) < kTinyAngle)
	{
		return; // RW 0x5E54AB: both within 0.1 of the pivot: nothing to turn
	}
	// atan2 stays a double in ST0; the subtraction runs at PC24, then RW 0x644FD0 wraps it
	const double ang = SimMath::atan2d(dy, dx);
	float rel = normalizeAngle(NumericState::pc24SubD(ang, (double)objAngle));
	if (outRelAngle)
	{
		*outRelAngle = rel;
	}
	if (rel > 0.0f)
	{
		if (rel > turnThreshold)
		{
			m_turnDirection = 1;
		}
		if (rel > maxTurnRate)
		{
			rel = maxTurnRate;
		}
	}
	else
	{
		const float negThreshold = SimMath::subf32(0.0f, turnThreshold);
		if (negThreshold > rel)
		{
			m_turnDirection = -1;
		}
		const float negMax = SimMath::subf32(0.0f, maxTurnRate);
		if (negMax > rel)
		{
			rel = negMax;
		}
	}
	if (turnLimited)
	{
		rel = SimMath::mulf32(rel, 0.5f);
	}
	const float c = SimMath::cosf32(rel);
	const float s = SimMath::sinf32(rel);
	// RW 0x5E5558-0x5E55FB: rotate the first two rows of the working matrix about Z
	for (int col = 0; col < 3; ++col)
	{
		const float a = m_matrix.m[0][col];
		const float bb = m_matrix.m[1][col];
		const float ac = SimMath::mulf32(a, c);
		const float bs = SimMath::mulf32(bb, s);
		const float as = SimMath::mulf32(a, s);
		const float bc = SimMath::mulf32(bb, c);
		m_matrix.m[0][col] = SimMath::subf32(ac, bs);
		m_matrix.m[1][col] = SimMath::addf32(as, bc);
	}

	// ---- turn animation model conditions (RW 0x5E5603-0x5E5774) ----
	const float curSpeed = m_speed;
	const float half = SimMath::mulf32(getMaxSpeedForCondition(host), 0.5f);
	bool wantTurnAnim = true;
	if (curSpeed > half && !t.m_enableHighSpeedTurnModelconditions)
	{
		wantTurnAnim = false;
	}
	if (turnLimited)
	{
		wantTurnAnim = false;
	}
	if (!m_nearPathEnd)
	{
		setConditionIf(host, b.turnLeft, false);
		setConditionIf(host, b.turnRight, false);
		setConditionIf(host, b.turnLeftHigh, false);
		setConditionIf(host, b.turnRightHigh, false);
	}
	if (!wantTurnAnim)
	{
		return;
	}
	if (m_turnDirection == -1)
	{
		if (curSpeed > half)
		{
			host.clearAndSetModelConditions({ b.turnLeft, b.turnLeftHigh }, { b.turnRight, b.turnRightHigh });
		}
		else
		{
			const bool left = host.testModelCondition(b.turnLeft);
			const bool right = host.testModelCondition(b.turnRight);
			if (left || !right)
			{
				host.clearAndSetModelConditions({ b.turnLeft }, { b.turnRight });
			}
		}
	}
	else if (m_turnDirection == 1)
	{
		if (curSpeed > half)
		{
			host.clearAndSetModelConditions({ b.turnRight, b.turnRightHigh }, { b.turnLeft, b.turnLeftHigh });
		}
		else
		{
			const bool left = host.testModelCondition(b.turnLeft);
			const bool right = host.testModelCondition(b.turnRight);
			if (right || !left)
			{
				host.clearAndSetModelConditions({ b.turnRight }, { b.turnLeft });
			}
		}
	}
}

// RW 0x5E60C7: the object's transform becomes the working matrix, then the turn runs at this locomotor's rate.
void Locomotor::rotateTowardsWrapper(LocomotorHost &host, const Coord3D &goal, float *outRelAngle)
{
	m_matrix = host.getTransform();
	rotateTowardsPosition(host, goal, getMaxTurnRate(host), outRelAngle);
}

// ---- Legs (TWO_LEGS, FOUR_LEGS_HUGE, HUGE_TWO_LEGS; RW 0x5E63F3) ----------------------------------------------------
void Locomotor::moveTowardsPositionLegs(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	const Coord3D pos = host.getPosition();
	if (t.m_downhillOnly && goal.z > pos.z)
	{
		return; // RW 0x5E6415
	}
	float maxSpeed = getMaxSpeedForCondition(host); // [ebp-0x14]
	if (desiredSpeed > maxSpeed)
	{
		desiredSpeed = maxSpeed;
	}
	const float c = m_speed;                 // [ebp-0x24]
	const float objAngle = host.getAngle();  // [ebp-0x20]
	float angleToGoal = (float)SimMath::atan2d(SimMath::subf32(goal.y, pos.y), SimMath::subf32(goal.x, pos.x)); // [ebp-0x1c] (fstp dword)
	const double dist = SimMath::length3d(SimMath::subf32(pos.x, goal.x), SimMath::subf32(pos.y, goal.y), 0.0f);
	if ((double)kTinyAngle > dist)
	{
		angleToGoal = objAngle; // RW 0x5E64C2: closer than 0.1: keep the heading
	}
	float rel = normalizeAngle(SimMath::subf32(angleToGoal, objAngle)); // [ebp-0x18]
	Coord3D goalCopy = goal;                             // [ebp-0x34]

	bool backing = false; // [ebp-0xd]
	if (t.m_canMoveBackwards != 0 && host.testModelCondition(b.emotionTerror))
	{
		backing = true; // RW 0x5E6512 (model condition 0x41)
	}
	if (host.testObjectStatus(b.statusHordeMember) && host.containerAllowsBackingUp())
	{
		backing = true; // RW 0x5E6523-0x5E655A
	}

	if (c == 0.0f) // ucomiss against 0.0; the block runs only for an exact zero (RW 0x5E655E-0x5E65B0)
	{
		if (backing)
		{
			m_flags &= ~0x100u;
			m_flags |= LOCOMOTOR_FLAG_BACKING_UP;
			maxSpeed = SimMath::mulf32(maxSpeed, 0.5f);
		}
		else
		{
			if (host.testModelCondition(b.backingUp))
			{
				host.setModelCondition(b.backingUp, false);
			}
			m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP;
		}
		m_byte98 = false;
	}

	if (!backing)
	{
		if (m_flags & LOCOMOTOR_FLAG_BACKING_UP)
		{
			if (host.testModelCondition(b.backingUp))
			{
				host.setModelCondition(b.backingUp, false);
			}
			m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP;
		}
	}
	else
	{
		m_flags |= LOCOMOTOR_FLAG_BACKING_UP;
		if (!host.testModelCondition(b.backingUp))
		{
			host.setModelCondition(b.backingUp, true);
		}
		// aim the other way: heading + pi, goal mirrored through the object (RW 0x5E6609-0x5E667F)
		const float flipped = normalizeAngle(SimMath::subf32(angleToGoal, kPi));
		rel = normalizeAngle(NumericState::pc24Sub(flipped, objAngle));
		goalCopy.x = SimMath::subf32(SimMath::mulf32(pos.x, 2.0f), goal.x);
		goalCopy.y = SimMath::subf32(SimMath::mulf32(pos.y, 2.0f), goal.y);
		goalCopy.z = SimMath::subf32(SimMath::mulf32(pos.z, 2.0f), goal.z);
	}
	rotateTowardsWrapper(host, goalCopy, nullptr);

	// turn slow-down (RW 0x5E668F-0x5E6774)
	if (t.m_circlingRadius > 0.0f)
	{
		const float accel = getMaxAcceleration(host);
		if (!(accel < c))
		{
			rel = SimMath::mulf32(rel, 2.0f);
		}
		else
		{
			const float half = SimMath::mulf32(maxSpeed, 0.5f);
			if (c > half)
			{
				rel = 0.0f;
			}
		}
	}
	const bool hugeLegs = t.m_appearance == LOCO_LEGS_TWO_HUGE || t.m_appearance == LOCO_LEGS_FOUR_HUGE;
	float f = NumericState::pc24MulD((double)fabsd(rel), (double)kFourOverPi); // |rel| * 4/pi, x87
	if (f > 0.9f)
	{
		f = 0.9f; // RW 0xBDE198
	}
	float target = desiredSpeed;
	if (hugeLegs)
	{
		const float oneMinus = SimMath::subf32(1.0f, f);
		target = SimMath::mulf32(oneMinus, desiredSpeed);
	}
	if (desiredSpeed > 0.0f)
	{
		moveForward(host, goal, onPathDistToGoal, target);
	}
	else
	{
		m_speed = 0.0f; // RW 0x5E6774
	}
}

// ---- Wheels (FOUR_WHEELS; TREADS share it; RW 0x5E83FD) ---------------------------------------------------------------
void Locomotor::moveTowardsPositionWheels(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	const float maxSpeed = getMaxSpeedForCondition(host); // [ebp-8]
	if (desiredSpeed > maxSpeed)
	{
		desiredSpeed = maxSpeed;
	}
	const float objAngle = host.getAngle();
	const Coord3D pos = host.getPosition();
	float angleToGoal = (float)SimMath::atan2d(SimMath::subf32(goal.y, pos.y), SimMath::subf32(goal.x, pos.x)); // [ebp-4]
	float rel = normalizeAngle(SimMath::subf32(angleToGoal, objAngle));                        // [ebp+8]

	bool backing = t.m_canMoveBackwards != 0; // setne bl
	if (backing)
	{
		// distance from the object to the locomotor's stored point (RW 0x403720); too far in both senses disables backing up
		const float dist = approxLength2D(SimMath::subf32(m_preferredPoint.x, pos.x), SimMath::subf32(m_preferredPoint.y, pos.y));
		if (dist > t.m_backingUpDistanceMin && onPathDistToGoal > t.m_backingUpDistanceMax)
		{
			backing = false;
		}
	}
	const float c0 = m_speed; // [ebp-0xc]
	if (backing)
	{
		const double limit = (double)NumericState::pc24MulD((double)t.m_backingUpAngle, (double)kPi);
		if ((double)fabsd(rel) > limit)
		{
			if (!host.testModelCondition(b.backingUp))
			{
				host.setModelCondition(b.backingUp, true);
			}
			m_flags |= LOCOMOTOR_FLAG_BACKING_UP;
		}
		else
		{
			if (host.testModelCondition(b.backingUp))
			{
				host.setModelCondition(b.backingUp, false);
			}
			m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP;
		}
	}
	else
	{
		if (host.testModelCondition(b.backingUp))
		{
			host.setModelCondition(b.backingUp, false);
		}
		m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP;
	}
	if (m_flags & LOCOMOTOR_FLAG_BACKING_UP)
	{
		angleToGoal = normalizeAngle(SimMath::subf32(angleToGoal, kPi)); // RW 0x5E8555
	}
	// rotate towards the point 1000 units out along the heading (RW 0x5E8577-0x5E85C6): fcos / fsin stay wide in
	// ST0, the multiply by 1000.0f and the add run at PC24
	Coord3D farPoint{ pos.x, pos.y, pos.z };
	farPoint.x = NumericState::pc24AddD((double)NumericState::pc24MulD(SimMath::cosd(angleToGoal), 1000.0), (double)pos.x);
	farPoint.y = NumericState::pc24AddD((double)NumericState::pc24MulD(SimMath::sind(angleToGoal), 1000.0), (double)pos.y);
	rotateTowardsWrapper(host, farPoint, nullptr);

	const float accel = getMaxAcceleration(host);
	float p = rel;
	if (!(accel < c0))
	{
		p = SimMath::mulf32(rel, 2.0f);
	}
	else
	{
		const float quarter = SimMath::mulf32(maxSpeed, 0.25f);
		if (c0 > quarter)
		{
			p = 0.0f;
		}
	}
	if (maxSpeed > 0.0f)
	{
		float f = NumericState::pc24MulD((double)fabsd(p), (double)kFourOverPi); // |p| * 4/pi, x87
		if (f > 1.0f)
		{
			f = 1.0f;
		}
		float target;
		if (t.m_backingUpStopWhenTurning && (m_flags & LOCOMOTOR_FLAG_BACKING_UP))
		{
			target = (host.testModelCondition(b.turnLeft) || host.testModelCondition(b.turnRight)) ? 0.0f : desiredSpeed;
		}
		else if (m_flags & LOCOMOTOR_FLAG_BACKING_UP)
		{
			target = desiredSpeed;
		}
		else
		{
			const float oneMinus = SimMath::subf32(1.0f, f);
			target = SimMath::mulf32(oneMinus, desiredSpeed);
		}
		moveForward(host, goal, onPathDistToGoal, target);
	}
	else
	{
		if (fabsd(p) < 0.01f)
		{
			host.notifyWheelsStopped(); // RW 0x5E86CE
		}
	}
}

// ---- RW 0x5E3A81 ---------------------------------------------------------------------------------------------------------
// The retail code builds the rotation with multiplications by 0.0f and subtractions of zeros; the same float
// operations are used here so the zero signs are the retail ones.
void Locomotor::setMatrixAngle(float angle)
{
	const float tx = m_matrix.m[0][3];
	const float ty = m_matrix.m[1][3];
	const float tz = m_matrix.m[2][3];
	const float c = SimMath::cosf32(angle);
	const float s = SimMath::sinf32(angle);
	const float zero = 0.0f;
	const float cz = SimMath::mulf32(c, zero);
	const float negS = SimMath::subf32(zero, s);
	float m21 = SimMath::mulf32(s, zero);
	m21 = SimMath::subf32(m21, cz);                 // xmm1 = s*0 - c*0 -> RW +0x8C
	const float m21Copy = m21;      // xmm6
	float t = SimMath::mulf32(m21, zero);           // xmm1 = xmm1 * 0
	const float m00 = SimMath::subf32(c, t);        // RW +0x68
	const float m10 = SimMath::subf32(t, negS);     // RW +0x78
	const float negSz = SimMath::mulf32(negS, zero); // xmm3
	const float cz2 = SimMath::mulf32(c, zero);      // xmm7
	const float m20 = SimMath::subf32(negSz, cz2);   // RW +0x88
	m_matrix.m[0][0] = m00;
	m_matrix.m[0][1] = negS;
	m_matrix.m[0][2] = zero;
	m_matrix.m[0][3] = tx;
	m_matrix.m[1][0] = m10;
	m_matrix.m[1][1] = c;
	m_matrix.m[1][2] = zero;
	m_matrix.m[1][3] = ty;
	m_matrix.m[2][0] = m20;
	m_matrix.m[2][1] = m21Copy;
	m_matrix.m[2][2] = 1.0f;
	m_matrix.m[2][3] = tz;
}

// ---- HORDE (RW 0x5E6D4F), plain path only ----------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionHorde(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	const bool contain = host.hasHordeContain();
	float pathRemaining = onPathDistToGoal; // [ebp-0x28]
	if (LocomotorPath *path = host.getPath())
	{
		const int node = path->currentSpecialNodeType();
		if (node == 2 || node == 3 || node == 7 || node == 8)
		{
			noteStop(allStops()[1].c_str());
			throw std::logic_error("S-084: HORDE mover formation path nodes (types 2, 3, 7, 8) are not ported");
		}
		const LocomotorPathPoint ahead = path->computePointAhead(m_speed); // RW 0x766173
		pathRemaining = path->remainingDistanceFrom(ahead);                // RW 0x765972
	}

	const float maxSpeed = getMaxSpeedForCondition(host);   // [ebp-0x20]
	const float maxTurnRate = getMaxTurnRate(host);         // [ebp-0x30]
	if (desiredSpeed > maxSpeed)
	{
		desiredSpeed = maxSpeed;
	}
	if (t.m_scalesWalls && contain)
	{
		desiredSpeed = host.hordeWallScalingSpeed(desiredSpeed); // RW 0x5E7186-0x5E71A5: the contain's vfunc +0x240 (RW 0x86BC78)
	}
	const float objAngle = host.getAngle();                  // [ebp-0x24]
	const Coord3D pos = host.getPosition();
	float angleToGoal = (float)SimMath::atan2d(SimMath::subf32(goal.y, pos.y), SimMath::subf32(goal.x, pos.x)); // [ebp-0x18]
	float rel = normalizeAngle(SimMath::subf32(angleToGoal, objAngle));      // [ebp-0x20]
	Coord3D goalCopy = goal;                                 // [ebp-0x3c]
	const bool terrorBack = t.m_canMoveBackwards != 0 && host.testModelCondition(b.emotionTerror); // [ebp-0x1c]
	if (m_speed == 0.0f) // RW 0x5E723B: ucomiss against 0.0
	{
		if (terrorBack)
		{
			m_flags |= LOCOMOTOR_FLAG_BACKING_UP;  // RW 0x5E2CC1(7, bl)
		}
		else
		{
			m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP;
		}
		m_byte98 = false;
	}
	if (m_flags & LOCOMOTOR_FLAG_BACKING_UP)
	{
		if (!terrorBack)
		{
			m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP; // RW 0x5E7277
		}
		else
		{
			angleToGoal = normalizeAngle(SimMath::subf32(angleToGoal, kPi));
			rel = normalizeAngle(NumericState::pc24Sub(angleToGoal, objAngle));
			goalCopy.x = SimMath::subf32(SimMath::mulf32(pos.x, 2.0f), goal.x);
			goalCopy.y = SimMath::subf32(SimMath::mulf32(pos.y, 2.0f), goal.y);
			goalCopy.z = SimMath::subf32(SimMath::mulf32(pos.z, 2.0f), goal.z);
		}
	}
	// reform instead of wheel when the turn exceeds MaxTurnWithoutReform (RW 0x5E72F7-0x5E7368)
	const double cosMax = SimMath::cosd(t.m_maxTurnWithoutReform);
	const float cosRel = SimMath::cosf32(rel);
	if (cosMax > (double)cosRel)
	{
		if (pathRemaining > 20.0f && contain) // RW 0xBDBC6C
		{
			host.hordeBeginReform();       // contain vfunc +0x3C
			setMatrixAngle(angleToGoal);   // RW 0x5E3A81
			host.setTransform(m_matrix);   // RW 0x70BBA3
			host.hordeEndReform();         // contain vfunc +0x40
		}
	}
	if (t.m_turnWhileMoving)
	{
		rotateTowardsPosition(host, goalCopy, maxTurnRate, &rel); // RW 0x5E4F44 directly: the matrix was copied by the dispatcher
	}
	if (!isFasterThanQuarterSpeed(host) && host.thingWaitsForFormation() && contain && !host.hordeFormationReady(angleToGoal))
	{
		desiredSpeed = SimMath::mulf32(desiredSpeed, kTinyAngle); // RW 0x5E73D4: x0.1 while the formation is not ready
	}
	moveForward(host, goalCopy, onPathDistToGoal, desiredSpeed);
}

// ---- OTHER / WINGS (RW 0x5E6835) -------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionOther(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	rotateTowardsWrapper(host, goal, nullptr); // RW 0x5E6810
	host.setTransform(m_matrix);               // RW 0x70BA76
	moveForward(host, goal, onPathDistToGoal, desiredSpeed);
}

// ---- calcPreferredHeight (RW 0x5E5D9A; lane SHIP-1) -------------------------------------------------------------------------------------
// TARGET FACTS (RotWK game.dat, caveat S-001; tier B same-shape with BFME2 0x5E6532, no decomp source; read from the disassembly):
//   PreferredAttackHeight (+ 0x4C) not above 0, or no AI (obj + 0x260): the preferred height (+ 0x48). Else d = RW 0x403111 of (goal - position) in 2D (z 0),
//   stored float; above = z - surface (SSE); the current weapon RW 0x68B58C(0). A dead object (+ 0x458 bit 0): SPLATTED (obj + 0x118 bit 26) set when it is
//   not and 0 >= above; the result 0. No weapon or CirclingRadius (template + 0x64) not above 0: state (+ 0x9C) 0, the preferred height. State 0: without
//   flag 4, when CirclingRadius > d and the AI's slot 0x1BC answers true, state 1; the preferred height. State 1: m = min(d / CirclingRadius, 1);
//   m x (preferred - attack) + attack; state 2 when the weapon's status (RW 0x6CDCE7) is not READY_TO_FIRE or the AI's slot 0x1BC answers false. State 2:
//   m = min(d / CirclingRadius x 2, 1); (preferred - attack) x m + attack; state 0 once above >= the preferred height. Any other state: the preferred height.
float Locomotor::calcPreferredHeight(LocomotorHost &host, const Coord3D &goal, float surfaceHeight)
{
	const LocomotorTemplate &t = *m_template;
	if (!(m_preferredAttackHeight > 0.0f) || !host.hasAI())
	{
		return m_preferredHeight;
	}
	const Coord3D pos = host.getPosition();
	const float d = (float)SimMath::length3d(SimMath::subf32(goal.x, pos.x), SimMath::subf32(goal.y, pos.y), 0.0f);
	const float above = SimMath::subf32(pos.z, surfaceHeight);
	const bool weapon = host.hasCurrentWeapon(); // RW 0x5E5E1E: RW 0x68B58C(0); its status is asked in state 1 alone (RW 0x5E5F36)
	if (host.isEffectivelyDead())
	{
		if (!(0.0f < above))
		{
			const int splatted = bitIndex(TheModelConditionNames, "SPLATTED");
			if (!host.testModelCondition(splatted))
			{
				host.setModelCondition(splatted, true);
			}
		}
		return 0.0f; // RW 0xC1B594
	}
	if (!weapon || !(t.m_circlingRadius > 0.0f))
	{
		m_attackHeightState = 0;
		return m_preferredHeight;
	}
	const float span = SimMath::subf32(m_preferredHeight, m_preferredAttackHeight);
	switch (m_attackHeightState)
	{
	case 0:
		if (!(m_flags & (unsigned)LOCOMOTOR_FLAG_BIT2) && t.m_circlingRadius > d && host.aiStateActive())
		{
			m_attackHeightState = 1;
		}
		return m_preferredHeight;
	case 1:
	{
		float m = SimMath::divf32(d, t.m_circlingRadius);
		if (m > 1.0f)
		{
			m = 1.0f;
		}
		const float v = SimMath::addf32(SimMath::mulf32(m, span), m_preferredAttackHeight);
		if (host.currentWeaponStatusWriteBack() != 0 || !host.aiStateActive()) // RW 0x5E5F36 .. 0x5E5F4C: the status first, slot 0x1BC only when it is ready
		{
			m_attackHeightState = 2;
		}
		return v;
	}
	case 2:
	{
		float m = SimMath::mulf32(SimMath::divf32(d, t.m_circlingRadius), 2.0f);
		if (m > 1.0f)
		{
			m = 1.0f;
		}
		const float v = SimMath::addf32(SimMath::mulf32(span, m), m_preferredAttackHeight);
		if (!(above < m_preferredHeight))
		{
			m_attackHeightState = 0;
		}
		return v;
	}
	default:
		return m_preferredHeight;
	}
}

// ---- HOVER (RW 0x5E742E; lane SHIP-1) ------------------------------------------------------------------------------------------
// TARGET FACTS (RotWK game.dat, caveat S-001; tier A with BFME2 0x5E7C4C, the decomp has no source for it; ZH Locomotor::moveTowardsPositionHover is the same
// shape): the OTHER mover (RW 0x5E6835), then TheTerrainLogic vslot 0x4C (isUnderwater) at the object's position (obj + 0x38 / + 0x3C, the transform the OTHER
// mover wrote); under water with flag 0x20 clear: flag 0x20 set and OVER_WATER (obj + 0x118 bit 5 = model condition 101) set when it is not; not under water
// with flag 0x20 set: flag 0x20 cleared and OVER_WATER cleared when it is set (RW 0x68B53C after each change).
void Locomotor::moveTowardsPositionHover(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const Bits &b = bits();
	moveTowardsPositionOther(host, goal, onPathDistToGoal, desiredSpeed);
	const Coord3D p = host.getPosition();
	if (host.isUnderwater(p.x, p.y, nullptr))
	{
		if (m_flags & LOCOMOTOR_FLAG_OVER_WATER)
		{
			return;
		}
		m_flags |= LOCOMOTOR_FLAG_OVER_WATER;
		if (!host.testModelCondition(b.overWater))
		{
			host.setModelCondition(b.overWater, true);
		}
		return;
	}
	if (!(m_flags & LOCOMOTOR_FLAG_OVER_WATER))
	{
		return;
	}
	m_flags &= ~(unsigned)LOCOMOTOR_FLAG_OVER_WATER;
	if (host.testModelCondition(b.overWater))
	{
		host.setModelCondition(b.overWater, false);
	}
}

// ---- SHIP (RW 0x5E613F; lane SHIP-1) ------------------------------------------------------------------------------------------
// TARGET FACTS (RotWK game.dat, caveat S-001; tier A with BFME2 0x5E68D7, no decomp source; read from the disassembly). The Wheels mover's shape (RW 0x5E83FD)
// with three differences: the turn's slow-down has no quarter-speed branch, the speed factor is |rel| / (3.5 x the turn rate) capped at 0.9 (not |rel| x 4/pi
// capped at 1), and a ship at a standstill (maximum speed 0) does nothing (no wheels-stopped notice):
//   0x5E614E maxSpeed = RW 0x5E3F49 (stored float); desiredSpeed is lowered to it (x87 compare)
//   0x5E6169 .. 0x5E61B5 angleToGoal = RW 0x441BF4(goal.y - y, goal.x - x) (SSE subtractions), rel = normalize(angleToGoal - angle) (RW 0x644FD0)
//   0x5E61B8 .. 0x5E6220 backing = CanMoveBackwards (template + 0xD8 != 0); off when the 2D estimate (RW 0x403720) of the stored point (+0x14) less the position
//     is above BackingUpDistanceMin (+0x140) and onPathDistToGoal is above BackingUpDistanceMax (+0x144)
//   0x5E6224 c0 = the speed (+0x40)
//   0x5E6230 .. 0x5E628D backing and |rel| > BackingUpAngle x pi (x87): BACKING_UP (obj + 0x11C 0x10000000) set when clear, flag 0x80 set; otherwise BACKING_UP
//     cleared when set and flag 0x80 cleared
//   0x5E6291 .. 0x5E62B5 flag 0x80: angleToGoal = normalize(angleToGoal - pi)
//   0x5E62B9 .. 0x5E6308 the point 1000 (RW 0xBD4388) out along angleToGoal (fcos / fsin, x87 fmul and fadd), RW 0x5E60C7(obj, point, 0)
//   0x5E630D .. 0x5E632D RW 0x5E40AD (getMaxAcceleration) >= c0 (fcomip, jc): rel x 2 (RW 0xBD889C)
//   0x5E6332 maxSpeed > 0 (RW 0xC1B594): f = fabs(rel) / (3.5 (RW 0xD9E040, only this function reads it) x RW 0x5E372D) (x87, stored float), capped at 0.9
//     (RW 0xBDE198); desiredSpeed = (1 - f) x desiredSpeed (SSE); BackingUpStopWhenTurning (+0x13C) with flag 0x80 and TURN_LEFT or TURN_RIGHT (obj + 0x11C bits
//     5 / 6): desiredSpeed = 0; then moveForward (RW 0x5E586F)
void Locomotor::moveTowardsPositionShip(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	const float maxSpeed = getMaxSpeedForCondition(host);
	if (desiredSpeed > maxSpeed)
	{
		desiredSpeed = maxSpeed;
	}
	const float objAngle = host.getAngle();
	const Coord3D pos = host.getPosition();
	float angleToGoal = (float)SimMath::atan2d(SimMath::subf32(goal.y, pos.y), SimMath::subf32(goal.x, pos.x));
	float rel = normalizeAngle(SimMath::subf32(angleToGoal, objAngle));

	bool backing = t.m_canMoveBackwards != 0;
	if (backing)
	{
		const float dist = approxLength2D(SimMath::subf32(m_preferredPoint.x, pos.x), SimMath::subf32(m_preferredPoint.y, pos.y));
		if (dist > t.m_backingUpDistanceMin && onPathDistToGoal > t.m_backingUpDistanceMax)
		{
			backing = false;
		}
	}
	const float c0 = m_speed;
	if (backing && (double)fabsd(rel) > (double)NumericState::pc24MulD((double)t.m_backingUpAngle, (double)kPi))
	{
		if (!host.testModelCondition(b.backingUp))
		{
			host.setModelCondition(b.backingUp, true);
		}
		m_flags |= LOCOMOTOR_FLAG_BACKING_UP;
	}
	else
	{
		if (host.testModelCondition(b.backingUp))
		{
			host.setModelCondition(b.backingUp, false);
		}
		m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BACKING_UP;
	}
	if (m_flags & LOCOMOTOR_FLAG_BACKING_UP)
	{
		angleToGoal = normalizeAngle(SimMath::subf32(angleToGoal, kPi));
	}
	Coord3D farPoint{ pos.x, pos.y, pos.z };
	farPoint.x = NumericState::pc24AddD((double)NumericState::pc24MulD(SimMath::cosd(angleToGoal), 1000.0), (double)pos.x);
	farPoint.y = NumericState::pc24AddD((double)NumericState::pc24MulD(SimMath::sind(angleToGoal), 1000.0), (double)pos.y);
	rotateTowardsWrapper(host, farPoint, nullptr);

	if (!(getMaxAcceleration(host) < c0))
	{
		rel = SimMath::mulf32(rel, 2.0f);
	}
	if (!(maxSpeed > 0.0f))
	{
		return;
	}
	const float turnRate = getMaxTurnRate(host);
	float f = NumericState::pc24DivD((double)fabsd(rel), (double)NumericState::pc24Mul(3.5f, turnRate)); // RW 0xD9E040
	if (f > 0.9f)
	{
		f = 0.9f; // RW 0xBDE198
	}
	desiredSpeed = SimMath::mulf32(SimMath::subf32(1.0f, f), desiredSpeed);
	if (t.m_backingUpStopWhenTurning && (m_flags & LOCOMOTOR_FLAG_BACKING_UP) && (host.testModelCondition(b.turnLeft) || host.testModelCondition(b.turnRight)))
	{
		desiredSpeed = 0.0f;
	}
	moveForward(host, goal, onPathDistToGoal, desiredSpeed);
}

// ---- handleBehaviorZ (RW 0x5E76AC): NO_Z_MOTIVE_FORCE, FLOATING_Z, SEA_LEVEL ------------------------------------------------------------------
void Locomotor::handleBehaviorZ(LocomotorHost &host, const Coord3D &goal)
{
	const LocomotorTemplate &t = *m_template;
	int zAxis = t.m_zAxisBehavior;
	if (zAxis == Z_SCALING_WALLS && !host.isScalingWall())
	{
		zAxis = Z_NO_Z_MOTIVE_FORCE; // RW 0x5E76CA-0x5E76EC: an object that is not on a wall is moved like NO_Z_MOTIVE_FORCE
	}
	if (zAxis == Z_FLOATING_Z)
	{
		// RW 0x5E7709-0x5E7745: the pending position is the matrix position, the wading / cliff updater runs (RW 0x5E5785, not ported: S-084); the height
		// the movers left in the matrix stays: no ground snap
		Coord3D p{ m_matrix.m[0][3], m_matrix.m[1][3], m_matrix.m[2][3] }; // RW 0x5E3A5A
		host.setPendingPosition(p);
		noteStop(allStops()[1].c_str());
		return;
	}
	if (zAxis == Z_SEA_LEVEL)
	{
		// lane SHIP-1, RW 0x5E781F-0x5E78D6 (case 1 of the table RW 0x5E7CA3): unless the object is DISABLED_HELD (obj + 0x1C8 bit 3), the height is the water's
		// where standing water covers the matrix position (TheTerrainLogic vslot 0x4C), else the ground's (vslot 0x1C on the object's layer, as NO_Z_MOTIVE_FORCE's
		// query); a SUBMARINE (template KindOf, + 0x11F bit 3) adds (speed / (maxSpeed + 0.1) + 1) x the preferred height (x87: fadd, fdivr, fadd, fmul, fadd), any
		// other object the preferred height (SSE add); the matrix translation takes x, y and that height. The common tail (RW 0x5E7C40) follows.
		Coord3D p{ m_matrix.m[0][3], m_matrix.m[1][3], m_matrix.m[2][3] }; // RW 0x5E3A5A
		if (!host.isDisabledHeld())
		{
			float waterZ = 0.0f;
			float h = host.isUnderwater(p.x, p.y, &waterZ) ? waterZ : host.groundHeightAt(p.x, p.y);
			if (host.isKindOfSubmarine())
			{
				const float ratio = NumericState::pc24Div(m_speed, NumericState::pc24Add(getMaxSpeedForCondition(host), kTinyAngle)); // RW 0xBD83D4
				h = NumericState::pc24Add(NumericState::pc24Mul(NumericState::pc24Add(ratio, 1.0f), m_preferredHeight), h);
			}
			else
			{
				h = SimMath::addf32(m_preferredHeight, h);
			}
			p.z = h;
			m_matrix.m[0][3] = p.x;
			m_matrix.m[1][3] = p.y;
			m_matrix.m[2][3] = h;
		}
		const Coord3D cur = host.getPosition();
		const Coord3D effective = host.hasPendingPosition() ? host.getPendingPosition() : cur;
		host.setPendingPosition(Coord3D{ effective.x, effective.y, SimMath::subf32(SimMath::mulf32(p.z, 2.0f), cur.z) });
		return;
	}
	if (zAxis == Z_SURFACE_RELATIVE_HEIGHT || zAxis == Z_ABSOLUTE_HEIGHT)
	{
		// lane SHIP-1, RW 0x5E7AF6 .. 0x5E7C3B (cases 2 and 3 of the table RW 0x5E7CA3): nothing while the preferred height (+ 0x48) is 0 and flag 8 (ZH
		// PRECISE_Z_POS) is clear. The surface (RW 0x5E3960: the water's height under standing water, else the ground's) for SURFACE_RELATIVE_HEIGHT, 0 for
		// ABSOLUTE_HEIGHT; the aim = RW 0x5E5D9A + the surface (x87 fadd, stored), or the goal's z with flag 8. The step: a dead object (+ 0x458 bit 0) GameData
		// Gravity, else (((aim - z) x PreferredHeightDamping (+ 0x50) + z) - z) / (LOGICFRAMES_PER_SECOND (RW 0xD9F608, int 5) x 2.0) (SSE); a step other than 0
		// moves the matrix to (x, y, z + step). A step above 0.5 (RW 0xBD869C) sets CLIMBING (obj + 0x118 bit 7) and reveals a stealthy object (RW 0x68FBD3 ->
		// 0x7767A9: not ported, reported), else CLIMBING is cleared. The common tail (RW 0x5E7C40) follows.
		Coord3D p{ m_matrix.m[0][3], m_matrix.m[1][3], m_matrix.m[2][3] }; // RW 0x5E3A5A
		if (!(m_preferredHeight == 0.0f) || (m_flags & 0x8u))
		{
			float surface = 0.0f;
			if (t.m_zAxisBehavior == Z_SURFACE_RELATIVE_HEIGHT)
			{
				float waterZ = 0.0f;
				surface = host.isUnderwater(p.x, p.y, &waterZ) ? waterZ : host.groundHeightAt(p.x, p.y); // RW 0x5E3960 (vslot 0x4C, else vslot 0x18)
			}
			float aim = NumericState::pc24Add(calcPreferredHeight(host, goal, surface), surface);
			if (m_flags & 0x8u)
			{
				aim = goal.z;
			}
			float step;
			if (host.isEffectivelyDead())
			{
				step = host.gravity();
			}
			else
			{
				const float frames2 = SimMath::mulf32((float)LOGICFRAMES_PER_SECOND, 2.0f); // cvtsi2ss [0xD9F608], mulss RW 0xBD889C
				step = SimMath::subf32(aim, p.z);
				step = SimMath::mulf32(step, m_preferredHeightDamping);
				step = SimMath::addf32(step, p.z);
				step = SimMath::subf32(step, p.z);
				step = SimMath::divf32(step, frames2);
			}
			if (!(step == 0.0f))
			{
				p.z = SimMath::addf32(step, p.z);
				m_matrix.m[0][3] = p.x;
				m_matrix.m[1][3] = p.y;
				m_matrix.m[2][3] = p.z;
			}
			const int climbing = bitIndex(TheModelConditionNames, "CLIMBING");
			if (step > 0.5f)
			{
				if (!host.testModelCondition(climbing))
				{
					host.setModelCondition(climbing, true);
				}
				if (host.hasStealthModule())
				{
					noteStop(allStops()[1].c_str()); // RW 0x5E7C0A: the stealth reveal of a climbing object is not ported
				}
			}
			else if (host.testModelCondition(climbing))
			{
				host.setModelCondition(climbing, false);
			}
		}
		const Coord3D cur = host.getPosition();
		const Coord3D effective = host.hasPendingPosition() ? host.getPendingPosition() : cur;
		host.setPendingPosition(Coord3D{ effective.x, effective.y, SimMath::subf32(SimMath::mulf32(p.z, 2.0f), cur.z) });
		return;
	}
	if (zAxis != Z_NO_Z_MOTIVE_FORCE)
	{
		noteStop(allStops()[1].c_str());
		throw std::logic_error("S-084: ZAxisBehavior other than NO_Z_MOTIVE_FORCE, FLOATING_Z, SEA_LEVEL, SURFACE_RELATIVE_HEIGHT, ABSOLUTE_HEIGHT and SCALING_WALLS is not ported");
	}
	Coord3D pos{ m_matrix.m[0][3], m_matrix.m[1][3], m_matrix.m[2][3] }; // RW 0x5E3A5A
	if (!host.zMotionSuppressed())
	{
		// RW 0x5E774A-0x5E7816: z comes from the terrain (RW 0x5E5785 on the ground layer, else the terrain query);
		// the layer / water / falling special cases are not ported (S-084)
		noteStop(allStops()[1].c_str());
		pos.z = host.groundHeightAt(pos.x, pos.y);
		m_matrix.m[2][3] = pos.z;
	}
	// tail, RW 0x5E7C40: the pending position is the previous effective position with an extrapolated z
	const Coord3D cur = host.getPosition();
	const Coord3D effective = host.hasPendingPosition() ? host.getPendingPosition() : cur;
	float z2 = SimMath::mulf32(pos.z, 2.0f);
	z2 = SimMath::subf32(z2, cur.z);
	host.setPendingPosition(Coord3D{ effective.x, effective.y, z2 });
}

// ---- moveTowardsAngle (RW 0x5E98D6; lane SMOOTH-3) ------------------------------------------------------------------------
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   0x5E98E3 flags &= ~4; 0x5E98FA: nothing when the physics motion is disabled (RW 0x5E3A1B, obj+0x264 byte +0x5C)
//   0x5E9907: the object's transform is the working matrix; 0x5E9957 .. 0x5E9997: cos(normalize(angle - obj angle)) against cos(MaxTurnWithoutReform)
//     (RW 0x42F4E0 both) -> a horde contain reforms around the instant turn (contain vfuncs +0x3C, +0x40), as the HORDE mover does
//   0x5E99DE: v = RW 0x5E3796 = ai+0x1F8 * MinSpeed (template +0x28) * 0.2 (x87)
//   v > 0: the goal 2 v ahead along the angle (x87: cos * v, doubled, plus the position), RW 0x5E8865(goal, 99999, v): the full mover
//   v <= 0: a point 1000 ahead along the angle (RW 0xBD4388), RW 0x5E6810 (rotate at the turn rate, then setTransform), RW 0x5E76AC handleBehaviorZ(pos)
// The port had the heading set at once (AIMover's angle goal): a horde member reaching its slot spun to the formation's facing in one frame.
void Locomotor::locomotorMoveTowardsAngle(LocomotorHost &host, float angle)
{
	m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BIT2;
	if (host.physicsMotionDisabled())
	{
		return;
	}
	m_matrix = host.getTransform();
	const float rel = normalizeAngle(SimMath::subf32(angle, host.getAngle()));
	const float cosRel = SimMath::cosDet(rel);
	const float cosMax = SimMath::cosDet(m_template->m_maxTurnWithoutReform);
	if (cosMax > cosRel && host.hasHordeContain())
	{
		host.hordeBeginReform();  // contain vfunc +0x3C
		setMatrixAngle(angle);    // RW 0x5E3A81
		host.setTransform(m_matrix); // RW 0x70BBA3
		host.hordeEndReform();    // contain vfunc +0x40
	}
	const float v = NumericState::pc24Mul(NumericState::pc24Mul(host.locomotorSetSpeed(), m_template->m_minSpeed), SECONDS_PER_LOGICFRAME_REAL); // RW 0x5E3796
	const Coord3D pos = host.getPosition();
	Coord3D goal = pos;
	if (v > 0.0f)
	{
		float dx = NumericState::pc24Mul(SimMath::cosDet(angle), v);
		float dy = NumericState::pc24Mul(SimMath::sinDet(angle), v);
		goal.x = NumericState::pc24Add(NumericState::pc24Add(dx, dx), pos.x); // fadd st0, st0; fadd x
		goal.y = NumericState::pc24Add(NumericState::pc24Add(dy, dy), pos.y);
		locomotorMoveTowardsPosition(host, goal, 99999.0f, v); // RW 0xBF3C58
		return;
	}
	goal.x = NumericState::pc24Add(NumericState::pc24Mul(SimMath::cosDet(angle), 1000.0f), pos.x); // RW 0xBD4388
	goal.y = NumericState::pc24Add(NumericState::pc24Mul(SimMath::sinDet(angle), 1000.0f), pos.y);
	rotateTowardsWrapper(host, goal, nullptr); // RW 0x5E6810: the turn ...
	host.setTransform(m_matrix);               // ... then RW 0x70BA76
	handleBehaviorZ(host, pos);                // RW 0x5E76AC(obj, obj + 0x38)
}

// RW 0x5E4C40 (lane EXIT-1): comiss v, speed; above: speed += getMaxAcceleration (x87 fadd, fst dword) and, when that passed v, v; else v. Then clamped to
// [0, getMaxSpeedForCondition] (SSE: 0 when 0 > speed, the maximum when speed > it)
void Locomotor::setSpeedTowards(const LocomotorHost &host, float v)
{
	if (v > m_speed)
	{
		m_speed = SimMath::addf32(getMaxAcceleration(host), m_speed);
		if (m_speed > v)
		{
			m_speed = v;
		}
	}
	else
	{
		m_speed = v;
	}
	const float maxSpeed = getMaxSpeedForCondition(host);
	if (0.0f > m_speed)
	{
		m_speed = 0.0f;
	}
	else if (m_speed > maxSpeed)
	{
		m_speed = maxSpeed;
	}
}

// ---- maintainCurrentPosition (RW 0x5E7CC7; lane EXIT-1) ------------------------------------------------------------------------
// TARGET FACTS (RotWK game.dat, caveat S-001), Ghidra decompile of RW 0x5E7CC7(this = locomotor, obj), called by doLocomotor (RW 0x669932) for goal type 0:
//   null obj: false. The object's transform (obj+0x08..+0x37) becomes the working matrix (+0x68). Without flag 4 (+0x44) the object's position (+0x38) is the
//   held position (+0x08) and flag 4 is set. While the object's MOVING (+0x110 bit 29) is off: TURN_LEFT, TURN_RIGHT, TURN_LEFT_HIGH_SPEED,
//   TURN_RIGHT_HIGH_SPEED, ACCELERATE, DECELERATE and WALKING (+0x11C bits 0x20 0x40 0x200 0x400 0x80 0x100 0x08) are cleared, one by one (RW 0x68B53C each).
//   Then flag 1 (braking) and +0x98 are cleared. No physics module (obj+0x264): true. By appearance (template +0x74): LEGS_TWO, WHEELS_FOUR,
//   LEGS_FOUR_HUGE, HORDE, LEGS_TWO_HUGE: speed (+0x40) = 0, MOVING cleared (RW 0x5E5D7E), result false; TREADS: MOVING cleared, result false (the
//   speed is kept); HOVER, GIANT_BIRD: speed = 0, true; WINGS: RW 0x5E74C1, true; any other: speed = 0, MOVING cleared, true. Then RW 0x5E76AC on the
//   held position (true when it says so) and the transform write RW 0x70BA76 (ported below for WINGS; other appearances: S-1750).
// Before this port the goal-less unit's locomotor did nothing, so a unit whose walk ended without the move state's exit (a horde member released to its
// melee, an idle member of a parked horde) kept MOVING and the turn conditions: the run cycle on the spot of QA-2's finding 2.
bool Locomotor::locomotorMaintainCurrentPosition(LocomotorHost &host)
{
	const Bits &b = bits();
	m_matrix = host.getTransform();
	if ((m_flags & (unsigned)LOCOMOTOR_FLAG_BIT2) == 0)
	{
		m_maintainPosition = host.getPosition();
		m_flags |= (unsigned)LOCOMOTOR_FLAG_BIT2;
	}
	if (!host.testModelCondition(b.moving))
	{
		for (int bit : { b.turnLeft, b.turnRight, b.turnLeftHigh, b.turnRightHigh, b.accelerate, b.decelerate, b.walking })
		{
			if (host.testModelCondition(bit))
			{
				host.setModelCondition(bit, false);
			}
		}
	}
	m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BRAKING;
	m_byte98 = false;
	if (!host.hasPhysicsModule())
	{
		return true;
	}
	bool result = true;
	switch (m_template->m_appearance)
	{
	case LOCO_LEGS_TWO:
	case LOCO_WHEELS_FOUR:
	case LOCO_LEGS_FOUR_HUGE:
	case LOCO_HORDE:
	case LOCO_LEGS_TWO_HUGE:
		m_speed = 0.0f;
		// fall through (RW 0x5E7DFD: TREADS joins here)
	case LOCO_TREADS:
		if (host.testModelCondition(b.moving))
		{
			host.setModelCondition(b.moving, false); // RW 0x5E5D7E
		}
		result = false;
		break;
	case LOCO_HOVER:
	case LOCO_GIANT_BIRD:
		m_speed = 0.0f;
		break;
	case LOCO_WINGS:
		maintainCurrentPositionWings(host);
		// RW 0x5E7E60: wings always need another update, including while climbing from the ground.
		handleBehaviorZ(host, m_maintainPosition);
		host.setTransform(m_matrix);
		return true;
	default:
		m_speed = 0.0f;
		if (host.testModelCondition(b.moving))
		{
			host.setModelCondition(b.moving, false);
		}
		break;
	}
	noteStop(allStops()[2].c_str()); // the tail (RW 0x5E76AC on the held position, RW 0x70BA76) is not run
	return result;
}

// TARGET: RW 0x5E74C1..0x5E7604, read from RotWK's disassembly. BFME2's
// maintainCurrentPosition dispatch and BFME1's matched wings body explain the field names.
// This is idle flight, not an AI move order: retain the original hold point across frames.
void Locomotor::maintainCurrentPositionWings(LocomotorHost &host)
{
	const Coord3D pos = host.getPosition();
	if (!(pos.z > host.groundHeightAt(pos.x, pos.y))) // RW 0x4B12E8 -> height above terrain > 0
	{
		return;
	}
	m_flags |= LOCOMOTOR_FLAG_NO_BRAKE;
	float radius = m_template->m_circlingRadius;
	if (radius == 0.0f)
	{
		const float turn = getMaxTurnRate(host);
		radius = turn > 0.0f ? SimMath::divf32(getMinSpeed(host), turn) : 99999.0f; // RW 0x5E38FA
	}
	const float dx = NumericState::pc24Sub(m_maintainPosition.x, pos.x);
	const float dy = SimMath::subf32(m_maintainPosition.y, pos.y);
	float angle = fabsd(dx) < 0.001f && fabsd(dy) < 0.001f ? host.getAngle() : (float)SimMath::atan2d(dy, dx);
	float aim = 2.7488937377929688f; // RW 0xBF4B2C: pi - pi/8
	if (radius < 0.0f)
	{
		radius = SimMath::subf32(0.0f, radius);
		aim = -2.7488937377929688f;
	}
	angle = NumericState::pc24Add(aim, angle);
	Coord3D goal = m_maintainPosition;
	goal.x = NumericState::pc24Add(NumericState::pc24Mul(SimMath::cosDet(angle), radius), goal.x);
	goal.y = NumericState::pc24Add(NumericState::pc24Mul(SimMath::sinDet(angle), radius), goal.y);
	moveTowardsPositionOther(host, goal, 0.0f, getMinSpeed(host)); // RW 0x5E740D -> 0x5E6835
}

// ---- the dispatcher (RW 0x5E8865) ---------------------------------------------------------------------------------------------
void Locomotor::locomotorMoveTowardsPosition(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed)
{
	const LocomotorTemplate &t = *m_template;
	const Bits &b = bits();
	m_flags &= ~(unsigned)LOCOMOTOR_FLAG_BIT2;
	m_matrix = host.getTransform(); // the object's transform becomes the working matrix
	if (host.logicFrame() <= m_desiredSpeedCapUntilFrame && desiredSpeed > m_desiredSpeedCap)
	{
		desiredSpeed = m_desiredSpeedCap; // RW 0x5E88CB: a temporary cap that holds until a frame
	}
	const float maxSpeed = getMaxSpeedForCondition(host);
	if (desiredSpeed > maxSpeed)
	{
		desiredSpeed = maxSpeed;
	}
	if (host.physicsMotionDisabled() || host.testObjectStatus(b.statusContesting))
	{
		return; // RW 0x5E8906-0x5E8922
	}
	m_nearPathEnd = false;
	LocomotorPath *path = host.getPath();
	if (path)
	{
		m_nearPathEnd = path->isNearPathEnd(); // RW 0x5E2DBA
	}
	if (t.m_scalesWalls)
	{
		noteStop(allStops()[1].c_str()); // the wall-scaling look-ahead (RW 0x5E8956-0x5E8FF8) is not ported
	}
	switch (t.m_appearance)
	{
	case LOCO_LEGS_TWO:
	case LOCO_LEGS_FOUR_HUGE:
	case LOCO_LEGS_TWO_HUGE:
		moveTowardsPositionLegs(host, goal, onPathDistToGoal, desiredSpeed);
		break;
	case LOCO_WHEELS_FOUR:
	case LOCO_TREADS:
		moveTowardsPositionWheels(host, goal, onPathDistToGoal, desiredSpeed);
		break;
	case LOCO_HORDE:
		moveTowardsPositionHorde(host, goal, onPathDistToGoal, desiredSpeed);
		break;
	case LOCO_WINGS:
		moveTowardsPositionOther(host, goal, onPathDistToGoal, desiredSpeed);
		break;
	case LOCO_GIANT_BIRD:
		break; // RW 0x5E9180: the table entry skips every mover
	case LOCO_HOVER:
		moveTowardsPositionHover(host, goal, onPathDistToGoal, desiredSpeed); // RW 0x5E90E3 (lane SHIP-1)
		break;
	case LOCO_SHIP:
		moveTowardsPositionShip(host, goal, onPathDistToGoal, desiredSpeed); // RW 0x5E911A (lane SHIP-1)
		break;
	default:
		moveTowardsPositionOther(host, goal, onPathDistToGoal, desiredSpeed); // RW 0x5E9168
		break;
	}
	path = host.getPath();
	if (!(path && path->hasExplicitZ()))
	{
		handleBehaviorZ(host, goal); // RW 0x5E919C
	}
	host.setTransform(m_matrix); // RW 0x70BA76
	m_nearPathEnd = false;
}

// MOVE-1: the OpenBFME state hash of a locomotor instance: the speed and flags, the caps, the pending desired-speed cap, the working matrix and the
// small state bytes the movers keep between frames
void Locomotor::crc(StateHasher &h) const
{
	h.addFloat(m_lookAheadMult);
	h.addFloat(m_maxSpeedCap);
	h.addFloat(m_temporarySpeedCap);
	h.addFloat(m_accelerationCap);
	h.addFloat(m_brakingCap);
	h.addFloat(m_turnRateCap);
	h.addFloat(m_closeEnoughDist);
	h.addFloat(m_speed);
	h.addU32(m_flags);
	h.addFloat(m_desiredSpeedCap);
	h.addU32(m_desiredSpeedCapUntilFrame);
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			h.addFloat(m_matrix.m[r][c]);
		}
	}
	h.addBool(m_byte98);
	h.addBool(m_nearPathEnd);
	h.addBool(m_byte9a);
	h.addI32(m_turnDirection);
	h.addFloat(m_preferredPoint.x);
	h.addFloat(m_preferredPoint.y);
	h.addFloat(m_preferredPoint.z);
	h.addFloat(m_maintainPosition.x); // lane EXIT-1
	h.addFloat(m_maintainPosition.y);
	h.addFloat(m_maintainPosition.z);
	if (m_template->m_zAxisBehavior == Z_SURFACE_RELATIVE_HEIGHT || m_template->m_zAxisBehavior == Z_ABSOLUTE_HEIGHT)
	{
		h.addI32(m_attackHeightState); // lane SHIP-1: hashed for these locomotors alone (every other game's hash is unchanged)
	}
	if (m_template->m_appearance == LOCO_GIANT_BIRD)
	{
		// lane FLY-1: the fields only the GIANT_BIRD movers write (hashed for those locomotors alone: every other game's hash is unchanged)
		h.addFloat(m_preferredHeight);
		h.addFloat(m_liftCap);
		h.addFloat(m_hoverLift);
		h.addFloat(m_hoverYaw);
	}
}
