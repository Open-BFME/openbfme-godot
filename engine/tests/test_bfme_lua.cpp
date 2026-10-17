// OpenBFME. GPL-3.0. Contract tests against docs.secondage.dev API examples.
#include "GameLogic/ScriptEngine/BfmeLuaRuntime.h"
#include "LuaTestUtil.h"
#include "doctest.h"

#include <chrono>
#include <map>
#include <stdexcept>
#include <thread>

namespace
{
struct BfmeFixture
{
	BfmeLuaRuntime::Snapshot snapshot;
	std::vector<BfmeLuaRuntime::Mutation> applied;
	int writes = 0;
	std::string constructionState = "building";
	std::vector<std::pair<std::string, std::string>> scripts;
	std::vector<std::pair<std::string, std::vector<BfmeLuaValue>>> queries;
	std::map<std::string, BfmeLuaValue> answers;
	std::string asyncOutput;
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
		host.construct = [this](const auto &m) -> unsigned {
			applied.push_back(m);
			if (std::get<double>(m.args[0].data) == -999) throw std::runtime_error("illegal construction site");
			snapshot.collections["buildingInstances"]["777"] = {{"id", 777}, {"playerId", 1}, {"underConstruction", true}};
			return 777;
		};
		host.constructionStatus = [this](unsigned id) { CHECK(id == 777); return constructionState; };
		snapshot.collections["buildingTypes"]["TestBuilding"] = {{"id", "TestBuilding"}};
		host.onMutation = [this] { ++writes; };
		host.scripts = [this] { return scripts; };
		host.query = [this](const std::string &name, const std::vector<BfmeLuaValue> &args) {
			queries.emplace_back(name, args);
			auto it = answers.find(name);
			if (it == answers.end()) throw std::runtime_error("unknown bfme query: " + name);
			return it->second;
		};
		host.output = [this](const std::string &text) { asyncOutput += text; };
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
	// lane LUAGD-1: LuaJIT (Lua 5.1 + its 5.2 extensions), the sandbox unchanged: no jit / ffi / io / os / debug, no native modules
	CHECK(f.run("return _VERSION, bfme==import('bfme'), bfme==require('bfme')") == "Lua 5.1\ttrue\ttrue\n");
	CHECK(f.run("return jit, io, os, debug, package.loadlib, package.cpath, #package.loaders, (pcall(require, 'ffi'))") ==
		  "nil\tnil\tnil\tnil\tnil\t\t2\tfalse\n");
	CHECK(f.run("return bit.band(12, 10), goto_ok") == "8\tnil\n");
	CHECK(f.run("return #bfme.players, bfme.players.localPlayer==bfme.players[1], bfme.players.neutral.id") == "2\ttrue\t0\n");
	CHECK(f.run("local n=0; for id,p in pairs(bfme.players) do n=n+1 end; return n") == "2\n");
	CHECK(f.run("return tostring(bfme.players[1]), bfme.players[99]") == "Player[1]\tnil\n");
	CHECK(f.run("return #bfme.players[1]:units(), #bfme.players.neutral:units(), bfme.unitInstances[1234].ownerPlayer.id") == "1\t0\t1\n");
	CHECK(f.run("return bfme.unitInstances[1234].type==bfme.unitTypes.GondorSoldier") == "true\n");
	CHECK(
		f.run("local p=bfme.unitInstances[1234].position;p.x=90;return bfme.unitInstances[1234].x, bfme.unitInstances[1234].position.x") ==
		"nil\t5\n");
	CHECK(f.run("local s=bfme.players[1]:inspect();s.values.money=999;return bfme.players[1].money") == "1000\n");
	CHECK_FALSE(f.vm->execute("bfme.players[1]={}").ok);
	CHECK_FALSE(f.vm->execute("rawset(bfme.players[1], 'id', 9)").ok);
	CHECK(f.run("return type(bfme.players), type(bfme.players[1])") == "userdata\tuserdata\n");
	CHECK_FALSE(f.vm->execute("import('unknown')").ok);
	CHECK_FALSE(f.vm->execute("bfme.match={}").ok);
	CHECK(f.run("return setGameSpeed,beginConstruction,getCursorPosition,ExecuteAction,GetFrame,console") == "nil\tnil\tnil\tnil\tnil\tnil\n");
}

TEST_CASE("bfme Lua: queued money writes, ticket outcomes and input validation")
{
	BfmeFixture f;
	CHECK(f.run("ticket=bfme.players.localPlayer:set('money',10000);return bfme.mutations.getStatus(ticket),bfme.players[1].money") ==
		  "pending\t1000\n");
	CHECK(f.applied.empty());
	CHECK(f.writes == 1);
	f.vm->flush();
	CHECK(f.run("return bfme.mutations.getStatus(ticket),bfme.players.localPlayer.money") == "applied\t10000\n");
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
	CHECK(f.run("return bfme.mutations.getStatus(deadTicket)") == "expired object\n");
	f.run("bfme.players[1]:setMoney(42)");
	f.vm->flush();
	CHECK(f.run("return bfme.players[1].money") == "42\n");
	f.run("bfme.players[1]:addMoney(-2)");
	f.vm->flush();
	CHECK(f.run("return bfme.players[1].money") == "40\n");
	CHECK_FALSE(f.vm->execute("bfme.players[1]:addMoney(-41)").ok);
	CHECK_FALSE(f.vm->execute("bfme.players[1]:setFogOfWar(1)").ok);
	CHECK_FALSE(f.vm->execute("bfme.players[1].revealMap=true").ok);
	f.run("bfme.players[1]:revealMap(); bfme.players[1]:setFogOfWar(false)");
	f.vm->flush();
	CHECK(f.applied[f.applied.size() - 2].field == "revealMap");
	CHECK(f.applied.back().field == "fogOfWar");
	CHECK(std::get<bool>(f.applied.back().value.data) == false);
}

TEST_CASE("bfme Lua: expired handles, pending mutations and result limits")
{
	BfmeFixture f;
	f.run("old=bfme.players[1];ticket=old:set('money',3)");
	f.vm->invalidate();
	CHECK_FALSE(f.vm->execute("return old.money").ok);
	CHECK(f.run("return bfme.mutations.getStatus(ticket)") == "expired registry\n");
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
	CHECK(f.run("return bfme.mutations.getStatus(ticket)") == "nil\n");
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
	CHECK(f.run("bfme.console.reload()") == "Reloaded dev/test.lua\n1\n");
	CHECK(f.run("answer(), keep") == "1\t99\n");
	f.scripts[0].second = "function answer() return 2 end";
	f.run("bfme.console:reload()");
	CHECK(f.run("answer(), keep") == "2\t99\n");
	f.scripts.push_back({"dev/broken.lua", "function broken("});
	f.scripts[0].second = "function answer() return 3 end";
	CHECK_FALSE(f.vm->execute("bfme.console.reload()").ok);
	CHECK(f.run("answer()") == "2\n");
	f.scripts.pop_back();
	f.scripts[0].second = "error('reload failure')";
	CHECK_FALSE(f.vm->execute("bfme.console.reload()").ok);
	CHECK(f.run("keep") == "99\n");
}

TEST_CASE("bfme Lua: game speed validates multipliers before queuing")
{
	BfmeFixture f;
	for (const auto &value : {"0", "50", "100", "12.5"})
	{
		CHECK(f.vm->execute(std::string("return bfme.match.setGameSpeed(") + value + ")").ok);
	}
	CHECK(f.vm->execute("return bfme.match:setGameSpeed(100)").ok);
	CHECK(f.applied.empty());
	for (const auto &value : {"-1", "101", "0/0", "math.huge", "'50'", "nil"})
		CHECK_FALSE(f.vm->execute(std::string("bfme.match.setGameSpeed(") + value + ")").ok);
	f.vm->flush();
	REQUIRE(f.applied.size() == 5);
	CHECK(f.writes == 5);
	CHECK(f.applied[0].collection == "match");
	CHECK(f.applied[0].field == ":setGameSpeed");
	CHECK(std::get<double>(f.applied[0].value.data) == 0);
	CHECK(std::get<double>(f.applied[3].value.data) == 12.5);
}

TEST_CASE("bfme Lua: player construction validates, queues, returns building and completes callback once")
{
	BfmeFixture f;
	CHECK(f.run("ticket=bfme.players.localPlayer:construct('TestBuilding',{x=10,y=20},2,function(b) calls=(calls or 0)+1; finished=b.id; b.ownerPlayer.money=4321 end); return bfme.mutations.getStatus(ticket)") == "pending\n");
	CHECK(f.applied.empty());
	f.vm->flush();
	REQUIRE(f.applied.size() == 1);
	CHECK(f.applied[0].field == ":construct");
	CHECK(std::get<double>(f.applied[0].args[2].data) == 0);
	CHECK(std::get<double>(f.applied[0].args[3].data) == 1);
	CHECK(std::get<double>(f.applied[0].args[4].data) == 2);
	CHECK(f.run("return bfme.mutations.getBuilding(ticket)==bfme.buildingInstances[777],bfme.mutations.getStatus(ticket),calls") == "true\tbuilding\tnil\n");
	CHECK(f.vm->hasPending());
	f.vm->flush();
	CHECK(f.run("return calls") == "nil\n");
	f.constructionState="complete";
	f.vm->flush();
	CHECK(f.run("return calls,finished,bfme.mutations.getStatus(ticket)") == "1\t777\tcomplete\n");
	f.vm->flush();
	CHECK(f.run("return calls,bfme.players.localPlayer.money") == "1\t4321\n");
	CHECK_FALSE(f.vm->hasPending());
}

TEST_CASE("bfme Lua: construction rejection, unit builders, cancellation, callback errors and registry invalidation")
{
	BfmeFixture f;
	for (const char *code : {
		"bfme.players[1]:construct('missing',{x=1,y=2})",
		"bfme.players[1]:construct(bfme.unitTypes.GondorSoldier,{x=1,y=2})",
		"bfme.players[1]:construct('TestBuilding',{x=0/0,y=2})",
		"bfme.players[1]:construct('TestBuilding',{x=1,y=2},0)",
		"bfme.players[1]:construct('TestBuilding',{x=1,y=2},math.huge)",
		"bfme.players[1]:construct('TestBuilding',{x=1,y=2},1,123)",
		"bfme.players[1]:construct('TestBuilding')"})
		CHECK_FALSE(f.vm->execute(code).ok);
	CHECK_FALSE(f.vm->hasPending());
	CHECK(f.run("ticket=bfme.players[1]:construct(bfme.buildingTypes.TestBuilding,{x=-999,y=0});return ticket") == "1\n");
	f.vm->flush();
	CHECK(f.run("return bfme.mutations.getStatus(ticket),bfme.mutations.getBuilding(ticket)") == "illegal construction site\tnil\n");
	f.run("ticket=bfme.unitInstances[1234]:construct('TestBuilding',{x=1,y=2},0.5,function() calls=1 end)");
	f.vm->flush();
	CHECK(std::get<double>(f.applied.back().args[2].data) == 1234);
	CHECK(std::get<double>(f.applied.back().args[3].data) == 1); // the builder's owner
	f.constructionState="construction cancelled or building destroyed";
	f.vm->flush();
	CHECK(f.run("return calls,bfme.mutations.getStatus(ticket)") == "nil\tconstruction cancelled or building destroyed\n");
	f.constructionState="complete";
	f.run("ticket=bfme.players[1]:construct('TestBuilding',{x=1,y=2},1,function() error('callback test') end)");
	f.vm->flush();
	CHECK(f.run("return bfme.mutations.getStatus(ticket):find('completion callback failed',1,true)~=nil") == "true\n");
	f.constructionState="building";
	f.run("ticket=bfme.players[1]:construct('TestBuilding',{x=1,y=2},1,function() calls=1 end)");
	f.vm->flush();
	f.vm->invalidate();
	CHECK_FALSE(f.vm->hasPending());
	CHECK(f.run("return calls,bfme.mutations.getStatus(ticket),bfme.mutations.getBuilding(ticket)") == "nil\texpired registry\tnil\n");
}

TEST_CASE("bfme Lua: cursor position is a detached terrain coordinate or nil")
{
	BfmeFixture f;
	CHECK(f.run("return bfme.input.getCursorPosition()") == "nil\n");
	f.snapshot.cursorPosition = BfmeLuaValue::Table{{"x", 120.5}, {"y", 450.25}, {"z", 32}};
	CHECK(f.run("local p=bfme.input.getCursorPosition(); return p.x,p.y,p.z") == "120.5\t450.25\t32\n");
	CHECK(f.run("local p=bfme.input:getCursorPosition();p.x=999;return bfme.input.getCursorPosition().x") == "120.5\n");
	CHECK(f.applied.empty());
	CHECK(f.writes == 0);
	f.snapshot.cursorPosition = {};
	CHECK(f.run("return bfme.input.getCursorPosition()") == "nil\n");
}

TEST_CASE("bfme Lua: building:trainUnit accepts unit handles or names with an optional count")
{
	BfmeFixture f;
	f.snapshot.collections["buildingInstances"]["99"] = {{"id", 99}, {"playerId", 1}};
	CHECK(f.run("ticket=bfme.buildingInstances[99]:trainUnit(bfme.unitTypes.GondorSoldier,3);return bfme.mutations.getStatus(ticket)") == "pending\n");
	CHECK(f.applied.empty());
	f.vm->flush();
	REQUIRE(f.applied.size() == 1);
	CHECK(f.applied[0].id == "99");
	CHECK(f.applied[0].field == ":trainUnit");
	CHECK(std::get<std::string>(f.applied[0].value.data) == "GondorSoldier");
	CHECK(std::get<double>(f.applied[0].args[0].data) == 3);
	CHECK(f.run("return bfme.mutations.getStatus(ticket)") == "applied\n");
	f.run("bfme.buildingInstances[99]:trainUnit('GondorSoldier')");
	f.vm->flush();
	CHECK(std::get<double>(f.applied.back().args[0].data) == 1);
	for (const char *code : {
		"bfme.buildingInstances[99]:trainUnit('GondorSoldier',0)",
		"bfme.buildingInstances[99]:trainUnit('GondorSoldier',1.5)",
		"bfme.buildingInstances[99]:trainUnit('GondorSoldier',1001)",
		"bfme.buildingInstances[99]:trainUnit('GondorSoldier',0/0)",
		"bfme.buildingInstances[99]:trainUnit('Missing',1)",
		"bfme.buildingInstances[99]:trainUnit(bfme.buildingTypes.TestBuilding,1)",
		"bfme.unitInstances[1234]:trainUnit('GondorSoldier',1)"})
		CHECK_FALSE(f.vm->execute(code).ok);
	CHECK_FALSE(f.vm->hasPending());
}

TEST_CASE("bfme Lua: libraries are read-only objects reachable through bfme, import and require")
{
	BfmeFixture f;
	CHECK(f.run("return bfme.debug==import('bfme.debug'), bfme.debug==require('bfme.debug'), tostring(bfme.debug)") ==
		  "true\ttrue\tbfme.debug\n");
	CHECK(f.run("return table.concat(bfme.capabilities.libraries,',')") == "ai,console,debug,events,graphics,input,map,match,mutations,orders,selection\n");
	CHECK(f.run("bfme.capabilities.libraries[1]='x'; return bfme.capabilities.libraries[1]") == "ai\n");
	CHECK(f.run("return getmetatable(bfme.selection), getmetatable(bfme), type(bfme.match.getFrame), bfme.debug.missing") ==
		  "bfme library\tbfme module\tfunction\tnil\n");
	CHECK_FALSE(f.vm->execute("bfme.debug.getSelectedBuilding=nil").ok);
	CHECK_FALSE(f.vm->execute("import('bfme.unknown')").ok);
	CHECK(f.run("local n=0; for k,fn in pairs(bfme.mutations) do n=n+1 end; return n") == "2\n");
	CHECK(f.run("ticket=bfme.match.setGameSpeed(2); return bfme.mutations.getStatus(ticket), bfme.mutations:getStatus(ticket)") ==
		  "pending\tpending\n");
	CHECK(f.run("local s=bfme.match.getStatus(); return s.ready, s.playerCount, s.unitCount, bfme.match.getFrame()") == "true\t2\t1\t0\n");
}

TEST_CASE("bfme Lua: debug and selection libraries read the sampled client selection")
{
	BfmeFixture f;
	CHECK(f.run("return bfme.debug.getSelectedBuilding(), bfme.debug.getSelectedUnit(), bfme.selection.count()") == "nil\tnil\t0\n");
	f.snapshot.collections["buildingInstances"]["99"] = {{"id", 99}, {"playerId", 1}, {"typeId", "TestBuilding"}};
	f.snapshot.selection = {5555, 1234, 99}; // 5555 is not a unit/building instance
	CHECK(f.run("return (bfme.debug).getSelectedBuilding()==bfme.buildingInstances[99], bfme.debug:getSelectedBuilding().id") == "true\t99\n");
	CHECK(f.run("return bfme.debug.getSelectedUnit().id, bfme.selection.first().id, bfme.selection.count()") == "1234\t1234\t2\n");
	CHECK(f.run("return #bfme.selection.units(), #bfme.selection.buildings(), bfme.match.getStatus().selectionCount") == "1\t1\t2\n");
	CHECK(f.run("return bfme.debug.inspect(bfme.debug.getSelectedBuilding()).label") == "Building[99]\n");
	CHECK_FALSE(f.vm->execute("bfme.debug.inspect({})").ok);
	f.run("bfme.debug.getSelectedBuilding():trainUnit('GondorSoldier', 2)");
	f.snapshot.selection.clear();
	CHECK(f.run("return bfme.debug.getSelectedBuilding()") == "nil\n");
	CHECK(f.writes == 1);
}

TEST_CASE("bfme Lua: orders go to the owner as one group with validated positions and targets")
{
	BfmeFixture f;
	f.snapshot.collections["unitInstances"]["1235"] = {{"id", 1235}, {"playerId", 1}, {"position", BfmeLuaValue::Table{{"x", 7}, {"y", 8}}}};
	f.snapshot.collections["unitInstances"]["2000"] = {{"id", 2000}, {"playerId", 2}, {"position", BfmeLuaValue::Table{{"x", 50}, {"y", 60}}}};
	f.snapshot.collections["buildingInstances"]["99"] = {{"id", 99}, {"playerId", 1}};
	CHECK(f.run("ticket=bfme.orders.attackMoveTo({bfme.unitInstances[1234],bfme.unitInstances[1235]},{x=100,y=200});return bfme.mutations.getStatus(ticket)") == "pending\n");
	f.run("bfme.unitInstances[1234]:attack(bfme.unitInstances[2000]); bfme.unitInstances[1235]:moveTo(bfme.unitInstances[2000])");
	f.run("bfme.buildingInstances[99]:setRallyPoint({x=1,y=2}); bfme.buildingInstances[99]:researchUpgrade('Upgrade_Forge')");
	f.run("bfme.unitInstances[1234]:useAbility('SpecialAbilityLeap', {x=3,y=4}); bfme.unitInstances[1234]:useAbility('SpecialAbilityHeal')");
	f.vm->flush();
	REQUIRE(f.applied.size() == 7);
	const auto &move = f.applied[0];
	CHECK(move.collection == "orders");
	CHECK(move.id == "1");
	CHECK(move.field == ":attackMoveTo");
	const auto &ids = std::get<BfmeLuaValue::List>(move.args[0].data);
	REQUIRE(ids.size() == 2);
	CHECK(std::get<double>(ids[1].data) == 1235);
	const auto &pos = std::get<BfmeLuaValue::Table>(move.args[1].data);
	CHECK(std::get<double>(pos.at("y").data) == 200);
	CHECK(std::get<double>(f.applied[1].args[1].data) == 2000);              // attack: the target's id
	CHECK(std::get<double>(std::get<BfmeLuaValue::Table>(f.applied[2].args[1].data).at("x").data) == 50); // a handle as a position
	CHECK(f.applied[3].field == ":setRallyPoint");
	CHECK(std::get<std::string>(f.applied[4].args[1].data) == "Upgrade_Forge");
	CHECK(std::get<std::string>(f.applied[5].args[1].data) == "SpecialAbilityLeap");
	CHECK(std::holds_alternative<BfmeLuaValue::Table>(f.applied[5].args[2].data));
	CHECK(std::holds_alternative<std::monostate>(f.applied[6].args[2].data)); // no target
	for (const char *code : {
		"bfme.orders.moveTo({bfme.unitInstances[1234],bfme.unitInstances[2000]},{x=1,y=1})", // two owners
		"bfme.orders.moveTo({},{x=1,y=1})",
		"bfme.unitInstances[1234]:moveTo({x=0/0,y=1})",
		"bfme.unitInstances[1234]:attack({x=1,y=1})",
		"bfme.unitInstances[1234]:setStance(3)",
		"bfme.unitInstances[1234]:sell()",            // buildings only
		"bfme.buildingInstances[99]:moveTo({x=1,y=1})"}) // units only
		CHECK_FALSE(f.vm->execute(code).ok);
	CHECK_FALSE(f.vm->hasPending());
}

TEST_CASE("bfme Lua: the AI switch, player orders and the AI library")
{
	BfmeFixture f;
	f.snapshot.collections["players"]["2"] = {{"id", 2}, {"name", "AI"}, {"hasAI", true}, {"aiEnabled", true},
		{"relations", BfmeLuaValue::Table{{"1", "enemy"}}}};
	f.snapshot.collections["players"]["1"]["relations"] = BfmeLuaValue::Table{{"2", "enemy"}};
	CHECK(f.run("local list=bfme.ai.list(); return #list, list[1].id, bfme.ai.isEnabled(list[1])") == "1\t2\ttrue\n");
	CHECK(f.run("return bfme.players[2]:isEnemy(bfme.players[1]), bfme.players[2]:isAlly(bfme.players[1])") == "true\tfalse\n");
	f.run("bfme.ai.setEnabled(bfme.players[2], false); bfme.players[2]:purchaseScience('SCIENCE_HealingHands'); bfme.players[2]:castPower('SpecialPowerArrowVolley', {x=5,y=6})");
	CHECK_FALSE(f.vm->execute("bfme.players[2]:setAIEnabled('no')").ok);
	f.vm->flush();
	REQUIRE(f.applied.size() == 3);
	CHECK(f.applied[0].collection == "players");
	CHECK(f.applied[0].field == ":setAIEnabled");
	CHECK(std::get<bool>(f.applied[0].value.data) == false);
	CHECK(f.applied[1].field == ":purchaseScience");
	CHECK(std::get<std::string>(f.applied[1].args[0].data) == "SCIENCE_HealingHands");
	CHECK(f.applied[2].id == "2");
	CHECK(std::holds_alternative<BfmeLuaValue::Table>(f.applied[2].args[1].data));
}

TEST_CASE("bfme Lua: queries are read-only engine answers with checked arguments")
{
	BfmeFixture f;
	f.snapshot.collections["buildingInstances"]["99"] = {{"id", 99}, {"playerId", 1}};
	f.answers["trainable"] = BfmeLuaValue::List{BfmeLuaValue::Table{{"unit", "GondorSoldier"}, {"ok", true}, {"status", "ok"}}};
	f.answers["canTrain"] = BfmeLuaValue::Table{{"ok", false}, {"status", "not enough gold"}};
	f.answers["findBuildSite"] = BfmeLuaValue::Table{{"x", 40}, {"y", 80}, {"z", 0}};
	f.snapshot.collections["unitInstances"]["1234"]["position"] = BfmeLuaValue::Table{{"x", 5}, {"y", 6}};
	CHECK(f.run("local t=bfme.buildingInstances[99]:getTrainableUnits(); return #t, t[1].unit, t[1].ok") == "1\tGondorSoldier\ttrue\n");
	CHECK(f.run("return bfme.buildingInstances[99]:canTrain(bfme.unitTypes.GondorSoldier)") == "false\tnot enough gold\n");
	CHECK(f.run("local s=bfme.players[1]:findBuildSite('TestBuilding', bfme.unitInstances[1234]); return s.x, s.y") == "40\t80\n");
	REQUIRE(f.queries.size() == 3);
	CHECK(std::get<double>(f.queries[0].second[0].data) == 99);
	CHECK(std::get<std::string>(f.queries[2].second[1].data) == "TestBuilding");
	CHECK(std::get<double>(f.queries[2].second[3].data) == 600); // the default radius
	CHECK_FALSE(f.vm->execute("bfme.players[1]:findBuildSite('NoSuchBuilding', {x=1,y=1})").ok);
	CHECK_FALSE(f.vm->execute("bfme.buildingInstances[99]:getUpgrades()").ok); // the host has no answer: the error reaches Lua
	CHECK(f.writes == 0);
}

TEST_CASE("bfme Lua: update callbacks run at flush with a fresh snapshot, by name, and report failures")
{
	BfmeFixture f;
	f.run("ticks=0; bfme.events.onUpdate('ai', function(n) ticks=ticks+1; seen=bfme.players[1].money; print('tick', n) end, 50)");
	CHECK(f.vm->hasPending()); // due at the next safe point
	f.snapshot.collections["players"]["1"]["money"] = 777;
	f.vm->flush();
	CHECK(f.run("return ticks, seen") == "1\t777\n");
	CHECK(f.asyncOutput == "tick\tai\n");
	CHECK_FALSE(f.vm->hasPending()); // not due again for 50 ms
	f.vm->flush();
	CHECK(f.run("return ticks") == "1\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	CHECK(f.vm->hasPending());
	f.vm->flush();
	CHECK(f.run("return ticks") == "2\n");
	// a callback that orders: the order runs at the next flush like any other
	f.run("bfme.events.onUpdate('ai', function() bfme.unitInstances[1234]:stop(); error('after the order') end, 50)");
	f.asyncOutput.clear();
	f.vm->flush();
	CHECK(f.asyncOutput.find("bfme update 'ai' failed:") != std::string::npos);
	CHECK(f.run("local l=bfme.events.list(); return #l, l[1].failures") == "1\t1\n");
	f.vm->flush();
	REQUIRE_FALSE(f.applied.empty());
	CHECK(f.applied.back().field == ":stop");
	f.run("bfme.events.remove('ai')");
	CHECK(f.run("return #bfme.events.list()") == "0\n");
	CHECK_FALSE(f.vm->execute("bfme.events.onUpdate('x', function() end, 10)").ok);
}

TEST_CASE("bfme graphics texture overrides queue in menus and validate arguments")
{
 BfmeFixture f;
 f.snapshot.ready = false;
 f.run("ticket = require('bfme.graphics'):setTextureOverride('GBBarracks_Bib.tga', 'res://mods/gondor_plots/gbbarracks_bib.dds')");
 f.vm->flush();
 REQUIRE(f.applied.size() == 1);
 CHECK(f.applied[0].collection == "graphics");
 CHECK(f.applied[0].id == "GBBarracks_Bib.tga");
 CHECK(std::get<std::string>(f.applied[0].value.data) == "res://mods/gondor_plots/gbbarracks_bib.dds");
 f.run("bfme.graphics.setTextureOverride('gbbarracks_bib', nil)");
 f.vm->flush();
 REQUIRE(f.applied.size() == 2);
 CHECK(std::holds_alternative<std::monostate>(f.applied[1].value.data));
 CHECK_FALSE(f.vm->execute("bfme.graphics.setTextureOverride('', 'res://bad.dds')").ok);
 CHECK_FALSE(f.vm->execute("bfme.graphics.setTextureOverride('floor', 42)").ok);
}

TEST_CASE("bfme Lua data: preserves marker properties without exposing engine libraries")
{
 auto data = BfmeLuaRuntime::readData("return {markers={{x=12.5,properties={GenericAIObjectID=7,GenericAIObjectName='_WallHub_A'}}}}", "markers");
 const auto &root = std::get<BfmeLuaValue::Table>(data.data);
 const auto &markers = std::get<BfmeLuaValue::List>(root.at("markers").data);
 REQUIRE(markers.size() == 1);
 const auto &marker = std::get<BfmeLuaValue::Table>(markers[0].data);
 CHECK(std::get<double>(marker.at("x").data) == 12.5);
 const auto &props = std::get<BfmeLuaValue::Table>(marker.at("properties").data);
 CHECK(std::get<double>(props.at("GenericAIObjectID").data) == 7);
 CHECK(std::get<std::string>(props.at("GenericAIObjectName").data) == "_WallHub_A");
 CHECK_THROWS(BfmeLuaRuntime::readData("return io.open('file')", "bad"));
 CHECK_THROWS(BfmeLuaRuntime::readData("return {", "bad"));
}

TEST_CASE("bfme Lua: layout restoration retains plot ID, completion callback and upgrade/link actions")
{
	BfmeFixture f;
	f.run("ticket=bfme.players[1]:construct('TestBuilding',{x=10,y=20,rotation=1.25,plotId=42},2,function(b) done=b.id end)");
	f.vm->flush();
	REQUIRE(f.applied.size() == 1);
	REQUIRE(f.applied[0].args.size() == 7);
	CHECK(std::get<double>(f.applied[0].args[5].data) == 1.25);
	CHECK(std::get<double>(f.applied[0].args[6].data) == 42);
	f.constructionState = "complete";
	f.vm->flush();
	CHECK(f.run("return done") == "777\n");
	f.run("bfme.buildingInstances[777]:giveUpgrade('Upgrade_Test'); bfme.buildingInstances[777]:linkWallPiece(bfme.buildingInstances[777])");
	f.vm->flush();
	REQUIRE(f.applied.size() == 3);
	CHECK(f.applied[1].field == ":giveUpgrade");
	CHECK(std::get<std::string>(f.applied[1].value.data) == "Upgrade_Test");
	CHECK(f.applied[2].field == ":linkWallPiece");
	CHECK(std::get<double>(f.applied[2].value.data) == 777);
}

TEST_CASE("bfme Lua: invulnerability is a validated queued unit and building action")
{
	BfmeFixture f;
	f.snapshot.collections["buildingInstances"]["777"] = {{"id", 777}};
	CHECK(f.run("t=bfme.unitInstances[1234]:setInvulnerable(true);return bfme.mutations.getStatus(t)") == "pending\n");
	CHECK(f.applied.empty());
	f.vm->flush();
	REQUIRE(f.applied.size() == 1);
	CHECK(f.applied.back().collection == "unitInstances");
	CHECK(f.applied.back().field == ":setInvulnerable");
	CHECK(std::get<bool>(f.applied.back().value.data));
	CHECK(f.run("return bfme.mutations.getStatus(t)") == "applied\n");
	f.run("bfme.buildingInstances[777]:setInvulnerable(false)");
	f.vm->flush();
	CHECK(f.applied.back().collection == "buildingInstances");
	CHECK_FALSE(std::get<bool>(f.applied.back().value.data));
	for (const char *value : {"nil", "1", "'true'", "{}"})
		CHECK_FALSE(f.vm->execute(std::string("bfme.unitInstances[1234]:setInvulnerable(") + value + ")").ok);
	CHECK_FALSE(f.vm->hasPending());
	f.run("u=bfme.unitInstances[1234]");
	f.snapshot.collections["unitInstances"].clear();
	CHECK_FALSE(f.vm->execute("u:setInvulnerable(true)").ok);
}
