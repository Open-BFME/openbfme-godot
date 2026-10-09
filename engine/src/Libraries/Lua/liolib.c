/* ALTERED SOURCE VERSION. This file is NOT the original Lua 4.0.1 software:
** it is the pristine file from lua-4.0.1.tar.gz plus the OpenBFME patch layer
** that reproduces EA's fork of Lua (the BFME/RotWK build). The change list,
** the evidence for each change and the diff against the pristine file are in
** engine/src/Libraries/Lua/README.md and engine/src/Libraries/Lua/ea-fork.patch.
** Altered source versions must be plainly marked as such (Lua copyright notice). */
/*
** $Id: liolib.c,v 1.91 2000/10/31 13:10:24 roberto Exp $
** Standard I/O (and system) library
** See Copyright Notice in lua.h
**
** OpenBFME: this is the structure of stock liolib.c (the 11 `iolib' functions, the 9 `iolibtag' functions with
** their IOCtrl upvalue and the two tags created by lua_iolibopen, the global handles _STDIN, _STDOUT, _STDERR,
** _INPUT, _OUTPUT and the `gc' tag method) with every body that touches the host replaced by a refusal:
**
**   TARGET (RotWK game.dat, spec lua-scripting.md 2.4): lua_iolibopen RW 0xB5F820 registers the 11 functions of
**   the table at 0xD0A9A0 (_ERRORMESSAGE = errorfb 0xB5F3E0, clock, date, debug, execute, exit, getenv, remove,
**   rename, setlocale, tmpname) and then opens the file functions through RW 0xB5F6E0. io.debug is EA's console
**   callback (luaB_debug.cpp); the rest is stock.
**   PORT: a file, process, clock, locale or environment function of the host is not available to a script: it
**   raises a Lua error and reports it to the host (stop S-122), because the port is deterministic (no host time, no
**   locale, no files). `errorfb' (the traceback _ERRORMESSAGE) is stock and unchanged. Retail data uses none of
**   these functions (spec 1.5).
*/


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lua_ea.h"
#include "luadebug.h"
#include "lualib.h"


#define INFILE	0
#define OUTFILE 1

typedef struct IOCtrl {
  int ref[2];  /* ref for strings _INPUT/_OUTPUT */
  int iotag;    /* tag for file handles */
  int closedtag;  /* tag for closed handles */
} IOCtrl;



static const char *const filenames[] = {"_INPUT", "_OUTPUT"};

/* OpenBFME: the standard handles are userdata whose value stands for the stream: no file is ever used (every operation refuses), and a
** deterministic value keeps the userdata table's bucket order independent of the C runtime's addresses */
#define EA_HSTDIN ((FILE *)1)
#define EA_HSTDOUT ((FILE *)2)
#define EA_HSTDERR ((FILE *)3)


static void setfilebyname (lua_State *L, IOCtrl *ctrl, FILE *f,
                           const char *name) {
  lua_pushusertag(L, f, ctrl->iotag);
  lua_setglobal(L, name);
}


#define setfile(L,ctrl,f,inout)	(setfilebyname(L,ctrl,f,filenames[inout]))


/* the handles are only the three standard streams, which are never closed */
static int file_collect (lua_State *L) {
  return 0;
}


/* the refusals: every function below is registered under its retail name */
#define REFUSE(fname, name) \
  static int fname (lua_State *L) { \
    luaEA_refuse(L, name); \
    return 0; \
  }

REFUSE(io_appendto, "appendto")
REFUSE(io_close, "closefile")
REFUSE(io_flush, "flush")
REFUSE(io_open, "openfile")
REFUSE(io_read, "read")
REFUSE(io_readfrom, "readfrom")
REFUSE(io_seek, "seek")
REFUSE(io_write, "write")
REFUSE(io_writeto, "writeto")
REFUSE(io_clock, "clock")
REFUSE(io_date, "date")
REFUSE(io_debug, "debug")
REFUSE(io_execute, "execute")
REFUSE(io_exit, "exit")
REFUSE(io_getenv, "getenv")
REFUSE(io_remove, "remove")
REFUSE(io_rename, "rename")
REFUSE(setloc, "setlocale")
REFUSE(io_tmpname, "tmpname")


#define LEVELS1	12	/* size of the first part of the stack */
#define LEVELS2	10	/* size of the second part of the stack */

static int errorfb (lua_State *L) {
  int level = 1;  /* skip level 0 (it's this function) */
  int firstpart = 1;  /* still before eventual `...' */
  lua_Debug ar;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  luaL_addstring(&b, "error: ");
  luaL_addstring(&b, luaL_check_string(L, 1));
  luaL_addstring(&b, "\n");
  while (lua_getstack(L, level++, &ar)) {
    char buff[120];  /* enough to fit following `sprintf's */
    if (level == 2)
      luaL_addstring(&b, "stack traceback:\n");
    else if (level > LEVELS1 && firstpart) {
      /* no more than `LEVELS2' more levels? */
      if (!lua_getstack(L, level+LEVELS2, &ar))
        level--;  /* keep going */
      else {
        luaL_addstring(&b, "       ...\n");  /* too many levels */
        while (lua_getstack(L, level+LEVELS2, &ar))  /* find last levels */
          level++;
      }
      firstpart = 0;
      continue;
    }
    sprintf(buff, "%4d:  ", level-1);
    luaL_addstring(&b, buff);
    lua_getinfo(L, "Snl", &ar);
    switch (*ar.namewhat) {
      case 'g':  case 'l':  /* global, local */
        sprintf(buff, "function `%.50s'", ar.name);
        break;
      case 'f':  /* field */
        sprintf(buff, "method `%.50s'", ar.name);
        break;
      case 't':  /* tag method */
        sprintf(buff, "`%.50s' tag method", ar.name);
        break;
      default: {
        if (*ar.what == 'm')  /* main? */
          sprintf(buff, "main of %.70s", ar.short_src);
        else if (*ar.what == 'C')  /* C function? */
          sprintf(buff, "%.70s", ar.short_src);
        else
          sprintf(buff, "function <%d:%.70s>", ar.linedefined, ar.short_src);
        ar.source = NULL;  /* do not print source again */
      }
    }
    luaL_addstring(&b, buff);
    if (ar.currentline > 0) {
      sprintf(buff, " at line %d", ar.currentline);
      luaL_addstring(&b, buff);
    }
    if (ar.source) {
      sprintf(buff, " [%.70s]", ar.short_src);
      luaL_addstring(&b, buff);
    }
    luaL_addstring(&b, "\n");
  }
  luaL_pushresult(&b);
  lua_getglobal(L, LUA_ALERT);
  if (lua_isfunction(L, -1)) {  /* avoid loop if _ALERT is not defined */
    lua_pushvalue(L, -2);  /* error message */
    lua_rawcall(L, 1, 0);
  }
  return 0;
}



static const struct luaL_reg iolib[] = {
  {LUA_ERRORMESSAGE, errorfb},
  {"clock",     io_clock},
  {"date",     io_date},
  {"debug",    io_debug},
  {"execute",  io_execute},
  {"exit",     io_exit},
  {"getenv",   io_getenv},
  {"remove",   io_remove},
  {"rename",   io_rename},
  {"setlocale", setloc},
  {"tmpname",   io_tmpname}
};


static const struct luaL_reg iolibtag[] = {
  {"appendto", io_appendto},
  {"closefile",   io_close},
  {"flush",     io_flush},
  {"openfile",   io_open},
  {"read",     io_read},
  {"readfrom", io_readfrom},
  {"seek",     io_seek},
  {"write",    io_write},
  {"writeto",  io_writeto}
};


static void openwithcontrol (lua_State *L) {
  IOCtrl *ctrl = (IOCtrl *)lua_newuserdata(L, sizeof(IOCtrl));
  unsigned int i;
  ctrl->iotag = lua_newtag(L);
  ctrl->closedtag = lua_newtag(L);
  for (i=0; i<sizeof(iolibtag)/sizeof(iolibtag[0]); i++) {
    /* put `ctrl' as upvalue for these functions */
    lua_pushvalue(L, -1);
    lua_pushcclosure(L, iolibtag[i].func, 1);
    lua_setglobal(L, iolibtag[i].name);
  }
  /* create references to variable names */
  lua_pushstring(L, filenames[INFILE]);
  ctrl->ref[INFILE] = lua_ref(L, 1);
  lua_pushstring(L, filenames[OUTFILE]);
  ctrl->ref[OUTFILE] = lua_ref(L, 1);
  /* predefined file handles */
  setfile(L, ctrl, EA_HSTDIN, INFILE);
  setfile(L, ctrl, EA_HSTDOUT, OUTFILE);
  setfilebyname(L, ctrl, EA_HSTDIN, "_STDIN");
  setfilebyname(L, ctrl, EA_HSTDOUT, "_STDOUT");
  setfilebyname(L, ctrl, EA_HSTDERR, "_STDERR");
  /* close files when collected */
  lua_pushcclosure(L, file_collect, 1);  /* pops `ctrl' from stack */
  lua_settagmethod(L, ctrl->iotag, "gc");
}


LUALIB_API void lua_iolibopen (lua_State *L) {
  luaL_openl(L, iolib);
  openwithcontrol(L);
}

