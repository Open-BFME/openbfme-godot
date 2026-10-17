// OpenBFME. GPL-3.0. Native, portable subset of the docs.secondage.dev bfme API.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct BfmeLuaValue
{
	using Table = std::map<std::string, BfmeLuaValue>;
	using List = std::vector<BfmeLuaValue>;
	std::variant<std::monostate, bool, double, std::string, Table, List> data;
	BfmeLuaValue() = default;
	BfmeLuaValue(bool v) : data(v)
	{
	}
	BfmeLuaValue(int v) : data(double(v))
	{
	}
	BfmeLuaValue(unsigned v) : data(double(v))
	{
	}
	BfmeLuaValue(double v) : data(v)
	{
	}
	BfmeLuaValue(const char *v) : data(std::string(v))
	{
	}
	BfmeLuaValue(std::string v) : data(std::move(v))
	{
	}
	BfmeLuaValue(Table v) : data(std::move(v))
	{
	}
	BfmeLuaValue(List v) : data(std::move(v))
	{
	}
};

class BfmeLuaRuntime
{
  public:
	using Value = BfmeLuaValue;
	using Records = std::map<std::string, Value::Table>;
	struct Snapshot
	{
		bool ready = false, paused = false;
		bool networkGame = false, replay = false; // lane LUAAI-1: orders and the AI switch are refused in both
		int localPlayerId = -1;
		unsigned frame = 0;
		Value cursorPosition; // nil when no active terrain hit; SAGE x/y/z otherwise
		std::vector<unsigned> selection; // client-selected object IDs, newest first (InGameUI::selected)
		std::map<std::string, Records> collections;
	};
	struct Mutation
	{
		std::string collection, id, field;
		Value value;
		std::vector<Value> args;
	};
	struct Host
	{
		std::function<Snapshot()> snapshot;
		// Empty string = success. Resolve IDs again; never retain an Object* in a queued request.
		std::function<std::string(const Mutation &)> apply;
		std::function<void()> onMutation;
		std::function<unsigned(const Mutation &)> construct;
		// "building", "complete", or a terminal error; resolve the ID every time.
		std::function<std::string(unsigned)> constructionStatus;
		// Sorted entry-script name/source pairs, freshly read for each reload.
		std::function<std::vector<std::pair<std::string, std::string>>()> scripts;
		// Read-only engine questions answered at once (the caller holds the simulation): bfme queries such as building:getTrainableUnits().
		// Throws for an unknown name or bad arguments; never changes game state.
		// The restricted layout persistence helpers also use this bridge for local JSON files.
		std::function<Value(const std::string &name, const std::vector<Value> &args)> query;
		// Output of Lua that runs outside execute() (update callbacks, completion callbacks): print / bfme.console.log text.
		std::function<void(const std::string &text)> output;
		std::function<void()> onTimingChanged; // registration changes while the simulation is held
	};
	struct Result
	{
		bool ok = false;
		std::string output;
	};
	// luaState: a fresh LuaJIT lua_State the caller owns (a lua-gdextension LuaState's, GodotBfmeLua) to run on; null: the runtime
	// makes and closes its own. The runtime opens its libraries and the bfme.* API in it either way.
	explicit BfmeLuaRuntime(Host host, void *luaState = nullptr);
	~BfmeLuaRuntime();
	Result execute(const std::string &code);
	// Evaluate a data-only Lua chunk in an isolated state without libraries or engine access.
	static Value readData(const std::string &source, const std::string &name);
	// queued writes, construction jobs, or an update callback that is due (wall clock): the caller should flush() at its next safe point
	bool hasPending() const;
	void flush();	   // caller holds the simulation between frames; also runs the due update callbacks
	bool hasTimingHook(const std::string &kind) const;
	bool hasTimingHooks() const;
	// Caller owns the simulation on the Lua thread. Context is data, never engine pointers.
	// Throws on a callback error/invalid duration; engine reports it and keeps the native duration.
	int adjustTiming(const std::string &kind, Value::Table context, int nativeFrames);
	void invalidate(); // new match / registry: old proxies expire; globals remain
  private:
	struct Impl;
	std::unique_ptr<Impl> m;
};
