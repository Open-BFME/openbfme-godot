/*
** OpenBFME. GPL-3.0. NEW FILE of the Lua patch layer (engine/src/Libraries/Lua/README.md); it is part of the Lua
** library and includes its internal headers, see the Copyright Notice in lua.h for Lua itself.
**
** The host side of EA's Lua build: print's logger, the refusal of host-affecting library functions, the CRT rand
** of MSVCR71.
**
** TARGET FACTS (RotWK game.dat, caveat S-001): print RW 0xB5F950 sends its text through the logger function RW
** 0x7355A6, which writes only when an options byte is set (spec lua-scripting.md 2.6, gap G6: not identified here);
** the CRT rand() of MSVCR71 is `holdrand = holdrand * 214013 + 2531011; return (holdrand >> 16) & 0x7FFF` with
** holdrand 1 until srand.
*/

#include <stdio.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lstate.h"
#include "lua_ea.h"


void luaEA_log (lua_State *L, const char *text) {
  const lua_EA_Host *host = L->eahost;
  if (host && host->log) host->log(host->user, text);
}


void luaEA_refuse (lua_State *L, const char *function) {
  char msg[200];
  const lua_EA_Host *host = L->eahost;
  sprintf(msg, "`%.60s' is not available: it reaches the host's files, processes, clock, locale or environment "
               "(acceptance stop S-122)", function);
  if (host && host->report) host->report(host->user, "S-122", msg);
  luaL_verror(L, "%s", msg);
}


int luaEA_rand (lua_State *L) {
  L->rngstate = L->rngstate * 214013u + 2531011u;
  return (int)((L->rngstate >> 16) & 0x7FFF);
}


void luaEA_srand (lua_State *L, unsigned int seed) {
  L->rngstate = seed;
}
