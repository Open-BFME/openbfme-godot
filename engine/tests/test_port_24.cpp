// OpenBFME unit tests. PORT-24: Object/ForceLuaRegistration, the Lua object registration of an object that has no AI.
//
// What RotWK does (read from the supplied game.dat, caveat S-001):
//   * the field is the Object field-table row RW 0xDA4838 (name "ForceLuaRegistration", parseBool RW 0x42E558, userData 0, offset 0x640 =
//     ThingTemplate + 0x640); the parser stores one byte (RW 0x42E571 `mov byte ptr [ecx], al`); the ThingTemplate constructor zeroes +0x640
//     (RW 0x74021F, next to +0x642 / +0x643) and copyFrom copies it (RW 0x6D26CA / RW 0x6D26D0);
//   * register RW 0x735941: RW 0x735950 loads Object + 0x260 (the AI) and RW 0x73595A jumps over the field test when it is non-null; with no AI,
//     RW 0x73595C loads [Object + 4] (the template), RW 0x73595F tests byte + 0x640 and RW 0x735966 jumps to the return at RW 0x735A0D when it is
//     zero, so only a template that sets the field gets the Lua object record "ObjID#%08x" (RW 0x7359DF / RW 0x7359F9);
//   * the record itself carries no handler list: dispatch RW 0x735F53 loads Object + 0x260 (RW 0x735F8F), compares it with zero (RW 0x735F95) and
//     returns when it is null (RW 0x735F9D), so an AI-less object is referenced by scripts but receives no event.
// The three call sites pass every object, so the gate is inside register and nowhere else: RW 0x68E421 (entering the world), RW 0x699623 (a change
// of owner: unregister RW 0x735A1D then register) and RW 0x667C6E (the AI's load-post-process).
//
// Expectations below are the synthetic templates' own data (the field set / not set) and the RW branch each one takes; the mod objects that carry
// the field are projectile / system objects spawned at runtime, so this file uses synthetic templates only (no game files needed).

#include "doctest.h"
#include "LogicTestUtil.h"

#include "GameClient/LiveScripting.h"
#include "GameClient/MapCreationHooks.h"
#include "GameLogic/ScriptEngine/LuaScriptEngine.h"

#include "GameLogic/Object/Object.h"

extern "C"
{
#include "lua.h"
#include "lua_ea.h"
}

#include <memory>
#include <string>
#include <variant>

using namespace logictest;

namespace
{

// Three templates of the same shape: no AI and the field, no AI and no field, an AI (an AILuaEventsList in its AI module) and no field.
const char *kPort24Objects =
	"Object P24ForcedNoAI\n"
	"  KindOf = INERT\n"
	"  ForceLuaRegistration = Yes\n"
	"End\n"
	"Object P24PlainNoAI\n"
	"  KindOf = INERT\n"
	"End\n"
	"Object P24WithAI\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AILuaEventsList = P24List\n"
	"  End\n"
	"  KindOf = INFANTRY\n"
	"End\n";

// ScriptEvents.xml knows the list the AI object names; Scripts.lua counts its OnCreated handler runs. The AI-less objects name no list at all.
const char *kPort24Xml =
	"<?xml version=\"1.0\"?>\n"
	"<SageLuaScriptSection>\n"
	"  <Events>\n"
	"    <InternalEvent Name=\"OnCreated\" />\n"
	"  </Events>\n"
	"  <EventList Name=\"P24List\">\n"
	"    <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"P24OnCreated\"/>\n"
	"  </EventList>\n"
	"</SageLuaScriptSection>\n";

const char *kPort24Lua =
	"P24Created = 0\n"
	"function P24OnCreated(self)\n"
	"\tP24Created = P24Created + 1\n"
	"end\n";

// A live world with the script engine of a game: the creation path (LiveScripting's proc) and the world hooks are the ones a live object takes.
// In the port an object "has an AI" for the registry when its template names an AILuaEventsList in a behavior module (S-147); the AIUpdateInterface
// module itself runs as the unported shell in a logic without an AIWorld, which does not touch the registration gate.
struct Port24World : LogicWorld
{
	Port24World()
		: LogicWorld(RandomAlgorithm::ZH_CarryChain, kPort24Objects)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		w.fx.mount({ { "data\\scripts\\scriptevents.xml", kPort24Xml }, { "data\\scripts\\scripts.lua", kPort24Lua } });
		scripting.reset(new LiveScripting(*logic, w.fx.fsys, w.keys));
		std::string err;
		REQUIRE_MESSAGE(scripting->start("", &err), err);
	}
	~Port24World()
	{
		if (scripting)
		{
			scripting->detach(); // the hooks capture `this`: they go before the logic does
		}
	}
	std::unique_ptr<LiveScripting> scripting;
};

// The handle table "ObjID#%08x" (RW 0x735941) of an id in the logic state, or nil.
bool hasRecord(LuaScriptEngine &engine, int id)
{
	lua_State *L = engine.logic()->state();
	lua_getglobal(L, LuaScriptEngine::objectGlobalName(id).c_str());
	const bool table = lua_type(L, -1) == LUA_TTABLE;
	lua_settop(L, 0);
	return table;
}

int recordPayload(LuaScriptEngine &engine, int id)
{
	lua_State *L = engine.logic()->state();
	lua_getglobal(L, LuaScriptEngine::objectGlobalName(id).c_str());
	const int payload = lua_type(L, -1) == LUA_TTABLE ? lua_toobjid(L, -1) : -1;
	lua_settop(L, 0);
	return payload;
}

// the creation counters of the run so far: a test compares its own object's contribution, not the whole world's
struct Counts
{
	size_t creations = 0, lists = 0, forced = 0, hooks = 0;
};

Counts counts(Port24World &f)
{
	const LiveScripting::Stats s = f.scripting->stats();
	return Counts{ s.creations, s.creationListObjects, s.creationForcedObjects, s.creationHookObjects };
}

int luaInt(LuaScriptEngine &engine, const char *global)
{
	lua_State *L = engine.logic()->state();
	lua_getglobal(L, global);
	const int value = lua_type(L, -1) == LUA_TNUMBER ? (int)lua_tonumber(L, -1) : -1;
	lua_settop(L, 0);
	return value;
}

} // namespace

TEST_CASE("PORT-24 the template's ForceLuaRegistration is read as the Object field it is (row RW 0xDA4838, parseBool RW 0x42E558, ThingTemplate + 0x640)")
{
	objtest::World w;
	const std::string err = w.load(kPort24Objects);
	REQUIRE_MESSAGE(err.empty(), err);
	const ThingTemplate *forced = w.get("P24ForcedNoAI");
	const ThingTemplate *plain = w.get("P24PlainNoAI");
	REQUIRE(forced);
	REQUIRE(plain);
	// the value the field table's parse function stores: row RW 0xDA4838 is parsed by parseBool RW 0x42E558, so the slot is one bool and Yes -> true
	const FieldValue *slot = forced->getFinalOverride()->findField("ForceLuaRegistration");
	REQUIRE(slot);
	CHECK(std::get_if<bool>(slot) != nullptr);
	CHECK(MapCreationHooks::templateForcesLuaRegistration(*forced));
	// no line: the constructor's zero (RW 0x74021F)
	CHECK_FALSE(MapCreationHooks::templateForcesLuaRegistration(*plain));
	// the parser's other side: No is false, not "set"
	const std::string noErr = w.load("Object P24ForcedOff\n  ForceLuaRegistration = No\nEnd\n");
	REQUIRE_MESSAGE(noErr.empty(), noErr);
	const ThingTemplate *off = w.get("P24ForcedOff");
	REQUIRE(off);
	CHECK_FALSE(MapCreationHooks::templateForcesLuaRegistration(*off));
}

TEST_CASE("PORT-24 an object without an AI and with ForceLuaRegistration gets its Lua object record, and no handler is sent (RW 0x73595C .. RW 0x735966)")
{
	Port24World f;
	LuaScriptEngine &engine = f.scripting->engine();
	const Counts before = counts(f);
	Object *forced = f.make("P24ForcedNoAI");
	REQUIRE(forced);
	// RW 0x7359DF / RW 0x7359F9: the record is the table whose payload is the object id
	CHECK(hasRecord(engine, (int)forced->getID()));
	CHECK(recordPayload(engine, (int)forced->getID()) == (int)forced->getID());
	// the record has no handler list (the object has no AI, so no AILuaEventsList): nothing ran for it
	CHECK(luaInt(engine, "P24Created") == 0);
	// the same object through the host: registered, no AI, no handlers
	LuaObjectInfo info;
	REQUIRE(f.scripting->findObject((int)forced->getID(), &info));
	CHECK(info.id == (int)forced->getID());
	CHECK_FALSE(info.hasAI);
	CHECK(info.forceLuaRegistration);
	CHECK(info.luaEvents == nullptr);
	// the creation report names it apart from the objects that have a handler list
	CHECK(f.scripting->stats().creations == before.creations + 1);
	CHECK(f.scripting->stats().creationForcedObjects == before.forced + 1);
	CHECK(f.scripting->stats().creationListObjects == before.lists);
	CHECK(f.scripting->stats().creationHookObjects == before.hooks);
}

TEST_CASE("PORT-24 an object with an AI is registered and sent OnCreated as before: the field is not consulted (RW 0x73595A jne)")
{
	Port24World f;
	LuaScriptEngine &engine = f.scripting->engine();
	const Counts before = counts(f);
	Object *withAI = f.make("P24WithAI");
	REQUIRE(withAI);
	CHECK(hasRecord(engine, (int)withAI->getID()));
	CHECK(luaInt(engine, "P24Created") == 1); // its AILuaEventsList's OnCreated ran once
	LuaObjectInfo info;
	REQUIRE(f.scripting->findObject((int)withAI->getID(), &info));
	CHECK(info.hasAI);
	CHECK_FALSE(info.forceLuaRegistration); // RW 0x73595A jumps over the +0x640 test
	CHECK(info.luaEvents != nullptr);
	CHECK(f.scripting->stats().creationListObjects == before.lists + 1);
	CHECK(f.scripting->stats().creationForcedObjects == before.forced);
}

TEST_CASE("PORT-24 an object without an AI and without ForceLuaRegistration gets no Lua object record (RW 0x735966 je)")
{
	Port24World f;
	LuaScriptEngine &engine = f.scripting->engine();
	const Counts before = counts(f);
	Object *plain = f.make("P24PlainNoAI");
	REQUIRE(plain);
	CHECK_FALSE(hasRecord(engine, (int)plain->getID()));
	LuaObjectInfo info;
	CHECK_FALSE(f.scripting->findObject((int)plain->getID(), &info));
	CHECK(f.scripting->stats().creationForcedObjects == before.forced);
	CHECK(f.scripting->stats().creationListObjects == before.lists);
	// and it never had one: the world entry does not register it either
	CHECK_FALSE(hasRecord(engine, (int)plain->getID()));
}
