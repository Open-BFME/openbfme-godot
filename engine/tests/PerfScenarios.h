// OpenBFME unit tests. GPL-3.0.
// Lanes PERF-1 / PERF-2: the benchmark scenarios of test_perf1_bench.cpp (the big battle, the computer players' skirmish) shared with the determinism
// matrix of the parallel logic (test_perf2_determinism.cpp). Test-only.

#pragma once

#include "HudTestUtil.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ArchiveFileSystem;
class RetailObjectWorld;
struct GameLogicSettings;

namespace perf1
{
struct Run
{
	std::vector<double> ms;              ///< thread CPU time of each logic frame (skirmish: of each advance, which runs one frame)
	std::vector<std::uint32_t> hashes;   ///< the state hash after each frame
	long long instructions = -1;         ///< user instructions of all frames (-1: no counter)
	size_t objectsStart = 0, objectsEnd = 0, peakObjects = 0;
	double loadMs = 0.0;                 ///< skirmish: the wall time of LiveGame::load (the map load; lane PERF-2)
};

// the partition bench's armies on the rig's map, ordered to attack (two settling frames run)
void setUpBigBattle(hudtest::Rig &rig);
// the partition bench's battle on world `sh` for `frames` logic frames
void runBigBattle(hudtest::SharedWorld &sh, int frames, Run &out);
// a skirmish of medium computer players (`factions`, start positions 0.., free for all) on map mp fall back 4p (seed 7) for `frames` logic frames
void runAISkirmish(ArchiveFileSystem &fs, RetailObjectWorld &world, const MapObjectOptions &options, const GameLogicSettings &settings,
	const std::vector<std::string> &factions, int frames, Run &out);
// Men, Mordor, Elves, Isengard
const std::vector<std::string> &fourFactions();
// a world loaded the way hudtest::shared() loads one, owned by the caller (no game has run on it)
bool loadFreshWorld(hudtest::SharedWorld &w, GameLogicSettings &settings, std::string &error);
// tests/data/perf1/<name>: the stored per-frame hashes
std::vector<std::uint32_t> readHashes(const char *name);
// the first frame whose hash differs from the reference (-1: none)
int firstDifference(const std::vector<std::uint32_t> &got, const std::vector<std::uint32_t> &want);
} // namespace perf1
