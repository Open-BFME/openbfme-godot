// OpenBFME. GPL-3.0. See PristinePose.h (lane RENDER-2, review r1).
//
// Every floating operation below is one numeric facade call (SimMath / NumericState). The expressions are written with the small value type F whose
// operators forward to SimMath::sseAdd / sseSub / sseMul / sseDiv, so each line keeps the exact grouping of the htree.cpp / motchan.cpp / quat.h
// source it transcribes (C++ precedence and left-to-right associativity apply to the overloaded operators as they do to the float ones).

#include "GameLogic/Object/PristinePose.h"

#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WW3D2/hcanim.h"
#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <cstdint>
#include <cstring>

namespace
{
struct F
{
	float v;
	explicit F(float x) : v(x) {}
};
F operator+(F a, F b) { return F(SimMath::sseAdd(a.v, b.v)); }
F operator-(F a, F b) { return F(SimMath::sseSub(a.v, b.v)); }
F operator*(F a, F b) { return F(SimMath::sseMul(a.v, b.v)); }
F operator/(F a, F b) { return F(SimMath::sseDiv(a.v, b.v)); }

// ---- the adaptive delta filter table (ZH motchan.cpp:56-76) ---------------------------------------------------------------------------------
void fillFilterTable(float value[256])
{
	const PristinePose::CanonicalEnvironment canonical;
	static const float head[16] = {
		0.00000001f, 0.0000001f, 0.000001f, 0.00001f, 0.0001f, 0.001f, 0.01f, 0.1f,
		1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f, 1000000.0f, 10000000.0f,
	};
	for (int i = 0; i < 16; ++i)
	{
		value[i] = head[i];
	}
	// ratio = (float)i / 240; 1 - sin((90 * ratio) * (pi / 180)) with the float constant pi / 180 of the source (3.14159265f / 180.0f)
	const F degToRad = F(3.14159265358979323846f) / F(180.0f);
	for (int i = 0; i < 240; ++i)
	{
		const F ratio = F(SimMath::sseFromInt32(i)) / F(240.0f);
		const F angle = (F(90.0f) * ratio) * degToRad;
		value[i + 16] = (F(1.0f) - F(SimMath::sinDet(angle.v))).v;
	}
}

struct FilterTable
{
	float value[256];
	FilterTable() { fillFilterTable(value); }
};

const FilterTable &filterTable()
{
	static const FilterTable table;
	return table;
}

// ---- BFME2 nlerp (quat.h BFME2_Nlerp / BFME2_Inverse_Sqrt, RVA 0x00717550 / 0x0044233A) -----------------------------------------------------
float mul24(float a, float b) { return SimMath::pc24Mul(a, b); }
float sub24(float a, float b) { return SimMath::pc24Sub(a, b); }

float inverseSqrt(float x)
{
	std::uint32_t bits;
	std::memcpy(&bits, &x, 4);
	const std::uint32_t yb = (0xBE6EB508u - bits) >> 1;
	const std::uint32_t hb = bits - 0x800000u;
	float y0, xh;
	std::memcpy(&y0, &yb, 4);
	std::memcpy(&xh, &hb, 4);
	const float a = mul24(mul24(y0, y0), xh);
	const float b = sub24(1.5f, a);
	const float c = mul24(mul24(a, b), b);
	const float y0b = mul24(y0, b);
	const float d = sub24(1.5f, c);
	const float cd = mul24(c, d);
	const float y0bd = mul24(y0b, d);
	const float e = sub24(1.5f, mul24(cd, d));
	return mul24(y0bd, e);
}

void nlerp(float res[4], const float p[4], const float q[4], float alphaF)
{
	const F alpha(alphaF);
	const F dot = F(p[0]) * F(q[0]) + F(p[2]) * F(q[2]) + F(p[1]) * F(q[1]) + F(p[3]) * F(q[3]);
	const F beta = F(1.0f) - alpha;
	if (dot.v < 0.0f)
	{
		for (int i = 0; i < 4; ++i)
		{
			res[i] = (beta * F(p[i]) - alpha * F(q[i])).v;
		}
	}
	else
	{
		for (int i = 0; i < 4; ++i)
		{
			res[i] = (beta * F(p[i]) + alpha * F(q[i])).v;
		}
	}
	const F length = F(res[0]) * F(res[0]) + F(res[1]) * F(res[1]) + F(res[2]) * F(res[2]) + F(res[3]) * F(res[3]);
	if (0.0f != length.v)
	{
		const float scale = inverseSqrt(length.v);
		for (int i = 0; i < 4; ++i)
		{
			res[i] = mul24(scale, res[i]);
		}
	}
}

float lerp(float a, float b, float ratio) { return (F(a) + (F(b) - F(a)) * F(ratio)).v; } // motchan.cpp lerpf: a + (b - a) * ratio

// ---- channel values at an integer frame ------------------------------------------------------------------------------------------------------
struct Undefined
{
	std::string what;
};

// MotionChannelClass::Get_Vector / Get_Vector_As_Quat (raw): the stored value, the identity outside [First, Last] (fade 1)
void rawValue(const MotionChannelClass &c, int frame, float *out)
{
	const int n = c.Get_Vector_Length();
	if (frame < c.Get_First_Frame() || frame > c.Get_Last_Frame())
	{
		if (c.Get_Type() == ANIM_CHANNEL_Q)
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			out[3] = 1.0f;
		}
		else
		{
			out[0] = c.Get_Type() == ANIM_CHANNEL_FADE ? 1.0f : 0.0f;
		}
		return;
	}
	const std::vector<float> &d = c.Get_Data();
	for (int i = 0; i < n; ++i)
	{
		out[i] = d[(size_t)(frame - c.Get_First_Frame()) * (size_t)n + (size_t)i];
	}
}

// TimeCodedMotionChannelClass::Get_Vector / Get_QuatVector at `frame` (motchan.cpp timecodeOfFrame / index_for)
void timeCodedValue(const TimeCodedMotionChannelClass &c, int iframe, bool quat, float *out)
{
	const std::uint32_t mask = ~(std::uint32_t)W3D_TIMECODED_BINARY_MOVEMENT_FLAG;
	const std::uint32_t tc = iframe < 0 ? 0xFFFFFFFFu : (std::uint32_t)iframe;
	const int keys = c.Get_Num_Keys();
	if (tc < (c.Get_Key_Time_Code(0) & mask))
	{
		throw Undefined{ "a classic time-coded channel has no value before its first key (S-003)" };
	}
	std::uint32_t pidx = (std::uint32_t)keys - 1;
	if (tc < (c.Get_Key_Time_Code(keys - 1) & mask))
	{
		std::uint32_t lo = 0, hi = (std::uint32_t)keys - 1;
		while (hi - lo > 1)
		{
			const std::uint32_t mid = lo + (hi - lo) / 2;
			if (tc < (c.Get_Key_Time_Code((int)mid) & mask))
			{
				hi = mid;
			}
			else
			{
				lo = mid;
			}
		}
		pidx = lo;
	}
	const int n = c.Get_Vector_Length();
	float a[4] = { 0, 0, 0, 0 }, b[4] = { 0, 0, 0, 0 };
	c.Get_Key((int)pidx, a);
	if (pidx == (std::uint32_t)keys - 1 || (c.Get_Key_Time_Code((int)pidx + 1) & W3D_TIMECODED_BINARY_MOVEMENT_FLAG))
	{
		std::memcpy(out, a, sizeof(float) * (size_t)n);
		return;
	}
	c.Get_Key((int)pidx + 1, b);
	const F frame(SimMath::sseFromInt32(iframe));
	const F time1(SimMath::sseFromInt32((std::int32_t)(c.Get_Key_Time_Code((int)pidx) & mask)));
	const F time2(SimMath::sseFromInt32((std::int32_t)(c.Get_Key_Time_Code((int)pidx + 1) & mask)));
	const float ratio = ((frame - time1) / (time2 - time1)).v;
	if (quat)
	{
		nlerp(out, a, b, ratio);
		return;
	}
	for (int i = 0; i < n; ++i)
	{
		out[i] = lerp(a[i], b[i], ratio);
	}
}

// the adaptive delta decode (motchan.cpp AdaptiveDeltaMotionChannelClass::Load_W3D / BFME2MotionChannel::Load_W3D) of component `vi` up to `frame`
int nibbleDelta(const std::uint8_t *deltas, int fi)
{
	int factor = deltas[fi >> 1];
	if (fi & 1)
	{
		factor >>= 4;
	}
	factor &= 0xF;
	if (factor & 0x8)
	{
		factor |= (int)0xFFFFFFF0;
	}
	return factor;
}

float decodeDelta(const std::vector<std::uint8_t> &raw, size_t initialAt, size_t blocksAt, int components, int vi, int blockBytes, bool eightBit,
	float scale, std::uint32_t frame)
{
	const FilterTable &table = filterTable();
	float last;
	std::memcpy(&last, &raw[initialAt + (size_t)vi * 4], sizeof(float));
	for (std::uint32_t f = 1; f <= frame; ++f)
	{
		const std::uint32_t row = (f - 1) >> 4;
		const int fi = (int)((f - 1) & 15);
		const std::uint8_t *block = &raw[blocksAt + ((size_t)row * (size_t)components + (size_t)vi) * (size_t)blockBytes];
		F filter = F(table.value[block[0]]) * F(scale);
		F ffactor(0.0f);
		if (!eightBit)
		{
			ffactor = F(SimMath::sseFromInt32(nibbleDelta(block + 1, fi)));
		}
		else
		{
			const std::int8_t d = (std::int8_t)(block[1 + fi] ^ 0x80);
			ffactor = F(SimMath::sseFromInt32(d));
			filter = filter * F(0.0625f); // 1 / 16, exact
		}
		const F delta = ffactor * filter;
		last = (F(last) + delta).v;
	}
	return last;
}

// motchan.cpp streamFrames for an integer frame >= 0: ratio = frame - (float)(int)frame, f1 clamped to the last frame, f2 = f1 + 1 clamped
void streamFrames(int iframe, std::uint32_t count, std::uint32_t &f1, std::uint32_t &f2, float &ratio)
{
	const F frame(SimMath::sseFromInt32(iframe));
	ratio = (frame - F(SimMath::sseFromInt32(iframe))).v;
	const std::uint32_t u = (std::uint32_t)iframe;
	f1 = u >= count ? count - 1 : u;
	f2 = f1 + 1 >= count ? count - 1 : f1 + 1;
}

void adaptiveValue(const AdaptiveDeltaMotionChannelClass &c, int iframe, bool quat, float *out)
{
	const int n = c.Get_Vector_Length();
	std::uint32_t f1, f2;
	float ratio;
	streamFrames(iframe, (std::uint32_t)c.Get_Num_Keys(), f1, f2, ratio);
	float a[4], b[4];
	for (int vi = 0; vi < n; ++vi)
	{
		a[vi] = decodeDelta(c.Get_Packed(), 0, (size_t)n * 4, n, vi, 9, false, c.Get_Scale(), f1);
		b[vi] = decodeDelta(c.Get_Packed(), 0, (size_t)n * 4, n, vi, 9, false, c.Get_Scale(), f2);
	}
	if (quat)
	{
		nlerp(out, a, b, ratio);
		return;
	}
	for (int i = 0; i < n; ++i)
	{
		out[i] = lerp(a[i], b[i], ratio);
	}
}

void motionValue(const BFME2MotionChannel &c, int iframe, bool quat, float *out)
{
	const int n = c.Get_Components();
	const std::uint32_t count = (std::uint32_t)c.Get_Count();
	float a[4] = { 0, 0, 0, 0 }, b[4] = { 0, 0, 0, 0 };
	float factor;
	if (c.Get_Encoding() != BFME2MotionChannel::ENCODING_TIMECODED)
	{
		if (iframe <= -1)
		{
			throw Undefined{ "a motion stream channel has no value below frame 0 (S-004)" };
		}
		std::uint32_t f1, f2;
		streamFrames(iframe, count, f1, f2, factor);
		const std::vector<std::uint8_t> &raw = c.Get_Packed();
		float scale;
		std::memcpy(&scale, raw.data(), 4);
		const bool eight = c.Get_Encoding() == BFME2MotionChannel::ENCODING_ADAPTIVE_DELTA_8;
		const int blockBytes = 1 + (eight ? 16 : 8);
		for (int vi = 0; vi < n; ++vi)
		{
			a[vi] = decodeDelta(raw, 4, 4 + (size_t)n * 4, n, vi, blockBytes, eight, scale, f1);
			b[vi] = decodeDelta(raw, 4, 4 + (size_t)n * 4, n, vi, blockBytes, eight, scale, f2);
		}
	}
	else
	{
		// FindIndex (0x001B2EFC) with time = (int)frame, unsigned
		const std::uint32_t time = iframe < 0 ? 0xFFFFFFFFu : (std::uint32_t)iframe;
		const std::uint32_t mask = 0x7FFFu;
		int index;
		if (time <= (c.Get_Key_Time_Code(0) & mask))
		{
			index = 0;
		}
		else if (time >= (c.Get_Key_Time_Code((int)count - 1) & mask))
		{
			index = (int)count - 1;
		}
		else
		{
			int low = 0, high = (int)count - 2;
			for (;;)
			{
				const int mid = (low + high) / 2;
				if (time < (c.Get_Key_Time_Code(mid) & mask))
				{
					high = mid;
				}
				else if (time >= (c.Get_Key_Time_Code(mid + 1) & mask))
				{
					low = (low ^ mid) ? mid : low + 1;
				}
				else
				{
					index = mid;
					break;
				}
			}
		}
		c.Get_Key(index, a);
		if (index == (int)count - 1 || (c.Get_Key_Time_Code(index + 1) & 0x8000u))
		{
			std::memcpy(out, a, sizeof(float) * (size_t)n);
			return;
		}
		c.Get_Key(index + 1, b);
		const F frame(SimMath::sseFromInt32(iframe));
		const F t0(SimMath::sseFromInt32((std::int32_t)(c.Get_Key_Time_Code(index) & mask)));
		const F t1(SimMath::sseFromInt32((std::int32_t)(c.Get_Key_Time_Code(index + 1) & mask)));
		factor = ((frame - t0) / (t1 - t0)).v;
	}
	if (quat)
	{
		nlerp(out, a, b, factor);
		return;
	}
	for (int i = 0; i < n; ++i)
	{
		out[i] = lerp(a[i], b[i], factor);
	}
}

// ---- the composition (htree.cpp composeBase / applyTranslation / applyRotation / matrixOf) -------------------------------------------------------
struct QT
{
	float q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	float t[3] = { 0.0f, 0.0f, 0.0f };
};

enum BaseVariant { BASE_U, BASE_A, BASE_B };

void composeBase(BaseVariant v, const QT &P, const Quaternion &bq, const Vector3 &bt, QT &W)
{
	const F Px(P.q[0]), Py(P.q[1]), Pz(P.q[2]), Pw(P.q[3]);
	const F PT0(P.t[0]), PT1(P.t[1]), PT2(P.t[2]);
	const F bx(bq.X), by(bq.Y), bz(bq.Z), bw(bq.W);
	const F tx(bt.X), ty(bt.Y), tz(bt.Z);
	const F zero(0.0f);
	F ax(0.0f), ay(0.0f), az(0.0f), aw(0.0f), Tx(0.0f), Ty(0.0f), Tz(0.0f), Wx(0.0f), Wy(0.0f), Wz(0.0f), Ww(0.0f);
	if (v == BASE_A)
	{
		ax = (Py * tz - Pz * ty) + tx * Pw;
		ay = Pw * ty - (Px * tz - tx * Pz);
		az = (Px * ty - Py * tx) + tz * Pw;
		aw = zero - (tz * Pz + Px * tx + Py * ty);
		Tx = PT0 + ((Py * az - Pz * ay) + (Pw * ax - Px * aw));
		Ty = PT1 + ((Pw * ay - Py * aw) - (Px * az - Pz * ax));
		Tz = PT2 + ((Px * ay - Py * ax) + (Pw * az - Pz * aw));
		Wx = (bz * Py - Pz * by) + bx * Pw + bw * Px;
		Wy = (Pw * by + bw * Py) - (bz * Px - bx * Pz);
		Wz = (Px * by - Py * bx) + bw * Pz + bz * Pw;
		Ww = bw * Pw - (Px * bx + bz * Pz + Py * by);
	}
	else if (v == BASE_B)
	{
		ax = (tz * Py - Pz * ty) + Pw * tx;
		ay = Pw * ty - (tz * Px - Pz * tx);
		az = (Px * ty - tx * Py) + tz * Pw;
		aw = zero - (ty * Py + tz * Pz + Px * tx);
		Tx = (az * Py - Pz * ay) + (Pw * ax - Px * aw) + PT0;
		Ty = PT1 + ((Pw * ay - aw * Py) - (Px * az - Pz * ax));
		Tz = PT2 + ((Px * ay - ax * Py) + (Pw * az - Pz * aw));
		Wx = (bz * Py - Pz * by) + bx * Pw + bw * Px;
		Wy = (bw * Py + Pw * by) - (Px * bz - bx * Pz);
		Wz = (by * Px - bx * Py) + Pz * bw + Pw * bz;
		Ww = Pw * bw - (by * Py + bx * Px + Pz * bz);
	}
	else
	{
		ax = (tz * Py - Pz * ty) + Pw * tx;
		ay = Pw * ty - (tz * Px - Pz * tx);
		az = (Px * ty - tx * Py) + tz * Pw;
		aw = zero - (ty * Py + tz * Pz + Px * tx);
		Tx = PT0 + ((az * Py - Pz * ay) + (Pw * ax - Px * aw));
		Ty = PT1 + ((Pw * ay - aw * Py) - (Px * az - Pz * ax));
		Tz = PT2 + ((Px * ay - ax * Py) + (Pw * az - Pz * aw));
		Wx = (bz * Py - Pz * by) + Pw * bx + bw * Px;
		Wy = (bw * Py + Pw * by) - (Px * bz - bx * Pz);
		Wz = (by * Px - bx * Py) + Pz * bw + Pw * bz;
		Ww = Pw * bw - (Px * bx + by * Py + Pz * bz);
	}
	W.q[0] = Wx.v;
	W.q[1] = Wy.v;
	W.q[2] = Wz.v;
	W.q[3] = Ww.v;
	W.t[0] = Tx.v;
	W.t[1] = Ty.v;
	W.t[2] = Tz.v;
}

void applyTranslation(bool motionArm, bool hasRotation, QT &W, float txf, float tyf, float tzf)
{
	const F Wx(W.q[0]), Wy(W.q[1]), Wz(W.q[2]), Ww(W.q[3]);
	const F tx(txf), ty(tyf), tz(tzf);
	const F zero(0.0f);
	F ax(0.0f), ay(0.0f), az(0.0f), aw(0.0f);
	if (motionArm)
	{
		ax = (tz * Wy - Wz * ty) + Ww * tx;
		ay = Ww * ty - (tz * Wx - Wz * tx);
		az = (ty * Wx - tx * Wy) + Ww * tz;
		aw = zero - (ty * Wy + tx * Wx + Wz * tz);
		W.t[0] = (F(W.t[0]) + ((az * Wy - Wz * ay) + (Ww * ax - aw * Wx))).v;
		if (hasRotation)
		{
			W.t[1] = (F(W.t[1]) + ((Ww * ay - aw * Wy) - (az * Wx - Wz * ax))).v;
			W.t[2] = (F(W.t[2]) + ((ay * Wx - ax * Wy) + (Ww * az - Wz * aw))).v;
		}
		else
		{
			W.t[1] = (((Ww * ay - aw * Wy) - (az * Wx - Wz * ax)) + F(W.t[1])).v;
			W.t[2] = ((ay * Wx - ax * Wy) + (Ww * az - Wz * aw) + F(W.t[2])).v;
		}
		return;
	}
	ax = (tz * Wy - ty * Wz) + tx * Ww;
	az = (ty * Wx - Wy * tx) + tz * Ww;
	if (hasRotation)
	{
		ay = ty * Ww - (tz * Wx - tx * Wz);
		aw = zero - (tx * Wx + ty * Wy + tz * Wz);
		W.t[0] = ((Wy * az - ay * Wz) + (ax * Ww - aw * Wx) + F(W.t[0])).v;
	}
	else
	{
		ay = ty * Ww - (tz * Wx - Wz * tx);
		aw = zero - (tx * Wx + tz * Wz + ty * Wy);
		W.t[0] = (F(W.t[0]) + ((az * Wy - ay * Wz) + (ax * Ww - aw * Wx))).v;
	}
	W.t[1] = (((ay * Ww - aw * Wy) - (az * Wx - ax * Wz)) + F(W.t[1])).v;
	W.t[2] = ((ay * Wx - Wy * ax) + (az * Ww - aw * Wz) + F(W.t[2])).v;
}

void applyRotation(bool motionArm, QT &W, const float qv[4])
{
	const F Wx(W.q[0]), Wy(W.q[1]), Wz(W.q[2]), Ww(W.q[3]);
	const F qx(qv[0]), qy(qv[1]), qz(qv[2]), qw(qv[3]);
	if (motionArm)
	{
		W.q[0] = ((qz * Wy - qy * Wz) + qx * Ww + qw * Wx).v;
		W.q[1] = ((qw * Wy + qy * Ww) - (qz * Wx - qx * Wz)).v;
		W.q[2] = ((qy * Wx - qx * Wy) + qw * Wz + qz * Ww).v;
		W.q[3] = (qw * Ww - (qx * Wx + qz * Wz + qy * Wy)).v;
	}
	else
	{
		W.q[0] = ((qz * Wy - qy * Wz) + qw * Wx + qx * Ww).v;
		W.q[1] = ((qw * Wy + qy * Ww) - (qz * Wx - qx * Wz)).v;
		W.q[2] = ((qy * Wx - qx * Wy) + qw * Wz + qz * Ww).v;
		W.q[3] = (qw * Ww - (qz * Wz + qy * Wy + qx * Wx)).v;
	}
}

Matrix3D matrixOf(const QT &w)
{
	const F x(w.q[0]), y(w.q[1]), z(w.q[2]), qw(w.q[3]);
	const F two(2.0f), one(1.0f);
	const F xx2 = x * x * two;
	const F xz2 = x * z * two;
	const F zz2 = z * z * two;
	const F yx2 = y * x * two;
	const F wz2 = qw * z * two;
	const F yw2 = y * qw * two;
	const F xw2 = x * qw * two;
	const F yz2 = y * z * two;
	const F one_yy = one - y * y * two;
	Matrix3D m;
	m.Row[0][0] = (one_yy - zz2).v;
	m.Row[0][1] = (yx2 - wz2).v;
	m.Row[0][2] = (yw2 + xz2).v;
	m.Row[0][3] = w.t[0];
	m.Row[1][0] = (wz2 + yx2).v;
	m.Row[1][1] = ((one - zz2) - xx2).v;
	m.Row[1][2] = (yz2 - xw2).v;
	m.Row[1][3] = w.t[1];
	m.Row[2][0] = (xz2 - yw2).v;
	m.Row[2][1] = (yz2 + xw2).v;
	m.Row[2][2] = (one_yy - xx2).v;
	m.Row[2][3] = w.t[2];
	return m;
}
} // namespace

namespace PristinePose
{

float adaptiveDeltaFilter(unsigned index)
{
	return filterTable().value[index & 0xFF];
}

void computeFilterTable(float out[256])
{
	fillFilterTable(out);
}

CanonicalEnvironment::CanonicalEnvironment()
{
	std::fegetenv(&m_saved);
	NumericState::normalizeFloatingPointEnvironment();
}

CanonicalEnvironment::~CanonicalEnvironment()
{
	std::fesetenv(&m_saved);
}

bool evaluate(const HTreeClass &tree, const HAnimClass *anim, int frame, float scale, std::vector<Matrix3D> &out, std::string *error)
{
	const CanonicalEnvironment canonical;
	const int pivots = tree.Num_Pivots();
	std::vector<QT> world((size_t)(pivots > 0 ? pivots : 0));
	out.clear();
	if (pivots <= 0)
	{
		return true;
	}
	// the root: the identity root matrix gives the quaternion (0, 0, 0, 1) and no translation (htree.cpp setRoot / quaternionOf: trace 3, s = 2,
	// w = 0.5 * 2, the other terms (0 - 0) * 0.25)
	const HRawAnimClass *raw = dynamic_cast<const HRawAnimClass *>(anim);
	const HCompressedAnimClass *comp = dynamic_cast<const HCompressedAnimClass *>(anim);
	if (anim && !raw && !comp)
	{
		if (error)
		{
			*error = "animation " + anim->Get_Name() + " is of a class the pristine pose does not evaluate";
		}
		return false;
	}
	const bool motionArm = comp && comp->Uses_Motion_Channels();
	const F scaleFactor(tree.Get_Scale_Factor());
	int iframe = frame;
	if (raw && iframe >= raw->Get_Num_Frames())
	{
		iframe = 0; // Anim_Pose_Raw: a frame at or past the end becomes 0
	}
	try
	{
		const int animPivots = anim ? anim->Get_Num_Pivots() : 0;
		for (int p = 1; p < pivots; ++p)
		{
			const PivotClass &pivot = tree.Get_Pivot(p);
			QT &W = world[(size_t)p];
			const BaseVariant variant = !anim ? BASE_U : (motionArm ? BASE_A : BASE_B);
			composeBase(variant, world[(size_t)pivot.ParentIdx], pivot.BaseQuat, pivot.BaseTrans, W);
			if (!anim || p >= animPivots)
			{
				continue;
			}
			float t[3] = { 0.0f, 0.0f, 0.0f };
			float q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			bool hasRotation = false;
			if (raw)
			{
				const NodeMotionStruct &nm = raw->Get_Node_Motion(p);
				if (nm.X) rawValue(*nm.X, iframe, &t[0]);
				if (nm.Y) rawValue(*nm.Y, iframe, &t[1]);
				if (nm.Z) rawValue(*nm.Z, iframe, &t[2]);
				hasRotation = nm.Q != nullptr;
				if (hasRotation) rawValue(*nm.Q, iframe, q);
			}
			else if (motionArm)
			{
				const BFME2CompressedMotionChannels &m = comp->Get_Vector_Motion(p);
				for (int i = 0; i < 3; ++i)
				{
					if (m.Channels[i]) motionValue(*m.Channels[i], iframe, false, &t[i]);
				}
				hasRotation = m.Channels[CHANNEL_SLOT_Q] != nullptr;
				if (hasRotation) motionValue(*m.Channels[CHANNEL_SLOT_Q], iframe, true, q);
			}
			else
			{
				const NodeCompressedMotionStruct &m = comp->Get_Node_Motion(p);
				const bool timeCoded = m.Flavor == ANIM_FLAVOR_TIMECODED;
				for (int i = 0; i < 3; ++i)
				{
					if (timeCoded && m.tc[i]) timeCodedValue(*m.tc[i], iframe, false, &t[i]);
					if (!timeCoded && m.ad[i]) adaptiveValue(*m.ad[i], iframe, false, &t[i]);
				}
				if (timeCoded)
				{
					hasRotation = m.tc[CHANNEL_SLOT_Q] != nullptr;
					if (hasRotation) timeCodedValue(*m.tc[CHANNEL_SLOT_Q], iframe, true, q);
				}
				else
				{
					hasRotation = m.ad[CHANNEL_SLOT_Q] != nullptr;
					if (hasRotation) adaptiveValue(*m.ad[CHANNEL_SLOT_Q], iframe, true, q);
				}
			}
			// the motion-channel arm multiplies by the tree's ScaleFactor unconditionally, the others only when it is not 1
			if (motionArm || scaleFactor.v != 1.0f)
			{
				for (int i = 0; i < 3; ++i)
				{
					t[i] = (F(t[i]) * scaleFactor).v;
				}
			}
			applyTranslation(motionArm, hasRotation, W, t[0], t[1], t[2]);
			if (hasRotation)
			{
				applyRotation(motionArm, W, q);
			}
		}
	}
	catch (const Undefined &u)
	{
		if (error)
		{
			*error = "animation " + anim->Get_Name() + " at frame " + std::to_string(frame) + ": " + u.what;
		}
		return false;
	}
	out.resize((size_t)pivots);
	const F s(scale);
	for (int p = 0; p < pivots; ++p)
	{
		Matrix3D m = matrixOf(world[(size_t)p]);
		if (scale != 1.0f)
		{
			for (int r = 0; r < 3; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					m.Row[r][c] = (s * F(m.Row[r][c])).v;
				}
			}
		}
		out[(size_t)p] = m;
	}
	return true;
}

} // namespace PristinePose
