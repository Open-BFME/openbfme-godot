// OpenBFME unit tests. GPL-3.0.
// Lane PRODUCTION1: an independent model of the production counter of ProductionUpdate::update for the retail production sweeps. RW 0x8A1EBC .. 0x8A1F0F:
// every update adds the producer's PRODUCTION rate (1.0 times its PRODUCTION attribute modifiers, RW 0x68C82D) to the entry's counter (SSE single), then
// percent = counter / totalFrames * 100 (the divide first); the entry is done at percent >= 100. A level 2 / 3 building carries its BuildSpeedModLvl<n>, so
// it no longer takes "BuildTime * 5" frames. The level grants of a LevelUpUpgrade land in the frame's phase 5, after the object updates: the rate read
// before a frame is the rate that frame's update uses, so a test records it before every frame it runs.

#pragma once

#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/Object.h"

#include <vector>

namespace prodrate
{
// the PRODUCTION rate the producer's next update uses (1.0 without a pool or an active PRODUCTION list)
inline float rateNow(const Object &producer)
{
	float r = 1.0f;
	producer.attributeModifierProduct(ATTRIBUTE_PRODUCTION, nullptr, true, r);
	return r;
}

// the frames until the percent reaches 100 when frame i adds rates[i]; -1 when the recorded frames end first. With every rate 1.0 this is `total`
inline int framesToComplete(const std::vector<float> &rates, int total)
{
	volatile float counter = 0.0f;
	for (size_t i = 0; i < rates.size(); ++i)
	{
		counter = counter + rates[i];
		volatile float percent = counter / (float)total;
		percent = percent * 100.0f;
		if (percent >= 100.0f)
		{
			return (int)i + 1;
		}
	}
	return -1;
}

// `total` frames at rate 1.0, shortened by the rates recorded so far; `total` when the producer is gone (nothing recorded)
inline int expectedFrames(const std::vector<float> &rates, int total)
{
	const int n = framesToComplete(rates, total);
	return n > 0 ? n : total;
}
} // namespace prodrate
