// OpenBFME unit tests. GPL-3.0. Helpers of the Lua lane (LUA-1): evaluating Lua text in a LuaRuntime and a recording game host.

#pragma once

#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "GameLogic/ScriptEngine/LuaHost.h"
#include "GameLogic/ScriptEngine/LuaRuntime.h"
#include "GameLogic/ScriptEngine/LuaScriptEngine.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lua_ea.h"
#include "lualib.h"
}

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace luatest
{
// the values left on the stack by `code` as "type:value" tokens joined by '|' (nil, boolean, number, string, table, function)
inline std::string show(lua_State *L)
{
	std::string s;
	const int n = lua_gettop(L);
	for (int i = 1; i <= n; ++i)
	{
		if (i > 1)
		{
			s += "|";
		}
		switch (lua_type(L, i))
		{
		case LUA_TNIL:
			s += "nil";
			break;
		case LUA_TBOOLEAN:
			s += lua_toboolean(L, i) ? "boolean:true" : "boolean:false";
			break;
		case LUA_TNUMBER:
		{
			char b[64];
			std::snprintf(b, sizeof b, "number:%.17g", lua_tonumber(L, i));
			s += b;
			break;
		}
		case LUA_TSTRING:
			s += std::string("string:") + lua_tostring(L, i);
			break;
		case LUA_TTABLE:
			s += "table";
			break;
		case LUA_TFUNCTION:
			s += "function";
			break;
		default:
			s += "other";
		}
	}
	return s;
}

// status and results of one chunk, stack cleared first
struct Result
{
	int status = 0;
	std::string values;
};
inline Result run(LuaRuntime &rt, const std::string &code)
{
	lua_State *L = rt.state();
	lua_settop(L, 0);
	Result r;
	r.status = rt.doBuffer(code, code.c_str());
	r.values = show(L);
	lua_settop(L, 0);
	return r;
}

// A game host over a table of synthetic objects. Everything it is asked is recorded in `calls` (callee name + arguments);
// the callees a test does not give a behaviour to fall through to the base class (the reported stop S-124).
class RecordingHost : public LuaGameHost
{
public:
	struct Obj
	{
		LuaObjectInfo info;
		std::string templateName = "Tmpl";
		std::string objectName;
		std::string teamName = "team";
		std::string side = "Men";
		int playerIndex = 1;
		std::wstring playerName = L"Player";
		std::set<int> modelBits;
		float fearFactor = 0.0f;
		int aiKind = 0;
		bool hasModifier = false;
		float modifierBonus = 0.0f;
		std::string capturingSide;
		bool hasCapturer = false;
		std::vector<int> hordeMembers;
		bool isHorde = false;
	};
	std::map<int, Obj> objects;
	std::vector<std::string> calls;
	std::uint32_t frame = 77;
	bool audio = true;
	std::set<std::string> upgradesPlayerType; // known player upgrades
	std::set<std::string> upgradesObjectType; // known object upgrades
	std::set<std::string> playerHas;          // completed player upgrades
	std::map<std::string, int> conditionParams, actionParams;
	bool conditionResult = true;
	std::vector<int> inRange; // result of objectsInRange
	std::set<std::string> knownPowers, knownWeapons, knownDrawModules;
	std::uint32_t soundHandle = 9;
	bool soundValid = true;

	Obj &add(int id, bool hasAI = true)
	{
		Obj &o = objects[id];
		o.info.id = id;
		o.info.hasAI = hasAI;
		return o;
	}
	bool findObject(int id, LuaObjectInfo *out) override
	{
		auto it = objects.find(id);
		if (id == 0 || it == objects.end())
		{
			return false;
		}
		*out = it->second.info;
		return true;
	}
	std::uint32_t logicFrame() override { return frame; }
	bool audioAvailable() override { return audio; }
	bool objectDescription(int id, ObjectDescription *out) override
	{
		const Obj &o = objects.at(id);
		out->objectName = o.objectName;
		out->templateName = o.templateName;
		out->playerIndex = o.playerIndex;
		out->playerName = o.playerName;
		return true;
	}
	std::string objectTeamName(int id) override { return objects.at(id).teamName; }
	std::string objectPlayerSide(int id) override { return objects.at(id).side; }
	bool objectCapturingPlayerSide(int id, std::string *out) override
	{
		const Obj &o = objects.at(id);
		if (!o.hasCapturer)
		{
			return false;
		}
		*out = o.capturingSide;
		return true;
	}
	std::string objectTemplateName(int id) override { return objects.at(id).templateName; }
	bool objectModelConditionBit(int id, int bit) override { return objects.at(id).modelBits.count(bit) != 0; }
	bool objectAttributeModifier(int id, int, float *bonus) override
	{
		const Obj &o = objects.at(id);
		if (!o.hasModifier)
		{
			return false;
		}
		*bonus = o.modifierBonus;
		return true;
	}
	int objectAIKind(int id) override { return objects.at(id).aiKind; }
	std::vector<int> objectsInRange(int id, float radius, LuaRangeOrder order, int relationship, bool notOfPlayer) override
	{
		char b[160];
		std::snprintf(b, sizeof b, "objectsInRange(%d, %g, order %d, rel %d, notOfPlayer %d)", id, (double)radius, (int)order, relationship, notOfPlayer ? 1 : 0);
		calls.push_back(b);
		return inRange;
	}
	bool hordeMembers(int id, std::vector<int> *out) override
	{
		const Obj &o = objects.at(id);
		if (!o.isHorde)
		{
			return false;
		}
		*out = o.hordeMembers;
		return true;
	}
	void enterEmotion(int id, int type, int other) override { calls.push_back("enterEmotion(" + std::to_string(id) + ", " + std::to_string(type) + ", " + std::to_string(other) + ")"); }
	void enterFearState(int id, int other, bool flag) override { calls.push_back("enterFearState(" + std::to_string(id) + ", " + std::to_string(other) + ", " + (flag ? "true" : "false") + ")"); }
	void enterRampageState(int id) override { calls.push_back("enterRampageState(" + std::to_string(id) + ")"); }
	bool playObjectSound(int id, const std::string &name, std::uint32_t *handle) override
	{
		calls.push_back("playObjectSound(" + std::to_string(id) + ", " + name + ")");
		*handle = soundHandle;
		return soundValid;
	}
	void setChanting(int id, bool on) override { calls.push_back("setChanting(" + std::to_string(id) + ", " + (on ? "true" : "false") + ")"); }
	float fearFactor(int id) override { return objects.at(id).fearFactor; }
	void setFearFactor(int id, float v) override
	{
		objects.at(id).fearFactor = v;
		calls.push_back("setFearFactor(" + std::to_string(id) + ", " + std::to_string((int)v) + ")");
	}
	void setEnragedState(int id, bool on) override { calls.push_back("setEnragedState(" + std::to_string(id) + ", " + (on ? "true" : "false") + ")"); }
	bool doSpecialPower(int id, const std::string &name) override
	{
		calls.push_back("doSpecialPower(" + std::to_string(id) + ", " + name + ")");
		return knownPowers.count(name) != 0;
	}
	bool createAndFireTempWeapon(int id, const std::string &name) override
	{
		calls.push_back("createAndFireTempWeapon(" + std::to_string(id) + ", " + name + ")");
		return knownWeapons.count(name) != 0;
	}
	UpgradeLookup findUpgrade(const std::string &name) override
	{
		UpgradeLookup u;
		if (upgradesPlayerType.count(name))
		{
			u.found = true;
			u.playerType = true;
		}
		else if (upgradesObjectType.count(name))
		{
			u.found = true;
		}
		return u;
	}
	bool playerHasUpgradeComplete(int, const std::string &name) override { return playerHas.count(name) != 0; }
	void grantUpgrade(int id, const std::string &name, bool playerType) override
	{
		calls.push_back(std::string(playerType ? "Player::addUpgrade(" : "Object::giveUpgrade(") + std::to_string(id) + ", " + name + ")");
	}
	void removeUpgrade(int id, const std::string &name, bool playerType) override
	{
		calls.push_back(std::string(playerType ? "Player::removeUpgrade(" : "Object::removeUpgrade(") + std::to_string(id) + ", " + name + ")");
	}
	void setDelayedDeath(int id, bool on) override { calls.push_back("setDelayedDeath(" + std::to_string(id) + ", " + (on ? "true" : "false") + ")"); }
	bool drawableShowModule(int id, const std::string &name, bool visible, bool permanent) override
	{
		calls.push_back("showModule(" + std::to_string(id) + ", " + name + ", " + (visible ? "1" : "0") + ", " + (permanent ? "1" : "0") + ")");
		return knownDrawModules.count(name) != 0;
	}
	void drawableShowSubObject(int id, const std::string &name, bool visible, bool permanent) override
	{
		calls.push_back("showSubObject(" + std::to_string(id) + ", " + name + ", " + (visible ? "1" : "0") + ", " + (permanent ? "1" : "0") + ")");
	}
	void setGeometryActive(int id, const std::string &name, bool active) override
	{
		calls.push_back("setGeometryActive(" + std::to_string(id) + ", " + name + ", " + (active ? "true" : "false") + ")");
	}
	void changeAllegianceFromNonPlayablePlayer(int a, int b) override { calls.push_back("changeAllegiance(" + std::to_string(a) + ", " + std::to_string(b) + ")"); }
	void forbidPlayerCommands(int id, bool f) override { calls.push_back("forbidPlayerCommands(" + std::to_string(id) + ", " + (f ? "true" : "false") + ")"); }
	int scriptTemplateParamCount(bool action, const std::string &name) override
	{
		const auto &m = action ? actionParams : conditionParams;
		auto it = m.find(name);
		return it == m.end() ? -1 : it->second;
	}
	bool evaluateCondition(const std::string &name, const std::vector<ScriptArg> &args) override
	{
		calls.push_back("evaluateCondition(" + name + describe(args) + ")");
		return conditionResult;
	}
	void executeAction(const std::string &name, const std::vector<ScriptArg> &args) override
	{
		calls.push_back("executeAction(" + name + describe(args) + ")");
	}

	static std::string describe(const std::vector<ScriptArg> &args)
	{
		std::string s;
		for (const ScriptArg &a : args)
		{
			s += ", ";
			switch (a.kind)
			{
			case ScriptArg::NIL:
				s += "nil";
				break;
			case ScriptArg::NUMBER:
				s += "num(" + std::to_string(a.real) + "," + std::to_string(a.integer) + ")";
				break;
			case ScriptArg::STRING:
				s += "str(" + a.text + ")";
				break;
			case ScriptArg::BOOLEAN:
				s += "bool(" + std::to_string(a.integer) + ")";
				break;
			case ScriptArg::OBJECT:
				s += "obj(" + std::to_string(a.objectId) + ")";
				break;
			}
		}
		return s;
	}
};

// the engine over a RecordingHost, with the random generator named (the logic stream; PLAN: the algorithm has no default)
struct Rig
{
	NameKeyGenerator keys;
	GameLogicRandom random{ RandomAlgorithm::ZH_CarryChain };
	RecordingHost host;
	std::unique_ptr<LuaScriptEngine> engine;
	Rig()
	{
		keys.init();
		LuaScriptEngine::Config c;
		c.keys = &keys;
		c.logicRandom = &random;
		c.host = &host;
		engine.reset(new LuaScriptEngine(c));
		engine->init();
	}
	// the logic state with `lua` as Scripts.lua and `xml` as ScriptEvents.xml
	void load(const std::string &lua, const std::string &xml) { engine->loadLogicScripts(lua, xml); }
	LuaObjectInfo info(int id) { return host.objects.at(id).info; }
	// the printf format of the object handle global, read back from the engine's name builder
	static std::string objectGlobalNameFormat()
	{
		// "ObjID#%08x": id 0x1a2b gives "ObjID#00001a2b"
		const std::string n = LuaScriptEngine::objectGlobalName(0x1a2b);
		return n == "ObjID#00001a2b" ? "ObjID#%08x" : n;
	}
};
} // namespace luatest
