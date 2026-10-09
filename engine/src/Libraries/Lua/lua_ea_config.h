/*
** OpenBFME. GPL-3.0. NEW FILE of the Lua patch layer (engine/src/Libraries/Lua/README.md).
** Force-included (-include / /FI) into every C translation unit of the Lua library.
**
** Retail's Lua runs in the "C" locale of MSVCR71: character classes cover ASCII only, strcoll is strcmp, and nothing a
** host (Godot, the C runtime of another platform) does with setlocale can change what a script sees. The ctype macros
** and strcoll of this build are therefore locale independent, defined after the standard headers (whose include
** guards keep them from being redefined).
*/

#ifndef lua_ea_config_h
#define lua_ea_config_h

#include <ctype.h>
#include <string.h>

#ifdef _MSC_VER
#define LUAEA_INLINE static __inline
#else
#define LUAEA_INLINE static __inline__ __attribute__((unused))
#endif

#undef isalpha
#undef isalnum
#undef isdigit
#undef isspace
#undef iscntrl
#undef ispunct
#undef islower
#undef isupper
#undef isxdigit
#undef isprint
#undef isgraph
#undef tolower
#undef toupper

LUAEA_INLINE int luaEA_c_isdigit(int c) { return c >= '0' && c <= '9'; }
LUAEA_INLINE int luaEA_c_islower(int c) { return c >= 'a' && c <= 'z'; }
LUAEA_INLINE int luaEA_c_isupper(int c) { return c >= 'A' && c <= 'Z'; }
LUAEA_INLINE int luaEA_c_isalpha(int c) { return luaEA_c_islower(c) || luaEA_c_isupper(c); }
LUAEA_INLINE int luaEA_c_isalnum(int c) { return luaEA_c_isalpha(c) || luaEA_c_isdigit(c); }
LUAEA_INLINE int luaEA_c_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
LUAEA_INLINE int luaEA_c_iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
LUAEA_INLINE int luaEA_c_isprint(int c) { return c >= 32 && c < 127; }
LUAEA_INLINE int luaEA_c_isgraph(int c) { return c > 32 && c < 127; }
LUAEA_INLINE int luaEA_c_ispunct(int c) { return luaEA_c_isgraph(c) && !luaEA_c_isalnum(c); }
LUAEA_INLINE int luaEA_c_isxdigit(int c) { return luaEA_c_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
LUAEA_INLINE int luaEA_c_tolower(int c) { return luaEA_c_isupper(c) ? c - 'A' + 'a' : c; }
LUAEA_INLINE int luaEA_c_toupper(int c) { return luaEA_c_islower(c) ? c - 'a' + 'A' : c; }

#define isalpha(c) luaEA_c_isalpha(c)
#define isalnum(c) luaEA_c_isalnum(c)
#define isdigit(c) luaEA_c_isdigit(c)
#define isspace(c) luaEA_c_isspace(c)
#define iscntrl(c) luaEA_c_iscntrl(c)
#define ispunct(c) luaEA_c_ispunct(c)
#define islower(c) luaEA_c_islower(c)
#define isupper(c) luaEA_c_isupper(c)
#define isxdigit(c) luaEA_c_isxdigit(c)
#define isprint(c) luaEA_c_isprint(c)
#define isgraph(c) luaEA_c_isgraph(c)
#define tolower(c) luaEA_c_tolower(c)
#define toupper(c) luaEA_c_toupper(c)

#undef strcoll
#define strcoll(a, b) strcmp((a), (b))

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#endif
