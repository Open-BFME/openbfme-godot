// OpenBFME unit tests (lane MODULES-2): the retail game the emotion and area-scan tests share. GPL-3.0.
#pragma once

#include "HudTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include <memory>
#include <string>
#include <vector>

namespace mod2test
{
// a retail game on a flat synthetic terrain with TheAI (the move tests' setup): Men (P0) and Mordor (P1)
struct RetailGame
{
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	explicit RetailGame(hudtest::SharedWorld &s, unsigned seed = 3)
		: players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(100, 100)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Men", "FactionMen", true, 0, 0, 0 });
		setup.players.push_back({ "Mordor", "FactionMordor", false, 1, 0, 1 });
		setup.defaultStartingCash = 5000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic.combat().setAutoAcquireEnabled(false); // the emotions are the subject, not a fight
		std::string err;
		GameLogicSettings settings;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, settings, &err), err);
		logic.settings() = settings;
		logic.random().seedRandom(seed);
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the terrain's extent (RW 0x62FCCD)
	}
	~RetailGame()
	{
		logic.reset();
		ai.reset();
	}
	Object *make(const char *name, const char *player, float x, float y)
	{
		const ThingTemplate *tt = logic.things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Object *o = logic.newObject(tt, players.findPlayerWithName(player)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic.runLogicFrame();
		}
	}
	std::vector<Object *> members(Object *horde)
	{
		std::vector<Object *> out;
		if (ContainModuleInterface *c = horde->getContain())
		{
			if (const ContainModuleInterface::ContainedItemsList *l = c->getContainedItemsList())
			{
				out.assign(l->begin(), l->end());
			}
		}
		return out;
	}
};

} // namespace mod2test
