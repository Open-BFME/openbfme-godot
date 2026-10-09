// OpenBFME. GPL-3.0. The C functions registered in the two Lua states (spec 3.2, 3.3); see LuaBindings.cpp.

#pragma once

struct lua_State;
class LuaRuntime;

// RW 0x739C25-0x73A0A3: lua_pushcclosure + lua_setglobal for each of the 41 logic functions, in the registration order
void LuaRegisterLogicBindings(lua_State *L);
// RW 0x737686-0x7378CE: the 21 drawable functions
void LuaRegisterDrawableBindings(lua_State *L);

// numeric helpers shared with the tests: RW _ftol (0xA3CFA4): the low 32 bits of the truncated 64 bit integer, 0 for NaN / out of range
int LuaFtol(double d);
