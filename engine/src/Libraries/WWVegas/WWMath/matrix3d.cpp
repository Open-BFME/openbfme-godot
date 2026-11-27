// OpenBFME. GPL-3.0.
//
// WWMath Matrix3D rotations (lane LOCAL-25, #430): the bodies of Rotate_X / Rotate_Y / Rotate_Z, used by the drawable's presentation physics
// (GameClient/DrawablePhysics.cpp). CLIENT ONLY. They moved here unchanged from matrix3d.h, a header the sim audit scans through the
// simulation sources that include it.

#include "matrix3d.h"

#include <cmath>

// this = this * Rotate_X(theta) (ZH/RotWK Matrix3D::Rotate_X, RW 0x00A3CEF0, DONOR BFME2 0x0027BB65)
void Matrix3D::Rotate_X(float theta)
{
	float s = std::sin(theta);
	float c = std::cos(theta);
	for (int r = 0; r < 3; ++r)
	{
		float tmp1 = Row[r][1];
		float tmp2 = Row[r][2];
		Row[r][1] = c * tmp1 + s * tmp2;
		Row[r][2] = -s * tmp1 + c * tmp2;
	}
}

// this = this * Rotate_Y(theta) (ZH/RotWK Matrix3D::Rotate_Y, RW 0x00A3CF90, DONOR BFME2 0x0027BB65)
void Matrix3D::Rotate_Y(float theta)
{
	float s = std::sin(theta);
	float c = std::cos(theta);
	for (int r = 0; r < 3; ++r)
	{
		float tmp1 = Row[r][0];
		float tmp2 = Row[r][2];
		Row[r][0] = c * tmp1 - s * tmp2;
		Row[r][2] = s * tmp1 + c * tmp2;
	}
}

// this = this * Rotate_Z(theta) (ZH/RotWK Matrix3D::Rotate_Z, RW 0x00A3D030, DONOR BFME2 0x0027BB65)
void Matrix3D::Rotate_Z(float theta)
{
	float s = std::sin(theta);
	float c = std::cos(theta);
	for (int r = 0; r < 3; ++r)
	{
		float tmp1 = Row[r][0];
		float tmp2 = Row[r][1];
		Row[r][0] = c * tmp1 + s * tmp2;
		Row[r][1] = -s * tmp1 + c * tmp2;
	}
}
