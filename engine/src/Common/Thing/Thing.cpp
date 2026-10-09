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

void Thing::setOrientation(float angle)
{
	const Coord3D old = m_position;
	const float oldAngle = m_angle;
	m_angle = angle;
	const float c = SimMath::cosf32(angle);
	const float s = SimMath::sinf32(angle);
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
