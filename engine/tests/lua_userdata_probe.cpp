// OpenBFME. GPL-3.0. A separate process for tests/test_lua_runtime.cpp ("independent process" determinism of lua_pushusertag userdata).
//
// usage: lua_userdata_probe <shift>
// The shift changes the heap history of the process (how much is allocated first, and in which order the six pointer targets are allocated), so
// the native addresses of the six logical userdata differ from run to run. The Lua side is the same every time: six lua_pushusertag userdata created in
// the same order, used as table keys, found again by pointer, finalized at lua_close. The output (iteration order of the table, interning results and the
// finalizer order, all in LOGICAL indices) must not depend on the shift.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

static void *g_targets[6];
static std::vector<int> g_finalized;

static int indexOf(void *p)
{
	for (int i = 0; i < 6; ++i)
	{
		if (g_targets[i] == p)
		{
			return i;
		}
	}
	return -1;
}

static int finalizer(lua_State *L)
{
	g_finalized.push_back(indexOf(lua_touserdata(L, 1)));
	return 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	// the test compares the bytes: LF lines, not the CRLF of a text-mode stdout (lane WIN-1)
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	const int shift = argc > 1 ? std::atoi(argv[1]) : 0;
	std::mt19937 rng((unsigned)(shift * 7919 + 1));
	std::vector<void *> noise;
	for (int i = 0; i < 50 + shift * 13; ++i)
	{
		void *p = std::malloc(1 + rng() % 5000);
		if (rng() % 3 == 0)
		{
			noise.push_back(p);
		}
		else
		{
			std::free(p);
		}
	}
	// the six pointer targets, allocated in a shift dependent order with shift dependent sizes
	std::vector<int> order(6);
	std::iota(order.begin(), order.end(), 0);
	std::shuffle(order.begin(), order.end(), rng);
	for (int i : order)
	{
		g_targets[i] = std::malloc(16 + rng() % 2000);
	}
	lua_State *L = lua_open(0x100);
	const int tagA = lua_newtag(L), tagB = lua_newtag(L);
	for (int tag : { tagA, tagB })
	{
		lua_pushcfunction(L, finalizer);
		lua_settagmethod(L, tag, "gc");
	}
	lua_newtable(L); // index 1
	for (int i = 0; i < 6; ++i)
	{
		lua_pushusertag(L, g_targets[i], (i % 2) ? tagA : tagB);
		lua_pushnumber(L, i);
		lua_rawset(L, 1);
	}
	std::printf("iter=");
	lua_pushnil(L);
	while (lua_next(L, 1))
	{
		std::printf("%d", (int)lua_tonumber(L, -1));
		lua_pop(L, 1); // the value; the key stays for lua_next
	}
	std::printf("\n");
	// the same pointer and tag is the same record; LUA_ANYTAG finds it; the other tag is another record
	lua_pushusertag(L, g_targets[0], tagB);
	lua_pushusertag(L, g_targets[0], tagB);
	const int same = lua_equal(L, -1, -2);
	lua_pushusertag(L, g_targets[0], LUA_ANYTAG);
	const int any = lua_equal(L, -1, -2);
	lua_pushusertag(L, g_targets[0], tagA);
	const int other = lua_equal(L, -1, -4);
	std::printf("same=%d anytag=%d othertag=%d\n", same, any, other);
	lua_settop(L, 1);
	lua_pushnil(L);
	lua_setglobal(L, "unused");
	lua_close(L);
	std::printf("gc=");
	for (int i : g_finalized)
	{
		std::printf("%d", i);
	}
	std::printf("\n");
	for (void *p : noise)
	{
		std::free(p);
	}
	return 0;
}
