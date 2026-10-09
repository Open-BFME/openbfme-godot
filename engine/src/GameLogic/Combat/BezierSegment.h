// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// BezierSegment (lane PROJ-1): the cubic Bezier segment the projectile flight paths are made of, with the forward differencing iterator that fills the path (ZH
// GameEngine/Source/Common/Bezier/BezierSegment.cpp and BezFwdIterator.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the constructor RW 0x960E8D copies the four control points; evaluateBezSegmentAtT RW 0x960BA5 (the basis transform of
// (t^3, t^2, t, 1) and the three dot products, accumulated w*P3 + z*P2 + y*P1 + x*P0), splitSegmentAtT RW 0x960ED4, getApproximateLength RW 0x9610EF (lengths are the
// CRT root of the float sum of squares RW 0x403111; the sums run on the x87 at 24 bits and the result stays in ST0), getSegmentPoints RW 0x9612A3 and the iterator
// RW 0x9D69CC / 0x9D6A35 (start) / 0x9D6BBA (next): the coefficients are a = (a*d^3), b = ..., d = 1 / (steps - 1), summed c*d + b*d^2 first, then + a*d^3 (ZH adds in the
// opposite order: the binary's order is kept), the second difference 2*(b*d^2) + 6*(a*d^3), the third 6*(a*d^3).
// INFERENCE (stop S-361): the basis transform D3DXVec4Transform is an import of the D3DX DLL (not in game.dat): it is evaluated left to right as float32 products and sums
// (v0*M0j + v1*M1j + v2*M2j + v3*M3j); the DLL's own accumulation order is not known.

#pragma once

#include "Common/INIDataTypes.h"

#include <vector>

class BezierSegment
{
public:
	BezierSegment() = default;
	// RW 0x960E8D
	explicit BezierSegment(const Coord3D cp[4])
	{
		for (int i = 0; i < 4; ++i)
		{
			m_controlPoints[i] = cp[i];
		}
	}

	// RW 0x960BA5
	void evaluateBezSegmentAtT(float tValue, Coord3D *outResult) const;
	// RW 0x960ED4
	void splitSegmentAtT(float tValue, BezierSegment &outSeg1, BezierSegment &outSeg2) const;
	// RW 0x9610EF. Returns the x87 register value (a binary64 carrier of a 24 bit significand): the caller divides it by the speed before the store.
	double getApproximateLength(float withinTolerance) const;
	// RW 0x9612A3: `numSegments` points from the first control point to the last, forward differenced
	void getSegmentPoints(int numSegments, std::vector<Coord3D> *outResult) const;

	Coord3D m_controlPoints[4]{};
};
