// OpenBFME. GPL-3.0.
//
// The GIANT_BIRD movers of the Locomotor (lane FLY-1). The dispatcher (RW 0x5E8865) skips GIANT_BIRD (its table entry RW 0x5E9180 runs no mover):
// GiantBirdAIUpdate's doLocomotor (RW 0x76D25F) calls these two itself.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; both tier B same-shape against BFME2, whose decomp has them byte-matched: LocomotorRva001E9A00AerialMove.cpp
// Locomotor::rva001E9A00 for RW 0x5E91E2 and Rva001E46E1Ratios.cpp Rva001E46E1::rva001E546B for RW 0x5E4CD3; donor only, ported in our style):
//   RW 0x5E91E2 (obj, goalSpeed, &pathDistance, &spline): the look-ahead is half the bounding sphere radius (x LookAheadMult when that is over 1); the
//     object's transform becomes the working matrix; pathDistance += the aerial speed (AI + 0x5FC); the path's point there (RW 0x777DAE) is the new
//     translation, else MOVING, CLIMBING and FREEFALL go and the result is true. MOVING is set while AI + 0x57C bit 7 is set or the object is more than
//     70 from the path's anchor (AI + 0x608; RW 0x5E3BE4 x87 3D distance squared over 4900), else cleared. With NonDirtyTransform the matrix is applied at
//     once (RW 0x70BBA3). The points a look-ahead behind and ahead give the pitch (RW 0x5E3834: the normalised z of the difference) and the heading (the
//     relative angle RW 0x4B3D8D, the one behind turned by pi), averaged over the points found (times 1 / count). The turn is clamped to the turn rate,
//     the AI's blended turn (+ 0x600) = turn * 0.2 + old * 0.8, the matrix turned to orientation + turn (RW 0x5E3A81). Within four speeds of the path's end
//     point a falling pitch is mirrored, else a turn over three turn rates adds 1.5. The speed moves toward goalSpeed by the acceleration or the braking,
//     times (1 - pitch * 0.4), within [MinSpeed, the maximum]. The matrix is kept a quarter bounding sphere radius over the terrain; pitch over 0.3 sets
//     CLIMBING (clearing FREEFALL), under -0.6 FREEFALL (clearing CLIMBING), else both go. The transform is set (RW 0x70BA76) and the point one new speed
//     further is the pending position (obj + 0x198 / + 0x1A6). Result false.
//   RW 0x5E4CD3 (obj): with an AI, its aerial view and a body: the wave (fild (frame + id) * 0.1, CRT sin, x amplitude x 0.2) over the preferred height
//     (+ 0x48) is the desired height; the lift (+ 0xA0) gathers desired - current when the AI is well above it (RW 0x769F24) or the height is outside
//     [desired + amplitude * 0.2, desired * 1.5]; the lift x 0.25 is clamped to the lift limit (RW 0x5E37D4); over 0.3 CLIMBING replaces FREEFALL, else
//     CLIMBING goes; z += lift (RW 0x70C0AD); ABOUT_TO_HIT goes; the yaw (+ 0xA4) takes a logic random step in +-0.0628 (RW 0xBF4A68 / 0xBF4AC8,
//     clamped to that) and the orientation becomes normalizeAngle(angle + yaw).
// Numerics (S-081): x87 sequences run through the NumericState PC24 operations; the CRT sin is SimMath's deterministic one.

#include "GameLogic/Locomotor.h"

#include "Common/GameCommon.h"
#include "Common/NumericState.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/SimMath.h"

#include <cstring>
#include <stdexcept>

namespace
{
const float kPi = 3.14159274f;    // RW 0xBDD388
const float kTwoPi = 6.28318548f; // RW 0xBDD38C

int conditionIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("model condition missing from the registry: ") + name);
}

struct BirdBits
{
	int moving = conditionIndex("MOVING");       // 61 (RW obj + 0x113 bit 5)
	int freefall = conditionIndex("FREEFALL");   // 72 (+ 0x115 bit 0)
	int climbing = conditionIndex("CLIMBING");   // 103 (+ 0x118 bit 7)
	int aboutToHit = conditionIndex("ABOUT_TO_HIT"); // 155 (+ 0x11F bit 3)
};

const BirdBits &birdBits()
{
	static const BirdBits b;
	return b;
}

// RW 0x644FD0
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

// RW 0x5E3834 (a, b): the difference b - a normalised (RW 0x403175), then ((d.y + d.x) * 0.0) + d.z (x87): its z
float pitchBetween(const Coord3D &a, const Coord3D &b)
{
	float x = SimMath::subf32(b.x, a.x), y = SimMath::subf32(b.y, a.y), z = SimMath::subf32(b.z, a.z);
	const float len = (float)SimMath::length3d(x, y, z);
	if (len != 0.0f)
	{
		x = SimMath::divf32(x, len);
		y = SimMath::divf32(y, len);
		z = SimMath::divf32(z, len);
	}
	return SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24AddW((double)y, (double)x), 0.0), (double)z));
}

void setCondition(LocomotorHost &host, int bit, bool on)
{
	if (host.testModelCondition(bit) != on)
	{
		host.setModelCondition(bit, on);
	}
}
} // namespace

float Locomotor::getMinSpeed(const LocomotorHost &host) const
{
	// RW 0x5E3796: fld ai + 0x1F8; fmul template + 0x28; fmul [0xD9F61C] (0.2)
	return SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW((double)host.locomotorSetSpeed(), (double)m_template->m_minSpeed), (double)0.2f));
}

float Locomotor::getLiftLimit(const LocomotorHost &host) const
{
	// RW 0x5E37D4 (SSE): 0.2 * 0.2, x Lift (+ 0x48) below the movement-penalty damage state, else LiftDamaged (+ 0x4C), x the locomotor set speed; capped at + 0x24
	float v = SimMath::mulf32(0.2f, 0.2f);
	v = SimMath::mulf32(v, host.damageState() < host.movementPenaltyDamageState() ? m_template->m_lift : m_template->m_liftDamaged);
	v = SimMath::mulf32(v, host.locomotorSetSpeed());
	if (v > m_liftCap)
	{
		v = m_liftCap;
	}
	return v;
}

bool Locomotor::giantBirdFollowPath(GiantBirdLocomotorHost &host, float goalSpeed, float &pathDistance)
{
	const LocomotorTemplate &t = *m_template;
	const BirdBits &b = birdBits();
	float lookAhead = SimMath::mulf32(host.boundingSphereRadius(), 0.5f); // RW 0xBD869C
	if (1.0f < t.m_lookAheadMult)
	{
		lookAhead = SimMath::mulf32(lookAhead, t.m_lookAheadMult);
	}
	GiantBirdFlight *flight = host.aerialFlight();
	if (!flight)
	{
		return false;
	}
	m_matrix = host.getTransform();
	const float speedNow = flight->speed;
	pathDistance = SimMath::addf32(pathDistance, speedNow);
	Coord3D pos;
	if (!host.aerialQuery(pathDistance, pos))
	{
		setCondition(host, b.moving, false);
		setCondition(host, b.climbing, false);
		setCondition(host, b.freefall, false);
		return true;
	}
	m_matrix.m[0][3] = pos.x;
	m_matrix.m[1][3] = pos.y;
	m_matrix.m[2][3] = pos.z;
	bool far = (flight->flags >> 7) & 1u;
	if (!far)
	{
		const Coord3D o = host.getPosition();
		const Coord3D &a = flight->anchor;
		// RW 0x5E3BE4: x87 (dx^2 + dy^2) + dz^2, compared with 4900 (RW 0xBF4B4C)
		const double dx = SimMath::pc24SubW((double)o.x, (double)a.x), dy = SimMath::pc24SubW((double)o.y, (double)a.y), dz = SimMath::pc24SubW((double)o.z, (double)a.z);
		const double d2 = SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy)), SimMath::pc24MulW(dz, dz));
		far = 4900.0 < d2;
	}
	setCondition(host, b.moving, far);
	float angle = 0.0f, count = 0.0f, pitch = 0.0f;
	if (t.m_nonDirtyTransform)
	{
		host.setTransformAtOnce(m_matrix); // RW 0x70BBA3
	}
	Coord3D behind;
	if (host.aerialQuery(SimMath::subf32(pathDistance, lookAhead), behind))
	{
		pitch = pitchBetween(behind, pos);
		angle = normalizeAngle(SimMath::fstpDword(SimMath::pc24AddW((double)host.relativeAngle2D(behind), (double)kPi)));
		count = 1.0f;
	}
	Coord3D ahead;
	if (host.aerialQuery(SimMath::addf32(pathDistance, lookAhead), ahead))
	{
		pitch = SimMath::fstpDword(SimMath::pc24AddW((double)pitchBetween(pos, ahead), (double)pitch));
		angle = SimMath::addf32(angle, host.relativeAngle2D(ahead));
		count = SimMath::addf32(count, 1.0f);
	}
	if (0.0f < count)
	{
		const float inv = SimMath::divf32(1.0f, count); // RW 0x5E93E1: 1 / count, then the products
		angle = SimMath::mulf32(inv, angle);
		pitch = SimMath::mulf32(inv, pitch);
	}
	const float oldTurn = flight->turnRate;
	const float maxTurn = getMaxTurnRate(host);
	const bool bigTurn = !(SimMath::absD((double)angle) <= SimMath::pc24MulW((double)maxTurn, 3.0)); // RW 0xBDD42C, x87 fabs
	const float lo = SimMath::subf32(0.0f, maxTurn);
	float turn = angle <= lo ? lo : angle;
	if (turn < maxTurn)
	{
		turn = angle <= lo ? lo : angle;
	}
	else
	{
		turn = maxTurn;
	}
	flight->turnRate = SimMath::addf32(SimMath::mulf32(turn, 0.2f), SimMath::mulf32(oldTurn, 0.8f)); // RW 0xBDAD78, 0xBDE8D8
	setMatrixAngle(normalizeAngle(SimMath::fstpDword(SimMath::pc24AddW((double)host.getAngle(), (double)turn)))); // RW 0x5E3A81
	const Coord3D end = host.aerialPathEnd();
	const Coord3D o = host.getPosition();
	const float toEnd = (float)SimMath::length3d(SimMath::subf32(end.x, o.x), SimMath::subf32(end.y, o.y), SimMath::subf32(end.z, o.z)); // RW 0x5E3C12, length
	const float four = SimMath::mulf32(flight->speed, 4.0f); // RW 0xBD88C0
	if (four <= toEnd || 0.0f <= pitch)
	{
		if (bigTurn)
		{
			pitch = SimMath::addf32(pitch, 1.5f); // RW 0xBDE8C8
		}
	}
	else
	{
		pitch = SimMath::mulf32(pitch, -1.0f); // RW 0xBD19DC
	}
	// the speed (x87): toward goalSpeed by the acceleration or the braking, then x (1 - pitch * 0.4)
	double v;
	if (goalSpeed <= speedNow)
	{
		v = speedNow <= goalSpeed ? (double)speedNow : SimMath::pc24SubW((double)speedNow, (double)getBraking(host));
	}
	else
	{
		v = SimMath::pc24AddW((double)getMaxAcceleration(host), (double)speedNow);
	}
	float speed = SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24SubW(1.0, SimMath::pc24MulW((double)pitch, (double)0.4f)), v)); // RW 0xBE0DE4
	const float maxSpeed = getMaxSpeedForCondition(host);
	if (speed <= maxSpeed)
	{
		const float minSpeed = getMinSpeed(host);
		if (speed < minSpeed)
		{
			speed = minSpeed;
		}
	}
	else
	{
		speed = maxSpeed;
	}
	flight->speed = speed;
	const float x = m_matrix.m[0][3], y = m_matrix.m[1][3], z = m_matrix.m[2][3];
	const float ground = host.groundHeightAt(x, y);                                     // TheTerrainLogic vslot 0x18
	const float minZ = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)host.boundingSphereRadius(), 0.25), (double)ground)); // RW 0xBD1904
	m_matrix.m[2][3] = z <= minZ ? minZ : z;
	if (pitch > 0.3f) // RW 0xBE29D4
	{
		if (host.testModelCondition(b.freefall) || !host.testModelCondition(b.climbing))
		{
			host.clearAndSetModelConditions({ b.freefall }, { b.climbing });
		}
	}
	else if (-0.6f <= pitch) // RW 0xBF4B48
	{
		setCondition(host, b.freefall, false);
		setCondition(host, b.climbing, false);
	}
	else if (host.testModelCondition(b.climbing) || !host.testModelCondition(b.freefall))
	{
		host.clearAndSetModelConditions({ b.climbing }, { b.freefall });
	}
	host.setTransform(m_matrix); // RW 0x70BA76
	Coord3D next = host.getPosition();
	if (host.aerialQuery(SimMath::addf32(pathDistance, flight->speed), next))
	{
		host.setPendingPosition(next); // obj + 0x198, + 0x1A6 = 1
	}
	return false;
}

void Locomotor::giantBirdHover(GiantBirdLocomotorHost &host)
{
	const BirdBits &b = birdBits();
	float amplitude = SimMath::mulf32(host.boundingSphereRadius(), 0.5f);
	if (m_template->m_lookAheadMult > 1.0f)
	{
		amplitude = SimMath::mulf32(m_template->m_lookAheadMult, amplitude);
	}
	if (!host.aerialFlight() || !host.hasBody())
	{
		return;
	}
	const float baseline = m_preferredHeight;
	// fild (frame + id); fmul 0.1; CRT sin; fmul amplitude; fmul 0.2; fadd baseline; fstp dword
	const std::int32_t phase = (std::int32_t)(host.logicFrame() + host.objectId());
	const double arg = SimMath::pc24MulW((double)phase, (double)0.1f);
	const double sine = NumericState::sinDD(arg); // the CRT sin of the double (RW 0xA3CF90), the facade's (S-081)
	const float desired = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(sine, (double)amplitude), (double)0.2f), (double)baseline));
	const float current = host.heightAboveTerrainOrWater();
	bool outside = SimMath::pc24AddW(SimMath::pc24MulW((double)amplitude, (double)0.2f), (double)desired) > (double)current;
	if (!outside)
	{
		outside = current > SimMath::mulf32(desired, 1.5f);
	}
	if (host.aerialHigh() || outside)
	{
		m_hoverLift = SimMath::addf32(SimMath::subf32(desired, current), m_hoverLift);
	}
	const float cap = getLiftLimit(host);
	m_hoverLift = SimMath::mulf32(m_hoverLift, 0.25f);
	if (m_hoverLift > cap)
	{
		m_hoverLift = cap;
	}
	else
	{
		const float neg = SimMath::subf32(0.0f, cap);
		if (neg > m_hoverLift)
		{
			m_hoverLift = neg;
		}
	}
	if (m_hoverLift > 0.3f)
	{
		if (host.testModelCondition(b.freefall) || !host.testModelCondition(b.climbing))
		{
			host.clearAndSetModelConditions({ b.freefall }, { b.climbing });
		}
	}
	else
	{
		setCondition(host, b.climbing, false);
	}
	host.setPositionZ(SimMath::addf32(host.getPosition().z, m_hoverLift));
	setCondition(host, b.aboutToHit, false);
	const float step = host.logicRandomReal(-0.0628318563f, 0.0628318563f, 0x727);
	m_hoverYaw = SimMath::fstpDword(SimMath::pc24AddW((double)step, (double)m_hoverYaw));
	if (m_hoverYaw > 0.0628318563f)
	{
		m_hoverYaw = 0.0628318563f;
	}
	if (-0.0628318563f > m_hoverYaw)
	{
		m_hoverYaw = -0.0628318563f;
	}
	host.setOrientation(normalizeAngle(SimMath::fstpDword(SimMath::pc24AddW((double)host.getAngle(), (double)m_hoverYaw))));
}
