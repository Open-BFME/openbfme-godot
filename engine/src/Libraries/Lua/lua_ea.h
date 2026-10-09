/*
** OpenBFME: the API of EA's fork of Lua 4.0.1 (BFME / RotWK) and of the OpenBFME
** host layer. NEW FILE (not in the original Lua 4.0.1 distribution): see
** engine/src/Libraries/Lua/README.md. Lua itself: see the Copyright Notice in
** lua.h (engine/thirdparty/lua-4.0.1/include/lua.h).
**
** TARGET FACTS (RotWK game.dat on this machine, caveat S-001; the Lua library is byte-identical
** to BFME2 1.06's modulo relocation, spec lua-scripting.md 2.3):
**   * the boolean value is type tag 6: lua_pushboolean RW 0xB5B790 stores tag 6 and the int at
**     value+8; the "to boolean" function RW 0xB5B5C0 returns 0 for nil and for an invalid index,
**     the stored int for tag 6 and 1 for everything else;
**   * a table carries an integer payload `reqsize`: luaH_new RW 0xB649C0 stores it at Hash+0xC,
**     RW 0xB5BB30 creates such a table and pushes it, RW 0xB5B3E0 returns it for a value of tag 4
**     (table) and 0 otherwise; the engine uses it as the object id of an object handle.
*/

#ifndef lua_ea_h
#define lua_ea_h

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EA's added type tag (lua_type returns it; no macro is named in the image: retail switches on the literal 6) */
#ifndef LUA_TBOOLEAN
#define LUA_TBOOLEAN 6
#endif

/* RW 0xB5B790 */
void lua_pushboolean (lua_State *L, int b);
/* RW 0xB5B5C0: nil and invalid indices are 0, a boolean its int, everything else 1 */
int lua_toboolean (lua_State *L, int index);
/* RW 0xB5B3E0: the payload of a table, 0 for anything else */
int lua_toobjid (lua_State *L, int index);
/* RW 0xB5BB30: pushes a new empty table whose payload is `id` */
void lua_newtablewithid (lua_State *L, int id);


/* ---- OpenBFME host layer ----------------------------------------------------------------------------
** Everything the retail libraries do on the host (print, _ALERT, the process/file/clock functions) goes
** through this table, so the engine can report it and tests can observe it. A state without a host
** behaves like retail with the debug gate closed (print and _ALERT drop their text). */
typedef struct lua_EA_Host {
  void *user;
  /* print's logger (RW 0x7355A6, gated by a game option in retail): one call per separator / text / newline */
  void (*log) (void *user, const char *text);
  /* _ALERT (RW 0x734E41: builds "LUA Alert: " + message and drops it). Retail swallows it; the host may record it. */
  void (*alert) (void *user, const char *message);
  /* a library function that retail runs on the host (file, process, clock, locale) and the port refuses: the
  ** function raises a Lua error and reports (stop id, text) here */
  void (*report) (void *user, const char *stop, const char *message);
  /* a place where retail FAULTS (an access violation) and the retail-compatible profile stops the script instead: the Lua error that aborts the
  ** running chunk is raised after this call returns; the host counts it so nothing claims the script completed compatibly */
  void (*fault) (void *user, const char *stop, const char *message);
} lua_EA_Host;

/* profiles (docs/PLAN.md): LUA_EA_PROFILE_RETAIL stops where retail faults, LUA_EA_PROFILE_ENHANCED answers with the repaired behaviour. Default: retail. */
#define LUA_EA_PROFILE_RETAIL 0
#define LUA_EA_PROFILE_ENHANCED 1
void lua_ea_setprofile (lua_State *L, int profile);
int lua_ea_getprofile (lua_State *L);

/* the creation serial of a table or function (0 for anything else): what stands in for its address */
unsigned long lua_ea_serial (lua_State *L, int index);

/* print's logger and the host-affecting functions' refusal (see lua_EA_Host) */
void luaEA_log (lua_State *L, const char *text);
/* raises a Lua error "<function> is not available (acceptance stop S-122)" and reports it to the host; does not return */
void luaEA_refuse (lua_State *L, const char *function);

/* the table must outlive the state; NULL removes it */
void lua_ea_sethost (lua_State *L, const lua_EA_Host *host);
const lua_EA_Host *lua_ea_gethost (lua_State *L);

/* ---- numerics ----------------------------------------------------------------------------------------
** Retail runs the game with the x87 precision control at 24 bits (setFPMode RW 0x440809, PLAN rule 3): the
** VM's `fadd/fsub/fmul/fdiv qword` results are rounded to a 24 bit significand before `fstp qword` stores
** them. LUA_EA_NUM_PC24 reproduces that (exact: one rounding of the infinitely precise result); LUA_EA_NUM_DOUBLE
** is plain IEEE double arithmetic. Default for a new state: LUA_EA_NUM_PC24 (stop S-123 records that the
** precision state during script execution is assumed, not traced). */
#define LUA_EA_NUM_DOUBLE 0
#define LUA_EA_NUM_PC24 1
void lua_ea_setnumericmode (lua_State *L, int mode);
int lua_ea_getnumericmode (lua_State *L);

/* the arithmetic the VM uses (exported for the unit tests); `mode` as above */
double luaEA_add (int mode, double a, double b);
double luaEA_sub (int mode, double a, double b);
double luaEA_mul (int mode, double a, double b);
double luaEA_div (int mode, double a, double b);
/* x87 precision control 24: round the infinitely precise result r + (sign of err) to a 24 bit significand */
double luaEA_round24 (double r, int errsign);

/* the hash of a number key: the low 32 bits of its truncated 64 bit integer value (retail's _ftol) */
unsigned long luaEA_numhash (double n);
/* RW _ftol (0xA3CFA4): the low 32 bits of the truncated 64 bit conversion as an int; 0 for NaN, infinities and |n| >= 2^63 */
int luaEA_ftol (double n);
/* the retail string hash, explicit 32 bit arithmetic (lstring.c): identical on every host */
unsigned int luaEA_stringhash (const char *s, size_t l);
/* MSVCR71 strtoul (RW 0xBD06B8 in tonumber with a base): 32 bit unsigned result; leading white space, a sign (a minus negates modulo 2^32),
** an optional 0x with base 16, overflow gives 0xFFFFFFFF; *endptr is `s` when no digit was read */
unsigned int luaEA_strtoul (const char *s, char **endptr, int base);

/* number to text as MSVCR71's printf("%.16g") prints it (3 digit exponents, 1.#INF, 1.#QNAN, -1.#IND) */
void luaEA_number2str (char *s, double n);
/* number from text as MSVCR71's strtod reads it: [space][sign]digits[.digits][e[sign]digits]; no inf, nan or hex */
double luaEA_str2number (const char *s, char **endptr);

/* the C runtime's rand/srand of MSVCR71 (seed 1 until srand): math.random without host state */
int luaEA_rand (lua_State *L);
void luaEA_srand (lua_State *L, unsigned int seed);

#ifdef __cplusplus
}
#endif

#endif
