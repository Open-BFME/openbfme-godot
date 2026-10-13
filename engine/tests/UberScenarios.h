// OpenBFME unit tests. GPL-3.0.
// Lane UBER-1: the Uber performance mode's benchmark battle (docs/UBER.md): 8 players on map mp fall back 8p, two teams of four, armies of hordes
// with N units in total (10k, 20k, 40k: horde members count as units) facing each other across the middle of the map, every horde ordered to
// attack the enemy horde facing it. Shared by the bench (test_uber1_bench.cpp) and the determinism tests (test_uber1.cpp). Test-only.

#pragma once

#include "Common/SimulationProfile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ArchiveFileSystem;
class RetailObjectWorld;
struct GameLogicSettings;
struct MapObjectOptions;

namespace uberbench
{
struct PhaseTimes
{
	double ms[7] = { 0, 0, 0, 0, 0, 0, 0 }; ///< wall time of phase 1..6 ([0] unused)
};

struct Run
{
	// lane UBER-1 (review): the population is the living troops of GameClient/FrameCensus.h (KindOf INFANTRY, CAVALRY or MONSTER: every horde member counts,
	// the horde controllers do not)
	int troopsStart = 0, troopsEnd = 0, troopsMin = 0;
	double troopsMean = 0.0;               ///< the sustained combat population: the mean of the living troops after each measured frame
	int battalionsStart = 0;               ///< the living hordes at the start
	int troopsMade = 0;                    ///< the living troops right after the order (before the warm-up)
	std::vector<double> tickMs;            ///< logic + state hash of each frame: what the logic owner spends per tick of a recorded game
	std::uint64_t hashDispatches = 0;      ///< the logic job pool's dispatches made by the state hashes alone
	std::uint64_t hashWorkerChunks = 0;    ///< of their chunks, those a worker thread ran
	std::vector<double> wallMs;        ///< wall time of each logic frame (the parallel parts count once, as the player waits for them)
	std::vector<double> cpuMs;         ///< the calling thread's CPU time of each frame
	std::vector<PhaseTimes> phases;    ///< per frame, the wall time of each of the six phases
	std::vector<double> hashMs;        ///< the wall time of the state hash after each frame
	std::vector<std::uint32_t> hashes; ///< the state hash after each frame
	size_t objectsBefore = 0;          ///< objects of the map before the armies
	size_t units = 0;                  ///< objects the armies added (hordes, their members, banner carriers)
	size_t objectsStart = 0, objectsEnd = 0;
	double spawnMs = 0.0;              ///< the wall time of making the armies
	double processCpuMs = 0.0;         ///< the process's CPU time over the frames (all threads): / wall = cores used
	double framesWallMs = 0.0;
};

// a battle of at least `units` living troops with `profile`: `warmUp` logic frames after the order (each hashed like a recorded game's, not measured:
// the armies march into contact), then `frames` measured frames (hash after every frame when `hashEveryFrame`)
void runBattle(ArchiveFileSystem &fs, RetailObjectWorld &world, const MapObjectOptions &options, const GameLogicSettings &settings, int units, int frames,
	SimulationProfile profile, bool hashEveryFrame, Run &out, int warmUp = 0);
} // namespace uberbench
