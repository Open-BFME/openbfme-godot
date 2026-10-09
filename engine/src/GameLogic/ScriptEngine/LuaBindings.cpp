// OpenBFME. GPL-3.0.
//
// The SAGE bindings of the two Lua states: 41 registered in the logic state, 21 in the drawable state (two of them in both), as RW registers
// them (spec lua-scripting.md 3.1-3.4). TARGET facts are read from the RotWK game.dat disassembly of each function (RW 0x734xxx-0x738xxx, cited at
// each function; caveat S-001); the callee class names are inferred from Open-BFME-1 where it has the function (donor) and are reported (S-121).
//
// Conventions reproduced (3.1):
//   * every binding is `int f(lua_State*)` returning the NUMBER OF RESULTS; several return 1 without pushing (Q2): the result Lua sees is then
//     the top stack value, usually the last argument;
//   * object arguments: id = lua_toobjid(i); accepted when id != 0 or lua_type is nil; findObjectByID(0) is null. In five bindings the SECOND
//     object argument is validated against lua_type(L, 1), not 2 (Q3);
//   * numbers narrow with (float) and RW _ftol (low 32 bits of the truncating 64 bit conversion), booleans are RW lua_toboolean.

#include "GameLogic/ScriptEngine/LuaBindings.h"

#include "GameLogic/ScriptEngine/LuaBindingNames.h"
#include "GameLogic/ScriptEngine/LuaDrawableState.h"
#include "GameLogic/ScriptEngine/LuaScriptEngine.h"
#include "GameLogic/ScriptEngine/LuaStops.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lua_ea.h"
}

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
LuaScriptEngine &eng(lua_State *L)
{
	return *static_cast<LuaScriptEngine *>(LuaRuntime::of(L)->owner()); // logic state bindings only
}
LuaDrawableState &drawState(lua_State *L)
{
	return *static_cast<LuaDrawableState *>(LuaRuntime::of(L)->owner()); // drawable state bindings only
}
LuaGameHost &hostOf(lua_State *L) { return eng(L).host(); }

void inferred(lua_State *L, const char *what)
{
	eng(L).reports().report(LUA_STOP_INFERRED, what);
}

// RW 0xB5B3E0 + the accept test of 3.1: `typeIndex` is the stack index whose nil-ness lets id 0 through
bool objectArg(lua_State *L, int index, int typeIndex, int *id)
{
	*id = lua_toobjid(L, index);
	return *id != 0 || lua_type(L, typeIndex) == LUA_TNIL;
}

bool findObject(lua_State *L, int id, LuaObjectInfo *out)
{
	return id != 0 && hostOf(L).findObject(id, out);
}

// RW 0xB5B570 + the float store: lua_tonumber narrowed to float
float toFloat(lua_State *L, int index) { return (float)lua_tonumber(L, index); }

std::string toStringArg(lua_State *L, int index, bool *isNull)
{
	const char *s = lua_tostring(L, index);
	*isNull = s == nullptr;
	return s ? s : "";
}

void pushUnsigned(lua_State *L, std::uint32_t v)
{
	lua_pushnumber(L, (double)v); // RW: fild of the signed value, + 2^32 when negative
}

// ---------------------------------------------------------------------------------------------------------------------
// shared by both states
// ---------------------------------------------------------------------------------------------------------------------
// RW 0x734E41
int luaAlert(lua_State *L)
{
	const char *s = lua_tostring(L, 1);
	LuaRuntime::of(L)->recordAlert(std::string("LUA Alert: ") + (s ? s : ""));
	return 0;
}

// RW 0x73461B: TheGameLogic->frame (unsigned) as a number
int luaGetFrame(lua_State *L)
{
	pushUnsigned(L, LuaRuntime::of(L)->frame());
	return 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// logic state (RW 0x735xxx-0x738xxx)
// ---------------------------------------------------------------------------------------------------------------------
// shared by EvaluateCondition and ExecuteAction (RW 0x735BA7 / 0x735D04): numbers (lua_isnumber: numeric strings too) become a real and an
// int, other strings a string, nil an int 0 (conditions only), a boolean an int 0/1 and an object handle its id (actions only)
bool buildScriptArgs(lua_State *L, bool action, int count, std::vector<LuaGameHost::ScriptArg> *out)
{
	for (int i = 2; i - 2 < count; ++i)
	{
		LuaGameHost::ScriptArg a;
		if (lua_isnumber(L, i))
		{
			a.kind = LuaGameHost::ScriptArg::NUMBER;
			const double v = lua_tonumber(L, i);
			a.real = (float)v;
			a.integer = LuaFtol(v);
		}
		else if (lua_isstring(L, i))
		{
			a.kind = LuaGameHost::ScriptArg::STRING;
			a.text = lua_tostring(L, i);
		}
		else if (!action)
		{
			if (lua_type(L, i) != LUA_TNIL)
			{
				return false;
			}
			a.kind = LuaGameHost::ScriptArg::NIL;
			a.integer = 0;
		}
		else if (lua_type(L, i) == LUA_TBOOLEAN)
		{
			a.kind = LuaGameHost::ScriptArg::BOOLEAN;
			a.integer = lua_toboolean(L, i) ? 1 : 0;
		}
		else
		{
			if (lua_type(L, i) != LUA_TTABLE || lua_toobjid(L, i) == 0)
			{
				return false;
			}
			a.kind = LuaGameHost::ScriptArg::OBJECT;
			a.objectId = lua_toobjid(L, i);
		}
		out->push_back(a);
	}
	return true;
}

int scriptCall(lua_State *L, bool action)
{
	const char *name = lua_tostring(L, 1);
	if (!name)
	{
		return 0; // retail compares against NULL
	}
	const int params = hostOf(L).scriptTemplateParamCount(action, name);
	if (params < 0)
	{
		return 0; // no template of that InternalName (RW 0x735BFC)
	}
	if (lua_gettop(L) != params + 1)
	{
		return 0;
	}
	std::vector<LuaGameHost::ScriptArg> args;
	if (!buildScriptArgs(L, action, params, &args))
	{
		return 0;
	}
	if (action)
	{
		hostOf(L).executeAction(name, args);
		lua_pushboolean(L, 1);
	}
	else
	{
		lua_pushboolean(L, hostOf(L).evaluateCondition(name, args) ? 1 : 0);
	}
	return 1;
}

int luaEvaluateCondition(lua_State *L) { return scriptCall(L, false); } // RW 0x735BA7
int luaExecuteAction(lua_State *L) { return scriptCall(L, true); }      // RW 0x735D04

// RW 0x7366FE
int luaObjectDescription(lua_State *L)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		lua_pushnil(L);
		return 1;
	}
	LuaObjectInfo info;
	std::string text;
	LuaGameHost::ObjectDescription d;
	const bool exists = findObject(L, id, &info);
	if (exists && !hostOf(L).objectDescription(id, &d))
	{
		lua_pushnil(L); // the object exists and its description is not available (reported by the host): no text is invented
		return 1;
	}
	if (exists)
	{
		std::string player;
		for (wchar_t c : d.playerName)
		{
			player.push_back(c >= 0x20 && c < 0x7F ? (char)c : '?'); // %ls in the C locale
		}
		char buf[1024];
		if (d.objectName.empty())
		{
			std::snprintf(buf, sizeof buf, "Object %d [%s, owned by player %d (%s)]", id, d.templateName.c_str(), d.playerIndex, player.c_str());
		}
		else
		{
			std::snprintf(buf, sizeof buf, "Object %d (%s) [%s, owned by player %d (%s)]", id, d.objectName.c_str(), d.templateName.c_str(), d.playerIndex, player.c_str());
		}
		text = buf;
	}
	else
	{
		text = "<No Object>";
	}
	lua_pushstring(L, text.c_str());
	return 1;
}

// RW 0x7374A7
int luaObjectSpy(lua_State *L)
{
	const int id1 = lua_toobjid(L, 1);
	LuaObjectInfo self, other;
	if (id1 == 0 || !findObject(L, id1, &self))
	{
		if (id1 == 0)
		{
			eng(L).reports().report(LUA_STOP_SPY, "ObjectSpy failed its arguments: retail enters the debug console (RW 0x7358B2), inert in a normal install");
		}
		return 0;
	}
	const int id2 = lua_toobjid(L, 2);
	if (id2 == 0)
	{
		eng(L).reports().report(LUA_STOP_SPY, "ObjectSpy failed its arguments: retail enters the debug console (RW 0x7358B2), inert in a normal install");
		return 0;
	}
	if (!findObject(L, id2, &other))
	{
		return 0;
	}
	LuaScriptEngine &e = eng(L);
	const char *evName = lua_tostring(L, 3);
	const NameKeyType eventKey = e.keys().nameToKey(evName ? evName : "");
	if (!e.events().findEvent(eventKey).valid())
	{
		e.reports().report(LUA_STOP_SPY, "ObjectSpy: unknown event, retail enters the debug console (RW 0x7358B2), inert in a normal install");
		return 0;
	}
	const char *spyName = lua_tostring(L, 4);
	const NameKeyType spyKey = e.keys().nameToKey(spyName ? spyName : "");
	bool declared = false;
	for (NameKeyType k : e.events().scriptedEvents())
	{
		declared = declared || k == spyKey;
	}
	if (!declared)
	{
		e.reports().report(LUA_STOP_SPY, "ObjectSpy: spy event is not a declared ScriptedEvent, retail enters the debug console (RW 0x7358B2), inert in a normal install");
		return 0;
	}
	if (self.hasAI)
	{
		e.addSpy(id1, eventKey, spyKey);
	}
	return 0;
}

// RW 0x737269
int luaObjectDispatchEvent(lua_State *L)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		return 0;
	}
	LuaObjectInfo self;
	if (!findObject(L, id, &self))
	{
		return 0;
	}
	const int id2 = lua_toobjid(L, 2);
	if (id2 == 0 && lua_type(L, 1) != LUA_TNIL) // Q3: index 1
	{
		return 0;
	}
	const char *name = lua_tostring(L, 3);
	if (!name)
	{
		return 0;
	}
	LuaScriptEngine &e = eng(L);
	const LuaEventRef ev = e.findEvent(name);
	if (!ev.valid())
	{
		return 0;
	}
	LuaEventArgs args;
	args.arg[0] = LuaEventArg::makeObject(id2);
	const char *data = lua_tostring(L, 4);
	args.arg[1] = LuaEventArg::makeString(data ? data : "");
	e.dispatch(ev, self, args);
	return 0;
}

// RW 0x73316F: one Lua value to a dispatch argument record
LuaEventArg argFromLua(lua_State *L, int index)
{
	switch (lua_type(L, index))
	{
	case LUA_TNUMBER:
		return LuaEventArg::makeReal((float)lua_tonumber(L, index));
	case LUA_TSTRING:
		return LuaEventArg::makeString(lua_tostring(L, index));
	case LUA_TTABLE:
		return LuaEventArg::makeObject(lua_toobjid(L, index));
	case LUA_TBOOLEAN:
		return LuaEventArg::makeBool(lua_toboolean(L, index) != 0);
	default:
		return LuaEventArg();
	}
}

// RW 0x737D2D (enemies, relationship 1), 0x737F35 (allies, 4), 0x7380A2 (civilians = neutral, 2), 0x738246 (units: the player filter)
int broadcastTo(lua_State *L, int relationship, bool notOfPlayer)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		return 0;
	}
	LuaObjectInfo self;
	if (!findObject(L, id, &self))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	if (!name)
	{
		return 0;
	}
	LuaScriptEngine &e = eng(L);
	const LuaEventRef ev = e.findEvent(name);
	if (!ev.valid())
	{
		return 0;
	}
	const float range = toFloat(L, 3);
	LuaEventArgs args;
	args.arg[0] = LuaEventArg::makeObject(id);
	if (const char *text = lua_tostring(L, 4))
	{
		args.arg[1] = LuaEventArg::makeString(text);
	}
	if (notOfPlayer)
	{
		inferred(L, "ObjectBroadcastEventToUnits: the player filter RW 0x660D2C(self, 0) is read as 'objects of another player than self's' (ZH PartitionFilterPlayer(player, false)), spec 3.2 #11");
	}
	const std::vector<int> targets = hostOf(L).objectsInRange(id, range, LUA_RANGE_NEAR_TO_FAR, relationship, notOfPlayer);
	for (int target : targets)
	{
		LuaObjectInfo other;
		if (findObject(L, target, &other))
		{
			e.dispatch(ev, other, args);
		}
	}
	return 0;
}
int luaBroadcastEnemies(lua_State *L) { return broadcastTo(L, LUA_REL_ENEMIES, false); }
int luaBroadcastAllies(lua_State *L) { return broadcastTo(L, LUA_REL_ALLIES, false); }
int luaBroadcastCivilians(lua_State *L) { return broadcastTo(L, LUA_REL_NEUTRAL, false); }
int luaBroadcastUnits(lua_State *L) { return broadcastTo(L, 0, true); }

// RW 0x737380: no nil tolerance for the horde argument
int luaHordeBroadcast(lua_State *L)
{
	const int id = lua_toobjid(L, 1);
	if (id == 0)
	{
		return 0;
	}
	LuaObjectInfo horde;
	if (!findObject(L, id, &horde))
	{
		return 0;
	}
	std::vector<int> members;
	if (!hostOf(L).hordeMembers(id, &members))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	if (!name)
	{
		return 0;
	}
	LuaScriptEngine &e = eng(L);
	const LuaEventRef ev = e.findEvent(name);
	if (!ev.valid())
	{
		return 0;
	}
	LuaEventArgs args;
	args.arg[0] = argFromLua(L, 3); // only record 0, from Lua index 3
	for (int m : members)
	{
		LuaObjectInfo member;
		if (findObject(L, m, &member))
		{
			e.dispatch(ev, member, args);
		}
	}
	return 0;
}

// RW 0x736770
int luaObjectTeamName(lua_State *L)
{
	int id;
	LuaObjectInfo o;
	if (objectArg(L, 1, 1, &id) && findObject(L, id, &o))
	{
		lua_pushstring(L, hostOf(L).objectTeamName(id).c_str());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// RW 0x7367E3
int luaObjectPlayerSide(lua_State *L)
{
	int id;
	LuaObjectInfo o;
	if (objectArg(L, 1, 1, &id) && findObject(L, id, &o))
	{
		inferred(L, "ObjectPlayerSide: the string at +0x58 of the record RW 0x79FD6F returns for the object's team is read as the owner's side name (spec 3.2 #14)");
		lua_pushstring(L, hostOf(L).objectPlayerSide(id).c_str());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// RW 0x73684D
int luaObjectCapturingPlayerSide(lua_State *L)
{
	int id;
	LuaObjectInfo o;
	std::string side;
	if (objectArg(L, 1, 1, &id) && findObject(L, id, &o) && hostOf(L).objectCapturingPlayerSide(id, &side))
	{
		lua_pushstring(L, side.c_str());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// RW 0x7368CA
int luaObjectTemplateName(lua_State *L)
{
	int id;
	LuaObjectInfo o;
	if (objectArg(L, 1, 1, &id) && findObject(L, id, &o))
	{
		lua_pushstring(L, hostOf(L).objectTemplateName(id).c_str());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// RW 0x73692C
int luaObjectTestModelCondition(lua_State *L)
{
	int id;
	LuaObjectInfo o;
	if (objectArg(L, 1, 1, &id) && findObject(L, id, &o))
	{
		const char *name = lua_tostring(L, 2);
		if (name)
		{
			const int bit = ModelCondition::indexOf(name); // RW 0x4B3B5B: -1 for an unknown name
			if (bit != -1)
			{
				lua_pushboolean(L, hostOf(L).objectModelConditionBit(id, bit) ? 1 : 0);
				return 1;
			}
		}
	}
	lua_pushnil(L);
	return 1;
}

// RW 0x7369AE: an invalid argument pushes nil and returns 0 results
int luaObjectTestCanSufferFear(lua_State *L)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		lua_pushnil(L);
		return 0;
	}
	LuaObjectInfo o;
	if (!findObject(L, id, &o))
	{
		lua_pushnil(L);
		return 1;
	}
	bool result = true;
	float bonus = 0.0f;
	if (hostOf(L).objectAttributeModifier(id, 4, &bonus))
	{
		// RW 0x736A12: GetGameLogicRandomValue(0, 1, ..., 0x85A) on the logic stream; (float)r <= bonus: the object cannot suffer fear
		const int r = eng(L).logicRandom().getValue(0, 1, "LuaScriptEngine.cpp", 0x85A);
		if ((float)r <= bonus)
		{
			result = false;
		}
	}
	if (o.hasAI && hostOf(L).objectAIKind(id) == 0x2D)
	{
		result = false;
	}
	lua_pushboolean(L, result ? 1 : 0);
	return 1;
}

// RW 0x737BF7
int luaObjectCountNearbyEnemies(lua_State *L)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		lua_pushnil(L);
		return 0;
	}
	const int radius = LuaFtol(lua_tonumber(L, 2));
	LuaObjectInfo o;
	if (!findObject(L, id, &o))
	{
		lua_pushnumber(L, 0.0);
		return 1;
	}
	const std::vector<int> found = hostOf(L).objectsInRange(id, (float)radius, LUA_RANGE_FASTEST, LUA_REL_ENEMIES, false);
	lua_pushnumber(L, (double)found.size());
	return 1;
}

// RW 0x73898B
int luaObjectEnterFearState(lua_State *L)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		return 0;
	}
	LuaObjectInfo self;
	if (!findObject(L, id, &self))
	{
		return 0;
	}
	const int id2 = lua_toobjid(L, 2);
	if (id2 == 0 && lua_type(L, 1) != LUA_TNIL) // Q3
	{
		return 0;
	}
	LuaObjectInfo other;
	if (!findObject(L, id2, &other))
	{
		return 0;
	}
	const bool flag = lua_toboolean(L, 3) != 0;
	if (self.hasAI)
	{
		inferred(L, "ObjectEnterFearState: AI command 0x2F with the second object (RW 0x73841C), then the AI flag at +0x3C4 (spec G2)");
		hostOf(L).enterFearState(id, id2, flag);
	}
	return 0;
}

// the four emotion bindings: RW 0x736A69 (6), 0x736AE1 (4), 0x736BA0 (5), 0x736B59 (9, no second object); second argument checked against index 1 (Q3)
int enterEmotion(lua_State *L, int type, bool hasOther)
{
	int id;
	if (!objectArg(L, 1, 1, &id))
	{
		return 0;
	}
	LuaObjectInfo self;
	if (!findObject(L, id, &self))
	{
		return 0;
	}
	int otherId = 0;
	if (hasOther)
	{
		otherId = lua_toobjid(L, 2);
		if (otherId == 0 && lua_type(L, 1) != LUA_TNIL) // Q3
		{
			return 0;
		}
		LuaObjectInfo other;
		if (!findObject(L, otherId, &other))
		{
			return 0;
		}
	}
	inferred(L, "ObjectEnter*State: the emotion type numbers 4 cower, 5 uncontrollable cower, 6 run away panic, 9 alert are raw numbers in RW 0x68F37F (spec G10)");
	hostOf(L).enterEmotion(id, type, otherId);
	return 0;
}
int luaObjectEnterRunAwayPanicState(lua_State *L) { return enterEmotion(L, 6, true); }
int luaObjectEnterCowerState(lua_State *L) { return enterEmotion(L, 4, true); }
int luaObjectEnterUncontrollableCowerState(lua_State *L) { return enterEmotion(L, 5, true); }
int luaObjectEnterAlertState(lua_State *L) { return enterEmotion(L, 9, false); }

// RW 0x738A24
int luaObjectEnterRampageState(lua_State *L)
{
	int id;
	LuaObjectInfo o;
	if (objectArg(L, 1, 1, &id) && findObject(L, id, &o))
	{
		inferred(L, "ObjectEnterRampageState: the module at Object+0x25C vtable +0x98 and the AI command 0x3A are not identified (spec G9)");
		hostOf(L).enterRampageState(id);
	}
	return 0;
}

// RW 0x736FCD: returns 1 without pushing when the event has no audio info
int luaObjectPlaySound(lua_State *L)
{
	if (lua_gettop(L) < 2 || !hostOf(L).audioAvailable())
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	std::uint32_t handle = 0;
	if (hostOf(L).playObjectSound(id, name ? name : "", &handle))
	{
		if (handle >= 5)
		{
			pushUnsigned(L, handle);
		}
		else
		{
			lua_pushnil(L);
		}
	}
	return 1;
}

// RW 0x7370FF, 0x7371F8: argc >= 2; nil first argument allowed; return 1 without pushing (Q2)
int luaObjectSetChanting(lua_State *L)
{
	if (lua_gettop(L) < 2)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	hostOf(L).setChanting(id, lua_toboolean(L, 2) != 0);
	return 1;
}

// RW 0x737170
int luaObjectSetFearFactor(lua_State *L)
{
	if (lua_gettop(L) < 2)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const int n = LuaFtol(lua_tonumber(L, 2));
	const int current = (int)hostOf(L).fearFactor(id); // cvttss2si
	hostOf(L).setFearFactor(id, (float)(n > current ? n : current));
	return 1;
}

int luaObjectSetEnragedState(lua_State *L)
{
	if (lua_gettop(L) < 2)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	hostOf(L).setEnragedState(id, lua_toboolean(L, 2) != 0);
	return 1;
}

// RW 0x736C16: argc >= 2 and TheAudio; nil first argument allowed; 1 (no push) when the power's module exists
int luaObjectDoSpecialPower(lua_State *L)
{
	if (lua_gettop(L) < 2 || !hostOf(L).audioAvailable())
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	return hostOf(L).doSpecialPower(id, name ? name : "") ? 1 : 0;
}

// RW 0x736D3B
int luaObjectCreateAndFireTempWeapon(lua_State *L)
{
	if (lua_gettop(L) < 2 || !hostOf(L).audioAvailable())
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	return hostOf(L).createAndFireTempWeapon(id, name ? name : "") ? 1 : 0;
}

// RW 0x736ED1 (Q6): 1.0 / 0.0 only for a player upgrade; nothing for an unknown upgrade or a bad argument
int luaObjectHasUpgrade(lua_State *L)
{
	if (lua_gettop(L) < 2)
	{
		return 0;
	}
	int id = lua_toobjid(L, 1);
	if (id == 0 && lua_type(L, 1) != LUA_TNIL)
	{
		return 0;
	}
	LuaObjectInfo o;
	if (!findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	const LuaGameHost::UpgradeLookup up = hostOf(L).findUpgrade(name ? name : "");
	if (!up.found)
	{
		return 0;
	}
	double result = 0.0;
	if (up.playerType && hostOf(L).playerHasUpgradeComplete(id, name ? name : ""))
	{
		result = 1.0;
	}
	lua_pushnumber(L, result);
	return 1;
}

// RW 0x736DF5(L, grant): ObjectGrantUpgrade passes 1 (RW 0x736FB1), ObjectRemoveUpgrade 0 (RW 0x736FBF). No TheAudio check in RotWK.
int upgradeCall(lua_State *L, bool grant)
{
	if (lua_gettop(L) < 2)
	{
		return 0;
	}
	int id = lua_toobjid(L, 1);
	if (id == 0 && lua_type(L, 1) != LUA_TNIL)
	{
		return 0;
	}
	LuaObjectInfo o;
	if (!findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	const LuaGameHost::UpgradeLookup up = hostOf(L).findUpgrade(name ? name : "");
	if (!up.found)
	{
		return 0;
	}
	if (grant)
	{
		hostOf(L).grantUpgrade(id, name ? name : "", up.playerType);
	}
	else
	{
		hostOf(L).removeUpgrade(id, name ? name : "", up.playerType);
	}
	return 1;
}
int luaObjectGrantUpgrade(lua_State *L) { return upgradeCall(L, true); }
int luaObjectRemoveUpgrade(lua_State *L) { return upgradeCall(L, false); }

// RW 0x736CC8: returns 0 results
int luaObjectSetDelayedDeath(lua_State *L)
{
	if (lua_gettop(L) < 2)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	inferred(L, "ObjectSetDelayedDeath: the module at Object+0x25C vtable +0x98 is not identified (spec G9)");
	hostOf(L).setDelayedDeath(id, lua_toboolean(L, 2) != 0);
	return 0;
}

// RW 0x73635C: exactly 3 arguments; drawable->showSubObject(name, !hide, permanent 0)
int luaObjectHideSubObject(lua_State *L)
{
	if (lua_gettop(L) != 3)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	hostOf(L).drawableShowSubObject(id, name ? name : "", lua_toboolean(L, 3) == 0, false);
	return 0;
}

// RW 0x736412: first showModule(name, visible 0, permanent 1); only when no module has that tag the sub object
int luaObjectHideSubObjectPermanently(lua_State *L)
{
	if (lua_gettop(L) != 3)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	if (hostOf(L).drawableShowModule(id, name ? name : "", false, true))
	{
		return 0;
	}
	hostOf(L).drawableShowSubObject(id, lua_tostring(L, 2) ? lua_tostring(L, 2) : "", lua_toboolean(L, 3) == 0, true);
	return 0;
}

// RW 0x736519
int luaObjectSetGeometryActive(lua_State *L)
{
	if (lua_gettop(L) != 3)
	{
		return 0;
	}
	int id;
	LuaObjectInfo o;
	if (!objectArg(L, 1, 1, &id) || !findObject(L, id, &o))
	{
		return 0;
	}
	const char *name = lua_tostring(L, 2);
	inferred(L, "ObjectSetGeometryActive: RW 0xAD3520 is read as 'activate / deactivate a named geometry shape' (spec 3.2 #38)");
	hostOf(L).setGeometryActive(id, name ? name : "", lua_toboolean(L, 3) != 0);
	return 0;
}

// RW 0x7365BE: both ids must be non-zero (no nil tolerance)
int luaObjectChangeAllegiance(lua_State *L)
{
	const int a = lua_toobjid(L, 1);
	const int b = lua_toobjid(L, 2);
	if (b == 0 || a == 0)
	{
		return 0;
	}
	LuaObjectInfo self, other;
	if (!findObject(L, a, &self) || !findObject(L, b, &other))
	{
		return 0;
	}
	hostOf(L).changeAllegianceFromNonPlayablePlayer(a, b);
	return 0;
}

// RW 0x73474F: one draw of the logic generator
int luaGetRandomNumber(lua_State *L)
{
	lua_pushnumber(L, (double)eng(L).logicRandom().getValueReal(0.0f, 1.0f, "LuaScriptEngine.cpp", 0x74F));
	return 1;
}

// RW 0x736635: no nil tolerance
int luaObjectForbidPlayerCommands(lua_State *L)
{
	const int id = lua_toobjid(L, 1);
	if (id == 0)
	{
		return 0;
	}
	LuaObjectInfo o;
	if (!findObject(L, id, &o))
	{
		return 0;
	}
	if (o.hasAI)
	{
		hostOf(L).forbidPlayerCommands(id, lua_toboolean(L, 2) != 0);
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// drawable state (RW 0x734xxx-0x735xxx)
// ---------------------------------------------------------------------------------------------------------------------
LuaDrawableContext *ctxOf(lua_State *L) { return drawState(L).context(); }

// RW 0x734924: 1.0 when the drawable's model condition bit is set, else nil; returns 1
int luaCurDrawableModelcondition(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c && lua_gettop(L) > 0)
	{
		if (const char *name = lua_tostring(L, 1))
		{
			if (ModelCondition::indexOf(name) != -1 && c->modelCondition(name))
			{
				lua_pushnumber(L, 1.0);
				return 1;
			}
		}
	}
	lua_pushnil(L);
	return 1;
}

// RW 0x73499B
int luaCurDrawableObjectStatus(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c && lua_gettop(L) > 0)
	{
		if (const char *name = lua_tostring(L, 1))
		{
			if (c->objectStatus(name))
			{
				lua_pushnumber(L, 1.0);
				return 1;
			}
		}
	}
	lua_pushnil(L);
	return 1;
}

// RW 0x734F5A / 0x734E94: module first (visible / hidden, not permanent), else the sub object
int showHideSubObject(lua_State *L, bool visible)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c && lua_gettop(L) > 0)
	{
		const char *name = lua_tostring(L, 1);
		const std::string n = name ? name : "";
		if (c->hasModule(n))
		{
			c->showModule(n, visible, false); // RW 0x734EF4: a draw module of that tag takes the request
		}
		else
		{
			c->showSubObject(n, visible, false);
		}
	}
	return 0;
}
int luaCurDrawableShowSubObject(lua_State *L) { return showHideSubObject(L, true); }
int luaCurDrawableHideSubObject(lua_State *L) { return showHideSubObject(L, false); }

// RW 0x73509E / 0x73501F: the sub object only, permanent
int showHideSubObjectPermanently(lua_State *L, bool visible)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c && lua_gettop(L) > 0)
	{
		const char *name = lua_tostring(L, 1);
		c->showSubObject(name ? name : "", visible, true);
	}
	return 0;
}
int luaCurDrawableShowSubObjectPermanently(lua_State *L) { return showHideSubObjectPermanently(L, true); }
int luaCurDrawableHideSubObjectPermanently(lua_State *L) { return showHideSubObjectPermanently(L, false); }

// RW 0x735192 / 0x7351AB via 0x73511D: showModule(name, visible = (hide == 0), permanent 0); returns 0 results (the argument is read without a count check)
int luaCurDrawableHideModule(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c)
	{
		const char *name = lua_tostring(L, 1);
		c->showModule(name ? name : "", false, false);
	}
	return 0;
}
int luaCurDrawableShowModule(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c)
	{
		const char *name = lua_tostring(L, 1);
		c->showModule(name ? name : "", true, false);
	}
	return 0;
}

// RW 0x7351C4 / 0x735230: the string when non-empty, else nil
int luaCurDrawablePrevAnimationState(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	const std::string s = c ? c->previousAnimationState() : std::string();
	if (!s.empty())
	{
		lua_pushstring(L, s.c_str());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}
int luaCurDrawablePrevAnimation(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	const std::string s = c ? c->previousAnimation() : std::string();
	if (!s.empty())
	{
		lua_pushstring(L, s.c_str());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

int pushTarget(lua_State *L, bool (LuaDrawableContext::*fn)(double *))
{
	LuaDrawableContext *c = ctxOf(L);
	double v = 0.0;
	if (c && (c->*fn)(&v))
	{
		lua_pushnumber(L, v);
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}
int luaCurDrawableGetCurrentTargetDistance(lua_State *L) { return pushTarget(L, &LuaDrawableContext::targetDistance); } // RW 0x734A18
int luaCurDrawableGetCurrentTargetHeight(lua_State *L) { return pushTarget(L, &LuaDrawableContext::targetHeight); }     // RW 0x734649
int luaCurDrawableGetCurrentTargetBearing(lua_State *L) { return pushTarget(L, &LuaDrawableContext::targetBearing); }   // RW 0x734A75

// RW 0x7346FC: 0.0 without a context
int luaCurDrawablePrevAnimFraction(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	lua_pushnumber(L, (double)(c ? c->previousAnimFraction() : 0.0f));
	return 1;
}

// RW 0x734ACE
int luaCurDrawableSetTransitionAnimState(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c && lua_gettop(L) > 0)
	{
		const char *name = lua_tostring(L, 1);
		c->setTransitionAnimState(name ? name : "");
	}
	return 0;
}

// RW 0x734739
int luaCurDrawableAllowToContinue(lua_State *L)
{
	if (LuaDrawableContext *c = ctxOf(L))
	{
		c->allowToContinue();
	}
	return 0;
}

// RW 0x73529F: ctx.drawable, argc > 0, TheAudio
int luaCurDrawablePlaySound(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (c && lua_gettop(L) > 0 && drawState(L).audioAvailable())
	{
		const char *name = lua_tostring(L, 1);
		c->playSound(name ? name : "");
	}
	return 0;
}

// RW 0x73667D (Q8): nil and then false when the context chain is missing; false without a target / when it is not of that kind
int luaCurDrawableIsCurrentTargetKindof(lua_State *L)
{
	LuaDrawableContext *c = ctxOf(L);
	if (!c)
	{
		lua_pushnil(L);
		lua_pushboolean(L, 0);
		return 1;
	}
	int r = 1;
	if (lua_gettop(L) > 0)
	{
		const char *name = lua_tostring(L, 1);
		r = c->targetKindOf(name ? name : "");
	}
	if (r == 0)
	{
		lua_pushnil(L);
		lua_pushboolean(L, 0);
	}
	else
	{
		lua_pushboolean(L, r == 2 ? 1 : 0);
	}
	return 1;
}

// RW 0x734695: argc < 2 pushes 0.0; else the client random stream
int luaGetClientRandomNumberReal(lua_State *L)
{
	if (lua_gettop(L) <= 1)
	{
		lua_pushnumber(L, 0.0);
		return 1;
	}
	const float lo = toFloat(L, 1);
	const float hi = toFloat(L, 2);
	LuaDrawableContext *c = ctxOf(L);
	if (!c)
	{
		LuaRuntime::of(L)->sink().report(LUA_STOP_DRAW_CONTEXT, "GetClientRandomNumberReal was called outside a BeginScript: the client random stream is the draw context's, there is none");
		lua_pushnumber(L, 0.0);
		return 1;
	}
	lua_pushnumber(L, (double)c->clientRandomReal(lo, hi));
	return 1;
}

const lua_CFunction kLogicFunctions[41] = {
	luaAlert, luaGetFrame, luaEvaluateCondition, luaExecuteAction, luaObjectDescription, luaObjectSpy, luaObjectDispatchEvent,
	luaBroadcastEnemies, luaBroadcastAllies, luaBroadcastCivilians, luaBroadcastUnits, luaHordeBroadcast, luaObjectTeamName,
	luaObjectPlayerSide, luaObjectCapturingPlayerSide, luaObjectTemplateName, luaObjectTestModelCondition, luaObjectTestCanSufferFear,
	luaObjectCountNearbyEnemies, luaObjectEnterFearState, luaObjectEnterRunAwayPanicState, luaObjectEnterCowerState,
	luaObjectEnterUncontrollableCowerState, luaObjectEnterAlertState, luaObjectEnterRampageState, luaObjectPlaySound, luaObjectSetChanting,
	luaObjectSetFearFactor, luaObjectSetEnragedState, luaObjectDoSpecialPower, luaObjectCreateAndFireTempWeapon, luaObjectHasUpgrade,
	luaObjectGrantUpgrade, luaObjectRemoveUpgrade, luaObjectSetDelayedDeath, luaObjectHideSubObject, luaObjectHideSubObjectPermanently,
	luaObjectSetGeometryActive, luaObjectChangeAllegiance, luaGetRandomNumber, luaObjectForbidPlayerCommands
};

const lua_CFunction kDrawableFunctions[21] = {
	luaAlert, luaGetFrame, luaCurDrawableModelcondition, luaCurDrawableObjectStatus, luaCurDrawableShowSubObject, luaCurDrawableHideSubObject,
	luaCurDrawableShowSubObjectPermanently, luaCurDrawableHideSubObjectPermanently, luaCurDrawableHideModule, luaCurDrawableShowModule,
	luaCurDrawablePrevAnimationState, luaCurDrawablePrevAnimation, luaCurDrawableGetCurrentTargetDistance, luaCurDrawableGetCurrentTargetHeight,
	luaCurDrawableGetCurrentTargetBearing, luaCurDrawablePrevAnimFraction, luaCurDrawableSetTransitionAnimState, luaCurDrawableAllowToContinue,
	luaCurDrawablePlaySound, luaCurDrawableIsCurrentTargetKindof, luaGetClientRandomNumberReal
};
} // namespace

int LuaFtol(double d)
{
	return luaEA_ftol(d); // the 64 bit integer indefinite 0x8000000000000000 has low word 0
}

void LuaRegisterLogicBindings(lua_State *L)
{
	for (int i = 0; i < 41; ++i)
	{
		lua_pushcclosure(L, kLogicFunctions[i], 0);
		lua_setglobal(L, kLuaLogicBindingNames[i]);
	}
}

void LuaRegisterDrawableBindings(lua_State *L)
{
	for (int i = 0; i < 21; ++i)
	{
		lua_pushcclosure(L, kDrawableFunctions[i], 0);
		lua_setglobal(L, kLuaDrawableBindingNames[i]);
	}
}
