// OpenBFME unit tests. GPL-3.0.
// Lane BUILDCLEAR-1: a flat pathfinder terrain for the synthetic castle and wall fixtures. RotWK's castle unpack (options 5 | 0x100, RW 0x797A96) and the wall span
// (0x55) ask isLocationLegalToBuild for terrain restrictions (bit 0x01, RW 0x796A9A), which needs the pathfinder's terrain view (no terrain is an error there).
#pragma once

#include "LogicTestUtil.h"
#include "PathfindTestUtil.h"
#include "GameLogic/AI/AIWorld.h"

#include <memory>

namespace flatterrain
{
// an AIWorld over a flat SyntheticTerrain of `cells` x `cells` cells (10 units each) attached to `world`'s logic; reset the logic before it goes
struct Flat
{
	pathtest::SyntheticTerrain terrain;
	AIWorldConfig config;
	std::unique_ptr<AIWorld> ai;
	Flat(logictest::LogicWorld &world, int cells)
		: terrain(cells, cells)
	{
		config.pathfind = pathtest::testConfig();
		ai = std::make_unique<AIWorld>(*world.logic, config, world.w.fx.env.macros);
		ai->attach();
		ai->newMap(terrain);
	}
};
} // namespace flatterrain
