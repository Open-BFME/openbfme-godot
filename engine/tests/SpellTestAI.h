// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-2: TheAI for the spell book test games (a flat synthetic terrain of 8000 x 9000 units under an AIWorld). The effect objects of the spell book
// fire weapons (FireWeaponUpdate) and the combat geometry is the pathfinder's, so a game that casts needs one. Declare it after the GameLogic it serves, call attach()
// once the logic exists and reset the logic in the fixture's destructor (the objects leave the pathfinder while the world exists; MoveTestUtil.h).

#pragma once

#include "doctest.h"
#include "PathfindTestUtil.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"

#include <memory>
#include <string>

namespace spelltest
{
struct GameAI
{
	pathtest::SyntheticTerrain terrain{ 800, 900 };
	AIWorldConfig config;
	std::unique_ptr<AIWorld> ai;

	void attach(starttest::Shared &s, GameLogic &logic)
	{
		std::string err;
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, config, &err), err);
		ai = std::make_unique<AIWorld>(logic, config, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
};
} // namespace spelltest
