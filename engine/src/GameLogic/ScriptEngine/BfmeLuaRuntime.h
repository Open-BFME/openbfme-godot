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
		int localPlayerId = -1;
		unsigned frame = 0;
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
		// Sorted entry-script name/source pairs, freshly read for each reload.
		std::function<std::vector<std::pair<std::string, std::string>>()> scripts;
	};
	struct Result
	{
		bool ok = false;
		std::string output;
	};
	explicit BfmeLuaRuntime(Host host);
	~BfmeLuaRuntime();
	Result execute(const std::string &code);
	bool hasPending() const;
	void flush();	   // caller holds the simulation between frames
	void invalidate(); // new match / registry: old proxies expire; globals remain
  private:
	struct Impl;
	std::unique_ptr<Impl> m;
};
