// OpenBFME. GPL-3.0. Lua 5.4 companion; no Lua 4 headers or ABI cross this boundary.
#include "GameLogic/ScriptEngine/BfmeLuaRuntime.h"
#include "Libraries/Lua54/prefix.h"
extern "C"
{
#include "../../../thirdparty/lua-5.4.9/src/lauxlib.h"
#include "../../../thirdparty/lua-5.4.9/src/lua.h"
#include "../../../thirdparty/lua-5.4.9/src/lualib.h"
}
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

namespace
{
using V = BfmeLuaValue;
void push(lua_State *L, const V &v)
{
	if (std::holds_alternative<std::monostate>(v.data))
		lua_pushnil(L);
	else if (auto p = std::get_if<bool>(&v.data))
		lua_pushboolean(L, *p);
	else if (auto p = std::get_if<double>(&v.data))
	{
		if (std::isfinite(*p) && std::floor(*p) == *p && *p >= -9007199254740991.0 && *p <= 9007199254740991.0)
			lua_pushinteger(L, lua_Integer(*p));
		else
			lua_pushnumber(L, *p);
	}
	else if (auto p = std::get_if<std::string>(&v.data))
		lua_pushlstring(L, p->data(), p->size());
	else if (auto p = std::get_if<V::Table>(&v.data))
	{
		lua_newtable(L);
		for (const auto &kv : *p)
		{
			push(L, kv.second);
			lua_setfield(L, -2, kv.first.c_str());
		}
	}
	else if (auto p = std::get_if<V::List>(&v.data))
	{
		lua_newtable(L);
		int i = 0;
		for (const auto &item : *p)
		{
			push(L, item);
			lua_rawseti(L, -2, ++i);
		}
	}
}
V scalar(lua_State *L, int index)
{
	switch (lua_type(L, index))
	{
	case LUA_TNIL:
		return {};
	case LUA_TBOOLEAN:
		return bool(lua_toboolean(L, index));
	case LUA_TNUMBER:
		return double(lua_tonumber(L, index));
	case LUA_TSTRING: {
		size_t n;
		const char *s = lua_tolstring(L, index, &n);
		return std::string(s, n);
	}
	default:
		throw std::runtime_error("expected a scalar value");
	}
}
const char *bootstrap = R"lua(
local snapshot, queue, status, writeLog, clock, newproxy, scripts = _bfmeSnapshot, _bfmeQueue, _bfmeStatus, _bfmeLog, _bfmeClock, _bfmeProxy, _bfmeScripts
_bfmeSnapshot, _bfmeQueue, _bfmeStatus, _bfmeLog, _bfmeClock, _bfmeProxy = nil,nil,nil,nil,nil,nil
_bfmeScripts = nil
local view, generation, cache = {}, 1, {}
local kinds = {templates='Template',unitTypes='UnitType',buildingTypes='BuildingType',players='Player',unitInstances='Unit',buildingInstances='Building'}
local info = setmetatable({}, {__mode='k'})
local function copy(v)
 if type(v) ~= 'table' then return v end
 local out = {}; for k,x in pairs(v) do out[k]=copy(x) end; return out
end
local function record(p)
 local h=info[p]
 if not h or h.generation~=generation or h.expired then error('expired bfme handle',3) end
 local r=view.collections[h.kind] and view.collections[h.kind][h.id]
 if not r then h.expired=true; error('expired bfme handle',3) end
 return r,h
end
local function numeric(v,lo,hi,integer)
 if type(v)~='number' or v~=v or v<lo or v>hi or (integer and v%1~=0) then error('value out of range',3) end
end
local proxy
local function set(p, field, value)
 local r,h=record(p)
 local player=h.kind=='players'
 local template=h.kind=='templates' or h.kind=='unitTypes' or h.kind=='buildingTypes'
 if player then
  if field=='money' or field=='gold' then numeric(value,0,2147483647,true)
  elseif field=='powerPoints' or field=='powerpoint' or field=='commandPoints' or field=='commandPointsBase' or field=='commandPointsCap' or field=='commandPointsUsed' then numeric(value,0,1000000,true)
  elseif field=='commandPointsBonus' then numeric(value,-1000000,1000000,true)
  else error('unsupported or read-only player field: '..tostring(field),2) end
 elseif template then
  if field=='cost' or field=='refund' then numeric(value,0,65535,true)
  elseif field=='buildTime' or field=='trainTime' or field=='visionRange' or field=='shroudClearingRange' then numeric(value,0,1e30,false)
  else error('unsupported or read-only template field: '..tostring(field),2) end
 elseif field=='ownerPlayer' then
  if type(value)=='userdata' then local _,owner=record(value); if owner.kind~='players' then error('expected Player',2) end; value=tonumber(owner.id) end
  numeric(value,0,19,true)
 elseif field=='level' then numeric(value,1,100,true)
 else error('unsupported or read-only instance field: '..tostring(field),2) end
 return queue(h.kind,h.id,field,value)
end
local function inspect(p)
 local r,h=record(p); return {label=kinds[h.kind]..'['..h.id..']',kind=kinds[h.kind],id=r.id,values=copy(r)}
end
local function action(p,name,...)
 local _,h=record(p); return queue(h.kind,h.id,':'..name,nil,...)
end
local mt={
 __index=function(p,key)
  local r,h=record(p)
  if key=='set' then return set elseif key=='inspect' then return inspect end
  if (h.kind=='unitInstances' or h.kind=='buildingInstances') and (key=='kill' or key=='destroy' or key=='destory') then
   return function(self) return action(self,key=='destory' and 'destroy' or key) end
  end
  if key=='ownerPlayer' and r.playerId then return proxy('players',tostring(r.playerId)) end
  if (key=='type' or key=='template') and r.typeId then return proxy(h.kind=='buildingInstances' and 'buildingTypes' or 'unitTypes',r.typeId) end
  return copy(r[key])
 end,
 __newindex=set,
 __pairs=function(p) local r=record(p); return next,copy(r),nil end,
 __tostring=function(p) local _,h=record(p); return kinds[h.kind]..'['..h.id..']' end,
 __metatable='bfme proxy'
}
proxy=function(kind,id)
 id=tostring(id)
 if not (view.collections[kind] and view.collections[kind][id]) then return nil end
 local key=kind..':'..id
 if not cache[key] then local p=newproxy(mt); info[p]={kind=kind,id=id,generation=generation}; cache[key]=p end
 return cache[key]
end
bfme={version='OpenBFME bfme API 0.1',capabilities={players=true,templates=true,instances=true,mutationTickets=true,luaVersion='5.4.9'}}
for kind in pairs(kinds) do
 local collection=newproxy({
  __index=function(_,key)
   if kind=='players' and key=='localPlayer' then return view.localPlayerId and proxy(kind,view.localPlayerId) end
   if kind=='players' and key=='neutral' then key=0 end
   return proxy(kind,key)
  end,
  __newindex=function() error('collection entries cannot be replaced',2) end,
  __len=function() local n=0; for _ in pairs(view.collections[kind] or {}) do n=n+1 end; return n end,
  __pairs=function()
   local entries={}; for id in pairs(view.collections[kind] or {}) do entries[tonumber(id) or id]=proxy(kind,id) end
   return next,entries,nil
  end,
  __call=function(_,player)
   if kind~='unitInstances' and kind~='buildingInstances' then error('only instance collections support owner filtering',2) end
   if type(player)=='userdata' then local _,h=record(player); if h.kind~='players' then error('expected Player',2) end; player=tonumber(h.id) end
   if player~=nil then numeric(player,0,19,true) end
   local list={}; for id,r in pairs(view.collections[kind] or {}) do if player==nil or r.playerId==player then list[#list+1]=proxy(kind,id) end end
   table.sort(list,function(a,b) return a.id<b.id end); return list
  end,
  __metatable='bfme collection'
 }); bfme[kind]=collection
end
function bfme.getLiveDataStatus()
 return {ready=view.ready or false,generation=generation,localPlayerId=view.localPlayerId,templateCount=#bfme.templates,playerCount=#bfme.players,frame=view.frame or 0}
end
function bfme.getInstanceStatus() return {ready=view.ready or false,actionsReady=view.ready or false,generation=generation,unitCount=#bfme.unitInstances,buildingCount=#bfme.buildingInstances} end
bfme.getMutationStatus=status
bfme.log=writeLog
bfme.getTimeMs=clock
function bfme.setGameSpeed(value)
 numeric(value,0,100,false)
 return queue('match','',':setGameSpeed',value)
end
setGameSpeed=bfme.setGameSpeed
function bfme.isPaused() return view.paused or false end
function bfme.pause() return queue('match','',':pause',nil) end
function bfme.unpause() return queue('match','',':unpause',nil) end
function import(name) if name~='bfme' then error('Unknown module: '..tostring(name),2) end; return bfme end
package.loaded.bfme=bfme
console = {}
function console.reload()
 local entries=scripts()
 local chunks={}
 -- Compile every file first; a syntax error leaves the previous definitions running.
 for i,entry in ipairs(entries) do
  local fn,err=load(entry.source,'@'..entry.name,'t',_G)
  if not fn then error(err,2) end
  chunks[i]=fn
 end
 for i,fn in ipairs(chunks) do
  local ok,err=pcall(fn)
  if not ok then error(entries[i].name..': '..tostring(err),2) end
  print('Reloaded '..entries[i].name)
 end
 return #chunks
end
bfme.reloadScripts=console.reload
package.path=''; package.cpath=''; package.loadlib=nil; package.searchers={package.searchers[1]}
function print(...)
 local parts={}; for i=1,select('#',...) do parts[i]=tostring(select(i,...)):sub(1,65536) end
 writeLog(table.concat(parts,'\t'))
end
function GetFrame() return view.frame or 0 end
function ExecuteAction(name,player,amount)
 if name~='PLAYER_SET_MONEY' and name~='PLAYER_GIVE_MONEY' then error('unsupported ExecuteAction: '..tostring(name),2) end
 local p
 if type(player)=='number' then p=bfme.players[player]
 else for _,candidate in pairs(bfme.players) do if candidate.name==player then p=candidate; break end end end
 if not p then error('unknown player: '..tostring(player),2) end
 if name=='PLAYER_GIVE_MONEY' then amount=p.money+amount end
 return p:set('money',amount)
end
function _bfmeRefresh()
 view=snapshot()
 for key,p in pairs(cache) do local h=info[p]; if not (view.collections[h.kind] and view.collections[h.kind][h.id]) then h.expired=true;cache[key]=nil end end
end
function _bfmeInvalidate()
 generation=generation+1;cache={};view={collections={}}
end
_bfmeRefresh()
)lua";
} // namespace

struct BfmeLuaRuntime::Impl
{
	Host host;
	lua_State *L = nullptr;
	std::string output;
	unsigned long long nextTicket = 1;
	struct Pending
	{
		unsigned long long ticket;
		Mutation mutation;
	};
	std::deque<Pending> pending;
	std::map<unsigned long long, std::string> results;
	std::chrono::steady_clock::time_point deadline;
	int refreshRef = LUA_NOREF, invalidateRef = LUA_NOREF, printRef = LUA_NOREF;
	static Impl &self(lua_State *L)
	{
		return *static_cast<Impl *>(lua_touserdata(L, lua_upvalueindex(1)));
	}
	static int proxy(lua_State *L)
	{
		lua_newuserdatauv(L, 0, 0);
		lua_pushvalue(L, 1);
		lua_setmetatable(L, -2);
		return 1;
	}
	static int scripts(lua_State *L)
	{
		try
		{
			V::List entries;
			if (self(L).host.scripts)
				for (const auto &entry : self(L).host.scripts())
					entries.emplace_back(V::Table{{"name", entry.first}, {"source", entry.second}});
			push(L, entries);
			return 1;
		}
		catch (const std::exception &error) { lua_pushstring(L, error.what()); }
		return lua_error(L);
	}
	static int snapshot(lua_State *L)
	{
		try
		{
			auto s = self(L).host.snapshot();
			V::Table t{{"ready", s.ready}, {"paused", s.paused}, {"frame", s.frame}};
			if (s.localPlayerId >= 0)
				t["localPlayerId"] = s.localPlayerId;
			V::Table all;
			for (auto &c : s.collections)
			{
				V::Table records;
				for (auto &r : c.second)
					records[r.first] = r.second;
				all[c.first] = std::move(records);
			}
			t["collections"] = std::move(all);
			push(L, t);
			return 1;
		}
		catch (const std::exception &e)
		{
			lua_pushstring(L, e.what());
		}
		return lua_error(L);
	}
	static int queue(lua_State *L)
	{
		try
		{
			auto &s = self(L);
			if (s.pending.size() >= 128)
				throw std::runtime_error("bfme mutation queue full (128)");
			Mutation m;
			m.collection = luaL_checkstring(L, 1);
			m.id = luaL_checkstring(L, 2);
			m.field = luaL_checkstring(L, 3);
			m.value = scalar(L, 4);
			for (int i = 5; i <= lua_gettop(L); ++i)
				m.args.push_back(scalar(L, i));
			if (s.host.onMutation)
				s.host.onMutation();
			auto ticket = s.nextTicket++;
			s.pending.push_back({ticket, std::move(m)});
			s.results[ticket] = "pending";
			while (s.results.size() > 1024)
				s.results.erase(s.results.begin());
			lua_pushinteger(L, ticket);
			return 1;
		}
		catch (const std::exception &e)
		{
			lua_pushstring(L, e.what());
		}
		return lua_error(L);
	}
	static int status(lua_State *L)
	{
		auto &s = self(L);
		auto i = s.results.find(luaL_checkinteger(L, 1));
		if (i == s.results.end())
			lua_pushnil(L);
		else
			lua_pushstring(L, i->second.c_str());
		return 1;
	}
	static int log(lua_State *L)
	{
		size_t n;
		const char *s = luaL_checklstring(L, 1, &n);
		auto &out = self(L).output;
		if (out.size() < 65536)
		{
			out.append(s, std::min(n, size_t(65536 - out.size())));
			out += '\n';
		}
		return 0;
	}
	static int clock(lua_State *L)
	{
		lua_pushinteger(L,
						std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		return 1;
	}
	static void hook(lua_State *L, lua_Debug *)
	{
		Impl *s;
		lua_getfield(L, LUA_REGISTRYINDEX, "bfme.runtime");
		s = static_cast<Impl *>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		if (std::chrono::steady_clock::now() > s->deadline)
			luaL_error(L, "console execution limit exceeded");
	}
	void bind(const char *name, lua_CFunction fn)
	{
		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, fn, 1);
		lua_setglobal(L, name);
	}
	void call(int reference)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, reference);
		if (lua_pcall(L, 0, 0, 0) != LUA_OK)
		{
			const char *text = lua_tostring(L, -1);
			std::string e = text ? text : "non-string Lua error";
			lua_pop(L, 1);
			throw std::runtime_error(e);
		}
	}
	~Impl()
	{
		if (L)
			lua_close(L);
	}
};
BfmeLuaRuntime::BfmeLuaRuntime(Host host) : m(new Impl)
{
	m->host = std::move(host);
	m->L = luaL_newstate();
	if (!m->L)
		throw std::bad_alloc();
	auto L = m->L;
	// Load only portable libraries. OS, IO, native modules and debug hooks are not exposed.
	for (auto lib : std::vector<std::pair<const char *, lua_CFunction>>{{"_G", luaopen_base},
																		{"table", luaopen_table},
																		{"string", luaopen_string},
																		{"math", luaopen_math},
																		{"utf8", luaopen_utf8},
																		{"package", luaopen_package}})
	{
		luaL_requiref(L, lib.first, lib.second, 1);
		lua_pop(L, 1);
	}
	lua_pushnil(L);
	lua_setglobal(L, "dofile");
	lua_pushnil(L);
	lua_setglobal(L, "loadfile");
	m->bind("_bfmeProxy", Impl::proxy);
	m->bind("_bfmeScripts", Impl::scripts);
	m->bind("_bfmeSnapshot", Impl::snapshot);
	m->bind("_bfmeQueue", Impl::queue);
	m->bind("_bfmeStatus", Impl::status);
	m->bind("_bfmeLog", Impl::log);
	m->bind("_bfmeClock", Impl::clock);
	lua_pushlightuserdata(L, m.get());
	lua_setfield(L, LUA_REGISTRYINDEX, "bfme.runtime");
	if (luaL_dostring(L, bootstrap) != LUA_OK)
		throw std::runtime_error(lua_tostring(L, -1));
	lua_getglobal(L, "_bfmeRefresh");
	m->refreshRef = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushnil(L);
	lua_setglobal(L, "_bfmeRefresh");
	lua_getglobal(L, "_bfmeInvalidate");
	m->invalidateRef = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushnil(L);
	lua_setglobal(L, "_bfmeInvalidate");
	lua_getglobal(L, "print");
	m->printRef = luaL_ref(L, LUA_REGISTRYINDEX);
}
BfmeLuaRuntime::~BfmeLuaRuntime() = default;
BfmeLuaRuntime::Result BfmeLuaRuntime::execute(const std::string &code)
{
	auto L = m->L;
	lua_settop(L, 0);
	m->output.clear();
	Result r;
	try
	{
		m->call(m->refreshRef);
	}
	catch (const std::exception &e)
	{
		r.output = e.what();
		return r;
	}
	m->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
	lua_sethook(L, Impl::hook, LUA_MASKCOUNT, 1000);
	std::string chunk = code;
	const bool explicitExpression = !chunk.empty() && chunk.front() == '=';
	if (explicitExpression) chunk = chunk.substr(1);
	const std::string expression = "return " + chunk;
	int status = luaL_loadbufferx(L, expression.data(), expression.size(), "=console", "t");
	if (status != LUA_OK && !explicitExpression)
	{
		lua_pop(L, 1); // Only compilation is retried; a command is never executed twice.
		status = luaL_loadbufferx(L, chunk.data(), chunk.size(), "=console", "t");
	}
	if (status == LUA_OK)
		status = lua_pcall(L, 0, LUA_MULTRET, 0);
	lua_sethook(L, nullptr, 0, 0);
	r.ok = status == LUA_OK;
	if (!r.ok)
	{
		m->output += (lua_tostring(L, -1) ? lua_tostring(L, -1) : "non-string Lua error");
		m->output += '\n';
	}
	else
	{
		const int n = lua_gettop(L);
		// tostring metamethods run under a protected call, with the same execution budget.
		if (n > 0)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, m->printRef);
			lua_insert(L, 1);
			lua_sethook(L, Impl::hook, LUA_MASKCOUNT, 1000);
			if (lua_pcall(L, n, 0, 0) != LUA_OK)
			{
				r.ok = false;
				m->output += (lua_tostring(L, -1) ? lua_tostring(L, -1) : "non-string Lua error");
			}
			lua_sethook(L, nullptr, 0, 0);
		}
	}
	r.output = std::move(m->output);
	lua_settop(L, 0);
	return r;
}
bool BfmeLuaRuntime::hasPending() const
{
	return !m->pending.empty();
}
void BfmeLuaRuntime::flush()
{
	while (!m->pending.empty())
	{
		auto p = std::move(m->pending.front());
		m->pending.pop_front();
		std::string error;
		try
		{
			error = m->host.apply(p.mutation);
		}
		catch (const std::exception &e)
		{
			error = e.what();
		}
		m->results[p.ticket] = error.empty() ? "applied" : error;
	}
}
void BfmeLuaRuntime::invalidate()
{
	for (auto &p : m->pending)
		m->results[p.ticket] = "expired registry";
	m->pending.clear();
	m->call(m->invalidateRef);
}
