/* ALTERED SOURCE VERSION. This file is NOT the original Lua 4.0.1 software:
** it is the pristine file from lua-4.0.1.tar.gz plus the OpenBFME patch layer
** that reproduces EA's fork of Lua (the BFME/RotWK build). The change list,
** the evidence for each change and the diff against the pristine file are in
** engine/src/Libraries/Lua/README.md and engine/src/Libraries/Lua/ea-fork.patch.
** Altered source versions must be plainly marked as such (Lua copyright notice). */
/*
** $Id: ltable.h,v 1.24 2000/08/31 14:08:27 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


#define node(t,i)	(&(t)->node[i])
#define key(n)		(&(n)->key)
#define val(n)		(&(n)->val)

Hash *luaH_new (lua_State *L, int nhash, int reqsize);  /* EA: the extra argument is the table's integer payload */
void luaH_free (lua_State *L, Hash *t);
const TObject *luaH_get (lua_State *L, const Hash *t, const TObject *key);
const TObject *luaH_getnum (const Hash *t, Number key);
const TObject *luaH_getstr (const Hash *t, TString *key);
void luaH_remove (Hash *t, TObject *key);
TObject *luaH_set (lua_State *L, Hash *t, const TObject *key);
Node * luaH_next (lua_State *L, const Hash *t, const TObject *r);
TObject *luaH_setint (lua_State *L, Hash *t, int key);
void luaH_setstrnum (lua_State *L, Hash *t, TString *key, Number val);
unsigned long luaH_hash (lua_State *L, const TObject *key);
const TObject *luaH_getglobal (lua_State *L, const char *name);

/* exported only for debugging */
Node *luaH_mainposition (const Hash *t, const TObject *key);


#endif
