// OpenBFME. GPL-3.0. See LuaScriptEngine.h.

#include "GameLogic/ScriptEngine/LuaScriptEngine.h"

#include "GameLogic/ScriptEngine/LuaBindings.h"
#include "GameLogic/ScriptEngine/LuaStops.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lua_ea.h"
}

#include <cstdio>
#include <stdexcept>

namespace
{
const char *const kScriptsLua = "Data\\Scripts\\Scripts.lua";          // RW 0xC24AC8
const char *const kScriptEventsXml = "Data\\Scripts\\ScriptEvents.xml"; // RW 0xC24AA8
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// LuaGameHost defaults: every callee that is not implemented is a reported stop
// ---------------------------------------------------------------------------------------------------------------------
void LuaGameHost::unimplemented(const char *callee) const
{
	if (!m_sink)
	{
		throw std::logic_error(std::string("LuaGameHost: the callee ") + callee + " is not implemented and the host has no report sink (attach it to a LuaScriptEngine)");
	}
	m_sink->report(LUA_STOP_CALLEE, std::string("engine callee not implemented: ") + callee);
}

std::uint32_t LuaGameHost::logicFrame()
{
	unimplemented("TheGameLogic frame counter (RW 0xDE412C+0x40, GetFrame RW 0x73461B)");
	return 0;
}
bool LuaGameHost::objectDescription(int, ObjectDescription *)
{
	unimplemented("object description fields (RW 0x69005F)");
	return false;
}
std::string LuaGameHost::objectTeamName(int)
{
	unimplemented("Object team -> prototype name (RW 0x736770)");
	return std::string();
}
std::string LuaGameHost::objectPlayerSide(int)
{
	unimplemented("Object team side name (RW 0x7367E3 / 0x79FD6F)");
	return std::string();
}
bool LuaGameHost::objectCapturingPlayerSide(int, std::string *)
{
	unimplemented("capturing object's player side (RW 0x73684D)");
	return false;
}
std::string LuaGameHost::objectTemplateName(int)
{
	unimplemented("Object template name (RW 0x7368CA)");
	return std::string();
}
bool LuaGameHost::objectModelConditionBit(int, int)
{
	unimplemented("Object model condition flags (RW 0x73692C, Object+0x10C)");
	return false;
}
bool LuaGameHost::objectAttributeModifier(int, int, float *)
{
	unimplemented("Object attribute modifier (RW 0x68C818)");
	return false;
}
int LuaGameHost::objectAIKind(int)
{
	unimplemented("AIUpdateInterface kind (RW 0x662D70)");
	return -1;
}
std::vector<int> LuaGameHost::objectsInRange(int, float, LuaRangeOrder, int, bool)
{
	unimplemented("PartitionManager object query (RW 0xA39340)");
	return std::vector<int>();
}
bool LuaGameHost::hordeMembers(int, std::vector<int> *)
{
	unimplemented("horde contain member list (Object+0x258 vtable +0x7C / +0x10C)");
	return false;
}
void LuaGameHost::enterEmotion(int, int, int) { unimplemented("Object emotion state (RW 0x68F37F)"); }
void LuaGameHost::enterFearState(int, int, bool) { unimplemented("AI fear state command 0x2F (RW 0x73841C)"); }
void LuaGameHost::enterRampageState(int) { unimplemented("rampage state (RW 0x738A24: module +0x98, AI command 0x3A)"); }
bool LuaGameHost::playObjectSound(int, const std::string &, std::uint32_t *)
{
	unimplemented("TheAudio object sound (RW 0x736FCD: vtable +0x12C / +0x64)");
	return false;
}
void LuaGameHost::setChanting(int, bool) { unimplemented("Object::setChanting (RW 0x691784)"); }
float LuaGameHost::fearFactor(int)
{
	unimplemented("Object fear factor (Object+0x1AC)");
	return 0.0f;
}
void LuaGameHost::setFearFactor(int, float) { unimplemented("Object fear factor (Object+0x1AC)"); }
void LuaGameHost::setEnragedState(int, bool) { unimplemented("Object enraged state (RW 0x68F2F1)"); }
bool LuaGameHost::doSpecialPower(int, const std::string &)
{
	unimplemented("SpecialPowerStore + special power module (RW 0x69C146, 0x691526)");
	return false;
}
bool LuaGameHost::createAndFireTempWeapon(int, const std::string &)
{
	unimplemented("WeaponStore createAndFireTempWeapon (RW 0x6CC5DF, 0x6CF530)");
	return false;
}
LuaGameHost::UpgradeLookup LuaGameHost::findUpgrade(const std::string &)
{
	unimplemented("TheUpgradeCenter->findUpgrade (RW 0x66F5E5)");
	return UpgradeLookup();
}
bool LuaGameHost::playerHasUpgradeComplete(int, const std::string &)
{
	unimplemented("Player::hasUpgradeComplete (RW 0x6AC2AF)");
	return false;
}
void LuaGameHost::grantUpgrade(int, const std::string &, bool playerType)
{
	unimplemented(playerType ? "Player::addUpgrade (RW 0x6AEE22)" : "Object::giveUpgrade (RW 0x69388B)");
}
void LuaGameHost::removeUpgrade(int, const std::string &, bool playerType)
{
	unimplemented(playerType ? "Player::removeUpgrade (RW 0x6AE60C)" : "Object::removeUpgrade (RW 0x691438)");
}
void LuaGameHost::setDelayedDeath(int, bool) { unimplemented("delayed death module (Object+0x25C vtable +0x98)"); }
bool LuaGameHost::drawableShowModule(int, const std::string &, bool, bool)
{
	unimplemented("Drawable::showModule (RW 0x6789B4)");
	return false;
}
void LuaGameHost::drawableShowSubObject(int, const std::string &, bool, bool) { unimplemented("Drawable::showSubObject (RW 0x672823)"); }
void LuaGameHost::setGeometryActive(int, const std::string &, bool) { unimplemented("Object geometry shape toggle (RW 0xAD3520)"); }
void LuaGameHost::changeAllegianceFromNonPlayablePlayer(int, int) { unimplemented("AI allegiance (RW 0x7365BE: AI+0x240 = team+0x34)"); }
void LuaGameHost::forbidPlayerCommands(int, bool) { unimplemented("AIUpdateInterface forbidPlayerCommands (AI+0x3C5)"); }
int LuaGameHost::scriptTemplateParamCount(bool, const std::string &)
{
	unimplemented("ScriptEngine condition / action templates (RW 0x60328B, scripts.ini)");
	return -1;
}
bool LuaGameHost::evaluateCondition(const std::string &, const std::vector<ScriptArg> &)
{
	unimplemented("ScriptEngine::evaluateCondition (RW 0xDE8844 vtable +0x38)");
	return false;
}
void LuaGameHost::executeAction(const std::string &, const std::vector<ScriptArg> &)
{
	unimplemented("ScriptEngine::executeAction (RW 0xDE87D8 vtable +0x38)");
}

// ---------------------------------------------------------------------------------------------------------------------
// LuaScriptEngine
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
NameKeyGenerator &requireConfig(const LuaScriptEngine::Config &c)
{
	if (!c.keys || !c.logicRandom || !c.host)
	{
		throw std::invalid_argument("LuaScriptEngine: keys, logicRandom and host are required (no defaults, PLAN rule 10)");
	}
	return *c.keys;
}
} // namespace

LuaScriptEngine::LuaScriptEngine(const Config &config)
	: m_config(config)
	, m_keys(&requireConfig(config))
	, m_logicRandom(config.logicRandom)
	, m_host(config.host)
	, m_registry(*m_keys)
{
	m_host->attach(&m_sink);
}

LuaScriptEngine::~LuaScriptEngine() = default;

size_t LuaScriptEngine::compatibilityFaults() const
{
	return (m_logic ? m_logic->faults().size() : 0) + (m_drawable ? m_drawable->runtime().faults().size() : 0);
}

std::uint32_t LuaScriptEngine::frame() const
{
	return m_config.frame ? m_config.frame() : m_host->logicFrame();
}

void LuaScriptEngine::createLogicState()
{
	// RW 0x739C1F-0x73A0A3: lua_open(0x100), the libraries, then the 41 registrations (_ALERT first: EA's replaces the base library's)
	m_logic.reset(new LuaRuntime(m_sink, "logic"));
	m_logic->setOwner(this);
	m_logic->setFrameSource([this] { return frame(); });
	m_logic->setEnhancedProfile(m_config.enhancedProfile);
	LuaRegisterLogicBindings(m_logic->state());
}

void LuaScriptEngine::createDrawableState()
{
	// RW 0x737654 init(): lua_open(0x100), libraries, the 21 drawable registrations, line hook
	m_drawable.reset(new LuaDrawableState(m_sink, [this] { return frame(); }, [this] { return m_host->audioAvailable(); }));
	m_drawable->runtime().setEnhancedProfile(m_config.enhancedProfile);
}

void LuaScriptEngine::init()
{
	if (!m_drawable)
	{
		createDrawableState();
	}
}

void LuaScriptEngine::reset()
{
	// RW 0x739585: lua_close(logic), the vectors; the drawable state stays. Object registrations vanish with the state
	m_logic.reset();
	m_registry.reset();
	m_depth = 0;
	m_spies.clear();
}

void LuaScriptEngine::reportEventSources()
{
	// spec 4.4: who fires what. The sites are in other lanes' code; nothing in this build calls dispatch for them
	m_sink.report(LUA_STOP_SOURCES, "the engine call sites of the events retail fires are not wired (they belong to the object / AI / body / weapon lanes): "
		"OnCreated (RW 0x628882 GameLogic::sendObjectCreated), OnDestroyed (0x698F06), OnDamaged (0x8C3FA3), OnArrived (0x66803C), OnUnitEntered / OnUnitExited (0x69264D), "
		"OnTeamDestroyed (0x7A2589), OnAflame (0x8901AB), OnQuenched (0x88FF6F), OnBuildingComplete (0x856992), OnSlaughtered (0x883B4A), "
		"OnGenericEvent (0x4B91E2, 0x776F03, 0x7A4B93), OnBuildVariation (0x858701), the ObjectStatus events (0x663E32 -> 0x737954), LuaEventNugget (0x911EDC), "
		"DelayedLuaEventUpdate (0x8AC628) and the emotion LuaEvent (0x8E0ED7); OnTeamEntered, OnTeamExited and DamageIncoming have no dispatch site in this binary. "
		"The entry points are LuaScriptEngine::dispatchInternal / dispatch / updateModelConditionEvents (wired: OnCreated; BeScary from SpecialAbilityUpdate's "
		"SPECIAL_SCREECH trigger RW 0x854968 through GameLogic::dispatchScriptEvent and the ModelCondition events of the AI update RW 0x663E32 through "
		"GameLogic::scriptModelConditionEvents, lane HERO-2)");
}

void LuaScriptEngine::loadLogicScripts(const std::string &scriptsLua, const std::string &scriptEventsXml)
{
	reportEventSources();
	// RW 0x739C10 (state if absent, then the two loads, at 0x73A0C0 / 0x73A0CD)
	if (!m_logic)
	{
		createLogicState();
	}
	m_logic->doBuffer(scriptsLua, kScriptsLua); // RW 0x7358CB: chunk name = the file name, status ignored, stack emptied
	lua_settop(m_logic->state(), 0);
	lua_State *L = m_logic->state();
	m_registry.parse(scriptEventsXml, false, [L](const std::string &fn) {
		lua_getglobal(L, fn.c_str());
		const int type = lua_type(L, -1);
		lua_settop(L, -2);
		return type;
	}, m_sink);
}

bool LuaScriptEngine::startNewGame(LuaFileSource &files, const std::string &mapFolder)
{
	reportEventSources();
	bool ok = true;
	if (!m_logic)
	{
		createLogicState();
	}
	lua_State *L = m_logic->state();
	auto globalType = [L](const std::string &fn) {
		lua_getglobal(L, fn.c_str());
		const int type = lua_type(L, -1);
		lua_settop(L, -2);
		return type;
	};
	std::string text;
	// RW 0x739C10 -> 0x73A0B9
	if (files.read(kScriptsLua, &text))
	{
		m_logic->doBuffer(text, kScriptsLua);
		lua_settop(L, 0);
	}
	else
	{
		m_sink.report(LUA_STOP_LOAD, std::string(kScriptsLua) + " could not be read: retail loads nothing and says nothing; the port reports it");
		ok = false;
	}
	if (files.read(kScriptEventsXml, &text))
	{
		m_registry.parse(text, false, globalType, m_sink);
	}
	else
	{
		m_sink.report(LUA_STOP_LOAD, std::string(kScriptEventsXml) + " could not be read: retail loads nothing and says nothing; the port reports it");
		ok = false;
	}
	// RW 0x73A0D5: the map overlay
	if (!mapFolder.empty())
	{
		m_sink.report(LUA_STOP_LOAD, "map overlay: the folder is given by the caller; retail derives it from the map name through the map cache (RW 0xDE4AD4) before "
			"stripping the file name (spec G5, no retail map has the files)");
		const std::string luaPath = mapFolder + "\\Scripts.lua";    // RW 0xC24EB8
		const std::string xmlPath = mapFolder + "\\ScriptEvents.xml"; // RW 0xC24EA4
		if (files.exists(luaPath) && files.read(luaPath, &text))
		{
			m_logic->doBuffer(text, luaPath.c_str());
			lua_settop(L, 0);
		}
		if (files.exists(xmlPath) && files.read(xmlPath, &text))
		{
			m_registry.parse(text, true, globalType, m_sink);
		}
	}
	return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// objects
// ---------------------------------------------------------------------------------------------------------------------
std::string LuaScriptEngine::objectGlobalName(int id)
{
	char buf[32];
	std::snprintf(buf, sizeof buf, "ObjID#%08x", (unsigned)id); // RW 0xC244BC, builder 0x734C93
	return buf;
}

void LuaScriptEngine::registerObject(const LuaObjectInfo &obj)
{
	// RW 0x735941
	if (!obj.hasAI && !obj.forceLuaRegistration)
	{
		return;
	}
	if (!m_logic)
	{
		return;
	}
	lua_State *L = m_logic->state();
	if (m_drawable)
	{
		lua_settop(m_drawable->runtime().state(), 0); // a harmless stack reset on the other state, as in B1
	}
	const std::string name = objectGlobalName(obj.id);
	lua_getglobal(L, name.c_str());
	// Q4: absolute stack index 1, not the top
	if (lua_type(L, 1) == LUA_TNIL || lua_toobjid(L, 1) != obj.id)
	{
		lua_settop(L, -2);
		lua_newtablewithid(L, obj.id);
		lua_setglobal(L, name.c_str());
	}
	else
	{
		lua_settop(L, -2);
	}
}

void LuaScriptEngine::unregisterObject(const LuaObjectInfo &obj)
{
	// RW 0x735A1D
	if (!m_logic)
	{
		return;
	}
	lua_State *L = m_logic->state();
	if (m_drawable)
	{
		lua_settop(m_drawable->runtime().state(), 0);
	}
	const std::string name = objectGlobalName(obj.id);
	lua_getglobal(L, name.c_str());
	if (lua_type(L, 1) == LUA_TNIL || lua_toobjid(L, 1) != obj.id)
	{
		lua_settop(L, -2);
	}
	else
	{
		lua_settop(L, -2);
		lua_pushnil(L);
		lua_setglobal(L, name.c_str());
	}
}

void LuaScriptEngine::pushObject(lua_State *L, const LuaObjectInfo *obj)
{
	// RW 0x735AE9(this, L, Object*): the global is read in the logic state, the top is the argument state's (the same state)
	lua_State *logic = m_logic->state();
	if (!obj)
	{
		lua_pushnil(logic);
		return;
	}
	const std::string name = objectGlobalName(obj->id);
	lua_getglobal(logic, name.c_str());
	const int top = lua_gettop(L);
	if (lua_type(logic, top) == LUA_TNIL || lua_toobjid(logic, top) != obj->id)
	{
		lua_settop(logic, top);
		lua_pushnil(logic); // quirk Q5: the getglobal value stays, so there are TWO values
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------------------------------------------------
int LuaScriptEngine::sendObjectCreated(GameLogicRandom &rng, LuaScriptEngine *engine, const LuaObjectInfo *obj, const std::function<void()> &bindDrawable)
{
	const int drawableSeed = rng.getValue(1, 999, "GameLogic.cpp", kCreationDrawLine); // RW 0x628892..0x6288A5
	if (bindDrawable)
	{
		bindDrawable();
	}
	if (engine && obj)
	{
		engine->objectEnteredWorld(*obj);
		engine->dispatchInternal(LUAEVENT_OnCreated, *obj);
	}
	return drawableSeed;
}

void LuaScriptEngine::dispatchInternal(LuaInternalEvent event, const LuaObjectInfo &self, const LuaEventArgs &args)
{
	LuaEventRef ref;
	ref.kind = LuaEventRef::INTERNAL;
	ref.key = m_registry.internalKey((int)event);
	ref.index = (int)event;
	dispatch(ref, self, args);
}

void LuaScriptEngine::spyNotify(NameKeyType eventKey, const LuaObjectInfo &self)
{
	// RW 0x6625E8 -> 0x7337FE walks the pairs recorded for `self` and runs 0x73617F for the matching ones (not decoded, spec G11)
	for (const auto &s : m_spies)
	{
		if (s.first == self.id && s.second.eventKey == eventKey)
		{
			m_sink.report(LUA_STOP_SPY, "an ObjectSpy record matched a dispatched event: the re-dispatch RW 0x73617F is not ported (unused by retail data)");
		}
	}
}

void LuaScriptEngine::dispatch(const LuaEventRef &event, const LuaObjectInfo &self, const LuaEventArgs &args)
{
	// RW 0x735F53 (spec 4.3)
	if (!m_logic || m_depth > 10) // RW 0x735F71: `cmp [esi+0xD4], 0xA; jg`: entering at depth 10 is allowed, so 11 handlers can be nested
	{
		return;
	}
	lua_State *L = m_logic->state();
	const int top = lua_gettop(L);
	m_debugStep = nullptr;
	if (!self.hasAI)
	{
		return;
	}
	if (self.dead && event.key != m_registry.onDestroyedKey())
	{
		return; // a dead object receives OnDestroyed only
	}
	if (!self.luaEvents)
	{
		return;
	}
	const LuaEventHandler *handler = self.luaEvents->find(event.key);
	if (!handler || handler->function.empty())
	{
		return;
	}
	lua_getglobal(L, handler->function.c_str());
	if (lua_type(L, -1) != LUA_TFUNCTION)
	{
		// " is not defined." / " is not a lua function." is built and dropped (RW 0x736019)
		lua_settop(L, top);
		return;
	}
	pushObject(L, &self);
	int nargs = 1;
	for (int i = 0; i < 3; ++i)
	{
		const LuaEventArg &a = args.arg[i];
		if (a.kind == LuaEventArg::REAL)
		{
			lua_pushnumber(L, (double)a.real);
		}
		else if (a.kind == LuaEventArg::BOOLEAN)
		{
			lua_pushboolean(L, a.boolean ? 1 : 0);
		}
		else if (a.kind == LuaEventArg::OBJECT)
		{
			LuaObjectInfo other;
			const bool found = a.objectId != 0 && m_host->findObject(a.objectId, &other);
			pushObject(L, found ? &other : nullptr);
		}
		else if (a.kind == LuaEventArg::STRING)
		{
			lua_pushstring(L, a.text.c_str());
		}
		else
		{
			break; // kind 0 ends the list (RW 0x73608D)
		}
		++nargs; // once per argument, however many values pushObject left (quirk Q5: lua_call then takes the wrong function slot)
	}
	if (handler->debugSingleStep)
	{
		m_debugStep = L;
	}
	++m_depth;
	lua_call(L, nargs, 0); // protected; the status is ignored
	--m_depth;
	if (m_debugStep && m_depth == 0)
	{
		m_debugStep = nullptr; // "Stepping out of LUA function - step disabled." (the console is inert)
	}
	spyNotify(event.key, self);
}

void LuaScriptEngine::updateModelConditionEvents(const LuaObjectInfo &self, const ModelConditionFlags &flags, ModelConditionFlags *snapshot, std::uint32_t frame)
{
	// RW 0x663E32: with the logic frame < 2 only the snapshot is taken (object+0x10C, 19 words, into this+0x290)
	if (frame < 2)
	{
		*snapshot = flags;
		return;
	}
	// RW 0x7378DA: for each record (vector order): fire when the flags match it and the snapshot did not
	for (const LuaModelConditionEvent &rec : m_registry.modelConditionEvents())
	{
		if (rec.matches(flags) && !rec.matches(*snapshot))
		{
			LuaEventRef ref;
			ref.kind = LuaEventRef::MODEL_CONDITION;
			ref.key = rec.key;
			dispatch(ref, self, LuaEventArgs());
		}
	}
	*snapshot = flags;
}

// ---------------------------------------------------------------------------------------------------------------------
// the drawable state
// ---------------------------------------------------------------------------------------------------------------------
int LuaScriptEngine::runDrawableScript(const std::string &script, const std::string &chunkName, LuaDrawableContext *ctx, std::string *result)
{
	// RW 0x734D62
	if (!m_drawable)
	{
		createDrawableState();
	}
	return m_drawable->run(script, chunkName, ctx, result);
}
