// OpenBFME. GPL-3.0. See GameLogic/Sdk/LuauScriptState.h (lane SDK-2).

#include "GameLogic/Sdk/LuauScriptState.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/DeterministicLibm.h"
#include "GameLogic/Sdk/LuauSimMath.h"

#include "luau/lua.h"
#include "luau/lua_openbfme.h"
#include "luau/luacode.h"
#include "luau/lualib.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <set>

namespace
{

const char *const kSimDisabledBuiltins[] = { "vector.angle", "math.sin", "math.cos", "math.tan", "math.asin", "math.acos", "math.atan", "math.atan2",
	"math.exp", "math.log", "math.log10", "math.pow", "math.sinh", "math.cosh", "math.tanh",
	"setmetatable", nullptr }; // setmetatable: its fast path would skip the weak-table check of lua_setmetatable (Libraries/Luau/README.md)

LuauScriptState *self(lua_State *L)
{
	return static_cast<LuauScriptState *>(lua_callbacks(L)->userdata);
}

// a module of the state's role: openbfme/sdk/<role>/<name>.luau (GameLogic/Sdk/SdkManifest.h)
std::string chunkPath(SdkScriptRole role, const std::string &name)
{
	return std::string(role == SdkScriptRole::Sim ? "openbfme/sdk/sim/" : "openbfme/sdk/presentation/") + name + ".luau";
}

// ---- sim role math: the deterministic libm (DetLibm), never the platform's ----------------------------------------------------------

template <double (*F)(double)>
int detUnary(lua_State *L)
{
	lua_pushnumber(L, F(luaL_checknumber(L, 1)));
	return 1;
}

int detAtan2(lua_State *L)
{
	lua_pushnumber(L, DetLibm::atan2(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
	return 1;
}

int detPow(lua_State *L)
{
	lua_pushnumber(L, DetLibm::pow(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
	return 1;
}

int detLog(lua_State *L)
{
	const double x = luaL_checknumber(L, 1);
	lua_pushnumber(L, lua_isnoneornil(L, 2) ? DetLibm::log(x) : SdkSimMath::logBase(x, luaL_checknumber(L, 2)));
	return 1;
}

int detAtan(lua_State *L)
{
	lua_pushnumber(L, DetLibm::atan(luaL_checknumber(L, 1)));
	return 1;
}

// math.random with the game's synchronized generator: () -> [0, 1); (m) -> [1, m]; (m, n) -> [m, n]
int simRandom(lua_State *L)
{
	LuauScriptState *s = self(L);
	const int n = lua_gettop(L);
	if (n > 2)
		luaL_error(L, "wrong number of arguments");
	const std::string refused = s->actionRefusal(); // lane SDK-2b: a draw changes the game's generator
	if (!refused.empty())
		luaL_error(L, "math.random: %s", refused.c_str());
	const std::uint32_t r = s->randomBits();
	if (n == 0)
	{
		lua_pushnumber(L, SdkSimMath::unitFromBits(r));
		return 1;
	}
	const int lo = n == 2 ? luaL_checkinteger(L, 1) : 1;
	const int hi = n == 2 ? luaL_checkinteger(L, 2) : luaL_checkinteger(L, 1);
	luaL_argcheck(L, lo <= hi, n, "interval is empty");
	lua_pushnumber(L, SdkSimMath::rangeFromBits(r, lo, hi));
	return 1;
}

// vector.angle (lveclib.cpp's vector_angle) on DetLibm::atan2 (Sol r1: the platform atan2 differed between CPU dispatches)
int simVectorAngle(lua_State *L)
{
	const float *a = luaL_checkvector(L, 1);
	const float *b = luaL_checkvector(L, 2);
	const float *axis = luaL_optvector(L, 3, nullptr);
	lua_pushnumber(L, SdkSimMath::vectorAngle(a, b, axis));
	return 1;
}

int simRandomSeed(lua_State *L)
{
	luaL_error(L, "math.randomseed is not available to a sim script: math.random is the game's shared generator");
	return 0;
}

// ---- both roles ---------------------------------------------------------------------------------------------------------------------

int scriptPrint(lua_State *L)
{
	std::string line;
	// Sol r2: every line costs its newline and a fixed overhead (the string object kept for it), also an empty print()
	if (!self(L)->reservePrint(LuauScriptState::kPrintLineCost))
		luaL_error(L, "print: the script printed more than its output budget (%d bytes a call, %d in all)", (int)LuauScriptState::kPrintPerCall,
			(int)LuauScriptState::kPrintTotal);
	for (int i = 1; i <= lua_gettop(L); ++i)
	{
		size_t len = 0;
		const char *s = luaL_tolstring(L, i, &len);
		if (!self(L)->reservePrint(len + 1))
			luaL_error(L, "print: the script printed more than its output budget (%d bytes a call, %d in all)", (int)LuauScriptState::kPrintPerCall,
				(int)LuauScriptState::kPrintTotal);
		if (i > 1)
			line += '\t';
		line.append(s, len);
		lua_pop(L, 1);
	}
	self(L)->notePrinted(line);
	return 0;
}

// sdk.read(path): a file of the mod's own folder, as a string
int sdkRead(lua_State *L)
{
	const std::string path = luaL_checkstring(L, 1);
	if (!LuauScriptState::safePath(path))
		luaL_error(L, "sdk.read: '%s' is not a path inside the mod (relative, '/'-separated, no '..')", path.c_str());
	std::string out, error;
	if (!self(L)->readModFile(path, out, error))
		luaL_error(L, "sdk.read: %s", error.c_str());
	lua_pushlstring(L, out.data(), out.size());
	return 1;
}

// require(name): openbfme/sdk/<role>/<name>.luau of the mod (only the state's own role), run once; its result is cached
int scriptRequire(lua_State *L)
{
	const std::string name = luaL_checkstring(L, 1);
	if (!LuauScriptState::safePath(name))
		luaL_error(L, "require: '%s' is not a module of the mod (relative, '/'-separated, no '..')", name.c_str());
	lua_getfield(L, LUA_REGISTRYINDEX, "openbfme.modules");
	lua_getfield(L, -1, name.c_str());
	if (!lua_isnil(L, -1))
		return 1;
	lua_pop(L, 1);
	std::string source, error;
	if (!self(L)->readModFile(chunkPath(self(L)->role(), name), source, error))
		luaL_error(L, "require '%s': %s", name.c_str(), error.c_str());
	if (!self(L)->enterModule(name, error))
		luaL_error(L, "%s", error.c_str());
	size_t size = 0;
	lua_CompileOptions options = {};
	options.optimizationLevel = 1;
	options.debugLevel = 1;
	if (self(L)->role() == SdkScriptRole::Sim)
		options.disabledBuiltins = LuauScriptState::simDisabledBuiltins();
	std::unique_ptr<char, void (*)(void *)> bytecode(luau_compile(source.data(), source.size(), &options, &size), std::free);
	const std::string chunk = "@" + chunkPath(self(L)->role(), name);
	if (luau_load(L, chunk.c_str(), bytecode.get(), size, 0) != 0)
	{
		self(L)->leaveModule();
		lua_error(L);
	}
	// lane SDK-2b: a module's top level reruns when a save is loaded, so it may not change sdk.state
	std::string before, after, stateError;
	const bool sim = self(L)->role() == SdkScriptRole::Sim;
	const bool hadState = sim && self(L)->serializeState(before, stateError);
	const int status = lua_pcall(L, 0, 1, 0); // Sol r1 of SDK-2b: the module is left also when its top level fails (a caught error)
	self(L)->leaveModule();
	if (status != LUA_OK)
		lua_error(L);
	if (hadState && (!self(L)->serializeState(after, stateError) || after != before))
		luaL_error(L, "require '%s': its top level changed sdk.state (a top level reruns when a save is loaded: change sdk.state from an event)",
			name.c_str());
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushboolean(L, 1);
	}
	lua_pushvalue(L, -1);
	lua_setfield(L, -3, name.c_str());
	return 1;
}

// luau_load under lua_pcall: loading may raise (a lockstep state checks the keys of a chunk's table templates as they are built)
struct LoadRequest
{
	const char *chunk;
	const char *bytecode;
	size_t size;
};

int protectedLoad(lua_State *L)
{
	const LoadRequest *r = static_cast<const LoadRequest *>(lua_tolightuserdata(L, 1));
	lua_settop(L, 0);
	if (luau_load(L, r->chunk, r->bytecode, r->size, 0) != 0)
		lua_error(L);
	return 1;
}

void *scriptAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	LuauScriptState *s = static_cast<LuauScriptState *>(ud);
	return s->allocate(ptr, osize, nsize);
}

void scriptInterrupt(lua_State *L, int gc)
{
	if (gc >= 0)
		return; // a collector step: not script work
	self(L)->step(L);
}

} // namespace

const char *const *LuauScriptState::simDisabledBuiltins()
{
	return kSimDisabledBuiltins;
}

bool LuauScriptState::safePath(const std::string &path)
{
	if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos ||
		path.find('\0') != std::string::npos)
		return false;
	size_t start = 0;
	while (start <= path.size())
	{
		const size_t end = path.find('/', start);
		const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (part.empty() || part == "." || part == "..")
			return false;
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return true;
}

LuauScriptState::LuauScriptState(SdkScriptRole role, std::string modName, SdkScriptLimits limits, Reader reader, Random random, Setup setup)
	: m_role(role), m_modName(std::move(modName)), m_limits(limits), m_reader(std::move(reader)), m_random(std::move(random))
{
	m_L = lua_newstate(scriptAlloc, this);
	if (!m_L)
	{
		fail("cannot create the script state (memory cap " + std::to_string(m_limits.memoryBytes) + " bytes)");
		return;
	}
	lua_callbacks(m_L)->userdata = this;
	lua_callbacks(m_L)->interrupt = scriptInterrupt;

	// the libraries a script gets: no os, debug, integer or class libraries
	const luaL_Reg libs[] = { { "", luaopen_base }, { LUA_COLIBNAME, luaopen_coroutine }, { LUA_TABLIBNAME, luaopen_table },
		{ LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 },
		{ LUA_BITLIBNAME, luaopen_bit32 }, { LUA_BUFFERLIBNAME, luaopen_buffer }, { LUA_VECLIBNAME, luaopen_vector }, { nullptr, nullptr } };
	for (const luaL_Reg *lib = libs; lib->func; ++lib)
	{
		lua_pushcfunction(m_L, lib->func, nullptr);
		lua_pushstring(m_L, lib->name);
		lua_call(m_L, 1, 0);
	}
	for (const char *name : { "getfenv", "setfenv", "newproxy", "gcinfo", "collectgarbage", "loadstring", "load", "dofile", "loadfile" })
	{
		lua_pushnil(m_L);
		lua_setglobal(m_L, name);
	}
	lua_pushcfunction(m_L, scriptPrint, "print");
	lua_setglobal(m_L, "print");
	lua_pushcfunction(m_L, scriptRequire, "require");
	lua_setglobal(m_L, "require");

	lua_newtable(m_L); // sdk
	lua_pushcfunction(m_L, sdkRead, "sdk.read");
	lua_setfield(m_L, -2, "read");
	lua_pushstring(m_L, m_role == SdkScriptRole::Sim ? "sim" : "presentation");
	lua_setfield(m_L, -2, "role");
	// Sol r4: a sim script is not told the manifest's name (the name is not part of the sim fingerprint the peers share); a presentation
	// script is
	if (m_role == SdkScriptRole::Presentation)
	{
		lua_pushstring(m_L, m_modName.c_str());
		lua_setfield(m_L, -2, "mod");
	}
	if (m_role == SdkScriptRole::Sim)
	{
		lua_newtable(m_L); // lane SDK-2b: sdk.state, what a save keeps (sdk itself is made read-only below; the table stays writable)
		lua_setfield(m_L, -2, "state");
	}
	lua_setglobal(m_L, "sdk");

	if (m_role == SdkScriptRole::Sim)
	{
		lua_getglobal(m_L, LUA_MATHLIBNAME);
		const luaL_Reg simMath[] = { { "sin", detUnary<DetLibm::sin> }, { "cos", detUnary<DetLibm::cos> }, { "tan", detUnary<DetLibm::tan> },
			{ "asin", detUnary<DetLibm::asin> }, { "acos", detUnary<DetLibm::acos> }, { "atan", detAtan }, { "atan2", detAtan2 },
			{ "exp", detUnary<DetLibm::exp> }, { "log", detLog }, { "log10", detUnary<DetLibm::log10> }, { "pow", detPow },
			{ "sinh", detUnary<SdkSimMath::sinh> }, { "cosh", detUnary<SdkSimMath::cosh> }, { "tanh", detUnary<SdkSimMath::tanh> }, { "random", simRandom },
			{ "randomseed", simRandomSeed }, { nullptr, nullptr } };
		for (const luaL_Reg *f = simMath; f->func; ++f)
		{
			lua_pushcfunction(m_L, f->func, f->name);
			lua_setfield(m_L, -2, f->name);
		}
		lua_pop(m_L, 1);
		lua_getglobal(m_L, LUA_VECLIBNAME);
		lua_pushcfunction(m_L, simVectorAngle, "angle");
		lua_setfield(m_L, -2, "angle");
		lua_pop(m_L, 1);
	}

	lua_newtable(m_L);
	lua_setfield(m_L, LUA_REGISTRYINDEX, "openbfme.modules");
	if (setup)
		setup(m_L);

	luaL_sandbox(m_L); // the globals and the libraries read-only
	lua_State *T = lua_newthread(m_L); // the mod's environment: its chunks' globals live here
	luaL_sandboxthread(T);
	lua_setfield(m_L, LUA_REGISTRYINDEX, "openbfme.thread");
	// lockstep from here on (the libraries above set metatables and then fill them, which lockstep's frozen metatables would refuse)
	if (m_role == SdkScriptRole::Sim)
		lua_openbfme_setlockstep(m_L, 1);
}

LuauScriptState::~LuauScriptState()
{
	if (m_L)
		lua_close(m_L);
}

std::uint32_t LuauScriptState::randomBits()
{
	if (m_random)
		return m_random();
	// no host generator (tests, tools): a fixed sequence (PCG32, seed 0)
	m_rngState = m_rngState * 6364136223846793005ull + 1442695040888963407ull;
	const std::uint32_t x = std::uint32_t(((m_rngState >> 18u) ^ m_rngState) >> 27u);
	const std::uint32_t rot = std::uint32_t(m_rngState >> 59u);
	return (x >> rot) | (x << ((32u - rot) & 31u));
}

void LuauScriptState::notePrinted(const std::string &line)
{
	m_printed.push_back(line);
}

bool LuauScriptState::reservePrint(std::size_t bytes)
{
	// Sol r1: printed text is held outside the VM's memory cap, so it has budgets of its own: per call and in total
	if (m_printOverflow || m_printedThisCall + bytes > kPrintPerCall || m_printedTotal + bytes > kPrintTotal)
	{
		m_printOverflow = true;
		return false;
	}
	m_printedThisCall += bytes;
	m_printedTotal += bytes;
	return true;
}

bool LuauScriptState::readModFile(const std::string &path, std::string &out, std::string &error)
{
	// Sol r1: a sim script reads nothing of the presentation scripts' folder (it is not part of the fingerprint the peers share)
	if (m_role == SdkScriptRole::Sim)
	{
		// Sol r1 / r2: nor the presentation scripts' files nor the manifest (neither is in the fingerprint; the manifest's sim part is the "sdk sim"
		// line); the path is matched as the mount keys it (ModLooseIndex::key: any case, either separator)
		const std::string key = ModLooseIndex::key(path);
		if (key.rfind("openbfme\\sdk\\presentation\\", 0) == 0 || key == "openbfme\\sdk\\mod.json")
		{
			error = "a sim script cannot read " + path + ": it is not part of the game the players share (the presentation scripts' files and the "
				"manifest)";
			return false;
		}
	}
	if (!m_reader)
	{
		error = "this script has no mod folder to read from";
		return false;
	}
	return m_reader(path, out, error);
}

bool LuauScriptState::enterModule(const std::string &name, std::string &error)
{
	for (const std::string &n : m_loading)
	{
		if (n == name)
		{
			error = "require cycle: ";
			for (const std::string &m : m_loading)
				error += m + " -> ";
			error += name;
			return false;
		}
	}
	m_loading.push_back(name);
	return true;
}

void LuauScriptState::leaveModule()
{
	if (!m_loading.empty())
		m_loading.pop_back();
}

void *LuauScriptState::allocate(void *ptr, size_t osize, size_t nsize)
{
	if (nsize == 0)
	{
		std::free(ptr);
		m_bytes -= osize;
		return nullptr;
	}
	if (nsize > osize && m_bytes - osize + nsize > m_limits.memoryBytes)
		return nullptr; // over the cap: Luau raises its "not enough memory" error
	void *p = std::realloc(ptr, nsize);
	if (p)
		m_bytes = m_bytes - osize + nsize;
	return p;
}

void LuauScriptState::step(lua_State *L)
{
	// Sol r1: the budget is sticky for the rest of the call: a script that catches the error (pcall, xpcall, a coroutine) meets it again at
	// its next safepoint, so it can only unwind to the host
	if (m_budgetExceeded || ++m_steps > m_limits.stepsPerCall)
	{
		m_budgetExceeded = true;
		luaL_error(L, "the script ran longer than its step budget (%llu VM safepoints in one call)", (unsigned long long)m_limits.stepsPerCall);
	}
}

lua_State *LuauScriptState::thread()
{
	lua_getfield(m_L, LUA_REGISTRYINDEX, "openbfme.thread");
	lua_State *T = lua_tothread(m_L, -1);
	lua_pop(m_L, 1);
	return T;
}

void LuauScriptState::fail(const std::string &message)
{
	m_failed = true;
	m_failure = m_modName + ": " + message;
}

bool LuauScriptState::run(lua_State *T, int nargs, std::string &error)
{
	m_steps = 0;
	m_budgetExceeded = false;
	m_printedThisCall = 0;
	int status = lua_pcall(T, nargs, 0, 0);
	m_stepsLastCall = m_steps;
	m_steps = 0;
	if (status == LUA_OK && (m_budgetExceeded || m_printOverflow))
	{
		lua_settop(T, 0);
		lua_pushstring(T, m_budgetExceeded ? "the script ran longer than its step budget" : "the script printed more than its output budget");
		status = LUA_ERRRUN;
	}
	m_budgetExceeded = false;
	if (status != LUA_OK)
	{
		const char *msg = lua_tostring(T, -1);
		fail(msg ? msg : (status == LUA_ERRMEM ? "not enough memory (the script's memory cap)" : "error"));
		lua_settop(T, 0);
		error = m_failure;
		return false;
	}
	return true;
}

bool LuauScriptState::load(const std::string &chunkName, const std::string &source, std::string &error)
{
	if (m_failed || !m_L)
	{
		error = m_failure;
		return false;
	}
	lua_CompileOptions options = {};
	options.optimizationLevel = 1;
	options.debugLevel = 1;
	if (m_role == SdkScriptRole::Sim)
		options.disabledBuiltins = kSimDisabledBuiltins;
	size_t size = 0;
	std::unique_ptr<char, void (*)(void *)> bytecode(luau_compile(source.data(), source.size(), &options, &size), std::free);
	lua_State *T = thread();
	const std::string chunk = "@" + chunkName;
	LoadRequest request{ chunk.c_str(), bytecode.get(), size };
	lua_pushcfunction(T, protectedLoad, "load");
	lua_pushlightuserdata(T, &request);
	if (lua_pcall(T, 1, 1, 0) != LUA_OK)
	{
		const char *msg = lua_tostring(T, -1);
		fail(msg ? msg : "cannot load " + chunkName);
		lua_settop(T, 0);
		error = m_failure;
		return false;
	}
	// lane SDK-2b: the top level reruns when a save is loaded: it may not change the game (the action gate) or sdk.state (compared below)
	std::string before, after;
	if (m_role == SdkScriptRole::Sim && !serializeState(before, error))
	{
		fail(error);
		error = m_failure;
		return false;
	}
	m_topLevel = true;
	const bool ok = run(T, 0, error);
	m_topLevel = false;
	if (ok && m_role == SdkScriptRole::Sim && (!serializeState(after, error) || after != before))
	{
		fail(error.empty() || after != before ? chunkName + ": its top level changed sdk.state (a top level reruns when a save is loaded: change "
			"sdk.state from an event, such as onGameStart or onGameLoaded)" : error);
		error = m_failure;
		return false;
	}
	return ok;
}

bool LuauScriptState::loadFile(const std::string &name, std::string &error)
{
	if (!safePath(name))
	{
		error = m_modName + ": '" + name + "' is not a script of the mod";
		return false;
	}
	std::string source;
	if (!readModFile(chunkPath(m_role, name), source, error))
	{
		error = m_modName + ": " + error;
		return false;
	}
	return load(chunkPath(m_role, name), source, error);
}

bool LuauScriptState::pushFunction(lua_State *T, const std::string &name, std::string &error)
{
	if (m_failed || !m_L)
	{
		error = m_failure;
		return false;
	}
	lua_getglobal(T, name.c_str());
	if (!lua_isfunction(T, -1))
	{
		lua_settop(T, 0);
		error = m_modName + ": the scripts define no function " + name;
		return false;
	}
	return true;
}

bool LuauScriptState::call(const std::string &name, const std::vector<std::string> &stringArgs, std::string &error)
{
	lua_State *T = m_L ? thread() : nullptr;
	if (!T || !pushFunction(T, name, error))
	{
		if (!T)
			error = m_failure;
		return false;
	}
	for (const std::string &a : stringArgs)
		lua_pushlstring(T, a.data(), a.size());
	return run(T, (int)stringArgs.size(), error);
}

bool LuauScriptState::callNumbers(const std::string &name, const std::vector<double> &args, std::string &error)
{
	lua_State *T = m_L ? thread() : nullptr;
	if (!T || !pushFunction(T, name, error))
	{
		if (!T)
			error = m_failure;
		return false;
	}
	for (double a : args)
		lua_pushnumber(T, a);
	return run(T, (int)args.size(), error);
}

bool LuauScriptState::callWith(const std::string &name, const std::function<int(lua_State *T)> &push, std::string &error)
{
	lua_State *T = m_L ? thread() : nullptr;
	if (!T || !pushFunction(T, name, error))
	{
		if (!T)
			error = m_failure;
		return false;
	}
	return run(T, push(T), error);
}

bool LuauScriptState::hasFunction(const std::string &name)
{
	if (m_failed || !m_L)
		return false;
	lua_State *T = thread();
	lua_getglobal(T, name.c_str());
	const bool is = lua_isfunction(T, -1);
	lua_pop(T, 1);
	return is;
}

// ---- lane SDK-2b: sdk.state's canonical bytes ----------------------------------------------------------------------------------------------

namespace
{

struct StateKey
{
	bool number;
	double n;
	std::string s;
	bool operator<(const StateKey &o) const
	{
		if (number != o.number)
			return number; // numbers first
		return number ? n < o.n : s < o.s;
	}
};

void putU32(std::string &out, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out += (char)((v >> (8 * i)) & 0xFF);
}

void putNumber(std::string &out, double d)
{
	std::uint64_t bits;
	std::memcpy(&bits, &d, 8);
	out += 'N';
	for (int i = 0; i < 8; ++i)
		out += (char)((bits >> (8 * i)) & 0xFF);
}

void putString(std::string &out, const std::string &v)
{
	out += 'S';
	putU32(out, (std::uint32_t)v.size());
	out += v;
}

std::string keyPath(const std::string &path, const StateKey &k)
{
	if (!k.number)
		return path + "." + k.s;
	char buf[40];
	std::snprintf(buf, sizeof buf, "[%.17g]", k.n);
	return path + buf;
}

// the value at the top of L's stack
bool serializeValue(lua_State *L, const std::string &path, int depth, std::set<const void *> &seen, std::string &out, std::string &error)
{
	if (out.size() > LuauScriptState::kStateBytes)
	{
		error = "sdk.state is over " + std::to_string(LuauScriptState::kStateBytes / 1024) + " KB serialized";
		return false;
	}
	switch (lua_type(L, -1))
	{
	case LUA_TBOOLEAN:
		out += lua_toboolean(L, -1) ? 'R' : 'F';
		return true;
	case LUA_TNUMBER:
	{
		const double d = lua_tonumber(L, -1);
		if (std::isnan(d))
		{
			error = path + " is NaN (sdk.state holds numbers that are not NaN)";
			return false;
		}
		putNumber(out, d);
		return true;
	}
	case LUA_TSTRING:
	{
		size_t len = 0;
		const char *p = lua_tolstring(L, -1, &len);
		putString(out, std::string(p, len));
		return true;
	}
	case LUA_TTABLE:
		break;
	default:
		error = path + " is a " + std::string(lua_typename(L, lua_type(L, -1))) +
			" (sdk.state holds booleans, numbers, strings and tables of them)";
		return false;
	}
	if (depth > LuauScriptState::kStateDepth)
	{
		error = path + " is more than " + std::to_string(LuauScriptState::kStateDepth) + " tables deep";
		return false;
	}
	if (!seen.insert(lua_topointer(L, -1)).second)
	{
		error = path + " is a table sdk.state already holds elsewhere (it holds each table once)";
		return false;
	}
	if (lua_getmetatable(L, -1))
	{
		lua_pop(L, 1);
		error = path + " has a metatable (a save keeps plain tables only)";
		return false;
	}
	if (lua_getreadonly(L, -1))
	{
		error = path + " is frozen (a save keeps plain tables only)";
		return false;
	}
	if (!lua_checkstack(L, 4))
	{
		error = path + ": no stack left";
		return false;
	}
	const int t = lua_gettop(L);
	std::vector<StateKey> keys;
	lua_pushnil(L);
	while (lua_next(L, t))
	{
		StateKey k{ false, 0.0, std::string() };
		if (lua_type(L, -2) == LUA_TNUMBER)
		{
			k.number = true;
			k.n = lua_tonumber(L, -2);
			if (k.n != DetLibm::floor(k.n) || DetLibm::fabs(k.n) > 9007199254740992.0)
			{
				lua_pop(L, 2);
				error = keyPath(path, k) + ": a key that is not a whole number (sdk.state's keys are strings or whole numbers)";
				return false;
			}
		}
		else if (lua_type(L, -2) == LUA_TSTRING)
		{
			size_t len = 0;
			const char *p = lua_tolstring(L, -2, &len);
			k.s.assign(p, len);
		}
		else
		{
			const std::string type = lua_typename(L, lua_type(L, -2));
			lua_pop(L, 2);
			error = path + " has a " + type + " key (sdk.state's keys are strings or whole numbers)";
			return false;
		}
		keys.push_back(std::move(k));
		lua_pop(L, 1);
	}
	std::sort(keys.begin(), keys.end());
	out += 'T';
	putU32(out, (std::uint32_t)keys.size());
	for (const StateKey &k : keys)
	{
		if (k.number)
		{
			putNumber(out, k.n);
			lua_pushnumber(L, k.n);
		}
		else
		{
			putString(out, k.s);
			lua_pushlstring(L, k.s.data(), k.s.size());
		}
		lua_rawget(L, t);
		const bool ok = serializeValue(L, keyPath(path, k), depth + 1, seen, out, error);
		lua_pop(L, 1);
		if (!ok)
			return false;
	}
	return true;
}

// parses canonical bytes; with L, pushes each value as it is read (a table's pairs set raw, in order)
struct StateReader
{
	const std::string &b;
	size_t at = 0;
	lua_State *L = nullptr;
	std::string error;

	bool fail(const std::string &why)
	{
		if (error.empty())
			error = "sdk.state bytes at " + std::to_string(at) + ": " + why;
		return false;
	}
	bool u32(std::uint32_t &v)
	{
		if (b.size() - at < 4)
			return fail("cut short");
		v = 0;
		for (int i = 0; i < 4; ++i)
			v |= (std::uint32_t)(unsigned char)b[at + i] << (8 * i);
		at += 4;
		return true;
	}
	bool number(double &d)
	{
		if (b.size() - at < 8)
			return fail("cut short");
		std::uint64_t bits = 0;
		for (int i = 0; i < 8; ++i)
			bits |= (std::uint64_t)(unsigned char)b[at + i] << (8 * i);
		at += 8;
		std::memcpy(&d, &bits, 8);
		return std::isnan(d) ? fail("a NaN") : true;
	}
	bool string(std::string &s)
	{
		std::uint32_t n = 0;
		if (!u32(n))
			return false;
		if (b.size() - at < n)
			return fail("cut short");
		s = b.substr(at, n);
		at += n;
		return true;
	}
	bool value(int depth)
	{
		if (at >= b.size())
			return fail("cut short");
		const char tag = b[at++];
		if (tag == 'F' || tag == 'R')
		{
			if (L)
				lua_pushboolean(L, tag == 'R');
			return true;
		}
		if (tag == 'N')
		{
			double d;
			if (!number(d))
				return false;
			if (L)
				lua_pushnumber(L, d);
			return true;
		}
		if (tag == 'S')
		{
			std::string s;
			if (!string(s))
				return false;
			if (L)
				lua_pushlstring(L, s.data(), s.size());
			return true;
		}
		if (tag == 'T')
			return table(depth, false);
		return fail("an unknown tag");
	}
	// `into`: fill the table at L's top instead of pushing a new one
	bool table(int depth, bool into)
	{
		if (depth > LuauScriptState::kStateDepth)
			return fail("more than " + std::to_string(LuauScriptState::kStateDepth) + " tables deep");
		std::uint32_t n = 0;
		if (!u32(n))
			return false;
		if (n > b.size() - at)
			return fail("more pairs than bytes");
		if (L)
		{
			if (!lua_checkstack(L, 4))
				return fail("no stack left");
			if (!into)
				lua_createtable(L, 0, 0);
		}
		bool havePrev = false;
		StateKey prev{ false, 0.0, std::string() };
		for (std::uint32_t i = 0; i < n; ++i)
		{
			if (at >= b.size())
				return fail("cut short");
			StateKey k{ false, 0.0, std::string() };
			const char tag = b[at++];
			if (tag == 'N')
			{
				k.number = true;
				if (!number(k.n))
					return false;
				if (k.n != DetLibm::floor(k.n) || DetLibm::fabs(k.n) > 9007199254740992.0)
					return fail("a key that is not a whole number");
			}
			else if (tag == 'S')
			{
				if (!string(k.s))
					return false;
			}
			else
				return fail("a key that is not a number or a string");
			if (havePrev && !(prev < k))
				return fail("keys out of order");
			prev = k;
			havePrev = true;
			if (L)
			{
				if (k.number)
					lua_pushnumber(L, k.n);
				else
					lua_pushlstring(L, k.s.data(), k.s.size());
			}
			if (!value(depth + 1))
				return false;
			if (L)
				lua_rawset(L, -3);
		}
		return true;
	}
	bool root()
	{
		if (at >= b.size() || b[at] != 'T')
			return fail("not a table");
		++at;
		if (!table(1, L != nullptr))
			return false;
		return at == b.size() ? true : fail("bytes after the end");
	}
};

} // namespace

namespace
{
// runs `fn` on the state inside lua_pcall (Sol r2 of SDK-2b): a Lua error it raises (the memory cap) comes back as the error text and never
// reaches the game's frame
struct ProtectedWork
{
	const std::function<void(lua_State *)> *fn;
};

int protectedWork(lua_State *L)
{
	const ProtectedWork *w = static_cast<const ProtectedWork *>(lua_tolightuserdata(L, 1));
	lua_remove(L, 1);
	(*w->fn)(L);
	return 0;
}

bool runProtected(lua_State *L, const std::function<void(lua_State *)> &fn, std::string &error)
{
	const int base = lua_gettop(L);
	ProtectedWork w{ &fn };
	const int status = lua_cpcall(L, protectedWork, &w); // Sol r3: the closure is made inside the protection too
	if (status != LUA_OK)
	{
		const char *msg = lua_gettop(L) > base ? lua_tostring(L, -1) : nullptr;
		error = status == LUA_ERRMEM ? "not enough memory (the script's memory cap)" : (msg ? msg : "error");
	}
	lua_settop(L, base);
	return status == LUA_OK;
}
} // namespace

bool LuauScriptState::serializeState(std::string &out, std::string &error)
{
	out.clear();
	if (m_role != SdkScriptRole::Sim || !m_L)
	{
		error = m_modName + ": a presentation script has no sdk.state";
		return false;
	}
	bool ok = false;
	const std::function<void(lua_State *)> work = [&](lua_State *L) {
		lua_getglobal(L, "sdk");
		lua_getfield(L, -1, "state");
		std::set<const void *> seen;
		ok = serializeValue(L, "sdk.state", 1, seen, out, error) &&
			(out.size() <= kStateBytes || (error = "sdk.state is over " + std::to_string(kStateBytes / 1024) + " KB serialized", false));
	};
	if (!runProtected(m_L, work, error))
		ok = false;
	if (!ok)
		out.clear();
	return ok;
}

bool LuauScriptState::checkStateBytes(const std::string &bytes, std::string &error)
{
	if (bytes.size() > kStateBytes)
	{
		error = "sdk.state is over " + std::to_string(kStateBytes / 1024) + " KB serialized";
		return false;
	}
	StateReader r{ bytes };
	if (!r.root())
	{
		error = r.error;
		return false;
	}
	return true;
}

bool LuauScriptState::restoreState(const std::string &bytes, std::string &error)
{
	if (!checkStateBytes(bytes, error))
		return false;
	if (m_role != SdkScriptRole::Sim || !m_L || m_failed)
	{
		error = m_failed ? m_failure : m_modName + ": a presentation script has no sdk.state";
		return false;
	}
	bool ok = false;
	const std::function<void(lua_State *)> work = [&](lua_State *L) {
		lua_getglobal(L, "sdk");
		lua_getfield(L, -1, "state");
		lua_pushnil(L);
		const bool empty = lua_next(L, -2) == 0;
		if (!empty)
		{
			error = m_modName + ": sdk.state is not empty before its restore";
			return;
		}
		lua_openbfme_resettable(L, -1); // a top level that added and removed a key grew it: back to a fresh table's layout
		StateReader r{ bytes };
		r.L = L;
		ok = r.root();
		if (!ok)
			error = r.error;
	};
	if (!runProtected(m_L, work, error))
		ok = false;
	return ok;
}

namespace
{
// the table at the top of L's stack and every table in it (sdk.state passed serializeState: plain data, a tree), refilled in key order
void canonicalizeTable(lua_State *L)
{
	const int t = lua_gettop(L);
	lua_checkstack(L, 8);
	std::vector<StateKey> keys;
	lua_pushnil(L);
	while (lua_next(L, t))
	{
		StateKey k{ lua_type(L, -2) == LUA_TNUMBER, 0.0, std::string() };
		if (k.number)
			k.n = lua_tonumber(L, -2);
		else
		{
			size_t len = 0;
			const char *p = lua_tolstring(L, -2, &len);
			k.s.assign(p, len);
		}
		keys.push_back(std::move(k));
		lua_pop(L, 1);
	}
	std::sort(keys.begin(), keys.end());
	// the values, in key order, kept on a holding table's array part while the table is emptied
	lua_createtable(L, (int)keys.size(), 0);
	const int hold = lua_gettop(L);
	for (size_t i = 0; i < keys.size(); ++i)
	{
		if (keys[i].number)
			lua_pushnumber(L, keys[i].n);
		else
			lua_pushlstring(L, keys[i].s.data(), keys[i].s.size());
		lua_rawget(L, t);
		lua_rawseti(L, hold, (int)i + 1);
	}
	lua_openbfme_resettable(L, t);
	for (size_t i = 0; i < keys.size(); ++i)
	{
		if (keys[i].number)
			lua_pushnumber(L, keys[i].n);
		else
			lua_pushlstring(L, keys[i].s.data(), keys[i].s.size());
		lua_rawgeti(L, hold, (int)i + 1);
		if (lua_type(L, -1) == LUA_TTABLE)
		{
			lua_pushvalue(L, -1);
			canonicalizeTable(L);
			lua_pop(L, 1);
		}
		lua_rawset(L, t);
	}
	lua_settop(L, t);
}
} // namespace

bool LuauScriptState::canonicalizeState(std::string &error)
{
	if (m_role != SdkScriptRole::Sim || !m_L || m_failed)
	{
		error = m_failed ? m_failure : m_modName + ": a presentation script has no sdk.state";
		return false;
	}
	const std::function<void(lua_State *)> work = [](lua_State *L) {
		lua_getglobal(L, "sdk");
		lua_getfield(L, -1, "state");
		canonicalizeTable(L);
	};
	if (runProtected(m_L, work, error))
		return true;
	error = "sdk.state: " + error;
	fail(error); // a table may be half refilled: the state is not used again
	return false;
}
