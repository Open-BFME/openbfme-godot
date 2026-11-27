// OpenBFME. GPL-3.0.
//
// DrawableSway: the drawable's per-drawable instance matrix (RW 0x006714C0 / 0x008CD70C) and the call that steps its SwayClientUpdate modules (RW 0x008CD675).
// CLIENT ONLY: it reads the drawable's transform and the presented snapshot's breeze and writes only the drawable's instance matrix; it draws from the client
// random (W3DDrawRandom) and never reaches the simulation (tools/sim/sim_policy.json excludes it).

#include "GameClient/Drawable.h"
#include "GameClient/SwayClientUpdate.h"

#include <cmath>

void Drawable::setInstanceMatrix(const Matrix3D &xfrm)
{
	m_instanceMatrix = xfrm;
	m_hasInstanceMatrix = true;
}

// RW 0x00B261F0 Matrix3D::Get_Z_Rotation on the drawable's world transform
float Drawable::getZRotation() const
{
	const float *b = getBasis();
	return std::atan2(b[3], b[0]);
}

// RW 0x008CD675: every SwayClientUpdate module of the drawable takes one client step
void Drawable::updateSway(const W3DVegetationSway::Breeze &breeze, bool visible, bool burnedOrDead)
{
	for (const auto &m : m_clientModules)
	{
		if (auto *sway = dynamic_cast<SwayClientUpdate *>(m.get()))
		{
			sway->clientUpdate(breeze, m_services.random, visible, burnedOrDead);
		}
	}
}

// The model's world transform when the drawable carries an instance matrix (RW 0x006714C0): the object's transform (basis, entry position) times the instance
// matrix, scaled; false (outMatrix untouched) when it carries none
bool Drawable::instanceMatrixTransform(const DrawEntry &e, Matrix3D &outMatrix) const
{
	if (!m_hasInstanceMatrix)
	{
		return false;
	}
	const float *basis = getBasis();
	const Coord3D pos = entryPosition(e);
	Matrix3D world;
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			world.Row[r][c] = basis[r * 3 + c];
		}
	}
	world.Row[0][3] = pos.x;
	world.Row[1][3] = pos.y;
	world.Row[2][3] = pos.z;
	Matrix3D::Multiply(world, m_instanceMatrix, &outMatrix);
	const float scale = getInstanceScale();
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			outMatrix.Row[r][c] *= scale;
		}
	}
	return true;
}
