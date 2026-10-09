// OpenBFME. GPL-3.0. See GameClientRandomVariable.h.

#include "Common/GameClientRandomVariable.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"


const char *const GameClientRandomVariable::DistributionTypeNames[] = { "CONSTANT", "UNIFORM", "GAUSSIAN", "TRIANGULAR", "LOW_BIAS", "HIGH_BIAS", nullptr };

// RW 0x73a396 (notes fx-ini-parse.md section 4, parseRandVar).
void GameClientRandomVariable::parseRandomVariable(INI *ini, void *, void *store, const void *)
{
	GameClientRandomVariable *rv = (GameClientRandomVariable *)store;
	const float low = ini->scanReal(ini->getNextToken());
	const float high = ini->scanReal(ini->getNextToken());
	DistributionType type = UNIFORM;
	if (const char *token = ini->getNextTokenOrNull())
	{
		type = (DistributionType)INI::scanIndexList(token, DistributionTypeNames);
	}
	rv->setRange(low, high, type);
}

float GameClientRandomVariable::getValue(W3DDrawRandom &stream) const
{
	switch (m_type)
	{
	case CONSTANT:
		if (m_low == m_high)
		{
			return m_low;
		}
		return stream.real(m_low, m_high); // ZH: a CONSTANT with low != high is drawn as UNIFORM
	case UNIFORM:
		return stream.real(m_low, m_high);
	default:
		// RW 0x6d343b: GAUSSIAN, TRIANGULAR, LOW_BIAS and HIGH_BIAS are parsed but not implemented; they return 0.0f (as ZH's
		// getValue does after its assert).
		return 0.0f;
	}
}
