/* ALTERED SOURCE VERSION. This file is NOT the original Lua 4.0.1 software:
** it is the pristine file from lua-4.0.1.tar.gz plus the OpenBFME patch layer
** that reproduces EA's fork of Lua (the BFME/RotWK build). The change list,
** the evidence for each change and the diff against the pristine file are in
** engine/src/Libraries/Lua/README.md and engine/src/Libraries/Lua/ea-fork.patch.
** Altered source versions must be plainly marked as such (Lua copyright notice). */
/*
** $Id: lstate.h,v 1.41 2000/10/05 13:00:17 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lobject.h"
#include "lua.h"
#include "luadebug.h"
#include "lua_ea.h"



typedef TObject *StkId;  /* index to stack elements */


/*
** marks for Reference array
*/
#define NONEXT          -1      /* to end the free list */
#define HOLD            -2
#define COLLECTED       -3
#define LOCK            -4


struct Ref {
  TObject o;
  int st;  /* can be LOCK, HOLD, COLLECTED, or next (for free list) */
};


struct lua_longjmp;  /* defined in ldo.c */
struct TM;  /* defined in ltm.h */


typedef struct stringtable {
  int size;
  lint32 nuse;  /* number of elements */
  TString **hash;
} stringtable;



struct lua_State {
  /* thread-specific state */
  StkId top;  /* first free slot in the stack */
  StkId stack;  /* stack base */
  StkId stack_last;  /* last free slot in the stack */
  int stacksize;
  StkId Cbase;  /* base for current C function */
  struct lua_longjmp *errorJmp;  /* current error recover point */
  char *Mbuffer;  /* global buffer */
  size_t Mbuffsize;  /* size of Mbuffer */
  /* global state */
  Proto *rootproto;  /* list of all prototypes */
  Closure *rootcl;  /* list of all closures */
  Hash *roottable;  /* list of all tables */
  stringtable strt;  /* hash table for strings */
  stringtable udt;   /* hash table for udata */
  Hash *gt;  /* table for globals */
  struct TM *TMtable;  /* table for tag methods */
  int last_tag;  /* last used tag in TMtable */
  struct Ref *refArray;  /* locked objects */
  int refSize;  /* size of refArray */
  int refFree;  /* list of free positions in refArray */
  unsigned long GCthreshold;
  unsigned long nblocks;  /* number of `bytes' currently allocated */
  lua_Hook callhook;
  lua_Hook linehook;
  int allowhooks;
  /* OpenBFME additions (no retail counterpart): */
  unsigned long gcserial;  /* creation serial of the last table / closure / userdata: their deterministic hash */
  int eanumeric;  /* LUA_EA_NUM_DOUBLE / LUA_EA_NUM_PC24 (lua_ea.h) */
  const struct lua_EA_Host *eahost;  /* print / _ALERT / host report callbacks (lua_ea.h) */
  int eaprofile;  /* LUA_EA_PROFILE_RETAIL / LUA_EA_PROFILE_ENHANCED */
  unsigned int rngstate;  /* the CRT rand() state of math.random (MSVCR71: seed 1 until srand) */
};


#endif

