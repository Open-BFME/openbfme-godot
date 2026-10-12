// OpenBFME test helper (lanes STEALTH-1 / STEALTH-2): two enemy sides (A FactionMen, B FactionMordor) on a flat arena with the retail economy and AI data (the
// COMBAT-1 arena) for the stealth retail tests, which share the HUD tests' retail world.

#pragma once

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"

#include <memory>
#include <string>
#include <vector>

namespace stealthtest
{
// two enemy sides on a flat arena with the retail economy and AI data (the COMBAT-1 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	explicit Arena(hudtest::SharedWorld &s, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(seed);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = hudtest::shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
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
};

// lane SPELL-3 (review r1): a map tree beside a test unit. The stealth tree query reads the logic's tree list (TerrainTrees, RW 0x67F52B), so the tree goes
// there (a retail optimized tree template on a grid covering the arena) and, as the map loop does, into the invisibility manager's hashed position list
inline void addTerrainTree(GameLogic &logic, const Coord3D &pos)
{
	if (!logic.trees().hasExtent())
	{
		logic.trees().setExtent(-1000.0f, -1000.0f, 10000.0f, 10000.0f);
	}
	const ThingTemplate *t = hudtest::shared().world->things().findTemplate("GenericInteractableOptimizedTree");
	REQUIRE(t != nullptr);
	REQUIRE(logic.trees().addTree(*t, pos, (std::uint32_t)logic.trees().records().size() + 1u));
	logic.invisibility().addTree(pos);
}

inline std::vector<Object *> membersOf(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			for (Object *m : *items)
			{
				out.push_back(m);
			}
		}
	}
	return out;
}

} // namespace stealthtest
