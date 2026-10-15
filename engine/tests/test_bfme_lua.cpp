// OpenBFME. GPL-3.0. Contract tests against docs.secondage.dev API examples.
#include "GameLogic/ScriptEngine/BfmeLuaRuntime.h"
#include "LuaTestUtil.h"
#include "doctest.h"

namespace
{
struct BfmeFixture
{
	BfmeLuaRuntime::Snapshot snapshot;
	std::vector<BfmeLuaRuntime::Mutation> applied;
	int writes = 0;
	std::vector<std::pair<std::string, std::string>> scripts;
	std::unique_ptr<BfmeLuaRuntime> vm;
	BfmeFixture()
	{
		snapshot.ready = true;
		snapshot.localPlayerId = 1;
		snapshot.collections["players"]["0"] = {{"id", 0}, {"name", ""}, {"money", 0}};
		snapshot.collections["players"]["1"] = {{"id", 1}, {"name", "Player_1"}, {"money", 1000}, {"gold", 1000}};
		snapshot.collections["unitTypes"]["GondorSoldier"] = {{"id", "GondorSoldier"}, {"cost", 50}};
		snapshot.collections["unitInstances"]["1234"] = {
			{"id", 1234}, {"playerId", 1}, {"typeId", "GondorSoldier"}, {"position", BfmeLuaValue::Table{{"x", 5}}}};
		BfmeLuaRuntime::Host host;
		host.snapshot = [this] { return snapshot; };
		host.apply = [this](const auto &m) {
			applied.push_back(m);
			if (m.id == "1" && m.field == "money")
				snapshot.collections["players"]["1"]["money"] = m.value;
			return m.field == ":kill" ? std::string("expired object") : std::string();
		};
		host.onMutation = [this] { ++writes; };
		host.scripts = [this] { return scripts; };
		vm = std::make_unique<BfmeLuaRuntime>(std::move(host));
	}
	std::string run(const std::string &code)
	{
		auto r = vm->execute(code);
		CHECK_MESSAGE(r.ok, r.output);
		return r.output;
	}
};
} // namespace

TEST_CASE("bfme Lua: module identity, modern syntax, proxies and collection contracts")
{
	BfmeFixture f;
	CHECK(f.run("return _VERSION, bfme==import('bfme'), bfme==require('bfme')") == "Lua 5.4\ttrue\ttrue\n");
	CHECK(f.run("return #bfme.players, bfme.players.localPlayer==bfme.players[1], bfme.players.neutral.id") == "2\ttrue\t0\n");
	CHECK(f.run("local n=0; for id,p in pairs(bfme.players) do n=n+1 end; return n") == "2\n");
	CHECK(f.run("return tostring(bfme.players[1]), bfme.players[99]") == "Player[1]\tnil\n");
	CHECK(f.run("return #bfme.unitInstances(1), #bfme.unitInstances(2), bfme.unitInstances[1234].ownerPlayer.id") == "1\t0\t1\n");
	CHECK(f.run("return bfme.unitInstances[1234].type==bfme.unitTypes.GondorSoldier") == "true\n");
	CHECK(
		f.run("local p=bfme.unitInstances[1234].position;p.x=90;return bfme.unitInstances[1234].x, bfme.unitInstances[1234].position.x") ==
		"nil\t5\n");
	CHECK(f.run("local s=bfme.players[1]:inspect();s.values.money=999;return bfme.players[1].money") == "1000\n");
	CHECK_FALSE(f.vm->execute("bfme.players[1]={}").ok);
	CHECK_FALSE(f.vm->execute("rawset(bfme.players[1], 'id', 9)").ok);
	CHECK(f.run("return type(bfme.players), type(bfme.players[1])") == "userdata\tuserdata\n");
	CHECK_FALSE(f.vm->execute("import('unknown')").ok);
}

TEST_CASE("bfme Lua: queued money writes, ticket outcomes and input validation")
{
	BfmeFixture f;
	CHECK(f.run("ticket=bfme.players.localPlayer:set('money',10000);return bfme.getMutationStatus(ticket),bfme.players[1].money") ==
		  "pending\t1000\n");
	CHECK(f.applied.empty());
	CHECK(f.writes == 1);
	f.vm->flush();
	CHECK(f.run("return bfme.getMutationStatus(ticket),bfme.players.localPlayer.money") == "applied\t10000\n");
	for (const char *code :
		 {"bfme.players[1].money=-1", "bfme.players[1].money=1.5", "bfme.players[1].money=math.huge", "bfme.players[1].money=0/0",
		  "bfme.players[1].money='100'", "bfme.players[1].id=2", "bfme.players[1].money=2147483648"})
		CHECK_FALSE(f.vm->execute(code).ok);
	CHECK_FALSE(f.vm->hasPending());
	f.run("bfme.players.localPlayer.gold=15000");
	CHECK(f.vm->hasPending());
	f.vm->flush();
	CHECK(f.applied.back().field == "gold");
	f.run("deadTicket=bfme.unitInstances[1234]:kill()");
	f.vm->flush();
	CHECK(f.run("return bfme.getMutationStatus(deadTicket)") == "expired object\n");
	f.run("ExecuteAction('PLAYER_SET_MONEY','Player_1',42)");
	f.vm->flush();
	CHECK(f.run("return bfme.players[1].money") == "42\n");
}

TEST_CASE("bfme Lua: expired handles, pending mutations and result limits")
{
	BfmeFixture f;
	f.run("old=bfme.players[1];ticket=old:set('money',3)");
	f.vm->invalidate();
	CHECK_FALSE(f.vm->execute("return old.money").ok);
	CHECK(f.run("return bfme.getMutationStatus(ticket)") == "expired registry\n");
	CHECK_FALSE(f.vm->hasPending());
	f.run("old=bfme.unitInstances[1234]");
	f.snapshot.collections["unitInstances"].clear();
	CHECK_FALSE(f.vm->execute("return old.id").ok);
	f.run("for i=1,128 do bfme.players[1].money=i end");
	CHECK_FALSE(f.vm->execute("bfme.players[1].money=129").ok);
	f.vm->flush();
	CHECK(f.applied.size() == 128);
	for (int round = 0; round < 9; ++round)
	{
		f.run("for i=1,128 do bfme.players[1].money=i end");
		f.vm->flush();
	}
	CHECK(f.run("return bfme.getMutationStatus(ticket)") == "nil\n");
}

TEST_CASE("bfme Lua: errors and runaway chunks recover; Lua 4 and Lua 5 coexist")
{
	BfmeFixture f;
	CHECK_FALSE(f.vm->execute("error('expected failure')").ok);
	CHECK_FALSE(f.vm->execute("error({})").ok);
	CHECK_FALSE(f.vm->execute("while true do end").ok);
	CHECK(f.run("return 6*7") == "42\n");
	CHECK(f.run("6*7") == "42\n");
	CHECK(f.run("bfme.players.localPlayer.money") == "1000\n");
	CHECK(f.run("return io,os,debug,package.loadlib") == "nil\tnil\tnil\tnil\n");
	LuaReportSink sink;
	LuaRuntime retail(sink, "retail");
	CHECK(luatest::run(retail, "return 6*7").values == "number:42");
	CHECK(f.run("return 7*8") == "56\n");
}

TEST_CASE("bfme Lua: reload rereads scripts, replaces functions and preserves console state")
{
	BfmeFixture f;
	f.run("keep = 99");
	f.scripts = {{"dev/test.lua", "function answer() return 1 end"}};
	CHECK(f.run("console.reload()") == "Reloaded dev/test.lua\n1\n");
	CHECK(f.run("answer(), keep") == "1\t99\n");
	f.scripts[0].second = "function answer() return 2 end";
	f.run("bfme.reloadScripts()");
	CHECK(f.run("answer(), keep") == "2\t99\n");
	f.scripts.push_back({"dev/broken.lua", "function broken("});
	f.scripts[0].second = "function answer() return 3 end";
	CHECK_FALSE(f.vm->execute("console.reload()").ok);
	CHECK(f.run("answer()") == "2\n");
	f.scripts.pop_back();
	f.scripts[0].second = "error('reload failure')";
	CHECK_FALSE(f.vm->execute("console.reload()").ok);
	CHECK(f.run("keep") == "99\n");
}
