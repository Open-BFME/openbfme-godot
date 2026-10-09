// OpenBFME unit tests (lane PROD-1): a small synthetic production world: a barracks (ProductionUpdate + QueueProductionExitUpdate) and units, a player
// with money and command points, the CommandButton / CommandSet of the barracks. Shared by the production tests.

#pragma once

#include "LogicTestUtil.h"

#include "EconGameData.h"

#include "Common/BuildAssistant.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/UpgradeTypes.h"

namespace prodtest
{

inline ProductionSettings defaultSettings()
{
	ProductionSettings s;
	s.loaded = true;
	s.minLowEnergyProductionSpeed = 0.5f;
	s.maxLowEnergyProductionSpeed = 0.8f;
	s.lowEnergyPenaltyModifier = 1.0f;
	s.multipleFactory = 1.0f;
	for (int i = 0; i < 8; ++i)
	{
		s.multiPlayUnitSpeedMult[i] = 1.0f;
		s.multiPlayBuildingSpeedMult[i] = 1.0f;
	}
	s.multiPlayEntries = 8;
	return s;
}

// the command point usage of a player, set to a value for a test (the live accounting adds and removes what objects cost)
inline void setUsage(Player &p, int usage) { p.commandPoints().addUsage(usage - p.commandPoints().getUsage()); }

struct ProdWorld : logictest::LogicWorld
{
	CommandStore commands;
	CommandStore *savedStore = nullptr;
	UpgradeTypeTable upgrades;
	std::string error;

	ProdWorld()
	{
		LogicModules::registerAll(w.modules);
		savedStore = TheCommandStore;
		TheCommandStore = &commands;
		commands.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		logic->productionSettings() = defaultSettings();
		upgrades.scan("Upgrade Upgrade_Barracks2\n  Type = OBJECT\nEnd\nUpgrade Upgrade_Basic\n  Type = PLAYER\nEnd\n", &error);
		logic->setUpgradeTypes(&upgrades);
		// the economy owns the command points: GameData's values (retail excerpt) and the init of every player (two live playable players: MP2 = 100 / 1000)
		std::string econError;
		REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, logic->economy().settings(), &econError), econError);
		logic->economy().initAllCommandPoints();
	}
	~ProdWorld() { TheCommandStore = savedStore; }
	// TheCommandStore is a global (ZH TheControlBar): with two worlds alive, the one about to be used must be the current one
	void activate()
	{
		TheCommandStore = &commands;
		activateUpgrades();
	}

	void load(const std::string &text)
	{
		const std::string err = w.load(text);
		REQUIRE_MESSAGE(err.empty(), err);
	}

	Player *alice() { return players.findPlayerWithName("Alice"); }
	Economy &economy() { return logic->economy(); }
	ProductionUpdateInterface *pu(Object *o) { return o->getProductionUpdate(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic->runLogicFrame();
		}
	}
};

// a barracks, a soldier (BuildTime 10 s = 50 frames, cost 100) and an archer
extern const char kBarracksObjects[];
extern const char kBarracksCommands[];

} // namespace prodtest
