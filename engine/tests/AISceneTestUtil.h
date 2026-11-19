// OpenBFME. GPL-3.0.
#pragma once

#include "doctest.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <limits>

namespace testutil
{
// Isolate a scripted movement/combat scene from Easy's background roaming without changing difficulty, production, or the other tactics.
// RW 0x9B7F56 / 0x9B8039: the existing next-run counter gates eligibility before the targetless pool draws (RW 0x90C5E2).
// A non-default object team alone cannot isolate the shared RNG: choosing even an empty roaming tactic consumes a pool draw.
inline void deferRoamingDefense(GameLogic &logic, int player)
{
	auto *ai = const_cast<AISkirmishPlayer *>(logic.skirmishAI().findAI(player));
	REQUIRE(ai);
	REQUIRE(ai->brain.tactics.empty()); // call at scene setup, before any tactic can start
	ai->brain.namedCounters["AIRoamingDefenseTactic_NextLogicFrameRun"] = std::numeric_limits<int>::max();
}
} // namespace testutil
