// OpenBFME retail test fixture for lane COMBAT-2: two sides (enemies) of the named factions on a flat arena, the retail economy and the AI of the install, inside the shared retail
// object world's context (the same fixture as test_hud_combat_retail.cpp's Arena, which lives in that file's anonymous namespace).
#pragma once

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <memory>
#include <string>
#include <vector>

namespace structtest
{
using hudtest::SharedWorld;
using hudtest::shared;

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context; // the stores (weapons, armour, locomotors) are read inside the world's context scope
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, int cells = 120)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(cells, cells)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(7);
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
	// a new object of `side`; its footprint enters the pathfinder like a placed structure's (RW 0x62E192)
	Object *place(const char *templateName, int side, float x, float y, bool footprint = true)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(side == 0 ? 0.0f : 3.14159265f);
		if (footprint && o->isKindOfName("STRUCTURE"))
		{
			ai->addObjectToPathfindMap(*o);
		}
		return o;
	}
	size_t members(Object *horde) { return horde->getContain() ? horde->getContain()->getContainCount() : 0; }
	// the pathfinder holds an obstacle at world (x, y)
	bool obstacleAt(float x, float y)
	{
		PathfindCell *c = ai->pathfinder().getCell(LAYER_GROUND, (int)(x / 10.0f), (int)(y / 10.0f));
		return c && c->getType() == PathfindCell::CELL_OBSTACLE;
	}
};
} // namespace structtest
