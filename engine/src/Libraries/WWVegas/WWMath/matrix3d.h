// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Minimal port of ZH Libraries/Source/WWVegas/WWMath matrix3d.h / quat.h: a 3x4 affine
// matrix stored as three rows (rotation | translation), W3D's right-handed Z-up space.
// Only what HTreeClass and skin deformation need so far.

#pragma once

struct Vector3
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;

	Vector3() = default;
	Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
};

class Matrix3D
{
public:
	float Row[3][4];

	Matrix3D() { Make_Identity(); }

	void Make_Identity()
	{
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				Row[r][c] = (r == c) ? 1.0f : 0.0f;
			}
		}
	}

	// this = this * Translation(t)  (ZH Matrix3D::Translate)
	void Translate(const Vector3 &t)
	{
		for (int r = 0; r < 3; ++r)
		{
			Row[r][3] += Row[r][0] * t.X + Row[r][1] * t.Y + Row[r][2] * t.Z;
		}
	}

	void Set_Translation(const Vector3 &t)
	{
		Row[0][3] = t.X;
		Row[1][3] = t.Y;
		Row[2][3] = t.Z;
	}

	Vector3 Get_Translation() const { return Vector3(Row[0][3], Row[1][3], Row[2][3]); }

	// res = a * b
	static void Multiply(const Matrix3D &a, const Matrix3D &b, Matrix3D *res)
	{
		Matrix3D out;
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				float v = a.Row[r][0] * b.Row[0][c] + a.Row[r][1] * b.Row[1][c] + a.Row[r][2] * b.Row[2][c];
				if (c == 3)
				{
					v += a.Row[r][3];
				}
				out.Row[r][c] = v;
			}
		}
		*res = out;
	}

	// this = this * m  (ZH Matrix3D::postMul)
	void postMul(const Matrix3D &m)
	{
		Multiply(*this, m, this);
	}

	Vector3 Transform_Point(const Vector3 &v) const
	{
		return Vector3(Row[0][0] * v.X + Row[0][1] * v.Y + Row[0][2] * v.Z + Row[0][3],
			Row[1][0] * v.X + Row[1][1] * v.Y + Row[1][2] * v.Z + Row[1][3],
			Row[2][0] * v.X + Row[2][1] * v.Y + Row[2][2] * v.Z + Row[2][3]);
	}

	Vector3 Rotate_Vector(const Vector3 &v) const
	{
		return Vector3(Row[0][0] * v.X + Row[0][1] * v.Y + Row[0][2] * v.Z,
			Row[1][0] * v.X + Row[1][1] * v.Y + Row[1][2] * v.Z,
			Row[2][0] * v.X + Row[2][1] * v.Y + Row[2][2] * v.Z);
	}
};

// ZH quat.cpp Build_Matrix3D(const Quaternion&, Matrix3D&): rotation part from (x,y,z,w),
// translation zero.
inline Matrix3D Build_Matrix3D(float x, float y, float z, float w)
{
	Matrix3D m;
	m.Row[0][0] = 1.0f - 2.0f * (y * y + z * z);
	m.Row[0][1] = 2.0f * (x * y - z * w);
	m.Row[0][2] = 2.0f * (z * x + y * w);
	m.Row[0][3] = 0.0f;
	m.Row[1][0] = 2.0f * (x * y + z * w);
	m.Row[1][1] = 1.0f - 2.0f * (z * z + x * x);
	m.Row[1][2] = 2.0f * (y * z - x * w);
	m.Row[1][3] = 0.0f;
	m.Row[2][0] = 2.0f * (z * x - y * w);
	m.Row[2][1] = 2.0f * (y * z + x * w);
	m.Row[2][2] = 1.0f - 2.0f * (y * y + x * x);
	m.Row[2][3] = 0.0f;
	return m;
}
