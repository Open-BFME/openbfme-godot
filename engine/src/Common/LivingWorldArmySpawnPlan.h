// OpenBFME. GPL-3.0.
#pragma once

#include <string>
#include <vector>

// RotWK-only battle placement context. RW 0x80F754 chooses a method using
// the battle's original sides and ownership, not its later tactical slots.
// BFME2 ArmySummarySpawnPositions.cpp (RVA 0x40CCC6) lacks RotWK's retry
// after a failed home/walk-on choice. The plan is resolved against the loaded
// map before the reinforcement enters the snapshot state.
struct LivingWorldArmySpawnPlan
{
	enum Method
	{
		LEGACY = 0, // old OpenBFME recording or caller without placement context (reported S-1329)
		NORMAL_NEUTRAL = 1,
		NORMAL_FRIENDLY = 2,
		NORMAL_HOSTILE = 3,
		AT_HOME = 4,
		WALK_ON = 5
	};
	int method = LEGACY;
	int side = 0;
	bool friendlyOwner = false;
	std::string originRegion;
	std::vector<std::string> connections; // battle-region connection order, including links without map waypoints
};
