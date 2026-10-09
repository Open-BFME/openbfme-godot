// OpenBFME. GPL-3.0.
//
// LuaScriptEngine: the embedded Lua scripting of RotWK (spec lua-scripting.md; PLAN rule 8). Port of the retail LuaScriptEngine translation
// unit, RotWK game.dat 0x733F27-0x73A400 (target facts, caveat S-001: the TU cannot be compared with BFME2 1.06 and may contain community
// patches, so what is documented is "what this game.dat does"); Open-BFME-1 LuaScriptEngine*.cpp is the donor for the shapes of the pieces.
//
//   * two Lua states (spec 2.2): the LOGIC state runs Data\Scripts\Scripts.lua and the object event handlers, the DRAWABLE state runs the
//     BeginScript bodies of the draw modules. Both are opened like retail's (LuaRuntime) and carry their own registered C functions:
//     41 in the logic state, 21 in the drawable state, in the retail registration order (LuaBindings.cpp, tables in LuaBindingNames.h);
//   * Scripts.lua and ScriptEvents.xml are (re)loaded at every game start, then the map folder's overlay, if any (startNewGame);
//   * event lists bind to objects by AILuaEventsList (the object holds the LuaEventList pointer: findEventList), events are dispatched
//     synchronously from the engine's call sites, nested to a depth of 11 handlers (dispatch);
//   * objects reach Lua as empty tables whose payload is the object id, in globals "ObjID#%08x" (registerObject, pushObject).
//
// Everything outside the Lua library, the XML reader and this class is another lane's: the object table, AI, players, upgrades, powers,
// weapons, the partition manager, audio and the map script engine are reached through LuaGameHost (LuaHost.h; an unimplemented callee is
// the reported stop S-124); the drawable's side of the CurDrawable* functions through LuaDrawableContext.
//
// Retail quirks reproduced (spec section 8): Q1-Q11, and the ones the oracle and the disassembly added (spec corrections in the report).

#pragma once

#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "GameLogic/ScriptEngine/LuaDrawableState.h"
#include "GameLogic/ScriptEngine/LuaHost.h"
#include "GameLogic/ScriptEngine/LuaRuntime.h"
#include "GameLogic/ScriptEngine/LuaScriptEvents.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Where the engine reads Data\Scripts\Scripts.lua, Data\Scripts\ScriptEvents.xml and a map folder's overlay from
class LuaFileSource
{
public:
	virtual ~LuaFileSource() = default;
	virtual bool exists(const std::string &path) = 0;                       // RW 0xA14AEB
	virtual bool read(const std::string &path, std::string *bytes) = 0;     // RW 0xA149A2 (flags 0x41)
};

class LuaScriptEngine
{
public:
	struct Config
	{
		NameKeyGenerator *keys = nullptr;           // TheNameKeyGenerator (RW 0xDD90E4)
		GameLogicRandom *logicRandom = nullptr;     // the logic RNG: GetRandomNumber, ObjectTestCanSufferFear (spec 3.4)
		LuaGameHost *host = nullptr;                // the logic bindings' callees
		std::function<std::uint32_t()> frame;       // GetFrame of the drawable state (the logic frame): defaults to host->logicFrame()
		// The profile (docs/PLAN.md): false = retail-compatible: where retail FAULTS (type(<boolean>), S-041) the script is aborted and the fault counted;
		// true = Enhanced: the repaired behaviour. Never defaulted on.
		bool enhancedProfile = false;
	};
	explicit LuaScriptEngine(const Config &config);
	~LuaScriptEngine();
	LuaScriptEngine(const LuaScriptEngine &) = delete;
	LuaScriptEngine &operator=(const LuaScriptEngine &) = delete;

	// ---- lifecycle (RW vtable 0xC249E8) ----
	// init() (slot 1, RW 0x737654): creates the drawable state and registers its 21 functions; runs once, reset() never closes it
	void init();
	// reset() (slot 9, RW 0x739585): closes the logic state and clears the event vectors; the drawable state is left alone
	void reset();
	// RW 0x739C10 + 0x73A0B9-0x73A0CD + the map overlay 0x73A0D5: creates the logic state if absent (41 functions), runs Scripts.lua, reads
	// ScriptEvents.xml; then, when `mapFolder` is not empty, `<mapFolder>\Scripts.lua` and `<mapFolder>\ScriptEvents.xml` if they exist
	// (the XML with keepOpen: a list of the same name replaces the earlier one). Returns false when a base file could not be read (retail
	// loads nothing silently; the port reports S-127). Calling it again on a live state re-runs Scripts.lua and appends the lists again (G15).
	bool startNewGame(LuaFileSource &files, const std::string &mapFolder);
	// the same with the two texts already in hand (tests, tools): Scripts.lua text, ScriptEvents.xml text
	void loadLogicScripts(const std::string &scriptsLua, const std::string &scriptEventsXml);

	LuaRuntime *logic() { return m_logic.get(); }
	LuaRuntime *drawable() { return m_drawable ? &m_drawable->runtime() : nullptr; }
	LuaDrawableState *drawableState() { return m_drawable.get(); }
	LuaEventRegistry &events() { return m_registry; }
	const LuaEventRegistry &events() const { return m_registry; }
	// compatibility faults of both states (a script aborted where retail faults): non-zero means some script did not complete compatibly
	size_t compatibilityFaults() const;
	LuaReportSink &reports() { return m_sink; }
	const LuaReportSink &reports() const { return m_sink; }
	LuaGameHost &host() { return *m_host; }
	NameKeyGenerator &keys() { return *m_keys; }
	GameLogicRandom &logicRandom() { return *m_logicRandom; }

	// ---- the object side (spec 4.2) ----
	// RW 0x735941 register(obj): only an object with an AI or ForceLuaRegistration; creates / keeps the handle table "ObjID#%08x"
	void registerObject(const LuaObjectInfo &obj);
	// RW 0x735A1D unregister(obj)
	void unregisterObject(const LuaObjectInfo &obj);
	// the engine call sites of register / unregister (spec 4.2, names inferred: G3): an object entering the world registers (RW 0x68E31F, which also sets
	// Object+0x474), leaving unregisters (RW 0x68C18F), a change of owner unregisters then registers (RW 0x69954A), and the AI's load-post-process
	// registers it again (RW 0x667BEF). OnCreated is dispatched after the object entered the world (RW 0x628882 draws GetGameLogicRandomValue(1, 999)
	// first; that draw is GameLogic's, not the script engine's).
	void objectEnteredWorld(const LuaObjectInfo &obj) { registerObject(obj); }
	void objectLeftWorld(const LuaObjectInfo &obj) { unregisterObject(obj); }
	void objectChangedOwner(const LuaObjectInfo &obj) { unregisterObject(obj); registerObject(obj); }
	// RW 0x628882 Object::sendObjectCreated, the ONE function every creation of a full object (map object, bridge, horde member, and later
	// LOGIC-1's live objects) goes through. Retail order: (1) GetGameLogicRandomValue(1, 999) with source line 0x19A7 (RW 0x628892..0x6288A5,
	// unconditional: no event list, handler or Lua state is needed for it; the value is the drawable's seed); (2) `bindDrawable` (the Drawable is
	// created and bound, RW 0x6288D3 / 0x6288DC); (3) the object enters the script world (RW 0x68E31F; the order after the drawable is inferred,
	// spec G7) and OnCreated is dispatched (RW 0x6288FA). `engine` and `obj` may be null (no script state / no event list): the draw and the
	// drawable still happen. `rng` is the explicitly selected logic generator (S-080 stays the stop of its algorithm and seed).
	static int sendObjectCreated(GameLogicRandom &rng, LuaScriptEngine *engine, const LuaObjectInfo *obj, const std::function<void()> &bindDrawable);
	static constexpr int kCreationDrawLine = 0x19A7;   ///< GameLogic.cpp line of the draw (RW 0x6288A1)
	// RW 0x735AE9 pushObject(L, obj): null -> nil; unregistered or mismatched -> nil AND a second nil (quirk Q5)
	void pushObject(lua_State *L, const LuaObjectInfo *obj);
	static std::string objectGlobalName(int id); ///< "ObjID#%08x" (RW 0xC244BC)

	// ---- dispatch (spec 4.3) ----
	// RW 0x73449A slot keys through 0x7379CB(idx, object, args): the call the engine's sites use
	void dispatchInternal(LuaInternalEvent event, const LuaObjectInfo &self, const LuaEventArgs &args = LuaEventArgs());
	// RW 0x735F53 dispatch(event, self, args)
	void dispatch(const LuaEventRef &event, const LuaObjectInfo &self, const LuaEventArgs &args);
	// RW 0x735388 FindEvent by name (creates the NameKey like retail); invalid when no declared event has the name
	LuaEventRef findEvent(const std::string &name) { return m_registry.findEvent(name); }
	// RW 0x7396E8: the object's AILuaEventsList; null for "None" and unknown names
	const LuaEventList *findEventList(const std::string &name) const { return m_registry.findEventList(name); }
	int dispatchDepth() const { return m_depth; }

	// ModelCondition events (spec 4.4, RW 0x663E32 inside the AI update): edge triggered against the snapshot the caller keeps per object.
	// With frame < 2 the snapshot is only taken; otherwise a record fires when the flags match it and the snapshot did not; then the
	// snapshot is replaced. (The AI lane calls this from AIUpdateInterface::update.)
	void updateModelConditionEvents(const LuaObjectInfo &self, const ModelConditionFlags &flags, ModelConditionFlags *snapshot, std::uint32_t frame);

	// ---- the drawable state (spec 4.7) ----
	// RW 0x734D62: runs one BeginScript body in the drawable state with `ctx` as the current context. *result is the string the script
	// returned ("" for nothing); returns the Lua status (0 = ok). Errors are swallowed like retail's (the alert is in drawable()->alerts()).
	int runDrawableScript(const std::string &script, const std::string &chunkName, LuaDrawableContext *ctx, std::string *result);
	LuaDrawableContext *drawableContext() const { return m_drawable ? m_drawable->context() : nullptr; } ///< LuaScriptEngine+0x9C

	// the console / debug step of retail is inert; this is the part of it the dispatcher consults (RW global 0xDE7808)
	bool debugStepActive() const { return m_debugStep != nullptr; }

	// ObjectSpy records (RW 0x6625A4): (spied object id) -> (event key, spy key)
	struct SpyRecord { NameKeyType eventKey; NameKeyType spyKey; };
	const std::vector<std::pair<int, SpyRecord>> &spyRecords() const { return m_spies; }

	// used by the bindings
	std::uint32_t frame() const;                 ///< GetFrame: the logic frame
	void addSpy(int objectId, NameKeyType eventKey, NameKeyType spyKey) { m_spies.push_back({ objectId, { eventKey, spyKey } }); }

private:
	void reportEventSources();
	void createLogicState();
	void createDrawableState();
	void spyNotify(NameKeyType eventKey, const LuaObjectInfo &self);

	Config m_config;
	NameKeyGenerator *m_keys;
	GameLogicRandom *m_logicRandom;
	LuaGameHost *m_host;
	LuaReportSink m_sink;
	LuaEventRegistry m_registry;
	std::unique_ptr<LuaRuntime> m_logic;
	std::unique_ptr<LuaDrawableState> m_drawable;
	int m_depth = 0;                         ///< LuaScriptEngine+0xD4
	lua_State *m_debugStep = nullptr;        ///< global 0xDE7808
	std::vector<std::pair<int, SpyRecord>> m_spies;
};
