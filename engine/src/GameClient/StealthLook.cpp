// OpenBFME. GPL-3.0.
// See GameClient/StealthLook.h.

#include "GameClient/StealthLook.h"

#include "GameClient/LogicSnapshot.h"

#include <cmath>

bool StealthLook::hidden(const ObjectSnapshot &rec)
{
	return rec.stealthLook == 5;
}

float StealthLook::opacity(const ObjectSnapshot &rec, const LogicSnapshot &snap, double clockMs)
{
	if (rec.stealthLook != 1 && rec.stealthLook != 4)
	{
		return 1.0f;
	}
	(void)snap;
	const double lo = rec.stealthOpacityMin, hi = rec.stealthOpacityMax;
	const double period = rec.stealthCycleFrames > 0 ? rec.stealthCycleFrames / 5.0 : 1.0; // frames * 0.2f (RW 0x81AADC / 0x777ADA): seconds
	const double phase = (double)(rec.id % 97u) * 0.0647;
	const double wave = std::sin(6.283185307179586 * (clockMs / 1000.0) / period + phase);
	const double o = (lo + hi) * 0.5 + (hi - lo) * 0.5 * wave;
	return (float)(o < 0.0 ? 0.0 : o > 1.0 ? 1.0 : o);
}

float StealthLook::drawOpacity(float base, const ObjectSnapshot *rec, const LogicSnapshot *snap, double clockMs)
{
	return rec && snap ? base * opacity(*rec, *snap, clockMs) : base;
}

double StealthLook::advanceClock(double clockMs, double deltaMs)
{
	return clockMs + deltaMs;
}
