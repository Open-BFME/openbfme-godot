// OpenBFME. GPL-3.0. New file of the Luau patch layer (lane SDK-2; README.md).
#include "lua_openbfme.h"

#include "lapi.h"
#include "lstate.h"
#include "ltable.h"

void lua_openbfme_setlockstep(lua_State* L, int on)
{
    L->global->openbfmelockstep = on ? 1 : 0;
}

int lua_openbfme_lockstep(lua_State* L)
{
    return L->global->openbfmelockstep;
}

void lua_openbfme_resettable(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    api_check(L, o && ttistable(o));
    LuaTable* t = hvalue(o);
    api_check(L, t->readonly == 0 && t->metatable == nullptr);
    luaH_clear(t);              // every value nil: the resizes below keep nothing
    luaH_resizehash(L, t, 0);   // the dummy node, as luaH_new gives; first, so the array's resize allocates no hash part (Sol r2)
    luaH_resizearray(L, t, 0);
}
