// OpenBFME. GPL-3.0.
#pragma once

#include "Common/INIDataTypes.h"
#include "Common/LivingWorldArmySpawnPlan.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// RotWK-only ArmySummary::getSpawnPositions (RW 0x80F754), checked against
// the installed binary (S-001 provenance caveat) and BFME2 1.06's
// ArmySummarySpawnPositions.cpp, RVA 0x40CCC6. Waypoint helpers and farthest
// selectors are shared with BFME2; the non-normal battle's retry is RotWK's.
namespace ArmySpawnPositions
{
using Lookup = std::function<const Coord3D *(const std::string &)>;

// Replay/dictionary/native preparation validates before placement. Keep this
// guard for direct C++ callers too: invalid input must not become a failed
// waypoint lookup followed by retail's fallback to another start position.
inline void requireConnections(const std::vector<std::string> &connections)
{
	if (const char *why = LivingWorldArmySpawnPlan::connectionError(connections))
		throw std::invalid_argument(why);
}

struct Positions
{
	Coord3D spawn{}, gather{};
	std::pair<std::string, std::string> names;
	bool usedStart = false;
};

// RW 0x781A85 / 0x781C9F; BFME2 ArmyPlacer.cpp RVAs 0x37F59B / 0x37F7B6:
// a walk-on or defender pair is valid only when BOTH waypoints exist.
inline bool pair(const Lookup &lookup, const std::string &spawnName, const std::string &gatherName, Positions &out)
{
	const Coord3D *spawn = lookup(spawnName), *gather = lookup(gatherName);
	if (!spawn || !gather)
		return false;
	out = { *spawn, *gather, { spawnName, gatherName }, false };
	return true;
}

// RW 0x781B17; BFME2 ArmyPlacer.cpp RVA 0x37F62D. A missing rally point
// copies the unmodified start to gather, then moves spawn ten along +x.
inline bool start(const Lookup &lookup, int index, Positions &out)
{
	const std::string name = "Player_" + std::to_string(index + 1) + "_Start";
	const Coord3D *spawn = lookup(name);
	if (!spawn)
		return false;
	out = { *spawn, *spawn, { name, name + "_Rally" }, true };
	if (const Coord3D *gather = lookup(out.names.second))
		out.gather = *gather;
	else
		out.spawn.x = SimMath::addf32(out.spawn.x, 10.0f);
	return true;
}

inline bool namedPair(const Lookup &lookup, const std::pair<std::string, std::string> &names, Positions &out)
{
	// Resolved reinforcement records retain names, including a start whose
	// rally is missing. This reproduces start() after save/load as well.
	const std::string prefix = "Player_", suffix = "_Start";
	if (names.first.compare(0, prefix.size(), prefix) == 0 && names.second == names.first + "_Rally" &&
		names.first.size() > prefix.size() + suffix.size() && names.first.compare(names.first.size() - suffix.size(), suffix.size(), suffix) == 0)
	{
		const Coord3D *spawn = lookup(names.first);
		if (!spawn)
			return false;
		out = { *spawn, *spawn, names, true };
		if (const Coord3D *gather = lookup(names.second))
			out.gather = *gather;
		else
			out.spawn.x = SimMath::addf32(out.spawn.x, 10.0f);
		return true;
	}
	return pair(lookup, names.first, names.second, out);
}

inline bool walkOn(const Lookup &lookup, const std::string &region, Positions &out)
{
	return pair(lookup, "WOTRSpawnPoint_" + region + "_1", "WOTRGatherPoint_" + region + "_1", out);
}

inline float squaredGatherDistance(const Positions &a, const Positions &b)
{
	const float dx = SimMath::subf32(a.gather.x, b.gather.x), dy = SimMath::subf32(a.gather.y, b.gather.y);
	return SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
}

// RW 0x7824FD (distance helper RW 0x781D95); BFME2 ArmyPlacer.cpp
// RVA 0x380014: unordered connection pairs, GATHER distance, last equal pair;
// only side 0/1 is accepted. A zero-distance valid pair is still a pair.
inline bool farthestPair(const Lookup &lookup, const std::vector<std::string> &connections, int side, Positions &out)
{
	requireConnections(connections);
	if (side != 0 && side != 1)
		return false;
	bool found = false;
	float highest = 0.0f;
	for (size_t i = 0; i < connections.size(); ++i)
	{
		Positions first;
		if (!walkOn(lookup, connections[i], first))
			continue;
		for (size_t j = i + 1; j < connections.size(); ++j)
		{
			Positions second;
			if (!walkOn(lookup, connections[j], second))
				continue;
			const float distance = squaredGatherDistance(first, second);
			if (distance >= highest)
			{
				highest = distance;
				out = side == 0 ? first : second;
				found = true;
			}
		}
	}
	return found;
}

// RW 0x781E6E; BFME2 ArmyPlacerFarthestSpawnPoint.cpp RVA 0x37F985:
// start 0's GATHER coordinate, last equal candidate, strictly positive cost.
inline bool farthestFromStart(const Lookup &lookup, const std::vector<std::string> &connections, Positions &out)
{
	requireConnections(connections);
	Positions home;
	if (!start(lookup, 0, home))
		return false;
	float highest = 0.0f;
	for (const std::string &region : connections)
	{
		Positions candidate;
		if (!walkOn(lookup, region, candidate))
			continue;
		const float distance = squaredGatherDistance(home, candidate);
		if (distance >= highest)
		{
			highest = distance;
			out = candidate;
		}
	}
	return highest > 0.0f;
}

inline bool resolve(const LivingWorldArmySpawnPlan &plan, bool reinforcement, const Lookup &lookup, Positions &out)
{
	using P = LivingWorldArmySpawnPlan;
	requireConnections(plan.connections);
	bool found = false;
	switch (plan.method)
	{
	case P::NORMAL_NEUTRAL: found = farthestPair(lookup, plan.connections, plan.side, out); break;
	case P::NORMAL_FRIENDLY: found = reinforcement ? walkOn(lookup, "DefenderReinforcements", out) : start(lookup, 0, out); break;
	case P::NORMAL_HOSTILE: found = farthestFromStart(lookup, plan.connections, out); break;
	case P::AT_HOME:
		found = reinforcement ? walkOn(lookup, "DefenderReinforcements", out) : start(lookup, 0, out);
		break;
	case P::WALK_ON: found = walkOn(lookup, plan.originRegion, out); break;
	default: return false; // a legacy caller is handled and reported by StartingBase
	}
	// RW 0x80F8CB..0x80F8FC: this retry is absent from BFME2 RVA 0x40CCC6.
	if (!found && (plan.method == P::AT_HOME || plan.method == P::WALK_ON))
		found = plan.friendlyOwner ? walkOn(lookup, "DefenderReinforcements", out) : farthestFromStart(lookup, plan.connections, out);
	if (found && !reinforcement && !out.usedStart)
	{
		// RW 0x80F91B..0x80F9C6: initial armies stand on gather and face
		// ten units along the normalised spawn->gather direction (z unchanged).
		float dx = SimMath::subf32(out.gather.x, out.spawn.x), dy = SimMath::subf32(out.gather.y, out.spawn.y);
		const float dz = SimMath::subf32(out.gather.z, out.spawn.z);
		const float length2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dz, dz)), SimMath::mulf32(dx, dx));
		if (length2 != 0.0f)
		{
			const float inverse = BFME2_Inverse_Sqrt(length2);
			dx = BFME2_Mul24(dx, inverse);
			dy = BFME2_Mul24(dy, inverse);
		}
		out.spawn = out.gather;
		out.gather.x = SimMath::addf32(out.spawn.x, SimMath::mulf32(dx, 10.0f));
		out.gather.y = SimMath::addf32(out.spawn.y, SimMath::mulf32(dy, 10.0f));
	}
	return found || start(lookup, 1, out); // RW 0x80F9D1, no initial gather adjustment on this final fallback
}
} // namespace ArmySpawnPositions
