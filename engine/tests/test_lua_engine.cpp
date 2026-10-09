// OpenBFME unit tests: the Lua script engine, its bindings and the dispatcher (lane LUA-1; spec lua-scripting.md 3, 4, 5, 8).
// Expected values: the retail disassembly of each function (RW 0x734xxx-0x738xxx), cited at the test; the quirks Q1-Q11 of spec section 8
// and the ones found while tracing (spec corrections noted in each test). Objects are synthetic (RecordingHost); the retail Scripts.lua is
// used in test_lua_retail.cpp.

#include "doctest.h"

#include "LuaTestUtil.h"

#include "GameLogic/ScriptEngine/LuaBindingNames.h"
#include "GameLogic/ScriptEngine/LuaStops.h"

using luatest::Rig;
using luatest::RecordingHost;
using luatest::run;

namespace
{
const char *kXml =
	"<SageLuaScriptSection>"
	"<Events><InternalEvent Name=\"OnCreated\"/><ScriptedEvent Name=\"Fear\"/></Events>"
	"<EventList Name=\"Test\">"
	"<EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnCreated1\"/>"
	"<EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"OnDamaged1\"/>"
	"<EventHandler EventName=\"OnDestroyed\" ScriptFunctionName=\"OnDestroyed1\"/>"
	"<EventHandler EventName=\"OnGenericEvent\" ScriptFunctionName=\"OnGeneric1\"/>"
	"<EventHandler EventName=\"OnBuildVariation\" ScriptFunctionName=\"OnVariation1\"/>"
	"<EventHandler EventName=\"Fear\" ScriptFunctionName=\"OnFear1\"/>"
	"<EventHandler EventName=\"OnArrived\" ScriptFunctionName=\"NotDefined\"/>"
	"</EventList></SageLuaScriptSection>";

// a log that the Lua handlers append to: a global string
const char *kLua =
	"log = ''\n"
	"function OnCreated1(self) log = log .. 'C' end\n"
	"function OnDamaged1(self, other) log = log .. 'D' if other == nil then log = log .. '-nil' end end\n"
	"function OnDestroyed1(self) log = log .. 'X' end\n"
	"function OnGeneric1(self, text) log = log .. 'G(' .. tostring(text) .. ')' end\n"
	"function OnVariation1(self, v) log = log .. 'V(' .. tostring(v) .. ')' end\n"
	"function OnFear1(self, source, range, text) log = log .. 'F' end\n";

std::string getLog(Rig &r)
{
	lua_State *L = r.engine->logic()->state();
	lua_getglobal(L, "log");
	std::string s = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
	lua_settop(L, -2);
	return s;
}

struct World : Rig
{
	World()
	{
		load(kLua, kXml);
		host.add(1);
		host.add(2);
		host.add(3, false); // no AI: never registered
		attach(1);
		attach(2);
	}
	void attach(int id) { host.objects.at(id).info.luaEvents = engine->findEventList("Test"); }
	void registerAll()
	{
		for (auto &o : host.objects)
		{
			engine->registerObject(o.second.info);
		}
	}
	std::string eval(const std::string &code) { return run(*engine->logic(), code).values; }
	int stackTop() { return lua_gettop(engine->logic()->state()); }
};

class MemFiles : public LuaFileSource
{
public:
	std::map<std::string, std::string> files;
	std::vector<std::string> reads;
	bool exists(const std::string &p) override { return files.count(p) != 0; }
	bool read(const std::string &p, std::string *out) override
	{
		reads.push_back(p);
		auto it = files.find(p);
		if (it == files.end())
		{
			return false;
		}
		*out = it->second;
		return true;
	}
};

class RecCtx : public LuaDrawableContext
{
public:
	std::vector<std::string> calls;
	std::string prevState, prevAnim, transition;
	float fraction = 0.25f;
	bool allow = false;
	std::set<std::string> conditions, statuses, modules;
	int kindOf = 1;
	bool haveTarget = true;
	std::string previousAnimationState() override { return prevState; }
	std::string previousAnimation() override { return prevAnim; }
	float previousAnimFraction() override { return fraction; }
	void setTransitionAnimState(const std::string &n) override { transition = n; calls.push_back("transition(" + n + ")"); }
	void allowToContinue() override { allow = true; calls.push_back("allow"); }
	bool modelCondition(const std::string &n) override { return conditions.count(n) != 0; }
	bool objectStatus(const std::string &n) override { return statuses.count(n) != 0; }
	bool hasModule(const std::string &n) override { return modules.count(n) != 0; }
	bool showModule(const std::string &n, bool v, bool p) override
	{
		calls.push_back(std::string("module(") + n + "," + (v ? "1" : "0") + "," + (p ? "1" : "0") + ")");
		return modules.count(n) != 0;
	}
	void showSubObject(const std::string &n, bool v, bool p) override { calls.push_back(std::string("sub(") + n + "," + (v ? "1" : "0") + "," + (p ? "1" : "0") + ")"); }
	void playSound(const std::string &n) override { calls.push_back("sound(" + n + ")"); }
	bool targetDistance(double *o) override { if (!haveTarget) return false; *o = 12.5; return true; }
	bool targetHeight(double *o) override { if (!haveTarget) return false; *o = -3.0; return true; }
	bool targetBearing(double *o) override { if (!haveTarget) return false; *o = 90.0; return true; }
	int targetKindOf(const std::string &) override { return kindOf; }
	float clientRandomReal(float lo, float hi) override { calls.push_back("random"); return (lo + hi) * 0.5f; }
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// the two states and their registrations
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("engine: the logic state has the 41 bindings, the drawable state the 21, _ALERT and GetFrame in both (spec 3.2, 3.3)")
{
	Rig r;
	r.load("", "<SageLuaScriptSection/>");
	REQUIRE(r.engine->logic());
	REQUIRE(r.engine->drawable());
	for (const char *n : kLuaLogicBindingNames)
	{
		INFO(n);
		CHECK(run(*r.engine->logic(), std::string("return type(") + n + ")").values == "string:function");
	}
	for (const char *n : kLuaDrawableBindingNames)
	{
		INFO(n);
		CHECK(run(*r.engine->drawable(), std::string("return type(") + n + ")").values == "string:function");
	}
	// the sets differ everywhere else: a logic binding is not a global of the drawable state and the other way round
	CHECK(run(*r.engine->logic(), "return type(CurDrawableHideSubObject)").values == "string:nil");
	CHECK(run(*r.engine->drawable(), "return type(ObjectTeamName)").values == "string:nil");
	CHECK(run(*r.engine->drawable(), "return type(GetClientRandomNumberReal)").values == "string:function");
	CHECK(run(*r.engine->logic(), "return type(GetClientRandomNumberReal)").values == "string:nil");
	// 41 + 21 registrations, 60 distinct names (spec 0.3 item 2)
	std::set<std::string> names(kLuaLogicBindingNames, kLuaLogicBindingNames + 41);
	names.insert(kLuaDrawableBindingNames, kLuaDrawableBindingNames + 21);
	CHECK(names.size() == 60);
	CHECK(std::string(kLuaLogicBindingNames[0]) == "_ALERT");
	CHECK(std::string(kLuaLogicBindingNames[40]) == "ObjectForbidPlayerCommands");
	CHECK(std::string(kLuaDrawableBindingNames[20]) == "GetClientRandomNumberReal");
}

TEST_CASE("engine: lifecycle: init creates the drawable state once, reset closes only the logic state, a game start reloads Scripts.lua and ScriptEvents.xml")
{
	Rig r;
	LuaRuntime *drawable = r.engine->drawable();
	REQUIRE(drawable);
	r.engine->init();
	CHECK(r.engine->drawable() == drawable); // runs once
	CHECK(r.engine->logic() == nullptr);
	MemFiles files;
	files.files["Data\\Scripts\\Scripts.lua"] = "x = 41 function F(self) end";
	files.files["Data\\Scripts\\ScriptEvents.xml"] = "<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/></EventList></SageLuaScriptSection>";
	CHECK(r.engine->startNewGame(files, ""));
	REQUIRE(r.engine->logic());
	CHECK(run(*r.engine->logic(), "return x").values == "number:41");
	CHECK(r.engine->events().eventListCount() == 1);
	// a drawable global survives reset (the drawable state is never closed by it)
	run(*r.engine->drawable(), "kept = 7");
	r.engine->reset();
	CHECK(r.engine->logic() == nullptr);
	CHECK(r.engine->events().eventListCount() == 0);
	CHECK(run(*r.engine->drawable(), "return kept").values == "number:7");
	// dispatch after reset returns immediately
	LuaObjectInfo o;
	o.id = 1;
	o.hasAI = true;
	r.engine->dispatchInternal(LUAEVENT_OnCreated, o);
	// a start without reset appends the lists again (spec G15): both copies exist
	CHECK(r.engine->startNewGame(files, ""));
	CHECK(r.engine->startNewGame(files, ""));
	CHECK(r.engine->events().eventListCount() == 2);
	CHECK(r.engine->events().notes().back().find("defined twice") != std::string::npos);
	CHECK(r.engine->reports().has("S-127", "defined more than once"));
}

TEST_CASE("engine: missing Scripts.lua / ScriptEvents.xml are an error that reaches the report (retail loads nothing, silently)")
{
	Rig r;
	MemFiles files;
	CHECK(!r.engine->startNewGame(files, ""));
	CHECK(r.engine->reports().count("S-127") == 2);
	CHECK(r.engine->reports().has("S-127", "Scripts.lua"));
	CHECK(r.engine->reports().has("S-127", "ScriptEvents.xml"));
}

TEST_CASE("engine: the map folder overlay: <folder>\\Scripts.lua runs in the same state, <folder>\\ScriptEvents.xml replaces lists of the same name")
{
	Rig r;
	MemFiles files;
	files.files["Data\\Scripts\\Scripts.lua"] = "function F(self) return 1 end function G(self) return 2 end";
	files.files["Data\\Scripts\\ScriptEvents.xml"] = "<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/></EventList></SageLuaScriptSection>";
	files.files["maps\\m\\Scripts.lua"] = "function F(self) return 99 end";
	files.files["maps\\m\\ScriptEvents.xml"] = "<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"G\"/></EventList></SageLuaScriptSection>";
	CHECK(r.engine->startNewGame(files, "maps\\m"));
	const LuaEventList *l = r.engine->findEventList("L");
	REQUIRE(l);
	CHECK(r.engine->events().eventListCount() == 1);
	REQUIRE(l->handlers.size() == 1);
	CHECK(r.keys.keyToName(l->handlers[0].key) == "OnDamaged");
	CHECK(run(*r.engine->logic(), "return F()").values == "number:99"); // the overlay's function replaced the base one
	CHECK(r.engine->reports().has("S-127", "map overlay"));
	// no overlay files: nothing happens
	Rig q;
	files.files.erase("maps\\m\\Scripts.lua");
	files.files.erase("maps\\m\\ScriptEvents.xml");
	CHECK(q.engine->startNewGame(files, "maps\\m"));
	CHECK(q.engine->events().eventListCount() == 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// registration of objects
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("engine: register: only objects with an AI or ForceLuaRegistration get the handle table \"ObjID#%08x\" whose payload is the id (RW 0x735941)")
{
	World w;
	w.registerAll();
	CHECK(w.eval("t = getglobal('ObjID#00000001') return type(t)") == "string:table");
	lua_State *L = w.engine->logic()->state();
	lua_getglobal(L, "ObjID#00000001");
	CHECK(lua_toobjid(L, -1) == 1);
	lua_getglobal(L, "ObjID#00000002");
	CHECK(lua_toobjid(L, -1) == 2);
	lua_getglobal(L, "ObjID#00000003");
	CHECK(lua_type(L, -1) == LUA_TNIL); // no AI, no ForceLuaRegistration
	lua_settop(L, 0);
	// ForceLuaRegistration registers an object without an AI (a projectile object: spec 1.6)
	LuaObjectInfo forced = w.host.objects.at(3).info;
	forced.forceLuaRegistration = true;
	w.engine->registerObject(forced);
	lua_getglobal(L, "ObjID#00000003");
	CHECK(lua_toobjid(L, -1) == 3);
	lua_settop(L, 0);
	CHECK(LuaScriptEngine::objectGlobalName(0x1a2b) == "ObjID#00001a2b");
	CHECK(LuaScriptEngine::objectGlobalName(0x12345678) == "ObjID#12345678");
	// registering again keeps the same table (identity)
	w.eval("old = getglobal('ObjID#00000001')");
	w.engine->registerObject(w.host.objects.at(1).info);
	CHECK(w.eval("return old == getglobal('ObjID#00000001')") == "boolean:true");
	// unregister sets the global to nil
	w.engine->unregisterObject(w.host.objects.at(1).info);
	CHECK(w.eval("return getglobal('ObjID#00000001')") == "nil");
	CHECK(w.stackTop() == 0);
}

TEST_CASE("engine: register reads ABSOLUTE stack index 1, not the top (quirk Q4): a value left on the logic stack makes it replace a good handle")
{
	World w;
	w.registerAll();
	w.eval("old = getglobal('ObjID#00000001')");
	lua_State *L = w.engine->logic()->state();
	lua_pushnumber(L, 5); // a stray value at index 1
	w.engine->registerObject(w.host.objects.at(1).info);
	lua_settop(L, 0);
	CHECK(w.eval("return old == getglobal('ObjID#00000001')") == "boolean:false"); // a new table with the same payload
	lua_getglobal(L, "ObjID#00000001");
	CHECK(lua_toobjid(L, -1) == 1);
	lua_settop(L, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("engine: dispatch is synchronous and calls the handler with the object's handle and the argument records (RW 0x735F53)")
{
	World w;
	w.registerAll();
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(getLog(w) == "C");
	w.engine->dispatchInternal(LUAEVENT_OnDestroyed, w.info(1));
	CHECK(getLog(w) == "CX");
	// a real, a string and an object argument
	LuaEventArgs a;
	a.arg[0] = LuaEventArg::makeObject(2);
	w.engine->dispatchInternal(LUAEVENT_OnDamaged, w.info(1), a);
	CHECK(getLog(w) == "CXD");
	LuaEventArgs g;
	g.arg[0] = LuaEventArg::makeString("hide_rock");
	w.engine->dispatchInternal(LUAEVENT_OnGenericEvent, w.info(1), g);
	CHECK(getLog(w) == "CXDG(hide_rock)");
	LuaEventArgs v;
	v.arg[0] = LuaEventArg::makeReal(0.5f);
	w.engine->dispatchInternal(LUAEVENT_OnBuildVariation, w.info(1), v);
	CHECK(getLog(w) == "CXDG(hide_rock)V(0.5)");
	CHECK(w.stackTop() == 0);
	// the handler sees the registered table: its id round trips through Lua equality
	w.eval("function OnCreated1(self) seen = self end");
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(w.eval("return seen == getglobal('ObjID#00000001')") == "boolean:true");
	CHECK(w.eval("return seen == getglobal('ObjID#00000002')") == "boolean:false");
}

TEST_CASE("engine: a boolean argument and kind NONE ends the list: nargs counts every pushed argument and stops at the first empty record")
{
	World w;
	w.registerAll();
	w.eval("function OnGeneric1(self, a, b, c) seen = tostring(a) .. '/' .. tostring(b) .. '/' .. tostring(c) end");
	LuaEventArgs a;
	a.arg[0] = LuaEventArg::makeBool(true);
	a.arg[1] = LuaEventArg::makeReal(2.0f);
	a.arg[2] = LuaEventArg::makeString("s");
	w.engine->dispatchInternal(LUAEVENT_OnGenericEvent, w.info(1), a);
	CHECK(w.eval("return seen") == "string:true/2/s");
	LuaEventArgs b;
	b.arg[0] = LuaEventArg::makeBool(false);
	b.arg[2] = LuaEventArg::makeString("never pushed"); // behind an empty record
	w.engine->dispatchInternal(LUAEVENT_OnGenericEvent, w.info(1), b);
	CHECK(w.eval("return seen") == "string:false/nil/nil");
}

TEST_CASE("engine: a dead object receives OnDestroyed only; an object without an AI or without an EventList receives nothing (RW 0x735F8F-0x735FC3)")
{
	World w;
	w.registerAll();
	w.host.objects.at(1).info.dead = true;
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	w.engine->dispatchInternal(LUAEVENT_OnDamaged, w.info(1));
	CHECK(getLog(w) == "");
	w.engine->dispatchInternal(LUAEVENT_OnDestroyed, w.info(1));
	CHECK(getLog(w) == "X");
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(3)); // no AI
	CHECK(getLog(w) == "X");
	LuaObjectInfo noList = w.info(2);
	noList.luaEvents = nullptr;
	w.engine->dispatchInternal(LUAEVENT_OnCreated, noList);
	CHECK(getLog(w) == "X");
}

TEST_CASE("engine: a handler naming a missing function, a name that is not a function, or an event with no handler is silent (Q1)")
{
	World w;
	w.registerAll();
	w.engine->dispatchInternal(LUAEVENT_OnArrived, w.info(1)); // handler NotDefined
	w.eval("NotDefined = 5");
	w.engine->dispatchInternal(LUAEVENT_OnArrived, w.info(1)); // a number
	w.engine->dispatchInternal(LUAEVENT_OnAflame, w.info(1));  // no handler
	CHECK(getLog(w) == "");
	CHECK(w.stackTop() == 0);
	CHECK(w.engine->logic()->alerts().empty());
}

TEST_CASE("engine: Lua errors in a handler are swallowed (alert kept as a diagnostic), the effects before the error stay, the stack is restored")
{
	World w;
	w.registerAll();
	w.eval("function OnCreated1(self) log = log .. 'a' error('boom') end");
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(getLog(w) == "a");
	CHECK(w.stackTop() == 0);
	REQUIRE(w.engine->logic()->alerts().size() == 1);
	CHECK(w.engine->logic()->alerts()[0].find("boom") != std::string::npos);
}

TEST_CASE("engine: dispatch nests to 11 handlers: at depth 10 a call is still allowed, at 11 it returns (RW 0x735F71 cmp 0xA; jg) (spec says 10)")
{
	World w;
	w.registerAll();
	w.eval("depth = 0 maxdepth = 0\n"
		"function OnCreated1(self) depth = depth + 1 if depth > maxdepth then maxdepth = depth end ObjectDispatchEvent(self, self, 'OnCreated', 'x') depth = depth - 1 end");
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(w.eval("return maxdepth") == "number:11");
	CHECK(w.engine->dispatchDepth() == 0);
	CHECK(w.stackTop() == 0);
}

TEST_CASE("engine: an unregistered `other` makes the whole call fail: nargs counts one argument but pushObject left two values, so lua_call takes the wrong function slot (Q5)")
{
	World w;
	w.registerAll();
	// object 3 has no AI and is not registered: as the `other` of OnDamaged
	LuaEventArgs a;
	a.arg[0] = LuaEventArg::makeObject(3);
	w.engine->dispatchInternal(LUAEVENT_OnDamaged, w.info(1), a);
	CHECK(getLog(w) == ""); // the handler never ran
	// the failed call leaves the handler function on the logic stack (lua_call resets top to the wrong "function" slot)
	CHECK(w.stackTop() == 1);
	// a non existent object id behaves the same (pushObject(null) pushes ONE nil, which is fine): the handler runs with other == nil
	LuaEventArgs b;
	b.arg[0] = LuaEventArg::makeObject(99);
	w.engine->dispatchInternal(LUAEVENT_OnDamaged, w.info(1), b);
	CHECK(getLog(w) == "D-nil");
}

TEST_CASE("engine: an unregistered `self` cannot be dispatched to: the call fails and is swallowed (spec 4.5); each failure leaks one stack slot")
{
	World w;
	// nothing registered
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(getLog(w) == "");
	CHECK(w.stackTop() == 1);
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(w.stackTop() == 2);
	// and the leaked slot at absolute index 1 makes register (Q4) replace a good handle every time
	w.engine->registerObject(w.host.objects.at(1).info);
	w.engine->registerObject(w.host.objects.at(1).info);
	lua_State *L = w.engine->logic()->state();
	lua_settop(L, 0);
	lua_getglobal(L, "ObjID#00000001");
	CHECK(lua_toobjid(L, -1) == 1);
	lua_settop(L, 0);
}

TEST_CASE("engine: ObjectDispatchEvent and the scripted events: FindEvent finds declared scripted events, the argument records are {object id, text} (RW 0x737269)")
{
	World w;
	w.registerAll();
	w.eval("function OnFear1(self, source, range, text) log = log .. 'F(' .. tostring(source == getglobal('ObjID#00000002')) .. ',' .. tostring(range) .. ',' .. tostring(text) .. ')' end");
	// ObjectDispatchEvent(self, source, eventName, data): the handler gets (self, source, data) in its first three argument slots
	w.eval("ObjectDispatchEvent(getglobal('ObjID#00000001'), getglobal('ObjID#00000002'), 'Fear', 'hello')");
	CHECK(getLog(w) == "F(true,hello,nil)");
	// an undeclared event name dispatches nothing
	w.eval("ObjectDispatchEvent(getglobal('ObjID#00000001'), getglobal('ObjID#00000002'), 'NotDeclared', 'x')");
	CHECK(getLog(w) == "F(true,hello,nil)");
	// quirk Q3: the second object argument is validated against lua_type(L, 1): a nil second argument passes when the FIRST is nil
	w.eval("log = ''");
	w.eval("ObjectDispatchEvent(nil, nil, 'Fear', 'x')"); // self nil: findObjectByID(0) is null: nothing
	CHECK(getLog(w) == "");
	w.eval("ObjectDispatchEvent(getglobal('ObjID#00000001'), nil, 'Fear', 'x')"); // nil second argument with a non-nil first: refused
	CHECK(getLog(w) == "");
}

TEST_CASE("engine: ModelCondition events are edge triggered against the snapshot, and with frame < 2 only the snapshot is taken (RW 0x663E32 / 0x7378DA)")
{
	Rig r;
	r.load("log = '' function OnMove(self) log = log .. 'M' end function OnBoth(self) log = log .. 'B' end",
		"<SageLuaScriptSection><Events>"
		"<ModelConditionEvent Name=\"Moving\"><Conditions>+MOVING</Conditions></ModelConditionEvent>"
		"<ModelConditionEvent Name=\"MovingDamaged\"><Conditions>+MOVING +DAMAGED</Conditions></ModelConditionEvent>"
		"</Events><EventList Name=\"L\"><EventHandler EventName=\"Moving\" ScriptFunctionName=\"OnMove\"/>"
		"<EventHandler EventName=\"MovingDamaged\" ScriptFunctionName=\"OnBoth\"/></EventList></SageLuaScriptSection>");
	r.host.add(1);
	r.host.objects.at(1).info.luaEvents = r.engine->findEventList("L");
	r.engine->registerObject(r.host.objects.at(1).info);
	ModelConditionFlags snap, now;
	now.set(ModelCondition::indexOf("MOVING"));
	// frame 0 and 1: only the snapshot is taken
	r.engine->updateModelConditionEvents(r.info(1), now, &snap, 1);
	CHECK(snap == now);
	lua_State *L = r.engine->logic()->state();
	auto log = [&] { lua_getglobal(L, "log"); std::string s = lua_tostring(L, -1); lua_settop(L, 0); return s; };
	CHECK(log() == "");
	// still MOVING, snapshot too: no new edge
	r.engine->updateModelConditionEvents(r.info(1), now, &snap, 2);
	CHECK(log() == "");
	// a new flag: MovingDamaged matches now and did not before: fires once; Moving did match before: silent
	now.set(ModelCondition::indexOf("DAMAGED"));
	r.engine->updateModelConditionEvents(r.info(1), now, &snap, 3);
	CHECK(log() == "B");
	r.engine->updateModelConditionEvents(r.info(1), now, &snap, 4);
	CHECK(log() == "B");
	// leave and re-enter MOVING: the edge fires again
	ModelConditionFlags none;
	r.engine->updateModelConditionEvents(r.info(1), none, &snap, 5);
	ModelConditionFlags moving;
	moving.set(ModelCondition::indexOf("MOVING"));
	r.engine->updateModelConditionEvents(r.info(1), moving, &snap, 6);
	CHECK(log() == "BM");
}

// ---------------------------------------------------------------------------------------------------------------------
// the bindings
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("bindings: GetFrame is the logic frame as an unsigned number (RW 0x73461B), in both states")
{
	World w;
	w.host.frame = 0xFFFFFFFFu;
	CHECK(w.eval("return GetFrame()") == "number:4294967295");
	CHECK(run(*w.engine->drawable(), "return GetFrame()").values == "number:4294967295");
	w.host.frame = 12;
	CHECK(w.eval("return GetFrame()") == "number:12");
}

TEST_CASE("bindings: ObjectDescription: the three texts, nil and non objects (RW 0x7366FE / 0x69005F, strings 0xC11F94 / 0xC11FC0 / 0xC12004)")
{
	World w;
	w.registerAll();
	w.host.objects.at(1).templateName = "GondorFighter";
	w.host.objects.at(1).playerIndex = 3;
	w.host.objects.at(1).playerName = L"Rohan";
	CHECK(w.eval("return ObjectDescription(getglobal('ObjID#00000001'))") == "string:Object 1 [GondorFighter, owned by player 3 (Rohan)]");
	w.host.objects.at(1).objectName = "Hero";
	CHECK(w.eval("return ObjectDescription(getglobal('ObjID#00000001'))") == "string:Object 1 (Hero) [GondorFighter, owned by player 3 (Rohan)]");
	CHECK(w.eval("return ObjectDescription(nil)") == "string:<No Object>");
	CHECK(w.eval("return ObjectDescription(5)") == "nil");     // not an object handle and not nil: pushes nil
	CHECK(w.eval("return ObjectDescription({})") == "nil");
	w.host.objects.at(1).playerName = std::wstring(L"Café");
	CHECK(w.eval("return ObjectDescription(getglobal('ObjID#00000001'))").find("(Caf?)") != std::string::npos); // %ls in the C locale
}

TEST_CASE("bindings: the read-only queries: team name, side, capturing side, template name, model condition (RW 0x736770-0x73692C)")
{
	World w;
	w.registerAll();
	w.host.objects.at(1).teamName = "TheTeam";
	w.host.objects.at(1).side = "Isengard";
	w.host.objects.at(1).hasCapturer = true;
	w.host.objects.at(1).capturingSide = "Mordor";
	w.host.objects.at(1).templateName = "Banner";
	w.host.objects.at(1).modelBits.insert(ModelCondition::indexOf("AFLAME"));
	const std::string o1 = "getglobal('ObjID#00000001')";
	CHECK(w.eval("return ObjectTeamName(" + o1 + ")") == "string:TheTeam");
	CHECK(w.eval("return ObjectPlayerSide(" + o1 + ")") == "string:Isengard");
	CHECK(w.eval("return ObjectCapturingObjectPlayerSide(" + o1 + ")") == "string:Mordor");
	CHECK(w.eval("return ObjectCapturingObjectPlayerSide(getglobal('ObjID#00000002'))") == "nil");
	CHECK(w.eval("return ObjectTemplateName(" + o1 + ")") == "string:Banner");
	CHECK(w.eval("return ObjectTeamName(nil)") == "nil");
	CHECK(w.eval("return ObjectTeamName(7)") == "nil");
	CHECK(w.eval("return ObjectTeamName(getglobal('ObjID#00000099'))") == "nil");
	CHECK(w.eval("return ObjectTestModelCondition(" + o1 + ", 'AFLAME')") == "boolean:true");
	CHECK(w.eval("return ObjectTestModelCondition(" + o1 + ", 'aflame')") == "boolean:true"); // case insensitive lookup
	CHECK(w.eval("return ObjectTestModelCondition(" + o1 + ", 'MOVING')") == "boolean:false");
	CHECK(w.eval("return ObjectTestModelCondition(" + o1 + ", 'NOT_A_CONDITION')") == "nil");
	CHECK(w.eval("return ObjectTestModelCondition(" + o1 + ")") == "nil");
	CHECK(w.engine->reports().has("S-121", "ObjectPlayerSide"));
}

TEST_CASE("bindings: GetRandomNumber draws the LOGIC stream (one draw, real 0..1); ObjectTestCanSufferFear draws only when the object has a fear modifier (RW 0x73474F / 0x7369AE)")
{
	World w;
	w.registerAll();
	GameLogicRandom shadow(RandomAlgorithm::ZH_CarryChain); // the same initial seed array as w.random: a second stream to predict the draws
	const float r0 = shadow.getValueReal(0.0f, 1.0f, "t", 0);
	CHECK(w.eval("return GetRandomNumber()") == "number:" + [&] { char b[64]; std::snprintf(b, sizeof b, "%.17g", (double)r0); return std::string(b); }());
	// no modifier: no draw
	w.host.objects.at(1).hasModifier = false;
	CHECK(w.eval("return ObjectTestCanSufferFear(getglobal('ObjID#00000001'))") == "boolean:true");
	const float r1 = shadow.getValueReal(0.0f, 1.0f, "t", 0);
	CHECK(w.eval("return GetRandomNumber()") == "number:" + [&] { char b[64]; std::snprintf(b, sizeof b, "%.17g", (double)r1); return std::string(b); }());
	// with a modifier: one draw GetGameLogicRandomValue(0,1); (float)r <= bonus means no fear
	w.host.objects.at(2).hasModifier = true;
	w.host.objects.at(2).modifierBonus = 0.5f;
	const int d = shadow.getValue(0, 1, "t", 0);
	const std::string expect = (float)d <= 0.5f ? "boolean:false" : "boolean:true";
	CHECK(w.eval("return ObjectTestCanSufferFear(getglobal('ObjID#00000002'))") == expect);
	CHECK(w.random.seedArray() == shadow.seedArray()); // exactly the draws predicted: no other consumption
	// bonus 1: always "cannot suffer fear" (r is 0 or 1, both <= 1)
	w.host.objects.at(2).modifierBonus = 1.0f;
	CHECK(w.eval("return ObjectTestCanSufferFear(getglobal('ObjID#00000002'))") == "boolean:false");
	// the AI kind 0x2D never suffers fear
	w.host.objects.at(1).aiKind = 0x2D;
	CHECK(w.eval("return ObjectTestCanSufferFear(getglobal('ObjID#00000001'))") == "boolean:false");
	// a bad argument pushes nil and returns NO result (xor eax, eax at 0x7369DC); a missing object returns the nil (one result)
	CHECK(w.eval("return ObjectTestCanSufferFear(5)") == "");
	CHECK(w.eval("return ObjectTestCanSufferFear(nil)") == "nil");
}

TEST_CASE("bindings: ObjectCountNearbyEnemies: the radius is truncated by _ftol, an invalid argument pushes nil and returns 0 results (Q9), a missing object counts 0 (RW 0x737BF7)")
{
	World w;
	w.registerAll();
	w.host.inRange = { 5, 6, 7 };
	CHECK(w.eval("return ObjectCountNearbyEnemies(getglobal('ObjID#00000001'), 12.9)") == "number:3");
	CHECK(w.host.calls.back() == "objectsInRange(1, 12, order 0, rel 1, notOfPlayer 0)");
	CHECK(w.eval("return ObjectCountNearbyEnemies(5, 12)") == ""); // Q9
	CHECK(w.eval("return ObjectCountNearbyEnemies(nil, 12)") == "number:0");
	CHECK(w.eval("return ObjectCountNearbyEnemies(getglobal('ObjID#00000099'), 12)") == "number:0");
}

TEST_CASE("bindings: the broadcasts query the partition near to far with the relationship of the binding and dispatch to each result (RW 0x737D2D-0x738246)")
{
	World w;
	w.registerAll();
	w.host.add(4);
	w.host.add(5);
	w.attach(4);
	w.attach(5);
	w.registerAll();
	w.eval("function OnFear1(self, source, range, text) log = log .. 'F' .. tostring(range) end");
	w.host.inRange = { 4, 5, 99 }; // 99 does not exist any more: skipped
	w.eval("ObjectBroadcastEventToEnemies(getglobal('ObjID#00000001'), 'Fear', 75, 'txt')");
	CHECK(w.host.calls.back() == "objectsInRange(1, 75, order 1, rel 1, notOfPlayer 0)");
	CHECK(getLog(w) == "FtxtFtxt"); // objects 4 and 5; handler args: (self = the target, source = object 1 (kind 3), text = record 1)
	w.eval("log = ''");
	w.eval("ObjectBroadcastEventToAllies(getglobal('ObjID#00000001'), 'Fear', 200)");
	CHECK(w.host.calls.back() == "objectsInRange(1, 200, order 1, rel 4, notOfPlayer 0)");
	w.eval("ObjectBroadcastEventToCivilians(getglobal('ObjID#00000001'), 'Fear', 1)");
	CHECK(w.host.calls.back() == "objectsInRange(1, 1, order 1, rel 2, notOfPlayer 0)");
	w.eval("ObjectBroadcastEventToUnits(getglobal('ObjID#00000001'), 'Fear', 1)");
	CHECK(w.host.calls.back() == "objectsInRange(1, 1, order 1, rel 0, notOfPlayer 1)");
	CHECK(w.engine->reports().has("S-121", "ObjectBroadcastEventToUnits"));
	// an unknown event name, a nil self and a bad argument do nothing, and a nil text leaves the second record empty
	w.host.calls.clear();
	w.eval("ObjectBroadcastEventToEnemies(getglobal('ObjID#00000001'), 'NoSuchEvent', 75)");
	w.eval("ObjectBroadcastEventToEnemies(7, 'Fear', 75)");
	w.eval("ObjectBroadcastEventToEnemies(nil, 'Fear', 75)");
	CHECK(w.host.calls.empty());
}

TEST_CASE("bindings: HordeBroadcastEventToMembers dispatches to the members in list order with ONE argument record from Lua index 3 (RW 0x737380); no nil tolerance")
{
	World w;
	w.host.add(10);
	w.host.add(11);
	w.attach(10);
	w.attach(11);
	w.registerAll();
	w.host.objects.at(1).isHorde = true;
	w.host.objects.at(1).hordeMembers = { 11, 10 };
	w.eval("function OnFear1(self, a, b, c) log = log .. tostring(a) .. ':' .. tostring(b) .. ';' end");
	w.eval("HordeBroadcastEventToMembers(getglobal('ObjID#00000001'), 'Fear', 'payload')");
	CHECK(getLog(w) == "payload:nil;payload:nil;");
	w.eval("log = ''");
	w.eval("HordeBroadcastEventToMembers(getglobal('ObjID#00000001'), 'Fear', 7)");
	CHECK(getLog(w) == "7:nil;7:nil;");
	w.eval("log = ''");
	w.eval("HordeBroadcastEventToMembers(nil, 'Fear', 7)"); // id 0: return
	w.eval("HordeBroadcastEventToMembers(getglobal('ObjID#00000002'), 'Fear', 7)"); // not a horde
	CHECK(getLog(w) == "");
}

TEST_CASE("bindings: the emotion states: types 6, 4, 5, 9; the second argument is validated against index 1 (Q3) and must exist (RW 0x736A69-0x736BA0)")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')", o2 = "getglobal('ObjID#00000002')";
	w.eval("ObjectEnterRunAwayPanicState(" + o1 + ", " + o2 + ")");
	w.eval("ObjectEnterCowerState(" + o1 + ", " + o2 + ")");
	w.eval("ObjectEnterUncontrollableCowerState(" + o1 + ", " + o2 + ")");
	w.eval("ObjectEnterAlertState(" + o1 + ")");
	REQUIRE(w.host.calls.size() == 4);
	CHECK(w.host.calls[0] == "enterEmotion(1, 6, 2)");
	CHECK(w.host.calls[1] == "enterEmotion(1, 4, 2)");
	CHECK(w.host.calls[2] == "enterEmotion(1, 5, 2)");
	CHECK(w.host.calls[3] == "enterEmotion(1, 9, 0)");
	w.host.calls.clear();
	// Q3: a nil second argument is checked as lua_type(L, 1): with a non-nil first it fails the guard, and with a nil first the object is
	// missing anyway; so a nil `other` never reaches the callee, while an `other` that is not an object (a number) does not either
	w.eval("ObjectEnterCowerState(" + o1 + ", nil)");
	w.eval("ObjectEnterCowerState(" + o1 + ", 5)");
	w.eval("ObjectEnterCowerState(nil, " + o2 + ")");
	w.eval("ObjectEnterCowerState(getglobal('ObjID#00000099'), " + o2 + ")");
	CHECK(w.host.calls.empty());
	CHECK(w.engine->reports().has("S-121", "emotion type numbers"));
}

TEST_CASE("bindings: ObjectEnterFearState sets the AI command and flag; ObjectEnterRampageState; the setters return 1 without pushing (Q2) and nothing for a bad argument")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')", o2 = "getglobal('ObjID#00000002')";
	w.eval("ObjectEnterFearState(" + o1 + ", " + o2 + ", true)");
	w.eval("ObjectEnterFearState(" + o1 + ", " + o2 + ")"); // flag nil: false
	w.eval("ObjectEnterRampageState(" + o1 + ")");
	REQUIRE(w.host.calls.size() == 3);
	CHECK(w.host.calls[0] == "enterFearState(1, 2, true)");
	CHECK(w.host.calls[1] == "enterFearState(1, 2, false)");
	CHECK(w.host.calls[2] == "enterRampageState(1)");
	// Q2: `return 1` with nothing pushed: the visible result is the top stack value, the last argument
	w.host.calls.clear();
	CHECK(w.eval("return ObjectSetChanting(" + o1 + ", true)") == "boolean:true");
	CHECK(w.eval("return ObjectSetEnragedState(" + o1 + ", false)") == "boolean:false");
	CHECK(w.eval("return ObjectSetFearFactor(" + o1 + ", 12.7)") == "number:12.699999999999999"); // the last argument, a double
	CHECK(w.eval("return ObjectSetChanting(5, true)") == ""); // bad argument: 0 results
	CHECK(w.eval("return ObjectSetChanting(" + o1 + ")") == ""); // argc < 2
	CHECK(w.eval("return ObjectSetChanting(nil, true)") == ""); // nil first argument passes the guard, the object is missing
	// the fear factor keeps the larger of the old and the truncated new value (RW 0x7371C9)
	w.host.objects.at(1).fearFactor = 20.0f;
	w.eval("ObjectSetFearFactor(" + o1 + ", 5.9)");
	CHECK(w.host.objects.at(1).fearFactor == 20.0f);
	w.eval("ObjectSetFearFactor(" + o1 + ", 30.9)");
	CHECK(w.host.objects.at(1).fearFactor == 30.0f);
}

TEST_CASE("bindings: special powers, temp weapons and sounds need argc >= 2 and TheAudio; 1 (Q2) only when the power / weapon exists (RW 0x736C16, 0x736D3B, 0x736FCD)")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')";
	w.host.knownPowers.insert("PowerA");
	w.host.knownWeapons.insert("WeaponA");
	CHECK(w.eval("return ObjectDoSpecialPower(" + o1 + ", 'PowerA')") == "string:PowerA"); // Q2: the last argument
	CHECK(w.eval("return ObjectDoSpecialPower(" + o1 + ", 'Nope')") == "");
	CHECK(w.eval("return ObjectCreateAndFireTempWeapon(" + o1 + ", 'WeaponA')") == "string:WeaponA");
	CHECK(w.eval("return ObjectCreateAndFireTempWeapon(" + o1 + ", 'Nope')") == "");
	CHECK(w.eval("return ObjectDoSpecialPower(" + o1 + ")") == "");
	w.host.audio = false;
	CHECK(w.eval("return ObjectDoSpecialPower(" + o1 + ", 'PowerA')") == "");
	CHECK(w.eval("return ObjectCreateAndFireTempWeapon(" + o1 + ", 'WeaponA')") == "");
	CHECK(w.eval("return ObjectPlaySound(" + o1 + ", 'Snd')") == "");
	w.host.audio = true;
	// ObjectPlaySound: the audio handle as a number when >= 5, else nil; an unknown event pushes nothing (still returns 1: the last argument)
	CHECK(w.eval("return ObjectPlaySound(" + o1 + ", 'Snd')") == "number:9");
	w.host.soundHandle = 3;
	CHECK(w.eval("return ObjectPlaySound(" + o1 + ", 'Snd')") == "nil");
	w.host.soundValid = false;
	CHECK(w.eval("return ObjectPlaySound(" + o1 + ", 'Snd')") == "string:Snd");
	// the same three accept a nil first argument (findObjectByID(0) is null: nothing happens)
	CHECK(w.eval("return ObjectDoSpecialPower(nil, 'PowerA')") == "");
}

TEST_CASE("bindings: upgrades: HasUpgrade is 1.0 / 0.0 only for player upgrades and nothing for unknown ones (Q6); Grant / Remove return 1 without pushing (Q2) (RW 0x736DF5, 0x736ED1)")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')";
	w.host.upgradesPlayerType = { "Upgrade_Player" };
	w.host.upgradesObjectType = { "Upgrade_Object" };
	w.host.playerHas = { "Upgrade_Player" };
	CHECK(w.eval("return ObjectHasUpgrade(" + o1 + ", 'Upgrade_Player')") == "number:1");
	CHECK(w.eval("return ObjectHasUpgrade(" + o1 + ", 'Upgrade_Object')") == "number:0"); // object upgrades: always 0.0
	w.host.playerHas.clear();
	CHECK(w.eval("return ObjectHasUpgrade(" + o1 + ", 'Upgrade_Player')") == "number:0");
	CHECK(w.eval("return ObjectHasUpgrade(" + o1 + ", 'Unknown')") == "");   // no value at all
	CHECK(w.eval("return ObjectHasUpgrade(" + o1 + ")") == "");
	CHECK(w.eval("return ObjectHasUpgrade(5, 'Upgrade_Player')") == "");
	CHECK(w.eval("return ObjectHasUpgrade(nil, 'Upgrade_Player')") == "");
	// grant / remove: player type -> Player::addUpgrade / removeUpgrade, object type -> Object::giveUpgrade / removeUpgrade
	CHECK(w.eval("return ObjectGrantUpgrade(" + o1 + ", 'Upgrade_Object')") == "string:Upgrade_Object");
	CHECK(w.eval("return ObjectGrantUpgrade(" + o1 + ", 'Upgrade_Player')") == "string:Upgrade_Player");
	CHECK(w.eval("return ObjectRemoveUpgrade(" + o1 + ", 'Upgrade_Object')") == "string:Upgrade_Object");
	CHECK(w.eval("return ObjectRemoveUpgrade(" + o1 + ", 'Upgrade_Player')") == "string:Upgrade_Player");
	CHECK(w.eval("return ObjectGrantUpgrade(" + o1 + ", 'Unknown')") == "");
	REQUIRE(w.host.calls.size() == 4);
	CHECK(w.host.calls[0] == "Object::giveUpgrade(1, Upgrade_Object)");
	CHECK(w.host.calls[1] == "Player::addUpgrade(1, Upgrade_Player)");
	CHECK(w.host.calls[2] == "Object::removeUpgrade(1, Upgrade_Object)");
	CHECK(w.host.calls[3] == "Player::removeUpgrade(1, Upgrade_Player)");
}

TEST_CASE("bindings: hide / show sub objects: argc must be exactly 3; the permanent one tries a draw module first with visible 0 (RW 0x73635C, 0x736412)")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')";
	w.eval("ObjectHideSubObject(" + o1 + ", 'Part', true)");
	w.eval("ObjectHideSubObject(" + o1 + ", 'Part', false)");
	w.eval("ObjectHideSubObjectPermanently(" + o1 + ", 'Trunk01', true)");
	w.host.knownDrawModules.insert("Mod");
	w.eval("ObjectHideSubObjectPermanently(" + o1 + ", 'Mod', false)"); // a module with that tag: stop after it
	REQUIRE(w.host.calls.size() == 5);
	CHECK(w.host.calls[0] == "showSubObject(1, Part, 0, 0)");  // hide: visible = !hide
	CHECK(w.host.calls[1] == "showSubObject(1, Part, 1, 0)");
	CHECK(w.host.calls[2] == "showModule(1, Trunk01, 0, 1)");
	CHECK(w.host.calls[3] == "showSubObject(1, Trunk01, 0, 1)");
	CHECK(w.host.calls[4] == "showModule(1, Mod, 0, 1)");
	w.host.calls.clear();
	w.eval("ObjectHideSubObject(" + o1 + ", 'Part')");  // argc 2
	w.eval("ObjectHideSubObject(" + o1 + ", 'Part', true, 1)"); // argc 4
	w.eval("ObjectHideSubObject(5, 'Part', true)");
	CHECK(w.host.calls.empty());
	w.eval("ObjectHideSubObject(nil, 'Part', true)"); // nil first argument passes, the object is missing
	CHECK(w.host.calls.empty());
	CHECK(w.eval("return ObjectHideSubObject(" + o1 + ", 'Part', true)") == ""); // 0 results
}

TEST_CASE("bindings: the remaining object bindings: geometry, allegiance, forbid commands, delayed death (RW 0x736519, 0x7365BE, 0x736635, 0x736CC8)")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')", o2 = "getglobal('ObjID#00000002')";
	w.eval("ObjectSetGeometryActive(" + o1 + ", 'Shape', true)");
	w.eval("ObjectSetGeometryActive(" + o1 + ", 'Shape')"); // argc 2: refused
	w.eval("ObjectChangeAllegianceFromNonPlayablePlayer(" + o1 + ", " + o2 + ")");
	w.eval("ObjectChangeAllegianceFromNonPlayablePlayer(nil, " + o2 + ")"); // no nil tolerance
	w.eval("ObjectForbidPlayerCommands(" + o1 + ", true)");
	w.eval("ObjectForbidPlayerCommands(nil, true)"); // no nil tolerance
	w.eval("ObjectForbidPlayerCommands(getglobal('ObjID#00000003'), true)"); // object 3 has no AI: nothing to set
	w.host.objects.at(3).info.forceLuaRegistration = true;
	w.engine->registerObject(w.host.objects.at(3).info);
	w.eval("ObjectForbidPlayerCommands(getglobal('ObjID#00000003'), true)");
	w.eval("ObjectSetDelayedDeath(" + o1 + ", true)");
	w.eval("ObjectSetDelayedDeath(" + o1 + ")"); // argc 1
	REQUIRE(w.host.calls.size() == 4);
	CHECK(w.host.calls[0] == "setGeometryActive(1, Shape, true)");
	CHECK(w.host.calls[1] == "changeAllegiance(1, 2)");
	CHECK(w.host.calls[2] == "forbidPlayerCommands(1, true)");
	CHECK(w.host.calls[3] == "setDelayedDeath(1, true)");
}

TEST_CASE("bindings: EvaluateCondition / ExecuteAction: the template's parameter count must match, numbers (also numeric strings) become real+int, nil only in conditions, booleans and handles only in actions (RW 0x735BA7 / 0x735D04)")
{
	World w;
	w.registerAll();
	w.host.conditionParams["COND"] = 3;
	w.host.actionParams["ACT"] = 4;
	w.host.conditionResult = true;
	CHECK(w.eval("return EvaluateCondition('COND', 3.7, 'text', nil)") == "boolean:true");
	CHECK(w.eval("return EvaluateCondition('COND', '12', 'x', 'y')") == "boolean:true"); // a numeric string is a number
	w.host.conditionResult = false;
	CHECK(w.eval("return EvaluateCondition('COND', 1, 2, 3)") == "boolean:false");
	CHECK(w.eval("return EvaluateCondition('COND', 1, 2)") == "");        // wrong count
	CHECK(w.eval("return EvaluateCondition('NOPE', 1)") == "");           // no template
	CHECK(w.eval("return EvaluateCondition('COND', 1, 2, true)") == "");  // a boolean: conditions refuse it
	CHECK(w.eval("return ExecuteAction('ACT', 'a', true, getglobal('ObjID#00000002'), 5)") == "boolean:true");
	CHECK(w.eval("return ExecuteAction('ACT', 'a', nil, 1, 2)") == "");   // a nil: actions refuse it
	CHECK(w.eval("return ExecuteAction('ACT', 'a', 7, 1, 2)") == "boolean:true");
	REQUIRE(w.host.calls.size() >= 3);
	CHECK(w.host.calls[0] == "evaluateCondition(COND, num(3.700000,3), str(text), nil)");
	CHECK(w.host.calls[1] == "evaluateCondition(COND, num(12.000000,12), str(x), str(y))");
	CHECK(w.host.calls[3] == "executeAction(ACT, str(a), bool(1), obj(2), num(5.000000,5))");
}

TEST_CASE("bindings: ObjectSpy records the pair for objects with an AI and fails into the (inert) console otherwise; a matching dispatch reports the unported re-dispatch (S-128)")
{
	World w;
	w.registerAll();
	const std::string o1 = "getglobal('ObjID#00000001')", o2 = "getglobal('ObjID#00000002')";
	w.eval("ObjectSpy(" + o1 + ", " + o2 + ", 'OnCreated', 'Fear')");
	REQUIRE(w.engine->spyRecords().size() == 1);
	CHECK(w.engine->spyRecords()[0].first == 1);
	w.eval("ObjectSpy(" + o1 + ", " + o2 + ", 'OnCreated', 'NotDeclared')");
	w.eval("ObjectSpy(" + o1 + ", " + o2 + ", 'NoEvent', 'Fear')");
	w.eval("ObjectSpy(nil, " + o2 + ", 'OnCreated', 'Fear')");
	CHECK(w.engine->spyRecords().size() == 1);
	CHECK(w.engine->reports().has("S-128", "debug console"));
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(w.engine->reports().has("S-128", "re-dispatch"));
}

TEST_CASE("bindings: every callee that is not implemented is a reported stop S-124 with its retail name and address, never silent")
{
	// a host that implements only the object table: the bindings that reach the engine report their callee
	struct Bare : LuaGameHost
	{
		bool findObject(int id, LuaObjectInfo *o) override { o->id = id; o->hasAI = true; return id == 1 || id == 2; }
	} bare;
	NameKeyGenerator keys;
	keys.init();
	GameLogicRandom random(RandomAlgorithm::ZH_CarryChain);
	LuaScriptEngine::Config c;
	c.keys = &keys;
	c.logicRandom = &random;
	c.host = &bare;
	LuaScriptEngine e(c);
	e.loadLogicScripts("", "<SageLuaScriptSection/>");
	LuaObjectInfo o1;
	o1.id = 1;
	o1.hasAI = true;
	e.registerObject(o1);
	LuaObjectInfo o2 = o1;
	o2.id = 2;
	e.registerObject(o2);
	const char *calls[] = {
		"GetFrame()", "ObjectDescription(getglobal('ObjID#00000001'))", "ObjectTeamName(getglobal('ObjID#00000001'))",
		"ObjectPlayerSide(getglobal('ObjID#00000001'))", "ObjectCapturingObjectPlayerSide(getglobal('ObjID#00000001'))",
		"ObjectTemplateName(getglobal('ObjID#00000001'))", "ObjectTestModelCondition(getglobal('ObjID#00000001'), 'AFLAME')",
		"ObjectTestCanSufferFear(getglobal('ObjID#00000001'))", "ObjectCountNearbyEnemies(getglobal('ObjID#00000001'), 5)",
		"ObjectEnterCowerState(getglobal('ObjID#00000001'), getglobal('ObjID#00000002'))",
		"ObjectEnterFearState(getglobal('ObjID#00000001'), getglobal('ObjID#00000002'), true)", "ObjectEnterRampageState(getglobal('ObjID#00000001'))",
		"ObjectPlaySound(getglobal('ObjID#00000001'), 'x')", "ObjectSetChanting(getglobal('ObjID#00000001'), true)",
		"ObjectSetFearFactor(getglobal('ObjID#00000001'), 1)", "ObjectSetEnragedState(getglobal('ObjID#00000001'), true)",
		"ObjectDoSpecialPower(getglobal('ObjID#00000001'), 'x')", "ObjectCreateAndFireTempWeapon(getglobal('ObjID#00000001'), 'x')",
		"ObjectHasUpgrade(getglobal('ObjID#00000001'), 'x')", "ObjectSetDelayedDeath(getglobal('ObjID#00000001'), true)",
		"ObjectHideSubObject(getglobal('ObjID#00000001'), 'x', true)", "ObjectHideSubObjectPermanently(getglobal('ObjID#00000001'), 'x', true)",
		"ObjectSetGeometryActive(getglobal('ObjID#00000001'), 'x', true)", "ObjectChangeAllegianceFromNonPlayablePlayer(getglobal('ObjID#00000001'), getglobal('ObjID#00000002'))",
		"ObjectForbidPlayerCommands(getglobal('ObjID#00000001'), true)", "EvaluateCondition('X')", "ExecuteAction('X')",
		"ObjectBroadcastEventToEnemies(getglobal('ObjID#00000001'), 'OnCreated', 5)",
	};
	size_t total = 0;
	for (const char *call : calls)
	{
		const size_t before = e.reports().count("S-124");
		run(*e.logic(), call);
		INFO(std::string(call));
		CHECK(e.reports().count("S-124") > before); // a repeated callee only counts up
		total += e.reports().count("S-124") - before;
	}
	CHECK(e.reports().has("S-124", "RW 0x6AEE22") == false); // grant/remove are not reached without a known upgrade
	CHECK(e.reports().has("S-124", "TheUpgradeCenter->findUpgrade (RW 0x66F5E5)"));
	CHECK(e.reports().has("S-124", "PartitionManager object query (RW 0xA39340)"));
	CHECK(e.reports().has("S-124", "ScriptEngine condition / action templates (RW 0x60328B, scripts.ini)"));
	CHECK(e.reports().has("S-124", "TheGameLogic frame counter (RW 0xDE412C+0x40, GetFrame RW 0x73461B)"));
	CHECK(total >= 28);
	CHECK(e.reports().entries().size() > 20); // many different callees
}

TEST_CASE("engine: the world entry points register and unregister; a change of owner re-registers (a new handle table, RW 0x69954A)")
{
	World w;
	w.engine->objectEnteredWorld(w.host.objects.at(1).info);
	CHECK(w.eval("return type(getglobal('ObjID#00000001'))") == "string:table");
	w.eval("old = getglobal('ObjID#00000001')");
	w.engine->objectChangedOwner(w.host.objects.at(1).info);
	CHECK(w.eval("return type(getglobal('ObjID#00000001'))") == "string:table");
	CHECK(w.eval("return old == getglobal('ObjID#00000001')") == "boolean:false");
	w.engine->objectLeftWorld(w.host.objects.at(1).info);
	CHECK(w.eval("return getglobal('ObjID#00000001')") == "nil");
	// OnCreated after entering the world reaches the handler
	w.engine->objectEnteredWorld(w.host.objects.at(1).info);
	w.engine->dispatchInternal(LUAEVENT_OnCreated, w.info(1));
	CHECK(getLog(w) == "C");
}

TEST_CASE("engine: the logic random stream is the generator the caller names: both algorithms flow through GetRandomNumber and ObjectTestCanSufferFear")
{
	for (RandomAlgorithm algorithm : { RandomAlgorithm::ZH_CarryChain, RandomAlgorithm::RotWK_GameDat_LCG })
	{
		NameKeyGenerator keys;
		keys.init();
		GameLogicRandom random(algorithm);
		GameLogicRandom shadow(algorithm);
		RecordingHost host;
		LuaScriptEngine::Config c;
		c.keys = &keys;
		c.logicRandom = &random;
		c.host = &host;
		LuaScriptEngine e(c);
		e.loadLogicScripts("", "<SageLuaScriptSection/>");
		host.add(1);
		host.objects.at(1).hasModifier = true;
		host.objects.at(1).modifierBonus = 0.5f;
		e.registerObject(host.objects.at(1).info);
		run(*e.logic(), "r = GetRandomNumber() f = ObjectTestCanSufferFear(getglobal('ObjID#00000001'))");
		const float r = shadow.getValueReal(0.0f, 1.0f, "t", 0);
		const int d = shadow.getValue(0, 1, "t", 0);
		lua_State *L = e.logic()->state();
		lua_getglobal(L, "r");
		CHECK(lua_tonumber(L, -1) == (double)r);
		lua_getglobal(L, "f");
		CHECK(lua_toboolean(L, -1) == ((float)d <= 0.5f ? 0 : 1));
		lua_settop(L, 0);
		CHECK(random.seedArray() == shadow.seedArray());
	}
}

TEST_CASE("engine: each of the 17 internal events reaches its handler with the argument shape of its engine call site (spec 1.2, 4.4)")
{
	Rig r;
	std::string xml = "<SageLuaScriptSection><EventList Name=\"All\">";
	std::string lua = "log = ''\nfunction S(v) if type(v) == 'table' then return 'table' end return tostring(v) end\n";
	for (int i = 0; i < 17; ++i)
	{
		xml += std::string("<EventHandler EventName=\"") + kLuaInternalEventNames[i] + "\" ScriptFunctionName=\"H" + kLuaInternalEventNames[i] + "\"/>";
		lua += std::string("function H") + kLuaInternalEventNames[i] + "(a, b, c, d) log = log .. '" + kLuaInternalEventNames[i] +
			"(' .. tostring(a == getglobal('ObjID#00000001')) .. ',' .. S(b) .. ',' .. S(c) .. ',' .. S(d) .. ')' end\n";
	}
	xml += "</EventList></SageLuaScriptSection>";
	r.load(lua, xml);
	r.host.add(1);
	r.host.add(2);
	r.host.objects.at(1).info.luaEvents = r.engine->findEventList("All");
	r.engine->registerObject(r.host.objects.at(1).info);
	r.engine->registerObject(r.host.add(2).info);
	auto fire = [&](LuaInternalEvent e, const LuaEventArgs &args = LuaEventArgs()) {
		r.engine->dispatchInternal(e, r.info(1), args);
	};
	LuaEventArgs none;
	LuaEventArgs byObject;     // OnDamaged / OnSlaughtered: {the damage source / slaughterer}
	byObject.arg[0] = LuaEventArg::makeObject(2);
	LuaEventArgs byString;     // OnUnitEntered / OnUnitExited / OnGenericEvent: {a string}
	byString.arg[0] = LuaEventArg::makeString("area");
	LuaEventArgs byReal;       // OnBuildVariation: {a float}
	byReal.arg[0] = LuaEventArg::makeReal(3.0f);
	LuaEventArgs scary;        // BeScary: {self id, float range}
	scary.arg[0] = LuaEventArg::makeObject(1);
	scary.arg[1] = LuaEventArg::makeReal(75.0f);
	LuaEventArgs incoming;     // DamageIncoming: {other, delay, amount}
	incoming.arg[0] = LuaEventArg::makeObject(2);
	incoming.arg[1] = LuaEventArg::makeReal(2.0f);
	incoming.arg[2] = LuaEventArg::makeReal(9.5f);
	fire(LUAEVENT_OnDamaged, byObject);
	fire(LUAEVENT_OnDestroyed);
	fire(LUAEVENT_OnArrived);
	fire(LUAEVENT_OnUnitEntered, byString);
	fire(LUAEVENT_OnTeamEntered, byString);
	fire(LUAEVENT_OnUnitExited, byString);
	fire(LUAEVENT_OnTeamExited, byString);
	fire(LUAEVENT_OnTeamDestroyed);
	fire(LUAEVENT_BeScary, scary);
	fire(LUAEVENT_DamageIncoming, incoming);
	fire(LUAEVENT_OnAflame);
	fire(LUAEVENT_OnQuenched);
	fire(LUAEVENT_OnCreated);
	fire(LUAEVENT_OnBuildingComplete);
	fire(LUAEVENT_OnSlaughtered, byObject);
	fire(LUAEVENT_OnGenericEvent, byString);
	fire(LUAEVENT_OnBuildVariation, byReal);
	lua_State *L = r.engine->logic()->state();
	lua_getglobal(L, "log");
	const std::string log = lua_tostring(L, -1);
	lua_settop(L, 0);
	const std::string expected =
		"OnDamaged(true,table,nil,nil)"      // the second object is object 2's handle
		"OnDestroyed(true,nil,nil,nil)"
		"OnArrived(true,nil,nil,nil)"
		"OnUnitEntered(true,area,nil,nil)"
		"OnTeamEntered(true,area,nil,nil)"
		"OnUnitExited(true,area,nil,nil)"
		"OnTeamExited(true,area,nil,nil)"
		"OnTeamDestroyed(true,nil,nil,nil)"
		"BeScary(true,table,75,nil)"
		"DamageIncoming(true,table,2,9.5)"
		"OnAflame(true,nil,nil,nil)"
		"OnQuenched(true,nil,nil,nil)"
		"OnCreated(true,nil,nil,nil)"
		"OnBuildingComplete(true,nil,nil,nil)"
		"OnSlaughtered(true,table,nil,nil)"
		"OnGenericEvent(true,area,nil,nil)"
		"OnBuildVariation(true,3,nil,nil)";
	CHECK(log == expected);
	// the three events with no dispatch site in this binary are the declared-but-unfired ones (spec 0.3 item 11): the engine can still dispatch them
	CHECK(std::string(kLuaInternalEventNames[LUAEVENT_OnTeamEntered]) == "OnTeamEntered");
}
