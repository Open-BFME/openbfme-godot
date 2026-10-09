// OpenBFME unit tests. GPL-3.0.
// Fixture for the live object layer (lane LOGIC-1): the object model World (ModuleFactory with the full registry, ThingFactory), a
// PlayerTemplateStore filled from INI text, a skirmish PlayerList, a GameLogic, and test module classes bound over registry classes:
//   ScriptModule    an UpdateModule + CreateModuleInterface + DestroyModuleInterface whose callbacks are std::functions and that records
//                   every lifecycle call in a shared event log (module name, event, frame)

#pragma once

#include "Common/Upgrade.h"
#include "ObjectTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Object/Object.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace logictest
{

struct EventLog
{
	std::vector<std::string> events;
	void add(const std::string &e) { events.push_back(e); }
	size_t count(const std::string &e) const
	{
		size_t n = 0;
		for (const std::string &x : events)
		{
			n += x == e ? 1 : 0;
		}
		return n;
	}
	bool has(const std::string &e) const { return count(e) != 0; }
	int indexOf(const std::string &e) const
	{
		for (size_t i = 0; i < events.size(); ++i)
		{
			if (events[i] == e)
			{
				return (int)i;
			}
		}
		return -1;
	}
};

// per-class script of a ScriptModule: what update() returns and does
struct ModuleScript
{
	std::function<UpdateSleepTime(Object &self, GameLogic &logic)> update; ///< default: UPDATE_SLEEP_NONE
	SleepyUpdatePhase phase = PHASE_NORMAL;
	UnsignedInt initialNextCall = 0;                                        ///< 0 = leave to registerObject
	DisabledMaskType processWhenDisabled = DISABLEDMASK_NONE;
	bool hasCreate = false, hasDestroy = false;
	std::function<void(class ScriptModule &)> onObjectCreatedHook; ///< runs inside onObjectCreated
	std::function<void(class ScriptModule &)> onDeleteHook;        ///< runs inside onDelete
};

class ScriptModule : public UpdateModule, public CreateModuleInterface, public DestroyModuleInterface
{
public:
	ScriptModule(Thing *thing, const ModuleData *data, const std::string &className, std::shared_ptr<ModuleScript> script, EventLog *log)
		: UpdateModule(thing, data)
		, m_class(className)
		, m_script(std::move(script))
		, m_log(log)
	{
		if (m_script->initialNextCall)
		{
			friend_setNextCallFrame(m_script->initialNextCall);
		}
		if (m_log)
		{
			m_log->add("ctor:" + m_class);
		}
	}
	SleepyUpdatePhase getUpdatePhase() const override { return m_script->phase; }
	UpdateSleepTime update() override
	{
		++calls;
		lastFrame = getObject()->logic().getFrame();
		if (m_log)
		{
			m_log->add("update:" + m_class + "#" + std::to_string(getObject()->getID()) + "@" + std::to_string(lastFrame) + "." + std::to_string(getObject()->logic().getLastPhase()));
		}
		return m_script->update ? m_script->update(*getObject(), getObject()->logic()) : UPDATE_SLEEP_NONE;
	}
	DisabledMaskType getDisabledTypesToProcess() const override { return m_script->processWhenDisabled; }
	CreateModuleInterface *getCreate() override { return m_script->hasCreate ? this : nullptr; }
	DestroyModuleInterface *getDestroy() override { return m_script->hasDestroy ? this : nullptr; }
	void onCreate() override
	{
		if (m_log)
		{
			m_log->add("onCreate:" + m_class);
		}
	}
	void onBuildComplete() override {}
	void onDestroy() override
	{
		if (m_log)
		{
			m_log->add("onDestroy:" + m_class);
		}
	}
	void onObjectCreated() override
	{
		Object *o = getObject();
		modulesSeenAtCreated = o->modules().size();
		inListAtCreated = o->logic().getFirstObject() == o || o->getPrevObject() || o->getNextObject();
		if (m_log)
		{
			m_log->add("onObjectCreated:" + m_class + " modules=" + std::to_string(modulesSeenAtCreated) + (inListAtCreated ? " inList" : " notInList"));
		}
		if (m_script->onObjectCreatedHook)
		{
			m_script->onObjectCreatedHook(*this);
		}
	}
	void onDrawableBoundToObject() override
	{
		if (m_log)
		{
			m_log->add("onDrawableBound:" + m_class);
		}
	}
	void onDelete() override
	{
		if (m_log)
		{
			m_log->add("onDelete:" + m_class);
		}
		if (m_script->onDeleteHook)
		{
			m_script->onDeleteHook(*this);
		}
	}
	// public wake for tests (setWakeFrame is protected)
	void wake(UpdateSleepTime delay) { setWakeFrame(getObject(), delay); }

	unsigned calls = 0;
	UnsignedInt lastFrame = 0;
	size_t modulesSeenAtCreated = 0;
	bool inListAtCreated = false;

private:
	std::string m_class;
	std::shared_ptr<ModuleScript> m_script;
	EventLog *m_log;
};

const char kPlayerTemplates[] =
	"PlayerTemplate FactionNeutral\n"
	"  Side = Neutral\n"
	"End\n"
	"PlayerTemplate FactionA\n"
	"  Side = Alpha\n"
	"  DisplayName = SIDE:ALPHA\n"
	"  PlayableSide = Yes\n"
	"  StartMoney = 1500\n"
	"  PreferredColor = R:255 G:0 B:128\n"
	"  StartingBuilding = AlphaKeep\n"
	"End\n"
	"PlayerTemplate FactionB\n"
	"  Side = Beta\n"
	"  PlayableSide = Yes\n"
	"  StartMoney = 0\n"
	"  PreferredColor = R:0 G:64 B:255\n"
	"  StartingBuilding = BetaKeep\n"
	"  Evil = Yes\n"
	"End\n";

class RecordingClientHooks : public ObjectClientHooks
{
public:
	explicit RecordingClientHooks(EventLog &log)
		: m_log(log)
	{
	}
	void objectCreated(Object &obj) override { m_log.add("client:created#" + std::to_string(obj.getID())); }
	void objectDestroyed(Object &obj) override { m_log.add("client:destroyed#" + std::to_string(obj.getID())); }

private:
	EventLog &m_log;
};

// the upgrades the logic, production and economy tests name (UPGRADE-1: names resolve through TheUpgradeCenter)
inline const char *const kTestUpgrades =
	"Upgrade Upgrade_Barracks2\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_Basic\n  Type = PLAYER\nEnd\n"
	"Upgrade Upgrade_Harvest\n  Type = PLAYER\nEnd\n"
	"Upgrade Upgrade_Insurance\n  Type = PLAYER\nEnd\n"
	"Upgrade Upgrade_Anything\n  Type = PLAYER\nEnd\n"
	"Upgrade Upgrade_X\n  Type = PLAYER\nEnd\n";

struct LogicWorld
{
	EventLog log;                 // before the logic: modules and hooks log into it while the logic deletes its objects
	RecordingClientHooks hooks;
	std::map<std::string, std::shared_ptr<ModuleScript>> scripts;
	objtest::World w;
	PlayerTemplateStore templates;
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::string loadError;
	// UPGRADE-1: TheUpgradeCenter of the fixture (the veterancy upgrades and the test upgrades below; objects and players hold upgrade masks)
	UpgradeCenter upgradeCenter;
	UpgradeCenter *savedUpgradeCenter = TheUpgradeCenter;

	LogicWorld(RandomAlgorithm rng = RandomAlgorithm::ZH_CarryChain, const char *extraTemplates = nullptr)
		: hooks(log)
		, templates(w.keys)
		, players(w.keys, templates, teams)
	{
		upgradeCenter.init();
		upgradeCenter.registerBlock(w.fx.env.blocks);
		TheUpgradeCenter = &upgradeCenter;
		{
			const std::string err = w.load(kTestUpgrades, INI_LOAD_OVERWRITE, "upgrade.ini");
			REQUIRE_MESSAGE(err.empty(), err);
		}
		templates.registerBlock(w.fx.env.blocks);
		loadError = w.load(std::string(kPlayerTemplates) + (extraTemplates ? extraTemplates : ""), INI_LOAD_OVERWRITE, "players.ini");
		SkirmishSetup setup;
		setup.players.push_back({ "Alice", "FactionA", true, 0, 0, 0 });
		setup.players.push_back({ "Bob", "FactionB", false, 1, 0, 1 });
		setup.defaultStartingCash = 2000;
		players.setupSkirmish(setup);
		logic = std::make_unique<GameLogic>(w.things, w.modules, players, rng);
		logic->random().seedRandom(1);
	}

	// Binds a scripted runtime class over a registered class name (type behavior)
	std::shared_ptr<ModuleScript> bind(const std::string &className)
	{
		auto script = std::make_shared<ModuleScript>();
		scripts[className] = script;
		EventLog *l = &log;
		w.modules.bindModuleProc(className, MODULETYPE_BEHAVIOR, [className, script, l](Thing *t, const ModuleData *d, const ModuleFactory::ModuleTemplate &) {
			return std::unique_ptr<Module>(new ScriptModule(t, d, className, script, l));
		});
		return script;
	}

	Object *make(const std::string &templateName, Team *team = nullptr, ObjectID id = INVALID_ID)
	{
		const ThingTemplate *t = w.get(templateName);
		REQUIRE_MESSAGE(t != nullptr, "template " << templateName);
		return logic->newObject(t, team, ObjectStatusMaskType{}, id);
	}
	Team *teamOf(const std::string &player) { return players.findPlayerWithName(player)->getDefaultTeam(); }
	~LogicWorld() { TheUpgradeCenter = savedUpgradeCenter; }
	// several worlds alive: the one about to be used selects its center
	void activateUpgrades() { TheUpgradeCenter = &upgradeCenter; }
};

} // namespace logictest
