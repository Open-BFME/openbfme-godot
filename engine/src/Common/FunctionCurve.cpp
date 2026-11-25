// OpenBFME. GPL-3.0.
// See Common/FunctionCurve.h (editions: both RotWK 2.01 and BFME2 1.06).

#include "Common/FunctionCurve.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace
{
constexpr float kRadsPerDegree = 0.0174532925f;

const char *kCurvePaddingNames[] = {
	"HOLD",
	"EXTRAPOLATE",
	"CYCLE",
	"CYCLE_OFFSET",
	"MIRROR",
	nullptr
};

float floorF32(float x)
{
	const int i = static_cast<int>(x);
	const float fi = SimMath::sseFromInt32(i);
	return fi > x ? SimMath::subf32(fi, 1.0f) : fi;
}

float fmodF32(float x, float y)
{
	const float q = SimMath::divf32(x, y);
	const float iq = SimMath::sseFromInt32(static_cast<int>(q));
	return SimMath::subf32(x, SimMath::mulf32(iq, y));
}

float scanTangentAngle(INI *ini, const char *token)
{
	const float deg = ini->scanReal(token);
	const float rad = SimMath::mulf32(deg, kRadsPerDegree);
	return SimMath::divf32(SimMath::sinf32(rad), SimMath::cosf32(rad));
}
} // namespace

FunctionCurve::FunctionCurve(float constantValue)
{
	addKey(0.0f, constantValue);
}

void FunctionCurve::addKey(float time, float value, const float *inTangent, const float *outTangent)
{
	CurveKey key;
	key.m_time = time;
	key.m_value = value;
	if (inTangent)
	{
		key.m_inTangent = *inTangent;
	}
	if (outTangent)
	{
		key.m_outTangent = *outTangent;
	}

	if (m_keys.empty())
	{
		m_keys.push_back(key);
		return;
	}

	if (time < m_keys.back().m_time)
	{
		throw INIException(3, "Function curve time must be increasing");
	}

	CurveKey &prev = m_keys.back();
	const float dt = SimMath::subf32(time, prev.m_time);
	if (dt > 1e-6f)
	{
		const float slope = SimMath::divf32(SimMath::subf32(value, prev.m_value), dt);
		if (!outTangent)
		{
			prev.m_outTangent = slope;
		}
		if (!inTangent)
		{
			key.m_inTangent = slope;
		}
		if (m_keys.size() == 1 && !inTangent)
		{
			prev.m_inTangent = slope;
		}
		if (!outTangent)
		{
			key.m_outTangent = slope;
		}
	}

	m_keys.push_back(key);
}

float FunctionCurve::operator()(float t) const
{
	if (m_keys.empty())
	{
		return 0.0f;
	}
	if (m_keys.size() == 1)
	{
		return m_keys.front().m_value;
	}

	const float t0 = m_keys.front().m_time;
	const float t1 = m_keys.back().m_time;
	const float span = SimMath::subf32(t1, t0);

	if (t < t0)
	{
		switch (m_inPadding)
		{
		case CurvePadding::HOLD:
			return m_keys.front().m_value;
		case CurvePadding::EXTRAPOLATE:
			return SimMath::addf32(m_keys.front().m_value, SimMath::mulf32(SimMath::subf32(t, t0), m_keys.front().m_inTangent));
		case CurvePadding::CYCLE:
			if (span > 1e-6f)
			{
				float offset = fmodF32(SimMath::subf32(t, t0), span);
				if (offset < 0.0f)
				{
					offset = SimMath::addf32(offset, span);
				}
				t = SimMath::addf32(t0, offset);
			}
			break;
		case CurvePadding::CYCLE_OFFSET:
			if (span > 1e-6f)
			{
				const float cycles = floorF32(SimMath::divf32(SimMath::subf32(t, t0), span));
				const float offset = SimMath::subf32(SimMath::subf32(t, t0), SimMath::mulf32(cycles, span));
				const float deltaVal = SimMath::subf32(m_keys.back().m_value, m_keys.front().m_value);
				return SimMath::addf32((*this)(SimMath::addf32(t0, offset)), SimMath::mulf32(cycles, deltaVal));
			}
			break;
		case CurvePadding::MIRROR:
			if (span > 1e-6f)
			{
				const float twoSpan = SimMath::mulf32(2.0f, span);
				const float cycles = floorF32(SimMath::divf32(SimMath::subf32(t, t0), twoSpan));
				const float rem = SimMath::subf32(SimMath::subf32(t, t0), SimMath::mulf32(cycles, twoSpan));
				if (rem > span)
				{
					t = SimMath::subf32(t1, SimMath::subf32(rem, span));
				}
				else
				{
					t = SimMath::addf32(t0, rem);
				}
			}
			break;
		}
	}
	else if (t > t1)
	{
		switch (m_outPadding)
		{
		case CurvePadding::HOLD:
			return m_keys.back().m_value;
		case CurvePadding::EXTRAPOLATE:
			return SimMath::addf32(m_keys.back().m_value, SimMath::mulf32(SimMath::subf32(t, t1), m_keys.back().m_outTangent));
		case CurvePadding::CYCLE:
			if (span > 1e-6f)
			{
				const float offset = fmodF32(SimMath::subf32(t, t0), span);
				t = SimMath::addf32(t0, offset);
			}
			break;
		case CurvePadding::CYCLE_OFFSET:
			if (span > 1e-6f)
			{
				const float cycles = floorF32(SimMath::divf32(SimMath::subf32(t, t0), span));
				const float offset = SimMath::subf32(SimMath::subf32(t, t0), SimMath::mulf32(cycles, span));
				const float deltaVal = SimMath::subf32(m_keys.back().m_value, m_keys.front().m_value);
				return SimMath::addf32((*this)(SimMath::addf32(t0, offset)), SimMath::mulf32(cycles, deltaVal));
			}
			break;
		case CurvePadding::MIRROR:
			if (span > 1e-6f)
			{
				const float twoSpan = SimMath::mulf32(2.0f, span);
				const float cycles = floorF32(SimMath::divf32(SimMath::subf32(t, t0), twoSpan));
				const float rem = SimMath::subf32(SimMath::subf32(t, t0), SimMath::mulf32(cycles, twoSpan));
				if (rem > span)
				{
					t = SimMath::subf32(t1, SimMath::subf32(rem, span));
				}
				else
				{
					t = SimMath::addf32(t0, rem);
				}
			}
			break;
		}
	}

	size_t idx = 0;
	while (idx + 1 < m_keys.size() && m_keys[idx + 1].m_time < t)
	{
		++idx;
	}
	if (idx + 1 >= m_keys.size())
	{
		return m_keys.back().m_value;
	}

	const CurveKey &k0 = m_keys[idx];
	const CurveKey &k1 = m_keys[idx + 1];
	const float dt = SimMath::subf32(k1.m_time, k0.m_time);
	if (dt <= 1e-6f)
	{
		return k0.m_value;
	}

	const float u = SimMath::divf32(SimMath::subf32(t, k0.m_time), dt);
	const float u2 = SimMath::mulf32(u, u);
	const float u3 = SimMath::mulf32(u2, u);

	const float h00 = SimMath::addf32(SimMath::subf32(SimMath::mulf32(2.0f, u3), SimMath::mulf32(3.0f, u2)), 1.0f);
	const float h10 = SimMath::addf32(SimMath::subf32(u3, SimMath::mulf32(2.0f, u2)), u);
	const float h01 = SimMath::addf32(SimMath::mulf32(-2.0f, u3), SimMath::mulf32(3.0f, u2));
	const float h11 = SimMath::subf32(u3, u2);

	const float m0 = SimMath::mulf32(k0.m_outTangent, dt);
	const float m1 = SimMath::mulf32(k1.m_inTangent, dt);

	const float term0 = SimMath::mulf32(h00, k0.m_value);
	const float term1 = SimMath::mulf32(h10, m0);
	const float term2 = SimMath::mulf32(h01, k1.m_value);
	const float term3 = SimMath::mulf32(h11, m1);
	return SimMath::addf32(SimMath::addf32(term0, term1), SimMath::addf32(term2, term3));
}

void FunctionCurve::parseKey(INI *ini, void *, void *, const void *userData)
{
	auto *curve = reinterpret_cast<FunctionCurve *>(const_cast<void *>(userData));
	const float time = ini->scanReal(ini->getNextSubToken("T"));
	const float value = ini->scanReal(ini->getNextSubToken("V"));
	const float *inTangent = nullptr;
	const float *outTangent = nullptr;
	float inValue = 0.0f;
	float outValue = 0.0f;

	for (int count = 0; count < 2; ++count)
	{
		const char *token = ini->getNextTokenOrNull(ini->getSepsColon());
		if (!token)
		{
			break;
		}

		if (!inTangent && token[0] == 'I' && token[1] == '\0')
		{
			inValue = scanTangentAngle(ini, ini->getNextToken(ini->getSepsColon()));
			inTangent = &inValue;
		}
		else if (!outTangent && token[0] == 'O' && token[1] == '\0')
		{
			outValue = scanTangentAngle(ini, ini->getNextToken(ini->getSepsColon()));
			outTangent = &outValue;
		}
		else
		{
			throw INIException(3, "'I' or 'O' expected, and only one of them");
		}
	}

	curve->addKey(time, value, inTangent, outTangent);
}

void FunctionCurve::parse(INI *ini)
{
	FieldParse fields[] = {
		{ "InPadding", INI::parseIndexList, kCurvePaddingNames, (int)offsetof(FunctionCurve, m_inPadding) },
		{ "OutPadding", INI::parseIndexList, kCurvePaddingNames, (int)offsetof(FunctionCurve, m_outPadding) },
		{ "Key", FunctionCurve::parseKey, this, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(this, fields);
}

void FunctionCurve::parseFunctionCurve(INI *ini, void *, void *store, const void *)
{
	auto *curve = reinterpret_cast<FunctionCurve *>(store);
	const char *token = ini->getNextTokenOrNull();
	if (!token)
	{
		return;
	}

	char *endptr = nullptr;
	const float val = std::strtof(token, &endptr);
	if (endptr && endptr != token && *endptr == '\0')
	{
		curve->m_keys.clear();
		curve->addKey(0.0f, val);
		return;
	}

	if (AsciiStringUtil::compareNoCase(token, "FCurve") == 0 || AsciiStringUtil::compareNoCase(token, "Keys") == 0)
	{
		curve->m_keys.clear();
		curve->parse(ini);
		return;
	}

	throw INIException(3, "'FCurve' expected, got '%s'", token);
}
