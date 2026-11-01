// OpenBFME. GPL-3.0. New file of the Luau patch layer (lane SDK-2; README.md).
#pragma once

#include "lua.h"

// Lockstep mode for a state that runs simulation (sim role) scripts: table keys must be numbers / strings / booleans, no weak tables, no
// addresses (lua_encodepointer returns 0) and the collector is paced by allocation alone. Set it once the libraries are opened (they set metatables and then fill them).
LUA_API void lua_openbfme_setlockstep(lua_State* L, int on);
LUA_API int lua_openbfme_lockstep(lua_State* L);

// lane SDK-2b: empties the table at idx back to what a fresh `{}` is (no array part, no hash part), so refilling it in a given key order
// gives the same layout, and the same `pairs` order, as a new table filled in that order (the sim scripts' sdk.state, LuauScriptState.h)
LUA_API void lua_openbfme_resettable(lua_State* L, int idx);
