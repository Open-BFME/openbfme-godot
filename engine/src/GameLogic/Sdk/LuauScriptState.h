// OpenBFME. GPL-3.0.
//
// LuauScriptState (modding SDK piece 2, lane SDK-2): one sandboxed Luau state running a mod's scripts. OpenBFME feature, not a retail one:
// retail's own scripting (Scripts.lua + ScriptEvents.xml, Lua 4.0.1, GameLogic/ScriptEngine/LuaScriptEngine.h) is untouched; Luau scripts
// live in files retail never reads (<mod>/openbfme/sdk/, GameLogic/Sdk/SdkManifest.h, docs/MODS.md).
//
// Every script has a role (the mod's manifest says which):
//  * Sim: changes game results, so it runs in lockstep and is part of the game's identity. The state is in lockstep mode
//    (Libraries/Luau/README.md: no table keys that hash by address, no weak tables, no addresses shown, the collector paced by allocation
//    alone, the VM's ^ through the deterministic libm), its math functions are DetLibm's (and compiled as plain calls, never as the VM's
//    libm fast paths), math.random is the host's synchronized generator, and there is no clock.
//  * Presentation: drawing, sound, UI only; it may differ between players and gets the ordinary Luau libraries (still sandboxed).
//
// Sandbox (both roles): no os, io, debug, getfenv / setfenv, newproxy, gcinfo, collectgarbage or loadstring; the globals are read-only
// (luaL_sandbox) and the mod's chunks share one environment of their own; `require(name)` loads openbfme/sdk/<role>/<name>.luau of the
// state's own role, and `sdk.read(path)` reads a file of the mod, both only through the reader the host gives (rooted at the mod folder:
// paths are relative, '/'-separated, without '..'); there is no network. Two limits stop a script:
//  * steps: the VM's safepoints (loop back edges, calls, returns) are counted per call; over the budget the call fails with an error.
//    The count depends only on the bytecode executed, so a sim script stops at the same step on every machine;
//  * memory: the bytes the state holds; an allocation over the cap fails (Luau's "not enough memory" error);
//  * output: print may produce 16 KiB a call and 1 MiB in all (it is held outside the VM).
// Running out of steps or output is sticky for the rest of the call: catching the error (pcall, a coroutine) only meets it again.
// A script error is never skipped: the call fails with the message, the state is marked failed and refuses further calls, and the host
// reports it (for a sim script on every peer at the same frame, as the inputs are the same).
//
// lane SDK-2b: a sim state has `sdk.state`, a table the scripts fill in place (`sdk` is read-only, so the table itself cannot be replaced).
// It is the only thing of a script that a save keeps: a loaded game starts the scripts afresh, runs their top level again, then restores
// sdk.state (serializeState / restoreState). It holds plain data: booleans, numbers (not NaN), strings, and tables of them keyed by strings
// or whole numbers (|n| <= 2^53), each table once (a tree), with no metatable and not frozen; at most kStateDepth tables deep and
// kStateBytes serialized. The canonical bytes: a table is 'T', u32 count, then its pairs with the keys in order (numbers ascending, then
// strings bytewise); a value is 'F' / 'R' (false / true), 'N' + the double's 8 bytes (little endian), 'S' + u32 length + bytes, or a table.
// The tables are rebuilt in that key order at the end of every frame (canonicalizeState) and built so by a restore, so `pairs` runs in the
// same order in a loaded game as in the uninterrupted one.
// Since a top level reruns on load, it may not change the game: the host's action gate (setActionGate) refuses math.random there, and the
// host refuses its game actions; a top level that changed sdk.state is an error after it ran (runningTopLevel, the before / after bytes).

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct lua_State;

enum class SdkScriptRole
{
	Sim,
	Presentation
};

struct SdkScriptLimits
{
	std::uint64_t stepsPerCall = 2000000;   // VM safepoints per call into the script (a loading chunk counts as a call)
	std::size_t memoryBytes = 64u << 20;    // the state's total allocation
};

class LuauScriptState
{
public:
	// reads a file of the mod: `path` is already checked (relative, '/'-separated, no '..'); false + error when it is not there
	using Reader = std::function<bool(const std::string &path, std::string &out, std::string &error)>;
	// the synchronized random generator of the game (sim role): 32 random bits
	using Random = std::function<std::uint32_t()>;

	// adds the host's globals (a table of C functions) before the globals are made read-only; the state is not yet in lockstep mode
	using Setup = std::function<void(lua_State *L)>;

	LuauScriptState(SdkScriptRole role, std::string modName, SdkScriptLimits limits, Reader reader, Random random = Random(),
		Setup setup = Setup());
	~LuauScriptState();
	LuauScriptState(const LuauScriptState &) = delete;
	LuauScriptState &operator=(const LuauScriptState &) = delete;

	// compiles `source` and runs its top level in a fresh environment; false + error (also when the state already failed)
	bool load(const std::string &chunkName, const std::string &source, std::string &error);
	// loads openbfme/sdk/<role>/<name>.luau through the reader
	bool loadFile(const std::string &name, std::string &error);
	// calls the global function `name` of the loaded chunks with string / number arguments; results are dropped. A missing function is
	// an error. false + error on any failure.
	bool call(const std::string &name, const std::vector<std::string> &stringArgs, std::string &error);
	bool callNumbers(const std::string &name, const std::vector<double> &args, std::string &error);
	// a call with any arguments: `push` pushes them on the state's thread and returns how many
	bool callWith(const std::string &name, const std::function<int(lua_State *T)> &push, std::string &error);
	// do the loaded chunks define a global function `name`
	bool hasFunction(const std::string &name);

	// lane SDK-2b: sdk.state (sim role) as its canonical bytes; false + error naming the path of what it cannot hold
	static constexpr std::size_t kStateBytes = 64u << 10;
	static constexpr int kStateDepth = 16;
	bool serializeState(std::string &out, std::string &error);
	// fills an empty sdk.state from canonical bytes; false + error (bytes that are not canonical: a damaged save, or the memory cap). The
	// three run inside a protected call: a Lua error (the memory cap) is their error, never the caller's
	bool restoreState(const std::string &bytes, std::string &error);
	// the same parse without a state: are these canonical bytes
	static bool checkStateBytes(const std::string &bytes, std::string &error);
	// rebuilds every table of sdk.state in place (the same tables, so the scripts' references stay) in the canonical key order, as a
	// restore builds them: the running game and a game loaded from its save then iterate sdk.state (pairs / next) in the same order. The
	// host calls it at the end of every frame, after serializeState succeeded (Sol r1 of SDK-2b)
	// false + error when the state ran out of memory doing it (the state is then failed: its tables may be half refilled)
	bool canonicalizeState(std::string &error);
	// a top level is running: a chunk being loaded, or a module being required
	bool runningTopLevel() const { return m_topLevel || !m_loading.empty(); }
	// "" when the scripts may change the game now, else why not (the host's rule; checked by math.random of a sim state)
	using ActionGate = std::function<std::string()>;
	void setActionGate(ActionGate gate) { m_gate = std::move(gate); }
	std::string actionRefusal() const { return m_gate ? m_gate() : std::string(); }

	SdkScriptRole role() const { return m_role; }
	const std::string &modName() const { return m_modName; }
	bool failed() const { return m_failed; }
	const std::string &failure() const { return m_failure; }
	std::uint64_t stepsLastCall() const { return m_stepsLastCall; }
	std::size_t bytesInUse() const { return m_bytes; }
	// what the scripts printed (print), in order
	const std::vector<std::string> &printed() const { return m_printed; }
	lua_State *state() { return m_L; }

	// used by the state's C functions (LuauScriptState.cpp), not by hosts
	std::uint32_t randomBits();
	void notePrinted(const std::string &line);
	bool readModFile(const std::string &path, std::string &out, std::string &error);
	bool enterModule(const std::string &name, std::string &error);
	void leaveModule();
	void *allocate(void *ptr, std::size_t osize, std::size_t nsize);
	void step(lua_State *L);
	bool reservePrint(std::size_t bytes);
	static constexpr std::size_t kPrintPerCall = 16u << 10; // printed bytes a call may make
	static constexpr std::size_t kPrintTotal = 1u << 20;    // printed bytes over the state's life
	static constexpr std::size_t kPrintLineCost = 64;       // what one printed line costs on top of its text (its string object)

	// a path a script may name: relative, '/'-separated, no empty / '.' / '..' parts, no ':' or '\'
	static bool safePath(const std::string &path);
	// the compile options' disabled builtins of a sim script (the math functions whose fast paths would call the platform libm)
	static const char *const *simDisabledBuiltins();

private:
	lua_State *thread();
	bool pushFunction(lua_State *T, const std::string &name, std::string &error);
	bool run(lua_State *T, int nargs, std::string &error);
	void fail(const std::string &message);

	SdkScriptRole m_role;
	std::string m_modName;
	SdkScriptLimits m_limits;
	Reader m_reader;
	Random m_random;
	lua_State *m_L = nullptr;
	std::size_t m_bytes = 0;
	std::uint64_t m_steps = 0;
	std::uint64_t m_stepsLastCall = 0;
	bool m_failed = false;
	std::string m_failure;
	std::vector<std::string> m_printed;
	std::vector<std::string> m_loading; // require's chain, for the cycle error
	std::uint64_t m_rngState = 0;       // randomBits without a host generator
	bool m_budgetExceeded = false;      // sticky for the rest of a call (Sol r1)
	std::size_t m_printedThisCall = 0, m_printedTotal = 0;
	bool m_printOverflow = false;
	bool m_topLevel = false;            // lane SDK-2b: inside load()
	ActionGate m_gate;
};
