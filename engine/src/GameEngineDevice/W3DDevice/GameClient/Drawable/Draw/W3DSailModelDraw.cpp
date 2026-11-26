// OpenBFME. GPL-3.0. See W3DSailModelDraw.h.
// TARGET RW 0x004CA6F0 loadPostProcess, RW 0x004CFFBE getModuleNameKey, RW 0x004D0003 GetSnapshotName,
// RW 0x004D0009 xfer, RW 0x004D00AC scalar deleting dtor.
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DSailModelDraw.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <algorithm>
#include <cmath>

namespace
{
// RW 0x644FD0 (normalizeAngle): (-pi, pi]; in the sail callback the input is
// acos(clamped dot) or its negative, so one wrap suffices for either branch.
float normalizeAngle(float angle)
{
	constexpr float pi = 3.1415927410125732f;
	constexpr float twoPi = 6.2831854820251465f;
	if (angle > pi)
	{
		angle -= twoPi;
	}
	if (angle <= -pi)
	{
		angle += twoPi;
	}
	return angle;
}
} // namespace

W3DSailModelDraw::W3DSailModelDraw(const W3DSailModelDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random,
	W3DDrawScriptHost *scripts, const Options &options)
	: W3DScriptedModelDraw(data, assets, random, scripts, options)
	, m_sailData(data)
{
}

void W3DSailModelDraw::adjustAttachedTransform(Matrix3D &matrix)
{
	// RW 0x4D00DF..0x4D0119: (0.5, 0.5, 0), normalized on each call;
	// no weather/wind global is read. X and Y are *columns* of the incoming matrix.
	constexpr float wind = 0.7071067811865475244f;
	const float xDot = wind * matrix.Row[0][0] + wind * matrix.Row[1][0];
	const float yDot = wind * matrix.Row[0][1] + wind * matrix.Row[1][1];
	float target = normalizeAngle(static_cast<float>(std::acos(std::clamp(xDot, -1.0f, 1.0f)))); // RW 0x4D0184..0x4D01D1
	if (target > m_sailData.m_maxRotation) // RW 0x4D01D4..0x4D01EA: MaxRotationDegrees is already in radians
	{
		target = m_sailData.m_maxRotation;
	}
	if (yDot < 0.0f) // RW 0x4D01EF..0x4D020B: sign AFTER the max clamp
	{
		target = normalizeAngle(-target);
	}
	// RW 0x4D020C..0x4D0265: the first call skips damping; subsequent calls
	// step by AboutDamping per *draw*, not per second and not by the shortest arc.
	if (m_hasLastAngle && std::fabs(target - m_lastAngle) > m_sailData.m_aboutDamping)
	{
		target = m_lastAngle + (target < m_lastAngle ? -m_sailData.m_aboutDamping : m_sailData.m_aboutDamping);
	}
	m_lastAngle = target;

	// RW 0x4D0265..0x4D0330: WWMath Matrix3D::Rotate_Z postmultiplies
	// all three basis rows by the local Z turn, leaving the translation alone.
	const float c = std::cos(target), s = std::sin(target);
	for (int r = 0; r < 3; ++r)
	{
		const float x = matrix.Row[r][0], y = matrix.Row[r][1];
		matrix.Row[r][0] = c * x + s * y;
		matrix.Row[r][1] = -s * x + c * y;
	}
	m_hasLastAngle = true;
}

void W3DSailModelDraw::xfer(Xfer &x)
{
	// RW 0x004D0009 (DONOR BFME2 0x000D0816): W3DScriptedModelDraw::xfer, version(1, 1), xferReal(&m_lastAngle), xferBool(&m_hasLastAngle)
	SnapshotXfer::version(x, 1, 1);
	x.xferReal(&m_lastAngle);
	x.xferBool(&m_hasLastAngle);
}
