// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the parts of ZH Libraries/Source/WWVegas/WWMath/quat.h / quat.cpp that animation
// channels need: the Quaternion, Fast_Slerp (the C path of quat.cpp:441-478, the one compiled when
// the inline-asm block is disabled) and BFME2's normalised lerp.
//
// Fast_Slerp in ZH calls WWMath::Fast_Acos / Fast_Sin, which are table approximations. They are
// replaced here by std::acos / std::sin: the tables are interpolation accelerators with a documented
// error budget, not part of any data format, and nothing in the sim hashes animation output.

#pragma once

#include "Common/NumericState.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#define WWMATH_EPSILON 0.0001f

struct Quaternion
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float W = 1.0f;

	Quaternion() = default;
	Quaternion(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}

	void Set(float x, float y, float z, float w)
	{
		X = x;
		Y = y;
		Z = z;
		W = w;
	}
	void Make_Identity() { Set(0.0f, 0.0f, 0.0f, 1.0f); }
	float Length2() const { return X * X + Y * Y + Z * Z + W * W; }
};

// ZH quat.cpp Fast_Slerp, C version.
inline void Fast_Slerp(Quaternion &res, const Quaternion &p, const Quaternion &q, float alpha)
{
	float beta;
	float cos_t = p.X * q.X + p.Y * q.Y + p.Z * q.Z + p.W * q.W;

	// if q is on the opposite hemisphere from p, use -q instead
	bool qflip;
	if (cos_t < 0.0f)
	{
		cos_t = -cos_t;
		qflip = true;
	}
	else
	{
		qflip = false;
	}

	if (1.0f - cos_t < WWMATH_EPSILON * WWMATH_EPSILON)
	{
		// q is very close to p: interpolate linearly
		beta = 1.0f - alpha;
	}
	else
	{
		float theta = std::acos(cos_t);
		float sin_t = std::sin(theta);
		float oo_sin_t = 1.0f / sin_t;
		beta = std::sin(theta - alpha * theta) * oo_sin_t;
		alpha = std::sin(alpha * theta) * oo_sin_t;
	}

	if (qflip)
	{
		alpha = -alpha;
	}

	res.X = beta * p.X + alpha * q.X;
	res.Y = beta * p.Y + alpha * q.Y;
	res.Z = beta * p.Z + alpha * q.Z;
	res.W = beta * p.W + alpha * q.W;
}

// NumericState::pc24Mul / pc24Sub (the numeric facade) emulate "operate at x87 PC24 with a wide exponent, then store as float32" with exact
// integer arithmetic, which costs about 100 ns an operation. For a result in the normal float range that is the same as one IEEE float
// operation (one rounding of the exact result to 24 bits, nothing left to round at the store), so the float result is taken directly and
// only results near or outside the float range (subnormal, overflow, NaN, or a zero that is not exact) go through the facade. The
// equivalence is pinned bit for bit by test_w3d_pose.cpp ("pc24 fast path ...").
inline float BFME2_Mul24(float a, float b)
{
	const float r = a * b;
	const float m = std::fabs(r);
	if ((m >= 1.17549435e-38f && m <= 3.40282347e+38f) || (r == 0.0f && (a == 0.0f || b == 0.0f)))
	{
		return r;
	}
	return NumericState::pc24Mul(a, b);
}

inline float BFME2_Sub24(float a, float b)
{
	const float r = a - b;
	const float m = std::fabs(r);
	if ((m >= 1.17549435e-38f && m <= 3.40282347e+38f) || (r == 0.0f && a == b && a == a))
	{
		return r;
	}
	return NumericState::pc24Sub(a, b);
}

// BFME2 retail 0x0044233A (FUN_0044233a, 1.06 game.dat): the approximate inverse square root WWMath::Inv_Sqrt resolves to, called by the
// nlerp below. Target facts: y0 = bits((0xBE6EB508 - bits(x)) >> 1) as a float, xh = bits(bits(x) - 0x800000) (= x / 2); then two Newton
// steps on the x87 stack, in this operation order (verified against the real function through tools/retail_oracle, see test_w3d_pose.cpp):
//   a = (y0 * y0) * xh; b = 1.5 - a; c = (a * b) * b; d = 1.5 - c; r = ((y0 * b) * d) * (1.5 - (c * d) * d)
// Retail runs with the x87 precision control at 24 bits (setFPMode, RW 0x440809), so every operation rounds to a 24-bit significand
// exactly as NumericState::pc24* does (numeric facade, PLAN rule 3).
inline float BFME2_Inverse_Sqrt(float x)
{
	std::uint32_t bits;
	std::memcpy(&bits, &x, 4);
	const std::uint32_t yb = (0xBE6EB508u - bits) >> 1;
	const std::uint32_t hb = bits - 0x800000u;
	float y0, xh;
	std::memcpy(&y0, &yb, 4);
	std::memcpy(&xh, &hb, 4);
	const float a = BFME2_Mul24(BFME2_Mul24(y0, y0), xh);
	const float b = BFME2_Sub24(1.5f, a);
	const float c = BFME2_Mul24(BFME2_Mul24(a, b), b);
	const float y0b = BFME2_Mul24(y0, b);
	const float d = BFME2_Sub24(1.5f, c);
	const float cd = BFME2_Mul24(c, d);
	const float y0bd = BFME2_Mul24(y0b, d);
	const float e = BFME2_Sub24(1.5f, BFME2_Mul24(cd, d));
	return BFME2_Mul24(y0bd, e);
}

// BFME2 retail 0x00717550 (Open-BFME-2 Code/Libraries/Source/WWVegas/WWMath/bfme2_quaternion_nlerp.cpp, FUN_00b17550): shortest-path
// linear blend in single precision (SSE), then normalise with BFME2_Inverse_Sqrt. The dot product is summed X, Z, Y, W as in the binary,
// the squared length X, Y, Z, W. The blend is taken when the dot product is >= 0 or unordered (comiss / jbe).
inline void BFME2_Nlerp(Quaternion &res, const Quaternion &p, const Quaternion &q, float alpha)
{
	float dot = p.X * q.X + p.Z * q.Z + p.Y * q.Y + p.W * q.W;
	float beta = 1.0f - alpha;
	if (dot < 0.0f)
	{
		res.X = beta * p.X - alpha * q.X;
		res.Y = beta * p.Y - alpha * q.Y;
		res.Z = beta * p.Z - alpha * q.Z;
		res.W = beta * p.W - alpha * q.W;
	}
	else
	{
		res.X = beta * p.X + alpha * q.X;
		res.Y = beta * p.Y + alpha * q.Y;
		res.Z = beta * p.Z + alpha * q.Z;
		res.W = beta * p.W + alpha * q.W;
	}
	float length = res.X * res.X + res.Y * res.Y + res.Z * res.Z + res.W * res.W;
	if (0.0f != length)
	{
		const float scale = BFME2_Inverse_Sqrt(length);
		res.X = BFME2_Mul24(scale, res.X);
		res.Y = BFME2_Mul24(scale, res.Y);
		res.Z = BFME2_Mul24(scale, res.Z);
		res.W = BFME2_Mul24(scale, res.W);
	}
}

inline Matrix3D Build_Matrix3D(const Quaternion &q)
{
	return Build_Matrix3D(q.X, q.Y, q.Z, q.W);
}
