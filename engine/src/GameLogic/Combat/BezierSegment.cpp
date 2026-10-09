// OpenBFME. GPL-3.0.
// See GameLogic/Combat/BezierSegment.h for the target facts and the inference.

#include "GameLogic/Combat/BezierSegment.h"

#include "GameLogic/SimMath.h"

namespace
{
// the basis matrix of the binary (RW 0xDB77F0): rows (-1 3 -3 1) (3 -6 3 0) (-3 3 0 0) (1 0 0 0)
const float kBasis[4][4] = { { -1.0f, 3.0f, -3.0f, 1.0f }, { 3.0f, -6.0f, 3.0f, 0.0f }, { -3.0f, 3.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };

// D3DXVec4Transform(out, v, basis): left to right (INFERENCE S-361)
void transform4(const float v[4], float out[4])
{
	for (int j = 0; j < 4; ++j)
	{
		float s = SimMath::mulf32(v[0], kBasis[0][j]);
		s = SimMath::addf32(s, SimMath::mulf32(v[1], kBasis[1][j]));
		s = SimMath::addf32(s, SimMath::mulf32(v[2], kBasis[2][j]));
		s = SimMath::addf32(s, SimMath::mulf32(v[3], kBasis[3][j]));
		out[j] = s;
	}
}

// RW 0x403111 as the x87 sees it: the CRT root of the float sum of squares (left to right), rounded to 24 bits, left in the register
double lengthWide(float x, float y, float z)
{
	return SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, z));
}

Coord3D diff(const Coord3D &a, const Coord3D &b)
{
	return Coord3D{ SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y), SimMath::subf32(a.z, b.z) };
}
} // namespace

// RW 0x960BA5: the three coordinates are dotted with the transformed (t^3, t^2, t, 1), w first
void BezierSegment::evaluateBezSegmentAtT(float tValue, Coord3D *outResult) const
{
	if (!outResult)
	{
		return;
	}
	const float t2 = SimMath::mulf32(tValue, tValue);
	const float t3 = SimMath::mulf32(t2, tValue);
	const float tv[4] = { t3, t2, tValue, 1.0f };
	float r[4];
	transform4(tv, r);
	auto dot = [&](float p0, float p1, float p2, float p3) {
		float s = SimMath::mulf32(r[3], p3);
		s = SimMath::addf32(s, SimMath::mulf32(r[2], p2));
		s = SimMath::addf32(s, SimMath::mulf32(r[1], p1));
		s = SimMath::addf32(s, SimMath::mulf32(r[0], p0));
		return s;
	};
	const Coord3D *c = m_controlPoints;
	outResult->x = dot(c[0].x, c[1].x, c[2].x, c[3].x);
	outResult->y = dot(c[0].y, c[1].y, c[2].y, c[3].y);
	outResult->z = dot(c[0].z, c[1].z, c[2].z, c[3].z);
}

// RW 0x960ED4 (ZH BezierSegment::splitSegmentAtT): the de Casteljau split, every step one SSE operation in the binary's order
void BezierSegment::splitSegmentAtT(float tValue, BezierSegment &outSeg1, BezierSegment &outSeg2) const
{
	const Coord3D *c = m_controlPoints;
	Coord3D p0p1 = diff(c[1], c[0]);
	Coord3D p1p2 = diff(c[2], c[1]);
	Coord3D p2p3 = diff(c[3], c[2]);
	auto scaleAdd = [&](Coord3D &v, const Coord3D &base) {
		v.x = SimMath::addf32(SimMath::mulf32(v.x, tValue), base.x);
		v.y = SimMath::addf32(SimMath::mulf32(v.y, tValue), base.y);
		v.z = SimMath::addf32(SimMath::mulf32(v.z, tValue), base.z);
	};
	scaleAdd(p0p1, c[0]);
	scaleAdd(p1p2, c[1]);
	scaleAdd(p2p3, c[2]);
	Coord3D triLeft = diff(p1p2, p0p1);
	Coord3D triRight = diff(p2p3, p1p2);
	scaleAdd(triLeft, p0p1);
	scaleAdd(triRight, p1p2);
	BezierSegment a, b;
	a.m_controlPoints[0] = c[0];
	a.m_controlPoints[1] = p0p1;
	a.m_controlPoints[2] = triLeft;
	evaluateBezSegmentAtT(tValue, &a.m_controlPoints[3]);
	b.m_controlPoints[0] = a.m_controlPoints[3];
	b.m_controlPoints[1] = triRight;
	b.m_controlPoints[2] = p2p3;
	b.m_controlPoints[3] = c[3];
	outSeg1 = a;
	outSeg2 = b;
}

// RW 0x9610EF
double BezierSegment::getApproximateLength(float withinTolerance) const
{
	const Coord3D *c = m_controlPoints;
	const Coord3D p0p1 = diff(c[1], c[0]);
	const Coord3D p1p2 = diff(c[2], c[1]);
	const Coord3D p2p3 = diff(c[3], c[2]);
	const Coord3D p0p3 = diff(c[3], c[0]);
	const float length0 = SimMath::fstpDword(lengthWide(p0p3.x, p0p3.y, p0p3.z));
	const float l23 = SimMath::fstpDword(lengthWide(p2p3.x, p2p3.y, p2p3.z));
	const float l1223 = SimMath::fstpDword(SimMath::pc24AddW(lengthWide(p1p2.x, p1p2.y, p1p2.z), (double)l23));
	const double length1 = SimMath::pc24AddW(lengthWide(p0p1.x, p0p1.y, p0p1.z), (double)l1223); // stays in the register
	const double error = SimMath::pc24SubW(length1, (double)length0);
	if (error > (double)withinTolerance)
	{
		BezierSegment seg1, seg2;
		splitSegmentAtT(0.5f, seg1, seg2);
		const float second = SimMath::fstpDword(seg2.getApproximateLength(withinTolerance));
		const float sum = SimMath::fstpDword(SimMath::pc24AddW(seg1.getApproximateLength(withinTolerance), (double)second));
		return (double)sum;
	}
	return SimMath::pc24MulW(SimMath::pc24AddW(length1, (double)length0), 0.5);
}

// RW 0x9612A3 with the iterator RW 0x9D6A35 / 0x9D6BBA
void BezierSegment::getSegmentPoints(int numSegments, std::vector<Coord3D> *outResult) const
{
	if (!outResult)
	{
		return;
	}
	outResult->clear();
	if (numSegments <= 0)
	{
		return;
	}
	outResult->resize((size_t)numSegments);
	Coord3D cur{ 0.0f, 0.0f, 0.0f }, dq{ 0.0f, 0.0f, 0.0f }, ddq{ 0.0f, 0.0f, 0.0f }, dddq{ 0.0f, 0.0f, 0.0f };
	if (numSegments > 1)
	{
		const float d = SimMath::divf32(1.0f, SimMath::sseFromInt32(numSegments - 1));
		const float d2 = SimMath::mulf32(d, d);
		const float d3 = SimMath::mulf32(d2, d);
		const Coord3D *c = m_controlPoints;
		const float px[4] = { c[0].x, c[1].x, c[2].x, c[3].x };
		const float py[4] = { c[0].y, c[1].y, c[2].y, c[3].y };
		const float pz[4] = { c[0].z, c[1].z, c[2].z, c[3].z };
		float cv[3][4];
		transform4(px, cv[0]);
		transform4(py, cv[1]);
		transform4(pz, cv[2]);
		cur = c[0];
		float *pD[3] = { &dq.x, &dq.y, &dq.z };
		float *pDD[3] = { &ddq.x, &ddq.y, &ddq.z };
		float *pDDD[3] = { &dddq.x, &dddq.y, &dddq.z };
		for (int i = 2; i >= 0; --i)
		{
			const float a = cv[i][0];
			const float b = cv[i][1];
			const float cc = cv[i][2];
			const float bd2 = SimMath::mulf32(b, d2);
			const float cd = SimMath::mulf32(cc, d);
			const float ad3 = SimMath::mulf32(a, d3);
			float sum = SimMath::addf32(cd, bd2);
			sum = SimMath::addf32(sum, ad3);
			*pD[i] = sum;
			*pDD[i] = SimMath::addf32(SimMath::mulf32(bd2, 2.0f), SimMath::mulf32(ad3, 6.0f));
			*pDDD[i] = SimMath::mulf32(ad3, 6.0f);
		}
	}
	for (int i = 0; i < numSegments; ++i)
	{
		(*outResult)[(size_t)i] = cur;
		cur.x = SimMath::addf32(dq.x, cur.x);
		cur.y = SimMath::addf32(dq.y, cur.y);
		cur.z = SimMath::addf32(dq.z, cur.z);
		dq.x = SimMath::addf32(ddq.x, dq.x);
		dq.y = SimMath::addf32(ddq.y, dq.y);
		dq.z = SimMath::addf32(ddq.z, dq.z);
		ddq.x = SimMath::addf32(dddq.x, ddq.x);
		ddq.y = SimMath::addf32(dddq.y, ddq.y);
		ddq.z = SimMath::addf32(dddq.z, ddq.z);
	}
}
