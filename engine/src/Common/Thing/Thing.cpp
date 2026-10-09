// OpenBFME. GPL-3.0.
// See Common/Thing/Thing.h.

#include "Common/Thing/Thing.h"

#include "GameLogic/SimMath.h"

#include <cstring>

void Thing::setPosition(const Coord3D *pos)
{
	const Coord3D old = m_position;
	m_position = *pos;
	reactToTransformChange(&old, m_angle);
}

namespace
{
// RW 0x644FD0 (the copy of GameLogic/Object/LocomotorMove.cpp's): wrap an angle into [-pi, pi] with two SSE loops
float wrapAngle(float a)
{
	const float pi = 3.14159274f, twoPi = 6.28318548f;
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
} // namespace

void Thing::setOrientation(float angle)
{
	const float c = SimMath::cosf32(angle);
	const float s = SimMath::sinf32(angle);
	setOrientationTrig(angle, c, s);
}

void Thing::setOrientationTrig(float angle, float c, float s)
{
	const Coord3D old = m_position;
	const float oldAngle = m_angle;
	// lane MOVE-2 r3: RW 0x70C31E stores the angle wrapped (RW 0x70C4D0 calls RW 0x644FD0, then obj + 0x44); the basis is built from the angle as passed. The port
	// stored it as passed: a horde turned toward its melee target every frame (RW 0x870A1B adds the relative angle to its orientation) reached 10 radians
	m_angle = wrapAngle(angle);
	// columns X = (c, s, 0), Y = (-s, c, 0), Z = (0, 0, 1); stored row major
	const float b[9] = { c, -s, 0.0f, s, c, 0.0f, 0.0f, 0.0f, 1.0f };
	std::memcpy(m_basis, b, sizeof(b));
	reactToTransformChange(&old, oldAngle);
}

void Thing::setTransform(const Coord3D *pos, const float basis[9])
{
	const Coord3D old = m_position;
	const float oldAngle = m_angle;
	m_position = *pos;
	std::memcpy(m_basis, basis, sizeof(m_basis));
	m_angle = (float)SimMath::atan2d(m_basis[3], m_basis[0]); // X axis = column 0: (b[0], b[3], b[6])
	reactToTransformChange(&old, oldAngle);
}
