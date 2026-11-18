// OpenBFME. GPL-3.0.
#pragma once

#include <cstddef>
#include <set>
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

	// OpenBFME native input policy, not a retail graph invariant: cap the
	// quadratic farthest-pair search at 2,016 pairs per army. Reject duplicate
	// names instead of deduplicating them, which could change the last-tie
	// selection. Match names exactly, as TerrainLogic's waypoint lookup does.
	static constexpr std::size_t kMaxConnections = 64;
	static const char *connectionCountError(std::size_t count)
	{
		return count > kMaxConnections ? "the War of the Ring army's spawn plan has more than 64 connections (OpenBFME input limit)" : nullptr;
	}
	static const char *connectionError(const std::vector<std::string> &names)
	{
		if (const char *error = connectionCountError(names.size()))
			return error;
		std::set<std::string> seen;
		for (const std::string &name : names)
			if (!seen.insert(name).second)
				return "the War of the Ring army's spawn plan has duplicate connections (OpenBFME input policy)";
		return nullptr;
	}
};
