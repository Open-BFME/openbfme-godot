// OpenBFME. GPL-3.0. See WaterReflection.h for the RotWK addresses and the BFME2 donor files.

#include "GameClient/WaterReflection.h"

#include <cmath>

namespace WaterReflection
{
namespace
{
Vector3 reflectAxis(const Vector3 &v, float planeNormalZ)
{
	// axis - (2.0f * Dot(axis, n) * n) with n = (0, 0, 1): only z changes
	const float axisDistance = v.Z * planeNormalZ;
	return Vector3(v.X, v.Y, v.Z - 2.0f * axisDistance * planeNormalZ);
}

float sgn(float v)
{
	return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}
} // namespace

bool mirrorCameraTransform(const Matrix3D &source, float planeZ, Matrix3D &out)
{
	if (!std::isfinite(source.Row[0][0]))
	{
		return false; // `if (!_finite(source->Get_Transform().m[0][0])) return;`
	}
	const float nz = 1.0f; // waterNormal(0, 0, 1)
	const Vector3 right(source.Row[0][0], source.Row[1][0], source.Row[2][0]);
	const Vector3 up(source.Row[0][1], source.Row[1][1], source.Row[2][1]);
	const Vector3 back(source.Row[0][2], source.Row[1][2], source.Row[2][2]);
	const Vector3 position(source.Row[0][3], source.Row[1][3], source.Row[2][3]);
	const Vector3 rRight = reflectAxis(right, nz);
	const Vector3 rUp = reflectAxis(up, nz);
	const Vector3 rN = reflectAxis(back, nz);
	float axisDistance = position.Z * nz;
	axisDistance -= planeZ;
	const Vector3 rPos(position.X, position.Y, position.Z - 2.0f * axisDistance * nz);
	// Matrix3D reflected(rRight, -rUp, rN, rPos)
	out.Row[0][0] = rRight.X; out.Row[0][1] = -rUp.X; out.Row[0][2] = rN.X; out.Row[0][3] = rPos.X;
	out.Row[1][0] = rRight.Y; out.Row[1][1] = -rUp.Y; out.Row[1][2] = rN.Y; out.Row[1][3] = rPos.Y;
	out.Row[2][0] = rRight.Z; out.Row[2][1] = -rUp.Z; out.Row[2][2] = rN.Z; out.Row[2][3] = rPos.Z;
	return true;
}

bool invert(const Mat4 &a, Mat4 &out)
{
	double m[4][8];
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			m[r][c] = a.m[r][c];
			m[r][4 + c] = r == c ? 1.0 : 0.0;
		}
	}
	for (int col = 0; col < 4; ++col)
	{
		int pivot = col;
		for (int r = col + 1; r < 4; ++r)
		{
			if (std::fabs(m[r][col]) > std::fabs(m[pivot][col]))
			{
				pivot = r;
			}
		}
		if (m[pivot][col] == 0.0)
		{
			return false;
		}
		if (pivot != col)
		{
			for (int c = 0; c < 8; ++c)
			{
				const double t = m[col][c];
				m[col][c] = m[pivot][c];
				m[pivot][c] = t;
			}
		}
		const double inv = 1.0 / m[col][col];
		for (int c = 0; c < 8; ++c)
		{
			m[col][c] *= inv;
		}
		for (int r = 0; r < 4; ++r)
		{
			if (r == col)
			{
				continue;
			}
			const double f = m[r][col];
			if (f == 0.0)
			{
				continue;
			}
			for (int c = 0; c < 8; ++c)
			{
				m[r][c] -= f * m[col][c];
			}
		}
	}
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			out.m[r][c] = (float)m[r][4 + c];
		}
	}
	return true;
}

void transform(const float v[4], const Mat4 &m, float out[4])
{
	for (int c = 0; c < 4; ++c)
	{
		out[c] = v[0] * m.m[0][c] + v[1] * m.m[1][c] + v[2] * m.m[2][c] + v[3] * m.m[3][c];
	}
}

bool obliqueClipProjection(const Mat4 &d3dView, const Mat4 &d3dProjection, float planeZ, Mat4 &out)
{
	// inverse.m[i][j] = render_state.view.m[j][i], then D3DXMatrixInverse: the plane (a covector) moves to view space by the inverse transpose
	Mat4 transposed;
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			transposed.m[r][c] = d3dView.m[c][r];
		}
	}
	Mat4 inverse;
	if (!invert(transposed, inverse))
	{
		return false;
	}
	const float plane[4] = { 0.0f, 0.0f, 1.0f, -planeZ }; // Vector4 plane(WaterPlane.X, WaterPlane.Y, WaterPlane.Z, -WaterPlane.W)
	float clip[4];
	transform(plane, inverse, clip);
	Mat4 projection = d3dProjection;
	if (projection.m[0][0] == 0.0f || projection.m[1][1] == 0.0f)
	{
		return false;
	}
	float q[4];
	q[0] = (sgn(clip[0]) - projection.m[2][0]) / projection.m[0][0];
	q[1] = (sgn(clip[1]) - projection.m[2][1]) / projection.m[1][1];
	q[2] = 1.0f;
	q[3] = (1.0f - projection.m[2][2]) / projection.m[3][2];
	const float scale = 1.0f / (clip[0] * q[0] + clip[1] * q[1] + clip[2] * q[2] + clip[3] * q[3]);
	projection.m[0][2] = clip[0] * scale;
	projection.m[1][2] = clip[1] * scale;
	projection.m[2][2] = clip[2] * scale;
	projection.m[3][2] = clip[3] * scale;
	out = projection;
	return true;
}

bool polygonInFrustum(const std::vector<Vector3> &polygon, const std::vector<Plane> &frustum)
{
	std::vector<Vector3> current = polygon;
	for (const Plane &p : frustum)
	{
		if (current.size() < 3)
		{
			return false;
		}
		std::vector<Vector3> next;
		const size_t count = current.size();
		for (size_t i = 0; i < count; ++i)
		{
			const Vector3 &a = current[i];
			const Vector3 &b = current[(i + 1) % count];
			const float da = p.n[0] * a.X + p.n[1] * a.Y + p.n[2] * a.Z + p.d;
			const float db = p.n[0] * b.X + p.n[1] * b.Y + p.n[2] * b.Z + p.d;
			if (da >= 0.0f)
			{
				next.push_back(a);
			}
			if ((da >= 0.0f) != (db >= 0.0f))
			{
				const float t = da / (da - db);
				next.push_back(Vector3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t));
			}
		}
		current.swap(next);
	}
	return current.size() >= 3;
}

void reflectionUv(float ndcX, float ndcY, float perturbation, const float scaledNormalXY[2], float out[2])
{
	out[0] = (ndcX + 1.0f) * 0.5f + perturbation * scaledNormalXY[0];
	out[1] = (ndcY + 1.0f) * 0.5f + perturbation * scaledNormalXY[1];
}
} // namespace WaterReflection
