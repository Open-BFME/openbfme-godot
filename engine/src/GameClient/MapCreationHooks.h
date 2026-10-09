// OpenBFME. GPL-3.0.
//
// Creation hooks of map objects (lane MAPOBJ-1, stop S-110): a full object is sent the Lua event OnCreated when it is created, and the
// handler scripts do things the initial state needs (RangerFunctions -> OnGondorArcherCreated hides the sub object FireArowTip
// permanently; SubObjectsUpgrade shows it again when the fire arrow upgrade is granted). This lane does not run them. It finds,
// per template, which OnCreated handlers retail would run, so the report names every object whose initial state lacks their effect.
//
// Sources (retail data, scanned by name only; no engine code decides which scripts exist):
//   * the object template's AILuaEventsList = <list name> line in an AI behavior module body (215 INI files);
//   * data\scripts\scriptevents.xml: <EventList Name Inherit> with <EventHandler EventName="OnCreated" ScriptFunctionName=...>;
//   * data\scripts\scripts.lua: `function Name(self)` ... a line starting with `end`; its calls are recorded by name, and
//     ObjectHideSubObjectPermanently(self, "X", true) gives the permanently hidden sub object X (false: shown) (commented-out calls are not counted).
// Which handler wins when a list and the list it inherits both define OnCreated: the CHILD'S REPLACES the inherited one (RW 0x733FAF, the
// handler insert of the EventList reader, compares the key of every existing handler and overwrites an equal one; the inherited handlers are copied
// into the list first, RW 0x733CE6). The scan here still lists every handler along the chain, child first (a superset: it names the handlers
// the lists define); what RUNS is decided by the real event registry in MapObjectRuntime (LuaScriptEngine), which the report counts separately.
// The hooks are run by MapObjectRuntime::build (lane LUA-1), not by the object loop.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/Thing/ThingTemplate.h"

#include <map>
#include <string>
#include <vector>

struct CreationScriptData
{
	struct EventList
	{
		std::string name;
		std::string inherit;                    ///< "" = none
		std::vector<std::string> onCreated;     ///< ScriptFunctionName of each OnCreated handler of this list itself
	};
	struct LuaFunction
	{
		std::vector<std::string> permanentHides; ///< sub objects hidden by ObjectHideSubObjectPermanently(self, "X", true), in order
		std::vector<std::string> permanentShows; ///< the same call with false (shows the sub object permanently)
		std::vector<std::string> calls;          ///< every function called by name (comments excluded), in order, with repeats
	};
	std::map<std::string, EventList> eventLists;
	std::map<std::string, LuaFunction> luaFunctions;
	bool loaded = false;          ///< the report-only inventory scan succeeded (eventLists / luaFunctions are filled)
	bool rawLoaded = false;       ///< the two files were read: MapObjectRuntime loads them into the real Lua script engine, which decides what runs; independent of the scan
	std::string inventoryError;   ///< why the inventory scan failed (it is report-only; a valid script the scanner cannot read still runs)
	std::string xmlText, luaText; ///< the two files as read
};

// One OnCreated hook of a template that this lane does not run.
struct MapCreationHook
{
	std::string module;                       ///< module tag of the AI module that names the list
	std::string eventList;                    ///< AILuaEventsList value
	std::vector<std::string> functions;       ///< OnCreated handler functions along the Inherit chain, child list first
	std::vector<std::string> permanentHides;  ///< sub objects the functions hide permanently, in order, no repeats
	std::vector<std::string> permanentShows;  ///< sub objects the functions show permanently (ObjectHideSubObjectPermanently(self, "X", false))
};

namespace MapCreationHooks
{
// The AILuaEventsList of each AI behavior module of the template, from the template data alone (no script inventory): module tag and list name.
// The real event registry resolves the name, so a list or a handler the interim scanner cannot read is still found.
std::vector<MapCreationHook> templateEventLists(const ThingTemplate &tmpl);
// false + *error when the xml or the lua text has no list / no function, or a function has no closing `end` line.
bool scan(const std::string &scriptEventsXml, const std::string &scriptsLua, CreationScriptData &out, std::string *error);
// data\scripts\scriptevents.xml and data\scripts\scripts.lua of the mounted archives
bool load(ArchiveFileSystem &fs, CreationScriptData &out, std::string *error);

// The OnCreated hooks of `tmpl` (final override). Problems (an unknown list name, a handler function scripts.lua does not define,
// a list whose Inherit chain does not end) are appended to *problems, never dropped.
std::vector<MapCreationHook> templateHooks(const ThingTemplate &tmpl, const CreationScriptData &data, std::vector<std::string> *problems);
} // namespace MapCreationHooks
