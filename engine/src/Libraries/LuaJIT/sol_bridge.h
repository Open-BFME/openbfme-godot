// OpenBFME. GPL-3.0. Lane LUAGD-1: forced into every lua-gdextension / sol2 translation unit (cmake/LuaGDExtension.cmake) in place of
// prefix.h alone.
//
// prefix.h renames LuaJIT's API (lua_getfield -> objit_lua_getfield ...) so it cannot bind to the retail Lua 4 VM. sol2's Lua 5.3
// compatibility layer (sol/compatibility/compat-5.3.h, for LuaJIT's 5.1 API) then redefines some of those names as macros that call
// the name itself, e.g. `#define lua_getfield(L, i, k) (lua_getfield((L), (i), (k)), lua_type((L), -1))`: the redefinition replaces
// the rename, and the inner call names a function that no longer exists. Each of those names is given an internal (static inline)
// function here that forwards to the renamed LuaJIT one, after LuaJIT's headers and before sol2's layer. Internal linkage: nothing
// is exported under a retail Lua 4 name. The names: the self-referential macros of compat-5.3.h's Lua 5.1 branch, and lua_load (compat-5.3.c.h #undefs its
// own lua_load and calls the original).
#pragma once
#include "prefix.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luajit.h>
#ifdef __cplusplus
}
#endif

#undef lua_pushlstring
#undef lua_pushstring
#undef lua_gettable
#undef lua_getfield
#undef lua_rawget
#undef lua_rawgeti
#undef lua_dump
#undef lua_resume
#undef luaL_getmetafield
#undef luaL_newmetatable
#undef lua_load
static inline void lua_pushlstring(lua_State *L, const char *s, size_t l) { objit_lua_pushlstring(L, s, l); }
static inline void lua_pushstring(lua_State *L, const char *s) { objit_lua_pushstring(L, s); }
static inline void lua_gettable(lua_State *L, int idx) { objit_lua_gettable(L, idx); }
static inline void lua_getfield(lua_State *L, int idx, const char *k) { objit_lua_getfield(L, idx, k); }
static inline void lua_rawget(lua_State *L, int idx) { objit_lua_rawget(L, idx); }
static inline void lua_rawgeti(lua_State *L, int idx, int n) { objit_lua_rawgeti(L, idx, n); }
static inline int lua_dump(lua_State *L, lua_Writer writer, void *data) { return objit_lua_dump(L, writer, data); }
static inline int lua_resume(lua_State *L, int narg) { return objit_lua_resume(L, narg); }
static inline int luaL_getmetafield(lua_State *L, int obj, const char *e) { return objit_luaL_getmetafield(L, obj, e); }
static inline int lua_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname) { return objit_lua_load(L, reader, data, chunkname); }
static inline int luaL_newmetatable(lua_State *L, const char *tname) { return objit_luaL_newmetatable(L, tname); }
