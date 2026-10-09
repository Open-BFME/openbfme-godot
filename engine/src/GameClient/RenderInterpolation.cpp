// OpenBFME. GPL-3.0.
//
// RenderInterpolation (lane SMOOTH-1): see RenderInterpolation.h. Client-only maths (plain float, <cmath>): the result only places a drawable on
// screen, never enters the simulation.

#include "GameClient/RenderInterpolation.h"

#include <cmath>
#include <cstring>

namespace RenderInterpolation
{

namespace
{
struct Quat
{
	float w, x, y, z;
};

// row-major orthonormal 3x3 -> unit quaternion (Shepperd: the largest of w, x, y, z is computed from the diagonal, the others from it)
Quat fromBasis(const float m[9])
{
	const float m00 = m[0], m01 = m[1], m02 = m[2];
	const float m10 = m[3], m11 = m[4], m12 = m[5];
	const float m20 = m[6], m21 = m[7], m22 = m[8];
	const float trace = m00 + m11 + m22;
	Quat q;
	if (trace > 0.0f)
	{
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		q.w = 0.25f * s;
		q.x = (m21 - m12) / s;
		q.y = (m02 - m20) / s;
		q.z = (m10 - m01) / s;
	}
	else if (m00 > m11 && m00 > m22)
	{
		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		q.w = (m21 - m12) / s;
		q.x = 0.25f * s;
		q.y = (m01 + m10) / s;
		q.z = (m02 + m20) / s;
	}
	else if (m11 > m22)
	{
		const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
		q.w = (m02 - m20) / s;
		q.x = (m01 + m10) / s;
		q.y = 0.25f * s;
		q.z = (m12 + m21) / s;
	}
	else
	{
		const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
		q.w = (m10 - m01) / s;
		q.x = (m02 + m20) / s;
		q.y = (m12 + m21) / s;
		q.z = 0.25f * s;
	}
	return q;
}

void toBasis(const Quat &q, float m[9])
{
	const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	m[0] = 1.0f - 2.0f * (yy + zz);
	m[1] = 2.0f * (xy - wz);
	m[2] = 2.0f * (xz + wy);
	m[3] = 2.0f * (xy + wz);
	m[4] = 1.0f - 2.0f * (xx + zz);
	m[5] = 2.0f * (yz - wx);
	m[6] = 2.0f * (xz - wy);
	m[7] = 2.0f * (yz + wx);
	m[8] = 1.0f - 2.0f * (xx + yy);
}
} // namespace

float clampAlpha(double alpha)
{
	if (!(alpha > 0.0))
	{
		return 0.0f;
	}
	return alpha < 1.0 ? (float)alpha : 1.0f;
}

Coord3D lerpPosition(const Coord3D &from, const Coord3D &to, float a)
{
	Coord3D p;
	p.x = from.x + (to.x - from.x) * a;
	p.y = from.y + (to.y - from.y) * a;
	p.z = from.z + (to.z - from.z) * a;
	return p;
}

float lerpAngle(float from, float to, float a)
{
	const float pi = 3.14159265358979f;
	float d = to - from;
	while (d > pi)
	{
		d -= 2.0f * pi;
	}
	while (d < -pi)
	{
		d += 2.0f * pi;
	}
	return from + d * a;
}

bool isZRotation(const float b[9])
{
	return b[2] == 0.0f && b[5] == 0.0f && b[6] == 0.0f && b[7] == 0.0f && b[8] == 1.0f;
}

void slerpBasis(const float from[9], const float to[9], float a, float out[9])
{
	if (a <= 0.0f)
	{
		std::memcpy(out, from, sizeof(float) * 9);
		return;
	}
	if (a >= 1.0f)
	{
		std::memcpy(out, to, sizeof(float) * 9);
		return;
	}
	const Quat q0 = fromBasis(from);
	Quat q1 = fromBasis(to);
	float dot = q0.w * q1.w + q0.x * q1.x + q0.y * q1.y + q0.z * q1.z;
	if (dot < 0.0f)
	{
		q1 = Quat{ -q1.w, -q1.x, -q1.y, -q1.z }; // the shorter arc
		dot = -dot;
	}
	float k0, k1;
	if (dot > 0.9995f)
	{
		k0 = 1.0f - a; // nearly equal: normalised lerp (the slerp weights lose precision)
		k1 = a;
	}
	else
	{
		const float theta = std::acos(dot);
		const float s = std::sin(theta);
		k0 = std::sin((1.0f - a) * theta) / s;
		k1 = std::sin(a * theta) / s;
	}
	Quat q{ q0.w * k0 + q1.w * k1, q0.x * k0 + q1.x * k1, q0.y * k0 + q1.y * k1, q0.z * k0 + q1.z * k1 };
	const float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	q = Quat{ q.w / len, q.x / len, q.y / len, q.z / len };
	toBasis(q, out);
}

Pose interpolate(bool haveRecorded, const Coord3D &recordedPos, float recordedAngle, const float recordedBasis[9], const Coord3D &currentPos,
	float currentAngle, const float currentBasis[9], double alpha)
{
	Pose p;
	p.position = currentPos;
	p.angle = currentAngle;
	std::memcpy(p.basis, currentBasis, sizeof(p.basis));
	p.zRotation = isZRotation(currentBasis);
	if (!haveRecorded)
	{
		return p;
	}
	const float a = clampAlpha(alpha);
	p.position = lerpPosition(recordedPos, currentPos, a);
	if (p.zRotation && isZRotation(recordedBasis))
	{
		p.angle = lerpAngle(recordedAngle, currentAngle, a);
		return p;
	}
	// a terrain-aligned basis on either side: the whole rotation is interpolated
	slerpBasis(recordedBasis, currentBasis, a, p.basis);
	p.zRotation = false;
	return p;
}

Coord3D catmullRom(const Coord3D &p0, const Coord3D &p1, const Coord3D &p2, const Coord3D &p3, float s)
{
	const float s2 = s * s;
	const float s3 = s2 * s;
	Coord3D out;
	out.x = 0.5f * (2.0f * p1.x + (p2.x - p0.x) * s + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * s2 + (p3.x - 3.0f * p2.x + 3.0f * p1.x - p0.x) * s3);
	out.y = 0.5f * (2.0f * p1.y + (p2.y - p0.y) * s + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * s2 + (p3.y - 3.0f * p2.y + 3.0f * p1.y - p0.y) * s3);
	out.z = 0.5f * (2.0f * p1.z + (p2.z - p0.z) * s + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * s2 + (p3.z - 3.0f * p2.z + 3.0f * p1.z - p0.z) * s3);
	return out;
}

Pose retailPose(const ObjectSnapshot &s, UnsignedInt snapshotFrame, double alpha)
{
	Pose p;
	const UnsignedInt logicFrame = snapshotFrame + 1u;
	if (s.lastMovedFrame < logicFrame - 2u)
	{
		// RW 0x6765F1 .. 0x67664E: stale, the object's transform as it is
		p.position = s.position;
		p.angle = s.angle;
		std::memcpy(p.basis, s.basis, sizeof(p.basis));
		p.zRotation = isZRotation(s.basis);
		return p;
	}
	const float a = clampAlpha(alpha); // RW 0x63256F clamps the engine's alpha to [0, 1]
	p.position = catmullRom(s.previousPos, s.recordedPos, s.position, s.nextPos, a);
	if (isZRotation(s.basis) && isZRotation(s.recordedBasis))
	{
		// the slerp of two rotations about Z is the angle interpolated the short way round (exact up to rounding)
		p.angle = lerpAngle(s.recordedAngle, s.angle, a);
		p.zRotation = true;
		return p;
	}
	slerpBasis(s.recordedBasis, s.basis, a, p.basis);
	p.zRotation = false;
	return p;
}

namespace
{
// the tangent of one end of the segment `d`: its backward part along d removed, its length at most twice |d|
Coord3D limitTangent(Coord3D m, const Coord3D &d, float dd)
{
	const float along = m.x * d.x + m.y * d.y + m.z * d.z;
	if (along < 0.0f)
	{
		const float k = along / dd;
		m.x -= d.x * k;
		m.y -= d.y * k;
		m.z -= d.z * k;
	}
	const float mm = m.x * m.x + m.y * m.y + m.z * m.z;
	const float limit = 4.0f * dd; // (2 |d|)^2
	if (mm > limit)
	{
		const float k = std::sqrt(limit / mm);
		m.x *= k;
		m.y *= k;
		m.z *= k;
	}
	return m;
}
} // namespace

Pose smoothPose(const ObjectSnapshot &s, UnsignedInt snapshotFrame, double alpha)
{
	if (s.projectile)
	{
		return retailPose(s, snapshotFrame, alpha);
	}
	Pose p = retailPose(s, snapshotFrame, alpha); // the stale snap and the rotation are retail's
	if (s.lastMovedFrame < snapshotFrame + 1u - 2u)
	{
		return p;
	}
	const Coord3D &p0 = s.previousPos, &p1 = s.recordedPos, &p2 = s.position;
	const Coord3D d{ p2.x - p1.x, p2.y - p1.y, p2.z - p1.z };
	const float dd = d.x * d.x + d.y * d.y + d.z * d.z;
	if (!(dd > 0.0f))
	{
		p.position = p2;
		return p;
	}
	Coord3D p3{ p2.x + d.x, p2.y + d.y, p2.z + d.z };
	if (s.hasNext && !(s.nextPos.x == p1.x && s.nextPos.y == p1.y))
	{
		p3 = s.nextPos;
	}
	const Coord3D m1 = limitTangent(Coord3D{ (p2.x - p0.x) * 0.5f, (p2.y - p0.y) * 0.5f, (p2.z - p0.z) * 0.5f }, d, dd);
	const Coord3D m2 = limitTangent(Coord3D{ (p3.x - p1.x) * 0.5f, (p3.y - p1.y) * 0.5f, (p3.z - p1.z) * 0.5f }, d, dd);
	const float t = clampAlpha(alpha);
	const float t2 = t * t, t3 = t2 * t;
	const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f, h10 = t3 - 2.0f * t2 + t, h01 = -2.0f * t3 + 3.0f * t2, h11 = t3 - t2;
	p.position.x = h00 * p1.x + h10 * m1.x + h01 * p2.x + h11 * m2.x;
	p.position.y = h00 * p1.y + h10 * m1.y + h01 * p2.y + h11 * m2.y;
	p.position.z = h00 * p1.z + h10 * m1.z + h01 * p2.z + h11 * m2.z;
	return p;
}

std::vector<std::string> stopLines()
{
	return {
		"[S-810] frame pacing: the logic runs on a worker thread and no client code reads live objects while it runs a frame: the drawables, the radar and the "
		"camera's follow target come from the presented snapshot, the HUD's per-frame update and its input wait for an idle render frame (skipped / "
		"deferred, never blocking), its bound queries and commands wait for the worker. Open: the HUD's selection, control bar and picking still read the "
		"completed live state at those idle points instead of a snapshot. Lockstep (MP-1 integration, review r4): with a frame driver the protocol owner (the "
		"advance thread) pumps the transport on every call, stamps local input once against the protocol frame, acquires complete numbered batches in order "
		"(after the load barrier, every slot's frame data in, zero-command batches included) into a bounded FIFO; the worker runs batch N only as logic frame "
		"N and publishes frame N + 1's hash / CRC breakdown / RNG with its snapshot; render-clock caps drop clock time, never a batch",
		"[S-811] presentation beyond retail (SMOOTH-1): the render rate is uncapped (retail: UseFPSLimit, FramesPerSecondLimit 30) and the client alpha is the "
		"continuous fraction of the logic frame (retail: the engine tick's phase / 6, RW 0x63256F); the drawn pose itself is retail's (RW 0x6765B9: Catmull-Rom, "
		"slerp, the stale snap)",
		"[S-813] the teleport form of setPosition / setTransform (RW 0x696E63 / 0x68DA67: recordTransform twice, then the drawable's gather RW 0x674B1F(1)) is "
		"not ported: an object moved by such a call is drawn interpolated across the jump for one frame",
		"[S-814] audio beside the logic worker: the logic's audio calls (unit voices, weapon fire sounds, Lua object sounds) made on the worker are queued and "
		"run on the audio owner at the next worker-idle point (AudioApi::drainDeferred), in call order; a deferred playSoundForObject returns no handle "
		"(AHSV_Error) to its logic caller; the audio manager follows its owners through the drawables and the latest snapshot",
		"[S-815] effects beside the logic worker: retail plays the logic's FX calls inside the frame; here the call captures what the effect reads of its "
		"objects (position, transform, conditions, owner, relationship, kinds, radius, drawable, the list's object filters) and the render side plays it at "
		"the next worker-idle point; the fire FX bone, the attached systems and the particles step there too (render time of busy frames carried over)",
		"[S-1140] presentation beyond retail (SMOOTH-2): the drawn position is RenderInterpolation::smoothPose, not retail's Catmull-Rom (RW 0x6765B9), which "
		"slows a member without a path to a stop at every logic frame's end (its pending position is the pre-move one): without a look-ahead the step is "
		"extrapolated, a still segment is held, the tangents never point back or exceed twice the step; the presented time is continuous and a late "
		"worker lengthens the presentation delay (at most +70 ms, decaying) instead of a hold and a jump",
	};
}

} // namespace RenderInterpolation
