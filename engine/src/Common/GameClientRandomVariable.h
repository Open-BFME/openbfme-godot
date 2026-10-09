// OpenBFME. GPL-3.0.
//
// GameClientRandomVariable: a {distribution, low, high} range drawn from the CLIENT random stream.
// Port of ZH GameEngine/Include/Common/RandomValue.h (class GameClientRandomVariable) as RotWK stores it.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; notes fx-ini-parse.md section 4):
//   * the INI parser is RW 0x73a396 (parseRandVar): low = scanReal(token 1), high = scanReal(token 2, mandatory),
//     then an optional third token: absent -> UNIFORM, present -> scanIndexList over DistributionTypeNames
//     (RW 0xda1cbc). The store is 12 bytes {int type, float low, float high} (setRange RW 0x6d3481). There is
//     no low <= high check.
//   * the distribution list is CONSTANT, UNIFORM, GAUSSIAN, TRIANGULAR, LOW_BIAS, HIGH_BIAS.
//   * getValue is RW 0x6d343b: CONSTANT returns low when low == high, otherwise (and for UNIFORM) it draws
//     GameClientRandomValueReal(low, high) (RW 0x6d33ab: no draw and `high` when high <= low); GAUSSIAN, TRIANGULAR,
//     LOW_BIAS and HIGH_BIAS return 0.0f (parsed, never implemented).
// DONOR FACTS (ZH RandomValue.cpp GameClientRandomVariable::getValue): the same shape; the draw is taken from the
// explicit client stream passed in, so the stream stays the caller's.

#pragma once

#include "Common/INI.h"

class W3DDrawRandom;

struct GameClientRandomVariable
{
	enum DistributionType
	{
		CONSTANT = 0,
		UNIFORM,
		GAUSSIAN,
		TRIANGULAR,
		LOW_BIAS,
		HIGH_BIAS
	};

	int m_type = CONSTANT; ///< DistributionType (stored as int: RW 12-byte layout)
	float m_low = 0.0f;
	float m_high = 0.0f;

	GameClientRandomVariable() = default;
	GameClientRandomVariable(DistributionType type, float low, float high) : m_type((int)type), m_low(low), m_high(high) {}

	void setRange(float low, float high, DistributionType type)
	{
		m_low = low;
		m_high = high;
		m_type = (int)type;
	}

	// ZH GameClientRandomVariable::getValue over the explicit client stream.
	float getValue(W3DDrawRandom &stream) const;

	// RW 0xda1cbc
	static const char *const DistributionTypeNames[];

	// RW 0x73a396: INI field parser (store is a GameClientRandomVariable).
	static void parseRandomVariable(INI *ini, void *instance, void *store, const void *userData);
};
