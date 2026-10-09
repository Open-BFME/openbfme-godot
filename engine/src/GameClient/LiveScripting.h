// OpenBFME. GPL-3.0.
//
// LiveScripting (lane LOGIC-1, on LUA-1): the real Lua script engine of a live game and the LuaGameHost over live objects. It is the ONE
// retail creation path of the live world:
//   * GameLogic::setObjectCreatedProc(LuaScriptEngine::sendObjectCreated): every Object's initObject (RW 0x628882) makes the unconditional logic
//     draw GetGameLogicRandomValue(1, 999, "GameLogic.cpp", 0x19A7), then the drawable (DrawableManager::objectCreated: made, bound), then the
//     object enters the script world and OnCreated is dispatched to the handlers of its AILuaEventsList. MapObjectRuntime (the static map path)
//     calls the same LuaScriptEngine::sendObjectCreated, so one creation draw per object happens on either path and never on both;
//   * GameLogic::setWorldHooks(objectEnteredWorld, objectLeftWorld): the seams RW 0x69A6C6 (end of the Object constructor, after registerObject)
//     and RW 0x69A89D (destructor) call; the script engine's table of objects follows the live object list.
// The engine reads Data\Scripts\Scripts.lua and ScriptEvents.xml from the archives and the map folder's overlay (LuaScriptEngine::startNewGame).
//
// The host answers findObject, the logic frame and the drawable side (showModule / showSubObject) from live objects; every other callee a handler
// can name stays LUA-1's reported stop S-124 (no AI, upgrades, powers, emotions or partition manager exist yet). An object has an AI for the engine
// when its template names an AILuaEventsList in a behavior module (MapCreationHooks::templateEventLists); ForceLuaRegistration is not read (S-147).

#pragma once

#include "Common/NameKeyGenerator.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/LuaScriptEngine.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;

class LiveScripting : public LuaGameHost
{
public:
	LiveScripting(GameLogic &logic, ArchiveFileSystem &fs, NameKeyGenerator &keys);
	~LiveScripting() override;

	// Loads Scripts.lua / ScriptEvents.xml (+ the overlay of `mapFolder`, e.g. "maps\\map mp evendim") and installs the creation proc and the world
	// hooks into the logic. false + *error when a base file cannot be read. Must run before the first object is created.
	bool start(const std::string &mapFolder, std::string *error);
	// takes the hooks out of the logic (call while the logic is alive, before it is destroyed)
	void detach();

	LuaScriptEngine &engine() { return *m_engine; }

	struct Stats
	{
		size_t creations = 0;                  ///< sendObjectCreated calls (one creation draw each)
		size_t creationListObjects = 0;        ///< objects whose AILuaEventsList the registry resolved: registered and sent OnCreated
		size_t creationHookObjects = 0;        ///< of those, the ones whose list has an OnCreated handler
		std::map<std::string, size_t> creationHandlers; ///< OnCreated handler function -> objects
		size_t templatesWithSeveralLists = 0;  ///< objects whose template names AILuaEventsList in more than one AI module (the first is used)
		size_t worldEntries = 0, worldExits = 0;
		size_t engineEvents = 0;               ///< lane HERO-2: engine dispatches (RW 0x7379CB) that reached an object with a handler list
		size_t modelConditionChanges = 0;      ///< lane HERO-2: model condition changes the AI update handed to the ModelCondition events (RW 0x663E32)
		std::vector<std::string> errors;       ///< an AILuaEventsList that is not an EventList of ScriptEvents.xml; handler alerts
		std::vector<std::string> stops;        ///< the reports of the script engine ("[S-124] ...")
	};
	Stats stats() const;

	// LuaGameHost
	bool findObject(int id, LuaObjectInfo *out) override;
	std::uint32_t logicFrame() override;
	bool drawableShowModule(int id, const std::string &name, bool visible, bool permanent) override;
	void drawableShowSubObject(int id, const std::string &name, bool visible, bool permanent) override;
	// lane UPGRADE-1: the upgrade callees of RW 0x736DF5 / the has-upgrade binding, on TheUpgradeCenter and the live objects
	UpgradeLookup findUpgrade(const std::string &name) override;
	bool playerHasUpgradeComplete(int id, const std::string &name) override;
	void grantUpgrade(int id, const std::string &name, bool playerType) override;
	void removeUpgrade(int id, const std::string &name, bool playerType) override;
	// AUDIO-2: Lua ObjectPlaySound (RW 0x736FCD): the event attached to the object through the installed audio manager (AudioApi); false when the event does not
	// exist (or no manager is installed: AudioApi counts the call). Retail scripts ignore the returned handle
	bool playObjectSound(int id, const std::string &eventName, std::uint32_t *handle) override;
	// lane MODULES-2: Lua ObjectEnterRunAwayPanicState / CowerState / UncontrollableCowerState / AlertState (RW 0x736A69 / 0x736AE1 / 0x736BA0 / 0x736B59):
	// RW 0x68F37F(type, other, 1), the request of the object's EmotionTrackerUpdate (GameLogic/Module/EmotionModules.h)
	void enterEmotion(int id, int type, int otherId) override;
	// lane HERO-2: the bindings' partition queries (RW 0xA39340 with each binding's filter chain; LiveScripting.cpp)
	std::vector<int> objectsInRange(int id, float radius, LuaRangeOrder order, int relationship, bool notOfPlayer) override;

private:
	// the LuaObjectInfo of an object: the AI is the one of its template's first AILuaEventsList. false: the object has no list of handlers (it is
	// still created: the draw and the drawable happen, no registration, no dispatch). `count`: tally the creation stats.
	bool describe(Object &obj, LuaObjectInfo *out, bool count);

	GameLogic &m_logic;
	ArchiveFileSystem &m_fs;
	NameKeyGenerator &m_keys;
	std::unique_ptr<LuaScriptEngine> m_engine;
	struct TemplateLists
	{
		const LuaEventList *list = nullptr;
		size_t aiModules = 0;
		std::string listName;
		bool resolved = false;
	};
	std::map<const ThingTemplate *, TemplateLists> m_templates;
	Stats m_stats;
	bool m_attached = false;
};
