// OpenBFME. GPL-3.0.
//
// FunctionCurve (editions: both RotWK 2.01 and BFME2 1.06):
// Cubic Hermite spline curve evaluator and INI parser for time-based parameter curves
// (e.g. PartTheHeavensUpdate Radius/Angle/Opacity, projectile arcs, alpha transitions).
//
// TARGET FACTS:
// - BFME2: parseFunctionCurve RVA 0x005051DA, addKey 0x00504F09, eval 0x00504B2F, key parse 0x005050EA.
// - RotWK: parseFunctionCurve RW 0x73B723, addKey RW 0x90ADF3, eval RW 0x90AAEF, key parse RW 0x90B010.
// - Padding modes: HOLD (0), EXTRAPOLATE (1), CYCLE (2), CYCLE_OFFSET (3), MIRROR (4).
// - Single float literal (e.g. Radius = 100) becomes a 1-key constant curve (t=0, v=val).
// - "FCurve" keyword starts an FCurve block with InPadding, OutPadding, Key = T:... V:... [I:...] [O:...].
// - Tangent angle (I/O) is given in degrees, converted to slope: tan(deg * pi / 180).
// - Spline segments evaluate cubic Hermite polynomials:
//   p(u) = (2u^3 - 3u^2 + 1) v0 + (u^3 - 2u^2 + u) dt m0 + (-2u^3 + 3u^2) v1 + (u^3 - u^2) dt m1.

#pragma once

#include <vector>

class INI;

enum class CurvePadding
{
	HOLD = 0,
	EXTRAPOLATE = 1,
	CYCLE = 2,
	CYCLE_OFFSET = 3,
	MIRROR = 4
};

struct CurveKey
{
	float m_time = 0.0f;
	float m_value = 0.0f;
	float m_inTangent = 0.0f;
	float m_outTangent = 0.0f;
};

class FunctionCurve
{
public:
	CurvePadding m_inPadding = CurvePadding::HOLD;
	CurvePadding m_outPadding = CurvePadding::HOLD;
	std::vector<CurveKey> m_keys;

	FunctionCurve() = default;
	explicit FunctionCurve(float constantValue);

	void addKey(float time, float value, const float *inTangent = nullptr, const float *outTangent = nullptr);
	float operator()(float t) const;

	void parse(INI *ini);

	static void parseKey(INI *ini, void *instance, void *store, const void *userData);
	static void parseFunctionCurve(INI *ini, void *instance, void *store, const void *userData);
};

