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
local snapshot, queue, status, writeLog, clock, newproxy, scripts, constructionResult = _bfmeSnapshot, _bfmeQueue, _bfmeStatus, _bfmeLog, _bfmeClock, _bfmeProxy, _bfmeScripts, _bfmeConstructionResult
_bfmeSnapshot, _bfmeQueue, _bfmeStatus, _bfmeLog, _bfmeClock, _bfmeProxy, _bfmeScripts, _bfmeConstructionResult = nil,nil,nil,nil,nil,nil,nil,nil
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
local function count(kind) local n=0; for _ in pairs(view.collections[kind] or {}) do n=n+1 end; return n end
local proxy
-- A template handle or INI name that must exist in collection `kind`; returns the name.
local function templateName(value, kind, label)
 if type(value)=='userdata' and info[value] then value=(record(value)).id end
 if type(value)~='string' or not (view.collections[kind] or {})[value] then error('unknown '..label,3) end
 return value
end
local function owned(kind, player)
 local _,h=record(player); local id=tonumber(h.id)
 local list={}; for key,r in pairs(view.collections[kind] or {}) do if r.playerId==id then list[#list+1]=proxy(kind,key) end end
 table.sort(list,function(a,b) return a.id<b.id end); return list
end
local function set(p, field, value)
 local r,h=record(p)
 local template=h.kind=='templates' or h.kind=='unitTypes' or h.kind=='buildingTypes'
 if h.kind=='players' then
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
-- Map coordinates are SAGE x/y; terrain supplies z. builderId 0 = the building raises itself.
local function construct(building, position, builderId, playerId, speed, onComplete)
 building=templateName(building,'buildingTypes','building template')
 if type(position)~='table' then error('expected position {x=..., y=...}',2) end
 numeric(position.x,-1e7,1e7,false); numeric(position.y,-1e7,1e7,false)
 local angle=position.rotation or 0; numeric(angle,-1e6,1e6,false)
 numeric(playerId,0,19,true)
 if speed==nil then speed=1 end; numeric(speed,0.01,100,false)
 if onComplete~=nil and type(onComplete)~='function' then error('expected completion function',2) end
 local callback
 if onComplete then callback=function(objectId)
  view=snapshot()
  onComplete(assert(proxy('buildingInstances',objectId),'completed building no longer exists'))
 end end
 return queue('match','',':construct',building,position.x,position.y,builderId,playerId,speed,angle,callback)
end
local function action(name) return function(o) local _,h=record(o); return queue(h.kind,h.id,':'..name,nil) end end
-- Handle methods, by collection; every handle also has :set(field, value) and :inspect().
local methods={
 players={
  setMoney=function(p,amount) return set(p,'money',amount) end,
  addMoney=function(p,amount)
   local r=record(p); numeric(amount,-2147483647,2147483647,true)
   return set(p,'money',r.money+amount)
  end,
  revealMap=function(p) local _,h=record(p); return queue(h.kind,h.id,'revealMap',true) end,
  setFogOfWar=function(p,enabled)
   local _,h=record(p); if type(enabled)~='boolean' then error('expected boolean',2) end
   return queue(h.kind,h.id,'fogOfWar',enabled)
  end,
  units=function(p) return owned('unitInstances',p) end,
  buildings=function(p) return owned('buildingInstances',p) end,
  construct=function(p,building,position,speed,onComplete)
   local _,h=record(p); return construct(building,position,0,tonumber(h.id),speed,onComplete)
  end,
 },
 unitInstances={
  kill=action('kill'), destroy=action('destroy'),
  construct=function(u,building,position,speed,onComplete)
   local r,h=record(u); if r.playerId==nil then error('builder has no owning player',2) end
   return construct(building,position,tonumber(h.id),r.playerId,speed,onComplete)
  end,
 },
 buildingInstances={
  kill=action('kill'), destroy=action('destroy'),
  trainUnit=function(b,unit,count)
   local _,h=record(b)
   unit=templateName(unit,'unitTypes','unit template')
   if count==nil then count=1 end; numeric(count,1,1000,true)
   return queue(h.kind,h.id,':trainUnit',unit,count)
  end,
 },
}
local mt={
 __index=function(p,key)
  local r,h=record(p)
  if key=='set' then return set elseif key=='inspect' then return inspect end
  local method=methods[h.kind] and methods[h.kind][key]
  if method then return method end
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
local root={version='OpenBFME bfme API 0.2',capabilities={luaVersion='5.4.9',libraries={}}}
for kind in pairs(kinds) do
 root[kind]=newproxy({
  __index=function(_,key)
   if kind=='players' and key=='localPlayer' then return view.localPlayerId and proxy(kind,view.localPlayerId) end
   if kind=='players' and key=='neutral' then key=0 end
   return proxy(kind,key)
  end,
  __newindex=function() error('collection entries cannot be replaced',2) end,
  __len=function() return count(kind) end,
  __pairs=function()
   local entries={}; for id in pairs(view.collections[kind] or {}) do entries[tonumber(id) or id]=proxy(kind,id) end
   return next,entries,nil
  end,
  __metatable='bfme collection'
 })
end
-- Libraries: read-only objects reachable as bfme.<name>, import('bfme.<name>') and
-- require('bfme.<name>'). Functions accept both lib.fn(...) and lib:fn(...).
local libraries={}
local function library(name, functions)
 local lib
 local fns={}
 for key,fn in pairs(functions) do
  fns[key]=function(first,...) if first==lib then return fn(...) end; return fn(first,...) end
 end
 lib=newproxy({
  __index=fns,
  __newindex=function() error('bfme.'..name..' is read-only',2) end,
  __pairs=function() local out={}; for k,f in pairs(fns) do out[k]=f end; return next,out,nil end,
  __tostring=function() return 'bfme.'..name end,
  __metatable='bfme library'
 })
 libraries[name]=lib; root[name]=lib; package.loaded['bfme.'..name]=lib
 local list=root.capabilities.libraries; list[#list+1]=name; table.sort(list)
 return lib
end
-- Client selection (newest first; the first entry drives the command bar), sampled with this chunk.
-- Selected objects outside the unit/building collections are skipped.
local function selected(kind)
 local out={}
 for _,id in ipairs(view.selection or {}) do
  local key=tostring(id)
  local p=(kind~='buildingInstances' and proxy('unitInstances',key)) or (kind~='unitInstances' and proxy('buildingInstances',key)) or nil
  if p then out[#out+1]=p end
 end
 return out
end
library('match',{
 getFrame=function() return view.frame or 0 end,
 getTimeMs=clock,
 isPaused=function() return view.paused or false end,
 pause=function() return queue('match','',':pause',nil) end,
 unpause=function() return queue('match','',':unpause',nil) end,
 -- 0 stops time, 1 normal, 100 one hundred times as fast; pause is independent.
 setGameSpeed=function(value) numeric(value,0,100,false); return queue('match','',':setGameSpeed',value) end,
 getStatus=function()
  return {ready=view.ready or false,generation=generation,frame=view.frame or 0,paused=view.paused or false,localPlayerId=view.localPlayerId,
   playerCount=count('players'),templateCount=count('templates'),unitCount=count('unitInstances'),buildingCount=count('buildingInstances'),selectionCount=#selected(nil)}
 end,
})
-- Detached terrain position sampled with this Lua chunk; no simulation mutation.
library('input',{getCursorPosition=function() return copy(view.cursorPosition) end})
library('selection',{
 all=function() return selected(nil) end,
 units=function() return selected('unitInstances') end,
 buildings=function() return selected('buildingInstances') end,
 first=function() return selected(nil)[1] end,
 count=function() return #selected(nil) end,
})
library('mutations',{
 getStatus=status,
 -- The Building a construction ticket created, while it exists in this chunk's snapshot.
 getBuilding=function(ticket) local id=constructionResult(ticket); return id and proxy('buildingInstances',id) end,
})
library('debug',{
 getSelectedBuilding=function() return selected('buildingInstances')[1] end,
 getSelectedUnit=function() return selected('unitInstances')[1] end,
 inspect=function(handle)
  if type(handle)~='userdata' or getmetatable(handle)~='bfme proxy' then error('expected a bfme handle',2) end
  return inspect(handle)
 end,
})
library('console',{
 log=writeLog,
 reload=function()
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
 end,
})
local module
module=newproxy({
 __index=function(_,key) if key=='capabilities' then return copy(root.capabilities) end; return root[key] end,
 __newindex=function() error('bfme is read-only',2) end,
 __pairs=function() local out={}; for k in pairs(root) do out[k]=module[k] end; return next,out,nil end,
 __tostring=function() return 'bfme' end,
 __metatable='bfme module'
})
bfme=module
function import(name)
 if name=='bfme' then return module end
 local lib=type(name)=='string' and libraries[name:match('^bfme%.(.+)$') or '']
 if not lib then error('Unknown module: '..tostring(name),2) end
 return lib
end
package.loaded.bfme=module
package.path=''; package.cpath=''; package.loadlib=nil; package.searchers={package.searchers[1]}
function print(...)
 local parts={}; for i=1,select('#',...) do parts[i]=tostring(select(i,...)):sub(1,65536) end
 writeLog(table.concat(parts,'\t'))
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
		int callback = LUA_NOREF;
	};
	std::deque<Pending> pending;
	struct ConstructionJob { unsigned id; int callback; };
	std::map<unsigned long long, ConstructionJob> constructions;
	std::map<unsigned long long, unsigned> constructionResults;
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
			V::Table t{{"ready", s.ready}, {"paused", s.paused}, {"frame", s.frame}, {"cursorPosition", s.cursorPosition}};
			V::List selection;
			for (auto id : s.selection)
				selection.emplace_back(id);
			t["selection"] = std::move(selection);
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
			if (s.pending.size() + s.constructions.size() >= 128)
				throw std::runtime_error("bfme mutation queue full (128)");
			Mutation m;
			m.collection = luaL_checkstring(L, 1);
			m.id = luaL_checkstring(L, 2);
			m.field = luaL_checkstring(L, 3);
			m.value = scalar(L, 4);
			const bool construction = m.field == ":construct";
			if (construction && (!s.host.construct || !s.host.constructionStatus))
				throw std::runtime_error("construction interface unavailable");
			for (int i = 5; i <= (construction ? 10 : lua_gettop(L)); ++i)
				m.args.push_back(scalar(L, i));
			if (s.host.onMutation)
				s.host.onMutation();
			auto ticket = s.nextTicket++;
			int callback = LUA_NOREF;
			if (construction && lua_isfunction(L, 11))
			{
				lua_pushvalue(L, 11);
				callback = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			s.pending.push_back({ticket, std::move(m), callback});
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
	static int constructionResult(lua_State *L)
	{
		auto &s = self(L);
		auto i = s.constructionResults.find(luaL_checkinteger(L, 1));
		if (i == s.constructionResults.end()) lua_pushnil(L);
		else lua_pushinteger(L, i->second);
		return 1;
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
	m->bind("_bfmeConstructionResult", Impl::constructionResult);
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
	return !m->pending.empty() || !m->constructions.empty();
}
void BfmeLuaRuntime::flush()
{
	// Only consume the queue present on entry. A completion callback can enqueue
	// another request, which runs on the next safe point rather than recursively.
	auto count = m->pending.size();
	while (count--)
	{
		auto p = std::move(m->pending.front());
		m->pending.pop_front();
		std::string error;
		try
		{
			if (p.mutation.field == ":construct")
			{
				unsigned id = m->host.construct(p.mutation);
				m->constructionResults[p.ticket] = id;
				m->constructions[p.ticket] = {id, p.callback};
				p.callback = LUA_NOREF;
			}
			else error = m->host.apply(p.mutation);
		}
		catch (const std::exception &e)
		{
			error = e.what();
		}
		if (p.callback != LUA_NOREF) luaL_unref(m->L, LUA_REGISTRYINDEX, p.callback);
		m->results[p.ticket] = error.empty() ? (p.mutation.field == ":construct" ? "building" : "applied") : error;
	}
	for (auto it = m->constructions.begin(); it != m->constructions.end();)
	{
		const auto ticket = it->first;
		const auto job = it->second;
		std::string state;
		try { state = m->host.constructionStatus(job.id); }
		catch (const std::exception &e) { state = e.what(); }
		if (state == "building") { ++it; continue; }
		it = m->constructions.erase(it);
		m->results[ticket] = state;
		if (state == "complete" && job.callback != LUA_NOREF)
		{
			auto L = m->L;
			m->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
			lua_sethook(L, Impl::hook, LUA_MASKCOUNT, 1000);
			lua_rawgeti(L, LUA_REGISTRYINDEX, job.callback);
			lua_pushinteger(L, job.id);
			if (lua_pcall(L, 1, 0, 0) != LUA_OK)
			{
				const char *error = lua_tostring(L, -1);
				m->results[ticket] = std::string("completion callback failed: ") + (error ? error : "non-string Lua error");
				lua_pop(L, 1);
			}
			lua_sethook(L, nullptr, 0, 0);
		}
		if (job.callback != LUA_NOREF) luaL_unref(m->L, LUA_REGISTRYINDEX, job.callback);
	}
	while (m->constructionResults.size() > 1024) m->constructionResults.erase(m->constructionResults.begin());
}
void BfmeLuaRuntime::invalidate()
{
	for (auto &p : m->pending)
	{
		m->results[p.ticket] = "expired registry";
		if (p.callback != LUA_NOREF) luaL_unref(m->L, LUA_REGISTRYINDEX, p.callback);
	}
	for (auto &entry : m->constructions)
	{
		m->results[entry.first] = "expired registry";
		if (entry.second.callback != LUA_NOREF) luaL_unref(m->L, LUA_REGISTRYINDEX, entry.second.callback);
	}
	m->constructions.clear();
	m->constructionResults.clear();
	m->pending.clear();
	m->call(m->invalidateRef);
}
