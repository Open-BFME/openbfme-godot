// OpenBFME. GPL-3.0.
//
// The float arithmetic production's exit modules share (lane PROD-1): a model-space point transformed by the producer's matrix, and the natural rally
// point's offset along its own direction. Every operation goes through SimMath's binary32 names (PLAN rule 3), in the order of the binary
// (RW 0x88B2F0 natural rally point, 0x8A39CE / 0x8A3DD5 / 0x8A9DBA / 0x8A8F66 the create points: the association differs per axis and per site, the
// callers pass the products in the order the binary adds them).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace ExitMath
{

// Thing keeps the 3x3 as rows of the basis (basis[row * 3 + col] = Mrc) and the translation as the position
struct Mat
{
	float m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23;
	explicit Mat(const Object &o)
	{
		const float *b = o.getBasis();
		const Coord3D &p = *o.getPosition();
		m00 = b[0]; m01 = b[1]; m02 = b[2]; m03 = p.x;
		m10 = b[3]; m11 = b[4]; m12 = b[5]; m13 = p.y;
		m20 = b[6]; m21 = b[7]; m22 = b[8]; m23 = p.z;
	}
};

inline float mul(float a, float b) { return SimMath::mulf32(a, b); }
inline float add(float a, float b) { return SimMath::addf32(a, b); }
// ((t0 + t1) + t2) + t3
inline float chain4(float t0, float t1, float t2, float t3) { return add(add(add(t0, t1), t2), t3); }

// RW 0x88B2F0, the part before the matrix: with `offset` the point moves 20 units (2 * PATHFIND_CELL_SIZE_F, RW 0xBDBC6C) along its own direction
// (BFME2_Inverse_Sqrt of the squared length summed z, y, x), no offset or a zero vector leaves it
template <typename InverseSqrt>
inline void offsetRallyPoint(float &px, float &py, float &pz, bool offset, InverseSqrt inverseSqrt)
{
	if (!offset)
	{
		return;
	}
	const float lenSq = add(add(mul(pz, pz), mul(py, py)), mul(px, px));
	float nx = px, ny = py, nz = pz;
	if (lenSq != 0.0f)
	{
		const float inv = inverseSqrt(lenSq);
		nx = mul(px, inv);
		ny = mul(py, inv);
		nz = mul(pz, inv);
	}
	px = add(mul(nx, 20.0f), px);
	py = add(mul(ny, 20.0f), py);
	pz = add(mul(nz, 20.0f), pz);
}

// RW 0x88B2F0, the matrix part (the association of each axis as the binary adds it)
inline void transformRally(const Mat &m, float px, float py, float pz, Coord3D *out)
{
	out->x = chain4(mul(m.m02, pz), mul(m.m01, py), mul(m.m00, px), m.m03);
	out->y = chain4(mul(m.m10, px), mul(m.m12, pz), mul(m.m11, py), m.m13);
	out->z = chain4(mul(m.m21, py), mul(m.m20, px), mul(m.m22, pz), m.m23);
}

} // namespace ExitMath
