/* ALTERED SOURCE VERSION. This file is NOT the original Lua 4.0.1 software:
** it is the pristine file from lua-4.0.1.tar.gz plus the OpenBFME patch layer
** that reproduces EA's fork of Lua (the BFME/RotWK build). The change list,
** the evidence for each change and the diff against the pristine file are in
** engine/src/Libraries/Lua/README.md and engine/src/Libraries/Lua/ea-fork.patch.
** Altered source versions must be plainly marked as such (Lua copyright notice). */
/*
** $Id: lstring.c,v 1.45a 2000/10/30 17:49:19 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


/*
** type equivalent to TString, but with maximum alignment requirements
*/
union L_UTString {
  TString ts;
  union L_Umaxalign dummy;  /* ensures maximum alignment for `local' udata */
};



void luaS_init (lua_State *L) {
  L->strt.hash = luaM_newvector(L, 1, TString *);
  L->udt.hash = luaM_newvector(L, 1, TString *);
  L->nblocks += 2*sizeof(TString *);
  L->strt.size = L->udt.size = 1;
  L->strt.nuse = L->udt.nuse = 0;
  L->strt.hash[0] = L->udt.hash[0] = NULL;
}


void luaS_freeall (lua_State *L) {
  LUA_ASSERT(L->strt.nuse==0, "non-empty string table");
  L->nblocks -= (L->strt.size + L->udt.size)*sizeof(TString *);
  luaM_free(L, L->strt.hash);
  LUA_ASSERT(L->udt.nuse==0, "non-empty udata table");
  luaM_free(L, L->udt.hash);
}


/* EA/OpenBFME: retail is a 32 bit build, its `unsigned long' is 32 bits and the hash wraps there; the port uses an explicit 32 bit type so the
** hash (and with it every table's iteration order) is the same on LP64 and LLP64 hosts */
static unsigned int hash_s (const char *s, size_t l) {
  unsigned int h = (unsigned int)l;  /* seed */
  size_t step = (l>>5)|1;  /* if string is too long, don't hash all its chars */
  for (; l>=step; l-=step)
    h = h ^ ((h<<5)+(h>>2)+(unsigned char)*(s++));
  return h;
}


unsigned int luaEA_stringhash (const char *s, size_t l) {
  return hash_s(s, l);
}


void luaS_resize (lua_State *L, stringtable *tb, int newsize) {
  TString **newhash = luaM_newvector(L, newsize, TString *);
  int i;
  for (i=0; i<newsize; i++) newhash[i] = NULL;
  /* rehash */
  for (i=0; i<tb->size; i++) {
    TString *p = tb->hash[i];
    while (p) {  /* for each node in the list */
      TString *next = p->nexthash;  /* save next */
      unsigned long h = (tb == &L->strt) ? p->u.s.hash : p->u.d.hash;
      int h1 = h&(newsize-1);  /* new position */
      LUA_ASSERT(h%newsize == (h&(newsize-1)),
                    "a&(x-1) == a%x, for x power of 2");
      p->nexthash = newhash[h1];  /* chain it in new position */
      newhash[h1] = p;
      p = next;
    }
  }
  luaM_free(L, tb->hash);
  L->nblocks += (newsize - tb->size)*sizeof(TString *);
  tb->size = newsize;
  tb->hash = newhash;
}


static void newentry (lua_State *L, stringtable *tb, TString *ts, int h) {
  ts->nexthash = tb->hash[h];  /* chain new entry */
  tb->hash[h] = ts;
  tb->nuse++;
  if (tb->nuse > (lint32)tb->size && tb->size < MAX_INT/2)  /* too crowded? */
    luaS_resize(L, tb, tb->size*2);
}



TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  unsigned int h = hash_s(str, l);
  int h1 = h & (L->strt.size-1);
  TString *ts;
  for (ts = L->strt.hash[h1]; ts; ts = ts->nexthash) {
    if (ts->len == l && (memcmp(str, ts->str, l) == 0))
      return ts;
  }
  /* not found */
  ts = (TString *)luaM_malloc(L, sizestring(l));
  ts->marked = 0;
  ts->nexthash = NULL;
  ts->len = l;
  ts->u.s.hash = h;
  ts->u.s.constindex = 0;
  memcpy(ts->str, str, l);
  ts->str[l] = 0;  /* ending 0 */
  L->nblocks += sizestring(l);
  newentry(L, &L->strt, ts, h1);  /* insert it on table */
  return ts;
}


TString *luaS_newudata (lua_State *L, size_t s, void *udata) {
  union L_UTString *uts = (union L_UTString *)luaM_malloc(L,
                                (lint32)sizeof(union L_UTString)+s);
  TString *ts = &uts->ts;
  ts->marked = 0;
  ts->nexthash = NULL;
  ts->len = s;
  ts->u.d.tag = 0;
  ts->u.d.value = (s > 0) ? uts+1 : udata;
  /* OpenBFME: EVERY userdata (a lua_newuserdata block and a lua_pushusertag record made from a caller's pointer) hashes its creation serial, never
  ** its address and never the caller's pointer value, so the bucket layout of the udata table, the order the collector visits userdata in
  ** and the iteration order of a table keyed by them are the same on every host and after any heap history. A pointer / tag pair is found
  ** again by luaS_createudata through an equality scan, not through a bucket. */
  ts->u.d.hash = (unsigned int)(++L->gcserial);
  L->nblocks += sizestring(s);
 /* insert it on table */
  newentry(L, &L->udt, ts, ts->u.d.hash & (L->udt.size-1));
  return ts;
}


TString *luaS_createudata (lua_State *L, void *udata, int tag) {
  /* OpenBFME: the records are bucketed by serial (above), so a repeated pointer / tag pair is found by comparing the pointer of every record.
  ** The scan order is bucket by bucket, newest first within a bucket: deterministic, and LUA_ANYTAG keeps meaning "the first record of that pointer
  ** in this order" as in the original, where the chain order was likewise an implementation detail. */
  int i;
  TString *ts;
  for (i=0; i<L->udt.size; i++) {
    for (ts = L->udt.hash[i]; ts; ts = ts->nexthash) {
      if (udata == ts->u.d.value && (tag == ts->u.d.tag || tag == LUA_ANYTAG))
        return ts;
    }
  }
  /* not found */
  ts = luaS_newudata(L, 0, udata);
  if (tag != LUA_ANYTAG)
    ts->u.d.tag = tag;
  return ts;
}


TString *luaS_new (lua_State *L, const char *str) {
  return luaS_newlstr(L, str, strlen(str));
}


TString *luaS_newfixed (lua_State *L, const char *str) {
  TString *ts = luaS_new(L, str);
  if (ts->marked == 0) ts->marked = FIXMARK;  /* avoid GC */
  return ts;
}

